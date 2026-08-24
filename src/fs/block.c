#include "fs/block.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/ata.h"

static block_device_t block_devices[BLOCK_MAX_DEVICES];
static uint32_t block_device_count;
static uint8_t block_initialized;

static uint8_t block_ata_slots[ATA_MAX_DEVICES];

static void block_copy_text(char* destination, uint32_t capacity,
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

static int block_text_equal(const char* left, const char* right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (*left != *right) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int block_text_valid(const char* text, uint32_t capacity) {
    if (!text || !capacity || !text[0]) return 0;
    for (uint32_t index = 0U; index < capacity; index++) {
        if (!text[index]) return 1;
    }
    return 0;
}

static int block_text_terminated(const char* text, uint32_t capacity) {
    if (!text || !capacity) return 0;
    for (uint32_t index = 0U; index < capacity; index++) {
        if (!text[index]) return 1;
    }
    return 0;
}

static int block_index(const char* id) {
    if (!block_text_terminated(id, BLOCK_DEVICE_ID_SIZE)) return -1;
    for (uint32_t index = 0U; index < block_device_count; index++) {
        if (block_text_equal(block_devices[index].id, id)) {
            return (int)index;
        }
    }
    return -1;
}

static int block_ata_read(void* context, uint32_t lba, uint8_t count,
                          uint8_t* buffer) {
    uint8_t* slot = (uint8_t*)context;

    if (!slot) {
        LOG_ERROR("BLK", "Contexto ATA ausente na leitura de bloco");
        return ERR_NULL;
    }
    return ata_read_device_sectors(*slot, lba, count, buffer);
}

static int block_ata_write(void* context, uint32_t lba, uint8_t count,
                           const uint8_t* buffer) {
    uint8_t* slot = (uint8_t*)context;

    if (!slot) {
        LOG_ERROR("BLK", "Contexto ATA ausente na escrita de bloco");
        return ERR_NULL;
    }
    return ata_write_device_sectors(*slot, lba, count, buffer);
}

static int block_ata_descriptor(const ata_device_t* ata,
                                block_device_t* out_descriptor) {
    if (!ata || !out_descriptor) {
        LOG_ERROR("BLK", "Argumento nulo ao criar provedor ATA");
        return ERR_NULL;
    }
    if (ata->slot >= ATA_MAX_DEVICES || !ata->sectors) {
        LOG_ERROR("BLK", "Geometria ATA invalida no provedor de bloco");
        return ERR_INVALID;
    }
    kmemset(out_descriptor, 0, sizeof(*out_descriptor));
    out_descriptor->id[0] = 'a';
    out_descriptor->id[1] = 't';
    out_descriptor->id[2] = 'a';
    out_descriptor->id[3] = (char)('0' + ata->slot);
    out_descriptor->id[4] = '\0';
    block_copy_text(out_descriptor->model, BLOCK_DEVICE_MODEL_SIZE,
                    ata->model);
    out_descriptor->provider = BLOCK_PROVIDER_ATA;
    out_descriptor->sector_count = ata->sectors;
    out_descriptor->sector_size = BLOCK_SECTOR_SIZE;
    out_descriptor->read_only = 0U;
    out_descriptor->online = 1U;
    out_descriptor->read_ops = ata->read_ops;
    out_descriptor->write_ops = ata->write_ops;
    out_descriptor->last_error = ata->last_error;
    out_descriptor->ops.context = &block_ata_slots[ata->slot];
    out_descriptor->ops.read = block_ata_read;
    out_descriptor->ops.write = block_ata_write;
    block_ata_slots[ata->slot] = ata->slot;
    return OK;
}

static int block_register_ata_devices(void) {
    uint8_t count = 0U;
    int result = ata_get_device_count(&count);

    if (result != OK) {
        LOG_WARN("BLK", "Nenhum dispositivo ATA disponivel para o bloco");
        return result == ERR_NOT_FOUND ? OK : result;
    }
    if (!count) {
        LOG_WARN("BLK", "ATA inicializado sem discos presentes");
        return OK;
    }
    for (uint8_t slot = 0U; slot < ATA_MAX_DEVICES; slot++) {
        ata_device_t ata;
        block_device_t descriptor;
        int device_result;

        device_result = ata_get_device_at(slot, &ata);
        if (device_result == ERR_NOT_FOUND) {
            LOG_DEBUG("BLK", "Slot ATA vazio durante o registro de bloco");
            continue;
        }
        if (device_result != OK) {
            LOG_ERROR("BLK", "Falha ao consultar slot ATA no registro de bloco");
            return device_result;
        }
        result = block_ata_descriptor(&ata, &descriptor);
        if (result != OK) return result;
        result = block_register(&descriptor);
        if (result != OK) {
            LOG_ERROR("BLK", "Falha ao registrar provedor ATA");
            return result;
        }
    }
    return OK;
}

int block_init(void) {
    int result;

    LOG_INFO("BLK", "Inicializando camada de dispositivos de bloco");
    kmemset(block_devices, 0, sizeof(block_devices));
    kmemset(block_ata_slots, 0, sizeof(block_ata_slots));
    block_device_count = 0U;
    block_initialized = 1U;
    result = block_register_ata_devices();
    if (result != OK) {
        block_initialized = 0U;
        LOG_ERROR("BLK", "Falha ao registrar dispositivos ATA");
        return result;
    }
    LOG_INFO("BLK", "Camada de dispositivos de bloco inicializada");
    return OK;
}

int block_register(const block_device_t* descriptor) {
    int index;
    uint32_t read_ops;
    uint32_t write_ops;
    int last_error;

    if (!descriptor || !block_text_valid(descriptor->id,
                                         BLOCK_DEVICE_ID_SIZE) ||
        !block_text_terminated(descriptor->model, BLOCK_DEVICE_MODEL_SIZE) ||
        !descriptor->ops.read) {
        LOG_ERROR("BLK", "Descritor de bloco invalido");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Registro de bloco antes da inicializacao");
        return ERR_STATE;
    }
    if (descriptor->sector_size != BLOCK_SECTOR_SIZE ||
        !descriptor->sector_count ||
        descriptor->provider > BLOCK_PROVIDER_USB_MSC ||
        (!descriptor->read_only && !descriptor->ops.write)) {
        LOG_ERROR("BLK", "Geometria de bloco nao suportada");
        return ERR_INVALID;
    }
    index = block_index(descriptor->id);
    if (index >= 0) {
        read_ops = block_devices[index].read_ops;
        write_ops = block_devices[index].write_ops;
        last_error = block_devices[index].last_error;
        block_devices[index] = *descriptor;
        block_devices[index].read_ops = read_ops;
        block_devices[index].write_ops = write_ops;
        block_devices[index].last_error = last_error;
        return OK;
    }
    if (block_device_count >= BLOCK_MAX_DEVICES) {
        LOG_ERROR("BLK", "Limite de dispositivos de bloco atingido");
        return ERR_OVERFLOW;
    }
    block_devices[block_device_count++] = *descriptor;
    return OK;
}

int block_unregister(const char* id) {
    int index;

    if (!id) {
        LOG_ERROR("BLK", "ID nulo ao remover dispositivo de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Remocao de bloco antes da inicializacao");
        return ERR_STATE;
    }
    index = block_index(id);
    if (index < 0) {
        LOG_WARN("BLK", "Dispositivo de bloco nao encontrado");
        return ERR_NOT_FOUND;
    }
    for (uint32_t current = (uint32_t)index;
         current + 1U < block_device_count; current++) {
        block_devices[current] = block_devices[current + 1U];
    }
    block_device_count--;
    kmemset(&block_devices[block_device_count], 0,
            sizeof(block_devices[block_device_count]));
    return OK;
}

int block_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("BLK", "Destino nulo na contagem de blocos");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Contagem de blocos antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = block_device_count;
    return OK;
}

int block_get_at(uint32_t index, block_device_t* out_device) {
    if (!out_device) {
        LOG_ERROR("BLK", "Destino nulo na consulta de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Consulta de bloco antes da inicializacao");
        return ERR_STATE;
    }
    if (index >= block_device_count) {
        LOG_ERROR("BLK", "Indice de bloco invalido");
        return ERR_INVALID;
    }
    *out_device = block_devices[index];
    return OK;
}

int block_find(const char* id, block_device_t* out_device) {
    int index;

    if (!id || !out_device) {
        LOG_ERROR("BLK", "Argumento nulo na busca de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Busca de bloco antes da inicializacao");
        return ERR_STATE;
    }
    index = block_index(id);
    if (index < 0) return ERR_NOT_FOUND;
    *out_device = block_devices[index];
    return OK;
}

int block_read(const char* id, uint32_t lba, uint8_t count,
               uint8_t* buffer) {
    int index;
    int result;

    if (!id || !buffer || !count) {
        LOG_ERROR("BLK", "Argumento invalido na leitura de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Leitura de bloco antes da inicializacao");
        return ERR_STATE;
    }
    index = block_index(id);
    if (index < 0) return ERR_NOT_FOUND;
    if (!block_devices[index].online ||
        lba >= block_devices[index].sector_count ||
        count > block_devices[index].sector_count - lba) {
        LOG_ERROR("BLK", "Leitura de bloco fora dos limites");
        return ERR_DISK;
    }
    result = block_devices[index].ops.read(block_devices[index].ops.context,
                                           lba, count, buffer);
    if (result != OK) {
        block_devices[index].last_error = result;
        LOG_ERROR("BLK", "Leitura de bloco falhou");
        return result;
    }
    block_devices[index].read_ops += count;
    block_devices[index].last_error = OK;
    return OK;
}

int block_write(const char* id, uint32_t lba, uint8_t count,
                const uint8_t* buffer) {
    int index;
    int result;

    if (!id || !buffer || !count) {
        LOG_ERROR("BLK", "Argumento invalido na escrita de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Escrita de bloco antes da inicializacao");
        return ERR_STATE;
    }
    index = block_index(id);
    if (index < 0) return ERR_NOT_FOUND;
    if (block_devices[index].read_only || !block_devices[index].ops.write) {
        LOG_WARN("BLK", "Escrita recusada em provedor somente-leitura");
        return ERR_UNAVAILABLE;
    }
    if (lba >= block_devices[index].sector_count ||
        count > block_devices[index].sector_count - lba) {
        LOG_ERROR("BLK", "Escrita de bloco fora dos limites");
        return ERR_DISK;
    }
    result = block_devices[index].ops.write(block_devices[index].ops.context,
                                            lba, count, buffer);
    if (result != OK) {
        block_devices[index].last_error = result;
        LOG_ERROR("BLK", "Escrita de bloco falhou");
        return result;
    }
    block_devices[index].write_ops += count;
    block_devices[index].last_error = OK;
    return OK;
}

int block_validate_state(void) {
    if (!block_initialized || block_device_count > BLOCK_MAX_DEVICES) {
        LOG_ERROR("BLK", "Estado da camada de bloco invalido");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < block_device_count; index++) {
        block_device_t* device = &block_devices[index];

        if (!block_text_valid(device->id, BLOCK_DEVICE_ID_SIZE) ||
            !block_text_terminated(device->model, BLOCK_DEVICE_MODEL_SIZE) ||
            !device->ops.read ||
            device->sector_size != BLOCK_SECTOR_SIZE ||
            !device->sector_count ||
            device->provider > BLOCK_PROVIDER_USB_MSC ||
            (!device->read_only && !device->ops.write)) {
            LOG_ERROR("BLK", "Dispositivo de bloco inconsistente");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U; other < block_device_count; other++) {
            if (block_text_equal(device->id, block_devices[other].id)) {
                LOG_ERROR("BLK", "IDs de bloco duplicados");
                return ERR_STATE;
            }
        }
    }
    return OK;
}
