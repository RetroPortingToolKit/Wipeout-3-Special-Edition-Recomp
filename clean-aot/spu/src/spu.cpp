#include "clean_aot/spu/spu.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace clean_aot::spu {

namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint16_t kTransferControlMask = 0x000Eu;
constexpr std::uint32_t kBlockSpanFixed =
    static_cast<std::uint32_t>(AdpcmSamplesPerBlock << 12u);
constexpr std::array<std::int16_t, 5u> kPredictorOne = {
    0, 60, 115, 98, 122,
};
constexpr std::array<std::int16_t, 5u> kPredictorTwo = {
    0, 0, -52, -55, -60,
};

/*
 * Independently entered public SPU four-point coefficient table.  The
 * hardware documentation calls this the Gaussian table; the coefficients
 * are kept as literal fixed-width values so no host math participates in the
 * device path.  The four taps sum to 0x7F80 +/- 1 at every phase.
 */
constexpr std::array<std::int16_t, 512u> kInterpolationTable = {
    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
    0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
    0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0007, 0x0007,
    0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
    0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018,
    0x0019, 0x001B, 0x001C, 0x001E, 0x0020, 0x0021, 0x0023, 0x0025,
    0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
    0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050,
    0x0054, 0x0057, 0x005B, 0x005F, 0x0063, 0x0067, 0x006B, 0x006F,
    0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
    0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7,
    0x00CD, 0x00D4, 0x00DB, 0x00E3, 0x00EA, 0x00F2, 0x00FA, 0x0101,
    0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
    0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C,
    0x01A8, 0x01B4, 0x01C0, 0x01CC, 0x01D9, 0x01E5, 0x01F2, 0x0200,
    0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
    0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9,
    0x030B, 0x031D, 0x0330, 0x0343, 0x0356, 0x036A, 0x037E, 0x0392,
    0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
    0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506,
    0x0520, 0x053B, 0x0556, 0x0572, 0x058E, 0x05AA, 0x05C7, 0x05E4,
    0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
    0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF,
    0x0813, 0x0838, 0x085D, 0x0883, 0x08A9, 0x08D0, 0x08F7, 0x091E,
    0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
    0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8,
    0x0C07, 0x0C38, 0x0C68, 0x0C99, 0x0CCB, 0x0CFD, 0x0D30, 0x0D63,
    0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
    0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB,
    0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
    0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
    0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
    0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
    0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
    0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
    0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
    0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
    0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
    0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
    0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
    0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
    0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
    0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
    0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
    0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
    0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
    0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
    0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
    0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
    0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
    0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
    0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
    0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
    0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
    0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
    0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
    0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
    0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
    0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
    0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
    0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
    0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3,
};

constexpr std::uint8_t kSnapshotMagic[8u] = {
    static_cast<std::uint8_t>('S'), static_cast<std::uint8_t>('P'),
    static_cast<std::uint8_t>('U'), static_cast<std::uint8_t>('S'),
    static_cast<std::uint8_t>('N'), static_cast<std::uint8_t>('A'),
    static_cast<std::uint8_t>('P'), static_cast<std::uint8_t>('1'),
};

constexpr std::uint8_t to_u8(std::byte value) noexcept {
    return std::to_integer<std::uint8_t>(value);
}

constexpr std::byte to_byte(std::uint8_t value) noexcept {
    return static_cast<std::byte>(value);
}

} // namespace

struct Device::SnapshotWriter final {
    std::span<std::byte> destination;
    std::size_t position{0u};
    std::uint64_t digest{kFnvOffset};
    bool good{true};

    void put_u8(std::uint8_t value) noexcept {
        if (position >= destination.size()) {
            good = false;
            return;
        }
        destination[position] = to_byte(value);
        position += 1u;
        digest ^= value;
        digest *= kFnvPrime;
    }

    void put_u16(std::uint16_t value) noexcept {
        put_u8(static_cast<std::uint8_t>(value & 0x00FFu));
        put_u8(static_cast<std::uint8_t>(value >> 8u));
    }

    void put_u32(std::uint32_t value) noexcept {
        put_u16(static_cast<std::uint16_t>(value & 0xFFFFu));
        put_u16(static_cast<std::uint16_t>(value >> 16u));
    }

    void put_u64(std::uint64_t value) noexcept {
        put_u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
        put_u32(static_cast<std::uint32_t>(value >> 32u));
    }
};

struct Device::DigestSink final {
    std::uint64_t digest{kFnvOffset};

    void put_u8(std::uint8_t value) noexcept {
        digest ^= value;
        digest *= kFnvPrime;
    }

    void put_u16(std::uint16_t value) noexcept {
        put_u8(static_cast<std::uint8_t>(value & 0x00FFu));
        put_u8(static_cast<std::uint8_t>(value >> 8u));
    }

    void put_u32(std::uint32_t value) noexcept {
        put_u16(static_cast<std::uint16_t>(value & 0xFFFFu));
        put_u16(static_cast<std::uint16_t>(value >> 16u));
    }

    void put_u64(std::uint64_t value) noexcept {
        put_u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
        put_u32(static_cast<std::uint32_t>(value >> 32u));
    }
};

struct Device::SnapshotReader final {
    std::span<const std::byte> source;
    std::size_t position{0u};
    std::uint64_t digest{kFnvOffset};
    bool good{true};

    std::uint8_t get_u8() noexcept {
        if (position >= source.size()) {
            good = false;
            return 0u;
        }
        const std::uint8_t value = to_u8(source[position]);
        position += 1u;
        digest ^= value;
        digest *= kFnvPrime;
        return value;
    }

    std::uint16_t get_u16() noexcept {
        const std::uint16_t low = get_u8();
        const std::uint16_t high = get_u8();
        return static_cast<std::uint16_t>(low | (high << 8u));
    }

    std::uint32_t get_u32() noexcept {
        const std::uint32_t low = get_u16();
        const std::uint32_t high = get_u16();
        return low | (high << 16u);
    }

    std::uint64_t get_u64() noexcept {
        const std::uint64_t low = get_u32();
        const std::uint64_t high = get_u32();
        return low | (high << 32u);
    }

    void skip(std::size_t bytes) noexcept {
        const std::size_t remaining = position < source.size()
                                          ? source.size() - position
                                          : 0u;
        if (bytes > remaining) {
            good = false;
            position = source.size();
            return;
        }
        for (std::size_t index = 0u; index < bytes; ++index) {
            static_cast<void>(get_u8());
        }
    }
};

Device::Device() noexcept {
    reset();
}

std::int32_t Device::sign_extend(std::uint32_t value,
                                 std::uint32_t bits) noexcept {
    const std::uint32_t sign_bit = static_cast<std::uint32_t>(1u << (bits - 1u));
    const std::uint32_t mask = static_cast<std::uint32_t>((sign_bit << 1u) - 1u);
    const std::uint32_t clipped = value & mask;
    if ((clipped & sign_bit) == 0u) {
        return static_cast<std::int32_t>(clipped);
    }
    const std::uint32_t magnitude = static_cast<std::uint32_t>(
        (static_cast<std::uint32_t>(~clipped) & mask) + 1u);
    return -static_cast<std::int32_t>(magnitude);
}

std::int16_t Device::to_i16(std::uint16_t bits) noexcept {
    const std::int32_t value = (bits & 0x8000u) == 0u
                                   ? static_cast<std::int32_t>(bits)
                                   : static_cast<std::int32_t>(bits) - 65536;
    return static_cast<std::int16_t>(value);
}

std::uint16_t Device::bits_of_i16(std::int16_t value) noexcept {
    return static_cast<std::uint16_t>(value);
}

std::int32_t Device::arithmetic_shift_right(std::int32_t value,
                                            unsigned shift) noexcept {
    if (shift == 0u) {
        return value;
    }
    if (shift >= 31u) {
        return value < 0 ? -1 : 0;
    }
    const std::int64_t wide = static_cast<std::int64_t>(value);
    const std::int64_t divisor = static_cast<std::int64_t>(1ull << shift);
    if (wide >= 0) {
        return static_cast<std::int32_t>(wide / divisor);
    }
    const std::int64_t magnitude = -wide;
    return static_cast<std::int32_t>(-((magnitude + divisor - 1) / divisor));
}

std::int64_t Device::arithmetic_shift_right(std::int64_t value,
                                            unsigned shift) noexcept {
    if (shift == 0u) {
        return value;
    }
    if (shift >= 63u) {
        return value < 0 ? -1 : 0;
    }
    const std::int64_t divisor = static_cast<std::int64_t>(1ull << shift);
    if (value >= 0) {
        return value / divisor;
    }
    const std::int64_t magnitude = -value;
    return -((magnitude + divisor - 1) / divisor);
}

std::int16_t Device::saturate_i16(std::int64_t value) noexcept {
    if (value < -32768) {
        return static_cast<std::int16_t>(-32768);
    }
    if (value > 32767) {
        return static_cast<std::int16_t>(32767);
    }
    return static_cast<std::int16_t>(value);
}

