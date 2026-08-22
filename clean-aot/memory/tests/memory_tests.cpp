#include "clean_aot/memory/bus.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using clean_aot::memory::AccessOrigin;
using clean_aot::memory::AccessStatus;
using clean_aot::memory::AccessWidth;
using clean_aot::memory::Bus;
using clean_aot::memory::BusConfig;
using clean_aot::memory::CallbackMmioRouter;
using clean_aot::memory::CodeRegionCaptureStatus;
using clean_aot::memory::CodeRegionValidity;
using clean_aot::memory::MmioReadResponse;
using clean_aot::memory::MmioRequest;
using clean_aot::memory::MmioRouter;
using clean_aot::memory::MmioStatus;
using clean_aot::memory::MmioWriteResponse;
using clean_aot::memory::ResetStatus;
using clean_aot::memory::Segment;
using clean_aot::memory::VariantIdentity;
using clean_aot::memory::kBiosBytes;
using clean_aot::memory::kBiosPhysicalBase;
using clean_aot::memory::kCodePageBytes;
using clean_aot::memory::kDirectAddressBytes;
using clean_aot::memory::kKernelCachedBase;
using clean_aot::memory::kKernelUncachedBase;
using clean_aot::memory::kMmioBytes;
using clean_aot::memory::kMmioPhysicalBase;
using clean_aot::memory::kRamBytes;
using clean_aot::memory::kRamMirrorBytes;
using clean_aot::memory::kScratchpadBytes;
using clean_aot::memory::kScratchpadPhysicalBase;
using clean_aot::memory::kMaxCodeRegionBytes;
using clean_aot::memory::kMaxCodePagesPerRegion;
using clean_aot::memory::kPhysicalAddressMask;
using clean_aot::memory::next_code_page_generation;
using clean_aot::memory::segment_address;
using clean_aot::memory::translate_address;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::abort();
    }
}

[[nodiscard]] std::uint32_t direct(
    Segment segment,
    std::uint32_t physical_address) {
    const auto result = segment_address(segment, physical_address);
    require(result.has_value(), "test address must be a valid direct segment");
    return result.value();
}

[[nodiscard]] VariantIdentity variant(std::uint8_t seed) {
    VariantIdentity identity{};
    for (std::size_t index = 0u; index < identity.digest.size(); ++index) {
        identity.digest[index] = static_cast<std::uint8_t>(
            seed + static_cast<std::uint8_t>(index * 13u));
    }
    require(identity.valid(), "test variant identity must be nonzero");
    return identity;
}

struct Fixture final {
    std::vector<std::uint8_t> bios;
    std::unique_ptr<Bus> bus;

    explicit Fixture(MmioRouter* router = nullptr)
        : bios(kBiosBytes), bus(nullptr) {
        for (std::size_t index = 0u; index < bios.size(); ++index) {
            bios[index] = static_cast<std::uint8_t>(
                (index * 29u + 7u) & 0xFFu);
        }
        bus = std::make_unique<Bus>(BusConfig{
            std::span<const std::uint8_t>(bios.data(), bios.size()), router});
        require(bus->configured(), "valid BIOS view configures the bus");
    }
};

struct CallbackState final {
    std::uint32_t read_calls = 0u;
    std::uint32_t write_calls = 0u;
    MmioRequest last_request{};
    std::uint32_t last_write_value = 0u;
    MmioStatus read_status = MmioStatus::Handled;
    MmioStatus write_status = MmioStatus::Handled;
    std::uint32_t read_value = 0xA1B2C3D4u;
};

MmioReadResponse callback_read(
    const MmioRequest& request,
    void* user) noexcept {
    auto* state = static_cast<CallbackState*>(user);
    ++state->read_calls;
    state->last_request = request;
    return {state->read_status, state->read_value};
}

MmioWriteResponse callback_write(
    const MmioRequest& request,
    std::uint32_t value,
    void* user) noexcept {
    auto* state = static_cast<CallbackState*>(user);
    ++state->write_calls;
    state->last_request = request;
    state->last_write_value = value;
    return {state->write_status};
}

