#include "apps/shell_pipeline.h"
#include "apps/shell.h"
#include "apps/shell_dispatch.h"
#include "core/app_api.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/video.h"
#include "fs/vfs.h"
#include "process/thread.h"

#define SHELL_PIPELINE_MAX_COMMANDS 4U
#define SHELL_PIPELINE_MAX_INTERMEDIATE_PIPES 3U
#define SHELL_PIPELINE_TEST_SIZE (VFS_PIPE_BUFFER_SIZE + 128U)
#define SHELL_PIPELINE_TEST_CHUNK 257U
#define SHELL_PIPELINE_GREP_LINE_SIZE 256U

typedef struct {
    char command[SHELL_BUFFER_SIZE];
    int32_t input_fd;
    int32_t output_fd;
    thread_t* thread;
} shell_pipeline_stage_t;

typedef struct {
    uint8_t active;
    uint8_t redirect;
    uint8_t append;
    uint8_t stage_count;
    char commands[SHELL_PIPELINE_MAX_COMMANDS][SHELL_BUFFER_SIZE];
    char redirect_path[VFS_MAX_PATH];
    int32_t pipe_fds[SHELL_PIPELINE_MAX_INTERMEDIATE_PIPES][2];
    int32_t redirect_fds[2];
    shell_pipeline_stage_t stages[SHELL_PIPELINE_MAX_COMMANDS];
    thread_t* sink_thread;
    int32_t sink_fd;
    int result;
} shell_pipeline_context_t;

typedef struct {
    int32_t fds[2];
    thread_t* producer;
    thread_t* consumer;
    uint32_t consumed;
    int result;
    uint8_t payload[SHELL_PIPELINE_TEST_SIZE];
} shell_pipeline_test_context_t;

static shell_pipeline_context_t shell_pipeline_context;
static uint8_t shell_pipeline_redirect_buffer[VFS_REDIRECT_MAX_SIZE];
static shell_pipeline_test_context_t shell_pipeline_test_context;

