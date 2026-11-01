# Lab4 设计笔记 · 进程上下文切换与调度策略

> 学号：**2024302111127**
> 派生参数：`LAB4_POLICY_ID=7`（步长调度 Stride，默认票数按进程名 `hi=1/spin=2/sh=4`）、
> `LAB2_TICK=3`（每个时间片包含 3 次时钟中断）
> 配套增量包：`lab4-v2-20261009`

---

## 0. 一句话结论与最重要的取舍

本轮把 lab2/lab3 的"假调度"（只在 wait / 控制台读取处主动让出，时钟中断不抢 CPU）升级为
**真正的多任务抢占式调度**：标准 `sleep(chan, lk)` / `wakeup(chan)` 消除丢失唤醒、时钟中断驱动
`yield()` 抢占、`scheduler()` 主循环按**步长调度（Stride）**选出就绪进程，并补齐 `pause/schedstat/
time_rdtime/setickets` 四个观测系统调用。

**最重要的两个取舍：**

1. **调度策略选 Stride（步长）而非彩票/MLFQ**（由 `LAB4_POLICY_ID=7` 决定）。
   本学号要求"默认票数按进程名分配（hi=1/spin=2/sh=4）"，Stride 恰好把票数映射为
   `stride = STRIDE_BIG / tickets`：票数越多 stride 越小、pass 增长越慢、被选中越频繁。
   由于 pass 每次分配单调递增、确定性公平，天然满足 stride_test 的"有界公平"判据
   （每个采样窗口两进程都获得 CPU），这是彩票调度（随机）和纯优先级（可能饥饿）都做不到的。

2. **消除丢失唤醒，靠的是"先 acquire(&p->lock) 再 release(lk)"的锁序**（不是靠增加标志位）。
   这是 lab4 最核心的一处：把"检查条件（在条件锁 lk 下）"与"置 SLEEPING（在进程锁 p->lock 下）"
   通过锁的交接串成同一个原子临界区。详见 §2.4 的交错序列推演。

---

## 1. 模块与文件清单（相对 lab3 的变化）

| 模块 | 文件 | lab4 变化 |
| ---- | ---- | --------- |
| 进程结构 | `kernel/proc.h` | `struct proc` 增 `cpu_ticks/last_slice_ticks/tickets/stride/pass`；增 `STRIDE_BIG` |
| 休眠/唤醒 | `kernel/proc.c` | 删 `sleep_prepare`+`sleep()`，改标准 `sleep(chan, lk)`；`wakeup` 跳过自身 |
| 调度器 | `kernel/proc.c` | `scheduler()` 由固定顺序轮转改 **Stride 两步法**（选最小 pass） |
| 调度参数 | `kernel/proc.c` | `allocproc` 初始化；`kfork` 继承；新增 `default_tickets(name)` |
| 用户装载 | `kernel/exec.c` | exec 后按进程名重设默认票数/stride |
| 陷入 | `kernel/trap.c` | usertrap 时钟分支统计 `cpu_ticks/last_slice_ticks`，满 `LAB2_TICK` 调 `yield()` |
| 系统调用 | `kernel/sysproc.c` | 新增 `sys_pause/schedstat/time_rdtime/setickets/kill` |
| 分发 | `kernel/syscall.c` | 分发表补 `SYS_pause/SYS_kill/SYS_schedstat/SYS_time_rdtime/SYS_setickets` |
| 控制台 | `kernel/console.c` | `consoleread` 改用 `sleep(&cons.r, &cons.lock)` |
| 接口 | `kernel/syscall.h`、`user/user.h`、`user/usys.pl` | 覆盖增量包版本（新增 23/24/25） |
| 测试 | `Makefile`、`user/{common,bench,lottery_test,stride_test,alt}.c` | 接入课程测试 + 自写 `alt` 抢占验证 |

---

## 2. 动手前必须明确的四个设计决策（说明书验收重点）

### 2.1 策略元数据：扩展在 `struct proc`，不用独立调度表

