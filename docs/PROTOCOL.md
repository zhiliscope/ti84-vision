# USB transport and PING/PONG proof of concept

CDC-ACM is a byte stream; USB packet or serial-read boundaries are not message boundaries. Native ESP port: one control interface **0**, CDC data interface **1**, notification IN 0x81 (8 bytes), bulk OUT 0x02 and IN 0x82 (64-byte full-speed packets). The application frame may span several packets. TI opens any supported serial interface, then issues standard CDC `SET_CONTROL_LINE_STATE` (0x21/0x22, wValue=3, wIndex=0) to assert DTR/RTS for **this defined layout**. `srldrvce` already sets line coding. 115200 is line-coding metadata; the USB wire remains full speed.

The provisional VID/PID 303A:4001 follows Espressif's example space for local experimentation; it is not a product allocation. Obtain permission/IDs before distribution as a USB product. Keep the initial descriptor simple; this is an interoperability experiment, not a certified USB product.

## Wire format, version 1

All multibyte integers are little endian. Maximum payload 240 bytes; maximum frame 250.

```text
offset  bytes  meaning
0       2      ASCII T V (54 56)
2       1      version = 1
3       1      command/response type
4       2      request sequence
6       2      payload length (0..240)
8       N      payload
8+N     2      CRC-16/CCITT-FALSE over offsets 2..7+N
```

CRC polynomial 0x1021, initial 0xFFFF, no reflection, xorout=0. Known vector `123456789` → 0x29B1. Magic is excluded from CRC. Response sequence echoes the request. Success type = request | 0x80; error type = 0xFF with an ASCII diagnostic. No secrets or raw HTTP error bodies go to the calculator.

Commands:

- **01 PING:** payload `PING`; response `PONG`. This is framed, not a raw `PING\n` terminal command. A serial monitor showing no response to unframed text is expected.
- **02 STATUS:** empty request; bounded ASCII readiness/module status. “enabled” indicates compilation/configuration, not tested camera hardware or a Wi-Fi connection.
- **03 CAPTURE:** empty request; final reply `CAPTURED <n> bytes`, or error. Worker retains a JPEG up to 256 KiB. A failed capture invalidates the old image. The prior generated ESP answer is cleared when a capture/ask starts.
- **04 ASK:** 1..240 bytes of ASCII question without NUL; process last captured JPEG over HTTPS; final reply `READY <n> bytes`, or error. No fake response when network, credentials or camera are unavailable.
- **05 RESULT:** request uint16 byte offset. Reply begins uint16 total length and uint16 echoed offset, then up to 236 ASCII bytes. Maximum total 8192. Offset equal to total returns an empty chunk; larger offsets error. Busy worker rejects retrieval, preventing concurrent writes. Client checks all bounds/offsets and stable total before appending.

- **06 JPEG (Mac diagnostic only):** request uint32 offset; reply uint32 total JPEG size, uint32 echoed offset and up to 232 binary bytes. Same idle/offset rules as RESULT, with a 256 KiB cap. The calculator never requests images.

## Deadlines, recovery and ownership

Use one outstanding application request at a time. There is no automatic retry of capture/ask: after a timeout the ESP may still be working or the provider may have processed the request. Send STATUS or wait before manually starting another job. Duplicate sequences are correlated but not deduplicated; do not retry a paid request expecting exactly-once semantics.

TI request deadlines: PING/STATUS/chunks 5 s, CAPTURE 15 s, ASK 180 s by default (Settings toggles 120/180). CLEAR cancels the local wait, not the camera or HTTP request. Short reads and partial writes are normal and handled. `srl_Open` and synchronous USB control requests are upstream library calls with their own behavior; application timing does not establish their worst-case bounds.

ESP USB polling and worker are separate tasks. PING/STATUS remain available to a diagnostic host while the worker is busy. One worker job and one completion are queued; additional capture/ask returns BUSY. The worker owns image/result production; USB only reads a result after completion is received. Disconnect/re-enumeration changes the session epoch; an old completion is not sent to a new session. USB app TX attempts are bounded to 2 seconds; loss becomes a client timeout. Serial buffers and stream parser reset on disconnect.

Both endpoints drop invalid versions, impossible lengths and bad CRCs, searching again for magic; invalid frames are silently dropped because an untrusted header cannot reliably identify a requester. Parser errors are counted. An incomplete frame expires after 2 s without bytes. A full CRC protects byte corruption; USB also has its own link-level checks. The parser is not an authentication/security protocol.

## Minimal experiment sequence

1. Build with camera/network/milestone off. Pass physical power gate before enabling CDC.
2. Native port connected only to Mac: identify CDC descriptors, assert DTR, send one encoded PING, match PONG and sequence, then run 100.
3. Send corrupt CRC; verify no response, then valid PING recovers. Send a partial header, wait >2 s, then PING; verify timeout recovery.
4. Disconnect the Mac; after the power/adapter gate, connect TI host to native ESP port. Record role, connect/enabled counts and serial-open result. Press 6 for one PING, 7 for 100 rounds. Unplug/reconnect 10 times. If it fails, inspect actual descriptors and class requests; do not rewrite the architecture around an assumed missing CDC driver.
5. Enable no optional hardware feature until the TI, not just the Mac, has passed.
