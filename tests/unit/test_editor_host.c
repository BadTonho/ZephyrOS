#include <stdint.h>
#include <stdio.h>

#include "apps/editor.h"
#include "apps/editor_test.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/recovery.h"
#include "core/video.h"
#include "fs/fs.h"
#include "process/process.h"
#include "ui/desktop.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_ALLOCATION_CAPACITY 64U
#define HOST_FILE_BUFFER_SIZE 131072U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t allocation_pool[HOST_ALLOCATION_CAPACITY]
                             [EDITOR_MAX_LINE_LENGTH];
static uint8_t allocation_used[HOST_ALLOCATION_CAPACITY];
static uint8_t file_buffer[HOST_FILE_BUFFER_SIZE];
static uint8_t file_buffer_used;
static uint8_t host_fs_available;
static uint8_t host_editor_enabled;
static const uint8_t* host_file_data;
static uint32_t host_file_size;
static int host_write_result;
static uint32_t host_write_size;
static uint32_t host_ipc_index;

static const ipc_msg_t host_ipc_script[] = {
    {IPC_MSG_NONE, 0U, 0U},
    {IPC_MSG_KEYBOARD, 0x1EU, 0U},
    {IPC_MSG_KEYBOARD, 0x3CU, 0U},
    {IPC_MSG_KEYBOARD, 0x01U, 0U}
};

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
    printf("ZCOV_BEGIN|case=host:shell:editor|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:editor|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:editor|value=0x%08X\n",
           (uint32_t)result);
}

void* kmalloc(uint32_t size) {
    if (size == HOST_FILE_BUFFER_SIZE && !file_buffer_used) {
        file_buffer_used = 1U;
        return file_buffer;
    }
    if (size == 0U || size > EDITOR_MAX_LINE_LENGTH) return 0;
    for (uint32_t index = 0U; index < HOST_ALLOCATION_CAPACITY; index++) {
        if (!allocation_used[index]) {
            allocation_used[index] = 1U;
            return allocation_pool[index];
        }
    }
    return 0;
}

void kfree(void* pointer) {
    if (pointer == file_buffer) {
        file_buffer_used = 0U;
        return;
    }
    for (uint32_t index = 0U; index < HOST_ALLOCATION_CAPACITY; index++) {
        if (pointer == allocation_pool[index]) {
            allocation_used[index] = 0U;
            return;
        }
    }
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int recovery_is_available(recovery_component_id_t component) {
    (void)component;
    return host_fs_available ? 1 : 0;
}

int recovery_is_enabled(recovery_component_id_t component) {
    (void)component;
    return host_editor_enabled ? 1 : 0;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)component;
    (void)error_code;
    (void)message;
    return OK;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    (void)filename;
    if (host_file_size > max_size) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < host_file_size; index++) {
        buffer[index] = host_file_data[index];
    }
    return (int)host_file_size;
}

int fs_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    (void)filename;
    (void)data;
    host_write_size = size;
    return host_write_result;
}

void video_clear(void) {}
void video_print(const char* string, uint8_t color) {
    (void)string;
    (void)color;
}
void video_put_char_at(char character, uint8_t color, int x, int y) {
    (void)character;
    (void)color;
    (void)x;
    (void)y;
}
void video_print_at(int x, int y, const char* string, uint8_t color) {
    (void)x;
    (void)y;
    (void)string;
    (void)color;
}
void video_set_cursor(int x, int y) {
    (void)x;
    (void)y;
}

void desktop_set_active(int active) {
    (void)active;
}
void desktop_draw(void) {}

void process_yield(void) {}

int ipc_receive(ipc_msg_t* message) {
    ipc_msg_t current = host_ipc_script[host_ipc_index++];
    if (current.type == IPC_MSG_NONE) return 0;
    *message = current;
    return 1;
}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* reason) {
    (void)timeout_ticks;
    if (reason) *reason = WAIT_REASON_TIMEOUT;
    return ERR_TIMEOUT;
}

int main(void) {
    int result;

    static const uint8_t file_data[] = {
        'r', 'e', 't', 'u', 'r', 'n', ' ', '1', ';', '\r', '\n',
        'n', 'e', 'x', 't', '\n'
    };

    coverage_active = 1U;
    result = editor_host_test_contracts();
    host_fs_available = 0U;
    if (editor_open("missing.c") != ERR_UNAVAILABLE) result = ERR_STATE;
    host_fs_available = 1U;
    if (editor_open(0) != ERR_NULL) result = ERR_STATE;
    host_file_data = file_data;
    host_file_size = sizeof(file_data);
    if (editor_open("fixture.c") != OK) result = ERR_STATE;
    editor_close();

    host_editor_enabled = 1U;
    host_fs_available = 1U;
    host_file_data = file_data;
    host_file_size = sizeof(file_data);
    host_write_result = 1;
    host_write_size = 0U;
    host_ipc_index = 0U;
    editor_run();
    if (editor_is_running() || host_write_size != 1U) result = ERR_STATE;

    host_fs_available = 0U;
    host_ipc_index = 0U;
    editor_run_file("fixture.c");
    if (editor_is_running()) result = ERR_STATE;
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) {
        printf("editor-host: FAIL code=%d\n", result);
        return 1;
    }
    printf("editor-host: PASS\n");
    return 0;
}
