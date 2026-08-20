#include "gte.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using clean_aot::gte::Command;
using clean_aot::gte::ExecutionReceipt;
using clean_aot::gte::Gte;
using clean_aot::gte::RegisterBank;
using clean_aot::gte::TimedWrite;
using clean_aot::gte::TimedWriteEffect;

constexpr std::uint32_t bit(unsigned index) {
    return std::uint32_t{1} << index;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::uint32_t command_word(Command command,
                           bool sf = true,
                           bool lm = false,
                           std::uint8_t matrix = 0U,
                           std::uint8_t vector = 0U,
                           std::uint8_t translation = 0U,
                           std::uint8_t fake = 0U) {
    return static_cast<std::uint32_t>(command) |
           (sf ? bit(19U) : 0U) |
           ((static_cast<std::uint32_t>(matrix) & 3U) << 17U) |
           ((static_cast<std::uint32_t>(vector) & 3U) << 15U) |
           ((static_cast<std::uint32_t>(translation) & 3U) << 13U) |
           (lm ? bit(10U) : 0U) |
           ((static_cast<std::uint32_t>(fake) & 0x1fU) << 20U);
}

std::int32_t signed_word(std::uint32_t word) {
    return static_cast<std::int32_t>(word);
}

void write_identity(Gte& gte, std::uint8_t base) {
    gte.write_control(base, 0x00001000U);
    gte.write_control(static_cast<std::uint8_t>(base + 1U), 0U);
    gte.write_control(static_cast<std::uint8_t>(base + 2U), 0x00001000U);
    gte.write_control(static_cast<std::uint8_t>(base + 3U), 0U);
    gte.write_control(static_cast<std::uint8_t>(base + 4U), 0x00001000U);
}

void write_vector(Gte& gte, std::uint8_t base, std::int16_t x, std::int16_t y,
                  std::int16_t z) {
    gte.write_data(base, static_cast<std::uint32_t>(
                              static_cast<std::uint16_t>(x)) |
                   (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y))
                    << 16U));
    gte.write_data(static_cast<std::uint8_t>(base + 1U),
                   static_cast<std::uint32_t>(static_cast<std::uint16_t>(z)));
}

void test_register_transfers_and_fifo() {
    Gte gte;
    write_vector(gte, 0U, static_cast<std::int16_t>(-2), 3, 4);
    require(gte.read_data(0U) == 0x0003fffeU, "packed V0 transfer");
    require(signed_word(gte.read_data(1U)) == 4, "sign-expanded VZ0 transfer");

    gte.write_data(12U, 0x00020001U);
    gte.write_data(13U, 0x00040003U);
    gte.write_data(14U, 0x00060005U);
    gte.write_data(15U, 0x00080007U);
    require(gte.read_data(12U) == 0x00040003U, "SXYP moves SXY1 to SXY0");
    require(gte.read_data(13U) == 0x00060005U, "SXYP moves SXY2 to SXY1");
    require(gte.read_data(14U) == 0x00080007U, "SXYP writes new SXY2");
    gte.write_data(14U, 0x000a0009U);
    require(gte.read_data(12U) == 0x00040003U,
            "direct SXY write does not move FIFO");
    require(gte.read_data(14U) == 0x000a0009U, "direct SXY write");

    gte.write_control(58U, 0x0000f234U);
    require(gte.read_control(58U) == 0xfffff234U,
            "H readback sign-extension quirk");
    gte.write_control(59U, 0xffff8001U);
    require(gte.read_control(59U) == 0xffff8001U, "DQA signed transfer");
    gte.write_control(63U, 0x00555000U);
    require((gte.read_control(63U) & 0x00555000U) == 0x00555000U,
            "writable FLAG bits survive transfer");

    gte.write_data(28U, 0x000003ffU);
    require(gte.read_data(9U) == 0x00000f80U, "IRGB red expansion");
    require(gte.read_data(10U) == 0x00000f80U, "IRGB green expansion");
    require(gte.read_data(11U) == 0U, "IRGB blue expansion");
    require(gte.read_data(29U) == 0x000003ffU, "ORGB collapse and mirror");

    gte.write_data(30U, 0x00f00000U);
    require(gte.read_data(31U) == 8U, "LZCR leading zero count");
    gte.write_data(30U, 0xfff00000U);
    require(gte.read_data(31U) == 12U, "LZCR leading one count");
    gte.write_data(30U, 0U);
    require(gte.read_data(31U) == 32U, "LZCR zero count");
}

