#include "core/app_loader.h"
#include "core/errors.h"
#include "core/app_files.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/syscall.h"
#include "fs/fs.h"
#include "memory/paging.h"
#include "process/process.h"

static int app_loader_ready;
static uint32_t app_loader_pending_pid;
static uint32_t app_loader_active_pid;
static uint32_t app_loader_focus_acquired;
static app_loader_result_t app_loader_finished_result;
static int app_loader_result_pending;

static int app_loader_is_busy(void) {
    return app_loader_pending_pid != 0 || app_loader_active_pid != 0 ||
           app_loader_result_pending;
}

static int app_loader_is_launch_space(char value) {
    return value == ' ' || value == '\t';
}

static int app_loader_validate_launch_info(const app_launch_info_t* launch) {
    uint32_t position = 0;

    if (!launch || launch->abi_version != APP_LAUNCH_ABI_VERSION ||
        launch->argc > APP_LAUNCH_MAX_ARGS ||
        launch->raw_length > APP_LAUNCH_MAX_RAW_LENGTH ||
        launch->raw_args[launch->raw_length] != '\0') {
        LOG_ERROR("APP_LOADER", "Informacoes de lancamento invalidas");
        return ERR_INVALID;
    }

    for (uint32_t i = 0; i < launch->raw_length; i++) {
        char value = launch->raw_args[i];
        if (!app_loader_is_launch_space(value) &&
            ((uint8_t)value < 0x20U || (uint8_t)value > 0x7EU)) {
            LOG_ERROR("APP_LOADER", "Argumento contem caractere invalido");
            return ERR_INVALID;
        }
    }

    for (uint32_t i = 0; i < launch->argc; i++) {
        while (position < launch->raw_length &&
               app_loader_is_launch_space(launch->raw_args[position])) {
            position++;
        }
        if (launch->args[i].length == 0 ||
            launch->args[i].offset != position ||
            launch->args[i].offset > launch->raw_length ||
            launch->args[i].length >
                launch->raw_length - launch->args[i].offset) {
            LOG_ERROR("APP_LOADER", "Tabela de argumentos inconsistente");
            return ERR_INVALID;
        }
        for (uint32_t j = 0; j < launch->args[i].length; j++) {
            if (app_loader_is_launch_space(
                    launch->raw_args[position + j])) {
                LOG_ERROR("APP_LOADER", "Argumento contem espaco interno");
                return ERR_INVALID;
            }
        }
        position += launch->args[i].length;
    }

    while (position < launch->raw_length &&
           app_loader_is_launch_space(launch->raw_args[position])) {
        position++;
    }
    if (position != launch->raw_length) {
        LOG_ERROR("APP_LOADER", "Argumentos nao foram completamente mapeados");
        return ERR_INVALID;
    }
    return OK;
}

static int app_loader_prepare_launch(const app_launch_info_t* source,
                                     app_launch_info_t* destination) {
    if (!destination) {
        LOG_ERROR("APP_LOADER", "Destino nulo para argumentos ZAPP");
        return ERR_NULL;
    }

    kmemset(destination, 0, sizeof(*destination));
    destination->abi_version = APP_LAUNCH_ABI_VERSION;
    if (source) kmemcpy(destination, source, sizeof(*destination));
    return app_loader_validate_launch_info(destination);
}

