#include "fs/fat12.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"

static fat12_fs_t fs;
static uint8_t boot_sector[512];

static void fat12_release(void) {
    if (fs.fat) { kfree(fs.fat); fs.fat = 0; }
    if (fs.root_dir) { kfree(fs.root_dir); fs.root_dir = 0; }
    if (fs.data_area) { kfree(fs.data_area); fs.data_area = 0; }
    kmemset(&fs, 0, sizeof(fat12_fs_t));
}

static int fat12_validate_bpb(void) {
    uint32_t total_sectors = fs.bpb.total_sectors;
    if (total_sectors == 0) total_sectors = fs.bpb.large_sector_count;

    if (fs.bpb.bytes_per_sector != 512 || fs.bpb.sectors_per_cluster == 0 ||
        fs.bpb.sectors_per_cluster > 128 ||
        (fs.bpb.sectors_per_cluster & (fs.bpb.sectors_per_cluster - 1)) != 0 ||
        fs.bpb.reserved_sectors == 0 || fs.bpb.num_fats == 0 || fs.bpb.num_fats > 4 ||
        fs.bpb.root_entries == 0 || fs.bpb.sectors_per_fat == 0 || total_sectors == 0) {
        return ERR_INVALID;
    }

    uint32_t root_sectors = (fs.bpb.root_entries * 32 + 511) / 512;
    uint32_t data_start = fs.bpb.reserved_sectors +
        fs.bpb.num_fats * fs.bpb.sectors_per_fat + root_sectors;
    if (data_start >= total_sectors) return ERR_INVALID;

    uint32_t clusters = (total_sectors - data_start) / fs.bpb.sectors_per_cluster;
    if (clusters >= 4085) return ERR_NOT_FOUND;
    if (clusters < 1) return ERR_INVALID;

    uint32_t required_fat_bytes = ((clusters + 2) * 3 + 1) / 2;
    if ((uint32_t)fs.bpb.sectors_per_fat * 512 < required_fat_bytes) return ERR_INVALID;
    return OK;
}

static int strncmp(const char* a, const char* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static void to_upper(char* str, int max_len) {
    for (int i = 0; i < max_len && str[i]; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            str[i] -= 32;
        }
    }
}

#define FAT12_CHAIN_LIMIT 4096

int fat12_init(void) {
    fat12_release();

    if (ata_read_sectors(0, 1, boot_sector) != 0) {
        LOG_WARN("FAT12", "Falha ao ler boot sector");
        return ERR_DISK;
    }

    kmemcpy(&fs.bpb, boot_sector, sizeof(fat12_bpb_t));
    int validation = fat12_validate_bpb();
    if (validation != OK) {
        fat12_release();
        return validation;
    }

    fs.fat_start = fs.bpb.reserved_sectors;
    fs.root_start = fs.fat_start + (fs.bpb.num_fats * fs.bpb.sectors_per_fat);
    fs.data_start = fs.root_start + ((fs.bpb.root_entries * 32 + fs.bpb.bytes_per_sector - 1) / fs.bpb.bytes_per_sector);

    uint32_t fat_size = fs.bpb.sectors_per_fat * fs.bpb.bytes_per_sector;
    fs.fat = (uint16_t*)kmalloc(fat_size);
    if (!fs.fat) {
        LOG_ERROR("FAT12", "Falha ao alocar FAT");
        fat12_release();
        return ERR_MEM;
    }

    for (int i = 0; i < fs.bpb.sectors_per_fat; i++) {
        uint8_t sector[512];
        if (ata_read_sectors(fs.fat_start + i, 1, sector) != 0) {
            LOG_ERROR("FAT12", "Falha ao ler FAT");
            fat12_release();
            return ERR_DISK;
        }
        kmemcpy((uint8_t*)fs.fat + i * fs.bpb.bytes_per_sector, sector, fs.bpb.bytes_per_sector);
    }

    uint32_t root_size = fs.bpb.root_entries * 32;
    fs.root_dir = (fat12_dir_entry_t*)kmalloc(root_size);
    if (!fs.root_dir) {
        LOG_ERROR("FAT12", "Falha ao alocar diretorio raiz");
        fat12_release();
        return ERR_MEM;
    }

    uint32_t root_sectors = (root_size + fs.bpb.bytes_per_sector - 1) / fs.bpb.bytes_per_sector;
    for (uint32_t i = 0; i < root_sectors; i++) {
        uint8_t sector[512];
        if (ata_read_sectors(fs.root_start + i, 1, sector) != 0) {
            LOG_ERROR("FAT12", "Falha ao ler diretorio raiz");
            fat12_release();
            return ERR_DISK;
        }
        uint32_t copied = i * fs.bpb.bytes_per_sector;
        uint32_t remaining = root_size - copied;
        uint32_t copy_size = remaining < fs.bpb.bytes_per_sector ? remaining : fs.bpb.bytes_per_sector;
        kmemcpy((uint8_t*)fs.root_dir + copied, sector, copy_size);
    }

    fs.initialized = 1;
    LOG_INFO("FAT12", "Sistema FAT12 inicializado");
    return OK;
}

