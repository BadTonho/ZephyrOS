#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_dispatch.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/vfs.h"
#include "fs/storage.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/timer.h"
#include "core/clock.h"
#include "core/tls.h"
#include "core/wait.h"
#include "process/process.h"
#include "drivers/ata.h"
#include "drivers/idt.h"
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
#include "memory/vma.h"
#include "core/string.h"
#include "core/errors.h"
#include "core/crypto.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/workqueue.h"
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
#include "core/wifi_manager.h"
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
#include "drivers/ehci.h"
#include "drivers/pci.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/acpi.h"
#include "drivers/rtc.h"
#include "ui/display.h"
#include "apps/shell_command_utils.h"
#include "apps/shell_runtime.h"
#include "apps/shell_job.h"

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
#define SHELL_NETWORK_REQUIRED_IPV4_HANDLERS 3U
#define SHELL_DNS_NAME_SIZE DNS_NAME_BUFFER_SIZE
#define SHELL_MILLISECONDS_PER_SECOND 1000U
#define SHELL_MAX_TICK_INTERVAL 0xFFFFFFFFU
#define SHELL_NOINLINE __attribute__((noinline))
#define SHELL_UPDATE_SCANCODE_ESCAPE 0x01U
#define SHELL_UPDATE_SCANCODE_F12 0x58U
#define SHELL_REGCHECK_SCANCODE_F11 0x57U
#define SHELL_INDEX_ACTION_SIZE 16U
#define SHELL_FS_DIRECTORY_ATTRIBUTE 0x10U
#define SHELL_LOG_TAIL_DEFAULT 10U
#define SHELL_LOG_TAIL_MAXIMUM 16U
#define SHELL_WHEEL_SCROLL_LINES 3
#define APP_CHECK_DEMO_PATH "DEMO.ZAP"
#define APP_CHECK_DEMO_MAX_CLEANUP 4U
#define APP_INPUT_TEST_PATH "INPUT.ZAP"
#define APP_INPUT_EVENT_OFFSET 128U
#define APP_INPUT_EVENT_DATA1_OFFSET (APP_INPUT_EVENT_OFFSET + 4U)
#define APP_INPUT_TTY_PATH_OFFSET 80U
#define APP_INPUT_TTY_FD_OFFSET 96U
#define APP_CHECK_INVALID_IOCTL 0xFFFFFFFFU
#define APP_INPUT_READ_COUNT_OFFSET (APP_INPUT_EVENT_OFFSET + 4U)
#define SHELL_APP_OUTPUTTEST_FAILURE_CODE 1U
#define SHELL_Q2CHECK_FIRST_FAULT_INDEX 0U
#define SHELL_Q2CHECK_SECOND_FAULT_INDEX 1U
#define SHELL_Q2CHECK_EXPECTED_FAULT_VECTOR 14U
#define SHELL_Q2CHECK_EXPECTED_FAULT_ERROR 4U
#define SHELL_MEMCHECK_BLOCK_A 96U
#define SHELL_MEMCHECK_BLOCK_B 160U
#define SHELL_MEMCHECK_BLOCK_C 224U
#define SHELL_VMA_TEST_MULTI_LENGTH (PAGE_SIZE * 3U + 1U)
#define SHELL_VMA_TEST_MULTI_OFFSET (sizeof(uint32_t))
#define SHELL_VMA_TEST_SENTINEL_ONE 0x5A5A5A5AU
#define SHELL_VMA_TEST_SENTINEL_MULTI 0xA5A5A5A5U
#define SHELL_VMA_TEST_FAILURE_JUMPS 8U
#define SHELL_REGCHECK_NON_ATA_DEVICE_COUNT 7U
#define SHELL_REGCHECK_PCI_NETWORK_CLASS 0x02U
#define SHELL_REGCHECK_ACPI_SDT_HEADER_SIZE 36U
#define SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE (1024U * 1024U)
#define SHELL_REGCHECK_ACPI_MAX_ROOT_ENTRIES 256U
#define SHELL_REGCHECK_ACPI_RSDP_V1_LENGTH 20U
#define SHELL_REGCHECK_ACPI_RSDP_V2_LENGTH 36U
#define SHELL_REGCHECK_ACPI_MAX_RSDP_LENGTH 4096U
#define SHELL_APPCHECK_PHASE_COUNT 8U
#define SHELL_APPCHECK_MAX_FAILURES 16U
#define SHELL_APPCHECK_FAILURE_LABEL_SIZE 40U
#define SHELL_BLKCHECK_PHASE_COUNT 6U
#define SHELL_BLKCHECK_FAILURE_LABEL_SIZE 48U
#define SHELL_BLKCHECK_FILE_NAME "BLK4CHK.BIN"
#define SHELL_BLKCHECK_FILE_SIZE (4U * BLOCK_SECTOR_SIZE)
#define SHELL_BLKCHECK_FAT12_VOLUME "ata1p1"
#define SHELL_BLKCHECK_FAT32_VOLUME "ata1p4"
#define SHELL_BLKCHECK_README "README.TXT"
#define SHELL_BLKCHECK_SAMPLE "DOCS/SAMPLE.TXT"

typedef enum {
    SHELL_Q2CHECK_IDLE = 0,
    SHELL_Q2CHECK_FIRST_FAULT,
    SHELL_Q2CHECK_SECOND_FAULT
} shell_q2check_state_t;

typedef enum {
    SHELL_REGCHECK_IDLE = 0,
    SHELL_REGCHECK_PREPARE_FULL,
    SHELL_REGCHECK_PREPARE_BASE,
    SHELL_REGCHECK_PREPARE_MEMORY,
    SHELL_REGCHECK_PREPARE_PACKAGES,
    SHELL_REGCHECK_PREPARE_THREADS,
    SHELL_REGCHECK_PREPARE_LOADER,
    SHELL_REGCHECK_WAIT_DEMO,
    SHELL_REGCHECK_WAIT_CANCEL
} shell_regcheck_state_t;

typedef enum {
    SHELL_APPCHECK_PHASE_API = 0,
    SHELL_APPCHECK_PHASE_FILES,
    SHELL_APPCHECK_PHASE_PIPES,
    SHELL_APPCHECK_PHASE_PATHS,
    SHELL_APPCHECK_PHASE_DEVICES,
    SHELL_APPCHECK_PHASE_IPC,
    SHELL_APPCHECK_PHASE_LOADER,
    SHELL_APPCHECK_PHASE_VMA,
    SHELL_APPCHECK_PHASE_INVALID
} shell_appcheck_phase_t;

typedef struct {
    char label[SHELL_APPCHECK_FAILURE_LABEL_SIZE];
    int actual;
    int expected;
} shell_appcheck_failure_t;

typedef struct {
    uint8_t compact;
    uint8_t summary_printed;
    uint8_t phase_seen[SHELL_APPCHECK_PHASE_COUNT];
    uint8_t phase_unavailable[SHELL_APPCHECK_PHASE_COUNT];
    uint8_t phase_failed[SHELL_APPCHECK_PHASE_COUNT];
    shell_appcheck_phase_t phase;
    uint32_t failure_count;
    uint32_t stored_failure_count;
    shell_appcheck_failure_t failures[SHELL_APPCHECK_MAX_FAILURES];
} shell_appcheck_summary_t;

typedef enum {
    SHELL_BLKCHECK_PHASE_BASELINE = 0,
    SHELL_BLKCHECK_PHASE_FAILPOINTS,
    SHELL_BLKCHECK_PHASE_CACHE,
    SHELL_BLKCHECK_PHASE_FAT12,
    SHELL_BLKCHECK_PHASE_FAT32,
    SHELL_BLKCHECK_PHASE_SHUTDOWN,
    SHELL_BLKCHECK_PHASE_INVALID
} shell_blkcheck_phase_t;

typedef struct {
    shell_blkcheck_phase_t phase;
    uint8_t phase_ok[SHELL_BLKCHECK_PHASE_COUNT];
    uint8_t active;
    uint8_t scratch_created;
    uint8_t cleanup_pending;
    uint8_t fat32_step;
    uint32_t failure_count;
    char failure[SHELL_BLKCHECK_FAILURE_LABEL_SIZE];
    int result;
    uint32_t baseline_count;
    char baseline_ids[BLOCK_MAX_DEVICES][BLOCK_DEVICE_ID_SIZE];
    uint32_t physical_writes_before;
    uint8_t expected_hash[CRYPTO_SHA256_SIZE];
    uint8_t fat12_readme_hash[CRYPTO_SHA256_SIZE];
    uint8_t fat12_sample_hash[CRYPTO_SHA256_SIZE];
} shell_blkcheck_t;

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
    int block_result;
    int devices_result;
    int usb_result;
    int network_result;
    int wifi_result;
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


static char appcheck_oversized_text[APP_API_MAX_TEXT_SIZE + 1];
static uint8_t appcheck_demo_image[APP_IMAGE_MAX_FILE_SIZE];
static uint8_t appcheck_demo_verify[APP_IMAGE_MAX_FILE_SIZE];
static int shell_waiting_user_test = 0;
static uint32_t shell_appcheck_loader_pid = 0;
static uint32_t shell_appcheck_migration_pid = 0;
static uint32_t shell_appcheck_vma_pid = 0;
static uint32_t shell_appcheck_vma_initial_pages = 0;
static uint32_t shell_appcheck_vma_initial_directories = 0;
static uint32_t shell_appcheck_vma_initial_users = 0;
static uint32_t shell_appcheck_vma_initial_zombies = 0;
static page_fault_stats_t shell_appcheck_vma_initial_faults;
static uint32_t shell_appcheck_fault_pid = 0;
static uint32_t shell_appcheck_user_count = 0;
static uint32_t shell_appcheck_zombie_count = 0;
static shell_builtin_app_t shell_appcheck_migration_app = SHELL_BUILTIN_APP_NONE;
static uint32_t shell_user_test_pid = 0;
static uint8_t shell_checks_cancel_requested = 0;
static uint8_t shell_checks_cancel_started = 0;
static uint32_t shell_checks_job_generation = 0U;
static shell_appcheck_summary_t shell_appcheck_summary;

static char appcheck_oversized_args[APP_LAUNCH_MAX_TEXT + 1U];
static app_launch_info_t appcheck_launch_info;

static shell_q2check_t shell_q2check;
static shell_regcheck_t shell_regcheck;
static shell_blkcheck_t shell_blkcheck;
static uint8_t shell_blkcheck_data[SHELL_BLKCHECK_FILE_SIZE];
static uint8_t shell_blkcheck_verify[SHELL_BLKCHECK_FILE_SIZE];
static storage_dir_entry_t shell_blkcheck_entries[STORAGE_MAX_DIR_ENTRIES];
static storage_long_dir_entry_t
    shell_blkcheck_long_entries[STORAGE_MAX_DIR_ENTRIES];
static const uint8_t shell_blkcheck_expected_readme[CRYPTO_SHA256_SIZE] = {
    0xD1U, 0x03U, 0x5AU, 0xD8U, 0x1CU, 0x44U, 0x94U, 0x89U,
    0x79U, 0x77U, 0xD2U, 0xD3U, 0x11U, 0x02U, 0x35U, 0x8DU,
    0x3CU, 0xD2U, 0x60U, 0x97U, 0x77U, 0xF0U, 0xE2U, 0xB4U,
    0xFBU, 0x04U, 0xDCU, 0x35U, 0xC3U, 0xC9U, 0xE2U, 0xAFU
};
static const uint8_t shell_blkcheck_expected_sample[CRYPTO_SHA256_SIZE] = {
    0xB9U, 0x85U, 0xAFU, 0xFBU, 0x30U, 0xDFU, 0xEDU, 0x8CU,
    0x58U, 0x4DU, 0x93U, 0x84U, 0x8EU, 0x25U, 0x2BU, 0xB0U,
    0x81U, 0xB6U, 0xF2U, 0x8CU, 0xB2U, 0xBCU, 0xE5U, 0x6FU,
    0xC3U, 0x86U, 0x6CU, 0x7EU, 0x98U, 0xBAU, 0x33U, 0x2FU
};

static void shell_regcheck_reset(void);
static shell_job_step_result_t shell_regcheck_prepare_step(
    shell_job_context_t* context);
static shell_job_step_result_t shell_blkcheck_job_step(
    shell_job_context_t* context);
static int shell_blkcheck_job_cancel(shell_job_context_t* context);
static void shell_blkcheck_job_finish(shell_job_context_t* context,
                                      shell_job_state_t state, int result);
static shell_job_step_result_t shell_blkcheck_job_drain(
    shell_job_context_t* context);

static const char* shell_appcheck_phase_name(shell_appcheck_phase_t phase) {
    switch (phase) {
        case SHELL_APPCHECK_PHASE_API: return "api";
        case SHELL_APPCHECK_PHASE_FILES: return "arquivos";
        case SHELL_APPCHECK_PHASE_PIPES: return "pipes";
        case SHELL_APPCHECK_PHASE_PATHS: return "caminhos";
        case SHELL_APPCHECK_PHASE_DEVICES: return "dispositivos";
        case SHELL_APPCHECK_PHASE_IPC: return "ipc";
        case SHELL_APPCHECK_PHASE_LOADER: return "loader";
        case SHELL_APPCHECK_PHASE_VMA: return "vma/pagefault";
        default: return "desconhecida";
    }
}

static void shell_appcheck_summary_reset(uint8_t compact) {
    kmemset(&shell_appcheck_summary, 0, sizeof(shell_appcheck_summary));
    shell_appcheck_summary.compact = compact;
    shell_appcheck_summary.phase = SHELL_APPCHECK_PHASE_API;
}

static void shell_appcheck_set_phase(shell_appcheck_phase_t phase) {
    if (phase < SHELL_APPCHECK_PHASE_COUNT) {
        shell_appcheck_summary.phase = phase;
    }
}

