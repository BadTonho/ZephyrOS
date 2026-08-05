#include "fs/file_index.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"

#define FILE_INDEX_CANARY_HEAD 0x58444946U
#define FILE_INDEX_CANARY_TAIL 0x21444E45U
#define FILE_INDEX_HASH_OFFSET 2166136261U
#define FILE_INDEX_HASH_PRIME  16777619U
#define FILE_INDEX_DIRECTORY_ATTRIBUTE 0x10U
#define FILE_INDEX_FULL_PATH_SIZE \
    (FS_MAX_PATH + STORAGE_ID_SIZE + STORAGE_NAME_SIZE + 4U)

typedef struct {
    char volume_id[STORAGE_ID_SIZE];
    uint32_t mount_generation;
    uint32_t content_generation;
    storage_fs_type_t fs_type;
    uint8_t boot;
} file_index_source_t;

typedef struct {
    uint32_t canary_head;
    uint32_t count;
    uint32_t checksum;
    uint8_t partial;
    uint8_t source_count;
    file_index_source_t sources[FILE_INDEX_MAX_SOURCES];
    file_index_entry_t entries[FILE_INDEX_MAX_ENTRIES];
    uint32_t canary_tail;
} file_index_table_t;

typedef struct {
    char path[FS_MAX_PATH];
    uint8_t boot;
    uint8_t depth;
    union {
        fs_dir_cursor_t boot_cursor;
        storage_dir_cursor_t storage_cursor;
    } cursor;
} file_index_frame_t;

static file_index_table_t* file_index_active;
static file_index_table_t* file_index_candidate;
static file_index_source_t file_index_candidate_sources[FILE_INDEX_MAX_SOURCES];
static file_index_source_t file_index_suspended_sources[FILE_INDEX_MAX_SOURCES];
static file_index_frame_t file_index_frames[FILE_INDEX_MAX_DEPTH + 1U];
static uint32_t file_index_candidate_source_count;
static uint32_t file_index_suspended_source_count;
static uint32_t file_index_source_index;
static uint32_t file_index_frame_count;
static uint32_t file_index_directories;
static uint32_t file_index_steps;
static uint32_t file_index_candidate_hash;
static uint32_t file_index_event_generation;
static uint32_t file_index_validate_cursor;
static uint32_t file_index_validate_hash;
static file_index_state_t file_index_state;
static int file_index_last_error;
static uint8_t file_index_initialized;
static uint8_t file_index_stale;
static uint8_t file_index_automatic_suspended;
static uint8_t file_index_active_corrupt;
static uint8_t file_index_corruption_logged;
static spinlock_t file_index_lock;

static void file_index_copy_text(char* destination, uint32_t capacity,
                                 const char* source) {
    uint32_t index = 0;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int file_index_text_equal(const char* left, const char* right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (*left++ != *right++) return 0;
    }
    return *left == '\0' && *right == '\0';
}

static char file_index_lower(char value) {
    if (value >= 'A' && value <= 'Z') return (char)(value + ('a' - 'A'));
    return value == '\\' ? '/' : value;
}

static uint32_t file_index_hash_bytes(uint32_t hash, const void* data,
                                      uint32_t size) {
    const uint8_t* bytes = (const uint8_t*)data;

    for (uint32_t index = 0; index < size; index++) {
        hash ^= bytes[index];
        hash *= FILE_INDEX_HASH_PRIME;
    }
    return hash;
}

static uint32_t file_index_finalize_checksum(
    uint32_t hash, const file_index_table_t* table) {
    hash = file_index_hash_bytes(hash, &table->count, sizeof(table->count));
    hash = file_index_hash_bytes(hash, &table->partial,
                                 sizeof(table->partial));
    hash = file_index_hash_bytes(hash, &table->source_count,
                                 sizeof(table->source_count));
    return file_index_hash_bytes(
        hash, table->sources,
        sizeof(file_index_source_t) * table->source_count);
}

static int file_index_source_equal(const file_index_source_t* left,
                                   const file_index_source_t* right) {
    return left && right && left->boot == right->boot &&
           left->fs_type == right->fs_type &&
           left->mount_generation == right->mount_generation &&
           left->content_generation == right->content_generation &&
           file_index_text_equal(left->volume_id, right->volume_id);
}

static int file_index_sources_equal(const file_index_source_t* left,
                                    uint32_t left_count,
                                    const file_index_source_t* right,
                                    uint32_t right_count) {
    if (left_count != right_count) return 0;
    for (uint32_t index = 0; index < left_count; index++) {
        if (!file_index_source_equal(&left[index], &right[index])) return 0;
    }
    return 1;
}

static int file_index_capture_sources(file_index_source_t* sources,
                                      uint32_t* out_count) {
    storage_status_t status;
    int result;

    if (!sources || !out_count) {
        LOG_ERROR("FS", "Destino nulo ao capturar fontes do indice");
        return ERR_NULL;
    }
    *out_count = 0;
    result = storage_get_status(&status);
    if (result != OK || !status.initialized) {
        LOG_ERROR("FS", "Storage indisponivel para o indice");
        return result == OK ? ERR_STATE : result;
    }
    if (status.mounted_count > FILE_INDEX_MAX_SOURCES) {
        LOG_ERROR("FS", "Montagens excedem o limite do indice");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0; index < status.mounted_count; index++) {
        storage_volume_t volume;
        file_index_source_t* source = &sources[*out_count];

        result = storage_get_mounted_at((uint8_t)index, &volume);
        if (result != OK) {
            LOG_ERROR("FS", "Montagem mudou durante captura do indice");
            return result;
        }
        kmemset(source, 0, sizeof(*source));
        file_index_copy_text(source->volume_id, STORAGE_ID_SIZE, volume.id);
        source->mount_generation = volume.generation;
        source->content_generation = volume.boot ?
                                     fs_get_generation() : volume.generation;
        source->fs_type = volume.fs_type;
        source->boot = volume.boot;
        (*out_count)++;
    }
    return OK;
}

