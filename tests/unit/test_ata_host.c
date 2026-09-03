#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/ata.h"
#include "drivers/idt.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_CHANNEL_COUNT 2U
#define HOST_DRIVE_COUNT 2U
#define HOST_SECTOR_SIZE 512U
#define HOST_SECTOR_WORDS 256U
#define HOST_STATUS_NORMAL 0U
#define HOST_STATUS_ERROR 1U
#define HOST_STATUS_BUSY 2U
#define HOST_STATUS_NO_DRQ 3U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static uint8_t fake_present[HOST_CHANNEL_COUNT][HOST_DRIVE_COUNT];
static uint16_t fake_identify[HOST_CHANNEL_COUNT][HOST_DRIVE_COUNT][256];
static uint8_t fake_identify_invalid_layout[HOST_CHANNEL_COUNT][HOST_DRIVE_COUNT];
static uint8_t fake_selected_channel;
static uint8_t fake_selected_drive;
static uint8_t fake_command;
static uint8_t fake_preparing;
static uint8_t fake_sector_count;
static uint32_t fake_lba;
static uint32_t fake_data_word;
static uint32_t fake_identify_word;
static uint32_t fake_read_commands_remaining;
static uint8_t fake_read_command_error;
static uint8_t fake_ready_status_mode;
static uint8_t fake_identify_status_mode;
static uint8_t fake_read_status_mode;
static uint8_t fake_write_data_status_mode;
static uint8_t fake_write_complete_status_mode;
static uint8_t fake_flush_status_mode;
static int fake_primary_irq_result;
static int fake_secondary_irq_result;
static isr_handler_t fake_handlers[256];

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
    printf("ZCOV_BEGIN|case=host:drivers:ata|value=0x%08X\n", coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:ata|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:ata|value=0x%08X\n",
           (uint32_t)result);
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;

    if (!bytes) return;
    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    uint8_t* out = (uint8_t*)destination;
    const uint8_t* in = (const uint8_t*)source;

    if (!out || !in) return;
    for (uint32_t index = 0U; index < size; index++) out[index] = in[index];
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int idt_register_handler(uint8_t vector, isr_handler_t handler) {
    if (vector == ATA_PRIMARY_VECTOR) {
        if (fake_primary_irq_result != OK) return fake_primary_irq_result;
    }
    if (vector == ATA_SECONDARY_VECTOR) {
        if (fake_secondary_irq_result != OK) return fake_secondary_irq_result;
    }
    fake_handlers[vector] = handler;
    return OK;
}

static uint8_t fake_channel_for_port(uint16_t port) {
    if (port >= ATA_SECONDARY_IO && port <= ATA_SECONDARY_IO + 7U) {
        return 1U;
    }
    return 0U;
}

static uint16_t fake_io_for_channel(uint8_t channel) {
    return channel == 0U ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
}

static uint8_t fake_status_from_mode(uint8_t mode) {
    if (mode == HOST_STATUS_ERROR) return ATA_SR_ERR;
    if (mode == HOST_STATUS_BUSY) return ATA_SR_BSY;
    if (mode == HOST_STATUS_NO_DRQ) return ATA_SR_DRDY;
    return ATA_SR_DRDY | ATA_SR_DRQ;
}

static uint8_t fake_status(uint8_t channel) {
    uint8_t mode = HOST_STATUS_NORMAL;

    if (!fake_present[channel][fake_selected_drive]) return 0U;
    if (fake_preparing) {
        return fake_status_from_mode(fake_ready_status_mode);
    }
    if (fake_command == ATA_CMD_IDENTIFY) {
        return fake_status_from_mode(fake_identify_status_mode);
    }
    if (fake_command == ATA_CMD_READ) {
        mode = fake_read_command_error ? HOST_STATUS_ERROR : fake_read_status_mode;
        return fake_status_from_mode(mode);
    }
    if (fake_command == ATA_CMD_WRITE) {
        if (fake_data_word < (uint32_t)fake_sector_count * HOST_SECTOR_WORDS) {
            return fake_status_from_mode(fake_write_data_status_mode);
        }
        return fake_status_from_mode(fake_write_complete_status_mode);
    }
    if (fake_command == ATA_CMD_FLUSH) {
        return fake_status_from_mode(fake_flush_status_mode);
    }
    if (fake_identify_status_mode == HOST_STATUS_BUSY) {
        return ATA_SR_BSY;
    }
    return fake_status_from_mode(fake_ready_status_mode);
}

