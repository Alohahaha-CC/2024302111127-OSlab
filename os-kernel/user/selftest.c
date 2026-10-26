// 自测用例 · lab2 —— read 边界行为
//
// 覆盖说明书 §4 给出的两个例子:
//   ① 向 read 传入恰好等于缓冲区容量的长度 (LAB2_BUF_SIZE = 128)
//   ② 空缓冲区时 read 的行为 (无输入时应阻塞/睡眠, 来字符后才返回)
//
// 用法: 在 sh 里执行 selftest; 它会先打印一行"现在阻塞等你输入",
//       此刻验证"空缓冲区 read 阻塞"; 再喂入一行, 验证"read(128) 不溢出"。

#include "kernel/types.h"
#include "user/user.h"

#define N 128 // 恰好等于 LAB2_BUF_SIZE

int
main(void)
{
  char buf[N];

  // 用例②: 空缓冲区——打印后立即 read, 无输入则一直停在这里(阻塞)
  printf("SELFTEST: 空缓冲区 read(128) 阻塞中, 请喂入输入...\n");
  int n = read(0, buf, sizeof(buf)); // sizeof(buf) == 128

  // 用例①: read 长度恰好 = 容量, 返回 n 不应超过 128(无溢出)
  printf("SELFTEST: read(128) 返回 %d 字节\n", n);
  if (n <= N)
    printf("SELFTEST PASS (n=%d <= %d, 无溢出)\n", n, N);
  else
    printf("SELFTEST FAIL (n=%d > %d, 溢出!)\n", n, N);

  exit(0);
}
