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
QEMU_CPU_ARGS ?= -cpu max
QEMU_NET_ARGS ?= -nic user,model=e1000
QEMU_BOOT_DISK_ARGS ?= -drive file=$(OS_IMG),format=raw,if=none,id=bootdisk -device ide-hd,drive=bootdisk,cyls=80,heads=2,secs=18,bootindex=1
QEMU_USB_ARGS ?= -device piix3-usb-uhci,id=usb
QEMU_USB_DEVICE_ARGS ?= -device usb-kbd,bus=usb.0
QEMU_USB_HID_DEVICE_ARGS ?= -device usb-kbd,bus=usb.0,port=1 -device usb-mouse,bus=usb.0,port=2
QEMU_USB_MSC_ARGS ?= -drive if=none,id=usb-stick,format=raw,file=$(STORAGE_VALID_IMG),readonly=on -device usb-storage,bus=usb.0,drive=usb-stick

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

INPUT_C = src/core/input.c
INPUT_OBJ = build/input.o

IRQ_DEFERRED_C = src/core/irq_deferred.c
IRQ_DEFERRED_OBJ = build/irq_deferred.o

WAIT_C = src/core/wait.c
WAIT_OBJ = build/wait.o

CLOCK_C = src/core/clock.c
CLOCK_OBJ = build/clock.o

TLS_C = src/core/tls.c
TLS_OBJ = build/tls.o

TLS_CLIENT_C = src/core/tls_client.c
TLS_CLIENT_OBJ = build/tls_client.o

BEARSSL_COMPAT_C = src/core/bearssl_compat.c
BEARSSL_COMPAT_OBJ = build/bearssl_compat.o

