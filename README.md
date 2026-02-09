# FTMS Gradient Proxy

BLE proxy that sits between Zwift and a Tacx Neo 3M trainer. Intercepts FTMS gradient commands and scales negative (descent) grades by a configurable factor, making descents feel less extreme while leaving climbs untouched.

## Architecture

```
Zwift  ←BLE Peripheral→  ESP32  ←BLE Central→  Tacx Neo 3M
```

The ESP32 runs dual BLE roles via NimBLE:
- **Central**: Connects to the Tacx trainer by scanning for devices with "Tacx" in the name
- **Peripheral**: Advertises as "FTMS Proxy" so Zwift connects to it

All FTMS data passes through transparently. The only modification: when the proxy sees an Indoor Bike Simulation Parameters write (opcode `0x11`) with a negative grade, it scales the grade by `DESCENT_SCALE_FACTOR` (default 50%).

## Hardware

- ELEGOO ESP32 (classic, CP2102, BLE 4.2)
- Garmin Tacx Neo 3M

No wiring needed — everything is BLE.

## Configuration

Edit these defines at the top of `ftms-proxy.ino`:

| Constant | Default | Description |
|----------|---------|-------------|
| `DESCENT_SCALE_FACTOR` | `0.5` | Multiply descent grades by this factor (0.0 = flat, 1.0 = full) |
| `TARGET_DEVICE_NAME` | `"Tacx"` | Substring to match when scanning for the trainer |

## Build & Flash

### Arduino IDE

1. Install [ESP32 board support](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
2. Install **NimBLE-Arduino** library via Library Manager
3. Open `ftms-proxy.ino`
4. Select board: **ESP32 Dev Module**
5. Select correct COM/serial port
6. Upload

### PlatformIO

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = h2zero/NimBLE-Arduino@^1.4.0
monitor_speed = 115200
```

## Usage

1. Flash to ESP32, open Serial Monitor at 115200 baud
2. Power on the Tacx Neo 3M
3. Serial output should show the trainer being discovered and connected
4. Open Zwift, scan for BLE sensors — "FTMS Proxy" should appear
5. Pair as controllable trainer
6. Ride — serial log shows grade interception:
   ```
   [Filter] Grade: -800 -> -400 (-8.0% -> -4.0%)
   [Filter] Grade: 500 (5.0%) — no change
   [Passthrough] Target Power: 200 W
   ```

## How It Works

### FTMS Control Point (0x2AD9)

Zwift writes commands here. The proxy intercepts writes and:
- **Opcode 0x11** (Indoor Bike Simulation): Extracts the grade field (sint16, 0.01% units). If negative, multiplies by `DESCENT_SCALE_FACTOR`. Forwards the (possibly modified) packet to the trainer.
- **All other opcodes** (target power, reset, etc.): Forwarded unchanged.

### Other Characteristics

- **Indoor Bike Data (0x2AD2)**: Power/cadence/speed notifications from trainer → forwarded to Zwift
- **Fitness Machine Status (0x2AD3)**: Status notifications from trainer → forwarded to Zwift
- **Feature (0x2ACC), Resistance Range (0x2AD6), Power Range (0x2AD8)**: Read once from trainer at connection, cached, served to Zwift

## Troubleshooting

- **Proxy doesn't find trainer**: Make sure the trainer is on and not connected to another device. Check that the trainer's BLE name contains "Tacx".
- **Zwift doesn't see proxy**: The proxy only starts advertising after connecting to the trainer. Check serial output for connection status.
- **ERG mode not working**: ERG mode uses target power (opcode 0x05), which passes through unchanged. Check serial for "Target Power" messages.
- **Grade not being modified**: Only negative grades are scaled. Check serial output for "[Filter]" messages showing original and modified values.
