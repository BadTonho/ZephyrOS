#include "test_protocol_core.h"

#define Q3CHECK_ERROR_PROPAGATION_ONLY 1
#define TEST_PROTOCOL_CORE_PREFIX "@@ZTEST/1 "
#define TEST_PROTOCOL_CORE_PREFIX_LENGTH 10U
#define TEST_PROTOCOL_CORE_MIN_TOKEN_LENGTH 1U
#define TEST_PROTOCOL_CORE_CRC_DIGITS 8U
#define TEST_PROTOCOL_CORE_DECIMAL_BASE 10U
#define TEST_PROTOCOL_CORE_CRC_POLYNOMIAL 0xEDB88320U

static void core_zero(void* target, uint32_t size) {
    uint8_t* bytes = (uint8_t*)target;

    if (!bytes) return;
    for (uint32_t index = 0U; index < size; index++) bytes[index] = 0U;
}

static void core_copy(void* target, const void* source, uint32_t size) {
    uint8_t* destination = (uint8_t*)target;
    const uint8_t* origin = (const uint8_t*)source;

    if (!destination || !origin) return;
    for (uint32_t index = 0U; index < size; index++) destination[index] = origin[index];
}

static uint32_t core_length(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

static int core_token_valid(const char* value, uint32_t length,
                            uint32_t capacity) {
    if (!value || length < TEST_PROTOCOL_CORE_MIN_TOKEN_LENGTH ||
        length >= capacity) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        char current = value[index];

        if (!((current >= 'A' && current <= 'Z') ||
              (current >= 'a' && current <= 'z') ||
              (current >= '0' && current <= '9') || current == '_' ||
              current == '-' || current == '.' || current == ':')) {
            return 0;
        }
    }
    return 1;
}

static int core_token_equals(const char* value, uint32_t length,
                             const char* expected) {
    uint32_t expected_length;

    if (!value || !expected) return 0;
    expected_length = core_length(expected);
    if (length != expected_length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        if (value[index] != expected[index]) return 0;
    }
    return 1;
}

static uint32_t core_append_text(char* output, uint32_t capacity,
                                 uint32_t length, const char* text) {
    uint32_t text_length;

    if (!output || !text || length >= capacity) return capacity;
    text_length = core_length(text);
    if (text_length > capacity - length - 1U) return capacity;
    core_copy(output + length, text, text_length);
    length += text_length;
    output[length] = '\0';
    return length;
}

static uint32_t core_append_uint(char* output, uint32_t capacity,
                                 uint32_t length, uint32_t value) {
    char digits[11];
    uint32_t digit_count = 0U;

    if (value == 0U) digits[digit_count++] = '0';
    while (value > 0U && digit_count < sizeof(digits)) {
        digits[digit_count++] =
            (char)('0' + (value % TEST_PROTOCOL_CORE_DECIMAL_BASE));
        value /= TEST_PROTOCOL_CORE_DECIMAL_BASE;
    }
    while (digit_count > 0U) {
        char digit[2] = { digits[--digit_count], '\0' };

        length = core_append_text(output, capacity, length, digit);
    }
    return length;
}

static uint32_t core_append_hex(char* output, uint32_t capacity,
                                uint32_t length, uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[2];

    for (uint32_t shift = 28U; ; shift -= 4U) {
        text[0] = digits[(value >> shift) & 0x0FU];
        text[1] = '\0';
        length = core_append_text(output, capacity, length, text);
        if (shift == 0U) break;
    }
    return length;
}

static uint32_t core_crc32(const char* text, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;

    if (!text) return 0U;
    for (uint32_t index = 0U; index < length; index++) {
        crc ^= (uint8_t)text[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) ? TEST_PROTOCOL_CORE_CRC_POLYNOMIAL : 0U);
        }
    }
    return ~crc;
}

static int core_prefix_valid(const char* frame, uint32_t length) {
    if (!frame || length < TEST_PROTOCOL_CORE_PREFIX_LENGTH) return 0;
    for (uint32_t index = 0U; index < TEST_PROTOCOL_CORE_PREFIX_LENGTH; index++) {
        if (frame[index] != TEST_PROTOCOL_CORE_PREFIX[index]) return 0;
    }
    return 1;
}

