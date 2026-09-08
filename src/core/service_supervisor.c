#include "core/service_supervisor.h"

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"

typedef struct {
    service_supervisor_definition_t definition;
    char name[SERVICE_SUPERVISOR_NAME_LENGTH];
    service_supervisor_state_t state;
    uint32_t pid;
    uint32_t generation;
    uint32_t restart_attempts;
    uint32_t failures;
    int last_error;
    uint32_t stopped_pid;
    uint32_t stopped_generation;
    uint8_t configured;
    uint8_t retry_used;
    uint8_t cleanup_failed;
    uint8_t fallback_active;
} service_supervisor_entry_t;

static service_supervisor_entry_t service_entries[
    SERVICE_SUPERVISOR_ID_COUNT];
static uint8_t service_supervisor_initialized;
static uint8_t service_supervisor_started;
static uint8_t service_supervisor_quiescing;
static uint8_t service_supervisor_test_failures[
    SERVICE_SUPERVISOR_ID_COUNT];

static int service_supervisor_valid_id(service_supervisor_id_t id) {
    return id >= SERVICE_SUPERVISOR_KWORKER &&
           id < SERVICE_SUPERVISOR_ID_COUNT;
}

static void service_supervisor_copy_name(char* destination,
                                         const char* source) {
    uint32_t index = 0U;

    if (!destination) return;
    if (!source) {
        destination[0] = '\0';
        return;
    }
    while (index + 1U < SERVICE_SUPERVISOR_NAME_LENGTH && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void service_supervisor_clear_identity(
    service_supervisor_entry_t* entry) {
    if (!entry) return;
    entry->pid = 0U;
    entry->generation = 0U;
}

static void service_supervisor_clear_stopped_identity(
    service_supervisor_entry_t* entry) {
    if (!entry) return;
    entry->stopped_pid = 0U;
    entry->stopped_generation = 0U;
}

static int service_supervisor_process_state_valid(process_state_t state) {
    return state > PROCESS_STATE_UNUSED && state <= PROCESS_STATE_ZOMBIE;
}

static int service_supervisor_process_live_values(uint32_t pid,
                                                  uint32_t generation,
                                                  process_t** output) {
    process_t* process;

    if (output) *output = 0;
    if (!pid || pid == 0U || !generation) return 0;
    process = process_get_by_pid(pid);
    if (!process || process->pid != pid ||
        process->event_generation != generation ||
        !service_supervisor_process_state_valid(process->state) ||
        process->state == PROCESS_STATE_ZOMBIE) {
        return 0;
    }
    if (output) *output = process;
    return 1;
}

static int service_supervisor_created_process_cleanup(process_t* process) {
    process_t* current;
    uint32_t pid;

    if (!process || !process->pid || process->pid == 0U) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Invalid created service process");
        return ERR_STATE;
    }
    if (process->state == PROCESS_STATE_UNUSED) return OK;
    current = process_get_by_pid(process->pid);
    if (!current) return OK;
    if (current != process || process == process_get_current()) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Service process cleanup rejected");
        return ERR_STATE;
    }
    pid = process->pid;
    process_destroy(process);
    if (process_get_by_pid(pid)) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Service process survived cleanup");
        return ERR_STATE;
    }
    return OK;
}

static int service_supervisor_cleanup_identity(uint32_t pid,
                                               uint32_t generation) {
    process_t* process;

    if (!pid || !generation) return OK;
    process = process_get_by_pid(pid);
    if (!process || process->event_generation != generation) return OK;
    if (process->pid == 0U || process == process_get_current()) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Service identity cannot be destroyed");
        return ERR_STATE;
    }
    if (!service_supervisor_process_state_valid(process->state)) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Service identity has an invalid process state");
        return ERR_STATE;
    }
    process_destroy(process);
    if (process_get_by_pid(pid)) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Service identity survived cleanup");
        return ERR_STATE;
    }
    return OK;
}

