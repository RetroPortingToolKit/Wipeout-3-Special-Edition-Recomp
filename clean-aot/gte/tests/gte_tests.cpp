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
    require((mac_overflow.flag & bit(30U)) != 0U,
            "MAC1 overflow checks the pre-shift accumulator");
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
    test_math_fifos_and_flags();
    test_color_commands_and_fifo();
    test_triple_fifo_order();
    test_complete_command_set_smoke();
    test_deterministic_metamorphic_vectors();
    std::cout << "PASS: clean GTE register, command, saturation, reciprocal, "
                 "FIFO, timing, and metamorphic semantic coverage\n";
    return EXIT_SUCCESS;
}