void ata_host_outb(uint16_t port, uint8_t value) {
    uint8_t channel;
    uint16_t io;

    if (port == ATA_PRIMARY_CTRL || port == ATA_SECONDARY_CTRL) {
        fake_preparing = value == 0x02U;
        if (value == 0U || value == 0x04U) fake_command = 0U;
        return;
    }
    channel = fake_channel_for_port(port);
    io = fake_io_for_channel(channel);
    if (port == io + ATA_REG_DRIVE) {
        fake_selected_channel = channel;
        fake_selected_drive = (uint8_t)((value >> 4) & 1U);
        fake_lba = (fake_lba & 0x00FFFFFFU) |
                   ((uint32_t)(value & 0x0FU) << 24);
        return;
    }
    if (port == io + ATA_REG_SECCOUNT) {
        fake_sector_count = value;
        return;
    }
    if (port == io + ATA_REG_LBA_LOW) {
        fake_lba = (fake_lba & 0xFFFFFF00U) | value;
        return;
    }
    if (port == io + ATA_REG_LBA_MID) {
        fake_lba = (fake_lba & 0xFFFF00FFU) | ((uint32_t)value << 8);
        return;
    }
    if (port == io + ATA_REG_LBA_HIGH) {
        fake_lba = (fake_lba & 0xFF00FFFFU) | ((uint32_t)value << 16);
        return;
    }
    if (port != io + ATA_REG_COMMAND) return;

    fake_command = value;
    fake_preparing = 0U;
    fake_data_word = 0U;
    fake_identify_word = 0U;
    if (value == ATA_CMD_READ) {
        fake_read_command_error = fake_read_commands_remaining != 0U;
        if (fake_read_commands_remaining != 0U) fake_read_commands_remaining--;
    } else {
        fake_read_command_error = 0U;
    }
}

uint8_t ata_host_inb(uint16_t port) {
    uint8_t channel;
    uint16_t io;

    if (port == ATA_PRIMARY_CTRL || port == ATA_SECONDARY_CTRL) return 0U;
    channel = fake_channel_for_port(port);
    io = fake_io_for_channel(channel);
    if (port == io + ATA_REG_STATUS) return fake_status(channel);
    if (port == io + ATA_REG_LBA_MID || port == io + ATA_REG_LBA_HIGH) {
        if (fake_command == ATA_CMD_IDENTIFY &&
            fake_identify_invalid_layout[channel][fake_selected_drive]) {
            return 1U;
        }
    }
    return 0U;
}

uint16_t ata_host_inw(uint16_t port) {
    uint16_t io = fake_io_for_channel(fake_selected_channel);
    uint32_t sector;
    uint32_t word;

    if (port != io + ATA_REG_DATA) return 0U;
    if (fake_command == ATA_CMD_IDENTIFY) {
        if (fake_identify_word >= 256U) return 0U;
        return fake_identify[fake_selected_channel][fake_selected_drive]
            [fake_identify_word++];
    }
    if (fake_command != ATA_CMD_READ) return 0U;
    sector = fake_data_word / HOST_SECTOR_WORDS;
    word = fake_data_word % HOST_SECTOR_WORDS;
    fake_data_word++;
    return (uint16_t)(0xA000U |
                      (((fake_lba + sector) & 0x003FU) << 8) |
                      (word & 0x00FFU));
}

void ata_host_outw(uint16_t port, uint16_t value) {
    uint16_t io = fake_io_for_channel(fake_selected_channel);

    (void)value;
    if (port == io + ATA_REG_DATA && fake_command == ATA_CMD_WRITE) {
        fake_data_word++;
    }
}

