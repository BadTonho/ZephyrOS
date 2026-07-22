#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"

static int app_api_ready = 0;

static int app_api_require_ready(void) {
    if (!app_api_ready) {
        LOG_ERROR("APP_API", "API consultada antes da inicializacao");
        return ERR_STATE;
    }
    return OK;
}

static int app_api_validate_text(const char* text, uint32_t size) {
    if (!text) {
        LOG_ERROR("APP_API", "Texto nulo rejeitado");
        return ERR_NULL;
    }
    if (size == 0) {
        LOG_ERROR("APP_API", "Texto vazio rejeitado");
        return ERR_INVALID;
    }
    if (size > APP_API_MAX_TEXT_SIZE) {
        LOG_ERROR("APP_API", "Texto excede o limite da API");
        return ERR_OVERFLOW;
    }

    for (uint32_t i = 0; i < size; i++) {
        uint8_t value = (uint8_t)text[i];
        if (value < 0x20 && value != '\n' && value != '\r' && value != '\t') {
            LOG_ERROR("APP_API", "Texto contem caractere de controle invalido");
            return ERR_INVALID;
        }
        if (value > 0x7E) {
            LOG_ERROR("APP_API", "Texto fora do conjunto suportado");
            return ERR_INVALID;
        }
    }

    return OK;
}

int app_api_init(void) {
    LOG_INFO("APP_API", "Inicializando contrato da API de aplicativos");

    app_api_ready = 1;
    LOG_INFO("APP_API", "Contrato da API de aplicativos inicializado");
    return OK;
}

int app_api_is_ready(void) {
    return app_api_ready;
}

int app_api_get_version(app_api_version_t* version) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    if (!version) {
        LOG_ERROR("APP_API", "Destino de versao nulo");
        return ERR_NULL;
    }

    version->major = APP_API_VERSION_MAJOR;
    version->minor = APP_API_VERSION_MINOR;
    return OK;
}

int app_api_console_write(const char* text, uint32_t size) {
    char buffer[APP_API_MAX_TEXT_SIZE + 1];
    int result = app_api_require_ready();

    if (result != OK) return result;

    result = app_api_validate_text(text, size);
    if (result != OK) return result;

    kmemcpy(buffer, text, size);
    buffer[size] = '\0';
    video_print(buffer, 0x07);
    return OK;
}

int app_api_get_uptime(app_uptime_info_t* info) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    if (!info) {
        LOG_ERROR("APP_API", "Destino de uptime nulo");
        return ERR_NULL;
    }

    info->ticks = timer_get_ticks();
    info->seconds = info->ticks / APP_API_TICKS_PER_SECOND;
    return OK;
}

int app_api_get_memory_info(app_memory_info_t* info) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    if (!info) {
        LOG_ERROR("APP_API", "Destino de memoria nulo");
        return ERR_NULL;
    }

    info->total_bytes = memory_get_total();
    info->used_bytes = memory_get_used();
    info->free_bytes = memory_get_free();
    info->total_pages = memory_get_total_pages();
    info->free_pages = memory_get_free_pages();
    return OK;
}
