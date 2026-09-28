/*
 * lab2 start.c —— M 态启动段: 完成 M→S 切换的全部"前置义务"后 mret。
 *
 * 在 lab1 基础上新增:
 *   - 使能 S 态外部/时钟中断(sie 的 SEIE/STIE);
 *   - 使能 Sstc(stimecmp), 请求第一个时钟中断。
 * 页表在 main() 里由 kvminit()/kvminithart() 建立并打开(见 vm.c)。
 */
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"

void main();   /* main.c 中定义, mret 的目标 */
void timerinit();

/* 启动内核栈: entry.S 把 sp 设到 &bootstack[LAB1_STACK_KB*1024]。 */
__attribute__((aligned(16))) char bootstack[LAB1_STACK_KB * 1024];

void
start()
{
  // (1) mstatus.MPP = S: mret 降权到 S 态; 清 MIE 防 mret 后被中断打断。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  x &= ~(1L << 3);          // 清 MIE(bit3)
  w_mstatus(x);

  // (2) mepc = main。
  w_mepc((uint64)main);

  // (3) 启动早期先 Bare(satp=0), main 里 kvminithart 再开 SV39。
  w_satp(0);

  // (4) 异常/中断委托给 S 态。
  w_medeleg(0xffff);
  w_mideleg(0xffff);

  // (5) S 态使能外部中断(SEIE)与时钟中断(STIE)。
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  // (6) PMP 授权(全物理空间 R/W/X)。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // (7) 请求时钟中断(使能 Sstc 并设置 stimecmp)。
  timerinit();

  // (8) tp 记 hartid。
  w_tp((uint64)r_mhartid());

  // (9) 降权进入 S 态。
  asm volatile("mret");
}

// 让每个 hart 产生时钟中断。
void
timerinit()
{
  // 使能 Sstc 扩展(即 stimecmp 寄存器)。
  w_menvcfg(r_menvcfg() | MENVCFG_STCE);

  // 允许 S 态读取 time 与使用 stimecmp。
  w_mcounteren(r_mcounteren() | 2);

  // 请求第一个时钟中断(约 0.1 秒后)。
  w_stimecmp(r_time() + 1000000);
}