static uint16_t fat12_get_cluster(uint16_t cluster) {
    uint32_t offset = cluster + (cluster / 2);
    uint16_t* fat = fs.fat;
    uint16_t value = *(uint16_t*)((uint8_t*)fat + offset);
    if (cluster & 1) {
        value >>= 4;
    } else {
        value &= 0x0FFF;
    }
    return value;
}

static void fat12_set_cluster(uint16_t cluster, uint16_t value) {
    uint32_t offset = cluster + (cluster / 2);
    uint16_t* fat = fs.fat;
    uint16_t old = *(uint16_t*)((uint8_t*)fat + offset);
    if (cluster & 1) {
        old = (old & 0x000F) | (value << 4);
    } else {
        old = (old & 0xF000) | (value);
    }
    *(uint16_t*)((uint8_t*)fat + offset) = old;
}

static fat12_dir_entry_t* fat12_find_entry(const char* filename) {
    if (!fs.initialized) return 0;

    char fat12_name[11];
    int i, j;
    for (i = 0; i < 8; i++) fat12_name[i] = ' ';
    for (i = 8; i < 11; i++) fat12_name[i] = ' ';

    i = 0;
    j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat12_name[j++] = c;
        i++;
    }

    if (filename[i] == '.') {
        i++;
        j = 0;
        while (filename[i] && j < 3) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[8 + j++] = c;
            i++;
        }
    }

    for (uint32_t idx = 0; idx < fs.bpb.root_entries; idx++) {
        if (fs.root_dir[idx].name[0] == 0x00) break;
        if (fs.root_dir[idx].name[0] == 0xE5) continue;
        if (fs.root_dir[idx].attributes & 0x08) continue;

        if (strncmp(fs.root_dir[idx].name, fat12_name, 11) == 0) {
            return &fs.root_dir[idx];
        }
    }
    return 0;
}

static uint16_t fat12_find_free_cluster(void) {
    for (uint16_t i = 2; i < 0xFF8; i++) {
        if (fat12_get_cluster(i) == FAT12_CLUSTER_FREE) {
            return i;
        }
    }
    return 0;
}

int fat12_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    fat12_dir_entry_t* entry = fat12_find_entry(filename);
    if (!entry) return -1;

    uint32_t bytes_read = 0;
    uint16_t cluster = entry->cluster_low;
    uint32_t file_size = entry->file_size;
    uint32_t chain_steps = 0;

    while (cluster < 0xFF8 && cluster != 0 && bytes_read < max_size &&
           chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            uint8_t sector[512];
            if (ata_read_sectors(data_lba + s, 1, sector) != 0) {
                return -1;
            }

            uint32_t to_copy = fs.bpb.bytes_per_sector;
            if (bytes_read + to_copy > file_size) {
                to_copy = file_size - bytes_read;
            }
            if (bytes_read + to_copy > max_size) {
                to_copy = max_size - bytes_read;
            }

            kmemcpy(buffer + bytes_read, sector, to_copy);
            bytes_read += to_copy;

            if (bytes_read >= file_size || bytes_read >= max_size) {
                break;
            }
        }

        cluster = fat12_get_cluster(cluster);
    }

    return bytes_read;
}

