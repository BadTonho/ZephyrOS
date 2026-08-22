#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_dispatch.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/clock.h"
#include "core/tls.h"
#include "core/wait.h"
#include "process/process.h"
#include "drivers/ata.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "apps/taskmanager.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
#include "ui/updater.h"
#include "ui/appstore.h"
#include "ui/wm.h"
#include "memory/compress.h"
#include "apps/mediaplayer.h"
#include "apps/editor.h"
#include "ui/filemanager.h"
#include "ui/icons.h"
#include "memory/paging.h"
#include "core/string.h"
#include "core/errors.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "apps/guitest.h"
#include "core/recovery.h"
#include "core/device_manager.h"
#include "core/usb_manager.h"
#include "drivers/usb_hid.h"
#include "drivers/usb_msc.h"
#include "core/arp.h"
#include "core/dhcp.h"
#include "core/dns.h"
#include "core/http.h"
#include "core/ipv4.h"
#include "core/icmp.h"
#include "core/net_socket.h"
#include "core/tcp.h"
#include "core/udp.h"
#include "core/network_manager.h"
#include "core/power.h"
#include "core/app_api.h"
#include "core/app_catalog.h"
#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/app_remote.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_config.h"
#include "core/syscall.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/acpi.h"
#include "drivers/rtc.h"
#include "ui/display.h"
#include "apps/shell_command_utils.h"
#include "apps/shell_runtime.h"

#define SHELL_Q2CHECK_FAULT_RUNS 2U
#define SHELL_HOSTED_DEFAULT_CONTENT_WIDTH 880
#define SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT 560
#define SHELL_HOSTED_FRAME_WIDTH 4
#define SHELL_HOSTED_FRAME_HEIGHT 28
#define SHELL_IPV4_TEXT_SIZE 16U
#define SHELL_IPV4_OCTET_COUNT 4U
#define SHELL_IPV4_OCTET_MAX 255U
#define SHELL_IPV4_OCTET_BITS 8U
#define SHELL_IPV4_OCTET_DIGITS 3U
#define SHELL_NET_QEMU_REPLY_IPV4 0x0A000202U
#define SHELL_NET_QEMU_TIMEOUT_IPV4 0x0A0002FEU
#define SHELL_NET_QEMU_SUBNET_MASK 0xFFFFFF00U
#define SHELL_NET_CHECK_WAIT_SECONDS 5U
#define SHELL_NET_CHECK_EXPECTED_ATTEMPTS 3U
#define SHELL_PING_WAIT_EXTRA_SECONDS 5U
#define SHELL_DHCP_WAIT_SECONDS 20U
#define SHELL_DNS_WAIT_SECONDS 20U
#define SHELL_TCP_WAIT_SECONDS 45U
#define SHELL_HTTP_WAIT_SECONDS 50U
#define SHELL_HTTP_SUITE_ATTEMPTS 3U
#define SHELL_HTTP_PREVIEW_SIZE 512U
#define SHELL_HEALTH_CHECK_TEXT_COLOR 0x07U
#define SHELL_HEALTH_CHECK_LABEL_COLOR 0x0BU
#define SHELL_HEALTH_CHECK_DETAIL_COLOR 0x08U
#define SHELL_HEALTH_CHECK_ERROR_COLOR 0x0CU
#define SHELL_HEALTH_CHECK_WARN_COLOR 0x0EU
#define SHELL_HEALTH_CHECK_READY_COLOR 0x0AU
#define SHELL_NETWORK_REQUIRED_IPV4_HANDLERS 3U
#define SHELL_DNS_NAME_SIZE DNS_NAME_BUFFER_SIZE
#define SHELL_MILLISECONDS_PER_SECOND 1000U
#define SHELL_MAX_TICK_INTERVAL 0xFFFFFFFFU
#define SHELL_NOINLINE __attribute__((noinline))
#define SHELL_UPDATE_SCANCODE_ESCAPE 0x01U
#define SHELL_UPDATE_SCANCODE_F12 0x58U
#define SHELL_INDEX_ACTION_SIZE 16U
#define SHELL_FS_DIRECTORY_ATTRIBUTE 0x10U
#define SHELL_LOG_TAIL_DEFAULT 10U
#define SHELL_LOG_TAIL_MAXIMUM LOG_RECORD_CAPACITY
#define SHELL_WHEEL_SCROLL_LINES 3
#define APP_CHECK_DEMO_PATH "DEMO.ZAP"
#define APP_CHECK_DEMO_MAX_CLEANUP 4U
#define APP_INPUT_TEST_PATH "INPUT.ZAP"
#define APP_INPUT_EVENT_OFFSET 128U
#define APP_INPUT_EVENT_DATA1_OFFSET (APP_INPUT_EVENT_OFFSET + 4U)
#define SHELL_APP_OUTPUTTEST_FAILURE_CODE 1U
#define SHELL_Q2CHECK_FIRST_FAULT_INDEX 0U
#define SHELL_Q2CHECK_SECOND_FAULT_INDEX 1U
#define SHELL_Q2CHECK_EXPECTED_FAULT_VECTOR 14U
#define SHELL_Q2CHECK_EXPECTED_FAULT_ERROR 4U
#define SHELL_MEMCHECK_BLOCK_A 96U
#define SHELL_MEMCHECK_BLOCK_B 160U
#define SHELL_MEMCHECK_BLOCK_C 224U
#define SHELL_REGCHECK_NON_ATA_DEVICE_COUNT 7U
#define SHELL_REGCHECK_PCI_NETWORK_CLASS 0x02U
#define SHELL_REGCHECK_ACPI_SDT_HEADER_SIZE 36U
#define SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE (1024U * 1024U)
#define SHELL_REGCHECK_ACPI_MAX_ROOT_ENTRIES 256U

typedef enum {
    SHELL_Q2CHECK_IDLE = 0,
    SHELL_Q2CHECK_FIRST_FAULT,
    SHELL_Q2CHECK_SECOND_FAULT
} shell_q2check_state_t;

typedef enum {
    SHELL_REGCHECK_IDLE = 0,
    SHELL_REGCHECK_WAIT_DEMO,
    SHELL_REGCHECK_WAIT_CANCEL
} shell_regcheck_state_t;

typedef struct {
    uint32_t initial_focus;
    uint32_t initial_user_count;
    uint32_t initial_zombie_count;
    uint32_t initial_fault_count;
    uint32_t expected_pid;
    int logger_result;
    int fault_result[SHELL_Q2CHECK_FAULT_RUNS];
    int summary_result;
    int cleanup_result;
    shell_q2check_state_t state;
} shell_q2check_t;

typedef struct {
    uint32_t initial_focus;
    uint32_t initial_process_count;
    uint32_t initial_user_count;
    uint32_t initial_zombie_count;
    uint32_t initial_user_directories;
    uint32_t initial_user_pages;
    uint32_t expected_pid;
    int health_result;
    int services_result;
    int scheduler_result;
    int memory_result;
    int package_result;
    int thread_result;
    int processes_result;
    int device_scan_result;
    int devices_result;
    int usb_result;
    int network_result;
    int acpi_result;
    int power_result;
    int index_result;
    int loader_result;
    int cancellation_result;
    int cleanup_result;
    uint8_t full_mode;
    uint8_t loader_started;
    uint8_t cancellation_started;
    shell_regcheck_state_t state;
} shell_regcheck_t;

typedef struct {
    uint8_t reply;
    uint8_t cache_hit;
    uint8_t timeout;
    uint8_t ipv4;
    uint8_t icmp;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_check_t;

typedef struct {
    uint8_t udp;
    uint8_t dhcp;
    uint8_t lease;
    uint8_t dns;
    uint8_t dns_cache;
    uint8_t icmp;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_dhcp_check_t;

typedef struct {
    uint8_t dhcp;
    uint8_t dns;
    uint8_t tcp;
    uint8_t checksum;
    uint8_t sockets;
    uint8_t http;
    uint8_t closing;
    uint8_t polling;
    uint8_t invariants;
} shell_net_qemu_tcp_check_t;

typedef struct {
    uint32_t ticks;
    keyboard_metrics_t keyboard;
    ipc_stats_t ipc;
    scheduler_stats_t scheduler;
    vesa_metrics_t vesa;
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;
    paging_user_stats_t paging_user;
    paging_boot_stats_t paging_boot;
} shell_kmetrics_snapshot_t;

typedef struct {
    shell_kmetrics_snapshot_t snapshot;
    uint8_t valid;
} shell_kmetrics_baseline_t;

typedef struct {
    char operation[16];
    char first[FS_MAX_PATH];
    char second[UPDATE_REMOTE_URL_SIZE];
    char third[32];
    char extra[2];
    update_remote_status_t remote_status;
    update_remote_options_t remote_options;
    update_remote_result_t remote_result;
} shell_update_workspace_t;

typedef struct {
    char operation[16];
    char value[FS_MAX_PATH];
    char option[16];
    char extra[16];
    app_catalog_entry_t entry;
    app_package_action_result_t action;
    app_package_status_t status;
    app_launch_info_t launch;
    uint32_t pid;
    char remote_url[APP_REMOTE_URL_SIZE];
    char remote_token[APP_REMOTE_URL_SIZE];
    app_remote_entry_t remote_entry;
    app_remote_status_t remote_status;
    app_remote_options_t remote_options;
    app_remote_result_t remote_result;
} shell_store_workspace_t;

typedef struct {
    char query[FILE_INDEX_QUERY_SIZE];
    file_index_result_t results[FILE_INDEX_MAX_RESULTS];
    file_index_search_status_t status;
} shell_index_workspace_t;


static log_record_t shell_log_records[SHELL_LOG_TAIL_MAXIMUM];
static timer_info_t shell_timer_records[TIMER_CAPACITY];
static wait_info_t shell_wait_records[MAX_PROCESSES + MAX_THREADS];
static shell_kmetrics_baseline_t shell_kmetrics_baseline;

static void cmd_health_print_component(recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);

    if (!entry) return;

    video_print("  ", 0x07);
    video_print(entry->name, 0x0B);
    video_print(": ", 0x07);
    video_print(recovery_state_name(entry->state), 0x0F);
    video_print("  falhas=", 0x08);
    shell_command_print_num(entry->failures);
    video_print("  erro=", 0x08);
    shell_command_print_num((uint32_t)entry->last_error);
    video_print("\n", 0x07);
    video_print("    motivo: ", 0x08);
    video_print(entry->last_message, 0x07);
    video_print("\n", 0x07);
}

int shell_core_migrated_builtin_is_ready(void) {
    return app_loader_is_ready() && app_api_is_ready() &&
           syscall_is_ready() && syscall_user_mode_is_enabled() &&
           paging_is_ready() && idt_is_user_syscall_enabled();
}

static void cmd_health_print_migrated_builtin(shell_builtin_app_t app) {
    int ready = shell_core_migrated_builtin_is_ready();

    video_print("  ZAPP ", 0x07);
    video_print(shell_core_builtin_app_name(app), 0x0B);
    video_print(": ", 0x07);
    video_print(ready ? "READY" : "NATIVE FALLBACK", ready ? 0x0A : 0x0E);
    video_print("\n", 0x07);
}

static void cmd_health_print_usb_hid(void);

static void cmd_health_print_user_fault(void) {
    process_user_fault_summary_t fault;
    uint32_t fault_count = process_get_user_fault_count();

    video_print("  Falhas isoladas user: total=", 0x07);
    shell_command_print_num(fault_count);
    video_print(" ultima=", 0x08);
    if (process_get_last_user_fault(&fault) != OK) {
        video_print("N/D", 0x08);
        video_print("\n", 0x07);
        return;
    }

    video_print("PID=", 0x08);
    shell_command_print_num(fault.pid);
    video_print(" vetor=", 0x08);
    shell_command_print_num(fault.vector);
    video_print(" erro=", 0x08);
    shell_command_print_num(fault.error);
    video_print("\n", 0x07);
}

static const char* shell_process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_READY: return "READY";
        case PROCESS_STATE_RUNNING: return "RUNNING";
        case PROCESS_STATE_BLOCKED: return "BLOCKED";
        case PROCESS_STATE_ZOMBIE: return "ZOMBIE";
        default: return "UNUSED";
    }
}

