#ifndef CLEAN_AOT_CDROM_HPP
#define CLEAN_AOT_CDROM_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace clean_aot::cdrom {

using Cycle = std::uint64_t;
using Lba = std::uint32_t;

inline constexpr std::size_t RawSectorBytes = 2352U;
inline constexpr std::size_t DataOnlyBytes = 2048U;
inline constexpr std::size_t WholeSectorBytes = 2340U;
inline constexpr std::size_t ModeOnePayloadBytes = 2048U;
inline constexpr std::size_t FormOnePayloadBytes = 2048U;
inline constexpr std::size_t FormTwoPayloadBytes = 2324U;
inline constexpr std::size_t XaAdpcmBytes = 2304U;
inline constexpr std::size_t CddaSamplesPerSector = 588U;
inline constexpr std::uint32_t CpuClockHz = 33868800U;
inline constexpr std::uint32_t SpuClockHz = 44100U;
inline constexpr std::uint32_t SectorsPerSecond = 75U;
inline constexpr std::uint32_t DoubleSpeedSectorsPerSecond = 150U;
inline constexpr Cycle SingleSpeedSectorCycles = CpuClockHz / SectorsPerSecond;
inline constexpr Cycle DoubleSpeedSectorCycles = CpuClockHz / DoubleSpeedSectorsPerSecond;
inline constexpr Cycle CyclesPer25HzUpdate = CpuClockHz / 25U;
inline constexpr Cycle CyclesPer50HzUpdate = CpuClockHz / 50U;
inline constexpr Cycle CyclesPer100HzUpdate = CpuClockHz / 100U;
inline constexpr std::uint32_t SamplesPer25HzUpdate = SpuClockHz / 25U;
inline constexpr std::uint32_t SamplesPer50HzUpdate = SpuClockHz / 50U;
inline constexpr std::uint32_t SamplesPer100HzUpdate = SpuClockHz / 100U;
inline constexpr Cycle CommandLatencyCycles = 33868U;
inline constexpr Cycle SeekLatencyCycles = 1354752U;
inline constexpr std::uint8_t MaximumBcdMinute = 99U;
inline constexpr Lba MaximumAddressableLba = 449849U;

static_assert(CpuClockHz % SectorsPerSecond == 0U);
static_assert(CpuClockHz % DoubleSpeedSectorsPerSecond == 0U);
static_assert(CpuClockHz % 25U == 0U);
static_assert(CpuClockHz % 50U == 0U);
static_assert(CpuClockHz % 100U == 0U);
static_assert(SpuClockHz % 25U == 0U);
static_assert(SpuClockHz % 50U == 0U);
static_assert(SpuClockHz % 100U == 0U);
static_assert(CddaSamplesPerSector * SectorsPerSecond == SpuClockHz);
static_assert(CyclesPer100HzUpdate * 4U == SingleSpeedSectorCycles * 3U);

struct RawSector final {
    std::array<std::uint8_t, RawSectorBytes> bytes{};
};

struct Msf final {
    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;

    friend constexpr bool operator==(const Msf&, const Msf&) noexcept = default;
};

struct StereoSample final {
    std::int16_t left = 0;
    std::int16_t right = 0;

    friend constexpr bool operator==(const StereoSample&, const StereoSample&) noexcept =
        default;
};

enum class SectorKind : std::uint8_t {
    ModeOne = 1U,
    ModeTwoFormOne = 2U,
    ModeTwoFormTwo = 3U,
};

enum class TrackKind : std::uint8_t {
    Data,
    Audio,
};

struct XaMetadata final {
    std::uint8_t file = 0U;
    std::uint8_t channel = 0U;
    std::uint8_t submode = 0U;
    std::uint8_t coding = 0U;
    bool form_two = false;
    bool audio = false;
    bool video = false;
    bool data = false;
    bool real_time = false;
    bool end_of_record = false;
    bool trigger = false;
    bool end_of_file = false;
};

struct SectorView final {
    bool valid = false;
    SectorKind kind = SectorKind::ModeOne;
    Lba lba = 0U;
    Msf address{};
    std::array<std::uint8_t, FormTwoPayloadBytes> payload{};
    std::uint16_t payload_size = 0U;
    XaMetadata xa{};
    std::uint32_t error_code = 0U;
};

