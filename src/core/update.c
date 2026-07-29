#include "core/update.h"
#include "core/crypto.h"
#include "core/log.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/update_trust.h"
#include "core/version.h"
#include "fs/fs.h"

#define UPDATE_FORMAT_VERSION 1U
#define UPDATE_ARCH_I386 1U
#define UPDATE_SIGNATURE_SIZE 64U
#define UPDATE_SIGNATURE_ED25519 1U
#define UPDATE_HASH_SHA256 1U
#define UPDATE_OPERATION_REPLACE 1U
#define UPDATE_COMPRESSION_NONE 0U
#define UPDATE_TARGET_SYSTEM_FILE 1U
#define UPDATE_IO_BUFFER_SIZE 4096U
#define UPDATE_DOMAIN_SIZE 19U

#define HEADER_MAGIC 0U
#define HEADER_FORMAT_VERSION 4U
#define HEADER_SIZE 6U
#define HEADER_ARCHITECTURE 8U
#define HEADER_FLAGS 12U
#define HEADER_TOTAL_SIZE 16U
#define HEADER_MANIFEST_OFFSET 20U
#define HEADER_MANIFEST_SIZE 24U
#define HEADER_PAYLOAD_OFFSET 28U
#define HEADER_PAYLOAD_SIZE 32U
#define HEADER_SIGNATURE_OFFSET 36U
#define HEADER_SIGNATURE_SIZE 40U
#define HEADER_SIGNATURE_ALGORITHM 42U
#define HEADER_HASH_ALGORITHM 44U
#define HEADER_ENTRY_COUNT 46U
#define HEADER_ENTRY_SIZE 48U
#define HEADER_RESERVED0 50U
#define HEADER_BASE_MAJOR 52U
#define HEADER_BASE_MINOR 54U
#define HEADER_BASE_PATCH 56U
#define HEADER_TARGET_MAJOR 58U
#define HEADER_TARGET_MINOR 60U
#define HEADER_TARGET_PATCH 62U
#define HEADER_BASE_EPOCH 64U
#define HEADER_TARGET_EPOCH 68U
#define HEADER_KEY_ID 72U
#define HEADER_CONTENT_HASH 88U
#define HEADER_RESERVED 120U

#define ENTRY_PATH 0U
#define ENTRY_PAYLOAD_OFFSET 64U
#define ENTRY_PAYLOAD_SIZE 68U
#define ENTRY_INSTALLED_SIZE 72U
#define ENTRY_OPERATION 76U
#define ENTRY_COMPRESSION 78U
#define ENTRY_TARGET_CLASS 80U
#define ENTRY_FLAGS 82U
#define ENTRY_PAYLOAD_HASH 84U
#define ENTRY_RESERVED 116U

typedef struct {
    char path[ZUPD_PATH_SIZE];
    uint32_t payload_offset;
    uint32_t payload_size;
    uint8_t payload_hash[CRYPTO_SHA256_SIZE];
} update_entry_t;

typedef struct {
    uint32_t architecture;
    uint32_t total_size;
    uint32_t manifest_size;
    uint32_t payload_offset;
    uint32_t payload_size;
    uint32_t signature_offset;
    uint16_t entry_count;
    update_version_t base_version;
    update_version_t target_version;
    uint32_t base_epoch;
    uint32_t target_epoch;
    uint8_t key_id[16];
    uint8_t content_hash[CRYPTO_SHA256_SIZE];
} update_header_t;

typedef struct {
    uint8_t header[ZUPD_HEADER_SIZE];
    uint8_t table[ZUPD_ENTRY_SIZE * ZUPD_MAX_ENTRIES];
    uint8_t io_buffer[UPDATE_IO_BUFFER_SIZE];
    uint8_t signature[UPDATE_SIGNATURE_SIZE];
    update_entry_t entries[ZUPD_MAX_ENTRIES];
    crypto_sha256_ctx_t content_sha256;
    crypto_sha256_ctx_t payload_sha256;
    crypto_ed25519_verify_ctx_t signature_verify;
    update_header_t parsed;
    uint8_t busy;
} update_workspace_t;

