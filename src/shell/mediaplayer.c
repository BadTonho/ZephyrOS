#include "apps/mediaplayer.h"
#include "fs/wav.h"
#include "fs/bmp.h"
#include "drivers/ac97.h"
#include "fs/fs.h"
#include "drivers/vesa.h"
#include "core/video.h"
#include "process/process.h"
#include "core/memory.h"
#include "core/timer.h"
#include "drivers/font.h"
#include "core/string.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"

static mp_status_t status;
static wav_file_t current_wav;
static bmp_image_t current_bmp;
static uint8_t* wav_data = 0;
static uint32_t wav_size = 0;
static uint8_t* bmp_data = 0;
static uint32_t bmp_size = 0;
static uint32_t last_update_tick = 0;

static void str_copy(char* dst, const char* src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void mp_cleanup(void) {
    if (wav_data) {
        kfree(wav_data);
        wav_data = 0;
        wav_size = 0;
    }
    if (bmp_data) {
        kfree(bmp_data);
        bmp_data = 0;
        bmp_size = 0;
    }
    wav_free(&current_wav);
    bmp_free(&current_bmp);
}

static uint8_t* load_file(const char* filename, uint32_t* out_size) {
    uint8_t* buffer = (uint8_t*)kmalloc(65536);
    if (!buffer) return 0;

    int bytes = fs_read_file(filename, buffer, 65535);
    if (bytes <= 0) {
        kfree(buffer);
        buffer = 0;
        return 0;
    }

    *out_size = bytes;
    return buffer;
}

void mp_init(void) {
    kmemset(&status, 0, sizeof(mp_status_t));
    status.state = MP_STATE_IDLE;
    last_update_tick = 0;
}

int mp_play_audio(const char* filename) {
    if (!filename || !*filename) {
        LOG_ERROR("MP", "Arquivo de audio invalido");
        return ERR_NULL;
    }
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
        !recovery_is_available(RECOVERY_COMPONENT_AC97)) {
        LOG_WARN("MP", "Audio indisponivel por falta de filesystem ou AC97");
        return ERR_UNAVAILABLE;
    }

    mp_stop();
    mp_cleanup();

    wav_data = load_file(filename, &wav_size);
    if (!wav_data) {
        recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_DISK,
                               "Falha ao carregar audio");
        return ERR_DISK;
    }

    if (wav_load(wav_data, wav_size, &current_wav) != 0) {
        mp_cleanup();
        recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_INVALID,
                               "Formato WAV invalido");
        return ERR_DISK;
    }

    str_copy(status.filename, filename, 64);
    status.duration_ms = wav_get_duration_ms(&current_wav);
    status.position_ms = 0;
    status.has_audio = 1;
    status.has_image = 0;
    status.state = MP_STATE_PLAYING;

    wav_play(&current_wav);
    last_update_tick = timer_get_ticks();

    return 0;
}

int mp_play_image(const char* filename) {
    if (!filename || !*filename) {
        LOG_ERROR("MP", "Arquivo de imagem invalido");
        return ERR_NULL;
    }
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("MP", "Imagem indisponivel por falta de filesystem ou VESA");
        return ERR_UNAVAILABLE;
    }

    mp_stop();
    mp_cleanup();

    bmp_data = load_file(filename, &bmp_size);
    if (!bmp_data) {
        recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_DISK,
                               "Falha ao carregar imagem");
        return ERR_DISK;
    }

    if (bmp_load(bmp_data, bmp_size, &current_bmp) != 0) {
        mp_cleanup();
        recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_INVALID,
                               "Formato BMP invalido");
        return ERR_DISK;
    }

    str_copy(status.filename, filename, 64);
    status.has_image = 1;
    status.has_audio = 0;
    status.state = MP_STATE_PLAYING;

    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized) {
        int x = (mode->width - current_bmp.width) / 2;
        int y = (mode->height - current_bmp.height) / 2;
        bmp_draw(&current_bmp, x, y);

        char info[64];
        uint32_t w = current_bmp.width;
        int i = 0;
        char tmp[16];
        int t = 0;
        if (w == 0) { info[i++] = '0'; }
        else {
            while (w > 0) { tmp[t++] = '0' + (w % 10); w /= 10; }
            while (t > 0) { info[i++] = tmp[--t]; }
        }
        info[i++] = 'x';
        w = current_bmp.height;
        t = 0;
        if (w == 0) { info[i++] = '0'; }
        else {
            while (w > 0) { tmp[t++] = '0' + (w % 10); w /= 10; }
            while (t > 0) { info[i++] = tmp[--t]; }
        }
        info[i] = '\0';

        vesa_draw_string(10, 10, info, (vesa_color_t)vesa_rgb(255, 255, 255), 2);
        vesa_draw_string(10, 40, "Pressione ESC para voltar", (vesa_color_t)vesa_rgb(200, 200, 200), 1);
    }

    return 0;
}

