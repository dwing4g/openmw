#!/usr/bin/env python3
"""Combat sound routing and installation regressions; no game assets required."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import import_eldenring_sounds as importer
import install_eldenring_sounds as installer
import install_eldenring_animations as animations
from eldenring_music import wem, wwise


def chunk(tag, body):
    return tag + struct.pack("<I", len(body)) + body


def node(kind, oid, body):
    return struct.pack("<BII", kind, len(body) + 4, oid) + body


def bank(event=True, media=665535489, version=135, name="Play_c000008000"):
    source = struct.pack("<IBIIB", 0x40001, 0, media, 100, 0)
    base = bytes(3) + struct.pack("<II", 0, 0) + bytes(1)
    base += b"\x01\0" + struct.pack("<f", -10) + b"\0"
    objects = [node(2, 100, source + base)]
    if event:
        objects += [node(3, 101, struct.pack("<HI", 0x403, 100)),
                    node(4, wwise.fnv1(name), b"\x01" + struct.pack("<I", 101))]
    return chunk(b"BKHD", struct.pack("<I", version)) + chunk(
        b"HIRC", struct.pack("<I", len(objects)) + b"".join(objects))


def ogg(payload=b"test recording"):
    writer = wem.OggWriter()
    for packet in (b"\x01vorbis" + bytes(23), b"\x03vorbis", b"\x05vorbis"):
        writer.page(packet, 0)
    writer.page(payload, 24000, last=True)
    return bytes(writer.out)


def animation_bank(timing_header):
    joints = sorted(animations.POSITION_JOINTS)
    oriented = sorted(animations.ORIENTED_JOINTS)
    clips = sorted(animations.required_clips(timing_header))

    def name(value):
        encoded = value.encode("utf-8")
        return bytes([len(encoded)]) + encoded

    data = b"ERAN" + struct.pack("<III", len(joints), len(oriented), len(clips))
    data += b"".join(name(value) for value in joints + oriented)
    pose = [0.] * (len(joints) * 3) + [1., 0., 0., 0., 1., 0., 0., 0., 1.] * len(oriented)
    samples = struct.pack(f"<{len(pose) * 2}f", *(pose * 2))
    for clip in clips:
        data += name(clip) + struct.pack("<fI", 0., 2) + samples
    return data


class SoundRouting(unittest.TestCase):
    def test_resolves_play_action_to_real_source_and_mix(self):
        data = bank()
        importer.validate_bank(data)
        recordings = importer.event_recordings(wwise.Bank(data), "Play_c000008000")
        self.assertEqual(set(recordings), {665535489})
        self.assertEqual(recordings[665535489].volume, -10)

    def test_unrelated_media_id_is_not_a_dodge_event(self):
        with self.assertRaisesRegex(ValueError, "Missing combat"):
            importer.event_recordings(wwise.Bank(bank(event=False)), "Play_c000008000")

    def test_guard_counter_recording_is_imported_from_its_play_event(self):
        cue = next(cue for cue in importer.CUES if cue[0] == "guard-counter.ogg")
        self.assertEqual(cue[1:], ("Play_c999999984", 594059761))
        recordings = importer.event_recordings(wwise.Bank(bank(media=cue[2], name=cue[1])), cue[1])
        self.assertEqual(set(recordings), {cue[2]})

    def test_truncated_unknown_and_overrunning_banks_fail(self):
        invalid = chunk(b"BKHD", struct.pack("<I", 135)) + chunk(
            b"HIRC", struct.pack("<IBII", 1, 2, 0xffffffff, 100))
        for data in (bank()[:-1], bank(version=140), invalid):
            with self.subTest(data=data), self.assertRaises(ValueError):
                importer.validate_bank(data)

    def test_cannot_write_into_owned_game(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            importer.import_sounds(Path("/game"), Path("/game/assets"), Path("/keys"))

    def test_changed_event_media_fails_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(importer, "read_assets", return_value=({importer.BANK: bank(media=42)}, {})), \
                 patch.object(importer, "CUES", (("roll.ogg", "Play_c000008000", 665535489),)), \
                 self.assertRaisesRegex(ValueError, "changed"):
                importer.import_sounds(root / "game", root / "out", root / "keys")
            self.assertFalse((root / "out").exists())


class SoundInstallation(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.package = self.root / "пакет OpenMW"
        self.manifest = {"format_version": 2, "sounds": []}
        for name in sorted(installer.REQUIRED):
            path = self.source / name
            path.write_bytes(ogg())
            self.manifest["sounds"].append({"file": name, "bytes": path.stat().st_size,
                                             "sha256": installer.sha256(path)})
        self.write_manifest()

    def write_manifest(self):
        (self.source / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def install(self):
        return installer.install(self.source, self.package, installer.validate(self.source))

    def test_install_is_idempotent_and_preserves_settings(self):
        self.package.mkdir()
        config = self.package / "settings.cfg"
        config.write_text("unchanged")
        self.assertTrue(self.install()["changed"])
        self.assertFalse(self.install()["changed"])
        self.assertEqual(config.read_text(), "unchanged")
        self.assertEqual(installer.validate(self.source), installer.validate(self.package / installer.DESTINATION))

    def test_rejects_missing_duplicate_and_unsafe_filenames(self):
        original = self.manifest["sounds"][0]["file"]
        for name in ("../roll.ogg", "ROLL.ogg", "backstep-8.ogg"):
            self.manifest["sounds"][0]["file"] = name
            self.write_manifest()
            with self.subTest(name=name), self.assertRaises(ValueError):
                installer.validate(self.source)
        self.manifest["sounds"][0]["file"] = original
        self.manifest["sounds"].pop()
        self.write_manifest()
        with self.assertRaises(ValueError):
            installer.validate(self.source)

    def test_corrupt_recording_leaves_package_unchanged(self):
        self.install()
        old = (self.package / installer.DESTINATION / "roll.ogg").read_bytes()
        (self.source / "roll.ogg").write_bytes(b"bad")
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.install()
        self.assertEqual((self.package / installer.DESTINATION / "roll.ogg").read_bytes(), old)

    def test_incomplete_ogg_rejected_even_with_correct_hash(self):
        path = self.source / "roll.ogg"
        path.write_bytes(ogg()[:-1])
        sound = next(s for s in self.manifest["sounds"] if s["file"] == "roll.ogg")
        sound.update(bytes=path.stat().st_size, sha256=installer.sha256(path))
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "truncated"):
            installer.validate(self.source)

    def change(self):
        path = self.source / "roll.ogg"
        path.write_bytes(ogg(b"changed recording"))
        sound = next(s for s in self.manifest["sounds"] if s["file"] == "roll.ogg")
        sound.update(bytes=path.stat().st_size, sha256=installer.sha256(path))
        self.write_manifest()

    def test_upgrade_preserves_user_files_and_backup(self):
        self.install()
        destination = self.package / installer.DESTINATION
        (destination / "custom.ogg").write_bytes(b"custom")
        old = (destination / "roll.ogg").read_bytes()
        self.change()
        result = self.install()
        self.assertEqual((Path(result["backup"]) / "roll.ogg").read_bytes(), old)
        self.assertEqual((destination / "custom.ogg").read_bytes(), b"custom")

    def test_detects_source_changed_after_validation(self):
        report = installer.validate(self.source)
        self.change()
        with self.assertRaisesRegex(ValueError, "changed"):
            installer.install(self.source, self.package, report)

    def test_failed_swap_restores_existing_library(self):
        self.install()
        destination = self.package / installer.DESTINATION
        old = (destination / "roll.ogg").read_bytes()
        self.change()
        replace = installer.os.replace
        def fail_stage(source, target):
            if Path(source).name.startswith(".eldenring-sounds-"):
                raise OSError("simulated swap failure")
            replace(source, target)
        with patch.object(installer.os, "replace", side_effect=fail_stage), self.assertRaises(OSError):
            self.install()
        self.assertEqual((destination / "roll.ogg").read_bytes(), old)

    def test_library_copy_matches_windows_build_lookup(self):
        installer.install(self.source, self.package, installer.validate(self.source), library=True)
        self.assertEqual(installer.validate(self.package / "sound/eldenring/dodge"), installer.validate(self.source))

    def test_version_two_requires_guard_counter(self):
        self.manifest["sounds"] = [sound for sound in self.manifest["sounds"]
                                   if sound["file"] != "guard-counter.ogg"]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "every combat cue"):
            installer.validate(self.source)

    def test_version_two_requires_all_block_variants(self):
        self.manifest["sounds"] = [sound for sound in self.manifest["sounds"]
                                   if sound["file"] != "block-3.ogg"]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "every combat cue"):
            installer.validate(self.source)

    def test_legacy_dodge_library_still_installs_and_upgrades(self):
        expanded = self.manifest["sounds"]
        self.manifest.update(format_version=1, sounds=[sound for sound in expanded
                                                     if sound["file"] in installer.LEGACY_REQUIRED])
        self.write_manifest()
        self.assertTrue(self.install()["changed"])
        destination = self.package / installer.DESTINATION
        self.assertFalse((destination / "guard-counter.ogg").exists())
        self.assertEqual(len(installer.validate(destination)["sounds"]), 10)
        self.manifest.update(format_version=2, sounds=expanded)
        self.write_manifest()
        report = self.install()
        self.assertTrue(report["changed"])
        self.assertTrue((destination / "guard-counter.ogg").is_file())
        self.assertEqual(len(installer.validate(Path(report["backup"]))["sounds"]), 10)

    def legacy_library(self):
        library = self.root / "legacy sounds"
        shutil.copytree(self.source, library)
        manifest = {"format_version": 1,
                    "sounds": [sound for sound in self.manifest["sounds"]
                               if sound["file"] in installer.LEGACY_REQUIRED]}
        (library / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        for name in installer.REQUIRED - installer.LEGACY_REQUIRED:
            (library / name).unlink()
        return library

    def test_selection_upgrades_legacy_staged_sounds_to_complete_import(self):
        legacy = self.legacy_library()
        self.assertEqual(installer.select_source([legacy, self.source]), self.source.resolve())

    def test_selection_keeps_priority_between_complete_libraries(self):
        second = self.root / "other complete sounds"
        shutil.copytree(self.source, second)
        self.assertEqual(installer.select_source([self.source, second]), self.source.resolve())
        self.assertEqual(installer.select_source([second, self.source]), second.resolve())

    def test_selection_retains_first_legacy_library_when_no_complete_library_exists(self):
        legacy = self.legacy_library()
        second = self.root / "other legacy sounds"
        shutil.copytree(legacy, second)
        self.assertEqual(installer.select_source([self.root / "absent", legacy, second]), legacy.resolve())

    def test_selection_returns_none_when_no_manifest_exists(self):
        empty = self.root / "empty sounds"
        empty.mkdir()
        self.assertIsNone(installer.select_source([self.root / "absent", empty]))

    def test_selection_rejects_corrupt_libraries_instead_of_silently_falling_back(self):
        legacy = self.legacy_library()
        (self.source / "guard-counter.ogg").write_bytes(b"corrupt")
        for candidates in ([self.source, legacy], [legacy, self.source]):
            with self.subTest(candidates=candidates), self.assertRaisesRegex(ValueError, "mismatch"):
                installer.select_source(candidates)

    def test_selection_cli_prints_only_resolved_path_and_does_not_install(self):
        legacy = self.legacy_library()
        result = subprocess.run([sys.executable, installer.__file__, "--select-source", str(legacy),
                                 str(self.source), "--package", str(self.package)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout, str(self.source.resolve()) + "\n")
        self.assertEqual(result.stderr, "")
        self.assertFalse(self.package.exists())

    def test_selection_cli_prints_empty_result_without_available_libraries(self):
        result = subprocess.run([sys.executable, installer.__file__, "--select-source", str(self.root / "absent")],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "\n")
        self.assertEqual(result.stderr, "")

    def test_selection_json_survives_mismatched_windows_code_pages(self):
        source = self.root / "Никита 音声 sounds"
        shutil.copytree(self.source, source)
        for encoding in ("ascii", "cp1251", "utf-8"):
            with self.subTest(encoding=encoding):
                result = subprocess.run(
                    [sys.executable, installer.__file__, "--select-source", str(source), "--json"],
                    capture_output=True, timeout=30,
                    env={**os.environ, "PYTHONIOENCODING": encoding})
                self.assertEqual(result.returncode, 0, result.stderr.decode("ascii", errors="replace"))
                # PowerShell 5.1 can decode a redirected Python process using
                # a different OEM code page. The path must survive that boundary.
                self.assertTrue(result.stdout.isascii())
                for decoder in ("ascii", "cp866", "cp1251", "utf-8"):
                    selected = Path(json.loads(result.stdout.decode(decoder))["source"])
                    self.assertEqual(selected, source.resolve())
                    self.assertEqual(installer.validate(selected), installer.validate(source))

    def test_selection_json_returns_null_without_available_libraries(self):
        result = subprocess.run(
            [sys.executable, installer.__file__, "--select-source", str(self.root / "absent"), "--json"],
            capture_output=True, text=True, encoding="ascii", timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNone(json.loads(result.stdout)["source"])

    def source_copy_fixture(self, local=False):
        if sys.platform == "win32" or not all(shutil.which(tool) for tool in ("bash", "rsync", "realpath", "sha256sum")):
            self.skipTest("WSL source-copy helper requires bash and rsync")
        repository = Path(__file__).resolve().parents[1]
        tree = self.root / "WSL source"
        tree.mkdir()
        shutil.copyfile(repository / "copy-to-documents.sh", tree / "copy-to-documents.sh")
        for name in ("CMakeLists.txt", "apps/openmw/CMakeLists.txt", "components/CMakeLists.txt",
                     "extern/sol3/sol/sol.hpp", "extern/oics/CMakeLists.txt",
                     "components/eldencombat/imported_timings.hpp", "CI/eldenring-build-windows.ps1"):
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture source\n")
        shutil.copyfile(repository / "CI/eldenring-audit-animations.py", tree / "CI/eldenring-audit-animations.py")
        timing_header = tree / "components/eldencombat/imported_timings.hpp"
        timing_header.write_text('result.mAnimation = "a001_003000";\nreturn twoHanded ? 10u : 0u;',
                                 encoding="utf-8")
        (tree / "local-assets").mkdir()
        (tree / "local-assets/player_anims.bin").write_bytes(animation_bank(timing_header))
        scripts = tree / "scripts"
        scripts.mkdir()
        for name in ("install_eldenring_animations.py", "install_eldenring_sounds.py", "install_eldenring_music.py",
                     "import_eldenring_music.py", "unpack_eldenring_assets.py", "verify_eldenring_copy.py"):
            shutil.copyfile(repository / "scripts" / name, scripts / name)
        shutil.copytree(repository / "scripts/eldenring_music", scripts / "eldenring_music",
                        ignore=shutil.ignore_patterns("__pycache__"))
        library = tree / ("local-assets" if local else "build-eldenring-assets") / installer.RELATIVE
        shutil.copytree(self.source, library)
        return tree, library

    def run_source_copy(self, tree, *arguments, success=True):
        result = subprocess.run(["bash", str(tree / "copy-to-documents.sh"), str(self.package), *arguments],
                                capture_output=True, text=True, timeout=30)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_wsl_copy_includes_sounds_and_preserves_existing_profile(self):
        tree, _ = self.source_copy_fixture()
        profile = self.package / "elden-profile/settings.cfg"
        profile.parent.mkdir(parents=True)
        profile.write_text("user preferences")
        save = self.package / "saves/character.omwsave"
        save.parent.mkdir()
        save.write_bytes(b"existing save")
        self.run_source_copy(tree)
        self.assertEqual(installer.validate(self.package / "local-assets" / installer.RELATIVE),
                         installer.validate(self.source))
        self.assertFalse((self.package / "build-eldenring-assets").exists())
        self.run_source_copy(tree)
        self.assertEqual(profile.read_text(), "user preferences")
        self.assertEqual(save.read_bytes(), b"existing save")

    def test_wsl_copy_finds_local_sound_library(self):
        tree, _ = self.source_copy_fixture(local=True)
        self.run_source_copy(tree)
        self.assertEqual(installer.validate(self.package / "local-assets" / installer.RELATIVE),
                         installer.validate(self.source))

    def test_wsl_dry_run_reports_sound_copy_without_creating_files(self):
        tree, _ = self.source_copy_fixture()
        result = self.run_source_copy(tree, "--dry-run")
        self.assertIn("combat sounds", result.stdout)
        self.assertFalse(self.package.exists())

    def test_wsl_corrupt_sound_library_fails_before_source_copy(self):
        tree, library = self.source_copy_fixture()
        (library / "roll.ogg").write_bytes(b"corrupt")
        result = self.run_source_copy(tree, success=False)
        self.assertIn("Sound installation failed", result.stderr)
        self.assertFalse(self.package.exists())


if __name__ == "__main__":
    unittest.main()
