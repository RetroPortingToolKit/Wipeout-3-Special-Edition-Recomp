#include "mdec.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace clean_aot::mdec {
namespace {

constexpr std::uint32_t kStateMagic = 0x4D444543U; // "MDEC"
constexpr std::uint32_t kStateVersion = 3U;
constexpr std::size_t kInputBytes = Device::kInputCapacityWords * 4U;
constexpr std::size_t kMaxPumpSteps = 8192U;
constexpr std::uint16_t kEndCode = 0xFE00U;
constexpr std::int32_t kCoefficientMinimum = -0x400;
constexpr std::int32_t kCoefficientMaximum = 0x3FF;
constexpr std::int32_t kMonoMinimum = -128;
constexpr std::int32_t kMonoMaximum = 127;
constexpr std::int32_t kColorMinimum = -128;
constexpr std::int32_t kColorMaximum = 127;
constexpr std::int32_t kRMultiplier = 5743;  // 1.4020 in signed Q12.
constexpr std::int32_t kGBlueMultiplier = 1408; // 0.34375 in signed Q12.
constexpr std::int32_t kGRedMultiplier = 2926; // 0.71436 in signed Q12.
constexpr std::int32_t kBMultiplier = 7258; // 1.7720 in signed Q12.
constexpr std::uint64_t kRegisterCycleCost = 1U;
constexpr std::uint64_t kInputHalfwordCycleCost = 2U;
constexpr std::uint64_t kIdctMultiplyCycleCost = 1024U;
constexpr std::uint64_t kPixelCycleCost = 1U;

constexpr std::array<std::uint8_t, 64> kZigzag = {
    0U,  1U,  5U,  6U,  14U, 15U, 27U, 28U,
    2U,  4U,  7U,  13U, 16U, 26U, 29U, 42U,
    3U,  8U,  12U, 17U, 25U, 30U, 41U, 43U,
    9U,  11U, 18U, 24U, 31U, 40U, 44U, 53U,
    10U, 19U, 23U, 32U, 39U, 45U, 52U, 54U,
    20U, 22U, 33U, 38U, 46U, 51U, 55U, 60U,
    21U, 34U, 37U, 47U, 50U, 56U, 59U, 61U,
    35U, 36U, 48U, 49U, 57U, 58U, 62U, 63U,
};

consteval std::array<std::uint8_t, 64> make_zagzig()
{
    std::array<std::uint8_t, 64> inverse{};
    for (std::size_t index = 0U; index < kZigzag.size(); ++index) {
        inverse[kZigzag[index]] = static_cast<std::uint8_t>(index);
    }
    return inverse;
}

constexpr std::array<std::uint8_t, 64> kZagzig = make_zagzig();

constexpr std::int16_t signed_word(std::uint16_t value) noexcept
{
    return value < 0x8000U
               ? static_cast<std::int16_t>(value)
               : static_cast<std::int16_t>(static_cast<std::int32_t>(value) - 0x10000);
}

constexpr std::array<std::int16_t, 64> kDefaultScale = {
    signed_word(0x5A82U), signed_word(0x5A82U), signed_word(0x5A82U), signed_word(0x5A82U),
    signed_word(0x5A82U), signed_word(0x5A82U), signed_word(0x5A82U), signed_word(0x5A82U),
    signed_word(0x7D8AU), signed_word(0x6A6DU), signed_word(0x471CU), signed_word(0x18F8U),
    signed_word(0xE707U), signed_word(0xB8E3U), signed_word(0x9592U), signed_word(0x8275U),
    signed_word(0x7641U), signed_word(0x30FBU), signed_word(0xCF04U), signed_word(0x89BEU),
    signed_word(0x89BEU), signed_word(0xCF04U), signed_word(0x30FBU), signed_word(0x7641U),
    signed_word(0x6A6DU), signed_word(0xE707U), signed_word(0x8275U), signed_word(0xB8E3U),
    signed_word(0x471CU), signed_word(0x7D8AU), signed_word(0x18F8U), signed_word(0x9592U),
    signed_word(0x5A82U), signed_word(0xA57DU), signed_word(0xA57DU), signed_word(0x5A82U),
    signed_word(0x5A82U), signed_word(0xA57DU), signed_word(0xA57DU), signed_word(0x5A82U),
    signed_word(0x471CU), signed_word(0x8275U), signed_word(0x18F8U), signed_word(0x6A6DU),
    signed_word(0x9592U), signed_word(0xE707U), signed_word(0x7D8AU), signed_word(0xB8E3U),
    signed_word(0x30FBU), signed_word(0x89BEU), signed_word(0x7641U), signed_word(0xCF04U),
    signed_word(0xCF04U), signed_word(0x7641U), signed_word(0x89BEU), signed_word(0x30FBU),
    signed_word(0x18F8U), signed_word(0xB8E3U), signed_word(0x6A6DU), signed_word(0x8275U),
    signed_word(0x7D8AU), signed_word(0x9592U), signed_word(0x471CU), signed_word(0xE707U),
};

constexpr std::int32_t clamp_value(std::int64_t value,
                                   std::int32_t minimum,
                                   std::int32_t maximum) noexcept
{
    if (value < static_cast<std::int64_t>(minimum)) {
        return minimum;
    }
    if (value > static_cast<std::int64_t>(maximum)) {
        return maximum;
    }
    return static_cast<std::int32_t>(value);
}

constexpr std::int32_t sign_extend_10(std::uint16_t value) noexcept
{
    const std::int32_t raw = static_cast<std::int32_t>(value & 0x03FFU);
    return (raw & 0x0200) != 0 ? raw - 0x0400 : raw;
}

constexpr std::int32_t sign_extend_9(std::int32_t value) noexcept
{
    std::int32_t result = value & 0x01FF;
    if ((result & 0x0100) != 0) {
        result -= 0x0200;
    }
    return result;
}

// This is the integer form of floor((value + add) / 2^shift). The explicit
// negative path avoids relying on implementation-defined arithmetic right
// shift and makes every host agree. All callers supply bounded MDEC
// intermediates and constant shifts below 63.
constexpr std::int32_t mdec_divide_add(std::int64_t value,
                                       unsigned shift,
                                       std::int64_t add) noexcept
{
    const std::int64_t adjusted = value + add;
    const std::int64_t divisor = std::int64_t{1} << shift;
    if (adjusted >= 0) {
        return static_cast<std::int32_t>(adjusted / divisor);
    }
    const std::int64_t magnitude = -adjusted;
    return static_cast<std::int32_t>(-((magnitude + divisor - 1) / divisor));
}

constexpr std::int32_t quantized_ac(std::int32_t value,
                                    std::uint8_t quant,
                                    std::uint8_t q_scale) noexcept
{
    if (q_scale == 0U) {
        return clamp_value(static_cast<std::int64_t>(value) * 2, kCoefficientMinimum,
                           kCoefficientMaximum);
    }
    const std::int64_t product = static_cast<std::int64_t>(value) *
                                 static_cast<std::int64_t>(quant) *
                                 static_cast<std::int64_t>(q_scale);
    return clamp_value(mdec_divide_add(product, 3U, 4), kCoefficientMinimum,
                       kCoefficientMaximum);
}

constexpr std::int32_t quantized_dc(std::int32_t value,
                                    std::uint8_t quant,
                                    std::uint8_t q_scale) noexcept
{
    if (q_scale == 0U) {
        return clamp_value(static_cast<std::int64_t>(value) * 2, kCoefficientMinimum,
                           kCoefficientMaximum);
    }
    return clamp_value(static_cast<std::int64_t>(value) * static_cast<std::int64_t>(quant),
                       kCoefficientMinimum, kCoefficientMaximum);
}

void idct_block(const std::array<std::int32_t, 64>& source,
                const std::array<std::int16_t, 64>& scale,
                std::array<std::int32_t, 64>& result) noexcept
{
    std::array<std::int32_t, 64> temporary{};
    const std::array<std::int32_t, 64>* input = &source;
    std::array<std::int32_t, 64>* output = &temporary;

    for (unsigned pass = 0U; pass < 2U; ++pass) {
        for (unsigned x = 0U; x < 8U; ++x) {
            for (unsigned y = 0U; y < 8U; ++y) {
                std::int64_t sum = 0;
                for (unsigned z = 0U; z < 8U; ++z) {
                    const std::size_t source_index = static_cast<std::size_t>(y) +
                                                     static_cast<std::size_t>(z) * 8U;
                    const std::size_t scale_index = static_cast<std::size_t>(x) +
                                                    static_cast<std::size_t>(z) * 8U;
                    // The MDEC ignores the low three scale-table bits and
                    // applies the remaining Q14 value divided by eight.
                    const std::uint16_t raw_scale =
                        std::bit_cast<std::uint16_t>(scale[scale_index]);
                    const std::int32_t scale_factor = static_cast<std::int32_t>(
                        signed_word(static_cast<std::uint16_t>(raw_scale & 0xFFF8U))) / 8;
                    sum += static_cast<std::int64_t>((*input)[source_index]) *
                           static_cast<std::int64_t>(scale_factor);
                }
                const std::size_t output_index = static_cast<std::size_t>(x) +
                                                 static_cast<std::size_t>(y) * 8U;
                (*output)[output_index] = mdec_divide_add(sum, 13U, 0x0FFF);
            }
        }
        if (pass == 0U) {
            input = &temporary;
            output = &result;
        }
    }
}

std::int32_t mono_sample(std::int32_t value) noexcept
{
    return clamp_value(sign_extend_9(value), kMonoMinimum, kMonoMaximum);
}

std::int32_t color_sample(std::int32_t value) noexcept
{
    return clamp_value(value, kColorMinimum, kColorMaximum);
}

std::uint8_t as_byte(std::int32_t value) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) & 0xFFU);
}

