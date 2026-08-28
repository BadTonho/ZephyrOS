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

static void cmd_app_inputtest(void) {
    shell_checks_run_app_inputtest();
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
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    result = app_builtin_run_argtest(&launch, &pid);
    if (result != OK) {
        video_print("Erro: teste de argumentos indisponivel (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    video_print("Teste de argumentos iniciado, PID ", 0x0A);
    shell_command_print_num(pid);
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
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    video_print("Teste de saida iniciado, PID ", 0x0A);
    shell_command_print_num(pid);
    video_print(".\n", 0x0A);
}

static void cmd_app_pathtest(void) {
    uint32_t pid = 0U;
    int result = app_builtin_run_pathtest(&pid);

    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao iniciar teste ring3 de caminhos");
        video_print("Erro: teste de caminhos indisponivel (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Teste de caminhos iniciado, PID ", 0x0A);
    shell_command_print_num(pid);
    video_print(".\n", 0x0A);
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
        video_print("Uso: app run <arquivo.ZAP> [args] | app inputtest | app outputtest [fail] | app pathtest | app argtest <texto>\n", 0x0E);
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

    if (kstrcmp(subcommand, "pathtest") == 0) {
        if (args[sub_length] != '\0') {
            video_print("Uso: app pathtest\n", 0x0E);
            return;
        }
        cmd_app_pathtest();
        return;
    }

    if (kstrcmp(subcommand, "run") != 0) {
        video_print("Uso: app run <arquivo.ZAP> [args] | app inputtest | app outputtest [fail] | app pathtest | app argtest <texto>\n", 0x0E);
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
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }

    result = app_loader_run_file_with_launch(path, &launch, &pid);
    if (result != OK) {
        video_print("Erro: nao foi possivel executar o aplicativo (codigo ", 0x0C);
        shell_command_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
        return;
    }
    video_print("Aplicativo iniciado de forma assincrona, PID ", 0x0A);
    shell_command_print_num(pid);
    video_print(".\n", 0x0A);
}

static void cmd_icons_print_status(const char* name, icon_desktop_id_t id) {
    int status = icons_get_desktop_bitmap_status(id);

    video_print("  ", 0x07);
    video_print(name, 0x0B);
    video_print(": ", 0x07);
    video_print(status == OK ? "BMP" : "FALLBACK", status == OK ? 0x0A : 0x0E);
    if (status != OK) {
        video_print(" erro=", 0x08);
        shell_command_print_num((uint32_t)status);
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
            shell_runtime_resume_terminal();
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
    shell_runtime_suspend_terminal();
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
        shell_command_print_num(mode->width);
        video_print("x", 0x07);
        shell_command_print_num(mode->height);
        video_print("x", 0x07);
        shell_command_print_num(mode->bpp);
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
    shell_command_print_num(metrics.factor_numerator);
    video_print("/", 0x07);
    shell_command_print_num(metrics.factor_denominator);
    video_print(metrics.available ? ", disponivel)\n" :
                                    ", indisponivel)\n", 0x07);
    if (desktop_get_mode() == DESKTOP_MODE_SIMPLE) {
        video_print("  Fonte ativa: legada 8x16\n", 0x07);
        video_print("  Preset Classic: " FONT_UI_FAMILY_NAME " ", 0x07);
        shell_command_print_num(metrics.font_width);
        video_print("x", 0x07);
        shell_command_print_num(metrics.font_height);
        video_print(" (nativa, RAM)\n", 0x07);
        video_print("  Espaco Classic: ", 0x07);
    } else {
        video_print("  Fonte: " FONT_UI_FAMILY_NAME " ", 0x07);
        shell_command_print_num(metrics.font_width);
        video_print("x", 0x07);
        shell_command_print_num(metrics.font_height);
        video_print(" (nativa)  Espaco: ", 0x07);
    }
    shell_command_print_num(metrics.spacing);
    video_print(" px\n", 0x07);
    video_print("  Taskbar: ", 0x07);
    shell_command_print_num(metrics.taskbar_height);
    video_print("x", 0x07);
    shell_command_print_num(metrics.taskbar_side_width);
    video_print(" px  Titulo: ", 0x07);
    shell_command_print_num(metrics.title_bar_height);
    video_print(" px\n", 0x07);
    video_print("  Botao minimo: ", 0x07);
    shell_command_print_num(metrics.button_min_width);
    video_print("x", 0x07);
    shell_command_print_num(metrics.button_min_height);
    video_print("  Icone: ", 0x07);
    shell_command_print_num(metrics.icon_size);
    video_print(" px\n", 0x07);
    video_print("  VESA minima: ", 0x07);
    shell_command_print_num(metrics.min_width);
    video_print("x", 0x07);
    shell_command_print_num(metrics.min_height);
    video_print("\n", 0x07);
    video_print("  Area util: ", 0x07);
    shell_command_print_num((uint32_t)work_area.width);
    video_print("x", 0x07);
    shell_command_print_num((uint32_t)work_area.height);
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
        shell_command_uppercase(name);
        if (fs_get_type() == FS_TYPE_FAT12 &&
            (fs_get_root_file_info(name, &size, &attributes) != OK ||
             (attributes & SHELL_FS_DIRECTORY_ATTRIBUTE))) {
            LOG_WARN("SHELL", "Arquivo solicitado ao Editor nao existe");
            video_print("Erro: arquivo nao encontrado; crie-o com F7 no Explorer.\n",
                        0x0C);
            return;
        }
        if (wm_is_active()) wm_set_active(0);
        shell_runtime_suspend_terminal();
        editor_run_file(name);
        return;
    }
    if (wm_is_active()) wm_set_active(0);
    shell_runtime_suspend_terminal();
    editor_run();
}

void shell_dispatch_cmd_desktop(const char* arguments) {
    (void)arguments;
    if (!desktop_is_active()) {
        if (wm_is_active()) wm_set_active(0);
        shell_runtime_suspend_terminal();
        desktop_set_active(1);
        desktop_draw();
    }
}

void shell_dispatch_cmd_explorer(const char* arguments) {
    (void)arguments;
    if (shell_runtime_prepare_filemanager() != OK) {
        video_print("Erro: Explorer indisponivel.\n", 0x0C);
    } else {
        shell_runtime_suspend_terminal_for_scene();
        fm_run();
    }
}

void shell_dispatch_cmd_taskmgr(const char* arguments) {
    (void)arguments;
    if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
        shell_runtime_suspend_terminal_for_scene();
        if (desktop_get_mode() == DESKTOP_MODE_CLASSIC) {
            if (taskmgr_open_gui() != OK) {
                wm_set_active(0);
                shell_runtime_suspend_terminal();
                LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                taskmgr_run();
            }
        } else {
            taskmgr_run();
        }
    } else {
        video_print("Erro: Task Manager indisponivel.\n", 0x0C);
    }
}

void shell_dispatch_cmd_taskcfg(const char* arguments) {
    (void)arguments;
    taskbar_draw_config_menu();
}

void shell_dispatch_cmd_settings(const char* arguments) {
    (void)arguments;
    if (recovery_is_enabled(RECOVERY_COMPONENT_SETTINGS)) {
        shell_runtime_suspend_terminal_for_scene();
        settings_open();
    } else {
        video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
    }
}

void shell_dispatch_cmd_updater(const char* arguments) {
    (void)arguments;
    if (recovery_is_enabled(RECOVERY_COMPONENT_SYSTEM_UPDATER)) {
        shell_runtime_suspend_terminal_for_scene();
        if (updater_open() != OK) {
            video_print("Erro: System Updater indisponivel.\n", 0x0C);
        }
    } else {
        video_print("Erro: System Updater indisponivel.\n", 0x0C);
    }
}

void shell_dispatch_cmd_wm(const char* arguments) {
    (void)arguments;
    if (recovery_is_enabled(RECOVERY_COMPONENT_WM)) {
        if (desktop_get_mode() == DESKTOP_MODE_CLASSIC && wm_is_active()) {
            wm_set_active(0);
        }
        shell_runtime_suspend_terminal();
        desktop_set_active(0);
        wm_set_active(1);
    } else {
        video_print("Erro: Window Manager indisponivel.\n", 0x0C);
    }
}

void shell_dispatch_cmd_play(const char* arguments) {
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER) ||
        !recovery_is_available(RECOVERY_COMPONENT_AC97)) {
        video_print("Erro: audio ou filesystem indisponivel.\n", 0x0C);
    } else if (!arguments || !*arguments) {
        video_print("Uso: play <arquivo.wav>\n", 0x0C);
    } else {
        char name[13];
        int n = 0;
        while (arguments[n] && n < 12) {
            name[n] = arguments[n];
            n++;
        }
        name[n] = '\0';
        shell_command_uppercase(name);
        video_print("Tocando: ", 0x0A);
        video_print(name, 0x0A);
        video_print("\n", 0x0A);
        int play_result = mp_play_audio(name);
        if (play_result != OK) {
            video_print("Erro: nao foi possivel reproduzir o audio.\n", 0x0C);
        }
    }
}

