#pragma once

#include "clean_aot/memory/bus.hpp"
#include "clean_aot/runtime/native_dispatch.hpp"
#include "machine.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace clean_aot::cdrom {
class RawSectorSource;
struct DiscLayout;
} // namespace clean_aot::cdrom

namespace clean_aot::gpu {
class FrontEnd;
} // namespace clean_aot::gpu

namespace clean_aot::gte {
class Gte;
} // namespace clean_aot::gte

namespace clean_aot::mdec {
class Device;
} // namespace clean_aot::mdec

namespace clean_aot::sio {
class Sio0;
} // namespace clean_aot::sio

namespace clean_aot::soc {
class SystemOnChip;
} // namespace clean_aot::soc

namespace clean_aot::spu {
class Device;
} // namespace clean_aot::spu

namespace clean_aot::runtime {

enum class Status : std::uint8_t {
    Ok,
    InvalidConfiguration,
    TimeReversal,
    ArithmeticOverflow,
    DeviceRejected,
    ReentrantAccess,
    MissedDeadline,
    ExecutableRejected,
};

[[nodiscard]] constexpr bool succeeded(Status status) noexcept {
    return status == Status::Ok;
}

struct ClockAdvance final {
    Status status = Status::InvalidConfiguration;
    std::uint64_t retirement_delta = 0;
    std::uint64_t native_delta = 0;
    std::uint64_t retirement_total = 0;
    std::uint64_t native_total = 0;
    std::uint64_t phase = 0;
    std::uint64_t phase_denominator = 1;

    [[nodiscard]] bool ok() const noexcept { return succeeded(status); }
};

struct ClockDeadline final {
    Status status = Status::InvalidConfiguration;
    std::uint64_t native_cycle = 0;
    std::uint64_t retirement_cycle = 0;

    [[nodiscard]] bool ok() const noexcept { return succeeded(status); }
};

/*
 * Converts monotonically increasing recompiled CPU-retirement cycles to the
 * native 33.8688 MHz device clock.  For a retirement multiplier N/D, native
 * time advances by D/N.  The implementation is exact, carries fractional
 * phase across calls, and never forms an overflowing 64-bit product.
 */
class RetirementClock final {
public:
    RetirementClock(std::uint64_t multiplier_numerator,
                    std::uint64_t multiplier_denominator) noexcept;
    ~RetirementClock();

    RetirementClock(const RetirementClock&) = delete;
    RetirementClock& operator=(const RetirementClock&) = delete;
    RetirementClock(RetirementClock&&) noexcept;
    RetirementClock& operator=(RetirementClock&&) noexcept;

    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] machine::Machine::CpuMultiplier multiplier() const noexcept;
    [[nodiscard]] ClockAdvance advance_to(
        std::uint64_t retirement_cycle) noexcept;
    /* Earliest raw retirement cycle whose mapped native time is >= target. */
    [[nodiscard]] ClockDeadline deadline_for_native_cycle(
        std::uint64_t native_cycle) const noexcept;
    void reset() noexcept;

    [[nodiscard]] std::uint64_t retirement_total() const noexcept;
    [[nodiscard]] std::uint64_t native_total() const noexcept;
    [[nodiscard]] std::uint64_t phase() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class AccessFault : std::uint8_t {
    None,
    AddressError,
    TlbMiss,
    BusError,
    TimeReversal,
    ArithmeticOverflow,
    DeviceRejected,
    ReentrantAccess,
    MissedDeadline,
};

struct AccessContext final {
    /* Exact raw retirement cycle at which the transaction is issued. */
    std::uint64_t raw_cpu_cycle = 0;
    std::uint32_t instruction_pc = 0;
    bool in_delay_slot = false;
};

struct ReadResult final {
    AccessFault fault = AccessFault::BusError;
    std::uint32_t value = 0;
    std::uint32_t wait_cycles = 0;

    [[nodiscard]] bool ok() const noexcept { return fault == AccessFault::None; }
};

struct WriteResult final {
    AccessFault fault = AccessFault::BusError;
    std::uint32_t wait_cycles = 0;

