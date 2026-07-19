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
fi

export SPRING_DATADIR="$RUNTIME:$BAR"

exec "$RUNTIME/spring" \
	--write-dir="$WRITE_DIR" \
	--config="$CONFIG" \
	"$@"