static void shell_appcheck_copy_failure_label(char* destination,
                                               const char* source) {
    uint32_t index = 0U;

    if (!destination) return;
    if (!source) source = "desconhecido";
    while (source[index] && index + 1U < SHELL_APPCHECK_FAILURE_LABEL_SIZE) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void shell_appcheck_record_result(const char* label, int actual,
                                         int expected, uint8_t unavailable) {
    shell_appcheck_failure_t* failure;

    if (!shell_appcheck_summary.compact ||
        shell_appcheck_summary.phase >= SHELL_APPCHECK_PHASE_COUNT) {
        return;
    }
    shell_appcheck_summary.phase_seen[shell_appcheck_summary.phase] = 1U;
    if (unavailable && actual == expected) {
        shell_appcheck_summary.phase_unavailable[shell_appcheck_summary.phase] =
            1U;
        return;
    }
    if (actual == expected) return;

    shell_appcheck_summary.phase_failed[shell_appcheck_summary.phase] = 1U;
    shell_appcheck_summary.failure_count++;
    if (shell_appcheck_summary.stored_failure_count >=
        SHELL_APPCHECK_MAX_FAILURES) {
        return;
    }
    failure = &shell_appcheck_summary.failures[
        shell_appcheck_summary.stored_failure_count++];
    shell_appcheck_copy_failure_label(failure->label, label);
    failure->actual = actual;
    failure->expected = expected;
}

static void shell_appcheck_print_summary(int terminal_result) {
    shell_appcheck_phase_t phase;
    int summary_result;

    if (!shell_appcheck_summary.compact ||
        shell_appcheck_summary.summary_printed) {
        return;
    }
    shell_appcheck_summary.summary_printed = 1U;
    video_print("AppCheck compacto:\n", 0x0B);
    for (phase = SHELL_APPCHECK_PHASE_API;
         phase < SHELL_APPCHECK_PHASE_COUNT; phase++) {
        video_print("  ", 0x07);
        video_print(shell_appcheck_phase_name(phase), 0x07);
        if (!shell_appcheck_summary.phase_seen[phase]) {
            video_print(" PENDENTE\n", 0x0E);
        } else if (shell_appcheck_summary.phase_failed[phase]) {
            video_print(" ERRO\n", 0x0C);
        } else if (shell_appcheck_summary.phase_unavailable[phase]) {
            video_print(" INDISPONIVEL\n", 0x0E);
        } else {
            video_print(" OK\n", 0x0A);
        }
    }
    video_print("  falhas=", 0x07);
    shell_command_print_num(shell_appcheck_summary.failure_count);
    video_print("\n", 0x07);
    for (uint32_t index = 0U;
         index < shell_appcheck_summary.stored_failure_count; index++) {
        shell_appcheck_failure_t* failure =
            &shell_appcheck_summary.failures[index];
        video_print("    ", 0x07);
        video_print(failure->label, 0x0C);
        video_print(" retorno=", 0x08);
        shell_command_print_num((uint32_t)failure->actual);
        video_print(" esperado=", 0x08);
        shell_command_print_num((uint32_t)failure->expected);
        video_print("\n", 0x0C);
    }
    summary_result = terminal_result != OK ||
                     shell_appcheck_summary.failure_count != 0U;
    video_print("  resultado=", 0x07);
    if (terminal_result == ERR_CANCELLED) {
        video_print("CANCELADO\n", 0x0E);
    } else {
        video_print(summary_result ? "ERRO\n" : "OK\n",
                    summary_result ? 0x0C : 0x0A);
    }
}

static void shell_appcheck_summary_finish(int terminal_result) {
    shell_appcheck_print_summary(terminal_result);
    shell_appcheck_summary.compact = 0U;
}

static int shell_appcheck_is_compact_job(
    const shell_job_context_t* context) {
    return shell_appcheck_summary.compact && context &&
           kstrcmp(context->arguments, "appcheck compact") == 0;
}

static int shell_appcheck_has_pending_work(void) {
    return shell_appcheck_loader_pid != 0U ||
           shell_appcheck_migration_pid != 0U ||
           shell_appcheck_vma_pid != 0U ||
           shell_appcheck_fault_pid != 0U;
}

static void cmd_appcheck_print_result(const char* label, int result);
static void cmd_appcheck_print_expected_result(const char* label, int actual,
                                               int expected);
static void cmd_appcheck_print_result_with_expectation(const char* label,
                                                        int actual,
                                                        int expected);

static const char* shell_blkcheck_phase_name(shell_blkcheck_phase_t phase) {
    switch (phase) {
        case SHELL_BLKCHECK_PHASE_BASELINE: return "baseline";
        case SHELL_BLKCHECK_PHASE_FAILPOINTS: return "failpoints";
        case SHELL_BLKCHECK_PHASE_CACHE: return "cache";
        case SHELL_BLKCHECK_PHASE_FAT12: return "FAT12";
        case SHELL_BLKCHECK_PHASE_FAT32: return "FAT32";
        case SHELL_BLKCHECK_PHASE_SHUTDOWN: return "shutdown";
        default: return "desconhecida";
    }
}

static const char* shell_blkcheck_case_name(shell_blkcheck_phase_t phase) {
    switch (phase) {
        case SHELL_BLKCHECK_PHASE_BASELINE: return "fixtures/inventario";
        case SHELL_BLKCHECK_PHASE_FAILPOINTS: return "injecao one-shot";
        case SHELL_BLKCHECK_PHASE_CACHE: return "hash/LRU/dirty";
        case SHELL_BLKCHECK_PHASE_FAT12: return "listagem/leitura/hash";
        case SHELL_BLKCHECK_PHASE_FAT32: return "writeback/sync/limpeza";
        case SHELL_BLKCHECK_PHASE_SHUTDOWN: return "sync seguro";
        default: return "estado";
    }
}

static void shell_blkcheck_copy_failure(const char* source) {
    uint32_t index = 0U;

    if (!source) source = "desconhecido";
    while (source[index] && index + 1U < SHELL_BLKCHECK_FAILURE_LABEL_SIZE) {
        shell_blkcheck.failure[index] = source[index];
        index++;
    }
    shell_blkcheck.failure[index] = '\0';
}

static void shell_blkcheck_fail(const char* label, int result) {
    if (!shell_blkcheck.failure_count) shell_blkcheck_copy_failure(label);
    shell_blkcheck.failure_count++;
    shell_blkcheck.result = result;
}

static void shell_blkcheck_copy_id(char* destination, const char* source) {
    uint32_t index = 0U;

    if (!destination) return;
    if (!source) source = "";
    while (source[index] && index + 1U < BLOCK_DEVICE_ID_SIZE) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int shell_blkcheck_save_inventory(void) {
    uint32_t count;

    if (block_get_count(&count) != OK || count > BLOCK_MAX_DEVICES) {
        LOG_ERROR("SHELL", "Falha ao salvar inventario do BLKCheck");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < count; index++) {
        block_device_t device;

        if (block_get_at(index, &device) != OK) {
            LOG_ERROR("SHELL", "Falha ao copiar inventario do BLKCheck");
            return ERR_STATE;
        }
        shell_blkcheck_copy_id(shell_blkcheck.baseline_ids[index], device.id);
    }
    shell_blkcheck.baseline_count = count;
    return OK;
}

static int shell_blkcheck_inventory_unchanged(void) {
    uint32_t count;

    if (block_get_count(&count) != OK || count != shell_blkcheck.baseline_count) {
        LOG_ERROR("SHELL", "Inventario mudou durante o BLKCheck");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < count; index++) {
        block_device_t device;

        if (block_get_at(index, &device) != OK) {
            LOG_ERROR("SHELL", "Falha ao reler inventario do BLKCheck");
            return ERR_STATE;
        }
        if (kstrcmp(shell_blkcheck.baseline_ids[index], device.id) != 0) {
            LOG_ERROR("SHELL", "ID mudou durante o BLKCheck");
            return ERR_STATE;
        }
    }
    return OK;
}

static int shell_blkcheck_hash_file(const char* volume_id, const char* path,
                                    uint8_t* buffer, uint32_t capacity,
                                    uint8_t hash[CRYPTO_SHA256_SIZE],
                                    uint32_t* out_size) {
    uint32_t size;
    uint32_t read_size;
    int result;

    if (!volume_id || !path || !buffer || !hash || !out_size) {
        LOG_ERROR("SHELL", "Argumento nulo no hash do BLKCheck");
        return ERR_NULL;
    }
    result = storage_get_file_info(volume_id, path, &size, 0);
    if (result != OK) {
        LOG_ERROR("SHELL", "Arquivo conhecido ausente no BLKCheck");
        return result;
    }
    if (!size || size > capacity) {
        LOG_ERROR("SHELL", "Arquivo excede o buffer do BLKCheck");
        return ERR_OVERFLOW;
    }
    result = storage_read_file_range(volume_id, path, 0U, buffer, size,
                                     &read_size);
    if (result != OK || read_size != size) {
        LOG_ERROR("SHELL", "Falha ao ler arquivo do BLKCheck");
        return result == OK ? ERR_DISK : result;
    }
    result = crypto_sha256(buffer, size, hash);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao calcular hash do BLKCheck");
        return result;
    }
    *out_size = size;
    return OK;
}

static int shell_blkcheck_prepare_fixture(const char* id,
                                           storage_fs_type_t expected_fs,
                                           uint8_t writable) {
    storage_volume_t volume;
    int result;

    result = storage_find_volume(id, &volume);
    if (result != OK || volume.fs_type != expected_fs) {
        LOG_WARN("SHELL", "Fixture do BLKCheck indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (volume.state == STORAGE_VOLUME_INVALID ||
        volume.state == STORAGE_VOLUME_UNSUPPORTED) {
        LOG_WARN("SHELL", "Fixture do BLKCheck rejeitada pelo filesystem");
        return ERR_UNAVAILABLE;
    }
    if (!volume.mounted) {
        result = storage_mount(id);
        if (result != OK) {
            LOG_WARN("SHELL", "Montagem da fixture do BLKCheck falhou");
            return result;
        }
    }
    result = storage_find_volume(id, &volume);
    if (result != OK || !volume.mounted) {
        LOG_ERROR("SHELL", "Fixture do BLKCheck nao foi montada");
        return ERR_STATE;
    }
    if (writable && volume.read_only) {
        LOG_WARN("SHELL", "Fixture FAT32 do BLKCheck somente leitura");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static int shell_blkcheck_validate_fixture(void) {
    uint32_t count;
    int result;

    result = shell_blkcheck_prepare_fixture(
        SHELL_BLKCHECK_FAT12_VOLUME, STORAGE_FS_FAT12, 0U);
    if (result != OK) return result;
    result = shell_blkcheck_prepare_fixture(
        SHELL_BLKCHECK_FAT32_VOLUME, STORAGE_FS_FAT32, 1U);
    if (result != OK) return result;
    result = storage_list_dir_long(
        SHELL_BLKCHECK_FAT32_VOLUME, "", shell_blkcheck_long_entries,
        STORAGE_MAX_DIR_ENTRIES, &count);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao listar fixture FAT32 do BLKCheck");
        return result;
    }
    for (uint32_t index = 0U; index < count; index++) {
        if (kstrcmp(shell_blkcheck_long_entries[index].name,
                    SHELL_BLKCHECK_FILE_NAME) == 0 ||
            kstrcmp(shell_blkcheck_long_entries[index].short_name,
                    SHELL_BLKCHECK_FILE_NAME) == 0) {
            LOG_WARN("SHELL", "Arquivo reservado do BLKCheck ja existe");
            return ERR_STATE;
        }
    }
    return OK;
}

static int shell_blkcheck_fat12(void) {
    uint32_t count;
    uint32_t readme_size;
    uint32_t sample_size;
    uint32_t repeat_size;
    uint8_t repeat_hash[CRYPTO_SHA256_SIZE];
    int result;

    result = storage_list_dir(SHELL_BLKCHECK_FAT12_VOLUME, "",
                              shell_blkcheck_entries, STORAGE_MAX_DIR_ENTRIES,
                              &count);
    if (result != OK || count < 2U) {
        LOG_ERROR("SHELL", "Listagem FAT12 do BLKCheck falhou");
        return result == OK ? ERR_STATE : result;
    }
    result = shell_blkcheck_hash_file(
        SHELL_BLKCHECK_FAT12_VOLUME, SHELL_BLKCHECK_README,
        shell_blkcheck_verify, sizeof(shell_blkcheck_verify),
        shell_blkcheck.fat12_readme_hash, &readme_size);
    if (result != OK) return result;
    result = shell_blkcheck_hash_file(
        SHELL_BLKCHECK_FAT12_VOLUME, SHELL_BLKCHECK_SAMPLE,
        shell_blkcheck_verify, sizeof(shell_blkcheck_verify),
        shell_blkcheck.fat12_sample_hash, &sample_size);
    if (result != OK) return result;
    if (!crypto_equal(shell_blkcheck.fat12_readme_hash,
                      shell_blkcheck_expected_readme, CRYPTO_SHA256_SIZE) ||
        !crypto_equal(shell_blkcheck.fat12_sample_hash,
                      shell_blkcheck_expected_sample, CRYPTO_SHA256_SIZE)) {
        LOG_ERROR("SHELL", "Fixture FAT12 nao corresponde ao hash conhecido");
        return ERR_STATE;
    }
    result = shell_blkcheck_hash_file(
        SHELL_BLKCHECK_FAT12_VOLUME, SHELL_BLKCHECK_README,
        shell_blkcheck_verify, sizeof(shell_blkcheck_verify), repeat_hash,
        &repeat_size);
    if (result != OK || readme_size != repeat_size ||
        !crypto_equal(shell_blkcheck.fat12_readme_hash, repeat_hash,
                      CRYPTO_SHA256_SIZE) || !sample_size) {
        LOG_ERROR("SHELL", "Hash FAT12 do BLKCheck divergente");
        return result == OK ? ERR_STATE : result;
    }
    result = shell_blkcheck_hash_file(
        SHELL_BLKCHECK_FAT12_VOLUME, SHELL_BLKCHECK_SAMPLE,
        shell_blkcheck_verify, sizeof(shell_blkcheck_verify), repeat_hash,
        &repeat_size);
    if (result != OK || sample_size != repeat_size ||
        !crypto_equal(shell_blkcheck.fat12_sample_hash, repeat_hash,
                      CRYPTO_SHA256_SIZE)) {
        LOG_ERROR("SHELL", "Hash do arquivo interno FAT12 divergente");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_blkcheck_fat32_step(void) {
    block_cache_stats_t stats;
    uint8_t actual_hash[CRYPTO_SHA256_SIZE];
    uint32_t size;
    int result;

    if (shell_blkcheck.fat32_step == 0U) {
        if (block_cache_get_stats(&stats) != OK) {
            LOG_ERROR("SHELL", "Metricas fisicas indisponiveis no BLKCheck");
            return ERR_STATE;
        }
        shell_blkcheck.physical_writes_before = stats.physical_writes;
        for (uint32_t index = 0U; index < SHELL_BLKCHECK_FILE_SIZE; index++) {
            shell_blkcheck_data[index] = (uint8_t)((index * 37U + 11U) & 0xFFU);
        }
        result = crypto_sha256(shell_blkcheck_data, SHELL_BLKCHECK_FILE_SIZE,
                               shell_blkcheck.expected_hash);
        if (result == OK) {
            result = storage_write_file(
                SHELL_BLKCHECK_FAT32_VOLUME, SHELL_BLKCHECK_FILE_NAME,
                shell_blkcheck_data, SHELL_BLKCHECK_FILE_SIZE, 0x20U);
        }
        if (result != OK) {
            LOG_ERROR("SHELL", "Escrita FAT32 do BLKCheck falhou");
            return result;
        }
        shell_blkcheck.scratch_created = 1U;
        shell_blkcheck.fat32_step = 1U;
    }
    if (shell_blkcheck.fat32_step == 1U) {
        result = shell_blkcheck_hash_file(
            SHELL_BLKCHECK_FAT32_VOLUME, SHELL_BLKCHECK_FILE_NAME,
            shell_blkcheck_verify, sizeof(shell_blkcheck_verify), actual_hash,
            &size);
        if (block_cache_get_stats(&stats) != OK ||
            stats.physical_writes != shell_blkcheck.physical_writes_before ||
            result != OK || size != SHELL_BLKCHECK_FILE_SIZE ||
            !crypto_equal(actual_hash, shell_blkcheck.expected_hash,
                          CRYPTO_SHA256_SIZE)) {
            LOG_ERROR("SHELL", "Writeback antecipado no BLKCheck");
            return result == OK ? ERR_STATE : result;
        }
        shell_blkcheck.fat32_step = 2U;
        return OK;
    }
    if (shell_blkcheck.fat32_step == 2U) {
        result = storage_sync_volume(SHELL_BLKCHECK_FAT32_VOLUME);
        if (result == OK) result = block_cache_get_stats(&stats);
        if (result != OK ||
            stats.physical_writes <= shell_blkcheck.physical_writes_before) {
            LOG_ERROR("SHELL", "Sync FAT32 do BLKCheck falhou");
            return result == OK ? ERR_STATE : result;
        }
        shell_blkcheck.fat32_step = 3U;
        return OK;
    }
    if (shell_blkcheck.fat32_step == 3U) {
        result = shell_blkcheck_hash_file(
            SHELL_BLKCHECK_FAT32_VOLUME, SHELL_BLKCHECK_FILE_NAME,
            shell_blkcheck_verify, sizeof(shell_blkcheck_verify), actual_hash,
            &size);
        if (result != OK || size != SHELL_BLKCHECK_FILE_SIZE ||
            !crypto_equal(actual_hash, shell_blkcheck.expected_hash,
                          CRYPTO_SHA256_SIZE)) {
            LOG_ERROR("SHELL", "Hash FAT32 do BLKCheck divergente");
            return result == OK ? ERR_STATE : result;
        }
        shell_blkcheck.fat32_step = 4U;
        return OK;
    }
    if (shell_blkcheck.fat32_step == 4U) {
        result = storage_delete_file(SHELL_BLKCHECK_FAT32_VOLUME,
                                     SHELL_BLKCHECK_FILE_NAME);
        if (result != OK) {
            LOG_ERROR("SHELL", "Limpeza FAT32 do BLKCheck falhou");
            return result;
        }
        shell_blkcheck.scratch_created = 0U;
        shell_blkcheck.cleanup_pending = 1U;
        shell_blkcheck.fat32_step = 5U;
        return OK;
    }
    if (shell_blkcheck.fat32_step == 5U) {
        result = storage_sync_volume(SHELL_BLKCHECK_FAT32_VOLUME);
        if (result != OK) {
            LOG_ERROR("SHELL", "Sync final FAT32 do BLKCheck falhou");
            return result;
        }
        shell_blkcheck.cleanup_pending = 0U;
        shell_blkcheck.fat32_step = 6U;
        return OK;
    }
    result = storage_check(SHELL_BLKCHECK_FAT32_VOLUME);
    if (result != OK) {
        LOG_ERROR("SHELL", "Consistencia FAT32 do BLKCheck falhou");
    } else {
        shell_blkcheck.fat32_step = 7U;
    }
    return result;
}

static int shell_blkcheck_cleanup(void) {
    int result;

    if (shell_blkcheck.scratch_created) {
        result = storage_delete_file(SHELL_BLKCHECK_FAT32_VOLUME,
                                     SHELL_BLKCHECK_FILE_NAME);
        if (result != OK) {
            LOG_ERROR("SHELL", "Falha ao remover scratch durante drenagem");
            return result;
        }
        shell_blkcheck.scratch_created = 0U;
        shell_blkcheck.cleanup_pending = 1U;
    }
    if (shell_blkcheck.cleanup_pending) {
        result = storage_sync_volume(SHELL_BLKCHECK_FAT32_VOLUME);
        if (result != OK) {
            LOG_ERROR("SHELL", "Falha ao sincronizar scratch na drenagem");
            return result;
        }
        shell_blkcheck.cleanup_pending = 0U;
    }
    return OK;
}

static shell_job_step_result_t shell_blkcheck_job_step(
    shell_job_context_t* context) {
    int result = OK;

    if (!context) {
        LOG_ERROR("SHELL", "Contexto nulo no job BLKCheck");
        return SHELL_JOB_STEP_FAILED;
    }
    if (context->cancel_requested) {
        context->last_error = ERR_CANCELLED;
        return SHELL_JOB_STEP_CANCELLED;
    }
    shell_job_set_phase(context,
                        shell_blkcheck_phase_name(shell_blkcheck.phase));
    switch (shell_blkcheck.phase) {
        case SHELL_BLKCHECK_PHASE_BASELINE:
            result = shell_blkcheck_save_inventory();
            if (result == OK) result = shell_blkcheck_validate_fixture();
            break;
        case SHELL_BLKCHECK_PHASE_FAILPOINTS:
            result = block_self_test();
            break;
        case SHELL_BLKCHECK_PHASE_CACHE:
            result = block_cache_validate_state();
            break;
        case SHELL_BLKCHECK_PHASE_FAT12:
            result = shell_blkcheck_fat12();
            break;
        case SHELL_BLKCHECK_PHASE_FAT32:
            result = shell_blkcheck_fat32_step();
            if (result == OK && shell_blkcheck.fat32_step < 7U) {
                return SHELL_JOB_STEP_PENDING;
            }
            break;
        case SHELL_BLKCHECK_PHASE_SHUTDOWN:
            result = power_shutdown_prepare();
            if (result == OK) {
                block_cache_stats_t stats;
                block_queue_stats_t queue;

                result = block_cache_get_stats(&stats);
                if (result == OK && (stats.dirty_entries ||
                                     stats.writeback_entries)) result = ERR_STATE;
                if (result == OK) result = block_get_stats(&queue);
                if (result == OK && (queue.queue_depth || queue.in_flight)) {
                    result = ERR_STATE;
                }
                if (result == OK) result = shell_blkcheck_inventory_unchanged();
            }
            break;
        default:
            result = ERR_STATE;
            break;
    }
    if (result != OK) {
        int cleanup_result;

        shell_blkcheck_fail(shell_blkcheck_case_name(shell_blkcheck.phase),
                            result);
        cleanup_result = shell_blkcheck_cleanup();
        if (cleanup_result != OK) {
            shell_blkcheck.result = cleanup_result;
            result = cleanup_result;
        }
        context->last_error = result;
        return SHELL_JOB_STEP_FAILED;
    }
    shell_blkcheck.phase_ok[shell_blkcheck.phase] = 1U;
    if (shell_blkcheck.phase == SHELL_BLKCHECK_PHASE_SHUTDOWN) {
        context->last_error = OK;
        return SHELL_JOB_STEP_COMPLETE;
    }
    shell_blkcheck.phase++;
    shell_blkcheck.fat32_step = 0U;
    return SHELL_JOB_STEP_PENDING;
}

static int shell_blkcheck_job_cancel(shell_job_context_t* context) {
    (void)context;
    shell_blkcheck.result = ERR_CANCELLED;
    return OK;
}

static void shell_blkcheck_job_finish(shell_job_context_t* context,
                                      shell_job_state_t state, int result) {
    (void)context;
    shell_blkcheck.active = 0U;
    if (state == SHELL_JOB_STATE_SUCCEEDED && result == OK &&
        !shell_blkcheck.failure_count) {
        video_print("BLKCheck:\n", 0x0B);
        for (uint32_t index = 0U; index < SHELL_BLKCHECK_PHASE_COUNT; index++) {
            video_print("  ", 0x07);
            video_print(shell_blkcheck_phase_name(
                            (shell_blkcheck_phase_t)index), 0x07);
            video_print(" OK\n", 0x0A);
        }
        video_print("  resultado OK\n", 0x0A);
        return;
    }
    video_print("BLKCheck: ", 0x0C);
    video_print(shell_blkcheck_phase_name(shell_blkcheck.phase), 0x0C);
    video_print(" ", 0x07);
    video_print(shell_blkcheck.failure[0] ? shell_blkcheck.failure :
                                             "cancelado", 0x0C);
    video_print(" codigo=", 0x07);
    shell_command_print_num((uint32_t)(shell_blkcheck.result == OK ?
                                       result : shell_blkcheck.result));
    video_print("\n", 0x0C);
}

static shell_job_step_result_t shell_blkcheck_job_drain(
    shell_job_context_t* context) {
    int result = shell_blkcheck_cleanup();

    if (result != OK) {
        if (context) context->last_error = result;
        return SHELL_JOB_STEP_FAILED;
    }
    return SHELL_JOB_STEP_COMPLETE;
}

static const shell_job_definition_t shell_blkcheck_job_definition = {
    "blkcheck", SHELL_JOB_KIND_CHECK, shell_blkcheck_job_step,
    shell_blkcheck_job_cancel, shell_blkcheck_job_finish,
    shell_blkcheck_job_drain
};

static int shell_regcheck_is_preparing(void) {
    return shell_regcheck.state >= SHELL_REGCHECK_PREPARE_FULL &&
           shell_regcheck.state <= SHELL_REGCHECK_PREPARE_LOADER;
}

static shell_job_step_result_t shell_checks_job_step(
    shell_job_context_t* context) {
    int active;
    int result;

    if (!context) return SHELL_JOB_STEP_FAILED;
    if (context->cancel_requested) {
        if (shell_regcheck_is_preparing()) {
            shell_regcheck_reset();
            shell_runtime_reset_input();
            context->last_error = ERR_TIMEOUT;
            return SHELL_JOB_STEP_CANCELLED;
        }
        if (!shell_checks_cancel_started) {
            shell_checks_cancel_started = 1;
            shell_checks_cancel_requested = 1;
            if (shell_user_test_pid != 0U) {
                result = process_cancel_user_test(shell_user_test_pid,
                                                  PROCESS_EXIT_CANCELLED);
            } else if (app_loader_is_foreground_active()) {
                result = app_loader_cancel_foreground(PROCESS_EXIT_CANCELLED);
            } else {
                result = ERR_NOT_FOUND;
            }
            if (result != OK && result != ERR_NOT_FOUND) {
                context->last_error = result;
                return SHELL_JOB_STEP_FAILED;
            }
        }
        shell_job_set_phase(context, "cancelando");
        active = shell_q2check.state != SHELL_Q2CHECK_IDLE ||
                 shell_regcheck.state != SHELL_REGCHECK_IDLE ||
                 shell_appcheck_loader_pid != 0U ||
                 shell_appcheck_migration_pid != 0U ||
                 shell_appcheck_vma_pid != 0U ||
                 shell_appcheck_fault_pid != 0U ||
                 shell_waiting_user_test;
        if (active) return SHELL_JOB_STEP_PENDING;
        context->last_error = ERR_TIMEOUT;
        return SHELL_JOB_STEP_CANCELLED;
    }
    if (shell_regcheck_is_preparing()) {
        return shell_regcheck_prepare_step(context);
    }
    active = shell_q2check.state != SHELL_Q2CHECK_IDLE ||
             shell_regcheck.state != SHELL_REGCHECK_IDLE ||
             shell_appcheck_loader_pid != 0U ||
             shell_appcheck_migration_pid != 0U ||
             shell_appcheck_vma_pid != 0U ||
             shell_appcheck_fault_pid != 0U ||
             shell_waiting_user_test;
    shell_job_set_phase(context, active ? "aguardando resultado" : "finalizado");
    if (active) return SHELL_JOB_STEP_PENDING;
    context->last_error = OK;
    return SHELL_JOB_STEP_COMPLETE;
}

static void shell_checks_job_finish(shell_job_context_t* context,
                                    shell_job_state_t state, int result) {
    shell_checks_cancel_requested = 0;
    shell_checks_cancel_started = 0;
    if (state == SHELL_JOB_STATE_CANCELLED) {
        video_print("Diagnostico cooperativo cancelado.\n", 0x0E);
    } else if (state != SHELL_JOB_STATE_SUCCEEDED) {
        video_print("Diagnostico cooperativo terminou com erro; codigo=", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print("\n", 0x0C);
    }
    if (shell_appcheck_is_compact_job(context)) {
        shell_appcheck_summary_finish(state == SHELL_JOB_STATE_CANCELLED ?
                                      ERR_CANCELLED : result);
    }
}

static shell_job_step_result_t shell_checks_job_drain(
    shell_job_context_t* context) {
    int active;

    (void)context;
    active = shell_q2check.state != SHELL_Q2CHECK_IDLE ||
             shell_regcheck.state != SHELL_REGCHECK_IDLE ||
             shell_appcheck_loader_pid != 0U ||
             shell_appcheck_migration_pid != 0U ||
             shell_appcheck_vma_pid != 0U ||
             shell_appcheck_fault_pid != 0U ||
             shell_waiting_user_test;
    return active ? SHELL_JOB_STEP_PENDING : SHELL_JOB_STEP_COMPLETE;
}

static const shell_job_definition_t shell_checks_job_definition = {
    "checks", SHELL_JOB_KIND_CHECK, shell_checks_job_step, NULL,
    shell_checks_job_finish, shell_checks_job_drain
};

static const char app_input_test_message[] =
    "Entrada ZAPP ativa: Enter encerra; Ctrl+C interrompe; F12 cancela.\n";
static void shell_demo_patch_u32(uint8_t* code, uint32_t offset,
                                 uint32_t value) {
    code[offset + 0] = (uint8_t)(value & 0xFFU);
    code[offset + 1] = (uint8_t)((value >> 8) & 0xFFU);
    code[offset + 2] = (uint8_t)((value >> 16) & 0xFFU);
    code[offset + 3] = (uint8_t)((value >> 24) & 0xFFU);
}

static void shell_demo_emit_mov(uint8_t* code, uint32_t* offset,
                                uint8_t reg, uint32_t value) {
    code[(*offset)++] = (uint8_t)(0xB8U + reg);
    shell_demo_patch_u32(code, *offset, value);
    *offset += 4;
}

static int shell_demo_emit_jne(uint8_t* code, uint32_t* offset,
                               uint32_t target) {
    int32_t relative = (int32_t)target - (int32_t)(*offset + 2U);

    if (relative < -128 || relative > 127) {
        LOG_ERROR("SHELL", "Desvio do aplicativo de entrada excede limite");
        return ERR_OVERFLOW;
    }
    code[(*offset)++] = 0x75;
    code[(*offset)++] = (uint8_t)relative;
    return OK;
}

static uint32_t shell_demo_emit_jne_near(uint8_t* code, uint32_t* offset) {
    uint32_t patch_offset = *offset + 2U;

    code[(*offset)++] = 0x0FU;
    code[(*offset)++] = 0x85U;
    *offset += 4U;
    return patch_offset;
}

static void shell_demo_emit_load_ebx(uint8_t* code, uint32_t* offset,
                                     uint32_t address) {
    code[(*offset)++] = 0x8BU;
    code[(*offset)++] = 0x1DU;
    shell_demo_patch_u32(code, *offset, address);
    *offset += 4U;
}

static void shell_demo_emit_exit_on_error(uint8_t* code, uint32_t* offset) {
    code[(*offset)++] = 0x85U;
    code[(*offset)++] = 0xC0U;
    code[(*offset)++] = 0x74U;
    code[(*offset)++] = 10U;
    code[(*offset)++] = 0x89U;
    code[(*offset)++] = 0xC3U;
    shell_demo_emit_mov(code, offset, 0U, APP_SYSCALL_PROCESS_EXIT);
    code[(*offset)++] = 0xCDU;
    code[(*offset)++] = 0x80U;
    code[(*offset)++] = 0xF4U;
}

static void shell_demo_emit_syscall(uint8_t* code, uint32_t* offset) {
    code[(*offset)++] = 0xCDU;
    code[(*offset)++] = 0x80U;
}

static void shell_demo_emit_add_ebx(uint8_t* code, uint32_t* offset,
                                    uint32_t value) {
    code[(*offset)++] = 0x81U;
    code[(*offset)++] = 0xC3U;
    shell_demo_patch_u32(code, *offset, value);
    *offset += 4U;
}

static void shell_remove_image(const char* path) {
    uint32_t attempts = 0;

    if (!path) {
        LOG_ERROR("SHELL", "Caminho nulo ao remover imagem temporaria");
        return;
    }
    /* A FAT12 legada pode manter uma entrada de uma validacao interrompida. */
    while (attempts < APP_CHECK_DEMO_MAX_CLEANUP &&
           fs_delete_file(path) == OK) {
        attempts++;
    }
}

static int shell_verify_image(const char* path, uint32_t expected_size) {
    int read_size;

    if (!path || expected_size == 0 ||
        expected_size > sizeof(appcheck_demo_image)) {
        LOG_ERROR("SHELL", "Tamanho invalido na verificacao do ZAPP demo");
        return ERR_INVALID;
    }

    read_size = fs_read_file(path, appcheck_demo_verify,
                             sizeof(appcheck_demo_verify));
    if (read_size < 0) {
        LOG_ERROR("SHELL", "Falha ao reler ZAPP demo apos gravacao");
        return ERR_DISK;
    }
    if ((uint32_t)read_size != expected_size) {
        LOG_ERROR("SHELL", "Tamanho do ZAPP demo divergiu apos gravacao");
        return ERR_STATE;
    }

    for (uint32_t i = 0; i < expected_size; i++) {
        if (appcheck_demo_verify[i] != appcheck_demo_image[i]) {
            LOG_ERROR("SHELL", "Conteudo do ZAPP demo divergiu apos gravacao");
            return ERR_STATE;
        }
    }
    return OK;
}

static uint32_t shell_build_demo_image(void) {
    app_image_header_t header;
    uint8_t* code = appcheck_demo_image + APP_IMAGE_HEADER_SIZE;
    uint32_t offset = 0;

    kmemset(appcheck_demo_image, 0, sizeof(appcheck_demo_image));
    /* O demonstrativo exercita o caminho completo do carregador sem
       depender de I/O adicional durante o diagnostico do Shell. */
    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_PROCESS_EXIT);
    code[offset++] = 0x31;
    code[offset++] = 0xDB;
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    code[offset++] = 0xF4;

    kmemcpy(header.magic, "ZAPP", 4);
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = offset;
    header.data_offset = APP_IMAGE_HEADER_SIZE + offset;
    header.data_size = 0;
    header.entry_offset = 0;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    return header.data_offset;
}

static uint32_t shell_build_vma_test_image(void) {
    app_image_header_t header;
    uint8_t* code = appcheck_demo_image + APP_IMAGE_HEADER_SIZE;
    uint32_t failure_jumps[SHELL_VMA_TEST_FAILURE_JUMPS];
    uint32_t failure_count = 0U;
    uint32_t failure_offset;
    uint32_t offset = 0U;

    kmemset(appcheck_demo_image, 0, sizeof(appcheck_demo_image));

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MMAP);
    shell_demo_emit_mov(code, &offset, 3U, PAGE_SIZE);
    shell_demo_emit_mov(code, &offset, 1U,
                        APP_MMAP_PROT_READ | APP_MMAP_PROT_WRITE);
    shell_demo_emit_mov(code, &offset, 2U, APP_MMAP_FLAG_ANONYMOUS);
    shell_demo_emit_mov(code, &offset, 6U, USER_DATA_BASE);
    shell_demo_emit_syscall(code, &offset);
    shell_demo_emit_exit_on_error(code, &offset);
    shell_demo_emit_load_ebx(code, &offset, USER_DATA_BASE);
    shell_demo_emit_mov(code, &offset, 0U, SHELL_VMA_TEST_SENTINEL_ONE);
    code[offset++] = 0x89U;
    code[offset++] = 0x03U;
    code[offset++] = 0x8BU;
    code[offset++] = 0x03U;
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, SHELL_VMA_TEST_SENTINEL_ONE);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MMAP);
    shell_demo_emit_mov(code, &offset, 3U, SHELL_VMA_TEST_MULTI_LENGTH);
    shell_demo_emit_mov(code, &offset, 1U,
                        APP_MMAP_PROT_READ | APP_MMAP_PROT_WRITE);
    shell_demo_emit_mov(code, &offset, 2U, APP_MMAP_FLAG_ANONYMOUS);
    shell_demo_emit_mov(code, &offset, 6U,
                        USER_DATA_BASE + SHELL_VMA_TEST_MULTI_OFFSET);
    shell_demo_emit_syscall(code, &offset);
    shell_demo_emit_exit_on_error(code, &offset);
    shell_demo_emit_load_ebx(code, &offset,
                             USER_DATA_BASE + SHELL_VMA_TEST_MULTI_OFFSET);
    shell_demo_emit_mov(code, &offset, 0U, SHELL_VMA_TEST_SENTINEL_MULTI);
    code[offset++] = 0x89U;
    code[offset++] = 0x03U;
    code[offset++] = 0x8BU;
    code[offset++] = 0x03U;
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, SHELL_VMA_TEST_SENTINEL_MULTI);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MMAP);
    shell_demo_emit_mov(code, &offset, 3U, 0U);
    shell_demo_emit_mov(code, &offset, 1U, APP_MMAP_PROT_READ);
    shell_demo_emit_mov(code, &offset, 2U, APP_MMAP_FLAG_ANONYMOUS);
    shell_demo_emit_mov(code, &offset, 6U, USER_DATA_BASE);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, ERR_INVALID);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MMAP);
    shell_demo_emit_mov(code, &offset, 3U, 0xFFFFF001U);
    shell_demo_emit_mov(code, &offset, 1U, APP_MMAP_PROT_READ);
    shell_demo_emit_mov(code, &offset, 2U, APP_MMAP_FLAG_ANONYMOUS);
    shell_demo_emit_mov(code, &offset, 6U, USER_DATA_BASE);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, ERR_OVERFLOW);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MMAP);
    shell_demo_emit_mov(code, &offset, 3U, PAGE_SIZE);
    shell_demo_emit_mov(code, &offset, 1U, APP_MMAP_PROT_READ);
    shell_demo_emit_mov(code, &offset, 2U,
                        APP_MMAP_FLAG_SHARED | APP_MMAP_FLAG_ANONYMOUS);
    shell_demo_emit_mov(code, &offset, 6U, USER_DATA_BASE);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, ERR_UNAVAILABLE);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_load_ebx(code, &offset,
                             USER_DATA_BASE + SHELL_VMA_TEST_MULTI_OFFSET);
    shell_demo_emit_add_ebx(code, &offset, PAGE_SIZE);
    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 1U, PAGE_SIZE);
    shell_demo_emit_syscall(code, &offset);
    shell_demo_emit_exit_on_error(code, &offset);

    shell_demo_emit_load_ebx(code, &offset,
                             USER_DATA_BASE + SHELL_VMA_TEST_MULTI_OFFSET);
    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 1U, PAGE_SIZE);
    shell_demo_emit_syscall(code, &offset);
    shell_demo_emit_exit_on_error(code, &offset);

    shell_demo_emit_load_ebx(code, &offset,
                             USER_DATA_BASE + SHELL_VMA_TEST_MULTI_OFFSET);
    shell_demo_emit_add_ebx(code, &offset, PAGE_SIZE * 2U);
    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 1U, PAGE_SIZE * 2U);
    shell_demo_emit_syscall(code, &offset);
    shell_demo_emit_exit_on_error(code, &offset);

    shell_demo_emit_load_ebx(code, &offset, USER_DATA_BASE);
    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 1U, PAGE_SIZE);
    shell_demo_emit_syscall(code, &offset);
    shell_demo_emit_exit_on_error(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 3U, 0U);
    shell_demo_emit_mov(code, &offset, 1U, PAGE_SIZE);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, ERR_INVALID);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 3U, USER_CODE_BASE);
    shell_demo_emit_mov(code, &offset, 1U, 0U);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, ERR_INVALID);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_MUNMAP);
    shell_demo_emit_mov(code, &offset, 3U, USER_CODE_BASE);
    shell_demo_emit_mov(code, &offset, 1U, PAGE_SIZE);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0x3DU;
    shell_demo_patch_u32(code, offset, ERR_INVALID);
    offset += 4U;
    failure_jumps[failure_count++] =
        shell_demo_emit_jne_near(code, &offset);

    shell_demo_emit_mov(code, &offset, 3U, APP_EXIT_SUCCESS);
    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_PROCESS_EXIT);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0xF4U;

    failure_offset = offset;
    shell_demo_emit_mov(code, &offset, 3U, ERR_STATE);
    shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_PROCESS_EXIT);
    shell_demo_emit_syscall(code, &offset);
    code[offset++] = 0xF4U;
    for (uint32_t i = 0U; i < failure_count; i++) {
        int32_t relative = (int32_t)failure_offset -
                           (int32_t)(failure_jumps[i] + 4U);
        shell_demo_patch_u32(code, failure_jumps[i], (uint32_t)relative);
    }

    kmemcpy(header.magic, "ZAPP", 4);
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = offset;
    header.data_offset = APP_IMAGE_HEADER_SIZE + offset;
    header.data_size = SHELL_VMA_TEST_MULTI_OFFSET + sizeof(uint32_t);
    header.entry_offset = 0U;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    return header.data_offset + header.data_size;
}

