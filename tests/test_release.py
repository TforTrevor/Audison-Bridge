import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("package_release", Path(__file__).resolve().parents[1] / "tools/package_release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sketch = self.root / "HyundaiAudisonBridge"
        self.sketch.mkdir()
        self.config = self.sketch / "DebugConfig.h"
        self.config.write_text('constexpr char FIRMWARE_VERSION[] = "1.2.3-wifi";\n'
                               '#define AUDISON_BRIDGE_PASSWORD ""\n'
                               'constexpr char PASSWORD[] = AUDISON_BRIDGE_PASSWORD;\n', encoding="utf-8")
        self.build = self.root / "build"
        self.build.mkdir()
        self.output = self.root / "release"
        self.app = b"\xe9" + bytes(23) + b"1.2.3-wifi\0" + bytes(94)
        merged = bytearray(b"\xff" * 4194304)
        merged[0x10000:0x10000 + len(self.app)] = self.app
        (self.build / "HyundaiAudisonBridge.ino.bin").write_bytes(self.app)
        (self.build / "HyundaiAudisonBridge.ino.merged.bin").write_bytes(merged)

    def package(self):
        return release.package_release(self.root, self.build, self.output, "a" * 40)

    def test_assets_checksums_and_provenance(self):
        self.assertEqual(self.package(), "1.2.3-wifi")
        self.assertEqual(len(list(self.output.iterdir())), 5)
        for line in (self.output / "SHA256SUMS.txt").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest)
        info = json.loads((self.output / "build-info.json").read_text())
        self.assertEqual(info["commit"], "a" * 40)
        self.assertEqual((self.output / info["otaApplication"]).read_bytes(), self.app)
        self.assertEqual(info["usbFlashAddress"], "0x0")
        self.assertIn("overwrites saved device settings", (self.output / "FLASHING.txt").read_text())

    def test_private_header_is_rejected_even_when_ignored(self):
        (self.sketch / "LocalSecrets.h").write_text('// private local configuration', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "LocalSecrets"):
            self.package()
        self.assertFalse(self.output.exists())

    def test_nonempty_tracked_password_is_rejected(self):
        text = self.config.read_text().replace('#define AUDISON_BRIDGE_PASSWORD ""', '#define AUDISON_BRIDGE_PASSWORD "fixture-password"')
        self.config.write_text(text, encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "empty default"):
            self.package()

    def test_duplicate_password_definition_is_rejected(self):
        with self.config.open("a", encoding="utf-8") as output:
            output.write('#define AUDISON_BRIDGE_PASSWORD "fixture-password"\n')
        with self.assertRaisesRegex(ValueError, "empty default"):
            self.package()

    def test_stale_application_version_is_rejected(self):
        self.config.write_text(self.config.read_text().replace("1.2.3-wifi", "1.2.4-wifi"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "declared firmware version"):
            self.package()

    def test_version_cannot_inject_paths_or_shell(self):
        self.config.write_text(self.config.read_text().replace("1.2.3-wifi", "../../bad"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "version"):
            self.package()

    def test_mismatched_merged_image_is_rejected(self):
        (self.build / "HyundaiAudisonBridge.ino.merged.bin").write_bytes(bytes(4194304))
        with self.assertRaisesRegex(ValueError, "merged image"):
            self.package()

    def test_invalid_application_is_rejected(self):
        (self.build / "HyundaiAudisonBridge.ino.bin").write_bytes(b"not an ESP32 image")
        with self.assertRaisesRegex(ValueError, "Application"):
            self.package()

    def test_existing_output_is_not_reused(self):
        self.output.mkdir()
        (self.output / "private-old.bin").write_bytes(b"old")
        with self.assertRaisesRegex(ValueError, "empty"):
            self.package()

    def test_full_commit_id_is_required(self):
        with self.assertRaisesRegex(ValueError, "commit SHA"):
            release.package_release(self.root, self.build, self.output, "main")


if __name__ == "__main__":
    unittest.main()
