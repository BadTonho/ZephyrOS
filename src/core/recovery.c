#include "core/recovery.h"
#include "core/log.h"

static recovery_component_t components[RECOVERY_COMPONENT_COUNT];
static int recovery_initialized = 0;

static const char* component_names[RECOVERY_COMPONENT_COUNT] = {
    "VESA",
    "Backbuffer",
    "ATA",
    "Filesystem",
    "AC97",
    "Process System",
    "Shell",
    "Desktop",
    "Taskbar",
    "Window Manager",
    "Task Manager",
    "File Manager",
    "Settings",
    "Media Player",
    "Editor",
    "GUI Test"
};

static int recovery_valid_component(recovery_component_id_t component) {
    return component >= 0 && component < RECOVERY_COMPONENT_COUNT;
}

static int recovery_set_state(recovery_component_id_t component,
                              recovery_state_t state, int error_code,
                              const char* message) {
    recovery_component_t* entry;

    if (!recovery_initialized) {
        LOG_ERROR("RECOVERY", "Gerenciador de recovery nao inicializado");
        return ERR_STATE;
    }

    if (!recovery_valid_component(component)) {
        LOG_ERROR("RECOVERY", "ID de componente invalido");
        return ERR_INVALID;
    }

    entry = &components[component];
    entry->state = state;
    entry->last_error = error_code;
    entry->last_message = message ? message : "Mensagem de falha ausente";

    if (state != RECOVERY_STATE_READY) {
        entry->failures++;
        LOG_WARN("RECOVERY", entry->last_message);
    }

    return OK;
}

int recovery_init(void) {
    LOG_INFO("RECOVERY", "Inicializando gerenciador de recovery");

    for (int i = 0; i < RECOVERY_COMPONENT_COUNT; i++) {
        components[i].name = component_names[i];
        components[i].state = RECOVERY_STATE_UNKNOWN;
        components[i].failures = 0;
        components[i].last_error = OK;
        components[i].last_message = "Ainda nao inicializado";
    }

    recovery_initialized = 1;
    LOG_INFO("RECOVERY", "Gerenciador de recovery inicializado com sucesso");
    return OK;
}

int recovery_mark_ready(recovery_component_id_t component) {
    if (!recovery_valid_component(component)) {
        LOG_ERROR("RECOVERY", "ID de componente invalido ao marcar pronto");
        return ERR_INVALID;
    }

    return recovery_set_state(component, RECOVERY_STATE_READY, OK,
                              "Componente operacional");
}

int recovery_mark_degraded(recovery_component_id_t component,
                           int error_code, const char* message) {
    return recovery_set_state(component, RECOVERY_STATE_DEGRADED,
                              error_code, message);
}

int recovery_mark_disabled(recovery_component_id_t component,
                           int error_code, const char* message) {
    return recovery_set_state(component, RECOVERY_STATE_DISABLED,
                              error_code, message);
}

int recovery_is_available(recovery_component_id_t component) {
    if (!recovery_initialized) {
        LOG_ERROR("RECOVERY", "Consulta antes da inicializacao");
        return 0;
    }

    if (!recovery_valid_component(component)) {
        LOG_ERROR("RECOVERY", "ID de componente invalido em consulta");
        return 0;
    }

    return components[component].state == RECOVERY_STATE_READY;
}

int recovery_is_enabled(recovery_component_id_t component) {
    if (!recovery_initialized) {
        LOG_ERROR("RECOVERY", "Consulta de disponibilidade antes da inicializacao");
        return 0;
    }

    if (!recovery_valid_component(component)) {
        LOG_ERROR("RECOVERY", "ID de componente invalido em disponibilidade");
        return 0;
    }

    return components[component].state == RECOVERY_STATE_READY ||
           components[component].state == RECOVERY_STATE_DEGRADED;
}

const recovery_component_t* recovery_get(recovery_component_id_t component) {
    if (!recovery_initialized || !recovery_valid_component(component)) {
        LOG_ERROR("RECOVERY", "Consulta de componente invalida");
        return NULL;
    }

    return &components[component];
}

uint32_t recovery_get_count(void) {
    return recovery_initialized ? RECOVERY_COMPONENT_COUNT : 0;
}

const char* recovery_state_name(recovery_state_t state) {
    switch (state) {
        case RECOVERY_STATE_READY: return "READY";
        case RECOVERY_STATE_DEGRADED: return "DEGRADED";
        case RECOVERY_STATE_DISABLED: return "DISABLED";
        default: return "UNKNOWN";
    }
}
