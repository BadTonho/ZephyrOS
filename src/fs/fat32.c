#include "fs/fat32.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"

static fat32_fs_t fs;
static uint8_t boot_sector[512];

static void fat32_release(void) {
    if (fs.fat) { kfree(fs.fat); fs.fat = 0; }
    if (fs.root_cache) { kfree(fs.root_cache); fs.root_cache = 0; }
    kmemset(&fs, 0, sizeof(fat32_fs_t));
}

static uint32_t fat32_max_cluster_steps(void) {
    return fs.total_clusters > 0 ? fs.total_clusters + 1 : 1;
}

#define FAT32_CHAIN_LIMIT 0x01000000u

static int strncmp(const char* a, const char* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;
}

static uint32_t fat32_get_cluster(uint32_t cluster) {
    uint32_t offset = cluster * 4;
    uint32_t sector = offset / 512;
    uint32_t entry_offset = offset % 512;

    uint8_t buf[512];
    if (ata_read_sectors(fs.fat_start + sector, 1, buf) != 0) {
        return FAT32_CLUSTER_BAD;
    }

    uint32_t value = *(uint32_t*)(buf + entry_offset);
    return value & 0x0FFFFFFF;
}

static void fat32_set_cluster(uint32_t cluster, uint32_t value) {
    uint32_t offset = cluster * 4;
    uint32_t sector = offset / 512;
    uint32_t entry_offset = offset % 512;

    uint8_t buf[512];
    if (ata_read_sectors(fs.fat_start + sector, 1, buf) != 0) {
        return;
    }

    uint32_t old = *(uint32_t*)(buf + entry_offset);
    old = (old & 0xF0000000) | (value & 0x0FFFFFFF);
    *(uint32_t*)(buf + entry_offset) = old;

    for (uint32_t f = 0; f < fs.bpb.num_fats; f++) {
        if (ata_write_sectors(fs.fat_start + sector + f * fs.bpb.sectors_per_fat, 1, buf) != 0) {
            return;
        }
    }
}

static uint32_t fat32_find_free_cluster(void) {
    uint32_t total_clusters = fs.bpb.sectors_per_fat * 512 / 4;
    for (uint32_t i = 2; i < total_clusters; i++) {
        if (fat32_get_cluster(i) == FAT32_CLUSTER_FREE) {
            return i;
        }
    }
    return 0;
}

static int fat32_read_cluster(uint32_t cluster, uint8_t* buffer) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
        if (ata_read_sectors(lba + s, 1, buffer + s * 512) != 0) {
            return ERR_DISK;
        }
    }
    return OK;
}

static void fat32_write_cluster(uint32_t cluster, const uint8_t* buffer) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
        if (ata_write_sectors(lba + s, 1, (uint8_t*)(buffer + s * 512)) != 0) {
            return;
        }
    }
}

static int fat32_find_in_dir(uint32_t dir_cluster, const char* filename,
                             fat32_dir_entry_t* output) {
    if (!filename || !output) return ERR_NULL;
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return ERR_MEM;

    uint32_t cluster = dir_cluster;
    uint32_t steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           steps++ < fat32_max_cluster_steps()) {
        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return ERR_DISK;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);

            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return ERR_NOT_FOUND;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            if (strncmp(entry->name, filename, 11) == 0) {
                kmemcpy(output, entry, sizeof(fat32_dir_entry_t));
                kfree(cluster_buf);
                cluster_buf = 0;
                return OK;
            }
        }

        cluster = fat32_get_cluster(cluster);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return ERR_TIMEOUT;
}

static uint32_t fat32_get_dir_cluster(const char* path) {
    if (!path || path[0] == '\0') {
        return fs.root_cluster;
    }

    return fs.root_cluster;
}

