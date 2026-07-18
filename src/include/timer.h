#ifndef TIMER_H
#define TIMER_H

#include "types.h"

void timer_init(uint32_t freq);
void timer_handler(registers_t* regs);
uint32_t timer_get_ticks(void);

#endif
