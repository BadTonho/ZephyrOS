#include "fs/wav.h"
#include "core/memory.h"
#include "core/video.h"
#include "drivers/ac97.h"
#include "core/string.h"

static int memcmp(const void* a, const void* b, uint32_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    for (uint32_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

static uint32_t read_u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static uint16_t read_u16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

void wav_init(void) {
}

int wav_load(const uint8_t* raw_data, uint32_t size, wav_file_t* out) {
    if (!raw_data || !out || size < 44) return -1;

    kmemset(out, 0, sizeof(wav_file_t));

    if (memcmp(raw_data, "RIFF", 4) != 0) {
        return -1;
    }

    if (memcmp(raw_data + 8, "WAVE", 4) != 0) {
        return -1;
    }

    uint32_t offset = 12;
    while (offset + 8 <= size) {
        uint32_t chunk_id = read_u32(raw_data + offset);
        uint32_t chunk_size = read_u32(raw_data + offset + 4);

        if (chunk_id == 0x20746D66) {
            if (offset + 8 + chunk_size > size) return -1;

            out->audio_format = read_u16(raw_data + offset + 8);
            out->num_channels = read_u16(raw_data + offset + 10);
            out->sample_rate = read_u32(raw_data + offset + 12);
            out->byte_rate = read_u32(raw_data + offset + 16);
            out->block_align = read_u16(raw_data + offset + 20);
            out->bits_per_sample = read_u16(raw_data + offset + 22);
        }
        else if (chunk_id == 0x61746164) {
            if (offset + 8 + chunk_size > size) chunk_size = size - offset - 8;

            out->data_size = chunk_size;
            out->data = (uint8_t*)kmalloc(chunk_size);
            if (!out->data) return -1;

            kmemcpy(out->data, raw_data + offset + 8, chunk_size);
        }

        offset += 8 + chunk_size;
        if (chunk_size & 1) offset++;
    }

    if (!out->data) return -1;
    if (out->sample_rate == 0) return -1;

    out->initialized = 1;
    return 0;
}

void wav_play(wav_file_t* wav) {
    if (!wav || !wav->initialized || !wav->data) return;

    ac97_play(wav->data, wav->data_size, wav->sample_rate, wav->num_channels, wav->bits_per_sample);
}

void wav_free(wav_file_t* wav) {
    if (!wav) return;

    if (wav->data) {
        kfree(wav->data);
        wav->data = 0;
    }
    wav->initialized = 0;
}

uint32_t wav_get_duration_ms(wav_file_t* wav) {
    if (!wav || !wav->initialized || wav->byte_rate == 0) return 0;
    return (wav->data_size * 1000) / wav->byte_rate;
}
