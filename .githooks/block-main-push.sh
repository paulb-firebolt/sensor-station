#!/bin/sh
set -eu

# Run as a trunk pre-push action (see .trunk/trunk.yaml). Trunk captures the
# hook's stdin (the "<local ref> <local sha> <remote ref> <remote sha>"
# lines git normally pipes to pre-push) into TRUNK_GIT_STDIN_FILE before
# invoking action callbacks, so read the ref list from there instead of stdin.
stdin_file="${TRUNK_GIT_STDIN_FILE-}"
if [ -z "${stdin_file}" ] || [ ! -f "${stdin_file}" ]; then
	exit 0
fi

while read -r local_ref _ remote_ref _; do
	if [ "${local_ref}" = "refs/heads/main" ] || [ "${remote_ref}" = "refs/heads/main" ]; then
		echo "Refusing to push to protected branch 'main'." >&2
		echo "Push a feature branch and open a pull request instead." >&2
		exit 1
	fi
done <"${stdin_file}"
