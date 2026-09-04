#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/wait.h"
#include "process/process.h"
#include "process/thread.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define THREAD_POOL_CAPACITY MAX_THREADS
#define THREAD_OWNER_PID 42U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static thread_t thread_pool[THREAD_POOL_CAPACITY];
static uint8_t stack_pool[THREAD_POOL_CAPACITY][THREAD_STACK_SIZE]
    __attribute__((aligned(16)));
static uint8_t fake_cache_storage[64U];
static uint32_t cache_alloc_index;
static uint32_t stack_alloc_index;
static uint8_t fake_cache_failure;
static uint8_t fake_stack_failure;
static uint32_t fake_ticks = 100U;
static process_t owner_process;
static void (*switch_hook)(void);
static thread_t* hook_thread;
static wait_channel_t hook_channel;
static int hook_result;
static wait_reason_t hook_reason;
static uint8_t hook_current_seen;

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
    printf("ZCOV_BEGIN|case=host:process:threads|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:process:threads|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:process:threads|value=0x%08X\n",
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

void kmemset(void* destination, uint8_t value, uint32_t size) {
    memset(destination, value, size);
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    memcpy(destination, source, size);
}

void* kmalloc(uint32_t size) {
    (void)size;
    if (fake_stack_failure || stack_alloc_index >= THREAD_POOL_CAPACITY) {
        return NULL;
    }
    return stack_pool[stack_alloc_index++];
}

void kfree(void* pointer) {
    (void)pointer;
}

kmem_cache_t* kmem_cache_create(const char* name, uint32_t object_size,
                                uint32_t alignment) {
    (void)name;
    (void)object_size;
    (void)alignment;
    if (fake_cache_failure) return NULL;
    return (kmem_cache_t*)fake_cache_storage;
}

void* kmem_cache_alloc(kmem_cache_t* cache) {
    (void)cache;
    if (fake_cache_failure || cache_alloc_index >= THREAD_POOL_CAPACITY) {
        return NULL;
    }
    return &thread_pool[cache_alloc_index++];
}

void kmem_cache_free(kmem_cache_t* cache, void* object) {
    (void)cache;
    (void)object;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks;
}

process_t* process_get_current(void) {
    return &owner_process;
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
    if (!queue || !entry || !out_reason) return ERR_NULL;
    if (!queue->initialized) return ERR_STATE;
    entry->queue = queue;
    entry->observed_condition = observed_condition;
    entry->deadline_active = timeout_ticks != WAIT_TIMEOUT_INFINITE;
    entry->deadline_tick = entry->deadline_active ?
                           fake_ticks + timeout_ticks :
                           WAIT_TIMEOUT_INFINITE;
    entry->reason = WAIT_REASON_NONE;
    entry->linked = 1U;
    if (entry->block) entry->block(entry->target, entry);
    if (entry->yield) entry->yield(entry->target);
    *out_reason = WAIT_REASON_NONE;
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
                           wait_target_type_t target, wait_wake_mode_t mode,
                           wait_reason_t reason, uint32_t* out_woken) {
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
    if (!out_remaining) return ERR_NULL;
    *out_remaining = active && deadline_tick > fake_ticks ?
                     deadline_tick - fake_ticks : 0U;
    return OK;
}

void thread_context_switch(uint32_t* previous_esp, uint32_t next_esp) {
    (void)previous_esp;
    (void)next_esp;
    if (switch_hook) {
        void (*hook)(void) = switch_hook;
        switch_hook = NULL;
        hook();
    }
}

static void reset_fixture(void) {
    memset(&owner_process, 0, sizeof(owner_process));
    owner_process.pid = THREAD_OWNER_PID;
    cache_alloc_index = 0U;
    stack_alloc_index = 0U;
    fake_cache_failure = 0U;
    fake_stack_failure = 0U;
    fake_ticks = 100U;
    switch_hook = NULL;
    hook_thread = NULL;
    hook_result = ERR_STATE;
    hook_reason = WAIT_REASON_NONE;
    hook_current_seen = 0U;
    memset(&hook_channel, 0, sizeof(hook_channel));
    thread_init();
}

static void thread_entry_fixture(void) {
}

static void observe_current_hook(void) {
    hook_current_seen = thread_get_current() == hook_thread;
    thread_destroy(thread_get_current());
}

static void wait_hook(void) {
    hook_current_seen = thread_get_current() == hook_thread;
    hook_result = thread_wait(&hook_channel, 7U, 5U, &hook_reason);
}

static void invalid_wait_hook(void) {
    wait_channel_t invalid_channel;
    wait_reason_t reason;

    memset(&invalid_channel, 0, sizeof(invalid_channel));
    hook_result = thread_wait(&invalid_channel, 0U, 1U, &reason);
    if (hook_result != ERR_INVALID) return;
    hook_result = thread_wait(&hook_channel, 0U,
                              WAIT_MAX_TIMEOUT_TICKS + 1U, &reason);
    if (hook_result != ERR_INVALID) return;
    hook_result = thread_wait(&hook_channel, 0U, 1U, NULL);
}

static void block_hook(void) {
    hook_current_seen = thread_get_current() == hook_thread;
    thread_block(0U);
    if (hook_thread->state != THREAD_RUNNING) {
        hook_result = ERR_STATE;
        return;
    }
    thread_block(3U);
    if (hook_thread->state != THREAD_BLOCKED || hook_thread->wait_ticks != 3U) {
        hook_result = ERR_STATE;
        return;
    }
    thread_unblock(hook_thread);
    thread_block_indefinite();
    hook_result = hook_thread->state == THREAD_BLOCKED &&
                          hook_thread->wait_ticks == 0U ? OK : ERR_STATE;
}

static int test_initialization_and_lifecycle(void) {
    thread_t* first;
    thread_t* second;
    char long_name[THREAD_NAME_LENGTH + 8U];

    if (thread_create("before", thread_entry_fixture) != NULL) return 1;
    fake_cache_failure = 1U;
    thread_init();
    if (thread_is_ready()) return 2;
    fake_cache_failure = 0U;
    reset_fixture();
    if (!thread_is_ready() || thread_get_current() != NULL ||
        thread_get_count() != 0U || thread_schedule_next() != NULL) return 3;
    memset(long_name, 'x', sizeof(long_name));
    long_name[sizeof(long_name) - 1U] = '\0';
    first = thread_create(long_name, thread_entry_fixture);
    second = thread_create("second", thread_entry_fixture);
    if (!first || !second || first->owner_pid != THREAD_OWNER_PID ||
        first->state != THREAD_RUNNING || first->name[THREAD_NAME_LENGTH - 1U] ||
        thread_get_count() != 2U || thread_get_count_by_owner(THREAD_OWNER_PID) != 2U) {
        return 4;
    }
    if (thread_create(NULL, thread_entry_fixture) != NULL ||
        thread_create("bad", NULL) != NULL) return 5;
    if (thread_get_by_id(first->id) != first ||
        thread_get_by_id(second->id) != second ||
        thread_get_by_id(0xFFFFFFFFU) != NULL ||
        thread_schedule_next() != first) return 6;
    first->state = THREAD_BLOCKED;
    if (thread_schedule_next() != second) return 7;
    first->state = THREAD_RUNNING;
    hook_thread = first;
    switch_hook = observe_current_hook;
    thread_yield();
    if (!hook_current_seen || thread_get_current() != NULL ||
        thread_get_by_id(first->id) != first) return 8;
    thread_destroy(NULL);
    thread_destroy((thread_t*)&owner_process);
    second->state = THREAD_UNUSED;
    thread_destroy(second);
    if (thread_get_count() != 2U) return 9;
    second->state = THREAD_RUNNING;
    thread_destroy(second);
    thread_destroy(first);
    return thread_get_count() == 0U ? 0 : 10;
}

static int test_creation_failures(void) {
    thread_t* thread;

    reset_fixture();
    fake_cache_failure = 1U;
    thread = thread_create("cache-fail", thread_entry_fixture);
    fake_cache_failure = 0U;
    if (thread != NULL || thread_get_count() != 0U) return 1;
    fake_stack_failure = 1U;
    thread = thread_create("stack-fail", thread_entry_fixture);
    fake_stack_failure = 0U;
    if (thread != NULL || thread_get_count() != 0U) return 2;
    return 0;
}

static int test_wait_contract(void) {
    thread_t* thread;
    wait_info_t info;
    uint32_t count = 0U;
    uint32_t woken = 0U;

    reset_fixture();
    thread = thread_create("waiting-thread", thread_entry_fixture);
    if (!thread || wait_channel_init(&hook_channel, "THREAD-CH") != OK) {
        return 1;
    }
    hook_thread = thread;
    switch_hook = wait_hook;
    thread_yield();
    if (!hook_current_seen || hook_result != OK || hook_reason != WAIT_REASON_NONE ||
        !thread->wait_active || !thread->wait_entry.linked ||
        thread->state != THREAD_BLOCKED || thread->wait_ticks != 5U) return 2;
    if (thread_copy_waiters(NULL, 1U, &count) != ERR_NULL ||
        thread_copy_waiters(&info, 1U, NULL) != ERR_NULL ||
        thread_copy_waiters(&info, 1U, &count) != OK || count != 1U ||
        info.id != thread->id || info.target != WAIT_TARGET_THREAD ||
        strcmp(info.name, "waiting-thread") != 0 ||
        strcmp(info.channel_owner, "THREAD-CH") != 0 || !info.active ||
        !info.deadline_active || info.remaining_ticks != 5U) return 3;
    if (thread_cancel_wait(thread) != OK || thread->wait_active ||
        thread->wait_entry.linked || thread->state != THREAD_RUNNING ||
        thread->wait_reason != WAIT_REASON_CANCELLED ||
        thread_cancel_wait(thread) != ERR_STATE) return 4;
    if (thread_wake_channel(&hook_channel, WAIT_WAKE_ALL, WAIT_REASON_EVENT,
                            NULL) != ERR_NULL ||
        thread_wake_channel(&hook_channel, WAIT_WAKE_ALL, WAIT_REASON_EVENT,
                            &woken) != OK || woken != 0U) return 5;
    switch_hook = invalid_wait_hook;
    thread_yield();
    if (hook_result != ERR_NULL) return 6;
    if (thread_wait(&hook_channel, 0U, 1U, &hook_reason) != ERR_STATE) return 7;
    thread->state = THREAD_BLOCKED;
    thread->wait_ticks = 4U;
    thread_unblock(thread);
    if (thread->state != THREAD_RUNNING || thread->wait_reason != WAIT_REASON_EVENT) {
        return 8;
    }
    if (thread_cancel_wait((thread_t*)&owner_process) != ERR_NULL) return 9;
    thread_destroy(thread);
    return 0;
}

static int test_blocks_and_ticks(void) {
    thread_t* thread;

    reset_fixture();
    thread = thread_create("blocking-thread", thread_entry_fixture);
    if (!thread) return 1;
    hook_thread = thread;
    hook_result = ERR_STATE;
    switch_hook = block_hook;
    thread_yield();
    if (!hook_current_seen || hook_result != OK || thread_get_current() != NULL ||
        thread->state != THREAD_BLOCKED || thread->wait_ticks != 0U) return 2;
    thread_unblock(thread);
    thread->state = THREAD_BLOCKED;
    thread->wait_ticks = 2U;
    thread_scheduler_tick();
    if (thread->state != THREAD_BLOCKED || thread->wait_ticks != 1U) return 3;
    thread_scheduler_tick();
    if (thread->state != THREAD_RUNNING || thread->wait_reason != WAIT_REASON_TIMEOUT) {
        return 4;
    }
    if (wait_channel_init(&hook_channel, "THREAD-CH") != OK) return 5;
    hook_thread = thread;
    switch_hook = wait_hook;
    thread_yield();
    if (thread->state != THREAD_BLOCKED || !thread->wait_active ||
        !thread->wait_entry.linked) return 6;
    hook_channel.available = 0U;
    thread_scheduler_tick();
    if (thread->state != THREAD_RUNNING || thread->wait_reason !=
        WAIT_REASON_DEVICE_UNAVAILABLE) return 6;
    hook_channel.available = 1U;
    switch_hook = wait_hook;
    thread_yield();
    if (thread->state != THREAD_BLOCKED || !thread->wait_active ||
        !thread->wait_entry.linked) return 7;
    thread->wait_deadline_active = 1U;
    thread->wait_deadline = fake_ticks + 10U;
    thread_scheduler_tick();
    if (thread->state != THREAD_BLOCKED || thread->wait_ticks != 10U) return 8;
    fake_ticks += 10U;
    thread_scheduler_tick();
    if (thread->state != THREAD_RUNNING || thread->wait_reason != WAIT_REASON_TIMEOUT) {
        return 9;
    }
    thread_destroy(thread);
    return 0;
}

static int test_thread_limit(void) {
    thread_t* threads[THREAD_POOL_CAPACITY];

    reset_fixture();
    for (uint32_t index = 0U; index < THREAD_POOL_CAPACITY; index++) {
        threads[index] = thread_create("capacity", thread_entry_fixture);
        if (!threads[index]) return 1;
    }
    if (thread_create("overflow", thread_entry_fixture) != NULL ||
        thread_get_count() != THREAD_POOL_CAPACITY ||
        thread_get_count_by_owner(THREAD_OWNER_PID) != THREAD_POOL_CAPACITY) {
        return 2;
    }
    for (uint32_t index = 0U; index < THREAD_POOL_CAPACITY; index++) {
        thread_destroy(threads[index]);
    }
    return thread_get_count() == 0U ? 0 : 3;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) {
        result = test_initialization_and_lifecycle();
    }
    if (!result) {
        result = test_creation_failures();
    }
    if (!result) {
        result = test_wait_contract();
    }
    if (!result) {
        result = test_blocks_and_ticks();
    }
    if (!result) {
        result = test_thread_limit();
    }
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        fprintf(stderr, "thread host failure: %d\n", result);
        return result;
    }
    printf("thread host: PASS\n");
    return 0;
}
