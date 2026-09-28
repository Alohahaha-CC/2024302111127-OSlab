# Lab2 设计笔记 · 系统调用机制与控制台中断处理

> 学号：**2024302111127**
> 派生参数：`LAB2_TICK=3`、`LAB2_BUF_SEMANTICS=1`(字符流)、`LAB2_BUF_SIZE=128`

---

## 0. 一句话结论与最重要的设计取舍

本轮把 lab1 只会"自言自语"的内核装上耳朵(键盘输入中断)和嘴(系统调用/shell)：
trampoline 落地 U↔S 切换协议、usertrap 分发 ecall 与设备中断、中断驱动控制台、
最后从内嵌程序表装载并跑起第一个用户程序 `sh`。

**最重要的取舍：我提前建立了最小 SV39 页表，而不是用"satp=0 直映"。**

理由(经反汇编实证，非凭空)：赠送的 `user/sh.c` 里 `static char buf[64]` 是全局变量，
RISC-V `-mcmodel=medany -fno-pie` 对**全局变量用绝对寻址**(`li s0, 0x708` 直接把链接地址
当常数装进寄存器)，只有**字符串字面量**才用 `auipc` 相对寻址。也就是说这些平铺二进制
并不可重定位——它们要求"用户虚拟地址 0"必须映射到"程序装载的物理地址"。纯 Bare(satp=0)
下把程序装到 `end` 之后的某物理地址，一运行 `buf` 的访问就落在无效的低地址 0x708，
`sh` 必死。因此建立三级页表(内核恒等映射 + 用户从 VA 0 起映射)是**跑起 sh 的唯一正确路径**；
这也正是 trampoline.S 交换 satp、TRAPFRAME 取高位 VA 所预设的设计。完整内存管理留给 lab3 细化。

---

## 1. 模块与文件清单

| 模块 | 文件 | 职责 |
|---|---|---|
| 陷入跳板 | `kernel/trampoline.S`(赠送, 未改) | uservec 借 sscratch 存现场 / userret 恢复 |
| 陷入分发 | `kernel/trap.c` | usertrap/usertrapret/kerneltrap/devintr/clockintr |
| 系统调用 | `kernel/syscall.c` `sysproc.c` `sysfile.c` | 表驱动分发 + fork/exit/wait/getpid/read/write/exec |
| 控制台 | `kernel/console.c` | 16550 RX 中断 + 环形缓冲 + 回显 |
| 页表/分配 | `kernel/vm.c` | walk/mappages/uvm*/copy* + bump 页分配器 |
| 进程 | `kernel/proc.c` | proc 表/fork/exit/wait/sleep/wakeup/scheduler |
| 中断控制器 | `kernel/plic.c` | PLIC 使能与 claim/complete |
| 装载 | `kernel/exec.c` | 从 `_uprog_table` 查名装载 |
| 上下文切换 | `kernel/swtch.S` `kernelvec.S` | 内核上下文切换 / 内核态陷入向量 |
| 内嵌程序表 | `kernel/userimg.S`(Makefile 生成) | `.incbin` 用户程序 + `_uprog_table` |

---

## 2. 两张控制流图(照此写代码的精度)

### 图①：一次 write 系统调用的完整往返

```
[U 态] 用户程序 write(1,"hi",2)
   → user/printf.c 的 write() → user/usys.S 桩: li a7,SYS_write; ecall
        (此时 a0=fd a1=指针 a2=len, 都是用户寄存器)
── ecall 硬件: 特权级→S; sepc=ecall 的 PC; pc→stvec ──────────────────────
[跳板] uservec(此刻仍用用户页表, sp 是用户栈)
   csrw sscratch,a0               # 借 sscratch 暂存用户 a0
   li a0,TRAPFRAME                # 用高位 VA 定位 trapframe(用户页表映射)
   sd ra,40(a0) … sd t6,280(a0)   # 31 个通用寄存器按偏移存进 trapframe
   csrr t0,sscratch; sd t0,112(a0)# 补存用户 a0
   ld sp,8(a0)  ld tp,32(a0)      # 内核栈顶 / hartid
   ld t0,16(a0) ld t1,0(a0)       # usertrap 入口 / 内核 satp
   sfence.vma; csrw satp,t1       # 切内核页表
   jalr t0                        # → usertrap()
[S 态] usertrap
   scause==8 → p->trapframe->epc = sepc + 4   # 跳过 ecall, 防原地打转
   intr_on()                      # 现场已存, 开中断
   syscall() → 表驱动取 SYS_write → sys_write → consolewrite → uartputc_sync
[S 态] usertrapret(返回准备)
   intr_off(); w_stvec(uservec VA)            # 下次仍走 uservec
   填 trapframe 头部(kernel_satp/sp/trap/hartid)
   sstatus: SPP=U, SPIE=1;  w_sepc(trapframe->epc)
   return MAKE_SATP(p->pagetable)             # 返回用户 satp 给 a0
[跳板] userret(紧接 jalr 之后的代码, a0=用户 satp)
   fence.i; csrw satp,a0                      # 切回用户页表
   li a0,TRAPFRAME; 恢复 31 个寄存器
   sret                                        # 硬件: 特权级→U; pc=sepc
[U 态] 用户程序拿到 a0=返回值, 从 ecall 的下一条继续
```

