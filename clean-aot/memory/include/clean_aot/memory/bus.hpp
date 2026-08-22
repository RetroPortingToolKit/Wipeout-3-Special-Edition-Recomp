#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace clean_aot::memory {

/*
 * These are PS1 architectural bus constants, kept in one generic component so
 * a recompiled program can use the same address contract as the machine.  The
 * segment bases are expressed as their architectural selectors rather than as
 * title data.  A physical address is always the low 29-bit bus address.
 */
inline constexpr std::uint32_t kDirectAddressBytes = 1u << 29u;
inline constexpr std::uint32_t kKernelCachedBase = 4u << 29u;
inline constexpr std::uint32_t kKernelUncachedBase = 5u << 29u;
inline constexpr std::uint32_t kSegmentSelectorMask = 7u << 29u;
inline constexpr std::uint32_t kPhysicalAddressMask = kDirectAddressBytes - 1u;

inline constexpr std::uint32_t kRamBytes = 2u << 20u;
inline constexpr std::uint32_t kRamMirrorBytes = 8u << 20u;
inline constexpr std::uint32_t kScratchpadPhysicalBase = 0x1F8u << 16u;
inline constexpr std::uint32_t kScratchpadBytes = 1u << 10u;
inline constexpr std::uint32_t kBiosPhysicalBase = 0x1FCu << 20u;
inline constexpr std::uint32_t kBiosBytes = 512u << 10u;
inline constexpr std::uint32_t kMmioPhysicalBase =
    kScratchpadPhysicalBase + (1u << 12u);
inline constexpr std::uint32_t kMmioBytes =
    kBiosPhysicalBase - kMmioPhysicalBase;

inline constexpr std::uint32_t kCodePageBytes = 1u << 12u;
inline constexpr std::size_t kRamCodePageCount =
    kRamBytes / kCodePageBytes;
inline constexpr std::uint32_t kMaxCodeRegionBytes = 1u << 20u;
/* An unaligned one-megabyte interval can touch 257 distinct 4 KiB pages. */
inline constexpr std::size_t kMaxCodePagesPerRegion =
    (kMaxCodeRegionBytes / kCodePageBytes) + 1u;
inline constexpr std::size_t kVariantDigestBytes = 32u;

[[nodiscard]] constexpr std::optional<std::uint64_t>
next_code_page_generation(std::uint64_t current) noexcept {
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return current + 1u;
}

static_assert(kRamBytes % kCodePageBytes == 0u);
static_assert(kCodePageBytes % 4u == 0u);
static_assert(kRamMirrorBytes % kRamBytes == 0u);
static_assert(kMaxCodeRegionBytes < kRamBytes);
static_assert(kMmioBytes >= 4u);
static_assert(kRamMirrorBytes <= kScratchpadPhysicalBase);
static_assert(kScratchpadPhysicalBase + kScratchpadBytes <=
              kMmioPhysicalBase);
static_assert(kMmioPhysicalBase + kMmioBytes == kBiosPhysicalBase);
static_assert(kBiosPhysicalBase + kBiosBytes <= kDirectAddressBytes);
static_assert(kMaxCodePagesPerRegion <=
              std::numeric_limits<std::uint16_t>::max());

enum class Segment : std::uint8_t {
    User,
    KernelCached,
    KernelUncached,
    Unsupported,
};

enum class TranslationStatus : std::uint8_t {
    Ok,
    UnsupportedSegment,
};

struct Translation final {
    TranslationStatus status = TranslationStatus::UnsupportedSegment;
    Segment segment = Segment::Unsupported;
    std::uint32_t physical_address = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == TranslationStatus::Ok;
    }
};

/*
 * The PS1 has no usable virtual-memory mapping for the upper 1.5 GiB of
 * KUSEG in this contract.  KSEG0/KSEG1 are direct 512 MiB aliases of the
 * physical bus.  Keeping this function free of machine state makes address
 * normalization usable by generated regions as well as Bus.
 */
[[nodiscard]] constexpr Translation translate_address(
    std::uint32_t virtual_address) noexcept {
    const std::uint32_t selector = virtual_address & kSegmentSelectorMask;
    if (selector == 0u) {
        return {TranslationStatus::Ok, Segment::User, virtual_address};
    }
    if (selector == kKernelCachedBase) {
        return {TranslationStatus::Ok, Segment::KernelCached,
                virtual_address & kPhysicalAddressMask};
    }
    if (selector == kKernelUncachedBase) {
        return {TranslationStatus::Ok, Segment::KernelUncached,
                virtual_address & kPhysicalAddressMask};
    }
    return {};
}

