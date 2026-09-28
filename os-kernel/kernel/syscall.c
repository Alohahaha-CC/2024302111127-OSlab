// lab2 syscall.c —— 系统调用分发与参数解包。
//
// 用户侧桩(user/usys.pl 生成)把调用号放 a7、参数放 a0–a2 后 ecall;
// 进入内核后这些寄存器已由 trampoline 存进 trapframe, 这里按序号取回。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// 从当前进程取第 n 个原始参数(寄存器的裸值)。
static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// 取第 n 个 32 位整型参数。
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// 取第 n 个参数当作指针(合法性由 copyin/copyout 兜底)。
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// 从用户内存读 uint64(查 addr 是否越界)。
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
    return -1;
  if (copyin(p->pagetable, p->sz, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// 从用户内存读 NUL 结尾字符串。
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if (copyinstr(p->pagetable, p->sz, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

// 取第 n 个参数作为字符串拷贝进 buf。
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// 各系统调用处理函数原型。
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_read(void);
extern uint64 sys_exec(void);
extern uint64 sys_getpid(void);
extern uint64 sys_write(void);

// 调用号 → 处理函数的分发表(表驱动; 未实现项留空, 由 default 返回 -1)。
static uint64 (*syscalls[])(void) = {
  [SYS_fork]   sys_fork,
  [SYS_exit]   sys_exit,
  [SYS_wait]   sys_wait,
  [SYS_read]   sys_read,
  [SYS_exec]   sys_exec,
  [SYS_getpid] sys_getpid,
  [SYS_write]  sys_write,
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 调用并把返回值写回 a0。
    p->trapframe->a0 = syscalls[num]();
  } else {
    // 未知/未实现的调用号: 返回 -1, 不 panic。
    p->trapframe->a0 = -1;
  }
}
