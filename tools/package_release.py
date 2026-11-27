"""Package firmware from a clean build without local password overrides."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

FQBN = "esp32:esp32:esp32:PartitionScheme=min_spiffs"
CORE_VERSION = "3.3.12"
CLI_VERSION = "1.5.1"


def release_version(root):
    sketch = Path(root) / "HyundaiAudisonBridge"
    if (sketch / "LocalSecrets.h").exists():
        raise ValueError("Public release builds must not contain LocalSecrets.h")
    config = (sketch / "DebugConfig.h").read_text(encoding="utf-8")
    version = re.search(r'constexpr char FIRMWARE_VERSION\[\] = "(\d+\.\d+\.\d+-wifi)";', config)
    if not version:
        raise ValueError("Expected a numeric firmware version ending in -wifi")
    definitions = re.findall(r'^\s*#define AUDISON_BRIDGE_PASSWORD[ \t]+([^\r\n]+)', config, re.MULTILINE)
    if definitions != ['""']:
        raise ValueError("Public builds require an empty default password")
    if 'constexpr char PASSWORD[] = AUDISON_BRIDGE_PASSWORD;' not in config:
        raise ValueError("Public builds must use the default password configuration")
    return version.group(1)


def package_release(root, build, output, commit):
    root, build, output = Path(root), Path(build), Path(output)
    version = release_version(root)
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full Git commit SHA")
    application = build / "HyundaiAudisonBridge.ino.bin"
    merged = build / "HyundaiAudisonBridge.ino.merged.bin"
    app_data, merged_data = application.read_bytes(), merged.read_bytes()
    if not 24 <= len(app_data) <= 1966080 or app_data[0] != 0xE9:
        raise ValueError("Application is not an ESP image that fits the OTA slot")
    if version.encode() + b"\0" not in app_data:
        raise ValueError("Application does not contain the declared firmware version")
    if len(merged_data) != 4194304 or merged_data[0x10000:0x10000 + len(app_data)] != app_data:
        raise ValueError("Expected a 4 MB merged image containing this application at 0x10000")
    if output.exists() and any(output.iterdir()):
        raise ValueError("Release output directory must be empty")
    output.mkdir(parents=True, exist_ok=True)
    prefix = f"Audison-Bridge-{version}-esp32"
    ota_name, usb_name = prefix + "-ota.bin", prefix + "-usb-merged.bin"
    shutil.copyfile(application, output / ota_name)
    shutil.copyfile(merged, output / usb_name)
    info = {
        "version": version, "commit": commit, "fqbn": FQBN,
        "esp32ArduinoCore": CORE_VERSION, "arduinoCli": CLI_VERSION,
        "flashBytes": 4194304, "otaApplication": ota_name,
        "usbMergedImage": usb_name, "usbFlashAddress": "0x0",
        "credentials": "No private header; uses device NVS password or generates one",
    }
    (output / "build-info.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    instructions = f"""Audison Bridge {version}
Commit: {commit}
Target: classic ESP32 / ESP-WROOM-32, 4 MB flash, Minimal SPIFFS with OTA.

NORMAL WIFI UPDATE
Choose {ota_name} in Firmware & restart on the device dashboard.
Park, keep power connected, and remain on the ESP32 Wi-Fi until restart.
Do not select the USB merged image in the dashboard.

FIRST USB INSTALLATION / RECOVERY
Use {usb_name} with an ESP32 flashing tool at address 0x0.
This full 4 MB image overwrites saved device settings, including NVS passwords.
Use the OTA image for routine updates to preserve settings.

PASSWORDS
Public release builds contain no personal LocalSecrets.h password.
OTA retains an existing NVS password, or generates one if none is saved.
A password previously supplied only by a private header may therefore change.
Read the active Wi-Fi/dashboard password over USB serial at 115200 baud.
Dashboard username: admin. Default address: http://192.168.4.1

VERIFY DOWNLOADS
SHA256SUMS.txt lists hashes of the two images and these supporting files.
These are build artifacts; compilation does not establish physical validation.
"""
    (output / "FLASHING.txt").write_text(instructions, encoding="utf-8")
    checksums = []
    for path in sorted(output.iterdir()):
        checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n")
    (output / "SHA256SUMS.txt").write_text("".join(checksums), encoding="utf-8")
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--version-only", action="store_true")
    args = parser.parse_args()
    try:
        if args.version_only:
            print(release_version(args.root))
        else:
            if not args.build_dir or not args.output_dir or not args.commit:
                parser.error("--build-dir, --output-dir, and --commit are required")
            version = package_release(args.root, args.build_dir, args.output_dir, args.commit)
            print(f"Packaged public firmware {version} in {args.output_dir}")
    except (ValueError, OSError) as error:
        parser.exit(1, f"Release packaging failed: {error}\n")


if __name__ == "__main__":
    main()
