#include "drivers/mouse.h"
#include "drivers/idt.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/vesa.h"
#include "core/errors.h"

/* Dimensoes do cursor em pixels */
#define CURSOR_W 12
#define CURSOR_H 16

#define MOUSE_QUEUE_SIZE 256
#define MOUSE_RAW_QUEUE_SIZE 512U
#define MOUSE_BOTTOM_HALF_MAX_EVENTS 8U
#define MOUSE_BOTTOM_HALF_MAX_BYTES 32U
#define MOUSE_EVENT_BATCH_BUDGET 8U
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
#define MOUSE_ACCEL_MEDIUM_THRESHOLD 4
#define MOUSE_ACCEL_FAST_THRESHOLD 8
#define MOUSE_ACCEL_MEDIUM_NUMERATOR 3
#define MOUSE_ACCEL_MEDIUM_DENOMINATOR 2
#define MOUSE_ACCEL_FAST_NUMERATOR 2
#define MOUSE_CONTROLLER_PORT 0x64
#define MOUSE_DATA_PORT 0x60
#define MOUSE_STATUS_OUTPUT_FULL 0x01
#define MOUSE_STATUS_INPUT_FULL 0x02
#define MOUSE_STATUS_AUX_DATA 0x20
#define MOUSE_CONTROLLER_ENABLE_AUX 0xA8
#define MOUSE_CONTROLLER_READ_CONFIG 0x20
#define MOUSE_CONTROLLER_WRITE_CONFIG 0x60
#define MOUSE_CONTROLLER_WRITE_MOUSE 0xD4
#define MOUSE_CONTROLLER_IRQ12_ENABLE 0x02
#define MOUSE_IRQ_VECTOR 44
#define MOUSE_WAIT_TIMEOUT 100000U
#define MOUSE_BUTTON_MASK 0x07
#define MOUSE_EFLAGS_INTERRUPT_ENABLE 0x200U

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} mouse_damage_region_t;

typedef struct {
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    uint8_t next_raw_buttons;
    int raw_buttons_changed;
} mouse_event_batch_t;

static mouse_packet_t event_queue[MOUSE_QUEUE_SIZE];
static volatile int queue_head = 0;
static volatile int queue_tail = 0;
static volatile uint8_t mouse_raw_queue[MOUSE_RAW_QUEUE_SIZE];
static volatile uint16_t mouse_raw_head;
static volatile uint16_t mouse_raw_tail;
static volatile uint16_t mouse_raw_gap_index;
static volatile uint8_t mouse_raw_gap_pending;
static irq_deferred_work_t mouse_bottom_half_work;

static mouse_callback_t current_callback = 0;
static int driver_initialized = 0;
static int input_sink_ready = 0;
static int last_error = OK;
static volatile uint32_t dropped_packets = 0;
static int queue_overflow_logged = 0;
static mouse_config_t mouse_config = {
    MOUSE_SPEED_DEFAULT, 0, MOUSE_PRIMARY_LEFT
};

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
static uint8_t raw_buttons = 0;

/* Sincronizacao do protocolo PS/2, com fallback para tres bytes. */
static uint8_t cycle = 0;
static uint8_t packet[MOUSE_PACKET_WHEEL_SIZE];
static uint8_t packet_size = MOUSE_PACKET_STANDARD_SIZE;
static int wheel_supported = 0;
static int wheel_fallback_logged = 0;
static void mouse_bottom_half(void* context);
static uint32_t mouse_suspend_interrupts(void);
static void mouse_restore_interrupts(uint32_t flags);

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static int mouse_wait(uint8_t a_type) {
    uint32_t timeout = MOUSE_WAIT_TIMEOUT;
    if (a_type == 0) {
        /* Espera dado disponivel (bit 0 do status) */
        while (timeout--) {
            if ((inb(MOUSE_CONTROLLER_PORT) & MOUSE_STATUS_OUTPUT_FULL) != 0) {
                return OK;
            }
        }
    } else {
        /* Espera controladora pronta para receber (bit 1 limpo) */
        while (timeout--) {
            if ((inb(MOUSE_CONTROLLER_PORT) & MOUSE_STATUS_INPUT_FULL) == 0) {
                return OK;
            }
        }
    }
    LOG_ERROR("MOUSE", "Timeout aguardando controladora PS/2");
    return ERR_TIMEOUT;
}