void test_decode_and_cycle_metadata() {
    const std::array<std::pair<Command, std::uint8_t>, 22U> expected = {{
        {Command::Rtps, 15U},   {Command::Nclip, 8U}, {Command::Op, 6U},
        {Command::Dpcs, 8U},    {Command::Intpl, 8U}, {Command::Mvmva, 8U},
        {Command::Ncds, 19U},   {Command::Cdp, 13U},  {Command::Ncdt, 44U},
        {Command::Nccs, 17U},   {Command::Cc, 11U},   {Command::Ncs, 14U},
        {Command::Nct, 30U},    {Command::Sqr, 5U},   {Command::Dcpl, 8U},
        {Command::Dpct, 17U},   {Command::Avsz3, 5U}, {Command::Avsz4, 6U},
        {Command::Rtpt, 23U},   {Command::Gpf, 5U},   {Command::Gpl, 5U},
        {Command::Ncct, 39U},
    }};
    for (const auto& item : expected) {
        require(Gte::cycles(item.first) == item.second,
                std::string("cycle metadata for ") + Gte::command_name(item.first));
        const std::uint32_t raw = command_word(item.first, false, true, 2U, 3U,
                                               1U, 0x1fU);
        const auto decoded = Gte::decode(raw);
        require(decoded.command == item.first, "real opcode ignores fake opcode");
        require(!decoded.shift_fraction && decoded.limit_negative,
                "sf/lm decode");
        require(decoded.matrix_selector == 2U && decoded.vector_selector == 3U &&
                    decoded.translation_selector == 1U,
                "MVMVA selector decode");
    }
    Gte gte;
    const ExecutionReceipt unknown = gte.execute(0x00000000U);
    require(!unknown.recognized && unknown.cycles == 0U,
            "unknown command is reported without mutation");
}

void test_mvmva_selectors_and_saturation() {
    Gte gte;
    write_identity(gte, 32U);
    write_identity(gte, 40U);
    write_identity(gte, 48U);
    write_vector(gte, 0U, 2, 3, 4);
    gte.write_control(37U, 100U);
    gte.write_control(38U, 200U);
    gte.write_control(39U, 300U);
    const ExecutionReceipt receipt = gte.execute(
        command_word(Command::Mvmva, true, false, 0U, 0U, 0U));
    require(receipt.recognized && receipt.cycles == 8U, "MVMVA receipt");
    require(signed_word(gte.read_data(9U)) == 102 &&
                signed_word(gte.read_data(10U)) == 203 &&
                signed_word(gte.read_data(11U)) == 304,
            "MVMVA rotation and translation");

    gte.reset();
    gte.write_control(32U, 0x00002000U);
    gte.write_control(33U, 0x10000000U);
    gte.write_control(34U, 0U);
    gte.write_control(35U, 0U);
    gte.write_control(36U, 0U);
    gte.write_data(9U, 2U);
    gte.write_data(10U, 3U);
    gte.write_data(11U, 4U);
    gte.execute(command_word(Command::Mvmva, true, false, 0U, 3U, 3U));
    require(signed_word(gte.read_data(9U)) == 4 &&
                signed_word(gte.read_data(10U)) == 2 &&
                signed_word(gte.read_data(11U)) == 0,
            "MVMVA IR vector is latched before the pipeline writes IR");

    gte.reset();
    write_identity(gte, 48U);
    write_vector(gte, 0U, 2, 3, 4);
    gte.write_control(45U, 0x10000000U);
    gte.write_control(46U, 0x20000000U);
    gte.write_control(47U, 0x30000000U);
    gte.execute(command_word(Command::Mvmva, true, false, 2U, 0U, 2U));
    require(signed_word(gte.read_data(9U)) == 0 &&
                signed_word(gte.read_data(10U)) == 3 &&
                signed_word(gte.read_data(11U)) == 4,
            "MVMVA far-color selector uses documented two-term quirk");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 32767, 32767, 32767);
    const ExecutionReceipt positive = gte.execute(
        command_word(Command::Mvmva, false, true, 0U, 0U, 3U));
    require(signed_word(gte.read_data(9U)) == 32767 &&
                signed_word(gte.read_data(10U)) == 32767 &&
                signed_word(gte.read_data(11U)) == 32767,
            "positive IR lower-limit saturation");
    require((positive.flag & (bit(24U) | bit(23U) | bit(22U))) ==
                (bit(24U) | bit(23U) | bit(22U)),
            "positive IR saturation flags");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, static_cast<std::int16_t>(-32768),
                 static_cast<std::int16_t>(-32768),
                 static_cast<std::int16_t>(-32768));
    const ExecutionReceipt negative = gte.execute(
        command_word(Command::Mvmva, false, false, 0U, 0U, 3U));
    require(signed_word(gte.read_data(9U)) == -32768 &&
                signed_word(gte.read_data(10U)) == -32768 &&
                signed_word(gte.read_data(11U)) == -32768,
            "negative IR saturation");
    require((negative.flag & (bit(24U) | bit(23U) | bit(22U))) != 0U,
            "negative IR saturation flags");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 32767, 32767, 32767);
    gte.write_control(37U, 0x7fffffffU);
    const ExecutionReceipt mac_overflow = gte.execute(
        command_word(Command::Mvmva, true, false, 0U, 0U, 0U));
    require((mac_overflow.flag & bit(30U)) != 0U &&
                signed_word(gte.read_data(9U)) == -32768,
            "MAC1 wraps signed 44-bit transient before IR saturation");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 0, 0, 0);
    gte.write_control(37U, 1000000U);
    gte.execute(command_word(Command::Mvmva, false, false, 0U, 0U, 0U));
    require(gte.read_data(25U) == 0xf4240000U &&
                gte.snapshot().mac[1] == static_cast<std::int32_t>(-198967296),
            "MAC1 persists as architectural S32 after wide MVMVA");
    gte.write_data(8U, 0U);
    gte.execute(command_word(Command::Gpl, false, false));
    require(gte.read_data(25U) == 0xf4240000U &&
                signed_word(gte.read_data(9U)) == -32768,
            "GPL reads the architectural S32 MAC1 register");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 0, 0, static_cast<std::int16_t>(-1));
    const ExecutionReceipt rtps_lm = gte.execute(
        command_word(Command::Rtps, true, true));
    require(signed_word(gte.read_data(11U)) == 0 &&
                (rtps_lm.flag & bit(22U)) == 0U,
            "RTPS IR3 storage honors lm while FLAG.22 uses lm zero");
}

