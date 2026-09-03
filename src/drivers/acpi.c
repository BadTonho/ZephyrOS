#include "drivers/acpi.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"

#define ACPI_RSDP_ALIGNMENT 16U
#define ACPI_RSDP_V1_LENGTH 20U
#define ACPI_RSDP_V2_LENGTH 36U
#define ACPI_RSDP_MAX_LENGTH 4096U
#define ACPI_RSDP_RSDT_OFFSET 16U
#define ACPI_RSDP_XSDT_OFFSET 24U
#define ACPI_SDT_HEADER_LENGTH 36U
#define ACPI_SDT_LENGTH_OFFSET 4U
#define ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET 40U
#define ACPI_MADT_ENTRY_TYPE_OFFSET 0U
#define ACPI_MADT_ENTRY_LENGTH_OFFSET 1U
#define ACPI_FACS_MIN_LENGTH 64U
#define ACPI_MAX_TABLE_LENGTH 0x00100000U
#define ACPI_MAX_ROOT_ENTRIES 256U
#define ACPI_MAX_ANOMALY_LOGS 8U
#define ACPI_EBDA_POINTER 0x0000040EU
#define ACPI_EBDA_MIN 0x00080000U
#define ACPI_EBDA_LIMIT 0x000A0000U
#define ACPI_EBDA_SEARCH_LENGTH 1024U
#define ACPI_BIOS_SEARCH_START 0x000E0000U
#define ACPI_BIOS_SEARCH_END 0x00100000U
#define ACPI_FADT_FACS_OFFSET 36U
#define ACPI_FADT_DSDT_OFFSET 40U
#define ACPI_FADT_X_FACS_OFFSET 132U
#define ACPI_FADT_X_DSDT_OFFSET 140U
#define ACPI_FADT_X_FACS_END 140U
#define ACPI_FADT_X_DSDT_END 148U
#define ACPI_FADT_SMI_COMMAND_OFFSET 48U
#define ACPI_FADT_ACPI_ENABLE_OFFSET 52U
#define ACPI_FADT_ACPI_DISABLE_OFFSET 53U
#define ACPI_FADT_PM1A_CONTROL_OFFSET 64U
#define ACPI_FADT_PM1B_CONTROL_OFFSET 68U
#define ACPI_FADT_PM1_CONTROL_LENGTH_OFFSET 89U
#define ACPI_FADT_POWER_FIELDS_END 90U
#define ACPI_FADT_FLAGS_OFFSET 112U
#define ACPI_FADT_FLAGS_END 116U
#define ACPI_FADT_RESET_REGISTER_OFFSET 116U
#define ACPI_FADT_RESET_VALUE_OFFSET 128U
#define ACPI_FADT_RESET_FIELDS_END 129U
#define ACPI_FADT_X_PM1A_CONTROL_OFFSET 172U
#define ACPI_FADT_X_PM1B_CONTROL_OFFSET 184U
#define ACPI_FADT_X_PM1A_CONTROL_END 184U
#define ACPI_FADT_X_PM1B_CONTROL_END 196U
#define ACPI_FADT_HW_REDUCED_FLAG (1U << 20)
#define ACPI_GAS_ACCESS_UNDEFINED 0U
#define ACPI_GAS_ACCESS_BYTE 1U
#define ACPI_GAS_ACCESS_WORD 2U
#define ACPI_PM1_CONTROL_MIN_WIDTH 16U
#define ACPI_PM1_SCI_ENABLE 0x0001U
#define ACPI_PM1_SLEEP_TYPE_MASK 0x1C00U
#define ACPI_PM1_SLEEP_TYPE_SHIFT 10U
#define ACPI_PM1_SLEEP_ENABLE 0x2000U
#define ACPI_IO_WORD_MAX_PORT 0xFFFEU
#define ACPI_IO_BYTE_MAX_PORT 0xFFFFU
#define ACPI_MODE_ENABLE_POLL_LIMIT 1000000U
#define ACPI_RESET_RETURN_POLL_LIMIT 1000000U
#define ACPI_AML_NAME_OP 0x08U
#define ACPI_AML_ROOT_PREFIX 0x5CU
#define ACPI_AML_PACKAGE_OP 0x12U
#define ACPI_AML_ZERO_OP 0x00U
#define ACPI_AML_ONE_OP 0x01U
#define ACPI_AML_ONES_OP 0xFFU
#define ACPI_AML_BYTE_PREFIX 0x0AU
#define ACPI_AML_WORD_PREFIX 0x0BU
#define ACPI_AML_DWORD_PREFIX 0x0CU
#define ACPI_AML_QWORD_PREFIX 0x0EU
#define ACPI_AML_PACKAGE_FOLLOW_MASK 0xC0U
#define ACPI_AML_PACKAGE_FOLLOW_SHIFT 6U
#define ACPI_AML_PACKAGE_SHORT_MASK 0x3FU
#define ACPI_AML_PACKAGE_LONG_MASK 0x0FU
#define ACPI_AML_MAX_PACKAGE_FOLLOW 3U
#define ACPI_S5_MIN_ELEMENTS 2U
#define ACPI_S5_MAX_ELEMENTS 4U
#define ACPI_S5_TYPE_MAX 7U

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) acpi_rsdp_v1_t;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

static acpi_status_t acpi_status;
static acpi_power_info_t acpi_power_info;
static acpi_table_info_t acpi_tables[ACPI_MAX_TABLES];
static acpi_madt_info_t acpi_madt_info;
static acpi_madt_entry_t acpi_madt_entries[ACPI_MAX_MADT_ENTRIES];
static const mmap_entry_t* acpi_memory_map = 0;
static uint32_t acpi_memory_map_count = 0;
static uint32_t acpi_anomaly_logs = 0;
static int acpi_initialized = 0;
static int acpi_init_result = ERR_STATE;

#if defined(ZEPHYROS_HOST_TEST)
extern const void* acpi_host_resolve_address(uint32_t address);
extern uint32_t acpi_host_physical_address(const void* address);
extern uint16_t acpi_host_inw(uint16_t port);
extern void acpi_host_outb(uint16_t port, uint8_t value);
extern void acpi_host_outw(uint16_t port, uint16_t value);
extern void acpi_host_halt(void) __attribute__((noreturn));
#endif

static const void* acpi_resolve_address(uint32_t address) {
#if defined(ZEPHYROS_HOST_TEST)
    return acpi_host_resolve_address(address);
#else
    return (const void*)address;
#endif
}

static uint16_t acpi_read_u16(const void* address) {
    const uint8_t* bytes = (const uint8_t*)address;
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t acpi_read_u32(const void* address) {
    const uint8_t* bytes = (const uint8_t*)address;
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t acpi_read_u64(const void* address) {
    const uint8_t* bytes = (const uint8_t*)address;
    return (uint64_t)acpi_read_u32(bytes) |
           ((uint64_t)acpi_read_u32(bytes + 4) << 32);
}

static inline uint16_t acpi_inw(uint16_t port) {
#if defined(ZEPHYROS_HOST_TEST)
    return acpi_host_inw(port);
#else
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
#endif
}

static inline void acpi_outb(uint16_t port, uint8_t value) {
#if defined(ZEPHYROS_HOST_TEST)
    acpi_host_outb(port, value);
#else
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
#endif
}

static inline void acpi_outw(uint16_t port, uint16_t value) {
#if defined(ZEPHYROS_HOST_TEST)
    acpi_host_outw(port, value);
#else
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
#endif
}

static int acpi_signature_equal(const char* left, const char* right,
                                uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        if (left[i] != right[i]) return 0;
    }
    return 1;
}

static int acpi_checksum_valid(const void* address, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)address;
    uint8_t checksum = 0;

    for (uint32_t i = 0; i < length; i++) checksum += bytes[i];
    return checksum == 0;
}

