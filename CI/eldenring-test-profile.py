#!/usr/bin/env python3
"""Check save paths, rebuild preservation and launcher entry points without game assets."""

import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "configure_eldenring_profile", Path(__file__).resolve().parents[1] / "scripts/configure_eldenring_profile.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class ProfileConfiguration(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        locations = patch.object(profile, "save_data_locations", return_value=[])
        locations.start()
        self.addCleanup(locations.stop)
        self.package = self.root / "пакет OpenMW & settings"
        self.game = self.root / "Morrowind"
        self.data = self.game / "Data Files"
        self.data.mkdir(parents=True)
        for name in ("Morrowind.esm", "Morrowind.bsa", "Tribunal.esm", "Tribunal.bsa"):
            (self.data / name).touch()
        self.config = self.package / "elden-profile/openmw.cfg"

    def configure(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            profile.configure(self.package, self.game, kwargs.pop("target", "linux"), True, **kwargs)

    def test_new_profile_uses_standard_saves_and_separate_settings(self):
        self.configure()
        config = self.config.read_text(encoding="utf-8")
        self.assertIn('user-data="?userdata?"\n', config)
        self.assertIn("content=Morrowind.esm\ncontent=Tribunal.esm", config.replace("fallback-archive=Morrowind.bsa\n", ""))
        self.assertNotIn("Bloodmoon.esm", config)
        self.assertIn('config="elden-profile"', (self.package / "openmw.cfg").read_text())
        self.assertTrue((self.config.parent / "settings.cfg").is_file())
        self.assertFalse((self.config.parent / "user-data").exists())

    def test_rebuild_preserves_launcher_changes_and_user_files(self):
        self.configure()
        custom = (b'# launcher changes\r\nuser-data="D:/Custom Saves"\r\n'
                  b'content=Morrowind.esm\r\ncontent=MyMod.esp\r\nfallback=Weather_Clear_Fog_Day_Depth,.9\r\n')
        self.config.write_bytes(custom)
        files = {
            "settings.cfg": (b"[Video]\nresolution x = 1920\n"
                             b"[Sound]\nmaster volume = 0.8\nmusic volume = 0.6\ndevice = Custom device\n"),
            "input_v3.xml": b"custom bindings",
            "user-data/saves/character/slot.omwsave": b"existing save",
        }
        for name, data in files.items():
            path = self.config.parent / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.configure()
        self.assertEqual(self.config.read_bytes(), custom)
        for name, data in files.items():
            self.assertEqual((self.config.parent / name).read_bytes(), data)

    def test_empty_old_portable_profile_upgrades_only_save_path(self):
        self.configure(portable_saves=True)
        old = self.config.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        old += b"# keep launcher changes\r\ncontent=MyMod.esp\r\n"
        self.config.write_bytes(old)
        self.configure()
        self.assertEqual(self.config.read_bytes(), old.replace(b'user-data="./user-data"', b'user-data="?userdata?"'))

    def test_populated_portable_profile_keeps_existing_saves_visible(self):
        self.configure(portable_saves=True)
        original = self.config.read_bytes()
        save = self.config.parent / "user-data/saves/character/slot.omwsave"
        save.parent.mkdir(parents=True)
        save.write_bytes(b"saved game")
        self.configure()
        self.assertEqual(self.config.read_bytes(), original)
        self.assertEqual(save.read_bytes(), b"saved game")

    def test_explicit_portable_mode_is_preserved_on_rerun(self):
        self.configure(portable_saves=True)
        self.configure(portable_saves=True)
        self.assertIn('user-data="./user-data"', self.config.read_text())

    def test_windows_entry_points_select_the_same_local_profile(self):
        with patch.object(profile, "game_path", return_value="C:/Morrowind/Data Files"):
            self.configure(target="windows")
        game = (self.package / "Start-EldenCombat.cmd").read_bytes()
        launcher = (self.package / "Start-EldenCombat-Launcher.cmd").read_bytes()
        diagnostic = (self.package / "Start-EldenCombat-Audio-Diagnostic.cmd").read_bytes()
        self.assertIn(b'--replace config --config "elden-profile"', game)
        self.assertIn(b'".\\openmw-launcher.exe" %*\r\n', launcher)
        self.assertNotIn(b"--config", launcher)
        self.assertIn(b'-File ".\\eldenring-diagnose-audio-windows.ps1"', diagnostic)
        source_diagnostic = Path(profile.__file__).resolve().parents[1] / "CI/eldenring-diagnose-audio-windows.ps1"
        self.assertEqual((self.package / source_diagnostic.name).read_bytes(), source_diagnostic.read_bytes())
        for script in (game, launcher, diagnostic):
            self.assertIn(b'setlocal DisableDelayedExpansion\r\n', script)
            self.assertIn(b'pushd "%~dp0" || exit /b 1\r\n', script)
            self.assertNotIn(b"\n", script.replace(b"\r\n", b""))

    @unittest.skipIf(os.name == "nt", "executes the Linux shell entry points")
    def test_shell_entry_points_work_from_another_directory(self):
        self.configure()
        for name in ("openmw", "openmw-launcher"):
            executable = self.package / name
            executable.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$PWD" "$@"\nexit 7\n')
            executable.chmod(0o755)
        for script, arguments in (("Start-EldenCombat.sh", ["--replace", "config", "--config", "elden-profile"]),
                                  ("Start-EldenCombat-Launcher.sh", [])):
            result = subprocess.run([str(self.package / script), "argument with spaces"], cwd=self.root,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 7, result.stderr)
            self.assertEqual(result.stdout.splitlines(), [str(self.package), *arguments, "argument with spaces"])

    def test_missing_engine_does_not_create_a_profile(self):
        with self.assertRaisesRegex(ValueError, "has not been built"):
            profile.configure(self.package, self.game, "linux", False)
        self.assertFalse(self.config.exists())

    def saved_data(self, name):
        data = self.root / name / "My Games" / "OpenMW"
        slot = data / "saves/niki/Quicksave.omwsave"
        slot.parent.mkdir(parents=True)
        slot.write_bytes(b"unchanged saved game")
        return data

    def test_redirected_documents_saves_reused_without_touching_other_settings(self):
        self.configure()
        self.config.write_bytes(b'\xef\xbb\xbfuser-data="?userdata?"\r\ncontent=Morrowind.esm\r\n'
                                b'content=MyMod.esp\r\nfallback=Fonts_Font_0,custom\r\n')
        original = self.config.read_bytes()
        settings = self.config.parent / "settings.cfg"
        settings.write_bytes(b"[Video]\nresolution x = 1920\n")
        bindings = self.config.parent / "input_v3.xml"
        bindings.write_bytes(b"custom bindings")
        current = self.root / "Documents/My Games/OpenMW"
        previous = self.saved_data("OneDrive/Documents")
        with patch.object(profile, "save_data_locations", return_value=[current, previous]):
            self.configure()
        expected = original.replace(b'"?userdata?"',
                                    profile.quoted_path(profile.game_path(previous, "linux")).encode())
        self.assertEqual(self.config.read_bytes(), expected)
        self.assertEqual(self.config.with_name("openmw.cfg.before-save-repair").read_bytes(), original)
        self.assertEqual(settings.read_bytes(), b"[Video]\nresolution x = 1920\n")
        self.assertEqual(bindings.read_bytes(), b"custom bindings")
        self.assertEqual((previous / "saves/niki/Quicksave.omwsave").read_bytes(), b"unchanged saved game")
        self.assertFalse(current.exists())
        with patch.object(profile, "save_data_locations", return_value=[current, previous]):
            self.configure()
        self.assertEqual(self.config.read_bytes(), expected)
        self.assertFalse(self.config.with_name("openmw.cfg.before-save-repair.1").exists())

    def test_current_standard_saves_take_priority_over_former_documents(self):
        self.configure()
        original = self.config.read_bytes()
        current = self.saved_data("Documents")
        previous = self.saved_data("OneDrive/Documents")
        with patch.object(profile, "save_data_locations", return_value=[current, previous]):
            self.configure()
        self.assertEqual(self.config.read_bytes(), original)

    def test_multiple_previous_save_locations_require_explicit_selection(self):
        self.configure()
        original = self.config.read_bytes()
        current = self.root / "Documents/My Games/OpenMW"
        previous = [self.saved_data("OneDrive/Documents"), self.saved_data("Work OneDrive/Documents")]
        with patch.object(profile, "save_data_locations", return_value=[current, *previous]):
            self.configure()
        self.assertEqual(self.config.read_bytes(), original)
        self.configure(save_data=previous[1])
        self.assertIn(profile.quoted_path(profile.game_path(previous[1], "linux")).encode(), self.config.read_bytes())

    def test_custom_save_location_is_not_replaced_by_old_standard_saves(self):
        self.configure()
        original = b'user-data="D:/Custom Saves"\ncontent=Morrowind.esm\n'
        self.config.write_bytes(original)
        previous = self.saved_data("OneDrive/Documents")
        with patch.object(profile, "save_data_locations", return_value=[self.root / "current", previous]):
            self.configure()
        self.assertEqual(self.config.read_bytes(), original)

    def test_loose_or_vanilla_morrowind_saves_do_not_select_an_unusable_folder(self):
        self.configure()
        original = self.config.read_bytes()
        previous = self.root / "OneDrive/Documents/My Games/OpenMW"
        (previous / "saves/niki").mkdir(parents=True)
        (previous / "saves/quicksave.omwsave").touch()
        (previous / "saves/niki/quicksave.ess").touch()
        with patch.object(profile, "save_data_locations", return_value=[self.root / "current", previous]):
            self.configure()
        self.assertEqual(self.config.read_bytes(), original)

    def test_repair_only_does_not_require_engine_or_game_installation(self):
        self.configure()
        previous = self.saved_data('OneDrive & backup/Documents')
        original = self.config.read_bytes()
        result = subprocess.run([sys.executable, str(Path(profile.__file__)), "--package", str(self.package),
                                 "--target", "linux", "--repair-saves-only", "--save-data", str(previous)],
                                capture_output=True, encoding="utf-8", env={**os.environ, "PYTHONUTF8": "1"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(profile.quoted_path(profile.game_path(previous, "linux")).encode(), self.config.read_bytes())
        self.assertEqual(self.config.with_name("openmw.cfg.before-save-repair").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
