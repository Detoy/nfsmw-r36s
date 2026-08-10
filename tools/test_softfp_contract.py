#!/usr/bin/env python3

"""Ensure every imported scalar-float host API is routed through a thunk."""

from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
import sys


GLES_SCALAR_FLOAT = {
    "glBlendColor",
    "glClearColor",
    "glClearDepthf",
    "glDepthRangef",
    "glLineWidth",
    "glPolygonOffset",
    "glSampleCoverage",
    "glTexParameterf",
    "glUniform1f",
    "glUniform2f",
    "glUniform3f",
    "glUniform4f",
    "glVertexAttrib1f",
    "glVertexAttrib2f",
    "glVertexAttrib3f",
    "glVertexAttrib4f",
}

MATH_SCALAR_FLOAT = {
    "acos", "acosf", "asinf", "atan2", "atan2f", "ceil", "ceilf",
    "cos", "cosf", "exp", "expf", "floor", "floorf", "fmod", "fmodf",
    "frexp", "ldexp", "log", "log10", "log10f", "lrintf", "modf", "pow",
    "powf", "rint", "roundf", "sin", "sinf", "sqrt", "sqrtf", "strtod",
    "tan", "tanf",
}


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <android-libs> <softfp_symbols.c>", file=sys.stderr)
        return 2
    library_root = pathlib.Path(sys.argv[1])
    table_path = pathlib.Path(sys.argv[2])
    nm = (
        shutil.which("llvm-nm")
        or shutil.which("arm-linux-gnueabihf-nm")
        or "/Library/Developer/CommandLineTools/usr/bin/llvm-nm"
    )
    if not pathlib.Path(nm).is_file():
        print("missing llvm-nm or arm-linux-gnueabihf-nm", file=sys.stderr)
        return 1
    imported: set[str] = set()
    for library in sorted(library_root.glob("*.so")):
        result = subprocess.run(
            [
                nm,
                "-D",
                "--undefined-only",
                str(library),
            ],
            check=True,
            stdout=subprocess.PIPE,
            text=True,
        )
        for line in result.stdout.splitlines():
            fields = line.split()
            if fields:
                imported.add(fields[-1].split("@", 1)[0])
    table_text = table_path.read_text(encoding="utf-8")
    routed = set(re.findall(r'SOFTFP_SYMBOL\("([^"]+)"', table_text))
    required = (GLES_SCALAR_FLOAT | MATH_SCALAR_FLOAT) & imported
    missing = sorted(required - routed)
    stale = sorted(routed - imported)
    if missing:
        print("missing softfp routes:", ", ".join(missing), file=sys.stderr)
        return 1
    if stale:
        print("stale softfp routes:", ", ".join(stale), file=sys.stderr)
        return 1
    print(f"PASS: {len(required)} imported scalar-float APIs have ARM softfp thunks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