static void cmd_health_print_kernel(void) {
    process_t* current = process_get_current();
    ipc_stats_t ipc;
    app_api_version_t app_version;
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;
    paging_user_stats_t paging_user;

    ipc_get_stats(&ipc);
    memory_get_heap_stats(&heap);
    memory_get_pmm_stats(&pmm);
    paging_get_user_stats(&paging_user);
    video_print("\nEstado do kernel:\n", 0x0B);
    video_print("  Processo atual: PID ", 0x07);
    shell_command_print_num(process_get_current_pid());
    video_print("  estado=", 0x08);
    video_print(current ? shell_process_state_name(current->state) : "N/D", 0x07);
    video_print("\n", 0x07);

    video_print("  Processos: total=", 0x07);
    shell_command_print_num(process_get_count());
    video_print(" READY=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print("\n", 0x07);

    video_print("  Threads: ", 0x07);
    shell_command_print_num(thread_get_count());
    video_print("  ticks=", 0x08);
    shell_command_print_num(timer_get_ticks());
    video_print("\n", 0x07);

    video_print("  IPC: foco=", 0x07);
    shell_command_print_num(process_get_focus());
    video_print(" enviados=", 0x08);
    shell_command_print_num(ipc.sent);
    video_print(" recebidos=", 0x08);
    shell_command_print_num(ipc.received);
    video_print(" falhas=", 0x08);
    shell_command_print_num(ipc.failed);
    video_print(" filas_cheias=", 0x08);
    shell_command_print_num(ipc.queue_full);
    video_print("\n", 0x07);

    video_print("  Paging: ", 0x07);
    video_print(paging_is_ready() ? "READY" : "DISABLED", 0x0F);
    video_print("\n", 0x07);
    video_print("  App API: ", 0x07);
    video_print(app_api_is_ready() ? "READY" : "DISABLED", 0x0F);
    if (app_api_is_ready() && app_api_get_version(&app_version) == OK) {
        video_print(" v", 0x08);
        shell_command_print_num(app_version.major);
        video_print(".", 0x08);
        shell_command_print_num(app_version.minor);
    }
    video_print("\n", 0x07);
    video_print("  Syscalls: ", 0x07);
    video_print(syscall_is_ready() ? "READY" : "DISABLED", 0x0F);
    video_print("\n", 0x07);
    video_print("  Modo usuario: ", 0x07);
    video_print(syscall_user_mode_is_enabled() ? "READY" : "DISABLED", 0x0F);
    video_print("  gate=", 0x08);
    video_print(idt_is_user_syscall_enabled() ? "DPL3" : "DPL0", 0x0F);
    video_print("  processos=", 0x08);
    shell_command_print_num(process_get_user_count());
    video_print("\n", 0x07);
    video_print("  UserTest: ", 0x07);
    int user_test_found = 0;
    for (int user_index = 0; user_index < MAX_PROCESSES; user_index++) {
        if (processes[user_index].user_test &&
            processes[user_index].state != PROCESS_STATE_UNUSED) {
            shell_command_print_num(processes[user_index].pid);
            video_print(" ", 0x08);
            video_print(shell_process_state_name(processes[user_index].state), 0x0F);
            user_test_found = 1;
            break;
        }
    }
    if (!user_test_found) video_print("N/D", 0x08);
    video_print("\n", 0x07);
    video_print("  App ZAPP em foco: ", 0x07);
    if (app_loader_get_foreground_pid() != 0) {
        video_print("PID ", 0x08);
        shell_command_print_num(app_loader_get_foreground_pid());
    } else {
        video_print("N/D", 0x08);
    }
    video_print("\n", 0x07);
    cmd_health_print_migrated_builtin(SHELL_BUILTIN_APP_UPTIME);
    cmd_health_print_migrated_builtin(SHELL_BUILTIN_APP_MEM);
    video_print("  Pacotes: ", 0x07);
    video_print(app_package_is_ready() ? "READY" : "DISABLED",
                app_package_is_ready() ? 0x0A : 0x0E);
    video_print("\n", 0x07);
    cmd_health_print_user_fault();
    video_print("  File API: ", 0x07);
    video_print((app_api_file_is_ready() &&
                 recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) ?
                "READY" : "DISABLED", 0x0F);
    video_print("\n", 0x07);
    video_print("  IPC API: ", 0x07);
    video_print(app_api_ipc_is_ready() ? "READY" : "DISABLED", 0x0F);
    video_print("\n", 0x07);
    video_print("  Memoria KB: total=", 0x07);
    shell_command_print_num(memory_get_total() / 1024);
    video_print(" usada=", 0x08);
    shell_command_print_num(memory_get_used() / 1024);
    video_print(" livre=", 0x08);
    shell_command_print_num(memory_get_free() / 1024);
    video_print(" paginas_livres=", 0x08);
    shell_command_print_num(memory_get_free_pages());
    video_print("\n", 0x07);
    video_print("  Paginas: total=", 0x07);
    shell_command_print_num(memory_get_total_pages());
    video_print("\n", 0x07);
    video_print("  PMM: paginas_proprias=", 0x07);
    shell_command_print_num(pmm.owned_pages);
    video_print(" falhas=", 0x08);
    shell_command_print_num(pmm.allocation_failures);
    video_print(" rejeicoes=", 0x08);
    shell_command_print_num(pmm.invalid_frees);
    video_print("\n", 0x07);
    video_print("  Heap: ", 0x07);
    if (!heap.initialized || !heap.valid) {
        video_print("N/D\n", 0x08);
    } else {
        video_print("blocos_livres=", 0x07);
        shell_command_print_num(heap.free_blocks);
        video_print(" maior_livre=", 0x08);
        shell_command_print_num(heap.largest_free_block / 1024U);
        video_print(" KB fragmentacao=", 0x08);
        shell_command_print_num(heap.fragmentation_percent);
        video_print("% falhas=", 0x08);
        shell_command_print_num(heap.allocation_failures);
        video_print(" invalidos=", 0x08);
        shell_command_print_num(heap.invalid_frees + heap.double_frees);
        video_print("\n", 0x07);
    }
    video_print("  Paging user: diretorios=", 0x07);
    shell_command_print_num(paging_user.active_directories);
    video_print(" paginas=", 0x08);
    shell_command_print_num(paging_user.active_pages);
    video_print(" criados=", 0x08);
    shell_command_print_num(paging_user.directories_created);
    video_print(" liberados=", 0x08);
    shell_command_print_num(paging_user.directories_released);
    video_print(" rejeicoes=", 0x08);
    shell_command_print_num(paging_user.rejected_releases);
    video_print("\n", 0x07);
    cmd_health_print_usb_hid();
}

static void cmd_health_print_usb_hid(void) {
    usb_manager_status_t usb_status;
    input_metrics_t input_metrics;
    irq_deferred_status_t deferred_status;

    video_print("  USB HID: ", 0x07);
    if (usb_manager_get_status(&usb_status) != OK) {
        video_print("DISABLED\n", 0x0C);
        return;
    }
    if (!usb_status.controller_count) {
        video_print("DISABLED", 0x0C);
    } else {
        video_print(usb_status.interrupt_transfer_available ? "READY" :
                    "DEGRADED", usb_status.interrupt_transfer_available ?
                    0x0A : 0x0E);
    }
    video_print("  ativos=", 0x08);
    shell_command_print_num(usb_status.hid_active_count);
    video_print("  registrados=", 0x08);
    shell_command_print_num(usb_status.hid_device_count);
    if (input_get_metrics(&input_metrics) == OK) {
        video_print("  fila_kbd=", 0x08);
        shell_command_print_num(input_metrics.key_queued);
        video_print("  fila_ptr=", 0x08);
        shell_command_print_num(input_metrics.pointer_queued);
    }
    if (irq_deferred_get_status(&deferred_status) == OK) {
        video_print("  diferidos=", 0x08);
        shell_command_print_num(deferred_status.queued);
        video_print("/", 0x08);
        shell_command_print_num(deferred_status.capacity);
    }
    video_print("\n", 0x07);
}

static void cmd_health_print_update_capabilities(void) {
    update_capabilities_t capabilities;
    update_status_t status;
    update_remote_status_t remote;
    int status_ready;

    video_print("\nCapacidades de Update:\n", 0x0B);
    if (update_get_capabilities(&capabilities) != OK) {
        video_print("  verificacao local: DISABLED\n", 0x0C);
        video_print("  aplicacao: DISABLED\n", 0x08);
        video_print("  rollback: DISABLED\n", 0x08);
        video_print("  historico: DISABLED\n", 0x08);
        video_print("  remoto: DISABLED\n", 0x08);
        return;
    }
    status_ready = update_get_status(&status) == OK;
    if (status_ready) capabilities = status.capabilities;
    video_print("  verificacao local: ", 0x07);
    if (!capabilities.verifier_ready) {
        video_print("DISABLED\n", 0x0C);
    } else if (!capabilities.local_file_available) {
        video_print("DEGRADED (filesystem)\n", 0x0E);
    } else {
        video_print("READY\n", 0x0A);
    }
    video_print("  aplicacao: ", 0x07);
    if (capabilities.apply_available) {
        video_print("READY\n", 0x0A);
    } else if (capabilities.recovery_pending) {
        video_print("DEGRADED (recuperacao pendente)\n", 0x0E);
    } else if (fs_get_type() == FS_TYPE_FAT12 &&
               !capabilities.persistent_state_ready) {
        video_print("DEGRADED (estado persistente)\n", 0x0E);
    } else {
        video_print("DISABLED (requer FAT12)\n", 0x08);
    }
    video_print("  rollback: ", 0x07);
    if (capabilities.rollback_available) {
        video_print("READY\n", 0x0A);
    } else if (capabilities.apply_available) {
        video_print("DISABLED (sem backup)\n", 0x08);
    } else {
        video_print("DISABLED\n", 0x08);
    }
    video_print("  historico: ", 0x07);
    if (capabilities.history_available) {
        video_print("READY\n", 0x0A);
    } else if (status_ready &&
               status.history_store == UPDATE_STORE_INVALID) {
        video_print("DEGRADED (integridade)\n", 0x0E);
    } else {
        video_print("DISABLED (requer FAT12)\n", 0x08);
    }
    video_print("  remoto: ", 0x07);
    if (update_remote_get_status(&remote) != OK || !remote.enabled) {
        video_print("DISABLED\n", 0x08);
    } else if (!remote.network_ready ||
               remote.state == UPDATE_REMOTE_STATE_FAILED ||
               remote.cache_store == UPDATE_REMOTE_STORE_INVALID) {
        video_print("DEGRADED (", 0x0E);
        video_print(update_remote_reason_name(remote.reason), 0x0E);
        video_print(")\n", 0x0E);
    } else {
        video_print("READY\n", 0x0A);
    }
}

static void cmd_health_print_clock_tls(void) {
    clock_status_t clock_status;
    tls_status_t tls_status;

    video_print("\nTempo confiavel e TLS:\n", 0x0B);
    video_print("  UTC: ", 0x07);
    if (clock_get_status(&clock_status) != OK ||
        !clock_status.initialized) {
        video_print("DISABLED\n", 0x0C);
    } else if (clock_status.utc_available) {
        video_print("READY (", 0x0A);
        video_print(clock_source_name(clock_status.source), 0x0A);
        video_print(") monotono=READY\n", 0x0A);
    } else {
        video_print("DEGRADED (monotono sem UTC)\n", 0x0E);
    }
    video_print("  TLS: ", 0x07);
    if (tls_get_status(&tls_status) != OK) {
        video_print("UNAVAILABLE\n", 0x0C);
        return;
    }
    video_print(tls_state_name(tls_status.state),
                tls_status.state == TLS_STATE_READY ? 0x0A : 0x0E);
    video_print(" tempo=", 0x07);
    video_print(tls_status.trusted_time_available ? "TRUSTED" : "UNAVAILABLE",
                tls_status.trusted_time_available ? 0x0A : 0x0E);
    video_print(" handshake=", 0x07);
    video_print(tls_status.handshake_available ? "READY" : "UNAVAILABLE",
                tls_status.handshake_available ? 0x0A : 0x0E);
    video_print(" x509=", 0x07);
    video_print(tls_status.certificate_validation_available ? "READY" :
                "UNAVAILABLE",
                tls_status.certificate_validation_available ? 0x0A : 0x0E);
    video_print(" entropy=", 0x07);
    video_print(tls_status.entropy_available ? "RDRAND" : "UNAVAILABLE",
                tls_status.entropy_available ? 0x0A : 0x0E);
    video_print(" HTTPS=", 0x07);
    video_print(tls_capability_available() ? "READY\n" : "UNAVAILABLE\n",
                tls_capability_available() ? 0x0A : 0x0E);
}

uint8_t shell_diagnostics_health_state_color(recovery_state_t state) {
    if (state == RECOVERY_STATE_READY) return 0x0A;
    if (state == RECOVERY_STATE_DEGRADED) return 0x0E;
    return 0x0C;
}

static void cmd_health_print_summary_component(
    const recovery_component_t* entry) {
    if (!entry) return;
    video_print("  ", 0x07);
    video_print(entry->name, 0x0B);
    video_print(": ", 0x07);
    video_print(recovery_state_name(entry->state),
                shell_diagnostics_health_state_color(entry->state));
    video_print(" erro=", 0x08);
    shell_command_print_num((uint32_t)entry->last_error);
    video_print(" falhas=", 0x08);
    shell_command_print_num(entry->failures);
    video_print("\n", 0x07);
}

static void cmd_health_print_summary_components(void) {
    uint32_t counts[4] = {0, 0, 0, 0};

    for (uint32_t index = 0; index < recovery_get_count(); index++) {
        const recovery_component_t* entry =
            recovery_get((recovery_component_id_t)index);
        recovery_state_t state = entry ?
            entry->state : RECOVERY_STATE_UNKNOWN;

        if (state > RECOVERY_STATE_DISABLED) {
            state = RECOVERY_STATE_UNKNOWN;
        }
        counts[state]++;
    }
    video_print("  Componentes: READY=", 0x07);
    shell_command_print_num(counts[RECOVERY_STATE_READY]);
    video_print(" DEGRADED=", 0x08);
    shell_command_print_num(counts[RECOVERY_STATE_DEGRADED]);
    video_print(" DISABLED=", 0x08);
    shell_command_print_num(counts[RECOVERY_STATE_DISABLED]);
    video_print(" UNKNOWN=", 0x08);
    shell_command_print_num(counts[RECOVERY_STATE_UNKNOWN]);
    video_print("\n", 0x07);

    for (uint32_t index = 0; index < recovery_get_count(); index++) {
        const recovery_component_t* entry =
            recovery_get((recovery_component_id_t)index);

        if (!entry || entry->state == RECOVERY_STATE_READY ||
            index == RECOVERY_COMPONENT_UPDATE ||
            index == RECOVERY_COMPONENT_APP_STORE) continue;
        cmd_health_print_summary_component(entry);
    }
}

static recovery_state_t cmd_health_update_local_state(
    const update_capabilities_t* capabilities) {
    if (!capabilities->verifier_ready) return RECOVERY_STATE_DISABLED;
    if (!capabilities->local_file_available) return RECOVERY_STATE_DEGRADED;
    return RECOVERY_STATE_READY;
}

static recovery_state_t cmd_health_update_apply_state(
    const update_capabilities_t* capabilities) {
    if (capabilities->apply_available) return RECOVERY_STATE_READY;
    if (capabilities->recovery_pending ||
        (fs_get_type() == FS_TYPE_FAT12 &&
         !capabilities->persistent_state_ready)) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_DISABLED;
}

static recovery_state_t cmd_health_update_history_state(
    const update_capabilities_t* capabilities,
    const update_status_t* status, int status_ready) {
    if (capabilities->history_available) return RECOVERY_STATE_READY;
    if (status_ready && status->history_store == UPDATE_STORE_INVALID) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_DISABLED;
}

static recovery_state_t cmd_health_remote_state_from_status(
    const update_remote_status_t* remote) {
    if (!remote || !remote->enabled) return RECOVERY_STATE_DISABLED;
    if (!remote->network_ready ||
        remote->state == UPDATE_REMOTE_STATE_FAILED ||
        remote->cache_store == UPDATE_REMOTE_STORE_INVALID) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_READY;
}

static recovery_state_t cmd_health_update_remote_state(void) {
    update_remote_status_t remote;

    if (update_remote_get_status(&remote) != OK || !remote.enabled) {
        return RECOVERY_STATE_DISABLED;
    }
    return cmd_health_remote_state_from_status(&remote);
}

static void cmd_health_print_inline_state(recovery_state_t state) {
    video_print(recovery_state_name(state),
                shell_diagnostics_health_state_color(state));
}

static void cmd_health_print_summary_update(void) {
    const recovery_component_t* component =
        recovery_get(RECOVERY_COMPONENT_UPDATE);
    update_capabilities_t capabilities;
    update_status_t status;
    int capabilities_ready =
        update_get_capabilities(&capabilities) == OK;
    int status_ready = update_get_status(&status) == OK;

    if (!capabilities_ready) kmemset(&capabilities, 0, sizeof(capabilities));
    if (!status_ready) kmemset(&status, 0, sizeof(status));
    if (status_ready) capabilities = status.capabilities;
    video_print("  Update: ", 0x07);
    if (component) {
        cmd_health_print_inline_state(component->state);
        if (component->state != RECOVERY_STATE_READY) {
            video_print(" erro=", 0x08);
            shell_command_print_num((uint32_t)component->last_error);
            video_print(" falhas=", 0x08);
            shell_command_print_num(component->failures);
        }
    } else {
        video_print("UNKNOWN", 0x0C);
    }
    video_print("\n    local=", 0x07);
    cmd_health_print_inline_state(
        cmd_health_update_local_state(&capabilities));
    video_print(" apply=", 0x08);
    cmd_health_print_inline_state(
        cmd_health_update_apply_state(&capabilities));
    video_print(" rollback=", 0x08);
    cmd_health_print_inline_state(
        capabilities.rollback_available ?
        RECOVERY_STATE_READY : RECOVERY_STATE_DISABLED);
    video_print(" historico=", 0x08);
    cmd_health_print_inline_state(
        cmd_health_update_history_state(
            &capabilities, &status, status_ready));
    video_print(" remoto=", 0x08);
    cmd_health_print_inline_state(cmd_health_update_remote_state());
    video_print("\n", 0x07);
}

static void cmd_health_print_summary_app_store(void) {
    const recovery_component_t* component =
        recovery_get(RECOVERY_COMPONENT_APP_STORE);
    app_catalog_status_t status;
    int status_ready = app_catalog_get_status(&status) == OK;

    video_print("  App Store: ", 0x07);
    if (component) {
        cmd_health_print_inline_state(component->state);
        if (component->state != RECOVERY_STATE_READY) {
            video_print(" erro=", 0x08);
            shell_command_print_num((uint32_t)component->last_error);
            video_print(" falhas=", 0x08);
            shell_command_print_num(component->failures);
        }
    } else {
        video_print("UNKNOWN", 0x0C);
    }
    if (status_ready) {
        video_print("\n    fontes=", 0x07);
        shell_command_print_num(status.source_count);
        video_print(" validas=", 0x08);
        shell_command_print_num(status.valid_source_count);
        video_print(" invalidas=", 0x08);
        shell_command_print_num(status.invalid_source_count);
        video_print(" instaladas=", 0x08);
        shell_command_print_num(status.installed_count);
        video_print(" entradas=", 0x08);
        shell_command_print_num(status.entry_count);
    }
    video_print("\n", 0x07);
}

static void cmd_health_print_summary_kernel(void) {
    memory_heap_stats_t heap;

    memory_get_heap_stats(&heap);
    video_print("  Kernel: proc=", 0x07);
    shell_command_print_num(process_get_count());
    video_print(" READY=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print(" paging=", 0x08);
    video_print(paging_is_ready() ? "READY" : "DISABLED",
                paging_is_ready() ? 0x0A : 0x0C);
    video_print("\n  Memoria KB: usada=", 0x07);
    shell_command_print_num(memory_get_used() / 1024U);
    video_print(" livre=", 0x08);
    shell_command_print_num(memory_get_free() / 1024U);
    video_print(" heap=", 0x08);
    if (!heap.initialized) {
        video_print("DISABLED", 0x0C);
    } else {
        video_print(heap.valid ? "READY" : "DEGRADED",
                    heap.valid ? 0x0A : 0x0E);
    }
    video_print("\n", 0x07);
    cmd_health_print_usb_hid();
}

static void cmd_health_check_print_named_state(
    const char* name, const char* state, uint8_t color,
    const char* detail, int* issue_count) {
    if (!name || !state || !issue_count || kstrcmp(state, "READY") == 0) {
        return;
    }

    video_print("\n  ", SHELL_HEALTH_CHECK_TEXT_COLOR);
    video_print(name, SHELL_HEALTH_CHECK_LABEL_COLOR);
    video_print(": ", SHELL_HEALTH_CHECK_TEXT_COLOR);
    video_print(state, color);
    if (detail && detail[0] != '\0') {
        video_print(" motivo=", SHELL_HEALTH_CHECK_DETAIL_COLOR);
        video_print(detail, SHELL_HEALTH_CHECK_DETAIL_COLOR);
    }
    (*issue_count)++;
}

static void cmd_health_check_print_query_failure(
    const char* name, int result, int* issue_count) {
    if (!name || !issue_count) return;

    video_print("\n  ", SHELL_HEALTH_CHECK_TEXT_COLOR);
    video_print(name, SHELL_HEALTH_CHECK_LABEL_COLOR);
    video_print(": UNKNOWN erro=", SHELL_HEALTH_CHECK_ERROR_COLOR);
    shell_command_print_num((uint32_t)result);
    video_print(" motivo=consulta indisponivel",
                SHELL_HEALTH_CHECK_DETAIL_COLOR);
    (*issue_count)++;
}

static void cmd_health_check_print_component(
    const recovery_component_t* entry, int* issue_count) {
    if (!entry || !issue_count || entry->state == RECOVERY_STATE_READY) {
        return;
    }

    video_print("\n  ", SHELL_HEALTH_CHECK_TEXT_COLOR);
    video_print(entry->name, SHELL_HEALTH_CHECK_LABEL_COLOR);
    video_print(": ", SHELL_HEALTH_CHECK_TEXT_COLOR);
    video_print(recovery_state_name(entry->state),
                shell_diagnostics_health_state_color(entry->state));
    video_print(" erro=", SHELL_HEALTH_CHECK_DETAIL_COLOR);
    shell_command_print_num((uint32_t)entry->last_error);
    video_print(" falhas=", SHELL_HEALTH_CHECK_DETAIL_COLOR);
    shell_command_print_num(entry->failures);
    if (entry->last_message && entry->last_message[0] != '\0') {
        video_print(" motivo=", SHELL_HEALTH_CHECK_DETAIL_COLOR);
        video_print(entry->last_message, SHELL_HEALTH_CHECK_DETAIL_COLOR);
    }
    (*issue_count)++;
}

static void cmd_health_check_recovery(int* issue_count) {
    uint32_t index;

    if (!issue_count) return;
    for (index = 0; index < recovery_get_count(); index++) {
        const recovery_component_t* entry;

        if (index == RECOVERY_COMPONENT_UPDATE ||
            index == RECOVERY_COMPONENT_APP_STORE) {
            continue;
        }
        entry = recovery_get((recovery_component_id_t)index);
        if (!entry) {
            LOG_ERROR("SHELL", "Componente ausente no health check");
            cmd_health_check_print_named_state(
                "Recovery", "UNKNOWN", SHELL_HEALTH_CHECK_ERROR_COLOR,
                "consulta indisponivel", issue_count);
            continue;
        }
        cmd_health_check_print_component(entry, issue_count);
    }
}

static void cmd_health_check_update_capabilities(
    const update_capabilities_t* capabilities,
    const update_status_t* status, int status_ready, int* issue_count) {
    recovery_state_t state;
    const char* detail;

    if (!capabilities || !issue_count) return;

    state = cmd_health_update_local_state(capabilities);
    detail = !capabilities->verifier_ready ? "verificador indisponivel" :
             !capabilities->local_file_available ? "filesystem" : NULL;
    cmd_health_check_print_named_state(
        "Update/local", recovery_state_name(state),
        shell_diagnostics_health_state_color(state), detail, issue_count);

    state = cmd_health_update_apply_state(capabilities);
    detail = capabilities->apply_available ? NULL :
             capabilities->recovery_pending ? "recuperacao pendente" :
             (fs_get_type() == FS_TYPE_FAT12 &&
              !capabilities->persistent_state_ready) ?
             "estado persistente" : "requer FAT12";
    cmd_health_check_print_named_state(
        "Update/aplicacao", recovery_state_name(state),
        shell_diagnostics_health_state_color(state), detail, issue_count);

    state = capabilities->rollback_available ?
            RECOVERY_STATE_READY : RECOVERY_STATE_DISABLED;
    detail = capabilities->rollback_available ? NULL : "sem backup";
    cmd_health_check_print_named_state(
        "Update/rollback", recovery_state_name(state),
        shell_diagnostics_health_state_color(state), detail, issue_count);

    if (!status_ready) {
        cmd_health_check_print_named_state(
            "Update/historico", "UNKNOWN", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "estado do update indisponivel", issue_count);
    } else {
        state = cmd_health_update_history_state(
            capabilities, status, status_ready);
        detail = state == RECOVERY_STATE_DEGRADED ? "integridade" :
                 state == RECOVERY_STATE_DISABLED ? "requer FAT12" : NULL;
        cmd_health_check_print_named_state(
            "Update/historico", recovery_state_name(state),
            shell_diagnostics_health_state_color(state), detail, issue_count);
    }
}

static void cmd_health_check_update_remote(int* issue_count) {
    update_remote_status_t remote;
    recovery_state_t state;
    const char* detail;
    int result;

    if (!issue_count) return;
    result = update_remote_get_status(&remote);
    if (result != OK) {
        LOG_ERROR_CODE("SHELL", result,
                       "Falha ao consultar estado remoto no health check");
        cmd_health_check_print_query_failure(
            "Update/remoto", result, issue_count);
        return;
    }

    state = cmd_health_remote_state_from_status(&remote);
    if (state == RECOVERY_STATE_READY) return;
    detail = !remote.enabled ? "desabilitado" :
             update_remote_reason_name(remote.reason);
    cmd_health_check_print_named_state(
        "Update/remoto", recovery_state_name(state),
        shell_diagnostics_health_state_color(state), detail, issue_count);
}

static void cmd_health_check_update(int* issue_count) {
    const recovery_component_t* component;
    update_capabilities_t capabilities;
    update_status_t status;
    int capabilities_result;
    int status_result;
    int status_ready;

    if (!issue_count) return;
    component = recovery_get(RECOVERY_COMPONENT_UPDATE);
    if (!component) {
        LOG_ERROR("SHELL", "Componente Update ausente no health check");
        cmd_health_check_print_named_state(
            "Update", "UNKNOWN", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "consulta indisponivel", issue_count);
    } else {
        cmd_health_check_print_component(component, issue_count);
    }

    capabilities_result = update_get_capabilities(&capabilities);
    if (capabilities_result != OK) {
        LOG_ERROR_CODE("SHELL", capabilities_result,
                       "Falha ao consultar capacidades no health check");
        cmd_health_check_print_query_failure(
            "Update/capacidades", capabilities_result, issue_count);
    } else {
        kmemset(&status, 0, sizeof(status));
        status_result = update_get_status(&status);
        status_ready = status_result == OK;
        if (!status_ready) {
            LOG_ERROR_CODE("SHELL", status_result,
                           "Falha ao consultar status no health check");
        }
        cmd_health_check_update_capabilities(
            &capabilities, &status, status_ready, issue_count);
    }
    cmd_health_check_update_remote(issue_count);
}

static void cmd_health_check_app_store(int* issue_count) {
    const recovery_component_t* component;

    if (!issue_count) return;
    component = recovery_get(RECOVERY_COMPONENT_APP_STORE);
    if (!component) {
        LOG_ERROR("SHELL", "Componente App Store ausente no health check");
        cmd_health_check_print_named_state(
            "App Store", "UNKNOWN", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "consulta indisponivel", issue_count);
        return;
    }
    cmd_health_check_print_component(component, issue_count);
}

static void cmd_health_check_clock(int* issue_count) {
    clock_status_t status;
    int result;

    if (!issue_count) return;
    result = clock_get_status(&status);
    if (result != OK) {
        LOG_ERROR_CODE("SHELL", result,
                       "Falha ao consultar clock no health check");
        cmd_health_check_print_query_failure("UTC", result, issue_count);
        return;
    }
    if (!status.initialized) {
        cmd_health_check_print_named_state(
            "UTC", "DISABLED", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "clock nao inicializado", issue_count);
    } else if (!status.utc_available) {
        cmd_health_check_print_named_state(
            "UTC", "DEGRADED", SHELL_HEALTH_CHECK_WARN_COLOR,
            "monotono sem UTC", issue_count);
    }
}

static const char* cmd_health_check_tls_detail(
    const tls_status_t* status) {
    if (!status->trusted_time_available) return "tempo nao confiavel";
    if (!status->handshake_available) return "handshake indisponivel";
    if (!status->certificate_validation_available) {
        return "validacao X509 indisponivel";
    }
    if (!status->entropy_available) return "entropia indisponivel";
    if (!tls_capability_available()) return "HTTPS indisponivel";
    if (status->last_reason != TLS_REASON_NONE) {
        return tls_reason_name(status->last_reason);
    }
    return "estado TLS nao pronto";
}

static void cmd_health_check_tls(int* issue_count) {
    tls_status_t status;
    const char* detail;
    const char* state_name;
    uint8_t color;
    int result;

    if (!issue_count) return;
    result = tls_get_status(&status);
    if (result != OK) {
        LOG_ERROR_CODE("SHELL", result,
                       "Falha ao consultar TLS no health check");
        cmd_health_check_print_query_failure("TLS", result, issue_count);
        return;
    }
    if (status.state == TLS_STATE_READY &&
        status.trusted_time_available && status.handshake_available &&
        status.certificate_validation_available && status.entropy_available &&
        tls_capability_available()) {
        return;
    }
    detail = cmd_health_check_tls_detail(&status);
    state_name = status.state == TLS_STATE_READY ? "DEGRADED" :
                 tls_state_name(status.state);
    color = status.state == TLS_STATE_UNAVAILABLE ||
            status.state == TLS_STATE_UNINITIALIZED ?
            SHELL_HEALTH_CHECK_ERROR_COLOR : SHELL_HEALTH_CHECK_WARN_COLOR;
    cmd_health_check_print_named_state(
        "TLS", state_name, color, detail, issue_count);
}

static void cmd_health_check_kernel(int* issue_count) {
    memory_heap_stats_t heap;

    if (!issue_count) return;
    if (!paging_is_ready()) {
        cmd_health_check_print_named_state(
            "Kernel/paging", "DISABLED", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "paging indisponivel", issue_count);
    }
    memory_get_heap_stats(&heap);
    if (!heap.initialized) {
        cmd_health_check_print_named_state(
            "Kernel/heap", "DISABLED", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "heap nao inicializado", issue_count);
    } else if (!heap.valid) {
        cmd_health_check_print_named_state(
            "Kernel/heap", "DEGRADED", SHELL_HEALTH_CHECK_WARN_COLOR,
            "heap invalido", issue_count);
    }
}

static void cmd_health_check_usb_hid(int* issue_count) {
    usb_manager_status_t status;
    int result;

    if (!issue_count) return;
    result = usb_manager_get_status(&status);
    if (result != OK) {
        LOG_ERROR_CODE("SHELL", result,
                       "Falha ao consultar USB HID no health check");
        cmd_health_check_print_query_failure(
            "USB HID", result, issue_count);
        return;
    }
    if (!status.controller_count) {
        cmd_health_check_print_named_state(
            "USB HID", "DISABLED", SHELL_HEALTH_CHECK_ERROR_COLOR,
            "sem controlador", issue_count);
    } else if (!status.interrupt_transfer_available) {
        cmd_health_check_print_named_state(
            "USB HID", "DEGRADED", SHELL_HEALTH_CHECK_WARN_COLOR,
            "transferencia interrupt indisponivel", issue_count);
    }
}

static void cmd_health_check(void) {
    int issue_count = 0;

    video_begin_update();
    video_print("Health check:", SHELL_HEALTH_CHECK_LABEL_COLOR);
    cmd_health_check_recovery(&issue_count);
    cmd_health_check_update(&issue_count);
    cmd_health_check_app_store(&issue_count);
    cmd_health_check_clock(&issue_count);
    cmd_health_check_tls(&issue_count);
    cmd_health_check_kernel(&issue_count);
    cmd_health_check_usb_hid(&issue_count);
    if (!issue_count) {
        video_print(" OK\n", SHELL_HEALTH_CHECK_READY_COLOR);
    } else {
        video_print("\n", SHELL_HEALTH_CHECK_TEXT_COLOR);
    }
    video_end_update();
}

static void cmd_health_summary(void) {
    video_begin_update();
    video_print("Resumo do health:\n", 0x0B);
    cmd_health_print_summary_components();
    cmd_health_print_summary_update();
    cmd_health_print_summary_app_store();
    cmd_health_print_clock_tls();
    cmd_health_print_summary_kernel();
    video_end_update();
}

static void cmd_health_full(void) {
    video_begin_update();
    video_print("Estado dos componentes:\n", 0x0B);

    for (uint32_t i = 0; i < recovery_get_count(); i++) {
        cmd_health_print_component((recovery_component_id_t)i);
    }

    cmd_health_print_update_capabilities();
    cmd_health_print_clock_tls();
    cmd_health_print_kernel();
    video_end_update();
}

static void cmd_health(const char* args) {
    if (shell_command_args_equal(args, "")) {
        cmd_health_full();
        return;
    }
    if (shell_command_args_equal(args, "summary")) {
        cmd_health_summary();
        return;
    }
    if (shell_command_args_equal(args, "check")) {
        cmd_health_check();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido do comando health");
    video_print("Uso: health [summary|check]\n", 0x0E);
}

static const char* cmd_log_level_name(log_level_t level) {
    if (level == LOG_LEVEL_ERROR) return "error";
    if (level == LOG_LEVEL_WARN) return "warn";
    if (level == LOG_LEVEL_INFO) return "info";
    if (level == LOG_LEVEL_DEBUG) return "debug";
    return "invalid";
}

static int cmd_log_parse_level(const char* name, log_level_t* level) {
    if (!name || !level) return 0;
    if (kstrcmp(name, "error") == 0) *level = LOG_LEVEL_ERROR;
    else if (kstrcmp(name, "warn") == 0) *level = LOG_LEVEL_WARN;
    else if (kstrcmp(name, "info") == 0) *level = LOG_LEVEL_INFO;
    else if (kstrcmp(name, "debug") == 0) *level = LOG_LEVEL_DEBUG;
    else return 0;
    return 1;
}

static uint32_t cmd_log_parse_tail_count(const char* text) {
    uint32_t value = 0U;

    if (!text || !*text) return 0U;
    while (*text) {
        if (*text < '0' || *text > '9') return 0U;
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > SHELL_LOG_TAIL_MAXIMUM) return 0U;
        text++;
    }
    return value;
}

static void cmd_log_usage(void) {
    video_print("Uso: log [status|tail [1-16]|clear|level|check]\n", 0x0E);
    video_print("     log level <console|buffer> "
                "<error|warn|info|debug>\n", 0x0E);
}

static void cmd_log_invalid(void) {
    LOG_WARN_CODE("SHELL", ERR_INVALID, "Argumentos invalidos para log");
    cmd_log_usage();
}

static void cmd_log_print_levels(const log_stats_t* stats) {
    video_print("Niveis do log: console=", 0x0B);
    video_print(cmd_log_level_name(stats->console_level), 0x07);
    video_print(" buffer=", 0x0B);
    video_print(cmd_log_level_name(stats->buffer_level), 0x07);
    video_print("\n", 0x07);
}

static void cmd_log_status(void) {
    log_stats_t stats;

    if (log_get_stats(&stats) != OK) {
        video_print("Erro: estatisticas do log indisponiveis.\n", 0x0C);
        return;
    }
    video_print("Log circular: ", 0x0B);
    shell_command_print_num(stats.occupancy);
    video_print("/", 0x07);
    shell_command_print_num(stats.capacity);
    video_print(" registros\n", 0x07);
    cmd_log_print_levels(&stats);
    video_print("Proxima sequencia: ", 0x08);
    shell_command_print_num(stats.next_sequence);
    video_print("\nSobrescritas: ", 0x08);
    shell_command_print_num(stats.overwritten_records);
    video_print("  Agrupamentos: ", 0x08);
    shell_command_print_num(stats.grouped_events);
    video_print("\nTruncamentos: ", 0x08);
    shell_command_print_num(stats.truncated_events);
    video_print("  Descartes: ", 0x08);
    shell_command_print_num(stats.dropped_events);
    video_print("  Limpezas: ", 0x08);
    shell_command_print_num(stats.clear_count);
    video_print("\n", 0x07);
}

static void cmd_log_print_signed(int32_t value) {
    uint32_t magnitude;

    if (value < 0) {
        video_print("-", 0x07);
        magnitude = (uint32_t)(-(value + 1)) + 1U;
    } else {
        magnitude = (uint32_t)value;
    }
    shell_command_print_num(magnitude);
}

static void cmd_log_print_record(const log_record_t* record) {
    video_print("seq=", 0x08);
    shell_command_print_num(record->sequence);
    video_print(" ticks=", 0x08);
    shell_command_print_num(record->first_tick);
    video_print("..", 0x08);
    shell_command_print_num(record->last_tick);
    video_print(" [", 0x07);
    video_print(log_level_str(record->level), 0x0B);
    video_print("] [", 0x07);
    video_print(record->module, 0x0B);
    video_print("] ", 0x07);
    video_print(record->message, 0x07);
    video_print("\n  ocorrencias=", 0x08);
    shell_command_print_num(record->occurrences);
    video_print(" flags=0x", 0x08);
    shell_command_print_hex(record->flags, 2U);
    if (record->flags & LOG_RECORD_FLAG_HAS_ERROR_CODE) {
        video_print(" erro=", 0x08);
        cmd_log_print_signed(record->error_code);
    }
    video_print("\n", 0x07);
}

static void cmd_log_tail(const char* arguments) {
    char count_text[4];
    uint32_t requested = SHELL_LOG_TAIL_DEFAULT;
    uint32_t count = 0U;

    if (arguments && *arguments) {
        if (shell_command_read_single_arg(arguments, count_text,
                                  sizeof(count_text)) != OK) {
            cmd_log_invalid();
            return;
        }
        requested = cmd_log_parse_tail_count(count_text);
        if (requested == 0U) {
            cmd_log_invalid();
            return;
        }
    }
    if (log_copy_recent(shell_log_records, requested, &count) != OK) {
        video_print("Erro: historico do log indisponivel.\n", 0x0C);
        return;
    }
    if (count == 0U) {
        video_print("Log vazio.\n", 0x08);
        return;
    }
    for (uint32_t index = 0U; index < count; index++) {
        cmd_log_print_record(&shell_log_records[index]);
    }
}

static void cmd_log_level(const char* arguments) {
    char target[8];
    char level_name[8];
    log_level_t level;
    int result;

    if (!arguments || !*arguments) {
        log_stats_t stats;
        if (log_get_stats(&stats) == OK) cmd_log_print_levels(&stats);
        else video_print("Erro: niveis do log indisponiveis.\n", 0x0C);
        return;
    }
    if (shell_command_read_two_args(arguments, target, sizeof(target), level_name,
                            sizeof(level_name)) != OK ||
        !cmd_log_parse_level(level_name, &level)) {
        cmd_log_invalid();
        return;
    }
    if (kstrcmp(target, "console") == 0) {
        result = log_set_console_level(level);
    } else if (kstrcmp(target, "buffer") == 0) {
        result = log_set_buffer_level(level);
    } else {
        cmd_log_invalid();
        return;
    }
    if (result != OK) {
        LOG_WARN_CODE("SHELL", ERR_INVALID, "Combinacao de niveis invalida");
        video_print("Erro: o buffer deve ser tao detalhado quanto o console.\n",
                    0x0C);
        return;
    }
    video_print("Nivel ", 0x0A);
    video_print(target, 0x0A);
    video_print(" alterado para ", 0x0A);
    video_print(level_name, 0x0A);
    video_print(".\n", 0x0A);
}

static void cmd_log_print_test(const char* name, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_log_check(void) {
    log_self_test_result_t test;
    int result = log_self_test(&test);

    video_print("Autoteste do log (ring privado):\n", 0x0B);
    cmd_log_print_test("ordem e metadados", test.order_and_metadata);
    cmd_log_print_test("wrap e sobrescrita", test.wrap_and_overwrite);
    cmd_log_print_test("agrupamento", test.repetition_grouping);
    cmd_log_print_test("truncamento", test.safe_truncation);
    cmd_log_print_test("codigo de erro", test.optional_error_code);
    cmd_log_print_test("limpeza", test.clear_behavior);
    cmd_log_print_test("serializacao", test.text_serialization);
    cmd_log_print_test("filtragem", test.level_filtering);
    video_print("Resultado: ", 0x0B);
    video_print(result == OK ? "OK" : "ERRO", result == OK ? 0x0A : 0x0C);
    video_print(" (", 0x07);
    shell_command_print_num(test.passed);
    video_print(" aprovados, ", 0x07);
    shell_command_print_num(test.failed);
    video_print(" falhos)\n", 0x07);
}

static void cmd_log(const char* arguments) {
    const char* subarguments;

    if (shell_command_args_equal(arguments, "") ||
        shell_command_args_equal(arguments, "status")) {
        cmd_log_status();
        return;
    }
    subarguments = shell_command_match_subcommand(arguments, "tail");
    if (subarguments) {
        cmd_log_tail(subarguments);
        return;
    }
    if (shell_command_args_equal(arguments, "clear")) {
        log_clear_buffer();
        video_print("Log circular limpo.\n", 0x0A);
        return;
    }
    subarguments = shell_command_match_subcommand(arguments, "level");
    if (subarguments) {
        cmd_log_level(subarguments);
        return;
    }
    if (shell_command_args_equal(arguments, "check")) {
        cmd_log_check();
        return;
    }
    cmd_log_invalid();
}

static void cmd_timer_usage(void) {
    video_print("Uso: timer [status|list|check]\n", 0x0E);
}

static void cmd_timer_invalid(void) {
    LOG_WARN_CODE("SHELL", ERR_INVALID, "Argumentos invalidos para timer");
    cmd_timer_usage();
}

static void cmd_timer_status(void) {
    timer_stats_t stats;

    if (timer_get_stats(&stats) != OK) {
        video_print("Erro: estatisticas de timer indisponiveis.\n", 0x0C);
        return;
    }
    video_print("Servico de timers: tick=", 0x0B);
    shell_command_print_num(stats.current_tick);
    video_print(" frequencia=", 0x07);
    shell_command_print_num(stats.frequency);
    video_print(" Hz\nProprietarios: ", 0x07);
    shell_command_print_num(stats.owner_occupancy);
    video_print("/", 0x07);
    shell_command_print_num(stats.owner_capacity);
    video_print("  Timers: ", 0x07);
    shell_command_print_num(stats.occupancy);
    video_print("/", 0x07);
    shell_command_print_num(stats.capacity);
    video_print(" (armados=", 0x07);
    shell_command_print_num(stats.armed);
    video_print(" pendentes=", 0x07);
    shell_command_print_num(stats.pending);
    video_print(")\nMaior ocupacao: ", 0x07);
    shell_command_print_num(stats.high_watermark);
    video_print("  Criados: ", 0x07);
    shell_command_print_num(stats.timers_created);
    video_print("  Inicios: ", 0x07);
    shell_command_print_num(stats.timers_started);
    video_print("\nVencimentos: ", 0x07);
    shell_command_print_num(stats.expirations);
    video_print("  Cancelamentos: ", 0x07);
    shell_command_print_num(stats.cancellations);
    video_print("  Destruicoes: ", 0x07);
    shell_command_print_num(stats.timers_destroyed);
    video_print("\nCallbacks: ", 0x07);
    shell_command_print_num(stats.callbacks);
    video_print("  Erros: ", 0x07);
    shell_command_print_num(stats.callback_errors);
    video_print("  Atrasos: ", 0x07);
    shell_command_print_num(stats.delayed_callbacks);
    video_print("\nPeriodos perdidos: ", 0x07);
    shell_command_print_num(stats.missed_periods);
    video_print("  Operacoes invalidas: ", 0x07);
    shell_command_print_num(stats.invalid_operations);
    video_print("\n", 0x07);
}

static void cmd_timer_print_entry(const timer_info_t* info) {
    video_print("handle=0x", 0x08);
    shell_command_print_hex(info->handle, 8U);
    video_print(" owner=", 0x08);
    video_print(info->owner_name, 0x0B);
    video_print(" timer=", 0x08);
    video_print(info->name, 0x0B);
    video_print("\n  modo=", 0x08);
    video_print(timer_mode_name(info->mode), 0x07);
    video_print(" estado=", 0x08);
    video_print(timer_state_name(info->state), 0x07);
    video_print(" prazo=", 0x08);
    shell_command_print_num(info->deadline_tick);
    video_print(" periodo=", 0x08);
    shell_command_print_num(info->period_ticks);
    video_print("\n  execucoes=", 0x08);
    shell_command_print_num(info->executions);
    video_print(" atrasos=", 0x08);
    shell_command_print_num(info->delayed_callbacks);
    video_print(" perdidos=", 0x08);
    shell_command_print_num(info->missed_periods);
    video_print(" ultimo_atraso=", 0x08);
    shell_command_print_num(info->last_lateness_ticks);
    video_print(" erro=", 0x08);
    cmd_log_print_signed(info->last_error);
    video_print("\n", 0x07);
}

static void cmd_timer_list(void) {
    uint32_t count = 0U;

    if (timer_copy_active(shell_timer_records, TIMER_CAPACITY, &count) != OK) {
        video_print("Erro: lista de timers indisponivel.\n", 0x0C);
        return;
    }
    if (!count) {
        video_print("Nenhum timer criado.\n", 0x08);
        return;
    }
    video_print("Timers criados:\n", 0x0B);
    for (uint32_t index = 0U; index < count; index++) {
        cmd_timer_print_entry(&shell_timer_records[index]);
    }
}

static void cmd_timer_print_test(const char* name, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_timer_check(void) {
    timer_self_test_result_t test;
    int result = timer_self_test(&test);

    video_print("Autoteste de timers (tabelas privadas):\n", 0x0B);
    cmd_timer_print_test("conversao e limites", test.conversion_and_limits);
    cmd_timer_print_test("one-shot", test.one_shot);
    cmd_timer_print_test("periodico sem deriva", test.periodic_no_drift);
    cmd_timer_print_test("periodos agrupados", test.periodic_coalescing);
    cmd_timer_print_test("cancelamento armado", test.cancel_armed);
    cmd_timer_print_test("cancelamento pendente", test.cancel_pending);
    cmd_timer_print_test("destruicao do proprietario", test.owner_destruction);
    cmd_timer_print_test("handles antigos", test.stale_handles);
    cmd_timer_print_test("wrap de ticks", test.tick_wrap);
    cmd_timer_print_test("capacidade", test.capacity);
    cmd_timer_print_test("erro de callback", test.callback_errors);
    cmd_timer_print_test("invariantes", test.invariants);
    video_print("Resultado: ", 0x0B);
    video_print(result == OK ? "OK" : "ERRO", result == OK ? 0x0A : 0x0C);
    video_print(" (", 0x07);
    shell_command_print_num(test.passed);
    video_print(" aprovados, ", 0x07);
    shell_command_print_num(test.failed);
    video_print(" falhos)\n", 0x07);
}

static void cmd_timer(const char* arguments) {
    if (shell_command_args_equal(arguments, "") ||
        shell_command_args_equal(arguments, "status")) {
        cmd_timer_status();
        return;
    }
    if (shell_command_args_equal(arguments, "list")) {
        cmd_timer_list();
        return;
    }
    if (shell_command_args_equal(arguments, "check")) {
        cmd_timer_check();
        return;
    }
    cmd_timer_invalid();
}

static void cmd_diagnostics_print_u64(uint64_t value) {
    static const uint64_t powers_of_ten[20] = {
        10000000000000000000ULL, 1000000000000000000ULL,
        100000000000000000ULL, 10000000000000000ULL,
        1000000000000000ULL, 100000000000000ULL,
        10000000000000ULL, 1000000000000ULL,
        100000000000ULL, 10000000000ULL,
        1000000000ULL, 100000000ULL,
        10000000ULL, 1000000ULL,
        100000ULL, 10000ULL,
        1000ULL, 100ULL, 10ULL, 1ULL
    };
    uint8_t started = 0U;

    for (uint32_t index = 0U; index < 20U; index++) {
        uint8_t digit = 0U;

        while (value >= powers_of_ten[index]) {
            value -= powers_of_ten[index];
            digit++;
        }
        if (digit || started || index == 19U) {
            char digit_text[2];
            digit_text[0] = (char)('0' + digit);
            digit_text[1] = '\0';
            video_print(digit_text, 0x07);
            started = 1U;
        }
    }
}

static void cmd_diagnostics_print_test(const char* name, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_clock_usage(void) {
    video_print("Uso: clock [status|check]\n", 0x0E);
}

static void cmd_clock_status(void) {
    clock_status_t status;
    rtc_status_t rtc;
    uint64_t ticks;
    uint64_t utc;

    if (clock_get_status(&status) != OK) {
        video_print("Erro: estado do clock indisponivel.\n", 0x0C);
        return;
    }
    video_print("Clock: inicializado=", 0x0B);
    video_print(status.initialized ? "SIM" : "NAO", 0x07);
    video_print(" fonte=", 0x07);
    video_print(clock_source_name(status.source), 0x07);
    video_print(" PIT=", 0x07);
    shell_command_print_num(status.frequency);
    video_print(" Hz UTC=", 0x07);
    video_print(status.utc_available ? "READY" : "UNAVAILABLE",
                status.utc_available ? 0x0A : 0x0E);
    video_print(" wraps=", 0x07);
    cmd_diagnostics_print_u64(status.monotonic_wraps);
    video_print(" reads=", 0x07);
    shell_command_print_num(status.reads);
    video_print(" erro=", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
    video_print("  anchor_tick=", 0x07);
    cmd_diagnostics_print_u64(status.anchor_monotonic_ticks);
    video_print(" anchor_utc=", 0x07);
    cmd_diagnostics_print_u64(status.anchor_unix_seconds);
    video_print("\n", 0x07);
    if (clock_get_monotonic_ticks(&ticks) == OK) {
        video_print("  monotono_ticks=", 0x07);
        cmd_diagnostics_print_u64(ticks);
        video_print("\n", 0x07);
    }
    if (clock_get_utc(&utc) == OK) {
        video_print("  utc_unix_seconds=", 0x07);
        cmd_diagnostics_print_u64(utc);
        video_print("\n", 0x07);
    }
    if (rtc_get_status(&rtc) == OK) {
        video_print("  RTC=", 0x07);
        video_print(rtc.valid ? "READY" : "UNAVAILABLE",
                    rtc.valid ? 0x0A : 0x0E);
        if (rtc.valid) {
            video_print(" ", 0x07);
            shell_command_print_num(rtc.utc.year);
            video_print("-", 0x07);
            shell_command_print_num(rtc.utc.month);
            video_print("-", 0x07);
            shell_command_print_num(rtc.utc.day);
            video_print(" ", 0x07);
            shell_command_print_num(rtc.utc.hour);
            video_print(":", 0x07);
            shell_command_print_num(rtc.utc.minute);
            video_print(":", 0x07);
            shell_command_print_num(rtc.utc.second);
        }
        video_print("\n", 0x07);
    }
}

static void cmd_clock_check(void) {
    rtc_self_test_result_t rtc_test;
    clock_self_test_result_t clock_test;
    int rtc_result = rtc_self_test(&rtc_test);
    int clock_result = clock_self_test(&clock_test);

    video_print("Autoteste RTC/clock:\n", 0x0B);
    cmd_diagnostics_print_test("RTC BCD", rtc_test.bcd_conversion);
    cmd_diagnostics_print_test("RTC binario", rtc_test.binary_conversion);
    cmd_diagnostics_print_test("RTC 12/24 horas",
                               rtc_test.twelve_hour_conversion);
    cmd_diagnostics_print_test("RTC calendario",
                               rtc_test.calendar_validation);
    cmd_diagnostics_print_test("RTC datas invalidas",
                               rtc_test.invalid_dates_rejected);
    cmd_diagnostics_print_test("epoch Unix", clock_test.epoch_conversion);
    cmd_diagnostics_print_test("ano bissexto", clock_test.leap_year_conversion);
    cmd_diagnostics_print_test("data invalida",
                               clock_test.invalid_date_rejected);
    cmd_diagnostics_print_test("rollover monotono",
                               clock_test.monotonic_rollover);
    cmd_diagnostics_print_test("invariantes", clock_test.invariants);
    video_print("Resultado: ", 0x0B);
    video_print(rtc_result == OK && clock_result == OK ? "OK\n" : "ERRO\n",
                rtc_result == OK && clock_result == OK ? 0x0A : 0x0C);
}

static void cmd_clock(const char* arguments) {
    if (shell_command_args_equal(arguments, "") ||
        shell_command_args_equal(arguments, "status")) {
        cmd_clock_status();
        return;
    }
    if (shell_command_args_equal(arguments, "check")) {
        cmd_clock_check();
        return;
    }
    LOG_WARN_CODE("SHELL", ERR_INVALID, "Argumentos invalidos para clock");
    cmd_clock_usage();
}

static void cmd_tls_usage(void) {
    video_print("Uso: tls [status|check]\n", 0x0E);
}

static void cmd_tls_status(void) {
    tls_policy_t policy;
    tls_status_t status;

    if (tls_get_policy(&policy) != OK || tls_get_status(&status) != OK) {
        video_print("Erro: politica TLS indisponivel.\n", 0x0C);
        return;
    }
    video_print("TLS: estado=", 0x0B);
    video_print(tls_state_name(status.state),
                status.state == TLS_STATE_READY ? 0x0A : 0x0E);
    video_print(" tempo=", 0x07);
    video_print(status.trusted_time_available ? "TRUSTED" : "UNAVAILABLE",
                status.trusted_time_available ? 0x0A : 0x0E);
    video_print(" capability=", 0x07);
    video_print(tls_capability_available() ? "HTTPS_READY" :
                "HTTPS_UNAVAILABLE", tls_capability_available() ? 0x0A : 0x0E);
    video_print(" entropy=", 0x07);
    video_print(status.entropy_available ? "RDRAND" : "UNAVAILABLE",
                status.entropy_available ? 0x0A : 0x0E);
    video_print(" x509=", 0x07);
    video_print(status.certificate_validation_available ? "READY" :
                "UNAVAILABLE",
                status.certificate_validation_available ? 0x0A : 0x0E);
    video_print("\n  min_version=0x", 0x07);
    shell_command_print_hex(policy.minimum_version, 4U);
    video_print(" CA=", 0x07);
    video_print(policy.require_static_ca ? "REQUIRED" : "OPTIONAL", 0x07);
    video_print(" SAN=", 0x07);
    video_print(policy.require_hostname_san ? "REQUIRED" : "OPTIONAL", 0x07);
    video_print(" validity=", 0x07);
    video_print(policy.require_validity_window ? "REQUIRED" : "OPTIONAL", 0x07);
    video_print(" pin=", 0x07);
    video_print(policy.allow_spki_pin ? "OPTIONAL" : "FORBIDDEN", 0x07);
    video_print(" trust=current/next=", 0x07);
    shell_command_print_num(policy.trust_current_version);
    video_print("/", 0x07);
    shell_command_print_num(policy.trust_next_version);
    video_print(" revoked<", 0x07);
    shell_command_print_num(policy.trust_revocation_version);
    video_print(" fallback_http=", 0x07);
    video_print(policy.allow_http_fallback ? "ALLOWED" : "FORBIDDEN", 0x07);
    video_print("\n  handshake=", 0x07);
    video_print(status.handshake_available ? "AVAILABLE" : "DISABLED", 0x07);
    video_print(" x509=", 0x07);
    video_print(status.x509_available ? "AVAILABLE" : "DEFERRED", 0x07);
    video_print(" checks=", 0x07);
    shell_command_print_num(status.policy_checks);
    video_print(" rejects=", 0x07);
    shell_command_print_num(status.policy_rejections);
    video_print(" last_reason=", 0x07);
    video_print(tls_reason_name(status.last_reason), 0x07);
    video_print("\n", 0x07);
}

static void cmd_tls_check(void) {
    tls_self_test_result_t test;
    int result = tls_self_test(&test);

    video_print("Autoteste da politica TLS:\n", 0x0B);
    cmd_diagnostics_print_test("identidade valida", test.valid_identity);
    cmd_diagnostics_print_test("tempo indisponivel", test.time_unavailable);
    cmd_diagnostics_print_test("certificado futuro", test.certificate_future);
    cmd_diagnostics_print_test("certificado expirado", test.certificate_expired);
    cmd_diagnostics_print_test("cadeia nao confiavel", test.untrusted_chain);
    cmd_diagnostics_print_test("SAN divergente", test.san_mismatch);
    cmd_diagnostics_print_test("pin ausente", test.pin_absent);
    cmd_diagnostics_print_test("pin correto", test.pin_match);
    cmd_diagnostics_print_test("pin divergente", test.pin_mismatch);
    cmd_diagnostics_print_test("rotacao atual/proxima",
                               test.trust_rotation);
    cmd_diagnostics_print_test("revogacao de confianca",
                               test.trust_revocation);
    cmd_diagnostics_print_test("invariantes", test.invariants);
    video_print("Resultado: ", 0x0B);
    video_print(result == OK ? "OK\n" : "ERRO\n", result == OK ? 0x0A : 0x0C);
}

static void cmd_tls(const char* arguments) {
    if (shell_command_args_equal(arguments, "") ||
        shell_command_args_equal(arguments, "status")) {
        cmd_tls_status();
        return;
    }
    if (shell_command_args_equal(arguments, "check")) {
        cmd_tls_check();
        return;
    }
    LOG_WARN_CODE("SHELL", ERR_INVALID, "Argumentos invalidos para tls");
    cmd_tls_usage();
}

static void cmd_wait_usage(void) {
    video_print("Uso: wait [status|list|check]\n", 0x0E);
}

static void cmd_wait_invalid(void) {
    LOG_WARN_CODE("SHELL", ERR_INVALID, "Argumentos invalidos para wait");
    cmd_wait_usage();
}

static void cmd_wait_status(void) {
    wait_stats_t stats;

    if (wait_get_stats(&stats) != OK) {
        video_print("Erro: estatisticas de espera indisponiveis.\n", 0x0C);
        return;
    }
    video_print("Servico de espera: canais=", 0x0B);
    shell_command_print_num(stats.channels_active);
    video_print(" waiters=", 0x07);
    shell_command_print_num(stats.active_waiters);
    video_print(" pico=", 0x07);
    shell_command_print_num(stats.peak_waiters);
    video_print("\nInicios=", 0x07);
    shell_command_print_num(stats.waits_started);
    video_print(" eventos=", 0x07);
    shell_command_print_num(stats.event_wakes);
    video_print(" timeouts=", 0x07);
    shell_command_print_num(stats.timeout_wakes);
    video_print(" cancelamentos=", 0x07);
    shell_command_print_num(stats.cancellation_wakes);
    video_print(" indisponiveis=", 0x07);
    shell_command_print_num(stats.unavailable_wakes);
    video_print("\nOperacoes invalidas=", 0x07);
    shell_command_print_num(stats.invalid_operations);
    video_print("\n", 0x07);
}

static void cmd_wait_print_info(const wait_info_t* info) {
    video_print(info->target == WAIT_TARGET_PROCESS ? "processo=" : "thread=",
                0x08);
    shell_command_print_num(info->id);
    video_print(" nome=", 0x08);
    video_print(info->name, 0x07);
    video_print(" canal=", 0x08);
    video_print(info->channel_owner, 0x0B);
    video_print(" motivo=", 0x08);
    video_print(wait_reason_name(info->reason), 0x07);
    video_print(" restante=", 0x08);
    if (!info->deadline_active) {
        video_print("infinito", 0x07);
    } else {
        shell_command_print_num(info->remaining_ticks);
    }
    video_print("\n", 0x07);
}

static void cmd_wait_list(void) {
    uint32_t process_count = 0U;
    uint32_t thread_count = 0U;
    uint32_t total;

    if (process_copy_waiters(shell_wait_records, MAX_PROCESSES,
                             &process_count) != OK ||
        thread_copy_waiters(shell_wait_records + process_count, MAX_THREADS,
                            &thread_count) != OK) {
        video_print("Erro: lista de esperas indisponivel.\n", 0x0C);
        return;
    }
    total = process_count + thread_count;
    if (!total) {
        video_print("Nenhuma tarefa bloqueada por canal.\n", 0x08);
        return;
    }
    video_print("Tarefas bloqueadas por canal:\n", 0x0B);
    for (uint32_t index = 0U; index < total; index++) {
        cmd_wait_print_info(&shell_wait_records[index]);
    }
}

static void cmd_wait_print_test(const char* name, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_wait_check(void) {
    wait_self_test_result_t test;
    int result = wait_self_test(&test);

    video_print("Autoteste de esperas (canal privado):\n", 0x0B);
    cmd_wait_print_test("ciclo do canal", test.channel_lifecycle);
    cmd_wait_print_test("sinal de condicao", test.condition_signal);
    cmd_wait_print_test("disponibilidade", test.availability);
    cmd_wait_print_test("evento/timeout/cancelamento", test.accounting);
    cmd_wait_print_test("motivos/recurso ausente", test.reasons);
    cmd_wait_print_test("limites", test.limits);
    cmd_wait_print_test("reset seguro", test.reset);
    cmd_wait_print_test("invariantes", test.invariants);
    video_print("Resultado: ", 0x0B);
    video_print(result == OK ? "OK" : "ERRO", result == OK ? 0x0A : 0x0C);
    video_print(" (", 0x07);
    shell_command_print_num(test.passed);
    video_print(" aprovados, ", 0x07);
    shell_command_print_num(test.failed);
    video_print(" falhos)\n", 0x07);
}

static void cmd_wait(const char* arguments) {
    if (shell_command_args_equal(arguments, "") ||
        shell_command_args_equal(arguments, "status")) {
        cmd_wait_status();
        return;
    }
    if (shell_command_args_equal(arguments, "list")) {
        cmd_wait_list();
        return;
    }
    if (shell_command_args_equal(arguments, "check")) {
        cmd_wait_check();
        return;
    }
    cmd_wait_invalid();
}

static uint8_t cmd_device_status_color(device_status_t status) {
    if (status == DEVICE_STATUS_READY) return 0x0A;
    if (status == DEVICE_STATUS_DEGRADED) return 0x0E;
    if (status == DEVICE_STATUS_DISABLED) return 0x0C;
    return 0x08;
}

static int cmd_devices_print_entry(const device_info_t* info, int verbose) {
    device_text_t text;
    int result;

    if (!info) {
        LOG_ERROR("SHELL", "Entrada nula ao listar dispositivos");
        return ERR_NULL;
    }
    result = device_manager_format_text(info, &text);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao formatar entrada do inventario");
        return result;
    }

    video_print("  ", 0x07);
    video_print(text.id, 0x0B);
    video_print("  ", 0x07);
    video_print(device_manager_status_name(info->status),
                cmd_device_status_color(info->status));
    video_print("  ", 0x07);
    video_print(text.type, 0x07);
    video_print("  ", 0x07);
    video_print(text.name, 0x07);
    video_print("\n", 0x07);
    if (!verbose) return OK;

    video_print("    local=", 0x08);
    video_print(text.location, 0x07);
    if (info->irq != DEVICE_IRQ_UNKNOWN) {
        video_print(" irq=", 0x08);
        shell_command_print_num(info->irq);
    }
    if (info->vendor_id || info->device_id) {
        video_print(" vendor=0x", 0x08);
        shell_command_print_hex(info->vendor_id, 4U);
        video_print(" device=0x", 0x08);
        shell_command_print_hex(info->device_id, 4U);
    }
    video_print("\n    ", 0x08);
    video_print(text.detail, 0x07);
    video_print("\n", 0x07);
    return OK;
}

static void cmd_devices(const char* args) {
    uint32_t count = 0;
    int verbose = 0;

    if (*args) {
        if (!shell_command_args_equal(args, "-v")) {
            LOG_WARN("SHELL", "Uso invalido de devices");
            video_print("Uso: devices [-v]\n", 0x0C);
            return;
        }
        verbose = 1;
    }
    if (device_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Inventario de dispositivos indisponivel");
        video_print("Erro: inventario de dispositivos indisponivel.\n", 0x0C);
        return;
    }

    video_print("Dispositivos detectados:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        device_info_t info;
        if (device_manager_get_info(index, &info) != OK) {
            LOG_ERROR("SHELL", "Falha ao ler entrada do inventario");
            video_print("Erro: entrada do inventario indisponivel.\n", 0x0C);
            return;
        }
        if (cmd_devices_print_entry(&info, verbose) != OK) {
            video_print("Erro: entrada do inventario indisponivel.\n", 0x0C);
            return;
        }
    }
}

static void cmd_device_info(const char* args) {
    char id[DEVICE_ID_SIZE];
    device_info_t info;
    device_text_t text;
    int result = shell_command_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de device-info");
        video_print("Uso: device-info <id>\n", 0x0C);
        return;
    }
    result = device_manager_find(id, &info);
    if (result == ERR_NOT_FOUND) {
        video_print("Erro: dispositivo nao encontrado.\n", 0x0C);
        return;
    }
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao consultar dispositivo");
        video_print("Erro: inventario de dispositivos indisponivel.\n", 0x0C);
        return;
    }
    result = device_manager_format_text(&info, &text);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao formatar dispositivo solicitado");
        video_print("Erro: dispositivo indisponivel.\n", 0x0C);
        return;
    }

    video_print("Dispositivo:\n", 0x0B);
    video_print("  ID: ", 0x07);
    video_print(text.id, 0x0B);
    video_print("\n  Nome: ", 0x07);
    video_print(text.name, 0x07);
    video_print("\n  Tipo: ", 0x07);
    video_print(text.type, 0x07);
    video_print("\n  Estado: ", 0x07);
    video_print(device_manager_status_name(info.status),
                cmd_device_status_color(info.status));
    video_print("\n  Local: ", 0x07);
    video_print(text.location, 0x07);
    video_print("\n  Detalhe: ", 0x07);
    video_print(text.detail, 0x07);
    if (info.irq != DEVICE_IRQ_UNKNOWN) {
        video_print("\n  IRQ: ", 0x07);
        shell_command_print_num(info.irq);
    }
    if (info.vendor_id || info.device_id) {
        video_print("\n  Vendor: 0x", 0x07);
        shell_command_print_hex(info.vendor_id, 4U);
        video_print("  Device: 0x", 0x07);
        shell_command_print_hex(info.device_id, 4U);
        video_print("\n  PCI: ", 0x07);
        shell_command_print_hex(info.bus, 2U);
        video_print(":", 0x07);
        shell_command_print_hex(info.device, 2U);
        video_print(".", 0x07);
        shell_command_print_num(info.function);
    }
    if (info.capacity_sectors && info.kind == DEVICE_KIND_ATA_PRIMARY) {
        video_print("\n  Setores: ", 0x07);
        shell_command_print_num(info.capacity_sectors);
    }
    video_print("\n", 0x07);
}

static uint8_t cmd_usb_recovery_color(recovery_state_t state) {
    if (state == RECOVERY_STATE_READY) return 0x0A;
    if (state == RECOVERY_STATE_DEGRADED) return 0x0E;
    if (state == RECOVERY_STATE_DISABLED) return 0x0C;
    return 0x08;
}

static uint8_t cmd_usb_state_color(usb_controller_state_t state) {
    if (state == USB_CONTROLLER_READY) return 0x0A;
    if (state == USB_CONTROLLER_DEGRADED) return 0x0E;
    if (state == USB_CONTROLLER_DISABLED) return 0x0C;
    return 0x08;
}

static uint8_t cmd_usb_port_color(usb_port_state_t state) {
    if (state == USB_PORT_CONFIGURED) return 0x0A;
    if (state == USB_PORT_DEGRADED) return 0x0E;
    if (state == USB_PORT_EMPTY) return 0x08;
    return 0x0B;
}

static void cmd_usb_status(void) {
    usb_manager_status_t status;
    const recovery_component_t* health;

    if (usb_manager_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado USB indisponivel");
        video_print("Erro: inventario USB indisponivel.\n", 0x0C);
        return;
    }
    health = recovery_get(RECOVERY_COMPONENT_USB);
    if (!health) {
        LOG_ERROR("SHELL", "Componente USB ausente do health");
        video_print("Erro: health USB indisponivel.\n", 0x0C);
        return;
    }
    video_print("USB:\n  Servico: ", 0x0B);
    video_print(recovery_state_name(health->state),
                cmd_usb_recovery_color(health->state));
    video_print("\n  Inventario: ", 0x07);
    video_print(status.partial ? "PARCIAL" : "COMPLETO",
                status.partial ? 0x0E : 0x0A);
    video_print("\n  Controladores: ", 0x07);
    shell_command_print_num(status.controller_count);
    video_print("\n  UHCI: ", 0x07);
    shell_command_print_num(status.uhci_count);
    video_print("  EHCI: ", 0x07);
    shell_command_print_num(status.ehci_count);
    video_print("  Outros: ", 0x07);
    shell_command_print_num(status.other_count);
    video_print("\n  DMA: ", 0x07);
    video_print(status.dma_initialized ? "READY" : "INDISPONIVEL",
                status.dma_initialized ? 0x0A : 0x0E);
    video_print("  IRQ: ", 0x07);
    video_print(status.irq_initialized ? "READY" : "INDISPONIVEL",
                status.irq_initialized ? 0x0A : 0x0E);
    video_print("\n  Controle: ", 0x07);
    video_print(status.transfer_available ? "READY" : "INDISPONIVEL",
                status.transfer_available ? 0x0A : 0x0E);
    video_print("  Bulk: ", 0x07);
    video_print(status.bulk_transfer_available ? "READY" : "INDISPONIVEL",
                status.bulk_transfer_available ? 0x0A : 0x0E);
    video_print("  Interrupt: ", 0x07);
    video_print(status.interrupt_transfer_available ? "READY" :
                "INDISPONIVEL", status.interrupt_transfer_available ?
                0x0A : 0x0E);
    video_print("\n  Portas: ", 0x07);
    shell_command_print_num(status.port_count);
    video_print("  Dispositivos: ", 0x07);
    shell_command_print_num(status.configured_device_count);
    video_print("  TDs: ", 0x07);
    shell_command_print_num(status.dma_td_in_use);
    video_print("/", 0x07);
    shell_command_print_num(status.dma_td_capacity);
    video_print("\n  MSC ativos: ", 0x07);
    shell_command_print_num(status.msc_device_count);
    video_print("  HID: ", 0x07);
    shell_command_print_num(status.hid_active_count);
    video_print(" ativos / ", 0x08);
    shell_command_print_num(status.hid_device_count);
    video_print(" registrados", 0x08);
    video_print("  Hubs: INDISPONIVEL  Hotplug: INDISPONIVEL", 0x08);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static int cmd_usb_print_entry(const usb_controller_info_t* info) {
    usb_controller_text_t text;
    int result;

    if (!info) {
        LOG_ERROR("SHELL", "Entrada nula ao listar USB");
        return ERR_NULL;
    }
    result = usb_manager_format_text(info, &text);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao formatar entrada USB");
        return result;
    }
    video_print("  ", 0x07);
    video_print(text.id, 0x0B);
    video_print("  ", 0x07);
    video_print(usb_manager_state_name(info->state),
                cmd_usb_state_color(info->state));
    video_print("  ", 0x07);
    video_print(usb_manager_model_name(info->model), 0x07);
    video_print("  ", 0x07);
    video_print(text.location, 0x07);
    video_print("\n    ", 0x08);
    video_print(text.detail, 0x07);
    video_print("\n", 0x07);
    return OK;
}

static void cmd_usb_list(const char* args) {
    uint32_t count = 0;

    if (!shell_command_args_equal(args, "")) {
        LOG_WARN("SHELL", "Uso invalido de usb list");
        video_print("Uso: usb list\n", 0x0C);
        return;
    }
    if (usb_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Contagem USB indisponivel");
        video_print("Erro: inventario USB indisponivel.\n", 0x0C);
        return;
    }
    video_print("Controladores USB detectados:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        usb_controller_info_t info;

        if (usb_manager_get_info(index, &info) != OK ||
            cmd_usb_print_entry(&info) != OK) {
            video_print("Erro: entrada USB indisponivel.\n", 0x0C);
            return;
        }
    }
}

static void cmd_usb_ports(const char* args) {
    uint32_t count = 0U;

    if (!shell_command_args_equal(args, "")) {
        LOG_WARN("SHELL", "Uso invalido de usb ports");
        video_print("Uso: usb ports\n", 0x0C);
        return;
    }
    if (usb_manager_get_port_count(&count) != OK) {
        LOG_ERROR("SHELL", "Contagem de portas USB indisponivel");
        video_print("Erro: portas USB indisponiveis.\n", 0x0C);
        return;
    }
    video_print("Portas USB UHCI:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        usb_port_info_t port;

        if (usb_manager_get_port(index, &port) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar porta USB");
            video_print("Erro: porta USB indisponivel.\n", 0x0C);
            return;
        }
        video_print("  ", 0x07);
        video_print(port.controller_id, 0x0B);
        video_print(" p", 0x07);
        shell_command_print_num(port.port_number);
        video_print("  ", 0x07);
        video_print(usb_manager_port_state_name(port.state),
                    cmd_usb_port_color(port.state));
        video_print("  ", 0x07);
        video_print(usb_manager_port_reason_name(port.reason), 0x08);
        video_print("  ", 0x07);
        video_print(usb_manager_speed_name(port.speed), 0x07);
        video_print(" addr=", 0x07);
        shell_command_print_num(port.usb_address);
        if (port.device_id[0]) {
            video_print("  ", 0x07);
            video_print(port.device_id, 0x0A);
        }
        video_print("\n", 0x07);
    }
}

static void cmd_usb_devices(const char* args) {
    uint32_t count = 0U;

    if (!shell_command_args_equal(args, "")) {
        LOG_WARN("SHELL", "Uso invalido de usb devices");
        video_print("Uso: usb devices\n", 0x0C);
        return;
    }
    if (usb_manager_get_device_count(&count) != OK) {
        LOG_ERROR("SHELL", "Contagem de dispositivos USB indisponivel");
        video_print("Erro: dispositivos USB indisponiveis.\n", 0x0C);
        return;
    }
    video_print("Dispositivos USB configurados:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        usb_device_info_t device;

        if (usb_manager_get_device(index, &device) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar dispositivo USB");
            video_print("Erro: dispositivo USB indisponivel.\n", 0x0C);
            return;
        }
        video_print("  ID: ", 0x07);
        video_print(device.id, 0x0A);
        video_print("  ", 0x07);
        video_print(usb_manager_speed_name(device.speed), 0x07);
        video_print(" addr=", 0x07);
        shell_command_print_num(device.usb_address);
        video_print("  PCI: ", 0x07);
        video_print(device.controller_id, 0x0B);
        video_print(" p", 0x07);
        shell_command_print_num(device.port_number);
        video_print("\n    Vendor: 0x", 0x07);
        shell_command_print_hex(device.vendor_id, 4U);
        video_print(" Product: 0x", 0x07);
        shell_command_print_hex(device.product_id, 4U);
        video_print(" Class: 0x", 0x07);
        shell_command_print_hex(device.device_class, 2U);
        video_print(" Subclass: 0x", 0x07);
        shell_command_print_hex(device.device_subclass, 2U);
        video_print(" Protocol: 0x", 0x07);
        shell_command_print_hex(device.device_protocol, 2U);
        video_print("\n    Config: ", 0x07);
        shell_command_print_num(device.configuration_value);
        video_print(" Interface: ", 0x07);
        shell_command_print_num(device.interface_number);
        video_print(" Endpoints: ", 0x07);
        shell_command_print_num(device.endpoint_count);
        video_print("  DeviceDesc: ", 0x07);
        video_print(device.device_descriptor_valid ? "OK" : "INVALID", 0x0A);
        video_print("  ConfigDesc: ", 0x07);
        video_print(device.configuration_descriptor_valid ? "OK" : "INVALID",
                    0x0A);
        video_print("\n    Classe: ", 0x08);
        if (device.class_driver_active && device.hid_driver_active) {
            video_print("MSC e HID ativos", 0x0A);
        } else if (device.class_driver_active) {
            video_print("MSC ativo", 0x0A);
        } else if (device.hid_driver_active) {
            video_print("HID ativo", 0x0A);
        } else {
            video_print("sem driver", 0x08);
        }
        if (device.interrupt_in_endpoint) {
            video_print("  Interrupt IN 0x", 0x07);
            shell_command_print_hex(device.interrupt_in_endpoint, 2U);
            video_print("/", 0x07);
            shell_command_print_num(device.interrupt_in_max_packet);
        }
        video_print("  Hub: nao  Estado: ", 0x08);
        video_print(device.state == USB_DEVICE_CONFIGURED ? "CONFIGURED" :
                    "DEGRADED", device.state == USB_DEVICE_CONFIGURED ?
                    0x0A : 0x0E);
        video_print("\n", 0x07);
    }
}

static void cmd_usb_storage(const char* args) {
    uint32_t count = 0U;

    if (!shell_command_args_equal(args, "")) {
        LOG_WARN("SHELL", "Uso invalido de usb storage");
        video_print("Uso: usb storage\n", 0x0C);
        return;
    }
    if (usb_msc_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Contagem MSC indisponivel");
        video_print("Erro: inventario USB MSC indisponivel.\n", 0x0C);
        return;
    }
    video_print("USB Mass Storage: ", 0x0B);
    shell_command_print_num(count);
    video_print(" dispositivo(s)\n", 0x07);
    for (uint32_t index = 0U; index < count; index++) {
        usb_msc_info_t info;

        if (usb_msc_get_at(index, &info) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar entrada MSC");
            video_print("Erro: entrada MSC indisponivel.\n", 0x0C);
            return;
        }
        video_print("  ID: ", 0x07);
        video_print(info.id, 0x0A);
        video_print("  Estado: ", 0x07);
        video_print(usb_msc_state_name(info.state),
                    info.state == USB_MSC_READY ? 0x0A : 0x0E);
        video_print("\n    Bloco: ", 0x07);
        video_print(info.block_id, 0x0B);
        video_print("  LUN: ", 0x07);
        shell_command_print_num(info.lun);
        video_print("  Interface: ", 0x07);
        shell_command_print_num(info.interface_number);
        video_print("  Setor: ", 0x07);
        shell_command_print_num(info.sector_size);
        video_print("  Capacidade: ", 0x07);
        shell_command_print_num(info.sector_count);
        video_print("\n    Vendor: ", 0x07);
        video_print(info.vendor[0] ? info.vendor : "(desconhecido)", 0x07);
        video_print("  Produto: ", 0x07);
        video_print(info.product[0] ? info.product : "(desconhecido)", 0x07);
        video_print("  Revisao: ", 0x07);
        video_print(info.revision[0] ? info.revision : "(desconhecida)", 0x07);
        video_print("\n    Bulk IN 0x", 0x07);
        shell_command_print_hex(info.bulk_in_endpoint, 2U);
        video_print("/", 0x07);
        shell_command_print_num(info.bulk_in_max_packet);
        video_print("  Bulk OUT 0x", 0x07);
        shell_command_print_hex(info.bulk_out_endpoint, 2U);
        video_print("/", 0x07);
        shell_command_print_num(info.bulk_out_max_packet);
        video_print("\n    Comandos: ", 0x07);
        shell_command_print_num(info.command_count);
        video_print("  Leituras: ", 0x07);
        shell_command_print_num(info.read_ops);
        video_print("  Resets: ", 0x07);
        shell_command_print_num(info.reset_count);
        video_print("  Ultimo erro: ", 0x07);
        shell_command_print_num((uint32_t)info.last_error);
        video_print("\n", 0x07);
    }
}

static void cmd_usb_device(const char* args) {
    char id[USB_CONTROLLER_ID_SIZE];
    usb_controller_info_t info;
    usb_controller_text_t text;
    int result = shell_command_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de usb device");
        video_print("Uso: usb device <id>\n", 0x0C);
        return;
    }
    result = usb_manager_find(id, &info);
    if (result == ERR_NOT_FOUND) {
        video_print("Erro: controlador USB nao encontrado.\n", 0x0C);
        return;
    }
    if (result != OK || usb_manager_format_text(&info, &text) != OK) {
        LOG_ERROR("SHELL", "Falha ao consultar controlador USB");
        video_print("Erro: controlador USB indisponivel.\n", 0x0C);
        return;
    }
    video_print("Controlador USB:\n  ID: ", 0x0B);
    video_print(text.id, 0x0B);
    video_print("\n  Nome: ", 0x07);
    video_print(text.name, 0x07);
    video_print("\n  Modelo: ", 0x07);
    video_print(usb_manager_model_name(info.model), 0x07);
    video_print("\n  Estado: ", 0x07);
    video_print(usb_manager_state_name(info.state),
                cmd_usb_state_color(info.state));
    video_print("\n  Motivo: ", 0x07);
    video_print(usb_manager_reason_name(info.reason), 0x07);
    video_print("\n  Local: ", 0x07);
    video_print(text.location, 0x07);
    video_print("\n  Vendor: 0x", 0x07);
    shell_command_print_hex(info.vendor_id, 4U);
    video_print("  Device: 0x", 0x07);
    shell_command_print_hex(info.device_id, 4U);
    video_print("\n  Class: 0x", 0x07);
    shell_command_print_hex(info.class_code, 2U);
    video_print("  Subclass: 0x", 0x07);
    shell_command_print_hex(info.subclass_code, 2U);
    video_print("  ProgIF: 0x", 0x07);
    shell_command_print_hex(info.prog_if, 2U);
    video_print("\n  Revision: 0x", 0x07);
    shell_command_print_hex(info.revision, 2U);
    video_print("  IRQ: ", 0x07);
    if (info.irq == USB_CONTROLLER_IRQ_UNKNOWN) video_print("UNKNOWN", 0x08);
    else shell_command_print_num(info.irq);
    video_print("\n  BARs: ", 0x07);
    for (uint32_t bar = 0; bar < USB_CONTROLLER_BAR_COUNT; bar++) {
        video_print("0x", 0x08);
        shell_command_print_hex(info.bars[bar], 8U);
        if (bar + 1U < USB_CONTROLLER_BAR_COUNT) video_print(", ", 0x08);
    }
    video_print("\n  Detalhe: ", 0x07);
    video_print(text.detail, 0x07);
    video_print("\n", 0x07);
}

static void cmd_usb_hid(const char* args) {
    uint32_t count = 0U;

    if (shell_command_args_equal(args, "check")) {
        int hid_result = usb_hid_validate_state();
        int input_result = input_validate_state();
        int deferred_result = irq_deferred_validate_state();

        if (hid_result == OK && input_result == OK &&
            deferred_result == OK) {
            video_print("USB HID check: OK\n", 0x0A);
        } else {
            LOG_ERROR("SHELL", "USB HID check detectou estado invalido");
            video_print("USB HID check: FALHOU hid=", 0x0C);
            shell_command_print_num((uint32_t)hid_result);
            video_print(" input=", 0x0C);
            shell_command_print_num((uint32_t)input_result);
            video_print(" deferred=", 0x0C);
            shell_command_print_num((uint32_t)deferred_result);
            video_print("\n", 0x0C);
        }
        return;
    }
    if (!shell_command_args_equal(args, "") &&
        !shell_command_args_equal(args, "status")) {
        LOG_WARN("SHELL", "Uso invalido de usb hid");
        video_print("Uso: usb hid status|check\n", 0x0C);
        return;
    }
    if (usb_hid_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Contagem HID indisponivel");
        video_print("Erro: inventario USB HID indisponivel.\n", 0x0C);
        return;
    }
    video_print("USB HID Boot: ", 0x0B);
    shell_command_print_num(count);
    video_print(" registro(s)\n", 0x07);
    for (uint32_t index = 0U; index < count; index++) {
        usb_hid_info_t info;

        if (usb_hid_get_at(index, &info) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar entrada HID");
            video_print("Erro: entrada USB HID indisponivel.\n", 0x0C);
            return;
        }
        video_print("  ID: ", 0x07);
        video_print(info.id, 0x0A);
        video_print("  ", 0x07);
        video_print(usb_hid_kind_name(info.kind), 0x07);
        video_print("  Estado: ", 0x07);
        video_print(usb_hid_state_name(info.state),
                    info.state == USB_HID_STATE_READY ? 0x0A : 0x0E);
        video_print("  ativo=", 0x08);
        shell_command_print_num(info.active);
        video_print("\n    Endpoint IN 0x", 0x07);
        shell_command_print_hex(info.interrupt_endpoint, 2U);
        video_print(" packet=", 0x07);
        shell_command_print_num(info.max_packet);
        video_print(" interval=", 0x07);
        shell_command_print_num(info.interval);
        video_print("\n    Relatorios=", 0x07);
        shell_command_print_num(info.report_count);
        video_print(" malformed=", 0x08);
        shell_command_print_num(info.malformed_count);
        video_print(" timeout=", 0x08);
        shell_command_print_num(info.timeout_count);
        video_print(" erros=", 0x08);
        shell_command_print_num(info.error_count);
        video_print(" descartes=", 0x08);
        shell_command_print_num(info.dropped_count);
        video_print(" cancelamentos=", 0x08);
        shell_command_print_num(info.cancel_count);
        video_print(" erro=", 0x08);
        shell_command_print_num((uint32_t)info.last_error);
        video_print("\n", 0x07);
    }
}

static void cmd_usb(const char* args) {
    const char* subargs;

    if (shell_command_args_equal(args, "") || shell_command_args_equal(args, "status")) {
        cmd_usb_status();
        return;
    }
    if (shell_command_args_equal(args, "list")) {
        cmd_usb_list("");
        return;
    }
    if (shell_command_args_equal(args, "ports")) {
        cmd_usb_ports("");
        return;
    }
    if (shell_command_args_equal(args, "devices")) {
        cmd_usb_devices("");
        return;
    }
    if (shell_command_args_equal(args, "storage")) {
        cmd_usb_storage("");
        return;
    }
    subargs = shell_command_match_subcommand(args, "hid");
    if (subargs) {
        cmd_usb_hid(subargs);
        return;
    }
    subargs = shell_command_match_subcommand(args, "device");
    if (subargs) {
        cmd_usb_device(subargs);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido do comando usb");
    video_print("Uso: usb status|list|ports|devices|storage|hid|device <id>\n",
                0x0E);
}

int shell_diagnostics_run_device_scan(shell_device_scan_result_t* scan) {
    int recovery_result;

    if (!scan) {
        LOG_ERROR("SHELL", "Destino nulo para resultado de device-scan");
        return ERR_NULL;
    }
    scan->pci_result = pci_init();
    scan->devices_result = ERR_STATE;
    scan->usb_result = ERR_STATE;
    scan->storage_result = ERR_STATE;
    scan->network_result = ERR_STATE;
    if (scan->pci_result != OK && scan->pci_result != ERR_OVERFLOW) {
        recovery_result =
            recovery_mark_disabled(RECOVERY_COMPONENT_DEVICES,
                                   scan->pci_result, "Varredura PCI falhou");
        if (recovery_result != OK) {
            LOG_ERROR("SHELL", "Falha ao desabilitar Devices no health");
        }
        LOG_ERROR("SHELL", "Falha ao refazer varredura PCI");
        return scan->pci_result;
    }

    scan->usb_result = usb_manager_refresh();
    if (scan->usb_result == ERR_STATE) {
        scan->usb_result = usb_manager_init();
    }
    if (scan->usb_result != OK && scan->usb_result != ERR_OVERFLOW) {
        LOG_ERROR("SHELL", "Falha ao atualizar inventario USB");
        return scan->usb_result;
    }
    scan->storage_result = storage_refresh();
    if (file_index_rebuild() != OK) {
        LOG_WARN("SHELL", "Indice aguardara a nova geracao de Storage");
    }
    if (scan->storage_result != OK &&
        scan->storage_result != ERR_NOT_FOUND &&
        scan->storage_result != ERR_STATE) {
        LOG_WARN("SHELL", "Storage atualizado com estado degradado");
    }

    scan->devices_result = device_manager_refresh();
    if (scan->devices_result != OK &&
        scan->devices_result != ERR_OVERFLOW) {
        recovery_result =
            recovery_mark_disabled(
                RECOVERY_COMPONENT_DEVICES, scan->devices_result,
                "Inventario de dispositivos indisponivel");
        if (recovery_result != OK) {
            LOG_ERROR("SHELL", "Falha ao desabilitar Devices no health");
        }
        LOG_ERROR("SHELL", "Falha ao atualizar inventario de dispositivos");
        return scan->devices_result;
    }
    scan->network_result = network_manager_refresh();

    if (scan->pci_result == ERR_OVERFLOW ||
        scan->devices_result == ERR_OVERFLOW) {
        recovery_result =
            recovery_mark_degraded(RECOVERY_COMPONENT_DEVICES, ERR_OVERFLOW,
                                   "Inventario PCI parcial");
    } else {
        recovery_result = recovery_mark_ready(RECOVERY_COMPONENT_DEVICES);
    }
    if (recovery_result != OK) {
        LOG_ERROR("SHELL", "Falha ao sincronizar Devices no health");
        scan->devices_result = recovery_result;
        return recovery_result;
    }
    if (scan->pci_result != OK) return scan->pci_result;
    if (scan->usb_result == ERR_OVERFLOW) return ERR_OVERFLOW;
    return scan->devices_result;
}

static void cmd_device_scan(const char* args) {
    shell_device_scan_result_t scan;
    int result;

    if (*args) {
        LOG_WARN("SHELL", "Uso invalido de device-scan");
        video_print("Uso: device-scan\n", 0x0C);
        return;
    }
    result = shell_diagnostics_run_device_scan(&scan);
    if (scan.pci_result != OK && scan.pci_result != ERR_OVERFLOW) {
        video_print("Erro: varredura PCI indisponivel.\n", 0x0C);
        return;
    }
    if (scan.devices_result != OK &&
        scan.devices_result != ERR_OVERFLOW) {
        video_print("Erro: inventario de dispositivos indisponivel.\n", 0x0C);
        return;
    }
    if (scan.usb_result != OK && scan.usb_result != ERR_OVERFLOW) {
        video_print("Erro: inventario USB indisponivel.\n", 0x0C);
        return;
    }
    if (scan.network_result != OK &&
        scan.network_result != ERR_OVERFLOW) {
        LOG_WARN("SHELL", "Falha ao atualizar inventario de rede");
        video_print("Aviso: inventario de rede indisponivel.\n", 0x0E);
    }
    if (scan.storage_result != OK && scan.storage_result != ERR_NOT_FOUND &&
        scan.storage_result != ERR_STATE) {
        video_print("Aviso: inventario de storage degradado.\n", 0x0E);
    }
    if (result == ERR_OVERFLOW) {
        video_print("Varredura PCI/USB parcial; inventario atualizado.\n",
                    0x0E);
        return;
    }
    video_print("Varredura PCI concluida; inventario atualizado.\n", 0x0A);
    if (scan.usb_result == ERR_OVERFLOW) {
        video_print("Aviso: inventario USB parcial.\n", 0x0E);
    }
    if (scan.network_result == ERR_OVERFLOW) {
        video_print("Aviso: inventario de rede parcial.\n", 0x0E);
    }
}

static void cmd_power(const char* args) {
    power_status_t status;

    if (!shell_command_args_equal(args, "status")) {
        LOG_WARN("SHELL", "Uso invalido de power");
        video_print("Uso: power status\n", 0x0C);
        return;
    }
    if (power_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Diagnostico de energia indisponivel");
        video_print("Erro: diagnostico de energia indisponivel.\n", 0x0C);
        return;
    }

    video_print("Energia:\n", 0x0B);
    video_print("  ACPI: ", 0x07);
    video_print(status.acpi_available ? "DETECTADO" : "INDISPONIVEL",
                status.acpi_available ? 0x0A : 0x0E);
    video_print("\n  Tabelas de energia ACPI: ", 0x07);
    video_print(status.acpi_power_tables_available ?
                "VALIDADAS" : "INDISPONIVEIS",
                status.acpi_power_tables_available ? 0x0A : 0x0E);
    if (status.acpi_available) {
        video_print("\n  Snapshot ACPI: ", 0x07);
        video_print(status.acpi_partial ? "PARCIAL" : "COMPLETO",
                    status.acpi_partial ? 0x0E : 0x0A);
    }
    video_print("\n  Diagnostico PM1: ", 0x07);
    video_print(status.acpi_pm1_control_available ?
                "DISPONIVEL" : "INDISPONIVEL",
                status.acpi_pm1_control_available ? 0x0A : 0x0E);
    video_print("\n  Modo ACPI: ", 0x07);
    if (!status.acpi_mode_known) {
        video_print("DESCONHECIDO", 0x0E);
    } else {
        video_print(status.acpi_mode_enabled ?
                    "HABILITADO" : "DESABILITADO",
                    status.acpi_mode_enabled ? 0x0A : 0x0E);
    }
    video_print("\n  S5 declarado pelo firmware: ", 0x07);
    video_print(status.acpi_s5_declared ? "SIM" : "NAO",
                status.acpi_s5_declared ? 0x0A : 0x0E);
    video_print("\n  Ativacao do modo ACPI: ", 0x07);
    if (status.acpi_mode_enabled) {
        video_print("NAO NECESSARIA", 0x0A);
    } else {
        video_print(status.acpi_mode_enable_available ?
                    "DISPONIVEL" : "INDISPONIVEL",
                    status.acpi_mode_enable_available ? 0x0A : 0x0E);
    }
    video_print("\n  Transicao S5 ACPI: ", 0x07);
    video_print(status.acpi_s5_transition_ready ?
                "PRONTA" : "INDISPONIVEL",
                status.acpi_s5_transition_ready ? 0x0A : 0x0E);
    video_print("\n  S0: ", 0x07);
    video_print(power_capability_name(status.states[POWER_STATE_S0]), 0x0A);
    for (uint32_t state = POWER_STATE_S1; state <= POWER_STATE_S5; state++) {
        video_print("\n  S", 0x07);
        shell_command_print_num(state);
        video_print(": ", 0x07);
        video_print(power_capability_name(status.states[state]),
                    status.states[state] == POWER_CAPABILITY_UNAVAILABLE ?
                    0x0E : 0x0B);
    }
    video_print("\n  Idle CPU (HLT/C1): ", 0x07);
    video_print(power_capability_name(status.cpu_idle), 0x0A);
    video_print("\n  Desligamento fisico: ", 0x07);
    video_print(power_capability_name(status.hardware_poweroff),
                status.hardware_poweroff == POWER_CAPABILITY_AVAILABLE ?
                0x0A : 0x0E);
    video_print("\n  Reboot (controlador de teclado): ", 0x07);
    video_print(power_capability_name(status.reboot), 0x0A);
    video_print("\n  Fallback sem S5 ACPI: HLT; a maquina permanece ligada.\n",
                0x08);
}

static void cmd_acpi_print_table(const char* name, uint8_t present,
                                 uint32_t address) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    if (!present) {
        video_print("NAO ENCONTRADA\n", 0x0E);
        return;
    }
    video_print("VALIDA em 0x", 0x0A);
    shell_command_print_hex(address, 8U);
    video_print("\n", 0x07);
}

static const char* cmd_acpi_mode_name(acpi_mode_t mode) {
    if (mode == ACPI_MODE_DISABLED) return "DESABILITADO";
    if (mode == ACPI_MODE_ENABLED) return "HABILITADO";
    if (mode == ACPI_MODE_INCONSISTENT) return "INCONSISTENTE";
    return "DESCONHECIDO";
}

static const char* cmd_acpi_s5_name(acpi_s5_state_t state) {
    if (state == ACPI_S5_DECLARED) return "DECLARADO";
    if (state == ACPI_S5_MALFORMED) return "MALFORMADO";
    if (state == ACPI_S5_AMBIGUOUS) return "AMBIGUO";
    return "INDISPONIVEL";
}

static const char* cmd_acpi_space_name(uint8_t space_id) {
    if (space_id == ACPI_ADDRESS_SPACE_SYSTEM_MEMORY) return "MEMORIA";
    if (space_id == ACPI_ADDRESS_SPACE_SYSTEM_IO) return "SYSTEM-IO";
    return "NAO SUPORTADO";
}

static void cmd_acpi_print_address(uint64_t address) {
    uint32_t high = (uint32_t)(address >> 32);

    if (high) shell_command_print_hex(high, 8U);
    shell_command_print_hex((uint32_t)address, 8U);
}

static void cmd_acpi_print_register(const char* name, uint8_t present,
                                    uint8_t readable,
                                    const acpi_register_t* reg,
                                    uint16_t value) {
    video_print("  ", 0x07);
    video_print(name, 0x07);
    video_print(": ", 0x07);
    if (!present) {
        video_print("INDISPONIVEL\n", 0x0E);
        return;
    }
    video_print(cmd_acpi_space_name(reg->address_space_id),
                readable ? 0x0A : 0x0E);
    video_print(" 0x", 0x07);
    cmd_acpi_print_address(reg->address);
    video_print(" bits=", 0x07);
    shell_command_print_num(reg->register_bit_width);
    video_print(" offset=", 0x07);
    shell_command_print_num(reg->register_bit_offset);
    video_print(" acesso=", 0x07);
    shell_command_print_num(reg->access_size);
    if (readable) {
        video_print(" valor=0x", 0x07);
        shell_command_print_hex(value, 4U);
    } else {
        video_print(" leitura=NAO SUPORTADA", 0x0E);
    }
    video_print("\n", 0x07);
}

static void cmd_acpi_print_power(const acpi_power_info_t* info) {
    video_print("  FADT energia: ", 0x07);
    video_print(info->fadt_power_fields_present ?
                "PRESENTE" : "INDISPONIVEL",
                info->fadt_power_fields_present ? 0x0A : 0x0E);
    video_print("\n  Hardware-reduced: ", 0x07);
    if (!info->fadt_power_fields_present) {
        video_print("INDISPONIVEL", 0x0E);
    } else {
        video_print(info->hardware_reduced ? "SIM" : "NAO",
                    info->hardware_reduced ? 0x0E : 0x07);
    }
    video_print("\n  SMI_CMD: ", 0x07);
    if (info->smi_command_port) {
        video_print("0x", 0x07);
        shell_command_print_hex(info->smi_command_port, 8U);
        video_print(" enable=0x", 0x07);
        shell_command_print_hex(info->acpi_enable_value, 2U);
        video_print(" disable=0x", 0x07);
        shell_command_print_hex(info->acpi_disable_value, 2U);
    } else {
        video_print("INDISPONIVEL", 0x0E);
    }
    video_print(" PM1_LEN=", 0x07);
    shell_command_print_num(info->pm1_control_length);
    video_print("\n", 0x07);
    cmd_acpi_print_register("PM1a Control", info->pm1a_present,
                            info->pm1a_readable, &info->pm1a_control,
                            info->pm1a_value);
    cmd_acpi_print_register("PM1b Control", info->pm1b_present,
                            info->pm1b_readable, &info->pm1b_control,
                            info->pm1b_value);
    video_print("  Modo ACPI: ", 0x07);
    video_print(cmd_acpi_mode_name(info->mode),
                info->mode == ACPI_MODE_ENABLED ? 0x0A : 0x0E);
    video_print("\n  _S5_: ", 0x07);
    video_print(cmd_acpi_s5_name(info->s5_state),
                info->s5_state == ACPI_S5_DECLARED ? 0x0A : 0x0E);
    if (info->s5_state == ACPI_S5_DECLARED) {
        video_print(" tipo_a=", 0x07);
        shell_command_print_num(info->s5_type_a);
        video_print(" tipo_b=", 0x07);
        shell_command_print_num(info->s5_type_b);
    }
    video_print(" candidatos=", 0x07);
    shell_command_print_num(info->s5_candidates);
    video_print("\n  Ativacao do modo ACPI: ", 0x07);
    video_print(info->mode == ACPI_MODE_ENABLED ?
                "NAO NECESSARIA" :
                (info->mode_enable_available ?
                 "DISPONIVEL" : "INDISPONIVEL"),
                (info->mode == ACPI_MODE_ENABLED ||
                 info->mode_enable_available) ? 0x0A : 0x0E);
    video_print("\n  Transicao S5: ", 0x07);
    video_print(info->s5_transition_ready ? "PRONTA" : "INDISPONIVEL",
                info->s5_transition_ready ? 0x0A : 0x0E);
    video_print("\n", 0x07);
}

static void cmd_acpi(const char* args) {
    acpi_status_t status;
    acpi_power_info_t power_info;
    int power_info_result;

    if (!shell_command_args_equal(args, "status")) {
        LOG_WARN("SHELL", "Uso invalido de acpi");
        video_print("Uso: acpi status\n", 0x0C);
        return;
    }
    if (acpi_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Diagnostico ACPI indisponivel");
        video_print("Erro: diagnostico ACPI indisponivel.\n", 0x0C);
        return;
    }
    power_info_result = acpi_get_power_info(&power_info);
    if (power_info_result != OK) {
        LOG_WARN("SHELL", "Snapshot de energia ACPI indisponivel");
    }

    video_print("ACPI:\n  Estado: ", 0x0B);
    if (!status.available) {
        video_print("INDISPONIVEL\n", 0x0E);
        video_print("  RSDP: nao encontrado ou invalido\n", 0x08);
    } else {
        video_print(status.partial ? "DEGRADADO" : "PRONTO",
                    status.partial ? 0x0E : 0x0A);
        video_print("\n  OEM: ", 0x07);
        video_print(status.oem_id, 0x0B);
        video_print(" revisao=", 0x07);
        shell_command_print_num(status.revision);
        video_print("\n  RSDP: 0x", 0x07);
        shell_command_print_hex(status.rsdp_address, 8U);
        video_print("\n  Raiz: ", 0x07);
        video_print(acpi_root_kind_name(status.root_kind), 0x0B);
        video_print(" em 0x", 0x07);
        shell_command_print_hex(status.root_address, 8U);
        video_print("\n  Entradas raiz=", 0x07);
        shell_command_print_num(status.root_entry_count);
        video_print(" tabelas copiadas=", 0x07);
        shell_command_print_num(status.table_count);
        video_print("\n", 0x07);
        cmd_acpi_print_table("FADT", status.fadt_present,
                             status.fadt_address);
        cmd_acpi_print_table("DSDT", status.dsdt_present,
                             status.dsdt_address);
        cmd_acpi_print_table("FACS", status.facs_present,
                             status.facs_address);
    }
    video_print("  Invalidas=", 0x07);
    shell_command_print_num(status.malformed_tables);
    video_print(" ignoradas=", 0x07);
    shell_command_print_num(status.skipped_tables);
    video_print(" ticks=", 0x07);
    shell_command_print_num(status.scan_ticks);
    video_print("\n", 0x07);
    if (power_info_result == OK) {
        cmd_acpi_print_power(&power_info);
    } else {
        video_print("  Energia ACPI: INDISPONIVEL\n", 0x0E);
    }
    video_print("  AML completo/SCI/GPE: NAO IMPLEMENTADOS\n",
                0x0E);
}

static uint32_t shell_kmetrics_delta(uint32_t current, uint32_t baseline) {
    return current - baseline;
}

static void shell_kmetrics_take_snapshot(shell_kmetrics_snapshot_t* snapshot) {
    if (!snapshot) {
        LOG_ERROR("SHELL", "Destino nulo ao capturar metricas K1");
        return;
    }

    kmemset(snapshot, 0, sizeof(shell_kmetrics_snapshot_t));
    snapshot->ticks = timer_get_ticks();
    keyboard_get_metrics(&snapshot->keyboard);
    ipc_get_stats(&snapshot->ipc);
    scheduler_get_stats(&snapshot->scheduler);
    vesa_get_metrics(&snapshot->vesa);
    memory_get_heap_stats(&snapshot->heap);
    memory_get_pmm_stats(&snapshot->pmm);
    paging_get_user_stats(&snapshot->paging_user);
    if (paging_get_boot_stats(&snapshot->paging_boot) != OK) {
        LOG_WARN("SHELL", "Metricas de bootstrap do paging indisponiveis");
    }
}

static void cmd_kmetrics_print_scheduler(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline) {
    video_print("  Scheduler: trocas=", 0x07);
    shell_command_print_num(shell_kmetrics_delta(current->scheduler.context_switches,
                                   baseline->scheduler.context_switches));
    video_print(" READY=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    shell_command_print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print("\n", 0x07);
    video_print("             cooperativos=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->scheduler.cooperative_yields,
                                   baseline->scheduler.cooperative_yields));
    video_print(" preempcoes_user=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->scheduler.user_preemptions,
                                   baseline->scheduler.user_preemptions));
    video_print(" idle_fallbacks=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->scheduler.idle_fallbacks,
                                   baseline->scheduler.idle_fallbacks));
    video_print(" quantum_user=", 0x08);
    shell_command_print_num(current->scheduler.user_quantum_ticks);
    video_print(" tick\n", 0x07);
}

