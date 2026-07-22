#ifndef APP_LOADER_H
#define APP_LOADER_H

#include "types.h"

#define APP_IMAGE_VERSION              1U
#define APP_IMAGE_ARCH_I386            1U
#define APP_IMAGE_MAX_CODE_SIZE        4096U
#define APP_IMAGE_MAX_DATA_SIZE        4096U
#define APP_IMAGE_STACK_SIZE           4096U
#define APP_IMAGE_FLAGS_NONE           0U

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
int app_loader_reap_finished(void);

#endif
