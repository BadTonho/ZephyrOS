#include "process/resource.h"
#include "process/process.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "memory/vma.h"

typedef struct {
    const process_t* process;
    uint32_t pid;
    uint32_t generation;
    uint32_t resident_peak_pages;
    uint32_t anonymous_peak_bytes;
    uint32_t descriptor_peak;
    uint32_t child_peak;
    uint32_t ipc_pending_peak;
    uint32_t pipe_peak;
    uint32_t allocation_failures;
    process_resource_failure_t last_failure;
    uint32_t last_error;
    uint32_t last_requested;
    uint8_t active;
} process_resource_entry_t;

static process_resource_entry_t resource_entries[MAX_PROCESSES];

static process_resource_entry_t* resource_find_process(
    const process_t* process) {
    if (!process) return 0;
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        process_resource_entry_t* entry = &resource_entries[index];

        if (entry->active && entry->process == process &&
            entry->pid == process->pid &&
            entry->generation == process->event_generation) {
            return entry;
        }
    }
    return 0;
}

static process_resource_entry_t* resource_find_identity(uint32_t pid,
                                                        uint32_t generation) {
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        process_resource_entry_t* entry = &resource_entries[index];

        if (entry->active && entry->pid == pid &&
            entry->generation == generation) {
            return entry;
        }
    }
    return 0;
}

static process_resource_entry_t* resource_find_pid(uint32_t pid) {
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        process_resource_entry_t* entry = &resource_entries[index];

        if (entry->active && entry->pid == pid) return entry;
    }
    return 0;
}

static int resource_area_is_dynamic(const vm_area_t* area) {
    return area && area->start_addr >= VMA_USER_MMAP_START &&
           area->end_addr <= VMA_USER_MMAP_END;
}

static int resource_dynamic_usage(const process_t* process,
                                  uint32_t* bytes_out,
                                  uint32_t* vmas_out) {
    const vm_area_t* area;
    uint32_t bytes = 0U;
    uint32_t vmas = 0U;

    if (!process || !bytes_out || !vmas_out) {
        LOG_ERROR("PROC_RES", "Entrada nula ao calcular uso de VMAs");
        return ERR_NULL;
    }
    area = process->vma_list;
    while (area) {
        uint32_t length;

        if (resource_area_is_dynamic(area)) {
            if (area->end_addr < area->start_addr) {
                LOG_ERROR("PROC_RES", "VMA com intervalo invertido");
                return ERR_STATE;
            }
            length = area->end_addr - area->start_addr;
            if (length > 0xFFFFFFFFU - bytes) {
                LOG_ERROR("PROC_RES", "Overflow no uso anonimo");
                return ERR_OVERFLOW;
            }
            bytes += length;
            if (vmas == 0xFFFFFFFFU) {
                LOG_ERROR("PROC_RES", "Overflow na contagem de VMAs");
                return ERR_OVERFLOW;
            }
            vmas++;
        }
        area = area->next;
    }
    *bytes_out = bytes;
    *vmas_out = vmas;
    return OK;
}

static int resource_current_pages(const process_t* process,
                                  uint32_t* pages_out) {
    if (!process || !pages_out) {
        LOG_ERROR("PROC_RES", "Entrada nula ao calcular paginas residentes");
        return ERR_NULL;
    }
    *pages_out = 0U;
    if (!process->context.user_mode) return OK;
    if (!process->page_directory) {
        LOG_ERROR("PROC_RES", "Processo user sem diretorio de paginas");
        return ERR_STATE;
    }
    int result = paging_get_user_page_count(process->page_directory, pages_out);
    if (result != OK) {
        LOG_ERROR_CODE("PROC_RES", result,
                       "Falha ao consultar paginas residentes");
    }
    return result;
}

static int resource_image_bytes(const process_t* process,
                                uint32_t* bytes_out) {
    if (!process || !bytes_out) {
        LOG_ERROR("PROC_RES", "Entrada nula ao calcular imagem");
        return ERR_NULL;
    }
    if (process->user_code_size > 0xFFFFFFFFU - process->user_data_size) {
        LOG_ERROR("PROC_RES", "Overflow no tamanho da imagem");
        return ERR_OVERFLOW;
    }
    *bytes_out = process->user_code_size + process->user_data_size;
    return OK;
}