static void fake_prepare_identify(uint8_t channel, uint8_t drive,
                                  uint32_t sectors, uint8_t flush) {
    static const char model[] = "ZEphyr ATA TEST DISK";

    fake_identify[channel][drive][0] = 0x0040U;
    fake_identify[channel][drive][49] = 0x0200U;
    fake_identify[channel][drive][60] = (uint16_t)(sectors & 0xFFFFU);
    fake_identify[channel][drive][61] = (uint16_t)(sectors >> 16);
    fake_identify[channel][drive][82] = flush ? (uint16_t)(1U << 12) : 0U;
    for (uint32_t index = 0U; index < 20U; index++) {
        uint8_t first = model[index * 2U];
        uint8_t second = model[index * 2U + 1U];
        fake_identify[channel][drive][27U + index] =
            (uint16_t)(((uint16_t)first << 8) | second);
    }
}

static void fake_reset(void) {
    kmemset(fake_present, 0U, sizeof(fake_present));
    kmemset(fake_identify, 0U, sizeof(fake_identify));
    kmemset(fake_identify_invalid_layout, 0U,
            sizeof(fake_identify_invalid_layout));
    kmemset(fake_handlers, 0U, sizeof(fake_handlers));
    fake_present[0][0] = 1U;
    fake_present[1][0] = 1U;
    fake_prepare_identify(0U, 0U, 1000U, 1U);
    fake_prepare_identify(1U, 0U, 1000U, 0U);
    fake_selected_channel = 0U;
    fake_selected_drive = 0U;
    fake_command = 0U;
    fake_preparing = 0U;
    fake_sector_count = 0U;
    fake_lba = 0U;
    fake_data_word = 0U;
    fake_identify_word = 0U;
    fake_read_commands_remaining = 0U;
    fake_read_command_error = 0U;
    fake_ready_status_mode = HOST_STATUS_NORMAL;
    fake_identify_status_mode = HOST_STATUS_NORMAL;
    fake_read_status_mode = HOST_STATUS_NORMAL;
    fake_write_data_status_mode = HOST_STATUS_NORMAL;
    fake_write_complete_status_mode = HOST_STATUS_NORMAL;
    fake_flush_status_mode = HOST_STATUS_NORMAL;
    fake_primary_irq_result = OK;
    fake_secondary_irq_result = OK;
}

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            printf("ATA_CHECK_FAILED:%s:%u\n", __FILE__, __LINE__);           \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int test_before_init(void) {
    uint8_t count = 0U;
    uint32_t reads = 0U;
    uint32_t writes = 0U;
    ata_device_t device;
    uint8_t buffer[HOST_SECTOR_SIZE];

    CHECK(ata_get_device() == 0);
    CHECK(ata_get_read_ops() == 0U);
    CHECK(ata_get_write_ops() == 0U);
    CHECK(ata_get_device_count(0) == ERR_NULL);
    CHECK(ata_get_device_count(&count) == ERR_STATE);
    CHECK(ata_get_device_at(0, 0) == ERR_NULL);
    CHECK(ata_get_device_at(0, &device) == ERR_STATE);
    CHECK(ata_get_device_counters(0, 0, &writes) == ERR_NULL);
    CHECK(ata_get_device_counters(0, &reads, &writes) == ERR_STATE);
    CHECK(ata_read_device_sectors(ATA_MAX_DEVICES, 0U, 1U, buffer) == ERR_INVALID);
    CHECK(ata_read_device_sectors(0U, 0U, 1U, buffer) == ERR_NOT_FOUND);
    CHECK(ata_read_sectors(0U, 1U, buffer) == ERR_NOT_FOUND);
    CHECK(ata_write_device_sectors(ATA_MAX_DEVICES, 0U, 1U, buffer) == ERR_INVALID);
    CHECK(ata_write_device_sectors(0U, 0U, 1U, buffer) == ERR_NOT_FOUND);
    CHECK(ata_write_sectors(0U, 1U, buffer) == ERR_NOT_FOUND);
    CHECK(ata_flush_device(ATA_MAX_DEVICES) == ERR_INVALID);
    CHECK(ata_flush_device(0U) == ERR_STATE);
    return 0;
}

