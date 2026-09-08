#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/service_supervisor.h"
#include "core/string.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 1024U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_PROCESS_CAPACITY 32U
#define FAKE_FIRST_PID 10U
#define FAKE_FIRST_GENERATION 100U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static process_t fake_processes[FAKE_PROCESS_CAPACITY];
static process_t* fake_table[MAX_PROCESSES];
static process_t* fake_current;
static uint32_t fake_process_count;
static uint32_t fake_next_pid;
static uint32_t fake_next_generation;
static uint32_t fake_create_failures[SERVICE_SUPERVISOR_ID_COUNT];
static uint32_t fake_invalid_identity[SERVICE_SUPERVISOR_ID_COUNT];
static uint32_t fake_prepare_failures[SERVICE_SUPERVISOR_ID_COUNT];
static uint32_t fake_dependency_failures[SERVICE_SUPERVISOR_ID_COUNT];
static uint32_t fake_create_order[16];
static uint32_t fake_create_order_count;
static uint32_t fake_fallback_count[SERVICE_SUPERVISOR_ID_COUNT];
static uint8_t fake_fallback_active[SERVICE_SUPERVISOR_ID_COUNT];
static recovery_state_t fake_recovery_state[RECOVERY_COMPONENT_COUNT];

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
    printf("ZCOV_BEGIN|case=host:core:service-supervisor|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:service-supervisor|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:service-supervisor|value=0x%08X\n",
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

uint32_t recovery_get_count(void) {
    return RECOVERY_COMPONENT_COUNT;
}

int recovery_mark_ready(recovery_component_id_t component) {
    if (component >= RECOVERY_COMPONENT_COUNT) return ERR_INVALID;
    fake_recovery_state[component] = RECOVERY_STATE_READY;
    return OK;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)error_code;
    (void)message;
    if (component >= RECOVERY_COMPONENT_COUNT) return ERR_INVALID;
    fake_recovery_state[component] = RECOVERY_STATE_DEGRADED;
    return OK;
}

int recovery_mark_disabled(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)error_code;
    (void)message;
    if (component >= RECOVERY_COMPONENT_COUNT) return ERR_INVALID;
    fake_recovery_state[component] = RECOVERY_STATE_DISABLED;
    return OK;
}

process_t* process_get_by_pid(uint32_t pid) {
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (fake_table[index] && fake_table[index]->pid == pid) {
            return fake_table[index];
        }
    }
    return 0;
}

process_t* process_get_current(void) {
    return fake_current;
}

void process_destroy(process_t* process) {
    if (!process) return;
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (fake_table[index] == process) fake_table[index] = 0;
    }
    kmemset(process, 0, sizeof(*process));
}

static process_t* fake_create(service_supervisor_id_t id) {
    process_t* process;
    uint32_t table_index;

    if (fake_create_failures[id]) {
        fake_create_failures[id]--;
        return 0;
    }
    if (fake_process_count >= FAKE_PROCESS_CAPACITY ||
        fake_create_order_count >= sizeof(fake_create_order) /
                                   sizeof(fake_create_order[0])) {
        return 0;
    }
    process = &fake_processes[fake_process_count++];
    kmemset(process, 0, sizeof(*process));
    process->pid = fake_next_pid++;
    process->event_generation = fake_next_generation++;
    process->state = PROCESS_STATE_READY;
    if (fake_invalid_identity[id]) {
        fake_invalid_identity[id]--;
        process->state = PROCESS_STATE_ZOMBIE;
    }
    table_index = 0U;
    while (table_index < MAX_PROCESSES && fake_table[table_index]) {
        table_index++;
    }
    if (table_index >= MAX_PROCESSES) return 0;
    fake_table[table_index] = process;
    fake_create_order[fake_create_order_count++] = id;
    return process;
}

static process_t* fake_create_kworker(void) {
    return fake_create(SERVICE_SUPERVISOR_KWORKER);
}

static process_t* fake_create_system(void) {
    return fake_create(SERVICE_SUPERVISOR_SYSTEM);
}

static process_t* fake_create_shell(void) {
    return fake_create(SERVICE_SUPERVISOR_SHELL);
}

