#include "core/update.h"
#include "core/crypto.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/update_trust.h"
#include "core/update_remote.h"
#include "core/update_runtime.h"
#include "core/version.h"
#include "fs/fs.h"
#if defined(ZEPHYROS_HOST_TEST)
#include "update_host.h"
#endif

#define UPDATE_FORMAT_VERSION 1U
#define UPDATE_ARCH_I386 1U
#define UPDATE_SIGNATURE_SIZE 64U
#define UPDATE_SIGNATURE_ED25519 1U
#define UPDATE_HASH_SHA256 1U
#define UPDATE_OPERATION_REPLACE 1U
#define UPDATE_COMPRESSION_NONE 0U
#define UPDATE_TARGET_SYSTEM_FILE 1U
#define UPDATE_IO_BUFFER_SIZE 4096U
#define UPDATE_DOMAIN_SIZE 19U
#define UPDATE_CONTROL_SIZE 512U
#define UPDATE_CONTROL_HASH_OFFSET 480U
#define UPDATE_CONTROL_VERSION 1U
#define UPDATE_TARGET_COUNT 3U
#define UPDATE_STORAGE_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)

#define UPDATE_STATE_CURRENT_OFFSET 40U
#define UPDATE_STATE_ROLLBACK_OFFSET 160U
#define UPDATE_STATE_FILE_SIZE 40U

#define UPDATE_JOURNAL_ENTRY_OFFSET 48U
#define UPDATE_JOURNAL_ENTRY_SIZE 76U

#define UPDATE_HISTORY_HEADER_SIZE 32U
#define UPDATE_HISTORY_ENTRY_SIZE 56U
#define UPDATE_HISTORY_ENTRY_OFFSET 32U
#define UPDATE_HISTORY_FLAG_REBOOT 0x01U
#define UPDATE_APPLY_METADATA_CLUSTERS 8U
#define UPDATE_ROLLBACK_METADATA_CLUSTERS 2U

#define UPDATE_JOURNAL_NONE 0U
#define UPDATE_JOURNAL_APPLY 1U
#define UPDATE_JOURNAL_ROLLBACK 2U

#define UPDATE_PHASE_NONE 0U
#define UPDATE_PHASE_PREPARED 1U
#define UPDATE_PHASE_REPLACING 2U
#define UPDATE_PHASE_COMMITTED 3U

#define HEADER_MAGIC 0U
#define HEADER_FORMAT_VERSION 4U
#define HEADER_SIZE 6U
#define HEADER_ARCHITECTURE 8U
#define HEADER_FLAGS 12U
#define HEADER_TOTAL_SIZE 16U
#define HEADER_MANIFEST_OFFSET 20U
#define HEADER_MANIFEST_SIZE 24U
#define HEADER_PAYLOAD_OFFSET 28U
#define HEADER_PAYLOAD_SIZE 32U
#define HEADER_SIGNATURE_OFFSET 36U
#define HEADER_SIGNATURE_SIZE 40U
#define HEADER_SIGNATURE_ALGORITHM 42U
#define HEADER_HASH_ALGORITHM 44U
#define HEADER_ENTRY_COUNT 46U
#define HEADER_ENTRY_SIZE 48U
#define HEADER_RESERVED0 50U
#define HEADER_BASE_MAJOR 52U
#define HEADER_BASE_MINOR 54U
#define HEADER_BASE_PATCH 56U
#define HEADER_TARGET_MAJOR 58U
#define HEADER_TARGET_MINOR 60U
#define HEADER_TARGET_PATCH 62U
#define HEADER_BASE_EPOCH 64U
#define HEADER_TARGET_EPOCH 68U
#define HEADER_KEY_ID 72U
#define HEADER_CONTENT_HASH 88U
#define HEADER_RESERVED 120U

#define ENTRY_PATH 0U
#define ENTRY_PAYLOAD_OFFSET 64U
#define ENTRY_PAYLOAD_SIZE 68U
#define ENTRY_INSTALLED_SIZE 72U
#define ENTRY_OPERATION 76U
#define ENTRY_COMPRESSION 78U
#define ENTRY_TARGET_CLASS 80U
#define ENTRY_FLAGS 82U
#define ENTRY_PAYLOAD_HASH 84U
#define ENTRY_RESERVED 116U

typedef struct {
    char path[ZUPD_PATH_SIZE];
    uint32_t payload_offset;
    uint32_t payload_size;
    uint8_t payload_hash[CRYPTO_SHA256_SIZE];
} update_entry_t;

typedef struct {
    uint32_t architecture;
    uint32_t total_size;
    uint32_t manifest_size;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t signature_offset;
    uint16_t entry_count;
    update_version_t base_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
    uint8_t key_id[16];
    uint8_t content_hash[CRYPTO_SHA256_SIZE];
} update_header_t;

typedef struct {
    uint8_t header[ZUPD_HEADER_SIZE];
    uint8_t table[ZUPD_ENTRY_SIZE * ZUPD_MAX_ENTRIES];
    uint8_t io_buffer[UPDATE_IO_BUFFER_SIZE];
    uint8_t file_buffer[ZUPD_MAX_PAYLOAD_SIZE];
    uint8_t signature[UPDATE_SIGNATURE_SIZE];
    update_entry_t entries[ZUPD_MAX_ENTRIES];
    crypto_sha256_ctx_t content_sha256;
    crypto_sha256_ctx_t payload_sha256;
    crypto_ed25519_verify_ctx_t signature_verify;
    update_header_t parsed;
    uint8_t busy;
} update_workspace_t;

typedef struct {
    uint32_t size;
    uint8_t hash[CRYPTO_SHA256_SIZE];
    uint8_t present;
} update_file_state_t;

typedef struct {
    uint32_t sequence;
    update_version_t installed_version;
    uint32_t installed_epoch;
    uint8_t rollback_available;
    uint8_t rollback_slot;
    uint8_t rollback_entry_count;
    update_version_t previous_version;
    uint32_t previous_epoch;
    update_file_state_t current[UPDATE_TARGET_COUNT];
    update_file_state_t rollback[UPDATE_TARGET_COUNT];
} update_state_t;

typedef struct {
    uint8_t target_id;
    update_file_state_t old_file;
    update_file_state_t new_file;
} update_journal_entry_t;

typedef struct {
    uint32_t sequence;
    uint8_t kind;
    uint8_t phase;
    uint8_t slot;
    uint8_t entry_count;
    uint8_t progress;
    uint32_t base_state_sequence;
    update_version_t base_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
    update_journal_entry_t entries[UPDATE_TARGET_COUNT];
} update_journal_t;

typedef struct {
    uint32_t sequence;
    uint8_t count;
    uint8_t next_index;
    update_history_entry_t entries[UPDATE_HISTORY_MAX_ENTRIES];
} update_history_t;

static const uint8_t update_domain[UPDATE_DOMAIN_SIZE] = {
    0x5AU, 0x45U, 0x50U, 0x48U, 0x59U, 0x52U, 0x4FU, 0x53U,
    0x2DU, 0x55U, 0x50U, 0x44U, 0x41U, 0x54U, 0x45U, 0x2DU,
    0x56U, 0x31U, 0x00U
};

static update_workspace_t update_workspace;
static update_capabilities_t update_capabilities;
static spinlock_t update_lock;
static int update_initialized = 0;
static int update_state_healthy = 0;
static int update_state_record_slot = -1;
static int update_journal_record_slot = -1;
static int update_history_record_slot = -1;
static uint16_t update_fail_after = 0;
static update_state_t update_state;
static update_journal_t update_journal;
static update_history_t update_history;
static update_store_state_t update_history_store =
    UPDATE_STORE_UNAVAILABLE;
static int update_history_write_failed = 0;

static const char* update_target_paths[UPDATE_TARGET_COUNT] = {
    "EXPLORER.BMP", "SHELL.BMP", "TASKMGR.BMP"
};

static const char* update_state_paths[2] = {
    "ZUPD0.STA", "ZUPD1.STA"
};

static const char* update_journal_paths[2] = {
    "ZUPD0.JRN", "ZUPD1.JRN"
};

static const char* update_history_paths[2] = {
    "ZUPD0.HIS", "ZUPD1.HIS"
};

static const char* update_backup_paths[2][UPDATE_TARGET_COUNT] = {
    {"ZBA0.BAK", "ZBA1.BAK", "ZBA2.BAK"},
    {"ZBB0.BAK", "ZBB1.BAK", "ZBB2.BAK"}
};

static const char* update_stage_paths[2][UPDATE_TARGET_COUNT] = {
    {"ZSA0.NEW", "ZSA1.NEW", "ZSA2.NEW"},
    {"ZSB0.NEW", "ZSB1.NEW", "ZSB2.NEW"}
};

static uint16_t update_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t update_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int update_bytes_zero(const uint8_t* data, uint32_t size) {
    for (uint32_t index = 0; index < size; index++) {
        if (data[index] != 0) return 0;
    }
    return 1;
}

static void update_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void update_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static int update_target_id(const char* path) {
    for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
        if (kstrcmp(path, update_target_paths[index]) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int update_hash_root_file(const char* path,
                                 update_file_state_t* state_out) {
    crypto_sha256_ctx_t hash;
    uint32_t size = 0;
    uint32_t offset = 0;
    int result;

    if (!path || !state_out) {
        LOG_ERROR("UPDATE", "Argumento nulo ao hashear arquivo instalado");
        return ERR_NULL;
    }
    result = fs_get_root_file_info(path, &size, 0);
    if (result != OK) {
        LOG_ERROR("UPDATE", "Arquivo instalado nao pode ser consultado");
        return result;
    }
    if (size == 0U || size > ZUPD_MAX_PAYLOAD_SIZE) {
        LOG_ERROR("UPDATE", "Arquivo instalado excede limite U3");
        return ERR_OVERFLOW;
    }
    result = crypto_sha256_init(&hash);
    if (result != OK) return result;
    while (offset < size) {
        uint32_t chunk = size - offset;
        uint32_t bytes_read = 0;

        if (chunk > UPDATE_IO_BUFFER_SIZE) chunk = UPDATE_IO_BUFFER_SIZE;
        result = fs_read_file_range_at(
            path, offset, update_workspace.io_buffer, chunk, &bytes_read);
        if (result != OK || bytes_read != chunk) {
            LOG_ERROR("UPDATE", "Falha ao ler arquivo instalado");
            return ERR_DISK;
        }
        result = crypto_sha256_update(
            &hash, update_workspace.io_buffer, chunk);
        if (result != OK) return result;
        offset += chunk;
    }
    result = crypto_sha256_final(&hash, state_out->hash);
    if (result != OK) return result;
    state_out->size = size;
    state_out->present = 1;
    return OK;
}

static int update_file_state_equal(const update_file_state_t* first,
                                   const update_file_state_t* second) {
    if (!first || !second || first->present != second->present) return 0;
    if (!first->present) return 1;
    return first->size == second->size &&
           crypto_equal(first->hash, second->hash, CRYPTO_SHA256_SIZE);
}

static int update_load_root_file(const char* path,
                                 const update_file_state_t* expected) {
    uint32_t bytes_read = 0;
    int result;

    if (!path || !expected || !expected->present ||
        expected->size == 0U ||
        expected->size > sizeof(update_workspace.file_buffer)) {
        LOG_ERROR("UPDATE", "Estado invalido ao carregar arquivo U3");
        return ERR_INVALID;
    }
    result = fs_read_file_range_at(
        path, 0, update_workspace.file_buffer, expected->size, &bytes_read);
    if (result != OK || bytes_read != expected->size) {
        LOG_ERROR("UPDATE", "Falha ao carregar arquivo U3");
        return ERR_DISK;
    }
    result = crypto_sha256(
        update_workspace.file_buffer, expected->size,
        update_workspace.io_buffer);
    if (result != OK) return result;
    if (!crypto_equal(update_workspace.io_buffer, expected->hash,
                      CRYPTO_SHA256_SIZE)) {
        LOG_ERROR("UPDATE", "Hash do arquivo U3 nao confere");
        return ERR_INVALID;
    }
    return OK;
}

static int update_verify_root_file(const char* path,
                                   const update_file_state_t* expected) {
    update_file_state_t actual;
    int result;

    if (!path || !expected) {
        LOG_ERROR("UPDATE", "Argumento nulo ao validar estado U3");
        return ERR_NULL;
    }
    if (!expected->present) {
        uint32_t ignored_size = 0U;

        result = fs_get_root_file_info(path, &ignored_size, 0);
        if (result == ERR_NOT_FOUND) return OK;
        if (result == OK) {
            LOG_ERROR("UPDATE", "Arquivo ausente do estado U3 permaneceu");
            return ERR_INVALID;
        }
        LOG_ERROR("UPDATE", "Falha ao consultar arquivo ausente U3");
        return result;
    }
    kmemset(&actual, 0, sizeof(actual));
    result = update_hash_root_file(path, &actual);
    if (result != OK) return result;
    return update_file_state_equal(&actual, expected) ? OK : ERR_INVALID;
}

static void update_encode_file_state(uint8_t* record, uint32_t offset,
                                     const update_file_state_t* state) {
    update_write_u32(record + offset, state->size);
    kmemcpy(record + offset + 4U, state->hash, CRYPTO_SHA256_SIZE);
    record[offset + 36U] = state->present;
}

static void update_decode_file_state(update_file_state_t* state,
                                     const uint8_t* record,
                                     uint32_t offset) {
    state->size = update_read_u32(record + offset);
    kmemcpy(state->hash, record + offset + 4U, CRYPTO_SHA256_SIZE);
    state->present = record[offset + 36U];
}

static int update_encode_state_record(const update_state_t* state,
                                      uint8_t record[UPDATE_CONTROL_SIZE]) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    kmemset(record, 0, UPDATE_CONTROL_SIZE);
    kmemcpy(record, "ZUST", 4U);
    update_write_u16(record + 4U, UPDATE_CONTROL_VERSION);
    update_write_u16(record + 6U, UPDATE_CONTROL_SIZE);
    update_write_u32(record + 8U, state->sequence);
    update_write_u16(record + 12U, state->installed_version.major);
    update_write_u16(record + 14U, state->installed_version.minor);
    update_write_u16(record + 16U, state->installed_version.patch);
    update_write_u32(record + 20U, state->installed_epoch);
    record[24] = state->rollback_available;
    record[25] = state->rollback_slot;
    record[26] = state->rollback_entry_count;
    update_write_u16(record + 28U, state->previous_version.major);
    update_write_u16(record + 30U, state->previous_version.minor);
    update_write_u16(record + 32U, state->previous_version.patch);
    update_write_u32(record + 36U, state->previous_epoch);
    for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
        update_encode_file_state(
            record, UPDATE_STATE_CURRENT_OFFSET +
                    index * UPDATE_STATE_FILE_SIZE,
            &state->current[index]);
        update_encode_file_state(
            record, UPDATE_STATE_ROLLBACK_OFFSET +
                    index * UPDATE_STATE_FILE_SIZE,
            &state->rollback[index]);
    }
    result = crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash);
    if (result != OK) return result;
    kmemcpy(record + UPDATE_CONTROL_HASH_OFFSET, hash, sizeof(hash));
    return OK;
}

