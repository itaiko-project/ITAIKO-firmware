# ITAIKO Firmware

Firmware for DIY Taiko no Tatsujin arcade-style drum controllers based on the RP2040 microcontroller.

This project started as a fork of [DonCon2040](https://github.com/ravinrabbid/DonCon2040) by ravinrabbid. The input processing algorithm is inspired by [HIDtaiko](https://github.com/kasasiki3/HIDtaiko) by kasasiki3. Both projects deserve credit for the foundation this firmware builds on.

## Controller Emulation Modes

The firmware can emulate 12 different USB devices, selectable at runtime through the on-screen menu:

| Mode | Description |
|------|-------------|
| Switch Tatacon | HORI NSW-079 Taiko Drum (default) |
| Switch Horipad | Pro Controller, D-pad mode |
| PS3 Dualshock 3 | Standard PS3 controller |
| PS4 Tatacon | HORI PS4-095 Taiko Drum (requires authentication keys, see below) |
| PS4 Dualshock 4 | PC/Steam only, will **not** work on an actual PS4 |
| Keyboard P1 | DFJK mapping (configurable via serial) |
| Keyboard P2 | CBN, mapping (configurable via serial) |
| XInput | Xbox 360 controller |
| XInput Analog P1 | Analog triggers, compatible with [TaikoArcadeLoader](https://github.com/esuo1198/TaikoArcadeLoader) |
| XInput Analog P2 | Same as above, player 2 |
| MIDI | Note on/off output |
| Debug | Raw state dump over USB serial |

## Configurable Settings

All settings below can be adjusted at runtime through the on-screen menu (hold Start+Select for 2 seconds) and are saved to flash memory.

### Trigger Thresholds

Per-pad sensitivity thresholds for Don Left, Don Right, Ka Left, and Ka Right. Controls how hard you need to hit each pad for it to register.

### Cutoff Thresholds

Per-pad upper cutoff values. Hits above this value are clamped.

### Debounce Timings

Multiple independent debounce timers to prevent false inputs:

- **Global debounce delay** -- minimum time between any two inputs
- **Don debounce** -- lockout between left and right Don hits
- **Ka debounce** -- lockout between left and right Ka hits
- **Crosstalk debounce** -- lockout between Don and Ka (prevents bleed between pad types)
- **Key timeout** -- how long an input is held active after a hit. Some platforms need a minimum value here (Switch needs at least 25ms)

### Double Trigger (Large Notes)

Home versions of Taiko no Tatsujin give higher scores for large notes when both sides are hit simultaneously. Arcade versions only need a single harder hit. Two modes are available:

- **Off** -- both sides must be hit manually for large notes
- **Threshold** -- automatically triggers both sides if a single hit exceeds the configured threshold (per-pad)

## Serial Configuration

A USB CDC serial interface is available for remote configuration without using the on-screen menu. The official web configurator is available at [itaiko.com/configure](https://itaiko.com/configure).

## Building

### Prerequisites

- CMake 3.13+
- Pico SDK 2.2.0
- ARM GCC toolchain

### VSCode

Install the [Raspberry Pi Pico extension](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico), import the project, set the CMake variant to **Release**, and use 'Compile Project'.

### CLI

```sh
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

**Important:** The CMake variant must be set to `Release`. The default `Debug` build will cause the firmware to not start.

Set `PICO_SDK_PATH` to use a local SDK (otherwise fetched from GitHub). Set `PICO_BOARD` to target a different board (default: `waveshare_rp2040_zero`).

## Hardware

The firmware expects:

- **RP2040 board** (default config targets Waveshare RP2040-Zero)
- **SSD1306 OLED display** (128x64, I2C) for the menu and status display
- **Piezo sensors** (e.g. Sensatec GSS-4S*) providing analog signals to the ADC

Both internal RP2040 ADC and external MCP3204 SPI ADC are supported. Hardware pin assignments, I2C addresses, and default values are configured in `include/GlobalConfiguration.h`.

## PS4 Authentication

The PS4 requires controllers to periodically sign a cryptographic challenge. Without valid authentication keys, the controller stops responding after roughly 8 minutes.

ITAIKO can perform this signing, but it requires a serial number, signature, and private key extracted from a genuine DualShock 4 controller. These credentials are not included in this repository and will not be distributed. You need to source them yourself.

Authentication keys can be uploaded to the controller through the [web configurator](https://itaiko.com/configure), which stores them in flash.

During the signing process (~2-3 seconds), the display will freeze, this will happen repeatedly while in ps4 tatakon mode. Drum and controller input is not affected.

## Acknowledgements

- [ravinrabbid](https://github.com/ravinrabbid) -- [DonCon2040](https://github.com/ravinrabbid/DonCon2040), the original project this firmware is forked from
- [kasasiki3](https://github.com/kasasiki3) -- [HIDtaiko](https://github.com/kasasiki3/HIDtaiko), whose input processing algorithm inspired the one used here
- [daschr](https://github.com/daschr) -- [SSD1306 OLED driver](https://github.com/daschr/pico-ssd1306)
- [FeralAI](https://github.com/FeralAI) -- DS3/XInput driver from the [GP2040 Project](https://github.com/FeralAI/GP2040)
- The Linux kernel contributors for documenting game controller protocols in their drivers
