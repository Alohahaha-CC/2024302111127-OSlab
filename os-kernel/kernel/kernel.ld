/* lab2 链接脚本(在 lab1 基础上新增 trampsec 页对齐放置 + etext/end 导出)。
 * `.` 是位置计数器(VMA)。QEMU -kernel 按 program header 加载,
 * 加载地址 = 0x80000000(virt RAM 起点)。
 * 新增要点:
 *   - trampoline.S 的 trampsec 段按页对齐放进 .text(其符号 trampoline/uservec/
 *     userret 由 .globl 导出, 内核把它们映射到 TRAMPOLINE 高地址);
 *   - etext 标记内核代码结束(供 kvmmap 区分代码/数据权限);
 *   - end 标记内核镜像结束(bump 页分配器从这里开始)。
 */
OUTPUT_ARCH(riscv)
ENTRY(_entry)

BASE_ADDRESS = 0x80000000;

SECTIONS
{
  . = BASE_ADDRESS;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    *(trampsec)          /* trampoline.S: 页对齐 */
    . = ALIGN(0x1000);
  }

  PROVIDE(etext = .);   /* 内核代码结束(供 kvmmap 区分代码/数据权限) */

  .rodata : {
    *(.rodata .rodata.*)
  }

  .data : {
    *(.data .data.*)
  }

  .bss : {
    *(.bss .bss.*)
    *(.sbss*)
  }

  PROVIDE(end = .);     /* 内核镜像结束, 堆起点 */
}
