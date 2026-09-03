#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/usb_manager.h"
#include "drivers/uhci.h"
#include "drivers/usb_msc.h"
#include "fs/block.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_DEVICE_CAPACITY (USB_MSC_MAX_DEVICES + 1U)
#define FAKE_BLOCK_CAPACITY (USB_MSC_MAX_DEVICES + 1U)
#define FAKE_SECTOR_COUNT 4U
#define FAKE_SECTOR_SIZE 512U
#define FAKE_CBW_LENGTH 31U
#define FAKE_CSW_LENGTH 13U
#define FAKE_CSW_SIGNATURE 0x53425355U
#define FAKE_CBW_SIGNATURE 0x43425355U
#define FAKE_CDB_INQUIRY 0x12U
#define FAKE_CDB_CAPACITY 0x25U
#define FAKE_CDB_READ10 0x28U
#define INVALID_INDEX UINT32_MAX

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t cdb_length;
    uint8_t cdb[16];
} __attribute__((packed)) fake_cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed)) fake_csw_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static usb_device_info_t fake_devices[FAKE_DEVICE_CAPACITY];
static uint32_t fake_device_count;
static uint32_t fake_get_device_error_index;
static int fake_manager_result;
static int fake_get_device_result;
static block_device_t fake_blocks[FAKE_BLOCK_CAPACITY];
static uint32_t fake_block_count;
static int fake_block_register_result;
static int fake_block_find_result;
static uint8_t fake_cdb[16];
static uint8_t fake_pending_command;
static uint32_t fake_pending_tag;
static uint32_t fake_bulk_calls;
static uint32_t fake_bulk_fail_call;
static int fake_bulk_fail_result;
static uint8_t fake_corrupt_csw;
static uint8_t fake_inquiry_invalid;
static uint8_t fake_capacity_invalid;
static uint8_t fake_csw_status;
static uint32_t fake_control_calls;
static uint32_t fake_control_fail_call;
static int fake_control_fail_result;
static int fake_control_default_result;
static int fake_reset_toggle_result;
static uint32_t fake_reset_toggle_calls;

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

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int usb_manager_get_device_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_manager_result != OK) return fake_manager_result;
    *out_count = fake_device_count;
    return OK;
}

int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index >= fake_device_count) return ERR_NOT_FOUND;
    if (index == fake_get_device_error_index) return fake_get_device_result;
    *out_info = fake_devices[index];
    return OK;
}

int block_register(const block_device_t* descriptor) {
    if (!descriptor) return ERR_NULL;
    if (fake_block_register_result != OK) return fake_block_register_result;
    if (fake_block_count >= FAKE_BLOCK_CAPACITY) return ERR_OVERFLOW;
    fake_blocks[fake_block_count++] = *descriptor;
    return OK;
}

int block_find(const char* id, block_device_t* out_device) {
    if (!id || !out_device) return ERR_NULL;
    if (fake_block_find_result != OK) return fake_block_find_result;
    for (uint32_t index = 0U; index < fake_block_count; index++) {
        if (strcmp(fake_blocks[index].id, id) == 0) {
            *out_device = fake_blocks[index];
            return OK;
        }
    }
    return ERR_NOT_FOUND;
}

int uhci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length) {
    (void)device;
    (void)request_type;
    (void)request;
    (void)value;
    (void)index;
    (void)length;
    (void)data;
    fake_control_calls++;
    if (out_length) *out_length = 0U;
    if (fake_control_fail_call &&
        fake_control_calls == fake_control_fail_call) {
        return fake_control_fail_result;
    }
    return fake_control_default_result;
}

int uhci_reset_bulk_toggles(const usb_device_info_t* device) {
    if (!device) return ERR_NULL;
    fake_reset_toggle_calls++;
    return fake_reset_toggle_result;
}