static const uint8_t update_domain[UPDATE_DOMAIN_SIZE] = {
    0x5AU, 0x45U, 0x50U, 0x48U, 0x59U, 0x52U, 0x4FU, 0x53U,
    0x2DU, 0x55U, 0x50U, 0x44U, 0x41U, 0x54U, 0x45U, 0x2DU,
    0x56U, 0x31U, 0x00U
};

static update_workspace_t update_workspace;
static update_capabilities_t update_capabilities;
static spinlock_t update_lock;
static int update_initialized = 0;

static uint16_t update_read_u16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t update_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int update_bytes_zero(const uint8_t* data, uint32_t size) {
    for (uint32_t index = 0; index < size; index++) {
        if (data[index] != 0) return 0;
    }
    return 1;
}

static int update_fail(update_verification_t* output, zupd_reason_t reason,
                       int error, const char* message) {
    if (output) output->reason = reason;
    LOG_ERROR("UPDATE", message);
    return error;
}

static int update_reason_error(zupd_reason_t reason) {
    int error = ERR_INVALID;

    switch (reason) {
        case ZUPD_REASON_SIZE:
            error = ERR_OVERFLOW;
            break;
        case ZUPD_REASON_ARCHITECTURE:
        case ZUPD_REASON_BASE_VERSION:
        case ZUPD_REASON_DOWNGRADE:
            error = ERR_STATE;
            break;
        case ZUPD_REASON_UNSUPPORTED:
            error = ERR_UNAVAILABLE;
            break;
        default:
            break;
    }
    return error;
}

static int update_reject(update_verification_t* output,
                         zupd_reason_t reason, const char* message) {
    return update_fail(output, reason, update_reason_error(reason), message);
}

static int update_read_exact(const char* path, uint32_t offset,
                             uint8_t* buffer, uint32_t size,
                             update_verification_t* output) {
    uint32_t bytes_read = 0;
    int result = fs_read_file_range_at(path, offset, buffer, size, &bytes_read);

    if (result != OK) {
        return update_fail(output, ZUPD_REASON_NONE, result,
                           "Falha de I/O ao ler artefato ZUPD");
    }
    if (bytes_read != size) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Artefato ZUPD truncado");
    }
    return OK;
}

static void update_decode_header(update_header_t* parsed,
                                 const uint8_t* header) {
    parsed->architecture = update_read_u32(header + HEADER_ARCHITECTURE);
    parsed->total_size = update_read_u32(header + HEADER_TOTAL_SIZE);
    parsed->manifest_size = update_read_u32(header + HEADER_MANIFEST_SIZE);
    parsed->payload_offset = update_read_u32(header + HEADER_PAYLOAD_OFFSET);
    parsed->payload_size = update_read_u32(header + HEADER_PAYLOAD_SIZE);
    parsed->signature_offset =
        update_read_u32(header + HEADER_SIGNATURE_OFFSET);
    parsed->entry_count = update_read_u16(header + HEADER_ENTRY_COUNT);
    parsed->base_version.major = update_read_u16(header + HEADER_BASE_MAJOR);
    parsed->base_version.minor = update_read_u16(header + HEADER_BASE_MINOR);
    parsed->base_version.patch = update_read_u16(header + HEADER_BASE_PATCH);
    parsed->target_version.major =
        update_read_u16(header + HEADER_TARGET_MAJOR);
    parsed->target_version.minor =
        update_read_u16(header + HEADER_TARGET_MINOR);
    parsed->target_version.patch =
        update_read_u16(header + HEADER_TARGET_PATCH);
    parsed->base_epoch = update_read_u32(header + HEADER_BASE_EPOCH);
    parsed->target_epoch = update_read_u32(header + HEADER_TARGET_EPOCH);
    kmemcpy(parsed->key_id, header + HEADER_KEY_ID, sizeof(parsed->key_id));
    kmemcpy(parsed->content_hash, header + HEADER_CONTENT_HASH,
            sizeof(parsed->content_hash));
}

