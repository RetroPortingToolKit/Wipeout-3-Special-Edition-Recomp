#ifndef CLEAN_AOT_SIO_SIO_HPP
#define CLEAN_AOT_SIO_SIO_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace clean_aot::sio {

using Cycle = std::uint64_t;

enum class MmioWidth : std::uint8_t {
    Byte = 1,
    Halfword = 2,
    Word = 4,
};

enum class MmioError : std::uint8_t {
    None = 0,
    Misaligned,
    UnsupportedWidth,
    UnknownRegister,
    TimeWentBackwards,
    CycleOverflow,
    DeviceDisabled,
    ReceiveEmpty,
    InvalidImage,
    SnapshotMismatch,
    SnapshotInvalid,
    NoDevice,
    DeviceConflict,
    UnsupportedValue,
    UnsupportedMode,
};

struct MmioReadResult {
    bool accepted = false;
    std::uint32_t value = 0;
    MmioError error = MmioError::None;
};

struct MmioWriteResult {
    bool accepted = false;
    bool overwritten = false;
    MmioError error = MmioError::None;
};

enum class Port : std::uint8_t {
    One = 0,
    Two = 1,
};

enum class DeviceKind : std::uint8_t {
    None = 0,
    Controller = 1,
    MemoryCard = 2,
};

struct ByteTransfer {
    std::uint8_t value = 0xFFU;
    bool acknowledged = false;
    Cycle acknowledge_delay = 0;
    Cycle acknowledge_width = 0;
};

// Nominal Sony-compatible timings from the pinned behavioral reference. The
// device result carries these values explicitly so machine integration can
// replace them when hardware traces establish a more specific device profile.
inline constexpr Cycle kNominalAckDelayCycles = 400U;
inline constexpr Cycle kNominalAckPulseCycles = 100U;
inline constexpr Cycle kMemoryCardReadLateAckExtraCycles = 31'000U;
inline constexpr Cycle kMaximumAckLeadCycles = kNominalAckDelayCycles +
                                                kMemoryCardReadLateAckExtraCycles +
                                                kNominalAckPulseCycles;
inline constexpr Cycle kNoEventCycle = std::numeric_limits<Cycle>::max();

// SIO0 is mapped by the machine layer. These are offsets, not absolute guest
// addresses, so this component can be reused by any PS1 memory map.
inline constexpr std::uint32_t kDataOffset = 0x00U;
inline constexpr std::uint32_t kStatusOffset = 0x04U;
inline constexpr std::uint32_t kModeOffset = 0x08U;
inline constexpr std::uint32_t kControlOffset = 0x0AU;
inline constexpr std::uint32_t kBaudOffset = 0x0EU;

inline constexpr std::uint16_t kModeFactorMask = 0x0003U;
inline constexpr std::uint16_t kModeLengthMask = 0x000CU;
inline constexpr std::uint16_t kModeParityEnable = 0x0010U;
inline constexpr std::uint16_t kModeParityOdd = 0x0020U;
inline constexpr std::uint16_t kModeClockPolarity = 0x0100U;
inline constexpr std::uint16_t kSio0ModeWritableMask =
    kModeFactorMask | kModeLengthMask | kModeParityEnable |
    kModeParityOdd | kModeClockPolarity;

inline constexpr std::uint16_t kControlTxEnable = 0x0001U;
inline constexpr std::uint16_t kControlDtr = 0x0002U;
inline constexpr std::uint16_t kControlRxEnable = 0x0004U;
inline constexpr std::uint16_t kControlAcknowledge = 0x0010U;
inline constexpr std::uint16_t kControlReset = 0x0040U;
inline constexpr std::uint16_t kControlRxInterruptModeMask = 0x0300U;
inline constexpr std::uint16_t kControlTxInterruptEnable = 0x0400U;
inline constexpr std::uint16_t kControlRxInterruptEnable = 0x0800U;
inline constexpr std::uint16_t kControlDsrInterruptEnable = 0x1000U;
inline constexpr std::uint16_t kControlPortTwo = 0x2000U;

