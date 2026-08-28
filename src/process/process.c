#include "process/process.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/timer.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "core/syscall.h"
#include "drivers/tss.h"

#define PROCESS_DEFAULT_EFLAGS 0x202U
#define PROCESS_PID_POOL_SIZE (MAX_PROCESSES - 1U)
#define PROCESS_WAIT_EFLAGS_INTERRUPT_ENABLE (1U << 9U)
#define PROCESS_STACK_CANARY_LOWER 0x53544B4CU
#define PROCESS_STACK_CANARY_UPPER 0x53544B55U
#define PROCESS_STACK_FILL_BYTE 0xA5U
#define PROCESS_STACK_GUARD_WORDS \
    (PROCESS_STACK_GUARD_BYTES / sizeof(uint32_t))
#define PROCESS_STACK_TEST_STORAGE_SIZE \
    (KERNEL_STACK_SIZE + (PROCESS_STACK_GUARD_BYTES * 2U) + \
     PROCESS_KERNEL_STACK_ALIGNMENT)

process_t processes[MAX_PROCESSES];
static process_t* current_process = 0;
static uint8_t idle_stack[PROCESS_STACK_TEST_STORAGE_SIZE]
    __attribute__((aligned(16)));
static uint8_t process_stack_test_storage[PROCESS_STACK_TEST_STORAGE_SIZE]
    __attribute__((aligned(16)));
static process_t process_stack_test_process;
uint32_t process_count = 0;
static uint32_t free_pids[PROCESS_PID_POOL_SIZE];
static uint32_t free_pid_count = 0;
static int last_scheduled_idx = -1;
static uint32_t scheduler_context_switches = 0;
static uint32_t scheduler_cooperative_yields = 0;
static uint32_t scheduler_user_preemptions = 0;
static uint32_t scheduler_idle_fallbacks = 0;
static int last_user_fault_valid = 0;
static process_user_fault_summary_t last_user_fault;
static uint32_t user_fault_count = 0;
static int user_test_result_pending = 0;
static uint32_t user_test_result_pid = 0;
static uint32_t user_test_result_faulted = 0;
static uint32_t process_event_generation = 0;
static const app_launch_info_t process_empty_launch = {
    .abi_version = APP_LAUNCH_ABI_VERSION
};

static uint32_t process_stack_align_up(uint32_t value) {
    return (value + PROCESS_KERNEL_STACK_ALIGNMENT - 1U) &
           ~(PROCESS_KERNEL_STACK_ALIGNMENT - 1U);
}

static int process_stack_size_valid(uint32_t stack_size) {
    return stack_size >= PROCESS_KERNEL_STACK_MIN_SIZE &&
           stack_size <= PROCESS_KERNEL_STACK_MAX_SIZE &&
           (stack_size % PROCESS_KERNEL_STACK_ALIGNMENT) == 0U;
}

static int process_stack_bounds_valid(const process_t* proc) {
    if (!proc || !proc->kernel_stack || !proc->kernel_stack_top ||
        !process_stack_size_valid(proc->kernel_stack_size) ||
        proc->kernel_stack_top != proc->kernel_stack + proc->kernel_stack_size ||
        (proc->kernel_stack % PROCESS_KERNEL_STACK_ALIGNMENT) != 0U ||
        proc->kernel_stack < PROCESS_STACK_GUARD_BYTES) {
        return 0;
    }
    return 1;
}

static void process_stack_write_guard(uint32_t address, uint32_t value) {
    uint32_t* words = (uint32_t*)address;

    for (uint32_t index = 0U; index < PROCESS_STACK_GUARD_WORDS; index++) {
        words[index] = value;
    }
}

static uint8_t process_stack_guard_matches(uint32_t address,
                                            uint32_t expected) {
    const uint32_t* words = (const uint32_t*)address;

    for (uint32_t index = 0U; index < PROCESS_STACK_GUARD_WORDS; index++) {
        if (words[index] != expected) return 0U;
    }
    return 1U;
}

static int process_stack_attach(process_t* proc, uint32_t allocation,
                                uint32_t stack_size, uint8_t owned) {
    uint32_t stack;

    if (!proc || !allocation || !process_stack_size_valid(stack_size)) {
        LOG_ERROR("PROC", "Parametros invalidos para stack do processo");
        return ERR_INVALID;
    }
    stack = process_stack_align_up(allocation + PROCESS_STACK_GUARD_BYTES);
    if (stack < allocation || stack > 0xFFFFFFFFU - stack_size -
                                    PROCESS_STACK_GUARD_BYTES) {
        LOG_ERROR("PROC", "Endereco de stack fora dos limites");
        return ERR_OVERFLOW;
    }

    proc->kernel_stack_allocation = allocation;
    proc->kernel_stack = stack;
    proc->kernel_stack_top = stack + stack_size;
    proc->kernel_stack_size = stack_size;
    proc->kernel_stack_peak_used = 0U;
    proc->kernel_stack_min_free = stack_size;
    proc->kernel_stack_low_water_events = 0U;
    proc->kernel_stack_overflow_events = 0U;
    proc->kernel_stack_owned = owned;
    proc->kernel_stack_low_water_active = 0U;
    proc->kernel_stack_corruption_reported = 0U;

    process_stack_write_guard(stack - PROCESS_STACK_GUARD_BYTES,
                              PROCESS_STACK_CANARY_LOWER);
    process_stack_write_guard(proc->kernel_stack_top,
                              PROCESS_STACK_CANARY_UPPER);
    kmemset((void*)proc->kernel_stack, PROCESS_STACK_FILL_BYTE, stack_size);
    return OK;
}

static int process_stack_allocate(process_t* proc, uint32_t stack_size) {
    uint32_t allocation_size;
    uint32_t allocation;

    if (!process_stack_size_valid(stack_size)) {
        LOG_ERROR("PROC", "Tamanho ou alinhamento de stack invalido");
        return ERR_INVALID;
    }
    allocation_size = stack_size + (PROCESS_STACK_GUARD_BYTES * 2U) +
                      PROCESS_KERNEL_STACK_ALIGNMENT;
    allocation = (uint32_t)kmalloc(allocation_size);
    if (!allocation) {
        LOG_ERROR("PROC", "Falha ao alocar stack do processo");
        return ERR_MEM;
    }
    if (process_stack_attach(proc, allocation, stack_size, 1U) != OK) {
        kfree((void*)allocation);
        return ERR_STATE;
    }
    return OK;
}

static void process_stack_release(process_t* proc) {
    uint32_t allocation;

    if (!proc) return;
    allocation = proc->kernel_stack_allocation;
    if (proc->kernel_stack_owned && allocation) kfree((void*)allocation);
    proc->kernel_stack_allocation = 0U;
    proc->kernel_stack = 0U;
    proc->kernel_stack_top = 0U;
    proc->kernel_stack_size = 0U;
    proc->kernel_stack_owned = 0U;
}

static void process_stack_copy_name(char* output, const char* input) {
    uint32_t index = 0U;

    if (!output) return;
    if (!input) input = "";
    while (input[index] && index + 1U < PROCESS_NAME_LENGTH) {
        output[index] = input[index];
        index++;
    }
    output[index] = '\0';
}

static void process_stack_log_append_text(char* output, uint32_t* offset,
                                          const char* text) {
    if (!output || !offset || !text) return;
    while (*text && *offset + 1U < LOG_MESSAGE_CAPACITY) {
        output[(*offset)++] = *text++;
    }
    output[*offset] = '\0';
}

static void process_stack_log_append_number(char* output, uint32_t* offset,
                                            uint32_t value) {
    char digits[11];
    uint32_t count = 0U;

    if (!output || !offset) return;
    if (value == 0U) {
        process_stack_log_append_text(output, offset, "0");
        return;
    }
    while (value && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count) {
        char digit_text[2];

        digit_text[0] = digits[--count];
        digit_text[1] = '\0';
        process_stack_log_append_text(output, offset, digit_text);
    }
}

static uint32_t process_stack_recorded_usage(const process_t* proc) {
    uint32_t usage;
    uint32_t esp = 0U;

    if (!process_stack_bounds_valid(proc)) return 0U;
    usage = proc->kernel_stack_peak_used;
    if (proc->context.esp >= proc->kernel_stack &&
        proc->context.esp <= proc->kernel_stack_top) {
        uint32_t context_usage = proc->kernel_stack_top - proc->context.esp;
        if (context_usage > usage) usage = context_usage;
    }
    if (proc == current_process) {
        asm volatile("mov %%esp, %0" : "=r"(esp));
        if (esp >= proc->kernel_stack && esp <= proc->kernel_stack_top) {
            uint32_t current_usage = proc->kernel_stack_top - esp;
            if (current_usage > usage) usage = current_usage;
        }
    }
    return usage;
}

static void process_stack_report_corruption(process_t* proc) {
    char message[LOG_MESSAGE_CAPACITY];
    uint32_t offset = 0U;

    if (!proc || proc->kernel_stack_corruption_reported) return;
    proc->kernel_stack_corruption_reported = 1U;
    proc->kernel_stack_overflow_events++;
    kmemset(message, 0, sizeof(message));
    process_stack_log_append_text(message, &offset, "Canario PID=");
    process_stack_log_append_number(message, &offset, proc->pid);
    process_stack_log_append_text(message, &offset, " nome=");
    process_stack_log_append_text(message, &offset, proc->name);
    process_stack_log_append_text(message, &offset, " uso=");
    process_stack_log_append_number(message, &offset,
                                    process_stack_recorded_usage(proc));
    LOG_ERROR_CODE("PROC", (int32_t)proc->pid, message);
}

