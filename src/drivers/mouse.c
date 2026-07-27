#include "drivers/mouse.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/vesa.h"
#include "core/errors.h"

/* Multiplicador de velocidade para telas de alta resolucao */
#define MOUSE_SPEED 3

/* Dimensoes do cursor em pixels */
#define CURSOR_W 12
#define CURSOR_H 16

/* Tamanho da fila circular de pacotes brutos */
#define MOUSE_QUEUE_SIZE 128
#define MOUSE_PACKET_STANDARD_SIZE 3
#define MOUSE_PACKET_WHEEL_SIZE 4
#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_SET_SAMPLE_RATE 0xF3
#define MOUSE_CMD_ENABLE_REPORTING 0xF4
#define MOUSE_CMD_GET_DEVICE_ID 0xF2
#define MOUSE_RESPONSE_ACK 0xFA
#define MOUSE_ID_INTELLIMOUSE 0x03
#define MOUSE_SAMPLE_RATE_FAST 200
#define MOUSE_SAMPLE_RATE_MID 100
#define MOUSE_SAMPLE_RATE_WHEEL 80
#define MOUSE_WHEEL_VALUE_MASK 0x0F
#define MOUSE_WHEEL_SIGN_BIT 0x08
#define MOUSE_WHEEL_SIGN_EXTEND 0xF0
#define MOUSE_WHEEL_DELTA_MAX 127
#define MOUSE_WHEEL_DELTA_MIN -127

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} mouse_damage_region_t;

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

/* Sincronizacao do protocolo PS/2, com fallback para tres bytes. */
static uint8_t cycle = 0;
static uint8_t packet[MOUSE_PACKET_WHEEL_SIZE];
static uint8_t packet_size = MOUSE_PACKET_STANDARD_SIZE;
static int wheel_supported = 0;
static int wheel_fallback_logged = 0;

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

static int mouse_write_ack(uint8_t value) {
    mouse_write(value);
    return mouse_read() == MOUSE_RESPONSE_ACK;
}

static int mouse_set_sample_rate(uint8_t rate) {
    return mouse_write_ack(MOUSE_CMD_SET_SAMPLE_RATE) && mouse_write_ack(rate);
}

static int mouse_enable_wheel_protocol(void) {
    uint8_t device_id;

    if (!mouse_set_sample_rate(MOUSE_SAMPLE_RATE_FAST) ||
        !mouse_set_sample_rate(MOUSE_SAMPLE_RATE_MID) ||
        !mouse_set_sample_rate(MOUSE_SAMPLE_RATE_WHEEL) ||
        !mouse_write_ack(MOUSE_CMD_GET_DEVICE_ID)) {
        return 0;
    }
    device_id = mouse_read();
    if (device_id != MOUSE_ID_INTELLIMOUSE) return 0;

    packet_size = MOUSE_PACKET_WHEEL_SIZE;
    wheel_supported = 1;
    return 1;
}

static int8_t mouse_decode_wheel(uint8_t value) {
    int8_t delta;

    value &= MOUSE_WHEEL_VALUE_MASK;
    if (value & MOUSE_WHEEL_SIGN_BIT) value |= MOUSE_WHEEL_SIGN_EXTEND;
    delta = (int8_t)value;
    /* O pacote PS/2 usa o sentido oposto ao scroll visual do ZephyrOS. */
    return (int8_t)-delta;
}

/* ========== Renderizacao do cursor ========== */

static void erase_cursor(void) {
    if (!cursor_drawn) return;
    vesa_mode_t* mode = vesa_get_mode();
    if (!mode || !mode->initialized) {
        cursor_drawn = 0;
        return;
    }

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

void mouse_invalidate_cursor(void) {
    if (cursor_drawn) {
        vesa_frame_mark_region((uint32_t)prev_x, (uint32_t)prev_y,
                               CURSOR_W, CURSOR_H);
    }
    erase_cursor();
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

static int mouse_make_damage_region(const vesa_mode_t* mode, int x, int y,
                                    mouse_damage_region_t* region) {
    int left = x;
    int top = y;
    int right = x + CURSOR_W;
    int bottom = y + CURSOR_H;

    if (!mode || !mode->initialized || !region) return 0;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)mode->width) right = mode->width;
    if (bottom > (int)mode->height) bottom = mode->height;
    if (right <= left || bottom <= top) return 0;

    region->x = (uint32_t)left;
    region->y = (uint32_t)top;
    region->width = (uint32_t)(right - left);
    region->height = (uint32_t)(bottom - top);
    return 1;
}

