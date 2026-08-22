#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace clean_aot::soc {

enum class SocStatus : std::uint8_t {
    ok,
    idle,
    invalid_argument,
    malformed_callback,
    callback_rejected,
    bus_fault,
    device_fault,
    invalid_mode,
    traversal_limit,
    linked_list_loop,
    arithmetic_overflow,
    snapshot_mismatch,
};

[[nodiscard]] constexpr bool succeeded(const SocStatus status) noexcept {
    return status == SocStatus::ok || status == SocStatus::idle;
}

enum class InterruptSource : std::uint8_t {
    vblank = 0,
    graphics = 1,
    disc = 2,
    dma = 3,
    counter0 = 4,
    counter1 = 5,
    counter2 = 6,
    controller = 7,
    serial = 8,
    sound = 9,
    peripheral = 10,
};

constexpr std::uint32_t kInterruptSourceCount = 11U;
constexpr std::uint32_t kInterruptAllMask =
    (std::uint32_t{1} << kInterruptSourceCount) - std::uint32_t{1};

[[nodiscard]] constexpr std::uint32_t interrupt_bit(
    const InterruptSource source) noexcept {
    const std::uint8_t value = static_cast<std::uint8_t>(source);
    return value < kInterruptSourceCount ? std::uint32_t{1} << value : 0U;
}

struct InterruptSnapshot {
    std::uint32_t status = 0;
    std::uint32_t mask = 0;
};

class InterruptController final {
public:
    void reset() noexcept;
    void raise(InterruptSource source) noexcept;
    void raise_mask(std::uint32_t bits) noexcept;
    void acknowledge(InterruptSource source) noexcept;
    void acknowledge_mask(std::uint32_t bits) noexcept;
    void write_status(std::uint32_t value) noexcept;
    void write_mask(std::uint32_t value) noexcept;

    [[nodiscard]] std::uint32_t status() const noexcept;
    [[nodiscard]] std::uint32_t mask() const noexcept;
    [[nodiscard]] bool pending(InterruptSource source) const noexcept;
    [[nodiscard]] bool enabled(InterruptSource source) const noexcept;
    [[nodiscard]] bool cpu_line() const noexcept;

    [[nodiscard]] InterruptSnapshot snapshot() const noexcept;
    [[nodiscard]] SocStatus validate_snapshot(
        const InterruptSnapshot& state) const noexcept;
    [[nodiscard]] SocStatus restore(const InterruptSnapshot& state) noexcept;

private:
    std::uint32_t status_ = 0;
    std::uint32_t mask_ = 0;
};

struct CycleDelta {
    // This is native SoC/system-clock time. A clean-AOT scheduler may use a
    // separate CPU-retirement multiplier, but must not multiply this delta
    // when advancing peripheral-visible counter time.
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 1;

    [[nodiscard]] static constexpr CycleDelta whole(
        const std::uint64_t cycles) noexcept {
        return CycleDelta{cycles, 1};
    }
};

struct CounterAdvanceReport {
    SocStatus status = SocStatus::ok;
    std::uint64_t cpu_numerator = 0;
    std::uint64_t cpu_denominator = 1;
    std::array<std::uint64_t, 3> ticks{};
    std::uint32_t events = 0;
};

enum class CounterClockSource : std::uint8_t {
    system,
    dot_clock,
    hblank,
    system_div8,
};

enum class CounterEdge : std::uint8_t {
    hblank_begin,
    hblank_end,
    vblank_begin,
    vblank_end,
    dot_clock,
};

enum class CounterSyncMode : std::uint8_t {
    pause_during_gate = 0,
    reset_at_gate = 1,
    reset_at_gate_pause_outside = 2,
    pause_until_gate = 3,
    reserved = 7,
};

enum class CounterEventKind : std::uint8_t {
    target,
    overflow,
    synchronization,
};

struct CounterEvent {
    std::uint8_t counter = 0;
    CounterEventKind kind = CounterEventKind::target;
    bool irq_requested = false;
    bool irq_level = false;
    std::uint64_t ordinal = 0;
};

using CounterEventCallback = void (*)(void*, const CounterEvent&) noexcept;

struct CounterCallbacks {
    void* context = nullptr;
    CounterEventCallback on_event = nullptr;
};

struct CounterSnapshot {
    std::uint16_t count = 0;
    std::uint16_t target = 0;
    std::uint16_t mode = 0;
    bool running = true;
    bool synchronization_level = false;
    bool synchronization_released = false;
    bool irq_armed = true;
    bool irq_level = false;
    std::uint8_t reset_hold_ticks = 0;
    std::uint64_t phase_numerator = 0;
    std::uint64_t phase_denominator = 1;
    std::uint64_t event_ordinal = 0;
};