static void file_index_copy_sources(file_index_source_t* destination,
                                    const file_index_source_t* source,
                                    uint32_t count) {
    kmemset(destination, 0,
            sizeof(file_index_source_t) * FILE_INDEX_MAX_SOURCES);
    if (count) {
        kmemcpy(destination, source, sizeof(file_index_source_t) * count);
    }
}

static int file_index_text_terminated(const char* text, uint32_t capacity) {
    if (!text) return 0;
    for (uint32_t index = 0; index < capacity; index++) {
        if (text[index] == '\0') return 1;
    }
    return 0;
}

static int file_index_source_structure_valid(
    const file_index_source_t* source) {
    return source && source->boot <= 1U && source->mount_generation != 0U &&
           source->content_generation != 0U &&
           (source->fs_type == STORAGE_FS_FAT12 ||
            source->fs_type == STORAGE_FS_FAT32) &&
           file_index_text_terminated(source->volume_id, STORAGE_ID_SIZE) &&
           source->volume_id[0] != '\0';
}

static int file_index_active_sources_equal(
    const file_index_source_t* sources, uint32_t source_count) {
    if (!file_index_active || file_index_active_corrupt ||
        file_index_active->canary_head != FILE_INDEX_CANARY_HEAD ||
        file_index_active->canary_tail != FILE_INDEX_CANARY_TAIL ||
        file_index_active->source_count > FILE_INDEX_MAX_SOURCES) return 0;
    for (uint32_t index = 0;
         index < file_index_active->source_count; index++) {
        if (!file_index_source_structure_valid(
                &file_index_active->sources[index])) return 0;
    }
    return file_index_sources_equal(
        sources, source_count, file_index_active->sources,
        file_index_active->source_count);
}

static int file_index_entry_structure_valid(
    const file_index_table_t* table, const file_index_entry_t* entry) {
    int source_found = 0;

    if (!table || !entry || entry->boot > 1U ||
        entry->is_directory > 1U ||
        entry->is_directory !=
            ((entry->attributes & FILE_INDEX_DIRECTORY_ATTRIBUTE) ? 1U : 0U) ||
        !file_index_text_terminated(entry->volume_id, STORAGE_ID_SIZE) ||
        !file_index_text_terminated(entry->parent_path, FS_MAX_PATH) ||
        !file_index_text_terminated(entry->name, STORAGE_NAME_SIZE) ||
        !entry->name[0]) return 0;
    for (uint32_t index = 0; entry->parent_path[index]; index++) {
        if (entry->parent_path[index] == '\\') return 0;
    }
    for (uint32_t index = 0; entry->name[index]; index++) {
        if (entry->name[index] == '/' || entry->name[index] == '\\') return 0;
    }
    for (uint32_t index = 0; index < table->source_count; index++) {
        const file_index_source_t* source = &table->sources[index];

        if (entry->boot == source->boot &&
            entry->volume_generation == source->mount_generation &&
            file_index_text_equal(entry->volume_id, source->volume_id)) {
            source_found = 1;
            break;
        }
    }
    return source_found;
}

static int file_index_table_valid(const file_index_table_t* table) {
    uint32_t checksum = FILE_INDEX_HASH_OFFSET;

    if (!table || table->canary_head != FILE_INDEX_CANARY_HEAD ||
        table->canary_tail != FILE_INDEX_CANARY_TAIL ||
        table->count > FILE_INDEX_MAX_ENTRIES ||
        table->source_count > FILE_INDEX_MAX_SOURCES || table->partial > 1U) {
        return 0;
    }
    for (uint32_t index = 0; index < table->source_count; index++) {
        if (!file_index_source_structure_valid(&table->sources[index])) {
            return 0;
        }
    }
    for (uint32_t index = 0; index < table->count; index++) {
        if (!file_index_entry_structure_valid(table, &table->entries[index])) {
            return 0;
        }
        checksum = file_index_hash_bytes(
            checksum, &table->entries[index], sizeof(file_index_entry_t));
    }
    checksum = file_index_finalize_checksum(checksum, table);
    return checksum == table->checksum;
}

static int file_index_candidate_header_valid(void) {
    if (!file_index_candidate ||
        file_index_candidate->canary_head != FILE_INDEX_CANARY_HEAD ||
        file_index_candidate->canary_tail != FILE_INDEX_CANARY_TAIL ||
        file_index_candidate->count > FILE_INDEX_MAX_ENTRIES ||
        file_index_candidate->source_count > FILE_INDEX_MAX_SOURCES ||
        file_index_candidate->partial > 1U ||
        file_index_candidate->source_count !=
            file_index_candidate_source_count) return 0;
    for (uint32_t index = 0;
         index < file_index_candidate->source_count; index++) {
        if (!file_index_source_structure_valid(
                &file_index_candidate->sources[index])) return 0;
    }
    if (!file_index_sources_equal(
            file_index_candidate->sources,
            file_index_candidate->source_count,
            file_index_candidate_sources,
            file_index_candidate_source_count)) return 0;
    return 1;
}