/*
 * Forms a direct-segment address without silently masking invalid input.
 * Unsupported segments and physical addresses outside the 512 MiB direct
 * window fail closed rather than aliasing low RAM.
 */
[[nodiscard]] constexpr std::optional<std::uint32_t> segment_address(
    Segment segment, std::uint32_t physical_address) noexcept {
    if (physical_address > kPhysicalAddressMask) {
        return std::nullopt;
    }
    switch (segment) {
    case Segment::User:
        return physical_address;
    case Segment::KernelCached:
        return kKernelCachedBase | physical_address;
    case Segment::KernelUncached:
        return kKernelUncachedBase | physical_address;
    case Segment::Unsupported:
        return std::nullopt;
    }
    return std::nullopt;
}

enum class AccessWidth : std::uint8_t {
    Byte = 1u,
    Halfword = 2u,
    Word = 4u,
};

[[nodiscard]] constexpr std::uint32_t width_bytes(
    AccessWidth width) noexcept {
    switch (width) {
    case AccessWidth::Byte:
        return 1u;
    case AccessWidth::Halfword:
        return 2u;
    case AccessWidth::Word:
        return 4u;
    }
    return 0u;
}

enum class AccessOrigin : std::uint8_t {
    Cpu,
    Dma,
};

enum class AccessType : std::uint8_t {
    Read,
    Write,
};

enum class AccessStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    InvalidWidth,
    InvalidOrigin,
    AlignmentFault,
    AddressOverflow,
    UnsupportedSegment,
    OriginNotSupported,
    Unmapped,
    ReadOnly,
    MmioUnavailable,
    MmioRejected,
    GenerationExhausted,
};

struct Fault final {
    AccessStatus status = AccessStatus::Ok;
    AccessType type = AccessType::Read;
    AccessWidth width = AccessWidth::Byte;
    AccessOrigin origin = AccessOrigin::Cpu;
    std::uint32_t virtual_address = 0u;
    std::uint32_t physical_address = 0u;
};

template <typename T>
struct ReadResult final {
    static_assert(std::is_integral_v<T>, "memory results must be integral");

    T value = 0;
    AccessStatus status = AccessStatus::Ok;
    Fault fault{};

    [[nodiscard]] bool ok() const noexcept {
        return status == AccessStatus::Ok;
    }
};

struct WriteResult final {
    AccessStatus status = AccessStatus::Ok;
    Fault fault{};

    [[nodiscard]] bool ok() const noexcept {
        return status == AccessStatus::Ok;
    }
};

/*
 * MMIO is deliberately an explicit typed boundary.  The memory core does not
 * know device names, register meanings, or title policy.  A router may keep
 * device state, but it remains owned by the caller and must not throw or
 * retain a request reference after returning.
 */
enum class MmioStatus : std::uint8_t {
    Handled,
    Unmapped,
    Rejected,
};

struct MmioRequest final {
    std::uint32_t virtual_address = 0u;
    std::uint32_t physical_address = 0u;
    AccessWidth width = AccessWidth::Byte;
    AccessOrigin origin = AccessOrigin::Cpu;
};

struct MmioReadResponse final {
    MmioStatus status = MmioStatus::Unmapped;
    std::uint32_t value = 0u;
};

struct MmioWriteResponse final {
    MmioStatus status = MmioStatus::Unmapped;
};

class MmioRouter {
public:
    virtual ~MmioRouter() noexcept = default;

    [[nodiscard]] virtual MmioReadResponse read(
        const MmioRequest& request) noexcept = 0;
    [[nodiscard]] virtual MmioWriteResponse write(
        const MmioRequest& request,
        std::uint32_t value) noexcept = 0;
};

using MmioReadCallback = MmioReadResponse (*)(
    const MmioRequest& request,
    void* user) noexcept;
using MmioWriteCallback = MmioWriteResponse (*)(
    const MmioRequest& request,
    std::uint32_t value,
    void* user) noexcept;

/* A non-owning adapter for integrations that prefer typed function pointers. */
class CallbackMmioRouter final : public MmioRouter {
public:
    CallbackMmioRouter(MmioReadCallback read_callback,
                       MmioWriteCallback write_callback,
                       void* user = nullptr) noexcept
        : read_callback_(read_callback),
          write_callback_(write_callback),
          user_(user) {}

