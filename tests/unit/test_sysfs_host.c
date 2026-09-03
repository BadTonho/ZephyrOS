#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/network_manager.h"
#include "core/poll.h"
#include "core/power.h"
#include "core/string.h"
#include "drivers/pci.h"
#include "fs/block.h"
#include "fs/procfs.h"
#include "fs/sysfs.h"
#include "fs/vfs.h"

#define HOST_COVERAGE_CAPACITY 8192U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_SNAPSHOT_SLOTS 4U
#define HOST_ATTRIBUTE_BUFFER_SIZE 512U
#define HOST_PCI_COUNT 2U
#define HOST_NETWORK_COUNT 2U
#define HOST_BLOCK_COUNT 2U
#define HOST_PCI_ATTRIBUTE_COUNT 17U
#define HOST_NETWORK_ATTRIBUTE_COUNT 42U
#define HOST_BLOCK_ATTRIBUTE_COUNT 13U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t snapshot_storage[HOST_SNAPSHOT_SLOTS][PROCFS_MAX_SNAPSHOT_SIZE];
static uint8_t snapshot_used[HOST_SNAPSHOT_SLOTS];
static file_t fake_files[HOST_SNAPSHOT_SLOTS];
static vnode_t fake_vnodes[HOST_SNAPSHOT_SLOTS];
static sysfs_file_context_t fake_contexts[HOST_SNAPSHOT_SLOTS];
static uint8_t fake_fd_used[HOST_SNAPSHOT_SLOTS];
static uint32_t fake_mount_releases;
static int fake_power_result;
static pci_device_t fake_pci[HOST_PCI_COUNT];
static network_interface_info_t fake_network[HOST_NETWORK_COUNT];
static block_device_t fake_blocks[HOST_BLOCK_COUNT];

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
    printf("ZCOV_BEGIN|case=host:storage:sysfs|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:sysfs|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:sysfs|value=0x%08X\n",
           (uint32_t)result);
}

static int expect_true(int condition, const char* expression, int* failures) {
    if (condition) return OK;
    fprintf(stderr, "sysfs-host: falhou: %s\n", expression);
    (*failures)++;
    return ERR_STATE;
}

#define EXPECT(expression) \
    expect_true((expression), #expression, failures)

static void host_copy_text(char* destination, uint32_t capacity,
                           const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void host_append_text(char* destination, uint32_t capacity,
                             const char* source) {
    uint32_t offset;

    if (!destination || !capacity || !source) return;
    offset = kstrlen(destination);
    if (offset >= capacity) return;
    host_copy_text(destination + offset, capacity - offset, source);
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

void* kmalloc(uint32_t size) {
    if (!size || size > PROCFS_MAX_SNAPSHOT_SIZE) return 0;
    for (uint32_t index = 0U; index < HOST_SNAPSHOT_SLOTS; index++) {
        if (!snapshot_used[index]) {
            snapshot_used[index] = 1U;
            return snapshot_storage[index];
        }
    }
    return 0;
}

void kfree(void* pointer) {
    for (uint32_t index = 0U; index < HOST_SNAPSHOT_SLOTS; index++) {
        if (pointer == snapshot_storage[index]) {
            snapshot_used[index] = 0U;
            return;
        }
    }
}

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = HOST_PCI_COUNT;
    return OK;
}

int pci_get_device_at(uint8_t index, pci_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index >= HOST_PCI_COUNT) return ERR_NOT_FOUND;
    *out_device = fake_pci[index];
    return OK;
}

int network_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = HOST_NETWORK_COUNT;
    return OK;
}

int network_manager_get_interface(uint32_t index,
                                  network_interface_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index >= HOST_NETWORK_COUNT) return ERR_NOT_FOUND;
    *out_info = fake_network[index];
    return OK;
}

