#include <stdint.h>
#include <stdio.h>

#include "apps/shell_pipeline.h"
#include "apps/shell.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "fs/vfs.h"
#include "process/thread.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_PIPE_CAPACITY 70000U
#define HOST_PIPE_COUNT VFS_MAX_PIPES
#define HOST_FD_BASE 3
#define HOST_OUTPUT_CAPACITY 2048U
#define HOST_BIG_OUTPUT_SIZE (VFS_REDIRECT_MAX_SIZE + 1U)
#define HOST_THREAD_CAPACITY 5U

typedef struct {
    uint8_t data[HOST_PIPE_CAPACITY];
    uint32_t size;
    uint32_t read_offset;
    uint8_t read_open;
    uint8_t write_open;
} host_pipe_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_pipe_t host_pipes[HOST_PIPE_COUNT];
static uint32_t host_pipe_count;
static uint32_t host_pipe_fail_after;
static int host_pipe_failure;
static int host_read_result;
static int host_write_result;
static uint8_t host_write_zero;
static int host_redirect_result;
static int host_validate_result;
static int host_wait_validate_result;
static uint32_t host_thread_count;
static uint32_t host_thread_fail_after;
static thread_t host_threads[HOST_THREAD_CAPACITY];
static uint8_t host_thread_executed[HOST_THREAD_CAPACITY];
static thread_t* host_current_thread;
static char host_output[HOST_OUTPUT_CAPACITY];
static uint32_t host_output_length;
static char host_redirect_path[VFS_MAX_PATH];
static uint32_t host_redirect_size;
static uint8_t host_redirect_append;
static uint32_t host_redirect_calls;
static uint32_t host_dispatch_calls;
static int host_dispatch_mode;
static int host_dispatch_result;
static char host_big_output[HOST_BIG_OUTPUT_SIZE + 1U];

enum {
    HOST_DISPATCH_NORMAL = 0,
    HOST_DISPATCH_BIG,
    HOST_DISPATCH_NESTED,
    HOST_DISPATCH_SELF_TEST,
    HOST_DISPATCH_READ_FAILURE
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
    printf("ZCOV_BEGIN|case=host:shell:pipeline|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:pipeline|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:pipeline|value=0x%08X\n",
           (uint32_t)result);
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;

    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;

    for (uint32_t index = 0U; index < size; index++) output[index] = input[index];
}

int kstrcmp(const char* first, const char* second) {
    if (!first || !second) return first == second ? 0 : 1;
    while (*first && *first == *second) {
        first++;
        second++;
    }
    return (uint8_t)*first - (uint8_t)*second;
}

uint32_t kstrlen(const char* value) {
    uint32_t length = 0U;

    if (!value) return 0U;
    while (value[length]) length++;
    return length;
}

