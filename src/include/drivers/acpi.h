#ifndef ACPI_H
#define ACPI_H

#include "types.h"
#include "core/memory.h"

#define ACPI_MAX_TABLES 64U

typedef enum {
    ACPI_ROOT_NONE = 0,
    ACPI_ROOT_RSDT,
    ACPI_ROOT_XSDT
} acpi_root_kind_t;

typedef struct {
    char signature[5];
    char oem_id[7];
    char oem_table_id[9];
    uint32_t physical_address;
    uint32_t length;
    uint8_t revision;
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
} acpi_status_t;

int acpi_init(const mmap_entry_t* memory_map, uint32_t entry_count);
int acpi_get_status(acpi_status_t* out_status);
int acpi_get_table_count(uint32_t* out_count);
int acpi_get_table_at(uint32_t index, acpi_table_info_t* out_table);
int acpi_find_table(const char signature[4], uint32_t occurrence,
                    acpi_table_info_t* out_table);
const char* acpi_root_kind_name(acpi_root_kind_t kind);

#endif
