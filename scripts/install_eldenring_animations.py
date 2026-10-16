#!/usr/bin/env python3
"""Validate and install a user's locally baked Elden Ring animations.

Example:
    python scripts/install_eldenring_animations.py --source ../ELDEN-RING-Combat-Rewrite/assets/player_anims.bin --package build-eldenring-package-x64

Only the animation bank is installed; profiles, settings and saves are untouched.
Existing banks are backed up before an atomic replacement. No game assets belong
in the source repository. Python 3.10+ and its standard library are sufficient.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DESTINATION = Path("resources/vfs/animations/eldencombat/player_anims.bin")
MAX_BYTES = 256 * 1024 * 1024
POSITION_JOINTS = {
    "Pelvis", "Spine", "Spine1", "Spine2", "Neck", "Head",
    *(side + bone for side in ("L_", "R_") for bone in (
        "Clavicle", "UpperArm", "Forearm", "Hand", "Thigh", "Calf", "Foot", "Toe0", "Weapon",
        "Finger0", "Finger01", "Finger02", "Finger1", "Finger2", "Finger21", "Finger22")),
}
ORIENTED_JOINTS = {"Head", "R_Weapon", "L_Weapon"}
LOCOMOTION = (*range(20000, 20004), *range(20100, 20104), 20200, *range(22100, 22104))
BASE_ANIMATIONS = (
    0, 100, *LOCOMOTION, 300000, *range(320000, 320004), *range(320100, 320104),
    322100, 390000, 390001, 202040, 4000, 17002,
)
# Wind-up and uncharged release phases used by spellcasting.hpp. Keep these
# mandatory even in a copied Windows checkout without the recreation sources.
SPELL_ANIMATIONS = {
    "a433_045000", "a433_045010",  # Urgent Heal: self
    "a506_045000", "a506_045010",  # Catch Flame: touch
    "a417_045000", "a417_045011",  # Flame Sling: target
}


def load_auditor():
    spec = importlib.util.spec_from_file_location("eldenring_animation_audit", ROOT / "CI/eldenring-audit-animations.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def required_clips(timings_header: Path) -> set[str]:
    """The shipped engine's actions and stance categories must all be available."""
    source = timings_header.read_text(encoding="utf-8")
    actions = set(re.findall(r'\bmAnimation\s*=\s*"(a\d{3}_\d{6})"', source))
    stances = re.findall(r'\breturn\s+twoHanded\s*\?\s*(\d+)u\s*:\s*(\d+)u\s*;', source)
    if not actions or not stances:
        raise ValueError(f"no imported animation metadata in {timings_header}; regenerate the timing header first")
    expected = actions | {f"a000_{animation:06d}" for animation in BASE_ANIMATIONS} | SPELL_ANIMATIONS
    for two, one in stances:
        for stance in (int(one), int(two)):
            expected.update(f"a{stance:03d}_{animation:06d}" for animation in (0, 100))
        expected.update(f"a{int(two):03d}_{animation:06d}" for animation in LOCOMOTION)
    return expected


def validate(source: Path, timings_header: Path, expected_hash: str | None = None) -> dict:
    if not source.is_file() or not 16 <= source.stat().st_size <= MAX_BYTES:
        raise ValueError(f"animation source must be a regular ERAN file of at most {MAX_BYTES} bytes: {source}")
    auditor = load_auditor()
    report, clips = auditor.read_clips(source)
    if expected_hash is not None and report["sha256"].lower() != expected_hash.lower():
        raise ValueError("animation source changed since validation (SHA256 mismatch)")
    if not POSITION_JOINTS <= set(report["position_joints"]) or not ORIENTED_JOINTS <= set(report["oriented_joints"]):
        raise ValueError("animation bank lacks the humanoid joints or weapon/head axes needed for retargeting")
    expected = required_clips(timings_header)
    recreation = source.parent.parent
    if (recreation / "src/sim/extracted.rs").is_file() and (recreation / "tools/bake_anims.py").is_file():
        expected |= auditor.expected_clips(recreation)
        report["inventory"] = "engine requirements and local recreation's complete clip inventory"
    else:
        report["inventory"] = "engine action, stance and locomotion requirements"
    missing = sorted(expected - clips)
    if missing:
        raise ValueError(f"animation bank is missing {len(missing)} required clips: {', '.join(missing[:12])}")
    report["required_clips"] = len(expected)
    report["spellcasting_clips"] = sorted(SPELL_ANIMATIONS)
    return report


def install(source: Path, package: Path, report: dict, backup_directory: Path | None = None) -> dict:
    destination = package / DESTINATION
    if destination.exists() and not destination.is_file():
        raise ValueError(f"animation destination is not a file: {destination}")
    old_hash = sha256(destination) if destination.is_file() else None
    if old_hash == report["sha256"]:
        return {**report, "destination": str(destination), "changed": False}

    # Check the copied bytes too: the source may change after validation or during a network/UNC copy.
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = None
    backup = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".player_anims-", suffix=".tmp", dir=destination.parent, delete=False) as stream:
            stage = Path(stream.name)
            with source.open("rb") as original:
                shutil.copyfileobj(original, stream)
            stream.flush()
            os.fsync(stream.fileno())
        if sha256(stage) != report["sha256"]:
            raise ValueError("animation source changed while copying; the installed bank was preserved")
        if old_hash is not None:
            if backup_directory is None:
                backup_directory = package.parent / "build-eldenanimation-backups" / package.name
            backup_directory.mkdir(parents=True, exist_ok=True)
            backup = backup_directory / f"player_anims-{old_hash}.bin"
            if not backup.exists():
                shutil.copyfile(destination, backup)
            if sha256(backup) != old_hash or sha256(destination) != old_hash:
                raise ValueError("animation backup or installed bank changed; replacement cancelled")
        elif destination.exists():
            raise ValueError("animation destination appeared during copying; replacement cancelled")
        os.replace(stage, destination)
        stage = None
    finally:
        if stage is not None:
            stage.unlink(missing_ok=True)
    return {**report, "destination": str(destination), "changed": True,
            "backup": str(backup) if backup is not None else None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, type=Path, help="locally baked player_anims.bin")
    parser.add_argument("--package", type=Path, help="OpenMW runtime package directory")
    parser.add_argument("--timings-header", type=Path, default=ROOT / "components/eldencombat/imported_timings.hpp")
    parser.add_argument("--check-only", action="store_true", help="validate without writing any files")
    parser.add_argument("--source-sha256", help="require the hash recorded during a previous validation")
    parser.add_argument("--backup-directory", type=Path, help="optional directory for previous animation banks")
    parser.add_argument("--json", action="store_true", help="print machine-readable validation/install results")
    args = parser.parse_args()
    if not args.check_only and args.package is None:
        parser.error("--package is required unless --check-only is used")
    try:
        source = args.source.resolve()
        report = validate(source, args.timings_header.resolve(), args.source_sha256)
        if not args.check_only:
            report = install(source, args.package.resolve(), report, args.backup_directory)
    except (OSError, ValueError, SyntaxError, TypeError) as error:
        print(f"Animation installation failed: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Validated {report['clips']} clips and {report['frames']} frames; {report['required_clips']} required clips present.")
        print(f"SHA256: {report['sha256']}")
        if not args.check_only:
            print(f"{'Installed' if report['changed'] else 'Already installed'}: {report['destination']}")
            if report.get("backup"):
                print(f"Previous bank backup: {report['backup']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