static int process_stack_observe(process_t* proc, process_stack_info_t* info,
                                 uint8_t panic_on_corruption,
                                 uint8_t log_low_water) {
    uint8_t lower_ok;
    uint8_t upper_ok;
    uint8_t* bytes;
    uint32_t first_used;
    uint32_t observed_used;
    uint32_t bytes_used;
    uint32_t bytes_free;
    uint32_t observed_free;
    uint32_t current_esp = 0U;

    if (info) kmemset(info, 0, sizeof(process_stack_info_t));
    if (!process_stack_bounds_valid(proc)) {
        if (info && proc) {
            info->pid = proc->pid;
            process_stack_copy_name(info->name, proc->name);
        }
        LOG_ERROR("PROC", "Metadados de stack invalidos");
        return ERR_STATE;
    }
    lower_ok = process_stack_guard_matches(
        proc->kernel_stack - PROCESS_STACK_GUARD_BYTES,
        PROCESS_STACK_CANARY_LOWER);
    upper_ok = process_stack_guard_matches(proc->kernel_stack_top,
                                            PROCESS_STACK_CANARY_UPPER);
    if (!lower_ok || !upper_ok) {
        process_stack_report_corruption(proc);
        if (info) {
            kmemset(info, 0, sizeof(process_stack_info_t));
            info->pid = proc->pid;
            process_stack_copy_name(info->name, proc->name);
            info->stack_size = proc->kernel_stack_size;
            info->lower_canary_ok = lower_ok;
            info->upper_canary_ok = upper_ok;
            info->overflow_events = proc->kernel_stack_overflow_events;
        }
        if (panic_on_corruption) panic("PROC: canario de stack corrompido");
        return ERR_OVERFLOW;
    }

    bytes = (uint8_t*)proc->kernel_stack;
    first_used = 0U;
    while (first_used < proc->kernel_stack_size &&
           bytes[first_used] == PROCESS_STACK_FILL_BYTE) {
        first_used++;
    }
    observed_used = proc->kernel_stack_size - first_used;
    bytes_used = 0U;
    if (proc->context.esp >= proc->kernel_stack &&
        proc->context.esp <= proc->kernel_stack_top) {
        bytes_used = proc->kernel_stack_top - proc->context.esp;
    }
    if (proc == current_process) {
        asm volatile("mov %%esp, %0" : "=r"(current_esp));
        if (current_esp >= proc->kernel_stack &&
            current_esp <= proc->kernel_stack_top) {
            bytes_used = proc->kernel_stack_top - current_esp;
        }
    }
    if (bytes_used > observed_used) observed_used = bytes_used;
    bytes_free = proc->kernel_stack_size - bytes_used;
    observed_free = proc->kernel_stack_size - observed_used;
    if (observed_used > proc->kernel_stack_peak_used) {
        proc->kernel_stack_peak_used = observed_used;
    }
    if (observed_free < proc->kernel_stack_min_free) {
        proc->kernel_stack_min_free = observed_free;
    }
    if (observed_free <= PROCESS_STACK_LOW_WATER_BYTES) {
        if (!proc->kernel_stack_low_water_active) {
            proc->kernel_stack_low_water_active = 1U;
            proc->kernel_stack_low_water_events++;
            if (log_low_water) {
                LOG_WARN_CODE("PROC", (int32_t)proc->pid,
                              "Stack atingiu margem baixa");
            }
        }
    } else {
        proc->kernel_stack_low_water_active = 0U;
    }

    if (info) {
        kmemset(info, 0, sizeof(process_stack_info_t));
        info->pid = proc->pid;
        process_stack_copy_name(info->name, proc->name);
        info->stack_size = proc->kernel_stack_size;
        info->bytes_used = bytes_used;
        info->peak_bytes_used = proc->kernel_stack_peak_used;
        info->bytes_free = bytes_free;
        info->minimum_bytes_free = proc->kernel_stack_min_free;
        info->low_water_events = proc->kernel_stack_low_water_events;
        info->overflow_events = proc->kernel_stack_overflow_events;
        info->lower_canary_ok = lower_ok;
        info->upper_canary_ok = upper_ok;
        info->low_water_active = proc->kernel_stack_low_water_active;
    }
    return observed_free <= PROCESS_STACK_LOW_WATER_BYTES ? ERR_OVERFLOW : OK;
}

/* A troca de contexto precisa ser constante: os canarios sao suficientes
   aqui; a varredura completa do high-water fica para diagnostico e HTTP. */
static void process_stack_verify_or_panic(process_t* proc) {
    if (!process_stack_bounds_valid(proc)) {
        LOG_ERROR("PROC", "Metadados de stack invalidos na troca de contexto");
        panic("PROC: metadados de stack invalidos");
    }
    if (!process_stack_guard_matches(proc->kernel_stack -
                                     PROCESS_STACK_GUARD_BYTES,
                                     PROCESS_STACK_CANARY_LOWER) ||
        !process_stack_guard_matches(proc->kernel_stack_top,
                                     PROCESS_STACK_CANARY_UPPER)) {
        process_stack_report_corruption(proc);
        panic("PROC: canario de stack corrompido");
    }
}

