#include "fs/devfs.h"
#include "fs/vfs_internal.h"
#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "drivers/speaker.h"
#include "fs/block_cache.h"

#define DEVFS_HDA_BATCH_SECTORS 8U
#define DEVFS_UINT32_MAX 0xFFFFFFFFU

typedef struct {
    const char* name;
    vfs_node_type_t type;
    uint8_t readable;
    uint8_t writable;
} devfs_node_t;

static const devfs_node_t devfs_nodes[DEVFS_MAX_NODES] = {
    {"null", VFS_NODE_CHAR_DEVICE, 1U, 1U},
    {"zero", VFS_NODE_CHAR_DEVICE, 1U, 1U},
    {"tty", VFS_NODE_CHAR_DEVICE, 1U, 1U},
    {"speaker", VFS_NODE_CHAR_DEVICE, 0U, 1U},
    {"hda", VFS_NODE_BLOCK_DEVICE, 1U, 0U}
};

static spinlock_t devfs_lock;
static devfs_status_t devfs_metrics;
static uint8_t devfs_ready;
static vfs_descriptor_info_t devfs_test_descriptors[VFS_MAX_FDS];
static uint8_t devfs_test_buffer[BLOCK_SECTOR_SIZE];
static devfs_node_info_t devfs_test_nodes[DEVFS_MAX_NODES];

static int devfs_generic_open(vnode_t* vnode, file_t* file);
static int devfs_generic_close(file_t* file);
static int devfs_null_read(file_t* file, void* buffer, uint32_t size,
                           uint32_t* bytes_read);
static int devfs_null_write(file_t* file, const void* buffer, uint32_t size,
                            uint32_t* bytes_written);
static int devfs_zero_read(file_t* file, void* buffer, uint32_t size,
                           uint32_t* bytes_read);
static int devfs_speaker_write(file_t* file, const void* buffer, uint32_t size,
                               uint32_t* bytes_written);
static int devfs_speaker_ioctl(file_t* file, uint32_t request, void* argument);
static int devfs_hda_read(file_t* file, void* buffer, uint32_t size,
                          uint32_t* bytes_read);
static int devfs_hda_lseek(file_t* file, int32_t offset, uint32_t whence,
                           uint32_t* position);
static int devfs_unavailable_read(file_t* file, void* buffer, uint32_t size,
                                  uint32_t* bytes_read);
static int devfs_unavailable_write(file_t* file, const void* buffer,
                                   uint32_t size, uint32_t* bytes_written);
static int devfs_unavailable_lseek(file_t* file, int32_t offset,
                                   uint32_t whence, uint32_t* position);
static int devfs_unavailable_ioctl(file_t* file, uint32_t request,
                                   void* argument);
static int devfs_unavailable_sync(file_t* file);
static int devfs_hda_sync(file_t* file);

static const file_operations_t devfs_null_operations = {
    devfs_generic_open, devfs_null_read, devfs_null_write,
    devfs_generic_close, devfs_unavailable_lseek, devfs_unavailable_ioctl,
    devfs_unavailable_sync
};
static const file_operations_t devfs_zero_operations = {
    devfs_generic_open, devfs_zero_read, devfs_null_write,
    devfs_generic_close, devfs_unavailable_lseek, devfs_unavailable_ioctl,
    devfs_unavailable_sync
};
static const file_operations_t devfs_tty_operations = {
    devfs_generic_open, vfs_stream_read, vfs_stream_write,
    devfs_generic_close, devfs_unavailable_lseek, devfs_unavailable_ioctl,
    devfs_unavailable_sync
};
static const file_operations_t devfs_speaker_operations = {
    devfs_generic_open, devfs_unavailable_read, devfs_speaker_write,
    devfs_generic_close, devfs_unavailable_lseek, devfs_speaker_ioctl,
    devfs_unavailable_sync
};
static const file_operations_t devfs_hda_operations = {
    devfs_generic_open, devfs_hda_read, devfs_unavailable_write,
    devfs_generic_close, devfs_hda_lseek, devfs_unavailable_ioctl,
    devfs_hda_sync
};

