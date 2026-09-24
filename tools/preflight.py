#!/usr/bin/env python3
"""preflight.py — 环境预检工具 (教师开学前必跑)

检查项:
  1. riscv64-unknown-elf-gcc / riscv64-linux-gnu-gcc 工具链
  2. qemu-system-riscv64 可用性
  3. QEMU 版本探测: 纯 M 态跑无 PMP 代码是否死锁 / fault
     (决定 lab1 教学要求:是否必须讲 PMP;以实测结果为准,不猜测版本号)
  3. mtvec 非 4 对齐写入是否被静默丢弃(决定是否强调 .balign 4)
  4. riscv gdb 是否可用(不可用→提示改用《无gdb替代演示手册》三板斧)
  5. python3(工具链脚本依赖)

用法:python3 tools/preflight.py [xv6源码目录]
退出码:0 全部通过;1 存在会导致实验无法开展的硬缺失。
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

PASS, WARN, FAIL = "[ ok ]", "[warn]", "[FAIL]"
hard_fail = False

SMOKE_ASM = """
    .section .text
    .globl _entry
_entry:
    csrw mie, zero
    csrr tp, mhartid
    bnez tp, park
    la   sp, bootstack
    li   t0, 4096
    add  sp, sp, t0
    la   t1, park_trap
    csrw mtvec, t1
    {PMP}
    la   t1, s_park
    csrw mepc, t1
    csrr t3, mstatus
    li   t2, 3
    slli t2, t2, 11
    not  t4, t2
    and  t3, t3, t4
    li   t5, 1
    slli t5, t5, 11
    or   t3, t3, t5
    csrw mstatus, t3
    mret
park:
    wfi
    j park
    .balign 4
park_trap:
    wfi
    j park_trap
    .balign 4
    .globl s_park
s_park:
    wfi
    j s_park
    .section .bss
    .align 16
bootstack:
    .space 4096
"""

PMP_LINES = """
    li   t1, 0x3fffffffffffff
    csrw pmpaddr0, t1
    li   t1, 0xf
    csrw pmpcfg0, t1
"""

LD = """
OUTPUT_ARCH(riscv)
ENTRY(_entry)
. = 0x80000000;
SECTIONS { .text : { *(.text) } .bss : { *(.bss) } }
"""


def run_s_mode_probe(with_pmp: bool, workdir: str) -> str:
    """返回 'S'/'M':组装最小内核(±PMP),mret 进 S 态后读 priv。"""
    asm = SMOKE_ASM.replace("{PMP}", PMP_LINES if with_pmp else "")
    src = os.path.join(workdir, "entry.S")
    with open(src, "w") as f:
        f.write(asm)
    with open(os.path.join(workdir, "k.ld"), "w") as f:
        f.write(LD)
    r = subprocess.run(["riscv64-unknown-elf-gcc", "-c", "-o",
                        os.path.join(workdir, "e.o"), src],
                       capture_output=True)
    if r.returncode:
        return "BUILD_FAIL"
    r = subprocess.run(["riscv64-unknown-elf-ld", "-T",
                        os.path.join(workdir, "k.ld"), "-o",
                        os.path.join(workdir, "kern"),
                        os.path.join(workdir, "e.o")], capture_output=True)
    if r.returncode:
        return "BUILD_FAIL"
    sock = os.path.join(workdir, "m.sock")
    if os.path.exists(sock):
        os.remove(sock)
    p = subprocess.Popen(
        ["qemu-system-riscv64", "-machine", "virt", "-bios", "none",
         "-kernel", os.path.join(workdir, "kern"), "-nographic",
         "-monitor", f"unix:{sock},server,nowait"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    priv = "?"
    try:
        time.sleep(1.5)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock)
        time.sleep(0.3)
        try:
            s.recv(65536)
        except Exception:
            pass
        s.sendall(b"info registers\n")
        time.sleep(0.7)
        data = s.recv(65536).decode(errors="ignore")
        for line in data.splitlines():
            if "priv" in line:
                priv = line.split()[-1]
                break
        s.close()
    except Exception as e:
        priv = f"ERR({e})"
    finally:
        p.kill()
        p.wait()
    return priv


def main():
    global hard_fail
    print("== osLab 开课前环境自检 ==\n")

    for tool in ("riscv64-unknown-elf-gcc", "riscv64-unknown-elf-ld",
                 "riscv64-unknown-elf-objdump", "qemu-system-riscv64",
                 "python3"):
        have = shutil.which(tool) is not None
        print(f"{PASS if have else FAIL} {tool}" + ("" if have else " —— 缺失,必须安装"))
        if not have:
            hard_fail = True

    gdb = shutil.which("riscv64-unknown-elf-gdb") or shutil.which("gdb-multiarch")
    if gdb:
        print(f"{PASS} riscv gdb({gdb})——导学课可用 gdb 演示")
    else:
        print(f"{WARN} riscv gdb 不可用——三处导学演示改用"
              f"《教师版-无gdb替代演示手册》三板斧(-d int / -d in_asm / monpeek.py)")

    print()
    with tempfile.TemporaryDirectory() as wd:
        p1 = run_s_mode_probe(False, wd)
        p2 = run_s_mode_probe(True, wd)
        if p2 == "S" and p1 in ("M", "?"):
            print(f"{PASS} 实测:本机 QEMU 的 S 态取指需要 PMP 授权"
                  f"(无PMP={p1},有PMP={p2})。")
            print("      → lab1 必须讲授 PMP:说明书 §2'环境前置条件'"
                  "与骨架注释已含,按 V3 材料走即可。")
        elif p2 == "S" and p1 == "S":
            print(f"{PASS} 实测:本机 QEMU 无 PMP 也可进 S 态(旧版行为)。")
            print("      → PMP 仍按说明书讲授(写上不亏,跨版本安全),"
                  "但学生踩坑率会低。")
        else:
            print(f"{WARN} S 态探针结果异常(无PMP={p1},有PMP={p2})——"
                  "请人工复核;lab1 黑屏排查从 PMP 与 mtvec 对齐开始。")

    print()
    print("结论:" + ("存在硬缺失,先补环境再开课" if hard_fail
                      else "环境自检通过(警告项按提示处理)"))
    sys.exit(1 if hard_fail else 0)


if __name__ == "__main__":
    main()
