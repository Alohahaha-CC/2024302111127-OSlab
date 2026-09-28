// lab2 proc.h —— 进程/上下文/trapframe 结构定义。
//
// struct trapframe 的字段顺序与偏移必须与 kernel/trampoline.S 的硬编码偏移
// 严格一一对应(trampoline.S 用 sd ra,40(a0) 这类数字偏移读写, 不认识结构体)。
// 因此每个字段后的注释都标注了字节偏移, 改动即破坏 ABI。

#include "types.h"
#include "riscv.h"
#include "spinlock.h"

// 内核上下文: swtch.S 保存/恢复的 callee-saved 寄存器。
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// 每 CPU 状态(lab2 单核, 实际只用 cpus[0])。
struct cpu {
  struct proc *proc;      // 当前运行的进程(无则 0)
  struct context context; // 调度器上下文
  int noff;               // push_off 嵌套深度
  int intena;             // 首次 push_off 前中断是否使能
};

extern struct cpu cpus[NCPU];

// 每个进程的陷入现场, 独占一页, 位于用户页表 trampoline 页正下方。
// 与 trampoline.S 的偏移一一对应。
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // 内核页表(satp 值)
  /*   8 */ uint64 kernel_sp;     // 本进程内核栈顶
  /*  16 */ uint64 kernel_trap;   // usertrap() 入口
  /*  24 */ uint64 epc;           // 保存的用户 PC
  /*  32 */ uint64 kernel_hartid; // 保存的内核 tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 进程控制块(lab2 最小进程机制: 无文件系统、无真实调度)。
struct proc {
  struct spinlock lock;    // 保护本进程状态字段

  enum procstate state;    // 进程状态
  int pid;                 // 进程号
  struct proc *parent;     // 父进程
  int killed;              // 被请求杀死
  int xstate;              // exit 状态(给 wait 回收)
  char name[16];           // 调试用进程名

  uint64 sz;               // 用户内存大小(字节)
  pagetable_t pagetable;   // 用户页表(物理地址)
  struct trapframe *trapframe; // 陷入现场页(物理地址)
  uint64 kstack;           // 内核栈顶(KSTACK 虚拟地址)
  struct context context;  // swtch 上下文
  void *chan;              // sleep 等待通道
};

extern struct proc proc[NPROC];
