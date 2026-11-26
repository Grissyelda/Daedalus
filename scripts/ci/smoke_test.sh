#!/usr/bin/env bash
# GrizzTech4 headless smoke test (Linux).
#
# Boots the engine with only GrizzTech4's own content, compiles and loads the
# test map, spawns the player and quits. Fails if any step errors.
#
# usage: smoke_test.sh <path-to-GrizzTech4-binary>
# Needs: Xvfb, a Vulkan driver (Mesa's lavapipe works without a GPU).

set -euo pipefail

BIN="$(realpath "$1")"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"; [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null || true' EXIT

cp -r "$REPO/content" "$WORK/content"
LOG="$WORK/smoke.log"

if [ -z "${DISPLAY:-}" ]; then
	Xvfb :97 -screen 0 1280x720x24 >/dev/null 2>&1 &
	XVFB_PID=$!
	export DISPLAY=:97
	sleep 2
fi

# OpenAL's null backend: CI machines have no sound card
export ALSOFT_DRIVERS=null

set +e
timeout 900 "$BIN" \
	+set fs_basepath "$WORK" +set fs_savepath "$WORK/save" \
	+set r_fullscreen 0 +set com_skipIntroVideos 1 +set editor 0 \
	+dmap testmaps/gt4_test_room \
	+map testmaps/gt4_test_room \
	+wait 5 +quit > "$LOG" 2>&1
STATUS=$?
set -e

fail() {
	echo "SMOKE TEST FAILED: $1"
	echo "----- last 60 lines of the engine log -----"
	tail -n 60 "$LOG"
	exit 1
}

[ "$STATUS" -eq 0 ] || fail "engine exited with status $STATUS"
grep -q -- "--- Common Initialization Complete ---" "$LOG" || fail "engine did not finish starting up"
grep -q "seconds for dmap" "$LOG" || fail "the test map did not compile"
grep -q "SpawnPlayer: 0" "$LOG" || fail "the player did not spawn"
if grep -E "^(ERROR|Sys_Error)" "$LOG"; then
	fail "the engine reported an error"
fi

echo "Smoke test passed: engine started standalone, compiled and loaded the test map, spawned the player and quit."
