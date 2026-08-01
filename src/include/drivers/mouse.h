#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

/* Bits de botao */
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

#define MOUSE_SPEED_MIN     1
#define MOUSE_SPEED_MAX     10
#define MOUSE_SPEED_DEFAULT 3

/* Tipos de evento de clique */
#define MOUSE_EVENT_MOVE    0
#define MOUSE_EVENT_PRESS   1
#define MOUSE_EVENT_RELEASE 2
#define MOUSE_EVENT_WHEEL   3

typedef struct {
    int32_t dx;
    int32_t dy;
    int8_t wheel;
    uint8_t buttons;
} mouse_packet_t;

/* Evento processado enviado ao callback */
typedef struct {
    int x;          /* Posicao absoluta X do cursor */
    int y;          /* Posicao absoluta Y do cursor */
    uint8_t buttons; /* Estado efetivo apos remapear o botao principal */
    uint8_t event;   /* MOUSE_EVENT_MOVE, PRESS, RELEASE ou WHEEL */
    uint8_t changed; /* Mascara efetiva dos botoes que mudaram */
    int8_t wheel;    /* Delta vertical; positivo representa roda para cima */
} mouse_event_t;

typedef enum {
    MOUSE_PRIMARY_LEFT = 0,
    MOUSE_PRIMARY_RIGHT
} mouse_primary_button_t;

typedef struct {
    uint8_t speed;
    uint8_t acceleration_enabled;
    mouse_primary_button_t primary_button;
} mouse_config_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t initialized;
    uint8_t raw_buttons;
    uint8_t effective_buttons;
    uint8_t wheel_supported;
    uint32_t dropped_packets;
    int last_error;
    mouse_config_t config;
} mouse_status_t;

typedef void (*mouse_callback_t)(mouse_event_t*);

int mouse_init(void);
void mouse_process_events(void);
void mouse_invalidate_cursor(void);
mouse_callback_t mouse_set_callback(mouse_callback_t cb);
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);
int mouse_has_wheel(void);
int mouse_get_config(mouse_config_t* config);
int mouse_get_status(mouse_status_t* status);
int mouse_set_speed(uint8_t speed);
int mouse_set_acceleration(int enabled);
int mouse_set_primary_button(mouse_primary_button_t primary_button);

#endif
