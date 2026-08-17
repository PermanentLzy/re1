#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Test compiler on optimization-focused test cases to find assembly errors."""
import subprocess, sys

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"

# Test cases simulating the performance tests (p01-p05)
TEST_CASES = {
    "p01_const": """
int main() {
    int x = 5 + 3 * 2;
    return x;
}
""",
    "p02_dead_code": """
int main() {
    int x = 42;
    x = x + 1;
    x = x - 1;
    return 42;
}
""",
    "p03_copy": """
int main() {
    int a = 10;
    int b = a;
    int c = b;
    return c;
}
""",
    "p04_common_subexpr": """
int main() {
    int x = 5, y = 3;
    int a = x + y;
    int b = x + y;
    return a + b;
}
""",
    "p05_algebra": """
int main() {
    int x = 7;
    int a = x * 1;
    int b = x + 0;
    int c = x - 0;
    int d = x * 0;
    return a + b + c + d;
}
""",
}

def run_compiler(src):
    r = subprocess.run([COMPILER, "-opt"], input=src.encode("utf-8"),
                       capture_output=True, timeout=10)
    return r

def main():
    for name, src in TEST_CASES.items():
        print(f"\n{'='*60}")
        print(f"TEST: {name}")
        print(f"{'='*60}")
        r = run_compiler(src)
        if r.returncode != 0:
            print(f"COMPILE ERROR (rc={r.returncode})")
            print(f"STDERR: {r.stderr.decode('utf-8', errors='replace')[:500]}")
            continue
        asm = r.stdout.decode("utf-8", errors="replace")
        lines = [l for l in asm.split("\n") if l.strip()]
        print(f"Generated {len(lines)} assembly lines:")
        for line in lines:
            print(f"  {line}")
        
        # Check for potential RISC-V assembly issues
        issues = []
        for line in lines:
            stripped = line.strip()
            # Check for instructions that might not be valid
            parts = stripped.split()
            if len(parts) >= 2:
                instr = parts[0].lower()
                # Check immediate range issues
                if instr in ("addi", "slti", "andi", "xori", "ori", "andi"):
                    # Parse the immediate
                    if len(parts) >= 3:
                        imm_str = parts[-1]
                        try:
                            imm = int(imm_str)
                            if instr == "andi":
                                if imm < 0 or imm > 4095:
                                    issues.append(f"  ANDI immediate {imm} out of range [0,4095]")
                            elif instr in ("addi", "slti", "xori", "ori"):
                                if imm < -2048 or imm > 2047:
                                    issues.append(f"  {instr.upper()} immediate {imm} out of range [-2048,2047]")
                        except ValueError:
                            pass
                if instr in ("slli", "srai", "srli"):
                    if len(parts) >= 3:
                        imm_str = parts[-1]
                        try:
                            imm = int(imm_str)
                            if imm < 0 or imm > 31:
                                issues.append(f"  {instr.upper()} shift amount {imm} out of range [0,31]")
                        except ValueError:
                            pass
                if instr == "li":
                    if len(parts) >= 3:
                        imm_str = parts[-1]
                        try:
                            imm = int(imm_str)
                            # li can handle any 32-bit value, but some assemblers might not
                            if imm < -2147483648 or imm > 2147483647:
                                issues.append(f"  LI immediate {imm} out of 32-bit range")
                        except ValueError:
                            pass
        
        if issues:
            print(f"\n⚠️  POTENTIAL ISSUES:")
            for issue in issues:
                print(issue)
        else:
            print(f"\n✓ No obvious assembly issues detected")

if __name__ == "__main__":
    main()
