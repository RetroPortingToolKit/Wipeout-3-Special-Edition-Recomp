#include "clean_aot/spu/spu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using clean_aot::spu::AdpcmSamplesPerBlock;
using clean_aot::spu::AudioFifoCapacity;
using clean_aot::spu::AudioSource;
using clean_aot::spu::Device;
using clean_aot::spu::DmaFault;
using clean_aot::spu::MmioFault;
using clean_aot::spu::MmioWidth;
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
    put(device, Device::Control, static_cast<std::uint16_t>(kLive | kDmaWrite));
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
    configure_voice(device, 0u);
    put(device, Device::KeyOn, 1u);
    put(device, 12u, 0x7FFFu);
}

void test_adpcm_vectors_and_loop_edges() {
    constexpr std::array<std::int8_t, AdpcmSamplesPerBlock> nibbles = {
        1, -2, 3, -4, 5, -6, 7, -8,
        1, -2, 3, -4, 5, -6, 7, -8,
        1, -2, 3, -4, 5, -6, 7, -8,
        1, -2, 3, -4,
    };
    const auto block = make_block(0u, 0u, nibbles);
    static Device device;
    device.reset();
    prepare_device(device, block);
    std::array<StereoSample, 8u> output{};
    require(device.render(output).ok(), "render ADPCM vector");
    constexpr std::array<StereoSample, 8u> expected = {
        StereoSample{-1, -1},      StereoSample{605, 605},
        StereoSample{1545, 1545},  StereoSample{-3315, -3315},
        StereoSample{4969, 4969},  StereoSample{-6636, -6636},
        StereoSample{8289, 8289},  StereoSample{-9956, -9956},
    };
    for (std::size_t index = 0u; index < output.size(); ++index) {
        require(output[index].left == expected[index].left &&
                    output[index].right == expected[index].right,
                "independent ADPCM/interpolation vector");
    }

    static Device looped;
    looped.reset();
    prepare_device(looped, make_block(0u, 0x07u, nibbles));
    std::array<StereoSample, 29u> loop_output{};
    require(looped.render(loop_output).ok(), "render loop-end block");
    const auto endx = looped.read_mmio(Device::EndFlags, kWord);
    require(endx.ok() && (endx.value & 1u) != 0u,
            "loop-end sets ENDX");
    require(looped.voice_state(0u).active, "end-repeat keeps voice active");

    static Device muted;
    muted.reset();
    prepare_device(muted, make_block(0u, 0x01u, nibbles));
    std::array<StereoSample, 29u> muted_output{};
    require(muted.render(muted_output).ok(), "render end-mute block");
    require(muted.voice_state(0u).active &&
                muted.voice_state(0u).adsr_volume == 0,
            "end-mute releases and silences voice while scanning");

    static Device loop_start;
    loop_start.reset();
    prepare_device(loop_start, make_block(0u, 0x04u, nibbles));
    std::array<StereoSample, 1u> one{};
    require(loop_start.render(one).ok(), "render loop-start block");
    require(loop_start.voice_state(0u).repeat_address_units == 0u,
            "loop-start copies current address to repeat register");
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

    put(device, Device::TransferControl, 0xFFFFu);
    const auto transfer_control = device.read_mmio(Device::TransferControl, kWord);
    require(transfer_control.ok() && transfer_control.value == 0x000Eu,
            "transfer control masks reserved bits");
    require(device.read_mmio(Device::TransferFifo, kWord).fault ==
                MmioFault::TransferFifoUnderflow,
            "empty transfer FIFO is explicit");
    put(device, Device::TransferControl, kNormalTransfer);
    put(device, Device::TransferAddress, 0u);
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        const auto result = device.write_mmio(
            Device::TransferFifo, index + 1u, kWord);
        require(result.ok(), "fill 32-entry transfer FIFO");
    }
    require(device.write_mmio(Device::TransferFifo, 0xFFFFu, kWord).fault ==
                MmioFault::TransferFifoOverflow,
            "31st extra transfer FIFO write is rejected");
    put(device, Device::Control, static_cast<std::uint16_t>(kLive | 0x10u));
    const auto ram = device.sound_ram();
    require(ram[0u] == 1u && ram[1u] == 0u && ram[62u] == 32u,
            "manual transfer drains FIFO in order");

    static Device fifo_wrap;
    fifo_wrap.reset();
    put(fifo_wrap, Device::TransferControl, kNormalTransfer);
    put(fifo_wrap, Device::TransferAddress, 0u);
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        put(fifo_wrap, Device::TransferFifo,
            static_cast<std::uint16_t>(index + 1u));
    }
    put(fifo_wrap, Device::Control,
        static_cast<std::uint16_t>(kLive | kDmaRead));
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
    const auto wrapped_ram = fifo_wrap.sound_ram();
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        const std::uint16_t expected = static_cast<std::uint16_t>(index + 5u);
        const std::uint16_t actual = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(wrapped_ram[index * 2u]) |
            (static_cast<std::uint16_t>(wrapped_ram[index * 2u + 1u]) << 8u));
        require(actual == expected, "wrapped transfer FIFO preserves order");
    }

    const auto block = make_constant_block(2);
    static Device dma;
    dma.reset();
    put(dma, Device::TransferControl, kNormalTransfer);
    put(dma, Device::TransferAddress, 0xFFFFu);
    put(dma, Device::Control, static_cast<std::uint16_t>(kLive | kDmaWrite));
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
    put(dma, Device::TransferAddress, 0xFFFFu);
    put(dma, Device::Control, static_cast<std::uint16_t>(kLive | kDmaRead));
    std::array<std::uint16_t, 2u> read_words{};
    require(dma.dma_read(read_words).ok() && read_words == words,
            "bounded DMA read");
    require(dma.dma_write(words).fault == DmaFault::WrongTransferMode,
            "DMA write rejects read mode");

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
    put(capture, Device::IrqAddress, 0u);
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
    require(captured_ram[0u] == 0xD2u && captured_ram[1u] == 0x04u &&
                captured_ram[0x400u] == 0xD7u && captured_ram[0x401u] == 0xF6u,
            "CD capture writes raw stereo samples");
    std::array<StereoSample, 256u> half{};
    require(capture.render(half).ok(), "capture second half boundary");
    const auto status = capture.read_mmio(Device::Status, kWord);
    require(status.ok() && (status.value & Device::StatusCaptureSecondHalf) != 0u,
            "capture status exposes second half");
    require(capture_irq.calls > 0u, "capture crossing signals IRQ");

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
    std::array<StereoSample, AudioFifoCapacity + 1u> fifo_output{};
    require(fifo.render(fifo_output).ok(), "CDDA FIFO consumption");
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

void test_snapshot_chunking_and_long_replay() {
    const auto block = make_constant_block(3, 0x03u);
    static Device first;
    first.reset();
    prepare_device(first, block);
    DmaCollector collector;
    put(first, Device::Control, static_cast<std::uint16_t>(kLive | kDmaRead));
    put(first, Device::TransferAddress, 0u);
    require(first.dma_read(DmaCollector::collect, &collector, 64u).ok() &&
                collector.calls == 2u && collector.words == 64u,
            "typed bounded DMA callback");

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
                replay.deterministic_digest() == saved_digest,
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

} // namespace

int main() {
    test_adpcm_vectors_and_loop_edges();
    test_irq_and_mmio_boundaries();
    test_adsr_sweep_noise_and_modulation();
    test_mix_capture_and_input_fifos();
    test_snapshot_chunking_and_long_replay();
    std::cout << "clean_aot_spu tests passed\n";
    return 0;
}
