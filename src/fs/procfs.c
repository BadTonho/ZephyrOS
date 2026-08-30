#include "fs/procfs.h"
#include "fs/vfs_internal.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/version.h"
#include "fs/block_cache.h"
#include "memory/slab.h"
#include "process/process.h"

#define PROCFS_GLOBAL_COUNT 5U
#define PROCFS_SYS_CONTROL_COUNT 2U
#define PROCFS_PID_TEXT_SIZE 11U
#define PROCFS_CPU_VENDOR_SIZE 13U
#define PROCFS_CPU_BRAND_SIZE 49U
#define PROCFS_DEFAULT_LOG_LEVEL LOG_LEVEL_INFO

typedef enum {
    PROCFS_GLOBAL_UPTIME = 0,
    PROCFS_GLOBAL_MEMINFO,
    PROCFS_GLOBAL_CPUINFO,
    PROCFS_GLOBAL_VERSION,
    PROCFS_GLOBAL_CMDLINE
} procfs_global_kind_t;

typedef enum {
    PROCFS_CONTROL_CONSOLE_LOG_LEVEL = 0,
    PROCFS_CONTROL_BUFFER_LOG_LEVEL
} procfs_control_kind_t;

typedef struct {
    proc_entry_t entry;
    uint32_t index;
} procfs_entry_snapshot_t;

typedef struct {
    procfs_node_kind_t kind;
    uint32_t entry_index;
    uint32_t control_index;
    uint32_t pid;
} procfs_node_ref_t;

static int procfs_global_read(char* buffer, uint32_t capacity,
                              uint32_t* out_len, void* data);
static int procfs_open(vnode_t* vnode, file_t* file);
static int procfs_read(file_t* file, void* buffer, uint32_t size,
                       uint32_t* bytes_read);
static int procfs_write(file_t* file, const void* buffer, uint32_t size,
                        uint32_t* bytes_written);
static int procfs_close(file_t* file);
static int procfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                        uint32_t* position);
static int procfs_ioctl(file_t* file, uint32_t request, void* argument);
static int procfs_sync(file_t* file);
static int procfs_poll(file_t* file, uint32_t events, uint32_t* revents);
static int procfs_render_snapshot(const proc_entry_t* entry,
                                  procfs_file_context_t* context);
static int procfs_render_process(const procfs_node_ref_t* node,
                                 procfs_file_context_t* context);
static int procfs_control_read(char* buffer, uint32_t capacity,
                               uint32_t* out_len, void* data);
static int procfs_control_write(const char* buffer, uint32_t len,
                                void* data);

static uint32_t procfs_global_uptime = PROCFS_GLOBAL_UPTIME;
static uint32_t procfs_global_meminfo = PROCFS_GLOBAL_MEMINFO;
static uint32_t procfs_global_cpuinfo = PROCFS_GLOBAL_CPUINFO;
static uint32_t procfs_global_version = PROCFS_GLOBAL_VERSION;
static uint32_t procfs_global_cmdline = PROCFS_GLOBAL_CMDLINE;
static uint32_t procfs_control_console = PROCFS_CONTROL_CONSOLE_LOG_LEVEL;
static uint32_t procfs_control_buffer = PROCFS_CONTROL_BUFFER_LOG_LEVEL;

static const proc_entry_t procfs_entries[] = {
    {"uptime", VFS_MODE_READ, procfs_global_read, 0,
     (void*)&procfs_global_uptime},
    {"meminfo", VFS_MODE_READ, procfs_global_read, 0,
     (void*)&procfs_global_meminfo},
    {"cpuinfo", VFS_MODE_READ, procfs_global_read, 0,
     (void*)&procfs_global_cpuinfo},
    {"version", VFS_MODE_READ, procfs_global_read, 0,
     (void*)&procfs_global_version},
    {"cmdline", VFS_MODE_READ, procfs_global_read, 0,
     (void*)&procfs_global_cmdline}
};

static const proc_entry_t procfs_control_entries[] = {
    {"console_log_level", VFS_MODE_READ_WRITE, procfs_control_read,
     procfs_control_write, (void*)&procfs_control_console},
    {"buffer_log_level", VFS_MODE_READ_WRITE, procfs_control_read,
     procfs_control_write, (void*)&procfs_control_buffer}
};

static const file_operations_t procfs_operations = {
    procfs_open, procfs_read, procfs_write, procfs_close, procfs_lseek,
    procfs_ioctl, procfs_sync, procfs_poll
};

static spinlock_t procfs_lock;
static uint8_t procfs_ready;
static uint32_t procfs_active_snapshots;

static void procfs_test_count(procfs_test_result_t* result, uint8_t passed) {
    result->total++;
    if (passed) result->passed++;
}

static void procfs_copy_text(char* destination, uint32_t capacity,
                             const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int procfs_append_char(char* buffer, uint32_t capacity,
                              uint32_t* length, char value) {
    if (!buffer || !length) {
        LOG_ERROR("PROCFS", "Destino nulo ao serializar procfs");
        return ERR_NULL;
    }
    if (*length >= capacity) {
        LOG_WARN("PROCFS", "Snapshot procfs excedeu a capacidade");
        return ERR_OVERFLOW;
    }
    buffer[*length] = value;
    (*length)++;
    return OK;
}

static int procfs_append_text(char* buffer, uint32_t capacity,
                              uint32_t* length, const char* text) {
    uint32_t index = 0U;
    int result;

    if (!text) {
        LOG_ERROR("PROCFS", "Texto nulo ao serializar procfs");
        return ERR_NULL;
    }
    while (text[index]) {
        result = procfs_append_char(buffer, capacity, length, text[index]);
        if (result != OK) return result;
        index++;
    }
    return OK;
}

static int procfs_append_decimal(char* buffer, uint32_t capacity,
                                 uint32_t* length, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    int result;

    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count) {
        count--;
        result = procfs_append_char(buffer, capacity, length, digits[count]);
        if (result != OK) return result;
    }
    return OK;
}

static int procfs_append_hex(char* buffer, uint32_t capacity,
                             uint32_t* length, uint32_t value) {
    static const char digits[] = "0123456789abcdef";
    int result;

    result = procfs_append_text(buffer, capacity, length, "0x");
    for (int shift = 28; result == OK && shift >= 0; shift -= 4) {
        result = procfs_append_char(buffer, capacity, length,
                                    digits[(value >> shift) & 0x0FU]);
    }
    return result;
}

static int procfs_append_line_decimal(char* buffer, uint32_t capacity,
                                      uint32_t* length, const char* key,
                                      uint32_t value) {
    int result = procfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = procfs_append_char(buffer, capacity, length,
                                                   ' ');
    if (result == OK) result = procfs_append_decimal(buffer, capacity, length,
                                                     value);
    if (result == OK) result = procfs_append_char(buffer, capacity, length,
                                                   '\n');
    return result;
}

static int procfs_append_line_hex(char* buffer, uint32_t capacity,
                                  uint32_t* length, const char* key,
                                  uint32_t value) {
    int result = procfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = procfs_append_char(buffer, capacity, length,
                                                   ' ');
    if (result == OK) result = procfs_append_hex(buffer, capacity, length,
                                                 value);
    if (result == OK) result = procfs_append_char(buffer, capacity, length,
                                                   '\n');
    return result;
}

static int procfs_append_line_text(char* buffer, uint32_t capacity,
                                   uint32_t* length, const char* key,
                                   const char* value) {
    int result = procfs_append_text(buffer, capacity, length, key);

    if (result == OK) result = procfs_append_char(buffer, capacity, length,
                                                   ' ');
    if (result == OK && value) {
        for (uint32_t index = 0U; value[index] && result == OK; index++) {
            char value_char = value[index];

            if ((uint8_t)value_char < 0x20U || value_char == 0x7FU) {
                value_char = '?';
            }
            result = procfs_append_char(buffer, capacity, length, value_char);
        }
    }
    if (result == OK) result = procfs_append_char(buffer, capacity, length,
                                                   '\n');
    return result;
}

static int procfs_uptime_read(char* buffer, uint32_t capacity,
                              uint32_t* out_len) {
    process_snapshot_t idle;
    uint32_t frequency;
    uint32_t ticks;
    int result;

    if (!buffer || !out_len) {
        LOG_ERROR("PROCFS", "Destino nulo no callback de uptime");
        return ERR_NULL;
    }
    *out_len = 0U;
    frequency = timer_get_frequency();
    if (!frequency) {
        LOG_WARN("PROCFS", "Frequencia do timer indisponivel");
        return ERR_UNAVAILABLE;
    }
    result = process_snapshot_copy(0U, &idle);
    if (result != OK) return result;
    ticks = timer_get_ticks();
    result = procfs_append_line_decimal(buffer, capacity, out_len,
                                        "uptime_ticks", ticks);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "frequency_hz", frequency);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "uptime_seconds", ticks / frequency);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "idle_ticks", idle.total_ticks);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "idle_seconds", idle.total_ticks / frequency);
    return result;
}

