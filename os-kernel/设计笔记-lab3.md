# Lab3 设计笔记 · SV39 三级页表与物理内存管理

> 学号：**2024302111127**
> 派生参数：`LAB3_ALLOC_ORDER=2`(中点分配)、`LAB3_GUARD_PAGES=2`、`LAB3_MEMCAP_DELTA=7`
> 配套增量包：`lab3-v2-20260929`

---

## 0. 一句话结论与最重要的取舍

本轮把 lab2 的"bump 只增分配器 + 过渡页表"升级为**真正的 SV39 物理内存管理**：
位图页分配器(带配额)、具备回滚一致性的 mappages、段级权限的 exec 装载(代码 R+X、
数据 R+W)、guard page、sbrk 动态扩展，以及按 ABI 输出的 `dump_pagetable`。

**最重要的两个取舍：**

1. **分配器用位图而非有序链表**（对应说明书阶段一的选型分析）。
   本学号 `LAB3_ALLOC_ORDER=2`(中点)。若用单链表实现升序分配，分配 O(1) 但释放要按地址
   插入链表 → 单次 O(n)，memcap 压力测试一次性回收约 2 千 ×16 ≈ 3.2 万页，退化为 O(n²)，
   QEMU 下数分钟、极易被误判死锁。位图每页 1 bit、128MB 元数据仅 ~4KB(1 页)，释放 O(1)，
   分配检索空闲位，故选位图（详见 §3.1）。

2. **用户代码段严格 R+X（无 W）**（对应 TEST-3a 的硬约束）。
   平铺二进制没有 ELF 段属性，无法天然区分代码/数据。做法是**生成派生链接脚本
   `user/user-data.ld`**（不改原 `user.ld`）在 `.rodata` 后插入 `. = ALIGN(0x1000)` 并导出
   `__data_start`，再经 `tools/gen_userimg.py` 用 `nm` 把 `__data_start`/`end` 两个符号值装进
   内嵌程序表，内核装载时按页边界给 `[0, __data_start)` 赋 R+X、`[__data_start, end)` 赋 R+W。

---

## 1. 模块与文件清单（相对 lab2 的变化）

| 模块 | 文件 | lab3 变化 |
| ---- | ---- | --------- |
| 物理页分配器 | `kernel/vm.c`(kinit/kalloc/kfree) | bump → **位图 + 中点分配 + 配额** |
| 三级页表 | `kernel/vm.c`(walk/mappages/uvm*) | mappages 加**失败回滚**；uvmdealloc 回收空中间页表 |
| 页表观测 | `kernel/vm.c`(dump_pagetable) | **新增**，按 dump_pagetable-ABI.md 输出 |
| 用户装载 | `kernel/exec.c` | 段级权限(R+X/R+W) + guard page + 栈；表项 ABI 扩展 |
| 地址空间增长 | `kernel/sysproc.c`(sys_sbrk) | **新增** SYS_sbrk(12) |
| 系统调用 | `kernel/syscall.c` | 分发表补 `[SYS_sbrk]` |
| 进程 | `kernel/proc.c` | forkret 里首个进程装配完成后自动 dump 一次 |
| 内嵌程序表 | `tools/gen_userimg.py` | **新增**生成器：表项 `{start,end,data_start,mem_end,name}` 连续存放 |
| 派生链接脚本 | `user/user-data.ld` | **新增**：数据区 4KB 对齐 + 导出 `__data_start` |
| 跳板 | `kernel/trampoline.S` | 覆盖增量包 v2 修复(`.section trampsec,"ax"` + `.align 12`) |

---

## 2. 动手前必须明确的五个设计决策（说明书验收重点）

### 2.1 mappages 的回滚一致性（不变式）

我把资源释放职责明确划分为三层，并写死为不变式：

- **mappages 只拥有"本次调用中新建的中间页表页"**，不拥有调用方传入的叶子物理页。
- 失败时（walk 途中 kalloc 中间页表返回 0）：先 `uvmunmap(pagetable, va, (a-va)/PGSIZE, 0)`
  拆除已建的 `[va, a)` 叶子映射（`do_free=0`，**不释放**调用方的物理页），再 `reclaim_empty`
  递归回收本次新建、现已无任何有效映射的中间页表页。返回 -1。
- **调用方传入的叶子物理页，释放职责归调用方**：`uvmalloc` 逐页调用 mappages（每次 1 页），
  失败时 `kfree(当前 mem)` + `uvmdealloc` 回收此前已建页；`exec` 的 `loadseg` 同理
  `kfree(当前页)` + `uvmunmap(do_free=1)`。