void test_translation_and_mirrors() {
    const std::uint32_t physical = 0x12340u;
    const auto user = translate_address(direct(Segment::User, physical));
    const auto cached = translate_address(
        direct(Segment::KernelCached, physical));
    const auto uncached = translate_address(
        direct(Segment::KernelUncached, physical));

    require(user.ok() && cached.ok() && uncached.ok(),
            "all direct PS1 segments translate");
    require(user.physical_address == physical &&
                cached.physical_address == physical &&
                uncached.physical_address == physical,
            "segment translation preserves physical address");
    require(cached.segment == Segment::KernelCached &&
                uncached.segment == Segment::KernelUncached,
            "segment identity is retained for device-specific mirrors");

    const auto unsupported = translate_address(2u << 29u);
    require(!unsupported.ok(), "unmapped virtual segments are rejected");
    require(translate_address(kPhysicalAddressMask).ok(),
            "the final byte of the direct KUSEG window translates");
    require(!translate_address(kPhysicalAddressMask + 1u).ok() &&
                !translate_address(3u << 29u).ok() &&
                !translate_address(6u << 29u).ok() &&
                !translate_address(7u << 29u).ok(),
            "upper KUSEG and KSEG2/KSEG3 are rejected");
    require(translate_address(kKernelCachedBase + kPhysicalAddressMask).ok() &&
                translate_address(kKernelUncachedBase + kPhysicalAddressMask)
                    .ok(),
            "both direct kernel segments include their final byte");
    require(!segment_address(Segment::Unsupported, 0u).has_value() &&
                !segment_address(static_cast<Segment>(0xFFu), 0u)
                     .has_value() &&
                !segment_address(Segment::User,
                                 kPhysicalAddressMask + 1u)
                     .has_value(),
            "direct-address formation fails closed on invalid input");

    Fixture fixture;
    const std::uint32_t value = 0x10203040u;
    const std::array<Segment, 3u> segments{
        Segment::User, Segment::KernelCached, Segment::KernelUncached};
    const std::array<std::uint32_t, 4u> mirrors{
        physical,
        physical + kRamBytes,
        physical + 2u * kRamBytes,
        physical + 3u * kRamBytes,
    };

    require(fixture.bus->write32(
                direct(Segment::KernelUncached, physical), value)
                .ok(),
            "RAM write through uncached segment succeeds");
    for (const Segment segment : segments) {
        for (const std::uint32_t mirror : mirrors) {
            const auto result = fixture.bus->read32(direct(segment, mirror));
            require(result.ok() && result.value == value,
                    "RAM has exact segment and first-8MiB mirrors");
        }
    }

    const std::uint32_t lower_half = 0x400u;
    const std::uint32_t upper_half = lower_half + (kRamBytes / 2u);
    require(fixture.bus->write32(lower_half, 0x11112222u).ok() &&
                fixture.bus->write32(upper_half, 0x33334444u).ok() &&
                fixture.bus->read32(lower_half).value == 0x11112222u &&
                fixture.bus->read32(upper_half).value == 0x33334444u &&
                fixture.bus->read32(lower_half + 3u * kRamBytes).value ==
                    0x11112222u &&
                fixture.bus->read32(upper_half + 2u * kRamBytes).value ==
                    0x33334444u,
            "RAM has two distinct megabytes before the eight-megabyte mirror");

    const auto generation_a = fixture.bus->ram_page_generation(physical);
    const auto generation_b = fixture.bus->ram_page_generation(
        direct(Segment::KernelCached, physical + kRamBytes));
    require(generation_a.has_value() && generation_b.has_value() &&
                generation_a == generation_b,
            "all RAM mirrors share one code-page generation");
    require(kDirectAddressBytes > kRamMirrorBytes,
            "RAM mirror window remains inside direct physical space");
    require(kKernelCachedBase != kKernelUncachedBase,
            "cached and uncached architectural selectors are distinct");

    const auto segment_fault = fixture.bus->read8(2u << 29u);
    require(segment_fault.status == AccessStatus::UnsupportedSegment &&
                segment_fault.fault.virtual_address == (2u << 29u),
            "unsupported virtual segments retain a distinct fault contract");
    const auto physical_fault = fixture.bus->read8(kRamMirrorBytes);
    require(physical_fault.status == AccessStatus::Unmapped &&
                physical_fault.fault.physical_address == kRamMirrorBytes,
            "valid but unmapped physical addresses survive fault reporting");
}

