#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"

git config --local core.hooksPath "$root/scripts/githooks"
echo "core.hooksPath set to scripts/githooks"