int app_loader_build_launch_info(const char* text, app_launch_info_t* launch) {
    uint32_t length;
    uint32_t position = 0;

    if (!text || !launch) {
        LOG_ERROR("APP_LOADER", "Texto ou destino nulo para argumentos");
        return ERR_NULL;
    }

    length = kstrlen(text);
    if (length > APP_LAUNCH_MAX_RAW_LENGTH) {
        LOG_WARN("APP_LOADER", "Texto de argumentos excede o limite");
        return ERR_OVERFLOW;
    }

    kmemset(launch, 0, sizeof(*launch));
    launch->abi_version = APP_LAUNCH_ABI_VERSION;
    launch->raw_length = length;
    kmemcpy(launch->raw_args, text, length);

    while (position < length) {
        while (position < length && app_loader_is_launch_space(text[position])) {
            position++;
        }
        if (position == length) break;
        if (launch->argc >= APP_LAUNCH_MAX_ARGS) {
            LOG_WARN("APP_LOADER", "Quantidade de argumentos excede o limite");
            return ERR_OVERFLOW;
        }
        launch->args[launch->argc].offset = position;
        while (position < length && !app_loader_is_launch_space(text[position])) {
            if ((uint8_t)text[position] < 0x20U ||
                (uint8_t)text[position] > 0x7EU) {
                LOG_ERROR("APP_LOADER", "Argumento contem caractere invalido");
                return ERR_INVALID;
            }
            position++;
        }
        launch->args[launch->argc].length =
            position - launch->args[launch->argc].offset;
        launch->argc++;
    }

    return app_loader_validate_launch_info(launch);
}

static void app_loader_store_result(uint32_t pid, uint32_t exit_code,
                                    uint32_t faulted, uint32_t cancelled,
                                    uint32_t start_failed,
                                    uint32_t focus_acquired) {
    app_loader_finished_result.pid = pid;
    app_loader_finished_result.exit_code = exit_code;
    app_loader_finished_result.faulted = faulted;
    app_loader_finished_result.cancelled = cancelled;
    app_loader_finished_result.start_failed = start_failed;
    app_loader_finished_result.focus_acquired = focus_acquired;
    app_loader_result_pending = 1;
}

static void app_loader_release_process_resources(uint32_t pid) {
    if (app_files_close_owner(pid) != OK) {
        LOG_WARN("APP_LOADER", "Falha ao liberar handles do aplicativo");
    }
}

static int app_loader_start_pending(void) {
    uint32_t pid = app_loader_pending_pid;
    process_t* proc;
    int result;

    if (pid == 0) return OK;

    result = process_start_user(pid);
    if (result == OK) result = process_set_focus(pid);
    if (result != OK) {
        proc = process_get_by_pid(pid);
        if (proc) {
            process_cancel_user(pid, (uint32_t)result);
            app_loader_release_process_resources(pid);
            process_destroy(proc);
        }
        app_loader_pending_pid = 0;
        app_loader_store_result(pid, (uint32_t)result, 0, 0, 1, 0);
        LOG_ERROR("APP_LOADER", "Falha ao iniciar aplicativo ZAPP pendente");
        return result;
    }

    app_loader_pending_pid = 0;
    app_loader_active_pid = pid;
    /* process_set_focus() acabou de validar e registrar o PID. Nao lemos o
       foco novamente porque o aplicativo pode encerrar entre as duas linhas. */
    app_loader_focus_acquired = 1U;
    LOG_DEBUG("APP_LOADER", "Processo ZAPP liberado e recebeu foco");
    return OK;
}

static int app_loader_reap_active(void) {
    process_t* proc;
    uint32_t pid;
    uint32_t exit_code;
    uint32_t faulted;
    uint32_t cancelled;

    if (app_loader_active_pid == 0) return OK;

    pid = app_loader_active_pid;
    proc = process_get_by_pid(pid);
    if (!proc) {
        app_loader_active_pid = 0;
        app_loader_store_result(pid, ERR_STATE, 1, 0, 1,
                                app_loader_focus_acquired);
        app_loader_focus_acquired = 0;
        LOG_ERROR("APP_LOADER", "Processo ZAPP ativo desapareceu");
        return ERR_STATE;
    }
    if (proc->state != PROCESS_STATE_ZOMBIE) return OK;

    exit_code = proc->exit_code;
    faulted = proc->faulted ? 1U : 0U;
    cancelled = exit_code == APP_EXIT_CANCELLED ? 1U : 0U;
    app_loader_release_process_resources(pid);
    process_destroy(proc);
    if (process_get_by_pid(pid)) {
        LOG_ERROR("APP_LOADER", "Processo ZAPP zombie nao foi removido");
        return ERR_STATE;
    }

    app_loader_active_pid = 0;
    app_loader_store_result(pid, exit_code, faulted, cancelled, 0,
                            app_loader_focus_acquired);
    app_loader_focus_acquired = 0;
    LOG_DEBUG("APP_LOADER", "Processo ZAPP encerrado foi recolhido");
    return OK;
}

