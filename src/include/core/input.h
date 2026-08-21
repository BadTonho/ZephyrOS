#ifndef INPUT_H
#define INPUT_H

#include "types.h"

#define INPUT_KEY_QUEUE_CAPACITY 256U
#define INPUT_POINTER_QUEUE_CAPACITY 128U

typedef enum {
    INPUT_SOURCE_PS2 = 0,
    INPUT_SOURCE_USB_HID
} input_source_t;

typedef struct {
    uint16_t usage;
    uint8_t pressed;
    uint8_t modifiers;
    input_source_t source;
} input_key_event_t;

typedef struct {
    int32_t dx;
    int32_t dy;
    int8_t wheel;
    uint8_t buttons;
    input_source_t source;
} input_pointer_event_t;

typedef int (*input_key_sink_t)(const input_key_event_t* event);
typedef int (*input_pointer_sink_t)(const input_pointer_event_t* event);

typedef struct {
    uint8_t initialized;
    uint32_t key_queued;
    uint32_t pointer_queued;
    uint32_t key_capacity;
    uint32_t pointer_capacity;
    uint32_t key_published;
    uint32_t pointer_published;
    uint32_t key_processed;
    uint32_t pointer_processed;
    uint32_t key_dropped;
    uint32_t pointer_dropped;
    uint32_t key_peak_queued;
    uint32_t pointer_peak_queued;
    int last_error;
} input_metrics_t;

