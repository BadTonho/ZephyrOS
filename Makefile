# Makefile para ZephyrOS

SHELL = cmd.exe

# Ferramentas
# Os caminhos locais podem ficar em Makefile.local, que nao e versionado.
# Sem esse arquivo, as ferramentas sao procuradas no PATH do sistema.
-include Makefile.local

BUILD_DIR ?= build
MAKE_TOOL ?= make

NASM ?= nasm
GCC ?= i686-elf-gcc
LD ?= i686-elf-ld
NM ?= i686-elf-nm
HOST_CC ?= cc
HOST_SANITIZE_CC ?= clang
QEMU ?= qemu-system-i386
QEMU_CPU_ARGS ?= -cpu max
QEMU_NET_ARGS ?= -nic user,model=e1000
QEMU_TEST_CPU ?= max
QEMU_TEST_NETWORK ?= user,model=e1000
TST4_QEMU_BOOT_TIMEOUT ?= 60
TST4_QEMU_CASE_TIMEOUT ?= 90
TST4_QEMU_HEARTBEAT_TIMEOUT ?= 60
TST5_QEMU_BOOT_TIMEOUT ?= 60
TST5_QEMU_CASE_TIMEOUT ?= 120
TST5_QEMU_HEARTBEAT_TIMEOUT ?= 20
TST6_QEMU_BOOT_TIMEOUT ?= 60
TST6_QEMU_CASE_TIMEOUT ?= 120
TST6_QEMU_HEARTBEAT_TIMEOUT ?= 60
TST6_QEMU_STRESS_ITERATIONS ?= 8
TST6_QEMU_STRESS_DURATION ?= 300
TST6_QEMU_NETWORK ?= user,model=e1000,restrict=on
TST7_COMMAND_TIMEOUT ?= 300
TST7_QUICK_TIMEOUT ?= 1800
TST7_FULL_TIMEOUT ?= 7200
COVERAGE_BUILD_DIR ?= build-coverage
COVERAGE_CFLAGS ?= -g -DZEPHYROS_TEST_COVERAGE -finstrument-functions
QEMU_BOOT_DISK_ARGS ?= -drive file=$(OS_IMG),format=raw,if=none,id=bootdisk -device ide-hd,drive=bootdisk,bus=ide.0,unit=0,bootindex=1
QEMU_STAGE2_LBA_DISK_ARGS ?= -drive file=$(OS_IMG),format=raw,if=none,id=stage2lbadisk -device ide-hd,drive=stage2lbadisk,bootindex=1
QEMU_STAGE2_CHS_DISK_ARGS ?= -drive file=$(STAGE2_CHS_IMG),format=raw,if=floppy,index=0 -drive file=$(OS_IMG),format=raw,if=none,id=stage2chssystem -device ide-hd,drive=stage2chssystem,cyls=80,heads=2,secs=18 -boot order=a
QEMU_USB_ARGS ?= -device piix3-usb-uhci,id=usb
QEMU_USB_DEVICE_ARGS ?= -device usb-kbd,bus=usb.0
QEMU_USB_HID_DEVICE_ARGS ?= -device usb-kbd,bus=usb.0,port=1 -device usb-mouse,bus=usb.0,port=2
QEMU_USB_MSC_ARGS ?= -drive if=none,id=usb-stick,format=raw,file=$(STORAGE_VALID_IMG),readonly=on -device usb-storage,bus=usb.0,drive=usb-stick
QEMU_USB_WIFI_EHCI_ARGS ?= -machine q35 -device ich9-usb-ehci1,id=ehci
QEMU_USB_WIFI_ARGS ?= -device usb-host,vendorid=0x0BDA,productid=0xC811,bus=ehci.0

# Flags
CFLAGS_EXTRA ?=
CFLAGS = -m32 -O2 -fno-strict-aliasing -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -I src/include -I src/include/core -I src/include/drivers -I src/include/fs -I src/include/memory -I src/include/process -I src/include/apps -I src/include/ui $(CFLAGS_EXTRA)
LDFLAGS = -m elf_i386 -T src/linker.ld
NASMFLAGS = -f bin

# Arquivos - Boot
BOOT_SRC = src/boot/boot.asm
BOOT_BIN = $(BUILD_DIR)/boot.bin
STAGE2_SRC = src/boot/stage2.asm
STAGE2_BIN = $(BUILD_DIR)/stage2.bin
SYSTEM_BOOT_SRC = src/boot/system_boot.asm
SYSTEM_BOOT_BIN = $(BUILD_DIR)/system_boot.bin
SYSTEM_BOOT_HANDOFF_INVALID_BIN = $(BUILD_DIR)/system_boot_handoff_invalid.bin
SYSTEM_BOOT_RETURN_BIN = $(BUILD_DIR)/system_boot_return.bin
SYSTEM_STAGE2_SRC = src/boot/system_stage2.asm
SYSTEM_STAGE2_BIN = $(BUILD_DIR)/system_stage2.bin
RECOVERY_LOADER_C = src/boot/recovery_loader.c
RECOVERY_LOADER_OBJ = $(BUILD_DIR)/recovery_loader.o
RECOVERY_MENU_C = src/boot/recovery_menu.c
RECOVERY_MENU_HEADER = src/boot/recovery_menu.h
RECOVERY_CHAIN_HEADER = src/boot/recovery_chain.h
RECOVERY_MENU_OBJ = $(BUILD_DIR)/recovery_menu.o
RECOVERY_RUNTIME_C = src/boot/recovery_runtime.c
RECOVERY_RUNTIME_OBJ = $(BUILD_DIR)/recovery_runtime.o
RECOVERY_ENTRY_ASM = src/boot/recovery_entry.asm
RECOVERY_ENTRY_OBJ = $(BUILD_DIR)/recovery_entry.o
RECOVERY_LOADER_LD = src/boot/recovery_loader.ld
RECOVERY_LOADER_BIN = $(BUILD_DIR)/recovery_loader.bin
RECOVERY_LOADER_PADDED_BIN = $(BUILD_DIR)/recovery_loader_padded.bin
RECOVERY_LOADER_PAD_TOOL = tools/pad_boot_payload.py
RECOVERY_IMAGE_COMPOSE_TOOL = tools/compose_recovery_image.py
RECOVERY_LAYOUT_TOOL = tools/recovery_layout.py
RECOVERY_LAYOUT_HEADER = $(BUILD_DIR)/recovery_layout.h
RECOVERY_STAGE2_VGA_BIN = $(BUILD_DIR)/stage2-recovery-menu-vga.bin
RECOVERY_MENU_VGA_IMAGE = $(BUILD_DIR)/recovery-menu-vga.img
RECOVERY_STAGE2_PATCH_TOOL = tools/patch_stage2_image.py

# Arquivos - Kernel
ENTRY_SRC = src/kernel/entry.asm
ENTRY_OBJ = $(BUILD_DIR)/entry.o

KERNEL_C = src/kernel/kernel.c
KERNEL_OBJ = $(BUILD_DIR)/kernel.o

PANIC_C = src/kernel/panic.c
PANIC_OBJ = $(BUILD_DIR)/panic.o

LOG_C = src/core/log.c
LOG_OBJ = $(BUILD_DIR)/log.o

TEST_PROTOCOL_C = src/core/test_protocol.c
TEST_PROTOCOL_OBJ = $(BUILD_DIR)/test_protocol.o
TEST_PROTOCOL_CORE_C = src/core/test_protocol_core.c
TEST_PROTOCOL_CORE_OBJ = $(BUILD_DIR)/test_protocol_core.o

TEST_COVERAGE_C = src/core/test_coverage.c
TEST_COVERAGE_OBJ = $(BUILD_DIR)/test_coverage.o

KERNEL_TESTS_C = src/core/kernel_tests.c
KERNEL_TESTS_OBJ = $(BUILD_DIR)/kernel_tests.o

KERNEL_TESTS_PAGING_C = src/core/kernel_tests_paging.c
KERNEL_TESTS_PAGING_OBJ = $(BUILD_DIR)/kernel_tests_paging.o

KERNEL_TESTS_EXECUTION_C = src/core/kernel_tests_execution.c
KERNEL_TESTS_EXECUTION_OBJ = $(BUILD_DIR)/kernel_tests_execution.o

KERNEL_TESTS_STORAGE_C = src/core/kernel_tests_storage.c
KERNEL_TESTS_STORAGE_OBJ = $(BUILD_DIR)/kernel_tests_storage.o

KERNEL_TESTS_NETWORK_C = src/core/kernel_tests_network.c
KERNEL_TESTS_NETWORK_OBJ = $(BUILD_DIR)/kernel_tests_network.o

KERNEL_TESTS_PLATFORM_C = src/core/kernel_tests_platform.c
KERNEL_TESTS_PLATFORM_OBJ = $(BUILD_DIR)/kernel_tests_platform.o

KERNEL_TESTS_BLACKBOX_C = src/core/kernel_tests_blackbox.c
KERNEL_TESTS_BLACKBOX_OBJ = $(BUILD_DIR)/kernel_tests_blackbox.o

KERNEL_TESTS_TST6_C = src/core/kernel_tests_tst6.c
KERNEL_TESTS_TST6_OBJ = $(BUILD_DIR)/kernel_tests_tst6.o

INPUT_C = src/core/input.c
INPUT_OBJ = $(BUILD_DIR)/input.o

IRQ_DEFERRED_C = src/core/irq_deferred.c
IRQ_DEFERRED_OBJ = $(BUILD_DIR)/irq_deferred.o

WAIT_C = src/core/wait.c
WAIT_OBJ = $(BUILD_DIR)/wait.o

WORKQUEUE_C = src/core/workqueue.c
WORKQUEUE_OBJ = $(BUILD_DIR)/workqueue.o

CLOCK_C = src/core/clock.c
CLOCK_OBJ = $(BUILD_DIR)/clock.o

TLS_C = src/core/tls.c
TLS_OBJ = $(BUILD_DIR)/tls.o

TLS_CLIENT_C = src/core/tls_client.c
TLS_CLIENT_OBJ = $(BUILD_DIR)/tls_client.o

BEARSSL_COMPAT_C = src/core/bearssl_compat.c
BEARSSL_COMPAT_OBJ = $(BUILD_DIR)/bearssl_compat.o

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
BEARSSL_OBJ = $(patsubst vendor/bearssl/src/%.c,$(BUILD_DIR)/bearssl/%.o,$(BEARSSL_SRC))
BEARSSL_CFLAGS = $(CFLAGS) -I vendor/bearssl/inc -I vendor/bearssl/src -include vendor/bearssl/inc/string.h

RECOVERY_C = src/core/recovery.c
RECOVERY_OBJ = $(BUILD_DIR)/recovery.o

CRYPTO_C = src/core/crypto.c
CRYPTO_OBJ = $(BUILD_DIR)/crypto.o

CRYPTO_ED25519_C = src/core/crypto_ed25519.c
CRYPTO_ED25519_OBJ = $(BUILD_DIR)/crypto_ed25519.o

RECOVERY_CFLAGS = $(CFLAGS) -fno-instrument-functions
RECOVERY_CRYPTO_OBJ = $(BUILD_DIR)/recovery_crypto.o
RECOVERY_CRYPTO_ED25519_OBJ = $(BUILD_DIR)/recovery_crypto_ed25519.o

UPDATE_C = src/core/update.c
UPDATE_OBJ = $(BUILD_DIR)/update.o

UPDATE_SYSTEM_C = src/core/update_system.c
UPDATE_SYSTEM_OBJ = $(BUILD_DIR)/update_system.o

UPDATE_SYSTEM_SLOTS_C = src/core/update_system_slots.c
UPDATE_SYSTEM_SLOTS_OBJ = $(BUILD_DIR)/update_system_slots.o

UPDATE_REMOTE_SYSTEM_C = src/core/update_remote_system.c
UPDATE_REMOTE_SYSTEM_OBJ = $(BUILD_DIR)/update_remote_system.o

UPDATE_REMOTE_C = src/core/update_remote.c
UPDATE_REMOTE_OBJ = $(BUILD_DIR)/update_remote.o

UPDATE_REMOTE_RELEASE_C = src/core/update_remote_release.c
UPDATE_REMOTE_RELEASE_OBJ = $(BUILD_DIR)/update_remote_release.o

UPDATE_REMOTE_GITHUB_C = src/core/update_remote_github.c
UPDATE_REMOTE_GITHUB_OBJ = $(BUILD_DIR)/update_remote_github.o

UPDATE_RUNTIME_C = src/core/update_runtime.c
UPDATE_RUNTIME_OBJ = $(BUILD_DIR)/update_runtime.o

UPDATE_REMOTE_RUNTIME_C = src/core/update_remote_runtime.c
UPDATE_REMOTE_RUNTIME_OBJ = $(BUILD_DIR)/update_remote_runtime.o

DEVICE_MANAGER_C = src/core/device_manager.c
DEVICE_MANAGER_OBJ = $(BUILD_DIR)/device_manager.o

USB_MANAGER_C = src/core/usb_manager.c
USB_MANAGER_OBJ = $(BUILD_DIR)/usb_manager.o

UHCI_C = src/drivers/uhci.c
UHCI_OBJ = $(BUILD_DIR)/uhci.o

EHCI_C = src/drivers/ehci.c
EHCI_OBJ = $(BUILD_DIR)/ehci.o

USB_TRANSPORT_C = src/core/usb_transport.c
USB_TRANSPORT_OBJ = $(BUILD_DIR)/usb_transport.o

USB_MSC_C = src/drivers/usb_msc.c
USB_MSC_OBJ = $(BUILD_DIR)/usb_msc.o

USB_HID_C = src/drivers/usb_hid.c
USB_HID_OBJ = $(BUILD_DIR)/usb_hid.o

RTL8811CU_C = src/drivers/rtl8811cu.c
RTL8811CU_OBJ = $(BUILD_DIR)/rtl8811cu.o

NETWORK_MANAGER_C = src/core/network_manager.c
NETWORK_MANAGER_OBJ = $(BUILD_DIR)/network_manager.o

WIFI_MANAGER_C = src/core/wifi_manager.c
WIFI_MANAGER_OBJ = $(BUILD_DIR)/wifi_manager.o

ETHERNET_C = src/core/ethernet.c
ETHERNET_OBJ = $(BUILD_DIR)/ethernet.o

NET_BUFFER_C = src/core/net_buffer.c
NET_BUFFER_OBJ = $(BUILD_DIR)/net_buffer.o