int fat12_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    fat12_dir_entry_t* entry = fat12_find_entry(filename);

    if (!entry) {
        for (uint32_t i = 0; i < fs.bpb.root_entries; i++) {
            if (fs.root_dir[i].name[0] == 0x00 || fs.root_dir[i].name[0] == 0xE5) {
                entry = &fs.root_dir[i];
                kmemset(entry, 0, sizeof(fat12_dir_entry_t));
                kmemcpy(entry->name, filename, 11);
                entry->attributes = 0x20;
                break;
            }
        }
        if (!entry) return -1;
    }

    uint16_t first_cluster = fat12_find_free_cluster();
    if (!first_cluster) return -1;

    entry->cluster_low = first_cluster;
    entry->file_size = size;

    uint16_t cluster = first_cluster;
    uint32_t bytes_written = 0;

    while (bytes_written < size) {
        uint32_t data_lba = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            uint8_t sector[512];
            kmemset(sector, 0, 512);

            uint32_t to_copy = fs.bpb.bytes_per_sector;
            if (bytes_written + to_copy > size) {
                to_copy = size - bytes_written;
            }

            kmemcpy(sector, data + bytes_written, to_copy);
            if (ata_write_sectors(data_lba + s, 1, sector) != 0) {
                return -1;
            }

            bytes_written += to_copy;
            if (bytes_written >= size) break;
        }

        if (bytes_written < size) {
            uint16_t next = fat12_find_free_cluster();
            if (!next) return -1;
            fat12_set_cluster(cluster, next);
            cluster = next;
        }
    }

    fat12_set_cluster(cluster, FAT12_CLUSTER_END);

    for (int i = 0; i < fs.bpb.sectors_per_fat; i++) {
        if (ata_write_sectors(fs.fat_start + i, 1, (uint8_t*)fs.fat + i * 512) != 0) {
            return -1;
        }
    }

    uint32_t root_size = fs.bpb.root_entries * 32;
    uint32_t root_sectors = (root_size + 511) / 512;
    for (uint32_t i = 0; i < root_sectors; i++) {
        if (ata_write_sectors(fs.root_start + i, 1, (uint8_t*)fs.root_dir + i * 512) != 0) {
            return -1;
        }
    }

    return size;
}

