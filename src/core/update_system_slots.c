#include "core/update_system_slots.h"
#include "core/crypto.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "process/process.h"

#define SYSTEM_SLOTS_STATE_MAGIC "ZSI1"
#define SYSTEM_SLOTS_JOURNAL_MAGIC "ZSJ1"
#define SYSTEM_SLOTS_FORMAT_VERSION 1U
#define SYSTEM_SLOTS_STATE_SLOT_OFFSET 32U
#define SYSTEM_SLOTS_STATE_SLOT_SIZE 176U
#define SYSTEM_SLOTS_JOURNAL_TARGET_OFFSET 32U
#define SYSTEM_SLOTS_CONTROL_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)
#define SYSTEM_SLOTS_STATE_HASH_OFFSET \
    UPDATE_SYSTEM_SLOT_CONTROL_HASH_OFFSET
#define SYSTEM_SLOTS_JOURNAL_HASH_OFFSET \
    UPDATE_SYSTEM_SLOT_CONTROL_HASH_OFFSET
#define SYSTEM_SLOTS_PHASE_NONE UPDATE_SYSTEM_SLOTS_JOURNAL_NONE
#define SYSTEM_SLOTS_PHASE_PREPARED UPDATE_SYSTEM_SLOTS_JOURNAL_PREPARED
#define SYSTEM_SLOTS_PHASE_STAGING UPDATE_SYSTEM_SLOTS_JOURNAL_STAGING
#define SYSTEM_SLOTS_PHASE_VERIFIED UPDATE_SYSTEM_SLOTS_JOURNAL_VERIFIED
#define SYSTEM_SLOTS_PHASE_COMMITTED UPDATE_SYSTEM_SLOTS_JOURNAL_COMMITTED
#define SYSTEM_SLOTS_STATE_FLAG_RECOVERY 1U
#define SYSTEM_SLOTS_ID_SIZE UPDATE_SYSTEM_SLOT_IDENTIFIER_SIZE
#define SYSTEM_SLOTS_IO_SIZE (64U * 1024U)

typedef struct {
    uint32_t sequence;
    uint8_t active_slot;
    uint8_t pending_slot;
    uint8_t flags;
    update_system_slot_info_t slots[UPDATE_SYSTEM_SLOT_COUNT];
} system_slots_state_t;

typedef struct {
    uint32_t sequence;
    uint8_t phase;
    uint8_t target_slot;
    update_system_slot_info_t target;
} system_slots_journal_t;

static const char* system_slots_aliases[UPDATE_SYSTEM_SLOT_COUNT] = {
    UPDATE_SYSTEM_SLOT_A_ALIAS, UPDATE_SYSTEM_SLOT_B_ALIAS
};
static const char* system_slots_state_aliases[UPDATE_SYSTEM_SLOT_COUNT] = {
    UPDATE_SYSTEM_SLOT_STATE_A_ALIAS, UPDATE_SYSTEM_SLOT_STATE_B_ALIAS
};
static const char* system_slots_journal_aliases[UPDATE_SYSTEM_SLOT_COUNT] = {
    UPDATE_SYSTEM_SLOT_JOURNAL_A_ALIAS, UPDATE_SYSTEM_SLOT_JOURNAL_B_ALIAS
};

static spinlock_t system_slots_lock;
static uint8_t system_slots_initialized;
static uint8_t system_slots_volume_ready;
static uint8_t system_slots_state_degraded;
static uint8_t system_slots_journal_degraded;
static system_slots_state_t system_slots_state;
static system_slots_journal_t system_slots_journal;
static update_system_slots_status_t system_slots_status;
static uint8_t system_slots_control[UPDATE_SYSTEM_SLOT_CONTROL_SIZE];
static uint8_t system_slots_io[SYSTEM_SLOTS_IO_SIZE];

static uint16_t system_slots_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t system_slots_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void system_slots_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void system_slots_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static int system_slots_bytes_zero(const uint8_t* data, uint32_t size) {
    for (uint32_t index = 0U; index < size; index++) {
        if (data[index]) return 0;
    }
    return 1;
}

static int system_slots_magic_equal(const uint8_t* raw, const char* magic) {
    if (!raw || !magic) return 0;
    for (uint32_t index = 0U; index < 4U; index++) {
        if (raw[index] != (uint8_t)magic[index]) return 0;
    }
    return 1;
}

static int system_slots_copy_fixed_text(const uint8_t* raw, uint32_t capacity,
                                        char* output, uint32_t output_capacity) {
    uint32_t length = 0U;

    if (!raw || !output || !capacity || !output_capacity) return ERR_NULL;
    while (length < capacity && raw[length]) length++;
    if (length >= output_capacity) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < length; index++) {
        uint8_t value = raw[index];
        if (value < 0x20U || value > 0x7EU) return ERR_INVALID;
        output[index] = (char)value;
    }
    output[length] = '\0';
    for (uint32_t index = length + 1U; index < capacity; index++) {
        if (raw[index]) return ERR_INVALID;
    }
    return OK;
}

static int system_slots_write_fixed_text(uint8_t* raw, uint32_t capacity,
                                         const char* value) {
    uint32_t length;

    if (!raw || !capacity || !value) return ERR_NULL;
    length = kstrlen(value);
    if (length >= capacity) return ERR_OVERFLOW;
    kmemset(raw, 0, capacity);
    kmemcpy(raw, value, length);
    return OK;
}

static int system_slots_version_compare(const update_version_t* first,
                                        const update_version_t* second) {
    if (!first || !second) return 0;
    if (first->major != second->major) {
        return first->major < second->major ? -1 : 1;
    }
    if (first->minor != second->minor) {
        return first->minor < second->minor ? -1 : 1;
    }
    if (first->patch != second->patch) {
        return first->patch < second->patch ? -1 : 1;
    }
    return 0;
}

static int system_slots_is_newer(const update_version_t* target,
                                 uint32_t target_epoch,
                                 const update_version_t* base,
                                 uint32_t base_epoch) {
    int compare = system_slots_version_compare(target, base);

    return compare > 0 || (compare == 0 && target_epoch > base_epoch);
}

