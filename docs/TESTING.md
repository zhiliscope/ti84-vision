# Acceptance tests and evidence log

Each result below is **pending** until run on the actual devices. Do not convert an upstream support claim or successful compilation into a hardware PASS. Record date, firmware/calculator build hash, exact OS, topology, raw status and observed result. Do not put keys, Wi-Fi passwords, private images or raw API bodies in the log.

## 0. Pure software

Run `sh tools/test.sh`: C codec under AddressSanitizer/UndefinedBehaviorSanitizer checks CRC known vector, every payload size, fragmentation boundaries, single-bit payload/CRC errors, noise and bounds. Python tests cross-check the actual compiled C CRC with Python's standard implementation and exercise the diagnostic client's stream parser. These are protocol unit tests, not physical USB tests.

Build `calculator` with CEdev v15 and both the normal and `TV_COMPILE_CHECK` ESP variants with ESP-IDF v5.4.2. Compile-check mode enables all module source for API/link validation but prevents device operation. A normal build with power gate off also must log its gate and expose no app CDC interface. Keep diagnostic builds separate from hardware-test builds.

## 1. Native execution and safe electrical setup

1. Back up calculator and launch official CEdev hello-world using OS-appropriate arTIfiCE. Record correct display and clean exit. Then launch TIVISION with no ESP attached.
2. Complete every power/adapter check in `HARDWARE.md`. With adapter alone, status must show host/A, and VBUS must be valid. Record the verified power source/budget/isolation path. **STOP** if unresolved; PC single-port testing and software work can continue.
3. Flash through CH343 with native USB unplugged. Confirm expected chip, 16 MB flash configuration, no boot loop, and UART console. Disconnect CH343 before native-port testing until isolation is established.

## 2. Mac ↔ native USB CDC (calculator disconnected)

1. Enable only the verified power gate; leave camera/network/milestone off. Connect native USB to Mac as the sole power/data connection in this test. Identify the new CDC port with `serial.tools.list_ports -v`; inspect USB descriptors using macOS System Information or `ioreg -p IOUSB -l`. Expect one CDC function, two interfaces and the documented endpoints. Hardware USB Serial/JTAG in boot mode is not this application.
2. Run `terminal.py PORT ping --count 100`: accept 100 matching PONGs without reset, timeout or CRC errors. Run `status`: camera and network must be disabled. Run `corruption`: corrupt frame produces silence; next valid PING passes.
3. In a small pyserial diagnostic, send only the first 8 bytes of a valid frame, wait 2.2 s, send complete PING. Accept recovery. Unplug/replug 10 times, reopening the OS serial device each time. Confirm PONG after each cycle.
4. With defaults, CAPTURE and ASK must return clear NOT_SUPPORTED errors, not success or synthetic data. Ask without a JPEG once network is enabled must report INVALID_STATE.

## 3. TI ↔ native USB CDC (Mac native link disconnected)

Use only the verified topology. Never attach two hosts to the native port or feed two unverified power sources. The CH343 debug port remains available by design but must remain disconnected until safe dual-port power is confirmed.

1. Launch TIVISION, attach host adapter/data cable and ESP native port. Select Status (4); record role, connect/enabled counts and `srl_Open` outcome. CDC open must succeed; it is not yet a round trip.
2. Press 6. Accept exactly `PONG: USB round trip passed once`. Press 7: accept 100/100. A Mac PASS does not replace this test.
3. Unplug while waiting; accept disconnect/error/timeout without corrupting calculator memory; CLEAR exits cleanly. Reconnect 10 times and repeat PING. If enumeration stalls inside the work-in-progress upstream stack, record that behavior as a failure; the outer timeout does not guarantee recovery from a blocking library call.
4. Leave device absent: no false connected or success state. Try an ID-floating non-host adapter only as a controlled role test if safe: expect device/B role and no host enumeration, not an application success.

Only after this milestone passes on the actual calculator may `TV_USB_MILESTONE_PASSED` be set for operational builds.

## 4. Camera

Verify exact camera nets and connector orientation; enter documented pins and `VERIFIED=1` in local board config. Confirm no GPIO19/20 conflict, memory pin reuse, console/sense overlap or duplicate pins. Enable camera, leave network off. Power requirements must now cover camera operation.

