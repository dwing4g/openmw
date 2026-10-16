#!/usr/bin/env python3
"""Validate/install locally imported Elden Ring combat sounds, preserving backups."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from import_eldenring_music import validate_ogg
from install_eldenring_music import sha256

# Keep the original library directory so existing source/package paths still work.
RELATIVE = Path("sound/eldenring/dodge")
DESTINATION = Path("resources/vfs") / RELATIVE
LEGACY_REQUIRED = {"roll.ogg", *(f"backstep-{i}.ogg" for i in range(9))}
REQUIRED = LEGACY_REQUIRED | {"guard-counter.ogg", *(f"block-{i}.ogg" for i in range(4))}


def validate(source, expected_hash=None):
    path = source / "manifest.json"
    if not path.is_file() or path.stat().st_size > 128 * 1024:
        raise ValueError("missing or oversized sound manifest")
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if expected_hash is not None and expected_hash.lower() != digest:
        raise ValueError("sound manifest changed since validation")
    manifest = json.loads(raw)
    if not isinstance(manifest, dict) or manifest.get("format_version") not in (1, 2):
        raise ValueError("unsupported sound manifest format")
    required = LEGACY_REQUIRED if manifest["format_version"] == 1 else REQUIRED
    sounds = manifest.get("sounds")
    if not isinstance(sounds, list) or len(sounds) != len(required):
        raise ValueError("sound manifest must contain every combat cue for its version")
    seen = set()
    for sound in sounds:
        if not isinstance(sound, dict):
            raise ValueError("invalid sound entry")
        name = sound.get("file")
        if not isinstance(name, str) or name not in required or name in seen:
            raise ValueError("unsafe, duplicate or unknown sound filename")
        seen.add(name)
        size, expected = sound.get("bytes"), sound.get("sha256")
        if type(size) is not int or not 64 <= size <= 2 * 1024 * 1024:
            raise ValueError("invalid sound size")
        audio = source / name
        if not audio.is_file() or audio.stat().st_size != size or sha256(audio) != expected:
            raise ValueError(f"sound size or SHA256 mismatch: {name}")
        validate_ogg(audio.read_bytes())
    return {"manifest_sha256": digest, "sounds": sounds}


def select_source(candidates):
    """Prefer complete combat libraries over legacy dodge-only copies."""
    legacy = None
    for candidate in candidates:
        source = Path(candidate).resolve()
        if not (source / "manifest.json").exists():
            continue
        report = validate(source)
        if {sound["file"] for sound in report["sounds"]} == REQUIRED:
            return source
        if legacy is None:
            legacy = source
    return legacy


def install(source, package, report, library=False):
    destination = package / (RELATIVE if library else DESTINATION)
    validate(source, report["manifest_sha256"])
    if destination.exists() and not destination.is_dir():
        raise ValueError("sound destination is not a directory")
    if destination.is_dir():
        try:
            if validate(destination)["manifest_sha256"] == report["manifest_sha256"]:
                return {**report, "destination": str(destination), "changed": False}
        except (OSError, ValueError):
            pass
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".eldenring-sounds-", dir=destination.parent))
    backup = None
    try:
        if destination.is_dir():
            shutil.copytree(destination, stage, dirs_exist_ok=True)
        for name in {sound["file"] for sound in report["sounds"]} | {"manifest.json"}:
            shutil.copyfile(source / name, stage / name)
        validate(stage, report["manifest_sha256"])
        if destination.exists():
            backup_root = package.parent / "build-eldensound-backups" / package.name
            backup_root.mkdir(parents=True, exist_ok=True)
            backup = Path(tempfile.mkdtemp(prefix="dodge-", dir=backup_root))
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sources = parser.add_mutually_exclusive_group(required=True)
    sources.add_argument("--source", type=Path)
    sources.add_argument("--select-source", nargs="+", type=Path,
                         help="print the first complete library, or first legacy fallback, from these candidates")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--package", type=Path)
    output.add_argument("--library", type=Path)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--source-sha256")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.select_source is None and not args.check_only and args.package is None and args.library is None:
        parser.error("--package or --library is required unless --check-only is used")
    try:
        if args.select_source is not None:
            selected = select_source(args.select_source)
            if args.json:
                # PowerShell 5.1 may decode redirected Python output with a
                # different code page. Keep machine-consumed paths ASCII-safe.
                print(json.dumps({"source": str(selected) if selected is not None else None}, ensure_ascii=True))
            else:
                print(selected if selected is not None else "")
            return
        source = args.source.resolve()
        report = validate(source, args.source_sha256)
        if not args.check_only:
            report = install(source, (args.library or args.package).resolve(), report, args.library is not None)
    except (OSError, ValueError) as error:
        sys.exit(f"Sound installation failed: {error}")
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Validated {len(report['sounds'])} Elden Ring combat recordings.")
        if {sound["file"] for sound in report["sounds"]} == LEGACY_REQUIRED:
            print("Legacy dodge-only library: re-import sounds to include blocks and guard counters.", file=sys.stderr)
        if not args.check_only:
            print(f"{'Installed' if report['changed'] else 'Already installed'}: {report['destination']}")


if __name__ == "__main__":
    main()
