#!/usr/bin/env bash
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
PORT="$ROOT/portmaster"
DIST="$PORT/dist"
mkdir -p "$DIST" "$PORT/nfsmw/gamedata"
cp "$ROOT/runtime/build/nfsmw_mapper" "$PORT/nfsmw/nfsmw_runtime"
chmod +x "$PORT/nfsmw/nfsmw_runtime" "$PORT/nfsmw/setup.sh" \
    "$PORT/Need for Speed Most Wanted.sh"
rm -f "$DIST/nfsmw.zip"
cd "$PORT"
zip -9 -r "$DIST/nfsmw.zip" "Need for Speed Most Wanted.sh" nfsmw \
    -x 'nfsmw/gamedata/*.apk' 'nfsmw/gamedata/*.obb' \
       'nfsmw/gamefiles/*' 'nfsmw/files/*' 'nfsmw/cache/*' 'nfsmw/logs/*'
