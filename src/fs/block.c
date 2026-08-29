#include "fs/block.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/workqueue.h"
#include "drivers/ata.h"
#include "fs/block.h"
#include "fs/block_cache.h"

static block_device_t block_devices[BLOCK_MAX_DEVICES];
static uint32_t block_device_count;
static uint8_t block_initialized;

static uint8_t block_ata_slots[ATA_MAX_DEVICES];

typedef struct {
    bio_request_t* bio;
    char device_id[BLOCK_DEVICE_ID_SIZE];
    uint32_t device_index;
} block_queue_entry_t;

static block_queue_entry_t block_queue[BLOCK_QUEUE_CAPACITY];
static uint32_t block_queue_count;
static spinlock_t block_queue_lock;
static block_queue_stats_t block_queue_stats;
static uint32_t block_stats_start_tick;
static work_struct_t block_work;
static uint8_t block_work_ready;
static uint8_t block_dispatching;
static char block_active_device_id[BLOCK_DEVICE_ID_SIZE];
static bio_request_t* block_active_bios[BLOCK_QUEUE_CAPACITY];
static uint32_t block_active_count;

typedef struct {
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t submit_calls;
    uint32_t completion_calls;
    uint32_t callback_order[BLOCK_QUEUE_CAPACITY];
    uint32_t callback_order_count;
    int forced_result;
} block_self_test_context_t;

#define BLOCK_SELF_TEST_SECTOR_COUNT 128U
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

static void block_request_init(bio_request_t* request) {
    request->state = BLOCK_REQUEST_QUEUED;
    request->completed_sectors = 0U;
    request->status = ERR_STATE;
}

static int block_request_finish(bio_request_t* request,
                                block_request_state_t state,
                                uint32_t completed_sectors, int status) {
    if (!request) return status;
    request->state = state;
    request->completed_sectors = completed_sectors;
    request->status = status;
    if (request->completion) {
        request->completion(request, request->context);
    }
    return status;
}

static int block_request_reject(bio_request_t* request, int status) {
    if (!request) {
        LOG_ERROR("BLK", "BIO nulo na rejeicao de bloco");
        return ERR_NULL;
    }
    block_request_init(request);
    return block_request_finish(request, BLOCK_REQUEST_ERROR, 0U, status);
}

static int block_validate_bio(const block_device_t* device,
                              const bio_request_t* bio) {
    uint32_t transfer_bytes;

    if (!bio) return ERR_NULL;
    if (!device) return ERR_NULL;
    if ((bio->flags & ~BLOCK_BIO_FLAGS_SUPPORTED) != 0U) {
        LOG_ERROR("BLK", "Flags invalidas na submissao de BIO");
        return ERR_INVALID;
    }
    if (bio->operation > BLOCK_OPERATION_FLUSH) {
        LOG_ERROR("BLK", "Operacao invalida na submissao de BIO");
        return ERR_INVALID;
    }
    if (device->sector_size != BLOCK_SECTOR_SIZE ||
        !device->sector_count || !device->max_transfer_sectors ||
        device->max_transfer_sectors > BLOCK_MAX_TRANSFER_SECTORS) {
        LOG_ERROR("BLK", "Geometria invalida na submissao de BIO");
        return ERR_STATE;
    }
    if (!device->online) {
        LOG_ERROR("BLK", "Dispositivo offline na submissao de BIO");
        return ERR_DISK;
    }
    if (bio->operation == BLOCK_OPERATION_FLUSH) {
        if (bio->lba || bio->sector_count || bio->buffer ||
            bio->buffer_bytes || bio->flags != 0U) {
            LOG_ERROR("BLK", "Formato invalido para BIO de flush");
            return ERR_INVALID;
        }
        if (!(device->capabilities & BLOCK_DEVICE_CAP_FLUSH) ||
            (!device->ops.flush && !device->ops.submit)) {
            LOG_WARN("BLK", "Flush nao suportado pelo dispositivo");
            return ERR_UNAVAILABLE;
        }
        return OK;
    }
    if (!bio->sector_count) {
        LOG_ERROR("BLK", "Quantidade nula na submissao de BIO");
        return ERR_INVALID;
    }
    if (!bio->buffer) {
        LOG_ERROR("BLK", "Buffer nulo na submissao de BIO");
        return ERR_NULL;
    }
    if (bio->sector_count > device->max_transfer_sectors) {
        LOG_ERROR("BLK", "Quantidade de setores excede o limite do dispositivo");
        return ERR_OVERFLOW;
    }
    if (bio->sector_count > 0xFFFFFFFFU / device->sector_size) {
        LOG_ERROR("BLK", "Tamanho de buffer excede o limite de bloco");
        return ERR_OVERFLOW;
    }
    transfer_bytes = bio->sector_count * device->sector_size;
    if (bio->buffer_bytes < transfer_bytes) {
        LOG_ERROR("BLK", "Buffer de BIO menor que a transferencia");
        return ERR_INVALID;
    }
    if (bio->lba >= device->sector_count ||
        bio->sector_count > device->sector_count - bio->lba) {
        LOG_ERROR("BLK", "LBA de BIO fora dos limites");
        return ERR_DISK;
    }
    if (bio->operation == BLOCK_OPERATION_WRITE) {
        if (device->read_only || (!device->ops.write &&
                                  !device->ops.write_flags &&
                                  !device->ops.submit)) {
            LOG_WARN("BLK", "Escrita recusada pelo dispositivo");
            return ERR_UNAVAILABLE;
        }
        if ((bio->flags & BLOCK_BIO_FLAG_FUA) != 0U &&
            (!(device->capabilities & BLOCK_DEVICE_CAP_FUA) ||
             (!device->ops.write_flags && !device->ops.submit))) {
            LOG_WARN("BLK", "FUA nao suportado pelo dispositivo");
            return ERR_UNAVAILABLE;
        }
    } else if (bio->flags != 0U) {
        LOG_ERROR("BLK", "Flags de escrita usadas em leitura de BIO");
        return ERR_INVALID;
    }
    if (bio->operation == BLOCK_OPERATION_READ &&
        !device->ops.read && !device->ops.submit) {
        LOG_ERROR("BLK", "Callback de leitura ausente no dispositivo");
        return ERR_STATE;
    }
    return OK;
}

