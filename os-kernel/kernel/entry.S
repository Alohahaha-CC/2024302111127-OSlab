# lab1 entry.S —— 上电后第一条指令(内核被 QEMU 加载到 0x80000000,
# 每个 hart 从 _entry 开始执行, 此刻运行在 M 态)。
#
# 职责(按顺序):
#   1) 关中断 —— 防止在 mtvec/栈就绪前被意外中断打断;
#   2) 为 hart0 准备内核栈(sp 指向栈顶);
#   3) 从核(mhartid != 0)进入 park 自旋;
#   4) 设置机器态陷阱向量 mtvec;
#   5) 跳入 C 的 start()(start.c), 由它完成 M→S 切换后 mret 到 main。

#include "course_sid.h"   /* 提供 LAB1_STACK_KB 等个性化参数 */

.section .text
.global _entry
_entry:
        # (1) 关闭机器级中断: 清 mstatus.MIE(bit3)。
        #     此刻 mtvec 尚未指向合法处理程序, 若 MIE 置位, 任何意外中断都会
        #     跳进 mtvec 默认值(0 地址)导致跑飞。因此必须先关中断再动栈/mtvec。
        csrci   mstatus, 8

        # (2) 准备内核栈: 栈在 start.c 的 bootstack 数组(LAB1_STACK_KB KB),
        #     RISC-V 栈向下生长, 所以 sp 指向栈顶(bootstack + LAB1_STACK_KB*1024)。
        la      sp, bootstack
        li      t0, LAB1_STACK_KB * 1024
        add     sp, sp, t0

        # (3) 从核(mhartid != 0)进入 park 自旋等待。
        #     lab1 只初始化 hart0 单核环境; 从核无独立上下文/控制台, 若也跑进
        #     C 会与 hart0 争抢共享状态, 故用 wfi 驻留。
        csrr    t0, mhartid
        bnez    t0, park

        # (4) 设置机器态陷阱向量基址 mtvec。
        #     RISC-V 规定 mtvec 低 2 位是模式标志(Direct=00), 故地址必须 4 字节
        #     对齐; 若未对齐, 硬件会静默丢弃整次写入(后续异常跳 0)。
        la      t0, mtrap
        csrw    mtvec, t0

        # (5) 进入 start()(仍运行在 M 态), 由它完成 M→S 切换。
        call    start

# 从核永久驻留点(低功耗等待中断)
park:
        wfi
        j       park

# 机器态陷阱兜底: lab1 理论上不产生任何异常; 一旦触发即自旋, 便于用
# QEMU -d int 定位(日志首行 cause 即故障原因)。.balign 4 满足 mtvec 对齐。
.balign 4
mtrap:
        wfi
        j       mtrap