static uint32_t process_wait_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void process_wait_irq_restore(uint32_t flags) {
    if (flags & PROCESS_WAIT_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static void process_wait_block_transition(void* target,
                                          wait_queue_entry_t* entry) {
    process_t* proc = (process_t*)target;

    proc->wait_channel = entry->queue;
    proc->wait_condition = entry->observed_condition;
    proc->wait_deadline = entry->deadline_tick;
    proc->wait_reason = WAIT_REASON_NONE;
    proc->wait_deadline_active = entry->deadline_active;
    proc->wait_active = 1U;
    proc->wait_ticks = entry->deadline_active ?
                       entry->deadline_tick - timer_get_ticks() : 0U;
    proc->state = PROCESS_STATE_BLOCKED;
}

static void process_wait_wake_transition(void* target,
                                         wait_queue_entry_t* entry) {
    process_t* proc = (process_t*)target;

    proc->wait_active = 0U;
    proc->wait_channel = 0;
    proc->wait_condition = 0U;
    proc->wait_deadline = WAIT_TIMEOUT_INFINITE;
    proc->wait_reason = entry->reason;
    proc->wait_deadline_active = 0U;
    proc->wait_ticks = 0U;
    if (proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
    }
}

static void process_wait_yield_transition(void* target) {
    (void)target;
    process_yield();
}

static int process_wait_state_init(process_t* proc) {
    if (!proc) {
        LOG_ERROR("PROC", "Processo nulo ao inicializar espera");
        return ERR_NULL;
    }
    if (wait_channel_init(&proc->ipc_wait_channel, "IPC") != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar canal IPC do processo");
        return ERR_STATE;
    }
    proc->ipc_wait_generation = proc->ipc_wait_channel.condition;
    proc->wait_channel = 0;
    proc->wait_condition = 0U;
    proc->wait_deadline = WAIT_TIMEOUT_INFINITE;
    proc->wait_reason = WAIT_REASON_NONE;
    proc->wait_deadline_active = 0U;
    proc->wait_active = 0U;
    kmemset(&proc->wait_entry, 0, sizeof(proc->wait_entry));
    return OK;
}

static void process_wait_clear(process_t* proc, wait_reason_t reason) {
    if (!proc) return;
    if (proc->wait_entry.linked) {
        if (wait_queue_remove(&proc->wait_entry, reason) != OK) {
            LOG_ERROR("PROC", "Falha ao remover processo da fila de espera");
        }
        return;
    }
    proc->wait_active = 0U;
    proc->wait_channel = 0;
    proc->wait_condition = 0U;
    proc->wait_deadline = WAIT_TIMEOUT_INFINITE;
    proc->wait_reason = reason;
    proc->wait_deadline_active = 0U;
    proc->wait_ticks = 0U;
    if (proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
    }
}

static int process_wait_deadline_reached(const process_t* proc,
                                         uint32_t now) {
    if (!proc || !proc->wait_deadline_active) return 0;
    return (int32_t)(now - proc->wait_deadline) >= 0;
}

static void process_copy_wait_text(char* destination, uint32_t capacity,
                                   const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (index + 1U < capacity && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void process_idle_main(void) {
    while (1) {
        asm volatile("hlt");
        process_yield();
    }
}

static uint32_t process_allocate_pid(void) {
    if (free_pid_count == 0) {
        LOG_ERROR("PROC", "Pool de PIDs esgotado");
        return 0;
    }

    return free_pids[--free_pid_count];
}

static void process_release_pid(uint32_t pid) {
    if (pid == 0 || free_pid_count >= PROCESS_PID_POOL_SIZE) {
        LOG_ERROR("PROC", "PID invalido ao retornar para o pool");
        return;
    }

    for (uint32_t i = 0; i < free_pid_count; i++) {
        if (free_pids[i] == pid) {
            LOG_ERROR("PROC", "PID duplicado no pool de processos");
            return;
        }
    }

    free_pids[free_pid_count++] = pid;
}

static void process_initialize_pid_pool(void) {
    free_pid_count = PROCESS_PID_POOL_SIZE;
    for (uint32_t i = 0; i < PROCESS_PID_POOL_SIZE; i++) {
        free_pids[i] = PROCESS_PID_POOL_SIZE - i;
    }
}

static void process_discard_new_process(process_t* proc) {
    if (!proc) return;

    if (vfs_fd_table_release(&proc->fd_table) != OK) {
        LOG_ERROR("PROC", "Falha ao liberar descritores de processo descartado");
    }
    if (proc->ipc_wait_channel.initialized) {
        wait_channel_reset(&proc->ipc_wait_channel);
    }
    if (proc->pid != 0) process_release_pid(proc->pid);
    process_stack_release(proc);
    kmemset(proc, 0, sizeof(process_t));
    proc->state = PROCESS_STATE_UNUSED;
}

void process_init(void) {
    LOG_INFO("PROC", "Inicializando gerenciador de processos");
    current_process = 0;
    process_count = 0;
    process_initialize_pid_pool();
    last_scheduled_idx = -1;
    scheduler_context_switches = 0;
    last_user_fault_valid = 0;
    kmemset(&last_user_fault, 0, sizeof(last_user_fault));
    user_fault_count = 0;
    user_test_result_pending = 0;
    user_test_result_pid = 0;
    user_test_result_faulted = 0;
    process_event_generation = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        kmemset(&processes[i], 0, sizeof(process_t));
        processes[i].state = PROCESS_STATE_UNUSED;
    }
    if (process_signal_init() != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar sinais de processos");
    }
    scheduler_init();
    LOG_INFO("PROC", "Gerenciador de processos inicializado");
}

/* A funcao permanece separada do bootstrap para o Idle nao consumir PID. */
void process_bootstrap_idle(void) {
    process_t* proc = &processes[0];
    kmemset(proc, 0, sizeof(process_t));
    proc->pid = 0;
    
    const char* name = "System Idle";
    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    if (process_wait_state_init(proc) != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar espera do Idle");
        return;
    }
    if (vfs_fd_table_init(&proc->fd_table) != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar descritores do Idle");
        wait_channel_reset(&proc->ipc_wait_channel);
        return;
    }
    
    proc->state = PROCESS_STATE_RUNNING;
    proc->total_ticks = 0;
    proc->wait_ticks = 0;
    
    /* O Idle precisa de uma stack de retorno valida quando o scheduler
       precisar voltar a ele depois de trocar para outro processo. */
    if (process_stack_attach(proc, (uint32_t)idle_stack,
                             KERNEL_STACK_SIZE, 0U) != OK) {
        LOG_ERROR("PROC", "Falha ao preparar stack do Idle");
        if (wait_channel_reset(&proc->ipc_wait_channel) != OK) {
            LOG_ERROR("PROC", "Falha ao liberar fila IPC do Idle");
        }
        if (vfs_fd_table_release(&proc->fd_table) != OK) {
            LOG_ERROR("PROC", "Falha ao liberar descritores do Idle");
        }
        proc->state = PROCESS_STATE_UNUSED;
        return;
    }
    proc->page_directory = paging_get_current_directory();
    proc->context.esp = proc->kernel_stack_top;
    proc->context.eip = (uint32_t)process_idle_main;
    proc->context.eflags = PROCESS_DEFAULT_EFLAGS;
    proc->context.cs = KERNEL_CODE_SELECTOR;
    proc->context.ss = KERNEL_DATA_SELECTOR;
    proc->context.ds = KERNEL_DATA_SELECTOR;
    proc->context.es = KERNEL_DATA_SELECTOR;
    proc->context.fs = KERNEL_DATA_SELECTOR;
    proc->context.gs = KERNEL_DATA_SELECTOR;
    proc->context.cr3 = (uint32_t)proc->page_directory;
    
    current_process = proc;
    process_count = 1;
    process_signal_process_created(proc->pid, 0U);
    LOG_INFO("PROC", "Processo Idle inicializado");
}

static process_t* process_create_internal(const char* name,
                                          void (*entry_point)(),
                                          uint32_t stack_size) {
    process_t* proc = 0;

    if (!name || !entry_point) {
        LOG_ERROR("PROC", "Parametros invalidos ao criar processo");
        return 0;
    }
    if (!process_stack_size_valid(stack_size)) {
        LOG_ERROR("PROC", "Tamanho ou alinhamento de stack invalido");
        return 0;
    }
    if (!paging_get_current_directory()) {
        LOG_ERROR("PROC", "Paging indisponivel ao criar processo");
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }

    if (!proc) {
        LOG_ERROR("PROC", "Limite de processos atingido");
        return 0;
    }

    kmemset(proc, 0, sizeof(process_t));

    int i = 0;
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    if (process_wait_state_init(proc) != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar espera do processo");
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    if (vfs_fd_table_init(&proc->fd_table) != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar descritores do processo");
        wait_channel_reset(&proc->ipc_wait_channel);
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    if (vfs_fd_table_inherit_cwd(&proc->fd_table,
                                 current_process ?
                                 &current_process->fd_table : 0) != OK) {
        LOG_ERROR("PROC", "Falha ao herdar diretorio do processo");
        wait_channel_reset(&proc->ipc_wait_channel);
        vfs_fd_table_release(&proc->fd_table);
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }

    proc->pid = process_allocate_pid();
    if (!proc->pid) {
        LOG_ERROR("PROC", "Falha ao reservar PID do processo");
        wait_channel_reset(&proc->ipc_wait_channel);
        vfs_fd_table_release(&proc->fd_table);
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return 0;
    }
    /* O processo permanece invisivel ao scheduler ate o contexto estar pronto. */
    proc->total_ticks = 0;
    proc->wait_ticks = 0;

    if (process_stack_allocate(proc, stack_size) != OK) {
        process_discard_new_process(proc);
        return 0;
    }

    /* Os aplicativos nativos continuam compartilhando o espaco do kernel. */
    proc->page_directory = paging_get_current_directory();

    uint32_t stack = proc->kernel_stack_top;
    uint32_t* stack_ptr = (uint32_t*)stack;

    stack_ptr--;
    *stack_ptr = PROCESS_DEFAULT_EFLAGS;
    stack_ptr--;
    *stack_ptr = 0x08;
    stack_ptr--;
    *stack_ptr = (uint32_t)entry_point;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = stack;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;
    stack_ptr--;
    *stack_ptr = 0x10;

    proc->context.esp = (uint32_t)stack_ptr;
    proc->context.eip = (uint32_t)entry_point;
    proc->context.eflags = PROCESS_DEFAULT_EFLAGS;
    proc->context.cs = 0x08;
    proc->context.ds = 0x10;
    proc->context.es = 0x10;
    proc->context.fs = 0x10;
    proc->context.gs = 0x10;
    proc->context.ss = 0x10;
    proc->context.cr3 = (uint32_t)proc->page_directory;
    proc->context.user_entry = 0;
    proc->context.user_mode = 0;
    proc->user_test = 0;

    (void)process_stack_observe(proc, 0, 0U, 0U);

    proc->state = PROCESS_STATE_READY;
    process_count++;
    process_signal_process_created(
        proc->pid, current_process ? current_process->pid : 0U);
    LOG_INFO("PROC", "Processo criado com sucesso");
    return proc;
}

process_t* process_create(const char* name, void (*entry_point)()) {
    return process_create_internal(name, entry_point, KERNEL_STACK_SIZE);
}

process_t* process_create_with_stack_size(const char* name,
                                          void (*entry_point)(),
                                          uint32_t stack_size) {
    return process_create_internal(name, entry_point, stack_size);
}

static int process_pointer_valid(const process_t* proc) {
    uint32_t address;
    uint32_t start;
    uint32_t end;

    if (!proc) return 0;
    address = (uint32_t)proc;
    start = (uint32_t)&processes[0];
    end = start + sizeof(processes);
    return address >= start && address < end &&
           ((address - start) % sizeof(process_t)) == 0;
}

static const char user_test_message[] = "Zephyr ring3 OK\n";

static void process_user_patch_u32(uint8_t* code, uint32_t offset,
                                   uint32_t value) {
    code[offset + 0] = (uint8_t)(value & 0xFF);
    code[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    code[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    code[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static void process_user_emit_mov(uint8_t* code, uint32_t* offset,
                                  uint8_t reg, uint32_t value) {
    code[(*offset)++] = (uint8_t)(0xB8 + reg);
    process_user_patch_u32(code, *offset, value);
    *offset += 4;
}

static uint32_t process_user_build_code(uint8_t* code, int trigger_fault) {
    uint32_t offset = 0;

    if (trigger_fault) {
        code[offset++] = 0xA1;
        process_user_patch_u32(code, offset, 0);
        offset += 4;
        code[offset++] = 0xEB;
        code[offset++] = 0xFE;
        return offset;
    }

    process_user_emit_mov(code, &offset, 0, 1);
    process_user_emit_mov(code, &offset, 3, USER_DATA_BASE);
    process_user_emit_mov(code, &offset, 1, kstrlen(user_test_message));
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    process_user_emit_mov(code, &offset, 0, 2);
    process_user_emit_mov(code, &offset, 3, USER_DATA_BASE + 64);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    process_user_emit_mov(code, &offset, 0, 3);
    process_user_emit_mov(code, &offset, 3, USER_DATA_BASE + 128);
    code[offset++] = 0xCD;
    code[offset++] = 0x80;

    process_user_emit_mov(code, &offset, 0, 0);
    code[offset++] = 0x31;
    code[offset++] = 0xDB;
    code[offset++] = 0xCD;
    code[offset++] = 0x80;
    /* Se a syscall nao encerrar o processo, HLT provoca uma excecao
       controlada em ring 3 em vez de consumir CPU indefinidamente. */
    code[offset++] = 0xF4;
    return offset;
}

static void process_switch_after_termination(void) {
    process_t* previous = current_process;
    process_t* next = scheduler_schedule();

    process_stack_verify_or_panic(previous);
    if (!next || next == previous) {
        next = &processes[0];
    }
    if (next == previous) {
        LOG_ERROR("PROC", "Nao foi possivel sair de processo encerrado");
        return;
    }
    process_stack_verify_or_panic(next);

    current_process = next;
    next->state = PROCESS_STATE_RUNNING;
    if (next->kernel_stack_top) tss_set_kernel_stack(next->kernel_stack_top);
    if (next->page_directory &&
        next->page_directory != paging_get_current_directory()) {
        paging_switch_directory(next->page_directory);
    }
    scheduler_context_switches++;
    process_context_switch(&previous->context, &next->context);
}

static int process_mark_user_zombie(process_t* proc, uint32_t exit_code,
                                    int faulted) {
    int focus_result;

    if (!proc || !process_is_user(proc)) {
        LOG_WARN("PROC", "Encerramento recusado para processo ring 0");
        return ERR_UNAVAILABLE;
    }
    if (proc->state != PROCESS_STATE_RUNNING &&
        proc->state != PROCESS_STATE_READY &&
        proc->state != PROCESS_STATE_BLOCKED) {
        LOG_WARN("PROC", "Estado invalido ao encerrar processo ring 3");
        return ERR_STATE;
    }
    if (proc->wait_active) process_cancel_wait(proc);

    proc->exit_code = exit_code;
    proc->faulted = faulted ? 1U : proc->faulted;
    proc->pending_signals = 0U;
    proc->blocked_signals = 0U;
    proc->active_signal = 0U;
    proc->signal_saved_mask = 0U;
    proc->signal_context_valid = 0U;
    proc->cancel_exit_code = 0U;
    proc->cancel_pending = 0U;
    kmemset(&proc->signal_saved_context, 0,
            sizeof(proc->signal_saved_context));
    if (vfs_fd_table_release(&proc->fd_table) != OK) {
        LOG_ERROR("PROC", "Falha ao liberar descritores no encerramento");
    }
    proc->state = PROCESS_STATE_ZOMBIE;
    process_event_generation++;
    if (!process_event_generation) process_event_generation = 1U;
    if (proc->user_test) {
        user_test_result_pending = 1;
        user_test_result_pid = proc->pid;
        user_test_result_faulted = faulted ? 1U : 0U;
    }
    if (process_get_focus() == proc->pid) {
        focus_result = process_restore_focus();
        if (focus_result != OK) {
            LOG_WARN("PROC", "Falha ao restaurar foco apos encerrar usuario");
        }
    }
    process_signal_process_exited(proc->pid);
    return OK;
}

static int process_mark_current_user_zombie(uint32_t exit_code,
                                             int faulted) {
    return process_mark_user_zombie(current_process, exit_code, faulted);
}

int process_reap_finished_user(void) {
    if (!current_process) {
        LOG_ERROR("PROC", "Reaper de usuario executado sem processo atual");
        return ERR_STATE;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &processes[i];

        if (!proc->context.user_mode ||
            proc->state != PROCESS_STATE_ZOMBIE) continue;
        if (proc->user_test && user_test_result_pending) {
            continue;
        }

        process_destroy(proc);
    }
    return OK;
}

static int process_user_reap_previous_test(void) {
    int result = process_reap_finished_user();

    if (result != OK) return result;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!processes[i].user_test ||
            processes[i].state == PROCESS_STATE_UNUSED) continue;
        if (processes[i].state != PROCESS_STATE_ZOMBIE) {
            LOG_WARN("PROC", "Teste ring 3 ja esta em execucao");
            return ERR_STATE;
        }
        if (user_test_result_pending) {
            LOG_WARN("PROC", "Resultado do teste ring 3 ainda nao foi reportado");
            return ERR_STATE;
        }
    }
    return OK;
}

static int process_user_map_image(page_directory_t* dir) {
    uint32_t code_phys;
    uint32_t data_phys;
    uint32_t launch_phys;
    uint32_t stack_phys;
    int code_mapped = 0;
    int data_mapped = 0;
    int launch_mapped = 0;
    int stack_mapped = 0;

    if (!dir) {
        LOG_ERROR("PROC", "Diretorio nulo para imagem ring 3");
        return ERR_NULL;
    }
    code_phys = (uint32_t)pmm_alloc_page();
    data_phys = (uint32_t)pmm_alloc_page();
    launch_phys = (uint32_t)pmm_alloc_page();
    stack_phys = (uint32_t)pmm_alloc_page();
    if (!code_phys || !data_phys || !launch_phys || !stack_phys) {
        if (code_phys) pmm_free_page((void*)code_phys);
        if (data_phys) pmm_free_page((void*)data_phys);
        if (launch_phys) pmm_free_page((void*)launch_phys);
        if (stack_phys) pmm_free_page((void*)stack_phys);
        LOG_ERROR("PROC", "Memoria insuficiente para paginas ring 3");
        return ERR_MEM;
    }

    code_mapped = paging_map_page_in_directory(
        dir, USER_CODE_BASE, code_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_USER) == OK;
    data_mapped = paging_map_page_in_directory(
        dir, USER_DATA_BASE, data_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE | PAGING_FLAG_USER) == OK;
    launch_mapped = paging_map_page_in_directory(
        dir, USER_LAUNCH_BASE, launch_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE | PAGING_FLAG_USER) == OK;
    stack_mapped = paging_map_page_in_directory(
        dir, USER_STACK_BASE, stack_phys,
        PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE | PAGING_FLAG_USER) == OK;

    if (!code_mapped || !data_mapped || !launch_mapped || !stack_mapped) {
        if (!code_mapped) pmm_free_page((void*)code_phys);
        if (!data_mapped) pmm_free_page((void*)data_phys);
        if (!launch_mapped) pmm_free_page((void*)launch_phys);
        if (!stack_mapped) pmm_free_page((void*)stack_phys);
        LOG_ERROR("PROC", "Falha ao mapear imagem do teste ring 3");
        return ERR_MEM;
    }
    return OK;
}

static int process_user_load_image(page_directory_t* dir,
                                   page_directory_t* kernel_dir,
                                   const uint8_t* code,
                                   uint32_t code_size,
                                   const uint8_t* data,
                                   uint32_t data_size,
                                   const app_launch_info_t* launch) {
    int result = OK;

    if (!dir || !kernel_dir || !code || code_size == 0 ||
        code_size > PAGE_SIZE || data_size > PAGE_SIZE ||
        (data_size > 0 && !data) || !launch) {
        LOG_ERROR("PROC", "Imagem ring 3 invalida para carregamento");
        return ERR_INVALID;
    }

    asm volatile("cli");
    paging_switch_directory(dir);
    kmemset((void*)USER_CODE_BASE, 0, PAGE_SIZE);
    kmemset((void*)USER_DATA_BASE, 0, PAGE_SIZE);
    kmemset((void*)USER_LAUNCH_BASE, 0, PAGE_SIZE);
    kmemset((void*)USER_STACK_BASE, 0, PAGE_SIZE);
    kmemcpy((void*)USER_CODE_BASE, code, code_size);
    if (data_size > 0) {
        kmemcpy((void*)USER_DATA_BASE, data, data_size);
    }
    kmemcpy((void*)USER_LAUNCH_BASE, launch, sizeof(app_launch_info_t));

    /* A imagem pode ter vindo de uma fonte externa. Antes de torna-la
       escalonavel, confirma que a pagina de codigo recebeu todos os bytes. */
    for (uint32_t i = 0; i < code_size; i++) {
        if (((uint8_t*)USER_CODE_BASE)[i] != code[i]) {
            LOG_ERROR("PROC", "Falha ao verificar copia da imagem ring 3");
            result = ERR_STATE;
            break;
        }
    }
    paging_switch_directory(kernel_dir);
    asm volatile("sti");
    return result;
}

static int process_user_validate_launch(const app_launch_info_t* launch) {
    if (!launch || launch->abi_version != APP_LAUNCH_ABI_VERSION ||
        launch->argc > APP_LAUNCH_MAX_ARGS ||
        launch->raw_length > APP_LAUNCH_MAX_RAW_LENGTH ||
        launch->raw_args[launch->raw_length] != '\0') {
        LOG_ERROR("PROC", "Parametros de lancamento ring 3 invalidos");
        return ERR_INVALID;
    }

    for (uint32_t i = 0; i < launch->argc; i++) {
        if (launch->args[i].length == 0 ||
            launch->args[i].offset > launch->raw_length ||
            launch->args[i].length >
                launch->raw_length - launch->args[i].offset) {
            LOG_ERROR("PROC", "Argumento ring 3 fora dos limites");
            return ERR_INVALID;
        }
    }
    return OK;
}

static int process_user_initialize(process_t* proc, page_directory_t* dir,
                                   const char* name,
                                   uint32_t entry_offset,
                                   uint32_t code_size,
                                   int diagnostic_test,
                                   int start_suspended) {
    uint32_t stack_ptr;
    uint32_t entry_point;
    int result;
    int i = 0;

    if (!proc || !dir || !name ||
        entry_offset >= PAGE_SIZE) {
        LOG_ERROR("PROC", "Parametros invalidos para processo ring 3");
        return ERR_NULL;
    }
    kmemset(proc, 0, sizeof(process_t));
    proc->pid = process_allocate_pid();
    if (!proc->pid) {
        LOG_ERROR("PROC", "Falha ao reservar PID do processo ring 3");
        return ERR_MEM;
    }
    while (name[i] && i < PROCESS_NAME_LENGTH - 1) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';
    if (process_wait_state_init(proc) != OK) {
        if (proc->ipc_wait_channel.initialized) {
            wait_channel_reset(&proc->ipc_wait_channel);
        }
        process_release_pid(proc->pid);
        kmemset(proc, 0, sizeof(process_t));
        LOG_ERROR("PROC", "Falha ao inicializar espera do processo ring 3");
        return ERR_STATE;
    }
    if (vfs_fd_table_init(&proc->fd_table) != OK) {
        LOG_ERROR("PROC", "Falha ao inicializar descritores ring 3");
        wait_channel_reset(&proc->ipc_wait_channel);
        process_release_pid(proc->pid);
        kmemset(proc, 0, sizeof(process_t));
        return ERR_STATE;
    }
    if (vfs_fd_table_inherit_cwd(&proc->fd_table,
                                 current_process ?
                                 &current_process->fd_table : 0) != OK) {
        LOG_ERROR("PROC", "Falha ao herdar diretorio ring 3");
        wait_channel_reset(&proc->ipc_wait_channel);
        vfs_fd_table_release(&proc->fd_table);
        process_release_pid(proc->pid);
        kmemset(proc, 0, sizeof(process_t));
        return ERR_STATE;
    }
    proc->page_directory = dir;
    result = process_stack_allocate(proc, KERNEL_STACK_SIZE);
    if (result != OK) {
        wait_channel_reset(&proc->ipc_wait_channel);
        vfs_fd_table_release(&proc->fd_table);
        process_release_pid(proc->pid);
        kmemset(proc, 0, sizeof(process_t));
        proc->state = PROCESS_STATE_UNUSED;
        return result;
    }
    entry_point = USER_CODE_BASE + entry_offset;
    stack_ptr = proc->kernel_stack_top;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = USER_DATA_SELECTOR;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = USER_STACK_TOP;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = PROCESS_DEFAULT_EFLAGS;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = USER_CODE_SELECTOR;
    stack_ptr -= 4; *(uint32_t*)stack_ptr = entry_point;
    proc->context.esp = stack_ptr;
    proc->context.eip = (uint32_t)process_user_enter;
    proc->context.eflags = PROCESS_DEFAULT_EFLAGS;
    proc->context.cs = USER_CODE_SELECTOR;
    proc->context.ss = USER_DATA_SELECTOR;
    proc->context.ds = USER_DATA_SELECTOR;
    proc->context.es = USER_DATA_SELECTOR;
    proc->context.fs = USER_DATA_SELECTOR;
    proc->context.gs = USER_DATA_SELECTOR;
    proc->context.cr3 = (uint32_t)dir;
    proc->context.user_entry = entry_point;
    proc->context.user_mode = 1;
    proc->user_code_size = code_size;
    proc->user_test = diagnostic_test ? 1U : 0U;
    proc->state = start_suspended ? PROCESS_STATE_BLOCKED : PROCESS_STATE_READY;
    (void)process_stack_observe(proc, 0, 0U, 0U);
    process_count++;
    process_signal_process_created(
        proc->pid, current_process ? current_process->pid : 0U);
    return OK;
}

static int process_create_user_image_internal(const char* name,
                                              const uint8_t* code,
                                              uint32_t code_size,
                                              const uint8_t* data,
                                              uint32_t data_size,
                                              uint32_t entry_offset,
                                              uint32_t stack_size,
                                              int diagnostic_test,
                                              int start_suspended,
                                              const app_launch_info_t* launch,
                                              uint32_t* pid_out) {
    process_t* proc = 0;
    page_directory_t* dir;
    page_directory_t* kernel_dir;
    int result;

    if (!paging_is_ready() || !tss_is_ready() ||
        !syscall_user_mode_is_enabled()) {
        LOG_WARN("PROC", "Modo usuario indisponivel para processo");
        return ERR_UNAVAILABLE;
    }
    if (!name || !code || code_size == 0 || code_size > PAGE_SIZE ||
        data_size > PAGE_SIZE || (data_size > 0 && !data) ||
        entry_offset >= code_size || stack_size != PAGE_SIZE) {
        LOG_ERROR("PROC", "Parametros invalidos para imagem ring 3");
        return ERR_INVALID;
    }
    if (!launch) {
        launch = &process_empty_launch;
    }
    result = process_user_validate_launch(launch);
    if (result != OK) return result;
    if (diagnostic_test) {
        result = process_user_reap_previous_test();
        if (result != OK) return result;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_UNUSED) {
            proc = &processes[i];
            break;
        }
    }
    if (!proc) {
        LOG_ERROR("PROC", "Limite de processos ring 3 atingido");
        return ERR_MEM;
    }

    kernel_dir = paging_get_current_directory();
    dir = paging_create_user_directory();
    if (!dir) {
        LOG_ERROR("PROC", "Falha ao criar espaco do teste ring 3");
        return ERR_MEM;
    }
    result = process_user_map_image(dir);
    if (result != OK) {
        paging_free_user_directory(dir);
        return result;
    }
    result = process_user_load_image(dir, kernel_dir, code, code_size,
                                     data, data_size, launch);
    if (result != OK) {
        paging_free_user_directory(dir);
        return result;
    }
    result = process_user_initialize(proc, dir, name,
                                     entry_offset, code_size, diagnostic_test,
                                     start_suspended);
    if (result != OK) {
        paging_free_user_directory(dir);
        return result;
    }
    if (pid_out) *pid_out = proc->pid;
    LOG_DEBUG("PROC", "Processo ring 3 criado a partir de imagem");
    return OK;
}

int process_create_user_image(const char* name, const uint8_t* code,
                              uint32_t code_size, const uint8_t* data,
                              uint32_t data_size, uint32_t entry_offset,
                              uint32_t stack_size, int diagnostic_test,
                              uint32_t* pid_out) {
    return process_create_user_image_internal(
        name, code, code_size, data, data_size, entry_offset, stack_size,
        diagnostic_test, 0, 0, pid_out);
}

int process_create_user_image_suspended(const char* name,
                                        const uint8_t* code,
                                        uint32_t code_size,
                                        const uint8_t* data,
                                        uint32_t data_size,
                                        uint32_t entry_offset,
                                        uint32_t stack_size,
                                        uint32_t* pid_out) {
    return process_create_user_image_internal(
        name, code, code_size, data, data_size, entry_offset, stack_size,
        0, 1, 0, pid_out);
}

int process_create_user_image_suspended_with_launch(
    const char* name, const uint8_t* code, uint32_t code_size,
    const uint8_t* data, uint32_t data_size, uint32_t entry_offset,
    uint32_t stack_size, const app_launch_info_t* launch,
    uint32_t* pid_out) {
    return process_create_user_image_internal(
        name, code, code_size, data, data_size, entry_offset, stack_size,
        0, 1, launch, pid_out);
}

int process_create_user_test(int trigger_fault, uint32_t* pid_out) {
    uint8_t code[128];
    uint32_t code_size = process_user_build_code(code, trigger_fault);

    return process_create_user_image("UserTest", code, code_size,
                                     (const uint8_t*)user_test_message,
                                     kstrlen(user_test_message), 0,
                                     PAGE_SIZE, 1, pid_out);
}

int process_cancel_user_test(uint32_t pid, uint32_t exit_code) {
    process_t* proc = process_get_by_pid(pid);
    int result;

    if (!proc) {
        LOG_WARN("PROC", "UserTest nao encontrado para cancelamento");
        return ERR_NOT_FOUND;
    }
    if (!proc->user_test) {
        LOG_WARN("PROC", "PID informado nao e um UserTest");
        return ERR_UNAVAILABLE;
    }
    if (proc->state == PROCESS_STATE_ZOMBIE) return OK;

    result = process_mark_user_zombie(proc, exit_code, 0);
    if (result != OK) {
        LOG_ERROR("PROC", "Falha ao cancelar UserTest");
        return result;
    }
    LOG_DEBUG("PROC", "UserTest cancelado");
    return OK;
}

uint32_t process_get_user_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED &&
            processes[i].context.user_mode) count++;
    }
    return count;
}

int process_start_user(uint32_t pid) {
    process_t* proc = process_get_by_pid(pid);

    if (!proc || !process_is_user(proc)) {
        LOG_ERROR("PROC", "PID invalido ao iniciar processo ring 3");
        return ERR_NOT_FOUND;
    }
    if (proc->state != PROCESS_STATE_BLOCKED) {
        LOG_WARN("PROC", "Processo ring 3 nao estava suspenso");
        return ERR_STATE;
    }

    proc->state = PROCESS_STATE_READY;
    LOG_DEBUG("PROC", "Processo ring 3 liberado para execucao");
    return OK;
}

int process_is_user(const process_t* proc) {
    return process_pointer_valid(proc) && proc->context.user_mode;
}

int process_exit_current(uint32_t exit_code) {
    int result;

    if (exit_code == APP_EXIT_CANCELLED) {
        LOG_ERROR("PROC", "Aplicativo tentou usar codigo de cancelamento reservado");
        return ERR_INVALID;
    }

    current_process->termination_signal = 0U;
    result = process_mark_current_user_zombie(exit_code, 0);

    if (result != OK) return result;
    LOG_DEBUG("PROC", "Processo ring 3 encerrado por syscall");
    return OK;
}

int process_cancel_user(uint32_t pid, uint32_t exit_code) {
    process_t* proc = process_get_by_pid(pid);
    int result;

    if (!proc) return ERR_NOT_FOUND;
    if (!process_is_user(proc) || proc->user_test) return ERR_UNAVAILABLE;
    if (proc == current_process) {
        LOG_WARN("PROC", "Cancelamento do processo atual requer trampoline");
        return ERR_STATE;
    }
    if (proc->cancel_pending) return OK;

    proc->termination_signal = 0U;
    if (proc->state == PROCESS_STATE_BLOCKED && proc->wait_active) {
        proc->cancel_exit_code = exit_code;
        proc->cancel_pending = 1U;
        result = process_cancel_wait(proc);
        if (result != OK) {
            proc->cancel_exit_code = 0U;
            proc->cancel_pending = 0U;
            LOG_ERROR("PROC", "Falha ao acordar processo para cancelamento");
            return result;
        }
        LOG_DEBUG("PROC", "Cancelamento pendente aguardando retorno ring3");
        return OK;
    }
    result = process_mark_user_zombie(proc, exit_code, 0);
    if (result != OK) return result;

    LOG_DEBUG("PROC", "Processo ring 3 cancelado");
    return OK;
}

int process_cancel_focused_user(uint32_t exit_code) {
    return process_cancel_user(process_get_focus(), exit_code);
}

int process_terminate_user_signal(uint32_t pid, uint32_t signal_number,
                                  int faulted) {
    process_t* proc = process_get_by_pid(pid);
    int result;

    if (!proc || !process_is_user(proc)) {
        LOG_WARN("PROC", "Destino invalido para encerramento por sinal");
        return ERR_NOT_FOUND;
    }
    if (signal_number != APP_SIGNAL_INT &&
        signal_number != APP_SIGNAL_KILL &&
        signal_number != APP_SIGNAL_SEGV &&
        signal_number != APP_SIGNAL_TERM) {
        LOG_WARN("PROC", "Sinal invalido para encerramento de processo");
        return ERR_INVALID;
    }
    if (proc->state == PROCESS_STATE_ZOMBIE) return OK;

    proc->termination_signal = signal_number;
    proc->last_signal = signal_number;
    proc->signal_delivered++;
    result = process_mark_user_zombie(
        proc, APP_EXIT_FROM_SIGNAL(signal_number), faulted);
    if (result != OK) {
        LOG_ERROR_CODE("PROC", result,
                       "Falha ao encerrar processo por sinal");
        return result;
    }
    LOG_DEBUG("PROC", "Processo ring3 encerrado por sinal");
    return OK;
}

int process_handle_user_exception(registers_t* regs) {
    uint32_t fault_address = 0;
    int result;

    if (!regs || !current_process || !process_is_user(current_process) ||
        ((regs->cs & 0x03U) != 0x03U)) {
        return ERR_STATE;
    }
    if (regs->int_no == 14) {
        asm volatile("mov %%cr2, %0" : "=r"(fault_address));
    } else {
        fault_address = regs->eip;
    }
    current_process->fault_vector = regs->int_no;
    current_process->fault_error = regs->err_code;
    current_process->fault_address = fault_address;
    current_process->faulted = 1;
    result = process_signal_record_user_fault(regs);
    if (result != OK) {
        LOG_ERROR("PROC", "Falha ao encerrar processo apos excecao de usuario");
        return ERR_STATE;
    }
    last_user_fault_valid = 1;
    last_user_fault.pid = current_process->pid;
    last_user_fault.vector = regs->int_no;
    last_user_fault.error = regs->err_code;
    user_fault_count++;
    LOG_WARN("PROC", "Excecao de usuario isolada; processo encerrado");
    return OK;
}

int process_prepare_user_termination(registers_t* regs) {
    if (!regs) {
        LOG_ERROR("PROC", "Registradores nulos ao preparar retorno de usuario");
        return ERR_NULL;
    }
    if (!current_process || !process_is_user(current_process) ||
        current_process->state != PROCESS_STATE_ZOMBIE ||
        (regs->cs & 0x03U) != 0x03U) {
        LOG_ERROR("PROC", "Retorno de usuario invalido para processo encerrado");
        return ERR_STATE;
    }

    /* O IRET sai da pilha da interrupcao antes de trocar de contexto.
       Assim, nunca reutilizamos um frame de ring 3 ja encerrado. */
    regs->eip = (uint32_t)process_user_termination_enter;
    regs->cs = KERNEL_CODE_SELECTOR;
    /* Nenhuma IRQ deve interromper a pequena janela entre o IRET e a
       troca de contexto. O contexto seguinte restaura seus proprios flags. */
    regs->eflags &= ~0x200U;
    return OK;
}

int process_apply_pending_cancel(registers_t* regs) {
    uint32_t exit_code;
    int result;

    if (!regs) {
        LOG_ERROR("PROC", "Registradores nulos ao aplicar cancelamento");
        return ERR_NULL;
    }
    if (!current_process || !process_is_user(current_process) ||
        !current_process->cancel_pending ||
        (regs->cs & 0x03U) != 0x03U) {
        LOG_ERROR("PROC", "Cancelamento pendente sem retorno ring3 valido");
        return ERR_STATE;
    }
    exit_code = current_process->cancel_exit_code;
    result = process_mark_current_user_zombie(exit_code, 0);
    if (result != OK) {
        LOG_ERROR("PROC", "Falha ao concluir cancelamento pendente");
        return result;
    }
    return process_prepare_user_termination(regs);
}

void process_finish_user_termination(void) {
    if (!current_process || !process_is_user(current_process) ||
        current_process->state != PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Trampoline de encerramento sem processo ring 3 zombie");
        while (1) asm volatile("hlt");
    }

    process_switch_after_termination();
    LOG_ERROR("PROC", "Troca de contexto retornou apos encerrar usuario");
    while (1) asm volatile("hlt");
}

int process_take_user_test_result(uint32_t* pid, uint32_t* faulted) {
    if (!pid || !faulted) {
        LOG_ERROR("PROC", "Destino nulo ao consultar resultado do UserTest");
        return ERR_NULL;
    }
    if (!user_test_result_pending) return ERR_NOT_FOUND;

    *pid = user_test_result_pid;
    *faulted = user_test_result_faulted;
    user_test_result_pending = 0;
    return OK;
}

void process_destroy(process_t* proc) {
    uint32_t pid;

    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao destruir processo");
        return;
    }
    if (proc->state == PROCESS_STATE_UNUSED) {
        LOG_WARN("PROC", "Processo ja esta inutilizado");
        return;
    }
    if (proc->state < PROCESS_STATE_UNUSED ||
        proc->state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao destruir processo");
        return;
    }
    if (proc == current_process || proc->pid == 0) {
        LOG_WARN("PROC", "Destruicao do processo atual ou Idle bloqueada");
        return;
    }
    if (proc->page_directory &&
        proc->page_directory == paging_get_current_directory() &&
        proc->context.user_mode) {
        LOG_WARN("PROC", "Diretorio ativo impede destruicao do processo");
        return;
    }

    if (proc->wait_active) process_cancel_wait(proc);
    if (proc->ipc_wait_channel.initialized &&
        wait_channel_reset(&proc->ipc_wait_channel) != OK) {
        LOG_ERROR("PROC", "Falha ao destruir canal IPC do processo");
        return;
    }
    if (vfs_fd_table_release(&proc->fd_table) != OK) {
        LOG_ERROR("PROC", "Falha ao liberar descritores do processo");
        return;
    }

    if (process_get_focus() == proc->pid &&
        process_restore_focus() != OK) {
        LOG_WARN("PROC", "Processo destruido sem fallback de foco valido");
    }
    pid = proc->pid;
    process_signal_process_destroyed(pid);
    proc->state = PROCESS_STATE_UNUSED;
    process_stack_release(proc);
    if (proc->page_directory && proc->context.user_mode) {
        paging_free_user_directory(proc->page_directory);
    }
    if (process_count > 0) process_count--;
    kmemset(proc, 0, sizeof(process_t));
    proc->state = PROCESS_STATE_UNUSED;
    process_release_pid(pid);
    LOG_DEBUG("PROC", "Processo destruido");
}

process_t* process_get_current(void) {
    return current_process;
}

int process_stack_get_info(uint32_t pid, process_stack_info_t* output) {
    process_t* proc;

    if (!output) {
        LOG_ERROR("PROC", "Destino nulo ao consultar stack do processo");
        return ERR_NULL;
    }
    proc = process_get_by_pid(pid);
    if (!proc) {
        LOG_ERROR_CODE("PROC", (int32_t)pid,
                       "PID inexistente ao consultar stack");
        return ERR_NOT_FOUND;
    }
    return process_stack_observe(proc, output, 1U, 1U);
}

int process_stack_check_current(uint32_t* remaining_out) {
    process_stack_info_t info;
    int result;

    if (!remaining_out) {
        LOG_ERROR("PROC", "Destino nulo ao checar stack atual");
        return ERR_NULL;
    }
    *remaining_out = 0U;
    if (!current_process) {
        LOG_ERROR("PROC", "Stack atual indisponivel");
        return ERR_STATE;
    }
    result = process_stack_observe(current_process, &info, 1U, 1U);
    *remaining_out = info.minimum_bytes_free;
    return result;
}

int process_stack_validate_all(process_stack_validation_t* validation) {
    process_stack_info_t info;
    int result = OK;

    if (!validation) {
        LOG_ERROR("PROC", "Destino nulo ao validar stacks");
        return ERR_NULL;
    }
    kmemset(validation, 0, sizeof(process_stack_validation_t));
    for (uint32_t index = 0U; index < MAX_PROCESSES; index++) {
        process_t* proc = &processes[index];
        int observe_result;

        if (proc->state == PROCESS_STATE_UNUSED) continue;
        validation->checked++;
        observe_result = process_stack_observe(proc, &info, 1U, 1U);
        if (observe_result == OK || observe_result == ERR_OVERFLOW) {
            validation->valid++;
        }
        if (!info.lower_canary_ok || !info.upper_canary_ok) {
            validation->corrupted++;
        }
        if (observe_result == ERR_OVERFLOW &&
            info.lower_canary_ok && info.upper_canary_ok) {
            validation->low_water++;
            result = ERR_OVERFLOW;
        } else if (observe_result != OK) {
            result = observe_result;
        }
    }
    if (validation->corrupted) {
        LOG_ERROR("PROC", "Validacao encontrou canario de stack invalido");
        return ERR_OVERFLOW;
    }
    return result;
}

int process_stack_self_test(void) {
    process_stack_info_t info;
    uint32_t lower_guard;
    int result;

    if (!process_stack_size_valid(KERNEL_STACK_SIZE) ||
        process_stack_size_valid(PROCESS_KERNEL_STACK_MIN_SIZE -
                                 PROCESS_KERNEL_STACK_ALIGNMENT) ||
        process_stack_size_valid(PROCESS_KERNEL_STACK_MAX_SIZE +
                                 PROCESS_KERNEL_STACK_ALIGNMENT) ||
        process_stack_size_valid(KERNEL_STACK_SIZE + 1U)) {
        LOG_ERROR("PROC", "Autoteste de limites de stack falhou");
        return ERR_STATE;
    }

    kmemset(&process_stack_test_process, 0, sizeof(process_stack_test_process));
    process_stack_test_process.pid = MAX_PROCESSES;
    process_stack_copy_name(process_stack_test_process.name, "stack-selftest");
    result = process_stack_attach(&process_stack_test_process,
                                  (uint32_t)process_stack_test_storage,
                                  KERNEL_STACK_SIZE, 0U);
    if (result != OK ||
        (process_stack_test_process.kernel_stack %
         PROCESS_KERNEL_STACK_ALIGNMENT) != 0U) {
        LOG_ERROR("PROC", "Autoteste de alinhamento de stack falhou");
        return ERR_STATE;
    }
    result = process_stack_observe(&process_stack_test_process, &info, 0U, 0U);
    if (result != OK || info.bytes_used != 0U ||
        info.bytes_free != KERNEL_STACK_SIZE) {
        LOG_ERROR("PROC", "Autoteste de stack inicial falhou");
        return ERR_STATE;
    }

    kmemset((void*)(process_stack_test_process.kernel_stack_top - 128U),
            0, 128U);
    result = process_stack_observe(&process_stack_test_process, &info, 0U, 0U);
    if (result != OK || info.peak_bytes_used < 128U ||
        info.minimum_bytes_free > KERNEL_STACK_SIZE - 128U) {
        LOG_ERROR("PROC", "Autoteste de high-water de stack falhou");
        return ERR_STATE;
    }

    if (process_stack_attach(&process_stack_test_process,
                             (uint32_t)process_stack_test_storage,
                             KERNEL_STACK_SIZE, 0U) != OK) {
        LOG_ERROR("PROC", "Autoteste nao restaurou stack de teste");
        return ERR_STATE;
    }
    kmemset((void*)(process_stack_test_process.kernel_stack +
                     PROCESS_STACK_LOW_WATER_BYTES),
            0, KERNEL_STACK_SIZE - PROCESS_STACK_LOW_WATER_BYTES);
    result = process_stack_observe(&process_stack_test_process, &info, 0U, 0U);
    if (result != ERR_OVERFLOW || !info.low_water_active ||
        info.minimum_bytes_free != PROCESS_STACK_LOW_WATER_BYTES) {
        LOG_ERROR("PROC", "Autoteste de margem baixa de stack falhou");
        return ERR_STATE;
    }

    lower_guard = process_stack_test_process.kernel_stack -
                  PROCESS_STACK_GUARD_BYTES;
    if (!process_stack_guard_matches(lower_guard, PROCESS_STACK_CANARY_LOWER)) {
        LOG_ERROR("PROC", "Autoteste de canario inferior falhou");
        return ERR_STATE;
    }
    process_stack_write_guard(lower_guard, 0U);
    if (process_stack_guard_matches(lower_guard, PROCESS_STACK_CANARY_LOWER)) {
        LOG_ERROR("PROC", "Autoteste nao detectou canario corrompido");
        return ERR_STATE;
    }
    kmemset(&process_stack_test_process, 0, sizeof(process_stack_test_process));
    return OK;
}

uint32_t process_get_count(void) {
    return process_count;
}

uint32_t process_get_user_fault_count(void) {
    return user_fault_count;
}

int process_get_last_user_fault(process_user_fault_summary_t* summary) {
    if (!summary) {
        LOG_ERROR("PROC", "Destino nulo ao consultar ultima falha de usuario");
        return ERR_NULL;
    }
    if (!last_user_fault_valid) return ERR_NOT_FOUND;

    *summary = last_user_fault;
    return OK;
}

process_t* process_get_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROCESS_STATE_UNUSED &&
            processes[i].pid == pid) {
            return &processes[i];
        }
    }

    LOG_DEBUG("PROC", "PID nao encontrado");
    return 0;
}

