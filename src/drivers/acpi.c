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
static acpi_table_info_t acpi_tables[ACPI_MAX_TABLES];
static const mmap_entry_t* acpi_memory_map = 0;
static uint32_t acpi_memory_map_count = 0;
static uint32_t acpi_anomaly_logs = 0;
static int acpi_initialized = 0;
static int acpi_init_result = ERR_STATE;

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
        rsdp = (const acpi_rsdp_v1_t*)address;
        if (!acpi_signature_equal(rsdp->signature, "RSD PTR ", 8U)) continue;
        if (!acpi_checksum_valid(rsdp, ACPI_RSDP_V1_LENGTH)) {
            acpi_status.malformed_tables++;
            acpi_note_partial(ERR_INVALID);
            continue;
        }
        if (rsdp->revision < 2U) return rsdp;
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
        return rsdp;
    }
    return 0;
}

static const acpi_rsdp_v1_t* acpi_find_rsdp(void) {
    uint32_t ebda = (uint32_t)acpi_read_u16((const void*)ACPI_EBDA_POINTER)
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
    header = (const acpi_sdt_header_t*)address;
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
    } else {
        acpi_status.malformed_tables++;
        acpi_note_partial(dsdt_address ? ERR_INVALID : ERR_NOT_FOUND);
    }

    if (facs_address &&
        acpi_range_in_memory_map(facs_address, 8U)) {
        const uint8_t* facs = (const uint8_t*)facs_address;
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
    acpi_status.scan_ticks = timer_get_ticks() - start_ticks;
    acpi_status.initialized = 1;
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
    kmemset(acpi_tables, 0, sizeof(acpi_tables));
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
    acpi_status.rsdp_address = (uint32_t)rsdp;
    acpi_status.revision = rsdp->revision;
    acpi_copy_text(acpi_status.oem_id, rsdp->oem_id, 6U);

    root_result = acpi_select_root(rsdp, &root);
    if (root_result != OK) return acpi_finish_init(start_ticks, root_result);
    acpi_status.available = 1;
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

const char* acpi_root_kind_name(acpi_root_kind_t kind) {
    if (kind == ACPI_ROOT_RSDT) return "RSDT";
    if (kind == ACPI_ROOT_XSDT) return "XSDT";
    return "NENHUMA";
}
