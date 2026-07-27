#include "apps/shell.h"
#include "core/video.h"
#include "core/keyboard.h"
#include "fs/fs.h"
#include "core/memory.h"
#include "core/timer.h"
#include "process/process.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "apps/taskmanager.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
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
#include "core/power.h"
#include "core/app_api.h"
#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/syscall.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "drivers/vesa.h"
#include "drivers/font.h"

#define SHELL_Q2CHECK_FAULT_RUNS 2U
#define SHELL_HOSTED_DEFAULT_CONTENT_WIDTH 880
#define SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT 560
#define SHELL_HOSTED_FRAME_WIDTH 4
#define SHELL_HOSTED_FRAME_HEIGHT 28

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
    int loader_result;
    int cancellation_result;
    int cleanup_result;
    uint8_t loader_started;
    uint8_t cancellation_started;
    shell_regcheck_state_t state;
} shell_regcheck_t;

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

static char input_buffer[SHELL_BUFFER_SIZE];
static char appcheck_oversized_text[APP_API_MAX_TEXT_SIZE + 1];
static uint8_t appcheck_demo_image[APP_IMAGE_MAX_FILE_SIZE];
static uint8_t appcheck_demo_verify[APP_IMAGE_MAX_FILE_SIZE];
static int input_pos = 0;
static int shell_waiting_user_test = 0;
static uint8_t shell_extended_scancode = 0;
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

static void shell_handle_terminal_key(uint8_t scancode);
static int shell_open_hosted(void);
static void shell_hosted_draw(int x, int y, int width, int height);
static void shell_hosted_key(uint8_t scancode);
static int shell_hosted_mouse(mouse_event_t* event, int x, int y,
                              int width, int height);
static void shell_hosted_close(void);
static int shell_is_hosted_visible(void);
static void shell_suspend_terminal(void);

static const wm_hosted_app_t shell_hosted_app = {
    WM_APP_SHELL, "ZephyrOS Shell", "Shell",
    WM_HOSTED_MIN_WIDTH, WM_HOSTED_MIN_HEIGHT,
    SHELL_HOSTED_DEFAULT_CONTENT_WIDTH + SHELL_HOSTED_FRAME_WIDTH,
    SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT + SHELL_HOSTED_FRAME_HEIGHT,
    shell_hosted_draw, shell_hosted_key, shell_hosted_mouse, shell_hosted_close
};
static shell_q2check_t shell_q2check;
static shell_regcheck_t shell_regcheck;
static shell_kmetrics_baseline_t shell_kmetrics_baseline;

#define SHELL_SCANCODE_EXTENDED 0xE0
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

static const char app_input_test_message[] =
    "Entrada ZAPP ativa: Enter encerra; F12 cancela.\n";

static void cmd_appcheck_print_result(const char* label, int result);
static void cmd_appcheck_print_expected_result(const char* label, int actual,
                                               int expected);
static int shell_should_show_prompt(void);
static int shell_run_memcheck(shell_memcheck_result_t* result_out);

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
    kmemset(input_buffer, 0, sizeof(input_buffer));
}

