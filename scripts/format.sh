#!/usr/bin/env bash
set -euo pipefail

mode="format"
clang_format="${CLANG_FORMAT:-clang-format}"

usage() {
    cat <<'EOF'
Usage: scripts/format.sh [--check] [--list] [--clang-format PATH]

Formats project-owned C/C++/Objective-C source files with clang-format.
Build outputs, vendored source, references, and common generated dependency
directories are ignored.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)
            mode="check"
            shift
            ;;
        --list)
            mode="list"
            shift
            ;;
        --clang-format)
            if [[ $# -lt 2 ]]; then
                echo "--clang-format requires a path" >&2
                exit 2
            fi
            clang_format="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$script_dir/.." && pwd)"

find_sources() {
    find "$root" \
        \( -type d \( \
            -name .cache -o \
            -name .git -o \
            -name .vs -o \
            -name .vscode -o \
            -name .worktrees -o \
            -name _deps -o \
            -name build -o \
            -name 'build-*' -o \
            -name 'cmake-build-*' -o \
            -name external -o \
            -name node_modules -o \
            -name out -o \
            -name reference -o \
            -name third_party -o \
            -name vendor \
        \) -prune \) -o \
        \( -type f \( \
            -name '*.c' -o \
            -name '*.cc' -o \
            -name '*.cpp' -o \
            -name '*.cxx' -o \
            -name '*.h' -o \
            -name '*.hh' -o \
            -name '*.hpp' -o \
            -name '*.hxx' -o \
            -name '*.inl' -o \
            -name '*.ipp' -o \
            -name '*.ixx' -o \
            -name '*.m' -o \
            -name '*.mm' \
        \) -print \) | sort
}

mapfile -t files < <(find_sources)

case "$mode" in
    list)
        for file in "${files[@]}"; do
            printf '%s\n' "${file#"$root"/}"
        done
        ;;
    check)
        if [[ ${#files[@]} -eq 0 ]]; then
            echo "No source files found."
            exit 0
        fi
        "$clang_format" --dry-run --Werror --style=file "${files[@]}"
        echo "All ${#files[@]} source file(s) are formatted."
        ;;
    format)
        if [[ ${#files[@]} -eq 0 ]]; then
            echo "No source files found."
            exit 0
        fi
        "$clang_format" -i --style=file "${files[@]}"
        echo "Formatted ${#files[@]} source file(s)."
        ;;
esac
