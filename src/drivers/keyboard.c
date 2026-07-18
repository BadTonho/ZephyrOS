#include "keyboard.h"
#include "idt.h"
#include "video.h"

static keyboard_callback_t callback = 0;

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

char scancode_to_ascii(uint8_t scancode) {
    if (scancode < 128) {
        return scancode_table[scancode];
    }
    return 0;
}

void keyboard_init(void) {
    register_interrupt_handler(33, keyboard_handler);
}

void keyboard_handler(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        return;
    }

    if (callback) {
        callback(scancode);
    }
}

void keyboard_set_callback(keyboard_callback_t cb) {
    callback = cb;
}
