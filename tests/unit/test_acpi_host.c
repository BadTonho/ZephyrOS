#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/acpi.h"

void acpi_host_reset(void);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_FIRMWARE_SIZE 0x00100000U
#define HOST_RSDP_ADDRESS 0x000E1000U
#define HOST_RSDT_ADDRESS 0x000EF000U
#define HOST_XSDT_ADDRESS 0x000F0000U
#define HOST_FADT_ADDRESS 0x000F1000U
#define HOST_MADT_ADDRESS 0x000F2000U
#define HOST_DSDT_ADDRESS 0x000F3000U
#define HOST_FACS_ADDRESS 0x000F4000U
#define HOST_PM1A_PORT 0x1000U
#define HOST_SMI_PORT 0x00B2U
#define HOST_RESET_PORT 0x0CF9U
#define HOST_FADT_LENGTH 196U
#define HOST_MADT_LENGTH 64U
#define HOST_DSDT_LENGTH 47U
#define HOST_FACS_LENGTH 64U
#define HOST_PM1_SCI_ENABLE 0x0001U
#define HOST_GAS_ACCESS_BYTE 1U
#define HOST_AML_NAME_OP 0x08U
#define HOST_AML_ROOT_PREFIX 0x5CU
#define HOST_AML_PACKAGE_OP 0x12U
#define HOST_AML_ZERO_OP 0x00U
#define HOST_AML_ONE_OP 0x01U

static uint8_t host_firmware[HOST_FIRMWARE_SIZE];
static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint16_t host_io[0x10000U];
static uint32_t host_outb_count;
static uint32_t host_outw_count;
static uint16_t host_last_outb_port;
static uint16_t host_last_outw_port;
static uint8_t host_last_outb_value;
static uint16_t host_last_outw_value;
static uint32_t host_tick;
static uint32_t host_log_count;
static jmp_buf host_halt_jump;
static uint8_t host_halt_called;

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
    printf("ZCOV_BEGIN|case=host:drivers:acpi|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:acpi|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:acpi|value=0x%08X\n",
           (uint32_t)result);
}

const void* acpi_host_resolve_address(uint32_t address) {
    if (address >= HOST_FIRMWARE_SIZE) return 0;
    return &host_firmware[address];
}

uint32_t acpi_host_physical_address(const void* address) {
    const uint8_t* bytes = (const uint8_t*)address;

    if (!bytes || bytes < host_firmware ||
        bytes >= host_firmware + HOST_FIRMWARE_SIZE) return 0U;
    return (uint32_t)(bytes - host_firmware);
}

uint16_t acpi_host_inw(uint16_t port) {
    return host_io[port];
}

void acpi_host_outb(uint16_t port, uint8_t value) {
    host_outb_count++;
    host_last_outb_port = port;
    host_last_outb_value = value;
    if (port == HOST_SMI_PORT) host_io[HOST_PM1A_PORT] = HOST_PM1_SCI_ENABLE;
}

void acpi_host_outw(uint16_t port, uint16_t value) {
    host_outw_count++;
    host_last_outw_port = port;
    host_last_outw_value = value;
    host_io[port] = value;
}

void acpi_host_halt(void) {
    host_halt_called = 1U;
    longjmp(host_halt_jump, 1);
}

uint32_t timer_get_ticks(void) {
    return host_tick++;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    host_log_count++;
}

static void write_u32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static void write_u64(uint8_t* destination, uint64_t value) {
    write_u32(destination, (uint32_t)value);
    write_u32(destination + 4U, (uint32_t)(value >> 32));
}

static void copy_bytes(uint8_t* destination, const uint8_t* source,
                       uint32_t length) {
    for (uint32_t index = 0U; index < length; index++) {
        destination[index] = source[index];
    }
}