static int file_index_candidate_valid(void) {
    uint32_t checksum = FILE_INDEX_HASH_OFFSET;

    if (!file_index_candidate_header_valid()) return 0;
    for (uint32_t index = 0; index < file_index_candidate->count; index++) {
        file_index_entry_t* entry = &file_index_candidate->entries[index];

        if (!file_index_entry_structure_valid(file_index_candidate, entry)) {
            return 0;
        }
        checksum = file_index_hash_bytes(checksum, entry, sizeof(*entry));
    }
    return checksum == file_index_candidate_hash;
}

static void file_index_advance_event(void) {
    file_index_event_generation++;
    if (!file_index_event_generation) file_index_event_generation = 1U;
}

static void file_index_release_candidate(void) {
    if (!file_index_candidate) return;
    kfree(file_index_candidate);
    file_index_candidate = 0;
}

static int file_index_start_rebuild_unlocked(
    const file_index_source_t* sources, uint32_t source_count) {
    file_index_table_t* table;

    file_index_release_candidate();
    table = (file_index_table_t*)kmalloc(sizeof(file_index_table_t));
    if (!table) {
        file_index_state = FILE_INDEX_STATE_FAILED;
        file_index_last_error = ERR_MEM;
        file_index_stale = file_index_active &&
                           !file_index_active_sources_equal(
                               sources, source_count);
        file_index_automatic_suspended = 1;
        file_index_copy_sources(file_index_suspended_sources, sources,
                                source_count);
        file_index_suspended_source_count = source_count;
        file_index_advance_event();
        LOG_ERROR("FS", "Memoria insuficiente para reconstruir indice");
        return ERR_MEM;
    }
    table->canary_head = FILE_INDEX_CANARY_HEAD;
    table->count = 0;
    table->checksum = 0;
    table->partial = 0;
    table->canary_tail = FILE_INDEX_CANARY_TAIL;
    table->source_count = (uint8_t)source_count;
    file_index_copy_sources(table->sources, sources, source_count);
    file_index_candidate = table;
    file_index_copy_sources(file_index_candidate_sources, sources,
                            source_count);
    file_index_candidate_source_count = source_count;
    file_index_source_index = 0;
    file_index_frame_count = 0;
    file_index_directories = 0;
    file_index_steps = 0;
    file_index_candidate_hash = FILE_INDEX_HASH_OFFSET;
    file_index_state = FILE_INDEX_STATE_BUILDING;
    file_index_last_error = OK;
    file_index_stale = file_index_active &&
                       !file_index_active_sources_equal(
                           sources, source_count);
    file_index_automatic_suspended = 0;
    file_index_corruption_logged = 0;
    file_index_advance_event();
    LOG_INFO("FS", "Reconstrucao cooperativa do indice iniciada");
    return OK;
}

static int file_index_join_path(const char* parent, const char* name,
                                char* output) {
    uint32_t parent_length;
    uint32_t name_length;

    if (!parent || !name || !output) {
        LOG_ERROR("FS", "Argumento nulo ao compor caminho do indice");
        return ERR_NULL;
    }
    parent_length = kstrlen(parent);
    name_length = kstrlen(name);
    if (!name_length || parent_length + name_length +
        (parent_length ? 2U : 1U) > FS_MAX_PATH) {
        LOG_ERROR("FS", "Caminho excede o limite do indice");
        return ERR_OVERFLOW;
    }
    if (parent_length) {
        kmemcpy(output, parent, parent_length);
        output[parent_length++] = '/';
    }
    kmemcpy(output + parent_length, name, name_length);
    output[parent_length + name_length] = '\0';
    return OK;
}

static int file_index_open_source(void) {
    file_index_frame_t* frame;
    file_index_source_t* source;
    int result;

    if (file_index_source_index >= file_index_candidate_source_count) {
        LOG_ERROR("FS", "Fonte inexistente ao abrir cursor do indice");
        return ERR_NOT_FOUND;
    }
    if (file_index_directories >= FILE_INDEX_MAX_DIRECTORIES) {
        file_index_candidate->partial = 1;
        file_index_source_index++;
        LOG_WARN("FS", "Limite de diretorios atingido no indice");
        return OK;
    }
    source = &file_index_candidate_sources[file_index_source_index];
    frame = &file_index_frames[0];
    kmemset(frame, 0, sizeof(*frame));
    frame->boot = source->boot;
    result = source->boot ?
             fs_dir_cursor_open("", &frame->cursor.boot_cursor) :
             storage_dir_cursor_open(source->volume_id, "",
                                     &frame->cursor.storage_cursor);
    if (result != OK) {
        file_index_candidate->partial = 1;
        file_index_last_error = result;
        file_index_source_index++;
        LOG_ERROR("FS", "Falha ao abrir fonte do indice");
        return result;
    }
    file_index_frame_count = 1;
    file_index_directories++;
    return OK;
}

static void file_index_publish(void) {
    file_index_table_t* previous = file_index_active;

    if (!file_index_candidate_valid()) {
        file_index_candidate->canary_head = 0U;
        file_index_state = FILE_INDEX_STATE_FAILED;
        file_index_last_error = ERR_STATE;
        if (!file_index_corruption_logged) {
            LOG_ERROR("FS", "Tabela candidata invalida antes da publicacao");
            file_index_corruption_logged = 1;
        }
        return;
    }

    file_index_candidate->checksum = file_index_finalize_checksum(
        file_index_candidate_hash, file_index_candidate);
    file_index_active = file_index_candidate;
    file_index_candidate = 0;
    file_index_state = FILE_INDEX_STATE_READY;
    file_index_stale = 0;
    file_index_validate_cursor = 0;
    file_index_validate_hash = FILE_INDEX_HASH_OFFSET;
    file_index_active_corrupt = 0;
    file_index_advance_event();
    if (previous) kfree(previous);
    LOG_INFO("FS", "Indice de arquivos publicado com sucesso");
}

