#include <stdint.h>
#include <stdio.h>

#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_EXPECTED_PID_BASE 400U
#define HOST_MAX_CAPTURED_NAME 16U
#define HOST_ENTRY_NONZERO 0xFFFFFFFFU

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static int fake_loader_result = OK;
static int fake_foreground_active;
static uint32_t fake_run_calls;
static uint32_t fake_last_size;
static uint32_t fake_last_pid;
static const uint8_t* fake_last_image;
static const app_launch_info_t* fake_last_launch;
static char fake_last_name[HOST_MAX_CAPTURED_NAME];

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
    printf("ZCOV_BEGIN|case=host:core:app-builtin|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-builtin|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-builtin|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "app-builtin-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

static int text_equals(const char* left, const char* right) {
    uint32_t index = 0U;

    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        if (left[index] != right[index]) return 0;
        index++;
    }
    return left[index] == right[index];
}

static uint32_t text_length(const char* text) {
    uint32_t length = 0U;

    while (text && text[length]) length++;
    return length;
}

static void copy_name(char* destination, const char* source) {
    uint32_t index;

    for (index = 0U; index + 1U < HOST_MAX_CAPTURED_NAME && source[index];
         index++) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

int app_loader_is_foreground_active(void) {
    return fake_foreground_active;
}

int app_loader_run_image(const char* name, const uint8_t* image,
                         uint32_t size, const app_launch_info_t* launch,
                         uint32_t* pid_out) {
    fake_run_calls++;
    fake_last_size = size;
    fake_last_image = image;
    fake_last_launch = launch;
    copy_name(fake_last_name, name);
    if (fake_loader_result != OK) return fake_loader_result;
    if (!pid_out) return ERR_NULL;
    *pid_out = HOST_EXPECTED_PID_BASE + fake_run_calls;
    fake_last_pid = *pid_out;
    return OK;
}

uint32_t timer_get_ticks(void) {
    return 1U;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

int serial_is_ready(void) {
    return 1;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static void validate_image(const char* expected_name, uint32_t minimum_data,
                           uint32_t expected_launch,
                           const app_launch_info_t* launch) {
    const app_image_header_t* header;

    EXPECT(fake_last_image != 0);
    EXPECT(fake_last_size >= APP_IMAGE_HEADER_SIZE + minimum_data);
    EXPECT(fake_last_size <= APP_IMAGE_MAX_FILE_SIZE);
    EXPECT(text_equals(fake_last_name, expected_name));
    EXPECT(fake_last_launch == launch);
    header = (const app_image_header_t*)fake_last_image;
    EXPECT(header->magic[0] == 'Z' && header->magic[1] == 'A');
    EXPECT(header->magic[2] == 'P' && header->magic[3] == 'P');
    EXPECT(header->version == APP_IMAGE_VERSION);
    EXPECT(header->architecture == APP_IMAGE_ARCH_I386);
    EXPECT(header->header_size == APP_IMAGE_HEADER_SIZE);
    EXPECT(header->code_offset == APP_IMAGE_HEADER_SIZE);
    EXPECT(header->code_size > 0U);
    EXPECT(header->code_size <= APP_IMAGE_MAX_CODE_SIZE);
    EXPECT(header->data_offset == header->code_offset + header->code_size);
    EXPECT(header->data_size >= minimum_data);
    EXPECT(header->data_size <= APP_IMAGE_MAX_DATA_SIZE);
    if (expected_launch == HOST_ENTRY_NONZERO) {
        EXPECT(header->entry_offset > 0U);
    } else {
        EXPECT(header->entry_offset == expected_launch);
    }
    EXPECT(header->stack_size == APP_IMAGE_STACK_SIZE);
    EXPECT(header->flags == APP_IMAGE_FLAGS_NONE);
    EXPECT(fake_last_size == header->data_offset + header->data_size);
}

static void reset_capture(void) {
    fake_loader_result = OK;
    fake_foreground_active = 0;
    fake_run_calls = 0U;
    fake_last_size = 0U;
    fake_last_pid = 0U;
    fake_last_image = 0;
    fake_last_launch = 0;
    fake_last_name[0] = '\0';
}

static void test_echo_and_argtest(void) {
    app_launch_info_t launch = {0};
    uint32_t pid = 0U;

    launch.abi_version = APP_LAUNCH_ABI_VERSION;
    launch.argc = 2U;
    launch.raw_length = 7U;
    launch.raw_args[0] = 'a';
    EXPECT(app_builtin_run_echo(&launch, &pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 1U);
    validate_image("Echo", 1U, 0U, &launch);
    EXPECT(app_builtin_run_argtest(&launch, &pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 2U);
    validate_image("ArgTest", 0U, 0U, &launch);
    EXPECT(fake_run_calls == 2U);
}

static void test_query_apps(void) {
    uint32_t pid = 0U;

    EXPECT(app_builtin_run_uptime(&pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 1U);
    validate_image("Uptime", 128U, HOST_ENTRY_NONZERO, 0);
    EXPECT(app_builtin_run_mem(&pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 2U);
    validate_image("Mem", 128U, HOST_ENTRY_NONZERO, 0);
    EXPECT(app_builtin_run_pathtest(&pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 3U);
    validate_image("PathTest", 256U, 0U, 0);
    EXPECT(app_builtin_run_devtest(&pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 4U);
    validate_image("DevTest", 256U, 0U, 0);
    EXPECT(fake_run_calls == 4U);
}

static void test_output_and_loader_errors(void) {
    uint32_t pid = 0U;

    EXPECT(app_builtin_run_outputtest(0x42U, &pid) == OK);
    EXPECT(pid == HOST_EXPECTED_PID_BASE + 1U);
    validate_image("OutputTest", 128U, 0U, 0);
    EXPECT(app_builtin_run_outputtest(APP_EXIT_CANCELLED, &pid) == ERR_INVALID);
    EXPECT(fake_run_calls == 1U);
    fake_loader_result = ERR_UNAVAILABLE;
    EXPECT(app_builtin_run_outputtest(7U, &pid) == ERR_UNAVAILABLE);
    EXPECT(fake_run_calls == 2U);
    EXPECT(app_builtin_run_echo(0, &pid) == ERR_UNAVAILABLE);
    EXPECT(fake_run_calls == 3U);
}

static void test_preflight_and_null_outputs(void) {
    uint32_t pid = 0U;

    fake_foreground_active = 1;
    EXPECT(app_builtin_run_uptime(&pid) == ERR_STATE);
    EXPECT(app_builtin_run_mem(&pid) == ERR_STATE);
    EXPECT(app_builtin_run_pathtest(&pid) == ERR_STATE);
    EXPECT(app_builtin_run_devtest(&pid) == ERR_STATE);
    EXPECT(fake_run_calls == 0U);
    fake_foreground_active = 0;
    EXPECT(app_builtin_run_echo(0, 0) == ERR_NULL);
    EXPECT(app_builtin_run_argtest(0, 0) == ERR_NULL);
    EXPECT(app_builtin_run_uptime(0) == ERR_NULL);
    EXPECT(app_builtin_run_mem(0) == ERR_NULL);
    EXPECT(app_builtin_run_outputtest(1U, 0) == ERR_NULL);
    EXPECT(app_builtin_run_pathtest(0) == ERR_NULL);
    EXPECT(app_builtin_run_devtest(0) == ERR_NULL);
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    reset_capture();
    log_init();
    test_echo_and_argtest();
    reset_capture();
    test_query_apps();
    reset_capture();
    test_output_and_loader_errors();
    reset_capture();
    test_preflight_and_null_outputs();
    coverage_active = 0U;
    coverage_emit(result);
    printf("app_builtin host test OK\n");
    return result;
}