static int mouse_write(uint8_t a_write) {
    int result = mouse_wait(1);

    if (result != OK) return result;
    outb(MOUSE_CONTROLLER_PORT, MOUSE_CONTROLLER_WRITE_MOUSE);
    result = mouse_wait(1);
    if (result != OK) return result;
    outb(MOUSE_DATA_PORT, a_write);
    return OK;
}

static int mouse_read(uint8_t* value) {
    int result;

    if (!value) {
        LOG_ERROR("MOUSE", "Destino nulo ao ler controladora PS/2");
        return ERR_NULL;
    }
    result = mouse_wait(0);
    if (result != OK) return result;
    *value = inb(MOUSE_DATA_PORT);
    return OK;
}

static int mouse_write_ack(uint8_t value) {
    uint8_t response;
    int result = mouse_write(value);

    if (result != OK) return result;
    result = mouse_read(&response);
    if (result != OK) return result;
    if (response != MOUSE_RESPONSE_ACK) {
        LOG_ERROR("MOUSE", "Controladora PS/2 nao confirmou comando");
        return ERR_NOT_FOUND;
    }
    return OK;
}

static int mouse_set_sample_rate(uint8_t rate) {
    int result = mouse_write_ack(MOUSE_CMD_SET_SAMPLE_RATE);

    if (result != OK) return result;
    return mouse_write_ack(rate);
}

static int mouse_enable_wheel_protocol(void) {
    uint8_t device_id;
    int result;

    result = mouse_set_sample_rate(MOUSE_SAMPLE_RATE_FAST);
    if (result != OK) return result;
    result = mouse_set_sample_rate(MOUSE_SAMPLE_RATE_MID);
    if (result != OK) return result;
    result = mouse_set_sample_rate(MOUSE_SAMPLE_RATE_WHEEL);
    if (result != OK) return result;
    result = mouse_write_ack(MOUSE_CMD_GET_DEVICE_ID);
    if (result != OK) return result;
    result = mouse_read(&device_id);
    if (result != OK) return result;
    if (device_id != MOUSE_ID_INTELLIMOUSE) return ERR_NOT_FOUND;

    packet_size = MOUSE_PACKET_WHEEL_SIZE;
    wheel_supported = 1;
    return OK;
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

static uint8_t mouse_map_buttons(uint8_t buttons) {
    uint8_t mapped = buttons & MOUSE_BTN_MIDDLE;

    if (mouse_config.primary_button == MOUSE_PRIMARY_RIGHT) {
        if (buttons & MOUSE_BTN_RIGHT) mapped |= MOUSE_BTN_LEFT;
        if (buttons & MOUSE_BTN_LEFT) mapped |= MOUSE_BTN_RIGHT;
        return mapped;
    }
    return buttons & (MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT | MOUSE_BTN_MIDDLE);
}

static int32_t mouse_delta_magnitude(int32_t dx, int32_t dy) {
    int32_t abs_dx = dx < 0 ? -dx : dx;
    int32_t abs_dy = dy < 0 ? -dy : dy;

    return abs_dx > abs_dy ? abs_dx : abs_dy;
}

static int32_t mouse_scale_delta(int32_t delta, int32_t magnitude) {
    int32_t scaled = delta * (int32_t)mouse_config.speed;

    if (!mouse_config.acceleration_enabled ||
        magnitude < MOUSE_ACCEL_MEDIUM_THRESHOLD) {
        return scaled;
    }
    if (magnitude < MOUSE_ACCEL_FAST_THRESHOLD) {
        return (scaled * MOUSE_ACCEL_MEDIUM_NUMERATOR) /
               MOUSE_ACCEL_MEDIUM_DENOMINATOR;
    }
    return scaled * MOUSE_ACCEL_FAST_NUMERATOR;
}

static void mouse_collect_event_batch(mouse_event_batch_t* batch) {
    batch->dx = 0;
    batch->dy = 0;
    batch->wheel = 0;
    batch->next_raw_buttons = raw_buttons;
    batch->raw_buttons_changed = 0;

    while (queue_head != queue_tail) {
        mouse_packet_t pkt = event_queue[queue_head];

        queue_head = (queue_head + 1) % MOUSE_QUEUE_SIZE;
        batch->dx += pkt.dx;
        batch->dy += pkt.dy;
        batch->wheel += pkt.wheel;
        if (pkt.buttons != raw_buttons) {
            batch->next_raw_buttons = pkt.buttons;
            batch->raw_buttons_changed = 1;
            break;
        }
        if (pkt.wheel != 0) break;
    }
}

static void mouse_apply_movement(const vesa_mode_t* mode,
                                 const mouse_event_batch_t* batch) {
    int32_t magnitude = mouse_delta_magnitude(batch->dx, batch->dy);

    cursor_x += mouse_scale_delta(batch->dx, magnitude);
    cursor_y -= mouse_scale_delta(batch->dy, magnitude);
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= (int)mode->width) cursor_x = mode->width - 1;
    if (cursor_y >= (int)mode->height) cursor_y = mode->height - 1;
}

