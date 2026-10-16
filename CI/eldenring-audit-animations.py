#!/usr/bin/env python3
"""Check local recreation animation data without copying game-derived assets.

Usage:
    python3 CI/eldenring-audit-animations.py ../ELDEN-RING-Combat-Rewrite
    python3 CI/eldenring-audit-animations.py ../ELDEN-RING-Combat-Rewrite --json

The expected clip inventory comes from generated extracted.rs and the baker's
literal EXTRA/STANCE_LOCOMOTION lists. This checks binary integrity and source
coverage, not whether an engine retargets, loads, or correctly displays clips.
An optional --package directory reports loose animation assets present there;
it does not inspect archives or external data directories referenced by a profile.
Python 3.10+ and its standard library are sufficient. No source code is executed.
"""

import argparse
import array
import ast
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys


# Keep these limits in sync with components/eldencombat/animations.cpp. Banks
# accepted here are installed for that reader, so accepting a larger layout
# would replace a working asset with one the engine cannot load.
MAX_BYTES = 256 * 1024 * 1024
MAX_JOINTS = 256
MAX_CLIPS = 65536


class AuditError(ValueError):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def take(self, size: int) -> bytes:
        if size > len(self.data) - self.offset:
            raise AuditError(f"truncated animation file at byte {self.offset}: need {size} bytes")
        result = self.data[self.offset:self.offset + size]
        self.offset += size
        return result

    def name(self) -> str:
        size = self.take(1)[0]
        if size == 0:
            raise AuditError(f"empty name at byte {self.offset - 1}")
        name = self.take(size).decode("utf-8")
        if "\0" in name:
            raise AuditError("name contains a null byte")
        return name


def read_clips(path: Path) -> tuple[dict, set[str]]:
    # Read at most one byte past the engine's cap, including when the file grows
    # between validation and this read.
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise AuditError("animation file exceeds the engine size limit")
    reader = Reader(data)
    if reader.take(4) != b"ERAN":
        raise AuditError("animation file has no ERAN magic")
    joints, oriented, count = struct.unpack("<III", reader.take(12))
    if not 1 <= joints <= MAX_JOINTS or not 1 <= count <= MAX_CLIPS or oriented > joints:
        raise AuditError(f"invalid header counts: joints={joints}, oriented={oriented}, clips={count}")
    # Every encoded name occupies at least two bytes. Bound loops before reading.
    if joints + oriented + count > (len(data) - reader.offset) // 2:
        raise AuditError("header counts cannot fit in animation file")
    joint_names = [reader.name() for _ in range(joints)]
    oriented_names = [reader.name() for _ in range(oriented)]
    if len(set(joint_names)) != joints or len(set(oriented_names)) != oriented:
        raise AuditError("duplicate joint name")
    if not set(oriented_names) <= set(joint_names):
        raise AuditError("oriented joint missing from position joints")
    stride = joints * 3 + oriented * 9
    clips = set()
    total_frames = 0
    minimum_frames = None
    maximum_frames = 0
    for _ in range(count):
        name = reader.name()
        if not re.fullmatch(r"a\d{3}_\d{6}", name):
            raise AuditError(f"invalid clip name {name!r}")
        if name in clips:
            raise AuditError(f"duplicate clip {name}")
        blend, frames = struct.unpack("<fI", reader.take(8))
        if not frames or not math.isfinite(blend) or blend < 0:
            raise AuditError(f"invalid clip metadata for {name}: frames={frames}, blend={blend}")
        values = array.array("f", reader.take(frames * stride * 4))
        if sys.byteorder != "little":
            values.byteswap()
        if not all(map(math.isfinite, values)):
            raise AuditError(f"non-finite pose sample in {name}")
        clips.add(name)
        total_frames += frames
        minimum_frames = frames if minimum_frames is None else min(minimum_frames, frames)
        maximum_frames = max(maximum_frames, frames)
    if reader.offset != len(data):
        raise AuditError(f"{len(data) - reader.offset} unexpected trailing bytes")
    return {
        "file": str(path),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "position_joints": joint_names,
        "oriented_joints": oriented_names,
        "clips": count,
        "frames": total_frames,
        "finite_float_samples": total_frames * stride,
        "minimum_frames": minimum_frames,
        "maximum_frames": maximum_frames,
    }, clips


