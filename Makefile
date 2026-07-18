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

# Arquivos - Boot
BOOT_SRC = src/boot/boot.asm
BOOT_BIN = build/boot.bin

# Arquivos - Kernel
ENTRY_SRC = src/kernel/entry.asm
ENTRY_OBJ = build/entry.o

KERNEL_C = src/kernel/kernel.c
KERNEL_OBJ = build/kernel.o

PANIC_C = src/kernel/panic.c
PANIC_OBJ = build/panic.o

SWITCH_ASM = src/kernel/switch.asm
SWITCH_OBJ = build/switch.o

# Arquivos - Drivers
VIDEO_C = src/drivers/video.c
VIDEO_OBJ = build/video.o

VESA_C = src/drivers/vesa.c
VESA_OBJ = build/vesa.o

FONT_C = src/drivers/font.c
FONT_OBJ = build/font.o

IDT_C = src/drivers/idt.c
IDT_OBJ = build/idt.o

ISR_ASM = src/drivers/isr.asm
ISR_OBJ = build/isr.o

IRQ_ASM = src/drivers/irq.asm
IRQ_OBJ = build/irq.o

KEYBOARD_C = src/drivers/keyboard.c
KEYBOARD_OBJ = build/keyboard.o

TIMER_C = src/drivers/timer.c
TIMER_OBJ = build/timer.o

TSS_C = src/drivers/tss.c
TSS_OBJ = build/tss.o

ATA_C = src/drivers/ata.c
ATA_OBJ = build/ata.o

SPEAKER_C = src/drivers/speaker.c
SPEAKER_OBJ = build/speaker.o

PCI_C = src/drivers/pci.c
PCI_OBJ = build/pci.o

AC97_C = src/drivers/ac97.c
AC97_OBJ = build/ac97.o

# Arquivos - Memoria
MEMORY_C = src/memory/memory.c
MEMORY_OBJ = build/memory.o

PAGING_C = src/memory/paging.c
PAGING_OBJ = build/paging.o

# Arquivos - Sistema de Arquivos
FAT12_C = src/fs/fat12.c
FAT12_OBJ = build/fat12.o

FAT32_C = src/fs/fat32.c
FAT32_OBJ = build/fat32.o

FS_C = src/fs/fs.c
FS_OBJ = build/fs.o

# Arquivos - Processos
PROCESS_C = src/process/process.c
PROCESS_OBJ = build/process.o

# Arquivos - Threads
THREAD_C = src/thread/thread.c
THREAD_OBJ = build/thread.o

# Arquivos - Shell
SHELL_C = src/shell/shell.c
SHELL_OBJ = build/shell.o

TASKMGR_C = src/shell/taskmanager.c
TASKMGR_OBJ = build/taskmanager.o

# Arquivos - File Manager
FILEMANAGER_C = src/filemanager/filemanager.c
FILEMANAGER_OBJ = build/filemanager.o

# Arquivos - Taskbar
TASKBAR_C = src/taskbar/taskbar.c
TASKBAR_OBJ = build/taskbar.o

# Arquivos - Desktop
DESKTOP_C = src/desktop/desktop.c
DESKTOP_OBJ = build/desktop.o

# Output
KERNEL_BIN = build/kernel.bin
OS_IMG = build/minios.img

# Todas as variáveis de objetos
OBJS = $(ENTRY_OBJ) $(KERNEL_OBJ) $(PANIC_OBJ) $(SWITCH_OBJ) \
       $(VIDEO_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(IRQ_OBJ) $(KEYBOARD_OBJ) \
       $(TIMER_OBJ) $(TSS_OBJ) $(ATA_OBJ) $(SPEAKER_OBJ) \
       $(MEMORY_OBJ) $(PAGING_OBJ) \
       $(FAT12_OBJ) $(FAT32_OBJ) $(FS_OBJ) $(PROCESS_OBJ) $(THREAD_OBJ) $(SHELL_OBJ) $(TASKMGR_OBJ) $(FILEMANAGER_OBJ) $(TASKBAR_OBJ) $(DESKTOP_OBJ)

# Targets
all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_SRC)
	@mkdir -p build
	$(NASM) $(NASMFLAGS) $< -o $@

$(ENTRY_OBJ): $(ENTRY_SRC)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(KERNEL_OBJ): $(KERNEL_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PANIC_OBJ): $(PANIC_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SWITCH_OBJ): $(SWITCH_ASM)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(VIDEO_OBJ): $(VIDEO_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(VESA_OBJ): $(VESA_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FONT_OBJ): $(FONT_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(IDT_OBJ): $(IDT_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ISR_OBJ): $(ISR_ASM)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(IRQ_OBJ): $(IRQ_ASM)
	@mkdir -p build
	$(NASM) -f elf32 $< -o $@

$(KEYBOARD_OBJ): $(KEYBOARD_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TIMER_OBJ): $(TIMER_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TSS_OBJ): $(TSS_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ATA_OBJ): $(ATA_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SPEAKER_OBJ): $(SPEAKER_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PCI_OBJ): $(PCI_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(AC97_OBJ): $(AC97_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(MEMORY_OBJ): $(MEMORY_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PAGING_OBJ): $(PAGING_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT12_OBJ): $(FAT12_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT32_OBJ): $(FAT32_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FS_OBJ): $(FS_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PROCESS_OBJ): $(PROCESS_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(THREAD_OBJ): $(THREAD_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_OBJ): $(SHELL_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TASKMGR_OBJ): $(TASKMGR_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FILEMANAGER_OBJ): $(FILEMANAGER_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TASKBAR_OBJ): $(TASKBAR_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(DESKTOP_OBJ): $(DESKTOP_C)
	@mkdir -p build
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(OBJS)
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
