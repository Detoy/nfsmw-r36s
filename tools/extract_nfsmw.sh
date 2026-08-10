#!/usr/bin/env bash

set -euo pipefail

EXPECTED_APK_SHA256=bfbe9d08165b8e976924e94879b40ac6575108d5b92521ca837175c0b291c7c7
EXPECTED_OBB_SHA256=66dd4e695e698929f789e7c825eabe3ba5a50ed2ce28b628c96e5dbc008043a1
EXPECTED_PACKAGE=com.ea.games.nfs13_row
EXPECTED_VERSION=1.3.128
EXPECTED_VERSION_CODE=1003128
MARKER=.nfsmw-1.3.128-ready

expected_library_hash() {
    case "$1" in
        libNimble.so) printf '%s\n' ace73446e2e526a5dd1f098cb4d50dae5f18a83706856d91337c588ff5e6d4b6 ;;
        libapp.so) printf '%s\n' a73dcc170552866af52bf2d4f5822606307fb42aed1ef0df2e469fdfa374ff43 ;;
        libc++_shared.so) printf '%s\n' ee86a2cb55d2d205260793c67997cdd9cf3d2e7588ecf7c122e4790b7c2c2005 ;;
        libfmodevent.so) printf '%s\n' cfb17d2de3d95732a364c29ee032a501eabe26b9fc7af47433f5537fa85c6ce0 ;;
        libfmodex.so) printf '%s\n' 7c29ab877deb862c0154b548a057d35eb70cd0cf20b714054b65921e6961a6e8 ;;
        *) return 1 ;;
    esac
}

usage() {
    printf 'Usage: %s <game.apk> <main.1003128.com.ea.games.nfs13_row.obb> <output-directory>\n' "$0" >&2
    printf '       %s --verify <output-directory>\n' "$0" >&2
}

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

require_safe_output() {
    case "$1" in
        ''|/|.|..) printf 'Unsafe output directory: %s\n' "$1" >&2; return 1 ;;
    esac
}

verify_output() {
    local root=$1 marker="$1/$MARKER" name expected marker_hash actual
    [[ -d $root ]] || { printf 'Missing output directory: %s\n' "$root" >&2; return 1; }
    [[ -f $marker ]] || { printf 'Missing readiness marker: %s\n' "$marker" >&2; return 1; }

    for name in libNimble.so libapp.so libc++_shared.so libfmodevent.so libfmodex.so; do
        [[ -s "$root/android-libs/$name" ]] || {
            printf 'Missing ARMv7 library: %s\n' "$name" >&2
            return 1
        }
        actual=$(hash_file "$root/android-libs/$name")
        expected=$(expected_library_hash "$name")
        marker_hash=$(awk -F= -v key="$name.sha256" '$1 == key { print $2 }' "$marker")
        [[ $actual == "$expected" && $marker_hash == "$expected" ]] || {
            printf 'Hash mismatch for %s\n' "$name" >&2
            return 1
        }
    done

    [[ -s "$root/main.$EXPECTED_VERSION_CODE.$EXPECTED_PACKAGE.obb" ]] || {
        printf 'Missing OBB in %s\n' "$root" >&2
        return 1
    }
    actual=$(hash_file "$root/main.$EXPECTED_VERSION_CODE.$EXPECTED_PACKAGE.obb")
    [[ $actual == "$EXPECTED_OBB_SHA256" ]] || {
        printf 'OBB hash mismatch\n' >&2
        return 1
    }
    printf 'PASS: verified NFS Most Wanted %s private files in %s\n' "$EXPECTED_VERSION" "$root"
}

for tool in awk cp find mkdir mktemp mv rm rmdir unzip; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing required tool: %s\n' "$tool" >&2
        exit 1
    }
done
command -v sha256sum >/dev/null 2>&1 || command -v shasum >/dev/null 2>&1 || {
    printf 'Missing SHA-256 utility\n' >&2
    exit 1
}

if [[ ${1:-} == --verify ]]; then
    [[ $# -eq 2 ]] || { usage; exit 2; }
    verify_output "$2"
    exit 0
fi

[[ $# -eq 3 ]] || { usage; exit 2; }
apk=$1
obb=$2
output=$3
require_safe_output "$output"
[[ -f $apk ]] || { printf 'APK not found: %s\n' "$apk" >&2; exit 1; }
[[ -f $obb ]] || { printf 'OBB not found: %s\n' "$obb" >&2; exit 1; }

apk_hash=$(hash_file "$apk")
obb_hash=$(hash_file "$obb")
[[ $apk_hash == "$EXPECTED_APK_SHA256" ]] || {
    printf 'Unsupported APK hash: %s\n' "$apk_hash" >&2
    printf 'Expected NFS Most Wanted %s, not a PPSSPP repack or another release.\n' "$EXPECTED_VERSION" >&2
    exit 1
}
[[ $obb_hash == "$EXPECTED_OBB_SHA256" ]] || {
    printf 'Unsupported OBB hash: %s\n' "$obb_hash" >&2
    exit 1
}

mkdir -p "$output"
output=$(cd "$output" && pwd -P)
stage=$(mktemp -d "$output/.nfsmw-stage.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 130' INT TERM HUP
mkdir -p "$stage/android-libs" "$stage/apk-assets"

unzip -q "$apk" 'lib/armeabi-v7a/*' -d "$stage/apk"
for library in "$stage"/apk/lib/armeabi-v7a/*.so; do
    mv "$library" "$stage/android-libs/"
done
unzip -q "$apk" 'assets/*' -d "$stage/apk-assets"
if [[ -d "$stage/apk-assets/assets" ]]; then
    mv "$stage/apk-assets/assets" "$stage/assets"
    rmdir "$stage/apk-assets"
fi
cp -p "$obb" "$stage/main.$EXPECTED_VERSION_CODE.$EXPECTED_PACKAGE.obb"

{
    printf 'package=%s\n' "$EXPECTED_PACKAGE"
    printf 'version=%s\n' "$EXPECTED_VERSION"
    printf 'versionCode=%s\n' "$EXPECTED_VERSION_CODE"
    printf 'apk.sha256=%s\n' "$apk_hash"
    printf 'obb.sha256=%s\n' "$obb_hash"
    for library in "$stage"/android-libs/*.so; do
        printf '%s.sha256=%s\n' "${library##*/}" "$(hash_file "$library")"
    done
} >"$stage/$MARKER"

for item in android-libs assets "main.$EXPECTED_VERSION_CODE.$EXPECTED_PACKAGE.obb" "$MARKER"; do
    [[ ! -e "$output/$item" ]] || {
        printf 'Refusing to overwrite existing path: %s\n' "$output/$item" >&2
        exit 1
    }
done
for item in android-libs assets "main.$EXPECTED_VERSION_CODE.$EXPECTED_PACKAGE.obb" "$MARKER"; do
    mv "$stage/$item" "$output/$item"
done

verify_output "$output"
