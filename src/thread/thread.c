#include "process/thread.h"
#include "process/process.h"
#include "core/memory.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "core/timer.h"
#include "memory/slab.h"

#define THREAD_WAIT_EFLAGS_INTERRUPT_ENABLE (1U << 9U)

#define THREAD_TEST_ITERATIONS 3U
#define THREAD_TEST_TRACE_SIZE (THREAD_TEST_ITERATIONS * 2U)

static thread_t* threads[MAX_THREADS];
static kmem_cache_t* thread_cache = 0;
static thread_t* current_thread = 0;
static uint32_t thread_count = 0;
static uint32_t next_thread_id = 1;
static uint32_t scheduler_esp = 0;
static int scheduler_active = 0;
static int last_scheduled_idx = -1;
static int thread_initialized = 0;
static int thread_self_test_active = 0;
static uint32_t thread_test_a_runs = 0;
static uint32_t thread_test_b_runs = 0;
static uint32_t thread_test_trace_pos = 0;
static char thread_test_trace[THREAD_TEST_TRACE_SIZE];

static uint32_t thread_wait_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void thread_wait_irq_restore(uint32_t flags) {
    if (flags & THREAD_WAIT_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static void thread_wait_block_transition(void* target,
                                         wait_queue_entry_t* entry) {
    thread_t* thread = (thread_t*)target;

    thread->wait_channel = entry->queue;
    thread->wait_condition = entry->observed_condition;
    thread->wait_deadline = entry->deadline_tick;
    thread->wait_reason = WAIT_REASON_NONE;
    thread->wait_deadline_active = entry->deadline_active;
    thread->wait_active = 1U;
    thread->wait_ticks = entry->deadline_active ?
                         entry->deadline_tick - timer_get_ticks() : 0U;
    thread->state = THREAD_BLOCKED;
}

static void thread_wait_wake_transition(void* target,
                                        wait_queue_entry_t* entry) {
    thread_t* thread = (thread_t*)target;

    thread->wait_active = 0U;
    thread->wait_channel = 0;
    thread->wait_condition = 0U;
    thread->wait_deadline = WAIT_TIMEOUT_INFINITE;
    thread->wait_reason = entry->reason;
    thread->wait_deadline_active = 0U;
    thread->wait_ticks = 0U;
    if (thread->state == THREAD_BLOCKED) thread->state = THREAD_RUNNING;
}

static void thread_wait_yield_transition(void* target) {
    (void)target;
    thread_yield();
}

static void thread_wait_clear(thread_t* thread, wait_reason_t reason) {
    if (!thread) return;
    if (thread->wait_entry.linked) {
        if (wait_queue_remove(&thread->wait_entry, reason) != OK) {
            LOG_ERROR("THRD", "Falha ao remover thread da fila de espera");
        }
        return;
    }
    thread->wait_active = 0U;
    thread->wait_channel = 0;
    thread->wait_condition = 0U;
    thread->wait_deadline = WAIT_TIMEOUT_INFINITE;
    thread->wait_reason = reason;
    thread->wait_deadline_active = 0U;
    thread->wait_ticks = 0U;
    if (thread->state == THREAD_BLOCKED) thread->state = THREAD_RUNNING;
}

static int thread_wait_deadline_reached(const thread_t* thread,
                                        uint32_t now) {
    if (!thread || !thread->wait_deadline_active) return 0;
    return (int32_t)(now - thread->wait_deadline) >= 0;
}

static void thread_copy_wait_text(char* destination, uint32_t capacity,
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

static int thread_index(const thread_t* thread) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i] == thread) return i;
    }
    return -1;
}

static void thread_entry_trampoline(void) {
    thread_t* thread = current_thread;

    if (!thread || !thread->entry) {
        LOG_ERROR("THRD", "Thread sem ponto de entrada");
    } else {
        thread->entry();
    }

    if (current_thread) {
        current_thread->state = THREAD_FINISHED;
        current_thread->wait_ticks = 0;
        thread_yield();
    }

    LOG_ERROR("THRD", "Thread finalizada sem retornar ao scheduler");
    while (1) asm volatile("hlt");
}

