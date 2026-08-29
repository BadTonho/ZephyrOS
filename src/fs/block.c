#include "fs/block.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "drivers/ata.h"

static block_device_t block_devices[BLOCK_MAX_DEVICES];
static uint32_t block_device_count;
static uint8_t block_initialized;

static uint8_t block_ata_slots[ATA_MAX_DEVICES];

typedef struct {
    bio_request_t* bio;
    block_request_state_t state;
    uint32_t completed_sectors;
    int status;
} block_request_t;

typedef struct {
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t completion_calls;
    int forced_result;
} block_self_test_context_t;

#define BLOCK_SELF_TEST_SECTOR_COUNT 16U
#define BLOCK_SELF_TEST_MAX_TRANSFER 8U
#define BLOCK_SELF_TEST_LBA 2U
#define BLOCK_SELF_TEST_BUFFER_SECTORS 2U
#define BLOCK_SELF_TEST_PATTERN 0xA5U

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

static void block_request_init(block_request_t* request,
                               bio_request_t* bio) {
    request->bio = bio;
    request->state = BLOCK_REQUEST_QUEUED;
    request->completed_sectors = 0U;
    request->status = ERR_STATE;
    bio->state = BLOCK_REQUEST_QUEUED;
    bio->completed_sectors = 0U;
    bio->status = ERR_STATE;
}

static void block_request_in_flight(block_request_t* request) {
    request->state = BLOCK_REQUEST_IN_FLIGHT;
    request->bio->state = BLOCK_REQUEST_IN_FLIGHT;
}

static int block_request_finish(block_request_t* request,
                                block_request_state_t state,
                                uint32_t completed_sectors, int status) {
    request->state = state;
    request->completed_sectors = completed_sectors;
    request->status = status;
    request->bio->state = state;
    request->bio->completed_sectors = completed_sectors;
    request->bio->status = status;
    if (request->bio->completion) {
        request->bio->completion(request->bio, request->bio->context);
    }
    return status;
}

static int block_request_reject(bio_request_t* bio, int status) {
    block_request_t request;

    block_request_init(&request, bio);
    return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U, status);
}

