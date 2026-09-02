#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/wait.h"
#include "core/workqueue.h"
#include "drivers/ata.h"
#include "fs/block.h"
#include "fs/block_cache.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FAKE_SECTOR_COUNT 256U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks;
static uint32_t fake_log_count;
static uint8_t fake_disk[HOST_FAKE_SECTOR_COUNT][BLOCK_SECTOR_SIZE];
static ata_device_t fake_ata;
static const block_device_t* nested_device;
static uint8_t nested_read_buffer[BLOCK_CACHE_BLOCK_SIZE];
static int nested_read_result;
static uint8_t nested_read_active;
static uint8_t execute_scheduled_work;

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
    printf("ZCOV_BEGIN|case=host:storage:block|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:block|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:block|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "block-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint32_t memory_get_free_pages(void) {
    return 4096U;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    fake_log_count++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
    fake_log_count++;
}

static int fake_disk_range(uint32_t lba, uint8_t count) {
    return lba < HOST_FAKE_SECTOR_COUNT &&
           count <= HOST_FAKE_SECTOR_COUNT - lba;
}

static int fake_ata_read(uint8_t slot, uint32_t lba, uint8_t count,
                         uint8_t* buffer) {
    if (slot != 0U || !buffer || !count || !fake_disk_range(lba, count)) {
        return ERR_DISK;
    }
    memcpy(buffer, fake_disk[lba], (uint32_t)count * BLOCK_SECTOR_SIZE);
    return OK;
}

static int fake_ata_write(uint8_t slot, uint32_t lba, uint8_t count,
                          const uint8_t* buffer) {
    if (slot != 0U || !buffer || !count || !fake_disk_range(lba, count)) {
        return ERR_DISK;
    }
    memcpy(fake_disk[lba], buffer, (uint32_t)count * BLOCK_SECTOR_SIZE);
    return OK;
}

ata_device_t* ata_get_device(void) {
    return &fake_ata;
}

int ata_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 1U;
    return OK;
}

int ata_get_device_at(uint8_t slot, ata_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (slot != 0U) return ERR_NOT_FOUND;
    *out_device = fake_ata;
    return OK;
}

int ata_read_device_sectors(uint8_t slot, uint32_t lba, uint8_t count,
                            uint8_t* buffer) {
    return fake_ata_read(slot, lba, count, buffer);
}

int ata_write_device_sectors(uint8_t slot, uint32_t lba, uint8_t count,
                             const uint8_t* buffer) {
    return fake_ata_write(slot, lba, count, buffer);
}

int ata_flush_device(uint8_t slot) {
    return slot == 0U ? OK : ERR_NOT_FOUND;
}

int ata_get_device_counters(uint8_t slot, uint32_t* out_reads,
                            uint32_t* out_writes) {
    if (!out_reads || !out_writes) return ERR_NULL;
    if (slot != 0U) return ERR_NOT_FOUND;
    *out_reads = 0U;
    *out_writes = 0U;
    return OK;
}

uint32_t ata_get_read_ops(void) {
    return 0U;
}

uint32_t ata_get_write_ops(void) {
    return 0U;
}

int work_init(work_struct_t* work, const char* owner,
              work_priority_t priority, work_func_t callback, void* context) {
    if (!work || !owner || !owner[0] || !callback) return ERR_NULL;
    memset(work, 0, sizeof(*work));
    work->priority = priority;
    work->callback = callback;
    work->context = context;
    work->initialized = 1U;
    return OK;
}

int schedule_work(work_struct_t* work) {
    if (!work || !work->initialized || !work->callback) return ERR_STATE;
    if (!execute_scheduled_work) return OK;
    return work->callback(work->context);
}

int schedule_delayed_work(work_struct_t* work, uint32_t delay_ticks) {
    (void)delay_ticks;
    return work && work->initialized ? OK : ERR_STATE;
}

int init_waitqueue_head(wait_queue_head_t* queue, const char* owner) {
    if (!queue || !owner || !owner[0]) return ERR_NULL;
    memset(queue, 0, sizeof(*queue));
    queue->initialized = 1U;
    queue->available = 1U;
    return OK;
}

int wait_event_timeout(wait_queue_head_t* queue, wait_condition_fn_t condition,
                       void* context, uint32_t timeout_ticks,
                       wait_reason_t* out_reason) {
    uint8_t ready = 0U;

    (void)timeout_ticks;
    if (!queue || !queue->initialized || !condition || !out_reason) {
        return ERR_NULL;
    }
    if (condition(context, &ready) != OK) return ERR_STATE;
    *out_reason = ready ? WAIT_REASON_EVENT : WAIT_REASON_TIMEOUT;
    return ready ? OK : ERR_TIMEOUT;
}

int wait_deadline_remaining(uint32_t deadline_tick, uint32_t* out_remaining) {
    uint32_t now;

    if (!out_remaining) return ERR_NULL;
    if (deadline_tick == WAIT_TIMEOUT_INFINITE) {
        *out_remaining = WAIT_TIMEOUT_INFINITE;
        return OK;
    }
    now = timer_get_ticks();
    if ((int32_t)(now - deadline_tick) >= 0) *out_remaining = 0U;
    else *out_remaining = deadline_tick - now;
    return OK;
}

int wake_up_all(wait_queue_head_t* queue, uint32_t* out_woken) {
    if (!queue || !out_woken) return ERR_NULL;
    *out_woken = queue->waiters;
    queue->waiters = 0U;
    return OK;
}

