#include "drivers/usb_msc.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/uhci.h"

#define USB_MSC_CLASS 0x08U
#define USB_MSC_SUBCLASS_SCSI 0x06U
#define USB_MSC_PROTOCOL_BOT 0x50U
#define USB_MSC_BULK_IN_FLAG 0x80U
#define USB_MSC_REQUEST_CLASS_INTERFACE 0x21U
#define USB_MSC_REQUEST_STANDARD_ENDPOINT 0x02U
#define USB_MSC_REQUEST_RESET 0xFFU
#define USB_MSC_REQUEST_CLEAR_FEATURE 1U
#define USB_MSC_FEATURE_ENDPOINT_HALT 0U
#define USB_MSC_CBW_SIGNATURE 0x43425355U
#define USB_MSC_CSW_SIGNATURE 0x53425355U
#define USB_MSC_CBW_LENGTH 31U
#define USB_MSC_CSW_LENGTH 13U
#define USB_MSC_INQUIRY_LENGTH 36U
#define USB_MSC_CAPACITY_LENGTH 8U
#define USB_MSC_SECTOR_SIZE 512U
#define USB_MSC_CDB_INQUIRY 0x12U
#define USB_MSC_CDB_TEST_UNIT_READY 0x00U
#define USB_MSC_CDB_READ_CAPACITY10 0x25U
#define USB_MSC_CDB_READ10 0x28U
#define USB_MSC_RECOVERY_ATTEMPTS 2U

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_length;
    uint8_t flags;
    uint8_t lun;
    uint8_t cdb_length;
    uint8_t cdb[16];
} __attribute__((packed)) usb_msc_cbw_t;

typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed)) usb_msc_csw_t;

typedef struct {
    usb_msc_info_t info;
    usb_device_info_t device;
    uint32_t tag;
} usb_msc_record_t;

static usb_msc_record_t usb_msc_records[USB_MSC_MAX_DEVICES];
static uint32_t usb_msc_count;
static uint8_t usb_msc_initialized;

static uint32_t msc_read_u32_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

