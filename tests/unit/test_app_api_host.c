#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/timer.h"
#include "memory/vma.h"
#include "process/process.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks = 100U;
static int fake_files_init_result = OK;
static int fake_files_ready;
static int fake_ipc_ready;
static int fake_ipc_send_result = 1;
static int fake_ipc_receive_result;
static int fake_vma_mmap_result = OK;
static int fake_vma_munmap_result = OK;
static int fake_process_user;
static process_t fake_process;
static ipc_msg_t fake_received_message;
static uint32_t fake_console_writes;

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
    printf("ZCOV_BEGIN|case=host:core:app-api|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:app-api|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:app-api|value=0x%08X\n",
           (uint32_t)result);
}

int app_files_init(void) {
    if (fake_files_init_result == OK) fake_files_ready = 1;
    return fake_files_init_result;
}

int app_files_is_ready(void) { return fake_files_ready; }

int app_files_open(const char* path, uint32_t mode, app_handle_t* handle) {
    (void)path; (void)mode;
    if (!handle) return ERR_NULL;
    *handle = 17U;
    return OK;
}

int app_files_read(app_handle_t handle, uint8_t* buffer,
                   uint32_t size, uint32_t* bytes_read) {
    (void)handle; (void)buffer;
    if (!bytes_read) return ERR_NULL;
    *bytes_read = size > 2U ? 2U : size;
    return OK;
}

int app_files_write(app_handle_t handle, const uint8_t* buffer,
                    uint32_t size, uint32_t* bytes_written) {
    (void)handle; (void)buffer;
    if (!bytes_written) return ERR_NULL;
    *bytes_written = size;
    return OK;
}

int app_files_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
                   uint32_t* out_ready) {
    (void)fds; (void)count; (void)timeout_ticks;
    if (!out_ready) return ERR_NULL;
    *out_ready = 1U;
    return OK;
}

int app_files_select(uint32_t nfds, fd_set_t* readfds, fd_set_t* writefds,
                     fd_set_t* exceptfds, uint32_t timeout_ticks,
                     uint32_t* out_ready) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds;
    (void)timeout_ticks;
    if (!out_ready) return ERR_NULL;
    *out_ready = 2U;
    return OK;
}

int app_files_close(app_handle_t handle) { return handle == 17U ? OK : ERR_INVALID; }
int app_files_fsync(app_handle_t handle) { return handle == 17U ? OK : ERR_INVALID; }
int app_files_sync(void) { return OK; }

int app_files_lseek(app_handle_t handle, int32_t offset, uint32_t whence,
                    uint32_t* position) {
    (void)offset; (void)whence;
    if (!position) return ERR_NULL;
    *position = handle == 17U ? 4U : 0U;
    return handle == 17U ? OK : ERR_INVALID;
}

int app_files_ioctl(app_handle_t handle, uint32_t request, void* argument) {
    (void)request; (void)argument;
    return handle == 17U ? OK : ERR_INVALID;
}

int app_files_pipe(app_handle_t fds[2]) {
    if (!fds) return ERR_NULL;
    fds[0] = 20U; fds[1] = 21U;
    return OK;
}

int app_files_chdir(const char* path) { return path && path[0] ? OK : ERR_INVALID; }

int app_files_getcwd(char* path, uint32_t capacity) {
    if (!path) return ERR_NULL;
    if (capacity < 2U) return ERR_OVERFLOW;
    path[0] = '/'; path[1] = '\0';
    return OK;
}

void memory_unused(void) {
}

uint32_t memory_get_total(void) { return 100000U; }
uint32_t memory_get_used(void) { return 40000U; }
uint32_t memory_get_free(void) { return 60000U; }
uint32_t memory_get_total_pages(void) { return 25U; }
uint32_t memory_get_free_pages(void) { return 15U; }

int ipc_is_ready(void) { return fake_ipc_ready; }

int ipc_send(uint32_t pid, ipc_msg_t* message) {
    (void)pid; (void)message;
    return fake_ipc_send_result;
}

int ipc_receive(ipc_msg_t* message) {
    if (!message) return 0;
    if (!fake_ipc_receive_result) return 0;
    *message = fake_received_message;
    return 1;
}

process_t* process_get_by_pid(uint32_t pid) {
    return pid == 42U ? &fake_process : NULL;
}

process_t* process_get_current(void) {
    return fake_process.pid ? &fake_process : NULL;
}

int process_is_user(const process_t* process) {
    return process == &fake_process && fake_process_user;
}

