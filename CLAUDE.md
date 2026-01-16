# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Claude Directives

**Apply these to every response:**
- Stop acting like an idiot
- Do not guess - if uncertain, investigate or ask
- Verify your responses - check facts against source code and documentation before answering
- Be consistent - use the same paths, tools, and patterns as previous successful operations

## Project Overview

Axonapotamus - A Flipper Zero BLE application that triggers Axon Body and Dash Cameras to start recording by broadcasting Axon Signal protocol payloads.

**Origin**: Forked from [WithLoveFromMinneapolis/AxonCadabra](https://github.com/WithLoveFromMinneapolis/AxonCadabra) (Android app). This is a complete rewrite for Flipper Zero.

## Build Commands

```bash
# Clone into Flipper firmware applications_user directory
cp -r flipper_app /path/to/flipperzero-firmware/applications_user/axonapotamus

# Build with Flipper firmware
cd /path/to/flipperzero-firmware
./fbt fap_axonapotamus

# Or build all external apps
./fbt fap_dist
```

Output: `build/f7-firmware-D/.extapps/axonapotamus.fap`

## BLE Protocol Specification

**Target Detection (Scanning)**:
- OUI prefix: `00:25:DF` (Axon devices)

**Trigger Broadcast (Advertising)**:
- Service UUID: `0xFE6C` (16-bit, little-endian in BLE: `6C FE`)
- Base payload (24 bytes):
  ```
  01 58 38 37 30 30 32 46 50 34 01 02 00 00 00 00 CE 1B 33 00 00 02 00 00
  ```

**Fuzz Mode** (bypasses per-device trigger cooldown):
- Interval: 500ms between payload changes
- Modified bytes:
  - Position 10: `(fuzzValue >> 8) & 0xFF`
  - Position 11: `fuzzValue & 0xFF`
  - Position 20: `(fuzzValue >> 4) & 0xFF`
  - Position 21: `(fuzzValue << 4) & 0xFF`
- Cycles through 0x0000-0xFFFF (65,536 values)

## Project Structure

```
flipper_app/
├── application.fam      # Flipper app manifest
└── axonapotamus.c       # Main app (BLE advertising, fuzz mode, UI)

app/                     # Original Android implementation (reference only)
```

## Key Implementation Details

- Uses Flipper's Extra Beacon API (`furi_hal_bt_extra_beacon_*`) for BLE advertising
- Uses Flipper's real BLE MAC address via `furi_hal_version_get_ble_mac()`
- UI: Submenu with Transmit/Fuzz/Scan options, Popup for active states
- Fuzz timer uses `FuriTimer` with 500ms periodic callback
- LED indicators: Cyan blink for single TX, Purple blink for fuzz TX

## Limitations

**BLE Scanning**: Not available in stock Flipper firmware. The firmware exposes Extra Beacon API for advertising but no high-level scanning API. Scanning would require direct HCI commands (`hci_le_set_scan_parameters`, `hci_le_set_scan_enable`) and hooking into BLE event callbacks for advertising reports - significant work beyond external app scope. Use nRF Connect or similar app to scan for Axon devices (OUI `00:25:DF`).