static int acpi_e820_type_readable(uint32_t type) {
    return type >= 1U && type <= 4U;
}

static int acpi_range_in_memory_map(uint32_t address, uint32_t length) {
    uint64_t cursor = address;
    uint64_t end = cursor + length;

    if (!length || end <= cursor || end > 0x100000000ULL) return 0;
    while (cursor < end) {
        uint64_t covered_end = cursor;

        for (uint32_t i = 0; i < acpi_memory_map_count; i++) {
            const mmap_entry_t* entry = &acpi_memory_map[i];
            uint64_t base = ((uint64_t)entry->base_high << 32) |
                            entry->base_low;
            uint64_t size = ((uint64_t)entry->length_high << 32) |
                            entry->length_low;
            uint64_t entry_end = base + size;

            if (!acpi_e820_type_readable(entry->type) ||
                entry_end <= base || base > cursor ||
                entry_end <= cursor) continue;
            if (entry_end > covered_end) covered_end = entry_end;
        }
        if (covered_end == cursor) return 0;
        cursor = covered_end < end ? covered_end : end;
    }
    return 1;
}

static void acpi_copy_text(char* destination, const char* source,
                           uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        uint8_t value = (uint8_t)source[i];
        destination[i] = value >= 32U && value <= 126U ?
                         source[i] : '?';
    }
    destination[length] = '\0';
}

static void acpi_note_partial(int error_code) {
    acpi_status.partial = 1;
    if (error_code == ERR_OVERFLOW ||
        (error_code == ERR_INVALID && acpi_init_result != ERR_OVERFLOW) ||
        acpi_init_result == OK) {
        acpi_init_result = error_code;
    }
}

static void acpi_log_anomaly(const char* message) {
    if (acpi_anomaly_logs < ACPI_MAX_ANOMALY_LOGS) {
        LOG_WARN("ACPI", message);
    }
    acpi_anomaly_logs++;
}

static const acpi_rsdp_v1_t* acpi_find_rsdp_range(uint32_t start,
                                                  uint32_t end) {
    for (uint32_t address = start;
         address + ACPI_RSDP_V1_LENGTH <= end;
         address += ACPI_RSDP_ALIGNMENT) {
        const acpi_rsdp_v1_t* rsdp;
        uint32_t length;

        if (!acpi_range_in_memory_map(address, ACPI_RSDP_V1_LENGTH)) continue;
        rsdp = (const acpi_rsdp_v1_t*)acpi_resolve_address(address);
        if (!acpi_signature_equal(rsdp->signature, "RSD PTR ", 8U)) continue;
        if (!acpi_checksum_valid(rsdp, ACPI_RSDP_V1_LENGTH)) {
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
            continue;
        }
        if (rsdp->revision < 2U) {
            acpi_status.rsdp_length = ACPI_RSDP_V1_LENGTH;
            acpi_status.rsdp_checksum_valid = 1;
            return rsdp;
        }
        if (!acpi_range_in_memory_map(address, ACPI_RSDP_V2_LENGTH)) {
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
            continue;
        }
        length = acpi_read_u32((const uint8_t*)rsdp + 20U);
        if (length < ACPI_RSDP_V2_LENGTH ||
            length > ACPI_RSDP_MAX_LENGTH ||
            !acpi_range_in_memory_map(address, length) ||
            !acpi_checksum_valid(rsdp, length)) {
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
            continue;
        }
        acpi_status.rsdp_length = length;
        acpi_status.rsdp_checksum_valid = 1;
        return rsdp;
    }
    return 0;
}

static const acpi_rsdp_v1_t* acpi_find_rsdp(void) {
    uint32_t ebda = (uint32_t)acpi_read_u16(
                        acpi_resolve_address(ACPI_EBDA_POINTER))
                    << 4;
    const acpi_rsdp_v1_t* rsdp = 0;

    if (ebda >= ACPI_EBDA_MIN &&
        ebda <= ACPI_EBDA_LIMIT - ACPI_EBDA_SEARCH_LENGTH) {
        rsdp = acpi_find_rsdp_range(ebda,
                                    ebda + ACPI_EBDA_SEARCH_LENGTH);
    }
    if (rsdp) return rsdp;
    return acpi_find_rsdp_range(ACPI_BIOS_SEARCH_START,
                                ACPI_BIOS_SEARCH_END);
}

static const acpi_sdt_header_t* acpi_validate_sdt(uint32_t address,
                                                  const char* signature) {
    const acpi_sdt_header_t* header;
    uint32_t length;

    if (!address ||
        !acpi_range_in_memory_map(address, ACPI_SDT_HEADER_LENGTH)) {
        acpi_log_anomaly("Endereco de tabela fora do mapa E820");
        return 0;
    }
    header = (const acpi_sdt_header_t*)acpi_resolve_address(address);
    if (signature &&
        !acpi_signature_equal(header->signature, signature, 4U)) {
        acpi_log_anomaly("Assinatura de tabela ACPI invalida");
        return 0;
    }
    length = acpi_read_u32((const uint8_t*)header +
                           ACPI_SDT_LENGTH_OFFSET);
    if (length < ACPI_SDT_HEADER_LENGTH ||
        length > ACPI_MAX_TABLE_LENGTH ||
        !acpi_range_in_memory_map(address, length)) {
        acpi_log_anomaly("Comprimento de tabela ACPI invalido");
        return 0;
    }
    if (!acpi_checksum_valid(header, length)) {
        acpi_log_anomaly("Checksum de tabela ACPI invalido");
        return 0;
    }
    return header;
}

static int acpi_table_already_stored(uint32_t address) {
    for (uint32_t i = 0; i < acpi_status.table_count; i++) {
        if (acpi_tables[i].physical_address == address) return 1;
    }
    return 0;
}

static void acpi_store_table(uint32_t address,
                             const acpi_sdt_header_t* header) {
    acpi_table_info_t* table;

    if (acpi_table_already_stored(address)) return;
    if (acpi_status.table_count >= ACPI_MAX_TABLES) {
        acpi_status.skipped_tables++;
        acpi_note_partial(ERR_OVERFLOW);
        return;
    }
    table = &acpi_tables[acpi_status.table_count++];
    kmemset(table, 0, sizeof(*table));
    acpi_copy_text(table->signature, header->signature, 4U);
    acpi_copy_text(table->oem_id, header->oem_id, 6U);
    acpi_copy_text(table->oem_table_id, header->oem_table_id, 8U);
    table->physical_address = address;
    table->length = acpi_read_u32((const uint8_t*)header +
                                  ACPI_SDT_LENGTH_OFFSET);
    table->revision = header->revision;
    table->checksum_valid = 1;
}