static int resource_fill_usage(const process_t* process,
                               process_resource_snapshot_t* output) {
    uint32_t descriptors;
    uint32_t pipes;
    int result;

    result = resource_dynamic_usage(process, &output->anonymous_bytes,
                                    &output->dynamic_vmas);
    if (result != OK) return result;
    result = resource_current_pages(process, &output->resident_pages);
    if (result != OK) return result;
    result = resource_image_bytes(process, &output->image_bytes);
    if (result != OK) return result;
    output->argument_count = process->user_launch.argc;
    output->argument_bytes = process->user_launch.raw_length;
    output->user_stack_bytes = process->context.user_mode ?
                               PROCESS_RESOURCE_USER_STACK_BYTES : 0U;
    output->kernel_stack_bytes = process->kernel_stack_size;
    result = vfs_get_process_resource_usage(process->pid, &descriptors,
                                            &pipes);
    if (result != OK) return result;
    output->descriptors = descriptors;
    output->children = process_get_child_count(process->pid);
    result = ipc_get_pending_count_for_pid(process->pid,
                                           &output->ipc_pending);
    if (result != OK) return result;
    output->pipes = pipes;
    return OK;
}

static int resource_validate_initial(const process_t* process) {
    process_resource_snapshot_t snapshot;
    int result;

    kmemset(&snapshot, 0, sizeof(snapshot));
    result = resource_fill_usage(process, &snapshot);
    if (result != OK) return result;
    if (!process->context.user_mode) return OK;
    if (snapshot.resident_pages > PROCESS_RESOURCE_MAX_RESIDENT_PAGES ||
        snapshot.anonymous_bytes > PROCESS_RESOURCE_MAX_ANONYMOUS_BYTES ||
        snapshot.dynamic_vmas > PROCESS_RESOURCE_MAX_DYNAMIC_VMAS ||
        snapshot.argument_count > PROCESS_RESOURCE_MAX_ARGUMENTS ||
        snapshot.argument_bytes > PROCESS_RESOURCE_MAX_ARGUMENT_BYTES ||
        process->kernel_stack_size < PROCESS_KERNEL_STACK_MIN_SIZE ||
        process->kernel_stack_size > PROCESS_KERNEL_STACK_MAX_SIZE ||
        snapshot.user_stack_bytes != PROCESS_RESOURCE_USER_STACK_BYTES ||
        process->user_code_size > PAGE_SIZE ||
        process->user_data_size > PAGE_SIZE ||
        snapshot.descriptors > PROCESS_RESOURCE_MAX_DESCRIPTORS ||
        snapshot.children > PROCESS_RESOURCE_MAX_CHILDREN ||
        snapshot.ipc_pending > PROCESS_RESOURCE_MAX_IPC_PENDING ||
        snapshot.pipes > PROCESS_RESOURCE_MAX_PIPES) {
        LOG_ERROR("PROC_RES", "Limite inicial de processo excedido");
        return ERR_OVERFLOW;
    }
    return OK;
}

static void resource_update_peaks(process_resource_entry_t* entry) {
    process_resource_snapshot_t snapshot;
    int result;

    if (!entry || !entry->active || !entry->process) return;
    kmemset(&snapshot, 0, sizeof(snapshot));
    result = resource_fill_usage(entry->process, &snapshot);
    if (result != OK) {
        LOG_ERROR("PROC_RES", "Falha ao atualizar pico de recursos");
        return;
    }
    if (snapshot.resident_pages > entry->resident_peak_pages) {
        entry->resident_peak_pages = snapshot.resident_pages;
    }
    if (snapshot.anonymous_bytes > entry->anonymous_peak_bytes) {
        entry->anonymous_peak_bytes = snapshot.anonymous_bytes;
    }
    if (snapshot.descriptors > entry->descriptor_peak) {
        entry->descriptor_peak = snapshot.descriptors;
    }
    if (snapshot.children > entry->child_peak) {
        entry->child_peak = snapshot.children;
    }
    if (snapshot.ipc_pending > entry->ipc_pending_peak) {
        entry->ipc_pending_peak = snapshot.ipc_pending;
    }
    if (snapshot.pipes > entry->pipe_peak) {
        entry->pipe_peak = snapshot.pipes;
    }
}