static int shell_is_hosted_visible(void) {
    return shell_hosted_visible && wm_is_active() &&
           desktop_get_mode() == DESKTOP_MODE_MODERN &&
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

static void shell_hosted_draw(int x, int y, int width, int height) {
    if (video_terminal_draw(x, y, width, height) != OK) {
        LOG_WARN("SHELL", "Falha ao desenhar terminal hospedado");
    }
}

static void shell_hosted_key(uint8_t scancode) {
    shell_handle_terminal_key(scancode);
    /* O WM recompõe ao fim do despacho da tecla; evita um segundo frame no loop. */
    (void)video_terminal_take_hosted_dirty();
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
    if (event->event != MOUSE_EVENT_WHEEL || event->wheel == 0) return 0;
    if (!video_terminal_scroll(event->wheel * SHELL_WHEEL_SCROLL_LINES)) {
        return 0;
    }
    /* O WM recompõe o mesmo frame ao terminar este callback. */
    (void)video_terminal_take_hosted_dirty();
    return 1;
}

static void shell_hosted_close(void) {
    shell_hosted_visible = 0;
}

static int shell_open_hosted(void) {
    int result;

    if (desktop_get_mode() != DESKTOP_MODE_MODERN) {
        LOG_WARN("SHELL", "Shell hospedado requer modo Moderno");
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
        !app_loader_is_ready() || !app_package_is_ready()) {
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

static int shell_regcheck_validate_packages(void) {
    app_package_diagnostic_t diagnostic;
    int result;

    kmemset(&diagnostic, 0, sizeof(diagnostic));
    result = app_package_run_diagnostics(&diagnostic);
    if (result != OK || !diagnostic.invalid_package ||
        !diagnostic.missing_dependency || !diagnostic.insufficient_space) {
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
    int hosted_workspace = desktop_get_mode() == DESKTOP_MODE_MODERN &&
                           wm_is_active();

    if (!hosted_workspace) {
        if (taskmgr_is_gui_open() && request != IPC_APP_OPEN_TASKMANAGER_GUI) {
            taskmgr_close();
        }
        if (settings_is_open() && request != IPC_APP_OPEN_SETTINGS) {
            settings_close();
        }
    }

    switch ((ipc_app_request_t)request) {
        case IPC_APP_OPEN_SHELL:
            if (desktop_get_mode() == DESKTOP_MODE_MODERN) {
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
                if (desktop_get_mode() != DESKTOP_MODE_MODERN) {
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
                if (desktop_get_mode() != DESKTOP_MODE_MODERN) {
                    desktop_set_active(0);
                }
                if (desktop_get_mode() == DESKTOP_MODE_MODERN &&
                    taskmgr_open_gui() != OK) {
                    desktop_set_active(0);
                    wm_set_active(0);
                    shell_suspend_terminal();
                    LOG_WARN("SHELL", "GUI do Task Manager indisponivel; usando TUI");
                    taskmgr_run();
                } else if (desktop_get_mode() != DESKTOP_MODE_MODERN) {
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
            if (desktop_get_mode() != DESKTOP_MODE_MODERN) {
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
                if (desktop_get_mode() != DESKTOP_MODE_MODERN) {
                    desktop_set_active(0);
                }
                settings_open();
            } else {
                video_print("Erro: Configuracoes indisponiveis.\n", 0x0C);
            }
            break;
        default:
            LOG_ERROR("SHELL", "Solicitacao de aplicativo invalida");
            break;
    }
}

static void shell_redraw_after_overlay_close(void) {
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

static void cmd_help(void) {
    video_begin_update();
    video_print("Comandos disponiveis:\n", 0x0B);
    video_print("  help     - Mostra esta mensagem\n", 0x07);
    video_print("  clear    - Limpa tela e historico do terminal\n", 0x07);
    video_print("  desktop  - Abre a area de trabalho\n", 0x07);
    video_print("  guimode  - Alterna Desktop classic/modern\n", 0x07);
    video_print("  settings - Abre o painel de configuracoes\n", 0x07);
    video_print("  wm       - Abre gerenciador de janelas\n", 0x07);
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
    video_print("  mouse    - Mostra status do mouse PS/2\n", 0x07);
    video_print("  health   - Mostra estado dos componentes (use PgUp/PgDn)\n", 0x07);
    video_print("  devices  - Lista inventario de hardware (-v para detalhes)\n", 0x07);
    video_print("  device-info <id> - Mostra detalhes de um dispositivo\n", 0x07);
    video_print("  device-scan - Refaz apenas a varredura PCI\n", 0x07);
    video_print("  power status - Mostra capacidades reais de energia\n", 0x07);
    video_print("  kmetrics - Mostra linha-base de metricas do kernel\n", 0x07);
    video_print("  memcheck - Valida heap, PMM e diretorios de usuario\n", 0x07);
    video_print("  schedcheck - Valida invariantes do scheduler\n", 0x07);
    video_print("  q2check  - Executa diagnostico compacto da Q2\n", 0x07);
    video_print("  regcheck - Executa regressao compacta com F12\n", 0x07);
    video_print("  appcheck - Testa API, arquivos, IPC e loader\n", 0x07);
    video_print("  pkg      - Gerencia pacotes .ZPK locais\n", 0x07);
    video_print("             pkg list | info | verify | install | remove\n", 0x08);
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
    video_print("  shutdown - Desliga o sistema\n", 0x07);
    video_print("  guitest  - Testa primitivas GUI 2D\n", 0x07);
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

static void cmd_health(void) {
    video_begin_update();
    video_print("Estado dos componentes:\n", 0x0B);

    for (uint32_t i = 0; i < recovery_get_count(); i++) {
        cmd_health_print_component((recovery_component_id_t)i);
    }

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

static void cmd_device_scan(const char* args) {
    int pci_result;
    int refresh_result;

    if (*args) {
        LOG_WARN("SHELL", "Uso invalido de device-scan");
        video_print("Uso: device-scan\n", 0x0C);
        return;
    }
    pci_result = pci_init();
    if (pci_result != OK && pci_result != ERR_OVERFLOW) {
        recovery_mark_disabled(RECOVERY_COMPONENT_DEVICES, pci_result,
                               "Varredura PCI falhou");
        LOG_ERROR("SHELL", "Falha ao refazer varredura PCI");
        video_print("Erro: varredura PCI indisponivel.\n", 0x0C);
        return;
    }
    refresh_result = device_manager_refresh();
    if (refresh_result == OK) {
        recovery_mark_ready(RECOVERY_COMPONENT_DEVICES);
        video_print("Varredura PCI concluida; inventario atualizado.\n", 0x0A);
        return;
    }
    if (refresh_result == ERR_OVERFLOW) {
        recovery_mark_degraded(RECOVERY_COMPONENT_DEVICES, refresh_result,
                               "Inventario PCI parcial");
        video_print("Varredura PCI parcial; inventario atualizado.\n", 0x0E);
        return;
    }
    recovery_mark_disabled(RECOVERY_COMPONENT_DEVICES, refresh_result,
                           "Inventario de dispositivos indisponivel");
    LOG_ERROR("SHELL", "Falha ao atualizar inventario de dispositivos");
    video_print("Erro: inventario de dispositivos indisponivel.\n", 0x0C);
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
    video_print(status.acpi_available ? "DISPONIVEL" : "INDISPONIVEL",
                status.acpi_available ? 0x0A : 0x0E);
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
    video_print(power_capability_name(status.hardware_poweroff), 0x0E);
    video_print("\n  Reboot (controlador de teclado): ", 0x07);
    video_print(power_capability_name(status.reboot), 0x0A);
    video_print("\n  Nota: S5 e o comando shutdown atuais apenas param a CPU; ",
                0x08);
    video_print("nao desligam a maquina.\n", 0x08);
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
    app_launch_info_t launch;
    int result;

    result = app_loader_build_launch_info(valid_args, &launch);
    if (result == OK &&
        (launch.argc != 2U || launch.raw_length != kstrlen(valid_args) ||
         launch.args[0].offset != 0U || launch.args[0].length == 0U ||
         launch.args[1].offset <= launch.args[0].offset)) {
        result = ERR_STATE;
    }
    cmd_appcheck_print_result("loader_argumentos_validos", result);

    result = app_loader_build_launch_info("", &launch);
    if (result == OK && (launch.argc != 0U || launch.raw_length != 0U)) {
        result = ERR_STATE;
    }
    cmd_appcheck_print_result("loader_argumentos_vazios", result);

    result = app_loader_build_launch_info(too_many_args, &launch);
    cmd_appcheck_print_result("loader_argumentos_excesso", result);

    result = app_loader_build_launch_info(appcheck_oversized_args, &launch);
    cmd_appcheck_print_result("loader_argumentos_grandes", result);
}

static void cmd_appcheck_loader(void) {
    app_image_header_t header;
    app_launch_info_t launch;
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
            result = app_loader_build_launch_info("appcheck alpha beta", &launch);
            cmd_appcheck_print_result("loader_argumentos_execucao", result);
        }
        if (result == OK) {
            result = app_loader_run_file_with_launch(APP_CHECK_DEMO_PATH,
                                                     &launch, &pid);
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
    int result;

    if (*args) {
        video_print("Uso: regcheck\n", 0x0C);
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
    shell_regcheck.initial_focus = process_get_focus();
    shell_regcheck.initial_process_count = process_get_count();
    shell_regcheck.initial_user_count = process_get_user_count();
    shell_regcheck.initial_zombie_count =
        process_get_state_count(PROCESS_STATE_ZOMBIE);
    paging_get_user_stats(&paging);
    shell_regcheck.initial_user_directories = paging.active_directories;
    shell_regcheck.initial_user_pages = paging.active_pages;

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
    int result;
    int passed;

    kmemset(&diagnostic, 0, sizeof(diagnostic));
    result = app_package_run_diagnostics(&diagnostic);
    passed = result == OK && diagnostic.invalid_package &&
             diagnostic.missing_dependency && diagnostic.insufficient_space;

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
    video_print("  resultado ", 0x07);
    video_print(passed ? "OK\n" : "ERRO\n", passed ? 0x0A : 0x0C);
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
        video_print(desktop_get_mode() == DESKTOP_MODE_MODERN ?
                    "modern\n" : "classic\n", 0x07);
        return;
    }

    while (args[i] && args[i] != ' ' && i < (int)sizeof(mode_name) - 1) {
        mode_name[i] = args[i];
        i++;
    }
    mode_name[i] = '\0';

    if (kstrcmp(mode_name, "classic") == 0) {
        result = desktop_set_mode(DESKTOP_MODE_CLASSIC);
        if (result == OK) {
            if (wm_is_active()) wm_set_active(0);
            shell_resume_terminal();
            video_print("Desktop em modo classic.\n", 0x0A);
        }
        return;
    }

    if (kstrcmp(mode_name, "modern") == 0) {
        if (!recovery_is_available(RECOVERY_COMPONENT_VESA) ||
            !recovery_is_available(RECOVERY_COMPONENT_BACKBUFFER)) {
            video_print("Modo modern indisponivel; classic mantido.\n", 0x0C);
            return;
        }

        result = desktop_set_mode(DESKTOP_MODE_MODERN);
        if (result == OK) {
            video_print("Desktop em modo modern.\n", 0x0A);
        } else if (result == ERR_NOT_FOUND) {
            video_print("Modo modern requer VESA; classic mantido.\n", 0x0C);
        }
        return;
    }

    video_print("Uso: guimode classic|modern\n", 0x0C);
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

static void cmd_shutdown(void) {
    video_print("Desligando...\n", 0x0E);
    asm volatile("cli");
    for (;;) asm volatile("hlt");
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
    if (!shell_is_hosted_visible()) return;
    if (video_terminal_take_hosted_dirty()) {
        wm_request_hosted_redraw(WM_APP_SHELL);
    }
}

static void process_input(void) {
    shell_prompt_visible = 0;
    video_print("\n", 0x07);

    if (input_pos == 0) {
        shell_print_prompt();
        return;
    }

    input_buffer[input_pos] = '\0';
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

    /* QEMU pode encaminhar a barra ABNT2 por dois scancodes fisicos. */
    char c = (scancode == SHELL_SCANCODE_ISO_SLASH ||
              scancode == SHELL_SCANCODE_ABNT2_SLASH) ? '/' :
             scancode_table[scancode];

    if (scancode == 0x0E) {
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
            cmd_shutdown();
        } else if (tb_result == 7) {
            shell_handle_app_request(IPC_APP_OPEN_DESKTOP);
        } else if (tb_result == 8) {
            shell_handle_app_request(IPC_APP_OPEN_SETTINGS);
        } else if (tb_result == 9) {
            shell_redraw_after_overlay_close();
        }
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
        cmd_health();
    } else if (kstrcmp(cmd, "devices") == 0) {
        cmd_devices(input);
    } else if (kstrcmp(cmd, "device-info") == 0) {
        cmd_device_info(input);
    } else if (kstrcmp(cmd, "device-scan") == 0) {
        cmd_device_scan(input);
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
        cmd_shutdown();
    } else if (kstrcmp(cmd, "guitest") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_GUITEST)) {
            if (wm_is_active()) wm_set_active(0);
            shell_suspend_terminal();
            guitest_open();
        } else {
            video_print("Erro: GUI Test indisponivel.\n", 0x0C);
        }
    } else if (kstrcmp(cmd, "taskmgr") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_TASKMANAGER)) {
            shell_suspend_terminal_for_scene();
            if (desktop_get_mode() == DESKTOP_MODE_MODERN) {
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
    } else if (kstrcmp(cmd, "wm") == 0) {
        if (recovery_is_enabled(RECOVERY_COMPONENT_WM)) {
            if (desktop_get_mode() == DESKTOP_MODE_MODERN && wm_is_active()) {
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
        if (!recovery_is_enabled(RECOVERY_COMPONENT_EDITOR)) {
            video_print("Erro: Editor indisponivel.\n", 0x0C);
        } else if (*input) {
            char name[13];
            int n = 0;
            while (input[n] && n < 12) { name[n] = input[n]; n++; }
            name[n] = '\0';
            str_upper(name);
            if (wm_is_active()) wm_set_active(0);
            shell_suspend_terminal();
            editor_run_file(name);
        } else {
            if (wm_is_active()) wm_set_active(0);
            shell_suspend_terminal();
            editor_run();
        }
    } else if (kstrcmp(cmd, "mouse") == 0) {
        video_print("Mouse PS/2:\n", 0x0B);
        video_print("  X: ", 0x07);
        print_num(mouse_get_x());
        video_print("\n  Y: ", 0x07);
        print_num(mouse_get_y());
        video_print("\n  Botoes: ", 0x07);
        print_num(mouse_get_buttons());
        video_print("\n  roda: ", 0x07);
        if (mouse_has_wheel()) {
            video_print("DISPONIVEL", 0x0A);
        } else {
            video_print("INDISPONIVEL (FALLBACK 3 BYTES)", 0x0E);
        }
        video_print("\n", 0x07);
    } else {
        video_print("Comando nao encontrado: ", 0x0C);
        video_print(cmd, 0x0C);
        video_print("\n", 0x0C);
        video_print("Digite 'help' para ver os comandos.\n", 0x08);
    }

    return 0;
}