static int block_bio_pending_locked(const bio_request_t* bio) {
    for (uint32_t index = 0U; index < block_queue_count; index++) {
        if (block_queue[index].bio == bio) return 1;
    }
    for (uint32_t index = 0U; index < block_active_count; index++) {
        if (block_active_bios[index] == bio) return 1;
    }
    return 0;
}

static int block_id_pending_locked(const char* id) {
    if (!id) return 0;
    if (block_active_count && block_text_equal(block_active_device_id, id)) {
        return 1;
    }
    for (uint32_t index = 0U; index < block_queue_count; index++) {
        if (block_text_equal(block_queue[index].device_id, id)) return 1;
    }
    return 0;
}

static void block_queue_remove_locked(uint32_t index) {
    for (; index + 1U < block_queue_count; index++) {
        block_queue[index] = block_queue[index + 1U];
    }
    if (block_queue_count) block_queue_count--;
    if (block_queue_count < BLOCK_QUEUE_CAPACITY) {
        kmemset(&block_queue[block_queue_count], 0,
                sizeof(block_queue[block_queue_count]));
    }
    block_queue_stats.queue_depth = block_queue_count;
}

static int block_queue_enqueue(bio_request_t* request, uint8_t asynchronous) {
    int index;
    int result;

    if (!request) return ERR_NULL;
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
    spinlock_acquire(&block_queue_lock);
    if (block_bio_pending_locked(request)) {
        spinlock_release(&block_queue_lock);
        LOG_ERROR("BLK", "BIO ja esta pendente na fila de bloco");
        return ERR_STATE;
    }
    block_request_init(request);
    index = block_index(request->device_id);
    if (index < 0) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Dispositivo nao encontrado na submissao de BIO");
        return block_request_reject(request, ERR_NOT_FOUND);
    }
    result = block_validate_bio(&block_devices[index], request);
    if (result != OK) {
        spinlock_release(&block_queue_lock);
        return block_request_reject(request, result);
    }
    if (request->operation == BLOCK_OPERATION_WRITE) {
        result = block_cache_invalidate_range(
            request->device_id, request->lba, request->sector_count);
        if (result != OK) {
            spinlock_release(&block_queue_lock);
            return block_request_reject(request, result);
        }
    }
    if (asynchronous && !block_work_ready) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Submissao assincrona indisponivel sem workqueue");
        return block_request_reject(request, ERR_UNAVAILABLE);
    }
    if (block_queue_count >= BLOCK_QUEUE_CAPACITY) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Fila de bloco cheia");
        return block_request_reject(request, ERR_OVERFLOW);
    }
    block_queue[block_queue_count].bio = request;
    block_copy_text(block_queue[block_queue_count].device_id,
                    BLOCK_DEVICE_ID_SIZE, request->device_id);
    block_queue[block_queue_count].device_index = (uint32_t)index;
    block_queue_count++;
    block_queue_stats.queue_depth = block_queue_count;
    block_queue_stats.submitted++;
    if (block_queue_count > block_queue_stats.peak_depth) {
        block_queue_stats.peak_depth = block_queue_count;
    }
    spinlock_release(&block_queue_lock);
    if (asynchronous) {
        result = schedule_work(&block_work);
        if (result != OK) {
            int removed = 0;

            spinlock_acquire(&block_queue_lock);
            for (uint32_t current = 0U; current < block_queue_count;
                 current++) {
                if (block_queue[current].bio == request) {
                    block_queue_remove_locked(current);
                    block_queue_stats.failed++;
                    block_queue_stats.last_error = result;
                    removed = 1;
                    break;
                }
            }
            spinlock_release(&block_queue_lock);
            if (removed) return block_request_reject(request, result);
            return request->status;
        }
    }
    return OK;
}

static int block_can_merge(const block_queue_entry_t* left,
                           const block_queue_entry_t* right,
                           const block_device_t* device) {
    const bio_request_t* left_bio = left->bio;
    const bio_request_t* right_bio = right->bio;
    uint32_t left_bytes;

    if (!left_bio || !right_bio || !device ||
        left_bio->operation == BLOCK_OPERATION_FLUSH ||
        right_bio->operation == BLOCK_OPERATION_FLUSH ||
        left_bio->operation != right_bio->operation ||
        left_bio->flags != right_bio->flags ||
        left->device_index != right->device_index ||
        left_bio->sector_count > 0xFFFFFFFFU / BLOCK_SECTOR_SIZE) {
        return 0;
    }
    if (left_bio->lba + left_bio->sector_count != right_bio->lba) return 0;
    left_bytes = left_bio->sector_count * BLOCK_SECTOR_SIZE;
    if ((uint8_t*)left_bio->buffer + left_bytes != right_bio->buffer) {
        return 0;
    }
    if (left_bio->sector_count > device->max_transfer_sectors ||
        right_bio->sector_count > device->max_transfer_sectors) {
        return 0;
    }
    return left_bio->sector_count <=
               device->max_transfer_sectors - right_bio->sector_count;
}

