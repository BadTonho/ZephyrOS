#include "drivers/speaker.h"
#include "core/timer.h"
#include "core/log.h"

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void speaker_init(void) {
    LOG_INFO("SPEAKER", "Inicializando speaker");
    speaker_off();
    LOG_INFO("SPEAKER", "Speaker inicializado com sucesso");
}

void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    if (frequency == 0) {
        speaker_off();
        return;
    }

    uint32_t divisor = 1193180 / frequency;

    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }

    uint32_t start = timer_get_ticks();
    uint32_t end = start + (duration_ms * 50) / 1000;
    while (timer_get_ticks() < end) {
        asm volatile("hlt");
    }

    speaker_off();
}

void speaker_off(void) {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

void speaker_play_melody(const uint32_t* frequencies, const uint32_t* durations, int notes) {
    for (int i = 0; i < notes; i++) {
        if (frequencies[i] == 0) {
            uint32_t start = timer_get_ticks();
            uint32_t end = start + (durations[i] * 50) / 1000;
            while (timer_get_ticks() < end) {
                asm volatile("hlt");
            }
        } else {
            speaker_beep(frequencies[i], durations[i]);
        }
    }
}
