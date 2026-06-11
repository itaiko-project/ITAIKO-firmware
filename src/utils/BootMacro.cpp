#include "utils/BootMacro.h"

#include "utils/InputState.h"

#include "pico/time.h"

#include <variant>

namespace Doncon::Utils::BootMacro {

bool check(const Peripherals::Controller::Config &controller_config, unsigned hold_ms) {
    if (!std::holds_alternative<Peripherals::Controller::Config::InternalGpio>(controller_config.gpio_config)) {
        return false;
    }

    Peripherals::Controller controller(controller_config);

    const uint32_t start = to_ms_since_boot(get_absolute_time());
    bool held_whole_time = false;

    while ((to_ms_since_boot(get_absolute_time()) - start) < hold_ms) {
        Utils::InputState s;
        controller.updateInputState(s);

        if (s.controller.buttons.l && s.controller.buttons.r) {
            held_whole_time = true;
        } else {
            return false; // must be held continuously for the whole window
        }
        sleep_ms(10);
    }

    return held_whole_time;
}

} // namespace Doncon::Utils::BootMacro