static void devfs_copy_text(char* destination, uint32_t capacity,
                            const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    while (source && source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int devfs_path_node(const char* path) {
    const char* name;

    if (!path || path[0] != '/' || path[1] != 'd' || path[2] != 'e' ||
        path[3] != 'v') return -1;
    if (!path[4]) return -2;
    if (path[4] != '/' || !path[5]) return -1;
    name = path + 5U;
    for (uint32_t index = 0U; index < DEVFS_MAX_NODES; index++) {
        if (kstrcmp(name, devfs_nodes[index].name) == 0) return (int)index;
    }
    return -1;
}

static int devfs_find_hda(block_device_t* device) {
    uint32_t count = 0U;
    int result;

    if (!device) {
        LOG_ERROR("FS", "Destino de descoberta hda nulo");
        return ERR_NULL;
    }
    result = block_get_count(&count);
    if (result != OK) return result;
    for (uint32_t index = 0U; index < count; index++) {
        result = block_get_at(index, device);
        if (result == OK && device->online &&
            device->provider == BLOCK_PROVIDER_ATA) return OK;
    }
    LOG_WARN("FS", "Dispositivo ATA para devfs nao encontrado");
    return ERR_NOT_FOUND;
}

static uint32_t devfs_hda_capacity(const block_device_t* device) {
    if (!device || device->sector_count > DEVFS_UINT32_MAX / BLOCK_SECTOR_SIZE) {
        return DEVFS_UINT32_MAX;
    }
    return device->sector_count * BLOCK_SECTOR_SIZE;
}

static void devfs_metric(uint32_t* metric, int result) {
    spinlock_acquire(&devfs_lock);
    if (result == OK && metric) (*metric)++;
    else if (result != OK) devfs_metrics.failures++;
    spinlock_release(&devfs_lock);
}

int devfs_init(void) {
    LOG_INFO("FS", "Inicializando devfs");
    if (devfs_ready) {
        LOG_WARN("FS", "Devfs ja estava inicializado");
        return OK;
    }
    spinlock_init(&devfs_lock);
    kmemset(&devfs_metrics, 0, sizeof(devfs_metrics));
    devfs_metrics.initialized = 1U;
    devfs_metrics.capacity = DEVFS_MAX_NODES;
    devfs_metrics.active = DEVFS_MAX_NODES;
    devfs_ready = 1U;
    LOG_INFO("FS", "Devfs inicializado com sucesso");
    return OK;
}

int devfs_is_ready(void) {
    return devfs_ready;
}

int devfs_lookup(const char* canonical_path, vfs_lookup_result_t* result) {
    block_device_t device;
    int node;

    if (!canonical_path || !result) {
        LOG_ERROR("FS", "Lookup devfs recebeu argumento nulo");
        return ERR_NULL;
    }
    if (!devfs_ready) {
        LOG_ERROR("FS", "Lookup solicitado com devfs indisponivel");
        return ERR_STATE;
    }
    node = devfs_path_node(canonical_path);
    if (node == -2) {
        result->type = VFS_NODE_DIRECTORY;
        return OK;
    }
    if (node < 0) {
        LOG_WARN("FS", "No devfs solicitado nao foi encontrado");
        return ERR_NOT_FOUND;
    }
    result->type = devfs_nodes[node].type;
    result->size = 0U;
    if ((devfs_node_id_t)node == DEVFS_NODE_HDA &&
        devfs_find_hda(&device) == OK) {
        result->size = devfs_hda_capacity(&device);
    }
    return OK;
}

static const file_operations_t* devfs_operations(devfs_node_id_t node_id) {
    if (node_id == DEVFS_NODE_NULL) return &devfs_null_operations;
    if (node_id == DEVFS_NODE_ZERO) return &devfs_zero_operations;
    if (node_id == DEVFS_NODE_TTY) return &devfs_tty_operations;
    if (node_id == DEVFS_NODE_SPEAKER) return &devfs_speaker_operations;
    if (node_id == DEVFS_NODE_HDA) return &devfs_hda_operations;
    return 0;
}

int devfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                    vnode_t* vnode, file_t* file,
                    devfs_file_context_t* context) {
    block_device_t device;
    const file_operations_t* operations;
    int node;
    int result = OK;

    if (!lookup || !vnode || !file || !context) {
        LOG_ERROR("FS", "Abertura devfs recebeu argumento nulo");
        return ERR_NULL;
    }
    node = devfs_path_node(lookup->canonical_path);
    if (node < 0) {
        LOG_WARN("FS", "No devfs ausente durante abertura");
        return ERR_NOT_FOUND;
    }
    if ((mode & VFS_MODE_READ) && !devfs_nodes[node].readable) {
        result = ERR_UNAVAILABLE;
    }
    if ((mode & VFS_MODE_WRITE) && !devfs_nodes[node].writable) {
        result = ERR_UNAVAILABLE;
    }
    if (result != OK) {
        devfs_metric(0, result);
        return result;
    }
    operations = devfs_operations((devfs_node_id_t)node);
    if (!operations) {
        LOG_ERROR("FS", "No devfs sem operacoes registradas");
        return ERR_STATE;
    }
    kmemset(context, 0, sizeof(*context));
    context->node_id = (devfs_node_id_t)node;
    context->mount_slot = lookup->mount_slot;
    context->mount_generation = lookup->mount_generation;
    if (context->node_id == DEVFS_NODE_HDA) {
        result = devfs_find_hda(&device);
        if (result != OK) {
            devfs_metric(0, result);
            return result;
        }
        devfs_copy_text(context->block_id, sizeof(context->block_id), device.id);
        context->capacity = devfs_hda_capacity(&device);
    }
    vnode->type = devfs_nodes[node].type;
    vnode->operations = operations;
    vnode->private_data = context;
    vnode->size = context->node_id == DEVFS_NODE_HDA ? context->capacity : 0U;
    devfs_copy_text(vnode->path, VFS_MAX_PATH, lookup->canonical_path);
    file->mode = mode;
    result = operations->open(vnode, file);
    devfs_metric(&devfs_metrics.opens, result);
    return result;
}

int devfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
               uint32_t* out_count) {
    block_device_t device;

    if (!entries || !out_count) {
        LOG_ERROR("FS", "Destino de listagem devfs nulo");
        return ERR_NULL;
    }
    if (capacity < DEVFS_MAX_NODES) {
        LOG_ERROR("FS", "Capacidade de listagem devfs insuficiente");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < DEVFS_MAX_NODES; index++) {
        kmemset(&entries[index], 0, sizeof(entries[index]));
        devfs_copy_text(entries[index].name, sizeof(entries[index].name),
                        devfs_nodes[index].name);
        entries[index].type = devfs_nodes[index].type;
        if (index == DEVFS_NODE_HDA && devfs_find_hda(&device) == OK) {
            entries[index].size = devfs_hda_capacity(&device);
        }
    }
    *out_count = DEVFS_MAX_NODES;
    return OK;
}

