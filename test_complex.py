#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test compiler with complex programs - global vars, functions, loops."""
import subprocess

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

TEST_CASES = {
    "global_var": """int g = 42;
int main() { return g; }""",
    "global_const": """const int G = 99;
int main() { return G; }""",
    "func_call": """int add(int a, int b) { return a + b; }
int main() { return add(10, 20); }""",
    "while_loop": """int main() {
    int i = 0, s = 0;
    while (i < 10) { s = s + i; i = i + 1; }
    return s;
}""",
    "if_else": """int main() {
    int x = 10;
    if (x > 5) return 1; else return 0;
}""",
    "recursion": """int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main() { return fact(5); }""",
    "break_continue": """int main() {
    int i = 0, s = 0;
    while (i < 100) {
        if (i >= 10) break;
        if (i % 2 == 0) { i = i + 1; continue; }
        s = s + i;
        i = i + 1;
    }
    return s;
}""",
    "void_func": """void nop() { return; }
int main() { nop(); return 0; }""",
    "multi_func": """int a() { return 1; }
int b() { return 2; }
int main() { return a() + b(); }""",
    "neg_const": """int main() { return -42; }""",
    "complex_expr": """int main() {
    int x = 3, y = 5;
    int z = x * y + 2 * x - y;
    return z;
}""",
    "logical_ops": """int main() {
    int a = 1, b = 0;
    if (a && b) return 1;
    if (a || b) return 2;
    if (!a) return 3;
    return 0;
}""",
}

for name, src in TEST_CASES.items():
    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"{'='*60}")
    r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                       capture_output=True, timeout=10)
    if r.returncode != 0:
        print(f"COMPILE ERROR (rc={r.returncode})")
        err = r.stderr.decode("utf-8", errors="replace")[:500]
        print(f"STDERR: {err}")
        continue
    
    asm = r.stdout.decode("utf-8", errors="replace")
    lines = [l for l in asm.split("\n") if l.strip()]
    print(f"Generated {len(lines)} lines:")
    
    # Check for potential RISC-V issues
    issues = []
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.endswith(':') or stripped.startswith('.'):
            continue
        parts = stripped.split()
        if len(parts) >= 2:
            instr = parts[0].lower()
            # Check for instructions that might not be valid
            # sll, sra are valid RISC-V instructions
            # but some assemblers might not support them
            pass
    
    for line in lines:
        print(f"  {line}")
    
    if issues:
        print(f"\n⚠️  ISSUES: {issues}")
