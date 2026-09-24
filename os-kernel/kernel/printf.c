/*
 * lab1 printf.c —— 最小格式化打印。
 *
 * 支持格式(设计笔记 §7 预立规范):
 *   %d / %ld  有符号十进制(负数带 '-' 前缀, 不溢出)
 *   %u / %lu  无符号十进制
 *   %x / %lx  十六进制(小写、无前导零、不带 0x 前缀)
 *   %p        指针(0x + 16 位十六进制)
 *   %c / %s / %%
 * 核心格式化器 vprintfmt 通过 (emit, ctx) 回调解耦输出目标:
 *   printf    → 输出到控制台(putchar, 叠加节流);
 *   snprintf  → 输出到缓冲区(供 banner 校验和计算, 保证自洽)。
 */
#include "types.h"
#include "riscv.h"
#include <stdarg.h>

void putchar(int c);

static char digits[] = "0123456789abcdef";

// 把 xx 按 base 打印; sign!=0 时按有符号处理(负数补 '-').
static void
printint(long long xx, int base, int sign, void (*emit)(int, void *), void *ctx)
{
  char buf[24];
  int i = 0;
  unsigned long long x;
  int neg = 0;

  if (sign && xx < 0) {
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (neg)
    buf[i++] = '-';

  while (--i >= 0)
    emit(buf[i], ctx);
}

// 打印指针: "0x" + 16 位十六进制(高位补 0)。
static void
printptr(uint64 x, void (*emit)(int, void *), void *ctx)
{
  int i;
  emit('0', ctx);
  emit('x', ctx);
  for (i = 0; i < (int)(sizeof(uint64) * 2); i++, x <<= 4)
    emit(digits[x >> (sizeof(uint64) * 8 - 4)], ctx);
}

// 核心格式化器: 解析 fmt, 逐字符调用 emit。
static void
vprintfmt(void (*emit)(int, void *), void *ctx, const char *fmt, va_list ap)
{
  int i, c;
  char *s;

  for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
    if (c != '%') {
      emit(c, ctx);
      continue;
    }
    i++;
    c = fmt[i] & 0xff;
    if (c == 0)
      break;

    switch (c) {
    case 'd':
      printint((long long)va_arg(ap, int), 10, 1, emit, ctx);
      break;
    case 'l':
      c = fmt[i + 1] & 0xff;
      if (c == 'd') {
        printint(va_arg(ap, long), 10, 1, emit, ctx);
        i++;
      } else if (c == 'u') {
        printint(va_arg(ap, unsigned long), 10, 0, emit, ctx);
        i++;
      } else if (c == 'x') {
        printint(va_arg(ap, unsigned long), 16, 0, emit, ctx);
        i++;
      } else {
        emit('l', ctx);
      }
      break;
    case 'u':
      printint(va_arg(ap, unsigned int), 10, 0, emit, ctx);
      break;
    case 'x':
      printint(va_arg(ap, unsigned int), 16, 0, emit, ctx);
      break;
    case 'p':
      printptr(va_arg(ap, uint64), emit, ctx);
      break;
    case 'c':
      emit((char)va_arg(ap, int), ctx);
      break;
    case 's':
      if ((s = va_arg(ap, char *)) == 0)
        s = "(null)";
      for (; *s; s++)
        emit(*s, ctx);
      break;
    case '%':
      emit('%', ctx);
      break;
    default:
      // 未知 % 序列: 原样打印以引起注意。
      emit('%', ctx);
      emit(c, ctx);
      break;
    }
  }
}

// 输出到控制台的 emit 适配器
static void
cons_emit(int c, void *ctx)
{
  (void)ctx;
  putchar(c);
}

// 标准 printf: 输出到控制台(经 putchar, 叠加节流)。
int
printf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintfmt(cons_emit, 0, fmt, ap);
  va_end(ap);
  return 0;
}

// 缓冲 emit 适配器(供 snprintf)
struct sbuf {
  char *buf;
  int n;
  int len;
};

static void
sbuf_emit(int c, void *ctx)
{
  struct sbuf *b = (struct sbuf *)ctx;
  if (b->len + 1 < b->n)
    b->buf[b->len] = (char)c;
  b->len++;
}

// 格式化到缓冲区: 供 banner 校验和计算(保证内核输出与 expect_banner.txt 自洽)。
int
snprintf(char *buf, int n, const char *fmt, ...)
{
  struct sbuf b;
  b.buf = buf;
  b.n = n;
  b.len = 0;

  va_list ap;
  va_start(ap, fmt);
  vprintfmt(sbuf_emit, &b, fmt, ap);
  va_end(ap);

  if (n > 0)
    b.buf[(b.len < n ? b.len : n - 1)] = '\0';
  return b.len;
}