static uint32_t shell_build_pagefault_fault_image(void) {
    app_image_header_t header;
    uint8_t* code = appcheck_demo_image + APP_IMAGE_HEADER_SIZE;
    uint32_t offset = 0U;

    kmemset(appcheck_demo_image, 0, sizeof(appcheck_demo_image));
    shell_demo_emit_mov(code, &offset, 0U, VMA_USER_MMAP_START);
    code[offset++] = 0x8BU;
    code[offset++] = 0x00U;
    code[offset++] = 0xF4U;

    kmemcpy(header.magic, "ZAPP", 4);
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = offset;
    header.data_offset = APP_IMAGE_HEADER_SIZE + offset;
    header.data_size = 0U;
    header.entry_offset = 0U;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    return header.data_offset;
}

static uint32_t shell_build_input_test_image(uint8_t use_tty) {
    app_image_header_t header;
    uint8_t* code = appcheck_demo_image + APP_IMAGE_HEADER_SIZE;
    uint32_t loop_offset;
    uint32_t offset = 0;
    uint32_t data_size = APP_INPUT_EVENT_OFFSET + sizeof(uint32_t) * 2U;

    kmemset(appcheck_demo_image, 0, sizeof(appcheck_demo_image));
    if (use_tty) {
        shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_FILE_OPEN);
        shell_demo_emit_mov(code, &offset, 3U,
                            USER_DATA_BASE + APP_INPUT_TTY_PATH_OFFSET);
        shell_demo_emit_mov(code, &offset, 1U, APP_FILE_MODE_READ_WRITE);
        shell_demo_emit_mov(code, &offset, 2U,
                            USER_DATA_BASE + APP_INPUT_TTY_FD_OFFSET);
        code[offset++] = 0xCDU;
        code[offset++] = 0x80U;
        shell_demo_emit_exit_on_error(code, &offset);
    }

    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_FILE_WRITE);
    if (use_tty) {
        shell_demo_emit_load_ebx(code, &offset,
                                 USER_DATA_BASE + APP_INPUT_TTY_FD_OFFSET);
    } else {
        shell_demo_emit_mov(code, &offset, 3, APP_FD_STDOUT);
    }
    shell_demo_emit_mov(code, &offset, 1, USER_DATA_BASE);
    shell_demo_emit_mov(code, &offset, 2, kstrlen(app_input_test_message));
    shell_demo_emit_mov(code, &offset, 6,
                        USER_DATA_BASE + APP_INPUT_READ_COUNT_OFFSET);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    shell_demo_emit_exit_on_error(code, &offset);

    loop_offset = offset;
    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_FILE_READ);
    if (use_tty) {
        shell_demo_emit_load_ebx(code, &offset,
                                 USER_DATA_BASE + APP_INPUT_TTY_FD_OFFSET);
    } else {
        shell_demo_emit_mov(code, &offset, 3, APP_FD_STDIN);
    }
    shell_demo_emit_mov(code, &offset, 1,
                        USER_DATA_BASE + APP_INPUT_EVENT_OFFSET);
    shell_demo_emit_mov(code, &offset, 2, 1U);
    shell_demo_emit_mov(code, &offset, 6,
                        USER_DATA_BASE + APP_INPUT_READ_COUNT_OFFSET);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    code[offset++] = 0x85;
    code[offset++] = 0xC0;
    if (shell_demo_emit_jne(code, &offset, loop_offset) != OK) return 0;

    code[offset++] = 0x0F;
    code[offset++] = 0xB6;
    code[offset++] = 0x05;
    shell_demo_patch_u32(code, offset, USER_DATA_BASE + APP_INPUT_EVENT_OFFSET);
    offset += 4;
    code[offset++] = 0x3D;
    shell_demo_patch_u32(code, offset, 0x1CU);
    offset += 4;
    if (shell_demo_emit_jne(code, &offset, loop_offset) != OK) return 0;

    if (use_tty) {
        shell_demo_emit_mov(code, &offset, 0U, APP_SYSCALL_FILE_CLOSE);
        shell_demo_emit_load_ebx(code, &offset,
                                 USER_DATA_BASE + APP_INPUT_TTY_FD_OFFSET);
        code[offset++] = 0xCDU;
        code[offset++] = 0x80U;
        shell_demo_emit_exit_on_error(code, &offset);
    }

    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_PROCESS_EXIT);
    code[offset++] = 0x31;
    code[offset++] = 0xDB;
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    code[offset++] = 0xF4;

    kmemcpy(header.magic, "ZAPP", 4);
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = offset;
    header.data_offset = APP_IMAGE_HEADER_SIZE + offset;
    header.data_size = data_size;
    header.entry_offset = 0;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;
    kmemcpy(appcheck_demo_image + header.data_offset, app_input_test_message,
            kstrlen(app_input_test_message));
    if (use_tty) {
        kmemcpy(appcheck_demo_image + header.data_offset +
                APP_INPUT_TTY_PATH_OFFSET, "/dev/tty", 9U);
    }
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    return header.data_offset + header.data_size;
}

