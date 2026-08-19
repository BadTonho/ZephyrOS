#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/video.h"

#define LOG_EFLAGS_INTERRUPT_ENABLE 0x200U
#define LOG_PRIVATE_TEST_CAPACITY 4U
#define LOG_SELF_TEST_TOTAL 8U
#define LOG_MAX_OCCURRENCES 0xFFFFFFFFU
#define LOG_TEST_TEXT_CAPACITY 160U

typedef enum {
    LOG_STORE_FILTERED,
    LOG_STORE_NEW,
    LOG_STORE_GROUPED
} log_store_action_t;

typedef struct {
    log_store_action_t action;
    uint32_t index;
    uint32_t occurrences;
} log_store_result_t;

typedef struct {
    log_record_t* records;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
    uint32_t next_sequence;
    log_level_t buffer_level;
    log_level_t console_level;
    uint32_t overwritten_records;
    uint32_t grouped_events;
    uint32_t truncated_events;
    uint32_t dropped_events;
    uint32_t clear_count;
} log_ring_state_t;

static log_record_t log_records[LOG_RECORD_CAPACITY];
static log_record_t log_serialization_snapshot[LOG_RECORD_CAPACITY];
static log_ring_state_t log_state = {
    log_records, LOG_RECORD_CAPACITY, 0U, 0U, 1U,
    LOG_LEVEL_INFO, LOG_LEVEL_INFO, 0U, 0U, 0U, 0U, 0U
};

static const uint8_t level_colors[] = {
    0x4F,
    0x6F,
    0x0F,
    0x07
};

static const char* level_labels[] = {
    "ERR",
    "WRN",
    "INF",
    "DBG"
};

static int log_level_is_valid(log_level_t level) {
    return (uint32_t)level <= (uint32_t)LOG_LEVEL_DEBUG;
}