static void mouse_dispatch_event(const mouse_event_batch_t* batch,
                                 uint8_t changed) {
    mouse_event_t event;

    if (!current_callback) return;
    event.x = cursor_x;
    event.y = cursor_y;
    event.buttons = current_buttons;
    event.changed = changed;
    event.wheel = 0;

    if (changed) {
        event.event = (current_buttons & changed) ?
                      MOUSE_EVENT_PRESS : MOUSE_EVENT_RELEASE;
        current_callback(&event);
    }
    if (batch->wheel != 0) {
        int32_t wheel = batch->wheel;

        if (wheel > MOUSE_WHEEL_DELTA_MAX) wheel = MOUSE_WHEEL_DELTA_MAX;
        if (wheel < MOUSE_WHEEL_DELTA_MIN) wheel = MOUSE_WHEEL_DELTA_MIN;
        event.event = MOUSE_EVENT_WHEEL;
        event.changed = 0;
        event.wheel = (int8_t)wheel;
        current_callback(&event);
    }
    if (batch->dx != 0 || batch->dy != 0) {
        event.event = MOUSE_EVENT_MOVE;
        event.changed = 0;
        event.wheel = 0;
        current_callback(&event);
    }
}

static void mouse_report_queue_overflow(void) {
    if (!dropped_packets || queue_overflow_logged) return;

    queue_overflow_logged = 1;
    last_error = ERR_OVERFLOW;
    LOG_ERROR("MOUSE", "Fila de eventos do mouse cheia; pacotes descartados");
}

/* ========== Handler de interrupcao (IRQ12) ========== */

static void mouse_handler(registers_t* regs) {
    uint8_t status = inb(MOUSE_CONTROLLER_PORT);
    uint16_t next;

    (void)regs;
    if (!(status & MOUSE_STATUS_OUTPUT_FULL)) return;
    if (!(status & MOUSE_STATUS_AUX_DATA)) return;
    next = (uint16_t)((mouse_raw_head + 1U) % MOUSE_RAW_QUEUE_SIZE);
    if (next == mouse_raw_tail) {
        (void)inb(MOUSE_DATA_PORT);
        if (!mouse_raw_gap_pending) {
            mouse_raw_gap_index = mouse_raw_head;
            mouse_raw_gap_pending = 1U;
        }
        dropped_packets++;
        last_error = ERR_OVERFLOW;
        return;
    }
    mouse_raw_queue[mouse_raw_head] = inb(MOUSE_DATA_PORT);
    mouse_raw_head = next;
    (void)irq_deferred_schedule(&mouse_bottom_half_work);
}

