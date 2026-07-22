#include "core/timer.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/errors.h"
#include "process/process.h"

static uint32_t ticks = 0;

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void timer_init(uint32_t freq) {
    LOG_INFO("TIMER", "Inicializando timer");
    if (idt_register_handler(32, timer_handler) != OK) {
        LOG_ERROR("TIMER", "Falha ao registrar IRQ do timer");
        return;
    }

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
    
    // Como process_yield não retorna no caso de uma preempção,
    // precisamos confirmar a interrupção no PIC antes de trocar de contexto.
    outb(0x20, 0x20);

    // O scheduler so pode iniciar depois que o processo Idle estiver registrado.
    if (process_get_current()) process_yield();
}

uint32_t timer_get_ticks(void) {
    return ticks;
}
