#!/usr/bin/env python3
"""Exercise animation installation with synthetic ERAN data; no game assets needed."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "install_eldenring_animations", Path(__file__).resolve().parents[1] / "scripts/install_eldenring_animations.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def encoded_name(value):
    data = value.encode("utf-8")
    return bytes([len(data)]) + data


class AnimationInstallation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.header = self.root / "imported_timings.hpp"
        self.header.write_text('result.mAnimation = "a001_003000";\nreturn twoHanded ? 10u : 0u;', encoding="utf-8")
        self.clips = installer.required_clips(self.header)
        self.source = self.root / "player_anims.bin"
        self.package = self.root / "пакет OpenMW"
        self.destination = self.package / installer.DESTINATION
        self.backups = self.root / "backups"
        self.config = self.package / "elden-profile/openmw.cfg"
        self.config.parent.mkdir(parents=True)
        self.config.write_bytes(b"# user's existing profile\ncontent=Morrowind.esm\n")
        self.source.write_bytes(self.bank())

    def bank(self, clips=None, joints=None, value=0.0):
        joints = sorted(installer.POSITION_JOINTS if joints is None else joints)
        oriented = sorted(installer.ORIENTED_JOINTS)
        clips = sorted(self.clips if clips is None else clips)
        data = b"ERAN" + struct.pack("<III", len(joints), len(oriented), len(clips))
        data += b"".join(map(encoded_name, joints + oriented))
        pose = [value] * (len(joints) * 3) + [1., 0., 0., 0., 1., 0., 0., 0., 1.] * len(oriented)
        samples = struct.pack(f"<{len(pose) * 2}f", *(pose * 2))
        for name in clips:
            data += encoded_name(name) + struct.pack("<fI", 4., 2) + samples
        return data

    def validate(self):
        return installer.validate(self.source, self.header)

    def install(self):
        return installer.install(self.source, self.package, self.validate(), self.backups)

    def test_install_preserves_profile_and_is_idempotent(self):
        config = self.config.read_bytes()
        first = self.install()
        installed = self.destination.stat().st_mtime_ns
        second = self.install()
        self.assertTrue(first["changed"])
        self.assertFalse(second["changed"])
        self.assertEqual(self.source.read_bytes(), self.destination.read_bytes())
        self.assertEqual(self.destination.stat().st_mtime_ns, installed)
        self.assertEqual(self.config.read_bytes(), config)
        self.assertFalse(self.backups.exists())

    def test_replacement_retains_verified_original(self):
        self.install()
        original = self.destination.read_bytes()
        self.source.write_bytes(self.bank(value=1.0))
        result = self.install()
        self.assertEqual(Path(result["backup"]).read_bytes(), original)
        self.assertEqual(self.source.read_bytes(), self.destination.read_bytes())

    def test_corrupt_bank_does_not_replace_installed_bank(self):
        self.install()
        original = self.destination.read_bytes()
        for malformed in (self.bank()[:-4], self.bank(value=float("nan")), self.bank() + b"extra"):
            with self.subTest(bytes=len(malformed)):
                self.source.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    self.install()
                self.assertEqual(self.destination.read_bytes(), original)
                self.assertFalse(self.backups.exists())

    def test_incomplete_clip_inventory_is_rejected(self):
        self.source.write_bytes(self.bank(clips=self.clips - {"a001_003000"}))
        with self.assertRaisesRegex(ValueError, "missing 1 required clips"):
            self.install()
        self.assertFalse(self.destination.exists())

    def test_missing_skeleton_joint_is_rejected(self):
        self.source.write_bytes(self.bank(joints=installer.POSITION_JOINTS - {"L_Calf"}))
        with self.assertRaisesRegex(ValueError, "humanoid joints"):
            self.install()
        self.assertFalse(self.destination.exists())

    def test_missing_incantation_phase_preserves_installed_bank(self):
        # Copied Windows trees have only the bank and imported header. They
        # must reject old banks even without the recreation's clip inventory.
        self.install()
        original = self.destination.read_bytes()
        for name in installer.SPELL_ANIMATIONS:
            with self.subTest(clip=name):
                self.source.write_bytes(self.bank(clips=self.clips - {name}))
                with self.assertRaisesRegex(ValueError, "missing 1 required clips"):
                    self.install()
                self.assertEqual(self.destination.read_bytes(), original)
                self.assertFalse(self.backups.exists())

    def test_engine_incompatible_joint_names_preserve_installed_bank(self):
        self.install()
        original = self.destination.read_bytes()
        too_many = installer.POSITION_JOINTS | {f"Extra{index}" for index in range(219)}
        self.assertEqual(len(too_many), 257)
        for joints, error in ((too_many, "invalid header counts"),
                              (installer.POSITION_JOINTS | {"Extra\0Joint"}, "null byte")):
            with self.subTest(error=error):
                self.source.write_bytes(self.bank(joints=joints))
                with self.assertRaisesRegex(ValueError, error):
                    self.install()
                self.assertEqual(self.destination.read_bytes(), original)
                self.assertFalse(self.backups.exists())

    def test_source_changed_after_validation_preserves_destination(self):
        self.install()
        original = self.destination.read_bytes()
        self.source.write_bytes(self.bank(value=1.0))
        report = self.validate()
        self.source.write_bytes(self.bank(value=2.0))
        with self.assertRaisesRegex(ValueError, "changed while copying"):
            installer.install(self.source, self.package, report, self.backups)
        self.assertEqual(self.destination.read_bytes(), original)
        self.assertEqual(list(self.destination.parent.glob("*.tmp")), [])

    def test_wrong_prevalidated_hash_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            installer.validate(self.source, self.header, "0" * 64)
        self.assertFalse(self.destination.exists())

    def test_corrupt_existing_backup_is_not_overwritten(self):
        self.install()
        original = self.destination.read_bytes()
        original_hash = installer.sha256(self.destination)
        self.backups.mkdir()
        backup = self.backups / f"player_anims-{original_hash}.bin"
        backup.write_bytes(b"unexpected existing file")
        self.source.write_bytes(self.bank(value=1.0))
        with self.assertRaisesRegex(ValueError, "backup or installed bank changed"):
            self.install()
        self.assertEqual(self.destination.read_bytes(), original)
        self.assertEqual(backup.read_bytes(), b"unexpected existing file")


if __name__ == "__main__":
    unittest.main()
