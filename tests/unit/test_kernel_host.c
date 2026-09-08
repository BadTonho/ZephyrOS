#include <stdint.h>
#include <stdio.h>

#include "core/app_loader.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/network_manager.h"
#include "core/recovery.h"
#include "core/test_protocol.h"
#include "core/timer.h"
#include "core/usb_manager.h"
#include "core/workqueue.h"
#include "drivers/mouse.h"
#include "fs/file_index.h"
#include "kernel_host_test.h"
#include "process/process.h"
#include "ui/taskbar.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    uintptr_t address = (uintptr_t)function;

    (void)caller;
    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static int workqueue_initialized;

int workqueue_init(void) {
    workqueue_initialized = 1;
    return OK;
}

int work_init(work_struct_t* work, const char* owner,
              work_priority_t priority, work_func_t callback, void* context) {
    (void)owner;
    (void)priority;
    if (!work || !callback || !context) return ERR_NULL;
    work->callback = callback;
    work->context = context;
    work->initialized = 1U;
    return OK;
}

int schedule_work(work_struct_t* work) {
    return work && work->initialized ? OK : ERR_NULL;
}

int schedule_delayed_work(work_struct_t* work, uint32_t delay_ticks) {
    (void)delay_ticks;
    return work && work->initialized ? OK : ERR_NULL;
}

int workqueue_needs_fallback(uint8_t* out_required) {
    if (!out_required) return ERR_NULL;
    *out_required = 0U;
    return workqueue_initialized ? OK : ERR_STATE;
}

int workqueue_set_fallback(uint8_t active) {
    (void)active;
    return OK;
}

int workqueue_dispatch(uint32_t high_budget, uint32_t normal_budget,
                       uint32_t* out_executed) {
    (void)high_budget;
    (void)normal_budget;
    if (!out_executed) return ERR_NULL;
    *out_executed = 0U;
    return OK;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed) {
    (void)budget;
    if (!out_processed) return ERR_NULL;
    *out_processed = 0U;
    return OK;
}

int irq_deferred_get_status(irq_deferred_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    out_status->queued = 0U;
    return OK;
}

int irq_deferred_set_notifier(irq_deferred_notifier_t notifier, void* context) {
    (void)notifier;
    (void)context;
    return OK;
}

int timer_set_pending_notifier(timer_pending_notifier_t notifier, void* context) {
    (void)notifier;
    (void)context;
    return OK;
}

int timer_dispatch_pending(uint32_t budget, uint32_t* out_dispatched) {
    (void)budget;
    if (!out_dispatched) return ERR_NULL;
    *out_dispatched = 0U;
    return OK;
}

int network_manager_poll(uint32_t* out_processed) {
    if (!out_processed) return ERR_NULL;
    *out_processed = 0U;
    return OK;
}

int network_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int network_manager_get_interface(uint32_t index,
                                  network_interface_info_t* out_info) {
    (void)index;
    (void)out_info;
    return ERR_NOT_FOUND;
}

int network_manager_format_text(const network_interface_info_t* info,
                                network_interface_text_t* out_text) {
    (void)info;
    (void)out_text;
    return ERR_NOT_FOUND;
}

int network_manager_acquire_dhcp(const char* id) {
    (void)id;
    return OK;
}

int usb_manager_poll(uint32_t budget, uint32_t* out_processed) {
    (void)budget;
    if (!out_processed) return ERR_NULL;
    *out_processed = 0U;
    return OK;
}

uint32_t process_get_event_generation(void) {
    return 7U;
}

uint32_t process_get_focus(void) {
    return 0U;
}

process_t* process_get_by_pid(uint32_t pid) {
    (void)pid;
    return 0;
}

process_t* process_get_current(void) {
    return 0;
}

void process_destroy(process_t* process) {
    (void)process;
}

int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken) {
    (void)channel;
    (void)mode;
    (void)reason;
    if (out_woken) *out_woken = 0U;
    return OK;
}

int ipc_send(uint32_t pid, ipc_msg_t* message) {
    (void)pid;
    (void)message;
    return OK;
}

uint32_t app_loader_get_foreground_pid(void) {
    return 0U;
}

int app_loader_cancel_foreground(uint32_t exit_code) {
    (void)exit_code;
    return OK;
}

int file_index_poll(uint32_t budget, uint32_t* out_steps) {
    (void)budget;
    if (!out_steps) return ERR_NULL;
    *out_steps = 0U;
    return OK;
}

