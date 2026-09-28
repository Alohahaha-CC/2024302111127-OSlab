// lab2 sysfile.c —— 控制台读写 + exec 系统调用(无文件系统)。
//
// lab2 只有标准输入(0)/输出(1、2), 直接对接 console; 磁盘/文件是 lab6 的债。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_read(void)
{
  int fd, n;
  uint64 p;

  argint(0, &fd);
  argaddr(1, &p);
  argint(2, &n);

  if (fd != 0)
    return -1; // lab2 仅支持 stdin

  return consoleread(1, p, n);
}

uint64
sys_write(void)
{
  int fd, n;
  uint64 p;

  argint(0, &fd);
  argaddr(1, &p);
  argint(2, &n);

  if (fd != 1 && fd != 2)
    return -1; // lab2 仅支持 stdout/stderr

  return consolewrite(1, p, n);
}

uint64
sys_exec(void)
{
  char path[MAXPATH];
  uint64 uargv;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  // lab2 的内嵌程序均无参数, argv 允许忽略(lab5 再完整化)。
  (void)uargv;

  return kexec(path);
}