static void checksum_fix(uint8_t* bytes, uint32_t length, uint32_t offset) {
    uint8_t sum = 0U;

    bytes[offset] = 0U;
    for (uint32_t index = 0U; index < length; index++) sum += bytes[index];
    bytes[offset] = (uint8_t)(0U - sum);
}

static void table_header(uint8_t* table, const char* signature,
                         uint32_t length, uint8_t revision) {
    for (uint32_t index = 0U; index < length; index++) table[index] = 0U;
    for (uint32_t index = 0U; index < 4U; index++) {
        table[index] = (uint8_t)signature[index];
    }
    write_u32(table + 4U, length);
    table[8] = revision;
    copy_bytes(table + 10U, (const uint8_t*)"ZEPHYR", 6U);
    copy_bytes(table + 16U, (const uint8_t*)"HOSTTEST", 8U);
}

static void build_rsdp(uint32_t root_address, uint8_t revision) {
    uint8_t* rsdp = &host_firmware[HOST_RSDP_ADDRESS];

    for (uint32_t index = 0U; index < 36U; index++) rsdp[index] = 0U;
    copy_bytes(rsdp, (const uint8_t*)"RSD PTR ", 8U);
    copy_bytes(rsdp + 9U, (const uint8_t*)"ZEPHYR", 6U);
    rsdp[15] = revision;
    write_u32(rsdp + 16U, HOST_RSDT_ADDRESS);
    write_u32(rsdp + 20U, 36U);
    write_u64(rsdp + 24U, root_address);
    checksum_fix(rsdp, 20U, 8U);
    checksum_fix(rsdp, 36U, 32U);
}

static void build_fadt(uint8_t invalid_reset) {
    uint8_t* fadt = &host_firmware[HOST_FADT_ADDRESS];

    table_header(fadt, "FACP", HOST_FADT_LENGTH, 6U);
    write_u32(fadt + 36U, HOST_FACS_ADDRESS);
    write_u32(fadt + 40U, HOST_DSDT_ADDRESS);
    write_u32(fadt + 48U, HOST_SMI_PORT);
    fadt[52] = 0xA0U;
    fadt[53] = 0xA1U;
    write_u32(fadt + 64U, HOST_PM1A_PORT);
    fadt[89] = 2U;
    if (!invalid_reset) {
        fadt[116] = ACPI_ADDRESS_SPACE_SYSTEM_IO;
        fadt[117] = 8U;
        fadt[119] = HOST_GAS_ACCESS_BYTE;
        write_u64(fadt + 120U, HOST_RESET_PORT);
        fadt[128] = 6U;
    }
    checksum_fix(fadt, HOST_FADT_LENGTH, 9U);
}

static void build_madt(uint8_t invalid_entry) {
    uint8_t* madt = &host_firmware[HOST_MADT_ADDRESS];

    table_header(madt, "APIC", HOST_MADT_LENGTH, 5U);
    write_u32(madt + 36U, 0xFEE00000U);
    write_u32(madt + 40U, 1U);
    madt[44] = ACPI_MADT_TYPE_LOCAL_APIC;
    madt[45] = invalid_entry ? 1U : ACPI_MADT_LOCAL_APIC_MIN_LENGTH;
    write_u32(madt + 48U, ACPI_MADT_PROCESSOR_ENABLED);
    madt[52] = ACPI_MADT_TYPE_IO_APIC;
    madt[53] = ACPI_MADT_IO_APIC_MIN_LENGTH;
    checksum_fix(madt, HOST_MADT_LENGTH, 9U);
}