static int update_validate_header_limits(const uint8_t* header,
                                         update_verification_t* output) {
    uint32_t total_size = update_read_u32(header + HEADER_TOTAL_SIZE);
    uint32_t manifest_size = update_read_u32(header + HEADER_MANIFEST_SIZE);
    uint32_t payload_offset = update_read_u32(header + HEADER_PAYLOAD_OFFSET);
    uint32_t payload_size = update_read_u32(header + HEADER_PAYLOAD_SIZE);
    uint32_t signature_offset =
        update_read_u32(header + HEADER_SIGNATURE_OFFSET);
    uint16_t entry_count = update_read_u16(header + HEADER_ENTRY_COUNT);

    if (total_size < 321U || total_size > ZUPD_MAX_TOTAL_SIZE ||
        manifest_size > ZUPD_MAX_TOTAL_SIZE ||
        payload_offset > ZUPD_MAX_TOTAL_SIZE ||
        payload_size > ZUPD_MAX_TOTAL_SIZE ||
        signature_offset > ZUPD_MAX_TOTAL_SIZE ||
        entry_count == 0U || entry_count > ZUPD_MAX_ENTRIES ||
        update_read_u16(header + HEADER_SIGNATURE_SIZE) !=
            UPDATE_SIGNATURE_SIZE) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Limite de tamanho ZUPD invalido");
    }
    return OK;
}

static int update_validate_header_algorithms(const uint8_t* header,
                                             update_verification_t* output) {
    if (update_read_u16(header + HEADER_SIGNATURE_ALGORITHM) !=
            UPDATE_SIGNATURE_ED25519 ||
        update_read_u16(header + HEADER_HASH_ALGORITHM) !=
            UPDATE_HASH_SHA256) {
        return update_reject(output, ZUPD_REASON_UNSUPPORTED,
                             "Algoritmo ZUPD nao suportado");
    }
    return OK;
}

static int update_validate_header_layout(const uint8_t* header,
                                         update_verification_t* output) {
    uint32_t total_size = update_read_u32(header + HEADER_TOTAL_SIZE);
    uint32_t manifest_size = update_read_u32(header + HEADER_MANIFEST_SIZE);
    uint32_t payload_offset = update_read_u32(header + HEADER_PAYLOAD_OFFSET);
    uint32_t payload_size = update_read_u32(header + HEADER_PAYLOAD_SIZE);
    uint32_t signature_offset =
        update_read_u32(header + HEADER_SIGNATURE_OFFSET);
    uint32_t expected_manifest =
        update_read_u16(header + HEADER_ENTRY_COUNT) * ZUPD_ENTRY_SIZE;

    if (header[0] != 'Z' || header[1] != 'U' ||
        header[2] != 'P' || header[3] != 'D' ||
        update_read_u16(header + HEADER_FORMAT_VERSION) !=
            UPDATE_FORMAT_VERSION ||
        update_read_u16(header + HEADER_SIZE) != ZUPD_HEADER_SIZE ||
        update_read_u32(header + HEADER_FLAGS) != 0U ||
        update_read_u32(header + HEADER_MANIFEST_OFFSET) != ZUPD_HEADER_SIZE ||
        manifest_size != expected_manifest ||
        payload_offset != ZUPD_HEADER_SIZE + expected_manifest ||
        signature_offset != payload_offset + payload_size ||
        total_size != signature_offset + UPDATE_SIGNATURE_SIZE ||
        update_read_u16(header + HEADER_ENTRY_SIZE) != ZUPD_ENTRY_SIZE ||
        update_read_u16(header + HEADER_RESERVED0) != 0U ||
        !update_bytes_zero(header + HEADER_RESERVED, 8U)) {
        return update_reject(output, ZUPD_REASON_FORMAT,
                             "Layout ou campos reservados do header invalidos");
    }
    return OK;
}

static int update_check_exact_file_size(const char* path,
                                        const update_header_t* parsed,
                                        update_verification_t* output) {
    uint32_t bytes_read = 0;
    int result = fs_read_file_range_at(
        path, parsed->total_size, update_workspace.io_buffer, 1U, &bytes_read);

    if (result != OK) {
        return update_fail(output, ZUPD_REASON_NONE, result,
                           "Falha ao confirmar tamanho do ZUPD");
    }
    if (bytes_read != 0U) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Artefato possui bytes apos o fim declarado");
    }
    return OK;
}

