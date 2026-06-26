#!/usr/bin/env bash
# Counts total lines of text files tracked by git.
# Includes all source, docs, configs — excludes binary files.
# Usage: ./tools/count_lines.sh

set -euo pipefail

git ls-files -z |
while IFS= read -r -d '' f; do
    grep -Iq . "$f" && wc -l "$f"
done | awk '{sum += $1} END {print sum, "total"}'