inline constexpr std::uint16_t kStatusTxReady = 0x0001U;
inline constexpr std::uint16_t kStatusRxReady = 0x0002U;
inline constexpr std::uint16_t kStatusTxIdle = 0x0004U;
inline constexpr std::uint16_t kStatusParityError = 0x0008U;
inline constexpr std::uint16_t kStatusRxOverrun = 0x0010U;
inline constexpr std::uint16_t kStatusBadStop = 0x0020U;
inline constexpr std::uint16_t kStatusRxInput = 0x0040U;
inline constexpr std::uint16_t kStatusDsr = 0x0080U;
inline constexpr std::uint16_t kStatusCts = 0x0100U;
inline constexpr std::uint16_t kStatusInterrupt = 0x0200U;

class SioDevice {
public:
    virtual ~SioDevice() = default;

    [[nodiscard]] virtual DeviceKind kind() const noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void on_select(Cycle cycle) noexcept = 0;
    virtual void on_deselect(Cycle cycle) noexcept = 0;
    [[nodiscard]] virtual ByteTransfer exchange(std::uint8_t value,
                                                Cycle cycle) noexcept = 0;
};

struct PadInput {
    // Buttons use the PS1 active-low wire encoding: zero means pressed.
    std::uint16_t buttons = 0xFFFFU;
    std::uint8_t right_x = 0x80U;
    std::uint8_t right_y = 0x80U;
    std::uint8_t left_x = 0x80U;
    std::uint8_t left_y = 0x80U;
};

enum class PadButton : std::uint16_t {
    Select = 0x0001U,
    L3 = 0x0002U,
    R3 = 0x0004U,
    Start = 0x0008U,
    Up = 0x0010U,
    Right = 0x0020U,
    Down = 0x0040U,
    Left = 0x0080U,
    L2 = 0x0100U,
    R2 = 0x0200U,
    L1 = 0x0400U,
    R1 = 0x0800U,
    Triangle = 0x1000U,
    Circle = 0x2000U,
    Cross = 0x4000U,
    Square = 0x8000U,
};

struct PadState {
    PadInput input{};
    bool analog_mode = false;
    bool configuration_mode = false;
    bool mode_locked = false;
    bool led_on = false;
    bool rumble_configured = false;
    std::array<std::uint8_t, 6> rumble_map{};
    std::uint8_t rumble_small = 0;
    std::uint8_t rumble_large = 0;
    std::uint8_t error_count = 0;
};

struct PadSnapshot {
    PadState state{};
    bool selected = false;
    bool addressed = false;
    std::uint8_t command = 0;
    std::uint8_t phase = 0;
    std::uint8_t pending_mode = 0;
    std::uint8_t pending_key = 0;
    bool command_valid = false;
    bool transaction_done = false;
};

class PadDevice final : public SioDevice {
public:
    PadDevice() noexcept;

    [[nodiscard]] DeviceKind kind() const noexcept override;
    void reset() noexcept override;
    void on_select(Cycle cycle) noexcept override;
    void on_deselect(Cycle cycle) noexcept override;
    [[nodiscard]] ByteTransfer exchange(std::uint8_t value,
                                        Cycle cycle) noexcept override;

    void set_input(const PadInput& input) noexcept;
    [[nodiscard]] PadState state() const noexcept;
    [[nodiscard]] PadSnapshot snapshot() const noexcept;
    [[nodiscard]] bool restore(const PadSnapshot& snapshot) noexcept;

    // This is the host/input boundary for a physical Analog-button change.
    // It has no dependency on a windowing or controller API.
    [[nodiscard]] bool set_analog_mode(bool enabled) noexcept;

private:
    [[nodiscard]] std::uint8_t normal_id() const noexcept;
    [[nodiscard]] std::uint8_t response_id() const noexcept;
    [[nodiscard]] std::uint8_t normal_final_phase() const noexcept;
    [[nodiscard]] std::uint8_t analog_or_padding(std::uint8_t phase) const noexcept;
    [[nodiscard]] ByteTransfer exchange_normal(std::uint8_t value,
                                               Cycle cycle) noexcept;
    [[nodiscard]] ByteTransfer exchange_configuration(std::uint8_t value,
                                                       Cycle cycle) noexcept;
    [[nodiscard]] ByteTransfer response(std::uint8_t value,
                                        std::uint8_t final_phase) noexcept;
    void capture_rumble(std::uint8_t value) noexcept;
    void finish_command() noexcept;