int process_vma_mmap(struct process* process, uint32_t length,
                     uint32_t protection, uint32_t flags,
                     uint32_t* address_out) {
    (void)process; (void)length; (void)protection; (void)flags;
    if (fake_vma_mmap_result == OK) *address_out = 0x40000000U;
    return fake_vma_mmap_result;
}

int process_vma_munmap(struct process* process, uint32_t address,
                       uint32_t length) {
    (void)process; (void)address; (void)length;
    return fake_vma_munmap_result;
}

uint32_t timer_get_ticks(void) { return fake_ticks++; }
uint32_t timer_get_frequency(void) { return 1000U; }

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

int serial_is_ready(void) { return 1; }

void video_print(const char* text, uint8_t color) {
    (void)color;
    if (text) fake_console_writes++;
}

void video_newline(void) {
}

static int test_uninitialized_contract(void) {
    app_api_version_t version;
    app_uptime_info_t uptime;
    app_memory_info_t memory;
    app_message_t message;
    uint32_t value = 0U;
    char path[4];

    if (app_api_is_ready() || app_api_file_is_ready() || app_api_ipc_is_ready()) {
        return 1;
    }
    if (app_api_get_version(&version) != ERR_STATE ||
        app_api_console_write("x", 1U) != ERR_STATE ||
        app_api_get_uptime(&uptime) != ERR_STATE ||
        app_api_get_memory_info(&memory) != ERR_STATE ||
        app_api_file_open("/", APP_FILE_MODE_READ, &value) != ERR_STATE ||
        app_api_file_close(0U) != ERR_STATE ||
        app_api_message_receive(&message) != ERR_STATE ||
        app_api_mmap(4096U, APP_MMAP_PROT_READ, APP_MMAP_FLAG_ANONYMOUS,
                     &value) != ERR_STATE ||
        app_api_munmap(0U, 4096U) != ERR_STATE) return 2;
    if (app_api_getcwd(path, sizeof(path)) != ERR_STATE) return 3;
    return 0;
}

static int test_initialization_and_scalar_apis(void) {
    app_api_version_t version;
    app_uptime_info_t uptime;
    app_memory_info_t memory;
    char valid_text[] = "hello\n";
    char invalid_text[] = "bad\x01";
    char non_ascii[] = "\xC3";

    fake_files_init_result = ERR_DISK;
    if (app_api_init() != ERR_STATE || app_api_is_ready()) return 10;
    fake_files_init_result = OK;
    if (app_api_init() != OK || !app_api_is_ready() || !app_api_file_is_ready()) {
        return 11;
    }
    fake_console_writes = 0U;
    if (app_api_get_version(NULL) != ERR_NULL || app_api_get_version(&version) != OK ||
        version.major != APP_API_VERSION_MAJOR || version.minor != APP_API_VERSION_MINOR) {
        return 12;
    }
    if (app_api_console_write(NULL, 1U) != ERR_NULL) return 13;
    if (app_api_console_write(valid_text, 0U) != ERR_INVALID) return 13;
    if (app_api_console_write(valid_text, APP_API_MAX_TEXT_SIZE + 1U) != ERR_OVERFLOW) return 13;
    if (app_api_console_write(invalid_text, sizeof(invalid_text) - 1U) != ERR_INVALID) return 13;
    if (app_api_console_write(non_ascii, sizeof(non_ascii) - 1U) != ERR_INVALID) return 13;
    fake_console_writes = 0U;
    if (app_api_console_write(valid_text, sizeof(valid_text) - 1U) != OK) return 13;
    if (fake_console_writes != 1U) return 13;
    if (app_api_get_uptime(NULL) != ERR_NULL) return 14;
    fake_ticks = 100U;
    if (app_api_get_uptime(&uptime) != OK ||
        uptime.ticks != 100U || uptime.seconds != 2U) return 14;
    if (app_api_get_memory_info(NULL) != ERR_NULL ||
        app_api_get_memory_info(&memory) != OK || memory.total_bytes != 100000U ||
        memory.used_bytes != 40000U || memory.free_pages != 15U) return 15;
    return 0;
}

