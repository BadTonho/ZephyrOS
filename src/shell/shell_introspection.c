#include "apps/shell_introspection.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "fs/vfs.h"

static int shell_introspection_ascii_valid(const uint8_t* buffer,
                                           uint32_t size) {
    for (uint32_t index = 0U; index < size; index++) {
        if (buffer[index] == 0U || buffer[index] == '\r' ||
            buffer[index] == 0x1BU || buffer[index] > 0x7FU) {
            LOG_WARN("SHELL", "Snapshot textual fora do contrato ASCII");
            return ERR_INVALID;
        }
    }
    return OK;
}

int shell_introspection_read_file(const char* path, uint8_t* buffer,
                                  uint32_t capacity, uint32_t* out_size) {
    int32_t fd = VFS_FD_INVALID;
    uint32_t total = 0U;
    uint32_t bytes = 0U;
    uint8_t probe = 0U;
    int result;

    if (!path || !buffer || !capacity || !out_size) {
        LOG_ERROR("SHELL", "Argumento invalido ao ler introspeccao");
        return ERR_NULL;
    }
    *out_size = 0U;
    result = vfs_open(path, VFS_MODE_READ, &fd);
    if (result != OK) return result;
    do {
        result = vfs_read(fd, buffer + total, capacity - total, &bytes);
        if (result != OK) break;
        total += bytes;
        if (total == capacity) {
            result = vfs_read(fd, &probe, 1U, &bytes);
            if (result != OK) break;
            if (bytes) {
                result = ERR_OVERFLOW;
                LOG_WARN("SHELL", "Snapshot de introspeccao excedeu buffer");
                break;
            }
        }
    } while (bytes);
    if (vfs_close(fd) != OK && result == OK) {
        result = ERR_STATE;
        LOG_ERROR("SHELL", "Falha ao fechar snapshot de introspeccao");
    }
    if (result != OK) return result;
    result = shell_introspection_ascii_valid(buffer, total);
    if (result != OK) return result;
    *out_size = total;
    return OK;
}

static int shell_introspection_key_matches(const uint8_t* buffer,
                                           uint32_t start, uint32_t length,
                                           const char* key) {
    uint32_t key_length;

    if (!buffer || !key) return 0;
    key_length = kstrlen(key);
    if (length != key_length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        if (buffer[start + index] != (uint8_t)key[index]) return 0;
    }
    return 1;
}

int shell_introspection_find_value(const uint8_t* buffer, uint32_t size,
                                   const char* key, char* value,
                                   uint32_t value_capacity) {
    uint32_t line_start = 0U;

    if (!buffer || !key || !value || value_capacity == 0U) {
        LOG_ERROR("SHELL", "Argumento invalido ao procurar atributo");
        return ERR_NULL;
    }
    value[0] = '\0';
    while (line_start < size) {
        uint32_t line_end = line_start;
        uint32_t separator = line_start;

        while (line_end < size && buffer[line_end] != '\n') line_end++;
        if (line_end == size) {
            LOG_WARN("SHELL", "Linha textual sem terminador LF");
            return ERR_INVALID;
        }
        while (separator < line_end && buffer[separator] != ' ') separator++;
        if (separator == line_start || separator == line_end) {
            LOG_WARN("SHELL", "Linha textual sem chave e valor");
            return ERR_INVALID;
        }
        if (shell_introspection_key_matches(buffer, line_start,
                                            separator - line_start, key)) {
            uint32_t value_length = line_end - separator - 1U;

            if (value_length >= value_capacity) {
                LOG_WARN("SHELL", "Valor textual excedeu o destino");
                return ERR_OVERFLOW;
            }
            for (uint32_t index = 0U; index < value_length; index++) {
                value[index] = (char)buffer[separator + 1U + index];
            }
            value[value_length] = '\0';
            return OK;
        }
        line_start = line_end + 1U;
    }
    return ERR_NOT_FOUND;
}

int shell_introspection_parse_u32(const char* text, uint32_t* value) {
    uint32_t parsed = 0U;
    uint32_t index = 0U;

    if (!text || !value || !text[0]) {
        LOG_WARN("SHELL", "Numero decimal ausente na introspeccao");
        return ERR_INVALID;
    }
    while (text[index]) {
        uint32_t digit;

        if (text[index] < '0' || text[index] > '9') {
            LOG_WARN("SHELL", "Numero decimal invalido na introspeccao");
            return ERR_INVALID;
        }
        digit = (uint32_t)(text[index] - '0');
        if (parsed > (0xFFFFFFFFU - digit) / 10U) {
            LOG_WARN("SHELL", "Numero decimal excedeu uint32");
            return ERR_OVERFLOW;
        }
        parsed = parsed * 10U + digit;
        index++;
    }
    *value = parsed;
    return OK;
}

static int shell_introspection_hex_digit(char value, uint32_t* digit) {
    if (value >= '0' && value <= '9') *digit = (uint32_t)(value - '0');
    else if (value >= 'a' && value <= 'f') *digit = (uint32_t)(value - 'a' + 10);
    else if (value >= 'A' && value <= 'F') *digit = (uint32_t)(value - 'A' + 10);
    else {
        LOG_WARN("SHELL", "Digito hexadecimal ausente na introspeccao");
        return ERR_INVALID;
    }
    return OK;
}

int shell_introspection_parse_hex_u32(const char* text, uint32_t* value) {
    uint32_t parsed = 0U;
    uint32_t index = 2U;

    if (!text || !value || text[0] != '0' || text[1] != 'x' || !text[2]) {
        LOG_WARN("SHELL", "Numero hexadecimal invalido na introspeccao");
        return ERR_INVALID;
    }
    while (text[index]) {
        uint32_t digit;
        int result = shell_introspection_hex_digit(text[index], &digit);

        if (result != OK) {
            LOG_WARN("SHELL", "Digito hexadecimal invalido na introspeccao");
            return result;
        }
        if (parsed > (0xFFFFFFFFU - digit) / 16U) {
            LOG_WARN("SHELL", "Numero hexadecimal excedeu uint32");
            return ERR_OVERFLOW;
        }
        parsed = parsed * 16U + digit;
        index++;
    }
    *value = parsed;
    return OK;
}