std::int32_t Device::saturate_i32(std::int64_t value) noexcept {
    constexpr std::int64_t minimum =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
    constexpr std::int64_t maximum =
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
    if (value < minimum) {
        return std::numeric_limits<std::int32_t>::min();
    }
    if (value > maximum) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t Device::multiply_q15(std::int32_t left,
                                  std::int32_t right) noexcept {
    const std::int64_t product = static_cast<std::int64_t>(left) *
                                 static_cast<std::int64_t>(right);
    return saturate_i32(arithmetic_shift_right(product, 15u));
}

std::int64_t Device::multiply_q15_wide(std::int64_t left,
                                       std::int32_t right) noexcept {
    const std::int64_t product = left * static_cast<std::int64_t>(right);
    return arithmetic_shift_right(product, 15u);
}

std::int16_t Device::decode_fixed_volume(std::uint16_t raw) noexcept {
    const std::int32_t signed_value = sign_extend(raw & 0x7FFFu, 15u);
    return static_cast<std::int16_t>(signed_value * 2);
}

std::uint16_t Device::volume_register_for(
    const VolumeControl& control) noexcept {
    return control.raw;
}

Device::TransferMode Device::transfer_mode(std::uint16_t control) noexcept {
    return static_cast<TransferMode>((control >> 4u) & 0x3u);
}

std::size_t Device::capture_offset(std::size_t channel,
                                   std::size_t sample) noexcept {
    return (channel * CaptureSamplesPerHalf * sizeof(std::int16_t)) +
           (sample * sizeof(std::int16_t));
}

std::uint32_t Device::wrap_ram(std::uint32_t address) noexcept {
    return address & RamMask;
}

namespace {

std::int32_t signed32_from_bits(std::uint32_t bits) noexcept {
    if ((bits & 0x80000000u) == 0u) {
        return static_cast<std::int32_t>(bits);
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(bits) -
                                     0x100000000ll);
}

} // namespace

void Device::reset_voice(Voice& voice) noexcept {
    voice = Voice{};
}

void Device::reset() noexcept {
    sound_ram_.fill(0u);
    for (Voice& voice : voices_) {
        reset_voice(voice);
    }
    main_left_ = VolumeControl{};
    main_right_ = VolumeControl{};
    reverb_output_left_ = 0u;
    reverb_output_right_ = 0u;
    cd_volume_left_ = 0;
    cd_volume_right_ = 0;
    external_volume_left_ = 0;
    external_volume_right_ = 0;
    control_ = 0u;
    transfer_control_ = 0u;
    transfer_address_register_ = 0u;
    transfer_current_byte_ = 0u;
    irq_address_register_ = 0u;
    reverb_base_register_ = 0u;
    reverb_cursor_byte_ = 0u;
    reverb_registers_.fill(0u);
    transfer_fifo_.fill(0u);
    transfer_fifo_head_ = 0u;
    transfer_fifo_tail_ = 0u;
    transfer_fifo_count_ = 0u;
    pitch_modulation_mask_ = 0u;
    noise_enable_mask_ = 0u;
    reverb_enable_mask_ = 0u;
    end_flags_ = 0u;
    capture_index_ = 0u;
    noise_level_ = 0;
    noise_timer_ = 0;
    sample_counter_ = 0u;
    reverb_hold_left_ = 0;
    reverb_hold_right_ = 0;
    irq_pending_ = false;
    audio_source_ = AudioSource::Cdda;
    cdda_fifo_ = StereoFifo<AudioFifoCapacity>{};
    xa_fifo_ = StereoFifo<AudioFifoCapacity>{};
    external_fifo_ = StereoFifo<AudioFifoCapacity>{};
}

void Device::write_volume(VolumeControl& volume,
                          std::uint16_t raw) noexcept {
    volume.raw = raw;
    if ((raw & 0x8000u) == 0u) {
        volume.current = decode_fixed_volume(raw);
        volume.counter = 0u;
    }
}

void Device::advance_volume(VolumeControl& volume) noexcept {
    if ((volume.raw & 0x8000u) == 0u) {
        return;
    }
    const std::uint32_t shift = static_cast<std::uint32_t>((volume.raw >> 2u) &
                                                            0x1Fu);
    const std::uint32_t step_value = static_cast<std::uint32_t>(volume.raw & 3u);
    std::int32_t step = static_cast<std::int32_t>(7u - step_value);
    const bool exponential = (volume.raw & 0x4000u) != 0u;
    const bool decreasing = (volume.raw & 0x2000u) != 0u;
    const bool negative_phase = (volume.raw & 0x1000u) != 0u;

    if (decreasing != negative_phase) {
        step = -step - 1;
    }
    if (shift < 11u) {
        step *= static_cast<std::int32_t>(1u << (11u - shift));
    }
    std::uint32_t increment = shift > 11u
                                 ? (0x8000u >> std::min(shift - 11u, 15u))
                                 : 0x8000u;
    if ((step_value | (shift << 2u)) != 0x7Fu && increment == 0u) {
        increment = 1u;
    }
    if (exponential && decreasing) {
        step = saturate_i32(arithmetic_shift_right(
            static_cast<std::int64_t>(step) * volume.current, 15u));
    }
    if (exponential && !decreasing && volume.current > 0x6000) {
        if (shift < 10u) {
            step = arithmetic_shift_right(step, 2u);
        } else if (shift >= 11u) {
            increment = std::max(increment >> 2u, 1u);
        } else {
            step = arithmetic_shift_right(step, 1u);
            increment = std::max(increment >> 1u, 1u);
        }
    }

    const std::uint32_t next_counter =
        static_cast<std::uint32_t>(volume.counter) + increment;
    volume.counter = static_cast<std::uint16_t>(next_counter & 0xFFFFu);
    if ((next_counter & 0x8000u) == 0u) {
        return;
    }
    std::int32_t level = static_cast<std::int32_t>(volume.current) + step;
    if (!decreasing) {
        if (negative_phase) {
            level = std::clamp(level, -32768, 0);
        } else {
            level = std::clamp(level, -32768, 32767);
        }
    } else if (negative_phase) {
        level = std::clamp(level, -32768, 0);
    } else {
        level = std::max(level, 0);
    }
    volume.current = static_cast<std::int16_t>(level);
}

void Device::key_on(std::uint32_t mask) noexcept {
    for (std::size_t index = 0u; index < VoiceCount; ++index) {
        const std::uint32_t bit = static_cast<std::uint32_t>(1u << index);
        if ((mask & bit) == 0u) {
            continue;
        }
        Voice& voice = voices_[index];
        voice.active = true;
        voice.phase = AdsrPhase::Attack;
        voice.adsr_volume = 0;
        voice.current_adsr_register = 0u;
        voice.adsr_counter = 0u;
        voice.current_address_bytes =
            wrap_ram(static_cast<std::uint32_t>(voice.start_address_units) * 8u);
        voice.block_address_bytes = voice.current_address_bytes;
        voice.pitch_counter = 0u;
        voice.block_sample_index = 0u;
        voice.block_flags = 0u;
        voice.block_valid = false;
        voice.decoder_previous = 0;
        voice.decoder_previous_previous = 0;
        voice.history.fill(0);
        voice.block_samples.fill(0);
        end_flags_ &= ~bit;
    }
}

void Device::key_off(std::uint32_t mask) noexcept {
    for (std::size_t index = 0u; index < VoiceCount; ++index) {
        const std::uint32_t bit = static_cast<std::uint32_t>(1u << index);
        if ((mask & bit) != 0u) {
            release_adsr(voices_[index]);
        }
    }
}

void Device::set_voice_mask(std::uint32_t& destination,
                            std::uint32_t value) noexcept {
    destination = value & ((1u << VoiceCount) - 1u);
}

void Device::apply_control(std::uint16_t value) noexcept {
    const bool previously_enabled =
        (control_ & ControlIrqEnable) != 0u &&
        (control_ & ControlEnable) != 0u;
    const TransferMode previous_mode = transfer_mode(control_);
    if (previous_mode == TransferMode::ManualWrite) {
        service_transfer_fifo();
    }
    control_ = value;
    const TransferMode next_mode = transfer_mode(control_);
    if (next_mode == TransferMode::ManualWrite) {
        service_transfer_fifo();
    }
    if ((control_ & ControlIrqEnable) == 0u) {
        irq_pending_ = false;
    } else if (!previously_enabled &&
               (control_ & ControlEnable) != 0u && irq_pending_ &&
               irq_callback_ != nullptr) {
        irq_callback_(irq_user_);
    }
}

void Device::apply_transfer_control(std::uint16_t value) noexcept {
    transfer_control_ = value & kTransferControlMask;
}

void Device::touch_ram_range(std::uint32_t start,
                             std::uint32_t bytes) noexcept {
    for (std::uint32_t offset = 0u; offset < bytes; offset += 2u) {
        const std::uint32_t address = wrap_ram(start + offset);
        const std::uint32_t irq_byte =
            static_cast<std::uint32_t>(irq_address_register_) * 8u;
        if (address == irq_byte) {
            trigger_irq();
        }
    }
}

void Device::trigger_irq() noexcept {
    if ((control_ & ControlIrqEnable) == 0u ||
        (control_ & ControlEnable) == 0u) {
        return;
    }
    const bool was_pending = irq_pending_;
    irq_pending_ = true;
    if (!was_pending && irq_callback_ != nullptr) {
        irq_callback_(irq_user_);
    }
}

void Device::write_ram_word(std::uint32_t address,
                            std::uint16_t value) noexcept {
    const std::uint32_t wrapped = wrap_ram(address);
    touch_ram_range(wrapped, 2u);
    sound_ram_[wrapped] = static_cast<std::uint8_t>(value & 0x00FFu);
    sound_ram_[wrap_ram(wrapped + 1u)] =
        static_cast<std::uint8_t>(value >> 8u);
}

std::uint16_t Device::read_ram_word(std::uint32_t address) noexcept {
    const std::uint32_t wrapped = wrap_ram(address);
    touch_ram_range(wrapped, 2u);
    const std::uint16_t low = sound_ram_[wrapped];
    const std::uint16_t high = sound_ram_[wrap_ram(wrapped + 1u)];
    return static_cast<std::uint16_t>(low | (high << 8u));
}

void Device::write_transfer_word(std::uint16_t value) noexcept {
    write_ram_word(transfer_current_byte_, value);
    advance_transfer_address();
}

std::uint16_t Device::read_transfer_word() noexcept {
    if (transfer_fifo_count_ != 0u) {
        const std::uint16_t value = transfer_fifo_[transfer_fifo_head_];
        transfer_fifo_head_ = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(transfer_fifo_head_) + 1u) %
            TransferFifoCapacity);
        transfer_fifo_count_ = static_cast<std::uint8_t>(
            transfer_fifo_count_ - 1u);
        return value;
    }
    if (transfer_mode(control_) == TransferMode::DmaRead) {
        const std::uint16_t value = read_ram_word(transfer_current_byte_);
        advance_transfer_address();
        return value;
    }
    return 0u;
}

void Device::service_transfer_fifo() noexcept {
    if (transfer_fifo_count_ == 0u) {
        return;
    }
    const auto fifo_word = [this](std::uint8_t offset) noexcept {
        const std::size_t index =
            (static_cast<std::size_t>(transfer_fifo_head_) + offset) %
            TransferFifoCapacity;
        return transfer_fifo_[index];
    };
    const std::uint8_t count = transfer_fifo_count_;
    const std::uint32_t transfer_type =
        static_cast<std::uint32_t>((transfer_control_ >> 1u) & 0x7u);
    if (transfer_type == 0u || transfer_type == 1u || transfer_type >= 6u) {
        const std::uint16_t fill_value = fifo_word(
            static_cast<std::uint8_t>(count - 1u));
        for (std::uint8_t index = 0u; index < count; ++index) {
            write_transfer_word(fill_value);
        }
    } else if (transfer_type == 2u) {
        for (std::uint8_t index = 0u; index < count; ++index) {
            write_transfer_word(fifo_word(index));
        }
    } else {
        const std::uint8_t repeat = transfer_type == 3u
                                        ? static_cast<std::uint8_t>(2u)
                                        : (transfer_type == 4u
                                               ? static_cast<std::uint8_t>(4u)
                                               : static_cast<std::uint8_t>(8u));
        for (std::uint8_t index = 0u; index < count;
             index = static_cast<std::uint8_t>(index + repeat)) {
            const std::uint8_t source_index = transfer_type == 5u
                                                  ? static_cast<std::uint8_t>(
                                                        index + repeat - 1u)
                                                  : index;
            if (source_index >= count) {
                break;
            }
            for (std::uint8_t repeat_index = 0u; repeat_index < repeat;
                 ++repeat_index) {
                write_transfer_word(fifo_word(source_index));
            }
        }
    }
    transfer_fifo_head_ = 0u;
    transfer_fifo_tail_ = 0u;
    transfer_fifo_count_ = 0u;
}

void Device::advance_transfer_address() noexcept {
    transfer_current_byte_ = wrap_ram(transfer_current_byte_ + 2u);
}

