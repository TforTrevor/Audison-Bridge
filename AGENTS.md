# Audison Bridge project context

This repository contains a classic ESP32 Arduino firmware bridge from Hyundai
Elantra M-CAN volume/mute messages to Audison C8.14 RS232 commands, plus an
offline Wi-Fi diagnostics dashboard hosted by that same ESP32.

## Current state and next work

- The user has installed diagnostics on an ESP-WROOM-32. A physical cold-boot
  log shows mute ON followed by decoded volume changes remaining at -60 dB.
- Volume level changes now clear mute per the user's confirmed head-unit
  behavior. Repeated volume/mute broadcasts do not clear/reassert mute;
  the first volume frame establishes the baseline. Physical validation of
  this change and the remaining dashboard/OTA bench checks are still required.
- The updated ESP32 build (core 3.3.12, min_spiffs) and native regression suite
  pass. Native tests were run with a portable Zig C++ compiler in ignored
  .build/test-tools because this computer lacks the default VS Build Tools.
- Read README.md for setup, API endpoints, maintenance behavior, and limitations.

## Source and build

- Keep every .ino/.cpp/.h file under HyundaiAudisonBridge together as one sketch.
- Supported build verified here: Espressif ESP32 Arduino core 3.3.12;
  `esp32:esp32:esp32:PartitionScheme=min_spiffs`; classic ESP32 with 4 MB flash.
  Confirm physical hardware matches before flashing.
- Edit dashboard/index.html, then run `python tools/embed_dashboard.py`.
  HyundaiAudisonBridge/Dashboard.h is generated and tracked.
- Build on Windows with `tools/build_firmware.ps1`. It supports ArduinoCli,
  ArduinoData, and Fqbn overrides. Outputs go to ignored dist/ and .build/.
- Check the dashboard with `node --test tests/dashboard.test.mjs` and
  `python tools/embed_dashboard.py --check`.
- `tools/test_native.cmd` uses Visual Studio 2022 C++ Build Tools to test the
  actual sketch logic and upload lifecycle with simulated hardware adapters.
- `python tools/preview_dashboard.py` serves a simulated device at
  http://127.0.0.1:8765. Do not represent its readings as real hardware results.

## Existing behavior to preserve

- Car mode is CAN listen-only at 100 kbps; CAN TX/RX pins are 26/25 and UART
  TX/RX pins are 23/22. Serial and Audison UART both use 115200 baud.
- Head-unit levels 0..44 map linearly to -60..+6 dB. Ignore level 45.
- A change between known valid volume levels clears mute. Repeated levels and
  ignored/invalid frames do not. Track the received mute state separately so
  repeated ON broadcasts do not reassert mute; a fresh OFF -> ON does.
  Keep the 50 ms minimum command interval.
- Audison set-volume commands have no response; transmitted commands and UART
  loopback bytes are not acknowledgements of the DSP's state.
- Diagnostics must start even if CAN initialization fails.
- Log history is current-boot RAM only: 256 retained lines, with download.
- Keep HTTP transmission outside the log mutex. Preserve authentication and
  action-token checks on maintenance endpoints.
- OTA is a parked maintenance operation. RS232 output pauses during upload;
  failed uploads resume the latest requested state. Activate an image only
  after the entire authorized upload request has completed and been verified.

## Working across computers

- Repository: https://github.com/TforTrevor/Audison-Bridge
- Inspect Git state before editing or pulling. Preserve existing user changes.
- Before every new commit, increment DebugConfig::FIRMWARE_VERSION in
  HyundaiAudisonBridge/DebugConfig.h, including documentation-only commits.
  Default to increasing the patch number and preserving the -wifi suffix;
  use a larger version change only when requested. Compare with HEAD and bump
  once per commit, not per edit or build. Do not rewrite existing history.
  Rebuild firmware after a version bump when providing an updated binary.
- Keep build caches, firmware binaries, logs, credentials, and transfer ZIPs out
  of commits. Optional HyundaiAudisonBridge/LocalSecrets.h is ignored by Git;
  custom passwords belong there as AUDISON_BRIDGE_PASSWORD, never in tracked
  DebugConfig.h. The tracked LocalSecrets.example.h contains an empty value.
  Without a local override the device generates its own persistent password.
  Never print local secrets in tool output or include them in commits.
- Update this current-state note when physical validation or deployment occurs.