static int block_submit_device(block_device_t* device, bio_request_t* bio,
                               uint8_t update_counters) {
    block_request_t request;
    uint32_t transfer_bytes;
    int result;

    if (!bio) {
        LOG_ERROR("BLK", "BIO nulo no despacho de bloco");
        return ERR_NULL;
    }
    block_request_init(&request, bio);
    if (!device) {
        LOG_ERROR("BLK", "Dispositivo ausente na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_NULL);
    }
    if ((bio->flags & ~BLOCK_BIO_FLAGS_SUPPORTED) != 0U) {
        LOG_ERROR("BLK", "Flags invalidas na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_INVALID);
    }
    if (bio->operation > BLOCK_OPERATION_FLUSH) {
        LOG_ERROR("BLK", "Operacao invalida na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_INVALID);
    }
    if (device->sector_size != BLOCK_SECTOR_SIZE) {
        LOG_ERROR("BLK", "Tamanho de setor invalido na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_INVALID);
    }
    if (!device->sector_count || !device->max_transfer_sectors ||
        device->max_transfer_sectors > BLOCK_MAX_TRANSFER_SECTORS) {
        LOG_ERROR("BLK", "Geometria invalida na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_STATE);
    }
    if (!device->online) {
        LOG_ERROR("BLK", "Dispositivo offline na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_DISK);
    }
    if (bio->operation == BLOCK_OPERATION_FLUSH) {
        if (bio->lba || bio->sector_count || bio->buffer ||
            bio->buffer_bytes || bio->flags != 0U) {
            LOG_ERROR("BLK", "Formato invalido para BIO de flush");
            return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                        ERR_INVALID);
        }
        if (!(device->capabilities & BLOCK_DEVICE_CAP_FLUSH) ||
            !device->ops.flush) {
            LOG_WARN("BLK", "Flush nao suportado pelo dispositivo");
            return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                        ERR_UNAVAILABLE);
        }
        block_request_in_flight(&request);
        result = device->ops.flush(device->ops.context);
        if (result != OK) {
            if (update_counters) device->last_error = result;
            LOG_ERROR("BLK", "Flush de bloco falhou");
            return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                        result);
        }
        if (update_counters) device->last_error = OK;
        return block_request_finish(&request, BLOCK_REQUEST_COMPLETED, 0U,
                                    OK);
    }
    if (bio->sector_count == 0U || !bio->buffer) {
        LOG_ERROR("BLK", "Buffer ou quantidade invalida na submissao de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    bio->buffer ? ERR_INVALID : ERR_NULL);
    }
    if (bio->sector_count > device->max_transfer_sectors) {
        LOG_ERROR("BLK", "Quantidade de setores excede o limite do dispositivo");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_OVERFLOW);
    }
    if (bio->sector_count > 0xFFFFFFFFU / device->sector_size) {
        LOG_ERROR("BLK", "Tamanho de buffer excede o limite de bloco");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_OVERFLOW);
    }
    transfer_bytes = bio->sector_count * device->sector_size;
    if (bio->buffer_bytes < transfer_bytes) {
        LOG_ERROR("BLK", "Buffer de BIO menor que a transferencia");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_INVALID);
    }
    if (bio->lba >= device->sector_count ||
        bio->sector_count > device->sector_count - bio->lba) {
        LOG_ERROR("BLK", "LBA de BIO fora dos limites");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_DISK);
    }
    if (bio->operation == BLOCK_OPERATION_WRITE) {
        if (device->read_only || (!device->ops.write &&
                                  !device->ops.write_flags)) {
            LOG_WARN("BLK", "Escrita recusada pelo dispositivo");
            return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                        ERR_UNAVAILABLE);
        }
        if ((bio->flags & BLOCK_BIO_FLAG_FUA) != 0U &&
            (!(device->capabilities & BLOCK_DEVICE_CAP_FUA) ||
             !device->ops.write_flags)) {
            LOG_WARN("BLK", "FUA nao suportado pelo dispositivo");
            return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                        ERR_UNAVAILABLE);
        }
    } else if (bio->flags != 0U) {
        LOG_ERROR("BLK", "Flags de escrita usadas em leitura de BIO");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_INVALID);
    }
    if (bio->operation == BLOCK_OPERATION_READ && !device->ops.read) {
        LOG_ERROR("BLK", "Callback de leitura ausente no dispositivo");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    ERR_STATE);
    }

    block_request_in_flight(&request);
    if (bio->operation == BLOCK_OPERATION_READ) {
        result = device->ops.read(device->ops.context, bio->lba,
                                  (uint8_t)bio->sector_count,
                                  (uint8_t*)bio->buffer);
    } else if ((bio->flags & BLOCK_BIO_FLAG_FUA) != 0U) {
        result = device->ops.write_flags(
            device->ops.context, bio->lba, (uint8_t)bio->sector_count,
            (const uint8_t*)bio->buffer, bio->flags);
    } else if (device->ops.write) {
        result = device->ops.write(device->ops.context, bio->lba,
                                   (uint8_t)bio->sector_count,
                                   (const uint8_t*)bio->buffer);
    } else {
        result = device->ops.write_flags(
            device->ops.context, bio->lba, (uint8_t)bio->sector_count,
            (const uint8_t*)bio->buffer, bio->flags);
    }
    if (result != OK) {
        if (update_counters) device->last_error = result;
        LOG_ERROR("BLK", "Operacao de bloco falhou");
        return block_request_finish(&request, BLOCK_REQUEST_ERROR, 0U,
                                    result);
    }
    if (update_counters) {
        if (bio->operation == BLOCK_OPERATION_READ) {
            device->read_ops += bio->sector_count;
        } else {
            device->write_ops += bio->sector_count;
        }
        device->last_error = OK;
    }
    return block_request_finish(&request, BLOCK_REQUEST_COMPLETED,
                                bio->sector_count, OK);
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
    out_descriptor->max_transfer_sectors = BLOCK_MAX_TRANSFER_SECTORS;
    out_descriptor->capabilities = 0U;
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
    block_device_t normalized;
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
    normalized = *descriptor;
    if (normalized.max_transfer_sectors == 0U) {
        normalized.max_transfer_sectors = BLOCK_MAX_TRANSFER_SECTORS;
    }
    if (normalized.sector_size != BLOCK_SECTOR_SIZE ||
        !normalized.sector_count ||
        normalized.provider > BLOCK_PROVIDER_USB_MSC ||
        normalized.max_transfer_sectors > BLOCK_MAX_TRANSFER_SECTORS ||
        (normalized.capabilities & ~BLOCK_DEVICE_CAPABILITIES_SUPPORTED) != 0U ||
        ((normalized.capabilities & BLOCK_DEVICE_CAP_FLUSH) != 0U &&
         !normalized.ops.flush) ||
        ((normalized.capabilities & BLOCK_DEVICE_CAP_FUA) != 0U &&
         (normalized.read_only || !normalized.ops.write_flags)) ||
        (!normalized.read_only && !normalized.ops.write &&
         !normalized.ops.write_flags)) {
        LOG_ERROR("BLK", "Geometria de bloco nao suportada");
        return ERR_INVALID;
    }
    index = block_index(normalized.id);
    if (index >= 0) {
        read_ops = block_devices[index].read_ops;
        write_ops = block_devices[index].write_ops;
        last_error = block_devices[index].last_error;
        block_devices[index] = normalized;
        block_devices[index].read_ops = read_ops;
        block_devices[index].write_ops = write_ops;
        block_devices[index].last_error = last_error;
        return OK;
    }
    if (block_device_count >= BLOCK_MAX_DEVICES) {
        LOG_ERROR("BLK", "Limite de dispositivos de bloco atingido");
        return ERR_OVERFLOW;
    }
    block_devices[block_device_count++] = normalized;
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