int network_manager_format_text(const network_interface_info_t* info,
                                network_interface_text_t* out_text) {
    if (!info || !out_text) return ERR_NULL;
    kmemset(out_text, 0, sizeof(*out_text));
    if (info->bus == 3U) {
        host_copy_text(out_text->id, sizeof(out_text->id), "znet");
        host_copy_text(out_text->name, sizeof(out_text->name), "Zeta network");
        host_copy_text(out_text->driver, sizeof(out_text->driver), "fake-e1000");
    } else {
        host_copy_text(out_text->id, sizeof(out_text->id), "anet");
        host_copy_text(out_text->name, sizeof(out_text->name), "Alpha network");
        host_copy_text(out_text->driver, sizeof(out_text->driver), "fake-rtl8139");
    }
    return OK;
}

int block_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = HOST_BLOCK_COUNT;
    return OK;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index >= HOST_BLOCK_COUNT) return ERR_NOT_FOUND;
    *out_device = fake_blocks[index];
    return OK;
}

int power_get_status(power_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fake_power_result != OK) return fake_power_result;
    kmemset(out_status, 0, sizeof(*out_status));
    for (uint32_t index = 0U; index < POWER_STATE_COUNT; index++) {
        out_status->states[index] = POWER_CAPABILITY_AVAILABLE;
    }
    out_status->cpu_idle = POWER_CAPABILITY_SIMULATED;
    out_status->hardware_poweroff = POWER_CAPABILITY_UNAVAILABLE;
    out_status->reboot = POWER_CAPABILITY_SIMULATED;
    out_status->service_state = POWER_SERVICE_READY;
    return OK;
}

static void set_lookup_defaults(vfs_lookup_result_t* lookup,
                                const char* path) {
    host_copy_text(lookup->canonical_path, sizeof(lookup->canonical_path), path);
    host_copy_text(lookup->mount_point, sizeof(lookup->mount_point), "/sys");
    lookup->mount_kind = VFS_MOUNT_SYSFS;
    lookup->mount_slot = 2U;
    lookup->mount_generation = 7U;
}

int vfs_lookup(const char* path, vfs_lookup_result_t* result) {
    int status;

    if (!path || !result) return ERR_NULL;
    status = sysfs_lookup(path, result);
    if (status != OK) return status;
    set_lookup_defaults(result, path);
    return OK;
}

int vfs_list_dir(const char* path, vfs_dir_entry_t* entries,
                uint32_t capacity, uint32_t* out_count) {
    return sysfs_list_path(path, entries, capacity, out_count);
}

int vfs_mount_acquire(uint32_t slot, uint32_t generation) {
    return slot == 2U && generation == 7U ? OK : ERR_STATE;
}

void vfs_mount_release(uint32_t slot, uint32_t generation) {
    (void)slot;
    (void)generation;
    fake_mount_releases++;
}

static int fake_fd_index(int32_t fd) {
    int32_t index = fd - VFS_FD_FIRST_FILE;

    if (index < 0 || (uint32_t)index >= HOST_SNAPSHOT_SLOTS ||
        !fake_fd_used[index]) return -1;
    return (int)index;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    vfs_lookup_result_t lookup;
    int result;

    if (!path || !fd_out) return ERR_NULL;
    *fd_out = VFS_FD_INVALID;
    result = vfs_lookup(path, &lookup);
    if (result != OK) return result;
    if (lookup.type != VFS_NODE_REGULAR) return ERR_INVALID;
    for (uint32_t index = 0U; index < HOST_SNAPSHOT_SLOTS; index++) {
        if (!fake_fd_used[index]) {
            kmemset(&fake_files[index], 0, sizeof(fake_files[index]));
            kmemset(&fake_vnodes[index], 0, sizeof(fake_vnodes[index]));
            kmemset(&fake_contexts[index], 0, sizeof(fake_contexts[index]));
            result = sysfs_open_file(&lookup, mode, &fake_vnodes[index],
                                     &fake_files[index], &fake_contexts[index]);
            if (result != OK) return result;
            fake_files[index].vnode = &fake_vnodes[index];
            fake_files[index].slot = index;
            result = fake_vnodes[index].operations->open(
                &fake_vnodes[index], &fake_files[index]);
            if (result != OK) {
                (void)fake_vnodes[index].operations->close(&fake_files[index]);
                return result;
            }
            fake_fd_used[index] = 1U;
            *fd_out = VFS_FD_FIRST_FILE + (int32_t)index;
            return OK;
        }
    }
    return ERR_OVERFLOW;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    int index = fake_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->read(&fake_files[index], buffer,
                                                      size, bytes_read);
}

