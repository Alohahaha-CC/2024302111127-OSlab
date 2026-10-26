/* 派生链接脚本(lab3, 从 user.ld 派生 —— 原 user.ld 不修改)
 *
 * 差异: 在 .rodata 之后插入 `. = ALIGN(0x1000)` 并导出 __data_start,
 * 使可写数据区(.data/.bss)从全新的 4KB 页边界开始。这样内核才能以页粒度
 * 隔离"只读代码+只读数据(R+X)"与"可写数据+bss(R+W)":
 *   - [0, __data_start)   → 代码 + 只读数据(字符串字面量等)
 *   - [__data_start, end) → 可写数据 + bss
 * 若不加对齐, 当程序小于 4KB 时代码与数据会落在同一物理页, 页粒度的
 * R+X / R+W 权限控制无法隔离(坏测试 badaccess 的 TEST-3a 正是校验写代码段被杀)。
 *
 * __data_start 与 end 两个符号由 tools/gen_userimg.py 读取, 装入内嵌程序表。
 */
OUTPUT_ARCH(riscv)
ENTRY(main)

SECTIONS
{
  . = 0x0;

  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }

  . = ALIGN(0x1000);
  __data_start = .;

  .data : { *(.data .data.*) }
  .bss : {
    *(.bss .bss.*)
    *(.sbss*)
  }

  end = .;
}
