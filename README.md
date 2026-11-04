# FTMS Gradient Proxy

ESP32 BLE bridge between Zwift and a Garmin Tacx Neo 3M, with an iPhone-accessible Wi-Fi settings page. It changes the grade in FTMS Indoor Bike Simulation Parameters (0x11); other control commands and the trainer's actual result codes pass through.

**Review verdict:** the original sketch was not a working transparent FTMS control bridge: it never subscribed to trainer Control Point indications, invented success from an ATT write, and confused Machine Status with Training Status. Those blockers are fixed here. The revised firmware builds for classic ESP32 with NimBLE-Arduino 2.3.7 and passes host bridge/filter tests. **It has not been flashed or ridden on the user's Neo 3M.** Zwift discovery, trainer firmware compatibility, runtime heap and Wi-Fi/BLE timing remain hardware acceptance steps, not established results. See [the review](docs/review.md).

```
Zwift  <-- BLE peripheral FTMS -->  ESP32  <-- BLE central FTMS -->  Neo 3M
                                    |
                              Wi-Fi settings
```

## Hardware and build

Use a classic dual-core ESP32 Dev Module (ELEGOO/CP2102), 4 MB flash. ESP32-S2 has no BLE; ESP32-C3/S3 have different targets and were not tested. No wiring to the trainer is required.

Pinned dependencies used for the build:

- Arduino ESP32 core **3.3.12**
- NimBLE-Arduino **2.3.7**, not the old README's incompatible 1.4.x dependency
- `esp32:esp32:esp32:PartitionScheme=huge_app` (3 MB app, no OTA)

With `arduino-cli` installed:

```sh
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install NimBLE-Arduino@2.3.7
./scripts/build-firmware
arduino-cli board list
arduino-cli upload --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --port /dev/ttyUSB0 --input-dir build/firmware
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

Replace the port with the CP2102 port shown by `board list`. Press EN/reset with the monitor open to see the settings login. Some boards require holding BOOT as upload starts. Flashing the huge-app partition scheme removes OTA capability; changing a previously flashed partition scheme may require erasing flash, which also removes saved settings. No upload/erase has been performed during this review.

In Arduino IDE, open `ftms-proxy.ino`, select ESP32 Dev Module / 4MB / Huge APP (3MB No OTA), and install the pinned library/core versions above. Host tests are under `tests/` so Arduino does not compile their `main()` functions.

## First setup from an iPhone

1. On first boot, join the `FTMS-Setup-xxxx` hotspot. The generated password is printed over USB serial; it is also the web password. It persists in NVS and is never committed to source.
2. Open `http://192.168.4.1/` in Safari. Captive DNS also redirects setup probes; if the iPhone's captive sheet does not show the login, use Safari explicitly. Username: **admin**; password: the generated serial password.
3. Enter your home SSID/password under Wi-Fi, save, then reboot using the page. Open `http://ftms-proxy.local/` or the station IP printed in serial. A failed initial join falls back to the hotspot after 20 seconds; losing an established Wi-Fi connection falls back after 30 seconds. The fallback stops station reconnection attempts; saving Wi-Fi and rebooting tries again.
4. Set the trainer-name substring to match its actual advertised name. The default is `Tacx`; a name change takes effect on reboot. During one boot the first successfully discovered trainer address is pinned, so another trainer cannot silently replace it after a disconnect.
5. Save grade settings. These take effect on subsequent incoming simulation commands and survive reboot. Settings changes do not generate independent trainer commands.

No Web Bluetooth is used, so Safari does not need BLE browser support. The page has no cloud/CDN assets, scans no Wi-Fi networks, and polls stats once every three seconds. HTTP Basic authentication protects the page/API; a per-boot form token and Origin/Host checks protect mutations. Use only a trusted LAN/hotspot: HTTP and standard NVS are not TLS or encrypted flash storage. Do not expose the port to the internet. Wi-Fi passwords are write-only through the API and never logged; serial prints the separate device-admin/setup password for physical recovery.

## Settings

