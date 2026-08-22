#include "cdrom.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace clean_aot::cdrom {

namespace {

constexpr std::uint8_t ModeDoubleSpeed = 0x80U;
constexpr std::uint8_t ModeXaAdpcm = 0x40U;
constexpr std::uint8_t ModeWholeSector = 0x20U;
constexpr std::uint8_t ModeIgnoreSectorSize = 0x10U;
constexpr std::uint8_t ModeXaFilter = 0x08U;
constexpr std::uint8_t ModeReport = 0x04U;
constexpr std::uint8_t ModeAutoPause = 0x02U;
constexpr std::uint8_t ModeReadCdda = 0x01U;
constexpr std::uint8_t SyncFirst = 0x00U;
constexpr std::uint8_t SyncMiddle = 0xffU;
constexpr std::uint8_t SyncLast = 0x00U;
constexpr std::uint8_t XaFormTwo = 0x20U;
constexpr std::uint8_t XaAudio = 0x04U;
constexpr std::uint8_t XaVideo = 0x02U;
constexpr std::uint8_t XaData = 0x08U;
constexpr std::uint8_t XaRealTime = 0x40U;
constexpr std::uint8_t XaEndOfRecord = 0x01U;
constexpr std::uint8_t XaTrigger = 0x10U;
constexpr std::uint8_t XaEndOfFile = 0x80U;
constexpr std::size_t HeaderModeOffset = 15U;
constexpr std::uint8_t ModeOneValue = 1U;
constexpr std::uint8_t ModeTwoValue = 2U;
constexpr std::size_t SubheaderOffset = 16U;
constexpr std::size_t PayloadOffset = 24U;
constexpr std::size_t ModeOnePayloadOffset = 16U;
constexpr std::size_t ModeOneEdcOffset = 2064U;
constexpr std::size_t FormOneEdcOffset = 2072U;
constexpr std::size_t FormTwoEdcOffset = 2348U;
constexpr std::uint8_t HostReservedReadBits = 0xe0U;
constexpr std::uint8_t LeadOutTrack = 0xaaU;
constexpr std::uint8_t UnknownTrack = 0xffU;
constexpr Lba FramesPerMinute = 60U * SectorsPerSecond;
constexpr Lba MaximumAbsoluteFrame =
    static_cast<Lba>(MaximumBcdMinute) * FramesPerMinute +
    59U * SectorsPerSecond + 74U;

constexpr bool add_without_overflow(Cycle base, Cycle amount, Cycle& result) noexcept {
    if (amount > std::numeric_limits<Cycle>::max() - base) {
        return false;
    }
    result = base + amount;
    return true;
}

constexpr bool raw_sync_is_valid(const RawSector& raw) noexcept {
    if (raw.bytes[0U] != SyncFirst || raw.bytes[11U] != SyncLast) {
        return false;
    }
    for (std::size_t index = 1U; index < 11U; ++index) {
        if (raw.bytes[index] != SyncMiddle) {
            return false;
        }
    }
    return true;
}

constexpr std::uint32_t stored_little_endian(const RawSector& raw,
                                             std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(raw.bytes[offset]) |
           (static_cast<std::uint32_t>(raw.bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(raw.bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(raw.bytes[offset + 3U]) << 24U);
}

constexpr bool region_is_present(const DiscLayout& layout) noexcept {
    return std::all_of(layout.license_region.begin(),
                       layout.license_region.end(),
                       [](std::uint8_t value) { return value != 0U; });
}

} // namespace

bool DiscLayout::valid() const noexcept {
    const std::size_t count = static_cast<std::size_t>(track_count);
    if (count == 0U || count > tracks.size() || lead_out_lba > MaximumAddressableLba) {
        return false;
    }
    if (tracks[0U].start_lba != 0U) {
        return false;
    }
    Lba previous = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        const Track& track = tracks[index];
        const std::uint8_t expected_number = static_cast<std::uint8_t>(index + 1U);
        if (track.number != expected_number || track.start_lba >= lead_out_lba) {
            return false;
        }
        if (index != 0U && track.start_lba <= previous) {
            return false;
        }
        if (track.has_index_zero &&
            (index == 0U || track.index_zero_lba >= track.start_lba ||
             track.index_zero_lba <= previous)) {
            return false;
        }
        previous = track.start_lba;
    }
    return lead_out_lba > previous;
}

const Track* DiscLayout::find_track(std::uint8_t number) const noexcept {
    const std::size_t count = std::min<std::size_t>(track_count, tracks.size());
    for (std::size_t index = 0U; index < count; ++index) {
        if (tracks[index].number == number) {
            return &tracks[index];
        }
    }
    return nullptr;
}

const Track* DiscLayout::track_for_lba(Lba lba) const noexcept {
    const std::size_t count = std::min<std::size_t>(track_count, tracks.size());
    if (count == 0U || lba >= lead_out_lba || lba < tracks[0U].start_lba) {
        return nullptr;
    }
    const Track* result = &tracks[0U];
    for (std::size_t index = 1U; index < count; ++index) {
        const Lba boundary = tracks[index].has_index_zero
                                 ? tracks[index].index_zero_lba
                                 : tracks[index].start_lba;
        if (boundary > lba) {
            break;
        }
        result = &tracks[index];
    }
    return result;
}

std::uint8_t DiscLayout::track_at(Lba lba) const noexcept {
    if (track_count == 0U || static_cast<std::size_t>(track_count) > tracks.size()) {
        return UnknownTrack;
    }
    if (lba >= lead_out_lba) {
        return LeadOutTrack;
    }
    const Track* track = track_for_lba(lba);
    return track == nullptr ? UnknownTrack : track->number;
}

Cdrom::Cdrom(RawSectorSource* source, DiscLayout layout) noexcept
    : source_(source), layout_(layout.valid() ? layout : DiscLayout{}) {
    reset();
}

void Cdrom::reset() noexcept {
    const bool dma_enabled = dma_enabled_;
    if (scheduler_ != nullptr) {
        scheduler_->cancel_cdrom_event(ScheduledEvent::CommandStart);
        scheduler_->cancel_cdrom_event(ScheduledEvent::CommandPhase);
        scheduler_->cancel_cdrom_event(ScheduledEvent::DataSector);
        scheduler_->cancel_cdrom_event(ScheduledEvent::CddaSector);
    }
    event_sequence_ = 0U;
    events_ = {};
    event_count_ = 0U;
    parameter_storage_ = {};
    parameter_count_ = 0U;
    pending_command_ = 0U;
    pending_parameters_ = {};
    pending_parameter_count_ = 0U;
    command_pending_ = false;
    command_busy_ = false;
    command_start_scheduled_ = false;
    response_storage_ = {};
    response_length_ = 0U;
    response_cursor_ = 0U;
    response_unread_ = 0U;
    interrupt_reason_ = Interrupt::None;
    interrupt_queue_ = {};
    interrupt_queue_count_ = 0U;
    clear_transfer_buffers();
    data_storage_ = {};
    data_size_ = 0U;
    data_cursor_ = 0U;
    register_bank_ = 0U;
    interrupt_enable_ = 0U;
    last_error_ = ErrorCode::None;
    mode_ = 0U;
    coding_info_ = 0U;
    pending_audio_matrix_ = AudioMatrix{};
    audio_matrix_ = AudioMatrix{};
    muted_ = false;
    xa_host_muted_ = false;
    dma_enabled_ = dma_enabled;
    request_data_ = false;
    whole_sector_ = false;
    location_pending_ = false;
    reading_ = false;
    playing_ = false;
    seeking_ = false;
    paused_ = false;
    motor_on_ = false;
    phase_action_ = PhaseAction::None;
    filter_file_ = 0U;
    filter_channel_ = 0U;
    play_origin_track_ = 0U;
    current_lba_ = layout_.valid() ? layout_.tracks[0U].start_lba : 0U;
    target_lba_ = current_lba_;
    last_position_lba_ = current_lba_;
    last_position_valid_ = false;
    last_location_ = {};
    last_location_valid_ = false;
}

void Cdrom::attach_source(RawSectorSource* source) noexcept {
    if (source_ == source) {
        return;
    }
    if (command_pending_ || command_busy_ || command_start_scheduled_ ||
        phase_action_ != PhaseAction::None || reading_ || playing_ || seeking_ ||
        interrupt_reason_ != Interrupt::None || interrupt_queue_count_ != 0U) {
        record_error(ErrorCode::CommandBusy);
        return;
    }
    stop_streams();
    remove_events(ScheduledEvent::CommandPhase);
    phase_action_ = PhaseAction::None;
    seeking_ = false;
    source_ = source;
    clear_transfer_buffers();
    data_size_ = 0U;
    data_cursor_ = 0U;
    last_position_valid_ = false;
    last_location_valid_ = false;
}

void Cdrom::attach_dma(DmaChannel3* dma) noexcept {
    dma_ = dma;
}

void Cdrom::attach_scheduler(MachineScheduler* scheduler) noexcept {
    if (scheduler_ == scheduler) {
        return;
    }
    if (scheduler_ != nullptr) {
        scheduler_->cancel_cdrom_event(ScheduledEvent::CommandStart);
        scheduler_->cancel_cdrom_event(ScheduledEvent::CommandPhase);
        scheduler_->cancel_cdrom_event(ScheduledEvent::DataSector);
        scheduler_->cancel_cdrom_event(ScheduledEvent::CddaSector);
    }
    scheduler_ = scheduler;
    if (scheduler_ != nullptr) {
        for (std::size_t index = 0U; index < event_count_; ++index) {
            scheduler_->schedule_cdrom_event(events_[index].cycle, events_[index].kind);
        }
    }
}

void Cdrom::attach_cdda_sink(CddaSink* sink) noexcept {
    cdda_sink_ = sink;
}

void Cdrom::attach_xa_sink(XaSectorSink* sink) noexcept {
    xa_sink_ = sink;
}

bool Cdrom::set_disc_layout(const DiscLayout& layout) noexcept {
    if (!layout.valid()) {
        record_error(ErrorCode::InvalidDisc);
        return false;
    }
    if (command_pending_ || command_busy_ || command_start_scheduled_ ||
        phase_action_ != PhaseAction::None || reading_ || playing_ || seeking_ ||
        interrupt_reason_ != Interrupt::None || interrupt_queue_count_ != 0U) {
        record_error(ErrorCode::CommandBusy);
        return false;
    }
    stop_streams();
    remove_events(ScheduledEvent::CommandPhase);
    phase_action_ = PhaseAction::None;
    seeking_ = false;
    layout_ = layout;
    current_lba_ = layout_.tracks[0U].start_lba;
    target_lba_ = current_lba_;
    location_pending_ = false;
    clear_transfer_buffers();
    data_size_ = 0U;
    data_cursor_ = 0U;
    last_position_valid_ = false;
    last_location_valid_ = false;
    return true;
}

void Cdrom::set_shell_open(bool open) noexcept {
    if (shell_open_ == open) {
        return;
    }
    shell_open_ = open;
    if (!open) {
        return;
    }
    stop_streams();
    remove_events(ScheduledEvent::CommandPhase);
    phase_action_ = PhaseAction::None;
    seeking_ = false;
    paused_ = false;
    motor_on_ = false;
    location_pending_ = false;
    clear_transfer_buffers();
    data_storage_ = {};
    data_size_ = 0U;
    data_cursor_ = 0U;
    request_data_ = false;
    last_position_valid_ = false;
    last_location_valid_ = false;
}

void Cdrom::advance_to(Cycle cycle) noexcept {
    if (cycle < cycle_) {
        record_error(ErrorCode::TimeReversal);
        return;
    }
    while (event_count_ != 0U && events_[0U].cycle <= cycle) {
        const Event event = events_[0U];
        for (std::size_t index = 1U; index < event_count_; ++index) {
            events_[index - 1U] = events_[index];
        }
        --event_count_;
        cycle_ = event.cycle;
        process_event(event);
    }
    cycle_ = cycle;
}

Cycle Cdrom::current_cycle() const noexcept {
    return cycle_;
}

std::uint8_t Cdrom::read_register(std::uint8_t offset) noexcept {
    if (offset > 3U) {
        record_error(ErrorCode::InvalidRegister);
        return 0xffU;
    }
    switch (offset) {
    case 0U:
        return host_status_value();
    case 1U:
        return pop_response();
    case 2U:
        return pop_data();
    case 3U:
        if (register_bank_ == 0U || register_bank_ == 2U) {
            return static_cast<std::uint8_t>(HostReservedReadBits | interrupt_enable_);
        }
        return static_cast<std::uint8_t>(HostReservedReadBits | interrupt_flags());
    default:
        break;
    }
    return 0xffU;
}

std::uint16_t Cdrom::read_register16(std::uint8_t offset) noexcept {
    if (offset == 0U) {
        const std::uint16_t value = host_status_value();
        return static_cast<std::uint16_t>(value | (value << 8U));
    }
    if (offset == 2U) {
        const std::uint16_t low = pop_data();
        const std::uint16_t high = pop_data();
        return static_cast<std::uint16_t>(low | (high << 8U));
    }
    record_error(ErrorCode::InvalidRegister);
    return 0xffffU;
}

std::uint32_t Cdrom::read_register32(std::uint8_t offset) noexcept {
    if (offset == 0U) {
        const std::uint32_t value = host_status_value();
        return value | (value << 8U) | (value << 16U) | (value << 24U);
    }
    if (offset == 2U) {
        std::uint32_t value = 0U;
        for (unsigned byte = 0U; byte < 4U; ++byte) {
            value |= static_cast<std::uint32_t>(pop_data()) << (byte * 8U);
        }
        return value;
    }
    record_error(ErrorCode::InvalidRegister);
    return 0xffffffffU;
}

void Cdrom::write_register(std::uint8_t offset, std::uint8_t value) noexcept {
    if (offset > 3U) {
        record_error(ErrorCode::InvalidRegister);
        return;
    }
    if (offset == 0U) {
        register_bank_ = static_cast<std::uint8_t>(value & 0x03U);
        return;
    }
    switch (register_bank_) {
    case 0U:
        if (offset == 1U) {
            (void)enqueue_command(value);
        } else if (offset == 2U) {
            (void)enqueue_parameter(value);
        } else {
            request_data_ = (value & 0x80U) != 0U;
            if (request_data_) {
                latch_transfer();
            } else {
                data_size_ = 0U;
                data_cursor_ = 0U;
            }
        }
        break;
    case 1U:
        if (offset == 1U) {
            (void)value;
        } else if (offset == 2U) {
            interrupt_enable_ = static_cast<std::uint8_t>(value & 0x1fU);
        } else if ((value & 0x80U) != 0U) {
            reset();
        } else {
            acknowledge_interrupts(static_cast<std::uint8_t>(value & 0x07U));
            if ((value & 0x40U) != 0U) {
                parameter_storage_ = {};
                parameter_count_ = 0U;
            }
        }
        break;
    case 2U:
        if (offset == 1U) {
            coding_info_ = value;
        } else if (offset == 2U) {
            pending_audio_matrix_.left_to_left = value;
        } else {
            pending_audio_matrix_.left_to_right = value;
        }
        break;
    case 3U:
        if (offset == 1U) {
            pending_audio_matrix_.right_to_right = value;
        } else if (offset == 2U) {
            pending_audio_matrix_.right_to_left = value;
        } else {
            xa_host_muted_ = (value & 0x01U) != 0U;
            if ((value & 0x20U) != 0U) {
                audio_matrix_ = pending_audio_matrix_;
            }
        }
        break;
    default:
        record_error(ErrorCode::InvalidRegister);
        break;
    }
}

bool Cdrom::enqueue_command(std::uint8_t command) noexcept {
    if (command_pending_ || command_busy_ || command_start_scheduled_ ||
        phase_action_ != PhaseAction::None) {
        record_error(ErrorCode::CommandBusy);
        return false;
    }
    pending_command_ = command;
    pending_parameter_count_ = parameter_count_;
    std::copy_n(parameter_storage_.begin(), parameter_count_, pending_parameters_.begin());
    parameter_storage_ = {};
    parameter_count_ = 0U;
    command_pending_ = true;
    command_busy_ = true;
    pump_command();
    return command_pending_ && (command_start_scheduled_ || interrupt_reason_ != Interrupt::None);
}

bool Cdrom::enqueue_parameter(std::uint8_t value) noexcept {
    if (parameter_count_ == ParameterFifoCapacity) {
        record_error(ErrorCode::ParameterOverflow);
        return false;
    }
    parameter_storage_[parameter_count_] = value;
    ++parameter_count_;
    return true;
}

std::uint8_t Cdrom::pop_response() noexcept {
    if (response_length_ == 0U) {
        return 0U;
    }
    const std::uint8_t value = response_cursor_ < response_length_
                                   ? response_storage_[response_cursor_]
                                   : 0U;
    if (response_cursor_ < response_length_ && response_unread_ != 0U) {
        --response_unread_;
    }
    response_cursor_ = (response_cursor_ + 1U) % ResponseFifoCapacity;
    return value;
}

std::uint8_t Cdrom::pop_data() noexcept {
    if (!request_data_ || data_size_ == 0U) {
        return 0U;
    }
    if (data_cursor_ < data_size_) {
        const std::uint8_t value = data_storage_[data_cursor_];
        ++data_cursor_;
        return value;
    }
    const std::size_t padding_distance = data_size_ == WholeSectorBytes ? 4U : 8U;
    return data_storage_[data_size_ - padding_distance];
}

void Cdrom::acknowledge_interrupts(std::uint8_t mask) noexcept {
    const std::uint8_t old_reason = interrupt_code(interrupt_reason_);
    if (old_reason == 0U) {
        return;
    }
    const std::uint8_t new_reason =
        static_cast<std::uint8_t>(old_reason & ~static_cast<std::uint8_t>(mask & 0x07U));
    if (new_reason != 0U) {
        interrupt_reason_ = static_cast<Interrupt>(new_reason);
        return;
    }
    interrupt_reason_ = Interrupt::None;
    response_storage_ = {};
    response_length_ = 0U;
    response_cursor_ = 0U;
    response_unread_ = 0U;
    activate_next_interrupt();
    if (interrupt_reason_ == Interrupt::None) {
        pump_command();
    }
}

std::uint8_t Cdrom::status() const noexcept {
    return status_value();
}

std::uint8_t Cdrom::interrupt_flags() const noexcept {
    return interrupt_code(interrupt_reason_);
}

std::uint8_t Cdrom::interrupt_enable() const noexcept {
    return interrupt_enable_;
}

bool Cdrom::irq_line() const noexcept {
    return (interrupt_enable_ & interrupt_flags()) != 0U;
}

ErrorCode Cdrom::last_error() const noexcept {
    return last_error_;
}

std::uint8_t Cdrom::selected_register_bank() const noexcept {
    return register_bank_;
}

std::size_t Cdrom::command_count() const noexcept {
    return command_pending_ ? 1U : 0U;
}

std::size_t Cdrom::parameter_count() const noexcept {
    return parameter_count_;
}

std::size_t Cdrom::response_count() const noexcept {
    return response_unread_;
}

std::size_t Cdrom::data_count() const noexcept {
    return data_cursor_ < data_size_ ? data_size_ - data_cursor_ : 0U;
}

std::size_t Cdrom::pending_interrupt_count() const noexcept {
    return interrupt_queue_count_ + (interrupt_reason_ == Interrupt::None ? 0U : 1U);
}

Lba Cdrom::current_lba() const noexcept {
    return current_lba_;
}

Msf Cdrom::current_msf() const noexcept {
    return lba_to_msf(current_lba_);
}

bool Cdrom::reading() const noexcept {
    return reading_;
}

bool Cdrom::playing() const noexcept {
    return playing_;
}

bool Cdrom::muted() const noexcept {
    return muted_;
}

bool Cdrom::shell_open() const noexcept {
    return shell_open_;
}

std::uint8_t Cdrom::mode() const noexcept {
    return mode_;
}

AudioMatrix Cdrom::audio_matrix() const noexcept {
    return audio_matrix_;
}

void Cdrom::set_audio_matrix(AudioMatrix matrix) noexcept {
    pending_audio_matrix_ = matrix;
    audio_matrix_ = matrix;
}

void Cdrom::set_muted(bool muted) noexcept {
    muted_ = muted;
}

void Cdrom::set_dma_enabled(bool enabled) noexcept {
    dma_enabled_ = enabled;
    if (dma_enabled_ && request_data_) {
        latch_transfer();
    }
}

std::uint8_t Cdrom::to_bcd(std::uint8_t value) noexcept {
    if (value > 99U) {
        return 0xffU;
    }
    return static_cast<std::uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

bool Cdrom::from_bcd(std::uint8_t value, std::uint8_t& decoded) noexcept {
    const std::uint8_t high = static_cast<std::uint8_t>(value >> 4U);
    const std::uint8_t low = static_cast<std::uint8_t>(value & 0x0fU);
    if (high > 9U || low > 9U) {
        return false;
    }
    decoded = static_cast<std::uint8_t>(high * 10U + low);
    return true;
}

bool Cdrom::decode_msf_bcd(std::span<const std::uint8_t, 3U> bytes,
                           Msf& result) noexcept {
    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;
    if (!from_bcd(bytes[0U], minute) || !from_bcd(bytes[1U], second) ||
        !from_bcd(bytes[2U], frame)) {
        return false;
    }
    if (second >= 60U || frame >= 75U) {
        return false;
    }
    result = Msf{minute, second, frame};
    return true;
}

Msf Cdrom::lba_to_msf(Lba lba) noexcept {
    if (lba > MaximumAddressableLba) {
        return Msf{0xffU, 0xffU, 0xffU};
    }
    return frames_to_msf(static_cast<Lba>(lba + 150U));
}

Msf Cdrom::frames_to_msf(Lba frames) noexcept {
    if (frames > MaximumAbsoluteFrame) {
        return Msf{0xffU, 0xffU, 0xffU};
    }
    const Lba minute = frames / FramesPerMinute;
    const Lba second = (frames / SectorsPerSecond) % 60U;
    const Lba frame = frames % SectorsPerSecond;
    return Msf{static_cast<std::uint8_t>(minute),
               static_cast<std::uint8_t>(second),
               static_cast<std::uint8_t>(frame)};
}

bool Cdrom::msf_to_lba(const Msf& msf, Lba& lba) noexcept {
    if (msf.minute > MaximumBcdMinute || msf.second >= 60U || msf.frame >= 75U) {
        return false;
    }
    const Lba absolute_frame = static_cast<Lba>(msf.minute) * FramesPerMinute +
                               static_cast<Lba>(msf.second) * SectorsPerSecond + msf.frame;
    if (absolute_frame < 150U) {
        return false;
    }
    lba = static_cast<Lba>(absolute_frame - 150U);
    return true;
}

std::uint32_t Cdrom::edc(const std::uint8_t* bytes, std::size_t length) noexcept {
    if (bytes == nullptr) {
        return 0U;
    }
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < length; ++index) {
        value ^= bytes[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const bool carry = (value & 1U) != 0U;
            value >>= 1U;
            if (carry) {
                value ^= 0xd8018001U;
            }
        }
    }
    return value;
}

bool Cdrom::decode_data_sector(Lba lba,
                               const RawSector& raw,
                               SectorView& result) noexcept {
    result = SectorView{};
    result.lba = lba;
    if (lba > MaximumAddressableLba || !raw_sync_is_valid(raw)) {
        result.error_code = static_cast<std::uint32_t>(ErrorCode::MalformedSector);
        return false;
    }
    const Msf expected = lba_to_msf(lba);
    const std::array<std::uint8_t, 3U> header_bcd{
        raw.bytes[12U], raw.bytes[13U], raw.bytes[14U]};
    Msf address{};
    if (!decode_msf_bcd(std::span<const std::uint8_t, 3U>(header_bcd), address) ||
        address != expected) {
        result.error_code = static_cast<std::uint32_t>(ErrorCode::InvalidMsf);
        return false;
    }
    result.address = address;
    const std::uint8_t mode = raw.bytes[HeaderModeOffset];
    if (mode == ModeOneValue) {
        const std::uint32_t expected_edc = stored_little_endian(raw, ModeOneEdcOffset);
        if (expected_edc != edc(raw.bytes.data(), ModeOneEdcOffset)) {
            result.error_code = static_cast<std::uint32_t>(ErrorCode::MalformedSector);
            return false;
        }
        result.kind = SectorKind::ModeOne;
        result.payload_size = static_cast<std::uint16_t>(ModeOnePayloadBytes);
        std::copy_n(raw.bytes.begin() + static_cast<std::ptrdiff_t>(ModeOnePayloadOffset),
                    ModeOnePayloadBytes,
                    result.payload.begin());
        result.valid = true;
        return true;
    }
    if (mode != ModeTwoValue) {
        result.error_code = static_cast<std::uint32_t>(ErrorCode::MalformedSector);
        return false;
    }
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (raw.bytes[SubheaderOffset + index] != raw.bytes[SubheaderOffset + 4U + index]) {
            result.error_code = static_cast<std::uint32_t>(ErrorCode::MalformedSector);
            return false;
        }
    }
    result.xa.file = raw.bytes[SubheaderOffset];
    result.xa.channel = raw.bytes[SubheaderOffset + 1U];
    result.xa.submode = raw.bytes[SubheaderOffset + 2U];
    result.xa.coding = raw.bytes[SubheaderOffset + 3U];
    result.xa.form_two = (result.xa.submode & XaFormTwo) != 0U;
    result.xa.audio = (result.xa.submode & XaAudio) != 0U;
    result.xa.video = (result.xa.submode & XaVideo) != 0U;
    result.xa.data = (result.xa.submode & XaData) != 0U;
    result.xa.real_time = (result.xa.submode & XaRealTime) != 0U;
    result.xa.end_of_record = (result.xa.submode & XaEndOfRecord) != 0U;
    result.xa.trigger = (result.xa.submode & XaTrigger) != 0U;
    result.xa.end_of_file = (result.xa.submode & XaEndOfFile) != 0U;
    if (result.xa.form_two) {
        const std::uint32_t expected_edc = stored_little_endian(raw, FormTwoEdcOffset);
        const std::uint32_t calculated =
            edc(&raw.bytes[SubheaderOffset], FormTwoEdcOffset - SubheaderOffset);
        if (expected_edc != 0U && expected_edc != calculated) {
            result.error_code = static_cast<std::uint32_t>(ErrorCode::MalformedSector);
            return false;
        }
        result.kind = SectorKind::ModeTwoFormTwo;
        result.payload_size = static_cast<std::uint16_t>(FormTwoPayloadBytes);
    } else {
        const std::uint32_t expected_edc = stored_little_endian(raw, FormOneEdcOffset);
        if (expected_edc != edc(&raw.bytes[SubheaderOffset],
                                FormOneEdcOffset - SubheaderOffset)) {
            result.error_code = static_cast<std::uint32_t>(ErrorCode::MalformedSector);
            return false;
        }
        result.kind = SectorKind::ModeTwoFormOne;
        result.payload_size = static_cast<std::uint16_t>(FormOnePayloadBytes);
    }
    std::copy_n(raw.bytes.begin() + static_cast<std::ptrdiff_t>(PayloadOffset),
                static_cast<std::size_t>(result.payload_size),
                result.payload.begin());
    result.valid = true;
    return true;
}

