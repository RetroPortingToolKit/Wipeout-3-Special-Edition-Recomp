#include "clean_aot/spu/spu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>

namespace {

using clean_aot::spu::AdpcmSamplesPerBlock;
using clean_aot::spu::AudioFifoCapacity;
using clean_aot::spu::AudioSource;
using clean_aot::spu::Device;
using clean_aot::spu::DmaFault;
using clean_aot::spu::MmioFault;
using clean_aot::spu::MmioWidth;
using clean_aot::spu::SamplesPer100HzUpdate;
using clean_aot::spu::SnapshotFault;
using clean_aot::spu::StereoSample;

constexpr MmioWidth kWord = MmioWidth::Bits16;
constexpr std::uint16_t kLive = Device::ControlEnable | Device::ControlMute;
constexpr std::uint16_t kNormalTransfer = 0x0004u;
constexpr std::uint16_t kDmaWrite = 0x0020u;
constexpr std::uint16_t kDmaRead = 0x0030u;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void put(Device& device, std::uint32_t offset, std::uint16_t value) {
    const auto result = device.write_mmio(offset, value, kWord);
    require(result.ok(), "legal 16-bit register write");
}

void render_count(Device& device, std::size_t sample_count,
                  const char* message) {
    static std::array<StereoSample,
                      clean_aot::spu::MaxRenderSamplesPerCall> scratch{};
    while (sample_count != 0u) {
        const std::size_t count = std::min(sample_count, scratch.size());
        require(device.render(std::span<StereoSample>(scratch).first(count)).ok(),
                message);
        sample_count -= count;
    }
}

std::uint64_t audio_digest(std::span<const StereoSample> samples) noexcept {
    constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
    constexpr std::uint64_t fnv_prime = 1099511628211ull;
    std::uint64_t digest = fnv_offset;
    for (const StereoSample sample : samples) {
        for (const std::int16_t channel : {sample.left, sample.right}) {
            const std::uint16_t bits = static_cast<std::uint16_t>(channel);
            digest ^= static_cast<std::uint8_t>(bits & 0x00FFu);
            digest *= fnv_prime;
            digest ^= static_cast<std::uint8_t>(bits >> 8u);
            digest *= fnv_prime;
        }
    }
    return digest;
}

std::uint16_t ram_word(const Device& device, std::size_t byte_address) {
    const auto ram = device.sound_ram();
    require(byte_address + 1u < ram.size(), "sound RAM word is in range");
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(ram[byte_address]) |
        (static_cast<std::uint16_t>(ram[byte_address + 1u]) << 8u));
}

std::uint8_t snapshot_byte(std::byte value) {
    return std::to_integer<std::uint8_t>(value);
}

void set_snapshot_u16(std::span<std::byte> snapshot,
                      std::size_t offset,
                      std::uint16_t value) {
    require(offset + 1u < snapshot.size(), "snapshot halfword is in range");
    snapshot[offset] = static_cast<std::byte>(value & 0x00FFu);
    snapshot[offset + 1u] = static_cast<std::byte>(value >> 8u);
}

void reseal_snapshot(std::span<std::byte> snapshot) {
    constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
    constexpr std::uint64_t fnv_prime = 1099511628211ull;
    require(snapshot.size() == Device::SnapshotBytes,
            "snapshot reseal uses canonical size");
    std::uint64_t digest = fnv_offset;
    for (std::size_t index = 0u; index < Device::SnapshotPayloadBytes;
         ++index) {
        digest ^= snapshot_byte(
            snapshot[Device::SnapshotHeaderBytes + index]);
        digest *= fnv_prime;
    }
    const std::size_t digest_offset =
        Device::SnapshotHeaderBytes + Device::SnapshotPayloadBytes;
    for (std::size_t index = 0u; index < sizeof(digest); ++index) {
        snapshot[digest_offset + index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(digest >> (index * 8u)));
    }
}

std::array<std::uint8_t, 16u> make_block(
    std::uint8_t header,
    std::uint8_t flags,
    const std::array<std::int8_t, AdpcmSamplesPerBlock>& samples) {
    std::array<std::uint8_t, 16u> block{};
    block[0] = header;
    block[1] = flags;
    for (std::size_t index = 0u; index < samples.size(); ++index) {
        const std::int32_t signed_sample =
            static_cast<std::int32_t>(samples[index]);
        const std::uint8_t nibble = static_cast<std::uint8_t>(
            signed_sample < 0 ? signed_sample + 16 : signed_sample);
        const std::size_t byte_index = 2u + (index / 2u);
        if ((index & 1u) == 0u) {
            block[byte_index] = nibble;
        } else {
            block[byte_index] = static_cast<std::uint8_t>(
                block[byte_index] | static_cast<std::uint8_t>(nibble << 4u));
        }
    }
    return block;
}

std::array<std::uint8_t, 16u> make_constant_block(std::int8_t nibble,
                                                   std::uint8_t flags = 0u) {
    std::array<std::int8_t, AdpcmSamplesPerBlock> samples{};
    samples.fill(nibble);
    return make_block(0u, flags, samples);
}

void load_block(Device& device,
                const std::array<std::uint8_t, 16u>& block,
                std::uint16_t address_units = 0u) {
    std::array<std::uint16_t, 8u> words{};
    for (std::size_t index = 0u; index < words.size(); ++index) {
        words[index] = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(block[index * 2u]) |
            (static_cast<std::uint16_t>(block[index * 2u + 1u]) << 8u));
    }
    put(device, Device::TransferControl, kNormalTransfer);
    put(device, Device::TransferAddress, address_units);
    put(device, Device::Control,
        static_cast<std::uint16_t>(kLive | kDmaWrite |
                                   Device::ControlNoiseShiftMask));
    render_count(device, 1u, "apply delayed SPU DMA-write mode");
    const auto result = device.dma_write(words);
    require(result.ok() && result.transferred_halfwords == words.size(),
            "load synthetic ADPCM block through DMA");
    put(device, Device::Control, kLive);
}

void configure_voice(Device& device,
                     std::size_t voice_index,
                     std::uint16_t pitch = 0x1000u,
                     std::uint16_t adsr_low = 0x000Fu,
                     std::uint16_t adsr_high = 0u) {
    const std::uint32_t base = static_cast<std::uint32_t>(voice_index * 0x10u);
    put(device, base, 0x3FFFu);
    put(device, base + 2u, 0x3FFFu);
    put(device, base + 4u, pitch);
    put(device, base + 6u, 0u);
    put(device, base + 8u, adsr_low);
    put(device, base + 10u, adsr_high);
    put(device, base + 14u, 0u);
}

void prepare_device(Device& device,
                    const std::array<std::uint8_t, 16u>& block) {
    load_block(device, block);
    put(device, Device::MainVolume, 0x3FFFu);
    put(device, Device::MainVolume + 2u, 0x3FFFu);
    configure_voice(device, 0u, 0x1000u, 0x7FFFu);
    put(device, Device::KeyOn, 1u);
    put(device, 12u, 0x7FFFu);
}

