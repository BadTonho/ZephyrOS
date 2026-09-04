#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/syscall.h"
#include "drivers/tss.h"
#include "fs/fs.h"
#include "memory/paging.h"
#include "process/process.h"
#include "process/signal.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

#define USER_PATH_ADDRESS       0x1000U
#define USER_TEXT_ADDRESS       0x2000U
#define USER_BUFFER_ADDRESS     0x3000U
#define USER_COUNT_ADDRESS      0x4000U
#define USER_POLL_ADDRESS       0x5000U
#define USER_SELECT_ADDRESS     0x6000U
#define USER_MESSAGE_ADDRESS    0x7000U
#define USER_PIPE_ADDRESS       0x8000U
#define USER_MMAP_ADDRESS       0x9000U
#define USER_TONE_ADDRESS       0xA000U
#define USER_ACTION_ADDRESS     0xB000U
#define USER_OLD_ACTION_ADDRESS 0xC000U
#define USER_OLD_MASK_ADDRESS   0xD000U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t fake_app_ready;
static uint8_t fake_user_dispatch;
static uint8_t fake_paging_ready;
static uint8_t fake_tss_ready;
static uint8_t fake_idt_user_enabled;
static int fake_idt_register_result = OK;
static int fake_idt_enable_result = OK;
static int fake_receive_result = OK;
static int fake_mmap_result = OK;
static int fake_munmap_result = OK;
static int fake_signal_action_result = OK;
static int fake_signal_mask_result = OK;
static int fake_signal_raise_result = OK;
static int fake_signal_return_result = OK;
static int fake_process_exit_result = OK;
static int fake_termination_result = OK;
static process_t fake_process;
static uint8_t fake_heap[APP_API_MAX_FILE_IO_SIZE];
static char user_path[FS_MAX_PATH];
static char user_text[APP_API_MAX_TEXT_SIZE];
static uint8_t user_buffer[APP_API_MAX_FILE_IO_SIZE];
static uint32_t user_count;
static pollfd_t user_poll[POLL_MAX_FDS];
static select_request_t user_select;
static app_message_t user_message;
static app_handle_t user_pipe[2];
static uint32_t user_mmap_result;
static app_speaker_tone_t user_tone;
static app_signal_action_t user_action;
static app_signal_action_t user_old_action;
static uint32_t user_old_mask;

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
    printf("ZCOV_BEGIN|case=host:core:syscall|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:syscall|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:syscall|value=0x%08X\n",
           (uint32_t)result);
}

static int map_user_region(uint32_t address, uint32_t size, uint32_t base,
                           uint8_t* storage, uint32_t capacity,
                           uint8_t** output) {
    uint32_t offset;

    if (address < base) return ERR_INVALID;
    offset = address - base;
    if (offset > capacity || size > capacity - offset) return ERR_INVALID;
    *output = storage + offset;
    return OK;
}

