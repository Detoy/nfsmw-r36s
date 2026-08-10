#!/usr/bin/env python3

"""Deterministic inventory for the private NFS MW ARMv7 libraries."""

from __future__ import annotations

import os
import pathlib
import re
import shutil
import subprocess
import sys


LIBRARIES = (
    "libNimble.so",
    "libapp.so",
    "libc++_shared.so",
    "libfmodevent.so",
    "libfmodex.so",
)


def find_tool(environment_name: str, candidates: tuple[str, ...]) -> str:
    configured = os.environ.get(environment_name)
    if configured:
        return configured
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return found
        path = pathlib.Path(candidate)
        if path.is_file():
            return str(path)
    raise SystemExit(f"missing tool: set {environment_name}")


NM = find_tool(
    "NFSMW_NM",
    (
        "llvm-nm",
        "arm-linux-gnueabihf-nm",
        "/Library/Developer/CommandLineTools/usr/bin/llvm-nm",
    ),
)
OBJDUMP = find_tool(
    "NFSMW_OBJDUMP",
    (
        "llvm-objdump",
        "arm-linux-gnueabihf-objdump",
        "/Library/Developer/CommandLineTools/usr/bin/llvm-objdump",
    ),
)


def run(*arguments: str) -> str:
    return subprocess.run(
        arguments,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    ).stdout


def symbols(path: pathlib.Path, defined: bool) -> set[str]:
    mode = "--defined-only" if defined else "--undefined-only"
    output = run(NM, "-D", mode, str(path))
    result: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if fields:
            result.add(fields[-1].split("@@", 1)[0])
    return result


def dynamic(path: pathlib.Path) -> tuple[list[str], dict[str, int]]:
    private = run(OBJDUMP, "-p", str(path))
    needed = re.findall(r"^\s*NEEDED\s+(\S+)", private, re.MULTILINE)
    relocations = run(OBJDUMP, "--dynamic-reloc", str(path))
    counts: dict[str, int] = {}
    for kind in re.findall(r"\b(R_ARM_[A-Z0-9_]+)\b", relocations):
        counts[kind] = counts.get(kind, 0) + 1
    return needed, counts


def family(name: str) -> str:
    if name.startswith("gl"):
        return "GLES"
    if name.startswith("egl"):
        return "EGL"
    if name.startswith("FMOD_") or name.startswith("_ZN4FMOD"):
        return "FMOD"
    if name.startswith("pthread_"):
        return "pthread"
    if name.startswith("sem_"):
        return "semaphore"
    if name.startswith("AndroidBitmap_"):
        return "Android bitmap"
    if name.startswith("__android_"):
        return "Android log"
    if re.match(r"^(_ZNK?St|_ZTI|_ZTV|_ZSt|__cxa_|__dynamic_cast)", name):
        return "C++ ABI"
    if re.match(r"^(__aeabi_|_Unwind|__gnu_Unwind)", name):
        return "ARM EABI/unwind"
    return "other"


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <android-libs-directory>", file=sys.stderr)
        return 2
    root = pathlib.Path(sys.argv[1])
    missing = [name for name in LIBRARIES if not (root / name).is_file()]
    if missing:
        print("missing: " + ", ".join(missing), file=sys.stderr)
        return 1

    imports = {name: symbols(root / name, False) for name in LIBRARIES}
    exports = {name: symbols(root / name, True) for name in LIBRARIES}
    print("module\timports\trelocations\tneeded")
    for name in LIBRARIES:
        needed, relocations = dynamic(root / name)
        relocation_count = sum(relocations.values())
        print(f"{name}\t{len(imports[name])}\t{relocation_count}\t{','.join(needed)}")
        print("  " + " ".join(f"{kind}={count}" for kind, count in sorted(relocations.items())))

    print("\nlibapp import families")
    families: dict[str, int] = {}
    for symbol in imports["libapp.so"]:
        key = family(symbol)
        families[key] = families.get(key, 0) + 1
    for key, count in sorted(families.items(), key=lambda item: (-item[1], item[0])):
        print(f"{key}\t{count}")

    bundled = set().union(*(exports[name] for name in LIBRARIES if name != "libapp.so"))
    fmod_imports = {name for name in imports["libapp.so"] if family(name) == "FMOD"}
    missing_fmod = sorted(fmod_imports - bundled)
    print(f"\nFMOD app imports\t{len(fmod_imports)}")
    print(f"FMOD satisfied by bundle\t{len(fmod_imports) - len(missing_fmod)}")
    if missing_fmod:
        print("FMOD missing from bundle")
        print("\n".join(missing_fmod))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
