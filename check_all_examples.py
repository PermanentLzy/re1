#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Run compiler on all examples and report any failures + inspect asm."""
import subprocess, os, sys

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"
EX_DIR = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\examples"

def main():
    files = sorted(f for f in os.listdir(EX_DIR) if f.endswith(".src"))
    print(f"Found {len(files)} .src files\n")
    fail = 0
    for f in files:
        with open(os.path.join(EX_DIR, f), "r", encoding="utf-8") as fh:
            src = fh.read()
        try:
            r = subprocess.run([COMPILER, "-opt"], input=src, capture_output=True,
                               text=True, encoding="utf-8", errors="replace", timeout=10)
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT {f}"); fail += 1; continue
        if r.returncode != 0:
            err = (r.stderr or "").strip().splitlines()
            msg = err[0] if err else "unknown"
            print(f"FAIL {f:35} {msg}")
            fail += 1
        else:
            lines = [l for l in (r.stdout or "").split("\n") if l.strip()]
            print(f"OK   {f:35} ({len(lines)} asm lines)")
    print(f"\n{fail}/{len(files)} failures")

if __name__ == "__main__":
    main()