DmaResult Device::dma_write(std::span<const std::uint16_t> words) noexcept {
    if (transfer_mode(control_) != TransferMode::DmaWrite) {
        return DmaResult{DmaFault::WrongTransferMode, 0u};
    }
    if (words.size() > MaxDmaHalfwordsPerCall) {
        return DmaResult{DmaFault::TooManyHalfwords, 0u};
    }
    const std::uint32_t transfer_type =
        static_cast<std::uint32_t>((transfer_control_ >> 1u) & 0x7u);
    std::size_t transferred = 0u;
    if (transfer_type == 0u || transfer_type == 1u || transfer_type >= 6u) {
        const std::uint16_t fill_value = words.empty() ? 0u : words.back();
        for (std::size_t index = 0u; index < words.size(); ++index) {
            write_transfer_word(fill_value);
            transferred += 1u;
        }
    } else if (transfer_type == 2u) {
        for (const std::uint16_t word : words) {
            write_transfer_word(word);
            transferred += 1u;
        }
    } else {
        const std::size_t repeat = transfer_type == 3u
                                       ? 2u
                                       : (transfer_type == 4u ? 4u : 8u);
        for (std::size_t index = 0u; index < words.size(); index += repeat) {
            const std::size_t source_index = transfer_type == 5u
                                                 ? index + repeat - 1u
                                                 : index;
            if (source_index >= words.size()) {
                break;
            }
            for (std::size_t repeat_index = 0u; repeat_index < repeat;
                 ++repeat_index) {
                write_transfer_word(words[source_index]);
            }
        }
        transferred = words.size();
    }
    return DmaResult{DmaFault::None, transferred};
}

DmaResult Device::dma_read(std::span<std::uint16_t> words) noexcept {
    if (transfer_mode(control_) != TransferMode::DmaRead) {
        return DmaResult{DmaFault::WrongTransferMode, 0u};
    }
    if (words.size() > MaxDmaHalfwordsPerCall) {
        return DmaResult{DmaFault::TooManyHalfwords, 0u};
    }
    const std::uint32_t transfer_type =
        static_cast<std::uint32_t>((transfer_control_ >> 1u) & 0x7u);
    std::size_t transferred = 0u;
    if (transfer_type == 0u || transfer_type == 1u || transfer_type >= 6u) {
        for (std::uint16_t& word : words) {
            word = read_ram_word(transfer_current_byte_);
            advance_transfer_address();
            transferred += 1u;
        }
    } else if (transfer_type == 2u) {
        for (std::uint16_t& word : words) {
            word = read_ram_word(transfer_current_byte_);
            advance_transfer_address();
            transferred += 1u;
        }
    } else {
        const std::size_t repeat = transfer_type == 3u
                                       ? 2u
                                       : (transfer_type == 4u ? 4u : 8u);
        for (std::size_t index = 0u; index < words.size(); index += repeat) {
            const std::uint16_t value = read_ram_word(transfer_current_byte_);
            advance_transfer_address();
            const std::size_t count = std::min(repeat, words.size() - index);
            for (std::size_t repeat_index = 0u; repeat_index < count;
                 ++repeat_index) {
                words[index + repeat_index] = value;
            }
            transferred += count;
        }
    }
    return DmaResult{DmaFault::None, transferred};
}

DmaResult Device::dma_read(DmaReadCallback callback,
                            void* user,
                            std::size_t halfwords) noexcept {
    if (callback == nullptr) {
        return DmaResult{DmaFault::CallbackRejected, 0u};
    }
    if (halfwords > MaxDmaHalfwordsPerCall) {
        return DmaResult{DmaFault::TooManyHalfwords, 0u};
    }
    std::array<std::uint16_t, 32u> chunk{};
    std::size_t transferred = 0u;
    while (transferred < halfwords) {
        const std::size_t count = std::min(chunk.size(), halfwords - transferred);
        const DmaResult result = dma_read(
            std::span<std::uint16_t>(chunk.data(), count));
        if (!result.ok()) {
            return DmaResult{result.fault, transferred + result.transferred_halfwords};
        }
        if (!callback(std::span<const std::uint16_t>(chunk.data(), count), user)) {
            return DmaResult{DmaFault::CallbackRejected, transferred};
        }
        transferred += count;
    }
    return DmaResult{DmaFault::None, transferred};
}

void Device::decode_block(Voice& voice, std::uint32_t address) noexcept {
    const std::uint32_t block_address = wrap_ram(address);
    touch_ram_range(block_address, static_cast<std::uint32_t>(AdpcmBlockBytes));

    const std::uint8_t header = sound_ram_[block_address];
    voice.block_flags = sound_ram_[wrap_ram(block_address + 1u)];
    const std::uint32_t filter_number =
        std::min(static_cast<std::uint32_t>(header >> 4u), 4u);
    std::uint32_t shift = static_cast<std::uint32_t>(header & 0x0Fu);
    if (shift > 12u) {
        shift = 9u;
    }
    std::int32_t previous = voice.decoder_previous;
    std::int32_t previous_previous = voice.decoder_previous_previous;
    for (std::size_t sample_index = 0u;
         sample_index < AdpcmSamplesPerBlock;
         ++sample_index) {
        const std::uint32_t data_address = wrap_ram(
            block_address + 2u + static_cast<std::uint32_t>(sample_index / 2u));
        const std::uint8_t packed = sound_ram_[data_address];
        const std::uint8_t nibble = (sample_index & 1u) == 0u
                                        ? static_cast<std::uint8_t>(packed & 0x0Fu)
                                        : static_cast<std::uint8_t>(packed >> 4u);
        const std::int32_t signed_nibble =
            sign_extend(static_cast<std::uint32_t>(nibble), 4u);
        const std::int32_t shifted = arithmetic_shift_right(
            signed_nibble * 4096, static_cast<unsigned>(shift));
        const std::int32_t prediction = arithmetic_shift_right(
            previous * static_cast<std::int32_t>(kPredictorOne[filter_number]) +
                previous_previous *
                    static_cast<std::int32_t>(kPredictorTwo[filter_number]),
            6u);
        const std::int16_t decoded = saturate_i16(
            static_cast<std::int64_t>(shifted) + prediction);
        voice.block_samples[sample_index] = decoded;
        previous_previous = previous;
        previous = decoded;
    }
    voice.decoder_previous = static_cast<std::int16_t>(previous);
    voice.decoder_previous_previous = static_cast<std::int16_t>(
        previous_previous);
    voice.block_address_bytes = block_address;
    voice.current_address_bytes = block_address;
    voice.block_sample_index = 0u;
    voice.block_valid = true;
    if ((voice.block_flags & 0x04u) != 0u) {
        voice.repeat_address_units = static_cast<std::uint16_t>(
            block_address / 8u);
    }
}

void Device::finish_block(Voice& voice) noexcept {
    voice.history[0] = voice.block_samples[25u];
    voice.history[1] = voice.block_samples[26u];
    voice.history[2] = voice.block_samples[27u];
    if ((voice.block_flags & 0x01u) != 0u) {
        const std::size_t voice_index = static_cast<std::size_t>(
            &voice - voices_.data());
        const std::uint32_t bit = static_cast<std::uint32_t>(1u << voice_index);
        end_flags_ |= bit;
        if ((voice.block_flags & 0x02u) != 0u) {
            voice.current_address_bytes = wrap_ram(
                static_cast<std::uint32_t>(voice.repeat_address_units) * 8u);
            voice.block_valid = false;
            return;
        }
        voice.current_address_bytes = wrap_ram(
            static_cast<std::uint32_t>(voice.repeat_address_units) * 8u);
        voice.phase = AdsrPhase::Release;
        voice.adsr_volume = 0;
        voice.current_adsr_register = 0u;
        voice.block_valid = false;
        return;
    }
    voice.current_address_bytes =
        wrap_ram(voice.block_address_bytes + AdpcmBlockBytes);
    voice.block_valid = false;
}

void Device::ensure_block(Voice& voice) noexcept {
    if (!voice.block_valid) {
        decode_block(voice, voice.current_address_bytes);
    }
}

void Device::advance_block(Voice& voice) noexcept {
    if (voice.pitch_counter < kBlockSpanFixed) {
        voice.block_sample_index = static_cast<std::uint8_t>(
            voice.pitch_counter >> 12u);
        return;
    }
    voice.pitch_counter -= kBlockSpanFixed;
    finish_block(voice);
    if (voice.active) {
        ensure_block(voice);
    }
    voice.block_sample_index = static_cast<std::uint8_t>(
        voice.pitch_counter >> 12u);
}

std::int16_t Device::sample_before(const Voice& voice,
                                   std::uint32_t index,
                                   std::uint32_t distance) const noexcept {
    if (distance <= index) {
        return voice.block_samples[index - distance];
    }
    const std::uint32_t history_distance = distance - index;
    if (history_distance > 3u) {
        return 0;
    }
    return voice.history[3u - history_distance];
}

std::int16_t Device::interpolate(const Voice& voice) const noexcept {
    const std::uint32_t index = std::min(
        static_cast<std::uint32_t>(voice.pitch_counter >> 12u), 27u);
    const std::uint32_t phase = (voice.pitch_counter >> 4u) & 0xFFu;
    const std::int32_t oldest = sample_before(voice, index, 3u);
    const std::int32_t older = sample_before(voice, index, 2u);
    const std::int32_t old = sample_before(voice, index, 1u);
    const std::int32_t current = voice.block_samples[index];
    const std::int64_t oldest_term = arithmetic_shift_right(
        static_cast<std::int64_t>(kInterpolationTable[0x0FFu - phase]) *
            oldest,
        15u);
    const std::int64_t older_term = arithmetic_shift_right(
        static_cast<std::int64_t>(kInterpolationTable[0x1FFu - phase]) *
            older,
        15u);
    const std::int64_t old_term = arithmetic_shift_right(
        static_cast<std::int64_t>(kInterpolationTable[0x100u + phase]) * old,
        15u);
    const std::int64_t current_term = arithmetic_shift_right(
        static_cast<std::int64_t>(kInterpolationTable[phase]) * current,
        15u);
    return saturate_i16(oldest_term + older_term + old_term + current_term);
}

std::uint32_t Device::pitch_step(const Voice& voice,
                                 std::int16_t previous_output,
                                 std::size_t index) const noexcept {
    if (voice.pitch == 0u) {
        return 0u;
    }
    std::int64_t step = static_cast<std::int64_t>(voice.pitch);
    if (index != 0u && index < VoiceCount &&
        (pitch_modulation_mask_ & (static_cast<std::uint32_t>(1u << index))) !=
            0u) {
        const std::int32_t factor = static_cast<std::int32_t>(previous_output) +
                                    32768;
        const std::int32_t signed_pitch = sign_extend(voice.pitch, 16u);
        step = arithmetic_shift_right(
            static_cast<std::int64_t>(signed_pitch) * factor, 15u);
        const std::int64_t modulo = step % 65536;
        step = modulo < 0 ? modulo + 65536 : modulo;
    }
    const std::uint32_t unsigned_step = static_cast<std::uint32_t>(step);
    return std::min(unsigned_step, 0x4000u);
}

std::int16_t Device::next_noise() noexcept {
    const std::int32_t step = 4 + static_cast<std::int32_t>(
        (control_ & ControlNoiseStepMask) >> 8u);
    noise_timer_ -= step;
    if (noise_timer_ < 0) {
        const std::uint16_t bits = bits_of_i16(noise_level_);
        const std::uint16_t parity = static_cast<std::uint16_t>(
            ((bits >> 15u) ^ (bits >> 12u) ^ (bits >> 11u) ^
             (bits >> 10u) ^ 1u) & 1u);
        const std::uint16_t shifted = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bits << 1u) | parity);
        noise_level_ = to_i16(shifted);
        const std::uint32_t shift = static_cast<std::uint32_t>(
            (control_ & ControlNoiseShiftMask) >> 10u);
        const std::int32_t period = static_cast<std::int32_t>(
            0x20000u >> std::min(shift, 15u));
        noise_timer_ += period;
        if (noise_timer_ < 0) {
            noise_timer_ += period;
        }
    }
    return noise_level_;
}