static int block_driver_submit(block_device_t* device,
                               block_request_t* request) {
    int result;

    if (!device || !request) {
        LOG_ERROR("BLK", "Dispositivo ou requisicao fisica nulos");
        return ERR_NULL;
    }
    request->status = ERR_STATE;
    request->completed_sectors = 0U;
    if (device->ops.submit) {
        result = device->ops.submit(request);
    } else if (request->operation == BLOCK_OPERATION_READ) {
        result = device->ops.read(device->ops.context, request->lba,
                                  (uint8_t)request->sector_count,
                                  (uint8_t*)request->buffer);
    } else if (request->operation == BLOCK_OPERATION_WRITE &&
               (request->flags & BLOCK_BIO_FLAG_FUA) != 0U) {
        result = device->ops.write_flags(
            device->ops.context, request->lba, (uint8_t)request->sector_count,
            (const uint8_t*)request->buffer, request->flags);
    } else if (request->operation == BLOCK_OPERATION_WRITE && device->ops.write) {
        result = device->ops.write(device->ops.context, request->lba,
                                   (uint8_t)request->sector_count,
                                   (const uint8_t*)request->buffer);
    } else if (request->operation == BLOCK_OPERATION_WRITE) {
        result = device->ops.write_flags(
            device->ops.context, request->lba, (uint8_t)request->sector_count,
            (const uint8_t*)request->buffer, request->flags);
    } else {
        result = device->ops.flush(device->ops.context);
    }
    if (result == OK && request->completed_sectors == 0U) {
        request->completed_sectors = request->sector_count;
    }
    if (request->completed_sectors > request->sector_count) {
        LOG_ERROR("BLK", "Driver retornou setores concluidos invalidos");
        request->completed_sectors = 0U;
        return ERR_STATE;
    }
    request->status = result;
    return result;
}

static void block_complete_batch(block_device_t* device,
                                 block_queue_entry_t* batch,
                                 uint32_t batch_count,
                                 block_request_t* physical) {
    uint32_t remaining = physical->completed_sectors;
    uint32_t successful = 0U;
    uint32_t failed = 0U;
    uint32_t read_completed = 0U;
    uint32_t write_completed = 0U;
    int failure_status = physical->status == OK ? ERR_STATE : physical->status;

    spinlock_acquire(&block_queue_lock);
    for (uint32_t index = 0U; index < batch_count; index++) {
        bio_request_t* bio = batch[index].bio;
        uint32_t completed = remaining < bio->sector_count ? remaining :
                             bio->sector_count;
        block_request_state_t state = physical->status == OK &&
                                              completed == bio->sector_count ?
                                          BLOCK_REQUEST_COMPLETED :
                                          BLOCK_REQUEST_ERROR;
        int status = physical->status;

        if (physical->status == OK && completed != bio->sector_count) {
            status = ERR_STATE;
        }
        bio->completed_sectors = completed;
        bio->status = status;
        bio->state = state;
        remaining -= completed;
        if (state == BLOCK_REQUEST_COMPLETED) successful++;
        else failed++;
        if (physical->operation == BLOCK_OPERATION_READ) {
            read_completed += completed;
        } else if (physical->operation == BLOCK_OPERATION_WRITE) {
            write_completed += completed;
        }
    }
    block_queue_stats.read_sectors += read_completed;
    block_queue_stats.write_sectors += write_completed;
    block_queue_stats.completed += successful;
    block_queue_stats.failed += failed;
    if (failed) {
        block_queue_stats.last_error = failure_status;
        if (device) device->last_error = block_queue_stats.last_error;
    } else if (device) {
        device->last_error = OK;
    }
    if (device) {
        device->read_ops += read_completed;
        device->write_ops += write_completed;
    }
    block_active_count = 0U;
    block_active_device_id[0] = '\0';
    spinlock_release(&block_queue_lock);
    if (failed) LOG_ERROR("BLK", "Operacao de bloco falhou");

    for (uint32_t index = 0U; index < batch_count; index++) {
        if (batch[index].bio->completion) {
            batch[index].bio->completion(batch[index].bio,
                                         batch[index].bio->context);
        }
    }
}

static int block_dispatch_one(uint8_t* out_had_work) {
    block_queue_entry_t batch[BLOCK_QUEUE_CAPACITY];
    block_request_t physical;
    block_device_t* device;
    uint32_t batch_count = 0U;
    int result;

    if (!out_had_work) {
        LOG_ERROR("BLK", "Destino nulo no despacho unitario");
        return ERR_NULL;
    }
    *out_had_work = 0U;
    spinlock_acquire(&block_queue_lock);
    if (!block_queue_count) {
        spinlock_release(&block_queue_lock);
        return OK;
    }
    *out_had_work = 1U;
    batch[batch_count++] = block_queue[0];
    block_queue_remove_locked(0U);
    device = &block_devices[batch[0].device_index];
    while (batch_count < BLOCK_QUEUE_CAPACITY && block_queue_count &&
           block_can_merge(&batch[batch_count - 1U], &block_queue[0],
                           device)) {
        batch[batch_count++] = block_queue[0];
        block_queue_remove_locked(0U);
    }
    for (uint32_t index = 0U; index < batch_count; index++) {
        batch[index].bio->state = BLOCK_REQUEST_IN_FLIGHT;
        block_active_bios[index] = batch[index].bio;
    }
    if (batch_count > 1U) {
        block_queue_stats.merged += batch_count - 1U;
    }
    block_active_count = batch_count;
    block_copy_text(block_active_device_id, BLOCK_DEVICE_ID_SIZE,
                    batch[0].device_id);
    spinlock_release(&block_queue_lock);

    kmemset(&physical, 0, sizeof(physical));
    physical.device_id = batch[0].device_id;
    physical.device_context = device->ops.context;
    physical.lba = batch[0].bio->lba;
    physical.sector_count = 0U;
    physical.buffer = batch[0].bio->buffer;
    physical.buffer_bytes = 0U;
    physical.operation = batch[0].bio->operation;
    physical.flags = batch[0].bio->flags;
    for (uint32_t index = 0U; index < batch_count; index++) {
        uint32_t bytes = batch[index].bio->sector_count * BLOCK_SECTOR_SIZE;

        physical.sector_count += batch[index].bio->sector_count;
        physical.buffer_bytes += bytes;
    }
    result = block_driver_submit(device, &physical);
    if (result != OK && physical.status == OK) physical.status = result;
    block_complete_batch(device, batch, batch_count, &physical);
    return OK;
}

static int block_work_callback(void* context) {
    uint32_t processed = 0U;

    (void)context;
    return block_dispatch(BLOCK_DISPATCH_BUDGET, &processed);
}

