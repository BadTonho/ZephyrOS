#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/syscall.h"
#include "memory/paging.h"

#define APP_BUILTIN_MAX_CODE_SIZE 128U
#define APP_BUILTIN_OPCODE_MOV_IMM_BASE 0xB8U
#define APP_BUILTIN_OPCODE_MOV_REG_MEM  0x8BU
#define APP_BUILTIN_OPCODE_TEST_REG     0x85U
#define APP_BUILTIN_OPCODE_JZ           0x74U
#define APP_BUILTIN_OPCODE_INT          0xCDU
#define APP_BUILTIN_INTERRUPT_SYSCALL   0x80U
#define APP_BUILTIN_REG_EAX 0U
#define APP_BUILTIN_REG_ECX 1U
#define APP_BUILTIN_REG_EBX 3U
#define APP_BUILTIN_ECX_ABSOLUTE_MODRM 0x0DU
#define APP_BUILTIN_TEST_ECX_ECX 0xC9U

static uint8_t app_builtin_image[APP_IMAGE_MAX_FILE_SIZE];
static const char app_builtin_argtest_data[] = "Argumentos ZAPP: \n";

#define APP_BUILTIN_ARGTEST_PREFIX_LENGTH \
    ((uint32_t)(sizeof(app_builtin_argtest_data) - 2U))
#define APP_BUILTIN_ARGTEST_NEWLINE_OFFSET APP_BUILTIN_ARGTEST_PREFIX_LENGTH

static void app_builtin_write_u32(uint8_t* code, uint32_t offset,
                                  uint32_t value) {
    code[offset] = (uint8_t)(value & 0xFFU);
    code[offset + 1U] = (uint8_t)((value >> 8U) & 0xFFU);
    code[offset + 2U] = (uint8_t)((value >> 16U) & 0xFFU);
    code[offset + 3U] = (uint8_t)((value >> 24U) & 0xFFU);
}

static int app_builtin_require_space(uint32_t offset, uint32_t size) {
    if (offset > APP_BUILTIN_MAX_CODE_SIZE ||
        size > APP_BUILTIN_MAX_CODE_SIZE - offset) {
        LOG_ERROR("APP_BUILTIN", "Codigo interno ZAPP excede o limite");
        return ERR_OVERFLOW;
    }
    return OK;
}

static int app_builtin_emit_mov(uint8_t* code, uint32_t* offset,
                                uint8_t reg, uint32_t value) {
    if (!code || !offset || reg > APP_BUILTIN_REG_EBX) {
        LOG_ERROR("APP_BUILTIN", "Instrucao MOV interna invalida");
        return ERR_INVALID;
    }
    if (app_builtin_require_space(*offset, 5U) != OK) return ERR_OVERFLOW;
    code[(*offset)++] = (uint8_t)(APP_BUILTIN_OPCODE_MOV_IMM_BASE + reg);
    app_builtin_write_u32(code, *offset, value);
    *offset += 4U;
    return OK;
}

static int app_builtin_emit_load_ecx(uint8_t* code, uint32_t* offset,
                                     uint32_t address) {
    if (!code || !offset) {
        LOG_ERROR("APP_BUILTIN", "Leitura interna de argumentos invalida");
        return ERR_INVALID;
    }
    if (app_builtin_require_space(*offset, 6U) != OK) return ERR_OVERFLOW;
    code[(*offset)++] = APP_BUILTIN_OPCODE_MOV_REG_MEM;
    code[(*offset)++] = APP_BUILTIN_ECX_ABSOLUTE_MODRM;
    app_builtin_write_u32(code, *offset, address);
    *offset += 4U;
    return OK;
}

static int app_builtin_emit_int80(uint8_t* code, uint32_t* offset) {
    if (!code || !offset) {
        LOG_ERROR("APP_BUILTIN", "Interrupcao interna fora do limite");
        return ERR_INVALID;
    }
    if (app_builtin_require_space(*offset, 2U) != OK) return ERR_OVERFLOW;
    code[(*offset)++] = APP_BUILTIN_OPCODE_INT;
    code[(*offset)++] = APP_BUILTIN_INTERRUPT_SYSCALL;
    return OK;
}

static int app_builtin_emit_console_write(uint8_t* code, uint32_t* offset,
                                          uint32_t address, uint32_t size) {
    int result;

    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX, address);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_ECX, size);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    return app_builtin_emit_int80(code, offset);
}

