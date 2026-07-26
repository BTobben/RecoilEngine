#!/bin/bash

set -e

CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
RESOURCES="$CONTENTS/Resources"
RUNTIME="$RESOURCES/runtime"
BAR="$RESOURCES/BAR-content"
WRITE_DIR="${BAR_WRITE_DIR:-$HOME/Library/Application Support/Beyond All Reason GL41}"

mkdir -p "$WRITE_DIR"

if [ -n "${BAR_CONFIG:-}" ]; then
	CONFIG="$BAR_CONFIG"
else
	CONFIG="$WRITE_DIR/springsettings.cfg"
	if [ ! -f "$CONFIG" ]; then
		cp "$BAR/common/configs/macos-gl41.cfg" "$CONFIG"
	fi
	# Existing installations predate the standalone game-over opt-out.  Add it
	# only when absent so a deliberate user override remains respected.
	if ! grep -Eq '^[[:space:]]*AutoQuitWithoutMenu=' "$CONFIG"; then
		printf '\nAutoQuitWithoutMenu=0\n' >> "$CONFIG"
	fi
fi

export SPRING_DATADIR="$RUNTIME:$BAR"


LOG="$WRITE_DIR/infolog.txt"
GUARD_LOG="$WRITE_DIR/hang-guard.txt"
: > "$LOG"

"$RUNTIME/spring" \
	--write-dir="$WRITE_DIR" \
	--config="$CONFIG" \
	"$@" &
SPRING_PID=$!

if [ "${BAR_GL41_HANG_GUARD:-1}" = "1" ]; then
	(
		while kill -0 "$SPRING_PID" >/dev/null 2>&1; do
			if grep -Fq "[Watchdog] Hang detection triggered" "$LOG" 2>/dev/null; then
				printf '%s\n' \
					"BAR GL41 hang guard killed spring PID $SPRING_PID after the engine watchdog detected an unresponsive main thread." \
					> "$GUARD_LOG"
				kill -KILL "$SPRING_PID" >/dev/null 2>&1 || true
				exit 0
			fi
			sleep 1
		done
	) &
	GUARD_PID=$!
fi

set +e
wait "$SPRING_PID"
STATUS=$?
set -e

if [ -n "${GUARD_PID:-}" ]; then
	kill "$GUARD_PID" >/dev/null 2>&1 || true
	wait "$GUARD_PID" >/dev/null 2>&1 || true
fi

exit "$STATUS"