static int map_user_address(uint32_t address, uint32_t size, uint8_t** output) {
    if (address >= USER_PATH_ADDRESS && address - USER_PATH_ADDRESS < sizeof(user_path)) {
        return map_user_region(address, size, USER_PATH_ADDRESS,
                                (uint8_t*)user_path, sizeof(user_path), output);
    }
    if (address >= USER_TEXT_ADDRESS && address - USER_TEXT_ADDRESS < sizeof(user_text)) {
        return map_user_region(address, size, USER_TEXT_ADDRESS,
                                (uint8_t*)user_text, sizeof(user_text), output);
    }
    if (address >= USER_BUFFER_ADDRESS && address - USER_BUFFER_ADDRESS < sizeof(user_buffer)) {
        return map_user_region(address, size, USER_BUFFER_ADDRESS,
                                user_buffer, sizeof(user_buffer), output);
    }
    if (address >= USER_COUNT_ADDRESS && address - USER_COUNT_ADDRESS < sizeof(user_count)) {
        return map_user_region(address, size, USER_COUNT_ADDRESS,
                                (uint8_t*)&user_count, sizeof(user_count), output);
    }
    if (address >= USER_POLL_ADDRESS && address - USER_POLL_ADDRESS < sizeof(user_poll)) {
        return map_user_region(address, size, USER_POLL_ADDRESS,
                                (uint8_t*)user_poll, sizeof(user_poll), output);
    }
    if (address >= USER_SELECT_ADDRESS && address - USER_SELECT_ADDRESS < sizeof(user_select)) {
        return map_user_region(address, size, USER_SELECT_ADDRESS,
                                (uint8_t*)&user_select, sizeof(user_select), output);
    }
    if (address >= USER_MESSAGE_ADDRESS && address - USER_MESSAGE_ADDRESS < sizeof(user_message)) {
        return map_user_region(address, size, USER_MESSAGE_ADDRESS,
                                (uint8_t*)&user_message, sizeof(user_message), output);
    }
    if (address >= USER_PIPE_ADDRESS && address - USER_PIPE_ADDRESS < sizeof(user_pipe)) {
        return map_user_region(address, size, USER_PIPE_ADDRESS,
                                (uint8_t*)user_pipe, sizeof(user_pipe), output);
    }
    if (address >= USER_MMAP_ADDRESS && address - USER_MMAP_ADDRESS < sizeof(user_mmap_result)) {
        return map_user_region(address, size, USER_MMAP_ADDRESS,
                                (uint8_t*)&user_mmap_result, sizeof(user_mmap_result), output);
    }
    if (address >= USER_TONE_ADDRESS && address - USER_TONE_ADDRESS < sizeof(user_tone)) {
        return map_user_region(address, size, USER_TONE_ADDRESS,
                                (uint8_t*)&user_tone, sizeof(user_tone), output);
    }
    if (address >= USER_ACTION_ADDRESS && address - USER_ACTION_ADDRESS < sizeof(user_action)) {
        return map_user_region(address, size, USER_ACTION_ADDRESS,
                                (uint8_t*)&user_action, sizeof(user_action), output);
    }
    if (address >= USER_OLD_ACTION_ADDRESS && address - USER_OLD_ACTION_ADDRESS < sizeof(user_old_action)) {
        return map_user_region(address, size, USER_OLD_ACTION_ADDRESS,
                                (uint8_t*)&user_old_action, sizeof(user_old_action), output);
    }
    if (address >= USER_OLD_MASK_ADDRESS && address - USER_OLD_MASK_ADDRESS < sizeof(user_old_mask)) {
        return map_user_region(address, size, USER_OLD_MASK_ADDRESS,
                                (uint8_t*)&user_old_mask, sizeof(user_old_mask), output);
    }
    return ERR_INVALID;
}

static void reset_fixture(void) {
    memset(&fake_process, 0, sizeof(fake_process));
    memset(fake_heap, 0, sizeof(fake_heap));
    memset(user_path, 0, sizeof(user_path));
    memset(user_text, 0, sizeof(user_text));
    memset(user_buffer, 0, sizeof(user_buffer));
    memset(user_poll, 0, sizeof(user_poll));
    memset(&user_select, 0, sizeof(user_select));
    memset(&user_message, 0, sizeof(user_message));
    memset(user_pipe, 0, sizeof(user_pipe));
    memset(&user_tone, 0, sizeof(user_tone));
    memset(&user_action, 0, sizeof(user_action));
    memset(&user_old_action, 0, sizeof(user_old_action));
    user_count = 0U;
    user_mmap_result = 0U;
    user_old_mask = 0U;
    strcpy(user_path, "/tmp");
    strcpy(user_text, "hello");
    fake_app_ready = 0U;
    fake_user_dispatch = 0U;
    fake_paging_ready = 0U;
    fake_tss_ready = 0U;
    fake_idt_user_enabled = 0U;
    fake_idt_register_result = OK;
    fake_idt_enable_result = OK;
    fake_receive_result = OK;
    fake_mmap_result = OK;
    fake_munmap_result = OK;
    fake_signal_action_result = OK;
    fake_signal_mask_result = OK;
    fake_signal_raise_result = OK;
    fake_signal_return_result = OK;
    fake_process_exit_result = OK;
    fake_termination_result = OK;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    memset(destination, value, size);
}

uint32_t kstrlen(const char* value) { return (uint32_t)strlen(value); }

void* kmalloc(uint32_t size) {
    return size <= sizeof(fake_heap) ? fake_heap : NULL;
}

void kfree(void* pointer) { (void)pointer; }