static int procfs_meminfo_read(char* buffer, uint32_t capacity,
                               uint32_t* out_len) {
    kmem_slab_stats_t slab;
    block_cache_stats_t cache;
    uint32_t slab_bytes;
    int result;

    if (!buffer || !out_len) {
        LOG_ERROR("PROCFS", "Destino nulo no callback de meminfo");
        return ERR_NULL;
    }
    kmem_cache_get_stats(&slab);
    result = block_cache_get_stats(&cache);
    if (result != OK) {
        LOG_ERROR("PROCFS", "Falha ao consultar cache de blocos");
        return result;
    }
    if (slab.pages > 0xFFFFFFFFU / PAGE_SIZE) {
        LOG_ERROR("PROCFS", "Slab excedeu memoria publicada");
        return ERR_OVERFLOW;
    }
    slab_bytes = slab.pages * PAGE_SIZE;
    *out_len = 0U;
    result = procfs_append_line_decimal(buffer, capacity, out_len,
                                        "total_bytes", memory_get_total());
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "free_bytes", memory_get_free());
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "used_bytes", memory_get_used());
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "slab_bytes", slab_bytes);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, out_len, "buffers_bytes", cache.memory_bytes);
    return result;
}

static int procfs_cpuid_supported(void) {
    uint32_t original;
    uint32_t toggled;
    uint32_t current;

    asm volatile("pushfl\n\tpopl %0" : "=r"(original));
    toggled = original ^ (1U << 21U);
    asm volatile("pushl %0\n\tpopfl" : : "r"(toggled) : "cc");
    asm volatile("pushfl\n\tpopl %0" : "=r"(current));
    asm volatile("pushl %0\n\tpopfl" : : "r"(original) : "cc");
    return ((current ^ original) & (1U << 21U)) != 0U;
}

static void procfs_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax,
                         uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    asm volatile("cpuid"
                 : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                 : "a"(leaf), "c"(subleaf));
}

static int procfs_cpuinfo_read(char* buffer, uint32_t capacity,
                               uint32_t* out_len) {
    static const char* const edx_flag_names[] = {
        "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
        "cx8", "apic", "reserved10", "sep", "mtrr", "pge", "mca",
        "cmov", "pat", "pse36", "psn", "clfsh", "ds", "acpi", "mmx",
        "fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", "pbe",
        "reserved31"
    };
    static const char* const ecx_flag_names[] = {
        "sse3", "pclmulqdq", "dtes64", "monitor", "ds_cpl", "vmx",
        "smx", "est", "tm2", "ssse3", "cnxt_id", "sdbg", "fma", "cx16",
        "xtpr", "pdcm"
    };
    uint32_t max_leaf = 0U;
    uint32_t max_extended = 0U;
    uint32_t eax = 0U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    uint32_t cpu_eax = 0U;
    uint32_t cpu_ecx = 0U;
    uint32_t cpu_edx = 0U;
    uint32_t length = 0U;
    uint32_t frequency_hz = 0U;
    char vendor[PROCFS_CPU_VENDOR_SIZE];
    char brand[PROCFS_CPU_BRAND_SIZE];
    uint32_t brand_words[12];
    int result;

    if (!buffer || !out_len) {
        LOG_ERROR("PROCFS", "Destino nulo no callback de cpuinfo");
        return ERR_NULL;
    }
    *out_len = 0U;
    kmemset(vendor, 0, sizeof(vendor));
    kmemset(brand, 0, sizeof(brand));
    kmemset(brand_words, 0, sizeof(brand_words));
    if (procfs_cpuid_supported()) {
        procfs_cpuid(0U, 0U, &max_leaf, &ebx, &ecx, &edx);
        kmemcpy(vendor, &ebx, sizeof(ebx));
        kmemcpy(vendor + 4U, &edx, sizeof(edx));
        kmemcpy(vendor + 8U, &ecx, sizeof(ecx));
        if (max_leaf >= 1U) {
            procfs_cpuid(1U, 0U, &eax, &ebx, &ecx, &edx);
            cpu_eax = eax;
            cpu_ecx = ecx;
            cpu_edx = edx;
        }
        procfs_cpuid(0x80000000U, 0U, &max_extended, &ebx, &ecx, &edx);
        if (max_extended >= 0x80000004U) {
            procfs_cpuid(0x80000002U, 0U, &brand_words[0], &brand_words[1],
                         &brand_words[2], &brand_words[3]);
            procfs_cpuid(0x80000003U, 0U, &brand_words[4], &brand_words[5],
                         &brand_words[6], &brand_words[7]);
            procfs_cpuid(0x80000004U, 0U, &brand_words[8], &brand_words[9],
                         &brand_words[10], &brand_words[11]);
            kmemcpy(brand, brand_words, sizeof(brand_words));
            brand[sizeof(brand) - 1U] = '\0';
        }
        if (max_leaf >= 0x16U) {
            uint32_t base_mhz;

            procfs_cpuid(0x16U, 0U, &base_mhz, &ebx, &ecx, &edx);
            if (base_mhz && base_mhz <= 0xFFFFFFFFU / 1000000U) {
                frequency_hz = base_mhz * 1000000U;
            }
        }
    }
    if (!vendor[0]) procfs_copy_text(vendor, sizeof(vendor), "unknown");
    if (!brand[0]) procfs_copy_text(brand, sizeof(brand), vendor);
    result = procfs_append_line_decimal(buffer, capacity, &length,
                                        "processor", 0U);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, &length, "vendor_id", vendor);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, &length, "cpu_family", (cpu_eax >> 8) & 0x0FU);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, &length, "model", (cpu_eax >> 4) & 0x0FU);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, &length, "model_name", brand);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, &length, "stepping", cpu_eax & 0x0FU);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, &length, "frequency_hz", frequency_hz);
    if (result == OK) {
        uint32_t flags_present = 0U;

        result = procfs_append_text(buffer, capacity, &length, "flags ");
        for (uint32_t bit = 0U; result == OK && bit < 32U; bit++) {
            if (cpu_edx & (1U << bit)) {
                result = procfs_append_text(buffer, capacity, &length,
                                            edx_flag_names[bit]);
                if (result == OK) result = procfs_append_char(
                    buffer, capacity, &length, ' ');
                flags_present = 1U;
            }
        }
        for (uint32_t bit = 0U; result == OK && bit < 16U; bit++) {
            if (cpu_ecx & (1U << bit)) {
                result = procfs_append_text(buffer, capacity, &length,
                                            ecx_flag_names[bit]);
                if (result == OK) result = procfs_append_char(
                    buffer, capacity, &length, ' ');
                flags_present = 1U;
            }
        }
        if (result == OK && !flags_present) result = procfs_append_text(
            buffer, capacity, &length, "none");
        if (result == OK) result = procfs_append_char(buffer, capacity,
                                                       &length, '\n');
    }
    *out_len = length;
    return result;
}

static int procfs_version_read(char* buffer, uint32_t capacity,
                               uint32_t* out_len) {
    uint32_t length = 0U;
    int result;

    if (!buffer || !out_len) {
        LOG_ERROR("PROCFS", "Destino nulo no callback de version");
        return ERR_NULL;
    }
    result = procfs_append_line_text(buffer, capacity, &length, "version",
                                     ZEPHYROS_VERSION_TEXT);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, &length, "build_date", __DATE__);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, &length, "build_time", __TIME__);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, &length, "compiler", __VERSION__);
    *out_len = length;
    return result;
}

static int procfs_cmdline_read(char* buffer, uint32_t capacity,
                               uint32_t* out_len) {
    if (!buffer || !out_len) {
        LOG_ERROR("PROCFS", "Destino nulo no callback de cmdline");
        return ERR_NULL;
    }
    *out_len = 0U;
    return procfs_append_line_text(buffer, capacity, out_len, "cmdline", "");
}

static int procfs_global_read(char* buffer, uint32_t capacity,
                              uint32_t* out_len, void* data) {
    uint32_t kind;

    if (!data) {
        LOG_ERROR("PROCFS", "Contexto ausente no callback global");
        return ERR_NULL;
    }
    kind = *(uint32_t*)data;
    if (kind == PROCFS_GLOBAL_UPTIME) return procfs_uptime_read(
        buffer, capacity, out_len);
    if (kind == PROCFS_GLOBAL_MEMINFO) return procfs_meminfo_read(
        buffer, capacity, out_len);
    if (kind == PROCFS_GLOBAL_CPUINFO) return procfs_cpuinfo_read(
        buffer, capacity, out_len);
    if (kind == PROCFS_GLOBAL_VERSION) return procfs_version_read(
        buffer, capacity, out_len);
    if (kind == PROCFS_GLOBAL_CMDLINE) return procfs_cmdline_read(
        buffer, capacity, out_len);
    LOG_ERROR("PROCFS", "Tipo de node global invalido");
    return ERR_INVALID;
}

