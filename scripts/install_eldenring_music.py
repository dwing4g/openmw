#!/usr/bin/env python3
"""Validate and install locally imported Elden Ring ambience and boss music.

--source is the music/eldenring directory produced by import_eldenring_music.py.
No profile settings or saved games are changed. Previous music is backed up
outside the package before replacement. Python 3.10+; no external dependencies.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tempfile

from eldenring_music.catalog import LEGACY_REGIONS, REGIONS, BOSS_PROFILES, PROFILES

DESTINATION = Path("resources/vfs/music/eldenring")


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def validate(source: Path, expected_hash: str | None = None) -> dict:
    manifest_file = source / "manifest.json"
    if not manifest_file.is_file() or manifest_file.stat().st_size > 1024 * 1024:
        raise ValueError(f"missing or oversized music manifest: {manifest_file}")
    manifest_bytes = manifest_file.read_bytes()
    manifest_hash = hashlib.sha256(manifest_bytes).hexdigest()
    if expected_hash is not None and manifest_hash != expected_hash.lower():
        raise ValueError("music manifest changed since validation (SHA256 mismatch)")
    manifest = json.loads(manifest_bytes)
    if not isinstance(manifest, dict) or manifest.get("format_version") not in (1, 2, 3):
        raise ValueError("unsupported music manifest format")
    required = {1: LEGACY_REGIONS, 2: REGIONS, 3: PROFILES}[manifest["format_version"]]
    allowed = PROFILES if manifest["format_version"] == 3 else REGIONS
    tracks = manifest.get("tracks")
    if not isinstance(tracks, list) or not len(required) <= len(tracks) <= 100:
        raise ValueError("music manifest must contain all required music profiles")
    seen, regions = set(), set()
    for track in tracks:
        if not isinstance(track, dict):
            raise ValueError("invalid music track entry")
        name, region = track.get("file"), track.get("region")
        if not isinstance(name, str) or not isinstance(region, str) or region not in allowed:
            raise ValueError("invalid music filename or region")
        path = PurePosixPath(name)
        if (len(path.parts) != 2 or path.parts[0] != region or str(path) != name
                or not re.fullmatch(r"[a-zA-Z0-9_-]+\.ogg", path.name)):
            raise ValueError(f"unsafe music filename: {name}")
        if name.lower() in seen:
            raise ValueError(f"duplicate music filename: {name}")
        seen.add(name.lower())
        regions.add(region)
        digest = track.get("sha256")
        size = track.get("bytes")
        if (not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest)
                or type(size) is not int or not 64 <= size <= 256 * 1024 * 1024):
            raise ValueError(f"invalid music size or SHA256: {name}")
        audio = source / name
        if not audio.is_file() or audio.stat().st_size != size or sha256(audio) != digest:
            raise ValueError(f"music size or SHA256 mismatch: {audio}")
        with audio.open("rb") as stream:
            header = stream.read(256)
        if header[:4] != b"OggS" or b"\x01vorbis" not in header:
            raise ValueError(f"music is not Ogg Vorbis: {audio}")
    if not required <= regions:
        raise ValueError(f"music is missing profiles: {', '.join(sorted(required - regions))}")
    return {"manifest_sha256": manifest_hash, "tracks": tracks, "regions": sorted(regions)}


def install(source: Path, package: Path, report: dict, library: bool = False) -> dict:
    destination = package / (Path("music/eldenring") if library else DESTINATION)
    if destination.exists() and not destination.is_dir():
        raise ValueError(f"music destination is not a directory: {destination}")
    # Recheck the source before idempotence and after copying, to catch concurrent changes.
    validate(source, report["manifest_sha256"])
    if destination.is_dir():
        try:
            current = validate(destination)
        except (OSError, ValueError):
            current = None
        if current and current["manifest_sha256"] == report["manifest_sha256"]:
            return {**report, "destination": str(destination), "changed": False}
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".eldenring-music-", dir=destination.parent))
    backup = None
    try:
        # Keep any user-added tracks; replace only the importer's manifest and files.
        if destination.is_dir():
            shutil.copytree(destination, stage, dirs_exist_ok=True)
        for track in report["tracks"]:
            target = stage / track["file"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / track["file"], target)
        shutil.copyfile(source / "manifest.json", stage / "manifest.json")
        validate(stage, report["manifest_sha256"])
        if destination.exists():
            backup_root = package.parent / "build-eldenmusic-backups" / package.name
            backup_root.mkdir(parents=True, exist_ok=True)
            backup = Path(tempfile.mkdtemp(prefix="music-", dir=backup_root))
            backup.rmdir()
            os.replace(destination, backup)
        try:
            os.replace(stage, destination)
        except OSError:
            if backup is not None:
                os.replace(backup, destination)
            raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return {**report, "destination": str(destination), "changed": True,
            "backup": str(backup) if backup else None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--package", type=Path)
    output.add_argument("--library", type=Path, help="stage music/eldenring under a local asset library")
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--source-sha256", help="require a previously validated manifest hash")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if not args.check_only and args.package is None and args.library is None:
        parser.error("--package or --library is required unless --check-only is used")
    try:
        source = args.source.resolve()
        report = validate(source, args.source_sha256)
        if not args.check_only:
            report = install(source, (args.library or args.package).resolve(), report, args.library is not None)
    except (OSError, ValueError) as error:
        print(f"Music installation failed: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Validated {len(report['tracks'])} music tracks across {len(report['regions'])} music profiles.")
        if not args.check_only:
            print(f"{'Installed' if report['changed'] else 'Already installed'}: {report['destination']}")
            if report.get("backup"):
                print(f"Previous music backup: {report['backup']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