static void cmd_kmetrics_print_queues(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline) {
    video_print("  Filas: teclado=", 0x07);
    shell_command_print_num(current->keyboard.queued);
    video_print("/", 0x08);
    shell_command_print_num(current->keyboard.capacity);
    video_print(" descartes=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->keyboard.dropped,
                                   baseline->keyboard.dropped));
    video_print(" processados=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->keyboard.processed,
                                   baseline->keyboard.processed));
    video_print(" pico=", 0x08);
    shell_command_print_num(current->keyboard.peak_queued);
    video_print("\n", 0x07);
    video_print("         IPC pendentes=", 0x07);
    shell_command_print_num(ipc_get_pending_count());
    video_print(" capacidade_por_processo=", 0x08);
    shell_command_print_num(IPC_MSG_QUEUE_SIZE - 1U);
    video_print(" enviados=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->ipc.sent, baseline->ipc.sent));
    video_print(" recebidos=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->ipc.received, baseline->ipc.received));
    video_print(" falhas=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->ipc.failed, baseline->ipc.failed));
    video_print(" cheias=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->ipc.queue_full,
                                   baseline->ipc.queue_full));
    video_print("\n", 0x07);
}

static void cmd_kmetrics_print_memory(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline) {
    const memory_heap_stats_t* heap = &current->heap;
    const memory_pmm_stats_t* pmm = &current->pmm;
    const paging_user_stats_t* paging_user = &current->paging_user;
    const paging_boot_stats_t* paging_boot = &current->paging_boot;

    video_print("  Memoria PMM: usada=", 0x07);
    shell_command_print_num(memory_get_used() / 1024U);
    video_print(" KB livre=", 0x08);
    shell_command_print_num(memory_get_free() / 1024U);
    video_print(" KB paginas_livres=", 0x08);
    shell_command_print_num(memory_get_free_pages());
    video_print(" proprias=", 0x08);
    shell_command_print_num(pmm->owned_pages);
    video_print(" falhas=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(pmm->allocation_failures,
                                   baseline->pmm.allocation_failures));
    video_print(" rejeicoes=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(pmm->invalid_frees,
                                   baseline->pmm.invalid_frees));
    video_print("\n", 0x07);
    video_print("  Paging boot: paginas=", 0x07);
    shell_command_print_num(paging_boot->identity_pages);
    video_print(" tabelas=", 0x08);
    shell_command_print_num(paging_boot->page_tables_created);
    video_print(" ticks=", 0x08);
    shell_command_print_num(paging_boot->init_ticks);
    video_print(" modo=blocos\n", 0x08);
    video_print("  Heap: ", 0x07);
    if (!heap->initialized || !heap->valid) {
        video_print("N/D\n", 0x08);
    } else {
        video_print("usado=", 0x07);
        shell_command_print_num(heap->used_bytes / 1024U);
        video_print(" KB livre=", 0x08);
        shell_command_print_num(heap->free_bytes / 1024U);
        video_print(" KB total=", 0x08);
        shell_command_print_num(heap->total_bytes / 1024U);
        video_print(" KB blocos=", 0x08);
        shell_command_print_num(heap->allocated_blocks);
        video_print("/", 0x08);
        shell_command_print_num(heap->free_blocks);
        video_print(" maior_livre=", 0x08);
        shell_command_print_num(heap->largest_free_block / 1024U);
        video_print(" KB frag=", 0x08);
        shell_command_print_num(heap->fragmentation_percent);
        video_print("% falhas=", 0x08);
        shell_command_print_num(shell_kmetrics_delta(heap->allocation_failures,
                                       baseline->heap.allocation_failures));
        video_print(" invalidos=", 0x08);
        shell_command_print_num(shell_kmetrics_delta(heap->invalid_frees,
                                       baseline->heap.invalid_frees));
        video_print(" duplicados=", 0x08);
        shell_command_print_num(shell_kmetrics_delta(heap->double_frees,
                                       baseline->heap.double_frees));
        video_print("\n", 0x07);
    }
    video_print("  Paging user: dirs=", 0x07);
    shell_command_print_num(paging_user->active_directories);
    video_print(" paginas=", 0x08);
    shell_command_print_num(paging_user->active_pages);
    video_print(" criados=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(paging_user->directories_created,
                                   baseline->paging_user.directories_created));
    video_print(" liberados=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(paging_user->directories_released,
                                   baseline->paging_user.directories_released));
    video_print(" rejeicoes=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(paging_user->rejected_releases,
                                   baseline->paging_user.rejected_releases));
    video_print("\n", 0x07);
}

