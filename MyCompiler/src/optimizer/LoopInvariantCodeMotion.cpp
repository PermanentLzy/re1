#include "Optimizer.h"
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace MyCompiler
{

    // ================================================================
    //  循环不变式提取（Loop Invariant Code Motion - LICM）
    //
    //  正确性条件（一条指令可被安全地提到循环外）：
    //    1. 指令是 "纯计算" (ASSIGN / BINARY / UNARY)，无副作用
    //    2. 所有操作数都是 "循环不变"：
    //       - 常数 (CONST_INT / NONE)
    //       - 在循环外定义 且 在循环体内没有被重新定义
    //       - 由本趟已识别的循环不变式定义（链式）
    //    3. 指令的 result 在循环体内只被定义一次（避免覆盖）
    //    4. 指令不能抛异常 / 不能是除零等可能失败的操作（保守起见，包含 div/rem 时跳过）
    // ================================================================

    /// 判断操作数是否为常数类（CONST_INT 或 NONE）
    static bool isConstLike(const TACOperand &op)
    {
        return op.type == TACOpType::CONST_INT || op.type == TACOpType::NONE;
    }

    /// 收集指令使用到的操作数（lhs、rhs，但不包含 result）
    static std::vector<TACOperand> getUsedOperands(const TACInstruction &instr)
    {
        std::vector<TACOperand> ops;
        if (instr.lhs.type != TACOpType::NONE)
            ops.push_back(instr.lhs);
        if (instr.rhs.type != TACOpType::NONE)
            ops.push_back(instr.rhs);
        return ops;
    }

    /// 判断变量名是否定义在某条指令的 result 中
    static bool instrDefines(const TACInstruction &instr, const std::string &name)
    {
        if (name.empty())
            return false;
        if (instr.result.type != TACOpType::VAR && instr.result.type != TACOpType::TEMP)
            return false;
        return instr.result.name == name;
    }

    void Optimizer::loopInvariantCodeMotion(TACProgram &program)
    {
        std::vector<TACInstruction> &instrs = program.instructions;
        if (instrs.empty())
            return;

        // ---- 第 1 遍：找出所有循环结构 ----
        // 启发式：LABEL L → ... → GOTO L  形成一个自然循环
        // 也支持 WHILE 循环：LABEL L → ... → IF_GOTO cond, end → ... → GOTO L → LABEL end
        struct Loop
        {
            int head;
            int body_start;
            int body_end;
        };
        std::vector<Loop> loops;

        for (size_t i = 0; i < instrs.size(); ++i)
        {
            if (instrs[i].type != TACType::LABEL)
                continue;
            const std::string &loopLabel = instrs[i].label;

            // 模式 1: LABEL L → ... → GOTO L  (简单循环)
            for (size_t j = i + 1; j < instrs.size(); ++j)
            {
                if (instrs[j].type == TACType::GOTO && instrs[j].label == loopLabel)
                {
                    Loop lp;
                    lp.head = static_cast<int>(i);
                    lp.body_start = static_cast<int>(i) + 1;
                    lp.body_end = static_cast<int>(j); // 不包含 GOTO 本身
                    loops.push_back(lp);
                    break;
                }
                // 模式 2: WHILE 循环 - LABEL L → ... → IF_GOTO cond, end → ... → GOTO L
                // 检测 IF_GOTO 跳转到 end，然后后面有 GOTO 跳回 L
                if (instrs[j].type == TACType::IF_GOTO)
                {
                    const std::string &endLabel = instrs[j].label;
                    // 在 IF_GOTO 之后找 GOTO 跳回 loopLabel
                    for (size_t k = j + 1; k < instrs.size(); ++k)
                    {
                        if (instrs[k].type == TACType::GOTO && instrs[k].label == loopLabel)
                        {
                            // 检查 endLabel 是否在 GOTO 之后
                            bool endAfterLoop = false;
                            for (size_t m = k + 1; m < instrs.size(); ++m)
                            {
                                if (instrs[m].type == TACType::LABEL && instrs[m].label == endLabel)
                                {
                                    endAfterLoop = true;
                                    break;
                                }
                            }
                            if (endAfterLoop)
                            {
                                Loop lp;
                                lp.head = static_cast<int>(i);
                                lp.body_start = static_cast<int>(i) + 1;
                                lp.body_end = static_cast<int>(k); // 不包含 GOTO 本身
                                loops.push_back(lp);
                            }
                            break;
                        }
                        // 遇到新的标签或函数边界则停止
                        if (instrs[k].type == TACType::LABEL)
                            break;
                    }
                    break;
                }
                // 遇到新的标签则停止搜索（避免跨函数）
                if (instrs[j].type == TACType::LABEL)
                    break;
            }
        }

        if (loops.empty())
        {
            std::cerr << "[LICM] 未发现循环\n";
            return;
        }

        int totalHoisted = 0;

        // ---- 第 2 遍：对每个循环做不变式提取 ----
        for (const auto &lp : loops)
        {
            // 收集循环体内（body_start..body_end）所有定义的变量名
            std::unordered_set<std::string> definedInLoop;
            std::unordered_map<std::string, int> defCount; // 变量在循环内被定义的次数
            for (int idx = lp.body_start; idx < lp.body_end; ++idx)
            {
                const auto &instr = instrs[idx];
                if (instr.result.type == TACOpType::VAR || instr.result.type == TACOpType::TEMP)
                {
                    if (!instr.result.name.empty())
                    {
                        definedInLoop.insert(instr.result.name);
                        defCount[instr.result.name]++;
                    }
                }
            }

            // 循环前已定义的变量名
            std::unordered_set<std::string> preLoopDef;
            for (int i = 0; i < lp.head; ++i)
            {
                const auto &instr = instrs[i];
                if (instr.result.type == TACOpType::VAR || instr.result.type == TACOpType::TEMP)
                {
                    if (!instr.result.name.empty())
                        preLoopDef.insert(instr.result.name);
                }
            }

            // 迭代找不变式（可能存在链式依赖：t1 = a+b; t2 = t1*c;）
            std::vector<int> invariantIndices;
            std::unordered_set<std::string> invariantVars; // 这些变量现在视为循环外定义

            bool changed = true;
            int maxIter = 10;
            while (changed && maxIter-- > 0)
            {
                changed = false;
                for (int idx = lp.body_start; idx < lp.body_end; ++idx)
                {
                    if (std::find(invariantIndices.begin(), invariantIndices.end(), idx) != invariantIndices.end())
                        continue;

                    const auto &instr = instrs[idx];

                    // 仅处理纯计算
                    if (instr.type != TACType::ASSIGN &&
                        instr.type != TACType::BINARY &&
                        instr.type != TACType::UNARY)
                        continue;

                    // 保守：包含除法/求模的指令不外提（避免除零行为变化）
                    if (instr.type == TACType::BINARY &&
                        (instr.op == "/" || instr.op == "%"))
                    {
                        // 仍可外提，但要求除数是常数非零
                        if (instr.rhs.type != TACOpType::CONST_INT || instr.rhs.intValue == 0)
                            continue;
                    }

                    // result 必须在循环内只被定义一次（避免把多次赋值之一的版本提到外面）
                    if (instr.result.type == TACOpType::VAR || instr.result.type == TACOpType::TEMP)
                    {
                        if (defCount[instr.result.name] > 1)
                            continue;
                    }

                    // 所有操作数必须是不变量
                    auto usedOps = getUsedOperands(instr);
                    bool allInvariant = true;
                    for (const auto &op : usedOps)
                    {
                        if (isConstLike(op))
                            continue;
                        if (op.type != TACOpType::VAR && op.type != TACOpType::TEMP)
                        {
                            allInvariant = false;
                            break;
                        }
                        // 不变条件：在循环前定义 且 循环内未重定义，或者本身就是本趟识别出的不变式
                        bool isInvariant =
                            (preLoopDef.count(op.name) && !definedInLoop.count(op.name)) ||
                            invariantVars.count(op.name);
                        if (!isInvariant)
                        {
                            allInvariant = false;
                            break;
                        }
                    }
                    if (!allInvariant)
                        continue;

                    invariantIndices.push_back(idx);
                    if (instr.result.type == TACOpType::VAR || instr.result.type == TACOpType::TEMP)
                    {
                        invariantVars.insert(instr.result.name);
                    }
                    changed = true;
                }
            }

            if (invariantIndices.empty())
                continue;

            // 把这些不变式从原位置删除，插入到循环 head 之前
            std::sort(invariantIndices.begin(), invariantIndices.end());

            std::vector<TACInstruction> newInstrs;
            std::unordered_set<int> toRemove(invariantIndices.begin(), invariantIndices.end());

            for (size_t i = 0; i < instrs.size(); ++i)
            {
                if (static_cast<int>(i) == lp.head)
                {
                    // 在 LABEL 之前插入不变式
                    for (int invIdx : invariantIndices)
                        newInstrs.push_back(instrs[invIdx]);
                }
                if (toRemove.find(static_cast<int>(i)) != toRemove.end())
                    continue; // 跳过已外提的指令
                newInstrs.push_back(instrs[i]);
            }

            instrs = std::move(newInstrs);
            totalHoisted += static_cast<int>(invariantIndices.size());
        }

        std::cerr << "[LICM] 完成，发现 " << loops.size() << " 个循环, 外提 "
                  << totalHoisted << " 条不变式\n";
    }

} // namespace MyCompiler