void test_adpcm_vectors_and_loop_edges() {
    constexpr std::array<std::int8_t, AdpcmSamplesPerBlock> nibbles = {
        7, -8, 6, -7, 5, -6, 4, -5,
        3, -4, 2, -3, 1, -2, 7, -8,
        6, -7, 5, -6, 4, -5, 3, -4,
        2, -3, 1, -2,
    };
    const auto block = make_block(0x10u, 0u, nibbles);
    static Device device;
    device.reset();
    prepare_device(device, block);
    std::array<StereoSample, 16u> output{};
    require(device.render(output).ok(), "render ADPCM vector");
    constexpr std::array<StereoSample, 16u> expected = {
        StereoSample{-1, -1},       StereoSample{4259, 4259},
        StereoSample{19210, 19210}, StereoSample{2908, 2908},
        StereoSample{10878, 10878}, StereoSample{-3241, -3241},
        StereoSample{3449, 3449},   StereoSample{-8545, -8545},
        StereoSample{-3179, -3179}, StereoSample{-13102, -13102},
        StereoSample{-9111, -9111}, StereoSample{-17001, -17001},
        StereoSample{-14431, -14431}, StereoSample{-20327, -20327},
        StereoSample{-19207, -19207}, StereoSample{-18883, -18883},
    };
    for (std::size_t index = 0u; index < output.size(); ++index) {
        if (output[index] != expected[index]) {
            std::cerr << "ADPCM vector index " << index << ": got ("
                      << output[index].left << ", " << output[index].right
                      << "), expected (" << expected[index].left << ", "
                      << expected[index].right << ")\n";
        }
        require(output[index].left == expected[index].left &&
                    output[index].right == expected[index].right,
                "independent ADPCM/interpolation vector");
    }

    static Device looped;
    looped.reset();
    prepare_device(looped, make_block(0x10u, 0x07u, nibbles));
    std::array<StereoSample, 29u> loop_output{};
    require(looped.render(loop_output).ok(), "render loop-end block");
    const auto endx = looped.read_mmio(Device::EndFlags, kWord);
    require(endx.ok() && (endx.value & 1u) != 0u,
            "loop-end sets ENDX");
    require(looped.voice_state(0u).active, "end-repeat keeps voice active");

    static Device muted;
    muted.reset();
    prepare_device(muted, make_block(0x10u, 0x01u, nibbles));
    std::array<StereoSample, 29u> muted_output{};
    require(muted.render(muted_output).ok(), "render end-mute block");
    require(muted.voice_state(0u).active &&
                muted.voice_state(0u).adsr_volume == 0,
            "end-mute releases and silences voice while scanning");

    static Device loop_start;
    loop_start.reset();
    prepare_device(loop_start, make_block(0x10u, 0x04u, nibbles));
    std::array<StereoSample, 1u> one{};
    require(loop_start.render(one).ok(), "render loop-start block");
    require(loop_start.voice_state(0u).repeat_address_units == 0u,
            "loop-start copies current address to repeat register");
}

void test_gaussian_fractional_phases() {
    std::array<std::int8_t, AdpcmSamplesPerBlock> impulse{};
    impulse[0u] = 7;
    const auto block = make_block(0u, 0u, impulse);
    struct Anchor final {
        std::uint16_t phase;
        std::int16_t expected;
    };
    constexpr std::array<Anchor, 9u> anchors = {{
        {0u, -1}, {15u, -1}, {16u, 0}, {23u, 0}, {64u, 31},
        {127u, 357}, {128u, 368}, {192u, 1562}, {255u, 4203},
    }};
    static Device device;
    for (const Anchor anchor : anchors) {
        device.reset();
        load_block(device, block);
        put(device, Device::MainVolume, 0x3FFFu);
        put(device, Device::MainVolume + 2u, 0x3FFFu);
        configure_voice(device, 0u,
                        anchor.phase == 0u
                            ? 1u
                            : static_cast<std::uint16_t>(anchor.phase << 4u),
                        0x7FFFu);
        put(device, Device::KeyOn, 1u);
        put(device, 12u, 0x7FFFu);
        std::array<StereoSample, 2u> output{};
        require(device.render(output).ok(), "render Gaussian phase anchor");
        const StereoSample observed = output[anchor.phase == 0u ? 0u : 1u];
        if (observed.left != anchor.expected ||
            observed.right != anchor.expected) {
            std::cerr << "Gaussian phase " << anchor.phase << ": got "
                      << observed.left << ", expected " << anchor.expected
                      << '\n';
        }
        require(observed.left == anchor.expected &&
                    observed.right == anchor.expected,
                "exact Gaussian table/index/rounding anchor");
    }
}

struct IrqCounter final {
    std::uint32_t calls{0u};

    static void signal(void* user) noexcept {
        auto* counter = static_cast<IrqCounter*>(user);
        counter->calls += 1u;
    }
};

