# Lab1 设计笔记 · 启动与串口输出

> 学号：**2024302111127**
> 派生参数：协议 **2**（sid%3==2）、mod97 = **0xe**、内核栈 **12KB**、节流周期 **23** 字节。

---

## 0. 我的个性化参数推导

| 参数        | 公式                     | 计算               | 结果                               |
| ----------- | ------------------------ | ------------------ | ---------------------------------- |
| Banner 协议 | `sid % 3`              | 2024302111127 % 3  | **2**（整行 + ASCII 校验和） |
| mod97       | `sid % 97`             | 2024302111127 % 97 | **14 = 0xe**                 |
| 内核栈      | `4KB × (1 + sid % 3)` | 4 × 3             | **12 KB**                    |
| 节流周期    | `16 + sid % 16`        | 16 + 7             | **23 字节**                  |

banner 精确文本（协议 2，与 `expect_banner.txt` 逐字节一致）：

```
OSLAB1 sid=2024302111127 mod97=0xe
[chk=2275]
```

校验和算法（自定，自洽）：对整行正文 `OSLAB1 sid=2024302111127 mod97=0xe`
（不含末尾换行、不含 `[chk=]` 本身）逐字节求 ASCII 码之和 = **2275**。

---

## 1. 思考题

### Q1. 上电后 PC=0x1000，`-bios none` 意味着什么？内核为何必须链接到 0x80000000 而非 0x0？

- **PC=0x1000 的来源**：QEMU `-machine virt` 把机器第一条指令放在地址 `0x1000`，
  那是 QEMU 内置的一段 **boot ROM**（`hw/riscv/virt.c` 里描述的固件），负责搬运内核并跳转。
- **`-bios none` 的含义**：**禁用这段 boot ROM**。正常流程下 ROM 会把 `-kernel` 指定的镜像
  加载到 RAM 并跳过去；`-bios none` 后不再有 ROM 做这件事，改为 **QEMU 直接把内核 ELF
  按 program header 装载进 RAM，并让每个 hart 从 ELF 入口（`_entry`）开始执行**。
  也就是说：`-bios none` 意味着"从复位向量 0x1000 到内核入口"这一段没有固件，内核自己就是
  第一段被执行的代码。
- **为什么链接到 0x80000000**：`virt` 机器的 **RAM 起点物理地址就是 0x80000000**
  （见 `memlayout.h` 的注释：`0x80000000 -- qemu's boot ROM loads the kernel here`）。
  链接脚本 `kernel.ld` 里 `BASE_ADDRESS = 0x80000000` 使得代码里所有符号的 VMA 都落在 RAM
  内。若链接到 0x0：QEMU 会把镜像载入 0x0，但 0x0 一带在 virt 里是 ROM/保留区而非 RAM，
  且 `mret`/取指会与 `-bios none` 语义冲突，系统无输出或直接 fault。链接地址必须等于
  加载地址（QEMU `-kernel` 按 program header 加载），二者在 `kernel.ld` 里由 `BASE_ADDRESS`
  统一保证。

### Q2. entry.S 每条指令的存在理由

见 `kernel/entry.S`，逐条对应：

1. **第一步关中断** `csrci mstatus, 8`（清 mstatus.MIE/bit3）：
   此刻 `mtvec` 还是默认值 0，栈也还没备好；一旦 MIE 置位且来了任何中断，CPU 会跳进
   **地址 0** 直接跑飞。所以必须先关中断，再动栈和 mtvec，这是"防御性复位"原则。
2. **初始内核栈**：分配在 `.bss` 的 `bootstack[LAB1_STACK_KB*1024]`（`start.c`），
   大小 **12KB**（个性化，来自 `course_sid.h` 的 `LAB1_STACK_KB`）。entry.S 里
   `la sp, bootstack; li t0, LAB1_STACK_KB*1024; add sp, sp, t0` 把 sp 设到**栈顶**
   （RISC-V 栈向下生长）。为什么 12KB：C 函数调用链（`start→main→printf→…`）与
   `printf/snprintf` 的栈上缓冲区需要空间，4KB 是上游默认，课程按学号放大到 12KB 留余量。
   **sp 初值与哪个文件强一致**：`entry.S` 的 `LAB1_STACK_KB*1024` 与 `start.c` 里
   `bootstack[LAB1_STACK_KB*1024]` 二者都取自 `course_sid.h`，若不一致 sp 会越界踩内存。
3. **从核 park**：`mhartid != 0` 的核 `wfi` 自旋。lab1 只初始化 hart0 的单核环境；
   从核没有自己的控制台/时钟/进程上下文，若也跑进 C 会与 hart0 争抢共享状态（节流计数、
   UART），产生不可预测行为。所以从核必须驻留，等后续实验再唤醒。
