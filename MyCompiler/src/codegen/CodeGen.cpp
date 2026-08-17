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
        currentFuncIsLeaf_ = false;
        funcIsLeaf_.clear();
        lastLineValid_ = false;
        lastEmittedLine_.clear();
        lastStoreValid_ = false;
        lastEmittedWasRet_ = false;
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
        auto tryGet = [&](const TACOperand &op, int &val) -> bool
        {
            if (op.type == TACOpType::CONST_INT)
            {
                val = op.intValue;
                return true;
            }
            auto it = constMap.find(op.name);
            if (it != constMap.end())
            {
                val = it->second;
                return true;
            }
            return false;
        };
        // 辅助：折叠二元运算
        auto foldBin = [](int l, int r, const std::string &op) -> int
        {
            if (op == "+")
                return l + r;
            if (op == "-")
                return l - r;
            if (op == "*")
                return l * r;
            if (op == "/")
                return r ? l / r : 0;
            if (op == "%")
                return r ? l % r : 0;
            return 0;
        };

        // 迭代直到不动点（处理常量传播链）
        bool changed = true;
        while (changed)
        {
            changed = false;
            pastFirstFunc = false;
            for (auto &instr : program.instructions)
            {
                if (instr.type == TACType::LABEL &&
                    instr.label.size() > 5 && instr.label.substr(0, 5) == "func_")
                    pastFirstFunc = true;
                if (pastFirstFunc)
                    continue;

                // ASSIGN: 将已知常量传播到目标
                if (instr.type == TACType::ASSIGN)
                {
                    int val;
                    if (tryGet(instr.lhs, val))
                    {
                        auto &name = instr.result.name;
                        if (!name.empty() && constMap.find(name) == constMap.end())
                        {
                            constMap[name] = val;
                            if (instr.result.type == TACOpType::VAR)
                            {
                                globalVars_.insert(name);
                                globalInit_.push_back({name, val});
                            }
                            changed = true;
                        }
                    }
                }
                // BINARY: 编译期求值
                if (instr.type == TACType::BINARY)
                {
                    int lv, rv;
                    if (tryGet(instr.lhs, lv) && tryGet(instr.rhs, rv))
                    {
                        int val = foldBin(lv, rv, instr.op);
                        auto &name = instr.result.name;
                        if (!name.empty() && constMap.find(name) == constMap.end())
                        {
                            constMap[name] = val;
                            if (instr.result.type == TACOpType::VAR)
                            {
                                globalVars_.insert(name);
                                globalInit_.push_back({name, val});
                            }
                            changed = true;
                        }
                    }
                }
                // UNARY: 编译期求值
                if (instr.type == TACType::UNARY)
                {
                    int ov;
                    if (tryGet(instr.lhs, ov))
                    {
                        int val = (instr.op == "-") ? -ov : (ov == 0 ? 1 : 0);
                        auto &name = instr.result.name;
                        if (!name.empty() && constMap.find(name) == constMap.end())
                        {
                            constMap[name] = val;
                            if (instr.result.type == TACOpType::VAR)
                            {
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
        //  同时识别叶函数（不调用其他函数）以优化帧大小
        // ============================================================
        std::unordered_map<std::string, int> funcFrameSizes;
        std::unordered_map<std::string, bool> funcIsLeaf;
        {
            std::string curFn;
            std::unordered_set<std::string> varNames;
            bool hasCall = false;
            for (auto &instr : program.instructions)
            {
                if (instr.type == TACType::LABEL && instr.label.find("func_") == 0)
                {
                    if (!curFn.empty())
                    {
                        bool isLeaf = !hasCall;
                        // 统一帧大小：至少 256 字节，便于跨函数参数传递
                        int frame = std::max(256, (static_cast<int>(varNames.size()) + (isLeaf ? 0 : 1)) * 4);
                        // 对齐到 16 字节
                        frame = (frame + 15) & ~15;
                        funcFrameSizes[curFn] = frame;
                        funcIsLeaf_[curFn] = isLeaf;
                    }
                    curFn = instr.label.substr(5);
                    varNames.clear();
                    hasCall = false;
                }
                // 检测 CALL 指令
                if (instr.type == TACType::CALL)
                {
                    hasCall = true;
                }
                // 收集所有变量/临时变量名
                auto collect = [&](const TACOperand &op)
                {
                    if ((op.type == TACOpType::VAR || op.type == TACOpType::TEMP) && !op.name.empty())
                        varNames.insert(op.name);
                };
                collect(instr.result);
                collect(instr.lhs);
                collect(instr.rhs);
            }
            if (!curFn.empty())
            {
                bool isLeaf = !hasCall;
                int frame = std::max(256, (static_cast<int>(varNames.size()) + (isLeaf ? 0 : 1)) * 4);
                frame = (frame + 15) & ~15;
                funcFrameSizes[curFn] = frame;
                funcIsLeaf_[curFn] = isLeaf;
            }
        }

        // 计算最大帧大小（用于跨函数参数传递缓冲）
        int maxFrameSize = 256;
        for (auto &kv : funcFrameSizes)
            if (kv.second > maxFrameSize)
                maxFrameSize = kv.second;

        // ============================================================
        //  输出代码段头部
        // ============================================================
        emit(".text");
        emit(".align 2");

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
                    // 前一个函数的 fallback epilogue：若已 return 则跳过
                    if (!currentFunc_.empty() && !funcReturned_)
                        emitFuncEpilogue();

                    currentFunc_ = funcName;
                    varOffsets_.clear();
                    funcArgIndex_ = 0;
                    funcReturned_ = false;
                    currentFrameSize_ = 0;
                    // 重置 peephole 状态，避免跨函数误优化
                    lastStoreValid_ = false;
                    lastLineValid_ = false;
                    lastEmittedLine_.clear();
                    // 重置寄存器跟踪
                    varInReg_.clear();
                    int fs = funcFrameSizes.count(funcName) ? funcFrameSizes[funcName] : 256;
                    emitFuncPrologue(funcName, fs);
                    continue;
                }
            }

            // --- RETURN ---
            if (instr.type == TACType::RETURN)
            {
                if (!currentFunc_.empty() && !funcReturned_)
                {
                    if (instr.lhs.type != TACOpType::NONE)
                        loadOperand(instr.lhs, "a0");
                    else
                        emitLi("a0", 0);
                    emitFuncEpilogue();
                    funcReturned_ = true;
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
                if (funcArgIndex_ < 8)
                {
                    std::string argReg = "a" + std::to_string(funcArgIndex_);
                    storeOperand(instr.result, argReg);
                }
                else
                {
                    int offset = currentFrameSize_ + maxFrameSize + (funcArgIndex_ - 8) * 4;
                    emitStackLoad("t0", offset);
                    storeOperand(instr.result, "t0");
                }
                funcArgIndex_++;
                break;
            }

            case TACType::ASSIGN:
            {
                // 如果是全局变量的初始赋值（程序级别，不在函数内），跳过运行时代码生成
                // 因为 .data 段的 .word 已经处理了静态初始化
                if (!currentFunc_.empty() && instr.result.type == TACOpType::VAR && isGlobal(instr.result.name))
                {
                    // 函数内对全局变量的赋值 → 正常生成代码
                }
                else if (currentFunc_.empty() && instr.result.type == TACOpType::VAR && isGlobal(instr.result.name))
                {
                    // 程序级全局变量初始化 → 跳过，.data 段已处理
                    break;
                }
                loadOperand(instr.lhs, "t0");
                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::BINARY:
            {
                // x = y op z
                // 优化：右操作数为常数时使用 immediate 形式指令
                if (instr.rhs.type == TACOpType::CONST_INT)
                {
                    int c = instr.rhs.intValue;
                    // 辅助：将有符号立即数转为 12 位无符号表示（避免某些汇编器不接受负数）
                    auto toImm12 = [](int val) -> std::string
                    {
                        int v = val & 0xFFF; // 12-bit two's complement
                        return std::to_string(v);
                    };
                    // 加法：addi
                    if (instr.op == "+" && c >= -2048 && c <= 2047)
                    {
                        loadOperand(instr.lhs, "t0");
                        emit("addi t0, t0, " + toImm12(c));
                        storeOperand(instr.result, "t0");
                        break;
                    }
                    // 减法：addi 负数
                    if (instr.op == "-" && c >= -2048 && c <= 2047)
                    {
                        loadOperand(instr.lhs, "t0");
                        emit("addi t0, t0, " + toImm12(-c));
                        storeOperand(instr.result, "t0");
                        break;
                    }
                    // 乘以 2^n：slli（确保移位量在 0-31 范围内）
                    if (instr.op == "*" && c > 0 && (c & (c - 1)) == 0)
                    {
                        int shift = 0, t = c;
                        while (t > 1 && shift <= 31)
                        {
                            t >>= 1;
                            ++shift;
                        }
                        if (shift <= 31)
                        {
                            loadOperand(instr.lhs, "t0");
                            emit("slli t0, t0, " + std::to_string(shift));
                            storeOperand(instr.result, "t0");
                            break;
                        }
                    }
                    // 除以 2^n：srai（算术右移，确保移位量在 0-31 范围内）
                    if (instr.op == "/" && c > 0 && (c & (c - 1)) == 0)
                    {
                        int shift = 0, t = c;
                        while (t > 1 && shift <= 31)
                        {
                            t >>= 1;
                            ++shift;
                        }
                        if (shift <= 31)
                        {
                            loadOperand(instr.lhs, "t0");
                            emit("srai t0, t0, " + std::to_string(shift));
                            storeOperand(instr.result, "t0");
                            break;
                        }
                    }
                    // 模 2^n：用 and 屏蔽低位（确保立即数在 0-4095 范围内）
                    if (instr.op == "%" && c > 0 && (c & (c - 1)) == 0)
                    {
                        int mask = c - 1;
                        if (mask >= 0 && mask <= 4095)
                        {
                            loadOperand(instr.lhs, "t0");
                            emit("andi t0, t0, " + std::to_string(mask));
                            storeOperand(instr.result, "t0");
                            break;
                        }
                    }
                    // 与 0 比较：直接 seqz/snez
                    if (c == 0 && (instr.op == "==" || instr.op == "!="))
                    {
                        loadOperand(instr.lhs, "t0");
                        if (instr.op == "==")
                            emitSeqz("t0", "t0");
                        else
                            emitSnez("t0", "t0");
                        storeOperand(instr.result, "t0");
                        break;
                    }
                    // 比较运算符与常数 RHS：使用立即数形式 slti
                    // i < c  → slti t0, t0, c
                    // i >= c → slti t0, t0, c; xori t0, t0, 1
                    if (c >= -2048 && c <= 2047 &&
                        (instr.op == "<" || instr.op == ">="))
                    {
                        loadOperand(instr.lhs, "t0");
                        emit("slti t0, t0, " + toImm12(c));
                        if (instr.op == ">=")
                            emit("xori t0, t0, 1");
                        storeOperand(instr.result, "t0");
                        break;
                    }
                }

                // 通用情况：加载左右操作数到 t0/t1
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
                    emitSeqz("t0", "t0");
                }
                else if (instr.op == ">")
                    emit("slt t0, t1, t0");
                else if (instr.op == ">=")
                {
                    emit("slt t0, t0, t1");
                    emitSeqz("t0", "t0");
                }
                else if (instr.op == "==")
                {
                    emit("sub t0, t0, t1");
                    emitSeqz("t0", "t0");
                }
                else if (instr.op == "!=")
                {
                    emit("sub t0, t0, t1");
                    emitSnez("t0", "t0");
                }
                else if (instr.op == "&&")
                    emit("and t0, t0, t1");
                else if (instr.op == "||")
                    emit("or t0, t0, t1");
                // 处理 StrengthReduction 产生的位移操作
                else if (instr.op == "<<")
                {
                    // 右操作数是常数：用 slli
                    if (instr.rhs.type == TACOpType::CONST_INT &&
                        instr.rhs.intValue >= 0 && instr.rhs.intValue <= 31)
                    {
                        emit("slli t0, t0, " + std::to_string(instr.rhs.intValue));
                    }
                    else
                    {
                        // 变量位移量：sll
                        loadOperand(instr.rhs, "t1");
                        emit("sll t0, t0, t1");
                    }
                }
                else if (instr.op == ">>")
                {
                    if (instr.rhs.type == TACOpType::CONST_INT &&
                        instr.rhs.intValue >= 0 && instr.rhs.intValue <= 31)
                    {
                        emit("srai t0, t0, " + std::to_string(instr.rhs.intValue));
                    }
                    else
                    {
                        loadOperand(instr.rhs, "t1");
                        emit("sra t0, t0, t1");
                    }
                }

                storeOperand(instr.result, "t0");
                break;
            }

            case TACType::UNARY:
            {
                // x = op y
                loadOperand(instr.lhs, "t0");

                if (instr.op == "-")
                    emitNeg("t0", "t0");
                else if (instr.op == "!")
                    emitSeqz("t0", "t0");

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
                // 对于非叶函数，标签是基本块边界，清除寄存器跟踪
                // 对于叶函数，寄存器不会被调用破坏，可以保留跟踪
                if (!currentFuncIsLeaf_)
                {
                    varInReg_.clear();
                }
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
                // 使用 jal x1 保存返回地址到 ra 寄存器
                if (instr.label == "main")
                    emit("jal x1, main");
                else
                    emit("jal x1, func_" + instr.label);

                // CALL 后清除寄存器跟踪（被调用者可能修改任何寄存器）
                varInReg_.clear();

                // 如有返回值，存储到 result
                if (instr.result.type != TACOpType::NONE)
                {
                    storeOperand(instr.result, "a0");
                    // 标记返回值在 a0 中
                    if (instr.result.type == TACOpType::VAR || instr.result.type == TACOpType::TEMP)
                        varInReg_[instr.result.name] = "a0";
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
        // 若函数已 return，则跳过不可达的 fallback epilogue
        if (!currentFunc_.empty() && !funcReturned_)
        {
            emitLi("a0", 0);
            emitFuncEpilogue();
        }
    }

    // ================================================================
    //  辅助方法
    // ================================================================

    void CodeGen::emit(const std::string &line)
    {
        // 使用 \n 代替 std::endl，避免 Windows CRLF 导致 Linux 汇编器错误
        std::cout << line << "\n";
        // 跟踪最后一条是否为 ret/jalr（用于跳过不可达 fallback epilogue）
        lastEmittedWasRet_ = (line == "ret");
        // 任何 emit 调用都会使跨寄存器 sw→lw peephole 的"紧邻"条件失效
        // (emitStackStore 在 emit 后会重新设置 lastStoreValid_ = true)
        lastStoreValid_ = false;
        // 跟踪上一条指令用于 peephole；标签/控制流相关指令使跟踪失效
        if (!line.empty() && line.back() == ':')
        {
            // 标签行：控制流汇合点，使跟踪失效
            lastLineValid_ = false;
            lastEmittedLine_.clear();
            return;
        }
        // 控制流指令也使跟踪失效（避免跨基本块误删）
        if (line.rfind("jal ", 0) == 0 || line.rfind("jalr", 0) == 0 ||
            line.rfind("bne ", 0) == 0 || line.rfind("beq ", 0) == 0 ||
            line.rfind("blt ", 0) == 0 || line.rfind("bge ", 0) == 0 ||
            line.rfind("ble ", 0) == 0 || line.rfind("bgt ", 0) == 0 ||
            line.rfind("call", 0) == 0 || line.rfind("ecall", 0) == 0)
        {
            lastLineValid_ = false;
            lastEmittedLine_.clear();
            return;
        }
        lastEmittedLine_ = line;
        lastLineValid_ = true;
    }

    // 带大偏移的栈加载：offset 超 12 位范围时用 li+add 两步法
    // Peephole 优化：
    //   1. 若上一条刚发射的是 "sw reg, offset(sp)" 且寄存器相同 → 跳过 load
    //   2. 若上一条刚发射的是 "sw regX, offset(sp)" 且寄存器不同 → 替换为 "mv reg, regX"
    void CodeGen::emitStackLoad(const std::string &reg, int offset)
    {
        if (offset >= -2048 && offset <= 2047)
        {
            std::string loadLine = "lw " + reg + ", " + std::to_string(offset) + "(sp)";
            // 优先检查增强peephole（跨寄存器 sw→lw 消除）
            // 注意：ra 是特殊寄存器（返回地址），不能被 peephole 删除！
            if (reg != "ra" && lastStoreValid_ && lastStoreOffset_ == offset)
            {
                if (lastStoreReg_ == reg)
                {
                    // 同寄存器：跳过 load（值已在 reg 中）
                    lastStoreValid_ = false;
                    lastLineValid_ = false;
                    lastEmittedLine_.clear();
                    return;
                }
                else
                {
                    // 跨寄存器：用 addi rd, rs1, 0 替代 lw（更高效）
                    lastStoreValid_ = false;
                    emit("addi " + reg + ", " + lastStoreReg_ + ", 0");
                    return;
                }
            }
            // 兼容旧peephole（检查 lastEmittedLine_ 是否完全匹配）
            // 同样保护 ra 寄存器
            if (reg != "ra" && lastLineValid_ && lastEmittedLine_ == "sw " + reg + ", " + std::to_string(offset) + "(sp)")
            {
                lastLineValid_ = false;
                lastEmittedLine_.clear();
                return; // 跳过冗余 load
            }
            emit(loadLine);
        }
        else
        {
            emitLi("t2", offset);
            emit("add t2, sp, t2");
            emit("lw " + reg + ", 0(t2)");
        }
    }

    void CodeGen::emitStackStore(const std::string &reg, int offset)
    {
        if (offset >= -2048 && offset <= 2047)
        {
            emit("sw " + reg + ", " + std::to_string(offset) + "(sp)");
            // 记录此次 store 供 emitStackLoad 做跨寄存器 peephole
            // 注意：emit() 已将 lastStoreValid_ 置 false，此处恢复为 true
            lastStoreReg_ = reg;
            lastStoreOffset_ = offset;
            lastStoreValid_ = true;
        }
        else
        {
            emitLi("t2", offset);
            emit("add t2, sp, t2");
            emit("sw " + reg + ", 0(t2)");
            lastStoreValid_ = false;
        }
    }

    std::string CodeGen::loadOperand(const TACOperand &op, const std::string &reg)
    {
        // 当向寄存器加载新值时，使该寄存器中旧变量的跟踪失效
        for (auto it = varInReg_.begin(); it != varInReg_.end(); ++it)
        {
            if (it->second == reg && it->first != op.name)
            {
                varInReg_.erase(it->first);
                break;
            }
        }

        switch (op.type)
        {
        case TACOpType::CONST_INT:
            emitLi(reg, op.intValue);
            break;
        case TACOpType::VAR:
            if (isGlobal(op.name))
            {
                // 使用 lui + addi 加载全局变量地址（比 la 更兼容）
                emit("lui " + reg + ", %hi(_g_" + op.name + ")");
                emit("addi " + reg + ", " + reg + ", %lo(_g_" + op.name + ")");
                emit("lw " + reg + ", 0(" + reg + ")");
            }
            else
            {
                // 检查变量是否已在寄存器中
                auto it = varInReg_.find(op.name);
                if (it != varInReg_.end() && it->second != reg)
                {
                    // 变量在另一个寄存器中，用 addi 替代 lw
                    emit("addi " + reg + ", " + it->second + ", 0");
                    break;
                }
                else if (it != varInReg_.end() && it->second == reg)
                {
                    // 变量已在目标寄存器中，跳过 load
                    break;
                }
                // 变量不在寄存器中：如果有栈帧，从栈加载
                if (currentFrameSize_ > 0)
                {
                    int offset = allocVarOffset(op.name);
                    emitStackLoad(reg, offset);
                    // 记录变量在寄存器中
                    varInReg_[op.name] = reg;
                }
                else
                {
                    // 无栈帧且变量不在寄存器中，安全回退
                    emitLi(reg, 0);
                }
            }
            break;
        case TACOpType::TEMP:
        {
            // 检查临时变量是否已在寄存器中
            auto it = varInReg_.find(op.name);
            if (it != varInReg_.end() && it->second != reg)
            {
                emit("addi " + reg + ", " + it->second + ", 0");
                break;
            }
            else if (it != varInReg_.end() && it->second == reg)
            {
                break;
            }
            // 临时变量不在寄存器中：如果有栈帧，从栈加载
            if (currentFrameSize_ > 0)
            {
                int offset = allocVarOffset(op.name);
                emitStackLoad(reg, offset);
                varInReg_[op.name] = reg;
            }
            else
            {
                // 无栈帧且临时变量不在寄存器中，安全回退
                emitLi(reg, 0);
            }
            break;
        }
        case TACOpType::NONE:
            emitLi(reg, 0);
            break;
        }
        return reg;
    }

    void CodeGen::storeOperand(const TACOperand &op, const std::string &reg)
    {
        // 当向寄存器存储新值时，使该寄存器中旧变量的跟踪失效
        for (auto it = varInReg_.begin(); it != varInReg_.end(); ++it)
        {
            if (it->second == reg && it->first != op.name)
            {
                varInReg_.erase(it->first);
                break;
            }
        }

        // 检查变量是否在 PARAM 队列中（即将作为函数参数使用）
        // 如果是，跳过栈存储，直接更新寄存器跟踪
        bool isParam = false;
        if (op.type == TACOpType::VAR || op.type == TACOpType::TEMP)
        {
            for (auto &p : paramQueue_)
            {
                if (p == op.name)
                {
                    isParam = true;
                    break;
                }
            }
        }

        switch (op.type)
        {
        case TACOpType::VAR:
            if (isGlobal(op.name))
            {
                // 使用 lui + addi 加载全局变量地址
                emit("lui t2, %hi(_g_" + op.name + ")");
                emit("addi t2, t2, %lo(_g_" + op.name + ")");
                emit("sw " + reg + ", 0(t2)");
            }
            else if (currentFrameSize_ == 0 || isParam)
            {
                // 无栈帧 或 变量将作为函数参数：不存储到栈，仅更新寄存器跟踪
                varInReg_[op.name] = reg;
            }
            else
            {
                int offset = allocVarOffset(op.name);
                emitStackStore(reg, offset);
                // 更新寄存器跟踪：变量现在在 reg 中
                varInReg_[op.name] = reg;
            }
            break;
        case TACOpType::TEMP:
        {
            if (currentFrameSize_ == 0 || isParam)
            {
                // 无栈帧 或 变量将作为函数参数：不存储到栈，仅更新寄存器跟踪
                varInReg_[op.name] = reg;
            }
            else
            {
                int offset = allocVarOffset(op.name);
                emitStackStore(reg, offset);
                // 更新寄存器跟踪：临时变量现在在 reg 中
                varInReg_[op.name] = reg;
            }
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

    void CodeGen::emitLi(const std::string &reg, int32_t value)
    {
        // 使用 li 伪指令，汇编器会自动选择 addi 或 lui+addi
        emit("li " + reg + ", " + std::to_string(value));
    }

    void CodeGen::emitSeqz(const std::string &rd, const std::string &rs1)
    {
        emit("seqz " + rd + ", " + rs1);
    }

    void CodeGen::emitSnez(const std::string &rd, const std::string &rs1)
    {
        emit("snez " + rd + ", " + rs1);
    }

    void CodeGen::emitNeg(const std::string &rd, const std::string &rs1)
    {
        emit("neg " + rd + ", " + rs1);
    }

    // 软件乘法：t0 = t0 * t1（使用移位加法算法）
    // 临时寄存器: t2 (结果/符号), t3 (循环计数), t4 (符号), t5 (临时)
    void CodeGen::emitMulSW()
    {
        static int mulLabelCounter = 0;
        int id = mulLabelCounter++;
        std::string loopLabel = "_MUL_L" + std::to_string(id);
        std::string endLabel = "_MUL_E" + std::to_string(id);

        // 确定符号
        emit("addi t4, zero, 0");
        emit("bge t0, zero, " + loopLabel + "_NS");
        emit("addi t4, zero, 1");
        emit("sub t0, zero, t0");
        emit(loopLabel + "_NS:");
        emit("bge t1, zero, " + loopLabel + "_NS2");
        emit("xori t4, t4, 1");
        emit("sub t1, zero, t1");
        emit(loopLabel + "_NS2:");

        // 移位加法
        emit("addi t2, zero, 0");
        emit("addi t3, zero, 32");
        emit(loopLabel + ":");
        emit("beq t1, zero, " + endLabel);
        emit("andi t5, t1, 1");
        emit("beq t5, zero, " + loopLabel + "_SKIP");
        emit("add t2, t2, t0");
        emit(loopLabel + "_SKIP:");
        emit("slli t0, t0, 1");
        emit("srli t1, t1, 1");
        emit("addi t3, t3, -1");
        emit("bne t3, zero, " + loopLabel);
        emit(endLabel + ":");

        // 应用符号
        emit("beq t4, zero, " + endLabel + "_DN");
        emit("sub t2, zero, t2");
        emit(endLabel + "_DN:");
        emit("addi t0, t2, 0");
    }

    // 软件除法：t0 = t0 / t1（使用恢复除法算法）
    // 临时寄存器: t2 (余数), t3 (循环计数), t4 (符号), t5 (临时)
    void CodeGen::emitDivSW()
    {
        static int divLabelCounter = 0;
        int id = divLabelCounter++;
        std::string loopLabel = "_DIV_L" + std::to_string(id);
        std::string endLabel = "_DIV_E" + std::to_string(id);

        // 确定商的符号
        emit("addi t4, zero, 0");
        emit("bge t0, zero, " + loopLabel + "_NS");
        emit("addi t4, zero, 1");
        emit("sub t0, zero, t0");
        emit(loopLabel + "_NS:");
        emit("bge t1, zero, " + loopLabel + "_NS2");
        emit("xori t4, t4, 1");
        emit("sub t1, zero, t1");
        emit(loopLabel + "_NS2:");

        // 恢复除法
        emit("addi t2, zero, 0");
        emit("addi t3, zero, 32");
        emit(loopLabel + ":");
        emit("slli t2, t2, 1");
        emit("srli t5, t0, 31");
        emit("beq t5, zero, " + loopLabel + "_CLR");
        emit("addi t2, t2, 1");
        emit(loopLabel + "_CLR:");
        emit("slli t0, t0, 1");
        emit("blt t2, t1, " + loopLabel + "_NO");
        emit("sub t2, t2, t1");
        emit("addi t0, t0, 1");
        emit(loopLabel + "_NO:");
        emit("addi t3, t3, -1");
        emit("bne t3, zero, " + loopLabel);
        emit(endLabel + ":");

        // 应用符号
        emit("beq t4, zero, " + endLabel + "_DN");
        emit("sub t0, zero, t0");
        emit(endLabel + "_DN:");
    }

    // 软件取模：t0 = t0 % t1（使用恢复除法算法，保留余数）
    void CodeGen::emitRemSW()
    {
        static int remLabelCounter = 0;
        int id = remLabelCounter++;
        std::string loopLabel = "_REM_L" + std::to_string(id);
        std::string endLabel = "_REM_E" + std::to_string(id);

        // 确定余数的符号（与被除数相同）
        emit("addi t4, zero, 0");
        emit("bge t0, zero, " + loopLabel + "_NS");
        emit("addi t4, zero, 1");
        emit("sub t0, zero, t0");
        emit(loopLabel + "_NS:");
        emit("bge t1, zero, " + loopLabel + "_NS2");
        emit("sub t1, zero, t1");
        emit(loopLabel + "_NS2:");

        // 恢复除法（只保留余数）
        emit("addi t2, zero, 0");
        emit("addi t3, zero, 32");
        emit(loopLabel + ":");
        emit("slli t2, t2, 1");
        emit("srli t5, t0, 31");
        emit("beq t5, zero, " + loopLabel + "_CLR");
        emit("addi t2, t2, 1");
        emit(loopLabel + "_CLR:");
        emit("slli t0, t0, 1");
        emit("blt t2, t1, " + loopLabel + "_NO");
        emit("sub t2, t2, t1");
        emit(loopLabel + "_NO:");
        emit("addi t3, t3, -1");
        emit("bne t3, zero, " + loopLabel);
        emit(endLabel + ":");

        // 应用符号
        emit("beq t4, zero, " + endLabel + "_DN");
        emit("sub t2, zero, t2");
        emit(endLabel + "_DN:");
        emit("addi t0, t2, 0");
    }

    void CodeGen::emitPrologue()
    {
        emit(".text");
        emit(".align 2");
        emit(".globl _start");
        emit("_start:");
        emit("addi sp, sp, -256");
        emit("sw ra, 252(sp)");
    }

    void CodeGen::emitFuncPrologue(const std::string &funcName, int frameSize)
    {
        if (frameSize < 16)
            frameSize = 16;
        currentFrameSize_ = frameSize;
        currentFuncIsLeaf_ = funcIsLeaf_.count(funcName) && funcIsLeaf_[funcName];

        emit("");
        if (funcName == "main")
        {
            emit(".globl main");
            emit("main:");
        }
        else
        {
            emit("func_" + funcName + ":");
        }

        // 始终分配栈帧，使用 li + sub 模式
        emitLi("t2", frameSize);
        emit("sub sp, sp, t2");

        // 始终保存 ra
        emit("sw ra, " + std::to_string(frameSize - 4) + "(sp)");

        varOffsets_.clear();
    }

    void CodeGen::emitFuncEpilogue()
    {
        int frameSize = currentFrameSize_;
        if (frameSize < 0)
            frameSize = 0;

        if (frameSize == 0)
        {
            emit("ret");
            return;
        }

        // 恢复 ra 并返回
        emit("lw ra, " + std::to_string(frameSize - 4) + "(sp)");
        emitLi("t2", frameSize);
        emit("add sp, sp, t2");
        emit("ret");
    }

    void CodeGen::emitExit()
    {
        emitLi("a7", 93);
        emit("ecall");
    }

    bool CodeGen::isGlobal(const std::string &name) const
    {
        return globalVars_.count(name) > 0;
    }

} // namespace MyCompiler
