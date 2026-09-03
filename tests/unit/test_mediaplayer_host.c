#include <stdint.h>
#include <stdio.h>

#include "apps/mediaplayer.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/recovery.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/ac97.h"
#include "drivers/vesa.h"
#include "fs/bmp.h"
#include "fs/fs.h"
#include "fs/wav.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_ALLOCATION_COUNT 8U
#define HOST_ALLOCATION_SIZE 65536U
#define HOST_FILENAME_LENGTH 64U

typedef union {
    uint64_t alignment;
    uint8_t bytes[HOST_ALLOCATION_SIZE];
} host_allocation_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static host_allocation_t allocations[HOST_ALLOCATION_COUNT];
static uint8_t allocation_used[HOST_ALLOCATION_COUNT];
static recovery_state_t recovery_states[RECOVERY_COMPONENT_COUNT];
static uint8_t recovery_available[RECOVERY_COMPONENT_COUNT];
static uint8_t recovery_enabled[RECOVERY_COMPONENT_COUNT];
static int recovery_last_error;
static uint32_t recovery_degrade_calls;
static uint32_t clock_ticks;
static uint32_t ac97_stop_calls;
static uint32_t wav_play_calls;
static uint32_t bmp_draw_calls;
static uint32_t vesa_string_calls;
static uint8_t vesa_mode_available;
static vesa_mode_t video_mode;
static uint8_t last_wav_marker;
static uint8_t last_bmp_marker;

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
    printf("ZCOV_BEGIN|case=host:shell:mediaplayer|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:mediaplayer|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:mediaplayer|value=0x%08X\n",
           (uint32_t)result);
}

static int expect_true(int condition, const char* expression) {
    if (condition) return OK;
    fprintf(stderr, "mediaplayer-host: falhou: %s\n", expression);
    return ERR_STATE;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression) != OK) (*failures)++; \
    } while (0)

static void reset_fixture(void) {
    for (uint32_t index = 0U; index < RECOVERY_COMPONENT_COUNT; index++) {
        recovery_states[index] = RECOVERY_STATE_READY;
        recovery_available[index] = 1U;
        recovery_enabled[index] = 1U;
    }
    kmemset(allocation_used, 0, sizeof(allocation_used));
    recovery_last_error = OK;
    recovery_degrade_calls = 0U;
    clock_ticks = 0U;
    ac97_stop_calls = 0U;
    wav_play_calls = 0U;
    bmp_draw_calls = 0U;
    vesa_string_calls = 0U;
    vesa_mode_available = 1U;
    kmemset(&video_mode, 0, sizeof(video_mode));
    video_mode.width = 640U;
    video_mode.height = 480U;
    video_mode.initialized = 1U;
    last_wav_marker = 0U;
    last_bmp_marker = 0U;
    mp_init();
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
}

void kmemset(void* dst, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)dst;

    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

int kstrcmp(const char* first, const char* second) {
    uint32_t index = 0U;

    if (!first || !second) return first == second ? 0 : 1;
    while (first[index] || second[index]) {
        if ((uint8_t)first[index] != (uint8_t)second[index]) {
            return (uint8_t)first[index] < (uint8_t)second[index] ? -1 : 1;
        }
        index++;
    }
    return 0;
}

uint32_t kstrlen(const char* string) {
    uint32_t length = 0U;

    if (!string) return 0U;
    while (string[length]) length++;
    return length;
}

void* kmalloc(uint32_t size) {
    if (!size || size > HOST_ALLOCATION_SIZE) return 0;
    for (uint32_t index = 0U; index < HOST_ALLOCATION_COUNT; index++) {
        if (!allocation_used[index]) {
            allocation_used[index] = 1U;
            return allocations[index].bytes;
        }
    }
    return 0;
}

void kfree(void* pointer) {
    if (!pointer) return;
    for (uint32_t index = 0U; index < HOST_ALLOCATION_COUNT; index++) {
        if (pointer == allocations[index].bytes) allocation_used[index] = 0U;
    }
}

static uint8_t file_marker(const char* filename) {
    if (!filename || !filename[0]) return 0U;
    if (filename[0] == 'a') return 1U;
    if (filename[0] == 'i') return 2U;
    if (filename[0] == 'b') return 3U;
    return 0U;
}

int fs_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    uint8_t marker = file_marker(filename);

    if (!marker || !buffer || max_size < 4U) return 0;
    buffer[0] = marker;
    buffer[1] = 0xAAU;
    buffer[2] = 0x55U;
    buffer[3] = 0x11U;
    return 4;
}

void wav_free(wav_file_t* wav) {
    if (!wav) return;
    wav->data = 0;
    wav->data_size = 0U;
    wav->initialized = 0U;
}