static int core_fields_valid(const char* frame, uint32_t length) {
    const char* cursor;
    const char* field_names[TEST_PROTOCOL_CORE_MAX_FIELDS];
    uint32_t field_name_lengths[TEST_PROTOCOL_CORE_MAX_FIELDS];
    uint32_t field_count = 0U;

    if (!core_prefix_valid(frame, length)) return 0;
    cursor = frame + TEST_PROTOCOL_CORE_PREFIX_LENGTH;
    if (!*cursor || *cursor == ' ') return 0;
    while (*cursor) {
        const char* token_start = cursor;
        uint32_t token_length = 0U;
        uint32_t equals = 0U;
        uint32_t name_length = 0U;

        while (*cursor && *cursor != ' ') {
            if (*cursor == '=') equals++;
            cursor++;
            token_length++;
        }
        if (token_length < 3U || equals != 1U) return 0;
        while (name_length < token_length && token_start[name_length] != '=') {
            name_length++;
        }
        if (!name_length || field_count >= TEST_PROTOCOL_CORE_MAX_FIELDS) return 0;
        for (uint32_t index = 0U; index < field_count; index++) {
            uint8_t same_name = 1U;

            if (field_name_lengths[index] != name_length) continue;
            for (uint32_t name_index = 0U; name_index < name_length;
                 name_index++) {
                if (token_start[name_index] != field_names[index][name_index]) {
                    same_name = 0U;
                    break;
                }
            }
            if (same_name) return 0;
        }
        field_names[field_count] = token_start;
        field_name_lengths[field_count++] = name_length;
        for (uint32_t index = 0U; index < token_length; index++) {
            char current = token_start[index];

            if (current == '=') continue;
            if (!((current >= 'A' && current <= 'Z') ||
                  (current >= 'a' && current <= 'z') ||
                  (current >= '0' && current <= '9') || current == '_' ||
                  current == '-' || current == '.' || current == ':')) {
                return 0;
            }
        }
        if (!*cursor) return 1;
        cursor++;
        if (!*cursor || *cursor == ' ') return 0;
    }
    return 0;
}

static int core_parse_uint(const char* value, uint32_t length,
                           uint32_t* output) {
    uint32_t parsed = 0U;

    if (!value || !output || !length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        uint32_t digit;

        if (value[index] < '0' || value[index] > '9') return 0;
        digit = (uint32_t)(value[index] - '0');
        if (parsed > (0xFFFFFFFFU - digit) / TEST_PROTOCOL_CORE_DECIMAL_BASE) {
            return 0;
        }
        parsed = parsed * TEST_PROTOCOL_CORE_DECIMAL_BASE + digit;
    }
    *output = parsed;
    return 1;
}

static int core_parse_hex(const char* value, uint32_t length,
                          uint32_t* output) {
    uint32_t parsed = 0U;

    if (!value || !output || length != TEST_PROTOCOL_CORE_CRC_DIGITS) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        char current = value[index];
        uint32_t digit;

        if (current >= '0' && current <= '9') digit = (uint32_t)(current - '0');
        else if (current >= 'A' && current <= 'F') digit = (uint32_t)(current - 'A') + 10U;
        else if (current >= 'a' && current <= 'f') digit = (uint32_t)(current - 'a') + 10U;
        else return 0;
        parsed = (parsed << 4U) | digit;
    }
    *output = parsed;
    return 1;
}

static int core_field(const char* frame, const char* name,
                      const char** value, uint32_t* length) {
    uint32_t name_length;
    const char* cursor;

    if (!frame || !name || !value || !length) return 0;
    name_length = core_length(name);
    cursor = frame + TEST_PROTOCOL_CORE_PREFIX_LENGTH;
    while (*cursor) {
        const char* token_start = cursor;
        uint32_t token_length = 0U;

        while (*cursor && *cursor != ' ') {
            cursor++;
            token_length++;
        }
        if (token_length > name_length && token_start[name_length] == '=') {
            uint8_t matches = 1U;

            for (uint32_t index = 0U; index < name_length; index++) {
                if (token_start[index] != name[index]) matches = 0U;
            }
            if (matches && token_length > name_length + 1U) {
                *value = token_start + name_length + 1U;
                *length = token_length - name_length - 1U;
                return 1;
            }
        }
        while (*cursor == ' ') cursor++;
    }
    return 0;
}

static int core_frame_crc_valid(const char* frame, uint32_t length) {
    const char* marker = 0;
    uint32_t expected;

    if (!frame || length < TEST_PROTOCOL_CORE_PREFIX_LENGTH + 6U) return 0;
    for (uint32_t index = length; index >= 5U; index--) {
        if (frame[index - 5U] == ' ' && frame[index - 4U] == 'c' &&
            frame[index - 3U] == 'r' && frame[index - 2U] == 'c' &&
            frame[index - 1U] == '=') {
            marker = frame + index - 5U;
            break;
        }
        if (index == 5U) break;
    }
    if (!marker || (uint32_t)(frame + length - marker) != 13U) return 0;
    if (!core_parse_hex(marker + 5U, 8U, &expected)) return 0;
    return core_crc32(frame, (uint32_t)(marker - frame)) == expected;
}

