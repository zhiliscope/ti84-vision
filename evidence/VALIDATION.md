# Actual validation evidence

Date: 2026-11-05. Host: macOS arm64. No ESP flash, calculator installation, physical USB transfer, camera initialization/capture, Wi-Fi association or paid/live API request was performed. Hardware remains unverified.

## Results

- **PASS — calculator compile/link:** CEdev v15.0 produced `calculator/bin/TIVISION.8xp`, 16829 bytes. Source warnings were fixed. Remaining warnings are the upstream toolchain's LTO unused include flag and GNU-stack metadata notices; see calculator-build.txt. Native execution on the user's OS/unit is still pending.
- **PASS — ESP base compile/link:** ESP-IDF v5.4.2, target ESP32-S3, 16 MB flash, octal PSRAM settings, TinyUSB CDC. App image 261616 bytes. Camera/network off; POWER_APPROVED off; self-power off. A volatile false operation gate keeps the USB call graph linked but prevents operation. No hardware approval is implied by compiling.
- **PASS — all-module compile/link:** same IDF/target, `TV_COMPILE_CHECK=y`, SELF_POWERED=y with GPIO21 as a **compile-only placeholder**, POWER_APPROVED off and no USB milestone flag. App image 1111040 bytes. The compile fixture contains dummy credentials and an `.invalid` URL; its unselected manufacturer-reference pins retain verification=0. Operation gate remains false. `linked-symbols.txt` confirms actual camera, HTTPS, Wi-Fi and USB functions in the ELF. This variant must not be flashed as a hardware test.
- **PASS — C protocol validation:** 266,787 fragmentation/single-bit corruption cases; known CRC vector and exact PING frame; random/noisy stream bounds. Built with `-Wall -Wextra -Werror -fsanitize=address,undefined`. No sanitizer diagnostics.
- **PASS — three Python tests:** C/Python CRC differential checks, golden frame, stream/recovery. These run the actual desktop-compiled C CRC and host parser; they are not hardware emulation.

## Tooling and reproducibility

Exact SDK/toolchain source commits and binary hashes are in `tool-versions.json`; exact ESP managed package versions/hashes are in `firmware/dependencies.lock`. Resolved dependencies include esp_tinyusb 1.7.6~1, TinyUSB 0.21.0~3, esp32-camera 2.0.16 and esp_jpeg 1.3.2. Both firmware variants built without application/compiler warnings. Main task stack is 8192 bytes; worker stack is 12288 bytes. CMake emits an upstream deprecation warning on initial configuration with the installed recent CMake.

macOS disk-image mounting was unavailable in the restricted build environment; the official CEdev ARM DMG was extracted with 7-Zip into the workspace instead. The current IDF component manager attempted process enumeration denied by the sandbox; using official manager 2.2.1 avoided that tooling dependency. IDF tools, package caches and intermediates stayed under `work/`. No global user settings were modified.

The baseline used an external build directory with `IDF_TARGET=esp32s3`, the project's sdkconfig.defaults and a separate SDKCONFIG. The all-module build added settings identical to `firmware/sdkconfig.compilecheck.defaults`. See SETUP.md for commands that recreate these modes in a fresh checkout. Hashes record these particular builds; absolute paths/tool package changes may affect binary identity, so this is not a bit-for-bit reproducibility claim.

## Hardware acceptance — all pending

1. OS 5.8.0.0022 native launch and matching calculator libraries.
2. Mini adapter ID/role and data continuity.
3. Safe host current/VBUS and board supply/isolation arrangement.
4. Mac↔ESP native CDC and real TI↔ESP CDC PING/PONG, timeout and reconnect tests.
5. Verified camera pin map, expected PSRAM, actual OV2640 JPEG content/freshness.
6. Wi-Fi, time/TLS, provider authentication, real vision output and injected failure/oversize behavior.
7. Hardware UI wrapping/scrolling/settings, long-answer reconstruction and stability under load.

See docs/TESTING.md for procedures. Do not record a physical prototype as successful until these have actual results.
