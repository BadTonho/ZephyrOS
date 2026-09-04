#include <stdint.h>
#include <stdio.h>

#include "core/app_files.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/syscall.h"
#include "fs/fs.h"
#include "memory/paging.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_PID 71U
#define HOST_FILE_SIZE 51U
#define HOST_PROCESS_NAME "loader-fixture"

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static uint8_t fake_image[APP_IMAGE_MAX_FILE_SIZE + 1U];
static uint32_t fake_image_size;
static int fake_paging_ready;
static int fake_syscall_ready;
static uint8_t fake_fs_type;
static int fake_alloc_fail;
static int fake_read_result;
static int fake_create_result;
static int fake_start_result;
static int fake_focus_result;
static int fake_cancel_result;
static int fake_close_result;
static int fake_reap_result;
static process_t fake_process;
static int fake_process_present;
static uint32_t fake_destroy_calls;
static uint32_t fake_close_calls;
static uint32_t fake_cancel_calls;
static uint32_t fake_create_calls;
static uint32_t fake_start_calls;
static uint32_t fake_focus_calls;
static uint32_t fake_reap_calls;
static const uint8_t* fake_code;
static const uint8_t* fake_data;
static uint32_t fake_code_size;
static uint32_t fake_data_size;
static uint32_t fake_created_pid;
static uint32_t fake_cancel_exit_code;
static app_launch_info_t fake_launch;

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
    printf("ZCOV_BEGIN|case=host:core:app-loader|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-loader|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-loader|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "app-loader-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static void zero_bytes(void* address, uint32_t size) {
    uint8_t* bytes = (uint8_t*)address;

    for (uint32_t index = 0U; index < size; index++) bytes[index] = 0U;
}

static void copy_bytes(void* destination, const void* source, uint32_t size) {
    uint8_t* dst = (uint8_t*)destination;
    const uint8_t* src = (const uint8_t*)source;

    for (uint32_t index = 0U; index < size; index++) dst[index] = src[index];
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int paging_is_ready(void) {
    return fake_paging_ready;
}

int syscall_user_mode_is_enabled(void) {
    return fake_syscall_ready;
}

uint8_t fs_get_type(void) {
    return fake_fs_type;
}

void* kmalloc(uint32_t size) {
    if (fake_alloc_fail || size > sizeof(fake_image)) return 0;
    return fake_image;
}

void kfree(void* pointer) {
    EXPECT(pointer == fake_image);
}

int app_files_close_owner(uint32_t pid) {
    EXPECT(pid != 0U);
    fake_close_calls++;
    return fake_close_result;
}

int fs_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    EXPECT(path != 0);
    EXPECT(buffer != 0);
    if (fake_read_result < 0) return fake_read_result;
    if (fake_image_size > max_size) return (int)fake_image_size;
    copy_bytes(buffer, fake_image, fake_image_size);
    return (int)fake_image_size;
}

int process_create_user_image_suspended_with_launch(
    const char* name, const uint8_t* code, uint32_t code_size,
    const uint8_t* data, uint32_t data_size, uint32_t entry_offset,
    uint32_t stack_size, const app_launch_info_t* launch,
    uint32_t* pid_out) {
    (void)entry_offset;
    (void)stack_size;
    fake_create_calls++;
    if (fake_create_result != OK) return fake_create_result;
    EXPECT(name != 0);
    EXPECT(code != 0);
    EXPECT(data != 0 || data_size == 0U);
    EXPECT(launch != 0);
    EXPECT(pid_out != 0);
    fake_code = code;
    fake_data = data;
    fake_code_size = code_size;
    fake_data_size = data_size;
    fake_created_pid = HOST_PID;
    copy_bytes(&fake_launch, launch, sizeof(fake_launch));
    fake_process_present = 1;
    zero_bytes(&fake_process, sizeof(fake_process));
    fake_process.pid = HOST_PID;
    fake_process.state = PROCESS_STATE_READY;
    *pid_out = HOST_PID;
    return OK;
}

int process_start_user(uint32_t pid) {
    fake_start_calls++;
    EXPECT(pid == HOST_PID);
    if (fake_start_result != OK) return fake_start_result;
    fake_process.state = PROCESS_STATE_READY;
    return OK;
}

int process_set_focus(uint32_t pid) {
    fake_focus_calls++;
    EXPECT(pid == HOST_PID);
    return fake_focus_result;
}

int process_cancel_user(uint32_t pid, uint32_t exit_code) {
    fake_cancel_calls++;
    fake_cancel_exit_code = exit_code;
    EXPECT(pid == HOST_PID);
    if (fake_cancel_result == OK) {
        fake_process.state = PROCESS_STATE_ZOMBIE;
        fake_process.exit_code = exit_code;
    }
    return fake_cancel_result;
}

void process_destroy(process_t* process) {
    EXPECT(process == &fake_process);
    fake_destroy_calls++;
    fake_process_present = 0;
}

process_t* process_get_by_pid(uint32_t pid) {
    if (fake_process_present && pid == HOST_PID) return &fake_process;
    return 0;
}

int process_reap_finished_user(void) {
    fake_reap_calls++;
    return fake_reap_result;
}

static void reset_fixture(void) {
    fake_paging_ready = 1;
    fake_syscall_ready = 1;
    fake_fs_type = FS_TYPE_FAT12;
    fake_alloc_fail = 0;
    fake_read_result = 0;
    fake_create_result = OK;
    fake_start_result = OK;
    fake_focus_result = OK;
    fake_cancel_result = OK;
    fake_close_result = OK;
    fake_reap_result = OK;
    zero_bytes(&fake_process, sizeof(fake_process));
    fake_process_present = 0;
    fake_destroy_calls = 0U;
    fake_close_calls = 0U;
    fake_cancel_calls = 0U;
    fake_create_calls = 0U;
    fake_start_calls = 0U;
    fake_focus_calls = 0U;
    fake_reap_calls = 0U;
    fake_code = 0;
    fake_data = 0;
    fake_code_size = 0U;
    fake_data_size = 0U;
    fake_created_pid = 0U;
    fake_cancel_exit_code = 0U;
    zero_bytes(&fake_launch, sizeof(fake_launch));
    zero_bytes(fake_image, sizeof(fake_image));
    fake_image_size = HOST_FILE_SIZE;
}

static void make_valid_image(void) {
    app_image_header_t header;

    zero_bytes(&header, sizeof(header));
    header.magic[0] = 'Z';
    header.magic[1] = 'A';
    header.magic[2] = 'P';
    header.magic[3] = 'P';
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = 4U;
    header.data_offset = APP_IMAGE_HEADER_SIZE + header.code_size;
    header.data_size = 3U;
    header.entry_offset = 0U;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;
    copy_bytes(fake_image, &header, sizeof(header));
    fake_image[header.code_offset] = 0x90U;
    fake_image[header.code_offset + 1U] = 0xC3U;
    fake_image[header.data_offset] = 0x11U;
    fake_image[header.data_offset + 1U] = 0x22U;
    fake_image[header.data_offset + 2U] = 0x33U;
    fake_image_size = header.data_offset + header.data_size;
}

static void expect_result_available(app_loader_result_t* result,
                                    uint32_t exit_code) {
    EXPECT(result != 0);
    EXPECT(app_loader_take_finished_result(result) == OK);
    EXPECT(result->pid == HOST_PID);
    EXPECT(result->exit_code == exit_code);
    EXPECT(app_loader_take_finished_result(result) == ERR_NOT_FOUND);
}

static void test_launch_parser(void) {
    app_launch_info_t info;
    app_launch_info_t invalid;
    char long_text[APP_LAUNCH_MAX_TEXT + 1U];

    EXPECT(app_loader_build_launch_info(0, &info) == ERR_NULL);
    EXPECT(app_loader_build_launch_info("one two\tthree", 0) == ERR_NULL);
    EXPECT(app_loader_build_launch_info("one two\tthree", &info) == OK);
    EXPECT(info.argc == 3U);
    EXPECT(info.args[0].offset == 0U && info.args[0].length == 3U);
    EXPECT(info.args[1].offset == 4U && info.args[1].length == 3U);
    EXPECT(info.args[2].offset == 8U && info.args[2].length == 5U);
    EXPECT(app_loader_build_launch_info(" \t  ", &info) == OK);
    EXPECT(info.argc == 0U);

    for (uint32_t index = 0U; index < APP_LAUNCH_MAX_TEXT; index++) {
        long_text[index] = 'x';
    }
    long_text[APP_LAUNCH_MAX_TEXT] = '\0';
    EXPECT(app_loader_build_launch_info(long_text, &info) == ERR_OVERFLOW);
    long_text[0] = 0x01;
    long_text[APP_LAUNCH_MAX_RAW_LENGTH] = '\0';
    EXPECT(app_loader_build_launch_info(long_text, &info) == ERR_INVALID);
    EXPECT(app_loader_build_launch_info(
               "a b c d e f g h i", &info) == ERR_OVERFLOW);

    EXPECT(app_loader_build_launch_info("one two", &info) == OK);
    invalid = info;
    invalid.abi_version = 99U;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_UNAVAILABLE);
    reset_fixture();
    make_valid_image();
    EXPECT(app_loader_init() == OK);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.argc = APP_LAUNCH_MAX_ARGS + 1U;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.raw_length = APP_LAUNCH_MAX_RAW_LENGTH + 1U;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.raw_args[invalid.raw_length] = 'x';
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.args[0].length = 0U;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.args[0].offset = 1U;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.args[0].length = invalid.raw_length + 1U;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.raw_args[0] = ' ';
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
    invalid = info;
    invalid.args[1].offset = invalid.args[0].offset + invalid.args[0].length;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &invalid, 0) == ERR_INVALID);
}

