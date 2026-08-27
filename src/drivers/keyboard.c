#include "core/keyboard.h"
#include "drivers/idt.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/errors.h"
#include "process/process.h"


#define KEYBOARD_QUEUE_SIZE 256U
#define KEYBOARD_RAW_QUEUE_SIZE 256U
#define KEYBOARD_BOTTOM_HALF_RAW_BUDGET 8U
#define KEYBOARD_DISPATCH_BUDGET (IPC_MSG_QUEUE_SIZE / 2U)
#define KEYBOARD_SCANCODE_F12 0x58U
#define KEYBOARD_SCANCODE_ABNT2_SEMICOLON 0x35U
#define KEYBOARD_SCANCODE_ISO_EXTRA 0x56U
#define KEYBOARD_SCANCODE_ABNT2_SLASH 0x73U

static volatile uint8_t event_queue[KEYBOARD_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static volatile uint32_t dropped_events;
static volatile uint32_t dropped_total;
static volatile uint32_t processed_total;
static volatile uint32_t peak_queued;
static uint8_t drop_warning_active;
static uint8_t forward_warning_active;
static uint8_t input_warning_active;
static keyboard_focus_cancel_filter_t focus_cancel_filter;
static uint8_t keyboard_initialized;
static uint8_t keyboard_ps2_extended;
static volatile uint8_t keyboard_raw_queue[KEYBOARD_RAW_QUEUE_SIZE];
static volatile uint8_t keyboard_raw_head;
static volatile uint8_t keyboard_raw_tail;
static irq_deferred_work_t keyboard_bottom_half_work;
static void keyboard_bottom_half(void* context);

static uint32_t keyboard_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void keyboard_irq_restore(uint32_t flags) {
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
}

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static const char scancode_table[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';', '\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.',';',0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_shift_table[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>',':',0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

char keyboard_scancode_to_ascii_shifted(uint8_t scancode, uint8_t shifted) {
    /* A tecla brasileira /? e distinta da tecla ISO extra \\|. */
    if (scancode == KEYBOARD_SCANCODE_ABNT2_SLASH) {
        return shifted ? '?' : '/';
    }
    if (scancode == KEYBOARD_SCANCODE_ISO_EXTRA) {
        return shifted ? '|' : '\\';
    }
    if (scancode >= 128) return 0;
    return shifted ? scancode_shift_table[scancode] : scancode_table[scancode];
}

static int keyboard_raw_enqueue(uint8_t scancode) {
    uint8_t next =
        (uint8_t)((keyboard_raw_head + 1U) % KEYBOARD_RAW_QUEUE_SIZE);

    if (next == keyboard_raw_tail) {
        dropped_events++;
        dropped_total++;
        return ERR_OVERFLOW;
    }
    keyboard_raw_queue[keyboard_raw_head] = scancode;
    keyboard_raw_head = next;
    return OK;
}

char keyboard_scancode_to_ascii(uint8_t scancode) {
    return keyboard_scancode_to_ascii_shifted(scancode, 0);
}

static uint16_t keyboard_ps2_usage(uint8_t scancode, uint8_t extended) {
    static const uint16_t digits[] = {
        INPUT_USAGE_1, INPUT_USAGE_2, INPUT_USAGE_3, INPUT_USAGE_4,
        INPUT_USAGE_5, INPUT_USAGE_6, INPUT_USAGE_7, INPUT_USAGE_8,
        INPUT_USAGE_9, INPUT_USAGE_0
    };
    static const uint16_t top_row[] = {
        INPUT_USAGE_Q, INPUT_USAGE_W, INPUT_USAGE_E, INPUT_USAGE_R,
        INPUT_USAGE_T, INPUT_USAGE_Y, INPUT_USAGE_U, INPUT_USAGE_I,
        INPUT_USAGE_O, INPUT_USAGE_P
    };
    static const uint16_t home_row[] = {
        INPUT_USAGE_A, INPUT_USAGE_S, INPUT_USAGE_D, INPUT_USAGE_F,
        INPUT_USAGE_G, INPUT_USAGE_H, INPUT_USAGE_J, INPUT_USAGE_K,
        INPUT_USAGE_L
    };
    static const uint16_t bottom_row[] = {
        INPUT_USAGE_Z, INPUT_USAGE_X, INPUT_USAGE_C, INPUT_USAGE_V,
        INPUT_USAGE_B, INPUT_USAGE_N, INPUT_USAGE_M
    };

    if (extended) {
        switch (scancode) {
            case 0x1CU: return INPUT_USAGE_ENTER;
            case 0x1DU: return INPUT_USAGE_RIGHT_CTRL;
            case 0x37U: return INPUT_USAGE_PRINT_SCREEN;
            case 0x38U: return INPUT_USAGE_RIGHT_ALT;
            case 0x47U: return INPUT_USAGE_HOME;
            case 0x48U: return INPUT_USAGE_UP;
            case 0x49U: return INPUT_USAGE_PAGE_UP;
            case 0x4BU: return INPUT_USAGE_LEFT;
            case 0x4DU: return INPUT_USAGE_RIGHT;
            case 0x4FU: return INPUT_USAGE_END;
            case 0x50U: return INPUT_USAGE_DOWN;
            case 0x51U: return INPUT_USAGE_PAGE_DOWN;
            case 0x52U: return INPUT_USAGE_INSERT;
            case 0x53U: return INPUT_USAGE_DELETE;
            case 0x5BU: return INPUT_USAGE_LEFT_GUI;
            case 0x5CU: return INPUT_USAGE_RIGHT_GUI;
            default: return 0U;
        }
    }
    if (scancode >= 0x02U && scancode <= 0x0BU) {
        return digits[scancode - 0x02U];
    }
    if (scancode >= 0x10U && scancode <= 0x19U) {
        return top_row[scancode - 0x10U];
    }
    if (scancode >= 0x1EU && scancode <= 0x26U) {
        return home_row[scancode - 0x1EU];
    }
    if (scancode >= 0x2CU && scancode <= 0x32U) {
        return bottom_row[scancode - 0x2CU];
    }
    if (scancode >= 0x3BU && scancode <= 0x44U) {
        return INPUT_USAGE_F1 + (scancode - 0x3BU);
    }
    switch (scancode) {
        case 0x01U: return INPUT_USAGE_ESCAPE;
        case 0x0CU: return INPUT_USAGE_MINUS;
        case 0x0DU: return INPUT_USAGE_EQUAL;
        case 0x0EU: return INPUT_USAGE_BACKSPACE;
        case 0x0FU: return INPUT_USAGE_TAB;
        case 0x1AU: return INPUT_USAGE_LEFT_BRACKET;
        case 0x1BU: return INPUT_USAGE_RIGHT_BRACKET;
        case 0x1CU: return INPUT_USAGE_ENTER;
        case 0x1DU: return INPUT_USAGE_LEFT_CTRL;
        case 0x2AU: return INPUT_USAGE_LEFT_SHIFT;
        case 0x2BU: return INPUT_USAGE_BACKSLASH;
        case KEYBOARD_SCANCODE_ABNT2_SEMICOLON:
            return INPUT_USAGE_SEMICOLON;
        case KEYBOARD_SCANCODE_ISO_EXTRA:
            return INPUT_USAGE_NON_US_BACKSLASH;
        case KEYBOARD_SCANCODE_ABNT2_SLASH:
            return INPUT_USAGE_INTERNATIONAL1;
        case 0x36U: return INPUT_USAGE_RIGHT_SHIFT;
        case 0x37U: return INPUT_USAGE_PRINT_SCREEN;
        case 0x38U: return INPUT_USAGE_LEFT_ALT;
        case 0x39U: return INPUT_USAGE_SPACE;
        case 0x3AU: return INPUT_USAGE_CAPS_LOCK;
        case 0x45U: return INPUT_USAGE_NUM_LOCK;
        case 0x46U: return INPUT_USAGE_SCROLL_LOCK;
        case 0x47U: return INPUT_USAGE_HOME;
        case 0x48U: return INPUT_USAGE_UP;
        case 0x49U: return INPUT_USAGE_PAGE_UP;
        case 0x4BU: return INPUT_USAGE_LEFT;
        case 0x4DU: return INPUT_USAGE_RIGHT;
        case 0x4FU: return INPUT_USAGE_END;
        case 0x50U: return INPUT_USAGE_DOWN;
        case 0x51U: return INPUT_USAGE_PAGE_DOWN;
        case 0x52U: return INPUT_USAGE_INSERT;
        case 0x53U: return INPUT_USAGE_DELETE;
        case 0x57U: return INPUT_USAGE_F1 + 10U;
        case 0x58U: return INPUT_USAGE_F12;
        case 0x27U: return INPUT_USAGE_SEMICOLON;
        case 0x28U: return INPUT_USAGE_APOSTROPHE;
        case 0x29U: return INPUT_USAGE_GRAVE;
        case 0x33U: return INPUT_USAGE_COMMA;
        case 0x34U: return INPUT_USAGE_DOT;
        default: return 0U;
    }
}

static int keyboard_usage_scancode(uint16_t usage, uint8_t* out_scancode,
                                   uint8_t* out_extended) {
    if (!out_scancode || !out_extended) return ERR_NULL;
    *out_extended = 0U;
    if (usage >= INPUT_USAGE_1 && usage <= INPUT_USAGE_0) {
        *out_scancode = (uint8_t)(0x02U + (usage - INPUT_USAGE_1));
        if (usage == INPUT_USAGE_0) *out_scancode = 0x0BU;
        return OK;
    }
    if (usage >= INPUT_USAGE_A && usage <= INPUT_USAGE_Z) {
        static const uint8_t letters[] = {
            0x1EU, 0x30U, 0x2EU, 0x20U, 0x12U, 0x21U, 0x22U,
            0x23U, 0x17U, 0x24U, 0x25U, 0x26U, 0x32U, 0x31U,
            0x18U, 0x19U, 0x10U, 0x13U, 0x1FU, 0x14U, 0x16U,
            0x2FU, 0x11U, 0x2DU, 0x15U, 0x2CU
        };
        *out_scancode = letters[usage - INPUT_USAGE_A];
        return OK;
    }
    if (usage >= INPUT_USAGE_F1 && usage <= INPUT_USAGE_F12) {
        *out_scancode = (uint8_t)(0x3BU + (usage - INPUT_USAGE_F1));
        if (usage == INPUT_USAGE_F11) *out_scancode = 0x57U;
        if (usage == INPUT_USAGE_F12) *out_scancode = 0x58U;
        return OK;
    }
    switch (usage) {
        case INPUT_USAGE_ENTER: *out_scancode = 0x1CU; return OK;
        case INPUT_USAGE_ESCAPE: *out_scancode = 0x01U; return OK;
        case INPUT_USAGE_BACKSPACE: *out_scancode = 0x0EU; return OK;
        case INPUT_USAGE_TAB: *out_scancode = 0x0FU; return OK;
        case INPUT_USAGE_SPACE: *out_scancode = 0x39U; return OK;
        case INPUT_USAGE_MINUS: *out_scancode = 0x0CU; return OK;
        case INPUT_USAGE_EQUAL: *out_scancode = 0x0DU; return OK;
        case INPUT_USAGE_LEFT_BRACKET: *out_scancode = 0x1AU; return OK;
        case INPUT_USAGE_RIGHT_BRACKET: *out_scancode = 0x1BU; return OK;
        case INPUT_USAGE_BACKSLASH: *out_scancode = 0x2BU; return OK;
        case INPUT_USAGE_SEMICOLON: *out_scancode = 0x27U; return OK;
        case INPUT_USAGE_APOSTROPHE: *out_scancode = 0x28U; return OK;
        case INPUT_USAGE_GRAVE: *out_scancode = 0x29U; return OK;
        case INPUT_USAGE_COMMA: *out_scancode = 0x33U; return OK;
        case INPUT_USAGE_DOT: *out_scancode = 0x34U; return OK;
        case INPUT_USAGE_NON_US_BACKSLASH:
            *out_scancode = KEYBOARD_SCANCODE_ISO_EXTRA;
            return OK;
        /* Em ABNT2, 0x38 ocupa fisicamente a tecla ;/:. */
        case INPUT_USAGE_SLASH:
            *out_scancode = KEYBOARD_SCANCODE_ABNT2_SEMICOLON;
            return OK;
        /* A tecla brasileira /? usa o Usage International1 (0x87). */
        case INPUT_USAGE_INTERNATIONAL1:
            *out_scancode = KEYBOARD_SCANCODE_ABNT2_SLASH;
            return OK;
        case INPUT_USAGE_CAPS_LOCK: *out_scancode = 0x3AU; return OK;
        case INPUT_USAGE_NUM_LOCK: *out_scancode = 0x45U; return OK;
        case INPUT_USAGE_SCROLL_LOCK: *out_scancode = 0x46U; return OK;
        case INPUT_USAGE_LEFT_CTRL: *out_scancode = 0x1DU; return OK;
        case INPUT_USAGE_LEFT_SHIFT: *out_scancode = 0x2AU; return OK;
        case INPUT_USAGE_LEFT_ALT: *out_scancode = 0x38U; return OK;
        case INPUT_USAGE_RIGHT_CTRL: *out_scancode = 0x1DU; *out_extended = 1U; return OK;
        case INPUT_USAGE_RIGHT_SHIFT: *out_scancode = 0x36U; return OK;
        case INPUT_USAGE_RIGHT_ALT: *out_scancode = 0x38U; *out_extended = 1U; return OK;
        case INPUT_USAGE_LEFT_GUI: *out_scancode = 0x5BU; *out_extended = 1U; return OK;
        case INPUT_USAGE_RIGHT_GUI: *out_scancode = 0x5CU; *out_extended = 1U; return OK;
        case INPUT_USAGE_PRINT_SCREEN: *out_scancode = 0x37U; *out_extended = 1U; return OK;
        case INPUT_USAGE_HOME: *out_scancode = 0x47U; *out_extended = 1U; return OK;
        case INPUT_USAGE_UP: *out_scancode = 0x48U; *out_extended = 1U; return OK;
        case INPUT_USAGE_PAGE_UP: *out_scancode = 0x49U; *out_extended = 1U; return OK;
        case INPUT_USAGE_LEFT: *out_scancode = 0x4BU; *out_extended = 1U; return OK;
        case INPUT_USAGE_RIGHT: *out_scancode = 0x4DU; *out_extended = 1U; return OK;
        case INPUT_USAGE_END: *out_scancode = 0x4FU; *out_extended = 1U; return OK;
        case INPUT_USAGE_DOWN: *out_scancode = 0x50U; *out_extended = 1U; return OK;
        case INPUT_USAGE_PAGE_DOWN: *out_scancode = 0x51U; *out_extended = 1U; return OK;
        case INPUT_USAGE_INSERT: *out_scancode = 0x52U; *out_extended = 1U; return OK;
        case INPUT_USAGE_DELETE: *out_scancode = 0x53U; *out_extended = 1U; return OK;
        default: return ERR_UNAVAILABLE;
    }
}

static int keyboard_enqueue_scancodes(const uint8_t* bytes, uint32_t count) {
    uint32_t flags;
    uint32_t head;
    uint32_t tail;
    uint32_t available;

    if (!bytes || !count || count > 2U) return ERR_INVALID;
    flags = keyboard_irq_save();
    head = queue_head;
    tail = queue_tail;
    available = tail > head ? tail - head - 1U :
                KEYBOARD_QUEUE_SIZE - head + tail - 1U;
    if (available < count) {
        dropped_events++;
        dropped_total++;
        keyboard_irq_restore(flags);
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < count; index++) {
        event_queue[head] = bytes[index];
        head = (head + 1U) % KEYBOARD_QUEUE_SIZE;
    }
    queue_head = (uint8_t)head;
    {
        uint32_t queued = queue_head >= queue_tail ?
                          (uint32_t)(queue_head - queue_tail) :
                          KEYBOARD_QUEUE_SIZE - queue_tail + queue_head;
        if (queued > peak_queued) peak_queued = queued;
    }
    keyboard_irq_restore(flags);
    return OK;
}

static int keyboard_input_sink(const input_key_event_t* event) {
    uint8_t scancode;
    uint8_t extended;
    uint8_t bytes[2];
    uint32_t count = 1U;
    int result;

    if (!event) return ERR_NULL;
    result = keyboard_usage_scancode(event->usage, &scancode, &extended);
    if (result == ERR_UNAVAILABLE) return OK;
    if (result != OK) return result;
    if (!event->pressed) scancode |= 0x80U;
    if (extended) {
        bytes[0] = 0xE0U;
        bytes[1] = scancode;
        count = 2U;
    } else {
        bytes[0] = scancode;
    }
    return keyboard_enqueue_scancodes(bytes, count);
}

void keyboard_init(void) {
    LOG_INFO("KBD", "Inicializando teclado");
    keyboard_initialized = 0;
    queue_head = 0;
    queue_tail = 0;
    dropped_events = 0;
    dropped_total = 0;
    processed_total = 0;
    peak_queued = 0;
    drop_warning_active = 0;
    forward_warning_active = 0;
    input_warning_active = 0;
    focus_cancel_filter = 0;
    keyboard_ps2_extended = 0U;
    keyboard_raw_head = 0U;
    keyboard_raw_tail = 0U;
    keyboard_bottom_half_work.queued = 0U;
    keyboard_bottom_half_work.running = 0U;
    if (irq_deferred_work_init(&keyboard_bottom_half_work, "Keyboard", 1U,
                               keyboard_bottom_half, 0) != OK) {
        LOG_ERROR("KBD", "Falha ao preparar Bottom-Half do teclado");
        return;
    }
    if (input_register_key_sink(keyboard_input_sink) != OK) {
        LOG_ERROR("KBD", "Falha ao registrar consumidor de entrada");
        return;
    }
    if (idt_register_handler(33, keyboard_handler) != OK) {
        LOG_ERROR("KBD", "Falha ao registrar IRQ do teclado");
        return;
    }
    keyboard_initialized = 1;
    LOG_INFO("KBD", "Teclado inicializado com sucesso");
}

void keyboard_set_focus_cancel_filter(keyboard_focus_cancel_filter_t filter) {
    if (!keyboard_initialized) {
        LOG_WARN("KBD", "Filtro de cancelamento ignorado antes da inicializacao");
        return;
    }
    focus_cancel_filter = filter;
}

void keyboard_handler(registers_t* regs) {
    uint8_t scancode;

    (void)regs;
    scancode = inb(0x60);
    if (keyboard_raw_enqueue(scancode) != OK) return;
    (void)irq_deferred_schedule(&keyboard_bottom_half_work);
}

static int keyboard_process_raw_byte(uint8_t scancode) {
    uint8_t released;
    uint16_t usage;
    input_key_event_t event;

    if (scancode == 0xE0U) {
        keyboard_ps2_extended = 1U;
        return 0;
    }
    if (scancode == 0xE1U) {
        keyboard_ps2_extended = 0U;
        return 0;
    }
    released = (scancode & 0x80U) ? 1U : 0U;
    usage = keyboard_ps2_usage(scancode & 0x7FU, keyboard_ps2_extended);
    keyboard_ps2_extended = 0U;
    if (!usage) return 0;
    event.usage = usage;
    event.pressed = released ? 0U : 1U;
    event.modifiers = 0U;
    event.source = INPUT_SOURCE_PS2;
    return input_publish_key(&event) == OK ? 1 : -1;
}

static void keyboard_bottom_half(void* context) {
    uint32_t flags;
    uint32_t bytes = 0U;
    uint32_t events = 0U;
    uint32_t event_budget = KEYBOARD_BOTTOM_HALF_RAW_BUDGET;
    input_metrics_t metrics;

    (void)context;
    if (input_get_metrics(&metrics) != OK ||
        metrics.key_queued >= metrics.key_capacity) return;
    if (event_budget > metrics.key_capacity - metrics.key_queued) {
        event_budget = metrics.key_capacity - metrics.key_queued;
    }
    while (bytes < KEYBOARD_BOTTOM_HALF_RAW_BUDGET &&
           events < event_budget) {
        uint8_t scancode;
        int result;

        flags = keyboard_irq_save();
        if (keyboard_raw_tail == keyboard_raw_head) {
            keyboard_irq_restore(flags);
            break;
        }
        scancode = keyboard_raw_queue[keyboard_raw_tail];
        keyboard_raw_tail =
            (uint8_t)((keyboard_raw_tail + 1U) % KEYBOARD_RAW_QUEUE_SIZE);
        keyboard_irq_restore(flags);
        result = keyboard_process_raw_byte(scancode);
        if (result > 0) {
            events++;
            input_warning_active = 0;
        } else if (result < 0) {
            if (!input_warning_active) {
                LOG_WARN("KBD",
                         "Nucleo de entrada recusou scancode; aguardando drenagem");
            }
            input_warning_active = 1;
            break;
        }
        bytes++;
    }
}

void keyboard_process_events(void) {
    uint32_t dispatched = 0;
    uint32_t input_processed = 0U;

    if (input_dispatch(KEYBOARD_DISPATCH_BUDGET, &input_processed) != OK) {
        LOG_WARN("KBD", "Despacho do nucleo de entrada indisponivel");
    }
    keyboard_bottom_half(0);
    if (input_dispatch(KEYBOARD_DISPATCH_BUDGET, &input_processed) != OK) {
        LOG_WARN("KBD", "Despacho do nucleo de entrada indisponivel");
    }

    if (dropped_events > 0) {
        dropped_events = 0;
        if (!drop_warning_active) {
            LOG_WARN("KBD", "Fila de teclado cheia; eventos descartados");
        }
        drop_warning_active = 1;
    } else {
        drop_warning_active = 0;
    }

    /* O lote fica abaixo da capacidade IPC para o consumidor executar entre
       envios. A fila fisica maior absorve rajadas durante saidas longas. */
    while (queue_tail != queue_head &&
           dispatched < KEYBOARD_DISPATCH_BUDGET) {
        uint8_t scancode = event_queue[queue_tail];
        uint32_t focus = process_get_focus();
        process_t* target = process_get_by_pid(focus);
        int filtered_cancel = 0;
        ipc_msg_t msg;

        if (focus_cancel_filter && scancode != KEYBOARD_SCANCODE_F12) {
            filtered_cancel = focus_cancel_filter(scancode);
        }

        if (scancode == KEYBOARD_SCANCODE_F12 || filtered_cancel) {
            int result = process_cancel_focused_user(PROCESS_EXIT_CANCELLED);

            if (result == OK) {
                queue_tail = (uint8_t)((queue_tail + 1) % KEYBOARD_QUEUE_SIZE);
                dispatched++;
                forward_warning_active = 0;
                if (scancode == KEYBOARD_SCANCODE_F12) {
                    LOG_DEBUG("KBD", "F12 cancelou aplicativo ring 3 em foco");
                } else {
                    LOG_DEBUG("KBD", "Tecla de cancelamento encerrou aplicativo em foco");
                }
                continue;
            }
            if (filtered_cancel) {
                LOG_WARN("KBD", "Tecla de cancelamento nao conseguiu encerrar aplicativo em foco");
                queue_tail = (uint8_t)((queue_tail + 1) % KEYBOARD_QUEUE_SIZE);
                dispatched++;
                forward_warning_active = 0;
                continue;
            }
            if (result != ERR_UNAVAILABLE && result != ERR_NOT_FOUND) {
                LOG_WARN("KBD", "F12 nao conseguiu cancelar aplicativo em foco");
            }
        }

        msg.type = IPC_MSG_KEYBOARD;
        msg.data1 = scancode;
        msg.data2 = 0;

        if (target && (target->state == PROCESS_STATE_READY ||
                       target->state == PROCESS_STATE_RUNNING ||
                       target->state == PROCESS_STATE_BLOCKED) &&
            ((target->msg_head + 1U) % IPC_MSG_QUEUE_SIZE) == target->msg_tail) {
            /* Evita chamar ipc_send() repetidamente enquanto a fila esta
               cheia, pois o evento continua pendente no buffer do teclado. */
            if (!forward_warning_active) {
                LOG_WARN("KBD", "Fila IPC do foco cheia; aguardando consumo");
            }
            forward_warning_active = 1;
            break;
        }

        if (ipc_send(focus, &msg)) {
            queue_tail = (uint8_t)((queue_tail + 1) % KEYBOARD_QUEUE_SIZE);
            dispatched++;
            forward_warning_active = 0;
            continue;
        }

        if (target && (target->state == PROCESS_STATE_READY ||
                       target->state == PROCESS_STATE_RUNNING ||
                       target->state == PROCESS_STATE_BLOCKED)) {
            /* A fila do destino esta cheia. Nao avance o tail: o evento
               sera encaminhado quando o processo com foco consumir dados. */
            if (!forward_warning_active) {
                LOG_WARN("KBD", "Falha temporaria ao encaminhar evento ao foco");
            }
            forward_warning_active = 1;
            break;
        }

        /* Um foco invalido nao pode bloquear a fila de hardware inteira. */
        queue_tail = (uint8_t)((queue_tail + 1) % KEYBOARD_QUEUE_SIZE);
        dispatched++;
        if (!forward_warning_active) {
            LOG_WARN("KBD", "Evento descartado: processo em foco indisponivel");
        }
        forward_warning_active = 1;
    }
    processed_total += dispatched;
}

void keyboard_get_metrics(keyboard_metrics_t* metrics) {
    uint8_t head;
    uint8_t tail;

    if (!metrics) {
        LOG_ERROR("KBD", "Destino nulo ao consultar metricas");
        return;
    }

    head = queue_head;
    tail = queue_tail;
    metrics->queued = head >= tail ? (uint32_t)(head - tail) :
                      KEYBOARD_QUEUE_SIZE - tail + head;
    metrics->capacity = KEYBOARD_QUEUE_SIZE - 1U;
    metrics->dropped = dropped_total;
    metrics->processed = processed_total;
    metrics->peak_queued = peak_queued;
}


