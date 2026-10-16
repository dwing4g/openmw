#!/usr/bin/env python3
"""Check Git objects before publishing this source-only fork.

With no arguments, inspect the staged changes against HEAD, not working files.
Use --base <upstream-commit> --revision HEAD to inspect every changed blob in
every intervening commit, including files subsequently deleted. The original
OpenMW checkout is the default publication baseline. No working files, index
entries, Git configuration, or commits are modified.

This is a conservative publication guard, not a license or provenance oracle.
Passing does not replace reviewing source provenance and the staged diff.
It deliberately rejects new media, archives, generated game tables, unusual
file types, symlinks and submodules. The only permitted added binary is the
hash-pinned, BSD-licensed ww2ogg codebook, with its matching license notice.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import unittest


UPSTREAM_BASE = "8287d441aced614dcb0e6463c488fc399a66f149"
MAX_TEXT_BYTES = 512 * 1024
CODEBOOK_PATH = "scripts/eldenring_music/ww2ogg/packed_codebooks_aoTuV_603.bin"
CODEBOOK_SHA256 = "00a93eab267d281401b1efd54e888a2e183299b9e6c446c48d09f701a89d9d27"
CODEBOOK_LICENSE = "scripts/eldenring_music/ww2ogg/COPYING"
CODEBOOK_LICENSE_SHA256 = "0a12f3490863fa01539f500fc74a0e8efbfeb22852aac087c645fbd7362d6726"
TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".cmake",
    ".txt", ".md", ".rst", ".lua", ".py", ".sh", ".ps1", ".json",
    ".yml", ".yaml", ".xml", ".ui", ".cfg", ".manifest", ".patch",
    ".diff", ".toml", ".in", ".rs", ".gitignore", ".gitattributes",
}
TEXT_NAMES = {".gitignore", ".gitattributes", "LICENSE", "COPYING", "AUTHORS", "README"}
FORBIDDEN_NAMES = {
    "imported_timings.hpp", "extracted.rs", "player_anims.bin", "eldenring_cells.lua",
    "extraction-manifest.json", "regulation.bin", "credentials", "id_rsa", "id_ed25519",
}
FORBIDDEN_DIRECTORIES = {
    "local-assets", "er-files", "deps", "prebuilt", "__pycache__", ".venv", "venv",
    ".ssh", ".aws", ".codex", "node_modules",
}
FORBIDDEN_SUFFIXES = {
    ".anibnd", ".dcx", ".hkx", ".tae", ".wem", ".bnk", ".bhd", ".bdt",
    ".bnd", ".param", ".esm", ".esp", ".bsa", ".ba2", ".nif", ".kf",
    ".dds", ".tga", ".wav", ".ogg", ".mp3", ".flac", ".mp4", ".webm",
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".bin", ".exe", ".dll",
    ".so", ".dylib", ".lib", ".pdb", ".obj", ".o", ".a", ".zip", ".7z",
    ".rar", ".tar", ".gz", ".xz", ".bz2", ".zst", ".pem", ".key",
    ".omwsave", ".pyc", ".dmp", ".log",
}
SECRET_PATTERNS = (
    re.compile(r"-----BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY-----"),
    re.compile(r"\bgh[pousr]_[A-Za-z0-9]{25,}\b"),
    re.compile(r"\bgithub_pat_[A-Za-z0-9_]{25,}\b"),
    re.compile(r"\bAKIA[A-Z0-9]{16}\b"),
    re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{30,}\b"),
    re.compile(r"(?i)(?:password|api_key|api_secret|access_token)\s*[:=]\s*"
               r"[\"'][^\"'\n]{12,}[\"']"),
)
GAME_TABLE_MARKERS = (
    re.compile(r"(?m)^// Generated from a local extracted\.rs\."),
    re.compile(r"(?m)^#define OPENMW_COMPONENTS_ELDENCOMBAT_IMPORTED_TIMINGS_H\b"),
    re.compile(r"(?m)^-- Interior record IDs from Morrowind\.esm, Tribunal\.esm and Bloodmoon\.esm\."),
)


class AuditError(Exception):
    pass


@dataclass(frozen=True)
class Entry:
    mode: str
    oid: str
    path: str
    source: str


def git(root: Path, *args: str) -> bytes:
    result = subprocess.run(["git", "-C", str(root), *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise AuditError(f"git {args[0]} failed: {result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def raw_entries(data: bytes, source: str) -> list[Entry]:
    fields = data.split(b"\0")
    entries = []
    position = 0
    while position < len(fields) and fields[position]:
        header = fields[position].decode("ascii").split()
        if len(header) != 5 or not header[0].startswith(":") or position + 1 >= len(fields):
            raise AuditError("Unexpected Git raw diff format")
        try:
            path = fields[position + 1].decode("utf-8")
        except UnicodeDecodeError as error:
            raise AuditError("Changed filename is not UTF-8") from error
        if header[4] == "U":
            raise AuditError(f"Unmerged index entry: {path!r}")
        if header[4] != "D":
            entries.append(Entry(header[1], header[3], path, source))
        position += 2
    return entries


def path_problem(path: str) -> str | None:
    parts = PurePosixPath(path).parts
    lower = tuple(part.lower() for part in parts)
    if not parts or path.startswith("/") or ".." in parts or any(ord(c) < 32 for c in path):
        return "unsafe or unusual filename"
    if any(part in FORBIDDEN_DIRECTORIES or part.startswith("build-") or part == "build"
           or part.startswith("openmw-eldencombat-windows-") for part in lower):
        return "local asset, dependency, build, or private directory"
    name = lower[-1]
    if name in FORBIDDEN_NAMES or name == ".env" or name.startswith(".env."):
        return "generated game data or private configuration filename"
    if path == CODEBOOK_PATH:
        return None
    if PurePosixPath(name).suffix in FORBIDDEN_SUFFIXES:
        return "media, game data, archive, key, or build output extension"
    if PurePosixPath(name).suffix not in TEXT_SUFFIXES and parts[-1] not in TEXT_NAMES:
        return "file type is outside the source-text allowlist; review before extending the guard"
    return None


def inspect_blob(entry: Entry, data: bytes) -> list[str]:
    problems = []
    if entry.mode not in {"100644", "100755"}:
        return ["symlink, submodule, or nonregular Git mode"]
    issue = path_problem(entry.path)
    if issue:
        problems.append(issue)
    if entry.path == CODEBOOK_PATH:
        if hashlib.sha256(data).hexdigest() != CODEBOOK_SHA256:
            problems.append("ww2ogg codebook does not match the reviewed BSD upstream hash")
        return problems
    if len(data) > MAX_TEXT_BYTES:
        return problems + [f"text exceeds the {MAX_TEXT_BYTES}-byte review limit"]
    if b"\0" in data:
        return problems + ["binary content"]
    try:
        content = data.decode("utf-8")
    except UnicodeDecodeError:
        return problems + ["non-UTF-8 or binary content"]
    if content.startswith("version https://git-lfs.github.com/spec/v1"):
        problems.append("Git LFS pointer conceals content from this audit")
    if any(pattern.search(content) for pattern in SECRET_PATTERNS):
        problems.append("possible credential or private key (value not printed)")
    if any(pattern.search(content) for pattern in GAME_TABLE_MARKERS):
        problems.append("generated proprietary table marker")
    if any(len(line) > 4096 for line in content.splitlines()):
        problems.append("unusually long text line; possible embedded data")
    if re.search(r"(?<![A-Za-z0-9+/])[A-Za-z0-9+/]{256,}={0,2}(?![A-Za-z0-9+/])", content):
        problems.append("long encoded payload; requires source provenance review")
    if PurePosixPath(entry.path).suffix in {".patch", ".diff"}:
        for line in content.splitlines():
            if line.startswith("+++ ") and line[4:] != "/dev/null":
                nested = line[4:].split("\t", 1)[0].removeprefix("b/")
                if path_problem(nested):
                    problems.append(f"patch contains a restricted destination: {nested!r}")
            if line.startswith("+") and any(pattern.search(line[1:]) for pattern in GAME_TABLE_MARKERS):
                problems.append("patch embeds a generated proprietary table")
    return sorted(set(problems))


def audit(root: Path, base: str | None = None, revision: str | None = None) -> tuple[int, int, list[str]]:
    entries = []
    commits = 0
    if revision:
        baseline = git(root, "rev-parse", "--verify", (base or UPSTREAM_BASE) + "^{commit}").decode().strip()
        tip = git(root, "rev-parse", "--verify", revision + "^{commit}").decode().strip()
        git(root, "merge-base", "--is-ancestor", baseline, tip)
        revisions = git(root, "rev-list", "--reverse", f"{baseline}..{tip}").decode().splitlines()
        for commit in revisions:
            entries.extend(raw_entries(git(root, "diff-tree", "--no-commit-id", "--root", "-r", "-m",
                                           "--raw", "-z", "--no-abbrev", "--no-renames", commit), commit))
        commits = len(revisions)
    else:
        baseline = git(root, "rev-parse", "--verify", (base or "HEAD") + "^{commit}").decode().strip()
        entries = raw_entries(git(root, "diff", "--cached", "--raw", "-z", "--no-abbrev",
                                  "--no-renames", baseline), ":index")
    failures = []
    seen = set()
    for entry in entries:
        key = (entry.path, entry.oid, entry.mode)
        if key in seen:
            continue
        seen.add(key)
        if entry.mode not in {"100644", "100755"}:
            problems = ["symlink, submodule, or nonregular Git mode"]
        else:
            size = int(git(root, "cat-file", "-s", entry.oid))
            if size > MAX_TEXT_BYTES:
                problems = [f"blob exceeds the {MAX_TEXT_BYTES}-byte review limit"]
            else:
                problems = inspect_blob(entry, git(root, "cat-file", "blob", entry.oid))
        if entry.path == CODEBOOK_PATH:
            license_ref = ":" + CODEBOOK_LICENSE if entry.source == ":index" else entry.source + ":" + CODEBOOK_LICENSE
            try:
                notice = git(root, "show", license_ref)
                if hashlib.sha256(notice).hexdigest() != CODEBOOK_LICENSE_SHA256:
                    problems.append("codebook BSD notice differs from reviewed upstream notice")
            except AuditError:
                problems.append("codebook BSD license notice is missing from the audited tree")
        failures.extend(f"{entry.path!r} ({entry.source[:12]}): {problem}" for problem in problems)
    # A later commit (or staged deletion) must not remove the notice while
    # retaining a previously accepted codebook blob.
    final_prefix = tip + ":" if revision else ":"
    try:
        git(root, "rev-parse", "--verify", final_prefix + CODEBOOK_PATH)
    except AuditError:
        pass
    else:
        try:
            notice = git(root, "show", final_prefix + CODEBOOK_LICENSE)
            if hashlib.sha256(notice).hexdigest() != CODEBOOK_LICENSE_SHA256:
                failures.append("Final audited tree: ww2ogg BSD license notice differs from upstream")
        except AuditError:
            failures.append("Final audited tree: ww2ogg BSD license notice is missing")
    return len(seen), commits, failures


class GuardTests(unittest.TestCase):
    def test_text_and_binary_rules(self):
        def check(path, data, mode="100644"):
            return inspect_blob(Entry(mode, "", path, "test"), data)
        self.assertEqual(check("scripts/example.py", b"print('source')\n"), [])
        self.assertTrue(check("scripts/payload.py", b"ERAN\0\0"))
        self.assertTrue(check("files/player_anims.bin", b"ERAN"))
        self.assertTrue(check("components/eldencombat/imported_timings.hpp", b"// local\n"))
        self.assertTrue(check("scripts/file.py", b"../outside", "120000"))
        self.assertTrue(check("scripts/file.py", b"version https://git-lfs.github.com/spec/v1\n"))
        self.assertTrue(check("scripts/file.py", ("ghp_" + "a" * 36).encode()))
        self.assertTrue(check(CODEBOOK_PATH, b"not the reviewed codebook"))
        self.assertTrue(check("scripts/file.py", b"x" * (MAX_TEXT_BYTES + 1)))
        self.assertTrue(check("patches/source.patch", b"+++ b/src/sim/extracted.rs\n+data\n"))

    def test_index_and_deleted_historical_asset(self):
        with tempfile.TemporaryDirectory(prefix="elden-source-audit-") as directory:
            root = Path(directory)
            git(root, "init", "-q")
            git(root, "config", "user.name", "Audit Test")
            git(root, "config", "user.email", "audit@example.invalid")
            (root / "README.md").write_text("Synthetic source fixture\n")
            git(root, "add", "README.md")
            git(root, "-c", "commit.gpgsign=false", "commit", "-qm", "base")
            base = git(root, "rev-parse", "HEAD").decode().strip()
            (root / "README.md").write_text("Updated synthetic source\n")
            git(root, "add", "README.md")
            (root / "README.md").write_bytes(b"working tree binary\0")
            self.assertEqual(audit(root)[2], [])  # Inspect staged bytes, not working files.
            (root / "player_anims.bin").write_bytes(b"synthetic ERAN fixture\0")
            git(root, "add", "player_anims.bin")
            self.assertTrue(audit(root)[2])
            git(root, "-c", "commit.gpgsign=false", "commit", "-qm", "unsafe historical asset")
            git(root, "rm", "-q", "player_anims.bin")
            git(root, "-c", "commit.gpgsign=false", "commit", "-qm", "delete asset")
            count, commits, failures = audit(root, base, "HEAD")
            self.assertGreaterEqual(count, 2)
            self.assertEqual(commits, 2)
            self.assertTrue(any("player_anims.bin" in message for message in failures))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", help="trusted upstream commit (HEAD for staged checks)")
    parser.add_argument("--revision", help="audit every new commit through this revision, instead of the index")
    parser.add_argument("--self-test", action="store_true", help="exercise the guard with synthetic temporary Git data")
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(GuardTests))
        return 0 if result.wasSuccessful() else 1
    try:
        root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel").decode().strip())
        count, commits, failures = audit(root, args.base, args.revision)
    except (AuditError, OSError, ValueError) as error:
        print(f"Source audit could not complete: {error}", file=sys.stderr)
        return 2
    if failures:
        for failure in failures:
            print(f"REJECT: {failure}", file=sys.stderr)
        print(f"Source audit failed: {len(failures)} finding(s).", file=sys.stderr)
        return 1
    scope = f"{commits} new commit(s)" if args.revision else "the Git index"
    print(f"Source audit passed: {count} changed file/blob version(s) checked in {scope}.")
    print("This checks publication hazards; manually review provenance and the diff before publishing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
