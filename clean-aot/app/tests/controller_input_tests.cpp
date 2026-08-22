#include "clean_aot/app/controller_input.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main() {
    using namespace clean_aot::app;
    std::array<bool, kMappedGamepadButtonCount> buttons{};
    buttons[0] = true;
    buttons[3] = true;
    buttons[10] = true;
    require(map_gamepad_buttons(buttons) == 0x5010U,
            "SDL face/direction buttons must map to PS masks");
    buttons.fill(true);
    require((map_gamepad_buttons(buttons) & 0xffffU) == 0xfcffU,
            "all supported controls must remain finite PS button bits");

    require(apply_radial_deadzone(3000, -3000, 5000) == std::pair<std::int16_t, std::int16_t>{0, 0},
            "diagonal drift inside radial deadzone must be neutral");
    const auto horizontal = apply_radial_deadzone(10000, 0, 5000);
    require(horizontal.first > 5000 && horizontal.first < 6000 && horizontal.second == 0,
            "deadzone must rescale the usable steering range");
    const auto diagonal = apply_radial_deadzone(32767, 32767, 5000);
    require(diagonal.first == diagonal.second && diagonal.first > 23000,
            "diagonal steering must preserve direction and clamp magnitude");
    require(apply_radial_deadzone(-32768, 0, 0).first == -32767,
            "negative full scale must avoid asymmetric overflow");
    require(apply_radial_deadzone(123, 456, 32767) == std::pair<std::int16_t, std::int16_t>{0, 0},
            "maximum deadzone must be well-defined");
    std::cout << "clean_aot_controller_input_tests: all checks passed\n";
}
