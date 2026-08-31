#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

#define SERIAL_COM1_BASE 0x03F8U
#define SERIAL_TX_CAPACITY 4096U
#define SERIAL_TX_FLUSH_BUDGET 128U

void serial_init(void);
uint8_t serial_is_ready(void);
int serial_read_byte(uint8_t* out_byte);
uint32_t serial_write_text(const char* text, uint32_t length);
uint32_t serial_flush(uint32_t budget);

#endif
