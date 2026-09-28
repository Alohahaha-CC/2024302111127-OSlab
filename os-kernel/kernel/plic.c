// lab2 plic.c —— 平台级中断控制器(RISC-V PLIC)。
//
// 中断使能链路: UART 芯片 IER → PLIC 优先级/使能 → CPU sie(SEIE)
//              → sstatus(SIE)。这里负责 PLIC 一段。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void
plicinit(void)
{
  // 设置各设备中断优先级非零(否则禁用)。
  *(uint32 *)(PLIC + UART0_IRQ * 4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();

  // 为本 hart 的 S 态使能 UART 中断。
  *(uint32 *)PLIC_SENABLE(hart) = (1 << UART0_IRQ);

  // S 态优先级阈值设 0(全部接受)。
  *(uint32 *)PLIC_SPRIORITY(hart) = 0;
}

// 问 PLIC 要服务哪个中断。
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32 *)PLIC_SCLAIM(hart);
  return irq;
}

// 告知 PLIC 已处理完该 IRQ。
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32 *)PLIC_SCLAIM(hart) = irq;
}