Run CAPTURE 20 times; accept valid sensor initialization and bounded nonzero JPEG size each time, no reset/leak/brownout, and responsive PING from a concurrent diagnostic host while the worker captures. The calculator itself permits one pending request; use the Mac for concurrent diagnostic requests. Verify OV2640 identification in the camera driver's UART initialization log, only with a safely established logging arrangement.

To verify actual pixels/focus without an AI call, use the Mac as sole native USB host, capture then retrieve the real JPEG:

```sh
.venv/bin/python host/terminal.py PORT capture
.venv/bin/python host/terminal.py PORT jpeg --output /tmp/tivision-capture.jpg
open /tmp/tivision-capture.jpg
```

The output file must not already exist. Check it decodes and matches the scene; repeat with a changed scene to confirm freshness. The calculator never requests this diagnostic command and still displays text only. No unauthenticated HTTP camera preview is exposed. JPEG marker checks alone do not prove correct pixels.

Try invalid/missing local pins: accept a configuration error before camera GPIO initialization. Missing PSRAM must reject capture. A failed new capture must invalidate the prior JPEG so Ask cannot silently analyze the old scene. A wrong-but-valid pin map is unsafe; do not scan random pins until an image appears.

## 5. Wi-Fi / HTTPS / AI (opt-in live test)

Provide credentials privately in local config and choose endpoint/model. Build/flash, then establish the verified topology again. Use a non-sensitive, recognizable scene and a concise prompt. CAPTURE → ASK → RESULT must return scene-specific actual provider text. Record status/result length and latency, not secrets. Verify PING remains responsive during the network wait with a concurrent Mac diagnostic host. No provider call is made by the software unit tests.

Exercise wrong SSID/password, absent AP, blocked DNS/NTP, unreachable API, untrusted TLS cert/wrong hostname, invalid API key/model, HTTP 429 and 5xx. Use a deliberately configured Messages-compatible HTTPS test endpoint or account test facilities for injected responses; do not forge the production host's certificate or turn off verification. Each must error/timeout without producing an AI success or hanging the USB task. Restore endpoint/key after testing. Check that non-200 response bodies never reach TI.

Send JSON >32 KiB and text >8192 bytes from your controlled test endpoint: accept explicit overflow errors, no memory growth and recovery on a later request. Test an empty text array, malformed JSON, chunked response, partial/truncated body and no response. Do not use paid repeated requests to manufacture a rate limit. Confirm no automatic retry of ASK after any timeout.

## 6. UI and long text

With a controlled endpoint returning text across the 236-byte retrieval boundary and several 40-column lines, check exact byte-offset reconstruction, explicit newlines, long words, ASCII replacement, empty response, UP/DOWN line scroll and LEFT/RIGHT pages. View an 8192-byte response. Edits stay bounded at 240 bytes. Settings question entry uses UP/DOWN character selection, ENTER append, DEL erase, 2ND save and CLEAR cancel; USB must stay serviced while editing.

## Troubleshooting

- **No native launch / syntax error:** verify OS and arTIfiCE version, launch route and runtime libraries first; do not diagnose this as a USB bug.
- **Role includes 0x10/0x20:** inspect adapter ID configuration; do not force a power source into a device-role port.
- **No connect/enabled event:** check safe VBUS, true data cable, native connector routing, boot mode and device descriptors. CH343 is the wrong port.
- **Serial open NOT_SUPPORTED:** inspect actual CDC data interface/endpoints and current CE runtime versions; do not assume CH343 should work with this CDC driver.
- **CDC open but no PONG:** assert DTR/RTS on control interface 0, inspect framed bytes/sequence/CRC and TinyUSB buffers. Raw terminal `PING` is not a protocol frame.
- **USB reboots under camera/Wi-Fi:** investigate supply/inrush/PSRAM first; keep brownout protection enabled.
- **Sensor not found:** stop camera integration and check verified pin map, SCCB, connector seating/power/orientation. Do not adopt a clone pin map on appearance alone.
- **TLS/API errors:** verify AP/IP, DNS, valid time, CA chain, trusted HTTPS endpoint, API version, credentials and model access. Keep logs sanitized.
- **CLEAR after ASK timeout:** local cancellation does not stop the ESP/provider. STATUS can show BUSY. Wait for completion or deliberately restart; avoid immediate paid retries.
