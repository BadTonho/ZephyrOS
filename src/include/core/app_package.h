#ifndef APP_PACKAGE_H
#define APP_PACKAGE_H

#include "types.h"
#include "core/app_api.h"

#define APP_PACKAGE_VERSION                1U
#define APP_PACKAGE_ARCH_I386              1U
#define APP_PACKAGE_MAX_MANIFEST_SIZE      512U
#define APP_PACKAGE_ID_SIZE                9U
#define APP_PACKAGE_NAME_SIZE              32U
#define APP_PACKAGE_VERSION_TEXT_SIZE      16U
#define APP_PACKAGE_MAX_DEPENDENCIES       4U
#define APP_PACKAGE_MAX_ACTION_BLOCKERS    32U
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
    int mutation_serialization;
} app_package_diagnostic_t;

typedef enum {
    APP_PACKAGE_ACTION_REASON_NONE = 0,
    APP_PACKAGE_ACTION_REASON_INVALID_ARGUMENT = 1,
    APP_PACKAGE_ACTION_REASON_SOURCE_NOT_FOUND = 2,
    APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID = 3,
    APP_PACKAGE_ACTION_REASON_ALIAS_MISMATCH = 4,
    APP_PACKAGE_ACTION_REASON_DEPENDENCY_MISSING = 5,
    APP_PACKAGE_ACTION_REASON_INSUFFICIENT_SPACE = 6,
    APP_PACKAGE_ACTION_REASON_ALREADY_INSTALLED = 7,
    APP_PACKAGE_ACTION_REASON_NOT_INSTALLED = 8,
    APP_PACKAGE_ACTION_REASON_DEPENDENT_INSTALLED = 9,
    APP_PACKAGE_ACTION_REASON_FILESYSTEM_UNAVAILABLE = 10,
    APP_PACKAGE_ACTION_REASON_LOADER_UNAVAILABLE = 11,
    APP_PACKAGE_ACTION_REASON_PACKAGE_SERVICE_UNAVAILABLE = 12,
    APP_PACKAGE_ACTION_REASON_LOADER_BUSY = 13,
    APP_PACKAGE_ACTION_REASON_MUTATION_BUSY = 14,
    APP_PACKAGE_ACTION_REASON_READ_ERROR = 15,
    APP_PACKAGE_ACTION_REASON_WRITE_ERROR = 16
} app_package_action_reason_t;

typedef struct {
    app_package_action_reason_t reason;
    app_package_info_t info;
    char blocker_ids[APP_PACKAGE_MAX_ACTION_BLOCKERS][APP_PACKAGE_ID_SIZE];
    uint32_t blocker_count;
    uint32_t blocker_overflow;
    uint32_t required_clusters;
    uint32_t free_clusters;
} app_package_action_result_t;

int app_package_init(void);
int app_package_is_ready(void);
int app_package_verify_file(const char* path, app_package_info_t* info_out);
int app_package_install_file(const char* path, app_package_info_t* info_out);
int app_package_remove(const char* id);
int app_package_get_installed_count(void);
int app_package_get_installed_info(int index, app_package_info_t* info_out);
int app_package_get_installed_info_by_id(const char* id,
                                         app_package_info_t* info_out);
int app_package_preflight_install(const char* alias,
                                  app_package_action_result_t* result_out);
int app_package_install_confirmed(const char* alias,
                                  app_package_action_result_t* result_out);
int app_package_preflight_remove(const char* id,
                                 app_package_action_result_t* result_out);
int app_package_remove_confirmed(const char* id,
                                 app_package_action_result_t* result_out);
int app_package_run_installed(const char* id,
                              const app_launch_info_t* launch,
                              uint32_t* pid_out,
                              app_package_action_result_t* result_out);
int app_package_is_mutation_active(void);
int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out);
const char* app_package_action_reason_name(
    app_package_action_reason_t reason);

#endif