def expected_clips(recreation: Path) -> set[str]:
    source = (recreation / "src/sim/extracted.rs").read_text(encoding="utf-8")
    expected = set(re.findall(r'\b(?:source|start|end):\s*"(a\d{3}_\d{6})"', source))
    stances = {tuple(map(int, match)) for match in re.findall(r"stance:\s*\[(\d+),\s*(\d+)\]", source)}
    if not expected or not stances:
        raise AuditError("extracted.rs has no recognizable clip references or weapon stances")
    literals = {}
    tree = ast.parse((recreation / "tools/bake_anims.py").read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in ("EXTRA", "STANCE_LOCOMOTION"):
                    literals[target.id] = ast.literal_eval(node.value)
    if set(literals) != {"EXTRA", "STANCE_LOCOMOTION"}:
        raise AuditError("bake_anims.py lacks literal EXTRA or STANCE_LOCOMOTION lists")
    for file, animation in literals["EXTRA"]:
        expected.add(f"a{int(file[1:]):03d}_{animation:06d}")
    for one, two in stances:
        for stance in (one, two):
            for animation in (0, 100):
                expected.add(f"a{stance:03d}_{animation:06d}")
        for animation in literals["STANCE_LOCOMOTION"]:
            expected.add(f"a{two:03d}_{animation:06d}")
    return expected


def package_inventory(package: Path) -> dict:
    if not package.is_dir():
        raise AuditError(f"package directory does not exist: {package}")
    animations = []
    baked = []
    for path in package.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(package).as_posix()
        if path.suffix.lower() in (".kf", ".hkx", ".nif"):
            animations.append(relative)
        if path.suffix.lower() == ".bin":
            with path.open("rb") as stream:
                if stream.read(4) == b"ERAN":
                    baked.append(relative)
    return {
        "directory": str(package),
        "loose_animation_or_model_files": sorted(animations),
        "baked_eran_files": sorted(baked),
        "scope": "Loose package files only; excludes archives, external data, and engine playback validation.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("recreation", type=Path, help="local ELDEN-RING-Combat-Rewrite directory")
    parser.add_argument("--package", type=Path, help="optional OpenMW package directory to inventory")
    parser.add_argument("--json", action="store_true", help="print the report as JSON")
    args = parser.parse_args()
    try:
        recreation = args.recreation.resolve()
        report, clips = read_clips(recreation / "assets/player_anims.bin")
        expected = expected_clips(recreation)
        report["expected_clips"] = len(expected)
        report["missing_expected_clips"] = sorted(expected - clips)
        report["unexpected_clips"] = sorted(clips - expected)
        report["passed"] = not report["missing_expected_clips"] and not report["unexpected_clips"]
        report["scope"] = "Binary integrity and recreation clip coverage; does not establish OpenMW playback."
        if args.package:
            report["package"] = package_inventory(args.package.resolve())
    except (OSError, ValueError, SyntaxError, TypeError) as error:
        print(f"Animation audit failed: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"{'PASS' if report['passed'] else 'FAIL'}: {report['clips']} clips, {report['frames']} frames, "
              f"{len(report['position_joints'])} position joints and {len(report['oriented_joints'])} oriented joints.")
        print(f"Checked {report['finite_float_samples']} finite pose values and exact binary length.")
        print(f"Expected {len(expected)} clips; missing {len(expected - clips)}, unexpected {len(clips - expected)}.")
        for key in ("missing_expected_clips", "unexpected_clips"):
            if report[key]:
                print(f"{key}: {', '.join(report[key])}")
        if "package" in report:
            package = report["package"]
            print(f"Package: {len(package['baked_eran_files'])} ERAN files, "
                  f"{len(package['loose_animation_or_model_files'])} loose KF/HKX/NIF files.")
            print(package["scope"])
        print(report["scope"])
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