static uint32_t thread_prepare_stack(thread_t* thread) {
    uint32_t* stack_ptr;

    if (!thread || !thread->stack) return 0;

    stack_ptr = (uint32_t*)((uint8_t*)thread->stack + THREAD_STACK_SIZE);
    stack_ptr--; *stack_ptr = (uint32_t)thread_entry_trampoline;

    /* pushad/popad preserva EDI, ESI, EBP, ESP, EBX, EDX, ECX e EAX. */
    for (uint32_t i = 0; i < 8; i++) {
        stack_ptr--;
        *stack_ptr = 0;
    }

    return (uint32_t)stack_ptr;
}

static void thread_switch_to_scheduler(thread_t* previous) {
    if (!previous || !scheduler_active || !scheduler_esp) {
        LOG_ERROR("THRD", "Retorno ao scheduler sem contexto valido");
        return;
    }

    current_thread = 0;
    thread_context_switch(&previous->esp, scheduler_esp);
}

static void thread_test_record(char marker) {
    if (thread_test_trace_pos < THREAD_TEST_TRACE_SIZE) {
        thread_test_trace[thread_test_trace_pos++] = marker;
    }
}

static void thread_test_worker_a(void) {
    for (uint32_t i = 0; i < THREAD_TEST_ITERATIONS; i++) {
        thread_test_a_runs++;
        thread_test_record('A');
        thread_yield();
    }
}

static void thread_test_worker_b(void) {
    for (uint32_t i = 0; i < THREAD_TEST_ITERATIONS; i++) {
        thread_test_b_runs++;
        thread_test_record('B');
        thread_yield();
    }
}

void thread_init(void) {
    LOG_INFO("THRD", "Inicializando scheduler cooperativo de threads");
    thread_initialized = 0;

    for (int i = 0; i < MAX_THREADS; i++) {
        threads[i] = 0;
    }

    current_thread = 0;
    thread_count = 0;
    next_thread_id = 1;
    scheduler_esp = 0;
    scheduler_active = 0;
    last_scheduled_idx = -1;
    thread_self_test_active = 0;
    thread_cache = kmem_cache_create("thread", sizeof(thread_t), 16U);
    if (!thread_cache) {
        LOG_ERROR("THRD", "Falha ao criar cache de threads");
        return;
    }
    thread_initialized = 1;
    LOG_INFO("THRD", "Scheduler cooperativo inicializado com sucesso");
}

int thread_is_ready(void) {
    return thread_initialized;
}

thread_t* thread_create(const char* name, void (*entry)(void)) {
    thread_t* thread = 0;
    process_t* owner;
    int name_index = 0;

    if (!thread_initialized) {
        LOG_ERROR("THRD", "Criacao solicitada antes da inicializacao");
        return 0;
    }
    if (!name || !entry) {
        LOG_ERROR("THRD", "Parametros invalidos ao criar thread");
        return 0;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        if (!threads[i]) {
            thread = (thread_t*)kmem_cache_alloc(thread_cache);
            if (thread) threads[i] = thread;
            break;
        }
    }

    if (!thread) {
        LOG_ERROR("THRD", "Limite de threads atingido");
        return 0;
    }

    kmemset(thread, 0, sizeof(thread_t));
    owner = process_get_current();
    thread->owner_pid = owner ? owner->pid : 0U;
    while (name[name_index] && name_index < THREAD_NAME_LENGTH - 1) {
        thread->name[name_index] = name[name_index];
        name_index++;
    }
    thread->name[name_index] = '\0';

    thread->id = next_thread_id++;
    if (next_thread_id == 0) next_thread_id = 1;
    thread->state = THREAD_RUNNING;
    thread->entry = entry;
    thread->stack = (uint32_t*)kmalloc(THREAD_STACK_SIZE);
    if (!thread->stack) {
        LOG_ERROR("THRD", "Falha ao alocar stack da thread");
        threads[thread_index(thread)] = 0;
        kmem_cache_free(thread_cache, thread);
        return 0;
    }

    thread->esp = thread_prepare_stack(thread);
    if (!thread->esp) {
        LOG_ERROR("THRD", "Falha ao preparar contexto da thread");
        kfree(thread->stack);
        thread->stack = 0;
        threads[thread_index(thread)] = 0;
        kmem_cache_free(thread_cache, thread);
        return 0;
    }

    thread->eip = (uint32_t)entry;
    thread->wait_deadline = WAIT_TIMEOUT_INFINITE;
    thread->wait_reason = WAIT_REASON_NONE;
    thread->wait_deadline_active = 0U;
    thread_count++;
    if (thread_self_test_active) {
        LOG_DEBUG("THRD", "Thread de auto teste criada");
    } else {
        LOG_INFO("THRD", "Thread criada com sucesso");
    }
    return thread;
}