void test_projection_and_reciprocal_edges() {
    Gte gte;
    write_identity(gte, 32U);
    write_vector(gte, 0U, 10, 20, 200);
    gte.write_control(37U, 0U);
    gte.write_control(38U, 0U);
    gte.write_control(39U, 0U);
    gte.write_control(56U, 0U);
    gte.write_control(57U, 0U);
    gte.write_control(58U, 100U);
    gte.write_control(59U, 0U);
    gte.write_control(60U, 0U);
    const ExecutionReceipt ordinary = gte.execute(
        command_word(Command::Rtps, true, false));
    require(ordinary.cycles == 15U && gte.read_data(14U) == 0x000a0005U,
            "RTPS screen FIFO ordinary projection");
    require(gte.read_data(19U) == 200U, "RTPS SZ3 ordinary projection");
    require(signed_word(gte.read_data(8U)) == 0,
            "RTPS depth cue with zero DQA/DQB");
    require((ordinary.flag & (bit(17U) | bit(31U))) == 0U,
            "ordinary reciprocal has no overflow");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 10, 20, 200);
    gte.write_control(58U, 401U);
    const ExecutionReceipt overflow = gte.execute(
        command_word(Command::Rtps, true, false));
    require((overflow.flag & bit(17U)) != 0U &&
                (overflow.flag & bit(31U)) != 0U,
            "near-plane reciprocal overflow flags");
    require(gte.read_data(14U) == 0x00270013U,
            "saturated reciprocal produces deterministic screen result");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 1, 1, 0);
    gte.write_control(58U, 1U);
    const ExecutionReceipt zero_z = gte.execute(
        command_word(Command::Rtps, true, false));
    require((zero_z.flag & bit(17U)) != 0U, "zero denominator overflow");
    require(gte.read_data(19U) == 0U, "negative/zero Z clamps to zero");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 1, 0, 32767);
    gte.write_control(39U, 32768U);
    gte.write_control(58U, 65535U);
    const ExecutionReceipt final_table = gte.execute(
        command_word(Command::Rtps, true, false));
    require((final_table.flag & bit(17U)) == 0U &&
                (gte.read_data(14U) & 0xffffU) == 0U &&
                gte.read_data(19U) == 65535U,
            "reciprocal final table entry changes observable SX without overflow");

    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, 1, 0, 32767);
    gte.write_control(39U, 32768U);
    gte.write_control(58U, 65535U);
    gte.write_control(56U, 0x7fffffffU);
    const ExecutionReceipt mac0_overflow = gte.execute(
        command_word(Command::Rtps, true, false));
    require((mac0_overflow.flag & bit(16U)) != 0U &&
                (gte.read_data(14U) & 0xffffU) == 0xfc00U,
            "RTPS projection consumes signed S32 MAC0 after overflow");
}