### 图②：一次键盘中断的完整路径(字符流语义)

```
用户敲 'h' → 16550 RHR 有数据 + IER_RX_ENABLE
   → UART 拉高中断线 → PLIC 路由 → CPU 外部中断
── 硬件: 若 sie.SEIE=1 且 sstatus.SIE=1, 特权级→S, pc→stvec ────────────
[S 态] (若在用户态) uservec → usertrap → devintr(scause=0x8000...0009)
        (若在内核态) kernelvec → kerneltrap → devintr
   plic_claim()==UART0_IRQ
   → uartintr(): 读 ISR 确认, 循环 uartgetc() 读 RHR
   → consoleintr('h'):
        acquire(&cons.lock)
        consputc('h')                     # 回显(复用 lab1 输出通道)
        cons.buf[cons.e++ % 128] = 'h'    # 入环形缓冲
        [字符流] cons.w = cons.e; wakeup(&cons.r)   # 立即提交并唤醒
        release(&cons.lock)
   plic_complete(UART0_IRQ)
[S 态] 睡在 read(0) 的 sh: 被 wakeup 置 RUNNABLE → 调度器挑中 → consoleread
       醒来重新 acquire(&cons.lock), 见 cons.r != cons.w, 取 'h' copyout 给用户
[U 态] sh 的 gets() 拿到 'h', 继续 read 下一个字符, 直到 '\n'
```

---

## 3. 设计决策逐条回答(规格 §3 抽题点)

### 3.1 trapframe 放哪、多大、每进程一份还是全局？谁在何时切换它？

- **每进程一份**，独占一页(实际 288 字节，但页表按页映射，方便起见整页分配)。
- **放哪**：由 bump 页分配器从内核 `end` 之后分配物理页，映射到该进程用户页表的
  `TRAPFRAME`(0x3FFFE000) 虚拟地址——trampoline.S 用 `li a0,TRAPFRAME` 硬编码这个 VA，
  每个进程用自己的页表把同一 VA 映射到各自的物理 trapframe，天然隔离。
- **谁在何时切换**：切换靠**页表**，而非搬内容。切进程(swtch)时，新进程的页表一装上，
  TRAPFRAME VA 就自动落到新进程的 trapframe；usertrapret() 每次回用户前把
  kernel_satp/kernel_sp/kernel_trap/kernel_hartid 四个头字段填好，供下次 uservec 使用。

### 3.2 输入缓冲的数据结构与锁；"检查-入睡"怎么落地；两种语义差在哪几行？

- **数据结构**：`char buf[128]` + 三个单调递增下标 `r/w/e`，用 `buf[i % 128]` 取模；
  无符号差值 `w - r` 即缓冲内字节数(溢出回绕也正确)，`e` 是编辑下标(为 backspace 预留)。
- **锁**：一把 `struct spinlock`(acquire/release 内部关中断，单核下即互斥)。
- **检查-入睡**：`consoleread` 里 `while (cons.r == cons.w)` 表示空，走
  `sleep_prepare(&cons.r); release(&cons.lock); sleep(); acquire(&cons.lock);`
  ——先登记唤醒通道、**释放锁再睡**、醒来重取锁再查，杜绝"持锁睡眠"死锁。
- **两种语义差在哪几行**：只在 `consoleintr` 的提交条件(见 `console.c`)：
  - 字符流(我的参数=1)：每敲一个字符就 `cons.w = cons.e; wakeup(&cons.r)`；
  - 行缓冲(=0)：只有 `c == '\n'` 或缓冲满时才提交唤醒。
  读取函数 `consoleread` 两种语义**完全一样**(都是逐字节取、遇 `\n` 返回)。