int fat12_list_dir(void) {
    if (!fs.initialized) return -1;

    int count = 0;
    for (uint32_t i = 0; i < fs.bpb.root_entries; i++) {
        if (fs.root_dir[i].name[0] == 0x00) break;
        if (fs.root_dir[i].name[0] == 0xE5) continue;
        if (fs.root_dir[i].attributes & 0x08) continue;

        char name[13];
        int pos = 0;

        for (int j = 0; j < 8; j++) {
            if (fs.root_dir[i].name[j] != ' ') {
                name[pos++] = fs.root_dir[i].name[j];
            }
        }

        if (fs.root_dir[i].ext[0] != ' ') {
            name[pos++] = '.';
            for (int j = 0; j < 3; j++) {
                if (fs.root_dir[i].ext[j] != ' ') {
                    name[pos++] = fs.root_dir[i].ext[j];
                }
            }
        }
        name[pos] = '\0';

        uint32_t size = fs.root_dir[i].file_size;
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

    return count;
}

fat12_fs_t* fat12_get_fs(void) {
    return &fs;
}

int fat12_delete_file(const char* filename) {
    if (!fs.initialized) return -1;

    fat12_dir_entry_t* entry = fat12_find_entry(filename);
    if (!entry) return -1;

    uint16_t cluster = entry->cluster_low;
    uint32_t chain_steps = 0;

    while (cluster >= 2 && cluster < 0xFF8 && chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint16_t next = fat12_get_cluster(cluster);
        fat12_set_cluster(cluster, FAT12_CLUSTER_FREE);
        cluster = next;
    }

    entry->name[0] = 0xE5;
    entry->file_size = 0;
    entry->cluster_low = 0;

    for (int j = 0; j < fs.bpb.sectors_per_fat; j++) {
        if (ata_write_sectors(fs.fat_start + j, 1, (uint8_t*)fs.fat + j * 512) != 0) {
            return -1;
        }
    }

    uint32_t root_size = fs.bpb.root_entries * 32;
    uint32_t root_sectors = (root_size + 511) / 512;
    for (uint32_t j = 0; j < root_sectors; j++) {
        if (ata_write_sectors(fs.root_start + j, 1, (uint8_t*)fs.root_dir + j * 512) != 0) {
            return -1;
        }
    }

    return 0;
}

int fat12_get_file_count(void) {
    if (!fs.initialized) return 0;

    int count = 0;
    for (uint32_t i = 0; i < fs.bpb.root_entries; i++) {
        if (fs.root_dir[i].name[0] == 0x00) break;
        if (fs.root_dir[i].name[0] == 0xE5) continue;
        if (fs.root_dir[i].attributes & 0x08) continue;
        count++;
    }
    return count;
}

int fat12_get_file_info(int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    if (!fs.initialized) return -1;

    int count = 0;
    for (uint32_t i = 0; i < fs.bpb.root_entries; i++) {
        if (fs.root_dir[i].name[0] == 0x00) break;
        if (fs.root_dir[i].name[0] == 0xE5) continue;
        if (fs.root_dir[i].attributes & 0x08) continue;

        if (count == index) {
            int pos = 0;
            for (int j = 0; j < 8; j++) {
                if (fs.root_dir[i].name[j] != ' ') {
                    name_out[pos++] = fs.root_dir[i].name[j];
                }
            }
            if (fs.root_dir[i].ext[0] != ' ') {
                name_out[pos++] = '.';
                for (int j = 0; j < 3; j++) {
                    if (fs.root_dir[i].ext[j] != ' ') {
                        name_out[pos++] = fs.root_dir[i].ext[j];
                    }
                }
            }
            name_out[pos] = '\0';

            if (size_out) *size_out = fs.root_dir[i].file_size;
            if (attr_out) *attr_out = fs.root_dir[i].attributes;
            return 0;
        }
        count++;
    }
    return -1;
}

static void fat12_name_to_str(const char* fat_name, const char* fat_ext, char* out) {
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

static fat12_dir_entry_t* fat12_read_dir_cluster(uint16_t cluster, fat12_dir_entry_t* entries, uint32_t max_entries) {
    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return 0;

    uint32_t entry_idx = 0;
    uint16_t c = cluster;
    uint32_t chain_steps = 0;

    while (c >= 2 && c < 0xFF8 && entry_idx < max_entries &&
           chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (c - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (ata_read_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
        }

        uint32_t entries_per_cluster = bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries_per_cluster && entry_idx < max_entries; i++) {
            fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return entries;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes & 0x08) continue;

            kmemcpy(&entries[entry_idx], entry, sizeof(fat12_dir_entry_t));
            entry_idx++;
        }

        c = fat12_get_cluster(c);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return entries;
}

static fat12_dir_entry_t* fat12_find_in_dir(uint16_t dir_cluster, const char* fat12_name) {
    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return 0;

    uint16_t c = dir_cluster;
    uint32_t chain_steps = 0;
    static fat12_dir_entry_t found;

    while (c >= 2 && c < 0xFF8 && chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (c - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (ata_read_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
        }

        uint32_t entries_per_cluster = bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes & 0x08) continue;

            if (strncmp(entry->name, fat12_name, 11) == 0) {
                kmemcpy(&found, entry, sizeof(fat12_dir_entry_t));
                kfree(cluster_buf);
                cluster_buf = 0;
                return &found;
            }
        }

        c = fat12_get_cluster(c);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return 0;
}

uint16_t fat12_resolve_path(const char* path) {
    if (!fs.initialized) return 0;

    if (!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        return 0;
    }

    uint16_t current_cluster = 0;
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

        char fat12_name[11];
        int i, j;
        for (i = 0; i < 8; i++) fat12_name[i] = ' ';
        for (i = 8; i < 11; i++) fat12_name[i] = ' ';

        i = 0; j = 0;
        while (component[i] && component[i] != '.' && j < 8) {
            char c = component[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[j++] = c;
            i++;
        }
        if (component[i] == '.') {
            i++; j = 0;
            while (component[i] && j < 3) {
                char c = component[i];
                if (c >= 'a' && c <= 'z') c -= 32;
                fat12_name[8 + j++] = c;
                i++;
            }
        }

        fat12_dir_entry_t* entry = 0;
        if (current_cluster == 0) {
            for (uint32_t idx = 0; idx < fs.bpb.root_entries; idx++) {
                if (fs.root_dir[idx].name[0] == 0x00) break;
                if (fs.root_dir[idx].name[0] == 0xE5) continue;
                if (fs.root_dir[idx].attributes & 0x08) continue;
                if (strncmp(fs.root_dir[idx].name, fat12_name, 11) == 0) {
                    entry = &fs.root_dir[idx];
                    break;
                }
            }
        } else {
            entry = fat12_find_in_dir(current_cluster, fat12_name);
        }
        
        if (!entry) return 0;

        if (!(entry->attributes & 0x10)) {
            return 0;
        }

        current_cluster = entry->cluster_low;
    }

    return current_cluster;
}

int fat12_get_file_count_at(uint16_t dir_cluster) {
    if (!fs.initialized) return 0;

    if (dir_cluster == 0) {
        return fat12_get_file_count();
    }

    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint32_t max_entries = bytes_per_cluster / 32;
    fat12_dir_entry_t* entries = (fat12_dir_entry_t*)kmalloc(max_entries * sizeof(fat12_dir_entry_t));
    if (!entries) return 0;

    fat12_dir_entry_t* result = fat12_read_dir_cluster(dir_cluster, entries, max_entries);
    if (!result) {
        kfree(entries);
        entries = 0;
        return 0;
    }

    int count = 0;
    for (uint32_t i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & 0x08) continue;
        count++;
    }

    kfree(entries);
    entries = 0;
    return count;
}

int fat12_get_file_info_at(uint16_t dir_cluster, int index, char* name_out, uint32_t* size_out, uint8_t* attr_out) {
    if (!fs.initialized) return -1;

    if (dir_cluster == 0) {
        return fat12_get_file_info(index, name_out, size_out, attr_out);
    }

    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint32_t max_entries = bytes_per_cluster / 32;
    fat12_dir_entry_t* entries = (fat12_dir_entry_t*)kmalloc(max_entries * sizeof(fat12_dir_entry_t));
    if (!entries) return -1;

    fat12_dir_entry_t* result = fat12_read_dir_cluster(dir_cluster, entries, max_entries);
    if (!result) {
        kfree(entries);
        entries = 0;
        return -1;
    }

    int count = 0;
    for (uint32_t i = 0; i < max_entries; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & 0x08) continue;

        if (count == index) {
            fat12_name_to_str(entries[i].name, entries[i].ext, name_out);
            if (size_out) *size_out = entries[i].file_size;
            if (attr_out) *attr_out = entries[i].attributes;
            kfree(entries);
            entries = 0;
            return 0;
        }
        count++;
    }

    kfree(entries);
    entries = 0;
    return -1;
}

