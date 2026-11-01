// lab2 sysproc.c —— 进程类系统调用: fork/exit/wait/getpid。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // 不可达
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

// lab3: SYS_sbrk —— 地址空间动态扩展(EAGER)。
// n>0 扩展堆(超出配额 uvmalloc 返回 0, 已建映射回滚, 返回 -1);
// n<0 收缩; n=0 返回当前堆边界。返回值统一为**旧**边界(成功)或 -1(失败)。
uint64
sys_sbrk(void)
{
  int n;
  uint64 oldsz;
  struct proc *p = myproc();

  argint(0, &n);
  oldsz = p->sz;

  if (n > 0) {
    if (uvmalloc(p->pagetable, oldsz, oldsz + n, PTE_W) == 0)
      return -1; // OOM: uvmalloc 已回滚, sz 不变
    p->sz = oldsz + n;
  } else if (n < 0) {
    p->sz = uvmdealloc(p->pagetable, oldsz, oldsz + n);
  }
  // n == 0: 直接返回当前边界

  return oldsz;
}

// lab4: SYS_pause(13) —— 当前进程休眠 n 个 tick 后返回。
uint64
sys_pause(void)
{
  int n;
  argint(0, &n);
  if (n <= 0)
    return 0;

  acquire(&tickslock);
  uint64 end = (uint64)ticks + n;
  while (ticks < end)
    sleep(&ticks, &tickslock);
  release(&tickslock);
  return 0;
}

// lab4: SYS_schedstat(23) —— pid=-1 查自身; 写回累计/最近片长两个计数。
uint64
sys_schedstat(void)
{
  int pid, cpu, slice;
  uint64 cpu_addr, slice_addr;
  struct proc *p;

  argint(0, &pid);
  argaddr(1, &cpu_addr);
  argaddr(2, &slice_addr);

  if (pid == -1) {
    p = myproc();
    acquire(&p->lock);
    cpu = p->cpu_ticks;
    slice = p->last_slice_ticks;
    release(&p->lock);
  } else {
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->pid == pid && p->state != UNUSED && p->state != ZOMBIE) {
        cpu = p->cpu_ticks;
        slice = p->last_slice_ticks;
        release(&p->lock);
        goto out;
      }
      release(&p->lock);
    }
    return -1;   // 目标不存在或已退出
  }

out:
  // 释放锁后再 copyout(访问用户页表不宜持锁)。
  if (copyout(myproc()->pagetable, myproc()->sz, cpu_addr, (char *)&cpu,
              sizeof(cpu)) < 0)
    return -1;
  if (copyout(myproc()->pagetable, myproc()->sz, slice_addr, (char *)&slice,
              sizeof(slice)) < 0)
    return -1;
  return 0;
}

// lab4: SYS_time_rdtime(24) —— 返回 time CSR 计数(非 tick)。
uint64
sys_time_rdtime(void)
{
  return r_time();
}

// lab4: SYS_setickets(25) —— 设置本进程票数, 并重算步长。
uint64
sys_setickets(void)
{
  int tickets;
  argint(0, &tickets);
  if (tickets < 1)
    return -1;

  struct proc *p = myproc();
  acquire(&p->lock);
  p->tickets = tickets;
  p->stride = STRIDE_BIG / tickets;
  release(&p->lock);
  return 0;
}

// lab4: SYS_kill(6) —— 可选, 实现后测试结束更快。
uint64
sys_kill(void)
{
  int pid;
  argint(0, &pid);
  return kkill(pid);
}