static void test_image_validation(void) {
    app_image_header_t header;
    uint32_t original_size;

    reset_fixture();
    make_valid_image();
    EXPECT(app_loader_validate_image(0, fake_image_size, &header) == ERR_NULL);
    EXPECT(app_loader_validate_image(fake_image, fake_image_size, 0) == ERR_NULL);
    EXPECT(app_loader_validate_image(fake_image, APP_IMAGE_HEADER_SIZE - 1U,
                                     &header) == ERR_INVALID);
    EXPECT(app_loader_validate_image(fake_image, APP_IMAGE_MAX_FILE_SIZE + 1U,
                                     &header) == ERR_OVERFLOW);
    original_size = fake_image_size;

    ((app_image_header_t*)fake_image)->magic[0] = 'X';
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->version++;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->architecture++;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);

    make_valid_image();
    ((app_image_header_t*)fake_image)->header_size++;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->code_size = 0U;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->code_size = APP_IMAGE_MAX_CODE_SIZE + 1U;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_OVERFLOW);
    make_valid_image();
    ((app_image_header_t*)fake_image)->data_size = APP_IMAGE_MAX_DATA_SIZE + 1U;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_OVERFLOW);
    make_valid_image();
    ((app_image_header_t*)fake_image)->stack_size++;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->flags = 1U;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->entry_offset = 4U;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    ((app_image_header_t*)fake_image)->data_offset++;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
    make_valid_image();
    EXPECT(app_loader_validate_image(fake_image, original_size - 1U, &header) == ERR_INVALID);
    make_valid_image();
    EXPECT(app_loader_validate_image(fake_image, original_size + 1U, &header) == ERR_OVERFLOW);
    make_valid_image();
    ((app_image_header_t*)fake_image)->code_offset++;
    EXPECT(app_loader_validate_image(fake_image, original_size, &header) == ERR_INVALID);
}