int file_index_get_status(file_index_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    out_status->state = FILE_INDEX_STATE_READY;
    return OK;
}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return 1;
}

uint32_t recovery_get_count(void) {
    return RECOVERY_COMPONENT_COUNT;
}

int recovery_mark_ready(recovery_component_id_t component) {
    return component < RECOVERY_COMPONENT_COUNT ? OK : ERR_INVALID;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)error_code;
    (void)message;
    return component < RECOVERY_COMPONENT_COUNT ? OK : ERR_INVALID;
}

int recovery_mark_disabled(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)error_code;
    (void)message;
    return component < RECOVERY_COMPONENT_COUNT ? OK : ERR_INVALID;
}

int shell_job_is_active(void) {
    return 0;
}

int shell_job_get_wait_timeout(uint32_t* timeout_out) {
    if (!timeout_out) return ERR_NULL;
    *timeout_out = WAIT_TIMEOUT_INFINITE;
    return OK;
}

void taskbar_draw(void) {}
int taskbar_is_menu_open(void) { return 0; }
int taskbar_get_bounds(tb_rect_t* bounds) { (void)bounds; return 0; }
int taskbar_handle_click(int x, int y) { (void)x; (void)y; return 0; }
int taskbar_take_window_request(void) { return -1; }
void taskbar_update_clock(void) {}

int wm_is_active(void) { return 0; }
void wm_draw_all(void) {}
int wm_handle_mouse(mouse_event_t* event) { (void)event; return 0; }
void wm_set_active(int active) { (void)active; }
void wm_toggle_window(int id) { (void)id; }
void wm_update_cpu_stats(void) {}

int guitest_is_active(void) { return 0; }
void guitest_draw(void) {}
void guitest_handle_mouse(mouse_event_t* event) { (void)event; }

int taskmgr_is_gui_open(void) { return 0; }
int taskmgr_is_gui_minimized(void) { return 0; }
void taskmgr_gui_restore(void) {}
int taskmgr_gui_handle_mouse(mouse_event_t* event) { (void)event; return 0; }
void taskmgr_close(void) {}
int taskmgr_is_open(void) { return 0; }
void taskmgr_refresh(void) {}
void taskmgr_gui_update(void) {}

int settings_is_open(void) { return 0; }
void settings_draw(void) {}
void settings_close(void) {}
void settings_handle_mouse(mouse_event_t* event) { (void)event; }

int updater_is_open(void) { return 0; }
void updater_draw(void) {}
void updater_close(void) {}

int appstore_is_open(void) { return 0; }
void appstore_draw(void) {}
void appstore_close(void) {}

int fm_is_running(void) { return 0; }
void fm_draw(void) {}

int desktop_is_active(void) { return 0; }
void desktop_draw(void) {}
int desktop_get_mode(void) { return 0; }
int desktop_handle_mouse(mouse_event_t* event) { (void)event; return 0; }
int desktop_handle_click(int x, int y) { (void)x; (void)y; return 0; }

int shell_handle_mouse(mouse_event_t* event) { (void)event; return 0; }

int power_reboot(void) { return OK; }
int power_shutdown_request(void) { return OK; }
void shell_command_print_num(uint32_t value) { (void)value; }
void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void test_protocol_poll(void) {}
uint8_t test_protocol_is_active(void) { return 0U; }
void process_yield(void) {}
void process_block(uint32_t timeout) { (void)timeout; }

void keyboard_process_events(void) {}
void mouse_process_events(void) {}
void fm_update(void) {}
void shell_update_hosted_terminal(void) {}
void shell_init(void) {}
int ipc_receive(ipc_msg_t* message) { (void)message; return 0; }
int ipc_wait(uint32_t timeout, wait_reason_t* reason) {
    (void)timeout;
    if (reason) *reason = WAIT_REASON_NONE;
    return OK;
}
void shell_handle_key(uint8_t scancode) { (void)scancode; }
void shell_handle_app_request(uint32_t request) { (void)request; }
int app_loader_reap_finished(void) { return 0; }
void shell_report_user_test_result(void) {}
void shell_report_app_loader_result(void) {}
void shell_job_poll(void) {}

static void coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:kernel:runtime|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:kernel:runtime|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:kernel:runtime|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = kernel_host_test_run_finite_routes();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