int fat32_init(void) {
    fat32_release();

    if (ata_read_sectors(0, 1, boot_sector) != 0) {
        LOG_WARN("FAT32", "Falha ao ler boot sector");
        return ERR_DISK;
    }

    kmemcpy(&fs.bpb, boot_sector, sizeof(fat32_bpb_t));

    uint32_t total_sectors = fs.bpb.total_sectors_large;
    if (total_sectors == 0) total_sectors = fs.bpb.total_sectors_small;
    if (fs.bpb.bytes_per_sector != 512 || fs.bpb.sectors_per_cluster == 0 ||
        fs.bpb.sectors_per_cluster > 128 ||
        (fs.bpb.sectors_per_cluster & (fs.bpb.sectors_per_cluster - 1)) != 0 ||
        fs.bpb.reserved_sectors == 0 || fs.bpb.num_fats == 0 ||
        fs.bpb.num_fats > 4 || fs.bpb.sectors_per_fat == 0 ||
        total_sectors == 0 || fs.bpb.root_cluster < 2) {
        fat32_release();
        return ERR_INVALID;
    }

    fs.fat_start = fs.bpb.reserved_sectors;
    fs.data_start = fs.fat_start + (fs.bpb.num_fats * fs.bpb.sectors_per_fat);
    fs.root_cluster = fs.bpb.root_cluster;
    fs.cluster_size = fs.bpb.sectors_per_cluster * 512;
    if (fs.data_start >= total_sectors) {
        fat32_release();
        return ERR_INVALID;
    }
    fs.total_clusters = (total_sectors - fs.data_start) / fs.bpb.sectors_per_cluster;
    if (fs.total_clusters < 4085) {
        fat32_release();
        return ERR_NOT_FOUND;
    }
    if (fs.root_cluster >= fs.total_clusters + 2) {
        fat32_release();
        return ERR_INVALID;
    }

    if (fs.bpb.sectors_per_fat > 0xFFFFFFFFu / 512) {
        fat32_release();
        return ERR_OVERFLOW;
    }
    uint32_t fat_size = fs.bpb.sectors_per_fat * 512;
    fs.fat = (uint32_t*)kmalloc(fat_size);
    if (!fs.fat) {
        LOG_ERROR("FAT32", "Falha ao alocar FAT");
        fat32_release();
        return ERR_MEM;
    }

    for (uint32_t i = 0; i < fs.bpb.sectors_per_fat; i++) {
        uint8_t sector[512];
        if (ata_read_sectors(fs.fat_start + i, 1, sector) != 0) {
            LOG_ERROR("FAT32", "Falha ao ler FAT");
            fat32_release();
            return ERR_DISK;
        }
        kmemcpy((uint8_t*)fs.fat + i * 512, sector, 512);
    }

    fs.initialized = 1;
    LOG_INFO("FAT32", "Sistema FAT32 inicializado");
    return OK;
}

int fat32_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    if (!fs.initialized) return -1;

    uint32_t dir_cluster = fs.root_cluster;
    fat32_dir_entry_t entry;
    if (fat32_find_in_dir(dir_cluster, filename, &entry) != OK) return -1;

    uint32_t first_cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;
    uint32_t file_size = entry.file_size;
    uint32_t bytes_read = 0;
    uint32_t cluster = first_cluster;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED && bytes_read < max_size &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        uint32_t lba = cluster_to_lba(cluster);

        for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster && bytes_read < max_size; s++) {
            uint8_t sector[512];
            if (ata_read_sectors(lba + s, 1, sector) != 0) {
                return -1;
            }

            uint32_t to_copy = 512;
            if (bytes_read + to_copy > file_size) {
                to_copy = file_size - bytes_read;
            }
            if (bytes_read + to_copy > max_size) {
                to_copy = max_size - bytes_read;
            }

            kmemcpy(buffer + bytes_read, sector, to_copy);
            bytes_read += to_copy;
        }

        cluster = fat32_get_cluster(cluster);
    }

    return bytes_read;
}