static int app_loader_magic_is_valid(const char* magic) {
    return magic[0] == 'Z' && magic[1] == 'A' &&
           magic[2] == 'P' && magic[3] == 'P';
}

static int app_loader_has_zap_extension(const char* path) {
    uint32_t length;

    if (!path) return 0;
    length = kstrlen(path);
    if (length < 4) return 0;

    return (path[length - 4] == '.' &&
            (path[length - 3] == 'Z' || path[length - 3] == 'z') &&
            (path[length - 2] == 'A' || path[length - 2] == 'a') &&
            (path[length - 1] == 'P' || path[length - 1] == 'p'));
}

static int app_loader_validate_layout(const app_image_header_t* header,
                                      uint32_t size) {
    uint32_t data_end;

    if (header->header_size != APP_IMAGE_HEADER_SIZE ||
        header->code_offset != APP_IMAGE_HEADER_SIZE) {
        LOG_ERROR("APP_LOADER", "Cabecalho ZAPP com offsets invalidos");
        return ERR_INVALID;
    }
    if (header->code_size == 0 ||
        header->code_size > APP_IMAGE_MAX_CODE_SIZE) {
        LOG_WARN("APP_LOADER", "Tamanho de codigo ZAPP fora do limite");
        return header->code_size > APP_IMAGE_MAX_CODE_SIZE ?
               ERR_OVERFLOW : ERR_INVALID;
    }
    if (header->data_size > APP_IMAGE_MAX_DATA_SIZE) {
        LOG_WARN("APP_LOADER", "Tamanho de dados ZAPP fora do limite");
        return ERR_OVERFLOW;
    }
    if (header->stack_size != APP_IMAGE_STACK_SIZE ||
        header->flags != APP_IMAGE_FLAGS_NONE) {
        LOG_ERROR("APP_LOADER", "Recursos ZAPP nao suportados");
        return ERR_INVALID;
    }
    if (header->entry_offset >= header->code_size) {
        LOG_ERROR("APP_LOADER", "Ponto de entrada ZAPP fora do codigo");
        return ERR_INVALID;
    }
    if (header->code_offset > 0xFFFFFFFFU - header->code_size) {
        LOG_ERROR("APP_LOADER", "Offset de codigo ZAPP excede o limite");
        return ERR_OVERFLOW;
    }
    if (header->data_offset != header->code_offset + header->code_size) {
        LOG_ERROR("APP_LOADER", "Offset de dados ZAPP invalido");
        return ERR_INVALID;
    }
    if (header->data_offset > 0xFFFFFFFFU - header->data_size) {
        LOG_ERROR("APP_LOADER", "Offset de dados ZAPP excede o limite");
        return ERR_OVERFLOW;
    }

    data_end = header->data_offset + header->data_size;
    if (data_end != size) {
        LOG_ERROR("APP_LOADER", "Tamanho do arquivo ZAPP nao corresponde ao cabecalho");
        return data_end > size ? ERR_INVALID : ERR_OVERFLOW;
    }
    return OK;
}