int block_submit_sync(bio_request_t* request) {
    int index;

    if (!request) {
        LOG_ERROR("BLK", "Requisicao nula na submissao de BIO");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Submissao de BIO antes da inicializacao");
        return block_request_reject(request, ERR_STATE);
    }
    if (!request->device_id) {
        LOG_ERROR("BLK", "Dispositivo nulo na submissao de BIO");
        return block_request_reject(request, ERR_NULL);
    }
    if (!block_text_terminated(request->device_id, BLOCK_DEVICE_ID_SIZE)) {
        LOG_ERROR("BLK", "ID nao terminado na submissao de BIO");
        return block_request_reject(request, ERR_INVALID);
    }
    index = block_index(request->device_id);
    if (index < 0) {
        LOG_WARN("BLK", "Dispositivo nao encontrado na submissao de BIO");
        return block_request_reject(request, ERR_NOT_FOUND);
    }
    return block_submit_device(&block_devices[index], request, 1U);
}

int block_read(const char* id, uint32_t lba, uint8_t count,
               uint8_t* buffer) {
    bio_request_t request;

    if (!id || !buffer || !count) {
        LOG_ERROR("BLK", "Argumento invalido na leitura de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Leitura de bloco antes da inicializacao");
        return ERR_STATE;
    }
    if (!block_text_terminated(id, BLOCK_DEVICE_ID_SIZE)) {
        return ERR_NOT_FOUND;
    }
    kmemset(&request, 0, sizeof(request));
    request.device_id = id;
    request.lba = lba;
    request.sector_count = count;
    request.buffer = buffer;
    request.buffer_bytes = (uint32_t)count * BLOCK_SECTOR_SIZE;
    request.operation = BLOCK_OPERATION_READ;
    return block_submit_sync(&request);
}