std::uint8_t component5(std::int32_t value) noexcept
{
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(as_byte(value)) >> 3U) &
                                     0x1FU);
}

std::uint8_t current_block_for_input(std::uint8_t block) noexcept
{
    if (block == 0U) {
        return 4U; // Cr
    }
    if (block == 1U) {
        return 5U; // Cb
    }
    return static_cast<std::uint8_t>(block - 2U); // Y1..Y4
}

std::size_t output_bytes_for(OutputDepth depth) noexcept
{
    switch (depth) {
    case OutputDepth::FourBit:
        return 32U;
    case OutputDepth::EightBit:
        return 64U;
    case OutputDepth::TwentyFourBit:
        return 768U;
    case OutputDepth::FifteenBit:
        return 512U;
    }
    return 0U;
}

class SnapshotWriter final {
public:
    explicit SnapshotWriter(std::span<std::byte> destination) noexcept
        : destination_(destination)
    {
    }

    bool write_u8(std::uint8_t value) noexcept
    {
        if (offset_ >= destination_.size()) {
            valid_ = false;
            return false;
        }
        destination_[offset_++] = static_cast<std::byte>(value);
        return true;
    }

    bool write_u16(std::uint16_t value) noexcept
    {
        return write_u8(static_cast<std::uint8_t>(value & 0xFFU)) &&
               write_u8(static_cast<std::uint8_t>(value >> 8U));
    }