int app_loader_validate_image(const uint8_t* image, uint32_t size,
                              app_image_header_t* header) {
    if (!image || !header) {
        LOG_ERROR("APP_LOADER", "Imagem ou destino de cabecalho nulo");
        return ERR_NULL;
    }
    if (size < APP_IMAGE_HEADER_SIZE) {
        LOG_WARN("APP_LOADER", "Imagem ZAPP menor que o cabecalho");
        return ERR_INVALID;
    }
    if (size > APP_IMAGE_MAX_FILE_SIZE) {
        LOG_WARN("APP_LOADER", "Imagem ZAPP excede o limite de arquivo");
        return ERR_OVERFLOW;
    }

    kmemcpy(header, image, APP_IMAGE_HEADER_SIZE);
    if (!app_loader_magic_is_valid(header->magic) ||
        header->version != APP_IMAGE_VERSION ||
        header->architecture != APP_IMAGE_ARCH_I386) {
        LOG_ERROR("APP_LOADER", "Magic, versao ou arquitetura ZAPP invalidos");
        return ERR_INVALID;
    }

    return app_loader_validate_layout(header, size);
}

int app_loader_init(void) {
    LOG_INFO("APP_LOADER", "Inicializando carregador de aplicativos");
    app_loader_ready = 0;
    app_loader_pending_pid = 0;
    app_loader_active_pid = 0;
    app_loader_focus_acquired = 0;
    app_loader_result_pending = 0;
    kmemset(&app_loader_finished_result, 0, sizeof(app_loader_finished_result));

    if (!paging_is_ready() || !syscall_user_mode_is_enabled() ||
        fs_get_type() == FS_TYPE_NONE) {
        LOG_WARN("APP_LOADER", "Dependencias ausentes para carregador ZAPP");
        return ERR_UNAVAILABLE;
    }

    app_loader_ready = 1;
    LOG_INFO("APP_LOADER", "Carregador de aplicativos inicializado");
    return OK;
}

int app_loader_is_ready(void) {
    return app_loader_ready;
}

int app_loader_is_foreground_active(void) {
    return app_loader_is_busy();
}

uint32_t app_loader_get_foreground_pid(void) {
    if (app_loader_active_pid != 0) return app_loader_active_pid;
    return app_loader_pending_pid;
}

