#include "sio.hpp"

#include <algorithm>
#include <limits>

namespace clean_aot::sio {

namespace {

[[nodiscard]] constexpr std::size_t port_index(Port port) noexcept {
    return static_cast<std::size_t>(port);
}

[[nodiscard]] constexpr std::uint8_t low_byte(std::uint16_t value) noexcept {
    return static_cast<std::uint8_t>(value & 0x00FFU);
}

[[nodiscard]] constexpr std::uint8_t high_byte(std::uint16_t value) noexcept {
    return static_cast<std::uint8_t>((value >> 8U) & 0x00FFU);
}

[[nodiscard]] constexpr std::uint8_t xor_frame(std::span<const std::uint8_t, 128> frame) noexcept {
    std::uint8_t checksum = 0;
    for (std::size_t index = 0; index < 127U; ++index) {
        checksum = static_cast<std::uint8_t>(checksum ^ frame[index]);
    }
    return checksum;
}

[[nodiscard]] constexpr std::uint64_t fnv_offset() noexcept {
    return 14695981039346656037ULL;
}

[[nodiscard]] constexpr std::uint64_t fnv_prime() noexcept {
    return 1099511628211ULL;
}

void fnv_add(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= fnv_prime();
}

void fnv_add_u16(std::uint64_t& hash, std::uint16_t value) noexcept {
    fnv_add(hash, static_cast<std::uint8_t>(value & 0x00FFU));
    fnv_add(hash, static_cast<std::uint8_t>((value >> 8U) & 0x00FFU));
}

void fnv_add_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        fnv_add(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] constexpr bool is_valid_phase(std::uint8_t value,
                                             std::uint8_t maximum) noexcept {
    return value <= maximum;
}

[[nodiscard]] constexpr bool cycle_add_fits(Cycle base, Cycle delta) noexcept {
    return base < kNoEventCycle && delta < (kNoEventCycle - base);
}

} // namespace

PadDevice::PadDevice() noexcept {
    reset();
}

DeviceKind PadDevice::kind() const noexcept {
    return DeviceKind::Controller;
}

void PadDevice::reset() noexcept {
    state_ = PadState{};
    state_.rumble_map.fill(0xFFU);
    selected_ = false;
    addressed_ = false;
    command_ = 0;
    phase_ = 0;
    pending_mode_ = 0;
    pending_key_ = 0;
    command_valid_ = false;
    transaction_done_ = false;
    last_cycle_ = 0;
    watchdog_deadline_ = 0;
    watchdog_active_ = false;
    configuration_used_ = false;
    id_high_suppressed_ = false;
}

void PadDevice::on_select(Cycle cycle) noexcept {
    advance_watchdog(cycle);
    selected_ = true;
    addressed_ = false;
    command_ = 0;
    phase_ = 0;
    pending_mode_ = 0;
    pending_key_ = 0;
    command_valid_ = false;
    transaction_done_ = false;
}

void PadDevice::on_deselect(Cycle cycle) noexcept {
    advance_watchdog(cycle);
    selected_ = false;
    addressed_ = false;
    command_ = 0;
    phase_ = 0;
    pending_mode_ = 0;
    pending_key_ = 0;
    command_valid_ = false;
    transaction_done_ = false;
}

void PadDevice::set_input(const PadInput& input) noexcept {
    state_.input = input;
}

PadState PadDevice::state() const noexcept {
    return state_;
}

bool PadDevice::set_analog_mode(bool enabled) noexcept {
    if (state_.mode_locked) {
        return false;
    }
    state_.analog_mode = enabled;
    state_.led_on = enabled;
    state_.rumble_small = 0;
    state_.rumble_large = 0;
    return true;
}

void PadDevice::advance_watchdog(Cycle cycle) noexcept {
    if (cycle == kNoEventCycle || cycle < last_cycle_) {
        return;
    }
    last_cycle_ = cycle;
    if (watchdog_active_ && cycle >= watchdog_deadline_) {
        reset_after_watchdog();
    }
}

void PadDevice::advance_to(Cycle cycle) noexcept {
    advance_watchdog(cycle);
}

void PadDevice::observe_communication(Cycle cycle) noexcept {
    advance_watchdog(cycle);
    if (selected_ && watchdog_active_) {
        start_watchdog(last_cycle_);
    }
}

void PadDevice::start_watchdog(Cycle cycle) noexcept {
    if (!cycle_add_fits(cycle, kPadConfigurationWatchdogCycles)) {
        reset_after_watchdog();
        return;
    }
    watchdog_active_ = true;
    watchdog_deadline_ = cycle + kPadConfigurationWatchdogCycles;
}

void PadDevice::reset_after_watchdog() noexcept {
    const PadInput input = state_.input;
    const bool had_configuration = configuration_used_;
    state_ = PadState{};
    state_.input = input;
    state_.rumble_map.fill(0xFFU);
    state_.mode_locked = true;
    addressed_ = false;
    command_ = 0;
    phase_ = 0;
    pending_mode_ = 0;
    pending_key_ = 0;
    command_valid_ = false;
    transaction_done_ = false;
    watchdog_deadline_ = 0;
    watchdog_active_ = false;
    configuration_used_ = had_configuration;
    id_high_suppressed_ = had_configuration;
}

void PadDevice::apply_analog_button(bool enabled) noexcept {
    const bool had_configuration = configuration_used_;
    state_.analog_mode = enabled;
    state_.configuration_mode = false;
    state_.led_on = enabled;
    state_.rumble_configured = false;
    state_.rumble_map.fill(0xFFU);
    state_.rumble_small = 0;
    state_.rumble_large = 0;
    state_.mode_locked = true;
    id_high_suppressed_ = had_configuration;
    watchdog_active_ = false;
    watchdog_deadline_ = 0;
    addressed_ = false;
    command_ = 0;
    phase_ = 0;
    pending_mode_ = 0;
    pending_key_ = 0;
    command_valid_ = false;
    transaction_done_ = false;
}

bool PadDevice::press_analog_button(Cycle cycle) noexcept {
    advance_watchdog(cycle);
    if (state_.mode_locked) {
        return false;
    }
    apply_analog_button(!state_.analog_mode);
    return true;
}

std::uint8_t PadDevice::normal_id() const noexcept {
    if (state_.analog_mode) {
        return 0x73U;
    }

    std::uint8_t halfwords = 1U;
    if (state_.rumble_configured) {
        for (std::size_t index = 0; index < state_.rumble_map.size(); ++index) {
            if (state_.rumble_map[index] != 0xFFU) {
                halfwords = std::max<std::uint8_t>(
                    halfwords, static_cast<std::uint8_t>((index / 2U) + 1U));
            }
        }
    }
    return static_cast<std::uint8_t>(0x40U | halfwords);
}

std::uint8_t PadDevice::response_id() const noexcept {
    return state_.configuration_mode ? 0xF3U : normal_id();
}

std::uint8_t PadDevice::response_id_high() const noexcept {
    return id_high_suppressed_ ? 0x00U : 0x5AU;
}

std::uint8_t PadDevice::normal_final_phase() const noexcept {
    const std::uint8_t halfwords = static_cast<std::uint8_t>(normal_id() & 0x0FU);
    return static_cast<std::uint8_t>((halfwords * 2U) + 1U);
}

std::uint8_t PadDevice::analog_or_padding(std::uint8_t phase) const noexcept {
    if (!state_.analog_mode) {
        return 0x00U;
    }
    switch (phase) {
    case 4U:
        return state_.input.right_x;
    case 5U:
        return state_.input.right_y;
    case 6U:
        return state_.input.left_x;
    case 7U:
        return state_.input.left_y;
    default:
        return 0x00U;
    }
}

ByteTransfer PadDevice::response(std::uint8_t value,
                                 std::uint8_t final_phase) noexcept {
    const bool more_data = phase_ < final_phase;
    if (more_data) {
        phase_ = static_cast<std::uint8_t>(phase_ + 1U);
    } else {
        finish_command();
    }
    return ByteTransfer{value,
                        more_data,
                        more_data ? kControllerAckDelayCycles : 0U,
                        more_data ? kAckPulseCycles : 0U};
}

void PadDevice::finish_command() noexcept {
    command_valid_ = false;
    command_ = 0;
    phase_ = 0;
    transaction_done_ = true;
}

void PadDevice::capture_rumble(std::uint8_t value) noexcept {
    if (phase_ < 2U || phase_ > 7U) {
        return;
    }
    const std::size_t map_index = static_cast<std::size_t>(phase_ - 2U);
    if (state_.rumble_configured) {
        const std::uint8_t mapping = state_.rumble_map[map_index];
        if (mapping == 0U) {
            state_.rumble_small = static_cast<std::uint8_t>(value & 0x01U);
        } else if (mapping == 1U) {
            state_.rumble_large = value;
        }
        return;
    }

    // The legacy two-byte protocol is intentionally kept as a narrow device
    // behavior: it controls the small motor only and is never a host API.
    if (phase_ == 2U) {
        pending_mode_ = value;
    } else if (phase_ == 3U) {
        const bool valid_x = (pending_mode_ & 0xC0U) == 0x40U;
        const bool valid_y = (value & 0x01U) != 0U;
        state_.rumble_small = static_cast<std::uint8_t>(valid_x && valid_y ? 1U : 0U);
    }
}

ByteTransfer PadDevice::exchange(std::uint8_t value, Cycle cycle) noexcept {
    observe_communication(cycle);
    if (!selected_) {
        return ByteTransfer{};
    }
    if (transaction_done_) {
        return ByteTransfer{};
    }
    if (!addressed_) {
        if (value != 0x01U) {
            return ByteTransfer{};
        }
        addressed_ = true;
        return ByteTransfer{0xFFU, true, kControllerAckDelayCycles, kAckPulseCycles};
    }
    if (!command_valid_) {
        command_ = value;
        phase_ = 1U;
        const bool normal_command = value == 0x42U || value == 0x43U;
        const bool configuration_command = value >= 0x40U && value <= 0x4FU;
        command_valid_ = normal_command ||
                         (state_.configuration_mode && configuration_command);
        if (!command_valid_) {
            if (state_.error_count != std::numeric_limits<std::uint8_t>::max()) {
                state_.error_count = static_cast<std::uint8_t>(state_.error_count + 1U);
            }
            finish_command();
            return ByteTransfer{0xFFU, false};
        }
        return ByteTransfer{response_id(), true, kControllerAckDelayCycles, kAckPulseCycles};
    }
    if (state_.configuration_mode) {
        return exchange_configuration(value, cycle);
    }
    return exchange_normal(value, cycle);
}

ByteTransfer PadDevice::exchange_normal(std::uint8_t value,
                                        Cycle cycle) noexcept {
    const std::uint8_t final_phase = normal_final_phase();
    if (command_ == 0x42U) {
        switch (phase_) {
        case 1U:
            return response(response_id_high(), final_phase);
        case 2U:
            capture_rumble(value);
            return response(low_byte(state_.input.buttons), final_phase);
        case 3U:
            capture_rumble(value);
            return response(high_byte(state_.input.buttons), final_phase);
        case 4U:
        case 5U:
        case 6U:
        case 7U:
            capture_rumble(value);
            return response(analog_or_padding(phase_), final_phase);
        default:
            finish_command();
            return ByteTransfer{};
        }
    }

    // Command 43 has the same normal-mode response as a poll, but byte 3
    // requests entry/exit from configuration mode.
    if (command_ == 0x43U) {
        switch (phase_) {
        case 1U:
            return response(response_id_high(), final_phase);
        case 2U:
            pending_mode_ = value;
            return response(low_byte(state_.input.buttons), final_phase);
        case 3U: {
            const std::uint8_t response_value = high_byte(state_.input.buttons);
            if (phase_ == final_phase) {
                state_.configuration_mode = pending_mode_ == 1U;
                if (state_.configuration_mode) {
                    configuration_used_ = true;
                    start_watchdog(cycle);
                }
            }
            return response(response_value, final_phase);
        }
        case 4U:
        case 5U:
        case 6U:
        case 7U: {
            const std::uint8_t response_value = analog_or_padding(phase_);
            if (phase_ == final_phase) {
                state_.configuration_mode = pending_mode_ == 1U;
                if (state_.configuration_mode) {
                    configuration_used_ = true;
                    start_watchdog(cycle);
                }
            }
            return response(response_value, final_phase);
        }
        default:
            finish_command();
            return ByteTransfer{};
        }
    }

    finish_command();
    return ByteTransfer{};
}

ByteTransfer PadDevice::exchange_configuration(std::uint8_t value,
                                               Cycle cycle) noexcept {
    (void)cycle;
    if (command_ == 0x42U) {
        switch (phase_) {
        case 1U:
            return response(response_id_high(), 7U);
        case 2U:
            capture_rumble(value);
            return response(low_byte(state_.input.buttons), 7U);
        case 3U:
            capture_rumble(value);
            return response(high_byte(state_.input.buttons), 7U);
        case 4U:
            capture_rumble(value);
            return response(state_.input.right_x, 7U);
        case 5U:
            capture_rumble(value);
            return response(state_.input.right_y, 7U);
        case 6U:
            capture_rumble(value);
            return response(state_.input.left_x, 7U);
        case 7U:
            capture_rumble(value);
            return response(state_.input.left_y, 7U);
        default:
            finish_command();
            return ByteTransfer{};
        }
    }

    if (command_ == 0x43U) {
        if (phase_ == 2U) {
            pending_mode_ = value;
        }
        if (phase_ == 7U) {
            if (pending_mode_ == 0U) {
                state_.configuration_mode = false;
                watchdog_active_ = false;
                watchdog_deadline_ = 0U;
            }
        }
        return response(phase_ == 1U ? response_id_high() : 0x00U, 7U);
    }

    if (command_ == 0x44U) {
        if (phase_ == 2U) {
            pending_mode_ = value;
        } else if (phase_ == 3U) {
            pending_key_ = value;
        }
        if (phase_ == 7U) {
            if (pending_mode_ == 0U) {
                state_.analog_mode = false;
                state_.led_on = false;
            } else if (pending_mode_ == 1U) {
                state_.analog_mode = true;
                state_.led_on = true;
            } else if (state_.error_count != std::numeric_limits<std::uint8_t>::max()) {
                state_.error_count = static_cast<std::uint8_t>(state_.error_count + 1U);
            }
            state_.mode_locked = (pending_key_ & 0x03U) == 0x03U;
        }
        return response(phase_ == 1U ? response_id_high() : 0x00U, 7U);
    }

    if (command_ == 0x45U) {
        switch (phase_) {
        case 1U:
            return response(response_id_high(), 7U);
        case 2U:
            return response(0x01U, 7U);
        case 3U:
            return response(0x02U, 7U);
        case 4U:
            return response(state_.led_on ? 0x01U : 0x00U, 7U);
        case 5U:
            return response(0x02U, 7U);
        case 6U:
            return response(0x01U, 7U);
        case 7U:
            return response(0x00U, 7U);
        default:
            finish_command();
            return ByteTransfer{};
        }
    }

    if (command_ == 0x46U) {
        if (phase_ == 2U) {
            pending_mode_ = value;
        }
        const std::uint8_t selector = pending_mode_;
        switch (phase_) {
        case 1U:
            return response(response_id_high(), 7U);
        case 2U:
        case 3U:
            return response(0x00U, 7U);
        case 4U:
            return response(0x01U, 7U);
        case 5U:
            return response(selector == 0U ? 0x02U : (selector == 1U ? 0x01U : 0x00U), 7U);
        case 6U:
            return response(selector == 0U ? 0x00U : (selector == 1U ? 0x01U : 0x00U), 7U);
        case 7U:
            return response(selector == 0U ? 0x0AU : (selector == 1U ? 0x14U : 0x00U), 7U);
        default:
            finish_command();
            return ByteTransfer{};
        }
    }

    if (command_ == 0x47U) {
        const std::array<std::uint8_t, 7> values = {response_id_high(), 0x00U, 0x00U,
                                                     0x02U, 0x00U, 0x01U, 0x00U};
        return response(values[static_cast<std::size_t>(phase_ - 1U)], 7U);
    }

    if (command_ == 0x48U) {
        if (phase_ == 2U) {
            pending_mode_ = value;
        }
        const std::uint8_t value_out = phase_ == 1U ? response_id_high()
                                  : (phase_ == 6U && pending_mode_ <= 1U ? 0x01U : 0x00U);
        return response(value_out, 7U);
    }

    if (command_ == 0x4CU) {
        if (phase_ == 2U) {
            pending_mode_ = value;
        }
        const std::uint8_t selector = pending_mode_;
        const std::uint8_t value_out = phase_ == 1U ? response_id_high()
                                  : (phase_ == 5U ? (selector == 0U ? 0x04U
                                                                    : (selector == 1U ? 0x07U : 0x00U))
                                                  : 0x00U);
        return response(value_out, 7U);
    }

    if (command_ == 0x4DU) {
        const std::uint8_t old_value = phase_ == 1U ? response_id_high()
                                     : (phase_ <= 7U ? state_.rumble_map[static_cast<std::size_t>(phase_ - 2U)]
                                                     : 0x00U);
        if (phase_ >= 2U && phase_ <= 7U) {
            state_.rumble_map[static_cast<std::size_t>(phase_ - 2U)] = value;
        }
        if (phase_ == 7U) {
            state_.rumble_configured = true;
        }
        return response(old_value, 7U);
    }

    // 40h, 41h, 49h, 4Ah, 4Bh, 4Eh and 4Fh are bounded zero-response
    // configuration commands for this PSX DualShock-compatible boundary.
    return response(phase_ == 1U ? response_id_high() : 0x00U, 7U);
}

PadSnapshot PadDevice::snapshot() const noexcept {
    PadSnapshot value{};
    value.state = state_;
    value.selected = selected_;
    value.addressed = addressed_;
    value.command = command_;
    value.phase = phase_;
    value.pending_mode = pending_mode_;
    value.pending_key = pending_key_;
    value.command_valid = command_valid_;
    value.transaction_done = transaction_done_;
    value.last_cycle = last_cycle_;
    value.watchdog_deadline = watchdog_deadline_;
    value.watchdog_active = watchdog_active_;
    value.configuration_used = configuration_used_;
    value.id_high_suppressed = id_high_suppressed_;
    return value;
}

bool PadDevice::restore(const PadSnapshot& snapshot_value) noexcept {
    const bool partial_rumble_command =
        snapshot_value.command_valid && snapshot_value.command == 0x4DU &&
        snapshot_value.state.configuration_mode && snapshot_value.phase >= 2U &&
        snapshot_value.phase <= 7U;
    if (!snapshot_value.state.rumble_configured && !partial_rumble_command &&
        !std::all_of(snapshot_value.state.rumble_map.begin(),
                     snapshot_value.state.rumble_map.end(),
                     [](std::uint8_t mapping) { return mapping == 0xFFU; })) {
        return false;
    }
    if (snapshot_value.command_valid &&
        (!is_valid_phase(snapshot_value.phase, 7U) || snapshot_value.phase == 0U)) {
        return false;
    }
    if (!snapshot_value.command_valid && snapshot_value.phase != 0U) {
        return false;
    }
    if (!snapshot_value.command_valid && snapshot_value.command != 0U) {
        return false;
    }
    if (snapshot_value.addressed && !snapshot_value.selected) {
        return false;
    }
    if (!snapshot_value.addressed &&
        (snapshot_value.command_valid || snapshot_value.transaction_done ||
         snapshot_value.command != 0U || snapshot_value.phase != 0U)) {
        return false;
    }
    if ((snapshot_value.command_valid || snapshot_value.transaction_done) &&
        (!snapshot_value.selected || !snapshot_value.addressed)) {
        return false;
    }
    if (snapshot_value.command_valid && snapshot_value.transaction_done) {
        return false;
    }
    if (!snapshot_value.selected &&
        (snapshot_value.pending_mode != 0U || snapshot_value.pending_key != 0U)) {
        return false;
    }
    if (snapshot_value.state.rumble_small > 1U) {
        return false;
    }
    if (partial_rumble_command) {
        const std::size_t completed = static_cast<std::size_t>(snapshot_value.phase - 2U);
        if (snapshot_value.state.rumble_configured ||
            !std::all_of(snapshot_value.state.rumble_map.begin() +
                             static_cast<std::ptrdiff_t>(completed),
                         snapshot_value.state.rumble_map.end(),
                         [](std::uint8_t mapping) { return mapping == 0xFFU; })) {
            return false;
        }
    }
    if (snapshot_value.last_cycle == kNoEventCycle ||
        (snapshot_value.watchdog_active &&
         (!snapshot_value.configuration_used ||
          snapshot_value.watchdog_deadline <= snapshot_value.last_cycle ||
          snapshot_value.watchdog_deadline >= kNoEventCycle ||
          snapshot_value.watchdog_deadline - snapshot_value.last_cycle !=
              kPadConfigurationWatchdogCycles ||
          !cycle_add_fits(snapshot_value.last_cycle,
                          snapshot_value.watchdog_deadline - snapshot_value.last_cycle))) ||
        (!snapshot_value.watchdog_active && snapshot_value.watchdog_deadline != 0U) ||
        (snapshot_value.state.configuration_mode &&
         (!snapshot_value.configuration_used || !snapshot_value.watchdog_active)) ||
        (snapshot_value.id_high_suppressed &&
         (snapshot_value.state.configuration_mode || !snapshot_value.configuration_used))) {
        return false;
    }
    const bool normal_command = snapshot_value.command == 0x42U || snapshot_value.command == 0x43U;
    const bool configuration_command = snapshot_value.command >= 0x40U && snapshot_value.command <= 0x4FU;
    if (snapshot_value.command_valid &&
        ((!snapshot_value.state.configuration_mode && !normal_command) ||
         (snapshot_value.state.configuration_mode && !configuration_command))) {
        return false;
    }
    if (snapshot_value.command_valid && !snapshot_value.state.configuration_mode) {
        std::uint8_t halfwords = 1U;
        if (snapshot_value.state.analog_mode) {
            halfwords = 3U;
        } else if (snapshot_value.state.rumble_configured) {
            for (std::size_t index = 0U;
                 index < snapshot_value.state.rumble_map.size(); ++index) {
                if (snapshot_value.state.rumble_map[index] != 0xFFU) {
                    halfwords = std::max<std::uint8_t>(
                        halfwords, static_cast<std::uint8_t>((index / 2U) + 1U));
                }
            }
        }
        const std::uint8_t final_phase = static_cast<std::uint8_t>(
            (halfwords * 2U) + 1U);
        if (snapshot_value.phase > final_phase) {
            return false;
        }
    }
    state_ = snapshot_value.state;
    selected_ = snapshot_value.selected;
    addressed_ = snapshot_value.addressed;
    command_ = snapshot_value.command;
    phase_ = snapshot_value.phase;
    pending_mode_ = snapshot_value.pending_mode;
    pending_key_ = snapshot_value.pending_key;
    command_valid_ = snapshot_value.command_valid;
    transaction_done_ = snapshot_value.transaction_done;
    last_cycle_ = snapshot_value.last_cycle;
    watchdog_deadline_ = snapshot_value.watchdog_deadline;
    watchdog_active_ = snapshot_value.watchdog_active;
    configuration_used_ = snapshot_value.configuration_used;
    id_high_suppressed_ = snapshot_value.id_high_suppressed;
    return true;
}

MemoryCard::MemoryCard(std::span<std::uint8_t> image) noexcept
    : image_(image) {
    reset();
    rebuild_fingerprint_index();
}

DeviceKind MemoryCard::kind() const noexcept {
    return DeviceKind::MemoryCard;
}

void MemoryCard::reset() noexcept {
    phase_ = Phase::Idle;
    selected_ = false;
    valid_sector_ = false;
    pending_write_valid_ = false;
    address_ = 0;
    data_index_ = 0;
    checksum_ = 0;
    received_checksum_ = 0;
    previous_value_ = 0;
    flag_ = 0x08U;
    write_generation_ = 0;
    error_count_ = 0;
    pending_data_.fill(0);
}

void MemoryCard::clear_transaction() noexcept {
    phase_ = Phase::Idle;
    selected_ = false;
    valid_sector_ = false;
    pending_write_valid_ = false;
    address_ = 0;
    data_index_ = 0;
    checksum_ = 0;
    received_checksum_ = 0;
    previous_value_ = 0;
    pending_data_.fill(0);
}

void MemoryCard::on_select(Cycle cycle) noexcept {
    (void)cycle;
    clear_transaction();
    selected_ = true;
}

void MemoryCard::on_deselect(Cycle cycle) noexcept {
    (void)cycle;
    clear_transaction();
}

bool MemoryCard::valid() const noexcept {
    return image_.size() == kImageSize && image_.data() != nullptr;
}

std::uint64_t MemoryCard::image_fingerprint() const noexcept {
    return calculated_image_fingerprint();
}

std::uint64_t MemoryCard::sector_fingerprint(std::size_t sector) const noexcept {
    if (!valid()) {
        return 0;
    }
    std::uint64_t hash = fnv_offset();
    fnv_add_u32(hash, static_cast<std::uint32_t>(sector));
    const std::size_t offset = sector * kSectorSize;
    for (std::size_t index = 0; index < kSectorSize; ++index) {
        const std::uint8_t byte = image_[offset + index];
        fnv_add(hash, byte);
    }
    return hash;
}

std::uint64_t MemoryCard::calculated_image_fingerprint() const noexcept {
    if (!valid()) {
        return 0;
    }
    std::uint64_t fingerprint = fnv_offset();
    for (std::size_t sector = 0; sector < kSectorCount; ++sector) {
        fingerprint ^= sector_fingerprint(sector);
    }
    return fingerprint;
}

void MemoryCard::rebuild_fingerprint_index() noexcept {
    indexed_image_fingerprint_ = valid() ? fnv_offset() : 0U;
    for (std::size_t sector = 0; sector < kSectorCount; ++sector) {
        sector_fingerprints_[sector] = sector_fingerprint(sector);
        indexed_image_fingerprint_ ^= sector_fingerprints_[sector];
    }
}

std::uint32_t MemoryCard::write_generation() const noexcept {
    return write_generation_;
}

std::uint32_t MemoryCard::error_count() const noexcept {
    return error_count_;
}

std::uint8_t MemoryCard::flag() const noexcept {
    return flag_;
}

void MemoryCard::set_write_callback(WriteDurabilityCallback callback,
                                     void* context) noexcept {
    write_callback_ = callback;
    write_context_ = context;
}

ByteTransfer MemoryCard::response(std::uint8_t value,
                                  bool more_data,
                                  Cycle extra_ack_delay) noexcept {
    if (!more_data) {
        phase_ = Phase::Done;
    }
    return ByteTransfer{value,
                        more_data,
                        more_data ? kMemoryCardAckDelayCycles + extra_ack_delay : 0U,
                        more_data ? kAckPulseCycles : 0U};
}

bool MemoryCard::valid_address() const noexcept {
    return address_ < static_cast<std::uint16_t>(kSectorCount);
}

std::size_t MemoryCard::image_offset() const noexcept {
    return static_cast<std::size_t>(address_) * kSectorSize;
}

void MemoryCard::fail_command() noexcept {
    if (error_count_ != std::numeric_limits<std::uint32_t>::max()) {
        ++error_count_;
    }
    pending_write_valid_ = false;
    phase_ = Phase::Done;
}

void MemoryCard::commit_pending_write() noexcept {
    if (!pending_write_valid_ || !valid() || !valid_sector_) {
        return;
    }
    const std::size_t offset = image_offset();
    const std::size_t sector = static_cast<std::size_t>(address_);
    indexed_image_fingerprint_ ^= sector_fingerprints_[sector];
    std::copy_n(pending_data_.begin(), kSectorSize, image_.begin() + static_cast<std::ptrdiff_t>(offset));
    sector_fingerprints_[sector] = sector_fingerprint(sector);
    indexed_image_fingerprint_ ^= sector_fingerprints_[sector];
    if (write_generation_ != std::numeric_limits<std::uint32_t>::max()) {
        ++write_generation_;
    }
    flag_ = static_cast<std::uint8_t>(flag_ & static_cast<std::uint8_t>(~0x08U));
    pending_write_valid_ = false;
    const WriteReceipt receipt{address_, write_generation_, indexed_image_fingerprint_,
                               received_checksum_};
    if (write_callback_ != nullptr) {
        write_callback_(write_context_, receipt);
    }
}

ByteTransfer MemoryCard::exchange(std::uint8_t value, Cycle cycle) noexcept {
    (void)cycle;
    if (!selected_ || !valid()) {
        return ByteTransfer{};
    }
    switch (phase_) {
    case Phase::Idle:
        if (value != 0x81U) {
            return ByteTransfer{};
        }
        phase_ = Phase::Command;
        return response(0xFFU, true);
    case Phase::Command:
        if (value == 0x52U) {
            phase_ = Phase::ReadIdOne;
            return response(flag_, true);
        }
        if (value == 0x57U) {
            phase_ = Phase::WriteIdOne;
            return response(flag_, true);
        }
        if (value == 0x53U) {
            phase_ = Phase::IdDeviceOne;
            return response(flag_, true);
        }
        {
            const std::uint8_t response_flag = flag_;
            fail_command();
            return ByteTransfer{response_flag, false, 0U, 0U};
        }
    case Phase::ReadIdOne:
        phase_ = Phase::ReadIdTwo;
        return response(0x5AU, true);
    case Phase::ReadIdTwo:
        phase_ = Phase::ReadAddressHigh;
        return response(0x5DU, true);
    case Phase::ReadAddressHigh:
        address_ = static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) << 8U);
        previous_value_ = value;
        phase_ = Phase::ReadAddressLow;
        return response(0x00U, true);
    case Phase::ReadAddressLow: {
        const std::uint8_t response_value = previous_value_;
        address_ = static_cast<std::uint16_t>(address_ | value);
        previous_value_ = value;
        valid_sector_ = valid_address();
        checksum_ = static_cast<std::uint8_t>(high_byte(address_) ^ low_byte(address_));
        phase_ = Phase::ReadConfirmOne;
        return response(response_value, true);
    }
    case Phase::ReadConfirmOne:
        phase_ = Phase::ReadConfirmTwo;
        return response(0x5CU, true, kMemoryCardReadLateAckExtraCycles);
    case Phase::ReadConfirmTwo:
        phase_ = Phase::ReadConfirmHigh;
        return response(0x5DU, true);
    case Phase::ReadConfirmHigh:
        phase_ = Phase::ReadConfirmLow;
        return response(valid_sector_ ? high_byte(address_) : 0xFFU, true);
    case Phase::ReadConfirmLow:
        if (!valid_sector_) {
            phase_ = Phase::Done;
            return ByteTransfer{0xFFU, false};
        }
        data_index_ = 0;
        phase_ = Phase::ReadData;
        return response(low_byte(address_), true);
    case Phase::ReadData: {
        if (!valid_sector_ || !valid_address() || data_index_ >= kSectorSize) {
            fail_command();
            return ByteTransfer{};
        }
        const std::uint8_t response_value = image_[image_offset() + data_index_];
        checksum_ = static_cast<std::uint8_t>(checksum_ ^ response_value);
        if (data_index_ == 127U) {
            phase_ = Phase::ReadChecksum;
        } else {
            ++data_index_;
        }
        return response(response_value, true);
    }
    case Phase::ReadChecksum:
        phase_ = Phase::ReadEnd;
        return response(checksum_, true);
    case Phase::ReadEnd:
        return response(0x47U, false);
    case Phase::WriteIdOne:
        phase_ = Phase::WriteIdTwo;
        return response(0x5AU, true);
    case Phase::WriteIdTwo:
        phase_ = Phase::WriteAddressHigh;
        return response(0x5DU, true);
    case Phase::WriteAddressHigh:
        address_ = static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) << 8U);
        previous_value_ = value;
        phase_ = Phase::WriteAddressLow;
        return response(0x00U, true);
    case Phase::WriteAddressLow: {
        const std::uint8_t response_value = previous_value_;
        address_ = static_cast<std::uint16_t>(address_ | value);
        previous_value_ = value;
        valid_sector_ = valid_address();
        checksum_ = static_cast<std::uint8_t>(high_byte(address_) ^ low_byte(address_));
        data_index_ = 0;
        pending_write_valid_ = valid_sector_;
        phase_ = Phase::WriteData;
        return response(response_value, true);
    }
    case Phase::WriteData: {
        const std::uint8_t response_value = previous_value_;
        pending_data_[data_index_] = value;
        checksum_ = static_cast<std::uint8_t>(checksum_ ^ value);
        previous_value_ = value;
        if (data_index_ == 127U) {
            phase_ = Phase::WriteChecksum;
        } else {
            ++data_index_;
        }
        return response(response_value, true);
    }
    case Phase::WriteChecksum: {
        const std::uint8_t response_value = previous_value_;
        received_checksum_ = value;
        previous_value_ = value;
        pending_write_valid_ = pending_write_valid_ && received_checksum_ == checksum_;
        if (!pending_write_valid_ && valid_sector_ && error_count_ != std::numeric_limits<std::uint32_t>::max()) {
            ++error_count_;
            flag_ = static_cast<std::uint8_t>(flag_ | 0x04U);
        }
        phase_ = Phase::WriteConfirmOne;
        return response(response_value, true);
    }
    case Phase::WriteConfirmOne:
        phase_ = Phase::WriteConfirmTwo;
        return response(0x5CU, true);
    case Phase::WriteConfirmTwo:
        phase_ = Phase::WriteEnd;
        return response(0x5DU, true);
    case Phase::WriteEnd: {
        const std::uint8_t end_value = !valid_sector_ ? 0xFFU : (pending_write_valid_ ? 0x47U : 0x4EU);
        if (pending_write_valid_) {
            // Finalize the protocol state before entering caller code. A
            // durability callback may inspect the card, but must never observe
            // a half-committed WriteEnd transaction.
            phase_ = Phase::Done;
            commit_pending_write();
            return ByteTransfer{end_value, false, 0U, 0U};
        }
        return response(end_value, false);
    }
    case Phase::IdDeviceOne:
        phase_ = Phase::IdDeviceTwo;
        return response(0x5AU, true);
    case Phase::IdDeviceTwo:
        phase_ = Phase::IdConfirmOne;
        return response(0x5DU, true);
    case Phase::IdConfirmOne:
        phase_ = Phase::IdConfirmTwo;
        return response(0x5CU, true);
    case Phase::IdConfirmTwo:
        phase_ = Phase::IdByteZero;
        return response(0x5DU, true);
    case Phase::IdByteZero:
        phase_ = Phase::IdByteOne;
        return response(0x04U, true);
    case Phase::IdByteOne:
        phase_ = Phase::IdByteTwo;
        return response(0x00U, true);
    case Phase::IdByteTwo:
        phase_ = Phase::IdByteThree;
        return response(0x00U, true);
    case Phase::IdByteThree:
        return response(0x80U, false);
    case Phase::Done:
        return ByteTransfer{};
    default:
        fail_command();
        return ByteTransfer{};
    }
}