uint32_t process_get_current_pid(void) {
    if (!current_process) {
        LOG_WARN("PROC", "Processo atual indisponivel");
        return 0;
    }
    return current_process->pid;
}

uint32_t process_get_event_generation(void) {
    return process_event_generation;
}

uint32_t process_get_state_count(process_state_t state) {
    uint32_t count = 0;

    if (state < PROCESS_STATE_UNUSED || state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao contar processos");
        return 0;
    }

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == state) count++;
    }
    return count;
}

static process_t* scheduler_find_next_ready(void) {
    int start_idx = last_scheduled_idx;

    if (start_idx < 1 || start_idx >= MAX_PROCESSES) {
        start_idx = MAX_PROCESSES - 1;
    }

    for (int step = 1; step < MAX_PROCESSES; step++) {
        int idx = 1 + ((start_idx - 1 + step) % (MAX_PROCESSES - 1));
        if (processes[idx].state == PROCESS_STATE_READY) {
            last_scheduled_idx = idx;
            return &processes[idx];
        }
    }

    return 0;
}

process_t* scheduler_schedule(void) {
    process_t* next = scheduler_find_next_ready();
    process_t* idle = &processes[0];

    if (next) return next;
    if (idle->state == PROCESS_STATE_READY ||
        idle->state == PROCESS_STATE_RUNNING) {
        scheduler_idle_fallbacks++;
        return idle;
    }

    LOG_ERROR("PROC", "Idle indisponivel para fallback do scheduler");
    return 0;
}