    bool write_u32(std::uint32_t value) noexcept
    {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            if (!write_u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
                return false;
            }
        }
        return true;
    }

    bool write_u64(std::uint64_t value) noexcept
    {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            if (!write_u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU))) {
                return false;
            }
        }
        return true;
    }

    bool write_i16(std::int16_t value) noexcept
    {
        return write_u16(std::bit_cast<std::uint16_t>(value));
    }

    bool write_i32(std::int32_t value) noexcept
    {
        return write_u32(std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

private:
    std::span<std::byte> destination_;
    std::size_t offset_ = 0U;
    bool valid_ = true;
};

class SnapshotReader final {
public:
    explicit SnapshotReader(std::span<const std::byte> source) noexcept : source_(source) {}

    bool read_u8(std::uint8_t& value) noexcept
    {
        if (offset_ >= source_.size()) {
            valid_ = false;
            return false;
        }
        value = std::to_integer<std::uint8_t>(source_[offset_++]);
        return true;
    }

    bool read_u16(std::uint16_t& value) noexcept
    {
        std::uint8_t low = 0U;
        std::uint8_t high = 0U;
        if (!read_u8(low) || !read_u8(high)) {
            return false;
        }
        value = static_cast<std::uint16_t>(low) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8U);
        return true;
    }

    bool read_u32(std::uint32_t& value) noexcept
    {
        value = 0U;
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            std::uint8_t byte = 0U;
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept
    {
        value = 0U;
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            std::uint8_t byte = 0U;
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    bool read_i16(std::int16_t& value) noexcept
    {
        std::uint16_t raw = 0U;
        if (!read_u16(raw)) {
            return false;
        }
        value = std::bit_cast<std::int16_t>(raw);
        return true;
    }

    bool read_i32(std::int32_t& value) noexcept
    {
        std::uint32_t raw = 0U;
        if (!read_u32(raw)) {
            return false;
        }
        value = std::bit_cast<std::int32_t>(raw);
        return true;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    [[nodiscard]] bool trailing_zero() const noexcept
    {
        if (!valid_) {
            return false;
        }
        for (std::size_t index = offset_; index < source_.size(); ++index) {
            if (source_[index] != std::byte{0}) {
                return false;
            }
        }
        return true;
    }

private:
    std::span<const std::byte> source_;
    std::size_t offset_ = 0U;
    bool valid_ = true;
};

} // namespace

Device::Device() noexcept
{
    reset();
}

void Device::reset() noexcept
{
    state_ = State{};
    state_.magic = kStateMagic;
    state_.version = kStateVersion;
    state_.command = Command::Idle;
    state_.block_phase = BlockPhase::NeedHeader;
    state_.depth = OutputDepth::FourBit;
    state_.latched_fault = Fault::None;
    // The documented reset value is 80040000h: empty output and the
    // monochrome/current-block marker 4 with a zero low field.
    state_.idle_status_low = 0U;
    state_.quant_luma.fill(1U);
    state_.quant_chroma.fill(1U);
    state_.scale = kDefaultScale;
}

void Device::latch_fault(Fault fault_value) noexcept
{
    if (fault_value == Fault::None) {
        return;
    }
    state_.latched_fault = fault_value;
    state_.command = Command::Idle;
    state_.remaining_bytes = 0U;
    state_.receive_remaining_bytes = 0U;
    state_.expected_bytes = 0U;
    state_.idle_status_low = 0xFFFFU;
    state_.input_head = 0U;
    state_.input_tail = 0U;
    state_.input_count = 0U;
    state_.macroblock_started = false;
    state_.decode_block = 0U;
    state_.coefficient_position = 0U;
    state_.block_phase = BlockPhase::NeedHeader;
    state_.input_last_word_received = false;
}

bool Device::add_cycles(std::uint64_t cycles_value) noexcept
{
    if (cycles_value > std::numeric_limits<std::uint64_t>::max() - state_.cycles) {
        latch_fault(Fault::ArithmeticOverflow);
        return false;
    }
    state_.cycles += cycles_value;
    return true;
}

bool Device::push_input_word(std::uint32_t word) noexcept
{
    if (state_.input_count > static_cast<std::uint16_t>(kInputBytes - 4U)) {
        return false;
    }
    for (unsigned byte_index = 0U; byte_index < 4U; ++byte_index) {
        const std::uint32_t shift = byte_index * 8U;
        state_.input[state_.input_tail] =
            static_cast<std::uint8_t>((word >> shift) & 0xFFU);
        state_.input_tail = static_cast<std::uint16_t>(
            (static_cast<std::size_t>(state_.input_tail) + 1U) % kInputBytes);
    }
    state_.input_count = static_cast<std::uint16_t>(state_.input_count + 4U);
    return true;
}

bool Device::peek_input_halfword(std::uint16_t& value) const noexcept
{
    if (state_.input_count < 2U) {
        return false;
    }
    const std::size_t first = state_.input_head;
    const std::size_t second = (first + 1U) % kInputBytes;
    value = static_cast<std::uint16_t>(state_.input[first]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(state_.input[second]) << 8U);
    return true;
}

bool Device::consume_input_byte(std::uint8_t& value) noexcept
{
    if (state_.input_count == 0U) {
        return false;
    }
    value = state_.input[state_.input_head];
    state_.input_head = static_cast<std::uint16_t>(
        (static_cast<std::size_t>(state_.input_head) + 1U) % kInputBytes);
    --state_.input_count;
    return true;
}

bool Device::consume_input_halfword(std::uint16_t& value) noexcept
{
    if (!peek_input_halfword(value)) {
        return false;
    }
    std::uint8_t ignored = 0U;
    (void)consume_input_byte(ignored);
    (void)consume_input_byte(ignored);
    return true;
}

TransferResult Device::write_command_word(std::uint32_t word) noexcept
{
    const std::uint64_t before = state_.cycles;
    TransferResult result{};
    if (state_.latched_fault != Fault::None) {
        result.fault = state_.latched_fault;
        return result;
    }

    if (state_.command != Command::Idle) {
        if (state_.receive_remaining_bytes < 4U) {
            result.fault = Fault::CommandBusy;
            result.cycles = state_.cycles - before;
            return result;
        }
        if (!push_input_word(word)) {
            result.fault = Fault::InputFifoFull;
            result.cycles = state_.cycles - before;
            return result;
        }
        state_.receive_remaining_bytes -= 4U;
        if (state_.receive_remaining_bytes == 0U) {
            state_.input_last_word_received = true;
        }
        if (!add_cycles(kRegisterCycleCost)) {
            result.fault = state_.latched_fault;
            result.words = 1U;
            result.cycles = state_.cycles - before;
            return result;
        }
        pump();
        result.words = 1U;
        result.complete = state_.latched_fault == Fault::None;
        result.fault = state_.latched_fault;
        result.cycles = state_.cycles - before;
        return result;
    }

    const std::uint8_t opcode = static_cast<std::uint8_t>(word >> 29U);
    state_.command_word = word;
    state_.depth = static_cast<OutputDepth>((word >> 27U) & 0x03U);
    state_.output_signed = (word & 0x04000000U) != 0U;
    state_.output_bit15 = (word & 0x02000000U) != 0U;
    state_.color_quantization = false;
    state_.decode_block = 0U;
    state_.coefficient_position = 0U;
    state_.block_phase = BlockPhase::NeedHeader;
    state_.macroblock_started = false;
    state_.receive_remaining_bytes = 0U;
    state_.input_last_word_received = false;
    state_.idle_status_low = 0xFFFFU;

    if (opcode == 0U || opcode >= 4U) {
        state_.idle_status_low = static_cast<std::uint16_t>(word & 0xFFFFU);
    } else if (opcode == 1U) {
        state_.command = Command::Decode;
        state_.expected_bytes = static_cast<std::uint32_t>(word & 0xFFFFU) * 4U;
        state_.remaining_bytes = state_.expected_bytes;
        state_.receive_remaining_bytes = state_.expected_bytes;
        if (state_.remaining_bytes == 0U) {
            finish_command();
        }
    } else if (opcode == 2U) {
        state_.command = Command::Quantization;
        state_.color_quantization = (word & 1U) != 0U;
        state_.expected_bytes = state_.color_quantization ? 128U : 64U;
        state_.remaining_bytes = state_.expected_bytes;
        state_.receive_remaining_bytes = state_.expected_bytes;
        state_.table_index = 0U;
    } else if (opcode == 3U) {
        state_.command = Command::Scale;
        state_.expected_bytes = 128U;
        state_.remaining_bytes = state_.expected_bytes;
        state_.receive_remaining_bytes = state_.expected_bytes;
        state_.table_index = 0U;
    }

    if (!add_cycles(kRegisterCycleCost)) {
        result.fault = state_.latched_fault;
        result.cycles = state_.cycles - before;
        return result;
    }
    pump();
    result.words = 1U;
    result.complete = state_.latched_fault == Fault::None;
    result.fault = state_.latched_fault;
    result.cycles = state_.cycles - before;
    return result;
}

TransferResult Device::write_control_word(std::uint32_t word) noexcept
{
    const std::uint64_t before = state_.cycles;
    TransferResult result{};
    const bool resetting = (word & (std::uint32_t{1U} << 31U)) != 0U;
    if (resetting) {
        reset();
    } else if (state_.latched_fault != Fault::None) {
        result.fault = state_.latched_fault;
        return result;
    }

    state_.input_dma_enabled = (word & 0x40000000U) != 0U;
    state_.output_dma_enabled = (word & 0x20000000U) != 0U;
    (void)add_cycles(kRegisterCycleCost);
    result.words = 1U;
    result.complete = state_.latched_fault == Fault::None;
    result.fault = state_.latched_fault;
    result.cycles = resetting ? state_.cycles : state_.cycles - before;
    return result;
}

bool Device::process_quantization() noexcept
{
    bool progressed = false;
    while (state_.remaining_bytes != 0U && state_.input_count != 0U) {
        std::uint8_t value = 0U;
        (void)consume_input_byte(value);
        const std::size_t index = state_.table_index;
        if (index >= 64U && !state_.color_quantization) {
            latch_fault(Fault::MalformedStream);
            return true;
        }
        if (index < 64U) {
            state_.quant_luma[index] = value;
        } else {
            const std::size_t chroma_index = index - 64U;
            if (chroma_index >= state_.quant_chroma.size()) {
                latch_fault(Fault::MalformedStream);
                return true;
            }
            state_.quant_chroma[chroma_index] = value;
        }
        ++state_.table_index;
        --state_.remaining_bytes;
        progressed = true;
        if (!add_cycles(1U)) {
            return true;
        }
    }
    if (state_.remaining_bytes == 0U && state_.command == Command::Quantization) {
        finish_command();
        progressed = true;
    }
    return progressed;
}

bool Device::process_scale() noexcept
{
    if (state_.remaining_bytes == 0U) {
        finish_command();
        return true;
    }
    if (state_.remaining_bytes < 2U || state_.input_count < 2U) {
        if (state_.remaining_bytes == 1U) {
            latch_fault(Fault::MalformedStream);
            return true;
        }
        return false;
    }

    std::uint16_t raw = 0U;
    (void)consume_input_halfword(raw);
    const std::size_t index = state_.table_index;
    if (index >= state_.scale.size()) {
        latch_fault(Fault::MalformedStream);
        return true;
    }
    const std::uint16_t masked = static_cast<std::uint16_t>(raw & 0xFFF8U);
    state_.scale[index] = signed_word(masked);
    ++state_.table_index;
    state_.remaining_bytes -= 2U;
    if (!add_cycles(kInputHalfwordCycleCost)) {
        return true;
    }
    if (state_.remaining_bytes == 0U) {
        finish_command();
    }
    return true;
}

bool Device::process_decode() noexcept
{
    return process_decode_step();
}

bool Device::process_decode_step() noexcept
{
    const std::size_t required_words = output_bytes_for(state_.depth) / 4U;

    if (!state_.macroblock_started) {
        if (state_.remaining_bytes == 0U) {
            finish_command();
            return true;
        }
        // FE00 at a macroblock boundary is DMA padding. Consume it without
        // reserving an output slot or interpreting it as a new DC header.
        if (state_.input_count < 2U) {
            return false;
        }
        std::uint16_t boundary_word = 0U;
        if (!peek_input_halfword(boundary_word)) {
            return false;
        }
        if (boundary_word == kEndCode) {
            if (state_.remaining_bytes < 2U) {
                latch_fault(Fault::TruncatedStream);
                return true;
            }
            (void)consume_input_halfword(boundary_word);
            state_.remaining_bytes -= 2U;
            if (!add_cycles(kInputHalfwordCycleCost)) {
                return true;
            }
            if (state_.remaining_bytes == 0U) {
                finish_command();
            }
            return true;
        }
        if (state_.output_count + required_words > kOutputCapacityWords) {
            return false;
        }
        state_.macroblock_started = true;
        state_.decode_block = 0U;
        state_.block_phase = BlockPhase::NeedHeader;
        state_.coefficient_position = 0U;
    }

    if (state_.remaining_bytes == 0U) {
        latch_fault(Fault::TruncatedStream);
        return true;
    }

    if (state_.block_phase == BlockPhase::NeedHeader) {
        if (state_.remaining_bytes < 2U) {
            latch_fault(Fault::TruncatedStream);
            return true;
        }
        std::uint16_t value = 0U;
        if (!peek_input_halfword(value)) {
            return false;
        }
        (void)consume_input_halfword(value);
        state_.remaining_bytes -= 2U;
        if (!add_cycles(kInputHalfwordCycleCost)) {
            return true;
        }
        if (value == kEndCode) {
            if (state_.remaining_bytes == 0U) {
                latch_fault(Fault::TruncatedStream);
                return true;
            }
            return true;
        }

        const std::uint8_t q_scale = static_cast<std::uint8_t>(value >> 10U);
        const std::int32_t dc = sign_extend_10(value);
        const std::uint8_t quant = state_.decode_block < 2U
                                       ? state_.quant_chroma[0]
                                       : state_.quant_luma[0];
        state_.work_block.fill(0);
        state_.q_scale = q_scale;
        state_.work_block[0] = quantized_dc(dc, quant, q_scale);
        state_.coefficient_position = 1U;
        state_.block_phase = BlockPhase::NeedAc;
        if (state_.remaining_bytes == 0U) {
            latch_fault(Fault::TruncatedStream);
        }
        return true;
    }

    if (state_.block_phase != BlockPhase::NeedAc) {
        latch_fault(Fault::MalformedStream);
        return true;
    }
    if (state_.remaining_bytes < 2U) {
        latch_fault(Fault::TruncatedStream);
        return true;
    }
    std::uint16_t value = 0U;
    if (!peek_input_halfword(value)) {
        return false;
    }
    (void)consume_input_halfword(value);
    state_.remaining_bytes -= 2U;
    if (!add_cycles(kInputHalfwordCycleCost)) {
        return true;
    }
    if (value == kEndCode) {
        return finish_block();
    }

    const std::size_t next = static_cast<std::size_t>(state_.coefficient_position) +
                             static_cast<std::size_t>((value >> 10U) & 0x003FU);
    if (next >= 64U) {
        // FE00 is the conventional terminator, but the silicon terminates on
        // any run that advances beyond coefficient 63. The out-of-range
        // value is not stored.
        return finish_block();
    }
    const std::int32_t ac = sign_extend_10(value);
    const std::uint8_t quant = state_.decode_block < 2U
                                   ? state_.quant_chroma[next]
                                   : state_.quant_luma[next];
    const std::size_t destination = state_.q_scale == 0U
                                        ? next
                                        : static_cast<std::size_t>(kZagzig[next]);
    state_.work_block[destination] = quantized_ac(ac, quant, state_.q_scale);
    state_.coefficient_position = static_cast<std::uint8_t>(next + 1U);
    if (state_.coefficient_position == 64U) {
        return finish_block();
    }
    return true;
}

bool Device::finish_block() noexcept
{
    idct_block(state_.work_block, state_.scale, state_.blocks[state_.decode_block]);
    if (!add_cycles(kIdctMultiplyCycleCost)) {
        return true;
    }
    ++state_.decode_block;
    state_.block_phase = BlockPhase::NeedHeader;
    state_.coefficient_position = 0U;
    if (state_.decode_block ==
        ((state_.depth == OutputDepth::TwentyFourBit ||
          state_.depth == OutputDepth::FifteenBit)
             ? 6U
             : 1U)) {
        if (!emit_macroblock()) {
            return true;
        }
        state_.macroblock_started = false;
        state_.decode_block = 0U;
    }
    return true;
}

bool Device::append_output_bytes(const std::uint8_t* bytes,
                                 std::size_t size,
                                 std::uint8_t block) noexcept
{
    if (bytes == nullptr || size == 0U || (size % 4U) != 0U) {
        latch_fault(Fault::MalformedStream);
        return false;
    }
    const std::size_t words = size / 4U;
    if (words > kOutputCapacityWords - state_.output_count) {
        latch_fault(Fault::OutputFifoFull);
        return false;
    }
    for (std::size_t offset = 0U; offset < size; offset += 4U) {
        const std::uint32_t word = static_cast<std::uint32_t>(bytes[offset]) |
                                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        state_.output[state_.output_tail] = word;
        state_.output_block[state_.output_tail] = block;
        state_.output_tail = static_cast<std::uint16_t>(
            (static_cast<std::size_t>(state_.output_tail) + 1U) % kOutputCapacityWords);
        ++state_.output_count;
    }
    return true;
}

bool Device::emit_monochrome() noexcept
{
    std::array<std::uint8_t, 64> pixels{};
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        std::int32_t value = mono_sample(state_.blocks[0][index]);
        if (!state_.output_signed) {
            value += 128;
        }
        pixels[index] = as_byte(value);
    }

    if (!add_cycles(kPixelCycleCost * pixels.size())) {
        return false;
    }
    if (state_.depth == OutputDepth::EightBit) {
        return append_output_bytes(pixels.data(), pixels.size(), 4U);
    }

    std::array<std::uint8_t, 32> packed{};
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        const std::uint8_t nibble = state_.output_signed
                                        ? static_cast<std::uint8_t>(pixels[index] & 0x0FU)
                                        : static_cast<std::uint8_t>(pixels[index] >> 4U);
        const std::size_t destination = index / 2U;
        const unsigned shift = static_cast<unsigned>((index & 1U) * 4U);
        packed[destination] = static_cast<std::uint8_t>(
            packed[destination] | static_cast<std::uint8_t>(nibble << shift));
    }
    return append_output_bytes(packed.data(), packed.size(), 4U);
}

