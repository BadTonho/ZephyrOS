#include "core/app_builtin.h"
#include "core/app_loader.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/syscall.h"
#include "memory/paging.h"

#define APP_BUILTIN_MAX_CODE_SIZE    1024U
#define APP_BUILTIN_DATA_SIZE        128U
#define APP_BUILTIN_OUTPUT_OFFSET    32U
#define APP_BUILTIN_OUTPUTTEST_CHUNK_COUNT 9U

#define APP_BUILTIN_UPTIME_INFO_OFFSET    0U
#define APP_BUILTIN_UPTIME_SECONDS_OFFSET 4U
#define APP_BUILTIN_MEMORY_INFO_OFFSET    0U
#define APP_BUILTIN_MEMORY_TOTAL_OFFSET   0U
#define APP_BUILTIN_MEMORY_USED_OFFSET    4U
#define APP_BUILTIN_MEMORY_FREE_OFFSET    8U

#define APP_BUILTIN_BYTES_PER_KB 1024U
#define APP_BUILTIN_SECONDS_PER_MINUTE 60U
#define APP_BUILTIN_DECIMAL_BASE 10U

#define APP_BUILTIN_OPCODE_MOV_IMM_BASE 0xB8U
#define APP_BUILTIN_OPCODE_MOV_AL_IMM   0xB0U
#define APP_BUILTIN_OPCODE_MOV_REG_MEM  0x8BU
#define APP_BUILTIN_OPCODE_MOV_REG_REG  0x89U
#define APP_BUILTIN_OPCODE_MOV_MEM_REG  0x88U
#define APP_BUILTIN_OPCODE_MOV_MEM_IMM  0xC6U
#define APP_BUILTIN_OPCODE_TEST_REG     0x85U
#define APP_BUILTIN_OPCODE_JZ           0x74U
#define APP_BUILTIN_OPCODE_JNZ          0x75U
#define APP_BUILTIN_OPCODE_JMP          0xEBU
#define APP_BUILTIN_OPCODE_CALL         0xE8U
#define APP_BUILTIN_OPCODE_LOOP         0xE2U
#define APP_BUILTIN_OPCODE_INT          0xCDU
#define APP_BUILTIN_OPCODE_DIV          0xF7U
#define APP_BUILTIN_OPCODE_ADD          0x80U
#define APP_BUILTIN_OPCODE_SUB          0x81U
#define APP_BUILTIN_OPCODE_XOR          0x31U
#define APP_BUILTIN_OPCODE_PUSH_REG     0x50U
#define APP_BUILTIN_OPCODE_POP_REG      0x58U
#define APP_BUILTIN_OPCODE_INC_EDI      0x47U
#define APP_BUILTIN_OPCODE_INC_ECX      0x41U
#define APP_BUILTIN_OPCODE_RET          0xC3U
#define APP_BUILTIN_OPCODE_HLT          0xF4U

#define APP_BUILTIN_INTERRUPT_SYSCALL 0x80U
#define APP_BUILTIN_REG_EAX 0U
#define APP_BUILTIN_REG_ECX 1U
#define APP_BUILTIN_REG_EDX 2U
#define APP_BUILTIN_REG_EBX 3U
#define APP_BUILTIN_REG_EDI 7U

#define APP_BUILTIN_ECX_ABSOLUTE_MODRM 0x0DU
#define APP_BUILTIN_EAX_ABSOLUTE_MODRM 0x05U
#define APP_BUILTIN_EDI_AL_MODRM        0x07U
#define APP_BUILTIN_EDI_DL_MODRM        0x17U
#define APP_BUILTIN_TEST_EAX_EAX        0xC0U
#define APP_BUILTIN_TEST_ECX_ECX        0xC9U
#define APP_BUILTIN_XOR_EDX_EDX         0xD2U
#define APP_BUILTIN_DIV_EBX_MODRM       0xF3U
#define APP_BUILTIN_ADD_DL_IMM_MODRM    0xC2U
#define APP_BUILTIN_SUB_ECX_IMM_MODRM   0xE9U
#define APP_BUILTIN_MOV_EBX_EAX_MODRM   0xC3U
#define APP_BUILTIN_MOV_ECX_EDI_MODRM   0xF9U

