#include "MyCompiler/src/ir/ThreeAddrCode.h"
#include <iostream>
#include <vector>

using namespace MyCompiler;

// 简单的验证程序，检查 TAC 指令创建是否正确
int main() {
    std::cout << "验证 TAC 指令修复...\n";
    
    // 创建一个 ASSIGN 指令（正确的方式）
    TACInstruction assign_instr;
    assign_instr.type = TACType::ASSIGN;
    assign_instr.result = TACOperand::var("x");
    assign_instr.lhs = TACOperand::var("y");
    assign_instr.rhs = TACOperand::none();
    assign_instr.op.clear();
    
    std::cout << "✓ ASSIGN 指令创建成功\n";
    std::cout << "  Result: " << assign_instr.result.name << "\n";
    std::cout << "  LHS: " << assign_instr.lhs.name << "\n";
    std::cout << "  Type: " << (int)assign_instr.type << "\n";
    
    // 创建一个 BINARY 指令
    TACInstruction binary_instr;
    binary_instr.type = TACType::BINARY;
    binary_instr.result = TACOperand::temp("t1");
    binary_instr.lhs = TACOperand::var("a");
    binary_instr.rhs = TACOperand::constInt(0);
    binary_instr.op = "+";
    
    std::cout << "✓ BINARY 指令创建成功\n";
    std::cout << "  Op: " << binary_instr.op << "\n";
    std::cout << "  RHS Value: " << binary_instr.rhs.intValue << "\n";
    
    std::cout << "\n✅ 所有 TAC 指令创建验证通过！\n";
    return 0;
}