std::uint64_t MemoryCard::protocol_fingerprint() const noexcept {
    std::uint64_t hash = fnv_offset();
    fnv_add(hash, static_cast<std::uint8_t>(phase_));
    fnv_add(hash, selected_ ? 1U : 0U);
    fnv_add(hash, valid_sector_ ? 1U : 0U);
    fnv_add(hash, pending_write_valid_ ? 1U : 0U);
    fnv_add_u16(hash, address_);
    fnv_add(hash, flag_);
    fnv_add(hash, data_index_);
    fnv_add(hash, checksum_);
    fnv_add(hash, received_checksum_);
    fnv_add(hash, previous_value_);
    fnv_add_u32(hash, write_generation_);
    fnv_add_u32(hash, error_count_);
    for (const std::uint8_t byte : pending_data_) {
        fnv_add(hash, byte);
    }
    return hash;
}

MemoryCardSnapshot MemoryCard::snapshot() const noexcept {
    MemoryCardSnapshot value{};
    value.image_fingerprint = image_fingerprint();
    value.protocol_fingerprint = protocol_fingerprint();
    value.write_generation = write_generation_;
    value.error_count = error_count_;
    value.address = address_;
    value.flag = flag_;
    value.phase = static_cast<std::uint8_t>(phase_);
    value.data_index = data_index_;
    value.checksum = checksum_;
    value.received_checksum = received_checksum_;
    value.previous_value = previous_value_;
    value.selected = selected_;
    value.valid_sector = valid_sector_;
    value.pending_write_valid = pending_write_valid_;
    value.pending_data = pending_data_;
    return value;
}

