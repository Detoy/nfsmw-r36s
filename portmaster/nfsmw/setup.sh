#!/usr/bin/env bash
set -u

GAMEDIR=${1:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)}
GAMEDATA="$GAMEDIR/gamedata"
LIBDIR="$GAMEDIR/gamefiles/android-libs"
READY="$GAMEDIR/gamefiles/.ready-1.3.128"
APK_SHA=bfbe9d08165b8e976924e94879b40ac6575108d5b92521ca837175c0b291c7c7
OBB_SHA=66dd4e695e698929f789e7c825eabe3ba5a50ed2ce28b628c96e5dbc008043a1
OBB_NAME=main.1003128.com.ea.games.nfs13_row.obb

[ -f "$READY" ] && [ -s "$LIBDIR/libapp.so" ] && exit 0
command -v unzip >/dev/null 2>&1 || {
    echo "setup: unzip is required" >&2
    exit 1
}

APK=
for candidate in "$GAMEDATA"/*.apk; do
    [ -f "$candidate" ] || continue
    hash=$(sha256sum "$candidate" 2>/dev/null | awk '{print $1}')
    if [ "$hash" = "$APK_SHA" ]; then APK=$candidate; break; fi
done
OBB="$GAMEDATA/$OBB_NAME"
[ -n "$APK" ] || {
    echo "setup: matching NFS Most Wanted 1.3.128 APK was not found" >&2
    exit 1
}
[ -s "$OBB" ] || {
    echo "setup: $OBB_NAME was not found" >&2
    exit 1
}
obb_hash=$(sha256sum "$OBB" 2>/dev/null | awk '{print $1}')
[ "$obb_hash" = "$OBB_SHA" ] || {
    echo "setup: OBB hash does not match the supported release" >&2
    exit 1
}

TMP="$GAMEDIR/gamefiles/android-libs.new"
rm -rf -- "$TMP"
mkdir -p "$TMP" "$GAMEDIR/files" "$GAMEDIR/cache" \
    "$GAMEDIR/Android/data/com.ea.games.nfs13_row" || exit 1
for library in libc++_shared.so libfmodex.so libfmodevent.so libNimble.so libapp.so; do
    unzip -p "$APK" "lib/armeabi-v7a/$library" >"$TMP/$library" || exit 1
    [ -s "$TMP/$library" ] || exit 1
done
rm -rf -- "$LIBDIR"
mv -- "$TMP" "$LIBDIR" || exit 1
mkdir -p "$(dirname -- "$READY")" || exit 1
printf '%s\n' "NFS Most Wanted 1.3.128 / 1003128" >"$READY"
exit 0
