#include "drivers/mouse.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/vesa.h"

/* Multiplicador de velocidade para telas de alta resolucao */
#define MOUSE_SPEED 3

/* Dimensoes do cursor em pixels */
#define CURSOR_W 12
#define CURSOR_H 16

/* Tamanho da fila circular de pacotes brutos */
#define MOUSE_QUEUE_SIZE 128

static mouse_packet_t event_queue[MOUSE_QUEUE_SIZE];
static volatile int queue_head = 0;
static volatile int queue_tail = 0;

static mouse_callback_t current_callback = 0;
static int driver_initialized = 0;

/* Estado do cursor */
static int cursor_drawn = 0;
static int cursor_x = 0;
static int cursor_y = 0;
static int prev_x = 0;
static int prev_y = 0;
static vesa_color_t bg_buffer[CURSOR_W * CURSOR_H];
static int cursor_visible = 1;

/* Estado dos botoes para detectar press/release */
static uint8_t prev_buttons = 0;
static uint8_t current_buttons = 0;

/* Sincronizacao do protocolo PS/2 de 3 bytes */
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
        /* Espera dado disponivel (bit 0 do status) */
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        /* Espera controladora pronta para receber (bit 1 limpo) */
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

/* ========== Renderizacao do cursor ========== */

static void erase_cursor(void) {
    if (!cursor_drawn) return;
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized) return;

    for (int i = 0; i < CURSOR_H; i++) {
        for (int j = 0; j < CURSOR_W; j++) {
            int px = prev_x + j;
            int py = prev_y + i;
            if (px < (int)mode->width && py < (int)mode->height) {
                vesa_put_pixel(px, py, bg_buffer[i * CURSOR_W + j]);
            }
        }
    }
    cursor_drawn = 0;
}

static void draw_cursor(void) {
    if (!cursor_visible) return;
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized) return;

    /* Salva o fundo antes de desenhar */
    for (int i = 0; i < CURSOR_H; i++) {
        for (int j = 0; j < CURSOR_W; j++) {
            int px = cursor_x + j;
            int py = cursor_y + i;
            if (px < (int)mode->width && py < (int)mode->height) {
                bg_buffer[i * CURSOR_W + j] = vesa_get_pixel(px, py);
            }
        }
    }

    vesa_color_t white;
    white.raw = vesa_rgb(255, 255, 255);
    vesa_color_t black;
    black.raw = vesa_rgb(0, 0, 0);

    /* Desenha seta do cursor */
    for (int i = 0; i < CURSOR_H; i++) {
        int row_width = i / 2 + 1;
        if (row_width > CURSOR_W) row_width = CURSOR_W;
        for (int j = 0; j < row_width; j++) {
            int px = cursor_x + j;
            int py = cursor_y + i;
            if (px < (int)mode->width && py < (int)mode->height) {
                if (j == 0 || j == row_width - 1 || i == CURSOR_H - 1) {
                    vesa_put_pixel(px, py, black);
                } else {
                    vesa_put_pixel(px, py, white);
                }
            }
        }
    }

    cursor_drawn = 1;
    prev_x = cursor_x;
    prev_y = cursor_y;
}

/* ========== Handler de interrupcao (IRQ12) ========== */

static void mouse_handler(registers_t* regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    if (!(status & 0x20)) return;

    packet[cycle++] = inb(0x60);
    if (cycle == 3) {
        cycle = 0;

        /* Valida pacote: bits 6-7 devem ser 0, bit 3 deve ser 1 */
        if ((packet[0] & 0xC0) || !(packet[0] & 0x08)) return;

        int next_tail = (queue_tail + 1) % MOUSE_QUEUE_SIZE;
        if (next_tail != queue_head) {
            int32_t dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
            int32_t dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);

            event_queue[queue_tail].dx = dx;
            event_queue[queue_tail].dy = dy;
            event_queue[queue_tail].buttons = packet[0] & 0x07;
            queue_tail = next_tail;
        }
    }
}

