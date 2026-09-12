#include "drivers/speaker.h"
#include "core/timer.h"
#include "core/log.h"

#if defined(ZEPHYROS_HOST_TEST)
extern uint8_t speaker_host_inb(uint16_t port);
extern void speaker_host_outb(uint16_t port, uint8_t val);
#endif

#define SPEAKER_PIT_FREQUENCY 1193180U
#define SPEAKER_MIN_FREQUENCY 19U
#define SPEAKER_MAX_DURATION_MS 600000U
#define SPEAKER_TICK_RATE 50U
#define SPEAKER_MILLISECONDS_PER_SECOND 1000U

static uint32_t speaker_duration_ticks(uint32_t duration_ms) {
    if (duration_ms > SPEAKER_MAX_DURATION_MS) return 0U;
    return (duration_ms * SPEAKER_TICK_RATE) /
           SPEAKER_MILLISECONDS_PER_SECOND;
}

static void speaker_wait(uint32_t duration_ms) {
    uint32_t ticks = speaker_duration_ticks(duration_ms);
    uint32_t start;

    if (duration_ms > SPEAKER_MAX_DURATION_MS) {
        LOG_WARN("SPEAKER", "Duracao de speaker excede o limite");
        return;
    }
    if (!ticks) return;
    start = timer_get_ticks();
    while ((uint32_t)(timer_get_ticks() - start) < ticks) {
#if !defined(ZEPHYROS_HOST_TEST)
        asm volatile("hlt");
#endif
    }
}

static uint8_t inb(uint16_t port) {
#if defined(ZEPHYROS_HOST_TEST)
    return speaker_host_inb(port);
#else
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
#endif
}

static void outb(uint16_t port, uint8_t val) {
#if defined(ZEPHYROS_HOST_TEST)
    speaker_host_outb(port, val);
#else
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
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

    uint32_t divisor;

    if (frequency < SPEAKER_MIN_FREQUENCY ||
        frequency > SPEAKER_PIT_FREQUENCY ||
        duration_ms > SPEAKER_MAX_DURATION_MS) {
        LOG_WARN("SPEAKER", "Parametros de beep invalidos");
        speaker_off();
        return;
    }
    divisor = SPEAKER_PIT_FREQUENCY / frequency;

    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }

    speaker_wait(duration_ms);
    speaker_off();
}

void speaker_off(void) {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

void speaker_play_melody(const uint32_t* frequencies, const uint32_t* durations, int notes) {
    if (!frequencies || !durations || notes <= 0) {
        if (notes || frequencies || durations) {
            LOG_WARN("SPEAKER", "Melodia invalida");
        }
        return;
    }
    for (int i = 0; i < notes; i++) {
        if (frequencies[i] == 0) {
            speaker_wait(durations[i]);
        } else {
            speaker_beep(frequencies[i], durations[i]);
        }
    }
}