#define APP_BUILTIN_EXIT_FROM_EAX_SIZE 10U
#define APP_BUILTIN_OUTPUT_BASE (USER_DATA_BASE + APP_BUILTIN_OUTPUT_OFFSET)

static uint8_t app_builtin_image[APP_IMAGE_MAX_FILE_SIZE];
static const char app_builtin_argtest_data[] = "Argumentos ZAPP: \n";
static const char app_builtin_outputtest_prefix[] = "Q6D outputtest: ";

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

static int app_builtin_emit_data(uint8_t* code, uint32_t* offset,
                                 const uint8_t* data, uint32_t size) {
    if (!code || !offset || !data || size == 0) {
        LOG_ERROR("APP_BUILTIN", "Dados de instrucao internos invalidos");
        return ERR_INVALID;
    }
    if (app_builtin_require_space(*offset, size) != OK) return ERR_OVERFLOW;

    kmemcpy(code + *offset, data, size);
    *offset += size;
    return OK;
}

static int app_builtin_emit_mov(uint8_t* code, uint32_t* offset,
                                uint8_t reg, uint32_t value) {
    if (!code || !offset || reg > APP_BUILTIN_REG_EDI) {
        LOG_ERROR("APP_BUILTIN", "Instrucao MOV interna invalida");
        return ERR_INVALID;
    }
    if (app_builtin_require_space(*offset, 5U) != OK) return ERR_OVERFLOW;

    code[(*offset)++] = (uint8_t)(APP_BUILTIN_OPCODE_MOV_IMM_BASE + reg);
    app_builtin_write_u32(code, *offset, value);
    *offset += 4U;
    return OK;
}

static int app_builtin_emit_load_eax(uint8_t* code, uint32_t* offset,
                                     uint32_t address) {
    uint8_t instruction[] = {
        APP_BUILTIN_OPCODE_MOV_REG_MEM,
        APP_BUILTIN_EAX_ABSOLUTE_MODRM,
        0, 0, 0, 0
    };

    int result = app_builtin_emit_data(code, offset, instruction,
                                       sizeof(instruction));

    if (result != OK) return result;
    app_builtin_write_u32(code, *offset - 4U, address);
    return OK;
}

static int app_builtin_emit_load_ecx(uint8_t* code, uint32_t* offset,
                                     uint32_t address) {
    uint8_t instruction[] = {
        APP_BUILTIN_OPCODE_MOV_REG_MEM,
        APP_BUILTIN_ECX_ABSOLUTE_MODRM,
        0, 0, 0, 0
    };

    int result = app_builtin_emit_data(code, offset, instruction,
                                       sizeof(instruction));

    if (result != OK) return result;
    app_builtin_write_u32(code, *offset - 4U, address);
    return OK;
}

static int app_builtin_emit_int80(uint8_t* code, uint32_t* offset) {
    const uint8_t instruction[] = {
        APP_BUILTIN_OPCODE_INT, APP_BUILTIN_INTERRUPT_SYSCALL
    };

    return app_builtin_emit_data(code, offset, instruction, sizeof(instruction));
}

static int app_builtin_emit_exit_from_eax(uint8_t* code, uint32_t* offset) {
    const uint8_t move_exit_code[] = {
        APP_BUILTIN_OPCODE_MOV_REG_REG, APP_BUILTIN_MOV_EBX_EAX_MODRM
    };
    int result;

    result = app_builtin_emit_data(code, offset, move_exit_code,
                                   sizeof(move_exit_code));
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_PROCESS_EXIT);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, offset);
    if (result != OK) return result;

    return app_builtin_emit_data(code, offset,
                                 (const uint8_t[]){APP_BUILTIN_OPCODE_HLT}, 1U);
}

static int app_builtin_emit_exit_on_error(uint8_t* code, uint32_t* offset) {
    const uint8_t test_result[] = {
        APP_BUILTIN_OPCODE_TEST_REG, APP_BUILTIN_TEST_EAX_EAX,
        APP_BUILTIN_OPCODE_JZ, APP_BUILTIN_EXIT_FROM_EAX_SIZE
    };
    int result = app_builtin_emit_data(code, offset, test_result,
                                       sizeof(test_result));

    if (result != OK) return result;
    return app_builtin_emit_exit_from_eax(code, offset);
}

