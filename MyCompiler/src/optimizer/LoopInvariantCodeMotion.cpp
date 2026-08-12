#include "Optimizer.h"
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace MyCompiler {

// ================================================================
//  循环不变式提取（Loop Invariant Code Motion - LICM）
//  
//  原理：
//    1. 识别循环结构（由 LABEL → ... → IF_GOTO(循环条件) → ... → GOTO(回到LABEL)）
//    2. 在循环内查找不变式：操作数都来自循环外或是常数的指令
//    3. 将不变式移到循环前（向前）
//    4. 可能需要多轮迭代（可能有链式不变式）
// ================================================================

/// 辅助：判断操作数是否在给定的集合中定义过
static bool isDefined(const TACOperand& op, const std::unordered_set<std::string>& defined) {
    if (op.type == TACOpType::CONST_INT) return true;
    if (op.type == TACOpType::NONE) return true;
    if (op.type == TACOpType::VAR || op.type == TACOpType::TEMP) {
        return defined.find(op.name) != defined.end();
    }
    return false;
}

/// 辅助：获取指令的所有操作数
static std::vector<TACOperand> getUsedOperands(const TACInstruction& instr) {
    std::vector<TACOperand> ops;
    if (instr.lhs.type != TACOpType::NONE) ops.push_back(instr.lhs);
    if (instr.rhs.type != TACOpType::NONE) ops.push_back(instr.rhs);
    return ops;
}

void Optimizer::loopInvariantCodeMotion(TACProgram& program) {
    // 识别循环：使用支配树或简单的基于标签的方法
    // 这里我们用简单的启发式：LABEL → ... → GOTO(回到LABEL)
    
    std::unordered_map<std::string, std::pair<int, int>> loops; // 标签 → (start_idx, end_idx)
    std::vector<TACInstruction>& instrs = program.instructions;
    
    // 第一遍：找出所有循环的起点和终点
    for (size_t i = 0; i < instrs.size(); ++i) {
        if (instrs[i].type == TACType::LABEL) {
            std::string loopLabel = instrs[i].label;
            
            // 寻找回到这个标签的 GOTO
            for (size_t j = i + 1; j < instrs.size(); ++j) {
                if (instrs[j].type == TACType::GOTO && instrs[j].label == loopLabel) {
                    loops[loopLabel] = {i, j};
                    break;
                }
            }
        }
    }
    
    if (loops.empty()) return; // 没有找到循环
    
    // 第二遍：对每个循环做 LICM
    for (auto& [loopLabel, range] : loops) {
        int loopStart = range.first;
        int loopEnd = range.second;
        
        // 循环内的指令范围（不包括标签和最后的 GOTO）
        std::vector<int> loopBody;
        for (int i = loopStart + 1; i < loopEnd; ++i) {
            loopBody.push_back(i);
        }
        
        if (loopBody.empty()) continue;
        
        // 循环前的"定义"集合（循环之前定义过的变量）
        std::unordered_set<std::string> preLoopDef;
        for (int i = 0; i < loopStart; ++i) {
            if (instrs[i].result.type == TACOpType::VAR || 
                instrs[i].result.type == TACOpType::TEMP) {
                preLoopDef.insert(instrs[i].result.name);
            }
        }
        
        // 迭代找不变式并提出
        bool changed = true;
        int iterations = 0;
        const int MAX_ITER = 10;
        
        while (changed && iterations < MAX_ITER) {
            changed = false;
            ++iterations;
            
            std::vector<int> invariants; // 待提出的指令索引
            std::unordered_set<std::string> loopDef = preLoopDef;
            
            // 遍历循环体指令，找不变式
            for (int idx : loopBody) {
                auto& instr = instrs[idx];
                
                // 跳过控制流指令
                if (instr.type == TACType::LABEL || 
                    instr.type == TACType::GOTO || 
                    instr.type == TACType::IF_GOTO ||
                    instr.type == TACType::CALL ||
                    instr.type == TACType::RETURN) {
                    if ((instr.result.type == TACOpType::VAR || 
                         instr.result.type == TACOpType::TEMP) && !instr.result.name.empty()) {
                        loopDef.insert(instr.result.name);
                    }
                    continue;
                }
                
                // 检查该指令是否是不变式
                // 条件：操作数都在循环前已定义或是常数
                auto usedOps = getUsedOperands(instr);
                bool isInvariant = true;
                for (auto& op : usedOps) {
                    if (!isDefined(op, loopDef)) {
                        isInvariant = false;
                        break;
                    }
                }
                
                if (isInvariant && (instr.type == TACType::ASSIGN ||
                                   instr.type == TACType::BINARY ||
                                   instr.type == TACType::UNARY)) {
                    invariants.push_back(idx);
                    changed = true;
                }
                
                // 更新循环内的定义集合
                if ((instr.result.type == TACOpType::VAR || 
                     instr.result.type == TACOpType::TEMP) && !instr.result.name.empty()) {
                    loopDef.insert(instr.result.name);
                }
            }
            
            // 将不变式移到循环外（在循环前）
            if (!invariants.empty()) {
                std::vector<TACInstruction> newInstrs;
                std::unordered_set<int> invariantSet(invariants.begin(), invariants.end());
                
                for (size_t i = 0; i < instrs.size(); ++i) {
                    if (static_cast<int>(i) == loopStart) {
                        // 在循环开始前插入所有不变式
                        for (int invIdx : invariants) {
                            newInstrs.push_back(instrs[invIdx]);
                        }
                        newInstrs.push_back(instrs[i]);
                    } else if (invariantSet.find(i) == invariantSet.end()) {
                        // 其他指令正常添加
                        newInstrs.push_back(instrs[i]);
                    }
                }
                
                instrs = std::move(newInstrs);
                
                // 重新调整循环范围
                loopEnd -= invariants.size();
                
                // 重新计算 loopBody
                loopBody.clear();
                for (int i = loopStart + 1; i < loopEnd; ++i) {
                    loopBody.push_back(i);
                }
            }
        }
    }
    
    std::cerr << "[LICM] 完成，找到 " << loops.size() << " 个循环\n";
}

} // namespace MyCompiler