static int acpi_madt_entry_length_valid(uint8_t type, uint8_t length) {
    if (length < ACPI_MADT_ENTRY_HEADER_LENGTH) return 0;
    if (type == ACPI_MADT_TYPE_LOCAL_APIC &&
        length < ACPI_MADT_LOCAL_APIC_MIN_LENGTH) return 0;
    if (type == ACPI_MADT_TYPE_IO_APIC &&
        length < ACPI_MADT_IO_APIC_MIN_LENGTH) return 0;
    if (type == ACPI_MADT_TYPE_LOCAL_X2APIC &&
        length < ACPI_MADT_LOCAL_X2APIC_MIN_LENGTH) return 0;
    return 1;
}

static void acpi_madt_count_entry(const acpi_madt_entry_t* entry) {
    uint32_t flags;

    if (entry->type == ACPI_MADT_TYPE_IO_APIC) {
        acpi_madt_info.io_apic_count++;
        return;
    }
    if (entry->type != ACPI_MADT_TYPE_LOCAL_APIC &&
        entry->type != ACPI_MADT_TYPE_LOCAL_X2APIC) return;

    acpi_madt_info.local_apic_count++;
    flags = entry->type == ACPI_MADT_TYPE_LOCAL_APIC ?
            acpi_read_u32(entry->raw +
                          ACPI_MADT_LOCAL_APIC_ENTRY_FLAGS_OFFSET) :
            acpi_read_u32(entry->raw +
                          ACPI_MADT_LOCAL_X2APIC_ENTRY_FLAGS_OFFSET);
    if (flags & ACPI_MADT_PROCESSOR_ENABLED) {
        acpi_madt_info.enabled_processor_count++;
    }
}

static int acpi_parse_madt(uint32_t address,
                           const acpi_sdt_header_t* madt) {
    const uint8_t* bytes = (const uint8_t*)madt;
    uint32_t length = acpi_read_u32(bytes + ACPI_SDT_LENGTH_OFFSET);
    uint32_t cursor = ACPI_MADT_HEADER_LENGTH;
    int result = OK;

    acpi_madt_info.present = 1;
    acpi_madt_info.physical_address = address;
    acpi_madt_info.length = length;
    acpi_madt_info.revision = madt->revision;
    if (length < ACPI_MADT_HEADER_LENGTH) {
        acpi_madt_info.skipped_entries++;
        acpi_note_partial(ERR_INVALID);
        acpi_log_anomaly("MADT menor que o cabecalho obrigatorio");
        LOG_WARN("ACPI", "MADT rejeitada por comprimento minimo");
        return ERR_INVALID;
    }
    acpi_madt_info.local_apic_address = acpi_read_u32(bytes + 36U);
    acpi_madt_info.flags = acpi_read_u32(bytes +
                                         ACPI_MADT_LOCAL_APIC_FLAGS_OFFSET);
    while (cursor < length) {
        uint32_t remaining = length - cursor;
        uint8_t type;
        uint8_t entry_length;
        acpi_madt_entry_t* entry;

        if (remaining < ACPI_MADT_ENTRY_HEADER_LENGTH) {
            acpi_madt_info.skipped_entries++;
            acpi_note_partial(ERR_INVALID);
            acpi_log_anomaly("MADT possui bytes residuais invalidos");
            return ERR_INVALID;
        }
        type = bytes[cursor + ACPI_MADT_ENTRY_TYPE_OFFSET];
        entry_length = bytes[cursor + ACPI_MADT_ENTRY_LENGTH_OFFSET];
        if (entry_length > remaining || entry_length <
            ACPI_MADT_ENTRY_HEADER_LENGTH) {
            acpi_madt_info.skipped_entries++;
            acpi_note_partial(ERR_INVALID);
            acpi_log_anomaly("Entrada MADT possui comprimento invalido");
            return ERR_INVALID;
        }
        if (!acpi_madt_entry_length_valid(type, entry_length)) {
            acpi_madt_info.skipped_entries++;
            acpi_note_partial(ERR_INVALID);
            acpi_log_anomaly("Entrada MADT possui comprimento insuficiente");
            result = ERR_INVALID;
            cursor += entry_length;
            continue;
        }
        if (acpi_madt_info.entry_count >= ACPI_MAX_MADT_ENTRIES) {
            acpi_madt_info.skipped_entries++;
            acpi_note_partial(ERR_OVERFLOW);
            cursor += entry_length;
            continue;
        }
        entry = &acpi_madt_entries[acpi_madt_info.entry_count++];
        kmemset(entry, 0, sizeof(*entry));
        entry->type = type;
        entry->length = entry_length;
        for (uint32_t index = 0; index < entry_length; index++) {
            entry->raw[index] = bytes[cursor + index];
        }
        acpi_madt_count_entry(entry);
        cursor += entry_length;
    }
    return result;
}

static int acpi_select_root(const acpi_rsdp_v1_t* rsdp,
                            const acpi_sdt_header_t** out_root) {
    uint32_t rsdt_address = acpi_read_u32((const uint8_t*)rsdp +
                                          ACPI_RSDP_RSDT_OFFSET);

    if (rsdp->revision >= 2U) {
        uint64_t xsdt_address = acpi_read_u64((const uint8_t*)rsdp +
                                              ACPI_RSDP_XSDT_OFFSET);

        if (xsdt_address && (xsdt_address >> 32) == 0) {
            *out_root = acpi_validate_sdt((uint32_t)xsdt_address, "XSDT");
            if (*out_root) {
                acpi_status.root_kind = ACPI_ROOT_XSDT;
                acpi_status.root_address = (uint32_t)xsdt_address;
                return OK;
            }
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
        } else if (xsdt_address) {
            acpi_status.skipped_tables++;
            acpi_note_partial(ERR_OVERFLOW);
        }
    }
    *out_root = acpi_validate_sdt(rsdt_address, "RSDT");
    if (!*out_root) {
        LOG_ERROR("ACPI", "Nenhuma tabela raiz ACPI valida");
        return ERR_INVALID;
    }
    acpi_status.root_kind = ACPI_ROOT_RSDT;
    acpi_status.root_address = rsdt_address;
    return OK;
}

