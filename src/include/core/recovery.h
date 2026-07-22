#ifndef RECOVERY_H
#define RECOVERY_H

#include "types.h"
#include "core/errors.h"

typedef enum {
    RECOVERY_STATE_UNKNOWN = 0,
    RECOVERY_STATE_READY,
    RECOVERY_STATE_DEGRADED,
    RECOVERY_STATE_DISABLED
} recovery_state_t;

typedef enum {
    RECOVERY_COMPONENT_VESA = 0,
    RECOVERY_COMPONENT_BACKBUFFER,
    RECOVERY_COMPONENT_ATA,
    RECOVERY_COMPONENT_FILESYSTEM,
    RECOVERY_COMPONENT_AC97,
    RECOVERY_COMPONENT_SYSTEM_PROCESS,
    RECOVERY_COMPONENT_SHELL,
    RECOVERY_COMPONENT_DESKTOP,
    RECOVERY_COMPONENT_TASKBAR,
    RECOVERY_COMPONENT_WM,
    RECOVERY_COMPONENT_TASKMANAGER,
    RECOVERY_COMPONENT_FILEMANAGER,
    RECOVERY_COMPONENT_SETTINGS,
    RECOVERY_COMPONENT_MEDIAPLAYER,
    RECOVERY_COMPONENT_EDITOR,
    RECOVERY_COMPONENT_GUITEST,
    RECOVERY_COMPONENT_APP_LOADER,
    RECOVERY_COMPONENT_COUNT
} recovery_component_id_t;

typedef struct {
    const char* name;
    recovery_state_t state;
    uint32_t failures;
    int last_error;
    const char* last_message;
} recovery_component_t;

int recovery_init(void);
int recovery_mark_ready(recovery_component_id_t component);
int recovery_mark_degraded(recovery_component_id_t component,
                           int error_code, const char* message);
int recovery_mark_disabled(recovery_component_id_t component,
                           int error_code, const char* message);
int recovery_is_available(recovery_component_id_t component);
int recovery_is_enabled(recovery_component_id_t component);
const recovery_component_t* recovery_get(recovery_component_id_t component);
uint32_t recovery_get_count(void);
const char* recovery_state_name(recovery_state_t state);

#endif
