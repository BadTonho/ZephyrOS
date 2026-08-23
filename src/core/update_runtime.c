#include "core/update_runtime.h"
#include "core/crypto.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/update_remote_runtime.h"
#include "core/update_trust.h"
#include "core/version.h"
#include "fs/fs.h"

#define RUNTIME_MANIFEST_MAGIC "ZUM2"
#define RUNTIME_PACKAGE_MAGIC "ZUPD"
#define RUNTIME_STATE_MAGIC "ZRT2"
#define RUNTIME_JOURNAL_MAGIC "ZRTJ"
#define RUNTIME_CONTROL_VERSION 1U
#define RUNTIME_CONTROL_HASH_OFFSET 4064U
#define RUNTIME_STATE_HEADER_SIZE 64U
#define RUNTIME_JOURNAL_HEADER_SIZE 64U
#define RUNTIME_FILE_STATE_SIZE 40U
#define RUNTIME_JOURNAL_ENTRY_SIZE 80U
#define RUNTIME_STATE_CURRENT_OFFSET 64U
#define RUNTIME_STATE_ROLLBACK_OFFSET \
    (RUNTIME_STATE_CURRENT_OFFSET + UPDATE_RUNTIME_MAX_ENTRIES * \
     RUNTIME_FILE_STATE_SIZE)
#define RUNTIME_JOURNAL_ENTRY_OFFSET 64U
#define RUNTIME_JOURNAL_ENTRY_INDEX 1U
#define RUNTIME_JOURNAL_ENTRY_OPERATION 2U
#define RUNTIME_JOURNAL_NONE 0U
#define RUNTIME_JOURNAL_APPLY 1U
#define RUNTIME_JOURNAL_ROLLBACK 2U
#define RUNTIME_PHASE_NONE 0U
#define RUNTIME_PHASE_PREPARED 1U
#define RUNTIME_PHASE_STAGING 2U
#define RUNTIME_PHASE_REPLACING 3U
#define RUNTIME_PHASE_COMMITTED 4U
#define RUNTIME_SLOT_NONE 0xFFU
#define RUNTIME_SIGNATURE_OFFSET 4032U
#define RUNTIME_MANIFEST_TAG_OFFSET 84U
#define RUNTIME_MANIFEST_ID_OFFSET 148U
#define RUNTIME_MANIFEST_BASE_RESERVED_SIZE 6U
#define RUNTIME_PACKAGE_GENERATION 36U
#define RUNTIME_PACKAGE_TARGET_VERSION 40U
#define RUNTIME_PACKAGE_TARGET_EPOCH 46U
#define RUNTIME_PACKAGE_ENTRY_COUNT 50U
#define RUNTIME_PACKAGE_ENTRY_SIZE 52U
#define RUNTIME_PACKAGE_KEY_ID 56U
#define RUNTIME_PACKAGE_BASE_VERSION 74U
#define RUNTIME_PACKAGE_BASE_EPOCH 80U
#define RUNTIME_PACKAGE_PAYLOAD_OFFSET 20U
#define RUNTIME_PACKAGE_PAYLOAD_SIZE 24U
#define RUNTIME_PACKAGE_SIGNATURE_OFFSET 28U
#define RUNTIME_PACKAGE_SIGNATURE_SIZE 32U
#define RUNTIME_PACKAGE_FLAGS 12U
#define RUNTIME_ENTRY_PATH 0U
#define RUNTIME_ENTRY_PAYLOAD_OFFSET 64U
#define RUNTIME_ENTRY_PAYLOAD_SIZE 68U
#define RUNTIME_ENTRY_PRESENT 72U
#define RUNTIME_ENTRY_OPERATION 73U
#define RUNTIME_ENTRY_HASH 76U
#define RUNTIME_STATE_SEQUENCE 8U
#define RUNTIME_STATE_INSTALLED_VERSION 12U
#define RUNTIME_STATE_INSTALLED_EPOCH 18U
#define RUNTIME_STATE_ROLLBACK_AVAILABLE 22U
#define RUNTIME_STATE_ROLLBACK_SLOT 23U
#define RUNTIME_STATE_ENTRY_COUNT 24U
#define RUNTIME_STATE_ROLLBACK_COUNT 26U
#define RUNTIME_STATE_PREVIOUS_VERSION 28U
#define RUNTIME_STATE_PREVIOUS_EPOCH 34U
#define RUNTIME_JOURNAL_SEQUENCE 8U
#define RUNTIME_JOURNAL_PHASE 12U
#define RUNTIME_JOURNAL_KIND 13U
#define RUNTIME_JOURNAL_SLOT 14U
#define RUNTIME_JOURNAL_PROGRESS 15U
#define RUNTIME_JOURNAL_ENTRY_COUNT 16U
#define RUNTIME_JOURNAL_BASE_VERSION 20U
#define RUNTIME_JOURNAL_BASE_EPOCH 26U
#define RUNTIME_JOURNAL_TARGET_VERSION 30U
#define RUNTIME_JOURNAL_TARGET_EPOCH 36U
#define RUNTIME_JOURNAL_ENTRY_OLD 0U
#define RUNTIME_JOURNAL_ENTRY_NEW 40U
#define RUNTIME_RUNTIME_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)

typedef struct {
    uint8_t present;
    uint32_t size;
    uint8_t hash[32];
} runtime_file_state_t;

typedef struct {
    uint8_t catalog_index;
    runtime_file_state_t old_state;
    runtime_file_state_t new_state;
    uint8_t operation;
} runtime_plan_entry_t;

typedef struct {
    uint8_t magic[4];
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    update_version_t installed_version;
    uint32_t installed_epoch;
    uint8_t rollback_available;
    uint8_t rollback_slot;
    uint16_t entry_count;
    uint16_t rollback_entry_count;
    update_version_t previous_version;
    uint32_t previous_epoch;
    runtime_file_state_t current[UPDATE_RUNTIME_MAX_ENTRIES];
    runtime_file_state_t rollback[UPDATE_RUNTIME_MAX_ENTRIES];
} runtime_state_t;

typedef struct {
    uint8_t magic[4];
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint8_t phase;
    uint8_t kind;
    uint8_t slot;
    uint8_t progress;
    uint16_t entry_count;
    update_version_t base_version;
    uint32_t base_epoch;
    update_version_t target_version;
    uint32_t target_epoch;
    runtime_plan_entry_t entries[UPDATE_RUNTIME_MAX_ENTRIES];
} runtime_journal_t;

typedef struct {
    uint8_t valid;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint8_t present;
    uint8_t operation;
    char path[UPDATE_RUNTIME_PATH_SIZE];
    uint8_t hash[32];
} runtime_package_entry_t;

typedef struct {
    uint32_t generation;
    update_version_t target_version;
    uint32_t target_epoch;
    update_version_t base_version;
    uint32_t base_epoch;
    uint16_t entry_count;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t signature_offset;
    uint32_t signature_size;
    uint8_t key_id[16];
    runtime_package_entry_t entries[UPDATE_RUNTIME_MAX_ENTRIES];
} runtime_package_t;

static const char* runtime_catalog[] = {
    "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
};
static const char* runtime_state_aliases[2] = {
    "ZTV0.STA", "ZTV1.STA"
};
static const char* runtime_journal_aliases[2] = {
    "ZTV0.JRN", "ZTV1.JRN"
};
static const char* runtime_stage_prefixes[2] = {"ZTS0.", "ZTS1."};
static const char* runtime_backup_prefixes[2] = {"ZTB0.", "ZTB1."};
static const uint8_t runtime_manifest_domain[] =
    "ZEPHYROS-RUNTIME-MANIFEST-V2";
static const uint8_t runtime_package_domain[] =
    "ZEPHYROS-RUNTIME-PACKAGE-V2";
static const uint8_t runtime_zero_hash[32] = {0};

static spinlock_t runtime_lock;
static uint8_t runtime_initialized;
static uint8_t runtime_state_healthy;
static int runtime_state_slot = -1;
static int runtime_journal_slot = -1;
static runtime_state_t runtime_state;
static runtime_journal_t runtime_journal;
static uint8_t runtime_control[UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U];
static uint8_t runtime_manifest_raw[UPDATE_RUNTIME_MANIFEST_SIZE];
static uint8_t runtime_io_buffer[UPDATE_RUNTIME_IO_BUFFER_SIZE];
static uint8_t runtime_signature[UPDATE_RUNTIME_SIGNATURE_SIZE];
static uint8_t runtime_fail_after;

static int runtime_hash_file_range(const char* path, uint32_t offset,
                                   uint32_t size, uint8_t hash[32]);
static int runtime_magic_equal(const uint8_t* raw, const char* magic);
static int runtime_current_version(update_version_t* version,
                                   uint32_t* epoch);

static uint16_t runtime_read_u16(const uint8_t* raw) {
    return (uint16_t)raw[0] | ((uint16_t)raw[1] << 8U);
}

static uint32_t runtime_read_u32(const uint8_t* raw) {
    return (uint32_t)raw[0] |
           ((uint32_t)raw[1] << 8U) |
           ((uint32_t)raw[2] << 16U) |
           ((uint32_t)raw[3] << 24U);
}

static void runtime_write_u16(uint8_t* raw, uint16_t value) {
    raw[0] = (uint8_t)value;
    raw[1] = (uint8_t)(value >> 8U);
}

static void runtime_write_u32(uint8_t* raw, uint32_t value) {
    raw[0] = (uint8_t)value;
    raw[1] = (uint8_t)(value >> 8U);
    raw[2] = (uint8_t)(value >> 16U);
    raw[3] = (uint8_t)(value >> 24U);
}

static int runtime_version_compare(const update_version_t* first,
                                   const update_version_t* second) {
    if (first->major != second->major) {
        return first->major > second->major ? 1 : -1;
    }
    if (first->minor != second->minor) {
        return first->minor > second->minor ? 1 : -1;
    }
    if (first->patch != second->patch) {
        return first->patch > second->patch ? 1 : -1;
    }
    return 0;
}

static int runtime_version_is_newer(const update_version_t* target,
                                    uint32_t target_epoch,
                                    const update_version_t* base,
                                    uint32_t base_epoch) {
    return target && base && (target_epoch > base_epoch ||
           (target_epoch == base_epoch &&
            runtime_version_compare(target, base) > 0));
}

static void runtime_decode_version(update_version_t* version,
                                    const uint8_t* raw) {
    version->major = runtime_read_u16(raw);
    version->minor = runtime_read_u16(raw + 2U);
    version->patch = runtime_read_u16(raw + 4U);
}

static void runtime_encode_version(uint8_t* raw,
                                   const update_version_t* version) {
    runtime_write_u16(raw, version->major);
    runtime_write_u16(raw + 2U, version->minor);
    runtime_write_u16(raw + 4U, version->patch);
}

static int runtime_path_valid(const uint8_t* raw, char* output) {
    uint32_t length = 0U;

    if (!raw || !output) return 0;
    while (length < UPDATE_RUNTIME_PATH_SIZE && raw[length]) length++;
    if (!length || length >= UPDATE_RUNTIME_PATH_SIZE) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        uint8_t value = raw[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '.')) {
            return 0;
        }
        output[index] = (char)value;
    }
    output[length] = '\0';
    for (uint32_t index = length + 1U;
         index < UPDATE_RUNTIME_PATH_SIZE; index++) {
        if (raw[index]) return 0;
    }
    return 1;
}

static int runtime_catalog_index(const char* path) {
    if (!path) return -1;
    for (uint32_t index = 0U;
         index < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]); index++) {
        if (kstrcmp(path, runtime_catalog[index]) == 0) return (int)index;
    }
    return -1;
}

static void runtime_alias(char* output, uint8_t slot, uint8_t index,
                          const char* prefix) {
    output[0] = prefix[0];
    output[1] = prefix[1];
    output[2] = prefix[2];
    output[3] = prefix[3];
    output[4] = '.';
    output[5] = (char)('0' + index / 10U);
    output[6] = (char)('0' + index % 10U);
    output[7] = '\0';
    (void)slot;
}

static int runtime_delete_root(const char* path) {
    int result;

    if (!path) return ERR_NULL;
    result = fs_atomic_delete_root(path);
    if (result == ERR_NOT_FOUND) return OK;
    return result;
}

static int runtime_hash_file(const char* path, uint32_t expected_size,
                             uint8_t hash[32]) {
    crypto_sha256_ctx_t sha;
    uint32_t offset = 0U;
    uint32_t bytes;
    int result;

    if (!path || !hash) return ERR_NULL;
    result = crypto_sha256_init(&sha);
    while (result == OK && offset < expected_size) {
        uint32_t chunk = expected_size - offset;
        if (chunk > sizeof(runtime_io_buffer)) chunk = sizeof(runtime_io_buffer);
        result = fs_read_file_range_at(path, offset, runtime_io_buffer,
                                       chunk, &bytes);
        if (result == OK && bytes != chunk) result = ERR_DISK;
        if (result == OK) {
            result = crypto_sha256_update(&sha, runtime_io_buffer, bytes);
            if (result == OK) offset += bytes;
        }
    }
    if (result == OK) result = crypto_sha256_final(&sha, hash);
    return result;
}

static int runtime_read_file_state(const char* path, runtime_file_state_t* state) {
    uint32_t size = 0U;
    int result;

    if (!path || !state) return ERR_NULL;
    kmemset(state, 0, sizeof(*state));
    result = fs_get_root_file_info(path, &size, 0);
    if (result != OK) {
        if (result == ERR_NOT_FOUND) return OK;
        return result;
    }
    if (!size || size > UPDATE_RUNTIME_FILE_MAX_SIZE ||
        runtime_hash_file(path, size, state->hash) != OK) return ERR_INVALID;
    state->present = 1U;
    state->size = size;
    return OK;
}

static int runtime_states_equal(const runtime_file_state_t* first,
                                const runtime_file_state_t* second) {
    return first && second && first->present == second->present &&
           (!first->present ||
            (first->size == second->size &&
             crypto_equal(first->hash, second->hash, 32U)));
}

static int runtime_state_hash(uint8_t* raw) {
    uint8_t hash[32];
    if (!raw) return ERR_NULL;
    if (crypto_sha256(raw, RUNTIME_CONTROL_HASH_OFFSET, hash) != OK) {
        return ERR_INVALID;
    }
    kmemcpy(raw + RUNTIME_CONTROL_HASH_OFFSET, hash, sizeof(hash));
    return OK;
}

