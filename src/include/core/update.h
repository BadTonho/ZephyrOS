#ifndef UPDATE_H
#define UPDATE_H

#include "types.h"
#include "core/errors.h"

#define ZUPD_HEADER_SIZE 128U
#define ZUPD_ENTRY_SIZE 128U
#define ZUPD_MAX_ENTRIES 16U
#define ZUPD_MAX_TOTAL_SIZE (128U * 1024U)
#define ZUPD_MAX_PAYLOAD_SIZE (64U * 1024U)
#define ZUPD_PATH_SIZE 64U
#define UPDATE_HISTORY_MAX_ENTRIES 8U
#define UPDATE_HISTORY_PACKAGE_SIZE 13U

typedef enum {
    ZUPD_REASON_NONE = 0,
    ZUPD_REASON_FORMAT = 1,
    ZUPD_REASON_SIZE = 2,
    ZUPD_REASON_HASH = 3,
    ZUPD_REASON_UNKNOWN_KEY = 4,
    ZUPD_REASON_SIGNATURE = 5,
    ZUPD_REASON_ARCHITECTURE = 6,
    ZUPD_REASON_BASE_VERSION = 7,
    ZUPD_REASON_DOWNGRADE = 8,
    ZUPD_REASON_PATH_POLICY = 9,
    ZUPD_REASON_DUPLICATE_TARGET = 10,
    ZUPD_REASON_UNSUPPORTED = 11
} zupd_reason_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} update_version_t;

typedef struct {
    zupd_reason_t reason;
    update_version_t base_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
    uint32_t total_size;
    uint16_t entry_count;
} update_verification_t;

typedef struct {
    uint8_t verifier_ready;
    uint8_t local_file_available;
    uint8_t apply_available;
    uint8_t rollback_available;
    uint8_t remote_available;
    uint8_t persistent_state_ready;
    uint8_t recovery_pending;
    uint8_t history_available;
} update_capabilities_t;

typedef enum {
    UPDATE_ACTION_NONE = 0,
    UPDATE_ACTION_VERIFY = 1,
    UPDATE_ACTION_UNSUPPORTED_FS = 2,
    UPDATE_ACTION_STATE = 3,
    UPDATE_ACTION_SPACE = 4,
    UPDATE_ACTION_IO = 5,
    UPDATE_ACTION_CANCELLED = 6,
    UPDATE_ACTION_NO_ROLLBACK = 7,
    UPDATE_ACTION_RECOVERY_PENDING = 8
} update_action_reason_t;

typedef int (*update_cancel_check_t)(void* context);

typedef struct {
    uint8_t dry_run;
    update_cancel_check_t cancel_check;
    void* cancel_context;
} update_action_options_t;

typedef struct {
    update_action_reason_t reason;
    zupd_reason_t verification_reason;
    update_version_t from_version;
    update_version_t to_version;
    uint32_t from_epoch;
    uint32_t to_epoch;
    uint16_t entry_count;
    uint16_t completed_entries;
    uint8_t reboot_required;
    uint8_t recovery_pending;
} update_action_result_t;

typedef enum {
    UPDATE_STORE_UNAVAILABLE = 0,
    UPDATE_STORE_EMPTY = 1,
    UPDATE_STORE_VALID = 2,
    UPDATE_STORE_INVALID = 3
} update_store_state_t;

typedef enum {
    UPDATE_HISTORY_OPERATION_NONE = 0,
    UPDATE_HISTORY_OPERATION_APPLY = 1,
    UPDATE_HISTORY_OPERATION_ROLLBACK = 2,
    UPDATE_HISTORY_OPERATION_RECOVERY_APPLY = 3,
    UPDATE_HISTORY_OPERATION_RECOVERY_ROLLBACK = 4
} update_history_operation_t;

typedef enum {
    UPDATE_HISTORY_OUTCOME_NONE = 0,
    UPDATE_HISTORY_OUTCOME_SUCCESS = 1,
    UPDATE_HISTORY_OUTCOME_FAILED = 2,
    UPDATE_HISTORY_OUTCOME_CANCELLED = 3,
    UPDATE_HISTORY_OUTCOME_RECOVERED = 4
} update_history_outcome_t;

typedef struct {
    uint32_t sequence;
    update_history_operation_t operation;
    update_history_outcome_t outcome;
    update_action_reason_t action_reason;
    zupd_reason_t verification_reason;
    update_version_t from_version;
    update_version_t to_version;
    uint32_t from_epoch;
    uint32_t to_epoch;
    uint16_t entry_count;
    uint16_t completed_entries;
    uint8_t reboot_required;
    char package_alias[UPDATE_HISTORY_PACKAGE_SIZE];
} update_history_entry_t;

typedef struct {
    update_store_state_t state_store;
    update_store_state_t current_files;
    update_store_state_t history_store;
    update_version_t build_version;
    update_version_t installed_version;
    update_version_t rollback_version;
    uint32_t build_epoch;
    uint32_t installed_epoch;
    uint32_t rollback_epoch;
    uint16_t rollback_entry_count;
    uint8_t transaction_pending;
    uint8_t has_last_event;
    update_capabilities_t capabilities;
    update_history_entry_t last_event;
} update_status_t;

int update_init(void);
int update_is_ready(void);
int update_verify_file(const char* path, update_verification_t* result_out);
int update_get_capabilities(update_capabilities_t* capabilities_out);
int update_get_installed_version(update_version_t* version_out,
                                 uint32_t* epoch_out);
int update_get_status(update_status_t* status_out);
int update_get_history_count(uint32_t* count_out);
int update_get_history_entry(uint32_t newest_index,
                             update_history_entry_t* entry_out);
int update_apply_file(const char* path,
                      const update_action_options_t* options,
                      update_action_result_t* result_out);
int update_rollback(const update_action_options_t* options,
                    update_action_result_t* result_out);
int update_test_fail_after(uint16_t completed_entries);
const char* zupd_reason_name(zupd_reason_t reason);
const char* update_action_reason_name(update_action_reason_t reason);
const char* update_store_state_name(update_store_state_t state);
const char* update_history_operation_name(update_history_operation_t operation);
const char* update_history_outcome_name(update_history_outcome_t outcome);

#endif
