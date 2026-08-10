#!/bin/bash
# PORTMASTER: nfsmw-maptest.zip, NFS Most Wanted - Mapper Test.sh

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

export PORT_32BIT="Y"

if [ -f "$controlfolder/control.txt" ]; then
  # shellcheck disable=SC1090
  source "$controlfolder/control.txt"
fi
if [ -f "$controlfolder/device_info.txt" ]; then
  # shellcheck disable=SC1090
  source "$controlfolder/device_info.txt"
fi
if [ -n "${CFW_NAME:-}" ] && [ -f "$controlfolder/mod_${CFW_NAME}.txt" ]; then
  # shellcheck disable=SC1090
  source "$controlfolder/mod_${CFW_NAME}.txt"
fi

rom_root=${directory:-roms}
GAMEDIR="/${rom_root#/}/ports/nfsmw-maptest"
if [ ! -d "$GAMEDIR" ]; then
  GAMEDIR="/roms/ports/nfsmw-maptest"
fi
LOG="$GAMEDIR/maptest.log"
MAPPER="$GAMEDIR/nfsmw_mapper"
LIBDIR="$GAMEDIR/android-libs"

cd "$GAMEDIR" || exit 1
: > "$LOG"
exec >> "$LOG" 2>&1

echo "=== NFS Most Wanted R36S consolidated gameplay test ==="
date 2>/dev/null || true
uname -a 2>/dev/null || true
echo "CFW_NAME=${CFW_NAME:-unknown}"
echo "DEVICE_ARCH=${DEVICE_ARCH:-unknown}"
echo "DEVICE_HAS_ARMHF=${DEVICE_HAS_ARMHF:-unknown}"
echo "mapper=$(file "$MAPPER" 2>/dev/null || echo unavailable)"

# The firmware's generic EGL name may select Mesa while GLES selects Mali.
# Force both through the proven 32-bit Mali blob so SDL owns one consistent
# KMSDRM context, matching the working Android compatibility ports.
MALI_BLOB=""
for candidate in \
    /usr/lib/arm-linux-gnueabihf/libmali-bifrost-g31-rxp0-gbm.so \
    /usr/lib/arm-linux-gnueabihf/libMali.so \
    /usr/lib/arm-linux-gnueabihf/libmali.so.1; do
  [ -e "$candidate" ] && { MALI_BLOB="$candidate"; break; }
done
if [ -n "$MALI_BLOB" ]; then
  GL_SHIM=/tmp/nfsmw-preflight-gl
  rm -rf "$GL_SHIM"
  if mkdir -p "$GL_SHIM" && \
     ln -sf "$MALI_BLOB" "$GL_SHIM/libEGL.so.1" && \
     ln -sf "$MALI_BLOB" "$GL_SHIM/libEGL.so" && \
     ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv2.so.2" && \
     ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv2.so"; then
    export LD_LIBRARY_PATH="$GL_SHIM${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    echo "GL: forcing the Mali blob from $MALI_BLOB"
  fi
fi

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
export SDL_VIDEO_GL_DRIVER="${SDL_VIDEO_GL_DRIVER:-libGLESv2.so}"
export SDL_VIDEO_EGL_DRIVER="${SDL_VIDEO_EGL_DRIVER:-libEGL.so}"
export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-${SDL_GAMECONTROLLERCONFIG:-}}"
export SDL_NO_SIGNAL_HANDLERS=1
export NFSMW_RUN_CONSTRUCTORS=1
export NFSMW_RUN_JNI=1
export NFSMW_RUN_GAME=1
export NFSMW_TEST_FRAMES="${NFSMW_TEST_FRAMES:-0}"
export NFSMW_PERFORMANCE_SCORE="${NFSMW_PERFORMANCE_SCORE:-20}"
export NFSMW_SILENT_AUDIO="${NFSMW_SILENT_AUDIO:-1}"
export NFSMW_AUDIO_OUTPUT="${NFSMW_AUDIO_OUTPUT:-1}"
export NFSMW_SCREENSHOT_DIR="$GAMEDIR"
export NFSMW_OBB_PATH="$GAMEDIR/main.1003128.com.ea.games.nfs13_row.obb"

mkdir -p "$GAMEDIR/files" "$GAMEDIR/cache" \
  "$GAMEDIR/Android/data/com.ea.games.nfs13_row" || result=1
if [ ! -s "$NFSMW_OBB_PATH" ]; then
  echo "FAIL: required OBB is missing: $NFSMW_OBB_PATH"
  result=1
fi

if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$MAPPER"
fi

if [ ! -x "$MAPPER" ]; then
  chmod +x "$MAPPER" || {
    echo "FAIL: could not make mapper executable"
    result=1
  }
fi

result=${result:-0}
if [ "$result" -eq 0 ]; then
  "$MAPPER" "$LIBDIR"
  result=$?
fi
echo "exit_code=$result"
sync

CUR_TTY=/dev/tty0
[ -w "$CUR_TTY" ] || CUR_TTY=/dev/tty1
if [ -w "$CUR_TTY" ]; then
  printf '\033c' > "$CUR_TTY"
  {
    echo ""
    echo "  NFS Most Wanted gameplay test"
    echo ""
    if [ "$result" -eq 0 ]; then
      echo "  PASS: game loop exited cleanly."
    else
      echo "  FAIL: mapper returned $result."
    fi
    echo ""
    echo "  Result saved to:"
    echo "  ports/nfsmw-maptest/maptest.log"
    echo ""
    echo "  Returning to Ports in 10 seconds."
  } > "$CUR_TTY"
  sleep 10
  printf '\033c' > "$CUR_TTY"
fi

command -v pm_finish >/dev/null 2>&1 && pm_finish
exit "$result"