static int runtime_control_valid(const uint8_t* raw) {
    uint8_t hash[32];
    if (!raw || crypto_sha256(raw, RUNTIME_CONTROL_HASH_OFFSET, hash) != OK) {
        return 0;
    }
    return crypto_equal(hash, raw + RUNTIME_CONTROL_HASH_OFFSET, 32U);
}

static void runtime_encode_file_state(uint8_t* raw,
                                      const runtime_file_state_t* state) {
    kmemset(raw, 0, RUNTIME_FILE_STATE_SIZE);
    raw[0] = state->present;
    runtime_write_u32(raw + 4U, state->size);
    kmemcpy(raw + 8U, state->hash, 32U);
}

static int runtime_file_state_raw_valid(const uint8_t* raw) {
    if (!raw || raw[0] > 1U || raw[1U] || raw[2U] || raw[3U]) return 0;
    if (!raw[0]) {
        return runtime_read_u32(raw + 4U) == 0U &&
               crypto_equal(raw + 8U, runtime_zero_hash, 32U);
    }
    return runtime_read_u32(raw + 4U) > 0U &&
           runtime_read_u32(raw + 4U) <= UPDATE_RUNTIME_FILE_MAX_SIZE &&
           !crypto_equal(raw + 8U, runtime_zero_hash, 32U);
}

static int runtime_journal_old_state_raw_valid(const uint8_t* raw) {
    if (!raw || raw[0] > 1U || raw[3U]) return 0;
    if (!raw[0]) {
        return runtime_read_u32(raw + 4U) == 0U &&
               crypto_equal(raw + 8U, runtime_zero_hash, 32U);
    }
    return runtime_read_u32(raw + 4U) > 0U &&
           runtime_read_u32(raw + 4U) <= UPDATE_RUNTIME_FILE_MAX_SIZE &&
           !crypto_equal(raw + 8U, runtime_zero_hash, 32U);
}

static void runtime_decode_file_state(const uint8_t* raw,
                                      runtime_file_state_t* state) {
    kmemset(state, 0, sizeof(*state));
    state->present = raw[0] ? 1U : 0U;
    state->size = runtime_read_u32(raw + 4U);
    kmemcpy(state->hash, raw + 8U, 32U);
    if (!state->present) {
        state->size = 0U;
        kmemset(state->hash, 0, sizeof(state->hash));
    }
}

static void runtime_encode_state(uint8_t* raw, const runtime_state_t* state) {
    kmemset(raw, 0, UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U);
    kmemcpy(raw, RUNTIME_STATE_MAGIC, 4U);
    runtime_write_u16(raw + 4U, RUNTIME_CONTROL_VERSION);
    runtime_write_u16(raw + 6U, UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U);
    runtime_write_u32(raw + RUNTIME_STATE_SEQUENCE, state->sequence);
    runtime_encode_version(raw + RUNTIME_STATE_INSTALLED_VERSION,
                           &state->installed_version);
    runtime_write_u32(raw + RUNTIME_STATE_INSTALLED_EPOCH,
                      state->installed_epoch);
    raw[RUNTIME_STATE_ROLLBACK_AVAILABLE] = state->rollback_available;
    raw[RUNTIME_STATE_ROLLBACK_SLOT] = state->rollback_slot;
    runtime_write_u16(raw + RUNTIME_STATE_ENTRY_COUNT, state->entry_count);
    runtime_write_u16(raw + RUNTIME_STATE_ROLLBACK_COUNT,
                      state->rollback_entry_count);
    runtime_encode_version(raw + RUNTIME_STATE_PREVIOUS_VERSION,
                           &state->previous_version);
    runtime_write_u32(raw + RUNTIME_STATE_PREVIOUS_EPOCH,
                      state->previous_epoch);
    for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        runtime_encode_file_state(
            raw + RUNTIME_STATE_CURRENT_OFFSET + index * RUNTIME_FILE_STATE_SIZE,
            &state->current[index]);
        runtime_encode_file_state(
            raw + RUNTIME_STATE_ROLLBACK_OFFSET + index * RUNTIME_FILE_STATE_SIZE,
            &state->rollback[index]);
    }
    runtime_state_hash(raw);
}

static int runtime_decode_state(const uint8_t* raw, runtime_state_t* state) {
    if (!raw || !state || !runtime_control_valid(raw) ||
        !runtime_magic_equal(raw, RUNTIME_STATE_MAGIC) ||
        runtime_read_u16(raw + 4U) != RUNTIME_CONTROL_VERSION ||
        runtime_read_u16(raw + 6U) != UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U ||
        raw[RUNTIME_STATE_ROLLBACK_AVAILABLE] > 1U ||
        (raw[RUNTIME_STATE_ROLLBACK_SLOT] != RUNTIME_SLOT_NONE &&
         raw[RUNTIME_STATE_ROLLBACK_SLOT] > 1U) ||
        runtime_read_u16(raw + RUNTIME_STATE_ENTRY_COUNT) > UPDATE_RUNTIME_MAX_ENTRIES ||
        runtime_read_u16(raw + RUNTIME_STATE_ENTRY_COUNT) !=
            sizeof(runtime_catalog) / sizeof(runtime_catalog[0]) ||
            runtime_read_u16(raw + RUNTIME_STATE_ROLLBACK_COUNT) >
            UPDATE_RUNTIME_MAX_ENTRIES) {
        return ERR_INVALID;
    }
    for (uint32_t reserved = 38U; reserved < RUNTIME_STATE_HEADER_SIZE;
         reserved++) {
        if (raw[reserved]) return ERR_INVALID;
    }
    kmemset(state, 0, sizeof(*state));
    state->sequence = runtime_read_u32(raw + RUNTIME_STATE_SEQUENCE);
    runtime_decode_version(&state->installed_version,
                           raw + RUNTIME_STATE_INSTALLED_VERSION);
    state->installed_epoch = runtime_read_u32(raw + RUNTIME_STATE_INSTALLED_EPOCH);
    state->rollback_available = raw[RUNTIME_STATE_ROLLBACK_AVAILABLE] ? 1U : 0U;
    state->rollback_slot = raw[RUNTIME_STATE_ROLLBACK_SLOT];
    state->entry_count = runtime_read_u16(raw + RUNTIME_STATE_ENTRY_COUNT);
    state->rollback_entry_count =
        runtime_read_u16(raw + RUNTIME_STATE_ROLLBACK_COUNT);
    runtime_decode_version(&state->previous_version,
                           raw + RUNTIME_STATE_PREVIOUS_VERSION);
    state->previous_epoch = runtime_read_u32(raw + RUNTIME_STATE_PREVIOUS_EPOCH);
    for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        runtime_decode_file_state(
            raw + RUNTIME_STATE_CURRENT_OFFSET + index * RUNTIME_FILE_STATE_SIZE,
            &state->current[index]);
        if (!runtime_file_state_raw_valid(
                raw + RUNTIME_STATE_CURRENT_OFFSET + index * RUNTIME_FILE_STATE_SIZE) ||
            !runtime_file_state_raw_valid(
                raw + RUNTIME_STATE_ROLLBACK_OFFSET + index * RUNTIME_FILE_STATE_SIZE)) {
            return ERR_INVALID;
        }
        runtime_decode_file_state(
            raw + RUNTIME_STATE_ROLLBACK_OFFSET + index * RUNTIME_FILE_STATE_SIZE,
            &state->rollback[index]);
    }
    if ((!state->rollback_available &&
         (state->rollback_slot != RUNTIME_SLOT_NONE ||
          state->rollback_entry_count != 0U)) ||
        (state->rollback_available &&
         (state->rollback_slot == RUNTIME_SLOT_NONE ||
          state->rollback_entry_count == 0U))) {
        return ERR_INVALID;
    }
    {
        uint16_t rollback_count = 0U;

        for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
            if (!state->rollback_available) {
                if (state->rollback[index].present) return ERR_INVALID;
                continue;
            }
            if (!runtime_states_equal(&state->current[index],
                                      &state->rollback[index])) {
                rollback_count++;
            }
        }
        if (state->rollback_available &&
            rollback_count != state->rollback_entry_count) return ERR_INVALID;
    }
    return OK;
}

static void runtime_encode_plan_entry(uint8_t* raw,
                                      const runtime_plan_entry_t* entry) {
    kmemset(raw, 0, RUNTIME_JOURNAL_ENTRY_SIZE);
    runtime_encode_file_state(raw + RUNTIME_JOURNAL_ENTRY_OLD,
                              &entry->old_state);
    raw[RUNTIME_JOURNAL_ENTRY_INDEX] = entry->catalog_index;
    raw[RUNTIME_JOURNAL_ENTRY_OPERATION] = entry->operation;
    runtime_encode_file_state(raw + RUNTIME_JOURNAL_ENTRY_NEW,
                              &entry->new_state);
}

static void runtime_decode_plan_entry(const uint8_t* raw,
                                      runtime_plan_entry_t* entry) {
    kmemset(entry, 0, sizeof(*entry));
    entry->catalog_index = raw[RUNTIME_JOURNAL_ENTRY_INDEX];
    entry->operation = raw[RUNTIME_JOURNAL_ENTRY_OPERATION];
    runtime_decode_file_state(raw + RUNTIME_JOURNAL_ENTRY_OLD,
                              &entry->old_state);
    runtime_decode_file_state(raw + RUNTIME_JOURNAL_ENTRY_NEW,
                              &entry->new_state);
}

static void runtime_encode_journal(uint8_t* raw,
                                   const runtime_journal_t* journal) {
    kmemset(raw, 0, UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U);
    kmemcpy(raw, RUNTIME_JOURNAL_MAGIC, 4U);
    runtime_write_u16(raw + 4U, RUNTIME_CONTROL_VERSION);
    runtime_write_u16(raw + 6U, UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U);
    runtime_write_u32(raw + RUNTIME_JOURNAL_SEQUENCE, journal->sequence);
    raw[RUNTIME_JOURNAL_PHASE] = journal->phase;
    raw[RUNTIME_JOURNAL_KIND] = journal->kind;
    raw[RUNTIME_JOURNAL_SLOT] = journal->slot;
    raw[RUNTIME_JOURNAL_PROGRESS] = journal->progress;
    runtime_write_u16(raw + RUNTIME_JOURNAL_ENTRY_COUNT, journal->entry_count);
    runtime_encode_version(raw + RUNTIME_JOURNAL_BASE_VERSION,
                           &journal->base_version);
    runtime_write_u32(raw + RUNTIME_JOURNAL_BASE_EPOCH, journal->base_epoch);
    runtime_encode_version(raw + RUNTIME_JOURNAL_TARGET_VERSION,
                           &journal->target_version);
    runtime_write_u32(raw + RUNTIME_JOURNAL_TARGET_EPOCH,
                      journal->target_epoch);
    for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        runtime_encode_plan_entry(
            raw + RUNTIME_JOURNAL_ENTRY_OFFSET + index * RUNTIME_JOURNAL_ENTRY_SIZE,
            &journal->entries[index]);
    }
    runtime_state_hash(raw);
}

static int runtime_decode_journal(const uint8_t* raw,
                                  runtime_journal_t* journal) {
    if (!raw || !journal || !runtime_control_valid(raw) ||
        !runtime_magic_equal(raw, RUNTIME_JOURNAL_MAGIC) ||
        runtime_read_u16(raw + 4U) != RUNTIME_CONTROL_VERSION ||
        runtime_read_u16(raw + 6U) != UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U ||
        runtime_read_u16(raw + RUNTIME_JOURNAL_ENTRY_COUNT) > UPDATE_RUNTIME_MAX_ENTRIES ||
        raw[RUNTIME_JOURNAL_PHASE] > RUNTIME_PHASE_COMMITTED ||
        raw[RUNTIME_JOURNAL_KIND] > RUNTIME_JOURNAL_ROLLBACK) {
        return ERR_INVALID;
    }
    kmemset(journal, 0, sizeof(*journal));
    journal->sequence = runtime_read_u32(raw + RUNTIME_JOURNAL_SEQUENCE);
    journal->phase = raw[RUNTIME_JOURNAL_PHASE];
    journal->kind = raw[RUNTIME_JOURNAL_KIND];
    journal->slot = raw[RUNTIME_JOURNAL_SLOT];
    journal->progress = raw[RUNTIME_JOURNAL_PROGRESS];
    journal->entry_count = runtime_read_u16(raw + RUNTIME_JOURNAL_ENTRY_COUNT);
    runtime_decode_version(&journal->base_version,
                           raw + RUNTIME_JOURNAL_BASE_VERSION);
    journal->base_epoch = runtime_read_u32(raw + RUNTIME_JOURNAL_BASE_EPOCH);
    runtime_decode_version(&journal->target_version,
                           raw + RUNTIME_JOURNAL_TARGET_VERSION);
    journal->target_epoch = runtime_read_u32(raw + RUNTIME_JOURNAL_TARGET_EPOCH);
    for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        const uint8_t* entry_raw = raw + RUNTIME_JOURNAL_ENTRY_OFFSET +
                                   index * RUNTIME_JOURNAL_ENTRY_SIZE;
        if (!runtime_journal_old_state_raw_valid(
                entry_raw + RUNTIME_JOURNAL_ENTRY_OLD) ||
            !runtime_file_state_raw_valid(
                entry_raw + RUNTIME_JOURNAL_ENTRY_NEW)) {
            return ERR_INVALID;
        }
        runtime_decode_plan_entry(
            entry_raw, &journal->entries[index]);
    }
    for (uint32_t reserved = 40U; reserved < RUNTIME_JOURNAL_HEADER_SIZE;
         reserved++) {
        if (raw[reserved]) return ERR_INVALID;
    }
    if (journal->phase == RUNTIME_PHASE_NONE) {
        if (journal->kind != RUNTIME_JOURNAL_NONE ||
            journal->slot != RUNTIME_SLOT_NONE || journal->progress ||
            journal->entry_count) return ERR_INVALID;
        for (uint32_t reserved = RUNTIME_JOURNAL_BASE_VERSION;
             reserved < RUNTIME_JOURNAL_ENTRY_OFFSET; reserved++) {
            if (raw[reserved]) return ERR_INVALID;
        }
        for (uint32_t index = RUNTIME_JOURNAL_ENTRY_OFFSET;
             index < RUNTIME_JOURNAL_ENTRY_OFFSET +
                    UPDATE_RUNTIME_MAX_ENTRIES * RUNTIME_JOURNAL_ENTRY_SIZE;
             index++) {
            if (raw[index]) return ERR_INVALID;
        }
        return OK;
    }
    if ((journal->kind != RUNTIME_JOURNAL_APPLY &&
         journal->kind != RUNTIME_JOURNAL_ROLLBACK) ||
        journal->slot > 1U || journal->entry_count >
        sizeof(runtime_catalog) / sizeof(runtime_catalog[0]) ||
        journal->progress > journal->entry_count) return ERR_INVALID;
    for (uint32_t index = 0U; index < journal->entry_count; index++) {
        runtime_plan_entry_t* entry = &journal->entries[index];
        if (entry->catalog_index >=
                sizeof(runtime_catalog) / sizeof(runtime_catalog[0]) ||
            !entry->operation ||
            (entry->operation & ~(UPDATE_RUNTIME_OPERATION_REPLACE |
                                  UPDATE_RUNTIME_OPERATION_CREATE |
                                  UPDATE_RUNTIME_OPERATION_DELETE)) ||
            (entry->old_state.present && entry->new_state.present &&
             entry->operation != UPDATE_RUNTIME_OPERATION_REPLACE) ||
            (!entry->old_state.present && entry->new_state.present &&
             entry->operation != UPDATE_RUNTIME_OPERATION_CREATE) ||
            (entry->old_state.present && !entry->new_state.present &&
             entry->operation != UPDATE_RUNTIME_OPERATION_DELETE) ||
            (!entry->old_state.present && !entry->new_state.present)) {
            return ERR_INVALID;
        }
        for (uint32_t previous = 0U; previous < index; previous++) {
            if (journal->entries[previous].catalog_index ==
                entry->catalog_index) return ERR_INVALID;
        }
    }
    for (uint32_t index = RUNTIME_JOURNAL_ENTRY_OFFSET +
                           journal->entry_count * RUNTIME_JOURNAL_ENTRY_SIZE;
         index < RUNTIME_JOURNAL_ENTRY_OFFSET +
                UPDATE_RUNTIME_MAX_ENTRIES * RUNTIME_JOURNAL_ENTRY_SIZE;
         index++) {
        if (raw[index]) return ERR_INVALID;
    }
    return OK;
}