static void file_index_fill_entry(file_index_entry_t* output,
                                  const file_index_source_t* source,
                                  const file_index_frame_t* frame,
                                  const char* name, uint32_t size,
                                  uint8_t attributes, uint8_t directory) {
    kmemset(output, 0, sizeof(*output));
    file_index_copy_text(output->volume_id, STORAGE_ID_SIZE,
                         source->volume_id);
    file_index_copy_text(output->parent_path, FS_MAX_PATH, frame->path);
    file_index_copy_text(output->name, STORAGE_NAME_SIZE, name);
    output->volume_generation = source->mount_generation;
    output->size = size;
    output->attributes = attributes;
    output->is_directory = directory;
    output->boot = source->boot;
}

static void file_index_push_boot_directory(const file_index_frame_t* parent,
                                           const fs_dir_entry_t* entry,
                                           const char* path) {
    file_index_frame_t* child = &file_index_frames[file_index_frame_count++];

    kmemset(child, 0, sizeof(*child));
    file_index_copy_text(child->path, FS_MAX_PATH, path);
    child->boot = 1;
    child->depth = parent->depth + 1U;
    child->cursor.boot_cursor.generation =
        file_index_candidate_sources[file_index_source_index].content_generation;
    child->cursor.boot_cursor.directory_cluster = entry->cluster;
    child->cursor.boot_cursor.current_cluster = entry->cluster;
    child->cursor.boot_cursor.fs_type = parent->cursor.boot_cursor.fs_type;
    child->cursor.boot_cursor.active = 1;
    file_index_directories++;
}

static void file_index_push_storage_directory(
    const file_index_frame_t* parent, const storage_dir_entry_t* entry,
    const char* path) {
    file_index_frame_t* child = &file_index_frames[file_index_frame_count++];
    const file_index_source_t* source =
        &file_index_candidate_sources[file_index_source_index];

    kmemset(child, 0, sizeof(*child));
    file_index_copy_text(child->path, FS_MAX_PATH, path);
    child->depth = parent->depth + 1U;
    file_index_copy_text(child->cursor.storage_cursor.volume_id,
                         STORAGE_ID_SIZE, source->volume_id);
    child->cursor.storage_cursor.volume_generation = source->mount_generation;
    child->cursor.storage_cursor.directory_cluster = entry->cluster;
    child->cursor.storage_cursor.current_cluster = entry->cluster;
    child->cursor.storage_cursor.fs_type = source->fs_type;
    child->cursor.storage_cursor.active = 1;
    file_index_directories++;
}

static void file_index_consume_entry(file_index_frame_t* frame,
                                     const char* name, uint32_t size,
                                     uint32_t cluster, uint8_t attributes,
                                     uint8_t directory) {
    file_index_source_t* source =
        &file_index_candidate_sources[file_index_source_index];
    file_index_entry_t* output;
    char child_path[FS_MAX_PATH];

    if (file_index_candidate->count >= FILE_INDEX_MAX_ENTRIES) {
        file_index_candidate->partial = 1;
        return;
    }
    output = &file_index_candidate->entries[file_index_candidate->count++];
    file_index_fill_entry(output, source, frame, name, size,
                          attributes, directory);
    file_index_candidate_hash = file_index_hash_bytes(
        file_index_candidate_hash, output, sizeof(*output));
    if (!directory) return;
    if (file_index_directories >= FILE_INDEX_MAX_DIRECTORIES ||
        frame->depth >= FILE_INDEX_MAX_DEPTH ||
        file_index_join_path(frame->path, name, child_path) != OK) {
        file_index_candidate->partial = 1;
        return;
    }
    if (frame->boot) {
        fs_dir_entry_t entry;

        kmemset(&entry, 0, sizeof(entry));
        entry.cluster = cluster;
        file_index_push_boot_directory(frame, &entry, child_path);
    } else {
        storage_dir_entry_t entry;

        kmemset(&entry, 0, sizeof(entry));
        entry.cluster = cluster;
        file_index_push_storage_directory(frame, &entry, child_path);
    }
}

static int file_index_scan_cursor(file_index_frame_t* frame,
                                  uint8_t* out_found, uint8_t* out_done) {
    int result;

    if (frame->boot) {
        fs_dir_entry_t entry;

        result = fs_dir_cursor_next(&frame->cursor.boot_cursor, &entry,
                                    out_found, out_done);
        if (result == OK && *out_found) {
            file_index_consume_entry(frame, entry.name, entry.size,
                                     entry.cluster, entry.attributes,
                                     entry.is_directory);
        }
    } else {
        storage_dir_entry_t entry;

        result = storage_dir_cursor_next(&frame->cursor.storage_cursor, &entry,
                                         out_found, out_done);
        if (result == OK && *out_found) {
            file_index_consume_entry(frame, entry.name, entry.size,
                                     entry.cluster, entry.attributes,
                                     entry.is_directory);
        }
    }
    return result;
}

static void file_index_finish_frame(void) {
    if (file_index_frame_count) file_index_frame_count--;
    if (!file_index_frame_count) file_index_source_index++;
}

