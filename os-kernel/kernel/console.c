// lab2 console.c —— 16550 UART 中断驱动控制台 + 输入环形缓冲。
//
// 发送(TX): 保留 lab1 的轮询同步发射 uartputc_sync(现加 push_off 防重入);
//           内核 printf 走 putchar(叠加"空转节流"以满足 lab1 回归),
//           用户 write / 回显走 uartputc_sync(不节流)。
// 接收(RX): 开接收中断, uartintr 读 RHR 交给 consoleintr 回显并入环形缓冲,
//           按 LAB2_BUF_SEMANTICS 唤醒读者(1=字符流, 0=行缓冲)。
//
// 缓冲: 单调递增下标 r/w/e 配合取模索引, 无符号差值即元素个数。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "course_sid.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// 16550 寄存器偏移
#define RHR 0   // 接收保持寄存器(读)
#define THR 0   // 发送保持寄存器(写)
#define IER 1   // 中断使能
#define FCR 2   // FIFO 控制
#define ISR 2   // 中断状态
#define LCR 3   // 线控制
#define LSR 5   // 线状态

#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR (3 << 1)
#define LCR_EIGHT_BITS (3 << 0)
#define LSR_RX_READY (1 << 0)
#define LSR_TX_IDLE (1 << 5)

// lab1 空转节流: 每 (16 + COURSE_SID % 16) 字节 nop 一次。
#define THROTTLE_PERIOD (16 + (COURSE_SID % 16))
#define THROTTLE_NOPS 1024
static uint64 emitted = 0;

void
uartinit(void)
{
  WriteReg(IER, 0x00);                             // 先关中断
  WriteReg(LCR, LCR_EIGHT_BITS);                   // 8N1
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR); // 清空并使能 FIFO
  WriteReg(IER, IER_RX_ENABLE);                    // 只开接收中断
}

// 单字节同步发射: 关中断下轮询 THR, 防止与中断回显重入。
void
uartputc_sync(int c)
{
  push_off();
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
  pop_off();
}

// 内核 printf 输出(叠加节流, lab1 回归)。
void
putchar(int c)
{
  uartputc_sync(c);
  if (++emitted >= THROTTLE_PERIOD) {
    emitted = 0;
    for (volatile int i = 0; i < THROTTLE_NOPS; i++)
      asm volatile("nop");
  }
}

// 回显单字符(无节流)。
void
consputc(int c)
{
  uartputc_sync(c);
}

// 读一个输入字符, 无则返回 -1。
static int
uartgetc(void)
{
  if (ReadReg(LSR) & LSR_RX_READY)
    return ReadReg(RHR);
  return -1;
}

// ---- 输入环形缓冲 ----
#define INPUT_BUF_SIZE LAB2_BUF_SIZE

struct {
  struct spinlock lock;
  char buf[INPUT_BUF_SIZE];
  uint r; // 读下标
  uint w; // 写下标(已提交给读者)
  uint e; // 编辑下标
} cons;

// 串口中断: 逐个读字符进缓冲。
void
uartintr(void)
{
  ReadReg(ISR); // 确认中断

  while (1) {
    int c = uartgetc();
    if (c == -1)
      break;
    consoleintr(c);
  }
}

// 处理一个输入字符: 回显 + 入缓冲 + 按语义唤醒读者。
void
consoleintr(int c)
{
  acquire(&cons.lock);

  if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) {
    c = (c == '\r') ? '\n' : c;

    consputc(c); // 回显

    cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

#if LAB2_BUF_SEMANTICS == 1
    // 字符流: 每敲一个键立刻提交并唤醒读者。
    cons.w = cons.e;
    wakeup(&cons.r);
#else
    // 行缓冲: 只有回车才提交唤醒。
    if (c == '\n' || cons.e - cons.r == INPUT_BUF_SIZE) {
      cons.w = cons.e;
      wakeup(&cons.r);
    }
#endif
  }

  release(&cons.lock);
}

// 用户 read(0, ...): 从缓冲取字符, 遇 '\n' 或取满 n 返回。
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while (n > 0) {
    // 缓冲空: 入睡等中断唤醒。
    while (cons.r == cons.w) {
      if (killed(myproc())) {
        release(&cons.lock);
        return -1;
      }
      sleep_prepare(&cons.r);
      release(&cons.lock);
      sleep();
      acquire(&cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    cbuf = c;
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if (c == '\n')
      break; // 整行读完
  }
  release(&cons.lock);

  return target - n;
}

// 用户 write(1, ...): 批量搬用户字节 → 串口。
int
consolewrite(int user_src, uint64 src, int n)
{
  char buf[32];
  int i = 0;

  while (i < n) {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    for (int j = 0; j < nn; j++)
      uartputc_sync(buf[j]);
    i += nn;
  }

  return i;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");
  uartinit();
}
