#include "core/update_remote_system.h"
#include "core/crypto.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "process/process.h"
#if defined(ZEPHYROS_HOST_TEST)
#include "update_remote_system_host.h"
#endif

#define SYSTEM_CACHE_MAGIC "ZSC1"
#define SYSTEM_CACHE_FORMAT_VERSION 1U
#define SYSTEM_CACHE_ATTRIBUTES \
    (FS_ATTRIBUTE_HIDDEN | FS_ATTRIBUTE_SYSTEM | FS_ATTRIBUTE_ARCHIVE)
#define SYSTEM_CACHE_ACTIVE_OFFSET 12U
#define SYSTEM_CACHE_PACKAGE_SIZE_OFFSET 16U
#define SYSTEM_CACHE_PACKAGE_HASH_OFFSET 20U
#define SYSTEM_CACHE_VERSION_OFFSET 52U
#define SYSTEM_CACHE_EPOCH_OFFSET 60U
#define SYSTEM_CACHE_TAG_OFFSET 64U
#define SYSTEM_CACHE_RELEASE_OFFSET 129U
#define SYSTEM_CACHE_ALIAS_OFFSET 194U
#define SYSTEM_CACHE_IO_SIZE (64U * 1024U)
#define SYSTEM_CACHE_PATH_PREFIX_SIZE 8U
#define SYSTEM_CACHE_PATH_MIN_SIZE \
    (SYSTEM_CACHE_PATH_PREFIX_SIZE + UPDATE_REMOTE_ALIAS_SIZE)

typedef struct {
    uint32_t sequence;
    uint8_t active_slot;
    uint32_t package_size;
    uint8_t package_hash[32];
    update_version_t version;
    uint32_t epoch;
    char tag[UPDATE_REMOTE_TAG_SIZE];
    char release_id[UPDATE_REMOTE_RELEASE_ID_SIZE];
    char alias[UPDATE_REMOTE_ALIAS_SIZE];
} system_cache_record_t;

typedef struct {
    storage_volume_t volume;
    uint8_t target_slot;
    uint8_t writer_active;
    uint32_t received;
} system_cache_transfer_t;

static const char* system_cache_aliases[UPDATE_REMOTE_SYSTEM_CACHE_COUNT] = {
    UPDATE_REMOTE_SYSTEM_CACHE_A_ALIAS, UPDATE_REMOTE_SYSTEM_CACHE_B_ALIAS
};
static const char* system_cache_state_aliases[UPDATE_REMOTE_SYSTEM_CACHE_COUNT] = {
    UPDATE_REMOTE_SYSTEM_STATE_A_ALIAS, UPDATE_REMOTE_SYSTEM_STATE_B_ALIAS
};
static spinlock_t system_cache_lock;
static uint8_t system_cache_initialized;
static uint8_t system_cache_volume_ready;
static uint8_t system_cache_degraded;
static system_cache_record_t system_cache_record;
static update_remote_system_status_t system_cache_status;
static system_cache_transfer_t system_cache_transfer;
static uint8_t system_cache_control[UPDATE_REMOTE_SYSTEM_CONTROL_SIZE];
static uint8_t system_cache_io[SYSTEM_CACHE_IO_SIZE];

static uint16_t system_cache_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t system_cache_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void system_cache_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void system_cache_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static int system_cache_copy_text(char* output, uint32_t capacity,
                                  const char* input) {
    uint32_t length;

    if (!output || !capacity || !input) return ERR_NULL;
    length = kstrlen(input);
    if (length >= capacity) return ERR_OVERFLOW;
    kmemcpy(output, input, length + 1U);
    return OK;
}

static int system_cache_decode_text(const uint8_t* raw, uint32_t size,
                                    char* output, uint32_t capacity) {
    uint32_t length = 0U;

    if (!raw || !size || !output || !capacity) return ERR_NULL;
    while (length < size && raw[length]) length++;
    if (length >= size || length >= capacity) return ERR_INVALID;
    for (uint32_t index = 0U; index < length; index++) {
        if (raw[index] < 0x20U || raw[index] > 0x7EU) return ERR_INVALID;
        output[index] = (char)raw[index];
    }
    output[length] = '\0';
    for (uint32_t index = length + 1U; index < size; index++) {
        if (raw[index]) return ERR_INVALID;
    }
    return OK;
}

static int system_cache_encode(const system_cache_record_t* record,
                               uint8_t* raw) {
    uint8_t hash[32];

    if (!record || !raw) return ERR_NULL;
    kmemset(raw, 0, UPDATE_REMOTE_SYSTEM_CONTROL_SIZE);
    kmemcpy(raw, SYSTEM_CACHE_MAGIC, 4U);
    system_cache_write_u16(raw + 4U, SYSTEM_CACHE_FORMAT_VERSION);
    system_cache_write_u16(raw + 6U, UPDATE_REMOTE_SYSTEM_CONTROL_SIZE);
    system_cache_write_u32(raw + 8U, record->sequence);
    raw[SYSTEM_CACHE_ACTIVE_OFFSET] = record->active_slot;
    system_cache_write_u32(raw + SYSTEM_CACHE_PACKAGE_SIZE_OFFSET,
                           record->package_size);
    kmemcpy(raw + SYSTEM_CACHE_PACKAGE_HASH_OFFSET, record->package_hash, 32U);
    system_cache_write_u16(raw + SYSTEM_CACHE_VERSION_OFFSET,
                           record->version.major);
    system_cache_write_u16(raw + SYSTEM_CACHE_VERSION_OFFSET + 2U,
                           record->version.minor);
    system_cache_write_u16(raw + SYSTEM_CACHE_VERSION_OFFSET + 4U,
                           record->version.patch);
    system_cache_write_u32(raw + SYSTEM_CACHE_EPOCH_OFFSET, record->epoch);
    if (system_cache_copy_text((char*)(raw + SYSTEM_CACHE_TAG_OFFSET),
                               UPDATE_REMOTE_TAG_SIZE, record->tag) != OK ||
        system_cache_copy_text((char*)(raw + SYSTEM_CACHE_RELEASE_OFFSET),
                               UPDATE_REMOTE_RELEASE_ID_SIZE,
                               record->release_id) != OK ||
        system_cache_copy_text((char*)(raw + SYSTEM_CACHE_ALIAS_OFFSET),
                               UPDATE_REMOTE_ALIAS_SIZE,
                               record->alias) != OK) return ERR_OVERFLOW;
    if (crypto_sha256(raw, UPDATE_REMOTE_SYSTEM_CONTROL_HASH_OFFSET,
                      hash) != OK) return ERR_STATE;
    kmemcpy(raw + UPDATE_REMOTE_SYSTEM_CONTROL_HASH_OFFSET, hash, 32U);
    return OK;
}