void test_endian_width_and_boundaries() {
    Fixture fixture;
    const std::uint32_t address = 0x1400u;
    require(fixture.bus->write32(
                direct(Segment::KernelCached, address),
                0xA1B2C3D4u)
                .ok(),
            "aligned word write succeeds");

    const auto byte0 = fixture.bus->read8(address);
    const auto byte1 = fixture.bus->read8(address + 1u);
    const auto byte2 = fixture.bus->read8(address + 2u);
    const auto byte3 = fixture.bus->read8(address + 3u);
    require(byte0.ok() && byte0.value == 0xD4u && byte1.value == 0xC3u &&
                byte2.value == 0xB2u && byte3.value == 0xA1u,
            "word bytes are little endian");
    require(fixture.bus->read16(address).value == 0xC3D4u &&
                fixture.bus->read16(address + 2u).value == 0xA1B2u,
            "halfword reads preserve little-endian lanes");

    const std::uint32_t last_ram_word = kRamMirrorBytes - 4u;
    require(fixture.bus->write32(last_ram_word, 0x55667788u).ok(),
            "last word in the eight-megabyte RAM mirror is mapped");
    require(fixture.bus->read32(last_ram_word).value == 0x55667788u,
            "RAM mirror upper boundary is inclusive for four bytes");
    require(fixture.bus->read8(kRamMirrorBytes).status == AccessStatus::Unmapped,
            "address after the RAM mirror is unmapped");
    require(fixture.bus->write8(kRamBytes - 1u, 0xA5u).ok() &&
                fixture.bus->write8(kRamBytes, 0x5Au).ok() &&
                fixture.bus->read8(kRamBytes - 1u).value == 0xA5u &&
                fixture.bus->read8(0u).value == 0x5Au,
            "the 2 MiB canonical boundary wraps only after its final byte");
    require(fixture.bus->read32(kRamMirrorBytes - 2u).status ==
                AccessStatus::AlignmentFault,
            "unaligned access is rejected before mapping");

    const std::uint32_t scratch = kScratchpadPhysicalBase +
                                  kScratchpadBytes - 4u;
    require(fixture.bus->write32(scratch, 0xCAFEBABEu).ok(),
            "scratchpad boundary word is writable");
    require(fixture.bus->read32(direct(Segment::KernelCached, scratch))
                .value == 0xCAFEBABEu,
            "scratchpad KUSEG/KSEG0 mirrors agree");
    require(fixture.bus->read8(direct(Segment::KernelUncached, scratch))
                .status == AccessStatus::Unmapped,
            "scratchpad is not visible in KSEG1");

    const std::uint32_t bios_start = kBiosPhysicalBase;
    const std::uint32_t bios_end = kBiosPhysicalBase + kBiosBytes - 4u;
    const std::uint32_t expected_bios_word =
        static_cast<std::uint32_t>(fixture.bios[0]) |
        (static_cast<std::uint32_t>(fixture.bios[1]) << 8u) |
        (static_cast<std::uint32_t>(fixture.bios[2]) << 16u) |
        (static_cast<std::uint32_t>(fixture.bios[3]) << 24u);
    require(fixture.bus->read32(bios_start).value == expected_bios_word,
            "BIOS reads use the supplied immutable view");
    require(fixture.bus->read32(direct(Segment::KernelUncached,
                                      bios_end))
                .ok(),
            "BIOS final aligned word is mapped through KSEG1");
    require(fixture.bus->read8(kBiosPhysicalBase + kBiosBytes).status ==
                AccessStatus::Unmapped,
            "BIOS boundary after the supplied view is unmapped");
    const auto before = fixture.bus->read8(bios_start);
    const auto write_bios = fixture.bus->write8(bios_start, 0u);
    require(write_bios.status == AccessStatus::ReadOnly,
            "BIOS writes are rejected deterministically");
    require(fixture.bus->read8(bios_start).value == before.value,
            "rejected BIOS writes have no side effect");
    require(fixture.bus->bios_view().data() != fixture.bios.data(),
            "BIOS storage is owned rather than borrowed");
    const std::uint8_t owned_bios_byte = fixture.bus->bios_view()[0u];
    fixture.bios[0u] ^= 0xFFu;
    require(fixture.bus->read8(bios_start).value == owned_bios_byte,
            "caller mutation cannot alter the immutable BIOS image");

    std::unique_ptr<Bus> lifetime_bus;
    std::uint8_t expected_after_owner_destruction = 0u;
    {
        std::vector<std::uint8_t> temporary_bios(kBiosBytes, 0x3Cu);
        expected_after_owner_destruction = temporary_bios[0u];
        lifetime_bus = std::make_unique<Bus>(BusConfig{
            std::span<const std::uint8_t>(temporary_bios), nullptr});
    }
    require(lifetime_bus->read8(bios_start).value ==
                expected_after_owner_destruction,
            "BIOS remains valid after caller storage is destroyed");
}