static const char* core_event_name(test_protocol_event_type_t type) {
    switch (type) {
        case TEST_PROTOCOL_EVENT_READY: return "READY";
        case TEST_PROTOCOL_EVENT_HEARTBEAT: return "HEARTBEAT";
        case TEST_PROTOCOL_EVENT_BEGIN: return "BEGIN";
        case TEST_PROTOCOL_EVENT_PASS: return "PASS";
        case TEST_PROTOCOL_EVENT_FAIL: return "FAIL";
        case TEST_PROTOCOL_EVENT_SKIP: return "SKIP";
        case TEST_PROTOCOL_EVENT_BLOCKED: return "BLOCKED";
        case TEST_PROTOCOL_EVENT_PANIC: return "PANIC";
        case TEST_PROTOCOL_EVENT_TIMEOUT: return "TIMEOUT";
        default: return 0;
    }
}

static const char* core_error_name(int result) {
    if (result == ERR_INVALID) return "ERR_INVALID";
    if (result == ERR_STATE) return "ERR_STATE";
    if (result == ERR_TIMEOUT) return "ERR_TIMEOUT";
    if (result == ERR_UNAVAILABLE) return "ERR_UNAVAILABLE";
    if (result == ERR_AGAIN) return "ERR_AGAIN";
    if (result == ERR_MEM) return "ERR_MEM";
    if (result == ERR_NOT_FOUND) return "ERR_NOT_FOUND";
    if (result == ERR_CANCELLED) return "ERR_CANCELLED";
    return "ERR_UNKNOWN";
}

static void core_copy_reason(char* destination, const char* reason) {
    uint32_t length;

    if (!destination) return;
    if (!reason) return;
    length = core_length(reason);
    if (!core_token_valid(reason, length, TEST_PROTOCOL_CASE_CAPACITY)) {
        reason = "ERR_STATE";
        length = core_length(reason);
    }
    core_copy(destination, reason, length);
    destination[length] = '\0';
}

static int core_emit_event(test_protocol_core_t* core,
                           test_protocol_event_type_t type, uint8_t has_case,
                           const char* case_id, uint32_t case_length,
                           uint32_t iteration, uint32_t seed, uint32_t ticks,
                           const char* reason) {
    test_protocol_event_t event;

    if (!core || !core->callbacks.emit) return ERR_NULL;
    if (!core_event_name(type)) return ERR_INVALID;
    if (has_case && !core_token_valid(case_id, case_length,
                                      TEST_PROTOCOL_CASE_CAPACITY)) {
        return ERR_INVALID;
    }
    core_zero(&event, sizeof(event));
    event.type = type;
    event.has_case = has_case;
    event.sequence = ++core->guest_sequence;
    event.iteration = iteration;
    event.seed = seed;
    event.ticks = ticks;
    if (core->session) core_copy(event.run, core->run, sizeof(event.run));
    if (has_case) {
        core_copy(event.case_id, case_id, case_length);
        event.case_id[case_length] = '\0';
    }
    core_copy_reason(event.reason, reason);
    return core->callbacks.emit(&event, core->callbacks.context);
}

static int core_emit_simple(test_protocol_core_t* core,
                            test_protocol_event_type_t type,
                            const char* reason) {
    return core_emit_event(core, type, 0U, 0, 0U, 0U, 0U,
                           core->current_ticks, reason);
}

static int core_emit_case(test_protocol_core_t* core,
                          test_protocol_event_type_t type,
                          const char* case_id, uint32_t case_length,
                          uint32_t iteration, uint32_t seed,
                          const char* reason) {
    return core_emit_event(core, type, 1U, case_id, case_length, iteration,
                           seed, core->current_ticks, reason);
}

