# lab2 Makefile —— 在 lab1 基础上并入 Makefile.upgrade 的用户程序内嵌方案。
#
# 三固定项不变: make kernel / make qemu / course_sid.h 依赖。
# 新增: 用户程序 UPROGS 编为平铺二进制, 经 userimg.S 内嵌进内核。

CC   = riscv64-unknown-elf-gcc
LD   = riscv64-unknown-elf-ld
OBJCOPY ?= riscv64-unknown-elf-objcopy
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie
QEMU = qemu-system-riscv64

OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o \
       kernel/string.o kernel/spinlock.o kernel/vm.o kernel/proc.o \
       kernel/trap.o kernel/syscall.o kernel/sysproc.o kernel/sysfile.o \
       kernel/exec.o kernel/plic.o kernel/swtch.o kernel/kernelvec.o \
       kernel/trampoline.o kernel/main.o kernel/userimg.o

kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c $(wildcard kernel/*.h)
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# ---------- 用户程序内嵌方案(Makefile.upgrade 规则, 整段照抄) ----------

UPROGS = sh hi spin badecall bufstorm

UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc \
         -fno-stack-protector -fno-pie -I.

# usys 桩由 perl 脚本生成(课程赠送, 勿改)
user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

# 用户程序 → 链接(基址 0)→ 平铺二进制
user-flat/%.bin: user/%.c user/user.h user/ulib.c user/printf.c user/usys.S
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c user/ulib.c   -o user-flat/ulib.o
	$(CC) $(UFLAGS) -Iuser -c user/printf.c -o user-flat/printf.o
	$(CC) $(UFLAGS) -Iuser -c user/usys.S   -o user-flat/usys.o
	$(CC) $(UFLAGS) -Iuser -c $<            -o user-flat/$*.o
	$(LD) -T user/user.ld -o user-flat/$*.elf \
	    user-flat/$*.o user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

# 内嵌程序表(ABI, 勿改): 每项 = .quad start; .quad end; .asciz "name"; 以 {0,0} 结尾
kernel/userimg.S: Makefile $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_table\n_uprog_table:\n' > $@.tmp
	for p in $(UPROGS); do \
	  printf ' .quad _uprog_%s_start\n .quad _uprog_%s_end\n .asciz "%s"\n .balign 8\n' \
	    $$p $$p $$p >> $@.tmp; \
	  printf ' .global _uprog_%s_start\n_uprog_%s_start:\n .incbin "user-flat/%s.bin"\n .global _uprog_%s_end\n_uprog_%s_end:\n .balign 8\n' \
	    $$p $$p $$p $$p $$p >> $@.tmp; \
	done
	printf ' .quad 0\n .quad 0\n' >> $@.tmp
	mv $@.tmp $@

# ---------- 运行与清理 ----------

# 验收环境固定(排雷环节禁止改动本行以下内容)
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

clean:
	rm -f kernel/*.o kernel/kernel kernel/userimg.S user/usys.S
	rm -rf user-flat

clean-extra:
	rm -rf user-flat kernel/userimg.S user/usys.S
