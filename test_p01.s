.text
.align 2

.globl main
main:
addi sp, sp, -256
sw ra, 252(sp)
addi t0, x0, 7
sw t0, 0(sp)
addi a0, t0, 0
lw ra, 252(sp)
addi sp, sp, 256
jalr x0, 0(x1)