static uint32_t log_suspend_interrupts(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void log_restore_interrupts(uint32_t flags) {
    if (flags & LOG_EFLAGS_INTERRUPT_ENABLE) {
        asm volatile("sti" : : : "memory");
    }
}

static void log_ring_initialize(log_ring_state_t* state,
                                log_record_t* records,
                                uint32_t capacity) {
    kmemset(records, 0, sizeof(log_record_t) * capacity);
    state->records = records;
    state->capacity = capacity;
    state->head = 0U;
    state->count = 0U;
    state->next_sequence = 1U;
    state->buffer_level = LOG_LEVEL_INFO;
    state->console_level = LOG_LEVEL_INFO;
    state->overwritten_records = 0U;
    state->grouped_events = 0U;
    state->truncated_events = 0U;
    state->dropped_events = 0U;
    state->clear_count = 0U;
}

static uint8_t log_copy_text(char* destination, uint32_t capacity,
                             const char* source) {
    uint32_t length = 0U;

    while (source[length] && length + 1U < capacity) {
        destination[length] = source[length];
        length++;
    }
    destination[length] = '\0';
    return source[length] != '\0';
}

static int log_records_match(const log_record_t* record, log_level_t level,
                             uint8_t flags, int32_t error_code,
                             const char* module, const char* message) {
    uint8_t record_has_code = record->flags &
                              LOG_RECORD_FLAG_HAS_ERROR_CODE;
    uint8_t incoming_has_code = flags & LOG_RECORD_FLAG_HAS_ERROR_CODE;

    if (record->level != level || record_has_code != incoming_has_code) {
        return 0;
    }
    if (incoming_has_code && record->error_code != error_code) return 0;
    return kstrcmp(record->module, module) == 0 &&
           kstrcmp(record->message, message) == 0;
}

static uint32_t log_ring_latest_index(const log_ring_state_t* state) {
    return (state->head + state->count - 1U) % state->capacity;
}

static log_store_result_t log_ring_store(log_ring_state_t* state,
                                         log_level_t level,
                                         const char* module,
                                         const char* message,
                                         uint8_t has_error_code,
                                         int32_t error_code,
                                         uint32_t tick) {
    log_store_result_t result = { LOG_STORE_FILTERED, 0U, 0U };
    char stored_module[LOG_MODULE_CAPACITY];
    char stored_message[LOG_MESSAGE_CAPACITY];
    uint8_t flags = has_error_code ? LOG_RECORD_FLAG_HAS_ERROR_CODE : 0U;
    uint32_t index;

    if (level > state->buffer_level) return result;
    if (log_copy_text(stored_module, LOG_MODULE_CAPACITY, module)) {
        flags |= LOG_RECORD_FLAG_MODULE_TRUNCATED;
    }
    if (log_copy_text(stored_message, LOG_MESSAGE_CAPACITY, message)) {
        flags |= LOG_RECORD_FLAG_MESSAGE_TRUNCATED;
    }
    if (flags & (LOG_RECORD_FLAG_MODULE_TRUNCATED |
                 LOG_RECORD_FLAG_MESSAGE_TRUNCATED)) {
        state->truncated_events++;
    }

    if (state->count > 0U) {
        index = log_ring_latest_index(state);
        if (log_records_match(&state->records[index], level, flags,
                              error_code, stored_module, stored_message)) {
            log_record_t* record = &state->records[index];
            if (record->occurrences < LOG_MAX_OCCURRENCES) {
                record->occurrences++;
            }
            record->last_tick = tick;
            state->grouped_events++;
            result.action = LOG_STORE_GROUPED;
            result.index = index;
            result.occurrences = record->occurrences;
            return result;
        }
    }

    if (state->count == state->capacity) {
        index = state->head;
        state->head = (state->head + 1U) % state->capacity;
        state->overwritten_records++;
    } else {
        index = (state->head + state->count) % state->capacity;
        state->count++;
    }
    kmemset(&state->records[index], 0, sizeof(log_record_t));
    state->records[index].sequence = state->next_sequence++;
    state->records[index].first_tick = tick;
    state->records[index].last_tick = tick;
    state->records[index].level = level;
    state->records[index].error_code = has_error_code ? error_code : 0;
    state->records[index].occurrences = 1U;
    state->records[index].flags = flags;
    kmemcpy(state->records[index].module, stored_module,
            LOG_MODULE_CAPACITY);
    kmemcpy(state->records[index].message, stored_message,
            LOG_MESSAGE_CAPACITY);
    result.action = LOG_STORE_NEW;
    result.index = index;
    result.occurrences = 1U;
    return result;
}

static void log_ring_clear(log_ring_state_t* state) {
    kmemset(state->records, 0, sizeof(log_record_t) * state->capacity);
    state->head = 0U;
    state->count = 0U;
    state->clear_count++;
}

static uint32_t log_ring_copy_recent(const log_ring_state_t* state,
                                     log_record_t* output,
                                     uint32_t maximum) {
    uint32_t count = state->count < maximum ? state->count : maximum;
    uint32_t first = state->count - count;

    for (uint32_t offset = 0U; offset < count; offset++) {
        uint32_t index = (state->head + first + offset) % state->capacity;
        output[offset] = state->records[index];
    }
    return count;
}

static void log_append_char(char* output, uint32_t capacity,
                            uint32_t* position, char value) {
    if (*position + 1U >= capacity) return;
    output[*position] = value;
    (*position)++;
    output[*position] = '\0';
}

static void log_append_text(char* output, uint32_t capacity,
                            uint32_t* position, const char* text) {
    while (*text && *position + 1U < capacity) {
        output[*position] = *text;
        (*position)++;
        text++;
    }
    output[*position] = '\0';
}

static uint32_t log_serialize_records(const log_record_t* records,
                                      uint32_t count, char* output,
                                      uint32_t capacity) {
    uint32_t position = 0U;

    output[0] = '\0';
    for (uint32_t index = 0U; index < count; index++) {
        log_append_char(output, capacity, &position, '[');
        log_append_text(output, capacity, &position,
                        log_level_str(records[index].level));
        log_append_text(output, capacity, &position, "] [");
        log_append_text(output, capacity, &position, records[index].module);
        log_append_text(output, capacity, &position, "] ");
        log_append_text(output, capacity, &position, records[index].message);
        log_append_char(output, capacity, &position, '\n');
    }
    return position;
}

static void log_note_dropped_event(void) {
    uint32_t flags = log_suspend_interrupts();
    log_state.dropped_events++;
    log_restore_interrupts(flags);
}

static int log_is_power_of_two(uint32_t value) {
    return value >= 2U && (value & (value - 1U)) == 0U;
}

static void log_console_print_number(uint32_t value) {
    char reversed[16];
    char number[16];
    uint32_t length = 0U;

    if (value == 0U) reversed[length++] = '0';
    while (value > 0U && length + 1U < sizeof(reversed)) {
        reversed[length++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    for (uint32_t index = 0U; index < length; index++) {
        number[index] = reversed[length - index - 1U];
    }
    number[length] = '\0';
    video_print(number, 0x07);
}

static void log_print_colored(log_level_t level, const char* message,
                              uint32_t occurrences, int summary) {
    video_print("[", 0x07);
    video_print(level_labels[level], level_colors[level]);
    video_print("] ", 0x07);
    video_print(message, 0x07);
    if (summary) {
        video_print(" (repetido x", 0x08);
        log_console_print_number(occurrences);
        video_print(")", 0x08);
    }
    video_newline();
}

static void log_submit(log_level_t level, const char* module,
                       const char* message, uint8_t has_error_code,
                       int32_t error_code, int allow_console) {
    log_store_result_t result;
    char console_message[LOG_MESSAGE_CAPACITY];
    uint32_t interrupt_flags;
    int print_console = 0;
    int print_summary = 0;

    if (!log_level_is_valid(level) || !module || !message) {
        log_note_dropped_event();
        return;
    }
    interrupt_flags = log_suspend_interrupts();
    result = log_ring_store(&log_state, level, module, message,
                            has_error_code, error_code, timer_get_ticks());
    if (allow_console && level <= log_state.console_level) {
        print_console = result.action == LOG_STORE_NEW ||
                        (result.action == LOG_STORE_GROUPED &&
                         log_is_power_of_two(result.occurrences));
        print_summary = result.action == LOG_STORE_GROUPED;
    }
    if (print_console) {
        kmemcpy(console_message, log_state.records[result.index].message,
                LOG_MESSAGE_CAPACITY);
    }
    log_restore_interrupts(interrupt_flags);
    if (print_console) {
        log_print_colored(level, console_message, result.occurrences,
                          print_summary);
    }
}

void log_init(void) {
    uint32_t flags;

    LOG_INFO("LOG", "Inicializando sistema de log");
    flags = log_suspend_interrupts();
    log_ring_initialize(&log_state, log_records, LOG_RECORD_CAPACITY);
    log_restore_interrupts(flags);
    LOG_INFO("LOG", "Sistema de log inicializado com sucesso");
}

void log_set_level(log_level_t level) {
    uint32_t flags;

    if (!log_level_is_valid(level)) {
        log_note_dropped_event();
        LOG_WARN("LOG", "Nivel geral de log invalido");
        return;
    }
    flags = log_suspend_interrupts();
    log_state.buffer_level = level;
    log_state.console_level = level;
    log_restore_interrupts(flags);
}

log_level_t log_get_level(void) {
    return log_get_console_level();
}

int log_set_buffer_level(log_level_t level) {
    uint32_t flags;

    if (!log_level_is_valid(level)) {
        log_note_dropped_event();
        LOG_WARN("LOG", "Nivel do buffer de log invalido");
        return ERR_INVALID;
    }
    flags = log_suspend_interrupts();
    if (level < log_state.console_level) {
        log_restore_interrupts(flags);
        log_note_dropped_event();
        LOG_WARN("LOG", "Buffer nao pode filtrar mais que o console");
        return ERR_INVALID;
    }
    log_state.buffer_level = level;
    log_restore_interrupts(flags);
    return OK;
}

int log_set_console_level(log_level_t level) {
    uint32_t flags;

    if (!log_level_is_valid(level)) {
        log_note_dropped_event();
        LOG_WARN("LOG", "Nivel do console de log invalido");
        return ERR_INVALID;
    }
    flags = log_suspend_interrupts();
    if (level > log_state.buffer_level) {
        log_restore_interrupts(flags);
        log_note_dropped_event();
        LOG_WARN("LOG", "Console nao pode ser mais detalhado que o buffer");
        return ERR_INVALID;
    }
    log_state.console_level = level;
    log_restore_interrupts(flags);
    return OK;
}

log_level_t log_get_buffer_level(void) {
    log_level_t level;
    uint32_t flags = log_suspend_interrupts();

    level = log_state.buffer_level;
    log_restore_interrupts(flags);
    return level;
}

log_level_t log_get_console_level(void) {
    log_level_t level;
    uint32_t flags = log_suspend_interrupts();

    level = log_state.console_level;
    log_restore_interrupts(flags);
    return level;
}

const char* log_level_str(log_level_t level) {
    if (log_level_is_valid(level)) return level_labels[level];
    return "???";
}

void log_print(log_level_t level, const char* module, const char* message) {
    log_submit(level, module, message, 0U, 0, 1);
}

void log_print_code(log_level_t level, const char* module,
                    int32_t error_code, const char* message) {
    log_submit(level, module, message, 1U, error_code, 1);
}

void log_to_buffer(log_level_t level, const char* module,
                   const char* message) {
    log_submit(level, module, message, 0U, 0, 0);
}

int log_get_buffer(char* output, int max_size) {
    uint32_t count;
    uint32_t flags;

    if (!output || max_size <= 0) {
        log_note_dropped_event();
        LOG_ERROR("LOG", "Destino invalido para serializar o log");
        return ERR_NULL;
    }
    flags = log_suspend_interrupts();
    count = log_ring_copy_recent(&log_state, log_serialization_snapshot,
                                 LOG_RECORD_CAPACITY);
    log_restore_interrupts(flags);
    return (int)log_serialize_records(log_serialization_snapshot, count,
                                      output, (uint32_t)max_size);
}

void log_clear_buffer(void) {
    uint32_t flags = log_suspend_interrupts();

    log_ring_clear(&log_state);
    log_restore_interrupts(flags);
}

int log_get_stats(log_stats_t* output) {
    uint32_t flags;

    if (!output) {
        log_note_dropped_event();
        LOG_ERROR("LOG", "Destino nulo para estatisticas do log");
        return ERR_NULL;
    }
    flags = log_suspend_interrupts();
    output->occupancy = log_state.count;
    output->capacity = log_state.capacity;
    output->buffer_level = log_state.buffer_level;
    output->console_level = log_state.console_level;
    output->next_sequence = log_state.next_sequence;
    output->overwritten_records = log_state.overwritten_records;
    output->grouped_events = log_state.grouped_events;
    output->truncated_events = log_state.truncated_events;
    output->dropped_events = log_state.dropped_events;
    output->clear_count = log_state.clear_count;
    log_restore_interrupts(flags);
    return OK;
}

int log_copy_recent(log_record_t* output, uint32_t max_records,
                    uint32_t* out_count) {
    uint32_t flags;

    if (!output || !out_count || max_records == 0U) {
        log_note_dropped_event();
        LOG_ERROR("LOG", "Destino invalido para copiar registros do log");
        return ERR_NULL;
    }
    flags = log_suspend_interrupts();
    *out_count = log_ring_copy_recent(&log_state, output, max_records);
    log_restore_interrupts(flags);
    return OK;
}

static uint8_t log_test_order_and_metadata(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    log_ring_store(&state, LOG_LEVEL_INFO, "TEST", "primeiro", 0U, 0, 3U);
    log_ring_store(&state, LOG_LEVEL_ERROR, "TEST", "segundo", 1U,
                   ERR_DISK, 7U);
    return state.count == 2U && records[0].sequence == 1U &&
           records[0].first_tick == 3U && records[0].last_tick == 3U &&
           records[0].level == LOG_LEVEL_INFO &&
           kstrcmp(records[0].module, "TEST") == 0 &&
           kstrcmp(records[0].message, "primeiro") == 0 &&
           records[0].occurrences == 1U && records[1].sequence == 2U &&
           records[1].error_code == ERR_DISK;
}

static uint8_t log_test_wrap_and_overwrite(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;
    static const char* messages[] = { "a", "b", "c", "d", "e" };

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    for (uint32_t index = 0U; index < 5U; index++) {
        log_ring_store(&state, LOG_LEVEL_INFO, "TEST", messages[index],
                       0U, 0, index);
    }
    return state.count == LOG_PRIVATE_TEST_CAPACITY && state.head == 1U &&
           state.overwritten_records == 1U &&
           records[state.head].sequence == 2U;
}

static uint8_t log_test_repetition_grouping(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    log_ring_store(&state, LOG_LEVEL_WARN, "TEST", "igual", 0U, 0, 2U);
    log_ring_store(&state, LOG_LEVEL_WARN, "TEST", "igual", 0U, 0, 9U);
    return state.count == 1U && records[0].occurrences == 2U &&
           records[0].first_tick == 2U && records[0].last_tick == 9U &&
           state.grouped_events == 1U;
}

static uint8_t log_test_safe_truncation(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;
    static const char long_module[] = "MODULO-MUITO-LONGO-PARA-O-LIMITE";
    static const char long_message[] =
        "Mensagem propositalmente maior que oitenta bytes para validar "
        "truncamento seguro e terminacao nula do registro privado.";

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    log_ring_store(&state, LOG_LEVEL_INFO, long_module, long_message,
                   0U, 0, 1U);
    return records[0].module[LOG_MODULE_CAPACITY - 1U] == '\0' &&
           records[0].message[LOG_MESSAGE_CAPACITY - 1U] == '\0' &&
           (records[0].flags & LOG_RECORD_FLAG_MODULE_TRUNCATED) &&
           (records[0].flags & LOG_RECORD_FLAG_MESSAGE_TRUNCATED) &&
           state.truncated_events == 1U;
}

static uint8_t log_test_optional_error_code(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    log_ring_store(&state, LOG_LEVEL_WARN, "TEST", "sem codigo", 0U, 0, 1U);
    log_ring_store(&state, LOG_LEVEL_ERROR, "TEST", "com codigo", 1U,
                   ERR_TIMEOUT, 2U);
    return !(records[0].flags & LOG_RECORD_FLAG_HAS_ERROR_CODE) &&
           (records[1].flags & LOG_RECORD_FLAG_HAS_ERROR_CODE) &&
           records[1].error_code == ERR_TIMEOUT;
}

static uint8_t log_test_clear_behavior(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;
    uint32_t next_sequence;

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    log_ring_store(&state, LOG_LEVEL_INFO, "TEST", "antes", 0U, 0, 1U);
    log_ring_store(&state, LOG_LEVEL_INFO, "TEST", "antes", 0U, 0, 2U);
    next_sequence = state.next_sequence;
    state.buffer_level = LOG_LEVEL_DEBUG;
    state.console_level = LOG_LEVEL_WARN;
    state.dropped_events = 3U;
    log_ring_clear(&state);
    log_ring_store(&state, LOG_LEVEL_INFO, "TEST", "depois", 0U, 0, 3U);
    return state.count == 1U && state.clear_count == 1U &&
           state.next_sequence == next_sequence + 1U &&
           records[0].sequence == next_sequence &&
           state.buffer_level == LOG_LEVEL_DEBUG &&
           state.console_level == LOG_LEVEL_WARN &&
           state.grouped_events == 1U && state.dropped_events == 3U;
}

static uint8_t log_test_text_serialization(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;
    char output[LOG_TEST_TEXT_CAPACITY];

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    log_ring_store(&state, LOG_LEVEL_INFO, "TEST", "primeiro", 0U, 0, 1U);
    log_ring_store(&state, LOG_LEVEL_ERROR, "TEST", "segundo", 1U,
                   ERR_DISK, 2U);
    log_serialize_records(records, state.count, output, sizeof(output));
    return kstrcmp(output,
                   "[INF] [TEST] primeiro\n[ERR] [TEST] segundo\n") == 0;
}

static uint8_t log_test_level_filtering(void) {
    log_record_t records[LOG_PRIVATE_TEST_CAPACITY];
    log_ring_state_t state;
    log_store_result_t filtered;
    log_store_result_t stored;

    log_ring_initialize(&state, records, LOG_PRIVATE_TEST_CAPACITY);
    state.buffer_level = LOG_LEVEL_WARN;
    filtered = log_ring_store(&state, LOG_LEVEL_INFO, "TEST", "oculto",
                              0U, 0, 1U);
    stored = log_ring_store(&state, LOG_LEVEL_ERROR, "TEST", "visivel",
                            0U, 0, 2U);
    return filtered.action == LOG_STORE_FILTERED &&
           stored.action == LOG_STORE_NEW && state.count == 1U;
}

static void log_self_test_mark(log_self_test_result_t* result,
                               uint8_t* field, uint8_t passed) {
    *field = passed;
    if (passed) result->passed++;
    else result->failed++;
}

int log_self_test(log_self_test_result_t* result) {
    if (!result) {
        log_note_dropped_event();
        LOG_ERROR("LOG", "Destino nulo para autoteste do log");
        return ERR_NULL;
    }

    kmemset(result, 0, sizeof(*result));
    log_self_test_mark(result, &result->order_and_metadata,
                       log_test_order_and_metadata());
    log_self_test_mark(result, &result->wrap_and_overwrite,
                       log_test_wrap_and_overwrite());
    log_self_test_mark(result, &result->repetition_grouping,
                       log_test_repetition_grouping());
    log_self_test_mark(result, &result->safe_truncation,
                       log_test_safe_truncation());
    log_self_test_mark(result, &result->optional_error_code,
                       log_test_optional_error_code());
    log_self_test_mark(result, &result->clear_behavior,
                       log_test_clear_behavior());
    log_self_test_mark(result, &result->text_serialization,
                       log_test_text_serialization());
    log_self_test_mark(result, &result->level_filtering,
                       log_test_level_filtering());
    if (result->passed + result->failed != LOG_SELF_TEST_TOTAL) {
        result->failed++;
    }
    return result->failed ? ERR_STATE : OK;
}