void shell_dispatch_cmd_view(const char* arguments) {
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        video_print("Erro: imagem ou filesystem indisponivel.\n", 0x0C);
    } else if (!arguments || !*arguments) {
        video_print("Uso: view <arquivo.bmp>\n", 0x0C);
    } else {
        char name[13];
        int n = 0;
        while (arguments[n] && n < 12) {
            name[n] = arguments[n];
            n++;
        }
        name[n] = '\0';
        shell_command_uppercase(name);
        if (wm_is_active()) wm_set_active(0);
        shell_runtime_suspend_terminal();
        int view_result = mp_play_image(name);
        if (view_result != OK) {
            video_print("Erro: nao foi possivel exibir a imagem.\n", 0x0C);
        }
    }
}

void shell_dispatch_cmd_stop(const char* arguments) {
    (void)arguments;
    mp_stop();
    video_print("Player parado.\n", 0x0A);
}
#define SHELL_APPS_WRAP_ARGS(adapter, handler) \
    void adapter(const char* arguments) { handler(arguments); }

#define SHELL_APPS_WRAP_NO_ARGS(adapter, handler) \
    void adapter(const char* arguments) { (void)arguments; handler(); }

SHELL_APPS_WRAP_ARGS(shell_dispatch_cmd_app, cmd_app)
SHELL_APPS_WRAP_ARGS(shell_dispatch_cmd_guimode, cmd_guimode)
SHELL_APPS_WRAP_ARGS(shell_dispatch_cmd_display, cmd_display)
SHELL_APPS_WRAP_ARGS(shell_dispatch_cmd_guitest, cmd_guitest)
SHELL_APPS_WRAP_NO_ARGS(shell_dispatch_cmd_icons, cmd_icons)
SHELL_APPS_WRAP_ARGS(shell_dispatch_cmd_edit, cmd_edit)

#undef SHELL_APPS_WRAP_ARGS
#undef SHELL_APPS_WRAP_NO_ARGS