static void service_supervisor_set_fallback(
    service_supervisor_entry_t* entry, uint8_t active) {
    if (!entry) return;
    if (!entry->definition.fallback) {
        entry->fallback_active = 0U;
        return;
    }
    entry->definition.fallback(active ? 1U : 0U);
    entry->fallback_active = active ? 1U : 0U;
}

static void service_supervisor_publish_recovery(
    const service_supervisor_entry_t* entry) {
    recovery_component_id_t component;

    if (!entry) return;
    component = entry->definition.recovery_component;
    if (component >= RECOVERY_COMPONENT_COUNT ||
        recovery_get_count() != RECOVERY_COMPONENT_COUNT) {
        return;
    }
    if (entry->state == SERVICE_SUPERVISOR_READY) {
        (void)recovery_mark_ready(component);
    } else if (entry->fallback_active) {
        (void)recovery_mark_degraded(component, entry->last_error,
                                     "Service unavailable; fallback active");
    } else if (entry->state == SERVICE_SUPERVISOR_STOPPED) {
        (void)recovery_mark_disabled(component, entry->last_error,
                                     "Service stopped");
    } else {
        (void)recovery_mark_disabled(component, entry->last_error,
                                     "Service unavailable; no fallback");
    }
}

static void service_supervisor_record_failure(
    service_supervisor_entry_t* entry, int result) {
    if (!entry) return;
    if (entry->failures != 0xFFFFFFFFU) entry->failures++;
    entry->last_error = result == OK ? ERR_STATE : result;
    service_supervisor_clear_identity(entry);
    entry->state = SERVICE_SUPERVISOR_STARTING;
    service_supervisor_set_fallback(entry, 1U);
}

static void service_supervisor_record_cleanup_failure(
    service_supervisor_entry_t* entry, int result,
    service_supervisor_state_t state) {
    if (!entry) return;
    if (entry->failures != 0xFFFFFFFFU) entry->failures++;
    entry->last_error = result == OK ? ERR_STATE : result;
    entry->cleanup_failed = 1U;
    entry->state = state;
    service_supervisor_set_fallback(entry, 1U);
    service_supervisor_publish_recovery(entry);
}

static void service_supervisor_finish_failure(
    service_supervisor_entry_t* entry, service_supervisor_state_t state) {
    if (!entry) return;
    entry->state = state;
    service_supervisor_publish_recovery(entry);
}

static int service_supervisor_attempt_once(
    service_supervisor_entry_t* entry) {
    process_t* process;
    uint32_t pid;
    uint32_t generation;
    int result;

    if (!entry || !entry->configured || !entry->definition.create ||
        !entry->definition.dependency) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Invalid service startup entry");
        return ERR_STATE;
    }
    entry->state = SERVICE_SUPERVISOR_STARTING;
    service_supervisor_clear_identity(entry);
    result = entry->definition.dependency();
    if (result != OK) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", result,
                       "Service dependency rejected startup");
        return result;
    }
    process = entry->definition.create();
    if (!process) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_MEM,
                       "Service process creation failed");
        return ERR_MEM;
    }
    pid = process->pid;
    generation = process->event_generation;
    if (!pid || pid == 0U || !generation ||
        process_get_by_pid(pid) != process ||
        !service_supervisor_process_state_valid(process->state) ||
        process->state == PROCESS_STATE_ZOMBIE) {
        result = service_supervisor_created_process_cleanup(process);
        if (result != OK && pid && generation) {
            entry->pid = pid;
            entry->generation = generation;
            entry->cleanup_failed = 1U;
        }
        LOG_ERROR_CODE("SERVICE_SUPERVISOR",
                       result == OK ? ERR_STATE : result,
                       "Created service process has invalid identity");
        return result == OK ? ERR_STATE : result;
    }
    if (entry->definition.prepare) {
        result = entry->definition.prepare(process);
        if (result != OK) {
            int cleanup_result = service_supervisor_created_process_cleanup(
                process);

            if (cleanup_result != OK) {
                entry->pid = pid;
                entry->generation = generation;
                entry->cleanup_failed = 1U;
            }
            LOG_ERROR_CODE("SERVICE_SUPERVISOR",
                           cleanup_result == OK ? result : cleanup_result,
                           "Service preparation failed");
            return cleanup_result == OK ? result : cleanup_result;
        }
    }
    if (!service_supervisor_process_live_values(pid, generation, &process)) {
        result = service_supervisor_cleanup_identity(pid, generation);
        if (result != OK) {
            entry->pid = pid;
            entry->generation = generation;
            entry->cleanup_failed = 1U;
        }
        LOG_ERROR_CODE("SERVICE_SUPERVISOR",
                       result == OK ? ERR_STATE : result,
                       "Service identity became invalid during startup");
        return result == OK ? ERR_STATE : result;
    }
    entry->pid = pid;
    entry->generation = generation;
    entry->state = SERVICE_SUPERVISOR_READY;
    service_supervisor_set_fallback(entry, 0U);
    service_supervisor_publish_recovery(entry);
    return OK;
}