static void scheduler_yield_internal(void) {
    process_t* next = scheduler_schedule();

    if (!next || next == current_process) return;

    process_t* prev = current_process;
    process_stack_verify_or_panic(prev);
    process_stack_verify_or_panic(next);
    current_process = next;
    current_process->state = PROCESS_STATE_RUNNING;

    if (prev->state == PROCESS_STATE_RUNNING) {
        prev->state = PROCESS_STATE_READY;
    }

    if (next->kernel_stack_top) {
        tss_set_kernel_stack(next->kernel_stack_top);
    }
    if (next->page_directory &&
        next->page_directory != paging_get_current_directory()) {
        paging_switch_directory(next->page_directory);
    }
    scheduler_context_switches++;
    process_context_switch(&prev->context, &next->context);
}

void process_yield(void) {
    /* O timer pode disparar antes do bootstrap do Idle; isso nao e falha. */
    if (!current_process) return;
    if (current_process->state < PROCESS_STATE_READY ||
        current_process->state > PROCESS_STATE_BLOCKED) {
        return;
    }

    scheduler_cooperative_yields++;
    scheduler_yield_internal();
}

void scheduler_preempt_user(void) {
    if (!current_process || !process_is_user(current_process) ||
        current_process->state != PROCESS_STATE_RUNNING) {
        LOG_WARN("PROC", "Preempcao de usuario fora de estado executavel");
        return;
    }

    scheduler_user_preemptions++;
    scheduler_yield_internal();
}