static int update_decode_state_record(update_state_t* state,
                                      const uint8_t record[UPDATE_CONTROL_SIZE]) {
    uint8_t hash[CRYPTO_SHA256_SIZE];

    if (!state || !record) {
        LOG_ERROR("UPDATE", "Registro de estado nulo");
        return ERR_NULL;
    }
    if (!crypto_equal(record, (const uint8_t*)"ZUST", 4U) ||
        update_read_u16(record + 4U) != UPDATE_CONTROL_VERSION ||
        update_read_u16(record + 6U) != UPDATE_CONTROL_SIZE ||
        !update_bytes_zero(record + 18U, 2U) ||
        !update_bytes_zero(record + 27U, 1U) ||
        !update_bytes_zero(record + 34U, 2U) ||
        !update_bytes_zero(record + 280U, 200U)) {
        LOG_ERROR("UPDATE", "Formato do estado U3 invalido");
        return ERR_INVALID;
    }
    if (crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash) != OK ||
        !crypto_equal(hash, record + UPDATE_CONTROL_HASH_OFFSET,
                      sizeof(hash))) {
        LOG_ERROR("UPDATE", "Hash do estado U3 invalido");
        return ERR_INVALID;
    }

    kmemset(state, 0, sizeof(*state));
    state->sequence = update_read_u32(record + 8U);
    state->installed_version.major = update_read_u16(record + 12U);
    state->installed_version.minor = update_read_u16(record + 14U);
    state->installed_version.patch = update_read_u16(record + 16U);
    state->installed_epoch = update_read_u32(record + 20U);
    state->rollback_available = record[24];
    state->rollback_slot = record[25];
    state->rollback_entry_count = record[26];
    state->previous_version.major = update_read_u16(record + 28U);
    state->previous_version.minor = update_read_u16(record + 30U);
    state->previous_version.patch = update_read_u16(record + 32U);
    state->previous_epoch = update_read_u32(record + 36U);
    if (state->rollback_available > 1U || state->rollback_slot > 1U ||
        state->rollback_entry_count > UPDATE_TARGET_COUNT) {
        LOG_ERROR("UPDATE", "Campos de rollback U3 invalidos");
        return ERR_INVALID;
    }
    for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
        uint32_t current_offset = UPDATE_STATE_CURRENT_OFFSET +
                                  index * UPDATE_STATE_FILE_SIZE;
        uint32_t rollback_offset = UPDATE_STATE_ROLLBACK_OFFSET +
                                   index * UPDATE_STATE_FILE_SIZE;

        update_decode_file_state(
            &state->current[index], record,
            current_offset);
        update_decode_file_state(
            &state->rollback[index], record,
            rollback_offset);
        if (state->current[index].present > 1U ||
            state->rollback[index].present > 1U ||
            !update_bytes_zero(record + current_offset + 37U, 3U) ||
            !update_bytes_zero(record + rollback_offset + 37U, 3U)) {
            LOG_ERROR("UPDATE", "Estado de arquivo U3 invalido");
            return ERR_INVALID;
        }
        if ((state->current[index].present &&
             (state->current[index].size == 0U ||
              state->current[index].size > ZUPD_MAX_PAYLOAD_SIZE)) ||
            (!state->current[index].present &&
             (state->current[index].size != 0U ||
              !update_bytes_zero(state->current[index].hash,
                                 CRYPTO_SHA256_SIZE))) ||
            (state->rollback[index].present &&
             (state->rollback[index].size == 0U ||
              state->rollback[index].size > ZUPD_MAX_PAYLOAD_SIZE))) {
            LOG_ERROR("UPDATE", "Tamanho de arquivo do estado U3 invalido");
            return ERR_INVALID;
        }
        if (!state->rollback[index].present &&
            (state->rollback[index].size != 0U ||
             !update_bytes_zero(
                 state->rollback[index].hash, CRYPTO_SHA256_SIZE))) {
            LOG_ERROR("UPDATE", "Arquivo ausente do estado nao esta zerado");
            return ERR_INVALID;
        }
    }
    if ((!state->rollback_available &&
         (state->rollback_entry_count != 0U ||
          state->previous_version.major != 0U ||
          state->previous_version.minor != 0U ||
          state->previous_version.patch != 0U ||
          state->previous_epoch != 0U)) ||
        (state->rollback_available &&
         state->rollback_entry_count == 0U)) {
        LOG_ERROR("UPDATE", "Geracao de rollback U3 inconsistente");
        return ERR_INVALID;
    }
    {
        uint8_t rollback_count = 0;
        for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
            if (state->rollback[index].present) rollback_count++;
        }
        if (rollback_count != state->rollback_entry_count) {
            LOG_ERROR("UPDATE", "Contagem de rollback U3 divergente");
            return ERR_INVALID;
        }
    }
    return OK;
}

static int update_encode_journal_record(
    const update_journal_t* journal,
    uint8_t record[UPDATE_CONTROL_SIZE]) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    kmemset(record, 0, UPDATE_CONTROL_SIZE);
    kmemcpy(record, "ZUJ1", 4U);
    update_write_u16(record + 4U, UPDATE_CONTROL_VERSION);
    update_write_u16(record + 6U, UPDATE_CONTROL_SIZE);
    update_write_u32(record + 8U, journal->sequence);
    record[12] = journal->kind;
    record[13] = journal->phase;
    record[14] = journal->slot;
    record[15] = journal->entry_count;
    record[16] = journal->progress;
    update_write_u32(record + 20U, journal->base_state_sequence);
    update_write_u16(record + 24U, journal->base_version.major);
    update_write_u16(record + 26U, journal->base_version.minor);
    update_write_u16(record + 28U, journal->base_version.patch);
    update_write_u16(record + 30U, journal->target_version.major);
    update_write_u16(record + 32U, journal->target_version.minor);
    update_write_u16(record + 34U, journal->target_version.patch);
    update_write_u32(record + 36U, journal->base_epoch);
    update_write_u32(record + 40U, journal->target_epoch);
    for (uint32_t index = 0; index < journal->entry_count; index++) {
        uint32_t offset = UPDATE_JOURNAL_ENTRY_OFFSET +
                          index * UPDATE_JOURNAL_ENTRY_SIZE;
        const update_journal_entry_t* entry = &journal->entries[index];

        record[offset] = entry->target_id;
        record[offset + 1U] = entry->old_file.present;
        record[offset + 2U] = entry->new_file.present;
        update_write_u32(record + offset + 4U, entry->old_file.size);
        update_write_u32(record + offset + 8U, entry->new_file.size);
        kmemcpy(record + offset + 12U, entry->old_file.hash,
                CRYPTO_SHA256_SIZE);
        kmemcpy(record + offset + 44U, entry->new_file.hash,
                CRYPTO_SHA256_SIZE);
    }
    result = crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash);
    if (result != OK) return result;
    kmemcpy(record + UPDATE_CONTROL_HASH_OFFSET, hash, sizeof(hash));
    return OK;
}

static int update_decode_journal_record(
    update_journal_t* journal,
    const uint8_t record[UPDATE_CONTROL_SIZE]) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    uint8_t seen = 0;

    if (!journal || !record) {
        LOG_ERROR("UPDATE", "Registro de journal nulo");
        return ERR_NULL;
    }
    if (!crypto_equal(record, (const uint8_t*)"ZUJ1", 4U) ||
        update_read_u16(record + 4U) != UPDATE_CONTROL_VERSION ||
        update_read_u16(record + 6U) != UPDATE_CONTROL_SIZE ||
        !update_bytes_zero(record + 17U, 3U) ||
        !update_bytes_zero(record + 44U, 4U) ||
        !update_bytes_zero(record + 276U, 204U)) {
        LOG_ERROR("UPDATE", "Formato do journal U3 invalido");
        return ERR_INVALID;
    }
    if (crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash) != OK ||
        !crypto_equal(hash, record + UPDATE_CONTROL_HASH_OFFSET,
                      sizeof(hash))) {
        LOG_ERROR("UPDATE", "Hash do journal U3 invalido");
        return ERR_INVALID;
    }

    kmemset(journal, 0, sizeof(*journal));
    journal->sequence = update_read_u32(record + 8U);
    journal->kind = record[12];
    journal->phase = record[13];
    journal->slot = record[14];
    journal->entry_count = record[15];
    journal->progress = record[16];
    journal->base_state_sequence = update_read_u32(record + 20U);
    journal->base_version.major = update_read_u16(record + 24U);
    journal->base_version.minor = update_read_u16(record + 26U);
    journal->base_version.patch = update_read_u16(record + 28U);
    journal->target_version.major = update_read_u16(record + 30U);
    journal->target_version.minor = update_read_u16(record + 32U);
    journal->target_version.patch = update_read_u16(record + 34U);
    journal->base_epoch = update_read_u32(record + 36U);
    journal->target_epoch = update_read_u32(record + 40U);
    if (journal->kind > UPDATE_JOURNAL_ROLLBACK ||
        journal->phase > UPDATE_PHASE_COMMITTED ||
        journal->slot > 1U ||
        journal->entry_count > UPDATE_TARGET_COUNT ||
        journal->progress > journal->entry_count) {
        LOG_ERROR("UPDATE", "Campos do journal U3 invalidos");
        return ERR_INVALID;
    }
    if ((journal->kind == UPDATE_JOURNAL_NONE &&
         (journal->phase != UPDATE_PHASE_NONE ||
          journal->entry_count != 0U || journal->progress != 0U ||
          !update_bytes_zero(
              record + 12U, UPDATE_CONTROL_HASH_OFFSET - 12U))) ||
        (journal->kind != UPDATE_JOURNAL_NONE &&
         (journal->phase == UPDATE_PHASE_NONE ||
          journal->entry_count == 0U))) {
        LOG_ERROR("UPDATE", "Estado do journal U3 inconsistente");
        return ERR_INVALID;
    }

    for (uint32_t index = 0; index < journal->entry_count; index++) {
        uint32_t offset = UPDATE_JOURNAL_ENTRY_OFFSET +
                          index * UPDATE_JOURNAL_ENTRY_SIZE;
        update_journal_entry_t* entry = &journal->entries[index];

        if (!update_bytes_zero(record + offset + 3U, 1U)) {
            LOG_ERROR("UPDATE", "Reservado de entrada U3 nao zerado");
            return ERR_INVALID;
        }
        entry->target_id = record[offset];
        entry->old_file.present = record[offset + 1U];
        entry->new_file.present = record[offset + 2U];
        entry->old_file.size = update_read_u32(record + offset + 4U);
        entry->new_file.size = update_read_u32(record + offset + 8U);
        kmemcpy(entry->old_file.hash, record + offset + 12U,
                CRYPTO_SHA256_SIZE);
        kmemcpy(entry->new_file.hash, record + offset + 44U,
                CRYPTO_SHA256_SIZE);
        if (entry->target_id >= UPDATE_TARGET_COUNT ||
            entry->old_file.present != 1U ||
            entry->new_file.present != 1U ||
            entry->old_file.size == 0U ||
            entry->old_file.size > ZUPD_MAX_PAYLOAD_SIZE ||
            entry->new_file.size == 0U ||
            entry->new_file.size > ZUPD_MAX_PAYLOAD_SIZE ||
            (seen & (1U << entry->target_id))) {
            LOG_ERROR("UPDATE", "Entrada do journal U3 invalida");
            return ERR_INVALID;
        }
        seen |= (uint8_t)(1U << entry->target_id);
    }
    for (uint32_t index = journal->entry_count;
         index < UPDATE_TARGET_COUNT; index++) {
        uint32_t offset = UPDATE_JOURNAL_ENTRY_OFFSET +
                          index * UPDATE_JOURNAL_ENTRY_SIZE;
        if (!update_bytes_zero(record + offset,
                               UPDATE_JOURNAL_ENTRY_SIZE)) {
            LOG_ERROR("UPDATE", "Entrada nao usada do journal nao zerada");
            return ERR_INVALID;
        }
    }
    return OK;
}

static int update_history_alias_valid(const char* alias) {
    int terminated = 0;

    for (uint32_t index = 0; index < UPDATE_HISTORY_PACKAGE_SIZE; index++) {
        uint8_t value = (uint8_t)alias[index];

        if (value == 0U) {
            terminated = 1;
        } else if (terminated || value < 0x20U || value > 0x7EU ||
                   value == '/' || value == '\\') {
            return 0;
        }
    }
    return terminated;
}

static void update_encode_history_entry(
    uint8_t* record, uint32_t offset,
    const update_history_entry_t* entry) {
    update_write_u32(record + offset, entry->sequence);
    record[offset + 4U] = (uint8_t)entry->operation;
    record[offset + 5U] = (uint8_t)entry->outcome;
    record[offset + 6U] = (uint8_t)entry->action_reason;
    record[offset + 7U] = (uint8_t)entry->verification_reason;
    update_write_u16(record + offset + 8U, entry->from_version.major);
    update_write_u16(record + offset + 10U, entry->from_version.minor);
    update_write_u16(record + offset + 12U, entry->from_version.patch);
    update_write_u16(record + offset + 14U, entry->to_version.major);
    update_write_u16(record + offset + 16U, entry->to_version.minor);
    update_write_u16(record + offset + 18U, entry->to_version.patch);
    update_write_u32(record + offset + 20U, entry->from_epoch);
    update_write_u32(record + offset + 24U, entry->to_epoch);
    update_write_u16(record + offset + 28U, entry->entry_count);
    update_write_u16(record + offset + 30U, entry->completed_entries);
    record[offset + 32U] =
        entry->reboot_required ? UPDATE_HISTORY_FLAG_REBOOT : 0U;
    kmemcpy(record + offset + 33U, entry->package_alias,
            UPDATE_HISTORY_PACKAGE_SIZE);
}

static int update_decode_history_entry(
    update_history_entry_t* entry, const uint8_t* record,
    uint32_t offset, uint32_t record_sequence) {
    uint8_t flags = record[offset + 32U];

    kmemset(entry, 0, sizeof(*entry));
    entry->sequence = update_read_u32(record + offset);
    entry->operation =
        (update_history_operation_t)record[offset + 4U];
    entry->outcome = (update_history_outcome_t)record[offset + 5U];
    entry->action_reason =
        (update_action_reason_t)record[offset + 6U];
    entry->verification_reason =
        (zupd_reason_t)record[offset + 7U];
    entry->from_version.major = update_read_u16(record + offset + 8U);
    entry->from_version.minor = update_read_u16(record + offset + 10U);
    entry->from_version.patch = update_read_u16(record + offset + 12U);
    entry->to_version.major = update_read_u16(record + offset + 14U);
    entry->to_version.minor = update_read_u16(record + offset + 16U);
    entry->to_version.patch = update_read_u16(record + offset + 18U);
    entry->from_epoch = update_read_u32(record + offset + 20U);
    entry->to_epoch = update_read_u32(record + offset + 24U);
    entry->entry_count = update_read_u16(record + offset + 28U);
    entry->completed_entries = update_read_u16(record + offset + 30U);
    entry->reboot_required =
        (flags & UPDATE_HISTORY_FLAG_REBOOT) ? 1U : 0U;
    kmemcpy(entry->package_alias, record + offset + 33U,
            UPDATE_HISTORY_PACKAGE_SIZE);
    if (entry->sequence == 0U || entry->sequence > record_sequence ||
        entry->operation < UPDATE_HISTORY_OPERATION_APPLY ||
        entry->operation > UPDATE_HISTORY_OPERATION_RECOVERY_ROLLBACK ||
        entry->outcome < UPDATE_HISTORY_OUTCOME_SUCCESS ||
        entry->outcome > UPDATE_HISTORY_OUTCOME_RECOVERED ||
        entry->action_reason > UPDATE_ACTION_RECOVERY_PENDING ||
        entry->verification_reason > ZUPD_REASON_UNSUPPORTED ||
        entry->completed_entries > entry->entry_count ||
        (flags & (uint8_t)~UPDATE_HISTORY_FLAG_REBOOT) != 0U ||
        !update_history_alias_valid(entry->package_alias) ||
        !update_bytes_zero(record + offset + 46U, 10U)) {
        LOG_ERROR("UPDATE", "Entrada de historico U4 invalida");
        return ERR_INVALID;
    }
    return OK;
}

static int update_encode_history_record(
    const update_history_t* history,
    uint8_t record[UPDATE_CONTROL_SIZE]) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    kmemset(record, 0, UPDATE_CONTROL_SIZE);
    kmemcpy(record, "ZUH1", 4U);
    update_write_u16(record + 4U, UPDATE_CONTROL_VERSION);
    update_write_u16(record + 6U, UPDATE_CONTROL_SIZE);
    update_write_u32(record + 8U, history->sequence);
    record[12] = history->count;
    record[13] = history->next_index;
    for (uint32_t index = 0;
         index < UPDATE_HISTORY_MAX_ENTRIES; index++) {
        int active = history->count == UPDATE_HISTORY_MAX_ENTRIES ||
                     index < history->count;

        if (active) {
            update_encode_history_entry(
                record, UPDATE_HISTORY_ENTRY_OFFSET +
                        index * UPDATE_HISTORY_ENTRY_SIZE,
                &history->entries[index]);
        }
    }
    result = crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash);
    if (result != OK) return result;
    kmemcpy(record + UPDATE_CONTROL_HASH_OFFSET, hash, sizeof(hash));
    return OK;
}

