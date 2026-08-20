#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_dispatch.h"
#include "apps/shell_job.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "fs/file_index.h"
#include "core/memory.h"
#include "core/timer.h"
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
#include "core/log.h"
#include "drivers/mouse.h"
#include "ui/gui.h"
#include "apps/guitest.h"
#include "core/recovery.h"
#include "core/device_manager.h"
#include "core/usb_manager.h"
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
#define SHELL_LOG_TAIL_MAXIMUM 16U
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

static shell_index_workspace_t shell_index_workspace;

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
    if (disk->kind == STORAGE_DISK_USB_MSC) {
        video_print("\n  Provedor: USB MSC somente-leitura\n", 0x07);
    } else {
        video_print("\n  Provedor: ATA  Slot/canal: ", 0x07);
        shell_command_print_num(disk->slot);
        video_print(disk->channel ? " secundario " : " primario ", 0x07);
        video_print(disk->slave ? "slave\n" : "master\n", 0x07);
    }
    video_print("  Bytes/setor: ", 0x07);
    shell_command_print_num(disk->sector_size);
    video_print("  Acesso: ", 0x07);
    video_print(disk->read_only ? "READ-ONLY\n" : "READ-WRITE\n", 0x07);
    video_print("  Setores: ", 0x07);
    shell_command_print_num(disk->sector_count);
    video_print("  Leituras: ", 0x07);
    shell_command_print_num(disk->read_ops);
    video_print("  Escritas: ", 0x07);
    shell_command_print_num(disk->write_ops);
    video_print("  Erro: ", 0x07);
    shell_command_print_num((uint32_t)disk->last_error);
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
    shell_command_print_num(volume->start_lba);
    video_print("  Setores: ", 0x07);
    shell_command_print_num(volume->sector_count);
    video_print("  Bytes/setor: ", 0x07);
    shell_command_print_num(volume->bytes_per_sector);
    video_print("  Setores/cluster: ", 0x07);
    shell_command_print_num(volume->sectors_per_cluster);
    video_print("  Particao: ", 0x07);
    shell_command_print_num(volume->partition_index);
    video_print("  Tipo: ", 0x07);
    shell_command_print_hex(volume->partition_type, 2U);
    video_print("\n  Label: ", 0x07);
    video_print(volume->label[0] ? volume->label : "(sem label)", 0x07);
    video_print("  Geracao: ", 0x07);
    shell_command_print_num(volume->generation);
    video_print("  Erro: ", 0x07);
    shell_command_print_num((uint32_t)volume->last_error);
    video_print("\n", 0x07);
}

static void cmd_storage_list(void) {
    storage_status_t status;

    if (storage_get_status(&status) != OK || !status.initialized) {
        video_print("Storage indisponivel.\n", 0x0C);
        return;
    }
    video_print("Storage: discos=", 0x0B);
    shell_command_print_num(status.disk_count);
    video_print(" volumes=", 0x07);
    shell_command_print_num(status.volume_count);
    video_print(" montados=", 0x07);
    shell_command_print_num(status.mounted_count);
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
            shell_command_print_num(status.disk_count);
            video_print(" volumes=", 0x07);
            shell_command_print_num(status.volume_count);
            video_print(" montados=", 0x07);
            shell_command_print_num(status.mounted_count);
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
        shell_command_print_num((uint32_t)result);
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
    shell_command_print_num(status.active_entries);
    video_print(" candidato=", 0x07);
    shell_command_print_num(status.candidate_entries);
    video_print(" fontes=", 0x07);
    shell_command_print_num(status.source_count);
    video_print(" concluidas=", 0x07);
    shell_command_print_num(status.sources_completed);
    video_print("\nDiretorios=", 0x07);
    shell_command_print_num(status.directories_scanned);
    video_print(" passos=", 0x07);
    shell_command_print_num(status.scan_steps);
    video_print(" memoria=", 0x07);
    shell_command_print_num(status.memory_bytes);
    video_print(" evento=", 0x07);
    shell_command_print_num(status.event_generation);
    video_print(" erro=", 0x07);
    shell_command_print_num((uint32_t)status.last_error);
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
        shell_command_print_num((uint32_t)result);
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
    shell_command_print_num(entry->size);
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
        shell_command_print_num((uint32_t)status->last_error);
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
        shell_command_print_num((uint32_t)result);
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
    shell_command_print_num(shell_index_workspace.status.total_matches);
    video_print(" (exibidas ", 0x07);
    shell_command_print_num(shell_index_workspace.status.returned_matches);
    video_print(")\n", 0x07);
    cmd_search_print_warnings();
}

