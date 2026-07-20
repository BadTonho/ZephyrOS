#include "memory/compress.h"
#include "core/memory.h"
#include "core/video.h"
#include "core/errors.h"
#include "core/string.h"

static compress_stats_t stats;
static uint8_t enabled = 0;
static uint8_t compress_ring[COMPRESS_LZSS_N];
static uint8_t decompress_ring[COMPRESS_LZSS_N];

void compress_init(void) {
    kmemset(&stats, 0, sizeof(compress_stats_t));
    stats.enabled = 0;
}

void compress_enable(void) {
    enabled = 1;
    stats.enabled = 1;
}

void compress_disable(void) {
    enabled = 0;
    stats.enabled = 0;
}

uint8_t compress_is_enabled(void) {
    return enabled;
}

compress_stats_t* compress_get_stats(void) {
    return &stats;
}

uint32_t compress_get_max_size(uint32_t original_size) {
    return original_size + (original_size / 8) + 64;
}

int compress_data(const uint8_t* src, uint32_t src_size, uint8_t* dst,
                  uint32_t dst_capacity, uint32_t* dst_size) {
    if (!src || !dst || !dst_size) return ERR_NULL;
    *dst_size = 0;
    if (dst_capacity == 0) return ERR_OVERFLOW;

    uint32_t si = 0;
    uint32_t di = 0;

    uint8_t flags = 0;
    uint32_t flag_pos = 0;
    if (di >= dst_capacity) return ERR_OVERFLOW;
    uint32_t flag_offset = di++;

    kmemset(compress_ring, 0x20, COMPRESS_LZSS_N);

    uint32_t r = COMPRESS_LZSS_N - COMPRESS_LZSS_F;

    while (si < src_size) {
        uint32_t best_len = 0;
        uint32_t best_pos = 0;

        if (si >= COMPRESS_LZSS_F) {
            uint32_t search_start = (si >= COMPRESS_LZSS_N) ? (si - COMPRESS_LZSS_N + 1) : 0;
            for (uint32_t j = search_start; j < si; j++) {
                uint32_t len = 0;
                while (len < COMPRESS_LZSS_F && si + len < src_size) {
                    if (compress_ring[(j + len) % COMPRESS_LZSS_N] != src[si + len]) break;
                    len++;
                }
                if (len > best_len && len >= COMPRESS_LZSS_THRESHOLD) {
                    best_len = len;
                    best_pos = j;
                }
            }
        }

        if (best_len >= COMPRESS_LZSS_THRESHOLD) {
            if (di + 2 > dst_capacity) return ERR_OVERFLOW;
            uint16_t pos = (si - best_pos) & (COMPRESS_LZSS_N - 1);
            dst[di++] = (uint8_t)(pos & 0xFF);
            dst[di++] = (uint8_t)(((pos >> 8) & 0x0F) | ((best_len - COMPRESS_LZSS_THRESHOLD) << 4));
            flags |= (1 << flag_pos);

            for (uint32_t k = 0; k < best_len; k++) {
                compress_ring[r] = src[si + k];
                r = (r + 1) & (COMPRESS_LZSS_N - 1);
                si++;
            }
        } else {
            if (di + 1 > dst_capacity) return ERR_OVERFLOW;
            dst[di++] = src[si];
            compress_ring[r] = src[si];
            r = (r + 1) & (COMPRESS_LZSS_N - 1);
            si++;
        }

        flag_pos++;
        if (flag_pos == 8) {
            dst[flag_offset] = flags;
            flags = 0;
            flag_pos = 0;
            if (si < src_size) {
                if (di >= dst_capacity) return ERR_OVERFLOW;
                flag_offset = di++;
            }
        }
    }

    if (flag_pos > 0) {
        dst[flag_offset] = flags;
    }

    *dst_size = di;

    stats.original_size = src_size;
    stats.compressed_size = di;
    stats.total_compressed += di;
    stats.total_saved += (src_size > di) ? (src_size - di) : 0;
    stats.compression_count++;

    return 0;
}

int decompress_data(const uint8_t* src, uint32_t src_size, uint8_t* dst,
                    uint32_t dst_capacity, uint32_t* dst_size) {
    if (!src || !dst || !dst_size) return ERR_NULL;
    *dst_size = 0;

    uint32_t si = 0;
    uint32_t di = 0;

    kmemset(decompress_ring, 0x20, COMPRESS_LZSS_N);

    uint32_t r = COMPRESS_LZSS_N - COMPRESS_LZSS_F;

    while (si < src_size) {
        uint8_t flags = src[si++];
        for (int bit = 0; bit < 8 && si < src_size; bit++) {
            if (flags & (1 << bit)) {
                if (si + 1 >= src_size) return ERR_INVALID;

                uint8_t b1 = src[si++];
                uint8_t b2 = src[si++];
                uint16_t pos = b1 | ((b2 & 0x0F) << 8);
                uint32_t len = ((b2 >> 4) & 0x0F) + COMPRESS_LZSS_THRESHOLD;

                for (uint32_t k = 0; k < len; k++) {
                    if (di >= dst_capacity) return ERR_OVERFLOW;
                    uint8_t c = decompress_ring[(pos + k) & (COMPRESS_LZSS_N - 1)];
                    dst[di++] = c;
                    decompress_ring[r] = c;
                    r = (r + 1) & (COMPRESS_LZSS_N - 1);
                }
            } else {
                if (si >= src_size) return ERR_INVALID;

                uint8_t c = src[si++];
                if (di >= dst_capacity) return ERR_OVERFLOW;
                dst[di++] = c;
                decompress_ring[r] = c;
                r = (r + 1) & (COMPRESS_LZSS_N - 1);
            }
        }
    }

    *dst_size = di;
    return 0;
}

void compress_print_stats(void) {
    video_print("Estatisticas de Compressao:\n", 0x0B);
    video_print("  Estado: ", 0x07);
    if (enabled) {
        video_print("ATIVADO\n", 0x0A);
    } else {
        video_print("DESATIVADO\n", 0x0C);
    }

    video_print("  Compressoes: ", 0x07);
    char buf[16];
    uint32_t num = stats.compression_count;
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
    video_print("\n", 0x07);

    video_print("  Total comprimido: ", 0x07);
    num = stats.total_compressed;
    i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
    video_print(" bytes\n", 0x08);

    video_print("  Espaco economizado: ", 0x07);
    num = stats.total_saved;
    i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
    video_print(" bytes\n", 0x08);
}
