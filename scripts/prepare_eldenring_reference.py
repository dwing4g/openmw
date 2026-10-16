#!/usr/bin/env python3
"""Prepare the pinned, MIT-licensed extraction tools required by this fork.

Clones source code only. Existing checkouts must be at the pinned revision and
either clean or already patched. Never resets a checkout or touches game data.
"""

import argparse
from pathlib import Path
import subprocess


URL = "https://github.com/Funny-Bones/ELDEN-RING-Combat-Rewrite.git"
REVISION = "177a3b5ec14a9bde39b9ab458a3400b4038224e3"
ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "scripts/eldenring_reference.patch"


def git(destination, *args, check=True):
    return subprocess.run(["git", "-C", str(destination), *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def prepare(destination):
    destination = destination.resolve()
    if not destination.exists():
        # --no-checkout avoids ever writing the upstream default branch's assets.
        subprocess.run(["git", "clone", "--no-checkout", URL, str(destination)], check=True)
        git(destination, "checkout", "--detach", REVISION)
    if not (destination / ".git").exists():
        raise ValueError(f"Destination is not a Git checkout: {destination}")
    if git(destination, "rev-parse", "HEAD").stdout.strip() != REVISION:
        raise ValueError(f"Expected revision {REVISION}; use a separate destination for the pinned tools")
    if git(destination, "apply", "--reverse", "--check", str(PATCH), check=False).returncode == 0:
        print(f"Required source patch is already applied: {destination}")
        return
    if git(destination, "status", "--porcelain", "--untracked-files=all").stdout.strip():
        raise ValueError("Checkout has unrelated or partial changes; use a new destination")
    git(destination, "apply", "--check", str(PATCH))
    git(destination, "apply", str(PATCH))
    print(f"Prepared source tools at {REVISION}: {destination}")
    print("Game archives, timings, animations and audio must be generated locally.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path,
                        default=ROOT.parent / "ELDEN-RING-Combat-Rewrite")
    args = parser.parse_args()
    try:
        prepare(args.destination)
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"{exc}\n{getattr(exc, 'stderr', '') or ''}")


if __name__ == "__main__":
    main()
