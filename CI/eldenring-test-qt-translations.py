#!/usr/bin/env python3
"""Exercise Qt translation discovery without installing Qt or building OpenMW."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SOURCE_ROOT = Path(__file__).resolve().parents[1]
MODULE = SOURCE_ROOT / "cmake/QtTranslations.cmake"
LOCAL_CMAKE = SOURCE_ROOT / "build-tools-local/cmake/bin/cmake"
CMAKE = str(LOCAL_CMAKE) if LOCAL_CMAKE.is_file() else shutil.which("cmake")


def cmake_literal(value):
    """Quote paths without expanding CMake variables or Windows backslashes."""
    if isinstance(value, Path):
        value = value.as_posix()
    return "[==[" + str(value) + "]==]"


class QtTranslationDiscovery(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="openmw-qt-translations-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.prefix = self.root / "Никита Documents/Qt 6.6.3/msvc2019_64"
        self.translations = self.prefix / "translations"
        self.translations.mkdir(parents=True)
        self.query_translations = self.translations
        if os.name == "nt":
            try:
                self.query_translations.as_posix().encode("mbcs")
            except UnicodeEncodeError:
                # qmake uses the Windows ANSI code page, which may not support Cyrillic.
                # Metadata and explicit-override tests still exercise Unicode on every host.
                self.query_translations = self.root / "Qt query fallback/translations"
                self.query_translations.mkdir(parents=True)
        for directory in {self.translations, self.query_translations}:
            (directory / "qtbase_bg.qm").write_bytes(b"Qt translation fixture")
        self.resolved = self.root / "resolved.txt"
        self.query_log = self.root / "queried.txt"
        self.fake_qmake = self.root / "fake_qmake.py"
        self.fake_qmake.write_text(
            "import os\n"
            "from pathlib import Path\n"
            "import sys\n"
            "assert sys.argv[1:] == ['-query', 'QT_INSTALL_TRANSLATIONS'], sys.argv\n"
            "Path(os.environ['OPENMW_TEST_QMAKE_LOG']).write_text('queried', encoding='utf-8')\n"
            "encoding = 'mbcs' if os.name == 'nt' else 'utf-8'\n"
            "sys.stdout.buffer.write(os.environ['OPENMW_TEST_QMAKE_OUTPUT'].encode(encoding))\n"
            "sys.exit(int(os.environ['OPENMW_TEST_QMAKE_EXIT_CODE']))\n",
            encoding="utf-8",
        )

    def query_path(self, path):
        value = path.as_posix()
        if os.name == "nt":
            try:
                value.encode("mbcs")
            except UnicodeEncodeError:
                # TEMP can itself contain a username outside the active ANSI code page.
                # CMake runs from self.root, so an ASCII relative path avoids encoding it.
                value = path.relative_to(self.root).as_posix()
        return value

    def run_discovery(self, variables=None, *, query=False, query_output="", query_exit=0):
        lines = []
        for name, value in (variables or {}).items():
            cache = ' CACHE PATH "Test translation directory"' if name == "QT_TRANSLATIONS_DIR" else ""
            lines.append(f"set({name} {cmake_literal(value)}{cache})")
        lines.append(f"include({cmake_literal(MODULE)})")
        if query:
            arguments = f"{cmake_literal(Path(sys.executable))} {cmake_literal(self.fake_qmake)}"
        else:
            arguments = cmake_literal(self.root / "qmake-must-not-be-called")
        lines.append(f"openmw_get_qt_translations_dir(translations_dir {arguments})")
        lines.append(f'file(WRITE {cmake_literal(self.resolved)} "${{translations_dir}}")')
        script = self.root / "discover.cmake"
        script.write_text("\n".join(lines) + "\n", encoding="utf-8")
        environment = os.environ.copy()
        environment.update({
            "OPENMW_TEST_QMAKE_LOG": str(self.query_log),
            "OPENMW_TEST_QMAKE_OUTPUT": query_output,
            "OPENMW_TEST_QMAKE_EXIT_CODE": str(query_exit),
        })
        return subprocess.run(
            [CMAKE, "-P", str(script)], cwd=self.root, env=environment,
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
        )

    def assert_resolves(self, result, expected):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(self.resolved.is_file(), result.stdout + result.stderr)
        selected = Path(self.resolved.read_text(encoding="utf-8"))
        self.assertEqual(selected, expected)
        self.assertTrue((selected / "qtbase_bg.qm").is_file(), "Selected directory must expose Bulgarian translations")

    def assert_rejected(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.resolved.exists(), "Discovery continued after an invalid directory or query")

    def test_relative_qt_metadata_preserves_unicode_without_query(self):
        result = self.run_discovery({
            "QT6_INSTALL_PREFIX": self.prefix,
            "QT6_INSTALL_TRANSLATIONS": "translations",
        })
        self.assert_resolves(result, self.translations)

    def test_absolute_qt_metadata_preserves_unicode_without_query(self):
        result = self.run_discovery({
            "QT6_INSTALL_PREFIX": self.root / "unused prefix",
            "QT6_INSTALL_TRANSLATIONS": self.translations,
        })
        self.assert_resolves(result, self.translations)

    def test_explicit_directory_overrides_corrupt_metadata(self):
        result = self.run_discovery({
            "QT_TRANSLATIONS_DIR": self.translations,
            "QT6_INSTALL_PREFIX": self.root / "corrupted username",
            "QT6_INSTALL_TRANSLATIONS": "not present",
        })
        self.assert_resolves(result, self.translations)

    def test_relative_explicit_directory_is_resolved(self):
        result = self.run_discovery({
            "QT_TRANSLATIONS_DIR": self.translations.relative_to(self.root),
        })
        self.assert_resolves(result, self.translations)

    def test_missing_explicit_directory_is_rejected(self):
        result = self.run_discovery({
            "QT_TRANSLATIONS_DIR": self.root / "missing override",
            "QT6_INSTALL_PREFIX": self.prefix,
            "QT6_INSTALL_TRANSLATIONS": "translations",
        }, query=True, query_output=self.query_path(self.query_translations) + "\n")
        self.assert_rejected(result)
        self.assertFalse(self.query_log.exists(), "An invalid explicit override must not silently fall back")

    def test_missing_metadata_directory_is_rejected(self):
        result = self.run_discovery({
            "QT6_INSTALL_PREFIX": self.prefix,
            "QT6_INSTALL_TRANSLATIONS": "missing translations",
        }, query=True, query_output=self.query_path(self.query_translations) + "\n")
        self.assert_rejected(result)
        self.assertFalse(self.query_log.exists(), "Invalid Qt metadata must not silently fall back")

    def test_query_fallback_preserves_path_and_trims_newline(self):
        result = self.run_discovery(query=True, query_output=self.query_path(self.query_translations) + "\r\n")
        self.assert_resolves(result, self.query_translations)
        self.assertTrue(self.query_log.is_file())

    def test_query_fallback_rejects_empty_output(self):
        result = self.run_discovery(query=True, query_output="\r\n")
        self.assert_rejected(result)
        self.assertTrue(self.query_log.is_file())

    def test_query_fallback_rejects_nonzero_status_even_with_valid_output(self):
        result = self.run_discovery(
            query=True, query_output=self.query_path(self.query_translations) + "\n", query_exit=7,
        )
        self.assert_rejected(result)
        self.assertTrue(self.query_log.is_file())

    def test_query_fallback_rejects_missing_directory(self):
        result = self.run_discovery(query=True, query_output=self.query_path(self.root / "missing translations"))
        self.assert_rejected(result)
        self.assertTrue(self.query_log.is_file())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=CMAKE, help="CMake executable used for the regression fixtures")
    arguments, unittest_arguments = parser.parse_known_args()
    CMAKE = arguments.cmake
    if not CMAKE:
        parser.error("CMake was not found; pass --cmake with its executable path")
    unittest.main(argv=[sys.argv[0], *unittest_arguments])