int process_resource_init(void) {
    kmemset(resource_entries, 0, sizeof(resource_entries));
    return OK;
}

int process_resource_attach(struct process* process) {
    process_resource_entry_t* entry = 0;
    int result;

    if (!process) {
        LOG_ERROR("PROC_RES", "Processo nulo ao registrar recursos");
        return ERR_NULL;
    }
    if (resource_find_process(process)) {
        LOG_ERROR("PROC_RES", "Processo duplicado no registro de recursos");
        return ERR_STATE;
    }
    result = resource_validate_initial(process);
    if (result != OK) return result;
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        if (!resource_entries[index].active) {
            entry = &resource_entries[index];
            break;
        }
    }
    if (!entry) {
        LOG_ERROR("PROC_RES", "Tabela de recursos de processos esgotada");
        return ERR_MEM;
    }
    kmemset(entry, 0, sizeof(*entry));
    entry->process = process;
    entry->pid = process->pid;
    entry->generation = process->event_generation;
    entry->active = 1U;
    resource_update_peaks(entry);
    return OK;
}

void process_resource_detach(const struct process* process) {
    process_resource_entry_t* entry = resource_find_process(process);

    if (!entry) return;
    kmemset(entry, 0, sizeof(*entry));
}

void process_resource_record_failure(struct process* process,
                                     process_resource_failure_t failure,
                                     uint32_t error, uint32_t requested) {
    process_resource_entry_t* entry = resource_find_process(process);

    if (!entry) return;
    if (entry->allocation_failures != 0xFFFFFFFFU) {
        entry->allocation_failures++;
    }
    entry->last_failure = failure;
    entry->last_error = error;
    entry->last_requested = requested;
    LOG_WARN_CODE("PROC_RES", (int32_t)error,
                  "Operacao de recurso do processo rejeitada");
}

int process_resource_check_vma(struct process* process, uint32_t length) {
    process_resource_entry_t* entry = resource_find_process(process);
    uint32_t bytes;
    uint32_t vmas;
    int result;

    if (!entry) {
        LOG_ERROR("PROC_RES", "Processo ausente no controle de VMAs");
        return ERR_STATE;
    }
    if (!entry->process->context.user_mode) {
        LOG_ERROR("PROC_RES", "VMA user solicitada para processo kernel");
        return ERR_STATE;
    }
    if (!length || (length % PAGE_SIZE) != 0U) {
        LOG_WARN("PROC_RES", "Tamanho de VMA invalido");
        return ERR_INVALID;
    }
    result = resource_dynamic_usage(process, &bytes, &vmas);
    if (result != OK) return result;
    if (bytes > PROCESS_RESOURCE_MAX_ANONYMOUS_BYTES) {
        process_resource_record_failure(
            process, PROCESS_RESOURCE_FAILURE_ANONYMOUS_BYTES, ERR_OVERFLOW,
            length);
        return ERR_OVERFLOW;
    }
    if (vmas >= PROCESS_RESOURCE_MAX_DYNAMIC_VMAS) {
        process_resource_record_failure(process,
                                        PROCESS_RESOURCE_FAILURE_DYNAMIC_VMAS,
                                        ERR_OVERFLOW, 1U);
        return ERR_OVERFLOW;
    }
    if (length > PROCESS_RESOURCE_MAX_ANONYMOUS_BYTES - bytes) {
        process_resource_record_failure(
            process, PROCESS_RESOURCE_FAILURE_ANONYMOUS_BYTES, ERR_OVERFLOW,
            length);
        return ERR_OVERFLOW;
    }
    return OK;
}