static void file_index_scan_step(void) {
    file_index_frame_t* frame;
    uint8_t found = 0;
    uint8_t done = 0;
    int result;

    if (file_index_candidate->count >= FILE_INDEX_MAX_ENTRIES) {
        file_index_candidate->partial = 1;
        file_index_publish();
        return;
    }
    if (!file_index_frame_count) {
        if (file_index_source_index >= file_index_candidate_source_count) {
            file_index_publish();
            return;
        }
        file_index_open_source();
        return;
    }
    frame = &file_index_frames[file_index_frame_count - 1U];
    result = file_index_scan_cursor(frame, &found, &done);
    file_index_steps++;
    if (result != OK) {
        file_index_candidate->partial = 1;
        file_index_last_error = result;
        file_index_frame_count = 0;
        file_index_source_index++;
        LOG_ERROR("FS", "Fonte do indice ficou indisponivel");
    } else if (done) {
        file_index_finish_frame();
    }
    file_index_advance_event();
}

static int file_index_active_validation_step(void) {
    if (!file_index_active || file_index_active_corrupt) return OK;
    if (file_index_active->canary_head != FILE_INDEX_CANARY_HEAD ||
        file_index_active->canary_tail != FILE_INDEX_CANARY_TAIL ||
        file_index_active->count > FILE_INDEX_MAX_ENTRIES ||
        file_index_active->source_count > FILE_INDEX_MAX_SOURCES ||
        file_index_active->partial > 1U) {
        LOG_ERROR("FS", "Cabecalho ativo do indice ficou invalido");
        return ERR_STATE;
    }
    for (uint32_t index = 0;
         index < file_index_active->source_count; index++) {
        if (!file_index_source_structure_valid(
                &file_index_active->sources[index])) {
            LOG_ERROR("FS", "Fonte ativa do indice ficou invalida");
            return ERR_STATE;
        }
    }
    if (file_index_validate_cursor == 0U) {
        file_index_validate_hash = FILE_INDEX_HASH_OFFSET;
    }
    if (file_index_validate_cursor < file_index_active->count) {
        file_index_entry_t* entry =
            &file_index_active->entries[file_index_validate_cursor++];

        if (!file_index_entry_structure_valid(file_index_active, entry)) {
            LOG_ERROR("FS", "Entrada ativa do indice ficou invalida");
            return ERR_STATE;
        }
        file_index_validate_hash = file_index_hash_bytes(
            file_index_validate_hash, entry, sizeof(*entry));
        return OK;
    }
    file_index_validate_cursor = 0;
    if (file_index_finalize_checksum(file_index_validate_hash,
                                     file_index_active) !=
        file_index_active->checksum) {
        LOG_ERROR("FS", "Checksum ativo do indice ficou invalido");
        return ERR_STATE;
    }
    return OK;
}

static void file_index_handle_corruption(
    const file_index_source_t* sources, uint32_t source_count) {
    int result;

    file_index_active_corrupt = 1;
    file_index_stale = 1;
    file_index_last_error = ERR_STATE;
    if (!file_index_corruption_logged) {
        LOG_ERROR("FS", "Corrupcao detectada no indice de arquivos");
    }
    result = file_index_start_rebuild_unlocked(sources, source_count);
    if (result == OK) file_index_last_error = ERR_STATE;
    file_index_stale = 1;
    file_index_corruption_logged = 1;
}

static void file_index_handle_candidate_corruption(
    const file_index_source_t* sources, uint32_t source_count) {
    int result;

    if (!file_index_corruption_logged) {
        LOG_ERROR("FS", "Corrupcao detectada na tabela candidata do indice");
    }
    result = file_index_start_rebuild_unlocked(sources, source_count);
    if (result == OK) file_index_last_error = ERR_STATE;
    file_index_corruption_logged = 1;
}

int file_index_init(void) {
    file_index_source_t sources[FILE_INDEX_MAX_SOURCES];
    uint32_t source_count;
    int result;

    LOG_INFO("FS", "Inicializando indice global de arquivos");
    if (file_index_initialized) {
        LOG_ERROR("FS", "Indice global ja foi inicializado");
        return ERR_STATE;
    }
    spinlock_init(&file_index_lock);
    file_index_active = 0;
    file_index_candidate = 0;
    file_index_initialized = 1;
    file_index_state = FILE_INDEX_STATE_EMPTY;
    file_index_last_error = OK;
    file_index_event_generation = 1U;
    file_index_validate_hash = FILE_INDEX_HASH_OFFSET;
    result = file_index_capture_sources(sources, &source_count);
    if (result != OK) {
        file_index_state = FILE_INDEX_STATE_FAILED;
        file_index_last_error = result;
        file_index_automatic_suspended = 0;
        LOG_ERROR("FS", "Indice inicializado sem fontes validas");
        return result;
    }
    result = file_index_start_rebuild_unlocked(sources, source_count);
    if (result != OK) return result;
    LOG_INFO("FS", "Indice global inicializado com sucesso");
    return OK;
}

int file_index_rebuild(void) {
    file_index_source_t sources[FILE_INDEX_MAX_SOURCES];
    uint32_t source_count;
    int result;

    if (!file_index_initialized) {
        LOG_ERROR("FS", "Rebuild solicitado antes da inicializacao");
        return ERR_STATE;
    }
    result = file_index_capture_sources(sources, &source_count);
    if (result != OK) return result;
    spinlock_acquire(&file_index_lock);
    result = file_index_start_rebuild_unlocked(sources, source_count);
    spinlock_release(&file_index_lock);
    return result;
}