int wav_load(const uint8_t* raw_data, uint32_t size, wav_file_t* out) {
    if (!raw_data || size < 4U || !out || raw_data[0] != 1U) return ERR_INVALID;
    kmemset(out, 0, sizeof(*out));
    out->audio_format = 1U;
    out->num_channels = 2U;
    out->sample_rate = 8000U;
    out->byte_rate = 32000U;
    out->bits_per_sample = 16U;
    out->data_size = 4000U;
    out->data = (uint8_t*)raw_data;
    out->initialized = 1U;
    last_wav_marker = raw_data[0];
    return OK;
}

void wav_play(wav_file_t* wav) {
    if (wav && wav->initialized) wav_play_calls++;
}

uint32_t wav_get_duration_ms(wav_file_t* wav) {
    if (!wav || !wav->initialized) return 0U;
    return 100U;
}

void bmp_free(bmp_image_t* image) {
    if (!image) return;
    image->pixel_data = 0;
    image->color_table = 0;
    image->initialized = 0U;
}

int bmp_load(const uint8_t* raw_data, uint32_t size, bmp_image_t* out) {
    if (!raw_data || size < 4U || !out || raw_data[0] != 2U) return ERR_INVALID;
    kmemset(out, 0, sizeof(*out));
    out->width = 320U;
    out->height = 200U;
    out->bpp = 24U;
    out->pixel_data = (uint8_t*)raw_data;
    out->pixel_data_size = 4U;
    out->initialized = 1U;
    last_bmp_marker = raw_data[0];
    return OK;
}

void bmp_draw(bmp_image_t* image, int x, int y) {
    if (image && image->initialized && x >= 0 && y >= 0) bmp_draw_calls++;
}

vesa_mode_t* vesa_get_mode(void) {
    return vesa_mode_available ? &video_mode : 0;
}

uint32_t vesa_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)red << 16U) | ((uint32_t)green << 8U) | blue;
}

void vesa_draw_string(int x, int y, const char* string, vesa_color_t color,
                      uint32_t scale) {
    (void)x;
    (void)y;
    (void)color;
    (void)scale;
    if (string) vesa_string_calls++;
}

void ac97_stop(void) {
    ac97_stop_calls++;
}

uint32_t timer_get_ticks(void) {
    return clock_ticks;
}

int recovery_is_available(recovery_component_id_t component) {
    if (component >= RECOVERY_COMPONENT_COUNT) return 0;
    return recovery_available[component] != 0U;
}

int recovery_is_enabled(recovery_component_id_t component) {
    if (component >= RECOVERY_COMPONENT_COUNT) return 0;
    return recovery_enabled[component] != 0U;
}

int recovery_mark_degraded(recovery_component_id_t component, int error_code,
                           const char* message) {
    (void)message;
    if (component >= RECOVERY_COMPONENT_COUNT) return ERR_INVALID;
    recovery_states[component] = RECOVERY_STATE_DEGRADED;
    recovery_last_error = error_code;
    recovery_degrade_calls++;
    return OK;
}

static uint32_t allocation_count(void) {
    uint32_t count = 0U;

    for (uint32_t index = 0U; index < HOST_ALLOCATION_COUNT; index++) {
        if (allocation_used[index]) count++;
    }
    return count;
}

static void test_initial_and_invalid(int* failures) {
    mp_status_t* status;

    reset_fixture();
    status = mp_get_status();
    EXPECT(status->state == MP_STATE_IDLE);
    EXPECT(status->filename[0] == '\0');
    EXPECT(mp_play_audio(0) == ERR_NULL);
    EXPECT(mp_play_audio("") == ERR_NULL);
    EXPECT(mp_play_image(0) == ERR_NULL);
    EXPECT(mp_play_image("") == ERR_NULL);
    EXPECT(mp_play_media(0, 0) == ERR_NULL);
    mp_pause();
    mp_resume();
    mp_update();
    EXPECT(status->state == MP_STATE_IDLE);
}

static void test_audio(int* failures) {
    mp_status_t* status;

    reset_fixture();
    EXPECT(mp_play_audio("audio.wav") == OK);
    status = mp_get_status();
    EXPECT(status->state == MP_STATE_PLAYING);
    EXPECT(status->has_audio != 0U && status->has_image == 0U);
    EXPECT(status->duration_ms == 100U && status->position_ms == 0U);
    EXPECT(kstrcmp(status->filename, "audio.wav") == 0);
    EXPECT(last_wav_marker == 1U && wav_play_calls == 1U);
    mp_pause();
    EXPECT(status->state == MP_STATE_PAUSED);
    mp_pause();
    mp_resume();
    EXPECT(status->state == MP_STATE_PLAYING && wav_play_calls == 2U);
    clock_ticks = 2U;
    mp_update();
    EXPECT(status->position_ms == 40U);
    clock_ticks = 5U;
    mp_update();
    EXPECT(status->state == MP_STATE_IDLE && allocation_count() == 0U);
    EXPECT(ac97_stop_calls >= 2U);
}

