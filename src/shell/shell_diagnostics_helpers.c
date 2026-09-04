#include "apps/shell_diagnostics_helpers.h"

#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/memory.h"
#include "core/string.h"
#include "drivers/acpi.h"
#include "drivers/mouse.h"
#include "fs/fs.h"

#define SHELL_LOG_TAIL_MAXIMUM LOG_RECORD_CAPACITY
#define SHELL_CPU_USAGE_PERCENT_SCALE 100U
#define SHELL_CPU_USAGE_MAX_PRODUCT \
    (0xFFFFFFFFU / SHELL_CPU_USAGE_PERCENT_SCALE)

const char* shell_process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_READY: return "READY";
        case PROCESS_STATE_RUNNING: return "RUNNING";
        case PROCESS_STATE_BLOCKED: return "BLOCKED";
        case PROCESS_STATE_ZOMBIE: return "ZOMBIE";
        default: return "UNUSED";
    }
}

uint8_t shell_diagnostics_health_state_color(recovery_state_t state) {
    if (state == RECOVERY_STATE_READY) return 0x0A;
    if (state == RECOVERY_STATE_DEGRADED) return 0x0E;
    return 0x0C;
}

recovery_state_t cmd_health_update_local_state(
    const update_capabilities_t* capabilities) {
    if (!capabilities->verifier_ready) return RECOVERY_STATE_DISABLED;
    if (!capabilities->local_file_available) return RECOVERY_STATE_DEGRADED;
    return RECOVERY_STATE_READY;
}

recovery_state_t cmd_health_update_apply_state(
    const update_capabilities_t* capabilities) {
    if (capabilities->apply_available) return RECOVERY_STATE_READY;
    if (capabilities->recovery_pending ||
        (fs_get_type() == FS_TYPE_FAT12 &&
         !capabilities->persistent_state_ready)) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_DISABLED;
}

recovery_state_t cmd_health_update_history_state(
    const update_capabilities_t* capabilities,
    const update_status_t* status, int status_ready) {
    if (capabilities->history_available) return RECOVERY_STATE_READY;
    if (status_ready && status->history_store == UPDATE_STORE_INVALID) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_DISABLED;
}

recovery_state_t cmd_health_remote_state_from_status(
    const update_remote_status_t* remote) {
    if (!remote || !remote->enabled) return RECOVERY_STATE_DISABLED;
    if (!remote->network_ready ||
        remote->state == UPDATE_REMOTE_STATE_FAILED ||
        remote->cache_store == UPDATE_REMOTE_STORE_INVALID) {
        return RECOVERY_STATE_DEGRADED;
    }
    return RECOVERY_STATE_READY;
}

const char* cmd_health_check_tls_detail(const tls_status_t* status) {
    if (!status->trusted_time_available) return "tempo nao confiavel";
    if (!status->handshake_available) return "handshake indisponivel";
    if (!status->certificate_validation_available) {
        return "validacao X509 indisponivel";
    }
    if (!status->entropy_available) return "entropia indisponivel";
    if (!tls_capability_available()) return "HTTPS indisponivel";
    if (status->last_reason != TLS_REASON_NONE) {
        return tls_reason_name(status->last_reason);
    }
    return "estado TLS nao pronto";
}

const char* cmd_log_level_name(log_level_t level) {
    if (level == LOG_LEVEL_ERROR) return "error";
    if (level == LOG_LEVEL_WARN) return "warn";
    if (level == LOG_LEVEL_INFO) return "info";
    if (level == LOG_LEVEL_DEBUG) return "debug";
    return "invalid";
}

int cmd_log_parse_level(const char* name, log_level_t* level) {
    if (!name || !level) return 0;
    if (kstrcmp(name, "error") == 0) *level = LOG_LEVEL_ERROR;
    else if (kstrcmp(name, "warn") == 0) *level = LOG_LEVEL_WARN;
    else if (kstrcmp(name, "info") == 0) *level = LOG_LEVEL_INFO;
    else if (kstrcmp(name, "debug") == 0) *level = LOG_LEVEL_DEBUG;
    else return 0;
    return 1;
}

uint32_t cmd_log_parse_tail_count(const char* text) {
    uint32_t value = 0U;

    if (!text || !*text) return 0U;
    while (*text) {
        if (*text < '0' || *text > '9') return 0U;
        value = value * 10U + (uint32_t)(*text - '0');
        if (value > SHELL_LOG_TAIL_MAXIMUM) return 0U;
        text++;
    }
    return value;
}

