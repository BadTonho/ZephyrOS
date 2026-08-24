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
#include "core/crypto.h"
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
#include "core/update_system.h"
#include "core/update_remote.h"
#include "core/update_remote_runtime.h"
#include "core/update_remote_config.h"
#include "core/update_runtime.h"
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
    char third[UPDATE_REMOTE_TAG_SIZE + 1U];
    char extra[16];
    update_remote_status_t remote_status;
    update_remote_options_t remote_options;
    update_remote_result_t remote_result;
    update_runtime_status_t runtime_status;
    update_remote_runtime_status_t runtime_remote_status;
    update_remote_runtime_result_t runtime_remote_result;
    update_runtime_cache_t runtime_cache;
    update_system_verification_t system_verification;
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


static shell_update_workspace_t shell_update_workspace;
static shell_store_workspace_t shell_store_workspace;
static uint32_t shell_packages_job_generation;
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

static int cmd_pkg_has_trailing_token(const char* cursor) {
    if (!cursor) return 0;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    return *cursor != '\0';
}

static void cmd_update_print_version(const char* label,
                                     const update_version_t* version,
                                     uint32_t epoch) {
    video_print(label, 0x07);
    shell_command_print_num(version->major);
    video_print(".", 0x07);
    shell_command_print_num(version->minor);
    video_print(".", 0x07);
    shell_command_print_num(version->patch);
    video_print(" epoch=", 0x08);
    shell_command_print_num(epoch);
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
        shell_command_print_num((uint32_t)verification.reason);
        video_print(", erro=", 0x08);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        if (verification.entry_count > 0U) {
            cmd_update_print_version(
                "  Base autenticada: ", &verification.base_version,
                verification.base_epoch);
            cmd_update_print_version(
                "  Alvo autenticado: ", &verification.target_version,
                verification.target_epoch);
            video_print("  Arquivos autenticados: ", 0x07);
            shell_command_print_num(verification.entry_count);
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
    shell_command_print_num(verification.entry_count);
    video_print("  bytes=", 0x08);
    shell_command_print_num(verification.total_size);
    video_print("\n  Motivo: NONE\n", 0x07);
    video_print("Nenhuma gravacao foi realizada.\n", 0x0A);
}

static int cmd_update_cancel_check(void* context) {
    ipc_msg_t message;

    (void)context;
    if (shell_job_is_active()) {
        if (!shell_job_generation_matches(shell_packages_job_generation)) {
            LOG_WARN("SHELL", "Cancelamento de pacote pertence a geracao antiga");
            return 1;
        }
        shell_job_pump_events();
        return shell_job_cancel_requested();
    }
    shell_hosted_present_progress();
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
    shell_command_print_num(action->entry_count);
    video_print(" processados=", 0x08);
    shell_command_print_num(action->completed_entries);
    video_print("\n", 0x07);
}

static void cmd_update_print_version_inline(
    const update_version_t* version, uint32_t epoch) {
    shell_command_print_num(version->major);
    video_print(".", 0x07);
    shell_command_print_num(version->minor);
    video_print(".", 0x07);
    shell_command_print_num(version->patch);
    video_print("/e", 0x08);
    shell_command_print_num(epoch);
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
    shell_command_print_num(entry->sequence);
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
    shell_command_print_num(entry->completed_entries);
    video_print("/", 0x08);
    shell_command_print_num(entry->entry_count);
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
        shell_command_print_num(history_count);
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
    uint32_t entries = shell_command_parse_number(value);
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
    shell_command_print_num(entries);
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
    shell_command_print_num(candidate->generation);
    video_print("  bytes=", 0x08);
    shell_command_print_num(candidate->package_size);
    video_print("\n  Caminho: ", 0x07);
    video_print(candidate->package_path, 0x07);
    video_print("\n", 0x07);
}

static void cmd_update_print_hash(const uint8_t* hash) {
    if (!hash) return;
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        shell_command_print_hex(hash[index], 2U);
    }
}