void thread_destroy(thread_t* thread) {
    if (!thread || thread_index(thread) < 0) {
        LOG_ERROR("THRD", "Ponteiro invalido ao destruir thread");
        return;
    }
    if (thread == current_thread) {
        LOG_WARN("THRD", "Tentativa de destruir thread atual recusada");
        return;
    }
    if (thread->state == THREAD_UNUSED) {
        LOG_WARN("THRD", "Tentativa de destruir thread ja liberada");
        return;
    }

    if (thread->wait_active) thread_cancel_wait(thread);

    if (thread->stack) {
        kfree(thread->stack);
        thread->stack = 0;
    }
    {
        int index = thread_index(thread);
        if (index >= 0) threads[index] = 0;
    }
    kmem_cache_free(thread_cache, thread);
    if (thread_count > 0) thread_count--;
}

thread_t* thread_schedule_next(void) {
    for (int step = 1; step <= MAX_THREADS; step++) {
        int index = (last_scheduled_idx + step) % MAX_THREADS;
        if (threads[index] && threads[index]->state == THREAD_RUNNING) {
            return threads[index];
        }
    }

    return 0;
}

void thread_yield(void) {
    thread_t* next = thread_schedule_next();

    if (!current_thread) {
        if (!next) return;
        if (scheduler_active) {
            LOG_ERROR("THRD", "Scheduler cooperativo reentrante");
            return;
        }

        scheduler_active = 1;
        current_thread = next;
        last_scheduled_idx = thread_index(next);
        thread_context_switch(&scheduler_esp, next->esp);
        scheduler_active = 0;
        current_thread = 0;
        return;
    }

    if (next && next != current_thread) {
        thread_t* previous = current_thread;
        current_thread = next;
        last_scheduled_idx = thread_index(next);
        thread_context_switch(&previous->esp, next->esp);
        return;
    }

    if (current_thread->state != THREAD_RUNNING) {
        thread_switch_to_scheduler(current_thread);
    }
}

int thread_run_self_test(void) {
    thread_t* thread_a;
    thread_t* thread_b;
    int result = OK;

    if (!thread_initialized || current_thread || scheduler_active) {
        LOG_ERROR("THRD", "Auto teste solicitado em estado invalido");
        return ERR_STATE;
    }
    if (thread_count != 0) {
        LOG_WARN("THRD", "Auto teste requer scheduler sem threads pendentes");
        return ERR_STATE;
    }

    thread_test_a_runs = 0;
    thread_test_b_runs = 0;
    thread_test_trace_pos = 0;
    thread_self_test_active = 1;
    kmemset(thread_test_trace, 0, sizeof(thread_test_trace));
    thread_a = thread_create("ThreadTestA", thread_test_worker_a);
    thread_b = thread_create("ThreadTestB", thread_test_worker_b);
    if (!thread_a || !thread_b) {
        if (thread_a) thread_destroy(thread_a);
        if (thread_b) thread_destroy(thread_b);
        thread_self_test_active = 0;
        LOG_ERROR("THRD", "Auto teste nao criou as threads necessarias");
        return ERR_MEM;
    }

    last_scheduled_idx = MAX_THREADS - 1;
    thread_yield();
    for (uint32_t i = 0; i < THREAD_TEST_TRACE_SIZE; i++) {
        char expected = (i & 1U) == 0 ? 'A' : 'B';
        if (thread_test_trace[i] != expected) {
            result = ERR_STATE;
            break;
        }
    }
    if (thread_test_a_runs != THREAD_TEST_ITERATIONS ||
        thread_test_b_runs != THREAD_TEST_ITERATIONS) {
        result = ERR_STATE;
    }

    thread_destroy(thread_a);
    thread_destroy(thread_b);
    thread_self_test_active = 0;
    if (result != OK) {
        LOG_ERROR("THRD", "Auto teste detectou troca de contexto invalida");
        return result;
    }

    LOG_DEBUG("THRD", "Auto teste cooperativo concluido com sucesso");
    return OK;
}