int block_write(const char* id, uint32_t lba, uint8_t count,
                const uint8_t* buffer) {
    bio_request_t request;

    if (!id || !buffer || !count) {
        LOG_ERROR("BLK", "Argumento invalido na escrita de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Escrita de bloco antes da inicializacao");
        return ERR_STATE;
    }
    if (!block_text_terminated(id, BLOCK_DEVICE_ID_SIZE)) {
        return ERR_NOT_FOUND;
    }
    kmemset(&request, 0, sizeof(request));
    request.device_id = id;
    request.lba = lba;
    request.sector_count = count;
    request.buffer = (void*)buffer;
    request.buffer_bytes = (uint32_t)count * BLOCK_SECTOR_SIZE;
    request.operation = BLOCK_OPERATION_WRITE;
    return block_submit_sync(&request);
}

static int block_self_test_read(void* context, uint32_t lba, uint8_t count,
                                uint8_t* buffer) {
    block_self_test_context_t* test = (block_self_test_context_t*)context;

    (void)lba;
    if (!test || !buffer || !count) {
        LOG_WARN("BLK", "Argumento invalido no backend de leitura de teste");
        return ERR_NULL;
    }
    test->read_calls++;
    if (test->forced_result != OK) return test->forced_result;
    kmemset(buffer, BLOCK_SELF_TEST_PATTERN,
            (uint32_t)count * BLOCK_SECTOR_SIZE);
    return OK;
}

static int block_self_test_write(void* context, uint32_t lba, uint8_t count,
                                 const uint8_t* buffer) {
    block_self_test_context_t* test = (block_self_test_context_t*)context;

    (void)lba;
    if (!test || !buffer || !count) {
        LOG_WARN("BLK", "Argumento invalido no backend de escrita de teste");
        return ERR_NULL;
    }
    test->write_calls++;
    return test->forced_result;
}

static int block_self_test_write_flags(void* context, uint32_t lba,
                                       uint8_t count, const uint8_t* buffer,
                                       uint32_t flags) {
    (void)flags;
    return block_self_test_write(context, lba, count, buffer);
}

static int block_self_test_flush(void* context) {
    block_self_test_context_t* test = (block_self_test_context_t*)context;

    if (!test) {
        LOG_WARN("BLK", "Contexto invalido no backend de flush de teste");
        return ERR_NULL;
    }
    return test->forced_result;
}

static void block_self_test_completion(bio_request_t* request, void* context) {
    block_self_test_context_t* test = (block_self_test_context_t*)context;

    if (request && test) test->completion_calls++;
}

static int block_self_test_expect(const bio_request_t* request,
                                  block_request_state_t state, int status,
                                  uint32_t completed_sectors) {
    if (!request || request->state != state || request->status != status ||
        request->completed_sectors != completed_sectors) {
        LOG_ERROR("BLK", "Autoteste de estado de BIO falhou");
        return ERR_STATE;
    }
    return OK;
}

static void block_self_test_prepare(bio_request_t* request,
                                    block_self_test_context_t* context) {
    kmemset(request, 0, sizeof(*request));
    request->device_id = "blk-self-test";
    request->buffer = 0;
    request->buffer_bytes = 0U;
    request->operation = BLOCK_OPERATION_READ;
    request->completion = block_self_test_completion;
    request->context = context;
}

