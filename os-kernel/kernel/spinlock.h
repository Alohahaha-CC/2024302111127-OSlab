// lab2 spinlock.h —— 互斥自旋锁。
// 单核 + 中断是 lab2 唯一的并发来源; 锁的临界区同时关中断(push_off/pop_off),
// 即可避免"进程持有锁时被中断打断 → 中断里再抢同一把锁"的死锁。

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

struct spinlock {
  uint locked;        // 是否被持有
  char *name;         // 调试名
};

#endif // SPINLOCK_H