static int service_supervisor_activate(service_supervisor_entry_t* entry) {
    int result;

    if (!entry) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_NULL,
                       "Null service activation entry");
        return ERR_NULL;
    }
    entry->cleanup_failed = 0U;
    service_supervisor_clear_stopped_identity(entry);
    result = service_supervisor_attempt_once(entry);
    if (result == OK) return OK;
    if (entry->cleanup_failed) {
        service_supervisor_finish_failure(entry, SERVICE_SUPERVISOR_FAILED);
        return result;
    }
    service_supervisor_record_failure(entry, result);
    if (entry->cleanup_failed || service_supervisor_quiescing ||
        entry->retry_used) {
        service_supervisor_finish_failure(
            entry, service_supervisor_quiescing ?
                   SERVICE_SUPERVISOR_STOPPED : SERVICE_SUPERVISOR_FAILED);
        return result;
    }
    entry->retry_used = 1U;
    if (entry->restart_attempts != 0xFFFFFFFFU) {
        entry->restart_attempts++;
    }
    entry->cleanup_failed = 0U;
    result = service_supervisor_attempt_once(entry);
    if (result == OK) return OK;
    if (entry->cleanup_failed) {
        service_supervisor_finish_failure(entry, SERVICE_SUPERVISOR_FAILED);
        return result;
    }
    service_supervisor_record_failure(entry, result);
    service_supervisor_finish_failure(
        entry, service_supervisor_quiescing ?
               SERVICE_SUPERVISOR_STOPPED : SERVICE_SUPERVISOR_FAILED);
    return result;
}

static int service_supervisor_restart_after_loss(
    service_supervisor_entry_t* entry) {
    int result;

    if (!entry) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_NULL,
                       "Null service restart entry");
        return ERR_NULL;
    }
    if (entry->retry_used) {
        service_supervisor_record_failure(entry, ERR_NOT_FOUND);
        service_supervisor_finish_failure(entry, SERVICE_SUPERVISOR_FAILED);
        return ERR_NOT_FOUND;
    }
    entry->retry_used = 1U;
    if (entry->restart_attempts != 0xFFFFFFFFU) {
        entry->restart_attempts++;
    }
    entry->cleanup_failed = 0U;
    result = service_supervisor_attempt_once(entry);
    if (result == OK) return OK;
    if (entry->cleanup_failed) {
        service_supervisor_finish_failure(entry, SERVICE_SUPERVISOR_FAILED);
        return result;
    }
    service_supervisor_record_failure(entry, result);
    service_supervisor_finish_failure(entry, SERVICE_SUPERVISOR_FAILED);
    return result;
}