    PadState state_{};
    bool selected_ = false;
    bool addressed_ = false;
    std::uint8_t command_ = 0;
    std::uint8_t phase_ = 0;
    std::uint8_t pending_mode_ = 0;
    std::uint8_t pending_key_ = 0;
    bool command_valid_ = false;
    bool transaction_done_ = false;
};

struct WriteReceipt {
    std::uint16_t sector = 0;
    std::uint32_t generation = 0;
    std::uint64_t image_fingerprint = 0;
    std::uint8_t checksum = 0;
};

using WriteDurabilityCallback = void (*)(void*, const WriteReceipt&) noexcept;

struct MemoryCardSnapshot {
    std::uint64_t image_fingerprint = 0;
    std::uint64_t protocol_fingerprint = 0;
    std::uint32_t write_generation = 0;
    std::uint32_t error_count = 0;
    std::uint16_t address = 0;
    std::uint8_t flag = 0x08U;
    std::uint8_t phase = 0;
    std::uint8_t data_index = 0;
    std::uint8_t checksum = 0;
    std::uint8_t received_checksum = 0;
    std::uint8_t previous_value = 0;
    bool selected = false;
    bool valid_sector = false;
    bool pending_write_valid = false;
    std::array<std::uint8_t, 128> pending_data{};
};

class MemoryCard final : public SioDevice {
public:
    static constexpr std::size_t kImageSize = std::size_t{128U} * 1024U;
    static constexpr std::size_t kSectorSize = 128U;
    static constexpr std::size_t kSectorCount = kImageSize / kSectorSize;

    explicit MemoryCard(std::span<std::uint8_t> image) noexcept;

    [[nodiscard]] DeviceKind kind() const noexcept override;
    void reset() noexcept override;
    void on_select(Cycle cycle) noexcept override;
    void on_deselect(Cycle cycle) noexcept override;
    [[nodiscard]] ByteTransfer exchange(std::uint8_t value,
                                        Cycle cycle) noexcept override;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t image_fingerprint() const noexcept;
    [[nodiscard]] std::uint32_t write_generation() const noexcept;
    [[nodiscard]] std::uint32_t error_count() const noexcept;
    [[nodiscard]] std::uint8_t flag() const noexcept;
    void set_write_callback(WriteDurabilityCallback callback,
                            void* context) noexcept;

    [[nodiscard]] MemoryCardSnapshot snapshot() const noexcept;
    [[nodiscard]] bool restore(const MemoryCardSnapshot& snapshot) noexcept;

    // Creates a deterministic, protocol-ready raw card image. It does not
    // create a title save or a game-specific directory entry.
    [[nodiscard]] static bool format_new_image(std::span<std::uint8_t> image) noexcept;

private:
    enum class Phase : std::uint8_t {
        Idle = 0,
        Command,
        ReadIdOne,
        ReadIdTwo,
        ReadAddressHigh,
        ReadAddressLow,
        ReadConfirmOne,
        ReadConfirmTwo,
        ReadConfirmHigh,
        ReadConfirmLow,
        ReadData,
        ReadChecksum,
        ReadEnd,
        WriteIdOne,
        WriteIdTwo,
        WriteAddressHigh,
        WriteAddressLow,
        WriteData,
        WriteChecksum,
        WriteConfirmOne,
        WriteConfirmTwo,
        WriteEnd,
        IdDeviceOne,
        IdDeviceTwo,
        IdConfirmOne,
        IdConfirmTwo,
        IdByteZero,
        IdByteOne,
        IdByteTwo,
        IdByteThree,
        Done,
    };

    [[nodiscard]] ByteTransfer response(std::uint8_t value,
                                        bool more_data,
                                        Cycle extra_ack_delay = 0) noexcept;
    [[nodiscard]] bool valid_address() const noexcept;
    [[nodiscard]] std::size_t image_offset() const noexcept;
    [[nodiscard]] std::uint64_t protocol_fingerprint() const noexcept;
    void commit_pending_write() noexcept;
    void fail_command() noexcept;
    void clear_transaction() noexcept;