void test_faults_and_unmapped_behavior() {
    Fixture fixture;
    const std::uint32_t address = 0x2200u;
    require(fixture.bus->write32(address, 0x76543210u).ok(),
            "fault test seed write succeeds");
    const auto before = fixture.bus->read32(address);

    const auto misaligned_halfword = fixture.bus->read16(address + 1u);
    require(misaligned_halfword.status == AccessStatus::AlignmentFault &&
                misaligned_halfword.fault.type ==
                    clean_aot::memory::AccessType::Read &&
                misaligned_halfword.fault.width == AccessWidth::Halfword &&
                misaligned_halfword.fault.virtual_address == address + 1u,
            "halfword misalignment is reported");
    require(fixture.bus->read32(address + 2u).status ==
                AccessStatus::AlignmentFault,
            "word misalignment is reported");
    require(fixture.bus->write16(address + 1u, 0u).status ==
                AccessStatus::AlignmentFault,
            "misaligned writes are rejected without mutation");
    require(fixture.bus->read32(address).value == before.value,
            "misaligned writes do not change RAM");

    const auto edge_alignment = fixture.bus->read32(
        std::numeric_limits<std::uint32_t>::max() - 2u);
    require(edge_alignment.status == AccessStatus::AlignmentFault,
            "near-wrap unaligned access still reports alignment first");
    const auto unsupported = fixture.bus->read32(2u << 29u);
    require(unsupported.status == AccessStatus::UnsupportedSegment &&
                unsupported.value == 0u,
            "unsupported-segment reads return a deterministic zero value");
    require(fixture.bus->write32(2u << 29u, 1u).status ==
                AccessStatus::UnsupportedSegment,
            "unsupported-segment writes are deterministic no-ops");
    for (std::uint32_t raw = 0u; raw <= 0xFFu; ++raw) {
        const auto width = static_cast<AccessWidth>(raw);
        const bool valid = raw == 1u || raw == 2u || raw == 4u;
        const auto result = fixture.bus->read(address, width);
        require(valid ? result.ok()
                      : result.status == AccessStatus::InvalidWidth,
                "every invalid width enumerator fails closed");
    }
    for (std::uint32_t raw = 0u; raw <= 0xFFu; ++raw) {
        const auto origin = static_cast<AccessOrigin>(raw);
        const bool valid = raw == static_cast<std::uint32_t>(AccessOrigin::Cpu) ||
                           raw == static_cast<std::uint32_t>(AccessOrigin::Dma);
        const auto result = fixture.bus->write8(address, 0xFFu, origin);
        require(valid ? result.ok()
                      : result.status == AccessStatus::InvalidOrigin,
                "every invalid origin enumerator fails closed");
    }
    require(fixture.bus->read32(address).value == 0x765432FFu,
            "enum fuzzing performs only the two deliberate valid byte writes");

    std::vector<std::uint8_t> short_bios(kBiosBytes - 1u, 0u);
    auto invalid = std::make_unique<Bus>(
        BusConfig{std::span<const std::uint8_t>(short_bios), nullptr});
    require(!invalid->configured(), "wrong BIOS size makes the bus invalid");
    require(invalid->read8(0u).status == AccessStatus::InvalidConfiguration,
            "invalid configuration fails accesses deterministically");
    require(invalid->reset_mutable_state() ==
                ResetStatus::InvalidConfiguration,
            "invalid configuration cannot be reset into validity");
}