static void system_slots_encode_info(uint8_t* raw,
                                     const update_system_slot_info_t* info) {
    if (!raw || !info) return;
    kmemset(raw, 0, SYSTEM_SLOTS_STATE_SLOT_SIZE);
    raw[0] = (uint8_t)info->state;
    system_slots_write_u16(raw + 2U, info->version.major);
    system_slots_write_u16(raw + 4U, info->version.minor);
    system_slots_write_u16(raw + 6U, info->version.patch);
    system_slots_write_u32(raw + 8U, info->epoch);
    system_slots_write_u32(raw + 12U, info->size);
    kmemcpy(raw + 16U, info->file_hash, 32U);
    system_slots_write_fixed_text(raw + 48U, SYSTEM_SLOTS_ID_SIZE,
                                  info->release_id);
    system_slots_write_fixed_text(raw + 112U, SYSTEM_SLOTS_ID_SIZE,
                                  info->release_tag);
}

static int system_slots_decode_info(const uint8_t* raw,
                                    update_system_slot_info_t* info) {
    int result;

    if (!raw || !info) return ERR_NULL;
    kmemset(info, 0, sizeof(*info));
    if (raw[0] > UPDATE_SYSTEM_SLOT_FILE_VALID ||
        !system_slots_bytes_zero(raw + 1U, 1U)) return ERR_INVALID;
    info->state = (update_system_slot_file_state_t)raw[0];
    info->version.major = system_slots_read_u16(raw + 2U);
    info->version.minor = system_slots_read_u16(raw + 4U);
    info->version.patch = system_slots_read_u16(raw + 6U);
    info->epoch = system_slots_read_u32(raw + 8U);
    info->size = system_slots_read_u32(raw + 12U);
    kmemcpy(info->file_hash, raw + 16U, 32U);
    result = system_slots_copy_fixed_text(raw + 48U, SYSTEM_SLOTS_ID_SIZE,
                                           info->release_id,
                                           sizeof(info->release_id));
    if (result != OK) return result;
    result = system_slots_copy_fixed_text(raw + 112U, SYSTEM_SLOTS_ID_SIZE,
                                           info->release_tag,
                                           sizeof(info->release_tag));
    if (result != OK) return result;
    if (info->state == UPDATE_SYSTEM_SLOT_FILE_EMPTY &&
        (info->size || info->epoch || info->file_hash[0] ||
         info->release_id[0] || info->release_tag[0])) {
        return ERR_INVALID;
    }
    if (info->state == UPDATE_SYSTEM_SLOT_FILE_VALID &&
        (!info->size || !info->release_id[0] || !info->release_tag[0])) {
        return ERR_INVALID;
    }
    return OK;
}

static int system_slots_encode_state(const system_slots_state_t* state,
                                     uint8_t* raw) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    crypto_sha256_ctx_t context;
    int result;

    if (!state || !raw) return ERR_NULL;
    kmemset(raw, 0, UPDATE_SYSTEM_SLOT_CONTROL_SIZE);
    kmemcpy(raw, SYSTEM_SLOTS_STATE_MAGIC, 4U);
    system_slots_write_u16(raw + 4U, SYSTEM_SLOTS_FORMAT_VERSION);
    system_slots_write_u16(raw + 6U, UPDATE_SYSTEM_SLOT_CONTROL_SIZE);
    system_slots_write_u32(raw + 8U, state->sequence);
    raw[12] = state->active_slot;
    raw[13] = state->pending_slot;
    raw[14] = state->flags;
    for (uint32_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        system_slots_encode_info(
            raw + SYSTEM_SLOTS_STATE_SLOT_OFFSET +
                index * SYSTEM_SLOTS_STATE_SLOT_SIZE, &state->slots[index]);
    }
    result = crypto_sha256_init(&context);
    if (result == OK) result = crypto_sha256_update(
        &context, raw, SYSTEM_SLOTS_STATE_HASH_OFFSET);
    if (result == OK) result = crypto_sha256_final(&context, hash);
    if (result == OK) kmemcpy(raw + SYSTEM_SLOTS_STATE_HASH_OFFSET,
                              hash, sizeof(hash));
    return result;
}

static int system_slots_decode_state(const uint8_t* raw,
                                     system_slots_state_t* state) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    if (!raw || !state) return ERR_NULL;
    if (!system_slots_magic_equal(raw, SYSTEM_SLOTS_STATE_MAGIC) ||
        system_slots_read_u16(raw + 4U) != SYSTEM_SLOTS_FORMAT_VERSION ||
        system_slots_read_u16(raw + 6U) != UPDATE_SYSTEM_SLOT_CONTROL_SIZE ||
        system_slots_read_u32(raw + 8U) == 0U ||
        raw[12] >= UPDATE_SYSTEM_SLOT_COUNT ||
        (raw[13] != UPDATE_SYSTEM_SLOT_NONE &&
         raw[13] >= UPDATE_SYSTEM_SLOT_COUNT) ||
        (raw[14] & ~SYSTEM_SLOTS_STATE_FLAG_RECOVERY)) {
        return ERR_INVALID;
    }
    result = crypto_sha256(raw, SYSTEM_SLOTS_STATE_HASH_OFFSET, hash);
    if (result != OK || !crypto_equal(hash + 0U,
                                      raw + SYSTEM_SLOTS_STATE_HASH_OFFSET,
                                      sizeof(hash))) return ERR_INVALID;
    if (!system_slots_bytes_zero(raw + 15U, 17U) ||
        !system_slots_bytes_zero(raw + 384U,
                                 SYSTEM_SLOTS_STATE_HASH_OFFSET - 384U)) {
        return ERR_INVALID;
    }
    kmemset(state, 0, sizeof(*state));
    state->sequence = system_slots_read_u32(raw + 8U);
    state->active_slot = raw[12];
    state->pending_slot = raw[13];
    state->flags = raw[14];
    for (uint32_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        result = system_slots_decode_info(
            raw + SYSTEM_SLOTS_STATE_SLOT_OFFSET +
                index * SYSTEM_SLOTS_STATE_SLOT_SIZE, &state->slots[index]);
        if (result != OK) return result;
    }
    if (state->slots[state->active_slot].state !=
            UPDATE_SYSTEM_SLOT_FILE_VALID ||
        (state->pending_slot != UPDATE_SYSTEM_SLOT_NONE &&
         state->slots[state->pending_slot].state !=
             UPDATE_SYSTEM_SLOT_FILE_VALID) ||
        (state->pending_slot == state->active_slot)) return ERR_INVALID;
    return OK;
}

