/*
 * lab1 console.c —— 16550 UART 轮询输出(裸机, 无中断)。
 *
 * 分层(见《设计笔记》附录 A):
 *   uartputc_sync: 单字节硬件发射(轮询 LSR bit5 THRE 后写 THR), 不可重入;
 *   putchar:       上层单字符输出, 叠加"空转节流"注入;
 *   banner 的协议修饰(校验和)在 main.c 的 banner 层完成。
 */
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"

// 16550 UART 寄存器(mmio, 基址 UART0 = 0x10000000)
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// 寄存器偏移(16550 规范)
#define THR 0   // 发送保持寄存器(写)
#define IER 1   // 中断使能寄存器
#define FCR 2   // FIFO 控制寄存器
#define LCR 3   // 线控制寄存器
#define LSR 5   // 线状态寄存器

#define IER_TX_ENABLE   (1 << 1)
#define IER_RX_ENABLE   (1 << 0)
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR  (3 << 1)
#define LCR_EIGHT_BITS  (3 << 0)
#define LSR_TX_IDLE     (1 << 5)   // THRE: 发送保持寄存器空, 可写下一字符

// 空转节流: 每输出 (16 + COURSE_SID % 16) 字节后执行一次 nop 空转。
// 严禁硬编码 23 —— 必须由 COURSE_SID 宏推导(验收"节流参数源码核查"项)。
#define THROTTLE_PERIOD (16 + (COURSE_SID % 16))
#define THROTTLE_NOPS   1024          // 单次节流的 nop 次数(自定空转强度)

static uint64 emitted = 0;   // 已输出字节计数(节流用)

// 初始化 16550: 轮询模式(关中断) + 8N1 + 清空并使能 FIFO。
void
uartinit(void)
{
  WriteReg(IER, 0x00);                              // 关中断: 本实验纯轮询
  WriteReg(LCR, LCR_EIGHT_BITS);                    // 8 数据位、无校验、1 停止位
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);  // 清空并使能 FIFO
}

// 单字节硬件发射: 轮询 LSR bit5(THRE) 直到发送保持寄存器空闲, 再写 THR。
// 启动早期无时钟中断, 忙等是唯一可靠行为; 仅单核单上下文调用, 不可重入。
void
uartputc_sync(int c)
{
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
}

// 上层字符输出: 每次发射后计数, 每满 THROTTLE_PERIOD 字节执行一次 nop 空转。
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

// 控制台初始化入口(供 main 调用)
void
consoleinit(void)
{
  uartinit();
}