SK_BUFF_C = src/core/sk_buff.c
SK_BUFF_OBJ = $(BUILD_DIR)/sk_buff.o

SOCKET_C = src/core/socket.c
SOCKET_OBJ = $(BUILD_DIR)/socket.o

ARP_C = src/core/arp.c
ARP_OBJ = $(BUILD_DIR)/arp.o

IPV4_C = src/core/ipv4.c
IPV4_OBJ = $(BUILD_DIR)/ipv4.o

ROUTE_C = src/core/route.c
ROUTE_OBJ = $(BUILD_DIR)/route.o

ICMP_C = src/core/icmp.c
ICMP_OBJ = $(BUILD_DIR)/icmp.o

UDP_C = src/core/udp.c
UDP_OBJ = $(BUILD_DIR)/udp.o

DHCP_C = src/core/dhcp.c
DHCP_OBJ = $(BUILD_DIR)/dhcp.o

DNS_C = src/core/dns.c
DNS_OBJ = $(BUILD_DIR)/dns.o

TCP_C = src/core/tcp.c
TCP_OBJ = $(BUILD_DIR)/tcp.o

NET_SOCKET_C = src/core/net_socket.c
NET_SOCKET_OBJ = $(BUILD_DIR)/net_socket.o

HTTP_C = src/core/http.c
HTTP_OBJ = $(BUILD_DIR)/http.o

POWER_C = src/core/power.c
POWER_OBJ = $(BUILD_DIR)/power.o

POWER_NOTIFIER_C = src/core/power_notifier.c
POWER_NOTIFIER_OBJ = $(BUILD_DIR)/power_notifier.o

STRING_C = src/core/string.c
STRING_OBJ = $(BUILD_DIR)/string.o

APP_API_C = src/core/app_api.c
APP_API_OBJ = $(BUILD_DIR)/app_api.o

APP_FILES_C = src/core/app_files.c
APP_FILES_OBJ = $(BUILD_DIR)/app_files.o

APP_LOADER_C = src/core/app_loader.c
APP_LOADER_OBJ = $(BUILD_DIR)/app_loader.o

APP_BUILTIN_C = src/core/app_builtin.c
APP_BUILTIN_OBJ = $(BUILD_DIR)/app_builtin.o

APP_PACKAGE_C = src/core/app_package.c
APP_PACKAGE_OBJ = $(BUILD_DIR)/app_package.o

APP_REMOTE_C = src/core/app_remote.c
APP_REMOTE_OBJ = $(BUILD_DIR)/app_remote.o

APP_CATALOG_C = src/core/app_catalog.c
APP_CATALOG_OBJ = $(BUILD_DIR)/app_catalog.o

SYSCALL_C = src/core/syscall.c
SYSCALL_OBJ = $(BUILD_DIR)/syscall.o

SWITCH_ASM = src/kernel/switch.asm
SWITCH_OBJ = $(BUILD_DIR)/switch.o

# Arquivos - Drivers
VIDEO_C = src/drivers/video.c
VIDEO_OBJ = $(BUILD_DIR)/video.o

VESA_C = src/drivers/vesa.c
VESA_OBJ = $(BUILD_DIR)/vesa.o

FONT_C = src/drivers/font.c
FONT_OBJ = $(BUILD_DIR)/font.o

IDT_C = src/drivers/idt.c
IDT_OBJ = $(BUILD_DIR)/idt.o

SERIAL_C = src/drivers/serial.c
SERIAL_OBJ = $(BUILD_DIR)/serial.o

ISR_ASM = src/drivers/isr.asm
ISR_OBJ = $(BUILD_DIR)/isr.o

IRQ_ASM = src/drivers/irq.asm
IRQ_OBJ = $(BUILD_DIR)/irq.o

KEYBOARD_C = src/drivers/keyboard.c
KEYBOARD_OBJ = $(BUILD_DIR)/keyboard.o

MOUSE_C = src/drivers/mouse.c
MOUSE_OBJ = $(BUILD_DIR)/mouse.o

TIMER_C = src/drivers/timer.c
TIMER_OBJ = $(BUILD_DIR)/timer.o

RTC_C = src/drivers/rtc.c
RTC_OBJ = $(BUILD_DIR)/rtc.o

RNG_C = src/drivers/rng.c
RNG_OBJ = $(BUILD_DIR)/rng.o

TSS_C = src/drivers/tss.c
TSS_OBJ = $(BUILD_DIR)/tss.o

ATA_C = src/drivers/ata.c
ATA_OBJ = $(BUILD_DIR)/ata.o

SPEAKER_C = src/drivers/speaker.c
SPEAKER_OBJ = $(BUILD_DIR)/speaker.o

PCI_C = src/drivers/pci.c
PCI_OBJ = $(BUILD_DIR)/pci.o

E1000_C = src/drivers/e1000.c
E1000_OBJ = $(BUILD_DIR)/e1000.o

RTL8139_C = src/drivers/rtl8139.c
RTL8139_OBJ = $(BUILD_DIR)/rtl8139.o

AC97_C = src/drivers/ac97.c
AC97_OBJ = $(BUILD_DIR)/ac97.o

ACPI_C = src/drivers/acpi.c
ACPI_OBJ = $(BUILD_DIR)/acpi.o

# Arquivos - Memoria
MEMORY_C = src/memory/memory.c
MEMORY_OBJ = $(BUILD_DIR)/memory.o

SLAB_C = src/memory/slab.c
SLAB_OBJ = $(BUILD_DIR)/slab.o

PAGING_C = src/memory/paging.c
PAGING_OBJ = $(BUILD_DIR)/paging.o

VMA_C = src/memory/vma.c
VMA_OBJ = $(BUILD_DIR)/vma.o

COMPRESS_C = src/memory/compress.c
COMPRESS_OBJ = $(BUILD_DIR)/compress.o

# Arquivos - Sistema de Arquivos
FAT12_C = src/fs/fat12.c
FAT12_OBJ = $(BUILD_DIR)/fat12.o

FAT32_C = src/fs/fat32.c
FAT32_OBJ = $(BUILD_DIR)/fat32.o

FS_C = src/fs/fs.c
FS_OBJ = $(BUILD_DIR)/fs.o

VFS_C = src/fs/vfs.c
VFS_OBJ = $(BUILD_DIR)/vfs.o

VFS_PATH_C = src/fs/vfs_path.c
VFS_PATH_OBJ = $(BUILD_DIR)/vfs_path.o

DEVFS_C = src/fs/devfs.c
DEVFS_OBJ = $(BUILD_DIR)/devfs.o

PROCFS_C = src/fs/procfs.c
PROCFS_OBJ = $(BUILD_DIR)/procfs.o

SYSFS_C = src/fs/sysfs.c
SYSFS_OBJ = $(BUILD_DIR)/sysfs.o

BLOCK_C = src/fs/block.c
BLOCK_OBJ = $(BUILD_DIR)/block.o

BLOCK_CACHE_C = src/fs/block_cache.c
BLOCK_CACHE_OBJ = $(BUILD_DIR)/block_cache.o

STORAGE_C = src/fs/storage.c
STORAGE_OBJ = $(BUILD_DIR)/storage.o

FILE_INDEX_C = src/fs/file_index.c
FILE_INDEX_OBJ = $(BUILD_DIR)/file_index.o

WAV_C = src/fs/wav.c
WAV_OBJ = $(BUILD_DIR)/wav.o

BMP_C = src/fs/bmp.c
BMP_OBJ = $(BUILD_DIR)/bmp.o

# Arquivos - Processos
PROCESS_C = src/process/process.c
PROCESS_OBJ = $(BUILD_DIR)/process.o
SIGNAL_C = src/process/signal.c
SIGNAL_OBJ = $(BUILD_DIR)/signal.o
IPC_C = src/process/ipc.c
IPC_OBJ = $(BUILD_DIR)/ipc.o


# Arquivos - Threads
THREAD_C = src/thread/thread.c
THREAD_OBJ = $(BUILD_DIR)/thread.o

# Arquivos - Shell
SHELL_C = src/shell/shell.c
SHELL_OBJ = $(BUILD_DIR)/shell.o

SHELL_INPUT_C = src/shell/shell_input.c
SHELL_INPUT_OBJ = $(BUILD_DIR)/shell_input.o

SHELL_DISPATCH_C = src/shell/shell_dispatch.c
SHELL_DISPATCH_OBJ = $(BUILD_DIR)/shell_dispatch.o

SHELL_COMMAND_UTILS_C = src/shell/shell_command_utils.c
SHELL_COMMAND_UTILS_OBJ = $(BUILD_DIR)/shell_command_utils.o

SHELL_PIPELINE_C = src/shell/shell_pipeline.c
SHELL_PIPELINE_OBJ = $(BUILD_DIR)/shell_pipeline.o

SHELL_COMMANDS_VFS_C = src/shell/shell_commands_vfs.c
SHELL_COMMANDS_VFS_OBJ = $(BUILD_DIR)/shell_commands_vfs.o

SHELL_COMMANDS_CORE_C = src/shell/shell_commands_core.c
SHELL_COMMANDS_CORE_OBJ = $(BUILD_DIR)/shell_commands_core.o

SHELL_COMMANDS_STORAGE_C = src/shell/shell_commands_storage.c
SHELL_COMMANDS_STORAGE_OBJ = $(BUILD_DIR)/shell_commands_storage.o

SHELL_COMMANDS_DIAGNOSTICS_C = src/shell/shell_commands_diagnostics.c
SHELL_COMMANDS_DIAGNOSTICS_OBJ = $(BUILD_DIR)/shell_commands_diagnostics.o

SHELL_COMMANDS_NETWORK_C = src/shell/shell_commands_network.c
SHELL_COMMANDS_NETWORK_OBJ = $(BUILD_DIR)/shell_commands_network.o

SHELL_COMMANDS_WIFI_C = src/shell/shell_commands_wifi.c
SHELL_COMMANDS_WIFI_OBJ = $(BUILD_DIR)/shell_commands_wifi.o

SHELL_CHECKS_C = src/shell/shell_checks.c
SHELL_CHECKS_OBJ = $(BUILD_DIR)/shell_checks.o

SHELL_COMMANDS_PACKAGES_C = src/shell/shell_commands_packages.c
SHELL_COMMANDS_PACKAGES_OBJ = $(BUILD_DIR)/shell_commands_packages.o

SHELL_COMMANDS_APPS_C = src/shell/shell_commands_apps.c
SHELL_COMMANDS_APPS_OBJ = $(BUILD_DIR)/shell_commands_apps.o

SHELL_HOSTED_C = src/shell/shell_hosted.c
SHELL_HOSTED_OBJ = $(BUILD_DIR)/shell_hosted.o

SHELL_JOB_C = src/shell/shell_job.c
SHELL_JOB_OBJ = $(BUILD_DIR)/shell_job.o

TASKMGR_C = src/shell/taskmanager.c
TASKMGR_OBJ = $(BUILD_DIR)/taskmanager.o

SHELL_INTROSPECTION_C = src/shell/shell_introspection.c
SHELL_INTROSPECTION_OBJ = $(BUILD_DIR)/shell_introspection.o

MEDIAPLAYER_C = src/shell/mediaplayer.c
MEDIAPLAYER_OBJ = $(BUILD_DIR)/mediaplayer.o

EDITOR_C = src/shell/editor.c
EDITOR_OBJ = $(BUILD_DIR)/editor.o

GUITEST_C = src/shell/guitest_app.c
GUITEST_OBJ = $(BUILD_DIR)/guitest_app.o


# Arquivos - File Manager
FILEMANAGER_C = src/filemanager/filemanager.c
FILEMANAGER_OBJ = $(BUILD_DIR)/filemanager.o

# Arquivos - Taskbar
TASKBAR_C = src/taskbar/taskbar.c
TASKBAR_OBJ = $(BUILD_DIR)/taskbar.o

# Arquivos - Desktop
DESKTOP_C = src/desktop/desktop.c
DESKTOP_OBJ = $(BUILD_DIR)/desktop.o

# Arquivos - Settings
SETTINGS_C = src/settings/settings.c
SETTINGS_OBJ = $(BUILD_DIR)/settings.o

# Arquivos - System Updater
UPDATER_C = src/updater/updater.c
UPDATER_OBJ = $(BUILD_DIR)/updater.o

# Arquivos - App Store
APPSTORE_C = src/appstore/appstore.c
APPSTORE_OBJ = $(BUILD_DIR)/appstore.o

# Arquivos - Window Manager
WM_C = src/wm/wm.c
WM_OBJ = $(BUILD_DIR)/wm.o

# Arquivos - Icons
ICONS_C = src/icons/icons.c
ICONS_OBJ = $(BUILD_DIR)/icons.o

# Arquivos - GUI Gráfica
GUI_C = src/gui/gui.c
GUI_OBJ = $(BUILD_DIR)/gui.o

# Arquivos - Display Layout
DISPLAY_C = src/gui/display.c
DISPLAY_OBJ = $(BUILD_DIR)/display.o


