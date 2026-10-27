#!/usr/bin/env bash
# Concatenate multiple files or directories recursively.
# Each file’s contents are preceded by a header with its path.

set -euo pipefail

# If no args, default to current directory
if [[ $# -eq 0 ]]; then
    echo "Usage: $0 [paths...] [> output.txt]"
    exit 1
fi

# Collect all files (recursively for dirs)
FILES=()
for path in "$@"; do
    if [[ -f "$path" ]]; then
        FILES+=("$path")
    elif [[ -d "$path" ]]; then
        while IFS= read -r f; do
            FILES+=("$f")
        done < <(find "$path" -type f | sort)
    else
        echo "Warning: $path not found" >&2
    fi
done

# Concatenate, adding headers
for f in "${FILES[@]}"; do
    echo "===== FILE: $f ====="
    cat "$f"
    echo -e "\n"
done

