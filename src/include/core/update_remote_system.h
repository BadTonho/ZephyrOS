#ifndef UPDATE_REMOTE_SYSTEM_H
#define UPDATE_REMOTE_SYSTEM_H

#include "types.h"
#include "core/update_remote.h"
#include "core/update_system.h"

#define UPDATE_REMOTE_SYSTEM_CACHE_COUNT 2U
#define UPDATE_REMOTE_SYSTEM_CACHE_NONE 0xFFU
#define UPDATE_REMOTE_SYSTEM_CONTROL_SIZE 512U
#define UPDATE_REMOTE_SYSTEM_CONTROL_HASH_OFFSET 480U
#define UPDATE_REMOTE_SYSTEM_CACHE_A_ALIAS "ZSC0.ZSY"
#define UPDATE_REMOTE_SYSTEM_CACHE_B_ALIAS "ZSC1.ZSY"
#define UPDATE_REMOTE_SYSTEM_STATE_A_ALIAS "ZSC0.STA"
#define UPDATE_REMOTE_SYSTEM_STATE_B_ALIAS "ZSC1.STA"
#define UPDATE_REMOTE_SYSTEM_TEMP_ALIAS "ZSCT.ZSY"

typedef enum {
    UPDATE_REMOTE_SYSTEM_STATE_UNAVAILABLE = 0,
    UPDATE_REMOTE_SYSTEM_STATE_EMPTY,
    UPDATE_REMOTE_SYSTEM_STATE_READY,
    UPDATE_REMOTE_SYSTEM_STATE_DEGRADED,
    UPDATE_REMOTE_SYSTEM_STATE_DOWNLOADING,
    UPDATE_REMOTE_SYSTEM_STATE_VALIDATING,
    UPDATE_REMOTE_SYSTEM_STATE_FAILED
} update_remote_system_state_t;

typedef enum {
    UPDATE_REMOTE_SYSTEM_REASON_NONE = 0,
    UPDATE_REMOTE_SYSTEM_REASON_NETWORK,
    UPDATE_REMOTE_SYSTEM_REASON_RELEASE,
    UPDATE_REMOTE_SYSTEM_REASON_FORMAT,
    UPDATE_REMOTE_SYSTEM_REASON_SIGNATURE,
    UPDATE_REMOTE_SYSTEM_REASON_HASH,
    UPDATE_REMOTE_SYSTEM_REASON_COMPATIBILITY,
    UPDATE_REMOTE_SYSTEM_REASON_VERSION,
    UPDATE_REMOTE_SYSTEM_REASON_SIZE,
    UPDATE_REMOTE_SYSTEM_REASON_SPACE,
    UPDATE_REMOTE_SYSTEM_REASON_CACHE,
    UPDATE_REMOTE_SYSTEM_REASON_IO,
    UPDATE_REMOTE_SYSTEM_REASON_CANCELLED,
    UPDATE_REMOTE_SYSTEM_REASON_STATE
} update_remote_system_reason_t;

typedef struct {
    update_remote_system_state_t state;
    update_remote_system_reason_t reason;
    uint8_t initialized;
    uint8_t volume_ready;
    uint8_t cache_slot;
    uint8_t copies_valid;
    uint8_t package_valid;
    uint8_t busy;
    uint32_t sequence;
    uint32_t package_size;
    uint32_t bytes_received;
    uint32_t total_bytes;
    uint8_t package_hash[32];
    update_version_t version;
    uint32_t epoch;
    char tag[UPDATE_REMOTE_TAG_SIZE];
    char release_id[UPDATE_REMOTE_RELEASE_ID_SIZE];
    char cached_alias[UPDATE_REMOTE_ALIAS_SIZE];
} update_remote_system_status_t;

typedef struct {
    update_remote_system_reason_t reason;
    uint8_t cache_preserved;
    uint8_t cache_published;
    uint8_t cache_cleared;
    uint32_t bytes_received;
    char cached_alias[UPDATE_REMOTE_ALIAS_SIZE];
    update_system_verification_t verification;
} update_remote_system_result_t;

int update_remote_system_init(void);
int update_remote_system_fetch(
    const char* tag, const update_remote_options_t* options,
    update_remote_system_result_t* result_out);
int update_remote_system_clear(
    const update_remote_options_t* options,
    update_remote_system_result_t* result_out);
int update_remote_system_get_status(
    update_remote_system_status_t* status_out);
int update_remote_system_get_cached_path(char* path_out, uint32_t capacity);
const char* update_remote_system_state_name(update_remote_system_state_t state);
const char* update_remote_system_reason_name(update_remote_system_reason_t reason);

#endif
