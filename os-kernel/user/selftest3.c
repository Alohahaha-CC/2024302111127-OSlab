// 自测用例 · lab3 —— 说明书 §4 要求的两组用例(归档备查)
//
//   SELFTEST-3a 保护页越界访问: 子进程向"用户栈下方"的 guard page 写,
//                触发 store page fault, 被内核安全终止; 父进程 wait 回收。
//   SELFTEST-3b 跨页受控拷贝: sbrk 申请两页, 构造横跨页边界的缓冲(前 64
//                字节 'A' 落在第一页末尾、后 64 字节 'B' 落在第二页开头),
//                write 返回 128 且数据完整, 证明 copyin 逐页翻译跨页不丢字。
//
// 用法: make qemu 进 sh>, 键入 selftest3。

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "user/user.h"

// 读当前栈指针 sp(只有用户程序自己知道栈在哪)。
static uint64
read_sp(void)
{
  uint64 sp;
  asm volatile("mv %0, sp" : "=r"(sp));
  return sp;
}

int
main(void)
{
  // ---- 用例 3a: 保护页越界写 ----
  int pid = fork();
  if (pid == 0) {
    uint64 sp = read_sp();
    // 用户栈只有 1 页, 栈顶是 sp; 向下跨 2 页正好落到 LAB3_GUARD_PAGES=2
    // 的 guard page(未映射空洞)里, 写它必然 store page fault(scause=15)。
    volatile int *p = (volatile int *)(sp - 2 * PGSIZE);
    *p = 1;
    printf("SELFTEST-3a FAIL: guard write survived\n");
    exit(0);                      // 不应走到这里
  }
  int st;
  wait(&st);
  if (st != 0)
    printf("SELFTEST-3a PASS: guard access killed (status=%d)\n", st);
  else
    printf("SELFTEST-3a FAIL: child survived guard write\n");

  // ---- 用例 3b: 跨页受控拷贝 ----
  char *p = sbrk(2 * PGSIZE);            // 申请两页, p = 旧堆边界
  if (p == (char *)-1 || p == 0) {
    printf("SELFTEST-3b FAIL: sbrk failed\n");
    exit(1);
  }
  char *mid = p + PGSIZE - 64;           // 页边界前 64 字节
  for (int i = 0; i < 64; i++)
    mid[i] = 'A';                        // 第一页末尾
  for (int i = 64; i < 128; i++)
    mid[i] = 'B';                        // 第二页开头(跨页)
  int n = write(1, mid, 128);            // copyin 逐页搬运 64A+64B
  printf("\nSELFTEST-3b %s (cross-page write=%d/128)\n",
         n == 128 ? "PASS" : "FAIL", n);

  exit(0);
}