bool MemoryCard::restore(const MemoryCardSnapshot& snapshot_value) noexcept {
    constexpr std::uint8_t last_phase = static_cast<std::uint8_t>(Phase::Done);
    const std::uint64_t current_image_fingerprint = calculated_image_fingerprint();
    if (!valid() || snapshot_value.image_fingerprint != current_image_fingerprint ||
        snapshot_value.phase > last_phase || snapshot_value.data_index > 127U) {
        return false;
    }
    const Phase restored_phase = static_cast<Phase>(snapshot_value.phase);
    if ((!snapshot_value.selected && restored_phase != Phase::Idle) ||
        (snapshot_value.flag & 0xF3U) != 0U) {
        return false;
    }

    const bool address_is_valid =
        snapshot_value.address < static_cast<std::uint16_t>(kSectorCount);
    const std::uint8_t address_checksum = static_cast<std::uint8_t>(
        high_byte(snapshot_value.address) ^ low_byte(snapshot_value.address));
    const bool pending_data_is_zero = std::all_of(
        snapshot_value.pending_data.begin(), snapshot_value.pending_data.end(),
        [](std::uint8_t byte) { return byte == 0U; });
    const bool transaction_is_zero =
        !snapshot_value.valid_sector && !snapshot_value.pending_write_valid &&
        snapshot_value.address == 0U && snapshot_value.data_index == 0U &&
        snapshot_value.checksum == 0U && snapshot_value.received_checksum == 0U &&
        snapshot_value.previous_value == 0U && pending_data_is_zero;
    const auto pending_tail_is_zero = [&](std::size_t first) noexcept {
        return std::all_of(
            snapshot_value.pending_data.begin() + static_cast<std::ptrdiff_t>(first),
            snapshot_value.pending_data.end(), [](std::uint8_t byte) { return byte == 0U; });
    };
    const auto read_checksum = [&](std::size_t byte_count) noexcept {
        std::uint8_t checksum = address_checksum;
        if (!address_is_valid) {
            return checksum;
        }
        const std::size_t offset = static_cast<std::size_t>(snapshot_value.address) *
                                   kSectorSize;
        for (std::size_t index = 0U; index < byte_count; ++index) {
            checksum = static_cast<std::uint8_t>(checksum ^ image_[offset + index]);
        }
        return checksum;
    };
    const auto write_checksum = [&](std::size_t byte_count) noexcept {
        std::uint8_t checksum = address_checksum;
        for (std::size_t index = 0U; index < byte_count; ++index) {
            checksum = static_cast<std::uint8_t>(checksum ^ snapshot_value.pending_data[index]);
        }
        return checksum;
    };

    const bool write_can_be_pending = restored_phase == Phase::WriteData ||
                                      restored_phase == Phase::WriteChecksum ||
                                      restored_phase == Phase::WriteConfirmOne ||
                                      restored_phase == Phase::WriteConfirmTwo ||
                                      restored_phase == Phase::WriteEnd;
    if (snapshot_value.pending_write_valid && !write_can_be_pending) {
        return false;
    }

    bool semantic_state_is_valid = false;
    switch (restored_phase) {
    case Phase::Idle:
    case Phase::Command:
    case Phase::ReadIdOne:
    case Phase::ReadIdTwo:
    case Phase::WriteIdOne:
    case Phase::WriteIdTwo:
    case Phase::IdDeviceOne:
    case Phase::IdDeviceTwo:
    case Phase::IdConfirmOne:
    case Phase::IdConfirmTwo:
    case Phase::IdByteZero:
    case Phase::IdByteOne:
    case Phase::IdByteTwo:
    case Phase::IdByteThree:
        semantic_state_is_valid = transaction_is_zero;
        break;
    case Phase::ReadAddressHigh:
    case Phase::WriteAddressHigh:
        semantic_state_is_valid = transaction_is_zero;
        break;
    case Phase::ReadAddressLow:
    case Phase::WriteAddressLow:
        semantic_state_is_valid = !snapshot_value.valid_sector &&
                                  !snapshot_value.pending_write_valid &&
                                  low_byte(snapshot_value.address) == 0U &&
                                  snapshot_value.data_index == 0U &&
                                  snapshot_value.checksum == 0U &&
                                  snapshot_value.received_checksum == 0U &&
                                  snapshot_value.previous_value ==
                                      high_byte(snapshot_value.address) &&
                                  pending_data_is_zero;
        break;
    case Phase::ReadConfirmOne:
    case Phase::ReadConfirmTwo:
    case Phase::ReadConfirmHigh:
    case Phase::ReadConfirmLow:
        semantic_state_is_valid = snapshot_value.valid_sector == address_is_valid &&
                                  !snapshot_value.pending_write_valid &&
                                  snapshot_value.data_index == 0U &&
                                  snapshot_value.checksum == address_checksum &&
                                  snapshot_value.received_checksum == 0U &&
                                  snapshot_value.previous_value == low_byte(snapshot_value.address) &&
                                  pending_data_is_zero;
        break;
    case Phase::ReadData:
        // A ReadData state is only reachable after the card has confirmed an
        // in-range sector. This predicate is deliberately stronger than the
        // bounds check in exchange(): a forged snapshot cannot defer an OOB
        // decision until the next byte clocks.
        semantic_state_is_valid = address_is_valid && snapshot_value.valid_sector &&
                                  !snapshot_value.pending_write_valid &&
                                  snapshot_value.received_checksum == 0U &&
                                  snapshot_value.previous_value == low_byte(snapshot_value.address) &&
                                  pending_data_is_zero &&
                                  snapshot_value.checksum == read_checksum(snapshot_value.data_index);
        break;
    case Phase::ReadChecksum:
    case Phase::ReadEnd:
        semantic_state_is_valid = address_is_valid && snapshot_value.valid_sector &&
                                  !snapshot_value.pending_write_valid &&
                                  snapshot_value.data_index == 127U &&
                                  snapshot_value.received_checksum == 0U &&
                                  snapshot_value.previous_value == low_byte(snapshot_value.address) &&
                                  pending_data_is_zero &&
                                  snapshot_value.checksum == read_checksum(kSectorSize);
        break;
    case Phase::WriteData: {
        const std::size_t completed_bytes = snapshot_value.data_index;
        const std::uint8_t expected_previous =
            completed_bytes == 0U
                ? low_byte(snapshot_value.address)
                : snapshot_value.pending_data[completed_bytes - 1U];
        semantic_state_is_valid = snapshot_value.valid_sector == address_is_valid &&
                                  snapshot_value.pending_write_valid == address_is_valid &&
                                  snapshot_value.received_checksum == 0U &&
                                  snapshot_value.previous_value == expected_previous &&
                                  pending_tail_is_zero(completed_bytes) &&
                                  snapshot_value.checksum == write_checksum(completed_bytes);
        break;
    }
    case Phase::WriteChecksum:
        semantic_state_is_valid = snapshot_value.valid_sector == address_is_valid &&
                                  snapshot_value.pending_write_valid == address_is_valid &&
                                  snapshot_value.data_index == 127U &&
                                  snapshot_value.received_checksum == 0U &&
                                  snapshot_value.previous_value == snapshot_value.pending_data.back() &&
                                  snapshot_value.checksum == write_checksum(kSectorSize);
        break;
    case Phase::WriteConfirmOne:
    case Phase::WriteConfirmTwo:
    case Phase::WriteEnd: {
        const bool checksum_matches =
            snapshot_value.received_checksum == snapshot_value.checksum;
        semantic_state_is_valid = snapshot_value.valid_sector == address_is_valid &&
                                  snapshot_value.pending_write_valid ==
                                      (address_is_valid && checksum_matches) &&
                                  snapshot_value.data_index == 127U &&
                                  snapshot_value.previous_value == snapshot_value.received_checksum &&
                                  snapshot_value.checksum == write_checksum(kSectorSize) &&
                                  (!address_is_valid || checksum_matches ||
                                   (snapshot_value.flag & 0x04U) != 0U);
        break;
    }
    case Phase::Done: {
        const bool completed_invalid_read =
            !address_is_valid && !snapshot_value.valid_sector &&
            !snapshot_value.pending_write_valid && snapshot_value.data_index == 0U &&
            snapshot_value.checksum == address_checksum &&
            snapshot_value.received_checksum == 0U &&
            snapshot_value.previous_value == low_byte(snapshot_value.address) &&
            pending_data_is_zero;
        const bool completed_read =
            address_is_valid && snapshot_value.valid_sector &&
            !snapshot_value.pending_write_valid && snapshot_value.data_index == 127U &&
            snapshot_value.checksum == read_checksum(kSectorSize) &&
            snapshot_value.received_checksum == 0U &&
            snapshot_value.previous_value == low_byte(snapshot_value.address) &&
            pending_data_is_zero;
        const bool checksum_matches =
            snapshot_value.received_checksum == snapshot_value.checksum;
        const bool completed_invalid_write =
            !address_is_valid && !snapshot_value.valid_sector &&
            !snapshot_value.pending_write_valid && snapshot_value.data_index == 127U &&
            snapshot_value.previous_value == snapshot_value.received_checksum &&
            snapshot_value.checksum == write_checksum(kSectorSize);
        const bool committed_image_matches =
            !address_is_valid || !checksum_matches ||
            std::equal(snapshot_value.pending_data.begin(), snapshot_value.pending_data.end(),
                       image_.begin() + static_cast<std::ptrdiff_t>(
                                            static_cast<std::size_t>(snapshot_value.address) *
                                            kSectorSize));
        const bool completed_write =
            address_is_valid && snapshot_value.valid_sector &&
            !snapshot_value.pending_write_valid && snapshot_value.data_index == 127U &&
            snapshot_value.previous_value == snapshot_value.received_checksum &&
            snapshot_value.checksum == write_checksum(kSectorSize) &&
            (!checksum_matches || (snapshot_value.flag & 0x08U) == 0U) &&
            (!checksum_matches || committed_image_matches) &&
            (!checksum_matches || (snapshot_value.flag & 0x04U) != 0U ||
             committed_image_matches);
        semantic_state_is_valid = transaction_is_zero || completed_invalid_read ||
                                  completed_read || completed_invalid_write || completed_write;
        break;
    }
    default:
        semantic_state_is_valid = false;
        break;
    }
    if (!semantic_state_is_valid) {
        return false;
    }

    const MemoryCardSnapshot before = snapshot();
    phase_ = static_cast<Phase>(snapshot_value.phase);
    selected_ = snapshot_value.selected;
    valid_sector_ = snapshot_value.valid_sector;
    pending_write_valid_ = snapshot_value.pending_write_valid;
    address_ = snapshot_value.address;
    data_index_ = snapshot_value.data_index;
    checksum_ = snapshot_value.checksum;
    received_checksum_ = snapshot_value.received_checksum;
    previous_value_ = snapshot_value.previous_value;
    flag_ = snapshot_value.flag;
    write_generation_ = snapshot_value.write_generation;
    error_count_ = snapshot_value.error_count;
    pending_data_ = snapshot_value.pending_data;
    if (protocol_fingerprint() != snapshot_value.protocol_fingerprint) {
        phase_ = static_cast<Phase>(before.phase);
        selected_ = before.selected;
        valid_sector_ = before.valid_sector;
        pending_write_valid_ = before.pending_write_valid;
        address_ = before.address;
        data_index_ = before.data_index;
        checksum_ = before.checksum;
        received_checksum_ = before.received_checksum;
        previous_value_ = before.previous_value;
        flag_ = before.flag;
        write_generation_ = before.write_generation;
        error_count_ = before.error_count;
        pending_data_ = before.pending_data;
        return false;
    }
    // A caller may have lawfully replaced the backing bytes between idle
    // snapshot boundaries.  A successful restore re-synchronizes the O(1)
    // write-receipt index after the full-image identity check above.
    rebuild_fingerprint_index();
    return true;
}