static int block_ata_submit(block_request_t* request) {
    uint8_t* slot;
    int result;

    if (!request) {
        LOG_ERROR("BLK", "Requisicao fisica ATA nula");
        return ERR_NULL;
    }
    slot = (uint8_t*)request->device_context;
    if (!slot) {
        LOG_ERROR("BLK", "Contexto ATA ausente na submissao de bloco");
        return ERR_NULL;
    }
    if (request->operation == BLOCK_OPERATION_READ) {
        result = ata_read_device_sectors(*slot, request->lba,
                                         (uint8_t)request->sector_count,
                                         (uint8_t*)request->buffer);
    } else if (request->operation == BLOCK_OPERATION_WRITE &&
               request->flags == 0U) {
        result = ata_write_device_sectors(
            *slot, request->lba, (uint8_t)request->sector_count,
            (const uint8_t*)request->buffer);
    } else {
        LOG_WARN("BLK", "Operacao ATA sem suporte nesta etapa");
        return ERR_UNAVAILABLE;
    }
    if (result == OK) request->completed_sectors = request->sector_count;
    return result;
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
    out_descriptor->ops.submit = block_ata_submit;
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
    int work_result;

    LOG_INFO("BLK", "Inicializando camada de dispositivos de bloco");
    if (block_initialized) {
        LOG_WARN("BLK", "Camada de dispositivos de bloco ja estava pronta");
        return OK;
    }
    spinlock_init(&block_queue_lock);
    kmemset(block_devices, 0, sizeof(block_devices));
    kmemset(block_ata_slots, 0, sizeof(block_ata_slots));
    kmemset(block_queue, 0, sizeof(block_queue));
    kmemset(&block_queue_stats, 0, sizeof(block_queue_stats));
    kmemset(&block_work, 0, sizeof(block_work));
    kmemset(block_active_bios, 0, sizeof(block_active_bios));
    block_active_device_id[0] = '\0';
    block_device_count = 0U;
    block_queue_count = 0U;
    block_active_count = 0U;
    block_dispatching = 0U;
    block_work_ready = 0U;
    block_queue_stats.queue_capacity = BLOCK_QUEUE_CAPACITY;
    block_stats_start_tick = timer_get_ticks();
    block_initialized = 1U;
    result = block_cache_init();
    if (result != OK) {
        block_initialized = 0U;
        LOG_ERROR("BLK", "Falha ao inicializar cache de blocos");
        return result;
    }
    work_result = work_init(&block_work, "Block I/O", WORK_PRIORITY_NORMAL,
                            block_work_callback, 0);
    if (work_result != OK) {
        block_work_ready = 0U;
        LOG_WARN("BLK", "Workqueue indisponivel; usando caminho sincrono");
    } else {
        block_work_ready = 1U;
    }
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
    int result;

    if (!descriptor || !block_text_valid(descriptor->id,
                                         BLOCK_DEVICE_ID_SIZE) ||
        !block_text_terminated(descriptor->model, BLOCK_DEVICE_MODEL_SIZE) ||
        (!descriptor->ops.read && !descriptor->ops.submit)) {
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
         !normalized.ops.flush && !normalized.ops.submit) ||
        ((normalized.capabilities & BLOCK_DEVICE_CAP_FUA) != 0U &&
         (normalized.read_only ||
          (!normalized.ops.write_flags && !normalized.ops.submit))) ||
        (!normalized.read_only && !normalized.ops.write &&
         !normalized.ops.write_flags && !normalized.ops.submit)) {
        LOG_ERROR("BLK", "Geometria de bloco nao suportada");
        return ERR_INVALID;
    }
    spinlock_acquire(&block_queue_lock);
    index = block_index(normalized.id);
    if (index >= 0) {
        if (block_id_pending_locked(normalized.id)) {
            spinlock_release(&block_queue_lock);
            LOG_WARN("BLK", "Substituicao recusada com BIO pendente");
            return ERR_STATE;
        }
        result = block_cache_invalidate_device(normalized.id);
        if (result != OK) {
            spinlock_release(&block_queue_lock);
            LOG_WARN("BLK", "Substituicao recusada com cache ocupado");
            return result;
        }
        read_ops = block_devices[index].read_ops;
        write_ops = block_devices[index].write_ops;
        last_error = block_devices[index].last_error;
        block_devices[index] = normalized;
        block_devices[index].read_ops = read_ops;
        block_devices[index].write_ops = write_ops;
        block_devices[index].last_error = last_error;
        spinlock_release(&block_queue_lock);
        return OK;
    }
    if (block_device_count >= BLOCK_MAX_DEVICES) {
        spinlock_release(&block_queue_lock);
        LOG_ERROR("BLK", "Limite de dispositivos de bloco atingido");
        return ERR_OVERFLOW;
    }
    block_devices[block_device_count++] = normalized;
    spinlock_release(&block_queue_lock);
    return OK;
}

int block_unregister(const char* id) {
    int index;
    int cache_result;

    if (!id) {
        LOG_ERROR("BLK", "ID nulo ao remover dispositivo de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Remocao de bloco antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&block_queue_lock);
    if (block_queue_count || block_active_count) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Remocao recusada com requisicoes pendentes");
        return ERR_STATE;
    }
    index = block_index(id);
    if (index < 0) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Dispositivo de bloco nao encontrado");
        return ERR_NOT_FOUND;
    }
    cache_result = block_cache_invalidate_device(id);
    if (cache_result != OK) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Remocao recusada com cache ocupado");
        return cache_result;
    }
    for (uint32_t current = (uint32_t)index;
         current + 1U < block_device_count; current++) {
        block_devices[current] = block_devices[current + 1U];
    }
    block_device_count--;
    kmemset(&block_devices[block_device_count], 0,
            sizeof(block_devices[block_device_count]));
    spinlock_release(&block_queue_lock);
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
    spinlock_acquire(&block_queue_lock);
    *out_count = block_device_count;
    spinlock_release(&block_queue_lock);
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
    spinlock_acquire(&block_queue_lock);
    if (index >= block_device_count) {
        spinlock_release(&block_queue_lock);
        LOG_ERROR("BLK", "Indice de bloco invalido");
        return ERR_INVALID;
    }
    *out_device = block_devices[index];
    spinlock_release(&block_queue_lock);
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
    spinlock_acquire(&block_queue_lock);
    index = block_index(id);
    if (index < 0) {
        spinlock_release(&block_queue_lock);
        return ERR_NOT_FOUND;
    }
    *out_device = block_devices[index];
    spinlock_release(&block_queue_lock);
    return OK;
}

