/*
 * lab1 start.c —— M 态启动段: 完成 M→S 切换的全部"前置义务"后 mret。
 *
 * 设计决策对应《设计笔记》思考题 1/3/4:
 *   - 上电后 PC=0x1000 是 QEMU 的 boot ROM(-bios none 则跳过 ROM 直接 -kernel);
 *   - 内核链接/加载到 0x80000000(virt 机器 RAM 起点);
 *   - satp=0 选择 Bare 模式(见设计笔记 §4);
 *   - PMP 授权是 mret 降入 S 态取指的必要条件(见设计笔记 §3)。
 */
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"

void main();   /* main.c 中定义, mret 的目标 */

/* 启动内核栈: entry.S 把 sp 设到 &bootstack[LAB1_STACK_KB*1024]。
 * 大小来自 course_sid.h 的 LAB1_STACK_KB, 与 entry.S 强一致。 */
__attribute__((aligned(16))) char bootstack[LAB1_STACK_KB * 1024];

void
start()
{
  // (1) mstatus.MPP = S: 让 mret 返回时降权到 S 态。
  //     并兜底清 MIE(entry.S 已清), 确保 mret 后不会被中断打断。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;   // 清 MPP
  x |= MSTATUS_MPP_S;       // MPP = Supervisor
  x &= ~(1L << 3);          // 兜底: 清 MIE(bit3)
  w_mstatus(x);

  // (2) mepc = main: mret 的返回地址(S 态第一条指令)。
  //     -mcmodel=medany 使 main 的地址可用 auipc 直接表示, 可安全写入 mepc。
  w_mepc((uint64)main);

  // (3) 关闭地址翻译: satp=0(Bare 模式)。lab1 尚无虚拟内存, 用物理地址直接
  //     取指/访存; 不引入恒等映射页表的额外复杂度。
  w_satp(0);

  // (4) 中断/异常委托: 把软件能处理的陷入交由 S 态。lab1 期间不产生任何异常
  //     (-d int 零异常), 故无需在此设置 stvec。
  w_medeleg(0xffff);
  w_mideleg(0xffff);

  // (5) PMP 授权(环境前置条件, 必做): 让 S/U 态访存落在授权物理区域。
  //     pmpaddr0 = 0x3fffffffffffff(NAPOT 覆盖全部物理地址空间);
  //     pmpcfg0  = 0xf → A=NAPOT, R=W=X=1, L=0(不锁, 允许 M 态改写)。
  //     若缺失: mret 降入 S 态的第一条取指即触发 instruction access fault。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // (6) 把 hartid 记入 tp, 为后续多核代码(cpuid)留钩子。
  w_tp((uint64)r_mhartid());

  // (7) 降权进入 S 态并跳转 main: 此后运行在 S 态、内核栈、Bare 地址。
  asm volatile("mret");
}
