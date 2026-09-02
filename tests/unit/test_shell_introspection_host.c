#include <stdint.h>
#include <stdio.h>

#include "apps/shell_introspection.h"
#include "core/errors.h"
#include "core/log.h"
#include "fs/vfs.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:shell:introspection|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:introspection|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:introspection|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

int vfs_open(const char* path, uint32_t mode, int32_t* fd_out) {
    (void)path;
    (void)mode;
    (void)fd_out;
    return ERR_UNAVAILABLE;
}

int vfs_read(int32_t fd, void* buffer, uint32_t size, uint32_t* bytes_read) {
    (void)fd;
    (void)buffer;
    (void)size;
    (void)bytes_read;
    return ERR_UNAVAILABLE;
}

int vfs_close(int32_t fd) {
    (void)fd;
    return ERR_UNAVAILABLE;
}

static int check_valid_values(void) {
    uint32_t value = 0U;

    if (shell_introspection_parse_hex_u32("0x0", &value) != OK ||
        value != 0U) return 1;
    if (shell_introspection_parse_hex_u32("0x1234", &value) != OK ||
        value != 0x1234U) return 2;
    if (shell_introspection_parse_hex_u32("0xabcdef", &value) != OK ||
        value != 0xABCDEFU) return 3;
    if (shell_introspection_parse_hex_u32("0xABCDEF", &value) != OK ||
        value != 0xABCDEFU) return 4;
    if (shell_introspection_parse_hex_u32("0xFFFFFFFF", &value) != OK ||
        value != UINT32_MAX) return 5;
    return 0;
}

static int check_invalid_inputs(void) {
    uint32_t value = 0xA5A5A5A5U;

    if (shell_introspection_parse_hex_u32(NULL, &value) != ERR_INVALID) {
        return 1;
    }
    if (shell_introspection_parse_hex_u32("", &value) != ERR_INVALID) {
        return 2;
    }
    if (shell_introspection_parse_hex_u32("0X1", &value) != ERR_INVALID) {
        return 3;
    }
    if (shell_introspection_parse_hex_u32("0x", &value) != ERR_INVALID) {
        return 4;
    }
    if (shell_introspection_parse_hex_u32("0x12g", &value) != ERR_INVALID) {
        return 5;
    }
    if (shell_introspection_parse_hex_u32("0x1", NULL) != ERR_INVALID) {
        return 6;
    }
    if (value != 0xA5A5A5A5U) return 7;
    return 0;
}

static int check_overflow(void) {
    uint32_t value = 0x5A5A5A5AU;

    if (shell_introspection_parse_hex_u32("0x100000000", &value) !=
        ERR_OVERFLOW) return 1;
    if (value != 0x5A5A5A5AU) return 2;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    result = check_valid_values();
    if (result == 0) result = check_invalid_inputs();
    if (result == 0) result = check_overflow();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