void Device::begin_adsr(Voice& voice) noexcept {
    voice.active = true;
    voice.phase = AdsrPhase::Attack;
    voice.adsr_volume = 0;
    voice.current_adsr_register = 0u;
    voice.adsr_counter = 0u;
}

void Device::release_adsr(Voice& voice) noexcept {
    if (!voice.active) {
        return;
    }
    voice.phase = AdsrPhase::Release;
    voice.adsr_counter = 0u;
}

std::int32_t Device::adsr_step(const Voice& voice,
                               bool increasing,
                               bool exponential,
                               std::int32_t level,
                               std::uint16_t& counter) const noexcept {
    std::uint32_t shift = 0u;
    std::uint32_t step_value = 0u;
    switch (voice.phase) {
    case AdsrPhase::Attack:
        shift = (voice.adsr_low >> 10u) & 0x1Fu;
        step_value = (voice.adsr_low >> 8u) & 0x3u;
        break;
    case AdsrPhase::Decay:
        shift = (voice.adsr_low >> 4u) & 0xFu;
        step_value = 7u;
        break;
    case AdsrPhase::Sustain:
        shift = (voice.adsr_high >> 8u) & 0x1Fu;
        step_value = (voice.adsr_high >> 6u) & 0x3u;
        break;
    case AdsrPhase::Release:
        shift = voice.adsr_high & 0x1Fu;
        step_value = 7u;
        break;
    case AdsrPhase::Off:
        return 0;
    }

    std::int32_t step = static_cast<std::int32_t>(7u - step_value);
    if (!increasing) {
        step = -step - 1;
    }
    if (shift < 11u) {
        step *= static_cast<std::int32_t>(1u << (11u - shift));
    }
    std::uint32_t increment = shift > 11u
                                 ? (0x8000u >> std::min(shift - 11u, 15u))
                                 : 0x8000u;
    if ((step_value | (shift << 2u)) != 0x7Fu && increment == 0u) {
        increment = 1u;
    }
    if (exponential && increasing && level > 0x6000) {
        if (shift < 10u) {
            step = arithmetic_shift_right(step, 2u);
        } else if (shift >= 11u) {
            increment = std::max(increment >> 2u, 1u);
        } else {
            step = arithmetic_shift_right(step, 1u);
            increment = std::max(increment >> 1u, 1u);
        }
    } else if (exponential && !increasing) {
        step = saturate_i32(arithmetic_shift_right(
            static_cast<std::int64_t>(step) * level, 15u));
    }

    const std::uint32_t next_counter =
        static_cast<std::uint32_t>(counter) + increment;
    counter = static_cast<std::uint16_t>(next_counter & 0xFFFFu);
    if ((next_counter & 0x8000u) == 0u) {
        return 0;
    }
    return step;
}

void Device::advance_adsr(Voice& voice) noexcept {
    if (!voice.active || voice.phase == AdsrPhase::Off) {
        return;
    }
    const std::int32_t level = voice.adsr_volume;
    bool increasing = false;
    bool exponential = false;
    switch (voice.phase) {
    case AdsrPhase::Attack:
        increasing = true;
        exponential = (voice.adsr_low & 0x8000u) != 0u;
        break;
    case AdsrPhase::Decay:
        exponential = true;
        break;
    case AdsrPhase::Sustain:
        increasing = (voice.adsr_high & 0x4000u) == 0u;
        exponential = (voice.adsr_high & 0x8000u) != 0u;
        break;
    case AdsrPhase::Release:
        exponential = (voice.adsr_high & 0x0020u) != 0u;
        break;
    case AdsrPhase::Off:
        return;
    }

    const std::int32_t step = adsr_step(
        voice, increasing, exponential, level, voice.adsr_counter);
    std::int32_t next_level = level + step;
    if (increasing) {
        next_level = std::clamp(next_level, -32768, 32767);
    } else {
        next_level = std::max(next_level, 0);
    }
    voice.adsr_volume = static_cast<std::int16_t>(next_level);
    voice.current_adsr_register = bits_of_i16(voice.adsr_volume);

    if (voice.phase == AdsrPhase::Attack && voice.adsr_volume >= 0x7FFF) {
        voice.phase = AdsrPhase::Decay;
        voice.adsr_counter = 0u;
    } else if (voice.phase == AdsrPhase::Decay) {
        const std::int32_t sustain_level = std::min(
            static_cast<std::int32_t>(((voice.adsr_low & 0xFu) + 1u) * 0x800u),
            0x7FFF);
        if (voice.adsr_volume <= sustain_level) {
            voice.phase = AdsrPhase::Sustain;
            voice.adsr_counter = 0u;
        }
    } else if (voice.phase == AdsrPhase::Release && voice.adsr_volume <= 0) {
        voice.adsr_volume = 0;
        voice.current_adsr_register = 0u;
        voice.phase = AdsrPhase::Off;
    }
}

std::int16_t Device::voice_source(Voice& voice,
                                  std::int16_t previous_output,
                                  std::size_t index) noexcept {
    if (!voice.active) {
        return 0;
    }
    const bool noise = (noise_enable_mask_ &
                        static_cast<std::uint32_t>(1u << index)) != 0u;
    const std::int16_t source = noise ? next_noise() : interpolate(voice);
    const std::int16_t after_adsr = static_cast<std::int16_t>(multiply_q15(
        static_cast<std::int32_t>(source),
        static_cast<std::int32_t>(voice.adsr_volume)));
    static_cast<void>(previous_output);
    static_cast<void>(index);
    return after_adsr;
}

std::uint32_t Device::wrap_reverb(std::int64_t address) const noexcept {
    const std::uint32_t base = std::min(
        static_cast<std::uint32_t>(reverb_base_register_) * 8u,
        ReverbEndByte);
    const std::int64_t span =
        static_cast<std::int64_t>(ReverbEndByte - base) + 2;
    std::int64_t relative = (address - static_cast<std::int64_t>(base)) % span;
    if (relative < 0) {
        relative += span;
    }
    relative &= ~static_cast<std::int64_t>(1);
    return base + static_cast<std::uint32_t>(relative);
}

std::uint32_t Device::reverb_address(std::uint16_t relative_units,
                                     std::int32_t extra_units) const noexcept {
    const std::int32_t relative = sign_extend(relative_units, 16u) +
                                   extra_units;
    const std::int64_t address = static_cast<std::int64_t>(reverb_cursor_byte_) +
                                 (static_cast<std::int64_t>(relative) * 8);
    return wrap_reverb(address);
}

std::int16_t Device::read_reverb_sample(std::uint32_t address) noexcept {
    return to_i16(read_ram_word(address));
}

void Device::write_reverb_sample(std::uint32_t address,
                                 std::int16_t sample) noexcept {
    write_ram_word(address, bits_of_i16(sample));
}

StereoSample Device::process_reverb(std::int16_t left_input,
                                    std::int16_t right_input,
                                    bool write_buffer) noexcept {
    const auto volume = [this](std::size_t index) noexcept {
        return to_i16(reverb_registers_[index]);
    };
    const std::int16_t v_wall = volume(7u);
    const std::int16_t v_iir = volume(2u);
    const std::int32_t lin = multiply_q15(left_input, volume(30u));
    const std::int32_t rin = multiply_q15(right_input, volume(31u));

    const std::uint32_t same_l_address = reverb_address(reverb_registers_[10u]);
    const std::uint32_t same_r_address = reverb_address(reverb_registers_[11u]);
    const std::uint32_t diff_l_address = reverb_address(reverb_registers_[18u]);
    const std::uint32_t diff_r_address = reverb_address(reverb_registers_[19u]);
    const std::int32_t old_same_l = read_reverb_sample(
        wrap_reverb(static_cast<std::int64_t>(same_l_address) - 2));
    const std::int32_t old_same_r = read_reverb_sample(
        wrap_reverb(static_cast<std::int64_t>(same_r_address) - 2));
    const std::int32_t old_diff_l = read_reverb_sample(
        wrap_reverb(static_cast<std::int64_t>(diff_l_address) - 2));
    const std::int32_t old_diff_r = read_reverb_sample(
        wrap_reverb(static_cast<std::int64_t>(diff_r_address) - 2));
    const std::int32_t delayed_same_l = read_reverb_sample(
        reverb_address(reverb_registers_[16u]));
    const std::int32_t delayed_same_r = read_reverb_sample(
        reverb_address(reverb_registers_[17u]));
    const std::int32_t delayed_diff_l = read_reverb_sample(
        reverb_address(reverb_registers_[25u]));
    const std::int32_t delayed_diff_r = read_reverb_sample(
        reverb_address(reverb_registers_[24u]));

    const auto reflection = [v_wall, v_iir](std::int32_t input,
                                             std::int32_t delayed,
                                             std::int32_t old) noexcept {
        const std::int64_t before = static_cast<std::int64_t>(input) +
                                    Device::multiply_q15(delayed, v_wall) - old;
        return Device::saturate_i16(
            Device::multiply_q15_wide(before, v_iir) + old);
    };
    const std::int16_t same_l = reflection(lin, delayed_same_l, old_same_l);
    const std::int16_t same_r = reflection(rin, delayed_same_r, old_same_r);
    const std::int16_t diff_l = reflection(lin, delayed_diff_l, old_diff_l);
    const std::int16_t diff_r = reflection(rin, delayed_diff_r, old_diff_r);
    if (write_buffer) {
        write_reverb_sample(same_l_address, same_l);
        write_reverb_sample(same_r_address, same_r);
        write_reverb_sample(diff_l_address, diff_l);
        write_reverb_sample(diff_r_address, diff_r);
    }

    const auto comb = [this, &volume](std::size_t first,
                                      std::size_t second,
                                      std::size_t third,
                                      std::size_t fourth,
                                      std::size_t v_index) noexcept {
        std::int64_t value = 0;
        value += multiply_q15_wide(
            read_reverb_sample(reverb_address(reverb_registers_[first])),
            volume(v_index));
        value += multiply_q15_wide(
            read_reverb_sample(reverb_address(reverb_registers_[second])),
            volume(v_index + 1u));
        value += multiply_q15_wide(
            read_reverb_sample(reverb_address(reverb_registers_[third])),
            volume(v_index + 2u));
        value += multiply_q15_wide(
            read_reverb_sample(reverb_address(reverb_registers_[fourth])),
            volume(v_index + 3u));
        return saturate_i16(value);
    };
    std::int32_t out_l = comb(12u, 14u, 20u, 22u, 3u);
    std::int32_t out_r = comb(13u, 15u, 21u, 23u, 3u);

    const auto all_pass = [this, &volume, write_buffer](std::int32_t input,
                                                         std::size_t address_index,
                                                         std::size_t depth_index,
                                                         std::int32_t& output)
        noexcept {
        const std::uint32_t address =
            reverb_address(reverb_registers_[address_index]);
        const std::int32_t delayed = read_reverb_sample(wrap_reverb(
            static_cast<std::int64_t>(address) -
            static_cast<std::int64_t>(sign_extend(
                reverb_registers_[depth_index], 16u)) * 8));
        const std::int16_t first = saturate_i16(
            static_cast<std::int64_t>(input) -
            multiply_q15(delayed, volume(depth_index == 0u ? 8u : 9u)));
        if (write_buffer) {
            write_reverb_sample(address, first);
        }
        output = saturate_i16(
            multiply_q15_wide(first,
                              volume(depth_index == 0u ? 8u : 9u)) + delayed);
    };
    all_pass(out_l, 26u, 0u, out_l);
    all_pass(out_r, 27u, 0u, out_r);
    all_pass(out_l, 28u, 1u, out_l);
    all_pass(out_r, 29u, 1u, out_r);

    reverb_cursor_byte_ = wrap_reverb(
        static_cast<std::int64_t>(reverb_cursor_byte_) + 2);
    const std::int16_t output_left = saturate_i16(
        multiply_q15_wide(out_l, to_i16(reverb_output_left_)));
    const std::int16_t output_right = saturate_i16(
        multiply_q15_wide(out_r, to_i16(reverb_output_right_)));
    return StereoSample{output_left, output_right};
}

