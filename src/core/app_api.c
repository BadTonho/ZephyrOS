#include "core/app_api.h"
#include "core/app_files.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"
#include "process/process.h"

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

    if (app_files_init() != OK) {
        LOG_ERROR("APP_API", "Falha ao inicializar servico de arquivos");
        return ERR_STATE;
    }

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

int app_api_file_open(const char* path, uint32_t mode, app_handle_t* handle) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    return app_files_open(path, mode, handle);
}

int app_api_file_read(app_handle_t handle, uint8_t* buffer,
                      uint32_t size, uint32_t* bytes_read) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    return app_files_read(handle, buffer, size, bytes_read);
}

int app_api_file_write(app_handle_t handle, const uint8_t* buffer,
                       uint32_t size, uint32_t* bytes_written) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    return app_files_write(handle, buffer, size, bytes_written);
}

int app_api_file_close(app_handle_t handle) {
    int result = app_api_require_ready();

    if (result != OK) return result;
    return app_files_close(handle);
}

static int app_api_validate_message(const app_message_t* message) {
    if (!message) {
        LOG_ERROR("APP_API", "Mensagem nula rejeitada");
        return ERR_NULL;
    }
    if (message->type != APP_MESSAGE_KEYBOARD &&
        message->type != APP_MESSAGE_APP_REQUEST) {
        LOG_ERROR("APP_API", "Tipo de mensagem de aplicativo invalido");
        return ERR_INVALID;
    }
    return OK;
}

int app_api_message_send(uint32_t pid, const app_message_t* message) {
    ipc_msg_t ipc_message;
    process_t* target;
    int result = app_api_require_ready();

    if (result != OK) return result;
    if (!ipc_is_ready()) {
        LOG_WARN("APP_API", "IPC indisponivel para envio");
        return ERR_UNAVAILABLE;
    }

    result = app_api_validate_message(message);
    if (result != OK) return result;
    target = process_get_by_pid(pid);
    if (!target) {
        LOG_WARN("APP_API", "PID de destino nao encontrado");
        return ERR_NOT_FOUND;
    }
    if (target->state != PROCESS_STATE_READY &&
        target->state != PROCESS_STATE_RUNNING &&
        target->state != PROCESS_STATE_BLOCKED) {
        LOG_WARN("APP_API", "PID de destino esta inativo");
        return ERR_STATE;
    }

    ipc_message.type = (ipc_msg_type_t)message->type;
    ipc_message.data1 = message->data1;
    ipc_message.data2 = message->data2;
    if (!ipc_send(pid, &ipc_message)) {
        LOG_WARN("APP_API", "Envio IPC recusado pela fila");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

int app_api_message_receive(app_message_t* message) {
    ipc_msg_t ipc_message;
    process_t* current;
    int result = app_api_require_ready();

    if (result != OK) return result;
    if (!message) {
        LOG_ERROR("APP_API", "Destino de mensagem nulo");
        return ERR_NULL;
    }
    if (!ipc_is_ready()) {
        LOG_WARN("APP_API", "IPC indisponivel para recebimento");
        return ERR_UNAVAILABLE;
    }

    current = process_get_current();
    if (!current) {
        LOG_ERROR("APP_API", "Recebimento sem processo atual");
        return ERR_STATE;
    }
    if (!ipc_receive(&ipc_message)) {
        /* Fila vazia e o resultado normal da consulta nao bloqueante. */
        return ERR_NOT_FOUND;
    }

    message->type = (uint32_t)ipc_message.type;
    message->data1 = ipc_message.data1;
    message->data2 = ipc_message.data2;
    return OK;
}

int app_api_file_is_ready(void) {
    return app_api_ready && app_files_is_ready();
}

int app_api_ipc_is_ready(void) {
    return app_api_ready && ipc_is_ready();
}