static void build_dsdt(uint8_t malformed_package, uint8_t duplicate_s5) {
    uint8_t* dsdt = &host_firmware[HOST_DSDT_ADDRESS];
    uint32_t offset = ACPI_MADT_HEADER_LENGTH - 8U;

    table_header(dsdt, "DSDT", HOST_DSDT_LENGTH, 2U);
    dsdt[offset++] = HOST_AML_NAME_OP;
    dsdt[offset++] = HOST_AML_ROOT_PREFIX;
    copy_bytes(dsdt + offset, (const uint8_t*)"_S5_", 4U);
    offset += 4U;
    dsdt[offset++] = HOST_AML_PACKAGE_OP;
    dsdt[offset++] = malformed_package ? 1U : 4U;
    dsdt[offset++] = 2U;
    dsdt[offset++] = HOST_AML_ZERO_OP;
    dsdt[offset++] = HOST_AML_ONE_OP;
    if (duplicate_s5) {
        dsdt[offset++] = HOST_AML_NAME_OP;
        dsdt[offset++] = HOST_AML_ROOT_PREFIX;
        copy_bytes(dsdt + offset, (const uint8_t*)"_S5_", 4U);
        offset += 4U;
        dsdt[offset++] = HOST_AML_PACKAGE_OP;
        dsdt[offset++] = 4U;
        dsdt[offset++] = 2U;
        dsdt[offset++] = HOST_AML_ZERO_OP;
        dsdt[offset++] = HOST_AML_ONE_OP;
    }
    write_u32(dsdt + 4U, offset);
    checksum_fix(dsdt, offset, 9U);
}

static void build_facs(void) {
    uint8_t* facs = &host_firmware[HOST_FACS_ADDRESS];

    for (uint32_t index = 0U; index < HOST_FACS_LENGTH; index++) facs[index] = 0U;
    copy_bytes(facs, (const uint8_t*)"FACS", 4U);
    write_u32(facs + 4U, HOST_FACS_LENGTH);
}

static void build_root(uint8_t use_xsdt, uint8_t malformed_root) {
    uint8_t* root = &host_firmware[use_xsdt ? HOST_XSDT_ADDRESS :
                                      HOST_RSDT_ADDRESS];
    uint32_t entry_size = use_xsdt ? 8U : 4U;
    uint32_t root_length = 36U + entry_size * 3U;

    table_header(root, use_xsdt ? "XSDT" : "RSDT", root_length, 1U);
    if (use_xsdt) {
        write_u64(root + 36U, HOST_FADT_ADDRESS);
        write_u64(root + 44U, HOST_MADT_ADDRESS);
        write_u64(root + 52U, HOST_DSDT_ADDRESS);
    } else {
        write_u32(root + 36U, HOST_FADT_ADDRESS);
        write_u32(root + 40U, HOST_MADT_ADDRESS);
        write_u32(root + 44U, HOST_DSDT_ADDRESS);
    }
    if (malformed_root) write_u32(root + 4U, root_length + 1U);
    checksum_fix(root, malformed_root ? root_length + 1U : root_length, 9U);
}

static void build_fixture(uint8_t use_xsdt, uint8_t malformed_madt,
                          uint8_t malformed_package, uint8_t duplicate_s5,
                          uint8_t invalid_reset, uint8_t malformed_root) {
    for (uint32_t index = 0U; index < HOST_FIRMWARE_SIZE; index++) {
        host_firmware[index] = 0U;
    }
    build_facs();
    build_dsdt(malformed_package, duplicate_s5);
    build_fadt(invalid_reset);
    build_madt(malformed_madt);
    build_root(use_xsdt, malformed_root);
    build_rsdp(use_xsdt ? HOST_XSDT_ADDRESS : HOST_RSDT_ADDRESS,
               use_xsdt ? 2U : 1U);
}

static mmap_entry_t host_memory_map(void) {
    mmap_entry_t map = {0U, 0U, HOST_FIRMWARE_SIZE, 0U, 1U, 0U};

    return map;
}

static void reset_host_io(void) {
    for (uint32_t index = 0U; index < 0x10000U; index++) host_io[index] = 0U;
    host_outb_count = 0U;
    host_outw_count = 0U;
    host_last_outb_port = 0U;
    host_last_outw_port = 0U;
    host_last_outb_value = 0U;
    host_last_outw_value = 0U;
    host_halt_called = 0U;
}