void shell_checks_run_app_inputtest(uint8_t use_tty) {
    uint32_t image_size;
    uint32_t pid = 0;
    int result;

    if (!app_loader_is_ready()) {
        video_print("Erro: carregador de aplicativos indisponivel.\n", 0x0C);
        return;
    }

    image_size = shell_build_input_test_image(use_tty);
    if (image_size == 0) {
        video_print("Erro: nao foi possivel montar o teste de entrada.\n", 0x0C);
        return;
    }

    shell_remove_image(APP_INPUT_TEST_PATH);
    result = fs_write_file_at(APP_INPUT_TEST_PATH, appcheck_demo_image,
                              image_size);
    if (result == OK) {
        result = shell_verify_image(APP_INPUT_TEST_PATH, image_size);
    }
    if (result == OK) {
        result = app_loader_run_file(APP_INPUT_TEST_PATH, &pid);
    }
    shell_remove_image(APP_INPUT_TEST_PATH);

    if (result != OK) {
        video_print("Erro: teste de entrada indisponivel (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    video_print("Teste de entrada iniciado, PID ", 0x0A);
    shell_command_print_num(pid);
    video_print(". Aguarde o foco do aplicativo.\n", 0x0A);
}

static uint32_t shell_build_regcheck_input_image(uint8_t cancel_scancode) {
    app_image_header_t header;
    uint8_t* code = appcheck_demo_image + APP_IMAGE_HEADER_SIZE;
    uint32_t loop_offset;
    uint32_t offset = 0;
    uint32_t data_size = APP_INPUT_EVENT_OFFSET + sizeof(app_message_t);

    kmemset(appcheck_demo_image, 0, sizeof(appcheck_demo_image));
    loop_offset = offset;
    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_MESSAGE_RECEIVE);
    shell_demo_emit_mov(code, &offset, 3,
                        USER_DATA_BASE + APP_INPUT_EVENT_OFFSET);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    code[offset++] = 0x85;
    code[offset++] = 0xC0;
    if (shell_demo_emit_jne(code, &offset, loop_offset) != OK) return 0;

    code[offset++] = 0xA1;
    shell_demo_patch_u32(code, offset,
                         USER_DATA_BASE + APP_INPUT_EVENT_DATA1_OFFSET);
    offset += 4;
    code[offset++] = 0x3D;
    shell_demo_patch_u32(code, offset, cancel_scancode);
    offset += 4;
    if (shell_demo_emit_jne(code, &offset, loop_offset) != OK) return 0;

    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_PROCESS_EXIT);
    code[offset++] = 0x31;
    code[offset++] = 0xDB;
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    code[offset++] = 0xF4;

    kmemcpy(header.magic, "ZAPP", 4);
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = offset;
    header.data_offset = APP_IMAGE_HEADER_SIZE + offset;
    header.data_size = data_size;
    header.entry_offset = 0;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    return header.data_offset + header.data_size;
}

static void shell_q2check_print_result(const char* label, int result) {
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(result == OK ? " OK\n" : " ERRO\n",
                result == OK ? 0x0A : 0x0C);
}

static void shell_q2check_reset(void) {
    kmemset(&shell_q2check, 0, sizeof(shell_q2check));
    shell_q2check.logger_result = ERR_STATE;
    shell_q2check.fault_result[SHELL_Q2CHECK_FIRST_FAULT_INDEX] = ERR_STATE;
    shell_q2check.fault_result[SHELL_Q2CHECK_SECOND_FAULT_INDEX] = ERR_STATE;
    shell_q2check.summary_result = ERR_STATE;
    shell_q2check.cleanup_result = ERR_STATE;
    shell_q2check.state = SHELL_Q2CHECK_IDLE;
}

static int shell_q2check_validate_fault(uint32_t pid, uint32_t faulted,
                                        uint32_t fault_index) {
    process_user_fault_summary_t summary;
    uint32_t expected_count = shell_q2check.initial_fault_count +
                              fault_index + 1U;
    int result = OK;

    if (!faulted || pid != shell_q2check.expected_pid ||
        process_get_user_fault_count() != expected_count) {
        LOG_ERROR("SHELL", "Q2check recebeu falha isolada inesperada");
        result = ERR_STATE;
    }
    if (process_get_last_user_fault(&summary) != OK ||
        summary.pid != pid ||
        summary.vector != SHELL_Q2CHECK_EXPECTED_FAULT_VECTOR ||
        summary.error != SHELL_Q2CHECK_EXPECTED_FAULT_ERROR) {
        LOG_ERROR("SHELL", "Q2check detectou resumo de falha inseguro ou invalido");
        shell_q2check.summary_result = ERR_STATE;
    }
    return result;
}

static int shell_q2check_validate_cleanup(void) {
    if (process_get_focus() != shell_q2check.initial_focus ||
        process_get_user_count() != shell_q2check.initial_user_count ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) !=
            shell_q2check.initial_zombie_count) {
        LOG_ERROR("SHELL", "Q2check detectou processo ou foco residual");
        return ERR_STATE;
    }
    return OK;
}

static int shell_q2check_start_fault(uint32_t fault_index) {
    uint32_t pid = 0;
    int result;

    if (fault_index >= SHELL_Q2CHECK_FAULT_RUNS) {
        LOG_ERROR("SHELL", "Indice invalido no Q2check");
        return ERR_INVALID;
    }
    result = process_create_user_test(1, &pid);
    if (result != OK) {
        LOG_ERROR("SHELL", "Q2check nao iniciou falha isolada");
        return result;
    }
    shell_q2check.expected_pid = pid;
    shell_user_test_pid = pid;
    shell_q2check.state = fault_index == SHELL_Q2CHECK_FIRST_FAULT_INDEX ?
                          SHELL_Q2CHECK_FIRST_FAULT :
                          SHELL_Q2CHECK_SECOND_FAULT;
    return OK;
}

static void shell_q2check_finish(void) {
    int result = shell_q2check.logger_result;

    if (shell_q2check.fault_result[SHELL_Q2CHECK_FIRST_FAULT_INDEX] != OK ||
        shell_q2check.fault_result[SHELL_Q2CHECK_SECOND_FAULT_INDEX] != OK ||
        shell_q2check.summary_result != OK ||
        shell_q2check.cleanup_result != OK) {
        result = ERR_STATE;
    }
    video_print("\nQ2Check:\n", 0x0B);
    shell_q2check_print_result("logger_padrao", shell_q2check.logger_result);
    shell_q2check_print_result("falha_1",
                               shell_q2check.fault_result[
                                   SHELL_Q2CHECK_FIRST_FAULT_INDEX]);
    shell_q2check_print_result("falha_2",
                               shell_q2check.fault_result[
                                   SHELL_Q2CHECK_SECOND_FAULT_INDEX]);
    shell_q2check_print_result("resumo_seguro", shell_q2check.summary_result);
    shell_q2check_print_result("limpeza", shell_q2check.cleanup_result);
    shell_q2check_print_result("resultado", result);
    shell_q2check.state = SHELL_Q2CHECK_IDLE;
    shell_waiting_user_test = 0;
    shell_user_test_pid = 0;
    shell_runtime_reset_input();
    if (!shell_job_is_active()) shell_print_prompt();
}

static void shell_q2check_handle_user_test_result(uint32_t pid,
                                                   uint32_t faulted) {
    uint32_t fault_index = shell_q2check.state == SHELL_Q2CHECK_FIRST_FAULT ?
                           SHELL_Q2CHECK_FIRST_FAULT_INDEX :
                           SHELL_Q2CHECK_SECOND_FAULT_INDEX;
    int result;

    shell_q2check.fault_result[fault_index] =
        shell_q2check_validate_fault(pid, faulted, fault_index);
    result = process_reap_finished_user();
    if (result != OK) {
        LOG_ERROR("SHELL", "Q2check nao recolheu UserTest encerrado");
        shell_q2check.cleanup_result = result;
    } else if (shell_q2check_validate_cleanup() != OK) {
        shell_q2check.cleanup_result = ERR_STATE;
    }
    if (fault_index == SHELL_Q2CHECK_FIRST_FAULT_INDEX) {
        result = shell_q2check_start_fault(SHELL_Q2CHECK_SECOND_FAULT_INDEX);
        if (result == OK) return;
        shell_q2check.fault_result[SHELL_Q2CHECK_SECOND_FAULT_INDEX] = result;
    }
    shell_q2check_finish();
}

static void shell_regcheck_reset(void) {
    kmemset(&shell_regcheck, 0, sizeof(shell_regcheck));
    shell_regcheck.health_result = ERR_STATE;
    shell_regcheck.services_result = ERR_STATE;
    shell_regcheck.scheduler_result = ERR_STATE;
    shell_regcheck.memory_result = ERR_STATE;
    shell_regcheck.package_result = ERR_STATE;
    shell_regcheck.thread_result = ERR_STATE;
    shell_regcheck.processes_result = ERR_STATE;
    shell_regcheck.device_scan_result = ERR_STATE;
    shell_regcheck.block_result = ERR_STATE;
    shell_regcheck.devices_result = ERR_STATE;
    shell_regcheck.usb_result = ERR_STATE;
    shell_regcheck.network_result = ERR_STATE;
    shell_regcheck.wifi_result = ERR_STATE;
    shell_regcheck.acpi_result = ERR_STATE;
    shell_regcheck.power_result = ERR_STATE;
    shell_regcheck.index_result = ERR_STATE;
    shell_regcheck.loader_result = ERR_STATE;
    shell_regcheck.cancellation_result = ERR_STATE;
    shell_regcheck.cleanup_result = ERR_STATE;
    shell_regcheck.state = SHELL_REGCHECK_IDLE;
}

