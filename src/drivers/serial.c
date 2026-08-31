#include "drivers/serial.h"
#include "core/errors.h"
#include "core/log.h"

#define SERIAL_DATA_OFFSET 0U
#define SERIAL_INTERRUPT_OFFSET 1U
#define SERIAL_LINE_CONTROL_OFFSET 3U
#define SERIAL_MODEM_CONTROL_OFFSET 4U
#define SERIAL_LINE_STATUS_OFFSET 5U
#define SERIAL_FIFO_CONTROL_OFFSET 2U
#define SERIAL_DIVISOR_LOW 1U
#define SERIAL_DIVISOR_HIGH 0U
#define SERIAL_LINE_CONTROL_8N1 0x03U
#define SERIAL_LINE_CONTROL_DLAB 0x80U
#define SERIAL_FIFO_ENABLE 0xC7U
#define SERIAL_MODEM_ENABLE 0x0BU
#define SERIAL_LINE_STATUS_DATA_READY 0x01U
#define SERIAL_LINE_STATUS_TRANSMITTER_READY 0x20U
#define SERIAL_EFLAGS_INTERRUPT_ENABLE 0x200U
#define SERIAL_ESCAPE 0x1BU
#define SERIAL_ESCAPE_START 1U
#define SERIAL_ESCAPE_CSI 2U

static volatile uint8_t serial_tx_queue[SERIAL_TX_CAPACITY];
static uint16_t serial_tx_head;
static uint16_t serial_tx_tail;
static uint8_t serial_ready;
static uint8_t serial_escape_state;

static uint32_t serial_flush_locked(uint32_t budget);

static uint32_t serial_suspend_interrupts(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void serial_restore_interrupts(uint32_t flags) {
    if (flags & SERIAL_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static uint8_t serial_inb(uint16_t port) {
    uint8_t value;

    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t serial_next_index(uint16_t index) {
    return (uint16_t)((index + 1U) % SERIAL_TX_CAPACITY);
}

static int serial_byte_allowed(uint8_t value) {
    return value == '\n' || (value >= 0x20U && value <= 0x7EU);
}

static int serial_prepare_byte(uint8_t value) {
    if (serial_escape_state == SERIAL_ESCAPE_START) {
        serial_escape_state = value == '[' ? SERIAL_ESCAPE_CSI : 0U;
        return 0;
    }
    if (serial_escape_state == SERIAL_ESCAPE_CSI) {
        if (value >= 0x40U && value <= 0x7EU) serial_escape_state = 0U;
        return 0;
    }
    if (value == SERIAL_ESCAPE) {
        serial_escape_state = SERIAL_ESCAPE_START;
        return 0;
    }
    return serial_byte_allowed(value);
}

static uint32_t serial_flush_locked(uint32_t budget) {
    uint32_t sent = 0U;

    while (serial_tx_tail != serial_tx_head && sent < budget) {
        if (!(serial_inb(SERIAL_COM1_BASE + SERIAL_LINE_STATUS_OFFSET) &
              SERIAL_LINE_STATUS_TRANSMITTER_READY)) {
            break;
        }
        serial_outb(SERIAL_COM1_BASE + SERIAL_DATA_OFFSET,
                    serial_tx_queue[serial_tx_tail]);
        serial_tx_tail = serial_next_index(serial_tx_tail);
        sent++;
    }
    return sent;
}

void serial_init(void) {
    uint32_t flags;

    serial_outb(SERIAL_COM1_BASE + SERIAL_INTERRUPT_OFFSET, 0U);
    serial_outb(SERIAL_COM1_BASE + SERIAL_LINE_CONTROL_OFFSET,
                SERIAL_LINE_CONTROL_DLAB);
    serial_outb(SERIAL_COM1_BASE + SERIAL_DATA_OFFSET, SERIAL_DIVISOR_LOW);
    serial_outb(SERIAL_COM1_BASE + SERIAL_INTERRUPT_OFFSET,
                SERIAL_DIVISOR_HIGH);
    serial_outb(SERIAL_COM1_BASE + SERIAL_LINE_CONTROL_OFFSET,
                SERIAL_LINE_CONTROL_8N1);
    serial_outb(SERIAL_COM1_BASE + SERIAL_FIFO_CONTROL_OFFSET,
                SERIAL_FIFO_ENABLE);
    serial_outb(SERIAL_COM1_BASE + SERIAL_MODEM_CONTROL_OFFSET,
                SERIAL_MODEM_ENABLE);
    flags = serial_suspend_interrupts();
    serial_tx_head = 0U;
    serial_tx_tail = 0U;
    serial_escape_state = 0U;
    serial_ready = 1U;
    serial_restore_interrupts(flags);
    LOG_INFO("SERIAL", "Canal COM1 inicializado");
}

uint8_t serial_is_ready(void) {
    return serial_ready;
}

int serial_read_byte(uint8_t* out_byte) {
    if (!out_byte || !serial_ready) return 0;
    if (serial_inb(SERIAL_COM1_BASE + SERIAL_LINE_STATUS_OFFSET) &
        SERIAL_LINE_STATUS_DATA_READY) {
        *out_byte = serial_inb(SERIAL_COM1_BASE + SERIAL_DATA_OFFSET);
        return 1;
    }
    return 0;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    uint32_t written = 0U;
    uint32_t flags;

    if (!text || !serial_ready) return 0U;
    flags = serial_suspend_interrupts();
    while (written < length) {
        uint16_t next = serial_next_index(serial_tx_head);

        if (!serial_prepare_byte((uint8_t)text[written])) {
            written++;
            continue;
        }
        if (next == serial_tx_tail) break;
        serial_tx_queue[serial_tx_head] = (uint8_t)text[written++];
        serial_tx_head = next;
    }
    serial_flush_locked(SERIAL_TX_FLUSH_BUDGET);
    serial_restore_interrupts(flags);
    return written;
}

uint32_t serial_flush(uint32_t budget) {
    uint32_t flags;
    uint32_t sent;

    if (!serial_ready) return 0U;
    flags = serial_suspend_interrupts();
    sent = serial_flush_locked(budget);
    serial_restore_interrupts(flags);
    return sent;
}