static int update_decode_history_record(
    update_history_t* history,
    const uint8_t record[UPDATE_CONTROL_SIZE]) {
    uint8_t hash[CRYPTO_SHA256_SIZE];
    uint32_t sequence;
    uint8_t count;
    uint8_t next_index;

    if (!history || !record) {
        LOG_ERROR("UPDATE", "Registro de historico U4 nulo");
        return ERR_NULL;
    }
    sequence = update_read_u32(record + 8U);
    count = record[12];
    next_index = record[13];
    if (!crypto_equal(record, (const uint8_t*)"ZUH1", 4U) ||
        update_read_u16(record + 4U) != UPDATE_CONTROL_VERSION ||
        update_read_u16(record + 6U) != UPDATE_CONTROL_SIZE ||
        count > UPDATE_HISTORY_MAX_ENTRIES ||
        next_index >= UPDATE_HISTORY_MAX_ENTRIES ||
        (count < UPDATE_HISTORY_MAX_ENTRIES && next_index != count) ||
        !update_bytes_zero(record + 14U,
                           UPDATE_HISTORY_HEADER_SIZE - 14U)) {
        LOG_ERROR("UPDATE", "Formato do historico U4 invalido");
        return ERR_INVALID;
    }
    if (crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash) != OK ||
        !crypto_equal(hash, record + UPDATE_CONTROL_HASH_OFFSET,
                      sizeof(hash))) {
        LOG_ERROR("UPDATE", "Hash do historico U4 invalido");
        return ERR_INVALID;
    }
    kmemset(history, 0, sizeof(*history));
    history->sequence = sequence;
    history->count = count;
    history->next_index = next_index;
    for (uint32_t index = 0;
         index < UPDATE_HISTORY_MAX_ENTRIES; index++) {
        uint32_t offset = UPDATE_HISTORY_ENTRY_OFFSET +
                          index * UPDATE_HISTORY_ENTRY_SIZE;
        int active = count == UPDATE_HISTORY_MAX_ENTRIES || index < count;

        if (!active) {
            if (!update_bytes_zero(record + offset,
                                   UPDATE_HISTORY_ENTRY_SIZE)) {
                LOG_ERROR("UPDATE", "Slot vazio do historico nao esta zerado");
                return ERR_INVALID;
            }
            continue;
        }
        if (update_decode_history_entry(
                &history->entries[index], record, offset,
                sequence) != OK) {
            return ERR_INVALID;
        }
    }
    if (count > 0U) {
        uint32_t oldest = count == UPDATE_HISTORY_MAX_ENTRIES ?
                          next_index : 0U;

        if (sequence < count) {
            LOG_ERROR("UPDATE", "Sequencia do historico U4 inconsistente");
            return ERR_INVALID;
        }
        for (uint32_t order = 0; order < count; order++) {
            uint32_t slot =
                (oldest + order) % UPDATE_HISTORY_MAX_ENTRIES;
            uint32_t expected = sequence - count + 1U + order;

            if (history->entries[slot].sequence != expected) {
                LOG_ERROR("UPDATE", "Ordem do ring U4 inconsistente");
                return ERR_INVALID;
            }
        }
    }
    return OK;
}

static int update_read_control(const char* path,
                               uint8_t record[UPDATE_CONTROL_SIZE],
                               int* exists_out) {
    uint32_t size = 0;
    uint32_t bytes_read = 0;
    int result;

    if (!path || !record || !exists_out) {
        LOG_ERROR("UPDATE", "Argumento nulo ao ler controle U3");
        return ERR_NULL;
    }
    *exists_out = 0;
    result = fs_get_root_file_info(path, &size, 0);
    if (result == ERR_NOT_FOUND) return OK;
    if (result != OK) {
        LOG_ERROR("UPDATE", "Falha ao consultar controle U3");
        return result;
    }
    *exists_out = 1;
    if (size != UPDATE_CONTROL_SIZE) {
        LOG_ERROR("UPDATE", "Tamanho do controle U3 invalido");
        return ERR_INVALID;
    }
    result = fs_read_file_range_at(
        path, 0, record, UPDATE_CONTROL_SIZE, &bytes_read);
    if (result != OK || bytes_read != UPDATE_CONTROL_SIZE) {
        LOG_ERROR("UPDATE", "Falha ao ler controle U3");
        return ERR_DISK;
    }
    return OK;
}

static void update_history_mark_invalid(void) {
    kmemset(&update_history, 0, sizeof(update_history));
    update_history_record_slot = -1;
    update_history_store = UPDATE_STORE_INVALID;
    update_history_write_failed = 0;
}

static int update_load_history_records(void) {
    update_history_t candidate;
    uint8_t record[UPDATE_CONTROL_SIZE];
    uint8_t selected[UPDATE_CONTROL_SIZE];
    int found_valid = 0;
    int found_any = 0;

    kmemset(&update_history, 0, sizeof(update_history));
    kmemset(selected, 0, sizeof(selected));
    update_history_record_slot = -1;
    update_history_store = UPDATE_STORE_EMPTY;
    update_history_write_failed = 0;
    for (int slot = 0; slot < 2; slot++) {
        int exists = 0;
        int result = update_read_control(
            update_history_paths[slot], record, &exists);

        if (exists) found_any = 1;
        if (result != OK || !exists ||
            update_decode_history_record(&candidate, record) != OK) {
            continue;
        }
        if (found_valid && candidate.sequence == update_history.sequence &&
            !crypto_equal(record, selected, UPDATE_CONTROL_SIZE)) {
            LOG_ERROR("UPDATE", "Copias do historico empatadas e divergentes");
            update_history_mark_invalid();
            return OK;
        }
        if (!found_valid || candidate.sequence > update_history.sequence) {
            update_history = candidate;
            update_history_record_slot = slot;
            kmemcpy(selected, record, sizeof(selected));
            found_valid = 1;
        }
    }
    if (found_valid) {
        update_history_store = update_history.count ?
            UPDATE_STORE_VALID : UPDATE_STORE_EMPTY;
        return OK;
    }
    if (found_any) {
        LOG_ERROR("UPDATE", "Nenhum registro de historico U4 e valido");
        update_history_mark_invalid();
    }
    return OK;
}

static int update_write_history_record(update_history_t* history) {
    uint8_t record[UPDATE_CONTROL_SIZE];
    uint32_t old_sequence;
    int old_slot;
    int next_slot;
    int result;

    if (!history) {
        LOG_ERROR("UPDATE", "Historico U4 nulo para persistencia");
        return ERR_NULL;
    }
    old_sequence = history->sequence;
    if (old_sequence == 0xFFFFFFFFU) {
        LOG_ERROR("UPDATE", "Sequencia do historico U4 esgotada");
        return ERR_OVERFLOW;
    }
    old_slot = update_history_record_slot;
    next_slot = old_slot < 0 ? 0 : 1 - old_slot;
    history->sequence++;
    result = update_encode_history_record(history, record);
    if (result == OK) {
        result = fs_atomic_write_root(
            update_history_paths[next_slot], record, sizeof(record),
            UPDATE_STORAGE_ATTRIBUTES, FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        history->sequence = old_sequence;
        LOG_ERROR("UPDATE", "Falha ao persistir historico redundante");
        return result;
    }
    update_history_record_slot = next_slot;
    return OK;
}

static int update_history_append(const update_history_entry_t* source) {
    update_history_t previous;
    update_history_entry_t entry;
    update_store_state_t previous_store;
    int previous_slot;
    uint8_t slot;
    int result;

    if (!source) {
        LOG_ERROR("UPDATE", "Evento U4 nulo");
        return ERR_NULL;
    }
    if (fs_get_type() != FS_TYPE_FAT12) {
        LOG_WARN("UPDATE", "Historico U4 requer FAT12");
        return ERR_UNAVAILABLE;
    }
    previous = update_history;
    previous_store = update_history_store;
    previous_slot = update_history_record_slot;
    if (update_history_store == UPDATE_STORE_INVALID) {
        LOG_WARN("UPDATE", "Historico U4 corrompido sera reiniciado");
        kmemset(&update_history, 0, sizeof(update_history));
        update_history_record_slot = -1;
    }
    if (update_history.sequence == 0xFFFFFFFFU) {
        LOG_ERROR("UPDATE", "Sequencia de evento U4 esgotada");
        return ERR_OVERFLOW;
    }
    entry = *source;
    entry.sequence = update_history.sequence + 1U;
    slot = update_history.next_index;
    update_history.entries[slot] = entry;
    if (update_history.count < UPDATE_HISTORY_MAX_ENTRIES) {
        update_history.count++;
    }
    update_history.next_index =
        (uint8_t)((slot + 1U) % UPDATE_HISTORY_MAX_ENTRIES);
    result = update_write_history_record(&update_history);
    if (result != OK) {
        update_history = previous;
        update_history_store = previous_store;
        update_history_record_slot = previous_slot;
        update_history_write_failed = 1;
        return result;
    }
    update_history_store = UPDATE_STORE_VALID;
    update_history_write_failed = 0;
    return OK;
}

static int update_history_newest(
    uint32_t newest_index, update_history_entry_t* entry_out) {
    uint32_t slot;

    if (!entry_out) {
        LOG_ERROR("UPDATE", "Saida de historico U4 nula");
        return ERR_NULL;
    }
    if (update_history_store == UPDATE_STORE_UNAVAILABLE) {
        LOG_WARN("UPDATE", "Historico U4 indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (update_history_store == UPDATE_STORE_INVALID ||
        update_history_write_failed) {
        LOG_ERROR("UPDATE", "Historico U4 sem integridade");
        return ERR_INVALID;
    }
    if (newest_index >= update_history.count) {
        LOG_WARN("UPDATE", "Evento U4 solicitado nao existe");
        return ERR_NOT_FOUND;
    }
    slot = (update_history.next_index + UPDATE_HISTORY_MAX_ENTRIES - 1U -
            newest_index) % UPDATE_HISTORY_MAX_ENTRIES;
    *entry_out = update_history.entries[slot];
    return OK;
}

static void update_history_copy_alias(
    char output[UPDATE_HISTORY_PACKAGE_SIZE], const char* path) {
    uint32_t length;

    kmemset(output, 0, UPDATE_HISTORY_PACKAGE_SIZE);
    if (!path) return;
    length = kstrlen(path);
    if (length == 0U || length >= UPDATE_HISTORY_PACKAGE_SIZE) return;
    for (uint32_t index = 0; index < length; index++) {
        char value = path[index];

        if (value == '/' || value == '\\') {
            kmemset(output, 0, UPDATE_HISTORY_PACKAGE_SIZE);
            return;
        }
        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        output[index] = value;
    }
}

static int update_capture_baseline_state(update_state_t* state) {
    if (!state) return ERR_NULL;
    kmemset(state, 0, sizeof(*state));
    state->installed_version.major = ZEPHYROS_VERSION_MAJOR;
    state->installed_version.minor = ZEPHYROS_VERSION_MINOR;
    state->installed_version.patch = ZEPHYROS_VERSION_PATCH;
    state->installed_epoch = ZEPHYROS_VERSION_EPOCH;
    for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
        int result = update_hash_root_file(
            update_target_paths[index], &state->current[index]);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao capturar baseline dos arquivos");
            return result;
        }
    }
    return OK;
}

static int update_load_state_records(void) {
    update_state_t candidate;
    uint8_t record[UPDATE_CONTROL_SIZE];
    int found_valid = 0;
    int found_any = 0;

    update_state_record_slot = -1;
    kmemset(&update_state, 0, sizeof(update_state));
    for (int slot = 0; slot < 2; slot++) {
        int exists = 0;
        int result = update_read_control(
            update_state_paths[slot], record, &exists);

        if (exists) found_any = 1;
        if (result != OK || !exists ||
            update_decode_state_record(&candidate, record) != OK) continue;
        if (!found_valid || candidate.sequence > update_state.sequence) {
            update_state = candidate;
            update_state_record_slot = slot;
            found_valid = 1;
        }
    }
    if (found_valid) return OK;
    if (found_any) {
        LOG_ERROR("UPDATE", "Nenhum registro de estado U3 e valido");
        return ERR_INVALID;
    }
    return update_capture_baseline_state(&update_state);
}

static int update_load_journal_records(void) {
    update_journal_t candidate;
    uint8_t record[UPDATE_CONTROL_SIZE];
    int found_valid = 0;
    int found_any = 0;

    update_journal_record_slot = -1;
    kmemset(&update_journal, 0, sizeof(update_journal));
    for (int slot = 0; slot < 2; slot++) {
        int exists = 0;
        int result = update_read_control(
            update_journal_paths[slot], record, &exists);

        if (exists) found_any = 1;
        if (result != OK || !exists ||
            update_decode_journal_record(&candidate, record) != OK) continue;
        if (!found_valid || candidate.sequence > update_journal.sequence) {
            update_journal = candidate;
            update_journal_record_slot = slot;
            found_valid = 1;
        }
    }
    if (found_valid) return OK;
    if (found_any) {
        LOG_ERROR("UPDATE", "Nenhum journal U3 e valido");
        return ERR_INVALID;
    }
    return OK;
}

static int update_write_state_record(update_state_t* state) {
    uint8_t record[UPDATE_CONTROL_SIZE];
    uint32_t old_sequence;
    int old_slot;
    int next_slot;
    int result;

    if (!state) return ERR_NULL;
    old_sequence = state->sequence;
    if (old_sequence == 0xFFFFFFFFU) {
        LOG_ERROR("UPDATE", "Sequencia do estado U3 esgotada");
        return ERR_OVERFLOW;
    }
    old_slot = update_state_record_slot;
    next_slot = old_slot < 0 ? 0 : 1 - old_slot;
    state->sequence++;
    result = update_encode_state_record(state, record);
    if (result == OK) {
        result = fs_atomic_write_root(
            update_state_paths[next_slot], record, sizeof(record),
            UPDATE_STORAGE_ATTRIBUTES, FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        state->sequence = old_sequence;
        LOG_ERROR("UPDATE", "Falha ao persistir estado redundante");
        return result;
    }
    update_state_record_slot = next_slot;
    return OK;
}

static int update_write_journal_record(update_journal_t* journal) {
    uint8_t record[UPDATE_CONTROL_SIZE];
    uint32_t old_sequence;
    int old_slot;
    int next_slot;
    int result;

    if (!journal) return ERR_NULL;
    old_sequence = journal->sequence;
    if (old_sequence == 0xFFFFFFFFU) {
        LOG_ERROR("UPDATE", "Sequencia do journal U3 esgotada");
        return ERR_OVERFLOW;
    }
    old_slot = update_journal_record_slot;
    next_slot = old_slot < 0 ? 0 : 1 - old_slot;
    journal->sequence++;
    result = update_encode_journal_record(journal, record);
    if (result == OK) {
        result = fs_atomic_write_root(
            update_journal_paths[next_slot], record, sizeof(record),
            UPDATE_STORAGE_ATTRIBUTES, FS_ATOMIC_CREATE_OR_REPLACE);
    }
    if (result != OK) {
        journal->sequence = old_sequence;
        LOG_ERROR("UPDATE", "Falha ao persistir journal redundante");
        return result;
    }
    update_journal_record_slot = next_slot;
    return OK;
}

static int update_clear_journal(void) {
    update_journal_t previous = update_journal;
    int result;

    kmemset(&update_journal, 0, sizeof(update_journal));
    update_journal.sequence = previous.sequence;
    result = update_write_journal_record(&update_journal);
    if (result != OK) update_journal = previous;
    return result;
}

static int update_validate_current_state(void) {
    for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
        int result = update_verify_root_file(
            update_target_paths[index], &update_state.current[index]);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Arquivo instalado diverge do estado U3");
            return result;
        }
    }
    return OK;
}

static int update_fail(update_verification_t* output, zupd_reason_t reason,
                       int error, const char* message) {
    if (output) output->reason = reason;
    LOG_ERROR("UPDATE", message);
    return error;
}

static int update_reason_error(zupd_reason_t reason) {
    int error = ERR_INVALID;

    switch (reason) {
        case ZUPD_REASON_SIZE:
            error = ERR_OVERFLOW;
            break;
        case ZUPD_REASON_ARCHITECTURE:
        case ZUPD_REASON_BASE_VERSION:
        case ZUPD_REASON_DOWNGRADE:
            error = ERR_STATE;
            break;
        case ZUPD_REASON_UNSUPPORTED:
            error = ERR_UNAVAILABLE;
            break;
        default:
            break;
    }
    return error;
}

static int update_reject(update_verification_t* output,
                         zupd_reason_t reason, const char* message) {
    return update_fail(output, reason, update_reason_error(reason), message);
}

static int update_read_exact(const char* path, uint32_t offset,
                             uint8_t* buffer, uint32_t size,
                             update_verification_t* output) {
    uint32_t bytes_read = 0;
    int result = fs_read_file_range_at(path, offset, buffer, size, &bytes_read);

    if (result != OK) {
        return update_fail(output, ZUPD_REASON_NONE, result,
                           "Falha de I/O ao ler artefato ZUPD");
    }
    if (bytes_read != size) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Artefato ZUPD truncado");
    }
    return OK;
}

static void update_decode_header(update_header_t* parsed,
                                 const uint8_t* header) {
    parsed->architecture = update_read_u32(header + HEADER_ARCHITECTURE);
    parsed->total_size = update_read_u32(header + HEADER_TOTAL_SIZE);
    parsed->manifest_size = update_read_u32(header + HEADER_MANIFEST_SIZE);
    parsed->payload_offset = update_read_u32(header + HEADER_PAYLOAD_OFFSET);
    parsed->payload_size = update_read_u32(header + HEADER_PAYLOAD_SIZE);
    parsed->signature_offset =
        update_read_u32(header + HEADER_SIGNATURE_OFFSET);
    parsed->entry_count = update_read_u16(header + HEADER_ENTRY_COUNT);
    parsed->base_version.major = update_read_u16(header + HEADER_BASE_MAJOR);
    parsed->base_version.minor = update_read_u16(header + HEADER_BASE_MINOR);
    parsed->base_version.patch = update_read_u16(header + HEADER_BASE_PATCH);
    parsed->target_version.major =
        update_read_u16(header + HEADER_TARGET_MAJOR);
    parsed->target_version.minor =
        update_read_u16(header + HEADER_TARGET_MINOR);
    parsed->target_version.patch =
        update_read_u16(header + HEADER_TARGET_PATCH);
    parsed->base_epoch = update_read_u32(header + HEADER_BASE_EPOCH);
    parsed->target_epoch = update_read_u32(header + HEADER_TARGET_EPOCH);
    kmemcpy(parsed->key_id, header + HEADER_KEY_ID, sizeof(parsed->key_id));
    kmemcpy(parsed->content_hash, header + HEADER_CONTENT_HASH,
            sizeof(parsed->content_hash));
}

