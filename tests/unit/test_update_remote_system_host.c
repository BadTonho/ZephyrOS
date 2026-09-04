#include <stdint.h>
#include <stdio.h>

#include "core/crypto.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/update_system.h"
#include "core/update_remote_system.h"
#include "fs/fs.h"
#include "fs/storage.h"
#include "update_remote_system_host.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FILE_CAPACITY 8U
#define HOST_FILE_SIZE 4096U
#define HOST_SYSTEM_PACKAGE_SIZE (UPDATE_SYSTEM_HEADER_SIZE + 1024U)

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
static uint8_t host_writer_active;

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
    printf("ZCOV_BEGIN|case=host:core:update-remote-system|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:update-remote-system|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:update-remote-system|value=0x%08X\n",
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

static int host_is_package(const char* path) {
    return path && (kstrcmp(path, "system:/cache.zsys") == 0 ||
                    kstrcmp(path, "system:/ZSC0.ZSY") == 0 ||
                    kstrcmp(path, "system:/ZSC1.ZSY") == 0);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
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
    if (host_is_package(filename)) {
        if (size_out) *size_out = HOST_SYSTEM_PACKAGE_SIZE;
        if (attributes_out) *attributes_out = 0U;
        return OK;
    }
    file = host_file_find(filename, 0U);
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
    if (host_is_package(path)) {
        if (offset > HOST_SYSTEM_PACKAGE_SIZE ||
            max_size > HOST_SYSTEM_PACKAGE_SIZE - offset) return ERR_OVERFLOW;
        for (uint32_t index = 0U; index < max_size; index++) buffer[index] = 0U;
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

uint8_t fs_get_type(void) {
    return FS_TYPE_FAT32;
}

int fs_get_info(fs_info_t* info) {
    if (!info) return ERR_NULL;
    info->type = FS_TYPE_FAT32;
    info->bytes_per_sector = 512U;
    info->sectors_per_cluster = 1U;
    info->free_sectors = 100000U;
    return OK;
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

int storage_transaction_writer_begin(const char* id, const char* path,
                                     const char* temporary_path,
                                     uint32_t expected_size,
                                     uint8_t attributes) {
    (void)id;
    (void)path;
    (void)temporary_path;
    (void)expected_size;
    (void)attributes;
    host_writer_active = 1U;
    return OK;
}

int storage_transaction_writer_write(const uint8_t* data, uint32_t size) {
    if (size && !data) return ERR_NULL;
    return host_writer_active ? OK : ERR_STATE;
}

int storage_transaction_writer_finish(void) {
    host_writer_active = 0U;
    return OK;
}

int storage_transaction_writer_abort(void) {
    host_writer_active = 0U;
    return OK;
}

int storage_transaction_writer_is_active(void) {
    return host_writer_active;
}

static void host_fill_verification(update_system_verification_t* result_out) {
    if (!result_out) return;
    kmemset(result_out, 0, sizeof(*result_out));
    result_out->reason = UPDATE_SYSTEM_REASON_NONE;
    result_out->target_version.major = 2U;
    result_out->target_version.minor = 1U;
    result_out->target_version.patch = 0U;
    result_out->target_epoch = 9U;
    result_out->release_id[0] = 'R';
    result_out->release_id[1] = '1';
    result_out->release_tag[0] = 's';
    result_out->release_tag[1] = 't';
    result_out->release_tag[2] = 'a';
    result_out->release_tag[3] = 'b';
    result_out->release_tag[4] = 'l';
    result_out->release_tag[5] = 'e';
}

int update_system_verify_file(const char* path,
                              update_system_verification_t* result_out) {
    if (!path || !result_out) return ERR_NULL;
    host_fill_verification(result_out);
    return OK;
}

int update_system_check_tag(const char* tag,
                            const update_remote_options_t* options,
                            update_system_verification_t* result_out) {
    (void)options;
    if (!tag || !result_out) return ERR_NULL;
    host_fill_verification(result_out);
    result_out->reason = UPDATE_SYSTEM_REASON_FORMAT;
    return ERR_INVALID;
}

int update_system_transfer_tag(
    const char* tag, const update_remote_options_t* options,
    const update_system_transfer_t* transfer,
    update_system_verification_t* result_out,
    update_system_remote_asset_t* asset_out) {
    (void)tag;
    (void)options;
    (void)transfer;
    (void)result_out;
    (void)asset_out;
    return ERR_UNAVAILABLE;
}

void process_block(uint32_t ticks) {
    (void)ticks;
}

int main(void) {
    coverage_active = 1U;
    int result = update_remote_system_host_test_contracts();
    coverage_active = 0U;
    coverage_emit(result);
    if (result != OK) return result;
    puts("update-remote-system-host: PASS");
    return OK;
}