void test_irq_and_mmio_boundaries() {
    static Device device;
    device.reset();
    const auto wrong_width = device.read_mmio(Device::Control, MmioWidth::Bits32);
    require(wrong_width.fault == MmioFault::WidthNot16,
            "reject 32-bit MMIO access");
    require(device.read_mmio(Device::Control + 1u, kWord).fault ==
                MmioFault::Unaligned,
            "reject unaligned MMIO access");
    require(device.read_mmio(0x0400u, kWord).fault == MmioFault::Unmapped,
            "reject unmapped MMIO access");
    require(device.write_mmio(Device::Control, 0x1'0000u, kWord).fault ==
                MmioFault::ValueOutOfRange,
            "reject value wider than 16 bits");
    require(device.read_mmio(Device::KeyOn, kWord).fault == MmioFault::WriteOnly,
            "key-on is write-only");
    require(device.write_mmio(Device::EndFlags, 1u, kWord).fault ==
                MmioFault::ReadOnly,
            "ENDX is read-only");
    const auto unknown_status = device.read_mmio(Device::UnknownStatus, kWord);
    require(unknown_status.ok() && unknown_status.value == 0u &&
                device.write_mmio(Device::UnknownStatus, 1u, kWord).fault ==
                    MmioFault::ReadOnly,
            "undefined read-only status has deterministic compatibility value");
    put(device, Device::UnknownControlBase, 0xA55Au);
    put(device, Device::UnknownControlBase + 2u, 0x5AA5u);
    put(device, Device::UnknownInternalBase, 0x1234u);
    put(device, Device::UnknownInternalBase +
                    Device::UnknownInternalBytes - 2u,
        0xFEDCu);
    require(device.read_mmio(Device::UnknownControlBase, kWord).value ==
                    0xA55Au &&
                device.read_mmio(Device::UnknownControlBase + 2u, kWord).value ==
                    0x5AA5u &&
                device.read_mmio(Device::UnknownInternalBase, kWord).value ==
                    0x1234u &&
                device.read_mmio(Device::UnknownInternalBase +
                                     Device::UnknownInternalBytes - 2u,
                                 kWord)
                        .value == 0xFEDCu,
            "documented unknown scratch registers retain 16-bit values");

    static Device timing;
    timing.reset();
    put(timing, Device::MainVolume, 0x1000u);
    require(timing.read_mmio(Device::CurrentMainVolume, kWord).ok() &&
                timing.read_mmio(Device::CurrentMainVolume, kWord).value == 0u,
            "fixed volume level is not visible before a native sample boundary");
    put(timing, Device::MainVolume, 0x8000u);
    std::array<StereoSample, 1u> timing_sample{};
    require(timing.render(timing_sample).ok(),
            "apply superseding volume write at a native sample boundary");
    require(timing.read_mmio(Device::CurrentMainVolume, kWord).value == 14'336u,
            "a later sweep write cancels an unapplied fixed volume write");

    static Device current_adsr;
    current_adsr.reset();
    put(current_adsr, 12u, 0x2345u);
    require(current_adsr.read_mmio(12u, kWord).ok() &&
                current_adsr.read_mmio(12u, kWord).value == 0x2345u &&
                current_adsr.voice_state(0u).adsr_volume == 0,
            "current ADSR readback is immediate while its level is delayed");
    require(current_adsr.render(timing_sample).ok() &&
                current_adsr.voice_state(0u).adsr_volume == 0x2345,
            "current ADSR level applies on the next native sample boundary");

    static Device key_timing;
    key_timing.reset();
    put(key_timing, Device::KeyOn, 1u);
    require(!key_timing.voice_state(0u).active,
            "key-on remains pending before a native sample boundary");
    require(key_timing.render(timing_sample).ok() &&
                key_timing.voice_state(0u).active,
            "key-on applies on the next native sample boundary");

    put(device, Device::TransferControl, 0xFFFFu);
    const auto transfer_control = device.read_mmio(Device::TransferControl, kWord);
    require(transfer_control.ok() && transfer_control.value == 0x000Eu,
            "transfer control masks reserved bits");
    require(device.read_mmio(Device::TransferFifo, kWord).fault ==
                MmioFault::TransferFifoUnderflow,
            "empty transfer FIFO is explicit");
    put(device, Device::TransferControl, kNormalTransfer);
    put(device, Device::TransferAddress, 0x0200u);
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        const auto result = device.write_mmio(
            Device::TransferFifo, index + 1u, kWord);
        require(result.ok(), "fill 32-entry transfer FIFO");
    }
    require(device.write_mmio(Device::TransferFifo, 0xFFFFu, kWord).fault ==
                MmioFault::TransferFifoOverflow,
            "31st extra transfer FIFO write is rejected");
    put(device, Device::Control, static_cast<std::uint16_t>(kLive | 0x10u));
    const auto manual_status = device.read_mmio(Device::Status, kWord);
    require(manual_status.ok() &&
                (manual_status.value & Device::StatusTransferRequest) == 0u &&
                (manual_status.value & Device::StatusTransferBusy) != 0u,
            "SPUSTAT bit 7 mirrors SPUCNT bit 5, not manual mode");
    std::array<StereoSample, 1u> first_manual_tick{};
    require(device.render(first_manual_tick).ok(),
            "manual transfer waits for a 44.1 kHz device cycle");
    const auto ram = device.sound_ram();
    require(ram[0x1000u] == 1u && ram[0x1001u] == 0u &&
                ram[0x103Eu] == 32u,
            "manual transfer drains FIFO in order");

    static Device manual_live;
    manual_live.reset();
    put(manual_live, Device::TransferControl, kNormalTransfer);
    put(manual_live, Device::TransferAddress, 0x0200u);
    put(manual_live, Device::Control,
        static_cast<std::uint16_t>(kLive | 0x0010u));
    put(manual_live, Device::TransferFifo, 0xABCDu);
    require(ram_word(manual_live, 0x1000u) == 0u,
            "manual-mode FIFO write waits for a device sample cycle");
    std::array<StereoSample, 1u> manual_tick{};
    require(manual_live.render(manual_tick).ok(),
            "service manual FIFO on bounded sample cycle");
    require(ram_word(manual_live, 0x1000u) == 0xABCDu,
            "manual-mode FIFO can drain without a second control write");

    static Device fifo_wrap;
    fifo_wrap.reset();
    put(fifo_wrap, Device::TransferControl, kNormalTransfer);
    put(fifo_wrap, Device::TransferAddress, 0x0200u);
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        put(fifo_wrap, Device::TransferFifo,
            static_cast<std::uint16_t>(index + 1u));
    }
    put(fifo_wrap, Device::Control,
        static_cast<std::uint16_t>(kLive | kDmaRead));
    render_count(fifo_wrap, 2u, "apply delayed DMA-read mode and request");
    for (std::uint32_t index = 0u; index < 4u; ++index) {
        const auto result = fifo_wrap.read_mmio(Device::TransferFifo, kWord);
        require(result.ok() && result.value == index + 1u,
                "transfer FIFO read advances wrapped head");
    }
    for (std::uint32_t index = 32u; index < 36u; ++index) {
        put(fifo_wrap, Device::TransferFifo,
            static_cast<std::uint16_t>(index + 1u));
    }
    put(fifo_wrap, Device::Control, static_cast<std::uint16_t>(kLive | 0x0010u));
    require(fifo_wrap.render(first_manual_tick).ok(),
            "wrapped manual FIFO drains on device cycle");
    const auto wrapped_ram = fifo_wrap.sound_ram();
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        const std::uint16_t expected = static_cast<std::uint16_t>(index + 5u);
        const std::uint16_t actual = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(wrapped_ram[0x1000u + index * 2u]) |
            (static_cast<std::uint16_t>(
                 wrapped_ram[0x1000u + index * 2u + 1u])
             << 8u));
        require(actual == expected, "wrapped transfer FIFO preserves order");
    }

    const auto block = make_constant_block(2);
    static Device dma;
    dma.reset();
    put(dma, Device::TransferControl, kNormalTransfer);
    put(dma, Device::TransferAddress, 0xFFFFu);
    put(dma, Device::Control, static_cast<std::uint16_t>(kLive | kDmaWrite));
    const auto dma_status = dma.read_mmio(Device::Status, kWord);
    require(dma_status.ok() &&
                (dma_status.value & Device::StatusTransferRequest) == 0u &&
                (dma_status.value & Device::StatusDmaWriteRequest) == 0u,
            "SPUSTAT keeps the old mode until the next device cycle");
    render_count(dma, 1u, "apply delayed DMA-write mode");
    const auto applied_dma_status = dma.read_mmio(Device::Status, kWord);
    require(applied_dma_status.ok() &&
                (applied_dma_status.value & Device::StatusTransferRequest) != 0u &&
                (applied_dma_status.value & Device::StatusDmaWriteRequest) != 0u,
            "DMA write mode exposes SPUSTAT request bits after application");
    std::array<std::uint16_t, 2u> words = {0x1234u, 0x5678u};
    require(dma.dma_write(words).ok(), "bounded DMA write");
    const auto address_after_write =
        dma.read_mmio(Device::TransferAddress, kWord);
    require(address_after_write.ok() && address_after_write.value == 0xFFFFu,
            "transfer address register remains programmed during DMA");
    const auto dma_ram = dma.sound_ram();
    require(dma_ram[524280u] == 0x34u && dma_ram[524281u] == 0x12u &&
                dma_ram[524282u] == 0x78u && dma_ram[524283u] == 0x56u,
            "DMA write uses sound-RAM wrap semantics");
    put(dma, Device::Control, kLive);
    render_count(dma, 1u, "apply delayed DMA stop mode");
    put(dma, Device::TransferAddress, 0xFFFFu);
    put(dma, Device::Control, static_cast<std::uint16_t>(kLive | kDmaRead));
    const auto dma_read_before_tick = dma.read_mmio(Device::Status, kWord);
    require(dma_read_before_tick.ok() &&
                (dma_read_before_tick.value & Device::StatusDmaReadRequest) == 0u,
            "DMA-read request remains low until the delayed mode is applied");
    render_count(dma, 1u, "apply delayed DMA-read mode and request");
    const auto dma_read_after_tick = dma.read_mmio(Device::Status, kWord);
    require(dma_read_after_tick.ok() &&
                (dma_read_after_tick.value & Device::StatusDmaReadRequest) == 0u,
            "DMA-read request remains in its FIFO warm-up interval");
    render_count(dma, 1u, "finish delayed DMA-read FIFO warm-up");
    const auto dma_read_ready = dma.read_mmio(Device::Status, kWord);
    require(dma_read_ready.ok() &&
                (dma_read_ready.value & Device::StatusDmaReadRequest) != 0u,
            "DMA-read request becomes ready after FIFO warm-up");
    std::array<std::uint16_t, 2u> read_words{};
    require(dma.dma_read(read_words).ok() && read_words == words,
            "bounded DMA read");
    require(dma.dma_write(words).fault == DmaFault::WrongTransferMode,
            "DMA write rejects read mode");

    static std::array<std::uint16_t,
                      clean_aot::spu::MaxDmaHalfwordsPerCall + 1u>
        oversized_dma{};
    const std::uint64_t before_oversized_dma = dma.deterministic_digest();
    require(dma.dma_read(oversized_dma).fault == DmaFault::TooManyHalfwords &&
                dma.deterministic_digest() == before_oversized_dma,
            "oversized DMA is rejected without device mutation");

    static std::array<StereoSample,
                      clean_aot::spu::MaxRenderSamplesPerCall + 1u>
        oversized_render{};
    const std::uint64_t before_oversized_render = dma.deterministic_digest();
    const auto oversized_result = dma.render(oversized_render);
    require(oversized_result.fault ==
                clean_aot::spu::RenderFault::TooManySamples &&
                oversized_result.rendered_samples == 0u &&
                dma.deterministic_digest() == before_oversized_render,
            "oversized render is rejected without partial progress");

    static Device transformed;
    transformed.reset();
    put(transformed, Device::TransferControl, 0u);
    put(transformed, Device::TransferAddress, 0x0200u);
    put(transformed, Device::Control,
        static_cast<std::uint16_t>(kLive | kDmaWrite));
    render_count(transformed, 1u, "apply transformed DMA-write mode");
    std::array<std::uint16_t, 64u> fill_words{};
    for (std::size_t index = 0u; index < fill_words.size(); ++index) {
        fill_words[index] = static_cast<std::uint16_t>(index + 1u);
    }
    const auto fill_result = transformed.dma_write(fill_words);
    require(fill_result.ok() &&
                fill_result.transferred_halfwords == fill_words.size(),
            "DMA fill consumes two bounded FIFO blocks");
    for (std::size_t index = 0u; index < fill_words.size(); ++index) {
        const std::uint16_t expected = index < 32u ? 32u : 64u;
        require(ram_word(transformed, 0x1000u + index * 2u) == expected,
                "DMA fill uses the last halfword of each 32-word FIFO block");
    }

    const auto verify_replication = [](std::uint16_t replication_control,
                                       const std::array<std::uint16_t, 8u>&
                                           expected) {
        static Device replicated;
        replicated.reset();
        put(replicated, Device::TransferControl, replication_control);
        put(replicated, Device::TransferAddress, 0x0200u);
        put(replicated, Device::Control,
            static_cast<std::uint16_t>(kLive | kDmaWrite));
        render_count(replicated, 1u, "apply replicated DMA-write mode");
        constexpr std::array<std::uint16_t, 8u> input = {
            0x11u, 0x22u, 0x33u, 0x44u,
            0x55u, 0x66u, 0x77u, 0x88u,
        };
        require(replicated.dma_write(input).ok(),
                "DMA replication transfer succeeds");
        for (std::size_t index = 0u; index < expected.size(); ++index) {
            require(ram_word(replicated, 0x1000u + index * 2u) ==
                        expected[index],
                    "DMA replication mode matches FIFO forwarding");
        }
    };
    verify_replication(0x0006u,
                       {0x11u, 0x11u, 0x33u, 0x33u,
                        0x55u, 0x55u, 0x77u, 0x77u});
    verify_replication(0x0008u,
                       {0x11u, 0x11u, 0x11u, 0x11u,
                        0x55u, 0x55u, 0x55u, 0x55u});
    verify_replication(0x000Au,
                       {0x88u, 0x88u, 0x88u, 0x88u,
                        0x88u, 0x88u, 0x88u, 0x88u});

    IrqCounter counter;
    static Device irq_device;
    irq_device.reset();
    irq_device.set_irq_callback(IrqCounter::signal, &counter);
    load_block(irq_device, block);
    put(irq_device, Device::MainVolume, 0x3FFFu);
    put(irq_device, Device::MainVolume + 2u, 0x3FFFu);
    configure_voice(irq_device, 0u);
    put(irq_device, Device::IrqAddress, 0u);
    put(irq_device, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlIrqEnable));
    put(irq_device, Device::KeyOn, 1u);
    put(irq_device, 12u, 0x7FFFu);
    std::array<StereoSample, 1u> sample{};
    require(irq_device.render(sample).ok() && irq_device.irq_pending() &&
                counter.calls == 1u,
            "ADPCM block crossing signals IRQ once");
    irq_device.acknowledge_irq();
    require(!irq_device.irq_pending(), "IRQ acknowledge clears pending state");

    IrqCounter scheduled_counter;
    static Device scheduled_irq;
    scheduled_irq.reset();
    scheduled_irq.set_irq_callback(IrqCounter::signal, &scheduled_counter);
    load_block(scheduled_irq, block);
    put(scheduled_irq, Device::TransferControl, 0u);
    put(scheduled_irq, Device::ReverbBase, 0xFFFFu);
    configure_voice(scheduled_irq, 0u, 0x1000u, 0x7FFFu);
    put(scheduled_irq, Device::IrqAddress, 1u);
    put(scheduled_irq, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlIrqEnable));
    put(scheduled_irq, Device::KeyOn, 1u);
    put(scheduled_irq, 12u, 0x7FFFu);
    std::array<StereoSample, 12u> before_data_word{};
    require(scheduled_irq.render(before_data_word).ok() &&
                !scheduled_irq.irq_pending(),
            "voice IRQ does not pre-touch a complete ADPCM block");
    require(scheduled_irq.render(sample).ok() &&
                scheduled_irq.irq_pending() && scheduled_counter.calls == 1u,
            "voice IRQ follows the scheduled packed-data halfword read");
}