int file_index_cancel(void) {
    file_index_source_t sources[FILE_INDEX_MAX_SOURCES];
    uint32_t source_count;
    int result;

    if (!file_index_initialized) {
        LOG_ERROR("FS", "Cancelamento solicitado antes da inicializacao");
        return ERR_STATE;
    }
    result = file_index_capture_sources(sources, &source_count);
    if (result != OK) return result;
    spinlock_acquire(&file_index_lock);
    if (file_index_state != FILE_INDEX_STATE_BUILDING &&
        !file_index_active) {
        spinlock_release(&file_index_lock);
        LOG_WARN("FS", "Nao ha indice ativo ou rebuild para cancelar");
        return ERR_STATE;
    }
    file_index_release_candidate();
    file_index_copy_sources(file_index_suspended_sources, sources,
                            source_count);
    file_index_suspended_source_count = source_count;
    file_index_state = FILE_INDEX_STATE_CANCELLED;
    file_index_automatic_suspended = 1;
    file_index_last_error = OK;
    file_index_advance_event();
    spinlock_release(&file_index_lock);
    LOG_INFO("FS", "Indexacao automatica cancelada");
    return OK;
}

int file_index_poll(uint32_t budget, uint32_t* out_steps) {
    file_index_source_t sources[FILE_INDEX_MAX_SOURCES];
    uint32_t source_count;
    int result;

    if (!out_steps) {
        LOG_ERROR("FS", "Destino nulo no polling do indice");
        return ERR_NULL;
    }
    *out_steps = 0;
    if (!file_index_initialized || !budget) {
        LOG_ERROR("FS", "Polling invalido do indice");
        return file_index_initialized ? ERR_INVALID : ERR_STATE;
    }
    result = file_index_capture_sources(sources, &source_count);
    if (result != OK) return result;
    spinlock_acquire(&file_index_lock);
    if (file_index_active_validation_step() != OK) {
        file_index_handle_corruption(sources, source_count);
    } else if (file_index_candidate &&
               !file_index_candidate_header_valid()) {
        file_index_handle_candidate_corruption(sources, source_count);
    }
    if (file_index_automatic_suspended) {
        if (!file_index_sources_equal(
                sources, source_count, file_index_suspended_sources,
                file_index_suspended_source_count)) {
            file_index_start_rebuild_unlocked(sources, source_count);
        }
    } else if (file_index_state == FILE_INDEX_STATE_BUILDING) {
        if (!file_index_sources_equal(
                sources, source_count, file_index_candidate_sources,
                file_index_candidate_source_count)) {
            file_index_start_rebuild_unlocked(sources, source_count);
        }
    } else if (!file_index_active || !file_index_sources_equal(
                   sources, source_count, file_index_active->sources,
                   file_index_active->source_count)) {
        file_index_stale = file_index_active ? 1U : 0U;
        file_index_start_rebuild_unlocked(sources, source_count);
    }
    while (file_index_state == FILE_INDEX_STATE_BUILDING &&
           *out_steps < budget) {
        file_index_scan_step();
        (*out_steps)++;
    }
    spinlock_release(&file_index_lock);
    return OK;
}

int file_index_get_status(file_index_status_t* out_status) {
    uint8_t active_valid;
    uint8_t candidate_valid;

    if (!out_status) {
        LOG_ERROR("FS", "Destino nulo no status do indice");
        return ERR_NULL;
    }
    if (!file_index_initialized) {
        LOG_ERROR("FS", "Status do indice antes da inicializacao");
        return ERR_STATE;
    }
    spinlock_acquire(&file_index_lock);
    active_valid = file_index_active && !file_index_active_corrupt &&
                   file_index_active->canary_head == FILE_INDEX_CANARY_HEAD &&
                   file_index_active->canary_tail == FILE_INDEX_CANARY_TAIL &&
                   file_index_active->count <= FILE_INDEX_MAX_ENTRIES &&
                   file_index_active->source_count <= FILE_INDEX_MAX_SOURCES &&
                   file_index_active->partial <= 1U;
    candidate_valid = file_index_candidate &&
        file_index_candidate->canary_head == FILE_INDEX_CANARY_HEAD &&
        file_index_candidate->canary_tail == FILE_INDEX_CANARY_TAIL &&
        file_index_candidate->count <= FILE_INDEX_MAX_ENTRIES &&
        file_index_candidate->source_count <= FILE_INDEX_MAX_SOURCES &&
        file_index_candidate->partial <= 1U;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->state = file_index_state;
    out_status->active_entries = active_valid ?
                                 file_index_active->count : 0U;
    out_status->candidate_entries = candidate_valid ?
                                    file_index_candidate->count : 0U;
    out_status->directories_scanned = file_index_directories;
    out_status->scan_steps = file_index_steps;
    out_status->source_count = candidate_valid ?
                               file_index_candidate_source_count :
                               (active_valid ?
                                file_index_active->source_count : 0U);
    out_status->sources_completed = file_index_source_index;
    out_status->event_generation = file_index_event_generation;
    out_status->memory_bytes =
        (file_index_active ? sizeof(file_index_table_t) : 0U) +
        (file_index_candidate ? sizeof(file_index_table_t) : 0U);
    out_status->initialized = file_index_initialized;
    out_status->partial = active_valid ?
                          file_index_active->partial :
                          (candidate_valid ?
                           file_index_candidate->partial : 0U);
    out_status->stale = file_index_stale;
    out_status->automatic_suspended = file_index_automatic_suspended;
    out_status->last_error = file_index_last_error;
    spinlock_release(&file_index_lock);
    return OK;
}

