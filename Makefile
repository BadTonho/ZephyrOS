# Makefile para ZephyrOS

SHELL = cmd.exe

# Ferramentas
# Os caminhos locais podem ficar em Makefile.local, que nao e versionado.
# Sem esse arquivo, as ferramentas sao procuradas no PATH do sistema.
-include Makefile.local

NASM ?= nasm
GCC ?= i686-elf-gcc
LD ?= i686-elf-ld
QEMU ?= qemu-system-i386
QEMU_NET_ARGS ?= -nic user,model=e1000

# Flags
CFLAGS = -m32 -O2 -fno-strict-aliasing -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -I src/include -I src/include/core -I src/include/drivers -I src/include/fs -I src/include/memory -I src/include/process -I src/include/apps -I src/include/ui
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

RECOVERY_C = src/core/recovery.c
RECOVERY_OBJ = build/recovery.o

CRYPTO_C = src/core/crypto.c
CRYPTO_OBJ = build/crypto.o

CRYPTO_ED25519_C = src/core/crypto_ed25519.c
CRYPTO_ED25519_OBJ = build/crypto_ed25519.o

UPDATE_C = src/core/update.c
UPDATE_OBJ = build/update.o

DEVICE_MANAGER_C = src/core/device_manager.c
DEVICE_MANAGER_OBJ = build/device_manager.o

NETWORK_MANAGER_C = src/core/network_manager.c
NETWORK_MANAGER_OBJ = build/network_manager.o

ETHERNET_C = src/core/ethernet.c
ETHERNET_OBJ = build/ethernet.o

ARP_C = src/core/arp.c
ARP_OBJ = build/arp.o

IPV4_C = src/core/ipv4.c
IPV4_OBJ = build/ipv4.o

ICMP_C = src/core/icmp.c
ICMP_OBJ = build/icmp.o

UDP_C = src/core/udp.c
UDP_OBJ = build/udp.o

DHCP_C = src/core/dhcp.c
DHCP_OBJ = build/dhcp.o

DNS_C = src/core/dns.c
DNS_OBJ = build/dns.o

TCP_C = src/core/tcp.c
TCP_OBJ = build/tcp.o

NET_SOCKET_C = src/core/net_socket.c
NET_SOCKET_OBJ = build/net_socket.o

HTTP_C = src/core/http.c
HTTP_OBJ = build/http.o

POWER_C = src/core/power.c
POWER_OBJ = build/power.o

STRING_C = src/core/string.c
STRING_OBJ = build/string.o

APP_API_C = src/core/app_api.c
APP_API_OBJ = build/app_api.o

APP_FILES_C = src/core/app_files.c
APP_FILES_OBJ = build/app_files.o

APP_LOADER_C = src/core/app_loader.c
APP_LOADER_OBJ = build/app_loader.o

APP_BUILTIN_C = src/core/app_builtin.c
APP_BUILTIN_OBJ = build/app_builtin.o

APP_PACKAGE_C = src/core/app_package.c
APP_PACKAGE_OBJ = build/app_package.o

SYSCALL_C = src/core/syscall.c
SYSCALL_OBJ = build/syscall.o

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

E1000_C = src/drivers/e1000.c
E1000_OBJ = build/e1000.o

RTL8139_C = src/drivers/rtl8139.c
RTL8139_OBJ = build/rtl8139.o

AC97_C = src/drivers/ac97.c
AC97_OBJ = build/ac97.o

ACPI_C = src/drivers/acpi.c
ACPI_OBJ = build/acpi.o

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

GUITEST_C = src/shell/guitest_app.c
GUITEST_OBJ = build/guitest_app.o


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

# A imagem de boot reserva os primeiros setores para o stage2 e o kernel.
# O volume FAT12 fica depois dessa area para que o Explorer nunca sobrescreva
# o codigo usado no proximo boot. A reserva e calculada pelo payload real.
FAT12_DISK_BYTES = 1474560