int cmd_signal_parse_uint(const char* text, uint32_t* output) {
    uint32_t value = 0U;

    if (!text || !output || !text[0]) {
        LOG_WARN("SHELL", "Sinal numerico ausente ou invalido");
        return ERR_INVALID;
    }
    while (*text) {
        uint32_t digit;

        if (*text < '0' || *text > '9') {
            LOG_WARN("SHELL", "Sinal numerico contem caractere invalido");
            return ERR_INVALID;
        }
        digit = (uint32_t)(*text - '0');
        if (value > (0xFFFFFFFFU - digit) / 10U) {
            LOG_WARN("SHELL", "Sinal numerico excedeu o limite");
            return ERR_OVERFLOW;
        }
        value = value * 10U + digit;
        text++;
    }
    *output = value;
    return OK;
}

int cmd_signal_parse_name(const char* text, uint32_t* signal_number) {
    char name[16];
    uint32_t offset = 0U;

    if (!text || !signal_number || !text[0]) {
        LOG_WARN("SHELL", "Nome de sinal ausente ou invalido");
        return ERR_INVALID;
    }
    if (text[0] == '-') text++;
    if (!text[0]) {
        LOG_WARN("SHELL", "Nome de sinal vazio");
        return ERR_INVALID;
    }
    if (*text >= '0' && *text <= '9') {
        if (cmd_signal_parse_uint(text, signal_number) != OK) {
            LOG_WARN("SHELL", "Numero de sinal rejeitado");
            return ERR_INVALID;
        }
    } else {
        while (text[offset] && offset + 1U < sizeof(name)) {
            name[offset] = text[offset];
            offset++;
        }
        if (text[offset]) {
            LOG_WARN("SHELL", "Nome de sinal excedeu o limite");
            return ERR_OVERFLOW;
        }
        name[offset] = '\0';
        shell_command_uppercase(name);
        text = name;
        if (name[0] == 'S' && name[1] == 'I' && name[2] == 'G') {
            text = name + 3;
        }
        if (kstrcmp(text, "INT") == 0) *signal_number = APP_SIGNAL_INT;
        else if (kstrcmp(text, "KILL") == 0) *signal_number = APP_SIGNAL_KILL;
        else if (kstrcmp(text, "SEGV") == 0) *signal_number = APP_SIGNAL_SEGV;
        else if (kstrcmp(text, "TERM") == 0) *signal_number = APP_SIGNAL_TERM;
        else if (kstrcmp(text, "CHLD") == 0) *signal_number = APP_SIGNAL_CHLD;
        else {
            LOG_WARN("SHELL", "Nome de sinal desconhecido");
            return ERR_INVALID;
        }
    }
    if (*signal_number != APP_SIGNAL_INT &&
        *signal_number != APP_SIGNAL_KILL &&
        *signal_number != APP_SIGNAL_SEGV &&
        *signal_number != APP_SIGNAL_TERM &&
        *signal_number != APP_SIGNAL_CHLD) {
        LOG_WARN("SHELL", "Numero de sinal fora do contrato");
        return ERR_INVALID;
    }
    return OK;
}

uint32_t shell_cpu_usage_percent(uint32_t value, uint32_t other) {
    uint32_t total = value + other;

    if (total < value) {
        value >>= 1U;
        other >>= 1U;
        total = value + other;
    }
    while (total > SHELL_CPU_USAGE_MAX_PRODUCT) {
        value >>= 1U;
        other >>= 1U;
        total = value + other;
    }
    if (!total) return 0U;
    return (value * SHELL_CPU_USAGE_PERCENT_SCALE) / total;
}

uint8_t cmd_device_status_color(device_status_t status) {
    if (status == DEVICE_STATUS_READY) return 0x0A;
    if (status == DEVICE_STATUS_DEGRADED) return 0x0E;
    if (status == DEVICE_STATUS_DISABLED) return 0x0C;
    return 0x08;
}