int app_loader_run_image(const char* name, const uint8_t* image,
                         uint32_t size, const app_launch_info_t* launch,
                         uint32_t* pid_out) {
    app_image_header_t header;
    app_launch_info_t prepared_launch;
    uint32_t created_pid = 0;
    int result;

    if (!app_loader_ready) {
        LOG_WARN("APP_LOADER", "Tentativa de executar imagem indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (app_loader_is_busy()) {
        LOG_WARN("APP_LOADER", "Aplicativo ZAPP anterior ainda esta ativo");
        return ERR_STATE;
    }
    if (!name || !image) {
        LOG_ERROR("APP_LOADER", "Nome ou imagem ZAPP nulo");
        return ERR_NULL;
    }
    if (kstrlen(name) == 0) {
        LOG_ERROR("APP_LOADER", "Nome ZAPP vazio");
        return ERR_INVALID;
    }

    result = app_loader_prepare_launch(launch, &prepared_launch);
    if (result != OK) return result;
    result = app_loader_validate_image(image, size, &header);
    if (result != OK) return result;

    result = process_create_user_image_suspended_with_launch(
        name, image + header.code_offset, header.code_size,
        image + header.data_offset, header.data_size, header.entry_offset,
        header.stack_size, &prepared_launch, &created_pid);
    if (result != OK) {
        LOG_WARN("APP_LOADER", "Falha controlada ao criar processo ZAPP");
        return result;
    }

    app_loader_pending_pid = created_pid;
    if (pid_out) *pid_out = created_pid;
    LOG_DEBUG("APP_LOADER", "Processo ZAPP preparado para execucao assincrona");
    return OK;
}

int app_loader_run_file_with_launch(const char* path,
                                    const app_launch_info_t* launch,
                                    uint32_t* pid_out) {
    uint8_t* image;
    app_launch_info_t prepared_launch;
    uint32_t read_size;
    int file_size;
    int result;

    if (!app_loader_ready) {
        LOG_WARN("APP_LOADER", "Tentativa de executar ZAPP indisponivel");
        return ERR_UNAVAILABLE;
    }
    if (app_loader_is_busy()) {
        LOG_WARN("APP_LOADER", "Aplicativo ZAPP anterior ainda esta ativo");
        return ERR_STATE;
    }
    if (!path) {
        LOG_ERROR("APP_LOADER", "Caminho ZAPP nulo");
        return ERR_NULL;
    }
    if (kstrlen(path) == 0) {
        LOG_ERROR("APP_LOADER", "Caminho ZAPP vazio");
        return ERR_INVALID;
    }
    if (kstrlen(path) >= FS_MAX_PATH) {
        LOG_ERROR("APP_LOADER", "Caminho ZAPP excede o limite");
        return ERR_OVERFLOW;
    }
    if (!app_loader_has_zap_extension(path)) {
        LOG_ERROR("APP_LOADER", "Arquivo nao possui extensao .ZAP");
        return ERR_INVALID;
    }
    result = app_loader_prepare_launch(launch, &prepared_launch);
    if (result != OK) return result;

    image = (uint8_t*)kmalloc(APP_IMAGE_MAX_FILE_SIZE + 1U);
    if (!image) {
        LOG_ERROR("APP_LOADER", "Falha ao alocar buffer da imagem ZAPP");
        return ERR_MEM;
    }

    file_size = fs_read_file(path, image, APP_IMAGE_MAX_FILE_SIZE + 1U);
    if (file_size < 0) {
        kfree(image);
        LOG_WARN("APP_LOADER", "Falha ao ler arquivo ZAPP");
        return fs_get_type() == FS_TYPE_NONE ? ERR_UNAVAILABLE : ERR_NOT_FOUND;
    }
    read_size = (uint32_t)file_size;
    if (read_size > APP_IMAGE_MAX_FILE_SIZE) {
        kfree(image);
        LOG_WARN("APP_LOADER", "Arquivo ZAPP excede o tamanho suportado");
        return ERR_OVERFLOW;
    }

    result = app_loader_run_image(path, image, read_size, &prepared_launch,
                                  pid_out);

    kfree(image);
    image = 0;
    return result;
}

int app_loader_run_file(const char* path, uint32_t* pid_out) {
    return app_loader_run_file_with_launch(path, 0, pid_out);
}

int app_loader_reap_finished(void) {
    int result;

    result = app_loader_start_pending();
    if (result != OK) return result;
    result = app_loader_reap_active();
    if (result != OK) return result;

    return process_reap_finished_user();
}

int app_loader_cancel_foreground(uint32_t exit_code) {
    uint32_t pid = app_loader_get_foreground_pid();
    process_t* proc;
    int result;

    if (pid == 0) return ERR_NOT_FOUND;

    proc = process_get_by_pid(pid);
    if (proc && proc->state == PROCESS_STATE_ZOMBIE) return OK;

    result = process_cancel_user(pid, exit_code);
    if (result != OK) {
        LOG_WARN("APP_LOADER", "Falha ao cancelar aplicativo em primeiro plano");
        return result;
    }

    if (app_loader_pending_pid == pid) {
        app_loader_pending_pid = 0;
        app_loader_release_process_resources(pid);
        if (proc) process_destroy(proc);
        app_loader_store_result(pid, exit_code, 0,
                                exit_code == APP_EXIT_CANCELLED, 0, 0);
    }
    LOG_DEBUG("APP_LOADER", "Aplicativo em primeiro plano cancelado");
    return OK;
}

int app_loader_take_finished_result(app_loader_result_t* result) {
    if (!result) {
        LOG_ERROR("APP_LOADER", "Destino nulo para resultado do aplicativo");
        return ERR_NULL;
    }
    if (!app_loader_result_pending) return ERR_NOT_FOUND;

    *result = app_loader_finished_result;
    kmemset(&app_loader_finished_result, 0, sizeof(app_loader_finished_result));
    app_loader_result_pending = 0;
    return OK;
}
