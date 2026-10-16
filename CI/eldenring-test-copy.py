#!/usr/bin/env python3
"""Copy inventory regressions, including native PowerShell when run on Windows."""

import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import install_eldenring_music as music
import install_eldenring_sounds as sounds
import install_eldenring_animations as animations
import verify_eldenring_copy as verifier
from eldenring_music import wem
from eldenring_music.catalog import PROFILES


class SourceCopy(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # Exercise Python -> PowerShell path transport even on ASCII-only
        # Windows accounts; the reported failure used a Cyrillic profile path.
        self.source = self.root / "Никита source tree"
        self.destination = self.root / "пакет source"
        self.source.mkdir()
        self.inputs = (
            "CMakeLists.txt", "apps/openmw/CMakeLists.txt", "components/CMakeLists.txt",
            "components/eldencombat/imported_timings.hpp", "extern/oics/CMakeLists.txt", "extern/sol3/sol/sol.hpp",
            "apps/openmw/mwmechanics/character.cpp", "apps/openmw/mwmechanics/eldencombat.cpp",
            "apps/openmw/mwmechanics/eldencombat.hpp", "components/eldencombat/combat.cpp",
            "components/eldencombat/combat.hpp", "scripts/import_eldenring_timings.py",
            "scripts/import_eldenring_sounds.py", "CI/eldenring-test-jump.cpp",
            "files/openmw.cfg", "files/settings-default.cfg", "files/data/fonts/font.ttf",
            "files/data/scripts/input.lua", "files/data-mw/scripts/music.lua", "files/data/builtin.omwscripts",
            "files/shaders/water.glsl", "files/data/textures/button.dds", "cmake/module.cmake",
            "docs/mainpage.hpp.cmake", "manual/intro.tex", "docker/config", "README.md",
        )
        for relative in self.inputs:
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"synthetic project input\n")
        for relative in ("copy-to-documents.sh", "CI/eldenring-copy-to-documents.ps1", "CI/eldenring-build-windows.ps1",
                         "scripts/install_eldenring_animations.py", "scripts/install_eldenring_music.py",
                         "scripts/install_eldenring_sounds.py", "scripts/import_eldenring_music.py",
                         "scripts/unpack_eldenring_assets.py", "scripts/verify_eldenring_copy.py",
                         "CI/eldenring-audit-animations.py"):
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, path)
        shutil.copytree(ROOT / "scripts/eldenring_music", self.source / "scripts/eldenring_music",
                        ignore=shutil.ignore_patterns("__pycache__"))
        self.animation = self.source / "local-assets/player_anims.bin"
        self.animation.parent.mkdir()
        self.timing_header = self.source / "components/eldencombat/imported_timings.hpp"
        self.timing_header.write_text(
            'result.mAnimation = "a001_003000";\nreturn twoHanded ? 10u : 0u;\n'
            'result.mLanding = std::make_shared<ActionDef>(*definitionLanding(config));\n'
            'result.mLandingShort = std::make_shared<ActionDef>(*definitionLandingShort(config));\n'
            'result.mAnimation = "a023_031070";\nresult.mAnimation = "a023_031071";\n', encoding="utf-8")
        self.animation.write_bytes(self.animation_bank())
        self.music = self.source / "build-eldenring-assets/music/eldenring"
        self.music.mkdir(parents=True)
        manifest = {"format_version": 3, "tracks": []}
        for profile in sorted(PROFILES):
            path = self.music / profile / "theme.ogg"
            path.parent.mkdir()
            path.write_bytes(b"OggS\x01vorbis" + bytes(100))
            manifest["tracks"].append({"file": f"{profile}/theme.ogg", "region": profile,
                                       "bytes": path.stat().st_size, "sha256": music.sha256(path)})
        (self.music / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        self.sounds = self.source / "build-eldenring-assets" / sounds.RELATIVE
        self.sounds.mkdir(parents=True)
        writer = wem.OggWriter()
        for packet in (b"\x01vorbis" + bytes(23), b"\x03vorbis", b"\x05vorbis"):
            writer.page(packet, 0)
        writer.page(b"synthetic sound", 24000, last=True)
        manifest = {"format_version": 2, "sounds": []}
        for name in sorted(sounds.REQUIRED):
            path = self.sounds / name
            path.write_bytes(writer.out)
            manifest["sounds"].append({"file": name, "bytes": path.stat().st_size, "sha256": music.sha256(path)})
        (self.sounds / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    def animation_bank(self, missing=()):
        joints = sorted(animations.POSITION_JOINTS)
        oriented = sorted(animations.ORIENTED_JOINTS)
        clips = sorted(animations.required_clips(self.timing_header) - set(missing))
        def name(value):
            encoded = value.encode("utf-8")
            return bytes([len(encoded)]) + encoded
        bank = b"ERAN" + struct.pack("<III", len(joints), len(oriented), len(clips))
        bank += b"".join(name(value) for value in joints + oriented)
        pose = [0.] * (len(joints) * 3) + [1., 0., 0., 0., 1., 0., 0., 0., 1.] * len(oriented)
        samples = struct.pack(f"<{len(pose) * 2}f", *(pose * 2))
        for clip in clips:
            bank += name(clip) + struct.pack("<fI", 0., 2) + samples
        return bank

    def prepare_destination(self):
        shutil.copytree(self.source, self.destination)
        music.install(self.music, self.destination / "local-assets", music.validate(self.music), library=True)
        sounds.install(self.sounds, self.destination / "local-assets", sounds.validate(self.sounds), library=True)

    def verify(self):
        return verifier.verify(self.source, self.destination, self.animation, self.music, self.sounds)

    def test_every_build_and_resource_tree_is_verified(self):
        self.prepare_destination()
        count, banks, tracks, cues = self.verify()
        self.assertGreaterEqual(count, len(self.inputs))
        self.assertEqual((banks, tracks, cues), (1, len(PROFILES), len(sounds.REQUIRED)))
        for relative in self.inputs:
            path = self.destination / relative
            contents = path.read_bytes()
            path.unlink()
            with self.subTest(relative=relative), self.assertRaisesRegex(ValueError, "missing"):
                self.verify()
            path.write_bytes(contents)

    def test_stale_same_size_source_file_is_detected(self):
        self.prepare_destination()
        path = self.destination / "components/eldencombat/imported_timings.hpp"
        path.write_bytes(b"x" * path.stat().st_size)
        with self.assertRaisesRegex(ValueError, "differs"):
            self.verify()

    def test_future_resource_input_is_included_without_updating_inventory(self):
        self.prepare_destination()
        path = self.source / "files/data/textures/new-mod-texture.dds"
        path.write_bytes(b"new input")
        with self.assertRaisesRegex(ValueError, "new-mod-texture"):
            self.verify()

    def test_build_caches_vcs_and_runtime_profiles_are_not_source_inputs(self):
        self.prepare_destination()
        for relative in ("build-new/cache.bin", "deps/runtime.dll", "resources/generated.dds",
                         "elden-profile/settings.cfg", "files/.gitignore", "scripts/__pycache__/module.pyc"):
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"not an input")
        self.verify()

    def test_missing_or_stale_local_assets_are_detected(self):
        self.prepare_destination()
        for path in (self.destination / "local-assets/player_anims.bin",
                     self.destination / "local-assets/music/eldenring" / sorted(PROFILES)[0] / "theme.ogg",
                     self.destination / "local-assets" / sounds.RELATIVE / "roll.ogg"):
            contents = path.read_bytes()
            path.write_bytes(b"x" * len(contents))
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.verify()
            path.write_bytes(contents)

    def copy_command(self, dry_run=False):
        if sys.platform == "win32":
            shell = shutil.which("powershell.exe")
            if not shell:
                self.skipTest("native PowerShell unavailable")
            command = [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                       str(self.source / "CI/eldenring-copy-to-documents.ps1"),
                       "-SourceDirectory", str(self.source), "-DestinationDirectory", str(self.destination)]
            if dry_run:
                command.append("-ListOnly")
        else:
            if not all(shutil.which(tool) for tool in ("bash", "rsync", "realpath", "sha256sum", "python3")):
                self.skipTest("WSL copy dependencies unavailable")
            command = ["bash", str(self.source / "copy-to-documents.sh"), str(self.destination)]
            if dry_run:
                command.append("--dry-run")
        return command

    def run_copy(self, dry_run=False):
        result = subprocess.run(self.copy_command(dry_run), capture_output=True, text=True, errors="replace", timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_copier_transfers_all_inputs_and_preserves_user_state(self):
        preserved = ("elden-profile/settings.cfg", "saves/character.omwsave", "build-cache/file", "deps/runtime.dll")
        for relative in preserved:
            path = self.destination / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"keep existing state")
        self.run_copy()
        self.verify()
        self.assertFalse((self.destination / "build-eldenring-assets").exists())
        self.run_copy()
        for relative in preserved:
            self.assertEqual((self.destination / relative).read_bytes(), b"keep existing state")

    def test_native_dry_run_does_not_create_destination(self):
        self.run_copy(dry_run=True)
        self.assertFalse(self.destination.exists())

    def test_native_copier_rejects_incomplete_animations_before_copying(self):
        # An old bank still has ERAN magic, but cannot supply the new seal
        # gestures. Refuse it before replacing source files or existing assets.
        previous = self.destination / "local-assets/player_anims.bin"
        previous.parent.mkdir(parents=True)
        previous.write_bytes(self.animation_bank())
        original = previous.read_bytes()
        for bank in (self.animation_bank(missing=animations.SPELL_ANIMATIONS),
                     self.animation_bank(missing={"a417_045011"}),
                     self.animation_bank(missing={"a023_031070"}),
                     self.animation_bank(missing={"a023_031071"}),
                     b"ERAN truncated bank"):
            with self.subTest(bytes=len(bank)):
                self.animation.write_bytes(bank)
                result = subprocess.run(self.copy_command(), capture_output=True, text=True,
                                        errors="replace", timeout=60)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("Animation installation failed", result.stdout + result.stderr)
                self.assertEqual(previous.read_bytes(), original)
                self.assertFalse((self.destination / "CMakeLists.txt").exists())

    def test_native_copier_rejects_missing_sound_before_copying(self):
        for name in ("roll.ogg", "guard-counter.ogg", *(f"block-{i}.ogg" for i in range(4))):
            with self.subTest(name=name):
                path = self.sounds / name
                data = path.read_bytes()
                path.unlink()
                result = subprocess.run(self.copy_command(), capture_output=True, text=True,
                                        errors="replace", timeout=60)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(self.destination.exists())
                path.write_bytes(data)

    def test_native_copier_prefers_complete_import_over_staged_legacy_sounds(self):
        staged = self.source / "local-assets" / sounds.RELATIVE
        staged.mkdir(parents=True)
        manifest = json.loads((self.sounds / "manifest.json").read_text())
        manifest["format_version"] = 1
        manifest["sounds"] = [sound for sound in manifest["sounds"] if sound["file"] in sounds.LEGACY_REQUIRED]
        for sound in manifest["sounds"]:
            shutil.copyfile(self.sounds / sound["file"], staged / sound["file"])
        (staged / "manifest.json").write_text(json.dumps(manifest))
        self.run_copy()
        self.verify()
        copied = sounds.validate(self.destination / "local-assets" / sounds.RELATIVE)
        self.assertEqual({sound["file"] for sound in copied["sounds"]}, sounds.REQUIRED)

    def test_native_copier_accepts_staged_sound_library(self):
        staged = self.source / "local-assets" / sounds.RELATIVE
        staged.parent.mkdir(parents=True)
        shutil.move(str(self.sounds), staged)
        self.sounds = staged
        self.run_copy()
        self.verify()


if __name__ == "__main__":
    unittest.main()