为什么这样拆：mappages 不释放叶子物理页，是因为那些页是调用方 `kalloc` 出来的、调用方
才知道哪些需要回收；mappages 若擅自释放会与调用方的记账冲突。而"拆除部分叶子映射"由
mappages 做，是因为只有它清楚本次推进到哪一页失败。

### 2.2 guard page 的权限配置

- guard page **不分配、不映射任何物理页**——它就是地址空间里的"空洞"（无 PTE 或 PTE_V=0）。
- 用户程序非法访问它时，硬件触发 **load/store/instruction page fault**（scause=12/13/15），
  `usertrap` 的 default 分支（非 8、非 devintr）打印诊断后 `setkilled(p)` → `kexit(-1)`，
  即"安全终止当前进程"，而不是内核 panic。
- 内核在 `uvmcopy`(fork) 与 `uvmunmap` 中遇到未映射空洞直接 `continue` 跳过，不判为错误。
- 每进程 `LAB3_GUARD_PAGES=2` 页，夹在"数据+bss"与"用户栈"之间（见 §3.4 布局）。

### 2.3 物理内存配额记账

- 可用上限 = 托管页总数 − `LAB3_MEMCAP_DELTA(=7)`。实现上 kalloc 在 `nfree <= DELTA` 时
  直接返回 0，即**永远保留 DELTA 页作安全余量**（页表页、内核栈等仍可分配到，避免真 OOM panic）。
- 错误码约定：`kalloc` 返回 `0`（NULL）；`mappages`/`uvmalloc`/`loadseg` 返回 `-1`/`0`；
  `sys_sbrk` 对用户返回 `-1`。错误状态**逐层向上传递**：kalloc(0) → mappages(-1) →
  uvmalloc(0) → sys_sbrk(-1)，每一层在返回前完成自己负责的回滚（§2.1 的不变式）。

### 2.4 copyin/copyout 的实现策略

选 **"预先校验页表权限后批量拷贝"**（每页一次 `walkaddr` 翻译 + 校验，整页 `memmove`）：

- 逐页 `PGROUNDDOWN` 后 `walkaddr` 做翻译 + `PTE_U` 校验（walkaddr 内部 `if((*pte & PTE_U)==0) return 0`），
  比"逐字节安全拷贝"少 4KB 次函数调用/翻译，比"纯 memcpy 不校验"安全。
- copyout 额外校验 `PTE_W`，禁止写入只读代码页（TEST-3a 写地址 0 的兜底）。
- 边界安全：每页只拷贝 `min(PGSIZE - offset, 剩余)`，跨页缓冲与页尾不足一页两种情况
  由 `while(len>0)` 循环自然覆盖；遇到缺失映射/缺 U 位返回 -1。

### 2.5 struct proc 的演进

lab2 的 `struct proc` 已含 `pagetable`、`trapframe`、`sz`、`kstack`（提前建了页表），lab3
**未新增字段**，只是把这些字段的**生命周期**补完整：

- 创建（`allocproc`）：`kalloc` 分配 trapframe 页 + `proc_pagetable` 建含 trampoline/trapframe
  的空页表。
- 装载（`kexec`）：`proc_pagetable` 建新页表 → loadseg 填代码/数据/栈 → 提交 `p->pagetable`/`p->sz`
  → `proc_freepagetable(old)` 释放旧空间（**失败则保留旧空间**）。
- 增长（`sys_sbrk`）：`uvmalloc`/`uvmdealloc` 直接改 `p->sz`。
- 复制（`kfork`）：`uvmcopy` 逐页深拷贝（guard 空洞跳过），`np->sz = p->sz`。
- 销毁（`freeproc`）：`proc_freepagetable` = 先 unmap trampoline/trapframe，再 `uvmfree`(叶子+页表页)。

---

## 3. 分阶段设计决策

### 3.1 阶段一：物理页分配器

- **位图 vs 有序链表**：见 §0 取舍 1。位图元数据 1 页（`map_bytes = PGROUNDUP((PHYSTOP-start)/PGSIZE/8)`），
  位图页自身置于空闲区最前、不参与分配。
- **kinit 边界**：`start = PGROUNDUP(end)`（`end` 是链接脚本导出符号，本课程名为 `end` 而非
  xv6 教程的 `end`——这里一致）；`first_pa = start + map_bytes`；托管到 `PHYSTOP`。
  可用上限再扣 `LAB3_MEMCAP_DELTA`。