int fat12_read_file_at(const char* path, uint8_t* buffer, uint32_t max_size) {
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
        return fat12_read_file(path, buffer, max_size);
    }

    int fi = 0;
    for (int i = last_slash + 1; path[i]; i++) {
        filename[fi++] = path[i];
    }
    filename[fi] = '\0';

    dir_path[last_slash] = '\0';

    uint16_t dir_cluster = fat12_resolve_path(dir_path);
    if (dir_path[0] == '\0') dir_cluster = 0;

    char fat12_name[11];
    int i, j;
    for (i = 0; i < 8; i++) fat12_name[i] = ' ';
    for (i = 8; i < 11; i++) fat12_name[i] = ' ';
    i = 0; j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat12_name[j++] = c;
        i++;
    }
    if (filename[i] == '.') {
        i++; j = 0;
        while (filename[i] && j < 3) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[8 + j++] = c;
            i++;
        }
    }

    fat12_dir_entry_t* entry = fat12_find_in_dir(dir_cluster, fat12_name);
    if (!entry) return -1;

    uint32_t bytes_read = 0;
    uint16_t cluster = entry->cluster_low;
    uint32_t file_size = entry->file_size;
    uint32_t chain_steps = 0;

    while (cluster < 0xFF8 && cluster != 0 && bytes_read < max_size &&
           chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            uint8_t sector[512];
            if (ata_read_sectors(data_lba + s, 1, sector) != 0) {
                return -1;
            }

            uint32_t to_copy = fs.bpb.bytes_per_sector;
            if (bytes_read + to_copy > file_size) {
                to_copy = file_size - bytes_read;
            }
            if (bytes_read + to_copy > max_size) {
                to_copy = max_size - bytes_read;
            }

            kmemcpy(buffer + bytes_read, sector, to_copy);
            bytes_read += to_copy;

            if (bytes_read >= file_size || bytes_read >= max_size) {
                break;
            }
        }

        cluster = fat12_get_cluster(cluster);
    }

    return bytes_read;
}

