#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Run compiler on a source string, show stdout and stderr decoded properly."""
import subprocess, sys

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

def run(src, opt=True):
    args = [COMPILER] + (["-opt"] if opt else [])
    r = subprocess.run(args, input=src.encode("utf-8"),
                       capture_output=True, timeout=10)
    return r

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "int main() { return 42; }"
    r = run(src)
    sys.stdout.write("=== STDOUT (decoded utf-8) ===\n")
    sys.stdout.write(r.stdout.decode("utf-8", errors="replace"))
    sys.stdout.write("\n=== STDERR (decoded utf-8) ===\n")
    sys.stdout.write(r.stderr.decode("utf-8", errors="replace"))
    sys.stdout.write("\n=== RC = %d ===\n" % r.returncode)