static void test_init_and_reap(void) {
    app_launch_info_t launch;
    app_loader_result_t result;
    uint32_t pid = 0U;

    reset_fixture();
    make_valid_image();
    fake_paging_ready = 0;
    EXPECT(app_loader_init() == ERR_UNAVAILABLE);
    EXPECT(app_loader_is_ready() == 0);
    fake_paging_ready = 1;
    fake_syscall_ready = 0;
    EXPECT(app_loader_init() == ERR_UNAVAILABLE);
    fake_syscall_ready = 1;
    fake_fs_type = FS_TYPE_NONE;
    EXPECT(app_loader_init() == ERR_UNAVAILABLE);
    fake_fs_type = FS_TYPE_FAT12;
    EXPECT(app_loader_init() == OK);
    EXPECT(app_loader_is_ready() == 1);
    EXPECT(app_loader_is_foreground_active() == 0);
    EXPECT(app_loader_get_foreground_pid() == 0U);
    EXPECT(app_loader_run_image(0, fake_image, fake_image_size, 0, &pid) == ERR_NULL);
    EXPECT(app_loader_run_image("", fake_image, fake_image_size, 0, &pid) == ERR_INVALID);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, 0, fake_image_size, 0, &pid) == ERR_NULL);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, APP_IMAGE_HEADER_SIZE - 1U,
                                0, &pid) == ERR_INVALID);

    EXPECT(app_loader_build_launch_info("alpha beta", &launch) == OK);
    app_loader_set_operation_generation(44U);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                &launch, &pid) == OK);
    EXPECT(pid == HOST_PID);
    EXPECT(fake_code_size == 4U && fake_data_size == 3U);
    EXPECT(fake_code == fake_image + APP_IMAGE_HEADER_SIZE);
    EXPECT(fake_data == fake_image + APP_IMAGE_HEADER_SIZE + 4U);
    EXPECT(app_loader_is_foreground_active() == 1);
    EXPECT(app_loader_get_foreground_pid() == HOST_PID);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                0, &pid) == ERR_STATE);
    EXPECT(app_loader_reap_finished() == OK);
    EXPECT(fake_start_calls == 1U && fake_focus_calls == 1U);
    fake_process.exit_code = 23U;
    fake_process.faulted = 1U;
    fake_process.termination_signal = APP_SIGNAL_TERM;
    fake_process.state = PROCESS_STATE_ZOMBIE;
    EXPECT(app_loader_reap_finished() == OK);
    EXPECT(fake_reap_calls == 2U);
    EXPECT(app_loader_is_foreground_active() == 1);
    EXPECT(app_loader_get_foreground_pid() == 0U);
    EXPECT(app_loader_take_finished_result(&result) == OK);
    EXPECT(result.pid == HOST_PID && result.exit_code == 23U);
    EXPECT(result.faulted == 1U && result.cancelled == 0U);
    EXPECT(result.focus_acquired == 1U && result.generation == 44U);
    EXPECT(result.termination_signal == APP_SIGNAL_TERM);
    EXPECT(app_loader_take_finished_result(&result) == ERR_NOT_FOUND);
    EXPECT(app_loader_take_finished_result(0) == ERR_NULL);
}

