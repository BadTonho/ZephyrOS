#include "core/test_protocol.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/serial.h"

#define TEST_PROTOCOL_PREFIX "@@ZTEST/1 "
#define TEST_PROTOCOL_PREFIX_LENGTH 10U
#define TEST_PROTOCOL_RX_BUDGET 32U
#define TEST_PROTOCOL_MIN_TOKEN_LENGTH 1U
#define TEST_PROTOCOL_CRC_DIGITS 8U
#define TEST_PROTOCOL_DECIMAL_BASE 10U
#define TEST_PROTOCOL_CRC_POLYNOMIAL 0xEDB88320U
#define TEST_PROTOCOL_MAX_FIELDS 16U

static uint8_t protocol_initialized;
static uint8_t protocol_session;
static uint8_t protocol_boot_ready;
static uint8_t protocol_ready_sent;
static uint8_t protocol_busy;
static uint8_t protocol_rx_discard;
static uint32_t protocol_host_sequence;
static uint32_t protocol_guest_sequence;
static uint32_t protocol_last_heartbeat;
static uint32_t protocol_rx_length;
static char protocol_run[TEST_PROTOCOL_RUN_CAPACITY];
static char protocol_rx[TEST_PROTOCOL_FRAME_CAPACITY];

static uint32_t protocol_append_text(char* output, uint32_t length,
                                     const char* text) {
    uint32_t text_length;

    if (!output || !text || length >= TEST_PROTOCOL_FRAME_CAPACITY) {
        return length;
    }
    text_length = kstrlen(text);
    if (text_length > TEST_PROTOCOL_FRAME_CAPACITY - length - 1U) {
        return TEST_PROTOCOL_FRAME_CAPACITY;
    }
    kmemcpy(output + length, text, text_length);
    return length + text_length;
}

static uint32_t protocol_append_span(char* output, uint32_t length,
                                     const char* text, uint32_t text_length) {
    if (!output || !text || length >= TEST_PROTOCOL_FRAME_CAPACITY ||
        text_length > TEST_PROTOCOL_FRAME_CAPACITY - length - 1U) {
        return TEST_PROTOCOL_FRAME_CAPACITY;
    }
    kmemcpy(output + length, text, text_length);
    return length + text_length;
}

static uint32_t protocol_append_uint(char* output, uint32_t length,
                                     uint32_t value) {
    char digits[11];
    uint32_t digit_count = 0U;

    if (value == 0U) digits[digit_count++] = '0';
    while (value > 0U && digit_count < sizeof(digits)) {
        digits[digit_count++] = (char)('0' + (value % TEST_PROTOCOL_DECIMAL_BASE));
        value /= TEST_PROTOCOL_DECIMAL_BASE;
    }
    while (digit_count > 0U) {
        char digit[2] = { digits[--digit_count], '\0' };
        length = protocol_append_text(output, length, digit);
    }
    return length;
}

static uint32_t protocol_append_hex(char* output, uint32_t length,
                                    uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[2];
    uint32_t shift;

    for (shift = 28U; ; shift -= 4U) {
        text[0] = digits[(value >> shift) & 0x0FU];
        text[1] = '\0';
        length = protocol_append_text(output, length, text);
        if (shift == 0U) break;
    }
    return length;
}

static int protocol_token_equals(const char* value, uint32_t length,
                                 const char* expected) {
    uint32_t expected_length;

    if (!value || !expected) return 0;
    expected_length = kstrlen(expected);
    if (length != expected_length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        if (value[index] != expected[index]) return 0;
    }
    return 1;
}

static uint32_t protocol_crc32(const char* text, uint32_t length) {
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t index = 0U; index < length; index++) {
        crc ^= (uint8_t)text[index];
        for (uint32_t bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) ? TEST_PROTOCOL_CRC_POLYNOMIAL : 0U);
        }
    }
    return ~crc;
}