static void prepare_fixture(void) {
    memset(fake_disk, 0, sizeof(fake_disk));
    memset(&fake_ata, 0, sizeof(fake_ata));
    fake_ata.slot = 0U;
    fake_ata.sectors = HOST_FAKE_SECTOR_COUNT;
    fake_ata.present = 1;
    fake_ata.flush_supported = 1U;
    memcpy(fake_ata.model, "host-block", 11U);
    fake_ticks = 100U;
    fake_log_count = 0U;
    nested_device = 0;
    memset(nested_read_buffer, 0, sizeof(nested_read_buffer));
    nested_read_result = ERR_STATE;
    nested_read_active = 0U;
    execute_scheduled_work = 0U;
}

static int nested_read_backend(const char* device_id, uint32_t lba,
                               uint8_t count, uint8_t* buffer) {
    (void)device_id;
    if (!buffer || count != 1U) return ERR_INVALID;
    if (!nested_read_active) {
        nested_read_active = 1U;
        nested_read_result = block_cache_read(
            nested_device, lba, 1U, nested_read_buffer,
            sizeof(nested_read_buffer), nested_read_backend);
        nested_read_active = 0U;
    }
    memset(buffer, 0x5AU, BLOCK_CACHE_BLOCK_SIZE);
    return OK;
}

int main(void) {
    block_queue_stats_t block_stats;
    block_cache_stats_t cache_stats;
    block_durability_status_t durability;
    block_device_t device;
    bio_request_t asynchronous_request;
    uint32_t device_count;
    uint8_t buffer[BLOCK_SECTOR_SIZE];
    uint8_t partial_payload[2] = {0xA5U, 0x5AU};

    prepare_fixture();
    coverage_active = 1U;
    EXPECT(block_get_count(&device_count) == ERR_STATE);
    EXPECT(block_init() == OK);
    EXPECT(block_init() == OK);
    EXPECT(block_get_count(&device_count) == OK);
    EXPECT(device_count == 1U);
    EXPECT(block_get_at(0U, &device) == OK);
    EXPECT(device.ops.read != 0 && device.ops.write != 0);
    EXPECT(device.ops.read(device.ops.context, 1U, 1U, buffer) == OK);
    EXPECT(device.ops.write(device.ops.context, 1U, 1U, buffer) == OK);
    EXPECT(block_validate_state() == OK);
    EXPECT(block_cache_get_stats(&cache_stats) == OK);
    EXPECT(cache_stats.capacity == BLOCK_CACHE_CAPACITY);
    EXPECT(block_cache_get_durability_status(&durability) == OK);
    EXPECT(durability.state == BLOCK_DURABILITY_READY);
    EXPECT(block_get_stats(&block_stats) == OK);
    EXPECT(block_stats.queue_depth == 0U);
    EXPECT(block_read("ata0", 0U, 1U, buffer) == OK);
    EXPECT(block_write("ata0", 0U, 1U, buffer) == OK);
    memset(&asynchronous_request, 0, sizeof(asynchronous_request));
    asynchronous_request.device_id = "ata0";
    asynchronous_request.lba = 2U;
    asynchronous_request.sector_count = 1U;
    asynchronous_request.buffer = buffer;
    asynchronous_request.buffer_bytes = sizeof(buffer);
    asynchronous_request.operation = BLOCK_OPERATION_READ;
    execute_scheduled_work = 1U;
    EXPECT(block_submit(&asynchronous_request) == OK);
    execute_scheduled_work = 0U;
    EXPECT(asynchronous_request.state == BLOCK_REQUEST_COMPLETED);
    EXPECT(block_cache_write(&device, 3U, 1U, partial_payload,
                             sizeof(partial_payload)) == OK);
    EXPECT(block_cache_sync_device("ata0") == OK);
    nested_device = &device;
    EXPECT(block_cache_read(&device, 4U, 1U, buffer, sizeof(buffer),
                            nested_read_backend) == OK);
    EXPECT(nested_read_result == ERR_TIMEOUT);
    EXPECT(buffer[0] == 0x5AU && buffer[BLOCK_SECTOR_SIZE - 1U] == 0x5AU);
    EXPECT(block_cache_validate_state() == OK);
    EXPECT(block_self_test() == OK);
    EXPECT(block_validate_state() == OK);
    EXPECT(block_cache_validate_state() == OK);
    EXPECT(block_get_stats(&block_stats) == OK);
    EXPECT(block_stats.queue_depth == 0U);
    EXPECT(block_stats.in_flight == 0U);
    EXPECT(block_cache_get_stats(&cache_stats) == OK);
    EXPECT(cache_stats.entries == 0U);
    EXPECT(block_cache_get_durability_status(&durability) == OK);
    EXPECT(durability.devices_checked == 0U || durability.devices_checked == 1U);
    EXPECT(block_cache_read(0, 0U, 1U, buffer, sizeof(buffer), 0) == ERR_NULL);
    EXPECT(block_read(0, 0U, 1U, buffer) == ERR_NULL);
    EXPECT(block_write("missing", 0U, 1U, buffer) == ERR_NOT_FOUND);
    EXPECT(fake_log_count > 0U);
    coverage_active = 0U;
    coverage_emit(0);
    puts("block-host: PASS");
    return 0;
}