我把 Stride 需要的 `tickets / stride / pass` 以及观测用的 `cpu_ticks / last_slice_ticks`
**直接放进 `struct proc`**，不另建调度表。理由：

- 进程数少（`NPROC=64`），字段与进程生命周期一致（fork 继承、exec 重设、exit 释放），放一起最直观；
- 锁粒度自然复用 `p->lock`——读 `pass` 时就是 scheduler 已经持锁的那个临界区，无需再引入
  "调度表锁 vs 进程锁"的锁序问题；
- 本实验单核，无需为"调度元数据与进程状态分离"去处理跨表一致性的额外复杂度。

（这是本实验明确考核点：元数据**组织形式**。结论是"扩展 proc 而非独立表"，理由如上。）

### 2.2 就绪队列 / 时间片 / 权重的锁粒度

本实现**不维护显式就绪队列**，而是沿用 xv6 的"扫描 `proc[NPROC]` 选就绪进程"：

- `pass / tickets / stride` 只被 `p->lock` 保护。scheduler 扫描时逐进程 `acquire(&p->lock)`
  读 `q->pass`，用**交接模式**始终持有当前最优候选的锁：
  `if (p == 0 || q->pass < p->pass) { release(p); p = q; continue; }`——找到更小的就释放旧的、
  保留新的，扫描结束 `p` 已持锁，可直接改状态并 `swtch`。
- `cpu_ticks / last_slice_ticks` 也由 `p->lock` 保护：`usertrap` 里自增时当前进程正在运行、
  持不持锁都安全；`sys_schedstat` 查询时 `acquire(&p->lock)` 读取，释放锁后再 `copyout`。

### 2.3 时间片与 `LAB2_TICK`、抢占点位置

- **口径**：一次 tick = 一次时钟中断（`clockintr` 里 `stimecmp = r_time() + 1000000`，约 0.1s）。
  `LAB2_TICK=3` 解释为"每个时间片包含 3 次中断"，即时间片长度 = 3 tick。
- **抢占点只在 `usertrap()` 的时钟分支**：`p->last_slice_ticks++` 后，若
  `>= LAB2_TICK` 就 `yield()`。不在 `kerneltrap()` 抢占——内核态抢占还需额外保存/恢复
  `sepc`/`sstatus`，本实验只需用户态抢占即可满足要求，故不做。
- **为什么先统计再判断**：`last_slice_ticks` 在 scheduler 分发时清零，在 usertrap 累加，
  因此它的语义就是"本次连续运行至今的 tick 数"，满 3 即让出，形成固定 3-tick 时间片。

### 2.4 消除丢失唤醒：`sleep(chan, lk)` 的锁序（最核心）

标准实现与锁序：

```c
void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();
  acquire(&p->lock);   // ① 调用者已持 lk, noff 1→2
  release(lk);         // ② noff 2→1, 只剩 p->lock
  p->chan = chan;
  p->state = SLEEPING; // ③ 置休眠(此时已持 p->lock)
  sched();             // ④ noff==1, 满足 sched 不变式, 切走
  p->chan = 0;
  release(&p->lock);
  acquire(lk);         // ⑤ 醒来重新拿回条件锁
}
```

**为什么这样没有丢失唤醒窗口**：`wakeup(chan)` 遍历时也必须 `acquire(&p->lock)` 才能看/改
`p->state`。sleep 在 `release(lk)` **之前**已经 `acquire(&p->lock)`，而"置 SLEEPING"发生在
`p->lock` 保护下。于是唤醒方要么在 sleep 拿 `p->lock` 前就完成 wakeup（此时条件已在 lk 下
被 sleep 检查过，不会漏），要么被挡在 `p->lock` 上直到 sleep 置完 SLEEPING 并 `sched()` 切走、
由 scheduler 释放 `p->lock`——此时 wakeup 一定能看到 SLEEPING 状态。两条路径都不会漏唤醒。

