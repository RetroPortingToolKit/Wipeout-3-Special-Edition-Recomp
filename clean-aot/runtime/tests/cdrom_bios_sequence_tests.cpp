#include "clean_aot/runtime/runtime.hpp"

#include "cdrom.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {

using clean_aot::cdrom::Cdrom;
using clean_aot::cdrom::Command;
using clean_aot::cdrom::CommandLatencyCycles;
using clean_aot::cdrom::DiscLayout;
using clean_aot::cdrom::Interrupt;
using clean_aot::cdrom::RawSector;
using clean_aot::cdrom::RawSectorSource;
using clean_aot::cdrom::SingleSpeedSectorCycles;
using clean_aot::cdrom::Track;
using clean_aot::cdrom::TrackKind;
using clean_aot::runtime::AccessContext;
using clean_aot::runtime::Configuration;
using clean_aot::runtime::Runtime;

constexpr std::uint32_t kCdrom = 0x1f80'1800U;
constexpr std::uint32_t kIstat = 0x1f80'1070U;
constexpr std::uint32_t kImask = 0x1f80'1074U;
constexpr std::uint32_t kDpcr = 0x1f80'10f0U;
constexpr std::uint32_t kDma3Madr = 0x1f80'10b0U;
constexpr std::uint32_t kDma3Bcr = 0x1f80'10b4U;
constexpr std::uint32_t kDma3Chcr = 0x1f80'10b8U;
constexpr std::uint32_t kDiscInterrupt = 1U << 2U;
constexpr std::uint32_t kDestination = 0x0000'1000U;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct VerifiedSectorSource final : RawSectorSource {
    RawSector sector{};
    std::uint32_t reads = 0U;

    bool read_raw_sector(clean_aot::cdrom::Lba lba,
                         RawSector& destination) noexcept override {
        ++reads;
        if (lba != 0U) {
            return false;
        }
        destination = sector;
        return true;
    }
};

RawSector make_mode_one_sector() {
    RawSector raw{};
    raw.bytes[0U] = 0x00U;
    for (std::size_t index = 1U; index < 11U; ++index) {
        raw.bytes[index] = 0xffU;
    }
    raw.bytes[11U] = 0x00U;
    raw.bytes[12U] = 0x00U;
    raw.bytes[13U] = 0x02U;
    raw.bytes[14U] = 0x00U;
    raw.bytes[15U] = 0x01U;
    for (std::size_t index = 0U; index < 2048U; ++index) {
        raw.bytes[16U + index] = static_cast<std::uint8_t>(index);
    }
    const std::uint32_t checksum = Cdrom::edc(raw.bytes.data(), 2064U);
    for (unsigned byte = 0U; byte < 4U; ++byte) {
        raw.bytes[2064U + byte] =
            static_cast<std::uint8_t>(checksum >> (byte * 8U));
    }
    return raw;
}

DiscLayout make_layout() {
    DiscLayout layout{};
    layout.tracks[0U] = Track{1U, 0U, TrackKind::Data};
    layout.track_count = 1U;
    layout.lead_out_lba = 1U;
    layout.disc_type = 0x20U;
    layout.license_region = {'S', 'C', 'E', 'E'};
    return layout;
}

void write8(Runtime& runtime, std::uint32_t address, std::uint8_t value) {
    require(runtime.write8(AccessContext{runtime.current_retirement_cycle()},
                           address, value).ok(),
            "8-bit MMIO write");
}

std::uint8_t read8(Runtime& runtime, std::uint32_t address) {
    const auto result = runtime.read8(
        AccessContext{runtime.current_retirement_cycle()}, address);
    require(result.ok(), "8-bit MMIO read");
    return static_cast<std::uint8_t>(result.value);
}

void write32(Runtime& runtime, std::uint32_t address, std::uint32_t value) {
    require(runtime.write32(AccessContext{runtime.current_retirement_cycle()},
                            address, value).ok(),
            "32-bit MMIO write");
}

std::uint32_t read32(Runtime& runtime, std::uint32_t address) {
    const auto result = runtime.read32(
        AccessContext{runtime.current_retirement_cycle()}, address);
    require(result.ok(), "32-bit MMIO read");
    return result.value;
}

void advance(Runtime& runtime, std::uint64_t delta) {
    require(runtime.advance_to_retirement_cycle(
                runtime.current_retirement_cycle() + delta).ok(),
            "exact device deadline advance");
}

void select_bank(Runtime& runtime, std::uint8_t bank) {
    write8(runtime, kCdrom, bank);
}

void acknowledge_cd(Runtime& runtime, std::uint8_t reason) {
    select_bank(runtime, 1U);
    write8(runtime, kCdrom + 3U, reason);
    select_bank(runtime, 0U);
    write32(runtime, kIstat, ~kDiscInterrupt);
}

void issue(Runtime& runtime, Command command,
           std::span<const std::uint8_t> parameters = {}) {
    select_bank(runtime, 0U);
    for (const std::uint8_t parameter : parameters) {
        write8(runtime, kCdrom + 2U, parameter);
    }
    write8(runtime, kCdrom + 1U, static_cast<std::uint8_t>(command));
    advance(runtime, CommandLatencyCycles);
}