static int app_builtin_emit_divide(uint8_t* code, uint32_t* offset,
                                   uint32_t divisor) {
    const uint8_t clear_remainder[] = {
        APP_BUILTIN_OPCODE_XOR, APP_BUILTIN_XOR_EDX_EDX
    };
    const uint8_t divide[] = {
        APP_BUILTIN_OPCODE_DIV, APP_BUILTIN_DIV_EBX_MODRM
    };
    int result;

    if (divisor == 0) {
        LOG_ERROR("APP_BUILTIN", "Divisao interna por zero");
        return ERR_INVALID;
    }
    result = app_builtin_emit_data(code, offset, clear_remainder,
                                   sizeof(clear_remainder));
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX, divisor);
    if (result != OK) return result;
    return app_builtin_emit_data(code, offset, divide, sizeof(divide));
}

static int app_builtin_emit_call(uint8_t* code, uint32_t* offset,
                                 uint32_t target) {
    int32_t relative;

    if (!code || !offset || target >= APP_BUILTIN_MAX_CODE_SIZE) {
        LOG_ERROR("APP_BUILTIN", "Destino de chamada interna invalido");
        return ERR_INVALID;
    }
    if (app_builtin_require_space(*offset, 5U) != OK) return ERR_OVERFLOW;

    relative = (int32_t)target - (int32_t)(*offset + 5U);
    code[(*offset)++] = APP_BUILTIN_OPCODE_CALL;
    app_builtin_write_u32(code, *offset, (uint32_t)relative);
    *offset += 4U;
    return OK;
}

static int app_builtin_emit_literal(uint8_t* code, uint32_t* offset,
                                    const char* text) {
    const uint8_t store_al[] = {
        APP_BUILTIN_OPCODE_MOV_MEM_REG, APP_BUILTIN_EDI_AL_MODRM,
        APP_BUILTIN_OPCODE_INC_EDI
    };
    uint32_t length;
    int result;

    if (!text) {
        LOG_ERROR("APP_BUILTIN", "Texto literal nulo");
        return ERR_NULL;
    }
    length = kstrlen(text);
    for (uint32_t i = 0; i < length; i++) {
        result = app_builtin_emit_data(
            code, offset,
            (const uint8_t[]){APP_BUILTIN_OPCODE_MOV_AL_IMM, (uint8_t)text[i]},
            2U);
        if (result != OK) return result;
        result = app_builtin_emit_data(code, offset, store_al, sizeof(store_al));
        if (result != OK) return result;
    }
    return OK;
}

static int app_builtin_patch_relative8(uint8_t* code, uint32_t operand,
                                       uint32_t target) {
    int32_t relative = (int32_t)target - (int32_t)(operand + 1U);

    if (!code || operand >= APP_BUILTIN_MAX_CODE_SIZE ||
        relative < -128 || relative > 127) {
        LOG_ERROR("APP_BUILTIN", "Desvio interno ZAPP excede o limite");
        return ERR_OVERFLOW;
    }
    code[operand] = (uint8_t)relative;
    return OK;
}