static int shell_regcheck_validate_health(void) {
    uint32_t component_count = recovery_get_count();

    if (component_count != RECOVERY_COMPONENT_COUNT) {
        LOG_ERROR("SHELL", "RegCheck detectou tabela health indisponivel");
        return ERR_STATE;
    }

    for (uint32_t i = 0; i < component_count; i++) {
        recovery_component_id_t id = (recovery_component_id_t)i;
        const recovery_component_t* component = recovery_get(id);
        int available = recovery_is_available(id);
        int enabled = recovery_is_enabled(id);

        if (!component || component->state == RECOVERY_STATE_UNKNOWN) {
            LOG_ERROR("SHELL", "RegCheck detectou componente health desconhecido");
            return ERR_STATE;
        }
        if (component->state == RECOVERY_STATE_READY && available && enabled) {
            continue;
        }
        if (component->state == RECOVERY_STATE_DEGRADED && !available && enabled) {
            continue;
        }
        if (component->state == RECOVERY_STATE_DISABLED && !available && !enabled) {
            continue;
        }

        LOG_ERROR("SHELL", "RegCheck detectou estado health inconsistente");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_services(void) {
    app_api_version_t version;
    app_uptime_info_t uptime;
    app_memory_info_t memory;
    net_socket_status_t sockets;

    if (log_get_level() != LOG_LEVEL_INFO || !app_api_is_ready() ||
        !app_api_file_is_ready() || !app_api_ipc_is_ready() ||
        !syscall_is_ready() || !syscall_user_mode_is_enabled() ||
        !idt_is_user_syscall_enabled() || !paging_is_ready() ||
        !app_loader_is_ready() || !app_package_is_ready() ||
        !app_catalog_is_ready() || timer_validate_state() != OK ||
        idt_validate_irq_state() != OK ||
        irq_deferred_validate_state() != OK ||
        workqueue_validate_state() != OK ||
        wait_validate_state() != OK ||
        vfs_validate_state() != OK ||
        process_signal_validate_state() != OK ||
        rtc_validate_state() != OK || clock_validate_state() != OK ||
        tls_validate_state() != OK) {
        LOG_ERROR("SHELL", "RegCheck encontrou servico obrigatorio indisponivel");
        return ERR_STATE;
    }
    if (net_socket_get_status(&sockets) != OK ||
        (sockets.initialized && net_socket_validate_state() != OK)) {
        LOG_ERROR("SHELL", "RegCheck encontrou filas de socket invalidas");
        return ERR_STATE;
    }
    if (app_api_get_version(&version) != OK ||
        version.major != APP_API_VERSION_MAJOR ||
        version.minor != APP_API_VERSION_MINOR ||
        app_api_get_uptime(&uptime) != OK ||
        app_api_get_memory_info(&memory) != OK) {
        LOG_ERROR("SHELL", "RegCheck encontrou contrato base da App API invalido");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_scheduler(void) {
    scheduler_validation_t validation;
    int result = scheduler_validate_invariants(&validation);

    if (result != OK || !validation.current_valid || !validation.idle_valid ||
        !validation.pid_table_valid || !validation.state_table_valid ||
        !validation.slab_table_valid || !validation.stack_table_valid ||
        !validation.idle_accounting_valid ||
        process_stack_self_test() != OK) {
        LOG_ERROR("SHELL", "RegCheck detectou invariante do scheduler invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_same_device(const device_info_t* left,
                                      const device_info_t* right) {
    return left->kind == right->kind &&
           left->status == right->status &&
           left->vendor_id == right->vendor_id &&
           left->device_id == right->device_id &&
           left->class_code == right->class_code &&
           left->subclass_code == right->subclass_code &&
           left->bus == right->bus &&
           left->device == right->device &&
           left->function == right->function &&
           left->irq == right->irq &&
           left->capacity_sectors == right->capacity_sectors;
}

static int shell_regcheck_validate_devices(void) {
    uint32_t count = 0;
    uint8_t pci_count = 0;
    uint8_t ata_count = 0;
    uint32_t ata_inventory_count;
    int result = pci_get_device_count(&pci_count);

    if (result != OK) {
        LOG_ERROR("SHELL", "RegCheck Full nao consultou contagem PCI");
        return result;
    }
    result = device_manager_get_count(&count);
    if (result != OK) {
        LOG_ERROR("SHELL", "RegCheck Full nao consultou Devices");
        return result;
    }
    ata_inventory_count = 1U;
    if (ata_get_device()) {
        result = ata_get_device_count(&ata_count);
        if (result != OK || ata_count == 0) {
            LOG_ERROR("SHELL", "RegCheck Full nao consultou contagem ATA");
            return result == OK ? ERR_STATE : result;
        }
        ata_inventory_count = ata_count;
    }
    if (count != (uint32_t)pci_count +
                 SHELL_REGCHECK_NON_ATA_DEVICE_COUNT + ata_inventory_count ||
        count > DEVICE_MANAGER_MAX_DEVICES) {
        LOG_ERROR("SHELL", "RegCheck Full detectou contagem Devices invalida");
        return ERR_STATE;
    }

    for (uint32_t index = 0; index < count; index++) {
        device_info_t info;
        device_info_t found;
        device_text_t text;

        result = device_manager_get_info(index, &info);
        if (result != OK ||
            info.kind > DEVICE_KIND_PC_SPEAKER ||
            info.status > DEVICE_STATUS_DISABLED) {
            LOG_ERROR("SHELL", "RegCheck Full detectou entrada Devices invalida");
            return result == OK ? ERR_STATE : result;
        }
        result = device_manager_format_text(&info, &text);
        if (result != OK || !text.id[0] || !text.name[0] ||
            !text.type[0] || !text.location[0] || !text.detail[0]) {
            LOG_ERROR("SHELL", "RegCheck Full nao formatou entrada Devices");
            return result == OK ? ERR_STATE : result;
        }
        result = device_manager_find(text.id, &found);
        if (result != OK || !shell_regcheck_same_device(&info, &found)) {
            LOG_ERROR("SHELL", "RegCheck Full detectou ID Devices instavel");
            return result == OK ? ERR_STATE : result;
        }
    }
    return OK;
}

static int shell_regcheck_same_usb(const usb_controller_info_t* left,
                                   const usb_controller_info_t* right) {
    if (!left || !right || left->model != right->model ||
        left->state != right->state || left->reason != right->reason ||
        left->vendor_id != right->vendor_id ||
        left->device_id != right->device_id ||
        left->class_code != right->class_code ||
        left->subclass_code != right->subclass_code ||
        left->prog_if != right->prog_if || left->revision != right->revision ||
        left->bus != right->bus || left->device != right->device ||
        left->function != right->function || left->irq != right->irq ||
        left->uhci_initialized != right->uhci_initialized ||
        left->uhci_irq_registered != right->uhci_irq_registered ||
        left->uhci_dma_ready != right->uhci_dma_ready ||
        left->uhci_transfer_ready != right->uhci_transfer_ready ||
        left->uhci_port_count != right->uhci_port_count ||
        left->uhci_device_count != right->uhci_device_count ||
        left->uhci_port_errors != right->uhci_port_errors ||
        left->uhci_last_error != right->uhci_last_error ||
        left->ehci_initialized != right->ehci_initialized ||
        left->ehci_irq_registered != right->ehci_irq_registered ||
        left->ehci_dma_ready != right->ehci_dma_ready ||
        left->ehci_transfer_ready != right->ehci_transfer_ready ||
        left->ehci_port_count != right->ehci_port_count ||
        left->ehci_device_count != right->ehci_device_count ||
        left->ehci_port_errors != right->ehci_port_errors ||
        left->ehci_last_error != right->ehci_last_error) {
        return 0;
    }
    for (uint32_t bar = 0; bar < USB_CONTROLLER_BAR_COUNT; bar++) {
        if (left->bars[bar] != right->bars[bar]) return 0;
    }
    return 1;
}

static int shell_regcheck_validate_usb_entry(
    const usb_controller_info_t* info) {
    usb_controller_info_t found;
    usb_controller_text_t text;
    int result;

    if (!info || info->class_code != USB_CONTROLLER_PCI_CLASS ||
        info->subclass_code != USB_CONTROLLER_PCI_SUBCLASS ||
        (info->prog_if == USB_CONTROLLER_PROG_IF_UHCI &&
         info->model != USB_CONTROLLER_MODEL_UHCI) ||
        (info->prog_if == USB_CONTROLLER_PROG_IF_EHCI &&
         info->model != USB_CONTROLLER_MODEL_EHCI) ||
        (info->prog_if != USB_CONTROLLER_PROG_IF_UHCI &&
         info->prog_if != USB_CONTROLLER_PROG_IF_EHCI &&
         info->model != USB_CONTROLLER_MODEL_OTHER) ||
        (info->model == USB_CONTROLLER_MODEL_OTHER &&
         (info->state != USB_CONTROLLER_DEGRADED ||
          info->reason != USB_CONTROLLER_REASON_OUT_OF_SCOPE)) ||
        (info->model == USB_CONTROLLER_MODEL_EHCI &&
         ((!info->ehci_initialized && info->state != USB_CONTROLLER_DEGRADED &&
           info->state != USB_CONTROLLER_DISABLED) ||
          (info->ehci_initialized && info->reason !=
           USB_CONTROLLER_REASON_DRIVER_READY &&
           info->reason != USB_CONTROLLER_REASON_PORT_FAILURE &&
           info->reason != USB_CONTROLLER_REASON_DRIVER_FAILURE))) ||
        (info->model == USB_CONTROLLER_MODEL_UHCI &&
         ((!info->uhci_initialized && info->state != USB_CONTROLLER_DEGRADED &&
           info->state != USB_CONTROLLER_DISABLED) ||
          (info->uhci_initialized && info->reason !=
           USB_CONTROLLER_REASON_DRIVER_READY &&
           info->reason != USB_CONTROLLER_REASON_PORT_FAILURE &&
           info->reason != USB_CONTROLLER_REASON_DRIVER_FAILURE)))) {
        LOG_ERROR("SHELL", "RegCheck detectou entrada USB invalida");
        return ERR_STATE;
    }
    result = usb_manager_format_text(info, &text);
    if (result != OK || !text.id[0] || !text.name[0] ||
        !text.location[0] || !text.detail[0]) {
        LOG_ERROR("SHELL", "RegCheck nao formatou entrada USB");
        return result == OK ? ERR_STATE : result;
    }
    result = usb_manager_find(text.id, &found);
    if (result != OK || !shell_regcheck_same_usb(info, &found)) {
        LOG_ERROR("SHELL", "RegCheck detectou ID USB instavel");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_usb(void) {
    usb_manager_status_t status;
    const recovery_component_t* component;
    uint32_t count = 0;
    int result;
    recovery_state_t expected_state;
    int expected_error;

    result = usb_manager_get_status(&status);
    if (result != OK || !status.initialized ||
        usb_manager_get_count(&count) != OK ||
        count != status.controller_count ||
        count > USB_MANAGER_MAX_CONTROLLERS ||
        status.uhci_count + status.ehci_count + status.other_count != count ||
        status.uhci_ready_count > status.uhci_count ||
        status.ehci_ready_count > status.ehci_count ||
        status.port_count > USB_MANAGER_MAX_PORTS ||
        status.configured_device_count > USB_MANAGER_MAX_DEVICES ||
        status.dma_td_in_use > status.dma_td_capacity ||
        status.msc_device_count > USB_MSC_MAX_DEVICES ||
        status.hid_device_count > USB_HID_MAX_DEVICES ||
        status.hid_active_count > status.hid_device_count ||
        status.hub_support_active ||
        status.hotplug_active ||
        usb_msc_validate_state() != OK || usb_hid_validate_state() != OK ||
        input_validate_state() != OK ||
        irq_deferred_validate_state() != OK ||
        usb_manager_validate_state() != OK) {
        LOG_ERROR("SHELL", "RegCheck detectou resumo USB invalido");
        return result == OK ? ERR_STATE : result;
    }
    for (uint32_t index = 0; index < count; index++) {
        usb_controller_info_t info;

        result = usb_manager_get_info(index, &info);
        if (result != OK || shell_regcheck_validate_usb_entry(&info) != OK) {
            LOG_ERROR("SHELL", "RegCheck detectou controlador USB invalido");
            return result == OK ? ERR_STATE : result;
        }
        if (info.model == USB_CONTROLLER_MODEL_UHCI &&
            info.uhci_initialized) {
            usb_uhci_status_t runtime;

            result = usb_manager_get_uhci_status(index, &runtime);
            if (result != OK || !runtime.initialized ||
                !runtime.irq_registered || !runtime.dma_ready ||
                !runtime.control_transfer_ready ||
                runtime.hub_support_active || runtime.hotplug_active ||
                !runtime.bulk_transfer_ready ||
                !runtime.interrupt_transfer_ready ||
                runtime.td_in_use > runtime.td_capacity ||
                runtime.buffer_in_use > runtime.buffer_capacity ||
                runtime.port_count != USB_UHCI_PORT_COUNT ||
                runtime.timeout_count > 0xFFFFFFFFU - runtime.recovery_count) {
                LOG_ERROR("SHELL", "RegCheck detectou runtime UHCI invalido");
                return result == OK ? ERR_STATE : result;
            }
        } else if (info.model == USB_CONTROLLER_MODEL_EHCI &&
                   info.ehci_initialized) {
            usb_ehci_status_t runtime;

            result = ehci_get_status(info.bus, info.device, info.function,
                                     &runtime);
            if (result != OK || !runtime.initialized ||
                !runtime.running || !runtime.irq_registered ||
                !runtime.dma_ready || !runtime.control_transfer_ready ||
                !runtime.bulk_transfer_ready ||
                !runtime.interrupt_transfer_ready ||
                runtime.qtd_in_use > runtime.qtd_capacity ||
                runtime.buffer_in_use > runtime.buffer_capacity ||
                runtime.port_count == 0U ||
                runtime.port_count > USB_EHCI_PORT_COUNT ||
                runtime.timeout_count >
                0xFFFFFFFFU - runtime.recovery_count) {
                LOG_ERROR("SHELL", "RegCheck detectou runtime EHCI invalido");
                return result == OK ? ERR_STATE : result;
            }
        }
    }
    component = recovery_get(RECOVERY_COMPONENT_USB);
    if (!component) {
        LOG_ERROR("SHELL", "RegCheck nao consultou recovery USB");
        return ERR_STATE;
    }
    if (status.partial) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = ERR_OVERFLOW;
    } else if (!count) {
        expected_state = RECOVERY_STATE_DISABLED;
        expected_error = ERR_NOT_FOUND;
    } else if (status.last_error != OK) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status.last_error;
    } else {
        expected_state = RECOVERY_STATE_READY;
        expected_error = OK;
    }
    if (status.last_error != expected_error ||
        component->state != expected_state ||
        component->last_error != expected_error) {
        LOG_ERROR("SHELL", "RegCheck detectou recovery USB incoerente");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_valid_acpi_table(
    const acpi_table_info_t* table) {
    if (!table || table->signature[4] != '\0' ||
        !table->physical_address ||
        table->length < SHELL_REGCHECK_ACPI_SDT_HEADER_SIZE ||
        table->length > SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE ||
        !table->checksum_valid) {
        return 0;
    }
    for (uint32_t index = 0; index < 4U; index++) {
        if (!table->signature[index]) return 0;
    }
    return 1;
}

static uint32_t shell_regcheck_acpi_read_u32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int shell_regcheck_valid_acpi_madt_entry(
    const acpi_madt_entry_t* entry) {
    if (!entry || entry->length < ACPI_MADT_ENTRY_HEADER_LENGTH ||
        entry->length > ACPI_MADT_MAX_ENTRY_LENGTH ||
        entry->raw[0] != entry->type || entry->raw[1] != entry->length) {
        return 0;
    }
    if ((entry->type == ACPI_MADT_TYPE_LOCAL_APIC &&
         entry->length < ACPI_MADT_LOCAL_APIC_MIN_LENGTH) ||
        (entry->type == ACPI_MADT_TYPE_IO_APIC &&
         entry->length < ACPI_MADT_IO_APIC_MIN_LENGTH) ||
        (entry->type == ACPI_MADT_TYPE_LOCAL_X2APIC &&
         entry->length < ACPI_MADT_LOCAL_X2APIC_MIN_LENGTH)) {
        return 0;
    }
    return 1;
}

static int shell_regcheck_validate_acpi_power(
    const acpi_status_t* status, const acpi_power_info_t* power) {
    if (!power->initialized || power->mode > ACPI_MODE_INCONSISTENT ||
        power->s5_state > ACPI_S5_AMBIGUOUS ||
        (power->pm1a_readable && !power->pm1a_present) ||
        (power->pm1b_readable && !power->pm1b_present) ||
        (power->s5_state == ACPI_S5_DECLARED &&
         (power->s5_candidates != 1U ||
          power->s5_type_a > 7U || power->s5_type_b > 7U)) ||
        (!status->available &&
         (power->fadt_power_fields_present ||
          power->s5_transition_ready))) {
        LOG_ERROR("SHELL", "RegCheck Full detectou snapshot ACPI invalido");
        return ERR_STATE;
    }
    if (power->s5_transition_ready &&
        (!status->available || status->partial ||
         !status->fadt_present || !status->dsdt_present ||
         !power->fadt_power_fields_present || power->hardware_reduced ||
         !power->pm1a_readable ||
         (power->pm1b_present && !power->pm1b_readable) ||
         power->s5_state != ACPI_S5_DECLARED ||
         (power->mode != ACPI_MODE_ENABLED &&
          !power->mode_enable_available))) {
        LOG_ERROR("SHELL", "RegCheck Full detectou prontidao S5 incoerente");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_acpi_recovery(
    const acpi_status_t* status) {
    const recovery_component_t* component =
        recovery_get(RECOVERY_COMPONENT_ACPI);
    recovery_state_t expected;

    if (!component || !status) {
        LOG_ERROR("SHELL", "RegCheck Full nao consultou recovery ACPI");
        return ERR_STATE;
    }
    if (!status->available) {
        expected = RECOVERY_STATE_DISABLED;
    } else if (status->partial ||
               !status->fadt_present || !status->dsdt_present) {
        expected = RECOVERY_STATE_DEGRADED;
    } else {
        expected = RECOVERY_STATE_READY;
    }
    if (component->state != expected) {
        LOG_ERROR("SHELL", "RegCheck Full detectou recovery ACPI incoerente");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_acpi(void) {
    acpi_status_t status;
    acpi_power_info_t power;
    acpi_madt_info_t madt;
    uint32_t table_count = 0;
    uint32_t madt_count = 0;
    int result = acpi_get_status(&status);

    if (result != OK || acpi_get_power_info(&power) != OK ||
        acpi_get_table_count(&table_count) != OK ||
        acpi_get_madt_info(&madt) != OK ||
        acpi_get_madt_entry_count(&madt_count) != OK ||
        !status.initialized || table_count != status.table_count ||
        table_count > ACPI_MAX_TABLES ||
        madt_count != madt.entry_count ||
        madt_count > ACPI_MAX_MADT_ENTRIES ||
        !madt.initialized ||
        status.root_entry_count > SHELL_REGCHECK_ACPI_MAX_ROOT_ENTRIES ||
        status.root_kind > ACPI_ROOT_XSDT ||
        (status.available &&
         (status.root_kind == ACPI_ROOT_NONE ||
          !status.rsdp_address || !status.root_address ||
          !status.rsdp_checksum_valid ||
          ((status.revision < 2U &&
            status.rsdp_length != SHELL_REGCHECK_ACPI_RSDP_V1_LENGTH) ||
           (status.revision >= 2U &&
            (status.rsdp_length < SHELL_REGCHECK_ACPI_RSDP_V2_LENGTH ||
             status.rsdp_length > SHELL_REGCHECK_ACPI_MAX_RSDP_LENGTH))) ||
          !table_count)) ||
        (!status.available &&
         (status.rsdp_length || status.rsdp_checksum_valid)) ||
        (!status.available && status.root_kind != ACPI_ROOT_NONE) ||
        (status.fadt_present != (status.fadt_address != 0)) ||
        (status.dsdt_present != (status.dsdt_address != 0)) ||
        (status.facs_present != (status.facs_address != 0)) ||
        (status.madt_present != (status.madt_address != 0)) ||
        (madt.present != status.madt_present) ||
        (!madt.present &&
         (madt.physical_address || madt.length || madt.entry_count ||
          madt.skipped_entries || madt.local_apic_count ||
          madt.enabled_processor_count || madt.io_apic_count))) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado ACPI invalido");
        return result == OK ? ERR_STATE : result;
    }
    for (uint32_t index = 0; index < table_count; index++) {
        acpi_table_info_t table;

        result = acpi_get_table_at(index, &table);
        if (result != OK || !shell_regcheck_valid_acpi_table(&table)) {
            LOG_ERROR("SHELL", "RegCheck Full detectou tabela ACPI invalida");
            return result == OK ? ERR_STATE : result;
        }
        for (uint32_t previous = 0; previous < index; previous++) {
            acpi_table_info_t prior;

            result = acpi_get_table_at(previous, &prior);
            if (result != OK ||
                prior.physical_address == table.physical_address) {
                LOG_ERROR("SHELL", "RegCheck Full detectou tabela ACPI duplicada");
                return result == OK ? ERR_STATE : result;
            }
        }
        if (index == 0U &&
            (table.physical_address != status.root_address ||
             (status.root_kind == ACPI_ROOT_RSDT &&
              !shell_command_args_equal(table.signature, "RSDT")) ||
             (status.root_kind == ACPI_ROOT_XSDT &&
              !shell_command_args_equal(table.signature, "XSDT")))) {
            LOG_ERROR("SHELL", "RegCheck Full detectou ordem da raiz ACPI invalida");
            return ERR_STATE;
        }
    }
    if (madt.present &&
        (madt.length < ACPI_MADT_HEADER_LENGTH ||
         madt.length > SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE ||
         madt.physical_address != status.madt_address ||
         madt.local_apic_count > madt.entry_count ||
         madt.enabled_processor_count > madt.local_apic_count ||
         madt.io_apic_count > madt.entry_count)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou resumo MADT invalido");
        return ERR_STATE;
    }
    {
        uint32_t local_apic_count = 0;
        uint32_t enabled_processor_count = 0;
        uint32_t io_apic_count = 0;

        for (uint32_t index = 0; index < madt_count; index++) {
            acpi_madt_entry_t entry;

            result = acpi_get_madt_entry_at(index, &entry);
            if (result != OK ||
                !shell_regcheck_valid_acpi_madt_entry(&entry)) {
                LOG_ERROR("SHELL", "RegCheck Full detectou entrada MADT invalida");
                return result == OK ? ERR_STATE : result;
            }
            if (entry.type == ACPI_MADT_TYPE_LOCAL_APIC ||
                entry.type == ACPI_MADT_TYPE_LOCAL_X2APIC) {
                uint32_t flags = entry.type == ACPI_MADT_TYPE_LOCAL_APIC ?
                    shell_regcheck_acpi_read_u32(
                        entry.raw + ACPI_MADT_LOCAL_APIC_ENTRY_FLAGS_OFFSET) :
                    shell_regcheck_acpi_read_u32(
                        entry.raw + ACPI_MADT_LOCAL_X2APIC_ENTRY_FLAGS_OFFSET);

                local_apic_count++;
                if (flags & ACPI_MADT_PROCESSOR_ENABLED) {
                    enabled_processor_count++;
                }
            } else if (entry.type == ACPI_MADT_TYPE_IO_APIC) {
                io_apic_count++;
            }
        }
        if (local_apic_count != madt.local_apic_count ||
            enabled_processor_count != madt.enabled_processor_count ||
            io_apic_count != madt.io_apic_count) {
            LOG_ERROR("SHELL", "RegCheck Full detectou contagem MADT incoerente");
            return ERR_STATE;
        }
    }
    result = shell_regcheck_validate_acpi_power(&status, &power);
    if (result != OK) return result;
    return shell_regcheck_validate_acpi_recovery(&status);
}

static int shell_regcheck_validate_power(void) {
    power_status_t status;
    acpi_status_t acpi;
    acpi_power_info_t acpi_power;
    const recovery_component_t* component =
        recovery_get(RECOVERY_COMPONENT_POWER);
    int result = power_get_status(&status);

    if (result != OK || acpi_get_status(&acpi) != OK ||
        acpi_get_power_info(&acpi_power) != OK || !component) {
        LOG_ERROR("SHELL", "RegCheck Full nao consultou Power");
        return result == OK ? ERR_STATE : result;
    }
    if (status.states[POWER_STATE_S0] != POWER_CAPABILITY_AVAILABLE ||
        status.states[POWER_STATE_S1] != POWER_CAPABILITY_UNAVAILABLE ||
        status.states[POWER_STATE_S2] != POWER_CAPABILITY_UNAVAILABLE ||
        status.states[POWER_STATE_S3] != POWER_CAPABILITY_UNAVAILABLE ||
        status.states[POWER_STATE_S4] != POWER_CAPABILITY_UNAVAILABLE ||
        status.cpu_idle != POWER_CAPABILITY_AVAILABLE ||
        status.reboot != POWER_CAPABILITY_AVAILABLE ||
        status.acpi_available != acpi.available ||
        status.acpi_power_tables_available !=
            (acpi.fadt_present && acpi.dsdt_present) ||
        status.acpi_partial != acpi.partial ||
        status.acpi_pm1_control_available !=
            (acpi_power.pm1a_readable &&
             (!acpi_power.pm1b_present || acpi_power.pm1b_readable)) ||
        status.acpi_mode_known !=
            (acpi_power.mode == ACPI_MODE_ENABLED ||
             acpi_power.mode == ACPI_MODE_DISABLED) ||
        status.acpi_mode_enabled !=
            (acpi_power.mode == ACPI_MODE_ENABLED) ||
        status.acpi_s5_declared !=
            (acpi_power.s5_state == ACPI_S5_DECLARED) ||
        status.acpi_mode_enable_available !=
            acpi_power.mode_enable_available ||
        status.acpi_s5_transition_ready !=
            acpi_power.s5_transition_ready ||
        component->state != RECOVERY_STATE_READY) {
        LOG_ERROR("SHELL", "RegCheck Full detectou contrato Power invalido");
        return ERR_STATE;
    }
    if (status.acpi_s5_transition_ready) {
        if (status.states[POWER_STATE_S5] != POWER_CAPABILITY_AVAILABLE ||
            status.hardware_poweroff != POWER_CAPABILITY_AVAILABLE) {
            LOG_ERROR("SHELL", "RegCheck Full detectou S5 pronto incoerente");
            return ERR_STATE;
        }
    } else if (status.states[POWER_STATE_S5] != POWER_CAPABILITY_SIMULATED ||
               status.hardware_poweroff != POWER_CAPABILITY_UNAVAILABLE) {
        LOG_ERROR("SHELL", "RegCheck Full detectou fallback Power incoerente");
        return ERR_STATE;
    }
    return OK;
}

static void shell_regcheck_run_full_checks(void) {
    shell_device_scan_result_t scan;
    const recovery_component_t* network_before =
        recovery_get(RECOVERY_COMPONENT_NETWORK);
    recovery_state_t previous_network_state = RECOVERY_STATE_UNKNOWN;
    uint32_t previous_network_failures = 0;
    int previous_network_error = ERR_STATE;
    const recovery_component_t* usb_before =
        recovery_get(RECOVERY_COMPONENT_USB);
    recovery_state_t previous_usb_state = RECOVERY_STATE_UNKNOWN;
    uint32_t previous_usb_failures = 0;
    int previous_usb_error = ERR_STATE;
    int network_idempotent = 1;
    int usb_idempotent = 1;
    log_level_t previous_console_level = log_get_console_level();
    log_level_t previous_buffer_level = log_get_buffer_level();

    if (network_before) {
        previous_network_state = network_before->state;
        previous_network_failures = network_before->failures;
        previous_network_error = network_before->last_error;
    }
    if (usb_before) {
        previous_usb_state = usb_before->state;
        previous_usb_failures = usb_before->failures;
        previous_usb_error = usb_before->last_error;
    }
    log_set_level(LOG_LEVEL_ERROR);
    shell_regcheck.device_scan_result = shell_diagnostics_run_device_scan(&scan);
    log_set_buffer_level(previous_buffer_level);
    log_set_console_level(previous_console_level);

    network_before = recovery_get(RECOVERY_COMPONENT_NETWORK);
    if (network_before &&
        network_before->state == previous_network_state &&
        network_before->last_error == previous_network_error &&
        network_before->failures != previous_network_failures) {
        LOG_ERROR("SHELL", "RegCheck Full detectou falhas Network crescentes");
        network_idempotent = 0;
    }
    usb_before = recovery_get(RECOVERY_COMPONENT_USB);
    if (usb_before && usb_before->state == previous_usb_state &&
        usb_before->last_error == previous_usb_error &&
        usb_before->failures != previous_usb_failures) {
        LOG_ERROR("SHELL", "RegCheck Full detectou falhas USB crescentes");
        usb_idempotent = 0;
    }
    if (shell_regcheck.device_scan_result == OK) {
        shell_regcheck.devices_result = shell_regcheck_validate_devices();
    } else {
        shell_regcheck.devices_result = OK;
    }
    shell_regcheck.block_result = block_validate_state();
    if (shell_regcheck.block_result == OK) {
        shell_regcheck.block_result = block_self_test();
    }
    if (scan.wifi_result == OK || scan.wifi_result == ERR_OVERFLOW) {
        shell_regcheck.wifi_result = wifi_manager_validate_state();
    } else {
        shell_regcheck.wifi_result = scan.wifi_result;
    }
    if (!usb_idempotent) {
        shell_regcheck.usb_result = ERR_STATE;
    } else if (scan.usb_result == OK || scan.usb_result == ERR_OVERFLOW) {
        shell_regcheck.usb_result = shell_regcheck_validate_usb();
    } else {
        shell_regcheck.usb_result = scan.usb_result;
    }
    if (!network_idempotent) {
        shell_regcheck.network_result = ERR_STATE;
    } else if (scan.network_result == OK) {
        shell_regcheck.network_result = shell_network_validate_for_checks();
    } else if (scan.network_result == ERR_STATE &&
               shell_regcheck.device_scan_result != OK &&
               shell_regcheck.device_scan_result != ERR_OVERFLOW) {
        shell_regcheck.network_result = OK;
    } else {
        shell_regcheck.network_result = scan.network_result;
    }
    shell_regcheck.acpi_result = shell_regcheck_validate_acpi();
    shell_regcheck.power_result = shell_regcheck_validate_power();
    shell_regcheck.index_result = file_index_validate_state();
    if (shell_regcheck.index_result == OK) {
        shell_regcheck.index_result = file_index_self_test();
    }
}

static int shell_regcheck_validate_packages(void) {
    app_package_diagnostic_t diagnostic;
    int result;

    kmemset(&diagnostic, 0, sizeof(diagnostic));
    result = app_package_run_diagnostics(&diagnostic);
    if (result != OK || !diagnostic.invalid_package ||
        !diagnostic.missing_dependency || !diagnostic.insufficient_space ||
        !diagnostic.mutation_serialization) {
        LOG_ERROR("SHELL", "RegCheck detectou validacao de pacote invalida");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_processes(void) {
    if (process_get_count() != shell_regcheck.initial_process_count ||
        process_get_user_count() != shell_regcheck.initial_user_count ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) !=
            shell_regcheck.initial_zombie_count) {
        LOG_ERROR("SHELL", "RegCheck detectou processos residuais");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_cleanup(void) {
    paging_user_stats_t paging;

    paging_get_user_stats(&paging);
    if (process_get_focus() != shell_regcheck.initial_focus ||
        process_get_user_count() != shell_regcheck.initial_user_count ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) !=
            shell_regcheck.initial_zombie_count ||
        paging.active_directories != shell_regcheck.initial_user_directories ||
        paging.active_pages != shell_regcheck.initial_user_pages ||
        paging.active_directories != 0 || paging.active_pages != 0 ||
        app_loader_is_foreground_active()) {
        LOG_ERROR("SHELL", "RegCheck detectou foco ou recursos ring 3 residuais");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_start_image(shell_regcheck_state_t state) {
    const char* name;
    uint32_t image_size;
    uint32_t pid = 0;
    int result;

    if (state == SHELL_REGCHECK_WAIT_DEMO) {
        name = "REGDEMO.ZAP";
        image_size = shell_build_demo_image();
    } else if (state == SHELL_REGCHECK_WAIT_CANCEL) {
        name = "REGCANCEL.ZAP";
        image_size = shell_build_regcheck_input_image(
            SHELL_REGCHECK_SCANCODE_F11);
    } else {
        LOG_ERROR("SHELL", "RegCheck recebeu etapa ZAPP invalida");
        return ERR_INVALID;
    }
    if (image_size == 0) {
        LOG_ERROR("SHELL", "RegCheck nao montou imagem ZAPP interna");
        return ERR_STATE;
    }

    result = app_loader_run_image(name, appcheck_demo_image, image_size, 0, &pid);
    if (result != OK) {
        LOG_ERROR("SHELL", "RegCheck nao iniciou imagem ZAPP interna");
        return result;
    }
    shell_regcheck.expected_pid = pid;
    shell_regcheck.state = state;
    return OK;
}

static int shell_regcheck_validate_loader_result(
    const app_loader_result_t* result, int expect_cancelled) {
    int expected_exit = expect_cancelled ? APP_EXIT_CANCELLED : APP_EXIT_SUCCESS;

    if (!result || result->pid != shell_regcheck.expected_pid ||
        result->start_failed || result->faulted || !result->focus_acquired ||
        result->exit_code != (uint32_t)expected_exit ||
        (expect_cancelled && !result->cancelled) ||
        (!expect_cancelled && result->cancelled)) {
        LOG_ERROR("SHELL", "RegCheck recebeu resultado ZAPP inesperado");
        return ERR_STATE;
    }
    return shell_regcheck_validate_cleanup();
}

static int shell_regcheck_has_failures(void) {
    if (shell_regcheck.health_result != OK ||
        shell_regcheck.services_result != OK ||
        shell_regcheck.scheduler_result != OK ||
        shell_regcheck.memory_result != OK ||
        shell_regcheck.package_result != OK ||
        shell_regcheck.thread_result != OK ||
        shell_regcheck.processes_result != OK ||
        shell_regcheck.cleanup_result != OK) {
        return 1;
    }
    if (shell_regcheck.full_mode &&
        (shell_regcheck.device_scan_result != OK ||
         shell_regcheck.block_result != OK ||
         shell_regcheck.devices_result != OK ||
         shell_regcheck.usb_result != OK ||
         shell_regcheck.network_result != OK ||
         shell_regcheck.wifi_result != OK ||
         shell_regcheck.acpi_result != OK ||
         shell_regcheck.power_result != OK ||
         shell_regcheck.index_result != OK)) {
        return 1;
    }
    if (shell_regcheck.loader_started && shell_regcheck.loader_result != OK) {
        return 1;
    }
    return shell_regcheck.cancellation_started &&
           shell_regcheck.cancellation_result != OK;
}

static void shell_regcheck_print_failure(const char* label, int result) {
    if (result == OK) return;
    video_print("  ", 0x07);
    video_print(label, 0x0C);
    video_print(" codigo=", 0x07);
    shell_command_print_num((uint32_t)result);
    video_print("\n", 0x07);
}

static void shell_regcheck_finish(void) {
    int failed = shell_regcheck_has_failures();

    if (shell_regcheck.full_mode && shell_regcheck.block_result == OK) {
        video_print("camada_bloco OK\n", 0x0A);
    }
    video_print("\nRegCheck: ", 0x0B);
    if (!failed) {
        video_print("OK\n", 0x0A);
    } else {
        video_print("ERRO\n", 0x0C);
        shell_regcheck_print_failure("health", shell_regcheck.health_result);
        shell_regcheck_print_failure("servicos_base",
                                     shell_regcheck.services_result);
        shell_regcheck_print_failure("scheduler", shell_regcheck.scheduler_result);
        shell_regcheck_print_failure("memoria", shell_regcheck.memory_result);
        shell_regcheck_print_failure("pacotes", shell_regcheck.package_result);
        shell_regcheck_print_failure("threads", shell_regcheck.thread_result);
        shell_regcheck_print_failure("processos",
                                     shell_regcheck.processes_result);
        if (shell_regcheck.full_mode) {
            shell_regcheck_print_failure("device_scan",
                                         shell_regcheck.device_scan_result);
            shell_regcheck_print_failure("camada_bloco",
                                         shell_regcheck.block_result);
            shell_regcheck_print_failure("devices",
                                         shell_regcheck.devices_result);
            shell_regcheck_print_failure("usb", shell_regcheck.usb_result);
            shell_regcheck_print_failure("network",
                                         shell_regcheck.network_result);
            shell_regcheck_print_failure("wifi",
                                         shell_regcheck.wifi_result);
            shell_regcheck_print_failure("acpi",
                                         shell_regcheck.acpi_result);
            shell_regcheck_print_failure("power",
                                         shell_regcheck.power_result);
            shell_regcheck_print_failure("file_index",
                                         shell_regcheck.index_result);
        }
        if (shell_regcheck.loader_started) {
            shell_regcheck_print_failure("loader_ring3",
                                         shell_regcheck.loader_result);
        }
        if (shell_regcheck.cancellation_started) {
            shell_regcheck_print_failure("cancelamento_f11",
                                         shell_regcheck.cancellation_result);
        }
        shell_regcheck_print_failure("limpeza_final",
                                     shell_regcheck.cleanup_result);
        video_print("  resultado ERRO\n", 0x0C);
    }
    shell_regcheck_reset();
    shell_runtime_reset_input();
    if (!shell_job_is_active()) shell_print_prompt();
}

static void shell_regcheck_finish_after_ring3(void) {
    shell_memcheck_result_t memory;
    int result;

    result = shell_regcheck_validate_scheduler();
    if (shell_regcheck.scheduler_result == OK && result != OK) {
        shell_regcheck.scheduler_result = result;
    }
    result = shell_diagnostics_run_memcheck(&memory);
    if (shell_regcheck.memory_result == OK && result != OK) {
        shell_regcheck.memory_result = result;
    }
    shell_regcheck.processes_result = shell_regcheck_validate_processes();
    shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
    shell_regcheck_finish();
}

static void shell_regcheck_prepare_progress(shell_job_context_t* context,
                                            const char* phase,
                                            uint32_t progress) {
    uint32_t total = shell_regcheck.full_mode ? 6U : 5U;

    shell_job_set_phase(context, phase);
    shell_job_set_progress(context, progress, total);
}

static shell_job_step_result_t shell_regcheck_prepare_pending(
    shell_job_context_t* context) {
    shell_job_set_next_wake(context, timer_get_ticks() + 1U);
    return SHELL_JOB_STEP_PENDING;
}

static shell_job_step_result_t shell_regcheck_prepare_step(
    shell_job_context_t* context) {
    shell_memcheck_result_t memory;
    uint32_t offset = shell_regcheck.full_mode ? 1U : 0U;
    int result;

    if (!context) return SHELL_JOB_STEP_FAILED;
    if (shell_regcheck.state == SHELL_REGCHECK_PREPARE_FULL) {
        shell_regcheck_prepare_progress(context, "dispositivos", 0U);
        shell_regcheck_run_full_checks();
        shell_regcheck.state = SHELL_REGCHECK_PREPARE_BASE;
        shell_regcheck_prepare_progress(context, "dispositivos", 1U);
        return shell_regcheck_prepare_pending(context);
    }
    if (shell_regcheck.state == SHELL_REGCHECK_PREPARE_BASE) {
        shell_regcheck_prepare_progress(context, "invariantes", offset);
        shell_regcheck.health_result = shell_regcheck_validate_health();
        shell_regcheck.services_result = shell_regcheck_validate_services();
        shell_regcheck.scheduler_result = shell_regcheck_validate_scheduler();
        shell_regcheck.state = SHELL_REGCHECK_PREPARE_MEMORY;
        return shell_regcheck_prepare_pending(context);
    }
    if (shell_regcheck.state == SHELL_REGCHECK_PREPARE_MEMORY) {
        shell_regcheck_prepare_progress(context, "memoria", offset + 1U);
        kmemset(&memory, 0, sizeof(memory));
        shell_regcheck.memory_result = shell_diagnostics_run_memcheck(&memory);
        shell_regcheck.state = SHELL_REGCHECK_PREPARE_PACKAGES;
        return shell_regcheck_prepare_pending(context);
    }
    if (shell_regcheck.state == SHELL_REGCHECK_PREPARE_PACKAGES) {
        shell_regcheck_prepare_progress(context, "pacotes", offset + 2U);
        shell_regcheck.package_result = shell_regcheck_validate_packages();
        shell_regcheck.state = SHELL_REGCHECK_PREPARE_THREADS;
        return shell_regcheck_prepare_pending(context);
    }
    if (shell_regcheck.state == SHELL_REGCHECK_PREPARE_THREADS) {
        shell_regcheck_prepare_progress(context, "threads", offset + 3U);
        shell_regcheck.thread_result = thread_run_self_test();
        shell_regcheck.processes_result = shell_regcheck_validate_processes();
        shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
        if (shell_regcheck_has_failures()) {
            shell_regcheck_finish();
            return shell_regcheck_prepare_pending(context);
        }
        shell_regcheck.state = SHELL_REGCHECK_PREPARE_LOADER;
        return shell_regcheck_prepare_pending(context);
    }
    if (shell_regcheck.state == SHELL_REGCHECK_PREPARE_LOADER) {
        shell_regcheck_prepare_progress(context, "ring3", offset + 4U);
        shell_regcheck.loader_started = 1;
        result = shell_regcheck_start_image(SHELL_REGCHECK_WAIT_DEMO);
        if (result == OK) return SHELL_JOB_STEP_PENDING;
        shell_regcheck.loader_result = result;
        shell_regcheck.processes_result = shell_regcheck_validate_processes();
        shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
        shell_regcheck_finish();
        return shell_regcheck_prepare_pending(context);
    }
    LOG_ERROR("SHELL", "RegCheck recebeu preparacao invalida");
    context->last_error = ERR_STATE;
    return SHELL_JOB_STEP_FAILED;
}

static void shell_regcheck_handle_loader_result(const app_loader_result_t* result) {
    int launch_result;

    if (shell_regcheck.state == SHELL_REGCHECK_WAIT_DEMO) {
        shell_regcheck.loader_result =
            shell_regcheck_validate_loader_result(result, 0);
        if (shell_regcheck.loader_result != OK) {
            shell_regcheck.processes_result = shell_regcheck_validate_processes();
            shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
            shell_regcheck_finish();
            return;
        }

        shell_regcheck.cancellation_started = 1;
        launch_result = shell_regcheck_start_image(SHELL_REGCHECK_WAIT_CANCEL);
        if (launch_result == OK) {
            video_print("RegCheck: pressione F11 para validar cancelamento.\n",
                        0x0B);
            return;
        }
        shell_regcheck.cancellation_result = launch_result;
        shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
        shell_regcheck_finish();
        return;
    }

    if (shell_regcheck.state == SHELL_REGCHECK_WAIT_CANCEL) {
        shell_regcheck.cancellation_result =
            shell_regcheck_validate_loader_result(result, 1);
        shell_regcheck_finish_after_ring3();
        return;
    }

    LOG_ERROR("SHELL", "RegCheck recebeu resultado do loader fora de etapa");
    shell_regcheck.cleanup_result = ERR_STATE;
    shell_regcheck_finish();
}

static int shell_appcheck_migration_is_valid(const app_loader_result_t* result) {
    process_t* current = process_get_current();

    if (!result || result->start_failed || result->faulted ||
        result->cancelled || result->exit_code != OK ||
        !result->focus_acquired || !current ||
        process_get_focus() != current->pid ||
        process_get_user_count() != shell_appcheck_user_count ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) !=
            shell_appcheck_zombie_count) {
        LOG_ERROR("SHELL", "Migracao ZAPP falhou na validacao de ciclo de vida");
        return ERR_STATE;
    }
    return OK;
}

static int shell_appcheck_start_migration(shell_builtin_app_t app) {
    uint32_t pid = 0;
    int result;

    shell_appcheck_user_count = process_get_user_count();
    shell_appcheck_zombie_count = process_get_state_count(PROCESS_STATE_ZOMBIE);
    if (app == SHELL_BUILTIN_APP_UPTIME) {
        result = app_builtin_run_uptime(&pid);
        cmd_appcheck_print_result("migracao_uptime_inicio", result);
    } else if (app == SHELL_BUILTIN_APP_MEM) {
        result = app_builtin_run_mem(&pid);
        cmd_appcheck_print_result("migracao_mem_inicio", result);
    } else {
        LOG_ERROR("SHELL", "Migracao appcheck sem aplicativo definido");
        return ERR_INVALID;
    }
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao iniciar migracao ZAPP no appcheck");
        return result;
    }

    shell_appcheck_migration_app = app;
    shell_appcheck_migration_pid = pid;
    return OK;
}

static void shell_appcheck_finish_migration(const app_loader_result_t* result) {
    shell_builtin_app_t completed_app = shell_appcheck_migration_app;
    int validation = shell_appcheck_migration_is_valid(result);

    cmd_appcheck_print_result(
        completed_app == SHELL_BUILTIN_APP_UPTIME ?
            "migracao_uptime_conclusao" : "migracao_mem_conclusao",
        validation);
    shell_appcheck_migration_app = SHELL_BUILTIN_APP_NONE;
    shell_appcheck_migration_pid = 0;

    if (completed_app == SHELL_BUILTIN_APP_UPTIME &&
        shell_appcheck_start_migration(SHELL_BUILTIN_APP_MEM) == OK) {
        return;
    }
    if (completed_app == SHELL_BUILTIN_APP_MEM) {
        paging_user_stats_t paging;
        page_fault_stats_t faults;
        uint32_t image_size = shell_build_vma_test_image();
        uint32_t pid = 0U;
        int start_result;

        shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_VMA);
        paging_get_user_stats(&paging);
        shell_appcheck_vma_initial_pages = paging.active_pages;
        shell_appcheck_vma_initial_directories = paging.active_directories;
        shell_appcheck_vma_initial_users = process_get_user_count();
        shell_appcheck_vma_initial_zombies =
            process_get_state_count(PROCESS_STATE_ZOMBIE);
        if (process_vma_get_page_fault_stats(&faults) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar page faults do appcheck");
            faults.handled = 0U;
            faults.invalid = 0U;
        }
        shell_appcheck_vma_initial_faults = faults;
        start_result = app_loader_run_image("VMAMM3.ZAP", appcheck_demo_image,
                                            image_size, 0, &pid);
        cmd_appcheck_print_result("vma_ring3_inicio", start_result);
        if (start_result == OK) {
            paging_user_stats_t after_create;

            paging_get_user_stats(&after_create);
            cmd_appcheck_print_result(
                "vma_lazy_reserva",
                after_create.active_pages == shell_appcheck_vma_initial_pages &&
                after_create.active_directories ==
                    shell_appcheck_vma_initial_directories + 1U ?
                    OK : ERR_STATE);
            shell_appcheck_vma_pid = pid;
            return;
        }
    }
    if (!shell_job_is_active()) shell_runtime_finish_command();
}

static int shell_appcheck_vma_is_valid(const app_loader_result_t* result) {
    paging_user_stats_t paging;
    page_fault_stats_t faults;

    if (!result || result->start_failed || result->faulted ||
        result->cancelled || result->exit_code != APP_EXIT_SUCCESS ||
        !result->focus_acquired || result->pid == 0U) {
        LOG_ERROR("SHELL", "Fixture VMA ring 3 terminou com falha");
        return ERR_STATE;
    }
    paging_get_user_stats(&paging);
    if (paging.active_pages != shell_appcheck_vma_initial_pages ||
        paging.active_directories != shell_appcheck_vma_initial_directories ||
        process_get_user_count() != shell_appcheck_vma_initial_users ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) !=
            shell_appcheck_vma_initial_zombies) {
        LOG_ERROR("SHELL", "Fixture VMA deixou recursos de paging residuais");
        return ERR_STATE;
    }
    if (process_vma_get_page_fault_stats(&faults) != OK ||
        faults.handled <= shell_appcheck_vma_initial_faults.handled ||
        faults.invalid != shell_appcheck_vma_initial_faults.invalid) {
        LOG_ERROR("SHELL", "Fixture VMA nao exercitou faults lazy esperadas");
        return ERR_STATE;
    }
    return OK;
}

static int shell_appcheck_fault_is_valid(const app_loader_result_t* result) {
    paging_user_stats_t paging;
    page_fault_stats_t faults;

    if (!result || !result->faulted || result->start_failed ||
        result->cancelled ||
        result->exit_code != APP_EXIT_FROM_SIGNAL(APP_SIGNAL_SEGV) ||
        result->termination_signal != APP_SIGNAL_SEGV ||
        !result->focus_acquired || result->pid == 0U) {
        LOG_ERROR("SHELL", "Fixture de page fault invalida nao foi isolada");
        return ERR_STATE;
    }
    paging_get_user_stats(&paging);
    if (paging.active_pages != shell_appcheck_vma_initial_pages ||
        paging.active_directories != shell_appcheck_vma_initial_directories ||
        process_get_user_count() != shell_appcheck_vma_initial_users ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) !=
            shell_appcheck_vma_initial_zombies) {
        LOG_ERROR("SHELL", "Fixture de page fault deixou recursos residuais");
        return ERR_STATE;
    }
    if (process_vma_get_page_fault_stats(&faults) != OK ||
        faults.handled <= shell_appcheck_vma_initial_faults.handled ||
        faults.invalid != shell_appcheck_vma_initial_faults.invalid + 1U) {
        LOG_ERROR("SHELL", "Contadores de page fault invalidos divergiram");
        return ERR_STATE;
    }
    return OK;
}

static uint8_t shell_appcheck_label_is_unavailable(const char* label) {
    if (kstrcmp(label, "file_service_indisponivel") == 0) {
        return fs_get_type() == FS_TYPE_NONE ? 1U : 0U;
    }
    if (kstrcmp(label, "loader_indisponivel") == 0) {
        return app_loader_is_ready() ? 0U : 1U;
    }
    return 0U;
}

static void cmd_appcheck_print_result(const char* label, int result) {
    uint8_t optional_unavailable;

    if (shell_appcheck_summary.compact) {
        optional_unavailable = shell_appcheck_label_is_unavailable(label);
        shell_appcheck_record_result(
            label, result, optional_unavailable ? ERR_UNAVAILABLE : OK,
            optional_unavailable);
        return;
    }
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(" retorno=", 0x08);
    shell_command_print_num((uint32_t)result);
    video_print(result == OK ? " OK\n" : " ERRO\n", result == OK ? 0x0A : 0x0C);
}

static void cmd_appcheck_print_expected_result(const char* label, int actual,
                                               int expected) {
    int passed = actual == expected;

    if (shell_appcheck_summary.compact) {
        shell_appcheck_record_result(
            label, actual, expected,
            shell_appcheck_label_is_unavailable(label));
        return;
    }
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(" retorno=", 0x08);
    shell_command_print_num((uint32_t)actual);
    video_print(" esperado=", 0x08);
    shell_command_print_num((uint32_t)expected);
    video_print(passed ? " OK\n" : " ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_appcheck_print_result_with_expectation(const char* label,
                                                        int actual,
                                                        int expected) {
    if (shell_appcheck_summary.compact) {
        shell_appcheck_record_result(
            label, actual, expected,
            shell_appcheck_label_is_unavailable(label));
        return;
    }
    cmd_appcheck_print_result(label, actual);
}

static void cmd_appcheck_files(void) {
    char file_name[FS_MAX_FILENAME];
    uint8_t buffer[64];
    uint32_t file_size = 0;
    uint32_t bytes_read = 0;
    uint32_t second_read = 0;
    uint32_t written = 0;
    uint32_t position = 0;
    uint8_t attributes = 0;
    app_handle_t handle = APP_HANDLE_INVALID;
    int result = ERR_NOT_FOUND;
    int count = fs_get_file_count();
    int selected = -1;

    if (fs_get_type() == FS_TYPE_NONE) {
        cmd_appcheck_print_result("file_service_indisponivel", ERR_UNAVAILABLE);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (fs_get_file_info(i, file_name, &file_size, &attributes) == OK &&
            !(attributes & 0x10)) {
            selected = i;
            break;
        }
    }

    if (selected >= 0) {
        fs_get_file_info(selected, file_name, &file_size, &attributes);
        result = syscall_invoke_kernel(APP_SYSCALL_FILE_OPEN,
                                        (uint32_t)file_name,
                                        APP_FILE_MODE_READ,
                                        (uint32_t)&handle, 0, 0);
        cmd_appcheck_print_result("file_open", result);
        if (result == OK) {
            result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                            handle, (uint32_t)buffer,
                                            sizeof(buffer),
                                            (uint32_t)&bytes_read, 0);
            cmd_appcheck_print_result("file_read", result);
            if (result == OK && !shell_appcheck_summary.compact) {
                video_print("    bytes=", 0x08);
                shell_command_print_num(bytes_read);
                video_print("\n", 0x07);
            }

            result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                            handle, (uint32_t)buffer,
                                            sizeof(buffer),
                                            (uint32_t)&second_read, 0);
            cmd_appcheck_print_result("file_read_sequencial", result);
            result = syscall_invoke_kernel(APP_SYSCALL_FILE_LSEEK,
                                            handle, 0,
                                            APP_SEEK_SET,
                                            (uint32_t)&position, 0);
            cmd_appcheck_print_result("file_lseek", result);
            result = syscall_invoke_kernel(APP_SYSCALL_FSYNC,
                                            handle, 0, 0, 0, 0);
            cmd_appcheck_print_result("file_fsync", result);
            result = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE,
                                            handle, 0, 0, 0, 0);
            cmd_appcheck_print_result("file_close", result);
            result = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE,
                                            handle, 0, 0, 0, 0);
            cmd_appcheck_print_result_with_expectation("file_close_expirado",
                                                       result, ERR_INVALID);
        }
    } else {
        cmd_appcheck_print_result_with_expectation("file_open_sem_arquivo",
                                                   ERR_NOT_FOUND,
                                                   ERR_NOT_FOUND);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_OPEN,
                                    (uint32_t)"ZZZZZZZ9.NOF",
                                    APP_FILE_MODE_READ,
                                    (uint32_t)&handle, 0, 0);
    cmd_appcheck_print_result_with_expectation("file_open_inexistente", result,
                                               ERR_NOT_FOUND);

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                    APP_HANDLE_INVALID, (uint32_t)buffer,
                                    APP_API_MAX_FILE_IO_SIZE + 1,
                                    (uint32_t)&bytes_read, 0);
    cmd_appcheck_print_result_with_expectation("file_read_grande", result,
                                               ERR_OVERFLOW);

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                    APP_HANDLE_INVALID, 0, 1,
                                    (uint32_t)&bytes_read, 0);
    cmd_appcheck_print_result_with_expectation("file_read_handle_invalido",
                                               result, ERR_NULL);

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_WRITE,
                                    APP_HANDLE_INVALID, 0, 1,
                                    (uint32_t)&written, 0);
    cmd_appcheck_print_result_with_expectation("file_write_nulo", result,
                                               ERR_NULL);
}

static void cmd_appcheck_pipes(void) {
    app_handle_t fds[2] = {APP_HANDLE_INVALID, APP_HANDLE_INVALID};
    int result;

    result = syscall_invoke_kernel(APP_SYSCALL_PIPE, (uint32_t)fds,
                                    0U, 0U, 0U, 0U);
    cmd_appcheck_print_result("pipe_syscall", result);
    if (result == OK) {
        int read_close = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE,
                                                fds[0], 0U, 0U, 0U, 0U);
        int write_close = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE,
                                                 fds[1], 0U, 0U, 0U, 0U);
        result = read_close != OK ? read_close : write_close;
        cmd_appcheck_print_result("pipe_syscall_cleanup", result);
    }
    result = syscall_invoke_kernel(APP_SYSCALL_PIPE, 0U, 0U, 0U, 0U, 0U);
    cmd_appcheck_print_expected_result("pipe_pointer_nulo", result, ERR_NULL);
    cmd_appcheck_print_result("pipe_state", vfs_validate_state());
}

static void cmd_appcheck_paths(void) {
    char original[VFS_MAX_PATH];
    char current[VFS_MAX_PATH];
    int result;

    original[0] = '/';
    original[1] = '\0';
    current[0] = '\0';
    result = syscall_invoke_kernel(APP_SYSCALL_GETCWD,
                                   (uint32_t)original, sizeof(original),
                                   0, 0, 0);
    cmd_appcheck_print_result("getcwd", result);
    result = syscall_invoke_kernel(APP_SYSCALL_CHDIR,
                                   (uint32_t)"/mnt", 0, 0, 0, 0);
    cmd_appcheck_print_result("chdir", result);
    result = syscall_invoke_kernel(APP_SYSCALL_GETCWD,
                                   (uint32_t)current, sizeof(current),
                                   0, 0, 0);
    if (result == OK && kstrcmp(current, "/mnt") != 0) result = ERR_STATE;
    cmd_appcheck_print_result("getcwd_relativo", result);
    result = syscall_invoke_kernel(APP_SYSCALL_CHDIR,
                                   (uint32_t)original, 0, 0, 0, 0);
    cmd_appcheck_print_result("chdir_restauracao", result);
    result = syscall_invoke_kernel(APP_SYSCALL_CHDIR,
                                   (uint32_t)"/arquivo-inexistente", 0,
                                   0, 0, 0);
    cmd_appcheck_print_expected_result("chdir_inexistente", result,
                                       ERR_NOT_FOUND);
}

static void cmd_appcheck_devices(void) {
    app_handle_t handle = APP_HANDLE_INVALID;
    int result = syscall_invoke_kernel(
        APP_SYSCALL_FILE_OPEN, (uint32_t)"/dev/speaker",
        APP_FILE_MODE_WRITE, (uint32_t)&handle, 0U, 0U);

    cmd_appcheck_print_result("device_open", result);
    if (result != OK) return;
    result = syscall_invoke_kernel(APP_SYSCALL_FILE_IOCTL, handle,
                                   APP_IOCTL_SPEAKER_STOP, 0U, 0U, 0U);
    cmd_appcheck_print_result("file_ioctl", result);
    result = syscall_invoke_kernel(APP_SYSCALL_FILE_IOCTL, handle,
                                   APP_CHECK_INVALID_IOCTL, 0U, 0U, 0U);
    cmd_appcheck_print_expected_result("file_ioctl_invalido", result,
                                       ERR_INVALID);
    result = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE, handle,
                                   0U, 0U, 0U, 0U);
    cmd_appcheck_print_result("device_close", result);
}

static void cmd_appcheck_ipc(void) {
    app_message_t message;
    app_message_t received;
    uint32_t current_pid = process_get_current_pid();
    int result;

    message.type = APP_MESSAGE_KEYBOARD;
    message.data1 = 0x41;
    message.data2 = 0;
    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_SEND,
                                    current_pid, (uint32_t)&message,
                                    0, 0, 0);
    cmd_appcheck_print_result("message_send", result);

    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_RECEIVE,
                                    (uint32_t)&received, 0, 0, 0, 0);
    cmd_appcheck_print_result("message_receive", result);

    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_SEND,
                                    0xFFFFFFFFU, (uint32_t)&message,
                                    0, 0, 0);
    cmd_appcheck_print_result_with_expectation("message_pid_invalido", result,
                                               ERR_NOT_FOUND);

    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_SEND,
                                    current_pid, 0, 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("message_nula", result,
                                               ERR_NULL);

    message.type = 0;
    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_SEND,
                                    current_pid, (uint32_t)&message,
                                    0, 0, 0);
    cmd_appcheck_print_result_with_expectation("message_tipo_invalido", result,
                                               ERR_INVALID);
}