bool Device::emit_color() noexcept
{
    std::array<std::uint8_t, 768> raster{};
    std::size_t output_offset = 0U;
    for (std::size_t global_y = 0U; global_y < 16U; ++global_y) {
        for (std::size_t global_x = 0U; global_x < 16U; ++global_x) {
                const std::size_t quadrant = (global_y >= 8U ? 2U : 0U) +
                                             (global_x >= 8U ? 1U : 0U);
                const std::size_t local_x = global_x & 7U;
                const std::size_t local_y = global_y & 7U;
                const std::size_t chroma_index = (global_x / 2U) + (global_y / 2U) * 8U;
                const std::int32_t y_value = state_.blocks[quadrant + 2U]
                                                         [local_x + local_y * 8U];
                const std::int32_t cr = state_.blocks[0][chroma_index];
                const std::int32_t cb = state_.blocks[1][chroma_index];
                std::int32_t red = y_value + mdec_divide_add(
                                                static_cast<std::int64_t>(cr) * kRMultiplier,
                                                12U, 2048);
                std::int32_t green = y_value - mdec_divide_add(
                                                  static_cast<std::int64_t>(cb) *
                                                      kGBlueMultiplier,
                                                  12U, 2048) -
                                     mdec_divide_add(static_cast<std::int64_t>(cr) *
                                                          kGRedMultiplier,
                                                      12U, 2048);
                std::int32_t blue = y_value + mdec_divide_add(
                                                 static_cast<std::int64_t>(cb) * kBMultiplier,
                                                 12U, 2048);
                red = color_sample(red);
                green = color_sample(green);
                blue = color_sample(blue);
                if (!state_.output_signed) {
                    red += 128;
                    green += 128;
                    blue += 128;
                }

                if (state_.depth == OutputDepth::TwentyFourBit) {
                    // The GP0(A0h) packed-24 display stream is R, G, B in
                    // increasing VRAM byte order. MDEC output is DMA'd into
                    // that stream without a channel swizzle.
                    raster[output_offset++] = as_byte(red);
                    raster[output_offset++] = as_byte(green);
                    raster[output_offset++] = as_byte(blue);
                } else {
                    const std::uint32_t packed_pixel =
                        static_cast<std::uint32_t>(component5(red)) |
                        (static_cast<std::uint32_t>(component5(green)) << 5U) |
                        (static_cast<std::uint32_t>(component5(blue)) << 10U) |
                        (state_.output_bit15 ? 0x8000U : 0U);
                    const std::uint16_t pixel = static_cast<std::uint16_t>(packed_pixel);
                    raster[output_offset++] = static_cast<std::uint8_t>(pixel & 0xFFU);
                    raster[output_offset++] = static_cast<std::uint8_t>(pixel >> 8U);
                }
        }
    }
    if (output_offset != output_bytes_for(state_.depth)) {
        latch_fault(Fault::MalformedStream);
        return false;
    }
    if (!add_cycles(kPixelCycleCost * 256U)) {
        return false;
    }
    const std::size_t bytes_per_pixel = state_.depth == OutputDepth::TwentyFourBit ? 3U : 2U;
    std::array<std::uint8_t, 768> blocked{};
    std::size_t blocked_offset = 0U;
    for (std::uint8_t block = 0U; block < 4U; ++block) {
        const std::size_t origin_x = (block & 1U) != 0U ? 8U : 0U;
        const std::size_t origin_y = (block & 2U) != 0U ? 8U : 0U;
        for (std::size_t y = 0U; y < 8U; ++y) {
            const std::size_t source = ((origin_y + y) * 16U + origin_x) * bytes_per_pixel;
            const std::size_t row_bytes = 8U * bytes_per_pixel;
            std::copy_n(raster.data() + source, row_bytes, blocked.data() + blocked_offset);
            blocked_offset += row_bytes;
        }
        const std::size_t block_bytes = 64U * bytes_per_pixel;
        if (!append_output_bytes(blocked.data() + blocked_offset - block_bytes,
                                 block_bytes, block)) {
            return false;
        }
    }
    state_.output_dma_reordered = false;
    return true;
}