/* HID Usage IDs usados pelo contrato de entrada do ZephyrOS. */
#define INPUT_USAGE_A 0x04U
#define INPUT_USAGE_B (INPUT_USAGE_A + 1U)
#define INPUT_USAGE_C (INPUT_USAGE_A + 2U)
#define INPUT_USAGE_D (INPUT_USAGE_A + 3U)
#define INPUT_USAGE_E (INPUT_USAGE_A + 4U)
#define INPUT_USAGE_F (INPUT_USAGE_A + 5U)
#define INPUT_USAGE_G (INPUT_USAGE_A + 6U)
#define INPUT_USAGE_H (INPUT_USAGE_A + 7U)
#define INPUT_USAGE_I (INPUT_USAGE_A + 8U)
#define INPUT_USAGE_J (INPUT_USAGE_A + 9U)
#define INPUT_USAGE_K (INPUT_USAGE_A + 10U)
#define INPUT_USAGE_L (INPUT_USAGE_A + 11U)
#define INPUT_USAGE_M (INPUT_USAGE_A + 12U)
#define INPUT_USAGE_N (INPUT_USAGE_A + 13U)
#define INPUT_USAGE_O (INPUT_USAGE_A + 14U)
#define INPUT_USAGE_P (INPUT_USAGE_A + 15U)
#define INPUT_USAGE_Q (INPUT_USAGE_A + 16U)
#define INPUT_USAGE_R (INPUT_USAGE_A + 17U)
#define INPUT_USAGE_S (INPUT_USAGE_A + 18U)
#define INPUT_USAGE_T (INPUT_USAGE_A + 19U)
#define INPUT_USAGE_U (INPUT_USAGE_A + 20U)
#define INPUT_USAGE_V (INPUT_USAGE_A + 21U)
#define INPUT_USAGE_W (INPUT_USAGE_A + 22U)
#define INPUT_USAGE_X (INPUT_USAGE_A + 23U)
#define INPUT_USAGE_Y (INPUT_USAGE_A + 24U)
#define INPUT_USAGE_Z 0x1DU
#define INPUT_USAGE_1 0x1EU
#define INPUT_USAGE_2 (INPUT_USAGE_1 + 1U)
#define INPUT_USAGE_3 (INPUT_USAGE_1 + 2U)
#define INPUT_USAGE_4 (INPUT_USAGE_1 + 3U)
#define INPUT_USAGE_5 (INPUT_USAGE_1 + 4U)
#define INPUT_USAGE_6 (INPUT_USAGE_1 + 5U)
#define INPUT_USAGE_7 (INPUT_USAGE_1 + 6U)
#define INPUT_USAGE_8 (INPUT_USAGE_1 + 7U)
#define INPUT_USAGE_9 (INPUT_USAGE_1 + 8U)
#define INPUT_USAGE_0 0x27U
#define INPUT_USAGE_ENTER 0x28U
#define INPUT_USAGE_ESCAPE 0x29U
#define INPUT_USAGE_BACKSPACE 0x2AU
#define INPUT_USAGE_TAB 0x2BU
#define INPUT_USAGE_SPACE 0x2CU
#define INPUT_USAGE_MINUS 0x2DU
#define INPUT_USAGE_EQUAL 0x2EU
#define INPUT_USAGE_LEFT_BRACKET 0x2FU
#define INPUT_USAGE_RIGHT_BRACKET 0x30U
#define INPUT_USAGE_BACKSLASH 0x31U
#define INPUT_USAGE_NON_US_BACKSLASH 0x64U
#define INPUT_USAGE_SEMICOLON 0x33U
#define INPUT_USAGE_APOSTROPHE 0x34U
#define INPUT_USAGE_GRAVE 0x35U
#define INPUT_USAGE_COMMA 0x36U
#define INPUT_USAGE_DOT 0x37U
#define INPUT_USAGE_SLASH 0x38U
#define INPUT_USAGE_INTERNATIONAL1 0x87U
#define INPUT_USAGE_CAPS_LOCK 0x39U
#define INPUT_USAGE_F1 0x3AU
#define INPUT_USAGE_F2 (INPUT_USAGE_F1 + 1U)
#define INPUT_USAGE_F3 (INPUT_USAGE_F1 + 2U)
#define INPUT_USAGE_F4 (INPUT_USAGE_F1 + 3U)
#define INPUT_USAGE_F5 (INPUT_USAGE_F1 + 4U)
#define INPUT_USAGE_F6 (INPUT_USAGE_F1 + 5U)
#define INPUT_USAGE_F7 (INPUT_USAGE_F1 + 6U)
#define INPUT_USAGE_F8 (INPUT_USAGE_F1 + 7U)
#define INPUT_USAGE_F9 (INPUT_USAGE_F1 + 8U)
#define INPUT_USAGE_F10 (INPUT_USAGE_F1 + 9U)
#define INPUT_USAGE_F11 (INPUT_USAGE_F1 + 10U)
#define INPUT_USAGE_F12 0x45U
#define INPUT_USAGE_PRINT_SCREEN 0x46U
#define INPUT_USAGE_SCROLL_LOCK 0x47U
#define INPUT_USAGE_PAUSE 0x48U
#define INPUT_USAGE_INSERT 0x49U
#define INPUT_USAGE_HOME 0x4AU
#define INPUT_USAGE_PAGE_UP 0x4BU
#define INPUT_USAGE_DELETE 0x4CU
#define INPUT_USAGE_END 0x4DU
#define INPUT_USAGE_PAGE_DOWN 0x4EU
#define INPUT_USAGE_RIGHT 0x4FU
#define INPUT_USAGE_LEFT 0x50U
#define INPUT_USAGE_DOWN 0x51U
#define INPUT_USAGE_UP 0x52U
#define INPUT_USAGE_NUM_LOCK 0x53U
#define INPUT_USAGE_LEFT_CTRL 0xE0U
#define INPUT_USAGE_LEFT_SHIFT 0xE1U
#define INPUT_USAGE_LEFT_ALT 0xE2U
#define INPUT_USAGE_LEFT_GUI 0xE3U
#define INPUT_USAGE_RIGHT_CTRL 0xE4U
#define INPUT_USAGE_RIGHT_SHIFT 0xE5U
#define INPUT_USAGE_RIGHT_ALT 0xE6U
#define INPUT_USAGE_RIGHT_GUI 0xE7U

int input_init(void);
int input_register_key_sink(input_key_sink_t sink);
int input_register_pointer_sink(input_pointer_sink_t sink);
int input_publish_key(const input_key_event_t* event);
int input_publish_pointer(const input_pointer_event_t* event);
int input_dispatch(uint32_t budget, uint32_t* out_processed);
int input_get_metrics(input_metrics_t* out_metrics);
int input_validate_state(void);

#endif
