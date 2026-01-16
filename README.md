# Axonapotamus

A Flipper Zero app to wirelessly trigger any Axon Body or Dash Cams in range to start recording.

Ported from [AxonCadabra](https://github.com/WithLoveFromMinneapolis/AxonCadabra) (Android).

## Features

- **Transmit (Single)**: Broadcast BLE advertising data to trigger camera recording
- **Transmit (Fuzz)**: Iterate through payload values at 500ms intervals to bypass trigger cooldown
- **Scan**: Detect nearby Axon BLE devices (OUI 00:25:DF) - *placeholder, not yet implemented*

## Notes

- Don't bet your life on this working - cameras can be configured to ignore non-allowlisted Axon serial numbers
- Cameras can only be forced to START recording; the operator can always stop it
- Fuzz mode attempts to bypass the per-device trigger cooldown by varying payload bytes

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
