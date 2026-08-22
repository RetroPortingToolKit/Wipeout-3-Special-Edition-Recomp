#include "clean_aot/spu/spu.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>

namespace {

using clean_aot::spu::Device;
using clean_aot::spu::MmioWidth;
using clean_aot::spu::StereoSample;

constexpr MmioWidth kWord = MmioWidth::Bits16;

void put(Device& device, std::uint32_t offset, std::uint16_t value) {
    const auto result = device.write_mmio(offset, value, kWord);
    if (!result.ok()) {
        std::cerr << "benchmark setup MMIO failure\n";
        std::terminate();
    }
}

void load_block(Device& device) {
    std::array<std::uint16_t, 8u> words{};
    words[0] = 0x0000u;
    for (std::size_t index = 1u; index < words.size(); ++index) {
        words[index] = 0x7777u;
    }
    put(device, Device::TransferControl, 0x0004u);
    put(device, Device::TransferAddress, 0u);
    put(device, Device::Control,
        static_cast<std::uint16_t>(Device::ControlEnable |
                                   Device::ControlMute | 0x0020u));
    std::array<StereoSample, 1u> apply_mode{};
    if (!device.render(apply_mode).ok()) {
        std::cerr << "benchmark setup mode-apply failure\n";
        std::terminate();
    }
    const auto result = device.dma_write(words);
    if (!result.ok()) {
        std::cerr << "benchmark setup DMA failure\n";
        std::terminate();
    }
    put(device, Device::Control,
        static_cast<std::uint16_t>(Device::ControlEnable |
                                   Device::ControlMute));
}

} // namespace

int main() {
    Device device;
    load_block(device);
    put(device, Device::MainVolume, 0x3FFFu);
    put(device, Device::MainVolume + 2u, 0x3FFFu);
    put(device, Device::ReverbOutputVolume, 0x2800u);
    put(device, Device::ReverbOutputVolume + 2u, 0x2800u);
    put(device, Device::ReverbBase, 0x7000u);
    put(device, Device::TransferControl, 0x0004u);
    const std::uint16_t reverb_values[32u] = {
        0x0020u, 0x0018u, 0x5000u, 0x3000u, 0x2800u, 0x2000u, 0x1800u,
        0x4000u, 0x2800u, 0x2000u, 0x0040u, 0x0050u, 0x0080u, 0x0090u,
        0x00A0u, 0x00B0u, 0x0060u, 0x0070u, 0x00C0u, 0x00D0u, 0x00E0u,
        0x00F0u, 0x0100u, 0x0110u, 0x0120u, 0x0130u, 0x0140u, 0x0150u,
        0x0160u, 0x0170u, 0x2800u, 0x2800u,
    };
    for (std::size_t index = 0u; index < 32u; ++index) {
        put(device, Device::ReverbRegisterBase +
                       static_cast<std::uint32_t>(index * 2u),
            reverb_values[index]);
    }
    for (std::size_t index = 0u; index < 24u; ++index) {
        const std::uint32_t base = static_cast<std::uint32_t>(index * 0x10u);
        put(device, base, 0x3FFFu);
        put(device, base + 2u, 0x3FFFu);
        put(device, base + 4u, 0x1000u);
        put(device, base + 6u, 0u);
        put(device, base + 8u, 0x000Fu);
        put(device, base + 10u, 0u);
        put(device, base + 14u, 0u);
    }
    put(device, Device::ReverbEnable, 0xFFFFu);
    put(device, Device::ReverbEnable + 2u, 0x00FFu);
    put(device, Device::Control,
        static_cast<std::uint16_t>(Device::ControlEnable |
                                   Device::ControlMute |
                                   Device::ControlReverbEnable));
    put(device, Device::KeyOn, 0xFFFFu);
    put(device, Device::KeyOn + 2u, 0x00FFu);
    for (std::size_t index = 0u; index < 24u; ++index) {
        put(device, static_cast<std::uint32_t>(index * 0x10u + 12u),
            0x7FFFu);
    }

    constexpr std::size_t sample_count = 1'000'000u;
    std::array<StereoSample, 8192u> output{};
    std::uint64_t checksum = 0u;
    std::size_t rendered = 0u;
    const auto begin = std::chrono::steady_clock::now();
    while (rendered < sample_count) {
        const std::size_t count = std::min(output.size(), sample_count - rendered);
        const auto result = device.render(
            std::span<StereoSample>(output.data(), count));
        if (!result.ok()) {
            std::cerr << "benchmark render failure\n";
            return 1;
        }
        for (std::size_t index = 0u; index < count; ++index) {
            checksum = checksum * 0x9E3779B185EBCA87ull +
                       static_cast<std::uint16_t>(output[index].left) +
                       (static_cast<std::uint64_t>(
                            static_cast<std::uint16_t>(output[index].right))
                        << 16u);
        }
        rendered += count;
    }
    const auto end = std::chrono::steady_clock::now();
    const std::uint64_t elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
            .count());
    const std::uint64_t samples_per_second = elapsed_ns == 0u
                                                  ? 0u
                                                  : (static_cast<std::uint64_t>(
                                                         sample_count) *
                                                     1'000'000'000ull) /
                                                        elapsed_ns;
    const std::uint64_t nanoseconds_per_sample =
        sample_count == 0u ? 0u : elapsed_ns / sample_count;
    std::cout << "PERF samples=" << sample_count << " elapsed_ns="
              << elapsed_ns << " samples_per_second=" << samples_per_second
              << " ns_per_sample=" << nanoseconds_per_sample << " checksum=0x"
              << std::hex << checksum << std::dec << '\n';
    return 0;
}