static int file_index_normalize_query(const char* query, char* normalized) {
    uint32_t length;

    if (!query || !normalized) {
        LOG_ERROR("FS", "Argumento nulo ao normalizar pesquisa");
        return ERR_NULL;
    }
    length = kstrlen(query);
    if (!length) {
        LOG_ERROR("FS", "Termo vazio na pesquisa do indice");
        return ERR_INVALID;
    }
    if (length >= FILE_INDEX_QUERY_SIZE) {
        LOG_ERROR("FS", "Termo excede o limite da pesquisa do indice");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0; index < length; index++) {
        normalized[index] = file_index_lower(query[index]);
    }
    normalized[length] = '\0';
    return OK;
}

static int file_index_contains(const char* text, const char* query) {
    if (!text || !query || !*query) return 0;
    for (uint32_t start = 0; text[start]; start++) {
        uint32_t offset = 0;

        while (query[offset] && text[start + offset] &&
               file_index_lower(text[start + offset]) ==
               file_index_lower(query[offset])) {
            offset++;
        }
        if (!query[offset]) return 1;
    }
    return 0;
}

static void file_index_full_path(const file_index_entry_t* entry,
                                 char* output) {
    uint32_t offset = 0;

    file_index_copy_text(output, FILE_INDEX_FULL_PATH_SIZE, entry->volume_id);
    offset = kstrlen(output);
    if (offset + 2U < FILE_INDEX_FULL_PATH_SIZE) {
        output[offset++] = ':';
        output[offset++] = '/';
        output[offset] = '\0';
    }
    if (entry->parent_path[0]) {
        file_index_copy_text(output + offset,
                             FILE_INDEX_FULL_PATH_SIZE - offset,
                             entry->parent_path);
        offset = kstrlen(output);
        if (offset + 1U < FILE_INDEX_FULL_PATH_SIZE) {
            output[offset++] = '/';
            output[offset] = '\0';
        }
    }
    file_index_copy_text(output + offset, FILE_INDEX_FULL_PATH_SIZE - offset,
                         entry->name);
}

static int file_index_entry_matches(const file_index_entry_t* entry,
                                    const char* normalized_query) {
    char full_path[FILE_INDEX_FULL_PATH_SIZE];

    if (file_index_contains(entry->name, normalized_query)) return 1;
    file_index_full_path(entry, full_path);
    return file_index_contains(full_path, normalized_query);
}

static file_index_result_availability_t file_index_result_availability(
    const file_index_entry_t* entry) {
    storage_volume_t volume;

    if (storage_find_volume(entry->volume_id, &volume) != OK ||
        !volume.mounted) {
        return FILE_INDEX_RESULT_VOLUME_MISSING;
    }
    if (volume.generation != entry->volume_generation) {
        return FILE_INDEX_RESULT_STALE;
    }
    return FILE_INDEX_RESULT_AVAILABLE;
}

int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* out_status) {
    file_index_table_t* table;
    char normalized[FILE_INDEX_QUERY_SIZE];
    int result;

    if (!results || !out_status) {
        LOG_ERROR("FS", "Destino nulo na pesquisa do indice");
        return ERR_NULL;
    }
    if (!file_index_initialized) {
        LOG_ERROR("FS", "Pesquisa solicitada antes da inicializacao");
        return ERR_STATE;
    }
    if (!capacity || capacity > FILE_INDEX_MAX_RESULTS) {
        LOG_ERROR("FS", "Capacidade invalida na pesquisa do indice");
        return ERR_INVALID;
    }
    result = file_index_normalize_query(query, normalized);
    if (result != OK) {
        LOG_ERROR("FS", "Termo invalido na pesquisa do indice");
        return result;
    }
    spinlock_acquire(&file_index_lock);
    kmemset(out_status, 0, sizeof(*out_status));
    table = file_index_active && !file_index_active_corrupt ?
            file_index_active : file_index_candidate;
    if (!table || table->canary_head != FILE_INDEX_CANARY_HEAD ||
        table->canary_tail != FILE_INDEX_CANARY_TAIL ||
        table->count > FILE_INDEX_MAX_ENTRIES ||
        table->source_count > FILE_INDEX_MAX_SOURCES || table->partial > 1U) {
        spinlock_release(&file_index_lock);
        LOG_ERROR("FS", "Nenhuma tabela valida para pesquisa");
        return ERR_STATE;
    }
    if ((table == file_index_active && !file_index_table_valid(table)) ||
        (table == file_index_candidate && !file_index_candidate_valid())) {
        table->canary_head = 0U;
        file_index_last_error = ERR_STATE;
        file_index_stale = 1;
        file_index_advance_event();
        spinlock_release(&file_index_lock);
        LOG_ERROR("FS", "Corrupcao detectada antes da pesquisa");
        return ERR_STATE;
    }
    for (uint32_t index = 0; index < table->count; index++) {
        file_index_result_availability_t availability;

        if (!file_index_entry_matches(&table->entries[index], normalized)) {
            continue;
        }
        out_status->total_matches++;
        if (out_status->returned_matches >= capacity) continue;
        results[out_status->returned_matches].entry = table->entries[index];
        availability = file_index_result_availability(
            &table->entries[index]);
        results[out_status->returned_matches].availability = availability;
        if (availability == FILE_INDEX_RESULT_VOLUME_MISSING) {
            out_status->volume_missing = 1;
        } else if (availability == FILE_INDEX_RESULT_STALE) {
            out_status->result_stale = 1;
        }
        out_status->returned_matches++;
    }
    out_status->partial = table->partial ||
                          out_status->total_matches > capacity;
    out_status->stale = file_index_stale;
    out_status->building = file_index_state == FILE_INDEX_STATE_BUILDING;
    out_status->cancelled = file_index_state == FILE_INDEX_STATE_CANCELLED;
    out_status->last_error = file_index_last_error;
    spinlock_release(&file_index_lock);
    return OK;
}