class RootCounters final {
public:
    static constexpr std::size_t kCount = 3;

    explicit RootCounters(InterruptController* interrupt_sink = nullptr) noexcept;

    void reset() noexcept;
    void set_callbacks(CounterCallbacks callbacks) noexcept;

    [[nodiscard]] std::uint16_t read_count(std::size_t index) const noexcept;
    [[nodiscard]] std::uint16_t read_mode(std::size_t index) noexcept;
    [[nodiscard]] std::uint16_t read_target(std::size_t index) const noexcept;
    SocStatus write_count(std::size_t index, std::uint16_t value) noexcept;
    SocStatus write_mode(std::size_t index, std::uint16_t value) noexcept;
    SocStatus write_target(std::size_t index, std::uint16_t value) noexcept;

    // Advance counters by native SoC time, not by the amount of CPU work
    // retired during that time.
    SocStatus advance_cpu(const CycleDelta& delta,
                          CounterAdvanceReport* report = nullptr) noexcept;
    SocStatus advance_external_clock(
        CounterClockSource source, std::uint64_t ticks,
        CounterAdvanceReport* report = nullptr) noexcept;
    SocStatus input_edge(CounterEdge edge,
                         CounterAdvanceReport* report = nullptr) noexcept;
    // Re-drive level-sensitive timer requests after the interrupt controller
    // has been acknowledged. Pulse requests have already returned inactive.
    void resample_irq_lines() noexcept;

    [[nodiscard]] bool running(std::size_t index) const noexcept;
    [[nodiscard]] bool irq_level(std::size_t index) const noexcept;
    [[nodiscard]] CounterSnapshot snapshot(std::size_t index) const noexcept;
    [[nodiscard]] SocStatus validate_snapshot(
        std::size_t index, const CounterSnapshot& state) const noexcept;
    SocStatus restore(std::size_t index, const CounterSnapshot& state) noexcept;

private:
    struct CounterState {
        std::uint16_t count = 0;
        std::uint16_t target = 0;
        std::uint16_t mode = 0;
        bool running = true;
        bool synchronization_level = false;
        bool synchronization_released = false;
        bool irq_armed = true;
        bool irq_level = false;
        bool target_seen = false;
        bool overflow_seen = false;
        std::uint8_t reset_hold_ticks = 0;
        std::uint64_t phase_numerator = 0;
        std::uint64_t phase_denominator = 1;
        std::uint64_t event_ordinal = 0;
    };

    InterruptController* interrupt_sink_ = nullptr;
    CounterCallbacks callbacks_{};
    std::array<CounterState, kCount> state_storage_{};

    [[nodiscard]] CounterState* state(std::size_t index) noexcept;
    [[nodiscard]] const CounterState* state(std::size_t index) const noexcept;
    SocStatus add_ticks(std::size_t index, std::uint64_t ticks,
                        CounterAdvanceReport& report) noexcept;
    SocStatus synchronization_edge(std::size_t index, CounterEdge edge,
                                   CounterAdvanceReport& report) noexcept;
    void emit_event(std::size_t index, CounterEventKind kind,
                    CounterAdvanceReport& report) noexcept;
};

enum class DmaCallbackStatus : std::uint8_t {
    ok,
    not_ready,
    rejected,
    malformed,
};

using DmaReadWordCallback = DmaCallbackStatus (*)(
    void*, std::uint32_t address, std::uint32_t* value) noexcept;
using DmaWriteWordCallback = DmaCallbackStatus (*)(
    void*, std::uint32_t address, std::uint32_t value) noexcept;
using DmaDeviceReadCallback = DmaCallbackStatus (*)(
    void*, std::uint32_t* value) noexcept;
using DmaDeviceWriteCallback = DmaCallbackStatus (*)(
    void*, std::uint32_t value) noexcept;

struct DmaBusCallbacks {
    void* context = nullptr;
    DmaReadWordCallback read_word = nullptr;
    DmaWriteWordCallback write_word = nullptr;
};

struct DmaDeviceCallbacks {
    void* context = nullptr;
    DmaDeviceReadCallback read_word = nullptr;
    DmaDeviceWriteCallback write_word = nullptr;
};

// Endpoint callbacks may inspect the controller but must not re-enter run() or
// mutate DMA state. Such attempts fail the active transfer with
// SocStatus::callback_rejected before transfer counters are advanced.

enum class DmaSyncMode : std::uint8_t {
    manual = 0,
    request = 1,
    linked_list = 2,
    reserved = 3,
};

enum class DmaDirection : std::uint8_t {
    to_memory,
    to_device,
};