static int system_slots_encode_journal(const system_slots_journal_t* journal,
                                       uint8_t* raw) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    crypto_sha256_ctx_t context;
    int result;

    if (!journal || !raw) return ERR_NULL;
    kmemset(raw, 0, UPDATE_SYSTEM_SLOT_CONTROL_SIZE);
    kmemcpy(raw, SYSTEM_SLOTS_JOURNAL_MAGIC, 4U);
    system_slots_write_u16(raw + 4U, SYSTEM_SLOTS_FORMAT_VERSION);
    system_slots_write_u16(raw + 6U, UPDATE_SYSTEM_SLOT_CONTROL_SIZE);
    system_slots_write_u32(raw + 8U, journal->sequence);
    raw[12] = journal->phase;
    raw[13] = journal->target_slot;
    system_slots_encode_info(raw + SYSTEM_SLOTS_JOURNAL_TARGET_OFFSET,
                             &journal->target);
    result = crypto_sha256_init(&context);
    if (result == OK) result = crypto_sha256_update(
        &context, raw, SYSTEM_SLOTS_JOURNAL_HASH_OFFSET);
    if (result == OK) result = crypto_sha256_final(&context, hash);
    if (result == OK) kmemcpy(raw + SYSTEM_SLOTS_JOURNAL_HASH_OFFSET,
                              hash, sizeof(hash));
    return result;
}

static int system_slots_decode_journal(const uint8_t* raw,
                                       system_slots_journal_t* journal) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    if (!raw || !journal) return ERR_NULL;
    if (!system_slots_magic_equal(raw, SYSTEM_SLOTS_JOURNAL_MAGIC) ||
        system_slots_read_u16(raw + 4U) != SYSTEM_SLOTS_FORMAT_VERSION ||
        system_slots_read_u16(raw + 6U) != UPDATE_SYSTEM_SLOT_CONTROL_SIZE ||
        system_slots_read_u32(raw + 8U) == 0U || raw[12] < SYSTEM_SLOTS_PHASE_PREPARED ||
        raw[12] > SYSTEM_SLOTS_PHASE_COMMITTED ||
        raw[13] >= UPDATE_SYSTEM_SLOT_COUNT) return ERR_INVALID;
    result = crypto_sha256(raw, SYSTEM_SLOTS_JOURNAL_HASH_OFFSET, hash);
    if (result != OK || !crypto_equal(hash,
                                      raw + SYSTEM_SLOTS_JOURNAL_HASH_OFFSET,
                                      sizeof(hash))) return ERR_INVALID;
    if (!system_slots_bytes_zero(raw + 14U, 18U) ||
        !system_slots_bytes_zero(raw + 208U,
                                 SYSTEM_SLOTS_JOURNAL_HASH_OFFSET - 208U)) {
        return ERR_INVALID;
    }
    kmemset(journal, 0, sizeof(*journal));
    journal->sequence = system_slots_read_u32(raw + 8U);
    journal->phase = raw[12];
    journal->target_slot = raw[13];
    return system_slots_decode_info(raw + SYSTEM_SLOTS_JOURNAL_TARGET_OFFSET,
                                    &journal->target);
}

static int system_slots_load_control(const char* alias, uint8_t* raw) {
    uint32_t size = 0U;
    uint32_t bytes = 0U;
    int result;

    result = fs_get_root_file_info(alias, &size, 0);
    if (result != OK) return result;
    if (size != UPDATE_SYSTEM_SLOT_CONTROL_SIZE) return ERR_INVALID;
    result = fs_read_file_range_at(alias, 0U, raw,
                                   UPDATE_SYSTEM_SLOT_CONTROL_SIZE, &bytes);
    return result == OK && bytes == UPDATE_SYSTEM_SLOT_CONTROL_SIZE ?
           OK : (result == OK ? ERR_DISK : result);
}

static int system_slots_write_control(const char* alias, const uint8_t* raw) {
    return fs_atomic_write_root(alias, raw, UPDATE_SYSTEM_SLOT_CONTROL_SIZE,
                                SYSTEM_SLOTS_CONTROL_ATTRIBUTES,
                                FS_ATOMIC_CREATE_OR_REPLACE);
}

static int system_slots_hash_file(const char* path, uint32_t size,
                                  uint8_t hash[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t context;
    uint32_t position = 0U;
    int result;

    if (!path || !hash) return ERR_NULL;
    result = crypto_sha256_init(&context);
    while (result == OK && position < size) {
        uint32_t amount = size - position;
        uint32_t bytes = 0U;

        if (amount > sizeof(system_slots_io)) amount = sizeof(system_slots_io);
        result = fs_read_file_range_at(path, position, system_slots_io,
                                       amount, &bytes);
        if (result == OK && bytes != amount) result = ERR_DISK;
        if (result == OK) {
            result = crypto_sha256_update(&context, system_slots_io, bytes);
            position += bytes;
        }
    }
    if (result == OK) result = crypto_sha256_final(&context, hash);
    return result;
}

static int system_slots_path_is_local(const char* path) {
    static const char prefix[] = "system:/";
    uint32_t length;
    uint32_t prefix_length = sizeof(prefix) - 1U;

    if (!path) return 0;
    length = kstrlen(path);
    if (length <= prefix_length || length >= FS_MAX_PATH) return 0;
    for (uint32_t index = 0U; index < prefix_length; index++) {
        if (path[index] != prefix[index]) return 0;
    }
    for (uint32_t index = prefix_length; index < length; index++) {
        if (path[index] == '/' || path[index] == '\\') return 0;
    }
    if (kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_A_ALIAS) == 0 ||
        kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_B_ALIAS) == 0 ||
        kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_STAGING_ALIAS) == 0 ||
        kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_STATE_A_ALIAS) == 0 ||
        kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_STATE_B_ALIAS) == 0 ||
        kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_JOURNAL_A_ALIAS) == 0 ||
        kstrcmp(path, "system:/" UPDATE_SYSTEM_SLOT_JOURNAL_B_ALIAS) == 0) return 0;
    return 1;
}