void test_timed_input_latches() {
    const auto h_boundary = Gte::input_latch_boundary(
        Command::Rtps, RegisterBank::Control, 58U);
    require(h_boundary.measured && h_boundary.read && h_boundary.nop_boundary == 1U,
            "RTPS H latch boundary is measured and nonzero");
    const auto ncs_vz = Gte::input_latch_boundary(
        Command::Nccs, RegisterBank::Data, 1U);
    require(ncs_vz.measured && ncs_vz.read && ncs_vz.nop_boundary == 1U,
            "NCCS VZ latch boundary is not transcribed backwards");
    const auto dpcs_ir0 = Gte::input_latch_boundary(
        Command::Dpcs, RegisterBank::Data, 8U);
    const auto dpct_ir0 = Gte::input_latch_boundary(
        Command::Dpct, RegisterBank::Data, 8U);
    require(dpcs_ir0.measured && dpcs_ir0.nop_boundary == 1U &&
                dpct_ir0.measured && dpct_ir0.nop_boundary == 4U,
            "depth-cue IR0 latch boundaries match the timing table");
    const auto avsz3_sz0 = Gte::input_latch_boundary(
        Command::Avsz3, RegisterBank::Data, 16U);
    require(avsz3_sz0.measured && !avsz3_sz0.read,
            "AVSZ3 does not read SZ0");
    const std::array<Command, 5U> code_gap_commands = {
        Command::Ncs, Command::Nct, Command::Dpct, Command::Intpl, Command::Gpl,
    };
    for (const Command command : code_gap_commands) {
        const auto boundary = Gte::input_latch_boundary(
            command, RegisterBank::Data, 6U);
        require(!boundary.measured,
                std::string("RGBC code-field timing gap remains unmeasured for ") +
                    Gte::command_name(command));
    }
    const std::array<Command, 3U> triple_lighting = {
        Command::Nct, Command::Ncct, Command::Ncdt,
    };
    const std::array<std::array<std::uint8_t, 6U>, 3U> triple_vectors = {{
        {{0U, 2U, 0U, 1U, 1U, 3U}},
        {{0U, 2U, 0U, 1U, 3U, 3U}},
        {{0U, 0U, 0U, 1U, 3U, 4U}},
    }};
    for (unsigned command_index = 0U; command_index < triple_lighting.size();
         ++command_index) {
        for (std::uint8_t data_index = 0U; data_index < 6U; ++data_index) {
            const auto boundary = Gte::input_latch_boundary(
                triple_lighting[command_index], RegisterBank::Data, data_index);
            require(boundary.measured && boundary.read &&
                        boundary.nop_boundary == triple_vectors[command_index][data_index],
                    "triple-lighting vector latch table is indexed by register");
        }
        require(!Gte::input_latch_boundary(
                     triple_lighting[command_index], RegisterBank::Data, 6U)
                     .measured &&
                    !Gte::input_latch_boundary(
                         triple_lighting[command_index], RegisterBank::Data, 8U)
                         .measured,
                "triple-lighting RGBC and IR0 gaps remain unmeasured");
    }
    require(!Gte::input_latch_boundary(
                 Command::Mvmva, RegisterBank::Data, 0U)
                 .measured,
            "command-only MVMVA timing query fails closed");
    require(Gte::timed_write_effect(
                Command::Mvmva,
                TimedWrite{RegisterBank::Data, 0U, 1U, 0U}) ==
                TimedWriteEffect::Unmeasured,
            "command-only MVMVA write query fails closed");
    const auto mvmva_matrix = Gte::input_latch_boundary(
        Gte::decode(command_word(Command::Mvmva, true, false, 1U, 0U, 1U)),
        RegisterBank::Control, 40U);
    require(!mvmva_matrix.measured,
            "MVMVA selected-matrix max boundary is not misreported as exact");

    const std::array<TimedWrite, 1U> early_write = {{
        {RegisterBank::Control, 58U, 200U, 0U},
    }};
    require(Gte::timed_write_effect(
                Gte::decode(command_word(Command::Rtps, true, false)),
                early_write[0]) ==
                TimedWriteEffect::MayAffectInput,
            "RTPS sees H written before its latch boundary");

    const std::array<TimedWrite, 1U> late_write = {{
        {RegisterBank::Control, 58U, 200U, 1U},
    }};
    require(Gte::timed_write_effect(
                Gte::decode(command_word(Command::Rtps, true, false)),
                late_write[0]) ==
                TimedWriteEffect::NoEffect,
            "RTPS ignores H written at its latch boundary");

    const std::array<TimedWrite, 1U> unsupported_write = {{
        {RegisterBank::Data, 0U, 1U, 0U},
    }};
    require(Gte::timed_write_effect(
                Gte::decode(command_word(Command::Mvmva, true, false, 2U, 0U, 2U)),
                unsupported_write[0]) == TimedWriteEffect::Unmeasured,
            "unmeasured MVMVA latch shape fails closed");
}

void test_negative_intpl_arithmetic() {
    Gte gte;
    gte.write_data(8U, 0U);
    gte.write_data(9U, static_cast<std::uint32_t>(-1));
    gte.write_data(10U, static_cast<std::uint32_t>(-2));
    gte.write_data(11U, static_cast<std::uint32_t>(-3));
    const ExecutionReceipt receipt = gte.execute(
        command_word(Command::Intpl, true, false));
    require(receipt.recognized && signed_word(gte.read_data(9U)) == -1 &&
                signed_word(gte.read_data(10U)) == -2 &&
                signed_word(gte.read_data(11U)) == -3,
            "INTPL negative IR base uses defined signed fixed-point arithmetic");
}