static void output_append(const char* text) {
    if (!text) return;
    while (*text && host_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        host_output[host_output_length++] = *text++;
    }
    host_output[host_output_length] = '\0';
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    output_append(text);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

static int host_fd_pipe(int32_t fd) {
    int32_t index = fd - HOST_FD_BASE;

    if (index < 0 || (uint32_t)index >= host_pipe_count * 2U) return -1;
    return (int)(index / 2);
}

static uint8_t host_fd_is_write(int32_t fd) {
    return (uint8_t)(((fd - HOST_FD_BASE) & 1) != 0);
}

int vfs_pipe(int32_t fds[2]) {
    uint32_t index;

    if (!fds) return ERR_NULL;
    if (host_pipe_failure || host_pipe_count >= HOST_PIPE_COUNT) {
        return ERR_UNAVAILABLE;
    }
    if (host_pipe_fail_after && host_pipe_count >= host_pipe_fail_after) {
        return ERR_DISK;
    }
    index = host_pipe_count++;
    kmemset(&host_pipes[index], 0, sizeof(host_pipes[index]));
    host_pipes[index].read_open = 1U;
    host_pipes[index].write_open = 1U;
    fds[0] = HOST_FD_BASE + (int32_t)(index * 2U);
    fds[1] = fds[0] + 1;
    return OK;
}

int vfs_close(int32_t fd) {
    int pipe_index = host_fd_pipe(fd);

    if (pipe_index < 0) return ERR_INVALID;
    if (host_fd_is_write(fd)) {
        if (!host_pipes[pipe_index].write_open) return ERR_INVALID;
        host_pipes[pipe_index].write_open = 0U;
    } else {
        if (!host_pipes[pipe_index].read_open) return ERR_INVALID;
        host_pipes[pipe_index].read_open = 0U;
    }
    return OK;
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    int pipe_index = host_fd_pipe(fd);
    host_pipe_t* pipe;

    if (!bytes_written || (size > 0U && !buffer)) return ERR_NULL;
    *bytes_written = 0U;
    if (pipe_index < 0 || !host_fd_is_write(fd)) return ERR_INVALID;
    if (host_write_result != OK) return host_write_result;
    if (!host_pipes[pipe_index].write_open) return ERR_STATE;
    pipe = &host_pipes[pipe_index];
    if (host_write_zero) return OK;
    if (size > HOST_PIPE_CAPACITY - pipe->size) return ERR_OVERFLOW;
    kmemcpy(pipe->data + pipe->size, buffer, size);
    pipe->size += size;
    *bytes_written = size;
    return OK;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    int pipe_index = host_fd_pipe(fd);
    host_pipe_t* pipe;
    uint32_t available;
    uint32_t count;

    if (!bytes_read || (size > 0U && !buffer)) return ERR_NULL;
    *bytes_read = 0U;
    if (pipe_index < 0 || host_fd_is_write(fd)) return ERR_INVALID;
    if (host_read_result != OK) return host_read_result;
    if (!host_pipes[pipe_index].read_open) return ERR_STATE;
    pipe = &host_pipes[pipe_index];
    if (pipe->read_offset >= pipe->size) return OK;
    available = pipe->size - pipe->read_offset;
    count = available < size ? available : size;
    kmemcpy(buffer, pipe->data + pipe->read_offset, count);
    pipe->read_offset += count;
    *bytes_read = count;
    return OK;
}

int vfs_write_redirect(const char* path, const uint8_t* data, uint32_t size,
                       uint8_t append) {
    if (!path || !data) return ERR_NULL;
    host_redirect_calls++;
    if (host_redirect_result != OK) return host_redirect_result;
    if (size > VFS_REDIRECT_MAX_SIZE) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index + 1U < VFS_MAX_PATH && path[index]; index++) {
        host_redirect_path[index] = path[index];
        host_redirect_path[index + 1U] = '\0';
    }
    host_redirect_size = size;
    host_redirect_append = append;
    if (size && data[0] == 0U) return ERR_STATE;
    return OK;
}

int vfs_validate_state(void) {
    return host_validate_result;
}

int wait_validate_state(void) {
    return host_wait_validate_result;
}

thread_t* thread_get_current(void) {
    return host_current_thread;
}

thread_t* thread_create(const char* name, void (*entry)(void)) {
    thread_t* thread;

    if (!name || !entry || host_thread_count >= HOST_THREAD_CAPACITY) return 0;
    if (host_thread_fail_after && host_thread_count >= host_thread_fail_after) {
        return 0;
    }
    thread = &host_threads[host_thread_count];
    kmemset(thread, 0, sizeof(*thread));
    thread->state = THREAD_RUNNING;
    thread->entry = entry;
    thread->id = host_thread_count + 1U;
    host_thread_count++;
    return thread;
}

void thread_destroy(thread_t* thread) {
    if (thread) thread->state = THREAD_UNUSED;
}

void thread_yield(void) {
    for (uint32_t index = 0U; index < host_thread_count; index++) {
        if (host_threads[index].state == THREAD_RUNNING &&
            !host_thread_executed[index]) {
            host_thread_executed[index] = 1U;
            host_current_thread = &host_threads[index];
            host_threads[index].entry();
            host_current_thread = 0;
            host_threads[index].state = THREAD_FINISHED;
            return;
        }
    }
}

static int host_starts_with(const char* text, const char* prefix) {
    uint32_t index = 0U;

    while (prefix[index]) {
        if (text[index] != prefix[index]) return 0;
        index++;
    }
    return text[index] == '\0' || text[index] == ' ' || text[index] == '\t';
}

int shell_dispatch_execute(const char* input) {
    char buffer[HOST_BIG_OUTPUT_SIZE + 1U];
    uint32_t bytes_read;
    int result;

    host_dispatch_calls++;
    if (host_dispatch_mode == HOST_DISPATCH_NESTED) {
        uint8_t handled = 0U;

        result = shell_pipeline_try_execute("echo|echo", &handled);
        return result == ERR_STATE && handled ? OK : ERR_STATE;
    }
    if (host_dispatch_mode == HOST_DISPATCH_SELF_TEST) {
        return shell_pipeline_self_test() == ERR_STATE ? OK : ERR_STATE;
    }
    if (host_dispatch_mode == HOST_DISPATCH_READ_FAILURE) {
        if (host_starts_with(input, "echo")) {
            return shell_pipeline_write("read-source\n", 0x07);
        }
        result = shell_pipeline_read(buffer, 1U, &bytes_read);
        return result == ERR_DISK ? OK : ERR_STATE;
    }
    if (host_dispatch_result != OK) return host_dispatch_result;
    if (host_dispatch_mode == HOST_DISPATCH_BIG ||
        host_starts_with(input, "echo")) {
        return shell_pipeline_write(
            host_dispatch_mode == HOST_DISPATCH_BIG ? host_big_output :
                                                       "pipeline-data\n",
            0x07);
    }
    if (host_starts_with(input, "grep")) {
        bytes_read = 0U;
        result = shell_pipeline_read(buffer, sizeof(buffer) - 1U, &bytes_read);
        if (result != OK) return result;
        buffer[bytes_read] = '\0';
        return shell_pipeline_write(buffer, 0x07);
    }
    return OK;
}

