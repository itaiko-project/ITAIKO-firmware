#!/usr/bin/env python3
"""
PS4 Authentication timing test for ITAIKO/DonCon2040 firmware.

Tests the full PS4 auth handshake sequence (reports 0xF0 → 0xF2 poll → 0xF1 read)
and measures how long Core 0 is blocked during RSA signing, which is the suspected
cause of input lag on the PS4 Tatacon mode.

Usage:
    python test_ps4_auth.py [--list]

Requirements:
    pip install hid
    On Linux you may need: sudo usermod -aG plugdev $USER  (and re-login)
    Or run with sudo for quick testing.
"""

import argparse
import binascii
import os
import struct
import sys
import time
from typing import Any

try:
    import hid
except ImportError:
    print("ERROR: 'hid' package not found. Run: pip install hid")
    sys.exit(1)

# ----------------------------------------------------------------------------
# Device identity
# ----------------------------------------------------------------------------
PS4_TATACON_VID = 0x0F0D
PS4_TATACON_PID = 0x00C9

# ----------------------------------------------------------------------------
# Protocol constants (must match firmware ps4_auth.c)
# ----------------------------------------------------------------------------
CHUNK_LEN = 56          # CHALLENGE_CHUNK_LENGTH
CHALLENGE_LEN = 256     # PS4_AUTH_CHALLENGE_LENGTH

# Response struct layout (ps4_auth_challenge_response_t):
#   signature    256
#   serial        16
#   key_n        256
#   key_e        256
#   ca_signature 256
RESPONSE_LEN = 256 + 16 + 256 + 256 + 256  # 1040 bytes

REPORT_F0 = 0xF0   # SET_FEATURE  – send challenge chunk
REPORT_F1 = 0xF1   # GET_FEATURE  – read signed response chunk
REPORT_F2 = 0xF2   # GET_FEATURE  – poll signing state
REPORT_F3 = 0xF3   # GET_FEATURE  – reset auth state

SIGNING_BUSY  = 0x10
SIGNING_READY = 0x00

DEFAULT_COMMAND_DELAY_MS = 5.0
DEFAULT_POLL_INTERVAL_MS = 20.0


# ----------------------------------------------------------------------------
# HID backend compatibility helpers
# ----------------------------------------------------------------------------
def _read_field(obj: Any, key: str, default: Any = "") -> Any:
    """Read a key from a dict/object returned by hid backends."""
    if isinstance(obj, dict):
        return obj.get(key, default)
    return getattr(obj, key, default)


def _open_device(vid: int, pid: int):
    """
    Open a HID device across common python-hid backends.

    - `hid` package: uses `hid.device().open(vid, pid)`
    - `hidapi` package: often exposes `hid.Device(...)`
    """
    matches = [
        d for d in hid.enumerate()
        if int(_read_field(d, "vendor_id", -1)) == vid
        and int(_read_field(d, "product_id", -1)) == pid
    ]
    paths = [_read_field(d, "path", None) for d in matches]

    if hasattr(hid, "device"):
        # Prefer opening by explicit path to avoid stale VID/PID picks.
        for path in paths:
            if path and os.path.exists(path):
                dev = hid.device()
                dev.open_path(path)
                return dev
        dev = hid.device()
        dev.open(vid, pid)
        return dev

    if hasattr(hid, "Device"):
        # Try explicit path first when supported by this backend.
        for path in paths:
            if path and os.path.exists(path):
                return hid.Device(path=path)
        try:
            return hid.Device(vid=vid, pid=pid)
        except TypeError:
            return hid.Device(vendor_id=vid, product_id=pid)

    raise RuntimeError("Unsupported HID backend: no `device` or `Device` found")


def _set_blocking(dev: Any) -> None:
    """Set blocking mode in a backend-compatible way."""
    if hasattr(dev, "set_nonblocking"):
        dev.set_nonblocking(0)
    elif hasattr(dev, "nonblocking"):
        dev.nonblocking = False


def _device_identity(dev: Any) -> tuple[str, str]:
    """Return manufacturer/product strings across backends."""
    manufacturer = getattr(dev, "manufacturer", "") or ""
    product = getattr(dev, "product", "") or ""

    if not manufacturer and hasattr(dev, "get_manufacturer_string"):
        manufacturer = dev.get_manufacturer_string() or ""
    if not product and hasattr(dev, "get_product_string"):
        product = dev.get_product_string() or ""

    return manufacturer, product


# ----------------------------------------------------------------------------
# CRC32 helper (same nibble-table algorithm used in firmware)
# binascii.crc32 produces identical output for standard CRC32.
# ----------------------------------------------------------------------------
def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


