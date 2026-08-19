#include "apps/shell.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/timer.h"
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
#include "core/log.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "apps/guitest.h"
#include "core/recovery.h"
#include "core/device_manager.h"
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
#include "ui/display.h"

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
#define SHELL_NET_CHECK_BLOCK_TICKS 1U
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
#define SHELL_COMMAND_HISTORY_CAPACITY 16U
#define SHELL_INDEX_ACTION_SIZE 16U
#define SHELL_FS_DIRECTORY_ATTRIBUTE 0x10U
#define SHELL_LOG_TAIL_DEFAULT 10U
#define SHELL_LOG_TAIL_MAXIMUM 16U

typedef enum {
    SHELL_BUILTIN_APP_NONE,
    SHELL_BUILTIN_APP_UPTIME,
    SHELL_BUILTIN_APP_MEM
} shell_builtin_app_t;

typedef enum {
    SHELL_Q2CHECK_IDLE = 0,
    SHELL_Q2CHECK_FIRST_FAULT,
    SHELL_Q2CHECK_SECOND_FAULT
} shell_q2check_state_t;

typedef enum {
    SHELL_REGCHECK_IDLE = 0,
    SHELL_REGCHECK_WAIT_DEMO,
    SHELL_REGCHECK_WAIT_F12
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
    int heap_integrity;
    int coalescence;
    int pmm_guards;
    int user_directories;
} shell_memcheck_result_t;

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
    int pci_result;
    int devices_result;
    int network_result;
} shell_device_scan_result_t;

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

static char input_buffer[SHELL_BUFFER_SIZE];
static char shell_command_history[SHELL_COMMAND_HISTORY_CAPACITY]
                                 [SHELL_BUFFER_SIZE];
static char shell_command_draft[SHELL_BUFFER_SIZE];
static log_record_t shell_log_records[SHELL_LOG_TAIL_MAXIMUM];
static char appcheck_oversized_text[APP_API_MAX_TEXT_SIZE + 1];
static uint8_t appcheck_demo_image[APP_IMAGE_MAX_FILE_SIZE];
static uint8_t appcheck_demo_verify[APP_IMAGE_MAX_FILE_SIZE];
static int input_pos = 0;
static uint32_t shell_history_count = 0;
static uint32_t shell_history_next = 0;
static uint32_t shell_history_depth = 0;
static int shell_waiting_user_test = 0;
static uint8_t shell_extended_scancode = 0;
static uint8_t shell_shift_mask = 0;
static uint8_t shell_prompt_visible = 0;
static uint8_t shell_hosted_visible = 0;
static uint32_t shell_appcheck_loader_pid = 0;
static uint32_t shell_echo_loader_pid = 0;
static uint32_t shell_builtin_loader_pid = 0;
static uint32_t shell_appcheck_migration_pid = 0;
static uint32_t shell_appcheck_user_count = 0;
static uint32_t shell_appcheck_zombie_count = 0;
static shell_builtin_app_t shell_builtin_loader_app = SHELL_BUILTIN_APP_NONE;
static shell_builtin_app_t shell_appcheck_migration_app = SHELL_BUILTIN_APP_NONE;
static char appcheck_oversized_args[APP_LAUNCH_MAX_TEXT + 1U];
static app_launch_info_t appcheck_launch_info;

static void shell_handle_terminal_key(uint8_t scancode);
static int shell_open_hosted(void);
static void shell_hosted_draw(int x, int y, int width, int height);
static void shell_hosted_key(uint8_t scancode);
static int shell_hosted_mouse(mouse_event_t* event, int x, int y,
                              int width, int height);
static void shell_hosted_close(void);
static int shell_is_hosted_visible(void);
static void shell_present_hosted_progress(void);
static void shell_suspend_terminal(void);
static void shell_history_reset_navigation(void);

static const wm_hosted_app_t shell_hosted_app = {
    WM_APP_SHELL, "ZephyrOS Shell", "Shell",
    WM_HOSTED_MIN_WIDTH, WM_HOSTED_MIN_HEIGHT,
    SHELL_HOSTED_DEFAULT_CONTENT_WIDTH + SHELL_HOSTED_FRAME_WIDTH,
    SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT + SHELL_HOSTED_FRAME_HEIGHT,
    WM_KEY_REDRAW_APPLICATION,
    shell_hosted_draw, shell_hosted_key, shell_hosted_mouse, shell_hosted_close
};
static shell_q2check_t shell_q2check;
static shell_regcheck_t shell_regcheck;
static shell_kmetrics_baseline_t shell_kmetrics_baseline;
/* O Shell tem pilha de 4 KiB; snapshots HTTP ficam em BSS para que uma
   espera cooperativa nao alcance os metadados do bloco de pilha no heap. */
static char shell_http_command_url[HTTP_URL_BUFFER_SIZE];
static char shell_http_preview[SHELL_HTTP_PREVIEW_SIZE + 1U];
static http_status_t shell_http_command_status;
static http_status_t shell_http_wait_status;
/* Parser, status e resultados U5 compartilham a mesma sessao do Shell.
   Mantê-los no BSS evita somar cerca de 1 KiB à pilha durante Ed25519. */
static shell_update_workspace_t shell_update_workspace;
/* Resultados AS2 e argumentos ZAPP excedem 1 KiB combinados. O workspace
   serializado evita repetir a pressao de stack detectada no appcheck AS1. */
static shell_store_workspace_t shell_store_workspace;
/* Os 64 resultados do indice excedem a pilha de 4 KiB do Shell. */
static shell_index_workspace_t shell_index_workspace;

#define SHELL_SCANCODE_EXTENDED 0xE0
#define SHELL_SCANCODE_LEFT_SHIFT 0x2AU
#define SHELL_SCANCODE_RIGHT_SHIFT 0x36U
#define SHELL_SCANCODE_LEFT_SHIFT_RELEASE 0xAAU
#define SHELL_SCANCODE_RIGHT_SHIFT_RELEASE 0xB6U
#define SHELL_SHIFT_LEFT_MASK 0x01U
#define SHELL_SHIFT_RIGHT_MASK 0x02U
#define SHELL_SCANCODE_ISO_SLASH 0x56U
#define SHELL_SCANCODE_ABNT2_SLASH 0x73U
#define SHELL_SCANCODE_UP       0x48
#define SHELL_SCANCODE_DOWN     0x50
#define SHELL_SCANCODE_PAGE_UP  0x49
#define SHELL_SCANCODE_PAGE_DOWN 0x51
#define SHELL_SCANCODE_HOME     0x47
#define SHELL_SCANCODE_END      0x4F
#define SHELL_SCROLL_PAGE_LINES 20
#define SHELL_WHEEL_SCROLL_LINES 3

static void print_num(uint32_t num);

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

static const char app_input_test_message[] =
    "Entrada ZAPP ativa: Enter encerra; F12 cancela.\n";

static void cmd_appcheck_print_result(const char* label, int result);
static void cmd_appcheck_print_expected_result(const char* label, int actual,
                                               int expected);
static int shell_should_show_prompt(void);
static int shell_run_memcheck(shell_memcheck_result_t* result_out);
static int shell_run_device_scan(shell_device_scan_result_t* scan);

static const char* shell_builtin_app_name(shell_builtin_app_t app) {
    if (app == SHELL_BUILTIN_APP_UPTIME) return "uptime";
    if (app == SHELL_BUILTIN_APP_MEM) return "mem";
    return "desconhecido";
}

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

/* O demo e conhecido pelo Shell. Conferir o round-trip evita executar
   codigo diferente do que acabou de ser gravado no FAT. */
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

static uint32_t shell_build_input_test_image(void) {
    app_image_header_t header;
    uint8_t* code = appcheck_demo_image + APP_IMAGE_HEADER_SIZE;
    uint32_t loop_offset;
    uint32_t offset = 0;
    uint32_t data_size = APP_INPUT_EVENT_OFFSET + sizeof(app_message_t);

    kmemset(appcheck_demo_image, 0, sizeof(appcheck_demo_image));
    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_CONSOLE_WRITE);
    shell_demo_emit_mov(code, &offset, 3, USER_DATA_BASE);
    shell_demo_emit_mov(code, &offset, 1, kstrlen(app_input_test_message));
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    loop_offset = offset;
    shell_demo_emit_mov(code, &offset, 0, APP_SYSCALL_MESSAGE_RECEIVE);
    shell_demo_emit_mov(code, &offset, 3, USER_DATA_BASE + APP_INPUT_EVENT_OFFSET);
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
    shell_demo_patch_u32(code, offset, 0x1CU);
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
    kmemcpy(appcheck_demo_image + header.data_offset, app_input_test_message,
            kstrlen(app_input_test_message));
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    return header.data_offset + header.data_size;
}

/* O ZAPP do RegCheck recebe eventos normalmente, mas F12 e tratado pelo
   runtime antes de chegar a fila do aplicativo. Se isso falhar, o proprio
   ZAPP encerra e o coletor registra a divergencia sem deixar o teste preso. */
static uint32_t shell_build_regcheck_input_image(void) {
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
    shell_demo_patch_u32(code, offset, 0x58U);
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

static void shell_reset_input(void) {
    input_pos = 0;
    shell_extended_scancode = 0;
    shell_shift_mask = 0;
    shell_history_reset_navigation();
    kmemset(input_buffer, 0, sizeof(input_buffer));
}

static int shell_is_hosted_visible(void) {
    return shell_hosted_visible && wm_is_active() &&
           desktop_get_mode() == DESKTOP_MODE_CLASSIC &&
           video_terminal_is_hosted();
}

static void shell_suspend_terminal_for_scene(void) {
    if (!shell_is_hosted_visible()) shell_suspend_terminal();
}

static void shell_resume_terminal(void) {
    if (!wm_is_active() && video_terminal_is_hosted()) {
        video_terminal_begin();
        return;
    }
    if (!video_terminal_is_active()) video_terminal_begin();
}

static void shell_return_to_terminal_tail(void) {
    shell_resume_terminal();
    if (video_terminal_is_scrolled()) video_terminal_scroll_end();
}

static void shell_history_copy(char* destination, const char* source) {
    uint32_t index = 0;

    if (!destination) return;
    if (!source) source = "";
    while (source[index] && index + 1U < SHELL_BUFFER_SIZE) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void shell_history_reset_navigation(void) {
    shell_history_depth = 0;
    shell_command_draft[0] = '\0';
}

static void shell_history_replace_input(const char* command) {
    uint32_t index = 0;

    shell_return_to_terminal_tail();
    while (input_pos > 0) {
        video_backspace();
        input_pos--;
    }
    kmemset(input_buffer, 0, sizeof(input_buffer));
    if (!command) return;
    while (command[index] && index + 1U < SHELL_BUFFER_SIZE) {
        input_buffer[index] = command[index];
        video_put_char(command[index], 0x07);
        index++;
    }
    input_buffer[index] = '\0';
    input_pos = (int)index;
}

static void shell_history_record(const char* command) {
    uint32_t previous;

    if (!command || !command[0]) {
        shell_history_reset_navigation();
        return;
    }
    if (shell_history_count) {
        previous = (shell_history_next + SHELL_COMMAND_HISTORY_CAPACITY - 1U) %
                   SHELL_COMMAND_HISTORY_CAPACITY;
        if (kstrcmp(shell_command_history[previous], command) == 0) {
            shell_history_reset_navigation();
            return;
        }
    }
    shell_history_copy(shell_command_history[shell_history_next], command);
    shell_history_next =
        (shell_history_next + 1U) % SHELL_COMMAND_HISTORY_CAPACITY;
    if (shell_history_count < SHELL_COMMAND_HISTORY_CAPACITY) {
        shell_history_count++;
    }
    shell_history_reset_navigation();
}

static void shell_history_navigate(int direction) {
    uint32_t history_index;

    if (direction < 0) {
        if (!shell_history_count ||
            shell_history_depth >= shell_history_count) return;
        if (!shell_history_depth) {
            shell_history_copy(shell_command_draft, input_buffer);
        }
        shell_history_depth++;
    } else {
        if (!shell_history_depth) return;
        shell_history_depth--;
        if (!shell_history_depth) {
            shell_history_replace_input(shell_command_draft);
            return;
        }
    }
    history_index =
        (shell_history_next + SHELL_COMMAND_HISTORY_CAPACITY -
         shell_history_depth) % SHELL_COMMAND_HISTORY_CAPACITY;
    shell_history_replace_input(shell_command_history[history_index]);
}

static void shell_history_detach_for_edit(void) {
    if (!shell_history_depth) return;
    shell_history_reset_navigation();
}

static void shell_hosted_draw(int x, int y, int width, int height) {
    if (video_terminal_draw(x, y, width, height) != OK) {
        LOG_WARN("SHELL", "Falha ao desenhar terminal hospedado");
    }
}

static void shell_hosted_key(uint8_t scancode) {
    shell_handle_terminal_key(scancode);
    shell_present_hosted_progress();
}

static void shell_present_hosted_progress(void) {
    if (!shell_is_hosted_visible() ||
        !wm_is_hosted_app_focused(WM_APP_SHELL)) return;
    if (video_terminal_present_hosted_dirty() != OK) {
        LOG_WARN("SHELL", "Falha ao apresentar entrada do terminal hospedado");
    }
}

int shell_handle_mouse(mouse_event_t* event) {
    if (!event) {
        LOG_ERROR("SHELL", "Evento de mouse nulo");
        return 0;
    }
    if (!video_terminal_is_active() ||
        event->event != MOUSE_EVENT_WHEEL || event->wheel == 0) return 0;
    return video_terminal_scroll(event->wheel * SHELL_WHEEL_SCROLL_LINES);
}

static int shell_hosted_mouse(mouse_event_t* event, int x, int y,
                              int width, int height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;

    if (!event) {
        LOG_ERROR("SHELL", "Evento de mouse nulo no terminal hospedado");
        return 0;
    }
    if (!shell_handle_mouse(event)) return 0;
    /* O WM recompõe o mesmo frame ao terminar este callback. */
    (void)video_terminal_take_hosted_dirty();
    return 1;
}

static void shell_hosted_close(void) {
    shell_hosted_visible = 0;
    shell_shift_mask = 0;
}

static int shell_open_hosted(void) {
    int result;

    if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
        LOG_WARN("SHELL", "Shell hospedado requer modo Classic");
        return ERR_UNAVAILABLE;
    }
    wm_set_active(1);
    if (shell_hosted_visible) return wm_register_hosted_app(&shell_hosted_app);

    video_terminal_set_hosted(1);
    shell_hosted_visible = 1;
    result = wm_register_hosted_app(&shell_hosted_app);
    if (result != OK) {
        shell_hosted_visible = 0;
        video_terminal_set_hosted(0);
        wm_set_active(0);
        desktop_set_active(0);
        LOG_WARN("SHELL", "Workspace nao comporta o Shell hospedado");
        return result;
    }
    shell_print_prompt();
    return OK;
}

static void shell_suspend_terminal(void) {
    /* O prompt continua no historico enquanto outro app cobre o terminal. */
    if (video_terminal_is_active()) video_terminal_suspend();
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
    shell_reset_input();
    shell_print_prompt();
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
    shell_regcheck.devices_result = ERR_STATE;
    shell_regcheck.network_result = ERR_STATE;
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

    if (log_get_level() != LOG_LEVEL_INFO || !app_api_is_ready() ||
        !app_api_file_is_ready() || !app_api_ipc_is_ready() ||
        !syscall_is_ready() || !syscall_user_mode_is_enabled() ||
        !idt_is_user_syscall_enabled() || !paging_is_ready() ||
        !app_loader_is_ready() || !app_package_is_ready() ||
        !app_catalog_is_ready()) {
        LOG_ERROR("SHELL", "RegCheck encontrou servico obrigatorio indisponivel");
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
        !validation.pid_table_valid || !validation.state_table_valid) {
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

static int shell_regcheck_same_network(
    const network_interface_info_t* left,
    const network_interface_info_t* right) {
    if (left->model != right->model || left->state != right->state ||
        left->link != right->link || left->vendor_id != right->vendor_id ||
        left->device_id != right->device_id ||
        left->class_code != right->class_code ||
        left->subclass_code != right->subclass_code ||
        left->prog_if != right->prog_if || left->revision != right->revision ||
        left->bus != right->bus || left->device != right->device ||
        left->function != right->function || left->irq != right->irq) {
        return 0;
    }
    for (uint32_t bar = 0; bar < NETWORK_PCI_BAR_COUNT; bar++) {
        if (left->bars[bar] != right->bars[bar]) return 0;
    }
    return 1;
}

static int shell_regcheck_validate_network_entry(
    const network_interface_info_t* info, uint32_t* recognized,
    uint32_t* active, uint32_t* driver_errors,
    uint32_t* l3_count, uint32_t* dhcp_count) {
    network_interface_info_t found;
    network_interface_text_t text;
    int result;

    if (!info || !recognized || !active || !driver_errors ||
        !l3_count || !dhcp_count) {
        LOG_ERROR("SHELL", "RegCheck Full recebeu entrada Network nula");
        return ERR_NULL;
    }
    if (info->class_code != SHELL_REGCHECK_PCI_NETWORK_CLASS ||
        info->model > NETWORK_ADAPTER_RTL8139 ||
        info->state > NETWORK_INTERFACE_DRIVER_ERROR ||
        info->link > NETWORK_LINK_UP ||
        (info->state != NETWORK_INTERFACE_ACTIVE &&
         info->link != NETWORK_LINK_UNKNOWN) ||
        (info->state == NETWORK_INTERFACE_DRIVER_ERROR &&
         info->driver_error == OK) ||
        (info->model == NETWORK_ADAPTER_UNKNOWN &&
         info->state != NETWORK_INTERFACE_UNSUPPORTED) ||
        (info->model != NETWORK_ADAPTER_UNKNOWN &&
         info->state == NETWORK_INTERFACE_UNSUPPORTED) ||
        (info->state == NETWORK_INTERFACE_ACTIVE &&
         !info->ethernet_attached) ||
        (info->ethernet_attached &&
         info->state != NETWORK_INTERFACE_ACTIVE) ||
        (info->l3_active && !info->ethernet_attached) ||
        (info->dhcp_pending && !info->ethernet_attached)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou entrada Network invalida");
        return ERR_STATE;
    }
    result = network_manager_format_text(info, &text);
    if (result != OK || !text.id[0] || !text.name[0] || !text.driver[0]) {
        LOG_ERROR("SHELL", "RegCheck Full nao formatou entrada Network");
        return result == OK ? ERR_STATE : result;
    }
    result = network_manager_find(text.id, &found);
    if (result != OK || !shell_regcheck_same_network(info, &found)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou ID Network instavel");
        return result == OK ? ERR_STATE : result;
    }
    if (info->model != NETWORK_ADAPTER_UNKNOWN) (*recognized)++;
    if (info->state == NETWORK_INTERFACE_ACTIVE) (*active)++;
    if (info->state == NETWORK_INTERFACE_DRIVER_ERROR) {
        (*driver_errors)++;
    }
    if (info->l3_active) (*l3_count)++;
    if (info->dhcp_pending) (*dhcp_count)++;
    return OK;
}

static int shell_regcheck_validate_network_recovery(
    const network_manager_status_t* status) {
    const recovery_component_t* component =
        recovery_get(RECOVERY_COMPONENT_NETWORK);
    recovery_state_t expected_state;
    int expected_error;

    if (!component || !status) {
        LOG_ERROR("SHELL", "RegCheck Full nao consultou recovery Network");
        return ERR_STATE;
    }
    if (status->partial) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = ERR_OVERFLOW;
    } else if (status->driver_error_count) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status->last_error;
    } else if (status->active_count &&
               status->ethernet_available &&
               status->arp_available &&
               status->ipv4_available &&
               status->icmp_available &&
               status->udp_available &&
               status->dhcp_available &&
               status->dns_available &&
               status->tcp_available &&
               status->sockets_available &&
               status->http_available) {
        expected_state = RECOVERY_STATE_READY;
        expected_error = OK;
    } else if (status->active_count) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status->last_error;
    } else if (status->interface_count) {
        expected_state = RECOVERY_STATE_DEGRADED;
        expected_error = status->last_error;
    } else {
        expected_state = RECOVERY_STATE_DISABLED;
        expected_error = ERR_NOT_FOUND;
    }
    if (component->state != expected_state ||
        component->last_error != expected_error) {
        LOG_ERROR("SHELL", "RegCheck Full detectou recovery Network incoerente");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_arp(
    const network_manager_status_t* network_status) {
    arp_status_t status;
    ethernet_status_t ethernet_status;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck Full recebeu estado ARP nulo");
        return ERR_NULL;
    }
    result = arp_get_status(&status);
    if (result != OK ||
        ethernet_get_status(&ethernet_status) != OK ||
        arp_validate_state() != OK ||
        (network_status->arp_available && !status.initialized) ||
        ethernet_status.handler_count >
            ETHERNET_PROTOCOL_HANDLER_CAPACITY ||
        (network_status->arp_available &&
         !ethernet_status.handler_count) ||
        (network_status->arp_configured !=
         (uint8_t)(network_status->arp_available && status.configured)) ||
        status.cache_entries > ARP_CACHE_CAPACITY ||
        status.cache_entries != status.incomplete_entries +
                                status.resolved_entries +
                                status.failed_entries) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado ARP invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_ipv4_icmp(
    const network_manager_status_t* network_status) {
    ipv4_status_t ipv4_status;
    icmp_status_t icmp_status;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck Full recebeu estado IPv4 nulo");
        return ERR_NULL;
    }
    result = ipv4_get_status(&ipv4_status);
    if (result != OK || icmp_get_status(&icmp_status) != OK ||
        ipv4_validate_state() != OK || icmp_validate_state() != OK ||
        (network_status->ipv4_available && !ipv4_status.initialized) ||
        (network_status->icmp_available && !icmp_status.initialized) ||
        (network_status->ipv4_available &&
         !network_status->arp_available) ||
        (network_status->icmp_available &&
         !network_status->ipv4_available) ||
        (network_status->ipv4_configured !=
         (uint8_t)(network_status->ipv4_available &&
                   ipv4_status.configured)) ||
        (network_status->ipv4_configured &&
         (!network_status->arp_configured ||
          ipv4_status.local_ip == 0U ||
          kstrcmp(network_status->l3_interface_id,
                  ipv4_status.interface_id) != 0)) ||
        ipv4_status.handler_count > IPV4_PROTOCOL_HANDLER_CAPACITY ||
        (network_status->icmp_available &&
         !ipv4_status.handler_count)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado IPv4/ICMP invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_udp_dhcp_dns(
    const network_manager_status_t* network_status) {
    udp_status_t udp;
    dhcp_status_t dhcp;
    dns_status_t dns;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck recebeu estado UDP nulo");
        return ERR_NULL;
    }
    result = udp_get_status(&udp);
    if (result != OK || dhcp_get_status(&dhcp) != OK ||
        dns_get_status(&dns) != OK ||
        udp_validate_state() != OK ||
        dhcp_validate_state() != OK ||
        dns_validate_state() != OK ||
        (network_status->udp_available && !udp.initialized) ||
        (network_status->dhcp_available && !dhcp.initialized) ||
        (network_status->dns_available && !dns.initialized) ||
        (network_status->dhcp_bound !=
         (uint8_t)(dhcp.state == DHCP_STATE_BOUND ||
                   dhcp.state == DHCP_STATE_RENEWING ||
                   dhcp.state == DHCP_STATE_REBINDING)) ||
        (network_status->dns_configured !=
         (uint8_t)(network_status->dns_available &&
                   dns.configured)) ||
        udp.endpoint_count > UDP_ENDPOINT_CAPACITY ||
        dns.cache_entries > DNS_CACHE_CAPACITY) {
        LOG_ERROR("SHELL", "RegCheck detectou UDP/DHCP/DNS invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_tcp_socket_http(
    const network_manager_status_t* network_status) {
    tcp_status_t tcp;
    net_socket_status_t sockets;
    http_status_t http;
    ipv4_status_t ipv4;
    int result;

    if (!network_status) {
        LOG_ERROR("SHELL", "RegCheck recebeu estado TCP nulo");
        return ERR_NULL;
    }
    result = tcp_get_status(&tcp);
    if (result != OK || net_socket_get_status(&sockets) != OK ||
        http_get_status(&http) != OK ||
        ipv4_get_status(&ipv4) != OK ||
        tcp_validate_state() != OK ||
        net_socket_validate_state() != OK ||
        http_validate_state() != OK ||
        (network_status->tcp_available && !tcp.initialized) ||
        (network_status->sockets_available && !sockets.initialized) ||
        (network_status->http_available && !http.initialized) ||
        (network_status->tcp_available &&
         !network_status->ipv4_available) ||
        (network_status->sockets_available &&
         !network_status->tcp_available) ||
        (network_status->http_available &&
         (!network_status->sockets_available ||
          !network_status->dns_available)) ||
        (network_status->tcp_available &&
         ipv4.handler_count <
             SHELL_NETWORK_REQUIRED_IPV4_HANDLERS) ||
        tcp.connection_count > TCP_CONNECTION_CAPACITY ||
        sockets.active_count > NET_SOCKET_CAPACITY ||
        network_status->tcp_connection_count !=
            tcp.connection_count ||
        network_status->socket_count != sockets.active_count) {
        LOG_ERROR("SHELL", "RegCheck detectou TCP/sockets/HTTP invalido");
        return result == OK ? ERR_STATE : result;
    }
    return OK;
}

static int shell_regcheck_validate_network_summary(
    const network_manager_status_t* status,
    const ethernet_status_t* ethernet, uint32_t count) {
    if (!status || !ethernet) {
        LOG_ERROR("SHELL", "RegCheck recebeu resumo Network nulo");
        return ERR_NULL;
    }
    if (!status->initialized || count != status->interface_count ||
        count > NETWORK_MANAGER_MAX_INTERFACES ||
        status->recognized_count > count ||
        status->active_count > count ||
        status->driver_error_count > status->recognized_count ||
        status->active_count > ethernet->interface_count ||
        (status->arp_configured && !status->arp_available) ||
        (status->ipv4_configured && !status->ipv4_available) ||
        (status->dhcp_bound && !status->dhcp_available) ||
        (status->dns_configured &&
         (!status->dns_available || !status->ipv4_configured)) ||
        (status->arp_available &&
         (!status->active_count || !status->ethernet_available)) ||
        (status->ipv4_available &&
         (!status->active_count || !status->arp_available)) ||
        (status->icmp_available &&
         (!status->active_count || !status->ipv4_available)) ||
        (status->udp_available &&
         (!status->active_count || !status->ipv4_available)) ||
        (status->dhcp_available && !status->udp_available) ||
        (status->dns_available && !status->udp_available) ||
        (status->tcp_available && !status->ipv4_available) ||
        (status->sockets_available && !status->tcp_available) ||
        (status->http_available &&
         (!status->sockets_available || !status->dns_available)) ||
        status->tcp_connection_count > TCP_CONNECTION_CAPACITY ||
        status->socket_count > NET_SOCKET_CAPACITY ||
        (status->ipv4_configured &&
         (status->ipv4_source == NETWORK_IPV4_SOURCE_NONE ||
          !status->l3_interface_id[0])) ||
        (!status->ipv4_configured &&
         (status->ipv4_source != NETWORK_IPV4_SOURCE_NONE ||
          status->l3_interface_id[0])) ||
        (status->ethernet_available && !status->active_count) ||
        (status->packet_io_available && !status->active_count) ||
        (!status->packet_io_available && status->active_count) ||
        (status->active_count && status->ethernet_available &&
         status->arp_available && status->ipv4_available &&
         status->icmp_available && status->udp_available &&
         status->dhcp_available && status->dns_available &&
         status->tcp_available && status->sockets_available &&
         status->http_available && !status->partial &&
         !status->driver_error_count && status->last_error != OK) ||
        (!status->active_count && !status->interface_count &&
         status->last_error != ERR_NOT_FOUND)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou estado Network invalido");
        return ERR_STATE;
    }
    return OK;
}

static int shell_regcheck_validate_network(void) {
    network_manager_status_t status;
    ethernet_status_t ethernet;
    dhcp_status_t dhcp;
    uint32_t count = 0;
    uint32_t recognized = 0;
    uint32_t active = 0;
    uint32_t driver_errors = 0;
    uint32_t l3_count = 0;
    uint32_t dhcp_count = 0;
    int result = network_manager_get_status(&status);

    if (result != OK ||
        ethernet_get_status(&ethernet) != OK ||
        ethernet_validate_state() != OK ||
        dhcp_get_status(&dhcp) != OK ||
        network_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "RegCheck nao consultou estado Network");
        return result == OK ? ERR_STATE : result;
    }
    result = shell_regcheck_validate_network_summary(
        &status, &ethernet, count);
    if (result != OK) return result;
    for (uint32_t index = 0; index < count; index++) {
        network_interface_info_t info;
        network_interface_text_t text;
        uint8_t shared_count = 0;

        result = network_manager_get_interface(index, &info);
        if (result != OK) {
            LOG_ERROR("SHELL", "RegCheck Full nao consultou entrada Network");
            return result;
        }
        result = shell_regcheck_validate_network_entry(
            &info, &recognized, &active, &driver_errors,
            &l3_count, &dhcp_count);
        if (result != OK) return result;
        if (network_manager_format_text(&info, &text) != OK) {
            LOG_ERROR("SHELL", "RegCheck nao formatou ID de rede");
            return ERR_STATE;
        }
        for (uint32_t previous = 0; previous < index; previous++) {
            network_interface_info_t other;
            network_interface_text_t other_text;

            if (network_manager_get_interface(previous, &other) != OK ||
                network_manager_format_text(&other, &other_text) != OK ||
                kstrcmp(text.id, other_text.id) == 0) {
                LOG_ERROR("SHELL", "RegCheck detectou ID de rede duplicado");
                return ERR_STATE;
            }
        }
        if (info.state == NETWORK_INTERFACE_ACTIVE &&
            (idt_get_shared_irq_handler_count(
                 info.irq, &shared_count) != OK ||
             !shared_count ||
             shared_count > IDT_SHARED_IRQ_HANDLER_CAPACITY)) {
            LOG_ERROR("SHELL", "RegCheck detectou IRQ compartilhada invalida");
            return ERR_STATE;
        }
    }
    if (recognized != status.recognized_count ||
        active != status.active_count ||
        driver_errors != status.driver_error_count ||
        l3_count > 1U || dhcp_count > 1U ||
        l3_count != (uint32_t)status.ipv4_configured ||
        dhcp_count != (uint32_t)(
            dhcp.state == DHCP_STATE_SELECTING ||
            dhcp.state == DHCP_STATE_REQUESTING ||
            dhcp.state == DHCP_STATE_APPLYING)) {
        LOG_ERROR("SHELL", "RegCheck Full detectou contadores Network invalidos");
        return ERR_STATE;
    }
    result = shell_regcheck_validate_arp(&status);
    if (result != OK) return result;
    result = shell_regcheck_validate_ipv4_icmp(&status);
    if (result != OK) return result;
    result = shell_regcheck_validate_udp_dhcp_dns(&status);
    if (result != OK) return result;
    result = shell_regcheck_validate_tcp_socket_http(&status);
    if (result != OK) return result;
    return shell_regcheck_validate_network_recovery(&status);
}

static int shell_regcheck_valid_acpi_table(
    const acpi_table_info_t* table) {
    if (!table || table->signature[4] != '\0' ||
        !table->physical_address ||
        table->length < SHELL_REGCHECK_ACPI_SDT_HEADER_SIZE ||
        table->length > SHELL_REGCHECK_ACPI_MAX_TABLE_SIZE) {
        return 0;
    }
    for (uint32_t index = 0; index < 4U; index++) {
        if (!table->signature[index]) return 0;
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
    uint32_t table_count = 0;
    int result = acpi_get_status(&status);

    if (result != OK || acpi_get_power_info(&power) != OK ||
        acpi_get_table_count(&table_count) != OK ||
        !status.initialized || table_count != status.table_count ||
        table_count > ACPI_MAX_TABLES ||
        status.root_entry_count > SHELL_REGCHECK_ACPI_MAX_ROOT_ENTRIES ||
        status.root_kind > ACPI_ROOT_XSDT ||
        (status.available &&
         (status.root_kind == ACPI_ROOT_NONE ||
          !status.rsdp_address || !status.root_address)) ||
        (!status.available && status.root_kind != ACPI_ROOT_NONE) ||
        (status.fadt_present != (status.fadt_address != 0)) ||
        (status.dsdt_present != (status.dsdt_address != 0)) ||
        (status.facs_present != (status.facs_address != 0))) {
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
    int network_idempotent = 1;
    log_level_t previous_console_level = log_get_console_level();
    log_level_t previous_buffer_level = log_get_buffer_level();

    if (network_before) {
        previous_network_state = network_before->state;
        previous_network_failures = network_before->failures;
        previous_network_error = network_before->last_error;
    }
    log_set_level(LOG_LEVEL_ERROR);
    shell_regcheck.device_scan_result = shell_run_device_scan(&scan);
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
    if (shell_regcheck.device_scan_result == OK) {
        shell_regcheck.devices_result = shell_regcheck_validate_devices();
    } else {
        shell_regcheck.devices_result = OK;
    }
    if (!network_idempotent) {
        shell_regcheck.network_result = ERR_STATE;
    } else if (scan.network_result == OK) {
        shell_regcheck.network_result = shell_regcheck_validate_network();
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
    } else if (state == SHELL_REGCHECK_WAIT_F12) {
        name = "REGF12.ZAP";
        image_size = shell_build_regcheck_input_image();
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
         shell_regcheck.devices_result != OK ||
         shell_regcheck.network_result != OK ||
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
    print_num((uint32_t)result);
    video_print("\n", 0x07);
}

static void shell_regcheck_finish(void) {
    int failed = shell_regcheck_has_failures();

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
            shell_regcheck_print_failure("devices",
                                         shell_regcheck.devices_result);
            shell_regcheck_print_failure("network",
                                         shell_regcheck.network_result);
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
            shell_regcheck_print_failure("cancelamento_f12",
                                         shell_regcheck.cancellation_result);
        }
        shell_regcheck_print_failure("limpeza_final",
                                     shell_regcheck.cleanup_result);
        video_print("  resultado ERRO\n", 0x0C);
    }
    shell_regcheck_reset();
    shell_reset_input();
    shell_print_prompt();
}

static void shell_regcheck_finish_after_ring3(void) {
    shell_memcheck_result_t memory;
    int result;

    result = shell_regcheck_validate_scheduler();
    if (shell_regcheck.scheduler_result == OK && result != OK) {
        shell_regcheck.scheduler_result = result;
    }
    result = shell_run_memcheck(&memory);
    if (shell_regcheck.memory_result == OK && result != OK) {
        shell_regcheck.memory_result = result;
    }
    shell_regcheck.processes_result = shell_regcheck_validate_processes();
    shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
    shell_regcheck_finish();
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
        launch_result = shell_regcheck_start_image(SHELL_REGCHECK_WAIT_F12);
        if (launch_result == OK) {
            video_print("RegCheck: pressione F12 para validar cancelamento.\n",
                        0x0B);
            return;
        }
        shell_regcheck.cancellation_result = launch_result;
        shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
        shell_regcheck_finish();
        return;
    }

    if (shell_regcheck.state == SHELL_REGCHECK_WAIT_F12) {
        shell_regcheck.cancellation_result =
            shell_regcheck_validate_loader_result(result, 1);
        shell_regcheck_finish_after_ring3();
        return;
    }

    LOG_ERROR("SHELL", "RegCheck recebeu resultado do loader fora de etapa");
    shell_regcheck.cleanup_result = ERR_STATE;
    shell_regcheck_finish();
}

void shell_report_user_test_result(void) {
    uint32_t pid;
    uint32_t faulted;

    if (!video_terminal_is_active()) return;
    if (process_take_user_test_result(&pid, &faulted) != OK) return;

    if (shell_q2check.state != SHELL_Q2CHECK_IDLE) {
        shell_q2check_handle_user_test_result(pid, faulted);
        return;
    }

    video_print("\n[", 0x08);
    video_print(faulted ? "WARN" : "INFO", faulted ? 0x0E : 0x0A);
    video_print("] UserTest PID ", 0x08);
    print_num(pid);
    video_print(faulted ? " encerrado apos falha isolada.\n" :
                         " encerrado com sucesso.\n", 0x07);
    shell_waiting_user_test = 0;
    shell_reset_input();
    shell_print_prompt();
    process_reap_finished_user();
}

static void shell_finish_app_command(void) {
    shell_reset_input();
    if (shell_should_show_prompt()) shell_print_prompt();
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
    shell_finish_app_command();
}

static void shell_report_builtin_failure(shell_builtin_app_t app,
                                         const app_loader_result_t* result) {
    if (!result || (!result->start_failed && !result->faulted &&
                    !result->cancelled && result->exit_code == OK)) {
        return;
    }

    video_print("\n[", 0x08);
    video_print("ERRO", 0x0C);
    video_print("] ", 0x07);
    video_print(shell_builtin_app_name(app), 0x07);
    video_print(" ring 3 nao concluiu (codigo ", 0x07);
    print_num(result->exit_code);
    video_print(").\n", 0x07);
}

void shell_report_app_loader_result(void) {
    app_loader_result_t result;
    int appcheck_result;
    int echo_result;
    int builtin_result;
    int migration_result;
    process_t* current;

    /* Mantem o resultado no loader enquanto uma UI nativa cobre o terminal. */
    if (!video_terminal_is_active()) return;
    if (app_loader_take_finished_result(&result) != OK) return;

    if (shell_regcheck.state != SHELL_REGCHECK_IDLE) {
        shell_regcheck_handle_loader_result(&result);
        return;
    }

    echo_result = shell_echo_loader_pid == result.pid;
    builtin_result = shell_builtin_loader_pid == result.pid;
    migration_result = shell_appcheck_migration_pid == result.pid;

    if (shell_appcheck_loader_pid == result.pid) {
        cmd_appcheck_print_result("loader_foco_aquisicao",
                                  result.focus_acquired ? OK : ERR_STATE);
        current = process_get_current();
        appcheck_result = (!result.faulted && !result.cancelled &&
                           !result.start_failed && result.exit_code == OK &&
                           current && process_get_focus() == current->pid) ?
                          OK : ERR_STATE;
        cmd_appcheck_print_result("loader_foco_retorno", appcheck_result);
        shell_appcheck_loader_pid = 0;
        if (shell_appcheck_start_migration(SHELL_BUILTIN_APP_UPTIME) == OK) {
            return;
        }
        shell_finish_app_command();
        return;
    }

    if (migration_result) {
        shell_appcheck_finish_migration(&result);
        return;
    }

    if (echo_result) {
        shell_echo_loader_pid = 0;
        if (result.start_failed || result.faulted || result.cancelled ||
            result.exit_code != OK) {
            video_print("\n[", 0x08);
            video_print("ERRO", 0x0C);
            video_print("] echo ring 3 nao concluiu (codigo ", 0x07);
            print_num(result.exit_code);
            video_print(").\n", 0x07);
        }
        shell_finish_app_command();
        return;
    }

    if (builtin_result) {
        shell_report_builtin_failure(shell_builtin_loader_app, &result);
        shell_builtin_loader_pid = 0;
        shell_builtin_loader_app = SHELL_BUILTIN_APP_NONE;
        shell_finish_app_command();
        return;
    }

    video_print("\n[", 0x08);
    if (result.start_failed) {
        video_print("ERRO", 0x0C);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        print_num(result.pid);
        video_print(" nao iniciou (codigo ", 0x07);
        print_num(result.exit_code);
        video_print(").\n", 0x07);
    } else if (result.faulted) {
        video_print("WARN", 0x0E);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        print_num(result.pid);
        video_print(" encerrou apos falha isolada.\n", 0x07);
    } else if (result.cancelled) {
        video_print("INFO", 0x0A);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        print_num(result.pid);
        video_print(" cancelado; foco devolvido ao Shell.\n", 0x07);
    } else if (result.exit_code != APP_EXIT_SUCCESS) {
        video_print("ERRO", 0x0C);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        print_num(result.pid);
        video_print(" encerrou com codigo ", 0x07);
        print_num(result.exit_code);
        video_print(".\n", 0x07);
    } else {
        video_print("INFO", 0x0A);
        video_print("] Aplicativo ZAPP PID ", 0x07);
        print_num(result.pid);
        video_print(" encerrou com codigo ", 0x07);
        print_num(result.exit_code);
        video_print(".\n", 0x07);
    }

    shell_finish_app_command();
}

static int shell_prepare_filemanager(void) {
    int result;

    if (fs_get_type() != FS_TYPE_NONE) {
        if (!recovery_is_enabled(RECOVERY_COMPONENT_FILEMANAGER)) {
            LOG_WARN("SHELL", "Recuperando File Manager apos filesystem disponivel");
            recovery_mark_ready(RECOVERY_COMPONENT_FILESYSTEM);
            recovery_mark_ready(RECOVERY_COMPONENT_FILEMANAGER);
        }
        return OK;
    }

    LOG_WARN("SHELL", "Filesystem indisponivel; tentando remontar para o Explorer");
    result = fs_init();
    if (result == OK && fs_get_type() != FS_TYPE_NONE) {
        recovery_mark_ready(RECOVERY_COMPONENT_FILESYSTEM);
        recovery_mark_ready(RECOVERY_COMPONENT_FILEMANAGER);
        LOG_INFO("SHELL", "Filesystem remontado para o Explorer");
        return OK;
    }

    recovery_mark_disabled(RECOVERY_COMPONENT_FILESYSTEM, result,
                           "Sistema de arquivos indisponivel");
    recovery_mark_disabled(RECOVERY_COMPONENT_FILEMANAGER, result,
                           "File Manager requer filesystem");
    LOG_ERROR("SHELL", "Nao foi possivel preparar filesystem para o Explorer");
    return result == OK ? ERR_UNAVAILABLE : result;
}

void shell_handle_app_request(uint32_t request) {
    int hosted_workspace = desktop_get_mode() == DESKTOP_MODE_CLASSIC &&
                           wm_is_active();

    if (!hosted_workspace) {
        if (taskmgr_is_gui_open() && request != IPC_APP_OPEN_TASKMANAGER_GUI) {
            taskmgr_close();
        }
        if (settings_is_open() && request != IPC_APP_OPEN_SETTINGS) {
            settings_close();
        }
        if (updater_is_open() && request != IPC_APP_OPEN_UPDATER) {
            updater_close();
        }
        if (appstore_is_open() && request != IPC_APP_OPEN_APP_STORE) {
            appstore_close();
        }
    }

    switch ((ipc_app_request_t)request) {
        case IPC_APP_OPEN_SHELL:
            if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
                if (shell_open_hosted() == OK) break;
                shell_reset_input();
                video_terminal_begin();
                shell_print_prompt();
                taskbar_draw();
                break;
            }
            if (wm_is_active()) wm_set_active(0);
            desktop_set_active(0);
            if (!video_terminal_is_active()) {
                shell_reset_input();
                video_terminal_begin();
                shell_print_prompt();
            }
            taskbar_draw();
            break;
        case IPC_APP_OPEN_EXPLORER:
            if (shell_prepare_filemanager() == OK) {
                shell_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                fm_run();
            } else {
                video_print("Erro: File Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER:
            if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
                shell_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                if (desktop_get_mode() == DESKTOP_MODE_CLASSIC &&
                    taskmgr_open_gui() != OK) {
                    desktop_set_active(0);
                    wm_set_active(0);
                    shell_suspend_terminal();
                    LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                    taskmgr_run();
                } else if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    taskmgr_run();
                }
            } else {
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_TASKMANAGER_GUI:
            if (!recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
                video_print("Erro: Task Manager indisponivel.\n", 0x0C);
                break;
            }
            shell_suspend_terminal_for_scene();
            if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                desktop_set_active(0);
            }
            if (taskmgr_open_gui() != OK) {
                desktop_set_active(0);
                wm_set_active(0);
                shell_suspend_terminal();
                LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                taskmgr_run();
            }
            break;
        case IPC_APP_OPEN_DESKTOP:
            if (wm_is_active()) wm_set_active(0);
            shell_suspend_terminal();
            video_clear();
            desktop_set_active(1);
            desktop_draw();
            break;
        case IPC_APP_OPEN_SETTINGS:
            if (recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
                shell_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                settings_open();
            } else {
                video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
            }
            break;
        case IPC_APP_OPEN_UPDATER:
            if (recovery_is_enabled(
                    RECOVERY_COMPONENT_SYSTEM_UPDATER)) {
                shell_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                if (updater_open() != OK) {
                    video_print("Erro: System Updater indisponivel.\n",
                                0x0C);
                }
            } else {
                video_print("Erro: System Updater indisponivel.\n",
                            0x0C);
            }
            break;
        case IPC_APP_OPEN_APP_STORE:
            if (recovery_is_enabled(RECOVERY_COMPONENT_APP_STORE)) {
                shell_suspend_terminal_for_scene();
                if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
                    desktop_set_active(0);
                }
                if (appstore_open() != OK) {
                    video_print("Erro: App Store indisponivel.\n", 0x0C);
                }
            } else {
                video_print("Erro: App Store indisponivel.\n", 0x0C);
            }
            break;
        default:
            LOG_ERROR("SHELL", "Solicitacao de aplicativo invalida");
            break;
    }
}

static void shell_redraw_after_overlay_close(void) {
    if (appstore_is_open()) {
        appstore_draw();
        return;
    }
    if (updater_is_open()) {
        updater_draw();
        return;
    }
    if (desktop_is_active()) {
        desktop_draw();
        return;
    }

    if (wm_is_active()) {
        wm_draw_all();
        return;
    }

    if (guitest_is_active()) {
        guitest_draw();
        return;
    }

    /* Menus desenham por coordenadas e nao pertencem ao historico textual. */
    video_terminal_begin();
    taskbar_draw();
}



static void str_upper(char* str) {
    while (*str) {
        if (*str >= 'a' && *str <= 'z') {
            *str -= 32;
        }
        str++;
    }
}

static void print_num(uint32_t num) {
    char buf[16];
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
}

static uint32_t parse_number(const char* str) {
    uint32_t num = 0;
    while (*str >= '0' && *str <= '9') {
        num = num * 10 + (*str - '0');
        str++;
    }
    return num;
}

static void cmd_help_core(void) {
    video_print("  help     - Mostra esta mensagem\n", 0x07);
    video_print("  clear    - Limpa tela e historico do terminal\n", 0x07);
    video_print("  Setas cima/baixo - Navega comandos; Shift rola saida\n",
                0x08);
    video_print("  desktop  - Abre a area de trabalho\n", 0x07);
    video_print("  guimode  - Alterna Desktop simple/classic\n", 0x07);
    video_print("  display  - Mostra status ou altera escala da GUI\n", 0x07);
    video_print("  settings - Abre o painel de configuracoes\n", 0x07);
    video_print("  updater  - Abre o System Updater\n", 0x07);
    video_print("  wm       - Abre gerenciador de janelas\n", 0x07);
}

static void cmd_help(void) {
    video_begin_update();
    video_print("Comandos disponiveis:\n", 0x0B);
    cmd_help_core();
    video_print("  ls       - Lista arquivos\n", 0x07);
    video_print("  cat      - Exibe conteudo de arquivo\n", 0x07);
    video_print("  echo     - Exibe texto\n", 0x07);
    video_print("  mem      - Mostra informacoes de memoria\n", 0x07);
    video_print("  procs    - Mostra processos ativos\n", 0x07);
    video_print("  threads  - Mostra threads ativas\n", 0x07);
    video_print("  threadtest - Valida troca cooperativa de threads\n", 0x07);
    video_print("  uptime   - Mostra tempo ligado\n", 0x07);
    video_print("  beep     - Toca um beep (freq duracao_ms)\n", 0x07);
    video_print("  melody   - Toca uma melodia\n", 0x07);
    video_print("  explorer - Abre o gerenciador de arquivos\n", 0x07);
    video_print("  taskmgr  - Abre o gerenciador de tarefas\n", 0x07);
    video_print("  taskcfg  - Configura a barra de tarefas\n", 0x07);
    video_print("  compress - Liga/desliga compressao de RAM\n", 0x07);
    video_print("  stats    - Mostra estatisticas de compressao\n", 0x07);
    video_print("  mouse    - Status e preferencias do mouse PS/2\n", 0x07);
    video_print("  storage  - Lista, inspeciona e monta volumes ATA\n", 0x07);
    video_print("  index status|rebuild|cancel|check - Controla o indice\n",
                0x07);
    video_print("  search <termo> - Pesquisa nomes e caminhos globais\n", 0x07);
    video_print("  health [summary] - Estado completo ou resumo compacto\n",
                0x07);
    video_print("  log      - Consulta, configura e testa o log circular\n",
                0x07);
    video_print("  devices  - Lista inventario de hardware (-v para detalhes)\n", 0x07);
    video_print("  device-info <id> - Mostra detalhes de um dispositivo\n", 0x07);
    video_print("  device-scan - Refaz apenas a varredura PCI\n", 0x07);
    video_print("  net status - Mostra capacidades atuais de rede\n", 0x07);
    video_print("  net devices - Lista controladores de rede PCI\n", 0x07);
    video_print("  net info <id> - Mostra detalhes de uma interface\n", 0x07);
    video_print("  net ethernet <id> - Inspeciona recepcao Ethernet L2\n", 0x07);
    video_print("  net test <id> - Envia frame Ethernet de diagnostico\n", 0x07);
    video_print("  net arp config <id> <ip> - Configura ARP em RAM\n", 0x07);
    video_print("  net arp status|table|clear - Inspeciona cache ARP\n", 0x07);
    video_print("  net arp resolve <ip> - Resolve IPv4 para MAC\n", 0x07);
    video_print("  net ipv4 config <id> <ip> <mask> <gw> - Configura IPv4\n",
                0x07);
    video_print("  net ipv4 status - Inspeciona IPv4 e ICMP\n", 0x07);
    video_print("  net udp status - Inspeciona datagramas e endpoints\n", 0x07);
    video_print("  net dhcp acquire <id>|status|renew|release\n", 0x07);
    video_print("  net dns config <ip>|status|table|clear\n", 0x07);
    video_print("  net tcp status|connect <host> <porta>\n", 0x07);
    video_print("  net socket status|table - Inspeciona sockets nativos\n",
                0x07);
    video_print("  http get <url>|status - Cliente HTTP/1.1\n", 0x07);
    video_print("  nslookup <dominio> - Resolve registro DNS A\n", 0x07);
    video_print("  ping <ip-ou-dominio> [1-10] - Executa ICMP Echo\n", 0x07);
    video_print("  net check [id] - Agrupa diagnosticos de rede\n", 0x07);
    video_print("  net check qemu <id> <ip> - Executa suite de rede\n", 0x07);
    video_print("  net check qemu dhcp <id> <dominio> - Suite S2.6\n", 0x07);
    video_print("  net check qemu tcp <id> <dominio> - Suite S2.7\n", 0x07);
    video_print("  net check qemu multi <id-a> <id-b> - Suite S2.8\n",
                0x07);
    video_print("  acpi status - Mostra tabelas e energia ACPI observadas\n", 0x07);
    video_print("  power status - Mostra capacidades reais de energia\n", 0x07);
    video_print("  kmetrics - Mostra linha-base de metricas do kernel\n", 0x07);
    video_print("  memcheck - Valida heap, PMM e diretorios de usuario\n", 0x07);
    video_print("  schedcheck - Valida invariantes do scheduler\n", 0x07);
    video_print("  q2check  - Executa diagnostico compacto da Q2\n", 0x07);
    video_print("  regcheck [full] - Executa regressao compacta com F12\n",
                0x07);
    video_print("  appcheck - Testa API, arquivos, IPC e loader\n", 0x07);
    video_print("  pkg      - Gerencia pacotes .ZPK locais\n", 0x07);
    video_print("             pkg list | info | verify | install | remove\n", 0x08);
    video_print("  store    - Consulta e gerencia a App Store local\n", 0x07);
    video_print("             store status | list | info <ID|alias.ZPK>\n",
                0x08);
    video_print("             store install/update/rollback/history (use --confirm)\n",
                0x08);
    video_print("  update verify <arquivo.ZUP> - Verifica sem gravar\n", 0x07);
    video_print("  update status|history - Diagnostico persistente\n",
                0x07);
    video_print("  update remote ...|fetch ... - Distribuicao U5\n",
                0x07);
    video_print("  update apply <arquivo.ZUP> [--confirm] - Aplica U3\n",
                0x07);
    video_print("  update rollback [--confirm] - Desfaz a ultima U3\n",
                0x07);
    video_print("  pkgcheck - Testa validacoes de pacote sem gravar\n", 0x07);
    video_print("  app run <arquivo.ZAP> [args] - Executa aplicativo ring 3\n", 0x07);
    video_print("  app inputtest - Testa teclado de aplicativo ring 3\n", 0x07);
    video_print("  app outputtest [fail] - Testa saida ZAPP em blocos\n", 0x07);
    video_print("  app argtest <texto> - Testa argumentos em aplicativo ring 3\n", 0x07);
    video_print("  usertest - Executa teste isolado em ring 3\n", 0x07);
    video_print("             usertest fault | falha controlada\n", 0x08);
    video_print("  play     - Toca arquivo WAV\n", 0x07);
    video_print("  view     - Exibe imagem BMP\n", 0x07);
    video_print("  icons    - Mostra estado dos icones BMP\n", 0x07);
    video_print("  stop     - Para player de midia\n", 0x07);
    video_print("  edit     - Editor de texto\n", 0x07);
    video_print("             edit (novo) | edit arquivo.txt\n", 0x08);
    video_print("  reboot   - Reinicia o sistema\n", 0x07);
    video_print("  shutdown - Desliga por ACPI ou usa fallback HLT\n", 0x07);
    video_print("  guitest [modern] - Testa primitivas GUI 2D\n", 0x07);
    video_end_update();
}

static void cmd_icons_print_status(const char* name, icon_desktop_id_t id) {
    int status = icons_get_desktop_bitmap_status(id);

    video_print("  ", 0x07);
    video_print(name, 0x0B);
    video_print(": ", 0x07);
    video_print(status == OK ? "BMP" : "FALLBACK", status == OK ? 0x0A : 0x0E);
    if (status != OK) {
        video_print(" erro=", 0x08);
        print_num((uint32_t)status);
    }
    video_print("\n", 0x07);
}

static void cmd_icons(void) {
    video_print("Icones do Desktop:\n", 0x0B);
    video_print("  filesystem: ", 0x07);
    video_print(fs_get_type() == FS_TYPE_NONE ? "INDISPONIVEL" : "DISPONIVEL",
                fs_get_type() == FS_TYPE_NONE ? 0x0E : 0x0A);
    video_print("\n", 0x07);
    cmd_icons_print_status("Shell", ICON_DESKTOP_SHELL);
    cmd_icons_print_status("Explorer", ICON_DESKTOP_EXPLORER);
    cmd_icons_print_status("Task Manager", ICON_DESKTOP_TASKMGR);
}

static void cmd_health_print_component(recovery_component_id_t component) {
    const recovery_component_t* entry = recovery_get(component);

    if (!entry) return;

    video_print("  ", 0x07);
    video_print(entry->name, 0x0B);
    video_print(": ", 0x07);
    video_print(recovery_state_name(entry->state), 0x0F);
    video_print("  falhas=", 0x08);
    print_num(entry->failures);
    video_print("  erro=", 0x08);
    print_num((uint32_t)entry->last_error);
    video_print("\n", 0x07);
    video_print("    motivo: ", 0x08);
    video_print(entry->last_message, 0x07);
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

static int shell_migrated_builtin_is_ready(void) {
    return app_loader_is_ready() && app_api_is_ready() &&
           syscall_is_ready() && syscall_user_mode_is_enabled() &&
           paging_is_ready() && idt_is_user_syscall_enabled();
}

static void cmd_health_print_migrated_builtin(shell_builtin_app_t app) {
    int ready = shell_migrated_builtin_is_ready();

    video_print("  ZAPP ", 0x07);
    video_print(shell_builtin_app_name(app), 0x0B);
    video_print(": ", 0x07);
    video_print(ready ? "READY" : "NATIVE FALLBACK", ready ? 0x0A : 0x0E);
    video_print("\n", 0x07);
}

static void cmd_health_print_user_fault(void) {
    process_user_fault_summary_t fault;
    uint32_t fault_count = process_get_user_fault_count();

    video_print("  Falhas isoladas user: total=", 0x07);
    print_num(fault_count);
    video_print(" ultima=", 0x08);
    if (process_get_last_user_fault(&fault) != OK) {
        video_print("N/D", 0x08);
        video_print("\n", 0x07);
        return;
    }

    video_print("PID=", 0x08);
    print_num(fault.pid);
    video_print(" vetor=", 0x08);
    print_num(fault.vector);
    video_print(" erro=", 0x08);
    print_num(fault.error);
    video_print("\n", 0x07);
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
    print_num(process_get_current_pid());
    video_print("  estado=", 0x08);
    video_print(current ? shell_process_state_name(current->state) : "N/D", 0x07);
    video_print("\n", 0x07);

    video_print("  Processos: total=", 0x07);
    print_num(process_get_count());
    video_print(" READY=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print("\n", 0x07);

    video_print("  Threads: ", 0x07);
    print_num(thread_get_count());
    video_print("  ticks=", 0x08);
    print_num(timer_get_ticks());
    video_print("\n", 0x07);

    video_print("  IPC: foco=", 0x07);
    print_num(process_get_focus());
    video_print(" enviados=", 0x08);
    print_num(ipc.sent);
    video_print(" recebidos=", 0x08);
    print_num(ipc.received);
    video_print(" falhas=", 0x08);
    print_num(ipc.failed);
    video_print(" filas_cheias=", 0x08);
    print_num(ipc.queue_full);
    video_print("\n", 0x07);

    video_print("  Paging: ", 0x07);
    video_print(paging_is_ready() ? "READY" : "DISABLED", 0x0F);
    video_print("\n", 0x07);
    video_print("  App API: ", 0x07);
    video_print(app_api_is_ready() ? "READY" : "DISABLED", 0x0F);
    if (app_api_is_ready() && app_api_get_version(&app_version) == OK) {
        video_print(" v", 0x08);
        print_num(app_version.major);
        video_print(".", 0x08);
        print_num(app_version.minor);
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
    print_num(process_get_user_count());
    video_print("\n", 0x07);
    video_print("  UserTest: ", 0x07);
    int user_test_found = 0;
    for (int user_index = 0; user_index < MAX_PROCESSES; user_index++) {
        if (processes[user_index].user_test &&
            processes[user_index].state != PROCESS_STATE_UNUSED) {
            print_num(processes[user_index].pid);
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
        print_num(app_loader_get_foreground_pid());
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
    print_num(memory_get_total() / 1024);
    video_print(" usada=", 0x08);
    print_num(memory_get_used() / 1024);
    video_print(" livre=", 0x08);
    print_num(memory_get_free() / 1024);
    video_print(" paginas_livres=", 0x08);
    print_num(memory_get_free_pages());
    video_print("\n", 0x07);
    video_print("  Paginas: total=", 0x07);
    print_num(memory_get_total_pages());
    video_print("\n", 0x07);
    video_print("  PMM: paginas_proprias=", 0x07);
    print_num(pmm.owned_pages);
    video_print(" falhas=", 0x08);
    print_num(pmm.allocation_failures);
    video_print(" rejeicoes=", 0x08);
    print_num(pmm.invalid_frees);
    video_print("\n", 0x07);
    video_print("  Heap: ", 0x07);
    if (!heap.initialized || !heap.valid) {
        video_print("N/D\n", 0x08);
    } else {
        video_print("blocos_livres=", 0x07);
        print_num(heap.free_blocks);
        video_print(" maior_livre=", 0x08);
        print_num(heap.largest_free_block / 1024U);
        video_print(" KB fragmentacao=", 0x08);
        print_num(heap.fragmentation_percent);
        video_print("% falhas=", 0x08);
        print_num(heap.allocation_failures);
        video_print(" invalidos=", 0x08);
        print_num(heap.invalid_frees + heap.double_frees);
        video_print("\n", 0x07);
    }
    video_print("  Paging user: diretorios=", 0x07);
    print_num(paging_user.active_directories);
    video_print(" paginas=", 0x08);
    print_num(paging_user.active_pages);
    video_print(" criados=", 0x08);
    print_num(paging_user.directories_created);
    video_print(" liberados=", 0x08);
    print_num(paging_user.directories_released);
    video_print(" rejeicoes=", 0x08);
    print_num(paging_user.rejected_releases);
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

static uint8_t cmd_health_state_color(recovery_state_t state) {
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
                cmd_health_state_color(entry->state));
    video_print(" erro=", 0x08);
    print_num((uint32_t)entry->last_error);
    video_print(" falhas=", 0x08);
    print_num(entry->failures);
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
    print_num(counts[RECOVERY_STATE_READY]);
    video_print(" DEGRADED=", 0x08);
    print_num(counts[RECOVERY_STATE_DEGRADED]);
    video_print(" DISABLED=", 0x08);
    print_num(counts[RECOVERY_STATE_DISABLED]);
    video_print(" UNKNOWN=", 0x08);
    print_num(counts[RECOVERY_STATE_UNKNOWN]);
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

static recovery_state_t cmd_health_update_remote_state(void) {
    update_remote_status_t remote;

    if (update_remote_get_status(&remote) != OK || !remote.enabled) {
        return RECOVERY_STATE_DISABLED;
    }
    if (!remote.network_ready ||
        remote.state == UPDATE_REMOTE_STATE_FAILED ||
        remote.cache_store == UPDATE_REMOTE_STORE_INVALID) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_READY;
}

static void cmd_health_print_inline_state(recovery_state_t state) {
    video_print(recovery_state_name(state), cmd_health_state_color(state));
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
            print_num((uint32_t)component->last_error);
            video_print(" falhas=", 0x08);
            print_num(component->failures);
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
            print_num((uint32_t)component->last_error);
            video_print(" falhas=", 0x08);
            print_num(component->failures);
        }
    } else {
        video_print("UNKNOWN", 0x0C);
    }
    if (status_ready) {
        video_print("\n    fontes=", 0x07);
        print_num(status.source_count);
        video_print(" validas=", 0x08);
        print_num(status.valid_source_count);
        video_print(" invalidas=", 0x08);
        print_num(status.invalid_source_count);
        video_print(" instaladas=", 0x08);
        print_num(status.installed_count);
        video_print(" entradas=", 0x08);
        print_num(status.entry_count);
    }
    video_print("\n", 0x07);
}

static void cmd_health_print_summary_kernel(void) {
    memory_heap_stats_t heap;

    memory_get_heap_stats(&heap);
    video_print("  Kernel: proc=", 0x07);
    print_num(process_get_count());
    video_print(" READY=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print(" paging=", 0x08);
    video_print(paging_is_ready() ? "READY" : "DISABLED",
                paging_is_ready() ? 0x0A : 0x0C);
    video_print("\n  Memoria KB: usada=", 0x07);
    print_num(memory_get_used() / 1024U);
    video_print(" livre=", 0x08);
    print_num(memory_get_free() / 1024U);
    video_print(" heap=", 0x08);
    if (!heap.initialized) {
        video_print("DISABLED", 0x0C);
    } else {
        video_print(heap.valid ? "READY" : "DEGRADED",
                    heap.valid ? 0x0A : 0x0E);
    }
    video_print("\n", 0x07);
}

static void cmd_health_summary(void) {
    video_begin_update();
    video_print("Resumo do health:\n", 0x0B);
    cmd_health_print_summary_components();
    cmd_health_print_summary_update();
    cmd_health_print_summary_app_store();
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
    cmd_health_print_kernel();
    video_end_update();
}

static int shell_args_equal(const char* args, const char* expected) {
    if (!args || !expected) return 0;
    while (*args && *expected && *args == *expected) {
        args++;
        expected++;
    }
    if (*expected) return 0;
    while (*args == ' ' || *args == '\t') args++;
    return *args == '\0';
}

static void cmd_health(const char* args) {
    if (shell_args_equal(args, "")) {
        cmd_health_full();
        return;
    }
    if (shell_args_equal(args, "summary")) {
        cmd_health_summary();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido do comando health");
    video_print("Uso: health [summary]\n", 0x0E);
}

static const char* shell_match_subcommand(const char* args,
                                          const char* expected) {
    if (!args || !expected) return 0;
    while (*args && *expected && *args == *expected) {
        args++;
        expected++;
    }
    if (*expected || (*args && *args != ' ' && *args != '\t')) return 0;
    while (*args == ' ' || *args == '\t') args++;
    return args;
}

static int shell_read_single_arg(const char* args, char* out, uint32_t size) {
    uint32_t length = 0;

    if (!args || !out || size == 0) {
        LOG_ERROR("SHELL", "Destino invalido ao ler argumento");
        return ERR_NULL;
    }
    while (*args == ' ' || *args == '\t') args++;
    while (*args && *args != ' ' && *args != '\t') {
        if (length + 1U >= size) {
            LOG_WARN("SHELL", "Argumento do shell excede limite");
            return ERR_OVERFLOW;
        }
        out[length++] = *args++;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (length == 0 || *args) {
        LOG_WARN("SHELL", "Argumento unico ausente ou invalido");
        return ERR_INVALID;
    }
    out[length] = '\0';
    return OK;
}

static int shell_read_token(const char** cursor, char* out,
                            uint32_t size) {
    uint32_t length = 0;

    if (!cursor || !*cursor || !out || !size) {
        LOG_ERROR("SHELL", "Destino invalido ao ler token");
        return ERR_NULL;
    }
    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
    while (**cursor && **cursor != ' ' && **cursor != '\t') {
        if (length + 1U >= size) {
            LOG_WARN("SHELL", "Token do shell excede limite");
            return ERR_OVERFLOW;
        }
        out[length++] = **cursor;
        (*cursor)++;
    }
    if (!length) {
        LOG_WARN("SHELL", "Token ausente no comando");
        return ERR_INVALID;
    }
    out[length] = '\0';
    return OK;
}

static int shell_read_two_args(const char* args, char* first,
                               uint32_t first_size, char* second,
                               uint32_t second_size) {
    const char* cursor = args;
    int result;

    if (!args || !first || !second) {
        LOG_ERROR("SHELL", "Argumentos nulos ao ler dois valores");
        return ERR_NULL;
    }
    result = shell_read_token(&cursor, first, first_size);
    if (result != OK) return result;
    result = shell_read_token(&cursor, second, second_size);
    if (result != OK) return result;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor) {
        LOG_WARN("SHELL", "Comando recebeu argumentos excedentes");
        return ERR_INVALID;
    }
    return OK;
}

static int shell_read_four_args(const char* args, char* first,
                                uint32_t first_size, char* second,
                                uint32_t second_size, char* third,
                                uint32_t third_size, char* fourth,
                                uint32_t fourth_size) {
    const char* cursor = args;
    int result;

    if (!args || !first || !second || !third || !fourth) {
        LOG_ERROR("SHELL", "Argumentos nulos ao ler quatro valores");
        return ERR_NULL;
    }
    result = shell_read_token(&cursor, first, first_size);
    if (result == OK) {
        result = shell_read_token(&cursor, second, second_size);
    }
    if (result == OK) {
        result = shell_read_token(&cursor, third, third_size);
    }
    if (result == OK) {
        result = shell_read_token(&cursor, fourth, fourth_size);
    }
    if (result != OK) return result;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor) {
        LOG_WARN("SHELL", "Comando recebeu argumentos excedentes");
        return ERR_INVALID;
    }
    return OK;
}

static void cmd_print_hex(uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0) {
        uint32_t shift = (digits - 1U) * 4U;
        char value_char[2];
        value_char[0] = hex[(value >> shift) & 0x0FU];
        value_char[1] = '\0';
        video_print(value_char, 0x07);
        digits--;
    }
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
    print_num(stats.occupancy);
    video_print("/", 0x07);
    print_num(stats.capacity);
    video_print(" registros\n", 0x07);
    cmd_log_print_levels(&stats);
    video_print("Proxima sequencia: ", 0x08);
    print_num(stats.next_sequence);
    video_print("\nSobrescritas: ", 0x08);
    print_num(stats.overwritten_records);
    video_print("  Agrupamentos: ", 0x08);
    print_num(stats.grouped_events);
    video_print("\nTruncamentos: ", 0x08);
    print_num(stats.truncated_events);
    video_print("  Descartes: ", 0x08);
    print_num(stats.dropped_events);
    video_print("  Limpezas: ", 0x08);
    print_num(stats.clear_count);
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
    print_num(magnitude);
}

static void cmd_log_print_record(const log_record_t* record) {
    video_print("seq=", 0x08);
    print_num(record->sequence);
    video_print(" ticks=", 0x08);
    print_num(record->first_tick);
    video_print("..", 0x08);
    print_num(record->last_tick);
    video_print(" [", 0x07);
    video_print(log_level_str(record->level), 0x0B);
    video_print("] [", 0x07);
    video_print(record->module, 0x0B);
    video_print("] ", 0x07);
    video_print(record->message, 0x07);
    video_print("\n  ocorrencias=", 0x08);
    print_num(record->occurrences);
    video_print(" flags=0x", 0x08);
    cmd_print_hex(record->flags, 2U);
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
        if (shell_read_single_arg(arguments, count_text,
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
    if (shell_read_two_args(arguments, target, sizeof(target), level_name,
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
    print_num(test.passed);
    video_print(" aprovados, ", 0x07);
    print_num(test.failed);
    video_print(" falhos)\n", 0x07);
}

static void cmd_log(const char* arguments) {
    const char* subarguments;

    if (shell_args_equal(arguments, "") ||
        shell_args_equal(arguments, "status")) {
        cmd_log_status();
        return;
    }
    subarguments = shell_match_subcommand(arguments, "tail");
    if (subarguments) {
        cmd_log_tail(subarguments);
        return;
    }
    if (shell_args_equal(arguments, "clear")) {
        log_clear_buffer();
        video_print("Log circular limpo.\n", 0x0A);
        return;
    }
    subarguments = shell_match_subcommand(arguments, "level");
    if (subarguments) {
        cmd_log_level(subarguments);
        return;
    }
    if (shell_args_equal(arguments, "check")) {
        cmd_log_check();
        return;
    }
    cmd_log_invalid();
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
        print_num(info->irq);
    }
    if (info->vendor_id || info->device_id) {
        video_print(" vendor=0x", 0x08);
        cmd_print_hex(info->vendor_id, 4U);
        video_print(" device=0x", 0x08);
        cmd_print_hex(info->device_id, 4U);
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
        if (!shell_args_equal(args, "-v")) {
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
    int result = shell_read_single_arg(args, id, sizeof(id));

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
        print_num(info.irq);
    }
    if (info.vendor_id || info.device_id) {
        video_print("\n  Vendor: 0x", 0x07);
        cmd_print_hex(info.vendor_id, 4U);
        video_print("  Device: 0x", 0x07);
        cmd_print_hex(info.device_id, 4U);
        video_print("\n  PCI: ", 0x07);
        cmd_print_hex(info.bus, 2U);
        video_print(":", 0x07);
        cmd_print_hex(info.device, 2U);
        video_print(".", 0x07);
        print_num(info.function);
    }
    if (info.capacity_sectors && info.kind == DEVICE_KIND_ATA_PRIMARY) {
        video_print("\n  Setores: ", 0x07);
        print_num(info.capacity_sectors);
    }
    video_print("\n", 0x07);
}

static int shell_run_device_scan(shell_device_scan_result_t* scan) {
    int recovery_result;

    if (!scan) {
        LOG_ERROR("SHELL", "Destino nulo para resultado de device-scan");
        return ERR_NULL;
    }
    scan->pci_result = pci_init();
    scan->devices_result = ERR_STATE;
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
    result = shell_run_device_scan(&scan);
    if (scan.pci_result != OK && scan.pci_result != ERR_OVERFLOW) {
        video_print("Erro: varredura PCI indisponivel.\n", 0x0C);
        return;
    }
    if (scan.devices_result != OK &&
        scan.devices_result != ERR_OVERFLOW) {
        video_print("Erro: inventario de dispositivos indisponivel.\n", 0x0C);
        return;
    }
    if (scan.network_result != OK &&
        scan.network_result != ERR_OVERFLOW) {
        LOG_WARN("SHELL", "Falha ao atualizar inventario de rede");
        video_print("Aviso: inventario de rede indisponivel.\n", 0x0E);
    }
    if (result == ERR_OVERFLOW) {
        video_print("Varredura PCI parcial; inventario atualizado.\n", 0x0E);
        return;
    }
    video_print("Varredura PCI concluida; inventario atualizado.\n", 0x0A);
    if (scan.network_result == ERR_OVERFLOW) {
        video_print("Aviso: inventario de rede parcial.\n", 0x0E);
    }
}

static uint8_t cmd_network_state_color(network_interface_state_t state) {
    if (state == NETWORK_INTERFACE_ACTIVE) return 0x0A;
    if (state == NETWORK_INTERFACE_DRIVER_MISSING) return 0x0E;
    return 0x0C;
}

static network_link_state_t cmd_net_get_link_state(uint32_t count) {
    network_link_state_t result = NETWORK_LINK_UNKNOWN;

    for (uint32_t index = 0; index < count; index++) {
        network_interface_info_t info;

        if (network_manager_get_interface(index, &info) != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar link de rede");
            return NETWORK_LINK_UNKNOWN;
        }
        if (info.state != NETWORK_INTERFACE_ACTIVE) continue;
        if (info.link == NETWORK_LINK_UP) return NETWORK_LINK_UP;
        if (info.link == NETWORK_LINK_DOWN) result = NETWORK_LINK_DOWN;
    }
    return result;
}

static void cmd_net_status(void) {
    network_manager_status_t status;
    const recovery_component_t* health;
    network_link_state_t link;

    if (network_manager_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado de rede indisponivel");
        video_print("Erro: diagnostico de rede indisponivel.\n", 0x0C);
        return;
    }
    health = recovery_get(RECOVERY_COMPONENT_NETWORK);
    if (!health) {
        LOG_ERROR("SHELL", "Componente Network ausente do health");
        video_print("Erro: health de rede indisponivel.\n", 0x0C);
        return;
    }

    video_print("Rede:\n  Servico: ", 0x0B);
    video_print(recovery_state_name(health->state),
                health->state == RECOVERY_STATE_READY ? 0x0A :
                health->state == RECOVERY_STATE_DEGRADED ? 0x0E : 0x0C);
    video_print("\n  Inventario: ", 0x07);
    video_print(status.partial ? "PARCIAL" : "COMPLETO",
                status.partial ? 0x0E : 0x0A);
    video_print("\n  Controladores detectados: ", 0x07);
    print_num(status.interface_count);
    video_print("\n  Modelos reconhecidos: ", 0x07);
    print_num(status.recognized_count);
    video_print("\n  Drivers ativos: ", 0x07);
    print_num(status.active_count);
    video_print("  Erros de driver: ", 0x07);
    print_num(status.driver_error_count);
    video_print("\n  Link: ", 0x07);
    link = cmd_net_get_link_state(status.interface_count);
    video_print(network_manager_link_state_name(link),
                link == NETWORK_LINK_UP ? 0x0A :
                link == NETWORK_LINK_DOWN ? 0x0E : 0x08);
    video_print("\n  RX/TX: ", 0x07);
    video_print(status.packet_io_available ?
                "DISPONIVEL" : "NAO IMPLEMENTADO",
                status.packet_io_available ? 0x0A : 0x0E);
    video_print("\n  Ethernet L2: ", 0x07);
    video_print(status.ethernet_available ?
                "DISPONIVEL" : "INDISPONIVEL",
                status.ethernet_available ? 0x0A : 0x0E);
    video_print("\n  ARP: ", 0x07);
    if (!status.arp_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else if (status.arp_configured) {
        video_print("CONFIGURADO", 0x0A);
    } else {
        video_print("DISPONIVEL (NAO CONFIGURADO)", 0x0E);
    }
    video_print("\n  IPv4: ", 0x07);
    if (!status.ipv4_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else if (status.ipv4_configured) {
        video_print("CONFIGURADO", 0x0A);
    } else {
        video_print("DISPONIVEL (NAO CONFIGURADO)", 0x0E);
    }
    video_print("  Fonte: ", 0x07);
    video_print(network_manager_ipv4_source_name(status.ipv4_source),
                status.ipv4_source == NETWORK_IPV4_SOURCE_DHCP ? 0x0B :
                status.ipv4_source == NETWORK_IPV4_SOURCE_STATIC ?
                0x0A : 0x08);
    video_print("  Interface L3: ", 0x07);
    video_print(status.l3_interface_id[0] ?
                status.l3_interface_id : "N/D",
                status.l3_interface_id[0] ? 0x0B : 0x08);
    video_print("\n  ICMP Echo: ", 0x07);
    video_print(status.icmp_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.icmp_available ? 0x0A : 0x0E);
    video_print("\n  UDP: ", 0x07);
    video_print(status.udp_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.udp_available ? 0x0A : 0x0E);
    video_print("\n  DHCP: ", 0x07);
    if (!status.dhcp_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else {
        video_print(status.dhcp_bound ? "BOUND" : "DISPONIVEL",
                    status.dhcp_bound ? 0x0A : 0x0E);
    }
    video_print("\n  DNS: ", 0x07);
    if (!status.dns_available) {
        video_print("INDISPONIVEL", 0x0E);
    } else {
        video_print(status.dns_configured ?
                    "CONFIGURADO" : "DISPONIVEL (NAO CONFIGURADO)",
                    status.dns_configured ? 0x0A : 0x0E);
    }
    video_print("\n  TCP: ", 0x07);
    video_print(status.tcp_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.tcp_available ? 0x0A : 0x0E);
    video_print("  Conexoes: ", 0x07);
    print_num(status.tcp_connection_count);
    video_print("\n  Sockets nativos: ", 0x07);
    video_print(status.sockets_available ?
                "DISPONIVEL" : "INDISPONIVEL",
                status.sockets_available ? 0x0A : 0x0E);
    video_print("  Ativos: ", 0x07);
    print_num(status.socket_count);
    video_print("\n  HTTP: ", 0x07);
    video_print(status.http_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.http_available ? 0x0A : 0x0E);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_print_mac(const uint8_t* mac_address) {
    if (!mac_address) {
        video_print("N/D", 0x08);
        return;
    }
    for (uint32_t index = 0; index < NETWORK_MAC_ADDRESS_SIZE; index++) {
        if (index) video_print(":", 0x08);
        cmd_print_hex(mac_address[index], 2U);
    }
}

static int cmd_net_print_interface(const network_interface_info_t* info) {
    network_interface_text_t text;
    int result;

    if (!info) {
        LOG_ERROR("SHELL", "Interface nula ao listar rede");
        return ERR_NULL;
    }
    result = network_manager_format_text(info, &text);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao formatar interface de rede");
        return result;
    }
    video_print("  ", 0x07);
    video_print(text.id, 0x0B);
    video_print("  ", 0x07);
    video_print(network_manager_interface_state_name(info->state),
                cmd_network_state_color(info->state));
    video_print("  ", 0x07);
    video_print(network_manager_model_name(info->model), 0x07);
    video_print("  PCI ", 0x08);
    cmd_print_hex(info->bus, 2U);
    video_print(":", 0x08);
    cmd_print_hex(info->device, 2U);
    video_print(".", 0x08);
    print_num(info->function);
    video_print("  ", 0x07);
    video_print(text.driver, 0x08);
    if (info->l3_active) video_print("  [L3]", 0x0A);
    if (info->dhcp_pending) video_print("  [DHCP]", 0x0E);
    video_print("\n", 0x07);
    return OK;
}

static void cmd_net_devices(void) {
    uint32_t count = 0;

    if (network_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Inventario de rede indisponivel");
        video_print("Erro: inventario de rede indisponivel.\n", 0x0C);
        return;
    }
    if (!count) {
        video_print("Nenhum controlador de rede detectado.\n", 0x0E);
        return;
    }
    video_print("Controladores de rede:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        network_interface_info_t info;

        if (network_manager_get_interface(index, &info) != OK ||
            cmd_net_print_interface(&info) != OK) {
            LOG_ERROR("SHELL", "Falha ao listar controlador de rede");
            video_print("Erro: entrada de rede indisponivel.\n", 0x0C);
            return;
        }
    }
}

static void cmd_net_info(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_interface_info_t info;
    network_interface_text_t text;
    int result = shell_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net info");
        video_print("Uso: net info <id>\n", 0x0C);
        return;
    }
    result = network_manager_find(id, &info);
    if (result == ERR_NOT_FOUND) {
        video_print("Erro: interface de rede nao encontrada.\n", 0x0C);
        return;
    }
    if (result != OK ||
        network_manager_format_text(&info, &text) != OK) {
        LOG_ERROR("SHELL", "Falha ao consultar interface de rede");
        video_print("Erro: interface de rede indisponivel.\n", 0x0C);
        return;
    }

    video_print("Interface de rede:\n  ID: ", 0x0B);
    video_print(text.id, 0x0B);
    video_print("\n  Nome: ", 0x07);
    video_print(text.name, 0x07);
    video_print("\n  Estado: ", 0x07);
    video_print(network_manager_interface_state_name(info.state),
                cmd_network_state_color(info.state));
    video_print("\n  Driver: ", 0x07);
    video_print(text.driver, cmd_network_state_color(info.state));
    video_print("\n  Link: ", 0x07);
    video_print(network_manager_link_state_name(info.link),
                info.link == NETWORK_LINK_UP ? 0x0A :
                info.link == NETWORK_LINK_DOWN ? 0x0E : 0x08);
    video_print("\n  Vinculo Ethernet: ", 0x07);
    video_print(info.ethernet_attached ? "SIM" : "NAO",
                info.ethernet_attached ? 0x0A : 0x0E);
    video_print("  Papel L3: ", 0x07);
    video_print(info.l3_active ? "ATIVO" : "NAO",
                info.l3_active ? 0x0A : 0x08);
    video_print("  DHCP pendente: ", 0x07);
    video_print(info.dhcp_pending ? "SIM" : "NAO",
                info.dhcp_pending ? 0x0E : 0x08);
    video_print("\n  MAC: ", 0x07);
    if (info.state == NETWORK_INTERFACE_ACTIVE) {
        cmd_net_print_mac(info.mac_address);
    } else {
        video_print("N/D", 0x08);
    }
    video_print("\n  RX pacotes: ", 0x07);
    print_num(info.rx_packets);
    video_print("  TX pacotes: ", 0x07);
    print_num(info.tx_packets);
    video_print("\n  RX erros: ", 0x07);
    print_num(info.rx_errors);
    video_print("  TX erros: ", 0x07);
    print_num(info.tx_errors);
    video_print("  RX descartados: ", 0x07);
    print_num(info.rx_dropped);
    video_print("\n  Fila RX atual/pico: ", 0x07);
    print_num(info.rx_queue_depth);
    video_print("/", 0x07);
    print_num(info.rx_queue_high_water);
    video_print("  Fila cheia: ", 0x07);
    print_num(info.rx_queue_dropped);
    video_print("  IRQs RX: ", 0x07);
    print_num(info.rx_interrupts);
    video_print("\n  Erro do driver: ", 0x07);
    print_num((uint32_t)info.driver_error);
    video_print("\n  Vendor: 0x", 0x07);
    cmd_print_hex(info.vendor_id, 4U);
    video_print("  Device: 0x", 0x07);
    cmd_print_hex(info.device_id, 4U);
    video_print("\n  Classe: 0x", 0x07);
    cmd_print_hex(info.class_code, 2U);
    video_print("  Subclasse: 0x", 0x07);
    cmd_print_hex(info.subclass_code, 2U);
    video_print("  Prog-if: 0x", 0x07);
    cmd_print_hex(info.prog_if, 2U);
    video_print("  Revisao: 0x", 0x07);
    cmd_print_hex(info.revision, 2U);
    video_print("\n  PCI: ", 0x07);
    cmd_print_hex(info.bus, 2U);
    video_print(":", 0x07);
    cmd_print_hex(info.device, 2U);
    video_print(".", 0x07);
    print_num(info.function);
    video_print("  IRQ: ", 0x07);
    if (info.irq == NETWORK_IRQ_UNKNOWN) {
        video_print("N/D", 0x08);
    } else {
        print_num(info.irq);
    }
    for (uint32_t bar = 0; bar < NETWORK_PCI_BAR_COUNT; bar++) {
        video_print("\n  BAR", 0x07);
        print_num(bar);
        video_print(": 0x", 0x07);
        cmd_print_hex(info.bars[bar], 8U);
    }
    video_print("\n", 0x07);
}

static void cmd_net_test(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    int result = shell_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net test");
        video_print("Uso: net test <id>\n", 0x0C);
        return;
    }
    result = network_manager_send_diagnostic(id);
    if (result == OK) {
        video_print("Teste TX Ethernet concluido.\n", 0x0A);
        return;
    }
    LOG_WARN("SHELL", "Teste TX Ethernet nao concluiu");
    video_print("Erro: teste TX Ethernet falhou (codigo ", 0x0C);
    print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static const char* cmd_net_destination_name(
    ethernet_destination_t destination) {
    if (destination == ETHERNET_DESTINATION_LOCAL_UNICAST) {
        return "UNICAST LOCAL";
    }
    if (destination == ETHERNET_DESTINATION_BROADCAST) return "BROADCAST";
    return "DESCONHECIDO";
}

static void cmd_net_print_last_ethernet(
    const ethernet_interface_status_t* status) {
    if (!status || !status->rx_frames) {
        video_print("  Ultimo frame: N/D\n", 0x08);
        return;
    }
    video_print("  Ultimo frame: ", 0x07);
    video_print(cmd_net_destination_name(status->last_destination_type),
                0x0B);
    video_print("  tamanho=", 0x07);
    print_num(status->last_frame_length);
    video_print("  EtherType=0x", 0x07);
    cmd_print_hex(status->last_ethertype, 4U);
    video_print("\n    Origem: ", 0x07);
    cmd_net_print_mac(status->last_source);
    video_print("  Destino: ", 0x07);
    cmd_net_print_mac(status->last_destination);
    video_print("\n", 0x07);
}

static void cmd_net_ethernet(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_ethernet_diagnostic_t diagnostic;
    int result = shell_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net ethernet");
        video_print("Uso: net ethernet <id>\n", 0x0C);
        return;
    }
    result = network_manager_get_ethernet_diagnostic(id, &diagnostic);
    if (result != OK) {
        LOG_WARN("SHELL", "Diagnostico Ethernet nao concluiu");
        video_print("Erro: diagnostico Ethernet falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Camada Ethernet:\n  Estado: ATIVA", 0x0B);
    video_print("\n  Processados nesta consulta: ", 0x07);
    print_num(diagnostic.processed_now);
    video_print("\n  Fila RX atual/pico: ", 0x07);
    print_num(diagnostic.driver_queue_depth);
    video_print("/", 0x07);
    print_num(diagnostic.driver_queue_high_water);
    video_print("  Descartes por fila cheia: ", 0x07);
    print_num(diagnostic.driver_queue_dropped);
    video_print("  IRQs RX: ", 0x07);
    print_num(diagnostic.driver_rx_interrupts);
    video_print("\n  Frames L2 aceitos: ", 0x07);
    print_num(diagnostic.interface.rx_frames);
    video_print("  Unicast: ", 0x07);
    print_num(diagnostic.interface.rx_unicast);
    video_print("  Broadcast: ", 0x07);
    print_num(diagnostic.interface.rx_broadcast);
    video_print("\n  Invalidos: ", 0x07);
    print_num(diagnostic.interface.rx_invalid);
    video_print("  Filtrados: ", 0x07);
    print_num(diagnostic.interface.rx_filtered);
    video_print("  Sem protocolo: ", 0x07);
    print_num(diagnostic.interface.rx_unhandled);
    video_print("\n  Entregues a protocolo: ", 0x07);
    print_num(diagnostic.interface.rx_delivered);
    video_print("  Erros de protocolo: ", 0x07);
    print_num(diagnostic.interface.rx_protocol_errors);
    video_print("  Handlers: ", 0x07);
    print_num(diagnostic.layer.handler_count);
    video_print("\n  Polls: ", 0x07);
    print_num(diagnostic.interface.polls);
    video_print("  TX montados pela camada: ", 0x07);
    print_num(diagnostic.interface.tx_frames);
    video_print("\n", 0x07);
    cmd_net_print_last_ethernet(&diagnostic.interface);
}

static int cmd_net_invalid_ipv4(void) {
    LOG_WARN("SHELL", "IPv4 decimal invalido");
    return ERR_INVALID;
}

static int cmd_net_parse_ipv4(const char* text, uint32_t* out_ip) {
    uint32_t ip_address = 0;

    if (!text || !out_ip) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar IPv4");
        return ERR_NULL;
    }
    for (uint32_t octet = 0;
         octet < SHELL_IPV4_OCTET_COUNT; octet++) {
        uint32_t value = 0;
        uint32_t digits = 0;

        while (*text >= '0' && *text <= '9') {
            if (digits >= SHELL_IPV4_OCTET_DIGITS) {
                return cmd_net_invalid_ipv4();
            }
            value = value * 10U + (uint32_t)(*text - '0');
            if (value > SHELL_IPV4_OCTET_MAX) {
                return cmd_net_invalid_ipv4();
            }
            digits++;
            text++;
        }
        if (!digits) return cmd_net_invalid_ipv4();
        ip_address = (ip_address << SHELL_IPV4_OCTET_BITS) | value;
        if (octet + 1U < SHELL_IPV4_OCTET_COUNT) {
            if (*text != '.') return cmd_net_invalid_ipv4();
            text++;
        }
    }
    if (*text) return cmd_net_invalid_ipv4();
    *out_ip = ip_address;
    return OK;
}

static void cmd_net_print_ipv4(uint32_t ip_address) {
    print_num((ip_address >> 24U) & SHELL_IPV4_OCTET_MAX);
    video_print(".", 0x07);
    print_num((ip_address >> 16U) & SHELL_IPV4_OCTET_MAX);
    video_print(".", 0x07);
    print_num((ip_address >> 8U) & SHELL_IPV4_OCTET_MAX);
    video_print(".", 0x07);
    print_num(ip_address & SHELL_IPV4_OCTET_MAX);
}

static void cmd_net_arp_config(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    char ip_text[SHELL_IPV4_TEXT_SIZE];
    uint32_t ip_address = 0;
    int result = shell_read_two_args(args, id, sizeof(id),
                                     ip_text, sizeof(ip_text));

    if (result != OK ||
        cmd_net_parse_ipv4(ip_text, &ip_address) != OK ||
        !arp_ipv4_is_valid(ip_address)) {
        LOG_WARN("SHELL", "Uso invalido de net arp config");
        video_print("Uso: net arp config <id> <ip-local>\n", 0x0C);
        return;
    }
    result = network_manager_configure_arp(id, ip_address);
    if (result != OK) {
        LOG_WARN("SHELL", "Configuracao ARP nao concluiu");
        video_print("Erro: configuracao ARP falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("ARP configurado em RAM para ", 0x0A);
    cmd_net_print_ipv4(ip_address);
    video_print(".\n", 0x0A);
}

static void cmd_net_arp_status(void) {
    arp_status_t status;

    if (arp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel");
        video_print("Erro: estado ARP indisponivel.\n", 0x0C);
        return;
    }
    video_print("ARP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Configuracao: ", 0x07);
    video_print(status.configured ? "ATIVA" : "NAO CONFIGURADO",
                status.configured ? 0x0A : 0x0E);
    video_print("\n  Interface: ", 0x07);
    video_print(status.configured ? status.interface_id : "N/D",
                status.configured ? 0x0B : 0x08);
    video_print("\n  IPv4 local: ", 0x07);
    if (status.configured) {
        cmd_net_print_ipv4(status.local_ip);
    } else {
        video_print("N/D", 0x08);
    }
    video_print("\n  Cache total/pendente/resolvido/falho: ", 0x07);
    print_num(status.cache_entries);
    video_print("/", 0x07);
    print_num(status.incomplete_entries);
    video_print("/", 0x07);
    print_num(status.resolved_entries);
    video_print("/", 0x07);
    print_num(status.failed_entries);
    video_print("\n  Cache hits: ", 0x07);
    print_num(status.cache_hits);
    video_print("  Manutencao ciclos/erros: ", 0x07);
    print_num(status.maintenance_cycles);
    video_print("/", 0x07);
    print_num(status.maintenance_errors);
    video_print("\n  Requests RX/TX: ", 0x07);
    print_num(status.rx_requests);
    video_print("/", 0x07);
    print_num(status.tx_requests);
    video_print("  Replies RX/TX: ", 0x07);
    print_num(status.rx_replies);
    video_print("/", 0x07);
    print_num(status.tx_replies);
    video_print("\n  Invalidos: ", 0x07);
    print_num(status.invalid_packets);
    video_print("  Ignorados: ", 0x07);
    print_num(status.ignored_packets);
    video_print("  Timeouts: ", 0x07);
    print_num(status.timeouts);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n  Cobertura: reply=", 0x07);
    video_print(status.tx_requests && status.rx_replies ?
                "OK" : "NAO TESTADO",
                status.tx_requests && status.rx_replies ? 0x0A : 0x0E);
    video_print("  cache-hit=", 0x07);
    video_print(status.cache_hits ? "OK" : "NAO TESTADO",
                status.cache_hits ? 0x0A : 0x0E);
    video_print("  timeout=", 0x07);
    video_print(status.timeouts ? "OK" : "NAO TESTADO",
                status.timeouts ? 0x0A : 0x0E);
    video_print("\n", 0x07);
}

static void cmd_net_arp_resolve(const char* args) {
    char ip_text[SHELL_IPV4_TEXT_SIZE];
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved = 0;
    uint32_t ip_address = 0;
    int result = shell_read_single_arg(args, ip_text, sizeof(ip_text));

    if (result != OK ||
        cmd_net_parse_ipv4(ip_text, &ip_address) != OK ||
        !arp_ipv4_is_valid(ip_address)) {
        LOG_WARN("SHELL", "Uso invalido de net arp resolve");
        video_print("Uso: net arp resolve <ip>\n", 0x0C);
        return;
    }
    result = arp_resolve(ip_address, mac_address, &resolved);
    if (result == ERR_TIMEOUT) {
        video_print("Resolucao ARP falhou por timeout.\n", 0x0C);
        return;
    }
    if (result != OK) {
        LOG_WARN("SHELL", "Resolucao ARP nao iniciou");
        video_print("Erro: resolucao ARP falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    if (!resolved) {
        video_print("Resolucao ARP pendente.\n", 0x0E);
        return;
    }
    video_print("MAC resolvido: ", 0x0A);
    cmd_net_print_mac(mac_address);
    video_print("\n", 0x07);
}

static void cmd_net_arp_table(void) {
    arp_status_t status;
    uint32_t shown = 0;

    if (arp_get_status(&status) != OK || !status.initialized) {
        LOG_WARN("SHELL", "Tabela ARP indisponivel");
        video_print("Erro: ARP indisponivel.\n", 0x0C);
        return;
    }
    video_print("Tabela ARP:\n", 0x0B);
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        arp_cache_entry_info_t entry;
        int result = arp_get_cache_entry(index, &entry);

        if (result != OK) {
            LOG_ERROR("SHELL", "Falha ao consultar entrada ARP");
            video_print("Erro: entrada ARP indisponivel.\n", 0x0C);
            return;
        }
        if (!entry.used) continue;
        video_print("  ", 0x07);
        cmd_net_print_ipv4(entry.ip_address);
        video_print("  ", 0x07);
        if (entry.state == ARP_ENTRY_RESOLVED) {
            cmd_net_print_mac(entry.mac_address);
        } else {
            video_print("N/D", 0x08);
        }
        video_print("  ", 0x07);
        video_print(arp_entry_state_name(entry.state),
                    entry.state == ARP_ENTRY_RESOLVED ? 0x0A :
                    entry.state == ARP_ENTRY_FAILED ? 0x0C : 0x0E);
        video_print("  idade=", 0x07);
        print_num(entry.age_seconds);
        video_print("s tentativas=", 0x07);
        print_num(entry.attempts);
        video_print("\n", 0x07);
        shown++;
    }
    if (!shown) video_print("  Vazia.\n", 0x08);
}

static void cmd_net_arp_clear(void) {
    int result = arp_clear();

    if (result != OK) {
        LOG_WARN("SHELL", "Limpeza do cache ARP nao concluiu");
        video_print("Erro: cache ARP nao foi limpo (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Cache ARP limpo; configuracao preservada.\n", 0x0A);
}

static void cmd_net_arp(const char* args) {
    const char* config_args;
    const char* resolve_args;

    config_args = shell_match_subcommand(args, "config");
    if (config_args) {
        cmd_net_arp_config(config_args);
        return;
    }
    resolve_args = shell_match_subcommand(args, "resolve");
    if (resolve_args) {
        cmd_net_arp_resolve(resolve_args);
        return;
    }
    if (shell_args_equal(args, "status")) {
        cmd_net_arp_status();
        return;
    }
    if (shell_args_equal(args, "table")) {
        cmd_net_arp_table();
        return;
    }
    if (shell_args_equal(args, "clear")) {
        cmd_net_arp_clear();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net arp");
    video_print("Uso: net arp config <id> <ip> | status | resolve <ip> | "
                "table | clear\n", 0x0C);
}

static uint32_t cmd_net_ticks_to_milliseconds(uint32_t ticks) {
    uint32_t frequency = timer_get_frequency();

    if (!frequency) return 0;
    if (ticks >
        SHELL_MAX_TICK_INTERVAL / SHELL_MILLISECONDS_PER_SECOND) {
        return SHELL_MAX_TICK_INTERVAL;
    }
    return ticks * SHELL_MILLISECONDS_PER_SECOND / frequency;
}

static void cmd_net_ipv4_print_config(const ipv4_status_t* status) {
    video_print("IPv4:\n  Estado: ", 0x0B);
    video_print(status->initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status->initialized ? 0x0A : 0x0E);
    video_print("\n  Configuracao: ", 0x07);
    video_print(status->configured ? "ATIVA" : "NAO CONFIGURADO",
                status->configured ? 0x0A : 0x0E);
    video_print("\n  Interface: ", 0x07);
    video_print(status->configured ? status->interface_id : "N/D",
                status->configured ? 0x0B : 0x08);
    video_print("\n  IPv4 local: ", 0x07);
    if (status->configured) cmd_net_print_ipv4(status->local_ip);
    else video_print("N/D", 0x08);
    video_print("  Mascara: ", 0x07);
    if (status->configured) cmd_net_print_ipv4(status->subnet_mask);
    else video_print("N/D", 0x08);
    video_print("  Gateway: ", 0x07);
    if (status->configured && status->gateway) {
        cmd_net_print_ipv4(status->gateway);
    } else {
        video_print(status->configured ? "SEM ROTA" : "N/D", 0x08);
    }
    video_print("\n  MTU: ", 0x07);
    print_num(IPV4_MTU);
}

static void cmd_net_ipv4_print_counters(const ipv4_status_t* status) {
    video_print("\n  Pacotes RX/TX: ", 0x07);
    print_num(status->rx_packets);
    video_print("/", 0x07);
    print_num(status->tx_packets);
    video_print("  Bytes RX/TX: ", 0x07);
    print_num(status->rx_bytes);
    video_print("/", 0x07);
    print_num(status->tx_bytes);
    video_print("\n  TX rota direta/gateway: ", 0x07);
    print_num(status->tx_direct);
    video_print("/", 0x07);
    print_num(status->tx_via_gateway);
    video_print("  Broadcast RX/TX: ", 0x07);
    print_num(status->rx_limited_broadcast);
    video_print("/", 0x07);
    print_num(status->tx_limited_broadcast);
    video_print("  Entregues: ", 0x07);
    print_num(status->rx_delivered);
    video_print("\n  Invalidos/checksum/opcoes/fragmentos: ", 0x07);
    print_num(status->rx_invalid);
    video_print("/", 0x07);
    print_num(status->rx_checksum_errors);
    video_print("/", 0x07);
    print_num(status->rx_options);
    video_print("/", 0x07);
    print_num(status->rx_fragments);
    video_print("\n  Ignorados/sem handler/erros protocolo: ", 0x07);
    print_num(status->rx_ignored);
    video_print("/", 0x07);
    print_num(status->rx_unhandled);
    video_print("/", 0x07);
    print_num(status->rx_protocol_errors);
    video_print("  Handlers: ", 0x07);
    print_num(status->handler_count);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status->last_error);
    video_print("\n", 0x07);
}

static void cmd_net_icmp_print_status(const icmp_status_t* status) {
    uint32_t average_ticks = status->received ?
        status->rtt_total_ticks / status->received : 0U;
    uint32_t loss_percent = status->requested_count ?
        (uint32_t)status->timeouts * 100U /
            status->requested_count : 0U;

    video_print("ICMP Echo:\n  Estado: ", 0x0B);
    video_print(status->initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status->initialized ? 0x0A : 0x0E);
    video_print("\n  Echo request RX/TX: ", 0x07);
    print_num(status->echo_requests_rx);
    video_print("/", 0x07);
    print_num(status->echo_requests_tx);
    video_print("  Echo reply RX/TX: ", 0x07);
    print_num(status->echo_replies_rx);
    video_print("/", 0x07);
    print_num(status->echo_replies_tx);
    video_print("\n  Sessao: ", 0x07);
    video_print(icmp_ping_state_name(status->state),
                status->state == ICMP_PING_FAILED ? 0x0C :
                status->state == ICMP_PING_COMPLETE ? 0x0A : 0x0E);
    video_print("  Alvo: ", 0x07);
    if (status->requested_count) cmd_net_print_ipv4(status->target_ip);
    else video_print("N/D", 0x08);
    video_print("  id/seq: ", 0x07);
    print_num(status->identifier);
    video_print("/", 0x07);
    print_num(status->current_sequence);
    video_print("\n  Tentativas pedidas: ", 0x07);
    print_num(status->requested_count);
    video_print("  enviados/recebidos/timeouts: ", 0x07);
    print_num(status->sent);
    video_print("/", 0x07);
    print_num(status->received);
    video_print("/", 0x07);
    print_num(status->timeouts);
    video_print("  Perdas confirmadas: ", 0x07);
    print_num(status->timeouts);
    video_print(" (", 0x07);
    print_num(loss_percent);
    video_print("%)", 0x07);
    video_print("\n  RTT min/medio/max: ", 0x07);
    print_num(cmd_net_ticks_to_milliseconds(status->rtt_min_ticks));
    video_print("/", 0x07);
    print_num(cmd_net_ticks_to_milliseconds(average_ticks));
    video_print("/", 0x07);
    print_num(cmd_net_ticks_to_milliseconds(status->rtt_max_ticks));
    video_print(" ms  Reply pendente: ", 0x07);
    video_print(status->reply_pending ? "SIM" : "NAO",
                status->reply_pending ? 0x0E : 0x0A);
    video_print("\n  Invalidos/ignorados/slot ocupado: ", 0x07);
    print_num(status->invalid_packets);
    video_print("/", 0x07);
    print_num(status->ignored_packets);
    video_print("/", 0x07);
    print_num(status->pending_reply_drops);
    video_print("  Ultimo erro: ", 0x07);
    print_num((uint32_t)status->last_error);
    video_print("\n", 0x07);
}

static void cmd_net_ipv4_status(void) {
    ipv4_status_t ipv4_status;
    icmp_status_t icmp_status;

    if (ipv4_get_status(&ipv4_status) != OK ||
        icmp_get_status(&icmp_status) != OK) {
        LOG_ERROR("SHELL", "Estado IPv4 ou ICMP indisponivel");
        video_print("Erro: estado IPv4/ICMP indisponivel.\n", 0x0C);
        return;
    }
    cmd_net_ipv4_print_config(&ipv4_status);
    cmd_net_ipv4_print_counters(&ipv4_status);
    cmd_net_icmp_print_status(&icmp_status);
}

static void cmd_net_ipv4_config(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    char ip_text[SHELL_IPV4_TEXT_SIZE];
    char mask_text[SHELL_IPV4_TEXT_SIZE];
    char gateway_text[SHELL_IPV4_TEXT_SIZE];
    uint32_t local_ip = 0;
    uint32_t subnet_mask = 0;
    uint32_t gateway = 0;
    int result;

    result = shell_read_four_args(
        args, id, sizeof(id), ip_text, sizeof(ip_text),
        mask_text, sizeof(mask_text), gateway_text, sizeof(gateway_text));
    if (result != OK ||
        cmd_net_parse_ipv4(ip_text, &local_ip) != OK ||
        cmd_net_parse_ipv4(mask_text, &subnet_mask) != OK ||
        cmd_net_parse_ipv4(gateway_text, &gateway) != OK) {
        LOG_WARN("SHELL", "Uso invalido de net ipv4 config");
        video_print("Uso: net ipv4 config <id> <ip> <mascara> <gateway>\n",
                    0x0C);
        return;
    }
    result = network_manager_configure_ipv4(
        id, local_ip, subnet_mask, gateway);
    if (result != OK) {
        LOG_WARN("SHELL", "Configuracao IPv4 nao concluiu");
        video_print("Erro: configuracao IPv4 falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("IPv4 configurado em RAM para ", 0x0A);
    cmd_net_print_ipv4(local_ip);
    video_print(".\n", 0x0A);
}

static void cmd_net_ipv4(const char* args) {
    const char* config_args = shell_match_subcommand(args, "config");

    if (config_args) {
        cmd_net_ipv4_config(config_args);
        return;
    }
    if (shell_args_equal(args, "status")) {
        cmd_net_ipv4_status();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net ipv4");
    video_print("Uso: net ipv4 config <id> <ip> <mascara> <gateway> | "
                "status\n", 0x0C);
}

static void cmd_net_udp_status(void) {
    udp_status_t status;

    if (udp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado UDP indisponivel");
        video_print("Erro: estado UDP indisponivel.\n", 0x0C);
        return;
    }
    video_print("UDP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Endpoints: ", 0x07);
    print_num(status.endpoint_count);
    video_print("/", 0x07);
    print_num(UDP_ENDPOINT_CAPACITY);
    video_print("  Datagramas RX/TX: ", 0x07);
    print_num(status.rx_datagrams);
    video_print("/", 0x07);
    print_num(status.tx_datagrams);
    video_print("\n  Bytes RX/TX: ", 0x07);
    print_num(status.rx_bytes);
    video_print("/", 0x07);
    print_num(status.tx_bytes);
    video_print("  Broadcast RX/TX: ", 0x07);
    print_num(status.rx_broadcast);
    video_print("/", 0x07);
    print_num(status.tx_broadcast);
    video_print("\n  Invalidos/tamanho/checksum/sem listener: ", 0x07);
    print_num(status.rx_invalid);
    video_print("/", 0x07);
    print_num(status.rx_length_errors);
    video_print("/", 0x07);
    print_num(status.rx_checksum_errors);
    video_print("/", 0x07);
    print_num(status.rx_no_listener);
    video_print("  Entregues/erros RX/TX: ", 0x07);
    print_num(status.rx_delivered);
    video_print("/", 0x07);
    print_num(status.rx_protocol_errors);
    video_print("/", 0x07);
    print_num(status.tx_errors);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_udp(const char* args) {
    if (shell_args_equal(args, "status")) {
        cmd_net_udp_status();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net udp");
    video_print("Uso: net udp status\n", 0x0C);
}

static void cmd_net_dhcp_print_lease(const dhcp_status_t* status) {
    video_print("\n  Interface: ", 0x07);
    video_print(status->interface_id[0] ? status->interface_id : "N/D",
                status->interface_id[0] ? 0x0B : 0x08);
    video_print("  IPv4: ", 0x07);
    if (status->lease.address) cmd_net_print_ipv4(status->lease.address);
    else video_print("N/D", 0x08);
    video_print("\n  Mascara: ", 0x07);
    if (status->lease.subnet_mask) {
        cmd_net_print_ipv4(status->lease.subnet_mask);
    } else {
        video_print("N/D", 0x08);
    }
    video_print("  Gateway: ", 0x07);
    if (status->lease.gateway) cmd_net_print_ipv4(status->lease.gateway);
    else video_print("N/D", 0x08);
    video_print("  DNS: ", 0x07);
    if (status->lease.dns_server) {
        cmd_net_print_ipv4(status->lease.dns_server);
    } else {
        video_print("N/D", 0x08);
    }
}

static void cmd_net_dhcp_status(void) {
    dhcp_status_t status;

    if (dhcp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado DHCP indisponivel");
        video_print("Erro: estado DHCP indisponivel.\n", 0x0C);
        return;
    }
    video_print("DHCP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("  Sessao: ", 0x07);
    video_print(dhcp_state_name(status.state),
                status.state == DHCP_STATE_BOUND ? 0x0A :
                status.state == DHCP_STATE_FAILED ||
                status.state == DHCP_STATE_EXPIRED ? 0x0C : 0x0E);
    cmd_net_dhcp_print_lease(&status);
    video_print("\n  Lease/T1/T2 restantes: ", 0x07);
    print_num(status.lease_remaining_seconds);
    video_print("/", 0x07);
    print_num(status.t1_remaining_seconds);
    video_print("/", 0x07);
    print_num(status.t2_remaining_seconds);
    video_print(" s  Tentativas: ", 0x07);
    print_num(status.attempts);
    video_print("\n  Discover/Offer/Request/ACK/NAK: ", 0x07);
    print_num(status.discovers_tx);
    video_print("/", 0x07);
    print_num(status.offers_rx);
    video_print("/", 0x07);
    print_num(status.requests_tx);
    video_print("/", 0x07);
    print_num(status.acks_rx);
    video_print("/", 0x07);
    print_num(status.naks_rx);
    video_print("  Releases: ", 0x07);
    print_num(status.releases_tx);
    video_print("\n  Invalidos/ignorados/timeouts: ", 0x07);
    print_num(status.invalid_packets);
    video_print("/", 0x07);
    print_num(status.ignored_packets);
    video_print("/", 0x07);
    print_num(status.timeouts);
    video_print("  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static int cmd_net_dhcp_wait(dhcp_status_t* out_status) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    dhcp_status_t status;

    if (!out_status) {
        LOG_ERROR("SHELL", "Destino nulo na espera DHCP");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_DHCP_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera DHCP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_DHCP_WAIT_SECONDS;
    do {
        if (dhcp_get_status(&status) != OK) return ERR_STATE;
        if (status.state == DHCP_STATE_BOUND) {
            *out_status = status;
            return OK;
        }
        if (status.state == DHCP_STATE_FAILED ||
            status.state == DHCP_STATE_EXPIRED) {
            *out_status = status;
            return status.last_error == OK ? ERR_STATE :
                                             status.last_error;
        }
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    *out_status = status;
    LOG_WARN("SHELL", "Guarda de tempo DHCP expirou");
    return ERR_TIMEOUT;
}

static void cmd_net_dhcp_acquire(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    dhcp_status_t status;
    int result = shell_read_single_arg(args, id, sizeof(id));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de net dhcp acquire");
        video_print("Uso: net dhcp acquire <id>\n", 0x0C);
        return;
    }
    result = network_manager_acquire_dhcp(id);
    if (result == OK) result = cmd_net_dhcp_wait(&status);
    if (result != OK) {
        LOG_WARN("SHELL", "Aquisicao DHCP nao concluiu");
        video_print("Erro: DHCP falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Lease DHCP aplicado: ", 0x0A);
    cmd_net_print_ipv4(status.lease.address);
    video_print(".\n", 0x0A);
}

static void cmd_net_dhcp_renew(void) {
    dhcp_status_t status;
    int result = network_manager_renew_dhcp();

    if (result == OK) result = cmd_net_dhcp_wait(&status);
    if (result != OK) {
        LOG_WARN("SHELL", "Renovacao DHCP nao concluiu");
        video_print("Erro: renovacao DHCP falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Lease DHCP renovado.\n", 0x0A);
}

static void cmd_net_dhcp_release(void) {
    uint8_t sent = 0;
    int result = network_manager_release_dhcp(&sent);

    if (result != OK) {
        LOG_WARN("SHELL", "Liberacao DHCP nao concluiu");
        video_print("Erro: liberacao DHCP falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print(sent ? "DHCPRELEASE enviado; lease removido.\n" :
                       "Lease DHCP removido localmente.\n",
                sent ? 0x0A : 0x0E);
}

static void cmd_net_dhcp(const char* args) {
    const char* acquire_args = shell_match_subcommand(args, "acquire");

    if (acquire_args) {
        cmd_net_dhcp_acquire(acquire_args);
        return;
    }
    if (shell_args_equal(args, "status")) {
        cmd_net_dhcp_status();
        return;
    }
    if (shell_args_equal(args, "renew")) {
        cmd_net_dhcp_renew();
        return;
    }
    if (shell_args_equal(args, "release")) {
        cmd_net_dhcp_release();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net dhcp");
    video_print("Uso: net dhcp acquire <id> | status | renew | release\n",
                0x0C);
}

static void cmd_net_dns_status(void) {
    dns_status_t status;

    if (dns_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado DNS indisponivel");
        video_print("Erro: estado DNS indisponivel.\n", 0x0C);
        return;
    }
    video_print("DNS:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("  Configuracao: ", 0x07);
    video_print(status.configured ? "ATIVA" : "NAO CONFIGURADO",
                status.configured ? 0x0A : 0x0E);
    video_print("\n  Servidor: ", 0x07);
    if (status.configured) cmd_net_print_ipv4(status.server_ip);
    else video_print("N/D", 0x08);
    video_print("  Porta local: ", 0x07);
    print_num(status.local_port);
    video_print("  Cache: ", 0x07);
    print_num(status.cache_entries);
    video_print("/", 0x07);
    print_num(DNS_CACHE_CAPACITY);
    video_print("\n  Consulta: ", 0x07);
    video_print(dns_state_name(status.state),
                status.state == DNS_STATE_COMPLETE ? 0x0A :
                status.state == DNS_STATE_FAILED ? 0x0C : 0x0E);
    video_print("  Nome: ", 0x07);
    video_print(status.query_name[0] ? status.query_name : "N/D",
                status.query_name[0] ? 0x0B : 0x08);
    video_print("\n  Queries/replies/cache hit/miss: ", 0x07);
    print_num(status.queries_tx);
    video_print("/", 0x07);
    print_num(status.replies_rx);
    video_print("/", 0x07);
    print_num(status.cache_hits);
    video_print("/", 0x07);
    print_num(status.cache_misses);
    video_print("\n  Invalidos/ignorados/timeouts: ", 0x07);
    print_num(status.invalid_packets);
    video_print("/", 0x07);
    print_num(status.ignored_packets);
    video_print("/", 0x07);
    print_num(status.timeouts);
    video_print("  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_dns_table(void) {
    uint8_t found = 0;

    video_print("Cache DNS:\n", 0x0B);
    for (uint32_t index = 0; index < DNS_CACHE_CAPACITY; index++) {
        dns_cache_entry_info_t entry;

        if (dns_get_cache_entry(index, &entry) != OK) {
            video_print("  Erro ao consultar entrada.\n", 0x0C);
            return;
        }
        if (!entry.used) continue;
        found = 1;
        video_print("  ", 0x07);
        video_print(entry.name, 0x0B);
        video_print("  ", 0x07);
        cmd_net_print_ipv4(entry.address);
        video_print("  ttl=", 0x07);
        print_num(entry.ttl_remaining_seconds);
        video_print("s idade=", 0x07);
        print_num(entry.age_seconds);
        video_print("s\n", 0x07);
    }
    if (!found) video_print("  Vazio.\n", 0x08);
}

static void cmd_net_dns_config(const char* args) {
    char server_text[SHELL_IPV4_TEXT_SIZE];
    uint32_t server_ip = 0;
    int result = shell_read_single_arg(args, server_text,
                                       sizeof(server_text));

    if (result != OK ||
        cmd_net_parse_ipv4(server_text, &server_ip) != OK) {
        LOG_WARN("SHELL", "Uso invalido de net dns config");
        video_print("Uso: net dns config <servidor>\n", 0x0C);
        return;
    }
    result = network_manager_configure_dns(server_ip);
    if (result != OK) {
        video_print("Erro: configuracao DNS falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Servidor DNS configurado: ", 0x0A);
    cmd_net_print_ipv4(server_ip);
    video_print(".\n", 0x0A);
}

static void cmd_net_dns(const char* args) {
    const char* config_args = shell_match_subcommand(args, "config");

    if (config_args) {
        cmd_net_dns_config(config_args);
        return;
    }
    if (shell_args_equal(args, "status")) {
        cmd_net_dns_status();
        return;
    }
    if (shell_args_equal(args, "table")) {
        cmd_net_dns_table();
        return;
    }
    if (shell_args_equal(args, "clear")) {
        if (dns_clear() == OK) video_print("Cache DNS limpo.\n", 0x0A);
        else video_print("Erro: cache DNS nao foi limpo.\n", 0x0C);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net dns");
    video_print("Uso: net dns config <servidor> | status | table | clear\n",
                0x0C);
}

static int cmd_dns_wait(const char* name, uint32_t* out_ip) {
    dns_status_t status;
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    uint8_t resolved = 0;
    int result;

    if (!name || !out_ip) {
        LOG_ERROR("SHELL", "Destino nulo na espera DNS");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_DNS_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera DNS");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_DNS_WAIT_SECONDS;
    result = dns_resolve(name, out_ip, &resolved);
    if (result != OK || resolved) return result;
    do {
        if (dns_get_status(&status) != OK) return ERR_STATE;
        if (status.state == DNS_STATE_COMPLETE) {
            *out_ip = status.result_ip;
            return OK;
        }
        if (status.state == DNS_STATE_FAILED) {
            return status.last_error == OK ? ERR_STATE :
                                             status.last_error;
        }
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    LOG_WARN("SHELL", "Guarda de tempo DNS expirou");
    return ERR_TIMEOUT;
}

static void cmd_nslookup(const char* args) {
    char name[SHELL_DNS_NAME_SIZE];
    dns_status_t status;
    uint32_t address = 0;
    int result = shell_read_single_arg(args, name, sizeof(name));

    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de nslookup");
        video_print("Uso: nslookup <dominio>\n", 0x0C);
        return;
    }
    result = cmd_dns_wait(name, &address);
    if (result != OK || dns_get_status(&status) != OK) {
        video_print("Erro: consulta DNS falhou (codigo ", 0x0C);
        print_num((uint32_t)(result != OK ? result : ERR_STATE));
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Servidor: ", 0x07);
    cmd_net_print_ipv4(status.server_ip);
    video_print("\nNome: ", 0x07);
    video_print(status.canonical_name[0] ?
                status.canonical_name : name, 0x0B);
    video_print("\nEndereco: ", 0x07);
    cmd_net_print_ipv4(address);
    video_print("\n", 0x07);
}

static int cmd_ping_parse_count(const char* text, uint8_t* out_count) {
    uint32_t value = 0;

    if (!text || !out_count) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar quantidade ping");
        return ERR_NULL;
    }
    if (!*text) {
        LOG_WARN("SHELL", "Quantidade ausente no ping");
        return ERR_INVALID;
    }
    while (*text) {
        if (*text < '0' || *text > '9') {
            LOG_WARN("SHELL", "Quantidade nao numerica no ping");
            return ERR_INVALID;
        }
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > ICMP_PING_MAX_COUNT) {
            LOG_WARN("SHELL", "Quantidade excede limite do ping");
            return ERR_INVALID;
        }
        text++;
    }
    if (!value) {
        LOG_WARN("SHELL", "Quantidade zero no ping");
        return ERR_INVALID;
    }
    *out_count = (uint8_t)value;
    return OK;
}

static int cmd_ping_parse_args(const char* args, char* out_target,
                               uint32_t target_size,
                               uint8_t* out_count) {
    const char* cursor = args;
    char count_text[4];
    int result;

    if (!args || !out_target || !target_size || !out_count) {
        LOG_ERROR("SHELL", "Argumentos nulos ao interpretar ping");
        return ERR_NULL;
    }
    result = shell_read_token(&cursor, out_target, target_size);
    if (result != OK) return result;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    *out_count = ICMP_PING_DEFAULT_COUNT;
    if (*cursor) {
        result = shell_read_token(&cursor, count_text, sizeof(count_text));
        if (result != OK ||
            cmd_ping_parse_count(count_text, out_count) != OK) {
            return ERR_INVALID;
        }
    }
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor) {
        LOG_WARN("SHELL", "Sintaxe invalida no ping");
        return ERR_INVALID;
    }
    return OK;
}

static uint8_t cmd_ping_target_is_numeric(const char* target) {
    uint8_t saw_dot = 0;

    if (!target || !*target) return 0;
    while (*target) {
        if (*target == '.') saw_dot = 1;
        else if (*target < '0' || *target > '9') return 0;
        target++;
    }
    return saw_dot;
}

static int cmd_ping_resolve_target(const char* target,
                                   uint32_t* out_ip) {
    if (!target || !out_ip) {
        LOG_ERROR("SHELL", "Destino nulo ao resolver ping");
        return ERR_NULL;
    }
    if (cmd_ping_target_is_numeric(target)) {
        if (cmd_net_parse_ipv4(target, out_ip) != OK ||
            !ipv4_address_is_unicast(*out_ip)) return ERR_INVALID;
        return OK;
    }
    return cmd_dns_wait(target, out_ip);
}

static void cmd_ping_print_event(const icmp_status_t* status) {
    uint32_t sequence =
        (uint32_t)status->received + status->timeouts;

    if (status->last_event == ICMP_PING_EVENT_REPLY) {
        video_print("Resposta de ", 0x0A);
        cmd_net_print_ipv4(status->target_ip);
        video_print(": bytes=", 0x07);
        print_num(ICMP_ECHO_DATA_SIZE);
        video_print(" seq=", 0x07);
        print_num(sequence);
        video_print(" ttl=", 0x07);
        print_num(status->last_reply_ttl);
        video_print(" tempo=", 0x07);
        print_num(cmd_net_ticks_to_milliseconds(
            status->last_rtt_ticks));
        video_print("ms\n", 0x07);
    } else if (status->last_event == ICMP_PING_EVENT_TIMEOUT) {
        video_print("Timeout para ", 0x0E);
        cmd_net_print_ipv4(status->target_ip);
        video_print(" seq=", 0x07);
        print_num(sequence);
        video_print("\n", 0x07);
    }
}

static int cmd_ping_wait(uint8_t print_events,
                         icmp_status_t* out_status) {
    icmp_status_t status;
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick;
    uint32_t wait_ticks;
    uint32_t event_generation;
    uint32_t wait_seconds;

    if (!out_status) {
        LOG_ERROR("SHELL", "Destino nulo na espera de ping");
        return ERR_NULL;
    }
    if (icmp_get_status(&status) != OK || !frequency) {
        LOG_ERROR("SHELL", "Estado ou timer indisponivel para ping");
        return ERR_STATE;
    }
    wait_seconds =
        (uint32_t)status.requested_count + SHELL_PING_WAIT_EXTRA_SECONDS;
    if (frequency > SHELL_MAX_TICK_INTERVAL / wait_seconds) {
        LOG_ERROR("SHELL", "Intervalo do ping excede timer");
        return ERR_STATE;
    }
    wait_ticks = frequency * wait_seconds;
    start_tick = timer_get_ticks();
    event_generation = status.event_generation;
    do {
        if (icmp_get_status(&status) != OK) {
            LOG_ERROR("SHELL", "Estado ICMP perdido durante ping");
            return ERR_STATE;
        }
        if (status.event_generation != event_generation) {
            event_generation = status.event_generation;
            if (print_events) cmd_ping_print_event(&status);
        }
        if (status.state == ICMP_PING_COMPLETE) {
            *out_status = status;
            return OK;
        }
        if (status.state == ICMP_PING_FAILED) {
            *out_status = status;
            return status.last_error == OK ? ERR_STATE :
                                             status.last_error;
        }
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    *out_status = status;
    LOG_WARN("SHELL", "Guarda de tempo do ping expirou");
    return ERR_TIMEOUT;
}

static void cmd_ping_print_summary(const icmp_status_t* status) {
    uint32_t lost = status->requested_count - status->received;
    uint32_t loss_percent = status->requested_count ?
        lost * 100U / status->requested_count : 0U;
    uint32_t average_ticks = status->received ?
        status->rtt_total_ticks / status->received : 0U;

    video_print("\nResumo de ", 0x0B);
    cmd_net_print_ipv4(status->target_ip);
    video_print(": enviados=", 0x07);
    print_num(status->sent);
    video_print(" recebidos=", 0x07);
    print_num(status->received);
    video_print(" perdidos=", 0x07);
    print_num(lost);
    video_print(" (", 0x07);
    print_num(loss_percent);
    video_print("%)\nRTT min/medio/max = ", 0x07);
    print_num(cmd_net_ticks_to_milliseconds(status->rtt_min_ticks));
    video_print("/", 0x07);
    print_num(cmd_net_ticks_to_milliseconds(average_ticks));
    video_print("/", 0x07);
    print_num(cmd_net_ticks_to_milliseconds(status->rtt_max_ticks));
    video_print(" ms\n", 0x07);
}

static int cmd_ping_execute(uint32_t target_ip, uint8_t count,
                            uint8_t print_events,
                            icmp_status_t* out_status) {
    int result = icmp_ping_start(target_ip, count,
                                 ICMP_PING_TIMEOUT_SECONDS);

    if (result != OK) return result;
    return cmd_ping_wait(print_events, out_status);
}

static void cmd_ping(const char* args) {
    icmp_status_t status;
    char target_text[SHELL_DNS_NAME_SIZE];
    uint32_t target_ip = 0;
    uint8_t count = ICMP_PING_DEFAULT_COUNT;
    int result = cmd_ping_parse_args(
        args, target_text, sizeof(target_text), &count);

    kmemset(&status, 0, sizeof(status));
    if (result != OK) {
        LOG_WARN("SHELL", "Uso invalido de ping");
        video_print("Uso: ping <ip-ou-dominio> [quantidade 1-10]\n",
                    0x0C);
        return;
    }
    result = cmd_ping_resolve_target(target_text, &target_ip);
    if (result != OK) {
        LOG_WARN("SHELL", "Destino do ping nao foi resolvido");
        video_print("Erro: destino do ping nao resolvido (codigo ",
                    0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("PING ", 0x0B);
    video_print(target_text, 0x0B);
    video_print(" [", 0x07);
    cmd_net_print_ipv4(target_ip);
    video_print("] com 32 bytes de dados:\n", 0x07);
    result = cmd_ping_execute(target_ip, count, 1U, &status);
    if (result != OK) {
        LOG_WARN("SHELL", "Ping nao concluiu normalmente");
        video_print("Erro: ping falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        if (result == ERR_TIMEOUT &&
            (status.state == ICMP_PING_RESOLVING ||
             status.state == ICMP_PING_WAITING_REPLY)) {
            icmp_reset();
        }
        return;
    }
    cmd_ping_print_summary(&status);
}

static int cmd_net_parse_port(const char* text, uint16_t* out_port) {
    uint32_t value = 0;

    if (!text || !out_port) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar porta TCP");
        return ERR_NULL;
    }
    if (!*text) return ERR_INVALID;
    while (*text) {
        if (*text < '0' || *text > '9') return ERR_INVALID;
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > 65535U) return ERR_INVALID;
        text++;
    }
    if (!value) return ERR_INVALID;
    *out_port = (uint16_t)value;
    return OK;
}

static void cmd_net_tcp_status(void) {
    tcp_status_t status;

    if (tcp_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado TCP indisponivel");
        video_print("Erro: estado TCP indisponivel.\n", 0x0C);
        return;
    }
    video_print("TCP:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Conexoes: ", 0x07);
    print_num(status.connection_count);
    video_print("/", 0x07);
    print_num(TCP_CONNECTION_CAPACITY);
    video_print("  Segmentos RX/TX: ", 0x07);
    print_num(status.segments_rx);
    video_print("/", 0x07);
    print_num(status.segments_tx);
    video_print("\n  Bytes RX/TX: ", 0x07);
    print_num(status.bytes_rx);
    video_print("/", 0x07);
    print_num(status.bytes_tx);
    video_print("  SYN TX/SYN-ACK RX: ", 0x07);
    print_num(status.syn_tx);
    video_print("/", 0x07);
    print_num(status.syn_ack_rx);
    video_print("\n  FIN RX/TX: ", 0x07);
    print_num(status.fin_rx);
    video_print("/", 0x07);
    print_num(status.fin_tx);
    video_print("  RST RX/TX: ", 0x07);
    print_num(status.resets_rx);
    video_print("/", 0x07);
    print_num(status.resets_tx);
    video_print("\n  Retransmissoes/timeouts: ", 0x07);
    print_num(status.retransmissions);
    video_print("/", 0x07);
    print_num(status.timeouts);
    video_print("  Invalidos/checksum: ", 0x07);
    print_num(status.rx_invalid);
    video_print("/", 0x07);
    print_num(status.rx_checksum_errors);
    video_print("\n  Sem conexao/duplicados/fora de ordem: ", 0x07);
    print_num(status.rx_no_connection);
    video_print("/", 0x07);
    print_num(status.rx_duplicates);
    video_print("/", 0x07);
    print_num(status.rx_out_of_order);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static int cmd_net_wait_socket_connected(
    net_socket_handle_t handle, net_socket_info_t* out_info) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    net_socket_info_t info;
    int result;

    if (!out_info) {
        LOG_ERROR("SHELL", "Destino nulo na espera TCP");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_TCP_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera TCP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_TCP_WAIT_SECONDS;
    do {
        result = net_socket_get_handle_info(handle, &info);
        if (result != OK) return result;
        if (info.state == NET_SOCKET_STATE_CONNECTED) {
            *out_info = info;
            return OK;
        }
        if (info.state == NET_SOCKET_STATE_ERROR ||
            info.state == NET_SOCKET_STATE_EOF) {
            *out_info = info;
            return info.last_error == OK ? ERR_STATE :
                                           info.last_error;
        }
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    LOG_WARN("SHELL", "Guarda de tempo TCP expirou");
    return ERR_TIMEOUT;
}

static void cmd_net_tcp_connect(const char* args) {
    char target[SHELL_DNS_NAME_SIZE];
    char port_text[6];
    net_socket_handle_t handle = 0;
    net_socket_info_t info;
    uint32_t address = 0;
    uint16_t port = 0;
    int result;

    result = shell_read_two_args(args, target, sizeof(target),
                                 port_text, sizeof(port_text));
    if (result != OK ||
        cmd_net_parse_port(port_text, &port) != OK) {
        LOG_WARN("SHELL", "Uso invalido de net tcp connect");
        video_print("Uso: net tcp connect <ip-ou-dominio> <porta>\n",
                    0x0C);
        return;
    }
    result = cmd_ping_resolve_target(target, &address);
    if (result == OK) {
        result = net_socket_open(NET_SOCKET_TYPE_STREAM, &handle);
    }
    if (result == OK) result = net_socket_connect(handle, address, port);
    if (result == OK) {
        video_print("Conectando a ", 0x07);
        cmd_net_print_ipv4(address);
        video_print(":", 0x07);
        print_num(port);
        video_print("...\n", 0x07);
        result = cmd_net_wait_socket_connected(handle, &info);
    }
    if (result != OK) {
        if (handle) net_socket_abort(handle);
        video_print("Erro: conexao TCP falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Conexao TCP estabelecida. Porta local: ", 0x0A);
    print_num(info.local_port);
    video_print(".\n", 0x0A);
    net_socket_abort(handle);
}

static void cmd_net_tcp(const char* args) {
    const char* connect_args = shell_match_subcommand(args, "connect");

    if (shell_args_equal(args, "status")) {
        cmd_net_tcp_status();
        return;
    }
    if (connect_args) {
        cmd_net_tcp_connect(connect_args);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net tcp");
    video_print("Uso: net tcp status | "
                "net tcp connect <ip-ou-dominio> <porta>\n", 0x0C);
}

static void cmd_net_socket_status(void) {
    net_socket_status_t status;

    if (net_socket_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado de sockets indisponivel");
        video_print("Erro: estado de sockets indisponivel.\n", 0x0C);
        return;
    }
    video_print("Sockets nativos:\n  Estado: ", 0x0B);
    video_print(status.initialized ? "DISPONIVEL" : "INDISPONIVEL",
                status.initialized ? 0x0A : 0x0E);
    video_print("\n  Ativos: ", 0x07);
    print_num(status.active_count);
    video_print("/", 0x07);
    print_num(NET_SOCKET_CAPACITY);
    video_print("  Opens/connects/closes/aborts: ", 0x07);
    print_num(status.opens);
    video_print("/", 0x07);
    print_num(status.connects);
    video_print("/", 0x07);
    print_num(status.closes);
    video_print("/", 0x07);
    print_num(status.aborts);
    video_print("\n  Bytes fila TX/TCP TX/TCP RX/leitura: ", 0x07);
    print_num(status.bytes_queued_tx);
    video_print("/", 0x07);
    print_num(status.bytes_sent_tcp);
    video_print("/", 0x07);
    print_num(status.bytes_received_tcp);
    video_print("/", 0x07);
    print_num(status.bytes_read);
    video_print("  Overflows/stale: ", 0x07);
    print_num(status.rx_overflows);
    video_print("/", 0x07);
    print_num(status.stale_handles);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_net_socket_table(void) {
    uint8_t any = 0;

    video_print("Tabela de sockets:\n", 0x0B);
    for (uint32_t index = 0; index < NET_SOCKET_CAPACITY; index++) {
        net_socket_info_t info;

        if (net_socket_get_info(index, &info) != OK || !info.used) {
            continue;
        }
        any = 1;
        video_print("  #", 0x07);
        print_num(info.handle);
        video_print(" ", 0x07);
        video_print(net_socket_state_name(info.state),
                    info.state == NET_SOCKET_STATE_CONNECTED ? 0x0A :
                    info.state == NET_SOCKET_STATE_ERROR ? 0x0C : 0x0E);
        video_print(" ", 0x07);
        cmd_net_print_ipv4(info.remote_ip);
        video_print(":", 0x07);
        print_num(info.remote_port);
        video_print(" local=", 0x07);
        print_num(info.local_port);
        video_print(" tx/rx=", 0x07);
        print_num(info.tx_queued);
        video_print("/", 0x07);
        print_num(info.rx_queued);
        video_print("\n", 0x07);
    }
    if (!any) video_print("  Vazia.\n", 0x08);
}

static void cmd_net_socket(const char* args) {
    if (shell_args_equal(args, "status")) {
        cmd_net_socket_status();
        return;
    }
    if (shell_args_equal(args, "table")) {
        cmd_net_socket_table();
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net socket");
    video_print("Uso: net socket status | net socket table\n", 0x0C);
}

static int cmd_http_wait(http_status_t* out_status) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;
    http_status_t* status = &shell_http_wait_status;

    if (!out_status) {
        LOG_ERROR("SHELL", "Destino nulo na espera HTTP");
        return ERR_NULL;
    }
    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_HTTP_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera HTTP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_HTTP_WAIT_SECONDS;
    do {
        if (http_get_status(status) != OK) return ERR_STATE;
        if (status->state == HTTP_STATE_COMPLETE) {
            *out_status = *status;
            return OK;
        }
        if (status->state == HTTP_STATE_FAILED) {
            *out_status = *status;
            return status->last_error == OK ? ERR_STATE :
                                              status->last_error;
        }
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    *out_status = *status;
    LOG_WARN("SHELL", "Guarda de tempo HTTP expirou");
    return ERR_TIMEOUT;
}

static void cmd_http_status(void) {
    http_status_t status;

    if (http_get_status(&status) != OK) {
        LOG_ERROR("SHELL", "Estado HTTP indisponivel");
        video_print("Erro: estado HTTP indisponivel.\n", 0x0C);
        return;
    }
    video_print("HTTP:\n  Estado: ", 0x0B);
    video_print(http_state_name(status.state),
                status.state == HTTP_STATE_COMPLETE ? 0x0A :
                status.state == HTTP_STATE_FAILED ? 0x0C : 0x0E);
    video_print("\n  URL: ", 0x07);
    video_print(status.url[0] ? status.url : "N/D", 0x07);
    video_print("\n  Destino: ", 0x07);
    if (status.resolved_ip) cmd_net_print_ipv4(status.resolved_ip);
    else video_print("N/D", 0x08);
    video_print(":", 0x07);
    print_num(status.port);
    video_print("  Status: ", 0x07);
    print_num(status.status_code);
    video_print("\n  Headers/corpo: ", 0x07);
    print_num(status.headers_length);
    video_print("/", 0x07);
    print_num(status.body_length);
    video_print("  Content-Length: ", 0x07);
    if (status.has_content_length) print_num(status.content_length);
    else video_print("N/D (ate EOF)", 0x08);
    video_print("\n  Requests/respostas: ", 0x07);
    print_num(status.requests_tx);
    video_print("/", 0x07);
    print_num(status.responses_rx);
    video_print("  Bytes RX: ", 0x07);
    print_num(status.bytes_rx);
    video_print("  Parse/overflow/timeout: ", 0x07);
    print_num(status.parse_errors);
    video_print("/", 0x07);
    print_num(status.overflows);
    video_print("/", 0x07);
    print_num(status.timeouts);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
}

static void cmd_http_print_preview(void) {
    const uint8_t* body;
    uint32_t length;

    if (http_get_body(&body, &length) != OK) return;
    if (length > SHELL_HTTP_PREVIEW_SIZE) {
        length = SHELL_HTTP_PREVIEW_SIZE;
    }
    for (uint32_t index = 0; index < length; index++) {
        uint8_t value = body[index];

        shell_http_preview[index] =
            (value >= 0x20U && value <= 0x7EU) ||
            value == '\n' || value == '\r' || value == '\t' ?
            (char)value : '.';
    }
    shell_http_preview[length] = '\0';
    video_print("Previa do corpo", 0x0B);
    if (length == SHELL_HTTP_PREVIEW_SIZE) {
        video_print(" (primeiros 512 bytes)", 0x0B);
    }
    video_print(":\n", 0x0B);
    video_print(length ? shell_http_preview : "(vazio)", 0x07);
    video_print("\n", 0x07);
}

static int cmd_http_execute(const char* url, uint8_t print_result,
                            http_status_t* out_status) {
    int result;

    if (!url || !out_status) {
        LOG_ERROR("SHELL", "Argumento nulo no GET HTTP");
        return ERR_NULL;
    }
    result = http_get_start(url);
    if (result == OK) result = cmd_http_wait(out_status);
    if (result == OK && print_result) {
        video_print("HTTP ", 0x0A);
        print_num(out_status->status_code);
        video_print("  headers=", 0x07);
        print_num(out_status->headers_length);
        video_print(" corpo=", 0x07);
        print_num(out_status->body_length);
        video_print(" bytes\n", 0x07);
        cmd_http_print_preview();
    }
    return result;
}

static void cmd_http(const char* args) {
    const char* get_args = shell_match_subcommand(args, "get");
    int result;

    if (shell_args_equal(args, "status")) {
        cmd_http_status();
        return;
    }
    if (!get_args ||
        shell_read_single_arg(get_args, shell_http_command_url,
                              sizeof(shell_http_command_url)) != OK) {
        LOG_WARN("SHELL", "Uso invalido de http");
        video_print("Uso: http get <url> | http status\n", 0x0C);
        return;
    }
    video_print("Executando HTTP GET...\n", 0x07);
    result = cmd_http_execute(shell_http_command_url, 1U,
                              &shell_http_command_status);
    if (result != OK) {
        if (result == ERR_UNAVAILABLE) {
            video_print(
                "Erro: resposta usa Transfer-Encoding nao suportado.\n",
                0x0C);
        } else {
            video_print("Erro: HTTP GET falhou (codigo ", 0x0C);
            print_num((uint32_t)result);
            video_print(").\n", 0x0C);
        }
    }
}

static int cmd_net_find_arp_entry(uint32_t ip_address,
                                  arp_cache_entry_info_t* out_entry,
                                  uint8_t* out_found) {
    if (!out_entry || !out_found) {
        LOG_ERROR("SHELL", "Destino nulo ao buscar entrada ARP");
        return ERR_NULL;
    }
    *out_found = 0;
    kmemset(out_entry, 0, sizeof(*out_entry));
    for (uint32_t index = 0; index < ARP_CACHE_CAPACITY; index++) {
        int result = arp_get_cache_entry(index, out_entry);

        if (result != OK) {
            LOG_ERROR("SHELL", "Falha ao buscar entrada ARP da suite");
            return result;
        }
        if (out_entry->used && out_entry->ip_address == ip_address) {
            *out_found = 1;
            return OK;
        }
    }
    return OK;
}

static int cmd_net_wait_arp_state(uint32_t ip_address,
                                  arp_entry_state_t expected_state) {
    arp_cache_entry_info_t entry;
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick;
    uint32_t wait_ticks;
    uint8_t found;
    int result;

    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL / SHELL_NET_CHECK_WAIT_SECONDS) {
        LOG_ERROR("SHELL", "Timer invalido na espera da suite ARP");
        return ERR_STATE;
    }
    wait_ticks = frequency * SHELL_NET_CHECK_WAIT_SECONDS;
    start_tick = timer_get_ticks();
    do {
        result = cmd_net_find_arp_entry(ip_address, &entry, &found);
        if (result != OK) return result;
        if (found && entry.state == expected_state) return OK;
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
    LOG_WARN("SHELL", "Estado ARP esperado nao apareceu na suite");
    return ERR_TIMEOUT;
}

static int cmd_net_check_qemu_reply(const arp_status_t* baseline,
                                    shell_net_qemu_check_t* check) {
    arp_status_t after_reply;
    arp_status_t after_hit;
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved = 0;
    int result;

    if (!baseline || !check) {
        LOG_ERROR("SHELL", "Estado nulo no teste ARP de reply");
        return ERR_NULL;
    }
    result = arp_resolve(SHELL_NET_QEMU_REPLY_IPV4, mac_address, &resolved);
    if (result != OK || resolved) {
        LOG_WARN("SHELL", "Request inicial da suite ARP falhou");
        return result != OK ? result : ERR_STATE;
    }
    result = cmd_net_wait_arp_state(SHELL_NET_QEMU_REPLY_IPV4,
                                    ARP_ENTRY_RESOLVED);
    if (result != OK) return result;
    result = arp_get_status(&after_reply);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel depois do reply");
        return result;
    }
    check->reply = after_reply.rx_replies > baseline->rx_replies &&
                   after_reply.tx_requests > baseline->tx_requests;
    result = arp_resolve(SHELL_NET_QEMU_REPLY_IPV4, mac_address, &resolved);
    if (result != OK) {
        LOG_WARN("SHELL", "Consulta de cache ARP falhou na suite");
        return result;
    }
    result = arp_get_status(&after_hit);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel depois do cache hit");
        return result;
    }
    check->cache_hit = resolved &&
        after_hit.cache_hits == after_reply.cache_hits + 1U &&
        after_hit.tx_requests == after_reply.tx_requests;
    return OK;
}

static int cmd_net_check_qemu_timeout(const arp_status_t* baseline,
                                      shell_net_qemu_check_t* check) {
    arp_cache_entry_info_t entry;
    arp_status_t after_timeout;
    uint8_t mac_address[ARP_MAC_ADDRESS_SIZE];
    uint8_t resolved = 0;
    uint8_t found = 0;
    int result;

    if (!baseline || !check) {
        LOG_ERROR("SHELL", "Estado nulo no teste ARP de timeout");
        return ERR_NULL;
    }
    result = arp_resolve(SHELL_NET_QEMU_TIMEOUT_IPV4,
                         mac_address, &resolved);
    if (result != OK || resolved) {
        LOG_WARN("SHELL", "Request de timeout da suite ARP falhou");
        return result != OK ? result : ERR_STATE;
    }
    result = cmd_net_wait_arp_state(SHELL_NET_QEMU_TIMEOUT_IPV4,
                                    ARP_ENTRY_FAILED);
    if (result != OK) return result;
    result = cmd_net_find_arp_entry(SHELL_NET_QEMU_TIMEOUT_IPV4,
                                    &entry, &found);
    if (result != OK) return result;
    result = arp_get_status(&after_timeout);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado ARP indisponivel depois do timeout");
        return result;
    }
    check->timeout = found && entry.state == ARP_ENTRY_FAILED &&
        entry.attempts == SHELL_NET_CHECK_EXPECTED_ATTEMPTS &&
        after_timeout.timeouts > baseline->timeouts;
    return OK;
}

static void cmd_net_check_print_case(const char* label, uint8_t passed) {
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(": ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static int cmd_net_check_qemu_icmp(
    const ipv4_status_t* ipv4_baseline,
    const icmp_status_t* icmp_baseline,
    shell_net_qemu_check_t* check) {
    ipv4_status_t ipv4_after;
    icmp_status_t icmp_after;
    int result;

    if (!ipv4_baseline || !icmp_baseline || !check) {
        LOG_ERROR("SHELL", "Estado nulo no teste ICMP QEMU");
        return ERR_NULL;
    }
    result = cmd_ping_execute(SHELL_NET_QEMU_REPLY_IPV4, 1U, 0U,
                              &icmp_after);
    if (result != OK) {
        LOG_WARN("SHELL", "Ping ICMP da suite QEMU falhou");
        return result;
    }
    if (ipv4_get_status(&ipv4_after) != OK) {
        LOG_ERROR("SHELL", "Estado IPv4 final da suite indisponivel");
        return ERR_STATE;
    }
    check->ipv4 =
        ipv4_after.tx_packets > ipv4_baseline->tx_packets &&
        ipv4_after.rx_packets > ipv4_baseline->rx_packets &&
        ipv4_after.rx_checksum_errors ==
            ipv4_baseline->rx_checksum_errors &&
        ipv4_after.rx_delivered > ipv4_baseline->rx_delivered;
    check->icmp =
        icmp_after.state == ICMP_PING_COMPLETE &&
        icmp_after.sent == 1U && icmp_after.received == 1U &&
        icmp_after.timeouts == 0U &&
        icmp_after.echo_requests_tx >
            icmp_baseline->echo_requests_tx &&
        icmp_after.echo_replies_rx >
            icmp_baseline->echo_replies_rx &&
        icmp_after.rtt_total_ticks > 0U;
    return check->ipv4 && check->icmp ? OK : ERR_STATE;
}

static int cmd_net_check_qemu_dhcp_acquire(
    const char* id, const udp_status_t* udp_before,
    const dhcp_status_t* dhcp_before,
    shell_net_qemu_dhcp_check_t* check) {
    udp_status_t udp_after;
    dhcp_status_t dhcp_after;
    int result = network_manager_acquire_dhcp(id);

    if (result == OK) result = cmd_net_dhcp_wait(&dhcp_after);
    if (result != OK) return result;
    if (udp_get_status(&udp_after) != OK) {
        LOG_ERROR("SHELL", "Estado UDP final da suite indisponivel");
        return ERR_STATE;
    }
    check->udp =
        udp_after.tx_datagrams > udp_before->tx_datagrams &&
        udp_after.rx_datagrams > udp_before->rx_datagrams &&
        udp_after.rx_checksum_errors ==
            udp_before->rx_checksum_errors;
    check->dhcp =
        dhcp_after.state == DHCP_STATE_BOUND &&
        dhcp_after.discovers_tx > dhcp_before->discovers_tx &&
        dhcp_after.offers_rx > dhcp_before->offers_rx &&
        dhcp_after.requests_tx > dhcp_before->requests_tx &&
        dhcp_after.acks_rx > dhcp_before->acks_rx;
    check->lease =
        dhcp_after.lease.address != 0U &&
        dhcp_after.lease.subnet_mask == SHELL_NET_QEMU_SUBNET_MASK &&
        (dhcp_after.lease.address & SHELL_NET_QEMU_SUBNET_MASK) ==
            0x0A000200U &&
        dhcp_after.lease.gateway == SHELL_NET_QEMU_REPLY_IPV4 &&
        dhcp_after.lease.dns_server == 0x0A000203U;
    return check->udp && check->dhcp && check->lease ? OK : ERR_STATE;
}

static int cmd_net_check_qemu_dns(
    const char* domain, const dns_status_t* dns_before,
    shell_net_qemu_dhcp_check_t* check) {
    static dns_status_t dns_after;
    static udp_status_t udp_before_hit;
    static udp_status_t udp_after_hit;
    uint32_t address = 0;
    uint32_t cached_address = 0;
    uint8_t resolved = 0;
    int result = cmd_dns_wait(domain, &address);

    if (result != OK) return result;
    if (dns_get_status(&dns_after) != OK ||
        udp_get_status(&udp_before_hit) != OK) {
        LOG_ERROR("SHELL", "Estado DNS/UDP final da suite indisponivel");
        return ERR_STATE;
    }
    check->dns =
        ipv4_address_is_unicast(address) &&
        dns_after.queries_tx > dns_before->queries_tx &&
        dns_after.replies_rx > dns_before->replies_rx &&
        dns_after.invalid_packets == dns_before->invalid_packets;
    result = dns_resolve(domain, &cached_address, &resolved);
    if (result != OK || udp_get_status(&udp_after_hit) != OK) {
        return result != OK ? result : ERR_STATE;
    }
    check->dns_cache =
        resolved && cached_address == address &&
        udp_after_hit.tx_datagrams == udp_before_hit.tx_datagrams;
    return check->dns && check->dns_cache ? OK : ERR_STATE;
}

static int cmd_net_check_qemu_dhcp_ping(
    shell_net_qemu_dhcp_check_t* check) {
    ipv4_status_t ipv4_before;
    ipv4_status_t ipv4_after;
    icmp_status_t icmp_before;
    icmp_status_t icmp_after;
    int result;

    if (ipv4_get_status(&ipv4_before) != OK ||
        icmp_get_status(&icmp_before) != OK) {
        LOG_ERROR("SHELL", "Linha-base ICMP S2.6 indisponivel");
        return ERR_STATE;
    }
    result = cmd_ping_execute(SHELL_NET_QEMU_REPLY_IPV4, 1U, 0U,
                              &icmp_after);
    if (result != OK) return result;
    if (ipv4_get_status(&ipv4_after) != OK) {
        LOG_ERROR("SHELL", "Estado IPv4 final da suite indisponivel");
        return ERR_STATE;
    }
    check->icmp =
        ipv4_after.tx_packets > ipv4_before.tx_packets &&
        ipv4_after.rx_packets > ipv4_before.rx_packets &&
        ipv4_after.rx_checksum_errors ==
            ipv4_before.rx_checksum_errors &&
        icmp_after.state == ICMP_PING_COMPLETE &&
        icmp_after.received == 1U &&
        icmp_after.echo_replies_rx > icmp_before.echo_replies_rx;
    return check->icmp ? OK : ERR_STATE;
}

static void cmd_net_check_qemu_dhcp_print(
    const shell_net_qemu_dhcp_check_t* check,
    uint8_t passed) {
    cmd_net_check_print_case("UDP RX/TX e checksum", check->udp);
    cmd_net_check_print_case("DHCP Discover/Offer/Request/ACK",
                             check->dhcp);
    cmd_net_check_print_case("Lease, gateway e DNS QEMU", check->lease);
    cmd_net_check_print_case("Consulta DNS A", check->dns);
    cmd_net_check_print_case("Cache DNS sem novo TX", check->dns_cache);
    cmd_net_check_print_case("IPv4/ICMP para gateway", check->icmp);
    cmd_net_check_print_case("Polling e manutencao", check->polling);
    cmd_net_check_print_case("Invariantes de rede", check->invariants);
    video_print("Resultado da suite: ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n",
                passed ? 0x0A : 0x0C);
}

static SHELL_NOINLINE void cmd_net_check_qemu_dhcp(const char* args) {
    /* O compilador nao pode incorporar estes snapshots ao dispatcher:
       a pilha do processo Shell possui somente 4 KiB. */
    static char id[NETWORK_INTERFACE_ID_SIZE];
    static char domain[SHELL_DNS_NAME_SIZE];
    static udp_status_t udp_before;
    static dhcp_status_t dhcp_before;
    static dhcp_status_t dhcp_after;
    static dns_status_t dns_before;
    static dns_status_t dns_after;
    static shell_net_qemu_dhcp_check_t check;
    int acquire_result;
    int dns_result;
    int ping_result;
    uint8_t passed;
    uint8_t release_sent = 0;

    if (shell_read_two_args(args, id, sizeof(id),
                            domain, sizeof(domain)) != OK) {
        LOG_WARN("SHELL", "Uso invalido da suite DHCP QEMU");
        video_print("Uso: net check qemu dhcp <id> <dominio>\n", 0x0C);
        return;
    }
    if (dhcp_get_status(&dhcp_before) != OK) {
        video_print("Erro: estado DHCP indisponivel.\n", 0x0C);
        return;
    }
    if (dhcp_before.state == DHCP_STATE_BOUND ||
        dhcp_before.state == DHCP_STATE_RENEWING ||
        dhcp_before.state == DHCP_STATE_REBINDING) {
        if (network_manager_release_dhcp(&release_sent) != OK) {
            video_print("Erro: lease anterior nao foi removido.\n", 0x0C);
            return;
        }
    } else if (dhcp_before.state == DHCP_STATE_SELECTING ||
               dhcp_before.state == DHCP_STATE_REQUESTING ||
               dhcp_before.state == DHCP_STATE_APPLYING) {
        if (dhcp_reset() != OK) {
            video_print("Erro: sessao DHCP anterior nao foi reiniciada.\n",
                        0x0C);
            return;
        }
    }
    if (udp_get_status(&udp_before) != OK ||
        dhcp_get_status(&dhcp_before) != OK ||
        dns_get_status(&dns_before) != OK) {
        video_print("Erro: linha-base S2.6 indisponivel.\n", 0x0C);
        return;
    }
    kmemset(&check, 0, sizeof(check));
    video_print("=== Suite de rede - DHCP/DNS QEMU ===\n", 0x0B);
    video_print("Aguardando DHCP, DNS e ICMP...\n", 0x07);
    acquire_result = cmd_net_check_qemu_dhcp_acquire(
        id, &udp_before, &dhcp_before, &check);
    if (acquire_result == OK && dns_clear() == OK &&
        dns_get_status(&dns_before) == OK) {
        dns_result = cmd_net_check_qemu_dns(
            domain, &dns_before, &check);
    } else {
        dns_result = ERR_STATE;
    }
    ping_result = acquire_result == OK ?
        cmd_net_check_qemu_dhcp_ping(&check) : ERR_STATE;
    if (dhcp_get_status(&dhcp_after) == OK &&
        dns_get_status(&dns_after) == OK) {
        check.polling =
            dhcp_after.maintenance_cycles >
                dhcp_before.maintenance_cycles &&
            dns_after.maintenance_cycles >
                dns_before.maintenance_cycles;
    }
    check.invariants = shell_regcheck_validate_network() == OK;
    passed = acquire_result == OK && dns_result == OK &&
        ping_result == OK && check.udp && check.dhcp && check.lease &&
        check.dns && check.dns_cache && check.icmp &&
        check.polling && check.invariants;
    cmd_net_check_qemu_dhcp_print(&check, passed);
    cmd_net_udp_status();
    cmd_net_dhcp_status();
    cmd_net_dns_status();
    cmd_net_dns_table();
    cmd_net_ipv4_status();
}

static uint8_t shell_network_id_equal(const char* first,
                                      const char* second) {
    if (!first || !second) return 0;
    while (*first && *second) {
        char left = *first == ':' ? '-' : *first;
        char right = *second == ':' ? '-' : *second;

        if (left >= 'a' && left <= 'z') left -= (char)('a' - 'A');
        if (right >= 'a' && right <= 'z') right -= (char)('a' - 'A');
        if (left != right) return 0;
        first++;
        second++;
    }
    return *first == '\0' && *second == '\0';
}

static int cmd_net_qemu_tcp_prepare_dhcp(
    const char* id, shell_net_qemu_tcp_check_t* check) {
    dhcp_status_t status;
    uint8_t release_sent = 0;
    int result;

    if (!id || !check) {
        LOG_ERROR("SHELL", "Argumento nulo na preparacao TCP/DHCP");
        return ERR_NULL;
    }
    if (dhcp_get_status(&status) != OK) return ERR_STATE;
    if ((status.state == DHCP_STATE_BOUND ||
         status.state == DHCP_STATE_RENEWING ||
         status.state == DHCP_STATE_REBINDING) &&
        (!shell_network_id_equal(status.interface_id, id) ||
         status.lease.subnet_mask != SHELL_NET_QEMU_SUBNET_MASK ||
         status.lease.gateway != SHELL_NET_QEMU_REPLY_IPV4 ||
         !status.lease.dns_server)) {
        result = network_manager_release_dhcp(&release_sent);
        if (result != OK) return result;
        if (dhcp_get_status(&status) != OK) return ERR_STATE;
    }
    if (status.state != DHCP_STATE_BOUND &&
        status.state != DHCP_STATE_RENEWING &&
        status.state != DHCP_STATE_REBINDING) {
        if (status.state == DHCP_STATE_SELECTING ||
            status.state == DHCP_STATE_REQUESTING ||
            status.state == DHCP_STATE_APPLYING) {
            result = dhcp_reset();
            if (result != OK) return result;
        }
        result = network_manager_acquire_dhcp(id);
        if (result == OK) result = cmd_net_dhcp_wait(&status);
        if (result != OK) return result;
    }
    if (dhcp_get_status(&status) != OK) return ERR_STATE;
    check->dhcp =
        (status.state == DHCP_STATE_BOUND ||
         status.state == DHCP_STATE_RENEWING ||
         status.state == DHCP_STATE_REBINDING) &&
        status.lease.address != 0U &&
        status.lease.subnet_mask == SHELL_NET_QEMU_SUBNET_MASK &&
        status.lease.gateway == SHELL_NET_QEMU_REPLY_IPV4 &&
        status.lease.dns_server != 0U;
    return check->dhcp ? OK : ERR_STATE;
}

static int cmd_net_build_http_url(const char* domain,
                                  char* url,
                                  uint32_t capacity) {
    static const char prefix[] = "http://";
    uint32_t prefix_length = sizeof(prefix) - 1U;
    uint32_t domain_length;

    if (!domain || !url || !capacity) {
        LOG_ERROR("SHELL", "Destino nulo ao montar URL da suite TCP");
        return ERR_NULL;
    }
    domain_length = kstrlen(domain);
    if (!domain_length ||
        prefix_length + domain_length + 2U > capacity) {
        LOG_ERROR("SHELL", "Dominio excede URL da suite TCP");
        return ERR_OVERFLOW;
    }
    kmemcpy(url, prefix, prefix_length);
    kmemcpy(url + prefix_length, domain, domain_length);
    url[prefix_length + domain_length] = '/';
    url[prefix_length + domain_length + 1U] = '\0';
    return OK;
}

static void cmd_net_qemu_tcp_wait_close(uint32_t fin_baseline) {
    uint32_t frequency = timer_get_frequency();
    uint32_t start_tick = timer_get_ticks();
    uint32_t wait_ticks;

    if (!frequency ||
        frequency > SHELL_MAX_TICK_INTERVAL /
                    SHELL_NET_CHECK_WAIT_SECONDS) return;
    wait_ticks = frequency * SHELL_NET_CHECK_WAIT_SECONDS;
    do {
        tcp_status_t status;

        if (tcp_get_status(&status) == OK &&
            status.fin_tx > fin_baseline) return;
        process_block(SHELL_NET_CHECK_BLOCK_TICKS);
    } while ((uint32_t)(timer_get_ticks() - start_tick) <= wait_ticks);
}

static int cmd_net_qemu_tcp_run_http(
    const char* url, http_status_t* out_status) {
    int result = ERR_TIMEOUT;

    if (!url || !out_status) {
        LOG_ERROR("SHELL", "Argumento nulo no HTTP da suite TCP");
        return ERR_NULL;
    }
    for (uint32_t attempt = 0;
         attempt < SHELL_HTTP_SUITE_ATTEMPTS; attempt++) {
        if (attempt) {
            video_print("[WRN] Timeout HTTP; repetindo conexao ", 0x0E);
            print_num(attempt + 1U);
            video_print("/", 0x0E);
            print_num(SHELL_HTTP_SUITE_ATTEMPTS);
            video_print("...\n", 0x0E);
            result = http_reset();
            if (result != OK) return result;
        }
        result = cmd_http_execute(url, 0U, out_status);
        if (result != ERR_TIMEOUT) return result;
    }
    return result;
}

static void cmd_net_check_qemu_tcp_print(
    const shell_net_qemu_tcp_check_t* check, uint8_t passed) {
    cmd_net_check_print_case("Lease DHCP QEMU", check->dhcp);
    cmd_net_check_print_case("Resolucao DNS A", check->dns);
    cmd_net_check_print_case("Handshake e dados TCP", check->tcp);
    cmd_net_check_print_case("Checksum TCP", check->checksum);
    cmd_net_check_print_case("Socket RX/TX", check->sockets);
    cmd_net_check_print_case("Resposta HTTP suportada", check->http);
    cmd_net_check_print_case("Fechamento TCP", check->closing);
    cmd_net_check_print_case("Polling e manutencao", check->polling);
    cmd_net_check_print_case("Invariantes de rede", check->invariants);
    video_print("Resultado da suite: ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n",
                passed ? 0x0A : 0x0C);
}

/* A suite e serial no Shell. Os snapshots em BSS evitam somar varios
   status grandes a pilha de 4 KiB durante a espera HTTP. */
static SHELL_NOINLINE void cmd_net_check_qemu_tcp(const char* args) {
    static char id[NETWORK_INTERFACE_ID_SIZE];
    static char domain[SHELL_DNS_NAME_SIZE];
    static char url[HTTP_URL_BUFFER_SIZE];
    static shell_net_qemu_tcp_check_t check;
    static tcp_status_t tcp_before;
    static tcp_status_t tcp_after;
    static net_socket_status_t sockets_before;
    static net_socket_status_t sockets_after;
    static http_status_t http_before;
    static http_status_t http_after;
    uint32_t address = 0;
    int result;
    uint8_t passed;
    uint8_t metrics_valid = 0;

    if (shell_read_two_args(args, id, sizeof(id),
                            domain, sizeof(domain)) != OK) {
        LOG_WARN("SHELL", "Uso invalido da suite TCP QEMU");
        video_print("Uso: net check qemu tcp <id> <dominio>\n", 0x0C);
        return;
    }
    kmemset(&check, 0, sizeof(check));
    kmemset(&tcp_before, 0, sizeof(tcp_before));
    kmemset(&tcp_after, 0, sizeof(tcp_after));
    kmemset(&sockets_before, 0, sizeof(sockets_before));
    kmemset(&sockets_after, 0, sizeof(sockets_after));
    kmemset(&http_before, 0, sizeof(http_before));
    kmemset(&http_after, 0, sizeof(http_after));
    video_print("=== Suite de rede - TCP/HTTP QEMU ===\n", 0x0B);
    video_print("Aguardando DHCP, DNS, TCP e HTTP...\n", 0x07);
    result = cmd_net_qemu_tcp_prepare_dhcp(id, &check);
    if (result == OK) {
        result = cmd_dns_wait(domain, &address);
        check.dns = result == OK && ipv4_address_is_unicast(address);
    }
    if (result == OK) result = cmd_net_build_http_url(
        domain, url, sizeof(url));
    if (result == OK) result = http_reset();
    if (result == OK &&
        (tcp_get_status(&tcp_before) != OK ||
         net_socket_get_status(&sockets_before) != OK ||
         http_get_status(&http_before) != OK)) {
        result = ERR_STATE;
    } else if (result == OK) {
        metrics_valid = 1;
    }
    if (result == OK) {
        result = cmd_net_qemu_tcp_run_http(url, &http_after);
        if (result == OK) {
            cmd_net_qemu_tcp_wait_close(tcp_before.fin_tx);
        }
    }
    if (tcp_get_status(&tcp_after) != OK ||
        net_socket_get_status(&sockets_after) != OK ||
        http_get_status(&http_after) != OK) {
        result = ERR_STATE;
        metrics_valid = 0;
        kmemset(&tcp_after, 0, sizeof(tcp_after));
        kmemset(&sockets_after, 0, sizeof(sockets_after));
        kmemset(&http_after, 0, sizeof(http_after));
    }
    check.tcp = metrics_valid &&
        tcp_after.syn_tx > tcp_before.syn_tx &&
        tcp_after.syn_ack_rx > tcp_before.syn_ack_rx &&
        tcp_after.segments_rx > tcp_before.segments_rx &&
        tcp_after.segments_tx > tcp_before.segments_tx;
    check.checksum = metrics_valid &&
        tcp_after.rx_checksum_errors == tcp_before.rx_checksum_errors;
    check.sockets = metrics_valid &&
        sockets_after.connects > sockets_before.connects &&
        sockets_after.bytes_sent_tcp > sockets_before.bytes_sent_tcp &&
        sockets_after.bytes_received_tcp >
            sockets_before.bytes_received_tcp;
    check.http = result == OK &&
        http_after.state == HTTP_STATE_COMPLETE &&
        http_after.resolved_ip == address &&
        http_after.status_code >= HTTP_STATUS_MINIMUM &&
        http_after.status_code <= HTTP_STATUS_MAXIMUM &&
        http_after.headers_length != 0U &&
        http_after.requests_tx > http_before.requests_tx &&
        http_after.responses_rx > http_before.responses_rx;
    check.closing = metrics_valid &&
        tcp_after.fin_tx > tcp_before.fin_tx;
    check.polling = metrics_valid &&
        tcp_after.maintenance_cycles > tcp_before.maintenance_cycles &&
        sockets_after.maintenance_cycles >
            sockets_before.maintenance_cycles &&
        http_after.maintenance_cycles >
            http_before.maintenance_cycles;
    check.invariants = shell_regcheck_validate_network() == OK;
    passed = check.dhcp && check.dns && check.tcp &&
        check.checksum && check.sockets && check.http &&
        check.closing && check.polling && check.invariants;
    cmd_net_check_qemu_tcp_print(&check, passed);
    cmd_net_tcp_status();
    cmd_net_socket_status();
    cmd_net_socket_table();
    cmd_http_status();
}

static SHELL_NOINLINE void cmd_net_check_qemu_static(const char* args) {
    static char id[NETWORK_INTERFACE_ID_SIZE];
    static char ip_text[SHELL_IPV4_TEXT_SIZE];
    static arp_status_t baseline;
    static arp_status_t final_status;
    static ipv4_status_t ipv4_baseline;
    static icmp_status_t icmp_baseline;
    static shell_net_qemu_check_t check;
    uint32_t local_ip = 0;
    uint8_t passed;
    int reply_result;
    int timeout_result;
    int icmp_result;
    int result;
    result = shell_read_two_args(args, id, sizeof(id),
                                 ip_text, sizeof(ip_text));
    if (result != OK || cmd_net_parse_ipv4(ip_text, &local_ip) != OK ||
        !arp_ipv4_is_valid(local_ip) ||
        local_ip == SHELL_NET_QEMU_REPLY_IPV4 ||
        local_ip == SHELL_NET_QEMU_TIMEOUT_IPV4) {
        LOG_WARN("SHELL", "Uso invalido de net check qemu");
        video_print("Uso: net check qemu <id> <ip-local> | "
                    "net check qemu dhcp <id> <dominio> | "
                    "net check qemu tcp <id> <dominio> | "
                    "net check qemu multi <id-a> <id-b>\n", 0x0C);
        return;
    }
    result = network_manager_configure_ipv4(
        id, local_ip, SHELL_NET_QEMU_SUBNET_MASK,
        SHELL_NET_QEMU_REPLY_IPV4);
    if (result != OK || arp_clear() != OK ||
        arp_get_status(&baseline) != OK ||
        ipv4_get_status(&ipv4_baseline) != OK ||
        icmp_get_status(&icmp_baseline) != OK) {
        LOG_ERROR("SHELL", "Preparacao da suite de rede QEMU falhou");
        video_print("Erro: nao foi possivel preparar a suite QEMU.\n", 0x0C);
        return;
    }
    kmemset(&check, 0, sizeof(check));
    video_print("=== Suite de rede - perfil QEMU ===\n", 0x0B);
    video_print("Aguardando ARP, IPv4, ICMP e timeout...\n", 0x07);
    reply_result = cmd_net_check_qemu_reply(&baseline, &check);
    timeout_result = cmd_net_check_qemu_timeout(&baseline, &check);
    icmp_result = cmd_net_check_qemu_icmp(
        &ipv4_baseline, &icmp_baseline, &check);
    result = arp_get_status(&final_status);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado final da suite ARP indisponivel");
        video_print("Erro: resultado final ARP indisponivel.\n", 0x0C);
        return;
    }
    check.polling =
        final_status.maintenance_cycles > baseline.maintenance_cycles &&
        final_status.maintenance_errors == baseline.maintenance_errors;
    check.invariants = shell_regcheck_validate_network() == OK;
    cmd_net_check_print_case("Request/reply 10.0.2.2", check.reply);
    cmd_net_check_print_case("Cache hit sem novo TX", check.cache_hit);
    cmd_net_check_print_case("Timeout 10.0.2.254", check.timeout);
    cmd_net_check_print_case("IPv4 RX/TX e checksum", check.ipv4);
    cmd_net_check_print_case("ICMP Echo e RTT", check.icmp);
    cmd_net_check_print_case("Polling e manutencao", check.polling);
    cmd_net_check_print_case("Invariantes de rede", check.invariants);
    passed = reply_result == OK && timeout_result == OK &&
        icmp_result == OK &&
        check.reply && check.cache_hit && check.timeout && check.polling &&
        check.ipv4 && check.icmp && check.invariants;
    video_print("Resultado da suite: ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
    cmd_net_arp_status();
    cmd_net_arp_table();
    cmd_net_ipv4_status();
}

/* A suite e serial no Shell. IDs e snapshots em BSS evitam exceder a
   pilha de 4 KiB durante os diagnosticos das duas interfaces. */
static SHELL_NOINLINE void cmd_net_check_qemu_multi(
    const char* args) {
    static char first_id[NETWORK_INTERFACE_ID_SIZE];
    static char second_id[NETWORK_INTERFACE_ID_SIZE];
    static network_interface_info_t first_info;
    static network_interface_info_t second_info;
    static network_interface_text_t first_text;
    static network_interface_text_t second_text;
    static ethernet_interface_status_t first_before;
    static ethernet_interface_status_t first_middle;
    static ethernet_interface_status_t first_after;
    static ethernet_interface_status_t second_before;
    static ethernet_interface_status_t second_middle;
    static ethernet_interface_status_t second_after;
    uint8_t first_isolated;
    uint8_t second_isolated;
    uint8_t invariants;
    int result;

    result = shell_read_two_args(
        args, first_id, sizeof(first_id),
        second_id, sizeof(second_id));
    if (result != OK ||
        network_manager_find(first_id, &first_info) != OK ||
        network_manager_find(second_id, &second_info) != OK ||
        network_manager_format_text(&first_info, &first_text) != OK ||
        network_manager_format_text(&second_info, &second_text) != OK ||
        first_info.state != NETWORK_INTERFACE_ACTIVE ||
        second_info.state != NETWORK_INTERFACE_ACTIVE ||
        kstrcmp(first_text.id, second_text.id) == 0) {
        LOG_WARN("SHELL", "Uso invalido da suite Multi-NIC QEMU");
        video_print(
            "Uso: net check qemu multi <id-a> <id-b>\n", 0x0C);
        return;
    }
    if (ethernet_get_interface_status(
            first_text.id, &first_before) != OK ||
        ethernet_get_interface_status(
            second_text.id, &second_before) != OK ||
        network_manager_send_diagnostic(first_text.id) != OK ||
        ethernet_get_interface_status(
            first_text.id, &first_middle) != OK ||
        ethernet_get_interface_status(
            second_text.id, &second_middle) != OK) {
        LOG_ERROR("SHELL", "Primeiro TX da suite Multi-NIC falhou");
        video_print("Erro: primeiro TX Multi-NIC falhou.\n", 0x0C);
        return;
    }
    first_isolated =
        first_middle.tx_frames == first_before.tx_frames + 1U &&
        first_middle.driver.tx_packets ==
            first_before.driver.tx_packets + 1U &&
        second_middle.tx_frames == second_before.tx_frames &&
        second_middle.driver.tx_packets ==
            second_before.driver.tx_packets;
    result = network_manager_send_diagnostic(second_text.id);
    if (result != OK ||
        ethernet_get_interface_status(
            first_text.id, &first_after) != OK ||
        ethernet_get_interface_status(
            second_text.id, &second_after) != OK) {
        LOG_ERROR("SHELL", "Segundo TX da suite Multi-NIC falhou");
        video_print("Erro: segundo TX Multi-NIC falhou.\n", 0x0C);
        return;
    }
    second_isolated =
        first_after.tx_frames == first_middle.tx_frames &&
        first_after.driver.tx_packets ==
            first_middle.driver.tx_packets &&
        second_after.tx_frames == second_middle.tx_frames + 1U &&
        second_after.driver.tx_packets ==
            second_middle.driver.tx_packets + 1U;
    invariants = shell_regcheck_validate_network() == OK;
    video_print("=== Suite de rede - Multi-NIC QEMU ===\n", 0x0B);
    cmd_net_check_print_case("TX isolado na primeira NIC",
                             first_isolated);
    cmd_net_check_print_case("TX isolado na segunda NIC",
                             second_isolated);
    cmd_net_check_print_case("Invariantes Multi-NIC", invariants);
    video_print("Resultado da suite: ", 0x07);
    video_print(first_isolated && second_isolated && invariants ?
                "OK\n" : "ERRO\n",
                first_isolated && second_isolated && invariants ?
                0x0A : 0x0C);
}

static SHELL_NOINLINE void cmd_net_check_qemu(const char* args) {
    const char* tcp_args = shell_match_subcommand(args, "tcp");
    const char* dhcp_args;
    const char* multi_args;

    multi_args = shell_match_subcommand(args, "multi");
    if (multi_args) {
        cmd_net_check_qemu_multi(multi_args);
        return;
    }
    if (tcp_args) {
        cmd_net_check_qemu_tcp(tcp_args);
        return;
    }
    dhcp_args = shell_match_subcommand(args, "dhcp");
    if (dhcp_args) {
        cmd_net_check_qemu_dhcp(dhcp_args);
        return;
    }
    cmd_net_check_qemu_static(args);
}

static void cmd_net_check(const char* args) {
    char id[NETWORK_INTERFACE_ID_SIZE];
    network_interface_info_t info;
    network_manager_status_t network_status;
    arp_status_t arp_status;
    const char* qemu_args;
    uint8_t has_interface = 0;
    int result;

    if (!args) {
        LOG_ERROR("SHELL", "Argumentos nulos em net check");
        video_print("Erro: diagnostico agrupado invalido.\n", 0x0C);
        return;
    }
    qemu_args = shell_match_subcommand(args, "qemu");
    if (qemu_args) {
        cmd_net_check_qemu(qemu_args);
        return;
    }
    if (*args) {
        result = shell_read_single_arg(args, id, sizeof(id));
        if (result != OK) {
            LOG_WARN("SHELL", "Uso invalido de net check");
            video_print("Uso: net check [id] | "
                        "net check qemu <id> <ip-local> | "
                        "net check qemu dhcp <id> <dominio> | "
                        "net check qemu tcp <id> <dominio> | "
                        "net check qemu multi <id-a> <id-b>\n", 0x0C);
            return;
        }
        result = network_manager_find(id, &info);
        if (result != OK) {
            LOG_WARN("SHELL", "Interface invalida em net check");
            video_print("Erro: interface de rede nao encontrada.\n", 0x0C);
            return;
        }
        has_interface = 1;
    }
    if (network_manager_get_status(&network_status) != OK) {
        LOG_ERROR("SHELL", "Estado Network indisponivel em net check");
        video_print("Erro: estado de rede indisponivel.\n", 0x0C);
        return;
    }

    video_print("=== Diagnostico de rede agrupado ===\n", 0x0B);
    video_print("\n[1] Estado geral\n", 0x0B);
    cmd_net_status();
    video_print("\n[2] Controladores\n", 0x0B);
    cmd_net_devices();
    if (has_interface) {
        video_print("\n[3] Interface\n", 0x0B);
        cmd_net_info(id);
        video_print("\n[4] Ethernet\n", 0x0B);
        if (info.state == NETWORK_INTERFACE_ACTIVE &&
            network_status.ethernet_available) {
            cmd_net_ethernet(id);
        } else {
            video_print("Diagnostico Ethernet nao aplicavel.\n", 0x0E);
        }
    }
    video_print(has_interface ? "\n[5] ARP\n" : "\n[3] ARP\n", 0x0B);
    cmd_net_arp_status();
    if (arp_get_status(&arp_status) == OK && arp_status.initialized) {
        cmd_net_arp_table();
    }
    video_print(has_interface ? "\n[6] IPv4 e ICMP\n" :
                                "\n[4] IPv4 e ICMP\n", 0x0B);
    cmd_net_ipv4_status();
    video_print(has_interface ? "\n[7] UDP\n" : "\n[5] UDP\n", 0x0B);
    cmd_net_udp_status();
    video_print(has_interface ? "\n[8] DHCP\n" : "\n[6] DHCP\n", 0x0B);
    cmd_net_dhcp_status();
    video_print(has_interface ? "\n[9] DNS\n" : "\n[7] DNS\n", 0x0B);
    cmd_net_dns_status();
    cmd_net_dns_table();
    video_print(has_interface ? "\n[10] TCP\n" : "\n[8] TCP\n", 0x0B);
    cmd_net_tcp_status();
    video_print(has_interface ? "\n[11] Sockets\n" :
                                "\n[9] Sockets\n", 0x0B);
    cmd_net_socket_status();
    cmd_net_socket_table();
    video_print(has_interface ? "\n[12] HTTP\n" :
                                "\n[10] HTTP\n", 0x0B);
    cmd_http_status();
    video_print(has_interface ? "\n[13] Invariantes: " :
                                "\n[11] Invariantes: ", 0x0B);
    result = shell_regcheck_validate_network();
    video_print(result == OK ? "OK\n" : "ERRO\n",
                result == OK ? 0x0A : 0x0C);
}

static void cmd_net(const char* args) {
    const char* arp_args;
    const char* check_args;
    const char* dhcp_args;
    const char* dns_args;
    const char* ethernet_args;
    const char* info_args;
    const char* ipv4_args;
    const char* socket_args;
    const char* tcp_args;
    const char* test_args;
    const char* udp_args;

    if (shell_args_equal(args, "status")) {
        cmd_net_status();
        return;
    }
    if (shell_args_equal(args, "devices")) {
        cmd_net_devices();
        return;
    }
    arp_args = shell_match_subcommand(args, "arp");
    if (arp_args) {
        cmd_net_arp(arp_args);
        return;
    }
    ipv4_args = shell_match_subcommand(args, "ipv4");
    if (ipv4_args) {
        cmd_net_ipv4(ipv4_args);
        return;
    }
    udp_args = shell_match_subcommand(args, "udp");
    if (udp_args) {
        cmd_net_udp(udp_args);
        return;
    }
    dhcp_args = shell_match_subcommand(args, "dhcp");
    if (dhcp_args) {
        cmd_net_dhcp(dhcp_args);
        return;
    }
    dns_args = shell_match_subcommand(args, "dns");
    if (dns_args) {
        cmd_net_dns(dns_args);
        return;
    }
    tcp_args = shell_match_subcommand(args, "tcp");
    if (tcp_args) {
        cmd_net_tcp(tcp_args);
        return;
    }
    socket_args = shell_match_subcommand(args, "socket");
    if (socket_args) {
        cmd_net_socket(socket_args);
        return;
    }
    check_args = shell_match_subcommand(args, "check");
    if (check_args) {
        cmd_net_check(check_args);
        return;
    }
    info_args = shell_match_subcommand(args, "info");
    if (info_args) {
        cmd_net_info(info_args);
        return;
    }
    ethernet_args = shell_match_subcommand(args, "ethernet");
    if (ethernet_args) {
        cmd_net_ethernet(ethernet_args);
        return;
    }
    test_args = shell_match_subcommand(args, "test");
    if (test_args) {
        cmd_net_test(test_args);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido de net");
    video_print("Uso: net status | net devices | net info <id> | "
                "net ethernet <id> | net test <id> | net arp ... | "
                "net ipv4 ... | net udp ... | net dhcp ... | "
                "net dns ... | net tcp ... | net socket ... | "
                "net check ...\n", 0x0C);
}

static void cmd_power(const char* args) {
    power_status_t status;

    if (!shell_args_equal(args, "status")) {
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
        print_num(state);
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
    cmd_print_hex(address, 8U);
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

    if (high) cmd_print_hex(high, 8U);
    cmd_print_hex((uint32_t)address, 8U);
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
    print_num(reg->register_bit_width);
    video_print(" offset=", 0x07);
    print_num(reg->register_bit_offset);
    video_print(" acesso=", 0x07);
    print_num(reg->access_size);
    if (readable) {
        video_print(" valor=0x", 0x07);
        cmd_print_hex(value, 4U);
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
        cmd_print_hex(info->smi_command_port, 8U);
        video_print(" enable=0x", 0x07);
        cmd_print_hex(info->acpi_enable_value, 2U);
        video_print(" disable=0x", 0x07);
        cmd_print_hex(info->acpi_disable_value, 2U);
    } else {
        video_print("INDISPONIVEL", 0x0E);
    }
    video_print(" PM1_LEN=", 0x07);
    print_num(info->pm1_control_length);
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
        print_num(info->s5_type_a);
        video_print(" tipo_b=", 0x07);
        print_num(info->s5_type_b);
    }
    video_print(" candidatos=", 0x07);
    print_num(info->s5_candidates);
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

    if (!shell_args_equal(args, "status")) {
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
        print_num(status.revision);
        video_print("\n  RSDP: 0x", 0x07);
        cmd_print_hex(status.rsdp_address, 8U);
        video_print("\n  Raiz: ", 0x07);
        video_print(acpi_root_kind_name(status.root_kind), 0x0B);
        video_print(" em 0x", 0x07);
        cmd_print_hex(status.root_address, 8U);
        video_print("\n  Entradas raiz=", 0x07);
        print_num(status.root_entry_count);
        video_print(" tabelas copiadas=", 0x07);
        print_num(status.table_count);
        video_print("\n", 0x07);
        cmd_acpi_print_table("FADT", status.fadt_present,
                             status.fadt_address);
        cmd_acpi_print_table("DSDT", status.dsdt_present,
                             status.dsdt_address);
        cmd_acpi_print_table("FACS", status.facs_present,
                             status.facs_address);
    }
    video_print("  Invalidas=", 0x07);
    print_num(status.malformed_tables);
    video_print(" ignoradas=", 0x07);
    print_num(status.skipped_tables);
    video_print(" ticks=", 0x07);
    print_num(status.scan_ticks);
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
    print_num(shell_kmetrics_delta(current->scheduler.context_switches,
                                   baseline->scheduler.context_switches));
    video_print(" READY=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_READY));
    video_print(" RUNNING=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_RUNNING));
    video_print(" BLOCKED=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_BLOCKED));
    video_print(" ZOMBIE=", 0x08);
    print_num(process_get_state_count(PROCESS_STATE_ZOMBIE));
    video_print("\n", 0x07);
    video_print("             cooperativos=", 0x08);
    print_num(shell_kmetrics_delta(current->scheduler.cooperative_yields,
                                   baseline->scheduler.cooperative_yields));
    video_print(" preempcoes_user=", 0x08);
    print_num(shell_kmetrics_delta(current->scheduler.user_preemptions,
                                   baseline->scheduler.user_preemptions));
    video_print(" idle_fallbacks=", 0x08);
    print_num(shell_kmetrics_delta(current->scheduler.idle_fallbacks,
                                   baseline->scheduler.idle_fallbacks));
    video_print(" quantum_user=", 0x08);
    print_num(current->scheduler.user_quantum_ticks);
    video_print(" tick\n", 0x07);
}

static void cmd_kmetrics_print_queues(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline) {
    video_print("  Filas: teclado=", 0x07);
    print_num(current->keyboard.queued);
    video_print("/", 0x08);
    print_num(current->keyboard.capacity);
    video_print(" descartes=", 0x08);
    print_num(shell_kmetrics_delta(current->keyboard.dropped,
                                   baseline->keyboard.dropped));
    video_print(" processados=", 0x08);
    print_num(shell_kmetrics_delta(current->keyboard.processed,
                                   baseline->keyboard.processed));
    video_print(" pico=", 0x08);
    print_num(current->keyboard.peak_queued);
    video_print("\n", 0x07);
    video_print("         IPC pendentes=", 0x07);
    print_num(ipc_get_pending_count());
    video_print(" capacidade_por_processo=", 0x08);
    print_num(IPC_MSG_QUEUE_SIZE - 1U);
    video_print(" enviados=", 0x08);
    print_num(shell_kmetrics_delta(current->ipc.sent, baseline->ipc.sent));
    video_print(" recebidos=", 0x08);
    print_num(shell_kmetrics_delta(current->ipc.received, baseline->ipc.received));
    video_print(" falhas=", 0x08);
    print_num(shell_kmetrics_delta(current->ipc.failed, baseline->ipc.failed));
    video_print(" cheias=", 0x08);
    print_num(shell_kmetrics_delta(current->ipc.queue_full,
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
    print_num(memory_get_used() / 1024U);
    video_print(" KB livre=", 0x08);
    print_num(memory_get_free() / 1024U);
    video_print(" KB paginas_livres=", 0x08);
    print_num(memory_get_free_pages());
    video_print(" proprias=", 0x08);
    print_num(pmm->owned_pages);
    video_print(" falhas=", 0x08);
    print_num(shell_kmetrics_delta(pmm->allocation_failures,
                                   baseline->pmm.allocation_failures));
    video_print(" rejeicoes=", 0x08);
    print_num(shell_kmetrics_delta(pmm->invalid_frees,
                                   baseline->pmm.invalid_frees));
    video_print("\n", 0x07);
    video_print("  Paging boot: paginas=", 0x07);
    print_num(paging_boot->identity_pages);
    video_print(" tabelas=", 0x08);
    print_num(paging_boot->page_tables_created);
    video_print(" ticks=", 0x08);
    print_num(paging_boot->init_ticks);
    video_print(" modo=blocos\n", 0x08);
    video_print("  Heap: ", 0x07);
    if (!heap->initialized || !heap->valid) {
        video_print("N/D\n", 0x08);
    } else {
        video_print("usado=", 0x07);
        print_num(heap->used_bytes / 1024U);
        video_print(" KB livre=", 0x08);
        print_num(heap->free_bytes / 1024U);
        video_print(" KB total=", 0x08);
        print_num(heap->total_bytes / 1024U);
        video_print(" KB blocos=", 0x08);
        print_num(heap->allocated_blocks);
        video_print("/", 0x08);
        print_num(heap->free_blocks);
        video_print(" maior_livre=", 0x08);
        print_num(heap->largest_free_block / 1024U);
        video_print(" KB frag=", 0x08);
        print_num(heap->fragmentation_percent);
        video_print("% falhas=", 0x08);
        print_num(shell_kmetrics_delta(heap->allocation_failures,
                                       baseline->heap.allocation_failures));
        video_print(" invalidos=", 0x08);
        print_num(shell_kmetrics_delta(heap->invalid_frees,
                                       baseline->heap.invalid_frees));
        video_print(" duplicados=", 0x08);
        print_num(shell_kmetrics_delta(heap->double_frees,
                                       baseline->heap.double_frees));
        video_print("\n", 0x07);
    }
    video_print("  Paging user: dirs=", 0x07);
    print_num(paging_user->active_directories);
    video_print(" paginas=", 0x08);
    print_num(paging_user->active_pages);
    video_print(" criados=", 0x08);
    print_num(shell_kmetrics_delta(paging_user->directories_created,
                                   baseline->paging_user.directories_created));
    video_print(" liberados=", 0x08);
    print_num(shell_kmetrics_delta(paging_user->directories_released,
                                   baseline->paging_user.directories_released));
    video_print(" rejeicoes=", 0x08);
    print_num(shell_kmetrics_delta(paging_user->rejected_releases,
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
    print_num(presentations);
    video_print(" completas=", 0x08);
    print_num(shell_kmetrics_delta(current->vesa.full_presentations,
                                   baseline->vesa.full_presentations));
    video_print(" parciais=", 0x08);
    print_num(shell_kmetrics_delta(current->vesa.partial_presentations,
                                   baseline->vesa.partial_presentations));
    video_print(" bytes=", 0x08);
    print_num(copied_bytes);
    video_print(" media_bytes=", 0x08);
    if (presentations == 0) {
        video_print("N/D", 0x08);
    } else {
        print_num(copied_bytes / presentations);
    }
    video_print("\n    ultima=", 0x08);
    if (presentations == 0) {
        video_print("N/D", 0x08);
    } else {
        print_num(current->vesa.last_copy_bytes);
        video_print(" bytes/", 0x08);
        print_num(current->vesa.last_copy_ticks);
        video_print(" ticks", 0x08);
    }
    video_print(" max_boot=", 0x08);
    print_num(current->vesa.max_copy_ticks);
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
    print_num(shell_kmetrics_delta(current.ticks, baseline->ticks));
    video_print(" frequencia=", 0x08);
    print_num(timer_get_frequency());
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

static int shell_run_memcheck(shell_memcheck_result_t* result_out) {
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
    run_result = shell_run_memcheck(&result);

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

static void cmd_appcheck_print_result(const char* label, int result) {
    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(" retorno=", 0x08);
    print_num((uint32_t)result);
    video_print(result == OK ? " OK\n" : " ERRO\n", result == OK ? 0x0A : 0x0C);
}

static void cmd_appcheck_print_expected_result(const char* label, int actual,
                                               int expected) {
    int passed = actual == expected;

    video_print("  ", 0x07);
    video_print(label, 0x07);
    video_print(" retorno=", 0x08);
    print_num((uint32_t)actual);
    video_print(" esperado=", 0x08);
    print_num((uint32_t)expected);
    video_print(passed ? " OK\n" : " ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_appcheck_files(void) {
    char file_name[FS_MAX_FILENAME];
    uint8_t buffer[64];
    uint32_t file_size = 0;
    uint32_t bytes_read = 0;
    uint32_t second_read = 0;
    uint32_t written = 0;
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
            if (result == OK) {
                video_print("    bytes=", 0x08);
                print_num(bytes_read);
                video_print("\n", 0x07);
            }

            result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                            handle, (uint32_t)buffer,
                                            sizeof(buffer),
                                            (uint32_t)&second_read, 0);
            cmd_appcheck_print_result("file_read_sequencial", result);
            result = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE,
                                            handle, 0, 0, 0, 0);
            cmd_appcheck_print_result("file_close", result);
            result = syscall_invoke_kernel(APP_SYSCALL_FILE_CLOSE,
                                            handle, 0, 0, 0, 0);
            cmd_appcheck_print_result("file_close_expirado", result);
        }
    } else {
        cmd_appcheck_print_result("file_open_sem_arquivo", ERR_NOT_FOUND);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_OPEN,
                                    (uint32_t)"ZZZZZZZ9.NOF",
                                    APP_FILE_MODE_READ,
                                    (uint32_t)&handle, 0, 0);
    cmd_appcheck_print_result("file_open_inexistente", result);

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                    APP_HANDLE_INVALID, (uint32_t)buffer,
                                    APP_API_MAX_FILE_IO_SIZE + 1,
                                    (uint32_t)&bytes_read, 0);
    cmd_appcheck_print_result("file_read_grande", result);

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_READ,
                                    APP_HANDLE_INVALID, 0, 1,
                                    (uint32_t)&bytes_read, 0);
    cmd_appcheck_print_result("file_read_handle_invalido", result);

    result = syscall_invoke_kernel(APP_SYSCALL_FILE_WRITE,
                                    APP_HANDLE_INVALID, 0, 1,
                                    (uint32_t)&written, 0);
    cmd_appcheck_print_result("file_write_nulo", result);
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
    cmd_appcheck_print_result("message_pid_invalido", result);

    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_SEND,
                                    current_pid, 0, 0, 0, 0);
    cmd_appcheck_print_result("message_nula", result);

    message.type = 0;
    result = syscall_invoke_kernel(APP_SYSCALL_MESSAGE_SEND,
                                    current_pid, (uint32_t)&message,
                                    0, 0, 0);
    cmd_appcheck_print_result("message_tipo_invalido", result);
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
    cmd_appcheck_print_result("loader_argumentos_excesso", result);

    result = app_loader_build_launch_info(appcheck_oversized_args,
                                          &appcheck_launch_info);
    cmd_appcheck_print_result("loader_argumentos_grandes", result);
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
    cmd_appcheck_print_result("loader_entry_invalida", result);

    shell_build_demo_image();
    kmemcpy(&header, appcheck_demo_image, APP_IMAGE_HEADER_SIZE);
    header.flags = 1U;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    result = app_loader_validate_image(appcheck_demo_image, image_size,
                                       &header);
    cmd_appcheck_print_result("loader_flags_invalidas", result);

    shell_build_demo_image();
    kmemcpy(&header, appcheck_demo_image, APP_IMAGE_HEADER_SIZE);
    header.code_size = APP_IMAGE_MAX_CODE_SIZE + 1U;
    kmemcpy(appcheck_demo_image, &header, APP_IMAGE_HEADER_SIZE);
    result = app_loader_validate_image(appcheck_demo_image, image_size,
                                       &header);
    cmd_appcheck_print_result("loader_codigo_grande", result);

    shell_build_demo_image();
    result = app_loader_validate_image(appcheck_demo_image,
                                       APP_IMAGE_HEADER_SIZE - 1U,
                                       &header);
    cmd_appcheck_print_result("loader_cabecalho_curto", result);

    if (!app_loader_is_ready()) {
        cmd_appcheck_print_result("loader_indisponivel", ERR_UNAVAILABLE);
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
                video_print("    pid=", 0x08);
                print_num(pid);
                video_print(" assincrono\n", 0x07);
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
    video_print("Teste da API de aplicativos:\n", 0x0B);

    result = app_api_get_version(&version);
    cmd_appcheck_print_result("get_version", result);
    if (result == OK) {
        video_print("    versao=", 0x08);
        print_num(version.major);
        video_print(".", 0x08);
        print_num(version.minor);
        video_print("\n", 0x07);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_CONSOLE_WRITE,
                                    (uint32_t)message,
                                    kstrlen(message), 0, 0, 0);
    cmd_appcheck_print_result("console_write", result);

    result = syscall_invoke_kernel(APP_SYSCALL_UPTIME,
                                    (uint32_t)&uptime, 0, 0, 0, 0);
    cmd_appcheck_print_result("get_uptime", result);
    if (result == OK) {
        video_print("    ticks=", 0x08);
        print_num(uptime.ticks);
        video_print(" segundos=", 0x08);
        print_num(uptime.seconds);
        video_print("\n", 0x07);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_MEMORY_INFO,
                                    (uint32_t)&memory, 0, 0, 0, 0);
    cmd_appcheck_print_result("get_memory_info", result);
    if (result == OK) {
        video_print("    total_kb=", 0x08);
        print_num(memory.total_bytes / 1024);
        video_print(" livre_kb=", 0x08);
        print_num(memory.free_bytes / 1024);
        video_print(" paginas_livres=", 0x08);
        print_num(memory.free_pages);
        video_print("\n", 0x07);
    }

    result = syscall_invoke_kernel(APP_SYSCALL_INVALID, 0, 0, 0, 0, 0);
    cmd_appcheck_print_result("numero invalido", result);
    result = syscall_invoke_kernel(APP_SYSCALL_UPTIME, 0, 0, 0, 0, 0);
    cmd_appcheck_print_result("uptime nulo", result);
    result = syscall_invoke_kernel(APP_SYSCALL_MEMORY_INFO, 0, 0, 0, 0, 0);
    cmd_appcheck_print_expected_result("memory_info nulo", result, ERR_NULL);
    result = syscall_invoke_kernel(APP_SYSCALL_CONSOLE_WRITE,
                                    (uint32_t)"", 0, 0, 0, 0);
    cmd_appcheck_print_result("console_write vazio", result);
    result = syscall_invoke_kernel(APP_SYSCALL_CONSOLE_WRITE,
                                    (uint32_t)appcheck_oversized_text,
                                    sizeof(appcheck_oversized_text), 0, 0, 0);
    cmd_appcheck_print_result("console_write grande", result);
    result = syscall_invoke_kernel(APP_SYSCALL_PROCESS_EXIT,
                                    0, 0, 0, 0, 0);
    cmd_appcheck_print_result("process_exit", result);
    cmd_appcheck_files();
    cmd_appcheck_ipc();
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
    shell_memcheck_result_t memory;
    int full_mode = shell_args_equal(args, "full");
    int result;

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

    if (shell_regcheck.full_mode) shell_regcheck_run_full_checks();
    shell_regcheck.health_result = shell_regcheck_validate_health();
    shell_regcheck.services_result = shell_regcheck_validate_services();
    shell_regcheck.scheduler_result = shell_regcheck_validate_scheduler();
    kmemset(&memory, 0, sizeof(memory));
    shell_regcheck.memory_result = shell_run_memcheck(&memory);
    shell_regcheck.package_result = shell_regcheck_validate_packages();
    shell_regcheck.thread_result = thread_run_self_test();
    shell_regcheck.processes_result = shell_regcheck_validate_processes();
    shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();

    if (shell_regcheck_has_failures()) {
        shell_regcheck_finish();
        return;
    }

    shell_regcheck.loader_started = 1;
    result = shell_regcheck_start_image(SHELL_REGCHECK_WAIT_DEMO);
    if (result == OK) return;

    shell_regcheck.loader_result = result;
    shell_regcheck.processes_result = shell_regcheck_validate_processes();
    shell_regcheck.cleanup_result = shell_regcheck_validate_cleanup();
    shell_regcheck_finish();
}

static void cmd_app_inputtest(void) {
    uint32_t image_size;
    uint32_t pid = 0;
    int result;

    if (!app_loader_is_ready()) {
        video_print("Erro: carregador de aplicativos indisponivel.\n", 0x0C);
        return;
    }

    image_size = shell_build_input_test_image();
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
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    video_print("Teste de entrada iniciado, PID ", 0x0A);
    print_num(pid);
    video_print(". Aguarde o foco do aplicativo.\n", 0x0A);
}

static void cmd_app_argtest(const char* text) {
    app_launch_info_t launch;
    uint32_t pid = 0;
    int result;

    if (!text || !*text) {
        video_print("Uso: app argtest <texto>\n", 0x0E);
        return;
    }

    result = app_loader_build_launch_info(text, &launch);
    if (result != OK) {
        video_print("Erro: argumentos invalidos (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    result = app_builtin_run_argtest(&launch, &pid);
    if (result != OK) {
        video_print("Erro: teste de argumentos indisponivel (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    video_print("Teste de argumentos iniciado, PID ", 0x0A);
    print_num(pid);
    video_print(".\n", 0x0A);
}

static void cmd_app_outputtest(const char* args) {
    uint32_t exit_code = APP_EXIT_SUCCESS;
    uint32_t pid = 0;
    int result;

    if (args && *args) {
        if (kstrcmp(args, "fail") != 0) {
            LOG_ERROR("SHELL", "Argumento invalido no teste de saida ZAPP");
            video_print("Uso: app outputtest [fail]\n", 0x0E);
            return;
        }
        exit_code = SHELL_APP_OUTPUTTEST_FAILURE_CODE;
    }

    result = app_builtin_run_outputtest(exit_code, &pid);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao iniciar teste de saida ZAPP");
        video_print("Erro: teste de saida indisponivel (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    video_print("Teste de saida iniciado, PID ", 0x0A);
    print_num(pid);
    video_print(".\n", 0x0A);
}

static void cmd_pkg_take_token(const char** cursor, char* output,
                               uint32_t output_size) {
    uint32_t length = 0;

    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
    while (**cursor && **cursor != ' ' && **cursor != '\t') {
        if (length + 1U < output_size) output[length++] = **cursor;
        (*cursor)++;
    }
    output[length] = '\0';
}

static void cmd_update_print_version(const char* label,
                                     const update_version_t* version,
                                     uint32_t epoch) {
    video_print(label, 0x07);
    print_num(version->major);
    video_print(".", 0x07);
    print_num(version->minor);
    video_print(".", 0x07);
    print_num(version->patch);
    video_print(" epoch=", 0x08);
    print_num(epoch);
    video_print("\n", 0x07);
}

static void cmd_update_verify(const char* path) {
    update_verification_t verification;
    int result = update_verify_file(path, &verification);

    if (result != OK) {
        video_print("Atualizacao recusada: ", 0x0C);
        if (verification.reason == ZUPD_REASON_NONE) {
            video_print("IO", 0x0C);
        } else {
            video_print(zupd_reason_name(verification.reason), 0x0C);
        }
        video_print(" (motivo=", 0x08);
        print_num((uint32_t)verification.reason);
        video_print(", erro=", 0x08);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        if (verification.entry_count > 0U) {
            cmd_update_print_version(
                "  Base autenticada: ", &verification.base_version,
                verification.base_epoch);
            cmd_update_print_version(
                "  Alvo autenticado: ", &verification.target_version,
                verification.target_epoch);
            video_print("  Arquivos autenticados: ", 0x07);
            print_num(verification.entry_count);
            video_print("\n", 0x07);
        }
        video_print("Nenhuma gravacao foi realizada.\n", 0x0C);
        return;
    }
    video_print("ZUPD autenticado e compativel.\n", 0x0A);
    cmd_update_print_version("  Base: ", &verification.base_version,
                             verification.base_epoch);
    cmd_update_print_version("  Alvo: ", &verification.target_version,
                             verification.target_epoch);
    video_print("  Arquivos: ", 0x07);
    print_num(verification.entry_count);
    video_print("  bytes=", 0x08);
    print_num(verification.total_size);
    video_print("\n  Motivo: NONE\n", 0x07);
    video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
}

static int cmd_update_cancel_check(void* context) {
    ipc_msg_t message;

    (void)context;
    shell_present_hosted_progress();
    keyboard_process_events();
    while (ipc_receive(&message)) {
        if (message.type == IPC_MSG_KEYBOARD &&
            (message.data1 == SHELL_UPDATE_SCANCODE_ESCAPE ||
             message.data1 == SHELL_UPDATE_SCANCODE_F12)) {
            return 1;
        }
    }
    return 0;
}

static void cmd_update_refresh_component(void) {
    update_capabilities_t capabilities;

    if (update_get_capabilities(&capabilities) != OK) return;
    if (capabilities.recovery_pending ||
        (fs_get_type() == FS_TYPE_FAT12 &&
         !capabilities.persistent_state_ready)) {
        recovery_mark_degraded(
            RECOVERY_COMPONENT_UPDATE, ERR_STATE,
            capabilities.recovery_pending ?
            "Recuperacao de Update pendente" :
            "Estado persistente de Update invalido");
    } else {
        recovery_mark_ready(RECOVERY_COMPONENT_UPDATE);
    }
}

static void cmd_update_print_action(const update_action_result_t* action) {
    video_print("  Resultado: ", 0x07);
    video_print(update_action_reason_name(action->reason),
                action->reason == UPDATE_ACTION_NONE ? 0x0A : 0x0C);
    video_print("\n", 0x07);
    if (action->entry_count == 0U) return;
    cmd_update_print_version(
        "  Origem: ", &action->from_version, action->from_epoch);
    cmd_update_print_version(
        "  Destino: ", &action->to_version, action->to_epoch);
    video_print("  Arquivos: ", 0x07);
    print_num(action->entry_count);
    video_print(" processados=", 0x08);
    print_num(action->completed_entries);
    video_print("\n", 0x07);
}

static void cmd_update_print_version_inline(
    const update_version_t* version, uint32_t epoch) {
    print_num(version->major);
    video_print(".", 0x07);
    print_num(version->minor);
    video_print(".", 0x07);
    print_num(version->patch);
    video_print("/e", 0x08);
    print_num(epoch);
}

static void cmd_update_print_history_entry(
    const update_history_entry_t* entry) {
    uint8_t result_color;

    if (!entry) return;
    result_color =
        (entry->outcome == UPDATE_HISTORY_OUTCOME_SUCCESS ||
         entry->outcome == UPDATE_HISTORY_OUTCOME_RECOVERED) ?
        0x0A : (entry->outcome == UPDATE_HISTORY_OUTCOME_CANCELLED ?
                0x0E : 0x0C);
    video_print("  #", 0x07);
    print_num(entry->sequence);
    video_print(" ", 0x07);
    video_print(update_history_operation_name(entry->operation), 0x0B);
    video_print(" ", 0x07);
    video_print(update_history_outcome_name(entry->outcome), result_color);
    video_print("\n    ", 0x07);
    cmd_update_print_version_inline(
        &entry->from_version, entry->from_epoch);
    video_print(" -> ", 0x08);
    cmd_update_print_version_inline(&entry->to_version, entry->to_epoch);
    video_print(" arquivos=", 0x08);
    print_num(entry->completed_entries);
    video_print("/", 0x08);
    print_num(entry->entry_count);
    video_print("\n    motivo=", 0x08);
    video_print(update_action_reason_name(entry->action_reason), 0x07);
    if (entry->verification_reason != ZUPD_REASON_NONE) {
        video_print(" verificacao=", 0x08);
        video_print(zupd_reason_name(entry->verification_reason), 0x0C);
    }
    if (entry->package_alias[0]) {
        video_print(" pacote=", 0x08);
        video_print(entry->package_alias, 0x07);
    }
    if (entry->reboot_required) video_print(" reboot=SIM", 0x0E);
    video_print("\n", 0x07);
}

static void cmd_update_print_capability(const char* label, int ready) {
    video_print(label, 0x08);
    video_print(ready ? "READY" : "DISABLED",
                ready ? 0x0A : 0x08);
}

static void cmd_update_status(void) {
    update_status_t status;
    update_remote_status_t remote;
    uint32_t history_count = 0;

    if (update_get_status(&status) != OK) {
        video_print("Erro: status de Update indisponivel.\n", 0x0C);
        return;
    }
    video_print("Status do Update:\n", 0x0B);
    cmd_update_print_version(
        "  Build: ", &status.build_version, status.build_epoch);
    cmd_update_print_version(
        "  Instalado: ", &status.installed_version,
        status.installed_epoch);
    video_print("  Estado: ", 0x07);
    video_print(status.state_store == UPDATE_STORE_EMPTY ?
                "BASELINE" : update_store_state_name(status.state_store),
                status.state_store == UPDATE_STORE_INVALID ? 0x0C : 0x0A);
    video_print(" arquivos=", 0x08);
    video_print(update_store_state_name(status.current_files),
                status.current_files == UPDATE_STORE_INVALID ? 0x0C : 0x0A);
    video_print("  journal=", 0x08);
    video_print(status.transaction_pending ? "PENDING\n" : "CLEAN\n",
                status.transaction_pending ? 0x0E : 0x0A);
    video_print("  Historico: ", 0x07);
    video_print(update_store_state_name(status.history_store),
                status.history_store == UPDATE_STORE_INVALID ? 0x0C : 0x0A);
    if (update_get_history_count(&history_count) == OK) {
        video_print(" eventos=", 0x08);
        print_num(history_count);
    }
    video_print("\n  Rollback: ", 0x07);
    if (status.capabilities.rollback_available) {
        video_print("READY para ", 0x0A);
        cmd_update_print_version_inline(
            &status.rollback_version, status.rollback_epoch);
        video_print("\n", 0x07);
    } else {
        video_print("DISABLED\n", 0x08);
    }
    video_print("  Capacidades: ", 0x07);
    cmd_update_print_capability("local=", status.capabilities.verifier_ready &&
                                status.capabilities.local_file_available);
    video_print(" ", 0x07);
    cmd_update_print_capability(
        "apply=", status.capabilities.apply_available);
    video_print(" ", 0x07);
    cmd_update_print_capability(
        "historico=", status.capabilities.history_available);
    video_print(" remoto=", 0x07);
    if (update_remote_get_status(&remote) != OK || !remote.enabled) {
        video_print("DISABLED\n", 0x08);
    } else if (!remote.network_ready ||
               remote.state == UPDATE_REMOTE_STATE_FAILED ||
               remote.cache_store == UPDATE_REMOTE_STORE_INVALID) {
        video_print("DEGRADED\n", 0x0E);
    } else {
        video_print("READY\n", 0x0A);
    }
    if (status.has_last_event) {
        video_print("  Ultima operacao:\n", 0x07);
        cmd_update_print_history_entry(&status.last_event);
    }
}

static void cmd_update_history(void) {
    uint32_t count = 0;

    if (update_get_history_count(&count) != OK) {
        video_print("Historico de Update indisponivel ou corrompido.\n",
                    0x0C);
        return;
    }
    video_print("Historico do Update:\n", 0x0B);
    if (count == 0U) {
        video_print("  Nenhuma operacao registrada.\n", 0x08);
        return;
    }
    for (uint32_t index = 0; index < count; index++) {
        update_history_entry_t entry;

        if (update_get_history_entry(index, &entry) != OK) {
            video_print("  Erro ao ler entrada do historico.\n", 0x0C);
            return;
        }
        cmd_update_print_history_entry(&entry);
    }
}

static void cmd_update_apply(const char* path, int confirmed) {
    update_action_options_t options;
    update_action_result_t action;
    int result;

    kmemset(&options, 0, sizeof(options));
    options.dry_run = confirmed ? 0U : 1U;
    options.cancel_check = cmd_update_cancel_check;
    video_print(confirmed ?
                "Aplicando ZUPD; Esc/F12 cancela entre arquivos...\n" :
                "Executando verificacao e preflight sem gravar...\n",
                confirmed ? 0x0E : 0x07);
    result = update_apply_file(path, &options, &action);
    cmd_update_print_action(&action);
    cmd_update_refresh_component();
    if (result != OK) {
        if (action.verification_reason != ZUPD_REASON_NONE) {
            video_print("  Verificacao: ", 0x07);
            video_print(zupd_reason_name(action.verification_reason), 0x0C);
            video_print("\n", 0x07);
        }
        if (action.recovery_pending) {
            video_print("Reinicie para executar a recuperacao automatica.\n",
                        0x0E);
        }
        return;
    }
    if (!confirmed) {
        video_print("Preflight aprovado. Nenhuma gravacao foi realizada.\n",
                    0x0A);
        video_print("Para confirmar: update apply ", 0x0E);
        video_print(path, 0x0E);
        video_print(" --confirm\n", 0x0E);
        return;
    }
    video_print("Atualizacao aplicada. Rollback preservado.\n", 0x0A);
    video_print("Reinicie para recarregar os arquivos visuais.\n", 0x0E);
}

static void cmd_update_rollback(int confirmed) {
    update_action_options_t options;
    update_action_result_t action;
    int result;

    kmemset(&options, 0, sizeof(options));
    options.dry_run = confirmed ? 0U : 1U;
    options.cancel_check = cmd_update_cancel_check;
    result = update_rollback(&options, &action);
    cmd_update_print_action(&action);
    cmd_update_refresh_component();
    if (result != OK) {
        if (action.recovery_pending) {
            video_print("Reinicie para continuar o rollback.\n", 0x0E);
        }
        return;
    }
    if (!confirmed) {
        video_print("Rollback validado. Nenhuma gravacao foi realizada.\n",
                    0x0A);
        video_print("Para confirmar: update rollback --confirm\n", 0x0E);
        return;
    }
    video_print("Rollback concluido. Reinicie para recarregar os BMPs.\n",
                0x0A);
}

static void cmd_update_test_fail_after(const char* value) {
    uint32_t entries = parse_number(value);
    int result;

    if (entries == 0U || entries > 3U) {
        LOG_ERROR("SHELL", "Failpoint U3 fora do limite");
        video_print("Uso: update test fail-after <1-3>\n", 0x0E);
        return;
    }
    result = update_test_fail_after((uint16_t)entries);
    if (result != OK) {
        video_print("Erro: failpoint U3 indisponivel.\n", 0x0C);
        return;
    }
    video_print("TEST ONLY: a proxima aplicacao sera interrompida apos ",
                0x0E);
    print_num(entries);
    video_print(" arquivo(s).\n", 0x0E);
}

static void cmd_update_print_remote_candidate(
    const update_remote_candidate_t* candidate) {
    if (!candidate || !candidate->package_size) return;
    cmd_update_print_version(
        "  Base: ", &candidate->base_version, candidate->base_epoch);
    cmd_update_print_version(
        "  Alvo: ", &candidate->target_version, candidate->target_epoch);
    video_print("  Geracao: ", 0x07);
    print_num(candidate->generation);
    video_print("  bytes=", 0x08);
    print_num(candidate->package_size);
    video_print("\n  Caminho: ", 0x07);
    video_print(candidate->package_path, 0x07);
    video_print("\n", 0x07);
}

static void cmd_update_remote_status(void) {
    update_remote_status_t* status =
        &shell_update_workspace.remote_status;

    if (update_remote_get_status(status) != OK) {
        video_print("Update remoto indisponivel.\n", 0x0C);
        return;
    }
    video_print("Update remoto:\n  Estado: ", 0x0B);
    video_print(update_remote_state_name(status->state),
                status->state == UPDATE_REMOTE_STATE_FAILED ? 0x0C :
                status->state == UPDATE_REMOTE_STATE_DISABLED ? 0x08 : 0x0A);
    video_print("  motivo=", 0x08);
    video_print(update_remote_reason_name(status->reason), 0x07);
    video_print("\n  Sessao: ", 0x07);
    video_print(status->enabled ? "HABILITADA" : "DESABILITADA",
                status->enabled ? 0x0A : 0x08);
    video_print("  rede=", 0x08);
    video_print(status->network_ready ? "READY" : "UNAVAILABLE",
                status->network_ready ? 0x0A : 0x0E);
    video_print("\n  Canal: ", 0x07);
    video_print(UPDATE_REMOTE_CHANNEL_NAME, 0x0B);
    video_print("\n  URL: ", 0x07);
    video_print(status->manifest_url, 0x07);
    video_print("\n  Cache: ", 0x07);
    video_print(update_remote_store_name(status->cache_store),
                status->cache_store == UPDATE_REMOTE_STORE_INVALID ?
                0x0C : 0x0A);
    if (status->package_cached) {
        video_print(" alias=", 0x08);
        video_print(status->cached_alias, 0x0B);
    }
    video_print("\n  Progresso: ", 0x07);
    print_num(status->bytes_received);
    video_print("/", 0x08);
    print_num(status->total_bytes);
    video_print(" retry=", 0x08);
    print_num(status->retry_count);
    video_print("\n", 0x07);
    if (status->manifest_cached) {
        cmd_update_print_remote_candidate(&status->candidate);
    }
}

static void cmd_update_remote_control(const char* action, int confirmed) {
    update_remote_options_t* options =
        &shell_update_workspace.remote_options;
    update_remote_result_t* result =
        &shell_update_workspace.remote_result;
    int operation_result;

    if (kstrcmp(action, "status") == 0) {
        cmd_update_remote_status();
        return;
    }
    if (kstrcmp(action, "enable") == 0) {
        operation_result = update_remote_enable();
        video_print(operation_result == OK ?
                    "Update remoto habilitado nesta sessao.\n" :
                    "Falha ao habilitar Update remoto.\n",
                    operation_result == OK ? 0x0A : 0x0C);
        cmd_update_remote_status();
        return;
    }
    if (kstrcmp(action, "disable") == 0) {
        operation_result = update_remote_disable();
        video_print(operation_result == OK ?
                    "Update remoto desabilitado.\n" :
                    "Falha ao desabilitar Update remoto.\n",
                    operation_result == OK ? 0x0A : 0x0C);
        return;
    }
    if (kstrcmp(action, "clear") != 0) {
        video_print("Uso: update remote status|enable|disable\n", 0x0E);
        video_print("     update remote clear [--confirm]\n", 0x0E);
        return;
    }
    kmemset(options, 0, sizeof(*options));
    options->dry_run = confirmed ? 0U : 1U;
    operation_result = update_remote_clear(options, result);
    if (operation_result != OK) {
        video_print("Falha ao limpar cache remoto: ", 0x0C);
        video_print(update_remote_reason_name(result->reason), 0x0C);
        video_print("\n", 0x0C);
    } else if (!confirmed) {
        video_print("Cache remoto inspecionado. Nenhuma gravacao.\n", 0x0A);
        video_print("Para confirmar: update remote clear --confirm\n",
                    0x0E);
    } else {
        video_print("Cache remoto removido.\n", 0x0A);
    }
}

static void cmd_update_fetch(const char* url, int confirmed) {
    update_remote_options_t* options =
        &shell_update_workspace.remote_options;
    update_remote_result_t* result =
        &shell_update_workspace.remote_result;
    int operation_result;

    kmemset(options, 0, sizeof(*options));
    options->dry_run = confirmed ? 0U : 1U;
    options->cancel_check = cmd_update_cancel_check;
    if (confirmed) {
        video_print("Baixando ZUPD; Esc/F12 cancela a transferencia...\n",
                    0x0E);
        operation_result = update_remote_fetch(
            url && url[0] ? url : 0, options, result);
    } else {
        video_print("Consultando manifesto; Esc/F12 cancela sem gravar...\n",
                    0x07);
        operation_result = update_remote_check(
            url && url[0] ? url : 0, options, result);
    }
    video_print("Resultado remoto: ", 0x07);
    video_print(update_remote_reason_name(result->reason),
                operation_result == OK ? 0x0A : 0x0C);
    video_print(" bytes=", 0x08);
    print_num(result->bytes_received);
    video_print(" retry=", 0x08);
    print_num(result->retry_count);
    video_print("\n", 0x07);
    cmd_update_print_remote_candidate(&result->candidate);
    if (operation_result != OK) {
        if (result->cache_preserved) {
            video_print("O cache remoto anterior foi preservado.\n", 0x0A);
        }
        return;
    }
    if (!confirmed) {
        video_print("Manifesto autenticado. Nenhuma gravacao realizada.\n",
                    0x0A);
        video_print("Para confirmar: update fetch", 0x0E);
        if (url && url[0]) {
            video_print(" --url ", 0x0E);
            video_print(url, 0x0E);
        }
        video_print(" --confirm\n", 0x0E);
    } else {
        video_print("Pacote autenticado no cache: ", 0x0A);
        video_print(result->cached_alias, 0x0B);
        video_print("\nNenhuma instalacao foi iniciada.\n", 0x0A);
    }
}

static void cmd_update(const char* args) {
    char* operation = shell_update_workspace.operation;
    char* first = shell_update_workspace.first;
    char* second = shell_update_workspace.second;
    char* third = shell_update_workspace.third;
    char* extra = shell_update_workspace.extra;
    const char* cursor = args;

    cmd_pkg_take_token(
        &cursor, operation, sizeof(shell_update_workspace.operation));
    cmd_pkg_take_token(
        &cursor, first, sizeof(shell_update_workspace.first));
    cmd_pkg_take_token(
        &cursor, second, sizeof(shell_update_workspace.second));
    cmd_pkg_take_token(
        &cursor, third, sizeof(shell_update_workspace.third));
    cmd_pkg_take_token(
        &cursor, extra, sizeof(shell_update_workspace.extra));
    if (kstrcmp(operation, "status") == 0 && first[0] == '\0') {
        cmd_update_status();
        return;
    }
    if (kstrcmp(operation, "history") == 0 && first[0] == '\0') {
        cmd_update_history();
        return;
    }
    if (kstrcmp(operation, "verify") == 0 && first[0] &&
        second[0] == '\0') {
        cmd_update_verify(first);
        return;
    }
    if (kstrcmp(operation, "apply") == 0 && first[0] &&
        extra[0] == '\0' &&
        third[0] == '\0' &&
        (second[0] == '\0' || kstrcmp(second, "--confirm") == 0)) {
        cmd_update_apply(first, second[0] != '\0');
        return;
    }
    if (kstrcmp(operation, "rollback") == 0 &&
        extra[0] == '\0' && second[0] == '\0' && third[0] == '\0' &&
        (first[0] == '\0' || kstrcmp(first, "--confirm") == 0)) {
        cmd_update_rollback(first[0] != '\0');
        return;
    }
    if (kstrcmp(operation, "test") == 0 &&
        kstrcmp(first, "fail-after") == 0 &&
        second[0] && third[0] == '\0' && extra[0] == '\0') {
        cmd_update_test_fail_after(second);
        return;
    }
    if (kstrcmp(operation, "remote") == 0 && first[0] &&
        extra[0] == '\0' && third[0] == '\0' &&
        (second[0] == '\0' ||
         (kstrcmp(first, "clear") == 0 &&
          kstrcmp(second, "--confirm") == 0))) {
        cmd_update_remote_control(first, second[0] != '\0');
        return;
    }
    if (kstrcmp(operation, "fetch") == 0 && extra[0] == '\0') {
        if (!first[0] && !second[0] && !third[0]) {
            cmd_update_fetch(0, 0);
            return;
        }
        if (kstrcmp(first, "--confirm") == 0 &&
            !second[0] && !third[0]) {
            cmd_update_fetch(0, 1);
            return;
        }
        if (kstrcmp(first, "--url") == 0 && second[0] &&
            (!third[0] || kstrcmp(third, "--confirm") == 0)) {
            cmd_update_fetch(second, third[0] != '\0');
            return;
        }
    }
    LOG_WARN("SHELL", "Uso invalido do comando update");
    video_print("Uso: update status|history\n", 0x0E);
    video_print("Uso: update verify <arquivo.ZUP>\n", 0x0E);
    video_print("     update apply <arquivo.ZUP> [--confirm]\n", 0x0E);
    video_print("     update rollback [--confirm]\n", 0x0E);
    video_print("     update remote status|enable|disable\n", 0x0E);
    video_print("     update remote clear [--confirm]\n", 0x0E);
    video_print("     update fetch [--url <manifesto>] [--confirm]\n",
                0x0E);
    video_print("     update test fail-after <1-3>\n", 0x0E);
}

static int cmd_pkg_is_file_name(const char* value) {
    uint32_t length = kstrlen(value);

    if (length < 5U) return 0;
    return value[length - 4U] == '.' &&
           (value[length - 3U] == 'Z' || value[length - 3U] == 'z') &&
           (value[length - 2U] == 'P' || value[length - 2U] == 'p') &&
           (value[length - 1U] == 'K' || value[length - 1U] == 'k');
}

static void cmd_pkg_uppercase_id(char* value) {
    if (!value) return;

    for (uint32_t index = 0; value[index]; index++) {
        if (value[index] >= 'a' && value[index] <= 'z') {
            value[index] -= 'a' - 'A';
        }
    }
}

static void cmd_pkg_print_info(const app_package_info_t* info) {
    if (!info) return;

    video_print("Pacote ", 0x0B);
    video_print(info->id, 0x0A);
    video_print("\n  Nome: ", 0x07);
    video_print(info->name, 0x07);
    video_print("\n  Versao: ", 0x07);
    video_print(info->version, 0x07);
    video_print("\n  API: 0.3\n  Dependencias: ", 0x07);
    if (info->dependency_count == 0) {
        video_print("nenhuma\n", 0x08);
        return;
    }
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        if (index) video_print(", ", 0x07);
        video_print(info->dependencies[index], 0x07);
    }
    video_print("\n", 0x07);
}

static void cmd_pkg_print_usage(void) {
    video_print("Uso: pkg list | pkg info <ID|arquivo.ZPK> | ", 0x0E);
    video_print("pkg verify <arquivo.ZPK>\n", 0x0E);
    video_print("     pkg install <arquivo.ZPK> | pkg remove <ID>\n", 0x0E);
}

static void cmd_pkg_list(void) {
    int count = app_package_get_installed_count();

    video_print("Pacotes instalados:\n", 0x0B);
    if (count == 0) {
        video_print("  (nenhum)\n", 0x08);
        return;
    }
    for (int index = 0; index < count; index++) {
        app_package_info_t info;
        if (app_package_get_installed_info(index, &info) != OK) continue;
        video_print("  ", 0x07);
        video_print(info.id, 0x0A);
        video_print(" ", 0x07);
        video_print(info.version, 0x08);
        video_print(" - ", 0x07);
        video_print(info.name, 0x07);
        video_print("\n", 0x07);
    }
}

static void cmd_pkg_info(char* value) {
    app_package_info_t info;
    int result;

    if (cmd_pkg_is_file_name(value)) {
        result = app_package_verify_file(value, &info);
    } else {
        cmd_pkg_uppercase_id(value);
        result = app_package_get_installed_info_by_id(value, &info);
    }
    if (result != OK) {
        video_print("Erro: pacote nao encontrado ou invalido (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    cmd_pkg_print_info(&info);
}

static void cmd_pkg_verify(const char* value) {
    app_package_info_t info;
    int result = app_package_verify_file(value, &info);

    if (result != OK) {
        video_print("Erro: verificacao de pacote falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Pacote valido.\n", 0x0A);
    cmd_pkg_print_info(&info);
}

static void cmd_pkg_install(const char* value) {
    app_package_info_t info;
    int result = app_package_install_file(value, &info);

    if (result != OK) {
        video_print("Erro: instalacao de pacote falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Pacote instalado: ", 0x0A);
    video_print(info.id, 0x0A);
    video_print(".\n", 0x0A);
}

static void cmd_pkg_remove(char* value) {
    int result;

    cmd_pkg_uppercase_id(value);
    result = app_package_remove(value);

    if (result != OK) {
        video_print("Erro: remocao de pacote falhou (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Pacote removido: ", 0x0A);
    video_print(value, 0x0A);
    video_print(".\n", 0x0A);
}

static void cmd_pkg(const char* args) {
    char operation[16];
    char value[FS_MAX_PATH];
    char extra[2];
    const char* cursor = args;

    if (!args) {
        cmd_pkg_print_usage();
        return;
    }
    cmd_pkg_take_token(&cursor, operation, sizeof(operation));
    cmd_pkg_take_token(&cursor, value, sizeof(value));
    cmd_pkg_take_token(&cursor, extra, sizeof(extra));
    if (kstrcmp(operation, "list") == 0 && value[0] == '\0' &&
        extra[0] == '\0') {
        cmd_pkg_list();
    } else if (kstrcmp(operation, "info") == 0 && value[0] &&
               extra[0] == '\0') {
        cmd_pkg_info(value);
    } else if (kstrcmp(operation, "verify") == 0 && value[0] &&
               extra[0] == '\0') {
        cmd_pkg_verify(value);
    } else if (kstrcmp(operation, "install") == 0 && value[0] &&
               extra[0] == '\0') {
        cmd_pkg_install(value);
    } else if (kstrcmp(operation, "remove") == 0 && value[0] &&
               extra[0] == '\0') {
        cmd_pkg_remove(value);
    } else {
        LOG_WARN("SHELL", "Uso invalido do comando pkg");
        cmd_pkg_print_usage();
    }
}

static void cmd_pkgcheck(void) {
    app_package_diagnostic_t diagnostic;
    app_package_status_t status;
    app_remote_status_t remote;
    int result;
    int passed;

    kmemset(&diagnostic, 0, sizeof(diagnostic));
    result = app_package_run_diagnostics(&diagnostic);
    passed = result == OK && diagnostic.invalid_package &&
             diagnostic.missing_dependency && diagnostic.insufficient_space &&
             diagnostic.mutation_serialization;

    video_print("PkgCheck:\n", 0x0B);
    video_print("  pacote_invalido ", 0x07);
    video_print(diagnostic.invalid_package ? "OK\n" : "ERRO\n",
                diagnostic.invalid_package ? 0x0A : 0x0C);
    video_print("  dependencia_ausente ", 0x07);
    video_print(diagnostic.missing_dependency ? "OK\n" : "ERRO\n",
                diagnostic.missing_dependency ? 0x0A : 0x0C);
    video_print("  espaco_insuficiente ", 0x07);
    video_print(diagnostic.insufficient_space ? "OK\n" : "ERRO\n",
                diagnostic.insufficient_space ? 0x0A : 0x0C);
    video_print("  serializacao_mutacao ", 0x07);
    video_print(diagnostic.mutation_serialization ? "OK\n" : "ERRO\n",
                diagnostic.mutation_serialization ? 0x0A : 0x0C);
    video_print("  transacao_as4 ", 0x07);
    video_print(diagnostic.transaction_supported ? "FAT12 OK\n" : "INDISPONIVEL\n",
                diagnostic.transaction_supported ? 0x0A : 0x0E);
    video_print("  journal_pendente ", 0x07);
    video_print(diagnostic.transaction_pending ? "SIM\n" : "NAO\n",
                diagnostic.transaction_pending ? 0x0E : 0x0A);
    video_print("  rollback_disponivel ", 0x07);
    video_print(diagnostic.rollback_available ? "SIM\n" : "NAO\n",
                diagnostic.rollback_available ? 0x0E : 0x08);
    if (app_package_get_status(&status) == OK && status.rollback_count) {
        video_print("  rollbacks_as4 ", 0x07);
        print_num(status.rollback_count);
        video_print("\n", 0x07);
    }
    if (app_package_get_status(&status) == OK &&
        status.last_history.sequence != 0U) {
        video_print("  ultimo_historico ", 0x07);
        video_print(app_package_history_operation_name(
                        status.last_history.operation), 0x08);
        video_print(" ", 0x08);
        video_print(status.last_history.id, 0x08);
        video_print("\n", 0x07);
    }
    if (app_remote_get_status(&remote) == OK) {
        video_print("  remoto_as5 ", 0x07);
        video_print(app_remote_state_name(remote.state), 0x08);
        video_print(" cache=", 0x08);
        video_print(app_remote_cache_state_name(remote.cache_state),
                    remote.cache_state == APP_REMOTE_CACHE_VALID ?
                    0x0A : 0x08);
        video_print(" geracao=", 0x08);
        print_num(remote.highest_generation);
        video_print(" pendente=", 0x08);
        video_print(remote.cache_pending ? "SIM" : "NAO",
                    remote.cache_pending ? 0x0E : 0x0A);
        video_print(" procedencia=", 0x08);
        video_print(app_remote_is_provenance_available() ? "OK" : "N/D",
                    app_remote_is_provenance_available() ? 0x0A : 0x08);
        video_print("\n", 0x07);
    }
    video_print("  resultado ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
}

static void cmd_store_print_capabilities(uint32_t capabilities) {
    int printed = 0;

    if (capabilities & APP_CATALOG_CAPABILITY_VERIFY) {
        video_print("VERIFY", 0x07);
        printed = 1;
    }
    if (capabilities & APP_CATALOG_CAPABILITY_INSTALL) {
        video_print(printed ? ", INSTALL" : "INSTALL", 0x07);
        printed = 1;
    }
    if (capabilities & APP_CATALOG_CAPABILITY_RUN) {
        video_print(printed ? ", RUN" : "RUN", 0x07);
        printed = 1;
    }
    if (capabilities & APP_CATALOG_CAPABILITY_REMOVE) {
        video_print(printed ? ", REMOVE" : "REMOVE", 0x07);
        printed = 1;
    }
    if (capabilities & APP_CATALOG_CAPABILITY_UPDATE) {
        video_print(printed ? ", UPDATE" : "UPDATE", 0x07);
        printed = 1;
    }
    if (!printed) video_print("nenhuma", 0x08);
}

static void cmd_store_print_dependencies(const app_catalog_entry_t* entry) {
    const app_package_info_t* info;

    info = entry->source.id[0] ? &entry->source : &entry->installed;
    if (!info->id[0]) {
        video_print("N/D", 0x08);
        return;
    }
    if (info->dependency_count == 0) {
        video_print("nenhuma", 0x08);
        return;
    }
    for (uint32_t index = 0; index < info->dependency_count; index++) {
        if (index) video_print(", ", 0x07);
        video_print(info->dependencies[index],
                    (entry->missing_dependency_mask & (1U << index)) ?
                    0x0C : 0x07);
        if (entry->missing_dependency_mask & (1U << index)) {
            video_print("(ausente)", 0x0C);
        }
    }
}

static void cmd_store_print_list_entry(const app_catalog_entry_t* entry) {
    const char* id = entry->source.id[0] ?
        entry->source.id : entry->installed.id;

    video_print("  ", 0x07);
    video_print(entry->alias[0] ? entry->alias : "(instalado)", 0x0B);
    video_print(" id=", 0x08);
    video_print(id[0] ? id : "N/D", 0x07);
    video_print(" fonte=", 0x08);
    video_print(entry->source.version[0] ? entry->source.version : "-", 0x07);
    video_print(" instalada=", 0x08);
    video_print(entry->installed.version[0] ?
                entry->installed.version : "-", 0x07);
    video_print(" ", 0x07);
    video_print(app_catalog_state_name(entry->state),
                entry->state == APP_CATALOG_STATE_INVALID ? 0x0C :
                entry->state == APP_CATALOG_STATE_BLOCKED ? 0x0E : 0x0A);
    if (entry->reason != APP_CATALOG_REASON_NONE) {
        video_print(" motivo=", 0x08);
        video_print(app_catalog_reason_name(entry->reason), 0x0E);
    }
    video_print("\n", 0x07);
}

static void cmd_store_print_usage(void) {
    video_print("Uso: store status | store list | ", 0x0E);
    video_print("store info <ID|alias.ZPK>\n", 0x0E);
    video_print("     store install <alias.ZPK> [--confirm]\n", 0x0E);
    video_print("     store update <ID|alias.ZPK> [--downgrade] [--confirm]\n",
                0x0E);
    video_print("     store rollback <ID> [--confirm]\n", 0x0E);
    video_print("     store history [ID]\n", 0x0E);
    video_print("     store test fail-after <1..32>\n", 0x0E);
    video_print("     store remove <ID> [--confirm]\n", 0x0E);
    video_print("     store run <ID> [args]\n", 0x0E);
    video_print("     store remote status|enable|disable|list\n", 0x0E);
    video_print("     store remote check [--url URL]\n", 0x0E);
    video_print("     store remote info <ID>\n", 0x0E);
    video_print("     store remote fetch <ID> [--url URL] [--confirm]\n",
                0x0E);
    video_print("     store remote install <ID> [--confirm]\n", 0x0E);
    video_print("     store remote update <ID> [--downgrade] [--confirm]\n",
                0x0E);
    video_print("     store remote clear [--confirm]\n", 0x0E);
    video_print("     store remote test fail-after <1..16>\n", 0x0E);
}

static void cmd_store_status(void) {
    const recovery_component_t* component;
    app_catalog_status_t status;
    app_package_status_t transaction;
    int refresh_result = app_catalog_refresh();

    component = recovery_get(RECOVERY_COMPONENT_APP_STORE);
    if (app_catalog_get_status(&status) != OK) {
        video_print("Erro: status da App Store indisponivel.\n", 0x0C);
        return;
    }
    video_print("App Store: ", 0x0B);
    video_print(component ? recovery_state_name(component->state) : "UNKNOWN",
                component ? cmd_health_state_color(component->state) : 0x0C);
    video_print("\n  refresh=", 0x07);
    video_print(refresh_result == OK ? "OK" : "ERRO",
                refresh_result == OK ? 0x0A : 0x0C);
    video_print(" motivo=", 0x08);
    video_print(app_catalog_reason_name(status.reason),
                status.reason == APP_CATALOG_REASON_NONE ? 0x0A : 0x0E);
    video_print("\n  fontes=", 0x07);
    print_num(status.source_count);
    video_print(" validas=", 0x08);
    print_num(status.valid_source_count);
    video_print(" invalidas=", 0x08);
    print_num(status.invalid_source_count);
    video_print(" instaladas=", 0x08);
    print_num(status.installed_count);
    video_print(" entradas=", 0x08);
    print_num(status.entry_count);
    video_print("\n  limite_fontes=", 0x07);
    print_num(APP_CATALOG_MAX_SOURCES);
    video_print(status.source_overflow ? " EXCEDIDO" : " OK",
                status.source_overflow ? 0x0E : 0x0A);
    video_print(" limite_entradas=", 0x08);
    print_num(APP_CATALOG_MAX_ENTRIES);
    video_print(status.entry_overflow ? " EXCEDIDO\n" : " OK\n",
                status.entry_overflow ? 0x0E : 0x0A);
    if (app_package_get_status(&transaction) == OK) {
        video_print("  transacao=", 0x07);
        video_print(transaction.transaction_supported ? "FAT12" : "INDISPONIVEL",
                    transaction.transaction_supported ? 0x0A : 0x0E);
        video_print(" journal=", 0x08);
        video_print(transaction.transaction_pending ? "PENDENTE" : "LIMPO",
                    transaction.transaction_pending ? 0x0E : 0x0A);
        video_print(" rollback=", 0x08);
        video_print(transaction.rollback_available ? transaction.rollback_id :
                    "N/D", transaction.rollback_available ? 0x0A : 0x08);
        if (transaction.rollback_available) {
            video_print(" ", 0x08);
            video_print(transaction.rollback_version, 0x08);
        }
        if (transaction.rollback_count > 1U) {
            video_print(" +", 0x08);
            print_num(transaction.rollback_count - 1U);
            video_print(" app(s)", 0x08);
        }
        video_print("\n", 0x07);
        if (transaction.last_history.sequence != 0U) {
            video_print("  ultimo_historico=", 0x07);
            video_print(app_package_history_operation_name(
                            transaction.last_history.operation), 0x08);
            video_print(" ", 0x08);
            video_print(transaction.last_history.id, 0x08);
            video_print("\n", 0x07);
        }
    }
    if (app_remote_get_status(&shell_store_workspace.remote_status) == OK) {
        video_print("  remoto_as5=", 0x07);
        video_print(app_remote_state_name(
                        shell_store_workspace.remote_status.state), 0x08);
        video_print(" cache=", 0x08);
        video_print(app_remote_cache_state_name(
                        shell_store_workspace.remote_status.cache_state),
                    shell_store_workspace.remote_status.cache_state ==
                    APP_REMOTE_CACHE_VALID ? 0x0A : 0x08);
        video_print(" geracao=", 0x08);
        print_num(shell_store_workspace.remote_status.highest_generation);
        video_print("\n", 0x07);
    }
}

static void cmd_store_list(void) {
    uint32_t count = 0;

    if (app_catalog_refresh() != OK ||
        app_catalog_get_count(&count) != OK) {
        video_print("Erro: catalogo da App Store indisponivel.\n", 0x0C);
        return;
    }
    video_print("Catalogo local (LOCAL / NAO ASSINADO):\n", 0x0B);
    if (count == 0) {
        video_print("  (vazio)\n", 0x08);
        return;
    }
    for (uint32_t index = 0; index < count; index++) {
        app_catalog_entry_t entry;
        if (app_catalog_get_entry(index, &entry) == OK) {
            cmd_store_print_list_entry(&entry);
        }
    }
}

static void cmd_store_info(char* key) {
    app_catalog_entry_t entry;
    app_package_status_t transaction;
    const app_package_info_t* info;

    if (app_catalog_refresh() != OK) {
        video_print("Erro: catalogo da App Store indisponivel.\n", 0x0C);
        return;
    }
    (void)app_remote_refresh_provenance();
    cmd_pkg_uppercase_id(key);
    if (app_catalog_find_entry(key, &entry) != OK) {
        video_print("Erro: entrada da App Store nao encontrada.\n", 0x0C);
        return;
    }
    info = entry.source.id[0] ? &entry.source : &entry.installed;
    video_print("Entrada da App Store:\n  Alias: ", 0x0B);
    video_print(entry.alias[0] ? entry.alias : "N/D", 0x07);
    video_print("\n  ID: ", 0x07);
    video_print(info->id[0] ? info->id : "N/D", 0x07);
    video_print("\n  Nome: ", 0x07);
    video_print(info->name[0] ? info->name : "N/D", 0x07);
    video_print("\n  Versao fonte: ", 0x07);
    video_print(entry.source.version[0] ? entry.source.version : "N/D", 0x07);
    video_print("\n  Versao instalada: ", 0x07);
    video_print(entry.installed.version[0] ?
                entry.installed.version : "N/D", 0x07);
    video_print("\n  Estado: ", 0x07);
    video_print(app_catalog_state_name(entry.state),
                entry.state == APP_CATALOG_STATE_INVALID ? 0x0C :
                entry.state == APP_CATALOG_STATE_BLOCKED ? 0x0E : 0x0A);
    video_print("\n  Motivo: ", 0x07);
    video_print(app_catalog_reason_name(entry.reason),
                entry.reason == APP_CATALOG_REASON_NONE ? 0x0A : 0x0E);
    video_print("\n  Confianca: ", 0x07);
    video_print(entry.installed.id[0] &&
                app_remote_is_provenance_available() &&
                app_remote_get_installed_trust(
                    entry.installed.id, entry.installed.version) ?
                "REMOTO / AUTENTICADO (TESTE)" :
                entry.installed.id[0] &&
                !app_remote_is_provenance_available() ? "N/D" :
                entry.has_source ? "LOCAL / NAO ASSINADO" : "N/D", 0x0E);
    video_print("\n  Tamanho fonte: ", 0x07);
    print_num(entry.source_size);
    video_print("\n  Dependencias: ", 0x07);
    cmd_store_print_dependencies(&entry);
    video_print("\n  Capacidades: ", 0x07);
    cmd_store_print_capabilities(entry.capabilities);
    if (app_package_get_status(&transaction) == OK) {
        const app_package_rollback_entry_t* rollback = 0;

        for (uint32_t index = 0; index < transaction.rollback_count; index++) {
            if (info->id[0] &&
                kstrcmp(transaction.rollbacks[index].id, info->id) == 0) {
                rollback = &transaction.rollbacks[index];
                break;
            }
        }
        video_print("\n  Rollback: ", 0x07);
        if (rollback) {
            video_print("disponivel para ", 0x0A);
            video_print(rollback->version, 0x0A);
        } else {
            video_print("indisponivel", 0x08);
        }
    }
    video_print("\n", 0x07);
}

static void cmd_store_print_plan(const app_package_plan_t* plan) {
    if (!plan || plan->entry_count == 0U) return;
    video_print("  Plano topologico: ", 0x07);
    for (uint32_t index = 0; index < plan->entry_count; index++) {
        const app_package_plan_entry_t* entry = &plan->entries[index];

        if (index) video_print(" -> ", 0x08);
        video_print(entry->id, 0x0B);
        video_print("(", 0x08);
        video_print(app_package_plan_action_name(entry->action), 0x07);
        video_print(" ", 0x08);
        video_print(entry->from_version[0] ? entry->from_version : "novo",
                    0x07);
        video_print("->", 0x08);
        video_print(entry->to_version, 0x07);
        video_print(")", 0x08);
    }
    video_print("\n", 0x07);
}

static void cmd_store_print_action_blockers(
    const app_package_action_result_t* action) {
    if (!action || action->blocker_count == 0U) return;
    video_print("  Bloqueadores: ", 0x07);
    for (uint32_t index = 0; index < action->blocker_count; index++) {
        if (index) video_print(", ", 0x07);
        video_print(action->blocker_ids[index], 0x0E);
    }
    if (action->blocker_overflow) video_print(", ...", 0x0E);
    video_print("\n", 0x07);
}

static void cmd_store_print_action_result(
    const char* label, int operation_result) {
    app_package_action_result_t* action = &shell_store_workspace.action;

    video_print(label, 0x0B);
    video_print(operation_result == OK ? "OK" : "BLOQUEADO",
                operation_result == OK ? 0x0A : 0x0C);
    video_print(" motivo=", 0x08);
    video_print(app_package_action_reason_name(action->reason),
                action->reason == APP_PACKAGE_ACTION_REASON_NONE ?
                    0x0A : 0x0E);
    if (operation_result != OK) {
        video_print(" erro=", 0x08);
        print_num((uint32_t)operation_result);
    }
    video_print("\n", 0x07);
    if (action->info.id[0]) {
        video_print("  Pacote: ", 0x07);
        video_print(action->info.id, 0x0A);
        video_print(" ", 0x07);
        video_print(action->info.version, 0x08);
        video_print(" - ", 0x07);
        video_print(action->info.name, 0x07);
        video_print("\n", 0x07);
    }
    if (action->required_clusters > 0U) {
        video_print("  Clusters: necessarios=", 0x07);
        print_num(action->required_clusters);
        video_print(" livres=", 0x08);
        print_num(action->free_clusters);
        video_print("\n", 0x07);
    }
    cmd_store_print_action_blockers(action);
    cmd_store_print_plan(&action->plan);
}

static int cmd_store_find_action_entry(char* key) {
    if (app_catalog_refresh() != OK) {
        LOG_WARN("SHELL", "Catalogo indisponivel para acao da App Store");
        video_print("Erro: catalogo da App Store indisponivel.\n", 0x0C);
        return ERR_UNAVAILABLE;
    }
    cmd_pkg_uppercase_id(key);
    if (app_catalog_find_entry(key, &shell_store_workspace.entry) != OK) {
        LOG_WARN("SHELL", "Entrada de acao nao encontrada no catalogo");
        return ERR_NOT_FOUND;
    }
    return OK;
}

static void cmd_store_refresh_after_mutation(void) {
    if (app_catalog_refresh() != OK) {
        LOG_WARN("SHELL", "Catalogo nao atualizou apos mutacao da App Store");
        video_print("Aviso: operacao terminou, mas o catalogo nao atualizou.\n",
                    0x0E);
    }
}

static void cmd_store_set_local_failure(
    app_package_action_reason_t reason) {
    kmemset(&shell_store_workspace.action, 0,
            sizeof(shell_store_workspace.action));
    shell_store_workspace.action.reason = reason;
}

static void cmd_store_print_failed_transaction(int confirmed) {
    if (confirmed &&
        app_package_get_status(&shell_store_workspace.status) == OK &&
        shell_store_workspace.status.transaction_pending) {
        video_print("Transacao interrompida; reinicie para recuperar.\n",
                    0x0E);
        return;
    }
    video_print("Nenhuma gravacao foi realizada.\n", 0x0C);
}

static int cmd_store_build_plan(char* key, int update, int allow_downgrade) {
    app_package_plan_t* plan = &shell_store_workspace.action.plan;
    int result;

    result = cmd_store_find_action_entry(key);
    if (result != OK || !shell_store_workspace.entry.has_source) {
        cmd_store_set_local_failure(
            result == ERR_UNAVAILABLE ?
                APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE :
                APP_PACKAGE_ACTION_REASON_SOURCE_NOT_FOUND);
        return result == OK ? ERR_NOT_FOUND : result;
    }
    kmemset(plan, 0, sizeof(*plan));
    result = update ? app_catalog_build_update_plan(key, allow_downgrade, plan) :
                      app_catalog_build_install_plan(key, plan);
    if (result != OK) {
        shell_store_workspace.action.reason = plan->reason ? plan->reason :
            APP_PACKAGE_ACTION_REASON_PLAN_INCOMPLETE;
    }
    return result;
}

static void cmd_store_install(char* alias, int confirmed) {
    int result = cmd_store_build_plan(alias, 0, 0);

    if (result != OK) {
        cmd_store_print_action_result("Preflight de instalacao: ",
                                      result);
        return;
    }
    result = confirmed ?
        app_package_apply_plan_confirmed(&shell_store_workspace.action.plan,
                                         &shell_store_workspace.action) :
        app_package_preflight_plan(&shell_store_workspace.action.plan,
                                   &shell_store_workspace.action);
    if (confirmed) cmd_store_refresh_after_mutation();
    cmd_store_print_action_result(
        confirmed ? "Instalacao confirmada: " :
                    "Preflight de instalacao: ",
        result);
    if (result != OK) {
        cmd_store_print_failed_transaction(confirmed);
        return;
    }
    if (confirmed) {
        video_print("Pacote instalado: ", 0x0A);
        video_print(shell_store_workspace.action.info.id, 0x0A);
        video_print(".\n", 0x0A);
        return;
    }
    video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
    video_print("Para confirmar: store install ", 0x0E);
    video_print(alias, 0x0E);
    video_print(" --confirm\n", 0x0E);
}

static void cmd_store_update(char* key, int allow_downgrade, int confirmed) {
    int result = cmd_store_build_plan(key, 1, allow_downgrade && confirmed);

    if (result != OK) {
        cmd_store_print_action_result("Preflight de atualizacao: ", result);
        if (shell_store_workspace.action.reason ==
            APP_PACKAGE_ACTION_REASON_DOWNGRADE_REQUIRES_CONFIRM) {
            video_print("Downgrade exige --downgrade e --confirm.\n", 0x0E);
        }
        return;
    }
    result = confirmed ?
        app_package_apply_plan_confirmed(&shell_store_workspace.action.plan,
                                         &shell_store_workspace.action) :
        app_package_preflight_plan(&shell_store_workspace.action.plan,
                                   &shell_store_workspace.action);
    if (confirmed) cmd_store_refresh_after_mutation();
    cmd_store_print_action_result(
        confirmed ? "Atualizacao confirmada: " :
                    "Preflight de atualizacao: ", result);
    if (result != OK) {
        cmd_store_print_failed_transaction(confirmed);
        return;
    }
    if (!confirmed) {
        video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
        video_print("Para confirmar: store update ", 0x0E);
        video_print(key, 0x0E);
        video_print(" --confirm\n", 0x0E);
    }
}

static void cmd_store_remove(char* id, int confirmed) {
    int result = cmd_store_find_action_entry(id);

    if (result != OK) {
        cmd_store_set_local_failure(
            result == ERR_UNAVAILABLE ?
                APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE :
                APP_PACKAGE_ACTION_REASON_NOT_INSTALLED);
        cmd_store_print_action_result("Preflight de remocao: ", result);
        if (confirmed) cmd_store_refresh_after_mutation();
        return;
    }
    result = confirmed ?
        app_package_remove_confirmed(
            id, &shell_store_workspace.action) :
        app_package_preflight_remove(
            id, &shell_store_workspace.action);
    if (confirmed) cmd_store_refresh_after_mutation();
    cmd_store_print_action_result(
        confirmed ? "Remocao confirmada: " : "Preflight de remocao: ",
        result);
    if (result != OK) {
        video_print("Nenhuma gravacao foi realizada.\n", 0x0C);
        return;
    }
    if (confirmed) {
        video_print("Pacote removido: ", 0x0A);
        video_print(id, 0x0A);
        video_print(".\n", 0x0A);
        return;
    }
    video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
    video_print("Para confirmar: store remove ", 0x0E);
    video_print(id, 0x0E);
    video_print(" --confirm\n", 0x0E);
}

static void cmd_store_rollback(char* id, int confirmed) {
    int result;

    cmd_pkg_uppercase_id(id);
    result = confirmed ? app_package_rollback_confirmed(
        id, &shell_store_workspace.action) : app_package_preflight_rollback(
        id, &shell_store_workspace.action);
    if (confirmed) cmd_store_refresh_after_mutation();
    cmd_store_print_action_result(
        confirmed ? "Rollback confirmado: " : "Preflight de rollback: ",
        result);
    if (result != OK) {
        cmd_store_print_failed_transaction(confirmed);
        return;
    }
    if (!confirmed) {
        video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
        video_print("Para confirmar: store rollback ", 0x0E);
        video_print(id, 0x0E);
        video_print(" --confirm\n", 0x0E);
    }
}

static void cmd_store_history(char* id) {
    uint32_t count = 0;
    int printed = 0;

    if (id && id[0]) cmd_pkg_uppercase_id(id);
    if (app_package_get_history_count(&count) != OK) {
        video_print("Historico AS4 indisponivel.\n", 0x0C);
        return;
    }
    video_print("Historico App Store:\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        app_package_history_entry_t entry;

        if (app_package_get_history_entry(index, &entry) != OK ||
            (id && id[0] && kstrcmp(entry.id, id) != 0)) continue;
        video_print("  #", 0x08);
        print_num(entry.sequence);
        video_print(" ", 0x07);
        video_print(app_package_history_operation_name(entry.operation), 0x0B);
        video_print(" ", 0x07);
        video_print(entry.id[0] ? entry.id : "N/D", 0x07);
        video_print(" ", 0x08);
        video_print(entry.from_version[0] ? entry.from_version : "-", 0x08);
        video_print("->", 0x08);
        video_print(entry.to_version[0] ? entry.to_version : "-", 0x08);
        video_print(" ", 0x08);
        video_print(app_package_history_outcome_name(entry.outcome),
                    entry.outcome == APP_PACKAGE_HISTORY_OUTCOME_SUCCESS ?
                    0x0A : 0x0E);
        video_print("\n", 0x07);
        printed = 1;
    }
    if (!printed) video_print("  (vazio)\n", 0x08);
}

static void cmd_store_test_fail_after(char* value) {
    uint32_t count = parse_number(value);
    int result = app_package_test_fail_after((uint16_t)count);

    if (result == OK) {
        video_print("Failpoint AS4 configurado apos ", 0x0A);
        print_num(count);
        video_print(" trocas de arquivo.\n", 0x0A);
    } else {
        video_print("Uso: store test fail-after <1..32>\n", 0x0E);
    }
}

static void cmd_store_run(char* id, const char* arguments) {
    int result = cmd_store_find_action_entry(id);

    if (result != OK || !shell_store_workspace.entry.has_installed) {
        cmd_store_set_local_failure(
            result == ERR_UNAVAILABLE ?
                APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE :
                APP_PACKAGE_ACTION_REASON_NOT_INSTALLED);
        cmd_store_print_action_result("Execucao instalada: ",
                                      result == OK ? ERR_NOT_FOUND : result);
        return;
    }
    result = app_loader_build_launch_info(
        arguments, &shell_store_workspace.launch);
    if (result != OK) {
        cmd_store_set_local_failure(
            APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT);
        cmd_store_print_action_result("Execucao instalada: ", result);
        return;
    }
    result = app_package_run_installed(
        id, &shell_store_workspace.launch, &shell_store_workspace.pid,
        &shell_store_workspace.action);
    cmd_store_print_action_result("Execucao instalada: ", result);
    if (result != OK) return;
    video_print("Aplicativo iniciado de forma assincrona, PID ", 0x0A);
    print_num(shell_store_workspace.pid);
    video_print(". Use F12 para cancelar.\n", 0x0A);
}

static void cmd_store_remote_status(void) {
    app_remote_status_t* status = &shell_store_workspace.remote_status;

    if (app_remote_get_status(status) != OK) {
        video_print("Repositorio remoto indisponivel.\n", 0x0C);
        return;
    }
    video_print("Repositorio App Store remoto (raiz de teste):\n", 0x0B);
    video_print("  estado=", 0x07);
    video_print(app_remote_state_name(status->state),
                status->state == APP_REMOTE_STATE_FAILED ? 0x0C :
                status->enabled ? 0x0A : 0x08);
    video_print(" motivo=", 0x08);
    video_print(app_remote_reason_name(status->reason),
                status->reason == APP_REMOTE_REASON_NONE ? 0x0A : 0x0E);
    video_print(" rede=", 0x08);
    video_print(status->network_ready ? "READY" : "UNAVAILABLE",
                status->network_ready ? 0x0A : 0x0E);
    video_print(" autenticado=", 0x08);
    video_print(status->catalog_available ? "SIM" : "NAO",
                status->catalog_available ? 0x0A : 0x0E);
    video_print("\n  geracao=", 0x07);
    print_num(status->generation);
    video_print(" maxima=", 0x08);
    print_num(status->highest_generation);
    video_print(" entradas=", 0x08);
    print_num(status->entry_count);
    video_print("\n  cache=", 0x07);
    video_print(app_remote_cache_state_name(status->cache_state),
                status->cache_state == APP_REMOTE_CACHE_VALID ? 0x0A : 0x0E);
    video_print(" pendente=", 0x08);
    video_print(status->cache_pending ? "SIM" : "NAO",
                status->cache_pending ? 0x0E : 0x0A);
    if (status->cache_target_available) {
        video_print(" alvo=", 0x08);
        video_print(status->cache_target, 0x0B);
    }
    video_print("\n  URL=", 0x07);
    video_print(status->catalog_url[0] ? status->catalog_url : "padrao",
                0x08);
    video_print("\n", 0x07);
}

static void cmd_store_remote_print_entry(const app_remote_entry_t* entry) {
    if (!entry) return;
    video_print("  ", 0x07);
    video_print(entry->info.id, 0x0B);
    video_print(" ", 0x07);
    video_print(entry->info.version, 0x07);
    video_print(" ", 0x07);
    video_print(app_remote_entry_state_name(entry->state),
                entry->state == APP_REMOTE_ENTRY_BLOCKED ? 0x0C : 0x0A);
    video_print(entry->cached ? " CACHE" : "", 0x0E);
    video_print("\n", 0x07);
}

static void cmd_store_remote_list(void) {
    uint32_t count = 0U;

    (void)app_remote_refresh_provenance();
    if (app_remote_get_count(&count) != OK) {
        video_print("Catalogo remoto ainda nao foi consultado.\n", 0x0E);
        return;
    }
    video_print("Catalogo REMOTO / AUTENTICADO (TESTE):\n", 0x0B);
    for (uint32_t index = 0; index < count; index++) {
        if (app_remote_get_entry(index,
                                 &shell_store_workspace.remote_entry) == OK) {
            cmd_store_remote_print_entry(&shell_store_workspace.remote_entry);
        }
    }
}

static void cmd_store_remote_info(char* id) {
    app_remote_entry_t* entry = &shell_store_workspace.remote_entry;

    cmd_pkg_uppercase_id(id);
    (void)app_remote_refresh_provenance();
    if (app_remote_find_entry(id, entry) != OK) {
        video_print("Entrada remota nao encontrada.\n", 0x0C);
        return;
    }
    video_print("Aplicativo REMOTO / AUTENTICADO (TESTE):\n  ID: ", 0x0B);
    video_print(entry->info.id, 0x07);
    video_print("\n  Nome: ", 0x07);
    video_print(entry->info.name, 0x07);
    video_print("\n  Versao: ", 0x07);
    video_print(entry->info.version, 0x07);
    video_print("\n  Estado: ", 0x07);
    video_print(app_remote_entry_state_name(entry->state), 0x0A);
    video_print("\n  Motivo: ", 0x07);
    video_print(app_remote_reason_name(entry->reason),
                entry->reason == APP_REMOTE_REASON_NONE ? 0x0A : 0x0E);
    video_print("\n  Cache: ", 0x07);
    video_print(entry->cached ? "SIM" : "NAO",
                entry->cached ? 0x0A : 0x08);
    video_print("\n  Tamanho: ", 0x07);
    print_num(entry->package_size);
    video_print(" bytes\n  SHA-256: ", 0x07);
    for (uint32_t index = 0; index < 32U; index++) {
        cmd_print_hex(entry->package_hash[index], 2U);
    }
    video_print("\n  Dependencias: ", 0x07);
    if (!entry->info.dependency_count) video_print("nenhuma", 0x08);
    for (uint32_t index = 0; index < entry->info.dependency_count; index++) {
        if (index) video_print(", ", 0x07);
        video_print(entry->info.dependencies[index], 0x07);
    }
    video_print("\n  Caminho: ", 0x07);
    video_print(entry->package_path, 0x08);
    video_print("\n  Procedencia instalada: ", 0x07);
    video_print(!entry->installed ? "N/A" :
                app_remote_get_installed_trust(
                    entry->info.id, entry->installed_version) ?
                "REMOTO / AUTENTICADO (TESTE)" : "N/D",
                entry->installed && app_remote_get_installed_trust(
                    entry->info.id, entry->installed_version) ? 0x0A : 0x08);
    video_print("\n", 0x07);
}

static void cmd_store_remote_print_result(const char* label, int result) {
    app_remote_result_t* remote = &shell_store_workspace.remote_result;

    video_print(label, 0x0B);
    video_print(result == OK ? "OK" : "BLOQUEADO",
                result == OK ? 0x0A : 0x0C);
    video_print(" motivo=", 0x08);
    video_print(app_remote_reason_name(remote->reason),
                remote->reason == APP_REMOTE_REASON_NONE ? 0x0A : 0x0E);
    if (remote->http_status) {
        video_print(" HTTP=", 0x08);
        print_num(remote->http_status);
    }
    video_print("\n", 0x07);
    if (remote->required_clusters || remote->free_clusters) {
        video_print("  Clusters: necessarios=", 0x07);
        print_num(remote->required_clusters);
        video_print(" livres=", 0x08);
        print_num(remote->free_clusters);
        video_print("\n", 0x07);
    }
    cmd_store_print_plan(&remote->plan);
    if (remote->package_action.reason != APP_PACKAGE_ACTION_REASON_NONE) {
        shell_store_workspace.action = remote->package_action;
        cmd_store_print_action_result("  Motor AS4: ", result);
    }
}

static int cmd_store_remote_options(const char** cursor, int allow_url,
                                    int* confirmed, int* downgrade) {
    char* token = shell_store_workspace.remote_token;

    while (1) {
        cmd_pkg_take_token(cursor, token,
                           sizeof(shell_store_workspace.remote_token));
        if (!token[0]) return OK;
        if (kstrcmp(token, "--confirm") == 0) {
            *confirmed = 1;
        } else if (kstrcmp(token, "--downgrade") == 0) {
            *downgrade = 1;
        } else if (allow_url && kstrcmp(token, "--url") == 0) {
            cmd_pkg_take_token(cursor, shell_store_workspace.remote_url,
                               sizeof(shell_store_workspace.remote_url));
            if (!shell_store_workspace.remote_url[0]) {
                LOG_WARN("SHELL", "URL ausente na operacao remota");
                return ERR_INVALID;
            }
        } else {
            LOG_WARN("SHELL", "Opcao remota da App Store e invalida");
            return ERR_INVALID;
        }
    }
}

static int cmd_store_remote_control(const char* action, const char* arguments,
                                    app_remote_options_t* options) {
    const char* cursor = arguments;
    int confirmed = 0;
    int downgrade = 0;
    int result;

    if (kstrcmp(action, "status") == 0) {
        cmd_store_remote_status();
        return 1;
    }
    if (kstrcmp(action, "enable") == 0 || kstrcmp(action, "disable") == 0) {
        result = kstrcmp(action, "enable") == 0 ?
                 app_remote_enable() : app_remote_disable();
        video_print(result == OK ? "Controle remoto atualizado.\n" :
                                   "Controle remoto falhou.\n",
                    result == OK ? 0x0A : 0x0C);
        cmd_store_remote_status();
        return 1;
    }
    if (kstrcmp(action, "list") == 0) {
        cmd_store_remote_list();
        return 1;
    }
    if (kstrcmp(action, "check") != 0 && kstrcmp(action, "clear") != 0) {
        return 0;
    }
    if (cmd_store_remote_options(&cursor, kstrcmp(action, "check") == 0,
                                 &confirmed, &downgrade) != OK ||
        downgrade || (kstrcmp(action, "check") == 0 && confirmed)) {
        cmd_store_print_usage();
        return 1;
    }
    kmemset(options, 0, sizeof(*options));
    options->cancel_check = cmd_update_cancel_check;
    result = kstrcmp(action, "check") == 0 ?
        app_remote_check(shell_store_workspace.remote_url, options,
                         &shell_store_workspace.remote_result) :
        app_remote_clear(confirmed, &shell_store_workspace.remote_result);
    cmd_store_remote_print_result(
        kstrcmp(action, "check") == 0 ? "Consulta remota: " :
                                        "Limpeza remota: ", result);
    if (kstrcmp(action, "clear") == 0 && !confirmed && result == OK) {
        video_print("Nenhuma gravacao. Confirme com --confirm.\n", 0x0E);
    }
    return 1;
}

static void cmd_store_remote(const char* args) {
    const char* cursor = args;
    char* action = shell_store_workspace.value;
    char* id = shell_store_workspace.option;
    app_remote_options_t* options = &shell_store_workspace.remote_options;
    int confirmed = 0;
    int downgrade = 0;
    int result;

    cmd_pkg_take_token(&cursor, action, sizeof(shell_store_workspace.value));
    if (cmd_store_remote_control(action, cursor, options)) return;
    cmd_pkg_take_token(&cursor, id, sizeof(shell_store_workspace.option));
    if (kstrcmp(action, "test") == 0) {
        cmd_pkg_take_token(&cursor, shell_store_workspace.extra,
                           sizeof(shell_store_workspace.extra));
        if (kstrcmp(id, "fail-after") == 0 &&
            shell_store_workspace.extra[0]) {
            uint32_t fail_after = parse_number(
                shell_store_workspace.extra);

            result = fail_after >= 1U && fail_after <= 16U ?
                     app_remote_test_fail_after((uint8_t)fail_after) :
                     ERR_INVALID;
            video_print(result == OK ? "Failpoint AS5 configurado.\n" :
                                       "Failpoint AS5 invalido.\n",
                        result == OK ? 0x0A : 0x0C);
            return;
        }
        cmd_store_print_usage();
        return;
    }
    if (!id[0]) {
        cmd_store_print_usage();
        return;
    }
    if (kstrcmp(action, "info") == 0) {
        cmd_store_remote_info(id);
        return;
    }
    if (cmd_store_remote_options(&cursor,
                                 kstrcmp(action, "fetch") == 0,
                                 &confirmed, &downgrade) != OK) {
        cmd_store_print_usage();
        return;
    }
    cmd_pkg_uppercase_id(id);
    kmemset(options, 0, sizeof(*options));
    options->allow_downgrade = downgrade ? 1U : 0U;
    options->cancel_check = cmd_update_cancel_check;
    if (kstrcmp(action, "update") == 0 && downgrade && !confirmed) {
        video_print("Downgrade remoto exige --downgrade e --confirm.\n", 0x0E);
        return;
    }
    if (kstrcmp(action, "update") == 0 && !downgrade &&
        app_remote_find_entry(id, &shell_store_workspace.remote_entry) == OK &&
        shell_store_workspace.remote_entry.state == APP_REMOTE_ENTRY_DOWNGRADE) {
        video_print("Downgrade remoto exige --downgrade e --confirm.\n", 0x0E);
        return;
    }
    if (kstrcmp(action, "fetch") == 0 && !downgrade) {
        result = app_remote_fetch(id, shell_store_workspace.remote_url,
                                  confirmed, options,
                                  &shell_store_workspace.remote_result);
        cmd_store_remote_print_result(confirmed ? "Cache remoto: " :
                                                  "Plano de download: ",
                                      result);
    } else if (kstrcmp(action, "install") == 0 && !downgrade) {
        result = app_remote_apply_cached(
            id, 0, confirmed, options,
            &shell_store_workspace.remote_result);
        cmd_store_remote_print_result(confirmed ? "Instalacao remota: " :
                                                  "Preflight remoto: ", result);
    } else if (kstrcmp(action, "update") == 0 &&
               (!downgrade || confirmed)) {
        result = app_remote_apply_cached(
            id, 1, confirmed, options,
            &shell_store_workspace.remote_result);
        cmd_store_remote_print_result(confirmed ? "Atualizacao remota: " :
                                                  "Preflight remoto: ", result);
    } else {
        cmd_store_print_usage();
        return;
    }
    if (confirmed && result == OK) cmd_store_refresh_after_mutation();
    if (!confirmed && result == OK) {
        video_print("Nenhuma gravacao foi realizada. Use --confirm.\n", 0x0E);
    }
}

static void cmd_store(const char* args) {
    const char* cursor = args;
    int confirmed;
    int downgrade;

    kmemset(&shell_store_workspace, 0, sizeof(shell_store_workspace));
    if (!args || !args[0]) {
        shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
        return;
    }
    cmd_pkg_take_token(&cursor, shell_store_workspace.operation,
                       sizeof(shell_store_workspace.operation));
    if (kstrcmp(shell_store_workspace.operation, "remote") == 0) {
        cmd_store_remote(cursor);
        return;
    }
    cmd_pkg_take_token(&cursor, shell_store_workspace.value,
                       sizeof(shell_store_workspace.value));
    if (kstrcmp(shell_store_workspace.operation, "run") == 0 &&
        shell_store_workspace.value[0]) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        cmd_pkg_uppercase_id(shell_store_workspace.value);
        cmd_store_run(shell_store_workspace.value, cursor);
        return;
    }
    cmd_pkg_take_token(&cursor, shell_store_workspace.option,
                       sizeof(shell_store_workspace.option));
    cmd_pkg_take_token(&cursor, shell_store_workspace.extra,
                       sizeof(shell_store_workspace.extra));
    confirmed = kstrcmp(shell_store_workspace.option, "--confirm") == 0 ||
                kstrcmp(shell_store_workspace.extra, "--confirm") == 0;
    downgrade = kstrcmp(shell_store_workspace.option, "--downgrade") == 0 ||
                kstrcmp(shell_store_workspace.extra, "--downgrade") == 0;
    if (shell_store_workspace.operation[0] == '\0') {
        shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
    } else if (kstrcmp(shell_store_workspace.operation, "status") == 0 &&
               shell_store_workspace.value[0] == '\0' &&
               shell_store_workspace.option[0] == '\0') {
        cmd_store_status();
    } else if (kstrcmp(shell_store_workspace.operation, "list") == 0 &&
               shell_store_workspace.value[0] == '\0' &&
               shell_store_workspace.option[0] == '\0') {
        cmd_store_list();
    } else if (kstrcmp(shell_store_workspace.operation, "info") == 0 &&
               shell_store_workspace.value[0] != '\0' &&
               shell_store_workspace.option[0] == '\0') {
        cmd_store_info(shell_store_workspace.value);
    } else if (kstrcmp(shell_store_workspace.operation, "install") == 0 &&
               shell_store_workspace.value[0] != '\0' &&
               shell_store_workspace.extra[0] == '\0' &&
               (shell_store_workspace.option[0] == '\0' || confirmed)) {
        cmd_store_install(shell_store_workspace.value, confirmed);
    } else if (kstrcmp(shell_store_workspace.operation, "update") == 0 &&
               shell_store_workspace.value[0] != '\0' &&
               ((shell_store_workspace.option[0] == '\0' &&
                 shell_store_workspace.extra[0] == '\0') ||
                (confirmed && downgrade))) {
        cmd_store_update(shell_store_workspace.value, downgrade, confirmed);
    } else if (kstrcmp(shell_store_workspace.operation, "update") == 0 &&
               shell_store_workspace.value[0] != '\0' &&
               shell_store_workspace.extra[0] == '\0' &&
               (kstrcmp(shell_store_workspace.option, "--confirm") == 0 ||
                kstrcmp(shell_store_workspace.option, "--downgrade") == 0)) {
        cmd_store_update(shell_store_workspace.value, downgrade, confirmed);
    } else if (kstrcmp(shell_store_workspace.operation, "remove") == 0 &&
               shell_store_workspace.value[0] != '\0' &&
               shell_store_workspace.extra[0] == '\0' &&
               (shell_store_workspace.option[0] == '\0' || confirmed)) {
        cmd_store_remove(shell_store_workspace.value, confirmed);
    } else if (kstrcmp(shell_store_workspace.operation, "rollback") == 0 &&
               shell_store_workspace.value[0] != '\0' &&
               shell_store_workspace.extra[0] == '\0' &&
               (shell_store_workspace.option[0] == '\0' || confirmed)) {
        cmd_store_rollback(shell_store_workspace.value, confirmed);
    } else if (kstrcmp(shell_store_workspace.operation, "history") == 0 &&
               shell_store_workspace.option[0] == '\0' &&
               shell_store_workspace.extra[0] == '\0') {
        cmd_store_history(shell_store_workspace.value);
    } else if (kstrcmp(shell_store_workspace.operation, "test") == 0 &&
               kstrcmp(shell_store_workspace.value, "fail-after") == 0 &&
               shell_store_workspace.option[0] != '\0' &&
               shell_store_workspace.extra[0] == '\0') {
        cmd_store_test_fail_after(shell_store_workspace.option);
    } else {
        LOG_WARN("SHELL", "Uso invalido do comando store");
        cmd_store_print_usage();
    }
}

static void cmd_app(const char* args) {
    char subcommand[16];
    char path[FS_MAX_PATH];
    app_launch_info_t launch;
    uint32_t sub_length = 0;
    uint32_t path_length = 0;
    uint32_t pid = 0;
    int result;

    if (!args) {
        video_print("Uso: app run <arquivo.ZAP> [args] | app inputtest | app outputtest [fail] | app argtest <texto>\n", 0x0E);
        return;
    }
    while (args[sub_length] && args[sub_length] != ' ' &&
           args[sub_length] != '\t' && sub_length < sizeof(subcommand) - 1) {
        subcommand[sub_length] = args[sub_length];
        sub_length++;
    }
    subcommand[sub_length] = '\0';
    while (args[sub_length] == ' ' || args[sub_length] == '\t') sub_length++;

    if (kstrcmp(subcommand, "inputtest") == 0) {
        if (args[sub_length] != '\0') {
            video_print("Uso: app inputtest\n", 0x0E);
            return;
        }
        cmd_app_inputtest();
        return;
    }

    if (kstrcmp(subcommand, "outputtest") == 0) {
        cmd_app_outputtest(args + sub_length);
        return;
    }

    if (kstrcmp(subcommand, "argtest") == 0) {
        cmd_app_argtest(args + sub_length);
        return;
    }

    if (kstrcmp(subcommand, "run") != 0) {
        video_print("Uso: app run <arquivo.ZAP> [args] | app inputtest | app outputtest [fail] | app argtest <texto>\n", 0x0E);
        return;
    }
    while (args[sub_length] && args[sub_length] != ' ' &&
           args[sub_length] != '\t' && path_length < FS_MAX_PATH - 1) {
        path[path_length++] = args[sub_length++];
    }
    path[path_length] = '\0';
    if (path_length == 0) {
        video_print("Uso: app run <arquivo.ZAP> [args]\n", 0x0E);
        return;
    }
    while (args[sub_length] == ' ' || args[sub_length] == '\t') sub_length++;
    result = app_loader_build_launch_info(args + sub_length, &launch);
    if (result != OK) {
        video_print("Erro: argumentos invalidos (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    result = app_loader_run_file_with_launch(path, &launch, &pid);
    if (result != OK) {
        video_print("Erro: nao foi possivel executar o aplicativo (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Aplicativo iniciado de forma assincrona, PID ", 0x0A);
    print_num(pid);
    video_print(".\n", 0x0A);
}

static void cmd_usertest(const char* args) {
    uint32_t pid = 0;
    int trigger_fault = args && kstrcmp(args, "fault") == 0;
    int result = process_create_user_test(trigger_fault, &pid);

    if (result != OK) {
        video_print("Erro: nao foi possivel criar o teste ring 3 (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print(trigger_fault ? "UserTest fault criado, PID " :
                               "UserTest criado, PID ", 0x0A);
    print_num(pid);
    video_print(".\n", 0x0A);
    shell_waiting_user_test = 1;
}

static void cmd_guimode(const char* args) {
    char mode_name[16];
    int i = 0;
    int result;

    if (!args || !*args) {
        video_print("Modo Desktop: ", 0x0B);
        if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
            video_print("classic\n", 0x07);
        } else if (desktop_get_mode() == DESKTOP_MODE_SIMPLE) {
            video_print("simple\n", 0x07);
        } else {
            video_print("modern (reservado)\n", 0x07);
        }
        return;
    }

    while (args[i] && args[i] != ' ' && i < (int)sizeof(mode_name) - 1) {
        mode_name[i] = args[i];
        i++;
    }
    mode_name[i] = '\0';

    if (kstrcmp(mode_name, "simple") == 0) {
        result = desktop_set_mode(DESKTOP_MODE_SIMPLE);
        if (result == OK) {
            if (wm_is_active()) wm_set_active(0);
            shell_resume_terminal();
            video_print("Desktop em modo simple.\n", 0x0A);
        }
        return;
    }

    if (kstrcmp(mode_name, "classic") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_VESA) ||
            !recovery_is_available(RECOVERY_COMPONENT_BACKBUFFER)) {
            video_print("Modo classic indisponivel; simple mantido.\n", 0x0C);
            return;
        }

        result = desktop_set_mode(DESKTOP_MODE_CLASSIC);
        if (result == OK) {
            video_print("Desktop em modo classic.\n", 0x0A);
        } else if (result == ERR_NOT_FOUND) {
            video_print("Modo classic requer VESA; simple mantido.\n", 0x0C);
        }
        return;
    }

    if (kstrcmp(mode_name, "modern") == 0) {
        video_print("Modo modern reservado para a futura interface.\n", 0x0E);
        return;
    }

    video_print("Uso: guimode simple|classic\n", 0x0C);
}

static void cmd_guitest(const char* args) {
    char scene_name[16];
    int index = 0;
    int modern_scene = 0;

    if (args && *args) {
        while (args[index] && args[index] != ' ' && args[index] != '\t' &&
               index < (int)sizeof(scene_name) - 1) {
            scene_name[index] = args[index];
            index++;
        }
        scene_name[index] = '\0';
        args += index;
        while (*args == ' ' || *args == '\t') args++;
        if (*args || kstrcmp(scene_name, "modern") != 0) {
            LOG_WARN("SHELL", "Argumento invalido para guitest");
            video_print("Uso: guitest [modern]\n", 0x0C);
            return;
        }
        modern_scene = 1;
    }
    if (modern_scene && desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
        LOG_WARN("SHELL", "guitest modern requer o modo Classic");
        video_print("Erro: guitest modern requer guimode classic.\n", 0x0C);
        return;
    }
    if (!recovery_is_enabled(RECOVERY_COMPONENT_GUITEST) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("SHELL", "GUI Test indisponivel");
        video_print("Erro: GUI Test indisponivel.\n", 0x0C);
        return;
    }

    if (wm_is_active()) wm_set_active(0);
    shell_suspend_terminal();
    if (modern_scene) {
        guitest_open_modern();
    } else {
        guitest_open();
    }
}

static void cmd_display_status(void) {
    display_metrics_t metrics;
    vesa_mode_t* mode = vesa_get_mode();
    tb_rect_t work_area;

    if (display_get_metrics(&metrics) != OK) {
        video_print("Display indisponivel.\n", 0x0C);
        return;
    }
    work_area.x = 0;
    work_area.y = 0;
    work_area.width = mode && mode->initialized ? (int)mode->width : 0;
    work_area.height = mode && mode->initialized ? (int)mode->height : 0;
    if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
        (void)taskbar_get_work_area(&work_area);
    }

    video_print("Display:\n", 0x0B);
    video_print("  VESA: ", 0x07);
    if (mode && mode->initialized) {
        print_num(mode->width);
        video_print("x", 0x07);
        print_num(mode->height);
        video_print("x", 0x07);
        print_num(mode->bpp);
        video_print("\n", 0x07);
    } else {
        video_print("indisponivel\n", 0x0C);
    }
    video_print("  Backbuffer: ", 0x07);
    video_print(vesa_has_backbuffer() ? "OK\n" : "indisponivel\n",
                vesa_has_backbuffer() ? 0x0A : 0x0C);
    video_print("  Interface: ", 0x07);
    if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
        video_print("classic\n", 0x07);
    } else if (desktop_get_mode() == DESKTOP_MODE_SIMPLE) {
        video_print("simple\n", 0x07);
    } else {
        video_print("modern (reservado)\n", 0x07);
    }
    video_print("  Escala: ", 0x07);
    video_print(display_scale_name(metrics.scale), 0x0B);
    video_print(" (", 0x07);
    print_num(metrics.factor_numerator);
    video_print("/", 0x07);
    print_num(metrics.factor_denominator);
    video_print(metrics.available ? ", disponivel)\n" :
                                    ", indisponivel)\n", 0x07);
    if (desktop_get_mode() == DESKTOP_MODE_SIMPLE) {
        video_print("  Fonte ativa: legada 8x16\n", 0x07);
        video_print("  Preset Classic: " FONT_UI_FAMILY_NAME " ", 0x07);
        print_num(metrics.font_width);
        video_print("x", 0x07);
        print_num(metrics.font_height);
        video_print(" (nativa, RAM)\n", 0x07);
        video_print("  Espaco Classic: ", 0x07);
    } else {
        video_print("  Fonte: " FONT_UI_FAMILY_NAME " ", 0x07);
        print_num(metrics.font_width);
        video_print("x", 0x07);
        print_num(metrics.font_height);
        video_print(" (nativa)  Espaco: ", 0x07);
    }
    print_num(metrics.spacing);
    video_print(" px\n", 0x07);
    video_print("  Taskbar: ", 0x07);
    print_num(metrics.taskbar_height);
    video_print("x", 0x07);
    print_num(metrics.taskbar_side_width);
    video_print(" px  Titulo: ", 0x07);
    print_num(metrics.title_bar_height);
    video_print(" px\n", 0x07);
    video_print("  Botao minimo: ", 0x07);
    print_num(metrics.button_min_width);
    video_print("x", 0x07);
    print_num(metrics.button_min_height);
    video_print("  Icone: ", 0x07);
    print_num(metrics.icon_size);
    video_print(" px\n", 0x07);
    video_print("  VESA minima: ", 0x07);
    print_num(metrics.min_width);
    video_print("x", 0x07);
    print_num(metrics.min_height);
    video_print("\n", 0x07);
    video_print("  Area util: ", 0x07);
    print_num((uint32_t)work_area.width);
    video_print("x", 0x07);
    print_num((uint32_t)work_area.height);
    video_print("\n", 0x07);
}

static void cmd_display(const char* args) {
    char scale_name[16];
    display_scale_t scale;
    int index = 0;
    int result;

    if (args && kstrcmp(args, "status") == 0) {
        cmd_display_status();
        return;
    }
    if (!args || args[0] != 's' || args[1] != 'c' || args[2] != 'a' ||
        args[3] != 'l' || args[4] != 'e' ||
        (args[5] != ' ' && args[5] != '\t')) {
        video_print("Uso: display status | display scale "
                    "<pequena|normal|grande>\n", 0x0C);
        return;
    }

    args += 5;
    while (*args == ' ' || *args == '\t') args++;
    while (args[index] && args[index] != ' ' && args[index] != '\t' &&
           index < (int)sizeof(scale_name) - 1) {
        scale_name[index] = args[index];
        index++;
    }
    scale_name[index] = '\0';
    args += index;
    while (*args == ' ' || *args == '\t') args++;
    if (!scale_name[0] || *args ||
        display_parse_scale(scale_name, &scale) != OK) {
        video_print("Escala invalida; use pequena, normal ou grande.\n",
                    0x0C);
        return;
    }

    result = display_apply_scale(scale);
    if (result != OK) {
        if (result == ERR_OVERFLOW) {
            video_print("Escala recusada: VESA ou area util insuficiente; ",
                        0x0C);
        } else if (result == ERR_UNAVAILABLE || result == ERR_STATE) {
            video_print("Escala recusada: display Classic indisponivel; ",
                        0x0C);
        } else {
            video_print("Escala recusada: reflow da cena falhou; ", 0x0C);
        }
        video_print("estado anterior preservado.\n", 0x0C);
        return;
    }
    video_print("Escala ", 0x0A);
    video_print(display_scale_name(scale), 0x0A);
    video_print(" aplicada em RAM.\n", 0x0A);
}

static void cmd_storage_print_usage(void) {
    video_print("Uso: storage list | storage info <id> | ", 0x0C);
    video_print("storage mount <id> | storage unmount <id>\n", 0x0C);
}

static int cmd_storage_read_token(const char** cursor, char* token,
                                  int token_size) {
    const char* input;
    int length = 0;

    if (!cursor || !*cursor || !token || token_size < 2) {
        LOG_ERROR("SHELL", "Destino invalido no parser de storage");
        return ERR_NULL;
    }
    input = *cursor;
    while (*input == ' ' || *input == '\t') input++;
    if (!*input) {
        token[0] = '\0';
        *cursor = input;
        return ERR_NOT_FOUND;
    }
    while (*input && *input != ' ' && *input != '\t') {
        if (length >= token_size - 1) {
            LOG_ERROR("SHELL", "Argumento storage longo demais");
            return ERR_OVERFLOW;
        }
        token[length++] = *input++;
    }
    token[length] = '\0';
    *cursor = input;
    return OK;
}

static int cmd_storage_has_extra(const char* cursor) {
    if (!cursor) {
        LOG_ERROR("SHELL", "Cursor nulo no parser de storage");
        return 1;
    }
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (!*cursor) return 0;
    LOG_ERROR("SHELL", "Argumentos excedentes no comando storage");
    return 1;
}

static void cmd_storage_print_disk(const storage_disk_t* disk) {
    if (!disk) return;
    video_print("Disco ", 0x0B);
    video_print(disk->id, 0x0B);
    video_print(": ", 0x07);
    video_print(disk->model, 0x07);
    video_print("\n  Slot/canal: ", 0x07);
    print_num(disk->slot);
    video_print(disk->channel ? " secundario " : " primario ", 0x07);
    video_print(disk->slave ? "slave\n" : "master\n", 0x07);
    video_print("  Setores: ", 0x07);
    print_num(disk->sector_count);
    video_print("  Leituras: ", 0x07);
    print_num(disk->read_ops);
    video_print("  Escritas: ", 0x07);
    print_num(disk->write_ops);
    video_print("  Erro: ", 0x07);
    print_num((uint32_t)disk->last_error);
    video_print("\n", 0x07);
}

static void cmd_storage_print_volume(const storage_volume_t* volume) {
    if (!volume) return;
    video_print("Volume ", 0x0B);
    video_print(volume->id, 0x0B);
    video_print(" (", 0x07);
    video_print(volume->disk_id, 0x07);
    video_print(")\n  Estado: ", 0x07);
    video_print(storage_volume_state_name(volume->state),
                volume->mounted ? 0x0A : 0x0E);
    video_print("  FS: ", 0x07);
    video_print(storage_fs_name(volume->fs_type), 0x0B);
    video_print("  Acesso: ", 0x07);
    video_print(volume->boot ? "BOOT/LEGACY-RW" : "READ-ONLY", 0x0B);
    video_print("\n  LBA inicial: ", 0x07);
    print_num(volume->start_lba);
    video_print("  Setores: ", 0x07);
    print_num(volume->sector_count);
    video_print("  Bytes/setor: ", 0x07);
    print_num(volume->bytes_per_sector);
    video_print("  Setores/cluster: ", 0x07);
    print_num(volume->sectors_per_cluster);
    video_print("  Particao: ", 0x07);
    print_num(volume->partition_index);
    video_print("  Tipo: ", 0x07);
    cmd_print_hex(volume->partition_type, 2U);
    video_print("\n  Label: ", 0x07);
    video_print(volume->label[0] ? volume->label : "(sem label)", 0x07);
    video_print("  Geracao: ", 0x07);
    print_num(volume->generation);
    video_print("  Erro: ", 0x07);
    print_num((uint32_t)volume->last_error);
    video_print("\n", 0x07);
}

static void cmd_storage_list(void) {
    storage_status_t status;

    if (storage_get_status(&status) != OK || !status.initialized) {
        video_print("Storage indisponivel.\n", 0x0C);
        return;
    }
    video_print("Storage ATA: discos=", 0x0B);
    print_num(status.disk_count);
    video_print(" volumes=", 0x07);
    print_num(status.volume_count);
    video_print(" montados=", 0x07);
    print_num(status.mounted_count);
    video_print("\n", 0x07);
    for (uint8_t index = 0; index < status.disk_count; index++) {
        storage_disk_t disk;
        if (storage_get_disk_at(index, &disk) == OK) cmd_storage_print_disk(&disk);
    }
    for (uint8_t index = 0; index < status.volume_count; index++) {
        storage_volume_t volume;
        if (storage_get_volume_at(index, &volume) == OK) {
            cmd_storage_print_volume(&volume);
        }
    }
    video_print("Montagens adicionais permanecem somente em RAM.\n", 0x0E);
}

static void cmd_storage_info(const char* id) {
    storage_disk_t disk;
    storage_volume_t volume;

    if (storage_find_disk(id, &disk) == OK) {
        cmd_storage_print_disk(&disk);
        return;
    }
    if (storage_find_volume(id, &volume) == OK) {
        cmd_storage_print_volume(&volume);
        if (storage_find_disk(volume.disk_id, &disk) == OK) {
            cmd_storage_print_disk(&disk);
        }
        return;
    }
    LOG_WARN("SHELL", "ID storage nao encontrado");
    video_print("Erro: disco ou volume nao encontrado.\n", 0x0C);
}

static void cmd_storage(const char* args) {
    char action[16];
    char id[STORAGE_ID_SIZE];
    const char* cursor = args;
    int result;

    if (!args || !*args) {
        storage_status_t status;
        if (storage_get_status(&status) == OK) {
            video_print("Storage: discos=", 0x0B);
            print_num(status.disk_count);
            video_print(" volumes=", 0x07);
            print_num(status.volume_count);
            video_print(" montados=", 0x07);
            print_num(status.mounted_count);
            video_print("\n", 0x07);
        }
        cmd_storage_print_usage();
        return;
    }
    result = cmd_storage_read_token(&cursor, action, sizeof(action));
    if (result != OK) {
        cmd_storage_print_usage();
        return;
    }
    if (kstrcmp(action, "list") == 0) {
        if (cmd_storage_has_extra(cursor)) cmd_storage_print_usage();
        else cmd_storage_list();
        return;
    }
    if (cmd_storage_read_token(&cursor, id, sizeof(id)) != OK ||
        cmd_storage_has_extra(cursor)) {
        LOG_ERROR("SHELL", "ID ausente ou sintaxe invalida em storage");
        cmd_storage_print_usage();
        return;
    }
    if (kstrcmp(action, "info") == 0) {
        cmd_storage_info(id);
        return;
    }
    if (kstrcmp(action, "mount") == 0) result = storage_mount(id);
    else if (kstrcmp(action, "unmount") == 0) result = storage_unmount(id);
    else {
        LOG_ERROR("SHELL", "Subcomando storage desconhecido");
        cmd_storage_print_usage();
        return;
    }
    if (result != OK) {
        video_print("Erro: operacao storage recusada (codigo ", 0x0C);
        print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print(kstrcmp(action, "mount") == 0 ?
                "Volume montado somente-leitura em RAM.\n" :
                "Volume desmontado.\n", 0x0A);
}

static void cmd_index_print_usage(void) {
    video_print("Uso: index status | index rebuild | index cancel | ", 0x0C);
    video_print("index check\n", 0x0C);
}

static int cmd_index_read_action(
    const char* args, char action[SHELL_INDEX_ACTION_SIZE]) {
    uint32_t length = 0;

    if (!args || !action) {
        LOG_ERROR("SHELL", "Argumento nulo no parser do indice");
        return ERR_NULL;
    }
    while (*args == ' ' || *args == '\t') args++;
    while (*args && *args != ' ' && *args != '\t') {
        if (length + 1U >= SHELL_INDEX_ACTION_SIZE) {
            LOG_ERROR("SHELL", "Subcomando do indice excede o limite");
            return ERR_OVERFLOW;
        }
        action[length++] = *args++;
    }
    action[length] = '\0';
    while (*args == ' ' || *args == '\t') args++;
    if (!length || *args) {
        LOG_ERROR("SHELL", "Sintaxe invalida no comando index");
        return ERR_INVALID;
    }
    return OK;
}

static void cmd_index_status(void) {
    file_index_status_t status;

    if (file_index_get_status(&status) != OK) {
        video_print("Indice indisponivel.\n", 0x0C);
        return;
    }
    video_print("Indice: ", 0x0B);
    video_print(file_index_state_name(status.state), 0x0B);
    video_print(" ativos=", 0x07);
    print_num(status.active_entries);
    video_print(" candidato=", 0x07);
    print_num(status.candidate_entries);
    video_print(" fontes=", 0x07);
    print_num(status.source_count);
    video_print(" concluidas=", 0x07);
    print_num(status.sources_completed);
    video_print("\nDiretorios=", 0x07);
    print_num(status.directories_scanned);
    video_print(" passos=", 0x07);
    print_num(status.scan_steps);
    video_print(" memoria=", 0x07);
    print_num(status.memory_bytes);
    video_print(" evento=", 0x07);
    print_num(status.event_generation);
    video_print(" erro=", 0x07);
    print_num((uint32_t)status.last_error);
    video_print("\n", 0x07);
    if (status.partial) video_print("Aviso: indice parcial.\n", 0x0E);
    if (status.stale) video_print("Aviso: indice desatualizado.\n", 0x0E);
    if (status.automatic_suspended) {
        video_print("Aviso: rebuild automatico suspenso.\n", 0x0E);
    }
}

static void cmd_index(const char* args) {
    char action[SHELL_INDEX_ACTION_SIZE];
    int result;

    if (cmd_index_read_action(args, action) != OK) {
        cmd_index_print_usage();
        return;
    }
    if (kstrcmp(action, "status") == 0) {
        cmd_index_status();
        return;
    }
    if (kstrcmp(action, "rebuild") == 0) result = file_index_rebuild();
    else if (kstrcmp(action, "cancel") == 0) result = file_index_cancel();
    else if (kstrcmp(action, "check") == 0) {
        result = file_index_validate_state();
        if (result == OK) result = file_index_self_test();
    } else {
        cmd_index_print_usage();
        return;
    }
    if (result != OK) {
        video_print("Operacao do indice falhou; codigo=", 0x0C);
        print_num((uint32_t)result);
        video_print("\n", 0x0C);
        return;
    }
    video_print(kstrcmp(action, "check") == 0 ?
                "Indice e autoteste validos.\n" :
                (kstrcmp(action, "cancel") == 0 ?
                 "Rebuild cancelado; indice ativo preservado.\n" :
                 "Rebuild cooperativo iniciado.\n"), 0x0A);
}

static int cmd_search_copy_query(const char* args) {
    uint32_t length;

    if (!args) {
        LOG_ERROR("SHELL", "Termo nulo no comando search");
        return ERR_NULL;
    }
    while (*args == ' ' || *args == '\t') args++;
    length = kstrlen(args);
    while (length && (args[length - 1U] == ' ' ||
                      args[length - 1U] == '\t')) length--;
    if (!length) {
        LOG_ERROR("SHELL", "Termo vazio no comando search");
        return ERR_INVALID;
    }
    if (length >= FILE_INDEX_QUERY_SIZE) {
        LOG_ERROR("SHELL", "Termo excede o limite do comando search");
        return ERR_OVERFLOW;
    }
    kmemcpy(shell_index_workspace.query, args, length);
    shell_index_workspace.query[length] = '\0';
    return OK;
}

static void cmd_search_print_result(const file_index_result_t* result) {
    const file_index_entry_t* entry = &result->entry;

    video_print(entry->volume_id, 0x0B);
    video_print(":/", 0x07);
    if (entry->parent_path[0]) {
        video_print(entry->parent_path, 0x07);
        video_print("/", 0x07);
    }
    video_print(entry->name, entry->is_directory ? 0x0B : 0x07);
    video_print(entry->is_directory ? "  [DIR] " : "  [ARQ] ", 0x08);
    print_num(entry->size);
    video_print(" bytes", 0x08);
    if (result->availability == FILE_INDEX_RESULT_VOLUME_MISSING) {
        video_print("  [volume ausente ou desmontado]", 0x0C);
    } else if (result->availability == FILE_INDEX_RESULT_STALE) {
        video_print("  [resultado obsoleto]", 0x0C);
    }
    video_print("\n", 0x07);
}

static void cmd_search_print_warnings(void) {
    file_index_search_status_t* status = &shell_index_workspace.status;

    if (status->partial) video_print("Aviso: resultados parciais.\n", 0x0E);
    if (status->building) video_print("Aviso: indice em construcao.\n", 0x0E);
    if (status->cancelled) video_print("Aviso: rebuild cancelado.\n", 0x0E);
    if (status->stale) video_print("Aviso: indice desatualizado.\n", 0x0E);
    if (status->volume_missing) {
        video_print("Aviso: um ou mais volumes estao ausentes.\n", 0x0E);
    }
    if (status->result_stale) {
        video_print("Aviso: um ou mais resultados estao obsoletos.\n", 0x0E);
    }
    if (status->last_error != OK) {
        video_print("Aviso: ultimo erro do indice=", 0x0E);
        print_num((uint32_t)status->last_error);
        video_print("\n", 0x0E);
    }
}

static void cmd_search(const char* args) {
    int result = cmd_search_copy_query(args);

    if (result != OK) {
        video_print("Uso: search <termo de ate 63 caracteres>\n", 0x0C);
        return;
    }
    result = file_index_search(shell_index_workspace.query,
                               shell_index_workspace.results,
                               FILE_INDEX_MAX_RESULTS,
                               &shell_index_workspace.status);
    if (result != OK) {
        file_index_status_t status;

        video_print("Pesquisa indisponivel; codigo=", 0x0C);
        print_num((uint32_t)result);
        video_print("\n", 0x0C);
        if (file_index_get_status(&status) == OK &&
            status.state == FILE_INDEX_STATE_CANCELLED) {
            video_print("Aviso: rebuild cancelado e sem indice ativo.\n", 0x0E);
        }
        return;
    }
    for (uint32_t index = 0;
         index < shell_index_workspace.status.returned_matches; index++) {
        cmd_search_print_result(&shell_index_workspace.results[index]);
    }
    video_print("Correspondencias: ", 0x0B);
    print_num(shell_index_workspace.status.total_matches);
    video_print(" (exibidas ", 0x07);
    print_num(shell_index_workspace.status.returned_matches);
    video_print(")\n", 0x07);
    cmd_search_print_warnings();
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
    print_num((uint32_t)status.x);
    video_print(",", 0x07);
    print_num((uint32_t)status.y);
    video_print("\n  Velocidade: ", 0x07);
    print_num(status.config.speed);
    video_print("\n  Aceleracao: ", 0x07);
    video_print(status.config.acceleration_enabled ? "ON" : "OFF", 0x0B);
    video_print("\n  Botao principal: ", 0x07);
    video_print(status.config.primary_button == MOUSE_PRIMARY_RIGHT ?
                "right" : "left", 0x0B);
    video_print("\n  Botoes efetivos: ", 0x07);
    print_num(status.effective_buttons);
    video_print("  brutos: ", 0x07);
    print_num(status.raw_buttons);
    video_print("\n  Roda: ", 0x07);
    video_print(status.wheel_supported ? "DISPONIVEL" : "INDISPONIVEL",
                status.wheel_supported ? 0x0A : 0x0E);
    video_print("\n  Pacotes descartados: ", 0x07);
    print_num(status.dropped_packets);
    video_print("\n  Ultimo erro: ", 0x07);
    print_num((uint32_t)status.last_error);
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

static void cmd_clear(void) {
    video_terminal_clear();
    if (!shell_is_hosted_visible()) taskbar_draw();
}

static void cmd_ls(void) {
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        video_print("Erro: filesystem indisponivel.\n", 0x0C);
        return;
    }

    video_print("Arquivos no disco:\n", 0x0B);
    int count = fs_list_dir();
    if (count == 0) {
        video_print("  (vazio)\n", 0x08);
    }
}

static void cmd_cat(const char* filename) {
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        video_print("Erro: filesystem indisponivel.\n", 0x0C);
        return;
    }

    if (!filename || !*filename) {
        video_print("Uso: cat <arquivo>\n", 0x0C);
        return;
    }

    char name[12];
    int i = 0;
    while (filename[i] && i < 11) {
        name[i] = filename[i];
        i++;
    }
    name[i] = '\0';
    str_upper(name);

    uint8_t* buffer = (uint8_t*)kmalloc(4096);
    if (!buffer) {
        video_print("Erro: sem memoria!\n", 0x0C);
        return;
    }

    int bytes = fs_read_file(name, buffer, 4095);
    if (bytes < 0) {
        video_print("Erro: arquivo nao encontrado: ", 0x0C);
        video_print(filename, 0x0C);
        video_print("\n", 0x0C);
        kfree(buffer);
        buffer = 0;
        return;
    }

    if (bytes == 0) {
        video_print("(arquivo vazio)\n", 0x08);
        kfree(buffer);
        buffer = 0;
        return;
    }

    buffer[bytes] = '\0';
    video_print((char*)buffer, 0x07);
    video_print("\n", 0x07);
    kfree(buffer);
    buffer = 0;
}

static void cmd_echo_native(const char* text) {
    if (text && *text) {
        video_print(text, 0x07);
    }
    video_print("\n", 0x07);
}

static void cmd_edit(const char* input) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_EDITOR)) {
        video_print("Erro: Editor indisponivel.\n", 0x0C);
        return;
    }
    if (input && *input) {
        char name[13];
        uint32_t size = 0;
        uint8_t attributes = 0;
        int index = 0;

        while (input[index] && index < 12) {
            name[index] = input[index];
            index++;
        }
        name[index] = '\0';
        str_upper(name);
        if (fs_get_type() == FS_TYPE_FAT12 &&
            (fs_get_root_file_info(name, &size, &attributes) != OK ||
             (attributes & SHELL_FS_DIRECTORY_ATTRIBUTE))) {
            LOG_WARN("SHELL", "Arquivo solicitado ao Editor nao existe");
            video_print("Erro: arquivo nao encontrado; crie-o com F7 no Explorer.\n",
                        0x0C);
            return;
        }
        if (wm_is_active()) wm_set_active(0);
        shell_suspend_terminal();
        editor_run_file(name);
        return;
    }
    if (wm_is_active()) wm_set_active(0);
    shell_suspend_terminal();
    editor_run();
}

static void cmd_echo(const char* text) {
    app_launch_info_t launch;
    uint32_t pid = 0;
    int result;

    if (!text) text = "";
    result = app_loader_build_launch_info(text, &launch);
    if (result != OK) {
        LOG_WARN("SHELL", "Argumentos do echo rejeitados; usando fallback nativo");
        cmd_echo_native(text);
        return;
    }

    result = app_builtin_run_echo(&launch, &pid);
    if (result == OK) {
        shell_echo_loader_pid = pid;
        return;
    }
    if (result == ERR_UNAVAILABLE) {
        LOG_WARN("SHELL", "Echo ring 3 indisponivel; usando fallback nativo");
        cmd_echo_native(text);
        return;
    }

    video_print("Erro: echo ring 3 nao iniciou (codigo ", 0x0C);
    print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static void cmd_mem_native(void) {
    video_print("Memoria:\n", 0x0B);
    video_print("  Total: ", 0x07);
    print_num(memory_get_total() / 1024);
    video_print(" KB\n", 0x07);

    video_print("  Livre: ", 0x07);
    print_num(memory_get_free() / 1024);
    video_print(" KB\n", 0x07);

    video_print("  Usada: ", 0x07);
    print_num(memory_get_used() / 1024);
    video_print(" KB\n", 0x07);
}

static void cmd_procs(void) {
    video_print("Processos ativos:\n", 0x0B);

    extern process_t processes[];
    extern uint32_t process_count;

    const char* state_names[] = {"UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE"};

    for (int i = 0; i < 64; i++) {
        if (processes[i].state != 0) {
            video_print("  PID ", 0x07);
            print_num(processes[i].pid);
            video_print("  ", 0x07);
            video_print(processes[i].name, 0x0B);
            video_print("  ", 0x07);
            video_print(state_names[processes[i].state], 0x08);
            video_print("\n", 0x07);
        }
    }

    video_print("Total: ", 0x07);
    print_num(process_count);
    video_print(" processos\n", 0x07);
}

static void cmd_threads(void) {
    video_print("Threads ativas:\n", 0x0B);

    const char* state_names[] = {"UNUSED", "RUNNING", "BLOCKED", "FINISHED"};
    uint32_t count = thread_get_count();

    for (uint32_t i = 0; i < 32; i++) {
        thread_t* t = thread_get_by_id(i + 1);
        if (t) {
            video_print("  TID ", 0x07);
            print_num(t->id);
            video_print("  ", 0x07);
            video_print(t->name, 0x0B);
            video_print("  ", 0x07);
            video_print(state_names[t->state], 0x08);
            video_print("\n", 0x07);
        }
    }

    video_print("Total: ", 0x07);
    print_num(count);
    video_print(" threads\n", 0x07);
}

static void cmd_uptime_native(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 50;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;

    video_print("Uptime: ", 0x0B);
    print_num(hours);
    video_print("h ", 0x07);
    print_num(minutes % 60);
    video_print("m ", 0x07);
    print_num(seconds % 60);
    video_print("s\n", 0x07);
}

static void cmd_run_migrated_builtin(shell_builtin_app_t app) {
    uint32_t pid = 0;
    int result;

    if (app == SHELL_BUILTIN_APP_UPTIME) {
        result = app_builtin_run_uptime(&pid);
    } else if (app == SHELL_BUILTIN_APP_MEM) {
        result = app_builtin_run_mem(&pid);
    } else {
        LOG_ERROR("SHELL", "Comando migrado sem aplicativo definido");
        return;
    }
    if (result == OK) {
        shell_builtin_loader_pid = pid;
        shell_builtin_loader_app = app;
        return;
    }
    if (result == ERR_UNAVAILABLE) {
        LOG_WARN("SHELL", "Aplicativo ring 3 indisponivel; usando fallback nativo");
        if (app == SHELL_BUILTIN_APP_UPTIME) {
            cmd_uptime_native();
        } else {
            cmd_mem_native();
        }
        return;
    }

    LOG_ERROR("SHELL", "Aplicativo ring 3 nao iniciou");
    video_print("Erro: ", 0x0C);
    video_print(shell_builtin_app_name(app), 0x0C);
    video_print(" ring 3 nao iniciou (codigo ", 0x0C);
    print_num((uint32_t)result);
    video_print(").\n", 0x0C);
}

static void cmd_mem(void) {
    cmd_run_migrated_builtin(SHELL_BUILTIN_APP_MEM);
}

static void cmd_uptime(void) {
    cmd_run_migrated_builtin(SHELL_BUILTIN_APP_UPTIME);
}

static void cmd_beep(const char* args) {
    if (!args || !*args) {
        speaker_beep(800, 200);
        video_print("Beep!\n", 0x0A);
        return;
    }

    uint32_t freq = parse_number(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;

    uint32_t dur = parse_number(args);
    if (dur == 0) dur = 200;
    if (freq == 0) freq = 800;

    speaker_beep(freq, dur);
    video_print("Beep! (", 0x0A);
    print_num(freq);
    video_print(" Hz, ", 0x0A);
    print_num(dur);
    video_print(" ms)\n", 0x0A);
}

static void cmd_melody(void) {
    video_print("Tocando melodia...\n", 0x0A);

    uint32_t freqs[] = {523, 587, 659, 698, 784, 880, 988, 1047};
    uint32_t durs[] =  {200, 200, 200, 200, 200, 200, 200, 400};

    speaker_play_melody(freqs, durs, 8);
    video_print("Melodia concluida!\n", 0x0A);
}

static void cmd_reboot(void) {
    video_print("Reiniciando...\n", 0x0E);
    asm volatile("cli");
    asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    for (;;) asm volatile("hlt");
}

static void cmd_shutdown(const char* args) {
    if (args && *args) {
        LOG_WARN("SHELL", "Uso invalido de shutdown");
        video_print("Uso: shutdown\n", 0x0C);
        return;
    }
    video_print("Desligando...\n", 0x0E);
    power_shutdown();
}

void shell_init(void) {
    shell_reset_input();
    shell_prompt_visible = 0;
    shell_hosted_visible = 0;
    kmemset(&shell_kmetrics_baseline, 0, sizeof(shell_kmetrics_baseline));
}

static void cmd_threadtest(void) {
    int result = thread_run_self_test();

    video_print("Teste de threads: ", 0x0B);
    video_print(result == OK ? "OK\n" : "ERRO\n", result == OK ? 0x0A : 0x0C);
}

void shell_print_prompt(void) {
    shell_resume_terminal();
    if (shell_prompt_visible) return;
    video_print(SHELL_PROMPT, 0x0A);
    shell_prompt_visible = 1;
}

static int shell_should_show_prompt(void) {
    if (shell_is_hosted_visible()) {
        return !shell_waiting_user_test && !app_loader_is_foreground_active();
    }

    /* Apps que retornam ao Desktop ja redesenham a cena antes de voltar. */
    if (desktop_is_active()) return 0;
    if (fm_is_running()) return 0;
    if (taskmgr_is_open() || taskmgr_is_gui_open()) return 0;
    if (settings_is_open() || wm_is_active() || guitest_is_active()) return 0;
    if (shell_waiting_user_test) return 0;
    if (app_loader_is_foreground_active()) return 0;
    return 1;
}

void shell_update_hosted_terminal(void) {
    shell_present_hosted_progress();
}

static void process_input(void) {
    shell_prompt_visible = 0;
    video_print("\n", 0x07);

    if (input_pos == 0) {
        shell_print_prompt();
        return;
    }

    input_buffer[input_pos] = '\0';
    shell_history_record(input_buffer);
    shell_process_command(input_buffer);

    shell_reset_input();
    if (shell_should_show_prompt()) {
        shell_print_prompt();
    }
}

static int shell_handle_terminal_scroll_key(uint8_t scancode) {
    switch (scancode) {
        case SHELL_SCANCODE_UP:
            return video_terminal_scroll(1);
        case SHELL_SCANCODE_DOWN:
            return video_terminal_scroll(-1);
        case SHELL_SCANCODE_PAGE_UP:
            return video_terminal_scroll(SHELL_SCROLL_PAGE_LINES);
        case SHELL_SCANCODE_PAGE_DOWN:
            return video_terminal_scroll(-SHELL_SCROLL_PAGE_LINES);
        case SHELL_SCANCODE_HOME:
            video_terminal_scroll_home();
            return 1;
        case SHELL_SCANCODE_END:
            video_terminal_scroll_end();
            return 1;
        default:
            return 0;
    }
}

static void shell_handle_terminal_key(uint8_t scancode) {
    if (scancode == SHELL_SCANCODE_LEFT_SHIFT) {
        shell_shift_mask |= SHELL_SHIFT_LEFT_MASK;
        return;
    }
    if (scancode == SHELL_SCANCODE_RIGHT_SHIFT) {
        shell_shift_mask |= SHELL_SHIFT_RIGHT_MASK;
        return;
    }
    if (scancode == SHELL_SCANCODE_LEFT_SHIFT_RELEASE) {
        shell_shift_mask &= (uint8_t)~SHELL_SHIFT_LEFT_MASK;
        return;
    }
    if (scancode == SHELL_SCANCODE_RIGHT_SHIFT_RELEASE) {
        shell_shift_mask &= (uint8_t)~SHELL_SHIFT_RIGHT_MASK;
        return;
    }
    if (shell_waiting_user_test) return;

    shell_resume_terminal();

    if (scancode == SHELL_SCANCODE_EXTENDED) {
        shell_extended_scancode = 1;
        return;
    }

    if (scancode & 0x80) {
        shell_extended_scancode = 0;
        return;
    }

    if (shell_extended_scancode) {
        shell_extended_scancode = 0;
        if (scancode == SHELL_SCANCODE_UP ||
            scancode == SHELL_SCANCODE_DOWN) {
            if (shell_shift_mask) {
                (void)shell_handle_terminal_scroll_key(scancode);
            } else {
                shell_history_navigate(scancode == SHELL_SCANCODE_UP ? -1 : 1);
            }
            return;
        }
        if (shell_handle_terminal_scroll_key(scancode)) return;
    }

    static const char scancode_table[128] = {
        0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
        '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
        0,  '\\','z','x','c','v','b','n','m',',','.',';',0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    static const char scancode_shift_table[128] = {
        0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
        '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
        0,  'A','S','D','F','G','H','J','K','L',':','"','~',
        0,  '|','Z','X','C','V','B','N','M','<','>',':',0,
        '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    /* QEMU pode encaminhar a barra ABNT2 por dois scancodes fisicos. */
    char c = (scancode == SHELL_SCANCODE_ISO_SLASH ||
              scancode == SHELL_SCANCODE_ABNT2_SLASH) ?
             (shell_shift_mask ? '?' : '/') :
             (shell_shift_mask ? scancode_shift_table[scancode] :
                                 scancode_table[scancode]);

    if (scancode == 0x0E) {
        shell_history_detach_for_edit();
        shell_return_to_terminal_tail();
        if (input_pos > 0) {
            input_pos--;
            input_buffer[input_pos] = '\0';
            video_backspace();
        }
        return;
    }

    if (scancode == 0x1C) {
        shell_return_to_terminal_tail();
        process_input();
        return;
    }

    /* Teclas de controle podem chegar antes do primeiro caractere visivel
       quando o QEMU entrega o foco. Elas nao devem contaminar o comando. */
    if (c >= ' ' && c <= '~' && input_pos < SHELL_BUFFER_SIZE - 1) {
        shell_history_detach_for_edit();
        shell_return_to_terminal_tail();
        input_buffer[input_pos++] = c;
        input_buffer[input_pos] = '\0';
        video_put_char(c, 0x07);
    }

}

void shell_handle_key(uint8_t scancode) {
    int config_result = taskbar_handle_config_key(scancode);
    if (config_result) {
        shell_extended_scancode = 0;
        if (config_result == 9) {
            shell_redraw_after_overlay_close();
        }
        return;
    }

    int tb_result = taskbar_handle_key(scancode);
    if (tb_result) {
        shell_extended_scancode = 0;
        if (tb_result == 2) {
            shell_handle_app_request(IPC_APP_OPEN_SHELL);
        } else if (tb_result == 3) {
            shell_handle_app_request(IPC_APP_OPEN_EXPLORER);
        } else if (tb_result == 4) {
            shell_handle_app_request(IPC_APP_OPEN_TASKMANAGER_GUI);
        } else if (tb_result == 5) {
            cmd_reboot();
        } else if (tb_result == 6) {
            cmd_shutdown("");
        } else if (tb_result == 7) {
            shell_handle_app_request(IPC_APP_OPEN_DESKTOP);
        } else if (tb_result == 8) {
            shell_handle_app_request(IPC_APP_OPEN_SETTINGS);
        } else if (tb_result == TB_ACTION_UPDATER) {
            shell_handle_app_request(IPC_APP_OPEN_UPDATER);
        } else if (tb_result == TB_ACTION_APPSTORE) {
            shell_handle_app_request(IPC_APP_OPEN_APP_STORE);
        } else if (tb_result == 9) {
            shell_redraw_after_overlay_close();
        }
        return;
    }

    if (guitest_is_active()) {
        guitest_handle_key(scancode);
        return;
    }

    if (wm_is_active()) {
        if (wm_handle_key(scancode) == WM_RESULT_EXIT) {
            wm_set_active(0);
            shell_reset_input();
            video_terminal_begin();
            shell_print_prompt();
            taskbar_draw();
        }
        return;
    }

    if (taskmgr_is_gui_open()) {
        taskmgr_gui_handle_key(scancode);
        return;
    }

    if (settings_is_open()) {
        settings_handle_key(scancode);
        return;
    }

    if (updater_is_open()) {
        updater_handle_key(scancode);
        return;
    }

    if (appstore_is_open()) {
        appstore_handle_key(scancode);
        return;
    }

    if (desktop_is_active()) {
        int result = desktop_handle_key(scancode);
        if (result == -1) {
            desktop_set_active(0);
            shell_reset_input();
            video_terminal_begin();
            shell_print_prompt();
            taskbar_draw();
            return;
        }
        if (result == 2) {
            shell_handle_app_request(IPC_APP_OPEN_EXPLORER);
            return;
        }
        if (result == 3) {
            shell_handle_app_request(IPC_APP_OPEN_TASKMANAGER_GUI);
            return;
        }
        return;
    }

    if (taskmgr_is_open()) {
        taskmgr_handle_key(scancode);
        return;
    }

    shell_handle_terminal_key(scancode);
}

int shell_process_command(const char* input) {
    static char cmd[32];
    int i = 0;

    if (!input) {
        LOG_ERROR("SHELL", "Comando nulo recebido");
        return ERR_NULL;
    }

    shell_resume_terminal();

    while (*input == ' ' || *input == '\t' || *input == '\r' ||
           *input == '\n' || *input == 27) {
        input++;
    }

    if (!*input) return 0;

    while (*input && *input != ' ' && *input != '\t' && i < 31) {
        if ((uint8_t)*input >= ' ' && (uint8_t)*input <= '~') {
            cmd[i++] = *input;
        }
        input++;
    }
    cmd[i] = '\0';

    while (*input == ' ' || *input == '\t') input++;

    if (kstrcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (kstrcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (kstrcmp(cmd, "ls") == 0) {
        cmd_ls();
    } else if (kstrcmp(cmd, "cat") == 0) {
        cmd_cat(input);
    } else if (kstrcmp(cmd, "echo") == 0) {
        cmd_echo(input);
    } else if (kstrcmp(cmd, "mem") == 0) {
        cmd_mem();
    } else if (kstrcmp(cmd, "procs") == 0) {
        cmd_procs();
    } else if (kstrcmp(cmd, "threads") == 0) {
        cmd_threads();
    } else if (kstrcmp(cmd, "threadtest") == 0) {
        cmd_threadtest();
    } else if (kstrcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (kstrcmp(cmd, "health") == 0) {
        cmd_health(input);
    } else if (kstrcmp(cmd, "log") == 0) {
        cmd_log(input);
    } else if (kstrcmp(cmd, "devices") == 0) {
        cmd_devices(input);
    } else if (kstrcmp(cmd, "device-info") == 0) {
        cmd_device_info(input);
    } else if (kstrcmp(cmd, "device-scan") == 0) {
        cmd_device_scan(input);
    } else if (kstrcmp(cmd, "net") == 0) {
        cmd_net(input);
    } else if (kstrcmp(cmd, "ping") == 0) {
        cmd_ping(input);
    } else if (kstrcmp(cmd, "nslookup") == 0) {
        cmd_nslookup(input);
    } else if (kstrcmp(cmd, "http") == 0) {
        cmd_http(input);
    } else if (kstrcmp(cmd, "acpi") == 0) {
        cmd_acpi(input);
    } else if (kstrcmp(cmd, "power") == 0) {
        cmd_power(input);
    } else if (kstrcmp(cmd, "kmetrics") == 0) {
        cmd_kmetrics(input);
    } else if (kstrcmp(cmd, "memcheck") == 0) {
        cmd_memcheck(input);
    } else if (kstrcmp(cmd, "schedcheck") == 0) {
        cmd_schedcheck(input);
    } else if (kstrcmp(cmd, "q2check") == 0) {
        cmd_q2check();
    } else if (kstrcmp(cmd, "regcheck") == 0) {
        cmd_regcheck(input);
    } else if (kstrcmp(cmd, "appcheck") == 0) {
        cmd_appcheck();
    } else if (kstrcmp(cmd, "pkg") == 0) {
        cmd_pkg(input);
    } else if (kstrcmp(cmd, "store") == 0) {
        cmd_store(input);
    } else if (kstrcmp(cmd, "update") == 0) {
        cmd_update(input);
    } else if (kstrcmp(cmd, "pkgcheck") == 0) {
        cmd_pkgcheck();
    } else if (kstrcmp(cmd, "app") == 0) {
        cmd_app(input);
    } else if (kstrcmp(cmd, "usertest") == 0) {
        cmd_usertest(input);
    } else if (kstrcmp(cmd, "beep") == 0) {
        cmd_beep(input);
    } else if (kstrcmp(cmd, "melody") == 0) {
        cmd_melody();
    } else if (kstrcmp(cmd, "desktop") == 0) {
        if (!desktop_is_active()) {
            if (wm_is_active()) wm_set_active(0);
            shell_suspend_terminal();
            desktop_set_active(1);
            desktop_draw();
        }
    } else if (kstrcmp(cmd, "guimode") == 0) {
        cmd_guimode(input);
    } else if (kstrcmp(cmd, "display") == 0) {
        cmd_display(input);
    } else if (kstrcmp(cmd, "explorer") == 0) {
        if (shell_prepare_filemanager() != OK) {
            video_print("Erro: Explorer indisponivel.\n", 0x0C);
        } else {
            shell_suspend_terminal_for_scene();
            fm_run();
        }
    } else if (kstrcmp(cmd, "reboot") == 0) {
        cmd_reboot();
    } else if (kstrcmp(cmd, "shutdown") == 0) {
        cmd_shutdown(input);
    } else if (kstrcmp(cmd, "guitest") == 0) {
        cmd_guitest(input);
    } else if (kstrcmp(cmd, "taskmgr") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
            shell_suspend_terminal_for_scene();
            if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
                if (taskmgr_open_gui() != OK) {
                    wm_set_active(0);
                    shell_suspend_terminal();
                    LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                    taskmgr_run();
                }
            } else {
                taskmgr_run();
            }
        } else {
            video_print("Erro: Task Manager indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "taskcfg") == 0) {
        taskbar_draw_config_menu();
    } else if (kstrcmp(cmd, "settings") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
            shell_suspend_terminal_for_scene();
            settings_open();
        } else {
            video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "updater") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_SYSTEM_UPDATER)) {
            shell_suspend_terminal_for_scene();
            if (updater_open() != OK) {
                video_print("Erro: System Updater indisponivel.\n", 0x0C);
            }
        } else {
            video_print("Erro: System Updater indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "wm") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_WM)) {
            if (desktop_get_mode() == DESKTOP_MODE_CLASSIC && wm_is_active()) {
                wm_set_active(0);
            }
            shell_suspend_terminal();
            desktop_set_active(0);
            wm_set_active(1);
        } else {
            video_print("Erro: Window Manager indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "play") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
            !recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER) ||
            !recovery_is_available(RECOVERY_COMPONENT_AC97)) {
            video_print("Erro: audio ou filesystem indisponivel.\n", 0x0C);
        } else if (!*input) {
            video_print("Uso: play <arquivo.wav>\n", 0x0C);
        } else {
            char name[13];
            int n = 0;
            while (input[n] && n < 12) { name[n] = input[n]; n++; }
            name[n] = '\0';
            str_upper(name);
            video_print("Tocando: ", 0x0A);
            video_print(name, 0x0A);
            video_print("\n", 0x0A);
            int play_result = mp_play_audio(name);
            if (play_result != OK) {
                video_print("Erro: nao foi possivel reproduzir o audio.\n", 0x0C);
            }
        }
    } else if (kstrcmp(cmd, "view") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
            !recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER) ||
            !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
            video_print("Erro: imagem ou filesystem indisponivel.\n", 0x0C);
        } else if (!*input) {
            video_print("Uso: view <arquivo.bmp>\n", 0x0C);
        } else {
            char name[13];
            int n = 0;
            while (input[n] && n < 12) { name[n] = input[n]; n++; }
            name[n] = '\0';
            str_upper(name);
            if (wm_is_active()) wm_set_active(0);
            shell_suspend_terminal();
            int view_result = mp_play_image(name);
            if (view_result != OK) {
                video_print("Erro: nao foi possivel exibir a imagem.\n", 0x0C);
            }
        }
    } else if (kstrcmp(cmd, "icons") == 0) {
        cmd_icons();
    } else if (kstrcmp(cmd, "stop") == 0) {
        mp_stop();
        video_print("Player parado.\n", 0x0A);
    } else if (kstrcmp(cmd, "compress") == 0) {
        if (compress_is_enabled()) {
            compress_disable();
            video_print("Compressao de RAM DESATIVADA\n", 0x0C);
        } else {
            compress_enable();
            video_print("Compressao de RAM ATIVADA\n", 0x0A);
        }
    } else if (kstrcmp(cmd, "stats") == 0) {
        compress_print_stats();
    } else if (kstrcmp(cmd, "edit") == 0) {
        cmd_edit(input);
    } else if (kstrcmp(cmd, "storage") == 0) {
        cmd_storage(input);
    } else if (kstrcmp(cmd, "index") == 0) {
        cmd_index(input);
    } else if (kstrcmp(cmd, "search") == 0) {
        cmd_search(input);
    } else if (kstrcmp(cmd, "mouse") == 0) {
        cmd_mouse(input);
    } else {
        video_print("Comando nao encontrado: ", 0x0C);
        video_print(cmd, 0x0C);
        video_print("\n", 0x0C);
        video_print("Digite 'help' para ver os comandos.\n", 0x08);
    }

    return 0;
}