void test_adsr_sweep_noise_and_modulation() {
    const auto block = make_constant_block(2);
    static Device device;
    device.reset();
    prepare_device(device, block);
    put(device, 8u, 0u);
    put(device, 10u, 0u);
    put(device, 12u, 0u);
    std::array<StereoSample, 1u> first{};
    require(device.render(first).ok(), "attack sample");
    const auto attack_view = device.voice_state(0u);
    require(attack_view.adsr_volume > 0 &&
                (attack_view.phase == clean_aot::spu::AdsrPhase::Attack ||
                 attack_view.phase == clean_aot::spu::AdsrPhase::Decay),
            "attack advances from zero");
    std::array<StereoSample, 8u> envelope{};
    require(device.render(envelope).ok(), "decay samples");
    put(device, Device::KeyOff, 1u);
    std::array<StereoSample, 4096u> release{};
    require(device.render(release).ok(), "release samples");
    require(device.voice_state(0u).phase == clean_aot::spu::AdsrPhase::Off &&
                device.voice_state(0u).active,
            "release reaches silent off phase while voice scanning continues");

    const auto configure_digest_voice = [](Device& target) {
        target.reset();
        load_block(target, make_constant_block(7, 0x03u));
        put(target, Device::MainVolume, 0x3FFFu);
        put(target, Device::MainVolume + 2u, 0x3FFFu);
        configure_voice(target, 0u, 0x1000u, 0x300Fu, 0x007Fu);
        put(target, Device::KeyOn, 1u);
    };
    static Device digest_whole;
    static Device digest_chunked;
    configure_digest_voice(digest_whole);
    configure_digest_voice(digest_chunked);
    std::array<StereoSample, SamplesPer100HzUpdate> whole_audio{};
    std::array<StereoSample, SamplesPer100HzUpdate> chunked_audio{};
    require(digest_whole.render_100hz_update(whole_audio).ok(),
            "render ADSR digest at exact 100 Hz");
    for (std::size_t offset = 0u; offset < chunked_audio.size(); offset += 147u) {
        require(digest_chunked.render(
                    std::span<StereoSample>(chunked_audio).subspan(offset, 147u))
                    .ok(),
                "render chunked ADSR digest");
    }
    const std::uint64_t envelope_digest = audio_digest(whole_audio);
    require(whole_audio == chunked_audio &&
                digest_whole.deterministic_digest() ==
                    digest_chunked.deterministic_digest(),
            "ADSR timing is invariant across 100 Hz render chunking");
    require(envelope_digest == 9'303'418'669'728'391'773ull,
            "ADSR one-sample edge audio digest");

    for (std::uint16_t shift = 0u; shift < 32u; ++shift) {
        static Device rate_device;
        rate_device.reset();
        prepare_device(rate_device, block);
        put(rate_device, 8u, static_cast<std::uint16_t>(shift << 10u));
        put(rate_device, 12u, 0u);
        std::array<StereoSample, 2u> rate_output{};
        require(rate_device.render(rate_output).ok(), "ADSR rate vector");
        require(rate_device.voice_state(0u).adsr_volume >= 0,
                "ADSR rate remains in fixed signed range");
    }

    static Device sweep;
    sweep.reset();
    prepare_device(sweep, block);
    put(sweep, 0u, 0x1000u);
    const auto before_sweep = sweep.read_mmio(Device::InternalVoiceVolumeBase,
                                              kWord);
    put(sweep, 0u, 0x8000u);
    std::array<StereoSample, 1u> sweep_output{};
    require(sweep.render(sweep_output).ok(), "volume sweep sample");
    const auto after_sweep = sweep.read_mmio(Device::InternalVoiceVolumeBase,
                                             kWord);
    require(before_sweep.ok() && after_sweep.ok() &&
                before_sweep.value != after_sweep.value,
            "linear volume sweep changes current volume");

    static Device wipeout_sweep;
    wipeout_sweep.reset();
    put(wipeout_sweep, 0u, 0x1000u);
    render_count(wipeout_sweep, 1u,
                 "apply inverted sweep starting fixed volume");
    put(wipeout_sweep, 0u, 0xFFFFu);
    render_count(wipeout_sweep, 65'536u,
                 "render inverted all-ones sweep");
    const auto frozen_sweep = wipeout_sweep.read_mmio(
        Device::InternalVoiceVolumeBase, kWord);
    require(frozen_sweep.ok() && frozen_sweep.value == 0x2000u,
            "all-ones inverted sweep never steps or saturates");

    static Device inverted_increase;
    inverted_increase.reset();
    put(inverted_increase, 0u, 0x0001u);
    put(inverted_increase, 0u, 0x9000u);
    std::array<StereoSample, 1u> inverted_output{};
    require(inverted_increase.render(inverted_output).ok(),
            "render inverted increasing sweep");
    const auto inverted_level = inverted_increase.read_mmio(
        Device::InternalVoiceVolumeBase, kWord);
    require(inverted_level.ok() && (inverted_level.value & 0x8000u) != 0u,
            "inverted increasing sweep crosses zero into signed range");

    static Device decay;
    decay.reset();
    load_block(decay, block);
    configure_voice(decay, 0u, 0x1000u, 0x0000u, 0u);
    put(decay, Device::KeyOn, 1u);
    put(decay, 12u, 0x7FFFu);
    std::array<StereoSample, 2u> decay_output{};
    require(decay.render(decay_output).ok(), "render fixed-step decay");
    require(decay.voice_state(0u).adsr_volume == 24'575,
            "decay uses odd five-bit rate and fixed -8 step");

    static Device release_device;
    release_device.reset();
    load_block(release_device, block);
    configure_voice(release_device, 0u, 0x1000u, 0x7FFFu, 0u);
    put(release_device, Device::KeyOn, 1u);
    put(release_device, 12u, 0x4000u);
    put(release_device, Device::KeyOff, 1u);
    std::array<StereoSample, 1u> release_output{};
    require(release_device.render(release_output).ok(),
            "render fixed-step release");
    require(release_device.voice_state(0u).adsr_volume == 0 &&
                release_device.voice_state(0u).phase ==
                    clean_aot::spu::AdsrPhase::Off,
            "release shift zero applies fixed -8 step");

    static Device frozen_release;
    frozen_release.reset();
    load_block(frozen_release, block);
    configure_voice(frozen_release, 0u, 0x1000u, 0x7FFFu, 0x001Fu);
    put(frozen_release, Device::KeyOn, 1u);
    put(frozen_release, 12u, 0x4000u);
    put(frozen_release, Device::KeyOff, 1u);
    render_count(frozen_release, 65'536u,
                 "render all-ones release rate");
    require(frozen_release.voice_state(0u).adsr_volume == 0x4000 &&
                frozen_release.voice_state(0u).phase ==
                    clean_aot::spu::AdsrPhase::Release,
            "all-ones release rate remains frozen");

    static Device modulation_off;
    modulation_off.reset();
    load_block(modulation_off, make_constant_block(7));
    put(modulation_off, Device::MainVolume, 0x3FFFu);
    put(modulation_off, Device::MainVolume + 2u, 0x3FFFu);
    configure_voice(modulation_off, 0u);
    configure_voice(modulation_off, 1u, 0x2000u);
    put(modulation_off, Device::KeyOn, 0x0003u);
    put(modulation_off, 12u, 0x7FFFu);
    put(modulation_off, 0x1Cu, 0x7FFFu);
    std::array<StereoSample, 2u> modulation_output{};
    require(modulation_off.render(modulation_output).ok(),
            "unmodulated pitch sample");
    const auto off_view = modulation_off.voice_state(1u);

    static Device modulation_on;
    modulation_on.reset();
    load_block(modulation_on, make_constant_block(7));
    put(modulation_on, Device::MainVolume, 0x3FFFu);
    put(modulation_on, Device::MainVolume + 2u, 0x3FFFu);
    configure_voice(modulation_on, 0u);
    configure_voice(modulation_on, 1u, 0x2000u);
    put(modulation_on, Device::PitchModulation, 0x0002u);
    put(modulation_on, Device::KeyOn, 0x0003u);
    put(modulation_on, 12u, 0x7FFFu);
    put(modulation_on, 0x1Cu, 0x7FFFu);
    require(modulation_on.render(modulation_output).ok(),
            "modulated pitch sample");
    const auto on_view = modulation_on.voice_state(1u);
    require(off_view.pitch_counter == 0x4000u &&
                on_view.pitch_counter == 17448u,
            "pitch modulation is applied before pitch accumulation");

    static Device noise;
    noise.reset();
    load_block(noise, block);
    put(noise, Device::MainVolume, 0x3FFFu);
    put(noise, Device::MainVolume + 2u, 0x3FFFu);
    configure_voice(noise, 0u, 0u);
    put(noise, Device::NoiseEnable, 1u);
    put(noise, Device::KeyOn, 1u);
    put(noise, 12u, 0x7FFFu);
    put(noise, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlNoiseShiftMask));
    std::array<StereoSample, 64u> noise_output{};
    require(noise.render(noise_output).ok(), "noise generation");
    bool nonzero = false;
    for (const StereoSample sample : noise_output) {
        nonzero = nonzero || sample.left != 0 || sample.right != 0;
    }
    require(nonzero, "noise source reaches mixer");

    static Device continuous_a;
    static Device continuous_b;
    continuous_a.reset();
    continuous_b.reset();
    load_block(continuous_a, block);
    load_block(continuous_b, block);
    configure_voice(continuous_a, 0u, 0u, 0x7FFFu);
    configure_voice(continuous_b, 0u, 0u, 0x7FFFu);
    put(continuous_a, Device::KeyOn, 1u);
    put(continuous_b, Device::KeyOn, 1u);
    put(continuous_a, 12u, 0x7FFFu);
    put(continuous_b, 12u, 0x7FFFu);
    put(continuous_a, Device::NoiseEnable, 1u);
    put(continuous_a, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlNoiseShiftMask));
    put(continuous_b, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlNoiseShiftMask));
    std::array<StereoSample, 257u> warmup{};
    require(continuous_a.render(warmup).ok() &&
                continuous_b.render(warmup).ok(),
            "advance global noise while routing differs");
    put(continuous_b, Device::NoiseEnable, 1u);
    std::array<StereoSample, 128u> noise_a{};
    std::array<StereoSample, 128u> noise_b{};
    require(continuous_a.render(noise_a).ok() &&
                continuous_b.render(noise_b).ok(),
            "compare continuously clocked noise");
    require(noise_a == noise_b &&
                continuous_a.deterministic_digest() ==
                    continuous_b.deterministic_digest(),
            "noise generator clocks globally even when no voice selects it");

    static Device noise_scan;
    noise_scan.reset();
    load_block(noise_scan, make_constant_block(2, 0x03u));
    configure_voice(noise_scan, 0u, 0x1000u, 0x7FFFu);
    put(noise_scan, Device::NoiseEnable, 1u);
    put(noise_scan, Device::KeyOn, 1u);
    put(noise_scan, 12u, 0x7FFFu);
    render_count(noise_scan, 29u, "scan ADPCM flags under noise routing");
    const auto noise_endx = noise_scan.read_mmio(Device::EndFlags, kWord);
    require(noise_endx.ok() && (noise_endx.value & 1u) != 0u,
            "noise-routed voice still scans ADPCM loop flags");
}

