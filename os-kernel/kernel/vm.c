// lab3 vm.c —— SV39 三级页表 + 位图物理页分配器 + 受控拷贝 + 页表观测。
//
// 相比 lab2 的三处升级:
//   1. 物理页分配器: 由 bump(只增不回收) 换成**位图**, 每物理页 1 bit;
//      O(1) 释放, 分配按 LAB3_ALLOC_ORDER(本学号=2 中点) 检索空闲位;
//      可用上限受 LAB3_MEMCAP_DELTA 配额约束(空闲页 ≤ DELTA 时返回 0)。
//   2. mappages 失败回滚: 拆除本调用已建立的部分映射并回收空中间页表。
//   3. dump_pagetable: 按 dump_pagetable-ABI.md 输出页表结构。
//
// 回滚不变式(设计笔记 §"mappages 回滚一致性" 的落点):
//   - mappages 只拥有**本次调用中新建的中间页表页**; 失败时拆除 [va,a) 的
//     叶子映射(do_free=0, **不**释放调用方传入的物理页), 再回收空中间页表。
//   - 调用方传入的叶子物理页释放职责归**调用方**(uvmalloc/loadseg 各自
//     kfree 当前页 + uvmunmap(do_free=1) 已建页)。
//
// 分配器数据结构选型(设计笔记 §"阶段一" 的落点):
//   位图 vs 有序链表 —— 链表释放需按地址插入 O(n), memcap 批量释放数万页
//   退化为 O(n^2)(QEMU 下数分钟); 位图释放 O(1), 分配扫描空闲位, 元数据
//   开销 128MB 仅 ~4KB(1 页), 故选位图。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "course_sid.h"

pagetable_t kernel_pagetable;

extern char etext[];      // kernel.ld 导出: 内核代码结束
extern char end[];        // kernel.ld 导出: 内核镜像结束(空闲页起点)
extern char trampoline[]; // trampoline.S

// ---------------------------------------------------------------------------
// 物理页分配器(位图 + 中点分配 + 配额)
// ---------------------------------------------------------------------------

static uint8 *freemap;      // 位图, 1=空闲
static uint64 npages;       // 管理页总数
static uint64 first_pa;     // 第一个可分配物理页地址(位图之后)
static uint64 lo_free;      // 最低空闲页下标(维护)
static uint64 hi_free;      // 最高空闲页下标(维护)
static uint64 nfree;        // 当前空闲页数
static struct spinlock kmem_lock;

#define NONE  ((uint64)-1)
#define BIT_INDEX(pa) (((uint64)(pa) - first_pa) >> PGSHIFT)
#define BIT_TEST(i)   (freemap[(i) >> 3] & (1 << ((i) & 7)))
#define BIT_SET(i)    (freemap[(i) >> 3] |= (1 << ((i) & 7)))
#define BIT_CLR(i)    (freemap[(i) >> 3] &= ~(1 << ((i) & 7)))

// 按 LAB3_ALLOC_ORDER 挑选一个空闲页下标; 无空闲返回 NONE。
static uint64
pick_free(void)
{
  uint64 i;

#if LAB3_ALLOC_ORDER == 0
  // 升序: 最低空闲位
  return (lo_free != NONE) ? lo_free : NONE;
#elif LAB3_ALLOC_ORDER == 1
  // 降序: 最高空闲位
  return (hi_free != NONE) ? hi_free : NONE;
#else
  // 中点(本学号 LAB3_ALLOC_ORDER==2): 空闲区间内最接近中点的空闲页
  uint64 mid = (lo_free + hi_free) / 2;
  uint64 range = hi_free - lo_free;
  for (i = 0; i <= range; i++) {
    if (mid >= lo_free + i && BIT_TEST(mid - i))
      return mid - i;
    if (mid + i <= hi_free && BIT_TEST(mid + i))
      return mid + i;
  }
  return NONE;
#endif
}

