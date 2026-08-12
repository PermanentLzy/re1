# 性能优化方案总览

## 概述
根据9个编译阶段的性能评分分析，性能瓶颈主要在**优化器（Pass 7）**和**代码生成（Pass 8）**阶段。本方案通过添加高效的优化 Pass 和改进代码生成器来显著提升性能。

---

## 性能瓶颈分析

### 当前问题
1. **优化不足**：仅有常量折叠、CSE、DCE 等基础优化
2. **没有利用循环结构**：循环内的不变式未被提取
3. **没有强度削弱**：乘法/除法操作未优化为更便宜的操作
4. **代码生成不够智能**：未使用 RISC-V immediate 形式指令，产生大量冗余指令
5. **寄存器分配幼稚**：所有变量都在栈上，没有充分利用寄存器

---

## 优化方案

### 方案 1：循环不变式提取（LICM）
**文件**：`src/optimizer/LoopInvariantCodeMotion.cpp`

**原理**：
```
while (i < 10) {
    x = a + b;    // 这是循环不变式
    i = i + 1;
}
```

变为：
```
x = a + b;      // 提到循环外
while (i < 10) {
    i = i + 1;
}
```

**收益**：
- 减少循环内指令数
- 降低执行时间（尤其对大循环）
- 为其他优化创造机会

---

### 方案 2：强度削弱（Strength Reduction）
**文件**：`src/optimizer/StrengthReduction.cpp`

**优化规则**：
```
x = y * 4   →  x = y << 2     (乘法 → 位移，快 3-5 倍)
x = y * 3   →  t = y << 1; x = y + t  (乘以 3 → 加法+位移)
x = y / 4   →  x = y >> 2     (除法 → 位移，快 4-6 倍)
```

**收益**：
- 乘/除运算是 CPU 中最慢的操作（10-40 个周期）
- 位移操作只需 1 个周期
- 大幅减少执行时间

---

### 方案 3：窥孔优化（Peephole Optimization）
**文件**：`src/optimizer/PeepholeOptimization.cpp`

**优化规则**：
```
x = y + 0   →  x = y         (加零消除)
x = y - 0   →  x = y         (减零消除)
x = y * 1   →  x = y         (乘一消除)
x = y * 0   →  x = 0         (乘零变常数)
x = y / 1   →  x = y         (除一消除)
t = a + b; x = t;  →  x = a + b  (冗余复制消除)
```

**收益**：
- 清除无用指令
- 减少数据依赖链长度
- 为指令调度创造机会

---

### 方案 4：优化的 RISC-V 代码生成
**文件**：`src/codegen/CodeGenOptimized.cpp`

**关键优化**：

#### 4.1 使用 Immediate 形式指令
```
原来：  li t0, 100
      add t0, t0, 50

优化：  addi t0, t0, 50    (单条指令，更快)
```

#### 4.2 常数加载优化
```
原来：  li t0, 5
      ... (10条指令)
      li t1, 5            (重复加载同一常数)

优化：  缓存 5 在某个寄存器中，直接复用
```

#### 4.3 乘除优化
```
原来：  li t1, 4
      mul t0, t0, t1      (昂贵的乘法)

优化：  slli t0, t0, 2    (位移，快3倍)
```

**收益**：
- 每个 BINARY 指令减少 1-2 条汇编指令
- 减少内存访问和总体执行时间
- 改进 CPU 流水线效率

---

## 集成步骤

### 第1步：编译新的优化器模块
```bash
cd MyCompiler/src/optimizer
# 新文件会自动通过 CMakeLists.txt 的 GLOB_RECURSE 编译
```

### 第2步：在 Optimizer.cpp 中集成新 Pass
已在 `optimize()` 函数中添加：
```cpp
void Optimizer::optimize(TACProgram& program) {
    // 一次性优化
    tailRecursionElimination(program);      
    strengthReduction(program);             // 新增
    loopInvariantCodeMotion(program);       // 新增

    // 迭代优化
    while (changed && iter < MAX_ITER) {
        // ...
        peepholeOptimization(program);      // 新增
        // ...
    }
}
```

### 第3步：更新 main.cpp 使用优化代码生成（可选）
```cpp
// 选择使用优化版本
#ifdef USE_OPTIMIZED_CODEGEN
    CodeGenOptimized codegen;
    codegen.generate(*irProgram);
#else
    CodeGen codegen;
    codegen.generate(*irProgram);
#endif
```

---

## 预期性能提升

| 优化方案 | 适用场景 | 预期提升 |
|---------|---------|---------|
| LICM | 含循环的程序 | 20-40% |
| 强度削弱 | 含乘除法的程序 | 15-30% |
| 窥孔优化 | 所有程序 | 5-15% |
| Immediate指令 | 所有程序 | 10-25% |
| **综合** | **复杂程序** | **40-80%** |

---

## 测试建议

### 1. 功能测试
```bash
cd build
ctest --output-on-failure
```

### 2. 性能对比测试
```bash
# 原始版本
time ./mycompiler ../examples/test20_complex_expr.src > out1.s

# 优化版本
time ./mycompiler -opt ../examples/test20_complex_expr.src > out2.s

# 比较生成的汇编大小和执行时间
```

### 3. 逐个启用优化观察效果
修改 `Optimizer.cpp` 的 `optimize()` 函数，注释掉各个 pass，观察指标变化：
```cpp
// strengthReduction(program);          // 临时关闭
// loopInvariantCodeMotion(program);    // 临时关闭
peepholeOptimization(program);
```

---

## 进一步优化方向

### 短期（可立即实现）
1. **寄存器分配优化**：使用图着色算法分配寄存器而非全部用栈
2. **函数内联**：对小函数做编译期内联
3. **指令调度**：重排指令顺序以最小化数据依赖

### 中期（需要深度改动）
1. **SSA 形式**：将 IR 转为 SSA 形式，启用更多优化
2. **向量化**：识别可被向量化的循环
3. **分支预测优化**：重排代码以改进分支预测命中率

### 长期（体系结构优化）
1. **后端定制**：针对特定 CPU（如 RISC-V RV32I、RV32IM 等）定制生成
2. **模式匹配**：识别特殊计算模式并用单一强大指令替换
3. **缓存优化**：数据局部性分析和循环转换

---

## 调试支持

### 环境变量
```bash
# 打印优化过程
DEBUG_OPTIMIZER=1 ./mycompiler code.src

# 打印生成的 IR
DEBUG_IR=1 ./mycompiler code.src

# 对比优化前后的 IR
VERBOSE_IR=1 ./mycompiler code.src
```

### 详细日志
所有优化 pass 都会输出 stderr 调试信息：
```
[StrengthRed] 乘以 4 → 左移 2 位
[Peephole] Pass 1: 优化 3 处
[LICM] 完成，找到 2 个循环
```

---

## 参考文献

1. **编译原理经典优化**：
   - Engineering a Compiler (Keith D. Cooper)
   - Compilers: Principles, Techniques, and Tools (Aho et al.)

2. **RISC-V 指令集优化**：
   - RISC-V Unprivileged ISA Spec
   - Howto Optimize RISC-V Code

3. **性能分析工具**：
   - `perf` - Linux 性能分析
   - `cachegrind` - 缓存分析
   - `massif` - 内存分析
