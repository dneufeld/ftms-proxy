# Validation, 2026-11-04

No board was attached, no firmware was flashed and no trainer was controlled.

## Firmware build

- arduino-cli 1.5.2-rc.1 (fef6e48df)
- ESP32 Arduino core 3.3.12
- NimBLE-Arduino 2.3.7
- FQBN `esp32:esp32:esp32:PartitionScheme=huge_app`
- Two compiler jobs, classic ESP32, no PSRAM dependency

The build script stages only the sketch and firmware C++ headers/sources into a correctly named Arduino sketch directory. Host tests remain outside Arduino's source search path.

```
Sketch uses 1261152 bytes (40%) of program storage space. Maximum is 3145728 bytes.
Global variables use 60340 bytes (18%) of dynamic memory, leaving 267340 bytes for local variables. Maximum is 327680 bytes.
```

This is **static** usage, not runtime free heap. Wi-Fi driver buffers, BLE allocations, task stacks and HTTP requests reduce the latter. Runtime minimum heap and task stack watermarks are exposed for the board acceptance test; this review cannot assert those margins or RF timing.

## Host tests

Compiled with GCC 16.2.0, `-std=c++11 -Wall -Wextra -Werror`:

```
18/18 tests passed
Settings/filter tests PASS (including 100000 seeded grades)
Actual bridge tests PASS: queued writes, actual result relay, sign/bytes, reset, pipelining, disconnect
```

Original half-unit expectations were changed intentionally from truncation toward zero to nearest rounding, half away from zero. Other existing grade/pass-through cases remain. Additional tests cover crest interpolation, millis rollover, climb multiplier/cap, saturation, invalid settings, null/short/long packets and preservation of non-grade bytes.

The bridge test compiles the actual `ble_peripheral.cpp` with fake BLE and FreeRTOS queue transports. It checks that a callback only queues a command, no success appears before the trainer result, a denial is relayed unchanged, the simulation payload is correctly transformed, invalid parameters stay local, and invalid pipelining/link loss discard pending work. It does not emulate a radio or prove the Neo's FTMS behavior.

## Browser check

Playwright 1.64.1 / Chromium, 390 × 844 touch viewport, actual embedded HTML extracted from `settings_web.cpp`, local HTTP fixtures:

```
Mobile settings UI PASS: 390px layout, escaped values, forms/token, password clearing, error feedback
```

Checks include no horizontal overflow, rendering a trainer name containing HTML as plain input text, settings fields/CSRF submission, write-only password clearing and a rejected reboot's error message. This is not a physical iPhone Safari test; it uses web APIs supported by Safari and no Web Bluetooth.

## Pending hardware acceptance

Follow the README: discover/control/power pairing in Zwift, Request Control/Reset/ERG, grade equivalence, two-sided reconnect, data fragmentation, 30-minute runtime heap/stack and Wi-Fi-on/off timing comparison. If Wi-Fi coexistence is unstable, the serial Wi-Fi-off mode allows continued BLE operation; do not treat the compile as a measurement of the physical setup.
