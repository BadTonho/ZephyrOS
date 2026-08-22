#ifndef UPDATE_RUNTIME_H
#define UPDATE_RUNTIME_H

#include "types.h"
#include "core/errors.h"
#include "core/update.h"

#define UPDATE_RUNTIME_FORMAT_VERSION 2U
#define UPDATE_RUNTIME_ARCH_I386 1U
#define UPDATE_RUNTIME_MANIFEST_SIZE 4096U
#define UPDATE_RUNTIME_MANIFEST_SIGNED_SIZE 4032U
#define UPDATE_RUNTIME_MANIFEST_HEADER_SIZE 256U
#define UPDATE_RUNTIME_MANIFEST_BASE_OFFSET 256U
#define UPDATE_RUNTIME_MANIFEST_BASE_SIZE 16U
#define UPDATE_RUNTIME_MANIFEST_MAX_BASES 8U
#define UPDATE_RUNTIME_MANIFEST_ENTRY_OFFSET 384U
#define UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE 224U
#define UPDATE_RUNTIME_MAX_ENTRIES 16U
#define UPDATE_RUNTIME_PACKAGE_HEADER_SIZE 128U
#define UPDATE_RUNTIME_PACKAGE_ENTRY_SIZE 128U
#define UPDATE_RUNTIME_PACKAGE_MAX_SIZE (128U * 1024U)
#define UPDATE_RUNTIME_FILE_MAX_SIZE (64U * 1024U)
#define UPDATE_RUNTIME_PATH_SIZE 64U
#define UPDATE_RUNTIME_ASSET_NAME_SIZE 64U
#define UPDATE_RUNTIME_RELEASE_TAG_SIZE 65U
#define UPDATE_RUNTIME_RELEASE_ID_SIZE 65U
#define UPDATE_RUNTIME_SIGNATURE_SIZE 64U
#define UPDATE_RUNTIME_KEY_ID_SIZE 16U
#define UPDATE_RUNTIME_CACHE_ALIAS_SIZE 13U
#define UPDATE_RUNTIME_CACHE_RECORD_SIZE 512U
#define UPDATE_RUNTIME_IO_BUFFER_SIZE (64U * 1024U)

#define UPDATE_RUNTIME_OPERATION_REPLACE 0x01U
#define UPDATE_RUNTIME_OPERATION_CREATE  0x02U
#define UPDATE_RUNTIME_OPERATION_DELETE  0x04U

typedef enum {
    UPDATE_RUNTIME_REASON_NONE = 0,
    UPDATE_RUNTIME_REASON_FORMAT,
    UPDATE_RUNTIME_REASON_SIZE,
    UPDATE_RUNTIME_REASON_HASH,
    UPDATE_RUNTIME_REASON_UNKNOWN_KEY,
    UPDATE_RUNTIME_REASON_SIGNATURE,
    UPDATE_RUNTIME_REASON_ARCHITECTURE,
    UPDATE_RUNTIME_REASON_BASE_VERSION,
    UPDATE_RUNTIME_REASON_DOWNGRADE,
    UPDATE_RUNTIME_REASON_PATH_POLICY,
    UPDATE_RUNTIME_REASON_OPERATION,
    UPDATE_RUNTIME_REASON_DUPLICATE_TARGET,
    UPDATE_RUNTIME_REASON_STATE,
    UPDATE_RUNTIME_REASON_JOURNAL,
    UPDATE_RUNTIME_REASON_SPACE,
    UPDATE_RUNTIME_REASON_IO,
    UPDATE_RUNTIME_REASON_CANCELLED,
    UPDATE_RUNTIME_REASON_CACHE,
    UPDATE_RUNTIME_REASON_PACKAGE_MISMATCH
} update_runtime_reason_t;

typedef struct {
    char path[UPDATE_RUNTIME_PATH_SIZE];
    uint8_t target_present;
    uint8_t allowed_operations;
    uint16_t flags;
    uint32_t target_size;
    uint8_t target_hash[32];
    char asset_name[UPDATE_RUNTIME_ASSET_NAME_SIZE];
    uint32_t asset_size;
    uint8_t asset_hash[32];
} update_runtime_entry_t;