static void acpi_parse_root(const acpi_sdt_header_t* root,
                            uint32_t* out_fadt_address) {
    uint32_t entry_size = acpi_status.root_kind == ACPI_ROOT_XSDT ? 8U : 4U;
    uint32_t root_length = acpi_read_u32((const uint8_t*)root +
                                         ACPI_SDT_LENGTH_OFFSET);
    uint32_t payload_length = root_length - ACPI_SDT_HEADER_LENGTH;
    uint32_t entry_count = payload_length / entry_size;
    uint32_t scan_count = entry_count;
    const uint8_t* entries = (const uint8_t*)root +
                             ACPI_SDT_HEADER_LENGTH;

    acpi_status.root_entry_count = entry_count;
    if ((payload_length % entry_size) != 0) {
        acpi_status.malformed_tables++;
        acpi_note_partial(ERR_INVALID);
    }
    if (scan_count > ACPI_MAX_ROOT_ENTRIES) {
        acpi_status.skipped_tables += scan_count - ACPI_MAX_ROOT_ENTRIES;
        scan_count = ACPI_MAX_ROOT_ENTRIES;
        acpi_note_partial(ERR_OVERFLOW);
    }

    for (uint32_t i = 0; i < scan_count; i++) {
        uint64_t physical = entry_size == 8U ?
                            acpi_read_u64(entries + i * entry_size) :
                            acpi_read_u32(entries + i * entry_size);
        const acpi_sdt_header_t* table;

        if (!physical || (physical >> 32) != 0) {
            acpi_status.skipped_tables++;
            acpi_note_partial(physical ? ERR_OVERFLOW : ERR_INVALID);
            continue;
        }
        table = acpi_validate_sdt((uint32_t)physical, 0);
        if (!table) {
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
            continue;
        }
        if (!*out_fadt_address &&
            acpi_signature_equal(table->signature, "FACP", 4U)) {
            *out_fadt_address = (uint32_t)physical;
        }
        if (acpi_signature_equal(table->signature, "APIC", 4U)) {
            if (!acpi_status.madt_present) {
                int madt_result;

                kmemset(&acpi_madt_info, 0, sizeof(acpi_madt_info));
                madt_result = acpi_parse_madt((uint32_t)physical, table);
                if (madt_result == OK) {
                    acpi_status.madt_present = 1;
                    acpi_status.madt_address = (uint32_t)physical;
                } else {
                    acpi_status.malformed_tables++;
                    kmemset(&acpi_madt_info, 0, sizeof(acpi_madt_info));
                }
            } else {
                acpi_log_anomaly("Mais de uma tabela MADT foi encontrada");
            }
        }
        acpi_store_table((uint32_t)physical, table);
    }
}

static uint32_t acpi_choose_32bit_address(const uint8_t* fadt,
                                          uint32_t length,
                                          uint32_t legacy_offset,
                                          uint32_t extended_offset,
                                          uint32_t extended_end) {
    uint32_t legacy = acpi_read_u32(fadt + legacy_offset);

    if (length >= extended_end) {
        uint64_t extended = acpi_read_u64(fadt + extended_offset);

        if (extended && (extended >> 32) == 0) return (uint32_t)extended;
        if (extended) {
            acpi_status.skipped_tables++;
            acpi_note_partial(ERR_OVERFLOW);
        }
    }
    return legacy;
}

static int acpi_gas_valid(const acpi_register_t* reg) {
    uint16_t bit_end;

    if (!reg->address || !reg->register_bit_width ||
        reg->access_size > 4U) return 0;
    bit_end = (uint16_t)reg->register_bit_offset +
              reg->register_bit_width;
    return bit_end <= 64U;
}

static void acpi_read_gas(const uint8_t* bytes, acpi_register_t* out_reg) {
    out_reg->address_space_id = bytes[0];
    out_reg->register_bit_width = bytes[1];
    out_reg->register_bit_offset = bytes[2];
    out_reg->access_size = bytes[3];
    out_reg->address = acpi_read_u64(bytes + 4U);
}

static void acpi_set_legacy_register(uint32_t address,
                                     acpi_register_t* out_reg) {
    uint8_t length = acpi_power_info.pm1_control_length;

    out_reg->address_space_id = ACPI_ADDRESS_SPACE_SYSTEM_IO;
    out_reg->register_bit_width =
        length <= 8U ? (uint8_t)(length * 8U) : 0U;
    out_reg->register_bit_offset = 0;
    out_reg->access_size = ACPI_GAS_ACCESS_UNDEFINED;
    out_reg->address = address;
}

static void acpi_select_pm_register(const uint8_t* fadt, uint32_t length,
                                    uint32_t legacy_offset,
                                    uint32_t extended_offset,
                                    uint32_t extended_end,
                                    acpi_register_t* out_reg,
                                    uint8_t* out_present) {
    acpi_register_t extended;
    uint32_t legacy = 0;

    kmemset(out_reg, 0, sizeof(*out_reg));
    *out_present = 0;
    if (length >= extended_end) {
        acpi_read_gas(fadt + extended_offset, &extended);
        if (acpi_gas_valid(&extended)) {
            *out_reg = extended;
            *out_present = 1;
            return;
        }
        if (extended.address) {
            acpi_log_anomaly("GAS PM1 invalido; tentando campo legado");
        }
    }
    if (length >= legacy_offset + sizeof(uint32_t)) {
        legacy = acpi_read_u32(fadt + legacy_offset);
    }
    if (!legacy) return;
    acpi_set_legacy_register(legacy, out_reg);
    *out_present = 1;
}

static int acpi_pm_register_readable(const acpi_register_t* reg,
                                     uint8_t present) {
    if (!present ||
        reg->address_space_id != ACPI_ADDRESS_SPACE_SYSTEM_IO ||
        reg->address > ACPI_IO_WORD_MAX_PORT ||
        reg->register_bit_offset != 0 ||
        reg->register_bit_width < ACPI_PM1_CONTROL_MIN_WIDTH) {
        return 0;
    }
    return reg->access_size == ACPI_GAS_ACCESS_UNDEFINED ||
           reg->access_size == ACPI_GAS_ACCESS_WORD;
}

static int acpi_reset_register_valid(const acpi_register_t* reg) {
    if (!reg || reg->address_space_id != ACPI_ADDRESS_SPACE_SYSTEM_IO ||
        !reg->address || reg->address > ACPI_IO_BYTE_MAX_PORT ||
        reg->register_bit_width != 8U || reg->register_bit_offset != 0U) {
        return 0;
    }
    return reg->access_size == ACPI_GAS_ACCESS_UNDEFINED ||
           reg->access_size == ACPI_GAS_ACCESS_BYTE;
}

static void acpi_parse_fadt_reset(const uint8_t* fadt, uint32_t length) {
    acpi_register_t reset_register;

    kmemset(&acpi_power_info.reset_register, 0,
            sizeof(acpi_power_info.reset_register));
    acpi_power_info.reset_register_present = 0U;
    acpi_power_info.reset_register_valid = 0U;
    acpi_power_info.reset_value = 0U;
    if (length < ACPI_FADT_RESET_FIELDS_END) return;
    acpi_read_gas(fadt + ACPI_FADT_RESET_REGISTER_OFFSET, &reset_register);
    acpi_power_info.reset_register = reset_register;
    acpi_power_info.reset_value = fadt[ACPI_FADT_RESET_VALUE_OFFSET];
    acpi_power_info.reset_register_present = 1U;
    if (!acpi_reset_register_valid(&reset_register)) {
        acpi_log_anomaly("RESET_REG ACPI invalido para i386");
        return;
    }
    acpi_power_info.reset_register_valid = 1U;
}