# Output
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
OS_IMG = $(BUILD_DIR)/zephyros.img
STAGE2_CHS_IMG = $(BUILD_DIR)/zephyros-stage2-chs.img
STORAGE_FIXTURES_TOOL = tools\storage_fixtures.py
STORAGE_FIXTURES_STAMP = $(BUILD_DIR)\storage-fixtures.stamp
STORAGE_VALID_IMG = $(BUILD_DIR)\storage-valid.img
STORAGE_CORRUPT_IMG = $(BUILD_DIR)\storage-corrupt.img
STORAGE_UNKNOWN_IMG = $(BUILD_DIR)\storage-unknown.img
STORAGE_NO_SPACE_IMG = $(BUILD_DIR)\storage-fat32-no-space.img
STORAGE_FAT_DIVERGENT_IMG = $(BUILD_DIR)\storage-fat32-fat-divergent.img
STORAGE_CHAIN_CORRUPT_IMG = $(BUILD_DIR)\storage-fat32-chain-corrupt.img
STORAGE_LFN_INVALID_IMG = $(BUILD_DIR)\storage-fat32-lfn-invalid.img
SYSTEM_FIXTURES_DIR = $(BUILD_DIR)\system-fixtures
SYSTEM_FIXTURE_IMAGES_DIR = $(BUILD_DIR)\system-fixture-images
SYSTEM_FIXTURES_MANIFEST = docs\fixtures\updates\system\system.json
SYSTEM_FIXTURES_PUBLIC = config\update-release-public.json
SYSTEM_SLOTS_FIXTURES_DIR = $(BUILD_DIR)\system-slots-fixtures
SYSTEM_SLOTS_BASELINE_DIR = $(SYSTEM_SLOTS_FIXTURES_DIR)\baseline
SYSTEM_SLOTS_BASELINE_MANIFEST = docs\fixtures\updates\system\baseline.json
SYSTEM_SLOTS_FIXTURE_IMAGE = $(SYSTEM_SLOTS_FIXTURES_DIR)\SLOTS.img
SYSTEM_SLOTS_MATRIX_DIR = $(BUILD_DIR)\system-slots-matrix
SYSTEM_SLOTS_MATRIX_IMAGE ?=
SYSTEM_UPDATE_MATRIX_IMAGE = $(SYSTEM_SLOTS_MATRIX_DIR)\SYSTEM_UPDATE_GUIDED.img
EP94B_FIXTURES_DIR = $(BUILD_DIR)\ep94b-fixtures
EP94B_ABI1_DIR = $(EP94B_FIXTURES_DIR)\abi1
EP94B_ABI2_DIR = $(EP94B_FIXTURES_DIR)\abi2
EP94B_MATRIX_DIR = $(BUILD_DIR)\ep94b-matrix
EP94B_MATRIX_IMAGE = $(EP94B_MATRIX_DIR)\EP94B_GUIDED.img
EP94C_MATRIX_DIR = $(BUILD_DIR)\ep94c-matrix
EP94C_MATRIX_IMAGE = $(EP94C_MATRIX_DIR)\EP94C_GUIDED.img
EP94C_FIXTURES_DIR = $(EP94C_MATRIX_DIR)\preflight-fixtures
# Defina somente em Makefile.local; a chave privada nunca entra no repositorio.
SYSTEM_PRIVATE_KEY ?=
SYSTEM_FIXTURE_IMAGE ?=

# A area FAT12 legada continua contendo o boot, stage2 e kernel. O restante
# da imagem abriga a particao FAT32 de sistema sem alterar o setor de boot.
HYBRID_DISK_BYTES = 268435456
LEGACY_KERNEL_LBA = 64
RECOVERY_LOADER_LBA = 6144
FAT32_START_LBA = 8192
FAT32_LABEL = ZEPHYROS
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
OBJS = $(ENTRY_OBJ) $(KERNEL_OBJ) $(PANIC_OBJ) $(LOG_OBJ) $(TEST_PROTOCOL_CORE_OBJ) $(TEST_PROTOCOL_OBJ) $(TEST_COVERAGE_OBJ) $(KERNEL_TESTS_OBJ) $(KERNEL_TESTS_PAGING_OBJ) $(KERNEL_TESTS_EXECUTION_OBJ) $(KERNEL_TESTS_STORAGE_OBJ) $(KERNEL_TESTS_NETWORK_OBJ) $(KERNEL_TESTS_PLATFORM_OBJ) $(KERNEL_TESTS_BLACKBOX_OBJ) $(KERNEL_TESTS_TST6_OBJ) $(INPUT_OBJ) $(IRQ_DEFERRED_OBJ) $(WAIT_OBJ) $(WORKQUEUE_OBJ) $(RECOVERY_OBJ) $(CRYPTO_OBJ) $(CRYPTO_ED25519_OBJ) $(BEARSSL_COMPAT_OBJ) $(BEARSSL_OBJ) $(UPDATE_OBJ) $(UPDATE_SYSTEM_OBJ) $(UPDATE_SYSTEM_SLOTS_OBJ) $(UPDATE_REMOTE_SYSTEM_OBJ) $(UPDATE_REMOTE_OBJ) $(UPDATE_REMOTE_RELEASE_OBJ) $(UPDATE_REMOTE_GITHUB_OBJ) $(UPDATE_RUNTIME_OBJ) $(UPDATE_REMOTE_RUNTIME_OBJ) $(STRING_OBJ) $(APP_API_OBJ) $(SYSCALL_OBJ) $(SWITCH_OBJ) \
       $(VIDEO_OBJ) $(VESA_OBJ) $(FONT_OBJ) $(IDT_OBJ) $(SERIAL_OBJ) $(ISR_OBJ) $(IRQ_OBJ) $(KEYBOARD_OBJ) \
       $(MOUSE_OBJ) $(TIMER_OBJ) $(TSS_OBJ) $(ATA_OBJ) $(SPEAKER_OBJ) $(PCI_OBJ) $(UHCI_OBJ) $(EHCI_OBJ) $(USB_TRANSPORT_OBJ) $(USB_MSC_OBJ) $(USB_HID_OBJ) $(RTL8811CU_OBJ) $(E1000_OBJ) $(RTL8139_OBJ) $(AC97_OBJ) $(ACPI_OBJ) $(RNG_OBJ) \
       $(MEMORY_OBJ) $(PAGING_OBJ) $(VMA_OBJ) $(COMPRESS_OBJ) \
       $(FAT12_OBJ) $(FAT32_OBJ) $(FS_OBJ) $(VFS_OBJ) $(VFS_PATH_OBJ) $(DEVFS_OBJ) $(PROCFS_OBJ) $(SYSFS_OBJ) $(BLOCK_OBJ) $(BLOCK_CACHE_OBJ) $(STORAGE_OBJ) $(FILE_INDEX_OBJ) $(WAV_OBJ) $(BMP_OBJ) $(PROCESS_OBJ) $(SIGNAL_OBJ) $(IPC_OBJ) $(THREAD_OBJ) $(SHELL_OBJ) $(TASKMGR_OBJ) $(SHELL_INTROSPECTION_OBJ) $(MEDIAPLAYER_OBJ) $(EDITOR_OBJ) $(GUITEST_OBJ) $(FILEMANAGER_OBJ) $(TASKBAR_OBJ) $(DESKTOP_OBJ) $(SETTINGS_OBJ) $(UPDATER_OBJ) $(APPSTORE_OBJ) $(WM_OBJ) $(ICONS_OBJ) $(GUI_OBJ) $(APP_FILES_OBJ) $(APP_LOADER_OBJ) $(APP_BUILTIN_OBJ) $(APP_PACKAGE_OBJ) $(APP_REMOTE_OBJ) $(DEVICE_MANAGER_OBJ) $(USB_MANAGER_OBJ) $(NETWORK_MANAGER_OBJ) $(WIFI_MANAGER_OBJ) $(POWER_OBJ) $(NET_BUFFER_OBJ) $(SK_BUFF_OBJ) $(SOCKET_OBJ) $(ETHERNET_OBJ) $(ARP_OBJ) $(IPV4_OBJ) $(ICMP_OBJ) $(UDP_OBJ) $(DHCP_OBJ) $(DNS_OBJ) $(TCP_OBJ) $(NET_SOCKET_OBJ) $(HTTP_OBJ) $(APP_CATALOG_OBJ) $(DISPLAY_OBJ) $(SHELL_INPUT_OBJ) $(SHELL_DISPATCH_OBJ) $(SHELL_COMMAND_UTILS_OBJ) $(SHELL_PIPELINE_OBJ) $(SHELL_COMMANDS_VFS_OBJ) $(SHELL_COMMANDS_CORE_OBJ) $(SHELL_COMMANDS_STORAGE_OBJ) $(SHELL_COMMANDS_DIAGNOSTICS_OBJ) $(SHELL_COMMANDS_NETWORK_OBJ) $(SHELL_COMMANDS_WIFI_OBJ) $(SHELL_CHECKS_OBJ) $(SHELL_COMMANDS_PACKAGES_OBJ) $(SHELL_COMMANDS_APPS_OBJ) $(SHELL_HOSTED_OBJ) $(SHELL_JOB_OBJ) $(RTC_OBJ) $(CLOCK_OBJ) $(TLS_OBJ) $(TLS_CLIENT_OBJ) $(SLAB_OBJ)

OBJS += $(ROUTE_OBJ)
OBJS += $(POWER_NOTIFIER_OBJ)

# Targets
all: $(OS_IMG)

coverage-image:
	python tools\build_coverage_image.py --make "$(MAKE_TOOL)" --build-dir "$(COVERAGE_BUILD_DIR)" --cflags "$(COVERAGE_CFLAGS)"

coverage-map: coverage-image tools\coverage_collector.py
	python tools\coverage_collector.py symbols --image $(COVERAGE_BUILD_DIR)\kernel.elf --output $(COVERAGE_BUILD_DIR)\coverage-symbols.json --nm "$(NM)" --addr2line "addr2line" --compiler "$(GCC)"

kernel-elf: $(KERNEL_ELF)

$(BOOT_BIN): $(BOOT_SRC) $(STAGE2_BIN)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	for /f %%S in ('powershell -NoProfile -Command "$$size = (Get-Item '$(STAGE2_BIN)').Length; [math]::Ceiling($$size / 512)"') do $(NASM) $(NASMFLAGS) -dSTAGE2_SECTORS=%%S $< -o $@

$(STAGE2_BIN): $(STAGE2_SRC) $(RECOVERY_LOADER_PADDED_BIN) $(KERNEL_BIN)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	for /f %%S in ('powershell -NoProfile -Command "$$size = (Get-Item '$(KERNEL_BIN)').Length; [math]::Ceiling($$size / 512)"') do for /f %%K in ('powershell -NoProfile -Command "(Get-Item '$(KERNEL_BIN)').Length"') do for /f %%R in ('powershell -NoProfile -Command "(Get-Item '$(RECOVERY_LOADER_PADDED_BIN)').Length / 512"') do $(NASM) $(NASMFLAGS) -dKERNEL_SECTORS=%%S -dKERNEL_BYTES=%%K -dRECOVERY_LOADER_SECTORS=%%R -dLEGACY_KERNEL_LBA=$(LEGACY_KERNEL_LBA) -dRECOVERY_LOADER_LBA=$(RECOVERY_LOADER_LBA) -dFAT32_START_LBA=$(FAT32_START_LBA) $< -o $@

$(SYSTEM_BOOT_BIN): $(SYSTEM_BOOT_SRC)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(SYSTEM_BOOT_HANDOFF_INVALID_BIN): $(SYSTEM_BOOT_SRC)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) -dSYSTEM_BOOT_CORRUPT_HANDOFF=1 $< -o $@

$(SYSTEM_BOOT_RETURN_BIN): $(SYSTEM_BOOT_SRC)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) -dSYSTEM_BOOT_FORCE_RETURN=1 $< -o $@

$(SYSTEM_STAGE2_BIN): $(SYSTEM_STAGE2_SRC)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) $(NASMFLAGS) $< -o $@

$(RECOVERY_STAGE2_VGA_BIN): $(STAGE2_SRC) $(RECOVERY_LOADER_PADDED_BIN) $(KERNEL_BIN)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	for /f %%S in ('powershell -NoProfile -Command "$$size = (Get-Item '$(KERNEL_BIN)').Length; [math]::Ceiling($$size / 512)"') do for /f %%K in ('powershell -NoProfile -Command "(Get-Item '$(KERNEL_BIN)').Length"') do for /f %%R in ('powershell -NoProfile -Command "(Get-Item '$(RECOVERY_LOADER_PADDED_BIN)').Length / 512"') do $(NASM) $(NASMFLAGS) -dKERNEL_SECTORS=%%S -dKERNEL_BYTES=%%K -dRECOVERY_LOADER_SECTORS=%%R -dLEGACY_KERNEL_LBA=$(LEGACY_KERNEL_LBA) -dRECOVERY_LOADER_LBA=$(RECOVERY_LOADER_LBA) -dFAT32_START_LBA=$(FAT32_START_LBA) -dRECOVERY_FORCE_VGA_TEXT=1 $< -o $@

$(RECOVERY_ENTRY_OBJ): $(RECOVERY_ENTRY_ASM)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(RECOVERY_LAYOUT_HEADER): $(KERNEL_BIN) $(RECOVERY_LAYOUT_TOOL)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	python $(RECOVERY_LAYOUT_TOOL) --kernel $(KERNEL_BIN) --output $@

$(RECOVERY_LOADER_OBJ): $(RECOVERY_LOADER_C) $(RECOVERY_MENU_HEADER) $(RECOVERY_CHAIN_HEADER) $(RECOVERY_LAYOUT_HEADER) src/include/core/crypto.h src/include/core/update_system.h src/include/core/update_system_slots.h src/include/core/update_trust.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(RECOVERY_CFLAGS) -I $(BUILD_DIR) -c $< -o $@

$(RECOVERY_MENU_OBJ): $(RECOVERY_MENU_C) $(RECOVERY_MENU_HEADER)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(RECOVERY_CFLAGS) -c $< -o $@

$(RECOVERY_RUNTIME_OBJ): $(RECOVERY_RUNTIME_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(RECOVERY_CFLAGS) -c $< -o $@

$(RECOVERY_LOADER_BIN): $(RECOVERY_ENTRY_OBJ) $(RECOVERY_LOADER_OBJ) $(RECOVERY_MENU_OBJ) $(RECOVERY_RUNTIME_OBJ) $(RECOVERY_CRYPTO_OBJ) $(RECOVERY_CRYPTO_ED25519_OBJ) $(RECOVERY_LOADER_LD)
	$(LD) -m elf_i386 -T $(RECOVERY_LOADER_LD) $(RECOVERY_ENTRY_OBJ) $(RECOVERY_LOADER_OBJ) $(RECOVERY_MENU_OBJ) $(RECOVERY_RUNTIME_OBJ) $(RECOVERY_CRYPTO_OBJ) $(RECOVERY_CRYPTO_ED25519_OBJ) -o $@

$(RECOVERY_LOADER_PADDED_BIN): $(RECOVERY_LOADER_BIN) $(RECOVERY_LOADER_PAD_TOOL)
	python $(RECOVERY_LOADER_PAD_TOOL) --input $(RECOVERY_LOADER_BIN) --output $@

$(ENTRY_OBJ): $(ENTRY_SRC)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(KERNEL_OBJ): $(KERNEL_C) src/include/apps/shell_job.h src/include/core/keyboard.h src/include/core/power.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/workqueue.h src/include/core/ethernet.h src/include/core/clock.h src/include/core/tls.h src/include/core/update_system.h src/include/core/update_system_slots.h src/include/core/update_remote_system.h src/include/core/test_protocol.h src/include/drivers/serial.h src/include/drivers/rtc.h src/include/process/process.h src/include/process/thread.h src/include/memory/slab.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(PANIC_OBJ): $(PANIC_C) src/include/core/test_protocol.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(LOG_OBJ): $(LOG_C) src/include/drivers/serial.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TEST_PROTOCOL_CORE_OBJ): $(TEST_PROTOCOL_CORE_C) src/core/test_protocol_core.h src/include/core/test_protocol.h src/include/core/errors.h src/include/types.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TEST_PROTOCOL_OBJ): $(TEST_PROTOCOL_C) src/core/test_protocol_core.h src/core/kernel_tests.h src/core/test_coverage.h src/include/core/test_protocol.h src/include/core/errors.h src/include/core/log.h src/include/core/timer.h src/include/drivers/serial.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TEST_COVERAGE_OBJ): $(TEST_COVERAGE_C) src/core/test_coverage.h src/include/drivers/serial.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@