int fat32_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    if (!fs.initialized) return -1;

    uint32_t dir_cluster = fs.root_cluster;
    fat32_dir_entry_t entry;
    int entry_result = fat32_find_in_dir(dir_cluster, filename, &entry);
    if (entry_result != OK && entry_result != ERR_NOT_FOUND) return -1;

    uint32_t first_cluster;
    if (entry_result == OK) {
        first_cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;
        uint32_t cluster = first_cluster;
        uint32_t chain_steps = 0;
        while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
               chain_steps++ < FAT32_CHAIN_LIMIT) {
            uint32_t next = fat32_get_cluster(cluster);
            fat32_set_cluster(cluster, FAT32_CLUSTER_FREE);
            cluster = next;
        }
    } else {
        first_cluster = fat32_find_free_cluster();
        if (!first_cluster) return -1;

        uint8_t cluster_buf[512];
        kmemset(cluster_buf, 0, 512);

        uint32_t lba = cluster_to_lba(dir_cluster);
        if (ata_read_sectors(lba, 1, cluster_buf) != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < 512 / 32; i++) {
            fat32_dir_entry_t* e = (fat32_dir_entry_t*)(cluster_buf + i * 32);
            if (e->name[0] == 0x00 || e->name[0] == 0xE5) {
                kmemset(e, 0, sizeof(fat32_dir_entry_t));
                kmemcpy(e->name, filename, 11);
                e->attributes = 0x20;
                e->cluster_high = (first_cluster >> 16) & 0xFFFF;
                e->cluster_low = first_cluster & 0xFFFF;
                e->file_size = size;

                if (ata_write_sectors(lba, 1, cluster_buf) != 0) {
                    return -1;
                }
                break;
            }
        }
    }

    uint32_t cluster = first_cluster;
    uint32_t bytes_written = 0;

    while (bytes_written < size) {
        uint32_t next_cluster = fat32_find_free_cluster();
        if (!next_cluster) return -1;

        uint32_t to_write = fs.cluster_size;
        if (bytes_written + to_write > size) {
            to_write = size - bytes_written;
        }

        uint8_t cluster_buf[512];
        kmemset(cluster_buf, 0, 512);

        for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            kmemset(cluster_buf, 0, 512);
            uint32_t sector_offset = s * 512;
            uint32_t remaining = to_write - sector_offset;
            if (remaining > 512) remaining = 512;
            if (remaining > 0) {
                kmemcpy(cluster_buf, data + bytes_written + sector_offset, remaining);
            }
            uint32_t lba = cluster_to_lba(cluster) + s;
            if (ata_write_sectors(lba, 1, cluster_buf) != 0) {
                return -1;
            }
        }

        fat32_set_cluster(cluster, next_cluster);
        cluster = next_cluster;
        bytes_written += to_write;
    }

    fat32_set_cluster(cluster, FAT32_CLUSTER_END);

    return size;
}

int fat32_delete_file(const char* filename) {
    if (!fs.initialized) return -1;

    uint32_t dir_cluster = fs.root_cluster;
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return -1;

    uint32_t cluster = dir_cluster;
    uint32_t chain_steps = 0;
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);

            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            if (strncmp(entry->name, filename, 11) == 0) {
                uint32_t file_cluster = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;

                uint32_t c = file_cluster;
                uint32_t file_chain_steps = 0;
                while (c >= 2 && c < FAT32_CLUSTER_RESERVED &&
                       file_chain_steps++ < FAT32_CHAIN_LIMIT) {
                    uint32_t next = fat32_get_cluster(c);
                    fat32_set_cluster(c, FAT32_CLUSTER_FREE);
                    c = next;
                }

                entry->name[0] = 0xE5;
                entry->file_size = 0;
                entry->cluster_low = 0;
                entry->cluster_high = 0;

                fat32_write_cluster(cluster, cluster_buf);
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
        }

        cluster = fat32_get_cluster(cluster);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return -1;
}

