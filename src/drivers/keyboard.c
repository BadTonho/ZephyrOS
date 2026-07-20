#include "core/keyboard.h"
#include "drivers/idt.h"
#include "core/log.h"

static keyboard_callback_t callback = 0;

#define KEYBOARD_QUEUE_SIZE 64

static volatile uint8_t event_queue[KEYBOARD_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static volatile uint32_t dropped_events;

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static const char scancode_table[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';', '\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

char keyboard_scancode_to_ascii(uint8_t scancode) {
    if (scancode < 128) {
        return scancode_table[scancode];
    }
    return 0;
}

void keyboard_init(void) {
    LOG_INFO("KBD", "Inicializando teclado");
    queue_head = 0;
    queue_tail = 0;
    dropped_events = 0;
    idt_register_handler(33, keyboard_handler);
    LOG_INFO("KBD", "Teclado inicializado com sucesso");
}

void keyboard_handler(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        return;
    }

    uint8_t next_head = (uint8_t)((queue_head + 1) % KEYBOARD_QUEUE_SIZE);
    if (next_head == queue_tail) {
        dropped_events++;
        return;
    }

    event_queue[queue_head] = scancode;
    queue_head = next_head;
}

void keyboard_process_events(void) {
    if (dropped_events > 0) {
        dropped_events = 0;
        LOG_WARN("KBD", "Fila de teclado cheia; eventos descartados");
    }

    while (queue_tail != queue_head) {
        uint8_t scancode = event_queue[queue_tail];
        queue_tail = (uint8_t)((queue_tail + 1) % KEYBOARD_QUEUE_SIZE);

        if (callback) {
            callback(scancode);
        }
    }
}

keyboard_callback_t keyboard_set_callback(keyboard_callback_t cb) {
    keyboard_callback_t prev = callback;
    callback = cb;
    return prev;
}