$(KERNEL_TESTS_OBJ): $(KERNEL_TESTS_C) src/core/kernel_tests.h src/include/core/errors.h src/include/core/log.h src/include/core/memory.h src/include/memory/paging.h src/include/memory/slab.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_PAGING_OBJ): $(KERNEL_TESTS_PAGING_C) src/core/kernel_tests.h src/include/core/app_api.h src/include/core/log.h src/include/core/memory.h src/include/core/string.h src/include/core/syscall.h src/include/core/timer.h src/include/memory/paging.h src/include/memory/vma.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_EXECUTION_OBJ): $(KERNEL_TESTS_EXECUTION_C) src/core/kernel_tests.h src/include/core/log.h src/include/core/wait.h src/include/core/workqueue.h src/include/process/process.h src/include/process/signal.h src/include/process/thread.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_STORAGE_OBJ): $(KERNEL_TESTS_STORAGE_C) src/core/kernel_tests.h src/include/core/log.h src/include/fs/block.h src/include/fs/block_cache.h src/include/fs/devfs.h src/include/fs/file_index.h src/include/fs/procfs.h src/include/fs/storage.h src/include/fs/sysfs.h src/include/fs/vfs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_NETWORK_OBJ): $(KERNEL_TESTS_NETWORK_C) src/core/kernel_tests.h src/include/core/arp.h src/include/core/crypto.h src/include/core/dhcp.h src/include/core/dns.h src/include/core/ethernet.h src/include/core/http.h src/include/core/icmp.h src/include/core/ipv4.h src/include/core/log.h src/include/core/net_buffer.h src/include/core/net_socket.h src/include/core/route.h src/include/core/sk_buff.h src/include/core/socket.h src/include/core/tcp.h src/include/core/tls.h src/include/core/tls_client.h src/include/core/udp.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_PLATFORM_OBJ): $(KERNEL_TESTS_PLATFORM_C) src/core/kernel_tests.h src/include/core/clock.h src/include/core/device_manager.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/log.h src/include/core/power.h src/include/core/power_notifier.h src/include/core/timer.h src/include/core/usb_manager.h src/include/core/wifi_manager.h src/include/drivers/acpi.h src/include/drivers/idt.h src/include/drivers/rng.h src/include/drivers/rtc.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_BLACKBOX_OBJ): $(KERNEL_TESTS_BLACKBOX_C) src/core/kernel_tests.h src/core/video_test.h src/include/core/errors.h src/include/core/log.h src/include/core/timer.h src/include/process/process.h src/include/core/video.h src/include/ui/desktop.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(KERNEL_TESTS_TST6_OBJ): $(KERNEL_TESTS_TST6_C) src/core/kernel_tests.h src/include/core/app_package.h src/include/core/errors.h src/include/core/log.h src/include/core/update_runtime.h src/include/memory/paging.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(INPUT_OBJ): $(INPUT_C) src/include/core/input.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(IRQ_DEFERRED_OBJ): $(IRQ_DEFERRED_C) src/include/core/irq_deferred.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(WAIT_OBJ): $(WAIT_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(WORKQUEUE_OBJ): $(WORKQUEUE_C) src/include/core/workqueue.h src/include/core/wait.h src/include/core/timer.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(CLOCK_OBJ): $(CLOCK_C) src/include/core/clock.h src/include/drivers/rtc.h src/include/core/timer.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TLS_OBJ): $(TLS_C) src/include/core/tls.h src/include/core/clock.h src/include/core/tls_client.h src/include/drivers/rng.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TLS_CLIENT_OBJ): $(TLS_CLIENT_C) src/include/core/tls_client.h src/include/core/tls.h src/include/core/clock.h src/include/core/net_socket.h src/include/core/string.h src/include/drivers/rng.h vendor/bearssl/inc/bearssl.h vendor/bearssl/inc/string.h vendor/bearssl/inc/stddef.h vendor/bearssl/inc/stdint.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BEARSSL_COMPAT_OBJ): $(BEARSSL_COMPAT_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(BEARSSL_OBJ): $(BUILD_DIR)/bearssl/%.o: vendor/bearssl/src/%.c vendor/bearssl/inc/string.h vendor/bearssl/inc/stddef.h vendor/bearssl/inc/stdint.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@if not exist "$(@D)" mkdir "$(@D)"
	$(GCC) $(BEARSSL_CFLAGS) -c $< -o $@

$(RECOVERY_OBJ): $(RECOVERY_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(CRYPTO_OBJ): $(CRYPTO_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(CRYPTO_ED25519_OBJ): $(CRYPTO_ED25519_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(RECOVERY_CRYPTO_OBJ): $(CRYPTO_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(RECOVERY_CFLAGS) -c $< -o $@

$(RECOVERY_CRYPTO_ED25519_OBJ): $(CRYPTO_ED25519_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(RECOVERY_CFLAGS) -c $< -o $@

$(UPDATE_OBJ): $(UPDATE_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_SYSTEM_OBJ): $(UPDATE_SYSTEM_C) src/include/core/update_system.h src/include/core/update_trust.h src/include/core/update.h src/include/core/update_remote_github.h src/include/core/update_remote_config.h src/include/core/http.h src/include/fs/fs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_SYSTEM_SLOTS_OBJ): $(UPDATE_SYSTEM_SLOTS_C) src/include/core/update_system_slots.h src/include/core/update_system.h src/include/core/update.h src/include/fs/fs.h src/include/fs/storage.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_SYSTEM_OBJ): $(UPDATE_REMOTE_SYSTEM_C) src/include/core/update_remote_system.h src/include/core/update_system.h src/include/core/update_remote.h src/include/fs/fs.h src/include/fs/storage.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_OBJ): $(UPDATE_REMOTE_C) src/include/core/update_remote.h src/include/core/update_remote_config.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_RELEASE_OBJ): $(UPDATE_REMOTE_RELEASE_C) src/include/core/update_remote.h src/include/core/update_remote_config.h src/include/core/update_remote_github.h src/include/core/http.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_GITHUB_OBJ): $(UPDATE_REMOTE_GITHUB_C) src/include/core/update_remote_github.h src/include/core/update_remote.h src/include/core/update_system.h src/include/core/update_remote_config.h src/include/core/http.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_RUNTIME_OBJ): $(UPDATE_RUNTIME_C) src/include/core/update_runtime.h src/include/core/update_remote_runtime.h src/include/core/update_trust.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATE_REMOTE_RUNTIME_OBJ): $(UPDATE_REMOTE_RUNTIME_C) src/include/core/update_remote_runtime.h src/include/core/update_runtime.h src/include/core/update_remote_github.h src/include/core/update_remote_config.h src/include/core/http.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_REMOTE_OBJ): $(APP_REMOTE_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(DEVICE_MANAGER_OBJ): $(DEVICE_MANAGER_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_MANAGER_OBJ): $(USB_MANAGER_C) src/include/drivers/usb_hid.h src/include/drivers/ehci.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UHCI_OBJ): $(UHCI_C) src/include/drivers/uhci.h src/include/core/irq_deferred.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(EHCI_OBJ): $(EHCI_C) src/include/drivers/ehci.h src/include/core/usb_manager.h src/include/core/irq_deferred.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_TRANSPORT_OBJ): $(USB_TRANSPORT_C) src/include/core/usb_transport.h src/include/drivers/uhci.h src/include/drivers/ehci.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_MSC_OBJ): $(USB_MSC_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(USB_HID_OBJ): $(USB_HID_C) src/include/drivers/usb_hid.h src/include/core/input.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(RTL8811CU_OBJ): $(RTL8811CU_C) src/include/drivers/rtl8811cu.h src/include/core/usb_manager.h src/include/core/usb_transport.h src/include/core/ethernet.h src/include/core/errors.h src/include/core/log.h src/include/core/string.h src/include/fs/fs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(NETWORK_MANAGER_OBJ): $(NETWORK_MANAGER_C) src/include/core/network_manager.h src/include/core/ethernet.h src/include/core/usb_manager.h src/include/drivers/rtl8811cu.h src/include/core/socket.h src/include/core/route.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(WIFI_MANAGER_OBJ): $(WIFI_MANAGER_C) src/include/core/wifi_manager.h src/include/core/usb_manager.h src/include/drivers/pci.h src/include/drivers/rtl8811cu.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ETHERNET_OBJ): $(ETHERNET_C) src/include/core/ethernet.h src/include/core/net_buffer.h src/include/core/sk_buff.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(NET_BUFFER_OBJ): $(NET_BUFFER_C) src/include/core/net_buffer.h src/include/core/errors.h src/include/core/log.h src/include/core/string.h src/include/core/spinlock.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SK_BUFF_OBJ): $(SK_BUFF_C) src/include/core/sk_buff.h src/include/core/net_buffer.h src/include/core/errors.h src/include/core/log.h src/include/core/string.h src/include/core/spinlock.h src/include/memory/slab.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SOCKET_OBJ): $(SOCKET_C) src/include/core/socket.h src/include/core/poll.h src/include/core/net_socket.h src/include/core/net_buffer.h src/include/core/sk_buff.h src/include/core/errors.h src/include/core/ipv4.h src/include/core/log.h src/include/core/spinlock.h src/include/core/string.h src/include/core/wait.h src/include/fs/vfs.h src/include/fs/vfs_internal.h src/include/process/process.h src/include/memory/slab.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ARP_OBJ): $(ARP_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(IPV4_OBJ): $(IPV4_C) src/include/core/route.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ROUTE_OBJ): $(ROUTE_C) src/include/core/route.h src/include/core/ipv4.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ICMP_OBJ): $(ICMP_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UDP_OBJ): $(UDP_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(DHCP_OBJ): $(DHCP_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(DNS_OBJ): $(DNS_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TCP_OBJ): $(TCP_C) src/include/core/tcp.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(NET_SOCKET_OBJ): $(NET_SOCKET_C) src/include/core/net_buffer.h src/include/fs/vfs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(HTTP_OBJ): $(HTTP_C) src/include/core/http.h src/include/core/tls_client.h src/include/core/tls.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(POWER_OBJ): $(POWER_C) src/include/core/power.h src/include/core/power_notifier.h src/include/core/errors.h src/include/core/keyboard.h src/include/core/log.h src/include/core/network_manager.h src/include/core/string.h src/include/core/timer.h src/include/core/video.h src/include/core/workqueue.h src/include/drivers/ac97.h src/include/drivers/acpi.h src/include/drivers/idt.h src/include/drivers/speaker.h src/include/fs/storage.h src/include/fs/vfs.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(POWER_NOTIFIER_OBJ): $(POWER_NOTIFIER_C) src/include/core/power_notifier.h src/include/core/errors.h src/include/core/log.h src/include/core/string.h src/include/core/timer.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(STRING_OBJ): $(STRING_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_API_OBJ): $(APP_API_C) src/include/core/app_api.h src/include/core/app_files.h src/include/core/poll.h src/include/memory/vma.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_FILES_OBJ): $(APP_FILES_C) src/include/core/app_files.h src/include/core/app_api.h src/include/core/poll.h src/include/fs/vfs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_LOADER_OBJ): $(APP_LOADER_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_BUILTIN_OBJ): $(APP_BUILTIN_C) src/include/core/syscall.h src/include/core/app_api.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_PACKAGE_OBJ): $(APP_PACKAGE_C) src/include/core/app_package.h src/include/core/app_loader.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APP_CATALOG_OBJ): $(APP_CATALOG_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SYSCALL_OBJ): $(SYSCALL_C) src/include/core/syscall.h src/include/core/app_api.h src/include/core/app_files.h src/include/core/poll.h src/include/memory/paging.h src/include/memory/vma.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SWITCH_OBJ): $(SWITCH_ASM)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(VIDEO_OBJ): $(VIDEO_C) src/include/core/video.h src/include/drivers/vesa.h src/include/core/errors.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(VESA_OBJ): $(VESA_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(FONT_OBJ): $(FONT_C) src/drivers/font_data.inc src/include/drivers/font.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(IDT_OBJ): $(IDT_C) src/include/drivers/idt.h src/include/core/test_protocol.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SERIAL_OBJ): $(SERIAL_C) src/include/drivers/serial.h src/include/core/errors.h src/include/core/log.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ISR_OBJ): $(ISR_ASM)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(IRQ_OBJ): $(IRQ_ASM)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(KEYBOARD_OBJ): $(KEYBOARD_C) src/include/core/keyboard.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/errors.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(MOUSE_OBJ): $(MOUSE_C) src/include/core/input.h src/include/core/irq_deferred.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TIMER_OBJ): $(TIMER_C) src/include/core/timer.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(RTC_OBJ): $(RTC_C) src/include/drivers/rtc.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(RNG_OBJ): $(RNG_C) src/include/drivers/rng.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TSS_OBJ): $(TSS_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ATA_OBJ): $(ATA_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SPEAKER_OBJ): $(SPEAKER_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(PCI_OBJ): $(PCI_C) src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(E1000_OBJ): $(E1000_C) src/include/core/ethernet.h src/include/core/irq_deferred.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(RTL8139_OBJ): $(RTL8139_C) src/include/core/ethernet.h src/include/core/irq_deferred.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(AC97_OBJ): $(AC97_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ACPI_OBJ): $(ACPI_C) src/include/drivers/acpi.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(MEMORY_OBJ): $(MEMORY_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SLAB_OBJ): $(SLAB_C) src/include/memory/slab.h src/include/core/errors.h src/include/core/log.h src/include/core/memory.h src/include/core/spinlock.h src/include/core/string.h src/include/memory/paging.h src/core/kernel_tests.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(PAGING_OBJ): $(PAGING_C) src/include/memory/paging.h src/include/core/errors.h src/include/core/log.h src/include/core/memory.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(VMA_OBJ): $(VMA_C) src/include/memory/vma.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(COMPRESS_OBJ): $(COMPRESS_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT12_OBJ): $(FAT12_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(FAT32_OBJ): $(FAT32_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(FS_OBJ): $(FS_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(VFS_OBJ): $(VFS_C) src/include/fs/vfs.h src/include/core/poll.h src/include/fs/vfs_internal.h src/include/fs/devfs.h src/include/fs/procfs.h src/include/fs/sysfs.h src/include/fs/storage.h src/include/process/process.h src/include/process/thread.h src/include/core/app_api.h src/include/core/wait.h src/include/memory/slab.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(VFS_PATH_OBJ): $(VFS_PATH_C) src/include/fs/vfs.h src/include/fs/vfs_internal.h src/include/fs/devfs.h src/include/fs/procfs.h src/include/fs/sysfs.h src/include/fs/storage.h src/include/process/process.h src/include/core/timer.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(DEVFS_OBJ): $(DEVFS_C) src/include/fs/devfs.h src/include/fs/vfs.h src/include/fs/block.h src/include/core/app_api.h src/include/drivers/speaker.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(PROCFS_OBJ): $(PROCFS_C) src/include/fs/procfs.h src/include/fs/vfs.h src/include/fs/vfs_internal.h src/include/core/errors.h src/include/core/log.h src/include/core/memory.h src/include/core/spinlock.h src/include/core/string.h src/include/core/timer.h src/include/core/version.h src/include/fs/block_cache.h src/include/memory/slab.h src/include/memory/paging.h src/include/memory/vma.h src/include/process/process.h src/include/process/thread.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SYSFS_OBJ): $(SYSFS_C) src/include/fs/sysfs.h src/include/fs/procfs.h src/include/fs/vfs.h src/include/fs/vfs_internal.h src/include/core/errors.h src/include/core/log.h src/include/core/memory.h src/include/core/poll.h src/include/core/power.h src/include/core/spinlock.h src/include/core/string.h src/include/drivers/pci.h src/include/core/network_manager.h src/include/core/ethernet.h src/include/fs/block.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(BLOCK_OBJ): $(BLOCK_C) src/include/fs/block.h src/include/fs/block_cache.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(BLOCK_CACHE_OBJ): $(BLOCK_CACHE_C) src/include/fs/block_cache.h src/include/fs/block.h src/include/core/errors.h src/include/core/log.h src/include/core/spinlock.h src/include/core/string.h src/include/core/timer.h src/include/core/wait.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(STORAGE_OBJ): $(STORAGE_C) src/include/fs/storage.h src/include/fs/block_cache.h src/include/core/wait.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(FILE_INDEX_OBJ): $(FILE_INDEX_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(WAV_OBJ): $(WAV_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(BMP_OBJ): $(BMP_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(PROCESS_OBJ): $(PROCESS_C) src/include/process/process.h src/include/process/thread.h src/include/memory/slab.h src/include/memory/vma.h src/include/memory/paging.h src/include/core/app_api.h src/include/core/timer.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SIGNAL_OBJ): $(SIGNAL_C) src/include/process/signal.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(IPC_OBJ): $(IPC_C) src/include/process/process.h src/include/core/poll.h src/include/fs/vfs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(THREAD_OBJ): $(THREAD_C) src/include/process/thread.h src/include/process/process.h src/include/memory/slab.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_OBJ): $(SHELL_C) src/include/apps/shell_input.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/core/keyboard.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_INPUT_OBJ): $(SHELL_INPUT_C) src/include/apps/shell_input.h src/include/apps/shell.h src/include/core/keyboard.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_DISPATCH_OBJ): $(SHELL_DISPATCH_C) src/include/apps/shell_dispatch.h src/include/apps/shell_job.h src/include/apps/shell_pipeline.h src/include/core/log.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMAND_UTILS_OBJ): $(SHELL_COMMAND_UTILS_C) src/include/apps/shell_command_utils.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_PIPELINE_OBJ): $(SHELL_PIPELINE_C) src/include/apps/shell_pipeline.h src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/core/app_api.h src/include/core/errors.h src/include/core/log.h src/include/core/memory.h src/include/core/string.h src/include/core/video.h src/include/core/wait.h src/include/fs/vfs.h src/include/process/thread.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_VFS_OBJ): $(SHELL_COMMANDS_VFS_C) src/include/apps/shell_command_utils.h src/include/apps/shell_pipeline.h src/include/core/errors.h src/include/core/log.h src/include/core/string.h src/include/core/video.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_CORE_OBJ): $(SHELL_COMMANDS_CORE_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_pipeline.h src/include/apps/shell_runtime.h src/include/core/power.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_STORAGE_OBJ): $(SHELL_COMMANDS_STORAGE_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/fs/block_cache.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_DIAGNOSTICS_OBJ): $(SHELL_COMMANDS_DIAGNOSTICS_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_introspection.h src/include/apps/shell_runtime.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/workqueue.h src/include/core/clock.h src/include/core/tls.h src/include/core/wifi_manager.h src/include/core/log.h src/include/core/power.h src/include/fs/vfs.h src/include/fs/procfs.h src/include/drivers/idt.h src/include/drivers/acpi.h src/include/drivers/rtc.h src/include/drivers/usb_hid.h src/include/process/process.h src/include/memory/slab.h src/include/memory/vma.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_NETWORK_OBJ): $(SHELL_COMMANDS_NETWORK_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/core/poll.h src/include/core/route.h src/include/core/wait.h src/include/fs/vfs.h src/include/process/process.h src/include/process/thread.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_WIFI_OBJ): $(SHELL_COMMANDS_WIFI_C) src/include/apps/shell_command_utils.h src/include/core/wifi_manager.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_CHECKS_OBJ): $(SHELL_CHECKS_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/core/keyboard.h src/include/core/power.h src/include/core/power_notifier.h src/include/core/input.h src/include/core/irq_deferred.h src/include/core/workqueue.h src/include/core/clock.h src/include/core/tls.h src/include/core/wifi_manager.h src/include/drivers/idt.h src/include/drivers/acpi.h src/include/drivers/rtc.h src/include/drivers/usb_hid.h src/include/process/process.h src/include/memory/vma.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_PACKAGES_OBJ): $(SHELL_COMMANDS_PACKAGES_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_job.h src/include/apps/shell_runtime.h src/include/core/update_system.h src/include/core/update_system_slots.h src/include/core/update_remote_system.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_COMMANDS_APPS_OBJ): $(SHELL_COMMANDS_APPS_C) src/include/apps/shell.h src/include/apps/shell_dispatch.h src/include/apps/shell_command_utils.h src/include/apps/shell_runtime.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_HOSTED_OBJ): $(SHELL_HOSTED_C) src/include/apps/shell.h src/include/apps/shell_input.h src/include/apps/shell_runtime.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_JOB_OBJ): $(SHELL_JOB_C) src/include/apps/shell_job.h src/include/apps/shell.h src/include/apps/shell_command_utils.h src/include/apps/shell_runtime.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TASKMGR_OBJ): $(TASKMGR_C) src/include/apps/shell_introspection.h src/include/core/power.h src/include/fs/vfs.h src/include/process/process.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SHELL_INTROSPECTION_OBJ): $(SHELL_INTROSPECTION_C) src/include/apps/shell_introspection.h src/include/core/errors.h src/include/core/log.h src/include/core/string.h src/include/fs/vfs.h
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(MEDIAPLAYER_OBJ): $(MEDIAPLAYER_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(EDITOR_OBJ): $(EDITOR_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(GUITEST_OBJ): $(GUITEST_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@


$(FILEMANAGER_OBJ): $(FILEMANAGER_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(TASKBAR_OBJ): $(TASKBAR_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(DESKTOP_OBJ): $(DESKTOP_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(SETTINGS_OBJ): $(SETTINGS_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(UPDATER_OBJ): $(UPDATER_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(APPSTORE_OBJ): $(APPSTORE_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(WM_OBJ): $(WM_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(ICONS_OBJ): $(ICONS_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(GUI_OBJ): $(GUI_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@

$(DISPLAY_OBJ): $(DISPLAY_C)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c $< -o $@


$(KERNEL_BIN): $(OBJS) src/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(KERNEL_ELF): $(OBJS) src/linker.ld
	$(LD) -m elf_i386 --oformat elf32-i386 -T src/linker.ld $(OBJS) -o $@

$(OS_IMG): $(BOOT_BIN) $(STAGE2_BIN) $(RECOVERY_LOADER_PADDED_BIN) $(KERNEL_BIN) tools\packager.py $(RECOVERY_IMAGE_COMPOSE_TOOL) \
          assets\icons\SHELL.BMP assets\icons\EXPLORER.BMP assets\icons\TASKMGR.BMP \
          $(STORE_FIXTURES) $(STORE_AS2_FIXTURES) $(STORE_AS4_UPDATE_FIXTURES) \
          docs\fixtures\updates\u2\VALID.ZUP docs\fixtures\updates\u2\TRUNC.ZUP \
          docs\fixtures\updates\u2\BADHASH.ZUP docs\fixtures\updates\u2\BADSIG.ZUP \
          docs\fixtures\updates\u2\BADVER.ZUP docs\fixtures\updates\u2\BADFMT.ZUP \
          docs\fixtures\updates\u2\UNKKEY.ZUP docs\fixtures\updates\u3\APPLY.ZUP
	python $(RECOVERY_IMAGE_COMPOSE_TOOL) --boot $(BOOT_BIN) --stage2 $(STAGE2_BIN) --kernel $(KERNEL_BIN) --loader $(RECOVERY_LOADER_PADDED_BIN) --kernel-lba $(LEGACY_KERNEL_LBA) --loader-lba $(RECOVERY_LOADER_LBA) --fat32-start-lba $(FAT32_START_LBA) --output $(OS_IMG)
	python tools\packager.py prepare-hybrid-image --image $(OS_IMG) --disk-bytes $(HYBRID_DISK_BYTES) --fat32-start-lba $(FAT32_START_LBA) --label $(FAT32_LABEL)
	python tools\packager.py inject-files-fat32 --image $(OS_IMG) --fat32-start-lba $(FAT32_START_LBA) --entry assets\icons\SHELL.BMP=SHELL.BMP --entry assets\icons\EXPLORER.BMP=EXPLORER.BMP --entry assets\icons\TASKMGR.BMP=TASKMGR.BMP --entry $(STORE_FIXTURES_DIR)\VALID.ZPK=VALID.ZPK --entry $(STORE_FIXTURES_DIR)\BADCRC.ZPK=BADCRC.ZPK --entry $(STORE_FIXTURES_DIR)\BADAPI.ZPK=BADAPI.ZPK --entry $(STORE_FIXTURES_DIR)\BADALIAS.ZPK=BADALIAS.ZPK --entry $(STORE_FIXTURES_DIR)\NEEDSDEP.ZPK=NEEDSDEP.ZPK --entry $(STORE_FIXTURES_DIR)\SAMEVER.ZPK=SAMEVER.ZPK --entry $(STORE_AS2_FIXTURES_DIR)\WAITAPP.ZPK=WAITAPP.ZPK --entry $(STORE_AS2_FIXTURES_DIR)\BASE.ZPK=BASE.ZPK --entry $(STORE_AS2_FIXTURES_DIR)\DEPEND.ZPK=DEPEND.ZPK --entry $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPTARGET.ZPK=UPTARGET.ZPK --entry $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPA.ZPK=UPDEPA.ZPK --entry $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPB.ZPK=UPDEPB.ZPK --entry $(STORE_AS4_UPDATE_FIXTURES_DIR)\BROKEN.ZPK=BROKEN.ZPK --entry $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEA.ZPK=CYCLEA.ZPK --entry $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEB.ZPK=CYCLEB.ZPK --entry docs\fixtures\updates\u2\VALID.ZUP=VALID.ZUP --entry docs\fixtures\updates\u2\TRUNC.ZUP=TRUNC.ZUP --entry docs\fixtures\updates\u2\BADHASH.ZUP=BADHASH.ZUP --entry docs\fixtures\updates\u2\BADSIG.ZUP=BADSIG.ZUP --entry docs\fixtures\updates\u2\BADVER.ZUP=BADVER.ZUP --entry docs\fixtures\updates\u2\BADFMT.ZUP=BADFMT.ZUP --entry docs\fixtures\updates\u2\UNKKEY.ZUP=UNKKEY.ZUP --entry docs\fixtures\updates\u3\APPLY.ZUP=APPLY.ZUP

system-fixtures: $(OS_IMG) $(SYSTEM_BOOT_BIN) $(SYSTEM_STAGE2_BIN) $(SYSTEM_FIXTURES_MANIFEST) tools\updater.py tools\packager.py
	@if "$(SYSTEM_PRIVATE_KEY)"=="" (echo SYSTEM_PRIVATE_KEY nao configurada em Makefile.local & exit /b 2)
	@if exist "$(SYSTEM_FIXTURES_DIR)" rmdir /s /q "$(SYSTEM_FIXTURES_DIR)"
	@if exist "$(SYSTEM_FIXTURE_IMAGES_DIR)" rmdir /s /q "$(SYSTEM_FIXTURE_IMAGES_DIR)"
	@if not exist "$(SYSTEM_FIXTURE_IMAGES_DIR)" mkdir "$(SYSTEM_FIXTURE_IMAGES_DIR)"
	python tools\updater.py fixtures-system-qemu --full-kernel --manifest $(SYSTEM_FIXTURES_MANIFEST) --boot $(SYSTEM_BOOT_BIN) --stage2 $(SYSTEM_STAGE2_BIN) --kernel $(KERNEL_BIN) --private "$(SYSTEM_PRIVATE_KEY)" --public $(SYSTEM_FIXTURES_PUBLIC) --output-dir $(SYSTEM_FIXTURES_DIR)
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\VALID.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\valid.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\VALID.img --path VALID.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\TRUNC.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\truncated.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\TRUNC.img --path TRUNC.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\HDRBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\tampered-header.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\HDRBAD.img --path HDRBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\PAYBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\tampered-payload.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\PAYBAD.img --path PAYBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\SIGBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\tampered-signature.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\SIGBAD.img --path SIGBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\OVERSIZ.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\oversized.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\OVERSIZ.img --path OVERSIZ.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\MISALGN.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\misaligned.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\MISALGN.img --path MISALGN.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\VERBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\incompatible-version.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\VERBAD.img --path VERBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\EPCHBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\incompatible-epoch.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\EPCHBAD.img --path EPCHBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\ABIBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\incompatible-abi.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\ABIBAD.img --path ABIBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\SCHBAD.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\incompatible-schema.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\SCHBAD.img --path SCHBAD.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\IMGHASH.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\hash-divergent-image.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\IMGHASH.img --path IMGHASH.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_FIXTURE_IMAGES_DIR)\CMPHASH.img' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\hash-divergent-component.zsys --image $(SYSTEM_FIXTURE_IMAGES_DIR)\CMPHASH.img --path CMPHASH.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace

run-system-fixture:
	@if "$(SYSTEM_FIXTURE_IMAGE)"=="" (echo SYSTEM_FIXTURE_IMAGE nao configurada & exit /b 2)
	@if not exist "$(SYSTEM_FIXTURE_IMAGE)" (echo Imagem de fixture nao encontrada: $(SYSTEM_FIXTURE_IMAGE) & exit /b 2)
	$(QEMU) $(QEMU_CPU_ARGS) -drive file=$(SYSTEM_FIXTURE_IMAGE),format=raw,if=none,id=systemfixture -device ide-hd,drive=systemfixture,bootindex=1 $(QEMU_NET_ARGS)

system-slots-fixtures: system-fixtures $(SYSTEM_SLOTS_BASELINE_MANIFEST) tools\updater.py tools\system_slots_fixtures.py tools\packager.py
	@if "$(SYSTEM_PRIVATE_KEY)"=="" (echo SYSTEM_PRIVATE_KEY nao configurada em Makefile.local & exit /b 2)
	@if exist "$(SYSTEM_SLOTS_FIXTURES_DIR)" rmdir /s /q "$(SYSTEM_SLOTS_FIXTURES_DIR)"
	@if not exist "$(SYSTEM_SLOTS_BASELINE_DIR)" mkdir "$(SYSTEM_SLOTS_BASELINE_DIR)"
	python tools\updater.py fixtures-system-qemu --full-kernel --manifest $(SYSTEM_SLOTS_BASELINE_MANIFEST) --boot $(BOOT_BIN) --stage2 $(STAGE2_BIN) --kernel $(KERNEL_BIN) --private "$(SYSTEM_PRIVATE_KEY)" --public $(SYSTEM_FIXTURES_PUBLIC) --output-dir $(SYSTEM_SLOTS_BASELINE_DIR)
	python tools\system_slots_fixtures.py --package $(SYSTEM_SLOTS_BASELINE_DIR)\valid.zsys --output-dir $(SYSTEM_SLOTS_FIXTURES_DIR)
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(SYSTEM_SLOTS_FIXTURE_IMAGE)' -Force"
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_SLOTS_BASELINE_DIR)\valid.zsys --image $(SYSTEM_SLOTS_FIXTURE_IMAGE) --path ZSA0.ZSY --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_SLOTS_FIXTURES_DIR)\ZSI0.STA --image $(SYSTEM_SLOTS_FIXTURE_IMAGE) --path ZSI0.STA --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_SLOTS_FIXTURES_DIR)\ZSI1.STA --image $(SYSTEM_SLOTS_FIXTURE_IMAGE) --path ZSI1.STA --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(SYSTEM_FIXTURES_DIR)\valid.zsys --image $(SYSTEM_SLOTS_FIXTURE_IMAGE) --path VALID.ZSYS --fat32-start-lba $(FAT32_START_LBA) --replace

run-system-slots-fixture: system-slots-fixtures
	$(QEMU) $(QEMU_CPU_ARGS) -drive file=$(SYSTEM_SLOTS_FIXTURE_IMAGE),format=raw,if=none,id=systemslots -device ide-hd,drive=systemslots,bootindex=1 $(QEMU_NET_ARGS)

system-slots-matrix: system-slots-fixtures tools\system_slots_matrix.py tools\packager.py
	@if exist "$(SYSTEM_SLOTS_MATRIX_DIR)" rmdir /s /q "$(SYSTEM_SLOTS_MATRIX_DIR)"
	@if not exist "$(SYSTEM_SLOTS_MATRIX_DIR)" mkdir "$(SYSTEM_SLOTS_MATRIX_DIR)"
	python tools\system_slots_matrix.py --base-image $(SYSTEM_SLOTS_FIXTURE_IMAGE) --baseline $(SYSTEM_SLOTS_BASELINE_DIR)\valid.zsys --candidate $(SYSTEM_FIXTURES_DIR)\valid.zsys --bad-signature $(SYSTEM_FIXTURES_DIR)\tampered-signature.zsys --bad-image-hash $(SYSTEM_FIXTURES_DIR)\hash-divergent-image.zsys --bad-component-hash $(SYSTEM_FIXTURES_DIR)\hash-divergent-component.zsys --output-dir $(SYSTEM_SLOTS_MATRIX_DIR) --fat32-start-lba $(FAT32_START_LBA)

run-system-slots-matrix: system-slots-matrix
	@if "$(SYSTEM_SLOTS_MATRIX_IMAGE)"=="" (echo SYSTEM_SLOTS_MATRIX_IMAGE nao configurada & exit /b 2)
	@if not exist "$(SYSTEM_SLOTS_MATRIX_IMAGE)" (echo Imagem de matriz nao encontrada: $(SYSTEM_SLOTS_MATRIX_IMAGE) & exit /b 2)
	$(QEMU) $(QEMU_CPU_ARGS) -drive file=$(SYSTEM_SLOTS_MATRIX_IMAGE),format=raw,if=none,id=systemslotsmatrix -device ide-hd,drive=systemslotsmatrix,bootindex=1 $(QEMU_NET_ARGS)

run-system-update-matrix: system-slots-matrix
	@if not exist "$(SYSTEM_UPDATE_MATRIX_IMAGE)" (echo Imagem guiada EP9.3 nao encontrada: $(SYSTEM_UPDATE_MATRIX_IMAGE) & exit /b 2)
	$(QEMU) $(QEMU_CPU_ARGS) -snapshot -monitor stdio -drive file=$(SYSTEM_UPDATE_MATRIX_IMAGE),format=raw,if=none,id=systemupdatematrix -device ide-hd,drive=systemupdatematrix,bootindex=1 $(QEMU_NET_ARGS)

ep94b-fixtures: $(OS_IMG) $(SYSTEM_BOOT_BIN) $(SYSTEM_BOOT_HANDOFF_INVALID_BIN) $(SYSTEM_BOOT_RETURN_BIN) $(SYSTEM_STAGE2_BIN) $(SYSTEM_FIXTURES_MANIFEST) $(SYSTEM_SLOTS_BASELINE_MANIFEST) tools\updater.py
	@if "$(SYSTEM_PRIVATE_KEY)"=="" (echo SYSTEM_PRIVATE_KEY nao configurada em Makefile.local & exit /b 2)
	@if exist "$(EP94B_FIXTURES_DIR)" rmdir /s /q "$(EP94B_FIXTURES_DIR)"
	@if not exist "$(EP94B_ABI1_DIR)" mkdir "$(EP94B_ABI1_DIR)"
	@if not exist "$(EP94B_ABI2_DIR)" mkdir "$(EP94B_ABI2_DIR)"
	python tools\updater.py fixtures-system-qemu --full-kernel --manifest $(SYSTEM_FIXTURES_MANIFEST) --boot $(SYSTEM_BOOT_BIN) --stage2 $(SYSTEM_STAGE2_BIN) --kernel $(KERNEL_BIN) --private "$(SYSTEM_PRIVATE_KEY)" --public $(SYSTEM_FIXTURES_PUBLIC) --output-dir $(EP94B_ABI2_DIR) --handoff-invalid-boot $(SYSTEM_BOOT_HANDOFF_INVALID_BIN) --returning-boot $(SYSTEM_BOOT_RETURN_BIN) --legacy-manifest $(SYSTEM_SLOTS_BASELINE_MANIFEST) --legacy-boot $(BOOT_BIN) --legacy-stage2 $(STAGE2_BIN) --legacy-output-dir $(EP94B_ABI1_DIR)

ep94b-matrix: ep94b-fixtures tools\ep94b_matrix.py tools\system_slots_matrix.py tools\packager.py
	@if exist "$(EP94B_MATRIX_DIR)" rmdir /s /q "$(EP94B_MATRIX_DIR)"
	@if not exist "$(EP94B_MATRIX_DIR)" mkdir "$(EP94B_MATRIX_DIR)"
	python tools\ep94b_matrix.py --base-image $(OS_IMG) --abi1 $(EP94B_ABI1_DIR)\valid.zsys --abi2 $(EP94B_ABI2_DIR)\valid.zsys --bad-boot $(EP94B_ABI2_DIR)\hash-divergent-boot.zsys --bad-stage2 $(EP94B_ABI2_DIR)\hash-divergent-stage2.zsys --bad-kernel $(EP94B_ABI2_DIR)\hash-divergent-kernel.zsys --handoff-invalid $(EP94B_ABI2_DIR)\handoff-invalid.zsys --returning-boot $(EP94B_ABI2_DIR)\returning-boot.zsys --output $(EP94B_MATRIX_IMAGE) --fat32-start-lba $(FAT32_START_LBA)

run-ep94b-matrix: ep94b-matrix
	@if not exist "$(EP94B_MATRIX_IMAGE)" (echo Imagem guiada EP9.4B nao encontrada: $(EP94B_MATRIX_IMAGE) & exit /b 2)
	$(QEMU) $(QEMU_CPU_ARGS) -snapshot -monitor stdio -drive file=$(EP94B_MATRIX_IMAGE),format=raw,if=none,id=ep94bmatrix -device ide-hd,drive=ep94bmatrix,bootindex=1 $(QEMU_NET_ARGS)

ep94c-matrix: ep94b-fixtures tools\ep94c_matrix.py tools\ep94b_matrix.py tools\system_slots_matrix.py tools\packager.py
	@if exist "$(EP94C_MATRIX_DIR)" rmdir /s /q "$(EP94C_MATRIX_DIR)"
	@if not exist "$(EP94C_MATRIX_DIR)" mkdir "$(EP94C_MATRIX_DIR)"
	python tools\ep94c_matrix.py --base-image $(OS_IMG) --active $(EP94B_ABI1_DIR)\valid.zsys --candidate $(EP94B_ABI2_DIR)\valid.zsys --output $(EP94C_MATRIX_IMAGE) --fixtures-dir $(EP94C_FIXTURES_DIR) --fat32-start-lba $(FAT32_START_LBA)

run-ep94c-matrix: ep94c-matrix
	@if not exist "$(EP94C_MATRIX_IMAGE)" (echo Imagem guiada EP9.4C nao encontrada: $(EP94C_MATRIX_IMAGE) & exit /b 2)
	$(QEMU) $(QEMU_CPU_ARGS) -snapshot -monitor stdio -drive file=$(EP94C_MATRIX_IMAGE),format=raw,if=none,id=ep94cmatrix -device ide-hd,drive=ep94cmatrix,bootindex=1 $(QEMU_NET_ARGS)

$(RECOVERY_MENU_VGA_IMAGE): system-slots-matrix $(RECOVERY_STAGE2_VGA_BIN) $(STAGE2_BIN) $(RECOVERY_STAGE2_PATCH_TOOL)
	python $(RECOVERY_STAGE2_PATCH_TOOL) --base $(SYSTEM_SLOTS_MATRIX_DIR)\MENU_FAILED_VALID.img --stage2 $(RECOVERY_STAGE2_VGA_BIN) --reference-stage2 $(STAGE2_BIN) --output $@ --stage2-lba 1 --kernel-lba 64

run-recovery-menu-vga: $(RECOVERY_MENU_VGA_IMAGE)
	$(QEMU) $(QEMU_CPU_ARGS) -drive file=$(RECOVERY_MENU_VGA_IMAGE),format=raw,if=none,id=recoverymenuvga -device ide-hd,drive=recoverymenuvga,bootindex=1 $(QEMU_NET_ARGS)

run: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS)

run-stage2-lba: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_STAGE2_LBA_DISK_ARGS) $(QEMU_NET_ARGS)

$(STAGE2_CHS_IMG): $(OS_IMG)
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(OS_IMG)' -Destination '$(STAGE2_CHS_IMG)' -Force"

run-stage2-chs: $(STAGE2_CHS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_STAGE2_CHS_DISK_ARGS) $(QEMU_NET_ARGS)

run-usb: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_ARGS) $(QEMU_USB_DEVICE_ARGS)

run-usb-msc: $(OS_IMG) $(STORAGE_FIXTURES_STAMP)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_ARGS) $(QEMU_USB_DEVICE_ARGS) $(QEMU_USB_MSC_ARGS)

run-usb-hid: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_ARGS) $(QEMU_USB_HID_DEVICE_ARGS)

run-usb-wifi: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) $(QEMU_USB_WIFI_EHCI_ARGS) $(QEMU_USB_WIFI_ARGS)

$(STORAGE_FIXTURES_STAMP): $(STORAGE_FIXTURES_TOOL)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	python $(STORAGE_FIXTURES_TOOL) generate --output-dir $(BUILD_DIR)

storage-fixtures: $(STORAGE_FIXTURES_STAMP)

storage-fixtures-test:
	python $(STORAGE_FIXTURES_TOOL) selftest

storage-fixtures-verify: $(STORAGE_FIXTURES_STAMP)
	python $(STORAGE_FIXTURES_TOOL) verify --output-dir $(BUILD_DIR)

run-storage: $(OS_IMG) $(STORAGE_FIXTURES_STAMP)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) -drive format=raw,file=$(STORAGE_VALID_IMG),if=ide,index=1 -drive format=raw,file=$(STORAGE_CORRUPT_IMG),if=ide,index=2 -drive format=raw,file=$(STORAGE_UNKNOWN_IMG),if=ide,index=3 $(QEMU_NET_ARGS)

debug: $(OS_IMG)
	$(QEMU) $(QEMU_CPU_ARGS) $(QEMU_BOOT_DISK_ARGS) $(QEMU_NET_ARGS) -s -S &

q3check:
	python tools\q3check.py
	python tools\vendor_terminus.py --check

catalog-test: tools\test_catalog.py tests\unit\test_catalog.py tests\coverage\registry.json tests\catalog.json docs\qualidade\catalogo-testes.md
	python -m unittest tests.unit.test_catalog
	python tools\test_catalog.py validate --catalog tests\catalog.json --coverage-registry tests\coverage\registry.json
	python tools\test_catalog.py check-rendered --catalog tests\catalog.json --view docs\qualidade\catalogo-testes.md

catalog-test-strict: tools\test_catalog.py tests\coverage\registry.json tests\catalog.json
	python tools\test_catalog.py validate --strict --catalog tests\catalog.json --coverage-registry tests\coverage\registry.json

test-qemu: tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py run --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-qemu-selftest: tools\qemu_test_runner.py tests\unit\test_qemu_test_runner.py
	python tools\qemu_test_runner.py --self-test
	python -m unittest tests.unit.test_qemu_test_runner

test-tst4-qemu: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst4:memory-slab --iterations 1 --boot-timeout "$(TST4_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST4_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST4_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst4-qemu-paging-vma: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst4:paging-vma --iterations 1 --boot-timeout "$(TST4_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST4_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST4_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst4-qemu-execution: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst4:execution --iterations 1 --boot-timeout "$(TST4_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST4_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST4_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst4-qemu-storage-vfs: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst4:storage-vfs --iterations 1 --boot-timeout "$(TST4_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST4_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST4_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst4-qemu-network: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst4:network --iterations 1 --boot-timeout "$(TST4_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST4_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST4_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst4-qemu-platform: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst4:platform --iterations 1 --boot-timeout "$(TST4_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST4_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST4_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-host: tools\tst5_host_runner.py tests\unit\test_tst5_runner.py tools\qemu_test_runner.py
	python tools\tst5_host_runner.py

test-tst5-qemu-shell: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:shell --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-qemu-input: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:input --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-qemu-apps: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:apps --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-qemu-processes: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:processes --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-qemu-storage: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:storage --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-qemu-network: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:network --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst5-qemu-update-recovery: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:update-recovery --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none --fixture readonly-update

test-tst5-qemu-reboot: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:reboot --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst5-qemu-poweroff: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst5:poweroff --iterations 1 --boot-timeout "$(TST5_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST5_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST5_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(QEMU_TEST_NETWORK)"

test-tst6-host: tools\tst6_host_runner.py tests\unit\test_tst6_runner.py tools\qemu_test_runner.py
	python tools\tst6_host_runner.py

test-tst6-qemu-matrix-baseline: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:baseline --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(TST6_QEMU_NETWORK)"

test-tst6-qemu-matrix-minimal: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:minimal --iterations 1 --qemu-profile minimal --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-matrix-network: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:network --iterations 1 --qemu-profile network --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(TST6_QEMU_NETWORK)"

test-tst6-qemu-matrix-usb-hid: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:usb-hid --iterations 1 --qemu-profile usb-hid --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-matrix-usb-storage: $(OS_IMG) $(STORAGE_FIXTURES_STAMP) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:usb-storage --iterations 1 --qemu-profile usb-storage --storage-image "$(STORAGE_VALID_IMG)" --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-matrix-audio: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:audio --iterations 1 --qemu-profile audio --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-matrix-display: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:display --iterations 1 --qemu-profile display --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-matrix-pci: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:matrix:pci --iterations 1 --qemu-profile pci --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-stress-kernel: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:stress:kernel --iterations "$(TST6_QEMU_STRESS_ITERATIONS)" --suite-timeout "$(TST6_QEMU_STRESS_DURATION)" --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(TST6_QEMU_NETWORK)"

test-tst6-qemu-stress-storage: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:stress:storage --iterations "$(TST6_QEMU_STRESS_ITERATIONS)" --suite-timeout "$(TST6_QEMU_STRESS_DURATION)" --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-stress-network: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:stress:network --iterations "$(TST6_QEMU_STRESS_ITERATIONS)" --suite-timeout "$(TST6_QEMU_STRESS_DURATION)" --qemu-profile network --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(TST6_QEMU_NETWORK)"

test-tst6-qemu-stress-apps: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:stress:apps --iterations "$(TST6_QEMU_STRESS_ITERATIONS)" --suite-timeout "$(TST6_QEMU_STRESS_DURATION)" --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-memory: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:memory --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-block: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:block --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-block-cache: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:block-cache --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-package: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:package --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-update: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:update --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-network: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:network --iterations 1 --qemu-profile network --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network "$(TST6_QEMU_NETWORK)"

test-tst6-qemu-fault-process: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:process --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst6-qemu-fault-recovery: $(OS_IMG) tools\qemu_test_runner.py tests\catalog.json
	@if not exist "$(OS_IMG)" (echo Imagem ausente: $(OS_IMG) & exit /b 2)
	python tools\qemu_test_runner.py stress --case qemu:tst6:fault:recovery --iterations 1 --qemu-profile baseline --boot-timeout "$(TST6_QEMU_BOOT_TIMEOUT)" --case-timeout "$(TST6_QEMU_CASE_TIMEOUT)" --heartbeat-timeout "$(TST6_QEMU_HEARTBEAT_TIMEOUT)" --image "$(OS_IMG)" --catalog tests\catalog.json --qemu $(QEMU) --cpu "$(QEMU_TEST_CPU)" --network none

test-tst7-host: tools\tst7_regression_runner.py tests\unit\test_tst7_runner.py tests\catalog.json tests\regressions\manifest.json
	python -m unittest tests.unit.test_tst7_runner

test-tst7-quick: tools\tst7_regression_runner.py tests\catalog.json tests\regressions\manifest.json
	python tools\tst7_regression_runner.py quick --make "$(MAKE)" --qemu $(QEMU) --image "$(OS_IMG)" --command-timeout "$(TST7_COMMAND_TIMEOUT)" --suite-timeout "$(TST7_QUICK_TIMEOUT)"

test-tst7-full: tools\tst7_regression_runner.py tools\test_catalog.py tests\coverage\registry.json tests\catalog.json tests\regressions\manifest.json
	python tools\tst7_regression_runner.py full --strict-coverage --make "$(MAKE)" --qemu $(QEMU) --image "$(OS_IMG)" --command-timeout "$(TST7_COMMAND_TIMEOUT)" --suite-timeout "$(TST7_FULL_TIMEOUT)"

test-tst7-continuous-host: tools\tst7_continuous_runner.py tests\unit\test_tst7_continuous_runner.py
	python -m unittest tests.unit.test_tst7_continuous_runner

test-tst7-continuous: tools\tst7_continuous_runner.py tools\tst7_regression_runner.py
	python tools\tst7_continuous_runner.py start --mode full --forever --interval 60 --cycle-timeout "$(TST7_FULL_TIMEOUT)" --make "$(MAKE)" --qemu $(QEMU) --image "$(OS_IMG)" --catalog tests\catalog.json

test-core-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_core_contracts.c tests\unit\test_core_host_runner.py tests\catalog.json
	python tools\core_host_runner.py --cc "$(HOST_CC)"

test-network-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_network_host.c tests\catalog.json
	python tools\core_host_runner.py --case host:core:net-buffer --cc "$(HOST_CC)"

test-network-manager-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_network_manager_host.c tests\catalog.json src\core\network_manager.c src\core\recovery.c
	python tools\core_host_runner.py --case host:core:network-manager --cc "$(HOST_CC)"

test-route-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_route_host.c tests\catalog.json
	python tools\core_host_runner.py --case host:network:route --cc "$(HOST_CC)"

test-ipv4-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_ipv4_host.c tests\catalog.json
	python tools\core_host_runner.py --case host:network:ipv4 --cc "$(HOST_CC)"

test-crypto-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_crypto_host.c tests\catalog.json
	python tools\core_host_runner.py --case host:core:crypto --cc "$(HOST_CC)"

test-scheduling-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_core_scheduling_host.c tests\catalog.json
	python tools\core_host_runner.py --case host:core:scheduling --cc "$(HOST_CC)"

test-package-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_package_host.c tests\catalog.json src\core\app_package.c
	python tools\core_host_runner.py --case host:core:app-package --cc "$(HOST_CC)"

test-state-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_core_state_host.c tests\catalog.json src\core\recovery.c src\core\power_notifier.c
	python tools\core_host_runner.py --case host:core:state --cc "$(HOST_CC)"

test-device-manager-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_device_manager_host.c tests\catalog.json src\core\device_manager.c src\core\recovery.c
	python tools\core_host_runner.py --case host:core:device-manager --cc "$(HOST_CC)"

test-app-api-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_app_api_host.c tests\catalog.json src\core\app_api.c
	python tools\core_host_runner.py --case host:core:app-api --cc "$(HOST_CC)"

test-app-files-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_app_files_host.c tests\catalog.json src\core\app_files.c src\include\core\app_files.h
	python tools\core_host_runner.py --case host:core:app-files --cc "$(HOST_CC)"

test-app-builtin-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_app_builtin_host.c tests\catalog.json src\core\app_builtin.c src\include\core\app_builtin.h src\include\core\app_loader.h
	python tools\core_host_runner.py --case host:core:app-builtin --cc "$(HOST_CC)"

test-app-catalog-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_app_catalog_host.c tests\catalog.json src\core\app_catalog.c
	python tools\core_host_runner.py --case host:core:app-catalog --cc "$(HOST_CC)"

test-input-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_input_host.c tests\catalog.json src\core\input.c
	python tools\core_host_runner.py --case host:core:input --cc "$(HOST_CC)"

test-power-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_power_host.c tests\catalog.json src\core\power.c src\core\power_notifier.c
	python tools\core_host_runner.py --case host:core:power --cc "$(HOST_CC)"

test-vfs-path-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_vfs_path_host.c tests\catalog.json src\fs\vfs_path.c src\include\fs\vfs_internal.h
	python tools\core_host_runner.py --case host:storage:vfs-path --cc "$(HOST_CC)"

test-file-index-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_file_index_host.c tests\catalog.json src\fs\file_index.c src\include\fs\file_index.h
	python tools\core_host_runner.py --case host:storage:file-index --cc "$(HOST_CC)"

test-fs-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_fs_host.c tests\catalog.json src\fs\fs.c src\include\fs\fs.h
	python tools\core_host_runner.py --case host:storage:fs --cc "$(HOST_CC)"

test-storage-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_storage_host.c tests\catalog.json src\fs\storage.c src\include\fs\storage.h
	python tools\core_host_runner.py --case host:storage:storage --cc "$(HOST_CC)"

test-block-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_block_host.c tests\catalog.json src\fs\block.c src\fs\block_cache.c src\include\fs\block.h src\include\fs\block_cache.h
	python tools\core_host_runner.py --case host:storage:block --cc "$(HOST_CC)"

test-fat12-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_fat12_host.c tests\catalog.json src\fs\fat12.c src\include\fs\fat12.h
	python tools\core_host_runner.py --case host:storage:fat12 --cc "$(HOST_CC)"

test-fat32-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_fat32_host.c tests\catalog.json src\fs\fat32.c src\include\fs\fat32.h
	python tools\core_host_runner.py --case host:storage:fat32 --cc "$(HOST_CC)"

test-vfs-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_vfs_host.c tests\catalog.json src\fs\vfs.c src\include\fs\vfs.h
	python tools\core_host_runner.py --case host:storage:vfs --cc "$(HOST_CC)"

test-slab-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_slab_metadata_host.c tests\catalog.json src\memory\slab.c src\include\memory\slab.h
	python tools\core_host_runner.py --case host:memory:slab-metadata --cc "$(HOST_CC)"

test-timer-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_timer_host.c tests\catalog.json src\drivers\timer.c src\include\core\timer.h
	python tools\core_host_runner.py --case host:core:timer --cc "$(HOST_CC)"

test-udp-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_udp_host.c tests\catalog.json src\core\udp.c src\include\core\udp.h
	python tools\core_host_runner.py --case host:network:udp --cc "$(HOST_CC)"

test-arp-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_arp_host.c tests\catalog.json src\core\arp.c src\include\core\arp.h
	python tools\core_host_runner.py --case host:network:arp --cc "$(HOST_CC)"

test-icmp-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_icmp_host.c tests\catalog.json src\core\icmp.c src\include\core\icmp.h
	python tools\core_host_runner.py --case host:network:icmp --cc "$(HOST_CC)"

test-dns-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_dns_host.c tests\catalog.json src\core\dns.c src\include\core\dns.h
	python tools\core_host_runner.py --case host:network:dns --cc "$(HOST_CC)"

test-dhcp-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_dhcp_host.c tests\catalog.json src\core\dhcp.c src\include\core\dhcp.h
	python tools\core_host_runner.py --case host:network:dhcp --cc "$(HOST_CC)"

test-ethernet-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_ethernet_host.c tests\catalog.json src\core\ethernet.c src\core\sk_buff.c src\core\net_buffer.c src\memory\slab.c src\include\core\ethernet.h src\include\core\sk_buff.h src\include\core\net_buffer.h
	python tools\core_host_runner.py --case host:network:ethernet --cc "$(HOST_CC)"

test-tcp-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_tcp_host.c tests\catalog.json src\core\tcp.c src\include\core\tcp.h
	python tools\core_host_runner.py --case host:network:tcp --cc "$(HOST_CC)"

test-tls-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_tls_host.c tests\catalog.json src\core\tls.c src\include\core\tls.h
	python tools\core_host_runner.py --case host:security:tls --cc "$(HOST_CC)"

test-http-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_http_host.c tests\catalog.json src\core\http.c src\include\core\http.h
	python tools\core_host_runner.py --case host:network:http --cc "$(HOST_CC)"

test-net-socket-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_net_socket_host.c tests\catalog.json src\core\net_socket.c src\core\wait.c src\core\net_buffer.c src\include\core\net_socket.h src\include\core\wait.h
	python tools\core_host_runner.py --case host:network:socket --cc "$(HOST_CC)"

test-vma-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_vma_host.c tests\catalog.json src\memory\vma.c src\include\memory\vma.h
	python tools\core_host_runner.py --case host:memory:vma --cc "$(HOST_CC)"

test-paging-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_paging_host.c tests\catalog.json src\memory\paging.c src\include\memory\paging.h
	python tools\core_host_runner.py --case host:memory:paging --cc "$(HOST_CC)"

test-memory-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_memory_host.c tests\catalog.json src\memory\memory.c src\include\core\memory.h
	python tools\core_host_runner.py --case host:memory:memory --cc "$(HOST_CC)"

test-process-signal-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_process_signal_host.c tests\catalog.json src\process\signal.c src\include\process\signal.h
	python tools\core_host_runner.py --case host:process:signals --cc "$(HOST_CC)"

test-process-ipc-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_process_ipc_host.c tests\catalog.json src\process\ipc.c src\include\process\process.h
	python tools\core_host_runner.py --case host:process:ipc --cc "$(HOST_CC)"

test-workqueue-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_workqueue_host.c tests\catalog.json src\core\workqueue.c src\include\core\workqueue.h
	python tools\core_host_runner.py --case host:core:workqueue --cc "$(HOST_CC)"

test-bearssl-compat-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_bearssl_compat_host.c tests\catalog.json src\core\bearssl_compat.c src\include\types.h
	python tools\core_host_runner.py --case host:core:bearssl-compat --cc "$(HOST_CC)"

test-shell-dispatch-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_shell_dispatch_host.c tests\catalog.json src\shell\shell_dispatch.c src\include\apps\shell_dispatch.h
	python tools\core_host_runner.py --case host:shell:dispatch --cc "$(HOST_CC)"

test-shell-introspection-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_shell_introspection_host.c tests\catalog.json src\shell\shell_introspection.c src\include\apps\shell_introspection.h src\core\string.c
	python tools\core_host_runner.py --case host:shell:introspection --cc "$(HOST_CC)"

test-font-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_font_host.c tests\catalog.json src\drivers\font.c src\drivers\font_data.inc src\include\drivers\font.h
	python tools\core_host_runner.py --case host:drivers:font --cc "$(HOST_CC)"

test-rtc-status-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_rtc_host.c tests\catalog.json src\drivers\rtc.c src\include\drivers\rtc.h src\core\string.c
	python tools\core_host_runner.py --case host:drivers:rtc-status --cc "$(HOST_CC)"

test-wifi-manager-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_wifi_manager_host.c tests\catalog.json src\core\wifi_manager.c src\include\core\wifi_manager.h src\include\drivers\pci.h src\include\drivers\rtl8811cu.h src\include\core\usb_manager.h src\core\string.c
	python tools\core_host_runner.py --case host:core:wifi-manager --cc "$(HOST_CC)"

test-usb-manager-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_usb_manager_host.c tests\catalog.json src\core\usb_manager.c src\include\core\usb_manager.h src\include\drivers\pci.h src\include\drivers\uhci.h src\include\drivers\ehci.h src\include\drivers\usb_hid.h src\include\drivers\usb_msc.h src\include\core\recovery.h src\core\string.c
	python tools\core_host_runner.py --case host:core:usb-manager --cc "$(HOST_CC)"

test-usb-hid-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_usb_hid_host.c tests\catalog.json src\drivers\usb_hid.c src\include\drivers\usb_hid.h src\include\core\usb_manager.h src\include\drivers\uhci.h src\include\core\input.h src\core\string.c
	python tools\core_host_runner.py --case host:drivers:usb-hid --cc "$(HOST_CC)"

test-usb-msc-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_usb_msc_host.c tests\catalog.json src\drivers\usb_msc.c src\include\drivers\usb_msc.h src\include\core\usb_manager.h src\include\drivers\uhci.h src\include\fs\block.h src\core\string.c
	python tools\core_host_runner.py --case host:drivers:usb-msc --cc "$(HOST_CC)"

test-devfs-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_devfs_host.c tests\catalog.json src\fs\devfs.c src\include\fs\devfs.h src\include\fs\vfs.h src\include\fs\vfs_internal.h src\include\fs\block.h src\include\fs\block_cache.h src\include\core\app_api.h src\include\drivers\speaker.h src\core\string.c
	python tools\core_host_runner.py --case host:storage:devfs --cc "$(HOST_CC)"

test-procfs-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_procfs_host.c tests\catalog.json src\fs\procfs.c src\include\fs\procfs.h src\include\fs\vfs.h src\include\core\log.h src\include\core\memory.h src\include\core\timer.h src\include\memory\slab.h src\include\process\process.h src\core\string.c
	python tools\core_host_runner.py --case host:storage:procfs --cc "$(HOST_CC)"

test-wav-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_wav_host.c tests\catalog.json src\fs\wav.c src\include\fs\wav.h src\include\core\memory.h src\include\drivers\ac97.h src\core\string.c
	python tools\core_host_runner.py --case host:storage:wav --cc "$(HOST_CC)"

test-bmp-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_bmp_host.c tests\catalog.json src\fs\bmp.c src\include\fs\bmp.h src\include\drivers\vesa.h src\include\core\memory.h src\include\core\video.h src\include\core\log.h src\core\string.c
	python tools\core_host_runner.py --case host:storage:bmp --cc "$(HOST_CC)"

test-shell-input-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_shell_input_host.c tests\catalog.json src\shell\shell_input.c src\include\apps\shell_input.h src\include\apps\shell.h src\include\core\keyboard.h src\include\core\video.h src\core\string.c
	python tools\core_host_runner.py --case host:shell:input --cc "$(HOST_CC)"

test-shell-command-utils-host: tools\core_host_runner.py tools\coverage_collector.py tests\unit\test_shell_command_utils_host.c tests\catalog.json src\shell\shell_command_utils.c src\include\apps\shell_command_utils.h src\include\core\video.h src\include\core\errors.h src\core\string.c
	python tools\core_host_runner.py --case host:shell:command-utils --cc "$(HOST_CC)"

test-tst2-host: tools\tst2_host_runner.py tests\unit\test_protocol_core.c tests\unit\test_qemu_test_runner.py src\core\test_protocol_core.c src\core\test_protocol_core.h
	python tools\tst2_host_runner.py --cc "$(HOST_CC)"

test-tst3-host: tools\tst3_host_runner.py tests\unit\test_string_compress.c tests\unit\test_packager.py tests\unit\test_updater.py src\core\string.c src\memory\compress.c
	python tools\tst3_host_runner.py --mode strict --cc "$(HOST_CC)"

test-tst3-sanitize: tools\tst3_host_runner.py tests\unit\test_string_compress.c src\core\string.c src\memory\compress.c
	python tools\tst3_host_runner.py --mode sanitize --sanitize-cc "$(HOST_SANITIZE_CC)"

q3check-test:
	python tools\q3check.py --self-test

package-test:
	python tools\packager.py selftest

update-test:
	python tools\updater.py selftest

package-demo: $(OS_IMG)
	python tools\packager.py demo --output $(BUILD_DIR)\DEMO.zephyrosapp --image $(OS_IMG)

store-test:
	python tools\packager.py selftest
	python tools\packager.py audit-store --fixtures-dir $(STORE_FIXTURES_DIR)

store-demo: $(OS_IMG) $(STORE_FIXTURES)
	python tools\packager.py inject-file-fat32 --file $(STORE_FIXTURES_DIR)\VALID.ZPK --image $(OS_IMG) --path VALID.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_FIXTURES_DIR)\BADCRC.ZPK --image $(OS_IMG) --path BADCRC.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_FIXTURES_DIR)\BADAPI.ZPK --image $(OS_IMG) --path BADAPI.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_FIXTURES_DIR)\BADALIAS.ZPK --image $(OS_IMG) --path BADALIAS.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_FIXTURES_DIR)\NEEDSDEP.ZPK --image $(OS_IMG) --path NEEDSDEP.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_FIXTURES_DIR)\SAMEVER.ZPK --image $(OS_IMG) --path SAMEVER.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py audit-store --fixtures-dir $(STORE_FIXTURES_DIR) --image $(OS_IMG)

