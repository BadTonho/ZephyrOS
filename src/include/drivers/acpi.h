#ifndef ACPI_H
#define ACPI_H

#include "types.h"
#include "core/memory.h"

#define ACPI_MAX_TABLES 64U
#define ACPI_MAX_MADT_ENTRIES 64U
#define ACPI_MADT_MAX_ENTRY_LENGTH 255U
#define ACPI_MADT_HEADER_LENGTH 44U
#define ACPI_MADT_ENTRY_HEADER_LENGTH 2U
#define ACPI_MADT_TYPE_LOCAL_APIC 0U
#define ACPI_MADT_TYPE_IO_APIC 1U
#define ACPI_MADT_TYPE_LOCAL_X2APIC 9U
#define ACPI_MADT_LOCAL_APIC_MIN_LENGTH 8U
#define ACPI_MADT_IO_APIC_MIN_LENGTH 12U
#define ACPI_MADT_LOCAL_X2APIC_MIN_LENGTH 16U
#define ACPI_MADT_LOCAL_APIC_ENTRY_FLAGS_OFFSET 4U
#define ACPI_MADT_LOCAL_X2APIC_ENTRY_FLAGS_OFFSET 8U
#define ACPI_MADT_PROCESSOR_ENABLED 0x1U

typedef enum {
    ACPI_ROOT_NONE = 0,
    ACPI_ROOT_RSDT,
    ACPI_ROOT_XSDT
} acpi_root_kind_t;

typedef enum {
    ACPI_ADDRESS_SPACE_SYSTEM_MEMORY = 0,
    ACPI_ADDRESS_SPACE_SYSTEM_IO = 1
} acpi_address_space_t;

typedef enum {
    ACPI_MODE_UNKNOWN = 0,
    ACPI_MODE_DISABLED,
    ACPI_MODE_ENABLED,
    ACPI_MODE_INCONSISTENT
} acpi_mode_t;

typedef enum {
    ACPI_S5_UNAVAILABLE = 0,
    ACPI_S5_DECLARED,
    ACPI_S5_MALFORMED,
    ACPI_S5_AMBIGUOUS
} acpi_s5_state_t;

typedef struct {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} acpi_register_t;

typedef struct {
    char signature[5];
    char oem_id[7];
    char oem_table_id[9];
    uint32_t physical_address;
    uint32_t length;
    uint8_t revision;
    uint8_t checksum_valid;
} acpi_table_info_t;

typedef struct {
    uint8_t initialized;
    uint8_t available;
    uint8_t partial;
    uint8_t revision;
    acpi_root_kind_t root_kind;
    char oem_id[7];
    uint32_t rsdp_address;
    uint32_t root_address;
    uint32_t root_entry_count;
    uint32_t table_count;
    uint32_t malformed_tables;
    uint32_t skipped_tables;
    uint32_t scan_ticks;
    uint8_t fadt_present;
    uint8_t dsdt_present;
    uint8_t facs_present;
    uint32_t fadt_address;
    uint32_t dsdt_address;
    uint32_t facs_address;
    uint32_t rsdp_length;
    uint8_t rsdp_checksum_valid;
    uint8_t madt_present;
    uint32_t madt_address;
} acpi_status_t;

typedef struct {
    uint8_t initialized;
    uint8_t present;
    uint8_t revision;
    uint32_t physical_address;
    uint32_t length;
    uint32_t local_apic_address;
    uint32_t flags;
    uint32_t entry_count;
    uint32_t skipped_entries;
    uint32_t local_apic_count;
    uint32_t enabled_processor_count;
    uint32_t io_apic_count;
} acpi_madt_info_t;

typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t raw[ACPI_MADT_MAX_ENTRY_LENGTH];
} acpi_madt_entry_t;

typedef struct {
    uint8_t initialized;
    uint8_t fadt_power_fields_present;
    uint8_t hardware_reduced;
    uint8_t pm1a_present;
    uint8_t pm1b_present;
    uint8_t pm1a_readable;
    uint8_t pm1b_readable;
    uint8_t pm1_control_length;
    acpi_register_t pm1a_control;
    acpi_register_t pm1b_control;
    uint32_t smi_command_port;
    uint8_t acpi_enable_value;
    uint8_t acpi_disable_value;
    acpi_mode_t mode;
    uint16_t pm1a_value;
    uint16_t pm1b_value;
    acpi_s5_state_t s5_state;
    uint8_t s5_type_a;
    uint8_t s5_type_b;
    uint8_t mode_enable_available;
    uint8_t s5_transition_ready;
    uint32_t s5_candidates;
    acpi_register_t reset_register;
    uint8_t reset_register_present;
    uint8_t reset_register_valid;
    uint8_t reset_value;
} acpi_power_info_t;

int acpi_init(const mmap_entry_t* memory_map, uint32_t entry_count);
int acpi_get_status(acpi_status_t* out_status);
int acpi_get_power_info(acpi_power_info_t* out_info);
int acpi_reset(void);
int acpi_poweroff(void);
int acpi_enter_s5(void);
int acpi_get_table_count(uint32_t* out_count);
int acpi_get_table_at(uint32_t index, acpi_table_info_t* out_table);
int acpi_find_table(const char signature[4], uint32_t occurrence,
                    acpi_table_info_t* out_table);
int acpi_get_madt_info(acpi_madt_info_t* out_info);
int acpi_get_madt_entry_count(uint32_t* out_count);
int acpi_get_madt_entry_at(uint32_t index, acpi_madt_entry_t* out_entry);
const char* acpi_root_kind_name(acpi_root_kind_t kind);

#endif