void test_mix_capture_and_input_fifos() {
    static Device saturated;
    saturated.reset();
    load_block(saturated, make_constant_block(7));
    put(saturated, Device::MainVolume, 0x3FFFu);
    put(saturated, Device::MainVolume + 2u, 0x3FFFu);
    for (std::size_t index = 0u; index < 24u; ++index) {
        configure_voice(saturated, index);
        put(saturated, static_cast<std::uint32_t>(index * 0x10u + 12u),
            0x7FFFu);
    }
    put(saturated, Device::KeyOn, 0xFFFFu);
    put(saturated, Device::KeyOn + 2u, 0x00FFu);
    std::array<StereoSample, 4u> saturated_output{};
    require(saturated.render(saturated_output).ok(), "24-voice mix");
    require(saturated_output[1].left == 32767 &&
                saturated_output[1].right == 32767,
            "voice mix saturates at signed 16-bit maximum");

    IrqCounter capture_irq;
    static Device capture;
    capture.reset();
    capture.set_irq_callback(IrqCounter::signal, &capture_irq);
    load_block(capture, make_constant_block(2));
    put(capture, Device::MainVolume, 0x3FFFu);
    put(capture, Device::MainVolume + 2u, 0x3FFFu);
    configure_voice(capture, 0u);
    put(capture, Device::TransferControl, 0x000Cu);
    put(capture, Device::IrqAddress, 0x0081u);
    put(capture, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlIrqEnable));
    put(capture, Device::KeyOn, 1u);
    put(capture, 12u, 0x7FFFu);
    std::array<StereoSample, 1u> capture_input = {StereoSample{1234, -2345}};
    require(capture.push_cdda(capture_input).accepted == 1u,
            "push capture CD input");
    std::array<StereoSample, 1u> capture_output{};
    require(capture.render(capture_output).ok(), "capture first sample");
    const auto captured_ram = capture.sound_ram();
    require(captured_ram[2u] == 0xD2u && captured_ram[3u] == 0x04u &&
                captured_ram[0x402u] == 0xD7u &&
                captured_ram[0x403u] == 0xF6u,
            "CD capture writes raw stereo samples");
    std::array<StereoSample, 256u> half{};
    require(capture.render(half).ok(), "capture second half boundary");
    const auto status = capture.read_mmio(Device::Status, kWord);
    require(status.ok() && (status.value & Device::StatusCaptureSecondHalf) != 0u,
            "capture status exposes second half");
    require(capture_irq.calls > 0u, "capture crossing signals IRQ");

    IrqCounter passive_capture_irq;
    static Device passive_capture;
    passive_capture.reset();
    passive_capture.set_irq_callback(IrqCounter::signal, &passive_capture_irq);
    put(passive_capture, Device::ReverbBase, 0xFFFFu);
    put(passive_capture, Device::IrqAddress, 0x0080u);
    put(passive_capture, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlIrqEnable));
    constexpr std::array<StereoSample, 1u> passive_input = {
        StereoSample{3210, -4321},
    };
    require(passive_capture.push_cdda(passive_input).accepted == 1u,
            "queue CD sample while dry output is disabled");
    std::array<StereoSample, 1u> passive_output{};
    require(passive_capture.render(passive_output).ok(),
            "always-active CD capture tick");
    require(passive_output[0u] == StereoSample{0, 0} &&
                passive_capture.input_status(AudioSource::Cdda).queued == 0u &&
                ram_word(passive_capture, 0u) == 3210u &&
                ram_word(passive_capture, 0x400u) ==
                    static_cast<std::uint16_t>(-4321),
            "CD capture consumes and stores raw input independently of dry routing");
    require(passive_capture_irq.calls == 0u,
            "capture writes are IRQ-gated when transfer-control bits are clear");
    render_count(passive_capture, 255u,
                 "advance always-active capture to second half");
    const auto passive_status = passive_capture.read_mmio(Device::Status, kWord);
    require(passive_status.ok() &&
                (passive_status.value & Device::StatusCaptureSecondHalf) == 0u &&
                !passive_capture.input_status(AudioSource::Cdda).underflow,
            "capture advances without inventing inactive-stream underflow or status");
    put(passive_capture, Device::TransferControl, 0x0004u);
    const auto visible_capture_status =
        passive_capture.read_mmio(Device::Status, kWord);
    require(visible_capture_status.ok() &&
                (visible_capture_status.value &
                 Device::StatusCaptureSecondHalf) != 0u,
            "capture-half status is exposed only when capture IRQ gating is enabled");

    IrqCounter enabled_capture_irq;
    static Device enabled_capture;
    enabled_capture.reset();
    enabled_capture.set_irq_callback(IrqCounter::signal, &enabled_capture_irq);
    put(enabled_capture, Device::ReverbBase, 0xFFFFu);
    put(enabled_capture, Device::TransferControl, 0x0004u);
    put(enabled_capture, Device::IrqAddress, 0x0080u);
    put(enabled_capture, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlIrqEnable));
    require(enabled_capture.render(passive_output).ok() &&
                enabled_capture_irq.calls == 1u,
            "capture write signals IRQ when either gate bit is enabled");

    std::array<StereoSample, AudioFifoCapacity + 2u> fifo_input{};
    fifo_input.fill(StereoSample{1000, -1000});
    static Device fifo;
    fifo.reset();
    const auto pushed = fifo.push_cdda(fifo_input);
    require(pushed.accepted == AudioFifoCapacity && pushed.dropped == 2u &&
                pushed.overflow,
            "CDDA FIFO drops newest samples at capacity");
    const auto full_status = fifo.input_status(AudioSource::Cdda);
    require(full_status.overflow && full_status.overflow_count == 2u,
            "CDDA FIFO overflow is observable");
    put(fifo, Device::MainVolume, 0x3FFFu);
    put(fifo, Device::MainVolume + 2u, 0x3FFFu);
    put(fifo, Device::CdVolume, 0x7FFFu);
    put(fifo, Device::CdVolume + 2u, 0x7FFFu);
    put(fifo, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlCdEnable));
    std::array<StereoSample, clean_aot::spu::MaxRenderSamplesPerCall> fifo_output{};
    require(fifo.render(fifo_output).ok() && fifo.render(fifo_output).ok(),
            "CDDA FIFO consumption in bounded native chunks");
    std::array<StereoSample, 1u> underflow_output{};
    require(fifo.render(underflow_output).ok(), "CDDA FIFO underflow probe");
    const auto empty_status = fifo.input_status(AudioSource::Cdda);
    require(empty_status.underflow && empty_status.underflow_count == 1u,
            "CDDA FIFO underflow is observable");

    static Device xa;
    xa.reset();
    xa.set_audio_source(AudioSource::XaResampled);
    put(xa, Device::MainVolume, 0x3FFFu);
    put(xa, Device::MainVolume + 2u, 0x3FFFu);
    put(xa, Device::CdVolume, 0x7FFFu);
    put(xa, Device::CdVolume + 2u, 0x7FFFu);
    put(xa, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlCdEnable));
    const auto xa_push = xa.push_xa_resampled(
        std::span<const StereoSample>(capture_input));
    require(xa_push.accepted == 1u && xa.audio_source() == AudioSource::XaResampled,
            "resampled-XA boundary accepts stereo PCM");
    std::array<StereoSample, 1u> xa_output{};
    require(xa.render(xa_output).ok() && xa_output[0].left > 0 &&
                xa_output[0].right < 0,
            "resampled-XA sample reaches CD mixer");

    static Device external;
    external.reset();
    put(external, Device::MainVolume, 0x3FFFu);
    put(external, Device::MainVolume + 2u, 0x3FFFu);
    put(external, Device::ExternalVolume, 0x7FFFu);
    put(external, Device::ExternalVolume + 2u, 0x7FFFu);
    put(external, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlExternalEnable));
    require(external.push_external(capture_input).accepted == 1u,
            "external input boundary accepts stereo PCM");
    std::array<StereoSample, 1u> external_output{};
    require(external.render(external_output).ok() &&
                external_output[0].left > 0 && external_output[0].right < 0,
            "external sample reaches mixer");

    static_assert(clean_aot::spu::ReverbFirTapCount == 39u);
    static_assert(clean_aot::spu::ReverbFirDelaySamples == 19u);
    static_assert(clean_aot::spu::ReverbCombinedDelaySamples == 38u);
    static Device reverb_fir;
    reverb_fir.reset();
    put(reverb_fir, Device::TransferControl, kNormalTransfer);
    put(reverb_fir, Device::TransferAddress, 0x1001u);
    put(reverb_fir, Device::Control,
        static_cast<std::uint16_t>(kLive | kDmaWrite |
                                   Device::ControlNoiseShiftMask));
    render_count(reverb_fir, 1u, "apply reverb FIR impulse DMA mode");
    constexpr std::array<std::uint16_t, 1u> reverb_impulse = {0x4000u};
    require(reverb_fir.dma_write(reverb_impulse).ok(),
            "seed reverb FIR impulse through the bounded DMA boundary");
    put(reverb_fir, Device::Control, kLive);
    put(reverb_fir, Device::ReverbBase, 0x1000u);
    put(reverb_fir, Device::ReverbOutputVolume, 0x7FFFu);
    put(reverb_fir, Device::ReverbOutputVolume + 2u, 0x7FFFu);
    for (std::size_t index = 0u; index < 32u; ++index) {
        put(reverb_fir,
            Device::ReverbRegisterBase +
                static_cast<std::uint32_t>(index * 2u),
            0u);
    }
    put(reverb_fir, Device::ReverbRegisterBase + 28u * 2u, 1u);
    put(reverb_fir, Device::ReverbRegisterBase + 29u * 2u, 1u);
    std::array<StereoSample, 20u> reverb_impulse_output{};
    require(reverb_fir.render(reverb_impulse_output).ok(),
            "render alternating reverb FIR impulse");
    require(reverb_impulse_output[19u].right == 16'382 &&
                reverb_impulse_output[19u].left == 0,
            "reverb output FIR preserves 19-sample phase and alternating sides");

    static Device reverb_delay;
    reverb_delay.reset();
    put(reverb_delay, Device::ReverbBase, 0x1000u);
    put(reverb_delay, Device::ReverbOutputVolume, 0u);
    put(reverb_delay, Device::ReverbOutputVolume + 2u, 0x7FFFu);
    for (std::size_t index = 0u; index < 32u; ++index) {
        put(reverb_delay,
            Device::ReverbRegisterBase +
                static_cast<std::uint32_t>(index * 2u),
            0u);
    }
    put(reverb_delay, Device::ReverbRegisterBase + 2u * 2u, 0x7FFFu);
    put(reverb_delay, Device::ReverbRegisterBase + 27u * 2u, 1u);
    put(reverb_delay, Device::ReverbRegisterBase + 31u * 2u, 0x7FFFu);
    put(reverb_delay, Device::CdVolume, 0u);
    put(reverb_delay, Device::CdVolume + 2u, 0x7FFFu);
    put(reverb_delay, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlCdReverb |
                                   Device::ControlReverbEnable));
    constexpr std::array<StereoSample, 1u> delayed_input = {
        StereoSample{0, 12'000},
    };
    require(reverb_delay.push_cdda(delayed_input).accepted == 1u,
            "queue the reverb delay impulse");
    std::array<StereoSample, 40u> reverb_delay_output{};
    require(reverb_delay.render(reverb_delay_output).ok(),
            "render combined reverb FIR delay");
    require(reverb_delay_output[38u].right > reverb_delay_output[37u].right &&
                reverb_delay_output[38u].right > reverb_delay_output[39u].right &&
                reverb_delay_output[38u].left == 0,
            "input and output reverb FIRs preserve their combined 38-sample delay");

    const auto configure_reverb_route = [](Device& routed) {
        put(routed, Device::ReverbBase, 0x1000u);
        put(routed, Device::ReverbRegisterBase + 2u * 2u, 0x7FFFu);
        for (std::size_t index = 10u; index <= 29u; ++index) {
            put(routed,
                Device::ReverbRegisterBase +
                    static_cast<std::uint32_t>(index * 2u),
                1u);
        }
        put(routed, Device::ReverbRegisterBase + 10u * 2u, 0u);
        put(routed, Device::ReverbRegisterBase + 30u * 2u, 0x7FFFu);
    };

    static Device cd_reverb_only;
    cd_reverb_only.reset();
    configure_reverb_route(cd_reverb_only);
    put(cd_reverb_only, Device::CdVolume, 0x7FFFu);
    put(cd_reverb_only, Device::CdVolume + 2u, 0x7FFFu);
    put(cd_reverb_only, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlCdReverb |
                                   Device::ControlReverbEnable));
    constexpr std::array<StereoSample, 1u> reverb_input = {
        StereoSample{12'000, 0},
    };
    require(cd_reverb_only.push_cdda(reverb_input).accepted == 1u &&
                cd_reverb_only.render(passive_output).ok(),
            "route CD input to reverb without dry enable");
    require(passive_output[0u] == StereoSample{0, 0} &&
                ram_word(cd_reverb_only, 0x8000u) != 0u,
            "CD dry and reverb route bits are independent");

    static Device external_reverb_only;
    external_reverb_only.reset();
    configure_reverb_route(external_reverb_only);
    put(external_reverb_only, Device::ExternalVolume, 0x7FFFu);
    put(external_reverb_only, Device::ExternalVolume + 2u, 0x7FFFu);
    put(external_reverb_only, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlExternalReverb |
                                   Device::ControlReverbEnable));
    require(external_reverb_only.push_external(reverb_input).accepted == 1u &&
                external_reverb_only.render(passive_output).ok(),
            "route external input to reverb without dry enable");
    require(passive_output[0u] == StereoSample{0, 0} &&
                ram_word(external_reverb_only, 0x8000u) != 0u,
            "external dry and reverb route bits are independent");

    IrqCounter disabled_reverb_irq;
    static Device disabled_reverb;
    disabled_reverb.reset();
    disabled_reverb.set_irq_callback(IrqCounter::signal,
                                     &disabled_reverb_irq);
    configure_reverb_route(disabled_reverb);
    put(disabled_reverb, Device::IrqAddress, 0x1000u);
    put(disabled_reverb, Device::Control,
        static_cast<std::uint16_t>(kLive | Device::ControlIrqEnable));
    require(disabled_reverb.render(passive_output).ok() &&
                disabled_reverb_irq.calls == 1u,
            "disabled reverb substitutes destination reads for writes");
}

