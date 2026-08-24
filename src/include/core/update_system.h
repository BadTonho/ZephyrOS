#ifndef UPDATE_SYSTEM_H
#define UPDATE_SYSTEM_H

#include "types.h"
#include "core/errors.h"
#include "core/update.h"
#include "core/update_remote.h"

#define UPDATE_SYSTEM_MAGIC "ZSYS"
#define UPDATE_SYSTEM_FORMAT_VERSION 1U
#define UPDATE_SYSTEM_ARCH_I386 1U
#define UPDATE_SYSTEM_HEADER_SIZE 1024U
#define UPDATE_SYSTEM_SIGNATURE_SIZE 64U
#define UPDATE_SYSTEM_KEY_ID_SIZE 16U
#define UPDATE_SYSTEM_MAX_IMAGE_SIZE (8U * 1024U * 1024U)
#define UPDATE_SYSTEM_PAYLOAD_OFFSET UPDATE_SYSTEM_HEADER_SIZE
#define UPDATE_SYSTEM_COMPONENT_COUNT 3U
#define UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE 64U
#define UPDATE_SYSTEM_BASE_ENTRY_SIZE 12U
#define UPDATE_SYSTEM_MAX_BASES 8U
#define UPDATE_SYSTEM_CHECKPOINT_SIZE 64U
#define UPDATE_SYSTEM_MAX_CHECKPOINTS 4U
#define UPDATE_SYSTEM_CHANNEL_SIZE 16U
#define UPDATE_SYSTEM_IDENTIFIER_SIZE 64U
#define UPDATE_SYSTEM_IO_BUFFER_SIZE (64U * 1024U)

#define UPDATE_SYSTEM_COMPONENT_BOOT 1U
#define UPDATE_SYSTEM_COMPONENT_STAGE2 2U
#define UPDATE_SYSTEM_COMPONENT_KERNEL 3U

#define UPDATE_SYSTEM_ROUTE_DIRECT 1U
#define UPDATE_SYSTEM_ROUTE_CHECKPOINT 2U
#define UPDATE_SYSTEM_FLAG_REQUIRES_REBOOT 0x0001U
#define UPDATE_SYSTEM_FLAG_BRIDGE_REQUIRED 0x0002U

#define UPDATE_SYSTEM_IMAGE_HASH_OFFSET 28U
#define UPDATE_SYSTEM_TARGET_VERSION_OFFSET 60U
#define UPDATE_SYSTEM_TARGET_EPOCH_OFFSET 66U
#define UPDATE_SYSTEM_BASE_COUNT_OFFSET 70U
#define UPDATE_SYSTEM_BASE_ENTRY_SIZE_OFFSET 72U
#define UPDATE_SYSTEM_BASES_OFFSET 76U
#define UPDATE_SYSTEM_MIN_UPDATER_OFFSET \
    (UPDATE_SYSTEM_BASES_OFFSET + \
     UPDATE_SYSTEM_MAX_BASES * UPDATE_SYSTEM_BASE_ENTRY_SIZE)
#define UPDATE_SYSTEM_BOOT_ABI_OFFSET \
    (UPDATE_SYSTEM_MIN_UPDATER_OFFSET + UPDATE_SYSTEM_BASE_ENTRY_SIZE)
#define UPDATE_SYSTEM_SCHEMA_FROM_OFFSET \
    (UPDATE_SYSTEM_BOOT_ABI_OFFSET + 4U)
#define UPDATE_SYSTEM_SCHEMA_TO_OFFSET \
    (UPDATE_SYSTEM_SCHEMA_FROM_OFFSET + 4U)
#define UPDATE_SYSTEM_ROUTE_OFFSET \
    (UPDATE_SYSTEM_SCHEMA_TO_OFFSET + 4U)
#define UPDATE_SYSTEM_CHECKPOINT_COUNT_OFFSET \
    (UPDATE_SYSTEM_ROUTE_OFFSET + 1U)
#define UPDATE_SYSTEM_CHANNEL_OFFSET \
    (UPDATE_SYSTEM_CHECKPOINT_COUNT_OFFSET + 1U)
#define UPDATE_SYSTEM_RELEASE_ID_OFFSET \
    (UPDATE_SYSTEM_CHANNEL_OFFSET + UPDATE_SYSTEM_CHANNEL_SIZE)
#define UPDATE_SYSTEM_RELEASE_TAG_OFFSET \
    (UPDATE_SYSTEM_RELEASE_ID_OFFSET + UPDATE_SYSTEM_IDENTIFIER_SIZE)
#define UPDATE_SYSTEM_COMPONENT_COUNT_OFFSET \
    (UPDATE_SYSTEM_RELEASE_TAG_OFFSET + UPDATE_SYSTEM_IDENTIFIER_SIZE)
