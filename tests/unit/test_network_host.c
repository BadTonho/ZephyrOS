#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/net_buffer.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks = 100U;

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
    printf("ZCOV_BEGIN|case=host:core:net-buffer|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:net-buffer|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:net-buffer|value=0x%08X\n",
           (uint32_t)result);
}

static int stats_equal(const net_buffer_stats_t* left,
                       const net_buffer_stats_t* right) {
    if (!left || !right) return 0;
    return left->initialized == right->initialized &&
           left->active_buffers == right->active_buffers &&
           left->peak_buffers == right->peak_buffers &&
           left->allocations == right->allocations &&
           left->frees == right->frees &&
           left->delivered == right->delivered &&
           left->dropped == right->dropped &&
           left->copies == right->copies &&
           left->copied_bytes == right->copied_bytes &&
           left->clones == right->clones &&
           left->fragments == right->fragments &&
           left->invalid_transitions == right->invalid_transitions &&
           left->duplicate_completions == right->duplicate_completions &&
           left->ref_acquires == right->ref_acquires &&
           left->ref_releases == right->ref_releases &&
           left->last_error == right->last_error;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

int serial_is_ready(void) {
    return 1;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static int run_lifecycle(void) {
    net_buffer_t buffer = {0};

    if (net_buffer_begin(NULL, 128U, 0U, 1U,
                         NET_BUFFER_OWNER_ETHERNET) != ERR_NULL) return 10;
    if (net_buffer_begin(&buffer, 0U, 0U, 1U,
                         NET_BUFFER_OWNER_ETHERNET) != ERR_INVALID) return 11;
    if (net_buffer_begin(&buffer, 256U, 32U, 3U,
                         NET_BUFFER_OWNER_ETHERNET) != ERR_INVALID) return 12;
    if (net_buffer_begin(&buffer, 256U, 32U, 16U,
                         NET_BUFFER_OWNER_NONE) != ERR_INVALID) return 13;
    if (net_buffer_begin(&buffer, 256U, 257U, 16U,
                         NET_BUFFER_OWNER_ETHERNET) != ERR_INVALID) return 14;
    if (net_buffer_begin(&buffer, 256U, 32U, 16U,
                         NET_BUFFER_OWNER_ETHERNET) != OK) return 15;
    if (buffer.state != NET_BUFFER_STATE_ALLOCATED || buffer.refcount != 1U ||
        buffer.capacity != 256U || buffer.headroom != 32U ||
        buffer.length != 0U || buffer.tailroom != 224U ||
        buffer.alignment != 16U) return 16;
    if (net_buffer_set_length(&buffer, 225U) != ERR_OVERFLOW) return 17;
    if (net_buffer_set_layout(&buffer, 257U, 0U) != ERR_OVERFLOW) return 18;
    if (net_buffer_set_layout(&buffer, 32U, 64U) != OK) return 19;
    if (net_buffer_validate_state() != OK) return 30;
    if (net_buffer_transition(&buffer, NET_BUFFER_STATE_DELIVERED,
                              NET_BUFFER_OWNER_NONE) != ERR_INVALID) return 20;
    if (net_buffer_transition(&buffer, NET_BUFFER_STATE_RX,
                              NET_BUFFER_OWNER_ETHERNET) != OK) return 21;
    if (net_buffer_transition(&buffer, NET_BUFFER_STATE_QUEUED,
                              NET_BUFFER_OWNER_PROTOCOL) != OK) return 22;
    if (net_buffer_transition(&buffer, NET_BUFFER_STATE_IN_FLIGHT,
                              NET_BUFFER_OWNER_SOCKET) != OK) return 23;
    if (net_buffer_retain(&buffer) != OK || buffer.refcount != 2U) return 24;
    if (net_buffer_complete(&buffer, OK, NET_BUFFER_OWNER_SOCKET) != OK ||
        buffer.state != NET_BUFFER_STATE_DELIVERED) return 25;
    if (net_buffer_release(&buffer) != OK || buffer.refcount != 1U) return 26;
    if (net_buffer_release(&buffer) != OK ||
        buffer.state != NET_BUFFER_STATE_FREED || buffer.refcount != 0U) {
        return 27;
    }
    if (net_buffer_release(&buffer) != ERR_INVALID) return 28;
    if (net_buffer_set_length(&buffer, 1U) != ERR_INVALID) return 29;
    return 0;
}

static int run_drop_and_counters(void) {
    net_buffer_t buffer = {0};
    net_buffer_stats_t stats;

    if (net_buffer_begin(&buffer, 512U, 0U, 1U,
                         NET_BUFFER_OWNER_ETHERNET) != OK) return 40;
    if (net_buffer_complete(&buffer, ERR_TIMEOUT,
                            NET_BUFFER_OWNER_NONE) != OK ||
        buffer.state != NET_BUFFER_STATE_DROPPED ||
        buffer.completion_error != ERR_TIMEOUT) return 41;
    if (net_buffer_complete(&buffer, ERR_TIMEOUT,
                            NET_BUFFER_OWNER_NONE) != ERR_STATE) return 42;
    if (net_buffer_release(&buffer) != OK) return 43;
    if (net_buffer_note_copy(128U) != OK || net_buffer_note_clone() != OK ||
        net_buffer_note_fragment() != OK) return 44;
    if (net_buffer_get_stats(&stats) != OK || stats.copies == 0U ||
        stats.copied_bytes < 128U || stats.clones == 0U ||
        stats.fragments == 0U || stats.dropped == 0U ||
        stats.delivered == 0U || stats.invalid_transitions == 0U ||
        stats.duplicate_completions == 0U || stats.ref_acquires == 0U ||
        stats.ref_releases == 0U) return 45;
    if (net_buffer_set_length(NULL, 1U) != ERR_NULL ||
        net_buffer_set_layout(NULL, 0U, 0U) != ERR_NULL ||
        net_buffer_complete(NULL, OK, NET_BUFFER_OWNER_NONE) != ERR_NULL ||
        net_buffer_retain(NULL) != ERR_NULL ||
        net_buffer_release(NULL) != ERR_NULL) return 46;
    return 0;
}

static int run_capacity(void) {
    net_buffer_t buffers[NET_BUFFER_TRACKING_CAPACITY] = {{0}};
    net_buffer_t extra = {0};

    for (uint32_t index = 0U; index < NET_BUFFER_TRACKING_CAPACITY; index++) {
        if (net_buffer_begin(&buffers[index], 64U, 0U, 1U,
                             NET_BUFFER_OWNER_PROTOCOL) != OK) return 60;
    }
    if (net_buffer_begin(&extra, 64U, 0U, 1U,
                         NET_BUFFER_OWNER_PROTOCOL) != ERR_OVERFLOW) return 61;
    for (uint32_t index = 0U; index < NET_BUFFER_TRACKING_CAPACITY; index++) {
        if (net_buffer_complete(&buffers[index], ERR_CANCELLED,
                                NET_BUFFER_OWNER_NONE) != OK ||
            net_buffer_release(&buffers[index]) != OK) return 62;
    }
    return net_buffer_validate_state() == OK ? 0 : 63;
}

int main(void) {
    net_buffer_stats_t before_self_test;
    net_buffer_stats_t after_self_test;
    net_buffer_stats_t invalid_snapshot;
    int result = 0;

    coverage_active = 1U;
    if (net_buffer_get_stats(&before_self_test) != ERR_STATE) result = 1;
    if (!result && net_buffer_init() != OK) result = 2;
    if (!result && net_buffer_validate_state() != OK) result = 3;
    if (!result) result = run_lifecycle();
    if (!result) result = run_drop_and_counters();
    if (!result) result = run_capacity();
    if (!result && net_buffer_get_stats(&before_self_test) != OK) result = 4;
    if (!result && net_buffer_restore_stats(NULL) != ERR_NULL) result = 5;
    if (!result) {
        invalid_snapshot = before_self_test;
        invalid_snapshot.initialized = 0U;
        if (net_buffer_restore_stats(&invalid_snapshot) != ERR_STATE) result = 6;
    }
    if (!result && net_buffer_self_test() != OK) result = 7;
    if (!result && net_buffer_get_stats(&after_self_test) != OK) result = 8;
    if (!result && !stats_equal(&before_self_test, &after_self_test)) result = 9;
    if (!result && net_buffer_validate_state() != OK) result = 10;
    coverage_active = 0U;
    coverage_emit(result);
    if (result) printf("network-host: FAIL code=%d\n", result);
    else printf("network-host: PASS\n");
    return result;
}
