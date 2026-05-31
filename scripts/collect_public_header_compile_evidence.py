#!/usr/bin/env python3
"""Collect public-header fixture build timings for release evidence."""

from __future__ import annotations

import argparse
import datetime as dt
import json
from pathlib import Path
import subprocess
import time


DEFAULT_TARGETS = [
    "mcp_public_header_protocol",
    "mcp_public_header_error",
    "mcp_public_header_config",
    "mcp_public_header_auth",
    "mcp_public_header_transport",
    "mcp_public_header_websocket_transport",
    "mcp_public_header_client",
    "mcp_public_header_server",
    "mcp_public_header_peer",
    "mcp_public_header_handler",
    "mcp_public_header_service",
    "mcp_public_header_sdk",
]


def fail(message: str) -> None:
    raise SystemExit(f"public header compile evidence failed: {message}")


def build_target(
    build_dir: Path, target: str, config: str | None, parallel: int
) -> dict[str, object]:
    command = ["cmake", "--build", str(build_dir)]
    if config:
        command.extend(["--config", config])
    command.extend(["--target", target, "--parallel", str(parallel)])

    started = time.perf_counter()
    result = subprocess.run(
        command,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        check=False,
    )
    elapsed = time.perf_counter() - started

    entry: dict[str, object] = {
        "target": target,
        "elapsed_seconds": round(elapsed, 6),
        "returncode": result.returncode,
        "command": command,
    }
    if result.stdout:
        entry["stdout_tail"] = result.stdout[-4000:]
    if result.stderr:
        entry["stderr_tail"] = result.stderr[-4000:]
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="")
        fail(f"{target} failed with exit code {result.returncode}")
    return entry


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--config")
    parser.add_argument("--parallel", type=int, default=1)
    parser.add_argument("--target", action="append", dest="targets")
    args = parser.parse_args()

    if args.parallel < 1:
        fail("--parallel must be positive")

    build_dir = Path(args.build_dir).resolve()
    if not build_dir.is_dir():
        fail(f"build directory does not exist: {build_dir}")

    targets = args.targets or DEFAULT_TARGETS
    entries = [
        build_target(build_dir, target, args.config, args.parallel)
        for target in targets
    ]

    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema_version": 1,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "build_dir": str(build_dir),
        "config": args.config,
        "parallel": args.parallel,
        "targets": entries,
    }
    output.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
    print(f"wrote {output}")


if __name__ == "__main__":
    main()