bool MemoryCard::format_new_image(std::span<std::uint8_t> image) noexcept {
    if (image.size() != kImageSize || image.data() == nullptr) {
        return false;
    }
    std::fill(image.begin(), image.end(), 0xFFU);

    const auto frame = [&image](std::size_t index) {
        return std::span<std::uint8_t, 128>(image.data() + (index * kSectorSize), 128U);
    };

    auto header = frame(0U);
    std::fill(header.begin(), header.end(), 0U);
    header[0] = 0x4DU;
    header[1] = 0x43U;
    header[127] = xor_frame(std::span<const std::uint8_t, 128>(header));

    for (std::size_t index = 1U; index <= 15U; ++index) {
        auto directory = frame(index);
        std::fill(directory.begin(), directory.end(), 0U);
        directory[0] = 0xA0U;
        directory[8] = 0xFFU;
        directory[9] = 0xFFU;
        directory[127] = xor_frame(std::span<const std::uint8_t, 128>(directory));
    }

    for (std::size_t index = 16U; index <= 35U; ++index) {
        auto broken = frame(index);
        std::fill(broken.begin(), broken.end(), 0U);
        broken[0] = 0xFFU;
        broken[1] = 0xFFU;
        broken[2] = 0xFFU;
        broken[3] = 0xFFU;
        broken[127] = xor_frame(std::span<const std::uint8_t, 128>(broken));
    }

    std::copy(header.begin(), header.end(), frame(63U).begin());
    return true;
}

