#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
INDEX="$PROJECT_DIR/doxygen/html/index.html"

if [[ ! -f "$INDEX" ]]; then
    echo "ERROR: Docs not built yet. Run: make docs" >&2
    exit 1
fi

if command -v xdg-open &>/dev/null; then
    xdg-open "$INDEX"
elif command -v open &>/dev/null; then
    open "$INDEX"
else
    echo "Open manually: $INDEX" >&2
fi
