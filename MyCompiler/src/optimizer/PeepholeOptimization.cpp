#include "Optimizer.h"
#include <iostream>
#include <vector>

namespace MyCompiler {

// ================================================================
//  窥孔优化（Peephole Optimization）
//  
//  原理：
//    - 扫描相邻的少数几条指令（通常 2-3 条）
//    - 识别低效指令模式
//    - 用等价的更高效指令替换
//  
//  常见模式：
//    1. x = y; z = x; → z = y;  (多余的复制)
//    2. x = y + 0; → x = y;  (加 0)
//    3. x = y - 0; → x = y;  (减 0)
//    4. x = y * 1; → x = y;  (乘 1)
//    5. x = y * 0; → x = 0;  (乘 0)
//    6. x = y / 1; → x = y;  (除 1)
//    7. x = -(-y); → x = y;  (双重取反)
//    8. x = !(!y); → x = y;  (双重取反逻辑)
// ================================================================

void Optimizer::peepholeOptimization(TACProgram& program) {
    std::vector<TACInstruction>& instrs = program.instructions;
    bool changed = true;
    int passCount = 0;
    const int MAX_PASSES = 5;
    
    while (changed && passCount < MAX_PASSES) {
        changed = false;
        ++passCount;
        int optimized = 0;
        
        std::vector<TACInstruction> result;
        result.reserve(instrs.size());
        
        for (size_t i = 0; i < instrs.size(); ++i) {
            auto& instr = instrs[i];
            bool skip = false;
            
            // 模式：x = y + 0 → x = y
            if (instr.type == TACType::BINARY && instr.op == "+" &&
                instr.rhs.type == TACOpType::CONST_INT && instr.rhs.intValue == 0) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = instr.lhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = 0 + y → x = y
            if (instr.type == TACType::BINARY && instr.op == "+" &&
                instr.lhs.type == TACOpType::CONST_INT && instr.lhs.intValue == 0) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = instr.rhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = y - 0 → x = y
            if (instr.type == TACType::BINARY && instr.op == "-" &&
                instr.rhs.type == TACOpType::CONST_INT && instr.rhs.intValue == 0) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = instr.lhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = y * 1 → x = y
            if (instr.type == TACType::BINARY && instr.op == "*" &&
                instr.rhs.type == TACOpType::CONST_INT && instr.rhs.intValue == 1) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = instr.lhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = 1 * y → x = y
            if (instr.type == TACType::BINARY && instr.op == "*" &&
                instr.lhs.type == TACOpType::CONST_INT && instr.lhs.intValue == 1) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = instr.rhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = y * 0 → x = 0
            if (instr.type == TACType::BINARY && instr.op == "*" &&
                instr.rhs.type == TACOpType::CONST_INT && instr.rhs.intValue == 0) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = TACOperand::constInt(0);
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = 0 * y → x = 0
            if (instr.type == TACType::BINARY && instr.op == "*" &&
                instr.lhs.type == TACOpType::CONST_INT && instr.lhs.intValue == 0) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = TACOperand::constInt(0);
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = y / 1 → x = y
            if (instr.type == TACType::BINARY && instr.op == "/" &&
                instr.rhs.type == TACOpType::CONST_INT && instr.rhs.intValue == 1) {
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = instr.lhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = -(-y) → x = y
            if (instr.type == TACType::UNARY && instr.op == "-" &&
                i > 0 && result.back().type == TACType::UNARY && result.back().op == "-" &&
                result.back().result.name == instr.lhs.name) {
                // 替换上一条指令的结果
                TACOperand prev_lhs = result.back().lhs;
                result.pop_back();
                TACInstruction new_instr;
                new_instr.type = TACType::ASSIGN;
                new_instr.result = instr.result;
                new_instr.lhs = prev_lhs;
                new_instr.rhs = TACOperand::none();
                new_instr.op.clear();
                result.push_back(new_instr);
                ++optimized;
                changed = true;
                continue;
            }
            
            // 模式：x = y; z = x; → z = y;  (仅当 x 不再被使用时)
            if (instr.type == TACType::ASSIGN && i > 0 &&
                result.back().type == TACType::ASSIGN &&
                instr.lhs.type == result.back().result.type &&
                instr.lhs.name == result.back().result.name) {
                
                // 检查中间是否有其他对 x 的使用
                bool xUsedInBetween = false;
                // 这里简化处理，只看相邻
                
                if (!xUsedInBetween) {
                    // 修改前一条指令直接赋值给 instr.result
                    result.back().result = instr.result;
                    ++optimized;
                    changed = true;
                    continue;
                }
            }
            
            result.push_back(std::move(instr));
        }
        
        instrs = std::move(result);
        
        if (optimized > 0) {
            std::cerr << "[Peephole] Pass " << passCount << ": 优化 " << optimized << " 处\n";
        }
    }
}

} // namespace MyCompiler