static void acpi_observe_pm1_mode(void) {
    uint8_t pm1a_enabled;
    uint8_t pm1b_enabled;

    acpi_power_info.mode = ACPI_MODE_UNKNOWN;
    if (acpi_power_info.hardware_reduced ||
        !acpi_power_info.pm1a_readable) return;
    if (acpi_power_info.pm1b_present &&
        !acpi_power_info.pm1b_readable) return;

    acpi_power_info.pm1a_value =
        acpi_inw((uint16_t)acpi_power_info.pm1a_control.address);
    pm1a_enabled =
        (acpi_power_info.pm1a_value & ACPI_PM1_SCI_ENABLE) != 0;
    if (!acpi_power_info.pm1b_present) {
        acpi_power_info.mode = pm1a_enabled ?
                               ACPI_MODE_ENABLED : ACPI_MODE_DISABLED;
        return;
    }
    acpi_power_info.pm1b_value =
        acpi_inw((uint16_t)acpi_power_info.pm1b_control.address);
    pm1b_enabled =
        (acpi_power_info.pm1b_value & ACPI_PM1_SCI_ENABLE) != 0;
    if (pm1a_enabled != pm1b_enabled) {
        acpi_power_info.mode = ACPI_MODE_INCONSISTENT;
        return;
    }
    acpi_power_info.mode = pm1a_enabled ?
                           ACPI_MODE_ENABLED : ACPI_MODE_DISABLED;
}

static void acpi_parse_fadt_power(const uint8_t* fadt, uint32_t length) {
    if (length < ACPI_FADT_POWER_FIELDS_END) return;

    acpi_power_info.fadt_power_fields_present = 1;
    acpi_power_info.smi_command_port =
        acpi_read_u32(fadt + ACPI_FADT_SMI_COMMAND_OFFSET);
    acpi_power_info.acpi_enable_value =
        fadt[ACPI_FADT_ACPI_ENABLE_OFFSET];
    acpi_power_info.acpi_disable_value =
        fadt[ACPI_FADT_ACPI_DISABLE_OFFSET];
    acpi_power_info.pm1_control_length =
        fadt[ACPI_FADT_PM1_CONTROL_LENGTH_OFFSET];
    if (length >= ACPI_FADT_FLAGS_END) {
        uint32_t flags = acpi_read_u32(fadt + ACPI_FADT_FLAGS_OFFSET);
        acpi_power_info.hardware_reduced =
            (flags & ACPI_FADT_HW_REDUCED_FLAG) != 0;
    }

    acpi_select_pm_register(fadt, length,
                            ACPI_FADT_PM1A_CONTROL_OFFSET,
                            ACPI_FADT_X_PM1A_CONTROL_OFFSET,
                            ACPI_FADT_X_PM1A_CONTROL_END,
                            &acpi_power_info.pm1a_control,
                            &acpi_power_info.pm1a_present);
    acpi_select_pm_register(fadt, length,
                            ACPI_FADT_PM1B_CONTROL_OFFSET,
                            ACPI_FADT_X_PM1B_CONTROL_OFFSET,
                            ACPI_FADT_X_PM1B_CONTROL_END,
                            &acpi_power_info.pm1b_control,
                            &acpi_power_info.pm1b_present);
    acpi_power_info.pm1a_readable =
        acpi_pm_register_readable(&acpi_power_info.pm1a_control,
                                  acpi_power_info.pm1a_present);
    acpi_power_info.pm1b_readable =
        acpi_pm_register_readable(&acpi_power_info.pm1b_control,
                                  acpi_power_info.pm1b_present);
    acpi_parse_fadt_reset(fadt, length);
    if (acpi_power_info.hardware_reduced) {
        acpi_power_info.pm1a_readable = 0;
        acpi_power_info.pm1b_readable = 0;
    }
    acpi_observe_pm1_mode();
}

static int acpi_aml_package_length(const uint8_t* bytes,
                                   const uint8_t* end,
                                   uint32_t* out_length,
                                   uint32_t* out_encoded_length) {
    uint32_t following;
    uint32_t length;

    if (!bytes || !out_length || !out_encoded_length || bytes >= end) {
        LOG_WARN("ACPI", "PkgLength AML com limites invalidos");
        return ERR_INVALID;
    }
    following = (bytes[0] & ACPI_AML_PACKAGE_FOLLOW_MASK) >>
                ACPI_AML_PACKAGE_FOLLOW_SHIFT;
    if (following > ACPI_AML_MAX_PACKAGE_FOLLOW ||
        (uint32_t)(end - bytes) < following + 1U) {
        LOG_WARN("ACPI", "PkgLength AML truncado");
        return ERR_INVALID;
    }
    length = following ? bytes[0] & ACPI_AML_PACKAGE_LONG_MASK :
                         bytes[0] & ACPI_AML_PACKAGE_SHORT_MASK;
    for (uint32_t i = 0; i < following; i++) {
        length |= (uint32_t)bytes[i + 1U] << (4U + i * 8U);
    }
    if (length < following + 1U) {
        LOG_WARN("ACPI", "PkgLength AML menor que sua codificacao");
        return ERR_INVALID;
    }
    *out_length = length;
    *out_encoded_length = following + 1U;
    return OK;
}

static int acpi_aml_integer(const uint8_t* bytes, const uint8_t* end,
                            uint64_t* out_value, uint32_t* out_length) {
    uint32_t width;

    if (!bytes || !out_value || !out_length || bytes >= end) {
        LOG_WARN("ACPI", "Inteiro AML com limites invalidos");
        return ERR_INVALID;
    }
    if (bytes[0] == ACPI_AML_ZERO_OP || bytes[0] == ACPI_AML_ONE_OP ||
        bytes[0] == ACPI_AML_ONES_OP) {
        *out_value = bytes[0] == ACPI_AML_ZERO_OP ? 0U :
                     bytes[0] == ACPI_AML_ONE_OP ? 1U :
                     0xFFFFFFFFFFFFFFFFULL;
        *out_length = 1U;
        return OK;
    }
    if (bytes[0] == ACPI_AML_BYTE_PREFIX) width = 1U;
    else if (bytes[0] == ACPI_AML_WORD_PREFIX) width = 2U;
    else if (bytes[0] == ACPI_AML_DWORD_PREFIX) width = 4U;
    else if (bytes[0] == ACPI_AML_QWORD_PREFIX) width = 8U;
    else {
        LOG_WARN("ACPI", "Tipo inteiro AML nao suportado em _S5_");
        return ERR_INVALID;
    }
    if ((uint32_t)(end - bytes) < width + 1U) {
        LOG_WARN("ACPI", "Inteiro AML truncado em _S5_");
        return ERR_INVALID;
    }

    *out_value = 0;
    for (uint32_t i = 0; i < width; i++) {
        *out_value |= (uint64_t)bytes[i + 1U] << (i * 8U);
    }
    *out_length = width + 1U;
    return OK;
}