static int test_file_apis(void) {
    app_handle_t handle;
    app_handle_t pipes[2];
    uint8_t buffer[4] = {0};
    uint32_t value = 0U;
    pollfd_t poll = {0};
    fd_set_t set = {0};
    char path[4];

    if (app_api_file_open("/tmp", APP_FILE_MODE_READ, &handle) != OK ||
        handle != 17U || app_api_file_open("/", 0U, NULL) != ERR_NULL) return 20;
    if (app_api_file_read(handle, buffer, sizeof(buffer), &value) != OK ||
        value != 2U || app_api_file_read(handle, buffer, 1U, NULL) != ERR_NULL ||
        app_api_file_write(handle, buffer, 3U, &value) != OK || value != 3U ||
        app_api_file_write(handle, buffer, 3U, NULL) != ERR_NULL) return 21;
    if (app_api_poll(&poll, 1U, 5U, &value) != OK || value != 1U ||
        app_api_select(1U, &set, NULL, NULL, 5U, &value) != OK || value != 2U) {
        return 22;
    }
    if (app_api_file_close(handle) != OK || app_api_file_fsync(handle) != OK ||
        app_api_sync() != OK || app_api_file_lseek(handle, 0, APP_SEEK_SET,
                                                    &value) != OK || value != 4U ||
        app_api_file_ioctl(handle, 0U, NULL) != OK || app_api_pipe(pipes) != OK ||
        pipes[0] != 20U || pipes[1] != 21U) return 23;
    if (app_api_chdir(NULL) != ERR_NULL || app_api_chdir("/") != OK ||
        app_api_getcwd(NULL, sizeof(path)) != ERR_NULL ||
        app_api_getcwd(path, 1U) != ERR_OVERFLOW ||
        app_api_getcwd(path, sizeof(path)) != OK || strcmp(path, "/") != 0) return 24;
    return 0;
}

static int test_ipc_and_vma_apis(void) {
    app_message_t message = {APP_MESSAGE_KEYBOARD, 7U, 8U};
    app_message_t invalid = {99U, 0U, 0U};
    app_message_t received;
    uint32_t address = 0U;

    if (app_api_message_send(42U, &message) != ERR_UNAVAILABLE ||
        app_api_message_send(42U, NULL) != ERR_UNAVAILABLE) return 30;
    fake_ipc_ready = 1;
    if (!app_api_ipc_is_ready() || app_api_message_send(42U, NULL) != ERR_NULL ||
        app_api_message_send(42U, &invalid) != ERR_INVALID ||
        app_api_message_send(99U, &message) != ERR_NOT_FOUND) return 31;
    fake_process.pid = 42U;
    fake_process.state = PROCESS_STATE_READY;
    fake_process_user = 1;
    fake_ipc_send_result = 1;
    if (app_api_message_send(42U, &message) != OK) return 32;
    fake_process.state = PROCESS_STATE_ZOMBIE;
    if (app_api_message_send(42U, &message) != ERR_STATE) return 33;
    fake_process.state = PROCESS_STATE_READY;
    fake_ipc_send_result = 0;
    if (app_api_message_send(42U, &message) != ERR_UNAVAILABLE) return 34;
    fake_ipc_send_result = 1;
    if (app_api_message_receive(NULL) != ERR_NULL) return 35;
    if (app_api_message_receive(&received) != ERR_NOT_FOUND) return 36;
    fake_received_message.type = IPC_MSG_APP_REQUEST;
    fake_received_message.data1 = 10U;
    fake_received_message.data2 = 11U;
    fake_ipc_receive_result = 1;
    if (app_api_message_receive(&received) != OK || received.type != IPC_MSG_APP_REQUEST ||
        received.data1 != 10U || received.data2 != 11U) return 37;
    fake_process_user = 0;
    if (app_api_mmap(4096U, APP_MMAP_PROT_READ, APP_MMAP_FLAG_ANONYMOUS,
                     &address) != ERR_STATE) return 38;
    fake_process_user = 1;
    fake_vma_mmap_result = ERR_UNAVAILABLE;
    if (app_api_mmap(4096U, APP_MMAP_PROT_READ, APP_MMAP_FLAG_ANONYMOUS,
                     &address) != ERR_UNAVAILABLE) return 39;
    fake_vma_mmap_result = OK;
    if (app_api_mmap(4096U, APP_MMAP_PROT_READ, APP_MMAP_FLAG_ANONYMOUS,
                     &address) != OK || address != 0x40000000U ||
        app_api_munmap(0x40000000U, 4096U) != OK) return 40;
    fake_vma_munmap_result = ERR_INVALID;
    if (app_api_munmap(0x40000000U, 4096U) != ERR_INVALID) return 41;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_uninitialized_contract();
    if (!result) result = test_initialization_and_scalar_apis();
    if (!result) result = test_file_apis();
    if (!result) result = test_ipc_and_vma_apis();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
