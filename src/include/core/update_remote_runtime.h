#ifndef UPDATE_REMOTE_RUNTIME_H
#define UPDATE_REMOTE_RUNTIME_H

#include "types.h"
#include "core/update_remote.h"
#include "core/update_runtime.h"

#define UPDATE_REMOTE_RUNTIME_URL_SIZE 512U

typedef enum {
    UPDATE_REMOTE_RUNTIME_FETCH_SELECTIVE = 0,
    UPDATE_REMOTE_RUNTIME_FETCH_FULL = 1
} update_remote_runtime_fetch_mode_t;

typedef enum {
    UPDATE_REMOTE_RUNTIME_STATE_DISABLED = 0,
    UPDATE_REMOTE_RUNTIME_STATE_UNAVAILABLE,
    UPDATE_REMOTE_RUNTIME_STATE_READY,
    UPDATE_REMOTE_RUNTIME_STATE_CHECKING,
    UPDATE_REMOTE_RUNTIME_STATE_AVAILABLE,
    UPDATE_REMOTE_RUNTIME_STATE_DOWNLOADING,
    UPDATE_REMOTE_RUNTIME_STATE_VALIDATING,
    UPDATE_REMOTE_RUNTIME_STATE_FAILED
} update_remote_runtime_state_t;

typedef enum {
    UPDATE_REMOTE_RUNTIME_REASON_NONE = 0,
    UPDATE_REMOTE_RUNTIME_REASON_DISABLED,
    UPDATE_REMOTE_RUNTIME_REASON_NETWORK,
    UPDATE_REMOTE_RUNTIME_REASON_HTTP,
    UPDATE_REMOTE_RUNTIME_REASON_TIMEOUT,
    UPDATE_REMOTE_RUNTIME_REASON_DNS,
    UPDATE_REMOTE_RUNTIME_REASON_TLS,
    UPDATE_REMOTE_RUNTIME_REASON_RELEASE_NOT_FOUND,
    UPDATE_REMOTE_RUNTIME_REASON_RELEASE_FORMAT,
    UPDATE_REMOTE_RUNTIME_REASON_RELEASE_TAG,
    UPDATE_REMOTE_RUNTIME_REASON_RELEASE_ASSET,
    UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_FORMAT,
    UPDATE_REMOTE_RUNTIME_REASON_MANIFEST_SIGNATURE,
    UPDATE_REMOTE_RUNTIME_REASON_UNKNOWN_KEY,
    UPDATE_REMOTE_RUNTIME_REASON_VERSION,
    UPDATE_REMOTE_RUNTIME_REASON_SIZE,
    UPDATE_REMOTE_RUNTIME_REASON_ASSET_HASH,
    UPDATE_REMOTE_RUNTIME_REASON_PACKAGE_HASH,
    UPDATE_REMOTE_RUNTIME_REASON_PACKAGE_VERIFY,
    UPDATE_REMOTE_RUNTIME_REASON_CACHE,
    UPDATE_REMOTE_RUNTIME_REASON_IO,
    UPDATE_REMOTE_RUNTIME_REASON_CANCELLED,
    UPDATE_REMOTE_RUNTIME_REASON_TIME
} update_remote_runtime_reason_t;

typedef struct {
    update_remote_runtime_state_t state;
    update_remote_runtime_reason_t reason;
    uint8_t initialized;
    uint8_t enabled;
    uint8_t network_ready;
    uint8_t manifest_cached;
    uint8_t selective_ready;
    uint8_t package_cached;
    uint8_t busy;
    uint8_t pending_recovery;
    uint8_t active_slot;
    uint16_t http_status;
    uint32_t bytes_received;
    uint32_t total_bytes;
    uint16_t entry_count;
    uint16_t reused_entries;
    uint16_t missing_assets;
    uint32_t operation_generation;
    char manifest_url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
    char release_url[UPDATE_REMOTE_RUNTIME_URL_SIZE];
    char cached_manifest_alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    char cached_package_alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    update_runtime_manifest_t manifest;
} update_remote_runtime_status_t;

typedef struct {
    update_remote_runtime_reason_t reason;
    uint16_t http_status;
    uint8_t cache_preserved;
    uint8_t manifest_published;
    uint8_t package_published;
    uint8_t selective_published;
    uint16_t assets_downloaded;
    uint16_t assets_reused;
    uint32_t bytes_received;
    char cached_manifest_alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    char cached_package_alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    update_runtime_verification_t verification;
    update_runtime_manifest_t manifest;
    tls_reason_t tls_reason;
    uint16_t tls_error;
} update_remote_runtime_result_t;

int update_remote_runtime_init(void);
int update_remote_runtime_enable(void);
int update_remote_runtime_disable(void);
int update_remote_runtime_check(
    const char* tag, const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out);
int update_remote_runtime_fetch(
    const char* tag, update_remote_runtime_fetch_mode_t mode,
    const update_remote_options_t* options,
    update_remote_runtime_result_t* result_out);
int update_remote_runtime_clear(const update_remote_options_t* options,
                                update_remote_runtime_result_t* result_out);
int update_remote_runtime_get_status(
    update_remote_runtime_status_t* status_out);
int update_remote_runtime_get_cache(update_runtime_cache_t* cache_out);
int update_remote_runtime_get_cached_alias(char* alias_out,
                                           uint32_t capacity);
int update_remote_runtime_capability_available(void);
const char* update_remote_runtime_state_name(
    update_remote_runtime_state_t state);
const char* update_remote_runtime_reason_name(
    update_remote_runtime_reason_t reason);

#endif