Sio0::Sio0() noexcept
    : Sio0(Callbacks{nullptr, nullptr}) {}

Sio0::Sio0(Callbacks callbacks) noexcept
    : callbacks_(callbacks) {
    reset();
}

bool Sio0::valid_port(Port port) noexcept {
    return port == Port::One || port == Port::Two;
}

bool Sio0::attach_controller(Port port, PadDevice& device) noexcept {
    if (!valid_port(port)) {
        last_error_ = MmioError::UnsupportedValue;
        return false;
    }
    if ((control_ & kControlDtr) != 0U) {
        last_error_ = MmioError::DeviceDisabled;
        return false;
    }
    slots_[port_index(port)].controller = &device;
    last_error_ = MmioError::None;
    return true;
}

bool Sio0::attach_memory_card(Port port, MemoryCard& device) noexcept {
    if (!valid_port(port)) {
        last_error_ = MmioError::UnsupportedValue;
        return false;
    }
    if ((control_ & kControlDtr) != 0U) {
        last_error_ = MmioError::DeviceDisabled;
        return false;
    }
    if (!device.valid()) {
        last_error_ = MmioError::InvalidImage;
        return false;
    }
    slots_[port_index(port)].memory_card = &device;
    last_error_ = MmioError::None;
    return true;
}

SioDevice* Sio0::device_for(Port port, DeviceKind kind) const noexcept {
    if (!valid_port(port)) {
        return nullptr;
    }
    const Slot& slot = slots_[port_index(port)];
    if (kind == DeviceKind::Controller) {
        return slot.controller;
    }
    if (kind == DeviceKind::MemoryCard) {
        return slot.memory_card;
    }
    return nullptr;
}

SioDevice* Sio0::active_device() const noexcept {
    return device_for(selected_port_, active_kind_);
}

bool Sio0::validate_mmio(std::uint32_t offset,
                         MmioWidth width,
                         MmioWidth expected) noexcept {
    const std::uint32_t width_bytes = static_cast<std::uint32_t>(width);
    if (width_bytes == 0U || (offset % width_bytes) != 0U) {
        last_error_ = MmioError::Misaligned;
        return false;
    }
    if (width != expected) {
        last_error_ = MmioError::UnsupportedWidth;
        return false;
    }
    return true;
}

bool Sio0::advance_to(Cycle cycle) noexcept {
    if (cycle == kNoEventCycle) {
        last_error_ = MmioError::CycleOverflow;
        return false;
    }
    if (cycle < cycle_) {
        last_error_ = MmioError::TimeWentBackwards;
        return false;
    }
    MmioError event_error = MmioError::None;
    while (true) {
        Cycle next = kNoEventCycle;
        if (shift_active_ && shift_complete_ <= cycle) {
            next = std::min(next, shift_complete_);
        }
        if (ack_pending_ && ack_begin_ <= cycle) {
            next = std::min(next, ack_begin_);
        }
        if (dsr_ && ack_until_ <= cycle) {
            next = std::min(next, ack_until_);
        }
        if (next == kNoEventCycle) {
            break;
        }
        advance_baud_timer(next - cycle_);
        cycle_ = next;
        // A pulse ending at the exact byte-completion cycle no longer
        // overlaps an acknowledgement scheduled by that byte.
        if (dsr_ && ack_until_ == next) {
            if (!expire_ack() && event_error == MmioError::None) {
                event_error = last_error_;
            }
        }
        if (shift_active_ && shift_complete_ == next) {
            if (!complete_transfer() && event_error == MmioError::None) {
                event_error = last_error_;
            }
        }
        if (ack_pending_ && ack_begin_ == next) {
            begin_ack();
        }
    }
    advance_baud_timer(cycle - cycle_);
    cycle_ = cycle;
    for (const Slot& slot : slots_) {
        if (slot.controller != nullptr) {
            slot.controller->advance_to(cycle);
        }
    }
    update_irq();
    if (event_error != MmioError::None) {
        last_error_ = event_error;
        return false;
    }
    last_error_ = MmioError::None;
    return true;
}

Cycle Sio0::baud_factor() const noexcept {
    switch (mode_ & kModeFactorMask) {
    case 2U:
        return 16U;
    case 3U:
        return 64U;
    default:
        return 1U;
    }
}

Cycle Sio0::cycles_per_bit() const noexcept {
    Cycle period = static_cast<Cycle>(baud_) * baud_factor();
    period &= ~static_cast<Cycle>(1U);
    return std::max<Cycle>(period, 1U);
}

Cycle Sio0::cycles_per_byte() const noexcept {
    return cycles_per_bit() * 8U;
}

Cycle Sio0::baud_timer_reload() const noexcept {
    return std::max<Cycle>(cycles_per_bit() / 2U, 1U);
}

Cycle Sio0::baud_timer() const noexcept {
    if (!baud_timer_running_) {
        return 0;
    }
    return baud_timer_value_;
}

bool Sio0::device_mode_supported() const noexcept {
    const bool eight_bits = (mode_ & kModeLengthMask) == kModeLengthMask;
    const bool no_parity = (mode_ & kModeParityEnable) == 0U;
    const bool high_when_idle = (mode_ & kModeClockPolarity) == 0U;
    return eight_bits && no_parity && high_when_idle;
}

bool Sio0::event_time_fits(Cycle base, Cycle delay) noexcept {
    return base < kNoEventCycle && delay < (kNoEventCycle - base);
}

bool Sio0::queued_transfer_window_fits() const noexcept {
    if (!device_mode_supported() || tx_count_ == 0U) {
        return true;
    }

    Cycle start = cycle_;
    std::size_t entry_index = tx_head_;
    if (shift_active_) {
        if (tx_count_ < kTxCapacity) {
            return true;
        }
        start = shift_complete_;
        entry_index = (static_cast<std::size_t>(tx_head_) + 1U) % kTxCapacity;
    }
    const bool enabled = tx_fifo_[entry_index].tx_enabled_when_written ||
                         (control_ & kControlTxEnable) != 0U;
    if (!enabled) {
        return true;
    }
    return event_time_fits(start, cycles_per_byte() + kMaximumAckLeadCycles);
}

void Sio0::advance_baud_timer(Cycle elapsed) noexcept {
    if (!baud_timer_running_ || elapsed == 0U) {
        return;
    }
    if (elapsed < baud_timer_value_) {
        baud_timer_value_ -= elapsed;
        return;
    }

    elapsed -= baud_timer_value_;
    const Cycle reload = baud_timer_reload();
    const Cycle phase = elapsed % reload;
    baud_timer_value_ = phase == 0U ? reload : reload - phase;
}

Cycle Sio0::next_event_cycle() const noexcept {
    Cycle next = kNoEventCycle;
    if (shift_active_) {
        next = std::min(next, shift_complete_);
    }
    if (ack_pending_) {
        next = std::min(next, ack_begin_);
    }
    if (dsr_) {
        next = std::min(next, ack_until_);
    }
    return next;
}