    std::span<std::uint8_t> image_{};
    WriteDurabilityCallback write_callback_ = nullptr;
    void* write_context_ = nullptr;
    Phase phase_ = Phase::Idle;
    bool selected_ = false;
    bool valid_sector_ = false;
    bool pending_write_valid_ = false;
    std::uint16_t address_ = 0;
    std::uint8_t data_index_ = 0;
    std::uint8_t checksum_ = 0;
    std::uint8_t received_checksum_ = 0;
    std::uint8_t previous_value_ = 0;
    std::uint8_t flag_ = 0x08U;
    std::uint32_t write_generation_ = 0;
    std::uint32_t error_count_ = 0;
    std::array<std::uint8_t, kSectorSize> pending_data_{};
};

struct SioSnapshot {
    struct TxEntry {
        std::uint8_t value = 0;
        bool tx_enabled_when_written = false;
    };

    Cycle cycle = 0;
    Cycle shift_complete = 0;
    Cycle shift_bit_period = 0;
    Cycle baud_timer_value = 0;
    Cycle ack_begin = 0;
    Cycle ack_until = 0;
    std::uint16_t mode = 0;
    std::uint16_t control = 0;
    std::uint16_t baud = 0;
    std::uint8_t tx_head = 0;
    std::uint8_t tx_count = 0;
    std::uint8_t rx_head = 0;
    std::uint8_t rx_count = 0;
    bool shift_active = false;
    bool baud_timer_running = false;
    bool ack_pending = false;
    bool dsr = false;
    bool irq = false;
    bool parity_error = false;
    bool rx_overrun = false;
    bool bad_stop = false;
    bool forced_receive = false;
    bool addressing = true;
    bool device_conflict = false;
    bool ack_collision = false;
    Port selected_port = Port::One;
    DeviceKind active_device = DeviceKind::None;
    std::array<TxEntry, 2> tx_fifo{};
    std::array<std::uint8_t, 8> rx_fifo{};
};

class Sio0 final {
public:
    static constexpr std::size_t kTxCapacity = 2U;
    static constexpr std::size_t kRxCapacity = 8U;
    static constexpr Cycle kCpuClockHz = 33'868'800U;
    static constexpr Cycle kDefaultAckDelayCycles = kNominalAckDelayCycles;
    static constexpr Cycle kDefaultAckPulseCycles = kNominalAckPulseCycles;

    struct Callbacks {
        void (*irq)(void*, bool) noexcept = nullptr;
        void* context = nullptr;
    };

    Sio0() noexcept;
    explicit Sio0(Callbacks callbacks) noexcept;

    [[nodiscard]] bool attach_controller(Port port, PadDevice& device) noexcept;
    [[nodiscard]] bool attach_memory_card(Port port, MemoryCard& device) noexcept;

    [[nodiscard]] MmioReadResult read(std::uint32_t offset,
                                      MmioWidth width,
                                      Cycle cycle) noexcept;
    [[nodiscard]] MmioWriteResult write(std::uint32_t offset,
                                         MmioWidth width,
                                         std::uint32_t value,
                                         Cycle cycle) noexcept;
    [[nodiscard]] bool advance_to(Cycle cycle) noexcept;

    // This convenience method models the two SIO0 /CS inputs without making
    // the machine layer know the control-bit encoding.
    [[nodiscard]] MmioWriteResult select(Port port, Cycle cycle) noexcept;
    [[nodiscard]] MmioWriteResult deselect(Cycle cycle) noexcept;

    void reset(Cycle cycle = 0) noexcept;
    [[nodiscard]] std::uint32_t status() const noexcept;
    [[nodiscard]] std::uint16_t mode() const noexcept;
    [[nodiscard]] std::uint16_t control() const noexcept;
    [[nodiscard]] std::uint16_t baud() const noexcept;
    [[nodiscard]] Cycle cycle() const noexcept;
    [[nodiscard]] Cycle cycles_per_bit() const noexcept;
    [[nodiscard]] Cycle cycles_per_byte() const noexcept;
    [[nodiscard]] Cycle next_event_cycle() const noexcept;
    [[nodiscard]] bool irq_pending() const noexcept;
    [[nodiscard]] bool dsr_active() const noexcept;
    [[nodiscard]] bool ack_collision() const noexcept;
    [[nodiscard]] bool rx_overrun() const noexcept;
    [[nodiscard]] MmioError last_error() const noexcept;
    [[nodiscard]] SioSnapshot snapshot() const noexcept;
    [[nodiscard]] bool restore(const SioSnapshot& snapshot) noexcept;

private:
    struct Slot {
        SioDevice* controller = nullptr;
        SioDevice* memory_card = nullptr;
    };