static const char* procfs_log_level_name(log_level_t level) {
    static const char* const names[] = {
        "error", "warn", "info", "debug"
    };

    if ((uint32_t)level >= sizeof(names) / sizeof(names[0])) return "unknown";
    return names[level];
}

static int procfs_control_get(uint32_t control, log_level_t* level) {
    if (!level || control >= PROCFS_SYS_CONTROL_COUNT) {
        LOG_ERROR("PROCFS", "Controle de log invalido na leitura");
        return ERR_INVALID;
    }
    if (control == PROCFS_CONTROL_CONSOLE_LOG_LEVEL) {
        *level = log_get_console_level();
    } else {
        *level = log_get_buffer_level();
    }
    if ((uint32_t)*level > (uint32_t)LOG_LEVEL_DEBUG) {
        LOG_ERROR("PROCFS", "Nivel de log inconsistente no controle");
        return ERR_STATE;
    }
    return OK;
}

static int procfs_control_set(uint32_t control, log_level_t level) {
    if (control >= PROCFS_SYS_CONTROL_COUNT) {
        LOG_ERROR("PROCFS", "Controle de log invalido na escrita");
        return ERR_INVALID;
    }
    if (control == PROCFS_CONTROL_CONSOLE_LOG_LEVEL) {
        return log_set_console_level(level);
    }
    return log_set_buffer_level(level);
}

static int procfs_control_parse_level(const char* buffer, uint32_t len,
                                      log_level_t* level) {
    static const char* const names[] = {
        "error", "warn", "info", "debug"
    };
    char token[PROCFS_SYS_CONTROL_MAX_INPUT];
    uint32_t token_length = 0U;

    if (!level || (len && !buffer)) {
        LOG_ERROR("PROCFS", "Entrada nula no controle de log");
        return ERR_NULL;
    }
    if (!len) {
        LOG_WARN("PROCFS", "Valor vazio no controle de log");
        return ERR_INVALID;
    }
    if (len > PROCFS_SYS_CONTROL_MAX_INPUT) {
        LOG_WARN("PROCFS", "Valor do controle de log excedeu o limite");
        return ERR_OVERFLOW;
    }
    for (uint32_t index = 0U; index < len; index++) {
        uint8_t value = (uint8_t)buffer[index];

        if (value == '\n') {
            if (index + 1U != len) {
                LOG_WARN("PROCFS", "Valor do controle possui linhas extras");
                return ERR_INVALID;
            }
            continue;
        }
        if (value == '\r' || value == 0x1BU || value < 0x20U ||
            value > 0x7FU || token_length + 1U >= sizeof(token)) {
            LOG_WARN("PROCFS", "Valor do controle de log invalido");
            return ERR_INVALID;
        }
        token[token_length++] = (char)value;
    }
    if (!token_length) {
        LOG_WARN("PROCFS", "Token vazio no controle de log");
        return ERR_INVALID;
    }
    token[token_length] = '\0';
    for (uint32_t index = 0U; index < sizeof(names) / sizeof(names[0]);
         index++) {
        if (kstrcmp(token, names[index]) == 0) {
            *level = (log_level_t)index;
            return OK;
        }
    }
    LOG_WARN("PROCFS", "Token desconhecido no controle de log");
    return ERR_INVALID;
}

static int procfs_control_read(char* buffer, uint32_t capacity,
                               uint32_t* out_len, void* data) {
    uint32_t control;
    log_level_t level;
    int result;

    if (!data) {
        LOG_ERROR("PROCFS", "Contexto ausente no callback de controle");
        return ERR_NULL;
    }
    control = *(uint32_t*)data;
    result = procfs_control_get(control, &level);
    if (result != OK) return result;
    return procfs_append_line_text(buffer, capacity, out_len,
                                   procfs_control_entries[control].name,
                                   procfs_log_level_name(level));
}

static int procfs_control_write(const char* buffer, uint32_t len,
                                void* data) {
    uint32_t control;
    log_level_t level;
    int result;

    if (!data) {
        LOG_ERROR("PROCFS", "Contexto ausente na escrita de controle");
        return ERR_NULL;
    }
    control = *(uint32_t*)data;
    result = procfs_control_parse_level(buffer, len, &level);
    if (result != OK) return result;
    return procfs_control_set(control, level);
}

static int procfs_parse_pid(const char* text, uint32_t length,
                            uint32_t* pid) {
    uint32_t value = 0U;

    if (!text || !pid || !length || length >= PROCFS_PID_TEXT_SIZE) {
        LOG_WARN("PROCFS", "Texto de PID invalido");
        return ERR_INVALID;
    }
    for (uint32_t index = 0U; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') {
            LOG_WARN("PROCFS", "Digito invalido em PID procfs");
            return ERR_INVALID;
        }
        if (value > (0xFFFFFFFFU - (uint32_t)(text[index] - '0')) / 10U) {
            LOG_WARN("PROCFS", "PID procfs excedeu limite");
            return ERR_OVERFLOW;
        }
        value = value * 10U + (uint32_t)(text[index] - '0');
    }
    *pid = value;
    return OK;
}

static int procfs_parse_path(const char* canonical_path,
                             procfs_node_ref_t* node) {
    const char* suffix;
    const char* separator = 0;
    uint32_t canonical_length;
    uint32_t pid_length;
    process_snapshot_t process;
    int result;

    if (!canonical_path || !node) {
        LOG_ERROR("PROCFS", "Argumento nulo ao interpretar caminho procfs");
        return ERR_NULL;
    }
    kmemset(node, 0, sizeof(*node));
    canonical_length = kstrlen(canonical_path);
    if (canonical_length < 6U || canonical_path[0] != '/' ||
        canonical_path[1] != 'p' || canonical_path[2] != 'r' ||
        canonical_path[3] != 'o' || canonical_path[4] != 'c' ||
        canonical_path[5] != '/') {
        LOG_WARN("PROCFS", "Prefixo de caminho procfs invalido");
        return ERR_INVALID;
    }
    suffix = canonical_path + 6U;
    if (kstrcmp(canonical_path, "/proc") == 0) return ERR_INVALID;
    if (kstrcmp(canonical_path, "/proc/uptime") == 0) {
        node->kind = PROCFS_NODE_GLOBAL;
        node->entry_index = PROCFS_GLOBAL_UPTIME;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/meminfo") == 0) {
        node->kind = PROCFS_NODE_GLOBAL;
        node->entry_index = PROCFS_GLOBAL_MEMINFO;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/cpuinfo") == 0) {
        node->kind = PROCFS_NODE_GLOBAL;
        node->entry_index = PROCFS_GLOBAL_CPUINFO;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/version") == 0) {
        node->kind = PROCFS_NODE_GLOBAL;
        node->entry_index = PROCFS_GLOBAL_VERSION;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/cmdline") == 0) {
        node->kind = PROCFS_NODE_GLOBAL;
        node->entry_index = PROCFS_GLOBAL_CMDLINE;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/sys") == 0) {
        node->kind = PROCFS_NODE_SYS_DIRECTORY;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/sys/kernel") == 0) {
        node->kind = PROCFS_NODE_SYS_KERNEL_DIRECTORY;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/sys/kernel/console_log_level") == 0) {
        node->kind = PROCFS_NODE_SYS_CONTROL;
        node->control_index = PROCFS_CONTROL_CONSOLE_LOG_LEVEL;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/sys/kernel/buffer_log_level") == 0) {
        node->kind = PROCFS_NODE_SYS_CONTROL;
        node->control_index = PROCFS_CONTROL_BUFFER_LOG_LEVEL;
        return OK;
    }
    if (canonical_length >= 9U && canonical_path[0] == '/' &&
        canonical_path[1] == 'p' && canonical_path[2] == 'r' &&
        canonical_path[3] == 'o' && canonical_path[4] == 'c' &&
        canonical_path[5] == '/' && canonical_path[6] == 's' &&
        canonical_path[7] == 'y' && canonical_path[8] == 's' &&
        (canonical_length == 9U || canonical_path[9] == '/')) {
        return canonical_length == 10U ? ERR_INVALID : ERR_NOT_FOUND;
    }
    for (uint32_t index = 0U; suffix[index]; index++) {
        if (suffix[index] == '/') {
            separator = suffix + index;
            break;
        }
    }
    pid_length = separator ? (uint32_t)(separator - suffix) : kstrlen(suffix);
    result = procfs_parse_pid(suffix, pid_length, &node->pid);
    if (result != OK) return result;
    result = process_snapshot_copy(node->pid, &process);
    if (result != OK) return result;
    if (!separator) {
        node->kind = PROCFS_NODE_PROCESS_DIRECTORY;
        return OK;
    }
    if (!separator[1]) return ERR_INVALID;
    if (kstrcmp(separator + 1U, "status") == 0) {
        node->kind = PROCFS_NODE_PROCESS_STATUS;
    } else if (kstrcmp(separator + 1U, "cmdline") == 0) {
        node->kind = PROCFS_NODE_PROCESS_CMDLINE;
    } else if (kstrcmp(separator + 1U, "maps") == 0) {
        node->kind = PROCFS_NODE_PROCESS_MAPS;
    } else {
        return ERR_NOT_FOUND;
    }
    return OK;
}