int fat32_list_dir(void) {
    if (!fs.initialized) return -1;

    int count = 0;
    uint32_t cluster = fs.root_cluster;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
        uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
        if (!cluster_buf) return -1;

        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);

            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return count;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            char name[13];
            int pos = 0;

            for (int j = 0; j < 8; j++) {
                if (entry->name[j] != ' ') {
                    name[pos++] = entry->name[j];
                }
            }

            if (entry->ext[0] != ' ') {
                name[pos++] = '.';
                for (int j = 0; j < 3; j++) {
                    if (entry->ext[j] != ' ') {
                        name[pos++] = entry->ext[j];
                    }
                }
            }
            name[pos] = '\0';

            uint32_t size = entry->file_size;
            char size_str[16];
            int s = 0;
            if (size == 0) {
                size_str[s++] = '0';
            } else {
                char tmp[16];
                int t = 0;
                while (size > 0) { tmp[t++] = '0' + (size % 10); size /= 10; }
                while (t > 0) { size_str[s++] = tmp[--t]; }
            }
            size_str[s] = '\0';

            video_print("  ", 0x07);
            video_print(name, 0x0B);
            video_print("  ", 0x07);

            int padding = 13 - pos;
            for (int p = 0; p < padding; p++) {
                video_print(" ", 0x07);
            }

            video_print(size_str, 0x08);
            video_print(" bytes\n", 0x08);
            count++;
        }

        kfree(cluster_buf);
        cluster_buf = 0;
        cluster = fat32_get_cluster(cluster);
    }

    return count;
}

int fat32_get_file_count(void) {
    if (!fs.initialized) return 0;

    int count = 0;
    uint32_t cluster = fs.root_cluster;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
        uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
        if (!cluster_buf) return count;

        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return count;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);

            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return count;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            count++;
        }

        kfree(cluster_buf);
        cluster_buf = 0;
        cluster = fat32_get_cluster(cluster);
    }

    return count;
}

int fat32_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    if (!fs.initialized) return -1;

    int count = 0;
    uint32_t cluster = fs.root_cluster;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
        uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
        if (!cluster_buf) return -1;

        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);

            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            if (count == index) {
                int pos = 0;
                for (int j = 0; j < 8; j++) {
                    if (entry->name[j] != ' ') {
                        name_out[pos++] = entry->name[j];
                    }
                }
                if (entry->ext[0] != ' ') {
                    name_out[pos++] = '.';
                    for (int j = 0; j < 3; j++) {
                        if (entry->ext[j] != ' ') {
                            name_out[pos++] = entry->ext[j];
                        }
                    }
                }
                name_out[pos] = '\0';

                if (size_out) *size_out = entry->file_size;
                if (attr_out) *attr_out = entry->attributes;
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
            count++;
        }

        kfree(cluster_buf);
        cluster_buf = 0;
        cluster = fat32_get_cluster(cluster);
    }
    return -1;
}

fat32_fs_t* fat32_get_fs(void) {
    return &fs;
}

static void fat32_name_to_str(const char* fat_name, const char* fat_ext, char* out) {
    int pos = 0;
    for (int j = 0; j < 8; j++) {
        if (fat_name[j] != ' ') {
            out[pos++] = fat_name[j];
        }
    }
    if (fat_ext[0] != ' ') {
        out[pos++] = '.';
        for (int j = 0; j < 3; j++) {
            if (fat_ext[j] != ' ') {
                out[pos++] = fat_ext[j];
            }
        }
    }
    out[pos] = '\0';
}

static void fat32_str_to_name(const char* filename, char* fat_name, char* fat_ext) {
    int i, j;
    for (i = 0; i < 8; i++) fat_name[i] = ' ';
    for (i = 0; i < 3; i++) fat_ext[i] = ' ';

    i = 0; j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat_name[j++] = c;
        i++;
    }
    if (filename[i] == '.') {
        i++; j = 0;
        while (filename[i] && j < 3) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat_ext[j++] = c;
            i++;
        }
    }
}

