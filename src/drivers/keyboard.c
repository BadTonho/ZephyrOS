#include "core/keyboard.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/errors.h"
#include "process/process.h"


#define KEYBOARD_QUEUE_SIZE 256U
#define KEYBOARD_DISPATCH_BUDGET (IPC_MSG_QUEUE_SIZE / 2U)
#define KEYBOARD_SCANCODE_F12 0x58U
#define KEYBOARD_SCANCODE_ISO_SLASH 0x56U
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
static keyboard_focus_cancel_filter_t focus_cancel_filter;
static uint8_t keyboard_initialized;

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
    /* Hosts podem entregar a barra ABNT2 como a tecla ISO extra ou ABNT2. */
    if (scancode == KEYBOARD_SCANCODE_ISO_SLASH ||
        scancode == KEYBOARD_SCANCODE_ABNT2_SLASH) {
        return shifted ? '?' : '/';
    }
    if (scancode >= 128) return 0;
    return shifted ? scancode_shift_table[scancode] : scancode_table[scancode];
}

char keyboard_scancode_to_ascii(uint8_t scancode) {
    return keyboard_scancode_to_ascii_shifted(scancode, 0);
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
    focus_cancel_filter = 0;
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
    (void)regs;
    uint8_t scancode = inb(0x60);

    // Nao descarta scancode & 0x80 para permitir a detecao de Key Release 
    // e prefixos estendidos (0xE0). Os apps tratam press/release conforme necessario.

    uint8_t next_head = (uint8_t)((queue_head + 1) % KEYBOARD_QUEUE_SIZE);
    if (next_head == queue_tail) {
        dropped_events++;
        dropped_total++;
        return;
    }

    event_queue[queue_head] = scancode;
    queue_head = next_head;
    uint32_t queued = queue_head >= queue_tail ?
                      (uint32_t)(queue_head - queue_tail) :
                      KEYBOARD_QUEUE_SIZE - queue_tail + queue_head;
    if (queued > peak_queued) peak_queued = queued;
}

void keyboard_process_events(void) {
    uint32_t dispatched = 0;

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