static int mouse_process_raw_byte(uint8_t value) {
    input_pointer_event_t event;
    int result;

    if (cycle == 0U && ((value & 0x08U) == 0U || (value & 0xC0U) != 0U)) {
        return 0;
    }
    packet[cycle++] = value;
    if (cycle == packet_size) {
        cycle = 0;
        if ((packet[0] & 0xC0) || !(packet[0] & 0x08)) return 0;
        event.dx = packet[1] - ((packet[0] & 0x10) ? 256 : 0);
        event.dy = packet[2] - ((packet[0] & 0x20) ? 256 : 0);
        event.wheel = wheel_supported ? mouse_decode_wheel(packet[3]) : 0;
        event.buttons = packet[0] & MOUSE_BUTTON_MASK;
        event.source = INPUT_SOURCE_PS2;
        result = input_publish_pointer(&event);
        if (result != OK) {
            dropped_packets++;
            last_error = result;
            return -1;
        }
        return 1;
    }
    return 0;
}

static void mouse_bottom_half(void* context) {
    uint32_t flags;
    uint32_t bytes = 0U;
    uint32_t events = 0U;
    uint32_t event_budget = MOUSE_BOTTOM_HALF_MAX_EVENTS;
    input_metrics_t metrics;

    (void)context;
    if (input_get_metrics(&metrics) != OK ||
        metrics.pointer_queued >= metrics.pointer_capacity) return;
    if (event_budget > metrics.pointer_capacity - metrics.pointer_queued) {
        event_budget = metrics.pointer_capacity - metrics.pointer_queued;
    }
    while (bytes < MOUSE_BOTTOM_HALF_MAX_BYTES &&
           events < event_budget) {
        uint8_t value;
        int result;

        flags = mouse_suspend_interrupts();
        if (mouse_raw_gap_pending &&
            mouse_raw_tail == mouse_raw_gap_index) {
            cycle = 0U;
            mouse_raw_gap_pending = 0U;
        }
        if (mouse_raw_tail == mouse_raw_head) {
            mouse_restore_interrupts(flags);
            break;
        }
        value = mouse_raw_queue[mouse_raw_tail];
        mouse_raw_tail =
            (uint16_t)((mouse_raw_tail + 1U) % MOUSE_RAW_QUEUE_SIZE);
        mouse_restore_interrupts(flags);
        result = mouse_process_raw_byte(value);
        if (result > 0) events++;
        bytes++;
    }
}

/* ========== Inicializacao ========== */

