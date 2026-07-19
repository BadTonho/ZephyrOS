#ifndef SPEAKER_H
#define SPEAKER_H

#include "types.h"

void speaker_init(void);
void speaker_beep(uint32_t frequency, uint32_t duration_ms);
void speaker_play_melody(const uint32_t* frequencies, const uint32_t* durations, int notes);
void speaker_off(void);

#endif