void test_mmio_callbacks_and_dma_origin() {
    CallbackState state{};
    CallbackMmioRouter router(callback_read, callback_write, &state);
    Fixture fixture(&router);
    const std::uint32_t address = kMmioPhysicalBase + 0x20u;

    const auto read = fixture.bus->read16(address, AccessOrigin::Dma);
    require(read.ok() && read.value == 0xC3D4u,
            "typed MMIO read is width-masked and endian-neutral");
    require(state.read_calls == 1u &&
                state.last_request.width == AccessWidth::Halfword &&
                state.last_request.origin == AccessOrigin::Dma &&
                state.last_request.physical_address == address,
            "MMIO receives physical address, width, and DMA origin");
    const std::uint32_t uncached_alias =
        direct(Segment::KernelUncached, address);
    require(fixture.bus->read16(uncached_alias).ok() &&
                state.last_request.virtual_address == uncached_alias &&
                state.last_request.physical_address == address &&
                state.last_request.origin == AccessOrigin::Cpu,
            "CPU MMIO preserves virtual identity while normalizing physical");

    const std::uint32_t final_mmio_word =
        kMmioPhysicalBase + kMmioBytes - 4u;
    require(fixture.bus->read32(final_mmio_word).ok(),
            "the final aligned hardware-aperture word is routed");
    const std::uint32_t calls_before_bios = state.read_calls;
    require(fixture.bus->read8(kMmioPhysicalBase + kMmioBytes).ok() &&
                state.read_calls == calls_before_bios,
            "the byte after the hardware aperture is the BIOS, not MMIO");

    const std::uint32_t expansion_one =
        kScratchpadPhysicalBase - (8u << 20u);
    const std::uint32_t calls_before_unmapped = state.read_calls;
    require(fixture.bus->read8(expansion_one).status ==
                AccessStatus::Unmapped &&
                fixture.bus->read8(kScratchpadPhysicalBase +
                                   kScratchpadBytes)
                        .status == AccessStatus::Unmapped &&
                state.read_calls == calls_before_unmapped,
            "expansion-one and the scratchpad gap remain unmapped");

    state.read_value = 0xFEDCBA98u;
    require(fixture.bus->read8(address).value == 0x98u &&
                fixture.bus->read16(address).value == 0xBA98u &&
                fixture.bus->read32(address).value == 0xFEDCBA98u,
            "all callback widths are masked at the bus boundary");

    const auto write = fixture.bus->write16(
        direct(Segment::User, address + 2u),
        0xABCDu,
        AccessOrigin::Dma);
    require(write.ok() && state.write_calls == 1u &&
                state.last_write_value == 0xABCDu &&
                state.last_request.origin == AccessOrigin::Dma,
            "typed MMIO write receives a masked DMA value");
    require(fixture.bus->write(
                address,
                AccessWidth::Byte,
                0xFFFFFF5Au,
                AccessOrigin::Cpu)
                .ok() &&
                state.last_write_value == 0x5Au,
            "partial MMIO writes cannot leak high value lanes");

    require(fixture.bus->read8(
                direct(Segment::KernelUncached, address),
                AccessOrigin::Dma)
                .status == AccessStatus::OriginNotSupported,
            "DMA callers cannot smuggle virtual cache aliases into the bus");
    require(fixture.bus->write8(kScratchpadPhysicalBase,
                                1u,
                                AccessOrigin::Dma)
                .status == AccessStatus::OriginNotSupported,
            "DMA cannot access CPU scratchpad storage");

    state.read_status = MmioStatus::Rejected;
    require(fixture.bus->read8(address).status == AccessStatus::MmioRejected,
            "MMIO rejection becomes a deterministic access status");
    state.write_status = MmioStatus::Unmapped;
    require(fixture.bus->write8(address, 1u).status == AccessStatus::Unmapped,
            "MMIO unmapped response is preserved");
    state.read_status = static_cast<MmioStatus>(0xFFu);
    require(fixture.bus->read8(address).status == AccessStatus::MmioRejected,
            "invalid router status values fail closed as rejection");

    Fixture without_router;
    require(without_router.bus->read8(address).status ==
                AccessStatus::MmioUnavailable &&
                without_router.bus->write8(address, 1u).status ==
                    AccessStatus::MmioUnavailable,
            "missing MMIO router is explicit, not a hidden fallback");

    CallbackMmioRouter null_callbacks(nullptr, nullptr);
    Fixture with_null_callbacks(&null_callbacks);
    require(with_null_callbacks.bus->read8(address).status ==
                AccessStatus::Unmapped &&
                with_null_callbacks.bus->write8(address, 1u).status ==
                    AccessStatus::Unmapped,
            "null callback adapters remain explicit unmapped devices");
}