static int acpi_parse_s5_package(const uint8_t* package,
                                 const uint8_t* end,
                                 uint8_t* out_type_a,
                                 uint8_t* out_type_b) {
    uint32_t package_length;
    uint32_t encoded_length;
    uint32_t integer_length;
    uint32_t element_count;
    const uint8_t* cursor;
    const uint8_t* package_end;
    uint64_t type_a;
    uint64_t type_b;
    uint64_t ignored_value;

    if (!package || package >= end ||
        package[0] != ACPI_AML_PACKAGE_OP) {
        LOG_WARN("ACPI", "_S5_ sem PackageOp valido");
        return ERR_INVALID;
    }
    if (acpi_aml_package_length(package + 1U, end, &package_length,
                                &encoded_length) != OK ||
        package_length > (uint32_t)(end - (package + 1U))) {
        LOG_WARN("ACPI", "Pacote _S5_ excede os limites da DSDT");
        return ERR_INVALID;
    }
    package_end = package + 1U + package_length;
    cursor = package + 1U + encoded_length;
    if (cursor >= package_end || cursor[0] < ACPI_S5_MIN_ELEMENTS ||
        cursor[0] > ACPI_S5_MAX_ELEMENTS) {
        LOG_WARN("ACPI", "Quantidade de elementos _S5_ invalida");
        return ERR_INVALID;
    }
    element_count = cursor[0];
    cursor++;
    if (acpi_aml_integer(cursor, package_end, &type_a,
                         &integer_length) != OK) {
        LOG_WARN("ACPI", "Primeiro tipo _S5_ invalido");
        return ERR_INVALID;
    }
    cursor += integer_length;
    if (acpi_aml_integer(cursor, package_end, &type_b,
                         &integer_length) != OK) {
        LOG_WARN("ACPI", "Segundo tipo _S5_ invalido");
        return ERR_INVALID;
    }
    cursor += integer_length;
    for (uint32_t i = ACPI_S5_MIN_ELEMENTS; i < element_count; i++) {
        if (acpi_aml_integer(cursor, package_end, &ignored_value,
                             &integer_length) != OK) {
            LOG_WARN("ACPI", "Elemento reservado _S5_ invalido");
            return ERR_INVALID;
        }
        cursor += integer_length;
    }
    if (cursor != package_end) {
        LOG_WARN("ACPI", "Pacote _S5_ contem dados residuais");
        return ERR_INVALID;
    }
    if (type_a > ACPI_S5_TYPE_MAX || type_b > ACPI_S5_TYPE_MAX) {
        LOG_WARN("ACPI", "Tipo _S5_ fora do intervalo seguro");
        return ERR_INVALID;
    }
    *out_type_a = (uint8_t)type_a;
    *out_type_b = (uint8_t)type_b;
    return OK;
}

static void acpi_parse_s5(const acpi_sdt_header_t* dsdt) {
    const uint8_t* body = (const uint8_t*)dsdt + ACPI_SDT_HEADER_LENGTH;
    uint32_t length = acpi_read_u32((const uint8_t*)dsdt +
                                    ACPI_SDT_LENGTH_OFFSET);
    uint32_t body_length = length - ACPI_SDT_HEADER_LENGTH;
    uint32_t valid_count = 0;

    for (uint32_t i = 0; i < body_length; i++) {
        uint32_t cursor;
        uint8_t type_a;
        uint8_t type_b;

        if (body[i] != ACPI_AML_NAME_OP) continue;
        cursor = i + 1U;
        if (cursor < body_length &&
            body[cursor] == ACPI_AML_ROOT_PREFIX) cursor++;
        if (cursor + 4U > body_length ||
            !acpi_signature_equal((const char*)body + cursor,
                                  "_S5_", 4U)) continue;
        acpi_power_info.s5_candidates++;
        cursor += 4U;
        if (cursor >= body_length ||
            acpi_parse_s5_package(body + cursor, body + body_length,
                                  &type_a, &type_b) != OK) {
            continue;
        }
        if (!valid_count) {
            acpi_power_info.s5_type_a = type_a;
            acpi_power_info.s5_type_b = type_b;
        }
        valid_count++;
    }

    if (valid_count > 1U) {
        acpi_power_info.s5_state = ACPI_S5_AMBIGUOUS;
        acpi_power_info.s5_type_a = 0;
        acpi_power_info.s5_type_b = 0;
        acpi_log_anomaly("Declaracao _S5_ ambigua; transicao bloqueada");
    } else if (valid_count == 1U) {
        acpi_power_info.s5_state = ACPI_S5_DECLARED;
    } else if (acpi_power_info.s5_candidates) {
        acpi_power_info.s5_state = ACPI_S5_MALFORMED;
        acpi_log_anomaly("Declaracao _S5_ malformada; transicao bloqueada");
    }
}

static void acpi_finalize_s5_capability(void) {
    uint8_t mode_ready;
    uint8_t pm1_ready;

    acpi_power_info.mode_enable_available =
        acpi_power_info.smi_command_port > 0U &&
        acpi_power_info.smi_command_port <= ACPI_IO_BYTE_MAX_PORT &&
        acpi_power_info.acpi_enable_value != 0U;
    mode_ready = acpi_power_info.mode == ACPI_MODE_ENABLED ||
                 (acpi_power_info.mode == ACPI_MODE_DISABLED &&
                  acpi_power_info.mode_enable_available);
    pm1_ready = acpi_power_info.pm1a_readable &&
                (!acpi_power_info.pm1b_present ||
                 acpi_power_info.pm1b_readable);
    acpi_power_info.s5_transition_ready =
        acpi_status.available &&
        !acpi_status.partial &&
        acpi_status.fadt_present &&
        acpi_status.dsdt_present &&
        acpi_power_info.fadt_power_fields_present &&
        !acpi_power_info.hardware_reduced &&
        pm1_ready &&
        acpi_power_info.s5_state == ACPI_S5_DECLARED &&
        mode_ready;
}

static acpi_mode_t acpi_read_current_mode(uint16_t* out_pm1a,
                                          uint16_t* out_pm1b) {
    uint16_t pm1a = acpi_inw(
        (uint16_t)acpi_power_info.pm1a_control.address);
    uint8_t pm1a_enabled = (pm1a & ACPI_PM1_SCI_ENABLE) != 0;

    if (out_pm1a) *out_pm1a = pm1a;
    if (!acpi_power_info.pm1b_present) return pm1a_enabled ?
        ACPI_MODE_ENABLED : ACPI_MODE_DISABLED;

    uint16_t pm1b = acpi_inw(
        (uint16_t)acpi_power_info.pm1b_control.address);
    uint8_t pm1b_enabled = (pm1b & ACPI_PM1_SCI_ENABLE) != 0;

    if (out_pm1b) *out_pm1b = pm1b;
    if (pm1a_enabled != pm1b_enabled) return ACPI_MODE_INCONSISTENT;
    return pm1a_enabled ? ACPI_MODE_ENABLED : ACPI_MODE_DISABLED;
}

static void acpi_halt_forever(void) __attribute__((noreturn));

static void acpi_halt_forever(void) {
#if defined(ZEPHYROS_HOST_TEST)
    acpi_host_halt();
#else
    asm volatile("cli");
    for (;;) asm volatile("hlt");
#endif
}

static void acpi_enable_mode_or_halt(void) {
    LOG_INFO("ACPI", "Solicitando propriedade ACPI via SMI_CMD");
    acpi_outb((uint16_t)acpi_power_info.smi_command_port,
              acpi_power_info.acpi_enable_value);

    for (uint32_t i = 0; i < ACPI_MODE_ENABLE_POLL_LIMIT; i++) {
        if (acpi_read_current_mode(0, 0) == ACPI_MODE_ENABLED) {
            LOG_INFO("ACPI", "Modo ACPI habilitado para transicao S5");
            return;
        }
        __asm__ volatile("nop");
    }
    LOG_ERROR("ACPI", "Timeout ao adquirir modo ACPI; estado terminal");
    acpi_halt_forever();
}