int vfs_write(int32_t fd, const void* buffer, uint32_t size,
              uint32_t* bytes_written) {
    int index = fake_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->write(
        &fake_files[index], buffer, size, bytes_written);
}

int vfs_close(int32_t fd) {
    int index = fake_fd_index(fd);
    int result;

    if (index < 0) return ERR_INVALID;
    result = fake_files[index].vnode->operations->close(&fake_files[index]);
    if (result == OK) fake_fd_used[index] = 0U;
    return result;
}

int vfs_lseek(int32_t fd, int32_t offset, uint32_t whence,
              uint32_t* position) {
    int index = fake_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->lseek(
        &fake_files[index], offset, whence, position);
}

int vfs_ioctl(int32_t fd, uint32_t request, void* argument) {
    int index = fake_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->ioctl(
        &fake_files[index], request, argument);
}

int vfs_fsync(int32_t fd) {
    int index = fake_fd_index(fd);

    if (index < 0) return ERR_INVALID;
    return fake_files[index].vnode->operations->sync(&fake_files[index]);
}

int vfs_poll(pollfd_t* fds, uint32_t count, uint32_t timeout_ticks,
             uint32_t* out_ready) {
    int index;
    uint32_t revents = 0U;

    (void)timeout_ticks;
    if (!fds || !count || !out_ready) return ERR_NULL;
    *out_ready = 0U;
    index = fake_fd_index(fds[0].fd);
    if (index < 0) return ERR_INVALID;
    if (fake_files[index].vnode->operations->poll(
            &fake_files[index], fds[0].events, &revents) != OK) {
        return ERR_STATE;
    }
    fds[0].revents = revents;
    if (revents) *out_ready = 1U;
    return OK;
}