void test_code_page_invalidation() {
    Fixture fixture;
    const std::uint32_t start = 0x5000u;
    const VariantIdentity variant_a = variant(0x11u);
    const VariantIdentity variant_b = variant(0x42u);
    require(next_code_page_generation(
                std::numeric_limits<std::uint64_t>::max() - 1u)
                .value() == std::numeric_limits<std::uint64_t>::max() &&
                !next_code_page_generation(
                     std::numeric_limits<std::uint64_t>::max())
                     .has_value(),
            "generation arithmetic reaches max once and never wraps");
    const auto capture =
        fixture.bus->capture_code_region(start, 32u, variant_a);
    require(capture.status == CodeRegionCaptureStatus::Ok,
            "RAM code region can be stamped");
    require(fixture.bus->validate_code_region(capture.stamp, variant_a) ==
                CodeRegionValidity::Current,
            "fresh region stamp is current");
    require(fixture.bus->validate_code_region(capture.stamp, variant_b) ==
                CodeRegionValidity::VariantMismatch,
            "catalog variant mismatch blocks dispatch");
    require(fixture.bus->capture_code_region(
                start, 32u, VariantIdentity{})
                .status == CodeRegionCaptureStatus::InvalidVariant,
            "an unselected all-zero catalog identity cannot be captured");

    require(fixture.bus->write8(
                direct(Segment::KernelUncached, start + 4u),
                0x99u)
                .ok(),
            "CPU RAM write succeeds");
    require(fixture.bus->validate_code_region(capture.stamp, variant_a) ==
                CodeRegionValidity::Stale,
            "CPU writes invalidate precompiled regions through an alias");

    const auto variant_b_epoch =
        fixture.bus->capture_code_region(start, 32u, variant_b);
    require(fixture.bus->validate_code_region(variant_b_epoch.stamp,
                                              variant_b) ==
                CodeRegionValidity::Current,
            "the B image can select its own immutable catalog identity");
    require(fixture.bus->write8(start + 4u, 0u).ok(),
            "the candidate bytes can return from B to A");
    require(fixture.bus->validate_code_region(capture.stamp, variant_a) ==
                CodeRegionValidity::Stale &&
                fixture.bus->validate_code_region(variant_b_epoch.stamp,
                                                  variant_b) ==
                    CodeRegionValidity::Stale,
            "A-to-B-to-A never revives either earlier execution epoch");
    const auto refreshed =
        fixture.bus->capture_code_region(start, 32u, variant_a);
    require(fixture.bus->validate_code_region(refreshed.stamp, variant_a) ==
                CodeRegionValidity::Current,
            "returning to A requires a newly captured epoch");

    auto malformed = refreshed.stamp;
    malformed.pages[0].page_index += 1u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "validation rejects a page list that does not match the range");
    malformed = refreshed.stamp;
    malformed.page_count = static_cast<std::uint16_t>(
        malformed.page_count + 1u);
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "validation rejects forged page counts before variant dispatch");
    malformed = refreshed.stamp;
    malformed.bus_domain = 0u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "zero stamp domains are structurally invalid");
    malformed = refreshed.stamp;
    malformed.reset_epoch = 0u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "zero reset epochs are structurally invalid");
    malformed = refreshed.stamp;
    malformed.byte_count = 0u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "zero-length stamps cannot enter dispatch validation");
    malformed = refreshed.stamp;
    malformed.page_count = 0u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "empty page snapshots cannot enter dispatch validation");
    malformed = refreshed.stamp;
    malformed.byte_count = kMaxCodeRegionBytes + 1u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "oversize forged stamps are invalid");
    malformed = refreshed.stamp;
    malformed.page_count = static_cast<std::uint16_t>(
        kMaxCodePagesPerRegion + 1u);
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "oversize forged page arrays are invalid");
    malformed = refreshed.stamp;
    malformed.variant = VariantIdentity{};
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid &&
                fixture.bus->validate_code_region(refreshed.stamp,
                                                  VariantIdentity{}) ==
                    CodeRegionValidity::Invalid,
            "zero selected or expected identities are invalid");
    malformed = refreshed.stamp;
    malformed.virtual_start =
        std::numeric_limits<std::uint32_t>::max() - 3u;
    malformed.byte_count = 5u;
    require(fixture.bus->validate_code_region(malformed, variant_a) ==
                CodeRegionValidity::Invalid,
            "overflowed forged stamp ranges are invalid");

    require(fixture.bus->write32(
                direct(Segment::User, start + kRamBytes),
                0x01020304u,
                AccessOrigin::Dma)
                .ok(),
            "DMA RAM write uses the same bounded bus");
    require(fixture.bus->validate_code_region(refreshed.stamp, variant_a) ==
                CodeRegionValidity::Stale,
            "DMA writes also invalidate code pages");

    const auto cross_page = fixture.bus->capture_code_region(
        kCodePageBytes - 2u, 4u, variant_a);
    require(cross_page.ok() && cross_page.stamp.page_count == 2u,
            "a region crossing a page records both canonical pages");
    const auto first_generation = fixture.bus->ram_page_generation(0u);
    const auto second_generation = fixture.bus->ram_page_generation(kCodePageBytes);
    require(first_generation.has_value() && second_generation.has_value(),
            "cross-page generations are queryable");
    require(fixture.bus->write8(kCodePageBytes - 1u, 0xEFu).ok() &&
                fixture.bus->write8(kCodePageBytes, 0xBEu).ok(),
            "writes on both sides of a page boundary succeed");
    require(fixture.bus->ram_page_generation(0u).value() ==
                first_generation.value() + 1u &&
                fixture.bus->ram_page_generation(kCodePageBytes).value() ==
                    second_generation.value() + 1u,
            "each page write advances only its canonical page generation");
    require(!fixture.bus->ram_page_generation(kScratchpadPhysicalBase)
                 .has_value(),
            "non-RAM regions never expose code-page generations");

    const auto maximal_unaligned = fixture.bus->capture_code_region(
        1u, kMaxCodeRegionBytes, variant_a);
    require(maximal_unaligned.ok() &&
                maximal_unaligned.stamp.page_count ==
                    kMaxCodePagesPerRegion,
            "an unaligned one-megabyte range records all 257 pages");
    bool maximal_validation_current = true;
    for (std::uint32_t iteration = 0u; iteration < 2048u; ++iteration) {
        maximal_validation_current =
            maximal_validation_current &&
            fixture.bus->validate_code_region(maximal_unaligned.stamp,
                                              variant_a) ==
                CodeRegionValidity::Current;
    }
    require(maximal_validation_current,
            "repeated maximal validation remains page-bounded and current");

    const std::uint32_t mirror_wrap_start =
        kRamBytes - (kCodePageBytes / 2u);
    const auto mirror_wrap = fixture.bus->capture_code_region(
        mirror_wrap_start, kCodePageBytes, variant_a);
    require(mirror_wrap.ok() && mirror_wrap.stamp.page_count == 2u &&
                mirror_wrap.stamp.pages[0u].page_index ==
                    (kRamBytes / kCodePageBytes) - 1u &&
                mirror_wrap.stamp.pages[1u].page_index == 0u,
            "region stamps preserve canonical page order across a RAM mirror");

    Fixture other_bus;
    require(other_bus.bus->validate_code_region(mirror_wrap.stamp,
                                                variant_a) ==
                CodeRegionValidity::Stale,
            "a stamp from another Bus instance cannot be replayed");

    const std::uint32_t reset_ram = 0x7000u;
    require(fixture.bus->write8(reset_ram, 0x5Au).ok() &&
                fixture.bus->write8(kScratchpadPhysicalBase, 0xA5u).ok(),
            "reset fixture state can be populated");
    const auto before_reset = fixture.bus->capture_code_region(
        reset_ram, 4u, variant_a);
    const std::uint8_t bios_before_reset = fixture.bus->bios_view()[0u];
    require(fixture.bus->reset_mutable_state() == ResetStatus::Ok,
            "mutable machine memory resets without replacing BIOS or router");
    require(fixture.bus->ram_view()[reset_ram] == 0u &&
                fixture.bus->scratchpad_view()[0u] == 0u &&
                fixture.bus->bios_view()[0u] == bios_before_reset &&
                fixture.bus->ram_page_generation(reset_ram).value() == 0u,
            "reset clears RAM, scratchpad, and generations only");
    require(fixture.bus->validate_code_region(before_reset.stamp,
                                              variant_a) ==
                CodeRegionValidity::Stale,
            "reset invalidates every previously captured region epoch");

    const auto same_value_generation =
        fixture.bus->ram_page_generation(reset_ram).value();
    require(fixture.bus->write8(reset_ram, 0u).ok() &&
                fixture.bus->ram_page_generation(reset_ram).value() ==
                    same_value_generation + 1u,
            "same-value writes conservatively advance generation");

    require(fixture.bus->capture_code_region(
                kScratchpadPhysicalBase, 4u, variant_a)
                .status == CodeRegionCaptureStatus::NotExecutable,
            "scratchpad cannot be stamped as executable RAM");
    require(fixture.bus->capture_code_region(
                kBiosPhysicalBase, 4u, variant_a)
                .status == CodeRegionCaptureStatus::NotExecutable,
            "BIOS cannot be stamped as executable RAM");
    require(fixture.bus->capture_code_region(
                0u, 0u, variant_a)
                .status == CodeRegionCaptureStatus::InvalidArgument,
            "zero-length region stamps are rejected");
    require(fixture.bus->capture_code_region(
                0u, kMaxCodeRegionBytes + 1u, variant_a)
                .status == CodeRegionCaptureStatus::TooLarge,
            "region stamp ownership is bounded");
    require(fixture.bus->capture_code_region(
                std::numeric_limits<std::uint32_t>::max() - 3u,
                5u,
                variant_a)
                .status == CodeRegionCaptureStatus::AddressOverflow,
            "region stamp rejects address wraparound");
    require(fixture.bus->capture_code_region(
                kRamMirrorBytes - 2u, 4u, variant_a)
                .status == CodeRegionCaptureStatus::NotExecutable,
            "region stamp containment rejects a range crossing out of RAM");
    require(fixture.bus->capture_code_region(
                kPhysicalAddressMask, 2u, variant_a)
                .status == CodeRegionCaptureStatus::NotExecutable &&
                fixture.bus->capture_code_region(
                    2u << 29u, 1u, variant_a)
                        .status == CodeRegionCaptureStatus::NotExecutable &&
                fixture.bus->capture_code_region(
                    kKernelCachedBase + kPhysicalAddressMask - 3u,
                    8u,
                    variant_a)
                        .status == CodeRegionCaptureStatus::NotExecutable,
            "region stamps cannot cross virtual segment boundaries");

    std::vector<std::uint8_t> short_bios(kBiosBytes - 1u, 0u);
    auto invalid_bus = std::make_unique<Bus>(
        BusConfig{std::span<const std::uint8_t>(short_bios), nullptr});
    require(invalid_bus->capture_code_region(0u, 4u, variant_a).status ==
                CodeRegionCaptureStatus::InvalidConfiguration &&
                invalid_bus->validate_code_region(refreshed.stamp,
                                                  variant_a) ==
                    CodeRegionValidity::Invalid,
            "invalid buses cannot capture or validate execution stamps");
}

