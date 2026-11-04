# FTMS / NimBLE review

## Verdict and evidence boundary

The original 307bdee cannot be called a working FTMS control proxy. The byte offset and signed little-endian grade decoding were correct, but protocol responses were not: it treated an ATT write acknowledgement as successful FTMS execution, never subscribed to the trainer's result indications, and put Machine Status on the Training Status UUID. This PR fixes those issues and adds the settings app. The corrected firmware compiles and its bridge logic runs under host tests; there is no physical ESP32/Neo/Zwift test in this environment. A successful compile does not establish RF interoperability or safe runtime margins. The README ride checklist remains required.

## Findings and changes

| Area | Finding / resulting behavior |
| --- | --- |
| Simulation packet | Exactly 7 bytes: opcode 0x11, wind signed16 at 1–2 (0.001 m/s), grade signed16 at 3–4 (0.01%), unsigned Crr/Cw at 5/6. Only grade bytes change; malformed lengths are not forwarded or truncated. |
| Arithmetic | Original C++ cast truncated half units and could overflow with large multipliers. Now nearest rounding, half away from zero, saturating signed16; non-finite/out-of-range settings rejected. Defaults remain descent 0.5 / climb 1 / no cap or smoothing. |
| Control Point | Zwift WRITE + INDICATE on 0x2AD9. Trainer `subscribe(false, callback)` enables indications; its complete 0x80 result, including rejection and optional response parameters, is relayed unchanged. Successful `writeValue` alone never produces success. |
| Request Control / Reset | These remain real trainer procedures. No unsolicited control acquisition, no automatic reset that steals an active session; Reset resets the local smoothing history. Closing Zwift disconnects upstream, releasing that connection's control permission before re-pairing. |
| Callback scheduling | Original blocked the NimBLE host callback waiting for a synchronous central write. Now bounded one-command/one-response queues transfer work to Arduino loop. One operation remains busy until Zwift confirms the downstream indication; no command replay after loss. |
| Machine vs Training Status | Machine Status is 0x2ADA, required with a Control Point; Training Status is 0x2AD3. Each notification stream has its own callback. Training Status is also initially readable when present. |
| Discovery/profile | Require readable 8-byte Feature, writable/indicatable Control Point, and successful CP/Bike Data/Machine Status subscriptions before advertising. Proxy supported resistance, power, inclination and heart-rate ranges when present, not invented defaults. Read/subscription failures prevent a falsely ready proxy. |
| Advertising | Include FTMS 0x1826, service data `{available=1, indoor bike=0x0020}`, name `FTMS Proxy` and cycling appearance; scan response enabled. One Zwift controller at a time. Advertise only once a trainer profile is ready. |
| Scan / reconnect | NimBLE 2.x scan durations are milliseconds: original `start(10)` scanned for 10 ms, now 10000 ms. Bounded scan storage, 30% scan window, five-second retry spacing. Copy advertisements before their callback lifetime ends. Pin the first working trainer address for this boot. Target changes require reboot. |
| Trainer loss | Stop advertising, disconnect Zwift, discard pending work and smoothing state, then rediscover. Zwift loss also drops upstream so a new controller does not inherit stale permission. No stale command replay. |
| MTU | Both links negotiate preferred ATT MTU 23. The actual negotiated payload is then at least the mandatory 20 bytes, so the trainer splits FTMS data records according to FTMS flags; proxy forwards those fragments unchanged. Reject/drop-and-reset an unexpected >20-byte notification rather than send silently truncated data. |
| Tacx protocol | Garmin documents standard FTMS on the Neo 3M. No proprietary Tacx FE-C-over-BLE protocol is needed for this bridge. Proprietary road feel, virtual shifting, ANT+, standalone Cycling Power/CSC services and multi-controller semantics are not implemented. Pair the proxy in Zwift for both power and control. Physical pairing remains unverified. |
| Wi-Fi / storage | Tiny flash-resident HTML, no Web Bluetooth or external assets. STA with bounded initial join/loss fallback to captive WPA2 AP. Config and credentials persisted in NVS; failed config writes do not update RAM. Write-only Wi-Fi password, unique generated device login, no committed credentials. NVS is not encrypted flash; trusted LAN only. |
| Resource/timing | Compile-time DRAM fits. BLE host uses core 0 by NimBLE default, settings server its own 8 KB task on core 1. No web handling in BLE callbacks. Static counters are atomic; config snapshots use a short critical section. Runtime heap, stack and radio timing cannot be confirmed without the device. Serial `wifi off`/`wifi on` provides an RF baseline. |

## Deliberate limitations

- **NimBLE-Arduino 2.3.7's `onWrite` returns void.** It does not expose an application ATT-error return path for FTMS's “procedure already in progress” (0x80) and “CCCD improperly configured” (0xFD) cases. This implementation disconnects an unsubscribed or pipelining controller and forwards no offending command. It does not claim formal FTMS conformance for malformed clients; proper ATT errors would need a lower-level GATT service or a separately reviewed NimBLE change. A correctly subscribed Zwift client issuing one procedure at a time follows the supported flow.
- One outstanding procedure, 20-byte command/reply bounds and a 30-second local transaction watchdog. Timeout/write failure closes the links, not retries a physical command. A failure after the trainer acted cannot be safely replayed.
- Both links fixed at MTU 23 trades throughput for an unambiguous fragment size. Actual Neo firmware must correctly implement FTMS multi-notification records; the ride test verifies this. Oversize packets are counted and links reset, never arbitrarily split.
- Trainer-name changes and trainer firmware/profile changes require reboot. The optional GATT layout is built once per boot. This is a proxy for one selected trainer, not a hot-swappable arbitrary FTMS hub.
- Crest interpolation only advances on incoming grade commands, so sparse commands make a stepped ramp. It sends no autonomous Control Point traffic that would interfere with Zwift/ERG.
- Soft-AP + BLE connected coexistence is marked unstable by Espressif. Prefer STA for normal riding; keep the AP for setup. If on-board timing/heap measurements fail, use Wi-Fi-off riding and investigate; an Android/desktop BLE settings characteristic is an alternative, not an iOS Safari solution and not implemented in this PR.

## Sources checked

- [Bluetooth SIG FTMS 1.0 and mandatory errata](https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/), especially service advertising, 4.9/4.10, 4.16–4.19. [Official specification PDF](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=423422).
- [Garmin trainer protocol matrix](https://developer.garmin.com/tacx-trainer/) and [Neo 3M connection guidance](https://support.garmin.com/id-ID/?faq=gIDYGIqH5N69ClkahxuigA).
- [NimBLE 1.x to 2.x migration](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.7/docs/1.x_to2.x_migration_guide.md); actual 2.3.7 headers/implementation checked for scan units, `subscribe`, `indicate`, acknowledgement status and callback signatures.
- [Espressif ESP32 RF coexistence](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/coexist.html). Supported coexistence is not a measurement of this firmware on this board.
