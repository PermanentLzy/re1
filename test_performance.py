#!/usr/bin/env python3
"""
性能优化效果评估脚本

用法：
    python3 test_performance.py
    python3 test_performance.py --compare    # 对比优化前后
    python3 test_performance.py --verbose    # 详细输出
"""

import subprocess
import os
import sys
import time
import tempfile
from pathlib import Path

class PerformanceTester:
    def __init__(self, compiler_path="./build/mycompiler.exe", example_dir="./examples"):
        self.compiler = compiler_path
        self.example_dir = example_dir
        self.results = {}
        
    def run_test(self, source_file, with_opt=False):
        """运行编译器并测量性能"""
        cmd = [self.compiler]
        if with_opt:
            cmd.append("-opt")
        
        try:
            # 读取源码
            with open(source_file, 'r') as f:
                source_code = f.read()
            
            # 执行编译
            start = time.time()
            result = subprocess.run(
                cmd,
                input=source_code,
                capture_output=True,
                text=True,
                timeout=5
            )
            elapsed = time.time() - start
            
            if result.returncode != 0:
                return None, elapsed, result.stderr
            
            # 统计生成的汇编代码
            asm_lines = len(result.stdout.strip().split('\n'))
            
            return {
                'asm_lines': asm_lines,
                'compile_time': elapsed,
                'ir_info': result.stderr
            }, elapsed, None
            
        except subprocess.TimeoutExpired:
            return None, 5.0, "超时"
        except Exception as e:
            return None, 0, str(e)
    
    def compare_tests(self):
        """对比优化前后的性能"""
        test_files = [
            "test20_complex_expr.src",
            "test21_comprehensive.src",
            "test07_while.src",
            "test13_recursive.src",
        ]
        
        print("=" * 70)
        print("性能对比测试（优化前后）")
        print("=" * 70)
        
        for test_file in test_files:
            source_path = os.path.join(self.example_dir, test_file)
            if not os.path.exists(source_path):
                continue
            
            print(f"\n测试文件: {test_file}")
            print("-" * 70)
            
            # 原始版本
            result1, time1, err1 = self.run_test(source_path, with_opt=False)
            
            # 优化版本
            result2, time2, err2 = self.run_test(source_path, with_opt=True)
            
            if result1 and result2:
                asm_reduction = (1 - result2['asm_lines'] / result1['asm_lines']) * 100
                time_reduction = (1 - time2 / time1) * 100
                
                print(f"  汇编行数: {result1['asm_lines']:5} → {result2['asm_lines']:5} "
                      f"(减少 {asm_reduction:5.1f}%)")
                print(f"  编译时间: {time1:7.3f}s → {time2:7.3f}s "
                      f"(减少 {time_reduction:5.1f}%)")
                
                # 提取 IR 统计
                if result1['ir_info']:
                    ir_line = [l for l in result1['ir_info'].split('\n') if 'IR' in l]
                    if ir_line:
                        print(f"  IR 信息: {ir_line[-1].strip()}")
            else:
                print(f"  ❌ 编译失败")
                if err1:
                    print(f"     原始: {err1}")
                if err2:
                    print(f"     优化: {err2}")
    
    def run_all_tests(self):
        """运行所有示例测试"""
        print("=" * 70)
        print("运行所有示例文件")
        print("=" * 70)
        
        example_files = [
            f for f in os.listdir(self.example_dir) 
            if f.endswith('.src')
        ]
        
        passed = 0
        failed = 0
        
        for test_file in sorted(example_files):
            source_path = os.path.join(self.example_dir, test_file)
            
            result, elapsed, err = self.run_test(source_path, with_opt=True)
            
            if result:
                status = "✓"
                passed += 1
                detail = f"{result['asm_lines']} 行汇编, {elapsed:.3f}s"
            else:
                status = "✗"
                failed += 1
                detail = f"错误: {err}" if err else "超时"
            
            print(f"  {status} {test_file:30} {detail}")
        
        print("-" * 70)
        print(f"总计: {passed} 通过, {failed} 失败")


def main():
    # 确保在项目目录下
    if not os.path.exists("build/mycompiler.exe") and not os.path.exists("build/mycompiler"):
        print("❌ 未找到编译器可执行文件")
        print("   请先运行: mkdir build && cd build && cmake .. && cmake --build . -j4")
        sys.exit(1)
    
    # 确定编译器路径
    if os.path.exists("build/mycompiler.exe"):
        compiler = "build/mycompiler.exe"
    else:
        compiler = "build/mycompiler"
    
    tester = PerformanceTester(
        compiler_path=compiler,
        example_dir="MyCompiler/examples"
    )
    
    if len(sys.argv) > 1:
        if sys.argv[1] == "--compare":
            tester.compare_tests()
        elif sys.argv[1] == "--verbose":
            # TODO: 实现详细输出
            tester.run_all_tests()
    else:
        tester.run_all_tests()


if __name__ == "__main__":
    main()
