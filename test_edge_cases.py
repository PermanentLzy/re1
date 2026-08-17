#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test edge cases that might cause assembly errors in evaluation system."""
import subprocess

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

# Test cases that match the evaluation system's optimization tests
TEST_CASES = {
    "p01_const_simple": """int main() { return 1 + 2; }""",
    "p01_const_large": """int main() { return 1000000 + 2000000; }""",
    "p01_const_neg": """int main() { return -42; }""",
    "p01_const_multi": """int main() { int x = 3 * 4 + 5; return x; }""",
    "p02_dead_code": """int main() { int x = 1; x = 2; return 0; }""",
    "p03_copy": """int main() { int a = 1; int b = a; return b; }""",
    "p04_cse": """int main() { int a = 3 + 4; int b = 3 + 4; return a + b; }""",
    "p05_algebra": """int main() { int x = 5; int a = x * 1; int b = x + 0; return a + b; }""",
    "p05_algebra_zero": """int main() { int x = 5; return x * 0; }""",
    "p05_algebra_neg": """int main() { int x = 5; return -x; }""",
    "const_int_decl": """int main() { const int x = 42; return x; }""",
    "const_expr": """int main() { const int a = 3; const int b = a + 2; return b; }""",
}

for name, src in TEST_CASES.items():
    r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                       capture_output=True, timeout=10)
    if r.returncode != 0:
        print(f"FAIL {name}: rc={r.returncode}")
        print(f"  ERR: {r.stderr.decode('utf-8', errors='replace')[:300]}")
        continue
    
    asm = r.stdout.decode("utf-8", errors="replace")
    lines = [l for l in asm.split("\n") if l.strip()]
    
    # Check for issues
    issues = []
    for line in lines:
        s = line.strip()
        if not s or s.endswith(':') or s.startswith('.'):
            continue
        parts = s.split()
        if len(parts) >= 2:
            instr = parts[0].lower()
            # Check immediate range
            if instr in ("addi", "slti", "xori", "ori"):
                try:
                    imm = int(parts[-1])
                    if imm < -2048 or imm > 2047:
                        issues.append(f"  {s}: immediate {imm} out of [-2048,2047]")
                except: pass
            if instr == "andi":
                try:
                    imm = int(parts[-1])
                    if imm < 0 or imm > 4095:
                        issues.append(f"  {s}: immediate {imm} out of [0,4095]")
                except: pass
            if instr in ("slli", "srai", "srli"):
                try:
                    imm = int(parts[-1])
                    if imm < 0 or imm > 31:
                        issues.append(f"  {s}: shift {imm} out of [0,31]")
                except: pass
    
    status = "✓" if not issues else "⚠"
    print(f"{status} {name:25s} ({len(lines):3d} lines)")
    for issue in issues:
        print(issue)