static int runtime_load_control(const char* alias, uint8_t* raw) {
    uint32_t file_size = 0U;
    uint32_t bytes = 0U;
    int result;

    if (!alias || !raw) return ERR_NULL;
    result = fs_get_root_file_info(alias, &file_size, 0);
    if (result == ERR_NOT_FOUND) return ERR_NOT_FOUND;
    if (result != OK || file_size != UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U) {
        return ERR_INVALID;
    }
    result = fs_read_file_range_at(alias, 0U, raw,
                                   UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U,
                                   &bytes);
    if (result != OK || bytes != UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U) {
        return ERR_INVALID;
    }
    return OK;
}

static int runtime_write_control(const char* alias, uint8_t* raw) {
    if (runtime_state_hash(raw) != OK) return ERR_INVALID;
    return fs_atomic_write_root(alias, raw,
                                UPDATE_RUNTIME_CACHE_RECORD_SIZE * 8U,
                                RUNTIME_RUNTIME_ATTRIBUTES,
                                FS_ATOMIC_CREATE_OR_REPLACE);
}

static int runtime_write_state(void) {
    runtime_state.sequence++;
    runtime_encode_state(runtime_control, &runtime_state);
    runtime_state_slot = runtime_state.sequence & 1U;
    return runtime_write_control(runtime_state_aliases[runtime_state_slot],
                                 runtime_control);
}

static int runtime_write_journal(void) {
    runtime_encode_journal(runtime_control, &runtime_journal);
    runtime_journal_slot = runtime_journal.sequence & 1U;
    return runtime_write_control(
        runtime_journal_aliases[runtime_journal_slot], runtime_control);
}

static int runtime_load_state_records(void) {
    runtime_state_t candidate;
    runtime_state_t selected;
    uint8_t found = 0U;
    uint8_t found_any = 0U;

    kmemset(&selected, 0, sizeof(selected));
    for (uint8_t slot = 0U; slot < 2U; slot++) {
        int load_result = runtime_load_control(runtime_state_aliases[slot],
                                                runtime_control);
        if (load_result != ERR_NOT_FOUND) found_any = 1U;
        if (load_result != OK ||
            runtime_decode_state(runtime_control, &candidate) != OK) continue;
        if (!found || candidate.sequence > selected.sequence) {
            selected = candidate;
            runtime_state_slot = slot;
            found = 1U;
        }
    }
    if (!found) return found_any ? ERR_INVALID : ERR_NOT_FOUND;
    runtime_state = selected;
    return OK;
}

static int runtime_load_journal_records(void) {
    runtime_journal_t candidate;
    runtime_journal_t selected;
    uint8_t found = 0U;
    uint8_t found_any = 0U;

    kmemset(&selected, 0, sizeof(selected));
    for (uint8_t slot = 0U; slot < 2U; slot++) {
        int load_result = runtime_load_control(runtime_journal_aliases[slot],
                                                runtime_control);
        if (load_result != ERR_NOT_FOUND) found_any = 1U;
        if (load_result != OK ||
            runtime_decode_journal(runtime_control, &candidate) != OK) continue;
        if (!found || candidate.sequence > selected.sequence) {
            selected = candidate;
            runtime_journal_slot = slot;
            found = 1U;
        }
    }
    if (!found && found_any) return ERR_INVALID;
    if (!found) {
        kmemset(&runtime_journal, 0, sizeof(runtime_journal));
        runtime_journal.kind = RUNTIME_JOURNAL_NONE;
        return OK;
    }
    runtime_journal = selected;
    return OK;
}

static int runtime_magic_equal(const uint8_t* raw, const char* magic) {
    if (!raw || !magic) return 0;
    return raw[0] == (uint8_t)magic[0] && raw[1] == (uint8_t)magic[1] &&
           raw[2] == (uint8_t)magic[2] && raw[3] == (uint8_t)magic[3];
}

static int runtime_fixed_text(const uint8_t* raw, uint32_t capacity,
                              char* output, uint32_t output_capacity) {
    uint32_t length = 0U;

    if (!raw || !output || !output_capacity || !capacity) return ERR_NULL;
    while (length < capacity && raw[length]) length++;
    if (length == capacity || length + 1U > output_capacity) return ERR_INVALID;
    for (uint32_t index = length + 1U; index < capacity; index++) {
        if (raw[index]) return ERR_INVALID;
    }
    kmemcpy(output, raw, length);
    output[length] = '\0';
    return OK;
}

static int runtime_identifier_valid(const char* value) {
    uint32_t length;

    if (!value || !value[0]) return 0;
    length = kstrlen(value);
    if (length >= UPDATE_RUNTIME_RELEASE_TAG_SIZE) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        char character = value[index];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '_' || character == '-')) {
            return 0;
        }
    }
    return 1;
}

static int runtime_manifest_signature(const uint8_t* raw) {
    crypto_ed25519_verify_ctx_t verify;
    int result;

    result = crypto_ed25519_verify_init(
        &verify, raw + RUNTIME_SIGNATURE_OFFSET,
        UPDATE_TRUST_PUBLIC_KEY);
    if (result == OK) {
        result = crypto_ed25519_verify_update(
            &verify, runtime_manifest_domain,
            sizeof(runtime_manifest_domain));
    }
    if (result == OK) {
        result = crypto_ed25519_verify_update(
            &verify, raw, UPDATE_RUNTIME_MANIFEST_SIGNED_SIZE);
    }
    if (result == OK) result = crypto_ed25519_verify_final(&verify);
    return result;
}

static int runtime_asset_name_valid(const uint8_t* raw, char* output) {
    uint32_t length = 0U;

    if (!raw || !output) return 0;
    while (length < UPDATE_RUNTIME_ASSET_NAME_SIZE && raw[length]) length++;
    if (!length || length >= UPDATE_RUNTIME_ASSET_NAME_SIZE) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        uint8_t value = raw[index];
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '-' ||
              value == '_' || value == '.')) return 0;
        output[index] = (char)value;
    }
    output[length] = '\0';
    for (uint32_t index = length + 1U;
         index < UPDATE_RUNTIME_ASSET_NAME_SIZE; index++) {
        if (raw[index]) return 0;
    }
    return 1;
}

static int runtime_manifest_entry_valid(const uint8_t* raw,
                                        update_runtime_entry_t* entry) {
    if (!raw || !entry) return ERR_NULL;
    kmemset(entry, 0, sizeof(*entry));
    if (!runtime_path_valid(raw, entry->path)) return ERR_INVALID;
    if (raw[64U] > 1U) return ERR_INVALID;
    entry->target_present = raw[64U] ? 1U : 0U;
    entry->allowed_operations = raw[65U];
    entry->flags = runtime_read_u16(raw + 66U);
    entry->target_size = runtime_read_u32(raw + 68U);
    kmemcpy(entry->target_hash, raw + 72U, 32U);
    if (entry->flags || (entry->allowed_operations &
                         ~(UPDATE_RUNTIME_OPERATION_REPLACE |
                           UPDATE_RUNTIME_OPERATION_CREATE |
                           UPDATE_RUNTIME_OPERATION_DELETE))) {
        return ERR_INVALID;
    }
    if (!entry->allowed_operations) return ERR_INVALID;
    if (!entry->target_present) {
        if (entry->target_size || !crypto_equal(entry->target_hash,
                                                runtime_zero_hash, 32U) ||
            entry->allowed_operations != UPDATE_RUNTIME_OPERATION_DELETE ||
            runtime_fixed_text(raw + 104U, UPDATE_RUNTIME_ASSET_NAME_SIZE,
                               entry->asset_name,
                               sizeof(entry->asset_name)) != OK) {
            return ERR_INVALID;
        }
        if (kstrcmp(entry->asset_name, entry->path) != 0) return ERR_INVALID;
        if (raw[168U] != 0U || raw[169U] != 0U || raw[170U] != 0U ||
            raw[171U] != 0U || !crypto_equal(raw + 172U,
                                              runtime_zero_hash, 32U)) {
            return ERR_INVALID;
        }
        for (uint32_t index = 204U; index < UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE;
             index++) {
            if (raw[index]) return ERR_INVALID;
        }
        return OK;
    }
    if (!entry->target_size || entry->target_size > UPDATE_RUNTIME_FILE_MAX_SIZE ||
        crypto_equal(entry->target_hash, runtime_zero_hash, 32U) != 0 ||
        !runtime_asset_name_valid(raw + 104U, entry->asset_name) ||
        kstrcmp(entry->asset_name, entry->path) != 0 ||
        runtime_read_u32(raw + 168U) != entry->target_size ||
        crypto_equal(raw + 172U, runtime_zero_hash, 32U) != 0) {
        return ERR_INVALID;
    }
    entry->asset_size = runtime_read_u32(raw + 168U);
    kmemcpy(entry->asset_hash, raw + 172U, 32U);
    if ((entry->allowed_operations &
         (UPDATE_RUNTIME_OPERATION_REPLACE |
          UPDATE_RUNTIME_OPERATION_CREATE)) == 0U) return ERR_INVALID;
    if (entry->allowed_operations & UPDATE_RUNTIME_OPERATION_DELETE) {
        return ERR_INVALID;
    }
    for (uint32_t index = 204U; index < UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE;
         index++) {
        if (raw[index]) return ERR_INVALID;
    }
    return OK;
}

static int runtime_manifest_applicable(const update_runtime_manifest_t* manifest) {
    update_version_t installed;
    uint32_t installed_epoch;
    uint8_t supported = 0U;

    if (!manifest || runtime_current_version(&installed, &installed_epoch) != OK) {
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < manifest->base_count; index++) {
        if (runtime_version_compare(&manifest->base_versions[index],
                                    &installed) == 0 &&
            manifest->base_epochs[index] == installed_epoch) {
            supported = 1U;
            break;
        }
    }
    if (!supported || manifest->target_epoch < installed_epoch ||
        (manifest->target_epoch == installed_epoch &&
         runtime_version_compare(&manifest->target_version, &installed) <= 0)) {
        return ERR_INVALID;
    }
    return OK;
}

