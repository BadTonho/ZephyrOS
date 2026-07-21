#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

/* Bits de botao */
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

/* Tipos de evento de clique */
#define MOUSE_EVENT_MOVE    0
#define MOUSE_EVENT_PRESS   1
#define MOUSE_EVENT_RELEASE 2

typedef struct {
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
} mouse_packet_t;

/* Evento processado enviado ao callback */
typedef struct {
    int x;          /* Posicao absoluta X do cursor */
    int y;          /* Posicao absoluta Y do cursor */
    uint8_t buttons; /* Estado atual dos botoes */
    uint8_t event;   /* MOUSE_EVENT_MOVE, PRESS ou RELEASE */
    uint8_t changed; /* Mascara dos botoes que mudaram */
} mouse_event_t;

typedef void (*mouse_callback_t)(mouse_event_t*);

void mouse_init(void);
void mouse_process_events(void);
mouse_callback_t mouse_set_callback(mouse_callback_t cb);
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);

#endif
