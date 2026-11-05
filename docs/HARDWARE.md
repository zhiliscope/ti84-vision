# USB connections, power checks and stop conditions

**Do not power/connect CH343 USB and native USB simultaneously until the board's power arrangement is confirmed.** No hardware connection was made in this session. Do not enable an unverified camera pin configuration.

## Logical data connection (not permission to energize it)

```text
TI Mini-AB port
  | Mini host/OTG adapter: Mini plug -> USB-A female
  | USB-A male -> USB-C male DATA cable
ESP native USB-OTG Type-C (ESP USB D-/D+, not CH343)

Mac USB -> CH343 Type-C -> UART0 flashing/debug
          [unplug before native link until isolation is verified]
```

Native SoC USB nets are GPIO19 D− / GPIO20 D+. These are **SoC facts**, not a claim about a connector's PCB routing. Keep the SD slot unused initially. It adds no required function and can introduce pin/power conflicts.

## A. Checks with everything disconnected and unpowered

1. Establish the Mini connector contact numbering from a connector drawing. Check adapter ID pin 4 to GND pin 5 is a low-resistance host/A connection (original Mini-A spec <10 Ω). Floating ID does not establish host role. Check VBUS, ground, D+ and D− continuity through the adapter/cable, and absence of shorts between those nets. Never probe tiny powered connector contacts where a slipped probe can bridge pins.
2. Confirm the native port data nets lead to SoC GPIO19/20, and that the CH343 port is a separate UART bridge. Use PCB net labels/schematic or careful unpowered continuity checks; never infer from left/right connector position.
3. Determine how **both** USB VBUS rails connect to the board 5 V rail and regulator. Look for Schottky/ideal-diode ORing and any upstream output/backfeed path. A continuity beep alone cannot establish diode direction, powered behavior or isolation. If no safe topology can be established, stop the calculator power experiment; software development and PC single-port tests can continue.

## B. Host role and no-load VBUS

After verifying the adapter and installing the calculator app, attach the **adapter alone**. Launch TIVISION, select Connection Status (4). `usb_GetRole()` must lack `USB_ROLE_DEVICE` (0x10) and `USB_ROLE_B` (0x20). Expected host/A bits are zero. Record the raw role. Use an appropriate breakout/meter to check approximately 5 V from host VBUS to ground without attaching the ESP. A source-only no-load voltage says nothing about available current.

Do not connect the ESP just because the host role is correct. Establish its available host current and the board's load/inrush first. The prototype's USB configuration declares 100 mA; that descriptor is neither a current limiter nor proof the actual board consumes <=100 mA. The calculator is not assumed to offer PC-like USB power. Wi-Fi/camera peaks in particular require a separate measured budget.

## C. Two conditional power designs

**Bus powered:** native host VBUS supplies the board. CH343 and every other external power source remain unplugged. Use this only if verified host budget, ESP load/inrush and voltage at the board are acceptable. Camera/Wi-Fi remain off for first PING. Full camera/network operation needs the load budget rechecked. Set `TV_SELF_POWERED=n`.

**Self powered:** board uses an adequate separate supply (possibly through CH343 **only after** verified isolation). Prevent that supply/board rail from driving the calculator's VBUS. Share ground and the USB data pair. Route the calculator's actual VBUS to a verified divider/comparator and safe free GPIO; native host VBUS remains meaningful even when it does not power the board. Configure `TV_SELF_POWERED=y` and that GPIO. The firmware does not create missing diodes, isolation or a voltage divider.

Use the [Espressif self-powered requirements](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/peripherals/usb_device.html): sense must become low within 3 ms after unplug and have the documented valid high threshold. ESP GPIOs are not 5 V tolerant. Do not pick divider resistor values from guesswork; design against input thresholds and discharge timing and verify them. Merely cutting the red wire, declaring self power in a descriptor, or setting a GPIO number is insufficient.

A compliant externally powered hub is a possible fallback only after confirming its isolation, TI hub support and actual current arrangement. No new hardware purchase is assumed; first use the available single-port PC test and unpowered checks.

## D. Camera board research

[Freenove's manufacturer camera example](https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board/tree/3563960643987e327a9213c90566fca1e8c5ab67/C/Sketches/Sketch_07.1_CameraWebServer) selects the ESP32S3_EYE pin configuration. Its assignments are isolated in `firmware/config/reference_freenove_UNVERIFIED.h`, marked unverified for this board and never included by default. Similar product labels and matching memory do not verify those nets. The [Waveshare ESP32-S3-CAM family](https://docs.waveshare.com/ESP32-S3-CAM-OVxxxx) is another camera design with different features/revisions; it is not established as the user's dual-port CH343 board, and its pins are not selected.

The active example file deliberately uses invalid required pins. Verify D0..D7 ordering, SCCB SDA/SCL, XCLK, VSYNC/HREF/PCLK, camera supply and reset/powerdown wiring. The driver conservatively rejects USB, N16R8 memory, UART0 and strapping pins and duplicate nets. That check catches conflicts, **not** a wrong-but-plausible mapping, reversed ribbon cable, incompatible connector voltage or incorrect sensor. Schematic/continuity verification remains necessary. Confirm sensor ID is OV2640 during bring-up.

Stop on unexpected VBUS backfeed, brownout, voltage collapse, USB overcurrent or suspicious heating. Record the result before changing software. Do not defeat protection or replace a fuse/current limit to force operation.