static int system_cache_decode(const uint8_t* raw,
                               system_cache_record_t* record) {
    uint8_t hash[32];

    if (!raw || !record) return ERR_NULL;
    if (raw[0] != 'Z' || raw[1] != 'S' || raw[2] != 'C' || raw[3] != '1' ||
        system_cache_read_u16(raw + 4U) != SYSTEM_CACHE_FORMAT_VERSION ||
        system_cache_read_u16(raw + 6U) !=
            UPDATE_REMOTE_SYSTEM_CONTROL_SIZE ||
        crypto_sha256(raw, UPDATE_REMOTE_SYSTEM_CONTROL_HASH_OFFSET,
                      hash) != OK ||
        !crypto_equal(hash,
                      raw + UPDATE_REMOTE_SYSTEM_CONTROL_HASH_OFFSET, 32U)) {
        return ERR_INVALID;
    }
    kmemset(record, 0, sizeof(*record));
    record->sequence = system_cache_read_u32(raw + 8U);
    record->active_slot = raw[SYSTEM_CACHE_ACTIVE_OFFSET];
    record->package_size = system_cache_read_u32(
        raw + SYSTEM_CACHE_PACKAGE_SIZE_OFFSET);
    kmemcpy(record->package_hash, raw + SYSTEM_CACHE_PACKAGE_HASH_OFFSET, 32U);
    record->version.major = system_cache_read_u16(
        raw + SYSTEM_CACHE_VERSION_OFFSET);
    record->version.minor = system_cache_read_u16(
        raw + SYSTEM_CACHE_VERSION_OFFSET + 2U);
    record->version.patch = system_cache_read_u16(
        raw + SYSTEM_CACHE_VERSION_OFFSET + 4U);
    record->epoch = system_cache_read_u32(raw + SYSTEM_CACHE_EPOCH_OFFSET);
    if (system_cache_decode_text(raw + SYSTEM_CACHE_TAG_OFFSET,
                                 UPDATE_REMOTE_TAG_SIZE, record->tag,
                                 sizeof(record->tag)) != OK ||
        system_cache_decode_text(raw + SYSTEM_CACHE_RELEASE_OFFSET,
                                 UPDATE_REMOTE_RELEASE_ID_SIZE,
                                 record->release_id,
                                 sizeof(record->release_id)) != OK ||
        system_cache_decode_text(raw + SYSTEM_CACHE_ALIAS_OFFSET,
                                 UPDATE_REMOTE_ALIAS_SIZE, record->alias,
                                 sizeof(record->alias)) != OK) return ERR_INVALID;
    if (record->active_slot == UPDATE_REMOTE_SYSTEM_CACHE_NONE) {
        if (record->package_size || record->alias[0]) return ERR_INVALID;
        return OK;
    }
    if (record->active_slot >= UPDATE_REMOTE_SYSTEM_CACHE_COUNT ||
        record->package_size < UPDATE_SYSTEM_HEADER_SIZE ||
        record->package_size > UPDATE_SYSTEM_HEADER_SIZE +
            UPDATE_SYSTEM_MAX_IMAGE_SIZE ||
        kstrcmp(record->alias,
                system_cache_aliases[record->active_slot]) != 0) {
        return ERR_INVALID;
    }
    return OK;
}

static int system_cache_load_control(const char* alias, uint8_t* raw) {
    uint32_t size = 0U;
    uint32_t bytes = 0U;
    uint8_t attributes = 0U;
    int result;

    result = fs_get_root_file_info(alias, &size, &attributes);
    if (result != OK) return result;
    if (size != UPDATE_REMOTE_SYSTEM_CONTROL_SIZE) return ERR_INVALID;
    result = fs_read_file_range_at(alias, 0U, raw,
                                   UPDATE_REMOTE_SYSTEM_CONTROL_SIZE, &bytes);
    return result == OK && bytes == UPDATE_REMOTE_SYSTEM_CONTROL_SIZE ?
        OK : ERR_DISK;
}