static void cmd_kmetrics_print_vesa(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline) {
    vesa_mode_t* mode = vesa_get_mode();
    uint32_t presentations;
    uint32_t copied_bytes;

    video_print("  VESA: ", 0x07);
    if (!mode || !mode->initialized || !vesa_has_backbuffer()) {
        video_print("N/D\n", 0x08);
        return;
    }
    presentations = shell_kmetrics_delta(current->vesa.presentations,
                                         baseline->vesa.presentations);
    copied_bytes = shell_kmetrics_delta(current->vesa.bytes_copied,
                                        baseline->vesa.bytes_copied);
    video_print("apresentacoes=", 0x07);
    shell_command_print_num(presentations);
    video_print(" completas=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->vesa.full_presentations,
                                   baseline->vesa.full_presentations));
    video_print(" parciais=", 0x08);
    shell_command_print_num(shell_kmetrics_delta(current->vesa.partial_presentations,
                                   baseline->vesa.partial_presentations));
    video_print(" bytes=", 0x08);
    shell_command_print_num(copied_bytes);
    video_print(" media_bytes=", 0x08);
    if (presentations == 0) {
        video_print("N/D", 0x08);
    } else {
        shell_command_print_num(copied_bytes / presentations);
    }
    video_print("\n    ultima=", 0x08);
    if (presentations == 0) {
        video_print("N/D", 0x08);
    } else {
        shell_command_print_num(current->vesa.last_copy_bytes);
        video_print(" bytes/", 0x08);
        shell_command_print_num(current->vesa.last_copy_ticks);
        video_print(" ticks", 0x08);
    }
    video_print(" max_boot=", 0x08);
    shell_command_print_num(current->vesa.max_copy_ticks);
    video_print("\n", 0x07);
}