static void host_reset(void) {
    kmemset(host_pipes, 0, sizeof(host_pipes));
    kmemset(host_threads, 0, sizeof(host_threads));
    kmemset(host_thread_executed, 0, sizeof(host_thread_executed));
    host_pipe_count = 0U;
    host_pipe_fail_after = 0U;
    host_pipe_failure = 0;
    host_read_result = OK;
    host_write_result = OK;
    host_write_zero = 0U;
    host_redirect_result = OK;
    host_validate_result = OK;
    host_wait_validate_result = OK;
    host_thread_count = 0U;
    host_thread_fail_after = 0U;
    host_current_thread = 0;
    host_output_length = 0U;
    host_output[0] = '\0';
    host_redirect_path[0] = '\0';
    host_redirect_size = 0U;
    host_redirect_append = 0U;
    host_redirect_calls = 0U;
    host_dispatch_calls = 0U;
    host_dispatch_mode = HOST_DISPATCH_NORMAL;
    host_dispatch_result = OK;
}

static int expect_pipeline_error(const char* input, int expected) {
    uint8_t handled = 0U;
    int actual;

    actual = shell_pipeline_try_execute(input, &handled);
    if (actual != expected) return 1;
    return handled != 0U ? 0 : 2;
}

static int check_public_guards(void) {
    uint8_t handled = 0U;
    uint32_t bytes_read = 7U;
    char buffer[8];

    host_reset();
    if (shell_pipeline_is_active()) return 1;
    if (shell_pipeline_try_execute(0, &handled) != ERR_NULL) return 2;
    if (shell_pipeline_try_execute("echo", 0) != ERR_NULL) return 3;
    if (shell_pipeline_try_execute("echo", &handled) != OK || handled) return 4;
    if (shell_pipeline_write(0, 0U) != ERR_NULL) return 5;
    if (shell_pipeline_write("outside", 0U) != OK ||
        kstrcmp(host_output, "outside") != 0) return 6;
    if (shell_pipeline_read(0, sizeof(buffer), &bytes_read) != ERR_NULL) return 7;
    if (shell_pipeline_read(buffer, sizeof(buffer), 0) != ERR_NULL) return 8;
    if (shell_pipeline_read(buffer, sizeof(buffer), &bytes_read) !=
        ERR_UNAVAILABLE || bytes_read != 0U) return 9;
    shell_pipeline_print_num(0U);
    shell_pipeline_print_num(42U);
    if (kstrcmp(host_output, "outside042") != 0) return 10;
    return 0;
}

static int check_parser_rejections(void) {
    char long_segment[SHELL_BUFFER_SIZE + 1U];
    char long_segment_input[SHELL_BUFFER_SIZE + 7U];
    char long_target[VFS_MAX_PATH + 1U];
    char long_command[64];
    char long_input[VFS_MAX_PATH + 8U];
    char long_command_input[72];

    for (uint32_t index = 0U; index < sizeof(long_segment) - 1U; index++) {
        long_segment[index] = 'x';
    }
    long_segment[sizeof(long_segment) - 1U] = '\0';
    for (uint32_t index = 0U; index < sizeof(long_target) - 1U; index++) {
        long_target[index] = 'y';
    }
    long_target[sizeof(long_target) - 1U] = '\0';
    for (uint32_t index = 0U; index < sizeof(long_command) - 1U; index++) {
        long_command[index] = 'z';
    }
    long_command[sizeof(long_command) - 1U] = '\0';
    kmemcpy(long_input, "echo > ", 7U);
    kmemcpy(long_input + 7U, long_target, sizeof(long_target));
    kmemcpy(long_command_input, long_command, sizeof(long_command));
    kmemcpy(long_command_input + sizeof(long_command) - 1U, "|echo", 6U);
    if (expect_pipeline_error("echo||grep", ERR_INVALID)) return 1;
    if (expect_pipeline_error("echo>", ERR_INVALID)) return 2;
    if (expect_pipeline_error("echo > ", ERR_INVALID)) return 3;
    if (expect_pipeline_error("echo>one>two", ERR_INVALID)) return 4;
    if (expect_pipeline_error("echo >one | grep", ERR_INVALID)) return 5;
    if (expect_pipeline_error("grep|echo", ERR_INVALID)) return 6;
    if (expect_pipeline_error("unknown|echo", ERR_NOT_FOUND)) return 7;
    if (expect_pipeline_error("echo|echo|echo|echo|echo", ERR_INVALID)) return 8;
    kmemcpy(long_segment_input, long_segment, sizeof(long_segment) - 1U);
    kmemcpy(long_segment_input + sizeof(long_segment) - 1U, "|echo", 6U);
    if (expect_pipeline_error(long_segment_input, ERR_INVALID)) return 9;
    if (expect_pipeline_error(long_input, ERR_INVALID)) return 10;
    if (expect_pipeline_error("echo > target with-space", ERR_INVALID)) return 11;
    if (expect_pipeline_error(long_command_input, ERR_OVERFLOW)) return 12;
    return 0;
}