void Cdrom::record_error(ErrorCode error) noexcept {
    if (error != ErrorCode::None) {
        last_error_ = error;
    }
}

bool Cdrom::schedule_event(Cycle cycle, ScheduledEvent event) noexcept {
    if (event_count_ == EventCapacity) {
        record_error(ErrorCode::SchedulerOverflow);
        return false;
    }
    const Event inserted{cycle, event, event_sequence_++};
    std::size_t position = event_count_;
    while (position != 0U) {
        const Event& previous = events_[position - 1U];
        if (previous.cycle < inserted.cycle ||
            (previous.cycle == inserted.cycle && previous.sequence < inserted.sequence)) {
            break;
        }
        events_[position] = previous;
        --position;
    }
    events_[position] = inserted;
    ++event_count_;
    if (scheduler_ != nullptr) {
        scheduler_->schedule_cdrom_event(cycle, event);
    }
    return true;
}

void Cdrom::remove_events(ScheduledEvent event) noexcept {
    std::size_t write = 0U;
    for (std::size_t read = 0U; read < event_count_; ++read) {
        if (events_[read].kind != event) {
            events_[write] = events_[read];
            ++write;
        }
    }
    if (write != event_count_ && scheduler_ != nullptr) {
        scheduler_->cancel_cdrom_event(event);
    }
    event_count_ = write;
}