int app_api_is_ready(void) { return fake_app_ready; }
int app_api_init(void) { fake_app_ready = 1U; return OK; }
int app_api_file_is_ready(void) { return fake_app_ready; }
int app_api_ipc_is_ready(void) { return fake_app_ready; }

int app_api_console_write(const char* text, uint32_t size) {
    (void)text;
    return size <= APP_API_MAX_TEXT_SIZE ? OK : ERR_OVERFLOW;
}

int app_api_get_uptime(app_uptime_info_t* info) {
    if (fake_user_dispatch && info) {
        info->ticks = 25U;
        info->seconds = 1U;
    }
    return info ? OK : ERR_NULL;
}

int app_api_get_memory_info(app_memory_info_t* info) {
    if (fake_user_dispatch && info) {
        info->total_bytes = 100000U;
        info->free_bytes = 50000U;
        info->free_pages = 12U;
    }
    return info ? OK : ERR_NULL;
}

int app_api_file_open(const char* path, uint32_t mode, app_handle_t* handle) {
    (void)path;
    (void)mode;
    if (!handle) return ERR_NULL;
    if (fake_user_dispatch) *handle = 17U;
    return OK;
}

int app_api_file_read(app_handle_t handle, uint8_t* buffer, uint32_t size,
                      uint32_t* bytes_read) {
    (void)handle;
    if (!bytes_read) return ERR_NULL;
    if (fake_user_dispatch && buffer) {
        uint32_t count = size < 3U ? size : 3U;
        memset(buffer, 'R', count);
        *bytes_read = count;
    }
    return OK;
}

int app_api_file_write(app_handle_t handle, const uint8_t* buffer,
                       uint32_t size, uint32_t* bytes_written) {
    (void)handle;
    (void)buffer;
    if (!bytes_written) return ERR_NULL;
    if (fake_user_dispatch) *bytes_written = size;
    return OK;
}

int app_api_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
                 uint32_t* out_ready) {
    (void)timeout_ticks;
    if (!out_ready) return ERR_NULL;
    if (fake_user_dispatch && fds && count) fds[0].revents = POLLIN;
    if (fake_user_dispatch) *out_ready = count ? 1U : 0U;
    return OK;
}

int app_api_select(uint32_t nfds, fd_set_t* readfds, fd_set_t* writefds,
                   fd_set_t* exceptfds, uint32_t timeout_ticks,
                   uint32_t* out_ready) {
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout_ticks;
    if (!out_ready) return ERR_NULL;
    if (fake_user_dispatch) *out_ready = 1U;
    return OK;
}

int app_api_file_close(app_handle_t handle) { return handle == 17U ? OK : ERR_INVALID; }
int app_api_file_fsync(app_handle_t handle) { return handle == 17U ? OK : ERR_INVALID; }
int app_api_sync(void) { return OK; }

int app_api_file_lseek(app_handle_t handle, int32_t offset, uint32_t whence,
                       uint32_t* position) {
    (void)offset;
    (void)whence;
    if (!position) return ERR_NULL;
    if (fake_user_dispatch) *position = 4U;
    return handle == 17U ? OK : ERR_INVALID;
}

int app_api_file_ioctl(app_handle_t handle, uint32_t request, void* argument) {
    (void)request;
    (void)argument;
    return handle == 17U ? OK : ERR_INVALID;
}

int app_api_pipe(app_handle_t fds[2]) {
    if (!fds) return ERR_NULL;
    if (fake_user_dispatch) {
        fds[0] = 20U;
        fds[1] = 21U;
    }
    return OK;
}

int app_api_chdir(const char* path) {
    if (!fake_user_dispatch) return OK;
    return path && path[0] ? OK : ERR_INVALID;
}

int app_api_getcwd(char* path, uint32_t capacity) {
    if (!fake_user_dispatch) return OK;
    if (!path) return ERR_NULL;
    if (capacity < 2U) return ERR_OVERFLOW;
    if (fake_user_dispatch) {
        path[0] = '/';
        path[1] = '\0';
    }
    return OK;
}

int app_api_message_send(uint32_t pid, const app_message_t* message) {
    (void)pid;
    return message ? OK : ERR_NULL;
}

int app_api_message_receive(app_message_t* message) {
    if (!message) return ERR_NULL;
    if (fake_user_dispatch) {
        message->type = APP_MESSAGE_KEYBOARD;
        message->data1 = 0x1CU;
        message->data2 = 0U;
    }
    return fake_receive_result;
}

