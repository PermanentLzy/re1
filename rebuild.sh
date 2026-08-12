#!/bin/bash

# 性能优化编译脚本 (Linux/Mac)
# 快速重新编译项目，包含所有新的优化

set -e

echo ""
echo "========================================"
echo "编译器性能优化 - 快速重新编译"
echo "========================================"
echo ""

# 检查是否在正确的目录
if [ ! -d "MyCompiler" ]; then
    echo "错误: 请在项目根目录运行此脚本"
    exit 1
fi

cd MyCompiler

# 删除旧的构建
echo "[1/4] 清理旧文件..."
rm -rf build

# 创建构建目录
echo "[2/4] 创建构建目录..."
mkdir build
cd build

# 运行 CMake
echo "[3/4] 运行 CMake 配置..."
cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "错误: CMake 失败"
    exit 1
fi

# 编译
echo "[4/4] 编译项目..."
cmake --build . -j$(nproc || echo 4)
if [ $? -ne 0 ]; then
    echo "错误: 编译失败"
    exit 1
fi

cd ..

echo ""
echo "========================================"
echo "✓ 编译成功！"
echo "========================================"
echo ""
echo "新增优化包括:"
echo "  1. 循环不变式提取 (LICM)"
echo "  2. 强度削弱 (Strength Reduction)"
echo "  3. 窥孔优化 (Peephole Optimization)"
echo "  4. 优化的 RISC-V 代码生成"
echo ""
echo "快速测试命令:"
echo "  1. 基础测试:"
echo "     ./build/mycompiler examples/test07_while.src -opt"
echo ""
echo "  2. 性能对比:"
echo "     python3 test_performance.py --compare"
echo ""
echo "  3. 完整测试:"
echo "     cd build && ctest --output-on-failure"
echo ""
