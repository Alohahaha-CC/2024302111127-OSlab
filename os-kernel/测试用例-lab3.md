# Lab3 测试用例与回归报告

> 学号 2024302111127 · 参数 `LAB3_ALLOC_ORDER=2`(中点) `LAB3_GUARD_PAGES=2` `LAB3_MEMCAP_DELTA=7`

## 一、自测用例（说明书 §4 要求，编码前设计，归档备查）

### 用例 1：保护页越界访问 → 内核安全终止进程

- **对应测试程序**：`user/selftest3.c`（自写，`SELFTEST-3a`）
- **设计思路**：fork 一个子进程，用 `read_sp()` 读栈指针，向**用户栈下方 2 页**（正好落在
  `LAB3_GUARD_PAGES=2` 的 guard page 空洞）写入，触发 store page fault，验证内核终止该进程而非 panic。
- **操作**：`make qemu` 进 `sh>`，键入 `selftest3`
- **预期**：子进程写 guard 页 → 硬件 `scause=15`(store fault) → `usertrap` default 分支
  `setkilled` → `kexit`；父进程 `wait` 回收非 0 状态，打印 PASS。
- **实际结果**：✅
  ```
  usertrap(): unexpected scause 0xf pid=3
              sepc=0x12 stval=0x1fe0
  SELFTEST-3a PASS: guard access killed (status=-1)
  ```
  `stval=0x1fe0` 落在 guard 区间 `[0x1000, 0x3000)` 内，证明确实是保护页拦截。

### 用例 2：跨越物理页边界的受控拷贝

- **对应测试程序**：`user/selftest3.c`（自写，`SELFTEST-3b`）
- **设计思路**：`sbrk(2*PGSIZE)` 申请两页，构造缓冲 `mid = p + PGSIZE - 64`（前 64 字节 'A'
  落在第一页末尾、后 64 字节 'B' 落在第二页开头），`write(1, mid, 128)` 让内核 copyin 逐页搬运，
  验证跨页不丢字、数据完整。
- **操作**：`make qemu` 进 `sh>`，键入 `selftest3`
- **预期**：`write` 返回 128，输出 `64×'A' + 64×'B'` 完整无截断。
- **实际结果**：✅
  ```
  AAAA...(64)BBBB...(64)
  SELFTEST-3b PASS (cross-page write=128/128)
  ```

---

## 二、官方测试（课程组评分参考，support/tests/）

| 测试程序 | 结果 |
|----------|------|
| `badaccess` | ✅ `TEST-3a PASS`（写地址 0 被杀, scause=0xf）、`TEST-3b PASS`（越 sbrk 读被杀, scause=0xd） |
| `memcap` | ✅ `TEST-4 PASS: OOM returned error after 2031 chunks` |

---

## 三、回归测试（说明书 §4 要求）

| 步骤 | 命令 | 通过判据 |
|------|------|---------|
| 1. 启动进 sh | `make qemu` | 出现 `sh> `，且打印一次 `DUMP-PAGETABLE begin/end` |
| 2. hi | 键入 `hi` | `hi: user program running, pid=2` |
| 3. spin | 键入 `spin` | 输出 `spin N` 不断递增 |
| 4. 自动化回归 | `python3 support/inject_uart.py --tree . --script storm.txt` | `EXPECT 失败 0 条` |

**实际结果**：四步全部通过；`inject_uart.py` 输出 `boot ok`、3 个 `EXPECT ... PASS`、`EXPECT 失败 0 条`。
（注意：inject_uart.py 依赖 WSL 的 qemu 与 Unix `select()`，必须在 WSL 内运行，Windows PowerShell 下会失败。）

---

## 四、页表 dump 快照（对照 lab0 地址空间图）

```
LEAF va=0x0      perm=r-xu   ← 代码+只读数据(无 W)
LEAF va=0x1000   perm=rw-u   ← 数据+bss
LEAF va=0x4000   perm=rw-u   ← 用户栈(guard 页 0x2000/0x3000 不打印=未映射)
LEAF va=0x3fffffe000 perm=rw-- ← trapframe(无 U)
LEAF va=0x3ffffff000 perm=r-x- ← trampoline(无 U)
```

---

## 五、一句话结论

- 位图分配器(中点分配 + 7 页配额)跑通，OOM 优雅返回 -1 不崩溃；
- 三级页表 + 每进程独立地址空间跑通，代码 R+X / 数据 R+W / guard page / 栈逐项正确；
- sbrk 动态扩展(EAGER)跑通；
- guard page 越界访问被杀、跨页 copyin 不丢字，两项自测用例均 PASS。
