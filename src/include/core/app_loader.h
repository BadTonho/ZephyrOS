#ifndef APP_LOADER_H
#define APP_LOADER_H

#include "types.h"
#include "core/app_api.h"

#define APP_IMAGE_VERSION              1U
#define APP_IMAGE_ARCH_I386            1U
#define APP_IMAGE_MAX_CODE_SIZE        4096U
#define APP_IMAGE_MAX_DATA_SIZE        4096U
#define APP_IMAGE_STACK_SIZE           4096U
#define APP_IMAGE_FLAGS_NONE           0U

typedef struct {
    uint32_t pid;
    uint32_t exit_code;
    uint32_t faulted;
    uint32_t cancelled;
    uint32_t start_failed;
    uint32_t focus_acquired;
} app_loader_result_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint32_t version;
    uint32_t architecture;
    uint32_t header_size;
    uint32_t code_offset;
    uint32_t code_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t entry_offset;
    uint32_t stack_size;
    uint32_t flags;
} app_image_header_t;

#define APP_IMAGE_HEADER_SIZE ((uint32_t)sizeof(app_image_header_t))
#define APP_IMAGE_MAX_FILE_SIZE \
    (APP_IMAGE_HEADER_SIZE + APP_IMAGE_MAX_CODE_SIZE + APP_IMAGE_MAX_DATA_SIZE)

int app_loader_init(void);
int app_loader_is_ready(void);
int app_loader_validate_image(const uint8_t* image, uint32_t size,
                              app_image_header_t* header);
int app_loader_run_file(const char* path, uint32_t* pid_out);
int app_loader_run_file_with_launch(const char* path,
                                    const app_launch_info_t* launch,
                                    uint32_t* pid_out);
int app_loader_run_image(const char* name, const uint8_t* image,
                         uint32_t size, const app_launch_info_t* launch,
                         uint32_t* pid_out);
int app_loader_build_launch_info(const char* text, app_launch_info_t* launch);
int app_loader_reap_finished(void);
int app_loader_is_foreground_active(void);
uint32_t app_loader_get_foreground_pid(void);
int app_loader_cancel_foreground(uint32_t exit_code);
int app_loader_take_finished_result(app_loader_result_t* result);

#endif