/* ========== Inicializacao ========== */

void mouse_init(void) {
    LOG_INFO("MOUSE", "Inicializando driver PS/2...");

    /* Habilita porta auxiliar (mouse) */
    mouse_wait(1);
    outb(0x64, 0xA8);

    /* Configura controladora para gerar IRQ12 */
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = inb(0x60) | 2;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);

    /* Restaura padroes */
    mouse_write(0xF6);
    mouse_read();

    /* Configura sample rate para 200 amostras/s (mais responsivo) */
    mouse_write(0xF3);
    mouse_read();
    mouse_write(200);
    mouse_read();

    /* Habilita data reporting */
    mouse_write(0xF4);
    mouse_read();

    /* Registra handler na IRQ12 (INT 44) */
    idt_register_handler(44, (isr_handler_t)mouse_handler);

    /* Posiciona cursor no centro da tela */
    vesa_mode_t* mode = vesa_get_mode();
    if (mode && mode->initialized) {
        cursor_x = mode->width / 2;
        cursor_y = mode->height / 2;
        prev_x = cursor_x;
        prev_y = cursor_y;
    }

    driver_initialized = 1;
    LOG_INFO("MOUSE", "Inicializado com sucesso");
}

/* ========== Processamento de eventos ========== */

void mouse_process_events(void) {
    if (!driver_initialized) return;
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized) return;

    /* Se nao ha eventos, apenas garante que o cursor esteja desenhado */
    if (queue_head == queue_tail) {
        if (!cursor_drawn && cursor_visible) {
            draw_cursor();
        }
        return;
    }

    /* Apaga o cursor UMA VEZ antes de processar todos os eventos */
    erase_cursor();

    /* Acumula todos os movimentos e pega o ultimo estado de botoes */
    int32_t total_dx = 0;
    int32_t total_dy = 0;
    uint8_t last_buttons = current_buttons;

    while (queue_head != queue_tail) {
        mouse_packet_t pkt = event_queue[queue_head];
        queue_head = (queue_head + 1) % MOUSE_QUEUE_SIZE;

        total_dx += pkt.dx;
        total_dy += pkt.dy;
        last_buttons = pkt.buttons;
    }

    /* Aplica multiplicador de velocidade */
    cursor_x += total_dx * MOUSE_SPEED;
    cursor_y -= total_dy * MOUSE_SPEED;

    /* Limita dentro da tela */
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= (int)mode->width) cursor_x = mode->width - 1;
    if (cursor_y >= (int)mode->height) cursor_y = mode->height - 1;

    /* Detecta press/release comparando com estado anterior */
    prev_buttons = current_buttons;
    current_buttons = last_buttons;
    uint8_t changed = prev_buttons ^ current_buttons;

    /* Despacha evento ao callback registrado */
    if (current_callback) {
        mouse_event_t evt;
        evt.x = cursor_x;
        evt.y = cursor_y;
        evt.buttons = current_buttons;
        evt.changed = changed;

        if (changed) {
            /* Algum botao mudou de estado */
            if (current_buttons & changed) {
                evt.event = MOUSE_EVENT_PRESS;
            } else {
                evt.event = MOUSE_EVENT_RELEASE;
            }
            current_callback(&evt);
        } else if (total_dx != 0 || total_dy != 0) {
            evt.event = MOUSE_EVENT_MOVE;
            current_callback(&evt);
        }
    }

    /* Redesenha o cursor UMA VEZ na posicao final */
    draw_cursor();
}

mouse_callback_t mouse_set_callback(mouse_callback_t cb) {
    mouse_callback_t old = current_callback;
    current_callback = cb;
    return old;
}

int mouse_get_x(void) { return cursor_x; }
int mouse_get_y(void) { return cursor_y; }

uint8_t mouse_get_buttons(void) {
    return current_buttons;
}