int fat12_write_file_in_dir(uint16_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size) {
    if (!fs.initialized) return -1;

    char fat12_name[11];
    int i, j;
    for (i = 0; i < 8; i++) fat12_name[i] = ' ';
    for (i = 8; i < 11; i++) fat12_name[i] = ' ';
    i = 0; j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat12_name[j++] = c;
        i++;
    }
    if (filename[i] == '.') {
        i++; j = 0;
        while (filename[i] && j < 3) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[8 + j++] = c;
            i++;
        }
    }

    uint16_t first_cluster = fat12_find_free_cluster();
    if (!first_cluster) return -1;

    if (dir_cluster == 0) {
        fat12_dir_entry_t* entry = 0;
        for (uint32_t idx = 0; idx < fs.bpb.root_entries; idx++) {
            if (fs.root_dir[idx].name[0] == 0x00 || fs.root_dir[idx].name[0] == 0xE5) {
                entry = &fs.root_dir[idx];
                kmemset(entry, 0, sizeof(fat12_dir_entry_t));
                kmemcpy(entry->name, fat12_name, 11);
                entry->attributes = 0x20;
                break;
            }
        }
        if (!entry) return -1;

        entry->cluster_low = first_cluster;
        entry->file_size = size;

        uint16_t cluster = first_cluster;
        uint32_t bytes_written = 0;

        while (bytes_written < size) {
            uint32_t data_lba = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;

            for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                uint8_t sector[512];
                kmemset(sector, 0, 512);
                uint32_t to_copy = fs.bpb.bytes_per_sector;
                if (bytes_written + to_copy > size) to_copy = size - bytes_written;
                kmemcpy(sector, data + bytes_written, to_copy);
                if (ata_write_sectors(data_lba + s, 1, sector) != 0) return -1;
                bytes_written += to_copy;
                if (bytes_written >= size) break;
            }

            if (bytes_written < size) {
                uint16_t next = fat12_find_free_cluster();
                if (!next) return -1;
                fat12_set_cluster(cluster, next);
                cluster = next;
            }
        }

        fat12_set_cluster(cluster, FAT12_CLUSTER_END);

        for (int x = 0; x < fs.bpb.sectors_per_fat; x++) {
            if (ata_write_sectors(fs.fat_start + x, 1, (uint8_t*)fs.fat + x * 512) != 0) return -1;
        }

        uint32_t root_size = fs.bpb.root_entries * 32;
        uint32_t root_sectors = (root_size + 511) / 512;
        for (uint32_t x = 0; x < root_sectors; x++) {
            if (ata_write_sectors(fs.root_start + x, 1, (uint8_t*)fs.root_dir + x * 512) != 0) return -1;
        }

        return size;
    }

    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return -1;

    uint16_t c = dir_cluster;
    int found_slot = 0;
    uint32_t chain_steps = 0;

    while (c >= 2 && c < 0xFF8 && chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (c - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (ata_read_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
        }

        uint32_t entries_per_cluster = bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                kmemset(entry, 0, sizeof(fat12_dir_entry_t));
                kmemcpy(entry->name, fat12_name, 11);
                entry->attributes = 0x20;
                entry->cluster_low = first_cluster;
                entry->file_size = size;

                for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                    if (ata_write_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                        kfree(cluster_buf);
                        cluster_buf = 0;
                        return -1;
                    }
                }

                found_slot = 1;
                break;
            }
        }

        if (found_slot) break;
        c = fat12_get_cluster(c);
    }

    if (!found_slot) {
        kfree(cluster_buf);
        cluster_buf = 0;
        return -1;
    }

    uint16_t cluster = first_cluster;
    uint32_t bytes_written = 0;

    while (bytes_written < size) {
        uint32_t data_lba = fs.data_start + (cluster - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            uint8_t sector[512];
            kmemset(sector, 0, 512);
            uint32_t to_copy = fs.bpb.bytes_per_sector;
            if (bytes_written + to_copy > size) to_copy = size - bytes_written;
            kmemcpy(sector, data + bytes_written, to_copy);
            if (ata_write_sectors(data_lba + s, 1, sector) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            bytes_written += to_copy;
            if (bytes_written >= size) break;
        }

        if (bytes_written < size) {
            uint16_t next = fat12_find_free_cluster();
            if (!next) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            fat12_set_cluster(cluster, next);
            cluster = next;
        }
    }

    fat12_set_cluster(cluster, FAT12_CLUSTER_END);

    for (int x = 0; x < fs.bpb.sectors_per_fat; x++) {
        if (ata_write_sectors(fs.fat_start + x, 1, (uint8_t*)fs.fat + x * 512) != 0) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return size;
}

int fat12_delete_file_in_dir(uint16_t dir_cluster, const char* filename) {
    if (!fs.initialized) return -1;

    char fat12_name[11];
    int i, j;
    for (i = 0; i < 8; i++) fat12_name[i] = ' ';
    for (i = 8; i < 11; i++) fat12_name[i] = ' ';
    i = 0; j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat12_name[j++] = c;
        i++;
    }
    if (filename[i] == '.') {
        i++; j = 0;
        while (filename[i] && j < 3) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[8 + j++] = c;
            i++;
        }
    }

    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return -1;

    uint16_t c = (dir_cluster == 0) ? 0 : dir_cluster;

    if (dir_cluster == 0) {
        fat12_dir_entry_t* entry = fat12_find_in_dir(0, fat12_name);
        if (!entry) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }

        uint16_t cluster = entry->cluster_low;
        uint32_t chain_steps = 0;
        while (cluster >= 2 && cluster < 0xFF8 && chain_steps++ < FAT12_CHAIN_LIMIT) {
            uint16_t next = fat12_get_cluster(cluster);
            fat12_set_cluster(cluster, FAT12_CLUSTER_FREE);
            cluster = next;
        }

        entry->name[0] = 0xE5;
        entry->file_size = 0;
        entry->cluster_low = 0;

        for (int x = 0; x < fs.bpb.sectors_per_fat; x++) {
            if (ata_write_sectors(fs.fat_start + x, 1, (uint8_t*)fs.fat + x * 512) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
        }

        uint32_t root_size = fs.bpb.root_entries * 32;
        uint32_t root_sectors = (root_size + 511) / 512;
        for (uint32_t x = 0; x < root_sectors; x++) {
            if (ata_write_sectors(fs.root_start + x, 1, (uint8_t*)fs.root_dir + x * 512) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
        }

        kfree(cluster_buf);
        cluster_buf = 0;
        return 0;
    }

    uint32_t chain_steps = 0;
    while (c >= 2 && c < 0xFF8 && chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (c - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (ata_read_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
        }

        uint32_t entries_per_cluster = bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
            if (entry->name[0] == 0xE5) continue;
            if (entry->attributes & 0x08) continue;

            if (strncmp(entry->name, fat12_name, 11) == 0) {
                uint16_t cluster = entry->cluster_low;
                uint32_t file_chain_steps = 0;
                while (cluster >= 2 && cluster < 0xFF8 &&
                       file_chain_steps++ < FAT12_CHAIN_LIMIT) {
                    uint16_t next = fat12_get_cluster(cluster);
                    fat12_set_cluster(cluster, FAT12_CLUSTER_FREE);
                    cluster = next;
                }

                entry->name[0] = 0xE5;
                entry->file_size = 0;
                entry->cluster_low = 0;

                for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                    if (ata_write_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                        kfree(cluster_buf);
                        cluster_buf = 0;
                        return -1;
                    }
                }

                for (int x = 0; x < fs.bpb.sectors_per_fat; x++) {
                    if (ata_write_sectors(fs.fat_start + x, 1, (uint8_t*)fs.fat + x * 512) != 0) {
                        kfree(cluster_buf);
                        cluster_buf = 0;
                        return -1;
                    }
                }

                kfree(cluster_buf);
                cluster_buf = 0;
                return 0;
            }
        }

        c = fat12_get_cluster(c);
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return -1;
}

