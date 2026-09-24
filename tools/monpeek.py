#!/usr/bin/env python3
"""monpeek.py — QEMU monitor 一瞥:连接 unix socket,读寄存器/进程现场。

用法:
    # 先以如下参数启动 QEMU(-monitor 挂到 unix socket):
    #   qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
    #     -nographic -monitor unix:/tmp/m.sock,server,nowait
    python3 tools/monpeek.py /tmp/m.sock              # 默认 info registers
    python3 tools/monpeek.py /tmp/m.sock "info cpus"

无 gdb 环境下"看现场寄存器"的替代手段(见《教师版-无gdb替代演示手册》)。
"""
import socket
import sys
import time

KEYS = ("CPU#", "priv", "pc ", "mtvec", "mepc ", "mstatus", "sstatus",
        "stvec", "sepc ", "scause", "stval", "satp ", "mie ", "mip ")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/m.sock"
    cmd = (sys.argv[2] if len(sys.argv) > 2 else "info registers").encode()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    time.sleep(0.4)
    try:
        s.recv(65536)
    except Exception:
        pass
    s.sendall(cmd + b"\n")
    time.sleep(0.8)
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
            if len(chunk) < 65536:
                break
    except Exception:
        pass
    for line in data.decode(errors="ignore").splitlines():
        line = line.strip()
        if not line:
            continue
        if cmd.startswith(b"info registers") and not any(k in line for k in KEYS):
            continue
        print(line)


if __name__ == "__main__":
    main()
