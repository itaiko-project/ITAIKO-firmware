#include "utils/Edition.h"

#include "hardware/gpio.h"
#include "pico/time.h"

namespace Doncon::Utils {

namespace {
// Bridge this pin to GND to flag the board as the ZhongTaiko edition.
constexpr uint EDITION_DETECT_PIN = 25;
} // namespace

const char *getFirmwareEdition() {
    static const char *cached_edition = nullptr;

    if (cached_edition == nullptr) {
        gpio_init(EDITION_DETECT_PIN);
        gpio_set_dir(EDITION_DETECT_PIN, GPIO_IN);
        gpio_pull_up(EDITION_DETECT_PIN);

        // Settle the pull-up before reading.
        busy_wait_us(50);

        // Bridged to GND (low) -> ZhongTaiko edition, otherwise stock iTAIKO.
        cached_edition = gpio_get(EDITION_DETECT_PIN) ? "iTAIKO" : "ZhongTaiko";
    }

    return cached_edition;
}

} // namespace Doncon::Utils
