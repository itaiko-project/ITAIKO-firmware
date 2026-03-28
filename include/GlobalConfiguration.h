#ifndef GLOBALCONFIGURATION_H_
#define GLOBALCONFIGURATION_H_

#include "peripherals/Controller.h"
#include "peripherals/Display.h"
#include "peripherals/Drum.h"
#include "peripherals/StatusLed.h"
#include "utils/KeyboardMappings.h"

#include "hardware/i2c.h"
#include "hardware/spi.h"

// Include tinyusb HID definitions for keycodes
#include "class/hid/hid.h"

namespace Doncon::Config {

struct I2c {
    uint8_t sda_pin;
    uint8_t scl_pin;
    i2c_inst_t *block;
    uint speed_hz;
};

namespace Default {

const usb_mode_t usb_mode = USB_MODE_KEYBOARD_P1;

const Utils::DrumKeys drum_keys_p1 = {
    .ka_left = HID_KEY_D,
    .don_left = HID_KEY_F,
    .don_right = HID_KEY_J,
    .ka_right = HID_KEY_K,
};

const Utils::DrumKeys drum_keys_p2 = {
    .ka_left = HID_KEY_C,
    .don_left = HID_KEY_B,
    .don_right = HID_KEY_N,
    .ka_right = HID_KEY_COMMA,
};

const Utils::ControllerKeys controller_keys = {
    .up = HID_KEY_ARROW_UP,
    .down = HID_KEY_ARROW_DOWN,
    .left = HID_KEY_ARROW_LEFT,
    .right = HID_KEY_ARROW_RIGHT,

    .north = HID_KEY_L,
    .east = HID_KEY_BACKSPACE,
    .south = HID_KEY_ENTER,
    .west = HID_KEY_P,

    .l = HID_KEY_Q,
    .r = HID_KEY_E,

    .start = HID_KEY_ESCAPE,
    .select = HID_KEY_TAB,
    .home = HID_KEY_NONE,
    .share = HID_KEY_NONE,

    .l3 = HID_KEY_NONE,
    .r3 = HID_KEY_NONE,
};

const I2c i2c_config = {
    .sda_pin = 8,
    .scl_pin = 9,
    .block = i2c0,
    .speed_hz = 1000000,
};

const Peripherals::Drum::Config drum_config = {
    .trigger_thresholds =
        {
            .don_left = 100,
            .ka_left = 100,
            .don_right = 100,
            .ka_right = 100,
        },

    .double_trigger_mode = Peripherals::Drum::Config::DoubleTriggerMode::Off,
    .double_trigger_thresholds =
        {
            .don_left = 1500,
            .ka_left = 1500,
            .don_right = 1500,
            .ka_right = 1500,
        },

    .cutoff_thresholds =
        {
            .don_left = 4095,
            .ka_left = 4095,
            .don_right = 4095,
            .ka_right = 4095,
        },

    .weighted_comparison_mode = Peripherals::Drum::Config::WeightedComparisonMode::On,

    .debounce_delay_ms = 25,
    .don_debounce = 30,
    .kat_debounce = 30,
    .crosstalk_debounce = 30,
    .key_timeout_ms = 20,
    .roll_counter_timeout_ms = 500,

    .adc_channels =
        {
            .don_left = 1,
            .ka_left = 0,
            .don_right = 2,
            .ka_right = 3,
        },

    // ADC Config, either InternalAdc or ExternalAdc
    // .adc_config =
    //     Peripherals::Drum::Config::InternalAdc{
    //         .sample_count = 16,
    //     },

    .adc_config =
        Peripherals::Drum::Config::ExternalAdc{
            .spi_block = spi1,
            .spi_speed_hz = 2000000,
            .spi_mosi_pin = 11,
            .spi_miso_pin = 12,
            .spi_sclk_pin = 10,
            .spi_scsn_pin = 13,
        },
};

const Peripherals::Controller::Config controller_config = {
    .pins =
        {
            .dpad =
                {
                    .up = 27,
                    .down = 7,
                    .left = 26,
                    .right = 14,
                },
            .buttons =
                {
                    .north = 1,
                    .east = 6,
                    .south = 2,
                    .west = 5,

                    .l = 29,
                    .r = 0,

                    .start = 3,
                    .select = 28,
                    .home = 4,
                    .share = 15,
                },
        },

    .debounce_delay_ms = 25,

    // GPIO Config, either InternalGpio or ExternalGpio
    .gpio_config = Peripherals::Controller::Config::InternalGpio{},
    // .gpio_config =
    //     Peripherals::Controller::Config::ExternalGpio{
    //         .i2c =
    //             {
    //                 .block = i2c_config.block,
    //                 .address = 0x20,
    //             },
    //     },
};

const Peripherals::StatusLed::Config led_config = {
    .idle_color = {.r = 0, .g = 0, .b = 0},
    .don_left_color = {.r = 255, .g = 66, .b = 33},
    .ka_left_color = {.r = 107, .g = 189, .b = 198},
    .don_right_color = {.r = 255, .g = 66, .b = 33},
    .ka_right_color = {.r = 107, .g = 189, .b = 198},

    .led_pin = 15,
    .led_count = 84,
    .is_rgbw = false,
    .reversed = true,
    .max_current_ma = 350,

    .brightness = 255,
    .enable_player_color = true,
};

const Peripherals::Display::Config display_config = {
    .i2c_block = i2c_config.block,
    .i2c_address = 0x3C,
};

} // namespace Default
} // namespace Doncon::Config

#endif // GLOBALCONFIGURATION_H_