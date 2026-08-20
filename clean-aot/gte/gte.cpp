#include "gte.hpp"

#include <cstddef>
#include <limits>

namespace clean_aot::gte {

namespace {

constexpr std::uint32_t kFlagMac0Positive = 1U << 16U;
constexpr std::uint32_t kFlagMac0Negative = 1U << 15U;
constexpr std::uint32_t kFlagDivideOverflow = 1U << 17U;
constexpr std::uint32_t kFlagDivideSummary = 1U << 31U;
constexpr std::uint32_t kFlagSz3 = 1U << 18U;
constexpr std::uint32_t kFlagIr0 = 1U << 12U;

constexpr std::uint32_t kFlagIr[3U] = {
    1U << 24U,
    1U << 23U,
    1U << 22U,
};

constexpr std::uint32_t kFlagColor[3U] = {
    1U << 21U,
    1U << 20U,
    1U << 19U,
};

constexpr std::uint32_t kFlagMacPositive[3U] = {
    1U << 30U,
    1U << 29U,
    1U << 28U,
};

constexpr std::uint32_t kFlagMacNegative[3U] = {
    1U << 27U,
    1U << 26U,
    1U << 25U,
};

constexpr std::uint32_t kFlagSx = 1U << 14U;
constexpr std::uint32_t kFlagSy = 1U << 13U;

constexpr std::uint32_t kFlagSummaryInputs = 0x7f87e000U;

constexpr std::uint32_t kFNVOffset = 2166136261U;
constexpr std::uint32_t kFNVPrime = 16777619U;

std::uint32_t fnv_step(std::uint32_t hash, std::uint8_t byte) noexcept {
    return (hash ^ static_cast<std::uint32_t>(byte)) * kFNVPrime;
}

} // namespace

Gte::Gte() noexcept {
    reset();
}

void Gte::reset() noexcept {
    vectors_ = {};
    rgbc_ = 0U;
    otz_ = 0U;
    ir0_ = 0;
    ir_ = {};
    sxy_ = {};
    sz_ = {};
    rgb_ = {};
    res1_ = 0U;
    mac_ = {};
    lzcs_ = 0U;
    lzcr_ = 0U;
    rotation_ = {};
    translation_ = {};
    light_ = {};
    background_ = {};
    color_ = {};
    far_color_ = {};
    ofx_ = 0;
    ofy_ = 0;
    h_ = 0U;
    dqa_ = 0;
    dqb_ = 0;
    zsf3_ = 0;
    zsf4_ = 0;
    flag_ = 0U;
}

Command Gte::decode_command(std::uint32_t opcode) noexcept {
    switch (opcode & 0x3fU) {
    case 0x01U:
        return Command::Rtps;
    case 0x06U:
        return Command::Nclip;
    case 0x0cU:
        return Command::Op;
    case 0x10U:
        return Command::Dpcs;
    case 0x11U:
        return Command::Intpl;
    case 0x12U:
        return Command::Mvmva;
    case 0x13U:
        return Command::Ncds;
    case 0x14U:
        return Command::Cdp;
    case 0x16U:
        return Command::Ncdt;
    case 0x1bU:
        return Command::Nccs;
    case 0x1cU:
        return Command::Cc;
    case 0x1eU:
        return Command::Ncs;
    case 0x20U:
        return Command::Nct;
    case 0x28U:
        return Command::Sqr;
    case 0x29U:
        return Command::Dcpl;
    case 0x2aU:
        return Command::Dpct;
    case 0x2dU:
        return Command::Avsz3;
    case 0x2eU:
        return Command::Avsz4;
    case 0x30U:
        return Command::Rtpt;
    case 0x3dU:
        return Command::Gpf;
    case 0x3eU:
        return Command::Gpl;
    case 0x3fU:
        return Command::Ncct;
    default:
        return Command::Unknown;
    }
}

DecodedInstruction Gte::decode(std::uint32_t instruction) noexcept {
    DecodedInstruction decoded{};
    decoded.raw = instruction;
    decoded.command = decode_command(instruction);
    decoded.shift_fraction = (instruction & (1U << 19U)) != 0U;
    decoded.matrix_selector = static_cast<std::uint8_t>((instruction >> 17U) & 3U);
    decoded.vector_selector = static_cast<std::uint8_t>((instruction >> 15U) & 3U);
    decoded.translation_selector =
        static_cast<std::uint8_t>((instruction >> 13U) & 3U);
    decoded.limit_negative = (instruction & (1U << 10U)) != 0U;
    return decoded;
}

std::uint8_t Gte::cycles(Command command) noexcept {
    switch (command) {
    case Command::Rtps:
        return 15U;
    case Command::Nclip:
        return 8U;
    case Command::Op:
        return 6U;
    case Command::Dpcs:
        return 8U;
    case Command::Intpl:
        return 8U;
    case Command::Mvmva:
        return 8U;
    case Command::Ncds:
        return 19U;
    case Command::Cdp:
        return 13U;
    case Command::Ncdt:
        return 44U;
    case Command::Nccs:
        return 17U;
    case Command::Cc:
        return 11U;
    case Command::Ncs:
        return 14U;
    case Command::Nct:
        return 30U;
    case Command::Sqr:
        return 5U;
    case Command::Dcpl:
        return 8U;
    case Command::Dpct:
        return 17U;
    case Command::Avsz3:
        return 5U;
    case Command::Avsz4:
        return 6U;
    case Command::Rtpt:
        return 23U;
    case Command::Gpf:
        return 5U;
    case Command::Gpl:
        return 5U;
    case Command::Ncct:
        return 39U;
    case Command::Unknown:
        return 0U;
    }
    return 0U;
}

const char* Gte::command_name(Command command) noexcept {
    switch (command) {
    case Command::Rtps:
        return "RTPS";
    case Command::Nclip:
        return "NCLIP";
    case Command::Op:
        return "OP";
    case Command::Dpcs:
        return "DPCS";
    case Command::Intpl:
        return "INTPL";
    case Command::Mvmva:
        return "MVMVA";
    case Command::Ncds:
        return "NCDS";
    case Command::Cdp:
        return "CDP";
    case Command::Ncdt:
        return "NCDT";
    case Command::Nccs:
        return "NCCS";
    case Command::Cc:
        return "CC";
    case Command::Ncs:
        return "NCS";
    case Command::Nct:
        return "NCT";
    case Command::Sqr:
        return "SQR";
    case Command::Dcpl:
        return "DCPL";
    case Command::Dpct:
        return "DPCT";
    case Command::Avsz3:
        return "AVSZ3";
    case Command::Avsz4:
        return "AVSZ4";
    case Command::Rtpt:
        return "RTPT";
    case Command::Gpf:
        return "GPF";
    case Command::Gpl:
        return "GPL";
    case Command::Ncct:
        return "NCCT";
    case Command::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

Gte::S64 Gte::arithmetic_shift_right(S64 value, unsigned shift) noexcept {
    if (shift == 0U) {
        return value;
    }
    if (shift >= 63U) {
        return value < 0 ? S64{-1} : S64{0};
    }
    if (value >= 0) {
        return value / (S64{1} << shift);
    }
    const S64 divisor = S64{1} << shift;
    return -((-value + divisor - 1) / divisor);
}

Gte::S64 Gte::clamp(S64 value, S64 minimum, S64 maximum) noexcept {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

Gte::S32 Gte::sign_extend_16(U16 value) noexcept {
    return static_cast<S32>(static_cast<S16>(value));
}

Gte::S16 Gte::low_s16(std::uint32_t value) noexcept {
    return static_cast<S16>(static_cast<U16>(value & 0xffffU));
}

Gte::U16 Gte::low_u16(std::uint32_t value) noexcept {
    return static_cast<U16>(value & 0xffffU);
}

std::uint32_t Gte::pack_s16(S16 low, S16 high) noexcept {
    return static_cast<std::uint32_t>(static_cast<U16>(low)) |
           (static_cast<std::uint32_t>(static_cast<U16>(high)) << 16U);
}

unsigned Gte::leading_count(std::uint32_t value) noexcept {
    const std::uint32_t sign_bit = std::uint32_t{1} << 31U;
    const bool negative = (value & sign_bit) != 0U;
    const std::uint32_t wanted = negative ? 0xffffffffU : 0U;
    unsigned count = 0U;
    for (unsigned bit = 31U; bit < 32U; --bit) {
        const bool value_bit = ((value >> bit) & 1U) != 0U;
        const bool wanted_bit = ((wanted >> bit) & 1U) != 0U;
        if (value_bit != wanted_bit) {
            break;
        }
        ++count;
    }
    return count;
}

std::uint32_t Gte::color_pack(std::uint32_t red,
                              std::uint32_t green,
                              std::uint32_t blue,
                              std::uint32_t code) noexcept {
    return (red & 0xffU) | ((green & 0xffU) << 8U) |
           ((blue & 0xffU) << 16U) | ((code & 0xffU) << 24U);
}

void Gte::clear_flags() noexcept {
    flag_ = 0U;
}

void Gte::set_flag(std::uint32_t bit) noexcept {
    flag_ |= bit;
}

std::uint32_t Gte::visible_flag() const noexcept {
    std::uint32_t result = flag_ & 0x7fffffffU;
    if ((flag_ & kFlagSummaryInputs) != 0U) {
        result |= std::uint32_t{1} << 31U;
    }
    if ((flag_ & kFlagDivideSummary) != 0U) {
        result |= kFlagDivideSummary;
    }
    return result;
}

void Gte::record_mac(unsigned index, S64 value, S64 overflow_probe) noexcept {
    mac_[index] = value;
    if (index == 0U) {
        if (overflow_probe >= (S64{1} << 31U)) {
            set_flag(kFlagMac0Positive);
        } else if (overflow_probe < -(S64{1} << 31U)) {
            set_flag(kFlagMac0Negative);
        }
        return;
    }
    const unsigned component = index - 1U;
    if (overflow_probe >= MacPositiveLimit) {
        set_flag(kFlagMacPositive[component]);
    } else if (overflow_probe < MacNegativeLimit) {
        set_flag(kFlagMacNegative[component]);
    }
}

void Gte::set_ir(unsigned index,
                 S64 value,
                 bool limit_negative,
                 S64 flag_probe,
                 bool flag_uses_lm_zero) noexcept {
    const S64 minimum = limit_negative ? S64{0} : S64{-32768};
    const S64 flag_minimum = flag_uses_lm_zero ? S64{-32768} : minimum;
    if (flag_probe < flag_minimum || flag_probe > S64{32767}) {
        set_flag(kFlagIr[index]);
    }
    const S64 stored = clamp(value, minimum, S64{32767});
    ir_[index] = static_cast<S16>(stored);
}

void Gte::set_ir0(S64 value) noexcept {
    if (value < 0 || value > S64{0x1000}) {
        set_flag(kFlagIr0);
    }
    ir0_ = static_cast<S16>(clamp(value, 0, S64{0x1000}));
}

void Gte::set_sz3(S64 value) noexcept {
    if (value < 0 || value > S64{0xffff}) {
        set_flag(kFlagSz3);
    }
    sz_[0] = sz_[1];
    sz_[1] = sz_[2];
    sz_[2] = sz_[3];
    sz_[3] = static_cast<U16>(clamp(value, 0, S64{0xffff}));
}

void Gte::push_sxy(S64 x, S64 y) noexcept {
    if (x < -S64{0x400} || x > S64{0x3ff}) {
        set_flag(kFlagSx);
    }
    if (y < -S64{0x400} || y > S64{0x3ff}) {
        set_flag(kFlagSy);
    }
    sxy_[0] = sxy_[1];
    sxy_[1] = sxy_[2];
    sxy_[2][0] = static_cast<S16>(clamp(x, -S64{0x400}, S64{0x3ff}));
    sxy_[2][1] = static_cast<S16>(clamp(y, -S64{0x400}, S64{0x3ff}));
}

void Gte::push_rgb(S64 red, S64 green, S64 blue) noexcept {
    const std::array<S64, 3U> values = {red, green, blue};
    std::array<std::uint32_t, 3U> output{};
    for (unsigned component = 0U; component < 3U; ++component) {
        const S64 channel = arithmetic_shift_right(values[component], 4U);
        if (channel < 0 || channel > S64{0xff}) {
            set_flag(kFlagColor[component]);
        }
        output[component] = static_cast<std::uint32_t>(
            clamp(channel, 0, S64{0xff}));
    }
    rgb_[0] = rgb_[1];
    rgb_[1] = rgb_[2];
    const std::uint32_t code = rgbc_ >> 24U;
    rgb_[2] = color_pack(output[0], output[1], output[2], code);
}

const std::array<Gte::S16, 3U>& Gte::selected_vector(
    std::uint8_t selector) const noexcept {
    switch (selector & 3U) {
    case 0U:
        return vectors_[0];
    case 1U:
        return vectors_[1];
    case 2U:
        return vectors_[2];
    case 3U:
        return ir_;
    default:
        return vectors_[0];
    }
}

const std::array<std::array<Gte::S16, 3U>, 3U>& Gte::selected_matrix(
    std::uint8_t selector) const noexcept {
    switch (selector & 3U) {
    case 0U:
        return rotation_;
    case 1U:
        return light_;
    case 2U:
        return color_;
    case 3U:
        return rotation_;
    default:
        return rotation_;
    }
}

Gte::S64 Gte::selected_translation(std::uint8_t selector,
                                   unsigned row) const noexcept {
    switch (selector & 3U) {
    case 0U:
        return static_cast<S64>(translation_[row]);
    case 1U:
        return static_cast<S64>(background_[row]);
    case 2U:
        return static_cast<S64>(far_color_[row]);
    case 3U:
        return 0;
    default:
        return 0;
    }
}

void Gte::make_reserved_matrix(
    std::array<std::array<S16, 3U>, 3U>& matrix) const noexcept {
    const S16 red = static_cast<S16>((rgbc_ & 0xffU) * 0x10U);
    matrix[0] = {static_cast<S16>(-red), red, ir0_};
    matrix[1] = {rotation_[0][2], rotation_[0][2], rotation_[0][2]};
    matrix[2] = {rotation_[1][1], rotation_[1][1], rotation_[1][1]};
}

void Gte::execute_rtps(const DecodedInstruction& instruction,
                       unsigned vector_index) noexcept {
    const std::array<S16, 3U>& vertex = vectors_[vector_index];
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    std::array<S64, 3U> shifted{};

    for (unsigned row = 0U; row < 3U; ++row) {
        const S64 raw = static_cast<S64>(translation_[row]) * 0x1000LL +
                        static_cast<S64>(rotation_[row][0]) * vertex[0] +
                        static_cast<S64>(rotation_[row][1]) * vertex[1] +
                        static_cast<S64>(rotation_[row][2]) * vertex[2];
        shifted[row] = arithmetic_shift_right(raw, shift);
        record_mac(row + 1U, shifted[row], raw);
        S64 flag_probe = shifted[row];
        if (row == 2U && !instruction.shift_fraction) {
            flag_probe = arithmetic_shift_right(raw, 12U);
        }
        set_ir(row, shifted[row], instruction.limit_negative, flag_probe,
               row == 2U);
    }

    const S64 z_value = arithmetic_shift_right(
        shifted[2], instruction.shift_fraction ? 0U : 12U);
    set_sz3(z_value);

    std::uint32_t division_flags = 0U;
    const std::uint32_t scale = reciprocal_scale(h_, sz_[3], division_flags);
    flag_ |= division_flags;

    S64 mac0 = static_cast<S64>(scale) * static_cast<S64>(ir_[0]) +
               static_cast<S64>(ofx_);
    record_mac(0U, mac0, mac0);
    const S64 screen_x = arithmetic_shift_right(mac0, 16U);

    mac0 = static_cast<S64>(scale) * static_cast<S64>(ir_[1]) +
           static_cast<S64>(ofy_);
    record_mac(0U, mac0, mac0);
    const S64 screen_y = arithmetic_shift_right(mac0, 16U);

    mac0 = static_cast<S64>(scale) * static_cast<S64>(dqa_) +
           static_cast<S64>(dqb_);
    record_mac(0U, mac0, mac0);
    set_ir0(arithmetic_shift_right(mac0, 12U));
    push_sxy(screen_x, screen_y);
}

void Gte::execute_rtpt(const DecodedInstruction& instruction) noexcept {
    execute_rtps(instruction, 0U);
    execute_rtps(instruction, 1U);
    execute_rtps(instruction, 2U);
}

void Gte::execute_mvmva(const DecodedInstruction& instruction) noexcept {
    std::array<std::array<S16, 3U>, 3U> matrix{};
    if (instruction.matrix_selector == 3U) {
        make_reserved_matrix(matrix);
    } else {
        matrix = selected_matrix(instruction.matrix_selector);
    }
    const std::array<S16, 3U> vector =
        selected_vector(instruction.vector_selector);
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    const bool far_color_bug = instruction.translation_selector == 2U;

    for (unsigned row = 0U; row < 3U; ++row) {
        const S64 full_raw =
            selected_translation(instruction.translation_selector, row) * 0x1000LL +
            static_cast<S64>(matrix[row][0]) * vector[0] +
            static_cast<S64>(matrix[row][1]) * vector[1] +
            static_cast<S64>(matrix[row][2]) * vector[2];
        const S64 actual_raw =
            far_color_bug
                ? static_cast<S64>(matrix[row][1]) * vector[1] +
                      static_cast<S64>(matrix[row][2]) * vector[2]
                : full_raw;
        const S64 result = arithmetic_shift_right(actual_raw, shift);
        record_mac(row + 1U, result, full_raw);
        set_ir(row, result, instruction.limit_negative, result);
    }
}

void Gte::execute_nclip() noexcept {
    const S64 x0 = sxy_[0][0];
    const S64 y0 = sxy_[0][1];
    const S64 x1 = sxy_[1][0];
    const S64 y1 = sxy_[1][1];
    const S64 x2 = sxy_[2][0];
    const S64 y2 = sxy_[2][1];
    const S64 result = x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 -
                       x1 * y0 - x2 * y1;
    record_mac(0U, result, result);
}

void Gte::execute_avsz(unsigned count) noexcept {
    S64 sum = 0;
    S16 scale = zsf3_;
    if (count == 3U) {
        sum = static_cast<S64>(sz_[1]) + static_cast<S64>(sz_[2]) +
              static_cast<S64>(sz_[3]);
    } else {
        sum = static_cast<S64>(sz_[0]) + static_cast<S64>(sz_[1]) +
              static_cast<S64>(sz_[2]) + static_cast<S64>(sz_[3]);
        scale = zsf4_;
    }
    const S64 result = static_cast<S64>(scale) * sum;
    record_mac(0U, result, result);
    const S64 average = arithmetic_shift_right(result, 12U);
    if (average < 0 || average > S64{0xffff}) {
        set_flag(kFlagSz3);
    }
    otz_ = static_cast<U16>(clamp(average, 0, S64{0xffff}));
}

void Gte::execute_op(const DecodedInstruction& instruction) noexcept {
    const S64 d1 = rotation_[0][0];
    const S64 d2 = rotation_[1][1];
    const S64 d3 = rotation_[2][2];
    const S64 raw[3U] = {
        static_cast<S64>(ir_[2]) * d2 - static_cast<S64>(ir_[1]) * d3,
        static_cast<S64>(ir_[0]) * d3 - static_cast<S64>(ir_[2]) * d1,
        static_cast<S64>(ir_[1]) * d1 - static_cast<S64>(ir_[0]) * d2,
    };
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    for (unsigned row = 0U; row < 3U; ++row) {
        const S64 result = arithmetic_shift_right(raw[row], shift);
        record_mac(row + 1U, result, raw[row]);
        set_ir(row, result, instruction.limit_negative, result);
    }
}

void Gte::execute_sqr(const DecodedInstruction& instruction) noexcept {
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    for (unsigned index = 0U; index < 3U; ++index) {
        const S64 raw = static_cast<S64>(ir_[index]) * ir_[index];
        const S64 result = arithmetic_shift_right(raw, shift);
        record_mac(index + 1U, result, raw);
        set_ir(index, result, true, result);
    }
}

void Gte::normal_stage(const DecodedInstruction& instruction,
                       unsigned vector_index) noexcept {
    const std::array<S16, 3U>& vertex = vectors_[vector_index];
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    for (unsigned row = 0U; row < 3U; ++row) {
        const S64 raw = static_cast<S64>(light_[row][0]) * vertex[0] +
                        static_cast<S64>(light_[row][1]) * vertex[1] +
                        static_cast<S64>(light_[row][2]) * vertex[2];
        const S64 result = arithmetic_shift_right(raw, shift);
        record_mac(row + 1U, result, raw);
        set_ir(row, result, instruction.limit_negative, result);
    }
}

void Gte::color_matrix_stage(const DecodedInstruction& instruction) noexcept {
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    const std::array<S16, 3U> input = ir_;
    for (unsigned row = 0U; row < 3U; ++row) {
        const S64 raw = static_cast<S64>(background_[row]) * 0x1000LL +
                        static_cast<S64>(color_[row][0]) * input[0] +
                        static_cast<S64>(color_[row][1]) * input[1] +
                        static_cast<S64>(color_[row][2]) * input[2];
        const S64 result = arithmetic_shift_right(raw, shift);
        record_mac(row + 1U, result, raw);
        set_ir(row, result, instruction.limit_negative, result);
    }
}

std::array<Gte::S64, 3U> Gte::color_modulation() noexcept {
    const std::array<S64, 3U> channels = {
        static_cast<S64>(rgbc_ & 0xffU),
        static_cast<S64>((rgbc_ >> 8U) & 0xffU),
        static_cast<S64>((rgbc_ >> 16U) & 0xffU),
    };
    std::array<S64, 3U> base{};
    for (unsigned index = 0U; index < 3U; ++index) {
        base[index] = channels[index] * static_cast<S64>(ir_[index]) * 16LL;
        record_mac(index + 1U, base[index], base[index]);
    }
    return base;
}

void Gte::finish_color(const DecodedInstruction& instruction,
                       const std::array<S64, 3U>& base) noexcept {
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    for (unsigned index = 0U; index < 3U; ++index) {
        const S64 result = arithmetic_shift_right(base[index], shift);
        record_mac(index + 1U, result, base[index]);
        set_ir(index, result, instruction.limit_negative, result);
    }
    push_rgb(mac_[1], mac_[2], mac_[3]);
}

void Gte::execute_depth_cue(const DecodedInstruction& instruction,
                            const std::array<S64, 3U>& base) noexcept {
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    const std::array<S64, 3U> far = {
        static_cast<S64>(far_color_[0]) * 0x1000LL,
        static_cast<S64>(far_color_[1]) * 0x1000LL,
        static_cast<S64>(far_color_[2]) * 0x1000LL,
    };
    std::array<S16, 3U> delta{};
    for (unsigned index = 0U; index < 3U; ++index) {
        const S64 difference = arithmetic_shift_right(far[index] - base[index],
                                                       shift);
        const S64 limited = clamp(difference, S64{-32768}, S64{32767});
        if (difference != limited) {
            set_flag(kFlagIr[index]);
        }
        delta[index] = static_cast<S16>(limited);
    }
    for (unsigned index = 0U; index < 3U; ++index) {
        const S64 raw = static_cast<S64>(delta[index]) * ir0_ + base[index];
        const S64 result = arithmetic_shift_right(raw, shift);
        record_mac(index + 1U, result, raw);
        set_ir(index, result, instruction.limit_negative, result);
    }
    push_rgb(mac_[1], mac_[2], mac_[3]);
}

void Gte::execute_normal_color(const DecodedInstruction& instruction,
                               unsigned vector_index,
                               bool with_color,
                               bool with_depth) noexcept {
    normal_stage(instruction, vector_index);
    color_matrix_stage(instruction);
    if (!with_color) {
        push_rgb(mac_[1], mac_[2], mac_[3]);
        return;
    }
    const std::array<S64, 3U> base = color_modulation();
    if (with_depth) {
        execute_depth_cue(instruction, base);
    } else {
        finish_color(instruction, base);
    }
}

void Gte::execute_color_color(const DecodedInstruction& instruction,
                              bool with_depth) noexcept {
    color_matrix_stage(instruction);
    const std::array<S64, 3U> base = color_modulation();
    if (with_depth) {
        execute_depth_cue(instruction, base);
    } else {
        finish_color(instruction, base);
    }
}

void Gte::execute_dpct(const DecodedInstruction& instruction) noexcept {
    for (unsigned pass = 0U; pass < 3U; ++pass) {
        const std::uint32_t source = rgb_[0];
        const std::array<S64, 3U> base = {
            static_cast<S64>(source & 0xffU) << 16U,
            static_cast<S64>((source >> 8U) & 0xffU) << 16U,
            static_cast<S64>((source >> 16U) & 0xffU) << 16U,
        };
        execute_depth_cue(instruction, base);
    }
}

void Gte::execute_gpf(const DecodedInstruction& instruction,
                      bool with_base) noexcept {
    const unsigned shift = instruction.shift_fraction ? 12U : 0U;
    const std::array<S16, 3U> input = ir_;
    const std::array<S64, 3U> base = {
        with_base ? mac_[1] * (S64{1} << shift) : S64{0},
        with_base ? mac_[2] * (S64{1} << shift) : S64{0},
        with_base ? mac_[3] * (S64{1} << shift) : S64{0},
    };
    for (unsigned index = 0U; index < 3U; ++index) {
        const S64 raw = static_cast<S64>(input[index]) * ir0_ + base[index];
        const S64 result = arithmetic_shift_right(raw, shift);
        record_mac(index + 1U, result, raw);
        set_ir(index, result, instruction.limit_negative, result);
    }
    push_rgb(mac_[1], mac_[2], mac_[3]);
}

std::uint32_t Gte::reciprocal_scale(U16 h,
                                    U16 sz,
                                    std::uint32_t& flags) noexcept {
    const std::uint32_t denominator = static_cast<std::uint32_t>(sz);
    if (static_cast<std::uint32_t>(h) >= denominator * 2U) {
        flags |= kFlagDivideOverflow | kFlagDivideSummary;
        return 0x1ffffU;
    }

    unsigned leading = 0U;
    std::uint32_t normalized = denominator;
    while ((normalized & 0x8000U) == 0U) {
        normalized <<= 1U;
        ++leading;
    }
    const std::uint32_t numerator = static_cast<std::uint32_t>(h) << leading;
    const std::uint32_t table_index = (normalized - 0x7fc0U) >> 7U;
    const std::uint32_t table_value =
        ((0x40000U / (table_index + 0x100U)) + 1U) / 2U;
    const std::uint32_t reciprocal =
        table_value > 0x101U ? table_value - 0x101U : 0U;
    const std::uint32_t u = reciprocal + 0x101U;
    std::uint32_t refined = (0x2000080U - normalized * u) >> 8U;
    refined = (0x80U + refined * u) >> 8U;
    const std::uint64_t product = static_cast<std::uint64_t>(numerator) *
                                  static_cast<std::uint64_t>(refined);
    const std::uint64_t rounded = (product + 0x8000U) >> 16U;
    const std::uint64_t limited = rounded > 0x1ffffU ? 0x1ffffU : rounded;
    return static_cast<std::uint32_t>(limited);
}

ExecutionReceipt Gte::execute(std::uint32_t instruction) noexcept {
    return execute(decode(instruction));
}

ExecutionReceipt Gte::execute(const DecodedInstruction& instruction) noexcept {
    ExecutionReceipt receipt{};
    receipt.command = instruction.command;
    if (instruction.command == Command::Unknown ||
        cycles(instruction.command) == 0U) {
        receipt.flag = visible_flag();
        receipt.state_digest = state_digest();
        return receipt;
    }

    clear_flags();
    switch (instruction.command) {
    case Command::Rtps:
        execute_rtps(instruction, 0U);
        break;
    case Command::Rtpt:
        execute_rtpt(instruction);
        break;
    case Command::Nclip:
        execute_nclip();
        break;
    case Command::Op:
        execute_op(instruction);
        break;
    case Command::Dpcs: {
        const std::array<S64, 3U> base = {
            static_cast<S64>(rgbc_ & 0xffU) << 16U,
            static_cast<S64>((rgbc_ >> 8U) & 0xffU) << 16U,
            static_cast<S64>((rgbc_ >> 16U) & 0xffU) << 16U,
        };
        execute_depth_cue(instruction, base);
        break;
    }
    case Command::Intpl: {
        const std::array<S64, 3U> base = {
            static_cast<S64>(ir_[0]) << 12U,
            static_cast<S64>(ir_[1]) << 12U,
            static_cast<S64>(ir_[2]) << 12U,
        };
        execute_depth_cue(instruction, base);
        break;
    }
    case Command::Mvmva:
        execute_mvmva(instruction);
        break;
    case Command::Ncds:
        execute_normal_color(instruction, 0U, true, true);
        break;
    case Command::Cdp:
        execute_color_color(instruction, true);
        break;
    case Command::Ncdt:
        execute_normal_color(instruction, 0U, true, true);
        execute_normal_color(instruction, 1U, true, true);
        execute_normal_color(instruction, 2U, true, true);
        break;
    case Command::Nccs:
        execute_normal_color(instruction, 0U, true, false);
        break;
    case Command::Cc:
        execute_color_color(instruction, false);
        break;
    case Command::Ncs:
        execute_normal_color(instruction, 0U, false, false);
        break;
    case Command::Nct:
        execute_normal_color(instruction, 0U, false, false);
        execute_normal_color(instruction, 1U, false, false);
        execute_normal_color(instruction, 2U, false, false);
        break;
    case Command::Sqr:
        execute_sqr(instruction);
        break;
    case Command::Dcpl: {
        const std::array<S64, 3U> channels = {
            static_cast<S64>(rgbc_ & 0xffU),
            static_cast<S64>((rgbc_ >> 8U) & 0xffU),
            static_cast<S64>((rgbc_ >> 16U) & 0xffU),
        };
        const std::array<S64, 3U> base = {
            channels[0] * ir_[0] * 16LL,
            channels[1] * ir_[1] * 16LL,
            channels[2] * ir_[2] * 16LL,
        };
        execute_depth_cue(instruction, base);
        break;
    }
    case Command::Dpct:
        execute_dpct(instruction);
        break;
    case Command::Avsz3:
        execute_avsz(3U);
        break;
    case Command::Avsz4:
        execute_avsz(4U);
        break;
    case Command::Gpf:
        execute_gpf(instruction, false);
        break;
    case Command::Gpl:
        execute_gpf(instruction, true);
        break;
    case Command::Ncct:
        execute_normal_color(instruction, 0U, true, false);
        execute_normal_color(instruction, 1U, true, false);
        execute_normal_color(instruction, 2U, true, false);
        break;
    case Command::Unknown:
        break;
    }

    receipt.recognized = true;
    receipt.cycles = cycles(instruction.command);
    receipt.flag = visible_flag();
    receipt.state_digest = state_digest();
    return receipt;
}

std::uint32_t Gte::read_data(std::uint8_t index) const noexcept {
    switch (index) {
    case 0U:
        return pack_s16(vectors_[0][0], vectors_[0][1]);
    case 1U:
        return static_cast<std::uint32_t>(
            sign_extend_16(static_cast<U16>(vectors_[0][2])));
    case 2U:
        return pack_s16(vectors_[1][0], vectors_[1][1]);
    case 3U:
        return static_cast<std::uint32_t>(
            sign_extend_16(static_cast<U16>(vectors_[1][2])));
    case 4U:
        return pack_s16(vectors_[2][0], vectors_[2][1]);
    case 5U:
        return static_cast<std::uint32_t>(
            sign_extend_16(static_cast<U16>(vectors_[2][2])));
    case 6U:
        return rgbc_;
    case 7U:
        return static_cast<std::uint32_t>(otz_);
    case 8U:
        return static_cast<std::uint32_t>(sign_extend_16(static_cast<U16>(ir0_)));
    case 9U:
    case 10U:
    case 11U:
        return static_cast<std::uint32_t>(sign_extend_16(static_cast<U16>(ir_[index - 9U])));
    case 12U:
        return pack_s16(sxy_[0][0], sxy_[0][1]);
    case 13U:
        return pack_s16(sxy_[1][0], sxy_[1][1]);
    case 14U:
    case 15U:
        return pack_s16(sxy_[2][0], sxy_[2][1]);
    case 16U:
    case 17U:
    case 18U:
    case 19U:
        return static_cast<std::uint32_t>(sz_[index - 16U]);
    case 20U:
    case 21U:
    case 22U:
        return rgb_[index - 20U];
    case 23U:
        return res1_;
    case 24U:
    case 25U:
    case 26U:
    case 27U:
        return static_cast<std::uint32_t>(mac_[index - 24U]);
    case 28U:
    case 29U: {
        const std::uint32_t red = static_cast<std::uint32_t>(
            clamp(arithmetic_shift_right(ir_[0], 7U), 0, S64{31}));
        const std::uint32_t green = static_cast<std::uint32_t>(
            clamp(arithmetic_shift_right(ir_[1], 7U), 0, S64{31}));
        const std::uint32_t blue = static_cast<std::uint32_t>(
            clamp(arithmetic_shift_right(ir_[2], 7U), 0, S64{31}));
        return red | (green << 5U) | (blue << 10U);
    }
    case 30U:
        return lzcs_;
    case 31U:
        return lzcr_;
    default:
        return 0U;
    }
}

void Gte::write_data(std::uint8_t index, std::uint32_t value) noexcept {
    switch (index) {
    case 0U:
        vectors_[0][0] = low_s16(value);
        vectors_[0][1] = low_s16(value >> 16U);
        break;
    case 1U:
        vectors_[0][2] = low_s16(value);
        break;
    case 2U:
        vectors_[1][0] = low_s16(value);
        vectors_[1][1] = low_s16(value >> 16U);
        break;
    case 3U:
        vectors_[1][2] = low_s16(value);
        break;
    case 4U:
        vectors_[2][0] = low_s16(value);
        vectors_[2][1] = low_s16(value >> 16U);
        break;
    case 5U:
        vectors_[2][2] = low_s16(value);
        break;
    case 6U:
        rgbc_ = value;
        break;
    case 7U:
        otz_ = low_u16(value);
        break;
    case 8U:
        ir0_ = low_s16(value);
        break;
    case 9U:
    case 10U:
    case 11U:
        ir_[index - 9U] = low_s16(value);
        break;
    case 12U:
    case 13U:
    case 14U:
        sxy_[index - 12U][0] = low_s16(value);
        sxy_[index - 12U][1] = low_s16(value >> 16U);
        break;
    case 15U:
        sxy_[0] = sxy_[1];
        sxy_[1] = sxy_[2];
        sxy_[2][0] = low_s16(value);
        sxy_[2][1] = low_s16(value >> 16U);
        break;
    case 16U:
    case 17U:
    case 18U:
    case 19U:
        sz_[index - 16U] = low_u16(value);
        break;
    case 20U:
    case 21U:
    case 22U:
        rgb_[index - 20U] = value;
        break;
    case 23U:
        res1_ = value;
        break;
    case 24U:
    case 25U:
    case 26U:
    case 27U:
        mac_[index - 24U] = static_cast<S32>(value);
        break;
    case 28U:
        ir_[0] = static_cast<S16>((value & 0x1fU) << 7U);
        ir_[1] = static_cast<S16>(((value >> 5U) & 0x1fU) << 7U);
        ir_[2] = static_cast<S16>(((value >> 10U) & 0x1fU) << 7U);
        break;
    case 30U:
        lzcs_ = value;
        lzcr_ = static_cast<std::uint32_t>(leading_count(value));
        break;
    default:
        break;
    }
}

std::uint32_t Gte::read_control(std::uint8_t index) const noexcept {
    const auto read_matrix = [](const std::array<std::array<S16, 3U>, 3U>& matrix,
                                unsigned offset) noexcept -> std::uint32_t {
        switch (offset) {
        case 0U:
            return pack_s16(matrix[0][0], matrix[0][1]);
        case 1U:
            return pack_s16(matrix[0][2], matrix[1][0]);
        case 2U:
            return pack_s16(matrix[1][1], matrix[1][2]);
        case 3U:
            return pack_s16(matrix[2][0], matrix[2][1]);
        case 4U:
            return static_cast<std::uint32_t>(
                sign_extend_16(static_cast<U16>(matrix[2][2])));
        default:
            return 0U;
        }
    };

    if (index >= 32U && index <= 36U) {
        return read_matrix(rotation_, index - 32U);
    }
    if (index >= 40U && index <= 44U) {
        return read_matrix(light_, index - 40U);
    }
    if (index >= 48U && index <= 52U) {
        return read_matrix(color_, index - 48U);
    }
    switch (index) {
    case 37U:
    case 38U:
    case 39U:
        return static_cast<std::uint32_t>(translation_[index - 37U]);
    case 45U:
    case 46U:
    case 47U:
        return static_cast<std::uint32_t>(background_[index - 45U]);
    case 53U:
    case 54U:
    case 55U:
        return static_cast<std::uint32_t>(far_color_[index - 53U]);
    case 56U:
        return static_cast<std::uint32_t>(ofx_);
    case 57U:
        return static_cast<std::uint32_t>(ofy_);
    case 58U:
        return static_cast<std::uint32_t>(sign_extend_16(h_));
    case 59U:
        return static_cast<std::uint32_t>(sign_extend_16(static_cast<U16>(dqa_)));
    case 60U:
        return static_cast<std::uint32_t>(dqb_);
    case 61U:
        return static_cast<std::uint32_t>(sign_extend_16(static_cast<U16>(zsf3_)));
    case 62U:
        return static_cast<std::uint32_t>(sign_extend_16(static_cast<U16>(zsf4_)));
    case 63U:
        return visible_flag();
    default:
        return 0U;
    }
}

void Gte::write_control(std::uint8_t index, std::uint32_t value) noexcept {
    auto write_matrix = [](std::array<std::array<S16, 3U>, 3U>& matrix,
                           unsigned offset,
                           std::uint32_t word) noexcept {
        switch (offset) {
        case 0U:
            matrix[0][0] = low_s16(word);
            matrix[0][1] = low_s16(word >> 16U);
            break;
        case 1U:
            matrix[0][2] = low_s16(word);
            matrix[1][0] = low_s16(word >> 16U);
            break;
        case 2U:
            matrix[1][1] = low_s16(word);
            matrix[1][2] = low_s16(word >> 16U);
            break;
        case 3U:
            matrix[2][0] = low_s16(word);
            matrix[2][1] = low_s16(word >> 16U);
            break;
        case 4U:
            matrix[2][2] = low_s16(word);
            break;
        default:
            break;
        }
    };

    if (index >= 32U && index <= 36U) {
        write_matrix(rotation_, index - 32U, value);
        return;
    }
    if (index >= 40U && index <= 44U) {
        write_matrix(light_, index - 40U, value);
        return;
    }
    if (index >= 48U && index <= 52U) {
        write_matrix(color_, index - 48U, value);
        return;
    }
    switch (index) {
    case 37U:
    case 38U:
    case 39U:
        translation_[index - 37U] = static_cast<S32>(value);
        break;
    case 45U:
    case 46U:
    case 47U:
        background_[index - 45U] = static_cast<S32>(value);
        break;
    case 53U:
    case 54U:
    case 55U:
        far_color_[index - 53U] = static_cast<S32>(value);
        break;
    case 56U:
        ofx_ = static_cast<S32>(value);
        break;
    case 57U:
        ofy_ = static_cast<S32>(value);
        break;
    case 58U:
        h_ = low_u16(value);
        break;
    case 59U:
        dqa_ = low_s16(value);
        break;
    case 60U:
        dqb_ = static_cast<S32>(value);
        break;
    case 61U:
        zsf3_ = low_s16(value);
        break;
    case 62U:
        zsf4_ = low_s16(value);
        break;
    case 63U:
        flag_ = value & 0x7ffff000U;
        break;
    default:
        break;
    }
}

Snapshot Gte::snapshot() const noexcept {
    Snapshot result{};
    result.vectors = vectors_;
    result.rgbc = rgbc_;
    result.otz = otz_;
    result.ir0 = ir0_;
    result.ir = ir_;
    result.sxy = sxy_;
    result.sz = sz_;
    result.rgb = rgb_;
    result.res1 = res1_;
    result.mac = mac_;
    result.lzcs = lzcs_;
    result.lzcr = lzcr_;
    result.rotation = rotation_;
    result.translation = translation_;
    result.light = light_;
    result.background = background_;
    result.color = color_;
    result.far_color = far_color_;
    result.ofx = ofx_;
    result.ofy = ofy_;
    result.h = h_;
    result.dqa = dqa_;
    result.dqb = dqb_;
    result.zsf3 = zsf3_;
    result.zsf4 = zsf4_;
    result.flag = visible_flag();
    return result;
}

std::uint64_t Gte::state_digest() const noexcept {
    std::uint64_t hash = (static_cast<std::uint64_t>(kFNVOffset) << 32U) |
                         static_cast<std::uint64_t>(kFNVOffset);
    const auto add_byte = [&hash](std::uint8_t byte) noexcept {
        const std::uint32_t low = fnv_step(
            static_cast<std::uint32_t>(hash & 0xffffffffULL), byte);
        const std::uint32_t high = fnv_step(
            static_cast<std::uint32_t>((hash >> 32U) & 0xffffffffULL),
            static_cast<std::uint8_t>(byte ^ 0xa5U));
        hash = (static_cast<std::uint64_t>(high) << 32U) | low;
    };
    const auto add_u32 = [&add_byte](std::uint32_t value) noexcept {
        for (unsigned byte = 0U; byte < 4U; ++byte) {
            add_byte(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
        }
    };
    const auto add_s64 = [&add_byte](S64 value) noexcept {
        const std::uint64_t bits = static_cast<std::uint64_t>(value);
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            add_byte(static_cast<std::uint8_t>((bits >> (byte * 8U)) & 0xffU));
        }
    };

    for (std::uint8_t index = 0U; index < 32U; ++index) {
        add_u32(read_data(index));
    }
    for (std::uint8_t index = 32U; index < 64U; ++index) {
        add_u32(read_control(index));
    }
    for (const S64 value : mac_) {
        add_s64(value);
    }
    return hash;
}

} // namespace clean_aot::gte
