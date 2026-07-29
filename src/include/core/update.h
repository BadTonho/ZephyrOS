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
} update_capabilities_t;

int update_init(void);
int update_is_ready(void);
int update_verify_file(const char* path, update_verification_t* result_out);
int update_get_capabilities(update_capabilities_t* capabilities_out);
const char* zupd_reason_name(zupd_reason_t reason);

#endif