    [[nodiscard]] bool ok() const noexcept { return fault == AccessFault::None; }
};

struct StereoSample final {
    std::int16_t left = 0;
    std::int16_t right = 0;

    friend constexpr bool operator==(const StereoSample&,
                                     const StereoSample&) noexcept = default;
};

enum class VideoClock : std::uint8_t {
    Pal,
    Ntsc,
};

struct CrtcSnapshot final {
    std::uint64_t oscillator_phase = 0;
    std::uint32_t line_half_cycle = 0;
    std::uint64_t field_half_cycle = 0;
    std::uint16_t scanline = 0;
    bool odd_field = false;
    bool hblank = true;
    bool vblank = true;

    friend constexpr bool operator==(const CrtcSnapshot&,
                                     const CrtcSnapshot&) noexcept = default;
};

struct Configuration final {
    /* Copied into immutable machine-owned storage during construction. */
    std::span<const std::uint8_t> bios{};
    machine::VBlankRate initial_vblank_rate = machine::VBlankRate::Hz100;
    std::uint64_t cpu_multiplier_numerator = 1;
    std::uint64_t cpu_multiplier_denominator = 1;
    VideoClock video_clock = VideoClock::Pal;
    /* Optional caller-owned media endpoint. No media bytes are retained. */
    cdrom::RawSectorSource* disc_source = nullptr;
    const cdrom::DiscLayout* disc_layout = nullptr;
    /* Fixed at construction from the authenticated manifest cardinality. */
    std::size_t authenticated_region_capacity = 512U;
};

enum class DeadlineSource : std::uint8_t {
    None,
    RootCounter,
    Cdrom,
    Sio0,
    Spu,
    Dma,
};

struct DeviceDeadline final {
    Status status = Status::InvalidConfiguration;
    bool pending = false;
    DeadlineSource source = DeadlineSource::None;
    std::uint64_t native_cycle = 0;
    std::uint64_t raw_cpu_cycle = 0;