static void msc_write_u32_be(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void msc_copy_field(char* destination, uint32_t capacity,
                           const uint8_t* source, uint32_t length) {
    uint32_t copied = 0U;

    if (!destination || !capacity || !source) return;
    while (length && source[length - 1U] == ' ') length--;
    while (copied < length && copied + 1U < capacity) {
        destination[copied] = (char)source[copied];
        copied++;
    }
    destination[copied] = '\0';
}

static int msc_text_equal(const char* left, const char* right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (*left != *right) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int msc_record_index(const char* id) {
    if (!id) return -1;
    for (uint32_t index = 0U; index < usb_msc_count; index++) {
        if (msc_text_equal(usb_msc_records[index].info.id, id)) {
            return (int)index;
        }
    }
    return -1;
}

static int msc_is_candidate(const usb_device_info_t* device) {
    if (!device || device->state != USB_DEVICE_CONFIGURED ||
        device->speed != USB_DEVICE_SPEED_FULL) return 0;
    return device->interface_class == USB_MSC_CLASS &&
           device->interface_subclass == USB_MSC_SUBCLASS_SCSI &&
           device->interface_protocol == USB_MSC_PROTOCOL_BOT &&
           device->bulk_in_count == 1U && device->bulk_out_count == 1U &&
           device->bulk_in_endpoint && device->bulk_out_endpoint &&
           device->bulk_in_max_packet && device->bulk_out_max_packet;
}

static void msc_build_block_id(const usb_device_info_t* device, char* output) {
    uint32_t offset = 0U;
    const char* prefix = "usb-ms-";

    if (!output) return;
    output[0] = '\0';
    while (*prefix && offset + 1U < BLOCK_DEVICE_ID_SIZE) {
        output[offset++] = *prefix++;
    }
    if (device) {
        const char* source = device->id;

        if (source[0] == 'u' && source[1] == 's' && source[2] == 'b' &&
            source[3] == '-' && source[4] == 'd' && source[5] == 'e' &&
            source[6] == 'v' && source[7] == '-') source += 8U;

        while (*source && offset + 1U < BLOCK_DEVICE_ID_SIZE) {
            output[offset++] = *source++;
        }
    }
    if (offset + 3U < BLOCK_DEVICE_ID_SIZE) {
        output[offset++] = '-';
        output[offset++] = 'l';
        output[offset++] = '0';
    }
    output[offset] = '\0';
}

static int msc_bulk_out(usb_msc_record_t* record, uint8_t* data,
                        uint16_t length) {
    uint16_t actual = 0U;
    int result = uhci_bulk_transfer(&record->device,
                                    record->info.bulk_out_endpoint, 0U,
                                    data, length, &actual);

    if (result != OK) return result;
    if (actual != length) {
        LOG_ERROR("MSC", "Transferencia Bulk OUT incompleta");
        return ERR_DISK;
    }
    return OK;
}

static int msc_bulk_in(usb_msc_record_t* record, uint8_t* data,
                       uint16_t length, uint16_t* out_length) {
    return uhci_bulk_transfer(&record->device, record->info.bulk_in_endpoint,
                              USB_MSC_BULK_IN_FLAG, data, length, out_length);
}

static int msc_reset_recovery(usb_msc_record_t* record) {
    uint16_t actual = 0U;
    int result;

    result = uhci_control_request(
        &record->device, USB_MSC_REQUEST_CLASS_INTERFACE,
        USB_MSC_REQUEST_RESET, 0U, record->info.interface_number, 0U, 0,
        &actual);
    if (result == OK) {
        result = uhci_control_request(
            &record->device, USB_MSC_REQUEST_STANDARD_ENDPOINT,
            USB_MSC_REQUEST_CLEAR_FEATURE, USB_MSC_FEATURE_ENDPOINT_HALT,
            record->info.bulk_in_endpoint, 0U, 0, &actual);
    }
    if (result == OK) {
        result = uhci_control_request(
            &record->device, USB_MSC_REQUEST_STANDARD_ENDPOINT,
            USB_MSC_REQUEST_CLEAR_FEATURE, USB_MSC_FEATURE_ENDPOINT_HALT,
            record->info.bulk_out_endpoint, 0U, 0, &actual);
    }
    if (result == OK) result = uhci_reset_bulk_toggles(&record->device);
    if (result != OK) {
        LOG_ERROR("MSC", "Reset recovery BOT falhou");
        return result;
    }
    record->info.last_error = OK;
    record->reset_count++;
    return OK;
}

static int msc_bot_command_once(usb_msc_record_t* record, const uint8_t* cdb,
                                uint8_t cdb_length, uint8_t* data,
                                uint32_t data_length, uint8_t direction_in) {
    usb_msc_cbw_t cbw;
    usb_msc_csw_t csw;
    uint8_t cbw_bytes[USB_MSC_CBW_LENGTH];
    uint8_t csw_bytes[USB_MSC_CSW_LENGTH];
    uint16_t actual = 0U;
    int result;

    if (!record || !cdb || !cdb_length || cdb_length > sizeof(cbw.cdb) ||
        data_length > USB_UHCI_BULK_BUFFER_SIZE ||
        (data_length && !data) || (data_length && !direction_in)) {
        LOG_ERROR("MSC", "Comando BOT fora do contrato de leitura");
        return ERR_INVALID;
    }
    kmemset(&cbw, 0, sizeof(cbw));
    cbw.signature = USB_MSC_CBW_SIGNATURE;
    cbw.tag = ++record->tag;
    if (!cbw.tag) cbw.tag = ++record->tag;
    cbw.data_length = data_length;
    cbw.flags = data_length && direction_in ? USB_MSC_BULK_IN_FLAG : 0U;
    cbw.lun = record->info.lun;
    cbw.cdb_length = cdb_length;
    kmemcpy(cbw.cdb, cdb, cdb_length);
    kmemcpy(cbw_bytes, &cbw, sizeof(cbw));
    result = msc_bulk_out(record, cbw_bytes, sizeof(cbw_bytes));
    if (result != OK) return result;
    if (data_length) {
        if (!direction_in) return ERR_UNAVAILABLE;
        result = msc_bulk_in(record, data, (uint16_t)data_length, &actual);
        if (result != OK || actual != data_length) return ERR_DISK;
    }
    result = msc_bulk_in(record, csw_bytes, sizeof(csw_bytes), &actual);
    if (result != OK || actual != sizeof(csw_bytes)) return ERR_DISK;
    kmemcpy(&csw, csw_bytes, sizeof(csw));
    if (csw.signature != USB_MSC_CSW_SIGNATURE || csw.tag != cbw.tag ||
        csw.residue != 0U || csw.residue > data_length) {
        LOG_ERROR("MSC", "CSW BOT invalido");
        return ERR_INVALID;
    }
    if (csw.status == 2U) return ERR_STATE;
    if (csw.status != 0U) return ERR_DISK;
    return OK;
}

static int msc_bot_command(usb_msc_record_t* record, const uint8_t* cdb,
                           uint8_t cdb_length, uint8_t* data,
                           uint32_t data_length, uint8_t direction_in) {
    int result = ERR_STATE;

    for (uint32_t attempt = 0U; attempt < USB_MSC_RECOVERY_ATTEMPTS;
         attempt++) {
        record->command_count++;
        result = msc_bot_command_once(record, cdb, cdb_length, data,
                                      data_length, direction_in);
        if (result == OK) return OK;
        if (attempt + 1U < USB_MSC_RECOVERY_ATTEMPTS &&
            msc_reset_recovery(record) != OK) break;
    }
    record->last_error = result;
    record->info.state = USB_MSC_DEGRADED;
    LOG_ERROR("MSC", "Comando BOT/SCSI falhou");
    return result;
}

static int msc_scsi_prepare(usb_msc_record_t* record) {
    uint8_t inquiry[USB_MSC_INQUIRY_LENGTH];
    uint8_t capacity[USB_MSC_CAPACITY_LENGTH];
    uint8_t cdb[16];
    int result;

    kmemset(cdb, 0, sizeof(cdb));
    cdb[0] = USB_MSC_CDB_INQUIRY;
    cdb[4] = USB_MSC_INQUIRY_LENGTH;
    result = msc_bot_command(record, cdb, 6U, inquiry,
                             sizeof(inquiry), 1U);
    if (result != OK) return result;
    if ((inquiry[0] & 0x1FU) != 0U) return ERR_UNAVAILABLE;
    msc_copy_field(record->info.vendor, USB_MSC_VENDOR_SIZE, inquiry + 8U, 8U);
    msc_copy_field(record->info.product, USB_MSC_PRODUCT_SIZE, inquiry + 16U,
                   16U);
    msc_copy_field(record->info.revision, USB_MSC_REVISION_SIZE, inquiry + 32U,
                   4U);

    kmemset(cdb, 0, sizeof(cdb));
    cdb[0] = USB_MSC_CDB_TEST_UNIT_READY;
    result = msc_bot_command(record, cdb, 6U, 0, 0U, 0U);
    if (result != OK) return result;

    kmemset(cdb, 0, sizeof(cdb));
    cdb[0] = USB_MSC_CDB_READ_CAPACITY10;
    result = msc_bot_command(record, cdb, 10U, capacity,
                             sizeof(capacity), 1U);
    if (result != OK) return result;
    if (msc_read_u32_be(capacity) == 0xFFFFFFFFU ||
        msc_read_u32_be(capacity + 4U) != USB_MSC_SECTOR_SIZE) {
        LOG_ERROR("MSC", "Capacidade MSC fora do contrato de setor");
        return ERR_UNAVAILABLE;
    }
    record->info.sector_count = msc_read_u32_be(capacity) + 1U;
    record->info.sector_size = USB_MSC_SECTOR_SIZE;
    return record->info.sector_count ? OK : ERR_INVALID;
}

static int msc_scsi_read_sector(usb_msc_record_t* record, uint32_t lba,
                                uint8_t* buffer) {
    uint8_t cdb[16];

    if (!record || !buffer || lba >= record->info.sector_count) {
        LOG_ERROR("MSC", "Leitura SCSI fora dos limites");
        if (record) record->info.last_error = ERR_DISK;
        return ERR_DISK;
    }
    kmemset(cdb, 0, sizeof(cdb));
    cdb[0] = USB_MSC_CDB_READ10;
    msc_write_u32_be(cdb + 2U, lba);
    cdb[7] = 0U;
    cdb[8] = 1U;
    if (msc_bot_command(record, cdb, 10U, buffer, USB_MSC_SECTOR_SIZE,
                        1U) != OK) return record->last_error;
    record->read_ops++;
    return OK;
}

static int msc_block_read(void* context, uint32_t lba, uint8_t count,
                          uint8_t* buffer) {
    usb_msc_record_t* record = (usb_msc_record_t*)context;

    if (!record || !buffer || !count) {
        LOG_ERROR("MSC", "Argumento invalido na leitura de bloco MSC");
        return ERR_NULL;
    }
    if (record->info.state != USB_MSC_READY) {
        int error = record->info.last_error ? record->info.last_error : ERR_STATE;

        LOG_ERROR("MSC", "Leitura recusada por MSC degradado");
        return error;
    }
    if (lba >= record->info.sector_count ||
        count > record->info.sector_count - lba) {
        record->info.last_error = ERR_DISK;
        LOG_ERROR("MSC", "Leitura de bloco MSC fora dos limites");
        return ERR_DISK;
    }
    for (uint8_t index = 0U; index < count; index++) {
        int result = msc_scsi_read_sector(record, lba + index,
                                          buffer + index * USB_MSC_SECTOR_SIZE);

        if (result != OK) return result;
    }
    return OK;
}

static int msc_prepare_record(usb_msc_record_t* record,
                              const usb_device_info_t* device) {
    block_device_t block;
    uint32_t command_count = record ? record->info.command_count : 0U;
    uint32_t read_ops = record ? record->info.read_ops : 0U;
    uint32_t reset_count = record ? record->info.reset_count : 0U;
    uint32_t tag = record ? record->tag : 0U;
    int result;

    if (!record || !device) {
        LOG_ERROR("MSC", "Registro ou dispositivo nulo no preparo MSC");
        return ERR_NULL;
    }
    kmemset(record, 0, sizeof(*record));
    record->info.command_count = command_count;
    record->info.read_ops = read_ops;
    record->info.reset_count = reset_count;
    record->tag = tag;
    record->device = *device;
    record->info.state = USB_MSC_DEGRADED;
    kmemcpy(record->info.id, device->id, USB_DEVICE_ID_SIZE);
    record->info.lun = 0U;
    record->info.interface_number = device->interface_number;
    record->info.bulk_in_endpoint = device->bulk_in_endpoint;
    record->info.bulk_out_endpoint = device->bulk_out_endpoint;
    record->info.bulk_in_max_packet = device->bulk_in_max_packet;
    record->info.bulk_out_max_packet = device->bulk_out_max_packet;
    msc_build_block_id(device, record->info.block_id);
    result = msc_scsi_prepare(record);
    if (result != OK) {
        record->info.last_error = result;
        return result;
    }
    kmemset(&block, 0, sizeof(block));
    block.provider = BLOCK_PROVIDER_USB_MSC;
    block.sector_count = record->info.sector_count;
    block.sector_size = record->info.sector_size;
    block.read_only = 1U;
    block.online = 1U;
    block.last_error = OK;
    block.ops.context = record;
    block.ops.read = msc_block_read;
    block.ops.write = 0;
    kmemcpy(block.id, record->info.block_id, BLOCK_DEVICE_ID_SIZE);
    kmemcpy(block.model, record->info.product, USB_MSC_PRODUCT_SIZE);
    return block_register(&block);
}

static int msc_register_device(const usb_device_info_t* device) {
    int index = msc_record_index(device->id);
    uint8_t existing = index >= 0;
    int result;

    if (existing && usb_msc_records[index].info.state == USB_MSC_READY) {
        usb_msc_records[index].device = *device;
        return OK;
    }
    if (!existing) {
        if (usb_msc_count >= USB_MSC_MAX_DEVICES) return ERR_OVERFLOW;
        index = (int)usb_msc_count++;
    } else {
        result = msc_reset_recovery(&usb_msc_records[index]);
        if (result != OK) {
            usb_msc_records[index].info.last_error = result;
            return result;
        }
    }
    result = msc_prepare_record(&usb_msc_records[index], device);
    if (result != OK) {
        usb_msc_records[index].info.state = USB_MSC_DEGRADED;
        usb_msc_records[index].info.last_error = result;
        LOG_WARN("MSC", "Dispositivo USB nao ficou pronto como MSC");
        return result;
    }
    usb_msc_records[index].info.state = USB_MSC_READY;
    usb_msc_records[index].info.last_error = OK;
    return OK;
}

int usb_msc_init(void) {
    int result;

    LOG_INFO("MSC", "Inicializando driver USB Mass Storage");
    kmemset(usb_msc_records, 0, sizeof(usb_msc_records));
    usb_msc_count = 0U;
    usb_msc_initialized = 1U;
    result = usb_msc_refresh();
    if (result != OK) {
        LOG_ERROR("MSC", "Falha ao enumerar dispositivos Mass Storage");
        return result;
    }
    LOG_INFO("MSC", "Driver USB Mass Storage inicializado com sucesso");
    return OK;
}

int usb_msc_refresh(void) {
    uint32_t device_count = 0U;
    int result;
    int first_error = OK;

    if (!usb_msc_initialized) {
        LOG_ERROR("MSC", "Atualizacao MSC antes da inicializacao");
        return ERR_STATE;
    }
    result = usb_manager_get_device_count(&device_count);
    if (result != OK) return result;
    for (uint32_t index = 0U; index < device_count; index++) {
        usb_device_info_t device;

        if (usb_manager_get_device(index, &device) != OK) {
            LOG_WARN("MSC", "Dispositivo USB ausente durante a atualizacao");
            continue;
        }
        if (!msc_is_candidate(&device)) continue;
        result = msc_register_device(&device);
        if (result != OK && first_error == OK) first_error = result;
    }
    return first_error;
}

int usb_msc_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("MSC", "Destino nulo na contagem MSC");
        return ERR_NULL;
    }
    if (!usb_msc_initialized) {
        LOG_ERROR("MSC", "Contagem MSC antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = usb_msc_count;
    return OK;
}

int usb_msc_get_at(uint32_t index, usb_msc_info_t* out_info) {
    if (!out_info) {
        LOG_ERROR("MSC", "Destino nulo na consulta MSC");
        return ERR_NULL;
    }
    if (!usb_msc_initialized) {
        LOG_ERROR("MSC", "Consulta MSC antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= usb_msc_count) {
        LOG_ERROR("MSC", "Indice MSC invalido");
        return ERR_INVALID;
    }
    *out_info = usb_msc_records[index].info;
    return OK;
}

int usb_msc_find(const char* id, usb_msc_info_t* out_info) {
    int index;

    if (!id || !out_info) {
        LOG_ERROR("MSC", "Argumento nulo na busca MSC");
        return ERR_NULL;
    }
    if (!usb_msc_initialized) {
        LOG_ERROR("MSC", "Busca MSC antes da inicializacao");
        return ERR_STATE;
    }
    index = msc_record_index(id);
    if (index < 0) return ERR_NOT_FOUND;
    *out_info = usb_msc_records[index].info;
    return OK;
}

int usb_msc_is_active(const char* id) {
    int index = msc_record_index(id);

    return usb_msc_initialized && index >= 0 &&
           usb_msc_records[index].info.state == USB_MSC_READY;
}

int usb_msc_validate_state(void) {
    if (!usb_msc_initialized || usb_msc_count > USB_MSC_MAX_DEVICES) {
        LOG_ERROR("MSC", "Estado MSC invalido");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < usb_msc_count; index++) {
        usb_msc_record_t* record = &usb_msc_records[index];

        if (!record->info.id[0] || !record->info.block_id[0] ||
            record->info.lun != 0U ||
            record->info.bulk_in_endpoint == 0U ||
            record->info.bulk_out_endpoint == 0U ||
            (record->info.state != USB_MSC_READY &&
             record->info.state != USB_MSC_DEGRADED)) {
            LOG_ERROR("MSC", "Registro MSC inconsistente");
            return ERR_STATE;
        }
        if (record->info.state == USB_MSC_READY) {
            block_device_t block;

            if (record->info.sector_size != USB_MSC_SECTOR_SIZE ||
                !record->info.sector_count ||
                block_find(record->info.block_id, &block) != OK ||
                !block.read_only || block.provider != BLOCK_PROVIDER_USB_MSC) {
                LOG_ERROR("MSC", "Provedor de bloco MSC ausente");
                return ERR_STATE;
            }
        }
    }
    return OK;
}

const char* usb_msc_state_name(usb_msc_state_t state) {
    return state == USB_MSC_READY ? "READY" : "DEGRADED";
}
