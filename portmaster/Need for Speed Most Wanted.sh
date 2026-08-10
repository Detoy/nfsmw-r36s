#!/usr/bin/env bash
# PORTMASTER: nfsmw.zip, Need for Speed Most Wanted.sh

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
XDG_DATA_HOME=${XDG_DATA_HOME:-${HOME:-/tmp}/.local/share}
controlfolder=
for candidate in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
    "$XDG_DATA_HOME/PortMaster" "$SCRIPT_DIR/PortMaster" /roms/ports/PortMaster; do
    if [ -f "$candidate/control.txt" ]; then
        controlfolder=$candidate
        # shellcheck disable=SC1090
        source "$controlfolder/control.txt"
        if [ -n "${CFW_NAME:-}" ] && [ -f "$controlfolder/mod_${CFW_NAME}.txt" ]; then
            # shellcheck disable=SC1090
            source "$controlfolder/mod_${CFW_NAME}.txt"
        fi
        declare -F get_controls >/dev/null 2>&1 && get_controls
        break
    fi
done

GAMEDIR="$SCRIPT_DIR/nfsmw"
if [ ! -d "$GAMEDIR" ]; then
    for candidate in "${directory:+/${directory#/}/ports/nfsmw}" \
        /roms/ports/nfsmw /sdcard/ports/nfsmw /mnt/mmc/ports/nfsmw; do
        [ -n "$candidate" ] && [ -d "$candidate" ] && { GAMEDIR=$candidate; break; }
    done
fi
cd "$GAMEDIR" || exit 1
mkdir -p logs files cache Android/data/com.ea.games.nfs13_row || exit 1
LOG="$GAMEDIR/logs/nfsmw.log"
[ -f "$LOG" ] && mv -f -- "$LOG" "$LOG.1" 2>/dev/null || true
exec >>"$LOG" 2>&1

SETUP="$GAMEDIR/setup.sh"
chmod +x "$SETUP" "$GAMEDIR/nfsmw_runtime" 2>/dev/null || true
if ! "$SETUP" "$GAMEDIR"; then
    echo "NFS Most Wanted setup failed; see gamedata/README.txt"
    sleep 8
    command -v pm_finish >/dev/null 2>&1 && pm_finish
    exit 1
fi

MALI_BLOB=
for candidate in /usr/lib/arm-linux-gnueabihf/libmali-bifrost-g31-rxp0-gbm.so \
    /usr/lib/arm-linux-gnueabihf/libMali.so \
    /usr/lib/arm-linux-gnueabihf/libmali.so.1; do
    [ -e "$candidate" ] && { MALI_BLOB=$candidate; break; }
done
if [ -n "$MALI_BLOB" ]; then
    GL_SHIM=/tmp/nfsmw-gl
    rm -rf -- "$GL_SHIM"
    mkdir -p "$GL_SHIM" || exit 1
    ln -sf "$MALI_BLOB" "$GL_SHIM/libEGL.so.1"
    ln -sf "$MALI_BLOB" "$GL_SHIM/libEGL.so"
    ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv2.so.2"
    ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv2.so"
    export LD_LIBRARY_PATH="$GL_SHIM${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

export PORT_32BIT=Y
export SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-kmsdrm}
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-alsa}
export SDL_VIDEO_GL_DRIVER=${SDL_VIDEO_GL_DRIVER:-libGLESv2.so}
export SDL_VIDEO_EGL_DRIVER=${SDL_VIDEO_EGL_DRIVER:-libEGL.so}
export SDL_GAMECONTROLLERCONFIG=${sdl_controllerconfig:-${SDL_GAMECONTROLLERCONFIG:-}}
export SDL_NO_SIGNAL_HANDLERS=1
export NFSMW_RUN_CONSTRUCTORS=1 NFSMW_RUN_JNI=1 NFSMW_RUN_GAME=1
export NFSMW_TEST_FRAMES=0
export NFSMW_PERFORMANCE_SCORE=${NFSMW_PERFORMANCE_SCORE:-20}
export NFSMW_SILENT_AUDIO=${NFSMW_SILENT_AUDIO:-1}
export NFSMW_AUDIO_OUTPUT=${NFSMW_AUDIO_OUTPUT:-1}
export NFSMW_OBB_PATH="$GAMEDIR/gamedata/main.1003128.com.ea.games.nfs13_row.obb"

command -v pm_platform_helper >/dev/null 2>&1 && \
    pm_platform_helper "$GAMEDIR/nfsmw_runtime"
"$GAMEDIR/nfsmw_runtime" "$GAMEDIR/gamefiles/android-libs"
result=$?
echo "exit_code=$result"
sync
command -v pm_finish >/dev/null 2>&1 && pm_finish
exit "$result"