static int system_cache_load_locked(void) {
    system_cache_record_t candidate;
    system_cache_record_t selected;
    uint8_t found = 0U;
    uint8_t valid = 0U;
    uint8_t invalid = 0U;

    kmemset(&selected, 0, sizeof(selected));
    for (uint8_t index = 0U;
         index < UPDATE_REMOTE_SYSTEM_CACHE_COUNT; index++) {
        int result = system_cache_load_control(
            system_cache_state_aliases[index], system_cache_control);
        if (result == ERR_NOT_FOUND) continue;
        if (result != OK ||
            system_cache_decode(system_cache_control, &candidate) != OK) {
            invalid++;
            continue;
        }
        valid++;
        if (!found || candidate.sequence > selected.sequence) {
            selected = candidate;
            found = 1U;
        }
    }
    system_cache_status.copies_valid = valid;
    system_cache_degraded = (!found && invalid) ? 1U : 0U;
    if (!found) {
        kmemset(&system_cache_record, 0, sizeof(system_cache_record));
        system_cache_record.active_slot = UPDATE_REMOTE_SYSTEM_CACHE_NONE;
        return system_cache_degraded ? ERR_INVALID : OK;
    }
    system_cache_record = selected;
    return OK;
}

static int system_cache_hash_file(const char* path, uint32_t size,
                                  uint8_t output[32]) {
    crypto_sha256_ctx_t sha;
    int result = crypto_sha256_init(&sha);

    for (uint32_t offset = 0U; result == OK && offset < size;) {
        uint32_t amount = size - offset;
        uint32_t bytes = 0U;
        if (amount > sizeof(system_cache_io)) amount = sizeof(system_cache_io);
        result = fs_read_file_range_at(path, offset, system_cache_io,
                                       amount, &bytes);
        if (result == OK && bytes != amount) result = ERR_DISK;
        if (result == OK) result = crypto_sha256_update(
            &sha, system_cache_io, bytes);
        offset += result == OK ? bytes : 0U;
        process_block(1U);
    }
    return result == OK ? crypto_sha256_final(&sha, output) : result;
}

static void system_cache_path(uint8_t slot, char* path) {
    kmemcpy(path, "system:/", SYSTEM_CACHE_PATH_PREFIX_SIZE);
    system_cache_copy_text(path + SYSTEM_CACHE_PATH_PREFIX_SIZE,
                           FS_MAX_PATH - SYSTEM_CACHE_PATH_PREFIX_SIZE,
                           system_cache_aliases[slot]);
}

static update_remote_system_reason_t system_cache_map_reason(
    update_system_reason_t reason) {
    switch (reason) {
        case UPDATE_SYSTEM_REASON_SIGNATURE:
        case UPDATE_SYSTEM_REASON_UNKNOWN_KEY:
            return UPDATE_REMOTE_SYSTEM_REASON_SIGNATURE;
        case UPDATE_SYSTEM_REASON_HASH:
            return UPDATE_REMOTE_SYSTEM_REASON_HASH;
        case UPDATE_SYSTEM_REASON_COMPATIBILITY:
        case UPDATE_SYSTEM_REASON_ARCHITECTURE:
            return UPDATE_REMOTE_SYSTEM_REASON_COMPATIBILITY;
        case UPDATE_SYSTEM_REASON_BASE_VERSION:
        case UPDATE_SYSTEM_REASON_DOWNGRADE:
            return UPDATE_REMOTE_SYSTEM_REASON_VERSION;
        case UPDATE_SYSTEM_REASON_SIZE:
            return UPDATE_REMOTE_SYSTEM_REASON_SIZE;
        case UPDATE_SYSTEM_REASON_IO:
            return UPDATE_REMOTE_SYSTEM_REASON_IO;
        default:
            return UPDATE_REMOTE_SYSTEM_REASON_FORMAT;
    }
}

static void system_cache_refresh_status_locked(void) {
    uint8_t copies = system_cache_status.copies_valid;

    kmemset(&system_cache_status, 0, sizeof(system_cache_status));
    system_cache_status.initialized = system_cache_initialized;
    system_cache_status.volume_ready = system_cache_volume_ready;
    system_cache_status.copies_valid = copies;
    system_cache_status.sequence = system_cache_record.sequence;
    system_cache_status.cache_slot = system_cache_record.active_slot;
    system_cache_status.package_size = system_cache_record.package_size;
    system_cache_status.bytes_received = system_cache_record.package_size;
    system_cache_status.total_bytes = system_cache_record.package_size;
    system_cache_status.version = system_cache_record.version;
    system_cache_status.epoch = system_cache_record.epoch;
    kmemcpy(system_cache_status.package_hash,
            system_cache_record.package_hash, 32U);
    system_cache_copy_text(system_cache_status.tag,
                           sizeof(system_cache_status.tag),
                           system_cache_record.tag);
    system_cache_copy_text(system_cache_status.release_id,
                           sizeof(system_cache_status.release_id),
                           system_cache_record.release_id);
    system_cache_copy_text(system_cache_status.cached_alias,
                           sizeof(system_cache_status.cached_alias),
                           system_cache_record.alias);
    if (!system_cache_volume_ready) {
        system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_UNAVAILABLE;
        system_cache_status.reason = UPDATE_REMOTE_SYSTEM_REASON_IO;
    } else if (system_cache_degraded) {
        system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_DEGRADED;
        system_cache_status.reason = UPDATE_REMOTE_SYSTEM_REASON_CACHE;
    } else if (system_cache_record.active_slot ==
               UPDATE_REMOTE_SYSTEM_CACHE_NONE) {
        system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_EMPTY;
    } else {
        system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_READY;
        system_cache_status.package_valid = 1U;
    }
}

