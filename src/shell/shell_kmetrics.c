#include "apps/shell_kmetrics.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/serial.h"

#define Q3CHECK_ERROR_PROPAGATION_ONLY 1

#define SHELL_KMETRICS_LINE_CAPACITY 256U
#define SHELL_KMETRICS_MACHINE_PREFIX "@@ZMETRIC/1 "
#define SHELL_KMETRICS_DOMAIN_COUNT 14U
#define SHELL_KMETRICS_ALL_DOMAINS ((1U << SHELL_KMETRICS_DOMAIN_COUNT) - 1U)

typedef enum {
    SHELL_KMETRICS_KIND_COUNTER = 0,
    SHELL_KMETRICS_KIND_GAUGE,
    SHELL_KMETRICS_KIND_BYTES,
    SHELL_KMETRICS_KIND_DURATION,
    SHELL_KMETRICS_KIND_STATE
} shell_kmetrics_kind_t;

static char shell_kmetrics_machine_line[SHELL_KMETRICS_LINE_CAPACITY];
static uint32_t shell_kmetrics_machine_sequence;

static const char* shell_kmetrics_kind_name(shell_kmetrics_kind_t kind) {
    if (kind == SHELL_KMETRICS_KIND_COUNTER) return "counter";
    if (kind == SHELL_KMETRICS_KIND_GAUGE) return "gauge";
    if (kind == SHELL_KMETRICS_KIND_BYTES) return "bytes";
    if (kind == SHELL_KMETRICS_KIND_DURATION) return "duration";
    return "state";
}

static int shell_kmetrics_append_text(char* output, uint32_t* length,
                                      uint32_t capacity, const char* text) {
    uint32_t text_length;

    if (!output || !length || !text) return ERR_NULL;
    text_length = kstrlen(text);
    if (*length >= capacity || text_length >= capacity - *length) {
        return ERR_OVERFLOW;
    }
    kmemcpy(output + *length, text, text_length);
    *length += text_length;
    output[*length] = '\0';
    return OK;
}

static int shell_kmetrics_append_u32(char* output, uint32_t* length,
                                     uint32_t capacity, uint32_t value) {
    char digits[11];
    uint32_t digit_count = 0U;

    if (value == 0U) {
        digits[digit_count++] = '0';
    } else {
        while (value > 0U && digit_count < sizeof(digits)) {
            digits[digit_count++] = (char)('0' + (value % 10U));
            value /= 10U;
        }
    }
    while (digit_count > 0U) {
        char digit[2];

        digit[0] = digits[--digit_count];
        digit[1] = '\0';
        if (shell_kmetrics_append_text(output, length, capacity, digit) != OK) {
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

static int shell_kmetrics_emit_line(uint32_t length) {
    uint32_t written;

    if (!serial_is_ready()) return ERR_UNAVAILABLE;
    written = serial_write_text(shell_kmetrics_machine_line, length);
    serial_flush(SERIAL_TX_CAPACITY);
    if (written != length) return ERR_OVERFLOW;
    return OK;
}

static int shell_kmetrics_line_start(const char* record, uint32_t* length) {
    *length = 0U;
    shell_kmetrics_machine_line[0] = '\0';
    if (shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line),
                                   SHELL_KMETRICS_MACHINE_PREFIX) != OK) {
        return ERR_OVERFLOW;
    }
    if (shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line),
                                   "record=") != OK) {
        return ERR_OVERFLOW;
    }
    return shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                      sizeof(shell_kmetrics_machine_line),
                                      record);
}

static int shell_kmetrics_append_field(const char* key, const char* value,
                                       uint32_t* length) {
    if (shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line),
                                   " ") != OK ||
        shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line), key) != OK ||
        shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line), "=") != OK) {
        return ERR_OVERFLOW;
    }
    return shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                      sizeof(shell_kmetrics_machine_line), value);
}

static int shell_kmetrics_append_u32_field(const char* key, uint32_t value,
                                           uint32_t* length) {
    if (shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line), " ") != OK ||
        shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line), key) != OK ||
        shell_kmetrics_append_text(shell_kmetrics_machine_line, length,
                                   sizeof(shell_kmetrics_machine_line), "=") != OK) {
        return ERR_OVERFLOW;
    }
    return shell_kmetrics_append_u32(shell_kmetrics_machine_line, length,
                                     sizeof(shell_kmetrics_machine_line), value);
}

static int shell_kmetrics_emit_record_begin(uint32_t sequence,
                                            uint8_t baseline_valid) {
    uint32_t length;

    if (shell_kmetrics_line_start("begin", &length) != OK ||
        shell_kmetrics_append_u32_field("seq", sequence, &length) != OK ||
        shell_kmetrics_append_field("baseline",
                                    baseline_valid ? "reset" : "boot",
                                    &length) != OK ||
        shell_kmetrics_append_field("source", "guest", &length) != OK) {
        return ERR_OVERFLOW;
    }
    shell_kmetrics_machine_line[length++] = '\n';
    return shell_kmetrics_emit_line(length);
}

static int shell_kmetrics_emit_record_end(uint32_t sequence, uint8_t partial) {
    uint32_t length;

    if (shell_kmetrics_line_start("end", &length) != OK ||
        shell_kmetrics_append_u32_field("seq", sequence, &length) != OK ||
        shell_kmetrics_append_field("status", partial ? "partial" : "ok",
                                    &length) != OK) {
        return ERR_OVERFLOW;
    }
    shell_kmetrics_machine_line[length++] = '\n';
    return shell_kmetrics_emit_line(length);
}

static int shell_kmetrics_emit_metric(const char* name, const char* value,
                                      const char* unit,
                                      shell_kmetrics_kind_t kind,
                                      const char* source, const char* context,
                                      uint8_t available) {
    uint32_t length;

    if (shell_kmetrics_line_start("metric", &length) != OK ||
        shell_kmetrics_append_field("metric", name, &length) != OK ||
        shell_kmetrics_append_field("value", value, &length) != OK ||
        shell_kmetrics_append_field("unit", unit, &length) != OK ||
        shell_kmetrics_append_field("kind", shell_kmetrics_kind_name(kind),
                                    &length) != OK ||
        shell_kmetrics_append_field("source", source, &length) != OK ||
        shell_kmetrics_append_field("context", context, &length) != OK ||
        shell_kmetrics_append_field("status", available ? "ok" : "unavailable",
                                    &length) != OK ||
        shell_kmetrics_append_field("resolution", "1", &length) != OK ||
        shell_kmetrics_append_field(
            "overflow", kind == SHELL_KMETRICS_KIND_COUNTER ||
                         kind == SHELL_KMETRICS_KIND_BYTES ?
                         "wrap_u32" : "none", &length) != OK) {
        return ERR_OVERFLOW;
    }
    shell_kmetrics_machine_line[length++] = '\n';
    return shell_kmetrics_emit_line(length);
}

static int shell_kmetrics_emit_u32(const char* name, uint32_t current,
                                   uint32_t baseline, uint8_t baseline_valid,
                                   uint8_t available, const char* unit,
                                   shell_kmetrics_kind_t kind,
                                   const char* source, const char* context) {
    char value[12];
    uint32_t length = 0U;

    value[0] = '\0';
    if (!available) {
        return shell_kmetrics_emit_metric(name, "ND", unit, kind, source,
                                           context, 0U);
    }
    if (kind == SHELL_KMETRICS_KIND_COUNTER ||
        kind == SHELL_KMETRICS_KIND_BYTES) {
        current = baseline_valid ? current - baseline : current;
    }
    if (shell_kmetrics_append_u32(value, &length, sizeof(value), current) != OK) {
        return ERR_OVERFLOW;
    }
    return shell_kmetrics_emit_metric(name, value, unit, kind, source,
                                      context, 1U);
}

static int shell_kmetrics_emit_indexed_u32(const char* prefix, uint32_t index,
                                           const char* suffix, uint32_t current,
                                           uint32_t baseline,
                                           uint8_t baseline_valid,
                                           uint8_t available,
                                           const char* unit,
                                           shell_kmetrics_kind_t kind,
                                           const char* source,
                                           const char* context) {
    char name[64];
    uint32_t length = 0U;

    name[0] = '\0';
    if (shell_kmetrics_append_text(name, &length, sizeof(name), prefix) != OK ||
        shell_kmetrics_append_u32(name, &length, sizeof(name), index) != OK ||
        shell_kmetrics_append_text(name, &length, sizeof(name), suffix) != OK) {
        return ERR_OVERFLOW;
    }
    return shell_kmetrics_emit_u32(name, current, baseline, baseline_valid,
                                   available, unit, kind, source, context);
}

static void shell_kmetrics_capture_irq(shell_kmetrics_snapshot_t* snapshot) {
    uint32_t index;

    for (index = 0U; index < IDT_IRQ_LINE_COUNT; index++) {
        snapshot->irq_valid[index] =
            idt_get_irq_status((uint8_t)index, &snapshot->irq[index]) == OK;
    }
    snapshot->deferred_result = irq_deferred_get_status(&snapshot->deferred);
    for (index = 0U; index < IRQ_DEFERRED_IRQ_COUNT; index++) {
        snapshot->deferred_irq_valid[index] =
            irq_deferred_get_irq_status((uint8_t)index,
                                        &snapshot->deferred_irq[index]) == OK;
    }
}

static void shell_kmetrics_capture_process(
    shell_kmetrics_snapshot_t* snapshot) {
    process_t* current = process_get_current();

    snapshot->process_count = process_get_count();
    snapshot->user_process_count = process_get_user_count();
    snapshot->state_counts[PROCESS_STATE_UNUSED] =
        process_get_state_count(PROCESS_STATE_UNUSED);
    snapshot->state_counts[PROCESS_STATE_READY] =
        process_get_state_count(PROCESS_STATE_READY);
    snapshot->state_counts[PROCESS_STATE_RUNNING] =
        process_get_state_count(PROCESS_STATE_RUNNING);
    snapshot->state_counts[PROCESS_STATE_BLOCKED] =
        process_get_state_count(PROCESS_STATE_BLOCKED);
    snapshot->state_counts[PROCESS_STATE_ZOMBIE] =
        process_get_state_count(PROCESS_STATE_ZOMBIE);
    snapshot->resource_result =
        process_resource_validate_all(&snapshot->resource_validation);
    snapshot->stack_result =
        process_stack_validate_all(&snapshot->stack_validation);
    snapshot->current_resource_valid = 0U;
    if (current) {
        snapshot->current_resource_valid =
            process_resource_snapshot_copy(current->pid,
                                           current->event_generation,
                                           &snapshot->current_resource) == OK;
    }
    snapshot->credentials_valid =
        process_credentials_current(&snapshot->credentials) == OK;
    snapshot->permissions_result = fs_permissions_validate();
    snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_PROCESS;
    if (snapshot->resource_result == OK && snapshot->stack_result == OK &&
        snapshot->credentials_valid && snapshot->permissions_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_SECURITY;
    }
}