int update_runtime_parse_manifest(const uint8_t* raw, uint32_t size,
                                  update_runtime_manifest_t* output,
                                  update_runtime_reason_t* reason_out) {
    uint8_t seen[UPDATE_RUNTIME_MAX_ENTRIES];
    int result;

    if (!raw || !output || !reason_out) {
        LOG_ERROR("UPDATE", "Argumento nulo ao analisar manifesto ZUM2");
        return ERR_NULL;
    }
    *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
    kmemset(output, 0, sizeof(*output));
    kmemset(seen, 0, sizeof(seen));
    if (size != UPDATE_RUNTIME_MANIFEST_SIZE ||
        !runtime_magic_equal(raw, RUNTIME_MANIFEST_MAGIC) ||
        runtime_read_u16(raw + 4U) != UPDATE_RUNTIME_FORMAT_VERSION ||
        runtime_read_u16(raw + 6U) != UPDATE_RUNTIME_MANIFEST_SIZE ||
        runtime_read_u16(raw + 8U) != UPDATE_RUNTIME_ARCH_I386 ||
        runtime_read_u16(raw + 10U) != 0U ||
        runtime_read_u16(raw + 28U) != UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE ||
        runtime_read_u16(raw + 30U) == 0U ||
        runtime_read_u16(raw + 30U) > UPDATE_RUNTIME_MANIFEST_MAX_BASES ||
        runtime_read_u16(raw + 26U) == 0U ||
        runtime_read_u16(raw + 26U) > UPDATE_RUNTIME_MAX_ENTRIES ||
        runtime_read_u32(raw + 32U) == 0U ||
        runtime_read_u32(raw + 32U) > UPDATE_RUNTIME_PACKAGE_MAX_SIZE) {
        LOG_ERROR("UPDATE", "Cabecalho ZUM2 invalido");
        return ERR_INVALID;
    }
    if (!crypto_equal(raw + 68U, UPDATE_TRUST_KEY_ID,
                      UPDATE_RUNTIME_KEY_ID_SIZE)) {
        *reason_out = UPDATE_RUNTIME_REASON_UNKNOWN_KEY;
        LOG_ERROR("UPDATE", "ZUM2 usa chave desconhecida");
        return ERR_INVALID;
    }
    if (runtime_manifest_signature(raw) != OK) {
        *reason_out = UPDATE_RUNTIME_REASON_SIGNATURE;
        LOG_ERROR("UPDATE", "Assinatura do ZUM2 invalida");
        return ERR_INVALID;
    }
    output->generation = runtime_read_u32(raw + 12U);
    runtime_decode_version(&output->target_version, raw + 16U);
    output->target_epoch = runtime_read_u32(raw + 22U);
    output->entry_count = runtime_read_u16(raw + 26U);
    output->base_count = runtime_read_u16(raw + 30U);
    output->package_size = runtime_read_u32(raw + 32U);
    kmemcpy(output->package_hash, raw + 36U, 32U);
    if (crypto_equal(output->package_hash, runtime_zero_hash, 32U) != 0 ||
        runtime_fixed_text(raw + RUNTIME_MANIFEST_TAG_OFFSET, 64U,
                           output->release_tag,
                           sizeof(output->release_tag)) != OK ||
        runtime_fixed_text(raw + RUNTIME_MANIFEST_ID_OFFSET, 64U,
                           output->release_id,
                           sizeof(output->release_id)) != OK ||
        !output->release_tag[0] || !output->release_id[0]) {
        *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
        return ERR_INVALID;
    }
    if (!runtime_identifier_valid(output->release_tag) ||
        !runtime_identifier_valid(output->release_id)) {
        *reason_out = UPDATE_RUNTIME_REASON_PATH_POLICY;
        return ERR_INVALID;
    }
    for (uint32_t reserved = 212U; reserved < UPDATE_RUNTIME_MANIFEST_BASE_OFFSET;
         reserved++) {
        if (raw[reserved]) {
            *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
            return ERR_INVALID;
        }
    }
    for (uint32_t index = 0U; index < output->base_count; index++) {
        const uint8_t* base = raw + UPDATE_RUNTIME_MANIFEST_BASE_OFFSET +
                              index * UPDATE_RUNTIME_MANIFEST_BASE_SIZE;
        runtime_decode_version(&output->base_versions[index], base);
        output->base_epochs[index] = runtime_read_u32(base + 6U);
        if (!runtime_version_is_newer(&output->target_version,
                                      output->target_epoch,
                                      &output->base_versions[index],
                                      output->base_epochs[index])) {
            *reason_out = UPDATE_RUNTIME_REASON_DOWNGRADE;
            return ERR_INVALID;
        }
        for (uint32_t reserved = 10U; reserved < UPDATE_RUNTIME_MANIFEST_BASE_SIZE;
             reserved++) {
            if (base[reserved]) return ERR_INVALID;
        }
        for (uint32_t previous = 0U; previous < index; previous++) {
            if (runtime_version_compare(&output->base_versions[previous],
                                        &output->base_versions[index]) == 0 &&
                output->base_epochs[previous] == output->base_epochs[index]) {
                *reason_out = UPDATE_RUNTIME_REASON_DUPLICATE_TARGET;
                return ERR_INVALID;
            }
        }
    }
    for (uint32_t offset = UPDATE_RUNTIME_MANIFEST_BASE_OFFSET +
                           output->base_count * UPDATE_RUNTIME_MANIFEST_BASE_SIZE;
         offset < UPDATE_RUNTIME_MANIFEST_ENTRY_OFFSET; offset++) {
        if (raw[offset]) {
            *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
            return ERR_INVALID;
        }
    }
    for (uint32_t index = 0U; index < output->entry_count; index++) {
        const uint8_t* entry_raw = raw + UPDATE_RUNTIME_MANIFEST_ENTRY_OFFSET +
                                   index * UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE;
        result = runtime_manifest_entry_valid(entry_raw, &output->entries[index]);
        if (result != OK) {
            *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
            return result;
        }
        int catalog = runtime_catalog_index(output->entries[index].path);
        if (catalog < 0) {
            *reason_out = UPDATE_RUNTIME_REASON_PATH_POLICY;
            return ERR_INVALID;
        }
        if (seen[catalog]) {
            *reason_out = UPDATE_RUNTIME_REASON_DUPLICATE_TARGET;
            return ERR_INVALID;
        }
        seen[catalog] = 1U;
        for (uint32_t padding = 204U; padding < UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE;
             padding++) {
            if (entry_raw[padding]) {
                *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
                return ERR_INVALID;
            }
        }
    }
    for (uint32_t offset = UPDATE_RUNTIME_MANIFEST_ENTRY_OFFSET +
                           output->entry_count * UPDATE_RUNTIME_MANIFEST_ENTRY_SIZE;
         offset < UPDATE_RUNTIME_MANIFEST_SIGNED_SIZE; offset++) {
        if (raw[offset]) {
            *reason_out = UPDATE_RUNTIME_REASON_FORMAT;
            return ERR_INVALID;
        }
    }
    for (uint32_t index = 0U;
         index < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]); index++) {
        if (!seen[index]) {
            *reason_out = UPDATE_RUNTIME_REASON_PATH_POLICY;
            return ERR_INVALID;
        }
    }
    *reason_out = UPDATE_RUNTIME_REASON_NONE;
    return OK;
}

static int runtime_package_signature(const char* path,
                                     const runtime_package_t* package) {
    crypto_ed25519_verify_ctx_t verify;
    uint32_t offset = 0U;
    uint32_t bytes;
    int result;

    if (!path || !package) return ERR_NULL;
    result = fs_read_file_range_at(path, package->signature_offset,
                                   runtime_signature,
                                   UPDATE_RUNTIME_SIGNATURE_SIZE, &bytes);
    if (result != OK || bytes != UPDATE_RUNTIME_SIGNATURE_SIZE) return ERR_DISK;
    result = crypto_ed25519_verify_init(
        &verify, runtime_signature, UPDATE_TRUST_PUBLIC_KEY);
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &verify, runtime_package_domain, sizeof(runtime_package_domain));
    while (result == OK && offset < package->signature_offset) {
        uint32_t chunk = package->signature_offset - offset;
        if (chunk > sizeof(runtime_io_buffer)) chunk = sizeof(runtime_io_buffer);
        result = fs_read_file_range_at(path, offset, runtime_io_buffer,
                                       chunk, &bytes);
        if (result == OK && bytes != chunk) result = ERR_DISK;
        if (result == OK) result = crypto_ed25519_verify_update(
            &verify, runtime_io_buffer, bytes);
        if (result == OK) offset += bytes;
    }
    if (result == OK) result = crypto_ed25519_verify_final(&verify);
    return result;
}

static int runtime_package_entry(const uint8_t* raw,
                                 runtime_package_entry_t* entry) {
    if (!raw || !entry) return ERR_NULL;
    kmemset(entry, 0, sizeof(*entry));
    if (!runtime_path_valid(raw, entry->path)) return ERR_INVALID;
    entry->payload_offset = runtime_read_u32(raw + RUNTIME_ENTRY_PAYLOAD_OFFSET);
    entry->payload_size = runtime_read_u32(raw + RUNTIME_ENTRY_PAYLOAD_SIZE);
    if (raw[RUNTIME_ENTRY_PRESENT] > 1U) return ERR_INVALID;
    entry->present = raw[RUNTIME_ENTRY_PRESENT] ? 1U : 0U;
    entry->operation = raw[RUNTIME_ENTRY_OPERATION];
    kmemcpy(entry->hash, raw + RUNTIME_ENTRY_HASH, 32U);
    if (entry->operation & ~(UPDATE_RUNTIME_OPERATION_REPLACE |
                             UPDATE_RUNTIME_OPERATION_CREATE |
                             UPDATE_RUNTIME_OPERATION_DELETE) ||
        !entry->operation ||
        (!entry->present &&
         (entry->operation != UPDATE_RUNTIME_OPERATION_DELETE ||
          entry->payload_offset || entry->payload_size ||
          !crypto_equal(entry->hash, runtime_zero_hash, 32U))) ||
        (entry->present &&
         (!entry->payload_size ||
          entry->payload_size > UPDATE_RUNTIME_FILE_MAX_SIZE ||
          crypto_equal(entry->hash, runtime_zero_hash, 32U) ||
          (entry->operation & UPDATE_RUNTIME_OPERATION_DELETE))) ||
        raw[74U] || raw[75U] ||
        raw[108U] || raw[109U] || raw[110U] || raw[111U] || raw[112U] ||
        raw[113U] || raw[114U] || raw[115U] || raw[116U] || raw[117U] ||
        raw[118U] || raw[119U] || raw[120U] || raw[121U] || raw[122U] ||
        raw[123U] || raw[124U] || raw[125U] || raw[126U] || raw[127U]) {
        return ERR_INVALID;
    }
    entry->valid = 1U;
    return OK;
}

static int runtime_parse_package_header(const char* path,
                                        runtime_package_t* package,
                                        uint32_t* file_size_out) {
    uint8_t header[UPDATE_RUNTIME_PACKAGE_HEADER_SIZE];
    uint32_t file_size;
    uint32_t bytes;
    uint32_t table_end;
    uint32_t next_payload;
    uint8_t seen[UPDATE_RUNTIME_MAX_ENTRIES];
    int result;

    if (!path || !package) return ERR_NULL;
    if (fs_get_root_file_info(path, &file_size, 0) != OK ||
        file_size < UPDATE_RUNTIME_PACKAGE_HEADER_SIZE +
                    UPDATE_RUNTIME_PACKAGE_ENTRY_SIZE +
                    UPDATE_RUNTIME_SIGNATURE_SIZE ||
        file_size > UPDATE_RUNTIME_PACKAGE_MAX_SIZE) return ERR_INVALID;
    result = fs_read_file_range_at(path, 0U, header, sizeof(header), &bytes);
    if (result != OK || bytes != sizeof(header)) return ERR_DISK;
    if (!runtime_magic_equal(header, RUNTIME_PACKAGE_MAGIC) ||
        runtime_read_u16(header + 4U) != UPDATE_RUNTIME_FORMAT_VERSION ||
        runtime_read_u16(header + 6U) != UPDATE_RUNTIME_PACKAGE_HEADER_SIZE ||
        runtime_read_u16(header + 8U) != UPDATE_RUNTIME_ARCH_I386 ||
        runtime_read_u16(header + 10U) != 0U ||
        runtime_read_u32(header + RUNTIME_PACKAGE_FLAGS) != 0U ||
        runtime_read_u32(header + 16U) != file_size ||
        runtime_read_u16(header + RUNTIME_PACKAGE_ENTRY_SIZE) !=
            UPDATE_RUNTIME_PACKAGE_ENTRY_SIZE ||
        runtime_read_u32(header + RUNTIME_PACKAGE_SIGNATURE_SIZE) !=
            UPDATE_RUNTIME_SIGNATURE_SIZE ||
        runtime_read_u16(header + 54U) != 0U ||
        runtime_read_u16(header + 72U) != 0U ||
        runtime_read_u16(header + RUNTIME_PACKAGE_ENTRY_COUNT) == 0U ||
        runtime_read_u16(header + RUNTIME_PACKAGE_ENTRY_COUNT) >
            UPDATE_RUNTIME_MAX_ENTRIES ||
            !crypto_equal(header + RUNTIME_PACKAGE_KEY_ID,
                      UPDATE_TRUST_KEY_ID, UPDATE_RUNTIME_KEY_ID_SIZE)) {
        return ERR_INVALID;
    }
    for (uint32_t reserved = 84U; reserved < UPDATE_RUNTIME_PACKAGE_HEADER_SIZE;
         reserved++) {
        if (header[reserved]) return ERR_INVALID;
    }
    kmemset(package, 0, sizeof(*package));
    kmemset(seen, 0, sizeof(seen));
    package->generation = runtime_read_u32(header + RUNTIME_PACKAGE_GENERATION);
    runtime_decode_version(&package->target_version,
                           header + RUNTIME_PACKAGE_TARGET_VERSION);
    package->target_epoch = runtime_read_u32(header + RUNTIME_PACKAGE_TARGET_EPOCH);
    package->entry_count = runtime_read_u16(header + RUNTIME_PACKAGE_ENTRY_COUNT);
    package->payload_offset = runtime_read_u32(
        header + RUNTIME_PACKAGE_PAYLOAD_OFFSET);
    package->payload_size = runtime_read_u32(header + RUNTIME_PACKAGE_PAYLOAD_SIZE);
    package->signature_offset = runtime_read_u32(
        header + RUNTIME_PACKAGE_SIGNATURE_OFFSET);
    package->signature_size = runtime_read_u32(
        header + RUNTIME_PACKAGE_SIGNATURE_SIZE);
    runtime_decode_version(&package->base_version,
                           header + RUNTIME_PACKAGE_BASE_VERSION);
    package->base_epoch = runtime_read_u32(header + RUNTIME_PACKAGE_BASE_EPOCH);
    kmemcpy(package->key_id, header + RUNTIME_PACKAGE_KEY_ID, 16U);
    table_end = UPDATE_RUNTIME_PACKAGE_HEADER_SIZE +
                package->entry_count * UPDATE_RUNTIME_PACKAGE_ENTRY_SIZE;
    if (package->payload_offset != table_end ||
        package->signature_offset != package->payload_offset +
                                     package->payload_size ||
        package->signature_offset + package->signature_size != file_size ||
        package->payload_size > UPDATE_RUNTIME_PACKAGE_MAX_SIZE) return ERR_INVALID;
    next_payload = package->payload_offset;
    for (uint32_t index = 0U; index < package->entry_count; index++) {
        uint8_t entry_raw[UPDATE_RUNTIME_PACKAGE_ENTRY_SIZE];
        int catalog;
        result = fs_read_file_range_at(
            path, UPDATE_RUNTIME_PACKAGE_HEADER_SIZE +
                  index * UPDATE_RUNTIME_PACKAGE_ENTRY_SIZE,
            entry_raw, sizeof(entry_raw), &bytes);
        if (result != OK || bytes != sizeof(entry_raw) ||
            runtime_package_entry(entry_raw, &package->entries[index]) != OK) {
            return ERR_INVALID;
        }
        catalog = runtime_catalog_index(package->entries[index].path);
        if (catalog < 0 || seen[catalog]) return ERR_INVALID;
        seen[catalog] = 1U;
        if (package->entries[index].present) {
            if (package->entries[index].payload_offset < package->payload_offset ||
                package->entries[index].payload_offset +
                    package->entries[index].payload_size > package->signature_offset) {
                return ERR_INVALID;
            }
            if (package->entries[index].payload_offset != next_payload) {
                return ERR_INVALID;
            }
            next_payload += package->entries[index].payload_size;
        } else if (package->entries[index].payload_offset != 0U ||
                   package->entries[index].payload_size != 0U) {
            return ERR_INVALID;
        }
        for (uint32_t previous = 0U; previous < index; previous++) {
            if (kstrcmp(package->entries[previous].path,
                        package->entries[index].path) == 0) return ERR_INVALID;
        }
    }
    for (uint32_t catalog = 0U;
         catalog < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]);
         catalog++) {
        if (!seen[catalog]) return ERR_INVALID;
    }
    if (next_payload != package->signature_offset) return ERR_INVALID;
    if (file_size_out) *file_size_out = file_size;
    return OK;
}

