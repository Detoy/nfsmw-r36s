#!/bin/sh
# Run 31 - build the mapper with the same GCC cross toolchain used for runs
# 1-30 and install it on the mounted R36S card.
#
# Usage:  sh "run31/build_and_install.sh"
#
# It refuses to install anything it has not just verified, and it keeps the
# working Run-29 mapper on the card as nfsmw_mapper.run29 so a revert is one
# file copy.

set -eu

RUN_DIR=$(cd "$(dirname "$0")" && pwd)
RUNTIME_DIR="$RUN_DIR/runtime"
TOOLCHAIN_BIN=/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/bin
CARD_PORT=/Volumes/EASYROMS/ports/nfsmw-maptest

if [ ! -x "$TOOLCHAIN_BIN/arm-unknown-linux-gnueabihf-gcc" ]; then
    echo "error: cross toolchain not found at $TOOLCHAIN_BIN" >&2
    echo "       install it with: brew install arm-unknown-linux-gnueabihf" >&2
    exit 1
fi
PATH="$TOOLCHAIN_BIN:$PATH"
export PATH

echo "=== 1/5 build ==="
cd "$RUNTIME_DIR"
rm -rf build
make
file build/nfsmw_mapper

echo
echo "=== 2/5 identity ==="
NEW_SHA=$(shasum -a 256 build/nfsmw_mapper | awk '{print $1}')
NEW_ID=$(arm-unknown-linux-gnueabihf-readelf -n build/nfsmw_mapper |
         awk '/Build ID/ {print $3}')
echo "mapper SHA-256 : $NEW_SHA"
echo "GNU build ID   : $NEW_ID"

echo
echo "=== 3/5 ABI check ==="
arm-unknown-linux-gnueabihf-readelf -A build/nfsmw_mapper |
    grep -E 'Tag_CPU_arch:|Tag_ABI_VFP_args|Tag_Advanced_SIMD_arch'
arm-unknown-linux-gnueabihf-readelf -A build/nfsmw_mapper |
    grep -q 'Tag_ABI_VFP_args: VFP registers' || {
        echo "error: mapper is not tagged hard-float; refusing to install" >&2
        exit 1
    }

if [ ! -d "$CARD_PORT" ]; then
    echo
    echo "card not mounted at $CARD_PORT - build only, nothing installed."
    exit 0
fi

echo
echo "=== 4/5 install ==="
if [ ! -f "$CARD_PORT/nfsmw_mapper.run29" ]; then
    cp "$CARD_PORT/nfsmw_mapper" "$CARD_PORT/nfsmw_mapper.run29"
    echo "kept the previous mapper as nfsmw_mapper.run29"
fi
cp build/nfsmw_mapper "$CARD_PORT/nfsmw_mapper"
chmod 755 "$CARD_PORT/nfsmw_mapper"
sync

echo
echo "=== 5/5 read-back verification ==="
CARD_SHA=$(shasum -a 256 "$CARD_PORT/nfsmw_mapper" | awk '{print $1}')
echo "on-card SHA-256: $CARD_SHA"
if [ "$CARD_SHA" = "$NEW_SHA" ]; then
    echo "PASS: card matches the build."
else
    echo "FAIL: card does not match the build." >&2
    exit 1
fi