static void initialize_fixture(void) {
    kmemset(fake_pci, 0, sizeof(fake_pci));
    fake_pci[0].vendor_id = 0x8086U;
    fake_pci[0].device_id = 0x100EU;
    fake_pci[0].class = 0x02U;
    fake_pci[0].subclass = 0x00U;
    fake_pci[0].prog_if = 0x01U;
    fake_pci[0].revision = 0x02U;
    fake_pci[0].irq = 11U;
    fake_pci[0].bus = 2U;
    fake_pci[0].device = 4U;
    fake_pci[0].function = 1U;
    fake_pci[0].bar0 = 0xD0000000U;
    fake_pci[0].bar1 = 0xD0001000U;
    fake_pci[0].bar2 = 0xD0002000U;
    fake_pci[0].bar3 = 0xD0003000U;
    fake_pci[0].bar4 = 0xD0004000U;
    fake_pci[0].bar5 = 0xD0005000U;
    fake_pci[0].present = 1U;
    fake_pci[1] = fake_pci[0];
    fake_pci[1].vendor_id = 0x1234U;
    fake_pci[1].device_id = 0x5678U;
    fake_pci[1].bus = 1U;
    fake_pci[1].device = 2U;
    fake_pci[1].function = 0U;

    kmemset(fake_network, 0, sizeof(fake_network));
    fake_network[0].transport = NETWORK_TRANSPORT_PCI;
    fake_network[0].model = NETWORK_ADAPTER_RTL8139;
    fake_network[0].state = NETWORK_INTERFACE_DRIVER_ERROR;
    fake_network[0].link = NETWORK_LINK_DOWN;
    fake_network[0].bus = 3U;
    fake_network[0].vendor_id = 0x10ECU;
    fake_network[0].device_id = 0x8139U;
    fake_network[0].class_code = 2U;
    fake_network[0].subclass_code = 0U;
    fake_network[0].prog_if = 1U;
    fake_network[0].revision = 3U;
    fake_network[0].device = 5U;
    fake_network[0].function = 1U;
    fake_network[0].irq = 10U;
    fake_network[0].usb_port = 2U;
    fake_network[0].usb_address = 4U;
    fake_network[0].usb_revision = 0x1200U;
    fake_network[0].usb_endpoint_count = 3U;
    fake_network[0].mac_address[0] = 0x02U;
    fake_network[0].mac_address[1] = 0xAAU;
    fake_network[0].mac_address[2] = 0xBBU;
    fake_network[0].mac_address[3] = 0xCCU;
    fake_network[0].mac_address[4] = 0xDDU;
    fake_network[0].mac_address[5] = 0xEEU;
    fake_network[0].ethernet_attached = 1U;
    fake_network[0].l3_active = 0U;
    fake_network[0].dhcp_pending = 1U;
    fake_network[0].rx_packets = UINT32_MAX;
    fake_network[0].tx_packets = 21U;
    fake_network[0].rx_errors = 2U;
    fake_network[0].tx_errors = 3U;
    fake_network[0].rx_dropped = 4U;
    fake_network[0].rx_queue_depth = 5U;
    fake_network[0].rx_queue_high_water = 6U;
    fake_network[0].rx_queue_dropped = 7U;
    fake_network[0].rx_interrupts = 8U;
    fake_network[0].driver_error = -9;
    fake_network[0].bars[0] = 0xE0000000U;
    fake_network[0].bars[1] = 0xE0001000U;
    fake_network[0].bars[2] = 0xE0002000U;
    fake_network[0].bars[3] = 0xE0003000U;
    fake_network[0].bars[4] = 0xE0004000U;
    fake_network[0].bars[5] = 0xE0005000U;
    fake_network[1] = fake_network[0];
    fake_network[1].transport = NETWORK_TRANSPORT_USB;
    fake_network[1].model = NETWORK_ADAPTER_E1000;
    fake_network[1].state = NETWORK_INTERFACE_ACTIVE;
    fake_network[1].link = NETWORK_LINK_UP;
    fake_network[1].bus = 1U;
    fake_network[1].usb_device_id[0] = 'U';
    fake_network[1].usb_device_id[1] = '1';
    fake_network[1].ethernet_attached = 0U;
    fake_network[1].l3_active = 1U;
    fake_network[1].dhcp_pending = 0U;

    kmemset(fake_blocks, 0, sizeof(fake_blocks));
    host_copy_text(fake_blocks[0].id, sizeof(fake_blocks[0].id), "zblock");
    host_copy_text(fake_blocks[0].model, sizeof(fake_blocks[0].model), "Fake USB disk");
    fake_blocks[0].provider = BLOCK_PROVIDER_USB_MSC;
    fake_blocks[0].sector_count = 200U;
    fake_blocks[0].sector_size = BLOCK_SECTOR_SIZE;
    fake_blocks[0].read_only = 1U;
    fake_blocks[0].online = 1U;
    fake_blocks[0].read_ops = 10U;
    fake_blocks[0].write_ops = 0U;
    fake_blocks[0].max_transfer_sectors = 32U;
    fake_blocks[0].capabilities = BLOCK_DEVICE_CAP_FLUSH;
    fake_blocks[0].last_error = -5;
    fake_blocks[1] = fake_blocks[0];
    host_copy_text(fake_blocks[1].id, sizeof(fake_blocks[1].id), "ablock");
    host_copy_text(fake_blocks[1].model, sizeof(fake_blocks[1].model), "Fake ATA disk");
    fake_blocks[1].provider = BLOCK_PROVIDER_ATA;
    fake_blocks[1].read_only = 0U;
    fake_blocks[1].online = 0U;
    fake_blocks[1].write_ops = 12U;
    fake_blocks[1].last_error = 0;
    fake_power_result = OK;
}