static int runtime_verify_package_payloads(const char* path,
                                           const runtime_package_t* package) {
    uint8_t hash[32];

    for (uint32_t index = 0U; index < package->entry_count; index++) {
        const runtime_package_entry_t* entry = &package->entries[index];
        if (!entry->present) continue;
        if (runtime_hash_file_range(path, entry->payload_offset,
                                    entry->payload_size, hash) != OK ||
            !crypto_equal(hash, entry->hash, 32U)) return ERR_INVALID;
    }
    return OK;
}

static int runtime_hash_file_range(const char* path, uint32_t offset,
                                   uint32_t size, uint8_t hash[32]) {
    crypto_sha256_ctx_t sha;
    uint32_t bytes;
    int result;

    if (!path || !hash) return ERR_NULL;
    result = crypto_sha256_init(&sha);
    while (result == OK && size) {
        uint32_t chunk = size > sizeof(runtime_io_buffer) ?
                         sizeof(runtime_io_buffer) : size;
        result = fs_read_file_range_at(path, offset, runtime_io_buffer,
                                       chunk, &bytes);
        if (result == OK && bytes != chunk) result = ERR_DISK;
        if (result == OK) result = crypto_sha256_update(&sha, runtime_io_buffer,
                                                        bytes);
        if (result == OK) {
            offset += bytes;
            size -= bytes;
        }
    }
    if (result == OK) result = crypto_sha256_final(&sha, hash);
    return result;
}

static int runtime_current_version(update_version_t* version,
                                   uint32_t* epoch) {
    if (!version || !epoch) return ERR_NULL;
    if (runtime_state_healthy) {
        *version = runtime_state.installed_version;
        *epoch = runtime_state.installed_epoch;
        return OK;
    }
    return update_get_installed_version(version, epoch);
}

static int runtime_legacy_transaction_pending(void) {
    update_status_t legacy_status;

    if (update_get_status(&legacy_status) != OK) {
        LOG_ERROR("UPDATE", "Estado U3 nao pode ser consultado pelo runtime v2");
        return 1;
    }
    return legacy_status.transaction_pending ? 1 : 0;
}

static int runtime_initial_version(update_version_t* version,
                                   uint32_t* epoch) {
    if (!version || !epoch) return ERR_NULL;
    if (update_get_installed_version(version, epoch) == OK) return OK;
    if (runtime_legacy_transaction_pending()) {
        LOG_ERROR("UPDATE", "Runtime v2 recusado durante transacao U3 pendente");
        return ERR_STATE;
    }
    version->major = ZEPHYROS_VERSION_MAJOR;
    version->minor = ZEPHYROS_VERSION_MINOR;
    version->patch = ZEPHYROS_VERSION_PATCH;
    *epoch = ZEPHYROS_VERSION_EPOCH;
    LOG_WARN("UPDATE", "Runtime v2 usando versao compilada; estado U3 degradado");
    return OK;
}

static int runtime_refresh_current_files(runtime_file_state_t* output) {
    if (!output) return ERR_NULL;
    for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        kmemset(&output[index], 0, sizeof(output[index]));
    }
    for (uint32_t index = 0U;
         index < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]); index++) {
        int result = runtime_read_file_state(runtime_catalog[index],
                                             &output[index]);
        if (result != OK) return result;
    }
    return OK;
}

static int runtime_state_seed(void) {
    update_version_t installed;
    uint32_t installed_epoch;
    int result;

    kmemset(&runtime_state, 0, sizeof(runtime_state));
    if (runtime_initial_version(&installed, &installed_epoch) != OK) {
        LOG_ERROR("UPDATE", "Versao instalada indisponivel para runtime v2");
        return ERR_STATE;
    }
    runtime_state.sequence = 0U;
    runtime_state.installed_version = installed;
    runtime_state.installed_epoch = installed_epoch;
    runtime_state.entry_count =
        (uint16_t)(sizeof(runtime_catalog) / sizeof(runtime_catalog[0]));
    result = runtime_refresh_current_files(runtime_state.current);
    if (result != OK) return result;
    runtime_state.rollback_available = 0U;
    runtime_state.rollback_slot = RUNTIME_SLOT_NONE;
    return runtime_write_state();
}

static int runtime_state_healthy_now(void) {
    return runtime_initialized && runtime_state_healthy &&
           runtime_journal.kind == RUNTIME_JOURNAL_NONE;
}

static int runtime_manifest_find(const update_runtime_manifest_t* manifest,
                                 uint32_t catalog_index) {
    if (!manifest) return -1;
    for (uint32_t index = 0U; index < manifest->entry_count; index++) {
        if (runtime_catalog_index(manifest->entries[index].path) ==
            (int)catalog_index) return (int)index;
    }
    return -1;
}

static int runtime_manifest_plan(const update_runtime_manifest_t* manifest,
                                 runtime_plan_entry_t* plan,
                                 uint16_t* count_out,
                                 uint16_t* reused_out,
                                 uint16_t* missing_out) {
    runtime_file_state_t current[UPDATE_RUNTIME_MAX_ENTRIES];
    uint16_t count = 0U;
    uint16_t reused = 0U;
    uint16_t missing = 0U;
    int result;

    if (!manifest || !plan || !count_out) return ERR_NULL;
    if (runtime_manifest_applicable(manifest) != OK) return ERR_INVALID;
    result = runtime_refresh_current_files(current);
    if (result != OK) return result;
    for (uint32_t catalog = 0U;
         catalog < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]); catalog++) {
        int manifest_index = runtime_manifest_find(manifest, catalog);
        const update_runtime_entry_t* desired;
        runtime_plan_entry_t* item;

        if (manifest_index < 0) return ERR_INVALID;
        desired = &manifest->entries[manifest_index];
        if (current[catalog].present == desired->target_present &&
            (!desired->target_present ||
             (current[catalog].size == desired->target_size &&
              crypto_equal(current[catalog].hash, desired->target_hash, 32U)))) {
            reused++;
            continue;
        }
        if (count >= UPDATE_RUNTIME_MAX_ENTRIES) return ERR_OVERFLOW;
        item = &plan[count++];
        kmemset(item, 0, sizeof(*item));
        item->catalog_index = (uint8_t)catalog;
        item->old_state = current[catalog];
        item->new_state.present = desired->target_present;
        item->new_state.size = desired->target_present ? desired->target_size : 0U;
        if (desired->target_present) {
            kmemcpy(item->new_state.hash, desired->target_hash, 32U);
        }
        if (current[catalog].present && desired->target_present) {
            item->operation = UPDATE_RUNTIME_OPERATION_REPLACE;
        } else if (desired->target_present) {
            item->operation = UPDATE_RUNTIME_OPERATION_CREATE;
        } else {
            item->operation = UPDATE_RUNTIME_OPERATION_DELETE;
        }
        if ((desired->allowed_operations & item->operation) == 0U) {
            LOG_ERROR("UPDATE", "Operacao runtime nao autorizada pelo manifesto");
            return ERR_INVALID;
        }
        if (item->operation != UPDATE_RUNTIME_OPERATION_DELETE) missing++;
    }
    *count_out = count;
    if (reused_out) *reused_out = reused;
    if (missing_out) *missing_out = missing;
    return OK;
}

static int runtime_package_plan(const runtime_package_t* package,
                                const update_runtime_manifest_t* manifest,
                                runtime_plan_entry_t* plan,
                                uint16_t* count_out) {
    runtime_file_state_t current[UPDATE_RUNTIME_MAX_ENTRIES];
    uint8_t seen[UPDATE_RUNTIME_MAX_ENTRIES];
    update_version_t installed;
    uint32_t installed_epoch;
    uint16_t count = 0U;

    if (!package || !plan || !count_out) return ERR_NULL;
    kmemset(seen, 0, sizeof(seen));
    if (runtime_refresh_current_files(current) != OK ||
        runtime_current_version(&installed, &installed_epoch) != OK) {
        return ERR_INVALID;
    }
    if (manifest) {
        uint8_t supported = 0U;
        uint8_t package_base_declared = 0U;

        if (package->generation != manifest->generation ||
            runtime_version_compare(&package->target_version,
                                    &manifest->target_version) != 0 ||
            package->target_epoch != manifest->target_epoch) return ERR_INVALID;
        for (uint32_t base = 0U; base < manifest->base_count; base++) {
            if (runtime_version_compare(&manifest->base_versions[base],
                                        &package->base_version) == 0 &&
                manifest->base_epochs[base] == package->base_epoch) {
                package_base_declared = 1U;
                break;
            }
        }
        if (!package_base_declared) return ERR_INVALID;
        for (uint32_t base = 0U; base < manifest->base_count; base++) {
            if (runtime_version_compare(&manifest->base_versions[base],
                                        &installed) == 0 &&
                manifest->base_epochs[base] == installed_epoch) {
                supported = 1U;
                break;
            }
        }
        if (!supported || package->target_epoch < installed_epoch ||
            (package->target_epoch == installed_epoch &&
             runtime_version_compare(&package->target_version,
                                     &installed) <= 0)) return ERR_INVALID;
    } else if (runtime_version_compare(&package->base_version, &installed) != 0 ||
               package->base_epoch != installed_epoch ||
               package->target_epoch < installed_epoch ||
               (package->target_epoch == installed_epoch &&
                runtime_version_compare(&package->target_version,
                                        &installed) <= 0)) {
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < package->entry_count; index++) {
        const runtime_package_entry_t* source = &package->entries[index];
        int catalog = runtime_catalog_index(source->path);
        runtime_plan_entry_t* item;

        if (manifest) {
            int manifest_index;
            const update_runtime_entry_t* desired;

            if (catalog < 0) return ERR_INVALID;
            manifest_index = runtime_manifest_find(manifest,
                                                   (uint32_t)catalog);
            if (manifest_index < 0) return ERR_INVALID;
            desired = &manifest->entries[manifest_index];
            if (desired->target_present != source->present ||
                desired->target_size != source->payload_size ||
                !crypto_equal(desired->target_hash, source->hash, 32U) ||
                !(desired->allowed_operations & source->operation)) {
                return ERR_INVALID;
            }
        }

        if (catalog < 0 || seen[catalog]) return ERR_INVALID;
        seen[catalog] = 1U;
        if (current[catalog].present == source->present &&
            (!source->present ||
             (current[catalog].size == source->payload_size &&
              crypto_equal(current[catalog].hash, source->hash, 32U)))) continue;
        if (count >= UPDATE_RUNTIME_MAX_ENTRIES) return ERR_OVERFLOW;
        item = &plan[count++];
        kmemset(item, 0, sizeof(*item));
        item->catalog_index = (uint8_t)catalog;
        item->old_state = current[catalog];
        item->new_state.present = source->present;
        item->new_state.size = source->present ? source->payload_size : 0U;
        kmemcpy(item->new_state.hash, source->hash, 32U);
        item->operation = current[catalog].present && source->present ?
                          UPDATE_RUNTIME_OPERATION_REPLACE :
                          source->present ? UPDATE_RUNTIME_OPERATION_CREATE :
                          UPDATE_RUNTIME_OPERATION_DELETE;
        if ((source->operation & item->operation) == 0U) return ERR_INVALID;
    }
    for (uint32_t index = 0U;
         index < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]); index++) {
        if (!seen[index]) return ERR_INVALID;
    }
    *count_out = count;
    return OK;
}

static int runtime_read_package_entry(const char* path,
                                      const runtime_package_entry_t* entry) {
    uint32_t bytes;
    uint8_t hash[32];
    int result;

    if (!path || !entry || !entry->present) return ERR_INVALID;
    if (entry->payload_size > sizeof(runtime_io_buffer)) return ERR_OVERFLOW;
    result = fs_read_file_range_at(path, entry->payload_offset,
                                   runtime_io_buffer, entry->payload_size,
                                   &bytes);
    if (result != OK || bytes != entry->payload_size) return ERR_DISK;
    result = crypto_sha256(runtime_io_buffer, bytes, hash);
    if (result != OK || !crypto_equal(hash, entry->hash, 32U)) return ERR_INVALID;
    return OK;
}

static int runtime_read_cache_entry(const char* alias,
                                    const update_runtime_entry_t* entry) {
    uint32_t bytes;
    uint8_t hash[32];
    int result;

    if (!alias || !entry || !entry->target_present || !entry->asset_size) {
        return ERR_INVALID;
    }
    if (entry->asset_size > sizeof(runtime_io_buffer)) return ERR_OVERFLOW;
    result = fs_read_file_at(alias, runtime_io_buffer,
                             sizeof(runtime_io_buffer));
    if (result < 0 || (uint32_t)result != entry->asset_size) return ERR_DISK;
    bytes = (uint32_t)result;
    result = crypto_sha256(runtime_io_buffer, bytes, hash);
    if (result != OK || !crypto_equal(hash, entry->asset_hash, 32U) ||
        entry->asset_size != entry->target_size) return ERR_INVALID;
    return OK;
}

static int runtime_write_stage(uint8_t slot, uint8_t catalog_index,
                               const uint8_t* data, uint32_t size) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];

    if (!data || !size || size > sizeof(runtime_io_buffer)) return ERR_INVALID;
    runtime_alias(alias, slot, catalog_index, runtime_stage_prefixes[slot]);
    return fs_atomic_write_root(alias, data, size, RUNTIME_RUNTIME_ATTRIBUTES,
                                FS_ATOMIC_CREATE_OR_REPLACE);
}

static int runtime_copy_current_to_backup(uint8_t slot, uint8_t catalog_index,
                                          const runtime_file_state_t* state) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    int result;

    if (!state || !state->present) return OK;
    if (state->size > sizeof(runtime_io_buffer)) return ERR_OVERFLOW;
    result = fs_read_file_at(runtime_catalog[catalog_index], runtime_io_buffer,
                             sizeof(runtime_io_buffer));
    if (result < 0 || (uint32_t)result != state->size) return ERR_DISK;
    runtime_alias(alias, slot, catalog_index, runtime_backup_prefixes[slot]);
    return fs_atomic_write_root(alias, runtime_io_buffer, state->size,
                                RUNTIME_RUNTIME_ATTRIBUTES,
                                FS_ATOMIC_CREATE_OR_REPLACE);
}