static void cmd_appcheck_launch(void) {
    static const char valid_args[] = "alpha beta";
    static const char too_many_args[] = "1 2 3 4 5 6 7 8 9";
    int result;

    result = app_loader_build_launch_info(valid_args, &appcheck_launch_info);
    if (result == OK &&
        (appcheck_launch_info.argc != 2U ||
         appcheck_launch_info.raw_length != kstrlen(valid_args) ||
         appcheck_launch_info.args[0].offset != 0U ||
         appcheck_launch_info.args[0].length == 0U ||
         appcheck_launch_info.args[1].offset <=
             appcheck_launch_info.args[0].offset)) {
        result = ERR_STATE;
    }
    cmd_appcheck_print_result("loader_argumentos_validos", result);

    result = app_loader_build_launch_info("", &appcheck_launch_info);
    if (result == OK && (appcheck_launch_info.argc != 0U ||
                         appcheck_launch_info.raw_length != 0U)) {
        result = ERR_STATE;
    }
    cmd_appcheck_print_result("loader_argumentos_vazios", result);

    result = app_loader_build_launch_info(too_many_args,
                                          &appcheck_launch_info);
    cmd_appcheck_print_result_with_expectation("loader_argumentos_excesso",
                                               result, ERR_OVERFLOW);

    result = app_loader_build_launch_info(appcheck_oversized_args,
                                          &appcheck_launch_info);
    cmd_appcheck_print_result_with_expectation("loader_argumentos_grandes",
                                               result, ERR_OVERFLOW);
}