std::uint8_t Sio0::rx_threshold() const noexcept {
    const std::uint8_t mode = static_cast<std::uint8_t>((control_ >> 8U) & 0x03U);
    return static_cast<std::uint8_t>(1U << mode);
}

bool Sio0::rx_condition() const noexcept {
    return rx_count_ >= rx_threshold();
}

bool Sio0::tx_condition() const noexcept {
    return shift_active_ ? tx_count_ < kTxCapacity : tx_count_ == 0U;
}

bool Sio0::dsr_condition() const noexcept {
    return dsr_;
}

void Sio0::set_irq(bool enabled) noexcept {
    if (irq_ == enabled) {
        return;
    }
    irq_ = enabled;
    if (callbacks_.irq != nullptr) {
        callbacks_.irq(callbacks_.context, enabled);
    }
}

void Sio0::update_irq() noexcept {
    const bool condition = ((control_ & kControlTxInterruptEnable) != 0U && tx_condition()) ||
                           ((control_ & kControlRxInterruptEnable) != 0U && rx_condition()) ||
                           ((control_ & kControlDsrInterruptEnable) != 0U && dsr_condition());
    if (condition) {
        set_irq(true);
    }
}

std::uint32_t Sio0::status_value() const noexcept {
    std::uint32_t value = 0U;
    if (tx_condition()) {
        value |= kStatusTxReady;
    }
    if (rx_count_ != 0U) {
        value |= kStatusRxReady;
    }
    if (!shift_active_ && tx_count_ == 0U) {
        value |= kStatusTxIdle;
    }
    if (parity_error_) {
        value |= kStatusParityError;
    }
    // SIO0 exposes zero on the SIO1-only overrun, stop, input and CTS fields.
    if (dsr_) {
        value |= kStatusDsr;
    }
    if (irq_) {
        value |= kStatusInterrupt;
    }
    const std::uint32_t timer = static_cast<std::uint32_t>(
        std::min<Cycle>(baud_timer(), 0x1FFFFFU));
    value |= timer << 11U;
    return value;
}

std::uint32_t Sio0::status() const noexcept {
    return status_value();
}

std::uint16_t Sio0::mode() const noexcept {
    return mode_;
}

std::uint16_t Sio0::control() const noexcept {
    return control_;
}

std::uint16_t Sio0::baud() const noexcept {
    return baud_;
}

Cycle Sio0::cycle() const noexcept {
    return cycle_;
}

bool Sio0::irq_pending() const noexcept {
    return irq_;
}

bool Sio0::dsr_active() const noexcept {
    return dsr_;
}

bool Sio0::ack_collision() const noexcept {
    return ack_collision_;
}

bool Sio0::rx_overrun() const noexcept {
    return rx_overrun_;
}

MmioError Sio0::last_error() const noexcept {
    return last_error_;
}

bool Sio0::receive_allowed() const noexcept {
    return (control_ & kControlRxEnable) != 0U || (control_ & kControlDtr) != 0U;
}

void Sio0::enqueue_rx(std::uint8_t value) noexcept {
    if (!receive_allowed()) {
        return;
    }
    if (rx_count_ == kRxCapacity) {
        rx_overrun_ = true;
        const std::size_t last = (static_cast<std::size_t>(rx_head_) + kRxCapacity - 1U) % kRxCapacity;
        rx_fifo_[last] = value;
        return;
    }
    // SIO mirrors the newest byte into every physically unused RX slot. This
    // makes the documented 16/32-bit preview behavior deterministic even when
    // fewer than four logical entries are present.
    for (std::size_t index = rx_count_; index < kRxCapacity; ++index) {
        const std::size_t slot = (static_cast<std::size_t>(rx_head_) + index) % kRxCapacity;
        rx_fifo_[slot] = value;
    }
    ++rx_count_;
}

void Sio0::enqueue_tx(std::uint8_t value, bool tx_enabled) noexcept {
    SioSnapshot::TxEntry entry{value, tx_enabled};
    if (!shift_active_ && tx_count_ != 0U) {
        tx_fifo_[tx_head_] = entry;
        return;
    }
    if (tx_count_ == kTxCapacity) {
        const std::size_t waiting = (static_cast<std::size_t>(tx_head_) + 1U) % kTxCapacity;
        tx_fifo_[waiting] = entry;
        return;
    }
    const std::size_t tail = (static_cast<std::size_t>(tx_head_) + tx_count_) % kTxCapacity;
    tx_fifo_[tail] = entry;
    ++tx_count_;
}

bool Sio0::tx_can_start() const noexcept {
    if (tx_count_ == 0U || !device_mode_supported()) {
        return false;
    }
    return tx_fifo_[tx_head_].tx_enabled_when_written || (control_ & kControlTxEnable) != 0U;
}

bool Sio0::try_start_transfer() noexcept {
    // /ACK is the peripheral's request to clock another byte. The hardware
    // does not begin the next synchronous byte while the request is pending
    // or while the active-low pulse is asserted.
    if (shift_active_ || ack_pending_ || dsr_ || !tx_can_start()) {
        return true;
    }
    if (!queued_transfer_window_fits()) {
        last_error_ = MmioError::CycleOverflow;
        return false;
    }
    shift_active_ = true;
    shift_bit_period_ = cycles_per_bit();
    shift_complete_ = cycle_ + (shift_bit_period_ * 8U);
    return true;
}

bool Sio0::schedule_ack(const ByteTransfer& transfer) noexcept {
    if (!transfer.acknowledged) {
        return true;
    }
    if (ack_pending_ || dsr_) {
        ack_collision_ = true;
        return true;
    }
    const Cycle delay = transfer.acknowledge_delay == 0U
                            ? kDefaultAckDelayCycles
                            : transfer.acknowledge_delay;
    const Cycle width = transfer.acknowledge_width == 0U
                            ? kDefaultAckPulseCycles
                            : transfer.acknowledge_width;
    if (!event_time_fits(cycle_, delay)) {
        ack_begin_ = 0;
        ack_until_ = 0;
        ack_delay_ = 0;
        last_error_ = MmioError::CycleOverflow;
        return false;
    }
    ack_begin_ = cycle_ + delay;
    if (!event_time_fits(ack_begin_, std::max<Cycle>(width, 1U))) {
        ack_begin_ = 0;
        ack_until_ = 0;
        ack_delay_ = 0;
        last_error_ = MmioError::CycleOverflow;
        return false;
    }
    ack_until_ = ack_begin_ + std::max<Cycle>(width, 1U);
    ack_delay_ = delay;
    ack_pending_ = true;
    return true;
}

void Sio0::begin_ack() noexcept {
    ack_pending_ = false;
    dsr_ = true;
    update_irq();
}

bool Sio0::expire_ack() noexcept {
    dsr_ = false;
    ack_begin_ = 0;
    ack_until_ = 0;
    ack_delay_ = 0;
    const bool next_started = try_start_transfer();
    update_irq();
    return next_started;
}

bool Sio0::complete_transfer() noexcept {
    if (!shift_active_ || tx_count_ == 0U) {
        shift_active_ = false;
        last_error_ = MmioError::SnapshotInvalid;
        return false;
    }
    const std::uint8_t transmitted = tx_fifo_[tx_head_].value;
    ByteTransfer transfer{};
    if (addressing_) {
        const Slot& slot = slots_[port_index(selected_port_)];
        std::uint8_t acknowledgements = 0;
        ByteTransfer controller_transfer{};
        ByteTransfer card_transfer{};
        if (slot.controller != nullptr) {
            controller_transfer = slot.controller->exchange(transmitted, cycle_);
            if (controller_transfer.acknowledged) {
                ++acknowledgements;
            }
        }
        if (slot.memory_card != nullptr) {
            card_transfer = slot.memory_card->exchange(transmitted, cycle_);
            if (card_transfer.acknowledged) {
                ++acknowledgements;
            }
        }
        if (acknowledgements == 1U) {
            if (controller_transfer.acknowledged) {
                active_kind_ = DeviceKind::Controller;
                transfer = controller_transfer;
            } else {
                active_kind_ = DeviceKind::MemoryCard;
                transfer = card_transfer;
            }
            addressing_ = false;
        } else {
            if (acknowledgements > 1U) {
                device_conflict_ = true;
            }
            // Only the first byte after /CS is an address. A missing or
            // conflicting response leaves the selected transaction inert
            // until /CS is toggled; later bytes cannot retry addressing.
            addressing_ = false;
            active_kind_ = DeviceKind::None;
            transfer = ByteTransfer{};
        }
    } else if (SioDevice* device = active_device(); device != nullptr) {
        transfer = device->exchange(transmitted, cycle_);
    } else {
        transfer = ByteTransfer{};
    }

    enqueue_rx(transfer.value);
    if (forced_receive_) {
        forced_receive_ = false;
        control_ = static_cast<std::uint16_t>(control_ & static_cast<std::uint16_t>(~kControlRxEnable));
    }

    const bool ack_scheduled = schedule_ack(transfer);

    tx_head_ = static_cast<std::uint8_t>((static_cast<std::size_t>(tx_head_) + 1U) % kTxCapacity);
    --tx_count_;
    shift_active_ = false;
    shift_complete_ = cycle_;
    shift_bit_period_ = 0;
    const bool next_started = try_start_transfer();
    update_irq();
    return ack_scheduled && next_started;
}

void Sio0::begin_bus_select(Cycle cycle) noexcept {
    addressing_ = true;
    active_kind_ = DeviceKind::None;
    device_conflict_ = false;
    const Slot& slot = slots_[port_index(selected_port_)];
    if (slot.controller != nullptr) {
        slot.controller->on_select(cycle);
    }
    if (slot.memory_card != nullptr) {
        slot.memory_card->on_select(cycle);
    }
}

void Sio0::end_bus_select(Cycle cycle) noexcept {
    const Slot& slot = slots_[port_index(selected_port_)];
    if (slot.controller != nullptr) {
        slot.controller->on_deselect(cycle);
    }
    if (slot.memory_card != nullptr) {
        slot.memory_card->on_deselect(cycle);
    }
    addressing_ = true;
    active_kind_ = DeviceKind::None;
    device_conflict_ = false;
    ack_pending_ = false;
    dsr_ = false;
    ack_begin_ = 0;
    ack_until_ = 0;
    ack_delay_ = 0;
    // A byte is meaningful only within the /CS transaction in which it was
    // queued.  Dropping /CS aborts both the shifter and holding register so a
    // later selection cannot deliver stale command/address bytes.
    shift_active_ = false;
    shift_complete_ = cycle;
    shift_bit_period_ = 0;
    tx_head_ = 0;
    tx_count_ = 0;
    tx_fifo_ = {};
}

void Sio0::apply_control(std::uint16_t value, Cycle cycle) noexcept {
    const bool was_selected = (control_ & kControlDtr) != 0U;
    const bool was_rx_enabled = (control_ & kControlRxEnable) != 0U;
    const Port old_port = (control_ & kControlPortTwo) != 0U ? Port::Two : Port::One;
    const Port new_port = (value & kControlPortTwo) != 0U ? Port::Two : Port::One;
    const bool target_selected = (value & kControlDtr) != 0U;
    const bool port_changed = old_port != new_port;
    if (was_selected && (!target_selected || port_changed)) {
        end_bus_select(cycle);
    }
    control_ = static_cast<std::uint16_t>(value & static_cast<std::uint16_t>(~(kControlAcknowledge | kControlReset)));
    selected_port_ = new_port;
    if ((control_ & kControlRxEnable) == 0U) {
        forced_receive_ = false;
    } else if (!was_rx_enabled) {
        forced_receive_ = true;
    }
    if (target_selected && (!was_selected || port_changed)) {
        begin_bus_select(cycle);
    }
    (void)try_start_transfer();
    update_irq();
}