static int update_validate_header_limits(const uint8_t* header,
                                         update_verification_t* output) {
    uint32_t total_size = update_read_u32(header + HEADER_TOTAL_SIZE);
    uint32_t manifest_size = update_read_u32(header + HEADER_MANIFEST_SIZE);
    uint32_t payload_offset = update_read_u32(header + HEADER_PAYLOAD_OFFSET);
    uint32_t payload_size = update_read_u32(header + HEADER_PAYLOAD_SIZE);
    uint32_t signature_offset =
        update_read_u32(header + HEADER_SIGNATURE_OFFSET);
    uint16_t entry_count = update_read_u16(header + HEADER_ENTRY_COUNT);

    if (total_size < 321U || total_size > ZUPD_MAX_TOTAL_SIZE ||
        manifest_size > ZUPD_MAX_TOTAL_SIZE ||
        payload_offset > ZUPD_MAX_TOTAL_SIZE ||
        payload_size > ZUPD_MAX_TOTAL_SIZE ||
        signature_offset > ZUPD_MAX_TOTAL_SIZE ||
        entry_count == 0U || entry_count > ZUPD_MAX_ENTRIES ||
        update_read_u16(header + HEADER_SIGNATURE_SIZE) !=
            UPDATE_SIGNATURE_SIZE) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Limite de tamanho ZUPD invalido");
    }
    return OK;
}

static int update_validate_header_algorithms(const uint8_t* header,
                                             update_verification_t* output) {
    if (update_read_u16(header + HEADER_SIGNATURE_ALGORITHM) !=
            UPDATE_SIGNATURE_ED25519 ||
        update_read_u16(header + HEADER_HASH_ALGORITHM) !=
            UPDATE_HASH_SHA256) {
        return update_reject(output, ZUPD_REASON_UNSUPPORTED,
                             "Algoritmo ZUPD nao suportado");
    }
    return OK;
}

static int update_validate_header_layout(const uint8_t* header,
                                         update_verification_t* output) {
    uint32_t total_size = update_read_u32(header + HEADER_TOTAL_SIZE);
    uint32_t manifest_size = update_read_u32(header + HEADER_MANIFEST_SIZE);
    uint32_t payload_offset = update_read_u32(header + HEADER_PAYLOAD_OFFSET);
    uint32_t payload_size = update_read_u32(header + HEADER_PAYLOAD_SIZE);
    uint32_t signature_offset =
        update_read_u32(header + HEADER_SIGNATURE_OFFSET);
    uint32_t expected_manifest =
        update_read_u16(header + HEADER_ENTRY_COUNT) * ZUPD_ENTRY_SIZE;

    if (header[0] != 'Z' || header[1] != 'U' ||
        header[2] != 'P' || header[3] != 'D' ||
        update_read_u16(header + HEADER_FORMAT_VERSION) !=
            UPDATE_FORMAT_VERSION ||
        update_read_u16(header + HEADER_SIZE) != ZUPD_HEADER_SIZE ||
        update_read_u32(header + HEADER_FLAGS) != 0U ||
        update_read_u32(header + HEADER_MANIFEST_OFFSET) != ZUPD_HEADER_SIZE ||
        manifest_size != expected_manifest ||
        payload_offset != ZUPD_HEADER_SIZE + expected_manifest ||
        signature_offset != payload_offset + payload_size ||
        total_size != signature_offset + UPDATE_SIGNATURE_SIZE ||
        update_read_u16(header + HEADER_ENTRY_SIZE) != ZUPD_ENTRY_SIZE ||
        update_read_u16(header + HEADER_RESERVED0) != 0U ||
        !update_bytes_zero(header + HEADER_RESERVED, 8U)) {
        return update_reject(output, ZUPD_REASON_FORMAT,
                             "Layout ou campos reservados do header invalidos");
    }
    return OK;
}

static int update_check_exact_file_size(const char* path,
                                        const update_header_t* parsed,
                                        update_verification_t* output) {
    uint32_t bytes_read = 0;
    int result = fs_read_file_range_at(
        path, parsed->total_size, update_workspace.io_buffer, 1U, &bytes_read);

    if (result != OK) {
        return update_fail(output, ZUPD_REASON_NONE, result,
                           "Falha ao confirmar tamanho do ZUPD");
    }
    if (bytes_read != 0U) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Artefato possui bytes apos o fim declarado");
    }
    return OK;
}

static int update_is_path_character(char value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

static int update_validate_path_syntax(const char* path) {
    uint32_t base_size = 0;
    uint32_t extension_size = 0;

    while (path[base_size] && path[base_size] != '.') {
        if (!update_is_path_character(path[base_size]) || base_size >= 8U) {
            return 0;
        }
        base_size++;
    }
    if (base_size == 0U || path[base_size] != '.') return 0;
    for (uint32_t index = base_size + 1U; path[index]; index++) {
        if (!update_is_path_character(path[index]) || path[index] == '_' ||
            extension_size >= 3U) {
            return 0;
        }
        extension_size++;
    }
    return extension_size > 0U;
}

static int update_decode_path(char output[ZUPD_PATH_SIZE],
                              const uint8_t* encoded,
                              update_verification_t* result) {
    uint32_t length = 0;

    while (length < ZUPD_PATH_SIZE && encoded[length] != 0U) {
        output[length] = (char)encoded[length];
        length++;
    }
    if (length == 0U || length == ZUPD_PATH_SIZE ||
        !update_bytes_zero(encoded + length + 1U,
                           ZUPD_PATH_SIZE - length - 1U)) {
        return update_reject(result, ZUPD_REASON_FORMAT,
                             "Campo path sem terminacao canonica");
    }
    output[length] = '\0';
    if (!update_validate_path_syntax(output)) {
        return update_reject(result, ZUPD_REASON_PATH_POLICY,
                             "Caminho FAT nao canonico");
    }
    return OK;
}

static int update_compare_paths(const char* first, const char* second) {
    while (*first && *first == *second) {
        first++;
        second++;
    }
    return (uint8_t)*first - (uint8_t)*second;
}

static int update_parse_entry(update_entry_t* parsed, const uint8_t* raw,
                              uint32_t expected_offset,
                              update_verification_t* output) {
    int result = update_decode_path(parsed->path, raw + ENTRY_PATH, output);

    if (result != OK) return result;
    parsed->payload_offset = update_read_u32(raw + ENTRY_PAYLOAD_OFFSET);
    parsed->payload_size = update_read_u32(raw + ENTRY_PAYLOAD_SIZE);
    kmemcpy(parsed->payload_hash, raw + ENTRY_PAYLOAD_HASH,
            sizeof(parsed->payload_hash));
    if (parsed->payload_size == 0U ||
        parsed->payload_size > ZUPD_MAX_PAYLOAD_SIZE) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Payload individual fora dos limites");
    }
    if (update_read_u16(raw + ENTRY_OPERATION) != UPDATE_OPERATION_REPLACE ||
        update_read_u16(raw + ENTRY_COMPRESSION) != UPDATE_COMPRESSION_NONE ||
        update_read_u16(raw + ENTRY_TARGET_CLASS) !=
            UPDATE_TARGET_SYSTEM_FILE) {
        return update_reject(output, ZUPD_REASON_UNSUPPORTED,
                             "Operacao ZUPD nao suportada");
    }
    if (update_read_u32(raw + ENTRY_INSTALLED_SIZE) != parsed->payload_size ||
        parsed->payload_offset != expected_offset ||
        update_read_u16(raw + ENTRY_FLAGS) != 0U ||
        !update_bytes_zero(raw + ENTRY_RESERVED, 12U)) {
        return update_reject(output, ZUPD_REASON_FORMAT,
                             "Layout ou campo reservado de entrada invalido");
    }
    return OK;
}

static int update_parse_entries(update_workspace_t* workspace,
                                update_verification_t* output) {
    uint32_t expected_offset = workspace->parsed.payload_offset;

    for (uint32_t index = 0; index < workspace->parsed.entry_count; index++) {
        const uint8_t* raw = workspace->table + index * ZUPD_ENTRY_SIZE;
        update_entry_t* entry = &workspace->entries[index];
        int result = update_parse_entry(entry, raw, expected_offset, output);

        if (result != OK) return result;
        if (index > 0U) {
            int order = update_compare_paths(
                workspace->entries[index - 1U].path, entry->path);
            if (order == 0) {
                return update_reject(output, ZUPD_REASON_DUPLICATE_TARGET,
                                     "Target ZUPD duplicado");
            }
            if (order > 0) {
                return update_reject(output, ZUPD_REASON_FORMAT,
                                     "Tabela ZUPD fora de ordem");
            }
        }
        if (entry->payload_offset > workspace->parsed.signature_offset ||
            entry->payload_size >
                workspace->parsed.signature_offset - entry->payload_offset) {
            return update_reject(output, ZUPD_REASON_SIZE,
                                 "Payload excede a regiao declarada");
        }
        expected_offset = entry->payload_offset + entry->payload_size;
    }
    if (expected_offset != workspace->parsed.signature_offset) {
        return update_reject(output, ZUPD_REASON_FORMAT,
                             "Payloads possuem lacuna ou sobreposicao");
    }
    return OK;
}

static int update_hash_chunk(update_workspace_t* workspace,
                             const uint8_t* data, uint32_t size) {
    int result = crypto_sha256_update(&workspace->content_sha256, data, size);

    if (result != OK) return result;
    result = crypto_sha256_update(&workspace->payload_sha256, data, size);
    if (result != OK) return result;
    return crypto_ed25519_verify_update(
        &workspace->signature_verify, data, size);
}

static int update_hash_payload(const char* path, update_workspace_t* workspace,
                               update_entry_t* entry,
                               update_verification_t* output) {
    uint32_t remaining = entry->payload_size;
    uint32_t offset = entry->payload_offset;
    uint8_t computed[CRYPTO_SHA256_SIZE];
    int result = crypto_sha256_init(&workspace->payload_sha256);

    if (result != OK) return result;
    while (remaining > 0U) {
        uint32_t chunk = remaining > UPDATE_IO_BUFFER_SIZE ?
                         UPDATE_IO_BUFFER_SIZE : remaining;
        result = update_read_exact(
            path, offset, workspace->io_buffer, chunk, output);
        if (result != OK) return result;
        result = update_hash_chunk(workspace, workspace->io_buffer, chunk);
        if (result != OK) return result;
        offset += chunk;
        remaining -= chunk;
    }
    result = crypto_sha256_final(&workspace->payload_sha256, computed);
    if (result != OK) return result;
    if (!crypto_equal(computed, entry->payload_hash, sizeof(computed))) {
        return update_reject(output, ZUPD_REASON_HASH,
                             "SHA-256 individual do payload diverge");
    }
    return OK;
}

static int update_hash_content(const char* path, update_workspace_t* workspace,
                               update_verification_t* output) {
    uint8_t computed[CRYPTO_SHA256_SIZE];
    int result = crypto_sha256_init(&workspace->content_sha256);

    if (result != OK) return result;
    result = crypto_ed25519_verify_init(
        &workspace->signature_verify, workspace->signature,
        UPDATE_TRUST_PUBLIC_KEY);
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &workspace->signature_verify, update_domain, sizeof(update_domain));
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &workspace->signature_verify, workspace->header, ZUPD_HEADER_SIZE);
    if (result != OK) return result;
    result = crypto_sha256_update(
        &workspace->content_sha256, workspace->table,
        workspace->parsed.manifest_size);
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &workspace->signature_verify, workspace->table,
        workspace->parsed.manifest_size);
    if (result != OK) return result;
    for (uint32_t index = 0; index < workspace->parsed.entry_count; index++) {
        result = update_hash_payload(
            path, workspace, &workspace->entries[index], output);
        if (result != OK) return result;
    }
    result = crypto_sha256_final(&workspace->content_sha256, computed);
    if (result != OK) return result;
    if (!crypto_equal(computed, workspace->parsed.content_hash,
                      sizeof(computed))) {
        return update_reject(output, ZUPD_REASON_HASH,
                             "SHA-256 global do ZUPD diverge");
    }
    return OK;
}

