#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace clean_aot::app {

enum class PsButton : std::uint16_t {
    Select = 0x0001U, L3 = 0x0002U, R3 = 0x0004U, Start = 0x0008U,
    Up = 0x0010U, Right = 0x0020U,
    Down = 0x0040U, Left = 0x0080U, L2 = 0x0100U, R2 = 0x0200U,
    L1 = 0x0400U, R1 = 0x0800U, Triangle = 0x1000U, Circle = 0x2000U,
    Cross = 0x4000U, Square = 0x8000U,
};

inline constexpr std::size_t kMappedGamepadButtonCount = 14U;

[[nodiscard]] std::uint32_t map_gamepad_buttons(
    const std::array<bool, kMappedGamepadButtonCount>& pressed) noexcept;

[[nodiscard]] std::pair<std::int16_t, std::int16_t> apply_radial_deadzone(
    std::int16_t x, std::int16_t y, std::int16_t deadzone) noexcept;

} // namespace clean_aot::app