static void shell_pipeline_copy_text(char* destination, uint32_t capacity,
                                     const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    while (source && source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int shell_pipeline_has_operator(const char* input) {
    uint32_t index = 0U;

    while (input[index]) {
        if (input[index] == '|' || input[index] == '>') return 1;
        index++;
    }
    return 0;
}

static int shell_pipeline_is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int shell_pipeline_copy_segment(const char* input, uint32_t start,
                                       uint32_t end, char* output) {
    uint32_t length;

    while (start < end && shell_pipeline_is_space(input[start])) start++;
    while (end > start && shell_pipeline_is_space(input[end - 1U])) end--;
    if (start == end) {
        LOG_WARN("SHELL", "Segmento vazio no pipeline");
        return ERR_INVALID;
    }
    length = end - start;
    if (length >= SHELL_BUFFER_SIZE) {
        LOG_WARN("SHELL", "Segmento do pipeline excede o limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < length; index++) {
        output[index] = input[start + index];
    }
    output[length] = '\0';
    return OK;
}

static int shell_pipeline_copy_target(const char* input, uint32_t start,
                                      uint32_t end, char* output) {
    uint32_t length;

    while (start < end && shell_pipeline_is_space(input[start])) start++;
    while (end > start && shell_pipeline_is_space(input[end - 1U])) end--;
    if (start == end) {
        LOG_WARN("SHELL", "Destino vazio no redirecionamento");
        return ERR_INVALID;
    }
    for (uint32_t index = start; index < end; index++) {
        if (shell_pipeline_is_space(input[index])) {
            LOG_WARN("SHELL", "Destino do redirecionamento contem espacos");
            return ERR_INVALID;
        }
    }
    length = end - start;
    if (length >= VFS_MAX_PATH) {
        LOG_WARN("SHELL", "Destino do redirecionamento excede o limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < length; index++) {
        output[index] = input[start + index];
    }
    output[length] = '\0';
    return OK;
}

static int shell_pipeline_parse(const char* input) {
    uint32_t length = kstrlen(input);
    uint32_t start = 0U;
    uint32_t index = 0U;
    uint32_t target_start;

    shell_pipeline_context.stage_count = 0U;
    shell_pipeline_context.redirect = 0U;
    shell_pipeline_context.append = 0U;
    shell_pipeline_context.redirect_path[0] = '\0';
    while (index < length) {
        if (input[index] == '|') {
            if (shell_pipeline_context.redirect ||
                shell_pipeline_context.stage_count >=
                    SHELL_PIPELINE_MAX_COMMANDS) {
                LOG_WARN("SHELL", "Operador de pipeline invalido");
                return ERR_INVALID;
            }
            if (shell_pipeline_copy_segment(
                    input, start, index,
                    shell_pipeline_context.commands[
                        shell_pipeline_context.stage_count]) != OK) {
                LOG_WARN("SHELL", "Estagio de pipeline invalido");
                return ERR_INVALID;
            }
            shell_pipeline_context.stage_count++;
            start = index + 1U;
            index++;
            continue;
        }
        if (input[index] == '>') {
            if (shell_pipeline_context.redirect ||
                shell_pipeline_context.stage_count >=
                    SHELL_PIPELINE_MAX_COMMANDS) {
                LOG_WARN("SHELL", "Redirecionamento duplicado ou estagio invalido");
                return ERR_INVALID;
            }
            if (shell_pipeline_copy_segment(
                    input, start, index,
                    shell_pipeline_context.commands[
                        shell_pipeline_context.stage_count]) != OK) {
                LOG_WARN("SHELL", "Estagio final do redirecionamento invalido");
                return ERR_INVALID;
            }
            shell_pipeline_context.stage_count++;
            shell_pipeline_context.redirect = 1U;
            index++;
            if (input[index] == '>') {
                shell_pipeline_context.append = 1U;
                index++;
            }
            target_start = index;
            while (index < length && input[index] != '|' &&
                   input[index] != '>') index++;
            if (index < length) {
                LOG_WARN("SHELL", "Operador apos destino de redirecionamento");
                return ERR_INVALID;
            }
            if (shell_pipeline_copy_target(input, target_start, length,
                                           shell_pipeline_context.redirect_path) != OK) {
                LOG_WARN("SHELL", "Destino de redirecionamento invalido");
                return ERR_INVALID;
            }
            break;
        }
        index++;
    }
    if (!shell_pipeline_context.redirect) {
        if (shell_pipeline_context.stage_count >=
            SHELL_PIPELINE_MAX_COMMANDS) {
            LOG_WARN("SHELL", "Quantidade de estagios invalida no pipeline");
            return ERR_INVALID;
        }
        if (shell_pipeline_copy_segment(
                input, start, length,
                shell_pipeline_context.commands[
                    shell_pipeline_context.stage_count]) != OK) {
            LOG_WARN("SHELL", "Ultimo estagio de pipeline invalido");
            return ERR_INVALID;
        }
        shell_pipeline_context.stage_count++;
    }
    return OK;
}

static int shell_pipeline_command_supported(const char* command,
                                            uint32_t stage_index) {
    char name[32];
    uint32_t length = 0U;

    while (command[length] && !shell_pipeline_is_space(command[length])) {
        if (length + 1U >= sizeof(name)) {
            LOG_WARN("SHELL", "Nome de comando do pipeline excede o limite");
            return ERR_OVERFLOW;
        }
        name[length] = command[length];
        length++;
    }
    if (!length) {
        LOG_WARN("SHELL", "Comando vazio no pipeline");
        return ERR_INVALID;
    }
    name[length] = '\0';
    if (kstrcmp(name, "grep") == 0) {
        if (stage_index == 0U) {
            LOG_WARN("SHELL", "grep precisa consumir a saida de outro estagio");
            return ERR_INVALID;
        }
        return OK;
    }
    if (kstrcmp(name, "echo") == 0 || kstrcmp(name, "ls") == 0 ||
        kstrcmp(name, "cat") == 0 || kstrcmp(name, "procs") == 0) {
        return OK;
    }
    LOG_WARN("SHELL", "Comando nao suportado no pipeline");
    return ERR_NOT_FOUND;
}

static void shell_pipeline_reset(void) {
    kmemset(&shell_pipeline_context, 0, sizeof(shell_pipeline_context));
    for (uint32_t index = 0U; index < SHELL_PIPELINE_MAX_INTERMEDIATE_PIPES;
         index++) {
        shell_pipeline_context.pipe_fds[index][0] = VFS_FD_INVALID;
        shell_pipeline_context.pipe_fds[index][1] = VFS_FD_INVALID;
    }
    shell_pipeline_context.redirect_fds[0] = VFS_FD_INVALID;
    shell_pipeline_context.redirect_fds[1] = VFS_FD_INVALID;
    shell_pipeline_context.sink_fd = VFS_FD_INVALID;
    for (uint32_t index = 0U; index < SHELL_PIPELINE_MAX_COMMANDS; index++) {
        shell_pipeline_context.stages[index].input_fd = VFS_FD_INVALID;
        shell_pipeline_context.stages[index].output_fd = VFS_FD_INVALID;
    }
}

static void shell_pipeline_close_fd(int32_t* fd) {
    int result;

    if (!fd || *fd == VFS_FD_INVALID) return;
    result = vfs_close(*fd);
    if (result != OK && result != ERR_INVALID) {
        LOG_ERROR("SHELL", "Falha ao fechar descritor do pipeline");
    }
    *fd = VFS_FD_INVALID;
}

static void shell_pipeline_close_resources(void) {
    for (uint32_t index = 0U; index < SHELL_PIPELINE_MAX_INTERMEDIATE_PIPES;
         index++) {
        shell_pipeline_close_fd(&shell_pipeline_context.pipe_fds[index][0]);
        shell_pipeline_close_fd(&shell_pipeline_context.pipe_fds[index][1]);
    }
    shell_pipeline_close_fd(&shell_pipeline_context.redirect_fds[0]);
    shell_pipeline_close_fd(&shell_pipeline_context.redirect_fds[1]);
    for (uint32_t index = 0U; index < SHELL_PIPELINE_MAX_COMMANDS; index++) {
        shell_pipeline_close_fd(&shell_pipeline_context.stages[index].input_fd);
        shell_pipeline_close_fd(&shell_pipeline_context.stages[index].output_fd);
    }
    shell_pipeline_close_fd(&shell_pipeline_context.sink_fd);
}

static int shell_pipeline_create_resources(void) {
    int result;

    for (uint32_t index = 0U; index + 1U <
         shell_pipeline_context.stage_count; index++) {
        result = vfs_pipe(shell_pipeline_context.pipe_fds[index]);
        if (result != OK) return result;
    }
    if (shell_pipeline_context.redirect) {
        result = vfs_pipe(shell_pipeline_context.redirect_fds);
        if (result != OK) return result;
    }
    for (uint32_t index = 0U; index < shell_pipeline_context.stage_count;
         index++) {
        shell_pipeline_stage_t* stage = &shell_pipeline_context.stages[index];

        if (index > 0U) {
            stage->input_fd = shell_pipeline_context.pipe_fds[index - 1U][0];
            shell_pipeline_context.pipe_fds[index - 1U][0] = VFS_FD_INVALID;
        }
        if (index + 1U < shell_pipeline_context.stage_count) {
            stage->output_fd = shell_pipeline_context.pipe_fds[index][1];
            shell_pipeline_context.pipe_fds[index][1] = VFS_FD_INVALID;
        } else if (shell_pipeline_context.redirect) {
            stage->output_fd = shell_pipeline_context.redirect_fds[1];
            shell_pipeline_context.redirect_fds[1] = VFS_FD_INVALID;
            shell_pipeline_context.sink_fd =
                shell_pipeline_context.redirect_fds[0];
            shell_pipeline_context.redirect_fds[0] = VFS_FD_INVALID;
        }
    }
    return OK;
}

static shell_pipeline_stage_t* shell_pipeline_current_stage(void) {
    thread_t* current = thread_get_current();

    if (!current) return 0;
    for (uint32_t index = 0U; index < shell_pipeline_context.stage_count;
         index++) {
        if (shell_pipeline_context.stages[index].thread == current) {
            return &shell_pipeline_context.stages[index];
        }
    }
    return 0;
}

static void shell_pipeline_record_error(int result) {
    if (result != OK && shell_pipeline_context.result == OK) {
        shell_pipeline_context.result = result;
    }
}

int shell_pipeline_is_active(void) {
    return shell_pipeline_context.active != 0U;
}

int shell_pipeline_write(const char* text, uint8_t color) {
    shell_pipeline_stage_t* stage;
    uint32_t length;
    uint32_t offset = 0U;
    uint32_t chunk_size;
    uint32_t bytes_written;
    int result;

    if (!text) {
        LOG_ERROR("SHELL", "Saida do shell recebeu texto nulo");
        return ERR_NULL;
    }
    if (!shell_pipeline_context.active) {
        video_print(text, color);
        return OK;
    }
    stage = shell_pipeline_current_stage();
    if (!stage || stage->output_fd == VFS_FD_INVALID) {
        video_print(text, color);
        return OK;
    }
    length = kstrlen(text);
    while (offset < length) {
        chunk_size = length - offset;
        if (chunk_size > APP_API_MAX_FILE_IO_SIZE) {
            chunk_size = APP_API_MAX_FILE_IO_SIZE;
        }
        bytes_written = 0U;
        result = vfs_write(stage->output_fd, text + offset, chunk_size,
                           &bytes_written);
        if (result != OK) {
            shell_pipeline_record_error(result);
            return result;
        }
        if (!bytes_written) {
            shell_pipeline_record_error(ERR_UNAVAILABLE);
            return ERR_UNAVAILABLE;
        }
        offset += bytes_written;
    }
    return OK;
}

void shell_pipeline_print_num(uint32_t value) {
    char buffer[16];
    uint32_t index = 0U;

    if (value == 0U) {
        buffer[index++] = '0';
    } else {
        char temporary[16];
        uint32_t temporary_index = 0U;

        while (value > 0U) {
            temporary[temporary_index++] = (char)('0' + value % 10U);
            value /= 10U;
        }
        while (temporary_index > 0U) {
            buffer[index++] = temporary[--temporary_index];
        }
    }
    buffer[index] = '\0';
    (void)shell_pipeline_write(buffer, 0x07);
}

int shell_pipeline_read(void* buffer, uint32_t size, uint32_t* bytes_read) {
    shell_pipeline_stage_t* stage;
    int result;

    if (!bytes_read || (size > 0U && !buffer)) {
        LOG_ERROR("SHELL", "Entrada do shell recebeu buffer invalido");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    if (!shell_pipeline_context.active) {
        LOG_WARN("SHELL", "Leitura solicitada fora de pipeline");
        return ERR_UNAVAILABLE;
    }
    stage = shell_pipeline_current_stage();
    if (!stage || stage->input_fd == VFS_FD_INVALID) {
        LOG_WARN("SHELL", "Comando sem entrada de pipe");
        return ERR_UNAVAILABLE;
    }
    result = vfs_read(stage->input_fd, buffer, size, bytes_read);
    if (result != OK) shell_pipeline_record_error(result);
    return result;
}

static void shell_pipeline_stage_entry(void) {
    shell_pipeline_stage_t* stage = shell_pipeline_current_stage();
    int result;

    if (!stage) {
        shell_pipeline_record_error(ERR_STATE);
        LOG_ERROR("SHELL", "Worker de pipeline sem estagio associado");
        return;
    }
    result = shell_dispatch_execute(stage->command);
    if (result != OK) shell_pipeline_record_error(result);
    shell_pipeline_close_fd(&stage->output_fd);
    shell_pipeline_close_fd(&stage->input_fd);
}

static void shell_pipeline_sink_entry(void) {
    uint32_t total = 0U;
    uint32_t request;
    uint32_t bytes_read;
    uint8_t extra = 0U;
    int result = OK;

    while (1) {
        if (total < VFS_REDIRECT_MAX_SIZE) {
            request = VFS_REDIRECT_MAX_SIZE - total;
            if (request > APP_API_MAX_FILE_IO_SIZE) {
                request = APP_API_MAX_FILE_IO_SIZE;
            }
        } else {
            request = 1U;
        }
        bytes_read = 0U;
        result = vfs_read(shell_pipeline_context.sink_fd,
                          total < VFS_REDIRECT_MAX_SIZE ?
                              shell_pipeline_redirect_buffer + total : &extra,
                          request, &bytes_read);
        if (result != OK || bytes_read == 0U) break;
        if (total >= VFS_REDIRECT_MAX_SIZE) {
            result = ERR_OVERFLOW;
            break;
        }
        total += bytes_read;
    }
    shell_pipeline_close_fd(&shell_pipeline_context.sink_fd);
    if (result != OK) shell_pipeline_record_error(result);
    if (result == OK && shell_pipeline_context.result == OK) {
        result = vfs_write_redirect(shell_pipeline_context.redirect_path,
                                    shell_pipeline_redirect_buffer, total,
                                    shell_pipeline_context.append);
        if (result != OK) shell_pipeline_record_error(result);
    }
}

static int shell_pipeline_all_finished(void) {
    for (uint32_t index = 0U; index < shell_pipeline_context.stage_count;
         index++) {
        if (shell_pipeline_context.stages[index].thread &&
            shell_pipeline_context.stages[index].thread->state !=
                THREAD_FINISHED) return 0;
    }
    return !shell_pipeline_context.sink_thread ||
           shell_pipeline_context.sink_thread->state == THREAD_FINISHED;
}

static void shell_pipeline_destroy_threads(void) {
    for (uint32_t index = 0U; index < shell_pipeline_context.stage_count;
         index++) {
        if (shell_pipeline_context.stages[index].thread) {
            thread_destroy(shell_pipeline_context.stages[index].thread);
            shell_pipeline_context.stages[index].thread = 0;
        }
    }
    if (shell_pipeline_context.sink_thread) {
        thread_destroy(shell_pipeline_context.sink_thread);
        shell_pipeline_context.sink_thread = 0;
    }
}

static int shell_pipeline_reject(const char* message, int result) {
    LOG_WARN("SHELL", message);
    video_print("Erro: ", 0x0C);
    video_print(message, 0x0C);
    video_print("\n", 0x0C);
    return result;
}

int shell_pipeline_try_execute(const char* input, uint8_t* handled) {
    int result;

    if (!handled) {
        LOG_ERROR("SHELL", "Marcador de pipeline nulo");
        return ERR_NULL;
    }
    *handled = 0U;
    if (!input) {
        LOG_ERROR("SHELL", "Entrada de pipeline nula");
        return ERR_NULL;
    }
    if (!kstrlen(input)) return OK;
    if (!shell_pipeline_has_operator(input)) return OK;
    *handled = 1U;
    if (shell_pipeline_context.active) {
        return shell_pipeline_reject("Pipeline reentrante recusado", ERR_STATE);
    }
    shell_pipeline_reset();
    result = shell_pipeline_parse(input);
    if (result != OK) {
        return shell_pipeline_reject("Sintaxe de pipeline invalida", result);
    }
    for (uint32_t index = 0U; index < shell_pipeline_context.stage_count;
         index++) {
        result = shell_pipeline_command_supported(
            shell_pipeline_context.commands[index], index);
        if (result != OK) {
            return shell_pipeline_reject("Comando nao suportado no pipeline",
                                         result);
        }
        shell_pipeline_copy_text(shell_pipeline_context.stages[index].command,
                                 SHELL_BUFFER_SIZE,
                                 shell_pipeline_context.commands[index]);
    }
    result = shell_pipeline_create_resources();
    if (result != OK) {
        shell_pipeline_close_resources();
        return shell_pipeline_reject("Falha ao criar pipes do pipeline", result);
    }
    shell_pipeline_context.active = 1U;
    shell_pipeline_context.result = OK;
    for (uint32_t index = 0U; index < shell_pipeline_context.stage_count;
         index++) {
        shell_pipeline_context.stages[index].thread =
            thread_create("ShellPipe", shell_pipeline_stage_entry);
        if (!shell_pipeline_context.stages[index].thread) {
            shell_pipeline_context.result = ERR_MEM;
            shell_pipeline_destroy_threads();
            shell_pipeline_close_resources();
            shell_pipeline_context.active = 0U;
            return shell_pipeline_reject("Falha ao criar worker do pipeline",
                                         ERR_MEM);
        }
    }
    if (shell_pipeline_context.redirect) {
        shell_pipeline_context.sink_thread =
            thread_create("ShellPipeSink", shell_pipeline_sink_entry);
        if (!shell_pipeline_context.sink_thread) {
            shell_pipeline_context.result = ERR_MEM;
            shell_pipeline_destroy_threads();
            shell_pipeline_close_resources();
            shell_pipeline_context.active = 0U;
            return shell_pipeline_reject("Falha ao criar consumidor do pipe",
                                         ERR_MEM);
        }
    }
    while (!shell_pipeline_all_finished()) thread_yield();
    result = shell_pipeline_context.result;
    shell_pipeline_destroy_threads();
    shell_pipeline_close_resources();
    shell_pipeline_context.active = 0U;
    if (result != OK) {
        video_print("Erro: pipeline falhou (codigo ", 0x0C);
        shell_pipeline_print_num((uint32_t)result);
        video_print(").\n", 0x0C);
    }
    return result;
}

static void shell_pipeline_test_producer(void) {
    uint32_t offset = 0U;
    uint32_t request;
    uint32_t written;
    int result;

    while (offset < SHELL_PIPELINE_TEST_SIZE) {
        request = SHELL_PIPELINE_TEST_SIZE - offset;
        if (request > SHELL_PIPELINE_TEST_CHUNK) request = SHELL_PIPELINE_TEST_CHUNK;
        written = 0U;
        result = vfs_write(shell_pipeline_test_context.fds[1],
                           shell_pipeline_test_context.payload + offset,
                           request, &written);
        if (result != OK || !written) {
            shell_pipeline_test_context.result = result == OK ?
                                                 ERR_UNAVAILABLE : result;
            break;
        }
        offset += written;
    }
    shell_pipeline_close_fd(&shell_pipeline_test_context.fds[1]);
}

static void shell_pipeline_test_consumer(void) {
    uint8_t buffer[SHELL_PIPELINE_TEST_CHUNK];
    uint32_t request;
    uint32_t bytes_read;
    int result;

    while (shell_pipeline_test_context.consumed < SHELL_PIPELINE_TEST_SIZE) {
        request = SHELL_PIPELINE_TEST_SIZE -
                  shell_pipeline_test_context.consumed;
        if (request > sizeof(buffer)) request = sizeof(buffer);
        bytes_read = 0U;
        result = vfs_read(shell_pipeline_test_context.fds[0], buffer, request,
                          &bytes_read);
        if (result != OK || !bytes_read) {
            if (result != OK) shell_pipeline_test_context.result = result;
            break;
        }
        for (uint32_t index = 0U; index < bytes_read; index++) {
            if (buffer[index] != shell_pipeline_test_context.payload[
                                    shell_pipeline_test_context.consumed + index]) {
                shell_pipeline_test_context.result = ERR_STATE;
                break;
            }
        }
        shell_pipeline_test_context.consumed += bytes_read;
        if (shell_pipeline_test_context.result != OK) break;
    }
    shell_pipeline_close_fd(&shell_pipeline_test_context.fds[0]);
}

int shell_pipeline_self_test(void) {
    int result;

    if (shell_pipeline_context.active) {
        LOG_WARN("SHELL", "Pipetest solicitado durante pipeline ativo");
        return ERR_STATE;
    }
    kmemset(&shell_pipeline_test_context, 0,
            sizeof(shell_pipeline_test_context));
    shell_pipeline_test_context.fds[0] = VFS_FD_INVALID;
    shell_pipeline_test_context.fds[1] = VFS_FD_INVALID;
    shell_pipeline_test_context.result = OK;
    for (uint32_t index = 0U; index < SHELL_PIPELINE_TEST_SIZE; index++) {
        shell_pipeline_test_context.payload[index] =
            (uint8_t)('A' + index % 26U);
    }
    result = vfs_pipe(shell_pipeline_test_context.fds);
    if (result != OK) return result;
    shell_pipeline_test_context.producer =
        thread_create("PipeTestWriter", shell_pipeline_test_producer);
    shell_pipeline_test_context.consumer =
        thread_create("PipeTestReader", shell_pipeline_test_consumer);
    if (!shell_pipeline_test_context.producer ||
        !shell_pipeline_test_context.consumer) {
        if (shell_pipeline_test_context.producer) {
            thread_destroy(shell_pipeline_test_context.producer);
        }
        if (shell_pipeline_test_context.consumer) {
            thread_destroy(shell_pipeline_test_context.consumer);
        }
        shell_pipeline_close_fd(&shell_pipeline_test_context.fds[0]);
        shell_pipeline_close_fd(&shell_pipeline_test_context.fds[1]);
        return ERR_MEM;
    }
    while (shell_pipeline_test_context.producer->state != THREAD_FINISHED ||
           shell_pipeline_test_context.consumer->state != THREAD_FINISHED) {
        thread_yield();
    }
    result = shell_pipeline_test_context.result;
    if (shell_pipeline_test_context.consumed != SHELL_PIPELINE_TEST_SIZE) {
        result = ERR_STATE;
    }
    shell_pipeline_close_fd(&shell_pipeline_test_context.fds[0]);
    shell_pipeline_close_fd(&shell_pipeline_test_context.fds[1]);
    thread_destroy(shell_pipeline_test_context.producer);
    thread_destroy(shell_pipeline_test_context.consumer);
    if (result == OK) result = vfs_validate_state();
    if (result == OK) result = wait_validate_state();
    if (result != OK) LOG_ERROR("SHELL", "Pipetest detectou falha");
    return result;
}
