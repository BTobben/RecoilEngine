#!/bin/bash

set -e

CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
RESOURCES="$CONTENTS/Resources"
RUNTIME="$RESOURCES/runtime"
BAR="$RESOURCES/BAR-content"
WRITE_DIR="${BAR_WRITE_DIR:-$HOME/Library/Application Support/Beyond All Reason GL41}"
CONFIG="${BAR_CONFIG:-$BAR/common/configs/macos-gl41.cfg}"

mkdir -p "$WRITE_DIR"
export SPRING_DATADIR="$RUNTIME:$BAR"

exec "$RUNTIME/spring" \
	--write-dir="$WRITE_DIR" \
	--config="$CONFIG" \
	"$@"
