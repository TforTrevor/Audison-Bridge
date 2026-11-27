# Audison Bridge

ESP32 Hyundai Elantra M-CAN to Audison C8.14 RS232 volume bridge, with a
password-protected, offline Wi-Fi diagnostics dashboard.

## What it does

- Live application console, with timestamps, search, pause/resume, and follow.
- The latest **256 log lines from the current boot** stay in RAM, including
  startup messages from before a browser connects. Oldest lines are overwritten.
- Download the device's retained history, or download up to 2,000 lines captured
  by the open browser. History clears when the ESP32 reboots.
- Upload a compiled application `.bin`, verify it, then restart into that firmware.
- Restart the ESP32 from the dashboard.
- Live uptime, reset reason, memory, chip/CPU information, flash/application/OTA
  space, Wi-Fi clients, CAN state/activity/errors, volume/mute, RS232 counters,
  loop rate, and longest loop since boot.

The ESP32 hosts the dashboard itself. No internet, cloud service, extra board,
JavaScript libraries, or installed desktop application is required.

## Continue on another computer

Clone the repository, then add the cloned folder as a local project in Codex:

```powershell
git clone https://github.com/TforTrevor/Audison-Bridge.git
cd Audison-Bridge
```

Install Arduino IDE and Espressif ESP32 core **3.3.12** on each computer that
will build or flash firmware. Build caches and compiled binaries are not tracked;
rebuild locally with the instructions below.

Before starting work on a computer, run `git pull --ff-only`. After finishing,
commit the files you changed and run `git push` before switching computers.
GitHub carries the project files and commit history. Conversation handoff is
handled separately by Codex; the repository's `AGENTS.md` and this README provide
project context for a new chat.

Current handoff state: the user has installed diagnostics on an ESP-WROOM-32.
A cold-boot log captured mute ON with later volume changes still muted. The
firmware now clears mute when the volume level changes; this update still
requires physical validation. Complete the remaining dashboard and OTA bench
tests below before treating those features as hardware-verified.

## First installation over USB

1. Open `HyundaiAudisonBridge/HyundaiAudisonBridge.ino` in Arduino IDE. Keep all
   `.h` and `.cpp` files in that sketch folder together.
2. Select **ESP32 Dev Module**, with the **Espressif ESP32 Arduino core** installed.
   This project has been compiled with core **3.3.12**, targeting a classic ESP32
   and **4 MB flash**. Confirm that matches the physical board before flashing.
3. Choose **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** under Partition
   Scheme. This provides two OTA application slots. A “No OTA” scheme will not work.
   Install the partition scheme over USB; browser updates cannot change it.
4. Leave `CAN_LISTEN_ONLY = true` for the car. The CAN pins remain TX **26**, RX
   **25**; MAX3232 UART pins remain TX **23**, RX **22**. USB serial is **115200**.
5. Upload over USB, then open Serial Monitor at 115200 baud and reset the board.
   Record the Wi-Fi network and password printed at startup.
6. Join **Audison-Bridge-XXXX** on the laptop or phone. Stay connected if the OS
   reports that this network has no internet.
7. Open **http://192.168.4.1**. Log in as **admin**, using the same Wi-Fi password.
   **http://audison-bridge.local** is an optional mDNS address; the numeric address
   works without mDNS support.

By default a random 16-character password is generated once and saved to ESP32
NVS. It survives normal firmware updates and restarts. It is printed only to USB
serial, not the remote log history. This default keeps the password off your
computer and out of the source code. Erasing all flash also erases the saved
generated password.

To choose a personal password before building, copy
`HyundaiAudisonBridge/LocalSecrets.example.h` to `LocalSecrets.h` in the same
folder, then set `AUDISON_BRIDGE_PASSWORD` there to 8–63 ASCII characters.
`LocalSecrets.h` is ignored by Git; keep `DebugConfig.h` free of credentials.
Both Arduino IDE and the build script load the optional local header. A fresh
clone without it uses the generated device password. Transfer a custom local
header privately when moving to another computer, or use a password manager.
Builds with a custom password embed it in the firmware binary; keep those
artifacts private too. Git ignores prevent ordinary accidental additions, but
can be bypassed with `git add -f`; review staged changes before publishing.

## Wireless firmware updates

1. Build with the same board and partition settings. In Arduino IDE, use
   **Sketch → Export Compiled Binary**.
2. Download any logs you want to keep, then open **Firmware & restart**.
3. Select **HyundaiAudisonBridge.ino.bin**, the application image. Do not select
   `.merged.bin`, `.bootloader.bin`, `.partitions.bin`, or raw `.ino` source code.
4. Park, then choose **Upload & restart**. Keep the board powered and remain on
   its Wi-Fi. RS232 output pauses while firmware is written; CAN state continues
   to be decoded as scheduling permits. On failure, output resumes with the latest
   requested state. On success, the board restarts after two seconds.
5. Reconnect to its Wi-Fi if needed. The dashboard detects a new boot, refreshes
   its action token, and begins collecting the new boot's logs.