static int procfs_entry_find(const char* canonical_path,
                             procfs_entry_snapshot_t* snapshot) {
    procfs_node_ref_t node;
    int result;

    if (!canonical_path || !snapshot) {
        LOG_ERROR("PROCFS", "Argumento nulo no lookup procfs");
        return ERR_NULL;
    }
    result = procfs_parse_path(canonical_path, &node);
    if (result != OK || node.kind != PROCFS_NODE_GLOBAL ||
        node.entry_index >= PROCFS_GLOBAL_COUNT) return ERR_NOT_FOUND;
    spinlock_acquire(&procfs_lock);
    if (!procfs_ready) {
        spinlock_release(&procfs_lock);
        LOG_ERROR("PROCFS", "Lookup procfs antes da inicializacao");
        return ERR_STATE;
    }
    snapshot->entry = procfs_entries[node.entry_index];
    snapshot->index = node.entry_index;
    spinlock_release(&procfs_lock);
    return OK;
}

int procfs_init(void) {
    LOG_INFO("PROCFS", "Inicializando procfs");
    spinlock_init(&procfs_lock);
    spinlock_acquire(&procfs_lock);
    procfs_active_snapshots = 0U;
    procfs_ready = 1U;
    spinlock_release(&procfs_lock);
    LOG_INFO("PROCFS", "Procfs inicializado com controles de runtime");
    return OK;
}

int procfs_is_ready(void) {
    return procfs_ready;
}

int procfs_reset_controls(void) {
    if (!procfs_ready) {
        LOG_ERROR("PROCFS", "Reset procfs antes da inicializacao");
        return ERR_STATE;
    }
    log_set_level(PROCFS_DEFAULT_LOG_LEVEL);
    return OK;
}

int procfs_lookup(const char* canonical_path, vfs_lookup_result_t* result) {
    procfs_node_ref_t node;
    int status;

    if (!canonical_path || !result) {
        LOG_ERROR("PROCFS", "Destino nulo no lookup publico procfs");
        return ERR_NULL;
    }
    if (!procfs_ready) {
        LOG_ERROR("PROCFS", "Lookup publico procfs antes da inicializacao");
        return ERR_STATE;
    }
    if (kstrcmp(canonical_path, "/proc") == 0) {
        result->type = VFS_NODE_DIRECTORY;
        result->size = 0U;
        result->attributes = 0U;
        result->read_only = 1U;
        result->relative_path[0] = '\0';
        return OK;
    }
    status = procfs_parse_path(canonical_path, &node);
    if (status != OK) return status;
    result->size = 0U;
    result->attributes = 0U;
    result->read_only = node.kind == PROCFS_NODE_SYS_CONTROL ? 0U : 1U;
    procfs_copy_text(result->relative_path, VFS_MAX_PATH,
                     canonical_path + 6U);
    result->type = node.kind == PROCFS_NODE_PROCESS_DIRECTORY ||
                   node.kind == PROCFS_NODE_SYS_DIRECTORY ||
                   node.kind == PROCFS_NODE_SYS_KERNEL_DIRECTORY ?
                   VFS_NODE_DIRECTORY : VFS_NODE_REGULAR;
    return OK;
}

static int procfs_render_snapshot(const proc_entry_t* entry,
                                  procfs_file_context_t* context) {
    uint32_t length = 0U;
    int result;

    if (!entry || !context || !entry->read_proc) {
        LOG_ERROR("PROCFS", "Callback procfs invalido");
        return ERR_INVALID;
    }
    context->snapshot = (uint8_t*)kmalloc(PROCFS_MAX_SNAPSHOT_SIZE);
    if (!context->snapshot) {
        LOG_ERROR("PROCFS", "Falha ao alocar snapshot procfs");
        return ERR_MEM;
    }
    result = entry->read_proc((char*)context->snapshot,
                              PROCFS_MAX_SNAPSHOT_SIZE, &length,
                              entry->data);
    if (result != OK || length > PROCFS_MAX_SNAPSHOT_SIZE) {
        if (result == OK) result = ERR_OVERFLOW;
        LOG_ERROR("PROCFS", "Snapshot procfs recusado pelo callback");
        kfree(context->snapshot);
        context->snapshot = 0;
        context->snapshot_size = 0U;
        return result;
    }
    context->snapshot_size = length;
    spinlock_acquire(&procfs_lock);
    procfs_active_snapshots++;
    spinlock_release(&procfs_lock);
    return OK;
}

static int procfs_render_process_status(const process_snapshot_t* process,
                                        char* buffer, uint32_t capacity,
                                        uint32_t* length) {
    static const char* const states[] = {
        "unused", "ready", "running", "blocked", "zombie"
    };
    const char* state = "unknown";
    int result;

    if (process->state <= PROCESS_STATE_ZOMBIE) state = states[process->state];
    result = procfs_append_line_decimal(buffer, capacity, length, "pid",
                                        process->pid);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "generation", process->generation);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, length, "name", process->name);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, length, "state", state);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "ppid", process->parent_pid);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "threads", process->threads);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "total_ticks", process->total_ticks);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "wait_ticks", process->wait_ticks);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "memory_bytes", process->memory_bytes);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "resident_pages", process->resident_pages);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "image_bytes", process->image_bytes);
    if (result == OK) result = procfs_append_line_text(
        buffer, capacity, length, "user",
        process->user_mode ? "ring3" : "kernel");
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "exit_code", process->exit_code);
    if (result == OK) result = procfs_append_line_decimal(
        buffer, capacity, length, "faulted", process->faulted);
    return result;
}

static int procfs_render_process(const procfs_node_ref_t* node,
                                 procfs_file_context_t* context) {
    process_snapshot_t process;
    uint32_t length = 0U;
    int result;

    if (!node || !context || !context->snapshot) {
        LOG_ERROR("PROCFS", "Contexto invalido ao renderizar processo");
        return ERR_NULL;
    }
    result = process_snapshot_copy(node->pid, &process);
    if (result != OK) return result == ERR_NOT_FOUND ? ERR_AGAIN : result;
    context->process_pid = process.pid;
    context->process_generation = process.generation;
    if (node->kind == PROCFS_NODE_PROCESS_STATUS) {
        result = procfs_render_process_status(&process,
                                              (char*)context->snapshot,
                                              PROCFS_MAX_SNAPSHOT_SIZE,
                                              &length);
        context->snapshot_size = length;
        return result;
    }
    if (node->kind == PROCFS_NODE_PROCESS_CMDLINE) {
        const char* value = process.user_mode ? process.user_launch.raw_args :
                             process.name;

        result = procfs_append_line_text((char*)context->snapshot,
                                         PROCFS_MAX_SNAPSHOT_SIZE, &length,
                                         "cmdline", value);
        context->snapshot_size = length;
        return result;
    }
    if (node->kind == PROCFS_NODE_PROCESS_MAPS) {
        vm_area_info_t* areas = 0;
        uint32_t area_count = 0U;
        uint8_t captured = 0U;

        for (uint32_t attempt = 0U; attempt < 2U && !captured; attempt++) {
            result = process_snapshot_copy(node->pid, &process);
            if (result != OK) break;
            context->process_generation = process.generation;
            area_count = process.vma_count;
            if (area_count > 0U) {
                if (area_count > 0xFFFFFFFFU / sizeof(vm_area_info_t)) {
                    result = ERR_OVERFLOW;
                    break;
                }
                areas = (vm_area_info_t*)kmalloc(
                    area_count * sizeof(vm_area_info_t));
                if (!areas) {
                    result = ERR_MEM;
                    break;
                }
            }
            result = process_snapshot_copy_vmas(node->pid,
                                                process.generation, areas,
                                                area_count, &area_count);
            if (result == ERR_AGAIN) {
                if (areas) kfree(areas);
                areas = 0;
                continue;
            }
            if (result != OK) break;
            captured = 1U;
        }
        if (result == OK && captured) {
            for (uint32_t index = 0U; index < area_count && result == OK;
                 index++) {
                result = procfs_append_line_hex(
                    (char*)context->snapshot, PROCFS_MAX_SNAPSHOT_SIZE,
                    &length, "vma_start", areas[index].start_addr);
                if (result == OK) result = procfs_append_line_hex(
                    (char*)context->snapshot, PROCFS_MAX_SNAPSHOT_SIZE,
                    &length, "vma_end", areas[index].end_addr);
                if (result == OK) result = procfs_append_line_hex(
                    (char*)context->snapshot, PROCFS_MAX_SNAPSHOT_SIZE,
                    &length, "vma_flags", areas[index].flags);
                if (result == OK) result = procfs_append_line_hex(
                    (char*)context->snapshot, PROCFS_MAX_SNAPSHOT_SIZE,
                    &length, "vma_offset", areas[index].offset);
            }
        }
        if (areas) kfree(areas);
        if (result == ERR_NOT_FOUND) return ERR_AGAIN;
        if (result != OK) return result;
        context->snapshot_size = length;
        return OK;
    }
    return ERR_INVALID;
}