enum class ErrorCode : std::uint8_t {
    None = 0U,
    InvalidRegister = 1U,
    InvalidBcd = 2U,
    InvalidMsf = 3U,
    ParameterUnderflow = 4U,
    ParameterOverflow = 5U,
    CommandOverflow = 6U,
    ResponseOverflow = 7U,
    DataOverflow = 8U,
    FifoUnderflow = 9U,
    UnsupportedCommand = 10U,
    InvalidTrack = 11U,
    MalformedSector = 12U,
    SourceReadFailure = 13U,
    SchedulerOverflow = 14U,
    TimeReversal = 15U,
    InvalidDisc = 16U,
    WrongParameterCount = 17U,
    CommandBusy = 18U,
    CannotRespond = 19U,
};

enum class CommandError : std::uint8_t {
    SeekFailed = 0x04U,
    InvalidParameter = 0x10U,
    WrongParameterCount = 0x20U,
    InvalidCommand = 0x40U,
    CannotRespond = 0x80U,
};

enum class Command : std::uint8_t {
    Nop = 0x01U,
    Getstat = Nop,
    Setloc = 0x02U,
    Play = 0x03U,
    Forward = 0x04U,
    Backward = 0x05U,
    ReadN = 0x06U,
    Standby = 0x07U,
    MotorOn = Standby,
    Stop = 0x08U,
    Pause = 0x09U,
    Init = 0x0aU,
    Mute = 0x0bU,
    Demute = 0x0cU,
    Setfilter = 0x0dU,
    Setmode = 0x0eU,
    Getparam = 0x0fU,
    GetlocL = 0x10U,
    GetlocP = 0x11U,
    SetSession = 0x12U,
    GetTN = 0x13U,
    GetTD = 0x14U,
    SeekL = 0x15U,
    SeekP = 0x16U,
    Test = 0x19U,
    ID = 0x1aU,
    ReadS = 0x1bU,
    Reset = 0x1cU,
    GetQ = 0x1dU,
    ReadToc = 0x1eU,
};

enum class Interrupt : std::uint8_t {
    None = 0U,
    DataReady = 1U,
    Complete = 2U,
    Acknowledge = 3U,
    DataEnd = 4U,
    Error = 5U,
};

constexpr std::uint8_t interrupt_code(Interrupt interrupt) noexcept {
    return static_cast<std::uint8_t>(interrupt);
}

enum class Status : std::uint8_t {
    Error = 1U << 0U,
    MotorOn = 1U << 1U,
    SeekError = 1U << 2U,
    IdError = 1U << 3U,
    ShellOpen = 1U << 4U,
    Reading = 1U << 5U,
    Seeking = 1U << 6U,
    Playing = 1U << 7U,
};

constexpr std::uint8_t status_bit(Status status) noexcept {
    return static_cast<std::uint8_t>(status);
}

struct Track final {
    std::uint8_t number = 1U;
    Lba start_lba = 0U;
    TrackKind kind = TrackKind::Data;
};

struct DiscLayout final {
    std::array<Track, 99U> tracks{};
    std::uint8_t track_count = 0U;
    Lba lead_out_lba = 0U;
    std::uint8_t disc_type = 0U;
    std::array<std::uint8_t, 4U> license_region{};

    bool valid() const noexcept;
    const Track* find_track(std::uint8_t number) const noexcept;
    const Track* track_for_lba(Lba lba) const noexcept;
    std::uint8_t track_at(Lba lba) const noexcept;
};

class RawSectorSource {
public:
    virtual ~RawSectorSource() = default;
    virtual bool read_raw_sector(Lba lba, RawSector& destination) noexcept = 0;
};

class DmaChannel3 {
public:
    virtual ~DmaChannel3() = default;
    virtual void receive_cdrom_data(std::span<const std::uint8_t> bytes) noexcept = 0;
};

enum class ScheduledEvent : std::uint8_t {
    CommandStart,
    CommandPhase,
    DataSector,
    CddaSector,
};

class MachineScheduler {
public:
    virtual ~MachineScheduler() = default;
    virtual void schedule_cdrom_event(Cycle cycle, ScheduledEvent event) noexcept = 0;
    virtual void cancel_cdrom_event(ScheduledEvent event) noexcept = 0;
};

class CddaSink {
public:
    virtual ~CddaSink() = default;
    virtual void receive_cdda_samples(Lba lba,
                                      std::span<const StereoSample> samples) noexcept = 0;
};

struct AudioMatrix final {
    std::uint8_t left_to_left = 0x80U;
    std::uint8_t right_to_left = 0U;
    std::uint8_t left_to_right = 0U;
    std::uint8_t right_to_right = 0x80U;

    friend constexpr bool operator==(const AudioMatrix&, const AudioMatrix&) noexcept = default;
};

