# Serial Configuration Guide

This document explains how to use the USB serial configuration interface for runtime parameter adjustment during testing.

## Overview

The serial configuration system allows you to read and modify controller settings via USB CDC serial connection without using the OLED menu. This is particularly useful for:
- Quick testing of different threshold values
- Automated configuration scripts
- Bulk parameter adjustments
- Remote configuration
- Web-based configurator integration

## Protocol

The system uses a simple command-based protocol:

### Commands

**Configuration Commands:**
- **1000** - Read all settings (returns 46 key:value pairs)
- **1001** - Save current settings to flash memory
- **1002** - Enter write mode (to send key:value pairs)
- **1003** - Reload settings from flash
- **1004** - Reboot to BOOTSEL mode (firmware update mode)

**Streaming Commands:**
- **2000** - Start streaming sensor data (CSV format, ~100Hz)
- **2001** - Stop streaming sensor data
- **2002** - Start streaming input status (binary format, each pad is a bit)

**PS4 Authentication Commands:**
- **4000** - Start PS4 auth key upload (device enters binary upload mode, responds with `PS4_AUTH_READY`)
- **4001** - Query PS4 auth status (responds with `PS4_AUTH_STATUS:0` or `PS4_AUTH_STATUS:1`)
- **4002** - Clear stored PS4 auth credentials (responds with `PS4_AUTH_CLEARED`, then reboots)
- **4003** - Export stored PS4 auth credentials (see response format below)

### Setting Keys

**Trigger Thresholds:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 0 | Don Left Threshold | uint32 | Left face (don) sensitivity |
| 1 | Ka Left Threshold | uint32 | Left rim (ka) sensitivity |
| 2 | Don Right Threshold | uint32 | Right face (don) sensitivity |
| 3 | Ka Right Threshold | uint32 | Right rim (ka) sensitivity |