static int update_is_path_character(char value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

static int update_validate_path_syntax(const char* path) {
    uint32_t base_size = 0;
    uint32_t extension_size = 0;

    while (path[base_size] && path[base_size] != '.') {
        if (!update_is_path_character(path[base_size]) || base_size >= 8U) {
            return 0;
        }
        base_size++;
    }
    if (base_size == 0U || path[base_size] != '.') return 0;
    for (uint32_t index = base_size + 1U; path[index]; index++) {
        if (!update_is_path_character(path[index]) || path[index] == '_' ||
            extension_size >= 3U) {
            return 0;
        }
        extension_size++;
    }
    return extension_size > 0U;
}

static int update_decode_path(char output[ZUPD_PATH_SIZE],
                              const uint8_t* encoded,
                              update_verification_t* result) {
    uint32_t length = 0;

    while (length < ZUPD_PATH_SIZE && encoded[length] != 0U) {
        output[length] = (char)encoded[length];
        length++;
    }
    if (length == 0U || length == ZUPD_PATH_SIZE ||
        !update_bytes_zero(encoded + length + 1U,
                           ZUPD_PATH_SIZE - length - 1U)) {
        return update_reject(result, ZUPD_REASON_FORMAT,
                             "Campo path sem terminacao canonica");
    }
    output[length] = '\0';
    if (!update_validate_path_syntax(output)) {
        return update_reject(result, ZUPD_REASON_PATH_POLICY,
                             "Caminho FAT nao canonico");
    }
    return OK;
}

static int update_compare_paths(const char* first, const char* second) {
    while (*first && *first == *second) {
        first++;
        second++;
    }
    return (uint8_t)*first - (uint8_t)*second;
}

static int update_parse_entry(update_entry_t* parsed, const uint8_t* raw,
                              uint32_t expected_offset,
                              update_verification_t* output) {
    int result = update_decode_path(parsed->path, raw + ENTRY_PATH, output);

    if (result != OK) return result;
    parsed->payload_offset = update_read_u32(raw + ENTRY_PAYLOAD_OFFSET);
    parsed->payload_size = update_read_u32(raw + ENTRY_PAYLOAD_SIZE);
    kmemcpy(parsed->payload_hash, raw + ENTRY_PAYLOAD_HASH,
            sizeof(parsed->payload_hash));
    if (parsed->payload_size == 0U ||
        parsed->payload_size > ZUPD_MAX_PAYLOAD_SIZE) {
        return update_reject(output, ZUPD_REASON_SIZE,
                             "Payload individual fora dos limites");
    }
    if (update_read_u16(raw + ENTRY_OPERATION) != UPDATE_OPERATION_REPLACE ||
        update_read_u16(raw + ENTRY_COMPRESSION) != UPDATE_COMPRESSION_NONE ||
        update_read_u16(raw + ENTRY_TARGET_CLASS) !=
            UPDATE_TARGET_SYSTEM_FILE) {
        return update_reject(output, ZUPD_REASON_UNSUPPORTED,
                             "Operacao ZUPD nao suportada");
    }
    if (update_read_u32(raw + ENTRY_INSTALLED_SIZE) != parsed->payload_size ||
        parsed->payload_offset != expected_offset ||
        update_read_u16(raw + ENTRY_FLAGS) != 0U ||
        !update_bytes_zero(raw + ENTRY_RESERVED, 12U)) {
        return update_reject(output, ZUPD_REASON_FORMAT,
                             "Layout ou campo reservado de entrada invalido");
    }
    return OK;
}

static int update_parse_entries(update_workspace_t* workspace,
                                update_verification_t* output) {
    uint32_t expected_offset = workspace->parsed.payload_offset;

    for (uint32_t index = 0; index < workspace->parsed.entry_count; index++) {
        const uint8_t* raw = workspace->table + index * ZUPD_ENTRY_SIZE;
        update_entry_t* entry = &workspace->entries[index];
        int result = update_parse_entry(entry, raw, expected_offset, output);

        if (result != OK) return result;
        if (index > 0U) {
            int order = update_compare_paths(
                workspace->entries[index - 1U].path, entry->path);
            if (order == 0) {
                return update_reject(output, ZUPD_REASON_DUPLICATE_TARGET,
                                     "Target ZUPD duplicado");
            }
            if (order > 0) {
                return update_reject(output, ZUPD_REASON_FORMAT,
                                     "Tabela ZUPD fora de ordem");
            }
        }
        if (entry->payload_offset > workspace->parsed.signature_offset ||
            entry->payload_size >
                workspace->parsed.signature_offset - entry->payload_offset) {
            return update_reject(output, ZUPD_REASON_SIZE,
                                 "Payload excede a regiao declarada");
        }
        expected_offset = entry->payload_offset + entry->payload_size;
    }
    if (expected_offset != workspace->parsed.signature_offset) {
        return update_reject(output, ZUPD_REASON_FORMAT,
                             "Payloads possuem lacuna ou sobreposicao");
    }
    return OK;
}

static int update_hash_chunk(update_workspace_t* workspace,
                             const uint8_t* data, uint32_t size) {
    int result = crypto_sha256_update(&workspace->content_sha256, data, size);

    if (result != OK) return result;
    result = crypto_sha256_update(&workspace->payload_sha256, data, size);
    if (result != OK) return result;
    return crypto_ed25519_verify_update(
        &workspace->signature_verify, data, size);
}

static int update_hash_payload(const char* path, update_workspace_t* workspace,
                               update_entry_t* entry,
                               update_verification_t* output) {
    uint32_t remaining = entry->payload_size;
    uint32_t offset = entry->payload_offset;
    uint8_t computed[CRYPTO_SHA256_SIZE];
    int result = crypto_sha256_init(&workspace->payload_sha256);

    if (result != OK) return result;
    while (remaining > 0U) {
        uint32_t chunk = remaining > UPDATE_IO_BUFFER_SIZE ?
                         UPDATE_IO_BUFFER_SIZE : remaining;
        result = update_read_exact(
            path, offset, workspace->io_buffer, chunk, output);
        if (result != OK) return result;
        result = update_hash_chunk(workspace, workspace->io_buffer, chunk);
        if (result != OK) return result;
        offset += chunk;
        remaining -= chunk;
    }
    result = crypto_sha256_final(&workspace->payload_sha256, computed);
    if (result != OK) return result;
    if (!crypto_equal(computed, entry->payload_hash, sizeof(computed))) {
        return update_reject(output, ZUPD_REASON_HASH,
                             "SHA-256 individual do payload diverge");
    }
    return OK;
}

static int update_hash_content(const char* path, update_workspace_t* workspace,
                               update_verification_t* output) {
    uint8_t computed[CRYPTO_SHA256_SIZE];
    int result = crypto_sha256_init(&workspace->content_sha256);

    if (result != OK) return result;
    result = crypto_ed25519_verify_init(
        &workspace->signature_verify, workspace->signature,
        UPDATE_TRUST_PUBLIC_KEY);
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &workspace->signature_verify, update_domain, sizeof(update_domain));
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &workspace->signature_verify, workspace->header, ZUPD_HEADER_SIZE);
    if (result != OK) return result;
    result = crypto_sha256_update(
        &workspace->content_sha256, workspace->table,
        workspace->parsed.manifest_size);
    if (result != OK) return result;
    result = crypto_ed25519_verify_update(
        &workspace->signature_verify, workspace->table,
        workspace->parsed.manifest_size);
    if (result != OK) return result;
    for (uint32_t index = 0; index < workspace->parsed.entry_count; index++) {
        result = update_hash_payload(
            path, workspace, &workspace->entries[index], output);
        if (result != OK) return result;
    }
    result = crypto_sha256_final(&workspace->content_sha256, computed);
    if (result != OK) return result;
    if (!crypto_equal(computed, workspace->parsed.content_hash,
                      sizeof(computed))) {
        return update_reject(output, ZUPD_REASON_HASH,
                             "SHA-256 global do ZUPD diverge");
    }
    return OK;
}

