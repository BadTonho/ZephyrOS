#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/keyboard.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/update_remote_runtime.h"
#include "core/update_remote_system.h"
#include "core/update_system_slots.h"
#include "fs/fs.h"
#include "process/process.h"
#include "ui/updater_test.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static int fixture_cached_path_result;
static int fixture_slots_status_result;
static update_system_slots_status_t fixture_slots_status;
static char fixture_cached_path[FS_MAX_PATH];
static ipc_msg_t fixture_message;
static int fixture_message_available;
static char fixture_keyboard_ascii;

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
    printf("ZCOV_BEGIN|case=host:ui:updater|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:ui:updater|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:ui:updater|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void updater_host_fixture_set_message(int type, uint32_t data1) {
    fixture_message.type = (ipc_msg_type_t)type;
    fixture_message.data1 = data1;
    fixture_message.data2 = 0U;
    fixture_message_available = 1;
}

void updater_host_fixture_clear_message(void) {
    fixture_message_available = 0;
}

void updater_host_fixture_set_cached_path(int result, const char* path) {
    fixture_cached_path_result = result;
    kmemset(fixture_cached_path, 0, sizeof(fixture_cached_path));
    if (path) {
        uint32_t index = 0U;

        while (index + 1U < sizeof(fixture_cached_path) && path[index]) {
            fixture_cached_path[index] = path[index];
            index++;
        }
    }
}

void updater_host_fixture_set_slots(int result, uint8_t pending_slot,
                                    uint32_t sequence) {
    fixture_slots_status_result = result;
    kmemset(&fixture_slots_status, 0, sizeof(fixture_slots_status));
    fixture_slots_status.pending_slot = pending_slot;
    fixture_slots_status.sequence = sequence;
}

void updater_host_fixture_set_keyboard_ascii(char value) {
    fixture_keyboard_ascii = value;
}

int recovery_mark_disabled(recovery_component_id_t component,
                           int error_code, const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

void keyboard_process_events(void) {
}

char keyboard_scancode_to_ascii_shifted(uint8_t scancode, uint8_t shifted) {
    (void)scancode;
    (void)shifted;
    return fixture_keyboard_ascii;
}

int ipc_receive(ipc_msg_t* message) {
    if (!message || !fixture_message_available) return 0;
    *message = fixture_message;
    fixture_message_available = 0;
    return 1;
}

int update_remote_system_get_cached_path(char* path_out, uint32_t capacity) {
    if (fixture_cached_path_result == OK && path_out && capacity) {
        uint32_t index = 0U;

        while (index + 1U < capacity && fixture_cached_path[index]) {
            path_out[index] = fixture_cached_path[index];
            index++;
        }
        path_out[index] = '\0';
    }
    return fixture_cached_path_result;
}

int update_system_slots_get_status(update_system_slots_status_t* status_out) {
    if (fixture_slots_status_result == OK && status_out) {
        *status_out = fixture_slots_status;
    }
    return fixture_slots_status_result;
}

const char* update_action_reason_name(update_action_reason_t reason) {
    return reason == UPDATE_ACTION_NONE ? "NONE" : "ACTION";
}

const char* zupd_reason_name(zupd_reason_t reason) {
    return reason == ZUPD_REASON_NONE ? "NONE" : "VERIFY";
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = updater_host_test_contracts();
    coverage_active = 0U;
    if (result != 0) printf("UPDATER_HOST_FAIL:%d\n", result);
    coverage_emit(result);
    return result == 0 ? 0 : 1;
}