static int procfs_write_allowed(void) {
    process_t* process = process_get_current();

    if (!process) {
        LOG_ERROR("PROCFS", "Processo atual ausente na escrita procfs");
        return ERR_STATE;
    }
    if (process_is_user(process)) {
        LOG_WARN("PROCFS", "Escrita procfs recusada para processo ring3");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

int procfs_open_file(const vfs_lookup_result_t* lookup, uint32_t mode,
                     vnode_t* vnode, file_t* file,
                     procfs_file_context_t* context) {
    procfs_entry_snapshot_t entry;
    procfs_node_ref_t node;
    int result;

    if (!lookup || !vnode || !file || !context) {
        LOG_ERROR("PROCFS", "Argumento nulo na abertura procfs");
        return ERR_NULL;
    }
    if (lookup->mount_kind != VFS_MOUNT_PROCFS) {
        LOG_ERROR("PROCFS", "Montagem invalida na abertura procfs");
        return ERR_INVALID;
    }
    result = procfs_parse_path(lookup->canonical_path, &node);
    if (result != OK) return result;
    if (node.kind == PROCFS_NODE_PROCESS_DIRECTORY ||
        node.kind == PROCFS_NODE_SYS_DIRECTORY ||
        node.kind == PROCFS_NODE_SYS_KERNEL_DIRECTORY) {
        return ERR_INVALID;
    }
    if (node.kind != PROCFS_NODE_SYS_CONTROL && mode != VFS_MODE_READ) {
        LOG_WARN("PROCFS", "Abertura procfs com escrita recusada");
        return ERR_UNAVAILABLE;
    }
    if (node.kind == PROCFS_NODE_SYS_CONTROL && (mode & VFS_MODE_WRITE)) {
        result = procfs_write_allowed();
        if (result != OK) return result;
    }
    if (node.kind != PROCFS_NODE_GLOBAL &&
        node.kind != PROCFS_NODE_SYS_CONTROL &&
        node.kind != PROCFS_NODE_PROCESS_STATUS &&
        node.kind != PROCFS_NODE_PROCESS_CMDLINE &&
        node.kind != PROCFS_NODE_PROCESS_MAPS) {
        LOG_ERROR("PROCFS", "Tipo de node invalido na abertura procfs");
        return ERR_INVALID;
    }
    kmemset(context, 0, sizeof(*context));
    context->node_kind = node.kind;
    context->process_pid = node.pid;
    context->control_index = node.control_index;
    if (node.kind == PROCFS_NODE_GLOBAL) {
        result = procfs_entry_find(lookup->canonical_path, &entry);
        if (result == OK) {
            result = procfs_render_snapshot(&entry.entry, context);
        }
    } else if (node.kind == PROCFS_NODE_SYS_CONTROL) {
        if (node.control_index >= PROCFS_SYS_CONTROL_COUNT) {
            LOG_ERROR("PROCFS", "Controle procfs fora da tabela");
            return ERR_STATE;
        }
        result = procfs_render_snapshot(
            &procfs_control_entries[node.control_index], context);
    } else {
        context->snapshot = (uint8_t*)kmalloc(PROCFS_MAX_SNAPSHOT_SIZE);
        if (!context->snapshot) {
            LOG_ERROR("PROCFS", "Falha ao alocar snapshot de processo");
            return ERR_MEM;
        }
        result = procfs_render_process(&node, context);
        if (result != OK) {
            kfree(context->snapshot);
            context->snapshot = 0;
            context->snapshot_size = 0U;
        } else {
            spinlock_acquire(&procfs_lock);
            procfs_active_snapshots++;
            spinlock_release(&procfs_lock);
        }
    }
    if (result != OK) {
        LOG_ERROR("PROCFS", "Falha ao gerar snapshot procfs");
        return result;
    }
    context->entry_index = node.entry_index;
    context->mount_slot = lookup->mount_slot;
    context->mount_generation = lookup->mount_generation;
    context->mount_acquired = 1U;
    vnode->type = VFS_NODE_REGULAR;
    vnode->operations = &procfs_operations;
    vnode->private_data = context;
    vnode->size = context->snapshot_size;
    file->mode = mode;
    file->offset = 0U;
    return OK;
}

static int procfs_pid_name(char* output, uint32_t capacity, uint32_t pid) {
    uint32_t length = 0U;

    if (!output || !capacity) {
        LOG_ERROR("PROCFS", "Destino invalido para nome de PID");
        return ERR_NULL;
    }
    if (procfs_append_decimal(output, capacity, &length, pid) != OK) {
        LOG_WARN("PROCFS", "Nome de PID excedeu capacidade");
        return ERR_OVERFLOW;
    }
    if (length >= capacity) {
        LOG_WARN("PROCFS", "Nome de PID sem terminador");
        return ERR_OVERFLOW;
    }
    output[length] = '\0';
    return OK;
}

static int procfs_build_pid_path(char* output, uint32_t capacity,
                                 uint32_t pid, const char* leaf) {
    uint32_t length = 0U;

    if (!output || !capacity || !leaf) {
        LOG_ERROR("PROCFS", "Destino invalido para caminho de PID");
        return ERR_NULL;
    }
    if (procfs_append_text(output, capacity, &length, "/proc/") != OK ||
        procfs_append_decimal(output, capacity, &length, pid) != OK ||
        procfs_append_char(output, capacity, &length, '/') != OK ||
        procfs_append_text(output, capacity, &length, leaf) != OK ||
        length >= capacity) {
        LOG_WARN("PROCFS", "Caminho de PID excedeu capacidade");
        return ERR_OVERFLOW;
    }
    output[length] = '\0';
    return OK;
}

int procfs_list_path(const char* canonical_path, vfs_dir_entry_t* entries,
                     uint32_t capacity, uint32_t* out_count) {
    process_snapshot_t* snapshots = 0;
    uint32_t process_total = 0U;
    uint32_t count = 0U;
    procfs_node_ref_t node;
    int result;

    if (!canonical_path || !entries || !out_count) {
        LOG_ERROR("PROCFS", "Destino nulo na listagem procfs");
        return ERR_NULL;
    }
    *out_count = 0U;
    if (!capacity) return ERR_OVERFLOW;
    if (!procfs_ready) {
        LOG_ERROR("PROCFS", "Listagem procfs antes da inicializacao");
        return ERR_STATE;
    }
    if (kstrcmp(canonical_path, "/proc") == 0) {
        if (capacity < PROCFS_GLOBAL_COUNT + 1U) return ERR_OVERFLOW;
        snapshots = (process_snapshot_t*)kmalloc(
            MAX_PROCESSES * sizeof(process_snapshot_t));
        if (!snapshots) {
            LOG_ERROR("PROCFS", "Falha ao alocar lista de processos");
            return ERR_MEM;
        }
        result = process_snapshot_list(snapshots, MAX_PROCESSES,
                                       &process_total);
        if (result != OK) {
            kfree(snapshots);
            return result;
        }
        if (PROCFS_GLOBAL_COUNT + 1U + process_total > capacity) {
            kfree(snapshots);
            LOG_WARN("PROCFS", "Listagem procfs excedeu capacidade");
            return ERR_OVERFLOW;
        }
        for (uint32_t index = 0U; index < PROCFS_GLOBAL_COUNT; index++) {
            kmemset(&entries[count], 0, sizeof(entries[count]));
            procfs_copy_text(entries[count].name, sizeof(entries[count].name),
                             procfs_entries[index].name);
            entries[count].type = VFS_NODE_REGULAR;
            count++;
        }
        kmemset(&entries[count], 0, sizeof(entries[count]));
        procfs_copy_text(entries[count].name, sizeof(entries[count].name),
                         "sys");
        entries[count].type = VFS_NODE_DIRECTORY;
        count++;
        for (uint32_t index = 0U; index < process_total; index++) {
            kmemset(&entries[count], 0, sizeof(entries[count]));
            result = procfs_pid_name(entries[count].name,
                                     sizeof(entries[count].name),
                                     snapshots[index].pid);
            if (result != OK) break;
            entries[count].type = VFS_NODE_DIRECTORY;
            count++;
        }
        kfree(snapshots);
        if (result != OK) return result;
        *out_count = count;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/sys") == 0) {
        if (capacity < 1U) return ERR_OVERFLOW;
        kmemset(&entries[0], 0, sizeof(entries[0]));
        procfs_copy_text(entries[0].name, sizeof(entries[0].name), "kernel");
        entries[0].type = VFS_NODE_DIRECTORY;
        *out_count = 1U;
        return OK;
    }
    if (kstrcmp(canonical_path, "/proc/sys/kernel") == 0) {
        if (capacity < PROCFS_SYS_CONTROL_COUNT) return ERR_OVERFLOW;
        for (uint32_t index = 0U; index < PROCFS_SYS_CONTROL_COUNT; index++) {
            kmemset(&entries[index], 0, sizeof(entries[index]));
            procfs_copy_text(entries[index].name, sizeof(entries[index].name),
                             procfs_control_entries[index].name);
            entries[index].type = VFS_NODE_REGULAR;
        }
        *out_count = PROCFS_SYS_CONTROL_COUNT;
        return OK;
    }
    result = procfs_parse_path(canonical_path, &node);
    if (result != OK) return result;
    if (node.kind != PROCFS_NODE_PROCESS_DIRECTORY) return ERR_INVALID;
    if (capacity < 3U) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < 3U; index++) {
        static const char* const names[] = {"status", "cmdline", "maps"};

        kmemset(&entries[count], 0, sizeof(entries[count]));
        procfs_copy_text(entries[count].name, sizeof(entries[count].name),
                         names[index]);
        entries[count].type = VFS_NODE_REGULAR;
        count++;
    }
    *out_count = count;
    return OK;
}

int procfs_list(vfs_dir_entry_t* entries, uint32_t capacity,
                uint32_t* out_count) {
    return procfs_list_path("/proc", entries, capacity, out_count);
}

static int procfs_read(file_t* file, void* buffer, uint32_t size,
                       uint32_t* bytes_read) {
    procfs_file_context_t* context;
    uint32_t available;

    if (!file || !file->vnode || !bytes_read) {
        LOG_ERROR("PROCFS", "Argumento nulo na leitura procfs");
        return ERR_NULL;
    }
    *bytes_read = 0U;
    if (!(file->mode & VFS_MODE_READ)) return ERR_UNAVAILABLE;
    if (size && !buffer) {
        LOG_ERROR("PROCFS", "Buffer nulo na leitura procfs");
        return ERR_NULL;
    }
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot || file->offset > context->snapshot_size) {
        LOG_ERROR("PROCFS", "Snapshot procfs invalido na leitura");
        return ERR_STATE;
    }
    available = context->snapshot_size - file->offset;
    if (size > available) size = available;
    if (size) kmemcpy(buffer, context->snapshot + file->offset, size);
    file->offset += size;
    *bytes_read = size;
    return OK;
}