static int update_version_compare(const update_version_t* first,
                                  const update_version_t* second) {
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

static int update_path_allowed(const char* path) {
    return kstrcmp(path, "EXPLORER.BMP") == 0 ||
           kstrcmp(path, "SHELL.BMP") == 0 ||
           kstrcmp(path, "TASKMGR.BMP") == 0;
}

static int update_target_exists(const char* path) {
    uint32_t ignored = 0;
    return fs_read_file_range_at(path, 0, 0, 0, &ignored) == OK;
}

static int update_validate_applicability(update_workspace_t* workspace,
                                         update_verification_t* output) {
    if (workspace->parsed.architecture != UPDATE_ARCH_I386) {
        return update_reject(output, ZUPD_REASON_ARCHITECTURE,
                             "Arquitetura ZUPD incompativel");
    }
    if (!update_state_healthy) {
        return update_fail(output, ZUPD_REASON_NONE, ERR_STATE,
                           "Estado instalado U3 indisponivel");
    }
    if (update_version_compare(&workspace->parsed.base_version,
                               &update_state.installed_version) != 0 ||
        workspace->parsed.base_epoch != update_state.installed_epoch) {
        return update_reject(output, ZUPD_REASON_BASE_VERSION,
                             "Versao base ZUPD incompativel");
    }
    if (update_version_compare(&workspace->parsed.target_version,
                               &workspace->parsed.base_version) <= 0 ||
        workspace->parsed.target_epoch < workspace->parsed.base_epoch) {
        return update_reject(output, ZUPD_REASON_DOWNGRADE,
                             "ZUPD representa downgrade");
    }
    for (uint32_t index = 0; index < workspace->parsed.entry_count; index++) {
        const char* path = workspace->entries[index].path;
        if (!update_path_allowed(path) || !update_target_exists(path)) {
            return update_reject(output, ZUPD_REASON_PATH_POLICY,
                                 "Target ZUPD ausente ou fora da allowlist");
        }
    }
    return OK;
}

static void update_fill_result(update_verification_t* output,
                               const update_header_t* parsed) {
    output->reason = ZUPD_REASON_NONE;
    output->base_version = parsed->base_version;
    output->target_version = parsed->target_version;
    output->base_epoch = parsed->base_epoch;
    output->target_epoch = parsed->target_epoch;
    output->total_size = parsed->total_size;
    output->entry_count = parsed->entry_count;
}

static int update_verify_loaded(const char* path,
                                update_verification_t* output) {
    update_workspace_t* workspace = &update_workspace;
    int result = update_hash_content(path, workspace, output);

    if (result != OK) return result;
    if (!crypto_equal(workspace->parsed.key_id, UPDATE_TRUST_KEY_ID,
                      sizeof(workspace->parsed.key_id))) {
        return update_reject(output, ZUPD_REASON_UNKNOWN_KEY,
                             "key_id ZUPD desconhecido");
    }
    result = crypto_ed25519_verify_final(&workspace->signature_verify);
    if (result != OK) {
        return update_reject(output, ZUPD_REASON_SIGNATURE,
                             "Assinatura ZUPD invalida");
    }
    update_fill_result(output, &workspace->parsed);
    result = update_validate_applicability(workspace, output);
    if (result != OK) return result;
    return OK;
}

static int update_load_structure(const char* path,
                                  update_verification_t* output) {
    update_workspace_t* workspace = &update_workspace;
    int result = update_read_exact(
        path, 0, workspace->header, ZUPD_HEADER_SIZE, output);

    if (result != OK) return result;
    result = update_validate_header_limits(workspace->header, output);
    if (result != OK) return result;
    result = update_validate_header_algorithms(workspace->header, output);
    if (result != OK) return result;
    result = update_validate_header_layout(workspace->header, output);
    if (result != OK) return result;
    update_decode_header(&workspace->parsed, workspace->header);
    result = update_check_exact_file_size(path, &workspace->parsed, output);
    if (result != OK) return result;
    result = update_read_exact(
        path, ZUPD_HEADER_SIZE, workspace->table,
        workspace->parsed.manifest_size, output);
    if (result != OK) return result;
    result = update_parse_entries(workspace, output);
    if (result != OK) return result;
    return update_read_exact(
        path, workspace->parsed.signature_offset, workspace->signature,
        UPDATE_SIGNATURE_SIZE, output);
}

static int update_begin_operation(void) {
    spinlock_acquire(&update_lock);
    if (update_workspace.busy) {
        spinlock_release(&update_lock);
        LOG_ERROR("UPDATE", "Operacao concorrente recusada");
        return ERR_STATE;
    }
    update_workspace.busy = 1;
    spinlock_release(&update_lock);
    return OK;
}

static void update_end_operation(void) {
    spinlock_acquire(&update_lock);
    update_workspace.busy = 0;
    spinlock_release(&update_lock);
}

static int update_verify_internal(const char* path,
                                  update_verification_t* result_out) {
    int result = update_load_structure(path, result_out);

    if (result == OK) result = update_verify_loaded(path, result_out);
    return result;
}

static void update_fill_action_result(
    update_action_result_t* output,
    const update_version_t* from, uint32_t from_epoch,
    const update_version_t* to, uint32_t to_epoch,
    uint16_t entry_count) {
    kmemset(output, 0, sizeof(*output));
    output->reason = UPDATE_ACTION_NONE;
    output->from_version = *from;
    output->from_epoch = from_epoch;
    output->to_version = *to;
    output->to_epoch = to_epoch;
    output->entry_count = entry_count;
}

static int update_action_fail(update_action_result_t* output,
                              update_action_reason_t reason,
                              int error, const char* message) {
    if (output) output->reason = reason;
    LOG_ERROR("UPDATE", message);
    return error;
}

static update_history_outcome_t update_history_action_outcome(
    int result, const update_action_result_t* action) {
    if (result == OK) return UPDATE_HISTORY_OUTCOME_SUCCESS;
    if (action && action->reason == UPDATE_ACTION_CANCELLED) {
        return UPDATE_HISTORY_OUTCOME_CANCELLED;
    }
    return UPDATE_HISTORY_OUTCOME_FAILED;
}

static void update_history_record_action(
    update_history_operation_t operation, const char* path,
    int result, const update_action_result_t* action) {
    update_history_entry_t entry;

    if (!action || action->recovery_pending) return;
    kmemset(&entry, 0, sizeof(entry));
    entry.operation = operation;
    entry.outcome = update_history_action_outcome(result, action);
    entry.action_reason = action->reason;
    entry.verification_reason = action->verification_reason;
    entry.from_version = action->from_version;
    entry.to_version = action->to_version;
    entry.from_epoch = action->from_epoch;
    entry.to_epoch = action->to_epoch;
    entry.entry_count = action->entry_count;
    entry.completed_entries = action->completed_entries;
    entry.reboot_required = action->reboot_required;
    if (operation == UPDATE_HISTORY_OPERATION_APPLY) {
        update_history_copy_alias(entry.package_alias, path);
    }
    if (update_history_append(&entry) != OK) {
        LOG_WARN("UPDATE", "Operacao concluida sem persistir historico U4");
    }
}

static int update_cancelled(const update_action_options_t* options) {
    return options && options->cancel_check &&
           options->cancel_check(options->cancel_context);
}

static void update_remove_if_present(const char* path) {
    int result = fs_atomic_delete_root(path);

    if (result != OK && result != ERR_NOT_FOUND) {
        LOG_WARN("UPDATE", "Falha ao limpar arquivo transacional");
    }
}

static void update_cleanup_slot(uint8_t slot, int backups, int stages) {
    if (slot > 1U) return;
    for (uint32_t index = 0; index < UPDATE_TARGET_COUNT; index++) {
        if (stages) update_remove_if_present(update_stage_paths[slot][index]);
        if (backups) update_remove_if_present(update_backup_paths[slot][index]);
    }
}

static int update_read_payload(const char* package_path,
                               const update_entry_t* entry) {
    uint32_t bytes_read = 0;
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    if (!package_path || !entry ||
        entry->payload_size > sizeof(update_workspace.file_buffer)) {
        LOG_ERROR("UPDATE", "Payload U3 invalido para leitura");
        return ERR_INVALID;
    }
    result = fs_read_file_range_at(
        package_path, entry->payload_offset, update_workspace.file_buffer,
        entry->payload_size, &bytes_read);
    if (result != OK || bytes_read != entry->payload_size) {
        LOG_ERROR("UPDATE", "Falha ao ler payload U3");
        return ERR_DISK;
    }
    result = crypto_sha256(
        update_workspace.file_buffer, entry->payload_size, hash);
    if (result != OK) return result;
    if (!crypto_equal(hash, entry->payload_hash, sizeof(hash))) {
        LOG_ERROR("UPDATE", "Hash do payload U3 mudou apos verificacao");
        return ERR_INVALID;
    }
    return OK;
}

static uint32_t update_clusters_for(uint32_t size, uint32_t cluster_size) {
    return (size + cluster_size - 1U) / cluster_size;
}

static int update_check_apply_space(const update_journal_t* journal) {
    fs_info_t info;
    uint32_t cluster_size;
    uint32_t required = UPDATE_APPLY_METADATA_CLUSTERS;
    uint32_t largest_old = 0;
    uint32_t largest_new = 0;

    if (!journal || fs_get_info(&info) != OK ||
        info.bytes_per_sector == 0U ||
        info.sectors_per_cluster == 0U) {
        LOG_ERROR("UPDATE", "Geometria indisponivel no preflight U3");
        return ERR_DISK;
    }
    cluster_size = info.bytes_per_sector * info.sectors_per_cluster;
    for (uint32_t index = 0; index < journal->entry_count; index++) {
        const update_journal_entry_t* entry = &journal->entries[index];

        required += update_clusters_for(entry->old_file.size, cluster_size);
        required += update_clusters_for(entry->new_file.size, cluster_size);
        if (entry->old_file.size > largest_old) {
            largest_old = entry->old_file.size;
        }
        if (entry->new_file.size > largest_new) {
            largest_new = entry->new_file.size;
        }
    }
    required += update_clusters_for(UPDATE_CONTROL_SIZE, cluster_size);
    required += update_clusters_for(largest_old, cluster_size);
    required += update_clusters_for(largest_new, cluster_size);
    if (required > info.free_clusters) {
        LOG_ERROR("UPDATE", "Espaco insuficiente para transacao U3");
        return ERR_OVERFLOW;
    }
    return OK;
}

static int update_build_apply_journal(update_journal_t* journal) {
    uint32_t sequence;

    if (!journal || update_workspace.parsed.entry_count > UPDATE_TARGET_COUNT) {
        LOG_ERROR("UPDATE", "Pacote invalido para journal de aplicacao");
        return ERR_INVALID;
    }
    sequence = update_journal.sequence;
    kmemset(journal, 0, sizeof(*journal));
    journal->sequence = sequence;
    journal->kind = UPDATE_JOURNAL_APPLY;
    journal->phase = UPDATE_PHASE_PREPARED;
    journal->slot = update_state.rollback_available ?
                    (uint8_t)(1U - update_state.rollback_slot) : 0U;
    journal->entry_count = (uint8_t)update_workspace.parsed.entry_count;
    journal->base_state_sequence = update_state.sequence;
    journal->base_version = update_workspace.parsed.base_version;
    journal->target_version = update_workspace.parsed.target_version;
    journal->base_epoch = update_workspace.parsed.base_epoch;
    journal->target_epoch = update_workspace.parsed.target_epoch;

    for (uint32_t index = 0; index < journal->entry_count; index++) {
        const update_entry_t* source = &update_workspace.entries[index];
        update_journal_entry_t* target = &journal->entries[index];
        int target_id = update_target_id(source->path);

        if (target_id < 0 ||
            !update_state.current[target_id].present) {
            LOG_ERROR("UPDATE", "Alvo invalido no journal de aplicacao");
            return ERR_INVALID;
        }
        target->target_id = (uint8_t)target_id;
        target->old_file = update_state.current[target_id];
        target->new_file.size = source->payload_size;
        target->new_file.present = 1;
        kmemcpy(target->new_file.hash, source->payload_hash,
                CRYPTO_SHA256_SIZE);
    }
    return update_check_apply_space(journal);
}

static int update_stage_apply_files(
    const char* package_path, const update_journal_t* journal,
    const update_action_options_t* options) {
    update_cleanup_slot(journal->slot, 1, 1);
    for (uint32_t index = 0; index < journal->entry_count; index++) {
        const update_entry_t* source = &update_workspace.entries[index];
        const update_journal_entry_t* entry = &journal->entries[index];
        const char* stage = update_stage_paths[journal->slot][entry->target_id];
        int result;

        if (update_cancelled(options)) {
            LOG_WARN("UPDATE", "Aplicacao cancelada durante staging");
            return ERR_TIMEOUT;
        }
        result = update_read_payload(package_path, source);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao preparar payload de staging");
            return result;
        }
        result = fs_atomic_write_root(
            stage, update_workspace.file_buffer, source->payload_size,
            UPDATE_STORAGE_ATTRIBUTES, FS_ATOMIC_CREATE_OR_REPLACE);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao gravar staging U3");
            return result;
        }
        result = update_verify_root_file(stage, &entry->new_file);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Staging U3 nao passou na validacao");
            return result;
        }
    }
    for (uint32_t index = 0; index < journal->entry_count; index++) {
        const update_journal_entry_t* entry = &journal->entries[index];
        const char* target = update_target_paths[entry->target_id];
        const char* backup =
            update_backup_paths[journal->slot][entry->target_id];
        int result;

        if (update_cancelled(options)) {
            LOG_WARN("UPDATE", "Aplicacao cancelada durante backup");
            return ERR_TIMEOUT;
        }
        result = update_load_root_file(target, &entry->old_file);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao carregar alvo para backup");
            return result;
        }
        result = fs_atomic_write_root(
            backup, update_workspace.file_buffer, entry->old_file.size,
            UPDATE_STORAGE_ATTRIBUTES, FS_ATOMIC_CREATE_OR_REPLACE);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao gravar backup U3");
            return result;
        }
        result = update_verify_root_file(backup, &entry->old_file);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Backup U3 nao passou na validacao");
            return result;
        }
    }
    return OK;
}

static int update_replace_from_alias(
    const char* alias, uint8_t target_id,
    const update_file_state_t* expected) {
    int result = update_load_root_file(alias, expected);

    if (result != OK) return result;
    result = fs_atomic_write_root(
        update_target_paths[target_id], update_workspace.file_buffer,
        expected->size, FS_ATTRIBUTES_PRESERVE,
        FS_ATOMIC_REPLACE_ONLY);
    if (result != OK) return result;
    return update_verify_root_file(update_target_paths[target_id], expected);
}

static int update_restore_interrupted_apply(void) {
    update_journal.phase = UPDATE_PHASE_REPLACING;
    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        update_journal_entry_t* entry = &update_journal.entries[index];
        const char* backup =
            update_backup_paths[update_journal.slot][entry->target_id];
        int result = update_replace_from_alias(
            backup, entry->target_id, &entry->old_file);

        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao restaurar aplicacao interrompida");
            return result;
        }
        update_journal.progress = (uint8_t)(index + 1U);
        if (update_write_journal_record(&update_journal) != OK) {
            return ERR_DISK;
        }
    }
    if (update_validate_current_state() != OK) {
        LOG_ERROR("UPDATE", "Estado anterior nao foi restaurado");
        return ERR_INVALID;
    }
    update_cleanup_slot(update_journal.slot, 1, 1);
    return update_clear_journal();
}

static void update_apply_state_from_journal(update_state_t* next) {
    next->installed_version = update_journal.target_version;
    next->installed_epoch = update_journal.target_epoch;
    next->previous_version = update_journal.base_version;
    next->previous_epoch = update_journal.base_epoch;
    next->rollback_available = 1;
    next->rollback_slot = update_journal.slot;
    next->rollback_entry_count = update_journal.entry_count;
    kmemset(next->rollback, 0, sizeof(next->rollback));
    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        const update_journal_entry_t* entry = &update_journal.entries[index];

        next->current[entry->target_id] = entry->new_file;
        next->rollback[entry->target_id] = entry->old_file;
    }
}

static void update_rollback_state_from_journal(update_state_t* next) {
    next->installed_version = update_journal.target_version;
    next->installed_epoch = update_journal.target_epoch;
    kmemset(&next->previous_version, 0, sizeof(next->previous_version));
    next->previous_epoch = 0;
    next->rollback_available = 0;
    next->rollback_slot = 0;
    next->rollback_entry_count = 0;
    kmemset(next->rollback, 0, sizeof(next->rollback));
    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        const update_journal_entry_t* entry = &update_journal.entries[index];
        next->current[entry->target_id] = entry->new_file;
    }
}

static int update_finalize_committed(void) {
    update_state_t next = update_state;
    uint8_t previous_slot = update_state.rollback_slot;
    uint8_t previous_available = update_state.rollback_available;
    int result;

    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        const update_journal_entry_t* entry = &update_journal.entries[index];
        result = update_verify_root_file(
            update_target_paths[entry->target_id], &entry->new_file);
        if (result != OK) {
            LOG_ERROR("UPDATE", "Alvo comprometido nao passou na validacao");
            return result;
        }
    }
    if (update_journal.kind == UPDATE_JOURNAL_APPLY) {
        update_apply_state_from_journal(&next);
    } else if (update_journal.kind == UPDATE_JOURNAL_ROLLBACK) {
        update_rollback_state_from_journal(&next);
    } else {
        LOG_ERROR("UPDATE", "Tipo de journal invalido no commit");
        return ERR_INVALID;
    }
    result = update_write_state_record(&next);
    if (result != OK) return result;
    update_state = next;

    update_cleanup_slot(update_journal.slot,
                        update_journal.kind == UPDATE_JOURNAL_ROLLBACK, 1);
    if (update_journal.kind == UPDATE_JOURNAL_APPLY &&
        previous_available && previous_slot != update_journal.slot) {
        update_cleanup_slot(previous_slot, 1, 1);
    }
    result = update_clear_journal();
    if (result != OK) return result;
    return update_validate_current_state();
}

static int update_continue_interrupted_rollback(void) {
    update_journal.phase = UPDATE_PHASE_REPLACING;
    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        update_journal_entry_t* entry = &update_journal.entries[index];
        const char* backup =
            update_backup_paths[update_journal.slot][entry->target_id];
        int result = update_replace_from_alias(
            backup, entry->target_id, &entry->new_file);

        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao continuar rollback interrompido");
            return result;
        }
        update_journal.progress = (uint8_t)(index + 1U);
        if (update_write_journal_record(&update_journal) != OK) {
            return ERR_DISK;
        }
    }
    update_journal.phase = UPDATE_PHASE_COMMITTED;
    if (update_write_journal_record(&update_journal) != OK) return ERR_DISK;
    return update_finalize_committed();
}

static void update_history_record_recovery(
    const update_journal_t* pending, uint8_t interrupted_progress) {
    update_history_entry_t failed;
    update_history_entry_t recovered;
    int is_apply;

    if (!pending) return;
    is_apply = pending->kind == UPDATE_JOURNAL_APPLY;
    kmemset(&failed, 0, sizeof(failed));
    failed.operation = is_apply ?
        UPDATE_HISTORY_OPERATION_APPLY :
        UPDATE_HISTORY_OPERATION_ROLLBACK;
    failed.outcome = UPDATE_HISTORY_OUTCOME_FAILED;
    failed.action_reason = UPDATE_ACTION_RECOVERY_PENDING;
    failed.from_version = pending->base_version;
    failed.to_version = pending->target_version;
    failed.from_epoch = pending->base_epoch;
    failed.to_epoch = pending->target_epoch;
    failed.entry_count = pending->entry_count;
    failed.completed_entries = interrupted_progress;
    if (update_history_append(&failed) != OK) {
        LOG_WARN("UPDATE", "Falha ao registrar transacao interrompida");
    }

    recovered = failed;
    recovered.sequence = 0;
    recovered.operation = is_apply ?
        UPDATE_HISTORY_OPERATION_RECOVERY_APPLY :
        UPDATE_HISTORY_OPERATION_RECOVERY_ROLLBACK;
    recovered.outcome = UPDATE_HISTORY_OUTCOME_RECOVERED;
    recovered.action_reason = UPDATE_ACTION_NONE;
    recovered.completed_entries = pending->entry_count;
    if (is_apply) {
        recovered.from_version = pending->target_version;
        recovered.to_version = pending->base_version;
        recovered.from_epoch = pending->target_epoch;
        recovered.to_epoch = pending->base_epoch;
    }
    if (update_history_append(&recovered) != OK) {
        LOG_WARN("UPDATE", "Recuperacao concluida sem historico U4");
    }
}

static int update_recover_pending(void) {
    update_journal_t pending;
    uint8_t interrupted_progress;
    int result;

    if (update_journal.kind == UPDATE_JOURNAL_NONE) return OK;
    pending = update_journal;
    interrupted_progress = update_journal.progress;
    LOG_WARN("UPDATE", "Transacao U3 pendente encontrada no boot");
    if (update_journal.phase == UPDATE_PHASE_COMMITTED) {
        result = update_finalize_committed();
    } else if (update_journal.kind == UPDATE_JOURNAL_APPLY) {
        result = update_restore_interrupted_apply();
    } else if (update_journal.kind == UPDATE_JOURNAL_ROLLBACK) {
        result = update_continue_interrupted_rollback();
    } else {
        return ERR_INVALID;
    }
    if (result == OK) {
        update_history_record_recovery(&pending, interrupted_progress);
    }
    return result;
}