template <std::size_t Capacity>
PushResult Device::push_fifo(StereoFifo<Capacity>& fifo,
                             std::span<const StereoSample> samples) noexcept {
    PushResult result{};
    for (const StereoSample& sample : samples) {
        if (fifo.count == Capacity) {
            fifo.overflow = true;
            fifo.overflow_count += 1u;
            result.dropped += 1u;
            result.overflow = true;
            continue;
        }
        fifo.samples[fifo.tail] = sample;
        fifo.tail = static_cast<std::uint16_t>(
            (static_cast<std::size_t>(fifo.tail) + 1u) % Capacity);
        fifo.count = static_cast<std::uint16_t>(fifo.count + 1u);
        result.accepted += 1u;
    }
    return result;
}

template <std::size_t Capacity>
InputStatus Device::fifo_status(const StereoFifo<Capacity>& fifo) noexcept {
    return InputStatus{fifo.count, fifo.underflow_count, fifo.overflow_count,
                       fifo.underflow, fifo.overflow};
}

template <std::size_t Capacity>
void Device::clear_fifo_status(StereoFifo<Capacity>& fifo) noexcept {
    fifo.underflow = false;
    fifo.overflow = false;
    fifo.underflow_count = 0u;
    fifo.overflow_count = 0u;
}

template <std::size_t Capacity>
bool Device::pop_fifo(StereoFifo<Capacity>& fifo,
                      StereoSample& sample) noexcept {
    if (fifo.count == 0u) {
        fifo.underflow = true;
        fifo.underflow_count += 1u;
        sample = StereoSample{0, 0};
        return false;
    }
    sample = fifo.samples[fifo.head];
    fifo.head = static_cast<std::uint16_t>(
        (static_cast<std::size_t>(fifo.head) + 1u) % Capacity);
    fifo.count = static_cast<std::uint16_t>(fifo.count - 1u);
    return true;
}

StereoSample Device::read_cd_sample(bool needed) noexcept {
    if (!needed) {
        return StereoSample{0, 0};
    }
    StereoSample sample{0, 0};
    if (audio_source_ == AudioSource::Cdda) {
        static_cast<void>(pop_fifo(cdda_fifo_, sample));
    } else {
        static_cast<void>(pop_fifo(xa_fifo_, sample));
    }
    return sample;
}

StereoSample Device::read_external_sample(bool needed) noexcept {
    if (!needed) {
        return StereoSample{0, 0};
    }
    StereoSample sample{0, 0};
    static_cast<void>(pop_fifo(external_fifo_, sample));
    return sample;
}

void Device::capture_sample(std::size_t channel,
                            std::int16_t sample,
                            bool enabled) noexcept {
    if (!enabled) {
        return;
    }
    const std::size_t byte_offset =
        capture_offset(channel, static_cast<std::size_t>(capture_index_));
    write_ram_word(static_cast<std::uint32_t>(byte_offset), bits_of_i16(sample));
}

void Device::capture_voice_samples(std::int16_t voice_one,
                                   std::int16_t voice_three) noexcept {
    const bool enabled = (transfer_control_ & 0x0008u) != 0u;
    if (!enabled) {
        return;
    }
    capture_sample(2u, voice_one, true);
    capture_sample(3u, voice_three, true);
}

bool Device::is_known_even_offset(std::uint32_t offset) const noexcept {
    if (offset < VoiceWindowBytes && (offset & 0x0Fu) <= 0x0Eu) {
        return true;
    }
    if (offset >= ReverbRegisterBase &&
        offset < ReverbRegisterBase + ReverbRegisterBytes) {
        return true;
    }
    if (offset >= InternalVoiceVolumeBase &&
        offset < InternalVoiceVolumeBase + InternalVoiceVolumeBytes) {
        return true;
    }
    switch (offset) {
    case MainVolume:
    case MainVolume + 2u:
    case ReverbOutputVolume:
    case ReverbOutputVolume + 2u:
    case KeyOn:
    case KeyOn + 2u:
    case KeyOff:
    case KeyOff + 2u:
    case PitchModulation:
    case PitchModulation + 2u:
    case NoiseEnable:
    case NoiseEnable + 2u:
    case ReverbEnable:
    case ReverbEnable + 2u:
    case EndFlags:
    case EndFlags + 2u:
    case ReverbBase:
    case IrqAddress:
    case TransferAddress:
    case TransferFifo:
    case Control:
    case TransferControl:
    case Status:
    case CdVolume:
    case CdVolume + 2u:
    case ExternalVolume:
    case ExternalVolume + 2u:
    case CurrentMainVolume:
    case CurrentMainVolume + 2u:
        return true;
    default:
        return false;
    }
}

MmioReadResult Device::read_mmio(std::uint32_t offset,
                                 MmioWidth width) noexcept {
    if (width != MmioWidth::Bits16) {
        return MmioReadResult{MmioFault::WidthNot16, 0u};
    }
    if ((offset & 1u) != 0u) {
        return MmioReadResult{MmioFault::Unaligned, 0u};
    }
    if (!is_known_even_offset(offset)) {
        return MmioReadResult{MmioFault::Unmapped, 0u};
    }
    return read_mmio16(offset);
}

MmioWriteResult Device::write_mmio(std::uint32_t offset,
                                   std::uint32_t value,
                                   MmioWidth width) noexcept {
    if (width != MmioWidth::Bits16) {
        return MmioWriteResult{MmioFault::WidthNot16, 0u};
    }
    if ((offset & 1u) != 0u) {
        return MmioWriteResult{MmioFault::Unaligned, 0u};
    }
    if (value > 0xFFFFu) {
        return MmioWriteResult{MmioFault::ValueOutOfRange, 0u};
    }
    if (!is_known_even_offset(offset)) {
        return MmioWriteResult{MmioFault::Unmapped, 0u};
    }
    return write_mmio16(offset, static_cast<std::uint16_t>(value));
}

MmioReadResult Device::read_mmio16(std::uint32_t offset) noexcept {
    if (offset < VoiceWindowBytes) {
        const std::size_t voice_index = offset / 0x10u;
        const std::uint32_t register_offset = offset & 0x0Fu;
        const Voice& voice = voices_[voice_index];
        switch (register_offset) {
        case 0u:
            return MmioReadResult{MmioFault::None,
                                   volume_register_for(voice.left_volume)};
        case 2u:
            return MmioReadResult{MmioFault::None,
                                   volume_register_for(voice.right_volume)};
        case 4u:
            return MmioReadResult{MmioFault::None, voice.pitch};
        case 6u:
            return MmioReadResult{MmioFault::None, voice.start_address_units};
        case 8u:
            return MmioReadResult{MmioFault::None,
                                   static_cast<std::uint16_t>(voice.adsr_low)};
        case 10u:
            return MmioReadResult{MmioFault::None,
                                   static_cast<std::uint16_t>(voice.adsr_high)};
        case 12u:
            return MmioReadResult{MmioFault::None,
                                   voice.current_adsr_register};
        case 14u:
            return MmioReadResult{MmioFault::None,
                                   voice.repeat_address_units};
        default:
            return MmioReadResult{MmioFault::Unmapped, 0u};
        }
    }
    if (offset >= ReverbRegisterBase &&
        offset < ReverbRegisterBase + ReverbRegisterBytes) {
        return MmioReadResult{MmioFault::None,
                               reverb_registers_[(offset - ReverbRegisterBase) /
                                                 2u]};
    }
    if (offset >= InternalVoiceVolumeBase &&
        offset < InternalVoiceVolumeBase + InternalVoiceVolumeBytes) {
        const std::size_t voice_index =
            (offset - InternalVoiceVolumeBase) / 4u;
        const std::uint32_t register_offset =
            (offset - InternalVoiceVolumeBase) & 3u;
        if (register_offset == 0u) {
            return MmioReadResult{MmioFault::None,
                                  bits_of_i16(voices_[voice_index]
                                                  .left_volume.current)};
        }
        return MmioReadResult{MmioFault::None,
                              bits_of_i16(voices_[voice_index]
                                              .right_volume.current)};
    }
    switch (offset) {
    case MainVolume:
        return MmioReadResult{MmioFault::None,
                              volume_register_for(main_left_)};
    case MainVolume + 2u:
        return MmioReadResult{MmioFault::None,
                              volume_register_for(main_right_)};
    case ReverbOutputVolume:
        return MmioReadResult{MmioFault::None, reverb_output_left_};
    case ReverbOutputVolume + 2u:
        return MmioReadResult{MmioFault::None, reverb_output_right_};
    case KeyOn:
    case KeyOn + 2u:
    case KeyOff:
    case KeyOff + 2u:
        return MmioReadResult{MmioFault::WriteOnly, 0u};
    case PitchModulation:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(pitch_modulation_mask_)};
    case PitchModulation + 2u:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(pitch_modulation_mask_ >>
                                                         16u)};
    case NoiseEnable:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(noise_enable_mask_)};
    case NoiseEnable + 2u:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(noise_enable_mask_ >>
                                                         16u)};
    case ReverbEnable:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(reverb_enable_mask_)};
    case ReverbEnable + 2u:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(reverb_enable_mask_ >>
                                                         16u)};
    case EndFlags:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(end_flags_)};
    case EndFlags + 2u:
        return MmioReadResult{MmioFault::None,
                              static_cast<std::uint16_t>(end_flags_ >> 16u)};
    case ReverbBase:
        return MmioReadResult{MmioFault::None, reverb_base_register_};
    case IrqAddress:
        return MmioReadResult{MmioFault::None, irq_address_register_};
    case TransferAddress:
        return MmioReadResult{MmioFault::None, transfer_address_register_};
    case TransferFifo:
        if (transfer_fifo_count_ == 0u &&
            transfer_mode(control_) != TransferMode::DmaRead) {
            return MmioReadResult{MmioFault::TransferFifoUnderflow, 0u};
        }
        return MmioReadResult{MmioFault::None, read_transfer_word()};
    case Control:
        return MmioReadResult{MmioFault::None, control_};
    case TransferControl:
        return MmioReadResult{MmioFault::None, transfer_control_};
    case Status: {
        std::uint16_t status = static_cast<std::uint16_t>(control_ & 0x003Fu);
        if (transfer_mode(control_) != TransferMode::Stop) {
            status |= StatusTransferRequest;
        }
        if (transfer_fifo_count_ != 0u) {
            status |= StatusTransferBusy;
        }
        if (transfer_mode(control_) == TransferMode::DmaRead) {
            status |= StatusDmaReadRequest;
        } else if (transfer_mode(control_) == TransferMode::DmaWrite) {
            status |= StatusDmaWriteRequest;
        }
        if ((transfer_control_ & 0x000Cu) != 0u && capture_index_ >= 256u) {
            status |= StatusCaptureSecondHalf;
        }
        if (irq_pending_) {
            status |= StatusIrq;
        }
        return MmioReadResult{MmioFault::None, status};
    }
    case CdVolume:
        return MmioReadResult{MmioFault::None, bits_of_i16(cd_volume_left_)};
    case CdVolume + 2u:
        return MmioReadResult{MmioFault::None, bits_of_i16(cd_volume_right_)};
    case ExternalVolume:
        return MmioReadResult{MmioFault::None,
                              bits_of_i16(external_volume_left_)};
    case ExternalVolume + 2u:
        return MmioReadResult{MmioFault::None,
                              bits_of_i16(external_volume_right_)};
    case CurrentMainVolume:
        return MmioReadResult{MmioFault::None,
                              bits_of_i16(main_left_.current)};
    case CurrentMainVolume + 2u:
        return MmioReadResult{MmioFault::None,
                              bits_of_i16(main_right_.current)};
    default:
        return MmioReadResult{MmioFault::Unmapped, 0u};
    }
}