int cmd_sysfs_append(char* path, uint32_t capacity, uint32_t* length,
                     const char* text) {
    uint32_t text_length;

    if (!path || !length || !text) {
        LOG_WARN("SHELL", "Destino invalido ao montar caminho sysfs");
        return ERR_NULL;
    }
    text_length = kstrlen(text);
    if (*length > capacity || text_length >= capacity - *length) {
        LOG_WARN("SHELL", "Caminho sysfs excedeu o limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < text_length; index++) {
        path[*length + index] = text[index];
    }
    *length += text_length;
    path[*length] = '\0';
    return OK;
}

int cmd_sysfs_path_for_device(const device_info_t* info,
                              const device_text_t* text,
                              char* path, uint32_t capacity) {
    uint32_t length = 0U;
    int result;

    if (!info || !text || !path || capacity == 0U) {
        LOG_WARN("SHELL", "Argumento invalido ao resolver caminho sysfs");
        return ERR_NULL;
    }
    path[0] = '\0';
    if (info->kind == DEVICE_KIND_PCI) {
        result = cmd_sysfs_append(path, capacity, &length,
                                  "/sys/bus/pci/devices/");
        if (result == OK) result = cmd_sysfs_append(path, capacity, &length,
                                                     text->id + 4);
        return result;
    }
    if (info->kind == DEVICE_KIND_ATA_PRIMARY) {
        result = cmd_sysfs_append(path, capacity, &length,
                                  "/sys/class/block/");
        if (result == OK) result = cmd_sysfs_append(path, capacity, &length,
                                                     text->id);
        return result;
    }
    return ERR_NOT_FOUND;
}

int cmd_sysfs_path_for_id(const char* id, char* path, uint32_t capacity) {
    uint32_t length = 0U;
    const char* prefix;

    if (!id || !path || capacity == 0U) {
        LOG_WARN("SHELL", "Argumento invalido ao resolver ID sysfs");
        return ERR_NULL;
    }
    path[0] = '\0';
    if (id[0] == 'n' && id[1] == 'e' && id[2] == 't' && id[3] == '-') {
        if (!id[4]) {
            LOG_WARN("SHELL", "ID de rede sysfs sem identificador");
            return ERR_INVALID;
        }
        prefix = "/sys/class/net/";
        return cmd_sysfs_append(path, capacity, &length, prefix) == OK ?
               cmd_sysfs_append(path, capacity, &length, id) : ERR_OVERFLOW;
    }
    if (id[0] == 'p' && id[1] == 'c' && id[2] == 'i' && id[3] == '-') {
        if (!id[4]) {
            LOG_WARN("SHELL", "ID PCI sysfs sem identificador");
            return ERR_INVALID;
        }
        prefix = "/sys/bus/pci/devices/";
        if (cmd_sysfs_append(path, capacity, &length, prefix) != OK) {
            return ERR_OVERFLOW;
        }
        return cmd_sysfs_append(path, capacity, &length, id + 4);
    }
    if (id[0] != 'a' || id[1] != 't' || id[2] != 'a') {
        return ERR_NOT_FOUND;
    }
    if (!id[3]) {
        LOG_WARN("SHELL", "ID de bloco sysfs sem identificador");
        return ERR_INVALID;
    }
    prefix = "/sys/class/block/";
    if (cmd_sysfs_append(path, capacity, &length, prefix) != OK) {
        return ERR_OVERFLOW;
    }
    return cmd_sysfs_append(path, capacity, &length, id);
}

int cmd_proccheck_pid_path(char* path, uint32_t capacity, uint32_t pid,
                           const char* suffix) {
    uint32_t length = 0U;

    if (cmd_sysfs_append(path, capacity, &length, "/proc/") != OK) {
        LOG_WARN("SHELL", "Caminho proccheck excedeu o limite");
        return ERR_OVERFLOW;
    }
    {
        char digits[11];
        uint32_t count = 0U;
        do {
            digits[count++] = (char)('0' + pid % 10U);
            pid /= 10U;
        } while (pid && count < sizeof(digits));
        while (count) {
            char digit[2] = {digits[--count], '\0'};
            if (cmd_sysfs_append(path, capacity, &length, digit) != OK) {
                LOG_WARN("SHELL", "PID excedeu o caminho proccheck");
                return ERR_OVERFLOW;
            }
        }
    }
    return cmd_sysfs_append(path, capacity, &length, suffix);
}

uint8_t cmd_usb_recovery_color(recovery_state_t state) {
    if (state == RECOVERY_STATE_READY) return 0x0A;
    if (state == RECOVERY_STATE_DEGRADED) return 0x0E;
    if (state == RECOVERY_STATE_DISABLED) return 0x0C;
    return 0x08;
}

uint8_t cmd_usb_state_color(usb_controller_state_t state) {
    if (state == USB_CONTROLLER_READY) return 0x0A;
    if (state == USB_CONTROLLER_DEGRADED) return 0x0E;
    if (state == USB_CONTROLLER_DISABLED) return 0x0C;
    return 0x08;
}

uint8_t cmd_usb_port_color(usb_port_state_t state) {
    if (state == USB_PORT_CONFIGURED) return 0x0A;
    if (state == USB_PORT_DEGRADED) return 0x0E;
    if (state == USB_PORT_EMPTY) return 0x08;
    return 0x0B;
}

const char* cmd_acpi_mode_name(acpi_mode_t mode) {
    if (mode == ACPI_MODE_DISABLED) return "DESABILITADO";
    if (mode == ACPI_MODE_ENABLED) return "HABILITADO";
    if (mode == ACPI_MODE_INCONSISTENT) return "INCONSISTENTE";
    return "DESCONHECIDO";
}

const char* cmd_acpi_s5_name(acpi_s5_state_t state) {
    if (state == ACPI_S5_DECLARED) return "DECLARADO";
    if (state == ACPI_S5_MALFORMED) return "MALFORMADO";
    if (state == ACPI_S5_AMBIGUOUS) return "AMBIGUO";
    return "INDISPONIVEL";
}

const char* cmd_acpi_space_name(uint8_t space_id) {
    if (space_id == ACPI_ADDRESS_SPACE_SYSTEM_MEMORY) return "MEMORIA";
    if (space_id == ACPI_ADDRESS_SPACE_SYSTEM_IO) return "SYSTEM-IO";
    return "NAO SUPORTADO";
}

uint32_t shell_kmetrics_delta(uint32_t current, uint32_t baseline) {
    return current - baseline;
}

int shell_memcheck_same_layout(const memory_heap_stats_t* before,
                               const memory_heap_stats_t* after) {
    if (!before || !after) return 0;
    return before->total_bytes == after->total_bytes &&
           before->used_bytes == after->used_bytes &&
           before->free_bytes == after->free_bytes &&
           before->allocated_blocks == after->allocated_blocks &&
           before->free_blocks == after->free_blocks &&
           before->largest_free_block == after->largest_free_block &&
           before->fragmentation_percent == after->fragmentation_percent;
}

int shell_memcheck_same_memory_metrics(
    const memory_detailed_stats_t* before,
    const memory_detailed_stats_t* after) {
    if (!before || !after || before->total_pages != after->total_pages ||
        before->free_runs != after->free_runs ||
        before->largest_free_run != after->largest_free_run ||
        before->isolated_free_pages != after->isolated_free_pages ||
        before->fragmentation_percent != after->fragmentation_percent ||
        before->initialized != after->initialized ||
        before->valid != after->valid) {
        return 0;
    }
    for (memory_zone_t zone = MEMORY_ZONE_KERNEL;
         zone < MEMORY_ZONE_COUNT; zone++) {
        if (before->zone_pages[zone] != after->zone_pages[zone]) return 0;
    }
    return 1;
}

int shell_memcheck_valid_memory_metrics(
    const memory_detailed_stats_t* detailed,
    const memory_pmm_stats_t* pmm) {
    uint32_t zone_sum = 0U;

    if (!detailed || !pmm || !detailed->initialized || !detailed->valid ||
        !pmm->initialized || pmm->owned_pages > detailed->total_pages ||
        detailed->fragmentation_percent > 100U ||
        detailed->largest_free_run > detailed->zone_pages[MEMORY_ZONE_FREE] ||
        detailed->isolated_free_pages > detailed->free_runs ||
        detailed->free_runs > detailed->zone_pages[MEMORY_ZONE_FREE]) {
        return 0;
    }
    for (memory_zone_t zone = MEMORY_ZONE_KERNEL;
         zone < MEMORY_ZONE_COUNT; zone++) {
        zone_sum += detailed->zone_pages[zone];
    }
    return zone_sum == detailed->total_pages &&
           detailed->zone_pages[MEMORY_ZONE_FREE] == memory_get_free_pages();
}

const char* cmd_vmamap_area_type(const vm_area_info_t* area) {
    if (!area) return "INVALID";
    if (area->start_addr == USER_CODE_BASE) return "CODE";
    if (area->start_addr == USER_DATA_BASE) return "DATA";
    if (area->start_addr == USER_LAUNCH_BASE) return "LAUNCH";
    if (area->start_addr == USER_STACK_BASE) return "STACK";
    if (area->flags & VM_ANONYMOUS) return "ANON";
    return "OTHER";
}

int cmd_vmamap_parse_pid(const char* args, uint32_t* pid_out) {
    char token[12];
    uint32_t value = 0U;

    if (!pid_out) {
        LOG_WARN("SHELL", "Destino de PID ausente");
        return ERR_NULL;
    }
    if (shell_command_read_single_arg(args, token, sizeof(token)) != OK) {
        LOG_WARN("SHELL", "PID ausente ou com argumentos extras");
        return ERR_INVALID;
    }
    for (uint32_t i = 0U; token[i]; i++) {
        if (token[i] < '0' || token[i] > '9') {
            LOG_WARN("SHELL", "PID contem caractere invalido");
            return ERR_INVALID;
        }
        if (value > (MAX_PROCESSES - 1U) / 10U ||
            value * 10U > MAX_PROCESSES - 1U -
                (uint32_t)(token[i] - '0')) {
            LOG_WARN("SHELL", "PID excedeu o limite");
            return ERR_OVERFLOW;
        }
        value = value * 10U + (uint32_t)(token[i] - '0');
    }
    *pid_out = value;
    if (*pid_out == 0U) {
        LOG_WARN("SHELL", "PID zero nao e valido");
        return ERR_INVALID;
    }
    return OK;
}

int cmd_mouse_read_token(const char** cursor, char* token, int token_size) {
    const char* input;
    int length = 0;

    if (!cursor || !*cursor || !token || token_size < 2) {
        LOG_ERROR("SHELL", "Destino invalido no parser do comando mouse");
        return ERR_NULL;
    }
    input = *cursor;
    while (*input == ' ' || *input == '\t') input++;
    if (!*input) {
        LOG_ERROR("SHELL", "Argumento ausente no comando mouse");
        return ERR_INVALID;
    }
    while (*input && *input != ' ' && *input != '\t') {
        if (length >= token_size - 1) {
            LOG_ERROR("SHELL", "Argumento longo demais no comando mouse");
            return ERR_OVERFLOW;
        }
        token[length++] = *input++;
    }
    token[length] = '\0';
    *cursor = input;
    return OK;
}

int cmd_mouse_has_extra_args(const char* cursor) {
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (!*cursor) return 0;
    LOG_ERROR("SHELL", "Argumentos excedentes no comando mouse");
    return 1;
}

int cmd_mouse_parse_speed(const char* value, uint8_t* speed) {
    if (!value || !speed) {
        LOG_ERROR("SHELL", "Destino nulo ao interpretar velocidade do mouse");
        return ERR_NULL;
    }
    if (value[0] >= '1' && value[0] <= '9' && value[1] == '\0') {
        *speed = (uint8_t)(value[0] - '0');
        return OK;
    }
    if (value[0] == '1' && value[1] == '0' && value[2] == '\0') {
        *speed = MOUSE_SPEED_MAX;
        return OK;
    }
    LOG_ERROR("SHELL", "Velocidade invalida no comando mouse");
    return ERR_INVALID;
}

const char* cmd_vfs_node_name(vfs_node_type_t type) {
    if (type == VFS_NODE_REGULAR) return "FILE";
    if (type == VFS_NODE_STDIN) return "STDIN";
    if (type == VFS_NODE_STDOUT) return "STDOUT";
    if (type == VFS_NODE_STDERR) return "STDERR";
    if (type == VFS_NODE_DIRECTORY) return "DIR";
    if (type == VFS_NODE_CHAR_DEVICE) return "CHAR";
    if (type == VFS_NODE_BLOCK_DEVICE) return "BLOCK";
    if (type == VFS_NODE_PIPE) return "PIPE";
    if (type == VFS_NODE_SOCKET) return "SOCKET";
    if (type == VFS_NODE_TEST) return "TEST";
    return "NONE";
}