static uint32_t mouse_damage_region_area(const mouse_damage_region_t* region) {
    return region->width * region->height;
}

static mouse_damage_region_t mouse_damage_region_union(
    const mouse_damage_region_t* first, const mouse_damage_region_t* second) {
    mouse_damage_region_t combined;
    uint32_t left = first->x < second->x ? first->x : second->x;
    uint32_t top = first->y < second->y ? first->y : second->y;
    uint32_t first_right = first->x + first->width;
    uint32_t second_right = second->x + second->width;
    uint32_t first_bottom = first->y + first->height;
    uint32_t second_bottom = second->y + second->height;
    uint32_t right = first_right > second_right ? first_right : second_right;
    uint32_t bottom = first_bottom > second_bottom ? first_bottom : second_bottom;

    combined.x = left;
    combined.y = top;
    combined.width = right - left;
    combined.height = bottom - top;
    return combined;
}

static void mouse_present_damage_region(const mouse_damage_region_t* region) {
    vesa_flip_region(region->x, region->y, region->width, region->height);
}

static void mouse_present_cursor(int old_x, int old_y, int had_old_cursor) {
    vesa_mode_t* mode = vesa_get_mode();
    mouse_damage_region_t old_region;
    mouse_damage_region_t new_region;
    mouse_damage_region_t combined_region;
    int has_old_region;
    int has_new_region;

    if (!mode || !mode->initialized) return;

    has_old_region = had_old_cursor &&
        mouse_make_damage_region(mode, old_x, old_y, &old_region);
    has_new_region = cursor_drawn &&
        mouse_make_damage_region(mode, cursor_x, cursor_y, &new_region);

    if (!has_old_region && !has_new_region) return;
    if (!has_old_region) {
        mouse_present_damage_region(&new_region);
        return;
    }
    if (!has_new_region) {
        mouse_present_damage_region(&old_region);
        return;
    }

    combined_region = mouse_damage_region_union(&old_region, &new_region);
    if (mouse_damage_region_area(&combined_region) <=
        mouse_damage_region_area(&old_region) +
        mouse_damage_region_area(&new_region)) {
        mouse_present_damage_region(&combined_region);
        return;
    }

    /* Mantem o cursor visivel enquanto a regiao antiga e restaurada. */
    mouse_present_damage_region(&new_region);
    mouse_present_damage_region(&old_region);
}

/* ========== Handler de interrupcao (IRQ12) ========== */

static void mouse_handler(registers_t* regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    if (!(status & 0x20)) return;

    packet[cycle++] = inb(0x60);
    if (cycle == packet_size) {
        cycle = 0;

        /* Valida pacote: bits 6-7 devem ser 0, bit 3 deve ser 1 */
        if ((packet[0] & 0xC0) || !(packet[0] & 0x08)) return;

        int next_tail = (queue_tail + 1) % MOUSE_QUEUE_SIZE;
        if (next_tail != queue_head) {
            int32_t dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
            int32_t dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);

            event_queue[queue_tail].dx = dx;
            event_queue[queue_tail].dy = dy;
            event_queue[queue_tail].wheel = wheel_supported ?
                mouse_decode_wheel(packet[3]) : 0;
            event_queue[queue_tail].buttons = packet[0] & 0x07;
            queue_tail = next_tail;
        }
    }
}

/* ========== Inicializacao ========== */

