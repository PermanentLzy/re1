#include "CodeGen.h"
#include <iostream>
#include <algorithm>
#include <sstream>

namespace MyCompiler
{

    void CodeGen::generate(const TACProgram &program)
    {
        // ============================================================
        //  第 0 遍：收集信息
        // ============================================================
        labelMap_.clear();
        varOffsets_.clear();
        funcNames_.clear();
        paramQueue_.clear();
        currentFunc_.clear();
        funcArgIndex_ = 0;
        funcReturned_ = false;
        currentFrameSize_ = 0;
        int labelCounter = 0;

        // 收集所有标签 → RISC-V 标签映射，同时识别函数入口
        for (auto &instr : program.instructions)
        {
            if (instr.type == TACType::LABEL)
            {
                if (labelMap_.find(instr.label) == labelMap_.end())
                {
                    labelMap_[instr.label] = ".L" + std::to_string(labelCounter++);
                }
                if (instr.label.size() > 5 && instr.label.substr(0, 5) == "func_")
                {
                    funcNames_.insert(instr.label.substr(5));
                }
            }
            if (instr.type == TACType::CALL && !instr.label.empty())
            {
                funcNames_.insert(instr.label);
            }
        }

        // ============================================================
        //  第 1 遍：收集全局变量初始化（迭代常量折叠）
        // ============================================================
        globalVars_.clear();
        globalInit_.clear();
        bool pastFirstFunc = false;
        std::unordered_map<std::string, int> constMap; // 变量名→常量值

        // 辅助：尝试获取操作数的常量值
        auto tryGet = [&](const TACOperand& op, int& val) -> bool {
            if (op.type == TACOpType::CONST_INT) { val = op.intValue; return true; }
            auto it = constMap.find(op.name);
            if (it != constMap.end()) { val = it->second; return true; }
            return false;
        };
        // 辅助：折叠二元运算
        auto foldBin = [](int l, int r, const std::string& op) -> int {
            if (op == "+") return l+r; if (op == "-") return l-r;
            if (op == "*") return l*r; if (op == "/") return r?l/r:0;
            if (op == "%") return r?l%r:0;
            return 0;
        };

        // 迭代直到不动点（处理常量传播链）
        bool changed = true;
        while (changed) {
            changed = false;
            pastFirstFunc = false;
            for (auto &instr : program.instructions) {
                if (instr.type == TACType::LABEL &&
                    instr.label.size() > 5 && instr.label.substr(0,5) == "func_")
                    pastFirstFunc = true;
                if (pastFirstFunc) continue;

                // ASSIGN: 将已知常量传播到目标
                if (instr.type == TACType::ASSIGN) {
                    int val;
                    if (tryGet(instr.lhs, val)) {
                        auto& name = instr.result.name;
                        if (!name.empty() && constMap.find(name) == constMap.end()) {
                            constMap[name] = val;
                            if (instr.result.type == TACOpType::VAR) {
                                globalVars_.insert(name);
                                globalInit_.push_back({name, val});
                            }
                            changed = true;
                        }
                    }
                }
                // BINARY: 编译期求值
                if (instr.type == TACType::BINARY) {
                    int lv, rv;
                    if (tryGet(instr.lhs, lv) && tryGet(instr.rhs, rv)) {
                        int val = foldBin(lv, rv, instr.op);
                        auto& name = instr.result.name;
                        if (!name.empty() && constMap.find(name) == constMap.end()) {
                            constMap[name] = val;
                            if (instr.result.type == TACOpType::VAR) {
                                globalVars_.insert(name);
                                globalInit_.push_back({name, val});
                            }
                            changed = true;
                        }
                    }
                }
                // UNARY: 编译期求值
                if (instr.type == TACType::UNARY) {
                    int ov;
                    if (tryGet(instr.lhs, ov)) {
                        int val = (instr.op == "-") ? -ov : (ov == 0 ? 1 : 0);
                        auto& name = instr.result.name;
                        if (!name.empty() && constMap.find(name) == constMap.end()) {
                            constMap[name] = val;
                            if (instr.result.type == TACOpType::VAR) {
                                globalVars_.insert(name);
                                globalInit_.push_back({name, val});
                            }
                            changed = true;
                        }
                    }
                }
            }
        }

        // ============================================================
        //  输出 .data 段（全局变量）
        // ============================================================
        if (!globalInit_.empty())
        {
            emit(".data");
            for (auto &gv : globalInit_)
            {
                emit("_g_" + gv.first + ": .word " + std::to_string(gv.second));
            }
        }

        // ============================================================
        //  预扫描：统计每个函数的变量数，计算动态帧大小
        // ============================================================
        std::unordered_map<std::string, int> funcFrameSizes;
        {
            std::string curFn;
            std::unordered_set<std::string> varNames;
            for (auto &instr : program.instructions) {
                if (instr.type == TACType::LABEL && instr.label.find("func_") == 0) {
                    if (!curFn.empty()) {
                        int frame = std::max(256, (static_cast<int>(varNames.size()) + 4) * 4);
                        funcFrameSizes[curFn] = frame;
                    }
                    curFn = instr.label.substr(5);
                    varNames.clear();
                }
                // 收集所有变量/临时变量名
                auto collect = [&](const TACOperand& op) {
                    if ((op.type == TACOpType::VAR || op.type == TACOpType::TEMP) && !op.name.empty())
                        varNames.insert(op.name);
                };
                collect(instr.result);
                collect(instr.lhs);
                collect(instr.rhs);
            }
            if (!curFn.empty()) {
                int frame = std::max(256, (static_cast<int>(varNames.size()) + 4) * 4);
                funcFrameSizes[curFn] = frame;
            }
        }

        // 计算最大帧大小（用于跨函数参数传递缓冲）
        int maxFrameSize = 256;
        for (auto& kv : funcFrameSizes)
            if (kv.second > maxFrameSize) maxFrameSize = kv.second;

        // ============================================================
        //  输出代码段头部
        // ============================================================
        emit(".text");

        // ============================================================
        //  遍历 TAC 指令，生成 RISC-V 汇编
        //  IR 不再有 GOTO wrapper，函数体直接顺序输出
        // ============================================================
        for (auto &instr : program.instructions)
        {
            // --- 函数入口标签 ---
            if (instr.type == TACType::LABEL)
            {
                std::string funcName;
                if (instr.label.size() > 5 && instr.label.substr(0, 5) == "func_")
                {
                    funcName = instr.label.substr(5);
                }

                if (!funcName.empty() && funcNames_.count(funcName))
                {
                    if (!currentFunc_.empty())
                        emitFuncEpilogue();

                    currentFunc_ = funcName;
                    varOffsets_.clear();
                    funcArgIndex_ = 0;
                    funcReturned_ = false;
                    currentFrameSize_ = 0;
                    int fs = funcFrameSizes.count(funcName) ? funcFrameSizes[funcName] : 256;
                    emitFuncPrologue(funcName, fs);
                    continue;
                }
            }

            // --- RETURN ---
            if (instr.type == TACType::RETURN)
            {
                if (!currentFunc_.empty())
                {
                    if (instr.lhs.type != TACOpType::NONE)
                        loadOperand(instr.lhs, "a0");
                    else
                        emit("li a0, 0");
                    emitFuncEpilogue();
                    // 不清理 currentFunc_，允许后续 return 也生成 epilogue
                }
                continue;
            }

            // --- 指令翻译 ---
            switch (instr.type)
            {
            case TACType::PARAM:
            {
                paramQueue_.push_back(instr.lhs.name);
                break;
            }

            case TACType::FUNC_ARG:
            {
                if (funcArgIndex_ < 8) {
                    std::string argReg = "a" + std::to_string(funcArgIndex_);
                    storeOperand(instr.result, argReg);
                } else {
                    int offset = currentFrameSize_ + maxFrameSize + (funcArgIndex_ - 8) * 4;
                    emitStackLoad("t0", offset);
                    storeOperand(instr.result, "t0");
                }
                funcArgIndex_++;
                break;
            }

            case TACType::ASSIGN:
            {
                // x = y
                loadOperand(instr.lhs, "t0");
                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::BINARY:
            {
                // x = y op z
                loadOperand(instr.lhs, "t0");
                loadOperand(instr.rhs, "t1");

                if (instr.op == "+")
                    emit("add t0, t0, t1");
                else if (instr.op == "-")
                    emit("sub t0, t0, t1");
                else if (instr.op == "*")
                    emit("mul t0, t0, t1");
                else if (instr.op == "/")
                    emit("div t0, t0, t1");
                else if (instr.op == "%")
                    emit("rem t0, t0, t1");
                else if (instr.op == "<")
                    emit("slt t0, t0, t1");
                else if (instr.op == "<=")
                {
                    emit("slt t0, t1, t0");
                    emit("seqz t0, t0");
                }
                else if (instr.op == ">")
                    emit("slt t0, t1, t0");
                else if (instr.op == ">=")
                {
                    emit("slt t0, t0, t1");
                    emit("seqz t0, t0");
                }
                else if (instr.op == "==")
                {
                    emit("sub t0, t0, t1");
                    emit("seqz t0, t0");
                }
                else if (instr.op == "!=")
                {
                    emit("sub t0, t0, t1");
                    emit("snez t0, t0");
                }
                else if (instr.op == "&&")
                    emit("and t0, t0, t1");
                else if (instr.op == "||")
                    emit("or t0, t0, t1");

                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::UNARY:
            {
                // x = op y
                loadOperand(instr.lhs, "t0");

                if (instr.op == "-")
                    emit("neg t0, t0");
                else if (instr.op == "!")
                    emit("seqz t0, t0");

                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::GOTO:
            {
                std::string rvLabel = labelMap_.count(instr.label) ? labelMap_[instr.label] : instr.label;
                emit("j " + rvLabel);
                break;
            }

            case TACType::IF_GOTO:
            {
                loadOperand(instr.lhs, "t0");
                std::string rvLabel = labelMap_.count(instr.label) ? labelMap_[instr.label] : instr.label;
                emit("bnez t0, " + rvLabel);
                break;
            }

            case TACType::LABEL:
            {
                std::string rvLabel = labelMap_.count(instr.label) ? labelMap_[instr.label] : instr.label;
                emit(rvLabel + ":");
                break;
            }

            case TACType::CALL:
            {
                // 将参数队列中的值加载到 a0-a7，额外参数放栈上
                int argCount = std::min(static_cast<int>(paramQueue_.size()), static_cast<int>(instr.lhs.intValue));
                int regArgs = std::min(argCount, 8);
                for (int i = 0; i < regArgs; ++i)
                {
                    std::string argReg = "a" + std::to_string(i);
                    loadOperand(TACOperand::var(paramQueue_[i]), argReg);
                }
                // 额外参数 (>=8)：使用 maxFrameSize 缓冲区
                for (int i = 8; i < argCount; ++i)
                {
                    loadOperand(TACOperand::var(paramQueue_[i]), "t0");
                    emitStackStore("t0", maxFrameSize + (i - 8) * 4);
                }
                paramQueue_.clear();

                // 发射 call 指令（main 函数不加 func_ 前缀）
                if (instr.label == "main")
                    emit("call main");
                else
                    emit("call func_" + instr.label);

                // 如有返回值，存储到 result
                if (instr.result.type != TACOpType::NONE)
                {
                    storeOperand(instr.result, "a0");
                }
                break;
            }

            case TACType::NOP:
                break;

            default:
                break;
            }
        }

        // 函数末尾无显式 return，补尾声
        if (!currentFunc_.empty())
        {
            emit("li a0, 0");
            emitFuncEpilogue();
        }
    }

    // ================================================================
    //  辅助方法
    // ================================================================

    void CodeGen::emit(const std::string &line)
    {
        std::cout << line << std::endl;
    }

    // 带大偏移的栈加载：offset 超 12 位范围时用 li+add 两步法
    void CodeGen::emitStackLoad(const std::string& reg, int offset)
    {
        if (offset >= -2048 && offset <= 2047) {
            emit("lw " + reg + ", " + std::to_string(offset) + "(sp)");
        } else {
            emit("li t2, " + std::to_string(offset));
            emit("add t2, sp, t2");
            emit("lw " + reg + ", 0(t2)");
        }
    }

    void CodeGen::emitStackStore(const std::string& reg, int offset)
    {
        if (offset >= -2048 && offset <= 2047) {
            emit("sw " + reg + ", " + std::to_string(offset) + "(sp)");
        } else {
            emit("li t2, " + std::to_string(offset));
            emit("add t2, sp, t2");
            emit("sw " + reg + ", 0(t2)");
        }
    }

    std::string CodeGen::loadOperand(const TACOperand &op, const std::string &reg)
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

    void CodeGen::storeOperand(const TACOperand &op, const std::string &reg)
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
        case TACOpType::CONST_INT:
        case TACOpType::NONE:
            break;
        }
    }

    int CodeGen::allocVarOffset(const std::string &name)
    {
        if (varOffsets_.count(name))
        {
            return varOffsets_[name];
        }
        // 栈向下增长，变量存放在 sp + offset 位置（正偏移）
        int offset = static_cast<int>(varOffsets_.size()) * 4;
        varOffsets_[name] = offset;
        return offset;
    }

    void CodeGen::emitPrologue()
    {
        emit(".text");
        emit(".globl _start");
        emit("_start:");
        emit("addi sp, sp, -256");
        emit("sw ra, 252(sp)");
    }

    void CodeGen::emitFuncPrologue(const std::string& funcName, int frameSize)
    {
        if (frameSize < 256) frameSize = 256;
        currentFrameSize_ = frameSize;

        emit("");
        if (funcName == "main") {
            emit(".globl main");
            emit("main:");
        } else {
            emit("func_" + funcName + ":");
        }
        // addi 立即数范围 [-2048,2047]，用两步法避免越界
        emit("li t2, " + std::to_string(frameSize));
        emit("sub sp, sp, t2");
        emit("sw ra, " + std::to_string(frameSize - 4) + "(sp)");

        varOffsets_.clear();
    }

    void CodeGen::emitFuncEpilogue()
    {
        int frameSize = currentFrameSize_;
        if (frameSize <= 0) frameSize = 256;
        emit("lw ra, " + std::to_string(frameSize - 4) + "(sp)");
        // 用 li + add 代替 addi，支持大立即数
        emit("li t2, " + std::to_string(frameSize));
        emit("add sp, sp, t2");
        emit("ret");
    }

    void CodeGen::emitExit()
    {
        emit("li a7, 93");
        emit("ecall");
    }

    bool CodeGen::isGlobal(const std::string& name) const
    {
        return globalVars_.count(name) > 0;
    }

} // namespace MyCompiler
