#include "core/errors.h"
#include "core/string.h"
#include "memory/compress.h"

#include <stdint.h>

#define TEST_RAW_CAPACITY 8192U
#define TEST_COMPRESSED_CAPACITY 10000U

static uint32_t video_print_calls;

void video_print(const char* str, uint8_t color) {
    (void)str;
    (void)color;
    video_print_calls++;
}

static int expect(int condition) {
    return condition ? 0 : 1;
}

static void fill_random(uint8_t* data, uint32_t size) {
    uint32_t state = 0x13579BDFU;
    for (uint32_t index = 0; index < size; index++) {
        state = state * 1664525U + 1013904223U;
        data[index] = (uint8_t)(state >> 24);
    }
}

static int round_trip(const uint8_t* source, uint32_t source_size) {
    static uint8_t compressed[TEST_COMPRESSED_CAPACITY];
    static uint8_t restored[TEST_RAW_CAPACITY];
    uint32_t compressed_size = 0;
    uint32_t restored_size = 0;

    if (compress_data(source, source_size, compressed,
                      sizeof(compressed), &compressed_size) != OK) {
        return 1;
    }
    if (compressed_size == 0 || compressed_size > sizeof(compressed)) return 1;
    if (decompress_data(compressed, compressed_size, restored,
                        sizeof(restored), &restored_size) != OK) {
        return 1;
    }
    if (restored_size != source_size) return 1;
    for (uint32_t index = 0; index < source_size; index++) {
        if (restored[index] != source[index]) return 1;
    }
    return 0;
}

static int test_strings(void) {
    static const char text[] = "ZephyrOS";
    static uint8_t destination[sizeof(text)];

    kmemset(destination, 0xA5, sizeof(destination));
    if (expect(destination[0] == 0xA5 && destination[7] == 0xA5)) return 1;
    kmemset(destination, 0, 0);
    kmemcpy(destination, text, sizeof(text));
    if (expect(kstrlen((const char*)destination) == 8)) return 1;
    if (expect(kstrcmp((const char*)destination, text) == 0)) return 1;
    if (expect(kstrcmp("abc", "abd") < 0)) return 1;
    if (expect(kstrcmp("abd", "abc") > 0)) return 1;
    if (expect(kstrlen("") == 0)) return 1;
    return 0;
}

static int test_compression_round_trips(void) {
    static uint8_t repeated[TEST_RAW_CAPACITY];
    static uint8_t patterned[TEST_RAW_CAPACITY];
    static uint8_t random_data[TEST_RAW_CAPACITY];

    kmemset(repeated, 'A', sizeof(repeated));
    for (uint32_t index = 0; index < sizeof(patterned); index++) {
        patterned[index] = (uint8_t)((index * 37U + index / 17U) & 0xFFU);
    }
    fill_random(random_data, sizeof(random_data));
    if (round_trip(repeated, sizeof(repeated))) return 10;
    if (round_trip(patterned, sizeof(patterned))) return 11;
    if (round_trip(random_data, sizeof(random_data))) return 12;
    if (round_trip(repeated, 0)) return 13;
    if (round_trip(repeated, 1)) return 14;
    if (round_trip(repeated, COMPRESS_LZSS_N)) return 15;
    return 0;
}

static int test_compression_errors(void) {
    static const uint8_t literal[] = {0x00, 'x'};
    static const uint8_t token_without_length[] = {0x01, 0x00};
    static const uint8_t truncated_literal[] = {0x01};
    static uint8_t source[64];
    static uint8_t destination[64];
    uint32_t output_size = 99;

    kmemset(source, 'B', sizeof(source));
    if (expect(compress_data(NULL, 0, destination, sizeof(destination),
                             &output_size) == ERR_NULL)) return 21;
    if (expect(compress_data(source, sizeof(source), NULL, sizeof(destination),
                             &output_size) == ERR_NULL)) return 22;
    if (expect(compress_data(source, sizeof(source), destination,
                             sizeof(destination), NULL) == ERR_NULL)) return 23;
    if (expect(compress_data(source, sizeof(source), destination, 0,
                             &output_size) == ERR_OVERFLOW)) return 24;
    if (expect(compress_data(source, sizeof(source), destination, 1,
                             &output_size) == ERR_OVERFLOW)) return 25;
    if (expect(output_size == 0)) return 26;
    if (expect(decompress_data(NULL, 0, destination, sizeof(destination),
                               &output_size) == ERR_NULL)) return 27;
    if (expect(decompress_data(truncated_literal, sizeof(truncated_literal),
                               destination, sizeof(destination), &output_size)
               == ERR_INVALID)) return 28;
    if (expect(decompress_data(token_without_length,
                               sizeof(token_without_length), destination,
                               sizeof(destination), &output_size) == ERR_INVALID)) {
        return 29;
    }
    if (expect(decompress_data(literal, sizeof(literal), destination, 0,
                               &output_size) == ERR_OVERFLOW)) return 30;
    return 0;
}

static int test_state_and_limits(void) {
    compress_stats_t* stats;

    compress_enable();
    compress_init();
    if (expect(compress_is_enabled() == 0)) return 1;
    stats = compress_get_stats();
    if (expect(stats->enabled == 0 && stats->compression_count == 0)) return 1;
    if (expect(compress_get_max_size(0) == 64)) return 1;
    if (expect(compress_get_max_size(UINT32_MAX) == UINT32_MAX)) return 1;
    compress_enable();
    if (expect(compress_is_enabled() == 1 && stats->enabled == 1)) return 1;
    compress_disable();
    if (expect(compress_is_enabled() == 0 && stats->enabled == 0)) return 1;
    return 0;
}

int main(void) {
    int compression_result;
    int error_result;
    compress_stats_t* stats;

    compress_init();
    if (test_strings()) return 1;
    if (test_state_and_limits()) return 2;
    compression_result = test_compression_round_trips();
    if (compression_result) return compression_result;
    stats = compress_get_stats();
    if (stats->compression_count != 6 || stats->original_size != COMPRESS_LZSS_N ||
        stats->compressed_size == 0 ||
        stats->total_compressed < stats->compressed_size) return 7;
    error_result = test_compression_errors();
    if (error_result) return error_result;
    if (video_print_calls != 0) return 5;
    compress_print_stats();
    if (video_print_calls == 0) return 6;
    return 0;
}