int process_resource_check_vma_split(struct process* process) {
    process_resource_entry_t* entry = resource_find_process(process);
    uint32_t bytes;
    uint32_t vmas;
    int result;

    if (!entry) {
        LOG_ERROR("PROC_RES", "Processo ausente no split de VMA");
        return ERR_STATE;
    }
    result = resource_dynamic_usage(process, &bytes, &vmas);
    if (result != OK) return result;
    if (vmas >= PROCESS_RESOURCE_MAX_DYNAMIC_VMAS) {
        process_resource_record_failure(process,
                                        PROCESS_RESOURCE_FAILURE_DYNAMIC_VMAS,
                                        ERR_OVERFLOW, 1U);
        return ERR_OVERFLOW;
    }
    return OK;
}

int process_resource_check_page(struct process* process) {
    process_resource_entry_t* entry = resource_find_process(process);
    uint32_t pages;
    int result;

    if (!entry) {
        LOG_ERROR("PROC_RES", "Processo ausente no limite de paginas");
        return ERR_STATE;
    }
    result = resource_current_pages(process, &pages);
    if (result != OK) return result;
    if (pages >= PROCESS_RESOURCE_MAX_RESIDENT_PAGES) {
        process_resource_record_failure(process,
                                        PROCESS_RESOURCE_FAILURE_RESIDENT_PAGES,
                                        ERR_OVERFLOW, 1U);
        return ERR_OVERFLOW;
    }
    return OK;
}

void process_resource_note_vma_success(struct process* process) {
    resource_update_peaks(resource_find_process(process));
}

void process_resource_note_page_success(struct process* process) {
    resource_update_peaks(resource_find_process(process));
}

static int resource_check_quota(process_t* process, uint32_t current,
                                uint32_t requested, uint32_t limit,
                                process_resource_failure_t failure) {
    if (!process || !resource_find_process(process)) {
        LOG_ERROR("PROC", "Processo sem registro de recursos");
        return ERR_STATE;
    }
    if (!process->context.user_mode) return OK;
    if (requested > limit || current > limit - requested) {
        LOG_WARN("PROC", "Quota de processo excedida");
        process_resource_record_failure(process, failure, ERR_OVERFLOW,
                                        requested);
        return ERR_OVERFLOW;
    }
    return OK;
}

int process_resource_check_descriptors(struct process* process,
                                       uint32_t requested) {
    uint32_t descriptors;
    uint32_t pipes;
    int result;

    result = vfs_get_process_resource_usage(process ? process->pid : 0U,
                                            &descriptors, &pipes);
    if (result != OK) return result;
    return resource_check_quota(process, descriptors, requested,
                                PROCESS_RESOURCE_MAX_DESCRIPTORS,
                                PROCESS_RESOURCE_FAILURE_DESCRIPTORS);
}

int process_resource_check_children(struct process* process,
                                    uint32_t requested) {
    if (!process) {
        LOG_ERROR("PROC", "Processo ausente na quota de filhos");
        return ERR_NULL;
    }
    return resource_check_quota(process, process_get_child_count(process->pid),
                                requested, PROCESS_RESOURCE_MAX_CHILDREN,
                                PROCESS_RESOURCE_FAILURE_CHILDREN);
}

int process_resource_check_pipes(struct process* process,
                                 uint32_t requested) {
    uint32_t descriptors;
    uint32_t pipes;
    int result;

    result = vfs_get_process_resource_usage(process ? process->pid : 0U,
                                            &descriptors, &pipes);
    if (result != OK) return result;
    return resource_check_quota(process, pipes, requested,
                                PROCESS_RESOURCE_MAX_PIPES,
                                PROCESS_RESOURCE_FAILURE_PIPES);
}

int process_resource_check_ipc_pending(struct process* process,
                                       uint32_t current,
                                       uint32_t requested) {
    return resource_check_quota(process, current, requested,
                                PROCESS_RESOURCE_MAX_IPC_PENDING,
                                PROCESS_RESOURCE_FAILURE_IPC_PENDING);
}

