// lab4 抢占验证: 两个纯计算子进程分别持续打印 A、B。
// 加入时钟抢占前, 输出为"全部 A 之后全部 B"(或相反);
// 加入抢占后, A 与 B 交错出现, 最后打印 "alt done" 并回到提示符。
#include "kernel/types.h"
#include "user/user.h"

static void
run(char c)
{
  for (int i = 0; i < 40; i++) {
    for (volatile int d = 0; d < 20000000; d++)
      ;
    write(1, &c, 1);
  }
  exit(0);
}

int
main(void)
{
  if (fork() == 0)
    run('A');
  if (fork() == 0)
    run('B');
  wait(0);
  wait(0);
  printf("\nalt done\n");
  exit(0);
}