static void test_audio_failures(int* failures) {
    reset_fixture();
    EXPECT(mp_play_audio("missing") == ERR_DISK);
    EXPECT(recovery_last_error == ERR_DISK);
    EXPECT(recovery_degrade_calls == 1U);
    EXPECT(allocation_count() == 0U);

    reset_fixture();
    EXPECT(mp_play_audio("bad.wav") == ERR_DISK);
    EXPECT(recovery_last_error == ERR_INVALID && recovery_degrade_calls == 1U);
    EXPECT(allocation_count() == 0U);

    reset_fixture();
    recovery_available[RECOVERY_COMPONENT_FILESYSTEM] = 0U;
    EXPECT(mp_play_audio("audio.wav") == ERR_UNAVAILABLE);
    recovery_available[RECOVERY_COMPONENT_FILESYSTEM] = 1U;
    recovery_available[RECOVERY_COMPONENT_AC97] = 0U;
    EXPECT(mp_play_audio("audio.wav") == ERR_UNAVAILABLE);
}

static void test_image(int* failures) {
    mp_status_t* status;

    reset_fixture();
    EXPECT(mp_play_image("image.bmp") == OK);
    status = mp_get_status();
    EXPECT(status->state == MP_STATE_PLAYING && status->has_image != 0U);
    EXPECT(status->has_audio == 0U && last_bmp_marker == 2U);
    EXPECT(bmp_draw_calls == 1U && vesa_string_calls == 2U);
    mp_stop();
    EXPECT(status->state == MP_STATE_STOPPED && allocation_count() == 0U);

    reset_fixture();
    vesa_mode_available = 0U;
    EXPECT(mp_play_image("image.bmp") == OK);
    EXPECT(bmp_draw_calls == 0U && vesa_string_calls == 0U);

    reset_fixture();
    EXPECT(mp_play_image("bad.bmp") == ERR_DISK);
    EXPECT(recovery_last_error == ERR_INVALID && allocation_count() == 0U);
    recovery_enabled[RECOVERY_COMPONENT_VESA] = 0U;
    EXPECT(mp_play_image("image.bmp") == ERR_UNAVAILABLE);
}

static void test_combined_and_cleanup(int* failures) {
    mp_status_t* status;

    reset_fixture();
    EXPECT(mp_play_media("audio.wav", "image.bmp") == OK);
    status = mp_get_status();
    EXPECT(status->has_audio != 0U && status->has_image != 0U);
    EXPECT(status->state == MP_STATE_PLAYING && wav_play_calls == 1U);
    EXPECT(kstrcmp(status->filename, "audio.wav") == 0);
    mp_stop();
    EXPECT(status->state == MP_STATE_STOPPED && allocation_count() == 0U);

    reset_fixture();
    EXPECT(mp_play_media("bad.wav", "image.bmp") == OK);
    EXPECT(status->has_audio == 0U && status->has_image != 0U);
    EXPECT(recovery_degrade_calls == 1U);
    EXPECT(allocation_count() == 2U);
    mp_stop();
    EXPECT(allocation_count() == 0U);

    reset_fixture();
    EXPECT(mp_play_media("bad.wav", "bad.bmp") == ERR_NOT_FOUND);
    EXPECT(recovery_degrade_calls == 2U && allocation_count() == 0U);

    reset_fixture();
    recovery_enabled[RECOVERY_COMPONENT_MEDIAPLAYER] = 0U;
    EXPECT(mp_play_media("audio.wav", 0) == ERR_UNAVAILABLE);
    recovery_enabled[RECOVERY_COMPONENT_MEDIAPLAYER] = 1U;
    recovery_available[RECOVERY_COMPONENT_FILESYSTEM] = 0U;
    EXPECT(mp_play_media("audio.wav", 0) == ERR_UNAVAILABLE);
}

static void test_filename_limit(int* failures) {
    char filename[96];
    mp_status_t* status;

    reset_fixture();
    for (uint32_t index = 0U; index < sizeof(filename) - 1U; index++) {
        filename[index] = 'a';
    }
    filename[sizeof(filename) - 1U] = '\0';
    EXPECT(mp_play_audio(filename) == OK);
    status = mp_get_status();
    EXPECT(status->filename[HOST_FILENAME_LENGTH - 1U] == '\0');
    EXPECT(kstrlen(status->filename) == HOST_FILENAME_LENGTH - 1U);
    mp_stop();
    EXPECT(allocation_count() == 0U);
}

int main(void) {
    int failures = 0;

    coverage_active = 1U;
    test_initial_and_invalid(&failures);
    test_audio(&failures);
    test_audio_failures(&failures);
    test_image(&failures);
    test_combined_and_cleanup(&failures);
    test_filename_limit(&failures);
    coverage_active = 0U;
    coverage_emit(failures ? ERR_STATE : OK);
    if (failures) {
        printf("MEDIAPLAYER_HOST_FAIL:%d\n", failures);
        return 1;
    }
    printf("MEDIAPLAYER_HOST_PASS\n");
    return 0;
}