static int system_cache_verify_locked(update_system_verification_t* output) {
    char path[FS_MAX_PATH];
    uint8_t hash[32];
    int result;

    if (system_cache_record.active_slot >=
        UPDATE_REMOTE_SYSTEM_CACHE_COUNT) return ERR_NOT_FOUND;
    system_cache_path(system_cache_record.active_slot, path);
    result = update_system_verify_file(path, output);
    if (result == OK) result = system_cache_hash_file(
        path, system_cache_record.package_size, hash);
    if (result == OK && !crypto_equal(
            hash, system_cache_record.package_hash, 32U)) result = ERR_INVALID;
    if (result == OK &&
        (output->target_version.major != system_cache_record.version.major ||
         output->target_version.minor != system_cache_record.version.minor ||
         output->target_version.patch != system_cache_record.version.patch ||
         output->target_epoch != system_cache_record.epoch ||
         kstrcmp(output->release_tag, system_cache_record.tag) != 0 ||
         kstrcmp(output->release_id, system_cache_record.release_id) != 0)) {
        result = ERR_INVALID;
    }
    return result;
}

static int system_cache_write_record_locked(void) {
    system_cache_record_t persisted;
    uint8_t first;
    uint8_t second;
    int result;

    if (system_cache_record.sequence == 0xFFFFFFFFU) return ERR_OVERFLOW;
    system_cache_record.sequence++;
    result = system_cache_encode(&system_cache_record, system_cache_control);
    if (result != OK) return result;
    first = (uint8_t)(system_cache_record.sequence & 1U);
    second = (uint8_t)(first ^ 1U);
    result = fs_atomic_write_root(system_cache_state_aliases[first],
                                  system_cache_control,
                                  UPDATE_REMOTE_SYSTEM_CONTROL_SIZE,
                                  SYSTEM_CACHE_ATTRIBUTES,
                                  FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) result = system_cache_load_control(
        system_cache_state_aliases[first], system_cache_control);
    if (result == OK) result = system_cache_decode(
        system_cache_control, &persisted);
    if (result != OK || persisted.sequence != system_cache_record.sequence) {
        LOG_ERROR("UPDATE", "Controle do cache ZSYS nao confirmou gravacao");
        return ERR_DISK;
    }
    result = system_cache_encode(&system_cache_record, system_cache_control);
    if (result == OK) result = fs_atomic_write_root(
        system_cache_state_aliases[second], system_cache_control,
        UPDATE_REMOTE_SYSTEM_CONTROL_SIZE, SYSTEM_CACHE_ATTRIBUTES,
        FS_ATOMIC_CREATE_OR_REPLACE);
    if (result == OK) result = system_cache_load_control(
        system_cache_state_aliases[second], system_cache_control);
    if (result == OK) result = system_cache_decode(
        system_cache_control, &persisted);
    if (result == OK &&
        persisted.sequence != system_cache_record.sequence) {
        result = ERR_INVALID;
    }
    if (result != OK) {
        system_cache_degraded = 1U;
        LOG_ERROR("UPDATE", "Redundancia do cache ZSYS nao foi atualizada");
        return result;
    }
    system_cache_degraded = 0U;
    system_cache_status.copies_valid = UPDATE_REMOTE_SYSTEM_CACHE_COUNT;
    return OK;
}

static int system_cache_transfer_begin(
    const update_system_remote_asset_t* asset, void* context) {
    system_cache_transfer_t* transfer = (system_cache_transfer_t*)context;
    fs_info_t info;
    uint32_t free_bytes;
    int result;

    if (!asset || !transfer) return ERR_NULL;
    if (fs_get_info(&info) != OK || info.type != FS_TYPE_FAT32 ||
        !info.bytes_per_sector) return ERR_UNAVAILABLE;
    free_bytes = info.free_sectors > 0xFFFFFFFFU / info.bytes_per_sector ?
        0xFFFFFFFFU : info.free_sectors * info.bytes_per_sector;
    if (free_bytes < asset->package_size +
        info.bytes_per_sector * info.sectors_per_cluster * 2U) {
        return ERR_OVERFLOW;
    }
    result = storage_delete_file(transfer->volume.id,
                                 UPDATE_REMOTE_SYSTEM_TEMP_ALIAS);
    if (result != OK && result != ERR_NOT_FOUND) return result;
    result = storage_delete_file(transfer->volume.id,
                                 system_cache_aliases[transfer->target_slot]);
    if (result != OK && result != ERR_NOT_FOUND) return result;
    result = storage_transaction_writer_begin(
        transfer->volume.id, system_cache_aliases[transfer->target_slot],
        UPDATE_REMOTE_SYSTEM_TEMP_ALIAS, asset->package_size,
        SYSTEM_CACHE_ATTRIBUTES);
    if (result == OK) {
        transfer->writer_active = 1U;
        spinlock_acquire(&system_cache_lock);
        system_cache_status.total_bytes = asset->package_size;
        system_cache_status.bytes_received = 0U;
        spinlock_release(&system_cache_lock);
    }
    return result;
}

static int system_cache_transfer_write(const uint8_t* data, uint32_t size,
                                       void* context) {
    system_cache_transfer_t* transfer = (system_cache_transfer_t*)context;
    int result;

    if (!transfer) return ERR_NULL;
    result = storage_transaction_writer_write(data, size);
    if (result == OK) {
        transfer->received += size;
        spinlock_acquire(&system_cache_lock);
        system_cache_status.bytes_received = transfer->received;
        spinlock_release(&system_cache_lock);
    }
    return result;
}

static int system_cache_transfer_finish(void* context) {
    system_cache_transfer_t* transfer = (system_cache_transfer_t*)context;
    int result;

    if (!transfer) return ERR_NULL;
    result = storage_transaction_writer_finish();
    transfer->writer_active = 0U;
    return result;
}