bool Device::reorder_color_output_for_dma() noexcept
{
    if (state_.output_dma_reordered ||
        (state_.depth != OutputDepth::TwentyFourBit &&
         state_.depth != OutputDepth::FifteenBit)) {
        return true;
    }
    const std::size_t total_words = output_bytes_for(state_.depth) / 4U;
    if (state_.output_count < total_words) {
        return false;
    }
    const std::size_t bytes_per_pixel = state_.depth == OutputDepth::TwentyFourBit ? 3U : 2U;
    std::array<std::uint8_t, 768> blocked{};
    std::array<std::uint8_t, 768> raster{};
    for (std::size_t index = 0U; index < total_words; ++index) {
        const std::uint32_t word = state_.output[(state_.output_head + index) % kOutputCapacityWords];
        for (unsigned lane = 0U; lane < 4U; ++lane) {
            blocked[index * 4U + lane] = static_cast<std::uint8_t>(word >> (lane * 8U));
        }
    }
    const std::size_t block_bytes = 64U * bytes_per_pixel;
    for (std::size_t block = 0U; block < 4U; ++block) {
        const std::size_t origin_x = (block & 1U) != 0U ? 8U : 0U;
        const std::size_t origin_y = (block & 2U) != 0U ? 8U : 0U;
        for (std::size_t y = 0U; y < 8U; ++y) {
            const std::size_t source = block * block_bytes + y * 8U * bytes_per_pixel;
            const std::size_t destination = ((origin_y + y) * 16U + origin_x) * bytes_per_pixel;
            std::copy_n(blocked.data() + source, 8U * bytes_per_pixel,
                        raster.data() + destination);
        }
    }
    for (std::size_t index = 0U; index < total_words; ++index) {
        std::uint32_t word = 0U;
        for (unsigned lane = 0U; lane < 4U; ++lane) {
            word |= static_cast<std::uint32_t>(raster[index * 4U + lane]) << (lane * 8U);
        }
        state_.output[(state_.output_head + index) % kOutputCapacityWords] = word;
        state_.output_block[(state_.output_head + index) % kOutputCapacityWords] =
            static_cast<std::uint8_t>(index / (total_words / 4U));
    }
    state_.output_dma_reordered = true;
    return true;
}

bool Device::emit_macroblock() noexcept
{
    if (state_.depth == OutputDepth::TwentyFourBit ||
        state_.depth == OutputDepth::FifteenBit) {
        return emit_color();
    }
    return emit_monochrome();
}

