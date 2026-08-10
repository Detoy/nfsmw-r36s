#!/usr/bin/env python3
"""Verify that every imported Bionic/Android ABI-boundary name has a bridge."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


EXACT = {
    "__errno", "__sF", "_ctype_", "_tolower_tab_", "closedir",
    "fclose", "fdopen", "fflush", "fopen", "fprintf", "fread", "fseek",
    "fseeko", "ftell", "ftello", "fwide", "fwrite", "opendir", "readdir",
    "readdir_r", "sigaction", "sigprocmask", "stat", "statfs",
    "vfprintf", "setjmp", "sigsetjmp", "longjmp", "siglongjmp",
}


def is_boundary(name: str) -> bool:
    return (
        name.startswith("pthread_")
        or name.startswith("sem_")
        or name.startswith("__pthread_")
        or name.startswith("__android_log_")
        or name.startswith("AndroidBitmap_")
        or name in EXACT
    )


def imports(directory: Path) -> set[str]:
    result: set[str] = set()
    for library in directory.glob("*.so"):
        output = subprocess.check_output(
            ["arm-unknown-linux-gnueabihf-readelf", "-Ws", str(library)],
            text=True,
        )
        for line in output.splitlines():
            fields = line.split()
            if len(fields) >= 8 and fields[6] == "UND":
                result.add(fields[7].split("@", 1)[0])
    return result


def bridges(source: Path) -> set[str]:
    text = source.read_text()
    names = set(re.findall(r'RESOLVE_FUNCTION\("([^"]+)"', text))
    names.update(re.findall(r'strcmp\(name, "([^"]+)"\)', text))
    return names


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_compat_contract.py <android-libs> <compat_bridge.c>")
        return 2
    required = {name for name in imports(Path(sys.argv[1])) if is_boundary(name)}
    provided = bridges(Path(sys.argv[2]))
    missing = sorted(required - provided)
    if missing:
        print("FAIL: missing compatibility bridges:")
        for name in missing:
            print(f"  {name}")
        return 1
    print(f"PASS: {len(required)} imported ABI-boundary APIs have bridges")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
