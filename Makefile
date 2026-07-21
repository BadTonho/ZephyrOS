# Makefile para ZephyrOS

SHELL = cmd.exe

# Ferramentas
NASM = "C:\Users\Admin\AppData\Local\bin\NASM\nasm.exe"
GCC = D:\code\i686-elf-tools-windows\bin\i686-elf-gcc.exe
LD = D:\code\i686-elf-tools-windows\bin\i686-elf-ld.exe
QEMU = "C:\Program Files\QEMU\qemu-system-i386.exe"

# Flags
CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -I src/include -I src/include/core -I src/include/drivers -I src/include/fs -I src/include/memory -I src/include/process -I src/include/apps -I src/include/ui
LDFLAGS = -m elf_i386 -T src/linker.ld
NASMFLAGS = -f bin

# Arquivos - Boot
BOOT_SRC = src/boot/boot.asm
BOOT_BIN = build/boot.bin
STAGE2_SRC = src/boot/stage2.asm
STAGE2_BIN = build/stage2.bin

# Arquivos - Kernel
ENTRY_SRC = src/kernel/entry.asm
ENTRY_OBJ = build/entry.o

KERNEL_C = src/kernel/kernel.c
KERNEL_OBJ = build/kernel.o

PANIC_C = src/kernel/panic.c
PANIC_OBJ = build/panic.o

LOG_C = src/core/log.c
LOG_OBJ = build/log.o

STRING_C = src/core/string.c
STRING_OBJ = build/string.o

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

MOUSE_C = src/drivers/mouse.c
MOUSE_OBJ = build/mouse.o

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

COMPRESS_C = src/memory/compress.c
COMPRESS_OBJ = build/compress.o

# Arquivos - Sistema de Arquivos
FAT12_C = src/fs/fat12.c
FAT12_OBJ = build/fat12.o

FAT32_C = src/fs/fat32.c
FAT32_OBJ = build/fat32.o

FS_C = src/fs/fs.c
FS_OBJ = build/fs.o

WAV_C = src/fs/wav.c
WAV_OBJ = build/wav.o

BMP_C = src/fs/bmp.c
BMP_OBJ = build/bmp.o

# Arquivos - Processos
PROCESS_C = src/process/process.c
PROCESS_OBJ = build/process.o
IPC_C = src/process/ipc.c
IPC_OBJ = build/ipc.o


# Arquivos - Threads
THREAD_C = src/thread/thread.c
THREAD_OBJ = build/thread.o

# Arquivos - Shell
SHELL_C = src/shell/shell.c
SHELL_OBJ = build/shell.o

TASKMGR_C = src/shell/taskmanager.c
TASKMGR_OBJ = build/taskmanager.o

MEDIAPLAYER_C = src/shell/mediaplayer.c
MEDIAPLAYER_OBJ = build/mediaplayer.o

EDITOR_C = src/shell/editor.c
EDITOR_OBJ = build/editor.o

# Arquivos - File Manager
FILEMANAGER_C = src/filemanager/filemanager.c
FILEMANAGER_OBJ = build/filemanager.o

# Arquivos - Taskbar
TASKBAR_C = src/taskbar/taskbar.c
TASKBAR_OBJ = build/taskbar.o

# Arquivos - Desktop
DESKTOP_C = src/desktop/desktop.c
DESKTOP_OBJ = build/desktop.o

# Arquivos - Settings
SETTINGS_C = src/settings/settings.c
SETTINGS_OBJ = build/settings.o

# Arquivos - Window Manager
WM_C = src/wm/wm.c
WM_OBJ = build/wm.o

# Arquivos - Icons
ICONS_C = src/icons/icons.c
ICONS_OBJ = build/icons.o

# Arquivos - GUI Gráfica
GUI_C = src/gui/gui.c
GUI_OBJ = build/gui.o


# Output
KERNEL_BIN = build/kernel.bin
OS_IMG = build/zephyros.img