static int runtime_clear_slot(uint8_t slot) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    int result = OK;

    for (uint8_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        runtime_alias(alias, slot, index, runtime_stage_prefixes[slot]);
        if (runtime_delete_root(alias) != OK) result = ERR_DISK;
        runtime_alias(alias, slot, index, runtime_backup_prefixes[slot]);
        if (runtime_delete_root(alias) != OK) result = ERR_DISK;
    }
    return result;
}

static int runtime_clear_stage(uint8_t slot) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    int result = OK;

    for (uint8_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        runtime_alias(alias, slot, index, runtime_stage_prefixes[slot]);
        if (runtime_delete_root(alias) != OK) result = ERR_DISK;
    }
    return result;
}

static int runtime_write_target_from_stage(uint8_t slot, uint8_t catalog_index,
                                           const runtime_file_state_t* expected) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    uint32_t bytes;
    uint8_t hash[32];
    int result;

    if (!expected) return ERR_NULL;
    if (!expected->present) {
        result = runtime_delete_root(runtime_catalog[catalog_index]);
        return result;
    }
    runtime_alias(alias, slot, catalog_index, runtime_stage_prefixes[slot]);
    result = fs_read_file_at(alias, runtime_io_buffer, sizeof(runtime_io_buffer));
    if (result < 0 || (uint32_t)result != expected->size) return ERR_DISK;
    bytes = (uint32_t)result;
    result = fs_atomic_write_root(runtime_catalog[catalog_index], runtime_io_buffer,
                                  bytes, FS_ATTRIBUTE_ARCHIVE,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return result;
    result = runtime_hash_file(runtime_catalog[catalog_index], bytes, hash);
    return result == OK && crypto_equal(hash, expected->hash, 32U) ?
           OK : ERR_INVALID;
}

static int runtime_restore_old_entry(const runtime_plan_entry_t* entry,
                                     uint8_t slot) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    runtime_file_state_t current;
    int result;

    if (!entry || entry->catalog_index >=
            sizeof(runtime_catalog) / sizeof(runtime_catalog[0])) {
        return ERR_INVALID;
    }
    result = runtime_read_file_state(runtime_catalog[entry->catalog_index],
                                     &current);
    if (result != OK) return result;
    if (runtime_states_equal(&current, &entry->old_state)) return OK;
    if (!entry->old_state.present) {
        return runtime_delete_root(runtime_catalog[entry->catalog_index]);
    }
    runtime_alias(alias, slot, entry->catalog_index,
                  runtime_backup_prefixes[slot]);
    result = fs_read_file_at(alias, runtime_io_buffer, sizeof(runtime_io_buffer));
    if (result < 0 || (uint32_t)result != entry->old_state.size) return ERR_DISK;
    result = fs_atomic_write_root(runtime_catalog[entry->catalog_index],
                                  runtime_io_buffer, entry->old_state.size,
                                  FS_ATTRIBUTE_ARCHIVE,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return result;
    {
        uint8_t hash[32];
        result = runtime_hash_file(runtime_catalog[entry->catalog_index],
                                   entry->old_state.size, hash);
        return result == OK && crypto_equal(hash, entry->old_state.hash, 32U) ?
               OK : ERR_INVALID;
    }
}

static int runtime_restore_new_entry(const runtime_plan_entry_t* entry,
                                     uint8_t slot) {
    char alias[UPDATE_RUNTIME_CACHE_ALIAS_SIZE];
    runtime_file_state_t current;
    uint8_t hash[32];
    int result;

    if (!entry || entry->catalog_index >=
            sizeof(runtime_catalog) / sizeof(runtime_catalog[0])) {
        return ERR_INVALID;
    }
    result = runtime_read_file_state(runtime_catalog[entry->catalog_index],
                                     &current);
    if (result != OK) return result;
    if (runtime_states_equal(&current, &entry->new_state)) return OK;
    if (!entry->new_state.present) {
        return runtime_delete_root(runtime_catalog[entry->catalog_index]);
    }
    runtime_alias(alias, slot, entry->catalog_index,
                  runtime_backup_prefixes[slot]);
    result = fs_read_file_at(alias, runtime_io_buffer, sizeof(runtime_io_buffer));
    if (result < 0 || (uint32_t)result != entry->new_state.size) return ERR_DISK;
    result = fs_atomic_write_root(runtime_catalog[entry->catalog_index],
                                  runtime_io_buffer, entry->new_state.size,
                                  FS_ATTRIBUTE_ARCHIVE,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result != OK) return result;
    result = runtime_hash_file(runtime_catalog[entry->catalog_index],
                               entry->new_state.size, hash);
    return result == OK && crypto_equal(hash, entry->new_state.hash, 32U) ?
           OK : ERR_INVALID;
}

static int runtime_clear_journal(void) {
    runtime_journal_t clean;

    kmemset(&clean, 0, sizeof(clean));
    clean.sequence = runtime_journal.sequence + 1U;
    clean.kind = RUNTIME_JOURNAL_NONE;
    clean.phase = RUNTIME_PHASE_NONE;
    clean.slot = RUNTIME_SLOT_NONE;
    runtime_journal = clean;
    return runtime_write_journal();
}

static int runtime_commit_state_from_journal(void) {
    runtime_state_t next = runtime_state;
    runtime_state_t previous = runtime_state;
    int previous_slot = runtime_state_slot;

    next.installed_version = runtime_journal.target_version;
    next.installed_epoch = runtime_journal.target_epoch;
    next.entry_count =
        (uint16_t)(sizeof(runtime_catalog) / sizeof(runtime_catalog[0]));
    next.rollback_available = runtime_journal.kind == RUNTIME_JOURNAL_APPLY &&
                              runtime_journal.entry_count != 0U;
    next.rollback_slot = next.rollback_available ? runtime_journal.slot :
                         RUNTIME_SLOT_NONE;
    next.rollback_entry_count = next.rollback_available ?
                                runtime_journal.entry_count : 0U;
    kmemset(next.rollback, 0, sizeof(next.rollback));
    if (next.rollback_available) {
        next.previous_version = runtime_journal.base_version;
        next.previous_epoch = runtime_journal.base_epoch;
    } else {
        kmemset(&next.previous_version, 0, sizeof(next.previous_version));
        next.previous_epoch = 0U;
    }
    for (uint32_t index = 0U; index < UPDATE_RUNTIME_MAX_ENTRIES; index++) {
        if (index < runtime_journal.entry_count) {
            next.current[runtime_journal.entries[index].catalog_index] =
                runtime_journal.entries[index].new_state;
            next.rollback[runtime_journal.entries[index].catalog_index] =
                runtime_journal.entries[index].old_state;
        }
    }
    if (runtime_refresh_current_files(next.current) != OK) return ERR_DISK;
    runtime_state = next;
    if (runtime_write_state() != OK) {
        runtime_state = previous;
        runtime_state_slot = previous_slot;
        return ERR_DISK;
    }
    return OK;
}

static int runtime_recover_pending(void) {
    uint8_t slot;
    uint8_t kind;
    uint8_t phase;
    int result = OK;

    if (runtime_journal.kind == RUNTIME_JOURNAL_NONE) return OK;
    slot = runtime_journal.slot;
    kind = runtime_journal.kind;
    phase = runtime_journal.phase;
    LOG_WARN("UPDATE", "Journal runtime v2 pendente encontrado");
    if (runtime_journal.phase == RUNTIME_PHASE_COMMITTED) {
        result = runtime_commit_state_from_journal();
    } else if (runtime_journal.kind == RUNTIME_JOURNAL_APPLY) {
        for (uint32_t index = 0U; index < runtime_journal.entry_count; index++) {
            result = runtime_restore_old_entry(&runtime_journal.entries[index],
                                               runtime_journal.slot);
            if (result != OK) break;
        }
    } else if (runtime_journal.kind == RUNTIME_JOURNAL_ROLLBACK) {
        for (uint32_t index = 0U; index < runtime_journal.entry_count; index++) {
            result = runtime_restore_new_entry(&runtime_journal.entries[index],
                                               runtime_journal.slot);
            if (result != OK) break;
        }
        if (result == OK) result = runtime_commit_state_from_journal();
    } else {
        result = ERR_INVALID;
    }
    if (result != OK) {
        LOG_ERROR("UPDATE", "Recuperacao runtime v2 falhou");
        return result;
    }
    if (runtime_clear_journal() != OK) return ERR_DISK;
    if (runtime_clear_stage(slot) != OK) return ERR_DISK;
    if (kind == RUNTIME_JOURNAL_ROLLBACK ||
        (kind == RUNTIME_JOURNAL_APPLY && phase != RUNTIME_PHASE_COMMITTED)) {
        return runtime_clear_slot(slot);
    }
    return OK;
}

static int runtime_action_fail(update_runtime_action_result_t* output,
                               update_runtime_reason_t reason, int error,
                               const char* message) {
    if (output) output->reason = reason;
    if (message) LOG_ERROR("UPDATE", message);
    return error;
}

static int runtime_verification_fail(update_runtime_verification_t* output,
                                     update_runtime_reason_t reason, int error,
                                     const char* message) {
    if (output) output->reason = reason;
    if (message) LOG_ERROR("UPDATE", message);
    return error;
}

static int runtime_cancelled(const update_runtime_action_options_t* options) {
    return options && options->cancel_check &&
           options->cancel_check(options->cancel_context);
}

static void runtime_fill_action_result(update_runtime_action_result_t* output,
                                       const update_version_t* from,
                                       uint32_t from_epoch,
                                       const update_version_t* to,
                                       uint32_t to_epoch, uint16_t count) {
    if (!output) return;
    output->from_version = *from;
    output->from_epoch = from_epoch;
    output->to_version = *to;
    output->to_epoch = to_epoch;
    output->entry_count = count;
    output->reboot_required = 1U;
}

static int runtime_space_available(const runtime_plan_entry_t* plan,
                                   uint16_t count) {
    fs_info_t info;
    uint32_t required = 0U;
    uint32_t cluster_size;

    if (!plan || fs_get_info(&info) != OK || info.type != FS_TYPE_FAT12 ||
        !info.bytes_per_sector || !info.sectors_per_cluster) return 0;
    cluster_size = info.bytes_per_sector * info.sectors_per_cluster;
    for (uint32_t index = 0U; index < count; index++) {
        if (plan[index].old_state.present) required += plan[index].old_state.size;
        if (plan[index].new_state.present) required += plan[index].new_state.size;
    }
    required += UPDATE_RUNTIME_CACHE_RECORD_SIZE * 2U;
    required = (required + cluster_size - 1U) / cluster_size;
    return info.free_clusters >= required + 2U;
}

static int runtime_prepare_journal(const runtime_plan_entry_t* plan,
                                   uint16_t count, uint8_t kind, uint8_t slot,
                                   const update_version_t* target,
                                   uint32_t target_epoch) {
    update_version_t installed;
    uint32_t installed_epoch;

    if (!plan || !target || count > UPDATE_RUNTIME_MAX_ENTRIES ||
        runtime_current_version(&installed, &installed_epoch) != OK) return ERR_STATE;
    kmemset(&runtime_journal, 0, sizeof(runtime_journal));
    runtime_journal.sequence = runtime_state.sequence + 1U;
    runtime_journal.phase = RUNTIME_PHASE_PREPARED;
    runtime_journal.kind = kind;
    runtime_journal.slot = slot;
    runtime_journal.entry_count = count;
    runtime_journal.base_version = installed;
    runtime_journal.base_epoch = installed_epoch;
    runtime_journal.target_version = *target;
    runtime_journal.target_epoch = target_epoch;
    for (uint32_t index = 0U; index < count; index++) {
        runtime_journal.entries[index] = plan[index];
    }
    return runtime_write_journal();
}

static int runtime_stage_package(const char* path,
                                 const runtime_package_t* package,
                                 const runtime_plan_entry_t* plan,
                                 uint16_t count, uint8_t slot,
                                 const update_runtime_action_options_t* options) {
    for (uint32_t index = 0U; index < count; index++) {
        const runtime_plan_entry_t* item = &plan[index];
        if (runtime_cancelled(options)) return ERR_TIMEOUT;
        if (runtime_copy_current_to_backup(slot, item->catalog_index,
                                           &item->old_state) != OK) return ERR_DISK;
        if (!item->new_state.present) continue;
        int package_index = -1;
        for (uint32_t entry = 0U; entry < package->entry_count; entry++) {
            if (runtime_catalog_index(package->entries[entry].path) ==
                item->catalog_index) {
                package_index = (int)entry;
                break;
            }
        }
        if (package_index < 0 ||
            runtime_read_package_entry(path, &package->entries[package_index]) != OK ||
            runtime_write_stage(slot, item->catalog_index, runtime_io_buffer,
                                item->new_state.size) != OK) return ERR_INVALID;
    }
    return OK;
}

static int runtime_stage_cache(const update_runtime_cache_t* cache,
                               const runtime_plan_entry_t* plan,
                               uint16_t count, uint8_t slot,
                               const update_runtime_action_options_t* options) {
    for (uint32_t index = 0U; index < count; index++) {
        const runtime_plan_entry_t* item = &plan[index];
        int manifest_index;
        if (runtime_cancelled(options)) return ERR_TIMEOUT;
        if (runtime_copy_current_to_backup(slot, item->catalog_index,
                                           &item->old_state) != OK) return ERR_DISK;
        if (!item->new_state.present) continue;
        manifest_index = runtime_manifest_find(&cache->manifest,
                                               item->catalog_index);
        if (manifest_index < 0 || !cache->asset_aliases[manifest_index][0] ||
            runtime_read_cache_entry(cache->asset_aliases[manifest_index],
                                     &cache->manifest.entries[manifest_index]) != OK ||
            runtime_write_stage(slot, item->catalog_index, runtime_io_buffer,
                                item->new_state.size) != OK) return ERR_INVALID;
    }
    return OK;
}

