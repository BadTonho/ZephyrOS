# Makefile para MiniOS

# Ferramentas
NASM = nasm
GCC = gcc
LD = ld
QEMU = qemu-system-i386

# Flags
CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -I src/include
LDFLAGS = -m elf_i386 -T src/linker.ld
NASMFLAGS = -f bin

# Arquivos
BOOT_SRC = src/boot/boot.asm
BOOT_BIN = build/boot.bin

ENTRY_SRC = src/kernel/entry.asm
ENTRY_OBJ = build/entry.o

KERNEL_C = src/kernel/kernel.c
KERNEL_OBJ = build/kernel.o

VIDEO_C = src/kernel/video.c
VIDEO_OBJ = build/video.o

KERNEL_BIN = build/kernel.bin
OS_IMG = build/minios.img

# Targets
all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_SRC)
	@mkdir -p build
	$(NASM) $(NASMFLAGS) $< -o $@

$(ENTRY_OBJ): $(ENTRY_SRC)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(VIDEO_OBJ): $(VIDEO_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_OBJ): $(KERNEL_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(ENTRY_OBJ) $(KERNEL_OBJ) $(VIDEO_OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	cat $^ > $@

run: $(OS_IMG)
	$(QEMU) -drive format=raw,file=$(OS_IMG)

debug: $(OS_IMG)
	$(QEMU) -drive format=raw,file=$(OS_IMG) -s -S &

clean:
	rm -rf build/*

.PHONY: all run debug clean