struct DmaCollector final {
    std::uint32_t calls{0u};
    std::uint32_t words{0u};

    static bool collect(std::span<const std::uint16_t> values,
                        void* user) noexcept {
        auto* collector = static_cast<DmaCollector*>(user);
        collector->calls += 1u;
        collector->words += static_cast<std::uint32_t>(values.size());
        return true;
    }
};

struct RejectingDmaCollector final {
    std::uint32_t calls{0u};

    static bool collect(std::span<const std::uint16_t>, void* user) noexcept {
        auto* collector = static_cast<RejectingDmaCollector*>(user);
        collector->calls += 1u;
        return collector->calls < 2u;
    }
};

void test_snapshot_chunking_and_long_replay() {
    const auto block = make_constant_block(3, 0x03u);
    static Device first;
    first.reset();
    prepare_device(first, block);
    DmaCollector collector;
    put(first, Device::Control, static_cast<std::uint16_t>(kLive | kDmaRead));
    put(first, Device::TransferAddress, 0u);
    render_count(first, 2u, "apply delayed DMA-read mode and request");
    require(first.dma_read(DmaCollector::collect, &collector, 64u).ok() &&
                collector.calls == 2u && collector.words == 64u,
            "typed bounded DMA callback");
    const std::uint64_t before_null_callback = first.deterministic_digest();
    require(first.dma_read(nullptr, nullptr, 1u).fault ==
                DmaFault::CallbackRejected &&
                first.deterministic_digest() == before_null_callback,
            "null DMA callback is rejected before consuming device state");

    put(first, Device::TransferAddress, 0u);
    RejectingDmaCollector rejecting;
    const auto rejected = first.dma_read(RejectingDmaCollector::collect,
                                         &rejecting, 64u);
    require(rejected.fault == DmaFault::CallbackRejected &&
                rejected.transferred_halfwords == 64u &&
                rejecting.calls == 2u,
            "rejected DMA callback reports words already consumed from device");
    put(first, Device::UnknownControlBase, 0xA55Au);
    put(first, Device::UnknownInternalBase, 0x5AA5u);

    static std::array<std::byte, Device::SnapshotBytes> snapshot{};
    const auto saved = first.save_snapshot(snapshot);
    require(saved.ok() && saved.bytes == Device::SnapshotBytes,
            "save complete snapshot");
    const std::uint64_t saved_digest = first.deterministic_digest();
    std::array<StereoSample, 512u> one_chunk{};
    std::array<StereoSample, 512u> chunked{};
    require(first.render(one_chunk).ok(), "long one-chunk render");

    static Device replay;
    replay.reset();
    require(replay.restore_snapshot(snapshot).ok() &&
                replay.deterministic_digest() == saved_digest &&
                replay.read_mmio(Device::UnknownControlBase, kWord).value ==
                    0xA55Au &&
                replay.read_mmio(Device::UnknownInternalBase, kWord).value ==
                    0x5AA5u,
            "restore validates and reproduces digest");
    for (std::size_t offset = 0u; offset < chunked.size();) {
        const std::size_t count = std::min<std::size_t>(7u,
                                                         chunked.size() - offset);
        require(replay.render(std::span<StereoSample>(chunked).subspan(offset,
                                                                       count))
                    .ok(),
                "chunked render");
        offset += count;
    }
    require(one_chunk == chunked,
            "render output is invariant under caller chunking");
    require(first.deterministic_digest() == replay.deterministic_digest(),
            "render state is invariant under caller chunking");

    const std::uint64_t before_bad_restore = replay.deterministic_digest();
    static std::array<std::byte, Device::SnapshotBytes> forged{};
    forged = snapshot;
    constexpr std::size_t voice_sample_index_offset =
        Device::SnapshotHeaderBytes + clean_aot::spu::SoundRamBytes + 49u;
    forged[voice_sample_index_offset] = std::byte{28u};
    reseal_snapshot(forged);
    const auto bad_voice = replay.restore_snapshot(forged);
    require(bad_voice.fault == SnapshotFault::InvalidPayload &&
                replay.deterministic_digest() == before_bad_restore,
            "self-consistent digest cannot admit invalid voice sample index");

    forged = snapshot;
    constexpr std::size_t first_audio_fifo_payload_offset =
        Device::SnapshotPayloadBytes -
        (3u * Device::AudioFifoSnapshotBytes) -
        (32u * sizeof(std::uint16_t));
    set_snapshot_u16(
        forged,
        Device::SnapshotHeaderBytes + first_audio_fifo_payload_offset + 4u,
        1u);
    reseal_snapshot(forged);
    const auto bad_fifo = replay.restore_snapshot(forged);
    require(bad_fifo.fault == SnapshotFault::InvalidPayload &&
                replay.deterministic_digest() == before_bad_restore,
            "snapshot rejects incoherent bounded FIFO topology atomically");

    snapshot[0u] = std::byte{0x00};
    const auto bad = replay.restore_snapshot(snapshot);
    require(bad.fault == SnapshotFault::InvalidHeader &&
                replay.deterministic_digest() == before_bad_restore,
            "invalid snapshot is rejected without state mutation");

    static Device long_a;
    static Device long_b;
    long_a.reset();
    long_b.reset();
    prepare_device(long_a, make_constant_block(5));
    prepare_device(long_b, make_constant_block(5));
    std::array<StereoSample, 2048u> long_output_a{};
    std::array<StereoSample, 2048u> long_output_b{};
    require(long_a.render(long_output_a).ok(), "long deterministic render A");
    for (std::size_t offset = 0u; offset < long_output_b.size();) {
        const std::size_t count = std::min<std::size_t>(113u,
                                                         long_output_b.size() - offset);
        require(long_b.render(std::span<StereoSample>(long_output_b)
                                  .subspan(offset, count))
                    .ok(),
                "long deterministic render B");
        offset += count;
    }
    require(long_output_a == long_output_b &&
                long_a.deterministic_digest() == long_b.deterministic_digest(),
            "long generation remains deterministic");
}

