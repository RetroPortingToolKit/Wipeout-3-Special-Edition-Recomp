#include "clean_aot/app/controller_input.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

class MockRumble final : public clean_aot::app::IRumbleSink {
public:
    bool set_rumble(std::uint16_t low, std::uint16_t high,
                    std::uint32_t duration) noexcept override {
        ++calls; last_low = low; last_high = high; last_duration = duration;
        return supported;
    }
    std::uint32_t calls = 0U;
    std::uint16_t last_low = 0U, last_high = 0U;
    std::uint32_t last_duration = 0U;
    bool supported = true;
};
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

    RumbleBridge rumble;
    MockRumble sink;
    require(rumble.update(0U, 1U, true, true, 1U, 128U, sink), "first rumble sample rejected");
    require(sink.calls == 1U && sink.last_low == 32896U && sink.last_high == 0xffffU &&
            sink.last_duration == 10U, "DualShock motors were not converted exactly");
    for (std::uint64_t sample = 1U; sample < 3600U; ++sample)
        require(rumble.update(sample, 1U, true, true, 1U, 128U, sink), "100 Hz rumble sequence rejected");
    require(sink.calls == 1U, "unchanged motors must not repeat host calls");
    require(!rumble.update(3599U, 1U, true, true, 0U, 0U, sink),
            "duplicate sample must fail closed");
    require(rumble.update(3600U, 1U, true, false, 1U, 128U, sink) && sink.calls == 2U &&
            sink.last_low == 0U && sink.last_high == 0U, "focus loss must stop active motors once");
    require(rumble.update(3601U, 1U, true, false, 1U, 128U, sink) && sink.calls == 2U,
            "unfocused stop must not repeat");
    require(rumble.update(3602U, 2U, true, true, 0U, 255U, sink) && sink.calls == 3U &&
            sink.last_low == 0xffffU, "new device generation must receive current command");
    rumble.device_will_close(2U, sink);
    require(sink.calls == 4U && sink.last_low == 0U, "device removal must stop before close");
    sink.supported = false;
    require(rumble.update(3603U, 3U, true, true, 1U, 1U, sink) &&
            rumble.update(3604U, 3U, true, true, 1U, 1U, sink) && sink.calls == 5U,
            "unsupported haptics must be non-fatal and non-repeating");
    std::cout << "clean_aot_controller_input_tests: all checks passed\n";
}
