#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/memory.h"
#include "core/string.h"
#include "fs/wav.h"

#define HOST_COVERAGE_CAPACITY 2048U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_AUDIO_DATA_SIZE 32U
#define HOST_ALLOC_SIZE 256U

typedef union {
    uint64_t alignment;
    uint8_t bytes[HOST_ALLOC_SIZE];
} host_alloc_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_alloc_t allocation;
static uint8_t allocation_used;
static uint32_t play_calls;
static const uint8_t* played_data;
static uint32_t played_size;
static uint32_t played_rate;
static uint8_t played_channels;
static uint8_t played_bits;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:storage:wav|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:storage:wav|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:storage:wav|value=0x%08X\n",
           (uint32_t)result);
}

static int expect_true(int condition, const char* expression) {
    if (condition) return OK;
    fprintf(stderr, "wav-host: falhou: %s\n", expression);
    return ERR_STATE;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression) != OK) failures++; \
    } while (0)

void* kmalloc(uint32_t size) {
    if (!size || size > HOST_ALLOC_SIZE || allocation_used) return 0;
    allocation_used = 1U;
    return allocation.bytes;
}

void kfree(void* pointer) {
    if (pointer == allocation.bytes) allocation_used = 0U;
}

void ac97_play(const uint8_t* data, uint32_t size, uint32_t sample_rate,
               uint8_t channels, uint8_t bits) {
    play_calls++;
    played_data = data;
    played_size = size;
    played_rate = sample_rate;
    played_channels = channels;
    played_bits = bits;
}

static void write_u16(uint8_t* buffer, uint32_t offset, uint16_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1U] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t* buffer, uint32_t offset, uint32_t value) {
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1U] = (uint8_t)(value >> 8U);
    buffer[offset + 2U] = (uint8_t)(value >> 16U);
    buffer[offset + 3U] = (uint8_t)(value >> 24U);
}

static void write_text(uint8_t* buffer, uint32_t offset, const char* text) {
    for (uint32_t index = 0U; index < 4U; index++) {
        buffer[offset + index] = (uint8_t)text[index];
    }
}

static uint32_t make_wav(uint8_t* buffer, uint8_t include_data,
                         uint32_t data_size, uint32_t sample_rate) {
    kmemset(buffer, 0, 256U);
    write_text(buffer, 0U, "RIFF");
    write_text(buffer, 8U, "WAVE");
    write_text(buffer, 12U, "fmt ");
    write_u32(buffer, 16U, 16U);
    write_u16(buffer, 20U, 1U);
    write_u16(buffer, 22U, 2U);
    write_u32(buffer, 24U, sample_rate);
    write_u32(buffer, 28U, 32000U);
    write_u16(buffer, 32U, 4U);
    write_u16(buffer, 34U, 16U);
    if (!include_data) return 36U;
    write_text(buffer, 36U, "data");
    write_u32(buffer, 40U, data_size);
    for (uint32_t index = 0U; index < data_size && index < HOST_ALLOC_SIZE;
         index++) {
        buffer[44U + index] = (uint8_t)(index + 1U);
    }
    return 44U + data_size;
}

int main(void) {
    uint8_t wav_data[256];
    wav_file_t wav;
    uint32_t wav_size;
    int failures = 0;

    coverage_active = 1U;
    wav_init();
    wav_size = make_wav(wav_data, 1U, HOST_AUDIO_DATA_SIZE, 8000U);
    EXPECT(wav_load(wav_data, wav_size, &wav) == OK);
    EXPECT(wav.initialized != 0U);
    EXPECT(wav.audio_format == 1U);
    EXPECT(wav.num_channels == 2U);
    EXPECT(wav.sample_rate == 8000U);
    EXPECT(wav.byte_rate == 32000U);
    EXPECT(wav.data_size == HOST_AUDIO_DATA_SIZE);
    EXPECT(wav.data != 0);
    EXPECT(allocation_used != 0U);
    EXPECT(wav.data[0] == 1U && wav.data[HOST_AUDIO_DATA_SIZE - 1U] ==
           HOST_AUDIO_DATA_SIZE);
    EXPECT(wav_get_duration_ms(&wav) == 1U);
    wav_play(&wav);
    EXPECT(play_calls == 1U);
    EXPECT(played_data == wav.data);
    EXPECT(played_size == HOST_AUDIO_DATA_SIZE);
    EXPECT(played_rate == 8000U);
    EXPECT(played_channels == 2U);
    EXPECT(played_bits == 16U);
    wav_free(&wav);
    EXPECT(wav.data == 0 && wav.initialized == 0U && allocation_used == 0U);
    wav_free(&wav);

    EXPECT(wav_load(0, wav_size, &wav) != OK);
    EXPECT(wav_load(wav_data, 0U, &wav) != OK);
    EXPECT(wav_load(wav_data, wav_size, 0) != OK);
    wav_data[0] = 'X';
    EXPECT(wav_load(wav_data, wav_size, &wav) != OK);
    wav_size = make_wav(wav_data, 1U, HOST_AUDIO_DATA_SIZE, 8000U);
    wav_data[8] = 'X';
    EXPECT(wav_load(wav_data, wav_size, &wav) != OK);
    wav_size = make_wav(wav_data, 0U, 0U, 8000U);
    EXPECT(wav_load(wav_data, wav_size, &wav) != OK);
    wav_size = make_wav(wav_data, 1U, HOST_AUDIO_DATA_SIZE, 0U);
    EXPECT(wav_load(wav_data, wav_size, &wav) != OK);
    EXPECT(wav.data == 0 && allocation_used == 0U);

    wav_size = make_wav(wav_data, 1U, HOST_AUDIO_DATA_SIZE, 8000U);
    write_u32(wav_data, 16U, 1000U);
    EXPECT(wav_load(wav_data, wav_size, &wav) != OK);
    EXPECT(wav.data == 0 && allocation_used == 0U);

    wav_size = make_wav(wav_data, 1U, HOST_AUDIO_DATA_SIZE, 8000U);
    write_u32(wav_data, 40U, HOST_AUDIO_DATA_SIZE + 100U);
    EXPECT(wav_load(wav_data, wav_size, &wav) == OK);
    EXPECT(wav.data_size == wav_size - 44U);
    EXPECT(wav_get_duration_ms(&wav) == 1U);
    wav_play(&wav);
    EXPECT(play_calls == 2U);
    wav_free(&wav);

    kmemset(&wav, 0, sizeof(wav));
    EXPECT(wav_get_duration_ms(0) == 0U);
    EXPECT(wav_get_duration_ms(&wav) == 0U);
    wav_play(0);
    EXPECT(play_calls == 2U);
    wav_free(0);

    coverage_active = 0U;
    coverage_emit(failures ? ERR_STATE : OK);
    return failures ? 1 : 0;
}