static void cmd_kmetrics(const char* args) {
    shell_kmetrics_snapshot_t current;
    shell_kmetrics_snapshot_t empty;
    const shell_kmetrics_snapshot_t* baseline;

    if (*args) {
        if (kstrcmp(args, "reset") != 0) {
            video_print("Uso: kmetrics [reset]\n", 0x0C);
            return;
        }
        shell_kmetrics_take_snapshot(&shell_kmetrics_baseline.snapshot);
        shell_kmetrics_baseline.valid = 1;
        video_print("Linha-base K1 capturada.\n", 0x0A);
        return;
    }

    shell_kmetrics_take_snapshot(&current);
    kmemset(&empty, 0, sizeof(shell_kmetrics_snapshot_t));
    baseline = shell_kmetrics_baseline.valid ?
               &shell_kmetrics_baseline.snapshot : &empty;
    video_begin_update();
    video_print(shell_kmetrics_baseline.valid ?
                "Metricas K1 (desde reset):\n" :
                "Metricas K1 (desde boot):\n", 0x0B);
    video_print("  PIT: ticks=", 0x07);
    shell_command_print_num(shell_kmetrics_delta(current.ticks, baseline->ticks));
    video_print(" frequencia=", 0x08);
    shell_command_print_num(timer_get_frequency());
    video_print(" Hz\n", 0x07);
    video_print("  CPU real: N/D (RDTSC/PMU adiado)\n", 0x07);
    video_print("  CPU estimada: TCK% do Task Manager\n", 0x08);
    cmd_kmetrics_print_scheduler(&current, baseline);
    cmd_kmetrics_print_queues(&current, baseline);
    cmd_kmetrics_print_memory(&current, baseline);
    cmd_kmetrics_print_vesa(&current, baseline);
    video_end_update();
}

