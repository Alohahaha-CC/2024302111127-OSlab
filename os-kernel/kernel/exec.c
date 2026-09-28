// lab2 exec.c —— 从内嵌程序表 _uprog_table 装载并执行程序。
//
// 现阶段没有磁盘/文件系统(lab6 的债), 用户程序在编译期被 .incbin 进内核,
// Makefile.upgrade 生成全局表 _uprog_table: 每项 = {u64 start; u64 end;
// char name[]}(.balign 8), 以 {0,0} 结尾。这里按名字查找, 把平铺二进制
// 拷到新页表的 VA 0, 伪造第一现场(epc=0, sp=栈顶)后返回, 由调用者 sret
// 进用户态。
//
// 设计要点: 平铺二进制不含 .bss(objcopy -O binary 只带文件内容), 而用户
// 程序(如 sh 的 static char buf[64])的全局变量正是放在 .bss, 故装载时在
// 二进制之后多清 2 页作为 bss 零区(程序都很小, 该余量绰绰有余)。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 内嵌程序表(由 kernel/userimg.S 生成)。
struct uprog {
  uint64 start;
  uint64 end;
  // 紧接其后的 char name[](NUL 结尾, 8 字节对齐)
};

extern uint64 _uprog_table[];

// 按名字查程序表, 找不到返回 0。
static struct uprog *
find_uprog(char *name)
{
  uint64 *p = _uprog_table;

  for (;;) {
    uint64 start = p[0];
    uint64 end = p[1];
    if (start == 0 && end == 0)
      return 0; // 表尾

    char *n = (char *)(p + 2);
    if (strncmp(n, name, strlen(name) + 1) == 0)
      return (struct uprog *)p;

    // 下一项: 跳过 16 字节头 + 名字 + 对齐到 8。
    uint64 sz = 16 + strlen(n) + 1;
    sz = (sz + 7) & ~7ull;
    p = (uint64 *)((char *)p + sz);
  }
}

// 装载 path 指向的内嵌程序到当前进程, 替换其用户内存。成功返回 0, 失败 -1。
int
kexec(char *path)
{
  // sh 的 gets() 把行尾 '\n'/'\r' 也读进了命令名, 这里剥离尾部空白再查表。
  char name[MAXPATH];
  int len = strlen(path);
  while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r' ||
                     path[len - 1] == ' ' || path[len - 1] == '\t'))
    len--;
  if (len == 0)
    return -1;
  memmove(name, path, len);
  name[len] = '\0';

  struct uprog *up = find_uprog(name);
  if (up == 0)
    return -1;

  struct proc *p = myproc();
  uint64 size = up->end - up->start;
  uint64 oldsz = p->sz;
  pagetable_t old = p->pagetable;

  // 新页表(含 trampoline + trapframe 映射)。
  pagetable_t pt = proc_pagetable(p);
  if (pt == 0)
    return -1;

  // 布局: [0, progsz) 程序(含 2 页 bss 余量), [progsz, progsz+PGSIZE) 用户栈。
  uint64 progsz = PGROUNDUP(size) + 2 * PGSIZE;
  uint64 total = progsz + PGSIZE;

  if (uvmalloc(pt, 0, total, PTE_W | PTE_X) == 0)
    goto bad;

  // 把二进制拷到 VA 0(uvmalloc 已把页清零, 覆盖后 bss 自然为 0)。
  if (copyout(pt, total, 0, (char *)up->start, size) < 0)
    goto bad;

  // 提交新镜像。
  p->pagetable = pt;
  p->sz = total;
  safestrcpy(p->name, name, sizeof(p->name));

  // 伪造第一现场: epc = 程序入口(VA 0 = main), sp = 用户栈顶。
  p->trapframe->epc = 0;
  p->trapframe->sp = total;

  proc_freepagetable(old, oldsz);
  return 0;

bad:
  proc_freepagetable(pt, total);
  return -1;
}