static void system_cache_transfer_abort(void* context) {
    system_cache_transfer_t* transfer = (system_cache_transfer_t*)context;

    if (!transfer || !transfer->writer_active) return;
    storage_transaction_writer_abort();
    transfer->writer_active = 0U;
}

int update_remote_system_init(void) {
    storage_volume_t volume;
    update_system_verification_t verification;
    int result;

    LOG_INFO("UPDATE", "Inicializando cache remoto ZSYS");
    spinlock_acquire(&system_cache_lock);
    kmemset(&system_cache_record, 0, sizeof(system_cache_record));
    kmemset(&system_cache_status, 0, sizeof(system_cache_status));
    system_cache_record.active_slot = UPDATE_REMOTE_SYSTEM_CACHE_NONE;
    system_cache_initialized = 1U;
    system_cache_volume_ready = 0U;
    system_cache_degraded = 0U;
    if (storage_find_system_volume(&volume) != OK ||
        fs_get_type() != FS_TYPE_FAT32) {
        system_cache_refresh_status_locked();
        spinlock_release(&system_cache_lock);
        LOG_ERROR("UPDATE", "Cache remoto ZSYS indisponivel sem FAT32");
        return ERR_UNAVAILABLE;
    }
    system_cache_volume_ready = 1U;
    result = system_cache_load_locked();
    if (result == OK && system_cache_record.active_slot !=
                        UPDATE_REMOTE_SYSTEM_CACHE_NONE &&
        system_cache_verify_locked(&verification) != OK) {
        system_cache_degraded = 1U;
        result = ERR_INVALID;
    }
    {
        int cleanup = storage_delete_file(volume.id,
                                          UPDATE_REMOTE_SYSTEM_TEMP_ALIAS);
        if (cleanup != OK && cleanup != ERR_NOT_FOUND) {
            system_cache_degraded = 1U;
            result = cleanup;
        }
    }
    system_cache_refresh_status_locked();
    spinlock_release(&system_cache_lock);
    if (result != OK) {
        LOG_ERROR("UPDATE", "Cache remoto ZSYS inicializado degradado");
        return result;
    }
    LOG_INFO("UPDATE", "Cache remoto ZSYS inicializado com sucesso");
    return OK;
}