# Todas as variáveis de objetos
OBJS = $(ENTRY_OBJ) $(KERNEL_OBJ) $(PANIC_OBJ) $(LOG_OBJ) $(STRING_OBJ) $(SWITCH_OBJ) \
       $(VIDEO_OBJ) $(VESA_OBJ) $(FONT_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(IRQ_OBJ) $(KEYBOARD_OBJ) \
       $(MOUSE_OBJ) $(TIMER_OBJ) $(TSS_OBJ) $(ATA_OBJ) $(SPEAKER_OBJ) $(PCI_OBJ) $(AC97_OBJ) \
       $(MEMORY_OBJ) $(PAGING_OBJ) $(COMPRESS_OBJ) \
       $(FAT12_OBJ) $(FAT32_OBJ) $(FS_OBJ) $(WAV_OBJ) $(BMP_OBJ) $(PROCESS_OBJ) $(IPC_OBJ) $(THREAD_OBJ) $(SHELL_OBJ) $(TASKMGR_OBJ) $(MEDIAPLAYER_OBJ) $(EDITOR_OBJ) $(FILEMANAGER_OBJ) $(TASKBAR_OBJ) $(DESKTOP_OBJ) $(SETTINGS_OBJ) $(WM_OBJ) $(ICONS_OBJ) $(GUI_OBJ)

# Targets
all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_SRC) $(STAGE2_BIN)
	@if not exist build mkdir build
	for /f %%S in ('powershell -NoProfile -Command "$$size = (Get-Item '$(STAGE2_BIN)').Length; [math]::Ceiling($$size / 512)"') do $(NASM) $(NASMFLAGS) -dSTAGE2_SECTORS=%%S $< -o $@

$(STAGE2_BIN): $(STAGE2_SRC) $(KERNEL_BIN)
	@if not exist build mkdir build
	for /f %%S in ('powershell -NoProfile -Command "$$size = (Get-Item '$(KERNEL_BIN)').Length; [math]::Ceiling($$size / 512)"') do $(NASM) $(NASMFLAGS) -dKERNEL_SECTORS=%%S $< -o $@

$(ENTRY_OBJ): $(ENTRY_SRC)
	@if not exist build mkdir build
	$(NASM) -f elf32 $< -o $@

$(KERNEL_OBJ): $(KERNEL_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PANIC_OBJ): $(PANIC_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(LOG_OBJ): $(LOG_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(STRING_OBJ): $(STRING_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SWITCH_OBJ): $(SWITCH_ASM)
	@if not exist build mkdir build
	$(NASM) -f elf32 $< -o $@

$(VIDEO_OBJ): $(VIDEO_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(VESA_OBJ): $(VESA_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FONT_OBJ): $(FONT_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(IDT_OBJ): $(IDT_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ISR_OBJ): $(ISR_ASM)
	@if not exist build mkdir build
	$(NASM) -f elf32 $< -o $@

$(IRQ_OBJ): $(IRQ_ASM)
	@if not exist build mkdir build
	$(NASM) -f elf32 $< -o $@

$(KEYBOARD_OBJ): $(KEYBOARD_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(MOUSE_OBJ): $(MOUSE_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TIMER_OBJ): $(TIMER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TSS_OBJ): $(TSS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ATA_OBJ): $(ATA_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SPEAKER_OBJ): $(SPEAKER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PCI_OBJ): $(PCI_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(AC97_OBJ): $(AC97_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(MEMORY_OBJ): $(MEMORY_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PAGING_OBJ): $(PAGING_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(COMPRESS_OBJ): $(COMPRESS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT12_OBJ): $(FAT12_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT32_OBJ): $(FAT32_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FS_OBJ): $(FS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(WAV_OBJ): $(WAV_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(BMP_OBJ): $(BMP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PROCESS_OBJ): $(PROCESS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(IPC_OBJ): $(IPC_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(THREAD_OBJ): $(THREAD_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_OBJ): $(SHELL_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TASKMGR_OBJ): $(TASKMGR_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(MEDIAPLAYER_OBJ): $(MEDIAPLAYER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(EDITOR_OBJ): $(EDITOR_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FILEMANAGER_OBJ): $(FILEMANAGER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TASKBAR_OBJ): $(TASKBAR_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(DESKTOP_OBJ): $(DESKTOP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SETTINGS_OBJ): $(SETTINGS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(WM_OBJ): $(WM_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ICONS_OBJ): $(ICONS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(GUI_OBJ): $(GUI_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@


$(KERNEL_BIN): $(OBJS) src/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(OS_IMG): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	cmd /c "copy /b build\boot.bin+build\stage2.bin+build\kernel.bin build\zephyros.img"

run: $(OS_IMG)
	$(QEMU) -drive format=raw,file=$(OS_IMG)

debug: $(OS_IMG)
	$(QEMU) -drive format=raw,file=$(OS_IMG) -s -S &

clean:
	rmdir /s /q build

.PHONY: all run debug clean