static int procfs_write(file_t* file, const void* buffer, uint32_t size,
                        uint32_t* bytes_written) {
    procfs_file_context_t* context;
    const proc_entry_t* entry;
    int result;

    if (!file || !file->vnode) {
        LOG_ERROR("PROCFS", "Arquivo nulo na escrita procfs");
        return ERR_NULL;
    }
    if (!bytes_written) {
        LOG_ERROR("PROCFS", "Contador nulo na escrita procfs");
        return ERR_NULL;
    }
    *bytes_written = 0U;
    if (!(file->mode & VFS_MODE_WRITE)) return ERR_UNAVAILABLE;
    if (size && !buffer) {
        LOG_ERROR("PROCFS", "Buffer nulo na escrita procfs");
        return ERR_NULL;
    }
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot ||
        context->snapshot_size > PROCFS_MAX_SNAPSHOT_SIZE) {
        LOG_ERROR("PROCFS", "Contexto invalido na escrita procfs");
        return ERR_STATE;
    }
    if (context->node_kind != PROCFS_NODE_SYS_CONTROL ||
        context->control_index >= PROCFS_SYS_CONTROL_COUNT) {
        LOG_WARN("PROCFS", "Escrita procfs recusada para no somente leitura");
        return ERR_UNAVAILABLE;
    }
    result = procfs_write_allowed();
    if (result != OK) return result;
    if (file->offset != 0U) {
        LOG_WARN("PROCFS", "Escrita procfs fora do inicio recusada");
        return ERR_INVALID;
    }
    entry = &procfs_control_entries[context->control_index];
    if (!entry->write_proc) {
        LOG_ERROR("PROCFS", "Callback de escrita procfs ausente");
        return ERR_UNAVAILABLE;
    }
    result = entry->write_proc((const char*)buffer, size, entry->data);
    if (result != OK) return result;
    file->offset = 0U;
    *bytes_written = size;
    return OK;
}

static int procfs_open(vnode_t* vnode, file_t* file) {
    if (!vnode || !file) {
        LOG_ERROR("PROCFS", "Argumento nulo no open procfs");
        return ERR_NULL;
    }
    return OK;
}

static int procfs_close(file_t* file) {
    procfs_file_context_t* context;

    if (!file || !file->vnode) {
        LOG_ERROR("PROCFS", "Arquivo nulo no fechamento procfs");
        return ERR_NULL;
    }
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context) {
        LOG_ERROR("PROCFS", "Contexto nulo no fechamento procfs");
        return ERR_STATE;
    }
    if (context->snapshot) {
        kfree(context->snapshot);
        context->snapshot = 0;
        context->snapshot_size = 0U;
        spinlock_acquire(&procfs_lock);
        if (procfs_active_snapshots) procfs_active_snapshots--;
        spinlock_release(&procfs_lock);
    }
    if (context->mount_acquired) {
        vfs_mount_release(context->mount_slot, context->mount_generation);
        context->mount_acquired = 0U;
    }
    return OK;
}

static int procfs_lseek(file_t* file, int32_t offset, uint32_t whence,
                        uint32_t* position) {
    procfs_file_context_t* context;
    uint32_t base;
    uint32_t target;

    if (!file || !file->vnode || !position) {
        LOG_ERROR("PROCFS", "Argumento nulo no seek procfs");
        return ERR_NULL;
    }
    *position = 0U;
    if (!(file->mode & VFS_MODE_READ)) return ERR_UNAVAILABLE;
    context = (procfs_file_context_t*)file->vnode->private_data;
    if (!context || !context->snapshot) return ERR_STATE;
    if (whence == VFS_SEEK_SET) base = 0U;
    else if (whence == VFS_SEEK_CUR) base = file->offset;
    else if (whence == VFS_SEEK_END) base = context->snapshot_size;
    else return ERR_INVALID;
    if (offset < 0) {
        uint32_t magnitude = (uint32_t)(-(offset + 1)) + 1U;

        if (magnitude > base) return ERR_INVALID;
        target = base - magnitude;
    } else {
        if (base > 0xFFFFFFFFU - (uint32_t)offset) return ERR_OVERFLOW;
        target = base + (uint32_t)offset;
    }
    if (target > context->snapshot_size) return ERR_INVALID;
    file->offset = target;
    *position = target;
    return OK;
}

static int procfs_ioctl(file_t* file, uint32_t request, void* argument) {
    (void)file;
    (void)request;
    (void)argument;
    LOG_WARN("PROCFS", "Ioctl procfs nao suportado");
    return ERR_UNAVAILABLE;
}

static int procfs_sync(file_t* file) {
    (void)file;
    LOG_WARN("PROCFS", "Sync procfs nao suportado");
    return ERR_UNAVAILABLE;
}

static int procfs_poll(file_t* file, uint32_t events, uint32_t* revents) {
    (void)events;
    if (!file || !revents) {
        LOG_ERROR("PROCFS", "Argumento nulo no poll procfs");
        return ERR_NULL;
    }
    *revents = (file->mode & VFS_MODE_READ) ? POLLIN : POLLERR;
    return OK;
}

static int procfs_error_callback(char* buffer, uint32_t capacity,
                                 uint32_t* out_len, void* data) {
    (void)buffer;
    (void)capacity;
    (void)data;
    if (out_len) *out_len = 0U;
    LOG_WARN("PROCFS", "Callback de erro usado pelo autoteste");
    return ERR_INVALID;
}

static int procfs_overflow_callback(char* buffer, uint32_t capacity,
                                    uint32_t* out_len, void* data) {
    (void)buffer;
    (void)capacity;
    (void)data;
    if (out_len) *out_len = PROCFS_MAX_SNAPSHOT_SIZE + 1U;
    return OK;
}

static void procfs_test_process_entry(void) {
    while (1) process_yield();
}

static int procfs_buffer_valid(const uint8_t* buffer, uint32_t size) {
    if (!buffer) return 0;
    for (uint32_t index = 0U; index < size; index++) {
        if (buffer[index] == 0U || buffer[index] == '\r' ||
            buffer[index] == 0x1BU || buffer[index] > 0x7FU ||
            (buffer[index] < 0x20U && buffer[index] != '\n')) return 0;
    }
    return 1;
}

static int procfs_buffer_contains(const uint8_t* buffer, uint32_t size,
                                  const char* text) {
    uint32_t text_length;

    if (!buffer || !text) return 0;
    text_length = kstrlen(text);
    if (!text_length || text_length > size) return 0;
    for (uint32_t index = 0U; index + text_length <= size; index++) {
        uint32_t offset = 0U;

        while (offset < text_length &&
               buffer[index + offset] == (uint8_t)text[offset]) offset++;
        if (offset == text_length) return 1;
    }
    return 0;
}