static int app_builtin_emit_u32_formatter(uint8_t* code, uint32_t* offset) {
    uint32_t non_zero_operand;
    uint32_t zero_done_operand;
    uint32_t divide_offset;
    uint32_t write_offset;
    uint32_t loop_operand;
    uint32_t done_offset;
    int result;

    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_PUSH_REG + APP_BUILTIN_REG_EBX,
                          APP_BUILTIN_OPCODE_PUSH_REG + APP_BUILTIN_REG_ECX,
                          APP_BUILTIN_OPCODE_PUSH_REG + APP_BUILTIN_REG_EDX,
                          APP_BUILTIN_OPCODE_TEST_REG,
                          APP_BUILTIN_TEST_EAX_EAX,
                          APP_BUILTIN_OPCODE_JNZ, 0U}, 7U);
    if (result != OK) return result;
    non_zero_operand = *offset - 1U;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_MOV_MEM_IMM,
                          APP_BUILTIN_EDI_AL_MODRM, '0',
                          APP_BUILTIN_OPCODE_INC_EDI,
                          APP_BUILTIN_OPCODE_JMP, 0U}, 6U);
    if (result != OK) return result;
    zero_done_operand = *offset - 1U;

    divide_offset = *offset;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_XOR, APP_BUILTIN_XOR_EDX_EDX}, 2U);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX,
                                  APP_BUILTIN_DECIMAL_BASE);
    if (result != OK) return result;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_DIV,
                          APP_BUILTIN_DIV_EBX_MODRM,
                          APP_BUILTIN_OPCODE_PUSH_REG + APP_BUILTIN_REG_EDX,
                          APP_BUILTIN_OPCODE_INC_ECX,
                          APP_BUILTIN_OPCODE_TEST_REG,
                          APP_BUILTIN_TEST_EAX_EAX,
                          APP_BUILTIN_OPCODE_JNZ, 0U}, 8U);
    if (result != OK) return result;
    result = app_builtin_patch_relative8(code, *offset - 1U, divide_offset);
    if (result != OK) return result;

    write_offset = *offset;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_POP_REG + APP_BUILTIN_REG_EDX,
                          APP_BUILTIN_OPCODE_ADD, APP_BUILTIN_ADD_DL_IMM_MODRM,
                          '0', APP_BUILTIN_OPCODE_MOV_MEM_REG,
                          APP_BUILTIN_EDI_DL_MODRM,
                          APP_BUILTIN_OPCODE_INC_EDI,
                          APP_BUILTIN_OPCODE_LOOP, 0U}, 9U);
    if (result != OK) return result;
    loop_operand = *offset - 1U;

    done_offset = *offset;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_POP_REG + APP_BUILTIN_REG_EDX,
                          APP_BUILTIN_OPCODE_POP_REG + APP_BUILTIN_REG_ECX,
                          APP_BUILTIN_OPCODE_POP_REG + APP_BUILTIN_REG_EBX,
                          APP_BUILTIN_OPCODE_RET}, 4U);
    if (result != OK) return result;
    result = app_builtin_patch_relative8(code, non_zero_operand, divide_offset);
    if (result != OK) return result;
    result = app_builtin_patch_relative8(code, zero_done_operand, done_offset);
    if (result != OK) return result;
    return app_builtin_patch_relative8(code, loop_operand, write_offset);
}

static int app_builtin_emit_query(uint8_t* code, uint32_t* offset,
                                  uint32_t syscall_number, uint32_t destination) {
    int result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX,
                                      destination);

    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  syscall_number);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, offset);
    if (result != OK) return result;
    return app_builtin_emit_exit_on_error(code, offset);
}

static int app_builtin_emit_number(uint8_t* code, uint32_t* offset,
                                   uint32_t source, uint32_t divisor,
                                   uint32_t formatter) {
    int result = app_builtin_emit_load_eax(code, offset, source);

    if (result != OK) return result;
    result = app_builtin_emit_divide(code, offset, divisor);
    if (result != OK) return result;
    return app_builtin_emit_call(code, offset, formatter);
}

static int app_builtin_emit_console_and_exit(uint8_t* code, uint32_t* offset) {
    const uint8_t copy_output_length[] = {
        APP_BUILTIN_OPCODE_MOV_REG_REG, APP_BUILTIN_MOV_ECX_EDI_MODRM,
        APP_BUILTIN_OPCODE_SUB, APP_BUILTIN_SUB_ECX_IMM_MODRM,
        0, 0, 0, 0
    };
    int result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX,
                                      APP_BUILTIN_OUTPUT_BASE);

    if (result != OK) return result;
    result = app_builtin_emit_data(code, offset, copy_output_length,
                                   sizeof(copy_output_length));
    if (result != OK) return result;
    app_builtin_write_u32(code, *offset - 4U, APP_BUILTIN_OUTPUT_BASE);
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, offset);
    if (result != OK) return result;
    return app_builtin_emit_exit_from_eax(code, offset);
}

static int app_builtin_emit_console_write(uint8_t* code, uint32_t* offset,
                                          uint32_t address, uint32_t size) {
    int result;

    if (size == 0 || size > APP_API_MAX_TEXT_SIZE) {
        LOG_ERROR("APP_BUILTIN", "Tamanho invalido no console ZAPP interno");
        return ERR_INVALID;
    }
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX, address);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_ECX, size);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, offset);
    if (result != OK) return result;
    return app_builtin_emit_exit_on_error(code, offset);
}