# ----------------------------------------------------------------------------
# Report builders
# ----------------------------------------------------------------------------
def build_challenge_report(challenge_id: int, seq: int, chunk: bytes) -> bytes:
    """Build a 64-byte 0xF0 feature report (including report ID as first byte)."""
    padded = chunk.ljust(CHUNK_LEN, b"\x00")[:CHUNK_LEN]
    header = bytes([REPORT_F0, challenge_id, seq, 0x00]) + padded
    checksum = struct.pack("<I", crc32(header))
    return header + checksum  # 64 bytes total


def parse_state_report(data: bytes) -> tuple[int, int]:
    """Return (challenge_id, signing_state) from a 0xF2 GET_FEATURE response."""
    # hidapi prepends the report ID: [0xF2, challenge_id, signing_state, ...]
    if len(data) < 3:
        raise ValueError(f"State report too short: {len(data)} bytes")
    return data[1], data[2]


def parse_response_chunk(data: bytes) -> tuple[int, int, bytes]:
    """Return (challenge_id, seq, chunk_data) from a 0xF1 GET_FEATURE response."""
    if len(data) < 64:
        raise ValueError(f"Response chunk too short: {len(data)} bytes")
    challenge_id = data[1]
    seq          = data[2]
    chunk        = bytes(data[4:4 + CHUNK_LEN])
    reported_crc = struct.unpack_from("<I", data, 60)[0]
    computed_crc = crc32(bytes(data[:60]))
    if reported_crc != computed_crc:
        print(f"  [WARN] CRC mismatch on response chunk {seq}: "
              f"got {reported_crc:#010x}, expected {computed_crc:#010x}")
    return challenge_id, seq, chunk


# ----------------------------------------------------------------------------
# Main test
# ----------------------------------------------------------------------------
def list_devices():
    print("HID devices:")
    for d in hid.enumerate():
        vid = _read_field(d, "vendor_id", 0)
        pid = _read_field(d, "product_id", 0)
        manufacturer = _read_field(d, "manufacturer_string", "") or "Unknown"
        product = _read_field(d, "product_string", "") or "Unknown"
        print(f"  {int(vid):#06x}:{int(pid):#06x}  {manufacturer} – {product}")