static void cmd_appcheck_loader(void) {
    app_image_header_t header;
    uint32_t image_size;
    uint32_t pid = 0;
    uint32_t migrated_pid = 0;
    int result;

    image_size = shell_build_demo_image();
    result = app_loader_validate_image(appcheck_demo_image, image_size,
                                       &header);
    cmd_appcheck_print_result("loader_validacao", result);

    header.entry_offset = header.code_size;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    result = app_loader_validate_image(appcheck_demo_image, image_size,
                                       &header);
    cmd_appcheck_print_result_with_expectation("loader_entry_invalida", result,
                                               ERR_INVALID);

    shell_build_demo_image();
    kmemcpy(&header, appcheck_demo_image, APP_IMAGE_HEADER_SIZE);
    header.flags = 1U;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    result = app_loader_validate_image(appcheck_demo_image, image_size,
                                       &header);
    cmd_appcheck_print_result_with_expectation("loader_flags_invalidas", result,
                                               ERR_INVALID);

    shell_build_demo_image();
    kmemcpy(&header, appcheck_demo_image, APP_IMAGE_HEADER_SIZE);
    header.code_size = APP_IMAGE_MAX_CODE_SIZE + 1U;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    result = app_loader_validate_image(appcheck_demo_image, image_size,
                                       &header);
    cmd_appcheck_print_result_with_expectation("loader_codigo_grande", result,
                                               ERR_OVERFLOW);

    shell_build_demo_image();
    result = app_loader_validate_image(appcheck_demo_image,
                                       APP_IMAGE_HEADER_SIZE - 1U,
                                       &header);
    cmd_appcheck_print_result_with_expectation("loader_cabecalho_curto", result,
                                               ERR_INVALID);

    if (!app_loader_is_ready()) {
        cmd_appcheck_print_result_with_expectation("loader_indisponivel",
                                                   ERR_UNAVAILABLE,
                                                   ERR_UNAVAILABLE);
        return;
    }

    shell_remove_image(APP_CHECK_DEMO_PATH);

    result = fs_write_file_at(APP_CHECK_DEMO_PATH,
                              appcheck_demo_image, image_size);
    cmd_appcheck_print_result("loader_demo_gravacao", result);
    if (result == OK) {
        result = shell_verify_image(APP_CHECK_DEMO_PATH, image_size);
        cmd_appcheck_print_result("loader_demo_integridade", result);
        if (result == OK) {
            result = app_loader_build_launch_info(
                "appcheck alpha beta", &appcheck_launch_info);
            cmd_appcheck_print_result("loader_argumentos_execucao", result);
        }
        if (result == OK) {
            result = app_loader_run_file_with_launch(APP_CHECK_DEMO_PATH,
                                                     &appcheck_launch_info,
                                                     &pid);
            cmd_appcheck_print_result("loader_demo_execucao", result);
            if (result == OK) {
                shell_appcheck_loader_pid = pid;
                if (!shell_appcheck_summary.compact) {
                    video_print("    pid=", 0x08);
                    shell_command_print_num(pid);
                    video_print(" assincrono\n", 0x07);
                }
                result = app_builtin_run_uptime(&migrated_pid);
                cmd_appcheck_print_expected_result(
                    "migracao_uptime_concorrente", result, ERR_STATE);
                result = app_builtin_run_mem(&migrated_pid);
                cmd_appcheck_print_expected_result(
                    "migracao_mem_concorrente", result, ERR_STATE);
            }
        }
        shell_remove_image(APP_CHECK_DEMO_PATH);
        result = fs_read_file(APP_CHECK_DEMO_PATH, appcheck_demo_image, 1);
        if (result >= 0 && result != ERR_NOT_FOUND) {
            LOG_ERROR("SHELL", "Arquivo temporario ZAPP permaneceu apos limpeza");
            result = ERR_STATE;
        } else {
            result = OK;
        }
        cmd_appcheck_print_result("loader_demo_remocao", result);
    }
}