int file_index_validate_state(void) {
    file_index_source_t sources[FILE_INDEX_MAX_SOURCES];
    uint32_t source_count;
    int result;

    if (!file_index_initialized) {
        LOG_ERROR("FS", "Validacao do indice antes da inicializacao");
        return ERR_STATE;
    }
    result = file_index_capture_sources(sources, &source_count);
    if (result != OK) return result;
    spinlock_acquire(&file_index_lock);
    if (file_index_state == FILE_INDEX_STATE_FAILED && !file_index_active) {
        result = file_index_last_error ? file_index_last_error : ERR_STATE;
        spinlock_release(&file_index_lock);
        LOG_ERROR("FS", "Indice falhou sem tabela ativa");
        return result;
    }
    if (file_index_active_corrupt) {
        spinlock_release(&file_index_lock);
        LOG_ERROR("FS", "Tabela ativa do indice aguarda recuperacao");
        return ERR_STATE;
    }
    if ((file_index_state == FILE_INDEX_STATE_READY && !file_index_active) ||
        (file_index_active && !file_index_table_valid(file_index_active))) {
        file_index_handle_corruption(sources, source_count);
        spinlock_release(&file_index_lock);
        return ERR_STATE;
    }
    if ((file_index_state == FILE_INDEX_STATE_BUILDING &&
         !file_index_candidate) ||
        (file_index_candidate && !file_index_candidate_valid())) {
        file_index_handle_candidate_corruption(sources, source_count);
        spinlock_release(&file_index_lock);
        return ERR_STATE;
    }
    spinlock_release(&file_index_lock);
    return OK;
}

int file_index_self_test(void) {
    file_index_entry_t entry;
    file_index_source_t left[1];
    file_index_source_t right[1];
    char normalized[FILE_INDEX_QUERY_SIZE];
    uint32_t original_hash;
    uint32_t changed_hash;

    kmemset(&entry, 0, sizeof(entry));
    file_index_copy_text(entry.volume_id, STORAGE_ID_SIZE, "ata0raw");
    file_index_copy_text(entry.parent_path, FS_MAX_PATH, "DOCS/GUI");
    file_index_copy_text(entry.name, STORAGE_NAME_SIZE, "README.TXT");
    if (file_index_normalize_query("read", normalized) != OK ||
        !file_index_entry_matches(&entry, normalized) ||
        !file_index_entry_matches(&entry, "README") ||
        file_index_normalize_query("docs/gui", normalized) != OK ||
        !file_index_entry_matches(&entry, normalized) ||
        !file_index_entry_matches(&entry, "DoCs\\GuI")) {
        LOG_ERROR("FS", "Autoteste de matching do indice falhou");
        return ERR_STATE;
    }
    if (FILE_INDEX_MAX_ENTRIES != 512U ||
        FILE_INDEX_MAX_DIRECTORIES != 128U || FILE_INDEX_MAX_DEPTH != 16U ||
        FILE_INDEX_MAX_SOURCES != 4U || FILE_INDEX_MAX_RESULTS != 64U ||
        FILE_INDEX_QUERY_SIZE != 64U ||
        file_index_normalize_query(
            "012345678901234567890123456789012345678901234567890123456789012",
            normalized) != OK) {
        LOG_ERROR("FS", "Autoteste de limites do indice falhou");
        return ERR_STATE;
    }
    kmemset(left, 0, sizeof(left));
    file_index_copy_text(left[0].volume_id, STORAGE_ID_SIZE, "ata0raw");
    left[0].mount_generation = 1U;
    left[0].content_generation = 1U;
    left[0].boot = 1U;
    right[0] = left[0];
    if (!file_index_sources_equal(left, 1U, right, 1U)) {
        LOG_ERROR("FS", "Autoteste de snapshot do indice falhou");
        return ERR_STATE;
    }
    right[0].content_generation++;
    if (file_index_sources_equal(left, 1U, right, 1U)) {
        LOG_ERROR("FS", "Autoteste de cancelamento do indice falhou");
        return ERR_STATE;
    }
    original_hash = file_index_hash_bytes(FILE_INDEX_HASH_OFFSET, &entry,
                                          sizeof(entry));
    entry.size++;
    changed_hash = file_index_hash_bytes(FILE_INDEX_HASH_OFFSET, &entry,
                                         sizeof(entry));
    if (original_hash == changed_hash) {
        LOG_ERROR("FS", "Autoteste de corrupcao do indice falhou");
        return ERR_STATE;
    }
    return OK;
}

const char* file_index_state_name(file_index_state_t state) {
    if (state == FILE_INDEX_STATE_UNINITIALIZED) return "UNINITIALIZED";
    if (state == FILE_INDEX_STATE_EMPTY) return "EMPTY";
    if (state == FILE_INDEX_STATE_BUILDING) return "BUILDING";
    if (state == FILE_INDEX_STATE_READY) return "READY";
    if (state == FILE_INDEX_STATE_CANCELLED) return "CANCELLED";
    if (state == FILE_INDEX_STATE_FAILED) return "FAILED";
    return "UNKNOWN";
}