int block_dispatch(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;
    uint32_t pending;
    uint8_t had_work;
    int result = OK;

    if (!out_processed) {
        LOG_ERROR("BLK", "Destino nulo no despacho de bloco");
        return ERR_NULL;
    }
    *out_processed = 0U;
    if (!block_initialized) {
        LOG_ERROR("BLK", "Despacho de bloco antes da inicializacao");
        return ERR_STATE;
    }
    if (!budget) {
        LOG_ERROR("BLK", "Orcamento nulo no despacho de bloco");
        return ERR_INVALID;
    }
    spinlock_acquire(&block_queue_lock);
    if (block_dispatching) {
        spinlock_release(&block_queue_lock);
        LOG_WARN("BLK", "Despacho de bloco ja esta em andamento");
        return ERR_STATE;
    }
    block_dispatching = 1U;
    spinlock_release(&block_queue_lock);

    while (processed < budget) {
        result = block_dispatch_one(&had_work);
        if (result != OK) break;
        if (!had_work) break;
        processed++;
    }
    spinlock_acquire(&block_queue_lock);
    block_dispatching = 0U;
    pending = block_queue_count;
    spinlock_release(&block_queue_lock);
    if (pending && block_work_ready) {
        int schedule_result = schedule_work(&block_work);

        if (schedule_result != OK) {
            LOG_WARN("BLK", "Fila pendente sem novo agendamento de trabalho");
        }
    }
    *out_processed = processed;
    return result;
}

int block_submit(bio_request_t* request) {
    return block_queue_enqueue(request, 1U);
}

int block_submit_sync(bio_request_t* request) {
    uint32_t processed;
    int result;

    result = block_queue_enqueue(request, 0U);
    if (result != OK) return result;
    while (request->state == BLOCK_REQUEST_QUEUED ||
           request->state == BLOCK_REQUEST_IN_FLIGHT) {
        result = block_dispatch(BLOCK_DISPATCH_BUDGET, &processed);
        if (result != OK) {
            LOG_ERROR("BLK", "Despacho sincrono de BIO falhou");
            return result;
        }
        if (!processed) {
            LOG_ERROR("BLK", "BIO sincrono permaneceu pendente");
            return ERR_STATE;
        }
    }
    return request->status;
}