void Cdrom::pump_command() noexcept {
    if (!command_pending_ || command_start_scheduled_ ||
        interrupt_reason_ != Interrupt::None) {
        return;
    }
    Cycle event_cycle = 0U;
    if (!add_without_overflow(cycle_, CommandLatencyCycles, event_cycle) ||
        !schedule_event(event_cycle, ScheduledEvent::CommandStart)) {
        record_error(ErrorCode::SchedulerOverflow);
        command_pending_ = false;
        command_busy_ = false;
        pending_parameter_count_ = 0U;
        return;
    }
    command_start_scheduled_ = true;
}

void Cdrom::process_event(const Event& event) noexcept {
    switch (event.kind) {
    case ScheduledEvent::CommandStart: {
        command_start_scheduled_ = false;
        if (!command_pending_) {
            command_busy_ = false;
            return;
        }
        const std::uint8_t command = pending_command_;
        const std::size_t parameter_count = pending_parameter_count_;
        command_pending_ = false;
        command_busy_ = false;
        begin_command(command,
                      std::span<const std::uint8_t>(pending_parameters_.data(),
                                                    parameter_count));
        pending_parameters_ = {};
        pending_parameter_count_ = 0U;
        break;
    }
    case ScheduledEvent::CommandPhase:
        complete_phase();
        break;
    case ScheduledEvent::DataSector:
        if (reading_) {
            process_data_sector();
        }
        break;
    case ScheduledEvent::CddaSector:
        if (playing_) {
            process_cdda_sector();
        }
        break;
    }
}

