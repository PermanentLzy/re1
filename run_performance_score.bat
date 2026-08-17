@echo off
REM 性能评分快速运行脚本
REM 一键编译和评分

setlocal enabledelayedexpansion

echo.
echo ========================================
echo 编译器性能评分 - 快速运行
echo ========================================
echo.

REM 检查是否在项目根目录
if not exist "MyCompiler" (
    echo 错误: 请在项目根目录运行此脚本
    echo.
    echo 正确的目录应该是:
    echo   d:\Github\MyCompiler_re\MyCompiler-main
    echo.
    pause
    exit /b 1
)

REM 步骤 1: 编译项目
echo [1/3] 编译项目...
cd MyCompiler

if exist build (
    echo   清理旧构建...
    rmdir /s /q build >nul 2>&1
)

echo   创建构建目录...
mkdir build
cd build

echo   运行 CMake...
cmake .. >nul 2>&1
if errorlevel 1 (
    echo 错误: CMake 失败
    cd ..\..
    pause
    exit /b 1
)

echo   编译项目 (Release 模式)...
cmake --build . --config Release -j4 >nul 2>&1
if errorlevel 1 (
    echo 错误: 编译失败
    cd ..\..
    pause
    exit /b 1
)

echo ✓ 编译成功
echo.

REM 步骤 2: 返回项目根目录
cd ..\..

REM 步骤 3: 运行性能评分
echo [2/3] 运行性能评分...
echo.

if not exist "performance_scorer.py" (
    echo 错误: 找不到 performance_scorer.py
    pause
    exit /b 1
)

python performance_scorer.py

if errorlevel 1 (
    echo.
    echo 错误: 性能评分失败
    echo 请检查:
    echo   1. Python 已安装
    echo   2. 编译器位置正确
    echo.
    pause
    exit /b 1
)

REM 步骤 4: 显示结果摘要
echo.
echo [3/3] 生成结果摘要...
echo.

if exist "performance_results.json" (
    echo ✓ 结果已保存到 performance_results.json
    echo.
    echo 下一步:
    echo   1. 查看完整结果: cat performance_results.json
    echo   2. 阅读指南: 如何获得性能得分.md
    echo   3. 查看优化建议: 性能优化总结.md
    echo.
)

echo ========================================
echo ✓ 性能评分完成!
echo ========================================
echo.

pause
