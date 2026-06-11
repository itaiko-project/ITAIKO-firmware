#include "utils/BootModeSelect.h"

#include "utils/BootLed.h"

#include "pico/time.h"

#include <array>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace Doncon::Utils::BootModeSelect {

namespace {

struct Color {
    uint8_t r, g, b;
};

enum class Held : uint8_t {
    None,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    FaceNorth,
    FaceSouth,
    FaceWest,
    FaceEast,
};

struct ColorStep {
    Color color;
    uint16_t on_ms;
    uint16_t off_ms;
};

enum class PatternKind : uint8_t { Steps, RainbowSweep };

struct Pattern {
    PatternKind kind;
    std::vector<ColorStep> steps;
    uint16_t sweep_ms;
};

struct Mapping {
    Held button;
    usb_mode_t mode;
    Pattern pattern;
};

Pattern blinks(Color color, uint8_t count, uint16_t on_ms = 220, uint16_t off_ms = 180) {
    Pattern p{.kind = PatternKind::Steps, .steps = {}, .sweep_ms = 0};
    for (uint8_t i = 0; i < count; ++i) {
        p.steps.push_back({color, on_ms, off_ms});
    }
    return p;
}

Pattern mixed(std::vector<ColorStep> steps) {
    return Pattern{.kind = PatternKind::Steps, .steps = std::move(steps), .sweep_ms = 0};
}

Pattern rainbow(uint16_t ms = 1100) {
    return Pattern{.kind = PatternKind::RainbowSweep, .steps = {}, .sweep_ms = ms};
}

const std::array<Mapping, 8> &mappings() {
    static const std::array<Mapping, 8> table = {{
        {Held::DPadUp, USB_MODE_SWITCH_TATACON, blinks({255, 0, 0}, 3)},
        {Held::DPadDown, USB_MODE_PS4_TATACON, blinks({0, 0, 255}, 3)},
        {Held::DPadLeft, USB_MODE_KEYBOARD_P1,
         mixed({{{255, 255, 255}, 220, 180}, {{255, 255, 255}, 220, 180}, {{255, 0, 0}, 280, 0}})},
        {Held::DPadRight, USB_MODE_KEYBOARD_P2,
         mixed({{{255, 255, 255}, 220, 180}, {{255, 255, 255}, 220, 180}, {{0, 0, 255}, 280, 0}})},
        {Held::FaceNorth, USB_MODE_DUALSHOCK3, blinks({180, 0, 200}, 3)},
        {Held::FaceSouth, USB_MODE_USIO_TAIKO, blinks({0, 220, 0}, 3)},
        {Held::FaceWest, USB_MODE_MIDI, rainbow(1100)},
        {Held::FaceEast, USB_MODE_XBOX360, blinks({255, 200, 0}, 3)},
    }};
    return table;
}

Held detect_held(const Utils::InputState::Controller &c) {
    if (c.dpad.up) return Held::DPadUp;
    if (c.dpad.down) return Held::DPadDown;
    if (c.dpad.left) return Held::DPadLeft;
    if (c.dpad.right) return Held::DPadRight;
    if (c.buttons.north) return Held::FaceNorth;
    if (c.buttons.south) return Held::FaceSouth;
    if (c.buttons.west) return Held::FaceWest;
    if (c.buttons.east) return Held::FaceEast;
    return Held::None;
}

const Mapping *find_mapping(Held h) {
    for (const auto &m : mappings()) {
        if (m.button == h) return &m;
    }
    return nullptr;
}

void play_pattern(BootLed &led, const Pattern &p) {
    if (p.kind == PatternKind::Steps) {
        for (const auto &step : p.steps) {
            led.fill(step.color.r, step.color.g, step.color.b);
            sleep_ms(step.on_ms);
            led.fill(0, 0, 0);
            if (step.off_ms > 0) sleep_ms(step.off_ms);
        }
    } else {
        const uint16_t frame_ms = 25;
        const uint16_t frames = p.sweep_ms / frame_ms;
        for (uint16_t f = 0; f < frames; ++f) {
            const float phase = 360.0f * static_cast<float>(f) / static_cast<float>(frames);
            led.rainbowFrame(phase);
            sleep_ms(frame_ms);
        }
    }
}

} // namespace

bool run(SettingsStore &settings_store,
         const Peripherals::Controller::Config &controller_config,
         const LedConfig &led_config,
         unsigned hold_ms) {
    if (!std::holds_alternative<Peripherals::Controller::Config::InternalGpio>(controller_config.gpio_config)) {
        return false;
    }

    Peripherals::Controller controller(controller_config);

    const uint32_t start = to_ms_since_boot(get_absolute_time());
    Held candidate = Held::None;
    bool stable = false;

    while ((to_ms_since_boot(get_absolute_time()) - start) < hold_ms) {
        Utils::InputState s;
        controller.updateInputState(s);
        const Held now = detect_held(s.controller);

        if (now == Held::None) {
            candidate = Held::None;
            stable = false;
        } else if (candidate == Held::None) {
            candidate = now;
            stable = true;
        } else if (candidate != now) {
            return false;
        }
        sleep_ms(10);
    }

    if (!stable || candidate == Held::None) {
        return false;
    }

    const Mapping *m = find_mapping(candidate);
    if (m == nullptr) {
        return false;
    }

    if (settings_store.getUsbMode() != m->mode) {
        settings_store.setUsbMode(m->mode);
    }

    {
        BootLed led(led_config);
        play_pattern(led, m->pattern);
    }

    return true;
}

} // namespace Doncon::Utils::BootModeSelect
