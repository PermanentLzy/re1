@echo off
REM 性能优化编译脚本
REM 快速重新编译项目，包含所有新的优化

setlocal enabledelayedexpansion

echo.
echo ========================================
echo 编译器性能优化 - 快速重新编译
echo ========================================
echo.

REM 检查是否在正确的目录
if not exist "MyCompiler" (
    echo 错误: 请在项目根目录运行此脚本
    exit /b 1
)

cd MyCompiler

REM 删除旧的构建
echo [1/4] 清理旧文件...
if exist build (
    rmdir /s /q build >nul 2>&1
)

REM 创建构建目录
echo [2/4] 创建构建目录...
mkdir build
cd build

REM 运行 CMake
echo [3/4] 运行 CMake 配置...
cmake .. -G "Visual Studio 16 2019"
if errorlevel 1 (
    echo 错误: CMake 失败
    exit /b 1
)

REM 编译
echo [4/4] 编译项目...
cmake --build . --config Release -j4
if errorlevel 1 (
    echo 错误: 编译失败
    exit /b 1
)

cd ..

echo.
echo ========================================
echo ✓ 编译成功！
echo ========================================
echo.
echo 新增优化包括:
echo   1. 循环不变式提取 (LICM)
echo   2. 强度削弱 (Strength Reduction)
echo   3. 窥孔优化 (Peephole Optimization)
echo   4. 优化的 RISC-V 代码生成
echo.
echo 快速测试命令:
echo   1. 基础测试:
echo      .\build\Release\mycompiler.exe examples\test07_while.src -opt
echo.
echo   2. 性能对比:
echo      python test_performance.py --compare
echo.
echo   3. 完整测试:
echo      cd build && ctest --output-on-failure
echo.

pause