static int procfs_test_read_path(const char* path, uint8_t* buffer,
                                 uint32_t capacity, uint32_t* out_size) {
    int32_t fd = VFS_FD_INVALID;
    uint32_t total = 0U;
    uint32_t bytes = 0U;
    int result;

    if (!path || !buffer || !out_size || !capacity) {
        LOG_ERROR("PROCFS", "Argumento invalido no autoteste de leitura");
        return ERR_NULL;
    }
    *out_size = 0U;
    result = vfs_open(path, VFS_MODE_READ, &fd);
    if (result != OK) return result;
    do {
        result = vfs_read(fd, buffer + total, capacity - total, &bytes);
        if (result != OK) break;
        total += bytes;
    } while (bytes && total < capacity);
    if (vfs_close(fd) != OK) result = ERR_STATE;
    if (result == OK) *out_size = total;
    return result;
}

static int procfs_test_write_path(const char* path, const void* buffer,
                                  uint32_t size) {
    int32_t fd = VFS_FD_INVALID;
    uint32_t written = 0U;
    int result;

    if (!path || (size && !buffer)) {
        LOG_WARN("PROCFS", "Entrada invalida no helper de escrita");
        return ERR_NULL;
    }
    result = vfs_open(path, VFS_MODE_WRITE, &fd);
    if (result != OK) {
        LOG_WARN("PROCFS", "Abertura falhou no helper de escrita");
        return result;
    }
    result = vfs_write(fd, buffer, size, &written);
    if (result == OK && written != size) {
        LOG_ERROR("PROCFS", "Escrita parcial no helper de escrita");
        result = ERR_STATE;
    } else if (result != OK) {
        LOG_WARN("PROCFS", "Escrita rejeitada no helper de escrita");
    }
    if (vfs_close(fd) != OK) {
        LOG_ERROR("PROCFS", "Fechamento falhou no helper de escrita");
        if (result == OK) result = ERR_STATE;
    }
    return result;
}

int procfs_validate_state(void) {
    uint32_t active;

    spinlock_acquire(&procfs_lock);
    active = procfs_active_snapshots;
    if (!procfs_ready || active > VFS_MAX_OPEN_FILES ||
        sizeof(procfs_entries) / sizeof(procfs_entries[0]) !=
        PROCFS_GLOBAL_COUNT ||
        sizeof(procfs_control_entries) / sizeof(procfs_control_entries[0]) !=
        PROCFS_SYS_CONTROL_COUNT) {
        spinlock_release(&procfs_lock);
        LOG_ERROR("PROCFS", "Invariantes procfs invalidas");
        return ERR_STATE;
    }
    for (uint32_t index = 0U; index < PROCFS_GLOBAL_COUNT; index++) {
        if (!procfs_entries[index].name || procfs_entries[index].mode !=
            VFS_MODE_READ || !procfs_entries[index].read_proc ||
            procfs_entries[index].write_proc) {
            spinlock_release(&procfs_lock);
            LOG_ERROR("PROCFS", "Entry procfs invalido");
            return ERR_STATE;
        }
    }
    for (uint32_t index = 0U; index < PROCFS_SYS_CONTROL_COUNT; index++) {
        const proc_entry_t* entry = &procfs_control_entries[index];

        if (!entry->name || entry->mode != VFS_MODE_READ_WRITE ||
            !entry->read_proc || !entry->write_proc || !entry->data ||
            *(uint32_t*)entry->data != index) {
            spinlock_release(&procfs_lock);
            LOG_ERROR("PROCFS", "Controle procfs invalido");
            return ERR_STATE;
        }
    }
    spinlock_release(&procfs_lock);
    return OK;
}