void process_resource_note_descriptor_success(struct process* process) {
    resource_update_peaks(resource_find_process(process));
}

void process_resource_note_pipe_success(struct process* process) {
    resource_update_peaks(resource_find_process(process));
}

int process_resource_snapshot_copy(uint32_t pid, uint32_t generation,
                                   process_resource_snapshot_t* output) {
    process_resource_entry_t* entry;
    int result;

    if (!output) {
        LOG_ERROR("PROC_RES", "Destino nulo no snapshot de recursos");
        return ERR_NULL;
    }
    entry = resource_find_identity(pid, generation);
    if (!entry) {
        if (resource_find_pid(pid)) return ERR_AGAIN;
        return ERR_NOT_FOUND;
    }
    kmemset(output, 0, sizeof(*output));
    output->pid = entry->pid;
    output->generation = entry->generation;
    output->limits_active = entry->process->context.user_mode ? 1U : 0U;
    output->resident_peak_pages = entry->resident_peak_pages;
    output->anonymous_peak_bytes = entry->anonymous_peak_bytes;
    output->resident_limit_pages = PROCESS_RESOURCE_MAX_RESIDENT_PAGES;
    output->anonymous_limit_bytes = PROCESS_RESOURCE_MAX_ANONYMOUS_BYTES;
    output->dynamic_vma_limit = PROCESS_RESOURCE_MAX_DYNAMIC_VMAS;
    output->argument_count_limit = PROCESS_RESOURCE_MAX_ARGUMENTS;
    output->argument_bytes_limit = PROCESS_RESOURCE_MAX_ARGUMENT_BYTES;
    output->descriptor_peak = entry->descriptor_peak;
    output->child_peak = entry->child_peak;
    output->ipc_pending_peak = entry->ipc_pending_peak;
    output->pipe_peak = entry->pipe_peak;
    output->descriptor_limit = PROCESS_RESOURCE_MAX_DESCRIPTORS;
    output->child_limit = PROCESS_RESOURCE_MAX_CHILDREN;
    output->ipc_pending_limit = PROCESS_RESOURCE_MAX_IPC_PENDING;
    output->pipe_limit = PROCESS_RESOURCE_MAX_PIPES;
    output->allocation_failures = entry->allocation_failures;
    output->last_failure = entry->last_failure;
    output->last_error = entry->last_error;
    output->last_requested = entry->last_requested;
    result = resource_fill_usage(entry->process, output);
    if (result != OK) return result;
    return OK;
}

int process_resource_validate_all(process_resource_validation_t* validation) {
    if (!validation) {
        LOG_ERROR("PROC_RES", "Destino nulo na validacao de recursos");
        return ERR_NULL;
    }
    kmemset(validation, 0, sizeof(*validation));
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        process_resource_entry_t* entry = &resource_entries[index];
        process_resource_snapshot_t snapshot;
        int result;

        if (!entry->active) continue;
        validation->checked++;
        if (!entry->process || entry->pid != entry->process->pid ||
            entry->generation != entry->process->event_generation) {
            validation->invalid++;
            continue;
        }
        result = process_resource_snapshot_copy(entry->pid, entry->generation,
                                                &snapshot);
        if (result != OK ||
            (snapshot.limits_active &&
             (snapshot.resident_pages > snapshot.resident_limit_pages ||
              snapshot.anonymous_bytes > snapshot.anonymous_limit_bytes ||
              snapshot.dynamic_vmas > snapshot.dynamic_vma_limit ||
              snapshot.argument_count > snapshot.argument_count_limit ||
              snapshot.argument_bytes > snapshot.argument_bytes_limit ||
              snapshot.descriptors > snapshot.descriptor_limit ||
              snapshot.children > snapshot.child_limit ||
              snapshot.ipc_pending > snapshot.ipc_pending_limit ||
              snapshot.pipes > snapshot.pipe_limit))) {
            validation->invalid++;
            continue;
        }
        validation->valid++;
    }
    return validation->invalid ? ERR_STATE : OK;
}
