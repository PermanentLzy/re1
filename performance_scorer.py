#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
编译器性能评估系统
完整的性能测试、对比和评分脚本

用法:
    python performance_scorer.py              # 运行所有测试并计算性能评分
    python performance_scorer.py --verbose    # 详细输出
    python performance_scorer.py --detail     # 详细分析
"""

import subprocess
import os
import sys
import time
from pathlib import Path
from typing import Tuple, Dict, List, Optional
import json

class PerformanceScorer:
    def __init__(self, compiler_path=None, example_dir="examples"):
        """
        初始化性能评分器
        
        Args:
            compiler_path: 编译器路径，如果为None会自动查找
            example_dir: 示例目录路径
        """
        self.compiler = self._find_compiler(compiler_path)
        self.example_dir = example_dir
        self.results = {}
        self.verbose = False
        
    def _find_compiler(self, compiler_path=None) -> str:
        """查找编译器可执行文件"""
        if compiler_path and os.path.exists(compiler_path):
            return os.path.abspath(compiler_path)
        
        # 尝试多个可能的位置
        candidates = [
            "build/Release/mycompiler.exe",
            "build/Debug/mycompiler.exe",
            "build/mycompiler.exe",
            "MyCompiler/build/Release/mycompiler.exe",
            "MyCompiler/build/Debug/mycompiler.exe",
            "MyCompiler/build/mycompiler.exe",
            "./mycompiler.exe",
            "mycompiler.exe",
        ]
        
        for candidate in candidates:
            if os.path.exists(candidate):
                return os.path.abspath(candidate)
        
        # 在 PATH 中查找
        result = subprocess.run(
            ["where", "mycompiler.exe"] if sys.platform == "win32" else ["which", "mycompiler"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            return result.stdout.strip().split('\n')[0]
        
        return None
    
    def run_single_test(self, source_file: str, with_opt: bool = False) -> Tuple[Optional[Dict], float, Optional[str]]:
        """
        运行单个编译测试
        
        Args:
            source_file: 源文件路径
            with_opt: 是否启用优化
            
        Returns:
            (结果字典, 编译时间, 错误信息)
        """
        if not self.compiler:
            return None, 0, "未找到编译器"
        
        try:
            with open(source_file, 'r', encoding='utf-8') as f:
                source_code = f.read()
            
            cmd = [self.compiler]
            if with_opt:
                cmd.append("-opt")

            # 运行编译器（使用 utf-8 编码避免 GBK 解码错误）
            start = time.time()
            result = subprocess.run(
                cmd,
                input=source_code,
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=10
            )
            elapsed = time.time() - start

            if result.returncode != 0:
                return None, elapsed, (result.stderr or '').strip() if result.stderr else "编译失败"

            # 分析输出
            asm_output = (result.stdout or '').strip()
            asm_lines = len([line for line in asm_output.split('\n') if line.strip()])

            return {
                'asm_lines': asm_lines,
                'compile_time': elapsed,
                'stderr': result.stderr
            }, elapsed, None
            
        except subprocess.TimeoutExpired:
            return None, 10.0, "超时"
        except FileNotFoundError:
            return None, 0, f"文件不存在: {source_file}"
        except Exception as e:
            return None, 0, str(e)
    
    def calculate_score(self, results: Dict) -> float:
        """
        计算性能评分 (0-100)
        
        评分标准:
        - 代码大小减少: 40分
        - 编译速度提升: 30分
        - 测试通过率: 30分
        """
        if not results or 'tests' not in results:
            return 0.0
        
        tests = results['tests']
        if not tests:
            return 0.0
        
        # 1. 代码大小改进率
        total_reduction = 0
        valid_tests = 0
        for test_name, test_result in tests.items():
            if test_result.get('original') and test_result.get('optimized'):
                orig_lines = test_result['original']['asm_lines']
                opt_lines = test_result['optimized']['asm_lines']
                if orig_lines > 0:
                    reduction = (orig_lines - opt_lines) / orig_lines * 100
                    total_reduction += max(0, reduction)  # 只计算正的改进
                    valid_tests += 1
        
        code_size_score = 0
        if valid_tests > 0:
            avg_reduction = total_reduction / valid_tests
            code_size_score = min(40, avg_reduction * 0.4)  # 40% reduction = 40分
        
        # 2. 编译速度改进率
        total_speedup = 0
        valid_speedups = 0
        for test_name, test_result in tests.items():
            if test_result.get('original') and test_result.get('optimized'):
                orig_time = test_result['original']['compile_time']
                opt_time = test_result['optimized']['compile_time']
                if orig_time > 0:
                    speedup = (orig_time - opt_time) / orig_time * 100
                    total_speedup += max(0, speedup)
                    valid_speedups += 1
        
        speed_score = 0
        if valid_speedups > 0:
            avg_speedup = total_speedup / valid_speedups
            speed_score = min(30, avg_speedup * 0.3)  # 100% speedup = 30分
        
        # 3. 测试通过率
        passed = sum(1 for t in tests.values() if t.get('passed', False))
        total = len(tests)
        pass_rate_score = (passed / total * 100) * 0.3 if total > 0 else 0
        pass_rate_score = min(30, pass_rate_score)
        
        total_score = code_size_score + speed_score + pass_rate_score
        return min(100, total_score)
    
    def run_all_tests(self) -> Dict:
        """运行所有测试"""
        print("=" * 80)
        print("编译器性能评估系统")
        print("=" * 80)
        
        if not self.compiler:
            print("❌ 错误: 未找到编译器")
            print("   请确保编译器已编译到以下位置之一:")
            print("   - build/Release/mycompiler.exe")
            print("   - MyCompiler/build/Release/mycompiler.exe")
            return {}
        
        print(f"✓ 编译器路径: {self.compiler}\n")
        
        # 查找所有 .src 文件
        example_files = []
        if os.path.isdir(self.example_dir):
            example_files = sorted([
                os.path.join(self.example_dir, f)
                for f in os.listdir(self.example_dir)
                if f.endswith('.src')
            ])

        # 若默认目录不存在，尝试常见位置
        if not example_files:
            fallback_dirs = ["MyCompiler/examples", "examples", "../examples"]
            for d in fallback_dirs:
                if os.path.isdir(d):
                    example_files = sorted([
                        os.path.join(d, f)
                        for f in os.listdir(d)
                        if f.endswith('.src')
                    ])
                    if example_files:
                        self.example_dir = d
                        break

        if not example_files:
            print(f"❌ 未找到示例文件在 {self.example_dir}")
            return {}
        
        print(f"发现 {len(example_files)} 个测试文件\n")
        
        results = {'tests': {}, 'summary': {}}
        
        print("-" * 80)
        print("运行测试并对比优化前后性能")
        print("-" * 80)
        
        passed_count = 0
        failed_count = 0
        
        for source_file in example_files:
            test_name = os.path.basename(source_file)
            
            # 运行原始版本
            result1, time1, err1 = self.run_single_test(source_file, with_opt=False)
            
            # 运行优化版本
            result2, time2, err2 = self.run_single_test(source_file, with_opt=True)
            
            passed = result1 is not None and result2 is not None
            
            if passed:
                passed_count += 1
                status = "✓"
                
                lines1 = result1['asm_lines']
                lines2 = result2['asm_lines']
                reduction = (lines1 - lines2) / lines1 * 100 if lines1 > 0 else 0
                
                speedup = (time1 - time2) / time1 * 100 if time1 > 0 else 0
                
                print(f"{status} {test_name:35}")
                print(f"    代码: {lines1:5} → {lines2:5} 行 (减少 {reduction:5.1f}%)")
                print(f"    时间: {time1:7.3f}s → {time2:7.3f}s (加快 {speedup:5.1f}%)")
                
                results['tests'][test_name] = {
                    'passed': True,
                    'original': {
                        'asm_lines': lines1,
                        'compile_time': time1
                    },
                    'optimized': {
                        'asm_lines': lines2,
                        'compile_time': time2
                    },
                    'improvement': {
                        'code_reduction_percent': reduction,
                        'speedup_percent': speedup
                    }
                }
            else:
                failed_count += 1
                status = "✗"
                error = err1 or err2 or "未知错误"
                print(f"{status} {test_name:35} - {error[:40]}")
                
                results['tests'][test_name] = {
                    'passed': False,
                    'error': error
                }
        
        print("-" * 80)
        print(f"测试完成: {passed_count} 通过, {failed_count} 失败\n")
        
        # 计算统计数据
        print("=" * 80)
        print("性能统计")
        print("=" * 80)
        
        if passed_count > 0:
            total_reduction = sum(
                t['improvement']['code_reduction_percent']
                for t in results['tests'].values()
                if t.get('passed', False)
            )
            avg_reduction = total_reduction / passed_count if passed_count > 0 else 0
            
            total_speedup = sum(
                t['improvement']['speedup_percent']
                for t in results['tests'].values()
                if t.get('passed', False)
            )
            avg_speedup = total_speedup / passed_count if passed_count > 0 else 0
            
            print(f"\n📊 平均代码减少: {avg_reduction:5.1f}%")
            print(f"📊 平均编译加快: {avg_speedup:5.1f}%")
            print(f"📊 测试通过率:   {passed_count}/{len(example_files)} ({passed_count/len(example_files)*100:5.1f}%)")
            
            # 计算性能评分
            score = self.calculate_score(results)
            results['summary'] = {
                'avg_code_reduction': avg_reduction,
                'avg_speedup': avg_speedup,
                'pass_rate': passed_count / len(example_files),
                'performance_score': score
            }
            
            print(f"\n{'=' * 80}")
            print(f"🎯 性能评分: {score:.1f}/100")
            print(f"{'=' * 80}")
            
            # 评级
            if score >= 80:
                rating = "🌟 优秀"
            elif score >= 60:
                rating = "⭐ 良好"
            elif score >= 40:
                rating = "👍 及格"
            elif score > 0:
                rating = "📈 有改进"
            else:
                rating = "📉 无改进"
            
            print(f"评级: {rating}\n")
            
            # 建议
            print("建议:")
            if score < 30:
                print("  1. 检查优化是否正确启用 (-opt 参数)")
                print("  2. 查看是否有编译错误或警告")
                print("  3. 验证示例代码是否包含可优化的模式")
            elif score < 60:
                print("  1. 检查 LICM 和强度削弱是否有效")
                print("  2. 增加窥孔优化的模式匹配")
                print("  3. 考虑集成代码生成优化 (CodeGenOptimized)")
            else:
                print("  ✓ 优化效果良好!")
                print("  可以尝试集成 CodeGenOptimized 获得额外 10-25% 提升")
        
        return results
    
    def save_results(self, results: Dict, output_file: str = "performance_results.json"):
        """保存结果到 JSON 文件"""
        if results:
            with open(output_file, 'w', encoding='utf-8') as f:
                json.dump(results, f, indent=2, ensure_ascii=False)
            print(f"\n✓ 结果已保存到: {output_file}")


def main():
    """主函数"""
    verbose = '--verbose' in sys.argv or '--detail' in sys.argv
    
    scorer = PerformanceScorer()
    scorer.verbose = verbose
    
    results = scorer.run_all_tests()
    
    # 保存结果
    if results and results.get('tests'):
        scorer.save_results(results)
    
    # 返回成功/失败
    sys.exit(0 if results else 1)


if __name__ == '__main__':
    main()