# Todas as variáveis de objetos
OBJS = $(ENTRY_OBJ) $(KERNEL_OBJ) $(PANIC_OBJ) $(LOG_OBJ) $(RECOVERY_OBJ) $(CRYPTO_OBJ) $(CRYPTO_ED25519_OBJ) $(UPDATE_OBJ) $(STRING_OBJ) $(APP_API_OBJ) $(SYSCALL_OBJ) $(SWITCH_OBJ) \
       $(VIDEO_OBJ) $(VESA_OBJ) $(FONT_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(IRQ_OBJ) $(KEYBOARD_OBJ) \
       $(MOUSE_OBJ) $(TIMER_OBJ) $(TSS_OBJ) $(ATA_OBJ) $(SPEAKER_OBJ) $(PCI_OBJ) $(E1000_OBJ) $(RTL8139_OBJ) $(AC97_OBJ) $(ACPI_OBJ) \
       $(MEMORY_OBJ) $(PAGING_OBJ) $(COMPRESS_OBJ) \
       $(FAT12_OBJ) $(FAT32_OBJ) $(FS_OBJ) $(WAV_OBJ) $(BMP_OBJ) $(PROCESS_OBJ) $(IPC_OBJ) $(THREAD_OBJ) $(SHELL_OBJ) $(TASKMGR_OBJ) $(MEDIAPLAYER_OBJ) $(EDITOR_OBJ) $(GUITEST_OBJ) $(FILEMANAGER_OBJ) $(TASKBAR_OBJ) $(DESKTOP_OBJ) $(SETTINGS_OBJ) $(WM_OBJ) $(ICONS_OBJ) $(GUI_OBJ) $(APP_FILES_OBJ) $(APP_LOADER_OBJ) $(APP_BUILTIN_OBJ) $(APP_PACKAGE_OBJ) $(DEVICE_MANAGER_OBJ) $(NETWORK_MANAGER_OBJ) $(POWER_OBJ) $(ETHERNET_OBJ) $(ARP_OBJ) $(IPV4_OBJ) $(ICMP_OBJ) $(UDP_OBJ) $(DHCP_OBJ) $(DNS_OBJ) $(TCP_OBJ) $(NET_SOCKET_OBJ) $(HTTP_OBJ)

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

$(RECOVERY_OBJ): $(RECOVERY_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(CRYPTO_OBJ): $(CRYPTO_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(CRYPTO_ED25519_OBJ): $(CRYPTO_ED25519_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_OBJ): $(UPDATE_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(DEVICE_MANAGER_OBJ): $(DEVICE_MANAGER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(NETWORK_MANAGER_OBJ): $(NETWORK_MANAGER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ETHERNET_OBJ): $(ETHERNET_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ARP_OBJ): $(ARP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(IPV4_OBJ): $(IPV4_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ICMP_OBJ): $(ICMP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UDP_OBJ): $(UDP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(DHCP_OBJ): $(DHCP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(DNS_OBJ): $(DNS_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TCP_OBJ): $(TCP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(NET_SOCKET_OBJ): $(NET_SOCKET_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(HTTP_OBJ): $(HTTP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(POWER_OBJ): $(POWER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(STRING_OBJ): $(STRING_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_API_OBJ): $(APP_API_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_FILES_OBJ): $(APP_FILES_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_LOADER_OBJ): $(APP_LOADER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_BUILTIN_OBJ): $(APP_BUILTIN_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_PACKAGE_OBJ): $(APP_PACKAGE_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SYSCALL_OBJ): $(SYSCALL_C)
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

$(E1000_OBJ): $(E1000_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(RTL8139_OBJ): $(RTL8139_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(AC97_OBJ): $(AC97_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(ACPI_OBJ): $(ACPI_C)
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

$(GUITEST_OBJ): $(GUITEST_C)
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

$(OS_IMG): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) tools\packager.py \
          assets\icons\SHELL.BMP assets\icons\EXPLORER.BMP assets\icons\TASKMGR.BMP \
          docs\fixtures\updates\u2\VALID.ZUP docs\fixtures\updates\u2\TRUNC.ZUP \
          docs\fixtures\updates\u2\BADHASH.ZUP docs\fixtures\updates\u2\BADSIG.ZUP \
          docs\fixtures\updates\u2\BADVER.ZUP docs\fixtures\updates\u2\BADFMT.ZUP \
          docs\fixtures\updates\u2\UNKKEY.ZUP docs\fixtures\updates\u3\APPLY.ZUP
	cmd /c "copy /b build\boot.bin+build\stage2.bin+build\kernel.bin build\zephyros.img"
	python tools\packager.py prepare-image --image $(OS_IMG) --disk-bytes $(FAT12_DISK_BYTES)
	python tools\packager.py inject-file --file assets\icons\SHELL.BMP --image $(OS_IMG) --fat-name SHELL.BMP
	python tools\packager.py inject-file --file assets\icons\EXPLORER.BMP --image $(OS_IMG) --fat-name EXPLORER.BMP
	python tools\packager.py inject-file --file assets\icons\TASKMGR.BMP --image $(OS_IMG) --fat-name TASKMGR.BMP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\VALID.ZUP --image $(OS_IMG) --fat-name VALID.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\TRUNC.ZUP --image $(OS_IMG) --fat-name TRUNC.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADHASH.ZUP --image $(OS_IMG) --fat-name BADHASH.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADSIG.ZUP --image $(OS_IMG) --fat-name BADSIG.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADVER.ZUP --image $(OS_IMG) --fat-name BADVER.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADFMT.ZUP --image $(OS_IMG) --fat-name BADFMT.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\UNKKEY.ZUP --image $(OS_IMG) --fat-name UNKKEY.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u3\APPLY.ZUP --image $(OS_IMG) --fat-name APPLY.ZUP

run: $(OS_IMG)
	$(QEMU) -drive format=raw,file=$(OS_IMG) $(QEMU_NET_ARGS)

debug: $(OS_IMG)
	$(QEMU) -drive format=raw,file=$(OS_IMG) $(QEMU_NET_ARGS) -s -S &

q3check:
	python tools\q3check.py

q3check-test:
	python tools\q3check.py --self-test

package-test:
	python tools\packager.py selftest

update-test:
	python tools\updater.py selftest

package-demo: $(OS_IMG)
	python tools\packager.py demo --output build\DEMO.zephyrosapp --image $(OS_IMG)

clean:
	rmdir /s /q build

.PHONY: all run debug q3check q3check-test package-test update-test package-demo clean
