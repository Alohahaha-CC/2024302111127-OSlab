// lab2 trap.c —— 陷入分发: 用户态 ecall / 设备中断 / 时钟中断。
//
// 调用链: 用户态 ecall → trampoline.S uservec → usertrap() → usertrapret()
//         → trampoline.S userret → sret → 回用户态。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];
void kernelvec(void);

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// 设内核态陷入向量。
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// 用户态陷入处理入口(由 trampoline.S 调用)。
// 返回用户 satp 供 trampoline.S 的 userret 切换。
uint64
usertrap(void)
{
  int which_dev = 0;

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 进入内核后, 把 stvec 指向内核向量(此后内核态的中断走 kerneltrap)。
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();

  // 保存用户 PC。
  p->trapframe->epc = r_sepc();

  if (r_scause() == 8) {
    // 系统调用: sepc 指向 ecall 指令, 前移 4 跳过它, 否则原地打转。
    p->trapframe->epc += 4;

    // 现场已存好, 打开中断让 UART/时钟能打断。
    intr_on();

    syscall();
  } else if ((which_dev = devintr()) != 0) {
    // 设备/时钟中断, 已在 devintr 内处理。
  } else {
    // 其它异常(非法指令/越界): 打印关键寄存器后停机。
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(),
           p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if (killed(p))
    kexit(-1);

  // lab2 "假调度": 时钟中断不抢 CPU, 直接返回原进程(lab4 才 yield)。
  (void)which_dev;

  usertrapret();

  // 返回用户页表 satp 给 trampoline.S。
  return MAKE_SATP(p->pagetable);
}

// 送程序回用户态前的环境准备。
void
usertrapret(void)
{
  struct proc *p = myproc();

  // 马上要把 stvec 从 kernelvec 切到 uservec, 若此刻内核态再陷入会酿祸,
  // 所以先关中断。
  intr_off();

  // 下次用户态陷入时走 uservec。
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // 填 trapframe 头部字段, 供下次 uservec 读取。
  p->trapframe->kernel_satp = r_satp();         // 内核页表
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 本进程内核栈顶
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();

  // 设置 sstatus: SPP=U(下次 sret 降回 U 态), SPIE=1(恢复用户态中断使能)。
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);

  // 目标 PC = 保存的用户 PC。
  w_sepc(p->trapframe->epc);
}

// 内核态陷入处理(由 kernelvec.S 调用)。
void
kerneltrap(void)
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0) {
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(),
           r_stval());
    panic("kerneltrap");
  }

  // lab2 假调度: 时钟中断不 yield。
  (void)which_dev;

  w_sepc(sepc);
  w_sstatus(sstatus);
}

// 时钟中断: 递增 ticks, 安排下一次。
void
clockintr(void)
{
  if (cpuid() == 0) {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // 约 0.1 秒后再来一次, 同时清除本次中断请求。
  w_stimecmp(r_time() + 1000000);
}

// 判中断源: 返回 2=时钟, 1=其它设备, 0=不识别。
int
devintr(void)
{
  uint64 scause = r_scause();

  if (scause == 0x8000000000000009L) {
    // S 态外部中断(经 PLIC)。
    int irq = plic_claim();

    if (irq == UART0_IRQ) {
      uartintr();
    } else if (irq) {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    if (irq)
      plic_complete(irq);

    return 1;
  } else if (scause == 0x8000000000000005L) {
    // 时钟中断。
    clockintr();
    return 2;
  } else {
    return 0;
  }
}
