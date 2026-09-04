#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update_system.h"
#include "core/update_system_slots.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "update_system_slots_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FILE_CAPACITY 16U
#define HOST_FILE_SIZE 4096U
#define HOST_CANDIDATE_SIZE (UPDATE_SYSTEM_HEADER_SIZE + 1024U)

typedef struct {
    uint8_t used;
    uint32_t size;
    uint8_t data[HOST_FILE_SIZE];
    char name[FS_MAX_PATH];
} host_file_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_file_t host_files[HOST_FILE_CAPACITY];
static uint8_t host_candidate[HOST_CANDIDATE_SIZE];

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:update-system-slots|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-system-slots|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-system-slots|value=0x%08X\n",
           (uint32_t)result);
}

static host_file_t* host_file_find(const char* name, uint8_t create) {
    host_file_t* free_file = NULL;

    if (!name) return NULL;
    for (uint32_t index = 0U; index < HOST_FILE_CAPACITY; index++) {
        if (host_files[index].used && kstrcmp(host_files[index].name, name) == 0) {
            return &host_files[index];
        }
        if (!host_files[index].used && !free_file) free_file = &host_files[index];
    }
    if (!create || !free_file) return NULL;
    free_file->used = 1U;
    kmemcpy(free_file->name, name, kstrlen(name) + 1U);
    return free_file;
}

static int host_is_candidate(const char* path) {
    return path && (kstrcmp(path, "system:/candidate.zsy") == 0 ||
                    kstrcmp(path, "system:/ZSA0.ZSY") == 0 ||
                    kstrcmp(path, "system:/ZSB0.ZSY") == 0);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int crypto_sha256_init(crypto_sha256_ctx_t* context) {
    if (!context) return ERR_NULL;
    for (uint32_t index = 0U; index < 8U; index++) {
        context->state[index] = 0x9E3779B9U + index * 0x10001U;
    }
    context->total_size = 0U;
    context->buffer_size = 0U;
    return OK;
}

int crypto_sha256_update(crypto_sha256_ctx_t* context, const uint8_t* data,
                         uint32_t size) {
    if (!context || (size && !data)) return ERR_NULL;
    for (uint32_t index = 0U; index < size; index++) {
        uint32_t slot = (uint32_t)((context->total_size + index) & 7U);
        context->state[slot] = (context->state[slot] << 5U) ^
                               (context->state[slot] >> 2U) ^ data[index];
    }
    context->total_size += size;
    return OK;
}

int crypto_sha256_final(crypto_sha256_ctx_t* context,
                        uint8_t hash[CRYPTO_SHA256_SIZE]) {
    if (!context || !hash) return ERR_NULL;
    for (uint32_t index = 0U; index < CRYPTO_SHA256_SIZE; index++) {
        uint32_t word = context->state[index / 4U] +
                        (uint32_t)context->total_size + index * 0x45D9F3BU;
        hash[index] = (uint8_t)(word >> ((index & 3U) * 8U));
    }
    return OK;
}

int crypto_sha256(const uint8_t* data, uint32_t size,
                  uint8_t hash[CRYPTO_SHA256_SIZE]) {
    crypto_sha256_ctx_t context;
    int result = crypto_sha256_init(&context);

    if (result != OK) return result;
    result = crypto_sha256_update(&context, data, size);
    if (result != OK) return result;
    return crypto_sha256_final(&context, hash);
}

int crypto_equal(const uint8_t* first, const uint8_t* second, uint32_t size) {
    uint8_t different = 0U;

    if (!first || !second) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        different |= (uint8_t)(first[index] ^ second[index]);
    }
    return different == 0U;
}

int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out) {
    host_file_t* file;

    if (!filename) return ERR_NULL;
    if (host_is_candidate(filename)) {
        if (size_out) *size_out = HOST_CANDIDATE_SIZE;
        if (attributes_out) *attributes_out = 0U;
        return OK;
    }
    file = host_file_find(filename, 0U);
    if (!file && (kstrcmp(filename, UPDATE_SYSTEM_SLOT_STATE_A_ALIAS) == 0 ||
                  kstrcmp(filename, UPDATE_SYSTEM_SLOT_STATE_B_ALIAS) == 0)) {
        if (size_out) *size_out = UPDATE_SYSTEM_SLOT_CONTROL_SIZE;
        if (attributes_out) *attributes_out = 0U;
        return OK;
    }
    if (!file) return ERR_NOT_FOUND;
    if (size_out) *size_out = file->size;
    if (attributes_out) *attributes_out = 0U;
    return OK;
}