static process_t* fake_create_desktop(void) {
    return fake_create(SERVICE_SUPERVISOR_DESKTOP);
}

static int fake_prepare(service_supervisor_id_t id, process_t* process) {
    if (!process) return ERR_NULL;
    if (fake_prepare_failures[id]) {
        fake_prepare_failures[id]--;
        return ERR_STATE;
    }
    return OK;
}

static int fake_prepare_kworker(process_t* process) {
    return fake_prepare(SERVICE_SUPERVISOR_KWORKER, process);
}

static int fake_prepare_system(process_t* process) {
    return fake_prepare(SERVICE_SUPERVISOR_SYSTEM, process);
}

static int fake_prepare_shell(process_t* process) {
    return fake_prepare(SERVICE_SUPERVISOR_SHELL, process);
}

static int fake_prepare_desktop(process_t* process) {
    return fake_prepare(SERVICE_SUPERVISOR_DESKTOP, process);
}

static int fake_dependency(service_supervisor_id_t id) {
    if (fake_dependency_failures[id]) {
        fake_dependency_failures[id]--;
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static int fake_dependency_kworker(void) {
    return fake_dependency(SERVICE_SUPERVISOR_KWORKER);
}

static int fake_dependency_system(void) {
    return fake_dependency(SERVICE_SUPERVISOR_SYSTEM);
}

static int fake_dependency_shell(void) {
    return fake_dependency(SERVICE_SUPERVISOR_SHELL);
}

static int fake_dependency_desktop(void) {
    return fake_dependency(SERVICE_SUPERVISOR_DESKTOP);
}

static void fake_fallback(service_supervisor_id_t id, uint8_t active) {
    fake_fallback_count[id]++;
    fake_fallback_active[id] = active ? 1U : 0U;
}

static void fake_fallback_kworker(uint8_t active) {
    fake_fallback(SERVICE_SUPERVISOR_KWORKER, active);
}

static void fake_fallback_system(uint8_t active) {
    fake_fallback(SERVICE_SUPERVISOR_SYSTEM, active);
}

static void fixture_reset(void) {
    service_supervisor_test_reset();
    kmemset(fake_processes, 0, sizeof(fake_processes));
    kmemset(fake_table, 0, sizeof(fake_table));
    kmemset(fake_create_failures, 0, sizeof(fake_create_failures));
    kmemset(fake_invalid_identity, 0, sizeof(fake_invalid_identity));
    kmemset(fake_prepare_failures, 0, sizeof(fake_prepare_failures));
    kmemset(fake_dependency_failures, 0, sizeof(fake_dependency_failures));
    kmemset(fake_create_order, 0, sizeof(fake_create_order));
    kmemset(fake_fallback_count, 0, sizeof(fake_fallback_count));
    kmemset(fake_fallback_active, 0, sizeof(fake_fallback_active));
    kmemset(fake_recovery_state, 0, sizeof(fake_recovery_state));
    fake_current = &fake_processes[0];
    fake_current->pid = 1U;
    fake_current->event_generation = 1U;
    fake_current->state = PROCESS_STATE_RUNNING;
    fake_table[0] = fake_current;
    fake_process_count = 1U;
    fake_next_pid = FAKE_FIRST_PID;
    fake_next_generation = FAKE_FIRST_GENERATION;
    fake_create_order_count = 0U;
    if (service_supervisor_init() != OK) return;
}

static int configure_all(void) {
    service_supervisor_definition_t definitions[
        SERVICE_SUPERVISOR_ID_COUNT] = {
        {SERVICE_SUPERVISOR_KWORKER, "kworker", RECOVERY_COMPONENT_COUNT,
         fake_create_kworker, fake_prepare_kworker,
         fake_dependency_kworker, fake_fallback_kworker},
        {SERVICE_SUPERVISOR_SYSTEM, "System", RECOVERY_COMPONENT_SYSTEM_PROCESS,
         fake_create_system, fake_prepare_system,
         fake_dependency_system, fake_fallback_system},
        {SERVICE_SUPERVISOR_SHELL, "Shell", RECOVERY_COMPONENT_SHELL,
         fake_create_shell, fake_prepare_shell,
         fake_dependency_shell, 0},
        {SERVICE_SUPERVISOR_DESKTOP, "Desktop", RECOVERY_COMPONENT_DESKTOP,
         fake_create_desktop, fake_prepare_desktop,
         fake_dependency_desktop, 0}
    };

    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        if (service_supervisor_configure(&definitions[index]) != OK) return 0;
    }
    return 1;
}

static int check_configuration_contract(void) {
    service_supervisor_definition_t invalid;
    service_supervisor_snapshot_t snapshot;
    service_supervisor_snapshot_t snapshots[SERVICE_SUPERVISOR_ID_COUNT];
    uint32_t count = 0U;

    fixture_reset();
    if (!service_supervisor_is_initialized()) return 1;
    if (service_supervisor_init() != ERR_STATE) return 1;
    if (service_supervisor_configure(0) != ERR_NULL) return 2;
    kmemset(&invalid, 0, sizeof(invalid));
    invalid.id = (service_supervisor_id_t)SERVICE_SUPERVISOR_ID_COUNT;
    invalid.name = "invalid";
    if (service_supervisor_configure(&invalid) != ERR_INVALID) return 3;
    invalid.id = SERVICE_SUPERVISOR_KWORKER;
    invalid.name = "123456789012345678901234";
    invalid.create = fake_create_kworker;
    invalid.dependency = fake_dependency_kworker;
    if (service_supervisor_configure(&invalid) != ERR_OVERFLOW) return 4;
    if (!configure_all()) return 5;
    invalid.name = "duplicate";
    if (service_supervisor_configure(&invalid) != ERR_STATE) return 6;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SYSTEM,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_STOPPED || snapshot.pid != 0U ||
        snapshot.generation != 0U) return 7;
    if (service_supervisor_snapshot_list(snapshots, 3U, &count) !=
        ERR_OVERFLOW || count != 0U) return 8;
    if (service_supervisor_start() != OK) return 9;
    if (service_supervisor_start() != ERR_STATE) return 10;
    if (service_supervisor_validate_state() != OK) return 11;
    if (fake_create_order_count != SERVICE_SUPERVISOR_ID_COUNT) return 12;
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        uint32_t pid = 0U;
        uint32_t generation = 0U;

        if (service_supervisor_get_identity(
                (service_supervisor_id_t)index, &pid, &generation) != OK ||
            !pid || !generation ||
            service_supervisor_snapshot_copy(
                (service_supervisor_id_t)index, &snapshot) != OK ||
            snapshot.pid != pid || snapshot.generation != generation ||
            snapshot.state != SERVICE_SUPERVISOR_READY) return 13;
    }
    if (service_supervisor_snapshot_list(snapshots,
                                         SERVICE_SUPERVISOR_ID_COUNT,
                                         &count) != OK ||
        count != SERVICE_SUPERVISOR_ID_COUNT) return 14;
    if (service_supervisor_state_name(SERVICE_SUPERVISOR_STARTING)[0] != 'S' ||
        service_supervisor_state_name((service_supervisor_state_t)99)[0] !=
            'U') return 15;
    return 0;
}