static void test_start_and_missing_failures(void) {
    app_loader_result_t result;
    uint32_t pid = 0U;

    reset_fixture();
    make_valid_image();
    EXPECT(app_loader_init() == OK);
    app_loader_set_operation_generation(8U);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                0, &pid) == OK);
    fake_start_result = ERR_UNAVAILABLE;
    EXPECT(app_loader_reap_finished() == ERR_UNAVAILABLE);
    EXPECT(fake_cancel_calls == 1U && fake_destroy_calls == 1U);
    expect_result_available(&result, (uint32_t)ERR_UNAVAILABLE);
    EXPECT(result.start_failed == 1U && result.focus_acquired == 0U);

    reset_fixture();
    make_valid_image();
    EXPECT(app_loader_init() == OK);
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                0, &pid) == OK);
    EXPECT(app_loader_reap_finished() == OK);
    fake_process_present = 0;
    EXPECT(app_loader_reap_finished() == ERR_STATE);
    expect_result_available(&result, ERR_STATE);
    EXPECT(result.faulted == 1U && result.start_failed == 1U);

    reset_fixture();
    make_valid_image();
    EXPECT(app_loader_init() == OK);
    fake_create_result = ERR_MEM;
    EXPECT(app_loader_run_image(HOST_PROCESS_NAME, fake_image, fake_image_size,
                                0, &pid) == ERR_MEM);
}