static int update_version_compare(const update_version_t* first,
                                  const update_version_t* second) {
    if (first->major != second->major) {
        return first->major < second->major ? -1 : 1;
    }
    if (first->minor != second->minor) {
        return first->minor < second->minor ? -1 : 1;
    }
    if (first->patch != second->patch) {
        return first->patch < second->patch ? -1 : 1;
    }
    return 0;
}

static int update_path_allowed(const char* path) {
    return kstrcmp(path, "EXPLORER.BMP") == 0 ||
           kstrcmp(path, "SHELL.BMP") == 0 ||
           kstrcmp(path, "TASKMGR.BMP") == 0;
}

static int update_target_exists(const char* path) {
    uint32_t ignored = 0;
    return fs_read_file_range_at(path, 0, 0, 0, &ignored) == OK;
}

static int update_validate_applicability(update_workspace_t* workspace,
                                         update_verification_t* output) {
    static const update_version_t current = {
        ZEPHYROS_VERSION_MAJOR,
        ZEPHYROS_VERSION_MINOR,
        ZEPHYROS_VERSION_PATCH
    };

    if (workspace->parsed.architecture != UPDATE_ARCH_I386) {
        return update_reject(output, ZUPD_REASON_ARCHITECTURE,
                             "Arquitetura ZUPD incompativel");
    }
    if (update_version_compare(&workspace->parsed.base_version, &current) != 0 ||
        workspace->parsed.base_epoch != ZEPHYROS_VERSION_EPOCH) {
        return update_reject(output, ZUPD_REASON_BASE_VERSION,
                             "Versao base ZUPD incompativel");
    }
    if (update_version_compare(&workspace->parsed.target_version,
                               &workspace->parsed.base_version) <= 0 ||
        workspace->parsed.target_epoch < workspace->parsed.base_epoch) {
        return update_reject(output, ZUPD_REASON_DOWNGRADE,
                             "ZUPD representa downgrade");
    }
    for (uint32_t index = 0; index < workspace->parsed.entry_count; index++) {
        const char* path = workspace->entries[index].path;
        if (!update_path_allowed(path) || !update_target_exists(path)) {
            return update_reject(output, ZUPD_REASON_PATH_POLICY,
                                 "Target ZUPD ausente ou fora da allowlist");
        }
    }
    return OK;
}