void test_math_fifos_and_flags() {
    Gte gte;
    gte.write_data(12U, 0x00000000U);
    gte.write_data(13U, 0x0000000aU);
    gte.write_data(14U, 0x000a0000U);
    const ExecutionReceipt clip = gte.execute(command_word(Command::Nclip));
    require(signed_word(gte.read_data(24U)) == 100,
            "NCLIP signed area");
    require(clip.flag == 0U, "NCLIP ordinary FLAG");

    gte.reset();
    gte.write_data(16U, 400U);
    gte.write_data(17U, 100U);
    gte.write_data(18U, 200U);
    gte.write_data(19U, 300U);
    gte.write_control(61U, 0x1000U);
    gte.write_control(62U, 0x1000U);
    gte.execute(command_word(Command::Avsz3));
    require(gte.read_data(7U) == 600U && signed_word(gte.read_data(24U)) ==
                0x258000,
            "AVSZ3 average and MAC0");
    gte.execute(command_word(Command::Avsz4));
    require(gte.read_data(7U) == 1000U, "AVSZ4 average");

    gte.reset();
    gte.write_data(17U, 65535U);
    gte.write_data(18U, 65535U);
    gte.write_data(19U, 65535U);
    gte.write_control(61U, 0x8000U);
    const ExecutionReceipt avsz_mac0_overflow = gte.execute(
        command_word(Command::Avsz3));
    require((avsz_mac0_overflow.flag & bit(15U)) != 0U &&
                gte.read_data(7U) == 0U &&
                gte.read_data(24U) == ((std::uint32_t{0x8000U} << 16U) | 0x18000U),
            "AVSZ3 clamps negative architectural MAC0");

    gte.reset();
    gte.write_data(17U, 65535U);
    gte.write_data(18U, 65535U);
    gte.write_data(19U, 1U);
    gte.write_control(61U, 0x8000U);
    const ExecutionReceipt avsz_mac0_low_wrap = gte.execute(
        command_word(Command::Avsz3));
    require((avsz_mac0_low_wrap.flag & bit(15U)) != 0U &&
                gte.read_data(7U) == 8U &&
                gte.read_data(24U) == 0x00008000U,
            "AVSZ3 consumes signed low-32 MAC0 after overflow");

    gte.reset();
    gte.write_data(9U, 3U);
    gte.write_data(10U, 4U);
    gte.write_data(11U, 5U);
    write_identity(gte, 32U);
    gte.execute(command_word(Command::Sqr, true));
    require(signed_word(gte.read_data(9U)) == 0 &&
                signed_word(gte.read_data(10U)) == 0 &&
                signed_word(gte.read_data(11U)) == 0,
            "SQR fixed-point shift");
    gte.write_data(9U, 192U);
    gte.write_data(10U, 256U);
    gte.write_data(11U, 320U);
    gte.execute(command_word(Command::Sqr, true));
    require(signed_word(gte.read_data(9U)) == 9 &&
                signed_word(gte.read_data(10U)) == 16 &&
                signed_word(gte.read_data(11U)) == 25,
            "SQR Q12 square");

    gte.reset();
    gte.write_data(12U, 0x7fff7fffU);
    gte.write_data(13U, 0x7fff8000U);
    gte.write_data(14U, bit(31U) | 0x7fffU);
    const ExecutionReceipt large_clip = gte.execute(command_word(Command::Nclip));
    require((large_clip.flag & (bit(16U) | bit(15U))) != 0U,
            "MAC0 overflow flags are retained");
}

void test_color_commands_and_fifo() {
    Gte gte;
    write_identity(gte, 48U);
    gte.write_data(6U, 0x00204080U);
    gte.write_data(9U, 1000U);
    gte.write_data(10U, 2000U);
    gte.write_data(11U, 3000U);
    const ExecutionReceipt cc = gte.execute(command_word(Command::Cc, true, true));
    require(cc.cycles == 11U && gte.read_data(22U) == 0x00171f1fU,
            "CC modulation and color FIFO");
    require(gte.read_data(9U) == 500U && gte.read_data(10U) == 500U &&
                gte.read_data(11U) == 375U,
            "CC final IR values");

    gte.reset();
    gte.write_data(6U, 0x00102040U);
    gte.write_data(20U, 0x03020100U);
    gte.write_data(21U, 0x06050403U);
    gte.write_data(22U, 0x09080706U);
    gte.write_control(53U, 0U);
    gte.write_control(54U, 0U);
    gte.write_control(55U, 0U);
    gte.write_data(8U, 0U);
    const ExecutionReceipt dpct = gte.execute(command_word(Command::Dpct, true));
    require(dpct.cycles == 17U && gte.read_data(20U) == 0x00020100U,
            "DPCT updates RGB0 from bottom FIFO");
    require(gte.read_data(21U) == 0x00050403U &&
                gte.read_data(22U) == 0x00080706U,
            "DPCT updates all three color stages");

    gte.reset();
    gte.write_data(6U, 0x00ffffffU);
    const ExecutionReceipt color_overflow = gte.execute(
        command_word(Command::Dpcs, false, false));
    require((color_overflow.flag & (bit(21U) | bit(20U) | bit(19U))) ==
                (bit(21U) | bit(20U) | bit(19U)),
            "color FIFO saturation flags");
}