static void build_path(char* output, uint32_t capacity, const char* prefix,
                       const char* name) {
    if (!output || !capacity) return;
    output[0] = '\0';
    host_copy_text(output, capacity, prefix);
    host_append_text(output, capacity, name);
}

static void exercise_file(const char* path, int* failures) {
    int32_t fd = VFS_FD_INVALID;
    uint8_t buffer[HOST_ATTRIBUTE_BUFFER_SIZE];
    uint32_t bytes = 0U;
    uint32_t position = 0U;
    pollfd_t poll = {0};

    EXPECT(vfs_open(path, VFS_MODE_READ, &fd) == OK);
    if (fd == VFS_FD_INVALID) return;
    EXPECT(vfs_read(fd, buffer, sizeof(buffer), &bytes) == OK);
    EXPECT(bytes > 0U);
    EXPECT(vfs_lseek(fd, 0, VFS_SEEK_SET, &position) == OK);
    EXPECT(position == 0U);
    EXPECT(vfs_lseek(fd, 0, VFS_SEEK_END, &position) == OK);
    EXPECT(vfs_read(fd, buffer, sizeof(buffer), &bytes) == OK);
    EXPECT(bytes == 0U);
    EXPECT(vfs_lseek(fd, -1, VFS_SEEK_SET, &position) == ERR_INVALID);
    EXPECT(vfs_lseek(fd, 1, VFS_SEEK_END, &position) == ERR_INVALID);
    poll.fd = fd;
    poll.events = POLLIN;
    EXPECT(vfs_poll(&poll, 1U, 0U, &bytes) == OK);
    EXPECT((poll.revents & POLLIN) != 0U);
    EXPECT(vfs_write(fd, buffer, 1U, &bytes) == ERR_UNAVAILABLE);
    EXPECT(bytes == 0U);
    EXPECT(vfs_ioctl(fd, 0U, 0) == ERR_UNAVAILABLE);
    EXPECT(vfs_fsync(fd) == ERR_UNAVAILABLE);
    EXPECT(vfs_close(fd) == OK);
}