- **中点分配**（LAB3_ALLOC_ORDER=2）：维护 `lo_free`/`hi_free`（最低/最高空闲位），分配时取
  `(lo+hi)/2` 两侧最近的空闲位；升序/降序分别取 `lo_free`/`hi_free`（`#if` 三分支）。
- **junk 与清零**：kfree 先 `memset(pa,1,PGSIZE)`——释放后若有悬挂指针仍读它，读到的是 0x01
  而非旧数据，便于排查；kalloc `memset(pa,0,PGSIZE)`——数据安全隔离，空页表/空 trapframe/程序
  bss 都依赖，且不让新进程读到上一进程的残留数据。

### 3.2 阶段二：页表遍历与具备回滚的映射

- **三级索引提取**：`PX(level, va) = (va >> (12 + 9*level)) & 0x1FF`（riscv.h 预置宏）。
- **中间 vs 叶子**：PTE 的 R/W/X 全 0 即非叶（指向下一级页表）；任一置位即叶子。
- **walk 返回 PTE 指针**而非物理地址：因为既要读（walkaddr 取 pa）也要写（mappages 置 PTE、
  uvmclear 清 U 位），返回指针让调用方原地改，避免二次翻译。
- **重复映射**：`mappages` 对已有 PTE_V 的地址 `panic("mappages: remap")`——重复映射通常是
  装载/增长逻辑错误，静默覆盖会泄漏原物理页。
- **回滚边界**：见 §2.1。

### 3.3 阶段三：内核页表与分页使能

- **映射清单**：UART0(必需)、PLIC 0x4000000(必需，一并覆盖到 0x10000000)、内核镜像 + 全部可用
  物理内存**恒等映射到 PHYSTOP**（不能只到镜像末尾，否则空闲页/页表页/进程数据页访问缺页）、
  trampoline 同址映射、每进程内核栈 KSTACK(i)。**CLINT 不映射**：本内核时钟走 `rdtime/stimecmp`
  CSR（Sstc 扩展，start.c 已使能），不访问 CLINT 内存映射寄存器，故无需映射。
- **权限**：`kernel.ld` 已导出 `etext`，故可细分 `[KERNBASE, etext)` R+X、`[etext, PHYSTOP)` R+W
  （说明书的"RWX 过渡方案"在本工程因已有 etext 而升级为细分权限）。
- **satp 构成**：`MAKE_SATP(pt) = SATP_SV39 | (pa >> 12)`（MODE=8 + 根页表 PPN）。
- **sfence.vma 时机**：写 satp 前后各一次（flush 旧映射/陈旧 TLB）。单核下只刷本核；多核需
  逐核刷或 IPI，本实验单核，作为思考题留档。

### 3.4 阶段四：用户页表、受控拷贝、sbrk、观测

- **用户地址空间布局**（低→高，与 lab0 快照图对齐）：
  ```
  [0, data_start)        代码+只读数据  R+X+U   (data_start = __data_start, 页对齐)
  [data_start, data_end) 数据+bss       R+W+U   (data_end = PGROUNDUP(end))
  [data_end, stack_start) guard page × LAB3_GUARD_PAGES=2 (未映射空洞)
  [stack_start, sz)      用户栈 1 页    R+W+U   (stack_start = data_end + 2*PGSIZE)
  [sz, ...)              堆(向上增长, sbrk)
  ```
  注意与 xv6 原版差异：**保护页在数据与栈之间**，堆在更高地址，sz 初始即栈顶。
- **代码段禁 W**：三步链路 ① 构建期：派生链接脚本把 `.data` 4KB 对齐 + 导出 `__data_start`；
  ② 传递：`gen_userimg.py` 读 ELF 符号装进程序表；③ 装载：loadseg 按页判区间赋权限。
- **返回用户态不能直接在 C 里写 satp**：切换后当前 C 代码的虚拟映射立即失效，PC 取指不连续；
  必须经 trampoline 页（用户/内核同址映射）走 `userret` 汇编完成切换，C 侧用
  `TRAMPOLINE + (userret - trampoline)` 高地址间接调用（forkret 现有做法）。
- **sbrk**：SYS_sbrk=12，本实验统一 EAGER。n>0 走 `uvmalloc`（OOM 回滚返回 -1），n<0 走
  `uvmdealloc` 收缩，n=0 返回当前边界；返回值统一为**旧边界**（成功）或 -1（失败）。