void write_far_color(Gte& gte, std::int32_t red, std::int32_t green,
                     std::int32_t blue) {
    gte.write_control(53U, static_cast<std::uint32_t>(red));
    gte.write_control(54U, static_cast<std::uint32_t>(green));
    gte.write_control(55U, static_cast<std::uint32_t>(blue));
}

void test_command_family_vectors() {
    Gte op;
    write_identity(op, 32U);
    op.write_data(9U, 2U);
    op.write_data(10U, 3U);
    op.write_data(11U, 4U);
    op.execute(command_word(Command::Op, true, false));
    require(signed_word(op.read_data(9U)) == 1 &&
                signed_word(op.read_data(10U)) == -2 &&
                signed_word(op.read_data(11U)) == 1,
            "OP cross-product vector");

    Gte ncs;
    write_identity(ncs, 40U);
    write_identity(ncs, 48U);
    write_vector(ncs, 0U, 16, 32, 48);
    ncs.write_data(6U, 0xaa000000U);
    ncs.execute(command_word(Command::Ncs, true, false));
    require(signed_word(ncs.read_data(9U)) == 16 &&
                signed_word(ncs.read_data(10U)) == 32 &&
                signed_word(ncs.read_data(11U)) == 48 &&
                ncs.read_data(22U) == 0xaa030201U,
            "NCS lighting and CODE output");

    Gte nct;
    write_identity(nct, 40U);
    write_identity(nct, 48U);
    write_vector(nct, 0U, 16, 0, 0);
    write_vector(nct, 2U, 32, 0, 0);
    write_vector(nct, 4U, 48, 0, 0);
    nct.write_data(6U, 0xbb000000U);
    nct.execute(command_word(Command::Nct, true, false));
    require(nct.read_data(20U) == 0xbb000001U &&
                nct.read_data(21U) == 0xbb000002U &&
                nct.read_data(22U) == 0xbb000003U,
            "NCT triple lighting FIFO");

    Gte nccs;
    write_identity(nccs, 40U);
    write_identity(nccs, 48U);
    write_vector(nccs, 0U, 256, 512, 768);
    nccs.write_data(6U, 0xdd101010U);
    nccs.execute(command_word(Command::Nccs, true, false));
    require(nccs.read_data(22U) == 0xdd030201U &&
                signed_word(nccs.read_data(9U)) == 16 &&
                signed_word(nccs.read_data(10U)) == 32 &&
                signed_word(nccs.read_data(11U)) == 48,
            "NCCS normal-color modulation");

    Gte ncct;
    write_identity(ncct, 40U);
    write_identity(ncct, 48U);
    write_vector(ncct, 0U, 256, 0, 0);
    write_vector(ncct, 2U, 512, 0, 0);
    write_vector(ncct, 4U, 768, 0, 0);
    ncct.write_data(6U, 0xee101010U);
    ncct.execute(command_word(Command::Ncct, true, false));
    require(ncct.read_data(20U) == 0xee000001U &&
                ncct.read_data(21U) == 0xee000002U &&
                ncct.read_data(22U) == 0xee000003U,
            "NCCT triple normal-color FIFO");

    Gte ncds;
    write_identity(ncds, 40U);
    write_identity(ncds, 48U);
    write_vector(ncds, 0U, 256, 512, 768);
    ncds.write_data(6U, 0xf1010101U);
    ncds.write_data(8U, 0x1000U);
    write_far_color(ncds, 16, 32, 48);
    ncds.execute(command_word(Command::Ncds, true, false));
    require(ncds.read_data(22U) == 0xf1030201U &&
                signed_word(ncds.read_data(9U)) == 16 &&
                signed_word(ncds.read_data(10U)) == 32 &&
                signed_word(ncds.read_data(11U)) == 48,
            "NCDS depth-cued lighting");

    Gte ncdt;
    write_identity(ncdt, 40U);
    write_identity(ncdt, 48U);
    write_vector(ncdt, 0U, 256, 0, 0);
    write_vector(ncdt, 2U, 512, 0, 0);
    write_vector(ncdt, 4U, 768, 0, 0);
    ncdt.write_data(6U, 0xf2010101U);
    ncdt.write_data(8U, 0x1000U);
    write_far_color(ncdt, 16, 32, 48);
    ncdt.execute(command_word(Command::Ncdt, true, false));
    require(ncdt.read_data(20U) == 0xf2030201U &&
                ncdt.read_data(21U) == 0xf2030201U &&
                ncdt.read_data(22U) == 0xf2030201U,
            "NCDT triple depth-cued FIFO");

    Gte cdp;
    write_identity(cdp, 48U);
    cdp.write_data(6U, 0xf3010101U);
    cdp.write_data(8U, 0x1000U);
    cdp.write_data(9U, 256U);
    cdp.write_data(10U, 512U);
    cdp.write_data(11U, 768U);
    write_far_color(cdp, 16, 32, 48);
    cdp.execute(command_word(Command::Cdp, true, false));
    require(cdp.read_data(22U) == 0xf3030201U &&
                signed_word(cdp.read_data(9U)) == 16 &&
                signed_word(cdp.read_data(10U)) == 32 &&
                signed_word(cdp.read_data(11U)) == 48,
            "CDP color-depth cue");

    Gte dcpl;
    dcpl.write_data(6U, 0xf6010101U);
    dcpl.write_data(8U, 0x1000U);
    dcpl.write_data(9U, 256U);
    dcpl.write_data(10U, 512U);
    dcpl.write_data(11U, 768U);
    write_far_color(dcpl, 16, 32, 48);
    dcpl.execute(command_word(Command::Dcpl, true, false));
    require(dcpl.read_data(22U) == 0xf6030201U,
            "DCPL depth-cued color");

    Gte dpcs;
    dpcs.write_data(6U, 0xf4102030U);
    dpcs.write_data(8U, 0x1000U);
    write_far_color(dpcs, 16, 32, 48);
    dpcs.execute(command_word(Command::Dpcs, true, false));
    require(dpcs.read_data(22U) == 0xf4030201U,
            "DPCS depth cue");

    Gte intpl;
    intpl.write_data(6U, 0xf5010101U);
    intpl.write_data(8U, 0x1000U);
    intpl.write_data(9U, 256U);
    intpl.write_data(10U, 512U);
    intpl.write_data(11U, 768U);
    write_far_color(intpl, 16, 32, 48);
    intpl.execute(command_word(Command::Intpl, true, false));
    require(intpl.read_data(22U) == 0xf5030201U,
            "INTPL depth interpolation");

    Gte gpf;
    gpf.write_data(6U, 0xf7000000U);
    gpf.write_data(8U, 0x1000U);
    gpf.write_data(9U, 256U);
    gpf.write_data(10U, 512U);
    gpf.write_data(11U, 768U);
    gpf.execute(command_word(Command::Gpf, true, false));
    require(gpf.read_data(22U) == 0xf7302010U,
            "GPF interpolation from zero base");

    Gte gpl;
    gpl.write_data(6U, 0xf8000000U);
    gpl.write_data(8U, 0x1000U);
    gpl.write_data(9U, 256U);
    gpl.write_data(10U, 512U);
    gpl.write_data(11U, 768U);
    gpl.write_data(25U, 16U);
    gpl.write_data(26U, 32U);
    gpl.write_data(27U, 48U);
    gpl.execute(command_word(Command::Gpl, true, false));
    require(gpl.read_data(22U) == 0xf8332211U &&
                signed_word(gpl.read_data(9U)) == 272 &&
                signed_word(gpl.read_data(10U)) == 544 &&
                signed_word(gpl.read_data(11U)) == 816,
            "GPL interpolation with architectural MAC base");
}

