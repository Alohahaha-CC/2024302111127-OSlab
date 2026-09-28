/*
 * lab2 main.c —— S 态主函数: banner 回归 + 建立内核页表/进程/中断环境,
 * 然后进入调度器, 首个进程(sh)在 forkret 里 exec("sh") 后进入用户态。
 *
 * lab1 回归: 个性化 banner(协议 2)照常打出。
 */
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "course_sid.h"

// 协议 2 校验和: 对整行正文逐字节 ASCII 求和(与 lab1 自洽)。
static int
banner_checksum(const char *body)
{
  int sum = 0;
  for (const char *p = body; *p; p++)
    sum += (unsigned char)*p;
  return sum;
}

void
main()
{
  if (cpuid() != 0) {
    // 从核不应跑到这里(entry.S 已 park), 兜底自旋。
    for (;;)
      asm volatile("wfi");
  }

  consoleinit(); // 初始化 UART(开接收中断)

  // ---- lab1 回归: banner(协议 2) ----
  char body[64];
  snprintf(body, sizeof(body), "OSLAB1 sid=%ld mod97=0x%x",
           (long)COURSE_SID, (unsigned)(COURSE_SID % 97));
  printf("%s\n[chk=%d]", body, banner_checksum(body));

  printf("\nOSLAB2: traps, syscalls, interrupt-driven console\n");

  // ---- 建立环境 ----
  kinit();         // 页分配器(起点 = 内核镜像 end)
  kvminit();       // 建内核页表
  kvminithart();   // 打开分页
  procinit();      // 进程表 + 内核栈映射
  trapinit();      // 时钟锁
  trapinithart();  // 内核态陷入向量
  plicinit();      // PLIC 优先级
  plicinithart();  // 使能 UART 中断

  // ---- 首个用户进程(sh), 然后交给调度器 ----
  userinit();
  scheduler(); // 永不返回
}