static int app_builtin_emit_raw_arguments(uint8_t* code, uint32_t* offset) {
    uint32_t jump_offset;
    int32_t relative;
    int result;

    result = app_builtin_emit_load_ecx(
        code, offset, USER_LAUNCH_BASE + APP_LAUNCH_RAW_LENGTH_OFFSET);
    if (result != OK) return result;
    if (app_builtin_require_space(*offset, 4U) != OK) return ERR_OVERFLOW;
    code[(*offset)++] = APP_BUILTIN_OPCODE_TEST_REG;
    code[(*offset)++] = APP_BUILTIN_TEST_ECX_ECX;
    jump_offset = *offset;
    code[(*offset)++] = APP_BUILTIN_OPCODE_JZ;
    code[(*offset)++] = 0;

    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX,
                                  USER_LAUNCH_BASE +
                                  APP_LAUNCH_RAW_ARGS_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, offset);
    if (result != OK) return result;

    relative = (int32_t)(*offset) - (int32_t)(jump_offset + 2U);
    if (relative < -128 || relative > 127) {
        LOG_ERROR("APP_BUILTIN", "Desvio interno ZAPP excede o limite");
        return ERR_OVERFLOW;
    }
    code[jump_offset + 1U] = (uint8_t)relative;
    return OK;
}

static int app_builtin_emit_exit(uint8_t* code, uint32_t* offset) {
    int result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX, 0);

    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_PROCESS_EXIT);
    if (result != OK) return result;
    return app_builtin_emit_int80(code, offset);
}

static int app_builtin_finalize_image(const uint8_t* code,
                                      uint32_t code_size, const char* data,
                                      uint32_t data_size,
                                      uint32_t* image_size) {
    app_image_header_t header;

    if (!code || !data || !image_size || code_size == 0 ||
        code_size > APP_IMAGE_MAX_CODE_SIZE ||
        data_size > APP_IMAGE_MAX_DATA_SIZE) {
        LOG_ERROR("APP_BUILTIN", "Imagem interna ZAPP invalida");
        return ERR_INVALID;
    }

    kmemset(app_builtin_image, 0, sizeof(app_builtin_image));
    header.magic[0] = 'Z';
    header.magic[1] = 'A';
    header.magic[2] = 'P';
    header.magic[3] = 'P';
    header.version = APP_IMAGE_VERSION;
    header.architecture = APP_IMAGE_ARCH_I386;
    header.header_size = APP_IMAGE_HEADER_SIZE;
    header.code_offset = APP_IMAGE_HEADER_SIZE;
    header.code_size = code_size;
    header.data_offset = header.code_offset + code_size;
    header.data_size = data_size;
    header.entry_offset = 0;
    header.stack_size = APP_IMAGE_STACK_SIZE;
    header.flags = APP_IMAGE_FLAGS_NONE;

    kmemcpy(app_builtin_image, &header, APP_IMAGE_HEADER_SIZE);
    kmemcpy(app_builtin_image + header.code_offset, code, code_size);
    kmemcpy(app_builtin_image + header.data_offset, data, data_size);
    *image_size = header.data_offset + data_size;
    return OK;
}

static int app_builtin_build_echo(uint32_t* image_size) {
    uint8_t code[APP_BUILTIN_MAX_CODE_SIZE];
    uint32_t offset = 0;
    int result;

    kmemset(code, 0, sizeof(code));
    result = app_builtin_emit_raw_arguments(code, &offset);
    if (result != OK) return result;
    result = app_builtin_emit_console_write(code, &offset, USER_DATA_BASE, 1U);
    if (result != OK) return result;
    result = app_builtin_emit_exit(code, &offset);
    if (result != OK) return result;

    return app_builtin_finalize_image(code, offset, "\n", 1U, image_size);
}

static int app_builtin_build_argtest(uint32_t* image_size) {
    uint8_t code[APP_BUILTIN_MAX_CODE_SIZE];
    uint32_t offset = 0;
    int result;

    kmemset(code, 0, sizeof(code));
    result = app_builtin_emit_console_write(
        code, &offset, USER_DATA_BASE, APP_BUILTIN_ARGTEST_PREFIX_LENGTH);
    if (result != OK) return result;
    result = app_builtin_emit_raw_arguments(code, &offset);
    if (result != OK) return result;
    result = app_builtin_emit_console_write(
        code, &offset, USER_DATA_BASE + APP_BUILTIN_ARGTEST_NEWLINE_OFFSET, 1U);
    if (result != OK) return result;
    result = app_builtin_emit_exit(code, &offset);
    if (result != OK) return result;

    return app_builtin_finalize_image(
        code, offset, app_builtin_argtest_data,
        sizeof(app_builtin_argtest_data) - 1U, image_size);
}

int app_builtin_run_echo(const app_launch_info_t* launch, uint32_t* pid_out) {
    uint32_t image_size;
    int result = app_builtin_build_echo(&image_size);

    if (result != OK) return result;
    return app_loader_run_image("Echo", app_builtin_image, image_size,
                                launch, pid_out);
}

int app_builtin_run_argtest(const app_launch_info_t* launch,
                            uint32_t* pid_out) {
    uint32_t image_size;
    int result = app_builtin_build_argtest(&image_size);

    if (result != OK) return result;
    return app_loader_run_image("ArgTest", app_builtin_image, image_size,
                                launch, pid_out);
}