// 维护 lo_free/hi_free: 使它们精确指向最低/最高空闲位。
static void
fix_bounds(void)
{
  if (nfree == 0) {
    lo_free = NONE;
    hi_free = NONE;
    return;
  }
  while (lo_free < npages && !BIT_TEST(lo_free))
    lo_free++;
  while (hi_free != NONE && !BIT_TEST(hi_free))
    hi_free--;
}

// 回收分页表中所有"已无任何有效映射"的中间页表页(叶子不动, 根不动)。
static void
reclaim_empty(pagetable_t root);

//1.把“哪些物理页空闲、哪些物理页已被占用”的记账工作，交给一个叫位图分配器（bitmap allocator）的模块来负责。
void
kinit(void)
{
  uint64 start = PGROUNDUP((uint64)end);
  uint64 map_bytes = PGROUNDUP((PHYSTOP - start) / PGSIZE / 8);

  initlock(&kmem_lock, "kmem");

  // 位图放在空闲区最前, 位图本身不参与分配。
  freemap = (uint8 *)start;
  first_pa = start + map_bytes;//真正可以分配的物理页的起始地址
  npages = (PHYSTOP - first_pa) / PGSIZE;

  memset(freemap, 0, map_bytes);
  for (uint64 i = 0; i < npages; i++)
    BIT_SET(i);

  nfree = npages;
  lo_free = 0;
  hi_free = npages - 1;
}

// 分配一页(4KB 对齐、已清零); 空闲页数 ≤ LAB3_MEMCAP_DELTA 或耗尽返回 0。
void *
kalloc(void)
{
  uint64 idx;

  acquire(&kmem_lock);
  if (nfree <= LAB3_MEMCAP_DELTA) {
    release(&kmem_lock);
    return 0;
  }//空闲页<=7时直接失败
  idx = pick_free();
  if (idx == NONE) {
    release(&kmem_lock);
    return 0;
  }
  BIT_CLR(idx);
  nfree--;
  if (idx == lo_free || idx == hi_free)
    fix_bounds();
  release(&kmem_lock);

  void *pa = (void *)(first_pa + idx * PGSIZE);
  memset(pa, 0, PGSIZE); // 清零: 数据安全隔离(空页表/空页/程序 bss 依赖)
  return pa;
}

// 释放一页: 先填垃圾数据(排查悬挂指针), 再置回空闲。
void
kfree(void *pa)
{
  if (((uint64)pa % PGSIZE) != 0 || (uint64)pa < first_pa ||
      (uint64)pa >= PHYSTOP)
    panic("kfree");

  memset(pa, 1, PGSIZE); // junk: 释放后若仍被解引用将读到 0x01

  acquire(&kmem_lock);
  uint64 idx = BIT_INDEX(pa);
  if (BIT_TEST(idx))
    panic("kfree: double free");
  BIT_SET(idx);
  nfree++;
  if (lo_free == NONE || idx < lo_free)
    lo_free = idx;
  if (hi_free == NONE || idx > hi_free)
    hi_free = idx;
  release(&kmem_lock);
}

// ---------------------------------------------------------------------------
// 内核页表
// ---------------------------------------------------------------------------

void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // UART0 寄存器(必需)
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // PLIC(必需, 一并覆盖到 0x10000000)
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // 内核代码段: 只读可执行(kernel.ld 已导出 etext, 故可细分权限)
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // 内核数据 + 全部可用物理内存(空闲页/页表页都在这)恒等映射至 PHYSTOP
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // trampoline 映射到最高虚拟地址(用户/内核同一 VA)
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // 每个进程槽位的内核栈(高地址 KSTACK)
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}


//2.建内核页表，把内核镜像 + 全部物理内存恒等映射（地址照抄）
void
kvminit(void)
{
  kernel_pagetable = kvmmake();//建内核页表
}

