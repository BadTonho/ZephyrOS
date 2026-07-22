#include "core/syscall.h"
#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "process/process.h"

static int syscall_ready = 0;

static int syscall_validate_caller(void) {
    process_t* current = process_get_current();

    if (!current) {
        LOG_ERROR("SYSCALL", "Syscall sem processo atual");
        return ERR_STATE;
    }
    if (current->state != PROCESS_STATE_READY &&
        current->state != PROCESS_STATE_RUNNING) {
        LOG_WARN("SYSCALL", "Processo chamador esta inativo");
        return ERR_STATE;
    }
    return OK;
}

static int syscall_dispatch(registers_t* regs) {
    int result = syscall_validate_caller();

    if (result != OK) return result;
    if (!app_api_is_ready()) {
        LOG_ERROR("SYSCALL", "App API indisponivel para syscall");
        return ERR_UNAVAILABLE;
    }

    switch (regs->eax) {
        case APP_SYSCALL_PROCESS_EXIT:
            LOG_WARN("SYSCALL", "process_exit bloqueada ate o modo usuario");
            return ERR_UNAVAILABLE;
        case APP_SYSCALL_CONSOLE_WRITE:
            return app_api_console_write((const char*)regs->ebx, regs->ecx);
        case APP_SYSCALL_UPTIME:
            return app_api_get_uptime((app_uptime_info_t*)regs->ebx);
        case APP_SYSCALL_MEMORY_INFO:
            return app_api_get_memory_info((app_memory_info_t*)regs->ebx);
        case APP_SYSCALL_FILE_OPEN:
            return app_api_file_open((const char*)regs->ebx, regs->ecx,
                                     (app_handle_t*)regs->edx);
        case APP_SYSCALL_FILE_READ:
            return app_api_file_read((app_handle_t)regs->ebx,
                                     (uint8_t*)regs->ecx, regs->edx,
                                     (uint32_t*)regs->esi);
        case APP_SYSCALL_FILE_WRITE:
            return app_api_file_write((app_handle_t)regs->ebx,
                                      (const uint8_t*)regs->ecx, regs->edx,
                                      (uint32_t*)regs->esi);
        case APP_SYSCALL_FILE_CLOSE:
            return app_api_file_close((app_handle_t)regs->ebx);
        case APP_SYSCALL_MESSAGE_SEND:
            return app_api_message_send(regs->ebx,
                                         (const app_message_t*)regs->ecx);
        case APP_SYSCALL_MESSAGE_RECEIVE:
            return app_api_message_receive((app_message_t*)regs->ebx);
        default:
            LOG_WARN("SYSCALL", "Numero de syscall desconhecido");
            return ERR_INVALID;
    }
}

int syscall_init(void) {
    LOG_INFO("SYSCALL", "Inicializando dispatcher de syscalls");

    if (!app_api_is_ready()) {
        LOG_ERROR("SYSCALL", "App API deve ser inicializada antes do dispatcher");
        return ERR_UNAVAILABLE;
    }
    if (syscall_ready) {
        LOG_WARN("SYSCALL", "Dispatcher de syscalls ja estava inicializado");
        return OK;
    }
    if (idt_register_handler(SYSCALL_VECTOR, syscall_handler) != OK) {
        LOG_ERROR("SYSCALL", "Falha ao registrar handler de syscall");
        return ERR_STATE;
    }

    syscall_ready = 1;
    LOG_INFO("SYSCALL", "Dispatcher de syscalls inicializado");
    return OK;
}

int syscall_is_ready(void) {
    return syscall_ready;
}

void syscall_handler(registers_t* regs) {
    int result;

    if (!regs) {
        LOG_ERROR("SYSCALL", "Registradores nulos no handler");
        return;
    }
    if (regs->int_no != SYSCALL_VECTOR) {
        LOG_ERROR("SYSCALL", "Vetor invalido no handler");
        regs->eax = ERR_INVALID;
        return;
    }
    if (!syscall_ready) {
        LOG_ERROR("SYSCALL", "Dispatcher chamado antes da inicializacao");
        regs->eax = ERR_STATE;
        return;
    }

    result = syscall_dispatch(regs);
    regs->eax = (uint32_t)result;
}

int syscall_invoke_kernel(uint32_t number,
                          uint32_t arg1,
                          uint32_t arg2,
                          uint32_t arg3,
                          uint32_t arg4,
                          uint32_t arg5) {
    registers_t regs;

    kmemset(&regs, 0, sizeof(registers_t));
    regs.eax = number;
    regs.ebx = arg1;
    regs.ecx = arg2;
    regs.edx = arg3;
    regs.esi = arg4;
    regs.edi = arg5;
    regs.int_no = SYSCALL_VECTOR;
    syscall_handler(&regs);
    return (int)regs.eax;
}