static uint32_t mouse_suspend_interrupts(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void mouse_restore_interrupts(uint32_t flags) {
    if (flags & MOUSE_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static int mouse_enqueue_packet(const mouse_packet_t* value) {
    uint32_t flags;
    int next_tail;

    if (!value) return ERR_NULL;
    flags = mouse_suspend_interrupts();
    next_tail = (queue_tail + 1) % MOUSE_QUEUE_SIZE;
    if (next_tail == queue_head) {
        dropped_packets++;
        last_error = ERR_OVERFLOW;
        mouse_restore_interrupts(flags);
        return ERR_OVERFLOW;
    }
    event_queue[queue_tail] = *value;
    queue_tail = next_tail;
    mouse_restore_interrupts(flags);
    return OK;
}

static int mouse_input_sink(const input_pointer_event_t* event) {
    mouse_packet_t packet_value;

    if (!event) return ERR_NULL;
    packet_value.dx = event->dx;
    packet_value.dy = event->dy;
    packet_value.wheel = event->wheel;
    packet_value.buttons = event->buttons & MOUSE_BUTTON_MASK;
    return mouse_enqueue_packet(&packet_value);
}

static int mouse_init_fail(int error, const char* message, uint32_t flags) {
    driver_initialized = 0;
    last_error = error;
    mouse_restore_interrupts(flags);
    LOG_ERROR("MOUSE", message);
    return error;
}

static void mouse_reset_state(void) {
    driver_initialized = 0;
    input_sink_ready = 0;
    current_callback = 0;
    packet_size = MOUSE_PACKET_STANDARD_SIZE;
    wheel_supported = 0;
    cycle = 0;
    queue_head = 0;
    queue_tail = 0;
    mouse_raw_head = 0U;
    mouse_raw_tail = 0U;
    mouse_raw_gap_index = 0U;
    mouse_raw_gap_pending = 0U;
    mouse_bottom_half_work.queued = 0U;
    mouse_bottom_half_work.running = 0U;
    dropped_packets = 0;
    queue_overflow_logged = 0;
    prev_buttons = 0;
    current_buttons = 0;
    raw_buttons = 0;
    cursor_drawn = 0;
    cursor_x = 0;
    cursor_y = 0;
    prev_x = 0;
    prev_y = 0;
    mouse_config.speed = MOUSE_SPEED_DEFAULT;
    mouse_config.acceleration_enabled = 0;
    mouse_config.primary_button = MOUSE_PRIMARY_LEFT;
    last_error = OK;
}

int mouse_init(void) {
    uint32_t interrupt_flags;
    uint8_t controller_config;
    int result;

    LOG_INFO("MOUSE", "Inicializando driver PS/2...");
    mouse_reset_state();
    result = input_register_pointer_sink(mouse_input_sink);
    if (result != OK) {
        LOG_ERROR("MOUSE", "Falha ao registrar consumidor de entrada");
        return result;
    }
    input_sink_ready = 1;
    result = irq_deferred_work_init(&mouse_bottom_half_work, "Mouse", 12U,
                                    mouse_bottom_half, 0);
    if (result != OK) {
        LOG_ERROR("MOUSE", "Falha ao preparar Bottom-Half do mouse");
        return result;
    }
    /* A IRQ1 nao pode consumir respostas da transacao compartilhada PS/2. */
    interrupt_flags = mouse_suspend_interrupts();

    /* Habilita porta auxiliar (mouse) */
    result = mouse_wait(1);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao habilitar porta auxiliar",
                               interrupt_flags);
    }
    outb(MOUSE_CONTROLLER_PORT, MOUSE_CONTROLLER_ENABLE_AUX);

    /* Configura controladora para gerar IRQ12 */
    result = mouse_wait(1);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao consultar controladora PS/2",
                               interrupt_flags);
    }
    outb(MOUSE_CONTROLLER_PORT, MOUSE_CONTROLLER_READ_CONFIG);
    result = mouse_read(&controller_config);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao ler configuracao PS/2",
                               interrupt_flags);
    }
    controller_config |= MOUSE_CONTROLLER_IRQ12_ENABLE;
    result = mouse_wait(1);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao preparar configuracao PS/2",
                               interrupt_flags);
    }
    outb(MOUSE_CONTROLLER_PORT, MOUSE_CONTROLLER_WRITE_CONFIG);
    result = mouse_wait(1);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao gravar configuracao PS/2",
                               interrupt_flags);
    }
    outb(MOUSE_DATA_PORT, controller_config);

    /* Restaura padroes */
    result = mouse_write_ack(MOUSE_CMD_SET_DEFAULTS);
    if (result != OK) {
        return mouse_init_fail(result, "Mouse PS/2 ausente ou sem resposta",
                               interrupt_flags);
    }

    result = mouse_enable_wheel_protocol();
    if (result == OK) {
        LOG_INFO("MOUSE", "Roda PS/2 habilitada");
    } else {
        /* O protocolo basico conserva a taxa de movimento anterior. */
        packet_size = MOUSE_PACKET_STANDARD_SIZE;
        wheel_supported = 0;
        result = mouse_set_sample_rate(MOUSE_SAMPLE_RATE_FAST);
        if (result != OK) {
            return mouse_init_fail(result,
                                   "Falha ao ativar protocolo PS/2 basico",
                                   interrupt_flags);
        }
        if (!wheel_fallback_logged) {
            wheel_fallback_logged = 1;
            LOG_WARN("MOUSE", "Roda PS/2 indisponivel; usando protocolo basico");
        }
    }

    result = mouse_write_ack(MOUSE_CMD_ENABLE_REPORTING);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao habilitar dados do mouse",
                               interrupt_flags);
    }

    /* Instala o handler antes de restaurar as interrupcoes do processador. */
    result = idt_register_handler(MOUSE_IRQ_VECTOR,
                                  (isr_handler_t)mouse_handler);
    if (result != OK) {
        return mouse_init_fail(result, "Falha ao registrar IRQ do mouse",
                               interrupt_flags);
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
    last_error = OK;
    mouse_restore_interrupts(interrupt_flags);
    LOG_INFO("MOUSE", "Inicializado com sucesso");
    return OK;
}