static int test_preconditions(void) {
    acpi_status_t status;
    acpi_power_info_t power;
    acpi_table_info_t table;
    acpi_madt_info_t madt;
    acpi_madt_entry_t entry;
    uint32_t count;

    acpi_host_reset();
    if (acpi_get_status(0) != ERR_NULL || acpi_get_status(&status) != ERR_STATE) {
        return 1;
    }
    if (acpi_get_power_info(0) != ERR_NULL ||
        acpi_get_power_info(&power) != ERR_STATE) return 2;
    if (acpi_get_table_count(0) != ERR_NULL ||
        acpi_get_table_count(&count) != ERR_STATE) return 3;
    if (acpi_get_table_at(0U, 0) != ERR_NULL ||
        acpi_get_table_at(0U, &table) != ERR_STATE) return 4;
    if (acpi_find_table(0, 0U, &table) != ERR_NULL ||
        acpi_find_table((const char*)"FACP", 0U, 0) != ERR_NULL ||
        acpi_find_table((const char*)"FACP", 0U, &table) != ERR_STATE) return 5;
    if (acpi_get_madt_info(0) != ERR_NULL ||
        acpi_get_madt_info(&madt) != ERR_STATE) return 6;
    if (acpi_get_madt_entry_count(0) != ERR_NULL ||
        acpi_get_madt_entry_count(&count) != ERR_STATE) return 7;
    if (acpi_get_madt_entry_at(0U, 0) != ERR_NULL ||
        acpi_get_madt_entry_at(0U, &entry) != ERR_STATE) return 8;
    if (acpi_reset() != ERR_STATE || acpi_poweroff() != ERR_STATE ||
        acpi_enter_s5() != ERR_STATE) return 9;
    if (acpi_root_kind_name(ACPI_ROOT_NONE)[0] != 'N' ||
        acpi_root_kind_name(ACPI_ROOT_RSDT)[0] != 'R' ||
        acpi_root_kind_name(ACPI_ROOT_XSDT)[0] != 'X') return 10;
    return 0;
}

static int test_initialization_failures(void) {
    mmap_entry_t map = host_memory_map();
    acpi_status_t status;

    acpi_host_reset();
    reset_host_io();
    if (acpi_init(0, 1U) != ERR_NULL) return 20;
    acpi_host_reset();
    if (acpi_init(&map, 0U) != ERR_INVALID) return 21;
    acpi_host_reset();
    map.type = 5U;
    if (acpi_init(&map, 1U) != ERR_NOT_FOUND ||
        acpi_get_status(&status) != OK || status.available) return 22;
    return 0;
}

static int test_valid_xsdt(void) {
    mmap_entry_t map = host_memory_map();
    acpi_status_t status;
    acpi_power_info_t power;
    acpi_madt_info_t madt;
    acpi_madt_entry_t entry;
    acpi_table_info_t table;
    uint32_t count;

    acpi_host_reset();
    reset_host_io();
    build_fixture(1U, 0U, 0U, 0U, 0U, 0U);
    if (acpi_init(&map, 1U) != OK || acpi_get_status(&status) != OK) return 30;
    if (!status.available || status.root_kind != ACPI_ROOT_XSDT ||
        !status.fadt_present || !status.dsdt_present || !status.facs_present ||
        !status.madt_present || status.table_count != 4U) return 31;
    if (acpi_get_power_info(&power) != OK || power.s5_state != ACPI_S5_DECLARED ||
        !power.pm1a_readable || !power.reset_register_valid ||
        !power.s5_transition_ready) return 32;
    if (acpi_get_table_count(&count) != OK || count != 4U ||
        acpi_get_table_at(0U, &table) != OK ||
        acpi_get_table_at(count, &table) != ERR_NOT_FOUND ||
        acpi_find_table((const char*)"FACP", 0U, &table) != OK ||
        acpi_find_table((const char*)"FACP", 1U, &table) != ERR_NOT_FOUND ||
        acpi_find_table((const char*)"NOPE", 0U, &table) != ERR_NOT_FOUND) return 33;
    if (acpi_get_madt_info(&madt) != OK || madt.entry_count != 2U ||
        madt.local_apic_count != 1U || madt.enabled_processor_count != 1U ||
        madt.io_apic_count != 1U || acpi_get_madt_entry_count(&count) != OK ||
        count != 2U || acpi_get_madt_entry_at(0U, &entry) != OK ||
        entry.type != ACPI_MADT_TYPE_LOCAL_APIC ||
        acpi_get_madt_entry_at(count, &entry) != ERR_NOT_FOUND) return 34;
    if (acpi_reset() != ERR_TIMEOUT || host_outb_count == 0U ||
        host_last_outb_port != HOST_RESET_PORT) return 35;
    if (setjmp(host_halt_jump) == 0) {
        host_halt_called = 0U;
        acpi_poweroff();
        return 36;
    }
    if (!host_halt_called || host_outw_count != 1U ||
        host_last_outw_port != HOST_PM1A_PORT ||
        (host_last_outw_value & 0x2000U) == 0U) return 37;
    return 0;
}

