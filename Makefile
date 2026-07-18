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

PANIC_C = src/kernel/panic.c
PANIC_OBJ = build/panic.o

IDT_C = src/kernel/idt.c
IDT_OBJ = build/idt.o

ISR_ASM = src/kernel/isr.asm
ISR_OBJ = build/isr.o

IRQ_ASM = src/kernel/irq.asm
IRQ_OBJ = build/irq.o

KEYBOARD_C = src/kernel/keyboard.c
KEYBOARD_OBJ = build/keyboard.o

TIMER_C = src/kernel/timer.c
TIMER_OBJ = build/timer.o

MEMORY_C = src/kernel/memory.c
MEMORY_OBJ = build/memory.o

PAGING_C = src/kernel/paging.c
PAGING_OBJ = build/paging.o

TSS_C = src/kernel/tss.c
TSS_OBJ = build/tss.o

PROCESS_C = src/kernel/process.c
PROCESS_OBJ = build/process.o

SWITCH_ASM = src/kernel/switch.asm
SWITCH_OBJ = build/switch.o

ATA_C = src/kernel/ata.c
ATA_OBJ = build/ata.o

FAT12_C = src/kernel/fat12.c
FAT12_OBJ = build/fat12.o

SHELL_C = src/kernel/shell.c
SHELL_OBJ = build/shell.o

SPEAKER_C = src/kernel/speaker.c
SPEAKER_OBJ = build/speaker.o

THREAD_C = src/kernel/thread.c
THREAD_OBJ = build/thread.o

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

$(PANIC_OBJ): $(PANIC_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ISR_OBJ): $(ISR_ASM)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(IRQ_OBJ): $(IRQ_ASM)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(IDT_OBJ): $(IDT_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(KEYBOARD_OBJ): $(KEYBOARD_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TIMER_OBJ): $(TIMER_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(MEMORY_OBJ): $(MEMORY_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PAGING_OBJ): $(PAGING_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TSS_OBJ): $(TSS_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PROCESS_OBJ): $(PROCESS_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SWITCH_OBJ): $(SWITCH_ASM)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(ATA_OBJ): $(ATA_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT12_OBJ): $(FAT12_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_OBJ): $(SHELL_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SPEAKER_OBJ): $(SPEAKER_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(THREAD_OBJ): $(THREAD_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(ENTRY_OBJ) $(KERNEL_OBJ) $(VIDEO_OBJ) $(PANIC_OBJ) $(ISR_OBJ) $(IRQ_OBJ) $(IDT_OBJ) $(KEYBOARD_OBJ) $(TIMER_OBJ) $(MEMORY_OBJ) $(PAGING_OBJ) $(TSS_OBJ) $(PROCESS_OBJ) $(SWITCH_OBJ) $(ATA_OBJ) $(FAT12_OBJ) $(SHELL_OBJ) $(SPEAKER_OBJ) $(THREAD_OBJ)
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