static int protocol_token_valid(const char* value, uint32_t length,
                                uint32_t capacity) {
    if (!value || length < TEST_PROTOCOL_MIN_TOKEN_LENGTH ||
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

static const char* protocol_safe_reason(const char* reason,
                                        const char* fallback) {
    uint32_t length;

    if (!reason || !fallback) return fallback;
    length = kstrlen(reason);
    return protocol_token_valid(reason, length, TEST_PROTOCOL_CASE_CAPACITY) ?
           reason : fallback;
}

static int protocol_prefix_valid(const char* frame, uint32_t length) {
    if (!frame || length < TEST_PROTOCOL_PREFIX_LENGTH) return 0;
    for (uint32_t index = 0U; index < TEST_PROTOCOL_PREFIX_LENGTH; index++) {
        if (frame[index] != TEST_PROTOCOL_PREFIX[index]) return 0;
    }
    return 1;
}

static int protocol_fields_valid(const char* frame, uint32_t length) {
    const char* cursor;
    const char* field_names[TEST_PROTOCOL_MAX_FIELDS];
    uint32_t field_name_lengths[TEST_PROTOCOL_MAX_FIELDS];
    uint32_t field_count = 0U;

    if (!protocol_prefix_valid(frame, length)) return 0;
    cursor = frame + TEST_PROTOCOL_PREFIX_LENGTH;
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
        if (!name_length || field_count >= TEST_PROTOCOL_MAX_FIELDS) return 0;
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

static int protocol_parse_uint(const char* value, uint32_t length,
                               uint32_t* output) {
    uint32_t parsed = 0U;

    if (!value || !output || !length) return 0;
    for (uint32_t index = 0U; index < length; index++) {
        uint32_t digit;

        if (value[index] < '0' || value[index] > '9') return 0;
        digit = (uint32_t)(value[index] - '0');
        if (parsed > (0xFFFFFFFFU - digit) / TEST_PROTOCOL_DECIMAL_BASE) {
            return 0;
        }
        parsed = parsed * TEST_PROTOCOL_DECIMAL_BASE + digit;
    }
    *output = parsed;
    return 1;
}

static int protocol_parse_hex(const char* value, uint32_t length,
                              uint32_t* output) {
    uint32_t parsed = 0U;

    if (!value || !output || length != TEST_PROTOCOL_CRC_DIGITS) return 0;
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

static int protocol_field(const char* frame, const char* name,
                          const char** value, uint32_t* length) {
    uint32_t name_length;
    const char* cursor;

    if (!frame || !name || !value || !length) return 0;
    name_length = kstrlen(name);
    cursor = frame + TEST_PROTOCOL_PREFIX_LENGTH;
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

static int protocol_frame_crc_valid(const char* frame, uint32_t length) {
    const char* marker = 0;
    uint32_t expected;

    if (!frame || length < TEST_PROTOCOL_PREFIX_LENGTH + 6U) return 0;
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
    if (!protocol_parse_hex(marker + 5U, 8U, &expected)) return 0;
    return protocol_crc32(frame, (uint32_t)(marker - frame)) == expected;
}

static uint32_t protocol_emit_prefix(char* prefix, uint32_t length) {
    uint32_t crc;
    uint32_t written;

    if (!prefix || length > TEST_PROTOCOL_FRAME_CAPACITY - 14U) return 0U;
    crc = protocol_crc32(prefix, length);
    prefix[length] = ' ';
    prefix[length + 1U] = 'c';
    prefix[length + 2U] = 'r';
    prefix[length + 3U] = 'c';
    prefix[length + 4U] = '=';
    length = protocol_append_hex(prefix, length + 5U, crc);
    prefix[length++] = '\n';
    written = serial_write_text(prefix, length);
    serial_flush(SERIAL_TX_FLUSH_BUDGET);
    return written == length ? length : 0U;
}

static uint32_t protocol_emit_simple(const char* event, const char* reason) {
    char frame[TEST_PROTOCOL_FRAME_CAPACITY];
    uint32_t length = 0U;

    kmemset(frame, 0U, sizeof(frame));
    length = protocol_append_text(frame, length, TEST_PROTOCOL_PREFIX);
    length = protocol_append_text(frame, length, "event=");
    length = protocol_append_text(frame, length, event);
    length = protocol_append_text(frame, length, " run=");
    length = protocol_append_text(frame, length, protocol_run);
    length = protocol_append_text(frame, length, " seq=");
    length = protocol_append_uint(frame, length, ++protocol_guest_sequence);
    if (reason) {
        length = protocol_append_text(frame, length, " reason=");
        length = protocol_append_text(frame, length, reason);
    }
    return protocol_emit_prefix(frame, length);
}

static uint32_t protocol_emit_heartbeat(uint32_t ticks) {
    char frame[TEST_PROTOCOL_FRAME_CAPACITY];
    uint32_t length = 0U;

    kmemset(frame, 0U, sizeof(frame));
    length = protocol_append_text(frame, length, TEST_PROTOCOL_PREFIX);
    length = protocol_append_text(frame, length, "event=HEARTBEAT run=");
    length = protocol_append_text(frame, length, protocol_run);
    length = protocol_append_text(frame, length, " seq=");
    length = protocol_append_uint(frame, length, ++protocol_guest_sequence);
    length = protocol_append_text(frame, length, " ticks=");
    length = protocol_append_uint(frame, length, ticks);
    return protocol_emit_prefix(frame, length);
}

static uint32_t protocol_emit_case(const char* event, const char* case_id,
                                   uint32_t case_length, uint32_t iteration,
                                   uint32_t seed, const char* reason) {
    char frame[TEST_PROTOCOL_FRAME_CAPACITY];
    uint32_t length = 0U;

    kmemset(frame, 0U, sizeof(frame));
    length = protocol_append_text(frame, length, TEST_PROTOCOL_PREFIX);
    length = protocol_append_text(frame, length, "event=");
    length = protocol_append_text(frame, length, event);
    length = protocol_append_text(frame, length, " case=");
    length = protocol_append_span(frame, length, case_id, case_length);
    length = protocol_append_text(frame, length, " iteration=");
    length = protocol_append_uint(frame, length, iteration);
    length = protocol_append_text(frame, length, " seed=");
    length = protocol_append_uint(frame, length, seed);
    length = protocol_append_text(frame, length, " seq=");
    length = protocol_append_uint(frame, length, ++protocol_guest_sequence);
    if (reason) {
        length = protocol_append_text(frame, length, " reason=");
        length = protocol_append_text(frame, length, reason);
    }
    return protocol_emit_prefix(frame, length);
}

static const char* protocol_error_name(int result) {
    if (result == ERR_INVALID) return "ERR_INVALID";
    if (result == ERR_STATE) return "ERR_STATE";
    if (result == ERR_TIMEOUT) return "ERR_TIMEOUT";
    if (result == ERR_UNAVAILABLE) return "ERR_UNAVAILABLE";
    if (result == ERR_AGAIN) return "ERR_AGAIN";
    if (result == ERR_MEM) return "ERR_MEM";
    return "ERR_UNKNOWN";
}

static int protocol_boot_case(void) {
    if (protocol_boot_ready) return OK;
    LOG_WARN("TEST", "Caso de boot solicitado antes do estado READY");
    return ERR_AGAIN;
}

static int protocol_run_case(const char* case_id, uint32_t length,
                             uint32_t iteration, uint32_t seed) {
    int result;

    if (!protocol_token_valid(case_id, length, TEST_PROTOCOL_CASE_CAPACITY)) {
        protocol_emit_case("BLOCKED", "invalid-case", 12U, iteration, seed,
                           "ERR_INVALID");
        return 1;
    }
    if (protocol_token_equals(case_id, length, "qemu:tst2:boot-ready")) {
        protocol_emit_case("BEGIN", case_id, length, iteration, seed, 0);
        result = protocol_boot_case();
        protocol_emit_case(result == OK ? "PASS" : "FAIL", case_id, length,
                           iteration, seed, result == OK ? 0 :
                           protocol_error_name(result));
        return 1;
    }
    protocol_emit_case("BLOCKED", case_id, length, iteration, seed,
                       "ERR_NOT_FOUND");
    return 1;
}

static int protocol_handle_frame(char* frame, uint32_t length) {
    const char* command;
    const char* value;
    uint32_t command_length;
    uint32_t value_length;
    uint32_t sequence;

    if (!frame || length < TEST_PROTOCOL_PREFIX_LENGTH ||
        kstrlen(frame) != length ||
        !protocol_fields_valid(frame, length) ||
        !protocol_frame_crc_valid(frame, length)) {
        if (protocol_session) protocol_emit_simple("BLOCKED", "ERR_INVALID");
        return 0;
    }
    if (!protocol_field(frame, "cmd", &command, &command_length) ||
        !protocol_field(frame, "seq", &value, &value_length) ||
        !protocol_parse_uint(value, value_length, &sequence)) {
        if (protocol_session) protocol_emit_simple("BLOCKED", "ERR_AGAIN");
        return 0;
    }
    if (protocol_session && sequence == protocol_host_sequence &&
        protocol_token_equals(command, command_length, "HELLO") &&
        protocol_field(frame, "run", &value, &value_length) &&
        protocol_token_equals(value, value_length, protocol_run)) {
        return 1;
    }
    if (sequence != protocol_host_sequence + 1U) {
        if (protocol_session) protocol_emit_simple("BLOCKED", "ERR_AGAIN");
        return 0;
    }
    protocol_host_sequence = sequence;
    if (protocol_token_equals(command, command_length, "HELLO")) {
        if (!protocol_field(frame, "run", &value, &value_length) ||
            !protocol_token_valid(value, value_length,
                                  TEST_PROTOCOL_RUN_CAPACITY)) {
            protocol_emit_simple("BLOCKED", "ERR_INVALID");
            return 0;
        }
        kmemset(protocol_run, 0U, sizeof(protocol_run));
        kmemcpy(protocol_run, value, value_length);
        protocol_session = 1U;
        protocol_ready_sent = 0U;
        return 1;
    }
    if (!protocol_session || !protocol_ready_sent) {
        protocol_emit_simple("BLOCKED", "ERR_AGAIN");
        return 0;
    }
    if (protocol_token_equals(command, command_length, "PING")) {
        protocol_emit_heartbeat(timer_get_ticks());
        return 1;
    }
    if (protocol_token_equals(command, command_length, "ABORT")) {
        protocol_emit_simple("BLOCKED", "ERR_CANCELLED");
        return 1;
    }
    if (protocol_token_equals(command, command_length, "RUN")) {
        const char* case_value;
        const char* iteration_value;
        const char* seed_value;
        uint32_t case_length;
        uint32_t iteration_length;
        uint32_t seed_length;
        uint32_t iteration;
        uint32_t seed;

        case_value = 0;
        if (protocol_busy ||
            !protocol_field(frame, "case", &case_value, &case_length) ||
            !protocol_field(frame, "iteration", &iteration_value,
                            &iteration_length) ||
            !protocol_field(frame, "seed", &seed_value, &seed_length) ||
            !protocol_parse_uint(iteration_value, iteration_length,
                                 &iteration) ||
            !protocol_parse_uint(seed_value, seed_length, &seed)) {
            if (case_value && protocol_token_valid(case_value, case_length,
                                                   TEST_PROTOCOL_CASE_CAPACITY)) {
                protocol_emit_case("BLOCKED", case_value, case_length,
                                   0U, 0U,
                                   protocol_busy ? "ERR_AGAIN" : "ERR_INVALID");
            } else {
                protocol_emit_simple("BLOCKED", protocol_busy ? "ERR_AGAIN" :
                                     "ERR_INVALID");
            }
            return 0;
        }
        protocol_busy = 1U;
        protocol_run_case(case_value, case_length, iteration, seed);
        protocol_busy = 0U;
        return 1;
    }
    protocol_emit_simple("BLOCKED", "ERR_INVALID");
    return 0;
}

static void protocol_receive(void) {
    uint8_t byte;
    uint32_t received = 0U;

    while (received++ < TEST_PROTOCOL_RX_BUDGET && serial_read_byte(&byte)) {
        if (byte == '\n') {
            if (!protocol_rx_discard && protocol_rx_length > 0U) {
                protocol_rx[protocol_rx_length] = '\0';
                protocol_handle_frame(protocol_rx, protocol_rx_length);
            }
            protocol_rx_length = 0U;
            protocol_rx_discard = 0U;
            continue;
        }
        if (byte == 0U || byte == '\r' || byte < 0x20U || byte > 0x7EU) {
            protocol_rx_discard = 1U;
            continue;
        }
        if (protocol_rx_length >= TEST_PROTOCOL_FRAME_CAPACITY - 1U) {
            protocol_rx_discard = 1U;
            continue;
        }
        if (!protocol_rx_discard) protocol_rx[protocol_rx_length++] = (char)byte;
    }
}

int test_protocol_init(void) {
    if (!serial_is_ready()) {
        LOG_ERROR("TEST", "Protocolo ZTEST sem canal serial");
        return ERR_UNAVAILABLE;
    }
    protocol_initialized = 1U;
    protocol_session = 0U;
    protocol_boot_ready = 0U;
    protocol_ready_sent = 0U;
    protocol_busy = 0U;
    protocol_rx_discard = 0U;
    protocol_host_sequence = 0U;
    protocol_guest_sequence = 0U;
    protocol_last_heartbeat = 0U;
    protocol_rx_length = 0U;
    kmemset(protocol_run, 0U, sizeof(protocol_run));
    kmemset(protocol_rx, 0U, sizeof(protocol_rx));
    return OK;
}

void test_protocol_set_boot_ready(void) {
    if (!protocol_initialized) return;
    protocol_boot_ready = 1U;
}

void test_protocol_poll(void) {
    uint32_t current_tick;

    if (!protocol_initialized) return;
    protocol_receive();
    if (protocol_session && protocol_boot_ready && !protocol_ready_sent) {
        protocol_emit_simple("READY", 0);
        protocol_ready_sent = 1U;
        protocol_last_heartbeat = timer_get_ticks();
    }
    if (!protocol_session || !protocol_ready_sent) {
        serial_flush(SERIAL_TX_FLUSH_BUDGET);
        return;
    }
    current_tick = timer_get_ticks();
    if (current_tick - protocol_last_heartbeat >=
        TEST_PROTOCOL_HEARTBEAT_TICKS) {
        protocol_emit_heartbeat(current_tick);
        protocol_last_heartbeat = current_tick;
    }
    serial_flush(SERIAL_TX_FLUSH_BUDGET);
}

void test_protocol_panic(const char* reason) {
    if (!protocol_initialized || !protocol_session) return;
    protocol_emit_simple("PANIC", protocol_safe_reason(reason, "ERR_STATE"));
    serial_flush(SERIAL_TX_CAPACITY);
}

void test_protocol_timeout(const char* reason) {
    if (!protocol_initialized || !protocol_session) return;
    protocol_emit_simple("TIMEOUT",
                         protocol_safe_reason(reason, "ERR_TIMEOUT"));
    serial_flush(SERIAL_TX_CAPACITY);
}

uint8_t test_protocol_is_active(void) {
    return protocol_session;
}