static int acpi_s5_preflight(void) {
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "S5 solicitado antes da inicializacao");
        return ERR_STATE;
    }
    if (!acpi_status.available || acpi_status.partial ||
        !acpi_status.fadt_present || !acpi_status.dsdt_present) {
        LOG_WARN("ACPI", "S5 bloqueado por snapshot ACPI incompleto");
        return ERR_UNAVAILABLE;
    }
    if (!acpi_power_info.fadt_power_fields_present ||
        acpi_power_info.hardware_reduced) {
        LOG_WARN("ACPI", "S5 bloqueado pelo contrato da FADT");
        return ERR_UNAVAILABLE;
    }
    if (!acpi_power_info.pm1a_readable ||
        (acpi_power_info.pm1b_present &&
         !acpi_power_info.pm1b_readable)) {
        LOG_WARN("ACPI", "S5 bloqueado por registro PM1 incompativel");
        return ERR_UNAVAILABLE;
    }
    if (acpi_power_info.s5_state != ACPI_S5_DECLARED) {
        LOG_WARN("ACPI", "S5 bloqueado por declaracao AML insegura");
        return ERR_UNAVAILABLE;
    }
    if (!acpi_power_info.s5_transition_ready) {
        LOG_WARN("ACPI", "S5 bloqueado por modo ACPI indisponivel");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static uint16_t acpi_build_sleep_value(uint16_t current,
                                       uint8_t sleep_type) {
    uint16_t preserved = current &
        (uint16_t)~(ACPI_PM1_SLEEP_TYPE_MASK | ACPI_PM1_SLEEP_ENABLE);

    return preserved |
           ((uint16_t)sleep_type << ACPI_PM1_SLEEP_TYPE_SHIFT) |
           ACPI_PM1_SLEEP_ENABLE;
}

static void acpi_parse_fadt(uint32_t address,
                            const acpi_sdt_header_t* fadt) {
    const uint8_t* bytes = (const uint8_t*)fadt;
    uint32_t length = acpi_read_u32((const uint8_t*)fadt +
                                    ACPI_SDT_LENGTH_OFFSET);
    uint32_t dsdt_address;
    uint32_t facs_address;
    const acpi_sdt_header_t* dsdt;

    if (length < ACPI_FADT_DSDT_OFFSET + sizeof(uint32_t)) {
        acpi_status.malformed_tables++;
        acpi_note_partial(ERR_INVALID);
        acpi_log_anomaly("FADT menor que os campos obrigatorios");
        return;
    }
    acpi_status.fadt_present = 1;
    acpi_status.fadt_address = address;
    acpi_parse_fadt_power(bytes, length);
    dsdt_address = acpi_choose_32bit_address(bytes, length,
                                             ACPI_FADT_DSDT_OFFSET,
                                             ACPI_FADT_X_DSDT_OFFSET,
                                             ACPI_FADT_X_DSDT_END);
    facs_address = acpi_choose_32bit_address(bytes, length,
                                             ACPI_FADT_FACS_OFFSET,
                                             ACPI_FADT_X_FACS_OFFSET,
                                             ACPI_FADT_X_FACS_END);

    dsdt = acpi_validate_sdt(dsdt_address, "DSDT");
    if (dsdt) {
        acpi_status.dsdt_present = 1;
        acpi_status.dsdt_address = dsdt_address;
        acpi_store_table(dsdt_address, dsdt);
        acpi_parse_s5(dsdt);
    } else {
        acpi_status.malformed_tables++;
        acpi_note_partial(dsdt_address ? ERR_INVALID : ERR_NOT_FOUND);
    }

    if (facs_address &&
        acpi_range_in_memory_map(facs_address, 8U)) {
        const uint8_t* facs =
            (const uint8_t*)acpi_resolve_address(facs_address);
        uint32_t facs_length = acpi_read_u32(facs + 4U);

        if (acpi_signature_equal((const char*)facs, "FACS", 4U) &&
            facs_length >= ACPI_FACS_MIN_LENGTH &&
            facs_length <= ACPI_MAX_TABLE_LENGTH &&
            acpi_range_in_memory_map(facs_address, facs_length)) {
            acpi_status.facs_present = 1;
            acpi_status.facs_address = facs_address;
        } else {
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
        }
    }
}

static int acpi_finish_init(uint32_t start_ticks, int result) {
    acpi_finalize_s5_capability();
    acpi_status.scan_ticks = timer_get_ticks() - start_ticks;
    acpi_status.initialized = 1;
    acpi_power_info.initialized = 1;
    acpi_madt_info.initialized = 1;
    acpi_initialized = 1;
    acpi_memory_map = 0;
    acpi_memory_map_count = 0;
    acpi_init_result = result;

    if (result == OK) {
        LOG_INFO("ACPI", "Snapshot ACPI inicializado com sucesso");
    } else if (acpi_status.available) {
        LOG_WARN("ACPI", "Snapshot ACPI parcial; diagnostico preservado");
    } else {
        LOG_WARN("ACPI", "ACPI indisponivel; fallback preservado");
    }
    return result;
}

int acpi_init(const mmap_entry_t* memory_map, uint32_t entry_count) {
    uint32_t start_ticks = timer_get_ticks();
    const acpi_rsdp_v1_t* rsdp;
    const acpi_sdt_header_t* root = 0;
    uint32_t fadt_address = 0;
    int root_result;

    LOG_INFO("ACPI", "Inicializando descoberta ACPI");
    if (acpi_initialized) {
        LOG_WARN("ACPI", "Descoberta ACPI ja inicializada");
        return acpi_init_result;
    }
    kmemset(&acpi_status, 0, sizeof(acpi_status));
    kmemset(&acpi_power_info, 0, sizeof(acpi_power_info));
    kmemset(&acpi_madt_info, 0, sizeof(acpi_madt_info));
    kmemset(acpi_tables, 0, sizeof(acpi_tables));
    kmemset(acpi_madt_entries, 0, sizeof(acpi_madt_entries));
    acpi_anomaly_logs = 0;
    acpi_init_result = OK;
    if (!memory_map) {
        LOG_ERROR("ACPI", "Mapa E820 nulo");
        return acpi_finish_init(start_ticks, ERR_NULL);
    }
    if (!entry_count) {
        LOG_ERROR("ACPI", "Mapa E820 vazio");
        return acpi_finish_init(start_ticks, ERR_INVALID);
    }
    acpi_memory_map = memory_map;
    acpi_memory_map_count = entry_count;

    rsdp = acpi_find_rsdp();
    if (!rsdp) {
        int result = acpi_status.malformed_tables ?
                     ERR_INVALID : ERR_NOT_FOUND;
        LOG_WARN("ACPI", "RSDP nao encontrado");
        return acpi_finish_init(start_ticks, result);
    }
#if defined(ZEPHYROS_HOST_TEST)
    acpi_status.rsdp_address = acpi_host_physical_address(rsdp);
#else
    acpi_status.rsdp_address = (uint32_t)rsdp;
#endif
    acpi_status.revision = rsdp->revision;
    acpi_copy_text(acpi_status.oem_id, rsdp->oem_id, 6U);

    root_result = acpi_select_root(rsdp, &root);
    if (root_result != OK) return acpi_finish_init(start_ticks, root_result);
    acpi_status.available = 1;
    acpi_store_table(acpi_status.root_address, root);
    acpi_parse_root(root, &fadt_address);
    if (fadt_address) {
        const acpi_sdt_header_t* fadt =
            acpi_validate_sdt(fadt_address, "FACP");
        if (fadt) acpi_parse_fadt(fadt_address, fadt);
    }
    if (!acpi_status.fadt_present || !acpi_status.dsdt_present) {
        acpi_note_partial(ERR_NOT_FOUND);
    }
    return acpi_finish_init(start_ticks, acpi_init_result);
}

int acpi_get_status(acpi_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Consulta antes da inicializacao");
        return ERR_STATE;
    }
    *out_status = acpi_status;
    return OK;
}