int block_self_test(void) {
    block_device_t mock;
    block_self_test_context_t context;
    bio_request_t request;
    uint8_t buffer[BLOCK_SECTOR_SIZE * BLOCK_SELF_TEST_BUFFER_SECTORS];
    uint32_t initial_device_count;
    int result;

    if (!block_initialized || block_validate_state() != OK) {
        LOG_ERROR("BLK", "Autoteste de BIO antes da camada estar pronta");
        return ERR_STATE;
    }
    initial_device_count = block_device_count;
    kmemset(&mock, 0, sizeof(mock));
    block_copy_text(mock.id, BLOCK_DEVICE_ID_SIZE, "blk-self-test");
    block_copy_text(mock.model, BLOCK_DEVICE_MODEL_SIZE, "deterministic");
    mock.provider = BLOCK_PROVIDER_ATA;
    mock.sector_count = BLOCK_SELF_TEST_SECTOR_COUNT;
    mock.sector_size = BLOCK_SECTOR_SIZE;
    mock.online = 1U;
    mock.max_transfer_sectors = BLOCK_SELF_TEST_MAX_TRANSFER;
    mock.ops.context = &context;
    mock.ops.read = block_self_test_read;
    mock.ops.write = block_self_test_write;
    mock.ops.flush = block_self_test_flush;
    mock.ops.write_flags = block_self_test_write_flags;
    kmemset(&context, 0, sizeof(context));

    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_device(&mock, &request, 0U);
    if (result != OK || block_self_test_expect(
            &request, BLOCK_REQUEST_COMPLETED, OK, 1U) != OK ||
        buffer[0] != BLOCK_SELF_TEST_PATTERN) {
        LOG_ERROR("BLK", "Autoteste de leitura de BIO falhou");
        return ERR_STATE;
    }

    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_WRITE;
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_device(&mock, &request, 0U);
    if (result != OK || block_self_test_expect(
            &request, BLOCK_REQUEST_COMPLETED, OK, 1U) != OK) {
        LOG_ERROR("BLK", "Autoteste de escrita de BIO falhou");
        return ERR_STATE;
    }

    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = BLOCK_SELF_TEST_MAX_TRANSFER + 1U;
    request.buffer = buffer;
    request.buffer_bytes = sizeof(buffer);
    result = block_submit_device(&mock, &request, 0U);
    if (result != ERR_OVERFLOW || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_OVERFLOW, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de limite de BIO falhou");
        return ERR_STATE;
    }

    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_SECTOR_COUNT - 1U;
    request.sector_count = 2U;
    request.buffer = buffer;
    request.buffer_bytes = sizeof(buffer);
    result = block_submit_device(&mock, &request, 0U);
    if (result != ERR_DISK || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_DISK, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de LBA de BIO falhou");
        return ERR_STATE;
    }

    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_FLUSH;
    result = block_submit_device(&mock, &request, 0U);
    if (result != ERR_UNAVAILABLE || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_UNAVAILABLE, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de flush de BIO falhou");
        return ERR_STATE;
    }

    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_WRITE;
    request.flags = BLOCK_BIO_FLAG_FUA;
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_device(&mock, &request, 0U);
    if (result != ERR_UNAVAILABLE || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_UNAVAILABLE, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de FUA de BIO falhou");
        return ERR_STATE;
    }

    mock.read_only = 1U;
    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_WRITE;
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_device(&mock, &request, 0U);
    mock.read_only = 0U;
    if (result != ERR_UNAVAILABLE || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_UNAVAILABLE, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de somente leitura falhou");
        return ERR_STATE;
    }

    context.forced_result = ERR_TIMEOUT;
    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_device(&mock, &request, 0U);
    if (result != ERR_TIMEOUT || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_TIMEOUT, 0U) != OK ||
        context.completion_calls != 8U ||
        block_device_count != initial_device_count) {
        LOG_ERROR("BLK", "Autoteste de conclusao de BIO falhou");
        return ERR_STATE;
    }
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
            !device->max_transfer_sectors ||
            device->max_transfer_sectors > BLOCK_MAX_TRANSFER_SECTORS ||
            (device->capabilities & ~BLOCK_DEVICE_CAPABILITIES_SUPPORTED) != 0U ||
            ((device->capabilities & BLOCK_DEVICE_CAP_FLUSH) != 0U &&
             !device->ops.flush) ||
            ((device->capabilities & BLOCK_DEVICE_CAP_FUA) != 0U &&
             (device->read_only || !device->ops.write_flags)) ||
            (!device->read_only && !device->ops.write &&
             !device->ops.write_flags)) {
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
