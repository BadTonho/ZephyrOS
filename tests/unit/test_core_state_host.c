#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/power_notifier.h"
#include "core/recovery.h"
#include "core/timer.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define NOTIFIER_COUNT 6U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks = 100U;
static uint32_t callback_calls;
static int callback_failure_index = -1;
static int callback_failure_code;

static void __attribute__((no_instrument_function))
coverage_record(void* function) {
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

static void __attribute__((no_instrument_function))
coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:state|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:state|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:state|value=0x%08X\n",
           (uint32_t)result);
}

static int notifier_callback(uint32_t deadline_tick) {
    uint32_t index = callback_calls++;

    (void)deadline_tick;
    if ((int)index == callback_failure_index) return callback_failure_code;
    return OK;
}

static int test_recovery_contract(void) {
    const recovery_component_t* component;

    if (recovery_get_count() != 0U) return 1;
    if (recovery_mark_ready((recovery_component_id_t)-1) != ERR_INVALID) {
        return 2;
    }
    if (recovery_mark_ready(RECOVERY_COMPONENT_VESA) != ERR_STATE) return 3;
    if (recovery_get(RECOVERY_COMPONENT_VESA) != NULL) return 4;
    if (recovery_is_available(RECOVERY_COMPONENT_VESA) != 0) return 5;
    if (recovery_init() != OK) return 6;
    if (recovery_get_count() != RECOVERY_COMPONENT_COUNT) return 7;
    if (recovery_get((recovery_component_id_t)-1) != NULL) return 8;
    component = recovery_get(RECOVERY_COMPONENT_VESA);
    if (!component || component->state != RECOVERY_STATE_UNKNOWN ||
        component->failures != 0U ||
        strcmp(recovery_state_name(component->state), "UNKNOWN") != 0) {
        return 9;
    }
    if (recovery_mark_ready(RECOVERY_COMPONENT_VESA) != OK ||
        !recovery_is_available(RECOVERY_COMPONENT_VESA) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) return 10;
    if (recovery_mark_degraded(RECOVERY_COMPONENT_VESA, ERR_UNAVAILABLE,
                               "sem hardware") != OK) return 11;
    component = recovery_get(RECOVERY_COMPONENT_VESA);
    if (!component || component->state != RECOVERY_STATE_DEGRADED ||
        component->failures != 1U || component->last_error != ERR_UNAVAILABLE ||
        recovery_is_available(RECOVERY_COMPONENT_VESA) != 0) return 12;
    if (!recovery_is_enabled(RECOVERY_COMPONENT_VESA)) return 13;
    if (recovery_mark_disabled(RECOVERY_COMPONENT_VESA, ERR_DISK, NULL) != OK) {
        return 14;
    }
    component = recovery_get(RECOVERY_COMPONENT_VESA);
    if (!component || component->state != RECOVERY_STATE_DISABLED ||
        component->failures != 2U || component->last_error != ERR_DISK ||
        recovery_is_enabled(RECOVERY_COMPONENT_VESA)) return 15;
    if (recovery_mark_ready(RECOVERY_COMPONENT_VESA) != OK ||
        recovery_state_name(RECOVERY_STATE_READY) == NULL ||
        recovery_state_name(RECOVERY_STATE_DEGRADED) == NULL ||
        recovery_state_name(RECOVERY_STATE_DISABLED) == NULL) return 16;
    if (recovery_mark_degraded((recovery_component_id_t)-1, ERR_STATE, NULL) !=
        ERR_INVALID) return 17;
    if (recovery_is_available((recovery_component_id_t)-1) != 0 ||
        recovery_is_enabled((recovery_component_id_t)-1) != 0) return 18;
    return 0;
}

static int test_power_notifier_contract(void) {
    static const char* names[NOTIFIER_COUNT] = {
        "processes", "workqueue", "vfs-storage", "audio", "network", "video"
    };
    uint32_t count = 0U;
    uint32_t completed = 0U;
    uint32_t optional_failures = 0U;
    char invalid_name[POWER_NOTIFIER_NAME_SIZE + 1U];

    memset(invalid_name, 'x', sizeof(invalid_name));
    invalid_name[sizeof(invalid_name) - 1U] = '\0';
    if (power_notifier_get_count(NULL) != ERR_NULL) return 20;
    if (power_notifier_get_count(&count) != ERR_STATE) return 21;
    if (power_notifier_notify(200U, &completed, &optional_failures) != ERR_STATE) {
        return 22;
    }
    if (power_notifier_init() != OK || power_notifier_get_count(&count) != OK ||
        count != 0U) return 23;
    if (power_notifier_register(NULL, notifier_callback, notifier_callback, 0U) !=
        ERR_NULL) return 24;
    if (power_notifier_register("invalid", NULL, notifier_callback, 0U) !=
        ERR_INVALID) return 25;
    if (power_notifier_register(invalid_name, notifier_callback,
                                notifier_callback, 0U) != ERR_INVALID) return 26;
    if (power_notifier_finalize() != ERR_STATE) return 27;
    for (uint32_t index = 0U; index < NOTIFIER_COUNT; index++) {
        if (power_notifier_register(names[index], notifier_callback,
                                    notifier_callback, index == 3U) != OK) {
            return 28;
        }
    }
    if (power_notifier_register("video", notifier_callback, notifier_callback, 0U) !=
        ERR_STATE) return 29;
    if (power_notifier_get_count(&count) != OK || count != NOTIFIER_COUNT ||
        power_notifier_validate_state() != ERR_STATE) return 30;
    if (power_notifier_finalize() != OK || power_notifier_validate_state() != OK) {
        return 31;
    }
    if (power_notifier_notify(1000U, NULL, &optional_failures) != ERR_NULL) {
        return 32;
    }
    callback_calls = 0U;
    callback_failure_index = -1;
    fake_ticks = 100U;
    if (power_notifier_notify(1000U, &completed, &optional_failures) != OK ||
        completed != NOTIFIER_COUNT || optional_failures != 0U ||
        callback_calls != NOTIFIER_COUNT) return 33;
    callback_calls = 0U;
    callback_failure_index = 3;
    callback_failure_code = ERR_UNAVAILABLE;
    if (power_notifier_quiesce(1000U, &completed, &optional_failures) != OK ||
        completed != NOTIFIER_COUNT - 1U || optional_failures != 1U) return 34;
    callback_calls = 0U;
    callback_failure_index = 0;
    callback_failure_code = ERR_DISK;
    if (power_notifier_notify(1000U, &completed, &optional_failures) != ERR_DISK ||
        callback_calls != 1U) return 35;
    callback_failure_index = -1;
    fake_ticks = 2000U;
    if (power_notifier_notify(2000U, &completed, &optional_failures) != ERR_TIMEOUT) {
        return 36;
    }
    return 0;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
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

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_recovery_contract();
    if (!result) result = test_power_notifier_contract();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