int app_api_mmap(uint32_t length, uint32_t protection, uint32_t flags,
                 uint32_t* address_out) {
    (void)length;
    (void)protection;
    (void)flags;
    if (!address_out) return ERR_NULL;
    if (fake_user_dispatch) *address_out = 0x40000000U;
    return fake_mmap_result;
}

int app_api_munmap(uint32_t address, uint32_t length) {
    (void)address;
    (void)length;
    return fake_munmap_result;
}

int paging_is_ready(void) { return fake_paging_ready; }

int paging_validate_user_range(uint32_t address, uint32_t size, int write) {
    uint8_t* ignored;
    (void)write;
    return map_user_address(address, size, &ignored);
}

int paging_copy_from_user(void* destination, const void* source, uint32_t size) {
    uint8_t* mapped;
    int result = map_user_address((uint32_t)(uintptr_t)source, size, &mapped);

    if (result != OK) {
        return result;
    }
    memcpy(destination, mapped, size);
    return OK;
}

int paging_copy_to_user(void* destination, const void* source, uint32_t size) {
    uint8_t* mapped;
    int result = map_user_address((uint32_t)(uintptr_t)destination, size, &mapped);

    if (result != OK) {
        return result;
    }
    memcpy(mapped, source, size);
    return OK;
}

process_t* process_get_current(void) { return fake_process.pid ? &fake_process : NULL; }
int process_is_user(const process_t* process) {
    return process == &fake_process && fake_process.context.user_mode;
}
int process_exit_current(uint32_t exit_code) {
    (void)exit_code;
    return fake_process_exit_result;
}
int process_prepare_user_termination(registers_t* regs) {
    (void)regs;
    return fake_termination_result;
}

int process_signal_action(uint32_t signal_number,
                          const app_signal_action_t* action,
                          app_signal_action_t* old_action) {
    (void)signal_number;
    (void)action;
    if (old_action) {
        old_action->disposition = APP_SIGNAL_DISPOSITION_DEFAULT;
        old_action->handler = 0U;
        old_action->mask = 0U;
    }
    return fake_signal_action_result;
}

int process_signal_mask(uint32_t operation, uint32_t mask, uint32_t* old_mask) {
    (void)operation;
    (void)mask;
    if (old_mask) *old_mask = 0U;
    return fake_signal_mask_result;
}

int process_signal_raise(uint32_t signal_number) {
    (void)signal_number;
    return fake_signal_raise_result;
}

int process_signal_return(registers_t* regs) {
    (void)regs;
    return fake_signal_return_result;
}

int ipc_wait(uint32_t timeout_ticks, wait_reason_t* out_reason) {
    (void)timeout_ticks;
    if (out_reason) *out_reason = WAIT_REASON_EVENT;
    return OK;
}

int idt_register_handler(uint8_t vector, isr_handler_t handler) {
    (void)vector;
    (void)handler;
    return fake_idt_register_result;
}

int idt_enable_user_syscall(void) {
    if (fake_idt_enable_result == OK) fake_idt_user_enabled = 1U;
    return fake_idt_enable_result;
}

int idt_is_user_syscall_enabled(void) { return fake_idt_user_enabled; }
int tss_is_ready(void) { return fake_tss_ready; }

static registers_t make_registers(uint32_t number) {
    registers_t regs;

    memset(&regs, 0, sizeof(regs));
    regs.eax = number;
    regs.cs = KERNEL_CODE_SELECTOR;
    regs.ss = KERNEL_DATA_SELECTOR;
    regs.int_no = SYSCALL_VECTOR;
    return regs;
}

static int user_call(registers_t* regs) {
    uint32_t number = regs->eax;

    fake_user_dispatch = 1U;
    syscall_handler(regs);
    fake_user_dispatch = 0U;
    int result = (int)regs->eax;
    regs->eax = number;
    return result;
}