static void update_set_compile_time_version(void) {
    kmemset(&update_state, 0, sizeof(update_state));
    update_state.installed_version.major = ZEPHYROS_VERSION_MAJOR;
    update_state.installed_version.minor = ZEPHYROS_VERSION_MINOR;
    update_state.installed_version.patch = ZEPHYROS_VERSION_PATCH;
    update_state.installed_epoch = ZEPHYROS_VERSION_EPOCH;
}

static void update_refresh_capabilities(void) {
    update_capabilities.verifier_ready = update_initialized;
    update_capabilities.local_file_available =
        fs_get_type() != FS_TYPE_NONE;
    update_capabilities.persistent_state_ready =
        fs_get_type() == FS_TYPE_FAT12 && update_state_healthy;
    update_capabilities.recovery_pending =
        update_journal.kind != UPDATE_JOURNAL_NONE;
    update_capabilities.apply_available =
        update_initialized && fs_get_type() == FS_TYPE_FAT12 &&
        update_state_healthy &&
        update_journal.kind == UPDATE_JOURNAL_NONE;
    update_capabilities.rollback_available =
        update_capabilities.apply_available &&
        update_state.rollback_available;
    update_capabilities.history_available =
        fs_get_type() == FS_TYPE_FAT12 &&
        (update_history_store == UPDATE_STORE_EMPTY ||
         update_history_store == UPDATE_STORE_VALID) &&
        !update_history_write_failed;
    update_capabilities.remote_available =
        update_remote_capability_available() ? 1U : 0U;
}

#if defined(ZEPHYROS_HOST_TEST)
static void update_host_fill_file_state(update_file_state_t* state,
                                         uint32_t size, uint8_t seed,
                                         uint8_t present) {
    state->size = size;
    state->present = present;
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        state->hash[index] = (uint8_t)(seed + index);
    }
}

static void update_host_write_u16(uint8_t* data, uint32_t offset,
                                  uint16_t value) {
    update_write_u16(data + offset, value);
}

static void update_host_write_u32(uint8_t* data, uint32_t offset,
                                  uint32_t value) {
    update_write_u32(data + offset, value);
}

static int update_host_check_state_records(void) {
    update_state_t source;
    update_state_t decoded;
    uint8_t record[UPDATE_CONTROL_SIZE];
    int result;

    kmemset(&source, 0, sizeof(source));
    source.sequence = 7U;
    source.installed_version.major = 1U;
    source.installed_version.minor = 2U;
    source.installed_version.patch = 3U;
    source.installed_epoch = 40U;
    source.rollback_available = 1U;
    source.rollback_slot = 1U;
    source.rollback_entry_count = 1U;
    source.previous_version.major = 1U;
    source.previous_version.minor = 2U;
    source.previous_version.patch = 2U;
    source.previous_epoch = 39U;
    update_host_fill_file_state(&source.current[0], 11U, 0x10U, 1U);
    update_host_fill_file_state(&source.rollback[1], 9U, 0x40U, 1U);
    result = update_encode_state_record(&source, record);
    if (result != OK) return 101;
    if (update_decode_state_record(&decoded, record) != OK ||
        decoded.sequence != source.sequence ||
        decoded.current[0].size != source.current[0].size ||
        decoded.rollback[1].present != 1U) {
        return 102;
    }
    if (update_decode_state_record(0, record) != ERR_NULL) return 103;
    record[18] = 1U;
    if (update_decode_state_record(&decoded, record) != ERR_INVALID) {
        return 104;
    }
    return OK;
}

static int update_host_check_journal_record(void) {
    update_journal_t source;
    update_journal_t decoded;
    uint8_t record[UPDATE_CONTROL_SIZE];
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    kmemset(&source, 0, sizeof(source));
    source.sequence = 8U;
    source.kind = UPDATE_JOURNAL_APPLY;
    source.phase = UPDATE_PHASE_REPLACING;
    source.slot = 0U;
    source.entry_count = 2U;
    source.progress = 1U;
    source.base_state_sequence = 7U;
    source.base_version.major = 1U;
    source.base_version.minor = 2U;
    source.base_version.patch = 3U;
    source.target_version.major = 1U;
    source.target_version.minor = 3U;
    source.target_version.patch = 0U;
    source.base_epoch = 40U;
    source.target_epoch = 41U;
    for (uint32_t index = 0U; index < 2U; index++) {
        source.entries[index].target_id = (uint8_t)index;
        update_host_fill_file_state(&source.entries[index].old_file,
                                    10U + index, (uint8_t)(0x50U + index), 1U);
        update_host_fill_file_state(&source.entries[index].new_file,
                                    12U + index, (uint8_t)(0x70U + index), 1U);
    }
    result = update_encode_journal_record(&source, record);
    if (result != OK) return 111;
    if (update_decode_journal_record(&decoded, record) != OK ||
        decoded.sequence != source.sequence || decoded.entry_count != 2U ||
        decoded.entries[1].new_file.size != 13U) {
        return 112;
    }
    record[15] = 3U;
    if (crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash) != OK) {
        return 113;
    }
    kmemcpy(record + UPDATE_CONTROL_HASH_OFFSET, hash, sizeof(hash));
    if (update_decode_journal_record(&decoded, record) != ERR_INVALID) {
        return 114;
    }
    if (update_decode_journal_record(0, record) != ERR_NULL) return 115;
    return OK;
}

static void update_host_fill_history_entry(update_history_entry_t* entry,
                                           uint32_t sequence,
                                           update_history_operation_t operation,
                                           update_history_outcome_t outcome,
                                           const char* alias) {
    uint32_t alias_size = kstrlen(alias);

    kmemset(entry, 0, sizeof(*entry));
    entry->sequence = sequence;
    entry->operation = operation;
    entry->outcome = outcome;
    entry->action_reason = UPDATE_ACTION_NONE;
    entry->verification_reason = ZUPD_REASON_NONE;
    entry->from_version.major = 1U;
    entry->to_version.major = 2U;
    entry->from_epoch = sequence;
    entry->to_epoch = sequence + 1U;
    entry->entry_count = 1U;
    entry->completed_entries = 1U;
    entry->reboot_required = (uint8_t)(sequence & 1U);
    if (alias_size >= UPDATE_HISTORY_PACKAGE_SIZE) {
        alias_size = UPDATE_HISTORY_PACKAGE_SIZE - 1U;
    }
    kmemcpy(entry->package_alias, alias, alias_size);
}

static int update_host_check_history_record(void) {
    static const char valid_alias[UPDATE_HISTORY_PACKAGE_SIZE] = "VALID.ZUP";
    static const char invalid_alias[UPDATE_HISTORY_PACKAGE_SIZE] = {
        'B', 'A', 'D', '/', 'N', 'A', 'M', 'E', 0
    };
    update_history_t source;
    update_history_t decoded;
    uint8_t record[UPDATE_CONTROL_SIZE];
    uint8_t hash[CRYPTO_SHA256_SIZE];
    int result;

    kmemset(&source, 0, sizeof(source));
    source.sequence = 2U;
    source.count = 2U;
    source.next_index = 2U;
    update_host_fill_history_entry(&source.entries[0], 1U,
                                   UPDATE_HISTORY_OPERATION_APPLY,
                                   UPDATE_HISTORY_OUTCOME_SUCCESS, "A.ZUP");
    update_host_fill_history_entry(&source.entries[1], 2U,
                                   UPDATE_HISTORY_OPERATION_ROLLBACK,
                                   UPDATE_HISTORY_OUTCOME_RECOVERED, "B.ZUP");
    result = update_encode_history_record(&source, record);
    if (result != OK) return 121;
    if (update_decode_history_record(&decoded, record) != OK ||
        decoded.sequence != 2U || decoded.count != 2U ||
        decoded.entries[1].operation != UPDATE_HISTORY_OPERATION_ROLLBACK) {
        return 122;
    }
    if (!update_history_alias_valid(valid_alias) ||
        update_history_alias_valid(invalid_alias)) {
        return 123;
    }
    record[46U + UPDATE_HISTORY_ENTRY_OFFSET] = 1U;
    if (crypto_sha256(record, UPDATE_CONTROL_HASH_OFFSET, hash) != OK) {
        return 124;
    }
    kmemcpy(record + UPDATE_CONTROL_HASH_OFFSET, hash, sizeof(hash));
    if (update_decode_history_record(&decoded, record) != ERR_INVALID) {
        return 125;
    }
    if (update_decode_history_record(0, record) != ERR_NULL) return 126;
    return OK;
}

static void update_host_fill_header(uint8_t header[ZUPD_HEADER_SIZE]) {
    kmemset(header, 0, ZUPD_HEADER_SIZE);
    kmemcpy(header, "ZUPD", 4U);
    update_host_write_u16(header, HEADER_FORMAT_VERSION, UPDATE_FORMAT_VERSION);
    update_host_write_u16(header, HEADER_SIZE, ZUPD_HEADER_SIZE);
    update_host_write_u32(header, HEADER_ARCHITECTURE, UPDATE_ARCH_I386);
    update_host_write_u32(header, HEADER_TOTAL_SIZE, 321U);
    update_host_write_u32(header, HEADER_MANIFEST_OFFSET, ZUPD_HEADER_SIZE);
    update_host_write_u32(header, HEADER_MANIFEST_SIZE, ZUPD_ENTRY_SIZE);
    update_host_write_u32(header, HEADER_PAYLOAD_OFFSET, 256U);
    update_host_write_u32(header, HEADER_PAYLOAD_SIZE, 1U);
    update_host_write_u32(header, HEADER_SIGNATURE_OFFSET, 257U);
    update_host_write_u16(header, HEADER_SIGNATURE_SIZE, UPDATE_SIGNATURE_SIZE);
    update_host_write_u16(header, HEADER_SIGNATURE_ALGORITHM,
                          UPDATE_SIGNATURE_ED25519);
    update_host_write_u16(header, HEADER_HASH_ALGORITHM, UPDATE_HASH_SHA256);
    update_host_write_u16(header, HEADER_ENTRY_COUNT, 1U);
    update_host_write_u16(header, HEADER_ENTRY_SIZE, ZUPD_ENTRY_SIZE);
}

static int update_host_check_headers_and_paths(void) {
    uint8_t header[ZUPD_HEADER_SIZE];
    uint8_t encoded[ZUPD_PATH_SIZE];
    uint8_t table[ZUPD_ENTRY_SIZE * 2U];
    update_verification_t verification;
    update_header_t parsed;
    update_workspace_t workspace;
    update_version_t first;
    update_version_t second;
    int result;

    update_host_fill_header(header);
    kmemset(&verification, 0, sizeof(verification));
    if (update_validate_header_limits(header, &verification) != OK ||
        update_validate_header_algorithms(header, &verification) != OK ||
        update_validate_header_layout(header, &verification) != OK) {
        return 131;
    }
    update_decode_header(&parsed, header);
    if (parsed.architecture != UPDATE_ARCH_I386 || parsed.entry_count != 1U) {
        return 132;
    }
    header[HEADER_HASH_ALGORITHM] = 0U;
    if (update_validate_header_algorithms(header, &verification) !=
        ERR_UNAVAILABLE) {
        return 133;
    }
    update_host_fill_header(header);
    header[HEADER_TOTAL_SIZE] = 0U;
    if (update_validate_header_limits(header, &verification) != ERR_OVERFLOW) {
        return 134;
    }
    update_host_fill_header(header);
    header[0] = 'X';
    if (update_validate_header_layout(header, &verification) != ERR_INVALID) {
        return 135;
    }
    if (!update_validate_path_syntax("FILE.TXT") ||
        update_validate_path_syntax("file.TXT") ||
        update_validate_path_syntax("TOOLONG01.TXT") ||
        update_validate_path_syntax("BAD.TOOL")) {
        return 136;
    }
    kmemset(encoded, 0, sizeof(encoded));
    kmemcpy(encoded, "FILE.TXT", 8U);
    if (update_decode_path((char*)table, encoded, &verification) != OK ||
        kstrcmp((char*)table, "FILE.TXT") != 0) {
        return 137;
    }
    encoded[9] = 'X';
    if (update_decode_path((char*)table, encoded, &verification) != ERR_INVALID) {
        return 138;
    }
    if (update_compare_paths("A.TXT", "B.TXT") >= 0 ||
        update_compare_paths("B.TXT", "A.TXT") <= 0 ||
        update_compare_paths("A.TXT", "A.TXT") != 0 ||
        update_target_id("SHELL.BMP") != 1 || update_target_id("NOPE.BMP") != -1 ||
        !update_path_allowed("TASKMGR.BMP") || update_path_allowed("NOPE.BMP")) {
        return 139;
    }
    first.major = 1U; first.minor = 0U; first.patch = 0U;
    second = first;
    if (update_version_compare(&first, &second) != 0) return 140;
    second.patch = 1U;
    if (update_version_compare(&first, &second) >= 0) return 141;
    second.major = 0U;
    if (update_version_compare(&first, &second) <= 0) return 142;
    kmemset(table, 0, sizeof(table));
    workspace.parsed.payload_offset = 256U;
    workspace.parsed.signature_offset = 259U;
    workspace.parsed.entry_count = 2U;
    kmemcpy(table, "A.TXT", 5U);
    update_host_write_u32(table, ENTRY_PAYLOAD_OFFSET, 256U);
    update_host_write_u32(table, ENTRY_PAYLOAD_SIZE, 1U);
    update_host_write_u32(table, ENTRY_INSTALLED_SIZE, 1U);
    update_host_write_u16(table, ENTRY_OPERATION, UPDATE_OPERATION_REPLACE);
    update_host_write_u16(table, ENTRY_TARGET_CLASS, UPDATE_TARGET_SYSTEM_FILE);
    kmemcpy(table + ZUPD_ENTRY_SIZE, "B.TXT", 5U);
    update_host_write_u32(table + ZUPD_ENTRY_SIZE, ENTRY_PAYLOAD_OFFSET, 257U);
    update_host_write_u32(table + ZUPD_ENTRY_SIZE, ENTRY_PAYLOAD_SIZE, 2U);
    update_host_write_u32(table + ZUPD_ENTRY_SIZE, ENTRY_INSTALLED_SIZE, 2U);
    update_host_write_u16(table + ZUPD_ENTRY_SIZE, ENTRY_OPERATION,
                          UPDATE_OPERATION_REPLACE);
    update_host_write_u16(table + ZUPD_ENTRY_SIZE, ENTRY_TARGET_CLASS,
                          UPDATE_TARGET_SYSTEM_FILE);
    kmemcpy(workspace.table, table, sizeof(table));
    result = update_parse_entries(&workspace, &verification);
    if (result != OK) return 143;
    workspace.table[ZUPD_ENTRY_SIZE] = 'A';
    if (update_parse_entries(&workspace, &verification) != ERR_INVALID) return 144;
    return OK;
}

static int update_host_cancel(void* context) {
    return context != 0;
}

static int update_host_check_action_helpers(void) {
    update_action_options_t options;
    update_action_result_t action;
    update_version_t version;

    version.major = 1U; version.minor = 2U; version.patch = 3U;
    update_fill_action_result(&action, &version, 10U, &version, 11U, 2U);
    if (action.reason != UPDATE_ACTION_NONE || action.entry_count != 2U) {
        return 151;
    }
    if (update_action_fail(&action, UPDATE_ACTION_IO, ERR_DISK,
                           "host update action") != ERR_DISK ||
        action.reason != UPDATE_ACTION_IO) {
        return 152;
    }
    if (update_history_action_outcome(OK, &action) !=
            UPDATE_HISTORY_OUTCOME_SUCCESS ||
        update_history_action_outcome(ERR_DISK, &action) !=
            UPDATE_HISTORY_OUTCOME_FAILED) {
        return 153;
    }
    if (update_reason_error(ZUPD_REASON_SIZE) != ERR_OVERFLOW ||
        update_reason_error(ZUPD_REASON_UNSUPPORTED) != ERR_UNAVAILABLE ||
        update_reason_error(ZUPD_REASON_FORMAT) != ERR_INVALID ||
        update_reason_error(ZUPD_REASON_ARCHITECTURE) != ERR_STATE) {
        return 154;
    }
    options.cancel_check = 0;
    options.cancel_context = 0;
    options.dry_run = 0U;
    if (update_cancelled(&options) != 0 || update_cancelled(0) != 0) return 155;
    options.cancel_check = update_host_cancel;
    options.cancel_context = (void*)1;
    if (update_cancelled(&options) != 1) return 156;
    if (update_clusters_for(0U, 512U) != 0U ||
        update_clusters_for(1U, 512U) != 1U ||
        update_clusters_for(513U, 512U) != 2U) {
        return 157;
    }
    return OK;
}

