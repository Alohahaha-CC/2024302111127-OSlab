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