static void update_fill_result(update_verification_t* output,
                               const update_header_t* parsed) {
    output->reason = ZUPD_REASON_NONE;
    output->base_version = parsed->base_version;
    output->target_version = parsed->target_version;
    output->base_epoch = parsed->base_epoch;
    output->target_epoch = parsed->target_epoch;
    output->total_size = parsed->total_size;
    output->entry_count = parsed->entry_count;
}

static int update_verify_loaded(const char* path,
                                update_verification_t* output) {
    update_workspace_t* workspace = &update_workspace;
    int result = update_hash_content(path, workspace, output);

    if (result != OK) return result;
    if (!crypto_equal(workspace->parsed.key_id, UPDATE_TRUST_KEY_ID,
                      sizeof(workspace->parsed.key_id))) {
        return update_reject(output, ZUPD_REASON_UNKNOWN_KEY,
                             "key_id ZUPD desconhecido");
    }
    result = crypto_ed25519_verify_final(&workspace->signature_verify);
    if (result != OK) {
        return update_reject(output, ZUPD_REASON_SIGNATURE,
                             "Assinatura ZUPD invalida");
    }
    update_fill_result(output, &workspace->parsed);
    result = update_validate_applicability(workspace, output);
    if (result != OK) return result;
    return OK;
}

static int update_load_structure(const char* path,
                                 update_verification_t* output) {
    update_workspace_t* workspace = &update_workspace;
    int result = update_read_exact(
        path, 0, workspace->header, ZUPD_HEADER_SIZE, output);

    if (result != OK) return result;
    result = update_validate_header_limits(workspace->header, output);
    if (result != OK) return result;
    result = update_validate_header_algorithms(workspace->header, output);
    if (result != OK) return result;
    result = update_validate_header_layout(workspace->header, output);
    if (result != OK) return result;
    update_decode_header(&workspace->parsed, workspace->header);
    result = update_check_exact_file_size(path, &workspace->parsed, output);
    if (result != OK) return result;
    result = update_read_exact(
        path, ZUPD_HEADER_SIZE, workspace->table,
        workspace->parsed.manifest_size, output);
    if (result != OK) return result;
    result = update_parse_entries(workspace, output);
    if (result != OK) return result;
    return update_read_exact(
        path, workspace->parsed.signature_offset, workspace->signature,
        UPDATE_SIGNATURE_SIZE, output);
}

