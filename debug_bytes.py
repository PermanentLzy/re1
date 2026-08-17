#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Show exact bytes of stdout from the compiler."""
import subprocess, sys

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

src = sys.argv[1] if len(sys.argv) > 1 else "int main() { return 42; }"
r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                   capture_output=True, timeout=10)
out = r.stdout
sys.stdout.buffer.write(b"=== STDOUT raw bytes (repr) ===\n")
sys.stdout.buffer.write(repr(out).encode("utf-8"))
sys.stdout.buffer.write(b"\n=== STDOUT length: " + str(len(out)).encode() + b" bytes ===\n")