static void system_slots_reset_info(update_system_slot_info_t* info) {
    if (info) kmemset(info, 0, sizeof(*info));
}

static int system_slots_fill_info(const char* path,
                                  const update_system_verification_t* verification,
                                  update_system_slot_info_t* info) {
    uint32_t size = 0U;
    int result;

    if (!path || !verification || !info) return ERR_NULL;
    result = fs_get_root_file_info(path, &size, 0);
    if (result != OK) return result;
    if (size != UPDATE_SYSTEM_HEADER_SIZE + verification->image_size ||
        size > UPDATE_SYSTEM_SLOT_MAX_FILE_SIZE) return ERR_INVALID;
    system_slots_reset_info(info);
    info->state = UPDATE_SYSTEM_SLOT_FILE_VALID;
    info->version = verification->target_version;
    info->epoch = verification->target_epoch;
    info->size = size;
    kmemcpy(info->release_id, verification->release_id,
            sizeof(info->release_id));
    kmemcpy(info->release_tag, verification->release_tag,
            sizeof(info->release_tag));
    return system_slots_hash_file(path, size, info->file_hash);
}

static int system_slots_info_matches(const update_system_slot_info_t* expected,
                                     const update_system_slot_info_t* actual) {
    return expected && actual && expected->state == actual->state &&
           expected->version.major == actual->version.major &&
           expected->version.minor == actual->version.minor &&
           expected->version.patch == actual->version.patch &&
           expected->epoch == actual->epoch && expected->size == actual->size &&
           kstrcmp(expected->release_id, actual->release_id) == 0 &&
           kstrcmp(expected->release_tag, actual->release_tag) == 0 &&
           crypto_equal(expected->file_hash, actual->file_hash, 32U);
}

static update_system_slots_reason_t system_slots_map_reason(
    update_system_reason_t reason) {
    switch (reason) {
        case UPDATE_SYSTEM_REASON_FORMAT: return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
        case UPDATE_SYSTEM_REASON_SIZE: return UPDATE_SYSTEM_SLOTS_REASON_SIZE;
        case UPDATE_SYSTEM_REASON_HASH: return UPDATE_SYSTEM_SLOTS_REASON_HASH;
        case UPDATE_SYSTEM_REASON_SIGNATURE:
        case UPDATE_SYSTEM_REASON_UNKNOWN_KEY:
            return UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE;
        case UPDATE_SYSTEM_REASON_BASE_VERSION:
        case UPDATE_SYSTEM_REASON_COMPATIBILITY:
            return UPDATE_SYSTEM_SLOTS_REASON_COMPATIBILITY;
        case UPDATE_SYSTEM_REASON_DOWNGRADE:
            return UPDATE_SYSTEM_SLOTS_REASON_DOWNGRADE;
        case UPDATE_SYSTEM_REASON_IO: return UPDATE_SYSTEM_SLOTS_REASON_IO;
        default: return UPDATE_SYSTEM_SLOTS_REASON_FORMAT;
    }
}

static int system_slots_file_info(const char* path,
                                  update_system_slot_info_t* info) {
    update_system_verification_t verification;
    int result;

    if (!path || !info) return ERR_NULL;
    result = update_system_verify_file_for_slot(path, &verification);
    if (result != OK) return result;
    return system_slots_fill_info(path, &verification, info);
}

static int system_slots_write_state_locked(void) {
    uint8_t slot;
    int result;

    if (system_slots_state.sequence == 0xFFFFFFFFU) {
        LOG_ERROR("UPDATE", "Sequencia de estado de slots esgotada");
        return ERR_OVERFLOW;
    }
    system_slots_state.sequence++;
    result = system_slots_encode_state(&system_slots_state,
                                       system_slots_control);
    if (result != OK) return result;
    slot = (uint8_t)(system_slots_state.sequence & 1U);
    result = system_slots_write_control(system_slots_state_aliases[slot],
                                        system_slots_control);
    return result;
}

static int system_slots_write_journal_locked(void) {
    uint8_t slot;
    int result;

    result = system_slots_encode_journal(&system_slots_journal,
                                         system_slots_control);
    if (result != OK) return result;
    slot = (uint8_t)(system_slots_journal.sequence & 1U);
    result = system_slots_write_control(system_slots_journal_aliases[slot],
                                        system_slots_control);
    return result;
}

static int system_slots_clear_journal_locked(void) {
    int first_error = OK;

    for (uint32_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        int result = fs_atomic_delete_root(system_slots_journal_aliases[index]);
        if (result != OK && result != ERR_NOT_FOUND && first_error == OK) {
            first_error = result;
        }
    }
    if (first_error == OK) {
        kmemset(&system_slots_journal, 0, sizeof(system_slots_journal));
    }
    return first_error;
}

static int system_slots_delete_staging(const char* volume_id) {
    int result;

    if (!volume_id) return ERR_NULL;
    result = storage_delete_file(volume_id, UPDATE_SYSTEM_SLOT_STAGING_ALIAS);
    return result == ERR_NOT_FOUND ? OK : result;
}

static int system_slots_read_free_bytes(uint32_t* output) {
    fs_info_t info;

    if (!output) return ERR_NULL;
    if (fs_get_info(&info) != OK || info.type != FS_TYPE_FAT32 ||
        info.bytes_per_sector == 0U) return ERR_UNAVAILABLE;
    if (info.free_sectors > 0xFFFFFFFFU / info.bytes_per_sector) {
        *output = 0xFFFFFFFFU;
    } else {
        *output = info.free_sectors * info.bytes_per_sector;
    }
    return OK;
}