void Cdrom::begin_command(std::uint8_t command,
                          std::span<const std::uint8_t> parameters) noexcept {
    const auto wrong_count = [this]() {
        queue_command_error(ErrorCode::WrongParameterCount,
                            CommandError::WrongParameterCount);
    };
    switch (static_cast<Command>(command)) {
    case Command::Nop:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Setloc: {
        if (!exact_parameter_count(parameters, 3U)) {
            wrong_count();
            break;
        }
        Lba lba = 0U;
        if (!decode_parameter_msf(parameters, lba)) {
            queue_command_error(ErrorCode::InvalidMsf, CommandError::InvalidParameter);
            break;
        }
        if (shell_open_ || !layout_.valid() || lba >= layout_.lead_out_lba) {
            queue_command_error(ErrorCode::InvalidMsf, CommandError::InvalidParameter);
            break;
        }
        target_lba_ = lba;
        location_pending_ = true;
        (void)queue_status(Interrupt::Acknowledge);
        break;
    }
    case Command::ReadN:
    case Command::ReadS: {
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid() || source_ == nullptr) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        if (location_pending_) {
            current_lba_ = target_lba_;
            location_pending_ = false;
        }
        if (current_lba_ >= layout_.lead_out_lba) {
            queue_command_error(ErrorCode::InvalidMsf, CommandError::InvalidParameter);
            break;
        }
        stop_streams();
        clear_transfer_buffers();
        data_size_ = 0U;
        data_cursor_ = 0U;
        motor_on_ = true;
        paused_ = false;
        reading_ = true;
        Cycle next = 0U;
        const Cycle period = (mode_ & ModeDoubleSpeed) != 0U
                                 ? DoubleSpeedSectorCycles
                                 : SingleSpeedSectorCycles;
        if (!add_without_overflow(cycle_, period, next) ||
            !schedule_event(next, ScheduledEvent::DataSector)) {
            reading_ = false;
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    }
    case Command::Play: {
        if (parameters.size() > 1U) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid() || source_ == nullptr) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        if (parameters.size() == 1U) {
            std::uint8_t track = 0U;
            if (!from_bcd(parameters[0U], track)) {
                queue_command_error(ErrorCode::InvalidBcd,
                                    CommandError::InvalidParameter);
                break;
            }
            if (track != 0U) {
                const Track* selected = layout_.find_track(track);
                if (selected == nullptr) {
                    queue_command_error(ErrorCode::InvalidTrack,
                                        CommandError::InvalidParameter);
                    break;
                }
                current_lba_ = selected->start_lba;
                location_pending_ = false;
            }
        } else if (location_pending_) {
            current_lba_ = target_lba_;
            location_pending_ = false;
        }
        const Track* selected = layout_.track_for_lba(current_lba_);
        if (selected == nullptr) {
            queue_command_error(ErrorCode::InvalidTrack,
                                CommandError::InvalidParameter);
            break;
        }
        stop_streams();
        motor_on_ = true;
        paused_ = false;
        playing_ = true;
        play_origin_track_ = selected->number;
        Cycle next = 0U;
        // Setmode bit 7 changes data-sector cadence only.  Red Book audio
        // remains 75 sectors/second; applying the data multiplier here would
        // double CDDA pitch and run report/autopause/lead-out early.
        if (!add_without_overflow(cycle_, SingleSpeedSectorCycles, next) ||
            !schedule_event(next, ScheduledEvent::CddaSector)) {
            playing_ = false;
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    }
    case Command::Forward:
    case Command::Backward: {
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (!playing_) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        if (static_cast<Command>(command) == Command::Forward) {
            current_lba_ = std::min<Lba>(layout_.lead_out_lba, current_lba_ + 75U);
        } else {
            current_lba_ = current_lba_ > 75U ? current_lba_ - 75U : 0U;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    }
    case Command::Standby:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (motor_on_) {
            queue_command_error(ErrorCode::CannotRespond,
                                CommandError::WrongParameterCount);
            break;
        }
        if (!start_phase(PhaseAction::MotorOn, CommandLatencyCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Stop:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        stop_streams();
        seeking_ = false;
        if (!start_phase(PhaseAction::Stop, SeekLatencyCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Pause: {
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        const Cycle period = (mode_ & ModeDoubleSpeed) != 0U
                                 ? DoubleSpeedSectorCycles
                                 : SingleSpeedSectorCycles;
        const Cycle delay = (reading_ || playing_) ? period * 5U : CommandLatencyCycles;
        if (!start_phase(PhaseAction::Pause, delay)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    }
    case Command::Init:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        stop_streams();
        seeking_ = false;
        mode_ = ModeWholeSector;
        whole_sector_ = true;
        filter_file_ = 0U;
        filter_channel_ = 0U;
        muted_ = false;
        motor_on_ = true;
        paused_ = true;
        if (!start_phase(
                PhaseAction::InitAcknowledge,
                InitFirstResponseCycles - CommandLatencyCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        break;
    case Command::Mute:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        muted_ = true;
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Demute:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        muted_ = false;
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Setfilter:
        if (!exact_parameter_count(parameters, 2U)) {
            wrong_count();
            break;
        }
        filter_file_ = parameters[0U];
        filter_channel_ = parameters[1U];
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Setmode:
        if (!exact_parameter_count(parameters, 1U)) {
            wrong_count();
            break;
        }
        mode_ = parameters[0U];
        if ((mode_ & ModeIgnoreSectorSize) == 0U) {
            whole_sector_ = (mode_ & ModeWholeSector) != 0U;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Getparam: {
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        const std::array<std::uint8_t, 5U> response{
            status_value(), mode_, 0U, filter_file_, filter_channel_};
        (void)queue_interrupt(Interrupt::Acknowledge,
                              std::span<const std::uint8_t>(response));
        break;
    }
    case Command::GetlocL:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (!last_location_valid_) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        (void)queue_interrupt(Interrupt::Acknowledge,
                              std::span<const std::uint8_t>(last_location_));
        break;
    case Command::GetlocP: {
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid()) {
            queue_command_error(ErrorCode::InvalidDisc, CommandError::CannotRespond);
            break;
        }
        const Lba position = last_position_valid_ ? last_position_lba_ : current_lba_;
        const Track* track = layout_.track_for_lba(position);
        const std::uint8_t track_number =
            position >= layout_.lead_out_lba ? LeadOutTrack
                                             : (track == nullptr ? UnknownTrack : track->number);
        if (track_number == UnknownTrack) {
            queue_command_error(ErrorCode::InvalidTrack, CommandError::CannotRespond);
            break;
        }
        const Lba relative_frames = track == nullptr || position < track->start_lba
                                        ? 0U
                                        : position - track->start_lba;
        const std::uint8_t index_number =
            track != nullptr && position < track->start_lba ? 0U : 1U;
        const Msf relative = frames_to_msf(relative_frames);
        const Msf absolute = lba_to_msf(std::min(position, MaximumAddressableLba));
        const std::array<std::uint8_t, 8U> response{
            track_number == LeadOutTrack ? LeadOutTrack : to_bcd(track_number),
            to_bcd(index_number),
            to_bcd(relative.minute),
            to_bcd(relative.second),
            to_bcd(relative.frame),
            to_bcd(absolute.minute),
            to_bcd(absolute.second),
            to_bcd(absolute.frame)};
        (void)queue_interrupt(Interrupt::Acknowledge,
                              std::span<const std::uint8_t>(response));
        break;
    }
    case Command::SetSession:
        if (!exact_parameter_count(parameters, 1U)) {
            wrong_count();
            break;
        }
        if (parameters[0U] != 1U || shell_open_ || !layout_.valid()) {
            queue_command_error(ErrorCode::InvalidTrack, CommandError::InvalidParameter);
            break;
        }
        stop_streams();
        seeking_ = true;
        motor_on_ = true;
        if (!start_phase(PhaseAction::SetSession, SeekLatencyCycles)) {
            seeking_ = false;
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::GetTN: {
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid()) {
            queue_command_error(ErrorCode::InvalidDisc, CommandError::CannotRespond);
            break;
        }
        const std::array<std::uint8_t, 3U> response{
            status_value(),
            to_bcd(layout_.tracks[0U].number),
            to_bcd(layout_.tracks[static_cast<std::size_t>(layout_.track_count) - 1U]
                       .number)};
        (void)queue_interrupt(Interrupt::Acknowledge,
                              std::span<const std::uint8_t>(response));
        break;
    }
    case Command::GetTD: {
        if (!exact_parameter_count(parameters, 1U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid()) {
            queue_command_error(ErrorCode::InvalidDisc, CommandError::CannotRespond);
            break;
        }
        std::uint8_t track = 0U;
        if (!from_bcd(parameters[0U], track)) {
            queue_command_error(ErrorCode::InvalidBcd, CommandError::InvalidParameter);
            break;
        }
        Lba start = layout_.lead_out_lba;
        if (track != 0U) {
            const Track* selected = layout_.find_track(track);
            if (selected == nullptr) {
                queue_command_error(ErrorCode::InvalidTrack,
                                    CommandError::InvalidParameter);
                break;
            }
            start = selected->start_lba;
        }
        const Msf location = lba_to_msf(start);
        const std::array<std::uint8_t, 3U> response{
            status_value(), to_bcd(location.minute), to_bcd(location.second)};
        (void)queue_interrupt(Interrupt::Acknowledge,
                              std::span<const std::uint8_t>(response));
        break;
    }
    case Command::SeekL:
    case Command::SeekP:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid() || source_ == nullptr ||
            target_lba_ >= layout_.lead_out_lba) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        if (static_cast<Command>(command) == Command::SeekL) {
            const Track* target_track = layout_.track_for_lba(target_lba_);
            if (target_track == nullptr || target_track->kind != TrackKind::Data) {
                queue_command_error(ErrorCode::InvalidTrack, CommandError::InvalidParameter);
                break;
            }
        }
        stop_streams();
        seeking_ = true;
        motor_on_ = true;
        paused_ = false;
        location_pending_ = false;
        if (!start_phase(PhaseAction::Seek, SeekLatencyCycles)) {
            seeking_ = false;
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Test: {
        if (!exact_parameter_count(parameters, 1U)) {
            wrong_count();
            break;
        }
        if (parameters[0U] != 0x20U) {
            queue_command_error(ErrorCode::UnsupportedCommand,
                                CommandError::InvalidParameter);
            break;
        }
        constexpr std::array<std::uint8_t, 4U> version{0x95U, 0x05U, 0x16U, 0xc1U};
        (void)queue_interrupt(Interrupt::Acknowledge,
                              std::span<const std::uint8_t>(version));
        break;
    }
    case Command::ID:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid() || source_ == nullptr) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        if (!start_phase(PhaseAction::Id, CommandLatencyCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::Reset:
        stop_streams();
        seeking_ = false;
        mode_ = 0U;
        whole_sector_ = false;
        filter_file_ = 0U;
        filter_channel_ = 0U;
        muted_ = false;
        xa_host_muted_ = false;
        motor_on_ = false;
        paused_ = false;
        clear_transfer_buffers();
        data_size_ = 0U;
        data_cursor_ = 0U;
        if (!start_phase(PhaseAction::ResetReboot, ResetRebootCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::ReadToc:
        if (!exact_parameter_count(parameters, 0U)) {
            wrong_count();
            break;
        }
        if (shell_open_ || !layout_.valid() || source_ == nullptr) {
            queue_command_error(ErrorCode::CannotRespond, CommandError::CannotRespond);
            break;
        }
        if (!start_phase(PhaseAction::ReadToc, SeekLatencyCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
            break;
        }
        (void)queue_status(Interrupt::Acknowledge);
        break;
    case Command::GetQ:
    default:
        queue_command_error(ErrorCode::UnsupportedCommand, CommandError::InvalidCommand);
        break;
    }
}

bool Cdrom::start_phase(PhaseAction action, Cycle delay) noexcept {
    if (phase_action_ != PhaseAction::None) {
        record_error(ErrorCode::CommandBusy);
        return false;
    }
    Cycle completion = 0U;
    if (!add_without_overflow(cycle_, delay, completion) ||
        !schedule_event(completion, ScheduledEvent::CommandPhase)) {
        record_error(ErrorCode::SchedulerOverflow);
        return false;
    }
    phase_action_ = action;
    return true;
}

void Cdrom::complete_phase() noexcept {
    const PhaseAction action = phase_action_;
    phase_action_ = PhaseAction::None;
    switch (action) {
    case PhaseAction::None:
        return;
    case PhaseAction::MotorOn:
        motor_on_ = true;
        paused_ = true;
        (void)queue_status(Interrupt::Complete);
        break;
    case PhaseAction::Stop:
        motor_on_ = false;
        paused_ = false;
        if (layout_.valid()) {
            current_lba_ = layout_.tracks[0U].start_lba;
        }
        last_position_valid_ = false;
        last_location_valid_ = false;
        (void)queue_status(Interrupt::Complete);
        break;
    case PhaseAction::Pause:
        stop_streams();
        seeking_ = false;
        paused_ = true;
        (void)queue_status(Interrupt::Complete);
        break;
    case PhaseAction::InitAcknowledge:
        (void)queue_status(Interrupt::Acknowledge);
        if (!start_phase(PhaseAction::InitComplete, SeekLatencyCycles)) {
            queue_command_error(ErrorCode::SchedulerOverflow,
                                CommandError::CannotRespond);
        }
        break;
    case PhaseAction::InitComplete:
        paused_ = true;
        (void)queue_status(Interrupt::Complete);
        break;
    case PhaseAction::ResetReboot:
        /* Command 1Ch has no completion packet.  Reaching this native-time
         * boundary only makes the controller command interface responsive. */
        break;
    case PhaseAction::SetSession:
        seeking_ = false;
        paused_ = true;
        (void)queue_status(Interrupt::Complete);
        break;
    case PhaseAction::Seek:
        seeking_ = false;
        paused_ = true;
        current_lba_ = target_lba_;
        last_position_lba_ = current_lba_;
        last_position_valid_ = true;
        last_location_valid_ = false;
        (void)queue_status(Interrupt::Complete);
        break;
    case PhaseAction::Id: {
        std::array<std::uint8_t, 8U> response{
            status_value(), 0U, layout_.disc_type, 0U, 0U, 0U, 0U, 0U};
        std::copy(layout_.license_region.begin(),
                  layout_.license_region.end(),
                  response.begin() + 4);
        if (region_is_present(layout_)) {
            (void)queue_interrupt(Interrupt::Complete,
                                  std::span<const std::uint8_t>(response));
        } else {
            response[0U] = static_cast<std::uint8_t>(
                response[0U] | status_bit(Status::IdError));
            response[1U] = 0x80U;
            record_error(ErrorCode::CannotRespond);
            (void)queue_interrupt(Interrupt::Error,
                                  std::span<const std::uint8_t>(response));
        }
        break;
    }
    case PhaseAction::ReadToc:
        (void)queue_status(Interrupt::Complete);
        break;
    }
}

bool Cdrom::queue_interrupt(Interrupt reason,
                            std::span<const std::uint8_t> bytes) noexcept {
    if (reason == Interrupt::None || bytes.size() > ResponseFifoCapacity) {
        record_error(ErrorCode::ResponseOverflow);
        stop_streams();
        return false;
    }
    InterruptPacket packet{};
    packet.reason = reason;
    packet.size = static_cast<std::uint8_t>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), packet.bytes.begin());
    if (interrupt_reason_ == Interrupt::None) {
        activate_packet(packet);
        return true;
    }
    if (interrupt_queue_count_ == InterruptQueueCapacity) {
        record_error(ErrorCode::ResponseOverflow);
        stop_streams();
        return false;
    }
    interrupt_queue_[interrupt_queue_count_] = packet;
    ++interrupt_queue_count_;
    return true;
}

bool Cdrom::queue_status(Interrupt reason) noexcept {
    const std::array<std::uint8_t, 1U> response{status_value()};
    return queue_interrupt(reason, std::span<const std::uint8_t>(response));
}

void Cdrom::queue_command_error(ErrorCode diagnostic, CommandError error) noexcept {
    record_error(diagnostic);
    const std::array<std::uint8_t, 2U> response{
        static_cast<std::uint8_t>(status_value() | status_bit(Status::Error)),
        static_cast<std::uint8_t>(error)};
    (void)queue_interrupt(Interrupt::Error,
                          std::span<const std::uint8_t>(response));
}

void Cdrom::activate_packet(const InterruptPacket& packet) noexcept {
    response_storage_ = packet.bytes;
    response_length_ = packet.size;
    response_cursor_ = 0U;
    response_unread_ = packet.size;
    interrupt_reason_ = packet.reason;
}

void Cdrom::activate_next_interrupt() noexcept {
    if (interrupt_queue_count_ == 0U) {
        return;
    }
    const InterruptPacket packet = interrupt_queue_[0U];
    for (std::size_t index = 1U; index < interrupt_queue_count_; ++index) {
        interrupt_queue_[index - 1U] = interrupt_queue_[index];
    }
    --interrupt_queue_count_;
    activate_packet(packet);
}

void Cdrom::schedule_next_data_sector(Cycle from) noexcept {
    Cycle next = 0U;
    const Cycle period = (mode_ & ModeDoubleSpeed) != 0U
                             ? DoubleSpeedSectorCycles
                             : SingleSpeedSectorCycles;
    if (!add_without_overflow(from, period, next) ||
        !schedule_event(next, ScheduledEvent::DataSector)) {
        stop_streams();
        queue_command_error(ErrorCode::SchedulerOverflow,
                            CommandError::CannotRespond);
    }
}

void Cdrom::schedule_next_cdda_sector(Cycle from) noexcept {
    Cycle next = 0U;
    if (!add_without_overflow(from, SingleSpeedSectorCycles, next) ||
        !schedule_event(next, ScheduledEvent::CddaSector)) {
        stop_streams();
        queue_command_error(ErrorCode::SchedulerOverflow,
                            CommandError::CannotRespond);
    }
}

void Cdrom::process_data_sector() noexcept {
    if (shell_open_ || !layout_.valid() || source_ == nullptr) {
        stop_streams();
        queue_command_error(ErrorCode::SourceReadFailure,
                            CommandError::CannotRespond);
        return;
    }
    if (current_lba_ >= layout_.lead_out_lba) {
        end_data_stream();
        return;
    }
    const Track* track = layout_.track_for_lba(current_lba_);
    if (track == nullptr) {
        stop_streams();
        queue_command_error(ErrorCode::InvalidTrack, CommandError::CannotRespond);
        return;
    }
    RawSector raw{};
    const Lba delivered_lba = current_lba_;
    if (!source_->read_raw_sector(delivered_lba, raw)) {
        stop_streams();
        queue_command_error(ErrorCode::SourceReadFailure,
                            CommandError::CannotRespond);
        return;
    }

    bool deliver_to_cpu = true;
    SectorView sector{};
    if (track->kind == TrackKind::Audio) {
        last_location_valid_ = false;
        if ((mode_ & ModeReadCdda) == 0U) {
            stop_streams();
            queue_command_error(ErrorCode::CannotRespond,
                                CommandError::InvalidCommand);
            return;
        }
        const std::size_t size = whole_sector_ ? WholeSectorBytes : DataOnlyBytes;
        if (!prepare_transfer(
                std::span<const std::uint8_t>(raw.bytes.data(), size))) {
            stop_streams();
            return;
        }
    } else {
        if (!decode_data_sector(delivered_lba, raw, sector)) {
            stop_streams();
            const ErrorCode diagnostic =
                sector.error_code == static_cast<std::uint32_t>(ErrorCode::InvalidMsf)
                    ? ErrorCode::InvalidMsf
                    : ErrorCode::MalformedSector;
            queue_command_error(diagnostic, CommandError::CannotRespond);
            return;
        }
        std::copy_n(raw.bytes.begin() + 12, last_location_.size(), last_location_.begin());
        last_location_valid_ = true;
        const bool xa_audio = sector.kind == SectorKind::ModeTwoFormTwo &&
                              sector.xa.audio && sector.xa.real_time;
        const bool filter_matches = sector.xa.file == filter_file_ &&
                                    sector.xa.channel == filter_channel_;
        if (xa_audio && (mode_ & ModeXaAdpcm) != 0U) {
            deliver_to_cpu = false;
            if ((mode_ & ModeXaFilter) == 0U || filter_matches) {
                if (xa_sink_ != nullptr) {
                    xa_sink_->receive_xa_sector(
                        delivered_lba,
                        sector.xa,
                        std::span<const std::uint8_t>(sector.payload.data(), XaAdpcmBytes),
                        audio_matrix_,
                        muted_ || xa_host_muted_);
                }
            }
        } else if (xa_audio && (mode_ & ModeXaFilter) != 0U && !filter_matches) {
            deliver_to_cpu = false;
        }
        if (deliver_to_cpu) {
            if (whole_sector_) {
                if (!prepare_transfer(std::span<const std::uint8_t>(
                        raw.bytes.data() + 12U, WholeSectorBytes))) {
                    stop_streams();
                    return;
                }
            } else if (!prepare_transfer(std::span<const std::uint8_t>(
                           sector.payload.data(), DataOnlyBytes))) {
                stop_streams();
                return;
            }
        }
    }

    last_position_lba_ = delivered_lba;
    last_position_valid_ = true;
    if (deliver_to_cpu) {
        if (!queue_status(Interrupt::DataReady)) {
            stop_streams();
            return;
        }
        if (request_data_) {
            latch_transfer();
        }
    }
    ++current_lba_;
    if (reading_) {
        schedule_next_data_sector(cycle_);
    }
}

void Cdrom::process_cdda_sector() noexcept {
    if (shell_open_ || !layout_.valid() || source_ == nullptr) {
        stop_streams();
        queue_command_error(ErrorCode::SourceReadFailure,
                            CommandError::CannotRespond);
        return;
    }
    if (current_lba_ >= layout_.lead_out_lba) {
        end_cdda_stream();
        return;
    }
    const Track* track = layout_.track_for_lba(current_lba_);
    if (track == nullptr) {
        stop_streams();
        queue_command_error(ErrorCode::InvalidTrack, CommandError::CannotRespond);
        return;
    }
    if ((mode_ & ModeAutoPause) != 0U && track->number != play_origin_track_) {
        if (current_lba_ != 0U) {
            --current_lba_;
        }
        playing_ = false;
        paused_ = true;
        remove_events(ScheduledEvent::CddaSector);
        (void)queue_status(Interrupt::DataEnd);
        return;
    }
    RawSector raw{};
    const Lba delivered_lba = current_lba_;
    if (!source_->read_raw_sector(delivered_lba, raw)) {
        stop_streams();
        queue_command_error(ErrorCode::SourceReadFailure,
                            CommandError::CannotRespond);
        return;
    }
    if ((mode_ & ModeReport) != 0U && interrupt_reason_ == Interrupt::None) {
        const Msf absolute = lba_to_msf(delivered_lba);
        if (absolute.frame % 10U == 0U) {
            const bool in_pregap = delivered_lba < track->start_lba;
            const Lba relative_frames = in_pregap
                                            ? track->start_lba - delivered_lba
                                            : delivered_lba - track->start_lba;
            const Msf relative = frames_to_msf(relative_frames);
            std::int32_t peak = 0;
            for (std::size_t index = 0U; index < CddaSamplesPerSector; ++index) {
                const std::size_t byte_offset = index * 4U;
                const std::uint16_t word =
                    static_cast<std::uint16_t>(raw.bytes[byte_offset]) |
                    static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(raw.bytes[byte_offset + 1U])
                        << 8U);
                std::int32_t sample = signed_sample(word);
                if (sample < 0) {
                    sample = -sample;
                }
                peak = std::max(peak, sample);
            }
            peak = std::min<std::int32_t>(peak, 0x7fff);
            std::array<std::uint8_t, 8U> report{
                status_value(), to_bcd(track->number),
                to_bcd(in_pregap ? 0U : 1U), 0U, 0U, 0U,
                static_cast<std::uint8_t>(peak),
                static_cast<std::uint8_t>(static_cast<std::uint32_t>(peak) >> 8U)};
            if ((absolute.frame / 10U) % 2U == 0U) {
                report[3U] = to_bcd(absolute.minute);
                report[4U] = to_bcd(absolute.second);
                report[5U] = to_bcd(absolute.frame);
            } else {
                report[3U] = to_bcd(relative.minute);
                report[4U] = static_cast<std::uint8_t>(
                    to_bcd(relative.second) | 0x80U);
                report[5U] = to_bcd(relative.frame);
            }
            (void)queue_interrupt(Interrupt::DataReady,
                                  std::span<const std::uint8_t>(report));
        }
    }
    std::array<StereoSample, CddaSamplesPerSector> samples{};
    if (track->kind == TrackKind::Audio && !muted_) {
        for (std::size_t index = 0U; index < CddaSamplesPerSector; ++index) {
            const std::size_t byte_offset = index * 4U;
            const std::uint16_t left_word =
                static_cast<std::uint16_t>(raw.bytes[byte_offset]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(raw.bytes[byte_offset + 1U]) << 8U);
            const std::uint16_t right_word =
                static_cast<std::uint16_t>(raw.bytes[byte_offset + 2U]) |
                static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(raw.bytes[byte_offset + 3U]) << 8U);
            const std::int16_t input_left = signed_sample(left_word);
            const std::int16_t input_right = signed_sample(right_word);
            samples[index] = StereoSample{
                mix_audio(input_left,
                          input_right,
                          audio_matrix_.left_to_left,
                          audio_matrix_.right_to_left),
                mix_audio(input_left,
                          input_right,
                          audio_matrix_.left_to_right,
                          audio_matrix_.right_to_right)};
        }
    }
    if (cdda_sink_ != nullptr) {
        cdda_sink_->receive_cdda_samples(
            delivered_lba,
            std::span<const StereoSample>(samples.data(), samples.size()));
    }
    last_position_lba_ = delivered_lba;
    last_position_valid_ = true;
    last_location_valid_ = false;
    ++current_lba_;
    if (playing_) {
        schedule_next_cdda_sector(cycle_);
    }
}

void Cdrom::stop_streams() noexcept {
    remove_events(ScheduledEvent::DataSector);
    remove_events(ScheduledEvent::CddaSector);
    reading_ = false;
    playing_ = false;
}

bool Cdrom::prepare_transfer(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() > DataFifoCapacity) {
        record_error(ErrorCode::DataOverflow);
        return false;
    }
    if (!transfer_valid_) {
        transfer_storage_ = {};
        std::copy(bytes.begin(), bytes.end(), transfer_storage_.begin());
        transfer_size_ = bytes.size();
        transfer_valid_ = true;
        return true;
    }
    if (pending_transfer_count_ == pending_transfer_storage_.size()) {
        record_error(ErrorCode::DataOverflow);
        return false;
    }
    auto& destination = pending_transfer_storage_[pending_transfer_count_];
    destination = {};
    std::copy(bytes.begin(), bytes.end(), destination.begin());
    pending_transfer_sizes_[pending_transfer_count_] = bytes.size();
    ++pending_transfer_count_;
    return true;
}

void Cdrom::latch_transfer() noexcept {
    if (!request_data_ || !transfer_valid_) {
        return;
    }
    data_storage_ = transfer_storage_;
    data_size_ = transfer_size_;
    data_cursor_ = 0U;
    if (dma_enabled_ && dma_ != nullptr) {
        dma_->receive_cdrom_data(
            std::span<const std::uint8_t>(data_storage_.data(), data_size_));
        data_cursor_ = data_size_;
    }
    if (pending_transfer_count_ == 0U) {
        transfer_storage_ = {};
        transfer_size_ = 0U;
        transfer_valid_ = false;
        return;
    }
    transfer_storage_ = pending_transfer_storage_[0U];
    transfer_size_ = pending_transfer_sizes_[0U];
    for (std::size_t index = 1U; index < pending_transfer_count_; ++index) {
        pending_transfer_storage_[index - 1U] = pending_transfer_storage_[index];
        pending_transfer_sizes_[index - 1U] = pending_transfer_sizes_[index];
    }
    --pending_transfer_count_;
    pending_transfer_storage_[pending_transfer_count_] = {};
    pending_transfer_sizes_[pending_transfer_count_] = 0U;
}

void Cdrom::clear_transfer_buffers() noexcept {
    transfer_storage_ = {};
    transfer_size_ = 0U;
    transfer_valid_ = false;
    pending_transfer_storage_ = {};
    pending_transfer_sizes_ = {};
    pending_transfer_count_ = 0U;
}

void Cdrom::end_data_stream() noexcept {
    reading_ = false;
    remove_events(ScheduledEvent::DataSector);
    (void)queue_status(Interrupt::DataEnd);
}

void Cdrom::end_cdda_stream() noexcept {
    playing_ = false;
    paused_ = false;
    motor_on_ = false;
    remove_events(ScheduledEvent::CddaSector);
    (void)queue_status(Interrupt::DataEnd);
}

std::uint8_t Cdrom::status_value() const noexcept {
    std::uint8_t value = 0U;
    if (playing_) {
        value = static_cast<std::uint8_t>(value | status_bit(Status::Playing));
    } else if (seeking_) {
        value = static_cast<std::uint8_t>(value | status_bit(Status::Seeking));
    } else if (reading_) {
        value = static_cast<std::uint8_t>(value | status_bit(Status::Reading));
    }
    if (motor_on_) {
        value = static_cast<std::uint8_t>(value | status_bit(Status::MotorOn));
    }
    if (shell_open_) {
        value = static_cast<std::uint8_t>(value | status_bit(Status::ShellOpen));
    }
    return value;
}

std::uint8_t Cdrom::host_status_value() const noexcept {
    std::uint8_t value = static_cast<std::uint8_t>(register_bank_ & 0x03U);
    if (parameter_count_ == 0U) {
        value = static_cast<std::uint8_t>(value | 0x08U);
    }
    if (parameter_count_ < ParameterFifoCapacity) {
        value = static_cast<std::uint8_t>(value | 0x10U);
    }
    if (response_unread_ != 0U) {
        value = static_cast<std::uint8_t>(value | 0x20U);
    }
    if (request_data_ && data_count() != 0U) {
        value = static_cast<std::uint8_t>(value | 0x40U);
    }
    if (command_busy_) {
        value = static_cast<std::uint8_t>(value | 0x80U);
    }
    return value;
}

std::uint8_t Cdrom::current_track() const noexcept {
    return layout_.track_at(current_lba_);
}

bool Cdrom::exact_parameter_count(std::span<const std::uint8_t> parameters,
                                  std::size_t expected) noexcept {
    return parameters.size() == expected;
}

bool Cdrom::decode_parameter_msf(std::span<const std::uint8_t> parameters,
                                 Lba& lba) noexcept {
    if (parameters.size() != 3U) {
        return false;
    }
    const std::array<std::uint8_t, 3U> bytes{
        parameters[0U], parameters[1U], parameters[2U]};
    Msf msf{};
    return decode_msf_bcd(std::span<const std::uint8_t, 3U>(bytes), msf) &&
           msf_to_lba(msf, lba);
}

std::int16_t Cdrom::signed_sample(std::uint16_t value) noexcept {
    const std::int32_t signed_value = value <= 0x7fffU
                                          ? static_cast<std::int32_t>(value)
                                          : static_cast<std::int32_t>(value) - 0x10000;
    return static_cast<std::int16_t>(signed_value);
}

std::int16_t Cdrom::saturate_audio(std::int64_t value) noexcept {
    const std::int64_t minimum = std::numeric_limits<std::int16_t>::min();
    const std::int64_t maximum = std::numeric_limits<std::int16_t>::max();
    if (value < minimum) {
        return std::numeric_limits<std::int16_t>::min();
    }
    if (value > maximum) {
        return std::numeric_limits<std::int16_t>::max();
    }
    return static_cast<std::int16_t>(value);
}

std::int16_t Cdrom::mix_audio(std::int32_t left,
                              std::int32_t right,
                              std::uint8_t left_gain,
                              std::uint8_t right_gain) noexcept {
    const std::int64_t mixed =
        static_cast<std::int64_t>(left) * left_gain +
        static_cast<std::int64_t>(right) * right_gain;
    return saturate_audio(mixed / 0x80);
}

} // namespace clean_aot::cdrom