std::uint32_t next_random(std::uint32_t& value) {
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    return value;
}

void configure_identity_case(Gte& gte, std::int16_t x, std::int16_t y,
                             std::int16_t z, std::int32_t tx,
                             std::int32_t ty, std::int32_t tz) {
    gte.reset();
    write_identity(gte, 32U);
    write_vector(gte, 0U, x, y, z);
    gte.write_control(37U, static_cast<std::uint32_t>(tx));
    gte.write_control(38U, static_cast<std::uint32_t>(ty));
    gte.write_control(39U, static_cast<std::uint32_t>(tz));
}

void test_deterministic_metamorphic_vectors() {
    std::uint32_t seed = 0x13579bdfU;
    for (unsigned iteration = 0U; iteration < 512U; ++iteration) {
        const auto bounded = [&seed]() -> std::int16_t {
            const std::uint32_t value = next_random(seed) % 1001U;
            return static_cast<std::int16_t>(
                static_cast<std::int32_t>(value) - 500);
        };
        const std::int16_t x = bounded();
        const std::int16_t y = bounded();
        const std::int16_t z = bounded();
        const std::int32_t tx = static_cast<std::int32_t>(bounded());
        const std::int32_t ty = static_cast<std::int32_t>(bounded());
        const std::int32_t tz = static_cast<std::int32_t>(bounded());

        Gte positive;
        configure_identity_case(positive, x, y, z, tx, ty, tz);
        const std::uint32_t raw = command_word(Command::Mvmva, true, false,
                                               0U, 0U, 0U, 0U);
        const ExecutionReceipt first = positive.execute(raw);
        require(signed_word(positive.read_data(9U)) ==
                    static_cast<std::int32_t>(x) + tx,
                "identity MVMVA X metamorphic vector");
        require(signed_word(positive.read_data(10U)) ==
                    static_cast<std::int32_t>(y) + ty,
                "identity MVMVA Y metamorphic vector");
        require(signed_word(positive.read_data(11U)) ==
                    static_cast<std::int32_t>(z) + tz,
                "identity MVMVA Z metamorphic vector");

        Gte repeat;
        configure_identity_case(repeat, x, y, z, tx, ty, tz);
        const ExecutionReceipt second = repeat.execute(
            command_word(Command::Mvmva, true, false, 0U, 0U, 0U, 0x1fU));
        require(first.flag == second.flag &&
                    first.state_digest == second.state_digest,
                "fake opcode bits do not affect deterministic state");

        Gte negated;
        configure_identity_case(negated, static_cast<std::int16_t>(-x),
                                static_cast<std::int16_t>(-y),
                                static_cast<std::int16_t>(-z), -tx, -ty, -tz);
        const ExecutionReceipt negated_receipt = negated.execute(raw);
        require(negated_receipt.flag == first.flag,
                "sign inversion preserves ordinary flags");
        require(signed_word(negated.read_data(9U)) ==
                    -signed_word(positive.read_data(9U)) &&
                    signed_word(negated.read_data(10U)) ==
                        -signed_word(positive.read_data(10U)) &&
                    signed_word(negated.read_data(11U)) ==
                        -signed_word(positive.read_data(11U)),
                "identity MVMVA sign metamorphism");
    }
    std::cout << "EVIDENCE randomized_vectors=512 deterministic=1\n";
}