/* ========== Processamento de eventos ========== */

void mouse_process_events(void) {
    int old_x;
    int old_y;
    int had_old_cursor;
    uint8_t frame_open = 0U;
    uint32_t batches = 0U;

    if (!input_sink_ready) return;
    mouse_bottom_half(0);
    mouse_report_queue_overflow();
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
    old_x = prev_x;
    old_y = prev_y;
    had_old_cursor = cursor_drawn;
    erase_cursor();

    while (queue_head != queue_tail && batches < MOUSE_EVENT_BATCH_BUDGET) {
        mouse_event_batch_t batch;
        uint8_t changed;

        mouse_collect_event_batch(&batch);
        mouse_apply_movement(mode, &batch);
        prev_buttons = current_buttons;
        if (batch.raw_buttons_changed) {
            raw_buttons = batch.next_raw_buttons;
            current_buttons = mouse_map_buttons(raw_buttons);
        }
        changed = prev_buttons ^ current_buttons;
        if (changed && !frame_open) {
            vesa_frame_begin();
            frame_open = 1U;
        }
        mouse_dispatch_event(&batch, changed);
        batches++;
    }

    draw_cursor();
    if (frame_open) {
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
    if (!input_sink_ready) {
        LOG_WARN("MOUSE", "Consulta de roda antes da inicializacao");
        return 0;
    }
    return wheel_supported;
}

int mouse_get_config(mouse_config_t* config) {
    if (!config) {
        LOG_ERROR("MOUSE", "Destino nulo ao consultar configuracao");
        return ERR_NULL;
    }
    *config = mouse_config;
    return OK;
}

int mouse_get_status(mouse_status_t* status) {
    if (!status) {
        LOG_ERROR("MOUSE", "Destino nulo ao consultar status");
        return ERR_NULL;
    }
    status->x = cursor_x;
    status->y = cursor_y;
    status->initialized = driver_initialized ? 1 : 0;
    status->raw_buttons = raw_buttons;
    status->effective_buttons = current_buttons;
    status->wheel_supported = wheel_supported ? 1 : 0;
    status->dropped_packets = dropped_packets;
    status->last_error = last_error;
    status->config = mouse_config;
    return OK;
}

int mouse_set_speed(uint8_t speed) {
    if (!input_sink_ready) {
        last_error = ERR_UNAVAILABLE;
        LOG_ERROR("MOUSE", "Velocidade recusada; driver indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (speed < MOUSE_SPEED_MIN || speed > MOUSE_SPEED_MAX) {
        last_error = ERR_INVALID;
        LOG_ERROR("MOUSE", "Velocidade fora do intervalo permitido");
        return ERR_INVALID;
    }
    mouse_config.speed = speed;
    return OK;
}

int mouse_set_acceleration(int enabled) {
    if (!input_sink_ready) {
        last_error = ERR_UNAVAILABLE;
        LOG_ERROR("MOUSE", "Aceleracao recusada; driver indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (enabled != 0 && enabled != 1) {
        last_error = ERR_INVALID;
        LOG_ERROR("MOUSE", "Valor invalido para aceleracao");
        return ERR_INVALID;
    }
    mouse_config.acceleration_enabled = (uint8_t)enabled;
    return OK;
}

int mouse_set_primary_button(mouse_primary_button_t primary_button) {
    if (!input_sink_ready) {
        last_error = ERR_UNAVAILABLE;
        LOG_ERROR("MOUSE", "Botao principal recusado; driver indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (primary_button != MOUSE_PRIMARY_LEFT &&
        primary_button != MOUSE_PRIMARY_RIGHT) {
        last_error = ERR_INVALID;
        LOG_ERROR("MOUSE", "Botao principal invalido");
        return ERR_INVALID;
    }
    mouse_config.primary_button = primary_button;
    return OK;
}
