#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/speaker.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/devfs.h"
#include "fs/vfs.h"
#include "fs/vfs_internal.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_DEVICE_SECTORS 40U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_release_count;
static uint32_t fake_speaker_beeps;
static uint32_t fake_speaker_stops;
static block_device_t fake_device;

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
    printf("ZCOV_BEGIN|case=host:storage:devfs|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:devfs|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:devfs|value=0x%08X\n",
           (uint32_t)result);
}

static void expect_true(int condition, const char* expression) {
    if (!condition) {
        fprintf(stderr, "devfs-host: falhou: %s\n", expression);
        (void)fflush(stderr);
        __builtin_trap();
    }
}

#define EXPECT(expression) expect_true((expression), #expression)

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

int block_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = 1U;
    return OK;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index != 0U) return ERR_NOT_FOUND;
    *out_device = fake_device;
    return OK;
}

int block_read(const char* id, uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!id || !buffer) return ERR_NULL;
    if (strcmp(id, fake_device.id) != 0 || !count ||
        lba >= fake_device.sector_count ||
        count > fake_device.sector_count - lba) {
        return ERR_INVALID;
    }
    for (uint32_t sector = 0U; sector < count; sector++) {
        for (uint32_t index = 0U; index < BLOCK_SECTOR_SIZE; index++) {
            buffer[sector * BLOCK_SECTOR_SIZE + index] =
                (uint8_t)(lba + sector + index);
        }
    }
    return OK;
}

int block_cache_sync_device(const char* device_id) {
    return device_id && strcmp(device_id, fake_device.id) == 0 ? OK :
           ERR_NOT_FOUND;
}

void vfs_mount_release(uint32_t slot, uint32_t generation) {
    (void)slot;
    (void)generation;
    fake_release_count++;
}