static int runtime_replace_from_plan(const update_runtime_action_options_t* options,
                                     uint8_t kind, uint8_t slot,
                                     update_runtime_action_result_t* output) {
    for (uint32_t index = 0U; index < runtime_journal.entry_count; index++) {
        runtime_plan_entry_t* item = &runtime_journal.entries[index];
        int result;

        if (runtime_cancelled(options)) {
            if (index == 0U) {
                if (runtime_clear_slot(slot) != OK ||
                    runtime_clear_journal() != OK) {
                    if (output) output->recovery_pending = 1U;
                    return runtime_action_fail(output,
                        UPDATE_RUNTIME_REASON_JOURNAL, ERR_DISK,
                        "Nao foi possivel limpar cancelamento runtime");
                }
                return runtime_action_fail(output,
                    UPDATE_RUNTIME_REASON_CANCELLED, ERR_TIMEOUT,
                    "Aplicacao runtime cancelada antes da substituicao");
            }
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(output,
                UPDATE_RUNTIME_REASON_CANCELLED, ERR_STATE,
                "Aplicacao runtime cancelada; recuperacao pendente");
        }
        result = kind == RUNTIME_JOURNAL_ROLLBACK ?
                 runtime_restore_new_entry(item, slot) :
                 runtime_write_target_from_stage(slot, item->catalog_index,
                                                 &item->new_state);
        if (result != OK) {
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(
                output, UPDATE_RUNTIME_REASON_IO, result,
                "Falha ao substituir arquivo runtime");
        }
        runtime_journal.phase = RUNTIME_PHASE_REPLACING;
        runtime_journal.progress = (uint8_t)(index + 1U);
        if (output) output->completed_entries = (uint16_t)(index + 1U);
        if (runtime_write_journal() != OK) {
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(
                output, UPDATE_RUNTIME_REASON_JOURNAL, ERR_DISK,
                "Journal runtime nao foi persistido");
        }
        if (runtime_fail_after && runtime_journal.progress >= runtime_fail_after) {
            runtime_fail_after = 0U;
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(output, UPDATE_RUNTIME_REASON_JOURNAL,
                                       ERR_STATE,
                                       "Failpoint runtime deixou recuperacao pendente");
        }
    }
    return OK;
}

static int runtime_apply_plan_locked(const char* package_path,
                              const runtime_package_t* package,
                              const update_runtime_cache_t* cache,
                              const update_runtime_action_options_t* options,
                              update_runtime_action_result_t* output,
                              uint8_t kind, const update_version_t* target,
                              uint32_t target_epoch,
                              const runtime_plan_entry_t* plan, uint16_t count,
                              uint8_t slot) {
    update_version_t installed;
    uint32_t installed_epoch;
    int result;

    if (!target || runtime_current_version(&installed, &installed_epoch) != OK) {
        return runtime_action_fail(output, UPDATE_RUNTIME_REASON_STATE,
                                   ERR_STATE,
                                   "Versao runtime nao esta disponivel");
    }
    if (!runtime_space_available(plan, count)) return runtime_action_fail(
        output, UPDATE_RUNTIME_REASON_SPACE, ERR_OVERFLOW,
        "Espaco insuficiente para staging runtime");
    runtime_fill_action_result(output, &installed, installed_epoch, target,
                               target_epoch, count);
    if (options && options->dry_run) return OK;
    if (runtime_prepare_journal(plan, count, kind, slot, target,
                                target_epoch) != OK) return runtime_action_fail(
        output, UPDATE_RUNTIME_REASON_JOURNAL, ERR_DISK,
        "Nao foi possivel criar journal runtime");
    if (kind == RUNTIME_JOURNAL_APPLY) {
        if (runtime_clear_slot(slot) != OK) {
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(
                output, UPDATE_RUNTIME_REASON_IO, ERR_DISK,
                "Staging runtime nao foi preparado");
        }
        result = package ? runtime_stage_package(package_path, package, plan,
                                                  count, slot, options) :
                           runtime_stage_cache(cache, plan, count, slot,
                                                options);
        if (result != OK) {
            if (runtime_cancelled(options)) {
                if (runtime_clear_slot(slot) == OK &&
                    runtime_clear_journal() == OK) {
                    if (output) output->recovery_pending = 0U;
                    return runtime_action_fail(
                        output, UPDATE_RUNTIME_REASON_CANCELLED, ERR_TIMEOUT,
                        "Staging runtime cancelado");
                }
                if (output) output->recovery_pending = 1U;
                return runtime_action_fail(
                    output, UPDATE_RUNTIME_REASON_JOURNAL, ERR_DISK,
                    "Cancelamento runtime deixou recuperacao pendente");
            }
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(
                output, UPDATE_RUNTIME_REASON_IO, result,
                "Falha ao preparar payloads runtime");
        }
        runtime_journal.phase = RUNTIME_PHASE_STAGING;
        if (runtime_write_journal() != OK) {
            if (output) output->recovery_pending = 1U;
            return runtime_action_fail(
                output, UPDATE_RUNTIME_REASON_JOURNAL, ERR_DISK,
                "Journal de staging runtime nao foi persistido");
        }
    }
    result = runtime_replace_from_plan(options, kind, slot, output);
    if (result != OK) return result;
    runtime_journal.phase = RUNTIME_PHASE_COMMITTED;
    if (runtime_write_journal() != OK) {
        if (output) output->recovery_pending = 1U;
        return runtime_action_fail(
            output, UPDATE_RUNTIME_REASON_JOURNAL, ERR_DISK,
            "Commit runtime nao foi persistido");
    }
    if (runtime_commit_state_from_journal() != OK) {
        if (output) output->recovery_pending = 1U;
        return runtime_action_fail(
            output, UPDATE_RUNTIME_REASON_STATE, ERR_DISK,
            "Estado runtime nao foi publicado");
    }
    /* O rollback pode passar um ponteiro para previous_version; o commit o zera. */
    if (update_sync_runtime_state(&runtime_journal.target_version,
                                  runtime_journal.target_epoch) != OK) {
        if (output) output->recovery_pending = 1U;
        return runtime_action_fail(
            output, UPDATE_RUNTIME_REASON_STATE, ERR_DISK,
            "Estado U3 nao foi sincronizado com runtime v2");
    }
    if (runtime_clear_journal() != OK || runtime_clear_stage(slot) != OK) {
        if (output) output->recovery_pending = 1U;
        return runtime_action_fail(output, UPDATE_RUNTIME_REASON_JOURNAL,
                                   ERR_DISK,
                                   "Journal runtime nao foi encerrado");
    }
    if (kind == RUNTIME_JOURNAL_ROLLBACK && runtime_clear_slot(slot) != OK) {
        if (output) output->recovery_pending = 1U;
        return runtime_action_fail(output, UPDATE_RUNTIME_REASON_IO, ERR_DISK,
                                   "Backups de rollback runtime nao foram limpos");
    }
    if (output) output->reason = UPDATE_RUNTIME_REASON_NONE;
    return OK;
}

static int runtime_apply_plan(const char* package_path,
                              const runtime_package_t* package,
                              const update_runtime_cache_t* cache,
                              const update_runtime_action_options_t* options,
                              update_runtime_action_result_t* output,
                              uint8_t kind, const update_version_t* target,
                              uint32_t target_epoch,
                              const runtime_plan_entry_t* plan, uint16_t count,
                              uint8_t slot) {
    int result;

    spinlock_acquire(&runtime_lock);
    result = runtime_apply_plan_locked(package_path, package, cache, options,
                                       output, kind, target, target_epoch,
                                       plan, count, slot);
    spinlock_release(&runtime_lock);
    return result;
}

int update_runtime_file_matches(const char* path, uint32_t expected_size,
                                const uint8_t expected_hash[32],
                                uint8_t* matches_out) {
    runtime_file_state_t state;
    int result;

    if (!path || !expected_hash || !matches_out) {
        LOG_ERROR("UPDATE", "Argumento nulo ao comparar runtime");
        return ERR_NULL;
    }
    result = runtime_read_file_state(path, &state);
    if (result != OK) return result;
    *matches_out = state.present && state.size == expected_size &&
                   crypto_equal(state.hash, expected_hash, 32U) ? 1U : 0U;
    return OK;
}

static int runtime_verify_package_file(
    const char* path, const update_runtime_manifest_t* manifest,
    update_runtime_verification_t* result_out) {
    runtime_package_t package;
    runtime_plan_entry_t plan[UPDATE_RUNTIME_MAX_ENTRIES];
    uint8_t package_hash[32];
    uint32_t file_size;
    uint16_t plan_count = 0U;
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo na verificacao ZUPD v2");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_RUNTIME_REASON_FORMAT;
    if (!runtime_initialized || !path) return runtime_verification_fail(
        result_out, UPDATE_RUNTIME_REASON_STATE, ERR_STATE,
        "Runtime v2 nao esta inicializado");
    result = runtime_parse_package_header(path, &package, &file_size);
    if (result != OK) return runtime_verification_fail(
        result_out, UPDATE_RUNTIME_REASON_FORMAT, result,
        "Cabecalho ZUPD v2 invalido");
    result_out->package_size = file_size;
    result = runtime_package_signature(path, &package);
    if (result != OK) return runtime_verification_fail(
        result_out, UPDATE_RUNTIME_REASON_SIGNATURE, result,
        "Assinatura ZUPD v2 invalida");
    result = runtime_verify_package_payloads(path, &package);
    if (result != OK) return runtime_verification_fail(
        result_out, UPDATE_RUNTIME_REASON_HASH, result,
        "Hash de payload ZUPD v2 invalido");
    if (manifest) {
        result = runtime_hash_file_range(path, 0U, file_size, package_hash);
        if (result != OK || !crypto_equal(package_hash, manifest->package_hash,
                                          sizeof(package_hash)) ||
            file_size != manifest->package_size) {
            return runtime_verification_fail(
                result_out, UPDATE_RUNTIME_REASON_PACKAGE_MISMATCH, ERR_INVALID,
                "Hash do pacote diverge do manifesto runtime");
        }
    }
    result = runtime_package_plan(&package, manifest, plan, &plan_count);
    if (result != OK) return runtime_verification_fail(
        result_out, result == ERR_INVALID ? UPDATE_RUNTIME_REASON_PATH_POLICY :
        UPDATE_RUNTIME_REASON_VERSION, result,
        "ZUPD v2 nao e aplicavel a este runtime");
    result_out->target_version = package.target_version;
    result_out->target_epoch = package.target_epoch;
    result_out->entry_count = package.entry_count;
    result_out->changed_entries = plan_count;
    result_out->package_valid = 1U;
    result_out->compatible = 1U;
    result_out->reason = UPDATE_RUNTIME_REASON_NONE;
    return OK;
}

int update_runtime_verify_file(const char* path,
                               update_runtime_verification_t* result_out) {
    return runtime_verify_package_file(path, 0, result_out);
}

int update_runtime_verify_file_for_manifest(
    const char* path, const update_runtime_manifest_t* manifest,
    update_runtime_verification_t* result_out) {
    if (!manifest) {
        LOG_ERROR("UPDATE", "Manifesto nulo na verificacao de pacote runtime");
        return ERR_NULL;
    }
    return runtime_verify_package_file(path, manifest, result_out);
}

int update_runtime_get_capabilities(
    update_runtime_capabilities_t* capabilities_out) {
    if (!capabilities_out) {
        LOG_ERROR("UPDATE", "Destino nulo nas capacidades runtime");
        return ERR_NULL;
    }
    if (!runtime_initialized) {
        LOG_ERROR("UPDATE", "Capacidades runtime consultadas antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(capabilities_out, 0, sizeof(*capabilities_out));
    capabilities_out->verifier_ready = runtime_initialized;
    capabilities_out->local_file_available = fs_get_type() != FS_TYPE_NONE;
    capabilities_out->persistent_state_ready =
        fs_get_type() == FS_TYPE_FAT12 && runtime_state_healthy;
    capabilities_out->recovery_pending =
        runtime_journal.kind != RUNTIME_JOURNAL_NONE;
    capabilities_out->apply_available = runtime_state_healthy_now();
    capabilities_out->rollback_available = capabilities_out->apply_available &&
                                            runtime_state.rollback_available;
    capabilities_out->cache_available =
        update_remote_runtime_capability_available() ? 1U : 0U;
    capabilities_out->selective_ready = 0U;
    return OK;
}

int update_runtime_get_status(update_runtime_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("UPDATE", "Destino nulo no status runtime");
        return ERR_NULL;
    }
    if (!runtime_initialized) {
        LOG_ERROR("UPDATE", "Status runtime consultado antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->installed_version = runtime_state.installed_version;
    status_out->installed_epoch = runtime_state.installed_epoch;
    status_out->rollback_version = runtime_state.previous_version;
    status_out->rollback_epoch = runtime_state.previous_epoch;
    status_out->entry_count = runtime_state.entry_count;
    status_out->rollback_entry_count = runtime_state.rollback_entry_count;
    status_out->rollback_available = runtime_state.rollback_available;
    status_out->state_valid = runtime_state_healthy;
    status_out->recovery_pending =
        runtime_journal.kind != RUNTIME_JOURNAL_NONE;
    status_out->transaction_pending = status_out->recovery_pending;
    status_out->reason = runtime_state_healthy ?
                         UPDATE_RUNTIME_REASON_NONE :
                         UPDATE_RUNTIME_REASON_STATE;
    return update_runtime_get_capabilities(&status_out->capabilities);
}

int update_runtime_get_installed_version(update_version_t* version_out,
                                         uint32_t* epoch_out) {
    if (!version_out || !epoch_out) {
        LOG_ERROR("UPDATE", "Destino nulo na versao runtime");
        return ERR_NULL;
    }
    if (runtime_current_version(version_out, epoch_out) != OK) {
        LOG_ERROR("UPDATE", "Versao instalada runtime indisponivel");
        return ERR_STATE;
    }
    return OK;
}

int update_runtime_get_cache(update_runtime_cache_t* cache_out) {
    if (!cache_out) {
        LOG_ERROR("UPDATE", "Destino nulo no cache runtime");
        return ERR_NULL;
    }
    return update_remote_runtime_get_cache(cache_out);
}

static int runtime_build_rollback_plan(runtime_plan_entry_t* plan,
                                       uint16_t* count_out) {
    runtime_file_state_t current[UPDATE_RUNTIME_MAX_ENTRIES];
    uint16_t count = 0U;

    if (!plan || !count_out || !runtime_state.rollback_available) return ERR_NOT_FOUND;
    if (runtime_refresh_current_files(current) != OK) return ERR_DISK;
    for (uint32_t index = 0U;
         index < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]); index++) {
        if (runtime_states_equal(&current[index], &runtime_state.rollback[index])) {
            continue;
        }
        if (count >= UPDATE_RUNTIME_MAX_ENTRIES) return ERR_OVERFLOW;
        plan[count].catalog_index = (uint8_t)index;
        plan[count].old_state = current[index];
        plan[count].new_state = runtime_state.rollback[index];
        plan[count].operation = current[index].present &&
                                runtime_state.rollback[index].present ?
                                UPDATE_RUNTIME_OPERATION_REPLACE :
                                runtime_state.rollback[index].present ?
                                UPDATE_RUNTIME_OPERATION_CREATE :
                                UPDATE_RUNTIME_OPERATION_DELETE;
        count++;
    }
    *count_out = count;
    return count ? OK : ERR_NOT_FOUND;
}

int update_runtime_apply_file(
    const char* path, const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out) {
    runtime_package_t package;
    runtime_plan_entry_t plan[UPDATE_RUNTIME_MAX_ENTRIES];
    uint32_t file_size;
    uint16_t count;
    update_runtime_action_options_t defaults = {0U, 0, 0};
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo na aplicacao runtime");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (!runtime_state_healthy_now() || fs_get_type() != FS_TYPE_FAT12) {
        return runtime_action_fail(result_out, UPDATE_RUNTIME_REASON_STATE,
                                   ERR_UNAVAILABLE,
                                   "Aplicacao runtime requer FAT12 pronto");
    }
    result = runtime_parse_package_header(path, &package, &file_size);
    if (result == OK) result = runtime_package_signature(path, &package);
    if (result == OK) result = runtime_verify_package_payloads(path, &package);
    if (result != OK) return runtime_action_fail(
        result_out, UPDATE_RUNTIME_REASON_PACKAGE_MISMATCH, result,
        "Pacote runtime nao passou na verificacao");
    (void)file_size;
    result = runtime_package_plan(&package, 0, plan, &count);
    if (result != OK) return runtime_action_fail(
        result_out, UPDATE_RUNTIME_REASON_VERSION, result,
        "Pacote runtime nao e compativel");
    update_version_t installed;
    uint32_t installed_epoch;
    runtime_current_version(&installed, &installed_epoch);
    runtime_fill_action_result(result_out, &installed, installed_epoch,
                               &package.target_version, package.target_epoch,
                               count);
    result = runtime_apply_plan(path, &package, 0, options, result_out,
                                RUNTIME_JOURNAL_APPLY, &package.target_version,
                                package.target_epoch, plan, count,
                                runtime_state.rollback_available ?
                                (uint8_t)(runtime_state.rollback_slot ^ 1U) : 0U);
    return result;
}

