#ifndef COMPRESS_H
#define COMPRESS_H

#include "types.h"

#define COMPRESS_LZSS_N        4096
#define COMPRESS_LZSS_F        18
#define COMPRESS_LZSS_THRESHOLD 2

typedef struct {
    uint32_t original_size;
    uint32_t compressed_size;
    uint8_t  enabled;
    uint32_t total_compressed;
    uint32_t total_saved;
    uint32_t compression_count;
} compress_stats_t;

void compress_init(void);
int  compress_data(const uint8_t* src, uint32_t src_size, uint8_t* dst, uint32_t* dst_size);
int  decompress_data(const uint8_t* src, uint32_t src_size, uint8_t* dst, uint32_t* dst_size);
void compress_enable(void);
void compress_disable(void);
uint8_t compress_is_enabled(void);
compress_stats_t* compress_get_stats(void);
uint32_t compress_get_max_size(uint32_t original_size);
void compress_print_stats(void);

#endif