int block_cancel(bio_request_t* request) {
    uint32_t index;

    if (!request) {
        LOG_ERROR("BLK", "BIO nulo no cancelamento de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Cancelamento de bloco antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&block_queue_lock);
    for (index = 0U; index < block_queue_count; index++) {
        if (block_queue[index].bio == request) break;
    }
    if (index < block_queue_count) {
        block_queue_remove_locked(index);
        block_queue_stats.cancelled++;
        block_queue_stats.last_error = ERR_CANCELLED;
        spinlock_release(&block_queue_lock);
        block_request_finish(request, BLOCK_REQUEST_CANCELLED, 0U,
                             ERR_CANCELLED);
        return OK;
    }
    for (index = 0U; index < block_active_count; index++) {
        if (block_active_bios[index] == request) {
            spinlock_release(&block_queue_lock);
            LOG_WARN("BLK", "Cancelamento recusado para BIO em voo");
            return ERR_STATE;
        }
    }
    spinlock_release(&block_queue_lock);
    LOG_WARN("BLK", "BIO nao encontrado no cancelamento de bloco");
    return ERR_NOT_FOUND;
}

static uint32_t block_divide_word(uint32_t word, uint32_t divisor,
                                  uint32_t* remainder) {
    uint32_t quotient = 0U;

    for (uint32_t bit = 32U; bit > 0U; bit--) {
        uint32_t carry = *remainder >> 31U;
        uint32_t shifted = (*remainder << 1U) |
                           ((word >> (bit - 1U)) & 1U);
        if (carry || shifted >= divisor) {
            *remainder = shifted - divisor;
            quotient |= 1U << (bit - 1U);
        } else {
            *remainder = shifted;
        }
    }
    return quotient;
}

static uint64_t block_divide_u64(uint64_t value, uint32_t divisor) {
    uint32_t high = (uint32_t)(value >> 32U);
    uint32_t low = (uint32_t)value;
    uint32_t quotient_high;
    uint32_t quotient_low;
    uint32_t remainder = 0U;

    if (!divisor) return 0U;
    quotient_high = block_divide_word(high, divisor, &remainder);
    quotient_low = block_divide_word(low, divisor, &remainder);
    return ((uint64_t)quotient_high << 32U) | quotient_low;
}

static uint32_t block_rate(uint32_t sectors, uint32_t now,
                           uint32_t frequency) {
    uint32_t elapsed = now - block_stats_start_tick;
    uint64_t rate;

    if (!elapsed || !frequency) return 0U;
    rate = block_divide_u64((uint64_t)sectors * frequency, elapsed);
    return rate > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)rate;
}

int block_get_stats(block_queue_stats_t* out_stats) {
    block_queue_stats_t snapshot;
    uint32_t now;
    uint32_t frequency;

    if (!out_stats) {
        LOG_ERROR("BLK", "Destino nulo nas metricas de bloco");
        return ERR_NULL;
    }
    if (!block_initialized) {
        LOG_ERROR("BLK", "Metricas de bloco antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&block_queue_lock);
    snapshot = block_queue_stats;
    snapshot.queue_depth = block_queue_count;
    snapshot.in_flight = block_active_count;
    spinlock_release(&block_queue_lock);
    now = timer_get_ticks();
    frequency = timer_get_frequency();
    snapshot.read_sectors_per_second =
        block_rate(snapshot.read_sectors, now, frequency);
    snapshot.write_sectors_per_second =
        block_rate(snapshot.write_sectors, now, frequency);
    *out_stats = snapshot;
    return OK;
}

static int block_read_backend(const char* id, uint32_t lba, uint8_t count,
                              uint8_t* buffer) {
    bio_request_t request;

    kmemset(&request, 0, sizeof(request));
    request.device_id = id;
    request.lba = lba;
    request.sector_count = count;
    request.buffer = buffer;
    request.buffer_bytes = (uint32_t)count * BLOCK_SECTOR_SIZE;
    request.operation = BLOCK_OPERATION_READ;
    return block_submit_sync(&request);
}

int block_read(const char* id, uint32_t lba, uint8_t count,
               uint8_t* buffer) {
    block_device_t device;
    int result;

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
    result = block_find(id, &device);
    if (result != OK) return result;
    return block_cache_read(&device, lba, count, buffer,
                            (uint32_t)count * BLOCK_SECTOR_SIZE,
                            block_read_backend);
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

static int block_self_test_submit(block_request_t* request) {
    block_self_test_context_t* test;

    if (!request || !request->device_context) {
        LOG_WARN("BLK", "Requisicao fisica invalida no backend de teste");
        return ERR_NULL;
    }
    test = (block_self_test_context_t*)request->device_context;
    test->submit_calls++;
    if (request->operation == BLOCK_OPERATION_READ) {
        if (!request->buffer || !request->sector_count) {
            LOG_WARN("BLK", "Buffer invalido no autoteste de leitura");
            return ERR_NULL;
        }
        test->read_calls++;
        if (test->forced_result == OK) {
            kmemset(request->buffer, BLOCK_SELF_TEST_PATTERN,
                    request->sector_count * BLOCK_SECTOR_SIZE);
        }
    } else if (request->operation == BLOCK_OPERATION_WRITE) {
        if (!request->buffer || !request->sector_count) {
            LOG_WARN("BLK", "Buffer invalido no autoteste de escrita");
            return ERR_NULL;
        }
        test->write_calls++;
    }
    if (test->forced_result != OK) return test->forced_result;
    request->completed_sectors = request->sector_count;
    return OK;
}

static void block_self_test_completion(bio_request_t* request, void* context) {
    block_self_test_context_t* test = (block_self_test_context_t*)context;

    if (request && test) {
        test->completion_calls++;
        if (test->callback_order_count < BLOCK_QUEUE_CAPACITY) {
            test->callback_order[test->callback_order_count++] = request->lba;
        }
    }
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
    bio_request_t queued[BLOCK_QUEUE_CAPACITY + 1U];
    static uint8_t buffer[BLOCK_SECTOR_SIZE * BLOCK_SELF_TEST_BUFFER_SECTORS];
    static uint8_t queue_buffers[BLOCK_QUEUE_CAPACITY + 1U]
                                [BLOCK_SECTOR_SIZE];
    static uint8_t fusion_buffer[BLOCK_SECTOR_SIZE * 2U];
    static uint8_t separate_buffers[2U][BLOCK_SECTOR_SIZE * 2U];
    uint32_t initial_device_count;
    uint32_t processed;
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
    mock.ops.submit = block_self_test_submit;
    kmemset(&context, 0, sizeof(context));
    if (block_index(mock.id) >= 0) {
        LOG_ERROR("BLK", "ID reservado do autoteste ja esta em uso");
        return ERR_STATE;
    }
    result = block_register(&mock);
    if (result != OK) {
        LOG_ERROR("BLK", "Falha ao registrar backend de autoteste");
        return result;
    }

    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_sync(&request);
    if (result != OK || block_self_test_expect(
            &request, BLOCK_REQUEST_COMPLETED, OK, 1U) != OK ||
        buffer[0] != BLOCK_SELF_TEST_PATTERN) {
        LOG_ERROR("BLK", "Autoteste de leitura de BIO falhou");
        goto block_self_test_fail;
    }

    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_WRITE;
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_sync(&request);
    if (result != OK || block_self_test_expect(
            &request, BLOCK_REQUEST_COMPLETED, OK, 1U) != OK) {
        LOG_ERROR("BLK", "Autoteste de escrita de BIO falhou");
        goto block_self_test_fail;
    }

    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = BLOCK_SELF_TEST_MAX_TRANSFER + 1U;
    request.buffer = buffer;
    request.buffer_bytes = sizeof(buffer);
    result = block_submit_sync(&request);
    if (result != ERR_OVERFLOW || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_OVERFLOW, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de limite de BIO falhou");
        goto block_self_test_fail;
    }

    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_SECTOR_COUNT - 1U;
    request.sector_count = 2U;
    request.buffer = buffer;
    request.buffer_bytes = sizeof(buffer);
    result = block_submit_sync(&request);
    if (result != ERR_DISK || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_DISK, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de LBA de BIO falhou");
        goto block_self_test_fail;
    }

    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_FLUSH;
    result = block_submit_sync(&request);
    if (result != ERR_UNAVAILABLE || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_UNAVAILABLE, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de flush de BIO falhou");
        goto block_self_test_fail;
    }

    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_WRITE;
    request.flags = BLOCK_BIO_FLAG_FUA;
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_sync(&request);
    if (result != ERR_UNAVAILABLE || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_UNAVAILABLE, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de FUA de BIO falhou");
        goto block_self_test_fail;
    }

    mock.read_only = 1U;
    if (block_register(&mock) != OK) {
        LOG_ERROR("BLK", "Falha ao preparar autoteste somente-leitura");
        goto block_self_test_fail;
    }
    block_self_test_prepare(&request, &context);
    request.operation = BLOCK_OPERATION_WRITE;
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_sync(&request);
    mock.read_only = 0U;
    if (block_register(&mock) != OK) {
        LOG_ERROR("BLK", "Falha ao restaurar backend de autoteste");
        goto block_self_test_fail;
    }
    if (result != ERR_UNAVAILABLE || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_UNAVAILABLE, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de somente leitura falhou");
        goto block_self_test_fail;
    }

    context.forced_result = ERR_TIMEOUT;
    block_self_test_prepare(&request, &context);
    request.lba = BLOCK_SELF_TEST_LBA;
    request.sector_count = 1U;
    request.buffer = buffer;
    request.buffer_bytes = BLOCK_SECTOR_SIZE;
    result = block_submit_sync(&request);
    if (result != ERR_TIMEOUT || block_self_test_expect(
            &request, BLOCK_REQUEST_ERROR, ERR_TIMEOUT, 0U) != OK) {
        LOG_ERROR("BLK", "Autoteste de conclusao de BIO falhou");
        goto block_self_test_fail;
    }
    context.forced_result = OK;

    if (block_work_ready) {
        bio_request_t first;
        bio_request_t second;

        kmemset(&context, 0, sizeof(context));
        block_self_test_prepare(&first, &context);
        first.lba = BLOCK_SELF_TEST_LBA + 2U;
        first.sector_count = 1U;
        first.buffer = fusion_buffer;
        first.buffer_bytes = BLOCK_SECTOR_SIZE;
        block_self_test_prepare(&second, &context);
        second.lba = first.lba + 1U;
        second.sector_count = 1U;
        second.buffer = fusion_buffer + BLOCK_SECTOR_SIZE;
        second.buffer_bytes = BLOCK_SECTOR_SIZE;
        if (block_submit(&first) != OK || block_submit(&second) != OK ||
            block_dispatch(BLOCK_DISPATCH_BUDGET, &processed) != OK ||
            context.submit_calls != 1U ||
            context.completion_calls != 2U ||
            block_self_test_expect(&first, BLOCK_REQUEST_COMPLETED, OK, 1U) !=
                OK ||
            block_self_test_expect(&second, BLOCK_REQUEST_COMPLETED, OK, 1U) !=
                OK) {
            LOG_ERROR("BLK", "Autoteste de fusao de BIO falhou");
            goto block_self_test_fail;
        }
        if (block_dispatch(BLOCK_DISPATCH_BUDGET, &processed) != OK ||
            processed != 0U || context.completion_calls != 2U) {
            LOG_ERROR("BLK", "Autoteste de callback unico falhou");
            goto block_self_test_fail;
        }

        kmemset(&context, 0, sizeof(context));
        block_self_test_prepare(&first, &context);
        first.lba = BLOCK_SELF_TEST_LBA + 4U;
        first.sector_count = 1U;
        first.buffer = separate_buffers[0];
        first.buffer_bytes = BLOCK_SECTOR_SIZE;
        block_self_test_prepare(&second, &context);
        second.lba = first.lba + 1U;
        second.sector_count = 1U;
        second.buffer = separate_buffers[1];
        second.buffer_bytes = BLOCK_SECTOR_SIZE;
        if (block_submit(&first) != OK || block_submit(&second) != OK ||
            block_dispatch(BLOCK_DISPATCH_BUDGET, &processed) != OK ||
            context.submit_calls != 2U || context.completion_calls != 2U) {
            LOG_ERROR("BLK", "Autoteste de nao fusao de BIO falhou");
            goto block_self_test_fail;
        }

        kmemset(&context, 0, sizeof(context));
        for (uint32_t index = 0U; index < 3U; index++) {
            block_self_test_prepare(&queued[index], &context);
            queued[index].lba = BLOCK_SELF_TEST_LBA + 8U + index * 2U;
            queued[index].sector_count = 1U;
            queued[index].buffer = queue_buffers[index];
            queued[index].buffer_bytes = BLOCK_SECTOR_SIZE;
            if (block_submit(&queued[index]) != OK) {
                goto block_self_test_fail;
            }
        }
        if (block_dispatch(BLOCK_DISPATCH_BUDGET, &processed) != OK ||
            context.callback_order_count != 3U ||
            context.completion_calls != 3U ||
            context.callback_order[0] != queued[0].lba ||
            context.callback_order[1] != queued[1].lba ||
            context.callback_order[2] != queued[2].lba) {
            LOG_ERROR("BLK", "Autoteste de FIFO falhou");
            goto block_self_test_fail;
        }

        kmemset(&context, 0, sizeof(context));
        block_self_test_prepare(&queued[0], &context);
        queued[0].lba = 20U;
        queued[0].sector_count = 1U;
        queued[0].buffer = queue_buffers[0];
        queued[0].buffer_bytes = BLOCK_SECTOR_SIZE;
        block_self_test_prepare(&queued[1], &context);
        queued[1].lba = 22U;
        queued[1].sector_count = 1U;
        queued[1].buffer = queue_buffers[1];
        queued[1].buffer_bytes = BLOCK_SECTOR_SIZE;
        if (block_submit(&queued[0]) != OK || block_submit(&queued[1]) != OK ||
            block_cancel(&queued[1]) != OK ||
            block_dispatch(BLOCK_DISPATCH_BUDGET, &processed) != OK ||
            queued[1].state != BLOCK_REQUEST_CANCELLED ||
            queued[1].status != ERR_CANCELLED ||
            context.completion_calls != 2U) {
            LOG_ERROR("BLK", "Autoteste de cancelamento falhou");
            goto block_self_test_fail;
        }

        kmemset(&context, 0, sizeof(context));
        for (uint32_t index = 0U; index < BLOCK_QUEUE_CAPACITY; index++) {
            block_self_test_prepare(&queued[index], &context);
            queued[index].lba = 32U + index;
            queued[index].sector_count = 1U;
            queued[index].buffer = queue_buffers[index];
            queued[index].buffer_bytes = BLOCK_SECTOR_SIZE;
            if (block_submit(&queued[index]) != OK) {
                LOG_ERROR("BLK", "Autoteste de capacidade de fila falhou");
                goto block_self_test_fail;
            }
        }
        block_self_test_prepare(&queued[BLOCK_QUEUE_CAPACITY], &context);
        queued[BLOCK_QUEUE_CAPACITY].lba = 96U;
        queued[BLOCK_QUEUE_CAPACITY].sector_count = 1U;
        queued[BLOCK_QUEUE_CAPACITY].buffer = queue_buffers[BLOCK_QUEUE_CAPACITY];
        queued[BLOCK_QUEUE_CAPACITY].buffer_bytes = BLOCK_SECTOR_SIZE;
        if (block_submit(&queued[BLOCK_QUEUE_CAPACITY]) != ERR_OVERFLOW ||
            queued[BLOCK_QUEUE_CAPACITY].state != BLOCK_REQUEST_ERROR) {
            LOG_ERROR("BLK", "Autoteste de overflow de fila falhou");
            goto block_self_test_fail;
        }
        for (uint32_t index = 0U; index < BLOCK_QUEUE_CAPACITY; index++) {
            if (block_cancel(&queued[index]) != OK) {
                goto block_self_test_fail;
            }
        }
    } else {
        block_self_test_prepare(&request, &context);
        request.lba = BLOCK_SELF_TEST_LBA;
        request.sector_count = 1U;
        request.buffer = buffer;
        request.buffer_bytes = BLOCK_SECTOR_SIZE;
        result = block_submit(&request);
        if (result != ERR_UNAVAILABLE ||
            block_self_test_expect(&request, BLOCK_REQUEST_ERROR,
                                   ERR_UNAVAILABLE, 0U) != OK) {
            LOG_ERROR("BLK", "Autoteste de modo assincrono indisponivel falhou");
            goto block_self_test_fail;
        }
    }
    {
        block_queue_stats_t stats;

        if (block_get_stats(&stats) != OK || stats.queue_depth != 0U ||
            stats.in_flight != 0U) {
            LOG_ERROR("BLK", "Autoteste deixou fila de bloco pendente");
            goto block_self_test_fail;
        }
    }
    result = block_unregister(mock.id);
    if (result != OK || block_device_count != initial_device_count ||
        block_validate_state() != OK) {
        LOG_ERROR("BLK", "Autoteste alterou o inventario de bloco");
        return ERR_STATE;
    }
    if (block_cache_self_test() != OK) {
        LOG_ERROR("BLK", "Autoteste do cache de blocos falhou");
        return ERR_STATE;
    }
    return OK;

block_self_test_fail:
    (void)block_unregister(mock.id);
    return ERR_STATE;
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
            (!device->ops.read && !device->ops.submit) ||
            device->sector_size != BLOCK_SECTOR_SIZE ||
            !device->sector_count ||
            device->provider > BLOCK_PROVIDER_USB_MSC ||
            !device->max_transfer_sectors ||
            device->max_transfer_sectors > BLOCK_MAX_TRANSFER_SECTORS ||
            (device->capabilities & ~BLOCK_DEVICE_CAPABILITIES_SUPPORTED) != 0U ||
            ((device->capabilities & BLOCK_DEVICE_CAP_FLUSH) != 0U &&
             !device->ops.flush && !device->ops.submit) ||
            ((device->capabilities & BLOCK_DEVICE_CAP_FUA) != 0U &&
             (device->read_only ||
              (!device->ops.write_flags && !device->ops.submit))) ||
            (!device->read_only && !device->ops.write &&
             !device->ops.write_flags && !device->ops.submit)) {
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
    spinlock_acquire(&block_queue_lock);
    if (block_queue_count > BLOCK_QUEUE_CAPACITY ||
        block_queue_stats.queue_capacity != BLOCK_QUEUE_CAPACITY ||
        block_queue_stats.queue_depth != block_queue_count ||
        block_active_count > BLOCK_QUEUE_CAPACITY ||
        block_queue_stats.peak_depth > BLOCK_QUEUE_CAPACITY ||
        block_queue_stats.completed + block_queue_stats.failed +
                block_queue_stats.cancelled > block_queue_stats.submitted) {
        spinlock_release(&block_queue_lock);
        LOG_ERROR("BLK", "Fila de bloco inconsistente");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < block_queue_count; index++) {
        block_queue_entry_t* entry = &block_queue[index];

        if (!entry->bio || entry->device_index >= block_device_count ||
            !block_text_equal(entry->device_id,
                              block_devices[entry->device_index].id) ||
            entry->bio->state != BLOCK_REQUEST_QUEUED ||
            block_validate_bio(&block_devices[entry->device_index],
                               entry->bio) != OK) {
            spinlock_release(&block_queue_lock);
            LOG_ERROR("BLK", "Entrada invalida na fila de bloco");
            return ERR_STATE;
        }
        for (uint32_t other = 0U; other < index; other++) {
            if (block_queue[other].bio == entry->bio) {
                spinlock_release(&block_queue_lock);
                LOG_ERROR("BLK", "BIO duplicado na fila de bloco");
                return ERR_STATE;
            }
        }
    }
    for (uint32_t index = 0U; index < block_active_count; index++) {
        if (!block_active_bios[index] ||
            block_active_bios[index]->state != BLOCK_REQUEST_IN_FLIGHT) {
            spinlock_release(&block_queue_lock);
            LOG_ERROR("BLK", "BIO invalido em voo na camada de bloco");
            return ERR_STATE;
        }
        if (!block_text_terminated(block_active_bios[index]->device_id,
                                   BLOCK_DEVICE_ID_SIZE) ||
            !block_text_equal(block_active_device_id,
                              block_active_bios[index]->device_id)) {
            spinlock_release(&block_queue_lock);
            LOG_ERROR("BLK", "BIO em voo aponta para dispositivo invalido");
            return ERR_STATE;
        }
        for (uint32_t other = 0U; other < index; other++) {
            if (block_active_bios[other] == block_active_bios[index]) {
                spinlock_release(&block_queue_lock);
                LOG_ERROR("BLK", "BIO duplicado em voo na camada de bloco");
                return ERR_STATE;
            }
        }
    }
    if (block_active_count && !block_active_device_id[0]) {
        spinlock_release(&block_queue_lock);
        LOG_ERROR("BLK", "Dispositivo ativo ausente na camada de bloco");
        return ERR_STATE;
    }
    spinlock_release(&block_queue_lock);
    return block_cache_validate_state();
}
