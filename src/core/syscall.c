#include "core/syscall.h"
#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/memory.h"
#include "process/process.h"
#include "memory/paging.h"
#include "drivers/tss.h"
#include "fs/fs.h"

static int syscall_ready = 0;
static int syscall_user_mode_ready = 0;

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

static int syscall_is_user_caller(const registers_t* regs) {
    return regs && ((regs->cs & 0x03U) == 0x03U);
}

static int syscall_validate_user_caller(const registers_t* regs) {
    process_t* current = process_get_current();

    if (!syscall_user_mode_ready || !idt_is_user_syscall_enabled()) {
        LOG_WARN("SYSCALL", "Chamada ring 3 antes da habilitacao");
        return ERR_UNAVAILABLE;
    }
    if (!current || !process_is_user(current)) {
        LOG_ERROR("SYSCALL", "Chamador ring 3 sem processo de usuario");
        return ERR_STATE;
    }
    if (regs->cs != USER_CODE_SELECTOR || regs->ss != USER_DATA_SELECTOR) {
        LOG_ERROR("SYSCALL", "Segmentos invalidos no chamador ring 3");
        return ERR_INVALID;
    }
    return OK;
}

static int syscall_copy_user_string(char* destination, uint32_t capacity,
                                    const char* source) {
    if (!destination || !source || capacity == 0) {
        LOG_ERROR("SYSCALL", "String de usuario com argumento invalido");
        return ERR_NULL;
    }
    for (uint32_t i = 0; i < capacity; i++) {
        int result = paging_copy_from_user(&destination[i], source + i, 1);
        if (result != OK) return result;
        if (destination[i] == '\0') return OK;
    }
    LOG_WARN("SYSCALL", "String de usuario sem terminador");
    return ERR_OVERFLOW;
}

static int syscall_user_console_write(const registers_t* regs) {
    char* buffer;
    int result;

    if (!regs->ebx || regs->ecx == 0) {
        LOG_ERROR("SYSCALL", "console_write recebeu texto invalido");
        return ERR_NULL;
    }
    if (regs->ecx > APP_API_MAX_TEXT_SIZE) {
        LOG_WARN("SYSCALL", "console_write excedeu o limite");
        return ERR_OVERFLOW;
    }
    buffer = (char*)kmalloc(regs->ecx);
    if (!buffer) {
        LOG_ERROR("SYSCALL", "console_write sem memoria para copia");
        return ERR_MEM;
    }
    result = paging_copy_from_user(buffer, (const void*)regs->ebx, regs->ecx);
    if (result == OK) result = app_api_console_write(buffer, regs->ecx);
    kfree(buffer);
    return result;
}

static int syscall_user_uptime(const registers_t* regs) {
    app_uptime_info_t info;
    int result;

    if (!regs->ebx) {
        LOG_ERROR("SYSCALL", "uptime recebeu destino nulo");
        return ERR_NULL;
    }
    result = paging_validate_user_range(regs->ebx, sizeof(info), 1);
    if (result != OK) return result;
    result = app_api_get_uptime(&info);
    if (result != OK) return result;
    return paging_copy_to_user((void*)regs->ebx, &info, sizeof(info));
}

static int syscall_user_memory(const registers_t* regs) {
    app_memory_info_t info;
    int result;

    if (!regs->ebx) {
        LOG_ERROR("SYSCALL", "memory_info recebeu destino nulo");
        return ERR_NULL;
    }
    result = paging_validate_user_range(regs->ebx, sizeof(info), 1);
    if (result != OK) return result;
    result = app_api_get_memory_info(&info);
    if (result != OK) return result;
    return paging_copy_to_user((void*)regs->ebx, &info, sizeof(info));
}

static int syscall_user_file_open(const registers_t* regs) {
    char path[FS_MAX_PATH];
    app_handle_t handle = APP_HANDLE_INVALID;
    int result = syscall_copy_user_string(path, sizeof(path),
                                          (const char*)regs->ebx);

    if (result != OK) return result;
    if (!regs->edx) {
        LOG_ERROR("SYSCALL", "file_open recebeu destino nulo");
        return ERR_NULL;
    }
    result = paging_validate_user_range(regs->edx, sizeof(handle), 1);
    if (result != OK) return result;
    result = app_api_file_open(path, regs->ecx, &handle);
    if (result != OK) return result;
    return paging_copy_to_user((void*)regs->edx, &handle, sizeof(handle));
}

static int syscall_user_file_read(const registers_t* regs) {
    uint8_t* buffer;
    uint32_t bytes_read = 0;
    int result;

    if (!regs->ecx || !regs->esi) {
        LOG_ERROR("SYSCALL", "file_read recebeu buffer nulo");
        return ERR_NULL;
    }
    if (regs->edx == 0) {
        LOG_ERROR("SYSCALL", "file_read recebeu tamanho vazio");
        return ERR_INVALID;
    }
    if (regs->edx > APP_API_MAX_FILE_IO_SIZE) {
        LOG_WARN("SYSCALL", "file_read excedeu o limite");
        return ERR_OVERFLOW;
    }
    result = paging_validate_user_range(regs->ecx, regs->edx, 1);
    if (result != OK) return result;
    result = paging_validate_user_range(regs->esi, sizeof(bytes_read), 1);
    if (result != OK) return result;
    buffer = (uint8_t*)kmalloc(regs->edx);
    if (!buffer) {
        LOG_ERROR("SYSCALL", "file_read sem memoria para copia");
        return ERR_MEM;
    }
    result = app_api_file_read((app_handle_t)regs->ebx, buffer,
                               regs->edx, &bytes_read);
    if (result == OK && bytes_read > regs->edx) {
        LOG_ERROR("SYSCALL", "Servico de arquivo retornou leitura invalida");
        result = ERR_OVERFLOW;
    }
    if (result == OK && bytes_read > 0) {
        result = paging_copy_to_user((void*)regs->ecx, buffer, bytes_read);
    }
    if (result == OK) {
        result = paging_copy_to_user((void*)regs->esi, &bytes_read,
                                     sizeof(bytes_read));
    }
    kfree(buffer);
    return result;
}

