#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Show assembly for edge cases."""
import subprocess

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

cases = {
    "large_const": "int main() { return 1000000 + 2000000; }",
    "neg_const": "int main() { return -42; }",
    "neg_in_add": "int main() { int x = 10; return x - 15; }",
    "mod_power2": "int main() { int x = 100; return x % 16; }",
    "mul_power2": "int main() { int x = 5; return x * 8; }",
    "div_power2": "int main() { int x = 64; return x / 8; }",
    "global_var": "int g = 42; int main() { return g; }",
    "global_const": "const int G = 99; int main() { return G; }",
}

for name, src in cases.items():
    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"{'='*60}")
    r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                       capture_output=True, timeout=10)
    if r.returncode != 0:
        print(f"COMPILE ERROR: {r.stderr.decode('utf-8', errors='replace')[:300]}")
        continue
    asm = r.stdout.decode("utf-8", errors="replace")
    print(asm)