store-as2-test:
	python tools\packager.py selftest
	python tools\packager.py audit-store-as2 --fixtures-dir $(STORE_AS2_FIXTURES_DIR)

store-as2-demo: $(OS_IMG) $(STORE_AS2_FIXTURES)
	python tools\packager.py inject-file-fat32 --file $(STORE_AS2_FIXTURES_DIR)\WAITAPP.ZPK --image $(OS_IMG) --path WAITAPP.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS2_FIXTURES_DIR)\BASE.ZPK --image $(OS_IMG) --path BASE.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS2_FIXTURES_DIR)\DEPEND.ZPK --image $(OS_IMG) --path DEPEND.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py audit-store-as2 --fixtures-dir $(STORE_AS2_FIXTURES_DIR) --image $(OS_IMG)

store-as4-test:
	python tools\packager.py selftest
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_SEED_FIXTURES_DIR)
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_UPDATE_FIXTURES_DIR)

store-as4-seed-demo: $(OS_IMG) $(STORE_AS4_SEED_FIXTURES)
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_SEED_FIXTURES_DIR)\UPTARGET.ZPK --image $(OS_IMG) --path UPTARGET.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_SEED_FIXTURES_DIR)\UPDEPA.ZPK --image $(OS_IMG) --path UPDEPA.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_SEED_FIXTURES_DIR)\UPDEPB.ZPK --image $(OS_IMG) --path UPDEPB.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_SEED_FIXTURES_DIR)\BROKEN.ZPK --image $(OS_IMG) --path BROKEN.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_SEED_FIXTURES_DIR)\CYCLEA.ZPK --image $(OS_IMG) --path CYCLEA.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_SEED_FIXTURES_DIR)\CYCLEB.ZPK --image $(OS_IMG) --path CYCLEB.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_SEED_FIXTURES_DIR) --image $(OS_IMG)