class XaSectorSink {
public:
    virtual ~XaSectorSink() = default;
    virtual void receive_xa_sector(Lba lba,
                                   const XaMetadata& metadata,
                                   std::span<const std::uint8_t> adpcm_bytes,
                                   AudioMatrix matrix,
                                   bool muted) noexcept = 0;
};

class Cdrom final {
public:
    static constexpr std::size_t CommandFifoCapacity = 1U;
    static constexpr std::size_t ParameterFifoCapacity = 16U;
    static constexpr std::size_t ResponseFifoCapacity = 16U;
    static constexpr std::size_t DataFifoCapacity = WholeSectorBytes;
    static constexpr std::size_t InterruptQueueCapacity = 8U;
    static constexpr std::size_t EventCapacity = 64U;

    explicit Cdrom(RawSectorSource* source = nullptr, DiscLayout layout = {}) noexcept;

    void reset() noexcept;
    void attach_source(RawSectorSource* source) noexcept;
    void attach_dma(DmaChannel3* dma) noexcept;
    void attach_scheduler(MachineScheduler* scheduler) noexcept;
    void attach_cdda_sink(CddaSink* sink) noexcept;
    void attach_xa_sink(XaSectorSink* sink) noexcept;
    bool set_disc_layout(const DiscLayout& layout) noexcept;

    void advance_to(Cycle cycle) noexcept;
    Cycle current_cycle() const noexcept;

    std::uint8_t read_register(std::uint8_t offset) noexcept;
    std::uint16_t read_register16(std::uint8_t offset) noexcept;
    std::uint32_t read_register32(std::uint8_t offset) noexcept;
    void write_register(std::uint8_t offset, std::uint8_t value) noexcept;

    bool enqueue_command(std::uint8_t command) noexcept;
    bool enqueue_parameter(std::uint8_t value) noexcept;
    std::uint8_t pop_response() noexcept;
    std::uint8_t pop_data() noexcept;
    void acknowledge_interrupts(std::uint8_t mask) noexcept;

    std::uint8_t status() const noexcept;
    std::uint8_t interrupt_flags() const noexcept;
    std::uint8_t interrupt_enable() const noexcept;
    bool irq_line() const noexcept;
    ErrorCode last_error() const noexcept;
    std::uint8_t selected_register_bank() const noexcept;

    std::size_t command_count() const noexcept;
    std::size_t parameter_count() const noexcept;
    std::size_t response_count() const noexcept;
    std::size_t data_count() const noexcept;
    std::size_t pending_interrupt_count() const noexcept;

    Lba current_lba() const noexcept;
    Msf current_msf() const noexcept;
    bool reading() const noexcept;
    bool playing() const noexcept;
    bool muted() const noexcept;
    std::uint8_t mode() const noexcept;
    AudioMatrix audio_matrix() const noexcept;
    void set_audio_matrix(AudioMatrix matrix) noexcept;
    void set_muted(bool muted) noexcept;
    void set_dma_enabled(bool enabled) noexcept;

    static std::uint8_t to_bcd(std::uint8_t value) noexcept;
    static bool from_bcd(std::uint8_t value, std::uint8_t& decoded) noexcept;
    static bool decode_msf_bcd(std::span<const std::uint8_t, 3U> bytes,
                               Msf& result) noexcept;
    static Msf lba_to_msf(Lba lba) noexcept;
    static Msf frames_to_msf(Lba frames) noexcept;
    static bool msf_to_lba(const Msf& msf, Lba& lba) noexcept;
    static bool decode_data_sector(Lba lba,
                                   const RawSector& raw,
                                   SectorView& result) noexcept;
    static std::uint32_t edc(const std::uint8_t* bytes, std::size_t length) noexcept;

private:
    enum class PhaseAction : std::uint8_t {
        None,
        MotorOn,
        Stop,
        Pause,
        Init,
        SetSession,
        Seek,
        Id,
        ReadToc,
    };

    struct Event final {
        Cycle cycle = 0U;
        ScheduledEvent kind = ScheduledEvent::CommandStart;
        std::uint64_t sequence = 0U;
    };

    struct InterruptPacket final {
        Interrupt reason = Interrupt::None;
        std::array<std::uint8_t, ResponseFifoCapacity> bytes{};
        std::uint8_t size = 0U;
    };

    RawSectorSource* source_ = nullptr;
    DmaChannel3* dma_ = nullptr;
    MachineScheduler* scheduler_ = nullptr;
    CddaSink* cdda_sink_ = nullptr;
    XaSectorSink* xa_sink_ = nullptr;
    DiscLayout layout_{};
    Cycle cycle_ = 0U;
    std::uint64_t event_sequence_ = 0U;
    std::array<Event, EventCapacity> events_{};
    std::size_t event_count_ = 0U;

