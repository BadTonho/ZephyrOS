#ifndef UPDATE_SYSTEM_SLOTS_H
#define UPDATE_SYSTEM_SLOTS_H

#include "types.h"
#include "core/errors.h"
#include "core/update.h"
#include "core/update_system.h"

#define UPDATE_SYSTEM_SLOT_COUNT 2U
#define UPDATE_SYSTEM_SLOT_NONE 0xFFU
#define UPDATE_SYSTEM_SLOT_CONTROL_SIZE 512U
#define UPDATE_SYSTEM_SLOT_CONTROL_HASH_OFFSET 480U
#define UPDATE_SYSTEM_SLOT_MAX_FILE_SIZE \
    (UPDATE_SYSTEM_HEADER_SIZE + UPDATE_SYSTEM_MAX_IMAGE_SIZE)
#define UPDATE_SYSTEM_SLOT_ALIAS_SIZE 13U
#define UPDATE_SYSTEM_SLOT_IDENTIFIER_SIZE UPDATE_SYSTEM_IDENTIFIER_SIZE
#define UPDATE_SYSTEM_SLOT_A_ALIAS "ZSA0.ZSY"
#define UPDATE_SYSTEM_SLOT_B_ALIAS "ZSB0.ZSY"
#define UPDATE_SYSTEM_SLOT_STATE_A_ALIAS "ZSI0.STA"
#define UPDATE_SYSTEM_SLOT_STATE_B_ALIAS "ZSI1.STA"
#define UPDATE_SYSTEM_SLOT_JOURNAL_A_ALIAS "ZSI0.JRN"
#define UPDATE_SYSTEM_SLOT_JOURNAL_B_ALIAS "ZSI1.JRN"
#define UPDATE_SYSTEM_SLOT_STAGING_ALIAS "ZSTG.ZSY"

typedef enum {
    UPDATE_SYSTEM_SLOTS_STATE_EMPTY = 0,
    UPDATE_SYSTEM_SLOTS_STATE_READY,
    UPDATE_SYSTEM_SLOTS_STATE_DEGRADED,
    UPDATE_SYSTEM_SLOTS_STATE_RECOVERY_PENDING
} update_system_slots_state_t;

typedef enum {
    UPDATE_SYSTEM_SLOT_FILE_EMPTY = 0,
    UPDATE_SYSTEM_SLOT_FILE_VALID,
    UPDATE_SYSTEM_SLOT_FILE_INVALID
} update_system_slot_file_state_t;

typedef enum {
    UPDATE_SYSTEM_SLOTS_JOURNAL_NONE = 0,
    UPDATE_SYSTEM_SLOTS_JOURNAL_PREPARED,
    UPDATE_SYSTEM_SLOTS_JOURNAL_STAGING,
    UPDATE_SYSTEM_SLOTS_JOURNAL_VERIFIED,
    UPDATE_SYSTEM_SLOTS_JOURNAL_COMMITTED
} update_system_slots_journal_phase_t;

typedef enum {
    UPDATE_SYSTEM_SLOTS_REASON_NONE = 0,
    UPDATE_SYSTEM_SLOTS_REASON_FORMAT,
    UPDATE_SYSTEM_SLOTS_REASON_SIZE,
    UPDATE_SYSTEM_SLOTS_REASON_HASH,
    UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE,
    UPDATE_SYSTEM_SLOTS_REASON_COMPATIBILITY,
    UPDATE_SYSTEM_SLOTS_REASON_DOWNGRADE,
    UPDATE_SYSTEM_SLOTS_REASON_STATE,
    UPDATE_SYSTEM_SLOTS_REASON_SPACE,
    UPDATE_SYSTEM_SLOTS_REASON_IO,
    UPDATE_SYSTEM_SLOTS_REASON_JOURNAL,
    UPDATE_SYSTEM_SLOTS_REASON_CANCELLED,
    UPDATE_SYSTEM_SLOTS_REASON_RECOVERY,
    UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED
} update_system_slots_reason_t;

typedef struct {
    update_system_slot_file_state_t state;
    update_version_t version;
    uint32_t epoch;
    uint32_t size;
    uint8_t file_hash[32];
    char release_id[UPDATE_SYSTEM_SLOT_IDENTIFIER_SIZE];
    char release_tag[UPDATE_SYSTEM_SLOT_IDENTIFIER_SIZE];
} update_system_slot_info_t;

typedef struct {
    update_system_slots_state_t state;
    uint32_t sequence;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t journal_pending;
    uint8_t recovery_pending;
    update_system_slots_journal_phase_t journal_phase;
    uint32_t free_bytes;
    int last_error;
    update_system_slot_info_t slots[UPDATE_SYSTEM_SLOT_COUNT];
} update_system_slots_status_t;

typedef struct {
    uint8_t dry_run;
    update_cancel_check_t cancel_check;
    void* cancel_context;
} update_system_slots_action_options_t;

typedef struct {
    update_system_slots_reason_t reason;
    update_system_reason_t verification_reason;
    uint8_t target_slot;
    uint32_t bytes_written;
    update_version_t target_version;
    uint32_t target_epoch;
    uint8_t pending_published;
    uint8_t recovery_pending;
} update_system_slots_action_result_t;

int update_system_slots_init(void);
int update_system_slots_is_ready(void);
int update_system_slots_get_status(update_system_slots_status_t* status_out);
int update_system_slots_stage_file(
    const char* path,
    const update_system_slots_action_options_t* options,
    update_system_slots_action_result_t* result_out);
const char* update_system_slots_state_name(update_system_slots_state_t state);
const char* update_system_slots_journal_phase_name(
    update_system_slots_journal_phase_t phase);
const char* update_system_slot_file_state_name(
    update_system_slot_file_state_t state);
const char* update_system_slots_reason_name(
    update_system_slots_reason_t reason);

#endif