typedef struct {
    uint32_t generation;
    update_version_t target_version;
    uint32_t target_epoch;
    uint16_t base_count;
    update_version_t base_versions[UPDATE_RUNTIME_MANIFEST_MAX_BASES];
    uint32_t base_epochs[UPDATE_RUNTIME_MANIFEST_MAX_BASES];
    uint16_t entry_count;
    uint32_t package_size;
    uint8_t package_hash[32];
    char release_tag[UPDATE_RUNTIME_RELEASE_TAG_SIZE];
    char release_id[UPDATE_RUNTIME_RELEASE_ID_SIZE];
    update_runtime_entry_t entries[UPDATE_RUNTIME_MAX_ENTRIES];
} update_runtime_manifest_t;

typedef struct {
    update_runtime_reason_t reason;
    update_version_t target_version;
    uint32_t target_epoch;
    uint32_t package_size;
    uint16_t entry_count;
    uint16_t reused_entries;
    uint16_t missing_assets;
    uint16_t changed_entries;
    uint8_t manifest_valid;
    uint8_t package_valid;
    uint8_t compatible;
} update_runtime_verification_t;

typedef struct {
    uint8_t verifier_ready;
    uint8_t local_file_available;
    uint8_t apply_available;
    uint8_t rollback_available;
    uint8_t persistent_state_ready;
    uint8_t recovery_pending;
    uint8_t cache_available;
    uint8_t selective_ready;
} update_runtime_capabilities_t;

typedef struct {
    update_runtime_reason_t reason;
    update_version_t installed_version;
    update_version_t rollback_version;
    uint32_t installed_epoch;
    uint32_t rollback_epoch;
    uint16_t entry_count;
    uint16_t rollback_entry_count;
    uint16_t reused_entries;
    uint16_t missing_assets;
    uint8_t transaction_pending;
    uint8_t rollback_available;
    uint8_t state_valid;
    uint8_t recovery_pending;
    update_runtime_capabilities_t capabilities;
} update_runtime_status_t;

typedef struct {
    uint8_t dry_run;
    update_cancel_check_t cancel_check;
    void* cancel_context;
} update_runtime_action_options_t;

typedef struct {
    update_runtime_reason_t reason;
    update_version_t from_version;
    update_version_t to_version;
    uint32_t from_epoch;
    uint32_t to_epoch;
    uint16_t entry_count;
    uint16_t completed_entries;
    uint8_t reboot_required;
    uint8_t recovery_pending;
} update_runtime_action_result_t;

typedef struct {
    uint8_t valid;
    uint8_t selective;
    uint8_t full_package;
    uint8_t active_slot;
    uint16_t entry_count;
    uint16_t missing_assets;
    char manifest_alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    char package_alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    char asset_aliases[UPDATE_RUNTIME_MAX_ENTRIES][UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    update_runtime_manifest_t manifest;
} update_runtime_cache_t;

int update_runtime_init(void);
int update_runtime_is_ready(void);
int update_runtime_parse_manifest(const uint8_t* raw, uint32_t size,
                                  update_runtime_manifest_t* output,
                                  update_runtime_reason_t* reason_out);
int update_runtime_verify_file(const char* path,
                               update_runtime_verification_t* result_out);
int update_runtime_verify_file_for_manifest(
    const char* path, const update_runtime_manifest_t* manifest,
    update_runtime_verification_t* result_out);
int update_runtime_get_capabilities(
    update_runtime_capabilities_t* capabilities_out);
int update_runtime_get_status(update_runtime_status_t* status_out);
int update_runtime_get_installed_version(update_version_t* version_out,
                                         uint32_t* epoch_out);
int update_runtime_file_matches(const char* path, uint32_t expected_size,
                                const uint8_t expected_hash[32],
                                uint8_t* matches_out);
int update_runtime_apply_file(
    const char* path, const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out);
int update_runtime_apply_cached(
    const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out);
int update_runtime_rollback(
    const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out);
int update_runtime_get_cache(update_runtime_cache_t* cache_out);
int update_runtime_test_fail_after(uint16_t completed_entries);
const char* update_runtime_reason_name(update_runtime_reason_t reason);

#endif