| Setting | Range/default | Meaning |
| --- | --- | --- |
| Enable filtering | on | Off forwards grade unchanged; BLE proxy remains connected |
| Descent multiplier | 0–2, default 0.5 | Negative grades only; 0 makes descents flat |
| Climb multiplier | 0–2, default 1 | Nonnegative grades |
| Climb cap | 0–30%, default 0 | 0 disables cap; this is not a guarantee of the trainer's physical supported range |
| Crest smoothing | 0–10000 ms, default 0 | Interpolates from the last sent climb/flat grade toward descent, only when Zwift sends a new grade; no timer-generated writes |
| Trainer name contains | 1–31 bytes, default `Tacx` | Reboot to change discovery target |

Grade bytes 3–4 are little-endian signed 0.01% units. Other simulation fields are preserved byte for byte. Scaling rounds half-units away from zero and saturates to int16 instead of wrapping. For example, `-8.00%` becomes `-4.00%` at 0.5. ERG target power (0x05), Request Control (0x00), Reset (0x01), etc. are not scaled. Reboot is refused while Zwift is connected.

Stats show the last requested and transmitted grades (not proof the trainer accepted them), both connection states, last result-relay latency, current/minimum heap, web task stack high-water mark, and MTU drops. `/api/state` exposes the diagnostic values after authentication. Large FTMS records are generated as MTU-23 fragments by the trainer; they are not arbitrarily chopped or silently truncated by the proxy.

## Ride acceptance: still required

Before relying on this firmware, run these checks on the actual board/trainer:

1. Update/check the Neo's firmware using Garmin's supported tools. Disconnect apps controlling the trainer. Select **FTMS Proxy** as both Zwift's power source and controllable trainer (and cadence if offered); do not leave Zwift controlling the Neo directly. Advertising includes FTMS UUID and indoor-bike service data. No private Tacx FE-C-over-BLE protocol is used, and proprietary road-feel/virtual-shifting features are not promised.
2. Confirm power/cadence/speed arrive; Request Control and Reset get genuine successful trainer indications; ERG and positive grade behavior match direct connection. Test a gentle descent at scale 1, then 0.5. Zwift's own Trainer Difficulty also affects the grade it requests.
3. Unpair/re-pair Zwift, power-cycle the trainer and repeat. Trainer loss must disconnect Zwift; Zwift loss must reset the trainer link and release control. No old commands should replay on reconnect. Reboot the proxy after a trainer firmware/profile change.
4. Ride at least 30 minutes with the Safari page open on the home Wi-Fi. Watch minimum heap and both task stack margins in stats/serial; look for resets, stalled indications, missing notifications, rising MTU drops or increased response latency compared with Wi-Fi off (serial command `wifi off`; `wifi on` restores the startup network). Compile-time RAM usage does not prove runtime headroom.
5. If coexistence is unstable, use the hotspot for setup only, then issue `wifi off` over serial before riding and investigate on-board measurements. An alternative is a dedicated BLE settings characteristic with an Android/desktop Web Bluetooth app, but that would not support iPhone Safari and is not implemented here.

ESP32 shares one radio between Wi-Fi and BLE. Station-mode coexistence is supported by Espressif; soft-AP connected coexistence is marked unstable in their matrix. Prefer home Wi-Fi for riding. Web handling runs in a separate 8 KB task on core 1; no HTTP parser or NVS write runs in the NimBLE host callbacks (core 0). Only one controller and one outstanding control procedure are allowed. See [review limitations](docs/review.md), including NimBLE's ATT-error callback limitation.

## Tests

```sh
./scripts/test-host                     # C++11 compiler required
# Optional desktop Chromium UI check; dev dependencies only:
npm install --no-save --package-lock=false playwright@1.64.1
npx playwright install chromium
node tests/test_settings_ui.cjs
```

Host tests cover the original filter cases, crest/cap/rounding/saturation/settings validation, 100,000 seeded grades, and the actual bridge code with fake BLE/RTOS transports. The UI test extracts the actual embedded HTML and checks mobile layout/forms with HTTP fixtures. Neither substitutes for the hardware acceptance ride. The ESP32 build output is recorded in the PR and [validation notes](docs/validation.md).