static int app_builtin_emit_exit_code(uint8_t* code, uint32_t* offset,
                                      uint32_t exit_code) {
    int result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EBX,
                                      exit_code);

    if (result != OK) return result;
    result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_PROCESS_EXIT);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, offset);
    if (result != OK) return result;
    return app_builtin_emit_data(code, offset,
                                 (const uint8_t[]){APP_BUILTIN_OPCODE_HLT}, 1U);
}

static int app_builtin_finalize_image(const uint8_t* code,
                                      uint32_t code_size, uint32_t entry_offset,
                                      const char* data, uint32_t data_size,
                                      uint32_t* image_size) {
    app_image_header_t header;

    if (!code || !data || !image_size || code_size == 0 ||
        code_size > APP_IMAGE_MAX_CODE_SIZE || entry_offset >= code_size ||
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
    header.entry_offset = entry_offset;
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
    uint32_t skip_arguments_operand;
    uint32_t offset = 0;
    int result;

    kmemset(code, 0, sizeof(code));
    result = app_builtin_emit_load_ecx(
        code, &offset, USER_LAUNCH_BASE + APP_LAUNCH_RAW_LENGTH_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_data(code, &offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_TEST_REG, APP_BUILTIN_TEST_ECX_ECX,
                          APP_BUILTIN_OPCODE_JZ, 0U}, 4U);
    if (result != OK) return result;
    skip_arguments_operand = offset - 1U;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX,
                                  USER_LAUNCH_BASE + APP_LAUNCH_RAW_ARGS_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;
    result = app_builtin_patch_relative8(code, skip_arguments_operand, offset);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX,
                                  USER_DATA_BASE);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_ECX, 1U);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX, 0U);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_PROCESS_EXIT);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;

    return app_builtin_finalize_image(code, offset, 0U, "\n", 1U, image_size);
}

static int app_builtin_build_argtest(uint32_t* image_size) {
    uint8_t code[APP_BUILTIN_MAX_CODE_SIZE];
    uint32_t skip_arguments_operand;
    uint32_t offset = 0;
    int result;

    kmemset(code, 0, sizeof(code));
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX,
                                  USER_DATA_BASE);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_ECX,
                                  APP_BUILTIN_ARGTEST_PREFIX_LENGTH);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;
    result = app_builtin_emit_load_ecx(
        code, &offset, USER_LAUNCH_BASE + APP_LAUNCH_RAW_LENGTH_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_data(code, &offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_TEST_REG, APP_BUILTIN_TEST_ECX_ECX,
                          APP_BUILTIN_OPCODE_JZ, 0U}, 4U);
    if (result != OK) return result;
    skip_arguments_operand = offset - 1U;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX,
                                  USER_LAUNCH_BASE + APP_LAUNCH_RAW_ARGS_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;
    result = app_builtin_patch_relative8(code, skip_arguments_operand, offset);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX,
                                  USER_DATA_BASE + APP_BUILTIN_ARGTEST_NEWLINE_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_ECX, 1U);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_CONSOLE_WRITE);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EBX, 0U);
    if (result != OK) return result;
    result = app_builtin_emit_mov(code, &offset, APP_BUILTIN_REG_EAX,
                                  APP_SYSCALL_PROCESS_EXIT);
    if (result != OK) return result;
    result = app_builtin_emit_int80(code, &offset);
    if (result != OK) return result;

    return app_builtin_finalize_image(code, offset, 0U,
        app_builtin_argtest_data, sizeof(app_builtin_argtest_data) - 1U,
        image_size);
}