void
kvminithart(void)
{
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));//打开分页，satp告诉 CPU 当前该使用哪一套页表来进行虚拟地址到物理地址的转换。
  sfence_vma();
}

// ---------------------------------------------------------------------------
// 三级页表基元
// ---------------------------------------------------------------------------

// 返回 va 对应的 PTE 地址; alloc!=0 时按需创建中间页表页。
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pagetable_t)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// 为 [va, va+size) 建立到 [pa, pa+size) 的映射(需页对齐)。
// 中途失败: 回滚已建立的部分映射(不释放调用方物理页)并回收中间页表。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");
  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");
  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0) {
      // 回滚: 拆除本调用已建的 [va, a) 叶子映射(物理页归调用方), 再回收空中间页表。
      uvmunmap(pagetable, va, (a - va) / PGSIZE, 0);
      reclaim_empty(pagetable);
      return -1;
    }
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// 用户地址空间
// ---------------------------------------------------------------------------

pagetable_t
uvmcreate(void)
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0)
      continue;
    if ((*pte & PTE_V) == 0)
      continue;
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE) {
    mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) !=
        0) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    reclaim_empty(pagetable);
  }

  return newsz;
}

// 递归释放页表页(叶子映射须已清空)。
void
freewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

static void
reclaim_empty(pagetable_t root)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = root[i];
    if (!(pte & PTE_V))
      continue;
    if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      pagetable_t child = (pagetable_t)PTE2PA(pte);
      reclaim_empty(child);
      int empty = 1;
      for (int j = 0; j < 512; j++)
        if (child[j] & PTE_V) {
          empty = 0;
          break;
        }
      if (empty) {
        root[i] = 0;
        kfree((void *)child);
      }
    }
  }
}

void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    // guard page 等未映射空洞: 跳过(子进程同样保持该空洞)。
    if ((pte = walk(old, i, 0)) == 0)
      continue;
    if ((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// ---------------------------------------------------------------------------
// 受控拷贝(逐页校验地址合法性与 PTE_U 权限)
// ---------------------------------------------------------------------------

int
copyout(pagetable_t pagetable, uint64 psz, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  (void)psz;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    pte = walk(pagetable, va0, 0);
    if ((*pte & PTE_W) == 0)
      return -1; // 禁止写入只读(代码)页
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

int
copyin(pagetable_t pagetable, uint64 psz, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  (void)psz;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

int
copyinstr(pagetable_t pagetable, uint64 psz, char *dst, uint64 srcva,
          uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  (void)psz;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
    return 0;
  return -1;
}

// ---------------------------------------------------------------------------
// 页表观测(dump_pagetable-ABI.md)
// ---------------------------------------------------------------------------

static void
dump_level(pagetable_t pt, int level, uint64 va_base)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pt[i];
    if (!(pte & PTE_V))
      continue;

    uint64 va = va_base | ((uint64)i << (PGSHIFT + 9 * level));
    uint64 pa = PTE2PA(pte);

    if (level > 0 && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // 非叶表项: 指向下一级页表
      printf("L%d va=0x%lx pa=0x%lx perm=----\n", level, va, pa);
      dump_level((pagetable_t)pa, level - 1, va);
    } else {
      // 叶子表项
      char p[5];
      p[0] = (pte & PTE_R) ? 'r' : '-';
      p[1] = (pte & PTE_W) ? 'w' : '-';
      p[2] = (pte & PTE_X) ? 'x' : '-';
      p[3] = (pte & PTE_U) ? 'u' : '-';
      p[4] = 0;
      printf("LEAF va=0x%lx pa=0x%lx perm=%s\n", va, pa, p);
    }
  }
}

// 打印指定进程页表结构(与 lab0 地址空间快照图对齐)。进程无关, 可现场重放。
void
dump_pagetable(pagetable_t pagetable)
{
  printf("DUMP-PAGETABLE begin\n");
  dump_level(pagetable, 2, 0);
  printf("DUMP-PAGETABLE end\n");
}
