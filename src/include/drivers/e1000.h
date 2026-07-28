#ifndef E1000_H
#define E1000_H

#include "types.h"
#include "drivers/idt.h"

#define E1000_MAC_ADDRESS_SIZE 6U
#define E1000_MAX_FRAME_SIZE 1518U
#define E1000_RX_QUEUE_CAPACITY 8U

typedef struct {
    uint8_t detected;
    uint8_t initialized;
    uint8_t link_up;
    uint8_t irq;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t mac_address[E1000_MAC_ADDRESS_SIZE];
    uint8_t rx_queue_depth;
    uint8_t rx_queue_high_water;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint32_t rx_queue_dropped;
    uint32_t rx_interrupts;
    int last_error;
} e1000_status_t;

int e1000_init(void);
int e1000_get_status(e1000_status_t* out_status);
int e1000_send_frame(const uint8_t* data, uint16_t length);
int e1000_receive_frame(uint8_t* data, uint16_t capacity,
                        uint16_t* out_length, uint8_t* out_received);
void e1000_handler(registers_t* regs);

#endif