int shell_kmetrics_take_snapshot(shell_kmetrics_snapshot_t* snapshot) {
    uint32_t start_ticks;
    uint32_t index;
    uint8_t recovery_available = 1U;

    if (!snapshot) {
        LOG_ERROR("SHELL", "Destino nulo ao capturar metricas PERF1");
        return ERR_NULL;
    }
    kmemset(snapshot, 0, sizeof(*snapshot));
    process_yield();
    start_ticks = timer_get_ticks();
    snapshot->ticks = start_ticks;
    snapshot->frequency = timer_get_frequency();
    snapshot->workqueue_result = workqueue_get_stats(&snapshot->workqueue);
    if (snapshot->workqueue_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_WORKQUEUE;
    }
    keyboard_get_metrics(&snapshot->keyboard);
    snapshot->keyboard_flow_result =
        keyboard_get_flow_metrics(&snapshot->keyboard_flow);
    scheduler_get_stats(&snapshot->scheduler);
    snapshot->scheduler_runtime_result =
        scheduler_get_runtime_stats(&snapshot->scheduler_runtime);
    ipc_get_stats(&snapshot->ipc);
    shell_kmetrics_capture_irq(snapshot);
    snapshot->input_result = input_get_metrics(&snapshot->input);
    snapshot->input_flow_result =
        input_get_flow_metrics(&snapshot->input_flow);
    snapshot->mouse_status_result = mouse_get_status(&snapshot->mouse_status);
    snapshot->mouse_flow_result =
        mouse_get_flow_metrics(&snapshot->mouse_flow);
    if (snapshot->input_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_INPUT;
    }
    snapshot->job_result = shell_job_get_status(&snapshot->job);
    if (snapshot->job_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_JOB;
    }
    vesa_get_metrics(&snapshot->vesa);
    snapshot->vesa_available = vesa_get_mode() != NULL &&
                              vesa_get_mode()->initialized;
    snapshot->vesa_backbuffer = vesa_has_backbuffer() ? 1U : 0U;
    snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_VIDEO;
    memory_get_heap_stats(&snapshot->heap);
    memory_get_pmm_stats(&snapshot->pmm);
    snapshot->memory_detailed_result =
        memory_get_detailed_stats(&snapshot->memory_detailed);
    kmem_cache_get_stats(&snapshot->slab);
    paging_get_user_stats(&snapshot->paging_user);
    snapshot->paging_boot_result = paging_get_boot_stats(&snapshot->paging_boot);
    if (snapshot->paging_boot_result != OK) {
        LOG_WARN("SHELL", "Metricas de bootstrap do paging indisponiveis");
    }
    snapshot->update_capabilities_result =
        update_get_capabilities(&snapshot->update_capabilities);
    snapshot->update_status_result =
        update_get_status(&snapshot->update_status);
    snapshot->update_slots_result =
        update_system_slots_get_status(&snapshot->update_slots);
    if (snapshot->update_capabilities_result == OK &&
        snapshot->update_status_result == OK &&
        snapshot->update_slots_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_UPDATE;
    }
    if (snapshot->paging_boot_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_MEMORY;
    }
    snapshot->vfs_result = vfs_get_status(&snapshot->vfs);
    if (snapshot->vfs_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_VFS;
    }
    snapshot->block_result = block_get_stats(&snapshot->block);
    if (snapshot->block_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_BLOCK;
    }
    snapshot->cache_result = block_cache_get_stats(&snapshot->cache);
    snapshot->durability_result =
        block_cache_get_durability_status(&snapshot->durability);
    if (snapshot->cache_result == OK && snapshot->durability_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_CACHE;
    }
    snapshot->ethernet_result = ethernet_get_status(&snapshot->ethernet);
    snapshot->network_result =
        network_manager_get_status(&snapshot->network);
    snapshot->net_buffer_result =
        net_buffer_get_stats(&snapshot->net_buffer);
    snapshot->sk_buff_result = skb_get_stats(&snapshot->sk_buff);
    snapshot->socket_result = socket_get_status(&snapshot->sockets);
    snapshot->net_socket_result =
        net_socket_get_status(&snapshot->net_sockets);
    snapshot->route_result = route_get_status(&snapshot->routes);
    if (snapshot->ethernet_result == OK && snapshot->network_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_NETWORK;
    }
    snapshot->service_result = service_supervisor_snapshot_list(
        snapshot->services, SERVICE_SUPERVISOR_ID_COUNT,
        &snapshot->service_count);
    if (snapshot->service_result == OK) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_SERVICES;
    }
    shell_kmetrics_capture_process(snapshot);
    snapshot->recovery_count = recovery_get_count();
    if (snapshot->recovery_count > RECOVERY_COMPONENT_COUNT) {
        snapshot->recovery_count = RECOVERY_COMPONENT_COUNT;
    }
    for (index = 0U; index < snapshot->recovery_count; index++) {
        const recovery_component_t* component =
            recovery_get((recovery_component_id_t)index);

        if (!component) {
            recovery_available = 0U;
            continue;
        }
        snapshot->recovery_valid[index] = 1U;
        snapshot->recovery[index].state = component->state;
        snapshot->recovery[index].failures = component->failures;
        snapshot->recovery[index].last_error = component->last_error;
    }
    if (snapshot->recovery_count > 0U && recovery_available) {
        snapshot->valid_domains |= SHELL_KMETRICS_DOMAIN_RECOVERY;
    }
    snapshot->capture_ticks = timer_get_ticks() - start_ticks;
    return OK;
}

#define SHELL_KMETRICS_EMIT_U32(name, current, base, valid, unit, kind, source, context) \
    do { \
        int shell_kmetrics_result = shell_kmetrics_emit_u32( \
            name, current, base, baseline_valid, valid, unit, kind, source, context); \
        if (shell_kmetrics_result != OK) return shell_kmetrics_result; \
    } while (0)