static int core_handle_frame(test_protocol_core_t* core, char* frame,
                             uint32_t length) {
    const char* command;
    const char* value;
    uint32_t command_length;
    uint32_t value_length;
    uint32_t sequence;

    if (!frame || length < TEST_PROTOCOL_CORE_PREFIX_LENGTH ||
        core_length(frame) != length || !core_fields_valid(frame, length) ||
        !core_frame_crc_valid(frame, length)) {
        if (core->session) core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED,
                                             "ERR_INVALID");
        return ERR_INVALID;
    }
    if (!core_field(frame, "cmd", &command, &command_length) ||
        !core_field(frame, "seq", &value, &value_length) ||
        !core_parse_uint(value, value_length, &sequence)) {
        if (core->session) core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED,
                                             "ERR_AGAIN");
        return ERR_AGAIN;
    }
    if (core->session && sequence == core->host_sequence &&
        core_token_equals(command, command_length, "HELLO") &&
        core_field(frame, "run", &value, &value_length) &&
        core_token_equals(value, value_length, core->run)) {
        return OK;
    }
    if (sequence != core->host_sequence + 1U) {
        if (core->session) core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED,
                                             "ERR_AGAIN");
        return ERR_AGAIN;
    }
    core->host_sequence = sequence;
    if (core_token_equals(command, command_length, "HELLO")) {
        if (!core_field(frame, "run", &value, &value_length) ||
            !core_token_valid(value, value_length, TEST_PROTOCOL_RUN_CAPACITY)) {
            core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED, "ERR_INVALID");
            return ERR_INVALID;
        }
        core_zero(core->run, sizeof(core->run));
        core_copy(core->run, value, value_length);
        core->session = 1U;
        core->ready_sent = 0U;
        return OK;
    }
    if (!core->session || !core->ready_sent) {
        core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED, "ERR_AGAIN");
        return ERR_AGAIN;
    }
    if (core_token_equals(command, command_length, "PING")) {
        return core_emit_simple(core, TEST_PROTOCOL_EVENT_HEARTBEAT, 0);
    }
    if (core_token_equals(command, command_length, "ABORT")) {
        return core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED,
                                "ERR_CANCELLED");
    }
    if (core_token_equals(command, command_length, "RUN")) {
        const char* case_value = 0;
        const char* iteration_value;
        const char* seed_value;
        uint32_t case_length;
        uint32_t iteration_length;
        uint32_t seed_length;
        uint32_t iteration;
        uint32_t seed;
        int result;

        if (core->busy || !core_field(frame, "case", &case_value, &case_length) ||
            !core_field(frame, "iteration", &iteration_value, &iteration_length) ||
            !core_field(frame, "seed", &seed_value, &seed_length) ||
            !core_parse_uint(iteration_value, iteration_length, &iteration) ||
            !core_parse_uint(seed_value, seed_length, &seed)) {
            if (case_value && core_token_valid(case_value, case_length,
                                               TEST_PROTOCOL_CASE_CAPACITY)) {
                core_emit_case(core, TEST_PROTOCOL_EVENT_BLOCKED, case_value,
                               case_length, 0U, 0U,
                               core->busy ? "ERR_AGAIN" : "ERR_INVALID");
            } else {
                core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED,
                                 core->busy ? "ERR_AGAIN" : "ERR_INVALID");
            }
            return core->busy ? ERR_AGAIN : ERR_INVALID;
        }
        core->busy = 1U;
        core_emit_case(core, TEST_PROTOCOL_EVENT_BEGIN, case_value, case_length,
                       iteration, seed, 0);
        result = core->callbacks.run_case ?
                 core->callbacks.run_case(core->callbacks.context, case_value,
                                          case_length, iteration, seed) :
                 ERR_UNAVAILABLE;
        core_emit_case(core, result == OK ? TEST_PROTOCOL_EVENT_PASS :
                       TEST_PROTOCOL_EVENT_FAIL, case_value, case_length,
                       iteration, seed, result == OK ? 0 : core_error_name(result));
        core->busy = 0U;
        return result;
    }
    core_emit_simple(core, TEST_PROTOCOL_EVENT_BLOCKED, "ERR_INVALID");
    return ERR_INVALID;
}

int test_protocol_core_init(test_protocol_core_t* core,
                            const test_protocol_core_callbacks_t* callbacks) {
    if (!core || !callbacks || !callbacks->emit) return ERR_NULL;
    core_zero(core, sizeof(*core));
    core->callbacks = *callbacks;
    core->initialized = 1U;
    return OK;
}

int test_protocol_core_feed_byte(test_protocol_core_t* core, uint8_t byte) {
    if (!core) return ERR_NULL;
    if (!core->initialized) return ERR_STATE;
    if (byte == '\n') {
        int result = OK;

        if (!core->rx_discard && core->rx_length > 0U) {
            core->rx[core->rx_length] = '\0';
            result = core_handle_frame(core, core->rx, core->rx_length);
        }
        core->rx_length = 0U;
        core->rx_discard = 0U;
        return result;
    }
    if (byte == 0U || byte == '\r' || byte < 0x20U || byte > 0x7EU) {
        core->rx_discard = 1U;
        return ERR_INVALID;
    }
    if (core->rx_length >= TEST_PROTOCOL_FRAME_CAPACITY - 1U) {
        core->rx_discard = 1U;
        return ERR_OVERFLOW;
    }
    if (!core->rx_discard) core->rx[core->rx_length++] = (char)byte;
    return OK;
}