MmioWriteResult Device::write_mmio16(std::uint32_t offset,
                                     std::uint16_t value) noexcept {
    if (offset < VoiceWindowBytes) {
        const std::size_t voice_index = offset / 0x10u;
        const std::uint32_t register_offset = offset & 0x0Fu;
        Voice& voice = voices_[voice_index];
        switch (register_offset) {
        case 0u:
            write_volume(voice.left_volume, value);
            return MmioWriteResult{MmioFault::None, value};
        case 2u:
            write_volume(voice.right_volume, value);
            return MmioWriteResult{MmioFault::None, value};
        case 4u:
            voice.pitch = value;
            return MmioWriteResult{MmioFault::None, value};
        case 6u:
            voice.start_address_units = value;
            return MmioWriteResult{MmioFault::None, value};
        case 8u:
            voice.adsr_low = value;
            return MmioWriteResult{MmioFault::None, value};
        case 10u:
            voice.adsr_high = value;
            return MmioWriteResult{MmioFault::None, value};
        case 12u:
            voice.current_adsr_register = value;
            voice.adsr_volume = to_i16(value);
            return MmioWriteResult{MmioFault::None, value};
        case 14u:
            voice.repeat_address_units = value;
            return MmioWriteResult{MmioFault::None, value};
        default:
            return MmioWriteResult{MmioFault::Unmapped, 0u};
        }
    }
    if (offset >= ReverbRegisterBase &&
        offset < ReverbRegisterBase + ReverbRegisterBytes) {
        reverb_registers_[(offset - ReverbRegisterBase) / 2u] = value;
        return MmioWriteResult{MmioFault::None, value};
    }
    if (offset >= InternalVoiceVolumeBase &&
        offset < InternalVoiceVolumeBase + InternalVoiceVolumeBytes) {
        return MmioWriteResult{MmioFault::ReadOnly, 0u};
    }
    switch (offset) {
    case MainVolume:
        write_volume(main_left_, value);
        return MmioWriteResult{MmioFault::None, value};
    case MainVolume + 2u:
        write_volume(main_right_, value);
        return MmioWriteResult{MmioFault::None, value};
    case ReverbOutputVolume:
        reverb_output_left_ = value;
        return MmioWriteResult{MmioFault::None, value};
    case ReverbOutputVolume + 2u:
        reverb_output_right_ = value;
        return MmioWriteResult{MmioFault::None, value};
    case KeyOn:
        key_on(value);
        return MmioWriteResult{MmioFault::None, value};
    case KeyOn + 2u:
        key_on(static_cast<std::uint32_t>(value & 0x00FFu) << 16u);
        return MmioWriteResult{MmioFault::None,
                               static_cast<std::uint16_t>(value & 0x00FFu)};
    case KeyOff:
        key_off(value);
        return MmioWriteResult{MmioFault::None, value};
    case KeyOff + 2u:
        key_off(static_cast<std::uint32_t>(value & 0x00FFu) << 16u);
        return MmioWriteResult{MmioFault::None,
                               static_cast<std::uint16_t>(value & 0x00FFu)};
    case PitchModulation:
        pitch_modulation_mask_ =
            (pitch_modulation_mask_ & 0xFFFF0000u) | value;
        return MmioWriteResult{MmioFault::None, value};
    case PitchModulation + 2u:
        pitch_modulation_mask_ =
            (pitch_modulation_mask_ & 0x0000FFFFu) |
            (static_cast<std::uint32_t>(value & 0x00FFu) << 16u);
        return MmioWriteResult{MmioFault::None,
                               static_cast<std::uint16_t>(value & 0x00FFu)};
    case NoiseEnable:
        noise_enable_mask_ = (noise_enable_mask_ & 0xFFFF0000u) | value;
        return MmioWriteResult{MmioFault::None, value};
    case NoiseEnable + 2u:
        noise_enable_mask_ =
            (noise_enable_mask_ & 0x0000FFFFu) |
            (static_cast<std::uint32_t>(value & 0x00FFu) << 16u);
        return MmioWriteResult{MmioFault::None,
                               static_cast<std::uint16_t>(value & 0x00FFu)};
    case ReverbEnable:
        reverb_enable_mask_ = (reverb_enable_mask_ & 0xFFFF0000u) | value;
        return MmioWriteResult{MmioFault::None, value};
    case ReverbEnable + 2u:
        reverb_enable_mask_ =
            (reverb_enable_mask_ & 0x0000FFFFu) |
            (static_cast<std::uint32_t>(value & 0x00FFu) << 16u);
        return MmioWriteResult{MmioFault::None,
                               static_cast<std::uint16_t>(value & 0x00FFu)};
    case EndFlags:
    case EndFlags + 2u:
        return MmioWriteResult{MmioFault::ReadOnly, 0u};
    case ReverbBase:
        reverb_base_register_ = value;
        reverb_cursor_byte_ = wrap_reverb(static_cast<std::int64_t>(value) * 8);
        return MmioWriteResult{MmioFault::None, value};
    case IrqAddress:
        irq_address_register_ = value;
        return MmioWriteResult{MmioFault::None, value};
    case TransferAddress:
        transfer_address_register_ = value;
        transfer_current_byte_ = wrap_ram(static_cast<std::uint32_t>(value) * 8u);
        return MmioWriteResult{MmioFault::None, value};
    case TransferFifo:
        if (transfer_fifo_count_ == TransferFifoCapacity) {
            return MmioWriteResult{MmioFault::TransferFifoOverflow, 0u};
        }
        transfer_fifo_[transfer_fifo_tail_] = value;
        transfer_fifo_tail_ = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(transfer_fifo_tail_) + 1u) %
            TransferFifoCapacity);
        transfer_fifo_count_ = static_cast<std::uint8_t>(
            transfer_fifo_count_ + 1u);
        return MmioWriteResult{MmioFault::None, value};
    case Control:
        apply_control(value);
        return MmioWriteResult{MmioFault::None, value};
    case TransferControl:
        apply_transfer_control(value);
        return MmioWriteResult{MmioFault::None,
                               static_cast<std::uint16_t>(value &
                                                          kTransferControlMask)};
    case Status:
    case CurrentMainVolume:
    case CurrentMainVolume + 2u:
        return MmioWriteResult{MmioFault::ReadOnly, 0u};
    case CdVolume:
        cd_volume_left_ = to_i16(value);
        return MmioWriteResult{MmioFault::None, value};
    case CdVolume + 2u:
        cd_volume_right_ = to_i16(value);
        return MmioWriteResult{MmioFault::None, value};
    case ExternalVolume:
        external_volume_left_ = to_i16(value);
        return MmioWriteResult{MmioFault::None, value};
    case ExternalVolume + 2u:
        external_volume_right_ = to_i16(value);
        return MmioWriteResult{MmioFault::None, value};
    default:
        return MmioWriteResult{MmioFault::Unmapped, 0u};
    }
}

VoiceView Device::voice_state(std::size_t voice_index) const noexcept {
    if (voice_index >= VoiceCount) {
        return VoiceView{};
    }
    const Voice& voice = voices_[voice_index];
    return VoiceView{
        true,
        voice.active,
        voice.block_valid,
        voice.phase,
        voice.left_volume.raw,
        voice.right_volume.raw,
        voice.pitch,
        voice.start_address_units,
        voice.repeat_address_units,
        voice.current_address_bytes,
        voice.pitch_counter,
        voice.adsr_volume,
        voice.adsr_low,
        voice.adsr_high,
        voice.block_flags,
        voice.block_sample_index,
    };
}

void Device::set_irq_callback(IrqCallback callback, void* user) noexcept {
    irq_callback_ = callback;
    irq_user_ = user;
}

bool Device::irq_pending() const noexcept {
    return irq_pending_;
}

void Device::acknowledge_irq() noexcept {
    irq_pending_ = false;
}

void Device::set_audio_source(AudioSource source) noexcept {
    audio_source_ = source;
}

AudioSource Device::audio_source() const noexcept {
    return audio_source_;
}

PushResult Device::push_cdda(
    std::span<const StereoSample> samples) noexcept {
    return push_fifo(cdda_fifo_, samples);
}

PushResult Device::push_xa_resampled(
    std::span<const StereoSample> samples) noexcept {
    return push_fifo(xa_fifo_, samples);
}

PushResult Device::push_external(
    std::span<const StereoSample> samples) noexcept {
    return push_fifo(external_fifo_, samples);
}

InputStatus Device::input_status(AudioSource source) const noexcept {
    return source == AudioSource::Cdda ? fifo_status(cdda_fifo_)
                                       : fifo_status(xa_fifo_);
}

InputStatus Device::external_input_status() const noexcept {
    return fifo_status(external_fifo_);
}

void Device::clear_input_status() noexcept {
    clear_fifo_status(cdda_fifo_);
    clear_fifo_status(xa_fifo_);
    clear_fifo_status(external_fifo_);
}

std::span<const std::uint8_t> Device::sound_ram() const noexcept {
    return std::span<const std::uint8_t>(sound_ram_.data(), sound_ram_.size());
}