static void cmd_update_print_release_result(
    const update_remote_result_t* result) {
    update_remote_status_t status;

    if (!result) return;
    if (result->release.tag[0]) {
        video_print("Release selecionada:\n  Tag: ", 0x0B);
        video_print(result->release.tag, 0x0A);
        video_print("\n  Release: ", 0x07);
        video_print(result->release.release_id, 0x07);
        video_print(" - ", 0x08);
        video_print(result->release.release_name, 0x07);
        video_print("\n  Commit: ", 0x08);
        video_print(result->release.source_commit, 0x07);
        video_print("\n  Tamanho: ", 0x07);
        shell_command_print_num(result->release.package_size);
        video_print(" bytes\n  SHA-256: ", 0x08);
        cmd_update_print_hash(result->release.package_hash);
        video_print("\n  Manifesto SHA-256: ", 0x08);
        cmd_update_print_hash(result->release.manifest_hash);
        if (result->release.api_metadata_present) {
            video_print("\n  API metadata SHA-256: ", 0x08);
            cmd_update_print_hash(result->release.api_metadata_hash);
        }
        video_print("\n", 0x07);
    }
    cmd_update_print_remote_candidate(&result->candidate);
    if (update_remote_get_status(&status) == OK) {
        video_print("  Cache: ", 0x07);
        video_print(update_remote_store_name(status.cache_store),
                    status.cache_store == UPDATE_REMOTE_STORE_INVALID ?
                    0x0C : 0x0A);
        if (status.package_cached) {
            video_print(" alias=", 0x08);
            video_print(status.cached_alias, 0x0B);
        }
        video_print("\n", 0x07);
    }
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
    shell_command_print_num(status->bytes_received);
    video_print("/", 0x08);
    shell_command_print_num(status->total_bytes);
    video_print(" retry=", 0x08);
    shell_command_print_num(status->retry_count);
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
    shell_command_print_num(result->bytes_received);
    video_print(" retry=", 0x08);
    shell_command_print_num(result->retry_count);
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

static void cmd_update_github(const char* operation, const char* tag,
                              int confirmed) {
    update_remote_options_t* options =
        &shell_update_workspace.remote_options;
    update_remote_result_t* result =
        &shell_update_workspace.remote_result;
    int operation_result;

    kmemset(options, 0, sizeof(*options));
    options->dry_run = confirmed ? 0U : 1U;
    options->cancel_check = cmd_update_cancel_check;
    if (confirmed) {
        video_print("Baixando Release por tag; Esc/F12 cancela...\n",
                    0x0E);
        operation_result = update_remote_release_fetch(tag, options, result);
    } else {
        video_print("Consultando Release por tag; Esc/F12 cancela...\n",
                    0x07);
        operation_result = update_remote_release_check(tag, options, result);
    }
    video_print("Tag solicitada: ", 0x07);
    video_print(tag, 0x0A);
    video_print("\n", 0x07);
    video_print("Resultado EP6.2: ", 0x07);
    video_print(update_remote_reason_name(result->reason),
                operation_result == OK ? 0x0A : 0x0C);
    video_print(" http=", 0x08);
    shell_command_print_num(result->http_status);
    video_print(" bytes=", 0x08);
    shell_command_print_num(result->bytes_received);
    video_print(" tls=", 0x08);
    video_print(result->secure && result->tls_verified ? "VERIFIED" :
                "UNAVAILABLE",
                result->secure && result->tls_verified ? 0x0A : 0x0E);
    video_print(" redirects=", 0x08);
    shell_command_print_num(result->redirect_count);
    video_print("\n", 0x07);
    cmd_update_print_release_result(result);
    if (operation_result != OK) {
        if (result->cache_preserved) {
            video_print("O cache remoto anterior foi preservado.\n",
                        0x0A);
        }
        return;
    }
    if (!confirmed) {
        video_print("Preflight EP6.2 aprovado. Nenhuma gravacao realizada.\n",
                    0x0A);
        if (kstrcmp(operation, "check") == 0) return;
        video_print("Para confirmar: update github fetch --tag ", 0x0E);
        video_print(tag, 0x0E);
        video_print(" --confirm\n", 0x0E);
        return;
    }
    video_print("Release autenticada no cache: ", 0x0A);
    video_print(result->cached_alias, 0x0B);
    video_print("\nNenhuma instalacao foi iniciada.\n", 0x0A);
}

static void cmd_update_print_runtime_result(
    const update_remote_runtime_result_t* result, int operation_result) {
    if (!result) return;
    video_print("Resultado runtime v2: ", 0x07);
    video_print(update_remote_runtime_reason_name(result->reason),
                operation_result == OK ? 0x0A : 0x0C);
    video_print(" http=", 0x08);
    shell_command_print_num(result->http_status);
    video_print(" bytes=", 0x08);
    shell_command_print_num(result->bytes_received);
    video_print(" baixados=", 0x08);
    shell_command_print_num(result->assets_downloaded);
    video_print(" reutilizados=", 0x08);
    shell_command_print_num(result->assets_reused);
    video_print("\n", 0x07);
    if (result->manifest.target_version.major ||
        result->manifest.target_version.minor ||
        result->manifest.target_version.patch) {
        cmd_update_print_version("  Alvo: ", &result->manifest.target_version,
                                 result->manifest.target_epoch);
        video_print("  Arquivos: ", 0x07);
        shell_command_print_num(result->manifest.entry_count);
        video_print("\n", 0x07);
    }
    if (result->cached_manifest_alias[0]) {
        video_print("  Manifesto em: ", 0x08);
        video_print(result->cached_manifest_alias, 0x0B);
        video_print("\n", 0x07);
    }
    if (result->cached_package_alias[0]) {
        video_print("  Pacote em: ", 0x08);
        video_print(result->cached_package_alias, 0x0B);
        video_print("\n", 0x07);
    }
}

static void cmd_update_runtime_status(void) {
    update_runtime_status_t* local = &shell_update_workspace.runtime_status;
    update_remote_runtime_status_t* remote =
        &shell_update_workspace.runtime_remote_status;
    update_runtime_cache_t* cache = &shell_update_workspace.runtime_cache;

    if (update_runtime_get_status(local) != OK) {
        video_print("Runtime v2 indisponivel.\n", 0x0C);
        return;
    }
    video_print("Runtime v2:\n  Instalado: ", 0x0B);
    cmd_update_print_version_inline(&local->installed_version,
                                    local->installed_epoch);
    video_print("\n  Estado: ", 0x07);
    video_print(local->state_valid ? "READY" : "INVALID",
                local->state_valid ? 0x0A : 0x0C);
    video_print(" journal=", 0x08);
    video_print(local->recovery_pending ? "PENDING" : "CLEAN",
                local->recovery_pending ? 0x0E : 0x0A);
    video_print(" rollback=", 0x08);
    video_print(local->rollback_available ? "READY" : "DISABLED",
                local->rollback_available ? 0x0A : 0x08);
    video_print("\n  Capacidades: apply=", 0x07);
    video_print(local->capabilities.apply_available ? "READY" : "DISABLED",
                local->capabilities.apply_available ? 0x0A : 0x08);
    video_print(" cache=", 0x08);
    video_print(local->capabilities.cache_available ? "READY" : "DISABLED",
                local->capabilities.cache_available ? 0x0A : 0x08);
    video_print("\n", 0x07);
    if (update_remote_runtime_get_status(remote) == OK) {
        video_print("  Remoto: ", 0x07);
        video_print(update_remote_runtime_state_name(remote->state),
                    remote->state == UPDATE_REMOTE_RUNTIME_STATE_FAILED ?
                    0x0C : 0x0A);
        video_print(" motivo=", 0x08);
        video_print(update_remote_runtime_reason_name(remote->reason), 0x07);
        video_print(" rede=", 0x08);
        video_print(remote->network_ready ? "READY\n" : "UNAVAILABLE\n",
                    remote->network_ready ? 0x0A : 0x0E);
    }
    if (update_runtime_get_cache(cache) == OK && cache->valid) {
        video_print("  Cache: ", 0x07);
        video_print(cache->full_package ? "FULL" : "SELECTIVE", 0x0A);
        video_print(" manifesto=", 0x08);
        video_print(cache->manifest_alias, 0x0B);
        video_print(" faltantes=", 0x08);
        shell_command_print_num(cache->missing_assets);
        video_print("\n", 0x07);
    }
}

static void cmd_update_runtime_check(const char* tag) {
    update_remote_options_t* options = &shell_update_workspace.remote_options;
    update_remote_runtime_result_t* result =
        &shell_update_workspace.runtime_remote_result;
    int operation_result;

    kmemset(options, 0, sizeof(*options));
    options->cancel_check = cmd_update_cancel_check;
    operation_result = update_remote_runtime_check(tag, options, result);
    cmd_update_print_runtime_result(result, operation_result);
    if (operation_result == OK) {
        video_print("Manifesto ZUM2 autenticado; nenhum download realizado.\n",
                    0x0A);
    }
}

static void cmd_update_runtime_fetch(const char* tag, int full, int confirmed) {
    update_remote_options_t* options = &shell_update_workspace.remote_options;
    update_remote_runtime_result_t* result =
        &shell_update_workspace.runtime_remote_result;
    update_remote_runtime_fetch_mode_t mode = full ?
        UPDATE_REMOTE_RUNTIME_FETCH_FULL : UPDATE_REMOTE_RUNTIME_FETCH_SELECTIVE;
    int operation_result;

    kmemset(options, 0, sizeof(*options));
    options->dry_run = confirmed ? 0U : 1U;
    options->cancel_check = cmd_update_cancel_check;
    operation_result = update_remote_runtime_fetch(tag, mode, options, result);
    cmd_update_print_runtime_result(result, operation_result);
    if (operation_result != OK) {
        if (result->cache_preserved) {
            video_print("O cache runtime anterior foi preservado.\n", 0x0A);
        }
        return;
    }
    if (!confirmed) {
        video_print("Preflight runtime aprovado; nenhum cache foi alterado.\n",
                    0x0A);
        video_print("Repita com --confirm para baixar ", 0x0E);
        video_print(full ? "o pacote completo.\n" : "somente os assets necessarios.\n",
                    0x0E);
        return;
    }
    video_print("Cache runtime publicado. Nenhuma instalacao foi iniciada.\n",
                0x0A);
}

static void cmd_update_runtime_verify(const char* path) {
    update_runtime_verification_t verification;
    update_runtime_cache_t* cache = &shell_update_workspace.runtime_cache;
    const update_runtime_manifest_t* cached_manifest = 0;
    int result;

    if (kstrcmp(path, "--cached") == 0) {
        if (update_runtime_get_cache(cache) != OK || !cache->valid) {
            video_print("Cache runtime ausente ou invalido.\n", 0x0C);
            return;
        }
        if (!cache->full_package || !cache->package_alias[0]) {
            video_print("Cache seletivo valido; assets faltantes=", 0x0A);
            shell_command_print_num(cache->missing_assets);
            video_print(".\n", 0x0A);
            return;
        }
        path = cache->package_alias;
        cached_manifest = &cache->manifest;
    }
    result = cached_manifest ? update_runtime_verify_file_for_manifest(
        path, cached_manifest, &verification) :
        update_runtime_verify_file(path, &verification);
    video_print(result == OK ? "ZUPD v2 autenticado e compativel.\n" :
                "ZUPD v2 recusado: ", result == OK ? 0x0A : 0x0C);
    if (result != OK) {
        video_print(update_runtime_reason_name(verification.reason), 0x0C);
        video_print("\n", 0x0C);
        return;
    }
    cmd_update_print_version("  Alvo: ", &verification.target_version,
                             verification.target_epoch);
    video_print("  Arquivos alterados: ", 0x07);
    shell_command_print_num(verification.changed_entries);
    video_print("\n", 0x07);
}

static void cmd_update_system_print_result(int operation_result) {
    update_system_verification_t* verification =
        &shell_update_workspace.system_verification;

    video_print(operation_result == OK ? "ZSYS autenticado e compativel.\n" :
                "ZSYS recusado: ",
                operation_result == OK ? 0x0A : 0x0C);
    if (operation_result != OK) {
        video_print(update_system_reason_name(verification->reason), 0x0C);
        video_print(" (erro=", 0x08);
        shell_command_print_num((uint32_t)operation_result);
        video_print(").\n", 0x0C);
    }
    if (!verification->header_valid) return;
    cmd_update_print_version("  Alvo: ", &verification->target_version,
                             verification->target_epoch);
    video_print("  Imagem: ", 0x07);
    shell_command_print_num(verification->image_size);
    video_print(" bytes componentes=", 0x08);
    shell_command_print_num(verification->component_count);
    video_print("\n  boot_abi=", 0x07);
    shell_command_print_num(verification->compatibility.boot_abi);
    video_print(" schema=", 0x08);
    shell_command_print_num(verification->compatibility.data_schema_from);
    video_print("->", 0x08);
    shell_command_print_num(verification->compatibility.data_schema_to);
    video_print(" reboot=", 0x08);
    video_print(verification->compatibility.requires_reboot ? "true\n" : "false\n",
                verification->compatibility.requires_reboot ? 0x0A : 0x0C);
}

static void cmd_update_system_verify(const char* path) {
    int result;

    result = update_system_verify_file(
        path, &shell_update_workspace.system_verification);
    cmd_update_system_print_result(result);
    video_print("Nenhuma gravacao foi realizada.\n", result == OK ? 0x0A : 0x0C);
}

static void cmd_update_system_check(const char* tag) {
    update_remote_options_t* options = &shell_update_workspace.remote_options;
    int result;

    kmemset(options, 0, sizeof(*options));
    options->cancel_check = cmd_update_cancel_check;
    result = update_system_check_tag(
        tag, options, &shell_update_workspace.system_verification);
    cmd_update_system_print_result(result);
    if (result == OK) {
        video_print("Preflight remoto concluido; nenhum download ou cache foi alterado.\n",
                    0x0A);
    }
}

static void cmd_update_system(const char* args) {
    char operation[16];
    char command[16];
    char first[FS_MAX_PATH];
    char second[UPDATE_REMOTE_TAG_SIZE + 1U];
    char third[16];
    const char* cursor = args;

    cmd_pkg_take_token(&cursor, operation, sizeof(operation));
    cmd_pkg_take_token(&cursor, command, sizeof(command));
    cmd_pkg_take_token(&cursor, first, sizeof(first));
    cmd_pkg_take_token(&cursor, second, sizeof(second));
    cmd_pkg_take_token(&cursor, third, sizeof(third));
    if (kstrcmp(operation, "system") == 0 &&
        kstrcmp(command, "verify") == 0 && first[0] && !second[0] &&
        !third[0] && !cmd_pkg_has_trailing_token(cursor)) {
        cmd_update_system_verify(first);
        return;
    }
    if (kstrcmp(operation, "system") == 0 &&
        kstrcmp(command, "check") == 0 &&
        kstrcmp(first, "--tag") == 0 && second[0] && !third[0] &&
        !cmd_pkg_has_trailing_token(cursor)) {
        cmd_update_system_check(second);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido do comando update system");
    video_print("Uso: update system verify system:/<arquivo.ZSYS>\n", 0x0E);
    video_print("     update system check --tag <tag>\n", 0x0E);
}

static void cmd_update_runtime_action(int rollback, int confirmed) {
    update_runtime_action_options_t options;
    update_runtime_action_result_t action;
    int result;

    kmemset(&options, 0, sizeof(options));
    options.dry_run = confirmed ? 0U : 1U;
    options.cancel_check = cmd_update_cancel_check;
    result = rollback ? update_runtime_rollback(&options, &action) :
                        update_runtime_apply_cached(&options, &action);
    video_print(rollback ? "Rollback runtime: " : "Aplicacao runtime: ", 0x07);
    video_print(update_runtime_reason_name(action.reason),
                result == OK ? 0x0A : 0x0C);
    video_print("\n", 0x07);
    if (result != OK) {
        if (action.recovery_pending) {
            video_print("Recuperacao pendente; reinicie para concluir.\n", 0x0E);
        }
        return;
    }
    if (!confirmed) {
        video_print("Preflight concluido sem gravacao. Use update runtime ", 0x0A);
        video_print(rollback ? "rollback" : "apply", 0x0E);
        video_print(" --confirm.\n", 0x0E);
        return;
    }
    video_print("Operacao concluida; reinicie para recarregar o runtime.\n",
                0x0A);
}

static void cmd_update_runtime_clear(int confirmed) {
    update_remote_options_t* options = &shell_update_workspace.remote_options;
    update_remote_runtime_result_t* result =
        &shell_update_workspace.runtime_remote_result;
    int operation_result;

    kmemset(options, 0, sizeof(*options));
    options->dry_run = confirmed ? 0U : 1U;
    operation_result = update_remote_runtime_clear(options, result);
    if (operation_result != OK) {
        video_print("Falha ao limpar cache runtime: ", 0x0C);
        video_print(update_remote_runtime_reason_name(result->reason), 0x0C);
        video_print("\n", 0x0C);
    } else if (!confirmed) {
        video_print("Cache runtime inspecionado sem gravacao. Use ", 0x0A);
        video_print("update runtime clear --confirm.\n", 0x0E);
    } else {
        video_print("Cache runtime removido.\n", 0x0A);
    }
}

static void cmd_update_runtime_test_fail_after(const char* value) {
    uint32_t entries = shell_command_parse_number(value);
    int result;

    if (entries == 0U || entries > UPDATE_RUNTIME_MAX_ENTRIES) {
        LOG_ERROR("SHELL", "Failpoint runtime fora do limite");
        video_print("Uso: update runtime test fail-after <1-16>\n", 0x0E);
        return;
    }
    result = update_runtime_test_fail_after((uint16_t)entries);
    if (result != OK) {
        video_print("Erro: failpoint runtime indisponivel.\n", 0x0C);
        return;
    }
    video_print("TEST ONLY: a proxima aplicacao runtime sera interrompida apos ",
                0x0E);
    shell_command_print_num(entries);
    video_print(" arquivo(s).\n", 0x0E);
}

static void cmd_update_runtime(const char* args) {
    char operation[16];
    char command[16];
    char first[FS_MAX_PATH];
    char second[UPDATE_REMOTE_TAG_SIZE + 1U];
    char third[16];
    char fourth[16];
    const char* tokens[4] = {first, second, third, fourth};
    const char* cursor = args;
    const char* tag = 0;
    uint8_t full = 0U;
    uint8_t confirmed = 0U;
    uint8_t tag_pending = 0U;
    uint8_t invalid = 0U;

    cmd_pkg_take_token(&cursor, operation, sizeof(operation));
    cmd_pkg_take_token(&cursor, command, sizeof(command));
    cmd_pkg_take_token(&cursor, first, sizeof(first));
    cmd_pkg_take_token(&cursor, second, sizeof(second));
    cmd_pkg_take_token(&cursor, third, sizeof(third));
    cmd_pkg_take_token(&cursor, fourth, sizeof(fourth));
    if (kstrcmp(operation, "runtime") != 0 || command[0] == '\0' ||
        cmd_pkg_has_trailing_token(cursor)) {
        invalid = 1U;
    } else if (kstrcmp(command, "status") == 0 && !first[0]) {
        cmd_update_runtime_status();
        return;
    } else if (kstrcmp(command, "verify") == 0 && first[0] && !second[0]) {
        cmd_update_runtime_verify(first);
        return;
    } else if ((kstrcmp(command, "apply") == 0 ||
                kstrcmp(command, "rollback") == 0) &&
               (!first[0] || kstrcmp(first, "--confirm") == 0) &&
               !second[0]) {
        cmd_update_runtime_action(kstrcmp(command, "rollback") == 0,
                                  first[0] != '\0');
        return;
    } else if (kstrcmp(command, "clear") == 0 &&
               (!first[0] || kstrcmp(first, "--confirm") == 0) &&
               !second[0]) {
        cmd_update_runtime_clear(first[0] != '\0');
        return;
    } else if (kstrcmp(command, "test") == 0 &&
               kstrcmp(first, "fail-after") == 0 && second[0] &&
               !third[0] && !fourth[0]) {
        cmd_update_runtime_test_fail_after(second);
        return;
    } else if (kstrcmp(command, "check") == 0) {
        if (!first[0]) {
            cmd_update_runtime_check(0);
            return;
        }
        if (kstrcmp(first, "--tag") == 0 && second[0] && !third[0]) {
            cmd_update_runtime_check(second);
            return;
        }
        invalid = 1U;
    } else if (kstrcmp(command, "fetch") == 0) {
        for (uint32_t index = 0U; index < 4U; index++) {
            const char* token = tokens[index];

            if (!token[0]) continue;
            if (tag_pending) {
                tag = token;
                tag_pending = 0U;
            } else if (kstrcmp(token, "--tag") == 0 && !tag &&
                       !tag_pending) {
                tag_pending = 1U;
            } else if (kstrcmp(token, "--full") == 0 && !full) {
                full = 1U;
            } else if (kstrcmp(token, "--confirm") == 0 && !confirmed) {
                confirmed = 1U;
            } else {
                invalid = 1U;
            }
        }
        if (tag_pending) invalid = 1U;
        if (!invalid) {
            cmd_update_runtime_fetch(tag, full, confirmed);
            return;
        }
    }
    if (invalid) LOG_WARN("SHELL", "Uso invalido do comando update runtime");
    video_print("Uso: update runtime status\n", 0x0E);
    video_print("     update runtime check [--tag TAG]\n", 0x0E);
    video_print("     update runtime fetch [--tag TAG] [--full] [--confirm]\n",
                0x0E);
    video_print("     update runtime verify <ARQUIVO>|--cached\n", 0x0E);
    video_print("     update runtime apply|rollback --confirm\n", 0x0E);
    video_print("     update runtime clear --confirm\n", 0x0E);
    video_print("     update runtime test fail-after <1-16>\n", 0x0E);
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
    if (kstrcmp(operation, "system") == 0) {
        cmd_update_system(args);
        return;
    }
    if (kstrcmp(operation, "runtime") == 0) {
        cmd_update_runtime(args);
        return;
    }
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
    if (kstrcmp(operation, "github") == 0 &&
        (kstrcmp(first, "check") == 0 ||
         kstrcmp(first, "fetch") == 0) &&
        kstrcmp(second, "--tag") == 0 && third[0] &&
        !cmd_pkg_has_trailing_token(cursor) &&
        (extra[0] == '\0' || kstrcmp(extra, "--confirm") == 0) &&
        ((kstrcmp(first, "check") == 0 && extra[0] == '\0') ||
         kstrcmp(first, "fetch") == 0)) {
        cmd_update_github(first, third, extra[0] != '\0');
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
    video_print("     update github check --tag <tag>\n", 0x0E);
    video_print("     update github fetch --tag <tag> [--confirm]\n", 0x0E);
    video_print("     update runtime status|check|fetch|verify|apply|rollback|clear\n",
                0x0E);
    video_print("     update system verify system:/<arquivo.ZSYS>\n", 0x0E);
    video_print("     update system check --tag <tag>\n", 0x0E);
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
        shell_command_print_num((uint32_t)result);
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
        shell_command_print_num((uint32_t)result);
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
        shell_command_print_num((uint32_t)result);
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
        shell_command_print_num((uint32_t)result);
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
        shell_command_print_num(status.rollback_count);
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
        shell_command_print_num(remote.highest_generation);
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
                component ? shell_diagnostics_health_state_color(component->state) : 0x0C);
    video_print("\n  refresh=", 0x07);
    video_print(refresh_result == OK ? "OK" : "ERRO",
                refresh_result == OK ? 0x0A : 0x0C);
    video_print(" motivo=", 0x08);
    video_print(app_catalog_reason_name(status.reason),
                status.reason == APP_CATALOG_REASON_NONE ? 0x0A : 0x0E);
    video_print("\n  fontes=", 0x07);
    shell_command_print_num(status.source_count);
    video_print(" validas=", 0x08);
    shell_command_print_num(status.valid_source_count);
    video_print(" invalidas=", 0x08);
    shell_command_print_num(status.invalid_source_count);
    video_print(" instaladas=", 0x08);
    shell_command_print_num(status.installed_count);
    video_print(" entradas=", 0x08);
    shell_command_print_num(status.entry_count);
    video_print("\n  limite_fontes=", 0x07);
    shell_command_print_num(APP_CATALOG_MAX_SOURCES);
    video_print(status.source_overflow ? " EXCEDIDO" : " OK",
                status.source_overflow ? 0x0E : 0x0A);
    video_print(" limite_entradas=", 0x08);
    shell_command_print_num(APP_CATALOG_MAX_ENTRIES);
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
            shell_command_print_num(transaction.rollback_count - 1U);
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
        shell_command_print_num(shell_store_workspace.remote_status.highest_generation);
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
    shell_command_print_num(entry.source_size);
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
        shell_command_print_num((uint32_t)operation_result);
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
        shell_command_print_num(action->required_clusters);
        video_print(" livres=", 0x08);
        shell_command_print_num(action->free_clusters);
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
        shell_command_print_num(entry.sequence);
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
    uint32_t count = shell_command_parse_number(value);
    int result = app_package_test_fail_after((uint16_t)count);

    if (result == OK) {
        video_print("Failpoint AS4 configurado apos ", 0x0A);
        shell_command_print_num(count);
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
    shell_command_print_num(shell_store_workspace.pid);
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
    shell_command_print_num(status->generation);
    video_print(" maxima=", 0x08);
    shell_command_print_num(status->highest_generation);
    video_print(" entradas=", 0x08);
    shell_command_print_num(status->entry_count);
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
    shell_command_print_num(entry->package_size);
    video_print(" bytes\n  SHA-256: ", 0x07);
    for (uint32_t index = 0; index < 32U; index++) {
        shell_command_print_hex(entry->package_hash[index], 2U);
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
        shell_command_print_num(remote->http_status);
    }
    video_print("\n", 0x07);
    if (remote->required_clusters || remote->free_clusters) {
        video_print("  Clusters: necessarios=", 0x07);
        shell_command_print_num(remote->required_clusters);
        video_print(" livres=", 0x08);
        shell_command_print_num(remote->free_clusters);
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
            uint32_t fail_after = shell_command_parse_number(
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
#define SHELL_PACKAGES_JOB_MAX_TOKEN 16U

typedef enum {
    SHELL_PACKAGES_JOB_PKG = 0,
    SHELL_PACKAGES_JOB_STORE,
    SHELL_PACKAGES_JOB_UPDATE
} shell_packages_job_operation_t;

static shell_packages_job_operation_t shell_packages_job_operation;

static shell_job_step_result_t shell_packages_job_step(
    shell_job_context_t* context) {
    if (!context) return SHELL_JOB_STEP_FAILED;
    if (!shell_job_generation_matches(shell_packages_job_generation)) {
        context->stale_events++;
        context->last_error = ERR_STATE;
        LOG_WARN("SHELL", "Resultado de pacote pertence a geracao antiga");
        return SHELL_JOB_STEP_FAILED;
    }
    if (context->cancel_requested &&
        shell_packages_job_operation != SHELL_PACKAGES_JOB_STORE &&
        shell_packages_job_operation != SHELL_PACKAGES_JOB_UPDATE) {
        context->last_error = ERR_TIMEOUT;
        return SHELL_JOB_STEP_CANCELLED;
    }
    shell_job_set_phase(context, "executando");
    if (shell_packages_job_operation == SHELL_PACKAGES_JOB_PKG) {
        cmd_pkg(context->arguments);
    } else if (shell_packages_job_operation == SHELL_PACKAGES_JOB_STORE) {
        cmd_store(context->arguments);
    } else {
        cmd_update(context->arguments);
    }
    if (context->cancel_requested) {
        context->last_error = ERR_TIMEOUT;
        return SHELL_JOB_STEP_CANCELLED;
    }
    context->last_error = OK;
    return SHELL_JOB_STEP_COMPLETE;
}

static void shell_packages_job_finish(shell_job_context_t* context,
                                      shell_job_state_t state, int result) {
    (void)context;
    if (state == SHELL_JOB_STATE_CANCELLED) {
        video_print("Operacao de pacotes cancelada.\n", 0x0E);
    } else if (state != SHELL_JOB_STATE_SUCCEEDED) {
        video_print("Operacao de pacotes terminou com erro; codigo=", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print("\n", 0x0C);
    }
    shell_hosted_present_progress();
}

static int shell_packages_job_cancel(shell_job_context_t* context) {
    (void)context;
    if (shell_packages_job_operation == SHELL_PACKAGES_JOB_STORE) {
        app_remote_request_cancel();
    }
    return OK;
}

static shell_job_step_result_t shell_packages_job_drain(
    shell_job_context_t* context) {
    app_remote_status_t remote_status;
    update_remote_status_t update_remote_status;

    (void)context;
    if (app_package_is_mutation_active()) return SHELL_JOB_STEP_PENDING;
    if (app_remote_get_status(&remote_status) == OK && remote_status.busy) {
        return SHELL_JOB_STEP_PENDING;
    }
    if (update_remote_get_status(&update_remote_status) == OK &&
        update_remote_status.busy) {
        return SHELL_JOB_STEP_PENDING;
    }
    return SHELL_JOB_STEP_COMPLETE;
}

static const shell_job_definition_t shell_pkg_job_definition = {
    "pkg", SHELL_JOB_KIND_PACKAGES, shell_packages_job_step,
    shell_packages_job_cancel, shell_packages_job_finish,
    shell_packages_job_drain
};

static const shell_job_definition_t shell_store_job_definition = {
    "store", SHELL_JOB_KIND_PACKAGES, shell_packages_job_step,
    shell_packages_job_cancel, shell_packages_job_finish,
    shell_packages_job_drain
};

static const shell_job_definition_t shell_update_job_definition = {
    "update", SHELL_JOB_KIND_PACKAGES, shell_packages_job_step,
    shell_packages_job_cancel, shell_packages_job_finish,
    shell_packages_job_drain
};

static int shell_packages_should_start(const char* command,
                                       const char* arguments) {
    char first[SHELL_PACKAGES_JOB_MAX_TOKEN];
    const char* cursor = arguments;

    if (!command || !arguments ||
        shell_command_read_token(&cursor, first, sizeof(first)) != OK) {
        return 0;
    }
    if (kstrcmp(command, "pkg") == 0) {
        return kstrcmp(first, "verify") == 0 ||
               kstrcmp(first, "install") == 0 ||
               kstrcmp(first, "remove") == 0;
    }
    if (kstrcmp(command, "update") == 0) {
        return kstrcmp(first, "apply") == 0 ||
               kstrcmp(first, "rollback") == 0 ||
               kstrcmp(first, "verify") == 0 ||
               kstrcmp(first, "fetch") == 0 ||
               kstrcmp(first, "remote") == 0 ||
               kstrcmp(first, "github") == 0 ||
               kstrcmp(first, "system") == 0;
    }
    if (kstrcmp(command, "store") != 0) return 0;
    return kstrcmp(first, "remote") == 0 ||
           kstrcmp(first, "install") == 0 ||
           kstrcmp(first, "update") == 0 ||
           kstrcmp(first, "remove") == 0 ||
           kstrcmp(first, "rollback") == 0;
}

int shell_packages_start_job(const char* command, const char* arguments) {
    const shell_job_definition_t* definition;

    if (!shell_packages_should_start(command, arguments)) return 0;
    shell_packages_job_operation =
        kstrcmp(command, "pkg") == 0 ? SHELL_PACKAGES_JOB_PKG :
        kstrcmp(command, "store") == 0 ? SHELL_PACKAGES_JOB_STORE :
                                          SHELL_PACKAGES_JOB_UPDATE;
    definition = shell_packages_job_operation == SHELL_PACKAGES_JOB_PKG ?
                 &shell_pkg_job_definition :
                 shell_packages_job_operation == SHELL_PACKAGES_JOB_STORE ?
                 &shell_store_job_definition : &shell_update_job_definition;
    if (shell_job_start(definition, arguments) != OK) {
        video_print("Job de pacotes recusado.\n", 0x0C);
    } else {
        shell_packages_job_generation = shell_job_get_generation();
    }
    return 1;
}

#define SHELL_PACKAGES_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

#define SHELL_PACKAGES_WRAP_NO_ARGS(adapter, handler) \
    void adapter(const char* arguments) { (void)arguments; handler(); }

void shell_dispatch_cmd_pkg(const char* arguments) {
    if (shell_packages_start_job("pkg", arguments)) return;
    cmd_pkg(arguments);
}

void shell_dispatch_cmd_store(const char* arguments) {
    if (shell_packages_start_job("store", arguments)) return;
    cmd_store(arguments);
}

void shell_dispatch_cmd_update(const char* arguments) {
    if (shell_packages_start_job("update", arguments)) return;
    cmd_update(arguments);
}
SHELL_PACKAGES_WRAP_NO_ARGS(shell_dispatch_cmd_pkgcheck, cmd_pkgcheck)

#undef SHELL_PACKAGES_WRAP_ARGS
#undef SHELL_PACKAGES_WRAP_NO_ARGS
