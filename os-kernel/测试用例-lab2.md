# Lab2 测试用例与回归报告

> 学号 2024302111127 · 参数 `LAB2_BUF_SEMANTICS=1`(字符流) `LAB2_BUF_SIZE=128`

## 一、自测用例（说明书 §4 要求，归档备查）

### 用例 1：非法系统调用号返回 -1，不 panic

- **对应测试程序**：`user/badecall.c`（赠送）
- **操作**：`make qemu` 进 `sh>`，键入 `badecall`
- **预期**：对调用号 90~99 均返回 -1，打印 `TEST-1 PASS`，sh 与内核存活
- **实际结果**：✅ `TEST-1 PASS: unknown syscalls all return -1`

### 用例 2：输入缓冲边界（超长风暴不丢计数、不 panic）

- **对应测试程序**：`user/bufstorm.c`（赠送）+ `storm.txt`
- **操作**：`sh>` 键入 `bufstorm`，用 `inject_uart.py --script` 灌入 4 行
- **预期**：读满 4 行并完整回显，打印 `BUFSTORM lines=4`
- **实际结果**：✅ `BUFSTORM lines=4 bytes=148`（超长行丢字属文档化行为）

### 用例 3：read 长度恰好等于缓冲区容量（=128）

- **对应测试程序**：`user/selftest.c`（自写）
- **操作**：`sh>` 键入 `selftest`，程序阻塞后喂入 128 个字符（无换行）
- **预期**：`read(0, buf, 128)` 返回 128，无溢出
- **实际结果**：✅ `SELFTEST PASS (n=128 <= 128, 无溢出)`

### 用例 4：空缓冲区时 read 的行为

- **操作**：`make qemu` 进 `sh>` 后**不敲键盘**，观察 sh 停在 `sh> ` 等待；
  或执行 `selftest`，它会打印"阻塞中"后停住
- **预期**：缓冲空 → `consoleread` 入睡(`sleep`)，不让 CPU 空转；来字符后立即被唤醒返回
- **实际结果**：✅ sh / selftest 均阻塞等待，喂入字符立刻返回（证明"检查-入睡"生效）

---

## 二、回归测试（说明书 §4 要求）

| 步骤 | 命令 | 通过判据 |
|---|---|---|
| 1. 启动进 sh | `make qemu` | 出现 `sh> ` |
| 2. hi | 键入 `hi` | `hi: user program running, pid=2` |
| 3. spin + 键盘 | 键入 `spin`，运行时敲键盘 | 回显与 `spin N` 交错不乱 |
| 4. 自动化回归 | `python3 support/inject_uart.py --tree . --script storm.txt` | `EXPECT 失败 0 条` |

**实际结果**：四步全部通过；`inject_uart.py` 输出 `boot ok`、3 个 `EXPECT ... PASS`、`EXPECT 失败 0 条`。

---

## 三、一句话结论

- 7 个系统调用（fork/exit/wait/read/exec/getpid/write）跑通；
- 中断驱动控制台（回显/缓冲/唤醒）跑通；
- 非法调用号返回 -1 不崩溃；
- lab1 banner 回归照常打出。