void process_block(uint32_t ticks) {
    if (!current_process) {
        LOG_ERROR("PROC", "Tentativa de bloquear sem processo atual");
        return;
    }
    if (current_process->pid == 0 ||
        current_process->state != PROCESS_STATE_RUNNING) {
        LOG_WARN("PROC", "Estado invalido para bloquear processo");
        return;
    }
    if (ticks == 0) {
        LOG_WARN("PROC", "Bloqueio solicitado com duracao zero");
        return;
    }
    current_process->wait_channel = 0;
    current_process->wait_condition = 0U;
    current_process->wait_deadline = WAIT_TIMEOUT_INFINITE;
    current_process->wait_reason = WAIT_REASON_NONE;
    current_process->wait_deadline_active = 0U;
    current_process->wait_active = 0U;
    current_process->state = PROCESS_STATE_BLOCKED;
    current_process->wait_ticks = ticks;
    process_yield();
}

void process_unblock(process_t* proc) {
    uint32_t flags;

    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao desbloquear processo");
        return;
    }
    if (proc->state < PROCESS_STATE_UNUSED ||
        proc->state > PROCESS_STATE_ZOMBIE) {
        LOG_ERROR("PROC", "Estado invalido ao desbloquear processo");
        return;
    }
    flags = process_wait_irq_save();
    if (proc->wait_active) {
        process_wait_clear(proc, WAIT_REASON_EVENT);
        process_wait_irq_restore(flags);
        LOG_DEBUG("PROC", "Processo acordado por evento");
        return;
    }
    if (proc->state == PROCESS_STATE_BLOCKED) {
        proc->state = PROCESS_STATE_READY;
        proc->wait_ticks = 0U;
        proc->wait_reason = WAIT_REASON_EVENT;
        LOG_DEBUG("PROC", "Processo desbloqueado");
    } else {
        LOG_DEBUG("PROC", "Processo nao estava bloqueado");
    }
    process_wait_irq_restore(flags);
}