static void test_file_and_cancel(void) {
    app_launch_info_t invalid;
    app_loader_result_t result;
    char long_path[FS_MAX_PATH + 1U];
    uint32_t pid = 0U;

    reset_fixture();
    make_valid_image();
    EXPECT(app_loader_init() == OK);
    EXPECT(app_loader_run_file(0, &pid) == ERR_NULL);
    EXPECT(app_loader_run_file("", &pid) == ERR_INVALID);
    EXPECT(app_loader_run_file("/apps/demo.bin", &pid) == ERR_INVALID);
    for (uint32_t index = 0U; index < FS_MAX_PATH; index++) long_path[index] = 'x';
    long_path[FS_MAX_PATH] = '\0';
    EXPECT(app_loader_run_file(long_path, &pid) == ERR_OVERFLOW);
    fake_read_result = -1;
    EXPECT(app_loader_run_file("/apps/demo.ZAP", &pid) == ERR_NOT_FOUND);
    fake_fs_type = FS_TYPE_NONE;
    EXPECT(app_loader_run_file("/apps/demo.ZAP", &pid) == ERR_UNAVAILABLE);
    fake_fs_type = FS_TYPE_FAT12;
    fake_read_result = 0;
    fake_alloc_fail = 1;
    EXPECT(app_loader_run_file("/apps/demo.zap", &pid) == ERR_MEM);
    fake_alloc_fail = 0;
    fake_image_size = APP_IMAGE_MAX_FILE_SIZE + 1U;
    EXPECT(app_loader_run_file("/apps/demo.ZaP", &pid) == ERR_OVERFLOW);
    make_valid_image();
    EXPECT(app_loader_build_launch_info("ok", &invalid) == OK);
    invalid.args[0].length = 0U;
    EXPECT(app_loader_run_file_with_launch("/apps/demo.ZAP", &invalid, &pid) == ERR_INVALID);
    EXPECT(app_loader_run_file("/apps/demo.zap", &pid) == OK);
    EXPECT(app_loader_get_foreground_pid() == HOST_PID);
    EXPECT(app_loader_cancel_foreground(77U) == OK);
    EXPECT(fake_cancel_exit_code == 77U);
    EXPECT(fake_close_calls == 1U && fake_destroy_calls == 1U);
    EXPECT(app_loader_take_finished_result(&result) == OK);
    EXPECT(result.exit_code == 77U && result.cancelled == 0U);
    EXPECT(app_loader_cancel_foreground(77U) == ERR_NOT_FOUND);

    EXPECT(app_loader_run_file("/apps/demo.ZAP", &pid) == OK);
    EXPECT(app_loader_reap_finished() == OK);
    fake_process.state = PROCESS_STATE_ZOMBIE;
    EXPECT(app_loader_cancel_foreground(88U) == OK);
    EXPECT(app_loader_reap_finished() == OK);
    EXPECT(app_loader_take_finished_result(&result) == OK);
    EXPECT(result.exit_code == 0U);

    EXPECT(app_loader_cancel_foreground(0U) == ERR_NOT_FOUND);
}

int main(void) {
    int result = OK;

    coverage_active = 1U;
    test_launch_parser();
    test_image_validation();
    test_init_and_reap();
    test_start_and_missing_failures();
    test_file_and_cancel();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