static uint32_t shell_index_operation_generation;

static shell_job_step_result_t shell_index_job_step(
    shell_job_context_t* context) {
    file_index_status_t status;

    if (!context || file_index_get_status(&status) != OK) {
        if (context) context->last_error = ERR_STATE;
        LOG_ERROR("SHELL", "Falha ao consultar job do indice");
        return SHELL_JOB_STEP_FAILED;
    }
    if (!shell_job_generation_matches(context->generation)) {
        context->stale_events++;
        context->last_error = ERR_STATE;
        LOG_WARN("SHELL", "Resultado do indice pertence a geracao antiga");
        return SHELL_JOB_STEP_FAILED;
    }
    if (shell_index_operation_generation &&
        status.operation_generation != shell_index_operation_generation) {
        context->stale_events++;
        context->last_error = ERR_STATE;
        LOG_WARN("SHELL", "Reconstrucao do indice mudou de geracao");
        return SHELL_JOB_STEP_FAILED;
    }
    shell_job_set_phase(context, file_index_state_name(status.state));
    shell_job_set_progress(context, status.scan_steps,
                           status.candidate_entries);
    if (context->cancel_requested) {
        context->last_error = file_index_cancel();
        return context->last_error == OK ? SHELL_JOB_STEP_CANCELLED :
                                           SHELL_JOB_STEP_FAILED;
    }
    if (status.state == FILE_INDEX_STATE_BUILDING) {
        return SHELL_JOB_STEP_PENDING;
    }
    if (status.state == FILE_INDEX_STATE_READY ||
        status.state == FILE_INDEX_STATE_EMPTY) {
        context->last_error = OK;
        return SHELL_JOB_STEP_COMPLETE;
    }
    context->last_error = status.last_error == OK ? ERR_STATE :
                                                   status.last_error;
    return status.state == FILE_INDEX_STATE_CANCELLED ?
           SHELL_JOB_STEP_CANCELLED : SHELL_JOB_STEP_FAILED;
}

static int shell_index_job_cancel(shell_job_context_t* context) {
    (void)context;
    return file_index_cancel();
}

static shell_job_step_result_t shell_index_job_drain(
    shell_job_context_t* context) {
    file_index_status_t status;

    (void)context;
    if (file_index_get_status(&status) != OK) return SHELL_JOB_STEP_FAILED;
    return status.state == FILE_INDEX_STATE_BUILDING ?
           SHELL_JOB_STEP_PENDING : SHELL_JOB_STEP_COMPLETE;
}

static void shell_index_job_finish(shell_job_context_t* context,
                                   shell_job_state_t state, int result) {
    (void)context;
    if (state == SHELL_JOB_STATE_SUCCEEDED) {
        video_print("Indice reconstruido com sucesso.\n", 0x0A);
    } else if (state == SHELL_JOB_STATE_CANCELLED) {
        video_print("Rebuild do indice cancelado; indice ativo preservado.\n",
                    0x0E);
    } else {
        video_print("Rebuild do indice falhou; codigo=", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print("\n", 0x0C);
    }
}

static const shell_job_definition_t shell_index_job_definition = {
    "index", SHELL_JOB_KIND_INDEX, shell_index_job_step,
    shell_index_job_cancel, shell_index_job_finish, shell_index_job_drain
};

int shell_storage_start_job(const char* arguments) {
    file_index_status_t status;
    int result;

    if (!shell_command_args_equal(arguments, "rebuild")) return 0;
    result = file_index_rebuild();
    if (result != OK) {
        video_print("Operacao do indice falhou; codigo=", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print("\n", 0x0C);
        return 1;
    }
    if (file_index_get_status(&status) != OK) {
        file_index_cancel();
        video_print("Status do indice indisponivel.\n", 0x0C);
        return 1;
    }
    shell_index_operation_generation = status.operation_generation;
    result = shell_job_start(&shell_index_job_definition, arguments);
    if (result != OK) {
        file_index_cancel();
        video_print("Job do indice recusado; codigo=", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print("\n", 0x0C);
    }
    return 1;
}

#define SHELL_STORAGE_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

SHELL_STORAGE_WRAP_ARGS(shell_dispatch_cmd_storage, cmd_storage)
void shell_dispatch_cmd_index(const char* arguments) {
    if (shell_storage_start_job(arguments)) return;
    cmd_index(arguments);
}
SHELL_STORAGE_WRAP_ARGS(shell_dispatch_cmd_search, cmd_search)

#undef SHELL_STORAGE_WRAP_ARGS
