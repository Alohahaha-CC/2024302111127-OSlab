// lab2 defs.h —— 跨文件函数声明与通用宏。

struct context;
struct proc;
struct spinlock;
struct trapframe;

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);
int             consoleread(int user_dst, uint64 dst, int n);
int             consolewrite(int user_src, uint64 src, int n);
void            uartintr(void);

// exec.c
int             kexec(char *path);

// kalloc/vm.c
void*           kalloc(void);
void            kfree(void *);
void            kinit(void);
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, uint64, char *, uint64);
int             copyin(pagetable_t, uint64, char *, uint64, uint64);
int             copyinstr(pagetable_t, uint64, char *, uint64, uint64);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
void            proc_mapstacks(pagetable_t);

// proc.c
int             cpuid(void);
struct cpu*     mycpu(void);
void            kexit(int);
int             kfork(void);
int             kkill(int);
int             killed(struct proc *);
void            setkilled(struct proc *);
struct proc*    myproc(void);
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep_prepare(void *);
void            sleep(void);
void            userinit(void);
int             kwait(uint64);
void            wakeup(void *);
void            yield(void);
int             either_copyout(int, uint64, void *, uint64);
int             either_copyin(void *, int, uint64, uint64);

// swtch.S
void            swtch(struct context *, struct context *);

// spinlock.c
void            acquire(struct spinlock *);
int             holding(struct spinlock *);
void            initlock(struct spinlock *, char *);
void            release(struct spinlock *);
void            push_off(void);
void            pop_off(void);

// string.c
int             memcmp(const void *, const void *, uint);
void*           memmove(void *, const void *, uint);
void*           memset(void *, int, uint);
char*           safestrcpy(char *, const char *, int);
int             strlen(const char *);
int             strncmp(const char *, const char *, uint);
char*           strncpy(char *, const char *, int);

// syscall.c
void            argint(int, int *);
int             argstr(int, char *, int);
void            argaddr(int, uint64 *);
int             fetchstr(uint64, char *, int);
int             fetchaddr(uint64, uint64 *);
void            syscall(void);

// trap.c
extern uint     ticks;
extern struct spinlock tickslock;
void            trapinit(void);
void            trapinithart(void);
void            usertrapret(void);
void            kerneltrap(void);
void            clockintr(void);
int             devintr(void);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// printf.c
int             printf(const char *, ...);
int             snprintf(char *, int, const char *, ...);
void            panic(char *) __attribute__((noreturn));

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
