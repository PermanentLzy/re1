#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Run semantic tests (valid should pass, invalid should fail)."""
import subprocess
import os

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"
VALID_DIR = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\tests\semantic\valid"
INVALID_DIR = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\tests\semantic\invalid"

def run_test(fpath, expect_pass):
    with open(fpath, 'r', encoding='utf-8') as f:
        src = f.read()
    try:
        result = subprocess.run(
            [COMPILER, "-opt"],
            input=src, capture_output=True, text=True,
            encoding='utf-8', errors='replace', timeout=10
        )
        ok = (result.returncode == 0) if expect_pass else (result.returncode != 0)
        return ok, (result.stderr or '').strip().splitlines()
    except subprocess.TimeoutExpired:
        return False, ["timeout"]

def main():
    print("=" * 70)
    print("VALID tests (should pass)")
    print("=" * 70)
    valid_files = sorted(f for f in os.listdir(VALID_DIR) if f.endswith('.tc'))
    vp = 0
    for fname in valid_files:
        ok, err = run_test(os.path.join(VALID_DIR, fname), True)
        mark = "PASS" if ok else "FAIL"
        print(f"  {mark} {fname}")
        if ok: vp += 1
        elif err: print(f"       {err[0]}")
    print(f"  -> {vp}/{len(valid_files)} valid tests passed\n")

    print("=" * 70)
    print("INVALID tests (should fail)")
    print("=" * 70)
    invalid_files = sorted(f for f in os.listdir(INVALID_DIR) if f.endswith('.tc'))
    ip = 0
    for fname in invalid_files:
        ok, err = run_test(os.path.join(INVALID_DIR, fname), False)
        mark = "PASS" if ok else "FAIL"
        print(f"  {mark} {fname}")
        if ok: ip += 1
        else:
            print(f"       Expected compile error but got success (or wrong result)")
    print(f"  -> {ip}/{len(invalid_files)} invalid tests correctly rejected\n")

    total = vp + ip
    print(f"Total: {total}/{len(valid_files)+len(invalid_files)} correct")

if __name__ == '__main__':
    main()