static void cmd_appcheck(void) {
    app_api_version_t version;
    app_uptime_info_t uptime;
    app_memory_info_t memory;
    const char* message = "appcheck: console_write OK\n";
    int result;

    kmemset(appcheck_oversized_text, 'A', sizeof(appcheck_oversized_text));
    kmemset(appcheck_oversized_args, 'A', APP_LAUNCH_MAX_TEXT);
    appcheck_oversized_args[APP_LAUNCH_MAX_TEXT] = '\0';
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_API);
    if (!shell_appcheck_summary.compact) {
        video_print("Teste da API de aplicativos:\n", 0x0B);
    }

    result = app_api_get_version(&version);
    cmd_appcheck_print_result("get_version", result);
    if (result == OK && !shell_appcheck_summary.compact) {
        video_print("    versao=", 0x08);
        shell_command_print_num(version.major);
        video_print(".", 0x08);
        shell_command_print_num(version.minor);
        video_print("\n", 0x07);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_CONSOLE_WRITE,
                                    (uint32_t)message,
                                    kstrlen(message), 0, 0, 0);
    cmd_appcheck_print_result("console_write", result);

    result = syscall_invoke_kernel(APP_SYSCALL_UPTIME,
                                    (uint32_t)&uptime, 0, 0, 0, 0);
    cmd_appcheck_print_result("get_uptime", result);
    if (result == OK && !shell_appcheck_summary.compact) {
        video_print("    ticks=", 0x08);
        shell_command_print_num(uptime.ticks);
        video_print(" segundos=", 0x08);
        shell_command_print_num(uptime.seconds);
        video_print("\n", 0x07);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_MEMORY_INFO,
                                    (uint32_t)&memory, 0, 0, 0, 0);
    cmd_appcheck_print_result("get_memory_info", result);
    if (result == OK && !shell_appcheck_summary.compact) {
        video_print("    total_kb=", 0x08);
        shell_command_print_num(memory.total_bytes / 1024);
        video_print(" livre_kb=", 0x08);
        shell_command_print_num(memory.free_bytes / 1024);
        video_print(" paginas_livres=", 0x08);
        shell_command_print_num(memory.free_pages);
        video_print("\n", 0x07);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_INVALID, 0, 0, 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("numero invalido", result,
                                               ERR_INVALID);
    result = syscall_invoke_kernel(APP_SYSCALL_UPTIME, 0, 0, 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("uptime nulo", result,
                                               ERR_NULL);
    result = syscall_invoke_kernel(APP_SYSCALL_MEMORY_INFO, 0, 0, 0, 0, 0);
    cmd_appcheck_print_expected_result("memory_info nulo", result, ERR_NULL);
    result = syscall_invoke_kernel(APP_SYSCALL_CONSOLE_WRITE,
                                    (uint32_t)"", 0, 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("console_write vazio", result,
                                               ERR_INVALID);
    result = syscall_invoke_kernel(APP_SYSCALL_CONSOLE_WRITE,
                                    (uint32_t)appcheck_oversized_text,
                                    sizeof(appcheck_oversized_text), 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("console_write grande", result,
                                               ERR_OVERFLOW);
    result = syscall_invoke_kernel(APP_SYSCALL_PROCESS_EXIT,
                                    0, 0, 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("process_exit", result,
                                               ERR_UNAVAILABLE);
    result = syscall_invoke_kernel(APP_SYSCALL_FSYNC,
                                    APP_HANDLE_INVALID, 0, 0, 0, 0);
    cmd_appcheck_print_result_with_expectation("fsync invalido", result,
                                               ERR_INVALID);
    result = syscall_invoke_kernel(APP_SYSCALL_SYNC, 0, 0, 0, 0, 0);
    cmd_appcheck_print_result("sync global", result);
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_FILES);
    cmd_appcheck_files();
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_PIPES);
    cmd_appcheck_pipes();
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_PATHS);
    cmd_appcheck_paths();
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_DEVICES);
    cmd_appcheck_devices();
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_IPC);
    cmd_appcheck_ipc();
    shell_appcheck_set_phase(SHELL_APPCHECK_PHASE_LOADER);
    cmd_appcheck_launch();
    cmd_appcheck_loader();
}

static void cmd_q2check(void) {
    int result;

    if (shell_q2check.state != SHELL_Q2CHECK_IDLE ||
        shell_waiting_user_test || app_loader_is_foreground_active() ||
        process_get_user_count() != 0 ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) != 0) {
        LOG_WARN("SHELL", "Q2check recusado com processo ring 3 pendente");
        video_print("Q2Check indisponivel: aguarde processos ring 3 terminarem.\n",
                    0x0E);
        return;
    }

    shell_q2check_reset();
    shell_q2check.initial_focus = process_get_focus();
    shell_q2check.initial_user_count = process_get_user_count();
    shell_q2check.initial_zombie_count =
        process_get_state_count(PROCESS_STATE_ZOMBIE);
    shell_q2check.initial_fault_count = process_get_user_fault_count();
    shell_q2check.logger_result = log_get_level() == LOG_LEVEL_INFO ?
                                  OK : ERR_STATE;
    if (shell_q2check.logger_result != OK) {
        LOG_WARN("SHELL", "Q2check encontrou nivel de log inesperado");
    }
    shell_q2check.summary_result = OK;
    shell_q2check.cleanup_result = OK;
    shell_waiting_user_test = 1;
    result = shell_q2check_start_fault(SHELL_Q2CHECK_FIRST_FAULT_INDEX);
    if (result == OK) return;

    shell_q2check.fault_result[SHELL_Q2CHECK_FIRST_FAULT_INDEX] = result;
    shell_q2check_finish();
}

static void cmd_regcheck(const char* args) {
    paging_user_stats_t paging;
    int full_mode = shell_command_args_equal(args, "full");

    if (*args && !full_mode) {
        LOG_WARN("SHELL", "Uso invalido de regcheck");
        video_print("Uso: regcheck [full]\n", 0x0C);
        return;
    }
    if (shell_regcheck.state != SHELL_REGCHECK_IDLE ||
        shell_q2check.state != SHELL_Q2CHECK_IDLE || shell_waiting_user_test ||
        app_loader_is_foreground_active() || process_get_user_count() != 0 ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) != 0) {
        LOG_WARN("SHELL", "RegCheck recusado com diagnostico ou ring 3 pendente");
        video_print("RegCheck indisponivel: aguarde diagnosticos e processos terminarem.\n",
                    0x0C);
        return;
    }

    shell_regcheck_reset();
    shell_regcheck.full_mode = full_mode ? 1U : 0U;
    shell_regcheck.initial_focus = process_get_focus();
    shell_regcheck.initial_process_count = process_get_count();
    shell_regcheck.initial_user_count = process_get_user_count();
    shell_regcheck.initial_zombie_count =
        process_get_state_count(PROCESS_STATE_ZOMBIE);
    paging_get_user_stats(&paging);
    shell_regcheck.initial_user_directories = paging.active_directories;
    shell_regcheck.initial_user_pages = paging.active_pages;
    shell_regcheck.state = shell_regcheck.full_mode ?
                           SHELL_REGCHECK_PREPARE_FULL :
                           SHELL_REGCHECK_PREPARE_BASE;
}

void shell_checks_report_user_test_result(void) {
    uint32_t pid;
    uint32_t faulted;

    if (!video_terminal_is_active()) return;
    if (process_take_user_test_result(&pid, &faulted) != OK) return;

    if (shell_checks_cancel_requested) {
        shell_q2check_reset();
        shell_waiting_user_test = 0;
        shell_user_test_pid = 0;
        process_reap_finished_user();
        shell_runtime_reset_input();
        return;
    }

    if (shell_q2check.state != SHELL_Q2CHECK_IDLE) {
        shell_q2check_handle_user_test_result(pid, faulted);
        return;
    }

    video_print("\n[", 0x08);
    video_print(faulted ? "WARN" : "INFO", faulted ? 0x0E : 0x0A);
    video_print("] UserTest PID ", 0x08);
    shell_command_print_num(pid);
    video_print(faulted ? " encerrado apos falha isolada.\n" :
                         " encerrado com sucesso.\n", 0x07);
    shell_waiting_user_test = 0;
    shell_user_test_pid = 0;
    shell_runtime_reset_input();
    if (!shell_job_is_active()) shell_print_prompt();
    process_reap_finished_user();
}

static void cmd_usertest(const char* args) {
    uint32_t pid = 0;
    int trigger_fault = args && kstrcmp(args, "fault") == 0;
    int result = process_create_user_test(trigger_fault, &pid);

    if (result != OK) {
        video_print("Erro: nao foi possivel criar o teste ring 3 (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print(trigger_fault ? "UserTest fault criado, PID " :
                               "UserTest criado, PID ", 0x0A);
    shell_command_print_num(pid);
    video_print(".\n", 0x0A);
    shell_waiting_user_test = 1;
    shell_user_test_pid = pid;
}

int shell_checks_input_blocked(void) {
    return shell_waiting_user_test;
}

int shell_checks_should_cancel_focused_user(uint8_t scancode) {
    return shell_job_is_active() &&
           shell_regcheck.state == SHELL_REGCHECK_WAIT_CANCEL &&
           scancode == SHELL_REGCHECK_SCANCODE_F11 &&
           app_loader_is_foreground_active();
}

int shell_checks_handle_job_key(uint8_t scancode) {
    int result;

    if (!shell_checks_should_cancel_focused_user(scancode)) return 0;

    result = app_loader_cancel_foreground(PROCESS_EXIT_CANCELLED);
    if (result != OK) {
        LOG_WARN("SHELL", "Falha ao cancelar ZAPP do RegCheck com F11");
        shell_job_request_cancel();
    } else {
        LOG_INFO("SHELL", "F11 solicitou cancelamento do ZAPP do RegCheck");
    }
    return 1;
}

int shell_checks_handle_loader_result(const app_loader_result_t* result) {
    int appcheck_result;
    process_t* current;

    if (!result) return 0;
    if (result->generation &&
        (!shell_job_generation_matches(shell_checks_job_generation) ||
         result->generation != shell_checks_job_generation)) {
        shell_job_note_stale_event(result->generation);
        LOG_WARN("SHELL", "Resultado do App Loader pertence a geracao antiga");
        return 1;
    }

    if (shell_checks_cancel_requested) {
        if (shell_regcheck.state != SHELL_REGCHECK_IDLE) {
            shell_regcheck_reset();
            shell_runtime_reset_input();
            return 1;
        }
        if (shell_appcheck_loader_pid == result->pid) {
            shell_appcheck_loader_pid = 0;
            shell_appcheck_migration_app = SHELL_BUILTIN_APP_NONE;
            shell_runtime_reset_input();
            return 1;
        }
        if (shell_appcheck_migration_pid == result->pid) {
            shell_appcheck_migration_pid = 0;
            shell_appcheck_migration_app = SHELL_BUILTIN_APP_NONE;
            shell_runtime_reset_input();
            return 1;
        }
        if (shell_appcheck_vma_pid == result->pid) {
            shell_appcheck_vma_pid = 0;
            shell_runtime_reset_input();
            return 1;
        }
        if (shell_appcheck_fault_pid == result->pid) {
            shell_appcheck_fault_pid = 0;
            shell_runtime_reset_input();
            return 1;
        }
    }

    if (shell_regcheck.state != SHELL_REGCHECK_IDLE) {
        shell_regcheck_handle_loader_result(result);
        return 1;
    }

    if (shell_appcheck_loader_pid == result->pid) {
        cmd_appcheck_print_result("loader_foco_aquisicao",
                                  result->focus_acquired ? OK : ERR_STATE);
        current = process_get_current();
        appcheck_result = (!result->faulted && !result->cancelled &&
                           !result->start_failed && result->exit_code == OK &&
                           current && process_get_focus() == current->pid) ?
                          OK : ERR_STATE;
        cmd_appcheck_print_result("loader_foco_retorno", appcheck_result);
        shell_appcheck_loader_pid = 0;
        if (shell_appcheck_start_migration(SHELL_BUILTIN_APP_UPTIME) == OK) {
            return 1;
        }
        if (!shell_job_is_active()) shell_runtime_finish_command();
        return 1;
    }

    if (shell_appcheck_migration_pid == result->pid) {
        shell_appcheck_finish_migration(result);
        return 1;
    }

    if (shell_appcheck_vma_pid == result->pid) {
        appcheck_result = shell_appcheck_vma_is_valid(result);
        cmd_appcheck_print_result("vma_ring3_conclusao", appcheck_result);
        shell_appcheck_vma_pid = 0;
        if (appcheck_result == OK) {
            uint32_t image_size = shell_build_pagefault_fault_image();
            uint32_t pid = 0U;
            int start_result;

            if (process_vma_get_page_fault_stats(
                    &shell_appcheck_vma_initial_faults) != OK) {
                LOG_ERROR("SHELL", "Falha ao preparar fixture de page fault");
                shell_appcheck_vma_initial_faults.handled = 0U;
                shell_appcheck_vma_initial_faults.invalid = 0U;
            }
            start_result = app_loader_run_image(
                "MM3FAULT.ZAP", appcheck_demo_image, image_size, 0, &pid);
            cmd_appcheck_print_result("pagefault_invalida_inicio",
                                      start_result);
            if (start_result == OK) {
                shell_appcheck_fault_pid = pid;
                return 1;
            }
        }
        if (!shell_job_is_active()) shell_runtime_finish_command();
        return 1;
    }

    if (shell_appcheck_fault_pid == result->pid) {
        appcheck_result = shell_appcheck_fault_is_valid(result);
        cmd_appcheck_print_result("pagefault_invalida_conclusao",
                                  appcheck_result);
        shell_appcheck_fault_pid = 0;
        if (!shell_job_is_active()) shell_runtime_finish_command();
        return 1;
    }

    return 0;
}

int shell_checks_start_job(const char* command) {
    int active;

    if (!command ||
        (kstrcmp(command, "q2check") != 0 &&
         kstrcmp(command, "regcheck") != 0 &&
         kstrcmp(command, "regcheck full") != 0 &&
         kstrcmp(command, "appcheck") != 0 &&
         kstrcmp(command, "appcheck compact") != 0 &&
         kstrcmp(command, "usertest") != 0)) {
        return 0;
    }
    active = shell_q2check.state != SHELL_Q2CHECK_IDLE ||
             shell_regcheck.state != SHELL_REGCHECK_IDLE ||
             shell_appcheck_loader_pid != 0U ||
             shell_appcheck_migration_pid != 0U ||
             shell_appcheck_vma_pid != 0U ||
             shell_appcheck_fault_pid != 0U;
    if (kstrcmp(command, "usertest") == 0) {
        active = active || shell_waiting_user_test;
    }
    if (!active) return 0;
    shell_checks_cancel_requested = 0;
    shell_checks_cancel_started = 0;
    keyboard_set_focus_cancel_filter(shell_checks_should_cancel_focused_user);
    if (shell_job_start(&shell_checks_job_definition, command) != OK) {
        LOG_WARN("SHELL", "Job de diagnostico nao foi registrado");
        keyboard_set_focus_cancel_filter(0);
        if (shell_regcheck_is_preparing()) {
            shell_regcheck_reset();
            shell_runtime_reset_input();
        }
    } else {
        shell_checks_job_generation = shell_job_get_generation();
        app_loader_set_operation_generation(shell_checks_job_generation);
    }
    return 1;
}

#define SHELL_CHECKS_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

#define SHELL_CHECKS_WRAP_NO_ARGS(adapter, handler) \
    void adapter(const char* arguments) { (void)arguments; handler(); }

void shell_dispatch_cmd_q2check(const char* arguments) {
    (void)arguments;
    cmd_q2check();
    shell_checks_start_job("q2check");
}

void shell_dispatch_cmd_regcheck(const char* arguments) {
    cmd_regcheck(arguments);
    shell_checks_start_job(shell_command_args_equal(arguments, "full") ?
                           "regcheck full" : "regcheck");
}

void shell_dispatch_cmd_appcheck(const char* arguments) {
    uint8_t compact = 0U;
    const char* command = "appcheck";

    if (arguments && *arguments) {
        if (!shell_command_args_equal(arguments, "compact")) {
            LOG_WARN("SHELL", "Uso invalido de appcheck");
            video_print("Uso: appcheck [compact]\n", 0x0C);
            return;
        }
        compact = 1U;
        command = "appcheck compact";
    }
    shell_appcheck_summary_reset(compact);
    cmd_appcheck();
    if (compact && !shell_appcheck_has_pending_work()) {
        shell_appcheck_summary_finish(OK);
        return;
    }
    shell_checks_start_job(command);
}

void shell_dispatch_cmd_blkcheck(const char* arguments) {
    int result;

    if (!shell_command_args_equal(arguments, "")) {
        LOG_WARN("SHELL", "Uso invalido de blkcheck");
        video_print("Uso: blkcheck\n", 0x0C);
        return;
    }
    if (shell_job_is_active() || shell_blkcheck.active ||
        app_loader_is_foreground_active() || process_get_user_count() != 0U ||
        process_get_state_count(PROCESS_STATE_ZOMBIE) != 0U) {
        LOG_WARN("SHELL", "BLKCheck recusado com operacao concorrente");
        video_print("BLKCheck indisponivel: aguarde a operacao atual.\n",
                    0x0E);
        return;
    }
    kmemset(&shell_blkcheck, 0, sizeof(shell_blkcheck));
    shell_blkcheck.phase = SHELL_BLKCHECK_PHASE_BASELINE;
    shell_blkcheck.result = OK;
    shell_blkcheck.active = 1U;
    result = shell_job_start(&shell_blkcheck_job_definition, "blkcheck");
    if (result != OK) {
        shell_blkcheck.active = 0U;
        LOG_WARN("SHELL", "Job BLKCheck nao foi iniciado");
        video_print("BLKCheck indisponivel (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
    }
}

void shell_dispatch_cmd_usertest(const char* arguments) {
    cmd_usertest(arguments);
    shell_checks_start_job("usertest");
}

#undef SHELL_CHECKS_WRAP_ARGS
#undef SHELL_CHECKS_WRAP_NO_ARGS