static int test_lifecycle(void) {
    registers_t regs = make_registers(APP_SYSCALL_SYNC);

    reset_fixture();
    if (syscall_is_ready() != 0 || syscall_user_mode_is_enabled() != 0) return 10;
    syscall_handler(NULL);
    regs.int_no = SYSCALL_VECTOR + 1U;
    syscall_handler(&regs);
    if ((int)regs.eax != ERR_INVALID) return 11;
    regs = make_registers(APP_SYSCALL_SYNC);
    syscall_handler(&regs);
    if ((int)regs.eax != ERR_STATE) return 12;
    if (syscall_init() != ERR_UNAVAILABLE) return 13;
    fake_app_ready = 1U;
    fake_idt_register_result = ERR_STATE;
    if (syscall_init() != ERR_STATE) return 14;
    fake_idt_register_result = OK;
    if (syscall_init() != OK || syscall_init() != OK || !syscall_is_ready()) return 15;
    if (syscall_enable_user_mode() != ERR_STATE) return 16;
    fake_paging_ready = 1U;
    fake_tss_ready = 1U;
    fake_idt_enable_result = ERR_STATE;
    if (syscall_enable_user_mode() != ERR_STATE) return 17;
    fake_idt_enable_result = OK;
    if (syscall_enable_user_mode() != OK ||
        syscall_enable_user_mode() != OK || !syscall_user_mode_is_enabled()) return 18;
    return 0;
}

static int test_user_validation(void) {
    registers_t regs = make_registers(APP_SYSCALL_SYNC);

    regs.cs = USER_CODE_SELECTOR;
    regs.ss = USER_DATA_SELECTOR;
    fake_process.pid = 1U;
    fake_process.state = PROCESS_STATE_READY;
    if (user_call(&regs) != ERR_STATE) return 20;
    fake_process.context.user_mode = 1U;
    regs.cs = USER_CODE_SELECTOR;
    regs.ss = KERNEL_DATA_SELECTOR;
    if (user_call(&regs) != ERR_INVALID) return 21;
    regs.cs = USER_CODE_SELECTOR;
    regs.ss = USER_DATA_SELECTOR;
    fake_process.state = PROCESS_STATE_ZOMBIE;
    if (user_call(&regs) != ERR_STATE) return 22;
    fake_process.state = PROCESS_STATE_READY;
    fake_app_ready = 0U;
    if (user_call(&regs) != ERR_UNAVAILABLE) return 23;
    fake_app_ready = 1U;
    return 0;
}