MmioWriteResult Sio0::write_control(std::uint16_t value, Cycle cycle) noexcept {
    constexpr std::uint16_t supported = kControlTxEnable | kControlDtr | kControlRxEnable |
                                         kControlAcknowledge | kControlReset |
                                         kControlRxInterruptModeMask | kControlTxInterruptEnable |
                                         kControlRxInterruptEnable | kControlDsrInterruptEnable |
                                         kControlPortTwo;
    if ((value & static_cast<std::uint16_t>(~supported)) != 0U) {
        last_error_ = MmioError::UnsupportedValue;
        return MmioWriteResult{false, false, last_error_};
    }
    const bool acknowledge = (value & kControlAcknowledge) != 0U;
    const bool reset_requested = (value & kControlReset) != 0U;
    const std::uint16_t stored_value = static_cast<std::uint16_t>(
        value & static_cast<std::uint16_t>(~(kControlAcknowledge | kControlReset)));
    if (!reset_requested) {
        const std::uint16_t old_control = control_;
        control_ = stored_value;
        const bool timing_fits = queued_transfer_window_fits();
        control_ = old_control;
        if (!timing_fits) {
            last_error_ = MmioError::CycleOverflow;
            return MmioWriteResult{false, false, last_error_};
        }
    }
    if (reset_requested) {
        reset(cycle);
    }
    apply_control(stored_value, cycle);
    if (acknowledge) {
        parity_error_ = false;
        rx_overrun_ = false;
        bad_stop_ = false;
        set_irq(false);
        update_irq();
    }
    return MmioWriteResult{true, false, MmioError::None};
}

MmioWriteResult Sio0::write_data(std::uint8_t value, Cycle cycle) noexcept {
    if (!advance_to(cycle)) {
        return MmioWriteResult{false, false, last_error_};
    }
    if (!device_mode_supported()) {
        last_error_ = MmioError::UnsupportedMode;
        return MmioWriteResult{false, false, last_error_};
    }
    const bool overwritten = shift_active_ ? tx_count_ == kTxCapacity : tx_count_ != 0U;
    const auto old_fifo = tx_fifo_;
    const std::uint8_t old_count = tx_count_;
    const std::uint8_t old_head = tx_head_;
    enqueue_tx(value, (control_ & kControlTxEnable) != 0U);
    if (!queued_transfer_window_fits() || !try_start_transfer()) {
        tx_fifo_ = old_fifo;
        tx_count_ = old_count;
        tx_head_ = old_head;
        last_error_ = MmioError::CycleOverflow;
        return MmioWriteResult{false, false, last_error_};
    }
    update_irq();
    return MmioWriteResult{true, overwritten, MmioError::None};
}

MmioReadResult Sio0::read(std::uint32_t offset,
                          MmioWidth width,
                          Cycle cycle) noexcept {
    if (!advance_to(cycle)) {
        return MmioReadResult{false, 0U, last_error_};
    }
    last_error_ = MmioError::None;
    const std::uint32_t width_bytes = static_cast<std::uint32_t>(width);
    if (width_bytes == 0U || (width_bytes != 1U && width_bytes != 2U && width_bytes != 4U)) {
        last_error_ = MmioError::UnsupportedWidth;
        return MmioReadResult{false, 0U, last_error_};
    }
    if ((offset % width_bytes) != 0U) {
        last_error_ = MmioError::Misaligned;
        return MmioReadResult{false, 0U, last_error_};
    }
    if (offset == kDataOffset) {
        if (rx_count_ == 0U) {
            last_error_ = MmioError::ReceiveEmpty;
            return MmioReadResult{false, 0U, last_error_};
        }
        const std::size_t preview_count = static_cast<std::size_t>(width);
        std::uint32_t value = 0U;
        for (std::size_t index = 0; index < preview_count; ++index) {
            const std::size_t slot = (static_cast<std::size_t>(rx_head_) + index) % kRxCapacity;
            value |= static_cast<std::uint32_t>(rx_fifo_[slot]) << (index * 8U);
        }
        const std::uint8_t removed = width == MmioWidth::Word
                                         ? 4U
                                         : 1U;
        rx_head_ = static_cast<std::uint8_t>(
            (static_cast<std::size_t>(rx_head_) + removed) % kRxCapacity);
        rx_count_ = rx_count_ > removed
                        ? static_cast<std::uint8_t>(rx_count_ - removed)
                        : 0U;
        update_irq();
        return MmioReadResult{true, value, MmioError::None};
    }
    if (offset == kStatusOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Word)) {
            return MmioReadResult{false, 0U, last_error_};
        }
        return MmioReadResult{true, status_value(), MmioError::None};
    }
    if (offset == kModeOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Halfword)) {
            return MmioReadResult{false, 0U, last_error_};
        }
        return MmioReadResult{true, mode_, MmioError::None};
    }
    if (offset == kControlOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Halfword)) {
            return MmioReadResult{false, 0U, last_error_};
        }
        return MmioReadResult{true, control_, MmioError::None};
    }
    if (offset == kBaudOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Halfword)) {
            return MmioReadResult{false, 0U, last_error_};
        }
        return MmioReadResult{true, baud_, MmioError::None};
    }
    last_error_ = MmioError::UnknownRegister;
    return MmioReadResult{false, 0U, last_error_};
}

MmioWriteResult Sio0::write(std::uint32_t offset,
                             MmioWidth width,
                             std::uint32_t value,
                             Cycle cycle) noexcept {
    if (!advance_to(cycle)) {
        return MmioWriteResult{false, false, last_error_};
    }
    last_error_ = MmioError::None;
    const std::uint32_t width_bytes = static_cast<std::uint32_t>(width);
    if (width_bytes == 0U || (width_bytes != 1U && width_bytes != 2U && width_bytes != 4U)) {
        last_error_ = MmioError::UnsupportedWidth;
        return MmioWriteResult{false, false, last_error_};
    }
    if ((offset % width_bytes) != 0U) {
        last_error_ = MmioError::Misaligned;
        return MmioWriteResult{false, false, last_error_};
    }
    if (offset == kDataOffset) {
        return write_data(static_cast<std::uint8_t>(value & 0xFFU), cycle);
    }
    if (offset == kStatusOffset) {
        last_error_ = MmioError::UnsupportedWidth;
        return MmioWriteResult{false, false, last_error_};
    }
    if (offset == kModeOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Halfword)) {
            return MmioWriteResult{false, false, last_error_};
        }
        const std::uint16_t old_mode = mode_;
        mode_ = static_cast<std::uint16_t>(value & kSio0ModeWritableMask);
        if (!queued_transfer_window_fits() || !try_start_transfer()) {
            mode_ = old_mode;
            last_error_ = MmioError::CycleOverflow;
            return MmioWriteResult{false, false, last_error_};
        }
        update_irq();
        return MmioWriteResult{true, false, MmioError::None};
    }
    if (offset == kControlOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Halfword)) {
            return MmioWriteResult{false, false, last_error_};
        }
        return write_control(static_cast<std::uint16_t>(value & 0xFFFFU), cycle);
    }
    if (offset == kBaudOffset) {
        if (!validate_mmio(offset, width, MmioWidth::Halfword)) {
            return MmioWriteResult{false, false, last_error_};
        }
        const std::uint16_t old_baud = baud_;
        const Cycle old_timer_value = baud_timer_value_;
        const bool old_timer_running = baud_timer_running_;
        baud_ = static_cast<std::uint16_t>(value & 0xFFFFU);
        baud_timer_value_ = baud_timer_reload();
        baud_timer_running_ = true;
        if (!queued_transfer_window_fits() || !try_start_transfer()) {
            baud_ = old_baud;
            baud_timer_value_ = old_timer_value;
            baud_timer_running_ = old_timer_running;
            last_error_ = MmioError::CycleOverflow;
            return MmioWriteResult{false, false, last_error_};
        }
        update_irq();
        return MmioWriteResult{true, false, MmioError::None};
    }
    last_error_ = MmioError::UnknownRegister;
    return MmioWriteResult{false, false, last_error_};
}

MmioWriteResult Sio0::select(Port port, Cycle cycle) noexcept {
    if (!valid_port(port)) {
        last_error_ = MmioError::UnsupportedValue;
        return MmioWriteResult{false, false, last_error_};
    }
    if (!advance_to(cycle)) {
        return MmioWriteResult{false, false, last_error_};
    }
    std::uint16_t value = static_cast<std::uint16_t>(control_ | kControlDtr);
    if (port == Port::Two) {
        value = static_cast<std::uint16_t>(value | kControlPortTwo);
    } else {
        value = static_cast<std::uint16_t>(value & static_cast<std::uint16_t>(~kControlPortTwo));
    }
    return write_control(value, cycle);
}

MmioWriteResult Sio0::deselect(Cycle cycle) noexcept {
    if (!advance_to(cycle)) {
        return MmioWriteResult{false, false, last_error_};
    }
    return write_control(static_cast<std::uint16_t>(control_ & static_cast<std::uint16_t>(~kControlDtr)), cycle);
}

void Sio0::reset(Cycle cycle) noexcept {
    if ((control_ & kControlDtr) != 0U) {
        end_bus_select(cycle);
    }
    cycle_ = cycle;
    shift_complete_ = cycle;
    shift_bit_period_ = 0;
    baud_timer_value_ = 0;
    ack_begin_ = 0;
    ack_until_ = 0;
    ack_delay_ = 0;
    mode_ = 0;
    control_ = 0;
    baud_ = 0;
    tx_head_ = 0;
    tx_count_ = 0;
    rx_head_ = 0;
    rx_count_ = 0;
    shift_active_ = false;
    baud_timer_running_ = false;
    ack_pending_ = false;
    dsr_ = false;
    parity_error_ = false;
    rx_overrun_ = false;
    bad_stop_ = false;
    forced_receive_ = false;
    addressing_ = true;
    device_conflict_ = false;
    ack_collision_ = false;
    selected_port_ = Port::One;
    active_kind_ = DeviceKind::None;
    tx_fifo_ = {};
    rx_fifo_ = {};
    last_error_ = MmioError::None;
    set_irq(false);
}

SioSnapshot Sio0::snapshot() const noexcept {
    SioSnapshot value{};
    value.cycle = cycle_;
    value.shift_complete = shift_complete_;
    value.shift_bit_period = shift_bit_period_;
    value.baud_timer_value = baud_timer_value_;
    value.ack_begin = ack_begin_;
    value.ack_until = ack_until_;
    value.ack_delay = ack_delay_;
    value.mode = mode_;
    value.control = control_;
    value.baud = baud_;
    value.tx_head = tx_head_;
    value.tx_count = tx_count_;
    value.rx_head = rx_head_;
    value.rx_count = rx_count_;
    value.shift_active = shift_active_;
    value.baud_timer_running = baud_timer_running_;
    value.ack_pending = ack_pending_;
    value.dsr = dsr_;
    value.irq = irq_;
    value.parity_error = parity_error_;
    value.rx_overrun = rx_overrun_;
    value.bad_stop = bad_stop_;
    value.forced_receive = forced_receive_;
    value.addressing = addressing_;
    value.device_conflict = device_conflict_;
    value.ack_collision = ack_collision_;
    value.selected_port = selected_port_;
    value.active_device = active_kind_;
    value.tx_fifo = tx_fifo_;
    value.rx_fifo = rx_fifo_;
    return value;
}