struct DmaChannelRegisters {
    std::uint32_t base_address = 0;
    std::uint32_t block_control = 0;
    std::uint32_t channel_control = 0;
};

struct DmaChannelStatus {
    bool active = false;
    bool completed = false;
    bool error = false;
    bool request_asserted = true;
    SocStatus last_status = SocStatus::idle;
    std::uint32_t current_address = 0;
    std::uint64_t words_transferred = 0;
    std::uint64_t words_remaining = 0;
    std::uint32_t linked_nodes = 0;
};

struct DmaRunBudget {
    // Deterministic native scheduler service accounting. This is not a
    // CPU-retirement quota and must not be multiplied with CPU overclocking.
    std::uint64_t max_words = 1'000'000;
    std::uint64_t max_cpu_cycles = 1'000'000;
};

struct DmaRunReport {
    SocStatus status = SocStatus::idle;
    std::uint32_t channel = 0;
    std::uint64_t words_transferred = 0;
    // Deterministic native scheduler accounting, not device latency or
    // multiplied CPU-retirement work.
    std::uint64_t cpu_cycles = 0;
    bool became_idle = false;
    bool irq_boundary = false;
};

struct DmaChannelSnapshot {
    DmaChannelRegisters registers{};
    bool active = false;
    bool completed = false;
    bool error = false;
    SocStatus last_status = SocStatus::idle;
    std::uint32_t current_address = 0;
    std::uint64_t words_transferred = 0;
    std::uint64_t words_remaining = 0;
    std::uint32_t linked_current = 0;
    std::uint32_t linked_next = 0;
    std::uint32_t linked_nodes = 0;
    std::uint64_t linked_words = 0;
    std::uint32_t burst_words = 0;
    std::uint64_t block_words_remaining = 0;
    std::uint32_t request_blocks_remaining = 0;
    std::uint32_t cpu_window_remaining = 0;
    bool window_active = false;
    bool request_asserted = true;
    std::vector<std::uint32_t> visited_nodes{};
};

struct DmaSnapshot {
    std::array<DmaChannelSnapshot, 7> channels{};
    std::uint32_t priority_control = 0;
    std::uint32_t interrupt_control = 0;
    std::uint32_t interrupt_flags = 0;
    bool master_irq_line = false;
};

class DmaController final {
public:
    static constexpr std::size_t kChannelCount = 7;
    static constexpr std::uint32_t kAddressMask = 0x00ff'ffffU;
    static constexpr std::size_t kOrderingTableChannel = 6;

    explicit DmaController(InterruptController* interrupt_sink = nullptr,
                           std::uint32_t max_linked_nodes = 4096U,
                           std::uint64_t max_linked_words = 1'000'000U) noexcept;

    void reset() noexcept;
    SocStatus set_callbacks(std::size_t index, DmaBusCallbacks bus,
                            DmaDeviceCallbacks device) noexcept;
    [[nodiscard]] DmaChannelRegisters registers(std::size_t index) const noexcept;
    SocStatus write_base_address(std::size_t index, std::uint32_t value) noexcept;
    SocStatus write_block_control(std::size_t index, std::uint32_t value) noexcept;
    SocStatus write_channel_control(std::size_t index,
                                    std::uint32_t value) noexcept;
    SocStatus set_request(std::size_t index, bool asserted) noexcept;