int update_runtime_apply_cached(
    const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out) {
    update_runtime_cache_t cache;
    update_runtime_verification_t verification;
    runtime_plan_entry_t plan[UPDATE_RUNTIME_MAX_ENTRIES];
    uint16_t count;
    update_runtime_action_options_t defaults = {0U, 0, 0};
    update_version_t installed;
    uint32_t installed_epoch;
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo na aplicacao de cache runtime");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (update_remote_runtime_get_cache(&cache) != OK || !cache.valid) {
        return runtime_action_fail(result_out, UPDATE_RUNTIME_REASON_CACHE,
                                   ERR_NOT_FOUND,
                                   "Nenhum cache runtime esta pronto");
    }
    if (cache.full_package && cache.package_alias[0]) {
        runtime_package_t package;
        uint32_t file_size;

        result = update_runtime_verify_file_for_manifest(
            cache.package_alias, &cache.manifest, &verification);
        if (result != OK) return runtime_action_fail(
            result_out, verification.reason, result,
            "Pacote completo runtime nao passou na verificacao");
        result = runtime_parse_package_header(cache.package_alias, &package,
                                              &file_size);
        if (result != OK) return runtime_action_fail(
            result_out, UPDATE_RUNTIME_REASON_PACKAGE_MISMATCH, result,
            "Pacote completo runtime desapareceu do cache");
        result = runtime_package_plan(&package, &cache.manifest, plan, &count);
        if (result != OK) return runtime_action_fail(
            result_out, UPDATE_RUNTIME_REASON_VERSION, result,
            "Pacote completo runtime nao e compativel");
        runtime_current_version(&installed, &installed_epoch);
        runtime_fill_action_result(result_out, &installed, installed_epoch,
                                   &package.target_version,
                                   package.target_epoch, count);
        return runtime_apply_plan(cache.package_alias, &package, 0, options,
                                  result_out, RUNTIME_JOURNAL_APPLY,
                                  &package.target_version, package.target_epoch,
                                  plan, count,
                                  runtime_state.rollback_available ?
                                  (uint8_t)(runtime_state.rollback_slot ^ 1U) : 0U);
    }
    result = runtime_manifest_plan(&cache.manifest, plan, &count, 0, 0);
    if (result != OK) return runtime_action_fail(
        result_out, UPDATE_RUNTIME_REASON_VERSION, result,
        "Cache seletivo runtime nao e compativel");
    runtime_current_version(&installed, &installed_epoch);
    runtime_fill_action_result(result_out, &installed, installed_epoch,
                               &cache.manifest.target_version,
                               cache.manifest.target_epoch, count);
    return runtime_apply_plan(0, 0, &cache, options, result_out,
                              RUNTIME_JOURNAL_APPLY,
                              &cache.manifest.target_version,
                              cache.manifest.target_epoch, plan, count,
                              runtime_state.rollback_available ?
                              (uint8_t)(runtime_state.rollback_slot ^ 1U) : 0U);
}

int update_runtime_rollback(
    const update_runtime_action_options_t* options,
    update_runtime_action_result_t* result_out) {
    runtime_plan_entry_t plan[UPDATE_RUNTIME_MAX_ENTRIES];
    update_runtime_action_options_t defaults = {0U, 0, 0};
    uint16_t count;
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo no rollback runtime");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (!runtime_state_healthy_now()) return runtime_action_fail(
        result_out, UPDATE_RUNTIME_REASON_STATE, ERR_STATE,
        "Estado runtime nao permite rollback");
    result = runtime_build_rollback_plan(plan, &count);
    if (result != OK) return runtime_action_fail(
        result_out, UPDATE_RUNTIME_REASON_CACHE, result,
        "Nenhum rollback runtime esta disponivel");
    runtime_fill_action_result(result_out, &runtime_state.installed_version,
                               runtime_state.installed_epoch,
                               &runtime_state.previous_version,
                               runtime_state.previous_epoch, count);
    result = runtime_apply_plan(0, 0, 0, options, result_out,
                                RUNTIME_JOURNAL_ROLLBACK,
                                &runtime_state.previous_version,
                                runtime_state.previous_epoch, plan, count,
                                runtime_state.rollback_slot);
    return result;
}

int update_runtime_init(void) {
    int state_result;
    int journal_result;
    int recovery_result;
    runtime_file_state_t actual[UPDATE_RUNTIME_MAX_ENTRIES];
    uint8_t state_seeded = 0U;
    uint8_t state_reconciled = 0U;

    LOG_INFO("UPDATE", "Inicializando runtime v2");
    spinlock_init(&runtime_lock);
    kmemset(&runtime_state, 0, sizeof(runtime_state));
    kmemset(&runtime_journal, 0, sizeof(runtime_journal));
    kmemset(runtime_control, 0, sizeof(runtime_control));
    kmemset(runtime_manifest_raw, 0, sizeof(runtime_manifest_raw));
    runtime_initialized = 0U;
    runtime_state_healthy = 0U;
    runtime_state_slot = -1;
    runtime_journal_slot = -1;
    runtime_fail_after = 0U;
    if (!update_is_ready()) {
        LOG_ERROR("UPDATE", "Runtime v2 requer verificador local pronto");
        return ERR_STATE;
    }
    runtime_initialized = 1U;
    if (fs_get_type() != FS_TYPE_FAT12) {
        if (update_get_installed_version(&runtime_state.installed_version,
                                         &runtime_state.installed_epoch) != OK) {
            runtime_initialized = 0U;
            LOG_ERROR("UPDATE", "Versao instalada indisponivel sem FAT12");
            return ERR_STATE;
        }
        runtime_state.entry_count =
            (uint16_t)(sizeof(runtime_catalog) / sizeof(runtime_catalog[0]));
        runtime_state_healthy = 1U;
        LOG_INFO("UPDATE", "Runtime v2 pronto; aplicacao FAT12 indisponivel");
        return OK;
    }
    state_result = runtime_load_state_records();
    if (state_result == ERR_NOT_FOUND) {
        state_result = runtime_state_seed();
        state_seeded = state_result == OK ? 1U : 0U;
    }
    journal_result = runtime_load_journal_records();
    if (state_result != OK || journal_result != OK) {
        LOG_ERROR("UPDATE", "Estado persistente runtime v2 invalido");
        return ERR_STATE;
    }
    recovery_result = runtime_recover_pending();
    if (recovery_result != OK) {
        LOG_ERROR("UPDATE", "Recuperacao runtime v2 indisponivel");
        return ERR_STATE;
    }
    if (runtime_refresh_current_files(actual) != OK) {
        LOG_ERROR("UPDATE", "Arquivos gerenciados runtime nao puderam ser lidos");
        return ERR_DISK;
    }
    if (!state_seeded) {
        for (uint32_t index = 0U;
             index < sizeof(runtime_catalog) / sizeof(runtime_catalog[0]);
             index++) {
            if (!runtime_states_equal(&runtime_state.current[index],
                                      &actual[index])) {
                state_reconciled = 1U;
                break;
            }
        }
    }
    if (state_reconciled) {
        update_version_t system_version;
        uint32_t system_epoch;

        if (update_get_installed_version(&system_version, &system_epoch) != OK) {
            if (runtime_legacy_transaction_pending()) {
                LOG_ERROR("UPDATE", "Estado U3 pendente impede reconciliacao runtime");
                return ERR_STATE;
            }
            system_version = runtime_state.installed_version;
            system_epoch = runtime_state.installed_epoch;
            LOG_WARN("UPDATE", "Runtime reconciliado sem reescrever estado U3 degradado");
        }
        runtime_state.installed_version = system_version;
        runtime_state.installed_epoch = system_epoch;
        kmemcpy(runtime_state.current, actual, sizeof(runtime_state.current));
        kmemset(runtime_state.rollback, 0, sizeof(runtime_state.rollback));
        runtime_state.rollback_available = 0U;
        runtime_state.rollback_slot = RUNTIME_SLOT_NONE;
        runtime_state.rollback_entry_count = 0U;
        kmemset(&runtime_state.previous_version, 0,
                sizeof(runtime_state.previous_version));
        runtime_state.previous_epoch = 0U;
        LOG_WARN("UPDATE", "Estado runtime reconciliado com arquivos ativos");
    }
    if (state_seeded || state_reconciled) {
        if (runtime_write_state() != OK) {
            LOG_ERROR("UPDATE", "Estado runtime v2 nao foi reconciliado apos recovery");
            return ERR_DISK;
        }
    }
    if (!state_reconciled) {
        update_version_t legacy_version;
        uint32_t legacy_epoch;

        if (update_get_installed_version(&legacy_version, &legacy_epoch) != OK) {
            if (runtime_legacy_transaction_pending()) {
                LOG_ERROR("UPDATE", "Estado U3 pendente impede inicializacao runtime");
                return ERR_STATE;
            }
            LOG_WARN("UPDATE", "Estado U3 degradado preservado; runtime v2 segue independente");
        } else if (runtime_version_compare(&legacy_version,
                                           &runtime_state.installed_version) != 0 ||
                   legacy_epoch != runtime_state.installed_epoch) {
            if (update_sync_runtime_state(&runtime_state.installed_version,
                                          runtime_state.installed_epoch) != OK) {
                LOG_ERROR("UPDATE", "Estado U3 ficou atrasado em relacao ao runtime v2");
                return ERR_STATE;
            }
        }
    }
    if (runtime_refresh_current_files(runtime_state.current) != OK) {
        LOG_ERROR("UPDATE", "Estado runtime divergiu durante a inicializacao");
        return ERR_DISK;
    }
    runtime_state_healthy = 1U;
    LOG_INFO("UPDATE", "Runtime v2 inicializado com sucesso");
    return OK;
}

int update_runtime_is_ready(void) {
    return runtime_initialized && runtime_state_healthy;
}

int update_runtime_test_fail_after(uint16_t completed_entries) {
    if (!runtime_state_healthy_now() || completed_entries == 0U ||
        completed_entries > UPDATE_RUNTIME_MAX_ENTRIES) {
        LOG_ERROR("UPDATE", "Failpoint runtime fora do estado permitido");
        return ERR_INVALID;
    }
    spinlock_acquire(&runtime_lock);
    if (runtime_journal.kind != RUNTIME_JOURNAL_NONE) {
        spinlock_release(&runtime_lock);
        LOG_ERROR("UPDATE", "Failpoint runtime recusado durante transacao");
        return ERR_STATE;
    }
    runtime_fail_after = (uint8_t)completed_entries;
    spinlock_release(&runtime_lock);
    LOG_WARN("UPDATE", "Failpoint runtime armado para a proxima aplicacao");
    return OK;
}

const char* update_runtime_reason_name(update_runtime_reason_t reason) {
    switch (reason) {
        case UPDATE_RUNTIME_REASON_NONE: return "NONE";
        case UPDATE_RUNTIME_REASON_FORMAT: return "FORMAT";
        case UPDATE_RUNTIME_REASON_SIZE: return "SIZE";
        case UPDATE_RUNTIME_REASON_HASH: return "HASH";
        case UPDATE_RUNTIME_REASON_UNKNOWN_KEY: return "UNKNOWN_KEY";
        case UPDATE_RUNTIME_REASON_SIGNATURE: return "SIGNATURE";
        case UPDATE_RUNTIME_REASON_ARCHITECTURE: return "ARCHITECTURE";
        case UPDATE_RUNTIME_REASON_BASE_VERSION: return "BASE_VERSION";
        case UPDATE_RUNTIME_REASON_DOWNGRADE: return "DOWNGRADE";
        case UPDATE_RUNTIME_REASON_PATH_POLICY: return "PATH_POLICY";
        case UPDATE_RUNTIME_REASON_OPERATION: return "OPERATION";
        case UPDATE_RUNTIME_REASON_DUPLICATE_TARGET: return "DUPLICATE_TARGET";
        case UPDATE_RUNTIME_REASON_STATE: return "STATE";
        case UPDATE_RUNTIME_REASON_JOURNAL: return "JOURNAL";
        case UPDATE_RUNTIME_REASON_SPACE: return "SPACE";
        case UPDATE_RUNTIME_REASON_IO: return "IO";
        case UPDATE_RUNTIME_REASON_CANCELLED: return "CANCELLED";
        case UPDATE_RUNTIME_REASON_CACHE: return "CACHE";
        case UPDATE_RUNTIME_REASON_PACKAGE_MISMATCH: return "PACKAGE_MISMATCH";
        case UPDATE_RUNTIME_REASON_VERSION: return "VERSION";
        default: return "INVALID_REASON";
    }
}