def run_test(dev: Any, command_delay_s: float, poll_interval_s: float):
    print("=" * 60)
    print("PS4 Authentication Timing Test")
    print("=" * 60)
    print(f"Command delay: {command_delay_s * 1000.0:.1f}ms")
    print(f"Poll interval: {poll_interval_s * 1000.0:.1f}ms")

    # --- Reset auth state first ---
    print("\n[1] Sending reset (0xF3)...")
    reset_data = dev.get_feature_report(REPORT_F3, 8)
    print(f"    Reset response: {bytes(reset_data).hex()}")

    if command_delay_s > 0:
        time.sleep(command_delay_s)

    # --- Send challenge in chunks ---
    # Use a zeroed challenge — the firmware will still sign it, so timing is valid.
    challenge = bytes(CHALLENGE_LEN)
    num_chunks = (CHALLENGE_LEN + CHUNK_LEN - 1) // CHUNK_LEN
    challenge_id = 1

    print(f"\n[2] Sending {num_chunks} challenge chunks (0xF0) ...")
    for seq in range(num_chunks):
        offset = seq * CHUNK_LEN
        chunk = challenge[offset : offset + CHUNK_LEN]
        report = build_challenge_report(challenge_id, seq, chunk)
        sent = dev.send_feature_report(report)
        if sent < 0:
            print(f"    ERROR sending chunk {seq}")
            return
        print(f"    Chunk {seq}: sent {sent} bytes, offset {offset}–{offset+len(chunk)-1}")
        if command_delay_s > 0:
            time.sleep(command_delay_s)

    # --- Poll 0xF2 until signing is done ---
    print(f"\n[3] Polling signing state (0xF2) ...")
    t_sign_start = time.time()
    polls = 0
    while True:
        raw = dev.get_feature_report(REPORT_F2, 16)
        cid, state = parse_state_report(raw)
        polls += 1
        elapsed = time.time() - t_sign_start
        status = "BUSY" if state == SIGNING_BUSY else "READY" if state == SIGNING_READY else f"0x{state:02x}"
        print(f"    [{elapsed:6.3f}s] challenge_id={cid} state={status}", flush=True)
        if state == SIGNING_READY:
            break
        if elapsed > 15.0:
            print("    TIMEOUT waiting for signing!")
            return
        if poll_interval_s > 0:
            time.sleep(poll_interval_s)

    t_signing = time.time() - t_sign_start
    print(f"\n    Signing completed in {t_signing:.3f}s after {polls} polls")
    print(f"    Core 0 was blocked for approximately {t_signing:.3f}s")
    print(f"    Input lag window: ~{t_signing*1000:.0f}ms")

    # --- Read response chunks (0xF1) ---
    num_response_chunks = (RESPONSE_LEN + CHUNK_LEN - 1) // CHUNK_LEN
    print(f"\n[4] Reading {num_response_chunks} response chunks (0xF1) ...")
    response_buf = bytearray(RESPONSE_LEN)
    t_read_start = time.time()
    for i in range(num_response_chunks):
        raw = dev.get_feature_report(REPORT_F1, 64)
        cid, seq, chunk = parse_response_chunk(raw)
        offset = seq * CHUNK_LEN
        end = min(offset + CHUNK_LEN, RESPONSE_LEN)
        response_buf[offset:end] = chunk[:end - offset]
        print(f"    Chunk {seq} (seq from device): {chunk[:8].hex()}...")
        if command_delay_s > 0:
            time.sleep(command_delay_s)

    t_read = time.time() - t_read_start

    # --- Parse response fields ---
    signature    = response_buf[0:256]
    serial       = response_buf[256:272]
    key_n        = response_buf[272:528]
    key_e        = response_buf[528:784]
    ca_signature = response_buf[784:1040]

    print(f"\n[5] Response summary:")
    print(f"    Serial:         {serial.hex()}")
    print(f"    Signature[0:8]: {signature[:8].hex()}...")
    print(f"    key_n[0:8]:     {key_n[:8].hex()}...")
    print(f"    key_e[0:8]:     {key_e[:8].hex()}...")
    print(f"    Read time:      {t_read:.3f}s")

    print(f"\n{'='*60}")
    print(f"RESULTS")
    print(f"{'='*60}")
    print(f"  Signing (RSA) duration : {t_signing*1000:.1f} ms")
    print(f"  Response read duration : {t_read*1000:.1f} ms")
    print(f"  Total auth duration    : {(t_signing+t_read)*1000:.1f} ms")
    print(f"\n  During the {t_signing*1000:.0f}ms signing window, Core 0 is blocked.")
    print(f"  USB HID input reports cannot be sent during this time.")
    print(f"  This is the expected cause of input lag on PS4 mode.")


def main():
    parser = argparse.ArgumentParser(description="PS4 auth timing test for ITAIKO firmware")
    parser.add_argument("--list", action="store_true", help="List all HID devices and exit")
    parser.add_argument(
        "--command-delay-ms",
        type=float,
        default=DEFAULT_COMMAND_DELAY_MS,
        help=f"Delay between USB feature commands in milliseconds (default: {DEFAULT_COMMAND_DELAY_MS})",
    )
    parser.add_argument(
        "--poll-interval-ms",
        type=float,
        default=DEFAULT_POLL_INTERVAL_MS,
        help=f"Delay between 0xF2 poll commands in milliseconds (default: {DEFAULT_POLL_INTERVAL_MS})",
    )
    args = parser.parse_args()

    if args.list:
        list_devices()
        return

    print(f"Looking for device {PS4_TATACON_VID:#06x}:{PS4_TATACON_PID:#06x} ...")

    try:
        dev = _open_device(PS4_TATACON_VID, PS4_TATACON_PID)
        _set_blocking(dev)
    except Exception as e:
        print(f"ERROR: Could not open device: {e}")
        print("Tips:")
        print("  - Make sure the ITAIKO is in PS4 Tatacon mode")
        print("  - Try running with sudo, or add a udev rule:")
        print('    echo \'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="0f0d", '
              'ATTRS{idProduct}=="00c9", MODE="0666"\' '
              '| sudo tee /etc/udev/rules.d/99-itaiko.rules')
        print("    sudo udevadm control --reload-rules && sudo udevadm trigger")
        sys.exit(1)

    try:
        manufacturer, product = _device_identity(dev)
        print(f"Opened: {manufacturer or 'Unknown'} – {product or 'Unknown'}\n")
        command_delay_s = max(0.0, args.command_delay_ms / 1000.0)
        poll_interval_s = max(0.0, args.poll_interval_ms / 1000.0)
        run_test(dev, command_delay_s=command_delay_s, poll_interval_s=poll_interval_s)
    finally:
        dev.close()


if __name__ == "__main__":
    main()