void Device::finish_command() noexcept
{
    state_.command = Command::Idle;
    state_.remaining_bytes = 0U;
    state_.receive_remaining_bytes = 0U;
    state_.expected_bytes = 0U;
    state_.idle_status_low = 0xFFFFU;
    state_.macroblock_started = false;
    state_.decode_block = 0U;
    state_.coefficient_position = 0U;
    state_.block_phase = BlockPhase::NeedHeader;
}

void Device::pump() noexcept
{
    for (std::size_t step = 0U;
         step < kMaxPumpSteps && state_.command != Command::Idle &&
         state_.latched_fault == Fault::None;
         ++step) {
        bool progressed = false;
        switch (state_.command) {
        case Command::Decode:
            progressed = process_decode();
            break;
        case Command::Quantization:
            progressed = process_quantization();
            break;
        case Command::Scale:
            progressed = process_scale();
            break;
        case Command::Idle:
            return;
        }
        if (!progressed) {
            return;
        }
    }
    if (state_.command != Command::Idle && state_.latched_fault == Fault::None) {
        // The fixed input FIFO and one-macroblock work area make this bound
        // unreachable for a valid call. Treat an unexpected non-progressing
        // loop as a fail-closed malformed stream instead of spinning.
        latch_fault(Fault::MalformedStream);
    }
}

TransferResult Device::read_data_word(std::uint32_t& word) noexcept
{
    const std::uint64_t before = state_.cycles;
    TransferResult result{};
    if (state_.latched_fault != Fault::None) {
        result.fault = state_.latched_fault;
        return result;
    }
    pump();
    if (state_.output_count == 0U) {
        result.fault = state_.latched_fault == Fault::None ? Fault::OutputFifoEmpty
                                                            : state_.latched_fault;
        result.cycles = state_.cycles - before;
        return result;
    }
    word = state_.output[state_.output_head];
    state_.output_head = static_cast<std::uint16_t>(
        (static_cast<std::size_t>(state_.output_head) + 1U) % kOutputCapacityWords);
    --state_.output_count;
    if (state_.output_count == 0U) {
        state_.output_dma_reordered = false;
    }
    (void)add_cycles(kRegisterCycleCost);
    pump();
    result.words = 1U;
    result.complete = state_.latched_fault == Fault::None;
    result.fault = state_.latched_fault;
    result.cycles = state_.cycles - before;
    return result;
}

TransferResult Device::dma_write_words(std::span<const std::uint32_t> words) noexcept
{
    const std::uint64_t before = state_.cycles;
    TransferResult result{};
    if (state_.latched_fault != Fault::None) {
        result.fault = state_.latched_fault;
        return result;
    }
    if (!state_.input_dma_enabled || state_.command == Command::Idle) {
        result.fault = Fault::DmaNotRequested;
        return result;
    }
    for (const std::uint32_t word : words) {
        if ((status() & 0x10000000U) == 0U) {
            result.fault = state_.input_count > kInputBytes - 4U ? Fault::InputFifoFull
                                                                 : Fault::DmaNotRequested;
            break;
        }
        const TransferResult one = write_command_word(word);
        if (one.words != 1U) {
            result.fault = one.fault;
            break;
        }
        ++result.words;
        if (one.fault != Fault::None) {
            result.fault = one.fault;
            break;
        }
    }
    result.cycles = state_.cycles - before;
    result.complete = result.fault == Fault::None && result.words == words.size();
    return result;
}

TransferResult Device::dma_read_words(std::span<std::uint32_t> words) noexcept
{
    const std::uint64_t before = state_.cycles;
    TransferResult result{};
    if (state_.latched_fault != Fault::None) {
        result.fault = state_.latched_fault;
        return result;
    }
    if (!state_.output_dma_enabled) {
        result.fault = Fault::DmaNotRequested;
        return result;
    }
    if (!reorder_color_output_for_dma()) {
        result.fault = Fault::OutputFifoEmpty;
        return result;
    }
    for (std::uint32_t& word : words) {
        if ((status() & 0x08000000U) == 0U) {
            result.fault = Fault::OutputFifoEmpty;
            break;
        }
        const TransferResult one = read_data_word(word);
        if (one.words != 1U) {
            result.fault = one.fault;
            break;
        }
        ++result.words;
        if (one.fault != Fault::None) {
            result.fault = one.fault;
            break;
        }
    }
    result.cycles = state_.cycles - before;
    result.complete = result.fault == Fault::None && result.words == words.size();
    return result;
}

TransferResult Device::dma_write_bytes(std::span<const std::byte> bytes,
                                       DmaWidth width) noexcept
{
    TransferResult result{};
    if (width != DmaWidth::Word) {
        result.fault = Fault::UnsupportedDmaWidth;
        return result;
    }
    if ((bytes.size() % 4U) != 0U) {
        result.fault = Fault::DmaAlignment;
        return result;
    }
    const std::uint64_t before = state_.cycles;
    for (std::size_t offset = 0U; offset < bytes.size(); offset += 4U) {
        const std::uint32_t word =
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) <<
             8U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) <<
             16U) |
            (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U])) <<
             24U);
        const TransferResult one = dma_write_words(std::span<const std::uint32_t>(&word, 1U));
        result.words += one.words;
        if (one.fault != Fault::None || one.words != 1U) {
            result.fault = one.fault;
            break;
        }
    }
    result.cycles = state_.cycles - before;
    result.complete = result.fault == Fault::None && result.words == bytes.size() / 4U;
    return result;
}

TransferResult Device::dma_read_bytes(std::span<std::byte> bytes, DmaWidth width) noexcept
{
    TransferResult result{};
    if (width != DmaWidth::Word) {
        result.fault = Fault::UnsupportedDmaWidth;
        return result;
    }
    if ((bytes.size() % 4U) != 0U) {
        result.fault = Fault::DmaAlignment;
        return result;
    }
    const std::uint64_t before = state_.cycles;
    for (std::size_t offset = 0U; offset < bytes.size(); offset += 4U) {
        std::uint32_t word = 0U;
        const TransferResult one = dma_read_words(std::span<std::uint32_t>(&word, 1U));
        result.words += one.words;
        if (one.words == 1U) {
            bytes[offset] = static_cast<std::byte>(word & 0xFFU);
            bytes[offset + 1U] = static_cast<std::byte>((word >> 8U) & 0xFFU);
            bytes[offset + 2U] = static_cast<std::byte>((word >> 16U) & 0xFFU);
            bytes[offset + 3U] = static_cast<std::byte>((word >> 24U) & 0xFFU);
        }
        if (one.fault != Fault::None || one.words != 1U) {
            result.fault = one.fault;
            break;
        }
    }
    result.cycles = state_.cycles - before;
    result.complete = result.fault == Fault::None && result.words == bytes.size() / 4U;
    return result;
}

std::uint8_t Device::current_block() const noexcept
{
    if (state_.output_count != 0U) {
        return static_cast<std::uint8_t>(state_.output_block[state_.output_head] & 0x07U);
    }
    if (state_.command != Command::Decode || !state_.macroblock_started) {
        return 4U;
    }
    if (state_.depth != OutputDepth::TwentyFourBit &&
        state_.depth != OutputDepth::FifteenBit) {
        return 4U;
    }
    return current_block_for_input(state_.decode_block);
}

std::uint16_t Device::status_remaining() const noexcept
{
    if (state_.command == Command::Idle || state_.receive_remaining_bytes == 0U) {
        return state_.command == Command::Idle ? state_.idle_status_low : 0xFFFFU;
    }
    const std::uint32_t words = state_.receive_remaining_bytes / 4U;
    return static_cast<std::uint16_t>(words - 1U);
}