bool Sio0::restore(const SioSnapshot& snapshot_value) noexcept {
    constexpr std::uint16_t supported_control =
        kControlTxEnable | kControlDtr | kControlRxEnable | kControlRxInterruptModeMask |
        kControlTxInterruptEnable | kControlRxInterruptEnable | kControlDsrInterruptEnable |
        kControlPortTwo;
    const bool valid_device_kind = snapshot_value.active_device == DeviceKind::None ||
                                   snapshot_value.active_device == DeviceKind::Controller ||
                                   snapshot_value.active_device == DeviceKind::MemoryCard;
    const Port control_port = (snapshot_value.control & kControlPortTwo) != 0U
                                  ? Port::Two
                                  : Port::One;
    const bool target_selected = (snapshot_value.control & kControlDtr) != 0U;
    constexpr Cycle maximum_bit_period =
        static_cast<Cycle>(std::numeric_limits<std::uint16_t>::max()) * 64U;
    const auto snapshot_factor = [&snapshot_value]() noexcept -> Cycle {
        switch (snapshot_value.mode & kModeFactorMask) {
        case 2U:
            return 16U;
        case 3U:
            return 64U;
        default:
            return 1U;
        }
    };
    Cycle snapshot_bit_period = static_cast<Cycle>(snapshot_value.baud) * snapshot_factor();
    snapshot_bit_period &= ~static_cast<Cycle>(1U);
    snapshot_bit_period = std::max<Cycle>(snapshot_bit_period, 1U);
    const bool snapshot_mode_supported =
        (snapshot_value.mode & kModeLengthMask) == kModeLengthMask &&
        (snapshot_value.mode & kModeParityEnable) == 0U &&
        (snapshot_value.mode & kModeClockPolarity) == 0U;
    const Cycle snapshot_baud_reload = std::max<Cycle>(snapshot_bit_period / 2U, 1U);
    // MODE/BAUD writes affect the next byte.  An already-running byte keeps
    // the period latched when it started, so its historical period need not
    // equal the period derived from the current registers.
    const bool valid_shift = snapshot_value.shift_active
                                 ? snapshot_value.tx_count != 0U &&
                                        snapshot_mode_supported &&
                                        snapshot_value.shift_bit_period != 0U &&
                                        snapshot_value.shift_bit_period <= maximum_bit_period &&
                                        snapshot_value.shift_complete > snapshot_value.cycle &&
                                        snapshot_value.shift_complete - snapshot_value.cycle <=
                                            snapshot_value.shift_bit_period * 8U &&
                                        event_time_fits(snapshot_value.shift_complete,
                                                        kMaximumAckLeadCycles)
                                 : snapshot_value.shift_bit_period == 0U &&
                                        snapshot_value.shift_complete <= snapshot_value.cycle;
    const bool valid_baud_timer = snapshot_value.baud_timer_running
                                      ? snapshot_value.baud_timer_value != 0U &&
                                            snapshot_value.baud_timer_value <= snapshot_baud_reload
                                      : snapshot_value.baud_timer_value == 0U;
    const bool active_can_ack = snapshot_value.active_device != DeviceKind::None &&
                                !snapshot_value.addressing;
    const Cycle maximum_ack_delay = snapshot_value.active_device == DeviceKind::MemoryCard
                                        ? kMemoryCardAckDelayCycles +
                                              kMemoryCardReadLateAckExtraCycles
                                        : kControllerAckDelayCycles;
    const bool valid_ack_window =
        snapshot_value.ack_until > snapshot_value.ack_begin &&
        snapshot_value.ack_until - snapshot_value.ack_begin == kAckPulseCycles &&
        snapshot_value.ack_until < kNoEventCycle;
    const bool valid_ack_delay =
        (snapshot_value.active_device == DeviceKind::Controller &&
         snapshot_value.ack_delay == kControllerAckDelayCycles) ||
        (snapshot_value.active_device == DeviceKind::MemoryCard &&
         (snapshot_value.ack_delay == kMemoryCardAckDelayCycles ||
          snapshot_value.ack_delay ==
              kMemoryCardAckDelayCycles + kMemoryCardReadLateAckExtraCycles));
    const bool valid_ack = snapshot_value.ack_pending
                                      ? !snapshot_value.dsr && target_selected && active_can_ack &&
                                      valid_ack_delay &&
                                       snapshot_value.ack_begin > snapshot_value.cycle &&
                                       snapshot_value.ack_begin - snapshot_value.cycle ==
                                           snapshot_value.ack_delay &&
                                       snapshot_value.ack_delay <= maximum_ack_delay &&
                                      valid_ack_window
                               : snapshot_value.dsr
                                      ? target_selected && active_can_ack &&
                                            valid_ack_delay &&
                                            snapshot_value.ack_begin <= snapshot_value.cycle &&
                                            snapshot_value.ack_until > snapshot_value.cycle &&
                                            valid_ack_window
                                      : snapshot_value.ack_begin == 0U &&
                                            snapshot_value.ack_until == 0U &&
                                            snapshot_value.ack_delay == 0U;
    const bool valid_selection = target_selected
                                      ? (snapshot_value.addressing
                                             ? snapshot_value.active_device == DeviceKind::None
                                             : true)
                                      : snapshot_value.addressing &&
                                            snapshot_value.active_device == DeviceKind::None &&
                                            !snapshot_value.ack_pending && !snapshot_value.dsr;
    if (snapshot_value.cycle == kNoEventCycle || snapshot_value.tx_head >= kTxCapacity ||
        snapshot_value.tx_count > kTxCapacity ||
        snapshot_value.rx_head >= kRxCapacity || snapshot_value.rx_count > kRxCapacity ||
         !valid_port(snapshot_value.selected_port) || !valid_device_kind ||
         snapshot_value.selected_port != control_port ||
         (snapshot_value.control & static_cast<std::uint16_t>(~supported_control)) != 0U ||
         (snapshot_value.mode & static_cast<std::uint16_t>(~kSio0ModeWritableMask)) != 0U ||
         !valid_shift || !valid_baud_timer || !valid_ack ||
         !valid_selection ||
         (snapshot_value.shift_active &&
          (snapshot_value.ack_pending || snapshot_value.dsr)) ||
         ((snapshot_value.ack_pending || snapshot_value.dsr) &&
          snapshot_value.tx_count > 1U) ||
         (!snapshot_value.shift_active && snapshot_value.tx_count > 1U) ||
         snapshot_value.device_conflict || snapshot_value.ack_collision ||
         snapshot_value.parity_error || snapshot_value.bad_stop ||
         (snapshot_value.forced_receive &&
         (snapshot_value.control & kControlRxEnable) == 0U)) {
        last_error_ = MmioError::SnapshotInvalid;
        return false;
    }
    if (target_selected &&
        device_for(snapshot_value.selected_port, snapshot_value.active_device) == nullptr &&
        snapshot_value.active_device != DeviceKind::None) {
        last_error_ = MmioError::NoDevice;
        return false;
    }

    bool queued_window_valid = true;
    if (snapshot_mode_supported && snapshot_value.tx_count != 0U) {
        Cycle start = snapshot_value.cycle;
        std::size_t entry_index = snapshot_value.tx_head;
        if (snapshot_value.shift_active) {
            if (snapshot_value.tx_count == kTxCapacity) {
                start = snapshot_value.shift_complete;
                entry_index = (static_cast<std::size_t>(snapshot_value.tx_head) + 1U) %
                              kTxCapacity;
            } else {
                entry_index = kTxCapacity;
            }
        }
        if (entry_index < kTxCapacity) {
            const bool enabled = snapshot_value.tx_fifo[entry_index].tx_enabled_when_written ||
                                 (snapshot_value.control & kControlTxEnable) != 0U;
            if (enabled) {
                queued_window_valid = snapshot_value.shift_active &&
                                      event_time_fits(
                                          start,
                                          (snapshot_bit_period * 8U) +
                                              kMaximumAckLeadCycles);
            }
        }
    }
    if (!queued_window_valid) {
        last_error_ = MmioError::SnapshotInvalid;
        return false;
    }

    const std::uint8_t threshold = static_cast<std::uint8_t>(
        1U << ((snapshot_value.control >> 8U) & 0x03U));
    const bool snapshot_tx_ready = snapshot_value.shift_active
                                       ? snapshot_value.tx_count < kTxCapacity
                                       : snapshot_value.tx_count == 0U;
    const bool enabled_irq_condition =
        ((snapshot_value.control & kControlTxInterruptEnable) != 0U && snapshot_tx_ready) ||
        ((snapshot_value.control & kControlRxInterruptEnable) != 0U &&
         snapshot_value.rx_count >= threshold) ||
        ((snapshot_value.control & kControlDsrInterruptEnable) != 0U && snapshot_value.dsr);
    if (enabled_irq_condition && !snapshot_value.irq) {
        last_error_ = MmioError::SnapshotInvalid;
        return false;
    }

    const bool was_selected = (control_ & kControlDtr) != 0U;
    const bool same_bus = was_selected && target_selected &&
                          (control_ & kControlPortTwo) == (snapshot_value.control & kControlPortTwo);
    const bool old_irq = irq_;
    if (was_selected && !same_bus) {
        end_bus_select(cycle_);
    }
    cycle_ = snapshot_value.cycle;
    shift_complete_ = snapshot_value.shift_complete;
    shift_bit_period_ = snapshot_value.shift_bit_period;
    baud_timer_value_ = snapshot_value.baud_timer_value;
    ack_begin_ = snapshot_value.ack_begin;
    ack_until_ = snapshot_value.ack_until;
    ack_delay_ = snapshot_value.ack_delay;
    mode_ = snapshot_value.mode;
    control_ = snapshot_value.control;
    baud_ = snapshot_value.baud;
    tx_head_ = snapshot_value.tx_head;
    tx_count_ = snapshot_value.tx_count;
    rx_head_ = snapshot_value.rx_head;
    rx_count_ = snapshot_value.rx_count;
    shift_active_ = snapshot_value.shift_active;
    baud_timer_running_ = snapshot_value.baud_timer_running;
    ack_pending_ = snapshot_value.ack_pending;
    dsr_ = snapshot_value.dsr;
    irq_ = snapshot_value.irq;
    parity_error_ = snapshot_value.parity_error;
    rx_overrun_ = snapshot_value.rx_overrun;
    bad_stop_ = snapshot_value.bad_stop;
    forced_receive_ = snapshot_value.forced_receive;
    addressing_ = snapshot_value.addressing;
    device_conflict_ = snapshot_value.device_conflict;
    ack_collision_ = snapshot_value.ack_collision;
    selected_port_ = snapshot_value.selected_port;
    active_kind_ = snapshot_value.active_device;
    tx_fifo_ = snapshot_value.tx_fifo;
    rx_fifo_ = snapshot_value.rx_fifo;

    if ((control_ & kControlDtr) != 0U && !same_bus) {
        begin_bus_select(cycle_);
        addressing_ = snapshot_value.addressing;
        active_kind_ = snapshot_value.active_device;
        device_conflict_ = snapshot_value.device_conflict;
    }
    if (old_irq != irq_ && callbacks_.irq != nullptr) {
        callbacks_.irq(callbacks_.context, irq_);
    }
    last_error_ = MmioError::None;
    return true;
}

} // namespace clean_aot::sio
