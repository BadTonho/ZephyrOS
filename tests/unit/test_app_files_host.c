#include <stdint.h>
#include <stdio.h>

#include "core/app_files.h"
#include "core/errors.h"
#include "core/log.h"
#include "fs/vfs.h"

#define APP_FILES_HOST_HANDLE 7U
#define APP_FILES_HOST_READ_SIZE 3U
#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static int fake_init_result = OK;
static int fake_operation_result = OK;
static int fake_vfs_ready;
static uint32_t fake_init_calls;
static uint32_t fake_last_fd;
static uint32_t fake_last_mode;
static uint32_t fake_last_size;
static uint32_t fake_last_timeout;
static uint32_t fake_last_request;
static uint32_t fake_last_pid;
static uint32_t fake_last_whence;
static int32_t fake_last_offset;
static const char* fake_last_path;
static uint32_t fake_read_bytes;
static uint32_t fake_write_bytes;

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
    printf("ZCOV_BEGIN|case=host:core:app-files|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-files|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-files|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "app-files-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

int vfs_init(void) {
    fake_init_calls++;
    if (fake_init_result == OK) fake_vfs_ready = 1;
    return fake_init_result;
}

int vfs_is_ready(void) {
    return fake_vfs_ready;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    fake_last_path = path;
    fake_last_mode = mode;
    if (fake_operation_result != OK) return fake_operation_result;
    if (!path || !fd_out) return ERR_NULL;
    *fd_out = (int32_t)APP_FILES_HOST_HANDLE;
    return OK;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size,
             uint32_t* bytes_read) {
    fake_last_fd = (uint32_t)fd;
    fake_last_size = size;
    if (fake_operation_result != OK) return fake_operation_result;
    if (!buffer || !bytes_read) return ERR_NULL;
    *bytes_read = fake_read_bytes;
    return OK;
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    fake_last_fd = (uint32_t)fd;
    fake_last_size = size;
    if (fake_operation_result != OK) return fake_operation_result;
    if (!buffer || !bytes_written) return ERR_NULL;
    *bytes_written = fake_write_bytes;
    return OK;
}

int vfs_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
             uint32_t* out_ready) {
    fake_last_size = count;
    fake_last_timeout = timeout_ticks;
    if (fake_operation_result != OK) return fake_operation_result;
    if (!out_ready || (count && !fds)) return ERR_NULL;
    *out_ready = 1U;
    return OK;
}

int vfs_select(uint32_t nfds, fd_set_t* readfds, fd_set_t* writefds,
               fd_set_t* exceptfds, uint32_t timeout_ticks,
               uint32_t* out_ready) {
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    fake_last_size = nfds;
    fake_last_timeout = timeout_ticks;
    if (fake_operation_result != OK) return fake_operation_result;
    if (!out_ready) return ERR_NULL;
    *out_ready = 2U;
    return OK;
}

int vfs_close(int32_t fd) {
    fake_last_fd = (uint32_t)fd;
    return fake_operation_result;
}

int vfs_fsync(int32_t fd) {
    fake_last_fd = (uint32_t)fd;
    return fake_operation_result;
}

int vfs_sync(void) {
    return fake_operation_result;
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    fake_last_fd = (uint32_t)fd;
    fake_last_offset = offset;
    fake_last_whence = whence;
    if (fake_operation_result != OK) return fake_operation_result;
    if (!position) return ERR_NULL;
    *position = 11U;
    return OK;
}

int vfs_ioctl(int32_t fd, uint32_t request, void* argument) {
    (void)argument;
    fake_last_fd = (uint32_t)fd;
    fake_last_request = request;
    return fake_operation_result;
}

int vfs_pipe(int32_t fds[2]) {
    if (fake_operation_result != OK) return fake_operation_result;
    if (!fds) return ERR_NULL;
    fds[0] = 8;
    fds[1] = 9;
    return OK;
}

int vfs_chdir(const char* path) {
    fake_last_path = path;
    if (fake_operation_result != OK) return fake_operation_result;
    return path ? OK : ERR_NULL;
}

