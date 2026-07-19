#ifndef WAV_H
#define WAV_H

#include "types.h"

typedef struct {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
} __attribute__((packed)) wav_riff_t;

typedef struct {
    char     fmt[4];
    uint32_t chunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_fmt_t;

typedef struct {
    char     data[4];
    uint32_t data_size;
} __attribute__((packed)) wav_data_t;

typedef struct {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_size;
    uint8_t* data;
    uint8_t  initialized;
} wav_file_t;

void wav_init(void);
int  wav_load(const uint8_t* raw_data, uint32_t size, wav_file_t* out);
void wav_play(wav_file_t* wav);
void wav_free(wav_file_t* wav);
uint32_t wav_get_duration_ms(wav_file_t* wav);

#endif