**错误交错序列（若先 release(lk) 再 acquire(&p->lock)）**：
1. 进程 A 持 `wait_lock` 检查子进程，无 ZOMBIE，决定休眠；
2. A `release(&wait_lock)`（尚未拿 `p->lock`，也尚未置 SLEEPING）；
3. 子进程 B 退出：`acquire(&wait_lock)` → `wakeup(A)` 扫描时 A 还是 RUNNING，跳过；
4. A `acquire(&p->lock)`、置 SLEEPING、`sched()` 睡去——**永远无人唤醒**。

**kill 为何不能破坏 SLEEPING 但未满足条件的进程**：`kill` 只置 `killed` 标志并把 SLEEPING 的
目标改为 RUNNABLE 让它醒来**自行**检查；不能直接释放其资源，否则一个正被别处引用（如内核栈、
页表）的进程会被破坏。目标进程醒后在返回用户态前检查 `killed` 再 `exit`，保证退出发生在
安全的检查点。

### 2.5 `pass / tickets` 的初始化与继承（Stride 公平性）

- `allocproc`：`tickets=1, stride=STRIDE_BIG, pass=0, cpu_ticks=last_slice_ticks=0`。
- `kfork`：子进程**继承**父进程的 `tickets/stride/pass`（避免新进程 pass=0 而长期独占 CPU）。
- `kexec`：按进程名 `default_tickets(name)` 重设 `tickets` 与 `stride`，但**保留 pass**
  （exec 只是换程序映像，仍是同一调度实体）。
- `setickets(n)`：覆盖默认票数并重算 `stride = STRIDE_BIG / n`（课程测试据此设票）。

---

## 3. 分阶段设计决策

### 3.1 阶段一：锁与上下文结构（lab2/lab3 已基本具备）

本工程 lab2 起就已是"每进程一把 `p->lock` + 全局 `wait_lock` + `struct cpu`"，`sched()` 的
四条不变式断言（`holding(&p->lock)`、`noff==1`、`state!=RUNNING`、`intr_get()==0`）也早已就位，
故阶段一几乎无需重构，只需把旧的 `sleep_prepare/sleep` 替换为 `sleep(chan, lk)`（见 §2.4），
并把 `consoleread`、`kwait` 的调用点改成新签名。

### 3.2 阶段二：调度器主循环与时钟抢占

- 主循环每轮先 `intr_on()`（否则所有进程都睡时，关闭中断就无法收唤醒，系统停摆），再扫描选
  就绪进程，无就绪进程则 `wfi` 等中断。
- 选中后：`state=RUNNING → c->proc=p → last_slice_ticks=0 → pass+=stride → swtch`；
  `swtch` 返回意味着该进程已让出（时间片/休眠/退出），调度器清 `c->proc` 并 `release(&p->lock)`。
- 抢占在 `usertrap` 时钟分支，满 `LAB2_TICK` 才 `yield()`（不是每 tick 都让出）。

### 3.3 阶段三：休眠与唤醒、pause / kill

- `pause(n)` 基于全局 `ticks` + `tickslock`：记起始值，循环 `sleep(&ticks, &tickslock)` 直到
  经过 n 个 tick（`clockintr` 里 `ticks++` + `wakeup(&ticks)` 驱动）。
- `kill(pid)` 惰性置位：只设 `killed` 并把 SLEEPING 目标改 RUNNABLE，目标回用户态前自查并 exit。

### 3.4 阶段四：观测接口 + Stride 策略

- `schedstat(pid, &cpu, &slice)`：`pid==-1` 查自身，否则查进程表；持 `p->lock` 读两个计数，
  释放锁后 `copyout` 写回（copyout 访问用户页表，不宜持锁）。已退出/不存在返回 -1。
- `time_rdtime()`：直接返回 `r_time()`（time CSR，非 tick）。
- `setickets(n)`：`n<1` 返回 -1；成功重设票数并重算 stride。
- **Stride 选进程**：`scheduler()` 选 `pass` 最小者，分配一个时间片后 `pass += stride`。
  等票数时（如 bench 的 8 个子进程默认票数都 1）pass 单调递增天然轮转，不会饥饿，
  因此无需额外的 `rr_idx` 轮转游标（该游标主要防纯优先级/MLFQ 下低槽位饥饿）。