int acpi_get_power_info(acpi_power_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar energia ACPI");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Energia consultada antes da inicializacao");
        return ERR_STATE;
    }
    *out_info = acpi_power_info;
    return OK;
}

int acpi_reset(void) {
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "RESET_REG solicitado antes da inicializacao");
        return ERR_STATE;
    }
    if (!acpi_power_info.reset_register_present ||
        !acpi_power_info.reset_register_valid) {
        LOG_WARN("ACPI", "RESET_REG ACPI indisponivel para i386");
        return ERR_UNAVAILABLE;
    }
    LOG_INFO("ACPI", "Solicitando reinicio pelo RESET_REG ACPI");
#if !defined(ZEPHYROS_HOST_TEST)
    asm volatile("cli" : : : "memory");
#endif
    acpi_outb((uint16_t)acpi_power_info.reset_register.address,
              acpi_power_info.reset_value);
    for (uint32_t i = 0; i < ACPI_RESET_RETURN_POLL_LIMIT; i++) {
        __asm__ volatile("pause");
    }
    LOG_ERROR("ACPI", "RESET_REG ACPI retornou sem reiniciar");
    return ERR_TIMEOUT;
}

int acpi_poweroff(void) {
    uint16_t pm1a_value = 0;
    uint16_t pm1b_value = 0;
    uint16_t sleep_a;
    uint16_t sleep_b = 0;
    acpi_mode_t current_mode;
    int result = acpi_s5_preflight();

    if (result != OK) return result;
    current_mode = acpi_read_current_mode(&pm1a_value, &pm1b_value);
    if (current_mode != ACPI_MODE_ENABLED &&
        current_mode != ACPI_MODE_DISABLED) {
        LOG_ERROR("ACPI", "Modo ACPI atual impede transicao S5");
        return ERR_STATE;
    }
    if (current_mode == ACPI_MODE_DISABLED &&
        !acpi_power_info.mode_enable_available) {
        LOG_WARN("ACPI", "Modo ACPI desabilitado sem aquisicao segura");
        return ERR_UNAVAILABLE;
    }

    LOG_INFO("ACPI", "Iniciando transicao terminal para S5");
#if !defined(ZEPHYROS_HOST_TEST)
    asm volatile("cli");
#endif
    if (current_mode == ACPI_MODE_DISABLED) acpi_enable_mode_or_halt();
    current_mode = acpi_read_current_mode(&pm1a_value, &pm1b_value);
    if (current_mode != ACPI_MODE_ENABLED) {
        LOG_ERROR("ACPI", "Modo ACPI nao confirmado apos comando; estado terminal");
        acpi_halt_forever();
    }

    sleep_a = acpi_build_sleep_value(pm1a_value,
                                     acpi_power_info.s5_type_a);
    if (acpi_power_info.pm1b_present) {
        sleep_b = acpi_build_sleep_value(pm1b_value,
                                         acpi_power_info.s5_type_b);
    }
    LOG_INFO("ACPI", "Escrevendo S5 em PM1a antes de PM1b");
    acpi_outw((uint16_t)acpi_power_info.pm1a_control.address, sleep_a);
    if (acpi_power_info.pm1b_present) {
        acpi_outw((uint16_t)acpi_power_info.pm1b_control.address, sleep_b);
    }
    LOG_ERROR("ACPI", "Firmware nao concluiu S5; estado terminal");
    acpi_halt_forever();
}

int acpi_enter_s5(void) {
    return acpi_poweroff();
}

int acpi_get_table_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar quantidade de tabelas");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Quantidade consultada antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = acpi_status.table_count;
    return OK;
}

int acpi_get_table_at(uint32_t index, acpi_table_info_t* out_table) {
    if (!out_table) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar tabela");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Tabela consultada antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= acpi_status.table_count) {
        LOG_WARN("ACPI", "Indice de tabela inexistente");
        return ERR_NOT_FOUND;
    }
    *out_table = acpi_tables[index];
    return OK;
}

int acpi_find_table(const char signature[4], uint32_t occurrence,
                    acpi_table_info_t* out_table) {
    uint32_t found = 0;

    if (!signature || !out_table) {
        LOG_ERROR("ACPI", "Consulta por assinatura com destino nulo");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Busca por assinatura antes da inicializacao");
        return ERR_STATE;
    }
    for (uint32_t i = 0; i < acpi_status.table_count; i++) {
        if (!acpi_signature_equal(acpi_tables[i].signature,
                                  signature, 4U)) continue;
        if (found++ == occurrence) {
            *out_table = acpi_tables[i];
            return OK;
        }
    }
    LOG_WARN("ACPI", "Tabela solicitada nao encontrada");
    return ERR_NOT_FOUND;
}

int acpi_get_madt_info(acpi_madt_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar MADT");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "MADT consultado antes da inicializacao");
        return ERR_STATE;
    }
    *out_info = acpi_madt_info;
    return OK;
}

int acpi_get_madt_entry_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar entradas MADT");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Entradas MADT consultadas antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = acpi_madt_info.entry_count;
    return OK;
}

int acpi_get_madt_entry_at(uint32_t index, acpi_madt_entry_t* out_entry) {
    if (!out_entry) {
        LOG_ERROR("ACPI", "Destino nulo ao consultar entrada MADT");
        return ERR_NULL;
    }
    if (!acpi_initialized) {
        LOG_ERROR("ACPI", "Entrada MADT consultada antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= acpi_madt_info.entry_count) {
        LOG_WARN("ACPI", "Indice de entrada MADT inexistente");
        return ERR_NOT_FOUND;
    }
    *out_entry = acpi_madt_entries[index];
    return OK;
}

const char* acpi_root_kind_name(acpi_root_kind_t kind) {
    if (kind == ACPI_ROOT_RSDT) return "RSDT";
    if (kind == ACPI_ROOT_XSDT) return "XSDT";
    return "NENHUMA";
}

#if defined(ZEPHYROS_HOST_TEST)
void acpi_host_reset(void) {
    kmemset(&acpi_status, 0, sizeof(acpi_status));
    kmemset(&acpi_power_info, 0, sizeof(acpi_power_info));
    kmemset(acpi_tables, 0, sizeof(acpi_tables));
    kmemset(&acpi_madt_info, 0, sizeof(acpi_madt_info));
    kmemset(acpi_madt_entries, 0, sizeof(acpi_madt_entries));
    acpi_memory_map = 0;
    acpi_memory_map_count = 0;
    acpi_anomaly_logs = 0;
    acpi_initialized = 0;
    acpi_init_result = ERR_STATE;
}
#endif