static int syscall_user_file_write(const registers_t* regs) {
    uint8_t* buffer;
    uint32_t bytes_written = 0;
    int result;

    if (!regs->ecx || !regs->esi) {
        LOG_ERROR("SYSCALL", "file_write recebeu buffer nulo");
        return ERR_NULL;
    }
    if (regs->edx == 0) {
        LOG_ERROR("SYSCALL", "file_write recebeu tamanho vazio");
        return ERR_INVALID;
    }
    if (regs->edx > APP_API_MAX_FILE_IO_SIZE) {
        LOG_WARN("SYSCALL", "file_write excedeu o limite");
        return ERR_OVERFLOW;
    }
    result = paging_validate_user_range(regs->ecx, regs->edx, 0);
    if (result != OK) return result;
    result = paging_validate_user_range(regs->esi, sizeof(bytes_written), 1);
    if (result != OK) return result;
    buffer = (uint8_t*)kmalloc(regs->edx);
    if (!buffer) {
        LOG_ERROR("SYSCALL", "file_write sem memoria para copia");
        return ERR_MEM;
    }
    result = paging_copy_from_user(buffer, (const void*)regs->ecx, regs->edx);
    if (result == OK) {
        result = app_api_file_write((app_handle_t)regs->ebx, buffer,
                                    regs->edx, &bytes_written);
    }
    if (result == OK && bytes_written > regs->edx) {
        LOG_ERROR("SYSCALL", "Servico de arquivo retornou escrita invalida");
        result = ERR_OVERFLOW;
    }
    if (result == OK) {
        result = paging_copy_to_user((void*)regs->esi, &bytes_written,
                                     sizeof(bytes_written));
    }
    kfree(buffer);
    return result;
}

static int syscall_user_message_send(const registers_t* regs) {
    app_message_t message;
    int result;

    if (!regs->ecx) {
        LOG_ERROR("SYSCALL", "message_send recebeu mensagem nula");
        return ERR_NULL;
    }
    result = paging_copy_from_user(&message, (const void*)regs->ecx,
                                   sizeof(message));
    if (result != OK) return result;
    return app_api_message_send(regs->ebx, &message);
}

static int syscall_user_message_receive(const registers_t* regs) {
    app_message_t message;
    int result;

    if (!regs->ebx) {
        LOG_ERROR("SYSCALL", "message_receive recebeu destino nulo");
        return ERR_NULL;
    }
    result = paging_validate_user_range(regs->ebx, sizeof(message), 1);
    if (result != OK) return result;
    result = app_api_message_receive(&message);
    if (result != OK) return result;
    return paging_copy_to_user((void*)regs->ebx, &message, sizeof(message));
}

static int syscall_dispatch_user(registers_t* regs) {
    int result = syscall_validate_user_caller(regs);

    if (result != OK) return result;
    if (!app_api_is_ready()) return ERR_UNAVAILABLE;

    switch (regs->eax) {
        case APP_SYSCALL_PROCESS_EXIT:
            return process_exit_current(regs->ebx);
        case APP_SYSCALL_CONSOLE_WRITE:
            return syscall_user_console_write(regs);
        case APP_SYSCALL_UPTIME:
            return syscall_user_uptime(regs);
        case APP_SYSCALL_MEMORY_INFO:
            return syscall_user_memory(regs);
        case APP_SYSCALL_FILE_OPEN:
            return syscall_user_file_open(regs);
        case APP_SYSCALL_FILE_READ:
            return syscall_user_file_read(regs);
        case APP_SYSCALL_FILE_WRITE:
            return syscall_user_file_write(regs);
        case APP_SYSCALL_FILE_CLOSE:
            return app_api_file_close((app_handle_t)regs->ebx);
        case APP_SYSCALL_MESSAGE_SEND:
            return syscall_user_message_send(regs);
        case APP_SYSCALL_MESSAGE_RECEIVE:
            return syscall_user_message_receive(regs);
        default:
            LOG_WARN("SYSCALL", "Numero de syscall ring 3 desconhecido");
            return ERR_INVALID;
    }
}

static int syscall_dispatch(registers_t* regs) {
    int result = syscall_validate_caller();

    if (result != OK) return result;
    if (syscall_is_user_caller(regs)) {
        return syscall_dispatch_user(regs);
    }
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
    syscall_user_mode_ready = 0;
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

int syscall_enable_user_mode(void) {
    if (!syscall_ready || !paging_is_ready() || !tss_is_ready()) {
        LOG_ERROR("SYSCALL", "Dependencias ausentes para modo usuario");
        return ERR_STATE;
    }
    if (syscall_user_mode_ready) return OK;
    if (idt_enable_user_syscall() != OK) {
        LOG_ERROR("SYSCALL", "Falha ao habilitar gate DPL3");
        return ERR_STATE;
    }
    syscall_user_mode_ready = 1;
    LOG_INFO("SYSCALL", "Dispatcher pronto para processos ring 3");
    return OK;
}

int syscall_user_mode_is_enabled(void) {
    return syscall_user_mode_ready && idt_is_user_syscall_enabled();
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
    regs.cs = KERNEL_CODE_SELECTOR;
    regs.ss = KERNEL_DATA_SELECTOR;
    regs.int_no = SYSCALL_VECTOR;
    syscall_handler(&regs);
    return (int)regs.eax;
}