**Debounce Settings:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 4 | Don Debounce | uint16 | Lockout time between don hits (left/right) (ms) |
| 5 | Kat Debounce | uint16 | Lockout time between ka hits (left/right) (ms) |
| 6 | Crosstalk Debounce | uint16 | Time to ignore ka after don hit (ms) |
| 7 | Debounce Delay | uint16 | Same-pad lockout time (can't hit same pad twice) (ms) |
| 8 | Key Timeout | uint16 | How long button appears pressed to OS (ms) |

**Double Trigger Settings:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 9 | Double Trigger Mode | uint16 | 0=Off, 1=Threshold |
| 10 | Double Trigger Don Left | uint32 | Left face double trigger threshold |
| 11 | Double Trigger Ka Left | uint32 | Left rim double trigger threshold |
| 12 | Double Trigger Don Right | uint32 | Right face double trigger threshold |
| 13 | Double Trigger Ka Right | uint32 | Right rim double trigger threshold |

**Cutoff Thresholds:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 14 | Cutoff Don Left | uint16 | Don left cutoff value |
| 15 | Cutoff Ka Left | uint16 | Ka left cutoff value |
| 16 | Cutoff Don Right | uint16 | Don right cutoff value |
| 17 | Cutoff Ka Right | uint16 | Ka right cutoff value |

**Keyboard Mappings - Drum P1:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 18 | Drum P1 Ka Left | uint16 | Keyboard key code for P1 ka left |
| 19 | Drum P1 Don Left | uint16 | Keyboard key code for P1 don left |
| 20 | Drum P1 Don Right | uint16 | Keyboard key code for P1 don right |
| 21 | Drum P1 Ka Right | uint16 | Keyboard key code for P1 ka right |

**Keyboard Mappings - Drum P2:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 22 | Drum P2 Ka Left | uint16 | Keyboard key code for P2 ka left |
| 23 | Drum P2 Don Left | uint16 | Keyboard key code for P2 don left |
| 24 | Drum P2 Don Right | uint16 | Keyboard key code for P2 don right |
| 25 | Drum P2 Ka Right | uint16 | Keyboard key code for P2 ka right |

**Keyboard Mappings - Controller:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 26 | Controller Up | uint16 | Keyboard key code for D-pad up |
| 27 | Controller Down | uint16 | Keyboard key code for D-pad down |
| 28 | Controller Left | uint16 | Keyboard key code for D-pad left |
| 29 | Controller Right | uint16 | Keyboard key code for D-pad right |
| 30 | Controller North | uint16 | Keyboard key code for North button (Y/Triangle) |
| 31 | Controller East | uint16 | Keyboard key code for East button (B/Circle) |
| 32 | Controller South | uint16 | Keyboard key code for South button (A/Cross) |
| 33 | Controller West | uint16 | Keyboard key code for West button (X/Square) |
| 34 | Controller L | uint16 | Keyboard key code for L button |
| 35 | Controller R | uint16 | Keyboard key code for R button |
| 36 | Controller Start | uint16 | Keyboard key code for Start |
| 37 | Controller Select | uint16 | Keyboard key code for Select |
| 38 | Controller Home | uint16 | Keyboard key code for Home |
| 39 | Controller Share | uint16 | Keyboard key code for Share |
| 40 | Controller L3 | uint16 | Keyboard key code for L3 |
| 41 | Controller R3 | uint16 | Keyboard key code for R3 |

**ADC Channel Mappings:**

| Key | Setting | Type | Description |
|-----|---------|------|-------------|
| 42 | ADC Channel Don Left | uint16 | ADC channel number for don left (0-3) |
| 43 | ADC Channel Ka Left | uint16 | ADC channel number for ka left (0-3) |
| 44 | ADC Channel Don Right | uint16 | ADC channel number for don right (0-3) |
| 45 | ADC Channel Ka Right | uint16 | ADC channel number for ka right (0-3) |

**Note:** All 46 keys (0-45) can be configured via serial protocol using command-line tools or custom scripts.

### PS4 Authentication Upload Protocol

Command **4000** switches the device into binary upload mode, expecting a PAK1 bundle:

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 4 | Magic: `PAK1` (ASCII) |
| 4 | 2 | `key_len` — PEM key length in bytes (u16 LE) |
| 6 | 2 | Version (u16 LE, currently `1`) |
| 8 | 16 | Controller serial number (raw bytes) |
| 24 | 256 | Signature blob (raw bytes) |
| 280 | key_len | RSA private key (`key.pem` contents, UTF-8) |
| 280+key_len | 4 | CRC32 over all preceding bytes (u32 LE, IEEE 802.3) |

Upload flow:
1. Send `4000\n` → wait for `PS4_AUTH_READY`
2. Send the PAK1 bundle as raw binary (64-byte chunks recommended)
3. Wait for `PS4_AUTH_SAVED` (success) or `PS4_AUTH_ERROR:<reason>` (failure)
4. On success the device reboots automatically

Error codes: `BAD_MAGIC`, `BAD_KEY_LEN`, `TOO_LARGE`, `BAD_CRC`, `STORE_FAILED`

The credentials are stored in a dedicated flash sector (second-to-last 4 KB, separate from settings). On boot, if valid credentials are present the PS4 auth provider is initialised automatically.

### PS4 Authentication Export (4003)

Command **4003** returns the stored credentials as text lines:

```
PS4_AUTH_STATUS:1
PS4_AUTH_SERIAL_HEX:<32 uppercase hex chars>
PS4_AUTH_SIGNATURE_HEX:<512 uppercase hex chars>
PS4_AUTH_KEY_PEM_BASE64:<base64-encoded PEM key>
```

If no credentials are stored, only `PS4_AUTH_STATUS:0` is returned.

The base64 encoding uses the standard RFC 4648 alphabet (mbedTLS implementation). Decode with `base64 -d` or any standard base64 decoder to recover the original `key.pem` text.

## Usage Examples

### Using the Python Test Script

```bash
# Install pyserial if not already installed
pip install pyserial

# Read all current settings
python test_serial_config.py COM3 read

# Set don left threshold to 1000
python test_serial_config.py COM3 set 0 1000

# Save settings to flash
python test_serial_config.py COM3 save

# Reload settings from flash
python test_serial_config.py COM3 reload
```

### Manual Usage (Serial Terminal)

1. Connect to the COM port at 115200 baud
2. Send commands as plain text:

```
1000          # Read all settings
1002          # Enter write mode
0:1000        # Set don left threshold to 1000
1:900         # Set ka left threshold to 900
1001          # Save to flash
```

### Write Mode Details

When you send **1002**, the device enters write mode and accepts key:value pairs:
- Format: `key:value` (e.g., `0:800`)
- Multiple values can be sent space-separated: `0:800 1:900 2:800`
- Write mode exits automatically after receiving at least one value
- Values are applied immediately but not saved to flash until you send **1001**
- The device supports **46 keys total** (0-45)

### Streaming Mode

When you send **2000**, the device starts streaming sensor data:
- **Format:** 16-character Hexadecimal string (64-bit packed integer)
- **Content:** 4 x 16-bit unsigned integers (Raw ADC values)
- **Example:** `01F403E801F400FF` (KaL:500, DonL:1000, DonR:500, KaR:255)
- **Stop:** Send **2001** to stop streaming

**Packing Structure (64-bit Hex):**
`AAAABBBBCCCCDDDD`
- **AAAA (Bits 48-63):** Ka Left Raw
- **BBBB (Bits 32-47):** Don Left Raw
- **CCCC (Bits 16-31):** Don Right Raw
- **DDDD (Bits 0-15):**  Ka Right Raw

**Usage:**
```bash
# Start streaming
python test_serial_config.py COM3 stream

# (In your script, read line, parse as hex int64, unpack)
# value = int(line, 16)
# ka_left = (value >> 48) & 0xFFFF
# ...
```

### Input Status Streaming

When you send **2002**, the device starts streaming only the digital input status:
- **Format:** Hexadecimal encoded bitmask (0-F)
- **Example:** `6` (Binary 0110 -> Don Right + Don Left triggered)
- **Stop:** Send **2001** to stop streaming

**Bitmask Format:**
The output is a single hexadecimal character representing the lower 4 bits of the byte.
- **Bit 0 (LSB, Value 1):** Ka Left
- **Bit 1 (Value 2):** Don Left
- **Bit 2 (Value 4):** Don Right
- **Bit 3 (Value 8):** Ka Right

**Usage:**
```bash
# Start streaming
python test_serial_config.py COM3 stream_input
```

## Integration with Existing System

The serial configuration system:
- ✅ Integrates with existing `SettingsStore` class
- ✅ Respects the same value ranges and types
- ✅ Works alongside OLED menu system
- ✅ Changes made via serial are immediately applied to the Drum peripheral
- ✅ Changes persist across reboots when saved with command 1001
- ⚠️ Does NOT require menu system or cause settings conflicts
- ⚠️ Changes take effect immediately but must be saved manually

## Feature Summary

| Feature | Description |
|---------|-------------|
| Protocol | Commands 1000-1004 (config), 2000-2002 (streaming), 4000-4003 (PS4 auth) |
| Parameters | 46 configurable keys (0-45) |
| Value Storage | uint32_t for thresholds, uint16_t for other settings |
| Integration | Integrated with SettingsStore for persistence |
| Persistence | Automatic flash wear leveling (settings), dedicated flash sector (PS4 auth) |
| Streaming Mode | Commands 2000/2001 for live sensor data (~100Hz), 2002 for digital input status |
| PS4 Authentication | Commands 4000-4003 for runtime credential upload/query/clear/export |
| Features | Thresholds, debounce, double trigger, cutoffs, keyboard mappings, ADC channels, PS4 auth |

## USB Mode Compatibility

Serial configuration requires a USB mode that includes CDC (serial) interface:

✅ **Keyboard P1/P2** - Includes CDC + HID (recommended for PC testing)
✅ **Debug** - CDC only (no controller functionality)
❌ **Other modes** (Switch, PS4, Xbox, MIDI) - HID/Vendor only, no CDC

**To use serial configuration:**
1. Switch to Keyboard mode via OLED menu
2. Connect to PC - it will appear as both a keyboard and COM port
3. Use the web configurator or Python script
4. Settings persist across all modes!

## Troubleshooting

**Settings not persisting:**
- Make sure to send command **1001** to save to flash
- Verify "Settings saved" message appears

**No response from device:**
- Check COM port is correct
- Ensure device is properly connected
- Try reconnecting USB cable
- Verify baudrate is 115200

**Changes not taking effect:**
- Settings are applied immediately when written
- If using the menu system simultaneously, menu changes may override serial changes
- Exit menu before using serial configuration

## Implementation Details

- **Location**: `src/utils/SerialConfig.cpp` and `include/utils/SerialConfig.h`
- **Main Loop**: Called from Core 0 main loop via `serial_config.processSerial()`
- **Non-blocking**: All operations are non-blocking and safe for main loop
- **Thread-safe**: Uses existing SettingsStore which handles thread safety
