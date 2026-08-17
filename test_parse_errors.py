#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test parser on all example .src files and report parse errors."""
import subprocess
import os
from pathlib import Path

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"
EX_DIR = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\examples"

def main():
    src_files = sorted([f for f in os.listdir(EX_DIR) if f.endswith('.src')])
    print(f"Found {len(src_files)} .src files\n")
    failures = []
    for fname in src_files:
        fpath = os.path.join(EX_DIR, fname)
        with open(fpath, 'r', encoding='utf-8') as f:
            src = f.read()
        try:
            result = subprocess.run(
                [COMPILER, "-opt"],
                input=src,
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=10
            )
            if result.returncode != 0:
                err = (result.stderr or '').strip().splitlines()
                err_msg = err[0] if err else "unknown"
                print(f"FAIL {fname:35} {err_msg}")
                failures.append((fname, err_msg))
            else:
                lines = [l for l in (result.stdout or '').split('\n') if l.strip()]
                print(f"OK   {fname:35} ({len(lines)} lines)")
        except subprocess.TimeoutExpired:
            print(f"TIMEOUT {fname}")
            failures.append((fname, "timeout"))
    print(f"\n{len(failures)} failures:")
    for fname, err in failures:
        print(f"  - {fname}: {err}")

if __name__ == '__main__':
    main()
