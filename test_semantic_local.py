#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""使用 semantic 测试数据测试编译器，并对比 -opt 优化效果"""
import subprocess
import os
import sys
import io
import time
from pathlib import Path

# 强制 UTF-8 输出，避免 GBK 编码错误
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

COMPILER = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\build\mycompiler.exe"
SEMANTIC_DIR = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\tests\semantic"

def run_test(source_file, with_opt=False):
    """运行编译器并测量输出"""
    with open(source_file, 'r', encoding='utf-8') as f:
        source = f.read()
    cmd = [COMPILER]
    if with_opt:
        cmd.append("-opt")
    try:
        start = time.time()
        result = subprocess.run(cmd, input=source, capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=10)
        elapsed = time.time() - start
        if result.returncode != 0:
            return None, elapsed, (result.stderr or '').strip()[:200]
        asm_lines = len([l for l in (result.stdout or '').split('\n') if l.strip()])
        return {'asm_lines': asm_lines, 'compile_time': elapsed, 'asm': result.stdout}, elapsed, None
    except Exception as e:
        return None, 0, str(e)

def main():
    if not os.path.exists(COMPILER):
        print(f"❌ 编译器不存在: {COMPILER}")
        sys.exit(1)

    valid_dir = os.path.join(SEMANTIC_DIR, "valid")
    files = sorted(f for f in os.listdir(valid_dir) if f.endswith('.tc'))

    print("=" * 90)
    print(f"语义测试 - 性能对比 (valid 测试集, 共 {len(files)} 个)")
    print("=" * 90)
    print(f"{'文件':<30} {'orig行':>8} {'opt行':>8} {'减少%':>8} {'orig时间':>10} {'opt时间':>10} {'状态':>6}")
    print("-" * 90)

    total_orig_lines = 0
    total_opt_lines = 0
    total_reduction = 0
    passed = 0
    failed = 0

    for f in files:
        path = os.path.join(valid_dir, f)
        r1, t1, e1 = run_test(path, with_opt=False)
        r2, t2, e2 = run_test(path, with_opt=True)

        if r1 and r2:
            l1, l2 = r1['asm_lines'], r2['asm_lines']
            red = (l1 - l2) / l1 * 100 if l1 > 0 else 0
            total_orig_lines += l1
            total_opt_lines += l2
            total_reduction += max(0, red)
            passed += 1
            status = "✓"
            print(f"{f:<30} {l1:>8} {l2:>8} {red:>7.1f}% {t1:>9.4f}s {t2:>9.4f}s {status:>6}")
        else:
            failed += 1
            err = (e1 or e2 or "")[:50]
            print(f"{f:<30} {'-':>8} {'-':>8} {'-':>8} {'-':>10} {'-':>10} {'✗':>6} {err}")

    print("-" * 90)
    print(f"通过: {passed}/{len(files)}, 失败: {failed}")
    if passed > 0:
        avg_red = total_reduction / passed
        total_red = (total_orig_lines - total_opt_lines) / total_orig_lines * 100 if total_orig_lines > 0 else 0
        print(f"平均代码减少: {avg_red:.1f}%, 总代码减少: {total_red:.1f}%")
        print(f"原始总行数: {total_orig_lines}, 优化后总行数: {total_opt_lines}")

    # 用 examples 的 .src 也跑一遍（性能评分器使用的标准）
    print()
    print("=" * 90)
    print("examples/*.src 测试（与 performance_scorer 一致）")
    print("=" * 90)
    src_dir = r"d:\Github\MyCompiler_re\MyCompiler-main\MyCompiler\examples"
    src_files = sorted(f for f in os.listdir(src_dir) if f.endswith('.src'))

    print(f"{'文件':<35} {'orig行':>8} {'opt行':>8} {'减少%':>8} {'状态':>6}")
    print("-" * 90)

    src_passed = 0
    src_total_orig = 0
    src_total_opt = 0
    src_total_red = 0

    for f in src_files:
        path = os.path.join(src_dir, f)
        r1, t1, e1 = run_test(path, with_opt=False)
        r2, t2, e2 = run_test(path, with_opt=True)
        if r1 and r2:
            l1, l2 = r1['asm_lines'], r2['asm_lines']
            red = (l1 - l2) / l1 * 100 if l1 > 0 else 0
            src_total_orig += l1
            src_total_opt += l2
            src_total_red += max(0, red)
            src_passed += 1
            status = "✓"
            print(f"{f:<35} {l1:>8} {l2:>8} {red:>7.1f}% {status:>6}")
        else:
            err = (e1 or e2 or "")[:50]
            print(f"{f:<35} {'-':>8} {'-':>8} {'-':>8} {'✗':>6} {err}")

    print("-" * 90)
    if src_passed > 0:
        avg = src_total_red / src_passed
        tot = (src_total_orig - src_total_opt) / src_total_orig * 100 if src_total_orig > 0 else 0
        print(f"通过: {src_passed}/{len(src_files)}")
        print(f"平均代码减少: {avg:.1f}%, 总代码减少: {tot:.1f}%")

if __name__ == "__main__":
    main()