void thread_block(uint32_t ticks) {
    if (!current_thread) {
        LOG_ERROR("THRD", "Tentativa de bloquear sem thread atual");
        return;
    }
    if (ticks == 0) {
        LOG_WARN("THRD", "Bloqueio temporizado com duracao zero");
        return;
    }

    current_thread->wait_channel = 0;
    current_thread->wait_condition = 0U;
    current_thread->wait_deadline = WAIT_TIMEOUT_INFINITE;
    current_thread->wait_reason = WAIT_REASON_NONE;
    current_thread->wait_deadline_active = 0U;
    current_thread->wait_active = 0U;
    current_thread->state = THREAD_BLOCKED;
    current_thread->wait_ticks = ticks;
    thread_yield();
}

void thread_block_indefinite(void) {
    if (!current_thread) {
        LOG_ERROR("THRD", "Tentativa de bloquear sem thread atual");
        return;
    }

    current_thread->wait_channel = 0;
    current_thread->wait_condition = 0U;
    current_thread->wait_deadline = WAIT_TIMEOUT_INFINITE;
    current_thread->wait_reason = WAIT_REASON_NONE;
    current_thread->wait_deadline_active = 0U;
    current_thread->wait_active = 0U;
    current_thread->state = THREAD_BLOCKED;
    current_thread->wait_ticks = 0;
    thread_yield();
}

void thread_unblock(thread_t* thread) {
    uint32_t flags;

    if (!thread || thread_index(thread) < 0) {
        LOG_ERROR("THRD", "Ponteiro invalido ao desbloquear thread");
        return;
    }
    if (thread->state != THREAD_BLOCKED) {
        LOG_WARN("THRD", "Thread nao estava bloqueada");
        return;
    }

    flags = thread_wait_irq_save();
    if (thread->wait_active) thread_wait_clear(thread, WAIT_REASON_EVENT);
    else {
        thread->state = THREAD_RUNNING;
        thread->wait_ticks = 0U;
        thread->wait_reason = WAIT_REASON_EVENT;
    }
    thread_wait_irq_restore(flags);
}

int thread_wait(wait_channel_t* channel, uint32_t observed_condition,
                uint32_t timeout_ticks, wait_reason_t* out_reason) {
    int result;

    if (!out_reason) {
        LOG_ERROR("THRD", "Destino nulo para resultado da espera");
        return ERR_NULL;
    }
    *out_reason = WAIT_REASON_NONE;
    if (!current_thread || current_thread->state != THREAD_RUNNING) {
        LOG_ERROR("THRD", "Espera sem thread executavel");
        return ERR_STATE;
    }
    if (!channel || !channel->initialized || current_thread->wait_active ||
        current_thread->wait_entry.linked) {
        LOG_ERROR("THRD", "Canal ou estado invalido para espera");
        return ERR_INVALID;
    }
    if (timeout_ticks != WAIT_TIMEOUT_INFINITE &&
        timeout_ticks > WAIT_MAX_TIMEOUT_TICKS) {
        LOG_ERROR("THRD", "Timeout de espera excede o limite");
        return ERR_INVALID;
    }

    result = wait_queue_entry_init(
        &current_thread->wait_entry, current_thread, current_thread->name,
        WAIT_TARGET_THREAD, current_thread->id,
        thread_wait_block_transition, thread_wait_wake_transition,
        thread_wait_yield_transition);
    if (result != OK) return result;
    result = wait_queue_block(channel, &current_thread->wait_entry,
                              observed_condition, timeout_ticks, out_reason);
    current_thread->wait_reason = *out_reason;
    return result;
}

