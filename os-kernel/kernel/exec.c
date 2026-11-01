// lab3 exec.c —— 从内嵌程序表装载并执行程序(段级权限 + guard page)。
//
// lab3 相对 lab2 的升级:
//   - 程序表 ABI 扩展为 {start; end; data_start; mem_end; name}(tools/gen_userimg.py
//     生成), data_start 标记"只读区结束/可写区起始"(页对齐), mem_end 标记
//     进程内存总大小(含 bss)。
//   - 段级权限: [0, data_start) 代码+只读数据 R+X(严禁 W, 供 badaccess TEST-3a
//     校验写代码段被杀); [data_start, data_end) 数据+bss R+W。
//   - 在数据之后、用户栈之前插入 LAB3_GUARD_PAGES 个保护页(未映射空洞),
//     越界读写触发硬件缺页被 usertrap 安全终止。
//
// 用户地址空间布局(低→高, 与 lab0 快照图对齐):
//   [0, data_start)  代码+只读数据  R+X+U
//   [data_start, data_end) 数据+bss  R+W+U
//   [data_end, stack_start) guard page ×LAB3_GUARD_PAGES (未映射)
//   [stack_start, sz) 用户栈 1 页  R+W+U
//   [sz, ...)         堆(向上增长, sbrk)

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "course_sid.h"

// 内嵌程序表(由 tools/gen_userimg.py 生成)。
struct uprog {
  uint64 start;       // 程序二进制起始(.incbin, 基址 0)
  uint64 end;         // 程序二进制末尾(不含 bss)
  uint64 data_start;  // 只读区结束/可写区起始(页对齐)
  uint64 mem_end;     // 进程内存总大小(end 符号, 含 bss)
};

extern uint64 _uprog_table[];

// 按名字查程序表, 找不到返回 0。表项连续存放, 每项 = 32 字节头 + align8(name)。
static struct uprog *
find_uprog(char *name)
{
  uint64 *p = _uprog_table;

  for (;;) {
    uint64 start = p[0];
    uint64 end = p[1];
    if (start == 0 && end == 0)
      return 0; // 表尾

    char *n = (char *)(p + 4);
    if (strncmp(n, name, strlen(name) + 1) == 0)
      return (struct uprog *)p;

    uint64 sz = 32 + strlen(n) + 1;
    sz = (sz + 7) & ~7ull;
    p = (uint64 *)((char *)p + sz);
  }
}

// 把内核内 [src, src+filesz) 的字节拷到用户页表 [va, va+memsz)(超出 filesz
// 的部分清零), 权限 perm。失败返回 -1 并回滚本次已建立的映射与物理页。
static int
loadseg(pagetable_t pagetable, uint64 va, char *src, uint64 filesz,
        uint64 memsz, int perm)
{
  uint64 i, n;
  char *mem;

  for (i = 0; i < memsz; i += PGSIZE) {
    mem = kalloc();
    if (mem == 0)
      goto err;
    memset(mem, 0, PGSIZE);
    if (i < filesz) {
      n = filesz - i;
      if (n > PGSIZE)
        n = PGSIZE;
      memmove(mem, src + i, n);
    }
    if (mappages(pagetable, va + i, PGSIZE, (uint64)mem, perm) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(pagetable, va, i / PGSIZE, 1);
  return -1;
}

// 装载 path 指向的内嵌程序到当前进程, 替换其用户内存。成功返回 0, 失败 -1
// (失败时保留原地址空间, 进程可继续运行)。
int
kexec(char *path)
{
  // 剥离尾部空白(gets 把 '\n'/'\r' 也读进命令名)。
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
  uint64 size = up->end - up->start;        // 二进制大小(.text+.rodata+.data)
  uint64 data_start = up->data_start;       // 只读/可写边界(页对齐)
  uint64 data_end = PGROUNDUP(up->mem_end); // 数据+bss 结束(页对齐)
  uint64 stack_start = data_end + LAB3_GUARD_PAGES * PGSIZE;
  uint64 sz = stack_start + PGSIZE;         // 初始 break = 堆起点 = 栈顶
  uint64 oldsz = p->sz;
  pagetable_t old = p->pagetable;

  pagetable_t pt = proc_pagetable(p);
  if (pt == 0)
    return -1;

  // 代码+只读数据 [0, data_start) R+X+U
  uint64 code_filesz = (size < data_start) ? size : data_start;
  if (loadseg(pt, 0, (char *)up->start, code_filesz, data_start,
              PTE_R | PTE_X | PTE_U) < 0)
    goto bad;

  // 数据+bss [data_start, data_end) R+W+U
  uint64 data_filesz = (size > data_start) ? (size - data_start) : 0;
  if (loadseg(pt, data_start, (char *)up->start + data_start, data_filesz,
              data_end - data_start, PTE_R | PTE_W | PTE_U) < 0)
    goto bad;

  // 用户栈 [stack_start, sz) R+W+U(guard 页为未映射空洞, 无需映射)
  if (loadseg(pt, stack_start, 0, 0, PGSIZE, PTE_R | PTE_W | PTE_U) < 0)
    goto bad;

  // 提交新镜像, 释放旧地址空间。
  p->pagetable = pt;
  p->sz = sz;
  safestrcpy(p->name, name, sizeof(p->name));

  // lab4: 按进程名重设默认票数(hi=1/spin=2/sh=4)。
  p->tickets = default_tickets(p->name);
  p->stride = STRIDE_BIG / p->tickets;

  // 伪造第一现场: epc = 程序入口(VA 0), sp = 用户栈顶。
  p->trapframe->epc = 0;
  p->trapframe->sp = sz;

  proc_freepagetable(old, oldsz);
  return 0;

bad:
  proc_freepagetable(pt, sz);
  return -1;
}
