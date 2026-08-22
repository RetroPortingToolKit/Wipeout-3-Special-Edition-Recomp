#include "clean_aot/app/controller_input.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace clean_aot::app {

std::uint32_t map_gamepad_buttons(
    const std::array<bool, kMappedGamepadButtonCount>& pressed) noexcept {
    constexpr std::array<PsButton, kMappedGamepadButtonCount> mapping{
        PsButton::Cross, PsButton::Circle, PsButton::Square, PsButton::Triangle,
        PsButton::Select, PsButton::Start, PsButton::L3, PsButton::R3,
        PsButton::L1, PsButton::R1, PsButton::Up, PsButton::Down,
        PsButton::Left, PsButton::Right,
    };
    std::uint32_t result = 0U;
    for (std::size_t index = 0U; index < pressed.size(); ++index) {
        if (pressed[index]) result |= static_cast<std::uint16_t>(mapping[index]);
    }
    return result;
}

std::pair<std::int16_t, std::int16_t> apply_radial_deadzone(
    std::int16_t x, std::int16_t y, std::int16_t deadzone) noexcept {
    const std::int32_t threshold = std::clamp<std::int32_t>(deadzone, 0, 32767);
    if (threshold == 32767) return {0, 0};
    const double dx = static_cast<double>(x);
    const double dy = static_cast<double>(y);
    const double magnitude = std::hypot(dx, dy);
    if (magnitude <= static_cast<double>(threshold)) return {0, 0};
    const double maximum = 32767.0;
    const double clamped = std::min(magnitude, maximum);
    const double scaled = (clamped - static_cast<double>(threshold)) /
                          (maximum - static_cast<double>(threshold));
    const double factor = scaled * maximum / magnitude;
    const auto convert = [factor](double value) {
        return static_cast<std::int16_t>(std::clamp(
            std::lround(value * factor), -32767L, 32767L));
    };
    return {convert(dx), convert(dy)};
}

bool RumbleBridge::update(std::uint64_t sample_index,
                          std::uint64_t device_generation,
                          bool available, bool focused,
                          std::uint8_t small_motor,
                          std::uint8_t large_motor,
                          IRumbleSink& sink) noexcept {
    if (sampled_ && sample_index <= last_sample_) return false;
    sampled_ = true;
    last_sample_ = sample_index;
    const bool usable = available && focused && device_generation != 0U;
    const std::uint16_t desired_low = usable
        ? static_cast<std::uint16_t>(static_cast<std::uint16_t>(large_motor) * 257U) : 0U;
    const std::uint16_t desired_high = usable && small_motor != 0U ? 0xffffU : 0U;
    const bool generation_changed = generation_ != device_generation;
    if (!generation_changed && commanded_ && low_ == desired_low && high_ == desired_high)
        return true;
    generation_ = device_generation;
    low_ = desired_low;
    high_ = desired_high;
    commanded_ = true;
    if (desired_low == 0U && desired_high == 0U && generation_changed) return true;
    // Unsupported haptics are a host capability miss, not a guest timing error.
    (void)sink.set_rumble(desired_low, desired_high, 10U);
    return true;
}

void RumbleBridge::device_will_close(std::uint64_t device_generation,
                                     IRumbleSink& sink) noexcept {
    if (commanded_ && generation_ == device_generation && (low_ != 0U || high_ != 0U))
        (void)sink.set_rumble(0U, 0U, 0U);
    commanded_ = false;
    low_ = 0U;
    high_ = 0U;
}

} // namespace clean_aot::app