int procfs_self_test(procfs_test_result_t* result) {
    static const char* const global_names[] = {
        "uptime", "meminfo", "cpuinfo", "version", "cmdline"
    };
    vfs_dir_entry_t entries[VFS_MAX_DIR_ENTRIES];
    vfs_lookup_result_t lookup;
    uint8_t first[512];
    uint8_t second[512];
    procfs_file_context_t context;
    proc_entry_t error_entry;
    proc_entry_t overflow_entry;
    process_t* temporary = 0;
    process_t* reused = 0;
    uint32_t count = 0U;
    uint32_t bytes = 0U;
    uint32_t second_bytes = 0U;
    uint32_t position = 0U;
    uint32_t before_active;
    uint32_t temporary_pid = 0U;
    uint32_t temporary_generation = 0U;
    char temporary_status_path[VFS_MAX_PATH];
    char temporary_cmdline_path[VFS_MAX_PATH];
    char temporary_maps_path[VFS_MAX_PATH];
    char control_overflow[PROCFS_SYS_CONTROL_MAX_INPUT + 1U];
    const char invalid_nul[] = {'i', 'n', 'f', 'o', '\0'};
    log_level_t original_console_level;
    log_level_t original_buffer_level;
    uint8_t close_ok = 1U;
    uint8_t temporary_nodes = 0U;
    int32_t control_fd = VFS_FD_INVALID;
    int32_t fd = VFS_FD_INVALID;
    int result_code;

    if (!result) {
        LOG_ERROR("PROCFS", "Destino nulo no autoteste procfs");
        return ERR_NULL;
    }
    kmemset(result, 0, sizeof(*result));
    kmemset(&context, 0, sizeof(context));
    result->registry = procfs_validate_state() == OK;
    procfs_test_count(result, result->registry);
    result->lookup = vfs_lookup("/proc", &lookup) == OK &&
                     lookup.type == VFS_NODE_DIRECTORY &&
                     vfs_lookup("/proc/uptime", &lookup) == OK &&
                     lookup.type == VFS_NODE_REGULAR;
    procfs_test_count(result, result->lookup);
    result->listing = vfs_list_dir("/proc", entries, VFS_MAX_DIR_ENTRIES,
                                   &count) == OK &&
                      count >= PROCFS_GLOBAL_COUNT + 1U;
    for (uint32_t index = 0U; result->listing && index < PROCFS_GLOBAL_COUNT;
         index++) {
        result->listing = kstrcmp(entries[index].name, global_names[index]) == 0;
    }
    result->listing = result->listing &&
                      kstrcmp(entries[PROCFS_GLOBAL_COUNT].name, "sys") == 0 &&
                      entries[PROCFS_GLOBAL_COUNT].type == VFS_NODE_DIRECTORY;
    {
        uint32_t previous_pid = 0U;
        uint8_t first_pid = 1U;

        for (uint32_t index = PROCFS_GLOBAL_COUNT + 1U;
             result->listing && index < count; index++) {
            uint32_t pid;
            int pid_result = procfs_parse_pid(
                entries[index].name, kstrlen(entries[index].name), &pid);

            result->listing = pid_result == OK && (first_pid ||
                             pid > previous_pid);
            previous_pid = pid;
            first_pid = 0U;
        }
    }
    procfs_test_count(result, result->listing);

    result->global_nodes = 1U;
    result->format = 1U;
    for (uint32_t index = 0U; index < PROCFS_GLOBAL_COUNT; index++) {
        char path[VFS_MAX_PATH];
        uint32_t length = 0U;

        procfs_copy_text(path, sizeof(path), "/proc/");
        while (global_names[index][length] && length + 7U < sizeof(path)) {
            path[6U + length] = global_names[index][length];
            length++;
        }
        path[6U + length] = '\0';
        result_code = procfs_test_read_path(path, first, sizeof(first),
                                            &bytes);
        result->global_nodes = result->global_nodes && result_code == OK;
        result->format = result->format && result_code == OK &&
                         procfs_buffer_valid(first, bytes);
    }
    procfs_test_count(result, result->global_nodes);
    procfs_test_count(result, result->format);

    spinlock_acquire(&procfs_lock);
    before_active = procfs_active_snapshots;
    spinlock_release(&procfs_lock);
    result_code = vfs_open("/proc/uptime", VFS_MODE_READ, &fd);
    if (result_code == OK) {
        result_code = vfs_read(fd, first, 8U, &bytes);
        result->read = result_code == OK && bytes > 0U;
        result_code = vfs_lseek(fd, 0, VFS_SEEK_SET, &position);
        if (result_code == OK) result_code = vfs_read(fd, first, sizeof(first),
                                                       &bytes);
        result->read = result->read && result_code == OK && bytes > 0U;
        result->seek_eof = vfs_lseek(fd, 0, VFS_SEEK_END, &position) == OK &&
                           vfs_read(fd, first, sizeof(first), &second_bytes) ==
                           OK && second_bytes == 0U &&
                           vfs_lseek(fd, 1, VFS_SEEK_END, &position) ==
                           ERR_INVALID &&
                           vfs_lseek(fd, -1, VFS_SEEK_SET, &position) ==
                           ERR_INVALID;
        if (vfs_lseek(fd, 0, VFS_SEEK_SET, &position) != OK ||
            vfs_read(fd, first, sizeof(first), &bytes) != OK) {
            result->read = 0U;
        }
        if (vfs_lseek(fd, 0, VFS_SEEK_SET, &position) != OK ||
            vfs_read(fd, second, bytes, &second_bytes) != OK ||
            second_bytes != bytes || !procfs_buffer_valid(first, bytes)) {
            result->read = 0U;
        }
        result->read = result->read && procfs_buffer_valid(second, second_bytes) &&
                       procfs_buffer_contains(second, second_bytes,
                                              "uptime_ticks ") &&
                       procfs_buffer_contains(second, second_bytes,
                                              "idle_seconds ");
        if (vfs_close(fd) != OK) close_ok = 0U;
        fd = VFS_FD_INVALID;
    }
    result->read = result->read && result_code == OK;
    procfs_test_count(result, result->read);
    procfs_test_count(result, result->seek_eof);
    result->permissions = vfs_open("/proc/uptime", VFS_MODE_WRITE, &fd) ==
                          ERR_UNAVAILABLE && fd == VFS_FD_INVALID;
    procfs_test_count(result, result->permissions);

    original_console_level = log_get_console_level();
    original_buffer_level = log_get_buffer_level();
    result->control_nodes = vfs_lookup("/proc/sys", &lookup) == OK &&
                            lookup.type == VFS_NODE_DIRECTORY &&
                            vfs_lookup("/proc/sys/kernel", &lookup) == OK &&
                            lookup.type == VFS_NODE_DIRECTORY &&
                            vfs_lookup("/proc/sys/kernel/console_log_level",
                                       &lookup) == OK &&
                            lookup.type == VFS_NODE_REGULAR &&
                            !lookup.read_only &&
                            vfs_list_dir("/proc/sys", entries,
                                         VFS_MAX_DIR_ENTRIES, &count) == OK &&
                            count == 1U &&
                            kstrcmp(entries[0].name, "kernel") == 0 &&
                            vfs_list_dir("/proc/sys/kernel", entries,
                                         VFS_MAX_DIR_ENTRIES, &count) == OK &&
                            count == PROCFS_SYS_CONTROL_COUNT &&
                            kstrcmp(entries[0].name, "console_log_level") == 0 &&
                            kstrcmp(entries[1].name, "buffer_log_level") == 0;
    procfs_test_count(result, result->control_nodes);

    result->control_read = procfs_reset_controls() == OK &&
                           procfs_test_read_path(
                               "/proc/sys/kernel/console_log_level", first,
                               sizeof(first), &bytes) == OK &&
                           procfs_buffer_valid(first, bytes) &&
                           procfs_buffer_contains(first, bytes,
                                                  "console_log_level info\n") &&
                           procfs_test_read_path(
                               "/proc/sys/kernel/buffer_log_level", first,
                               sizeof(first), &bytes) == OK &&
                           procfs_buffer_valid(first, bytes) &&
                           procfs_buffer_contains(first, bytes,
                                                  "buffer_log_level info\n");
    procfs_test_count(result, result->control_read);

    result->control_privilege = procfs_write_allowed() == OK &&
                                process_get_current() != 0 &&
                                !process_is_user(process_get_current());
    procfs_test_count(result, result->control_privilege);

    result->control_write = procfs_test_write_path(
                                "/proc/sys/kernel/buffer_log_level",
                                "debug", 5U) == OK &&
                            procfs_test_write_path(
                                "/proc/sys/kernel/console_log_level",
                                "debug\n", 6U) == OK &&
                            procfs_test_read_path(
                                "/proc/sys/kernel/console_log_level", first,
                                sizeof(first), &bytes) == OK &&
                            procfs_buffer_contains(first, bytes,
                                                   "console_log_level debug\n");
    procfs_test_count(result, result->control_write);

    kmemset(control_overflow, 'x', sizeof(control_overflow));
    result->control_rollback =
        procfs_reset_controls() == OK &&
        vfs_open("/proc/sys/kernel/console_log_level", VFS_MODE_READ,
                 &control_fd) == OK &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               "debug\n", 6U) == OK &&
        procfs_test_write_path("/proc/sys/kernel/console_log_level",
                               "debug\n", 6U) == OK &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               "error\n", 6U) == ERR_INVALID &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               "invalid\n", 8U) == ERR_INVALID &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               "info\r\n", 6U) == ERR_INVALID &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               "info extra\n", 11U) == ERR_INVALID &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               "info\nwarn", 9U) == ERR_INVALID &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               invalid_nul, sizeof(invalid_nul)) == ERR_INVALID &&
        procfs_test_write_path("/proc/sys/kernel/buffer_log_level",
                               control_overflow, sizeof(control_overflow)) ==
            ERR_OVERFLOW &&
        vfs_read(control_fd, first, sizeof(first), &bytes) == OK &&
        procfs_buffer_contains(first, bytes, "console_log_level info\n");
    if (control_fd != VFS_FD_INVALID) {
        if (vfs_close(control_fd) != OK) result->control_rollback = 0U;
        control_fd = VFS_FD_INVALID;
    }
    procfs_test_count(result, result->control_rollback);

    result->control_reset = procfs_reset_controls() == OK &&
                            procfs_test_read_path(
                                "/proc/sys/kernel/console_log_level", first,
                                sizeof(first), &bytes) == OK &&
                            procfs_buffer_contains(first, bytes,
                                                   "console_log_level info\n") &&
                            procfs_test_read_path(
                                "/proc/sys/kernel/buffer_log_level", first,
                                sizeof(first), &bytes) == OK &&
                            procfs_buffer_contains(first, bytes,
                                                   "buffer_log_level info\n");
    procfs_test_count(result, result->control_reset);

    result->process_nodes = vfs_lookup("/proc/0/status", &lookup) == OK &&
                            vfs_lookup("/proc/0/cmdline", &lookup) == OK &&
                            vfs_lookup("/proc/0/maps", &lookup) == OK;
    procfs_test_count(result, result->process_nodes);
    result->process_listing = vfs_list_dir("/proc/0", entries,
                                           VFS_MAX_DIR_ENTRIES, &count) == OK &&
                              count == 3U &&
                              kstrcmp(entries[0].name, "status") == 0 &&
                              kstrcmp(entries[1].name, "cmdline") == 0 &&
                              kstrcmp(entries[2].name, "maps") == 0;
    procfs_test_count(result, result->process_listing);

    temporary = process_create("procfs-test", procfs_test_process_entry);
    if (temporary) {
        temporary_pid = temporary->pid;
        temporary_generation = temporary->event_generation;
        temporary_nodes =
            procfs_build_pid_path(temporary_status_path,
                                  sizeof(temporary_status_path), temporary_pid,
                                  "status") == OK &&
            procfs_build_pid_path(temporary_cmdline_path,
                                  sizeof(temporary_cmdline_path), temporary_pid,
                                  "cmdline") == OK &&
            procfs_build_pid_path(temporary_maps_path,
                                  sizeof(temporary_maps_path), temporary_pid,
                                  "maps") == OK &&
            procfs_test_read_path(temporary_status_path, first, sizeof(first),
                                  &bytes) == OK &&
            procfs_buffer_contains(first, bytes, "pid ") &&
            procfs_test_read_path(temporary_cmdline_path, first, sizeof(first),
                                  &bytes) == OK &&
            procfs_buffer_contains(first, bytes, "cmdline procfs-test") &&
            procfs_test_read_path(temporary_maps_path, first, sizeof(first),
                                  &bytes) == OK && bytes == 0U;
        process_destroy(temporary);
        temporary = 0;
        reused = process_create("procfs-reused", procfs_test_process_entry);
        result->generation = reused && reused->pid == temporary_pid &&
                             reused->event_generation != temporary_generation;
        if (reused) process_destroy(reused);
        reused = 0;
    }
    procfs_test_count(result, temporary_nodes);
    procfs_test_count(result, result->generation);

    error_entry.name = "error";
    error_entry.mode = VFS_MODE_READ;
    error_entry.read_proc = procfs_error_callback;
    error_entry.write_proc = 0;
    error_entry.data = 0;
    overflow_entry.name = "overflow";
    overflow_entry.mode = VFS_MODE_READ;
    overflow_entry.read_proc = procfs_overflow_callback;
    overflow_entry.write_proc = 0;
    overflow_entry.data = 0;
    result->callback_errors = procfs_render_snapshot(&error_entry, &context) ==
                              ERR_INVALID && context.snapshot == 0 &&
                              procfs_render_snapshot(&overflow_entry, &context) ==
                              ERR_OVERFLOW && context.snapshot == 0;
    procfs_test_count(result, result->callback_errors);
    log_set_level(original_buffer_level);
    if (log_set_console_level(original_console_level) != OK) close_ok = 0U;
    spinlock_acquire(&procfs_lock);
    result->cleanup = close_ok && procfs_active_snapshots == before_active &&
                      temporary == 0 && reused == 0;
    spinlock_release(&procfs_lock);
    procfs_test_count(result, result->cleanup);
    result->invariants = procfs_validate_state() == OK;
    procfs_test_count(result, result->invariants);
    if (fd != VFS_FD_INVALID) (void)vfs_close(fd);
    if (temporary) process_destroy(temporary);
    if (reused) process_destroy(reused);
    return result->passed == result->total ? OK : ERR_STATE;
}