static int check_retry_and_failure_contract(void) {
    service_supervisor_snapshot_t snapshot;
    process_t* shell;

    fixture_reset();
    if (!configure_all()) return 1;
    fake_create_failures[SERVICE_SUPERVISOR_SYSTEM] = 1U;
    fake_invalid_identity[SERVICE_SUPERVISOR_KWORKER] = 1U;
    if (service_supervisor_start() != OK) return 2;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SYSTEM,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_READY ||
        snapshot.failures != 1U || snapshot.restart_attempts != 1U) return 3;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_KWORKER,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_READY ||
        snapshot.failures != 1U || snapshot.restart_attempts != 1U) return 4;
    if (fake_fallback_active[SERVICE_SUPERVISOR_SYSTEM]) return 4;
    if (service_supervisor_test_fail_next(SERVICE_SUPERVISOR_SHELL) != OK ||
        service_supervisor_poll() != OK) return 5;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SHELL,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_READY ||
        snapshot.restart_attempts != 1U) return 6;
    shell = process_get_by_pid(snapshot.pid);
    if (!shell) return 7;
    shell->state = PROCESS_STATE_ZOMBIE;
    fake_create_failures[SERVICE_SUPERVISOR_SHELL] = 1U;
    if (service_supervisor_poll() != ERR_NOT_FOUND) return 8;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SHELL,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_FAILED || snapshot.pid != 0U ||
        snapshot.generation != 0U) return 9;
    if (service_supervisor_poll() != OK) return 10;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SHELL,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_FAILED) return 11;
    if (service_supervisor_validate_state() != OK) return 12;
    return 0;
}