static void test_contract_and_paths(int* failures) {
    vfs_lookup_result_t lookup;
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    uint32_t count = 0U;

    EXPECT(sysfs_is_ready() == 0);
    EXPECT(sysfs_lookup("/sys", &lookup) == ERR_STATE);
    EXPECT(sysfs_lookup(0, &lookup) == ERR_NULL);
    EXPECT(sysfs_self_test(0) == ERR_NULL);
    EXPECT(sysfs_init() == OK);
    EXPECT(sysfs_init() == OK);
    EXPECT(sysfs_is_ready() == 1);
    EXPECT(sysfs_validate_state() == OK);
    EXPECT(sysfs_lookup("/sys", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_DIRECTORY);
    EXPECT(lookup.read_only == 1U);
    EXPECT(sysfs_lookup("/sys/power/state", &lookup) == OK);
    EXPECT(lookup.type == VFS_NODE_REGULAR);
    EXPECT(sysfs_lookup("/outside", &lookup) == ERR_INVALID);
    EXPECT(sysfs_lookup("/sys/no-such-node", &lookup) == ERR_NOT_FOUND);
    EXPECT(sysfs_lookup("/sys/class/net/missing", &lookup) == ERR_NOT_FOUND);
    EXPECT(sysfs_lookup("/sys/class/net/anet/no-such", &lookup) == ERR_NOT_FOUND);
    EXPECT(sysfs_lookup("/sys/class/net/anet/", &lookup) == ERR_INVALID);
    EXPECT(sysfs_list(0, 1U, &count) == ERR_NULL);
    EXPECT(sysfs_list(entries, 0U, &count) == ERR_OVERFLOW);
    EXPECT(sysfs_list(entries, VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == 3U);
    EXPECT(sysfs_list_path("/sys", entries, 2U, &count) == ERR_OVERFLOW);
    EXPECT(sysfs_list_path("/sys/no-such-node", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == ERR_NOT_FOUND);
    EXPECT(sysfs_list_path("/sys/bus/pci/devices", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == HOST_PCI_COUNT);
    EXPECT(sysfs_list_path("/sys/class/net", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == HOST_NETWORK_COUNT);
    EXPECT(sysfs_list_path("/sys/class/block", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == HOST_BLOCK_COUNT);
    EXPECT(sysfs_list_path("/sys/bus/pci/devices/01:02.0", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == HOST_PCI_ATTRIBUTE_COUNT);
    EXPECT(sysfs_list_path("/sys/class/net/anet", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == HOST_NETWORK_ATTRIBUTE_COUNT);
    EXPECT(sysfs_list_path("/sys/class/block/ablock", entries,
                           VFS_MAX_DIR_ENTRIES, &count) == OK);
    EXPECT(count == HOST_BLOCK_ATTRIBUTE_COUNT);
}

static void test_attribute_rendering(int* failures) {
    static const char* const pci_attributes[HOST_PCI_ATTRIBUTE_COUNT] = {
        "bus", "device", "function", "vendor_id", "device_id", "class",
        "subclass", "prog_if", "revision", "irq", "bar0", "bar1", "bar2",
        "bar3", "bar4", "bar5", "present"
    };
    static const char* const network_attributes[HOST_NETWORK_ATTRIBUTE_COUNT] = {
        "id", "name", "driver", "transport", "model", "state", "link",
        "vendor_id", "device_id", "class", "subclass", "prog_if", "revision",
        "bus", "device", "function", "irq", "bar0", "bar1", "bar2", "bar3",
        "bar4", "bar5", "usb_device_id", "usb_port", "usb_address",
        "usb_revision", "usb_endpoint_count", "mac_address", "ethernet_attached",
        "l3_active", "dhcp_pending", "rx_packets", "tx_packets", "rx_errors",
        "tx_errors", "rx_dropped", "rx_queue_depth", "rx_queue_high_water",
        "rx_queue_dropped", "rx_interrupts", "driver_error"
    };
    static const char* const block_attributes[HOST_BLOCK_ATTRIBUTE_COUNT] = {
        "id", "model", "provider", "sector_count", "capacity_bytes",
        "sector_size", "read_only", "online", "read_ops", "write_ops",
        "max_transfer_sectors", "capabilities", "last_error"
    };
    char path[VFS_MAX_PATH];

    for (uint32_t index = 0U; index < HOST_PCI_ATTRIBUTE_COUNT; index++) {
        build_path(path, sizeof(path), "/sys/bus/pci/devices/01:02.0/",
                   pci_attributes[index]);
        exercise_file(path, failures);
    }
    for (uint32_t index = 0U; index < HOST_NETWORK_ATTRIBUTE_COUNT; index++) {
        build_path(path, sizeof(path), "/sys/class/net/anet/",
                   network_attributes[index]);
        exercise_file(path, failures);
    }
    for (uint32_t index = 0U; index < HOST_BLOCK_ATTRIBUTE_COUNT; index++) {
        build_path(path, sizeof(path), "/sys/class/block/ablock/",
                   block_attributes[index]);
        exercise_file(path, failures);
    }
    exercise_file("/sys/power/state", failures);
}

static void test_file_contracts(int* failures) {
    vfs_lookup_result_t lookup;
    vnode_t vnode;
    file_t file;
    sysfs_file_context_t context;
    uint32_t bytes = 0U;
    uint32_t position = 0U;

    EXPECT(vfs_lookup("/sys/power/state", &lookup) == OK);
    EXPECT(sysfs_open_file(0, VFS_MODE_READ, &vnode, &file, &context) == ERR_NULL);
    EXPECT(sysfs_open_file(&lookup, VFS_MODE_WRITE, &vnode, &file, &context) ==
           ERR_UNAVAILABLE);
    lookup.mount_kind = VFS_MOUNT_PROCFS;
    EXPECT(sysfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) ==
           ERR_INVALID);
    lookup.mount_kind = VFS_MOUNT_SYSFS;
    EXPECT(sysfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) == OK);
    file.vnode = &vnode;
    EXPECT(vnode.operations->open(0, &file) == ERR_NULL);
    EXPECT(vnode.operations->read(&file, 0, 1U, &bytes) == ERR_NULL);
    EXPECT(vnode.operations->write(&file, 0, 1U, &bytes) == ERR_UNAVAILABLE);
    EXPECT(vnode.operations->lseek(&file, 0, 99U, &position) == ERR_INVALID);
    EXPECT(vnode.operations->ioctl(0, 0U, 0) == ERR_UNAVAILABLE);
    EXPECT(vnode.operations->sync(0) == ERR_UNAVAILABLE);
    EXPECT(vnode.operations->poll(0, 0U, &bytes) == ERR_NULL);
    EXPECT(vnode.operations->close(&file) == OK);
    EXPECT(vnode.operations->close(&file) == OK);
    EXPECT(sysfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) == OK);
    EXPECT(vnode.operations->close(&file) == OK);
}

static void test_fallbacks_and_limits(int* failures) {
    vfs_lookup_result_t lookup;
    vnode_t vnode;
    file_t file;
    sysfs_file_context_t context;
    uint8_t buffer[HOST_ATTRIBUTE_BUFFER_SIZE];
    uint32_t length = 0U;
    int32_t fd = VFS_FD_INVALID;

    fake_power_result = ERR_UNAVAILABLE;
    EXPECT(vfs_open("/sys/power/state", VFS_MODE_READ, &fd) == OK);
    if (fd != VFS_FD_INVALID) {
        EXPECT(vfs_read(fd, buffer, sizeof(buffer), &length) == OK);
        EXPECT(length > 0U);
        EXPECT(vfs_close(fd) == OK);
    }
    fake_power_result = OK;
    fake_blocks[0].sector_count = UINT32_MAX;
    EXPECT(vfs_lookup("/sys/class/block/zblock/capacity_bytes", &lookup) == OK);
    EXPECT(sysfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) ==
           ERR_OVERFLOW);
    fake_blocks[0].sector_count = 200U;
    EXPECT(vfs_lookup("/sys/class/block/zblock/unknown", &lookup) == ERR_NOT_FOUND);
    EXPECT(vfs_lookup("/sys/bus/pci/devices/02:04.1/vendor_id", &lookup) == OK);
    EXPECT(sysfs_open_file(&lookup, VFS_MODE_READ, &vnode, &file, &context) == OK);
    file.vnode = &vnode;
    EXPECT(vnode.operations->close(&file) == OK);
}

static void test_self_test_and_cleanup(int* failures) {
    sysfs_test_result_t result;
    uint32_t active_before = fake_mount_releases;

    EXPECT(sysfs_self_test(&result) == OK);
    EXPECT(result.total == result.passed);
    EXPECT(result.total >= 10U);
    EXPECT(result.registry && result.lookup && result.listing && result.read);
    EXPECT(result.permissions && result.seek_eof && result.format);
    EXPECT(result.cleanup && result.inventories && result.power);
    EXPECT(sysfs_validate_state() == OK);
    EXPECT(fake_mount_releases > active_before);
}

int main(void) {
    int failures = 0;

    initialize_fixture();
    coverage_active = 1U;
    test_contract_and_paths(&failures);
    test_attribute_rendering(&failures);
    test_file_contracts(&failures);
    test_fallbacks_and_limits(&failures);
    test_self_test_and_cleanup(&failures);
    coverage_active = 0U;
    coverage_emit(failures ? ERR_STATE : OK);
    if (failures) return 1;
    printf("sysfs-host: PASS\n");
    return 0;
}
