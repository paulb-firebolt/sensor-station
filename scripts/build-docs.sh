#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

if ! command -v doxygen &>/dev/null; then
    echo "ERROR: doxygen not found. Install with: sudo pacman -S doxygen" >&2
    exit 1
fi

echo "Building API docs..."
doxygen Doxyfile
echo "Done. Output: doxygen/html/index.html"