int service_supervisor_init(void) {
    if (service_supervisor_initialized) {
        LOG_WARN("SERVICE_SUPERVISOR", "Duplicate initialization rejected");
        return ERR_STATE;
    }
    kmemset(service_entries, 0, sizeof(service_entries));
    kmemset(service_supervisor_test_failures,
            0, sizeof(service_supervisor_test_failures));
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        service_entries[index].definition.id =
            (service_supervisor_id_t)index;
        service_entries[index].state = SERVICE_SUPERVISOR_STOPPED;
        service_entries[index].last_error = OK;
    }
    service_supervisor_started = 0U;
    service_supervisor_quiescing = 0U;
    service_supervisor_initialized = 1U;
    LOG_INFO("SERVICE_SUPERVISOR", "Private service supervisor initialized");
    return OK;
}

int service_supervisor_configure(
    const service_supervisor_definition_t* definition) {
    service_supervisor_entry_t* entry;

    if (!definition) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Null service definition");
        return ERR_NULL;
    }
    if (!service_supervisor_initialized || service_supervisor_started) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Configuration is out of order");
        return ERR_STATE;
    }
    if (!service_supervisor_valid_id(definition->id) || !definition->name ||
        !definition->name[0] || !definition->create ||
        !definition->dependency ||
        definition->recovery_component > RECOVERY_COMPONENT_COUNT) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Invalid service definition");
        return ERR_INVALID;
    }
    uint32_t name_length = 0U;
    while (name_length < SERVICE_SUPERVISOR_NAME_LENGTH &&
           definition->name[name_length]) name_length++;
    if (name_length >= SERVICE_SUPERVISOR_NAME_LENGTH) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Service name exceeds the limit");
        return ERR_OVERFLOW;
    }
    entry = &service_entries[definition->id];
    if (entry->configured) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Duplicate service definition");
        return ERR_STATE;
    }
    service_supervisor_copy_name(entry->name, definition->name);
    entry->definition = *definition;
    entry->definition.name = entry->name;
    entry->configured = 1U;
    return OK;
}

int service_supervisor_start(void) {
    int first_error = OK;

    if (!service_supervisor_initialized || service_supervisor_started) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Service supervisor start rejected");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        if (!service_entries[index].configured) {
            LOG_ERROR("SERVICE_SUPERVISOR", "Service dependency table incomplete");
            return ERR_STATE;
        }
    }
    service_supervisor_started = 1U;
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        int result = service_supervisor_activate(&service_entries[index]);

        if (result != OK && first_error == OK) first_error = result;
    }
    if (service_supervisor_validate_state() != OK && first_error == OK) {
        first_error = ERR_STATE;
    }
    return first_error;
}

int service_supervisor_poll(void) {
    int first_error = OK;

    if (!service_supervisor_initialized || !service_supervisor_started) {
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        service_supervisor_entry_t* entry = &service_entries[index];
        uint32_t pid;
        uint32_t generation;
        process_t* process;
        int result;

        if (entry->state == SERVICE_SUPERVISOR_STOPPED &&
            !service_supervisor_quiescing) {
            result = service_supervisor_cleanup_identity(
                entry->stopped_pid, entry->stopped_generation);
            if (result != OK) {
                service_supervisor_record_cleanup_failure(
                    entry, result, SERVICE_SUPERVISOR_STOPPED);
                if (first_error == OK) first_error = result;
                continue;
            }
            service_supervisor_clear_stopped_identity(entry);
            result = service_supervisor_activate(entry);
            if (result != OK && first_error == OK) first_error = result;
            continue;
        }
        if (entry->state == SERVICE_SUPERVISOR_FAILED &&
            entry->cleanup_failed && entry->pid && entry->generation) {
            result = service_supervisor_cleanup_identity(
                entry->pid, entry->generation);
            if (result != OK) {
                if (first_error == OK) first_error = result;
                continue;
            }
            service_supervisor_clear_identity(entry);
            entry->cleanup_failed = 0U;
            result = service_supervisor_restart_after_loss(entry);
            if (result != OK && first_error == OK) first_error = result;
            continue;
        }
        if (entry->state != SERVICE_SUPERVISOR_READY) continue;
        pid = entry->pid;
        generation = entry->generation;
        if (service_supervisor_test_failures[index]) {
            service_supervisor_test_failures[index] = 0U;
            if (service_supervisor_quiescing) continue;
            result = service_supervisor_cleanup_identity(pid, generation);
            if (result != OK) {
                entry->pid = pid;
                entry->generation = generation;
                service_supervisor_record_cleanup_failure(
                    entry, result, SERVICE_SUPERVISOR_FAILED);
                if (first_error == OK) first_error = result;
                continue;
            }
        } else if (service_supervisor_process_live_values(
                       pid, generation, &process)) {
            continue;
        }
        if (service_supervisor_quiescing) {
            entry->stopped_pid = pid;
            entry->stopped_generation = generation;
            service_supervisor_clear_identity(entry);
            service_supervisor_set_fallback(entry, 1U);
            entry->last_error = ERR_STATE;
            service_supervisor_finish_failure(entry, SERVICE_SUPERVISOR_STOPPED);
            continue;
        }
        result = service_supervisor_cleanup_identity(pid, generation);
        if (result != OK) {
            entry->pid = pid;
            entry->generation = generation;
            service_supervisor_record_cleanup_failure(
                entry, result, SERVICE_SUPERVISOR_FAILED);
            if (first_error == OK) first_error = result;
            continue;
        }
        service_supervisor_clear_identity(entry);
        result = service_supervisor_restart_after_loss(entry);
        if (result != OK && first_error == OK) first_error = result;
    }
    if (first_error != OK) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", first_error,
                       "Service supervisor poll failed");
    }
    return first_error;
}