int update_remote_system_fetch(
    const char* tag, const update_remote_options_t* options,
    update_remote_system_result_t* result_out) {
    update_system_transfer_t transfer;
    update_system_remote_asset_t asset;
    update_system_verification_t verification;
    system_cache_record_t next;
    char path[FS_MAX_PATH];
    uint8_t hash[32];
    int result;

    if (!tag || !result_out) {
        LOG_ERROR("UPDATE", "Argumento nulo no fetch remoto ZSYS");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    if (!system_cache_initialized || !system_cache_volume_ready) {
        result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_STATE;
        LOG_ERROR("UPDATE", "Fetch remoto ZSYS sem cache pronto");
        return ERR_UNAVAILABLE;
    }
    if (!options || options->dry_run) {
        result = update_system_check_tag(tag, options, &verification);
        result_out->verification = verification;
        result_out->reason = result == OK ? UPDATE_REMOTE_SYSTEM_REASON_NONE :
            system_cache_map_reason(verification.reason);
        result_out->cache_preserved = 1U;
        return result;
    }
    spinlock_acquire(&system_cache_lock);
    if (system_cache_status.busy) {
        spinlock_release(&system_cache_lock);
        result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_STATE;
        LOG_ERROR("UPDATE", "Cache remoto ZSYS ja esta ocupado");
        return ERR_STATE;
    }
    system_cache_status.busy = 1U;
    system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_DOWNLOADING;
    system_cache_transfer.target_slot =
        system_cache_record.active_slot == UPDATE_REMOTE_SYSTEM_CACHE_NONE ?
        0U : (uint8_t)(system_cache_record.active_slot ^ 1U);
    if (storage_find_system_volume(&system_cache_transfer.volume) != OK) {
        system_cache_status.busy = 0U;
        spinlock_release(&system_cache_lock);
        result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_IO;
        return ERR_UNAVAILABLE;
    }
    system_cache_transfer.writer_active = 0U;
    system_cache_transfer.received = 0U;
    spinlock_release(&system_cache_lock);
    kmemset(&transfer, 0, sizeof(transfer));
    transfer.begin = system_cache_transfer_begin;
    transfer.write = system_cache_transfer_write;
    transfer.finish = system_cache_transfer_finish;
    transfer.abort = system_cache_transfer_abort;
    transfer.context = &system_cache_transfer;
    result = update_system_transfer_tag(tag, options, &transfer,
                                        &verification, &asset);
    result_out->verification = verification;
    result_out->bytes_received = system_cache_transfer.received;
    if (result != OK) {
        result_out->reason = result == ERR_TIMEOUT ?
            UPDATE_REMOTE_SYSTEM_REASON_CANCELLED :
            result == ERR_OVERFLOW ? UPDATE_REMOTE_SYSTEM_REASON_SPACE :
            system_cache_map_reason(verification.reason);
        result_out->cache_preserved = 1U;
        spinlock_acquire(&system_cache_lock);
        system_cache_status.busy = 0U;
        system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_FAILED;
        system_cache_status.reason = result_out->reason;
        system_cache_status.bytes_received = system_cache_transfer.received;
        spinlock_release(&system_cache_lock);
        LOG_ERROR_CODE("UPDATE", result, "Fetch remoto ZSYS falhou");
        return result;
    }
    system_cache_path(system_cache_transfer.target_slot, path);
    spinlock_acquire(&system_cache_lock);
    system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_VALIDATING;
    spinlock_release(&system_cache_lock);
    result = update_system_verify_file(path, &verification);
    if (result == OK) result = system_cache_hash_file(path,
                                                      asset.package_size, hash);
    if (result == OK && !crypto_equal(hash, asset.package_hash, 32U)) {
        result = ERR_INVALID;
        verification.reason = UPDATE_SYSTEM_REASON_HASH;
    }
    if (result != OK) {
        storage_delete_file(system_cache_transfer.volume.id,
                            system_cache_aliases[system_cache_transfer.target_slot]);
        result_out->verification = verification;
        result_out->reason = system_cache_map_reason(verification.reason);
        result_out->cache_preserved = 1U;
        spinlock_acquire(&system_cache_lock);
        system_cache_status.busy = 0U;
        system_cache_status.state = UPDATE_REMOTE_SYSTEM_STATE_FAILED;
        system_cache_status.reason = result_out->reason;
        spinlock_release(&system_cache_lock);
        LOG_ERROR("UPDATE", "Cache ZSYS falhou na verificacao local final");
        return result;
    }
    kmemset(&next, 0, sizeof(next));
    next.sequence = system_cache_record.sequence;
    next.active_slot = system_cache_transfer.target_slot;
    next.package_size = asset.package_size;
    kmemcpy(next.package_hash, asset.package_hash, 32U);
    next.version = verification.target_version;
    next.epoch = verification.target_epoch;
    system_cache_copy_text(next.tag, sizeof(next.tag), verification.release_tag);
    system_cache_copy_text(next.release_id, sizeof(next.release_id),
                           verification.release_id);
    system_cache_copy_text(next.alias, sizeof(next.alias),
                           system_cache_aliases[next.active_slot]);
    spinlock_acquire(&system_cache_lock);
    system_cache_record = next;
    result = system_cache_write_record_locked();
    if (result != OK) system_cache_load_locked();
    system_cache_status.busy = 0U;
    system_cache_refresh_status_locked();
    spinlock_release(&system_cache_lock);
    result_out->verification = verification;
    result_out->reason = result == OK ? UPDATE_REMOTE_SYSTEM_REASON_NONE :
        UPDATE_REMOTE_SYSTEM_REASON_CACHE;
    result_out->cache_published = result == OK ? 1U : 0U;
    result_out->cache_preserved = result == OK ? 0U : 1U;
    system_cache_copy_text(result_out->cached_alias,
                           sizeof(result_out->cached_alias), next.alias);
    if (result != OK) return result;
    LOG_INFO("UPDATE", "Cache remoto ZSYS publicado com sucesso");
    return OK;
}

int update_remote_system_clear(
    const update_remote_options_t* options,
    update_remote_system_result_t* result_out) {
    storage_volume_t volume;
    system_cache_record_t cleared;
    int result = OK;

    if (!result_out) {
        LOG_ERROR("UPDATE", "Resultado nulo ao limpar cache ZSYS");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    if (!system_cache_initialized || !system_cache_volume_ready) {
        result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_STATE;
        return ERR_UNAVAILABLE;
    }
    if (!options || options->dry_run) {
        result_out->cache_preserved = 1U;
        return OK;
    }
    spinlock_acquire(&system_cache_lock);
    if (system_cache_status.busy) {
        spinlock_release(&system_cache_lock);
        result_out->reason = UPDATE_REMOTE_SYSTEM_REASON_STATE;
        return ERR_STATE;
    }
    kmemset(&cleared, 0, sizeof(cleared));
    cleared.sequence = system_cache_record.sequence;
    cleared.active_slot = UPDATE_REMOTE_SYSTEM_CACHE_NONE;
    system_cache_record = cleared;
    result = system_cache_write_record_locked();
    if (result != OK) system_cache_load_locked();
    if (result == OK) {
        result = storage_find_system_volume(&volume);
        if (result == OK) {
            for (uint8_t index = 0U;
                 index < UPDATE_REMOTE_SYSTEM_CACHE_COUNT; index++) {
                int cleanup = storage_delete_file(
                    volume.id, system_cache_aliases[index]);
                if (cleanup != OK && cleanup != ERR_NOT_FOUND &&
                    result == OK) {
                    result = cleanup;
                }
            }
            {
                int cleanup = storage_delete_file(
                    volume.id, UPDATE_REMOTE_SYSTEM_TEMP_ALIAS);
                if (cleanup != OK && cleanup != ERR_NOT_FOUND &&
                    result == OK) {
                    result = cleanup;
                }
            }
        }
    }
    system_cache_refresh_status_locked();
    spinlock_release(&system_cache_lock);
    result_out->cache_cleared =
        system_cache_record.active_slot == UPDATE_REMOTE_SYSTEM_CACHE_NONE ?
        1U : 0U;
    result_out->reason = result == OK ? UPDATE_REMOTE_SYSTEM_REASON_NONE :
        UPDATE_REMOTE_SYSTEM_REASON_IO;
    if (result != OK) {
        LOG_ERROR("UPDATE", "Cache ZSYS despublicado com limpeza incompleta");
        return result;
    }
    LOG_INFO("UPDATE", "Cache remoto ZSYS removido");
    return OK;
}

int update_remote_system_get_status(
    update_remote_system_status_t* status_out) {
    if (!status_out) {
        LOG_ERROR("UPDATE", "Destino nulo no status do cache ZSYS");
        return ERR_NULL;
    }
    spinlock_acquire(&system_cache_lock);
    *status_out = system_cache_status;
    spinlock_release(&system_cache_lock);
    return system_cache_initialized ? OK : ERR_UNAVAILABLE;
}

int update_remote_system_get_cached_path(char* path_out, uint32_t capacity) {
    int result = OK;

    if (!path_out || capacity < SYSTEM_CACHE_PATH_MIN_SIZE) {
        LOG_ERROR("UPDATE", "Destino invalido para caminho do cache ZSYS");
        return ERR_NULL;
    }
    spinlock_acquire(&system_cache_lock);
    if (!system_cache_initialized || !system_cache_volume_ready ||
        system_cache_record.active_slot >=
            UPDATE_REMOTE_SYSTEM_CACHE_COUNT || system_cache_degraded) {
        result = ERR_NOT_FOUND;
    } else {
        system_cache_path(system_cache_record.active_slot, path_out);
    }
    spinlock_release(&system_cache_lock);
    return result;
}

const char* update_remote_system_state_name(update_remote_system_state_t state) {
    switch (state) {
        case UPDATE_REMOTE_SYSTEM_STATE_UNAVAILABLE: return "UNAVAILABLE";
        case UPDATE_REMOTE_SYSTEM_STATE_EMPTY: return "EMPTY";
        case UPDATE_REMOTE_SYSTEM_STATE_READY: return "READY";
        case UPDATE_REMOTE_SYSTEM_STATE_DEGRADED: return "DEGRADED";
        case UPDATE_REMOTE_SYSTEM_STATE_DOWNLOADING: return "DOWNLOADING";
        case UPDATE_REMOTE_SYSTEM_STATE_VALIDATING: return "VALIDATING";
        case UPDATE_REMOTE_SYSTEM_STATE_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char* update_remote_system_reason_name(update_remote_system_reason_t reason) {
    switch (reason) {
        case UPDATE_REMOTE_SYSTEM_REASON_NONE: return "NONE";
        case UPDATE_REMOTE_SYSTEM_REASON_NETWORK: return "NETWORK";
        case UPDATE_REMOTE_SYSTEM_REASON_RELEASE: return "RELEASE";
        case UPDATE_REMOTE_SYSTEM_REASON_FORMAT: return "FORMAT";
        case UPDATE_REMOTE_SYSTEM_REASON_SIGNATURE: return "SIGNATURE";
        case UPDATE_REMOTE_SYSTEM_REASON_HASH: return "HASH";
        case UPDATE_REMOTE_SYSTEM_REASON_COMPATIBILITY: return "COMPATIBILITY";
        case UPDATE_REMOTE_SYSTEM_REASON_VERSION: return "VERSION";
        case UPDATE_REMOTE_SYSTEM_REASON_SIZE: return "SIZE";
        case UPDATE_REMOTE_SYSTEM_REASON_SPACE: return "SPACE";
        case UPDATE_REMOTE_SYSTEM_REASON_CACHE: return "CACHE";
        case UPDATE_REMOTE_SYSTEM_REASON_IO: return "IO";
        case UPDATE_REMOTE_SYSTEM_REASON_CANCELLED: return "CANCELLED";
        case UPDATE_REMOTE_SYSTEM_REASON_STATE: return "STATE";
        default: return "UNKNOWN";
    }
}

#if defined(ZEPHYROS_HOST_TEST)
#define SYSTEM_CACHE_HOST_PACKAGE_SIZE (UPDATE_SYSTEM_HEADER_SIZE + 1024U)
static uint8_t system_cache_host_package_data[SYSTEM_CACHE_HOST_PACKAGE_SIZE];

int update_remote_system_host_test_contracts(void) {
    uint8_t raw[UPDATE_REMOTE_SYSTEM_CONTROL_SIZE];
    char text[UPDATE_REMOTE_TAG_SIZE];
    char path[FS_MAX_PATH];
    system_cache_record_t record;
    system_cache_record_t decoded;
    system_cache_transfer_t transfer;
    update_system_remote_asset_t asset;
    update_system_verification_t verification;
    update_remote_options_t options;
    update_remote_system_result_t result_out;
    update_remote_system_status_t status;
    uint8_t hash[32];

    kmemset(&options, 0, sizeof(options));
    if (update_remote_system_init() != OK) return 90;
    kmemset(raw, 0, sizeof(raw));
    if (system_cache_read_u16(raw) != 0U ||
        system_cache_read_u32(raw) != 0U) return 101;
    system_cache_write_u16(raw, 0x1234U);
    system_cache_write_u32(raw + 2U, 0x89ABCDEFU);
    if (system_cache_read_u16(raw) != 0x1234U ||
        system_cache_read_u32(raw + 2U) != 0x89ABCDEFU) return 102;
    if (system_cache_copy_text(text, sizeof(text), "stable") != OK ||
        kstrcmp(text, "stable") != 0 ||
        system_cache_copy_text(text, 4U, "stable") != ERR_OVERFLOW ||
        system_cache_copy_text(NULL, sizeof(text), "stable") != ERR_NULL) {
        return 103;
    }
    kmemset(raw, 0, sizeof(raw));
    raw[0] = 'S'; raw[1] = 'T'; raw[2] = 'A'; raw[3] = 'B';
    if (system_cache_decode_text(raw, 8U, text, sizeof(text)) != OK ||
        kstrcmp(text, "STAB") != 0 ||
        system_cache_decode_text(NULL, 8U, text, sizeof(text)) != ERR_NULL) {
        return 104;
    }
    kmemset(&record, 0, sizeof(record));
    record.sequence = 3U;
    record.active_slot = 0U;
    record.package_size = SYSTEM_CACHE_HOST_PACKAGE_SIZE;
    record.version.major = 2U;
    record.version.minor = 1U;
    record.version.patch = 0U;
    record.epoch = 9U;
    kmemcpy(record.tag, "stable", 7U);
    kmemcpy(record.release_id, "R1", 3U);
    kmemcpy(record.alias, UPDATE_REMOTE_SYSTEM_CACHE_A_ALIAS, 9U);
    if (system_cache_hash_file("system:/cache.zsys", record.package_size,
                               record.package_hash) != OK ||
        system_cache_encode(&record, raw) != OK ||
        system_cache_decode(raw, &decoded) != OK ||
        decoded.sequence != record.sequence ||
        decoded.active_slot != record.active_slot ||
        kstrcmp(decoded.tag, record.tag) != 0) return 105;
    record.active_slot = UPDATE_REMOTE_SYSTEM_CACHE_NONE;
    record.package_size = 0U;
    record.alias[0] = '\0';
    if (system_cache_encode(&record, raw) != OK ||
        system_cache_decode(raw, &decoded) != OK) return 106;
    raw[0] = 'X';
    if (system_cache_decode(raw, &decoded) != ERR_INVALID) return 107;
    record.active_slot = 0U;
    record.package_size = SYSTEM_CACHE_HOST_PACKAGE_SIZE;
    kmemcpy(record.alias, UPDATE_REMOTE_SYSTEM_CACHE_A_ALIAS, 9U);
    if (system_cache_load_control(UPDATE_REMOTE_SYSTEM_STATE_A_ALIAS, raw) !=
            ERR_NOT_FOUND ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_SIGNATURE) !=
            UPDATE_REMOTE_SYSTEM_REASON_SIGNATURE ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_HASH) !=
            UPDATE_REMOTE_SYSTEM_REASON_HASH ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_COMPATIBILITY) !=
            UPDATE_REMOTE_SYSTEM_REASON_COMPATIBILITY ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_BASE_VERSION) !=
            UPDATE_REMOTE_SYSTEM_REASON_VERSION ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_SIZE) !=
            UPDATE_REMOTE_SYSTEM_REASON_SIZE ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_IO) !=
            UPDATE_REMOTE_SYSTEM_REASON_IO ||
        system_cache_map_reason(UPDATE_SYSTEM_REASON_FORMAT) !=
            UPDATE_REMOTE_SYSTEM_REASON_FORMAT) return 108;
    system_cache_path(0U, path);
    if (kstrcmp(path, "system:/ZSC0.ZSY") != 0 ||
        system_cache_hash_file("system:/cache.zsys", 0U, hash) != OK) {
        return 109;
    }
    kmemset(&asset, 0, sizeof(asset));
    asset.package_size = SYSTEM_CACHE_HOST_PACKAGE_SIZE;
    kmemcpy(asset.package_hash, record.package_hash, sizeof(hash));
    kmemset(&transfer, 0, sizeof(transfer));
    kmemcpy(transfer.volume.id, "SYSVOL", 7U);
    transfer.target_slot = 0U;
    if (system_cache_transfer_begin(&asset, &transfer) != OK ||
        !transfer.writer_active ||
        system_cache_transfer_write(system_cache_host_package_data, 16U,
                                    &transfer) != OK ||
        transfer.received != 16U) return 110;
    system_cache_transfer_abort(&transfer);
    if (transfer.writer_active) return 111;
    kmemset(&transfer, 0, sizeof(transfer));
    kmemcpy(transfer.volume.id, "SYSVOL", 7U);
    transfer.target_slot = 0U;
    if (system_cache_transfer_begin(&asset, &transfer) != OK ||
        system_cache_transfer_finish(&transfer) != OK ||
        transfer.writer_active) return 112;
    if (system_cache_transfer_write(NULL, 0U, NULL) != ERR_NULL ||
        system_cache_transfer_finish(NULL) != ERR_NULL) return 113;
    system_cache_record = record;
    system_cache_record.sequence = 0U;
    if (system_cache_write_record_locked() != OK ||
        system_cache_load_locked() != OK) return 114;
    system_cache_volume_ready = 1U;
    system_cache_degraded = 0U;
    system_cache_refresh_status_locked();
    if (system_cache_verify_locked(&verification) != OK ||
        system_cache_status.state != UPDATE_REMOTE_SYSTEM_STATE_READY) {
        return 115;
    }
    system_cache_record.active_slot = UPDATE_REMOTE_SYSTEM_CACHE_NONE;
    system_cache_refresh_status_locked();
    options.dry_run = 1U;
    if (update_remote_system_fetch(NULL, &options, &result_out) != ERR_NULL ||
        update_remote_system_fetch("stable", NULL, &result_out) != ERR_INVALID ||
        update_remote_system_clear(&options, &result_out) != OK ||
        !result_out.cache_preserved ||
        update_remote_system_get_status(NULL) != ERR_NULL ||
        update_remote_system_get_status(&status) != OK ||
        update_remote_system_get_cached_path(NULL, sizeof(path)) != ERR_NULL ||
        update_remote_system_get_cached_path(path, 1U) != ERR_NULL) {
        return 116;
    }
    options.dry_run = 0U;
    if (update_remote_system_clear(&options, &result_out) != OK ||
        !result_out.cache_cleared ||
        update_remote_system_get_cached_path(path, sizeof(path)) != ERR_NOT_FOUND) {
        return 117;
    }
    if (update_remote_system_state_name(UPDATE_REMOTE_SYSTEM_STATE_READY) == NULL ||
        update_remote_system_reason_name(UPDATE_REMOTE_SYSTEM_REASON_CACHE) == NULL) {
        return 118;
    }
    return OK;
}
#endif