static void system_slots_refresh_status_locked(void) {
    uint32_t free_bytes = 0U;
    uint8_t has_invalid = 0U;

    kmemset(&system_slots_status, 0, sizeof(system_slots_status));
    system_slots_status.sequence = system_slots_state.sequence;
    system_slots_status.active_slot = system_slots_state.sequence == 0U ?
        UPDATE_SYSTEM_SLOT_NONE : system_slots_state.active_slot;
    system_slots_status.pending_slot = system_slots_state.pending_slot;
    system_slots_status.journal_pending =
        system_slots_journal.phase != SYSTEM_SLOTS_PHASE_NONE;
    system_slots_status.journal_phase =
        (update_system_slots_journal_phase_t)system_slots_journal.phase;
    system_slots_status.recovery_pending =
        (system_slots_state.flags & SYSTEM_SLOTS_STATE_FLAG_RECOVERY) != 0U ||
        system_slots_status.journal_pending;
    system_slots_status.last_error = OK;
    if (system_slots_read_free_bytes(&free_bytes) == OK) {
        system_slots_status.free_bytes = free_bytes;
    }
    if (!system_slots_volume_ready) {
        system_slots_status.state = UPDATE_SYSTEM_SLOTS_STATE_DEGRADED;
        system_slots_status.last_error = ERR_UNAVAILABLE;
        return;
    }
    for (uint32_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        system_slots_status.slots[index] = system_slots_state.slots[index];
        if (system_slots_status.slots[index].state ==
            UPDATE_SYSTEM_SLOT_FILE_INVALID) has_invalid = 1U;
    }
    if (has_invalid || system_slots_state_degraded ||
        system_slots_journal_degraded) {
        system_slots_status.state = UPDATE_SYSTEM_SLOTS_STATE_DEGRADED;
        system_slots_status.last_error = ERR_INVALID;
    } else if (system_slots_state.sequence == 0U) {
        system_slots_status.state = UPDATE_SYSTEM_SLOTS_STATE_EMPTY;
    } else if (system_slots_status.recovery_pending) {
        system_slots_status.state =
            UPDATE_SYSTEM_SLOTS_STATE_RECOVERY_PENDING;
        system_slots_status.last_error = ERR_STATE;
    } else {
        system_slots_status.state = UPDATE_SYSTEM_SLOTS_STATE_READY;
    }
}

static int system_slots_load_state_locked(void) {
    system_slots_state_t candidate;
    system_slots_state_t selected;
    uint8_t found = 0U;
    uint8_t invalid_count = 0U;

    kmemset(&selected, 0, sizeof(selected));
    for (uint8_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        int result = system_slots_load_control(system_slots_state_aliases[index],
                                               system_slots_control);
        if (result != OK ||
            system_slots_decode_state(system_slots_control, &candidate) != OK) {
            if (result != ERR_NOT_FOUND) invalid_count++;
            continue;
        }
        if (!found || candidate.sequence > selected.sequence) {
            selected = candidate;
            found = 1U;
        }
    }
    if (!found) {
        kmemset(&system_slots_state, 0, sizeof(system_slots_state));
        system_slots_state.pending_slot = UPDATE_SYSTEM_SLOT_NONE;
        if (invalid_count >= UPDATE_SYSTEM_SLOT_COUNT) {
            system_slots_state_degraded = 1U;
            return ERR_INVALID;
        }
        return ERR_NOT_FOUND;
    }
    system_slots_state = selected;
    return OK;
}

static int system_slots_load_journal_locked(void) {
    system_slots_journal_t candidate;
    system_slots_journal_t selected;
    uint8_t found = 0U;
    uint8_t invalid_count = 0U;

    kmemset(&selected, 0, sizeof(selected));
    for (uint8_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        int result = system_slots_load_control(
            system_slots_journal_aliases[index], system_slots_control);
        if (result != ERR_NOT_FOUND && result != OK) {
            invalid_count++;
            continue;
        }
        if (result == ERR_NOT_FOUND) continue;
        if (system_slots_decode_journal(system_slots_control, &candidate) != OK) {
            invalid_count++;
            continue;
        }
        if (!found || candidate.sequence > selected.sequence) {
            selected = candidate;
            found = 1U;
        }
    }
    if (!found) {
        kmemset(&system_slots_journal, 0, sizeof(system_slots_journal));
        if (invalid_count >= UPDATE_SYSTEM_SLOT_COUNT) {
            system_slots_journal_degraded = 1U;
            return ERR_INVALID;
        }
        return OK;
    }
    system_slots_journal = selected;
    return OK;
}

static int system_slots_recover_journal_locked(void) {
    char path[FS_MAX_PATH];
    update_system_slot_info_t actual;
    storage_volume_t volume;
    uint8_t discard_target = 0U;
    int result;

    if (system_slots_journal.phase == SYSTEM_SLOTS_PHASE_NONE) return OK;
    if (system_slots_journal.target_slot >= UPDATE_SYSTEM_SLOT_COUNT) {
        LOG_ERROR("UPDATE", "Journal de slots possui alvo invalido");
        return ERR_INVALID;
    }
    path[0] = 's'; path[1] = 'y'; path[2] = 's'; path[3] = 't';
    path[4] = 'e'; path[5] = 'm'; path[6] = ':'; path[7] = '/';
    for (uint32_t index = 0U; index < 8U; index++) {
        path[8U + index] = system_slots_aliases[
            system_slots_journal.target_slot][index];
    }
    path[16] = '\0';
    result = system_slots_file_info(path, &actual);
    if (result == OK && system_slots_info_matches(
            &system_slots_journal.target, &actual)) {
        if (system_slots_journal.phase >= SYSTEM_SLOTS_PHASE_VERIFIED &&
            (system_slots_state.pending_slot == UPDATE_SYSTEM_SLOT_NONE ||
             system_slots_state.pending_slot ==
                 system_slots_journal.target_slot)) {
            system_slots_state.slots[system_slots_journal.target_slot] = actual;
            system_slots_state.pending_slot = system_slots_journal.target_slot;
            system_slots_state.flags &=
                (uint8_t)~SYSTEM_SLOTS_STATE_FLAG_RECOVERY;
            if (system_slots_write_state_locked() != OK) return ERR_DISK;
        } else if (system_slots_journal.phase >=
                   SYSTEM_SLOTS_PHASE_VERIFIED) {
            LOG_ERROR("UPDATE", "Journal diverge do slot pendente atual");
            return ERR_INVALID;
        } else {
            discard_target =
                !(system_slots_state.slots[system_slots_journal.target_slot].state ==
                      UPDATE_SYSTEM_SLOT_FILE_VALID &&
                  system_slots_info_matches(
                      &system_slots_state.slots[
                          system_slots_journal.target_slot], &actual));
        }
    } else if (system_slots_journal.phase <= SYSTEM_SLOTS_PHASE_STAGING) {
        if (result != ERR_NOT_FOUND ||
            (result == OK && system_slots_info_matches(
                &system_slots_journal.target, &actual))) {
            discard_target = 1U;
        }
        LOG_WARN("UPDATE", "Staging de slot interrompido; preservando ativo");
    } else {
        LOG_ERROR("UPDATE", "Slot verificado diverge do journal");
        return ERR_INVALID;
    }
    if (storage_find_system_volume(&volume) != OK) {
        LOG_ERROR("UPDATE", "Volume de sistema ausente na recuperacao");
        return ERR_UNAVAILABLE;
    }
    if (discard_target) {
        result = storage_delete_file(
            volume.id, system_slots_aliases[system_slots_journal.target_slot]);
        if (result != OK && result != ERR_NOT_FOUND) {
            LOG_ERROR("UPDATE", "Nao foi possivel descartar slot incompleto");
            return ERR_DISK;
        }
    }
    result = system_slots_delete_staging(volume.id);
    if (result != OK) {
        LOG_ERROR("UPDATE", "Nao foi possivel limpar staging pendente");
        return ERR_DISK;
    }
    return system_slots_clear_journal_locked();
}