static void write_u32_be(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void fill_inquiry(uint8_t* buffer, uint16_t length) {
    memset(buffer, 0, length);
    if (length < 36U) return;
    buffer[0] = fake_inquiry_invalid ? 0x05U : 0U;
    memcpy(buffer + 8U, "ZEHPHYR ", 8U);
    memcpy(buffer + 16U, "USB MASS DISK   ", 16U);
    memcpy(buffer + 32U, "1.0 ", 4U);
}

static void fill_capacity(uint8_t* buffer, uint16_t length) {
    memset(buffer, 0, length);
    if (length < 8U) return;
    write_u32_be(buffer, fake_capacity_invalid ? UINT32_MAX :
                 FAKE_SECTOR_COUNT - 1U);
    write_u32_be(buffer + 4U, fake_capacity_invalid ? 1024U :
                 FAKE_SECTOR_SIZE);
}

static void fill_sector(uint8_t* buffer, uint16_t length) {
    uint32_t lba = ((uint32_t)fake_cdb[2] << 24U) |
                   ((uint32_t)fake_cdb[3] << 16U) |
                   ((uint32_t)fake_cdb[4] << 8U) | (uint32_t)fake_cdb[5];

    for (uint16_t index = 0U; index < length; index++) {
        buffer[index] = (uint8_t)(lba + index);
    }
}

static void fill_csw(uint8_t* buffer, uint16_t length) {
    fake_csw_t csw;

    memset(&csw, 0, sizeof(csw));
    csw.signature = fake_corrupt_csw ? 0U : FAKE_CSW_SIGNATURE;
    csw.tag = fake_corrupt_csw ? fake_pending_tag + 1U : fake_pending_tag;
    csw.status = fake_csw_status;
    memcpy(buffer, &csw, length < sizeof(csw) ? length : sizeof(csw));
}

int uhci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length) {
    fake_cbw_t cbw;

    (void)device;
    (void)endpoint_address;
    fake_bulk_calls++;
    if (out_length) *out_length = 0U;
    if (fake_bulk_fail_call && fake_bulk_calls == fake_bulk_fail_call) {
        fake_bulk_fail_call = 0U;
        return fake_bulk_fail_result;
    }
    if (!buffer) return ERR_NULL;
    if (!direction_in) {
        if (length != FAKE_CBW_LENGTH) return ERR_INVALID;
        memset(&cbw, 0, sizeof(cbw));
        memcpy(&cbw, buffer, sizeof(cbw));
        if (cbw.signature != FAKE_CBW_SIGNATURE || cbw.cdb_length > 16U) {
            return ERR_INVALID;
        }
        fake_pending_tag = cbw.tag;
        fake_pending_command = cbw.cdb[0];
        memset(fake_cdb, 0, sizeof(fake_cdb));
        memcpy(fake_cdb, cbw.cdb, cbw.cdb_length);
        if (out_length) *out_length = length;
        return OK;
    }
    if (length == FAKE_CSW_LENGTH) {
        fill_csw(buffer, length);
    } else if (fake_pending_command == FAKE_CDB_INQUIRY) {
        fill_inquiry(buffer, length);
    } else if (fake_pending_command == FAKE_CDB_CAPACITY) {
        fill_capacity(buffer, length);
    } else if (fake_pending_command == FAKE_CDB_READ10) {
        fill_sector(buffer, length);
    } else {
        memset(buffer, 0, length);
    }
    if (out_length) *out_length = length;
    return OK;
}

