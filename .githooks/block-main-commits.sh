#!/bin/sh
set -eu

# Run as a trunk pre-commit action (see .trunk/trunk.yaml) rather than via
# core.hooksPath directly -- `trunk git-hooks sync` resets core.hooksPath to
# its own managed directory, which previously made a plain .githooks
# entrypoint silently stop firing. Trunk actions survive that sync.
if [ "$(git rev-parse --abbrev-ref HEAD)" = "main" ]; then
	echo "Refusing to commit directly on protected branch 'main'." >&2
	echo "Create a feature branch first." >&2
	exit 1
fi