#define UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE_OFFSET \
    (UPDATE_SYSTEM_COMPONENT_COUNT_OFFSET + 2U)
#define UPDATE_SYSTEM_COMPONENTS_OFFSET \
    (UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE_OFFSET + 2U)
#define UPDATE_SYSTEM_CHECKPOINT_ENTRY_SIZE_OFFSET \
    (UPDATE_SYSTEM_COMPONENTS_OFFSET + \
     UPDATE_SYSTEM_COMPONENT_COUNT * UPDATE_SYSTEM_COMPONENT_ENTRY_SIZE)
#define UPDATE_SYSTEM_CHECKPOINTS_OFFSET \
    (UPDATE_SYSTEM_CHECKPOINT_ENTRY_SIZE_OFFSET + 2U)
#define UPDATE_SYSTEM_KEY_ID_OFFSET \
    (UPDATE_SYSTEM_CHECKPOINTS_OFFSET + \
     UPDATE_SYSTEM_MAX_CHECKPOINTS * UPDATE_SYSTEM_CHECKPOINT_SIZE)
#define UPDATE_SYSTEM_SIGNATURE_OFFSET \
    (UPDATE_SYSTEM_KEY_ID_OFFSET + UPDATE_SYSTEM_KEY_ID_SIZE)
#define UPDATE_SYSTEM_SIGNATURE_SIZE_OFFSET \
    (UPDATE_SYSTEM_SIGNATURE_OFFSET + UPDATE_SYSTEM_SIGNATURE_SIZE)
#define UPDATE_SYSTEM_SIGNATURE_OFFSET_FIELD \
    (UPDATE_SYSTEM_SIGNATURE_SIZE_OFFSET + 4U)
#define UPDATE_SYSTEM_RESERVED_OFFSET \
    (UPDATE_SYSTEM_SIGNATURE_OFFSET_FIELD + 4U)

typedef enum {
    UPDATE_SYSTEM_REASON_NONE = 0,
    UPDATE_SYSTEM_REASON_FORMAT,
    UPDATE_SYSTEM_REASON_SIZE,
    UPDATE_SYSTEM_REASON_HASH,
    UPDATE_SYSTEM_REASON_UNKNOWN_KEY,
    UPDATE_SYSTEM_REASON_SIGNATURE,
    UPDATE_SYSTEM_REASON_ARCHITECTURE,
    UPDATE_SYSTEM_REASON_BASE_VERSION,
    UPDATE_SYSTEM_REASON_DOWNGRADE,
    UPDATE_SYSTEM_REASON_COMPATIBILITY,
    UPDATE_SYSTEM_REASON_PATH_POLICY,
    UPDATE_SYSTEM_REASON_IO,
    UPDATE_SYSTEM_REASON_UNSUPPORTED
} update_system_reason_t;

typedef struct {
    update_version_t version;
    uint32_t epoch;
} update_system_base_t;

typedef struct {
    uint16_t kind;
    uint32_t offset;
    uint32_t size;
    uint8_t hash[32];
} update_system_component_t;

typedef struct {
    uint16_t base_count;
    update_system_base_t supported_from[UPDATE_SYSTEM_MAX_BASES];
    update_system_base_t minimum_updater;
    uint32_t boot_abi;
    uint32_t data_schema_from;
    uint32_t data_schema_to;
    uint8_t requires_reboot;
    uint8_t route_kind;
    uint8_t bridge_required;
    uint8_t checkpoint_count;
    char channel[UPDATE_SYSTEM_CHANNEL_SIZE];
    char checkpoints[UPDATE_SYSTEM_MAX_CHECKPOINTS]
                    [UPDATE_SYSTEM_CHECKPOINT_SIZE];
} update_system_compatibility_t;

typedef struct {
    update_system_reason_t reason;
    update_version_t target_version;
    uint32_t target_epoch;
    uint32_t image_size;
    uint8_t image_hash[32];
    uint16_t component_count;
    update_system_component_t components[UPDATE_SYSTEM_COMPONENT_COUNT];
    update_system_compatibility_t compatibility;
    char release_id[UPDATE_SYSTEM_IDENTIFIER_SIZE];
    char release_tag[UPDATE_SYSTEM_IDENTIFIER_SIZE];
    uint8_t header_valid;
    uint8_t signature_valid;
    uint8_t image_valid;
    uint8_t compatible;
} update_system_verification_t;

int update_system_init(void);
int update_system_is_ready(void);
int update_system_verify_file(const char* path,
                              update_system_verification_t* result_out);
int update_system_check_tag(const char* tag,
                            const update_remote_options_t* options,
                            update_system_verification_t* result_out);
const char* update_system_reason_name(update_system_reason_t reason);

#endif
