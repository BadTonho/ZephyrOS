#include "drivers/mouse.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/vesa.h"

// Fila circular para pacotes do mouse
#define MOUSE_QUEUE_SIZE 64

static mouse_packet_t event_queue[MOUSE_QUEUE_SIZE];
static volatile int queue_head = 0;
static volatile int queue_tail = 0;

static mouse_callback_t current_callback = 0;

// Renderizacao do cursor
static int cursor_drawn = 0;
static int cursor_x = 512;
static int cursor_y = 384;
static int prev_x = 512;
static int prev_y = 384;
static vesa_color_t bg_buffer[12 * 16];
static int cursor_visible = 1;

static uint8_t cycle = 0;
static uint8_t packet[3];

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void mouse_wait(uint8_t a_type) {
    uint32_t timeout = 100000;
    if (a_type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}

static void mouse_write(uint8_t a_write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, a_write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static void erase_cursor(void) {
    if (!cursor_drawn) return;
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || mode->bpp == 0) return;

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 12; j++) {
            if (prev_x + j < (int)mode->width && prev_y + i < (int)mode->height) {
                vesa_put_pixel(prev_x + j, prev_y + i, bg_buffer[i * 12 + j]);
            }
        }
    }
    cursor_drawn = 0;
}

static void draw_cursor(void) {
    if (!cursor_visible) return;
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || mode->bpp == 0) return;

    // Salva o fundo atual antes de desenhar por cima
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 12; j++) {
            if (cursor_x + j < (int)mode->width && cursor_y + i < (int)mode->height) {
                bg_buffer[i * 12 + j] = vesa_get_pixel(cursor_x + j, cursor_y + i);
            }
        }
    }

    vesa_color_t color;
    color.raw = vesa_rgb(255, 255, 255); // Branco
    vesa_color_t border;
    border.raw = vesa_rgb(0, 0, 0); // Preto

    // Desenha o cursor (borda e preenchimento)
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j <= i / 2 && j < 12; j++) {
            if (cursor_x + j < (int)mode->width && cursor_y + i < (int)mode->height) {
                if (j == 0 || j == i / 2 || i == 15) {
                    vesa_put_pixel(cursor_x + j, cursor_y + i, border);
                } else {
                    vesa_put_pixel(cursor_x + j, cursor_y + i, color);
                }
            }
        }
    }
    cursor_drawn = 1;
    prev_x = cursor_x;
    prev_y = cursor_y;
}

static void mouse_handler(registers_t* regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 1)) return; // Fila vazia
    if (!(status & 0x20)) return; // Dado nao pertence ao mouse

    packet[cycle++] = inb(0x60);
    if (cycle == 3) {
        cycle = 0;
        // Verifica pacote invalido baseado nos bits 6 e 7 ou bit 3 nao setado
        if ((packet[0] & 0xC0) || !(packet[0] & 0x08)) return; 

        int next_tail = (queue_tail + 1) % MOUSE_QUEUE_SIZE;
        if (next_tail != queue_head) { // Verifica overflow do ring buffer
            int32_t dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
            int32_t dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);

            event_queue[queue_tail].dx = dx;
            event_queue[queue_tail].dy = dy;
            event_queue[queue_tail].buttons = packet[0] & 0x07;
            queue_tail = next_tail;
        }
    }
}

void mouse_init(void) {
    LOG_INFO("MOUSE", "Inicializando driver PS/2...");

    // Habilita porta auxiliar (mouse)
    mouse_wait(1);
    outb(0x64, 0xA8);
    
    // Le configuracao da controladora e altera para suportar IRQ12
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60) | 2;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    
    // Restaura padroes no mouse
    mouse_write(0xF6);
    mouse_read(); // ACK
    
    // Habilita recebimento de dados
    mouse_write(0xF4);
    mouse_read(); // ACK

    // Registra IRQ12 (INT 44 na nossa IDT baseada no mapeamento)
    idt_register_handler(44, (isr_handler_t)mouse_handler);
    
    LOG_INFO("MOUSE", "Inicializado com sucesso");
}

void mouse_process_events(void) {
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode) return;
    
    int needs_redraw = 0;

    while (queue_head != queue_tail) {
        mouse_packet_t pkt = event_queue[queue_head];
        queue_head = (queue_head + 1) % MOUSE_QUEUE_SIZE;
        
        erase_cursor();
        
        cursor_x += pkt.dx;
        // Mouse PS/2 o Y cresce de baixo para cima, subtraimos
        cursor_y -= pkt.dy;
        
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= (int)mode->width) cursor_x = mode->width - 1;
        if (cursor_y >= (int)mode->height) cursor_y = mode->height - 1;
        
        if (current_callback) {
            current_callback(&pkt);
        }
        needs_redraw = 1;
    }
    
    if (needs_redraw) {
        draw_cursor();
    } else if (!cursor_drawn && cursor_visible) {
        draw_cursor();
    }
}

mouse_callback_t mouse_set_callback(mouse_callback_t cb) {
    mouse_callback_t old = current_callback;
    current_callback = cb;
    return old;
}

int mouse_get_x(void) { return cursor_x; }
int mouse_get_y(void) { return cursor_y; }
uint8_t mouse_get_buttons(void) {
    int prev_tail = (queue_tail - 1 + MOUSE_QUEUE_SIZE) % MOUSE_QUEUE_SIZE;
    return event_queue[prev_tail].buttons;
}