int fs_read_file_range_at(const char* path, uint32_t offset, uint8_t* buffer,
                          uint32_t max_size, uint32_t* bytes_read) {
    host_file_t* file;

    if (!path || !buffer || !bytes_read) return ERR_NULL;
    *bytes_read = 0U;
    if (host_is_candidate(path)) {
        if (offset > HOST_CANDIDATE_SIZE ||
            max_size > HOST_CANDIDATE_SIZE - offset) return ERR_OVERFLOW;
        for (uint32_t index = 0U; index < max_size; index++) {
            buffer[index] = host_candidate[offset + index];
        }
        *bytes_read = max_size;
        return OK;
    }
    file = host_file_find(path, 0U);
    if (!file) return ERR_NOT_FOUND;
    if (offset > file->size || max_size > file->size - offset) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < max_size; index++) {
        buffer[index] = file->data[offset + index];
    }
    *bytes_read = max_size;
    return OK;
}

int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode) {
    host_file_t* file;

    (void)attributes;
    (void)mode;
    if (!filename || !data) return ERR_NULL;
    if (size > HOST_FILE_SIZE) return ERR_OVERFLOW;
    file = host_file_find(filename, 1U);
    if (!file) return ERR_MEM;
    for (uint32_t index = 0U; index < size; index++) file->data[index] = data[index];
    file->size = size;
    return OK;
}

int fs_atomic_delete_root(const char* filename) {
    host_file_t* file = host_file_find(filename, 0U);

    if (!filename) return ERR_NULL;
    if (!file) return ERR_NOT_FOUND;
    file->used = 0U;
    return OK;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return ERR_NULL;
    info->type = FS_TYPE_FAT32;
    info->bytes_per_sector = 512U;
    info->sectors_per_cluster = 1U;
    info->free_sectors = 100000U;
    return OK;
}

int fs_has_system_volume(void) {
    return 1;
}

int storage_find_system_volume(storage_volume_t* out_volume) {
    if (!out_volume) return ERR_NULL;
    kmemset(out_volume, 0, sizeof(*out_volume));
    kmemcpy(out_volume->id, "SYSVOL", 7U);
    out_volume->fs_type = STORAGE_FS_FAT32;
    out_volume->state = STORAGE_VOLUME_MOUNTED;
    out_volume->mounted = 1U;
    out_volume->role = STORAGE_VOLUME_ROLE_SYSTEM;
    return OK;
}

int storage_delete_file(const char* id, const char* path) {
    (void)id;
    (void)path;
    return ERR_NOT_FOUND;
}

int storage_slot_writer_begin(const char* id, const char* path,
                              uint32_t expected_size, uint8_t attributes) {
    (void)id;
    (void)path;
    (void)expected_size;
    (void)attributes;
    return OK;
}

int storage_slot_writer_write(const uint8_t* data, uint32_t size) {
    (void)data;
    (void)size;
    return OK;
}

int storage_slot_writer_finish(void) {
    return OK;
}

int storage_slot_writer_abort(void) {
    return OK;
}

int storage_slot_writer_is_active(void) {
    return 0;
}

int update_system_is_ready(void) {
    return OK;
}

static void host_fill_verification(update_system_verification_t* result_out) {
    if (!result_out) return;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_SYSTEM_REASON_NONE;
    result_out->image_size = 1024U;
    result_out->target_version.major = 1U;
    result_out->target_version.minor = 1U;
    result_out->target_version.patch = 0U;
    result_out->target_epoch = 7U;
    result_out->release_id[0] = 'R';
    result_out->release_tag[0] = 'T';
}

int update_system_verify_file(const char* path,
                              update_system_verification_t* result_out) {
    if (!path || !result_out) return ERR_NULL;
    host_fill_verification(result_out);
    return OK;
}

int update_system_verify_file_for_slot(
    const char* path, update_system_verification_t* result_out) {
    return update_system_verify_file(path, result_out);
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int main(void) {
    for (uint32_t index = 0U; index < HOST_CANDIDATE_SIZE; index++) {
        host_candidate[index] = (uint8_t)(index * 13U + 5U);
    }
    coverage_active = 1U;
    int result = update_system_slots_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) return result;
    puts("update-system-slots-host: PASS");
    return OK;
}
