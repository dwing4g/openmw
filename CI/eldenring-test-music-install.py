#!/usr/bin/env python3
"""Music package installation regressions using synthetic files, no game assets."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))

spec = importlib.util.spec_from_file_location(
    "install_eldenring_music", Path(__file__).resolve().parents[1] / "scripts/install_eldenring_music.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class MusicInstallation(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "music-source"
        self.package = self.root / "пакет OpenMW"
        self.destination = self.package / installer.DESTINATION
        self.manifest = {"format_version": 2, "tracks": []}
        for region in sorted(installer.REGIONS):
            file = self.source / region / "day.ogg"
            file.parent.mkdir(parents=True)
            file.write_bytes(b"OggS" + bytes(24) + b"\x01vorbis" + bytes(64))
            self.manifest["tracks"].append({"region": region, "file": f"{region}/day.ogg",
                                            "sha256": installer.sha256(file), "bytes": file.stat().st_size})
        self.write_manifest()
        self.config = self.package / "elden-profile/settings.cfg"
        self.config.parent.mkdir(parents=True)
        self.config.write_bytes(b"[Game]\nelden ring music = false\n")

    def write_manifest(self):
        (self.source / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def install(self):
        return installer.install(self.source, self.package, installer.validate(self.source))

    def change_track(self):
        track = self.manifest["tracks"][0]
        file = self.source / track["file"]
        file.write_bytes(file.read_bytes() + b"changed")
        track.update(sha256=installer.sha256(file), bytes=file.stat().st_size)
        self.write_manifest()

    def test_install_and_repeat_leave_settings_untouched(self):
        before = self.config.read_bytes()
        self.assertTrue(self.install()["changed"])
        self.assertFalse(self.install()["changed"])
        self.assertEqual(self.config.read_bytes(), before)
        self.assertEqual(installer.validate(self.destination), installer.validate(self.source))

    def test_corrupt_track_preserves_installed_music(self):
        self.install()
        original = (self.destination / "manifest.json").read_bytes()
        (self.source / "caelid/day.ogg").write_bytes(b"truncated")
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.install()
        self.assertEqual((self.destination / "manifest.json").read_bytes(), original)

    def test_replacement_keeps_backup_and_custom_tracks(self):
        self.install()
        original = (self.destination / "manifest.json").read_bytes()
        custom = self.destination / "caelid/custom.ogg"
        custom.write_bytes(b"user-added file")
        self.change_track()
        result = self.install()
        self.assertEqual((Path(result["backup"]) / "manifest.json").read_bytes(), original)
        self.assertEqual(custom.read_bytes(), b"user-added file")
        self.assertEqual(installer.validate(self.destination), installer.validate(self.source))

    def test_replacement_failure_restores_original(self):
        self.install()
        original = (self.destination / "manifest.json").read_bytes()
        self.change_track()
        replace = installer.os.replace

        def fail_stage(source, destination):
            if Path(source).name.startswith(".eldenring-music-"):
                raise OSError("simulated replacement failure")
            return replace(source, destination)

        with patch.object(installer.os, "replace", side_effect=fail_stage):
            with self.assertRaises(OSError):
                self.install()
        self.assertEqual((self.destination / "manifest.json").read_bytes(), original)

    def test_missing_region_duplicate_and_unsafe_paths_are_rejected(self):
        original = json.dumps(self.manifest)
        for mode in ("missing", "duplicate", "traversal", "absolute", "backslash"):
            self.manifest = json.loads(original)
            if mode == "missing":
                self.manifest["tracks"].pop()
            elif mode == "duplicate":
                self.manifest["tracks"].append(self.manifest["tracks"][0])
            else:
                self.manifest["tracks"][0]["file"] = {
                    "traversal": "../escape.ogg", "absolute": "/escape.ogg",
                    "backslash": "caelid/..\\escape.ogg"}[mode]
            self.write_manifest()
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.install()
            self.assertFalse(self.destination.exists())

    def test_source_changed_after_validation_is_rejected(self):
        report = installer.validate(self.source)
        self.change_track()
        with self.assertRaisesRegex(ValueError, "changed since validation"):
            installer.install(self.source, self.package, report)
        self.assertFalse(self.destination.exists())

    def test_malformed_region_is_a_validation_error(self):
        for region in ([], {}, None, 7):
            self.manifest["tracks"][0]["region"] = region
            self.write_manifest()
            with self.subTest(region=region), self.assertRaisesRegex(ValueError, "invalid music filename or region"):
                self.install()
        self.assertFalse(self.destination.exists())

    def test_asset_library_copy(self):
        library = self.root / "local-assets"
        installer.install(self.source, library, installer.validate(self.source), library=True)
        self.assertEqual(installer.validate(library / "music/eldenring"), installer.validate(self.source))

    def test_legacy_five_track_library_upgrades_with_backup(self):
        expanded = json.dumps(self.manifest)
        self.manifest['format_version'] = 1
        self.manifest['tracks'] = [track for track in self.manifest['tracks']
                                   if track['region'] in installer.LEGACY_REGIONS]
        self.write_manifest()
        self.install()
        self.manifest = json.loads(expanded)
        self.write_manifest()
        result = self.install()
        self.assertTrue(result['changed'])
        self.assertEqual(len(installer.validate(Path(result['backup']))['regions']), 5)
        self.assertEqual(len(installer.validate(self.destination)['regions']), 20)

    def test_expanded_manifest_cannot_claim_full_coverage_with_legacy_tracks(self):
        self.manifest['tracks'] = [track for track in self.manifest['tracks']
                                   if track['region'] in installer.LEGACY_REGIONS]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'required music profiles'):
            self.install()

    def add_boss_tracks(self):
        self.manifest['format_version'] = 3
        for profile in sorted(installer.BOSS_PROFILES):
            file = self.source / profile / 'battle.ogg'
            file.parent.mkdir(parents=True)
            file.write_bytes(b'OggS' + bytes(24) + b'\x01vorbis' + bytes(64))
            self.manifest['tracks'].append({'region': profile, 'file': f'{profile}/battle.ogg',
                                           'sha256': installer.sha256(file), 'bytes': file.stat().st_size})
        self.write_manifest()

    def test_ambient_library_upgrades_to_bosses_without_changing_ambient_files(self):
        self.install()
        ambient = {track['file']: (self.destination / track['file']).read_bytes()
                   for track in self.manifest['tracks']}
        self.add_boss_tracks()
        result = self.install()
        self.assertTrue(result['changed'])
        self.assertFalse(self.install()['changed'])
        self.assertEqual(len(installer.validate(Path(result['backup']))['regions']), 20)
        self.assertEqual(len(installer.validate(self.destination)['regions']), 28)
        for name, contents in ambient.items():
            self.assertEqual((self.destination / name).read_bytes(), contents)

    def test_boss_manifest_requires_every_boss_profile(self):
        self.add_boss_tracks()
        self.manifest['tracks'].pop()
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'required music profiles'):
            self.install()
        self.assertFalse(self.destination.exists())

    def test_boss_manifest_rejects_unknown_and_mismatched_profile_paths(self):
        self.add_boss_tracks()
        original = json.dumps(self.manifest)
        for field, value in (('region', 'boss_unverified'), ('file', 'boss_mohg/../battle.ogg'),
                             ('file', 'caelid/battle.ogg')):
            self.manifest = json.loads(original)
            self.manifest['tracks'][-1][field] = value
            self.write_manifest()
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.install()
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
