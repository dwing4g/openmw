#!/usr/bin/env python3
"""Audit and publish only the elden-combat branch to this public OpenMW fork.

Requires an authenticated GitHub CLI and Git SSH access. Never force-pushes,
uploads packages, or pushes tags. Network/API failures stop publication.
"""

import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BRANCH = "elden-combat"
UPSTREAM = "OpenMW/openmw"
OFFICIAL_URL = "git@github.com:OpenMW/openmw.git"


def run(*args, capture=False):
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None).stdout


def repo_from_remote(name):
    url = run("git", "remote", "get-url", name, capture=True).strip()
    match = re.fullmatch(r"git@github\.com:([^/]+)/([^/]+?)(?:\.git)?", url)
    if not match:
        match = re.fullmatch(r"https://github\.com/([^/]+)/([^/]+?)(?:\.git)?/?", url)
    if not match:
        raise ValueError(f"Unsupported GitHub remote URL: {url}")
    return f"{match.group(1)}/{match.group(2)}", url


def repo_info(name):
    return json.loads(run("gh", "repo", "view", name,
                          "--json", "isFork,parent,visibility,url", capture=True))


def verify_fork(info, fork):
    parent = info.get("parent") or {}
    parent_name = parent.get("nameWithOwner") or f"{parent.get('owner', {}).get('login')}/{parent.get('name')}"
    if not info["isFork"] or parent_name.lower() != UPSTREAM.lower() or info["visibility"] != "PUBLIC":
        raise ValueError(f"{fork} is not a verified public fork of {UPSTREAM}")


def publish():
    if run("git", "branch", "--show-current", capture=True).strip() != BRANCH:
        raise ValueError(f"Check out the reviewed {BRANCH} branch before publishing")
    if run("git", "status", "--porcelain", "--untracked-files=all", capture=True).strip():
        raise ValueError("Review and commit source changes first; the working tree must be clean")
    audited_commit = run("git", "rev-parse", "HEAD", capture=True).strip()
    base = run("git", "rev-parse", "origin/master", capture=True).strip()
    run("git", "merge-base", "--is-ancestor", base, audited_commit)
    run(sys.executable, str(ROOT / "CI/eldenring-audit-source.py"),
        "--base", base, "--revision", audited_commit)
    owner = run("gh", "api", "user", "--jq", ".login", capture=True).strip()
    if owner.lower() == "openmw":
        raise ValueError("Refusing to publish feature changes to the official OpenMW owner")
    remotes = run("git", "remote", capture=True).splitlines()
    if "origin" not in remotes:
        raise ValueError("Set origin to your cloned OpenMW fork before publishing")
    fork, fork_url = repo_from_remote("origin")
    if fork.partition("/")[0].lower() != owner.lower():
        raise ValueError("The origin repository does not belong to the authenticated GitHub account")
    if "upstream" in remotes and repo_from_remote("upstream")[0].lower() != UPSTREAM.lower():
        raise ValueError("The upstream remote is not the official OpenMW repository")

    # Use the clone's actual repository name; GitHub permits a fork to have a
    # custom name. A failed preflight may mean the fork is missing, so try the
    # fork action and then verify visibility and parent before touching refs.
    try:
        info = repo_info(fork)
    except subprocess.CalledProcessError:
        run("gh", "repo", "fork", UPSTREAM, "--clone=false", "--fork-name", fork.split("/", 1)[1])
        info = repo_info(fork)
    verify_fork(info, fork)
    remotes = run("git", "remote", capture=True).splitlines()
    if "upstream" not in remotes:
        run("git", "remote", "add", "upstream", OFFICIAL_URL)
    origin_now = dict(line.split(None, 1) for line in remotes).get("origin")
    if origin_now:
        current_url = run("git", "remote", "get-url", "origin", capture=True).strip()
        current_repo, _ = repo_from_remote("origin")
        if current_repo.lower() != fork.lower():
            raise ValueError("GitHub fork setup changed origin unexpectedly; inspect remotes before pushing")
    else:
        run("git", "remote", "add", "origin", fork_url)
    run("git", "remote", "set-url", "--push", "origin", fork_url)
    run("git", "-c", "push.followTags=false", "push", "--no-follow-tags", fork_url,
        f"{audited_commit}:refs/heads/{BRANCH}")
    run("git", "fetch", "origin", f"refs/heads/{BRANCH}:refs/remotes/origin/{BRANCH}")
    run("git", "branch", "--set-upstream-to", f"origin/{BRANCH}", BRANCH)
    run("gh", "repo", "edit", fork, "--default-branch", BRANCH)
    result = json.loads(run("gh", "repo", "view", fork,
                            "--json", "visibility,defaultBranchRef,url", capture=True))
    if result["visibility"] != "PUBLIC" or result["defaultBranchRef"]["name"] != BRANCH:
        raise ValueError("Push completed, but public/default-branch verification failed")
    run("git", "remote", "set-head", "origin", BRANCH)
    remote = run("git", "ls-remote", fork_url, f"refs/heads/{BRANCH}", capture=True).split()[0]
    if audited_commit != remote:
        raise ValueError("Remote branch differs from the audited local commit")
    print(f"Published {audited_commit} to {result['url']} (public fork; default branch {BRANCH})")


if __name__ == "__main__":
    try:
        publish()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"Publication stopped: {exc}")
