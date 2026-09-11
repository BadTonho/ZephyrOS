#include <stdint.h>
#include <stdio.h>

#include "recovery_menu.h"
#include "core/update_system_slots.h"

int recovery_menu_host_test_contracts(void);

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_KEY_CAPACITY 16U
#define EXPECTED_RECOVERY_F8_TICKS 183U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint16_t host_keys[HOST_KEY_CAPACITY];
static uint32_t host_key_count;
static uint32_t host_key_index;
static uint32_t host_last_timeout_ticks;

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
    printf("ZCOV_BEGIN|case=host:boot:recovery-menu|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:boot:recovery-menu|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:boot:recovery-menu|value=0x%08X\n",
           (uint32_t)result);
}

uint16_t recovery_bios_wait_key(uint32_t timeout_ticks) {
    host_last_timeout_ticks = timeout_ticks;
    if (host_key_index >= host_key_count) return 0U;
    return host_keys[host_key_index++];
}

static void set_keys(const uint16_t* keys, uint32_t count) {
    for (uint32_t index = 0U; index < count; index++) {
        host_keys[index] = keys[index];
    }
    host_key_count = count;
    host_key_index = 0U;
}

static recovery_menu_view_t view_fixture(void) {
    recovery_menu_view_t view;

    view.diagnostic = "DIAGNOSTIC";
    view.active = "ACTIVE";
    view.pending = "PENDING";
    view.previous = "PREVIOUS";
    view.attempt = "ATTEMPT";
    view.boot_state = "READY";
    view.reason = "FAILED";
    view.slot_a_state = "VALID";
    view.slot_b_state = "EMPTY";
    view.sequence = 3U;
    view.attempt_sequence = 4U;
    view.attempt_limit = UPDATE_SYSTEM_SLOTS_BOOT_ATTEMPT_LIMIT;
    view.slot_a_major = 1U;
    view.slot_a_minor = 2U;
    view.slot_a_patch = 3U;
    view.slot_b_major = 4U;
    view.slot_b_minor = 5U;
    view.slot_b_patch = 6U;
    view.slot_a_version_available = 1U;
    view.slot_b_version_available = 1U;
    view.failure_menu = 0U;
    view.allow_continue = 1U;
    view.allow_previous = 1U;
    view.allow_retry = 1U;
    return view;
}

static int test_public_contracts(void) {
    recovery_menu_view_t view = view_fixture();
    static const uint16_t f8[] = {0x4200U};
    static const uint16_t non_f8[] = {0x1C00U};
    static const uint16_t select_continue[] = {0x4800U, 0x5000U, 0x1C00U};
    static const uint16_t timeout[] = {0U};
    static const uint16_t escape[] = {0x0100U};
    static const uint16_t confirm[] = {0x1C00U};
    static const uint16_t cancel[] = {0x0100U};

    recovery_console_init(0);
    recovery_console_clear();
    recovery_console_print(0);
    recovery_console_print("HOST");
    recovery_console_print_u32(0U);
    recovery_console_print_u32(4294967295U);
    set_keys(f8, 1U);
    if (!recovery_menu_wait_f8()) return 1;
    if (host_last_timeout_ticks != EXPECTED_RECOVERY_F8_TICKS) return 11;
    set_keys(non_f8, 1U);
    if (recovery_menu_wait_f8()) return 2;
    if (host_last_timeout_ticks != EXPECTED_RECOVERY_F8_TICKS) return 12;
    set_keys(select_continue, 3U);
    if (recovery_menu_run(&view) != RECOVERY_MENU_ACTION_CONTINUE) return 3;
    view.failure_menu = 1U;
    set_keys(timeout, 1U);
    if (recovery_menu_run(&view) != RECOVERY_MENU_ACTION_PREVIOUS_DEFAULT) return 4;
    view.failure_menu = 0U;
    set_keys(escape, 1U);
    if (recovery_menu_run(&view) != RECOVERY_MENU_ACTION_CONTINUE) return 5;
    view.allow_continue = 0U;
    set_keys(escape, 1U);
    if (recovery_menu_run(&view) != RECOVERY_MENU_ACTION_PREVIOUS_DEFAULT) return 6;
    set_keys(confirm, 1U);
    if (!recovery_menu_confirm_retry(&view)) return 7;
    set_keys(cancel, 1U);
    if (recovery_menu_confirm_retry(&view)) return 8;
    if (recovery_menu_run(0) != RECOVERY_MENU_ACTION_LEGACY) return 9;
    if (recovery_menu_confirm_retry(0)) return 10;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = recovery_menu_host_test_contracts();
    if (result == 0) result = test_public_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != 0) {
        fprintf(stderr, "recovery-menu-host: FAIL %d\n", result);
        return result;
    }
    printf("recovery-menu-host: PASS\n");
    return 0;
}
