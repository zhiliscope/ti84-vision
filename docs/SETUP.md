# Reproducible macOS build and installation

Run commands from the repository root unless a step says otherwise. Nothing below automatically installs software on the calculator or flashes connected hardware. Choose ARM vs Intel tools based on `uname -m`.

## 1. Compiler prerequisites

Install Apple's command line developer tools if needed (`xcode-select --install`). Python 3, git and make must be available. Use a directory without spaces for tool installations. The project supports spaces through quoted paths, but avoiding them simplifies upstream tooling.

```sh
mkdir -p .tools
python3 -m venv .tools/build-env
.tools/build-env/bin/pip install cmake ninja
export PATH="$PWD/.tools/build-env/bin:$PATH"
```

## 2. CEdev calculator toolchain

Download **v15.0**, matching macOS ARM or Intel, from the [official release](https://github.com/CE-Programming/toolchain/releases/tag/v15.0). Open the DMG and copy its `CEdev` directory into `.tools/CEdev`, or another path you choose. The DMG is a toolchain distribution, not a calculator OS update.

```sh
export PATH="$PWD/.tools/CEdev/bin:$PATH"
cedev-config --makefile
make -C calculator
```

Output: `calculator/bin/TIVISION.8xp`. Transfer matching CE runtime libraries, including USB and serial dependencies, from the official [CE libraries release](https://github.com/CE-Programming/libraries/releases/tag/v15.0) (`clibs.8xg` or separate variables). Use the runtime corresponding to this toolchain; a Missing Library/Version error must be resolved before proceeding. GraphX, usbdrvce and srldrvce are used by this program. A `-I` unused-argument LTO warning and upstream GNU-stack linker warnings may occur in v15.0; see build evidence.

For OS **5.8.0.0022**, follow the maintainer's [arTIfiCE instructions for <=5.8.4](https://yvantt.github.io/arTIfiCE/) using **v2.1**, not v3. Back up calculator variables first. Transfer the official v2.1 `.8xp` with [TI Connect CE](https://education.ti.com/en/products/computer-software/ti-connect-ce-sw), launch it as directed, and use its shell to launch native programs. Transfer `TIVISION.8xp` and the libraries. The ordinary stock PRGM/Python menu does not by itself provide native execution on this OS. No Cesium install, OS update, downgrade, or Cabri Jr. dependency is assumed for the v2.1 route. Test a CEdev hello-world first if native execution is not already working.

TI ↔ Mac program transfer uses the normal calculator USB data cable, before the ESP experiment. Disconnect it before attaching the calculator host adapter. The TI app displays USB status and an exit hint; CLEAR returns to the calculator. Settings and the last answer disappear when the app exits.

## 3. ESP-IDF tools

This project deliberately pins the documented v5.4.2 baseline and TinyUSB 1.x APIs. New TinyUSB major versions have API differences; do not replace the pins with `latest`.

```sh
git clone --branch v5.4.2 --depth 1 --recursive https://github.com/espressif/esp-idf.git .tools/esp-idf
export IDF_TOOLS_PATH="$PWD/.tools/idf-tools"
.tools/esp-idf/install.sh esp32s3
```

The current component manager may inspect parent processes, which restricted build environments can deny. This session used official **idf-component-manager==2.2.1** in the IDF Python environment to avoid that dependency; a normal Terminal may not need this pin. For reproducing the session's dependency environment, after install:

```sh
"$IDF_TOOLS_PATH/python_env/idf5.4_py3.9_env/bin/pip" install 'idf-component-manager==2.2.1'
```

The Python environment name reflects your Python version; use the directory actually created under `$IDF_TOOLS_PATH/python_env`, rather than assuming `py3.9`. Keep the exported variables for every new shell:

```sh
export IDF_TOOLS_PATH="$PWD/.tools/idf-tools"
export IDF_COMPONENT_CACHE_PATH="$PWD/.tools/component-cache"
export PATH="$PWD/.tools/build-env/bin:$PATH"
. .tools/esp-idf/export.sh
cd firmware
idf.py set-target esp32s3
idf.py build
```

The component manager downloads the exact manifest dependencies and creates `dependencies.lock`. Commit that lock when maintaining the repository. First-time IDF setup may fetch many submodules; allow disk space and network access. If `set-target` refuses to clean an incomplete build directory, remove only that generated build directory and repeat; do not remove source or local config files.

## 4. Flash through CH343, with native OTG unplugged

Identify the UART port with `python -m serial.tools.list_ports -v` or compare `/dev/cu.*` with the board unplugged and attached. CH343 commonly appears as `cu.wchusbserial...` or `cu.usbserial...`; names vary. The native application CDC port is a different interface and must not be used here. Do not identify a port solely by its pathname.

```sh
idf.py -p /dev/cu.YOUR_CH343_PORT flash monitor
```

If auto reset fails, hold BOOT, tap RESET, release BOOT, then flash through CH343. Exit monitor with Ctrl-]. If macOS needs a CH343 driver, use WCH's official driver for your macOS version. Do not disable macOS protections or erase all flash as a routine fix.

The default firmware logs **USB disabled: verify power/adapter** and returns. This is intentional. After completing `HARDWARE.md`, configure:

```sh
idf.py menuconfig
```

Under “TI Vision hardware gates”, acknowledge the verified supply/adapter and select either the established bus-power path or isolated self-power with an actual VBUS-sense GPIO. Leave camera, milestone and network off for PING. Build/flash again. Disconnect UART power before any native-port experiment unless isolation is already established.

## 5. Host CDC test tools

From repository root:

```sh
python3 -m venv .venv
.venv/bin/pip install -r host/requirements.txt
.venv/bin/python -m serial.tools.list_ports -v
.venv/bin/python host/terminal.py /dev/cu.YOUR_NATIVE_CDC_PORT ping --count 100
.venv/bin/python host/terminal.py /dev/cu.YOUR_NATIVE_CDC_PORT status
.venv/bin/python host/terminal.py /dev/cu.YOUR_NATIVE_CDC_PORT corruption
```

Only native USB connected to the Mac for this test; calculator disconnected and CH343 unplugged until power arrangement is confirmed. This test proves only the Mac/ESP link. Then follow the calculator test in `TESTING.md`.

## 6. Enable camera / network after USB hardware passes

Copy `firmware/config/board_pins.example.h` to ignored `board_pins.h`; enter schematic/continuity-verified nets, document the evidence, and change `TV_BOARD_PINS_VERIFIED` to 1. The research reference is never selected automatically. Ensure the PSRAM boot log reports the expected 8 MB; PING tolerates missing PSRAM, camera rejects it.

Use menuconfig to acknowledge the real USB milestone and enable camera. Build, flash via UART with native unplugged, disconnect UART, then test native USB again. Only after camera succeeds, copy `local_config.example.h` to ignored `local_config.h`, enter Wi-Fi and API settings, enable network, and rebuild. Do not share firmware or build directories containing credentials. The calculator only sends prompts and receives status/text.

## Compile every module without claiming a hardware milestone

For API/type/link checking only, enable `TV_COMPILE_CHECK` in a **separate** build configuration. This compiles the optional code but unconditionally prevents USB, camera and network operation. It does not set any hardware-passed flag. Never use its binary for a hardware test. The compile fixture uses an unverified manufacturer-reference map (verification stays 0), non-secret dummy strings and an unrouteable `.invalid` endpoint. This retains the full camera/TLS call graph for link checking without real credentials or a claimed board match.

From `firmware` with the IDF environment active:

```sh
idf.py -B build-check -DIDF_TARGET=esp32s3 -DSDKCONFIG=sdkconfig.check -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.compilecheck.defaults" build
```

This uses a separate sdkconfig so compile-check mode cannot silently become your normal flashing build. Do not flash `build-check`. See validation evidence for the actual local results.