static int test_user_syscalls(void) {
    registers_t regs;

    fake_process.pid = 1U;
    fake_process.state = PROCESS_STATE_READY;
    fake_process.context.user_mode = 1U;
    fake_app_ready = 1U;
    regs = make_registers(APP_SYSCALL_CONSOLE_WRITE);
    regs.cs = USER_CODE_SELECTOR;
    regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_TEXT_ADDRESS;
    regs.ecx = 5U;
    if (user_call(&regs) != OK) return 30;
    regs.ecx = APP_API_MAX_TEXT_SIZE + 1U;
    if (user_call(&regs) != ERR_OVERFLOW) return 31;
    regs = make_registers(APP_SYSCALL_UPTIME);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_SELECT_ADDRESS;
    if (user_call(&regs) != OK) return 32;
    regs = make_registers(APP_SYSCALL_MEMORY_INFO);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_SELECT_ADDRESS;
    if (user_call(&regs) != OK) return 33;
    regs = make_registers(APP_SYSCALL_FILE_OPEN);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_PATH_ADDRESS; regs.ecx = APP_FILE_MODE_READ;
    regs.edx = USER_COUNT_ADDRESS;
    int file_open_result = user_call(&regs);
    if (file_open_result != OK) {
        return 34;
    }
    regs = make_registers(APP_SYSCALL_CHDIR);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_PATH_ADDRESS;
    if (user_call(&regs) != OK) return 35;
    regs = make_registers(APP_SYSCALL_GETCWD);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_PATH_ADDRESS; regs.ecx = FS_MAX_PATH;
    if (user_call(&regs) != OK || user_path[0] != '/') return 36;
    regs.ecx = 0U;
    if (user_call(&regs) != ERR_OVERFLOW) return 37;
    regs = make_registers(APP_SYSCALL_FILE_READ);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 17U; regs.ecx = USER_BUFFER_ADDRESS; regs.edx = 4U;
    regs.esi = USER_COUNT_ADDRESS;
    if (user_call(&regs) != OK || user_count != 3U) return 38;
    regs.edx = 0U;
    if (user_call(&regs) != ERR_INVALID) return 39;
    regs = make_registers(APP_SYSCALL_FILE_WRITE);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 17U; regs.ecx = USER_BUFFER_ADDRESS; regs.edx = 4U;
    regs.esi = USER_COUNT_ADDRESS;
    if (user_call(&regs) != OK || user_count != 4U) return 40;
    regs = make_registers(APP_SYSCALL_POLL);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_POLL_ADDRESS; regs.ecx = 1U; regs.edx = 0U;
    regs.esi = USER_COUNT_ADDRESS;
    if (user_call(&regs) != OK || user_count != 1U) return 41;
    regs = make_registers(APP_SYSCALL_SELECT);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_SELECT_ADDRESS;
    user_select.set_mask = SELECT_SET_READ;
    if (user_call(&regs) != OK || user_select.ready_count != 1U) return 42;
    regs = make_registers(APP_SYSCALL_FILE_CLOSE);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR; regs.ebx = 17U;
    if (user_call(&regs) != OK) return 43;
    regs = make_registers(APP_SYSCALL_FILE_LSEEK);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 17U; regs.esi = USER_COUNT_ADDRESS;
    if (user_call(&regs) != OK || user_count != 4U) return 44;
    regs = make_registers(APP_SYSCALL_FSYNC);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR; regs.ebx = 17U;
    if (user_call(&regs) != OK) return 45;
    regs = make_registers(APP_SYSCALL_SYNC);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    if (user_call(&regs) != OK) return 46;
    regs = make_registers(APP_SYSCALL_FILE_IOCTL);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 17U; regs.ecx = APP_IOCTL_SPEAKER_BEEP;
    regs.edx = USER_TONE_ADDRESS;
    if (user_call(&regs) != OK) return 47;
    regs.ecx = APP_IOCTL_SPEAKER_STOP; regs.edx = 0U;
    if (user_call(&regs) != OK) return 48;
    regs.ecx = 99U;
    if (user_call(&regs) != ERR_INVALID) return 49;
    regs = make_registers(APP_SYSCALL_PIPE);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_PIPE_ADDRESS;
    if (user_call(&regs) != OK || user_pipe[0] != 20U) return 50;
    regs = make_registers(APP_SYSCALL_MESSAGE_SEND);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 1U; regs.ecx = USER_MESSAGE_ADDRESS;
    user_message.type = APP_MESSAGE_KEYBOARD;
    if (user_call(&regs) != OK) return 51;
    regs = make_registers(APP_SYSCALL_MESSAGE_RECEIVE);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = USER_MESSAGE_ADDRESS;
    if (user_call(&regs) != OK || user_message.data1 != 0x1CU) return 52;
    regs = make_registers(APP_SYSCALL_MMAP);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 4096U; regs.ecx = APP_MMAP_PROT_READ;
    regs.edx = APP_MMAP_FLAG_ANONYMOUS; regs.esi = USER_MMAP_ADDRESS;
    if (user_call(&regs) != OK || user_mmap_result != 0x40000000U) return 53;
    regs = make_registers(APP_SYSCALL_MUNMAP);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = 0x40000000U; regs.ecx = 4096U;
    if (user_call(&regs) != OK) return 54;
    regs = make_registers(APP_SYSCALL_SIGNAL_ACTION);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = APP_SIGNAL_TERM; regs.ecx = USER_ACTION_ADDRESS;
    regs.edx = USER_OLD_ACTION_ADDRESS;
    if (user_call(&regs) != OK) return 55;
    regs = make_registers(APP_SYSCALL_SIGNAL_MASK);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = APP_SIGNAL_MASK_BLOCK; regs.ecx = APP_SIGNAL_BIT(APP_SIGNAL_INT);
    regs.edx = USER_OLD_MASK_ADDRESS;
    if (user_call(&regs) != OK) return 56;
    regs = make_registers(APP_SYSCALL_SIGNAL_RAISE);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = APP_SIGNAL_INT;
    if (user_call(&regs) != OK) return 57;
    regs = make_registers(APP_SYSCALL_SIGNAL_RETURN);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    if (user_call(&regs) != APP_SYSCALL_SIGNAL_RETURN) return 58;
    regs = make_registers(APP_SYSCALL_PROCESS_EXIT);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    regs.ebx = APP_EXIT_SUCCESS;
    if (user_call(&regs) != OK) return 59;
    regs = make_registers(APP_SYSCALL_INVALID);
    regs.cs = USER_CODE_SELECTOR; regs.ss = USER_DATA_SELECTOR;
    if (user_call(&regs) != ERR_INVALID) return 60;
    return 0;
}

