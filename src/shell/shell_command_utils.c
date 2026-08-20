#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"

void shell_command_uppercase(char* text) {
    while (*text) {
        if (*text >= 'a' && *text <= 'z') {
            *text -= 32;
        }
        text++;
    }
}

void shell_command_print_num(uint32_t value) {
    char buffer[16];
    int index = 0;

    if (value == 0) {
        buffer[index++] = '0';
    } else {
        char temporary[16];
        int temporary_index = 0;

        while (value > 0) {
            temporary[temporary_index++] = '0' + (value % 10);
            value /= 10;
        }
        while (temporary_index > 0) {
            buffer[index++] = temporary[--temporary_index];
        }
    }
    buffer[index] = '\0';
    video_print(buffer, 0x07);
}

uint32_t shell_command_parse_number(const char* text) {
    uint32_t value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value;
}

int shell_command_args_equal(const char* args, const char* expected) {
    if (!args || !expected) return 0;
    while (*args && *expected && *args == *expected) {
        args++;
        expected++;
    }
    if (*expected) return 0;
    while (*args == ' ' || *args == '\t') args++;
    return *args == '\0';
}

const char* shell_command_match_subcommand(const char* args,
                                           const char* expected) {
    if (!args || !expected) return 0;
    while (*args && *expected && *args == *expected) {
        args++;
        expected++;
    }
    if (*expected || (*args && *args != ' ' && *args != '\t')) return 0;
    while (*args == ' ' || *args == '\t') args++;
    return args;
}

int shell_command_read_single_arg(const char* args, char* output,
                                  uint32_t output_size) {
    uint32_t length = 0;

    if (!args || !output || output_size == 0) {
        LOG_ERROR("SHELL", "Destino invalido ao ler argumento");
        return ERR_NULL;
    }
    while (*args == ' ' || *args == '\t') args++;
    while (*args && *args != ' ' && *args != '\t') {
        if (length + 1U >= output_size) {
            LOG_WARN("SHELL", "Argumento do shell excede limite");
            return ERR_OVERFLOW;
        }
        output[length++] = *args++;
    }
    while (*args == ' ' || *args == '\t') args++;
    if (length == 0 || *args) {
        LOG_WARN("SHELL", "Argumento unico ausente ou invalido");
        return ERR_INVALID;
    }
    output[length] = '\0';
    return OK;
}

int shell_command_read_token(const char** cursor, char* output,
                             uint32_t output_size) {
    uint32_t length = 0;

    if (!cursor || !*cursor || !output || !output_size) {
        LOG_ERROR("SHELL", "Destino invalido ao ler token");
        return ERR_NULL;
    }
    while (**cursor == ' ' || **cursor == '\t') (*cursor)++;
    while (**cursor && **cursor != ' ' && **cursor != '\t') {
        if (length + 1U >= output_size) {
            LOG_WARN("SHELL", "Token do shell excede limite");
            return ERR_OVERFLOW;
        }
        output[length++] = **cursor;
        (*cursor)++;
    }
    if (!length) {
        LOG_WARN("SHELL", "Token ausente no comando");
        return ERR_INVALID;
    }
    output[length] = '\0';
    return OK;
}

int shell_command_read_two_args(const char* args, char* first,
                                uint32_t first_size, char* second,
                                uint32_t second_size) {
    const char* cursor = args;
    int result;

    if (!args || !first || !second) {
        LOG_ERROR("SHELL", "Argumentos nulos ao ler dois valores");
        return ERR_NULL;
    }
    result = shell_command_read_token(&cursor, first, first_size);
    if (result != OK) return result;
    result = shell_command_read_token(&cursor, second, second_size);
    if (result != OK) return result;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor) {
        LOG_WARN("SHELL", "Comando recebeu argumentos excedentes");
        return ERR_INVALID;
    }
    return OK;
}

int shell_command_read_four_args(const char* args, char* first,
                                 uint32_t first_size, char* second,
                                 uint32_t second_size, char* third,
                                 uint32_t third_size, char* fourth,
                                 uint32_t fourth_size) {
    const char* cursor = args;
    int result;

    if (!args || !first || !second || !third || !fourth) {
        LOG_ERROR("SHELL", "Argumentos nulos ao ler quatro valores");
        return ERR_NULL;
    }
    result = shell_command_read_token(&cursor, first, first_size);
    if (result == OK) {
        result = shell_command_read_token(&cursor, second, second_size);
    }
    if (result == OK) {
        result = shell_command_read_token(&cursor, third, third_size);
    }
    if (result == OK) {
        result = shell_command_read_token(&cursor, fourth, fourth_size);
    }
    if (result != OK) return result;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor) {
        LOG_WARN("SHELL", "Comando recebeu argumentos excedentes");
        return ERR_INVALID;
    }
    return OK;
}

void shell_command_print_hex(uint32_t value, uint32_t digits) {
    static const char hex[] = "0123456789ABCDEF";

    while (digits > 0) {
        uint32_t shift = (digits - 1U) * 4U;
        char value_char[2];

        value_char[0] = hex[(value >> shift) & 0x0FU];
        value_char[1] = '\0';
        video_print(value_char, 0x07);
        digits--;
    }
}