static int shell_memcheck_same_layout(const memory_heap_stats_t* before,
                                      const memory_heap_stats_t* after) {
    if (!before || !after) return 0;
    return before->total_bytes == after->total_bytes &&
           before->used_bytes == after->used_bytes &&
           before->free_bytes == after->free_bytes &&
           before->allocated_blocks == after->allocated_blocks &&
           before->free_blocks == after->free_blocks &&
           before->largest_free_block == after->largest_free_block &&
           before->fragmentation_percent == after->fragmentation_percent;
}

static void cmd_memcheck_print_result(const char* label, int passed) {
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(passed ? " OK\n" : " ERRO\n", passed ? 0x0A : 0x0C);
}

int shell_diagnostics_run_memcheck(shell_memcheck_result_t* result_out) {
    memory_heap_stats_t heap_before;
    memory_heap_stats_t heap_after;
    memory_pmm_stats_t pmm_before;
    memory_pmm_stats_t pmm_after;
    paging_user_stats_t paging_before;
    paging_user_stats_t paging_after;
    void* block_a = 0;
    void* block_b = 0;
    void* block_c = 0;

    if (!result_out) {
        LOG_ERROR("SHELL", "Destino nulo para resultado do MemCheck");
        return ERR_NULL;
    }

    memory_get_heap_stats(&heap_before);
    memory_get_pmm_stats(&pmm_before);
    paging_get_user_stats(&paging_before);

    block_a = kmalloc(SHELL_MEMCHECK_BLOCK_A);
    block_b = kmalloc(SHELL_MEMCHECK_BLOCK_B);
    block_c = kmalloc(SHELL_MEMCHECK_BLOCK_C);
    if (block_b) kfree(block_b);
    if (block_a) kfree(block_a);
    if (block_c) kfree(block_c);

    memory_get_heap_stats(&heap_after);
    memory_get_pmm_stats(&pmm_after);
    paging_get_user_stats(&paging_after);

    result_out->heap_integrity = heap_before.initialized && heap_before.valid &&
                                 heap_after.initialized && heap_after.valid;
    result_out->coalescence = block_a && block_b && block_c &&
                              result_out->heap_integrity &&
                              shell_memcheck_same_layout(&heap_before, &heap_after);
    result_out->pmm_guards = pmm_before.initialized && pmm_after.initialized &&
                             pmm_before.owned_pages == pmm_after.owned_pages &&
                             pmm_before.allocation_failures ==
                                 pmm_after.allocation_failures &&
                             pmm_before.invalid_frees == pmm_after.invalid_frees;
    result_out->user_directories = paging_before.active_directories == 0 &&
                                   paging_before.active_pages == 0 &&
                                   paging_after.active_directories == 0 &&
                                   paging_after.active_pages == 0;

    if (!result_out->heap_integrity || !result_out->coalescence ||
        !result_out->pmm_guards || !result_out->user_directories) {
        LOG_ERROR("SHELL", "MemCheck detectou falha de integridade");
        return ERR_STATE;
    }
    return OK;
}