int update_init(void) {
    uint8_t key_hash[CRYPTO_SHA256_SIZE];

    LOG_INFO("UPDATE", "Inicializando verificador de atualizacoes");
    spinlock_init(&update_lock);
    kmemset(&update_workspace, 0, sizeof(update_workspace));
    kmemset(&update_capabilities, 0, sizeof(update_capabilities));
    update_initialized = 0;
    if (crypto_self_test() != OK ||
        crypto_sha256(UPDATE_TRUST_PUBLIC_KEY,
                      sizeof(UPDATE_TRUST_PUBLIC_KEY), key_hash) != OK ||
        !crypto_equal(key_hash, UPDATE_TRUST_KEY_ID,
                      sizeof(UPDATE_TRUST_KEY_ID))) {
        LOG_ERROR("UPDATE", "Chave publica ou autoteste invalido");
        return ERR_INVALID;
    }
    update_initialized = 1;
    update_capabilities.verifier_ready = 1;
    update_capabilities.local_file_available =
        fs_get_type() != FS_TYPE_NONE;
    LOG_INFO("UPDATE", "Verificador de atualizacoes inicializado com sucesso");
    return OK;
}

int update_is_ready(void) {
    return update_initialized;
}

int update_get_capabilities(update_capabilities_t* capabilities_out) {
    if (!capabilities_out) {
        LOG_ERROR("UPDATE", "Destino de capacidades nulo");
        return ERR_NULL;
    }
    if (!update_initialized) {
        LOG_ERROR("UPDATE", "Capacidades consultadas antes da inicializacao");
        return ERR_STATE;
    }
    update_capabilities.local_file_available =
        fs_get_type() != FS_TYPE_NONE;
    *capabilities_out = update_capabilities;
    return OK;
}

int update_verify_file(const char* path, update_verification_t* result_out) {
    int result;

    if (!path || !result_out) {
        LOG_ERROR("UPDATE", "Argumento nulo na verificacao ZUPD");
        return ERR_NULL;
    }
    kmemset(result_out, 0, sizeof(*result_out));
    if (!update_initialized) {
        return update_fail(result_out, ZUPD_REASON_NONE, ERR_STATE,
                           "Verificador ZUPD nao inicializado");
    }
    if (fs_get_type() == FS_TYPE_NONE) {
        return update_fail(result_out, ZUPD_REASON_NONE, ERR_UNAVAILABLE,
                           "Filesystem indisponivel para ZUPD");
    }
    spinlock_acquire(&update_lock);
    if (update_workspace.busy) {
        spinlock_release(&update_lock);
        return update_fail(result_out, ZUPD_REASON_NONE, ERR_STATE,
                           "Verificacao ZUPD concorrente recusada");
    }
    update_workspace.busy = 1;
    spinlock_release(&update_lock);
    result = update_load_structure(path, result_out);
    if (result == OK) result = update_verify_loaded(path, result_out);
    spinlock_acquire(&update_lock);
    update_workspace.busy = 0;
    spinlock_release(&update_lock);
    if (result == OK) {
        LOG_INFO("UPDATE", "Artefato ZUPD autenticado e compativel");
    }
    return result;
}

const char* zupd_reason_name(zupd_reason_t reason) {
    switch (reason) {
        case ZUPD_REASON_NONE: return "NONE";
        case ZUPD_REASON_FORMAT: return "FORMAT";
        case ZUPD_REASON_SIZE: return "SIZE";
        case ZUPD_REASON_HASH: return "HASH";
        case ZUPD_REASON_UNKNOWN_KEY: return "UNKNOWN_KEY";
        case ZUPD_REASON_SIGNATURE: return "SIGNATURE";
        case ZUPD_REASON_ARCHITECTURE: return "ARCHITECTURE";
        case ZUPD_REASON_BASE_VERSION: return "BASE_VERSION";
        case ZUPD_REASON_DOWNGRADE: return "DOWNGRADE";
        case ZUPD_REASON_PATH_POLICY: return "PATH_POLICY";
        case ZUPD_REASON_DUPLICATE_TARGET: return "DUPLICATE_TARGET";
        case ZUPD_REASON_UNSUPPORTED: return "UNSUPPORTED";
        default: return "INVALID_REASON";
    }
}
