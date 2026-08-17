#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test compiler output raw bytes to find assembly errors."""
import subprocess

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

TEST_CASES = {
    "p01_const": """int main() { int x = 5 + 3 * 2; return x; }""",
    "p02_dead_code": """int main() { int x = 42; x = x + 1; x = x - 1; return 42; }""",
    "p03_copy": """int main() { int a = 10; int b = a; int c = b; return c; }""",
    "p05_algebra": """int main() { int x = 7; int a = x * 1; int b = x + 0; return a + b; }""",
}

for name, src in TEST_CASES.items():
    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"{'='*60}")
    r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                       capture_output=True, timeout=10)
    if r.returncode != 0:
        print(f"COMPILE ERROR (rc={r.returncode})")
        print(f"STDERR: {r.stderr.decode('utf-8', errors='replace')[:500]}")
        continue
    
    # Show raw stdout bytes
    stdout_bytes = r.stdout
    print(f"STDOUT ({len(stdout_bytes)} bytes):")
    print(repr(stdout_bytes[:500]))
    
    print(f"\nDecoded assembly:")
    asm = r.stdout.decode("utf-8", errors="replace")
    for i, line in enumerate(asm.split("\n")):
        print(f"  [{i:2d}] {repr(line)}")