int process_wait(wait_channel_t* channel, uint32_t observed_condition,
                 uint32_t timeout_ticks, wait_reason_t* out_reason) {
    process_t* current = process_get_current();
    int result;

    if (!out_reason) {
        LOG_ERROR("PROC", "Destino nulo para resultado da espera");
        return ERR_NULL;
    }
    *out_reason = WAIT_REASON_NONE;
    if (!current || current->pid == 0U ||
        current->state != PROCESS_STATE_RUNNING) {
        LOG_ERROR("PROC", "Espera sem processo executavel");
        return ERR_STATE;
    }
    if (!channel || !channel->initialized || current->wait_active ||
        current->wait_entry.linked) {
        LOG_ERROR("PROC", "Canal ou estado invalido para espera");
        return ERR_INVALID;
    }
    if (timeout_ticks != WAIT_TIMEOUT_INFINITE &&
        timeout_ticks > WAIT_MAX_TIMEOUT_TICKS) {
        LOG_ERROR("PROC", "Timeout de espera excede o limite");
        return ERR_INVALID;
    }

    result = wait_queue_entry_init(
        &current->wait_entry, current, current->name, WAIT_TARGET_PROCESS,
        current->pid, process_wait_block_transition,
        process_wait_wake_transition, process_wait_yield_transition);
    if (result != OK) return result;
    result = wait_queue_block(channel, &current->wait_entry,
                              observed_condition, timeout_ticks, out_reason);
    current->wait_reason = *out_reason;
    return result;
}