std::uint8_t interrupt_reason(Runtime& runtime) {
    select_bank(runtime, 1U);
    const std::uint8_t reason =
        static_cast<std::uint8_t>(read8(runtime, kCdrom + 3U) & 0x07U);
    select_bank(runtime, 0U);
    return reason;
}

void test_bios_shell_read_sequence() {
    static const std::vector<std::uint8_t> bios(
        clean_aot::memory::kBiosBytes, 0U);
    VerifiedSectorSource source{};
    source.sector = make_mode_one_sector();
    const DiscLayout layout = make_layout();
    Configuration configuration{};
    configuration.bios = bios;
    configuration.cpu_multiplier_numerator = 1U;
    configuration.cpu_multiplier_denominator = 1U;
    configuration.disc_source = &source;
    configuration.disc_layout = &layout;
    Runtime runtime(configuration);
    require(runtime.configured(), "runtime with mounted verified media");

    write32(runtime, kImask, kDiscInterrupt);
    select_bank(runtime, 1U);
    write8(runtime, kCdrom + 2U, 0x1fU);
    select_bank(runtime, 0U);
    require(read8(runtime, kCdrom) == 0x18U, "reset HSTS FIFO bits");

    issue(runtime, Command::Getstat);
    require(interrupt_reason(runtime) ==
                static_cast<std::uint8_t>(Interrupt::Acknowledge) &&
                read8(runtime, kCdrom + 1U) == 0U,
            "Getstat sees closed shell and stopped mounted disc");
    require((read32(runtime, kIstat) & kDiscInterrupt) != 0U &&
                runtime.cpu_interrupt_line(),
            "CD IRQ is latched through I_STAT and gated by I_MASK");
    acknowledge_cd(runtime, static_cast<std::uint8_t>(Interrupt::Acknowledge));
    require((read32(runtime, kIstat) & kDiscInterrupt) == 0U &&
                !runtime.cpu_interrupt_line(),
            "CD and interrupt-controller acknowledgements lower the CPU line");

    issue(runtime, Command::ID);
    require(interrupt_reason(runtime) ==
                static_cast<std::uint8_t>(Interrupt::Acknowledge),
            "GetID first response");
    (void)read8(runtime, kCdrom + 1U);
    acknowledge_cd(runtime, static_cast<std::uint8_t>(Interrupt::Acknowledge));
    advance(runtime, CommandLatencyCycles);
    require(interrupt_reason(runtime) ==
                static_cast<std::uint8_t>(Interrupt::Complete),
            "GetID licensed completion");
    std::array<std::uint8_t, 8U> identity{};
    for (std::uint8_t& byte : identity) {
        byte = read8(runtime, kCdrom + 1U);
    }
    require(identity == std::array<std::uint8_t, 8U>{
                            0U, 0U, 0x20U, 0U, 'S', 'C', 'E', 'E'},
            "GetID reports authenticated licensed data track");
    acknowledge_cd(runtime, static_cast<std::uint8_t>(Interrupt::Complete));

    constexpr std::array<std::uint8_t, 3U> location{0x00U, 0x02U, 0x00U};
    issue(runtime, Command::Setloc, location);
    require(interrupt_reason(runtime) ==
                static_cast<std::uint8_t>(Interrupt::Acknowledge),
            "Setloc acknowledge");
    (void)read8(runtime, kCdrom + 1U);
    acknowledge_cd(runtime, static_cast<std::uint8_t>(Interrupt::Acknowledge));

    issue(runtime, Command::ReadN);
    require(interrupt_reason(runtime) ==
                static_cast<std::uint8_t>(Interrupt::Acknowledge),
            "ReadN acknowledge");
    (void)read8(runtime, kCdrom + 1U);
    acknowledge_cd(runtime, static_cast<std::uint8_t>(Interrupt::Acknowledge));
    advance(runtime, SingleSpeedSectorCycles);
    require(source.reads == 1U && interrupt_reason(runtime) ==
                                      static_cast<std::uint8_t>(Interrupt::DataReady),
            "ReadN fetches one raw sector and raises INT1");
    (void)read8(runtime, kCdrom + 1U);

    write32(runtime, kDpcr, 0x0000'8000U);
    write32(runtime, kDma3Madr, kDestination);
    write32(runtime, kDma3Bcr, 0x0001'0200U);
    write32(runtime, kDma3Chcr, 0x0100'0200U);
    write8(runtime, kCdrom + 3U, 0x80U);
    advance(runtime, 512U);
    const auto first = runtime.read32(
        AccessContext{runtime.current_retirement_cycle()}, kDestination);
    const auto last = runtime.read32(
        AccessContext{runtime.current_retirement_cycle()},
        kDestination + 2044U);
    require(first.ok() && first.value == 0x0302'0100U && last.ok() &&
                last.value == 0xfffe'fdfcU,
            "CD channel 3 DRQ transfers exactly one 2048-byte payload");
}

} // namespace

int main() {
    test_bios_shell_read_sequence();
    std::cout << "CD-ROM BIOS sequence tests passed\n";
    return EXIT_SUCCESS;
}