static int check_self_test_and_pipeline(void) {
    uint8_t handled = 0U;

    host_reset();
    if (shell_pipeline_self_test() != OK) return 1;
    if (shell_pipeline_is_active()) return 2;
    host_reset();
    if (shell_pipeline_try_execute("echo | grep", &handled) != OK || !handled) {
        return 3;
    }
    if (host_dispatch_calls != 2U) return 4;
    host_reset();
    if (shell_pipeline_try_execute("echo | grep > output", &handled) != OK ||
        !handled || host_redirect_calls != 1U || host_redirect_size != 14U ||
        !host_redirect_path[0] || host_redirect_append) return 5;
    host_reset();
    if (shell_pipeline_try_execute("echo >> output", &handled) != OK ||
        host_redirect_calls != 1U || !host_redirect_append) return 6;
    return 0;
}

static int check_fault_paths(void) {
    uint8_t handled = 0U;

    host_reset();
    host_pipe_failure = 1;
    if (shell_pipeline_try_execute("echo | grep", &handled) != ERR_UNAVAILABLE) {
        return 1;
    }
    host_reset();
    host_thread_fail_after = 1U;
    if (shell_pipeline_try_execute("echo | grep", &handled) != ERR_MEM) return 2;
    host_reset();
    host_thread_fail_after = 1U;
    if (shell_pipeline_try_execute("echo > output", &handled) != ERR_MEM) return 3;
    host_reset();
    host_write_result = ERR_DISK;
    if (shell_pipeline_try_execute("echo > output", &handled) != ERR_DISK) return 4;
    host_reset();
    host_write_zero = 1U;
    if (shell_pipeline_try_execute("echo > output", &handled) != ERR_UNAVAILABLE) {
        return 5;
    }
    host_reset();
    host_dispatch_mode = HOST_DISPATCH_READ_FAILURE;
    host_read_result = ERR_DISK;
    if (shell_pipeline_try_execute("echo | grep", &handled) != ERR_DISK) return 6;
    host_reset();
    host_redirect_result = ERR_DISK;
    {
        int actual = shell_pipeline_try_execute("echo > output", &handled);

        if (actual != ERR_DISK) return 7;
    }
    host_reset();
    host_dispatch_mode = HOST_DISPATCH_NESTED;
    if (shell_pipeline_try_execute("echo | echo", &handled) != OK) return 8;
    host_reset();
    host_dispatch_mode = HOST_DISPATCH_SELF_TEST;
    if (shell_pipeline_try_execute("echo | echo", &handled) != OK) return 9;
    return 0;
}

static int check_large_redirect_and_validation(void) {
    uint8_t handled = 0U;

    for (uint32_t index = 0U; index < HOST_BIG_OUTPUT_SIZE; index++) {
        host_big_output[index] = 'A';
    }
    host_big_output[HOST_BIG_OUTPUT_SIZE] = '\0';
    host_reset();
    host_dispatch_mode = HOST_DISPATCH_BIG;
    if (shell_pipeline_try_execute("echo > large", &handled) != ERR_OVERFLOW) {
        return 1;
    }
    host_reset();
    host_validate_result = ERR_STATE;
    if (shell_pipeline_self_test() != ERR_STATE) return 2;
    host_reset();
    host_wait_validate_result = ERR_STATE;
    if (shell_pipeline_self_test() != ERR_STATE) return 3;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = check_public_guards();
    if (!result) result = check_parser_rejections();
    if (!result) result = check_self_test_and_pipeline();
    if (!result) result = check_fault_paths();
    if (!result) result = check_large_redirect_and_validation();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("SHELL_PIPELINE_FAIL:%d\n", result);
        return result;
    }
    printf("SHELL_PIPELINE_PASS\n");
    return 0;
}