RenderResult Device::render(std::span<StereoSample> output) noexcept {
    if (output.size() > MaxRenderSamplesPerCall) {
        return RenderResult{RenderFault::TooManySamples, 0u};
    }
    const bool voice_enabled = (control_ & ControlEnable) != 0u &&
                               (control_ & ControlMute) != 0u;
    const bool capture_cd = (transfer_control_ & 0x0004u) != 0u;
    const bool capture_voice = (transfer_control_ & 0x0008u) != 0u;
    const bool cd_needed = (control_ & ControlCdEnable) != 0u ||
                           (control_ & ControlCdReverb) != 0u || capture_cd;
    const bool external_needed =
        (control_ & ControlExternalEnable) != 0u ||
        (control_ & ControlExternalReverb) != 0u;

    for (StereoSample& destination : output) {
        const StereoSample cd_sample = read_cd_sample(cd_needed);
        const StereoSample external_sample =
            read_external_sample(external_needed);
        const bool has_noise = noise_enable_mask_ != 0u;
        const std::int16_t noise_sample = has_noise ? next_noise() : 0;
        std::int16_t previous_voice_output = 0;
        std::int16_t capture_voice_one = 0;
        std::int16_t capture_voice_three = 0;
        std::int64_t dry_left = 0;
        std::int64_t dry_right = 0;
        std::int64_t reverb_input_left = 0;
        std::int64_t reverb_input_right = 0;

        for (std::size_t index = 0u; index < VoiceCount; ++index) {
            Voice& voice = voices_[index];
            const std::int16_t prior_output = previous_voice_output;
            std::int16_t source = 0;
            const bool noise = (noise_enable_mask_ &
                                static_cast<std::uint32_t>(1u << index)) != 0u;
            if (voice.active) {
                if (noise) {
                    source = noise_sample;
                } else if (voice.pitch != 0u) {
                    ensure_block(voice);
                    source = interpolate(voice);
                }
            }
            const std::int16_t after_adsr = static_cast<std::int16_t>(
                multiply_q15(static_cast<std::int32_t>(source),
                             static_cast<std::int32_t>(voice.adsr_volume)));
            previous_voice_output = after_adsr;
            if (index == 1u) {
                capture_voice_one = after_adsr;
            } else if (index == 3u) {
                capture_voice_three = after_adsr;
            }

            const std::int32_t channel_left = multiply_q15(
                static_cast<std::int32_t>(after_adsr),
                static_cast<std::int32_t>(voice.left_volume.current));
            const std::int32_t channel_right = multiply_q15(
                static_cast<std::int32_t>(after_adsr),
                static_cast<std::int32_t>(voice.right_volume.current));
            if (voice_enabled) {
                dry_left += channel_left;
                dry_right += channel_right;
                if ((reverb_enable_mask_ &
                     static_cast<std::uint32_t>(1u << index)) != 0u) {
                    reverb_input_left += channel_left;
                    reverb_input_right += channel_right;
                }
            }

            advance_adsr(voice);
            advance_volume(voice.left_volume);
            advance_volume(voice.right_volume);
            if (voice.active && voice.pitch != 0u) {
                const std::uint32_t step =
                    pitch_step(voice, prior_output, index);
                voice.pitch_counter += step;
                advance_block(voice);
            }
        }

        const bool cd_enabled = (control_ & ControlCdEnable) != 0u;
        const bool external_enabled =
            (control_ & ControlExternalEnable) != 0u;
        if (cd_enabled) {
            const std::int32_t cd_left = multiply_q15(
                cd_sample.left, cd_volume_left_);
            const std::int32_t cd_right = multiply_q15(
                cd_sample.right, cd_volume_right_);
            dry_left += cd_left;
            dry_right += cd_right;
            if ((control_ & ControlCdReverb) != 0u) {
                reverb_input_left += cd_left;
                reverb_input_right += cd_right;
            }
        }
        if (external_enabled) {
            const std::int32_t external_left = multiply_q15(
                external_sample.left, external_volume_left_);
            const std::int32_t external_right = multiply_q15(
                external_sample.right, external_volume_right_);
            dry_left += external_left;
            dry_right += external_right;
            if ((control_ & ControlExternalReverb) != 0u) {
                reverb_input_left += external_left;
                reverb_input_right += external_right;
            }
        }

        if (capture_cd) {
            capture_sample(0u, cd_sample.left, true);
            capture_sample(1u, cd_sample.right, true);
        }
        if (capture_voice) {
            capture_voice_samples(capture_voice_one, capture_voice_three);
        }
        if (capture_cd || capture_voice) {
            capture_index_ = static_cast<std::uint16_t>(
                (static_cast<std::uint32_t>(capture_index_) + 1u) %
                CaptureSamplesPerHalf);
        }

        const std::int16_t reverb_input_l = saturate_i16(reverb_input_left);
        const std::int16_t reverb_input_r = saturate_i16(reverb_input_right);
        if ((sample_counter_ & 1u) == 0u) {
            const StereoSample wet = process_reverb(
                reverb_input_l, reverb_input_r,
                (control_ & ControlReverbEnable) != 0u);
            reverb_hold_left_ = wet.left;
            reverb_hold_right_ = wet.right;
        }
        const std::int64_t main_left = multiply_q15_wide(
            dry_left, main_left_.current);
        const std::int64_t main_right = multiply_q15_wide(
            dry_right, main_right_.current);
        destination.left = saturate_i16(
            main_left + static_cast<std::int64_t>(reverb_hold_left_));
        destination.right = saturate_i16(
            main_right + static_cast<std::int64_t>(reverb_hold_right_));

        advance_volume(main_left_);
        advance_volume(main_right_);
        sample_counter_ += 1u;
    }
    return RenderResult{RenderFault::None, output.size()};
}

template <typename Sink>
void Device::serialize_payload(Sink& sink) const noexcept {
    for (const std::uint8_t byte : sound_ram_) {
        sink.put_u8(byte);
    }
    const auto put_volume = [&sink](const VolumeControl& volume) noexcept {
        sink.put_u16(volume.raw);
        sink.put_u16(bits_of_i16(volume.current));
        sink.put_u16(volume.counter);
    };
    for (const Voice& voice : voices_) {
        put_volume(voice.left_volume);
        put_volume(voice.right_volume);
        sink.put_u16(voice.pitch);
        sink.put_u16(voice.start_address_units);
        sink.put_u16(static_cast<std::uint16_t>(voice.adsr_low));
        sink.put_u16(static_cast<std::uint16_t>(voice.adsr_high));
        sink.put_u16(voice.current_adsr_register);
        sink.put_u16(voice.repeat_address_units);
        sink.put_u32(voice.current_address_bytes);
        sink.put_u32(voice.pitch_counter);
        sink.put_u32(voice.block_address_bytes);
        sink.put_u8(voice.block_flags);
        sink.put_u8(voice.block_sample_index);
        sink.put_u8(static_cast<std::uint8_t>(voice.phase));
        sink.put_u8(voice.block_valid ? 1u : 0u);
        sink.put_u8(voice.active ? 1u : 0u);
        sink.put_u16(bits_of_i16(voice.decoder_previous));
        sink.put_u16(bits_of_i16(voice.decoder_previous_previous));
        for (const std::int16_t sample : voice.history) {
            sink.put_u16(bits_of_i16(sample));
        }
        for (const std::int16_t sample : voice.block_samples) {
            sink.put_u16(bits_of_i16(sample));
        }
        sink.put_u16(bits_of_i16(voice.adsr_volume));
        sink.put_u16(voice.adsr_counter);
    }
    put_volume(main_left_);
    put_volume(main_right_);
    sink.put_u16(reverb_output_left_);
    sink.put_u16(reverb_output_right_);
    sink.put_u16(bits_of_i16(cd_volume_left_));
    sink.put_u16(bits_of_i16(cd_volume_right_));
    sink.put_u16(bits_of_i16(external_volume_left_));
    sink.put_u16(bits_of_i16(external_volume_right_));
    sink.put_u16(control_);
    sink.put_u16(transfer_control_);
    sink.put_u16(transfer_address_register_);
    sink.put_u16(irq_address_register_);
    sink.put_u16(reverb_base_register_);
    sink.put_u32(transfer_current_byte_);
    sink.put_u32(reverb_cursor_byte_);
    sink.put_u64(sample_counter_);
    sink.put_u32(pitch_modulation_mask_);
    sink.put_u32(noise_enable_mask_);
    sink.put_u32(reverb_enable_mask_);
    sink.put_u32(end_flags_);
    sink.put_u8(transfer_fifo_head_);
    sink.put_u8(transfer_fifo_tail_);
    sink.put_u8(transfer_fifo_count_);
    for (const std::uint16_t word : transfer_fifo_) {
        sink.put_u16(word);
    }
    sink.put_u16(capture_index_);
    sink.put_u16(bits_of_i16(noise_level_));
    sink.put_u32(static_cast<std::uint32_t>(noise_timer_));
    sink.put_u8(irq_pending_ ? 1u : 0u);
    sink.put_u8(static_cast<std::uint8_t>(audio_source_));
    sink.put_u16(bits_of_i16(reverb_hold_left_));
    sink.put_u16(bits_of_i16(reverb_hold_right_));

    const auto put_fifo = [&sink](const StereoFifo<AudioFifoCapacity>& fifo)
        noexcept {
        sink.put_u16(fifo.head);
        sink.put_u16(fifo.tail);
        sink.put_u16(fifo.count);
        sink.put_u8(fifo.underflow ? 1u : 0u);
        sink.put_u8(fifo.overflow ? 1u : 0u);
        sink.put_u64(fifo.underflow_count);
        sink.put_u64(fifo.overflow_count);
        for (const StereoSample sample : fifo.samples) {
            sink.put_u16(bits_of_i16(sample.left));
            sink.put_u16(bits_of_i16(sample.right));
        }
    };
    put_fifo(cdda_fifo_);
    put_fifo(xa_fifo_);
    put_fifo(external_fifo_);
    for (const std::uint16_t value : reverb_registers_) {
        sink.put_u16(value);
    }
}

std::uint64_t Device::deterministic_digest() const noexcept {
    DigestSink sink{};
    serialize_payload(sink);
    return sink.digest;
}

SnapshotResult Device::save_snapshot(
    std::span<std::byte> destination) const noexcept {
    if (destination.size() < SnapshotBytes) {
        return SnapshotResult{SnapshotFault::BufferTooSmall, 0u, 0u};
    }
    for (std::size_t index = 0u; index < 8u; ++index) {
        destination[index] = to_byte(kSnapshotMagic[index]);
    }
    const auto put_raw_u32 = [&destination](std::size_t offset,
                                             std::uint32_t value) noexcept {
        destination[offset] = to_byte(static_cast<std::uint8_t>(value));
        destination[offset + 1u] =
            to_byte(static_cast<std::uint8_t>(value >> 8u));
        destination[offset + 2u] =
            to_byte(static_cast<std::uint8_t>(value >> 16u));
        destination[offset + 3u] =
            to_byte(static_cast<std::uint8_t>(value >> 24u));
    };
    const auto put_raw_u64 = [&destination](std::size_t offset,
                                             std::uint64_t value) noexcept {
        for (std::size_t index = 0u; index < 8u; ++index) {
            destination[offset + index] = to_byte(static_cast<std::uint8_t>(
                value >> (index * 8u)));
        }
    };
    put_raw_u32(8u, SnapshotVersion);
    put_raw_u32(12u, static_cast<std::uint32_t>(SnapshotPayloadBytes));
    put_raw_u64(16u, 0u);

    SnapshotWriter writer{destination.subspan(SnapshotHeaderBytes,
                                              SnapshotPayloadBytes)};
    serialize_payload(writer);
    if (!writer.good || writer.position != SnapshotPayloadBytes) {
        return SnapshotResult{SnapshotFault::InvalidPayload, 0u, 0u};
    }
    put_raw_u64(SnapshotHeaderBytes + SnapshotPayloadBytes, writer.digest);
    return SnapshotResult{SnapshotFault::None, SnapshotBytes, writer.digest};
}