static int system_slots_cancelled(
    const update_system_slots_action_options_t* options) {
    return options && options->cancel_check &&
           options->cancel_check(options->cancel_context);
}

static int system_slots_publish_status_locked(void) {
    system_slots_refresh_status_locked();
    if (system_slots_status.state == UPDATE_SYSTEM_SLOTS_STATE_DEGRADED) {
        return ERR_INVALID;
    }
    return system_slots_status.state ==
               UPDATE_SYSTEM_SLOTS_STATE_RECOVERY_PENDING ? ERR_STATE : OK;
}

int update_system_slots_init(void) {
    storage_volume_t volume;
    int state_result;
    int journal_result;
    int recovery_result;
    int status_result;

    LOG_INFO("UPDATE", "Inicializando slots de imagem ZSYS");
    spinlock_acquire(&system_slots_lock);
    kmemset(&system_slots_state, 0, sizeof(system_slots_state));
    kmemset(&system_slots_journal, 0, sizeof(system_slots_journal));
    kmemset(&system_slots_status, 0, sizeof(system_slots_status));
    system_slots_state.pending_slot = UPDATE_SYSTEM_SLOT_NONE;
    system_slots_state_degraded = 0U;
    system_slots_journal_degraded = 0U;
    system_slots_initialized = 1U;
    system_slots_volume_ready =
        storage_find_system_volume(&volume) == OK && fs_has_system_volume();
    if (!system_slots_volume_ready || !update_system_is_ready()) {
        system_slots_status.state = UPDATE_SYSTEM_SLOTS_STATE_DEGRADED;
        system_slots_status.last_error = !system_slots_volume_ready ?
            ERR_UNAVAILABLE : ERR_STATE;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Slots ZSYS indisponiveis sem volume FAT32");
        return system_slots_status.last_error;
    }
    state_result = system_slots_load_state_locked();
    journal_result = system_slots_load_journal_locked();
    recovery_result = system_slots_recover_journal_locked();
    if (system_slots_journal.phase == SYSTEM_SLOTS_PHASE_NONE) {
        int cleanup_result = system_slots_delete_staging(volume.id);
        if (cleanup_result != OK) {
            system_slots_state.flags |= SYSTEM_SLOTS_STATE_FLAG_RECOVERY;
            recovery_result = ERR_DISK;
            LOG_ERROR("UPDATE", "Staging temporario nao foi removido");
        }
    }
    if (state_result != OK && state_result != ERR_NOT_FOUND) {
        LOG_ERROR("UPDATE", "Estado redundante de slots invalido");
    }
    if (journal_result != OK || recovery_result != OK) {
        system_slots_state.flags |= SYSTEM_SLOTS_STATE_FLAG_RECOVERY;
        LOG_ERROR("UPDATE", "Recuperacao de slots ZSYS pendente");
    }
    for (uint32_t index = 0U; index < UPDATE_SYSTEM_SLOT_COUNT; index++) {
        if (system_slots_state.slots[index].state !=
            UPDATE_SYSTEM_SLOT_FILE_VALID) continue;
        char path[FS_MAX_PATH];
        update_system_slot_info_t actual;
        path[0] = 's'; path[1] = 'y'; path[2] = 's'; path[3] = 't';
        path[4] = 'e'; path[5] = 'm'; path[6] = ':'; path[7] = '/';
        for (uint32_t offset = 0U; offset < 8U; offset++) {
            path[8U + offset] = system_slots_aliases[index][offset];
        }
        path[16] = '\0';
        if (system_slots_file_info(path, &actual) != OK ||
            !system_slots_info_matches(&system_slots_state.slots[index],
                                       &actual)) {
            system_slots_state.slots[index].state =
                UPDATE_SYSTEM_SLOT_FILE_INVALID;
            system_slots_state_degraded = 1U;
            LOG_ERROR("UPDATE", "Arquivo de slot ZSYS diverge do estado");
        }
    }
    status_result = system_slots_publish_status_locked();
    spinlock_release(&system_slots_lock);
    if (status_result != OK) {
        LOG_WARN("UPDATE", "Slots ZSYS inicializados em estado degradado");
        return status_result;
    }
    LOG_INFO("UPDATE", "Slots de imagem ZSYS inicializados");
    return OK;
}

int update_system_slots_is_ready(void) {
    return system_slots_initialized && system_slots_volume_ready;
}

int update_system_slots_get_status(update_system_slots_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("UPDATE", "Status de slots ZSYS nulo");
        return ERR_NULL;
    }
    spinlock_acquire(&system_slots_lock);
    if (!system_slots_initialized) {
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Slots ZSYS nao inicializados");
        return ERR_STATE;
    }
    system_slots_refresh_status_locked();
    *status_out = system_slots_status;
    spinlock_release(&system_slots_lock);
    return OK;
}

