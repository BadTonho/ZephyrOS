#ifndef APP_CATALOG_H
#define APP_CATALOG_H

#include "types.h"
#include "core/app_package.h"

#define APP_CATALOG_MAX_SOURCES 16U
#define APP_CATALOG_MAX_ENTRIES 32U
#define APP_CATALOG_ALIAS_SIZE  13U

typedef enum {
    APP_CATALOG_STATE_AVAILABLE = 0,
    APP_CATALOG_STATE_INSTALLED,
    APP_CATALOG_STATE_UPDATE_AVAILABLE,
    APP_CATALOG_STATE_SAME_VERSION,
    APP_CATALOG_STATE_DOWNGRADE,
    APP_CATALOG_STATE_BLOCKED,
    APP_CATALOG_STATE_INVALID
} app_catalog_state_t;

typedef enum {
    APP_CATALOG_REASON_NONE = 0,
    APP_CATALOG_REASON_PACKAGE_INVALID,
    APP_CATALOG_REASON_ALIAS_MISMATCH,
    APP_CATALOG_REASON_DEPENDENCY_MISSING,
    APP_CATALOG_REASON_INSUFFICIENT_SPACE,
    APP_CATALOG_REASON_SOURCE_LIMIT,
    APP_CATALOG_REASON_ENTRY_LIMIT,
    APP_CATALOG_REASON_READ_ERROR,
    APP_CATALOG_REASON_FILESYSTEM_UNAVAILABLE,
    APP_CATALOG_REASON_LOADER_UNAVAILABLE,
    APP_CATALOG_REASON_PACKAGE_SERVICE_UNAVAILABLE
} app_catalog_reason_t;

typedef enum {
    APP_CATALOG_CAPABILITY_NONE = 0,
    APP_CATALOG_CAPABILITY_VERIFY = 1U << 0,
    APP_CATALOG_CAPABILITY_INSTALL = 1U << 1,
    APP_CATALOG_CAPABILITY_RUN = 1U << 2,
    APP_CATALOG_CAPABILITY_REMOVE = 1U << 3,
    APP_CATALOG_CAPABILITY_UPDATE = 1U << 4
} app_catalog_capability_t;

typedef struct {
    char alias[APP_CATALOG_ALIAS_SIZE];
    uint32_t source_size;
    app_package_info_t source;
    app_package_info_t installed;
    uint32_t capabilities;
    uint32_t missing_dependency_mask;
    app_catalog_state_t state;
    app_catalog_reason_t reason;
    uint8_t has_source;
    uint8_t has_installed;
} app_catalog_entry_t;

typedef struct {
    uint32_t source_count;
    uint32_t valid_source_count;
    uint32_t invalid_source_count;
    uint32_t installed_count;
    uint32_t entry_count;
    app_catalog_reason_t reason;
    uint8_t source_overflow;
    uint8_t entry_overflow;
} app_catalog_status_t;

int app_catalog_init(void);
int app_catalog_refresh(void);
int app_catalog_is_ready(void);
int app_catalog_get_status(app_catalog_status_t* status_out);
int app_catalog_get_count(uint32_t* count_out);
int app_catalog_get_entry(uint32_t index, app_catalog_entry_t* entry_out);
int app_catalog_find_entry(const char* id_or_alias,
                           app_catalog_entry_t* entry_out);
const char* app_catalog_state_name(app_catalog_state_t state);
const char* app_catalog_reason_name(app_catalog_reason_t reason);

#endif
