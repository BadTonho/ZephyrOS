#include "core/keyboard.h"
#include "drivers/idt.h"
#include "core/log.h"
#include "core/errors.h"
#include "process/process.h"


#define KEYBOARD_QUEUE_SIZE 64
#define KEYBOARD_SCANCODE_F12 0x58U

static volatile uint8_t event_queue[KEYBOARD_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static volatile uint32_t dropped_events;
static uint8_t drop_warning_active;
static uint8_t forward_warning_active;

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
    drop_warning_active = 0;
    forward_warning_active = 0;
    if (idt_register_handler(33, keyboard_handler) != OK) {
        LOG_ERROR("KBD", "Falha ao registrar IRQ do teclado");
        return;
    }
    LOG_INFO("KBD", "Teclado inicializado com sucesso");
}

void keyboard_handler(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    // Nao descarta scancode & 0x80 para permitir a detecao de Key Release 
    // e prefixos estendidos (0xE0). Os apps tratam press/release conforme necessario.

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
        if (!drop_warning_active) {
            LOG_WARN("KBD", "Fila de teclado cheia; eventos descartados");
        }
        drop_warning_active = 1;
    } else {
        drop_warning_active = 0;
    }

    while (queue_tail != queue_head) {
        uint8_t scancode = event_queue[queue_tail];
        uint32_t focus = process_get_focus();
        process_t* target = process_get_by_pid(focus);
        ipc_msg_t msg;

        if (scancode == KEYBOARD_SCANCODE_F12) {
            int result = process_cancel_focused_user(PROCESS_EXIT_CANCELLED);

            if (result == OK) {
                queue_tail = (uint8_t)((queue_tail + 1) % KEYBOARD_QUEUE_SIZE);
                forward_warning_active = 0;
                LOG_INFO("KBD", "F12 cancelou aplicativo ring 3 em foco");
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
        if (!forward_warning_active) {
            LOG_WARN("KBD", "Evento descartado: processo em foco indisponivel");
        }
        forward_warning_active = 1;
    }
}