std::uint32_t Device::status() const noexcept
{
    std::uint32_t value = 0U;
    if (state_.output_count == 0U) {
        value |= std::uint32_t{1U} << 31U;
    }
    if (state_.input_count > kInputBytes - 4U || state_.input_last_word_received) {
        value |= 0x40000000U;
    }
    if (state_.command != Command::Idle && state_.latched_fault == Fault::None) {
        value |= 0x20000000U;
    }
    if (state_.input_dma_enabled && state_.command != Command::Idle &&
        state_.receive_remaining_bytes >= 4U && state_.input_count <= kInputBytes - 4U &&
        state_.latched_fault == Fault::None) {
        value |= 0x10000000U;
    }
    if (state_.output_dma_enabled && state_.output_count != 0U &&
        state_.latched_fault == Fault::None) {
        value |= 0x08000000U;
    }
    value |= (static_cast<std::uint32_t>(state_.depth) & 0x03U) << 25U;
    if (state_.output_signed) {
        value |= 0x01000000U;
    }
    if (state_.output_bit15) {
        value |= 0x00800000U;
    }
    value |= (static_cast<std::uint32_t>(current_block()) & 0x07U) << 16U;
    value |= status_remaining();
    return value;
}

Fault Device::fault() const noexcept
{
    return state_.latched_fault;
}

bool Device::command_busy() const noexcept
{
    return state_.command != Command::Idle && state_.latched_fault == Fault::None;
}

bool Device::dma_in_enabled() const noexcept
{
    return state_.input_dma_enabled;
}

bool Device::dma_out_enabled() const noexcept
{
    return state_.output_dma_enabled;
}

std::size_t Device::input_bytes_available() const noexcept
{
    return state_.input_count;
}

std::size_t Device::output_words_available() const noexcept
{
    return state_.output_count;
}

std::uint16_t Device::remaining_parameter_words_minus_one() const noexcept
{
    return status_remaining();
}

std::uint64_t Device::cycles() const noexcept
{
    return state_.cycles;
}

std::uint64_t Device::digest() const noexcept
{
    Snapshot snapshot{};
    if (!save_snapshot(snapshot)) {
        return 0U;
    }
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte byte : snapshot.storage) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint8_t Device::quantization_luma(std::size_t index) const noexcept
{
    return index < state_.quant_luma.size() ? state_.quant_luma[index] : 0U;
}

std::uint8_t Device::quantization_chroma(std::size_t index) const noexcept
{
    return index < state_.quant_chroma.size() ? state_.quant_chroma[index] : 0U;
}

std::int16_t Device::scale_table(std::size_t index) const noexcept
{
    return index < state_.scale.size() ? state_.scale[index] : 0;
}

bool Device::snapshot_valid(const State& candidate) const noexcept
{
    const auto command_value = static_cast<std::uint8_t>(candidate.command);
    const auto phase_value = static_cast<std::uint8_t>(candidate.block_phase);
    const std::size_t expected_input_tail =
        (static_cast<std::size_t>(candidate.input_head) + candidate.input_count) % kInputBytes;
    const std::size_t expected_output_tail =
        (static_cast<std::size_t>(candidate.output_head) + candidate.output_count) %
        kOutputCapacityWords;
    if (candidate.magic != kStateMagic || candidate.version != kStateVersion ||
        candidate.input_head >= kInputBytes || candidate.input_tail >= kInputBytes ||
        candidate.input_count > kInputBytes || candidate.output_head >= kOutputCapacityWords ||
        candidate.output_tail >= kOutputCapacityWords ||
        candidate.output_count > kOutputCapacityWords || command_value > 3U ||
        phase_value > 1U || static_cast<std::uint8_t>(candidate.depth) > 3U ||
        static_cast<std::uint8_t>(candidate.latched_fault) >
            static_cast<std::uint8_t>(Fault::InvalidSnapshot) ||
        candidate.decode_block > 5U || candidate.coefficient_position > 63U ||
        candidate.table_index > 128U || candidate.remaining_bytes > candidate.expected_bytes ||
        candidate.receive_remaining_bytes > candidate.remaining_bytes ||
        (candidate.receive_remaining_bytes % 4U) != 0U ||
        expected_input_tail != candidate.input_tail ||
        expected_output_tail != candidate.output_tail) {
        return false;
    }
    if (candidate.command == Command::Idle &&
        (candidate.remaining_bytes != 0U || candidate.receive_remaining_bytes != 0U ||
         candidate.expected_bytes != 0U || candidate.input_count != 0U ||
         candidate.macroblock_started)) {
        return false;
    }
    if (candidate.command != Command::Idle &&
        (static_cast<std::uint64_t>(candidate.remaining_bytes) !=
             static_cast<std::uint64_t>(candidate.receive_remaining_bytes) +
                 candidate.input_count ||
         candidate.expected_bytes == 0U ||
         candidate.input_last_word_received != (candidate.receive_remaining_bytes == 0U))) {
        return false;
    }
    if (candidate.latched_fault != Fault::None && candidate.command != Command::Idle) {
        return false;
    }
    if (candidate.depth != static_cast<OutputDepth>((candidate.command_word >> 27U) & 0x03U) ||
        candidate.output_signed != ((candidate.command_word & 0x04000000U) != 0U) ||
        candidate.output_bit15 != ((candidate.command_word & 0x02000000U) != 0U)) {
        return false;
    }
    if (candidate.command == Command::Scale && candidate.expected_bytes != 128U) {
        return false;
    }
    if (candidate.command == Command::Scale && (candidate.command_word >> 29U) != 3U) {
        return false;
    }
    if (candidate.command == Command::Scale &&
        static_cast<std::uint32_t>(candidate.table_index) * 2U +
                candidate.remaining_bytes !=
            candidate.expected_bytes) {
        return false;
    }
    if (candidate.command == Command::Quantization &&
        candidate.expected_bytes != (candidate.color_quantization ? 128U : 64U)) {
        return false;
    }
    if (candidate.command == Command::Quantization &&
        ((candidate.command_word >> 29U) != 2U ||
         candidate.color_quantization != ((candidate.command_word & 1U) != 0U))) {
        return false;
    }
    if (candidate.command == Command::Quantization &&
        static_cast<std::uint32_t>(candidate.table_index) + candidate.remaining_bytes !=
            candidate.expected_bytes) {
        return false;
    }
    if (candidate.command == Command::Decode && (candidate.expected_bytes % 4U) != 0U) {
        return false;
    }
    if (candidate.command == Command::Decode &&
        (candidate.command_word >> 29U) != 1U) {
        return false;
    }
    if (candidate.command == Command::Decode &&
        candidate.expected_bytes != (candidate.command_word & 0xFFFFU) * 4U) {
        return false;
    }
    if (candidate.command != Command::Decode && candidate.macroblock_started) {
        return false;
    }
    if (candidate.command == Command::Decode && !candidate.macroblock_started &&
        (candidate.decode_block != 0U || candidate.coefficient_position != 0U ||
         candidate.block_phase != BlockPhase::NeedHeader)) {
        return false;
    }
    if (candidate.command == Command::Decode && candidate.macroblock_started) {
        if (candidate.block_phase == BlockPhase::NeedHeader &&
            candidate.coefficient_position != 0U) {
            return false;
        }
        if (candidate.block_phase == BlockPhase::NeedAc &&
            (candidate.coefficient_position == 0U || candidate.coefficient_position > 63U)) {
            return false;
        }
    }
    if (candidate.command == Command::Decode && candidate.q_scale > 63U) {
        return false;
    }
    for (const std::uint8_t block : candidate.output_block) {
        if (block > 5U) {
            return false;
        }
    }
    if (candidate.output_dma_reordered &&
        (candidate.output_count == 0U ||
         (candidate.depth != OutputDepth::TwentyFourBit &&
          candidate.depth != OutputDepth::FifteenBit))) {
        return false;
    }
    return true;
}

