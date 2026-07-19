#ifndef MEDIAPLAYER_H
#define MEDIAPLAYER_H

#include "types.h"

#define MP_STATE_IDLE    0
#define MP_STATE_PLAYING 1
#define MP_STATE_PAUSED  2
#define MP_STATE_STOPPED 3

typedef struct {
    uint8_t  state;
    char     filename[64];
    uint32_t duration_ms;
    uint32_t position_ms;
    uint8_t  has_image;
    uint8_t  has_audio;
} mp_status_t;

void mp_init(void);
int  mp_play_audio(const char* filename);
int  mp_play_image(const char* filename);
int  mp_play_media(const char* audio_file, const char* image_file);
void mp_stop(void);
void mp_pause(void);
void mp_resume(void);
mp_status_t* mp_get_status(void);
void mp_update(void);

#endif