void test_triple_fifo_order() {
    Gte gte;
    write_identity(gte, 32U);
    write_vector(gte, 0U, 1, 2, 100);
    write_vector(gte, 2U, 3, 4, 100);
    write_vector(gte, 4U, 5, 6, 100);
    gte.write_data(16U, 1U);
    gte.write_data(17U, 2U);
    gte.write_data(18U, 3U);
    gte.write_data(19U, 4U);
    gte.write_control(58U, 100U);
    const ExecutionReceipt receipt = gte.execute(
        command_word(Command::Rtpt, true, false));
    require(receipt.cycles == 23U, "RTPT cycle metadata");
    require(gte.read_data(12U) == 0x00020001U &&
                gte.read_data(13U) == 0x00040003U &&
                gte.read_data(14U) == 0x00060005U,
            "RTPT SXY FIFO order");
    require(gte.read_data(16U) == 4U && gte.read_data(17U) == 100U &&
                gte.read_data(18U) == 100U && gte.read_data(19U) == 100U,
            "RTPT SZ FIFO order");
}

void test_complete_command_set_smoke() {
    const std::array<Command, 22U> commands = {
        Command::Rtps,  Command::Nclip, Command::Op,   Command::Dpcs,
        Command::Intpl, Command::Mvmva, Command::Ncds, Command::Cdp,
        Command::Ncdt,  Command::Nccs,  Command::Cc,   Command::Ncs,
        Command::Nct,   Command::Sqr,   Command::Dcpl, Command::Dpct,
        Command::Avsz3, Command::Avsz4, Command::Rtpt, Command::Gpf,
        Command::Gpl,   Command::Ncct,
    };
    for (const Command command : commands) {
        Gte gte;
        write_identity(gte, 32U);
        write_identity(gte, 40U);
        write_identity(gte, 48U);
        write_vector(gte, 0U, 1, 2, 3);
        write_vector(gte, 2U, 4, 5, 6);
        write_vector(gte, 4U, 7, 8, 9);
        gte.write_data(6U, 0x00102040U);
        gte.write_data(8U, 0x0800U);
        gte.write_data(9U, 0x0100U);
        gte.write_data(10U, 0x0200U);
        gte.write_data(11U, 0x0300U);
        gte.write_data(16U, 100U);
        gte.write_data(17U, 200U);
        gte.write_data(18U, 300U);
        gte.write_data(19U, 400U);
        gte.write_control(58U, 100U);
        gte.write_control(61U, 0x1000U);
        gte.write_control(62U, 0x1000U);
        const ExecutionReceipt receipt =
            gte.execute(command_word(command, true, false));
        require(receipt.recognized && receipt.cycles == Gte::cycles(command),
                std::string("complete command smoke: ") +
                    Gte::command_name(command));
    }
}

} // namespace

int main() {
    test_register_transfers_and_fifo();
    test_decode_and_cycle_metadata();
    test_mvmva_selectors_and_saturation();
    test_projection_and_reciprocal_edges();
    test_timed_input_latches();
    test_negative_intpl_arithmetic();
    test_math_fifos_and_flags();
    test_color_commands_and_fifo();
    test_command_family_vectors();
    test_triple_fifo_order();
    test_complete_command_set_smoke();
    test_deterministic_metamorphic_vectors();
    std::cout << "PASS: clean GTE register, command, saturation, reciprocal, "
                 "FIFO, timing, and metamorphic semantic coverage\n";
    return EXIT_SUCCESS;
}