int process_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                         wait_reason_t reason, uint32_t* out_woken) {
    return wait_queue_wake_target(channel, WAIT_TARGET_PROCESS, mode,
                                  reason, out_woken);
}

int process_cancel_wait(process_t* proc) {
    if (!process_pointer_valid(proc)) {
        LOG_ERROR("PROC", "Ponteiro invalido ao cancelar espera");
        return ERR_NULL;
    }
    if (!proc->wait_active || proc->state != PROCESS_STATE_BLOCKED ||
        !proc->wait_entry.linked) {
        LOG_WARN("PROC", "Processo nao possui espera cancelavel");
        return ERR_STATE;
    }
    return wait_queue_remove(&proc->wait_entry, WAIT_REASON_CANCELLED);
}

int process_copy_waiters(wait_info_t* output, uint32_t max_entries,
                         uint32_t* out_count) {
    uint32_t count = 0U;
    uint32_t flags;

    if (!out_count || (max_entries && !output)) {
        LOG_ERROR("PROC", "Destino invalido para lista de esperas");
        return ERR_NULL;
    }
    flags = process_wait_irq_save();
    for (uint32_t index = 0U; index < MAX_PROCESSES && count < max_entries;
         index++) {
        process_t* proc = &processes[index];
        wait_info_t* info;

        if (!proc->wait_active || proc->state != PROCESS_STATE_BLOCKED) {
            continue;
        }
        info = &output[count++];
        kmemset(info, 0, sizeof(*info));
        info->id = proc->pid;
        info->target = WAIT_TARGET_PROCESS;
        process_copy_wait_text(info->name, WAIT_INFO_NAME_LENGTH, proc->name);
        process_copy_wait_text(info->channel_owner,
                               WAIT_CHANNEL_OWNER_LENGTH,
                               proc->wait_channel ?
                               proc->wait_channel->owner : "");
        info->reason = proc->wait_reason;
        info->deadline_tick = proc->wait_deadline;
        info->deadline_active = proc->wait_deadline_active;
        if (info->deadline_active) {
            wait_deadline_remaining_active(proc->wait_deadline,
                                           proc->wait_deadline_active,
                                           &info->remaining_ticks);
        }
        info->active = 1U;
    }
    *out_count = count;
    process_wait_irq_restore(flags);
    return OK;
}

void scheduler_init(void) {
    process_count = 0;
    last_scheduled_idx = -1;
    scheduler_context_switches = 0;
    scheduler_cooperative_yields = 0;
    scheduler_user_preemptions = 0;
    scheduler_idle_fallbacks = 0;
    LOG_INFO("PROC", "Scheduler round-robin inicializado");
}

void scheduler_tick(void) {
    uint32_t now;

    if (!current_process) return;
    now = timer_get_ticks();

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_BLOCKED) {
            if (processes[i].wait_active) {
                if (!processes[i].wait_channel ||
                    !processes[i].wait_channel->available) {
                    process_wait_clear(&processes[i],
                                       WAIT_REASON_DEVICE_UNAVAILABLE);
                } else if (process_wait_deadline_reached(&processes[i], now)) {
                    process_wait_clear(&processes[i], WAIT_REASON_TIMEOUT);
                } else if (processes[i].wait_deadline_active) {
                    processes[i].wait_ticks = processes[i].wait_deadline - now;
                }
            } else if (processes[i].wait_ticks > 0) {
                processes[i].wait_ticks--;
                if (processes[i].wait_ticks == 0) {
                    processes[i].state = PROCESS_STATE_READY;
                    processes[i].wait_reason = WAIT_REASON_TIMEOUT;
                }
            }
        }
    }

    current_process->total_ticks++;
}

void scheduler_get_stats(scheduler_stats_t* stats) {
    if (!stats) {
        LOG_ERROR("PROC", "Destino nulo ao consultar metricas do scheduler");
        return;
    }

    stats->context_switches = scheduler_context_switches;
    stats->cooperative_yields = scheduler_cooperative_yields;
    stats->user_preemptions = scheduler_user_preemptions;
    stats->idle_fallbacks = scheduler_idle_fallbacks;
    stats->user_quantum_ticks = SCHEDULER_USER_QUANTUM_TICKS;
}

static uint32_t scheduler_validate_pid_table(void) {
    uint32_t active_count = 0;
    uint32_t valid = 1;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &processes[i];

        if (proc->state == PROCESS_STATE_UNUSED) {
            if (proc->pid != 0) valid = 0;
            continue;
        }

        active_count++;
        if ((i == 0 && proc->pid != 0) || (i != 0 && proc->pid == 0)) {
            valid = 0;
        }
        for (int previous = 0; previous < i; previous++) {
            if (processes[previous].state != PROCESS_STATE_UNUSED &&
                processes[previous].pid == proc->pid) {
                valid = 0;
            }
        }
    }

    return valid && active_count == process_count;
}

static uint32_t scheduler_validate_states(void) {
    uint32_t valid = 1;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &processes[i];

        if (proc->state > PROCESS_STATE_ZOMBIE) valid = 0;
        if (proc->state == PROCESS_STATE_BLOCKED && proc->wait_ticks == 0 &&
            !proc->context.user_mode && !proc->wait_active) {
            valid = 0;
        }
        if (proc->state == PROCESS_STATE_ZOMBIE && proc == current_process) {
            valid = 0;
        }
    }

    return valid;
}

int scheduler_validate_invariants(scheduler_validation_t* validation) {
    process_t* idle = &processes[0];
    process_stack_validation_t stack_validation;
    int stack_result;

    if (!validation) {
        LOG_ERROR("PROC", "Destino nulo ao validar scheduler");
        return ERR_NULL;
    }

    kmemset(validation, 0, sizeof(scheduler_validation_t));
    validation->current_valid = process_pointer_valid(current_process) &&
                                current_process->state == PROCESS_STATE_RUNNING &&
                                process_get_state_count(PROCESS_STATE_RUNNING) == 1;
    validation->idle_valid = idle->pid == 0 && !idle->context.user_mode &&
                             (idle->state == PROCESS_STATE_READY ||
                              idle->state == PROCESS_STATE_RUNNING);
    validation->pid_table_valid = scheduler_validate_pid_table();
    validation->state_table_valid = scheduler_validate_states();
    stack_result = process_stack_validate_all(&stack_validation);
    validation->stack_table_valid = stack_result == OK &&
                                    stack_validation.checked == process_count &&
                                    stack_validation.valid == process_count &&
                                    stack_validation.low_water == 0U &&
                                    stack_validation.corrupted == 0U;

    if (!validation->current_valid) LOG_ERROR("PROC", "Invariante do processo atual violada");
    if (!validation->idle_valid) LOG_ERROR("PROC", "Invariante do Idle violada");
    if (!validation->pid_table_valid) LOG_ERROR("PROC", "Invariante da tabela de PIDs violada");
    if (!validation->state_table_valid) LOG_ERROR("PROC", "Invariante dos estados violada");
    if (!validation->stack_table_valid) LOG_ERROR("PROC", "Invariante das stacks violada");
    if (!validation->current_valid || !validation->idle_valid ||
        !validation->pid_table_valid || !validation->state_table_valid ||
        !validation->stack_table_valid) {
        return stack_result == OK ? ERR_STATE : stack_result;
    }

    return OK;
}
