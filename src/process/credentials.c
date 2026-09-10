#include "process/credentials.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "process/process.h"

int process_credentials_init_native(process_credentials_t* credentials) {
    if (!credentials) {
        LOG_ERROR("PROC", "Destino de credenciais nativo ausente");
        return ERR_NULL;
    }
    credentials->uid = PROCESS_UID_ROOT;
    credentials->gid = PROCESS_GID_ROOT;
    credentials->capabilities = PROCESS_CAPABILITIES_NATIVE;
    return OK;
}

int process_credentials_init_user(process_credentials_t* credentials) {
    if (!credentials) {
        LOG_ERROR("PROC", "Destino de credenciais de usuario ausente");
        return ERR_NULL;
    }
    credentials->uid = PROCESS_UID_USER;
    credentials->gid = PROCESS_GID_USER;
    credentials->capabilities = PROCESS_CAPABILITIES_USER;
    return OK;
}

int process_credentials_validate(const process_credentials_t* credentials) {
    if (!credentials) return ERR_NULL;
    if (credentials->uid == PROCESS_UID_ROOT &&
        credentials->gid == PROCESS_GID_ROOT &&
        credentials->capabilities == PROCESS_CAPABILITIES_NATIVE) return OK;
    if (credentials->uid == PROCESS_UID_USER &&
        credentials->gid == PROCESS_GID_USER &&
        credentials->capabilities == PROCESS_CAPABILITIES_USER) return OK;
    LOG_ERROR("PROC", "Credenciais de processo invalidas");
    return ERR_STATE;
}

int process_credentials_copy(const struct process* process,
                             process_credentials_t* output) {
    if (!process || !output) {
        LOG_ERROR("PROC", "Processo ou destino de credenciais ausente");
        return ERR_NULL;
    }
    *output = process->credentials;
    return process_credentials_validate(output);
}

int process_credentials_current(process_credentials_t* output) {
    process_t* current;

    if (!output) {
        LOG_ERROR("PROC", "Destino de credenciais atuais ausente");
        return ERR_NULL;
    }
    current = process_get_current();
    if (!current) {
        LOG_WARN("PROC", "Processo atual indisponivel");
        return ERR_STATE;
    }
    return process_credentials_copy(current, output);
}

int process_credentials_has(const process_credentials_t* credentials,
                            uint32_t capability) {
    if (process_credentials_validate(credentials) != OK || !capability) {
        return 0;
    }
    return (credentials->capabilities & capability) == capability;
}

const char* process_capability_name(uint32_t capability) {
    switch (capability) {
        case PROCESS_CAPABILITY_FILE_READ: return "file-read";
        case PROCESS_CAPABILITY_FILE_WRITE: return "file-write";
        case PROCESS_CAPABILITY_FILE_EXECUTE: return "file-execute";
        case PROCESS_CAPABILITY_DEVICE_BASIC: return "device-basic";
        case PROCESS_CAPABILITY_DEVICE_AUDIO: return "device-audio";
        case PROCESS_CAPABILITY_DEVICE_BLOCK: return "device-block";
        case PROCESS_CAPABILITY_NETWORK: return "network";
        case PROCESS_CAPABILITY_POWER: return "power";
        case PROCESS_CAPABILITY_DIAGNOSTIC_READ: return "diagnostic-read";
        case PROCESS_CAPABILITY_DIAGNOSTIC_WRITE: return "diagnostic-write";
        case PROCESS_CAPABILITY_GLOBAL_SYNC: return "global-sync";
        case PROCESS_CAPABILITY_PACKAGE_ADMIN: return "package-admin";
        case PROCESS_CAPABILITY_IPC: return "ipc";
        default: return "unknown";
    }
}