store-as4-update-demo: $(OS_IMG) $(STORE_AS4_UPDATE_FIXTURES)
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPTARGET.ZPK --image $(OS_IMG) --path UPTARGET.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPA.ZPK --image $(OS_IMG) --path UPDEPA.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\UPDEPB.ZPK --image $(OS_IMG) --path UPDEPB.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\BROKEN.ZPK --image $(OS_IMG) --path BROKEN.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEA.ZPK --image $(OS_IMG) --path CYCLEA.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py inject-file-fat32 --file $(STORE_AS4_UPDATE_FIXTURES_DIR)\CYCLEB.ZPK --image $(OS_IMG) --path CYCLEB.ZPK --fat32-start-lba $(FAT32_START_LBA) --replace
	python tools\packager.py audit-store-as4 --fixtures-dir $(STORE_AS4_UPDATE_FIXTURES_DIR) --image $(OS_IMG)

store-as5-test:
	python tools\packager.py audit-store-as5 --fixtures-dir $(STORE_AS5_FIXTURES_DIR) --public $(STORE_AS5_PUBLIC) --header src\include\core\app_remote_trust.h

store-as5-seed-demo: store-as5-test
	python tools\packager.py serve-store-as5 --fixtures-dir $(STORE_AS5_FIXTURES_DIR) --profile seed --port 8000

