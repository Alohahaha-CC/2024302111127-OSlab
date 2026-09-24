import { mkdir, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");
const outDir = resolve(root, "验收图片", "svg");

const C = {
  bg: "#f5f7fa",
  ink: "#14213d",
  muted: "#52606d",
  line: "#9aa5b1",
  user: "#dbeafe",
  userLine: "#2563eb",
  kernel: "#fef3c7",
  kernelLine: "#d97706",
  sched: "#dcfce7",
  schedLine: "#15803d",
  irq: "#ffe4e6",
  irqLine: "#be123c",
  data: "#e0f2fe",
  dataLine: "#0369a1",
  device: "#ede9fe",
  deviceLine: "#6d28d9",
  note: "#fff7ed",
  noteLine: "#ea580c",
  white: "#ffffff",
  black: "#111827",
};

const esc = (value) =>
  String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");

function text(x, y, value, cls = "body", anchor = "start") {
  return `<text x="${x}" y="${y}" class="${cls}" text-anchor="${anchor}">${esc(value)}</text>`;
}

function lines(x, y, values, cls = "body", dy = 24, anchor = "start") {
  const tspans = values
    .map(
      (value, index) =>
        `<tspan x="${x}" dy="${index === 0 ? 0 : dy}">${esc(value)}</tspan>`,
    )
    .join("");
  return `<text x="${x}" y="${y}" class="${cls}" text-anchor="${anchor}">${tspans}</text>`;
}

function box(
  x,
  y,
  w,
  h,
  {
    fill = C.white,
    stroke = C.line,
    title = "",
    titleLines = [],
    body = [],
    titleClass = "box-title",
    bodyClass = "box-body",
    bodyY = 56,
    bodyDy = 22,
    rx = 10,
    shadow = false,
  } = {},
) {
  const titleList = titleLines.length ? titleLines : title ? [title] : [];
  const titleMarkup = titleList
    .map(
      (line, index) =>
        `<text x="${x + 18}" y="${y + 30 + index * 22}" class="${titleClass}">${esc(
          line,
        )}</text>`,
    )
    .join("");
  const bodyMarkup = body.length
    ? lines(x + 18, y + bodyY, body, bodyClass, bodyDy)
    : "";
  return `<g${shadow ? ' filter="url(#shadow)"' : ""}>
    <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${rx}" fill="${fill}" stroke="${stroke}" stroke-width="2"/>
    ${titleMarkup}
    ${bodyMarkup}
  </g>`;
}

function pill(x, y, w, h, label, fill, stroke, cls = "pill") {
  return `<g>
    <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${h / 2}" fill="${fill}" stroke="${stroke}" stroke-width="1.5"/>
    <text x="${x + w / 2}" y="${y + h / 2 + 6}" class="${cls}" text-anchor="middle">${esc(label)}</text>
  </g>`;
}

function arrow(
  x1,
  y1,
  x2,
  y2,
  {
    color = C.ink,
    dash = false,
    marker = "arrow",
    width = 2.5,
    label = "",
    lx = (x1 + x2) / 2,
    ly = (y1 + y2) / 2 - 8,
  } = {},
) {
  const markerId = color === C.irqLine ? "arrowRed" : color === C.userLine ? "arrowBlue" : marker;
  const labelMarkup = label
    ? `<text x="${lx}" y="${ly}" class="arrow-label" text-anchor="middle">${esc(label)}</text>`
    : "";
  return `<path d="M ${x1} ${y1} L ${x2} ${y2}" fill="none" stroke="${color}" stroke-width="${width}" ${dash ? 'stroke-dasharray="8 7"' : ""} marker-end="url(#${markerId})"/>
    ${labelMarkup}`;
}

function polyline(points, options = {}) {
  const { color = C.ink, dash = false, width = 2.5 } = options;
  return `<polyline points="${points}" fill="none" stroke="${color}" stroke-width="${width}" ${dash ? 'stroke-dasharray="8 7"' : ""} marker-end="url(#arrow)"/>`;
}

function annotation(x, y, w, h, title, body) {
  return `<g>
    <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="9" fill="${C.note}" stroke="${C.noteLine}" stroke-width="2"/>
    <text x="${x + 16}" y="${y + 28}" class="note-title">${esc(title)}</text>
    ${lines(x + 16, y + 54, body, "note-body", 20)}
  </g>`;
}

function sectionTitle(x, y, value, sub = "") {
  return `${text(x, y, value, "section")}${sub ? text(x, y + 25, sub, "section-sub") : ""}`;
}

function svgDocument(title, subtitle, width, height, content) {
  return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">
  <defs>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="5" stdDeviation="7" flood-color="#0f172a" flood-opacity="0.11"/>
    </filter>
    <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="${C.ink}"/>
    </marker>
    <marker id="arrowRed" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="${C.irqLine}"/>
    </marker>
    <marker id="arrowBlue" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" fill="${C.userLine}"/>
    </marker>
  </defs>
  <style>
    text { font-family: "Noto Sans SC", "Microsoft YaHei", "Microsoft JhengHei", sans-serif; fill: ${C.ink}; }
    .title { font-size: 32px; font-weight: 800; letter-spacing: .3px; }
    .subtitle { font-size: 17px; fill: ${C.muted}; }
    .section { font-size: 21px; font-weight: 750; }
    .section-sub { font-size: 14px; fill: ${C.muted}; }
    .lane { font-size: 16px; font-weight: 700; }
    .box-title { font-size: 18px; font-weight: 750; }
    .box-body { font-size: 15px; fill: #243b53; }
    .small { font-size: 13px; fill: ${C.muted}; }
    .tiny { font-size: 12px; fill: ${C.muted}; }
    .mono { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 14px; fill: #243b53; }
    .mono-small { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 12px; fill: #334e68; }
    .pill { font-size: 13px; font-weight: 700; }
    .arrow-label { font-size: 12px; font-weight: 700; fill: #334e68; }
    .note-title { font-size: 15px; font-weight: 800; fill: #9a3412; }
    .note-body { font-size: 13px; fill: #7c2d12; }
    .table-head { font-size: 14px; font-weight: 750; fill: #243b53; }
    .table-cell { font-size: 13px; fill: #334e68; }
    .code { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 13px; fill: #0f172a; }
  </style>
  <rect width="${width}" height="${height}" fill="${C.bg}"/>
  <rect x="36" y="28" width="${width - 72}" height="${height - 56}" rx="18" fill="${C.white}" stroke="#cbd2d9" stroke-width="1.5"/>
  ${text(68, 78, title, "title")}
  ${text(68, 106, subtitle, "subtitle")}
  ${content}
</svg>`;
}

function controlFlowSvg() {
  const width = 2100;
  const height = 1280;
  const laneX = 62;
  const laneW = 1976;
  const lanes = [
    { y: 132, h: 190, fill: "#eff6ff", title: "U 态 · 用户栈", color: C.userLine },
    { y: 332, h: 258, fill: "#fffbeb", title: "S 态 · 进程内核栈", color: C.kernelLine },
    { y: 600, h: 170, fill: "#f0fdf4", title: "S 态 · 调度器栈", color: C.schedLine },
    { y: 780, h: 172, fill: "#fff1f2", title: "S 态 · 中断上下文", color: C.irqLine },
  ];
  const laneMarkup = lanes
    .map(
      (lane) => `<g>
        <rect x="${laneX}" y="${lane.y}" width="${laneW}" height="${lane.h}" rx="12" fill="${lane.fill}" stroke="#d9e2ec" stroke-width="1.5"/>
        <rect x="${laneX}" y="${lane.y}" width="190" height="${lane.h}" rx="12" fill="${lane.color}" opacity="0.13"/>
        <text x="${laneX + 22}" y="${lane.y + 34}" class="lane">${esc(lane.title)}</text>
      </g>`,
    )
    .join("");

  const content = `
    ${laneMarkup}
    ${pill(70, 114, 160, 28, "时间向右 →", C.white, C.line)}
    ${text(1850, 136, "编号 1–10 为主要时序；红色虚线为中断插曲", "tiny", "end")}

    ${box(112, 182, 270, 104, {
      fill: C.user,
      stroke: C.userLine,
      title: "1  sh 等待命令",
      body: ["getcmd → gets → read(0)", "ecall(SYS_read)　U / 用户栈"],
      bodyY: 58,
      bodyDy: 22,
    })}
    ${arrow(400, 220, 470, 220, { color: C.userLine, label: "陷入", lx: 435, ly: 207 })}

    ${box(480, 360, 390, 126, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "2  read 进入控制台",
      body: [
        "uservec → usertrap → syscall",
        "sys_read → fileread → consoleread",
        "发现输入为空：cons.r == cons.w",
      ],
      bodyY: 58,
    })}
    ${arrow(880, 420, 950, 420, { label: "sleep", lx: 915, ly: 407 })}

    ${box(960, 346, 350, 154, {
      fill: "#fff7ed",
      stroke: C.noteLine,
      title: "3  两阶段睡眠",
      body: [
        "sleep_prepare(&cons.r)",
        "release(&cons.lock)",
        "sleep() → state=SLEEPING",
        "sched() → swtch",
      ],
      bodyY: 58,
    })}
    ${arrow(1130, 500, 1130, 620, { color: C.schedLine, label: "让出 CPU", lx: 1190, ly: 570 })}

    ${box(990, 642, 330, 96, {
      fill: C.sched,
      stroke: C.schedLine,
      title: "4  scheduler 循环",
      body: ["寻找 RUNNABLE 进程", "sh 在等待 UART，不在候选集"],
      bodyY: 57,
    })}

    ${box(1010, 818, 420, 104, {
      fill: C.irq,
      stroke: C.irqLine,
      title: "5  UART 接收中断",
      body: ["devintr → uartintr → consoleintr", "收集整行 → wakeup(&cons.r)"],
      bodyY: 57,
    })}
    ${arrow(1060, 818, 1060, 744, { color: C.irqLine, dash: true, label: "把 sh 改为 RUNNABLE", lx: 1190, ly: 782 })}
    ${polyline("980,680 720,680 720,560", { color: C.schedLine })}
    ${text(740, 667, "以后重新调度 sh", "arrow-label")}
    ${polyline("720,560 750,430 930,430", { color: C.schedLine })}

    ${box(1300, 360, 340, 128, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "6  fork / kfork",
      body: ["uvmcopy 复制地址空间", "复制 trapframe；child.a0=0", "filedup 增加 file.ref"],
      bodyY: 58,
    })}
    ${arrow(940, 425, 1300, 425, { label: "read 返回后", lx: 1120, ly: 412 })}
    ${polyline("1470,488 1470,540 1580,540 1580,395", { color: C.kernelLine })}
    ${polyline("1470,488 1470,245 1640,245", { color: C.userLine })}

    ${box(1600, 185, 275, 120, {
      fill: C.user,
      stroke: C.userLine,
      title: "7  父子分流",
      body: ["sh：fork 返回 pid", "echo：fork 返回 0", "同一用户 PC，不同 a0"],
      bodyY: 57,
    })}
    ${box(1885, 185, 125, 120, {
      fill: C.user,
      stroke: C.userLine,
      title: "8",
      titleLines: ["8", "sh 等待"],
      body: ["kwait", "SLEEPING"],
      bodyY: 73,
      bodyDy: 20,
    })}
    ${arrow(1875, 245, 1885, 245, { color: C.userLine })}

    ${box(1590, 625, 410, 150, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "9  echo：exec / kexec",
      body: [
        "新建页表；加载 ELF + guard + 栈",
        "trapframe.epc=入口，a0=argc",
        "整体释放旧地址空间；进程不新建",
      ],
      bodyY: 58,
    })}
    ${polyline("1740,305 1740,625", { color: C.userLine, dash: true })}
    ${arrow(2000, 700, 2050, 700, { color: C.kernelLine })}
    ${box(1630, 842, 370, 96, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "10  write / exit",
      body: ["filewrite → consolewrite → uartwrite", "exit → kexit：ZOMBIE；parent wait 回收"],
      bodyY: 56,
    })}
    ${polyline("2050,700 2050,842", { color: C.kernelLine })}

    ${annotation(
      72,
      982,
      600,
      220,
      "现场批注 A · 为什么要两阶段睡眠",
      [
        "先 sleep_prepare(&cons.r) 再释放 cons.lock，",
        "可以避免“检查为空 → 锁外睡着 → UART 已经 wakeup”",
        "造成永久丢唤醒。当前新版 xv6 用 chan 清零点补上窗口。",
      ],
    )}
    ${annotation(
      690,
      982,
      620,
      220,
      "现场批注 B · fork 与 exec 的本质差别",
      [
        "fork 复制出新的 proc、页表、trapframe 和文件引用；",
        "exec 仍是同一 pid、同一 proc，只替换页表、sz、epc、sp。",
        "所以 echo 的 fd 0/1/2 仍然继承 sh。",
      ],
    )}
    ${annotation(
      1328,
      982,
      676,
      220,
      "现场批注 C · 输出为何不是 inode 写入",
      [
        "stdout 对应 FD_DEVICE: file.major=CONSOLE=1。",
        "filewrite 按 major 调 devsw[1].write=consolewrite，",
        "最终在 uartwrite 中写 UART0.THR；不是 writei 写磁盘 inode。",
      ],
    )}
  `;
  return svgDocument(
    "lab0 材料一 · echo hi 的全系统控制流",
    "追踪范围：Shell 读入命令 → fork/exec → echo 输出 → exit/wait 回收。括号不写“模式说明”，直接落到栈、特权级和锁。",
    width,
    height,
    content,
  );
}

function snapshotSvg() {
  const width = 2100;
  const height = 1280;
  const content = `
    ${sectionTitle(70, 155, "A. 进程表快照", "截面：kexec 刚提交新页表，echo 首条用户指令尚未运行")}
    ${box(62, 198, 620, 372, {
      fill: "#eff6ff",
      stroke: C.userLine,
      title: "proc[NPROC] · 只看有效进程",
      body: [
        "init　state=SLEEPING　parent=0",
        "　　　pagetable=P_init　sz=init 镜像大小",
        "　　　ofile[0/1/2] → F_console",
        "",
        "sh　　state=SLEEPING　parent=init",
        "　　　pagetable=P_sh　sz=sh 镜像大小",
        "　　　ofile[0/1/2] → F_console",
        "",
        "echo　state=RUNNING　parent=sh",
        "　　　pagetable=P_echo（新表已提交）",
        "　　　sz=PGROUNDUP(ELF末尾)+2×PGSIZE",
        "　　　ofile[0/1/2] → F_console",
      ],
      bodyY: 65,
      bodyDy: 22,
    })}
    ${box(62, 590, 620, 250, {
      fill: C.white,
      stroke: C.line,
      title: "echo.trapframe 关键字段",
      body: [
        "epc = elf.entry　→ 首条用户指令",
        "sp  = 新用户栈顶",
        "a0  = argc = 2　a1 = argv",
        "kernel_satp / kernel_sp / kernel_trap",
        "→ 已在 prepare_return 前写入",
      ],
      bodyY: 62,
    })}
    ${annotation(
      62,
      862,
      620,
      260,
      "自主批注 1 · 为什么 sh 在睡、echo 在跑",
      [
        "sh 是父进程，fork 后马上 wait(0)，在 kwait 中 SLEEPING。",
        "echo 是子进程；被调度后从 fork 返回 0，马上进入 exec。",
        "截面落在 exec 已提交、尚未 sret 的精确时间点。",
      ],
    )}

    ${sectionTitle(714, 155, "B. echo 的 SV39 用户页表", "低地址用户映射 + 最高两页内核跳板；完整画地址边界和 PTE")}
    <rect x="704" y="198" width="760" height="924" rx="14" fill="#f8fafc" stroke="#cbd2d9" stroke-width="1.5"/>
    <line x1="1084" y1="258" x2="1084" y2="1060" stroke="#94a3b8" stroke-width="3"/>
    <text x="755" y="260" class="table-head">虚拟地址从低到高</text>
    <text x="1084" y="250" class="tiny" text-anchor="middle">SV39 VA</text>
    ${box(760, 286, 600, 92, {
      fill: C.data,
      stroke: C.dataLine,
      title: "text    0x00000000 → code_end",
      body: ["PTE_V | PTE_R | PTE_X | PTE_U　　epc 指向 ELF 入口"],
      bodyY: 58,
    })}
    ${box(760, 390, 600, 92, {
      fill: C.user,
      stroke: C.userLine,
      title: "rodata / data / bss    程序段",
      body: ["只读：V|R|U　　可写：V|R|W|U；BSS 清零"],
      bodyY: 58,
    })}
    ${box(760, 494, 600, 92, {
      fill: "#fff7ed",
      stroke: C.noteLine,
      title: "heap    当前未分配",
      body: ["sbrk 提升 sz；首次访问由 vmfault 填页"],
      bodyY: 58,
    })}
    ${box(760, 598, 600, 108, {
      fill: "#fef2f2",
      stroke: "#dc2626",
      title: "guard page    sz - 2×PGSIZE",
      body: ["PTE_V | PTE_R | PTE_W　但 PTE_U=0", "页表里存在；用户访问立即 fault，防栈越界踩数据"],
      bodyY: 58,
    })}
    ${box(760, 718, 600, 100, {
      fill: C.user,
      stroke: C.userLine,
      title: "user stack    sz - PGSIZE → sz",
      body: ["PTE_V | PTE_R | PTE_W | PTE_U　sp 指向 sz，向下增长"],
      bodyY: 58,
    })}
    ${box(760, 842, 600, 94, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "TRAPFRAME    0x3FFFFFE000",
      body: ["PTE_V | PTE_R | PTE_W　无 U；每进程独立物理页"],
      bodyY: 58,
    })}
    ${box(760, 948, 600, 94, {
      fill: C.sched,
      stroke: C.schedLine,
      title: "TRAMPOLINE    0x3FFFFFF000",
      body: ["PTE_V | PTE_R | PTE_X　无 W/U；内核和所有进程共享同一物理页"],
      bodyY: 58,
    })}
    ${text(1092, 1080, "MAXVA = 0x4000000000", "mono-small", "middle")}
    ${annotation(
      704,
      1140,
      760,
      72,
      "自主批注 2 · guard 不是“没映射”",
      ["uvmclear 只清掉 PTE_U，保留 V/R/W；这样内核仍能管理该页，而用户态访问会触发缺页/越界路径。"],
    )}

    ${sectionTitle(1512, 155, "C. 文件表快照", "stdout 是设备文件；引用计数决定何时真正释放")}
    ${box(1495, 198, 530, 230, {
      fill: "#eff6ff",
      stroke: C.userLine,
      title: "echo->ofile[NOFILE]",
      body: [
        "ofile[0] ─┐",
        "ofile[1] ─┼──→ 同一个 struct file F_console",
        "ofile[2] ─┘",
        "ofile[3..15] = 0",
      ],
      bodyY: 66,
    })}
    ${arrow(1760, 428, 1760, 520, { label: "fd 1 写", lx: 1820, ly: 477 })}
    ${box(1495, 522, 530, 286, {
      fill: C.device,
      stroke: C.deviceLine,
      title: "F_console：系统全局 file 表",
      body: [
        "type = FD_DEVICE",
        "ref = 9　（init/sh/echo 各 3 份）",
        "readable = 1　writable = 1",
        "major = CONSOLE = 1",
        "ip → console inode（T_DEVICE）",
        "minor = 0　off = 0",
      ],
      bodyY: 64,
    })}
    ${arrow(1760, 808, 1760, 880, { color: C.deviceLine, label: "filewrite 按 major 分发", lx: 1870, ly: 850 })}
    ${box(1495, 882, 530, 174, {
      fill: C.white,
      stroke: C.line,
      title: "实际输出链",
      body: [
        "filewrite → devsw[1].write",
        "→ consolewrite → uartwrite",
        "→ 读 LSR.THRE；写 UART0.THR",
        "文件里的 ip 只标明设备节点，不走 writei。",
      ],
      bodyY: 58,
    })}
    ${annotation(
      1495,
      1090,
      530,
      122,
      "自主批注 3 · ref=9 怎么来",
      [
        "init: open + dup + dup = 3；fork sh +3；",
        "fork echo +3。exec 不关闭 fd，故仍共享 F_console。",
      ],
    )}
  `;
  return svgDocument(
    "lab0 材料二 · 核心数据结构全景快照",
    "截面时刻：echo 的 exec 刚刚执行完毕、echo 的首条用户指令尚未运行。字段名按当前 xv6-riscv 源码。",
    width,
    height,
    content,
  );
}

function timerSvg() {
  const width = 1900;
  const height = 1260;
  const cards = [
    {
      x: 68,
      y: 172,
      fill: C.user,
      stroke: C.userLine,
      n: "1",
      title: "硬件陷入：U → S",
      body: [
        "scause=0x8000000000000005",
        "sepc=被打断用户 PC",
        "PC=TRAMPOLINE+uservec",
        "仍是用户 satp / 用户 sp",
      ],
    },
    {
      x: 500,
      y: 172,
      fill: C.kernel,
      stroke: C.kernelLine,
      n: "2",
      title: "uservec：保存现场",
      body: [
        "所有用户寄存器 → p->trapframe",
        "ld sp, kernel_sp",
        "ld t1, kernel_satp",
        "切内核页表并刷新 TLB",
      ],
    },
    {
      x: 932,
      y: 172,
      fill: C.kernel,
      stroke: C.kernelLine,
      n: "3",
      title: "usertrap：识别定时器",
      body: [
        "trapframe->epc = sepc",
        "stvec = kernelvec",
        "devintr → clockintr",
        "tickslock：tick++ / wakeup",
      ],
    },
    {
      x: 1364,
      y: 172,
      fill: C.note,
      stroke: C.noteLine,
      n: "4",
      title: "yield：进入调度点",
      body: [
        "acquire(&p->lock)",
        "state: RUNNING → RUNNABLE",
        "sched() 断言中断已关",
        "尚无其它自旋锁",
      ],
    },
    {
      x: 1364,
      y: 520,
      fill: C.sched,
      stroke: C.schedLine,
      n: "5",
      title: "swtch：离开进程栈",
      body: [
        "保存 ra/sp/s0–s11 → p->context",
        "恢复 cpus[hart].context",
        "回到 scheduler 的 swtch 下一行",
        "p->lock 跨切换继续持有",
      ],
    },
    {
      x: 932,
      y: 520,
      fill: C.sched,
      stroke: C.schedLine,
      n: "6",
      title: "scheduler：选择进程",
      body: [
        "c->proc=0；扫描 proc[]",
        "挑选 state==RUNNABLE",
        "设 RUNNING 并再次 swtch",
        "恢复 echo 内核栈",
      ],
    },
    {
      x: 500,
      y: 520,
      fill: C.kernel,
      stroke: C.kernelLine,
      n: "7",
      title: "prepare_return：备返回",
      body: [
        "stvec = uservec",
        "写入下一次 kernel_sp/satp/trap",
        "sstatus.SPP=0；SPIE=1",
        "sepc = trapframe->epc",
      ],
    },
    {
      x: 68,
      y: 520,
      fill: C.user,
      stroke: C.userLine,
      n: "8",
      title: "userret + sret",
      body: [
        "切回用户 satp",
        "恢复全部用户寄存器",
        "sret → U 态 / 用户栈",
        "回到被打断的同一条指令",
      ],
    },
  ];
  const cardMarkup = cards
    .map((card, index) => {
      const distance = index < 4 ? 432 : -432;
      const nextX = index === 3 ? 1364 : card.x + distance;
      const nextY = index === 3 ? 520 : card.y;
      const arrowMarkup =
        index < 7
          ? arrow(
              index === 3 || index === 4 ? card.x + (index === 3 ? 192 : -192) : card.x + 192,
              index === 3 || index === 4 ? card.y + (index === 3 ? 210 : 0) : card.y + 106,
              index === 3 || index === 4 ? nextX + (index === 3 ? 192 : -192) : nextX + 192,
              index === 3 || index === 4 ? nextY + (index === 3 ? 0 : 0) : nextY + 106,
              {
                color: index === 3 ? C.schedLine : index >= 4 ? C.schedLine : C.kernelLine,
                label: index === 3 ? "让出 CPU" : "",
              },
            )
          : "";
      return `${box(card.x, card.y, 384, 214, {
        fill: card.fill,
        stroke: card.stroke,
        title: `${card.n}  ${card.title}`,
        titleLines: [`${card.n}  ${card.title}`],
        body: card.body,
        bodyY: 64,
        bodyDy: 23,
        shadow: true,
      })}
      ${arrowMarkup}`;
    })
    .join("");

  const content = `
    <rect x="58" y="138" width="1784" height="700" rx="16" fill="#f8fafc" stroke="#cbd2d9"/>
    ${cardMarkup}
    ${box(235, 790, 430, 152, {
      fill: C.white,
      stroke: C.line,
      title: "两套“现场”不要混",
      body: [
        "trapframe：用户 ra/sp/gp/tp/epc/a0…a7/s0…s11/t0…t6",
        "context：内核 ra/sp/s0…s11",
        "前者由 uservec/userret 保存恢复；后者由 swtch 保存恢复。",
      ],
      bodyY: 58,
    })}
    ${box(735, 790, 430, 152, {
      fill: "#fff7ed",
      stroke: C.noteLine,
      title: "锁与中断状态",
      body: [
        "tickslock 在 clockintr 内短暂持有，随后释放。",
        "yield 获取 p->lock，sched 要求 noff==1。",
        "p->lock 跨 swtch；中断在整个 sched 路径保持关闭。",
      ],
      bodyY: 58,
    })}
    ${box(1235, 790, 430, 152, {
      fill: C.white,
      stroke: C.line,
      title: "为什么 sepc 不加 4",
      body: [
        "定时器中断不是 ecall。",
        "usertrap 只有 scause==8 时执行 trapframe->epc += 4。",
        "sret 应回到被打断的同一用户指令。",
      ],
      bodyY: 58,
    })}
    ${annotation(
      68,
      990,
      1597,
      206,
      "验收讲解主线 · 先讲三次控制转移，再讲两次状态保存",
      [
        "第一次控制转移：硬件把 U 态执行流送入 S 态 uservec，但还沿用用户页表和用户 sp。",
        "第二次控制转移：uservec 切到内核页表与内核栈，把陷阱交给 usertrap；此时才形成“当前进程在 S 态内核栈运行”的状态。",
        "第三次控制转移：yield/sched/swtch 让出 CPU，栈从进程内核栈切到调度器栈；调度器以后再次 swtch 回来。",
        "第一次状态保存是用户态寄存器进 trapframe；第二次状态保存是内核 callee-saved 寄存器进 context。最后 prepare_return 重建返回条件，userret+sret 回到用户态。",
      ],
    )}
  `;
  return svgDocument(
    "lab0 材料三 · 一次时钟中断的微观旅程",
    "场景：echo 在 U 态运行时收到 supervisor timer interrupt；从不经过 M 态陷阱处理程序。",
    width,
    height,
    content,
  );
}

function lab1BootSvg() {
  const width = 1900;
  const height = 1080;
  const steps = [
    {
      x: 72,
      title: "1  QEMU 装载",
      body: ["-machine virt", "-bios none", "ELF entry = _entry", "物理地址 0x80000000"],
      fill: C.data,
      stroke: C.dataLine,
    },
    {
      x: 388,
      title: "2  _entry（M 态）",
      body: ["csrci mstatus, 8", "sp = bootstack + 12KB", "非 0 hart → park", ".balign 4 + mtvec"],
      fill: C.kernel,
      stroke: C.kernelLine,
    },
    {
      x: 704,
      title: "3  start()（M 态）",
      body: ["MPP=S；MIE=0", "mepc=main", "satp=0（Bare）", "delegation=0xffff"],
      fill: C.kernel,
      stroke: C.kernelLine,
    },
    {
      x: 1020,
      title: "4  PMP 授权",
      body: ["pmpaddr0 = 0x3fff…ffff", "pmpcfg0 = 0xf", "S 态全物理空间 R/W/X", "缺失则 cause=1 黑屏"],
      fill: C.irq,
      stroke: C.irqLine,
    },
    {
      x: 1336,
      title: "5  mret → main",
      body: ["mret 使用 mepc", "MPP 降入 S 态", "入口仍是物理地址", "内核栈沿用 bootstack"],
      fill: C.sched,
      stroke: C.schedLine,
    },
    {
      x: 1652,
      title: "6  UART + banner",
      body: ["consoleinit()", "轮询 LSR bit5", "写 UART0.THR", "协议 2 + 校验和"],
      fill: C.user,
      stroke: C.userLine,
    },
  ];
  const arrows = steps
    .slice(0, -1)
    .map((step) =>
      arrow(step.x + 270, 356, step.x + 316, 356, { color: C.ink, label: "" }),
    )
    .join("");
  const content = `
    <rect x="58" y="140" width="1784" height="370" rx="16" fill="#f8fafc" stroke="#cbd2d9"/>
    ${steps
      .map((step) =>
        box(step.x, 192, 270, 330, {
          fill: step.fill,
          stroke: step.stroke,
          title: step.title,
          body: step.body,
          bodyY: 74,
          bodyDy: 30,
          shadow: true,
        }),
      )
      .join("")}
    ${arrows}

    ${sectionTitle(70, 570, "关键寄存器和栈的变换", "验收时按“为什么必须这样做，否则会发生什么”回答")}
    <rect x="62" y="602" width="1776" height="330" rx="14" fill="${C.white}" stroke="#cbd2d9"/>
    <line x1="62" y1="656" x2="1838" y2="656" stroke="#d9e2ec"/>
    <line x1="420" y1="602" x2="420" y2="932" stroke="#e5e7eb"/>
    <line x1="860" y1="602" x2="860" y2="932" stroke="#e5e7eb"/>
    <line x1="1280" y1="602" x2="1280" y2="932" stroke="#e5e7eb"/>
    ${text(92, 638, "对象", "table-head")}
    ${text(450, 638, "进入 main 前", "table-head")}
    ${text(890, 638, "进入 main 后", "table-head")}
    ${text(1310, 638, "考试要点", "table-head")}
    ${text(92, 690, "mstatus.MPP", "code")}
    ${text(450, 690, "S（由 start 设置）", "table-cell")}
    ${text(890, 690, "mret 后降至 S 态", "table-cell")}
    ${text(1310, 690, "写错会回到 M/U，权限模型错误", "table-cell")}
    ${text(92, 734, "mepc", "code")}
    ${text(450, 734, "main", "table-cell")}
    ${text(890, 734, "mret 跳转目标", "table-cell")}
    ${text(1310, 734, "不是 sepc，也不是 sret", "table-cell")}
    ${text(92, 778, "satp", "code")}
    ${text(450, 778, "0 = Bare", "table-cell")}
    ${text(890, 778, "无地址翻译", "table-cell")}
    ${text(1310, 778, "lab1 不需要恒等页表", "table-cell")}
    ${text(92, 822, "sp", "code")}
    ${text(450, 822, "bootstack + 12KB", "table-cell")}
    ${text(890, 822, "栈顶，向下生长", "table-cell")}
    ${text(1310, 822, "必须与 LAB1_STACK_KB 一致", "table-cell")}
    ${text(92, 866, "PMP", "code")}
    ${text(450, 866, "全物理空间 R/W/X", "table-cell")}
    ${text(890, 866, "S 态可继续取指/访存", "table-cell")}
    ${text(1310, 866, "删除后通常 cause=1 且全黑屏", "table-cell")}
    ${text(92, 910, "mtvec", "code")}
    ${text(450, 910, "mtrap（4 字节对齐）", "table-cell")}
    ${text(890, 910, "异常兜底自旋", "table-cell")}
    ${text(1310, 910, "未对齐写入会被硬件忽略", "table-cell")}

    ${annotation(
      70,
      960,
      1760,
      86,
      "一句话主线",
      [
        "QEMU 已在 RAM 中准备好镜像，但没有固件替你完成 M→S 配置；内核自己关中断、备栈、设置 mtvec/PMP，再用 mret 降权到 S 态运行 main。",
      ],
    )}
  `;
  return svgDocument(
    "lab1 · 启动时序与 M → S 特权级切换",
    "个性化参数：协议 2；mod97=0xe；栈 12KB；节流周期 23 字节。",
    width,
    height,
    content,
  );
}

function lab1MemorySvg() {
  const width = 1700;
  const height = 1160;
  const content = `
    ${sectionTitle(70, 155, "物理地址空间", "QEMU virt 机器固定布局；内核链接/加载地址 = RAM 起点")}
    <rect x="62" y="194" width="760" height="830" rx="14" fill="#f8fafc" stroke="#cbd2d9"/>
    <line x1="420" y1="238" x2="420" y2="984" stroke="#94a3b8" stroke-width="3"/>
    ${box(86, 244, 666, 94, {
      fill: C.data,
      stroke: C.dataLine,
      title: "0x88000000  PHYSTOP",
      body: ["RAM 管理上限：KERNBASE + 128MB"],
      bodyY: 58,
    })}
    ${box(86, 352, 666, 340, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "0x80000000  KERNBASE / BASE_ADDRESS",
      body: [
        ".text　→ entry.S 的 _entry 位于最前",
        ".rodata / .data",
        ".bss　→ 含 bootstack[LAB1_STACK_KB × 1024]",
        "kernel_end",
        "",
        "内核栈：12KB；sp = bootstack + 0x3000",
        "栈向下增长；sp 初值由 entry.S 与 start.c 共同约束",
      ],
      bodyY: 70,
      bodyDy: 30,
    })}
    ${box(86, 706, 666, 86, {
      fill: C.user,
      stroke: C.userLine,
      title: "0x10001000  VIRTIO0",
      body: ["MMIO 磁盘接口；lab1 不使用"],
      bodyY: 57,
    })}
    ${box(86, 806, 666, 86, {
      fill: C.irq,
      stroke: C.irqLine,
      title: "0x10000000  UART0",
      body: ["输出目标；THR=offset 0；LSR=offset 5；bit5=THRE"],
      bodyY: 57,
    })}
    ${box(86, 906, 666, 86, {
      fill: C.sched,
      stroke: C.schedLine,
      title: "0x0C000000 PLIC / 0x02000000 CLINT",
      body: ["平台中断控制器 / 核内定时器；lab1 只输出不接收"],
      bodyY: 57,
    })}
    ${box(86, 1006, 666, 86, {
      fill: "#f1f5f9",
      stroke: C.line,
      title: "0x00001000  QEMU reset/boot ROM 地址",
      body: ["-bios none：不依赖固件替内核完成早期初始化"],
      bodyY: 57,
    })}

    ${sectionTitle(880, 155, "启动时的关键地址与含义", "不要把链接地址、加载地址和 MMIO 混在一起")}
    ${box(872, 194, 760, 180, {
      fill: C.white,
      stroke: C.line,
      title: "链接地址 = 加载地址",
      body: [
        "kernel.ld: BASE_ADDRESS = 0x80000000",
        "QEMU 直接读 ELF program header 并装载到该物理地址。",
        "符号 _entry、main 和全局变量的地址都建立在同一个物理映射上。",
      ],
      bodyY: 64,
      bodyDy: 28,
    })}
    ${box(872, 404, 360, 270, {
      fill: C.kernel,
      stroke: C.kernelLine,
      title: "M 态启动现场",
      body: [
        "PC = _entry",
        "satp = 0（Bare）",
        "sp = bootstack + 12KB",
        "mstatus.MPP 设为 S",
        "mepc = main",
        "PMP 覆盖物理空间",
      ],
      bodyY: 66,
      bodyDy: 29,
    })}
    ${box(1272, 404, 360, 270, {
      fill: C.user,
      stroke: C.userLine,
      title: "S 态 main 现场",
      body: [
        "特权级 = S",
        "地址翻译 = Bare",
        "栈 = 同一 bootstack",
        "consoleinit() 初始化 UART",
        "printf 输出 banner",
        "for(;;) wfi",
      ],
      bodyY: 66,
      bodyDy: 29,
    })}
    ${annotation(
      872,
      704,
      760,
      320,
      "验收问答的三条硬结论",
      [
        "1. 链接到 0x80000000：QEMU virt 的可执行 RAM 从该地址开始。",
        "2. -bios none：没有固件替你做特权级、PMP、栈和 mtvec 初始化。",
        "3. PMP 授权缺失：mret 后 S 态第一次取指可能立刻 instruction access fault。",
        "",
        "UART 输出协议：等待 LSR.bit5=1，再向 THR 写字符。",
        "banner 十六进制必须 0x 小写且无前导零。",
      ],
    )}
    ${annotation(
      872,
      1048,
      760,
      74,
      "自主批注 · 为什么先关中断",
      [
        "entry.S 开始时 mtvec 和 sp 都还没就绪；若 MIE 已开且立刻来中断，异常入口可能跳到无效地址。",
      ],
    )}
  `;
  return svgDocument(
    "lab1 · 内核内存布局与启动地址",
    "从 0x80000000 的裸机入口到 UART 输出；图中地址均为物理地址。",
    width,
    height,
    content,
  );
}

const files = [
  ["01-lab0-全系统控制流图.svg", controlFlowSvg()],
  ["02-lab0-核心数据结构快照.svg", snapshotSvg()],
  ["03-lab0-时钟中断微观旅程.svg", timerSvg()],
  ["04-lab1-启动时序与特权级.svg", lab1BootSvg()],
  ["05-lab1-内存布局与启动地址.svg", lab1MemorySvg()],
];

await mkdir(outDir, { recursive: true });
for (const [name, svg] of files) {
  await writeFile(resolve(outDir, name), svg, "utf8");
}

console.log(`Generated ${files.length} SVG files in ${outDir}`);