uint32_t fat32_resolve_path(const char* path) {
    if (!fs.initialized) return 0;

    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return fs.root_cluster;
    }

    uint32_t current_cluster = fs.root_cluster;
    const char* p = path;

    if (p[0] == '/') p++;

    while (*p) {
        char component[13];
        int ci = 0;

        while (*p && *p != '/' && ci < 12) {
            component[ci++] = *p++;
        }
        component[ci] = '\0';

        if (*p == '/') p++;

        char fat_name[8], fat_ext[3];
        fat32_str_to_name(component, fat_name, fat_ext);

        char fat12_name[11];
        kmemcpy(fat12_name, fat_name, 8);
        kmemcpy(fat12_name + 8, fat_ext, 3);

        fat32_dir_entry_t entry;
        if (fat32_find_in_dir(current_cluster, fat12_name, &entry) != OK) return 0;

        if (!(entry.attributes & 0x10)) {
            return 0;
        }

        current_cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;
    }

    return current_cluster;
}

int fat32_get_file_count_at(uint32_t dir_cluster) {
    if (!fs.initialized) return 0;

    int count = 0;
    uint32_t cluster = dir_cluster;
    uint32_t chain_steps = 0;
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return count;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return count;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;
            count++;
        }

        cluster = fat32_get_cluster(cluster);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return count;
}

int fat32_get_file_info_at(uint32_t dir_cluster, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    if (!fs.initialized) return -1;

    int count = 0;
    uint32_t cluster = dir_cluster;
    uint32_t chain_steps = 0;
    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return -1;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            if (count == index) {
                fat32_name_to_str(entry->name, entry->ext, name_out);
                if (size_out) *size_out = entry->file_size;
                if (attr_out) *attr_out = entry->attributes;
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
            count++;
        }

        cluster = fat32_get_cluster(cluster);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return -1;
}

int fat32_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
    if (!fs.initialized || !path) return -1;

    char dir_path[256];
    char filename[13];
    int last_slash = -1;

    int len = 0;
    for (; path[len]; len++) {
        dir_path[len] = path[len];
        if (path[len] == '/') last_slash = len;
    }
    dir_path[len] = '\0';

    if (last_slash < 0) {
        return fat32_read_file(path, buffer, max_size);
    }

    int fi = 0;
    for (int i = last_slash + 1; path[i]; i++) {
        filename[fi++] = path[i];
    }
    filename[fi] = '\0';
    dir_path[last_slash] = '\0';

    uint32_t dir_cluster = fat32_resolve_path(dir_path);

    char fat_name[8], fat_ext[3];
    fat32_str_to_name(filename, fat_name, fat_ext);
    char fat12_name[11];
    kmemcpy(fat12_name, fat_name, 8);
    kmemcpy(fat12_name + 8, fat_ext, 3);

    fat32_dir_entry_t entry;
    if (fat32_find_in_dir(dir_cluster, fat12_name, &entry) != OK) return -1;

    uint32_t first_cluster = ((uint32_t)entry.cluster_high << 16) | entry.cluster_low;
    uint32_t file_size = entry.file_size;
    uint32_t bytes_read = 0;
    uint32_t cluster = first_cluster;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED && bytes_read < max_size &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        uint32_t lba = cluster_to_lba(cluster);

        for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster && bytes_read < max_size; s++) {
            uint8_t sector[512];
            if (ata_read_sectors(lba + s, 1, sector) != 0) {
                return -1;
            }

            uint32_t to_copy = 512;
            if (bytes_read + to_copy > file_size) to_copy = file_size - bytes_read;
            if (bytes_read + to_copy > max_size) to_copy = max_size - bytes_read;

            kmemcpy(buffer + bytes_read, sector, to_copy);
            bytes_read += to_copy;
        }

        cluster = fat32_get_cluster(cluster);
    }

    return bytes_read;
}

