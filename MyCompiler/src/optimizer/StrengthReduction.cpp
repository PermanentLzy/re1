#include "Optimizer.h"
#include <iostream>
#include <unordered_map>
#include <cmath>

namespace MyCompiler {

// ================================================================
//  强度削弱优化（Strength Reduction）
//  
//  原理：
//    - 将昂贵操作转化为便宜操作
//    - 乘以 2^n → 左移 n 位（更快）
//    - 乘以 3 → a + (a << 1)（有时更快）
//    - 除以 2^n → 右移 n 位
//    - 循环中的 induction 变量优化（后续扩展）
// ================================================================

void Optimizer::strengthReduction(TACProgram& program) {
    for (auto& instr : program.instructions) {
        if (instr.type != TACType::BINARY) continue;
        
        // 乘法优化：x * 2^n → x << n
        if (instr.op == "*" && instr.rhs.type == TACOpType::CONST_INT) {
            int multiplier = instr.rhs.intValue;
            
            // 检查是否是 2 的幂
            if (multiplier > 0 && (multiplier & (multiplier - 1)) == 0) {
                // 计算 log2(multiplier)
                int shift = 0;
                int temp = multiplier;
                while (temp > 1) {
                    temp >>= 1;
                    ++shift;
                }
                
                // 替换为左移
                instr.op = "<<";
                instr.rhs.intValue = shift;
                std::cerr << "[StrengthRed] 乘以 " << multiplier 
                         << " → 左移 " << shift << " 位\n";
            }
            // 乘以 3 的特殊优化：x * 3 → x + (x << 1)
            else if (multiplier == 3) {
                // 需要两条指令：
                // t_shift = x << 1
                // result = x + t_shift
                TACOperand shiftTemp = TACOperand::temp("t_sr_" + instr.result.name);
                
                // 插入左移指令
                TACInstruction shiftInstr;
                shiftInstr.type = TACType::BINARY;
                shiftInstr.result = shiftTemp;
                shiftInstr.lhs = instr.lhs;
                shiftInstr.op = "<<";
                shiftInstr.rhs = TACOperand::constInt(1);
                
                // 原指令改为加法
                instr.op = "+";
                instr.rhs = shiftTemp;
                
                std::cerr << "[StrengthRed] 乘以 3 → 加法 + 左移\n";
            }
        }
        // 除法优化：x / 2^n → x >> n（无符号）
        else if (instr.op == "/" && instr.rhs.type == TACOpType::CONST_INT) {
            int divisor = instr.rhs.intValue;
            
            if (divisor > 0 && (divisor & (divisor - 1)) == 0) {
                int shift = 0;
                int temp = divisor;
                while (temp > 1) {
                    temp >>= 1;
                    ++shift;
                }
                
                instr.op = ">>";
                instr.rhs.intValue = shift;
                std::cerr << "[StrengthRed] 除以 " << divisor 
                         << " → 右移 " << shift << " 位\n";
            }
        }
    }
}

} // namespace MyCompiler
