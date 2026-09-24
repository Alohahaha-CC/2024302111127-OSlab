/*
 * lab1 main.c —— S 态主函数: 打印个性化 banner + 自检 + printf 边界, 然后停机。
 *
 * 学号 2024302111127 → 协议 2(整行 + ASCII 校验和)、mod97=0xe、栈 12KB、
 * 节流周期 16 + 学号%16 = 23 字节。
 * banner 精确文本(与 expect_banner.txt 逐字节一致):
 *   OSLAB1 sid=2024302111127 mod97=0xe\n[chk=2275]
 */
#include "types.h"
#include "riscv.h"
#include "course_sid.h"

void consoleinit(void);
int printf(const char *fmt, ...);
int snprintf(char *buf, int n, const char *fmt, ...);

// 协议 2 的校验和: 对整行正文(不含末尾换行、不含 [chk=] 本身)逐字节 ASCII 求和。
// 算法自定, 但必须与内核实际输出自洽(此处与打印共用同一份 snprintf 结果)。
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
  consoleinit();   // 初始化 16550 UART(轮询模式)

  // ---- 1) 个性化 banner(协议 2) ----
  // 先拼出正文 body, 对其求校验和, 再一次性输出, 保证与 expect_banner.txt 自洽。
  char body[64];
  snprintf(body, sizeof(body), "OSLAB1 sid=%ld mod97=0x%x",
           (long)COURSE_SID, (unsigned)(COURSE_SID % 97));
  printf("%s\n[chk=%d]", body, banner_checksum(body));

  // ---- 2) printf 边界自检(数字 0 / 负数 / 最大整数 / 空串 / 超长串) ----
  printf("\n== printf boundary ==\n");
  printf("[zero] d=%d u=%u x=0x%x\n", 0, 0, 0);
  printf("[neg ] d=%d d=%d d=%d\n", -1, -12345, -2147483647);
  printf("[max ] ld=%ld lx=0x%lx\n",
         (long)0x7fffffffffffffffL, 0x7fffffffffffffffUL);
  printf("[empty] <%s>\n", "");
  printf("[long ] <%s>\n",
         "0123456789abcdef0123456789abcdef0123456789abcdef"
         "0123456789abcdef0123456789abcdef0123456789abcdef"
         "0123456789abcdef0123456789abcdef0123456789abcdef"
         "0123456789abcdef0123456789abcdef0123456789abcdef");

  // ---- 3) 停机: 裸机内核跑完自检后驻留(无中断, wfi 即停) ----
  for (;;)
    asm volatile("wfi");
}