static int test_valid_rsdt(void) {
    mmap_entry_t map = host_memory_map();
    acpi_status_t status;
    acpi_power_info_t power;

    acpi_host_reset();
    reset_host_io();
    build_fixture(0U, 0U, 0U, 0U, 1U, 0U);
    if (acpi_init(&map, 1U) != OK || acpi_get_status(&status) != OK ||
        status.root_kind != ACPI_ROOT_RSDT) return 40;
    if (acpi_get_power_info(&power) != OK || !power.reset_register_present ||
        power.reset_register_valid) return 41;
    if (acpi_get_table_count(&(uint32_t){0U}) != OK) return 42;
    return 0;
}

static int test_malformed_tables(void) {
    mmap_entry_t map = host_memory_map();
    acpi_status_t status;
    acpi_power_info_t power;
    acpi_madt_info_t madt;

    acpi_host_reset();
    reset_host_io();
    build_fixture(1U, 1U, 0U, 0U, 0U, 0U);
    if (acpi_init(&map, 1U) != ERR_INVALID || acpi_get_status(&status) != OK ||
        !status.partial || status.madt_present) return 50;
    if (acpi_get_madt_info(&madt) != OK || madt.entry_count != 0U) return 51;
    acpi_host_reset();
    build_fixture(1U, 0U, 1U, 0U, 0U, 0U);
    if (acpi_init(&map, 1U) != OK || acpi_get_power_info(&power) != OK ||
        power.s5_state != ACPI_S5_MALFORMED) return 52;
    acpi_host_reset();
    build_fixture(1U, 0U, 0U, 1U, 0U, 0U);
    if (acpi_init(&map, 1U) != OK || acpi_get_power_info(&power) != OK ||
        power.s5_state != ACPI_S5_AMBIGUOUS || power.s5_transition_ready) return 53;
    acpi_host_reset();
    build_fixture(1U, 0U, 0U, 0U, 0U, 1U);
    if (acpi_init(&map, 1U) != ERR_INVALID || acpi_get_status(&status) != OK ||
        !status.partial) return 54;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    host_tick = 0U;
    host_log_count = 0U;
    if (!result) result = test_preconditions();
    if (!result) result = test_initialization_failures();
    if (!result) result = test_valid_xsdt();
    if (!result) result = test_valid_rsdt();
    if (!result) result = test_malformed_tables();
    coverage_active = 0U;
    if (!result && host_log_count == 0U) result = 90;
    coverage_emit(result);
    if (result) {
        printf("ACPI_HOST_FAIL:%d\n", result);
        return result;
    }
    printf("ACPI_HOST_PASS\n");
    return 0;
}
