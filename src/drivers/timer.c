#include "core/timer.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "process/process.h"

static uint32_t ticks = 0;

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void timer_init(uint32_t freq) {
    LOG_INFO("TIMER", "Inicializando timer");
    idt_register_handler(32, timer_handler);

    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    LOG_INFO("TIMER", "Timer inicializado com sucesso");
}

void timer_handler(registers_t* regs) {
    (void)regs;
    ticks++;
    scheduler_tick();
    
    // Troca de contexto a cada 1 tick (20ms considerando 50Hz)
    process_yield();
}

uint32_t timer_get_ticks(void) {
    return ticks;
}