    std::array<std::uint8_t, ParameterFifoCapacity> parameter_storage_{};
    std::size_t parameter_count_ = 0U;
    std::uint8_t pending_command_ = 0U;
    std::array<std::uint8_t, ParameterFifoCapacity> pending_parameters_{};
    std::size_t pending_parameter_count_ = 0U;
    bool command_pending_ = false;
    bool command_busy_ = false;
    bool command_start_scheduled_ = false;

    std::array<std::uint8_t, ResponseFifoCapacity> response_storage_{};
    std::size_t response_length_ = 0U;
    std::size_t response_cursor_ = 0U;
    std::size_t response_unread_ = 0U;
    Interrupt interrupt_reason_ = Interrupt::None;
    std::array<InterruptPacket, InterruptQueueCapacity> interrupt_queue_{};
    std::size_t interrupt_queue_count_ = 0U;

    std::array<std::uint8_t, DataFifoCapacity> transfer_storage_{};
    std::size_t transfer_size_ = 0U;
    bool transfer_valid_ = false;
    std::array<std::uint8_t, DataFifoCapacity> data_storage_{};
    std::size_t data_size_ = 0U;
    std::size_t data_cursor_ = 0U;

    std::uint8_t register_bank_ = 0U;
    std::uint8_t interrupt_enable_ = 0U;
    ErrorCode last_error_ = ErrorCode::None;
    std::uint8_t mode_ = 0U;
    std::uint8_t coding_info_ = 0U;
    AudioMatrix pending_audio_matrix_{};
    AudioMatrix audio_matrix_{};
    bool muted_ = false;
    bool xa_host_muted_ = false;
    bool dma_enabled_ = false;
    bool request_data_ = false;
    bool whole_sector_ = false;
    bool location_pending_ = false;
    bool reading_ = false;
    bool playing_ = false;
    bool seeking_ = false;
    bool paused_ = false;
    bool motor_on_ = false;
    PhaseAction phase_action_ = PhaseAction::None;
    std::uint8_t filter_file_ = 0U;
    std::uint8_t filter_channel_ = 0U;
    std::uint8_t play_origin_track_ = 0U;
    Lba current_lba_ = 0U;
    Lba target_lba_ = 0U;
    Lba last_position_lba_ = 0U;
    bool last_position_valid_ = false;
    std::array<std::uint8_t, 8U> last_location_{};
    bool last_location_valid_ = false;

    void record_error(ErrorCode error) noexcept;
    bool schedule_event(Cycle cycle, ScheduledEvent event) noexcept;
    void remove_events(ScheduledEvent event) noexcept;
    void pump_command() noexcept;
    void process_event(const Event& event) noexcept;
    void begin_command(std::uint8_t command,
                       std::span<const std::uint8_t> parameters) noexcept;
    bool start_phase(PhaseAction action, Cycle delay) noexcept;
    void complete_phase() noexcept;

    bool queue_interrupt(Interrupt reason, std::span<const std::uint8_t> bytes) noexcept;
    bool queue_status(Interrupt reason) noexcept;
    void queue_command_error(ErrorCode diagnostic, CommandError error) noexcept;
    void activate_packet(const InterruptPacket& packet) noexcept;
    void activate_next_interrupt() noexcept;

    void schedule_next_data_sector(Cycle from) noexcept;
    void schedule_next_cdda_sector(Cycle from) noexcept;
    void process_data_sector() noexcept;
    void process_cdda_sector() noexcept;
    void stop_streams() noexcept;
    bool prepare_transfer(std::span<const std::uint8_t> bytes) noexcept;
    void latch_transfer() noexcept;
    void end_data_stream() noexcept;
    void end_cdda_stream() noexcept;

    std::uint8_t status_value() const noexcept;
    std::uint8_t host_status_value() const noexcept;
    std::uint8_t current_track() const noexcept;
    static bool exact_parameter_count(std::span<const std::uint8_t> parameters,
                                      std::size_t expected) noexcept;
    static bool decode_parameter_msf(std::span<const std::uint8_t> parameters,
                                     Lba& lba) noexcept;
    static std::int16_t signed_sample(std::uint16_t value) noexcept;
    static std::int16_t saturate_audio(std::int64_t value) noexcept;
    static std::int16_t mix_audio(std::int32_t left,
                                  std::int32_t right,
                                  std::uint8_t left_gain,
                                  std::uint8_t right_gain) noexcept;
};

} // namespace clean_aot::cdrom

#endif // CLEAN_AOT_CDROM_HPP