4. **mtvec 对齐要求**：RISC-V 规定 mtvec **低 2 位是模式标志**（Direct=00，Vectored=01），
   因此写入的地址必须 **4 字节对齐**；不满足时硬件会**静默丢弃整次写入**（异常仍跳地址 0）。
   所以 `mtrap` 标号前加 `.balign 4`（对应附录 B 第 1 条）。

### Q3. 进入 S 态前必须完成的配置清单

对应 `start.c` 的 `start()`，逐项：

| 项          | 代码                                             | 作用                                               |
| ----------- | ------------------------------------------------ | -------------------------------------------------- |
| mstatus.MIE | `x &= ~(1L<<3)`                                | 确保 mret 后不被中断打断（entry.S 已清，这里兜底） |
| mstatus.MPP | `x \|= MSTATUS_MPP_S`                           | 让 mret 降权到**S 态**                       |
| mepc 目标   | `w_mepc((uint64)main)`                         | mret 的返回地址 =`main`（S 态第一条指令）        |
| 中断委托    | `w_medeleg(0xffff); w_mideleg(0xffff)`         | 把异常/中断委托给 S 态处理                         |
| PMP         | `w_pmpaddr0(0x3fffffffffffff); w_pmpcfg0(0xf)` | **必做**（见下）                             |

- **PMP 两个字段涵义**：
  - `pmpaddr0 = 0x3fffffffffffff`：PMP 地址寄存器。配合 `pmpcfg0.A=NAPOT(3)`，
    这个值表示"覆盖**全部**物理地址空间"的 NAPOT 区域（`0x3f…f` 是 NAPOT 编码里
    "g=2、全 1" 的边界）。
  - `pmpcfg0 = 0xf`：低 8 位配置第一个 PMP 项——`A=11(NAPOT)`、`X=1`、`W=1`、`R=1`、
    `L=0`（不锁定，允许 M 态后续改写）。即"对整个物理空间开放 R/W/X"。
- 若缺失 PMP：新版 QEMU 要求 S/U 态取指必须落在 PMP 授权区内；否则 `mret` 降入 S 态的
  **第一条取指**就触发 instruction access fault（`-d int` 首行 cause=1），全程无输出。

### Q4. satp 选 Bare 还是恒等映射？

**选 Bare 模式（`w_satp(0)`）**。论证：

1. lab1 全程只用**物理地址**：代码链接/加载到 0x80000000，取指、访存、MMIO（UART 0x10000000）
   都用物理地址，没有"需要把虚拟地址翻译到不同物理地址"的需求。
2. Bare 模式最简单、不引入状态：恒等映射需要先建一张页表（SV39 三级页表），还要处理
   0x80000000 的巨型页映射、`sfence.vma` 等，属于 lab3 的职责，提前做只会增加出错面。
3. 风险点（Bare 下唯一约束）是 PMP：Bare 模式下没有页表保护，S/U 态访存边界完全由 PMP
   兜底，所以 **Q3 的 PMP 配置与 Q4 的 Bare 选择是配套决策**——Bare + PMP 全开 = 最小可用。

### Q5. UART 16550 轮询输出协议

见 `kernel/console.c` 的 `uartputc_sync`：

- **涉及寄存器**（mmio，基址 `UART0=0x10000000`）：
  - **THR**（发送保持寄存器，偏移 0）：写入要发送的字符；
  - **LSR**（线状态寄存器，偏移 5）：读状态。
- **关键状态标志位**：`LSR` 的 **bit5 = THRE（Transmit Holding Empty，发送保持寄存器空）**。
- **写字符的条件**：**只有当 THRE=1（即 `ReadReg(LSR) & LSR_TX_IDLE != 0`）时才能写 THR**；
  否则数据会覆盖上一个尚未发出的字节。实现为忙等循环：

```c
while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)   // 等 THRE=1
  ;
WriteReg(THR, c);                            // 写字符
```

- 本实验启动阶段**不设超时重试**：无时钟中断，忙等是唯一可靠行为（附录 A 好版规范）。

---

## 2. 启动时序图（上电 → main 第一行）

