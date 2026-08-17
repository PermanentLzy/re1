#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RISC-V 汇编验证器
检查生成的汇编代码是否符合标准 RISC-V 指令集
"""

import re
import sys
from typing import List, Tuple, Set

# 标准 RISC-V RV32I 指令
RV32I_INSTRUCTIONS = {
    # U-type
    'lui', 'auipc',
    # J-type
    'jal',
    # I-type (jump)
    'jalr',
    # B-type (branches)
    'beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu',
    # I-type (loads)
    'lb', 'lh', 'lw', 'lbu', 'lhu',
    # S-type (stores)
    'sb', 'sh', 'sw',
    # I-type (ALU immediate)
    'addi', 'slti', 'sltiu', 'xori', 'ori', 'andi',
    'slli', 'srli', 'srai',
    # R-type (ALU register)
    'add', 'sub', 'sll', 'slt', 'sltu', 'xor', 'srl', 'sra', 'or', 'and',
    # System
    'fence', 'ecall', 'ebreak',
}

# M 扩展 (乘除法)
M_EXTENSIONS = {'mul', 'mulh', 'mulhsu', 'mulhu', 'div', 'divu', 'rem', 'remu'}

# ABI 寄存器名
ABI_REGISTERS = {
    'zero', 'ra', 'sp', 'gp', 'tp', 't0', 't1', 't2',
    's0', 's1', 'a0', 'a1', 'a2', 'a3', 'a4', 'a5',
    'a6', 'a7', 's2', 's3', 's4', 's5', 's6', 's7',
    's8', 's9', 's10', 's11', 't3', 't4', 't5', 't6',
}

# ISA 寄存器名
ISA_REGISTERS = {f'x{i}' for i in range(32)}

ALL_REGISTERS = ABI_REGISTERS | ISA_REGISTERS


def validate_assembly(asm_code: str) -> Tuple[List[str], List[str]]:
    """
    验证 RISC-V 汇编代码
    
    Returns:
        (错误列表, 警告列表)
    """
    errors = []
    warnings = []
    
    lines = asm_code.strip().split('\n')
    in_text = True
    has_m_ext = False  # 是否使用了 M 扩展
    
    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        
        # 跳过空行和注释
        if not line or line.startswith('#') or line.startswith(';'):
            continue
        
        # 检查段切换
        if line == '.text' or line == '.section .text':
            in_text = True
            continue
        if line == '.data' or line == '.section .data':
            in_text = False
            continue
        
        # 检查伪指令 (常见的应该支持)
        pseudo_match = re.match(r'^(li|la|mv|neg|seqz|snez|j|ret|call|beqz|bnez|bgez|bltz|blez|bgtz|ble|bge)\s', line)
        if pseudo_match:
            # 常见伪指令，应该没问题
            continue
        
        # 检查标签
        if re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*:', line):
            continue
        
        # 检查指令
        instr_match = re.match(r'^(\w+)\s+(.*)', line)
        if not instr_match:
            # 可能是伪指令或指令
            if line.startswith('.'):
                # 汇编器指令
                if not any(line.startswith(d) for d in ['.globl', '.global', '.align', '.word', '.byte', '.half', '.string', '.asciz', '.ascii']):
                    warnings.append(f"Line {line_num}: 未知的汇编指令 '{line}'")
            else:
                errors.append(f"Line {line_num}: 无法识别的行 '{line}'")
            continue
        
        opcode = instr_match.group(1).lower()
        operands = instr_match.group(2)
        
        # 检查是否为合法指令
        all_valid = RV32I_INSTRUCTIONS | M_EXTENSIONS
        if opcode not in all_valid:
            errors.append(f"Line {line_num}: 未知指令 '{opcode}'")
            continue
        
        if opcode in M_EXTENSIONS:
            has_m_ext = True
        
        # 验证操作数
        try:
            validate_operands(opcode, operands, line_num, errors, warnings)
        except Exception as e:
            errors.append(f"Line {line_num}: 操作数验证失败 - {str(e)}")
    
    if has_m_ext:
        warnings.append("使用了 M 扩展指令 (乘除法)，某些汇编器可能不支持")
    
    return errors, warnings


def validate_operands(opcode: str, operands: str, line_num: int, 
                      errors: List[str], warnings: List[str]):
    """验证指令操作数"""
    
    # 解析操作数 (考虑括号格式如 0(sp))
    operand_list = parse_operands(operands)
    
    if opcode in ('lui', 'auipc'):
        # U-type: rd, imm
        check_operand_count(opcode, operand_list, 2, line_num, errors)
        if len(operand_list) >= 2:
            check_register(operand_list[0], line_num, errors)
            check_immediate(operand_list[1], line_num, errors, max_val=0xFFFFF)
    
    elif opcode in ('jal',):
        # J-type: rd, label
        check_operand_count(opcode, operand_list, 2, line_num, errors)
        if len(operand_list) >= 2:
            check_register(operand_list[0], line_num, errors)
            # 第二个操作数是标签，不需要验证
    
    elif opcode in ('jalr',):
        # I-type (jump): rd, rs1, imm 或 rd, imm(rs1)
        if len(operand_list) == 2 and '(' in operand_list[1]:
            # rd, offset(rs1) 格式
            check_register(operand_list[0], line_num, errors)
            offset, reg = parse_mem_operand(operand_list[1])
            if reg:
                check_register(reg, line_num, errors)
            if offset is not None:
                check_immediate(str(offset), line_num, errors, min_val=-2048, max_val=2047)
        elif len(operand_list) == 3:
            # rd, rs1, imm 格式
            check_register(operand_list[0], line_num, errors)
            check_register(operand_list[1], line_num, errors)
            check_immediate(operand_list[2], line_num, errors, min_val=-2048, max_val=2047)
        else:
            errors.append(f"Line {line_num}: {opcode} 需要 2 或 3 个操作数")
    
    elif opcode in ('beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu'):
        # B-type: rs1, rs2, label
        check_operand_count(opcode, operand_list, 3, line_num, errors)
        if len(operand_list) >= 2:
            check_register(operand_list[0], line_num, errors)
            check_register(operand_list[1], line_num, errors)
    
    elif opcode in ('lb', 'lh', 'lw', 'lbu', 'lhu'):
        # I-type (load): rd, offset(rs1)
        check_operand_count(opcode, operand_list, 2, line_num, errors)
        if len(operand_list) >= 2:
            check_register(operand_list[0], line_num, errors)
            offset, reg = parse_mem_operand(operand_list[1])
            if reg:
                check_register(reg, line_num, errors)
            if offset is not None:
                check_immediate(str(offset), line_num, errors, min_val=0, max_val=4095)
    
    elif opcode in ('sb', 'sh', 'sw'):
        # S-type: rs2, offset(rs1)
        check_operand_count(opcode, operand_list, 2, line_num, errors)
        if len(operand_list) >= 2:
            check_register(operand_list[0], line_num, errors)
            offset, reg = parse_mem_operand(operand_list[1])
            if reg:
                check_register(reg, line_num, errors)
            if offset is not None:
                check_immediate(str(offset), line_num, errors, min_val=0, max_val=4095)
    
    elif opcode in ('addi', 'slti', 'sltiu', 'xori', 'ori', 'andi'):
        # I-type (ALU): rd, rs1, imm
        check_operand_count(opcode, operand_list, 3, line_num, errors)
        if len(operand_list) >= 3:
            check_register(operand_list[0], line_num, errors)
            check_register(operand_list[1], line_num, errors)
            check_immediate(operand_list[2], line_num, errors, min_val=-2048, max_val=2047)
    
    elif opcode in ('slli', 'srli', 'srai'):
        # I-type (shift): rd, rs1, shamt
        check_operand_count(opcode, operand_list, 3, line_num, errors)
        if len(operand_list) >= 3:
            check_register(operand_list[0], line_num, errors)
            check_register(operand_list[1], line_num, errors)
            check_immediate(operand_list[2], line_num, errors, min_val=0, max_val=31)
    
    elif opcode in ('add', 'sub', 'sll', 'slt', 'sltu', 'xor', 'srl', 'sra', 'or', 'and'):
        # R-type: rd, rs1, rs2
        check_operand_count(opcode, operand_list, 3, line_num, errors)
        if len(operand_list) >= 3:
            check_register(operand_list[0], line_num, errors)
            check_register(operand_list[1], line_num, errors)
            check_register(operand_list[2], line_num, errors)
    
    elif opcode in ('mul', 'mulh', 'mulhsu', 'mulhu', 'div', 'divu', 'rem', 'remu'):
        # M extension: rd, rs1, rs2
        check_operand_count(opcode, operand_list, 3, line_num, errors)
        if len(operand_list) >= 3:
            check_register(operand_list[0], line_num, errors)
            check_register(operand_list[1], line_num, errors)
            check_register(operand_list[2], line_num, errors)
    
    elif opcode in ('fence', 'ecall', 'ebreak'):
        # System: 无或少量操作数
        pass
    
    else:
        warnings.append(f"Line {line_num}: 未验证的指令 '{opcode}'")


def parse_operands(operands: str) -> List[str]:
    """解析操作数列表，考虑括号内的内存操作数"""
    result = []
    current = ''
    in_parens = False
    
    for ch in operands:
        if ch == '(':
            in_parens = True
            current += ch
        elif ch == ')':
            in_parens = False
            current += ch
        elif ch == ',' and not in_parens:
            result.append(current.strip())
            current = ''
        else:
            current += ch
    
    if current.strip():
        result.append(current.strip())
    
    return result


def parse_mem_operand(operand: str) -> Tuple:
    """解析内存操作数 offset(reg)"""
    match = re.match(r'(-?\d+)\((\w+)\)', operand)
    if match:
        return int(match.group(1)), match.group(2)
    # 尝试无偏移的情况
    match = re.match(r'\((\w+)\)', operand)
    if match:
        return 0, match.group(1)
    return None, None


def check_register(reg: str, line_num: int, errors: List[str]):
    """检查寄存器名是否合法"""
    reg = reg.strip()
    if reg not in ALL_REGISTERS:
        errors.append(f"Line {line_num}: 未知寄存器 '{reg}'")


def check_immediate(imm_str: str, line_num: int, errors: List[str],
                    min_val: int = None, max_val: int = None):
    """检查立即数是否合法"""
    try:
        # 处理十六进制
        if imm_str.startswith('0x') or imm_str.startswith('0X'):
            value = int(imm_str, 16)
        elif imm_str.startswith('%hi(') or imm_str.startswith('%lo('):
            # 重定位表达式，跳过验证
            return
        else:
            value = int(imm_str)
        
        if min_val is not None and value < min_val:
            errors.append(f"Line {line_num}: 立即数 {value} 太小 (最小 {min_val})")
        if max_val is not None and value > max_val:
            errors.append(f"Line {line_num}: 立即数 {value} 太大 (最大 {max_val})")
    except ValueError:
        # 可能是标签或重定位表达式
        if not re.match(r'^[a-zA-Z_%]', imm_str):
            warnings.append(f"Line {line_num}: 无法解析立即数 '{imm_str}'")


def check_operand_count(opcode: str, actual: int, expected: int, 
                        line_num: int, errors: List[str]):
    """检查操作数数量"""
    if actual != expected:
        errors.append(f"Line {line_num}: {opcode} 需要 {expected} 个操作数，实际 {actual} 个")


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("用法: python riscv_validator.py <asm_file>")
        sys.exit(1)
    
    with open(sys.argv[1], 'r', encoding='utf-8') as f:
        asm_code = f.read()
    
    errors, warnings = validate_assembly(asm_code)
    
    if errors:
        print("❌ 发现错误:")
        for e in errors:
            print(f"  - {e}")
    else:
        print("✓ 没有发现错误")
    
    if warnings:
        print("\n⚠️  警告:")
        for w in warnings:
            print(f"  - {w}")
    
    if not errors and not warnings:
        print("✓ 汇编代码有效")