void mouse_init(void) {
    LOG_INFO("MOUSE", "Inicializando driver PS/2...");
    packet_size = MOUSE_PACKET_STANDARD_SIZE;
    wheel_supported = 0;
    cycle = 0;

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
    mouse_write(MOUSE_CMD_SET_DEFAULTS);
    mouse_read();

    if (mouse_enable_wheel_protocol()) {
        LOG_INFO("MOUSE", "Roda PS/2 habilitada");
    } else {
        /* O protocolo basico conserva a taxa de movimento anterior. */
        (void)mouse_set_sample_rate(MOUSE_SAMPLE_RATE_FAST);
        if (!wheel_fallback_logged) {
            wheel_fallback_logged = 1;
            LOG_WARN("MOUSE", "Roda PS/2 indisponivel; usando protocolo basico");
        }
    }

    /* Habilita data reporting */
    if (!mouse_write_ack(MOUSE_CMD_ENABLE_REPORTING)) {
        LOG_WARN("MOUSE", "Nao confirmou habilitacao de dados do mouse");
    }

    /* Registra handler na IRQ12 (INT 44) */
    if (idt_register_handler(44, (isr_handler_t)mouse_handler) != OK) {
        LOG_ERROR("MOUSE", "Falha ao registrar IRQ do mouse");
        return;
    }

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
            mouse_present_cursor(cursor_x, cursor_y, 0);
        }
        return;
    }

    /* Apaga o cursor UMA VEZ antes de processar todos os eventos */
    int old_x = prev_x;
    int old_y = prev_y;
    int had_old_cursor = cursor_drawn;
    erase_cursor();

    /*
     * Acumula movimentos consecutivos, mas para no primeiro evento de
     * botao. Assim um clique rapido nao desaparece quando press e release
     * chegam na mesma fila entre dois ciclos do sistema.
     */
    int32_t total_dx = 0;
    int32_t total_dy = 0;
    int32_t total_wheel = 0;
    uint8_t next_buttons = current_buttons;

    while (queue_head != queue_tail) {
        mouse_packet_t pkt = event_queue[queue_head];
        queue_head = (queue_head + 1) % MOUSE_QUEUE_SIZE;

        total_dx += pkt.dx;
        total_dy += pkt.dy;
        total_wheel += pkt.wheel;
        if (pkt.buttons != current_buttons) {
            next_buttons = pkt.buttons;
            break;
        }
        if (pkt.wheel != 0) break;
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
    current_buttons = next_buttons;
    uint8_t changed = prev_buttons ^ current_buttons;

    if (changed) vesa_frame_begin();

    /* Despacha evento ao callback registrado */
    if (current_callback) {
        mouse_event_t evt;
        evt.x = cursor_x;
        evt.y = cursor_y;
        evt.buttons = current_buttons;
        evt.changed = changed;
        evt.wheel = 0;

        if (changed) {
            /* Algum botao mudou de estado */
            if (current_buttons & changed) {
                evt.event = MOUSE_EVENT_PRESS;
            } else {
                evt.event = MOUSE_EVENT_RELEASE;
            }
            current_callback(&evt);
        } else if (total_wheel != 0) {
            if (total_wheel > MOUSE_WHEEL_DELTA_MAX) {
                total_wheel = MOUSE_WHEEL_DELTA_MAX;
            }
            if (total_wheel < MOUSE_WHEEL_DELTA_MIN) {
                total_wheel = MOUSE_WHEEL_DELTA_MIN;
            }
            evt.event = MOUSE_EVENT_WHEEL;
            evt.wheel = (int8_t)total_wheel;
            current_callback(&evt);
        } else if (total_dx != 0 || total_dy != 0) {
            evt.event = MOUSE_EVENT_MOVE;
            current_callback(&evt);
        }
    }

    /* Redesenha o cursor UMA VEZ na posicao final */
    draw_cursor();
    if (changed) {
        vesa_frame_end();
    } else {
        mouse_present_cursor(old_x, old_y, had_old_cursor);
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
    return current_buttons;
}

int mouse_has_wheel(void) {
    if (!driver_initialized) {
        LOG_WARN("MOUSE", "Consulta de roda antes da inicializacao");
        return 0;
    }
    return wheel_supported;
}