int devfs_copy_nodes(devfs_node_info_t* output, uint32_t capacity,
                     uint32_t* out_count) {
    block_device_t device;

    if (!output || !out_count) {
        LOG_ERROR("FS", "Destino de snapshot devfs nulo");
        return ERR_NULL;
    }
    if (capacity < DEVFS_MAX_NODES) {
        LOG_ERROR("FS", "Capacidade de snapshot devfs insuficiente");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < DEVFS_MAX_NODES; index++) {
        kmemset(&output[index], 0, sizeof(output[index]));
        devfs_copy_text(output[index].name, sizeof(output[index].name),
                        devfs_nodes[index].name);
        output[index].type = devfs_nodes[index].type;
        output[index].readable = devfs_nodes[index].readable;
        output[index].writable = devfs_nodes[index].writable;
        output[index].available = index != DEVFS_NODE_HDA ||
                                  devfs_find_hda(&device) == OK;
    }
    *out_count = DEVFS_MAX_NODES;
    return OK;
}

int devfs_get_status(devfs_status_t* status) {
    if (!status) {
        LOG_ERROR("FS", "Destino de status devfs nulo");
        return ERR_NULL;
    }
    if (!devfs_ready) {
        LOG_ERROR("FS", "Status solicitado com devfs indisponivel");
        return ERR_STATE;
    }
    spinlock_acquire(&devfs_lock);
    *status = devfs_metrics;
    spinlock_release(&devfs_lock);
    return OK;
}