static int app_builtin_emit_uptime_output(uint8_t* code, uint32_t* offset,
                                          uint32_t formatter) {
    int result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EDI,
                                      APP_BUILTIN_OUTPUT_BASE);

    if (result != OK) return result;
    result = app_builtin_emit_literal(code, offset, "Uptime: ");
    if (result != OK) return result;
    result = app_builtin_emit_load_eax(
        code, offset, USER_DATA_BASE + APP_BUILTIN_UPTIME_INFO_OFFSET +
        APP_BUILTIN_UPTIME_SECONDS_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_divide(code, offset, APP_BUILTIN_SECONDS_PER_MINUTE);
    if (result != OK) return result;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_PUSH_REG + APP_BUILTIN_REG_EDX},
        1U);
    if (result != OK) return result;
    result = app_builtin_emit_divide(code, offset, APP_BUILTIN_SECONDS_PER_MINUTE);
    if (result != OK) return result;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_PUSH_REG + APP_BUILTIN_REG_EDX},
        1U);
    if (result != OK) return result;
    result = app_builtin_emit_call(code, offset, formatter);
    if (result != OK) return result;
    result = app_builtin_emit_literal(code, offset, "h ");
    if (result != OK) return result;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_POP_REG + APP_BUILTIN_REG_EAX},
        1U);
    if (result != OK) return result;
    result = app_builtin_emit_call(code, offset, formatter);
    if (result != OK) return result;
    result = app_builtin_emit_literal(code, offset, "m ");
    if (result != OK) return result;
    result = app_builtin_emit_data(code, offset,
        (const uint8_t[]){APP_BUILTIN_OPCODE_POP_REG + APP_BUILTIN_REG_EAX},
        1U);
    if (result != OK) return result;
    result = app_builtin_emit_call(code, offset, formatter);
    if (result != OK) return result;
    return app_builtin_emit_literal(code, offset, "s\n");
}

static int app_builtin_emit_memory_output(uint8_t* code, uint32_t* offset,
                                          uint32_t formatter) {
    int result = app_builtin_emit_mov(code, offset, APP_BUILTIN_REG_EDI,
                                      APP_BUILTIN_OUTPUT_BASE);

    if (result != OK) return result;
    result = app_builtin_emit_literal(code, offset, "Memoria:\n  Total: ");
    if (result != OK) return result;
    result = app_builtin_emit_number(code, offset,
        USER_DATA_BASE + APP_BUILTIN_MEMORY_INFO_OFFSET +
        APP_BUILTIN_MEMORY_TOTAL_OFFSET, APP_BUILTIN_BYTES_PER_KB, formatter);
    if (result != OK) return result;
    result = app_builtin_emit_literal(code, offset, " KB\n  Livre: ");
    if (result != OK) return result;
    result = app_builtin_emit_number(code, offset,
        USER_DATA_BASE + APP_BUILTIN_MEMORY_INFO_OFFSET +
        APP_BUILTIN_MEMORY_FREE_OFFSET, APP_BUILTIN_BYTES_PER_KB, formatter);
    if (result != OK) return result;
    result = app_builtin_emit_literal(code, offset, " KB\n  Usada: ");
    if (result != OK) return result;
    result = app_builtin_emit_number(code, offset,
        USER_DATA_BASE + APP_BUILTIN_MEMORY_INFO_OFFSET +
        APP_BUILTIN_MEMORY_USED_OFFSET, APP_BUILTIN_BYTES_PER_KB, formatter);
    if (result != OK) return result;
    return app_builtin_emit_literal(code, offset, " KB\n");
}

