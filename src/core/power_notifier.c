#include "core/power_notifier.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"

typedef struct {
    char name[POWER_NOTIFIER_NAME_SIZE];
    power_notifier_callback_t notification;
    power_notifier_callback_t quiescence;
    uint8_t optional;
    uint8_t registered;
} power_notifier_entry_t;

static power_notifier_entry_t power_notifiers[POWER_NOTIFIER_CAPACITY];
static uint32_t power_notifier_count;
static uint8_t power_notifier_initialized;
static uint8_t power_notifier_finalized;
static const char* power_notifier_expected_names[POWER_NOTIFIER_CAPACITY] = {
    "processes", "workqueue", "vfs-storage", "audio", "network", "video"
};

static int power_notifier_name_valid(const char* name) {
    uint32_t length = 0U;

    if (!name || !name[0]) return 0;
    while (name[length] && length < POWER_NOTIFIER_NAME_SIZE) length++;
    return length < POWER_NOTIFIER_NAME_SIZE;
}

static void power_notifier_copy_name(char* destination, const char* source) {
    uint32_t index = 0U;

    while (index + 1U < POWER_NOTIFIER_NAME_SIZE && source[index]) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

int power_notifier_init(void) {
    kmemset(power_notifiers, 0, sizeof(power_notifiers));
    power_notifier_count = 0U;
    power_notifier_finalized = 0U;
    power_notifier_initialized = 1U;
    return OK;
}

int power_notifier_register(const char* name,
                            power_notifier_callback_t notification,
                            power_notifier_callback_t quiescence,
                            uint8_t optional) {
    if (!power_notifier_initialized) {
        LOG_ERROR("POWER", "Registro de notificador antes da inicializacao");
        return ERR_STATE;
    }
    if (power_notifier_finalized) {
        LOG_WARN("POWER", "Registro de notificador apos o bootstrap");
        return ERR_STATE;
    }
    if (!name || !notification || !quiescence) {
        LOG_WARN("POWER", "Notificador nulo rejeitado");
        return name ? ERR_INVALID : ERR_NULL;
    }
    if (!power_notifier_name_valid(name)) {
        LOG_WARN("POWER", "Nome de notificador invalido");
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < power_notifier_count; index++) {
        if (kstrcmp(power_notifiers[index].name, name) == 0) {
            LOG_WARN("POWER", "Notificador duplicado rejeitado");
            return ERR_STATE;
        }
    }
    if (power_notifier_count >= POWER_NOTIFIER_CAPACITY) {
        LOG_ERROR("POWER", "Capacidade da cadeia de energia excedida");
        return ERR_OVERFLOW;
    }
    power_notifier_copy_name(power_notifiers[power_notifier_count].name, name);
    power_notifiers[power_notifier_count].notification = notification;
    power_notifiers[power_notifier_count].quiescence = quiescence;
    power_notifiers[power_notifier_count].optional = optional ? 1U : 0U;
    power_notifiers[power_notifier_count].registered = 1U;
    power_notifier_count++;
    return OK;
}

int power_notifier_finalize(void) {
    if (!power_notifier_initialized) {
        LOG_ERROR("POWER", "Finalizacao da cadeia antes da inicializacao");
        return ERR_STATE;
    }
    if (!power_notifier_count) {
        LOG_ERROR("POWER", "Cadeia de energia sem participantes");
        return ERR_STATE;
    }
    power_notifier_finalized = 1U;
    return OK;
}

static int power_notifier_run(uint32_t deadline_tick,
                              uint8_t quiescence,
                              uint32_t* out_completed,
                              uint32_t* out_optional_failures) {
    uint32_t completed = 0U;
    uint32_t optional_failures = 0U;

    if (!out_completed || !out_optional_failures) {
        LOG_ERROR("POWER", "Destinos nulos ao executar notificadores");
        return ERR_NULL;
    }
    if (!power_notifier_initialized || !power_notifier_finalized) {
        LOG_ERROR("POWER", "Cadeia de energia nao finalizada");
        return ERR_STATE;
    }
    if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) {
        LOG_WARN("POWER", "Cadeia de energia iniciou fora do prazo");
        return ERR_TIMEOUT;
    }
    for (uint32_t index = 0U; index < power_notifier_count; index++) {
        power_notifier_callback_t callback = quiescence ?
            power_notifiers[index].quiescence :
            power_notifiers[index].notification;
        int result;

        if ((int32_t)(timer_get_ticks() - deadline_tick) >= 0) {
            LOG_WARN("POWER", "Cadeia de energia excedeu o prazo");
            return ERR_TIMEOUT;
        }
        result = callback(deadline_tick);
        if (result == OK) {
            completed++;
            continue;
        }
        if (result == ERR_UNAVAILABLE && power_notifiers[index].optional) {
            optional_failures++;
            LOG_WARN("POWER", "Participante opcional indisponivel");
            continue;
        }
        LOG_ERROR_CODE("POWER", result, "Participante de energia falhou");
        return result;
    }
    *out_completed = completed;
    *out_optional_failures = optional_failures;
    return OK;
}

int power_notifier_notify(uint32_t deadline_tick, uint32_t* out_completed,
                          uint32_t* out_optional_failures) {
    return power_notifier_run(deadline_tick, 0U, out_completed,
                              out_optional_failures);
}

int power_notifier_quiesce(uint32_t deadline_tick, uint32_t* out_completed,
                           uint32_t* out_optional_failures) {
    return power_notifier_run(deadline_tick, 1U, out_completed,
                              out_optional_failures);
}

int power_notifier_get_count(uint32_t* out_count) {
    if (!out_count) {
        LOG_ERROR("POWER", "Destino nulo na contagem de notificadores");
        return ERR_NULL;
    }
    if (!power_notifier_initialized) {
        LOG_ERROR("POWER", "Contagem da cadeia antes da inicializacao");
        return ERR_STATE;
    }
    *out_count = power_notifier_count;
    return OK;
}

int power_notifier_validate_state(void) {
    if (!power_notifier_initialized || !power_notifier_finalized) {
        LOG_ERROR("POWER", "Cadeia de energia nao esta pronta");
        return ERR_STATE;
    }
    if (power_notifier_count != POWER_NOTIFIER_CAPACITY) {
        LOG_ERROR("POWER", "Cadeia de energia incompleta");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < POWER_NOTIFIER_CAPACITY; index++) {
        if (!power_notifiers[index].registered ||
            !power_notifiers[index].notification ||
            !power_notifiers[index].quiescence ||
            kstrcmp(power_notifiers[index].name,
                    power_notifier_expected_names[index]) != 0) {
            LOG_ERROR("POWER", "Ordem ou callback da cadeia invalido");
            return ERR_STATE;
        }
        for (uint32_t other = index + 1U;
             other < POWER_NOTIFIER_CAPACITY; other++) {
            if (kstrcmp(power_notifiers[index].name,
                       power_notifiers[other].name) == 0) {
                LOG_ERROR("POWER", "Cadeia de energia contem duplicata");
                return ERR_STATE;
            }
        }
    }
    return OK;
}