static int update_host_check_public_unavailable(void) {
    update_capabilities_t capabilities;
    update_status_t status;
    update_verification_t verification;
    update_action_result_t action;
    update_history_entry_t history_entry;
    update_version_t version;
    uint32_t epoch = 0U;
    uint32_t count = 0U;

    if (update_init() != OK || !update_is_ready()) return 161;
    if (update_get_capabilities(0) != ERR_NULL ||
        update_get_capabilities(&capabilities) != OK ||
        !capabilities.verifier_ready || capabilities.local_file_available ||
        capabilities.apply_available || capabilities.rollback_available ||
        capabilities.history_available || capabilities.remote_available) {
        return 162;
    }
    if (update_get_status(0) != ERR_NULL || update_get_status(&status) != OK ||
        status.state_store != UPDATE_STORE_UNAVAILABLE ||
        status.current_files != UPDATE_STORE_UNAVAILABLE ||
        status.history_store != UPDATE_STORE_UNAVAILABLE ||
        status.transaction_pending || status.has_last_event) {
        return 163;
    }
    if (update_get_installed_version(0, &epoch) != ERR_NULL ||
        update_get_installed_version(&version, 0) != ERR_NULL ||
        update_get_installed_version(&version, &epoch) != OK ||
        version.major != ZEPHYROS_VERSION_MAJOR ||
        version.minor != ZEPHYROS_VERSION_MINOR ||
        version.patch != ZEPHYROS_VERSION_PATCH ||
        epoch != ZEPHYROS_VERSION_EPOCH) {
        return 164;
    }
    if (update_verify_file(0, &verification) != ERR_NULL ||
        update_verify_file("MISSING.ZUP", &verification) != ERR_UNAVAILABLE ||
        verification.reason != ZUPD_REASON_NONE) {
        return 165;
    }
    if (update_apply_file(0, 0, &action) != ERR_NULL ||
        update_apply_file("MISSING.ZUP", 0, &action) != ERR_UNAVAILABLE ||
        action.reason != UPDATE_ACTION_UNSUPPORTED_FS) {
        return 166;
    }
    if (update_rollback(0, &action) != ERR_UNAVAILABLE ||
        action.reason != UPDATE_ACTION_UNSUPPORTED_FS ||
        update_rollback(0, 0) != ERR_NULL) {
        return 167;
    }
    version.major++;
    if (update_sync_runtime_state(0, epoch) != ERR_NULL ||
        update_sync_runtime_state(&version, epoch) != ERR_STATE) {
        return 168;
    }
    if (update_get_history_count(0) != ERR_NULL ||
        update_get_history_count(&count) != ERR_UNAVAILABLE ||
        update_get_history_entry(0, 0) != ERR_NULL ||
        update_get_history_entry(0, &history_entry) != ERR_UNAVAILABLE) {
        return 169;
    }
    if (kstrcmp(zupd_reason_name(ZUPD_REASON_NONE), "NONE") != 0 ||
        kstrcmp(update_action_reason_name(UPDATE_ACTION_NONE), "NONE") != 0 ||
        kstrcmp(update_store_state_name(UPDATE_STORE_UNAVAILABLE),
                "UNAVAILABLE") != 0 ||
        kstrcmp(update_history_operation_name(UPDATE_HISTORY_OPERATION_APPLY),
                "APPLY") != 0 ||
        kstrcmp(update_history_outcome_name(UPDATE_HISTORY_OUTCOME_FAILED),
                "FAILED") != 0) {
        return 170;
    }
    return OK;
}

int update_host_test_contracts(void) {
    int result;

    spinlock_init(&update_lock);
    result = update_host_check_state_records();
    if (result == OK) result = update_host_check_journal_record();
    if (result == OK) result = update_host_check_history_record();
    if (result == OK) result = update_host_check_headers_and_paths();
    if (result == OK) result = update_host_check_action_helpers();
    if (result == OK) result = update_host_check_public_unavailable();
    return result;
}
#endif

int update_init(void) {
    uint8_t key_hash[CRYPTO_SHA256_SIZE];
    int state_result;
    int journal_result;
    int recovery_result;

    LOG_INFO("UPDATE", "Inicializando atualizacoes locais");
    spinlock_init(&update_lock);
    kmemset(&update_workspace, 0, sizeof(update_workspace));
    kmemset(&update_capabilities, 0, sizeof(update_capabilities));
    kmemset(&update_journal, 0, sizeof(update_journal));
    kmemset(&update_history, 0, sizeof(update_history));
    update_state_record_slot = -1;
    update_journal_record_slot = -1;
    update_history_record_slot = -1;
    update_history_store = UPDATE_STORE_UNAVAILABLE;
    update_history_write_failed = 0;
    update_state_healthy = 0;
    update_fail_after = 0;
    update_initialized = 0;
    if (crypto_self_test() != OK ||
        crypto_sha256(UPDATE_TRUST_PUBLIC_KEY,
                      sizeof(UPDATE_TRUST_PUBLIC_KEY), key_hash) != OK ||
        !crypto_equal(key_hash, UPDATE_TRUST_KEY_ID,
                      sizeof(UPDATE_TRUST_KEY_ID))) {
        LOG_ERROR("UPDATE", "Chave publica ou autoteste invalido");
        return ERR_INVALID;
    }
    update_initialized = 1;
    update_set_compile_time_version();
    if (fs_get_type() != FS_TYPE_FAT12) {
        update_state_healthy = 1;
        update_refresh_capabilities();
        if (update_runtime_init() != OK) {
            LOG_WARN("UPDATE", "Runtime v2 indisponivel sem filesystem FAT12");
        }
        LOG_INFO("UPDATE", "Verificador pronto; aplicacao FAT12 indisponivel");
        return OK;
    }

    state_result = update_load_state_records();
    journal_result = update_load_journal_records();
    (void)update_load_history_records();
    if (state_result != OK || journal_result != OK) {
        update_refresh_capabilities();
        if (update_runtime_init() != OK) {
            LOG_WARN("UPDATE", "Runtime v2 nao iniciou apos falha do estado v1");
        }
        LOG_ERROR("UPDATE", "Estado persistente U3 invalido");
        return OK;
    }
    if (update_state_record_slot < 0 &&
        update_journal.kind == UPDATE_JOURNAL_APPLY) {
        update_state.installed_version = update_journal.base_version;
        update_state.installed_epoch = update_journal.base_epoch;
        for (uint32_t index = 0;
             index < update_journal.entry_count; index++) {
            update_journal_entry_t* entry = &update_journal.entries[index];
            update_state.current[entry->target_id] = entry->old_file;
        }
    }
    recovery_result = update_recover_pending();
    if (recovery_result != OK || update_validate_current_state() != OK) {
        update_refresh_capabilities();
        if (update_runtime_init() != OK) {
            LOG_WARN("UPDATE", "Runtime v2 nao iniciou apos recuperacao v1 degradada");
        }
        LOG_ERROR("UPDATE", "Recuperacao ou estado instalado invalido");
        return OK;
    }
    update_state_healthy = 1;
    update_refresh_capabilities();
    if (update_runtime_init() != OK) {
        LOG_WARN("UPDATE", "Runtime v2 indisponivel; ZUPD v1 preservado");
    }
    LOG_INFO("UPDATE", "Atualizacoes locais inicializadas com sucesso");
    return OK;
}

int update_is_ready(void) {
    return update_initialized;
}

int update_get_capabilities(update_capabilities_t* capabilities_out) {
    if (!capabilities_out) {
        LOG_ERROR("UPDATE", "Destino de capacidades nulo");
        return ERR_NULL;
    }
    if (!update_initialized) {
        LOG_ERROR("UPDATE", "Capacidades consultadas antes da inicializacao");
        return ERR_STATE;
    }
    update_refresh_capabilities();
    *capabilities_out = update_capabilities;
    return OK;
}

int update_verify_file(const char* path, update_verification_t* result_out) {
    int result;

    if (!path || !result_out) {
        LOG_ERROR("UPDATE", "Argumento nulo na verificacao ZUPD");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    if (!update_initialized) {
        return update_fail(result_out, ZUPD_REASON_NONE, ERR_STATE,
                           "Verificador ZUPD nao inicializado");
    }
    if (fs_get_type() == FS_TYPE_NONE) {
        return update_fail(result_out, ZUPD_REASON_NONE, ERR_UNAVAILABLE,
                           "Filesystem indisponivel para ZUPD");
    }
    if (update_begin_operation() != OK) {
        return update_fail(result_out, ZUPD_REASON_NONE, ERR_STATE,
                           "Verificacao ZUPD concorrente recusada");
    }
    result = update_verify_internal(path, result_out);
    update_end_operation();
    if (result == OK) {
        LOG_INFO("UPDATE", "Artefato ZUPD autenticado e compativel");
    }
    return result;
}

int update_get_installed_version(update_version_t* version_out,
                                 uint32_t* epoch_out) {
    if (!version_out || !epoch_out) {
        LOG_ERROR("UPDATE", "Destino nulo na consulta de versao instalada");
        return ERR_NULL;
    }
    if (!update_initialized || !update_state_healthy) {
        LOG_ERROR("UPDATE", "Versao instalada indisponivel");
        return ERR_STATE;
    }
    *version_out = update_state.installed_version;
    *epoch_out = update_state.installed_epoch;
    return OK;
}

int update_sync_runtime_state(const update_version_t* installed_version,
                              uint32_t installed_epoch) {
    update_state_t next;
    int result = OK;

    if (!installed_version) {
        LOG_ERROR("UPDATE", "Versao nula ao sincronizar estado runtime");
        return ERR_NULL;
    }
    if (!update_initialized || fs_get_type() != FS_TYPE_FAT12 ||
        update_journal.kind != UPDATE_JOURNAL_NONE) {
        LOG_ERROR("UPDATE", "Estado U3 nao permite sincronizacao runtime");
        return ERR_STATE;
    }
    if (update_begin_operation() != OK) {
        LOG_ERROR("UPDATE", "Sincronizacao runtime concorrente recusada");
        return ERR_STATE;
    }
    next = update_state;
    for (uint32_t index = 0U; index < UPDATE_TARGET_COUNT; index++) {
        uint32_t ignored_size = 0U;

        result = fs_get_root_file_info(update_target_paths[index],
                                       &ignored_size, 0);
        if (result == ERR_NOT_FOUND) {
            kmemset(&next.current[index], 0, sizeof(next.current[index]));
            result = OK;
        } else if (result == OK) {
            result = update_hash_root_file(update_target_paths[index],
                                           &next.current[index]);
        }
        if (result != OK) {
            LOG_ERROR("UPDATE", "Falha ao atualizar estado U3 do runtime");
            break;
        }
    }
    if (result == OK) {
        next.installed_version = *installed_version;
        next.installed_epoch = installed_epoch;
        kmemset(&next.previous_version, 0, sizeof(next.previous_version));
        next.previous_epoch = 0U;
        next.rollback_available = 0U;
        next.rollback_slot = 0U;
        next.rollback_entry_count = 0U;
        kmemset(next.rollback, 0, sizeof(next.rollback));
        result = update_write_state_record(&next);
    }
    if (result == OK) {
        update_state = next;
        update_state_healthy = 1;
        update_refresh_capabilities();
        LOG_INFO("UPDATE", "Estado U3 sincronizado com runtime v2");
    }
    update_end_operation();
    return result;
}

static update_store_state_t update_effective_history_store(void) {
    return update_history_write_failed ?
        UPDATE_STORE_INVALID : update_history_store;
}

int update_get_status(update_status_t* status_out) {
    int validation;
    int result;

    if (!status_out) {
        LOG_ERROR("UPDATE", "Destino nulo na consulta de status");
        return ERR_NULL;
    }
    if (!update_initialized) {
        LOG_ERROR("UPDATE", "Status consultado antes da inicializacao");
        return ERR_STATE;
    }
    result = update_begin_operation();
    if (result != OK) {
        LOG_ERROR("UPDATE", "Status recusado durante outra operacao");
        return result;
    }
    kmemset(status_out, 0, sizeof(*status_out));
    status_out->build_version.major = ZEPHYROS_VERSION_MAJOR;
    status_out->build_version.minor = ZEPHYROS_VERSION_MINOR;
    status_out->build_version.patch = ZEPHYROS_VERSION_PATCH;
    status_out->build_epoch = ZEPHYROS_VERSION_EPOCH;
    status_out->installed_version = update_state.installed_version;
    status_out->installed_epoch = update_state.installed_epoch;
    status_out->rollback_version = update_state.previous_version;
    status_out->rollback_epoch = update_state.previous_epoch;
    status_out->rollback_entry_count = update_state.rollback_entry_count;
    status_out->transaction_pending =
        update_journal.kind != UPDATE_JOURNAL_NONE;
    if (fs_get_type() != FS_TYPE_FAT12) {
        status_out->state_store = UPDATE_STORE_UNAVAILABLE;
        status_out->current_files = UPDATE_STORE_UNAVAILABLE;
    } else if (!update_state_healthy) {
        status_out->state_store = UPDATE_STORE_INVALID;
        status_out->current_files = UPDATE_STORE_INVALID;
    } else {
        validation = update_validate_current_state();
        status_out->state_store = update_state_record_slot < 0 ?
            UPDATE_STORE_EMPTY : UPDATE_STORE_VALID;
        status_out->current_files = validation == OK ?
            UPDATE_STORE_VALID : UPDATE_STORE_INVALID;
    }
    status_out->history_store = update_effective_history_store();
    update_refresh_capabilities();
    status_out->capabilities = update_capabilities;
    if (status_out->state_store == UPDATE_STORE_INVALID ||
        status_out->current_files == UPDATE_STORE_INVALID) {
        status_out->capabilities.persistent_state_ready = 0;
        status_out->capabilities.apply_available = 0;
        status_out->capabilities.rollback_available = 0;
    }
    if ((status_out->history_store == UPDATE_STORE_EMPTY ||
         status_out->history_store == UPDATE_STORE_VALID) &&
        update_history.count > 0U &&
        update_history_newest(0U, &status_out->last_event) == OK) {
        status_out->has_last_event = 1;
    }
    update_end_operation();
    return OK;
}

int update_get_history_count(uint32_t* count_out) {
    int result;

    if (!count_out) {
        LOG_ERROR("UPDATE", "Destino nulo na contagem de historico");
        return ERR_NULL;
    }
    if (!update_initialized) {
        LOG_ERROR("UPDATE", "Historico consultado antes da inicializacao");
        return ERR_STATE;
    }
    result = update_begin_operation();
    if (result != OK) return result;
    if (update_effective_history_store() == UPDATE_STORE_UNAVAILABLE) {
        result = ERR_UNAVAILABLE;
    } else if (update_effective_history_store() == UPDATE_STORE_INVALID) {
        result = ERR_INVALID;
    } else {
        *count_out = update_history.count;
        result = OK;
    }
    update_end_operation();
    if (result != OK) LOG_ERROR("UPDATE", "Historico U4 indisponivel");
    return result;
}

int update_get_history_entry(uint32_t newest_index,
                             update_history_entry_t* entry_out) {
    int result;

    if (!entry_out) {
        LOG_ERROR("UPDATE", "Destino nulo na leitura de historico");
        return ERR_NULL;
    }
    if (!update_initialized) {
        LOG_ERROR("UPDATE", "Entrada de historico consultada sem init");
        return ERR_STATE;
    }
    result = update_begin_operation();
    if (result != OK) return result;
    result = update_history_newest(newest_index, entry_out);
    update_end_operation();
    if (result != OK) LOG_ERROR("UPDATE", "Entrada de historico indisponivel");
    return result;
}

static int update_apply_recover_failure(update_action_result_t* output,
                                        update_action_reason_t reason,
                                        int error) {
    int recovery = update_restore_interrupted_apply();

    if (recovery == OK) {
        update_state_healthy = 1;
        update_refresh_capabilities();
        return update_action_fail(
            output, reason, error,
            reason == UPDATE_ACTION_CANCELLED ?
            "Aplicacao cancelada e estado anterior restaurado" :
            "Aplicacao falhou e estado anterior foi restaurado");
    }
    output->reason = UPDATE_ACTION_RECOVERY_PENDING;
    output->recovery_pending = 1;
    update_state_healthy = 0;
    update_refresh_capabilities();
    return update_action_fail(
        output, UPDATE_ACTION_RECOVERY_PENDING, ERR_STATE,
        "Aplicacao falhou; recuperacao ficou pendente para o boot");
}

static int update_mark_recovery_pending(update_action_result_t* output,
                                        const char* message) {
    output->reason = UPDATE_ACTION_RECOVERY_PENDING;
    output->recovery_pending = 1;
    update_state_healthy = 0;
    update_refresh_capabilities();
    return update_action_fail(
        output, UPDATE_ACTION_RECOVERY_PENDING, ERR_STATE, message);
}

static int update_prepare_apply(const char* path,
                                update_action_result_t* output,
                                update_journal_t* prepared) {
    update_verification_t verification;
    int result;

    kmemset(&verification, 0, sizeof(verification));
    result = update_verify_internal(path, &verification);
    if (result != OK) {
        output->verification_reason = verification.reason;
        return update_action_fail(
            output, UPDATE_ACTION_VERIFY, result,
            "ZUPD recusado antes da aplicacao");
    }
    update_fill_action_result(
        output, &verification.base_version, verification.base_epoch,
        &verification.target_version, verification.target_epoch,
        verification.entry_count);
    result = update_validate_current_state();
    if (result == OK) result = update_build_apply_journal(prepared);
    if (result != OK) {
        return update_action_fail(
            output,
            result == ERR_OVERFLOW ? UPDATE_ACTION_SPACE :
            UPDATE_ACTION_STATE,
            result, "Preflight U3 falhou");
    }
    return OK;
}

static int update_start_apply_transaction(
    const char* path, const update_journal_t* prepared,
    const update_action_options_t* options,
    update_action_result_t* output) {
    int result = update_stage_apply_files(path, prepared, options);

    if (result != OK) {
        update_cleanup_slot(prepared->slot, 1, 1);
        return update_action_fail(
            output,
            result == ERR_TIMEOUT ? UPDATE_ACTION_CANCELLED :
            UPDATE_ACTION_IO,
            result == ERR_TIMEOUT ? ERR_STATE : result,
            result == ERR_TIMEOUT ?
            "Aplicacao cancelada antes da substituicao" :
            "Falha ao preparar staging ou backup");
    }
    update_journal = *prepared;
    result = update_write_journal_record(&update_journal);
    if (result != OK) {
        uint32_t sequence = update_journal.sequence;

        update_cleanup_slot(prepared->slot, 1, 1);
        kmemset(&update_journal, 0, sizeof(update_journal));
        update_journal.sequence = sequence;
        return update_action_fail(
            output, UPDATE_ACTION_IO, result,
            "Falha ao criar journal de aplicacao");
    }
    return OK;
}

static int update_replace_apply_targets(
    const update_action_options_t* options,
    update_action_result_t* output) {
    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        update_journal_entry_t* entry = &update_journal.entries[index];
        const char* stage =
            update_stage_paths[update_journal.slot][entry->target_id];
        int result;

        if (update_cancelled(options)) {
            return update_apply_recover_failure(
                output, UPDATE_ACTION_CANCELLED, ERR_STATE);
        }
        result = update_replace_from_alias(
            stage, entry->target_id, &entry->new_file);
        if (result != OK) {
            return update_apply_recover_failure(
                output, UPDATE_ACTION_IO, result);
        }
        update_journal.phase = UPDATE_PHASE_REPLACING;
        update_journal.progress = (uint8_t)(index + 1U);
        output->completed_entries = (uint16_t)(index + 1U);
        if (update_write_journal_record(&update_journal) != OK) {
            return update_apply_recover_failure(
                output, UPDATE_ACTION_IO, ERR_DISK);
        }
        if (update_fail_after == index + 1U) {
            update_fail_after = 0;
            LOG_WARN("UPDATE", "Failpoint U3 deixou recuperacao para o boot");
            return update_mark_recovery_pending(
                output, "Failpoint U3 aguarda recuperacao no boot");
        }
    }
    return OK;
}

