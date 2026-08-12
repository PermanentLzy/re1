// ================================================================
//  增强的 RISC-V 代码生成 - 优化版本
//  
//  优化点：
//    1. 使用 immediate 形式指令：addi/subi 替代 add/sub（当右操作数是常数）
//    2. 优化常数加载：避免重复 li 相同常数
//    3. 优化左移/右移：用 slli/srli 替代乘/除 2^n
//    4. 改进寄存器分配：缓存最常用值在寄存器中
// ================================================================

#pragma once

#include "../ir/ThreeAddrCode.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MyCompiler {

/// 增强的代码生成实现
class CodeGenOptimized {
public:
    /// 生成 RISC-V 32 汇编代码到 stdout
    void generate(const TACProgram& program);

private:
    // ========== 输出和状态管理 ==========
    void emit(const std::string& asm_line);
    
    // ========== 操作数处理 ==========
    
    /// 加载操作数到寄存器
    /// @param op 操作数
    /// @param reg 目标寄存器（如 "t0"）
    /// @return 实际使用的寄存器（可能是常数寄存器）
    std::string loadOperand(const TACOperand& op, const std::string& reg);
    
    /// 存储操作数
    void storeOperand(const TACOperand& op, const std::string& reg);
    
    /// 优化加载：如果是常数且在 [-2048, 2047]，直接返回 immediate；否则 li
    std::string loadOperandOptimized(const TACOperand& op, const std::string& reg);
    
    // ========== 栈管理 ==========
    int allocVarOffset(const std::string& name);
    void emitStackLoad(const std::string& reg, int offset);
    void emitStackStore(const std::string& reg, int offset);
    
    // ========== 函数 Prologue/Epilogue ==========
    void emitPrologue();
    void emitEpilogue();
    void emitFuncPrologue(const std::string& funcName, int frameSize);
    void emitFuncEpilogue();
    
    // ========== 全局变量处理 ==========
    bool isGlobal(const std::string& name) const;
    
    // ========== 二元操作优化 ==========
    
    /// 优化的二元操作生成
    /// 使用 immediate 形式指令当可能时
    void emitBinaryOptimized(const std::string& op,
                             const TACOperand& lhs,
                             const TACOperand& rhs,
                             const std::string& resultReg);
    
    // ========== 状态 ==========
    std::unordered_map<std::string, int> varOffsets_;          // 变量→栈偏移
    std::unordered_set<std::string> globalVars_;               // 全局变量集合
    std::vector<std::pair<std::string, int>> globalInit_;      // 全局初始化数据
    
    std::unordered_map<std::string, std::string> labelMap_;    // TAC标签→RISC-V标签
    std::unordered_set<std::string> funcNames_;                // 函数名集合
    
    std::vector<std::string> paramQueue_;                      // 参数队列
    std::string currentFunc_;                                  // 当前函数名
    int funcArgIndex_ = 0;                                     // 当前参数索引
    bool funcReturned_ = false;                                // 函数是否已返回
    int currentFrameSize_ = 0;                                 // 当前帧大小
    
    // 常数缓存：避免重复 li 相同常数
    std::unordered_map<int, std::string> constCache_;          // 常数值→缓存寄存器
};

} // namespace MyCompiler