- **copyin 跨页/页尾/缺 U**：见 §2.4。
- **直接解引用用户指针的清点**：lab2 里 sys_write/read/exec 已全部走 copyin/copyout，本轮
  复核无残留直接解引用。

---

## 4. dump_pagetable 观测

- 格式严格按 `dump_pagetable-ABI.md`：非叶输出 `L2/L1 va=0x.. pa=0x.. perm=----`，叶子输出
  `LEAF va=0x.. pa=0x.. perm=rwxu`（R/W/X/U 四字符，未置位 `-`）。
- **调用点**：首个用户进程(sh)页表装配完成、`userret` 之前，在 `forkret` 里 `dump_pagetable(p->pagetable)`
  自动打印一次；函数进程无关（参数即页表），lab4 回归可对任意进程重放。
- **A/D 位**：属硬件运行时状态位，不在 perm 四位中输出（现场口头说明：由硬件自动置位）。

实测输出（sh 首次装配）：
```
DUMP-PAGETABLE begin
L2 va=0x0 pa=0x... perm=----
L1 va=0x0 pa=0x... perm=----
LEAF va=0x0      pa=0x... perm=r-xu   ← 代码
LEAF va=0x1000   pa=0x... perm=rw-u   ← 数据+bss
LEAF va=0x4000   pa=0x... perm=rw-u   ← 用户栈(guard 页 0x2000/0x3000 不打印)
L2 va=0x3fc0000000 pa=0x... perm=----
L1 va=0x3fffe00000 pa=0x... perm=----
LEAF va=0x3fffffe000 pa=0x... perm=rw-- ← trapframe
LEAF va=0x3ffffff000 pa=0x... perm=r-x- ← trampoline
DUMP-PAGETABLE end
```

---

## 5. 思考题

**① SV39 一次 TLB 未命中访问几次物理内存？硬件靠什么降低开销？**
3 次（L2→L1→L0 逐级读页表），加上最终取数据共 4 次访存。硬件靠 **TLB**（快表缓存 VA→PA）
与**多级页表 walk 缓存/硬件页表遍历器**降低开销，命中 TLB 时无需再访问内存页表。

**② 拆除映射为何先置 PTE 无效、再释放物理页？颠倒会怎样？**
若先 kfree 物理页、PTE 仍 V，另一核/同一核稍后经该 PTE 翻译到已释放的物理页，读到垃圾数据或
撞上复用后的新内容（use-after-free / 悬挂映射）；先清 PTE 再释放，保证释放期间无任何路径能
再经旧映射访问该页。例：进程 A 释放页 P，进程 B 立刻 kalloc 到 P 并写入，若 A 的旧 PTE 还在，
A 的误访问就会读到 B 的数据。

**③ copyin 与 memcpy 的本质区别？恶意程序如何利用 memcpy 攻击？**
copyin 会把用户 VA 经进程页表翻译为物理地址、逐页校验 PTE_V/PTE_U，越界/无映射/缺权限即拒绝；
memcpy 直接按内核视角的地址拷贝。恶意程序可传一个"内核地址"或"未映射/超 sz"的假指针，内核若
直接 memcpy 就会读写内核内存（越权）或踩未映射地址（内核崩溃）。

---

## 6. 自测结果（全部通过）

| 测试 | 方式 | 结果 |
| ---- | ---- | ---- |
| 页表 dump | 启动观察 | 见 §4，perm 逐项正确 |
| lab2 回归 | `inject_uart.py --script storm.txt` | hi / badecall / bufstorm 3 项 EXPECT 全 PASS |
| badaccess | sh 里跑 `badaccess` | TEST-3a PASS(写地址 0 被杀, scause=0xf)、TEST-3b PASS(越 sbrk 读被杀, scause=0xd) |
| memcap | sh 里跑 `memcap` | `TEST-4 PASS: OOM returned error after 2031 chunks` |
| spin | sh 里跑 `spin` | 正常输出 `spin N` |

---

## 7. 已知债务（留待后续 lab）

- sbrk 的 LAZY 语义（缺页再分配）留待 lab5；本实验统一 EAGER。
- 内核代码/数据未做 W^X 细分（无 etext 时本课程基线需 RWX 过渡，本工程已有 etext 故已细分 RX/RW）。
- fork 仍为全量深拷贝，无 COW——lab5 换 refcount + 写时复制。
- 调度仍是"假调度"（时钟不 yield）——lab4 还时间片。
- exec 仍忽略 argv——lab5 完整化。
