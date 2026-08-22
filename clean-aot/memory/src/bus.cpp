#include "clean_aot/memory/bus.hpp"

#include <algorithm>
#include <atomic>
#include <limits>

namespace clean_aot::memory {

namespace {

[[nodiscard]] constexpr bool valid_origin(AccessOrigin origin) noexcept {
    return origin == AccessOrigin::Cpu || origin == AccessOrigin::Dma;
}

[[nodiscard]] constexpr AccessStatus rejected_mmio_status(
    MmioStatus status) noexcept {
    return status == MmioStatus::Unmapped ? AccessStatus::Unmapped
                                          : AccessStatus::MmioRejected;
}

static_assert(next_code_page_generation(0u).value() == 1u);
static_assert(!next_code_page_generation(
                   std::numeric_limits<std::uint64_t>::max())
                   .has_value());

} // namespace

MmioReadResponse CallbackMmioRouter::read(
    const MmioRequest& request) noexcept {
    if (read_callback_ == nullptr) {
        return {};
    }
    return read_callback_(request, user_);
}

MmioWriteResponse CallbackMmioRouter::write(
    const MmioRequest& request,
    std::uint32_t value) noexcept {
    if (write_callback_ == nullptr) {
        return {};
    }
    return write_callback_(request, value, user_);
}

Bus::Bus(BusConfig config) noexcept
    : mmio_(config.mmio) {
    const bool valid_bios = config.bios.size() == kBiosBytes &&
                            config.bios.data() != nullptr;
    if (!valid_bios) {
        return;
    }

    bus_domain_ = acquire_bus_domain();
    if (bus_domain_ == 0u) {
        return;
    }

    std::copy(config.bios.begin(), config.bios.end(), bios_.begin());
    configured_ = true;
}

std::uint64_t Bus::acquire_bus_domain() noexcept {
    static std::atomic<std::uint64_t> next_domain{1u};
    std::uint64_t current = next_domain.load(std::memory_order_relaxed);
    for (;;) {
        if (current == std::numeric_limits<std::uint64_t>::max()) {
            return 0u;
        }
        if (next_domain.compare_exchange_weak(current,
                                              current + 1u,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
            return current;
        }
    }
}

Bus::ResolvedByte Bus::resolve_byte(
    std::uint32_t virtual_address) const noexcept {
    const Translation translation = translate_address(virtual_address);
    if (!translation.ok()) {
        return {Region::Unmapped,
                translation.status,
                translation.segment,
                translation.physical_address,
                0u};
    }

    const std::uint32_t physical = translation.physical_address;
    if (physical < kRamMirrorBytes) {
        return {Region::Ram,
                translation.status,
                translation.segment,
                physical,
                static_cast<std::size_t>(physical % kRamBytes)};
    }

    const std::uint32_t scratch_offset =
        physical - kScratchpadPhysicalBase;
    if (translation.segment != Segment::KernelUncached &&
        physical >= kScratchpadPhysicalBase &&
        scratch_offset < kScratchpadBytes) {
        return {Region::Scratchpad,
                translation.status,
                translation.segment,
                physical,
                static_cast<std::size_t>(scratch_offset)};
    }

    const std::uint32_t mmio_offset = physical - kMmioPhysicalBase;
    if (physical >= kMmioPhysicalBase && mmio_offset < kMmioBytes) {
        return {Region::Mmio,
                translation.status,
                translation.segment,
                physical,
                static_cast<std::size_t>(mmio_offset)};
    }

    const std::uint32_t bios_offset = physical - kBiosPhysicalBase;
    if (physical >= kBiosPhysicalBase && bios_offset < kBiosBytes) {
        return {Region::Bios,
                translation.status,
                translation.segment,
                physical,
                static_cast<std::size_t>(bios_offset)};
    }

    return {Region::Unmapped,
            translation.status,
            translation.segment,
            translation.physical_address,
            0u};
}

Bus::PreparedAccess Bus::prepare(
    std::uint32_t address,
    AccessWidth width,
    AccessOrigin origin) const noexcept {
    PreparedAccess prepared{};
    prepared.fault_virtual_address = address;

    const std::uint32_t count = width_bytes(width);
    if (count == 0u) {
        prepared.status = AccessStatus::InvalidWidth;
        return prepared;
    }
    if (!valid_origin(origin)) {
        prepared.status = AccessStatus::InvalidOrigin;
        return prepared;
    }
    if (count > 1u && (address % count) != 0u) {
        prepared.status = AccessStatus::AlignmentFault;
        return prepared;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(address) + count;
    const std::uint64_t address_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
        1u;
    if (end > address_limit) {
        prepared.status = AccessStatus::AddressOverflow;
        return prepared;
    }

    prepared.count = static_cast<std::uint8_t>(count);
    for (std::uint32_t index = 0u; index < count; ++index) {
        const std::uint32_t current_address = address + index;
        const ResolvedByte resolved = resolve_byte(current_address);
        if (resolved.translation_status != TranslationStatus::Ok) {
            prepared.status = AccessStatus::UnsupportedSegment;
            prepared.fault_virtual_address = current_address;
            return prepared;
        }
        if (resolved.region == Region::Unmapped) {
            prepared.status = AccessStatus::Unmapped;
            prepared.fault_virtual_address = current_address;
            prepared.fault_physical_address = resolved.physical_address;
            return prepared;
        }
        if (origin == AccessOrigin::Dma &&
            (resolved.segment != Segment::User ||
             resolved.region == Region::Scratchpad)) {
            prepared.status = AccessStatus::OriginNotSupported;
            prepared.fault_virtual_address = current_address;
            prepared.fault_physical_address = resolved.physical_address;
            return prepared;
        }
        if (index == 0u) {
            prepared.region = resolved.region;
        } else if (resolved.region != prepared.region) {
            /* A multi-byte bus transaction never partially crosses devices. */
            prepared.status = AccessStatus::Unmapped;
            prepared.fault_virtual_address = current_address;
            prepared.fault_physical_address = resolved.physical_address;
            return prepared;
        }
        prepared.bytes[index] = resolved;
    }
    return prepared;
}

Fault Bus::make_fault(
    AccessStatus status,
    AccessType type,
    AccessWidth width,
    AccessOrigin origin,
    std::uint32_t virtual_address,
    std::uint32_t physical_address) noexcept {
    return {status, type, width, origin, virtual_address, physical_address};
}

std::uint32_t Bus::width_mask(AccessWidth width) noexcept {
    switch (width) {
    case AccessWidth::Byte:
        return 0xFFu;
    case AccessWidth::Halfword:
        return 0xFFFFu;
    case AccessWidth::Word:
        return std::numeric_limits<std::uint32_t>::max();
    }
    return 0u;
}

ReadResult<std::uint32_t> Bus::read(
    std::uint32_t address,
    AccessWidth width,
    AccessOrigin origin) const noexcept {
    if (!configured_) {
        return {0u,
                AccessStatus::InvalidConfiguration,
                make_fault(AccessStatus::InvalidConfiguration,
                            AccessType::Read,
                            width,
                            origin,
                            address,
                            0u)};
    }

    const PreparedAccess prepared = prepare(address, width, origin);
    if (prepared.status != AccessStatus::Ok) {
        return {0u,
                prepared.status,
                make_fault(prepared.status,
                            AccessType::Read,
                            width,
                            origin,
                            prepared.fault_virtual_address,
                            prepared.fault_physical_address)};
    }

    if (prepared.region == Region::Mmio) {
        if (mmio_ == nullptr) {
            return {0u,
                    AccessStatus::MmioUnavailable,
                    make_fault(AccessStatus::MmioUnavailable,
                                AccessType::Read,
                                width,
                                origin,
                                address,
                                prepared.bytes[0].physical_address)};
        }
        const MmioRequest request{address,
                                  prepared.bytes[0].physical_address,
                                  width,
                                  origin};
        const MmioReadResponse response = mmio_->read(request);
        if (response.status != MmioStatus::Handled) {
            const AccessStatus status = rejected_mmio_status(response.status);
            return {0u,
                    status,
                    make_fault(status,
                                AccessType::Read,
                                width,
                                origin,
                                address,
                                prepared.bytes[0].physical_address)};
        }
        return {response.value & width_mask(width), AccessStatus::Ok, {}};
    }

    std::uint32_t value = 0u;
    for (std::uint32_t index = 0u; index < prepared.count; ++index) {
        std::uint8_t byte = 0u;
        const ResolvedByte& resolved = prepared.bytes[index];
        switch (prepared.region) {
        case Region::Ram:
            byte = ram_[resolved.storage_offset];
            break;
        case Region::Scratchpad:
            byte = scratchpad_[resolved.storage_offset];
            break;
        case Region::Bios:
            byte = bios_[resolved.storage_offset];
            break;
        case Region::Mmio:
        case Region::Unmapped:
            break;
        }
        value |= static_cast<std::uint32_t>(byte) << (index * 8u);
    }
    return {value, AccessStatus::Ok, {}};
}

ReadResult<std::uint8_t> Bus::read8(
    std::uint32_t address,
    AccessOrigin origin) const noexcept {
    const ReadResult<std::uint32_t> result = read(address, AccessWidth::Byte, origin);
    return {static_cast<std::uint8_t>(result.value), result.status, result.fault};
}

ReadResult<std::uint16_t> Bus::read16(
    std::uint32_t address,
    AccessOrigin origin) const noexcept {
    const ReadResult<std::uint32_t> result =
        read(address, AccessWidth::Halfword, origin);
    return {static_cast<std::uint16_t>(result.value), result.status, result.fault};
}

ReadResult<std::uint32_t> Bus::read32(
    std::uint32_t address,
    AccessOrigin origin) const noexcept {
    return read(address, AccessWidth::Word, origin);
}

WriteResult Bus::write(
    std::uint32_t address,
    AccessWidth width,
    std::uint32_t value,
    AccessOrigin origin) noexcept {
    if (!configured_) {
        return {AccessStatus::InvalidConfiguration,
                make_fault(AccessStatus::InvalidConfiguration,
                            AccessType::Write,
                            width,
                            origin,
                            address,
                            0u)};
    }

    const PreparedAccess prepared = prepare(address, width, origin);
    if (prepared.status != AccessStatus::Ok) {
        return {prepared.status,
                make_fault(prepared.status,
                            AccessType::Write,
                            width,
                            origin,
                            prepared.fault_virtual_address,
                            prepared.fault_physical_address)};
    }

    const std::uint32_t masked_value = value & width_mask(width);
    if (prepared.region == Region::Bios) {
        return {AccessStatus::ReadOnly,
                make_fault(AccessStatus::ReadOnly,
                            AccessType::Write,
                            width,
                            origin,
                            address,
                            prepared.bytes[0].physical_address)};
    }

    if (prepared.region == Region::Mmio) {
        if (mmio_ == nullptr) {
            return {AccessStatus::MmioUnavailable,
                    make_fault(AccessStatus::MmioUnavailable,
                                AccessType::Write,
                                width,
                                origin,
                                address,
                                prepared.bytes[0].physical_address)};
        }
        const MmioRequest request{address,
                                  prepared.bytes[0].physical_address,
                                  width,
                                  origin};
        const MmioWriteResponse response = mmio_->write(request, masked_value);
        if (response.status != MmioStatus::Handled) {
            const AccessStatus status = rejected_mmio_status(response.status);
            return {status,
                    make_fault(status,
                                AccessType::Write,
                                width,
                                origin,
                                address,
                                prepared.bytes[0].physical_address)};
        }
        return {};
    }

    if (prepared.region == Region::Ram) {
        /* Strictly aligned 1/2/4-byte writes cannot cross a 4 KiB page. */
        const std::size_t page =
            prepared.bytes[0].storage_offset / kCodePageBytes;
        if (!next_code_page_generation(generations_[page]).has_value()) {
            return {AccessStatus::GenerationExhausted,
                    make_fault(AccessStatus::GenerationExhausted,
                               AccessType::Write,
                               width,
                               origin,
                               address,
                               prepared.bytes[0].physical_address)};
        }

        for (std::uint32_t index = 0u; index < prepared.count; ++index) {
            const std::uint8_t byte = static_cast<std::uint8_t>(
                (masked_value >> (index * 8u)) & 0xFFu);
            ram_[prepared.bytes[index].storage_offset] = byte;
        }
        ++generations_[page];
        return {};
    }

    if (prepared.region == Region::Scratchpad) {
        for (std::uint32_t index = 0u; index < prepared.count; ++index) {
            const std::uint8_t byte = static_cast<std::uint8_t>(
                (masked_value >> (index * 8u)) & 0xFFu);
            scratchpad_[prepared.bytes[index].storage_offset] = byte;
        }
        return {};
    }

    return {AccessStatus::Unmapped,
            make_fault(AccessStatus::Unmapped,
                        AccessType::Write,
                        width,
                        origin,
                        address,
                        prepared.bytes[0].physical_address)};
}

WriteResult Bus::write8(
    std::uint32_t address,
    std::uint8_t value,
    AccessOrigin origin) noexcept {
    return write(address, AccessWidth::Byte, value, origin);
}

WriteResult Bus::write16(
    std::uint32_t address,
    std::uint16_t value,
    AccessOrigin origin) noexcept {
    return write(address, AccessWidth::Halfword, value, origin);
}

WriteResult Bus::write32(
    std::uint32_t address,
    std::uint32_t value,
    AccessOrigin origin) noexcept {
    return write(address, AccessWidth::Word, value, origin);
}

ResetStatus Bus::reset_mutable_state() noexcept {
    if (!configured_) {
        return ResetStatus::InvalidConfiguration;
    }

    std::uint64_t next_epoch = 1u;
    std::uint64_t next_domain = bus_domain_;
    if (reset_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
        next_domain = acquire_bus_domain();
        if (next_domain == 0u) {
            return ResetStatus::IdentityExhausted;
        }
    } else {
        next_epoch = reset_epoch_ + 1u;
    }

    std::fill(ram_.begin(), ram_.end(), 0u);
    std::fill(scratchpad_.begin(), scratchpad_.end(), 0u);
    std::fill(generations_.begin(), generations_.end(), 0u);
    bus_domain_ = next_domain;
    reset_epoch_ = next_epoch;
    return ResetStatus::Ok;
}

std::optional<std::uint64_t> Bus::ram_page_generation(
    std::uint32_t address) const noexcept {
    const ResolvedByte resolved = resolve_byte(address);
    if (resolved.region != Region::Ram) {
        return std::nullopt;
    }
    return generations_[resolved.storage_offset / kCodePageBytes];
}

Bus::PreparedCodeRegion Bus::prepare_code_region(
    std::uint32_t virtual_start,
    std::uint32_t byte_count) const noexcept {
    PreparedCodeRegion prepared{};
    if (byte_count == 0u) {
        return prepared;
    }
    if (byte_count > kMaxCodeRegionBytes) {
        prepared.status = CodeRegionCaptureStatus::TooLarge;
        return prepared;
    }

    const std::uint64_t end = static_cast<std::uint64_t>(virtual_start) +
                              static_cast<std::uint64_t>(byte_count);
    const std::uint64_t address_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
        1u;
    if (end > address_limit) {
        prepared.status = CodeRegionCaptureStatus::AddressOverflow;
        return prepared;
    }

    const std::uint32_t virtual_last = static_cast<std::uint32_t>(end - 1u);
    const Translation first = translate_address(virtual_start);
    const Translation last = translate_address(virtual_last);
    if (!first.ok() || !last.ok() || first.segment != last.segment) {
        prepared.status = CodeRegionCaptureStatus::NotExecutable;
        return prepared;
    }

    const std::uint64_t physical_end =
        static_cast<std::uint64_t>(first.physical_address) + byte_count;
    if (first.physical_address >= kRamMirrorBytes ||
        physical_end > kRamMirrorBytes ||
        last.physical_address != physical_end - 1u) {
        prepared.status = CodeRegionCaptureStatus::NotExecutable;
        return prepared;
    }

    const std::uint32_t last_physical_page =
        last.physical_address / kCodePageBytes;
    prepared.first_physical_page = first.physical_address / kCodePageBytes;
    const std::uint32_t page_count =
        last_physical_page - prepared.first_physical_page + 1u;
    if (page_count > kMaxCodePagesPerRegion) {
        prepared.status = CodeRegionCaptureStatus::TooLarge;
        return prepared;
    }
    prepared.page_count = static_cast<std::uint16_t>(page_count);
    prepared.status = CodeRegionCaptureStatus::Ok;
    return prepared;
}

CodeRegionCapture Bus::capture_code_region(
    std::uint32_t virtual_start,
    std::uint32_t byte_count,
    const VariantIdentity& variant) const noexcept {
    CodeRegionCapture capture{};
    capture.stamp.variant = variant;
    capture.stamp.bus_domain = bus_domain_;
    capture.stamp.reset_epoch = reset_epoch_;
    capture.stamp.virtual_start = virtual_start;
    capture.stamp.byte_count = byte_count;

    if (!configured_) {
        capture.status = CodeRegionCaptureStatus::InvalidConfiguration;
        return capture;
    }
    if (byte_count == 0u) {
        capture.status = CodeRegionCaptureStatus::InvalidArgument;
        return capture;
    }
    if (!variant.valid()) {
        capture.status = CodeRegionCaptureStatus::InvalidVariant;
        return capture;
    }

    const PreparedCodeRegion prepared =
        prepare_code_region(virtual_start, byte_count);
    if (prepared.status != CodeRegionCaptureStatus::Ok) {
        capture.status = prepared.status;
        return capture;
    }

    /* The bounded interval is shorter than one RAM period, so these canonical
       page indices are unique even when the interval crosses a mirror. */
    capture.stamp.page_count = prepared.page_count;
    for (std::size_t page_slot = 0u;
         page_slot < prepared.page_count;
         ++page_slot) {
        const std::uint32_t physical_page =
            prepared.first_physical_page +
            static_cast<std::uint32_t>(page_slot);
        const std::uint32_t page_index = static_cast<std::uint32_t>(
            physical_page % kRamCodePageCount);
        capture.stamp.pages[page_slot] =
            {page_index, generations_[page_index]};
    }

    capture.status = CodeRegionCaptureStatus::Ok;
    return capture;
}

CodeRegionValidity Bus::validate_code_region(
    const CodeRegionStamp& stamp,
    const VariantIdentity& expected_variant) const noexcept {
    if (!configured_) {
        return CodeRegionValidity::Invalid;
    }
    if (stamp.byte_count == 0u || stamp.page_count == 0u ||
        stamp.page_count > kMaxCodePagesPerRegion ||
        stamp.byte_count > kMaxCodeRegionBytes || !stamp.variant.valid() ||
        !expected_variant.valid() || stamp.bus_domain == 0u ||
        stamp.reset_epoch == 0u) {
        return CodeRegionValidity::Invalid;
    }

    const PreparedCodeRegion prepared =
        prepare_code_region(stamp.virtual_start, stamp.byte_count);
    if (prepared.status != CodeRegionCaptureStatus::Ok ||
        prepared.page_count != stamp.page_count) {
        return CodeRegionValidity::Invalid;
    }
    for (std::size_t index = 0u; index < stamp.page_count; ++index) {
        const std::uint32_t physical_page =
            prepared.first_physical_page +
            static_cast<std::uint32_t>(index);
        const std::uint32_t page_index = static_cast<std::uint32_t>(
            physical_page % kRamCodePageCount);
        if (page_index != stamp.pages[index].page_index) {
            return CodeRegionValidity::Invalid;
        }
    }
    if (stamp.variant != expected_variant) {
        return CodeRegionValidity::VariantMismatch;
    }
    if (stamp.bus_domain != bus_domain_ || stamp.reset_epoch != reset_epoch_) {
        return CodeRegionValidity::Stale;
    }
    for (std::size_t index = 0u; index < stamp.page_count; ++index) {
        const std::uint32_t page_index = stamp.pages[index].page_index;
        if (generations_[page_index] != stamp.pages[index].generation) {
            return CodeRegionValidity::Stale;
        }
    }
    return CodeRegionValidity::Current;
}

} // namespace clean_aot::memory
