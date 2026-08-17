#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Debug IR and assembly for large constant."""
import subprocess

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

for name, src in [
    ("small", "int main() { return 42; }"),
    ("large", "int main() { return 1000000 + 2000000; }"),
    ("neg", "int main() { return -42; }"),
]:
    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"{'='*60}")
    r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                       capture_output=True, timeout=10,
                       env={"DEBUG_IR": "1"})
    print("STDOUT:")
    print(r.stdout.decode("utf-8", errors="replace"))
    print("STDERR:")
    print(r.stderr.decode("utf-8", errors="replace"))