namespace {

std::uint32_t raw_u32(std::span<const std::byte> source,
                      std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(to_u8(source[offset])) |
           (static_cast<std::uint32_t>(to_u8(source[offset + 1u])) << 8u) |
           (static_cast<std::uint32_t>(to_u8(source[offset + 2u])) << 16u) |
           (static_cast<std::uint32_t>(to_u8(source[offset + 3u])) << 24u);
}

std::uint64_t raw_u64(std::span<const std::byte> source,
                      std::size_t offset) noexcept {
    std::uint64_t value = 0u;
    for (std::size_t index = 0u; index < 8u; ++index) {
        value |= static_cast<std::uint64_t>(to_u8(source[offset + index])) <<
                 (index * 8u);
    }
    return value;
}

} // namespace

SnapshotResult Device::restore_snapshot(
    std::span<const std::byte> source) noexcept {
    if (source.size() < SnapshotBytes) {
        return SnapshotResult{SnapshotFault::BufferTooSmall, 0u, 0u};
    }
    if (source.size() != SnapshotBytes) {
        return SnapshotResult{SnapshotFault::InvalidHeader, 0u, 0u};
    }
    for (std::size_t index = 0u; index < 8u; ++index) {
        if (to_u8(source[index]) != kSnapshotMagic[index]) {
            return SnapshotResult{SnapshotFault::InvalidHeader, 0u, 0u};
        }
    }
    if (raw_u32(source, 8u) != SnapshotVersion ||
        raw_u32(source, 12u) != SnapshotPayloadBytes ||
        raw_u64(source, 16u) != 0u) {
        return SnapshotResult{SnapshotFault::InvalidHeader, 0u, 0u};
    }
    const std::span<const std::byte> payload =
        source.subspan(SnapshotHeaderBytes, SnapshotPayloadBytes);
    SnapshotReader digest_reader{payload};
    digest_reader.skip(SnapshotPayloadBytes);
    const std::uint64_t stored_digest =
        raw_u64(source, SnapshotHeaderBytes + SnapshotPayloadBytes);
    if (!digest_reader.good || digest_reader.digest != stored_digest) {
        return SnapshotResult{SnapshotFault::DigestMismatch, 0u,
                               digest_reader.digest};
    }
    SnapshotReader validator{payload};
    if (!validate_payload(validator) || !validator.good ||
        validator.position != SnapshotPayloadBytes ||
        validator.digest != stored_digest) {
        return SnapshotResult{SnapshotFault::InvalidPayload, 0u,
                               validator.digest};
    }
    SnapshotReader reader{payload};
    if (!deserialize_payload(reader) || !reader.good ||
        reader.position != SnapshotPayloadBytes) {
        return SnapshotResult{SnapshotFault::InvalidPayload, 0u,
                               reader.digest};
    }
    return SnapshotResult{SnapshotFault::None, SnapshotBytes, stored_digest};
}

bool Device::validate_payload(SnapshotReader& reader) const noexcept {
    reader.skip(SoundRamBytes);
    const auto validate_volume = [&reader]() noexcept {
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
    };
    for (std::size_t index = 0u; index < VoiceCount; ++index) {
        validate_volume();
        validate_volume();
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        const std::uint32_t current_address = reader.get_u32();
        const std::uint32_t pitch_counter = reader.get_u32();
        const std::uint32_t block_address = reader.get_u32();
        static_cast<void>(reader.get_u8());
        const std::uint8_t block_sample_index = reader.get_u8();
        const std::uint8_t phase = reader.get_u8();
        const std::uint8_t block_valid = reader.get_u8();
        const std::uint8_t active = reader.get_u8();
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        reader.skip(3u * sizeof(std::uint16_t));
        reader.skip(AdpcmSamplesPerBlock * sizeof(std::uint16_t));
        static_cast<void>(reader.get_u16());
        static_cast<void>(reader.get_u16());
        if (current_address > RamMask || block_address > RamMask ||
            pitch_counter >= kBlockSpanFixed || block_sample_index > 28u ||
            phase > static_cast<std::uint8_t>(AdsrPhase::Release) ||
            block_valid > 1u || active > 1u) {
            return false;
        }
    }
    validate_volume();
    validate_volume();
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    const std::uint16_t transfer_control = reader.get_u16();
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    const std::uint16_t reverb_base = reader.get_u16();
    const std::uint32_t transfer_current = reader.get_u32();
    const std::uint32_t reverb_cursor = reader.get_u32();
    static_cast<void>(reader.get_u64());
    const std::uint32_t pmon = reader.get_u32();
    const std::uint32_t non = reader.get_u32();
    const std::uint32_t eon = reader.get_u32();
    const std::uint32_t endx = reader.get_u32();
    const std::uint8_t transfer_head = reader.get_u8();
    const std::uint8_t transfer_tail = reader.get_u8();
    const std::uint8_t transfer_count = reader.get_u8();
    reader.skip(TransferFifoCapacity * sizeof(std::uint16_t));
    const std::uint16_t capture_index = reader.get_u16();
    static_cast<void>(reader.get_u16());
    const std::int32_t noise_timer = signed32_from_bits(reader.get_u32());
    const std::uint8_t irq = reader.get_u8();
    const std::uint8_t source = reader.get_u8();
    static_cast<void>(reader.get_u16());
    static_cast<void>(reader.get_u16());
    const auto validate_fifo = [&reader]() noexcept {
        const std::uint16_t head = reader.get_u16();
        const std::uint16_t tail = reader.get_u16();
        const std::uint16_t count = reader.get_u16();
        const std::uint8_t underflow = reader.get_u8();
        const std::uint8_t overflow = reader.get_u8();
        static_cast<void>(reader.get_u64());
        static_cast<void>(reader.get_u64());
        reader.skip(AudioFifoCapacity * sizeof(StereoSample));
        return head < AudioFifoCapacity && tail < AudioFifoCapacity &&
               count <= AudioFifoCapacity && underflow <= 1u && overflow <= 1u;
    };
    const bool cdda_valid = validate_fifo();
    const bool xa_valid = validate_fifo();
    const bool external_valid = validate_fifo();
    reader.skip(32u * sizeof(std::uint16_t));
    const std::uint32_t reverb_base_bytes =
        static_cast<std::uint32_t>(reverb_base) * 8u;
    return reader.good && cdda_valid && xa_valid && external_valid &&
           (transfer_control & ~kTransferControlMask) == 0u &&
           transfer_head < TransferFifoCapacity &&
           transfer_tail < TransferFifoCapacity && transfer_count <= 32u &&
           capture_index < CaptureSamplesPerHalf && irq <= 1u && source <= 1u &&
           noise_timer >= 0 && noise_timer <= 0x1FFFF &&
           transfer_current <= RamMask && reverb_cursor <= ReverbEndByte &&
           reverb_base_bytes <= RamMask && (pmon & 0xFF000000u) == 0u &&
           (non & 0xFF000000u) == 0u && (eon & 0xFF000000u) == 0u &&
           (endx & 0xFF000000u) == 0u && reader.position == SnapshotPayloadBytes;
}

bool Device::deserialize_payload(SnapshotReader& reader) noexcept {
    for (std::uint8_t& byte : sound_ram_) {
        byte = reader.get_u8();
    }
    const auto get_volume = [&reader](VolumeControl& volume) noexcept {
        volume.raw = reader.get_u16();
        volume.current = Device::to_i16(reader.get_u16());
        volume.counter = reader.get_u16();
    };
    for (Voice& voice : voices_) {
        get_volume(voice.left_volume);
        get_volume(voice.right_volume);
        voice.pitch = reader.get_u16();
        voice.start_address_units = reader.get_u16();
        voice.adsr_low = reader.get_u16();
        voice.adsr_high = reader.get_u16();
        voice.current_adsr_register = reader.get_u16();
        voice.repeat_address_units = reader.get_u16();
        voice.current_address_bytes = reader.get_u32();
        voice.pitch_counter = reader.get_u32();
        voice.block_address_bytes = reader.get_u32();
        voice.block_flags = reader.get_u8();
        voice.block_sample_index = reader.get_u8();
        voice.phase = static_cast<AdsrPhase>(reader.get_u8());
        voice.block_valid = reader.get_u8() != 0u;
        voice.active = reader.get_u8() != 0u;
        voice.decoder_previous = Device::to_i16(reader.get_u16());
        voice.decoder_previous_previous = Device::to_i16(reader.get_u16());
        for (std::int16_t& sample : voice.history) {
            sample = Device::to_i16(reader.get_u16());
        }
        for (std::int16_t& sample : voice.block_samples) {
            sample = Device::to_i16(reader.get_u16());
        }
        voice.adsr_volume = Device::to_i16(reader.get_u16());
        voice.adsr_counter = reader.get_u16();
    }
    get_volume(main_left_);
    get_volume(main_right_);
    reverb_output_left_ = reader.get_u16();
    reverb_output_right_ = reader.get_u16();
    cd_volume_left_ = to_i16(reader.get_u16());
    cd_volume_right_ = to_i16(reader.get_u16());
    external_volume_left_ = to_i16(reader.get_u16());
    external_volume_right_ = to_i16(reader.get_u16());
    control_ = reader.get_u16();
    transfer_control_ = reader.get_u16();
    transfer_address_register_ = reader.get_u16();
    irq_address_register_ = reader.get_u16();
    reverb_base_register_ = reader.get_u16();
    transfer_current_byte_ = reader.get_u32();
    reverb_cursor_byte_ = reader.get_u32();
    sample_counter_ = reader.get_u64();
    pitch_modulation_mask_ = reader.get_u32();
    noise_enable_mask_ = reader.get_u32();
    reverb_enable_mask_ = reader.get_u32();
    end_flags_ = reader.get_u32();
    transfer_fifo_head_ = reader.get_u8();
    transfer_fifo_tail_ = reader.get_u8();
    transfer_fifo_count_ = reader.get_u8();
    for (std::uint16_t& word : transfer_fifo_) {
        word = reader.get_u16();
    }
    capture_index_ = reader.get_u16();
    noise_level_ = to_i16(reader.get_u16());
    noise_timer_ = signed32_from_bits(reader.get_u32());
    irq_pending_ = reader.get_u8() != 0u;
    audio_source_ = static_cast<AudioSource>(reader.get_u8());
    reverb_hold_left_ = to_i16(reader.get_u16());
    reverb_hold_right_ = to_i16(reader.get_u16());
    const auto get_fifo = [&reader](StereoFifo<AudioFifoCapacity>& fifo) noexcept {
        fifo.head = reader.get_u16();
        fifo.tail = reader.get_u16();
        fifo.count = reader.get_u16();
        fifo.underflow = reader.get_u8() != 0u;
        fifo.overflow = reader.get_u8() != 0u;
        fifo.underflow_count = reader.get_u64();
        fifo.overflow_count = reader.get_u64();
        for (StereoSample& sample : fifo.samples) {
            sample.left = Device::to_i16(reader.get_u16());
            sample.right = Device::to_i16(reader.get_u16());
        }
    };
    get_fifo(cdda_fifo_);
    get_fifo(xa_fifo_);
    get_fifo(external_fifo_);
    for (std::uint16_t& value : reverb_registers_) {
        value = reader.get_u16();
    }
    return reader.good;
}

} // namespace clean_aot::spu