---

## 4. 思考题

**① swtch 只保存 14 个寄存器、不显式保存 PC，为什么仍能正确切回？完整保存 31 个可行吗，代价？**

swtch 保存的是 **callee-saved** 的 `ra/sp/s0–s11`（14 个）。PC 不显式保存，因为"返回地址"已经
存在 `ra` 里：`swtch` 先把当前执行流的返回地址 `sd ra, 0(a0)` 存进旧 context，恢复时 `ld ra, 0(a1)`
+ `ret` 就会跳回新执行流上次停下的位置——切回 A 时恢复的 `ra` 正是 A 上次调 `swtch` 之后的下一条指令。
**caller-saved**（`t0–t6/a0–a7/gp/tp`）不必保存：C 调用约定规定这些寄存器跨函数调用不保证保留，
编译器已自行处理（需要跨调用的值会提前压栈或重算），swtch 作为一次普通函数调用只需遵守 ABI。

完整保存 31 个寄存器**可行但冗余且更慢**：每次切换多存取 17 个寄存器；更关键的是破坏调用约定
（保存了本不该由 callee 保留的 caller-saved 寄存器）。**trapframe 才需要保存全部 31 个**，因为
特权级切换对用户态透明，用户进程不知道会被打断、无法自己保存；而内核态 context 切换发生在
C 函数调用边界，遵守 ABI 存 14 个即可。

**② 从进程 A 的用户态切到进程 B 的用户态，依次经过哪些栈？每个切换点由哪段代码完成？**

```
A 用户栈 ──uservec(trampoline.S)──▶ A 内核栈 ──sched→swtch(swtch.S)──▶ 调度器栈(c->context)
        ──swtch(swtch.S)──▶ B 内核栈 ──userret(trampoline.S)──▶ B 用户栈
```

- 用户态↔内核态：`trampoline.S` 的 `uservec/userret`（经 trampoline 同址映射页）。
- 内核栈之间（A 内核栈 ↔ 调度器栈 ↔ B 内核栈）：`swtch.S` + `sched()`/`scheduler()`。
- 调度器栈属于 `struct cpu`，不属于任何进程：A 在 `exit()` 时其内核栈随后会被 `freeproc` 回收，
  若直接在 A 内核栈上把 A 换出，等 A 被回收后调度器就踩在已释放的栈上，故必须经独立调度器栈。

**③ sleep 先释放外部锁、再改状态，构造交错序列使进程永久阻塞。**

见 §2.4 的"错误交错序列"：A 检查条件不满足 → `release(lk)` → B 改变条件并 `wakeup`（A 尚非
SLEEPING，漏唤醒）→ A 才置 SLEEPING 睡去，无人再唤醒 → 永久阻塞。根治靠"先 `acquire(&p->lock)`
再 `release(lk)`"。

---

## 5. 自测结果

> 以下为**预期判据**，实际数值需在工具链环境 `make qemu` 后填入。

| 测试 | 方式 | 预期 |
| ---- | ---- | ---- |
| 页表回归 | 启动观察 | `DUMP-PAGETABLE begin ... end` 输出与 lab3 一致 |
| 抢占验证 | sh 里跑 `alt` | A/B 交错输出，最后 `alt done` 回到提示符 |
| bench | sh 里跑 `bench` | `RESULT avg_fork_wait = <n>` + `RESULT concurrent_children = 8`，`PASS: bench` |
| stride_test | sh 里跑 `stride_test` | `RESULT stride_ratio = a:b`，`b/a ∈ [2.4, 3.6]` 且每窗口两者均获 CPU，`PASS: stride` |

---

## 6. 已知债务（留待后续 lab）

- fork 仍为全量深拷贝、无 COW / refcount——lab5。
- 无文件系统、pipe 未实现（本实验明确不要求）。
- 仅单核（-smp 1）；双核（-smp 2）挑战的 `entry.S` 分核栈、`started` 屏障、
  `holding(lk)` 的 `lk->cpu` 判定等未做——留作选做加分项。