    [[nodiscard]] MmioReadResponse read(
        const MmioRequest& request) noexcept override;
    [[nodiscard]] MmioWriteResponse write(
        const MmioRequest& request,
        std::uint32_t value) noexcept override;

private:
    MmioReadCallback read_callback_ = nullptr;
    MmioWriteCallback write_callback_ = nullptr;
    void* user_ = nullptr;
};

struct BusConfig final {
    /* Valid bytes are copied into Bus during construction. */
    std::span<const std::uint8_t> bios{};
    /* Optional caller-owned MMIO router. */
    MmioRouter* mmio = nullptr;
};

/* Full catalog identity; all-zero is reserved as invalid/unselected. */
struct VariantIdentity final {
    std::array<std::uint8_t, kVariantDigestBytes> digest{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        for (const std::uint8_t byte : digest) {
            if (byte != 0u) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool operator==(
        const VariantIdentity&) const noexcept = default;
};

struct CodePageStamp final {
    std::uint32_t page_index = 0u;
    std::uint64_t generation = 0u;
};

struct CodeRegionStamp final {
    VariantIdentity variant{};
    std::uint64_t bus_domain = 0u;
    std::uint64_t reset_epoch = 0u;
    std::uint32_t virtual_start = 0u;
    std::uint32_t byte_count = 0u;
    std::uint16_t page_count = 0u;
    std::array<CodePageStamp, kMaxCodePagesPerRegion> pages{};
};

static_assert(std::is_trivially_copyable_v<VariantIdentity>);
static_assert(std::is_trivially_copyable_v<CodeRegionStamp>);

enum class CodeRegionCaptureStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    InvalidArgument,
    InvalidVariant,
    AddressOverflow,
    TooLarge,
    NotExecutable,
};

struct CodeRegionCapture final {
    CodeRegionCaptureStatus status = CodeRegionCaptureStatus::InvalidArgument;
    CodeRegionStamp stamp{};

    [[nodiscard]] bool ok() const noexcept {
        return status == CodeRegionCaptureStatus::Ok;
    }
};

enum class CodeRegionValidity : std::uint8_t {
    Current,
    Stale,
    VariantMismatch,
    Invalid,
};

enum class ResetStatus : std::uint8_t {
    Ok,
    InvalidConfiguration,
    IdentityExhausted,
};

class Bus final {
public:
    static constexpr std::uint32_t RamBytes = kRamBytes;
    static constexpr std::uint32_t RamMirrorBytes = kRamMirrorBytes;
    static constexpr std::uint32_t ScratchpadBytes = kScratchpadBytes;
    static constexpr std::uint32_t MmioBytes = kMmioBytes;
    static constexpr std::uint32_t BiosBytes = kBiosBytes;
    static constexpr std::uint32_t CodePageBytes = kCodePageBytes;

    explicit Bus(BusConfig config) noexcept;
    ~Bus() = default;

    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;
    Bus(Bus&&) = delete;
    Bus& operator=(Bus&&) = delete;

    [[nodiscard]] bool configured() const noexcept { return configured_; }

    [[nodiscard]] ReadResult<std::uint8_t> read8(
        std::uint32_t address,
        AccessOrigin origin = AccessOrigin::Cpu) const noexcept;
    [[nodiscard]] ReadResult<std::uint16_t> read16(
        std::uint32_t address,
        AccessOrigin origin = AccessOrigin::Cpu) const noexcept;
    [[nodiscard]] ReadResult<std::uint32_t> read32(
        std::uint32_t address,
        AccessOrigin origin = AccessOrigin::Cpu) const noexcept;
    [[nodiscard]] ReadResult<std::uint32_t> read(
        std::uint32_t address,
        AccessWidth width,
        AccessOrigin origin = AccessOrigin::Cpu) const noexcept;

    [[nodiscard]] WriteResult write8(
        std::uint32_t address,
        std::uint8_t value,
        AccessOrigin origin = AccessOrigin::Cpu) noexcept;
    [[nodiscard]] WriteResult write16(
        std::uint32_t address,
        std::uint16_t value,
        AccessOrigin origin = AccessOrigin::Cpu) noexcept;
    [[nodiscard]] WriteResult write32(
        std::uint32_t address,
        std::uint32_t value,
        AccessOrigin origin = AccessOrigin::Cpu) noexcept;
    [[nodiscard]] WriteResult write(
        std::uint32_t address,
        AccessWidth width,
        std::uint32_t value,
        AccessOrigin origin = AccessOrigin::Cpu) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> ram_view() const noexcept {
        return ram_;
    }
    [[nodiscard]] std::span<const std::uint8_t> scratchpad_view() const noexcept {
        return scratchpad_;
    }
    [[nodiscard]] std::span<const std::uint8_t> bios_view() const noexcept {
        return bios_;
    }

