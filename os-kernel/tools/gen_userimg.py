#!/usr/bin/env python3
"""gen_userimg.py — 生成 kernel/userimg.S(内嵌程序表, lab3 扩展 ABI)

每个程序表项(32 字节头 + 变长名字, .balign 8):
    .quad _uprog_<p>_start     ; 程序二进制起始(相对 0 的装载地址)
    .quad _uprog_<p>_end       ; 程序二进制末尾(不含 bss)
    .quad <data_start>         ; 只读区结束/可写区起始(页对齐, __data_start 符号)
    .quad <mem_end>            ; 进程内存总大小(end 符号, 含 bss)
    .asciz "<p>"
    .balign 8
所有表项**连续**存放于 _uprog_table, 之后才是各程序的 .incbin 二进制,
以 {0,0,0,0} 结束。find_uprog 按 32 + align8(strlen+1) 步进即可正确定位。

用法(在 os-kernel 目录):
    python3 tools/gen_userimg.py sh hi spin ... > kernel/userimg.S
"""
import subprocess
import sys


def sym_addr(elf, name):
    """从 ELF 取符号 <name> 的地址; 找不到返回 0。"""
    try:
        out = subprocess.check_output(
            ["riscv64-unknown-elf-nm", elf]).decode("utf-8", "ignore")
    except Exception:
        return 0
    for line in out.splitlines():
        parts = line.split()
        # nm 输出形如: 0000000000001000 A __data_start
        if len(parts) >= 3 and parts[-1] == name:
            try:
                return int(parts[0], 16)
            except ValueError:
                return 0
    return 0


def main():
    progs = sys.argv[1:]
    lines = []

    # ---- 表头: 所有表项连续 ----
    lines.append(".section .rodata")
    lines.append(".global _uprog_table")
    lines.append("_uprog_table:")
    for p in progs:
        elf = "user-flat/%s.elf" % p
        data_start = sym_addr(elf, "__data_start")
        mem_end = sym_addr(elf, "end")
        lines.append(" .quad _uprog_%s_start" % p)
        lines.append(" .quad _uprog_%s_end" % p)
        lines.append(" .quad 0x%x" % data_start)
        lines.append(" .quad 0x%x" % mem_end)
        lines.append(' .asciz "%s"' % p)
        lines.append(" .balign 8")

    # ---- 表尾(与表项头等长: 4 个 quad) ----
    lines.append(" .quad 0")
    lines.append(" .quad 0")
    lines.append(" .quad 0")
    lines.append(" .quad 0")

    # ---- 各程序二进制(.incbin), 与表项分离存放 ----
    for p in progs:
        lines.append(" .global _uprog_%s_start" % p)
        lines.append("_uprog_%s_start:" % p)
        lines.append(' .incbin "user-flat/%s.bin"' % p)
        lines.append(" .global _uprog_%s_end" % p)
        lines.append("_uprog_%s_end:" % p)
        lines.append(" .balign 8")

    sys.stdout.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
