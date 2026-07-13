#!/usr/bin/env python3
"""Prepare, tag, publish, and verify an SDK GitHub release."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

RELEASE_METADATA_PATHS = [
    ".github/workflows/release-gates.yml",
    "VERSION",
    "CHANGELOG.md",
    "conanfile.py",
    "docs/Doxyfile",
    "docs/package_consumption.md",
    "docs/package_consumption_zh.md",
    "docs/pages/cookbook.html",
    "docs/pages/troubleshooting.html",
    "packaging/vcpkg/ports/cxxmcp-sdk/vcpkg.json",
    "packaging/xmake/packages/c/cxxmcp/xmake.lua",
    "scripts/prepare_release.py",
    "scripts/publish_release.py",
]

RELEASE_ASSETS = [
    "RELEASE_NOTES.md",
    "SHA256SUMS.txt",
]


class ReleaseError(RuntimeError):
    pass


def log(message: str) -> None:
    print(message, flush=True)


def normalize_version(value: str) -> str:
    version = value.strip()
    if version.startswith("v"):
        version = version[1:]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ReleaseError(f"expected semantic version like 1.2.3, got {value!r}")
    return version


def run(
    command: list[str],
    *,
    execute: bool,
    capture: bool = False,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    rendered = " ".join(command)
    if not execute:
        log(f"[dry-run] {rendered}")
        return subprocess.CompletedProcess(command, 0, "", "")

    log(f"+ {rendered}")
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if check and completed.returncode != 0:
        details = ""
        if capture:
            details = "\n" + (completed.stdout or "") + (completed.stderr or "")
        raise ReleaseError(f"command failed ({completed.returncode}): {rendered}{details}")
    return completed


def output(command: list[str], *, execute: bool, check: bool = True) -> str:
    completed = run(command, execute=execute, capture=True, check=check)
    return completed.stdout.strip()


def git_status() -> list[str]:
    completed = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return [line for line in completed.stdout.splitlines() if line.strip()]


def require_clean_or_allowed(allow_dirty: bool) -> None:
    status = git_status()
    if status and not allow_dirty:
        details = "\n".join(status)
        raise ReleaseError(
            "working tree is dirty; commit/stash first or pass --allow-dirty:\n"
            + details
        )


def current_branch() -> str:
    return output(["git", "rev-parse", "--abbrev-ref", "HEAD"], execute=True)


def current_commit() -> str:
    return output(["git", "rev-parse", "HEAD"], execute=True)


def local_tag_commit(tag: str) -> str | None:
    completed = run(
        ["git", "rev-parse", f"{tag}^{{commit}}"],
        execute=True,
        capture=True,
        check=False,
    )
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def remote_tag_commit(tag: str) -> str | None:
    completed = run(
        ["git", "ls-remote", "--tags", "origin", f"refs/tags/{tag}"],
        execute=True,
        capture=True,
        check=False,
    )
    if completed.returncode != 0 or not completed.stdout.strip():
        return None
    return completed.stdout.split()[0]


def gh_json(command: list[str], *, execute: bool, check: bool = True) -> object:
    text = output(command, execute=execute, check=check)
    if not text:
        return None
    return json.loads(text)


def repo_name(execute: bool) -> str:
    if not execute:
        return "OWNER/REPO"
    return output(["gh", "repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner"], execute=True)


def release_exists(tag: str, execute: bool) -> bool:
    if not execute:
        log(f"[dry-run] check whether GitHub release {tag} exists")
        return False
    completed = run(
        ["gh", "release", "view", tag],
        execute=execute,
        capture=True,
        check=False,
    )
    return completed.returncode == 0


def parse_required_gate_artifacts() -> list[str]:
    text = (ROOT / ".github/workflows/release-sdk.yml").read_text(encoding="utf-8")
    match = re.search(r"required_artifacts=\(\s*(.*?)\s*\)", text, re.DOTALL)
    if not match:
        raise ReleaseError("could not find required_artifacts in release-sdk.yml")
    return re.findall(r"^\s*([A-Za-z0-9_.-]+)\s*$", match.group(1), re.MULTILINE)


def api_workflow_runs(repo: str, workflow: str, head_sha: str, execute: bool) -> list[dict[str, object]]:
    if not execute:
        return []
    data = gh_json(
        [
            "gh",
            "api",
            "--method",
            "GET",
            f"/repos/{repo}/actions/workflows/{workflow}/runs",
            "--field",
            f"head_sha={head_sha}",
            "--field",
            "per_page=10",
        ],
        execute=True,
    )
    if not isinstance(data, dict):
        return []
    runs = data.get("workflow_runs", [])
    return runs if isinstance(runs, list) else []


def api_run_artifacts(repo: str, run_id: int, execute: bool) -> list[str]:
    if not execute:
        return []
    data = gh_json(
        [
            "gh",
            "api",
            "--method",
            "GET",
            f"/repos/{repo}/actions/runs/{run_id}/artifacts",
            "--field",
            "per_page=100",
        ],
        execute=True,
    )
    if not isinstance(data, dict):
        return []
    artifacts = data.get("artifacts", [])
    names = []
    if isinstance(artifacts, list):
        for artifact in artifacts:
            if isinstance(artifact, dict) and isinstance(artifact.get("name"), str):
                names.append(str(artifact["name"]))
    return names


def wait_for_successful_run(
    repo: str,
    workflow: str,
    head_sha: str,
    *,
    execute: bool,
    timeout_seconds: int,
    poll_seconds: int,
) -> dict[str, object]:
    if not execute:
        log(f"[dry-run] wait for {workflow} success at {head_sha}")
        return {"id": 0, "html_url": f"https://github.com/{repo}/actions", "head_sha": head_sha}

    deadline = time.monotonic() + timeout_seconds
    last_seen = "not started"
    while time.monotonic() < deadline:
        runs = api_workflow_runs(repo, workflow, head_sha, execute=True)
        if runs:
            run_info = runs[0]
            status = str(run_info.get("status"))
            conclusion = str(run_info.get("conclusion"))
            url = str(run_info.get("html_url"))
            last_seen = f"{status}/{conclusion} {url}"
            log(f"{workflow}: {last_seen}")
            if status == "completed":
                if conclusion == "success":
                    return run_info
                raise ReleaseError(f"{workflow} failed: {last_seen}")
        else:
            log(f"{workflow}: waiting for run at {head_sha}")
        time.sleep(poll_seconds)
    raise ReleaseError(f"timed out waiting for {workflow}; last seen: {last_seen}")


def wait_for_release_sdk_run(
    repo: str,
    tag: str,
    *,
    execute: bool,
    timeout_seconds: int,
    poll_seconds: int,
) -> dict[str, object]:
    if not execute:
        log(f"[dry-run] wait for release-sdk.yml success at {tag}")
        return {"id": 0, "html_url": f"https://github.com/{repo}/actions"}

    deadline = time.monotonic() + timeout_seconds
    last_seen = "not started"
    while time.monotonic() < deadline:
        runs = gh_json(
            [
                "gh",
                "run",
                "list",
                "--workflow",
                "release-sdk.yml",
                "--branch",
                tag,
                "--json",
                "databaseId,status,conclusion,headSha,url,createdAt",
                "--limit",
                "5",
            ],
            execute=True,
        )
        selected = runs[0] if isinstance(runs, list) and runs else None
        if isinstance(selected, dict):
            status = str(selected.get("status"))
            conclusion = str(selected.get("conclusion"))
            url = str(selected.get("url"))
            last_seen = f"{status}/{conclusion} {url}"
            log(f"release-sdk.yml: {last_seen}")
            if status == "completed":
                if conclusion == "success":
                    return selected
                raise ReleaseError(f"release-sdk.yml failed: {last_seen}")
        else:
            log(f"release-sdk.yml: waiting for workflow_dispatch at {tag}")
        time.sleep(poll_seconds)
    raise ReleaseError(f"timed out waiting for release-sdk.yml; last seen: {last_seen}")


def verify_gate_artifacts(repo: str, run_id: int, execute: bool) -> None:
    required = set(parse_required_gate_artifacts())
    if not execute:
        log(f"[dry-run] verify release-gates artifacts: {len(required)} required")
        return
    observed = set(api_run_artifacts(repo, run_id, execute))
    missing = sorted(required - observed)
    if missing:
        raise ReleaseError("release-gates run is missing artifacts:\n" + "\n".join(missing))
    log(f"release-gates artifacts verified ({len(required)} required)")


def verify_release_assets(tag: str, version: str, execute: bool) -> str:
    required = set(RELEASE_ASSETS + [f"cxxmcp-sdk-source-v{version}.tar.gz"])
    if not execute:
        log(f"[dry-run] verify GitHub release assets for {tag}: {sorted(required)}")
        return f"https://github.com/OWNER/REPO/releases/tag/{tag}"

    data = gh_json(
        [
            "gh",
            "release",
            "view",
            tag,
            "--json",
            "assets,url,tagName",
        ],
        execute=True,
    )
    if not isinstance(data, dict):
        raise ReleaseError(f"release {tag} was not found")
    assets = data.get("assets", [])
    observed = {
        str(asset.get("name"))
        for asset in assets
        if isinstance(asset, dict) and isinstance(asset.get("name"), str)
    }
    missing = sorted(required - observed)
    if missing:
        raise ReleaseError("GitHub release is missing assets:\n" + "\n".join(missing))
    url = str(data.get("url"))
    log(f"release assets verified: {', '.join(sorted(required))}")
    return url


def delete_existing_tag(tag: str, execute: bool) -> None:
    if local_tag_commit(tag):
        run(["git", "tag", "-d", tag], execute=execute)
    if remote_tag_commit(tag):
        run(["git", "push", "origin", f":refs/tags/{tag}"], execute=execute)


def prepare_metadata(version: str, execute: bool) -> None:
    run([sys.executable, "-B", "scripts/prepare_release.py", version], execute=execute)


def stage_release_files(execute: bool) -> None:
    paths = [path for path in RELEASE_METADATA_PATHS if (ROOT / path).exists()]
    run(["git", "add", *paths], execute=execute)


def commit_release_metadata(version: str, execute: bool) -> None:
    staged = output(["git", "diff", "--cached", "--name-only"], execute=execute)
    if not staged:
        log("No staged release metadata changes.")
        return
    run(["git", "commit", "-m", f"Prepare v{version} release"], execute=execute)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("version", help="release version, for example 1.2.2 or v1.2.2")
    parser.add_argument("--execute", action="store_true", help="perform mutations; default is dry-run")
    parser.add_argument("--allow-dirty", action="store_true", help="allow pre-existing dirty files")
    parser.add_argument("--delete-existing-tag", action="store_true", help="delete an existing local/remote tag before re-tagging")
    parser.add_argument("--delete-existing-release", action="store_true", help="delete an existing GitHub release before publishing")
    parser.add_argument("--skip-prepare", action="store_true", help="skip scripts/prepare_release.py")
    parser.add_argument("--skip-commit", action="store_true", help="do not commit release metadata")
    parser.add_argument("--skip-push", action="store_true", help="do not push the current branch")
    parser.add_argument("--skip-release", action="store_true", help="stop after pushing the tag")
    parser.add_argument("--timeout-minutes", type=int, default=180, help="workflow wait timeout")
    parser.add_argument("--poll-seconds", type=int, default=30, help="workflow polling interval")
    args = parser.parse_args()

    version = normalize_version(args.version)
    tag = f"v{version}"
    execute = args.execute
    timeout_seconds = args.timeout_minutes * 60

    try:
        require_clean_or_allowed(args.allow_dirty)
        repo = repo_name(execute)
        branch = current_branch()
        log(f"release target: {repo} {tag} on {branch}")

        if release_exists(tag, execute):
            if not args.delete_existing_release:
                raise ReleaseError(f"GitHub release {tag} already exists")
            run(["gh", "release", "delete", tag, "--cleanup-tag", "--yes"], execute=execute)

        if local_tag_commit(tag) or remote_tag_commit(tag):
            if not args.delete_existing_tag:
                raise ReleaseError(f"tag {tag} already exists; pass --delete-existing-tag")
            delete_existing_tag(tag, execute)

        if not args.skip_prepare:
            prepare_metadata(version, execute)

        if not args.skip_commit:
            stage_release_files(execute)
            commit_release_metadata(version, execute)

        release_commit = current_commit()
        if not args.skip_push:
            run(["git", "push", "origin", branch], execute=execute)

        gates = wait_for_successful_run(
            repo,
            "release-gates.yml",
            release_commit,
            execute=execute,
            timeout_seconds=timeout_seconds,
            poll_seconds=args.poll_seconds,
        )
        gates_run_id = int(gates["id"])
        verify_gate_artifacts(repo, gates_run_id, execute)

        run(["git", "tag", tag, release_commit], execute=execute)
        run(["git", "push", "origin", f"refs/tags/{tag}"], execute=execute)

        if args.skip_release:
            log(f"release tag pushed: {tag} at {release_commit}")
            return 0

        run(
            [
                "gh",
                "workflow",
                "run",
                "release-sdk.yml",
                "--ref",
                tag,
                "--field",
                f"release_gates_run_id={gates_run_id}",
            ],
            execute=execute,
        )
        sdk_run = wait_for_release_sdk_run(
            repo,
            tag,
            execute=execute,
            timeout_seconds=timeout_seconds,
            poll_seconds=args.poll_seconds,
        )
        release_url = verify_release_assets(tag, version, execute)

        log("release completed")
        log(f"  tag: {tag}")
        log(f"  commit: {release_commit}")
        log(f"  release-gates: {gates.get('html_url')}")
        log(f"  release-sdk: {sdk_run.get('url') or sdk_run.get('html_url')}")
        log(f"  release: {release_url}")
        return 0
    except ReleaseError as error:
        log(f"error: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