static int update_commit_transaction(update_action_result_t* output,
                                     const char* pending_message) {
    update_journal.phase = UPDATE_PHASE_COMMITTED;
    if (update_write_journal_record(&update_journal) != OK ||
        update_finalize_committed() != OK) {
        return update_mark_recovery_pending(output, pending_message);
    }
    output->reboot_required = 1;
    update_state_healthy = 1;
    update_refresh_capabilities();
    return OK;
}

int update_apply_file(const char* path,
                      const update_action_options_t* options,
                      update_action_result_t* result_out) {
    update_action_options_t defaults = {0, 0, 0};
    update_journal_t prepared;
    int result;

    if (!path || !result_out) {
        LOG_ERROR("UPDATE", "Argumento nulo na aplicacao ZUPD");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (!update_initialized || fs_get_type() != FS_TYPE_FAT12) {
        return update_action_fail(
            result_out, UPDATE_ACTION_UNSUPPORTED_FS, ERR_UNAVAILABLE,
            "Aplicacao U3 requer FAT12");
    }
    if (!update_state_healthy ||
        update_journal.kind != UPDATE_JOURNAL_NONE) {
        return update_action_fail(
            result_out, UPDATE_ACTION_STATE, ERR_STATE,
            "Estado U3 nao permite nova aplicacao");
    }
    result = update_begin_operation();
    if (result != OK) {
        return update_action_fail(
            result_out, UPDATE_ACTION_STATE, result,
            "Aplicacao concorrente recusada");
    }
    result = update_prepare_apply(path, result_out, &prepared);
    if (result == OK && !options->dry_run &&
        update_fail_after > prepared.entry_count) {
        update_fail_after = 0;
        result = update_action_fail(
            result_out, UPDATE_ACTION_STATE, ERR_INVALID,
            "Failpoint excede a quantidade de alvos do pacote");
    }
    if (result == OK && !options->dry_run) {
        result = update_start_apply_transaction(
            path, &prepared, options, result_out);
    }
    if (result == OK && !options->dry_run) {
        result = update_replace_apply_targets(options, result_out);
    }
    if (result == OK && !options->dry_run) {
        result = update_commit_transaction(
            result_out, "Commit U3 requer finalizacao no proximo boot");
    }
    update_end_operation();
    if (!options->dry_run) {
        update_history_record_action(
            UPDATE_HISTORY_OPERATION_APPLY, path, result, result_out);
    }
    if (result != OK || options->dry_run) return result;
    LOG_INFO("UPDATE", "Atualizacao ZUPD aplicada com sucesso");
    return OK;
}

static int update_check_rollback_space(const update_journal_t* journal) {
    fs_info_t info;
    uint32_t cluster_size;
    uint32_t largest = 0;
    uint32_t required;

    if (!journal || fs_get_info(&info) != OK ||
        info.bytes_per_sector == 0U ||
        info.sectors_per_cluster == 0U) {
        LOG_ERROR("UPDATE", "Geometria indisponivel no rollback");
        return ERR_DISK;
    }
    cluster_size = info.bytes_per_sector * info.sectors_per_cluster;
    for (uint32_t index = 0; index < journal->entry_count; index++) {
        if (journal->entries[index].new_file.size > largest) {
            largest = journal->entries[index].new_file.size;
        }
    }
    required = update_clusters_for(largest, cluster_size) +
               UPDATE_ROLLBACK_METADATA_CLUSTERS +
               update_clusters_for(UPDATE_CONTROL_SIZE, cluster_size);
    if (required > info.free_clusters) {
        LOG_ERROR("UPDATE", "Espaco insuficiente para rollback atomico");
        return ERR_OVERFLOW;
    }
    return OK;
}

static int update_rollback_cancelled(
    uint32_t index, update_action_result_t* output) {
    if (index == 0U) {
        if (update_clear_journal() != OK) {
            return update_mark_recovery_pending(
                output, "Cancelamento do rollback deixou journal pendente");
        }
        return update_action_fail(
            output, UPDATE_ACTION_CANCELLED, ERR_STATE,
            "Rollback cancelado antes da primeira substituicao");
    }
    return update_mark_recovery_pending(
        output, "Rollback cancelado; recuperacao continuara no boot");
}

static int update_build_rollback_journal(update_journal_t* journal) {
    uint32_t sequence;
    uint8_t count = 0;

    if (!journal || !update_state.rollback_available) {
        LOG_ERROR("UPDATE", "Rollback indisponivel para preparar journal");
        return ERR_NOT_FOUND;
    }
    sequence = update_journal.sequence;
    kmemset(journal, 0, sizeof(*journal));
    journal->sequence = sequence;
    journal->kind = UPDATE_JOURNAL_ROLLBACK;
    journal->phase = UPDATE_PHASE_PREPARED;
    journal->slot = update_state.rollback_slot;
    journal->base_state_sequence = update_state.sequence;
    journal->base_version = update_state.installed_version;
    journal->target_version = update_state.previous_version;
    journal->base_epoch = update_state.installed_epoch;
    journal->target_epoch = update_state.previous_epoch;

    for (uint32_t target = 0; target < UPDATE_TARGET_COUNT; target++) {
        update_journal_entry_t* entry;
        const char* backup;

        if (!update_state.rollback[target].present) continue;
        entry = &journal->entries[count++];
        entry->target_id = (uint8_t)target;
        entry->old_file = update_state.current[target];
        entry->new_file = update_state.rollback[target];
        backup = update_backup_paths[journal->slot][target];
        if (update_verify_root_file(backup, &entry->new_file) != OK ||
            update_verify_root_file(
                update_target_paths[target], &entry->old_file) != OK) {
            LOG_ERROR("UPDATE", "Arquivos do rollback nao conferem");
            return ERR_INVALID;
        }
    }
    journal->entry_count = count;
    if (count != update_state.rollback_entry_count || count == 0U) {
        LOG_ERROR("UPDATE", "Quantidade de backups do rollback invalida");
        return ERR_INVALID;
    }
    return update_check_rollback_space(journal);
}

static int update_replace_rollback_targets(
    const update_action_options_t* options,
    update_action_result_t* output) {
    for (uint32_t index = 0; index < update_journal.entry_count; index++) {
        update_journal_entry_t* entry = &update_journal.entries[index];
        const char* backup =
            update_backup_paths[update_journal.slot][entry->target_id];
        int result;

        if (update_cancelled(options)) {
            return update_rollback_cancelled(index, output);
        }
        result = update_replace_from_alias(
            backup, entry->target_id, &entry->new_file);
        if (result != OK) {
            return update_mark_recovery_pending(
                output,
                "Rollback sera retomado no proximo boot");
        }
        update_journal.phase = UPDATE_PHASE_REPLACING;
        update_journal.progress = (uint8_t)(index + 1U);
        output->completed_entries = (uint16_t)(index + 1U);
        if (update_write_journal_record(&update_journal) != OK) {
            return update_mark_recovery_pending(
                output, "Journal do rollback requer recuperacao no boot");
        }
    }
    return OK;
}

int update_rollback(const update_action_options_t* options,
                    update_action_result_t* result_out) {
    update_action_options_t defaults = {0, 0, 0};
    update_journal_t prepared;
    int result;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Destino nulo no rollback");
        return ERR_NULL;
    }
    if (!options) options = &defaults;
    kmemset(result_out, 0, sizeof(*result_out));
    if (!update_initialized || fs_get_type() != FS_TYPE_FAT12) {
        return update_action_fail(
            result_out, UPDATE_ACTION_UNSUPPORTED_FS, ERR_UNAVAILABLE,
            "Rollback U3 requer FAT12");
    }
    if (!update_state_healthy ||
        update_journal.kind != UPDATE_JOURNAL_NONE) {
        return update_action_fail(
            result_out, UPDATE_ACTION_STATE, ERR_STATE,
            "Estado U3 nao permite rollback");
    }
    if (!update_state.rollback_available) {
        result = update_action_fail(
            result_out, UPDATE_ACTION_NO_ROLLBACK, ERR_NOT_FOUND,
            "Nenhum rollback esta disponivel");
        if (!options->dry_run) {
            update_history_record_action(
                UPDATE_HISTORY_OPERATION_ROLLBACK, 0, result, result_out);
        }
        return result;
    }
    result = update_begin_operation();
    if (result != OK) {
        return update_action_fail(
            result_out, UPDATE_ACTION_STATE, result,
            "Rollback concorrente recusado");
    }
    result = update_build_rollback_journal(&prepared);
    if (result != OK) {
        result = update_action_fail(
            result_out,
            result == ERR_OVERFLOW ? UPDATE_ACTION_SPACE :
            UPDATE_ACTION_STATE,
            result, "Backup ou preflight de rollback invalido");
    } else {
        update_fill_action_result(
            result_out, &prepared.base_version, prepared.base_epoch,
            &prepared.target_version, prepared.target_epoch,
            prepared.entry_count);
    }
    if (result == OK && !options->dry_run) {
        update_journal = prepared;
        if (update_write_journal_record(&update_journal) != OK) {
            result = update_action_fail(
                result_out, UPDATE_ACTION_IO, ERR_DISK,
                "Falha ao criar journal de rollback");
        }
    }
    if (result == OK && !options->dry_run) {
        result = update_replace_rollback_targets(options, result_out);
    }
    if (result == OK && !options->dry_run) {
        result = update_commit_transaction(
            result_out, "Rollback requer finalizacao no proximo boot");
    }
    update_end_operation();
    if (!options->dry_run) {
        update_history_record_action(
            UPDATE_HISTORY_OPERATION_ROLLBACK, 0, result, result_out);
    }
    if (result != OK || options->dry_run) return result;
    LOG_INFO("UPDATE", "Rollback U3 concluido com sucesso");
    return OK;
}

int update_test_fail_after(uint16_t completed_entries) {
    if (!update_initialized || fs_get_type() != FS_TYPE_FAT12 ||
        !update_state_healthy) {
        LOG_ERROR("UPDATE", "Failpoint indisponivel");
        return ERR_STATE;
    }
    if (completed_entries == 0U ||
        completed_entries > UPDATE_TARGET_COUNT) {
        LOG_ERROR("UPDATE", "Failpoint fora do limite");
        return ERR_INVALID;
    }
    spinlock_acquire(&update_lock);
    if (update_workspace.busy) {
        spinlock_release(&update_lock);
        LOG_ERROR("UPDATE", "Failpoint recusado durante operacao");
        return ERR_STATE;
    }
    update_fail_after = completed_entries;
    spinlock_release(&update_lock);
    LOG_WARN("UPDATE", "Failpoint U3 armado para a proxima aplicacao");
    return OK;
}

const char* zupd_reason_name(zupd_reason_t reason) {
    switch (reason) {
        case ZUPD_REASON_NONE: return "NONE";
        case ZUPD_REASON_FORMAT: return "FORMAT";
        case ZUPD_REASON_SIZE: return "SIZE";
        case ZUPD_REASON_HASH: return "HASH";
        case ZUPD_REASON_UNKNOWN_KEY: return "UNKNOWN_KEY";
        case ZUPD_REASON_SIGNATURE: return "SIGNATURE";
        case ZUPD_REASON_ARCHITECTURE: return "ARCHITECTURE";
        case ZUPD_REASON_BASE_VERSION: return "BASE_VERSION";
        case ZUPD_REASON_DOWNGRADE: return "DOWNGRADE";
        case ZUPD_REASON_PATH_POLICY: return "PATH_POLICY";
        case ZUPD_REASON_DUPLICATE_TARGET: return "DUPLICATE_TARGET";
        case ZUPD_REASON_UNSUPPORTED: return "UNSUPPORTED";
        default: return "INVALID_REASON";
    }
}

const char* update_action_reason_name(update_action_reason_t reason) {
    switch (reason) {
        case UPDATE_ACTION_NONE: return "NONE";
        case UPDATE_ACTION_VERIFY: return "VERIFY";
        case UPDATE_ACTION_UNSUPPORTED_FS: return "UNSUPPORTED_FS";
        case UPDATE_ACTION_STATE: return "STATE";
        case UPDATE_ACTION_SPACE: return "SPACE";
        case UPDATE_ACTION_IO: return "IO";
        case UPDATE_ACTION_CANCELLED: return "CANCELLED";
        case UPDATE_ACTION_NO_ROLLBACK: return "NO_ROLLBACK";
        case UPDATE_ACTION_RECOVERY_PENDING: return "RECOVERY_PENDING";
        default: return "INVALID_ACTION_REASON";
    }
}

const char* update_store_state_name(update_store_state_t state) {
    switch (state) {
        case UPDATE_STORE_UNAVAILABLE: return "UNAVAILABLE";
        case UPDATE_STORE_EMPTY: return "EMPTY";
        case UPDATE_STORE_VALID: return "VALID";
        case UPDATE_STORE_INVALID: return "INVALID";
        default: return "INVALID_STORE_STATE";
    }
}

const char* update_history_operation_name(
    update_history_operation_t operation) {
    switch (operation) {
        case UPDATE_HISTORY_OPERATION_NONE: return "NONE";
        case UPDATE_HISTORY_OPERATION_APPLY: return "APPLY";
        case UPDATE_HISTORY_OPERATION_ROLLBACK: return "ROLLBACK";
        case UPDATE_HISTORY_OPERATION_RECOVERY_APPLY:
            return "RECOVERY_APPLY";
        case UPDATE_HISTORY_OPERATION_RECOVERY_ROLLBACK:
            return "RECOVERY_ROLLBACK";
        default: return "INVALID_HISTORY_OPERATION";
    }
}

const char* update_history_outcome_name(update_history_outcome_t outcome) {
    switch (outcome) {
        case UPDATE_HISTORY_OUTCOME_NONE: return "NONE";
        case UPDATE_HISTORY_OUTCOME_SUCCESS: return "SUCCESS";
        case UPDATE_HISTORY_OUTCOME_FAILED: return "FAILED";
        case UPDATE_HISTORY_OUTCOME_CANCELLED: return "CANCELLED";
        case UPDATE_HISTORY_OUTCOME_RECOVERED: return "RECOVERED";
        default: return "INVALID_HISTORY_OUTCOME";
    }
}