    // Retail 2 MiB RAM is visible through an 8 MiB aperture while RAM_SIZE
    // selects an 8 MiB bank, and only through its canonical 2 MiB aperture
    // when software selects the corrected 2 MiB bank geometry.
    void set_extended_ram_mirrors(bool enabled) noexcept {
        ram_aperture_bytes_ = enabled ? kRamMirrorBytes : kRamBytes;
    }
    [[nodiscard]] bool extended_ram_mirrors() const noexcept {
        return ram_aperture_bytes_ == kRamMirrorBytes;
    }

    /*
     * Clears RAM, scratchpad, and page generations while preserving the BIOS
     * image and MMIO router.  Every previously captured region becomes stale.
     */
    [[nodiscard]] ResetStatus reset_mutable_state() noexcept;

    [[nodiscard]] std::optional<std::uint64_t> ram_page_generation(
        std::uint32_t address) const noexcept;

    /*
     * Captures the canonical RAM pages touched by a precompiled region.  The
     * variant identity comes from the offline catalog; Bus never compiles or
     * discovers a replacement variant at runtime.
     */
    [[nodiscard]] CodeRegionCapture capture_code_region(
        std::uint32_t virtual_start,
        std::uint32_t byte_count,
        const VariantIdentity& variant) const noexcept;

    [[nodiscard]] CodeRegionValidity validate_code_region(
        const CodeRegionStamp& stamp,
        const VariantIdentity& expected_variant) const noexcept;

private:
    enum class Region : std::uint8_t {
        Unmapped,
        Ram,
        Scratchpad,
        Bios,
        Mmio,
    };

    struct ResolvedByte final {
        Region region = Region::Unmapped;
        TranslationStatus translation_status =
            TranslationStatus::UnsupportedSegment;
        Segment segment = Segment::Unsupported;
        std::uint32_t physical_address = 0u;
        std::size_t storage_offset = 0u;
    };

    struct PreparedAccess final {
        AccessStatus status = AccessStatus::Ok;
        std::uint32_t fault_virtual_address = 0u;
        std::uint32_t fault_physical_address = 0u;
        std::uint8_t count = 0u;
        Region region = Region::Unmapped;
        std::array<ResolvedByte, 4u> bytes{};
    };

    struct PreparedCodeRegion final {
        CodeRegionCaptureStatus status =
            CodeRegionCaptureStatus::InvalidArgument;
        std::uint32_t first_physical_page = 0u;
        std::uint16_t page_count = 0u;
    };

    [[nodiscard]] ResolvedByte resolve_byte(
        std::uint32_t virtual_address) const noexcept;
    [[nodiscard]] PreparedAccess prepare(
        std::uint32_t address,
        AccessWidth width,
        AccessOrigin origin) const noexcept;
    [[nodiscard]] PreparedCodeRegion prepare_code_region(
        std::uint32_t virtual_start,
        std::uint32_t byte_count) const noexcept;

    [[nodiscard]] static Fault make_fault(
        AccessStatus status,
        AccessType type,
        AccessWidth width,
        AccessOrigin origin,
        std::uint32_t virtual_address,
        std::uint32_t physical_address) noexcept;

    [[nodiscard]] static std::uint32_t width_mask(
        AccessWidth width) noexcept;
    [[nodiscard]] static std::uint64_t acquire_bus_domain() noexcept;

    std::array<std::uint8_t, kBiosBytes> bios_{};
    MmioRouter* mmio_ = nullptr;
    bool configured_ = false;
    std::uint64_t bus_domain_ = 0u;
    std::uint64_t reset_epoch_ = 1u;
    std::uint32_t ram_aperture_bytes_ = kRamMirrorBytes;
    std::array<std::uint8_t, kRamBytes> ram_{};
    std::array<std::uint8_t, kScratchpadBytes> scratchpad_{};
    std::array<std::uint64_t, kRamCodePageCount> generations_{};
};

static_assert(std::numeric_limits<std::uint32_t>::max() >=
              kKernelUncachedBase + kPhysicalAddressMask);

} // namespace clean_aot::memory
