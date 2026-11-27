#!/usr/bin/env bash
# Daedalus editor tests (Linux, headless).
#
# Each folder in scripts/test/editor/ is one test:
#   setup.sh      optional; run first with $WORK (the test's game folder root) and $ASSETS
#   test.cfg      console commands run in the editor; must end with "quit".
#                 @ASSETS@ is replaced by the folder setup.sh wrote into.
#   expect.txt    lines the log must contain (fixed text). A line starting with "!"
#                 must not appear; "#" starts a comment.
#   game.args     optional; a second run with the editor off and these arguments
#                 (e.g. +dmap and +map of a level the first run saved)
#   game.expect   what that second run's log must contain, as expect.txt
#
# usage: run_editor_tests.sh <path-to-binary> [test ...]
# Needs: Xvfb, a Vulkan driver (Mesa's lavapipe works without a GPU), python3.

set -uo pipefail

BIN="$(realpath "$1")"
shift
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
TESTS=("$@")
if [ ${#TESTS[@]} -eq 0 ]; then
	for d in "$HERE"/editor/*/; do TESTS+=("$(basename "$d")"); done
fi

if [ -z "${DISPLAY:-}" ]; then
	Xvfb :96 -screen 0 1920x1080x24 >/dev/null 2>&1 &
	XVFB_PID=$!
	export DISPLAY=:96
	sleep 2
fi
export ALSOFT_DRIVERS=null

check() {	# check <log> <expect file>; prints what's missing, returns 1 if anything is
	local log="$1" expect="$2" bad=0
	while IFS= read -r line || [ -n "$line" ]; do
		[ -z "$line" ] && continue
		case "$line" in
			\#*) continue ;;
			!*) if grep -qF -- "${line:1}" "$log"; then echo "    found what shouldn't be there: ${line:1}"; bad=1; fi ;;
			*) if ! grep -qF -- "$line" "$log"; then echo "    missing: $line"; bad=1; fi ;;
		esac
	done < "$expect"
	return $bad
}

failed=0
for name in "${TESTS[@]}"; do
	dir="$HERE/editor/$name"
	WORK="$(mktemp -d)"
	ASSETS="$WORK/assets"
	export WORK ASSETS REPO
	mkdir -p "$ASSETS"
	cp -r "$REPO/content" "$WORK/content"
	if [ -f "$dir/setup.sh" ]; then
		bash "$dir/setup.sh" || { echo "FAIL $name: setup failed"; failed=1; continue; }
	fi
	sed "s|@ASSETS@|$ASSETS|g" "$dir/test.cfg" > "$WORK/content/editortest.cfg"

	LOG="$WORK/editor.log"
	timeout -s KILL 900 "$BIN" \
		+set fs_basepath "$WORK" +set fs_savepath "$WORK/save" \
		+set r_fullscreen 0 +set com_skipIntroVideos 1 \
		+set r_windowWidth 1280 +set r_windowHeight 800 +set editor_showWelcome 0 \
		+wait 15 +exec editortest.cfg > "$LOG" 2>&1
	status=$?
	ok=1
	[ "$status" -eq 0 ] || { echo "    the editor exited with status $status"; ok=0; }
	check "$LOG" "$dir/expect.txt" || ok=0

	if [ "$ok" -eq 1 ] && [ -f "$dir/game.args" ]; then
		GLOG="$WORK/game.log"
		# shellcheck disable=SC2046
		timeout -s KILL 900 "$BIN" \
			+set fs_basepath "$WORK" +set fs_savepath "$WORK/save" \
			+set r_fullscreen 0 +set com_skipIntroVideos 1 +set editor 0 \
			$(cat "$dir/game.args") +wait 10 +quit > "$GLOG" 2>&1
			status=$?
		[ "$status" -eq 0 ] || { echo "    the game exited with status $status"; ok=0; }
		[ -f "$dir/game.expect" ] && { check "$GLOG" "$dir/game.expect" || ok=0; }
		LOG="$GLOG"
	fi

	if [ "$ok" -eq 1 ]; then
		echo "PASS $name"
		[ -n "${KEEP:-}" ] || rm -rf "$WORK"	# KEEP=1 keeps the logs of passing tests too
	else
		echo "FAIL $name (log: $LOG)"
		echo "----- last 40 lines -----"
		tail -n 40 "$LOG"
		failed=1
	fi
done

[ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
exit $failed