int thread_wake_channel(wait_channel_t* channel, wait_wake_mode_t mode,
                        wait_reason_t reason, uint32_t* out_woken) {
    return wait_queue_wake_target(channel, WAIT_TARGET_THREAD, mode,
                                  reason, out_woken);
}

int thread_cancel_wait(thread_t* thread) {
    if (!thread || thread_index(thread) < 0) {
        LOG_ERROR("THRD", "Ponteiro invalido ao cancelar espera");
        return ERR_NULL;
    }
    if (!thread->wait_active || thread->state != THREAD_BLOCKED ||
        !thread->wait_entry.linked) {
        LOG_WARN("THRD", "Thread nao possui espera cancelavel");
        return ERR_STATE;
    }
    return wait_queue_remove(&thread->wait_entry, WAIT_REASON_CANCELLED);
}

int thread_copy_waiters(wait_info_t* output, uint32_t max_entries,
                        uint32_t* out_count) {
    uint32_t count = 0U;
    uint32_t flags;

    if (!out_count || (max_entries && !output)) {
        LOG_ERROR("THRD", "Destino invalido para lista de esperas");
        return ERR_NULL;
    }
    flags = thread_wait_irq_save();
    for (uint32_t index = 0U; index < MAX_THREADS && count < max_entries;
         index++) {
        thread_t* thread = threads[index];
        wait_info_t* info;

        if (!thread || !thread->wait_active ||
            thread->state != THREAD_BLOCKED) continue;
        info = &output[count++];
        kmemset(info, 0, sizeof(*info));
        info->id = thread->id;
        info->target = WAIT_TARGET_THREAD;
        thread_copy_wait_text(info->name, WAIT_INFO_NAME_LENGTH, thread->name);
        thread_copy_wait_text(info->channel_owner,
                              WAIT_CHANNEL_OWNER_LENGTH,
                              thread->wait_channel ?
                              thread->wait_channel->owner : "");
        info->reason = thread->wait_reason;
        info->deadline_tick = thread->wait_deadline;
        info->deadline_active = thread->wait_deadline_active;
        if (info->deadline_active) {
            wait_deadline_remaining_active(thread->wait_deadline,
                                           thread->wait_deadline_active,
                                           &info->remaining_ticks);
        }
        info->active = 1U;
    }
    *out_count = count;
    thread_wait_irq_restore(flags);
    return OK;
}

thread_t* thread_get_current(void) {
    return current_thread;
}

thread_t* thread_get_by_id(uint32_t id) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i] && threads[i]->id == id) {
            return threads[i];
        }
    }
    return 0;
}

uint32_t thread_get_count(void) {
    return thread_count;
}

uint32_t thread_get_count_by_owner(uint32_t owner_pid) {
    uint32_t count = 0U;

    for (uint32_t index = 0U; index < MAX_THREADS; index++) {
        if (threads[index] && threads[index]->owner_pid == owner_pid) {
            count++;
        }
    }
    return count;
}

void thread_scheduler_tick(void) {
    uint32_t now;

    if (!thread_initialized) return;
    now = timer_get_ticks();

    for (int i = 0; i < MAX_THREADS; i++) {
        thread_t* thread = threads[i];

        if (!thread) continue;
        if (thread->state == THREAD_BLOCKED && thread->wait_active) {
            if (!thread->wait_channel || !thread->wait_channel->available) {
                thread_wait_clear(thread,
                                  WAIT_REASON_DEVICE_UNAVAILABLE);
            } else if (thread_wait_deadline_reached(thread, now)) {
                thread_wait_clear(thread, WAIT_REASON_TIMEOUT);
            } else if (thread->wait_deadline_active) {
                thread->wait_ticks = thread->wait_deadline - now;
            }
        } else if (thread->state == THREAD_BLOCKED && thread->wait_ticks > 0) {
            thread->wait_ticks--;
            if (thread->wait_ticks == 0) {
                thread->state = THREAD_RUNNING;
                thread->wait_reason = WAIT_REASON_TIMEOUT;
            }
        }
    }
}