    [[nodiscard]] bool ok() const noexcept { return succeeded(status); }
};

/*
 * Deterministic, single-threaded PS1 machine boundary for generated AOT code.
 * It owns no window, host audio device, renderer backend, compiler, or title
 * policy.  AccessContext is consumed synchronously and is never retained.
 */
class Runtime final {
public:
    struct AuthenticatedCopiedEpochBinding final {
        std::string_view variant_identity_sha256;
        std::uint32_t source_start = 0U;
        std::uint32_t source_byte_count = 0U;
        std::string_view source_sha256;
        std::string_view destination_sha256;
        GeneratedExecutableBinding generated{};
    };
    struct GeneratedExecutionSnapshot final {
        std::uint32_t pc = 0U;
        std::uint32_t cop0_status = 0U;
        std::uint32_t cop0_cause = 0U;
        std::uint32_t cop0_epc = 0U;
        std::uint32_t cop0_bad_vaddr = 0U;
        std::uint32_t instruction_pc = 0U;
        std::uint32_t branch_pc = 0U;
        bool in_delay_slot = false;
        std::uint64_t memory_reads = 0U;
        std::uint64_t memory_writes = 0U;
        std::uint64_t cache_isolated_writes = 0U;
        std::uint64_t device_reads = 0U;
        std::uint64_t device_writes = 0U;
        std::uint64_t spu_transfer_fifo_writes = 0U;
        std::uint64_t cdrom_mmio_reads = 0U;
        std::uint64_t cdrom_mmio_writes = 0U;
    };
    explicit Runtime(Configuration configuration);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) noexcept;
    Runtime& operator=(Runtime&&) noexcept;

    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] bool operational() const noexcept;
    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] Status reset() noexcept;
    [[nodiscard]] DispatchStatus bind_authenticated_bios(
        std::string_view selected_bios_identity_sha256,
        const GeneratedExecutableBinding& binding) noexcept;
    [[nodiscard]] DispatchStatus dispatch_bios() noexcept;
    [[nodiscard]] DispatchStatus bind_authenticated_copied_epoch(
        const AuthenticatedCopiedEpochBinding& binding) noexcept;
    [[nodiscard]] std::uint32_t bios_pc() const noexcept;
    [[nodiscard]] std::uint32_t last_bios_instruction_count() const noexcept;
    [[nodiscard]] GeneratedExecutionSnapshot bios_execution_snapshot() const noexcept;
    [[nodiscard]] DispatchStatus load_authenticated_executable(
        std::string_view selected_variant_identity_sha256,
        std::span<const std::uint8_t> executable,
        std::uint32_t selected_entry_pc,
        const GeneratedExecutableBinding& binding) noexcept;
    [[nodiscard]] DispatchStatus bind_authenticated_resident_executable(
        std::string_view selected_variant_identity_sha256,
        std::uint32_t selected_entry_pc,
        const GeneratedExecutableBinding& binding) noexcept;
    [[nodiscard]] DispatchStatus dispatch_selected(bool branch_taken) noexcept;
    [[nodiscard]] std::uint32_t selected_pc() const noexcept;
    [[nodiscard]] std::uint32_t last_selected_instruction_count() const noexcept;

    [[nodiscard]] ReadResult read8(AccessContext context,
                                   std::uint32_t address) noexcept;
    [[nodiscard]] ReadResult read16(AccessContext context,
                                    std::uint32_t address) noexcept;
    [[nodiscard]] ReadResult read32(AccessContext context,
                                    std::uint32_t address) noexcept;
    [[nodiscard]] WriteResult write8(AccessContext context,
                                     std::uint32_t address,
                                     std::uint8_t value) noexcept;
    [[nodiscard]] WriteResult write16(AccessContext context,
                                      std::uint32_t address,
                                      std::uint16_t value) noexcept;
    [[nodiscard]] WriteResult write32(AccessContext context,
                                      std::uint32_t address,
                                      std::uint32_t value) noexcept;
    [[nodiscard]] WriteResult write_masked32(AccessContext context,
                                             std::uint32_t aligned_address,
                                             std::uint32_t value,
                                             std::uint8_t byte_mask) noexcept;

    /* Explicit scheduler boundary for device deadlines without an MMIO access. */
    [[nodiscard]] ClockAdvance advance_to_retirement_cycle(
        std::uint64_t raw_cpu_cycle) noexcept;
    [[nodiscard]] DeviceDeadline next_device_deadline() const noexcept;

    [[nodiscard]] std::uint64_t current_retirement_cycle() const noexcept;
    [[nodiscard]] std::uint64_t current_native_cycle() const noexcept;
    [[nodiscard]] std::uint8_t hardware_interrupt_lines() const noexcept;
    [[nodiscard]] bool cpu_interrupt_line() const noexcept;
    [[nodiscard]] std::size_t queued_audio_samples() const noexcept;
    [[nodiscard]] std::uint64_t produced_audio_samples() const noexcept;
    [[nodiscard]] CrtcSnapshot crtc_snapshot() const noexcept;
    [[nodiscard]] bool restore_crtc_snapshot(
        const CrtcSnapshot& snapshot) noexcept;
    /* Removes up to destination.size() oldest samples; never blocks. */
    [[nodiscard]] std::size_t drain_audio(
        std::span<StereoSample> destination) noexcept;

    [[nodiscard]] memory::Bus& bus() noexcept;
    [[nodiscard]] const memory::Bus& bus() const noexcept;
    [[nodiscard]] soc::SystemOnChip& soc() noexcept;
    [[nodiscard]] const soc::SystemOnChip& soc() const noexcept;
    [[nodiscard]] gte::Gte& gte() noexcept;
    [[nodiscard]] const gte::Gte& gte() const noexcept;
    [[nodiscard]] gpu::FrontEnd& gpu() noexcept;
    [[nodiscard]] const gpu::FrontEnd& gpu() const noexcept;
    [[nodiscard]] mdec::Device& mdec() noexcept;
    [[nodiscard]] const mdec::Device& mdec() const noexcept;
    [[nodiscard]] sio::Sio0& sio0() noexcept;
    [[nodiscard]] const sio::Sio0& sio0() const noexcept;
    [[nodiscard]] spu::Device& spu() noexcept;
    [[nodiscard]] const spu::Device& spu() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clean_aot::runtime
