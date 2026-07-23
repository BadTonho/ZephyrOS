#include "process/thread.h"
#include "core/memory.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"

#define THREAD_TEST_ITERATIONS 3U
#define THREAD_TEST_TRACE_SIZE (THREAD_TEST_ITERATIONS * 2U)

static thread_t threads[MAX_THREADS];
static thread_t* current_thread = 0;
static uint32_t thread_count = 0;
static uint32_t next_thread_id = 1;
static uint32_t scheduler_esp = 0;
static int scheduler_active = 0;
static int last_scheduled_idx = -1;
static int thread_initialized = 0;
static uint32_t thread_test_a_runs = 0;
static uint32_t thread_test_b_runs = 0;
static uint32_t thread_test_trace_pos = 0;
static char thread_test_trace[THREAD_TEST_TRACE_SIZE];

static int thread_index(const thread_t* thread) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (&threads[i] == thread) return i;
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
        kmemset(&threads[i], 0, sizeof(thread_t));
        threads[i].state = THREAD_UNUSED;
    }

    current_thread = 0;
    thread_count = 0;
    next_thread_id = 1;
    scheduler_esp = 0;
    scheduler_active = 0;
    last_scheduled_idx = -1;
    thread_initialized = 1;
    LOG_INFO("THRD", "Scheduler cooperativo inicializado com sucesso");
}

thread_t* thread_create(const char* name, void (*entry)(void)) {
    thread_t* thread = 0;
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
        if (threads[i].state == THREAD_UNUSED) {
            thread = &threads[i];
            break;
        }
    }

    if (!thread) {
        LOG_ERROR("THRD", "Limite de threads atingido");
        return 0;
    }

    kmemset(thread, 0, sizeof(thread_t));
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
        kmemset(thread, 0, sizeof(thread_t));
        thread->state = THREAD_UNUSED;
        return 0;
    }

    thread->esp = thread_prepare_stack(thread);
    if (!thread->esp) {
        LOG_ERROR("THRD", "Falha ao preparar contexto da thread");
        kfree(thread->stack);
        kmemset(thread, 0, sizeof(thread_t));
        thread->state = THREAD_UNUSED;
        return 0;
    }

    thread->eip = (uint32_t)entry;
    thread_count++;
    LOG_INFO("THRD", "Thread criada com sucesso");
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

    if (thread->stack) {
        kfree(thread->stack);
        thread->stack = 0;
    }
    kmemset(thread, 0, sizeof(thread_t));
    thread->state = THREAD_UNUSED;
    if (thread_count > 0) thread_count--;
}

thread_t* thread_schedule_next(void) {
    for (int step = 1; step <= MAX_THREADS; step++) {
        int index = (last_scheduled_idx + step) % MAX_THREADS;
        if (threads[index].state == THREAD_RUNNING) {
            return &threads[index];
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
    kmemset(thread_test_trace, 0, sizeof(thread_test_trace));
    thread_a = thread_create("ThreadTestA", thread_test_worker_a);
    thread_b = thread_create("ThreadTestB", thread_test_worker_b);
    if (!thread_a || !thread_b) {
        if (thread_a) thread_destroy(thread_a);
        if (thread_b) thread_destroy(thread_b);
        LOG_ERROR("THRD", "Auto teste nao criou as threads necessarias");
        return ERR_MEM;
    }

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
    if (result != OK) {
        LOG_ERROR("THRD", "Auto teste detectou troca de contexto invalida");
        return result;
    }

    LOG_INFO("THRD", "Auto teste cooperativo concluido com sucesso");
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

    current_thread->state = THREAD_BLOCKED;
    current_thread->wait_ticks = ticks;
    thread_yield();
}

void thread_block_indefinite(void) {
    if (!current_thread) {
        LOG_ERROR("THRD", "Tentativa de bloquear sem thread atual");
        return;
    }

    current_thread->state = THREAD_BLOCKED;
    current_thread->wait_ticks = 0;
    thread_yield();
}

void thread_unblock(thread_t* thread) {
    if (!thread || thread_index(thread) < 0) {
        LOG_ERROR("THRD", "Ponteiro invalido ao desbloquear thread");
        return;
    }
    if (thread->state != THREAD_BLOCKED) {
        LOG_WARN("THRD", "Thread nao estava bloqueada");
        return;
    }

    thread->state = THREAD_RUNNING;
    thread->wait_ticks = 0;
}

thread_t* thread_get_current(void) {
    return current_thread;
}

thread_t* thread_get_by_id(uint32_t id) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].id == id && threads[i].state != THREAD_UNUSED) {
            return &threads[i];
        }
    }
    return 0;
}

uint32_t thread_get_count(void) {
    return thread_count;
}

void thread_scheduler_tick(void) {
    if (!thread_initialized) return;

    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].state == THREAD_BLOCKED && threads[i].wait_ticks > 0) {
            threads[i].wait_ticks--;
            if (threads[i].wait_ticks == 0) {
                threads[i].state = THREAD_RUNNING;
            }
        }
    }
}
