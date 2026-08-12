#include "Optimizer.h"
#include <iostream>
#include <unordered_set>
#include <string>
#include <algorithm>

namespace MyCompiler {

// ================================================================
//  主入口：串联所有优化 pass，迭代直到 IR 不再变化
// ================================================================
void Optimizer::optimize(TACProgram& program) {
    // ---- 第 0 轮：一次性优化（只执行一次）----
    tailRecursionElimination(program);      // 尾递归消除
    strengthReduction(program);             // 强度削弱
    loopInvariantCodeMotion(program);       // 循环不变式提取

    bool changed = true;
    int iter = 0;
    const int MAX_ITER = 10;

    while (changed && iter < MAX_ITER) {
        changed = false;
        size_t before = program.instructions.size();

        constantFolding(program);                   // 常量折叠
        commonSubexpressionElimination(program);   // 公共子表达式消除（CSE）
        deadCodeElimination(program);              // 死代码删除（DCE）
        removeUnreachableCode(program);            // 不可达代码删除
        coalesceTemporaryCopies(program);          // 临时→局部合并
        peepholeOptimization(program);             // 窥孔优化

        if (program.instructions.size() != before)
            changed = true;
        ++iter;
    }

    std::cerr << "[Optimizer] " << iter << " 轮, 最终 " << program.instructions.size() << " 条IR\n";
}

// ================================================================
//  不可达代码删除：无条件跳转/返回后的指令直到下一个标签均为死代码
// ================================================================
void Optimizer::removeUnreachableCode(TACProgram& program) {
    std::vector<TACInstruction> kept;
    kept.reserve(program.instructions.size());
    bool reachable = true;
    int removed = 0;

    for (auto& instr : program.instructions) {
        // 标签总是保留，并且标记为可达（新的基本块入口）
        if (instr.type == TACType::LABEL) {
            reachable = true;
            kept.push_back(std::move(instr));
            continue;
        }

        // 不可达的非标签指令 → 跳过
        if (!reachable) {
            ++removed;
            continue;
        }

        kept.push_back(std::move(instr));

        // 无条件跳转或返回后，后续代码不可达
        if (kept.back().type == TACType::GOTO ||
            kept.back().type == TACType::RETURN) {
            reachable = false;
        }
    }

    program.instructions = std::move(kept);

    if (removed > 0)
        std::cerr << "[Unreachable] 删除: " << removed << " 条\n";
}

// ================================================================
//  临时→局部合并：消除 %t = compute; var = %t 冗余拷贝
//  例如:  %t5 = a + b        →   x = a + b
//         x = %t5             (删除)
//         后续所有 %t5 的引用 → x
// ================================================================
void Optimizer::coalesceTemporaryCopies(TACProgram& program) {
    std::vector<TACInstruction> result;
    result.reserve(program.instructions.size());
    int merged = 0;
    const auto& instrs = program.instructions;

    /// 判断两个操作数是否指向同一存储（同类型+同名）
    auto sameStorage = [](const TACOperand& a, const TACOperand& b) -> bool {
        if (a.type != b.type) return false;
        if (a.type == TACOpType::TEMP || a.type == TACOpType::VAR)
            return a.name == b.name;
        return false;
    };

    /// 判断是否是"产生临时值"的指令（result 为 TEMP，且是 ASSIGN/BINARY/UNARY）
    auto producesTemp = [](const TACInstruction& instr) -> bool {
        if (instr.result.type != TACOpType::TEMP) return false;
        return instr.type == TACType::ASSIGN ||
               instr.type == TACType::BINARY ||
               instr.type == TACType::UNARY;
    };

    /// 判断是否是"拷贝到局部变量"的指令（ASSIGN, result=VAR, lhs 匹配某个 temp）
    auto copyToLocal = [](const TACInstruction& instr, const TACOperand& temp) -> bool {
        if (instr.type != TACType::ASSIGN) return false;
        if (instr.result.type != TACOpType::VAR) return false;
        if (instr.lhs.type != TACOpType::TEMP) return false;
        return instr.lhs.name == temp.name;
    };

    /// 判断指令是否阻止向前替换（控制流/标签/重定义）
    auto blocksSubstitution = [&](const TACInstruction& instr,
                                  const TACOperand& replacement) -> bool {
        if (instr.type == TACType::LABEL ||
            instr.type == TACType::GOTO ||
            instr.type == TACType::IF_GOTO ||
            instr.type == TACType::RETURN)
            return true;
        // 目标变量被重新赋值 → 停止
        if ((instr.type == TACType::ASSIGN ||
             instr.type == TACType::BINARY ||
             instr.type == TACType::UNARY ||
             instr.type == TACType::FUNC_ARG) &&
            instr.result.type == TACOpType::VAR &&
            instr.result.name == replacement.name)
            return true;
        return false;
    };

    /// 替换指令中对 from 的引用为 to
    auto replaceUse = [](TACInstruction& instr,
                         const TACOperand& from, const TACOperand& to) {
        if (instr.lhs.type == from.type && instr.lhs.name == from.name)
            instr.lhs = to;
        if (instr.rhs.type == from.type && instr.rhs.name == from.name)
            instr.rhs = to;
    };

    for (size_t i = 0; i < instrs.size(); ++i) {
        auto instr = instrs[i];

        // 检查模式: 指令 i 产生临时值，指令 i+1 将其拷贝到局部变量
        if (i + 1 < instrs.size() && producesTemp(instr)) {
            const TACOperand temp = instr.result;
            if (copyToLocal(instrs[i + 1], temp)) {
                const TACOperand local = instrs[i + 1].result;

                // 将指令 i 的结果改为局部变量
                instr.result = local;
                result.push_back(std::move(instr));
                ++i;  // 跳过 i+1 (拷贝指令)
                ++merged;

                // 向前替换后续指令中对 temp 的引用
                for (size_t j = i + 1; j < instrs.size(); ++j) {
                    auto& later = const_cast<TACInstruction&>(instrs[j]);

                    // 遇到阻断条件 → 停止替换
                    if (blocksSubstitution(later, local))
                        break;

                    replaceUse(later, temp, local);
                }
                continue;
            }
        }

        result.push_back(std::move(instr));
    }

    program.instructions = std::move(result);

    if (merged > 0)
        std::cerr << "[Coalesce] 合并: " << merged << " 条\n";
}

} // namespace MyCompiler
