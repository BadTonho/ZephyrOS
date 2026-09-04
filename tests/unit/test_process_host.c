#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/syscall.h"
#include "drivers/tss.h"
#include "memory/paging.h"
#include "memory/slab.h"
#include "memory/vma.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define PROCESS_FIXTURE_PID 7U
#define PROCESS_FIXTURE_USER_PID 8U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks = 100U;
static uint8_t fake_power_signal;
static uint8_t fake_cache_alloc_enabled;
static uint32_t fake_focus_pid;
static uint8_t fake_cache_storage[64U];
static page_directory_t fake_directory;
static page_directory_t foreign_directory;
static process_t fixture;
static process_t user_fixture;

void process_host_test_idle_once(void);
void process_host_test_report_corruption(process_t* proc);

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:process:runtime|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:process:runtime|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:process:runtime|value=0x%08X\n",
           (uint32_t)result);
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

void panic(const char* message) {
    (void)message;
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    memset(destination, value, size);
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    memcpy(destination, source, size);
}

uint32_t kstrlen(const char* value) {
    return value ? (uint32_t)strlen(value) : 0U;
}

void* kmalloc(uint32_t size) {
    (void)size;
    return NULL;
}

void kfree(void* pointer) {
    (void)pointer;
}

kmem_cache_t* kmem_cache_create(const char* name, uint32_t object_size,
                                uint32_t alignment) {
    (void)name;
    (void)object_size;
    (void)alignment;
    return (kmem_cache_t*)fake_cache_storage;
}

void* kmem_cache_alloc(kmem_cache_t* cache) {
    (void)cache;
    return fake_cache_alloc_enabled ? &fixture : NULL;
}

void kmem_cache_free(kmem_cache_t* cache, void* object) {
    (void)cache;
    (void)object;
}

int kmem_cache_owns(const kmem_cache_t* cache, const void* object) {
    (void)cache;
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (processes[index] == object) return 1;
    }
    return 0;
}