static int test_kernel_syscalls(void) {
    uint32_t numbers[] = {
        APP_SYSCALL_CONSOLE_WRITE, APP_SYSCALL_UPTIME,
        APP_SYSCALL_MEMORY_INFO, APP_SYSCALL_FILE_OPEN,
        APP_SYSCALL_FILE_READ, APP_SYSCALL_FILE_WRITE,
        APP_SYSCALL_FILE_CLOSE, APP_SYSCALL_MESSAGE_SEND,
        APP_SYSCALL_MESSAGE_RECEIVE, APP_SYSCALL_FILE_LSEEK,
        APP_SYSCALL_CHDIR, APP_SYSCALL_GETCWD, APP_SYSCALL_FILE_IOCTL,
        APP_SYSCALL_PIPE, APP_SYSCALL_FSYNC, APP_SYSCALL_SYNC,
        APP_SYSCALL_POLL, APP_SYSCALL_MMAP, APP_SYSCALL_MUNMAP,
        APP_SYSCALL_SIGNAL_ACTION, APP_SYSCALL_SIGNAL_MASK,
        APP_SYSCALL_SIGNAL_RAISE, APP_SYSCALL_SIGNAL_RETURN,
        APP_SYSCALL_INVALID
    };

    fake_process.pid = 1U;
    fake_process.state = PROCESS_STATE_RUNNING;
    fake_app_ready = 1U;
    for (uint32_t index = 0U; index < sizeof(numbers) / sizeof(numbers[0]); index++) {
        uint32_t arg1 = USER_TEXT_ADDRESS;
        uint32_t arg2 = 1U;
        uint32_t arg3 = USER_BUFFER_ADDRESS;
        uint32_t arg4 = USER_COUNT_ADDRESS;
        if (numbers[index] == APP_SYSCALL_FILE_CLOSE ||
            numbers[index] == APP_SYSCALL_FILE_READ ||
            numbers[index] == APP_SYSCALL_FILE_WRITE ||
            numbers[index] == APP_SYSCALL_FILE_LSEEK ||
            numbers[index] == APP_SYSCALL_FSYNC ||
            numbers[index] == APP_SYSCALL_FILE_IOCTL) {
            arg1 = 17U;
        }
        if (numbers[index] == APP_SYSCALL_FSYNC) {
            arg2 = 0U;
            arg3 = 0U;
            arg4 = 0U;
        }
        if (numbers[index] == APP_SYSCALL_SYNC) {
            arg1 = 0U;
            arg2 = 0U;
            arg3 = 0U;
            arg4 = 0U;
        }
        int result = syscall_invoke_kernel(numbers[index], arg1,
                                           arg2, arg3, arg4, 0U);
        if (numbers[index] == APP_SYSCALL_MMAP ||
            numbers[index] == APP_SYSCALL_MUNMAP ||
            (numbers[index] >= APP_SYSCALL_SIGNAL_ACTION &&
             numbers[index] <= APP_SYSCALL_SIGNAL_RETURN) ||
            numbers[index] == APP_SYSCALL_PROCESS_EXIT) {
            if (result != ERR_UNAVAILABLE && numbers[index] != APP_SYSCALL_INVALID) {
                return 70;
            }
        } else if (numbers[index] == APP_SYSCALL_INVALID) {
            if (result != ERR_INVALID) return 71;
        } else if (result == ERR_INVALID && numbers[index] != APP_SYSCALL_POLL) {
            return 72;
        }
    }
    if (syscall_invoke_kernel(APP_SYSCALL_POLL, USER_POLL_ADDRESS, 0U, 0U,
                               USER_COUNT_ADDRESS, 1U) != ERR_INVALID) return 73;
    return 0;
}

int main(void) {
    int result;

    reset_fixture();
    coverage_active = 1U;
    result = test_lifecycle();
    if (!result) result = test_user_validation();
    if (!result) result = test_user_syscalls();
    if (!result) result = test_kernel_syscalls();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
