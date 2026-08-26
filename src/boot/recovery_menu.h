#ifndef RECOVERY_MENU_H
#define RECOVERY_MENU_H

#include "types.h"

typedef enum {
    RECOVERY_MENU_ACTION_CONTINUE = 0,
    RECOVERY_MENU_ACTION_PREVIOUS,
    RECOVERY_MENU_ACTION_PREVIOUS_DEFAULT,
    RECOVERY_MENU_ACTION_RETRY,
    RECOVERY_MENU_ACTION_LEGACY
} recovery_menu_action_t;

typedef struct {
    const char* diagnostic;
    const char* active;
    const char* pending;
    const char* previous;
    const char* attempt;
    const char* boot_state;
    const char* reason;
    const char* slot_a_state;
    const char* slot_b_state;
    uint32_t sequence;
    uint32_t attempt_sequence;
    uint16_t slot_a_major;
    uint16_t slot_a_minor;
    uint16_t slot_a_patch;
    uint16_t slot_b_major;
    uint16_t slot_b_minor;
    uint16_t slot_b_patch;
    uint8_t slot_a_version_available;
    uint8_t slot_b_version_available;
    uint8_t failure_menu;
    uint8_t allow_continue;
    uint8_t allow_previous;
    uint8_t allow_retry;
} recovery_menu_view_t;

void recovery_console_init(uint8_t* vesa);
void recovery_console_clear(void);
void recovery_console_print(const char* message);
void recovery_console_print_u32(uint32_t value);
int recovery_menu_wait_f8(void);
recovery_menu_action_t recovery_menu_run(const recovery_menu_view_t* view);
int recovery_menu_confirm_retry(const recovery_menu_view_t* view);

#endif