static void cmd_memcheck(const char* args) {
    shell_memcheck_result_t result;
    int run_result;

    if (*args) {
        video_print("Uso: memcheck\n", 0x0C);
        return;
    }
    if (process_get_user_count() != 0 ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) != 0 ||
        app_loader_is_foreground_active()) {
        LOG_WARN("SHELL", "MemCheck recusado com processo ring 3 pendente");
        video_print("MemCheck indisponivel: processo ring 3 ou zumbi pendente.\n",
                    0x0C);
        return;
    }

    kmemset(&result, 0, sizeof(result));
    run_result = shell_diagnostics_run_memcheck(&result);

    video_begin_update();
    video_print("MemCheck:\n", 0x0B);
    cmd_memcheck_print_result("heap_integridade", result.heap_integrity);
    cmd_memcheck_print_result("coalescencia", result.coalescence);
    cmd_memcheck_print_result("pmm_guardas", result.pmm_guards);
    cmd_memcheck_print_result("diretorios_user", result.user_directories);
    cmd_memcheck_print_result("resultado", run_result == OK);
    video_end_update();
}

static void cmd_schedcheck_print_result(const char* label, int passed) {
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(passed ? " OK\n" : " ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_schedcheck(const char* args) {
    scheduler_validation_t validation;
    int result;

    if (*args) {
        video_print("Uso: schedcheck\n", 0x0C);
        return;
    }

    result = scheduler_validate_invariants(&validation);
    video_begin_update();
    video_print("SchedCheck:\n", 0x0B);
    cmd_schedcheck_print_result("estado_atual", validation.current_valid);
    cmd_schedcheck_print_result("idle", validation.idle_valid);
    cmd_schedcheck_print_result("tabela_pid", validation.pid_table_valid);
    cmd_schedcheck_print_result("estados", validation.state_table_valid);
    cmd_schedcheck_print_result("resultado", result == OK);
    video_end_update();
}

static void cmd_mouse_print_usage(void) {
    video_print("Uso: mouse | mouse speed <1-10> | ", 0x0C);
    video_print("mouse primary <left|right> | ", 0x0C);
    video_print("mouse acceleration <on|off>\n", 0x0C);
}

static int cmd_mouse_read_token(const char** cursor, char* token,
                                int token_size) {
    const char* input;
    int length = 0;

    if (!cursor || !*cursor || !token || token_size < 2) {
        LOG_ERROR("SHELL", "Destino invalido no parser do comando mouse");
        return ERR_NULL;
    }
    input = *cursor;
    while (*input == ' ' || *input == '\t') input++;
    if (!*input) {
        LOG_ERROR("SHELL", "Argumento ausente no comando mouse");
        return ERR_INVALID;
    }
    while (*input && *input != ' ' && *input != '\t') {
        if (length >= token_size - 1) {
            LOG_ERROR("SHELL", "Argumento longo demais no comando mouse");
            return ERR_OVERFLOW;
        }
        token[length++] = *input++;
    }
    token[length] = '\0';
    *cursor = input;
    return OK;
}

static int cmd_mouse_has_extra_args(const char* cursor) {
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (!*cursor) return 0;
    LOG_ERROR("SHELL", "Argumentos excedentes no comando mouse");
    return 1;
}

static void cmd_mouse_print_status(void) {
    mouse_status_t status;

    if (mouse_get_status(&status) != OK) {
        video_print("Erro: status do mouse indisponivel.\n", 0x0C);
        return;
    }
    video_print("Mouse PS/2:\n", 0x0B);
    video_print("  Driver: ", 0x07);
    video_print(status.initialized ? "DISPONIVEL\n" : "INDISPONIVEL\n",
                status.initialized ? 0x0A : 0x0C);
    video_print("  Posicao: ", 0x07);
    shell_command_print_num((uint32_t)status.x);
    video_print(",", 0x07);
    shell_command_print_num((uint32_t)status.y);
    video_print("\n  Velocidade: ", 0x07);
    shell_command_print_num(status.config.speed);
    video_print("\n  Aceleracao: ", 0x07);
    video_print(status.config.acceleration_enabled ? "ON" : "OFF", 0x0B);
    video_print("\n  Botao principal: ", 0x07);
    video_print(status.config.primary_button == MOUSE_PRIMARY_RIGHT ?
                "right" : "left", 0x0B);
    video_print("\n  Botoes efetivos: ", 0x07);
    shell_command_print_num(status.effective_buttons);
    video_print("  brutos: ", 0x07);
    shell_command_print_num(status.raw_buttons);
    video_print("\n  Roda: ", 0x07);
    video_print(status.wheel_supported ? "DISPONIVEL" : "INDISPONIVEL",
                status.wheel_supported ? 0x0A : 0x0E);
    video_print("\n  Pacotes descartados: ", 0x07);
    shell_command_print_num(status.dropped_packets);
    video_print("\n  Ultimo erro: ", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n  Persistencia: somente RAM\n", 0x0E);
}

static int cmd_mouse_parse_speed(const char* value, uint8_t* speed) {
    if (!value || !speed) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar velocidade do mouse");
        return ERR_NULL;
    }
    if (value[0] >= '1' && value[0] <= '9' && value[1] == '\0') {
        *speed = (uint8_t)(value[0] - '0');
        return OK;
    }
    if (value[0] == '1' && value[1] == '0' && value[2] == '\0') {
        *speed = MOUSE_SPEED_MAX;
        return OK;
    }
    LOG_ERROR("SHELL", "Velocidade invalida no comando mouse");
    return ERR_INVALID;
}

static int cmd_mouse_apply(const char* action, const char* value) {
    uint8_t speed;

    if (kstrcmp(action, "speed") == 0) {
        if (cmd_mouse_parse_speed(value, &speed) != OK) return ERR_INVALID;
        return mouse_set_speed(speed);
    }
    if (kstrcmp(action, "primary") == 0) {
        if (kstrcmp(value, "left") == 0) {
            return mouse_set_primary_button(MOUSE_PRIMARY_LEFT);
        }
        if (kstrcmp(value, "right") == 0) {
            return mouse_set_primary_button(MOUSE_PRIMARY_RIGHT);
        }
        LOG_ERROR("SHELL", "Botao principal invalido no comando mouse");
        return ERR_INVALID;
    }
    if (kstrcmp(action, "acceleration") == 0) {
        if (kstrcmp(value, "on") == 0) return mouse_set_acceleration(1);
        if (kstrcmp(value, "off") == 0) return mouse_set_acceleration(0);
        LOG_ERROR("SHELL", "Aceleracao invalida no comando mouse");
        return ERR_INVALID;
    }
    LOG_ERROR("SHELL", "Subcomando de mouse desconhecido");
    return ERR_INVALID;
}

static void cmd_mouse(const char* args) {
    char action[16];
    char value[16];
    const char* cursor = args;
    int result;

    if (!args || !*args) {
        cmd_mouse_print_status();
        return;
    }
    if (cmd_mouse_read_token(&cursor, action, sizeof(action)) != OK ||
        cmd_mouse_read_token(&cursor, value, sizeof(value)) != OK ||
        cmd_mouse_has_extra_args(cursor)) {
        cmd_mouse_print_usage();
        return;
    }
    result = cmd_mouse_apply(action, value);
    if (result != OK) {
        video_print(result == ERR_UNAVAILABLE ?
                    "Erro: driver de mouse indisponivel.\n" :
                    "Erro: preferencia invalida; estado preservado.\n", 0x0C);
        cmd_mouse_print_usage();
        return;
    }
    video_print("Preferencia do mouse aplicada em RAM.\n", 0x0A);
    cmd_mouse_print_status();
}

void shell_diagnostics_reset(void) {
    kmemset(&shell_kmetrics_baseline, 0, sizeof(shell_kmetrics_baseline));
}

void shell_diagnostics_print_usb_fixture_report(void) {
    uint32_t device_count = 0U;
    uint32_t port_count = 0U;
    uint32_t controller_count = 0U;

    if (usb_manager_get_device_count(&device_count) != OK) {
        LOG_WARN("SHELL", "Relatorio USB automatico indisponivel");
        return;
    }
    if (usb_manager_get_count(&controller_count) != OK) {
        LOG_WARN("SHELL", "Controladores ausentes no relatorio USB");
        return;
    }
    if (usb_manager_get_port_count(&port_count) != OK ||
        (!device_count && !port_count && !controller_count)) return;

    video_print("USB fixture report (automatico):\n", 0x0B);
    cmd_usb_status();
    cmd_usb_list("");
    cmd_usb_ports("");
    cmd_usb_devices("");
    cmd_usb_hid("status");
    cmd_usb_storage("");
    for (uint32_t index = 0U; index < controller_count; index++) {
        usb_controller_info_t info;
        usb_controller_text_t text;

        if (usb_manager_get_info(index, &info) != OK ||
            usb_manager_format_text(&info, &text) != OK) {
            LOG_WARN("SHELL", "Detalhe de controlador ausente no relatorio USB");
            continue;
        }
        cmd_usb_device(text.id);
    }
}

#define SHELL_DIAGNOSTICS_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_health, cmd_health)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_log, cmd_log)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_timer, cmd_timer)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_clock, cmd_clock)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_tls, cmd_tls)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_wait, cmd_wait)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_devices, cmd_devices)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_device_info, cmd_device_info)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_device_scan, cmd_device_scan)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_usb, cmd_usb)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_acpi, cmd_acpi)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_power, cmd_power)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_kmetrics, cmd_kmetrics)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_memcheck, cmd_memcheck)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_schedcheck, cmd_schedcheck)
SHELL_DIAGNOSTICS_WRAP_ARGS(shell_dispatch_cmd_mouse, cmd_mouse)

#undef SHELL_DIAGNOSTICS_WRAP_ARGS