store-as5-serve: store-as5-test
	python tools\packager.py serve-store-as5 --fixtures-dir $(STORE_AS5_FIXTURES_DIR) --profile update --port 8000

clean:
	rmdir /s /q $(BUILD_DIR)

.PHONY: all coverage-image coverage-map run run-stage2-lba run-stage2-chs run-usb run-usb-msc run-usb-hid run-usb-wifi run-system-fixture run-system-slots-fixture run-system-slots-matrix run-system-update-matrix ep94b-fixtures ep94b-matrix run-ep94b-matrix ep94c-matrix run-ep94c-matrix run-recovery-menu-vga run-storage storage-fixtures storage-fixtures-test storage-fixtures-verify system-fixtures system-slots-fixtures system-slots-matrix debug q3check catalog-test test-qemu test-qemu-selftest test-core-host test-tst2-host test-tst3-host test-tst3-sanitize test-tst4-qemu q3check-test package-test update-test package-demo store-test store-demo store-as2-test store-as2-demo store-as4-test store-as4-seed-demo store-as4-update-demo store-as5-test store-as5-seed-demo store-as5-serve clean
.PHONY: kernel-elf
.PHONY: test-tst4-qemu-paging-vma test-tst4-qemu-execution test-tst4-qemu-storage-vfs test-tst4-qemu-network test-tst4-qemu-platform
.PHONY: test-tst5-host test-tst5-qemu-shell test-tst5-qemu-input test-tst5-qemu-apps test-tst5-qemu-processes test-tst5-qemu-storage test-tst5-qemu-network test-tst5-qemu-update-recovery test-tst5-qemu-reboot test-tst5-qemu-poweroff
.PHONY: test-tst6-host test-tst6-qemu-matrix-baseline test-tst6-qemu-matrix-minimal test-tst6-qemu-matrix-network test-tst6-qemu-matrix-usb-hid test-tst6-qemu-matrix-usb-storage test-tst6-qemu-matrix-audio test-tst6-qemu-matrix-display test-tst6-qemu-matrix-pci test-tst6-qemu-stress-kernel test-tst6-qemu-stress-storage test-tst6-qemu-stress-network test-tst6-qemu-stress-apps test-tst6-qemu-fault-memory test-tst6-qemu-fault-block test-tst6-qemu-fault-block-cache test-tst6-qemu-fault-package test-tst6-qemu-fault-update test-tst6-qemu-fault-network test-tst6-qemu-fault-process test-tst6-qemu-fault-recovery
.PHONY: test-tst7-host test-tst7-quick test-tst7-full
.PHONY: test-tst7-continuous-host test-tst7-continuous
.PHONY: test-network-host test-network-manager-host test-route-host test-ipv4-host test-crypto-host test-scheduling-host test-package-host test-state-host test-device-manager-host test-app-api-host test-app-files-host test-app-builtin-host test-app-catalog-host test-input-host test-power-host test-vfs-path-host test-file-index-host test-fs-host test-storage-host test-block-host test-fat12-host test-fat32-host test-vfs-host test-slab-host test-timer-host test-udp-host test-arp-host test-icmp-host test-dns-host test-dhcp-host test-ethernet-host test-tcp-host test-tls-host test-http-host test-net-socket-host test-vma-host test-paging-host test-memory-host test-process-signal-host test-process-ipc-host test-workqueue-host test-bearssl-compat-host test-shell-dispatch-host test-shell-introspection-host test-font-host test-rtc-status-host test-wifi-manager-host test-usb-manager-host test-usb-hid-host test-usb-msc-host test-devfs-host test-procfs-host test-wav-host test-bmp-host test-shell-input-host test-shell-command-utils-host
.PHONY: catalog-test-strict
