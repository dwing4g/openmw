#!/usr/bin/env python3
"""Check copied build inputs and selected local assets without modifying either tree."""

import argparse
import fnmatch
import os
from pathlib import Path
import sys

import install_eldenring_music as music
import install_eldenring_sounds as sounds


# These are source trees, including files/ which CMake turns into runtime
# resources. Build/dependency caches at the repository root are not inputs.
SOURCE_TREES = ("apps", "cmake", "components", "extern", "files", "scripts", "CI", "docs", "manual", "docker")
CACHE_DIRECTORIES = {".git", ".github", ".gitlab", ".hg", ".svn", ".bzr", ".cache", ".idea", ".vscode", ".vs",
                     "__pycache__", ".pytest_cache", ".mypy_cache", ".ruff_cache", ".ipynb_checkpoints"}
ROOT_EXCLUDES = (".git*", ".hg*", ".svn*", ".bzr*", ".mailmap", "Makefile", "openmw.cfg", "settings.cfg",
                 "input_v3.xml", "CMakeCache.txt", "cmake_install.cmake", "compile_commands.json", ".ninja_*",
                 "build.ninja", "rules.ninja", "*.pyc", "*.pyo", "*.o", "*.obj", "*.a", "*.so", "*.pdb",
                 "*.dll", "*.exe", "*.lib", "*.dmp", "*.omwsave", "OpenMW-EldenCombat-Windows-*.zip*")


def source_files(source):
    """Inventory source inputs independently of rsync/Robocopy's exclusions."""
    for path in sorted(source.iterdir()):
        if path.is_file() and not any(fnmatch.fnmatchcase(path.name, pattern) for pattern in ROOT_EXCLUDES):
            yield path.relative_to(source)
    for tree in SOURCE_TREES:
        for directory, children, files in os.walk(source / tree):
            children[:] = sorted(name for name in children if name not in CACHE_DIRECTORIES)
            for name in sorted(files):
                if name.endswith((".pyc", ".pyo")) or name.startswith((".git", ".hg", ".svn", ".bzr")) or name == ".mailmap":
                    continue
                yield (Path(directory) / name).relative_to(source)


def matching_file(source, destination, label):
    if not destination.is_file():
        raise ValueError(f"copied input is missing: {label}")
    if source.stat().st_size != destination.stat().st_size or music.sha256(source) != music.sha256(destination):
        raise ValueError(f"copied input differs from source: {label}")


def verify(source, destination, animations=None, music_source=None, sounds_source=None):
    count = 0
    for relative in source_files(source):
        matching_file(source / relative, destination / relative, relative)
        count += 1
    if not count:
        raise ValueError("source inventory is empty")
    if animations:
        matching_file(animations, destination / "local-assets/player_anims.bin", "local animation bank")
    tracks, cues = 0, 0
    if music_source:
        report = music.validate(music_source)
        music.validate(destination / "local-assets/music/eldenring", report["manifest_sha256"])
        tracks = len(report["tracks"])
    if sounds_source:
        report = sounds.validate(sounds_source)
        sounds.validate(destination / "local-assets" / sounds.RELATIVE, report["manifest_sha256"])
        cues = len(report["sounds"])
    return count, int(animations is not None), tracks, cues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--animations-source", type=Path)
    parser.add_argument("--music-source", type=Path)
    parser.add_argument("--sounds-source", type=Path)
    args = parser.parse_args()
    try:
        count, banks, tracks, cues = verify(args.source, args.destination, args.animations_source,
                                            args.music_source, args.sounds_source)
    except (OSError, ValueError) as error:
        sys.exit(f"Source copy verification failed: {error}")
    print(f"Verified SHA256: {count} source/resource files, {banks} animation bank(s), "
          f"{tracks} music recordings, {cues} combat recordings.")


if __name__ == "__main__":
    main()