bool Device::save_snapshot(Snapshot& snapshot) const noexcept
{
    snapshot.storage.fill(std::byte{0});
    SnapshotWriter writer(snapshot.storage);
    (void)writer.write_u32(state_.magic);
    (void)writer.write_u32(state_.version);
    for (const std::uint8_t value : state_.input) {
        (void)writer.write_u8(value);
    }
    (void)writer.write_u16(state_.input_head);
    (void)writer.write_u16(state_.input_tail);
    (void)writer.write_u16(state_.input_count);
    for (const std::uint32_t value : state_.output) {
        (void)writer.write_u32(value);
    }
    for (const std::uint8_t value : state_.output_block) {
        (void)writer.write_u8(value);
    }
    (void)writer.write_u16(state_.output_head);
    (void)writer.write_u16(state_.output_tail);
    (void)writer.write_u16(state_.output_count);
    for (const std::uint8_t value : state_.quant_luma) {
        (void)writer.write_u8(value);
    }
    for (const std::uint8_t value : state_.quant_chroma) {
        (void)writer.write_u8(value);
    }
    for (const std::int16_t value : state_.scale) {
        (void)writer.write_i16(value);
    }
    for (const auto& block : state_.blocks) {
        for (const std::int32_t value : block) {
            (void)writer.write_i32(value);
        }
    }
    for (const std::int32_t value : state_.work_block) {
        (void)writer.write_i32(value);
    }
    (void)writer.write_u8(static_cast<std::uint8_t>(state_.command));
    (void)writer.write_u8(static_cast<std::uint8_t>(state_.block_phase));
    (void)writer.write_u8(static_cast<std::uint8_t>(state_.depth));
    (void)writer.write_u8(static_cast<std::uint8_t>(state_.latched_fault));
    (void)writer.write_u32(state_.command_word);
    (void)writer.write_u32(state_.remaining_bytes);
    (void)writer.write_u32(state_.receive_remaining_bytes);
    (void)writer.write_u32(state_.expected_bytes);
    (void)writer.write_u16(state_.idle_status_low);
    (void)writer.write_u8(state_.decode_block);
    (void)writer.write_u8(state_.coefficient_position);
    (void)writer.write_u8(state_.q_scale);
    (void)writer.write_u8(state_.table_index);
    (void)writer.write_u8(state_.output_signed ? 1U : 0U);
    (void)writer.write_u8(state_.output_bit15 ? 1U : 0U);
    (void)writer.write_u8(state_.color_quantization ? 1U : 0U);
    (void)writer.write_u8(state_.input_dma_enabled ? 1U : 0U);
    (void)writer.write_u8(state_.output_dma_enabled ? 1U : 0U);
    (void)writer.write_u8(state_.macroblock_started ? 1U : 0U);
    (void)writer.write_u8(state_.input_last_word_received ? 1U : 0U);
    (void)writer.write_u8(state_.output_dma_reordered ? 1U : 0U);
    (void)writer.write_u64(state_.cycles);
    return writer.valid();
}

bool Device::load_snapshot(const Snapshot& snapshot) noexcept
{
    State candidate{};
    SnapshotReader reader(snapshot.storage);
    (void)reader.read_u32(candidate.magic);
    (void)reader.read_u32(candidate.version);
    for (std::uint8_t& value : candidate.input) {
        (void)reader.read_u8(value);
    }
    (void)reader.read_u16(candidate.input_head);
    (void)reader.read_u16(candidate.input_tail);
    (void)reader.read_u16(candidate.input_count);
    for (std::uint32_t& value : candidate.output) {
        (void)reader.read_u32(value);
    }
    for (std::uint8_t& value : candidate.output_block) {
        (void)reader.read_u8(value);
    }
    (void)reader.read_u16(candidate.output_head);
    (void)reader.read_u16(candidate.output_tail);
    (void)reader.read_u16(candidate.output_count);
    for (std::uint8_t& value : candidate.quant_luma) {
        (void)reader.read_u8(value);
    }
    for (std::uint8_t& value : candidate.quant_chroma) {
        (void)reader.read_u8(value);
    }
    for (std::int16_t& value : candidate.scale) {
        (void)reader.read_i16(value);
    }
    for (auto& block : candidate.blocks) {
        for (std::int32_t& value : block) {
            (void)reader.read_i32(value);
        }
    }
    for (std::int32_t& value : candidate.work_block) {
        (void)reader.read_i32(value);
    }

    std::uint8_t command = 0U;
    std::uint8_t phase = 0U;
    std::uint8_t depth = 0U;
    std::uint8_t fault = 0U;
    (void)reader.read_u8(command);
    (void)reader.read_u8(phase);
    (void)reader.read_u8(depth);
    (void)reader.read_u8(fault);
    (void)reader.read_u32(candidate.command_word);
    (void)reader.read_u32(candidate.remaining_bytes);
    (void)reader.read_u32(candidate.receive_remaining_bytes);
    (void)reader.read_u32(candidate.expected_bytes);
    (void)reader.read_u16(candidate.idle_status_low);
    (void)reader.read_u8(candidate.decode_block);
    (void)reader.read_u8(candidate.coefficient_position);
    (void)reader.read_u8(candidate.q_scale);
    (void)reader.read_u8(candidate.table_index);

    std::array<std::uint8_t, 8> flags{};
    for (std::uint8_t& value : flags) {
        (void)reader.read_u8(value);
    }
    (void)reader.read_u64(candidate.cycles);

    if (!reader.valid() || !reader.trailing_zero() || command > 3U || phase > 1U ||
        depth > 3U || fault > static_cast<std::uint8_t>(Fault::InvalidSnapshot) ||
        std::any_of(flags.begin(), flags.end(), [](std::uint8_t value) { return value > 1U; })) {
        return false;
    }
    candidate.command = static_cast<Command>(command);
    candidate.block_phase = static_cast<BlockPhase>(phase);
    candidate.depth = static_cast<OutputDepth>(depth);
    candidate.latched_fault = static_cast<Fault>(fault);
    candidate.output_signed = flags[0] != 0U;
    candidate.output_bit15 = flags[1] != 0U;
    candidate.color_quantization = flags[2] != 0U;
    candidate.input_dma_enabled = flags[3] != 0U;
    candidate.output_dma_enabled = flags[4] != 0U;
    candidate.macroblock_started = flags[5] != 0U;
    candidate.input_last_word_received = flags[6] != 0U;
    candidate.output_dma_reordered = flags[7] != 0U;
    if (!snapshot_valid(candidate)) {
        return false;
    }
    state_ = candidate;
    return true;
}

} // namespace clean_aot::mdec