int vfs_getcwd(char* path, uint32_t capacity) {
    if (fake_operation_result != OK) return fake_operation_result;
    if (!path) return ERR_NULL;
    if (capacity < 2U) return ERR_OVERFLOW;
    path[0] = '/';
    path[1] = '\0';
    return OK;
}

int vfs_close_owner(uint32_t pid) {
    fake_last_pid = pid;
    return fake_operation_result;
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

uint32_t timer_get_ticks(void) {
    return 1U;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

static void test_before_initialization(void) {
    app_handle_t handle = APP_HANDLE_INVALID;
    app_handle_t fds[2] = {APP_HANDLE_INVALID, APP_HANDLE_INVALID};
    uint8_t buffer[4] = {0};
    pollfd_t poll = {0};
    fd_set_t set = {0};
    uint32_t value = 0U;
    char path[4] = {0};

    EXPECT(!app_files_is_ready());
    EXPECT(app_files_open("/", APP_FILE_MODE_READ, &handle) == ERR_STATE);
    EXPECT(app_files_read(1U, buffer, sizeof(buffer), &value) == ERR_STATE);
    EXPECT(app_files_write(1U, buffer, sizeof(buffer), &value) == ERR_STATE);
    EXPECT(app_files_poll(&poll, 1U, 0U, &value) == ERR_STATE);
    EXPECT(app_files_select(1U, &set, 0, 0, 0U, &value) == ERR_STATE);
    EXPECT(app_files_close(1U) == ERR_STATE);
    EXPECT(app_files_fsync(1U) == ERR_STATE);
    EXPECT(app_files_sync() == ERR_STATE);
    EXPECT(app_files_lseek(1U, 0, APP_SEEK_SET, &value) == ERR_STATE);
    EXPECT(app_files_ioctl(1U, 0U, 0) == ERR_STATE);
    EXPECT(app_files_pipe(fds) == ERR_STATE);
    EXPECT(app_files_chdir("/") == ERR_STATE);
    EXPECT(app_files_getcwd(path, sizeof(path)) == ERR_STATE);
    EXPECT(app_files_close_owner(42U) == ERR_STATE);
}

static void test_initialization(void) {
    fake_init_result = ERR_DISK;
    EXPECT(app_files_init() == ERR_DISK);
    EXPECT(!app_files_is_ready());
    EXPECT(fake_init_calls == 1U);
    fake_init_result = OK;
    EXPECT(app_files_init() == OK);
    EXPECT(app_files_is_ready());
    EXPECT(fake_init_calls == 2U);
    EXPECT(app_files_init() == OK);
    EXPECT(fake_init_calls == 2U);
}

static void test_forwarded_operations(void) {
    app_handle_t handle = APP_HANDLE_INVALID;
    app_handle_t fds[2] = {APP_HANDLE_INVALID, APP_HANDLE_INVALID};
    uint8_t buffer[4] = {0};
    pollfd_t poll = {0};
    fd_set_t set = {0};
    uint32_t value = 0U;
    char path[4] = {0};

    fake_read_bytes = APP_FILES_HOST_READ_SIZE;
    fake_write_bytes = 4U;
    EXPECT(app_files_open("/tmp/file", APP_FILE_MODE_READ, &handle) == OK);
    EXPECT(handle == APP_FILES_HOST_HANDLE && fake_last_path[0] == '/');
    EXPECT(fake_last_mode == APP_FILE_MODE_READ);
    EXPECT(app_files_open("/", APP_FILE_MODE_READ, 0) == ERR_NULL);
    EXPECT(app_files_read(handle, buffer, sizeof(buffer), &value) == OK);
    EXPECT(fake_last_fd == APP_FILES_HOST_HANDLE && value == 3U);
    EXPECT(fake_last_size == sizeof(buffer));
    EXPECT(app_files_write(handle, buffer, sizeof(buffer), &value) == OK);
    EXPECT(fake_last_fd == APP_FILES_HOST_HANDLE && value == 4U);
    EXPECT(app_files_poll(&poll, 1U, 5U, &value) == OK && value == 1U);
    EXPECT(fake_last_size == 1U && fake_last_timeout == 5U);
    EXPECT(app_files_select(1U, &set, 0, 0, 6U, &value) == OK && value == 2U);
    EXPECT(fake_last_size == 1U && fake_last_timeout == 6U);
    EXPECT(app_files_close(handle) == OK);
    EXPECT(app_files_fsync(handle) == OK);
    EXPECT(app_files_sync() == OK);
    EXPECT(app_files_lseek(handle, -2, APP_SEEK_END, &value) == OK);
    EXPECT(fake_last_offset == -2 && fake_last_whence == APP_SEEK_END);
    EXPECT(value == 11U);
    EXPECT(app_files_ioctl(handle, 9U, 0) == OK);
    EXPECT(fake_last_request == 9U);
    EXPECT(app_files_pipe(fds) == OK && fds[0] == 8U && fds[1] == 9U);
    EXPECT(app_files_chdir("/mnt") == OK);
    EXPECT(app_files_chdir(0) == ERR_NULL);
    EXPECT(app_files_getcwd(path, sizeof(path)) == OK);
    EXPECT(path[0] == '/' && path[1] == '\0');
    EXPECT(app_files_getcwd(path, 1U) == ERR_OVERFLOW);
    EXPECT(app_files_close_owner(42U) == OK && fake_last_pid == 42U);
}

static void test_errors_and_outputs(void) {
    app_handle_t fds[2] = {123U, 456U};
    uint8_t buffer[2] = {0};
    char path[2] = {0};
    uint32_t value = 99U;

    EXPECT(app_files_read(1U, buffer, sizeof(buffer), 0) == ERR_NULL);
    EXPECT(app_files_write(1U, buffer, sizeof(buffer), 0) == ERR_NULL);
    EXPECT(app_files_poll(0, 1U, 0U, &value) == ERR_NULL);
    EXPECT(app_files_select(1U, 0, 0, 0, 0U, 0) == ERR_NULL);
    EXPECT(app_files_lseek(1U, 0, APP_SEEK_SET, 0) == ERR_NULL);
    EXPECT(app_files_getcwd(0, 2U) == ERR_NULL);
    fake_operation_result = ERR_TIMEOUT;
    EXPECT(app_files_open("/tmp", APP_FILE_MODE_READ, &value) == ERR_TIMEOUT);
    EXPECT(app_files_read(1U, buffer, sizeof(buffer), &value) == ERR_TIMEOUT);
    EXPECT(app_files_write(1U, buffer, sizeof(buffer), &value) == ERR_TIMEOUT);
    EXPECT(app_files_poll(0, 0U, 0U, &value) == ERR_TIMEOUT);
    EXPECT(app_files_select(0U, 0, 0, 0, 0U, &value) == ERR_TIMEOUT);
    EXPECT(app_files_close(1U) == ERR_TIMEOUT);
    EXPECT(app_files_fsync(1U) == ERR_TIMEOUT);
    EXPECT(app_files_sync() == ERR_TIMEOUT);
    EXPECT(app_files_lseek(1U, 0, APP_SEEK_SET, &value) == ERR_TIMEOUT);
    EXPECT(app_files_ioctl(1U, 0U, 0) == ERR_TIMEOUT);
    EXPECT(app_files_pipe(fds) == ERR_TIMEOUT);
    EXPECT(fds[0] == APP_HANDLE_INVALID && fds[1] == APP_HANDLE_INVALID);
    EXPECT(app_files_chdir("/") == ERR_TIMEOUT);
    EXPECT(app_files_getcwd(path, sizeof(path)) == ERR_TIMEOUT);
    EXPECT(app_files_close_owner(1U) == ERR_TIMEOUT);
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    test_before_initialization();
    test_initialization();
    test_forwarded_operations();
    test_errors_and_outputs();
    coverage_active = 0U;
    coverage_emit(result);
    printf("app_files host test OK\n");
    return result;
}
