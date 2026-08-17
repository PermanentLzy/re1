.text

func_add:
addi sp, sp, -16
sw a0, 0(sp)
sw a1, 4(sp)
lw t0, 0(sp)
lw t1, 4(sp)
add t0, t0, t1
sw t0, 8(sp)
addi a0, t0, 0
addi sp, sp, 16
jalr zero, 0(x1)

func_get42:
addi sp, sp, -16
addi t0, zero, 42
sw t0, 0(sp)
addi a0, t0, 0
addi sp, sp, 16
jalr zero, 0(x1)

.globl main
main:
addi sp, sp, -256
sw ra, 252(sp)
jal x1, func_get42
sw a0, 0(sp)
addi t0, a0, 0
sw t0, 4(sp)
addi t0, zero, 1
sw t0, 8(sp)
addi t0, zero, 2
sw t0, 12(sp)
lw a0, 8(sp)
lw a1, 12(sp)
jal x1, func_add
sw a0, 16(sp)
addi t0, a0, 0
sw t0, 20(sp)
lw a0, 4(sp)
lw a1, 20(sp)
jal x1, func_add
sw a0, 24(sp)
addi t0, a0, 0
sw t0, 28(sp)
addi a0, t0, 0
lw ra, 252(sp)
addi sp, sp, 256
jalr zero, 0(x1)