static void copy_id(char* destination, uint32_t capacity, const char* value) {
    strncpy(destination, value, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static void make_device(usb_device_info_t* device, const char* id) {
    memset(device, 0, sizeof(*device));
    copy_id(device->id, USB_DEVICE_ID_SIZE, id);
    copy_id(device->controller_id, USB_PORT_CONTROLLER_ID_SIZE, "uhci-msc");
    device->state = USB_DEVICE_CONFIGURED;
    device->speed = USB_DEVICE_SPEED_FULL;
    device->controller_model = USB_CONTROLLER_MODEL_UHCI;
    device->interface_class = 0x08U;
    device->interface_subclass = 0x06U;
    device->interface_protocol = 0x50U;
    device->interface_number = 1U;
    device->bulk_in_endpoint = 0x81U;
    device->bulk_out_endpoint = 0x02U;
    device->bulk_in_count = 1U;
    device->bulk_out_count = 1U;
    device->bulk_in_max_packet = 64U;
    device->bulk_out_max_packet = 64U;
}

static void reset_fixtures(void) {
    memset(fake_devices, 0, sizeof(fake_devices));
    memset(fake_blocks, 0, sizeof(fake_blocks));
    memset(fake_cdb, 0, sizeof(fake_cdb));
    fake_device_count = 0U;
    fake_get_device_error_index = INVALID_INDEX;
    fake_manager_result = OK;
    fake_get_device_result = ERR_DISK;
    fake_block_count = 0U;
    fake_block_register_result = OK;
    fake_block_find_result = OK;
    fake_pending_command = 0U;
    fake_pending_tag = 0U;
    fake_bulk_calls = 0U;
    fake_bulk_fail_call = 0U;
    fake_bulk_fail_result = ERR_TIMEOUT;
    fake_corrupt_csw = 0U;
    fake_inquiry_invalid = 0U;
    fake_capacity_invalid = 0U;
    fake_csw_status = 0U;
    fake_control_calls = 0U;
    fake_control_fail_call = 0U;
    fake_control_fail_result = ERR_DISK;
    fake_control_default_result = OK;
    fake_reset_toggle_result = OK;
    fake_reset_toggle_calls = 0U;
}

static int test_contract_before_init(void) {
    uint32_t count = 0U;
    usb_msc_info_t info;

    if (usb_msc_get_count(NULL) != ERR_NULL ||
        usb_msc_get_count(&count) != ERR_STATE ||
        usb_msc_get_at(0U, NULL) != ERR_NULL ||
        usb_msc_get_at(0U, &info) != ERR_STATE ||
        usb_msc_find(NULL, &info) != ERR_NULL ||
        usb_msc_find("missing", &info) != ERR_STATE ||
        usb_msc_refresh() != ERR_STATE || usb_msc_validate_state() != ERR_STATE ||
        usb_msc_is_active("missing") ||
        strcmp(usb_msc_state_name(USB_MSC_READY), "READY") != 0 ||
        strcmp(usb_msc_state_name(USB_MSC_DEGRADED), "DEGRADED") != 0) {
        return 10;
    }
    return 0;
}

static int test_happy_path(void) {
    block_device_t block;
    block_request_t request;
    usb_msc_info_t info;
    uint8_t buffer[FAKE_SECTOR_SIZE * 2U];
    uint32_t count = 0U;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-msc-0");
    make_device(&fake_devices[1], "usb-dev-ignored");
    fake_devices[1].interface_class = 3U;
    fake_device_count = 2U;
    if (usb_msc_init() != OK || usb_msc_get_count(&count) != OK || count != 1U ||
        usb_msc_get_at(0U, &info) != OK ||
        strcmp(info.id, "usb-dev-msc-0") != 0 ||
        strcmp(info.block_id, "usb-ms-msc-0-l0") != 0 ||
        strcmp(info.vendor, "ZEHPHYR") != 0 ||
        strcmp(info.product, "USB MASS DISK") != 0 ||
        strcmp(info.revision, "1.0") != 0 || info.sector_count != FAKE_SECTOR_COUNT ||
        info.sector_size != FAKE_SECTOR_SIZE || info.state != USB_MSC_READY ||
        info.command_count != 3U || info.reset_count != 0U ||
        !usb_msc_is_active("usb-dev-msc-0") || usb_msc_validate_state() != OK ||
        fake_block_count != 1U) return 20;
    if (block_find(info.block_id, &block) != OK || !block.read_only ||
        !block.online || block.provider != BLOCK_PROVIDER_USB_MSC ||
        block.ops.read == NULL || block.ops.submit == NULL || block.ops.write != NULL ||
        block.sector_count != FAKE_SECTOR_COUNT ||
        block.sector_size != FAKE_SECTOR_SIZE ||
        block.max_transfer_sectors != BLOCK_MAX_TRANSFER_SECTORS ||
        block.ops.read(block.ops.context, 0U, 2U, buffer) != OK ||
        buffer[0] != 0U || buffer[FAKE_SECTOR_SIZE] != 1U) return 21;
    memset(&request, 0, sizeof(request));
    request.device_context = block.ops.context;
    request.lba = 2U;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = FAKE_SECTOR_SIZE;
    request.operation = BLOCK_OPERATION_READ;
    if (block.ops.submit(&request) != OK || request.completed_sectors != 1U ||
        buffer[0] != 2U) return 22;
    if (block.ops.read(NULL, 0U, 1U, buffer) != ERR_NULL ||
        block.ops.read(block.ops.context, 0U, 0U, buffer) != ERR_NULL ||
        block.ops.read(block.ops.context, FAKE_SECTOR_COUNT, 1U, buffer) != ERR_DISK ||
        block.ops.submit(NULL) != ERR_NULL) return 23;
    memset(&request, 0, sizeof(request));
    request.operation = BLOCK_OPERATION_READ;
    if (block.ops.submit(&request) != ERR_NULL) return 24;
    request.device_context = block.ops.context;
    request.operation = BLOCK_OPERATION_WRITE;
    if (block.ops.submit(&request) != ERR_UNAVAILABLE) return 25;
    request.operation = BLOCK_OPERATION_READ;
    request.flags = BLOCK_BIO_FLAG_FUA;
    if (block.ops.submit(&request) != ERR_UNAVAILABLE) return 26;
    if (usb_msc_get_at(1U, &info) != ERR_INVALID ||
        usb_msc_get_at(0U, NULL) != ERR_NULL || usb_msc_find(NULL, &info) != ERR_NULL ||
        usb_msc_find("absent", &info) != ERR_NOT_FOUND ||
        usb_msc_find("usb-dev-msc-0", &info) != OK ||
        usb_msc_refresh() != OK || usb_msc_get_count(&count) != OK || count != 1U) {
        return 27;
    }
    return 0;
}

static int test_candidate_filters(void) {
    for (uint32_t index = 0U; index < 11U; index++) {
        uint32_t count = 0U;

        reset_fixtures();
        make_device(&fake_devices[0], "usb-dev-filter");
        if (index == 0U) fake_devices[0].state = USB_DEVICE_DEGRADED;
        if (index == 1U) fake_devices[0].speed = USB_DEVICE_SPEED_HIGH;
        if (index == 2U) fake_devices[0].interface_class = 0U;
        if (index == 3U) fake_devices[0].interface_subclass = 0U;
        if (index == 4U) fake_devices[0].interface_protocol = 0U;
        if (index == 5U) fake_devices[0].bulk_in_count = 0U;
        if (index == 6U) fake_devices[0].bulk_out_count = 0U;
        if (index == 7U) fake_devices[0].bulk_in_endpoint = 0U;
        if (index == 8U) fake_devices[0].bulk_out_endpoint = 0U;
        if (index == 9U) fake_devices[0].bulk_in_max_packet = 0U;
        if (index == 10U) fake_devices[0].bulk_out_max_packet = 0U;
        fake_device_count = 1U;
        if (usb_msc_init() != OK || usb_msc_get_count(&count) != OK || count != 0U ||
            usb_msc_validate_state() != OK) return 40 + (int)index;
    }
    return 0;
}

static int test_capacity_and_manager_paths(void) {
    uint32_t count = 0U;

    reset_fixtures();
    for (uint32_t index = 0U; index < FAKE_DEVICE_CAPACITY; index++) {
        char id[USB_DEVICE_ID_SIZE];

        snprintf(id, sizeof(id), "usb-dev-cap-%u", (unsigned)index);
        make_device(&fake_devices[index], id);
    }
    fake_device_count = FAKE_DEVICE_CAPACITY;
    if (usb_msc_init() != ERR_OVERFLOW || usb_msc_get_count(&count) != OK ||
        count != USB_MSC_MAX_DEVICES || usb_msc_validate_state() != OK) return 60;

    reset_fixtures();
    fake_manager_result = ERR_UNAVAILABLE;
    if (usb_msc_init() != ERR_UNAVAILABLE || usb_msc_get_count(&count) != OK ||
        count != 0U || usb_msc_validate_state() != OK) return 61;
    fake_manager_result = OK;
    if (usb_msc_refresh() != OK) return 62;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-missing");
    fake_device_count = 1U;
    fake_get_device_error_index = 0U;
    if (usb_msc_init() != OK || usb_msc_get_count(&count) != OK || count != 0U ||
        usb_msc_validate_state() != OK) return 63;
    return 0;
}

static int test_retry_and_block_io(void) {
    block_device_t block;
    usb_msc_info_t info;
    uint8_t buffer[FAKE_SECTOR_SIZE];

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-retry");
    fake_device_count = 1U;
    fake_bulk_fail_call = 1U;
    if (usb_msc_init() != OK || fake_reset_toggle_calls != 1U ||
        usb_msc_get_at(0U, &info) != OK || info.reset_count != 1U ||
        info.command_count != 4U || usb_msc_validate_state() != OK ||
        block_find(info.block_id, &block) != OK ||
        block.ops.read(block.ops.context, 0U, 1U, buffer) != OK) return 70;
    return 0;
}

static int test_failures_and_recovery(void) {
    usb_msc_info_t info;
    uint32_t count = 0U;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-csw");
    fake_device_count = 1U;
    fake_corrupt_csw = 1U;
    if (usb_msc_init() != ERR_INVALID || usb_msc_get_count(&count) != OK ||
        count != 1U || usb_msc_get_at(0U, &info) != OK ||
        info.state != USB_MSC_DEGRADED || info.last_error != ERR_INVALID ||
        info.reset_count != 1U || usb_msc_validate_state() != OK) return 80;
    fake_corrupt_csw = 0U;
    fake_control_calls = 0U;
    if (usb_msc_refresh() != OK || !usb_msc_is_active("usb-dev-csw") ||
        usb_msc_validate_state() != OK) return 81;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-control");
    fake_device_count = 1U;
    fake_bulk_fail_call = 1U;
    fake_control_fail_call = 1U;
    if (usb_msc_init() != ERR_TIMEOUT || usb_msc_get_at(0U, &info) != OK ||
        info.state != USB_MSC_DEGRADED || info.last_error != ERR_TIMEOUT ||
        usb_msc_validate_state() != OK) return 82;
    fake_control_fail_call = 0U;
    if (usb_msc_refresh() != OK || !usb_msc_is_active("usb-dev-control")) return 83;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-register");
    fake_device_count = 1U;
    fake_block_register_result = ERR_UNAVAILABLE;
    if (usb_msc_init() != ERR_UNAVAILABLE || usb_msc_get_at(0U, &info) != OK ||
        info.state != USB_MSC_DEGRADED || usb_msc_validate_state() != OK) return 84;
    fake_block_register_result = OK;
    if (usb_msc_refresh() != OK || !usb_msc_is_active("usb-dev-register")) return 85;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-inquiry");
    fake_device_count = 1U;
    fake_inquiry_invalid = 1U;
    if (usb_msc_init() != ERR_UNAVAILABLE || usb_msc_get_at(0U, &info) != OK ||
        info.state != USB_MSC_DEGRADED || info.last_error != ERR_UNAVAILABLE ||
        usb_msc_validate_state() != OK) return 86;

    reset_fixtures();
    make_device(&fake_devices[0], "usb-dev-capacity");
    fake_device_count = 1U;
    fake_capacity_invalid = 1U;
    if (usb_msc_init() != ERR_UNAVAILABLE || usb_msc_get_at(0U, &info) != OK ||
        info.state != USB_MSC_DEGRADED || info.last_error != ERR_UNAVAILABLE ||
        usb_msc_validate_state() != OK) return 87;
    return 0;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:usb-msc|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:usb-msc|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:usb-msc|value=0x%08X\n",
           (uint32_t)result);
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = test_contract_before_init();
    if (!result) result = test_happy_path();
    if (!result) result = test_candidate_filters();
    if (!result) result = test_capacity_and_manager_paths();
    if (!result) result = test_retry_and_block_io();
    if (!result) result = test_failures_and_recovery();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("USB_MSC_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("USB_MSC_HOST_PASS\n");
    return 0;
}
