#ifndef APP_PACKAGE_H
#define APP_PACKAGE_H

#include "types.h"

#define APP_PACKAGE_VERSION                1U
#define APP_PACKAGE_ARCH_I386              1U
#define APP_PACKAGE_MAX_MANIFEST_SIZE      512U
#define APP_PACKAGE_ID_SIZE                9U
#define APP_PACKAGE_NAME_SIZE              32U
#define APP_PACKAGE_VERSION_TEXT_SIZE      16U
#define APP_PACKAGE_MAX_DEPENDENCIES       4U
#define APP_PACKAGE_DIRECTORY              "APPS"
#define APP_PACKAGE_ENTRY_NAME             "APP.ZAP"
#define APP_PACKAGE_METADATA_NAME          "META.DAT"
#define APP_PACKAGE_DIRECTORY_ATTRIBUTE    0x10U

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t version;
    uint16_t header_size;
    uint32_t architecture;
    uint32_t manifest_size;
    uint32_t payload_size;
    uint32_t content_crc32;
    uint32_t flags;
    uint32_t reserved;
} app_package_header_t;

typedef struct {
    char id[APP_PACKAGE_ID_SIZE];
    char name[APP_PACKAGE_NAME_SIZE];
    char version[APP_PACKAGE_VERSION_TEXT_SIZE];
    char dependencies[APP_PACKAGE_MAX_DEPENDENCIES][APP_PACKAGE_ID_SIZE];
    uint32_t dependency_count;
} app_package_info_t;

typedef struct {
    int invalid_package;
    int missing_dependency;
    int insufficient_space;
} app_package_diagnostic_t;

int app_package_init(void);
int app_package_is_ready(void);
int app_package_verify_file(const char* path, app_package_info_t* info_out);
int app_package_install_file(const char* path, app_package_info_t* info_out);
int app_package_remove(const char* id);
int app_package_get_installed_count(void);
int app_package_get_installed_info(int index, app_package_info_t* info_out);
int app_package_get_installed_info_by_id(const char* id,
                                         app_package_info_t* info_out);
int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out);

#endif
