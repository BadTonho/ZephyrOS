#ifndef APP_REMOTE_H
#define APP_REMOTE_H

#include "types.h"
#include "core/app_package.h"

#define APP_REMOTE_MAX_ENTRIES 16U
#define APP_REMOTE_HEADER_SIZE 128U
#define APP_REMOTE_ENTRY_SIZE 256U
#define APP_REMOTE_SIGNATURE_SIZE 64U
#define APP_REMOTE_MAX_CATALOG_SIZE \
    (APP_REMOTE_HEADER_SIZE + APP_REMOTE_MAX_ENTRIES * \
     APP_REMOTE_ENTRY_SIZE + APP_REMOTE_SIGNATURE_SIZE)
#define APP_REMOTE_URL_SIZE 512U
#define APP_REMOTE_PATH_SIZE 100U

typedef enum {
    APP_REMOTE_STATE_DISABLED = 0,
    APP_REMOTE_STATE_UNAVAILABLE,
    APP_REMOTE_STATE_READY,
    APP_REMOTE_STATE_CHECKING,
    APP_REMOTE_STATE_AVAILABLE,
    APP_REMOTE_STATE_DOWNLOADING,
    APP_REMOTE_STATE_VALIDATING,
    APP_REMOTE_STATE_FAILED
} app_remote_state_t;

typedef enum {
    APP_REMOTE_REASON_NONE = 0,
    APP_REMOTE_REASON_DISABLED,
    APP_REMOTE_REASON_NETWORK,
    APP_REMOTE_REASON_HTTP,
    APP_REMOTE_REASON_TIMEOUT,
    APP_REMOTE_REASON_CATALOG_FORMAT,
    APP_REMOTE_REASON_UNKNOWN_KEY,
    APP_REMOTE_REASON_REVOKED_KEY,
    APP_REMOTE_REASON_SIGNATURE,
    APP_REMOTE_REASON_REPLAY,
    APP_REMOTE_REASON_DUPLICATE,
    APP_REMOTE_REASON_PATH,
    APP_REMOTE_REASON_PLAN_INCOMPLETE,
    APP_REMOTE_REASON_PLAN_CYCLE,
    APP_REMOTE_REASON_PLAN_CONFLICT,
    APP_REMOTE_REASON_SIZE,
    APP_REMOTE_REASON_SPACE,
    APP_REMOTE_REASON_IO,
    APP_REMOTE_REASON_CANCELLED,
    APP_REMOTE_REASON_PACKAGE_HASH,
    APP_REMOTE_REASON_PACKAGE_VERIFY,
    APP_REMOTE_REASON_PACKAGE_MISMATCH,
    APP_REMOTE_REASON_CACHE_UNAVAILABLE,
    APP_REMOTE_REASON_CACHE_INVALID,
    APP_REMOTE_REASON_MUTATION_BUSY
} app_remote_reason_t;

typedef enum {
    APP_REMOTE_CACHE_UNAVAILABLE = 0,
    APP_REMOTE_CACHE_EMPTY,
    APP_REMOTE_CACHE_VALID,
    APP_REMOTE_CACHE_INVALID,
    APP_REMOTE_CACHE_STALE
} app_remote_cache_state_t;

typedef enum {
    APP_REMOTE_ENTRY_AVAILABLE = 0,
    APP_REMOTE_ENTRY_CACHED,
    APP_REMOTE_ENTRY_INSTALLED,
    APP_REMOTE_ENTRY_UPDATE_AVAILABLE,
    APP_REMOTE_ENTRY_SAME_VERSION,
    APP_REMOTE_ENTRY_DOWNGRADE,
    APP_REMOTE_ENTRY_BLOCKED
} app_remote_entry_state_t;

typedef int (*app_remote_cancel_check_t)(void* context);

typedef struct {
    app_package_info_t info;
    uint32_t package_size;
    uint8_t package_hash[32];
    char package_path[APP_REMOTE_PATH_SIZE];
    app_remote_entry_state_t state;
    app_remote_reason_t reason;
    uint8_t cached;
    uint8_t installed;
    char installed_version[APP_PACKAGE_VERSION_TEXT_SIZE];
} app_remote_entry_t;

typedef struct {
    app_remote_state_t state;
    app_remote_reason_t reason;
    app_remote_cache_state_t cache_state;
    uint8_t initialized;
    uint8_t enabled;
    uint8_t network_ready;
    uint8_t busy;
    uint8_t catalog_available;
    uint8_t cache_target_available;
    uint8_t cache_pending;
    uint16_t http_status;
    uint32_t generation;
    uint32_t highest_generation;
    uint32_t entry_count;
    uint32_t bytes_received;
    uint32_t total_bytes;
    char catalog_url[APP_REMOTE_URL_SIZE];
    char cache_target[APP_PACKAGE_ID_SIZE];
    uint8_t key_id[16];
} app_remote_status_t;

typedef struct {
    uint8_t allow_downgrade;
    app_remote_cancel_check_t cancel_check;
    void* cancel_context;
} app_remote_options_t;

typedef struct {
    app_remote_reason_t reason;
    uint16_t http_status;
    uint8_t cache_preserved;
    uint8_t cache_published;
    uint32_t bytes_received;
    uint32_t required_clusters;
    uint32_t free_clusters;
    app_package_plan_t plan;
    app_package_action_result_t package_action;
} app_remote_result_t;

int app_remote_init(void);
int app_remote_enable(void);
int app_remote_disable(void);
int app_remote_check(const char* catalog_url,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out);
int app_remote_fetch(const char* id, const char* catalog_url, int confirmed,
                     const app_remote_options_t* options,
                     app_remote_result_t* result_out);
int app_remote_apply_cached(const char* id, int update, int confirmed,
                            const app_remote_options_t* options,
                            app_remote_result_t* result_out);
int app_remote_clear(int confirmed, app_remote_result_t* result_out);
int app_remote_get_status(app_remote_status_t* status_out);
int app_remote_get_count(uint32_t* count_out);
int app_remote_get_entry(uint32_t index, app_remote_entry_t* entry_out);
int app_remote_find_entry(const char* id, app_remote_entry_t* entry_out);
int app_remote_build_plan(const char* id, int update, int allow_downgrade,
                          app_package_plan_t* plan_out,
                          app_remote_result_t* result_out);
int app_remote_refresh_provenance(void);
int app_remote_get_installed_trust(const char* id, const char* version);
int app_remote_is_provenance_available(void);
int app_remote_test_fail_after(uint8_t completed_files);
void app_remote_request_cancel(void);
const char* app_remote_state_name(app_remote_state_t state);
const char* app_remote_reason_name(app_remote_reason_t reason);
const char* app_remote_cache_state_name(app_remote_cache_state_t state);
const char* app_remote_entry_state_name(app_remote_entry_state_t state);

#endif