int fat32_create_dir_entry(uint32_t dir_cluster, const char* name, uint8_t attributes) {
    if (!fs.initialized) return -1;

    char fat_name[8], fat_ext[3];
    fat32_str_to_name(name, fat_name, fat_ext);

    uint32_t new_cluster = fat32_find_free_cluster();
    if (!new_cluster) return -1;


    fat32_set_cluster(new_cluster, FAT32_CLUSTER_END);

    uint32_t c_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* zero_buf = (uint8_t*)kmalloc(c_size);
    if (zero_buf) {
        kmemset(zero_buf, 0, c_size);
        fat32_write_cluster(new_cluster, zero_buf);
        kfree(zero_buf);
    }


    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return -1;

    uint32_t cluster = dir_cluster;
    int found_slot = 0;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            fat32_set_cluster(new_cluster, FAT32_CLUSTER_FREE);
            return -1;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                kmemset(entry, 0, sizeof(fat32_dir_entry_t));
                kmemcpy(entry->name, fat_name, 8);
                kmemcpy(entry->ext, fat_ext, 3);
                entry->attributes = attributes;
                entry->cluster_high = (new_cluster >> 16) & 0xFFFF;
                entry->cluster_low = new_cluster & 0xFFFF;

                fat32_write_cluster(cluster, cluster_buf);
                found_slot = 1;
                break;
            }
        }

        if (found_slot) break;
        cluster = fat32_get_cluster(cluster);
    }

    kfree(cluster_buf);
    cluster_buf = 0;

    if (!found_slot) {
        fat32_set_cluster(new_cluster, FAT32_CLUSTER_FREE);
        return -1;
    }

    for (uint32_t f = 0; f < fs.bpb.num_fats; f++) {
        uint32_t fat_sectors = fs.bpb.sectors_per_fat;
        for (uint32_t i = 0; i < fat_sectors; i++) {
            uint8_t buf[512];
            if (ata_read_sectors(fs.fat_start + i, 1, buf) == 0) {
                if (ata_write_sectors(fs.fat_start + i + f * fs.bpb.sectors_per_fat, 1, buf) != 0) {
                    return -1;
                }
            }
        }
    }

    return 0;
}

int fat32_write_file_in_dir(uint32_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size) {
    if (!fs.initialized) return -1;

    char fat_name[8], fat_ext[3];
    fat32_str_to_name(filename, fat_name, fat_ext);
    char fat12_name[11];
    kmemcpy(fat12_name, fat_name, 8);
    kmemcpy(fat12_name + 8, fat_ext, 3);

    fat32_dir_entry_t existing_entry;
    int existing_result = fat32_find_in_dir(dir_cluster, fat12_name, &existing_entry);
    if (existing_result != OK && existing_result != ERR_NOT_FOUND) return -1;
    int existing = existing_result == OK;

    uint32_t first_cluster;
    if (existing) {
        first_cluster = ((uint32_t)existing_entry.cluster_high << 16) | existing_entry.cluster_low;
        uint32_t cluster = first_cluster;
        uint32_t old_chain_steps = 0;
        while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
               old_chain_steps++ < FAT32_CHAIN_LIMIT) {
            uint32_t next = fat32_get_cluster(cluster);
            fat32_set_cluster(cluster, FAT32_CLUSTER_FREE);
            cluster = next;
        }
    } else {
        first_cluster = fat32_find_free_cluster();
        if (!first_cluster) return -1;
    }

    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return -1;

    uint32_t cluster = dir_cluster;
    int found_slot = existing ? 0 : 1;

    if (!existing) {
        uint32_t directory_steps = 0;
        while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
               directory_steps++ < FAT32_CHAIN_LIMIT) {
            if (fat32_read_cluster(cluster, cluster_buf) != OK) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }

            uint32_t entries_per_cluster = cluster_size / 32;
            for (uint32_t i = 0; i < entries_per_cluster; i++) {
                fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);
                if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                    kmemset(entry, 0, sizeof(fat32_dir_entry_t));
                    kmemcpy(entry->name, fat_name, 8);
                    kmemcpy(entry->ext, fat_ext, 3);
                    entry->attributes = 0x20;
                    entry->cluster_high = (first_cluster >> 16) & 0xFFFF;
                    entry->cluster_low = first_cluster & 0xFFFF;
                    entry->file_size = size;

                    fat32_write_cluster(cluster, cluster_buf);
                    found_slot = 1;
                    break;
                }
            }

            if (found_slot) break;
            cluster = fat32_get_cluster(cluster);
        }
    }

    if (!found_slot) {
        kfree(cluster_buf);
        cluster_buf = 0;
        return -1;
    }

    if (!existing) {
        kfree(cluster_buf);
        cluster_buf = 0;
    }

    cluster = first_cluster;
    uint32_t bytes_written = 0;

    while (bytes_written < size) {
        uint32_t next_cluster = fat32_find_free_cluster();
        if (!next_cluster) {
            if (cluster_buf) { kfree(cluster_buf); cluster_buf = 0; }
            return -1;
        }

        for (uint32_t s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            uint8_t sector_buf[512];
            kmemset(sector_buf, 0, 512);
            uint32_t remaining = size - bytes_written;
            if (remaining > 512) remaining = 512;
            if (remaining > 0) {
                kmemcpy(sector_buf, data + bytes_written, remaining);
            }
            if (ata_write_sectors(cluster_to_lba(cluster) + s, 1, sector_buf) != 0) {
                if (cluster_buf) { kfree(cluster_buf); cluster_buf = 0; }
                return -1;
            }
            bytes_written += (remaining > 0) ? remaining : 0;
            if (bytes_written >= size) break;
        }

        fat32_set_cluster(cluster, next_cluster);
        cluster = next_cluster;
    }

    fat32_set_cluster(cluster, FAT32_CLUSTER_END);

    if (existing) {
        cluster = dir_cluster;
        uint32_t update_steps = 0;
        while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
               update_steps++ < FAT32_CHAIN_LIMIT) {
            if (fat32_read_cluster(cluster, cluster_buf) != OK) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }

            uint32_t entries_per_cluster = cluster_size / 32;
            for (uint32_t i = 0; i < entries_per_cluster; i++) {
                fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);
                if (strncmp(entry->name, fat12_name, 11) == 0) {
                    entry->file_size = size;
                    entry->cluster_high = (first_cluster >> 16) & 0xFFFF;
                    entry->cluster_low = first_cluster & 0xFFFF;
                    fat32_write_cluster(cluster, cluster_buf);
                    kfree(cluster_buf);
                    cluster_buf = 0;
                    return size;
                }
            }

            cluster = fat32_get_cluster(cluster);
        }
    }

    if (cluster_buf) { kfree(cluster_buf); cluster_buf = 0; }
    return size;
}