static int check_cleanup_failure_contract(void) {
    service_supervisor_snapshot_t snapshot;
    process_t* shell;
    uint32_t old_pid;
    uint32_t old_generation;

    fixture_reset();
    if (!configure_all() || service_supervisor_start() != OK) return 1;
    if (service_supervisor_get_identity(SERVICE_SUPERVISOR_SHELL,
                                        &old_pid, &old_generation) != OK) {
        return 2;
    }
    shell = process_get_by_pid(old_pid);
    if (!shell) return 3;
    fake_current = shell;
    if (service_supervisor_test_fail_next(SERVICE_SUPERVISOR_SHELL) != OK) {
        return 4;
    }
    if (service_supervisor_poll() != ERR_STATE) return 5;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SHELL,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_FAILED ||
        snapshot.pid != old_pid || snapshot.generation != old_generation) {
        return 6;
    }
    fake_current = &fake_processes[0];
    if (service_supervisor_poll() != OK ||
        service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SHELL,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_READY ||
        snapshot.pid == old_pid || process_get_by_pid(old_pid)) return 7;
    if (service_supervisor_validate_state() != OK) return 8;
    return 0;
}

static int check_quiescence_and_generation_contract(void) {
    service_supervisor_snapshot_t snapshot;
    process_t* desktop;
    process_t* system;
    uint32_t old_pid;
    uint32_t old_generation;

    fixture_reset();
    if (!configure_all() || service_supervisor_start() != OK) return 1;
    if (service_supervisor_get_identity(SERVICE_SUPERVISOR_SYSTEM,
                                        &old_pid, &old_generation) != OK) {
        return 2;
    }
    system = process_get_by_pid(old_pid);
    if (!system) return 3;
    system->event_generation++;
    if (service_supervisor_poll() != OK) return 4;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_SYSTEM,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_READY || snapshot.pid == old_pid ||
        snapshot.generation == old_generation) return 5;
    if (service_supervisor_set_quiescing(1U) != OK) return 6;
    if (service_supervisor_get_identity(SERVICE_SUPERVISOR_DESKTOP,
                                        &old_pid, &old_generation) != OK) {
        return 7;
    }
    desktop = process_get_by_pid(old_pid);
    if (!desktop) return 8;
    desktop->state = PROCESS_STATE_ZOMBIE;
    if (service_supervisor_poll() != OK) return 9;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_DESKTOP,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_STOPPED || snapshot.pid != 0U ||
        snapshot.generation != 0U) return 10;
    if (process_get_by_pid(old_pid) != desktop) return 11;
    if (service_supervisor_set_quiescing(0U) != OK ||
        service_supervisor_poll() != OK) return 12;
    if (service_supervisor_snapshot_copy(SERVICE_SUPERVISOR_DESKTOP,
                                         &snapshot) != OK ||
        snapshot.state != SERVICE_SUPERVISOR_READY || snapshot.pid == old_pid) {
        return 13;
    }
    if (process_get_by_pid(old_pid) != 0) return 14;
    if (service_supervisor_validate_state() != OK) return 15;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_configuration_contract();
    if (!result) result = check_retry_and_failure_contract();
    if (!result) result = check_cleanup_failure_contract();
    if (!result) result = check_quiescence_and_generation_contract();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        fprintf(stderr, "service supervisor host failure: %d\n", result);
        return result;
    }
    printf("service supervisor host: PASS\n");
    return 0;
}
