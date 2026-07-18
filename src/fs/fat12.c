#include "fat12.h"
#include "memory.h"
#include "video.h"
#include "panic.h"

static fat12_fs_t fs;
static uint8_t boot_sector[512];

static void memset(void* dst, uint8_t val, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = val;
    }
}

static void memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
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

void fat12_init(void) {
    memset(&fs, 0, sizeof(fat12_fs_t));

    if (ata_read_sectors(0, 1, boot_sector) != 0) {
        panic("FAT12: Falha ao ler boot sector!");
        return;
    }

    memcpy(&fs.bpb, boot_sector, sizeof(fat12_bpb_t));

    fs.fat_start = fs.bpb.reserved_sectors;
    fs.root_start = fs.fat_start + (fs.bpb.num_fats * fs.bpb.sectors_per_fat);
    fs.data_start = fs.root_start + ((fs.bpb.root_entries * 32 + fs.bpb.bytes_per_sector - 1) / fs.bpb.bytes_per_sector);

    uint32_t fat_size = fs.bpb.sectors_per_fat * fs.bpb.bytes_per_sector;
    fs.fat = (uint16_t*)kmalloc(fat_size);
    if (!fs.fat) {
        panic("FAT12: Falha ao alocar FAT!");
        return;
    }

    for (int i = 0; i < fs.bpb.sectors_per_fat; i++) {
        uint8_t sector[512];
        if (ata_read_sectors(fs.fat_start + i, 1, sector) != 0) {
            panic("FAT12: Falha ao ler FAT!");
            return;
        }
        memcpy((uint8_t*)fs.fat + i * fs.bpb.bytes_per_sector, sector, fs.bpb.bytes_per_sector);
    }

    uint32_t root_size = fs.bpb.root_entries * 32;
    fs.root_dir = (fat12_dir_entry_t*)kmalloc(root_size);
    if (!fs.root_dir) {
        panic("FAT12: Falha ao alocar root dir!");
        return;
    }

    uint32_t root_sectors = (root_size + fs.bpb.bytes_per_sector - 1) / fs.bpb.bytes_per_sector;
    for (uint32_t i = 0; i < root_sectors; i++) {
        uint8_t sector[512];
        if (ata_read_sectors(fs.root_start + i, 1, sector) != 0) {
            panic("FAT12: Falha ao ler root dir!");
            return;
        }
        memcpy((uint8_t*)fs.root_dir + i * fs.bpb.bytes_per_sector, sector, fs.bpb.bytes_per_sector);
    }

    fs.initialized = 1;
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

    for (uint32_t i = 0; i < fs.bpb.root_entries; i++) {
        if (fs.root_dir[i].name[0] == 0x00) break;
        if (fs.root_dir[i].name[0] == 0xE5) continue;
        if (fs.root_dir[i].attributes & 0x08) continue;

        if (strncmp(fs.root_dir[i].name, filename, 11) == 0) {
            return &fs.root_dir[i];
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

    while (cluster < 0xFF8 && cluster != 0 && bytes_read < max_size) {
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

            memcpy(buffer + bytes_read, sector, to_copy);
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
                memset(entry, 0, sizeof(fat12_dir_entry_t));
                memcpy(entry->name, filename, 11);
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
            memset(sector, 0, 512);

            uint32_t to_copy = fs.bpb.bytes_per_sector;
            if (bytes_written + to_copy > size) {
                to_copy = size - bytes_written;
            }

            memcpy(sector, data + bytes_written, to_copy);
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

    for (uint32_t i = 0; i < fs.bpb.root_entries; i++) {
        if (fs.root_dir[i].name[0] == 0x00) break;
        if (fs.root_dir[i].name[0] == 0xE5) continue;
        if (fs.root_dir[i].attributes & 0x08) continue;

        if (strncmp(fs.root_dir[i].name, filename, 11) == 0) {
            uint16_t cluster = fs.root_dir[i].cluster_low;

            while (cluster >= 2 && cluster < 0xFF8) {
                uint16_t next = fat12_get_cluster(cluster);
                fat12_set_cluster(cluster, FAT12_CLUSTER_FREE);
                cluster = next;
            }

            fs.root_dir[i].name[0] = 0xE5;
            fs.root_dir[i].file_size = 0;
            fs.root_dir[i].cluster_low = 0;

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
    }
    return -1;
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
