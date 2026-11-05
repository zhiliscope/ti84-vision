# Upstream software

The project's MIT license applies to its original source, not third-party tools, runtime libraries or their licenses. Toolchains/dependencies are downloaded separately. Generated binaries and objects are excluded from Git. Full original source and build commands for the application are included.

- [CE C/C++ Toolchain v15.0](https://github.com/CE-Programming/toolchain/tree/v15.0), including runtime/libc and USB/serial/GraphX interfaces: see its [LGPL-3.0 license](https://github.com/CE-Programming/toolchain/blob/v15.0/license) and component-specific notices. Complete application source and build commands are provided; locally generated .8xp and relink objects are retained outside version control. Shared calculator libraries should be obtained from the official matching release.
- [ESP-IDF v5.4.2](https://github.com/espressif/esp-idf/tree/v5.4.2): Apache-2.0 core with component-specific notices.
- [esp32-camera v2.0.16](https://github.com/espressif/esp32-camera/tree/v2.0.16): Apache-2.0.
- [esp_tinyusb](https://components.espressif.com/components/espressif/esp_tinyusb/versions/1.7.6~1) and [TinyUSB](https://github.com/hathach/tinyusb): their Apache/MIT notices apply; exact packages/hashes are in firmware/dependencies.lock.
- [cJSON](https://github.com/DaveGamble/cJSON), [Mbed TLS](https://github.com/Mbed-TLS/mbedtls), FreeRTOS and other ESP dependencies retain their own notices in upstream/managed components.
- [pyserial](https://github.com/pyserial/pyserial): BSD license, installed separately by the host requirements.
- The unselected Freenove reference header contains sourced hardware pin facts, with an exact source URL in the header; it is not a board compatibility claim.

No arTIfiCE, TI OS/application or CE shared-library download is bundled. Follow the linked maintainers' installation/release instructions.