int update_system_slots_stage_file(
    const char* path,
    const update_system_slots_action_options_t* options,
    update_system_slots_action_result_t* result_out) {
    update_system_verification_t verification;
    update_system_slot_info_t target_info;
    storage_volume_t volume;
    uint32_t source_size = 0U;
    uint32_t free_bytes = 0U;
    uint32_t cluster_bytes = STORAGE_SECTOR_SIZE;
    uint8_t target_slot;
    char target_path[FS_MAX_PATH];
    int result;

    if (result_out) kmemset(result_out, 0, sizeof(*result_out));
    if (!path || !result_out) {
        LOG_ERROR("UPDATE", "Argumento nulo ao preparar slot ZSYS");
        return ERR_NULL;
    }
    result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_STATE;
    if (!system_slots_path_is_local(path)) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED;
        LOG_ERROR("UPDATE", "Staging ZSYS exige arquivo local system:/");
        return ERR_UNAVAILABLE;
    }
    spinlock_acquire(&system_slots_lock);
    if (!system_slots_initialized || !system_slots_volume_ready) {
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Slots ZSYS indisponiveis");
        return ERR_UNAVAILABLE;
    }
    if (system_slots_state.sequence == 0U ||
        system_slots_state.active_slot >= UPDATE_SYSTEM_SLOT_COUNT ||
        system_slots_state.slots[system_slots_state.active_slot].state !=
            UPDATE_SYSTEM_SLOT_FILE_VALID) {
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Slot ativo ZSYS nao esta semeado");
        return ERR_STATE;
    }
    if (system_slots_state.pending_slot != UPDATE_SYSTEM_SLOT_NONE ||
        system_slots_journal.phase != SYSTEM_SLOTS_PHASE_NONE) {
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Ja existe staging ou slot pendente ZSYS");
        return ERR_STATE;
    }
    result = update_system_verify_file(path, &verification);
    result_out->verification_reason = verification.reason;
    if (result != OK) {
        result_out->reason = system_slots_map_reason(verification.reason);
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "ZSYS recusado antes do staging");
        return result;
    }
    if (fs_get_root_file_info(path, &source_size, 0) != OK ||
        source_size > UPDATE_SYSTEM_SLOT_MAX_FILE_SIZE ||
        source_size != UPDATE_SYSTEM_HEADER_SIZE + verification.image_size) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_SIZE;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Tamanho do ZSYS invalido para staging");
        return ERR_INVALID;
    }
    target_slot = (uint8_t)(1U - system_slots_state.active_slot);
    result_out->target_slot = target_slot;
    result_out->target_version = verification.target_version;
    result_out->target_epoch = verification.target_epoch;
    if (!system_slots_is_newer(
            &verification.target_version, verification.target_epoch,
            &system_slots_state.slots[system_slots_state.active_slot].version,
            system_slots_state.slots[system_slots_state.active_slot].epoch)) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_DOWNGRADE;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "ZSYS nao supera o slot ativo");
        return ERR_INVALID;
    }
    if (storage_find_system_volume(&volume) != OK) {
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Volume de sistema ausente no staging ZSYS");
        return ERR_UNAVAILABLE;
    }
    {
        fs_info_t info;
        if (fs_get_info(&info) != OK ||
            system_slots_read_free_bytes(&free_bytes) != OK) {
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_SPACE;
            spinlock_release(&system_slots_lock);
            LOG_ERROR("UPDATE", "Nao foi possivel consultar espaco FAT32");
            return ERR_DISK;
        }
    }
    {
        fs_info_t info;
        if (fs_get_info(&info) == OK && info.sectors_per_cluster) {
            cluster_bytes = info.bytes_per_sector * info.sectors_per_cluster;
        }
    }
    if (source_size > 0xFFFFFFFFU - cluster_bytes * 3U ||
        free_bytes < source_size + cluster_bytes * 3U +
                     UPDATE_SYSTEM_SLOT_CONTROL_SIZE * 4U) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_SPACE;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Espaco insuficiente para staging ZSYS");
        return ERR_OVERFLOW;
    }
    result = system_slots_fill_info(path, &verification, &target_info);
    if (result != OK) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_IO;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Falha ao obter hash completo do ZSYS");
        return result;
    }
    if (options && options->dry_run) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_NONE;
        spinlock_release(&system_slots_lock);
        return OK;
    }
    if (system_slots_state.sequence == 0xFFFFFFFFU) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_STATE;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Sequencia de slots esgotada");
        return ERR_OVERFLOW;
    }
    kmemset(&system_slots_journal, 0, sizeof(system_slots_journal));
    system_slots_journal.sequence = system_slots_state.sequence + 1U;
    system_slots_journal.phase = SYSTEM_SLOTS_PHASE_PREPARED;
    system_slots_journal.target_slot = target_slot;
    system_slots_journal.target = target_info;
    result = system_slots_write_journal_locked();
    if (result != OK) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_JOURNAL;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Falha ao persistir journal de staging");
        return result;
    }
    target_path[0] = 's'; target_path[1] = 'y'; target_path[2] = 's';
    target_path[3] = 't'; target_path[4] = 'e'; target_path[5] = 'm';
    target_path[6] = ':'; target_path[7] = '/';
    for (uint32_t index = 0U; index < 8U; index++) {
        target_path[8U + index] = system_slots_aliases[target_slot][index];
    }
    target_path[16] = '\0';
    system_slots_journal.phase = SYSTEM_SLOTS_PHASE_STAGING;
    result = system_slots_write_journal_locked();
    if (result == OK) result = storage_slot_writer_begin(
        volume.id, system_slots_aliases[target_slot], source_size,
        SYSTEM_SLOTS_CONTROL_ATTRIBUTES);
    for (uint32_t offset = 0U; result == OK && offset < source_size;) {
        uint32_t amount = source_size - offset;
        uint32_t bytes = 0U;
        if (amount > sizeof(system_slots_io)) amount = sizeof(system_slots_io);
        if (system_slots_cancelled(options)) {
            result = ERR_TIMEOUT;
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_CANCELLED;
            break;
        }
        result = fs_read_file_range_at(path, offset, system_slots_io,
                                       amount, &bytes);
        if (result == OK && bytes != amount) result = ERR_DISK;
        if (result == OK) result = storage_slot_writer_write(
            system_slots_io, bytes);
        if (result == OK) offset += bytes;
        result_out->bytes_written = offset;
        process_block(1U);
    }
    if (result == ERR_TIMEOUT) {
        if (storage_slot_writer_is_active()) storage_slot_writer_abort();
        if (system_slots_delete_staging(volume.id) != OK) {
            result_out->recovery_pending = 1U;
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_RECOVERY;
        }
        if (system_slots_clear_journal_locked() != OK) {
            result_out->recovery_pending = 1U;
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_RECOVERY;
        }
        spinlock_release(&system_slots_lock);
        LOG_WARN("UPDATE", "Staging ZSYS cancelado");
        return ERR_TIMEOUT;
    }
    if (result == OK) result = storage_slot_writer_finish();
    if (result != OK) {
        if (storage_slot_writer_is_active()) storage_slot_writer_abort();
        if (system_slots_delete_staging(volume.id) != OK) {
            result_out->recovery_pending = 1U;
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_RECOVERY;
        } else {
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_IO;
        }
        if (system_slots_clear_journal_locked() != OK) {
            result_out->recovery_pending = 1U;
            result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_RECOVERY;
        }
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Falha ao gravar slot ZSYS");
        return result;
    }
    system_slots_journal.phase = SYSTEM_SLOTS_PHASE_VERIFIED;
    result = update_system_verify_file(target_path, &verification);
    if (result == OK) {
        update_system_slot_info_t actual_info;
        result = system_slots_fill_info(target_path, &verification,
                                        &actual_info);
        if (result == OK && !system_slots_info_matches(&target_info,
                                                       &actual_info)) {
            result = ERR_INVALID;
        }
    }
    if (result == OK) result = system_slots_write_journal_locked();
    if (result != OK) {
        result_out->reason = verification.reason == UPDATE_SYSTEM_REASON_NONE ?
            UPDATE_SYSTEM_SLOTS_REASON_JOURNAL :
            system_slots_map_reason(verification.reason);
        result_out->recovery_pending = 1U;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Slot ZSYS nao passou pela verificacao final");
        return result;
    }
    system_slots_state.slots[target_slot] = target_info;
    system_slots_state.pending_slot = target_slot;
    system_slots_state.flags &= (uint8_t)~SYSTEM_SLOTS_STATE_FLAG_RECOVERY;
    result = system_slots_write_state_locked();
    result_out->pending_published = result == OK ? 1U : 0U;
    if (result != OK) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_JOURNAL;
        result_out->recovery_pending = 1U;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Estado pendente do slot nao foi persistido");
        return result;
    }
    system_slots_journal.phase = SYSTEM_SLOTS_PHASE_COMMITTED;
    result = system_slots_write_journal_locked();
    if (result == OK) result = system_slots_clear_journal_locked();
    if (result != OK) {
        result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_RECOVERY;
        result_out->recovery_pending = 1U;
        system_slots_state.flags |= SYSTEM_SLOTS_STATE_FLAG_RECOVERY;
        spinlock_release(&system_slots_lock);
        LOG_ERROR("UPDATE", "Journal de slot permaneceu pendente");
        return result;
    }
    result_out->reason = UPDATE_SYSTEM_SLOTS_REASON_NONE;
    system_slots_publish_status_locked();
    spinlock_release(&system_slots_lock);
    LOG_INFO("UPDATE", "Slot ZSYS preparado e marcado como pendente");
    return OK;
}