int kmem_cache_validate(void) {
    return OK;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

page_directory_t* paging_get_current_directory(void) {
    return &fake_directory;
}

int paging_is_ready(void) {
    return 0;
}

page_directory_t* paging_create_user_directory(void) {
    return NULL;
}

void paging_free_user_directory(page_directory_t* directory) {
    (void)directory;
}

void paging_switch_directory(page_directory_t* directory) {
    (void)directory;
}

int paging_get_user_page_count(page_directory_t* directory,
                               uint32_t* out_count) {
    if (!directory || !out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

int paging_validate_user_range(uint32_t address, uint32_t size, int write) {
    (void)address;
    (void)size;
    (void)write;
    return OK;
}

int paging_copy_from_user(void* destination, const void* source,
                          uint32_t size) {
    if (!destination || !source || !size) return ERR_INVALID;
    memcpy(destination, source, size);
    return OK;
}

int paging_copy_to_user(void* destination, const void* source, uint32_t size) {
    if (!destination || !source || !size) return ERR_INVALID;
    memcpy(destination, source, size);
    return OK;
}

int process_vma_register_image(process_t* process, uint32_t code_size) {
    (void)process;
    (void)code_size;
    return OK;
}

void process_vma_release(process_t* process) {
    if (!process) return;
    process->vma_list = NULL;
    process->vma_count = 0U;
}

int process_vma_handle_page_fault(process_t* process,
                                  uint32_t fault_address,
                                  uint32_t fault_error) {
    (void)process;
    (void)fault_address;
    (void)fault_error;
    return ERR_NOT_FOUND;
}

int syscall_user_mode_is_enabled(void) {
    return 0;
}

int tss_is_ready(void) {
    return 0;
}

void tss_set_kernel_stack(uint32_t stack_top) {
    (void)stack_top;
}

uint32_t thread_get_count_by_owner(uint32_t owner_pid) {
    (void)owner_pid;
    return 0U;
}

void thread_yield(void) {
}

int process_signal_init(void) {
    return OK;
}

int process_signal_send(uint32_t pid, uint32_t signal_number) {
    (void)signal_number;
    if (fake_power_signal) {
        process_t* process = process_get_by_pid(pid);
        if (process) process->state = PROCESS_STATE_ZOMBIE;
    }
    return process_get_by_pid(pid) ? OK : ERR_NOT_FOUND;
}

int process_signal_record_user_fault(registers_t* regs) {
    return regs ? OK : ERR_NULL;
}

void process_signal_process_created(uint32_t pid, uint32_t parent_pid) {
    (void)pid;
    (void)parent_pid;
}

void process_signal_process_exited(uint32_t pid) {
    (void)pid;
}

void process_signal_process_destroyed(uint32_t pid) {
    (void)pid;
}

uint32_t process_get_focus(void) {
    return fake_focus_pid;
}

int process_restore_focus(void) {
    fake_focus_pid = 0U;
    return OK;
}

int wait_channel_init(wait_channel_t* channel, const char* owner) {
    if (!channel || !owner) return ERR_NULL;
    memset(channel, 0, sizeof(*channel));
    strncpy(channel->owner, owner, sizeof(channel->owner) - 1U);
    channel->initialized = 1U;
    channel->available = 1U;
    return OK;
}

int wait_channel_reset(wait_channel_t* channel) {
    if (!channel) return ERR_NULL;
    memset(channel, 0, sizeof(*channel));
    return OK;
}

int wait_queue_entry_init(wait_queue_entry_t* entry, void* target,
                          const char* target_name,
                          wait_target_type_t target_type, uint32_t target_id,
                          wait_queue_transition_fn_t block,
                          wait_queue_transition_fn_t wake,
                          wait_queue_yield_fn_t yield) {
    if (!entry || !target || !target_name || !block || !wake || !yield) {
        return ERR_NULL;
    }
    memset(entry, 0, sizeof(*entry));
    entry->target = target;
    entry->target_name = target_name;
    entry->target_type = target_type;
    entry->target_id = target_id;
    entry->block = block;
    entry->wake = wake;
    entry->yield = yield;
    return OK;
}

int wait_queue_block(wait_queue_head_t* queue, wait_queue_entry_t* entry,
                     uint32_t observed_condition, uint32_t timeout_ticks,
                     wait_reason_t* out_reason) {
    (void)queue;
    (void)entry;
    (void)observed_condition;
    (void)timeout_ticks;
    if (!out_reason) return ERR_NULL;
    *out_reason = WAIT_REASON_EVENT;
    return OK;
}

int wait_queue_remove(wait_queue_entry_t* entry, wait_reason_t reason) {
    if (!entry) return ERR_NULL;
    entry->linked = 0U;
    entry->reason = reason;
    if (entry->wake) entry->wake(entry->target, entry);
    return OK;
}

int wait_queue_wake_target(wait_queue_head_t* queue,
                           wait_target_type_t target,
                           wait_wake_mode_t mode, wait_reason_t reason,
                           uint32_t* out_woken) {
    (void)queue;
    (void)target;
    (void)mode;
    (void)reason;
    if (!out_woken) return ERR_NULL;
    *out_woken = 0U;
    return OK;
}

int wait_deadline_remaining_active(uint32_t deadline_tick, uint8_t active,
                                   uint32_t* out_remaining) {
    (void)deadline_tick;
    if (!out_remaining) return ERR_NULL;
    *out_remaining = active ? 1U : 0U;
    return OK;
}

int vfs_fd_table_init(vfs_fd_table_t* table) {
    if (!table) return ERR_NULL;
    memset(table, 0, sizeof(*table));
    table->initialized = 1U;
    return OK;
}

int vfs_fd_table_inherit_cwd(vfs_fd_table_t* table,
                             const vfs_fd_table_t* parent) {
    (void)parent;
    return table ? OK : ERR_NULL;
}

int vfs_fd_table_release(vfs_fd_table_t* table) {
    if (!table) return ERR_NULL;
    table->initialized = 0U;
    return OK;
}

void process_context_switch(process_context_t* previous,
                             process_context_t* next) {
    (void)previous;
    (void)next;
}

void process_user_enter(void) {
}

void process_user_termination_enter(void) {
}

static void reset_fixture(void) {
    process_init();
    memset(&fixture, 0, sizeof(fixture));
    memset(&user_fixture, 0, sizeof(user_fixture));
    fake_ticks = 100U;
    fake_power_signal = 0U;
    fake_cache_alloc_enabled = 0U;
    fake_focus_pid = 0U;
}

static void process_entry_fixture(void) {
}

static void install_fixture(uint32_t slot, process_t* process, uint32_t pid,
                            process_state_t state, uint8_t user_mode) {
    memset(process, 0, sizeof(*process));
    process->pid = pid;
    process->state = state;
    process->context.user_mode = user_mode;
    process->event_generation = pid + 100U;
    strncpy(process->name, user_mode ? "user-fixture" : "kernel-fixture",
            sizeof(process->name) - 1U);
    processes[slot] = process;
    process_count++;
}

static int test_initial_state(void) {
    scheduler_stats_t stats;
    scheduler_validation_t validation;
    process_user_fault_summary_t fault;
    process_stack_info_t stack_info;

    reset_fixture();
    if (process_get_current() != NULL || process_get_count() != 0U ||
        process_get_current_pid() != 0U || process_get_user_count() != 0U ||
        process_get_user_fault_count() != 0U ||
        process_get_event_generation() != 0U) return 1;
    if (process_get_by_pid(PROCESS_FIXTURE_PID) != NULL ||
        process_get_state_count(PROCESS_STATE_READY) != 0U ||
        process_get_state_count((process_state_t)99) != 0U) return 2;
    if (process_get_last_user_fault(NULL) != ERR_NULL) return 3;
    if (process_get_last_user_fault(&fault) != ERR_NOT_FOUND) return 4;
    scheduler_get_stats(NULL);
    scheduler_get_stats(&stats);
    if (stats.context_switches || stats.cooperative_yields ||
        stats.user_preemptions || stats.idle_fallbacks || stats.idle_ticks ||
        stats.active_ticks || stats.user_quantum_ticks != 1U) return 5;
    if (scheduler_schedule() != NULL) return 6;
    if (scheduler_validate_invariants(NULL) != ERR_NULL ||
        scheduler_validate_invariants(&validation) != ERR_STATE) return 7;
    if (process_stack_check_current(NULL) != ERR_NULL ||
        process_stack_check_current(&stats.active_ticks) != ERR_STATE) return 8;
    if (process_stack_get_info(PROCESS_FIXTURE_PID, NULL) != ERR_NULL) {
        return 90;
    }
    if (process_stack_get_info(PROCESS_FIXTURE_PID, &stack_info) !=
        ERR_NOT_FOUND) return 91;
    if (process_stack_validate_all(NULL) != ERR_NULL) return 10;
    process_bootstrap_idle();
    if (process_start_scheduler() != ERR_STATE) return 11;
    return 0;
}

static int test_creation_guards(void) {
    uint32_t pid = 0U;
    uint8_t code[8] = {0U};
    app_launch_info_t launch;

    memset(&launch, 0, sizeof(launch));
    launch.abi_version = APP_LAUNCH_ABI_VERSION;
    reset_fixture();
    if (process_create(NULL, NULL) != NULL) return 1;
    if (process_create_with_stack_size("bad", process_entry_fixture,
                                      KERNEL_STACK_SIZE + 1U) != NULL) return 2;
    fake_cache_alloc_enabled = 1U;
    if (process_create("no-memory", process_entry_fixture) != NULL) {
        fake_cache_alloc_enabled = 0U;
        return 3;
    }
    fake_cache_alloc_enabled = 0U;
    if (process_create_user_image("user", code, sizeof(code), NULL, 0U, 0U,
                                  PAGE_SIZE, 0, &pid) != ERR_UNAVAILABLE) {
        return 4;
    }
    if (process_create_user_image_suspended("user", code, sizeof(code), NULL,
                                           0U, 0U, PAGE_SIZE, &pid) !=
        ERR_UNAVAILABLE) return 5;
    if (process_create_user_image_suspended_with_launch(
            "user", code, sizeof(code), NULL, 0U, 0U, PAGE_SIZE, &launch,
            &pid) != ERR_UNAVAILABLE) return 6;
    if (process_create_user_test(0, &pid) != ERR_UNAVAILABLE ||
        process_create_user_test(1, &pid) != ERR_UNAVAILABLE) return 7;
    if (process_power_set_quiescing(1U) != OK ||
        process_create("blocked", process_entry_fixture) != NULL) {
        return 8;
    }
    if (process_create_user_image("user", code, sizeof(code), NULL, 0U, 0U,
                                  PAGE_SIZE, 0, &pid) != ERR_UNAVAILABLE) {
        return 9;
    }
    if (process_power_set_quiescing(0U) != OK) return 10;
    return 0;
}

static int test_scheduler_and_snapshots(void) {
    process_snapshot_t snapshots[2];
    process_snapshot_t snapshot;
    vm_area_info_t areas[1];
    scheduler_stats_t stats;
    scheduler_validation_t validation;
    uint32_t count = 0U;

    reset_fixture();
    install_fixture(0U, &fixture, 0U, PROCESS_STATE_RUNNING, 0U);
    install_fixture(1U, &user_fixture, PROCESS_FIXTURE_USER_PID,
                    PROCESS_STATE_READY, 1U);
    user_fixture.page_directory = &foreign_directory;
    user_fixture.user_code_size = 32U;
    user_fixture.user_data_size = 16U;
    if (scheduler_schedule() != &user_fixture) return 1;
    if (process_snapshot_copy(PROCESS_FIXTURE_USER_PID, &snapshot) != OK ||
        snapshot.pid != PROCESS_FIXTURE_USER_PID || snapshot.user_mode != 1U ||
        snapshot.image_bytes != 48U || snapshot.resident_pages != 0U) return 2;
    if (process_snapshot_copy(99U, &snapshot) != ERR_NOT_FOUND ||
        process_snapshot_copy(PROCESS_FIXTURE_USER_PID, NULL) != ERR_NULL) {
        return 3;
    }
    if (process_snapshot_list(NULL, 1U, &count) != ERR_NULL ||
        process_snapshot_list(snapshots, 1U, NULL) != ERR_NULL ||
        process_snapshot_list(snapshots, 1U, &count) != ERR_OVERFLOW ||
        count != 2U) return 4;
    if (process_snapshot_list(snapshots, 2U, &count) != OK || count != 2U ||
        snapshots[0].pid != 0U || snapshots[1].pid != PROCESS_FIXTURE_USER_PID) {
        return 5;
    }
    if (process_snapshot_copy_vmas(PROCESS_FIXTURE_USER_PID,
                                   user_fixture.event_generation, areas, 1U,
                                   &count) != OK || count != 0U) return 6;
    if (process_snapshot_copy_vmas(99U, 0U, NULL, 0U, &count) != ERR_NOT_FOUND ||
        process_snapshot_copy_vmas(PROCESS_FIXTURE_USER_PID,
                                   user_fixture.event_generation + 1U, NULL, 0U,
                                   &count) != ERR_AGAIN) return 7;
    scheduler_get_stats(&stats);
    if (stats.user_quantum_ticks != 1U) return 8;
    {
        int validation_result = scheduler_validate_invariants(&validation);
        if (validation_result != ERR_OVERFLOW ||
        validation.pid_table_valid != 1U || validation.state_table_valid != 1U) {
            return 9;
        }
    }
    return 0;
}

static int test_process_transitions(void) {
    registers_t regs;
    process_user_fault_summary_t fault;
    process_stack_validation_t stacks;
    uint32_t pid = 0U;
    uint32_t faulted = 0U;

    reset_fixture();
    install_fixture(1U, &fixture, PROCESS_FIXTURE_PID, PROCESS_STATE_READY,
                    0U);
    install_fixture(2U, &user_fixture, PROCESS_FIXTURE_USER_PID,
                    PROCESS_STATE_BLOCKED, 1U);
    if (process_is_user(&fixture) != 0 ||
        process_is_user(&user_fixture) != 1) return 1;
    if (process_start_user(PROCESS_FIXTURE_PID) != ERR_NOT_FOUND ||
        process_start_user(PROCESS_FIXTURE_USER_PID) != OK ||
        user_fixture.state != PROCESS_STATE_READY ||
        process_start_user(PROCESS_FIXTURE_USER_PID) != ERR_STATE) return 2;
    process_unblock(&user_fixture);
    if (user_fixture.state != PROCESS_STATE_READY) return 3;
    process_unblock(NULL);
    if (process_cancel_user_test(99U, 1U) != ERR_NOT_FOUND ||
        process_cancel_focused_user(1U) != ERR_NOT_FOUND) return 4;
    user_fixture.user_test = 1U;
    user_fixture.state = PROCESS_STATE_READY;
    if (process_cancel_user_test(PROCESS_FIXTURE_USER_PID, 22U) != OK ||
        user_fixture.state != PROCESS_STATE_ZOMBIE ||
        process_take_user_test_result(&pid, &faulted) != OK ||
        pid != PROCESS_FIXTURE_USER_PID || faulted != 0U ||
        process_take_user_test_result(&pid, &faulted) != ERR_NOT_FOUND) return 5;

    reset_fixture();
    install_fixture(1U, &user_fixture, PROCESS_FIXTURE_USER_PID,
                    PROCESS_STATE_READY, 1U);
    if (process_cancel_user(PROCESS_FIXTURE_USER_PID, 33U) != OK ||
        user_fixture.state != PROCESS_STATE_ZOMBIE) return 6;
    if (process_terminate_user_signal(99U, APP_SIGNAL_TERM, 0) !=
            ERR_NOT_FOUND ||
        process_terminate_user_signal(PROCESS_FIXTURE_USER_PID, 99U, 0) !=
            ERR_INVALID ||
        process_terminate_user_signal(PROCESS_FIXTURE_USER_PID, APP_SIGNAL_TERM,
                                       0) != OK) return 7;

    reset_fixture();
    install_fixture(1U, &fixture, PROCESS_FIXTURE_PID, PROCESS_STATE_READY,
                    0U);
    process_destroy(&fixture);
    if (processes[1] != NULL) return 8;
    memset(&regs, 0, sizeof(regs));
    if (process_handle_user_exception(&regs) != ERR_STATE ||
        process_prepare_user_termination(NULL) != ERR_NULL ||
        process_apply_pending_cancel(NULL) != ERR_NULL) return 9;
    if (process_stack_validate_all(&stacks) != OK || stacks.checked != 0U) {
        return 10;
    }
    if (process_get_last_user_fault(&fault) != ERR_NOT_FOUND) return 11;
    return 0;
}

static int test_power_shutdown(void) {
    uint32_t sigterm = 0U;
    uint32_t sigkill = 0U;
    uint32_t reaped = 0U;
    int shutdown_result;

    reset_fixture();
    if (process_power_shutdown_users(100U, &sigterm, &sigkill, &reaped) !=
        ERR_STATE) return 1;
    if (process_power_shutdown_users(100U, NULL, &sigkill, &reaped) != ERR_NULL) {
        return 2;
    }
    install_fixture(1U, &user_fixture, PROCESS_FIXTURE_USER_PID,
                    PROCESS_STATE_READY, 1U);
    user_fixture.page_directory = &foreign_directory;
    fake_power_signal = 1U;
    if (process_power_set_quiescing(1U) != OK) return 3;
    shutdown_result = process_power_shutdown_users(99U, &sigterm, &sigkill,
                                                   &reaped);
    if (shutdown_result != OK ||
        sigterm != 1U || sigkill != 0U || reaped != 1U || process_count != 0U) {
        return 3;
    }
    fake_power_signal = 0U;
    if (process_power_set_quiescing(0U) != OK) return 4;
    return 0;
}

static int test_wait_and_wake_contract(void) {
    wait_channel_t channel;
    wait_reason_t reason = WAIT_REASON_NONE;
    wait_info_t info;
    uint32_t count = 0U;

    reset_fixture();
    if (process_wait(NULL, 0U, 1U, &reason) != ERR_STATE ||
        process_wait(&channel, 0U, 1U, NULL) != ERR_NULL) return 1;
    memset(&channel, 0, sizeof(channel));
    if (process_wake_channel(&channel, WAIT_WAKE_ALL, WAIT_REASON_EVENT,
                             NULL) != ERR_NULL ||
        process_wake_channel(&channel, WAIT_WAKE_ALL, WAIT_REASON_EVENT,
                             &count) != OK || count != 0U) return 2;
    if (process_copy_waiters(NULL, 1U, &count) != ERR_NULL ||
        process_copy_waiters(&info, 1U, NULL) != ERR_NULL ||
        process_copy_waiters(&info, 1U, &count) != OK || count != 0U) return 3;

    install_fixture(1U, &fixture, PROCESS_FIXTURE_PID,
                    PROCESS_STATE_BLOCKED, 0U);
    if (process_cancel_wait(&fixture) != ERR_STATE) return 4;
    if (wait_channel_init(&channel, "host-wait") != OK) return 5;
    fixture.wait_active = 1U;
    fixture.wait_channel = &channel;
    fixture.wait_reason = WAIT_REASON_NONE;
    fixture.wait_deadline = fake_ticks + 10U;
    fixture.wait_deadline_active = 1U;
    fixture.wait_entry.linked = 1U;
    if (process_cancel_wait(&fixture) != OK ||
        fixture.wait_entry.linked != 0U) return 6;
    fixture.wait_active = 1U;
    fixture.state = PROCESS_STATE_BLOCKED;
    fixture.wait_entry.linked = 0U;
    if (process_copy_waiters(&info, 1U, &count) != OK || count != 1U ||
        info.id != PROCESS_FIXTURE_PID ||
        strcmp(info.name, "kernel-fixture") != 0 ||
        strcmp(info.channel_owner, "host-wait") != 0 ||
        info.remaining_ticks != 1U || !info.active) return 7;
    return 0;
}

static int test_stack_diagnostic_helpers(void) {
    reset_fixture();
    memset(&fixture, 0, sizeof(fixture));
    fixture.pid = PROCESS_FIXTURE_PID;
    strncpy(fixture.name, "stack-fixture", sizeof(fixture.name) - 1U);
    process_host_test_report_corruption(&fixture);
    if (!fixture.kernel_stack_corruption_reported ||
        fixture.kernel_stack_overflow_events != 1U) return 1;
    process_host_test_report_corruption(&fixture);
    if (fixture.kernel_stack_overflow_events != 1U) return 2;
    process_host_test_idle_once();
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = test_initial_state();
    if (!result) result = test_creation_guards();
    if (!result) result = test_scheduler_and_snapshots();
    if (!result) result = test_process_transitions();
    if (!result) result = test_power_shutdown();
    if (!result) result = test_wait_and_wake_contract();
    if (!result) result = test_stack_diagnostic_helpers();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        fprintf(stderr, "process host failure: %d\n", result);
        return result;
    }
    printf("process host: PASS\n");
    return 0;
}