static int shell_kmetrics_emit_scheduler(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    uint32_t index;
    uint8_t runtime_available = current->scheduler_runtime_result == OK;
    uint8_t runtime_baseline = baseline &&
                               baseline->scheduler_runtime_result == OK;

    SHELL_KMETRICS_EMIT_U32("pit_ticks", current->ticks,
                            baseline ? baseline->ticks : 0U, 1U, "tick",
                            SHELL_KMETRICS_KIND_COUNTER, "pit", "kernel");
    SHELL_KMETRICS_EMIT_U32("pit_frequency_hz", current->frequency, 0U, 1U,
                            "hz", SHELL_KMETRICS_KIND_GAUGE, "pit", "kernel");
    SHELL_KMETRICS_EMIT_U32("capture_ticks", current->capture_ticks, 0U, 1U,
                            "tick", SHELL_KMETRICS_KIND_DURATION, "pit",
                            "diagnostic");
    SHELL_KMETRICS_EMIT_U32("rdtsc_cycles", 0U, 0U, 0U, "cycle",
                            SHELL_KMETRICS_KIND_COUNTER, "rdtsc", "kernel");
    SHELL_KMETRICS_EMIT_U32("pmu_cycles", 0U, 0U, 0U, "cycle",
                            SHELL_KMETRICS_KIND_COUNTER, "pmu", "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_context_switches",
                            current->scheduler.context_switches,
                            baseline ? baseline->scheduler.context_switches : 0U,
                            1U, "count", SHELL_KMETRICS_KIND_COUNTER,
                            "scheduler", "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_cooperative_yields",
                            current->scheduler.cooperative_yields,
                            baseline ? baseline->scheduler.cooperative_yields : 0U,
                            1U, "count", SHELL_KMETRICS_KIND_COUNTER,
                            "scheduler", "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_user_preemptions",
                            current->scheduler.user_preemptions,
                            baseline ? baseline->scheduler.user_preemptions : 0U,
                            1U, "count", SHELL_KMETRICS_KIND_COUNTER,
                            "scheduler", "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_idle_fallbacks",
                            current->scheduler.idle_fallbacks,
                            baseline ? baseline->scheduler.idle_fallbacks : 0U,
                            1U, "count", SHELL_KMETRICS_KIND_COUNTER,
                            "scheduler", "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_user_quantum_ticks",
                            current->scheduler.user_quantum_ticks, 0U, 1U,
                            "tick", SHELL_KMETRICS_KIND_GAUGE, "scheduler",
                            "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_idle_ticks",
                            current->scheduler.idle_ticks,
                            baseline ? baseline->scheduler.idle_ticks : 0U, 1U,
                            "tick", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_active_ticks",
                            current->scheduler.active_ticks,
                            baseline ? baseline->scheduler.active_ticks : 0U, 1U,
                            "tick", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_idle_entries",
                            current->scheduler_runtime.idle_entries,
                            baseline ? baseline->scheduler_runtime.idle_entries : 0U,
                            baseline_valid && runtime_available && runtime_baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "idle");
    SHELL_KMETRICS_EMIT_U32("scheduler_idle_hlt_returns",
                            current->scheduler_runtime.idle_hlt_returns,
                            baseline ? baseline->scheduler_runtime.idle_hlt_returns : 0U,
                            baseline_valid && runtime_available && runtime_baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "idle");
    SHELL_KMETRICS_EMIT_U32("scheduler_wakeups",
                            current->scheduler_runtime.wakeups,
                            baseline ? baseline->scheduler_runtime.wakeups : 0U,
                            baseline_valid && runtime_available && runtime_baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "wait");
    SHELL_KMETRICS_EMIT_U32("scheduler_wake_latency_samples",
                            current->scheduler_runtime.wake_latency_samples,
                            baseline ? baseline->scheduler_runtime.wake_latency_samples : 0U,
                            baseline_valid && runtime_available && runtime_baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "wait");
    SHELL_KMETRICS_EMIT_U32("scheduler_wake_latency_ticks",
                            current->scheduler_runtime.wake_latency_total_ticks,
                            baseline ? baseline->scheduler_runtime.wake_latency_total_ticks : 0U,
                            baseline_valid && runtime_available && runtime_baseline,
                            "tick", SHELL_KMETRICS_KIND_COUNTER, "scheduler",
                            "wait");
    SHELL_KMETRICS_EMIT_U32("scheduler_wake_latency_max_ticks",
                            current->scheduler_runtime.wake_latency_max_ticks, 0U,
                            runtime_available, "tick", SHELL_KMETRICS_KIND_DURATION,
                            "scheduler", "wait");
    SHELL_KMETRICS_EMIT_U32("scheduler_ready_peak",
                            current->scheduler_runtime.ready_peak, 0U,
                            runtime_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "scheduler", "process");
    SHELL_KMETRICS_EMIT_U32("scheduler_blocked_peak",
                            current->scheduler_runtime.blocked_peak, 0U,
                            runtime_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "scheduler", "process");
    SHELL_KMETRICS_EMIT_U32("scheduler_current_pid",
                            current->scheduler_runtime.current_pid, 0U,
                            runtime_available, "pid", SHELL_KMETRICS_KIND_STATE,
                            "scheduler", "kernel");
    SHELL_KMETRICS_EMIT_U32("scheduler_last_error",
                            (uint32_t)current->scheduler_runtime.last_error, 0U,
                            runtime_available, "code", SHELL_KMETRICS_KIND_STATE,
                            "scheduler", "kernel");
    for (index = PROCESS_STATE_UNUSED; index <= PROCESS_STATE_ZOMBIE; index++) {
        SHELL_KMETRICS_EMIT_U32(
            index == PROCESS_STATE_UNUSED ? "process_unused" :
            index == PROCESS_STATE_READY ? "process_ready" :
            index == PROCESS_STATE_RUNNING ? "process_running" :
            index == PROCESS_STATE_BLOCKED ? "process_blocked" :
            "process_zombie", current->state_counts[index], 0U, 1U, "count",
            SHELL_KMETRICS_KIND_GAUGE, "process", "scheduler");
    }
    SHELL_KMETRICS_EMIT_U32("process_count", current->process_count, 0U, 1U,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "process",
                            "scheduler");
    SHELL_KMETRICS_EMIT_U32("user_process_count", current->user_process_count,
                            0U, 1U, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process", "scheduler");
    for (index = 0U; index < IDT_IRQ_LINE_COUNT; index++) {
        if (shell_kmetrics_emit_indexed_u32(
                "irq_", index, "_occurrences", current->irq[index].occurrences,
                baseline ? baseline->irq[index].occurrences : 0U,
                baseline_valid, current->irq_valid[index], "count",
                SHELL_KMETRICS_KIND_COUNTER, "idt", "irq") != OK ||
            shell_kmetrics_emit_indexed_u32(
                "irq_", index, "_handlers",
                current->irq[index].registered_handlers, 0U, 0U,
                current->irq_valid[index], "count", SHELL_KMETRICS_KIND_GAUGE,
                "idt", "irq") != OK) {
            return ERR_OVERFLOW;
        }
    }
    SHELL_KMETRICS_EMIT_U32("deferred_queued", current->deferred.queued, 0U,
                            current->deferred_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_running", current->deferred.running, 0U,
                            current->deferred_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_capacity", current->deferred.capacity,
                            0U, current->deferred_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_scheduled", current->deferred.scheduled,
                            baseline ? baseline->deferred.scheduled : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_dispatched", current->deferred.dispatched,
                            baseline ? baseline->deferred.dispatched : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_coalesced", current->deferred.coalesced,
                            baseline ? baseline->deferred.coalesced : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_reruns", current->deferred.reruns,
                            baseline ? baseline->deferred.reruns : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_cancelled", current->deferred.cancelled,
                            baseline ? baseline->deferred.cancelled : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_rejected", current->deferred.rejected,
                            baseline ? baseline->deferred.rejected : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_peak_queued",
                            current->deferred.peak_queued, 0U,
                            current->deferred_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "irq_deferred", "irq");
    SHELL_KMETRICS_EMIT_U32("deferred_context_errors",
                            current->deferred.context_errors,
                            baseline ? baseline->deferred.context_errors : 0U,
                            baseline_valid && current->deferred_result == OK &&
                                baseline,
                            "count", SHELL_KMETRICS_KIND_COUNTER,
                            "irq_deferred", "irq");
    for (index = 0U; index < IRQ_DEFERRED_IRQ_COUNT; index++) {
        if (shell_kmetrics_emit_indexed_u32(
                "deferred_irq_", index, "_scheduled",
                current->deferred_irq[index].scheduled,
                baseline ? baseline->deferred_irq[index].scheduled : 0U,
                baseline_valid && current->deferred_irq_valid[index] && baseline &&
                    baseline->deferred_irq_valid[index],
                current->deferred_irq_valid[index], "count",
                SHELL_KMETRICS_KIND_COUNTER, "irq_deferred", "irq") ||
            shell_kmetrics_emit_indexed_u32(
                "deferred_irq_", index, "_dispatched",
                current->deferred_irq[index].dispatched,
                baseline ? baseline->deferred_irq[index].dispatched : 0U,
                baseline_valid && current->deferred_irq_valid[index] && baseline &&
                    baseline->deferred_irq_valid[index],
                current->deferred_irq_valid[index], "count",
                SHELL_KMETRICS_KIND_COUNTER, "irq_deferred", "irq") ||
            shell_kmetrics_emit_indexed_u32(
                "deferred_irq_", index, "_coalesced",
                current->deferred_irq[index].coalesced,
                baseline ? baseline->deferred_irq[index].coalesced : 0U,
                baseline_valid && current->deferred_irq_valid[index] && baseline &&
                    baseline->deferred_irq_valid[index],
                current->deferred_irq_valid[index], "count",
                SHELL_KMETRICS_KIND_COUNTER, "irq_deferred", "irq") ||
            shell_kmetrics_emit_indexed_u32(
                "deferred_irq_", index, "_rejected",
                current->deferred_irq[index].rejected,
                baseline ? baseline->deferred_irq[index].rejected : 0U,
                baseline_valid && current->deferred_irq_valid[index] && baseline &&
                    baseline->deferred_irq_valid[index],
                current->deferred_irq_valid[index], "count",
                SHELL_KMETRICS_KIND_COUNTER, "irq_deferred", "irq")) {
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

static int shell_kmetrics_emit_input(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    uint8_t available = current->input_result == OK;
    uint8_t input_flow_available = current->input_flow_result == OK;
    uint8_t keyboard_flow_available = current->keyboard_flow_result == OK;
    uint8_t mouse_flow_available = current->mouse_flow_result == OK;
    uint8_t mouse_status_available = current->mouse_status_result == OK;

    SHELL_KMETRICS_EMIT_U32("keyboard_queued", current->keyboard.queued, 0U,
                            1U, "count", SHELL_KMETRICS_KIND_GAUGE, "keyboard",
                            "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_capacity", current->keyboard.capacity, 0U,
                            1U, "count", SHELL_KMETRICS_KIND_GAUGE, "keyboard",
                            "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_dropped", current->keyboard.dropped,
                            baseline ? baseline->keyboard.dropped : 0U,
                            baseline_valid, "count", SHELL_KMETRICS_KIND_COUNTER,
                            "keyboard", "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_processed", current->keyboard.processed,
                            baseline ? baseline->keyboard.processed : 0U,
                            baseline_valid, "count", SHELL_KMETRICS_KIND_COUNTER,
                            "keyboard", "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_peak_queued", current->keyboard.peak_queued,
                            0U, 1U, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "keyboard", "input");
    SHELL_KMETRICS_EMIT_U32("input_key_queued", current->input.key_queued, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "input", "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_queued",
                            current->input.pointer_queued, 0U, available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "input", "input");
    SHELL_KMETRICS_EMIT_U32("input_key_published",
                            current->input.key_published,
                            baseline ? baseline->input.key_published : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "input", "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_published",
                            current->input.pointer_published,
                            baseline ? baseline->input.pointer_published : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "input", "input");
    SHELL_KMETRICS_EMIT_U32("input_key_processed",
                            current->input.key_processed,
                            baseline ? baseline->input.key_processed : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "input", "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_processed",
                            current->input.pointer_processed,
                            baseline ? baseline->input.pointer_processed : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "input", "input");
    SHELL_KMETRICS_EMIT_U32("input_key_peak_queued",
                            current->input.key_peak_queued, 0U, available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_peak_queued",
                            current->input.pointer_peak_queued, 0U, available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_key_dropped", current->input.key_dropped,
                            baseline ? baseline->input.key_dropped : 0U,
                            baseline_valid && available &&
                                baseline->input_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_dropped",
                            current->input.pointer_dropped,
                            baseline ? baseline->input.pointer_dropped : 0U,
                            baseline_valid && available &&
                                baseline->input_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_key_coalesced",
                            current->input_flow.key_coalesced,
                            baseline ? baseline->input_flow.key_coalesced : 0U,
                            baseline_valid && input_flow_available && baseline &&
                                baseline->input_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_coalesced",
                            current->input_flow.pointer_coalesced,
                            baseline ? baseline->input_flow.pointer_coalesced : 0U,
                            baseline_valid && input_flow_available && baseline &&
                                baseline->input_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_key_rejected",
                            current->input_flow.key_rejected,
                            baseline ? baseline->input_flow.key_rejected : 0U,
                            baseline_valid && input_flow_available && baseline &&
                                baseline->input_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("input_pointer_rejected",
                            current->input_flow.pointer_rejected,
                            baseline ? baseline->input_flow.pointer_rejected : 0U,
                            baseline_valid && input_flow_available && baseline &&
                                baseline->input_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "input",
                            "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_raw_queued",
                            current->keyboard_flow.raw_queued, 0U,
                            keyboard_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "keyboard", "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_raw_capacity",
                            current->keyboard_flow.raw_capacity, 0U,
                            keyboard_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "keyboard", "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_raw_dropped",
                            current->keyboard_flow.raw_dropped,
                            baseline ? baseline->keyboard_flow.raw_dropped : 0U,
                            baseline_valid && keyboard_flow_available && baseline &&
                                baseline->keyboard_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "keyboard",
                            "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_raw_processed",
                            current->keyboard_flow.raw_processed,
                            baseline ? baseline->keyboard_flow.raw_processed : 0U,
                            baseline_valid && keyboard_flow_available && baseline &&
                                baseline->keyboard_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "keyboard",
                            "input");
    SHELL_KMETRICS_EMIT_U32("keyboard_raw_peak_queued",
                            current->keyboard_flow.raw_peak_queued, 0U,
                            keyboard_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "keyboard", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_raw_queued",
                            current->mouse_flow.raw_queued, 0U,
                            mouse_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_raw_capacity",
                            current->mouse_flow.raw_capacity, 0U,
                            mouse_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_raw_dropped",
                            current->mouse_flow.raw_dropped,
                            baseline ? baseline->mouse_flow.raw_dropped : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_raw_processed",
                            current->mouse_flow.raw_processed,
                            baseline ? baseline->mouse_flow.raw_processed : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_raw_peak_queued",
                            current->mouse_flow.raw_peak_queued, 0U,
                            mouse_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_packets_decoded",
                            current->mouse_flow.packets_decoded,
                            baseline ? baseline->mouse_flow.packets_decoded : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_packets_dropped",
                            current->mouse_flow.packets_dropped,
                            baseline ? baseline->mouse_flow.packets_dropped : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_queue_queued",
                            current->mouse_flow.queue_queued, 0U,
                            mouse_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_queue_capacity",
                            current->mouse_flow.queue_capacity, 0U,
                            mouse_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_queue_peak_queued",
                            current->mouse_flow.queue_peak_queued, 0U,
                            mouse_flow_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_queue_coalesced",
                            current->mouse_flow.queue_coalesced,
                            baseline ? baseline->mouse_flow.queue_coalesced : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_queue_rejected",
                            current->mouse_flow.queue_rejected,
                            baseline ? baseline->mouse_flow.queue_rejected : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_move_events",
                            current->mouse_flow.move_events,
                            baseline ? baseline->mouse_flow.move_events : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_press_events",
                            current->mouse_flow.press_events,
                            baseline ? baseline->mouse_flow.press_events : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_release_events",
                            current->mouse_flow.release_events,
                            baseline ? baseline->mouse_flow.release_events : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_wheel_events",
                            current->mouse_flow.wheel_events,
                            baseline ? baseline->mouse_flow.wheel_events : 0U,
                            baseline_valid && mouse_flow_available && baseline &&
                                baseline->mouse_flow_result == OK,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "mouse",
                            "input");
    SHELL_KMETRICS_EMIT_U32("mouse_initialized",
                            current->mouse_status.initialized, 0U,
                            mouse_status_available, "state",
                            SHELL_KMETRICS_KIND_STATE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_button_state",
                            current->mouse_status.effective_buttons, 0U,
                            mouse_status_available, "state",
                            SHELL_KMETRICS_KIND_STATE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_raw_button_state",
                            current->mouse_status.raw_buttons, 0U,
                            mouse_status_available, "state",
                            SHELL_KMETRICS_KIND_STATE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_wheel_supported",
                            current->mouse_status.wheel_supported, 0U,
                            mouse_status_available, "state",
                            SHELL_KMETRICS_KIND_STATE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("mouse_last_error",
                            (uint32_t)current->mouse_status.last_error, 0U,
                            mouse_status_available, "code",
                            SHELL_KMETRICS_KIND_STATE, "mouse", "input");
    SHELL_KMETRICS_EMIT_U32("ipc_pending", ipc_get_pending_count(), 0U, 1U,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "ipc", "shell");
    SHELL_KMETRICS_EMIT_U32("ipc_capacity", IPC_MSG_QUEUE_SIZE - 1U, 0U, 1U,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "ipc", "shell");
    SHELL_KMETRICS_EMIT_U32("ipc_sent", current->ipc.sent,
                            baseline ? baseline->ipc.sent : 0U, baseline_valid,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "ipc", "shell");
    SHELL_KMETRICS_EMIT_U32("ipc_received", current->ipc.received,
                            baseline ? baseline->ipc.received : 0U, baseline_valid,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "ipc", "shell");
    SHELL_KMETRICS_EMIT_U32("ipc_failed", current->ipc.failed,
                            baseline ? baseline->ipc.failed : 0U, baseline_valid,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "ipc", "shell");
    SHELL_KMETRICS_EMIT_U32("ipc_queue_full", current->ipc.queue_full,
                            baseline ? baseline->ipc.queue_full : 0U, baseline_valid,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "ipc", "shell");
    return OK;
}

static int shell_kmetrics_emit_work(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const workqueue_stats_t* work = &current->workqueue;
    const workqueue_stats_t* base = baseline ? &baseline->workqueue : 0;
    uint8_t available = current->workqueue_result == OK;

    SHELL_KMETRICS_EMIT_U32("workqueue_worker_bound", work->worker_bound, 0U,
                            available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_worker_active", work->worker_active, 0U,
                            available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_fallback_active", work->fallback_active,
                            0U, available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "workqueue", "fallback");
    SHELL_KMETRICS_EMIT_U32("workqueue_worker_pid", work->worker_pid, 0U,
                            available, "pid", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_execution_context",
                            work->execution_context, 0U, available, "enum",
                            SHELL_KMETRICS_KIND_STATE, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_pending", work->ready_high +
                            work->ready_normal + work->delayed + work->running,
                            0U, available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_ready_high", work->ready_high, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_ready_normal", work->ready_normal, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_delayed", work->delayed, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_running", work->running, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_scheduled", work->scheduled,
                            base ? base->scheduled : 0U, baseline_valid && available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "workqueue",
                            "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_executed", work->executed,
                            base ? base->executed : 0U, baseline_valid && available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "workqueue",
                            "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_coalesced", work->coalesced,
                            base ? base->coalesced : 0U, baseline_valid && available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "workqueue",
                            "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_cancelled", work->cancelled,
                            base ? base->cancelled : 0U, baseline_valid && available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "workqueue",
                            "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_callback_errors", work->callback_errors,
                            base ? base->callback_errors : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_rejected", work->rejected,
                            base ? base->rejected : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_context_errors", work->context_errors,
                            base ? base->context_errors : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_invariant_errors", work->invariant_errors,
                            base ? base->invariant_errors : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_wakeups", work->wakeups,
                            base ? base->wakeups : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_wake_errors", work->wake_errors,
                            base ? base->wake_errors : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_sleeps", work->sleeps,
                            base ? base->sleeps : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_peak_pending", work->peak_pending, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "workqueue", "shell");
    SHELL_KMETRICS_EMIT_U32("workqueue_callback_ticks", work->total_callback_ticks,
                            base ? base->total_callback_ticks : 0U,
                            baseline_valid && available, "tick",
                            SHELL_KMETRICS_KIND_DURATION, "workqueue", "job");
    SHELL_KMETRICS_EMIT_U32("workqueue_max_callback_ticks",
                            work->max_callback_ticks, 0U, available, "tick",
                            SHELL_KMETRICS_KIND_DURATION, "workqueue", "job");
    SHELL_KMETRICS_EMIT_U32("workqueue_dispatch_latency_samples",
                            work->dispatch_latency_samples,
                            base ? base->dispatch_latency_samples : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_dispatch_latency_ticks",
                            work->dispatch_latency_total_ticks,
                            base ? base->dispatch_latency_total_ticks : 0U,
                            baseline_valid && available, "tick",
                            SHELL_KMETRICS_KIND_COUNTER, "workqueue", "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_max_dispatch_latency_ticks",
                            work->max_dispatch_latency_ticks, 0U, available,
                            "tick", SHELL_KMETRICS_KIND_DURATION, "workqueue",
                            "kworker");
    SHELL_KMETRICS_EMIT_U32("workqueue_last_error",
                            (uint32_t)work->last_error, 0U, available, "code",
                            SHELL_KMETRICS_KIND_STATE, "workqueue", "kworker");
    return OK;
}

static int shell_kmetrics_emit_job(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const shell_job_status_t* job = &current->job;
    const shell_job_status_t* base = baseline ? &baseline->job : 0;
    uint8_t available = current->job_result == OK;

    SHELL_KMETRICS_EMIT_U32("job_active", job->active, 0U, available, "bool",
                            SHELL_KMETRICS_KIND_STATE, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_state", job->state, 0U, available, "enum",
                            SHELL_KMETRICS_KIND_STATE, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_kind", job->kind, 0U, available, "enum",
                            SHELL_KMETRICS_KIND_STATE, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_progress", job->progress, 0U, available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_total", job->total, 0U, available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_completed_ticks", job->completed_ticks,
                            base ? base->completed_ticks : 0U,
                            baseline_valid && available, "tick",
                            SHELL_KMETRICS_KIND_DURATION, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_blocked_events", job->blocked_events,
                            base ? base->blocked_events : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_cancel_requests", job->cancel_requests,
                            base ? base->cancel_requests : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_wakeups", job->wakeups,
                            base ? base->wakeups : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "shell_job", "job");
    SHELL_KMETRICS_EMIT_U32("job_stale_events", job->stale_events,
                            base ? base->stale_events : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "shell_job", "job");
    return OK;
}

static int shell_kmetrics_emit_memory(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const memory_heap_stats_t* heap = &current->heap;
    const memory_heap_stats_t* base_heap = baseline ? &baseline->heap : 0;
    const memory_pmm_stats_t* pmm = &current->pmm;
    const memory_pmm_stats_t* base_pmm = baseline ? &baseline->pmm : 0;
    const memory_detailed_stats_t* detailed = &current->memory_detailed;
    const memory_detailed_stats_t* base_detailed =
        baseline ? &baseline->memory_detailed : 0;
    const kmem_slab_stats_t* slab = &current->slab;
    const kmem_slab_stats_t* base_slab = baseline ? &baseline->slab : 0;
    const paging_user_stats_t* user = &current->paging_user;
    const paging_user_stats_t* base_user = baseline ? &baseline->paging_user : 0;
    uint8_t heap_available = heap->initialized && heap->valid;
    uint8_t detailed_available = current->memory_detailed_result == OK &&
                                 detailed->initialized && detailed->valid;
    uint8_t slab_available = slab->initialized && slab->valid;
    uint8_t pmm_available = pmm->initialized;
    uint8_t paging_user_available = user->initialized;
    uint8_t boot_available = current->paging_boot_result == OK &&
                             current->paging_boot.initialized;
    uint8_t base_heap_available = base_heap && base_heap->initialized &&
                                  base_heap->valid;
    uint8_t base_pmm_available = base_pmm && base_pmm->initialized;
    uint8_t base_detailed_available =
        base_detailed && baseline->memory_detailed_result == OK &&
        base_detailed->initialized && base_detailed->valid;
    uint8_t base_slab_available = base_slab && base_slab->initialized &&
                                  base_slab->valid;
    uint8_t base_paging_user_available =
        base_user && base_user->initialized;

    SHELL_KMETRICS_EMIT_U32("memory_heap_used_bytes", heap->used_bytes, 0U,
                            heap_available, "byte",
                            SHELL_KMETRICS_KIND_GAUGE, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_free_bytes", heap->free_bytes, 0U,
                            heap_available, "byte",
                            SHELL_KMETRICS_KIND_GAUGE, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_total_bytes", heap->total_bytes, 0U,
                            heap_available, "byte",
                            SHELL_KMETRICS_KIND_GAUGE, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_allocated_blocks",
                            heap->allocated_blocks, 0U, heap_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_free_blocks", heap->free_blocks, 0U,
                            heap_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_largest_free_block",
                            heap->largest_free_block, 0U, heap_available, "byte",
                            SHELL_KMETRICS_KIND_GAUGE, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_fragmentation_percent",
                            heap->fragmentation_percent, 0U, heap_available,
                            "percent", SHELL_KMETRICS_KIND_GAUGE, "heap",
                            "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_allocation_failures",
                            heap->allocation_failures,
                            base_heap_available ? base_heap->allocation_failures : 0U,
                            baseline_valid && base_heap_available && heap_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_invalid_frees", heap->invalid_frees,
                            base_heap_available ? base_heap->invalid_frees : 0U,
                            baseline_valid && base_heap_available && heap_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_double_frees", heap->double_frees,
                            base_heap_available ? base_heap->double_frees : 0U,
                            baseline_valid && base_heap_available && heap_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_initialized", heap->initialized, 0U,
                            heap_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_heap_valid", heap->valid, 0U,
                            heap_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "heap", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_pmm_owned_pages", pmm->owned_pages, 0U,
                            pmm_available, "page", SHELL_KMETRICS_KIND_GAUGE,
                            "pmm", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_pmm_allocation_failures",
                            pmm->allocation_failures,
                            base_pmm_available ? base_pmm->allocation_failures : 0U,
                            baseline_valid && base_pmm_available && pmm_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "pmm", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_pmm_invalid_frees", pmm->invalid_frees,
                            base_pmm_available ? base_pmm->invalid_frees : 0U,
                            baseline_valid && base_pmm_available && pmm_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "pmm", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_pmm_initialized", pmm->initialized, 0U,
                            pmm_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "pmm", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_detailed_total_pages", detailed->total_pages,
                            0U, detailed_available, "page",
                            SHELL_KMETRICS_KIND_GAUGE, "memory", "memory");
    for (uint32_t index = 0U; index < MEMORY_ZONE_COUNT; index++) {
        if (shell_kmetrics_emit_indexed_u32(
                "memory_zone_", index, "_pages", detailed->zone_pages[index],
                base_detailed_available ? base_detailed->zone_pages[index] : 0U,
                baseline_valid && base_detailed_available && detailed_available,
                detailed_available, "page", SHELL_KMETRICS_KIND_GAUGE,
                "memory", "memory") != OK) {
            return ERR_OVERFLOW;
        }
    }
    SHELL_KMETRICS_EMIT_U32("memory_detailed_free_runs", detailed->free_runs,
                            0U, detailed_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "memory", "memory");
    SHELL_KMETRICS_EMIT_U32("memory_detailed_largest_free_run",
                            detailed->largest_free_run, 0U, detailed_available,
                            "page", SHELL_KMETRICS_KIND_GAUGE, "memory",
                            "memory");
    SHELL_KMETRICS_EMIT_U32("memory_detailed_isolated_free_pages",
                            detailed->isolated_free_pages, 0U, detailed_available,
                            "page", SHELL_KMETRICS_KIND_GAUGE, "memory",
                            "memory");
    SHELL_KMETRICS_EMIT_U32("memory_detailed_fragmentation_percent",
                            detailed->fragmentation_percent, 0U,
                            detailed_available, "percent",
                            SHELL_KMETRICS_KIND_GAUGE, "memory", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_caches", slab->caches, 0U, slab_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_slabs", slab->slabs, 0U, slab_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_pages", slab->pages, 0U, slab_available,
                            "page", SHELL_KMETRICS_KIND_GAUGE, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_active_objects", slab->active_objects, 0U,
                            slab_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_capacity", slab->capacity, 0U, slab_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_allocation_failures",
                            slab->allocation_failures,
                            base_slab_available ? base_slab->allocation_failures : 0U,
                            baseline_valid && base_slab_available && slab_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_invalid_frees", slab->invalid_frees,
                            base_slab_available ? base_slab->invalid_frees : 0U,
                            baseline_valid && base_slab_available && slab_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_double_frees", slab->double_frees,
                            base_slab_available ? base_slab->double_frees : 0U,
                            baseline_valid && base_slab_available && slab_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_initialized", slab->initialized, 0U,
                            slab_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("slab_valid", slab->valid, 0U, slab_available, "bool",
                            SHELL_KMETRICS_KIND_STATE, "slab", "memory");
    SHELL_KMETRICS_EMIT_U32("paging_user_active_directories",
                            user->active_directories, 0U,
                            paging_user_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "paging", "memory");
    SHELL_KMETRICS_EMIT_U32("paging_user_active_pages", user->active_pages, 0U,
                            paging_user_available, "page",
                            SHELL_KMETRICS_KIND_GAUGE, "paging", "memory");
    SHELL_KMETRICS_EMIT_U32("paging_user_directories_created",
                            user->directories_created,
                            base_paging_user_available ? base_user->directories_created : 0U,
                            baseline_valid && base_paging_user_available &&
                                paging_user_available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "paging", "memory");
    SHELL_KMETRICS_EMIT_U32("paging_boot_identity_pages",
                            current->paging_boot.identity_pages, 0U,
                            boot_available, "page", SHELL_KMETRICS_KIND_GAUGE,
                            "paging", "boot");
    SHELL_KMETRICS_EMIT_U32("paging_boot_page_tables",
                            current->paging_boot.page_tables_created, 0U,
                            boot_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "paging", "boot");
    SHELL_KMETRICS_EMIT_U32("paging_boot_init_ticks",
                            current->paging_boot.init_ticks, 0U, boot_available,
                            "tick", SHELL_KMETRICS_KIND_DURATION, "paging", "boot");
    return OK;
}

static int shell_kmetrics_emit_storage(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const vfs_status_t* vfs = &current->vfs;
    const vfs_status_t* base_vfs = baseline ? &baseline->vfs : 0;
    const block_queue_stats_t* block = &current->block;
    const block_queue_stats_t* base_block = baseline ? &baseline->block : 0;
    const block_cache_stats_t* cache = &current->cache;
    const block_cache_stats_t* base_cache = baseline ? &baseline->cache : 0;
    uint8_t vfs_available = current->vfs_result == OK;
    uint8_t block_available = current->block_result == OK;
    uint8_t cache_available = current->cache_result == OK;
    uint8_t durability_available = current->durability_result == OK;
    uint8_t base_vfs_available = baseline && baseline->vfs_result == OK;
    uint8_t base_block_available = baseline && baseline->block_result == OK;
    uint8_t base_cache_available = baseline && baseline->cache_result == OK;

    SHELL_KMETRICS_EMIT_U32("vfs_initialized", vfs->initialized, 0U,
                            vfs_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_descriptor_capacity",
                            vfs->descriptor_capacity, 0U, vfs_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_global_file_capacity",
                            vfs->global_file_capacity, 0U, vfs_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_global_files_used", vfs->global_files_used,
                            0U, vfs_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_processes_with_tables",
                            vfs->processes_with_tables, 0U, vfs_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_descriptors_open", vfs->descriptors_open, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_opens", vfs->opens,
                            base_vfs_available ? base_vfs->opens : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_reads", vfs->reads,
                            base_vfs_available ? base_vfs->reads : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_writes", vfs->writes,
                            base_vfs_available ? base_vfs->writes : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_failures", vfs->failures,
                            base_vfs_available ? base_vfs->failures : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_seeks", vfs->seeks,
                            base_vfs_available ? base_vfs->seeks : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_closes", vfs->closes,
                            base_vfs_available ? base_vfs->closes : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_mount_capacity", vfs->mount_capacity, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_mounts_active", vfs->mounts_active, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_lookups", vfs->lookups,
                            base_vfs_available ? base_vfs->lookups : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_chdirs", vfs->chdirs,
                            base_vfs_available ? base_vfs->chdirs : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_ioctls", vfs->ioctls,
                            base_vfs_available ? base_vfs->ioctls : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_device_capacity", vfs->device_capacity, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_devices_active", vfs->devices_active, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_pipe_capacity", vfs->pipe_capacity, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_pipes_active", vfs->pipes_active, 0U,
                            vfs_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_pipe_reads", vfs->pipe_reads,
                            base_vfs_available ? base_vfs->pipe_reads : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("vfs_pipe_writes", vfs->pipe_writes,
                            base_vfs_available ? base_vfs->pipe_writes : 0U,
                            baseline_valid && base_vfs_available && vfs_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "vfs", "storage");
    SHELL_KMETRICS_EMIT_U32("block_queue_depth", block->queue_depth, 0U,
                            block_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_queue_capacity", block->queue_capacity, 0U,
                            block_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_in_flight", block->in_flight, 0U,
                            block_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_peak_depth", block->peak_depth, 0U,
                            block_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_submitted", block->submitted,
                            base_block_available ? base_block->submitted : 0U,
                            baseline_valid && base_block_available && block_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_completed", block->completed,
                            base_block_available ? base_block->completed : 0U,
                            baseline_valid && base_block_available && block_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_failed", block->failed,
                            base_block_available ? base_block->failed : 0U,
                            baseline_valid && base_block_available && block_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_cancelled", block->cancelled,
                            base_block_available ? base_block->cancelled : 0U,
                            baseline_valid && base_block_available && block_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_merged", block->merged,
                            base_block_available ? base_block->merged : 0U,
                            baseline_valid && base_block_available && block_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_read_sectors", block->read_sectors,
                            base_block_available ? base_block->read_sectors : 0U,
                            baseline_valid && base_block_available && block_available,
                            "sector",
                            SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_write_sectors", block->write_sectors,
                            base_block_available ? base_block->write_sectors : 0U,
                            baseline_valid && base_block_available && block_available,
                            "sector",
                            SHELL_KMETRICS_KIND_COUNTER, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_read_sectors_per_second",
                            block->read_sectors_per_second, 0U, block_available,
                            "sector_per_second", SHELL_KMETRICS_KIND_GAUGE,
                            "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_write_sectors_per_second",
                            block->write_sectors_per_second, 0U, block_available,
                            "sector_per_second", SHELL_KMETRICS_KIND_GAUGE,
                            "block", "storage");
    SHELL_KMETRICS_EMIT_U32("block_last_error", (uint32_t)block->last_error, 0U,
                            block_available && block->last_error >= 0, "error",
                            SHELL_KMETRICS_KIND_STATE, "block", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_capacity", cache->capacity, 0U,
                            cache_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_block_size", cache->block_size, 0U,
                            cache_available, "byte", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_memory_bytes", cache->memory_bytes, 0U,
                            cache_available, "byte", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_entries", cache->entries, 0U, cache_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_valid_entries", cache->valid_entries, 0U,
                            cache_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_reading_entries", cache->reading_entries, 0U,
                            cache_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_dirty_entries", cache->dirty_entries, 0U,
                            cache_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_writeback_entries", cache->writeback_entries,
                            0U, cache_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_pinned_entries", cache->pinned_entries, 0U,
                            cache_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_hits", cache->hits,
                            base_cache_available ? base_cache->hits : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_misses", cache->misses,
                            base_cache_available ? base_cache->misses : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_reads_avoided", cache->reads_avoided,
                            base_cache_available ? base_cache->reads_avoided : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_physical_reads", cache->physical_reads,
                            base_cache_available ? base_cache->physical_reads : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_physical_writes", cache->physical_writes,
                            base_cache_available ? base_cache->physical_writes : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_hit_rate_percent", cache->hit_rate_percent,
                            0U, cache_available, "percent",
                            SHELL_KMETRICS_KIND_GAUGE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_dirty_bytes", cache->dirty_bytes, 0U,
                            cache_available, "byte", SHELL_KMETRICS_KIND_GAUGE,
                            "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_evictions", cache->evictions,
                            base_cache_available ? base_cache->evictions : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_invalidations", cache->invalidations,
                            base_cache_available ? base_cache->invalidations : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_bypasses", cache->bypasses,
                            base_cache_available ? base_cache->bypasses : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_errors", cache->errors,
                            base_cache_available ? base_cache->errors : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_writeback_failures", cache->writeback_failures,
                            base_cache_available ? base_cache->writeback_failures : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count",
                            SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_writeback_attempts", cache->writeback_attempts,
                            base_cache_available ? base_cache->writeback_attempts : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_writeback_completed", cache->writeback_completed,
                            base_cache_available ? base_cache->writeback_completed : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_sync_operations", cache->sync_operations,
                            base_cache_available ? base_cache->sync_operations : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_flush_operations", cache->flush_operations,
                            base_cache_available ? base_cache->flush_operations : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_flush_unavailable", cache->flush_unavailable,
                            base_cache_available ? base_cache->flush_unavailable : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_degraded_syncs", cache->degraded_syncs,
                            base_cache_available ? base_cache->degraded_syncs : 0U,
                            baseline_valid && base_cache_available && cache_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_last_error", (uint32_t)cache->last_error, 0U,
                            cache_available && cache->last_error >= 0, "error",
                            SHELL_KMETRICS_KIND_STATE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_last_sync_error",
                            (uint32_t)cache->last_sync_error, 0U,
                            cache_available && cache->last_sync_error >= 0, "error",
                            SHELL_KMETRICS_KIND_STATE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_durability_state",
                            current->durability.state, 0U,
                            current->durability_result == OK, "enum",
                            SHELL_KMETRICS_KIND_STATE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("cache_flush_supported",
                            current->durability.flush_supported, 0U,
                            current->durability_result == OK, "bool",
                            SHELL_KMETRICS_KIND_STATE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("durability_devices_checked",
                            current->durability.devices_checked, 0U,
                            durability_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("durability_flush_supported",
                            current->durability.flush_supported, 0U,
                            durability_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("durability_flush_unavailable",
                            current->durability.flush_unavailable, 0U,
                            durability_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "block_cache", "storage");
    SHELL_KMETRICS_EMIT_U32("durability_last_error",
                            (uint32_t)current->durability.last_error, 0U,
                            durability_available &&
                                current->durability.last_error >= 0,
                            "error", SHELL_KMETRICS_KIND_STATE, "block_cache",
                            "storage");
    return OK;
}

static int shell_kmetrics_emit_network(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const ethernet_status_t* ethernet = &current->ethernet;
    const ethernet_status_t* base_ethernet = baseline ? &baseline->ethernet : 0;
    const network_manager_status_t* network = &current->network;
    const net_buffer_stats_t* buffers = &current->net_buffer;
    const net_buffer_stats_t* base_buffers =
        baseline ? &baseline->net_buffer : 0;
    const sk_buff_stats_t* sk_buff = &current->sk_buff;
    const sk_buff_stats_t* base_sk_buff = baseline ? &baseline->sk_buff : 0;
    const socket_status_t* sockets = &current->sockets;
    const socket_status_t* base_sockets = baseline ? &baseline->sockets : 0;
    const net_socket_status_t* net_sockets = &current->net_sockets;
    const net_socket_status_t* base_net_sockets =
        baseline ? &baseline->net_sockets : 0;
    const route_status_t* routes = &current->routes;
    const route_status_t* base_routes = baseline ? &baseline->routes : 0;
    uint8_t available = current->ethernet_result == OK &&
                        current->network_result == OK;
    uint8_t ethernet_available = current->ethernet_result == OK;
    uint8_t buffers_available = current->net_buffer_result == OK &&
                                buffers->initialized;
    uint8_t sk_buff_available = current->sk_buff_result == OK &&
                                sk_buff->initialized;
    uint8_t sockets_available = current->socket_result == OK &&
                                sockets->initialized;
    uint8_t net_sockets_available = current->net_socket_result == OK &&
                                    net_sockets->initialized;
    uint8_t routes_available = current->route_result == OK && routes->initialized;
    uint8_t base_ethernet_available = baseline &&
                                      baseline->ethernet_result == OK;
    uint8_t base_buffers_available = baseline &&
                                     baseline->net_buffer_result == OK &&
                                     base_buffers->initialized;
    uint8_t base_sk_buff_available = baseline &&
                                     baseline->sk_buff_result == OK &&
                                     base_sk_buff->initialized;
    uint8_t base_sockets_available = baseline &&
                                     baseline->socket_result == OK &&
                                     base_sockets->initialized;
    uint8_t base_net_sockets_available = baseline &&
                                         baseline->net_socket_result == OK &&
                                         base_net_sockets->initialized;
    uint8_t base_routes_available = baseline &&
                                    baseline->route_result == OK &&
                                    base_routes->initialized;

    SHELL_KMETRICS_EMIT_U32("network_interfaces", network->interface_count, 0U,
                            available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "network", "network");
    SHELL_KMETRICS_EMIT_U32("network_active_interfaces", network->active_count,
                            0U, available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "network", "network");
    SHELL_KMETRICS_EMIT_U32("network_packet_io_available",
                            network->packet_io_available, 0U, available, "bool",
                            SHELL_KMETRICS_KIND_STATE, "network", "network");
    SHELL_KMETRICS_EMIT_U32("network_ipv4_configured",
                            network->ipv4_configured, 0U, available, "bool",
                            SHELL_KMETRICS_KIND_STATE, "network", "network");
    SHELL_KMETRICS_EMIT_U32("network_tcp_connections",
                            network->tcp_connection_count, 0U, available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "network", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_interfaces", ethernet->interface_count,
                            0U, ethernet_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_handlers", ethernet->handler_count,
                            0U, ethernet_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_polls", ethernet->polls,
                            base_ethernet_available ? base_ethernet->polls : 0U,
                            baseline_valid && base_ethernet_available &&
                                ethernet_available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_poll_errors", ethernet->poll_errors,
                            base_ethernet_available ? base_ethernet->poll_errors : 0U,
                            baseline_valid && base_ethernet_available &&
                                ethernet_available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_rx_frames", ethernet->rx_frames,
                            base_ethernet_available ? base_ethernet->rx_frames : 0U,
                            baseline_valid && base_ethernet_available &&
                                ethernet_available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_rx_delivered", ethernet->rx_delivered,
                            base_ethernet_available ? base_ethernet->rx_delivered : 0U,
                            baseline_valid && base_ethernet_available &&
                                ethernet_available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("ethernet_tx_frames", ethernet->tx_frames,
                            base_ethernet_available ? base_ethernet->tx_frames : 0U,
                            baseline_valid && base_ethernet_available &&
                                ethernet_available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "ethernet", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_initialized", buffers->initialized, 0U,
                            buffers_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_active_buffers", buffers->active_buffers,
                            0U, buffers_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_peak_buffers", buffers->peak_buffers,
                            0U, buffers_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_allocations", buffers->allocations,
                            base_buffers_available ? base_buffers->allocations : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_frees", buffers->frees,
                            base_buffers_available ? base_buffers->frees : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_delivered", buffers->delivered,
                            base_buffers_available ? base_buffers->delivered : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_dropped", buffers->dropped,
                            base_buffers_available ? base_buffers->dropped : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_copies", buffers->copies,
                            base_buffers_available ? base_buffers->copies : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_copied_bytes", buffers->copied_bytes,
                            base_buffers_available ? base_buffers->copied_bytes : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_clones", buffers->clones,
                            base_buffers_available ? base_buffers->clones : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_fragments", buffers->fragments,
                            base_buffers_available ? base_buffers->fragments : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_invalid_transitions",
                            buffers->invalid_transitions,
                            base_buffers_available ? base_buffers->invalid_transitions : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_duplicate_completions",
                            buffers->duplicate_completions,
                            base_buffers_available ? base_buffers->duplicate_completions : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_ref_acquires", buffers->ref_acquires,
                            base_buffers_available ? base_buffers->ref_acquires : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_ref_releases", buffers->ref_releases,
                            base_buffers_available ? base_buffers->ref_releases : 0U,
                            baseline_valid && base_buffers_available && buffers_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("net_buffer_last_error",
                            (uint32_t)buffers->last_error, 0U,
                            buffers_available && buffers->last_error >= 0, "error",
                            SHELL_KMETRICS_KIND_STATE, "net_buffer", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_initialized", sk_buff->initialized, 0U,
                            sk_buff_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_active_buffers", sk_buff->active_buffers,
                            0U, sk_buff_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_peak_buffers", sk_buff->peak_buffers, 0U,
                            sk_buff_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_allocations", sk_buff->allocations,
                            base_sk_buff_available ? base_sk_buff->allocations : 0U,
                            baseline_valid && base_sk_buff_available && sk_buff_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_frees", sk_buff->frees,
                            base_sk_buff_available ? base_sk_buff->frees : 0U,
                            baseline_valid && base_sk_buff_available && sk_buff_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_completions", sk_buff->completions,
                            base_sk_buff_available ? base_sk_buff->completions : 0U,
                            baseline_valid && base_sk_buff_available && sk_buff_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_drops", sk_buff->drops,
                            base_sk_buff_available ? base_sk_buff->drops : 0U,
                            baseline_valid && base_sk_buff_available && sk_buff_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_invalid_operations",
                            sk_buff->invalid_operations,
                            base_sk_buff_available ? base_sk_buff->invalid_operations : 0U,
                            baseline_valid && base_sk_buff_available && sk_buff_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("sk_buff_last_error", (uint32_t)sk_buff->last_error,
                            0U, sk_buff_available && sk_buff->last_error >= 0,
                            "error", SHELL_KMETRICS_KIND_STATE, "sk_buff", "network");
    SHELL_KMETRICS_EMIT_U32("socket_initialized", sockets->initialized, 0U,
                            sockets_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_active_count", sockets->active_count, 0U,
                            sockets_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_peak_count", sockets->peak_count, 0U,
                            sockets_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_creates", sockets->creates,
                            base_sockets_available ? base_sockets->creates : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_closes", sockets->closes,
                            base_sockets_available ? base_sockets->closes : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_binds", sockets->binds,
                            base_sockets_available ? base_sockets->binds : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_connects", sockets->connects,
                            base_sockets_available ? base_sockets->connects : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_accepts", sockets->accepts,
                            base_sockets_available ? base_sockets->accepts : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_sends", sockets->sends,
                            base_sockets_available ? base_sockets->sends : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_receives", sockets->receives,
                            base_sockets_available ? base_sockets->receives : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_bytes_sent", sockets->bytes_sent,
                            base_sockets_available ? base_sockets->bytes_sent : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_bytes_received", sockets->bytes_received,
                            base_sockets_available ? base_sockets->bytes_received : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_queue_drops", sockets->queue_drops,
                            base_sockets_available ? base_sockets->queue_drops : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_stale_fds", sockets->stale_fds,
                            base_sockets_available ? base_sockets->stale_fds : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_failures", sockets->failures,
                            base_sockets_available ? base_sockets->failures : 0U,
                            baseline_valid && base_sockets_available && sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("socket_last_error", (uint32_t)sockets->last_error, 0U,
                            sockets_available && sockets->last_error >= 0, "error",
                            SHELL_KMETRICS_KIND_STATE, "socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_initialized", net_sockets->initialized, 0U,
                            net_sockets_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_active_count", net_sockets->active_count,
                            0U, net_sockets_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_opens", net_sockets->opens,
                            base_net_sockets_available ? base_net_sockets->opens : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_connects", net_sockets->connects,
                            base_net_sockets_available ? base_net_sockets->connects : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_closes", net_sockets->closes,
                            base_net_sockets_available ? base_net_sockets->closes : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_aborts", net_sockets->aborts,
                            base_net_sockets_available ? base_net_sockets->aborts : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_bytes_queued_tx", net_sockets->bytes_queued_tx,
                            base_net_sockets_available ? base_net_sockets->bytes_queued_tx : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_bytes_sent_tcp", net_sockets->bytes_sent_tcp,
                            base_net_sockets_available ? base_net_sockets->bytes_sent_tcp : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_bytes_received_tcp",
                            net_sockets->bytes_received_tcp,
                            base_net_sockets_available ? base_net_sockets->bytes_received_tcp : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_bytes_read", net_sockets->bytes_read,
                            base_net_sockets_available ? base_net_sockets->bytes_read : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "byte", SHELL_KMETRICS_KIND_BYTES, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_rx_overflows", net_sockets->rx_overflows,
                            base_net_sockets_available ? base_net_sockets->rx_overflows : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_stale_handles", net_sockets->stale_handles,
                            base_net_sockets_available ? base_net_sockets->stale_handles : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_maintenance_cycles",
                            net_sockets->maintenance_cycles,
                            base_net_sockets_available ? base_net_sockets->maintenance_cycles : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_wait_calls", net_sockets->wait_calls,
                            base_net_sockets_available ? base_net_sockets->wait_calls : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_wait_events", net_sockets->wait_events,
                            base_net_sockets_available ? base_net_sockets->wait_events : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_wait_timeouts", net_sockets->wait_timeouts,
                            base_net_sockets_available ? base_net_sockets->wait_timeouts : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_wait_cancellations",
                            net_sockets->wait_cancellations,
                            base_net_sockets_available ? base_net_sockets->wait_cancellations : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_wait_failures", net_sockets->wait_failures,
                            base_net_sockets_available ? base_net_sockets->wait_failures : 0U,
                            baseline_valid && base_net_sockets_available && net_sockets_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("net_socket_last_error",
                            (uint32_t)net_sockets->last_error, 0U,
                            net_sockets_available && net_sockets->last_error >= 0,
                            "error", SHELL_KMETRICS_KIND_STATE, "net_socket", "network");
    SHELL_KMETRICS_EMIT_U32("route_initialized", routes->initialized, 0U,
                            routes_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_entry_count", routes->entry_count, 0U,
                            routes_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_lookups", routes->lookups,
                            base_routes_available ? base_routes->lookups : 0U,
                            baseline_valid && base_routes_available && routes_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_matches", routes->matches,
                            base_routes_available ? base_routes->matches : 0U,
                            baseline_valid && base_routes_available && routes_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_misses", routes->misses,
                            base_routes_available ? base_routes->misses : 0U,
                            baseline_valid && base_routes_available && routes_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_adds", routes->adds,
                            base_routes_available ? base_routes->adds : 0U,
                            baseline_valid && base_routes_available && routes_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_deletes", routes->deletes,
                            base_routes_available ? base_routes->deletes : 0U,
                            baseline_valid && base_routes_available && routes_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_replacements", routes->replacements,
                            base_routes_available ? base_routes->replacements : 0U,
                            baseline_valid && base_routes_available && routes_available,
                            "count", SHELL_KMETRICS_KIND_COUNTER, "route", "network");
    SHELL_KMETRICS_EMIT_U32("route_last_error", (uint32_t)routes->last_error, 0U,
                            routes_available && routes->last_error >= 0, "error",
                            SHELL_KMETRICS_KIND_STATE, "route", "network");
    return OK;
}

static int shell_kmetrics_emit_system(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const process_resource_snapshot_t* resource = &current->current_resource;
    uint32_t index;
    uint8_t resource_available = current->current_resource_valid;
    uint8_t security_available = current->credentials_valid &&
                                 current->permissions_result == OK;

    SHELL_KMETRICS_EMIT_U32("resource_checked", current->resource_validation.checked,
                            0U, current->resource_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "process_resource", "system");
    SHELL_KMETRICS_EMIT_U32("resource_valid", current->resource_validation.valid,
                            0U, current->resource_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "process_resource", "system");
    SHELL_KMETRICS_EMIT_U32("resource_invalid", current->resource_validation.invalid,
                            0U, current->resource_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "process_resource", "system");
    SHELL_KMETRICS_EMIT_U32("resource_current_descriptors", resource->descriptors,
                            0U, resource_available, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_descriptor_limit",
                            resource->descriptor_limit, 0U, resource_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_children", resource->children, 0U,
                            resource_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_pipes", resource->pipes, 0U,
                            resource_available, "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_ipc_pending",
                            resource->ipc_pending, 0U, resource_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_ipc_limit",
                            resource->ipc_pending_limit, 0U, resource_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_resident_pages",
                            resource->resident_pages, 0U, resource_available,
                            "page", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_resident_limit",
                            resource->resident_limit_pages, 0U,
                            resource_available, "page",
                            SHELL_KMETRICS_KIND_GAUGE, "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_anonymous_bytes",
                            resource->anonymous_bytes, 0U, resource_available,
                            "byte", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_anonymous_limit",
                            resource->anonymous_limit_bytes, 0U,
                            resource_available, "byte",
                            SHELL_KMETRICS_KIND_GAUGE, "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_dynamic_vmas",
                            resource->dynamic_vmas, 0U, resource_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_dynamic_vma_limit",
                            resource->dynamic_vma_limit, 0U, resource_available,
                            "count", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("resource_current_image_bytes",
                            resource->image_bytes, 0U, resource_available,
                            "byte", SHELL_KMETRICS_KIND_GAUGE,
                            "process_resource", "process");
    SHELL_KMETRICS_EMIT_U32("credentials_valid", current->credentials_valid, 0U,
                            security_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "credentials", "security");
    SHELL_KMETRICS_EMIT_U32("credentials_uid", current->credentials.uid, 0U,
                            security_available, "uid", SHELL_KMETRICS_KIND_GAUGE,
                            "credentials", "security");
    SHELL_KMETRICS_EMIT_U32("credentials_gid", current->credentials.gid, 0U,
                            security_available, "gid", SHELL_KMETRICS_KIND_GAUGE,
                            "credentials", "security");
    SHELL_KMETRICS_EMIT_U32("credentials_capabilities",
                            current->credentials.capabilities, 0U,
                            security_available, "bitmap",
                            SHELL_KMETRICS_KIND_STATE, "credentials", "security");
    SHELL_KMETRICS_EMIT_U32("permissions_valid",
                            current->permissions_result == OK, 0U,
                            security_available, "bool", SHELL_KMETRICS_KIND_STATE,
                            "permissions", "security");
    SHELL_KMETRICS_EMIT_U32("stack_checked", current->stack_validation.checked,
                            0U, current->stack_result == OK, "count",
                            SHELL_KMETRICS_KIND_GAUGE, "process", "system");
    for (index = 0U; index < SERVICE_SUPERVISOR_ID_COUNT; index++) {
        uint8_t service_available = current->service_result == OK &&
                                    index < current->service_count;

        if (shell_kmetrics_emit_indexed_u32(
                "service_", index, "_state", current->services[index].state,
                0U, 0U, service_available, "enum",
                SHELL_KMETRICS_KIND_STATE, "supervisor", "system") != OK ||
            shell_kmetrics_emit_indexed_u32(
                "service_", index, "_failures", current->services[index].failures,
                baseline && index < baseline->service_count ?
                baseline->services[index].failures : 0U,
                baseline_valid && index < baseline->service_count,
                service_available, "count",
                SHELL_KMETRICS_KIND_COUNTER, "supervisor", "system") != OK) {
            return ERR_OVERFLOW;
        }
        if (shell_kmetrics_emit_indexed_u32(
                "service_", index, "_restart_attempts",
                current->services[index].restart_attempts,
                baseline && index < baseline->service_count ?
                baseline->services[index].restart_attempts : 0U,
                baseline_valid && index < baseline->service_count,
                service_available, "count",
                SHELL_KMETRICS_KIND_COUNTER, "supervisor", "system") != OK ||
            shell_kmetrics_emit_indexed_u32(
                "service_", index, "_fallback_active",
                current->services[index].fallback_active, 0U, 0U,
                service_available, "bool", SHELL_KMETRICS_KIND_STATE,
                "supervisor", "system") != OK ||
            shell_kmetrics_emit_indexed_u32(
                "service_", index, "_last_error",
                (uint32_t)current->services[index].last_error, 0U, 0U,
                service_available && current->services[index].last_error >= 0,
                "error", SHELL_KMETRICS_KIND_STATE, "supervisor", "system") != OK) {
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

static int shell_kmetrics_emit_video(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    const vesa_metrics_t* video = &current->vesa;
    const vesa_metrics_t* base = baseline ? &baseline->vesa : 0;
    uint8_t available = current->vesa_available && current->vesa_backbuffer;

    SHELL_KMETRICS_EMIT_U32("vesa_available", current->vesa_available, 0U, 1U,
                            "bool", SHELL_KMETRICS_KIND_STATE, "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_backbuffer", current->vesa_backbuffer, 0U, 1U,
                            "bool", SHELL_KMETRICS_KIND_STATE, "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_presentations", video->presentations,
                            base ? base->presentations : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_full_presentations", video->full_presentations,
                            base ? base->full_presentations : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_partial_presentations",
                            video->partial_presentations,
                            base ? base->partial_presentations : 0U,
                            baseline_valid && available, "count",
                            SHELL_KMETRICS_KIND_COUNTER, "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_bytes_copied", video->bytes_copied,
                            base ? base->bytes_copied : 0U,
                            baseline_valid && available, "byte",
                            SHELL_KMETRICS_KIND_BYTES, "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_last_copy_bytes", video->last_copy_bytes, 0U,
                            available, "byte", SHELL_KMETRICS_KIND_GAUGE,
                            "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_last_copy_ticks", video->last_copy_ticks, 0U,
                            available, "tick", SHELL_KMETRICS_KIND_DURATION,
                            "vesa", "video");
    SHELL_KMETRICS_EMIT_U32("vesa_max_copy_ticks", video->max_copy_ticks, 0U,
                            available, "tick", SHELL_KMETRICS_KIND_DURATION,
                            "vesa", "video");
    return OK;
}

static int shell_kmetrics_emit_update(
    const shell_kmetrics_snapshot_t* current) {
    uint8_t update_available = current->update_status_result == OK;
    uint8_t slots_available = current->update_slots_result == OK;

    if (shell_kmetrics_emit_u32("update_state_store",
                                current->update_status.state_store, 0U, 0U,
                                update_available, "enum",
                                SHELL_KMETRICS_KIND_STATE, "update", "update") != OK ||
        shell_kmetrics_emit_u32("update_transaction_pending",
                                current->update_status.transaction_pending, 0U,
                                0U, update_available, "bool",
                                SHELL_KMETRICS_KIND_STATE, "update", "update") != OK ||
        shell_kmetrics_emit_u32("update_recovery_pending",
                                current->update_capabilities.recovery_pending, 0U,
                                0U, current->update_capabilities_result == OK,
                                "bool", SHELL_KMETRICS_KIND_STATE, "update", "update") != OK ||
        shell_kmetrics_emit_u32("update_active_slot",
                                current->update_slots.active_slot, 0U, 0U,
                                slots_available, "slot", SHELL_KMETRICS_KIND_STATE,
                                "update_slots", "update") != OK ||
        shell_kmetrics_emit_u32("update_pending_slot",
                                current->update_slots.pending_slot, 0U, 0U,
                                slots_available, "slot", SHELL_KMETRICS_KIND_STATE,
                                "update_slots", "update") != OK ||
        shell_kmetrics_emit_u32("update_journal_pending",
                                current->update_slots.journal_pending, 0U, 0U,
                                slots_available, "bool", SHELL_KMETRICS_KIND_STATE,
                                "update_slots", "update")) {
        return ERR_OVERFLOW;
    }
    if (shell_kmetrics_emit_u32(
            "update_previous_slot", current->update_slots.previous_slot, 0U,
            0U, slots_available, "slot", SHELL_KMETRICS_KIND_STATE,
            "update_slots", "update") != OK ||
        shell_kmetrics_emit_u32(
            "update_attempt_slot", current->update_slots.attempt_slot, 0U,
            0U, slots_available, "slot", SHELL_KMETRICS_KIND_STATE,
            "update_slots", "update") != OK ||
        shell_kmetrics_emit_u32(
            "update_boot_state", current->update_slots.boot_state, 0U, 0U,
            slots_available, "enum", SHELL_KMETRICS_KIND_STATE,
            "update_slots", "update") != OK ||
        shell_kmetrics_emit_u32(
            "update_recovery_pending_slots",
            current->update_slots.recovery_pending, 0U, 0U, slots_available,
            "bool", SHELL_KMETRICS_KIND_STATE, "update_slots", "update") != OK) {
        return ERR_OVERFLOW;
    }
    return OK;
}

static int shell_kmetrics_emit_recovery(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    uint32_t index;

    for (index = 0U; index < RECOVERY_COMPONENT_COUNT; index++) {
        uint8_t available = index < current->recovery_count &&
                            current->recovery_valid[index];
        uint8_t baseline_available = baseline &&
                                     index < baseline->recovery_count &&
                                     baseline->recovery_valid[index];
        uint32_t base_failures = baseline_available ?
                                 baseline->recovery[index].failures : 0U;

        if (shell_kmetrics_emit_indexed_u32(
                "recovery_", index, "_state", current->recovery[index].state,
                0U, 0U, available, "enum", SHELL_KMETRICS_KIND_STATE,
                "recovery", "boot") != OK ||
            shell_kmetrics_emit_indexed_u32(
                "recovery_", index, "_failures",
                current->recovery[index].failures, base_failures,
                baseline_valid && baseline_available, available, "count",
                SHELL_KMETRICS_KIND_COUNTER, "recovery", "boot") != OK) {
            return ERR_OVERFLOW;
        }
        if (shell_kmetrics_emit_indexed_u32(
                "recovery_", index, "_last_error",
                (uint32_t)current->recovery[index].last_error, 0U, 0U,
                available && current->recovery[index].last_error >= 0,
                "error", SHELL_KMETRICS_KIND_STATE, "recovery", "boot") != OK) {
            return ERR_OVERFLOW;
        }
    }
    return OK;
}

int shell_kmetrics_emit_machine(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline, uint8_t baseline_valid) {
    uint32_t sequence;
    uint8_t partial;
    int result;

    if (!current) {
        LOG_ERROR("SHELL", "Snapshot nulo ao emitir metricas PERF1");
        return ERR_NULL;
    }
    if (!baseline) baseline_valid = 0U;
    if (!serial_is_ready()) {
        LOG_WARN("SHELL", "Serial indisponivel para metricas PERF1");
        return ERR_UNAVAILABLE;
    }
    sequence = ++shell_kmetrics_machine_sequence;
    partial = current->valid_domains != SHELL_KMETRICS_ALL_DOMAINS;
    result = shell_kmetrics_emit_record_begin(sequence, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_scheduler(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_input(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_work(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_job(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_memory(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_storage(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_network(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_system(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_video(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_update(current);
    if (result == OK) result = shell_kmetrics_emit_recovery(current, baseline, baseline_valid);
    if (result == OK) result = shell_kmetrics_emit_record_end(sequence, partial);
    if (result != OK) {
        LOG_ERROR("SHELL", "Falha ao emitir metricas PERF1 na serial");
    }
    return result;
}