The updater enforces exact upload size, available OTA space, one image per
request, and Espressif's image validation before activating the other partition.
Invalid, incomplete, disconnected, or rejected requests do not activate an image.
This is image validation, not a signing/rollback system: use a trusted firmware
build for this board. USB remains the recovery path for firmware that boots but
does not function correctly. OTA writes flash and can stall both cores briefly;
it is a maintenance operation, not an uninterrupted real-time service.

## Build and checks

With Arduino IDE and ESP32 core installed on Windows:

```powershell
.\tools\build_firmware.ps1
```

This regenerates `Dashboard.h`, compiles the sketch, and writes binaries to
`dist/`. The OTA application is `dist/HyundaiAudisonBridge.ino.bin`. The default
FQBN is `esp32:esp32:esp32:PartitionScheme=min_spiffs`. The script accepts
`-ArduinoCli`, `-ArduinoData`, and `-Fqbn` overrides, and does not change IDE settings.

```powershell
node --test .\tests\dashboard.test.mjs
.\tools\test_native.cmd
python .\tools\embed_dashboard.py --check
```

The native test requires Visual Studio 2022 C++ Build Tools at its default path.
It compiles the actual bridge sketch against test hardware adapters and exercises
the volume mapping, mute sequences, invalid frames, 50 ms command timing and
timer rollover, OTA pause/resume, initialization failures, statistics, bounded
receive processing, log history pagination/eviction, and the actual firmware
upload lifecycle with a simulated flash updater. The JavaScript test executes
the production dashboard script and checks its connection, history, upload,
restart, and failure/recovery behavior.

## Dashboard development and local preview

Edit `dashboard/index.html`, then run `python tools/embed_dashboard.py` to update
the generated flash asset in the sketch. There is no separate filesystem upload.

```powershell
python .\tools\preview_dashboard.py
```

Open **http://127.0.0.1:8765**. This preview is explicitly labeled **simulated**.
It provides fake readings and a simulated reboot; it rejects firmware uploads
and never accesses hardware. Stop it with Ctrl+C.

## Architecture and limits

- CAN/RS232 initialize before the Wi-Fi task starts. The dashboard still starts
  if CAN initialization fails, so that failure can be inspected remotely.
- A separate FreeRTOS task on core 0 owns HTTP, Wi-Fi, and the updater. The
  Arduino loop keeps processing the existing bridge logic. HTTP transmission
  never holds the history lock. Normal dashboard polling is about once per second.
- Application `Console` messages go to both USB and RAM. Bootloader, ROM, and
  ESP-IDF internal messages are USB-only. Log text is bounded to 191 characters
  per entry; longer application lines are split across consecutive entries.
- Levels 0–44 map to −60 to +6 dB. A change between known valid volume levels
  clears mute, matching the head unit. The first volume frame establishes a
  baseline; repeated levels and invalid/ignored frames do not clear mute.
  Repeated mute ON broadcasts do not reassert mute after a volume change;
  a fresh OFF → ON transition does. The 50 ms minimum command gap and ignored
  level 45 are preserved. “Audison output” is the last
  command sent, not confirmed DSP state: set-volume commands have no response.
- WPA2 Wi-Fi and HTTP Basic authentication protect access. Reboot and upload
  also require a per-boot action token and a valid local host/origin. Keep the
  service on the local ESP32 network; it is HTTP, not an internet-facing service.
- Stats are measured values and counters, not CPU-load estimates. No RS232 RX
  bytes does not itself mean an error. Set `DEBUG_RS232_RX = true` to include
  incoming UART bytes in the application console.

## Device API

All endpoints require HTTP Basic authentication. The host must be `192.168.4.1`
or `audison-bridge.local` (optionally with `:80`). There is no cross-origin API.

| Endpoint | Method | Purpose |
| --- | --- | --- |
| `/` | GET | Offline dashboard |
| `/api/session` | GET | Boot ID and action token |
| `/api/stats` | GET | Live device and bridge statistics |
| `/api/logs?after=0` | GET | Up to 32 retained entries; use `next` for pagination |
| `/api/logs.txt` | GET | Download retained current-boot logs |
| `/api/reboot` | POST | Restart after two seconds |
| `/api/firmware` | POST | Multipart application image; restart on success |

POST requests need `X-Bridge-Token` from the session endpoint. Firmware requests
also need `X-Firmware-Size` containing the application's exact byte length.
Log responses include a boot ID and a missed-entry count so a client can handle
restarts and history overwritten while it was disconnected.

## Hardware validation still required

The build and host tests do not establish Wi-Fi range, runtime heap margin,
electrical behavior, or reliable flashing on the actual controller. Bench-test
initial USB installation, login, disconnected/reconnected logging, CAN volume
and mute, reboot, a valid OTA update, and a rejected/aborted upload before use
in the car. The volume-change unmute update still requires a physical test.

Espressif references: [OTA web update](https://docs.espressif.com/projects/arduino-esp32/en/latest/ota_web_update.html)
and [partition tables](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/partition_table.html).