int service_supervisor_set_quiescing(uint8_t active) {
    if (!service_supervisor_initialized) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Quiescence requested before initialization");
        return ERR_STATE;
    }
    service_supervisor_quiescing = active ? 1U : 0U;
    if (!service_supervisor_quiescing) {
        for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
            service_supervisor_entry_t* entry = &service_entries[index];

            if (entry->state == SERVICE_SUPERVISOR_STOPPED) {
                entry->retry_used = 0U;
                if (!entry->cleanup_failed) entry->last_error = OK;
            }
        }
    }
    return OK;
}

int service_supervisor_get_identity(service_supervisor_id_t id,
                                    uint32_t* pid, uint32_t* generation) {
    service_supervisor_entry_t* entry;

    if (!pid || !generation) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Null identity output");
        return ERR_NULL;
    }
    *pid = 0U;
    *generation = 0U;
    if (!service_supervisor_initialized || !service_supervisor_started) {
        return ERR_STATE;
    }
    if (!service_supervisor_valid_id(id)) return ERR_INVALID;
    entry = &service_entries[id];
    if (entry->state != SERVICE_SUPERVISOR_READY ||
        !service_supervisor_process_live_values(entry->pid,
                                                entry->generation, 0)) {
        return ERR_NOT_FOUND;
    }
    *pid = entry->pid;
    *generation = entry->generation;
    return OK;
}

int service_supervisor_snapshot_copy(
    service_supervisor_id_t id, service_supervisor_snapshot_t* output) {
    service_supervisor_entry_t* entry;

    if (!output) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Null snapshot output");
        return ERR_NULL;
    }
    kmemset(output, 0, sizeof(*output));
    if (!service_supervisor_initialized) return ERR_STATE;
    if (!service_supervisor_valid_id(id)) return ERR_INVALID;
    entry = &service_entries[id];
    output->id = id;
    service_supervisor_copy_name(output->name, entry->definition.name);
    output->state = entry->state;
    output->pid = entry->pid;
    output->generation = entry->generation;
    output->restart_attempts = entry->restart_attempts;
    output->failures = entry->failures;
    output->last_error = entry->last_error;
    output->fallback_active = entry->fallback_active;
    return OK;
}