static int app_builtin_build_uptime(uint32_t* image_size) {
    uint8_t code[APP_BUILTIN_MAX_CODE_SIZE];
    uint8_t data[APP_BUILTIN_DATA_SIZE];
    uint32_t code_size = 0;
    uint32_t entry_offset;
    int result;

    kmemset(code, 0, sizeof(code));
    kmemset(data, 0, sizeof(data));
    result = app_builtin_emit_u32_formatter(code, &code_size);
    if (result != OK) return result;
    entry_offset = code_size;
    result = app_builtin_emit_query(code, &code_size, APP_SYSCALL_UPTIME,
        USER_DATA_BASE + APP_BUILTIN_UPTIME_INFO_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_uptime_output(code, &code_size, 0U);
    if (result != OK) return result;
    result = app_builtin_emit_console_and_exit(code, &code_size);
    if (result != OK) return result;

    return app_builtin_finalize_image(code, code_size, entry_offset,
        (const char*)data, sizeof(data), image_size);
}

static int app_builtin_build_mem(uint32_t* image_size) {
    uint8_t code[APP_BUILTIN_MAX_CODE_SIZE];
    uint8_t data[APP_BUILTIN_DATA_SIZE];
    uint32_t code_size = 0;
    uint32_t entry_offset;
    int result;

    kmemset(code, 0, sizeof(code));
    kmemset(data, 0, sizeof(data));
    result = app_builtin_emit_u32_formatter(code, &code_size);
    if (result != OK) return result;
    entry_offset = code_size;
    result = app_builtin_emit_query(code, &code_size, APP_SYSCALL_MEMORY_INFO,
        USER_DATA_BASE + APP_BUILTIN_MEMORY_INFO_OFFSET);
    if (result != OK) return result;
    result = app_builtin_emit_memory_output(code, &code_size, 0U);
    if (result != OK) return result;
    result = app_builtin_emit_console_and_exit(code, &code_size);
    if (result != OK) return result;

    return app_builtin_finalize_image(code, code_size, entry_offset,
        (const char*)data, sizeof(data), image_size);
}

static int app_builtin_prepare_outputtest_chunk(uint8_t* data) {
    uint32_t prefix_size = sizeof(app_builtin_outputtest_prefix) - 1U;

    if (!data) {
        LOG_ERROR("APP_BUILTIN", "Buffer nulo no teste de saida ZAPP");
        return ERR_NULL;
    }
    if (prefix_size >= APP_BUILTIN_DATA_SIZE) {
        LOG_ERROR("APP_BUILTIN", "Prefixo do teste de saida excede o buffer");
        return ERR_OVERFLOW;
    }

    kmemset(data, '.', APP_BUILTIN_DATA_SIZE);
    kmemcpy(data, app_builtin_outputtest_prefix, prefix_size);
    data[APP_BUILTIN_DATA_SIZE - 1U] = '\n';
    return OK;
}

static int app_builtin_build_outputtest(uint32_t exit_code,
                                        uint32_t* image_size) {
    uint8_t code[APP_BUILTIN_MAX_CODE_SIZE];
    uint8_t data[APP_BUILTIN_DATA_SIZE];
    uint32_t code_size = 0;
    int result;

    if (!image_size) {
        LOG_ERROR("APP_BUILTIN", "Destino nulo no teste de saida ZAPP");
        return ERR_NULL;
    }
    result = app_builtin_prepare_outputtest_chunk(data);
    if (result != OK) return result;

    for (uint32_t i = 0; i < APP_BUILTIN_OUTPUTTEST_CHUNK_COUNT; i++) {
        result = app_builtin_emit_console_write(code, &code_size,
                                                USER_DATA_BASE,
                                                APP_BUILTIN_DATA_SIZE);
        if (result != OK) return result;
    }

    result = app_builtin_emit_exit_code(code, &code_size, exit_code);
    if (result != OK) return result;
    return app_builtin_finalize_image(code, code_size, 0U, (const char*)data,
                                      sizeof(data), image_size);
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

int app_builtin_run_uptime(uint32_t* pid_out) {
    uint32_t image_size;
    int result = app_builtin_build_uptime(&image_size);

    if (result != OK) {
        LOG_ERROR("APP_BUILTIN", "Falha ao montar ZAPP de uptime");
        return result;
    }
    result = app_loader_run_image("Uptime", app_builtin_image, image_size,
                                  0, pid_out);
    if (result != OK) LOG_WARN("APP_BUILTIN", "ZAPP de uptime indisponivel");
    return result;
}

int app_builtin_run_mem(uint32_t* pid_out) {
    uint32_t image_size;
    int result = app_builtin_build_mem(&image_size);

    if (result != OK) {
        LOG_ERROR("APP_BUILTIN", "Falha ao montar ZAPP de memoria");
        return result;
    }
    result = app_loader_run_image("Mem", app_builtin_image, image_size,
                                  0, pid_out);
    if (result != OK) LOG_WARN("APP_BUILTIN", "ZAPP de memoria indisponivel");
    return result;
}

int app_builtin_run_outputtest(uint32_t exit_code, uint32_t* pid_out) {
    uint32_t image_size;
    int result;

    if (exit_code == APP_EXIT_CANCELLED) {
        LOG_ERROR("APP_BUILTIN", "Teste de saida recebeu codigo reservado");
        return ERR_INVALID;
    }
    result = app_builtin_build_outputtest(exit_code, &image_size);
    if (result != OK) {
        LOG_ERROR("APP_BUILTIN", "Falha ao montar ZAPP de teste de saida");
        return result;
    }

    result = app_loader_run_image("OutputTest", app_builtin_image, image_size,
                                  0, pid_out);
    if (result != OK) {
        LOG_ERROR("APP_BUILTIN", "Falha ao iniciar ZAPP de teste de saida");
    }
    return result;
}