int mp_play_media(const char* audio_file, const char* image_file) {
    if (!audio_file && !image_file) {
        LOG_ERROR("MP", "Nenhum arquivo de midia informado");
        return ERR_NULL;
    }
    if (!recovery_is_enabled(RECOVERY_COMPONENT_MEDIAPLAYER)) {
        LOG_WARN("MP", "Media Player desabilitado pelo recovery");
        return ERR_UNAVAILABLE;
    }
    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        LOG_WARN("MP", "Media Player sem filesystem");
        return ERR_UNAVAILABLE;
    }
    if (audio_file && !recovery_is_available(RECOVERY_COMPONENT_AC97)) {
        LOG_WARN("MP", "Audio solicitado sem AC97 disponivel");
        return ERR_UNAVAILABLE;
    }
    if (image_file && !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("MP", "Imagem solicitada sem VESA disponivel");
        return ERR_UNAVAILABLE;
    }

    mp_stop();
    mp_cleanup();

    if (audio_file) {
        wav_data = load_file(audio_file, &wav_size);
        if (!wav_data) {
            LOG_ERROR("MP", "Falha ao carregar audio solicitado");
            recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_DISK,
                                   "Falha ao carregar audio solicitado");
        } else if (wav_load(wav_data, wav_size, &current_wav) == 0) {
            status.has_audio = 1;
            status.duration_ms = wav_get_duration_ms(&current_wav);
        } else {
            LOG_ERROR("MP", "Formato WAV invalido no modo combinado");
            recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_INVALID,
                                   "Formato WAV invalido no modo combinado");
        }
    }

    if (image_file) {
        bmp_data = load_file(image_file, &bmp_size);
        if (!bmp_data) {
            LOG_ERROR("MP", "Falha ao carregar imagem solicitada");
            recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_DISK,
                                   "Falha ao carregar imagem solicitada");
        } else if (bmp_load(bmp_data, bmp_size, &current_bmp) == 0) {
            status.has_image = 1;

            vesa_mode_t* mode = vesa_get_mode();
            if (mode && mode->initialized) {
                int x = (mode->width - current_bmp.width) / 2;
                int y = (mode->height - current_bmp.height) / 2;
                bmp_draw(&current_bmp, x, y);
            }
        } else {
            LOG_ERROR("MP", "Formato BMP invalido no modo combinado");
            recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_INVALID,
                                   "Formato BMP invalido no modo combinado");
        }
    }

    if (!status.has_audio && !status.has_image) {
        LOG_WARN("MP", "Nenhum arquivo de midia pode ser reproduzido");
        mp_cleanup();
        return ERR_NOT_FOUND;
    }

    char name[64] = "";
    if (audio_file) str_copy(name, audio_file, 64);
    else if (image_file) str_copy(name, image_file, 64);
    str_copy(status.filename, name, 64);

    status.position_ms = 0;
    status.state = MP_STATE_PLAYING;

    if (status.has_audio) {
        wav_play(&current_wav);
    }

    last_update_tick = timer_get_ticks();
    return 0;
}

void mp_stop(void) {
    ac97_stop();
    mp_cleanup();
    status.state = MP_STATE_STOPPED;
    status.position_ms = 0;
    status.has_audio = 0;
    status.has_image = 0;
}

void mp_pause(void) {
    if (status.state == MP_STATE_PLAYING) {
        ac97_stop();
        status.state = MP_STATE_PAUSED;
    }
}

void mp_resume(void) {
    if (status.state == MP_STATE_PAUSED && status.has_audio) {
        wav_play(&current_wav);
        status.state = MP_STATE_PLAYING;
        last_update_tick = timer_get_ticks();
    }
}

mp_status_t* mp_get_status(void) {
    return &status;
}

void mp_update(void) {
    if (status.state != MP_STATE_PLAYING) return;

    uint32_t current_tick = timer_get_ticks();
    uint32_t elapsed = (current_tick - last_update_tick) * 20;
    last_update_tick = current_tick;

    if (status.has_audio) {
        status.position_ms += elapsed;
        if (status.position_ms >= status.duration_ms) {
            mp_stop();
            status.state = MP_STATE_IDLE;
        }
    }
}