void test_deterministic_property_round_trip() {
    Fixture fixture;
    std::uint32_t state = 0x13579BDFu;
    const auto next = [&state]() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    for (std::uint32_t iteration = 0u; iteration < 4000u; ++iteration) {
        const std::uint32_t choice = next() % 3u;
        const std::uint32_t width = choice == 0u ? 1u : (choice == 1u ? 2u : 4u);
        const std::uint32_t raw_offset = next() % (kRamBytes - 8u);
        const std::uint32_t canonical_offset =
            raw_offset & ~(width - 1u);
        const std::uint32_t write_mirror = next() % 4u;
        const std::uint32_t read_mirror =
            (write_mirror + 1u + (next() % 3u)) % 4u;
        const std::uint32_t write_physical =
            write_mirror * kRamBytes + canonical_offset;
        const std::uint32_t read_physical =
            read_mirror * kRamBytes + canonical_offset;
        const std::uint32_t value = next();
        Segment write_segment =
            (iteration % 3u == 0u) ? Segment::User
            : ((iteration % 3u == 1u) ? Segment::KernelCached
                                      : Segment::KernelUncached);
        const Segment read_segment =
            (iteration % 2u == 0u) ? Segment::KernelUncached : Segment::User;
        const AccessOrigin origin =
            (iteration % 5u == 0u) ? AccessOrigin::Dma : AccessOrigin::Cpu;
        if (origin == AccessOrigin::Dma) {
            write_segment = Segment::User;
        }
        const std::uint32_t write_address =
            direct(write_segment, write_physical);
        const std::uint32_t read_address =
            direct(read_segment, read_physical);

        if (width == 1u) {
            require(fixture.bus->write8(write_address,
                                        static_cast<std::uint8_t>(value),
                                        origin)
                        .ok() &&
                        fixture.bus->read8(read_address).value ==
                            static_cast<std::uint8_t>(value),
                    "property byte round trip across mirrors");
        } else if (width == 2u) {
            require(fixture.bus->write16(write_address,
                                         static_cast<std::uint16_t>(value),
                                         origin)
                        .ok() &&
                        fixture.bus->read16(read_address).value ==
                            static_cast<std::uint16_t>(value),
                    "property halfword round trip across mirrors");
        } else {
            require(fixture.bus->write32(write_address, value, origin).ok() &&
                        fixture.bus->read32(read_address).value == value,
                    "property word round trip across mirrors");
        }
    }
}

} // namespace

int main() {
    test_translation_and_mirrors();
    test_endian_width_and_boundaries();
    test_faults_and_unmapped_behavior();
    test_mmio_callbacks_and_dma_origin();
    test_code_page_invalidation();
    test_deterministic_property_round_trip();
    std::cout << "clean_aot_memory tests passed\n";
    return 0;
}
