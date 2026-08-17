#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Debug IR and assembly for constant return values."""
import subprocess

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

src = "int main() { return 42; }"
r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                   capture_output=True, timeout=10)
print("STDOUT:")
print(r.stdout.decode("utf-8", errors="replace"))
print("STDERR:")
print(r.stderr.decode("utf-8", errors="replace"))
print("RC:", r.returncode)

# Now with DEBUG_IR
r2 = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                    capture_output=True, timeout=10,
                    env={"DEBUG_IR": "1"})
print("\nSTDERR (with DEBUG_IR):")
print(r2.stderr.decode("utf-8", errors="replace"))
