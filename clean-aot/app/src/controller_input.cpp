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

} // namespace clean_aot::app