const char* update_system_slots_state_name(update_system_slots_state_t state) {
    switch (state) {
        case UPDATE_SYSTEM_SLOTS_STATE_EMPTY: return "EMPTY";
        case UPDATE_SYSTEM_SLOTS_STATE_READY: return "READY";
        case UPDATE_SYSTEM_SLOTS_STATE_DEGRADED: return "DEGRADED";
        case UPDATE_SYSTEM_SLOTS_STATE_RECOVERY_PENDING:
            return "RECOVERY_PENDING";
        default: return "UNKNOWN";
    }
}

const char* update_system_slots_journal_phase_name(
    update_system_slots_journal_phase_t phase) {
    switch (phase) {
        case UPDATE_SYSTEM_SLOTS_JOURNAL_NONE: return "NONE";
        case UPDATE_SYSTEM_SLOTS_JOURNAL_PREPARED: return "PREPARED";
        case UPDATE_SYSTEM_SLOTS_JOURNAL_STAGING: return "STAGING";
        case UPDATE_SYSTEM_SLOTS_JOURNAL_VERIFIED: return "VERIFIED";
        case UPDATE_SYSTEM_SLOTS_JOURNAL_COMMITTED: return "COMMITTED";
        default: return "UNKNOWN";
    }
}

const char* update_system_slot_file_state_name(
    update_system_slot_file_state_t state) {
    switch (state) {
        case UPDATE_SYSTEM_SLOT_FILE_EMPTY: return "EMPTY";
        case UPDATE_SYSTEM_SLOT_FILE_VALID: return "VALID";
        case UPDATE_SYSTEM_SLOT_FILE_INVALID: return "INVALID";
        default: return "UNKNOWN";
    }
}

const char* update_system_slots_reason_name(
    update_system_slots_reason_t reason) {
    switch (reason) {
        case UPDATE_SYSTEM_SLOTS_REASON_NONE: return "NONE";
        case UPDATE_SYSTEM_SLOTS_REASON_FORMAT: return "FORMAT";
        case UPDATE_SYSTEM_SLOTS_REASON_SIZE: return "SIZE";
        case UPDATE_SYSTEM_SLOTS_REASON_HASH: return "HASH";
        case UPDATE_SYSTEM_SLOTS_REASON_SIGNATURE: return "SIGNATURE";
        case UPDATE_SYSTEM_SLOTS_REASON_COMPATIBILITY: return "COMPATIBILITY";
        case UPDATE_SYSTEM_SLOTS_REASON_DOWNGRADE: return "DOWNGRADE";
        case UPDATE_SYSTEM_SLOTS_REASON_STATE: return "STATE";
        case UPDATE_SYSTEM_SLOTS_REASON_SPACE: return "SPACE";
        case UPDATE_SYSTEM_SLOTS_REASON_IO: return "IO";
        case UPDATE_SYSTEM_SLOTS_REASON_JOURNAL: return "JOURNAL";
        case UPDATE_SYSTEM_SLOTS_REASON_CANCELLED: return "CANCELLED";
        case UPDATE_SYSTEM_SLOTS_REASON_RECOVERY: return "RECOVERY";
        case UPDATE_SYSTEM_SLOTS_REASON_UNSUPPORTED: return "UNSUPPORTED";
        default: return "UNKNOWN";
    }
}
