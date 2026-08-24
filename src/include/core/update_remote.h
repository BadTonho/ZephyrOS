#ifndef UPDATE_REMOTE_H
#define UPDATE_REMOTE_H

#include "types.h"
#include "core/tls.h"
#include "core/update.h"

#define UPDATE_REMOTE_MANIFEST_SIZE 256U
#define UPDATE_REMOTE_SIGNED_SIZE 192U
#define UPDATE_REMOTE_PATH_SIZE 100U
#define UPDATE_REMOTE_URL_SIZE 512U
#define UPDATE_REMOTE_ALIAS_SIZE 13U
#define UPDATE_REMOTE_RECORD_SIZE 512U
#define UPDATE_REMOTE_TAG_SIZE 65U
#define UPDATE_REMOTE_RELEASE_ID_SIZE 65U
#define UPDATE_REMOTE_RELEASE_NAME_SIZE 101U
#define UPDATE_REMOTE_SOURCE_COMMIT_SIZE 41U
#define UPDATE_REMOTE_SYSTEM_NAME_SIZE 32U

typedef enum {
    UPDATE_REMOTE_STATE_DISABLED = 0,
    UPDATE_REMOTE_STATE_UNAVAILABLE,
    UPDATE_REMOTE_STATE_READY,
    UPDATE_REMOTE_STATE_CHECKING,
    UPDATE_REMOTE_STATE_AVAILABLE,
    UPDATE_REMOTE_STATE_DOWNLOADING,
    UPDATE_REMOTE_STATE_VALIDATING,
    UPDATE_REMOTE_STATE_FAILED
} update_remote_state_t;

typedef enum {
    UPDATE_REMOTE_REASON_NONE = 0,
    UPDATE_REMOTE_REASON_DISABLED,
    UPDATE_REMOTE_REASON_NETWORK,
    UPDATE_REMOTE_REASON_HTTP,
    UPDATE_REMOTE_REASON_TIMEOUT,
    UPDATE_REMOTE_REASON_MANIFEST_FORMAT,
    UPDATE_REMOTE_REASON_UNKNOWN_KEY,
    UPDATE_REMOTE_REASON_MANIFEST_SIGNATURE,
    UPDATE_REMOTE_REASON_VERSION,
    UPDATE_REMOTE_REASON_SIZE,
    UPDATE_REMOTE_REASON_SPACE,
    UPDATE_REMOTE_REASON_IO,
    UPDATE_REMOTE_REASON_CANCELLED,
    UPDATE_REMOTE_REASON_PACKAGE_HASH,
    UPDATE_REMOTE_REASON_PACKAGE_VERIFY,
    UPDATE_REMOTE_REASON_PACKAGE_MISMATCH,
    UPDATE_REMOTE_REASON_CACHE,
    UPDATE_REMOTE_REASON_RELEASE_NOT_FOUND,
    UPDATE_REMOTE_REASON_RELEASE_FORMAT,
    UPDATE_REMOTE_REASON_RELEASE_TAG,
    UPDATE_REMOTE_REASON_RELEASE_ASSET,
    UPDATE_REMOTE_REASON_RELEASE_CHANGED,
    UPDATE_REMOTE_REASON_TLS,
    UPDATE_REMOTE_REASON_REDIRECT,
    UPDATE_REMOTE_REASON_RELEASE_API
} update_remote_reason_t;

typedef enum {
    UPDATE_REMOTE_STORE_UNAVAILABLE = 0,
    UPDATE_REMOTE_STORE_EMPTY,
    UPDATE_REMOTE_STORE_VALID,
    UPDATE_REMOTE_STORE_INVALID
} update_remote_store_t;

typedef struct {
    uint32_t generation;
    update_version_t base_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
    uint32_t package_size;
    uint8_t package_hash[32];
    char package_path[UPDATE_REMOTE_PATH_SIZE];
} update_remote_candidate_t;

typedef struct {
    update_remote_state_t state;
    update_remote_reason_t reason;
    update_remote_store_t cache_store;
    uint8_t initialized;
    uint8_t enabled;
    uint8_t network_ready;
    uint8_t manifest_cached;
    uint8_t package_cached;
    uint8_t busy;
    uint8_t retry_count;
    uint8_t pending_recovery;
    uint16_t http_status;
    uint32_t bytes_received;
    uint32_t total_bytes;
    char manifest_url[UPDATE_REMOTE_URL_SIZE];
    char cached_alias[UPDATE_REMOTE_ALIAS_SIZE];
    update_remote_candidate_t candidate;
    uint32_t operation_generation;
} update_remote_status_t;

typedef struct {
    uint8_t dry_run;
    update_cancel_check_t cancel_check;
    void* cancel_context;
    const char* http_accept;
    const char* http_api_version;
    uint8_t http_require_https;
    uint8_t http_follow_redirects;
    uint8_t http_max_redirects;
} update_remote_options_t;

typedef struct {
    char tag[UPDATE_REMOTE_TAG_SIZE];
    char release_id[UPDATE_REMOTE_RELEASE_ID_SIZE];
    char release_name[UPDATE_REMOTE_RELEASE_NAME_SIZE];
    char source_commit[UPDATE_REMOTE_SOURCE_COMMIT_SIZE];
    char descriptor_url[UPDATE_REMOTE_URL_SIZE];
    char manifest_url[UPDATE_REMOTE_URL_SIZE];
    char package_name[UPDATE_REMOTE_PATH_SIZE];
    uint32_t package_size;
    uint8_t package_hash[32];
    uint8_t manifest_hash[32];
    uint8_t api_metadata_present;
    uint8_t api_metadata_hash[32];
    char system_name[UPDATE_REMOTE_SYSTEM_NAME_SIZE];
    uint32_t system_size;
    uint8_t system_hash[32];
    uint8_t system_present;
} update_remote_release_t;

typedef struct {
    update_remote_reason_t reason;
    uint16_t http_status;
    uint8_t retry_count;
    uint8_t cache_preserved;
    uint8_t package_published;
    uint32_t bytes_received;
    char cached_alias[UPDATE_REMOTE_ALIAS_SIZE];
    update_remote_candidate_t candidate;
    zupd_reason_t verification_reason;
    uint8_t manifest_hash[32];
    update_remote_release_t release;
    uint8_t secure;
    uint8_t tls_verified;
    uint8_t redirect_count;
    tls_reason_t tls_reason;
    uint16_t tls_error;
} update_remote_result_t;

int update_remote_init(void);
int update_remote_enable(void);
int update_remote_disable(void);
int update_remote_check(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out);
int update_remote_fetch(const char* manifest_url,
                        const update_remote_options_t* options,
                        update_remote_result_t* result_out);
int update_remote_release_check(const char* tag,
                                const update_remote_options_t* options,
                                update_remote_result_t* result_out);
int update_remote_release_fetch(const char* tag,
                                const update_remote_options_t* options,
                                update_remote_result_t* result_out);
int update_remote_clear(const update_remote_options_t* options,
                        update_remote_result_t* result_out);
int update_remote_get_status(update_remote_status_t* status_out);
int update_remote_get_cached_alias(char* alias_out, uint32_t capacity);
int update_remote_capability_available(void);
const char* update_remote_state_name(update_remote_state_t state);
const char* update_remote_reason_name(update_remote_reason_t reason);
const char* update_remote_store_name(update_remote_store_t state);

#endif