int vfs_stream_read(file_t* file, void* buffer, uint32_t size,
                    uint32_t* bytes_read) {
    (void)file;
    (void)buffer;
    (void)size;
    if (bytes_read) *bytes_read = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_stream_write(file_t* file, const void* buffer, uint32_t size,
                     uint32_t* bytes_written) {
    (void)file;
    (void)buffer;
    (void)size;
    if (bytes_written) *bytes_written = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_stream_poll(file_t* file, uint32_t events, uint32_t* revents) {
    (void)file;
    (void)events;
    if (revents) *revents = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    (void)path;
    (void)mode;
    if (fd_out) *fd_out = VFS_FD_INVALID;
    return ERR_UNAVAILABLE;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    (void)fd;
    (void)buffer;
    (void)size;
    if (bytes_read) *bytes_read = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    (void)fd;
    (void)buffer;
    (void)size;
    if (bytes_written) *bytes_written = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_fsync(int32_t fd) {
    (void)fd;
    return ERR_UNAVAILABLE;
}

int vfs_close(int32_t fd) {
    (void)fd;
    return ERR_UNAVAILABLE;
}

int vfs_ioctl(int32_t fd, uint32_t request, void* argument) {
    (void)fd;
    (void)request;
    (void)argument;
    return ERR_UNAVAILABLE;
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    (void)fd;
    (void)offset;
    (void)whence;
    if (position) *position = 0U;
    return ERR_UNAVAILABLE;
}

int vfs_copy_descriptors(vfs_descriptor_info_t* output, uint32_t capacity,
                         uint32_t* out_count) {
    (void)output;
    (void)capacity;
    if (!out_count) return ERR_NULL;
    *out_count = 0U;
    return OK;
}

void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    EXPECT(frequency >= DEVFS_SPEAKER_MIN_HZ);
    EXPECT(duration_ms >= DEVFS_SPEAKER_MIN_MS);
    fake_speaker_beeps++;
}

void speaker_off(void) {
    fake_speaker_stops++;
}

static void make_lookup(vfs_lookup_result_t* lookup, const char* path) {
    kmemset(lookup, 0, sizeof(*lookup));
    strcpy(lookup->canonical_path, path);
    lookup->mount_slot = 2U;
    lookup->mount_generation = 9U;
    lookup->mount_kind = VFS_MOUNT_DEVFS;
}

static void open_device(const char* path, uint32_t mode,
                        vnode_t* vnode, file_t* file,
                        devfs_file_context_t* context) {
    vfs_lookup_result_t lookup;

    make_lookup(&lookup, path);
    kmemset(vnode, 0, sizeof(*vnode));
    kmemset(file, 0, sizeof(*file));
    EXPECT(devfs_open_file(&lookup, mode, vnode, file, context) == OK);
    file->vnode = vnode;
}

static void test_contract_and_registry(void) {
    devfs_status_t status;
    devfs_node_info_t nodes[DEVFS_MAX_NODES];
    vfs_dir_entry_t entries[DEVFS_MAX_NODES];
    vfs_lookup_result_t lookup;
    uint32_t count = 0U;

    EXPECT(devfs_is_ready() == 0);
    EXPECT(devfs_get_status(&status) == ERR_STATE);
    EXPECT(devfs_lookup("/dev/null", &lookup) == ERR_STATE);
    EXPECT(devfs_init() == OK);
    EXPECT(devfs_is_ready() == 1);
    EXPECT(devfs_init() == OK);
    EXPECT(devfs_validate_state() == OK);
    EXPECT(devfs_get_status(&status) == OK);
    EXPECT(status.initialized == 1U);
    EXPECT(status.capacity == DEVFS_MAX_NODES);
    EXPECT(status.active == DEVFS_MAX_NODES);
    EXPECT(devfs_list(0, DEVFS_MAX_NODES, &count) == ERR_NULL);
    EXPECT(devfs_list(entries, DEVFS_MAX_NODES - 1U, &count) == ERR_OVERFLOW);
    EXPECT(devfs_list(entries, DEVFS_MAX_NODES, &count) == OK);
    EXPECT(count == DEVFS_MAX_NODES);
    EXPECT(strcmp(entries[DEVFS_NODE_NULL].name, "null") == 0);
    EXPECT(entries[DEVFS_NODE_HDA].size == HOST_DEVICE_SECTORS *
           BLOCK_SECTOR_SIZE);
    EXPECT(devfs_copy_nodes(0, DEVFS_MAX_NODES, &count) == ERR_NULL);
    EXPECT(devfs_copy_nodes(nodes, DEVFS_MAX_NODES - 1U, &count) == ERR_OVERFLOW);
    EXPECT(devfs_copy_nodes(nodes, DEVFS_MAX_NODES, &count) == OK);
    EXPECT(count == DEVFS_MAX_NODES);
    EXPECT(nodes[DEVFS_NODE_SPEAKER].writable == 1U);
    EXPECT(nodes[DEVFS_NODE_HDA].available == 1U);
    EXPECT(devfs_lookup("/dev", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_DIRECTORY);
    EXPECT(devfs_lookup("/dev/hda", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_BLOCK_DEVICE);
    EXPECT(lookup.size == HOST_DEVICE_SECTORS * BLOCK_SECTOR_SIZE);
    EXPECT(devfs_lookup("/dev/missing", &lookup) == ERR_NOT_FOUND);
    EXPECT(devfs_lookup(0, &lookup) == ERR_NULL);
    EXPECT(devfs_lookup("/dev/null", 0) == ERR_NULL);
}

static void test_unavailable_operations(void) {
    devfs_file_context_t context;
    vnode_t vnode;
    file_t file;
    uint32_t count = 0U;
    uint32_t position = 0U;

    open_device("/dev/speaker", VFS_MODE_WRITE, &vnode, &file, &context);
    EXPECT(file.vnode->operations->read(&file, 0, 1U, &count) ==
           ERR_UNAVAILABLE);
    EXPECT(count == 0U);
    EXPECT(file.vnode->operations->lseek(&file, 0, VFS_SEEK_SET, &position) ==
           ERR_UNAVAILABLE);
    EXPECT(position == 0U);
    EXPECT(file.vnode->operations->close(&file) == OK);

    open_device("/dev/null", VFS_MODE_READ_WRITE, &vnode, &file, &context);
    EXPECT(file.vnode->operations->ioctl(&file, 0U, 0) == ERR_UNAVAILABLE);
    EXPECT(file.vnode->operations->sync(&file) == ERR_UNAVAILABLE);
    EXPECT(file.vnode->operations->lseek(&file, 0, VFS_SEEK_SET, &position) ==
           ERR_UNAVAILABLE);
    EXPECT(file.vnode->operations->close(&file) == OK);
}

static void test_speaker_and_hda_operations(void) {
    app_speaker_tone_t tone = {440U, 20U};
    devfs_file_context_t context;
    vnode_t vnode;
    file_t file;
    uint8_t buffer[BLOCK_SECTOR_SIZE * 2U];
    uint32_t count = 0U;
    uint32_t position = 0U;

    open_device("/dev/speaker", VFS_MODE_WRITE, &vnode, &file, &context);
    EXPECT(file.vnode->operations->write(&file, &tone, sizeof(tone), &count) ==
           OK);
    EXPECT(count == sizeof(tone));
    EXPECT(file.vnode->operations->ioctl(&file, APP_IOCTL_SPEAKER_BEEP,
                                         &tone) == OK);
    EXPECT(file.vnode->operations->ioctl(&file, APP_IOCTL_SPEAKER_STOP, 0) ==
           OK);
    EXPECT(file.vnode->operations->write(&file, &tone, sizeof(tone) - 1U,
                                         &count) == ERR_INVALID);
    EXPECT(file.vnode->operations->ioctl(&file, APP_IOCTL_SPEAKER_BEEP, 0) ==
           ERR_NULL);
    EXPECT(file.vnode->operations->ioctl(&file, 99U, 0) == ERR_INVALID);
    EXPECT(file.vnode->operations->close(&file) == OK);
    EXPECT(fake_speaker_beeps == 2U);
    EXPECT(fake_speaker_stops == 1U);

    open_device("/dev/hda", VFS_MODE_READ, &vnode, &file, &context);
    EXPECT(file.vnode->operations->read(&file, buffer, sizeof(buffer), &count) ==
           OK);
    EXPECT(count == sizeof(buffer));
    EXPECT(buffer[0] == 0U);
    EXPECT(buffer[BLOCK_SECTOR_SIZE] == 1U);
    EXPECT(file.vnode->operations->write(&file, buffer, 1U, &count) ==
           ERR_UNAVAILABLE);
    EXPECT(file.vnode->operations->sync(&file) == OK);
    EXPECT(file.vnode->operations->lseek(&file, 1, VFS_SEEK_SET, &position) ==
           OK);
    EXPECT(position == 1U);
    EXPECT(file.vnode->operations->lseek(&file, -2, VFS_SEEK_CUR, &position) ==
           ERR_INVALID);
    EXPECT(file.vnode->operations->ioctl(&file, 0U, 0) == ERR_UNAVAILABLE);
    EXPECT(file.vnode->operations->close(&file) == OK);
}

static void test_invalid_contracts(void) {
    devfs_file_context_t context;
    vnode_t vnode;
    file_t file;
    vfs_lookup_result_t lookup;

    make_lookup(&lookup, "/dev/null");
    EXPECT(devfs_open_file(0, VFS_MODE_READ, &vnode, &file, &context) ==
           ERR_NULL);
    EXPECT(devfs_open_file(&lookup, VFS_MODE_READ, 0, &file, &context) ==
           ERR_NULL);
    EXPECT(devfs_open_file(&lookup, VFS_MODE_READ, &vnode, 0, &context) ==
           ERR_NULL);
    EXPECT(devfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, 0) ==
           ERR_NULL);
    EXPECT(devfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) ==
           OK);
    file.vnode = &vnode;
    EXPECT(file.vnode->operations->close(&file) == OK);
    make_lookup(&lookup, "/dev/null");
    lookup.mount_kind = VFS_MOUNT_PROCFS;
    EXPECT(devfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) ==
           OK);
    file.vnode = &vnode;
    EXPECT(file.vnode->operations->close(&file) == OK);
    make_lookup(&lookup, "/dev/speaker");
    EXPECT(devfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) ==
           ERR_UNAVAILABLE);
    make_lookup(&lookup, "/dev/hda");
    EXPECT(devfs_open_file(&lookup, VFS_MODE_WRITE, &vnode, &file, &context) ==
           ERR_UNAVAILABLE);
    EXPECT(fake_release_count >= 5U);
}

int main(void) {
    kmemset(&fake_device, 0, sizeof(fake_device));
    strcpy(fake_device.id, "hda0");
    strcpy(fake_device.model, "host-ata");
    fake_device.provider = BLOCK_PROVIDER_ATA;
    fake_device.sector_count = HOST_DEVICE_SECTORS;
    fake_device.sector_size = BLOCK_SECTOR_SIZE;
    fake_device.online = 1U;
    fake_device.max_transfer_sectors = BLOCK_MAX_TRANSFER_SECTORS;
    coverage_active = 1U;
    test_contract_and_registry();
    test_unavailable_operations();
    test_speaker_and_hda_operations();
    test_invalid_contracts();
    EXPECT(devfs_validate_state() == OK);
    coverage_active = 0U;
    coverage_emit(OK);
    return 0;
}