int service_supervisor_snapshot_list(service_supervisor_snapshot_t* output,
                                     uint32_t capacity, uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Null snapshot count output");
        return ERR_NULL;
    }
    *out_count = 0U;
    if (!service_supervisor_initialized) return ERR_STATE;
    if (!output) {
        LOG_ERROR("SERVICE_SUPERVISOR", "Null snapshot buffer");
        return ERR_NULL;
    }
    if (capacity < SERVICE_SUPERVISOR_ID_COUNT) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        int result = service_supervisor_snapshot_copy(
            (service_supervisor_id_t)index, &output[index]);

        if (result != OK) return result;
    }
    *out_count = SERVICE_SUPERVISOR_ID_COUNT;
    return OK;
}

int service_supervisor_validate_state(void) {
    if (!service_supervisor_initialized || !service_supervisor_started) {
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        const service_supervisor_entry_t* entry = &service_entries[index];

        if (!entry->configured || entry->definition.id != index ||
            !entry->definition.name || !entry->definition.name[0] ||
            !entry->definition.create || !entry->definition.dependency ||
            entry->definition.recovery_component > RECOVERY_COMPONENT_COUNT ||
            entry->state < SERVICE_SUPERVISOR_STARTING ||
            entry->state > SERVICE_SUPERVISOR_STOPPED) {
            LOG_ERROR("SERVICE_SUPERVISOR", "Service table invariant failed");
            return ERR_STATE;
        }
        if (!entry->definition.fallback && entry->fallback_active) {
            LOG_ERROR("SERVICE_SUPERVISOR", "Invalid fallback state");
            return ERR_STATE;
        }
        if (entry->state == SERVICE_SUPERVISOR_READY) {
            if (!service_supervisor_process_live_values(
                    entry->pid, entry->generation, 0) ||
                entry->fallback_active) {
                LOG_ERROR("SERVICE_SUPERVISOR", "Ready service identity is invalid");
                return ERR_STATE;
            }
            for (uint32_t other = 0U; other < index; other++) {
                if (service_entries[other].state == SERVICE_SUPERVISOR_READY &&
                    service_entries[other].pid == entry->pid &&
                    service_entries[other].generation == entry->generation) {
                    LOG_ERROR("SERVICE_SUPERVISOR", "Duplicate service identity");
                    return ERR_STATE;
                }
            }
        } else if (entry->pid || entry->generation) {
            if (!entry->cleanup_failed ||
                !service_supervisor_process_live_values(
                    entry->pid, entry->generation, 0)) {
                LOG_ERROR("SERVICE_SUPERVISOR",
                          "Inactive service retains invalid identity");
                return ERR_STATE;
            }
        }
    }
    return OK;
}

int service_supervisor_is_initialized(void) {
    return service_supervisor_initialized != 0U;
}

const char* service_supervisor_state_name(service_supervisor_state_t state) {
    switch (state) {
        case SERVICE_SUPERVISOR_STARTING: return "STARTING";
        case SERVICE_SUPERVISOR_READY: return "READY";
        case SERVICE_SUPERVISOR_FAILED: return "FAILED";
        case SERVICE_SUPERVISOR_STOPPED: return "STOPPED";
        default: return "UNKNOWN";
    }
}

int service_supervisor_test_fail_next(service_supervisor_id_t id) {
    if (!service_supervisor_initialized || !service_supervisor_started) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Test failpoint requested before supervisor start");
        return ERR_STATE;
    }
    if (!service_supervisor_valid_id(id)) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_INVALID,
                       "Invalid service failpoint identifier");
        return ERR_INVALID;
    }
    if (service_entries[id].state != SERVICE_SUPERVISOR_READY) {
        LOG_ERROR_CODE("SERVICE_SUPERVISOR", ERR_STATE,
                       "Service failpoint requires a ready service");
        return ERR_STATE;
    }
    service_supervisor_test_failures[id] = 1U;
    return OK;
}

#if defined(ZEPHYROS_HOST_TEST)
void service_supervisor_test_reset(void) {
    kmemset(service_entries, 0, sizeof(service_entries));
    kmemset(service_supervisor_test_failures,
            0, sizeof(service_supervisor_test_failures));
    service_supervisor_initialized = 0U;
    service_supervisor_started = 0U;
    service_supervisor_quiescing = 0U;
}
#endif