static int devfs_generic_open(vnode_t* vnode, file_t* file) {
    if (!vnode || !file || !vnode->private_data) {
        LOG_ERROR("FS", "Abertura de dispositivo recebeu estado nulo");
        return ERR_NULL;
    }
    file->offset = 0U;
    return OK;
}

static int devfs_generic_close(file_t* file) {
    devfs_file_context_t* context;

    if (!file || !file->vnode) {
        LOG_ERROR("FS", "Fechamento de dispositivo recebeu arquivo nulo");
        return ERR_NULL;
    }
    context = (devfs_file_context_t*)file->vnode->private_data;
    if (!context) {
        LOG_ERROR("FS", "Fechamento de dispositivo sem contexto");
        return ERR_STATE;
    }
    vfs_mount_release(context->mount_slot, context->mount_generation);
    return OK;
}

static int devfs_null_read(file_t* file, void* buffer, uint32_t size,
                           uint32_t* bytes_read) {
    (void)file;
    (void)buffer;
    (void)size;
    if (!bytes_read) {
        LOG_ERROR("FS", "Leitura de null sem contador");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    devfs_metric(&devfs_metrics.reads, OK);
    return OK;
}

static int devfs_null_write(file_t* file, const void* buffer, uint32_t size,
                            uint32_t* bytes_written) {
    (void)file;
    (void)buffer;
    if (!bytes_written) {
        LOG_ERROR("FS", "Escrita de dispositivo sem contador");
        return ERR_NULL;
    }
    *bytes_written = size;
    devfs_metric(&devfs_metrics.writes, OK);
    return OK;
}

static int devfs_zero_read(file_t* file, void* buffer, uint32_t size,
                           uint32_t* bytes_read) {
    (void)file;
    if (!bytes_read || (size && !buffer)) {
        LOG_ERROR("FS", "Leitura de zero recebeu argumento nulo");
        return ERR_NULL;
    }
    if (size) kmemset(buffer, 0, size);
    *bytes_read = size;
    devfs_metric(&devfs_metrics.reads, OK);
    return OK;
}

static int devfs_tone_valid(const app_speaker_tone_t* tone) {
    return tone && tone->frequency_hz >= DEVFS_SPEAKER_MIN_HZ &&
           tone->frequency_hz <= DEVFS_SPEAKER_MAX_HZ &&
           tone->duration_ms >= DEVFS_SPEAKER_MIN_MS &&
           tone->duration_ms <= DEVFS_SPEAKER_MAX_MS;
}

static int devfs_speaker_write(file_t* file, const void* buffer, uint32_t size,
                               uint32_t* bytes_written) {
    const app_speaker_tone_t* tone = (const app_speaker_tone_t*)buffer;
    int result = OK;

    (void)file;
    if (!bytes_written || !buffer) {
        LOG_ERROR("FS", "Escrita no speaker recebeu argumento nulo");
        return ERR_NULL;
    }
    *bytes_written = 0U;
    if (size != sizeof(app_speaker_tone_t) || !devfs_tone_valid(tone)) {
        result = ERR_INVALID;
    } else {
        speaker_beep(tone->frequency_hz, tone->duration_ms);
        *bytes_written = size;
    }
    devfs_metric(&devfs_metrics.writes, result);
    if (result != OK) LOG_ERROR("FS", "Tom invalido na escrita do speaker");
    return result;
}

static int devfs_speaker_ioctl(file_t* file, uint32_t request, void* argument) {
    app_speaker_tone_t* tone = (app_speaker_tone_t*)argument;
    int result = OK;

    (void)file;
    if (request == APP_IOCTL_SPEAKER_BEEP) {
        if (!devfs_tone_valid(tone)) result = tone ? ERR_INVALID : ERR_NULL;
        else speaker_beep(tone->frequency_hz, tone->duration_ms);
    } else if (request == APP_IOCTL_SPEAKER_STOP) {
        if (argument) result = ERR_INVALID;
        else speaker_off();
    } else {
        result = ERR_INVALID;
    }
    devfs_metric(&devfs_metrics.ioctls, result);
    if (result != OK) LOG_ERROR("FS", "Ioctl do speaker foi recusado");
    return result;
}

static int devfs_hda_read(file_t* file, void* buffer, uint32_t size,
                          uint32_t* bytes_read) {
    devfs_file_context_t* context;
    uint8_t* output = (uint8_t*)buffer;
    uint32_t remaining;
    int result = OK;

    if (!file || !file->vnode || !bytes_read || (size && !buffer)) {
        LOG_ERROR("FS", "Leitura hda recebeu argumento nulo");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    context = (devfs_file_context_t*)file->vnode->private_data;
    if (!context || context->node_id != DEVFS_NODE_HDA) {
        LOG_ERROR("FS", "Leitura hda sem contexto valido");
        return ERR_STATE;
    }
    if (file->offset >= context->capacity || !size) return OK;
    remaining = size;
    if (remaining > context->capacity - file->offset) {
        remaining = context->capacity - file->offset;
    }
    while (remaining) {
        uint32_t lba = file->offset / BLOCK_SECTOR_SIZE;
        uint32_t within = file->offset % BLOCK_SECTOR_SIZE;
        uint32_t amount;

        if (within || remaining < BLOCK_SECTOR_SIZE) {
            amount = BLOCK_SECTOR_SIZE - within;
            if (amount > remaining) amount = remaining;
            result = block_read(context->block_id, lba, 1U, context->sector);
            if (result != OK) break;
            kmemcpy(output + *bytes_read, context->sector + within, amount);
        } else {
            uint32_t sectors = remaining / BLOCK_SECTOR_SIZE;
            if (sectors > DEVFS_HDA_BATCH_SECTORS) {
                sectors = DEVFS_HDA_BATCH_SECTORS;
            }
            amount = sectors * BLOCK_SECTOR_SIZE;
            result = block_read(context->block_id, lba, (uint8_t)sectors,
                                output + *bytes_read);
            if (result != OK) break;
        }
        file->offset += amount;
        *bytes_read += amount;
        remaining -= amount;
    }
    devfs_metric(&devfs_metrics.reads, result);
    if (result != OK) LOG_ERROR("FS", "Falha de bloco durante leitura hda");
    return result;
}

static int devfs_hda_lseek(file_t* file, int32_t offset, uint32_t whence,
                           uint32_t* position) {
    devfs_file_context_t* context;
    int64_t base = 0;
    int64_t target = 0;
    int result = OK;

    if (!file || !file->vnode || !position) {
        LOG_ERROR("FS", "Seek hda recebeu argumento nulo");
        return ERR_NULL;
    }
    context = (devfs_file_context_t*)file->vnode->private_data;
    if (!context || context->node_id != DEVFS_NODE_HDA) {
        LOG_ERROR("FS", "Seek hda sem contexto valido");
        return ERR_STATE;
    }
    if (whence == VFS_SEEK_SET) base = 0;
    else if (whence == VFS_SEEK_CUR) base = file->offset;
    else if (whence == VFS_SEEK_END) base = context->capacity;
    else result = ERR_INVALID;
    if (result == OK) target = base + offset;
    if (result == OK && (target < 0 || (uint64_t)target > context->capacity)) {
        result = ERR_INVALID;
    }
    if (result == OK) {
        file->offset = (uint32_t)target;
        *position = file->offset;
    }
    devfs_metric(&devfs_metrics.seeks, result);
    if (result != OK) LOG_ERROR("FS", "Posicao de seek hda invalida");
    return result;
}

static int devfs_unavailable_read(file_t* file, void* buffer, uint32_t size,
                                  uint32_t* bytes_read) {
    (void)file; (void)buffer; (void)size;
    if (bytes_read) *bytes_read = 0U;
    LOG_WARN("FS", "Leitura nao suportada pelo dispositivo");
    return ERR_UNAVAILABLE;
}

static int devfs_unavailable_write(file_t* file, const void* buffer,
                                   uint32_t size, uint32_t* bytes_written) {
    (void)file; (void)buffer; (void)size;
    if (bytes_written) *bytes_written = 0U;
    LOG_WARN("FS", "Escrita nao suportada pelo dispositivo");
    return ERR_UNAVAILABLE;
}

static int devfs_unavailable_lseek(file_t* file, int32_t offset,
                                   uint32_t whence, uint32_t* position) {
    (void)file; (void)offset; (void)whence;
    if (position) *position = 0U;
    LOG_WARN("FS", "Seek nao suportado pelo dispositivo");
    return ERR_UNAVAILABLE;
}

static int devfs_unavailable_ioctl(file_t* file, uint32_t request,
                                   void* argument) {
    (void)file; (void)request; (void)argument;
    LOG_WARN("FS", "Ioctl nao suportado pelo dispositivo");
    return ERR_UNAVAILABLE;
}

static int devfs_unavailable_sync(file_t* file) {
    (void)file;
    LOG_WARN("FS", "Sync nao suportado pelo dispositivo devfs");
    return ERR_UNAVAILABLE;
}

static int devfs_hda_sync(file_t* file) {
    devfs_file_context_t* context;

    if (!file || !file->vnode) {
        LOG_ERROR("FS", "Arquivo hda nulo no fsync");
        return ERR_NULL;
    }
    context = (devfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->block_id[0]) {
        LOG_ERROR("FS", "Contexto hda invalido no fsync");
        return ERR_STATE;
    }
    return block_cache_sync_device(context->block_id);
}

int devfs_validate_state(void) {
    if (!devfs_ready || !devfs_metrics.initialized ||
        devfs_metrics.capacity != DEVFS_MAX_NODES ||
        devfs_metrics.active != DEVFS_MAX_NODES) {
        LOG_ERROR("FS", "Metricas devfs inconsistentes");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < DEVFS_MAX_NODES; index++) {
        if (!devfs_nodes[index].name[0] ||
            !devfs_operations((devfs_node_id_t)index)) {
            LOG_ERROR("FS", "Registro devfs possui no invalido");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U; other < DEVFS_MAX_NODES; other++) {
            if (kstrcmp(devfs_nodes[index].name, devfs_nodes[other].name) == 0) {
                LOG_ERROR("FS", "Registro devfs possui nome duplicado");
                return ERR_STATE;
            }
        }
    }
    return OK;
}

static void devfs_test_count(devfs_test_result_t* result, uint8_t passed) {
    result->total++;
    if (passed) result->passed++;
}

int devfs_self_test(devfs_test_result_t* result) {
    app_speaker_tone_t tone = {DEVFS_SPEAKER_MIN_HZ,
                               DEVFS_SPEAKER_MIN_MS};
    uint32_t bytes = 0U;
    uint32_t position = 0U;
    int32_t fd = VFS_FD_INVALID;
    int32_t initial_fd = VFS_FD_INVALID;
    uint32_t node_count = 0U;
    int code;

    if (!result) {
        LOG_ERROR("FS", "Destino de autoteste devfs nulo");
        return ERR_NULL;
    }
    kmemset(result, 0, sizeof(*result));
    result->registry = devfs_validate_state() == OK &&
        devfs_copy_nodes(devfs_test_nodes, DEVFS_MAX_NODES,
                         &node_count) == OK &&
        node_count == DEVFS_MAX_NODES;
    devfs_test_count(result, result->registry);
    code = vfs_open("/dev/null", VFS_MODE_READ_WRITE, &fd);
    initial_fd = fd;
    result->null_device = code == OK;
    if (result->null_device) {
        result->null_device =
            vfs_read(fd, devfs_test_buffer, sizeof(devfs_test_buffer),
                     &bytes) == OK && bytes == 0U &&
            vfs_write(fd, devfs_test_buffer, sizeof(devfs_test_buffer),
                      &bytes) == OK && bytes == sizeof(devfs_test_buffer) &&
            vfs_fsync(fd) == ERR_UNAVAILABLE;
        if (vfs_close(fd) != OK) result->null_device = 0U;
    }
    devfs_test_count(result, result->null_device);
    fd = VFS_FD_INVALID;
    kmemset(devfs_test_buffer, 0xA5, sizeof(devfs_test_buffer));
    result->zero_device = vfs_open("/dev/zero", VFS_MODE_READ_WRITE, &fd) == OK;
    if (result->zero_device) {
        result->zero_device =
            vfs_read(fd, devfs_test_buffer, sizeof(devfs_test_buffer),
                     &bytes) == OK && bytes == sizeof(devfs_test_buffer) &&
            devfs_test_buffer[0] == 0U &&
            devfs_test_buffer[BLOCK_SECTOR_SIZE - 1U] == 0U &&
            vfs_write(fd, devfs_test_buffer, sizeof(devfs_test_buffer),
                      &bytes) == OK && bytes == sizeof(devfs_test_buffer);
        if (vfs_close(fd) != OK) result->zero_device = 0U;
    }
    devfs_test_count(result, result->zero_device);
    fd = VFS_FD_INVALID;
    result->tty_device = vfs_open("/dev/tty", VFS_MODE_READ_WRITE, &fd) == OK;
    if (result->tty_device && vfs_close(fd) != OK) result->tty_device = 0U;
    devfs_test_count(result, result->tty_device);
    fd = VFS_FD_INVALID;
    result->speaker_device = vfs_open("/dev/speaker", VFS_MODE_WRITE, &fd) == OK;
    if (result->speaker_device) {
        result->speaker_device =
            vfs_write(fd, &tone, sizeof(tone), &bytes) == OK &&
            bytes == sizeof(tone) &&
            vfs_ioctl(fd, APP_IOCTL_SPEAKER_STOP, 0) == OK;
        if (vfs_close(fd) != OK) result->speaker_device = 0U;
    }
    devfs_test_count(result, result->speaker_device);
    fd = VFS_FD_INVALID;
    code = vfs_open("/dev/hda", VFS_MODE_READ, &fd);
    result->block_device = code == OK;
    if (result->block_device) {
        result->block_device =
            vfs_read(fd, devfs_test_buffer, BLOCK_SECTOR_SIZE, &bytes) == OK &&
            bytes == BLOCK_SECTOR_SIZE &&
            vfs_fsync(fd) == OK &&
            vfs_lseek(fd, 1, VFS_SEEK_SET, &position) == OK && position == 1U &&
            vfs_read(fd, devfs_test_buffer, 16U, &bytes) == OK && bytes == 16U &&
            vfs_lseek(fd, 0, VFS_SEEK_END, &position) == OK &&
            vfs_read(fd, devfs_test_buffer, 1U, &bytes) == OK && bytes == 0U &&
            vfs_lseek(fd, 1, VFS_SEEK_END, &position) == ERR_INVALID;
        if (vfs_close(fd) != OK) result->block_device = 0U;
    }
    devfs_test_count(result, result->block_device);
    {
        int32_t hda_fd = VFS_FD_INVALID;
        int32_t speaker_fd = VFS_FD_INVALID;
        int hda_result = vfs_open("/dev/hda", VFS_MODE_WRITE, &hda_fd);
        int speaker_result = vfs_open("/dev/speaker", VFS_MODE_READ,
                                      &speaker_fd);

        result->permissions = hda_result == ERR_UNAVAILABLE &&
                              speaker_result == ERR_UNAVAILABLE;
        if (hda_fd != VFS_FD_INVALID) {
            (void)vfs_close(hda_fd);
            result->permissions = 0U;
        }
        if (speaker_fd != VFS_FD_INVALID) {
            (void)vfs_close(speaker_fd);
            result->permissions = 0U;
        }
    }
    devfs_test_count(result, result->permissions);
    result->cleanup = initial_fd >= VFS_FD_FIRST_FILE;
    if (result->cleanup) {
        uint32_t count = 0U;
        result->cleanup = vfs_copy_descriptors(devfs_test_descriptors,
                                               VFS_MAX_FDS,
                                               &count) == OK;
        for (uint32_t index = 0U; result->cleanup && index < count; index++) {
            if (devfs_test_descriptors[index].fd == initial_fd) {
                result->cleanup = 0U;
            }
        }
    }
    devfs_test_count(result, result->cleanup);
    result->invariants = devfs_validate_state() == OK;
    devfs_test_count(result, result->invariants);
    if (result->passed != result->total) {
        LOG_ERROR("FS", "Autoteste devfs encontrou falhas");
        return ERR_STATE;
    }
    return OK;
}