int test_protocol_core_poll(test_protocol_core_t* core, uint32_t ticks) {
    int result;

    if (!core) return ERR_NULL;
    if (!core->initialized) return ERR_STATE;
    core->current_ticks = ticks;
    if (core->session && core->boot_ready && !core->ready_sent) {
        result = core_emit_simple(core, TEST_PROTOCOL_EVENT_READY, 0);
        if (result != OK) return result;
        core->ready_sent = 1U;
        core->last_heartbeat = ticks;
    }
    if (!core->session || !core->ready_sent) return OK;
    if (ticks - core->last_heartbeat >= TEST_PROTOCOL_HEARTBEAT_TICKS) {
        result = core_emit_simple(core, TEST_PROTOCOL_EVENT_HEARTBEAT, 0);
        if (result != OK) return result;
        core->last_heartbeat = ticks;
    }
    return OK;
}

int test_protocol_core_set_ticks(test_protocol_core_t* core, uint32_t ticks) {
    if (!core) return ERR_NULL;
    if (!core->initialized) return ERR_STATE;
    core->current_ticks = ticks;
    return OK;
}

int test_protocol_core_set_boot_ready(test_protocol_core_t* core) {
    if (!core) return ERR_NULL;
    if (!core->initialized) return ERR_STATE;
    core->boot_ready = 1U;
    return OK;
}

int test_protocol_core_emit(test_protocol_core_t* core,
                            test_protocol_event_type_t type,
                            const char* reason) {
    if (!core) return ERR_NULL;
    if (!core->initialized) return ERR_STATE;
    if (!core->session) return ERR_UNAVAILABLE;
    return core_emit_simple(core, type, reason);
}

uint8_t test_protocol_core_is_active(const test_protocol_core_t* core) {
    return core && core->initialized && core->session;
}

uint32_t test_protocol_core_format_event(const test_protocol_event_t* event,
                                         char* output, uint32_t capacity) {
    const char* name;
    uint32_t length = 0U;
    uint32_t crc;

    if (!event || !output || capacity < 2U) return 0U;
    name = core_event_name(event->type);
    if (!name || !core_token_valid(event->run, core_length(event->run),
                                   TEST_PROTOCOL_RUN_CAPACITY)) return 0U;
    if (event->has_case &&
        !core_token_valid(event->case_id, core_length(event->case_id),
                          TEST_PROTOCOL_CASE_CAPACITY)) return 0U;
    if (event->reason[0] &&
        !core_token_valid(event->reason, core_length(event->reason),
                          TEST_PROTOCOL_CASE_CAPACITY)) return 0U;
    core_zero(output, capacity);
    length = core_append_text(output, capacity, length,
                              TEST_PROTOCOL_CORE_PREFIX);
    length = core_append_text(output, capacity, length, "event=");
    length = core_append_text(output, capacity, length, name);
    if (event->has_case) {
        length = core_append_text(output, capacity, length, " case=");
        length = core_append_text(output, capacity, length, event->case_id);
        length = core_append_text(output, capacity, length, " iteration=");
        length = core_append_uint(output, capacity, length, event->iteration);
        length = core_append_text(output, capacity, length, " seed=");
        length = core_append_uint(output, capacity, length, event->seed);
    }
    length = core_append_text(output, capacity, length, " run=");
    length = core_append_text(output, capacity, length, event->run);
    length = core_append_text(output, capacity, length, " seq=");
    length = core_append_uint(output, capacity, length, event->sequence);
    if (event->type == TEST_PROTOCOL_EVENT_HEARTBEAT) {
        length = core_append_text(output, capacity, length, " ticks=");
        length = core_append_uint(output, capacity, length, event->ticks);
    }
    if (event->reason[0]) {
        length = core_append_text(output, capacity, length, " reason=");
        length = core_append_text(output, capacity, length, event->reason);
    }
    if (length >= capacity || capacity - length < 14U) return 0U;
    crc = core_crc32(output, length);
    length = core_append_text(output, capacity, length, " crc=");
    length = core_append_hex(output, capacity, length, crc);
    length = core_append_text(output, capacity, length, "\n");
    return length < capacity ? length : 0U;
}