    [[nodiscard]] static bool valid_port(Port port) noexcept;
    [[nodiscard]] SioDevice* active_device() const noexcept;
    [[nodiscard]] SioDevice* device_for(Port port, DeviceKind kind) const noexcept;
    [[nodiscard]] bool validate_mmio(std::uint32_t offset,
                                     MmioWidth width,
                                     MmioWidth expected) noexcept;
    [[nodiscard]] std::uint32_t status_value() const noexcept;
    [[nodiscard]] Cycle baud_factor() const noexcept;
    [[nodiscard]] Cycle baud_timer_reload() const noexcept;
    [[nodiscard]] Cycle baud_timer() const noexcept;
    [[nodiscard]] bool device_mode_supported() const noexcept;
    [[nodiscard]] static bool event_time_fits(Cycle base, Cycle delay) noexcept;
    [[nodiscard]] bool queued_transfer_window_fits() const noexcept;
    [[nodiscard]] std::uint8_t rx_threshold() const noexcept;
    [[nodiscard]] bool rx_condition() const noexcept;
    [[nodiscard]] bool tx_condition() const noexcept;
    [[nodiscard]] bool dsr_condition() const noexcept;
    void update_irq() noexcept;
    void set_irq(bool enabled) noexcept;
    void apply_control(std::uint16_t value, Cycle cycle) noexcept;
    void begin_bus_select(Cycle cycle) noexcept;
    void end_bus_select(Cycle cycle) noexcept;
    [[nodiscard]] bool try_start_transfer() noexcept;
    [[nodiscard]] bool complete_transfer() noexcept;
    [[nodiscard]] bool schedule_ack(const ByteTransfer& transfer) noexcept;
    void begin_ack() noexcept;
    void expire_ack() noexcept;
    void advance_baud_timer(Cycle elapsed) noexcept;
    void enqueue_rx(std::uint8_t value) noexcept;
    void enqueue_tx(std::uint8_t value, bool tx_enabled) noexcept;
    [[nodiscard]] bool tx_can_start() const noexcept;
    [[nodiscard]] bool receive_allowed() const noexcept;
    [[nodiscard]] MmioWriteResult write_control(std::uint16_t value,
                                                Cycle cycle) noexcept;
    [[nodiscard]] MmioWriteResult write_data(std::uint8_t value,
                                              Cycle cycle) noexcept;

    Callbacks callbacks_{};
    std::array<Slot, 2> slots_{};
    Cycle cycle_ = 0;
    Cycle shift_complete_ = 0;
    Cycle shift_bit_period_ = 0;
    Cycle baud_timer_value_ = 0;
    Cycle ack_begin_ = 0;
    Cycle ack_until_ = 0;
    std::uint16_t mode_ = 0;
    std::uint16_t control_ = 0;
    std::uint16_t baud_ = 0;
    std::uint8_t tx_head_ = 0;
    std::uint8_t tx_count_ = 0;
    std::uint8_t rx_head_ = 0;
    std::uint8_t rx_count_ = 0;
    bool shift_active_ = false;
    bool baud_timer_running_ = false;
    bool ack_pending_ = false;
    bool dsr_ = false;
    bool irq_ = false;
    bool parity_error_ = false;
    bool rx_overrun_ = false;
    bool bad_stop_ = false;
    bool forced_receive_ = false;
    bool addressing_ = true;
    bool device_conflict_ = false;
    bool ack_collision_ = false;
    MmioError last_error_ = MmioError::None;
    Port selected_port_ = Port::One;
    DeviceKind active_kind_ = DeviceKind::None;
    std::array<SioSnapshot::TxEntry, kTxCapacity> tx_fifo_{};
    std::array<std::uint8_t, kRxCapacity> rx_fifo_{};
};

} // namespace clean_aot::sio

#endif // CLEAN_AOT_SIO_SIO_HPP
