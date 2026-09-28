// lab2 spinlock.c —— 自旋锁 + 关中断嵌套(push_off/pop_off)。
//
// 单核环境下, 并发的唯一来源是中断。push_off 关中断并计数(每 CPU 一份),
// pop_off 计数归零后恢复中断; acquire/release 叠加其上, 保证临界区内中断关闭,
// 杜绝"持有锁时被中断打断 → 中断里再抢同一把锁"的重入死锁。

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

// 关中断并计数。
void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if (mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

// 恢复中断(仅当嵌套深度归零)。
void
pop_off(void)
{
  if (intr_get())
    panic("pop_off - interruptible");
  if (mycpu()->noff < 1)
    panic("pop_off");
  mycpu()->noff -= 1;
  if (mycpu()->noff == 0 && mycpu()->intena)
    intr_on();
}

void
initlock(struct spinlock *lk, char *name)
{
  lk->locked = 0;
  lk->name = name;
}

// 加锁: 先关中断, 再原子抢锁(单核 + 关中断 = 无竞争)。
void
acquire(struct spinlock *lk)
{
  push_off();

  if (holding(lk))
    panic("acquire");

  while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;
  __sync_synchronize();
}

void
release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release");

  __sync_synchronize();
  __sync_lock_release(&lk->locked);

  pop_off();
}

int
holding(struct spinlock *lk)
{
  return lk->locked;
}