int fat32_delete_file_in_dir(uint32_t dir_cluster, const char* filename) {
    if (!fs.initialized) return -1;

    char fat_name[8], fat_ext[3];
    fat32_str_to_name(filename, fat_name, fat_ext);
    char fat12_name[11];
    kmemcpy(fat12_name, fat_name, 8);
    kmemcpy(fat12_name + 8, fat_ext, 3);

    uint32_t cluster_size = fs.bpb.sectors_per_cluster * 512;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(cluster_size);
    if (!cluster_buf) return -1;

    uint32_t cluster = dir_cluster;
    uint32_t chain_steps = 0;
    while (cluster >= 2 && cluster < FAT32_CLUSTER_RESERVED &&
           chain_steps++ < FAT32_CHAIN_LIMIT) {
        if (fat32_read_cluster(cluster, cluster_buf) != OK) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }

        uint32_t entries_per_cluster = cluster_size / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes == 0x0F) continue;

            if (strncmp(entry->name, fat12_name, 11) == 0) {
                uint32_t file_cluster = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;

                uint32_t c = file_cluster;
                uint32_t file_chain_steps = 0;
                while (c >= 2 && c < FAT32_CLUSTER_RESERVED &&
                       file_chain_steps++ < FAT32_CHAIN_LIMIT) {
                    uint32_t next = fat32_get_cluster(c);
                    fat32_set_cluster(c, FAT32_CLUSTER_FREE);
                    c = next;
                }

                entry->name[0] = 0xE5;
                entry->file_size = 0;
                entry->cluster_low = 0;
                entry->cluster_high = 0;

                fat32_write_cluster(cluster, cluster_buf);
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
        }

        cluster = fat32_get_cluster(cluster);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return -1;
}
