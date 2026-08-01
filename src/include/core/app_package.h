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
#define APP_PACKAGE_MAX_PLAN_ENTRIES        16U
#define APP_PACKAGE_HISTORY_MAX_ENTRIES     16U
#define APP_PACKAGE_MAX_ROLLBACKS           32U
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
    APP_PACKAGE_ACTION_REASON_WRITE_ERROR = 16,
    APP_PACKAGE_ACTION_REASON_UPDATE_NOT_AVAILABLE = 17,
    APP_PACKAGE_ACTION_REASON_DOWNGRADE_REQUIRES_CONFIRM = 18,
    APP_PACKAGE_ACTION_REASON_PLAN_INCOMPLETE = 19,
    APP_PACKAGE_ACTION_REASON_PLAN_CYCLE = 20,
    APP_PACKAGE_ACTION_REASON_PLAN_CONFLICT = 21,
    APP_PACKAGE_ACTION_REASON_TRANSACTION_UNAVAILABLE = 22,
    APP_PACKAGE_ACTION_REASON_TRANSACTION_PENDING = 23,
    APP_PACKAGE_ACTION_REASON_ROLLBACK_UNAVAILABLE = 24,
    APP_PACKAGE_ACTION_REASON_RECOVERY_FAILED = 25,
    APP_PACKAGE_ACTION_REASON_HISTORY_UNAVAILABLE = 26
} app_package_action_reason_t;

typedef enum {
    APP_PACKAGE_PLAN_ACTION_NONE = 0,
    APP_PACKAGE_PLAN_ACTION_INSTALL,
    APP_PACKAGE_PLAN_ACTION_UPDATE
} app_package_plan_action_t;

typedef struct {
    char id[APP_PACKAGE_ID_SIZE];
    char alias[13];
    char from_version[APP_PACKAGE_VERSION_TEXT_SIZE];
    char to_version[APP_PACKAGE_VERSION_TEXT_SIZE];
    app_package_plan_action_t action;
} app_package_plan_entry_t;

typedef struct {
    app_package_plan_entry_t entries[APP_PACKAGE_MAX_PLAN_ENTRIES];
    uint32_t entry_count;
    uint32_t target_index;
    app_package_action_reason_t reason;
    uint8_t allow_downgrade;
} app_package_plan_t;

typedef enum {
    APP_PACKAGE_HISTORY_OPERATION_NONE = 0,
    APP_PACKAGE_HISTORY_OPERATION_INSTALL,
    APP_PACKAGE_HISTORY_OPERATION_REMOVE,
    APP_PACKAGE_HISTORY_OPERATION_UPDATE,
    APP_PACKAGE_HISTORY_OPERATION_ROLLBACK,
    APP_PACKAGE_HISTORY_OPERATION_RECOVERY
} app_package_history_operation_t;

typedef enum {
    APP_PACKAGE_HISTORY_OUTCOME_NONE = 0,
    APP_PACKAGE_HISTORY_OUTCOME_SUCCESS,
    APP_PACKAGE_HISTORY_OUTCOME_FAILED,
    APP_PACKAGE_HISTORY_OUTCOME_RECOVERED
} app_package_history_outcome_t;

typedef struct {
    uint32_t sequence;
    app_package_history_operation_t operation;
    app_package_history_outcome_t outcome;
    app_package_action_reason_t reason;
    char id[APP_PACKAGE_ID_SIZE];
    char from_version[APP_PACKAGE_VERSION_TEXT_SIZE];
    char to_version[APP_PACKAGE_VERSION_TEXT_SIZE];
    uint32_t plan_entry_count;
} app_package_history_entry_t;

typedef struct {
    char id[APP_PACKAGE_ID_SIZE];
    char version[APP_PACKAGE_VERSION_TEXT_SIZE];
} app_package_rollback_entry_t;

typedef struct {
    uint8_t transaction_supported;
    uint8_t transaction_pending;
    uint8_t rollback_available;
    uint8_t history_available;
    char rollback_id[APP_PACKAGE_ID_SIZE];
    char rollback_version[APP_PACKAGE_VERSION_TEXT_SIZE];
    app_package_history_entry_t last_history;
    uint32_t rollback_count;
    app_package_rollback_entry_t
        rollbacks[APP_PACKAGE_MAX_ROLLBACKS];
} app_package_status_t;

typedef struct {
    int invalid_package;
    int missing_dependency;
    int insufficient_space;
    int mutation_serialization;
    int transaction_supported;
    int transaction_pending;
    int rollback_available;
    int history_available;
} app_package_diagnostic_t;

typedef struct {
    app_package_action_reason_t reason;
    app_package_info_t info;
    char blocker_ids[APP_PACKAGE_MAX_ACTION_BLOCKERS][APP_PACKAGE_ID_SIZE];
    uint32_t blocker_count;
    uint32_t blocker_overflow;
    uint32_t required_clusters;
    uint32_t free_clusters;
    app_package_plan_t plan;
} app_package_action_result_t;

int app_package_init(void);
int app_package_is_ready(void);
int app_package_compare_versions(const char* left, const char* right,
                                 int* comparison_out);
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
int app_package_preflight_plan(const app_package_plan_t* plan,
                               app_package_action_result_t* result_out);
int app_package_apply_plan_confirmed(const app_package_plan_t* plan,
                                     app_package_action_result_t* result_out);
int app_package_preflight_rollback(const char* id,
                                   app_package_action_result_t* result_out);
int app_package_rollback_confirmed(const char* id,
                                   app_package_action_result_t* result_out);
int app_package_get_status(app_package_status_t* status_out);
int app_package_get_history_count(uint32_t* count_out);
int app_package_get_history_entry(uint32_t newest_index,
                                  app_package_history_entry_t* entry_out);
int app_package_test_fail_after(uint16_t completed_files);
int app_package_run_installed(const char* id,
                              const app_launch_info_t* launch,
                              uint32_t* pid_out,
                              app_package_action_result_t* result_out);
int app_package_is_mutation_active(void);
int app_package_run_diagnostics(app_package_diagnostic_t* diagnostic_out);
const char* app_package_action_reason_name(
    app_package_action_reason_t reason);
const char* app_package_plan_action_name(app_package_plan_action_t action);
const char* app_package_history_operation_name(
    app_package_history_operation_t operation);
const char* app_package_history_outcome_name(
    app_package_history_outcome_t outcome);

#endif
