#ifndef APP_API_H
#define APP_API_H

#include "types.h"

#define APP_API_VERSION_MAJOR 0
#define APP_API_VERSION_MINOR 6
#define APP_API_MAX_TEXT_SIZE 1024
#define APP_API_MAX_FILE_IO_SIZE 4096
#define APP_API_TICKS_PER_SECOND 50
#define APP_FD_STDIN  0U
#define APP_FD_STDOUT 1U
#define APP_FD_STDERR 2U
#define APP_FD_INVALID 0xFFFFFFFFU
#define APP_HANDLE_INVALID APP_FD_INVALID
#define APP_EXIT_SUCCESS 0U
/* Reservado ao runtime para cancelamentos controlados, como F11 e F12. */
#define APP_EXIT_CANCELLED 0x0000F120U
#define APP_EXIT_SIGNAL_BASE 0x00010000U
#define APP_EXIT_FROM_SIGNAL(signal_number) \
    (APP_EXIT_SIGNAL_BASE | ((signal_number) & 0xFFU))

#define APP_SIGNAL_INT  2U
#define APP_SIGNAL_KILL 9U
#define APP_SIGNAL_SEGV 11U
#define APP_SIGNAL_TERM 15U
#define APP_SIGNAL_CHLD 17U

#define APP_SIGNAL_DISPOSITION_DEFAULT 0U
#define APP_SIGNAL_DISPOSITION_IGNORE  1U
#define APP_SIGNAL_DISPOSITION_HANDLER 2U

#define APP_SIGNAL_MASK_BLOCK   0U
#define APP_SIGNAL_MASK_UNBLOCK 1U
#define APP_SIGNAL_MASK_SET     2U

#define APP_SIGNAL_BIT(signal_number) \
    (1U << ((signal_number) - 1U))
#define APP_SIGNAL_SUPPORTED_MASK \
    (APP_SIGNAL_BIT(APP_SIGNAL_INT) | APP_SIGNAL_BIT(APP_SIGNAL_KILL) | \
     APP_SIGNAL_BIT(APP_SIGNAL_SEGV) | APP_SIGNAL_BIT(APP_SIGNAL_TERM) | \
     APP_SIGNAL_BIT(APP_SIGNAL_CHLD))
#define APP_SIGNAL_UNBLOCKABLE_MASK \
    (APP_SIGNAL_BIT(APP_SIGNAL_KILL) | APP_SIGNAL_BIT(APP_SIGNAL_SEGV))

#define APP_LAUNCH_ABI_VERSION 1U
#define APP_LAUNCH_MAX_ARGS    8U
#define APP_LAUNCH_MAX_TEXT    512U
#define APP_LAUNCH_MAX_RAW_LENGTH (APP_LAUNCH_MAX_TEXT - 1U)

#define APP_FILE_MODE_READ       1
#define APP_FILE_MODE_WRITE      2
#define APP_FILE_MODE_READ_WRITE 3

#define APP_SEEK_SET 0U
#define APP_SEEK_CUR 1U
#define APP_SEEK_END 2U
#define APP_FILE_SEEK_SET APP_SEEK_SET
#define APP_FILE_SEEK_CUR APP_SEEK_CUR
#define APP_FILE_SEEK_END APP_SEEK_END

#define APP_MESSAGE_KEYBOARD    1 /* data1 contem scancode PS/2 bruto */
#define APP_MESSAGE_APP_REQUEST 2

typedef uint32_t app_handle_t;

typedef struct {
    uint32_t major;
    uint32_t minor;
} app_api_version_t;

typedef struct {
    uint32_t ticks;
    uint32_t seconds;
} app_uptime_info_t;

typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t total_pages;
    uint32_t free_pages;
} app_memory_info_t;

typedef struct {
    uint32_t type;
    uint32_t data1;
    uint32_t data2;
} app_message_t;

typedef struct {
    uint32_t disposition;
    uint32_t handler;
    uint32_t mask;
} app_signal_action_t;

typedef struct __attribute__((packed)) {
    uint32_t offset;
    uint32_t length;
} app_launch_arg_t;

/* Offsets, nunca ponteiros do kernel, mantem a ABI independente do loader. */
typedef struct __attribute__((packed)) {
    uint32_t abi_version;
    uint32_t argc;
    uint32_t raw_length;
    app_launch_arg_t args[APP_LAUNCH_MAX_ARGS];
    char raw_args[APP_LAUNCH_MAX_TEXT];
} app_launch_info_t;

#define APP_LAUNCH_RAW_LENGTH_OFFSET ((uint32_t)(sizeof(uint32_t) * 2U))
#define APP_LAUNCH_RAW_ARGS_OFFSET \
    ((uint32_t)(sizeof(uint32_t) * 3U + \
    sizeof(app_launch_arg_t) * APP_LAUNCH_MAX_ARGS))

int app_api_init(void);
int app_api_is_ready(void);
int app_api_get_version(app_api_version_t* version);
/* Escrita sincrona; blocos consecutivos nao formam uma operacao atomica. */
int app_api_console_write(const char* text, uint32_t size);
int app_api_get_uptime(app_uptime_info_t* info);
int app_api_get_memory_info(app_memory_info_t* info);
int app_api_file_open(const char* path, uint32_t mode, app_handle_t* handle);
int app_api_file_read(app_handle_t handle, uint8_t* buffer,
                      uint32_t size, uint32_t* bytes_read);
int app_api_file_write(app_handle_t handle, const uint8_t* buffer,
                       uint32_t size, uint32_t* bytes_written);
int app_api_file_close(app_handle_t handle);
int app_api_file_lseek(app_handle_t handle, int32_t offset, uint32_t whence,
                       uint32_t* position);
int app_api_chdir(const char* path);
int app_api_getcwd(char* path, uint32_t capacity);
int app_api_message_send(uint32_t pid, const app_message_t* message);
int app_api_message_receive(app_message_t* message);
int app_api_file_is_ready(void);
int app_api_ipc_is_ready(void);

#endif
