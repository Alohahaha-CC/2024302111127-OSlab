// lab2 proc.c —— 最小进程机制(够跑 sh / fork / exec / wait)。
//
// 无文件系统、无真实抢占调度: 进程只在阻塞(wait/read)或退出时让出 CPU,
// 这是 lab2 允许的"假调度"; 时间片调度留到 lab4。
// 页表(proc_pagetable)已提前建立, 见 vm.c 头注释。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

struct spinlock wait_lock;

extern char trampoline[]; // trampoline.S
extern char userret[];    // trampoline.S

void forkret(void);

static void freeproc(struct proc *p);

// 为每个进程槽位分配内核栈页, 映射到 KSTACK(i) 高地址。
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

//初始化 pid_lock、wait_lock
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

int
cpuid(void)
{
  return r_tp();
}

struct cpu *
mycpu(void)
{
  int id = cpuid();
  return &cpus[id];
}

struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid(void)
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 从进程表找 UNUSED 槽位, 初始化内核侧资源, 返回带锁的进程。
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED)
      goto found;
    release(&p->lock);
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // 分配 trapframe 页。
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 建空用户页表(含 trampoline/trapframe 映射)。
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 首次被调度时从 forkret 起跑, 栈用本进程内核栈。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // lab4 调度参数默认值(exec 时按名重设 tickets)。
  p->tickets = 1;
  p->stride = STRIDE_BIG;
  p->pass = 0;
  p->cpu_ticks = 0;
  p->last_slice_ticks = 0;

  return p;
}

// 释放进程及其附属页。须持 p->lock。
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// 建用户页表: 空页表 + trampoline + trapframe 两页。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // trampoline 映射到最高用户 VA(仅 S 态往返时用, 不加 U 位)。
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // trapframe 页在 trampoline 页正下方。
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 建第一个进程(initproc = sh), forkret 里会 exec("sh")。
//占一个进程名额、分配好 trapframe 和空页表、把状态设成 RUNNABLE（就绪）
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->state = RUNNABLE;

  release(&p->lock);
}

// 创建子进程: 复制父进程内存 + 现场, 子进程 fork 返回 0。
int
kfork(void)
{
  int pid;
  struct proc *np;
  struct proc *p = myproc();

  if ((np = allocproc()) == 0)
    return -1;

  // 复制用户内存。
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 复制陷入现场, 子进程 a0 = 0(fork 返回 0)。
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->a0 = 0;

  safestrcpy(np->name, p->name, sizeof(p->name));

  // lab4: 子进程继承父进程的调度参数。
  np->tickets = p->tickets;
  np->stride = p->stride;
  np->pass = p->pass;

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// 把 p 的子进程过继给 init(须持 wait_lock)。
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// 退出当前进程, 不返回: 置 ZOMBIE、唤醒父进程、让出 CPU。
void
kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  acquire(&wait_lock);

  reparent(p);

  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  sched();
  panic("zombie exit");
}

// 等待子进程退出, 返回其 pid; 无子进程返回 -1。
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          pid = pp->pid;
          if (addr != 0 &&
              copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                      sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          pp->parent = 0;
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // 没有已退出的子进程, 睡觉等唤醒。
    sleep(p, &wait_lock);
  }
}

// 步长调度: 每轮在 RUNNABLE 中选 pass 最小者, 分派后 pass += stride。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // 避免所有进程都在睡时死锁: 打开中断收唤醒。
    intr_on();

    // 第一步: 扫描, 选出 pass 最小的就绪进程(持其锁)。
    p = 0;
    for (struct proc *q = proc; q < &proc[NPROC]; q++) {
      acquire(&q->lock);
      if (q->state == RUNNABLE) {
        if (p == 0 || q->pass < p->pass) {
          if (p)
            release(&p->lock);
          p = q;
          continue;   // 保留 q 的锁, 跳过下方 release
        }
      }
      release(&q->lock);
    }

    if (p == 0) {
      // 没有可运行进程, 停等中断。
      asm volatile("wfi");
      continue;
    }

    // 第二步: 交接。此处 p 已持锁。
    p->state = RUNNING;
    c->proc = p;
    p->last_slice_ticks = 0;   // schedstat 规范: 分发时清零
    p->pass += p->stride;      // 步长调度: 每个时间片 pass 前进一个 stride
    swtch(&c->context, &p->context);
    c->proc = 0;
    release(&p->lock);
  }
}

// 让出 CPU 到调度器。须只持 p->lock 且已改 state。
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 让出 CPU 一轮。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// 子进程首次被调度时从这进入: 首次 exec("sh"), 然后回用户态。
void
forkret(void)
{
  static int first = 1;
  struct proc *p = myproc();

  release(&p->lock);

  if (first) {
    first = 0;
    p->trapframe->a0 = kexec("sh");//1.把sh程序装进用户页表
    if (p->trapframe->a0 < 0)
      panic("exec sh");

    // lab3 观测规范: 首个用户态进程(sh)页表装配完成后, 自动打印一次页表 dump。
    // dump_pagetable(pagetable_t) 进程无关, lab4 回归时可对任意进程重放。
    dump_pagetable(p->pagetable);
  }

  usertrapret();

  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// 原子地: 释放条件锁 lk 并睡在 chan 上, 醒来后重新获取 lk。
// 关键锁序: 先 acquire(&p->lock) 再 release(lk), 使"改 SLEEPING + 让出 CPU"
// 与"wakeup 扫描"互斥, 从而消除丢失唤醒窗口。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);   // 调用者已持 lk, noff: 1→2
  release(lk);         // noff: 2→1, 只剩 p->lock

  p->chan = chan;
  p->state = SLEEPING;
  sched();             // noff==1, 满足 sched 不变式

  p->chan = 0;
  release(&p->lock);
  acquire(lk);         // 醒来重新拿回条件锁
}

// 唤醒所有睡在 chan 上的进程。
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p == myproc())
      continue;
    acquire(&p->lock);
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
    release(&p->lock);
  }
}

// 杀掉指定 pid 的进程(惰性: 下次回用户态才生效)。
int
kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// 按 user_dst/user_src 决定拷到用户还是内核地址。
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
    return copyout(p->pagetable, p->sz, dst, src, len);
  memmove((char *)dst, src, len);
  return 0;
}

int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
    return copyin(p->pagetable, p->sz, dst, src, len);
  memmove(dst, (char *)src, len);
  return 0;
}

// ID 7(同 ID 5): 按进程名分配默认票数 hi=1 / spin=2 / sh=4, 其余默认 1。
int
default_tickets(char *name)
{
  if (strncmp(name, "hi", 3) == 0)
    return 1;
  if (strncmp(name, "spin", 5) == 0)
    return 2;
  if (strncmp(name, "sh", 3) == 0)
    return 4;
  return 1;
}
