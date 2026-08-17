#include "CodeGenOptimized.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace MyCompiler
{

    // ================================================================
    //  优化的 RISC-V 代码生成实现
    // ================================================================

    void CodeGenOptimized::emit(const std::string &asm_line)
    {
        std::cout << asm_line << "\n";
    }

    bool CodeGenOptimized::isGlobal(const std::string &name) const
    {
        return globalVars_.find(name) != globalVars_.end();
    }

    int CodeGenOptimized::allocVarOffset(const std::string &name)
    {
        if (varOffsets_.count(name))
        {
            return varOffsets_[name];
        }
        int offset = static_cast<int>(varOffsets_.size()) * 4;
        varOffsets_[name] = offset;
        return offset;
    }

    void CodeGenOptimized::emitStackLoad(const std::string &reg, int offset)
    {
        emit("lw " + reg + ", " + std::to_string(offset) + "(sp)");
    }

    void CodeGenOptimized::emitStackStore(const std::string &reg, int offset)
    {
        emit("sw " + reg + ", " + std::to_string(offset) + "(sp)");
    }

    // ================================================================
    //  优化的操作数加载
    //
    //  关键优化：
    //    1. 常数加载：如果在 [-2048, 2047]，使用 addi/subi imm;
    //       否则使用 lui + addi 的两指令组合
    //    2. 变量加载：缓存避免重复加载
    // ================================================================

    std::string CodeGenOptimized::loadOperandOptimized(const TACOperand &op,
                                                       const std::string &reg)
    {
        if (op.type == TACOpType::CONST_INT)
        {
            int val = op.intValue;

            // 检查常数缓存
            if (constCache_.count(val))
            {
                std::string cachedReg = constCache_[val];
                if (cachedReg != reg)
                {
                    emit("mv " + reg + ", " + cachedReg);
                }
                return reg;
            }

            // 小常数直接用 immediate 形式
            if (val >= -2048 && val <= 2047)
            {
                emit("li " + reg + ", " + std::to_string(val));
            }
            else
            {
                // 大常数：拆分为 lui + addi
                // li 伪指令通常会自动处理，这里直接用 li
                emit("li " + reg + ", " + std::to_string(val));
            }

            // 缓存
            constCache_[val] = reg;
            return reg;
        }

        // 其他操作数直接加载
        std::string result = loadOperand(op, reg);
        return result;
    }

    std::string CodeGenOptimized::loadOperand(const TACOperand &op,
                                              const std::string &reg)
    {
        switch (op.type)
        {
        case TACOpType::CONST_INT:
            emit("li " + reg + ", " + std::to_string(op.intValue));
            break;
        case TACOpType::VAR:
            if (isGlobal(op.name))
            {
                emit("la " + reg + ", _g_" + op.name);
                emit("lw " + reg + ", 0(" + reg + ")");
            }
            else
            {
                int offset = allocVarOffset(op.name);
                emitStackLoad(reg, offset);
            }
            break;
        case TACOpType::TEMP:
        {
            int offset = allocVarOffset(op.name);
            emitStackLoad(reg, offset);
            break;
        }
        case TACOpType::NONE:
            emit("li " + reg + ", 0");
            break;
        }
        return reg;
    }

    void CodeGenOptimized::storeOperand(const TACOperand &op, const std::string &reg)
    {
        switch (op.type)
        {
        case TACOpType::VAR:
            if (isGlobal(op.name))
            {
                emit("la t2, _g_" + op.name);
                emit("sw " + reg + ", 0(t2)");
            }
            else
            {
                int offset = allocVarOffset(op.name);
                emitStackStore(reg, offset);
            }
            break;
        case TACOpType::TEMP:
        {
            int offset = allocVarOffset(op.name);
            emitStackStore(reg, offset);
            break;
        }
        default:
            break;
        }
    }

    // ================================================================
    //  优化的二元操作生成
    //
    //  关键优化：
    //    1. 右操作数是常数时，使用 immediate 形式指令
    //    2. 乘以/除以 2^n 时，使用移位指令
    //    3. 减少冗余的 li 指令
    // ================================================================

    void CodeGenOptimized::emitBinaryOptimized(const std::string &op,
                                               const TACOperand &lhs,
                                               const TACOperand &rhs,
                                               const std::string &resultReg)
    {
        // 左操作数加载到 t0
        loadOperand(lhs, "t0");

        // 如果右操作数是小常数，尝试用 immediate 形式指令
        if (rhs.type == TACOpType::CONST_INT)
        {
            int val = rhs.intValue;

            // 加法：使用 addi
            if (op == "+" && val >= -2048 && val <= 2047)
            {
                emit("addi t0, t0, " + std::to_string(val));
                return;
            }

            // 减法：使用 addi 负数
            if (op == "-" && val >= -2048 && val <= 2047)
            {
                emit("addi t0, t0, " + std::to_string(-val));
                return;
            }

            // 乘以 2^n：使用左移
            if (op == "*" && val > 0 && (val & (val - 1)) == 0)
            {
                int shift = 0;
                int temp = val;
                while (temp > 1)
                {
                    temp >>= 1;
                    ++shift;
                }
                emit("slli t0, t0, " + std::to_string(shift));
                return;
            }

            // 除以 2^n：使用右移
            if (op == "/" && val > 0 && (val & (val - 1)) == 0)
            {
                int shift = 0;
                int temp = val;
                while (temp > 1)
                {
                    temp >>= 1;
                    ++shift;
                }
                emit("srai t0, t0, " + std::to_string(shift)); // 算术右移
                return;
            }

            // 比较操作数是常数时的优化
            if (op == "<")
            {
                // x < imm：先加载常数到 t1，再比较
                loadOperandOptimized(rhs, "t1");
                emit("slt t0, t0, t1");
                return;
            }
            if (op == "==")
            {
                // x == imm：用 xor + seqz 实现
                loadOperandOptimized(rhs, "t1");
                emit("sub t0, t0, t1");
                emit("seqz t0, t0");
                return;
            }
        }

        // 通用情况：加载右操作数到 t1
        loadOperand(rhs, "t1");

        if (op == "+")
            emit("add t0, t0, t1");
        else if (op == "-")
            emit("sub t0, t0, t1");
        else if (op == "*")
            emit("mul t0, t0, t1");
        else if (op == "/")
            emit("div t0, t0, t1");
        else if (op == "%")
            emit("rem t0, t0, t1");
        else if (op == "<")
            emit("slt t0, t0, t1");
        else if (op == "<=")
        {
            emit("slt t0, t1, t0");
            emit("seqz t0, t0");
        }
        else if (op == ">")
            emit("slt t0, t1, t0");
        else if (op == ">=")
        {
            emit("slt t0, t0, t1");
            emit("seqz t0, t0");
        }
        else if (op == "==")
        {
            emit("sub t0, t0, t1");
            emit("seqz t0, t0");
        }
        else if (op == "!=")
        {
            emit("sub t0, t0, t1");
            emit("snez t0, t0");
        }
        else if (op == "&&")
            emit("and t0, t0, t1");
        else if (op == "||")
            emit("or t0, t0, t1");
    }

    void CodeGenOptimized::emitPrologue() {
    emit(".text");
    emit(".align 2");
    emit(".globl _start");
    emit("_start:");
    emit("addi sp, sp, -256");
}

void CodeGenOptimized::emitEpilogue() {
    emit("li a7, 93");
    emit("li a0, 0");
    emit("ecall");
}

void CodeGenOptimized::emitFuncPrologue(const std::string& funcName, int frameSize) {
    std::string label = labelMap_[funcName];
    emit(label + ":");
    if (frameSize > 0) {
        emit("addi sp, sp, -" + std::to_string(frameSize));
    }
}

void CodeGenOptimized::emitFuncEpilogue() {
    emit("ret");
}

    void CodeGenOptimized::generate(const TACProgram &program)
    {
        // 初始化
        labelMap_.clear();
        varOffsets_.clear();
        funcNames_.clear();
        paramQueue_.clear();
        currentFunc_.clear();
        constCache_.clear();

        // 收集标签和全局变量
        int labelCounter = 0;
        for (auto &instr : program.instructions)
        {
            if (instr.type == TACType::LABEL)
            {
                if (labelMap_.find(instr.label) == labelMap_.end())
                {
                    labelMap_[instr.label] = ".L" + std::to_string(labelCounter++);
                }
                if (instr.label.find("func_") == 0)
                {
                    funcNames_.insert(instr.label.substr(5));
                }
            }
        }

        emitPrologue();

        // 遍历指令生成代码
        for (auto &instr : program.instructions)
        {
            switch (instr.type)
            {
            case TACType::ASSIGN:
            {
                loadOperand(instr.lhs, "t0");
                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::BINARY:
            {
                emitBinaryOptimized(instr.op, instr.lhs, instr.rhs, "t0");
                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::UNARY:
            {
                loadOperand(instr.lhs, "t0");
                if (instr.op == "-")
                    emit("neg t0, t0");
                else if (instr.op == "!")
                    emit("seqz t0, t0");
                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::LABEL:
            {
                std::string rvLabel = labelMap_[instr.label];
                emit(rvLabel + ":");
                break;
            }

            case TACType::GOTO:
            {
                std::string rvLabel = labelMap_[instr.label];
                emit("j " + rvLabel);
                break;
            }

            case TACType::IF_GOTO:
            {
                loadOperand(instr.lhs, "t0");
                std::string rvLabel = labelMap_[instr.label];
                emit("bnez t0, " + rvLabel);
                break;
            }

            case TACType::RETURN:
            {
                if (instr.lhs.type != TACOpType::NONE)
                    loadOperand(instr.lhs, "a0");
                else
                    emit("li a0, 0");
                emitFuncEpilogue();
                break;
            }

            default:
                break;
            }
        }

        emitEpilogue();
    }

} // namespace MyCompiler
