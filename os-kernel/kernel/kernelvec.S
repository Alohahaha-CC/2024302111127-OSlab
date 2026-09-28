# lab2 kernelvec.S —— S 态陷入时(内核代码被中断/异常打断)的入口。
#
# 内核态发生中断时 stvec 指向这里(usertrap() 进入内核后把 stvec 切到
# kernelvec, 见 trap.c)。当前已在某内核栈上: 压 caller-saved 寄存器,
# 调 kerneltrap(), 恢复, sret 返回被打断的内核代码。

.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # 开 256 字节栈空间存寄存器。
        addi sp, sp, -256

        sd ra, 0(sp)
        sd gp, 16(sp)
        sd t0, 32(sp)
        sd t1, 40(sp)
        sd t2, 48(sp)
        sd a0, 72(sp)
        sd a1, 80(sp)
        sd a2, 88(sp)
        sd a3, 96(sp)
        sd a4, 104(sp)
        sd a5, 112(sp)
        sd a6, 120(sp)
        sd a7, 128(sp)
        sd t3, 216(sp)
        sd t4, 224(sp)
        sd t5, 232(sp)
        sd t6, 240(sp)

        call kerneltrap

        ld ra, 0(sp)
        ld gp, 16(sp)
        ld t0, 32(sp)
        ld t1, 40(sp)
        ld t2, 48(sp)
        ld a0, 72(sp)
        ld a1, 80(sp)
        ld a2, 88(sp)
        ld a3, 96(sp)
        ld a4, 104(sp)
        ld a5, 112(sp)
        ld a6, 120(sp)
        ld a7, 128(sp)
        ld t3, 216(sp)
        ld t4, 224(sp)
        ld t5, 232(sp)
        ld t6, 240(sp)

        addi sp, sp, 256

        sret