void test_fixed_100hz_integration_contract() {
    static_assert(clean_aot::spu::OutputSampleRateHz == 44'100u);
    static_assert(SamplesPer100HzUpdate == 441u);
    static_assert(clean_aot::spu::OutputSampleRateHz /
                      clean_aot::spu::RemasterUpdateRateHz ==
                  SamplesPer100HzUpdate);

    const auto block = make_constant_block(5, 0x03u);
    const auto configure = [&block](Device& device) {
        device.reset();
        load_block(device, block, 0x0200u);
        put(device, Device::MainVolume, 0x3FFFu);
        put(device, Device::MainVolume + 2u, 0x3FFFu);
        configure_voice(device, 0u, 0x1000u, 0x7FFFu);
        put(device, 6u, 0x0200u);
        put(device, 14u, 0x0200u);
        put(device, Device::KeyOn, 1u);
        put(device, 12u, 0x7FFFu);
    };

    static Device fixed_updates;
    static Device arbitrary_chunks;
    configure(fixed_updates);
    configure(arbitrary_chunks);

    constexpr std::size_t update_count = 100u;
    constexpr std::size_t total_samples =
        update_count * SamplesPer100HzUpdate;
    static_assert(total_samples == 44'100u);
    static std::array<StereoSample, total_samples> fixed_output{};
    static std::array<StereoSample, total_samples> chunked_output{};
    constexpr std::array<std::uint32_t, 4u> retirement_multipliers = {
        1u, 2u, 4u, 8u,
    };
    std::uint64_t simulated_cpu_retirements = 0u;
    std::size_t rendered = 0u;
    for (std::size_t update = 0u; update < update_count; ++update) {
        const std::uint32_t multiplier =
            retirement_multipliers[update % retirement_multipliers.size()];
        simulated_cpu_retirements +=
            static_cast<std::uint64_t>(338'688u) * multiplier;
        auto update_output = std::span<StereoSample, SamplesPer100HzUpdate>(
            fixed_output.data() + rendered, SamplesPer100HzUpdate);
        const auto result = fixed_updates.render_100hz_update(update_output);
        require(result.ok() &&
                    result.rendered_samples == SamplesPer100HzUpdate,
                "100 Hz boundary emits exactly 441 samples");
        rendered += result.rendered_samples;
    }
    require(rendered == total_samples && simulated_cpu_retirements != 0u,
            "CPU retirement accounting cannot enter the SPU sample API");

    for (std::size_t offset = 0u; offset < chunked_output.size();) {
        const std::size_t count = std::min<std::size_t>(
            1u + ((offset * 17u) % 137u), chunked_output.size() - offset);
        const auto result = arbitrary_chunks.render(
            std::span<StereoSample>(chunked_output).subspan(offset, count));
        require(result.ok() && result.rendered_samples == count,
                "reference SPU render accepts arbitrary bounded chunks");
        offset += count;
    }
    require(fixed_output == chunked_output &&
                fixed_updates.deterministic_digest() ==
                    arbitrary_chunks.deterministic_digest(),
            "PAL x2/CPU retirement multiplier cannot alter SPU count or pitch");
}

} // namespace

int main() {
    test_adpcm_vectors_and_loop_edges();
    test_gaussian_fractional_phases();
    test_irq_and_mmio_boundaries();
    test_adsr_sweep_noise_and_modulation();
    test_mix_capture_and_input_fifos();
    test_snapshot_chunking_and_long_replay();
    test_fixed_100hz_integration_contract();
    std::cout << "clean_aot_spu tests passed\n";
    return 0;
}