```
PC=0x1000 (QEMU boot ROM)  ──( -bios none 跳过 ROM, QEMU 直接按 ELF 装载并跳转 )──▶
                                                                   
_entry @0x80000000  [M 态]                                         
  csrci mstatus,8         关中断(MIE=0)              ┐
  la sp,bootstack+12KB    内核栈就绪(sp=栈顶)          │  "先关中断,
  csrr mhartid; bnez park 从核→wfi 自旋               │   再动栈/mtvec"
  la mtvec;  csrw mtvec   设机器态陷阱向量(.balign 4)  ┘
  call start               ────────────────────────────▶ start() [M 态]
                                                                   
start() [M 态]                                                     
  mstatus.MPP=S, MIE=0    mret 后降权 S、不中断                      
  mepc = main              mret 目标 = main                         
  satp = 0                 Bare 模式(无地址翻译)                    
  medeleg/mideleg=0xffff   异常/中断委托给 S 态                      
  pmpaddr0/cfg0            PMP 授权(全物理空间 R/W/X)   ← 必做, 否则
  tp = mhartid             记 hartid(多核钩子)            取指 fault   
  mret  ──────────────────────────────────────────────▶ main() [S 态]
                                                                   
main() [S 态]                                                      
  consoleinit()            UART 16550 初始化(关中断+8N1+FIFO)        
  banner(协议2)            打印 OSLAB1 … + [chk=2275]               
  printf 边界自检           0 / 负数 / 最大整数 / 空串 / 超长串       
  for(;;) wfi              停机驻留
```

特权级轨迹：**M 态（entry.S + start）→ mret → S 态（main 起，直至停机）**。
寄存器关键变换：`mstatus.MPP: U→S`、`mepc: 0→main`、`satp: 0(保持 Bare)`、
`sp: 0→bootstack+0x3000`、`tp: 0→mhartid`。

---

## 3. 内核内存空间布局图

```
物理地址              内容                              来源
─────────────────────────────────────────────────────────────────
0x00001000            QEMU boot ROM                     (virt 机器)
0x02000000            CLINT(核内定时/软中断)              memlayout.h
0x0C000000            PLIC(平台级中断控制器)              memlayout.h
0x10000000            UART0 ← 串口基址(本次输出目标)      memlayout.h
0x10001000            VIRTIO0 磁盘                        memlayout.h
─────────────────────────────────────────────────────────────────
0x80000000  ┌─────── kernel.ld: BASE_ADDRESS ─────────────┐
            │  .text   ← entry.S 的 _entry 就在最前         │
            │  .rodata 只读数据(字符串常量)                 │
            │  .data   已初始化全局变量                      │
            │  .bss    未初始化全局(含 bootstack[12KB])      │
            │            └─ bootstack: sp 初值=栈顶 0x3000  │
            │  kernel_end ← lab3 起 kalloc 从这里分配物理页  │
            └───────────────────────────────────────────────┘
0x88000000            PHYSTOP(KERNBASE + 128MB, RAM 尽头)  memlayout.h
```

要点：

- **链接地址 = 加载地址 = 0x80000000**，由 `kernel.ld` 的 `BASE_ADDRESS` 统一（Q1）。
- 我的内核栈 `bootstack[12KB]` 在 `.bss`，sp 初值 = `bootstack + 0x3000`（栈顶，向下生长）。
- `0x10000000` 是**设备 MMIO**，不在 RAM 里，通过 `volatile` 指针直接读写（UART 寄存器）。

---

## 4. 分层与协议设计（附录 A 规范）

```
uartputc_sync(c)  单字节硬件发射: 轮询 LSR bit5(THRE) 后写 THR。不可重入, 仅单核。
      ▲
putchar(c)        上层单字符: 计数发射字节, 每满 (16+COURSE_SID%16)=23 字节
      ▲           执行一次 nop 空转(节流注入, 严禁硬编码 23)。
      │
printf / snprintf 格式化层: 负号/无前导零十六进制等规范; snprintf 供 banner 校验和自洽。
      ▲
main()            banner 层: 拼正文 → 求 ASCII 校验和 → 输出 [chk=...]。
```

### printf 规范（§7 预立）

- `%d`/`%ld`：有符号十进制，负数带 `-` 前缀（内部用 `long long` 取绝对值，`INT_MIN` 也不溢出）；
- `%x`/`%lx`：十六进制**小写、无前导零、不带 `0x` 前缀**（`0` → `0`，`14` → `e`）；
- `%u`/`%lu`、`%p`（`0x`+16 位）、`%c`、`%s`、`%%`。

---

## 5. 自检结果

- `make` 编译通过，`_entry` 链接警告已消除（仅剩无害的 RWX 段提示）；
- `python3 check_expect.py 2024302111127` → **`[ok] 形状校验通过(协议2)`**；
- QEMU 串口输出前 45 字节与 `expect_banner.txt` **逐字节一致**（`cmp` 通过）；
- `-d int` 日志 **0 条异常**（async:0 = 0），无 instruction access fault；
- printf 边界：`0`、`-1/-12345/-2147483647`、`9223372036854775807`、空串、超长串均正确输出。