int fat12_create_dir_entry(uint16_t dir_cluster, const char* name, uint8_t attributes) {
    if (!fs.initialized) return -1;

    char fat12_name[11];
    int i, j;
    for (i = 0; i < 8; i++) fat12_name[i] = ' ';
    for (i = 8; i < 11; i++) fat12_name[i] = ' ';
    i = 0; j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        fat12_name[j++] = c;
        i++;
    }
    if (name[i] == '.') {
        i++; j = 0;
        while (name[i] && j < 3) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            fat12_name[8 + j++] = c;
            i++;
        }
    }

    uint16_t new_cluster = fat12_find_free_cluster();
    if (!new_cluster) return -1;

    if (dir_cluster == 0) {
        for (uint32_t idx = 0; idx < fs.bpb.root_entries; idx++) {
            if (fs.root_dir[idx].name[0] == 0x00 || fs.root_dir[idx].name[0] == 0xE5) {
                kmemset(&fs.root_dir[idx], 0, sizeof(fat12_dir_entry_t));
                kmemcpy(fs.root_dir[idx].name, fat12_name, 11);
                fs.root_dir[idx].attributes = attributes;
                fs.root_dir[idx].cluster_low = new_cluster;


                fat12_set_cluster(new_cluster, FAT12_CLUSTER_END);
                
                uint32_t cluster_size_1 = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
                uint8_t* zero_buf_1 = (uint8_t*)kmalloc(cluster_size_1);
                if (zero_buf_1) {
                    kmemset(zero_buf_1, 0, cluster_size_1);
                    uint32_t data_lba = fs.data_start + (new_cluster - 2) * fs.bpb.sectors_per_cluster;
                    for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                        ata_write_sectors(data_lba + s, 1, zero_buf_1 + s * fs.bpb.bytes_per_sector);
                    }
                    kfree(zero_buf_1);
                }


                for (int x = 0; x < fs.bpb.sectors_per_fat; x++) {
                    if (ata_write_sectors(fs.fat_start + x, 1, (uint8_t*)fs.fat + x * 512) != 0) return -1;
                }

                uint32_t root_size = fs.bpb.root_entries * 32;
                uint32_t root_sectors = (root_size + 511) / 512;
                for (uint32_t x = 0; x < root_sectors; x++) {
                    if (ata_write_sectors(fs.root_start + x, 1, (uint8_t*)fs.root_dir + x * 512) != 0) return -1;
                }

                return 0;
            }
        }
        return -1;
    }

    uint32_t bytes_per_cluster = fs.bpb.sectors_per_cluster * fs.bpb.bytes_per_sector;
    uint8_t* cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return -1;

    uint16_t c = dir_cluster;
    int found_slot = 0;
    uint32_t chain_steps = 0;

    while (c >= 2 && c < 0xFF8 && chain_steps++ < FAT12_CHAIN_LIMIT) {
        uint32_t data_lba = fs.data_start + (c - 2) * fs.bpb.sectors_per_cluster;

        for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
            if (ata_read_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                kfree(cluster_buf);
                cluster_buf = 0;
                return -1;
            }
        }

        uint32_t entries_per_cluster = bytes_per_cluster / 32;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat12_dir_entry_t* entry = (fat12_dir_entry_t*)(cluster_buf + i * 32);
            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                kmemset(entry, 0, sizeof(fat12_dir_entry_t));
                kmemcpy(entry->name, fat12_name, 11);
                entry->attributes = attributes;
                entry->cluster_low = new_cluster;

                for (int s = 0; s < fs.bpb.sectors_per_cluster; s++) {
                    if (ata_write_sectors(data_lba + s, 1, cluster_buf + s * fs.bpb.bytes_per_sector) != 0) {
                        kfree(cluster_buf);
                        cluster_buf = 0;
                        return -1;
                    }
                }

                found_slot = 1;
                break;
            }
        }

        if (found_slot) break;
        c = fat12_get_cluster(c);
    }

    if (!found_slot) {
        kfree(cluster_buf);
        cluster_buf = 0;
        return -1;
    }

    fat12_set_cluster(new_cluster, FAT12_CLUSTER_END);

    for (int x = 0; x < fs.bpb.sectors_per_fat; x++) {
        if (ata_write_sectors(fs.fat_start + x, 1, (uint8_t*)fs.fat + x * 512) != 0) {
            kfree(cluster_buf);
            cluster_buf = 0;
            return -1;
        }
    }

    kfree(cluster_buf);
    cluster_buf = 0;
    return 0;
}