static int test_inventory(void) {
    ata_device_t device;
    uint8_t count;
    uint32_t sectors;

    fake_reset();
    fake_prepare_identify(0U, 0U, ATA_LBA28_MAX_SECTORS + 100U, 1U);
    CHECK(ata_init() == OK);
    CHECK(ata_get_device() != 0);
    CHECK(ata_get_device()->slot == 0U);
    CHECK(ata_get_device_count(&count) == OK);
    CHECK(count == 2U);
    CHECK(ata_get_device_at(0U, &device) == OK);
    CHECK(device.present == 1);
    CHECK(device.sectors == ATA_LBA28_MAX_SECTORS);
    CHECK(device.flush_supported == 1U);
    CHECK(device.model[0] == 'Z');
    CHECK(device.model[1] == 'E');
    CHECK(ata_get_device_at(1U, &device) == ERR_NOT_FOUND);
    CHECK(ata_get_device_at(ATA_MAX_DEVICES, &device) == ERR_INVALID);
    CHECK(ata_get_device_at(0U, 0) == ERR_NULL);
    CHECK(ata_get_device_counters(0U, 0, &sectors) == ERR_NULL);
    CHECK(ata_get_device_counters(0U, &sectors, &sectors) == OK);
    CHECK(sectors == 0U);
    CHECK(ata_get_device_counters(1U, &sectors, &sectors) == ERR_NOT_FOUND);
    CHECK(ata_get_device_counters(ATA_MAX_DEVICES, &sectors, &sectors) == ERR_INVALID);
    CHECK(ata_get_device_count(0) == ERR_NULL);
    CHECK(fake_handlers[ATA_PRIMARY_VECTOR] != 0);
    CHECK(fake_handlers[ATA_SECONDARY_VECTOR] != 0);
    fake_handlers[ATA_PRIMARY_VECTOR](0);
    fake_handlers[ATA_SECONDARY_VECTOR](0);

    fake_prepare_identify(0U, 0U, 1000U, 1U);
    fake_secondary_irq_result = ERR_STATE;
    CHECK(ata_init() == OK);
    CHECK(ata_get_device_count(&count) == OK);
    CHECK(count == 1U);
    fake_reset();
    fake_primary_irq_result = ERR_STATE;
    CHECK(ata_init() == ERR_STATE);
    fake_reset();
    fake_present[0][0] = 0U;
    fake_present[1][0] = 0U;
    CHECK(ata_init() == ERR_NOT_FOUND);
    fake_reset();
    fake_identify_invalid_layout[0][0] = 1U;
    CHECK(ata_init() == OK);
    CHECK(ata_get_device_count(&count) == OK);
    CHECK(count == 1U);
    fake_reset();
    fake_present[1][0] = 0U;
    fake_identify_status_mode = HOST_STATUS_BUSY;
    CHECK(ata_init() == ERR_NOT_FOUND);
    fake_reset();
    CHECK(ata_init() == OK);
    return 0;
}

static int test_reads(void) {
    uint8_t buffer[HOST_SECTOR_SIZE * 2U];
    uint32_t reads;
    uint32_t writes;
    uint32_t initial_reads = ata_get_read_ops();

    kmemset(buffer, 0U, sizeof(buffer));
    CHECK(ata_read_device_sectors(0U, 5U, 2U, buffer) == OK);
    CHECK(buffer[0] == 0U && buffer[1] == 0xA5U);
    CHECK(buffer[HOST_SECTOR_SIZE] == 0U &&
          buffer[HOST_SECTOR_SIZE + 1U] == 0xA6U);
    CHECK(ata_get_read_ops() == initial_reads + 1U);
    CHECK(ata_read_sectors(7U, 1U, buffer) == OK);
    CHECK(ata_read_device_sectors(2U, 9U, 1U, buffer) == OK);
    CHECK(ata_get_device_counters(2U, &reads, &writes) == OK);
    CHECK(reads == 1U && writes == 0U);
    CHECK(ata_read_device_sectors(1U, 0U, 1U, buffer) == ERR_NOT_FOUND);
    CHECK(ata_read_device_sectors(ATA_MAX_DEVICES, 0U, 1U, buffer) == ERR_INVALID);
    CHECK(ata_read_sectors(0U, 0U, buffer) == ERR_NULL);
    CHECK(ata_read_sectors(0U, 1U, 0) == ERR_NULL);
    CHECK(ata_read_sectors(1000U, 1U, buffer) == ERR_DISK);
    CHECK(ata_read_sectors(ATA_LBA28_MAX_SECTORS, 1U, buffer) == ERR_DISK);
    fake_read_commands_remaining = 1U;
    CHECK(ata_read_sectors(11U, 1U, buffer) == OK);
    fake_read_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_read_sectors(12U, 1U, buffer) == ERR_DISK);
    fake_read_status_mode = HOST_STATUS_BUSY;
    CHECK(ata_read_sectors(13U, 1U, buffer) == ERR_DISK);
    fake_read_status_mode = HOST_STATUS_NORMAL;
    fake_ready_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_read_sectors(14U, 1U, buffer) == ERR_DISK);
    fake_ready_status_mode = HOST_STATUS_NORMAL;
    return 0;
}

