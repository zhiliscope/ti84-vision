# Pre-publication validation

Date: 2026-11-05. Host: macOS arm64. Checks were rerun while preparing the source for private GitHub publication. Original test logs, source, build evidence and dependency lock are retained. `evidence/initial-delivery-SHA256SUMS` preserves the original local-delivery checksum list; its generated-artifact paths do not describe files committed to Git.

## Actual results

- **PASS: calculator compile/link** with CEdev v15.0. Ran `make -C calculator`, then forced compilation/linking with `make -B -C calculator` rather than relying only on an up-to-date result. The generated TIVISION.8xp is retained locally and excluded from Git. Upstream LTO unused-include and GNU-stack metadata warnings remain documented in `calculator-build.txt`.
- **PASS: ESP32-S3 base compile/link** with ESP-IDF v5.4.2: `idf.py -B <existing-base-build> build`. Power approval, camera and network remain off. See `esp-base-build.txt`.
- **PASS: all-module compile/link** with the separate compile-check configuration: `idf.py -B <existing-all-build> build`. The camera, Wi-Fi, HTTPS and USB call graph is compiled/linked, but the program deliberately refuses operation. See `esp-all-build.txt`.
- **PASS: automated tests** using `sh tools/test.sh`: 266,787 C fragmentation/corruption cases, CRC/golden-frame and noise/bounds checks under AddressSanitizer/UndefinedBehaviorSanitizer, plus all three Python cross-check/recovery tests. See `codec-tests.txt`.
- **PASS: source preservation**: all 24 original source/build/test files covered by the original manifest retain their SHA-256 hashes. Documentation and repository-hygiene files were improved; no application functionality was removed or changed.

The rerun reused pinned SDK/toolchain installations and existing external build directories; previously downloaded managed components were restored locally. Fresh setup commands are in README.md and docs/SETUP.md. Exact generated artifact sizes/hashes are in `artifacts.json`; binaries, objects, SDK downloads and build directories are excluded from Git and remain local. Original tool-version metadata and firmware/dependencies.lock are retained. No global user configuration was changed.

## Repository preparation and scan

The expanded README documents hardware, architecture, features, status, macOS installation/builds, calculator and firmware setup, protocol, hardware checklist and limitations. The .gitignore excludes compiled artifacts, downloaded SDK/components, caches, local config, credential files and keys. Original evidence logs remain available. Publication logs replace personal absolute paths with `<WORKSPACE>`. A user-timezone annotation was removed from the original validation prose without changing its test results.

The exact staged snapshot is checked by tools/check_repository.py for targeted credential patterns, nonempty credential/network definitions, personal home/contact data and generated/private files. Empty credential templates and exact compile-only dummy values in the dedicated fixture are reviewed exceptions. See `repository-audit.txt` for the final result. The scanner is a targeted check, not proof against every possible secret format; source/template/log contents were also inspected.

## Hardware results remain pending

No calculator installation, ESP flash, physical USB PING/PONG, camera initialization/capture, Wi-Fi association or live AI API request was performed. **Physical TI-to-ESP communication, actual camera GPIO mapping and USB power/VBUS compatibility remain unverified.** Passing software checks does not establish a working hardware prototype. Do not connect or power both board USB ports until the electrical arrangement is verified.