    [[nodiscard]] std::uint32_t read_priority_control() const noexcept;
    void write_priority_control(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t read_interrupt_control() const noexcept;
    void write_interrupt_control(std::uint32_t value) noexcept;
    void acknowledge_interrupts(std::uint32_t channel_mask) noexcept;

    [[nodiscard]] DmaChannelStatus channel_status(std::size_t index) const noexcept;
    [[nodiscard]] bool irq_line() const noexcept;
    [[nodiscard]] std::uint32_t interrupt_flags() const noexcept;

    DmaRunReport run(const DmaRunBudget& budget) noexcept;

    [[nodiscard]] DmaSnapshot snapshot() const;
    [[nodiscard]] SocStatus validate_snapshot(
        const DmaSnapshot& state) const noexcept;
    SocStatus restore(const DmaSnapshot& state) noexcept;

private:
    struct ChannelState {
        DmaChannelRegisters registers{};
        bool active = false;
        bool completed = false;
        bool error = false;
        SocStatus last_status = SocStatus::idle;
        std::uint32_t current_address = 0;
        std::uint64_t words_transferred = 0;
        std::uint64_t words_remaining = 0;
        std::uint32_t linked_current = 0;
        std::uint32_t linked_next = 0;
        std::uint32_t linked_nodes = 0;
        std::uint64_t linked_words = 0;
        std::uint32_t burst_words = 0;
        std::uint64_t block_words_remaining = 0;
        std::uint32_t request_blocks_remaining = 0;
        std::uint32_t cpu_window_remaining = 0;
        bool window_active = false;
        bool request_asserted = true;
        std::vector<std::uint32_t> visited_nodes{};
    };

    InterruptController* interrupt_sink_ = nullptr;
    std::array<ChannelState, kChannelCount> channel_storage_{};
    std::array<DmaBusCallbacks, kChannelCount> buses_{};
    std::array<DmaDeviceCallbacks, kChannelCount> devices_{};
    std::uint32_t priority_control_ = 0x0765'4321U;
    std::uint32_t interrupt_control_ = 0;
    std::uint32_t interrupt_flags_ = 0;
    bool master_irq_line_ = false;
    std::uint32_t max_linked_nodes_ = 4096U;
    std::uint64_t max_linked_words_ = 1'000'000U;
    bool running_ = false;
    bool callback_mutated_ = false;

    [[nodiscard]] ChannelState* channel(std::size_t index) noexcept;
    [[nodiscard]] const ChannelState* channel(std::size_t index) const noexcept;
    [[nodiscard]] bool ready(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t choose_channel() const noexcept;
    [[nodiscard]] DmaSyncMode sync_mode(const ChannelState& channel) const noexcept;
    [[nodiscard]] DmaDirection direction(const ChannelState& channel) const noexcept;
    [[nodiscard]] bool decrement(const ChannelState& channel) const noexcept;
    [[nodiscard]] bool chopping(const ChannelState& channel) const noexcept;
    [[nodiscard]] std::uint32_t dma_window(const ChannelState& channel) const noexcept;
    [[nodiscard]] std::uint32_t cpu_window(const ChannelState& channel) const noexcept;
    [[nodiscard]] std::uint64_t encoded_word_count(
        const ChannelState& channel) const noexcept;
    [[nodiscard]] std::uint64_t encoded_block_size(
        const ChannelState& channel) const noexcept;
    [[nodiscard]] std::uint32_t encoded_block_count(
        const ChannelState& channel) const noexcept;
    SocStatus start_if_needed(std::size_t index) noexcept;
    SocStatus transfer_one(std::size_t index, DmaRunReport& report,
                           const DmaRunBudget& budget) noexcept;
    SocStatus transfer_linear_word(std::size_t index, DmaRunReport& report,
                                   const DmaRunBudget& budget) noexcept;
    SocStatus transfer_linked_node(std::size_t index, DmaRunReport& report,
                                   const DmaRunBudget& budget) noexcept;
    SocStatus transfer_otc_word(std::size_t index, DmaRunReport& report,
                                const DmaRunBudget& budget) noexcept;
    SocStatus move_word_between_endpoints(std::size_t index,
                                          std::uint32_t address) noexcept;
    SocStatus read_bus(std::size_t index, std::uint32_t address,
                       std::uint32_t& value) noexcept;
    SocStatus write_bus(std::size_t index, std::uint32_t address,
                        std::uint32_t value) noexcept;
    SocStatus read_device(std::size_t index, std::uint32_t& value) noexcept;
    SocStatus write_device(std::size_t index, std::uint32_t value) noexcept;
    [[nodiscard]] bool complete(std::size_t index) noexcept;
    void fail(std::size_t index, SocStatus status) noexcept;
    [[nodiscard]] bool assert_channel_flag(std::size_t index,
                                           bool slice) noexcept;
    void update_irq_line(bool emit_edge = true) noexcept;
    static std::uint32_t step_address(std::uint32_t address,
                                      bool decrement) noexcept;
};

struct SocSnapshot {
    InterruptSnapshot interrupts{};
    DmaSnapshot dma{};
    std::array<CounterSnapshot, RootCounters::kCount> counters{};
};

class SystemOnChip final {
public:
    SystemOnChip() noexcept;

    void reset() noexcept;
    [[nodiscard]] InterruptController& interrupts() noexcept;
    [[nodiscard]] DmaController& dma() noexcept;
    [[nodiscard]] RootCounters& counters() noexcept;
    [[nodiscard]] const InterruptController& interrupts() const noexcept;
    [[nodiscard]] const DmaController& dma() const noexcept;
    [[nodiscard]] const RootCounters& counters() const noexcept;

    [[nodiscard]] SocSnapshot snapshot() const;
    [[nodiscard]] SocStatus validate_snapshot(
        const SocSnapshot& state) const noexcept;
    SocStatus restore(const SocSnapshot& state) noexcept;

private:
    InterruptController interrupts_{};
    DmaController dma_;
    RootCounters counters_;
};

} // namespace clean_aot::soc
