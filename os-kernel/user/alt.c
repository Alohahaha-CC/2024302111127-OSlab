// lab4 抢占验证: 两个纯计算子进程分别持续打印 A、B。
// 加入时钟抢占前, 输出为"全部 A 之后全部 B"(或相反);
// 加入抢占后, A 与 B 交错出现, 最后打印 "alt done" 并回到提示符。
//
// 【重要】子进程逻辑必须直接写进 main, 不能用独立的 run() 辅助函数。
// 原因: 本内核用户程序从虚拟地址 0 开始执行(exec.c 里 epc=0), 而地址 0
// 是链接后 .text 段的第一个函数。若用独立函数 run(), 编译器可能把 main
// 放进 .text.startup 段、run 留在 .text 段, 链接时 .text 排在 .text.* 之前,
// 导致 run 占住地址 0、main 永远不执行。让 main 成为唯一函数即可保证入口正确。
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  if (fork() == 0) {
    char c = 'A';
    for (int i = 0; i < 40; i++) {
      for (volatile int d = 0; d < 20000000; d++)
        ;
      write(1, &c, 1);
    }
    exit(0);
  }

  if (fork() == 0) {
    char c = 'B';
    for (int i = 0; i < 40; i++) {
      for (volatile int d = 0; d < 20000000; d++)
        ;
      write(1, &c, 1);
    }
    exit(0);
  }

  wait(0);
  wait(0);
  printf("\nalt done\n");
  exit(0);
}