BEARSSL_SRC = $(wildcard vendor/bearssl/src/*.c) \
              $(wildcard vendor/bearssl/src/aead/*.c) \
              $(wildcard vendor/bearssl/src/codec/*.c) \
              $(wildcard vendor/bearssl/src/ec/*.c) \
              $(wildcard vendor/bearssl/src/hash/*.c) \
              $(wildcard vendor/bearssl/src/int/*.c) \
              $(wildcard vendor/bearssl/src/kdf/*.c) \
              $(wildcard vendor/bearssl/src/mac/*.c) \
              $(wildcard vendor/bearssl/src/rand/*.c) \
              $(wildcard vendor/bearssl/src/rsa/*.c) \
              $(wildcard vendor/bearssl/src/ssl/*.c) \
              $(wildcard vendor/bearssl/src/symcipher/*.c) \
              $(wildcard vendor/bearssl/src/x509/*.c)
# ZephyrOS usa o perfil TLS cliente restrito a ECDHE + AES-GCM. Os perfis
# BearSSL full e os módulos DES/3DES não entram no artefato do sistema.
BEARSSL_EXCLUDED_SRC = vendor/bearssl/src/ssl/ssl_client_full.c \
                       vendor/bearssl/src/ssl/ssl_engine_default_descbc.c \
                       vendor/bearssl/src/ssl/ssl_server_full_ec.c \
                       vendor/bearssl/src/ssl/ssl_server_full_rsa.c \
                       $(wildcard vendor/bearssl/src/symcipher/des_*.c)
BEARSSL_SRC := $(filter-out $(BEARSSL_EXCLUDED_SRC),$(BEARSSL_SRC))
BEARSSL_OBJ = $(patsubst vendor/bearssl/src/%.c,build/bearssl/%.o,$(BEARSSL_SRC))
BEARSSL_CFLAGS = $(CFLAGS) -I vendor/bearssl/inc -I vendor/bearssl/src -include vendor/bearssl/inc/string.h

RECOVERY_C = src/core/recovery.c
RECOVERY_OBJ = build/recovery.o

CRYPTO_C = src/core/crypto.c
CRYPTO_OBJ = build/crypto.o

CRYPTO_ED25519_C = src/core/crypto_ed25519.c
CRYPTO_ED25519_OBJ = build/crypto_ed25519.o

UPDATE_C = src/core/update.c
UPDATE_OBJ = build/update.o

UPDATE_REMOTE_C = src/core/update_remote.c
UPDATE_REMOTE_OBJ = build/update_remote.o

UPDATE_REMOTE_RELEASE_C = src/core/update_remote_release.c
UPDATE_REMOTE_RELEASE_OBJ = build/update_remote_release.o

UPDATE_REMOTE_GITHUB_C = src/core/update_remote_github.c
UPDATE_REMOTE_GITHUB_OBJ = build/update_remote_github.o

UPDATE_RUNTIME_C = src/core/update_runtime.c
UPDATE_RUNTIME_OBJ = build/update_runtime.o

UPDATE_REMOTE_RUNTIME_C = src/core/update_remote_runtime.c
UPDATE_REMOTE_RUNTIME_OBJ = build/update_remote_runtime.o

DEVICE_MANAGER_C = src/core/device_manager.c
DEVICE_MANAGER_OBJ = build/device_manager.o

USB_MANAGER_C = src/core/usb_manager.c
USB_MANAGER_OBJ = build/usb_manager.o

UHCI_C = src/drivers/uhci.c
UHCI_OBJ = build/uhci.o

USB_MSC_C = src/drivers/usb_msc.c
USB_MSC_OBJ = build/usb_msc.o

USB_HID_C = src/drivers/usb_hid.c
USB_HID_OBJ = build/usb_hid.o

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

APP_REMOTE_C = src/core/app_remote.c
APP_REMOTE_OBJ = build/app_remote.o

APP_CATALOG_C = src/core/app_catalog.c
APP_CATALOG_OBJ = build/app_catalog.o

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

RTC_C = src/drivers/rtc.c
RTC_OBJ = build/rtc.o

RNG_C = src/drivers/rng.c
RNG_OBJ = build/rng.o

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

BLOCK_C = src/fs/block.c
BLOCK_OBJ = build/block.o

STORAGE_C = src/fs/storage.c
STORAGE_OBJ = build/storage.o

FILE_INDEX_C = src/fs/file_index.c
FILE_INDEX_OBJ = build/file_index.o

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

SHELL_INPUT_C = src/shell/shell_input.c
SHELL_INPUT_OBJ = build/shell_input.o

SHELL_DISPATCH_C = src/shell/shell_dispatch.c
SHELL_DISPATCH_OBJ = build/shell_dispatch.o

SHELL_COMMAND_UTILS_C = src/shell/shell_command_utils.c
SHELL_COMMAND_UTILS_OBJ = build/shell_command_utils.o

SHELL_COMMANDS_CORE_C = src/shell/shell_commands_core.c
SHELL_COMMANDS_CORE_OBJ = build/shell_commands_core.o

SHELL_COMMANDS_STORAGE_C = src/shell/shell_commands_storage.c
SHELL_COMMANDS_STORAGE_OBJ = build/shell_commands_storage.o

SHELL_COMMANDS_DIAGNOSTICS_C = src/shell/shell_commands_diagnostics.c
SHELL_COMMANDS_DIAGNOSTICS_OBJ = build/shell_commands_diagnostics.o

SHELL_COMMANDS_NETWORK_C = src/shell/shell_commands_network.c
SHELL_COMMANDS_NETWORK_OBJ = build/shell_commands_network.o

SHELL_CHECKS_C = src/shell/shell_checks.c
SHELL_CHECKS_OBJ = build/shell_checks.o

SHELL_COMMANDS_PACKAGES_C = src/shell/shell_commands_packages.c
SHELL_COMMANDS_PACKAGES_OBJ = build/shell_commands_packages.o

SHELL_COMMANDS_APPS_C = src/shell/shell_commands_apps.c
SHELL_COMMANDS_APPS_OBJ = build/shell_commands_apps.o

SHELL_HOSTED_C = src/shell/shell_hosted.c
SHELL_HOSTED_OBJ = build/shell_hosted.o

SHELL_JOB_C = src/shell/shell_job.c
SHELL_JOB_OBJ = build/shell_job.o

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

# Arquivos - System Updater
UPDATER_C = src/updater/updater.c
UPDATER_OBJ = build/updater.o

# Arquivos - App Store
APPSTORE_C = src/appstore/appstore.c
APPSTORE_OBJ = build/appstore.o

# Arquivos - Window Manager
WM_C = src/wm/wm.c
WM_OBJ = build/wm.o

# Arquivos - Icons
ICONS_C = src/icons/icons.c
ICONS_OBJ = build/icons.o

# Arquivos - GUI Gráfica
GUI_C = src/gui/gui.c
GUI_OBJ = build/gui.o

# Arquivos - Display Layout
DISPLAY_C = src/gui/display.c
DISPLAY_OBJ = build/display.o


# Output
KERNEL_BIN = build/kernel.bin
OS_IMG = build/zephyros.img
STORAGE_FIXTURES_TOOL = tools\storage_fixtures.py
STORAGE_FIXTURES_STAMP = build\storage-fixtures.stamp
STORAGE_VALID_IMG = build\storage-valid.img
STORAGE_CORRUPT_IMG = build\storage-corrupt.img
STORAGE_UNKNOWN_IMG = build\storage-unknown.img

# A imagem de boot reserva os primeiros setores para o stage2 e o kernel.
# O volume FAT12 fica depois dessa area para que o Explorer nunca sobrescreva
# o codigo usado no proximo boot. A reserva e calculada pelo payload real.
FAT12_DISK_BYTES = 1474560
STORE_FIXTURES_DIR = docs\fixtures\apps\store
STORE_FIXTURES = $(STORE_FIXTURES_DIR)\VALID.ZPK \
                 $(STORE_FIXTURES_DIR)\BADCRC.ZPK \
                 $(STORE_FIXTURES_DIR)\BADAPI.ZPK \
                 $(STORE_FIXTURES_DIR)\BADALIAS.ZPK \
                 $(STORE_FIXTURES_DIR)\NEEDSDEP.ZPK \
                 $(STORE_FIXTURES_DIR)\SAMEVER.ZPK \
                 $(STORE_FIXTURES_DIR)\fixtures.json
STORE_AS2_FIXTURES_DIR = docs\fixtures\apps\store-as2
STORE_AS2_FIXTURES = $(STORE_AS2_FIXTURES_DIR)\WAITAPP.ZPK \
                     $(STORE_AS2_FIXTURES_DIR)\BASE.ZPK \
                     $(STORE_AS2_FIXTURES_DIR)\DEPEND.ZPK \
                     $(STORE_AS2_FIXTURES_DIR)\fixtures.json
STORE_AS4_SEED_FIXTURES_DIR = docs\fixtures\apps\store-as4-seed
STORE_AS4_UPDATE_FIXTURES_DIR = docs\fixtures\apps\store-as4-update
STORE_AS4_SEED_FIXTURES = $(STORE_AS4_SEED_FIXTURES_DIR)\UPTARGET.ZPK \
                          $(STORE_AS4_SEED_FIXTURES_DIR)\UPDEPA.ZPK \
                          $(STORE_AS4_SEED_FIXTURES_DIR)\UPDEPB.ZPK \
                          $(STORE_AS4_SEED_FIXTURES_DIR)\BROKEN.ZPK \
                          $(STORE_AS4_SEED_FIXTURES_DIR)\CYCLEA.ZPK \
                          $(STORE_AS4_SEED_FIXTURES_DIR)\CYCLEB.ZPK \
                          $(STORE_AS4_SEED_FIXTURES_DIR)\fixtures.json
STORE_AS4_UPDATE_FIXTURES = $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPTARGET.ZPK \
                            $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPA.ZPK \
                            $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPB.ZPK \
                            $(STORE_AS4_UPDATE_FIXTURES_DIR)\BROKEN.ZPK \
                            $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEA.ZPK \
                            $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEB.ZPK \
                            $(STORE_AS4_UPDATE_FIXTURES_DIR)\fixtures.json
STORE_AS5_FIXTURES_DIR = docs\fixtures\apps\store-as5
STORE_AS5_PUBLIC = config\app-store-test-public.json

# Todas as variáveis de objetos
OBJS = $(ENTRY_OBJ) $(KERNEL_OBJ) $(PANIC_OBJ) $(LOG_OBJ) $(INPUT_OBJ) $(IRQ_DEFERRED_OBJ) $(WAIT_OBJ) $(RECOVERY_OBJ) $(CRYPTO_OBJ) $(CRYPTO_ED25519_OBJ) $(BEARSSL_COMPAT_OBJ) $(BEARSSL_OBJ) $(UPDATE_OBJ) $(UPDATE_REMOTE_OBJ) $(UPDATE_REMOTE_RELEASE_OBJ) $(UPDATE_REMOTE_GITHUB_OBJ) $(UPDATE_RUNTIME_OBJ) $(UPDATE_REMOTE_RUNTIME_OBJ) $(STRING_OBJ) $(APP_API_OBJ) $(SYSCALL_OBJ) $(SWITCH_OBJ) \
       $(VIDEO_OBJ) $(VESA_OBJ) $(FONT_OBJ) $(IDT_OBJ) $(ISR_OBJ) $(IRQ_OBJ) $(KEYBOARD_OBJ) \
       $(MOUSE_OBJ) $(TIMER_OBJ) $(TSS_OBJ) $(ATA_OBJ) $(SPEAKER_OBJ) $(PCI_OBJ) $(UHCI_OBJ) $(USB_MSC_OBJ) $(USB_HID_OBJ) $(E1000_OBJ) $(RTL8139_OBJ) $(AC97_OBJ) $(ACPI_OBJ) $(RNG_OBJ) \
       $(MEMORY_OBJ) $(PAGING_OBJ) $(COMPRESS_OBJ) \
       $(FAT12_OBJ) $(FAT32_OBJ) $(FS_OBJ) $(BLOCK_OBJ) $(STORAGE_OBJ) $(FILE_INDEX_OBJ) $(WAV_OBJ) $(BMP_OBJ) $(PROCESS_OBJ) $(IPC_OBJ) $(THREAD_OBJ) $(SHELL_OBJ) $(TASKMGR_OBJ) $(MEDIAPLAYER_OBJ) $(EDITOR_OBJ) $(GUITEST_OBJ) $(FILEMANAGER_OBJ) $(TASKBAR_OBJ) $(DESKTOP_OBJ) $(SETTINGS_OBJ) $(UPDATER_OBJ) $(APPSTORE_OBJ) $(WM_OBJ) $(ICONS_OBJ) $(GUI_OBJ) $(APP_FILES_OBJ) $(APP_LOADER_OBJ) $(APP_BUILTIN_OBJ) $(APP_PACKAGE_OBJ) $(APP_REMOTE_OBJ) $(DEVICE_MANAGER_OBJ) $(USB_MANAGER_OBJ) $(NETWORK_MANAGER_OBJ) $(POWER_OBJ) $(ETHERNET_OBJ) $(ARP_OBJ) $(IPV4_OBJ) $(ICMP_OBJ) $(UDP_OBJ) $(DHCP_OBJ) $(DNS_OBJ) $(TCP_OBJ) $(NET_SOCKET_OBJ) $(HTTP_OBJ) $(APP_CATALOG_OBJ) $(DISPLAY_OBJ) $(SHELL_INPUT_OBJ) $(SHELL_DISPATCH_OBJ) $(SHELL_COMMAND_UTILS_OBJ) $(SHELL_COMMANDS_CORE_OBJ) $(SHELL_COMMANDS_STORAGE_OBJ) $(SHELL_COMMANDS_DIAGNOSTICS_OBJ) $(SHELL_COMMANDS_NETWORK_OBJ) $(SHELL_CHECKS_OBJ) $(SHELL_COMMANDS_PACKAGES_OBJ) $(SHELL_COMMANDS_APPS_OBJ) $(SHELL_HOSTED_OBJ) $(SHELL_JOB_OBJ) $(RTC_OBJ) $(CLOCK_OBJ) $(TLS_OBJ) $(TLS_CLIENT_OBJ)

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

$(KERNEL_OBJ): $(KERNEL_C) src/include/apps/shell_job.h src/include/core/keyboard.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/clock.h src/include/core/tls.h src/include/drivers/rtc.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PANIC_OBJ): $(PANIC_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(LOG_OBJ): $(LOG_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(INPUT_OBJ): $(INPUT_C) src/include/core/input.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(IRQ_DEFERRED_OBJ): $(IRQ_DEFERRED_C) src/include/core/irq_deferred.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(WAIT_OBJ): $(WAIT_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(CLOCK_OBJ): $(CLOCK_C) src/include/core/clock.h src/include/drivers/rtc.h src/include/core/timer.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TLS_OBJ): $(TLS_C) src/include/core/tls.h src/include/core/clock.h src/include/core/tls_client.h src/include/drivers/rng.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TLS_CLIENT_OBJ): $(TLS_CLIENT_C) src/include/core/tls_client.h src/include/core/tls.h src/include/core/clock.h src/include/core/net_socket.h src/include/core/string.h src/include/drivers/rng.h vendor/bearssl/inc/bearssl.h vendor/bearssl/inc/string.h vendor/bearssl/inc/stddef.h vendor/bearssl/inc/stdint.h
	@if not exist build mkdir build
	$(GCC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BEARSSL_COMPAT_OBJ): $(BEARSSL_COMPAT_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(BEARSSL_OBJ): build/bearssl/%.o: vendor/bearssl/src/%.c vendor/bearssl/inc/string.h vendor/bearssl/inc/stddef.h vendor/bearssl/inc/stdint.h
	@if not exist build mkdir build
	@if not exist "$(@D)" mkdir "$(@D)"
	$(GCC) $(BEARSSL_CFLAGS) -c $< -o $@

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

$(UPDATE_REMOTE_OBJ): $(UPDATE_REMOTE_C) src/include/core/update_remote.h src/include/core/update_remote_config.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_RELEASE_OBJ): $(UPDATE_REMOTE_RELEASE_C) src/include/core/update_remote.h src/include/core/update_remote_config.h src/include/core/update_remote_github.h src/include/core/http.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_GITHUB_OBJ): $(UPDATE_REMOTE_GITHUB_C) src/include/core/update_remote_github.h src/include/core/update_remote.h src/include/core/update_remote_config.h src/include/core/http.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_RUNTIME_OBJ): $(UPDATE_RUNTIME_C) src/include/core/update_runtime.h src/include/core/update_remote_runtime.h src/include/core/update_trust.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_RUNTIME_OBJ): $(UPDATE_REMOTE_RUNTIME_C) src/include/core/update_remote_runtime.h src/include/core/update_runtime.h src/include/core/update_remote_github.h src/include/core/update_remote_config.h src/include/core/http.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_REMOTE_OBJ): $(APP_REMOTE_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(DEVICE_MANAGER_OBJ): $(DEVICE_MANAGER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_MANAGER_OBJ): $(USB_MANAGER_C) src/include/drivers/usb_hid.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(UHCI_OBJ): $(UHCI_C) src/include/drivers/uhci.h src/include/core/irq_deferred.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_MSC_OBJ): $(USB_MSC_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_HID_OBJ): $(USB_HID_C) src/include/drivers/usb_hid.h src/include/core/input.h
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

$(HTTP_OBJ): $(HTTP_C) src/include/core/http.h src/include/core/tls_client.h src/include/core/tls.h
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

$(APP_CATALOG_OBJ): $(APP_CATALOG_C)
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

$(FONT_OBJ): $(FONT_C) src/drivers/font_data.inc src/include/drivers/font.h
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

$(KEYBOARD_OBJ): $(KEYBOARD_C) src/include/core/keyboard.h src/include/core/input.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(MOUSE_OBJ): $(MOUSE_C) src/include/core/input.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(TIMER_OBJ): $(TIMER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(RTC_OBJ): $(RTC_C) src/include/drivers/rtc.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(RNG_OBJ): $(RNG_C) src/include/drivers/rng.h
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

$(BLOCK_OBJ): $(BLOCK_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(STORAGE_OBJ): $(STORAGE_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(FILE_INDEX_OBJ): $(FILE_INDEX_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(WAV_OBJ): $(WAV_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(BMP_OBJ): $(BMP_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(PROCESS_OBJ): $(PROCESS_C) src/include/process/process.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(IPC_OBJ): $(IPC_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(THREAD_OBJ): $(THREAD_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_OBJ): $(SHELL_C) src/include/apps/shell_input.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/core/keyboard.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_INPUT_OBJ): $(SHELL_INPUT_C) src/include/apps/shell_input.h src/include/apps/shell.h src/include/core/keyboard.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_DISPATCH_OBJ): $(SHELL_DISPATCH_C) src/include/apps/shell_dispatch.h src/include/apps/shell_job.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMAND_UTILS_OBJ): $(SHELL_COMMAND_UTILS_C) src/include/apps/shell_command_utils.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_CORE_OBJ): $(SHELL_COMMANDS_CORE_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_runtime.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_STORAGE_OBJ): $(SHELL_COMMANDS_STORAGE_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_DIAGNOSTICS_OBJ): $(SHELL_COMMANDS_DIAGNOSTICS_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_runtime.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/clock.h src/include/core/tls.h src/include/drivers/rtc.h src/include/drivers/usb_hid.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_NETWORK_OBJ): $(SHELL_COMMANDS_NETWORK_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_CHECKS_OBJ): $(SHELL_CHECKS_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/core/keyboard.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/clock.h src/include/core/tls.h src/include/drivers/rtc.h src/include/drivers/usb_hid.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_PACKAGES_OBJ): $(SHELL_COMMANDS_PACKAGES_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_APPS_OBJ): $(SHELL_COMMANDS_APPS_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_runtime.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_HOSTED_OBJ): $(SHELL_HOSTED_C) src/include/apps/shell.h src/include/apps/shell_input.h src/include/apps/shell_runtime.h
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_JOB_OBJ): $(SHELL_JOB_C) src/include/apps/shell_job.h src/include/apps/shell.h src/include/apps/shell_command_utils.h src/include/apps/shell_runtime.h
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

$(UPDATER_OBJ): $(UPDATER_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@

$(APPSTORE_OBJ): $(APPSTORE_C)
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

$(DISPLAY_OBJ): $(DISPLAY_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@


$(KERNEL_BIN): $(OBJS) src/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(OS_IMG): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) tools\packager.py \
          assets\icons\SHELL.BMP assets\icons\EXPLORER.BMP assets\icons\TASKMGR.BMP \
          $(STORE_FIXTURES) $(STORE_AS2_FIXTURES) $(STORE_AS4_UPDATE_FIXTURES) \
          docs\fixtures\updates\u2\VALID.ZUP docs\fixtures\updates\u2\TRUNC.ZUP \
          docs\fixtures\updates\u2\BADHASH.ZUP docs\fixtures\updates\u2\BADSIG.ZUP \
          docs\fixtures\updates\u2\BADVER.ZUP docs\fixtures\updates\u2\BADFMT.ZUP \
          docs\fixtures\updates\u2\UNKKEY.ZUP docs\fixtures\updates\u3\APPLY.ZUP
	cmd /c "copy /b build\boot.bin+build\stage2.bin+build\kernel.bin build\zephyros.img"
	python tools\packager.py prepare-image --image $(OS_IMG) --disk-bytes $(FAT12_DISK_BYTES)
	python tools\packager.py inject-file --file assets\icons\SHELL.BMP --image $(OS_IMG) --fat-name SHELL.BMP
	python tools\packager.py inject-file --file assets\icons\EXPLORER.BMP --image $(OS_IMG) --fat-name EXPLORER.BMP
	python tools\packager.py inject-file --file assets\icons\TASKMGR.BMP --image $(OS_IMG) --fat-name TASKMGR.BMP
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\VALID.ZPK --image $(OS_IMG) --fat-name VALID.ZPK
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\BADCRC.ZPK --image $(OS_IMG) --fat-name BADCRC.ZPK
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\BADAPI.ZPK --image $(OS_IMG) --fat-name BADAPI.ZPK
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\BADALIAS.ZPK --image $(OS_IMG) --fat-name BADALIAS.ZPK
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\NEEDSDEP.ZPK --image $(OS_IMG) --fat-name NEEDSDEP.ZPK
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\SAMEVER.ZPK --image $(OS_IMG) --fat-name SAMEVER.ZPK
	python tools\packager.py inject-file --file $(STORE_AS2_FIXTURES_DIR)\WAITAPP.ZPK --image $(OS_IMG) --fat-name WAITAPP.ZPK
	python tools\packager.py inject-file --file $(STORE_AS2_FIXTURES_DIR)\BASE.ZPK --image $(OS_IMG) --fat-name BASE.ZPK
	python tools\packager.py inject-file --file $(STORE_AS2_FIXTURES_DIR)\DEPEND.ZPK --image $(OS_IMG) --fat-name DEPEND.ZPK
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPTARGET.ZPK --image $(OS_IMG) --fat-name UPTARGET.ZPK
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPA.ZPK --image $(OS_IMG) --fat-name UPDEPA.ZPK
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPB.ZPK --image $(OS_IMG) --fat-name UPDEPB.ZPK
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\BROKEN.ZPK --image $(OS_IMG) --fat-name BROKEN.ZPK
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEA.ZPK --image $(OS_IMG) --fat-name CYCLEA.ZPK
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEB.ZPK --image $(OS_IMG) --fat-name CYCLEB.ZPK
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\VALID.ZUP --image $(OS_IMG) --fat-name VALID.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\TRUNC.ZUP --image $(OS_IMG) --fat-name TRUNC.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADHASH.ZUP --image $(OS_IMG) --fat-name BADHASH.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADSIG.ZUP --image $(OS_IMG) --fat-name BADSIG.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADVER.ZUP --image $(OS_IMG) --fat-name BADVER.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\BADFMT.ZUP --image $(OS_IMG) --fat-name BADFMT.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u2\UNKKEY.ZUP --image $(OS_IMG) --fat-name UNKKEY.ZUP
	python tools\packager.py inject-file --file docs\fixtures\updates\u3\APPLY.ZUP --image $(OS_IMG) --fat-name APPLY.ZUP

run: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS)

run-usb: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_ARGS) $(QEMU_USB_DEVICE_ARGS)

run-usb-msc: $(OS_IMG) $(STORAGE_FIXTURES_STAMP)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_ARGS) $(QEMU_USB_DEVICE_ARGS) $(QEMU_USB_MSC_ARGS)

run-usb-hid: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_ARGS) $(QEMU_USB_HID_DEVICE_ARGS)

$(STORAGE_FIXTURES_STAMP): $(STORAGE_FIXTURES_TOOL)
	@if not exist build mkdir build
	python $(STORAGE_FIXTURES_TOOL) generate --output-dir build

storage-fixtures: $(STORAGE_FIXTURES_STAMP)

storage-fixtures-test:
	python $(STORAGE_FIXTURES_TOOL) selftest

storage-fixtures-verify: $(STORAGE_FIXTURES_STAMP)
	python $(STORAGE_FIXTURES_TOOL) verify --output-dir build

run-storage: $(OS_IMG) $(STORAGE_FIXTURES_STAMP)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) -drive format=raw,file=$(STORAGE_VALID_IMG),if=ide,index=1 -drive format=raw,file=$(STORAGE_CORRUPT_IMG),if=ide,index=2 -drive format=raw,file=$(STORAGE_UNKNOWN_IMG),if=ide,index=3 $(QEMU_NET_ARGS)

debug: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) -s -S &

q3check:
	python tools\q3check.py
	python tools\vendor_terminus.py --check

q3check-test:
	python tools\q3check.py --self-test

package-test:
	python tools\packager.py selftest

update-test:
	python tools\updater.py selftest

package-demo: $(OS_IMG)
	python tools\packager.py demo --output build\DEMO.zephyrosapp --image $(OS_IMG)

store-test:
	python tools\packager.py selftest
	python tools\packager.py audit-store --fixtures-dir $(STORE_FIXTURES_DIR)

store-demo: $(OS_IMG) $(STORE_FIXTURES)
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\VALID.ZPK --image $(OS_IMG) --fat-name VALID.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\BADCRC.ZPK --image $(OS_IMG) --fat-name BADCRC.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\BADAPI.ZPK --image $(OS_IMG) --fat-name BADAPI.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\BADALIAS.ZPK --image $(OS_IMG) --fat-name BADALIAS.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\NEEDSDEP.ZPK --image $(OS_IMG) --fat-name NEEDSDEP.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_FIXTURES_DIR)\SAMEVER.ZPK --image $(OS_IMG) --fat-name SAMEVER.ZPK --replace
	python tools\packager.py audit-store --fixtures-dir $(STORE_FIXTURES_DIR) --image $(OS_IMG)

store-as2-test:
	python tools\packager.py selftest
	python tools\packager.py audit-store-as2 --fixtures-dir $(STORE_AS2_FIXTURES_DIR)

store-as2-demo: $(OS_IMG) $(STORE_AS2_FIXTURES)
	python tools\packager.py inject-file --file $(STORE_AS2_FIXTURES_DIR)\WAITAPP.ZPK --image $(OS_IMG) --fat-name WAITAPP.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS2_FIXTURES_DIR)\BASE.ZPK --image $(OS_IMG) --fat-name BASE.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS2_FIXTURES_DIR)\DEPEND.ZPK --image $(OS_IMG) --fat-name DEPEND.ZPK --replace
	python tools\packager.py audit-store-as2 --fixtures-dir $(STORE_AS2_FIXTURES_DIR) --image $(OS_IMG)

store-as4-test:
	python tools\packager.py selftest
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_SEED_FIXTURES_DIR)
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_UPDATE_FIXTURES_DIR)

store-as4-seed-demo: $(OS_IMG) $(STORE_AS4_SEED_FIXTURES)
	python tools\packager.py inject-file --file $(STORE_AS4_SEED_FIXTURES_DIR)\UPTARGET.ZPK --image $(OS_IMG) --fat-name UPTARGET.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_SEED_FIXTURES_DIR)\UPDEPA.ZPK --image $(OS_IMG) --fat-name UPDEPA.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_SEED_FIXTURES_DIR)\UPDEPB.ZPK --image $(OS_IMG) --fat-name UPDEPB.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_SEED_FIXTURES_DIR)\BROKEN.ZPK --image $(OS_IMG) --fat-name BROKEN.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_SEED_FIXTURES_DIR)\CYCLEA.ZPK --image $(OS_IMG) --fat-name CYCLEA.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_SEED_FIXTURES_DIR)\CYCLEB.ZPK --image $(OS_IMG) --fat-name CYCLEB.ZPK --replace
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_SEED_FIXTURES_DIR) --image $(OS_IMG)

store-as4-update-demo: $(OS_IMG) $(STORE_AS4_UPDATE_FIXTURES)
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPTARGET.ZPK --image $(OS_IMG) --fat-name UPTARGET.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPA.ZPK --image $(OS_IMG) --fat-name UPDEPA.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPB.ZPK --image $(OS_IMG) --fat-name UPDEPB.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\BROKEN.ZPK --image $(OS_IMG) --fat-name BROKEN.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEA.ZPK --image $(OS_IMG) --fat-name CYCLEA.ZPK --replace
	python tools\packager.py inject-file --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEB.ZPK --image $(OS_IMG) --fat-name CYCLEB.ZPK --replace
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_UPDATE_FIXTURES_DIR) --image $(OS_IMG)

store-as5-test:
	python tools\packager.py audit-store-as5 --fixtures-dir $(STORE_AS5_FIXTURES_DIR) --public $(STORE_AS5_PUBLIC) --header src\include\core\app_remote_trust.h

store-as5-seed-demo: store-as5-test
	python tools\packager.py serve-store-as5 --fixtures-dir $(STORE_AS5_FIXTURES_DIR) --profile seed --port 8000

store-as5-serve: store-as5-test
	python tools\packager.py serve-store-as5 --fixtures-dir $(STORE_AS5_FIXTURES_DIR) --profile update --port 8000

clean:
	rmdir /s /q build

.PHONY: all run run-usb run-usb-msc run-usb-hid run-storage storage-fixtures storage-fixtures-test storage-fixtures-verify debug q3check q3check-test package-test update-test package-demo store-test store-demo store-as2-test store-as2-demo store-as4-test store-as4-seed-demo store-as4-update-demo store-as5-test store-as5-seed-demo store-as5-serve clean