### 3.3 syscall 分发表怎么组织？错误码约定？

- **表驱动**：`static uint64 (*syscalls[])(void)` 用调用号直接索引(designated initializer)，
  lab2 只实现 fork/exit/wait/read/exec/getpid/write 七项，其余槽位为 NULL。
- **错误码**：未知或未实现的调用号统一在 default 分支把 `-1` 写回 `trapframe->a0`，
  绝不 panic(满足 `badecall` 测试)。

### 3.4 fork 怎么复制用户程序镜像？

我建了页表，所以用标准 `uvmcopy`：遍历父进程页表，每个有效页 `kalloc` 一块新物理页、
`memmove` 复制内容、以相同权限映射进子进程页表；子进程 trapframe 全量拷贝、`a0` 置 0(返回 0)。
复制到**新分配的物理页**，记账靠子进程自己的页表(引用各自的物理页)。

### 3.5 用户指针怎么处理？

没有"直接解引用"：统一走 `copyin/copyout/copyinstr`(经 `walkaddr` 逐页翻译+边界检查)。
这笔债(lab3 起还需更细的访问权限检查)已记下。

---

## 4. 思考题(验收问答抽选)

**① ecall 与 sret 硬件各做了什么？为何不硬件自动保存寄存器、切栈？**

- ecall 只做三件事：切到 S 态、`sepc=当前 PC`、`pc→stvec`；**不保存任何通用寄存器、不切栈**。
- sret 只做两件事：降回 U 态、`pc=sepc`。
- 硬件不自动保存/切栈，是因为"保存哪些、存哪、栈切到哪"是**软件 ABI 决策**(要多少现场、
  trapframe 布局、内核栈位置)，硬件代劳会锁死灵活性且代价高；交给 trampoline.S 用几十条
  `sd/ld` 显式完成，注释即规范。

**② 系统调用要对 sepc 做什么？时钟中断要吗？**

- 系统调用：`sepc` 指向 ecall 指令本身，返回前必须 `epc += 4` 跳过它，否则 sret 回来
  又在同一条 ecall 原地打转(对应排查表第一行"反复输出同一串字符")。
- 时钟中断：**不需要**。中断发生时 `sepc` 指向的是被打断的那条用户指令，我们希望中断
  处理完**回到原指令继续执行**，所以不动 sepc。

**③ 行缓冲 vs 字符流，改一处会出什么问题？**

- 差别在 §3.2：只在 `consoleintr` 的**提交/唤醒时机**不同，读取函数相同。
- 只改中断里唤醒条件(改成每字符唤醒)但不改语义宏：行为会与 `LAB2_BUF_SEMANTICS` 声明不符；
  只改读取函数不改进程提交：读者永远等不到数据(行缓冲下没 `\n` 就不提交)。必须两处配套。

---

## 5. 自测结果(全部通过)

| 测试 | 命令/方式 | 结果 |
|---|---|---|
| lab1 回归 banner | 启动观察 | `OSLAB1 sid=… mod97=0xe\n[chk=2275]` 逐字节正确 |
| sh 启动 | `make qemu` | 出现 `sh> ` 提示符 |
| hi | 键入 `hi` | `hi: user program running, pid=2` |
| 非法调用号 | `badecall` | `TEST-1 PASS` |
| spin + 键盘交错 | `spin` 运行时敲键盘 | 回显 `hello` 与 `spin N` 交错不乱 |
| 缓冲边界 | `bufstorm` + `inject_uart.py --script storm.txt` | `BUFSTORM lines=4 bytes=148`，EXPECT 0 失败 |

- `python3 support/inject_uart.py --tree . --script storm.txt` → 三个 EXPECT 全 PASS。
- `-d int` 无未处理异常。

---

## 6. 已知债务(留待后续 lab 偿还)

- bump 页分配器不回收(uvmfree/uvmunmap 的 do_free 实为 no-op)——lab3 换 freelist+refcount；
- 用户程序代码段也映射了 W/X(flat 二进制无节区信息, 未做 W^X 细分)——lab3 补权限；
- exec 对 .bss 用"二进制后多清 2 页"的余量兜底(无 ELF 头可读 bss 大小)——lab3 起有 ELF；
- "假调度"：时钟中断不 yield, 进程只在阻塞/退出让出 CPU——lab4 还时间片；
- sys_exec 忽略 argv(内嵌程序均无参数)——lab5 完整化。