static int test_writes_and_flush(void) {
    uint8_t buffer[HOST_SECTOR_SIZE];
    uint32_t reads;
    uint32_t writes;
    uint32_t initial_writes = ata_get_write_ops();

    for (uint32_t index = 0U; index < sizeof(buffer); index++) {
        buffer[index] = (uint8_t)index;
    }
    CHECK(ata_write_device_sectors(0U, 2U, 1U, buffer) == OK);
    CHECK(ata_write_sectors(3U, 1U, buffer) == OK);
    CHECK(ata_write_device_sectors(2U, 4U, 1U, buffer) == OK);
    CHECK(ata_get_write_ops() == initial_writes + 3U);
    CHECK(ata_get_device_counters(0U, &reads, &writes) == OK);
    CHECK(writes == 2U);
    CHECK(ata_write_device_sectors(1U, 0U, 1U, buffer) == ERR_NOT_FOUND);
    CHECK(ata_write_device_sectors(ATA_MAX_DEVICES, 0U, 1U, buffer) == ERR_INVALID);
    CHECK(ata_write_sectors(0U, 0U, buffer) == ERR_NULL);
    CHECK(ata_write_sectors(0U, 1U, 0) == ERR_NULL);
    CHECK(ata_write_sectors(1000U, 1U, buffer) == ERR_DISK);
    CHECK(ata_write_sectors(ATA_LBA28_MAX_SECTORS, 1U, buffer) == ERR_DISK);
    fake_write_data_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_write_sectors(5U, 1U, buffer) == ERR_DISK);
    fake_write_data_status_mode = HOST_STATUS_NORMAL;
    fake_ready_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_write_sectors(6U, 1U, buffer) == ERR_DISK);
    fake_ready_status_mode = HOST_STATUS_NORMAL;
    fake_write_complete_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_write_sectors(7U, 1U, buffer) == ERR_DISK);
    fake_write_complete_status_mode = HOST_STATUS_BUSY;
    CHECK(ata_write_sectors(8U, 1U, buffer) == ERR_DISK);
    fake_write_complete_status_mode = HOST_STATUS_NORMAL;
    CHECK(ata_flush_device(0U) == OK);
    CHECK(ata_flush_device(2U) == ERR_UNAVAILABLE);
    CHECK(ata_flush_device(1U) == ERR_NOT_FOUND);
    CHECK(ata_flush_device(ATA_MAX_DEVICES) == ERR_INVALID);
    fake_flush_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_flush_device(0U) == ERR_DISK);
    fake_flush_status_mode = HOST_STATUS_NORMAL;
    fake_ready_status_mode = HOST_STATUS_ERROR;
    CHECK(ata_flush_device(0U) == ERR_DISK);
    fake_ready_status_mode = HOST_STATUS_NORMAL;
    return 0;
}

int main(void) {
    int result;

    result = test_before_init();
    if (result != 0) goto done;
    coverage_active = 1U;
    result = test_inventory();
    if (result != 0) goto done;
    result = test_reads();
    if (result != 0) goto done;
    result = test_writes_and_flush();

done:
    coverage_active = 0U;
    coverage_emit(result);
    if (result == 0) printf("ATA_HOST_TEST_PASS\n");
    return result;
}
