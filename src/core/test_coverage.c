#include "test_coverage.h"

#include "drivers/serial.h"

#define TEST_COVERAGE_ADDRESS_CAPACITY 8192U
#define TEST_COVERAGE_HASH_CAPACITY 16384U
#define TEST_COVERAGE_CASE_CAPACITY 80U
#define TEST_COVERAGE_LINE_CAPACITY 512U
#define TEST_COVERAGE_DATA_PER_LINE 32U
#define TEST_COVERAGE_SERIAL_FLUSH_BUDGET 128U

#if defined(ZEPHYROS_HOST_TEST)
typedef uint64_t test_coverage_address_t;
#define TEST_COVERAGE_HEX_DIGITS 16U
#else
typedef uint32_t test_coverage_address_t;
#define TEST_COVERAGE_HEX_DIGITS 8U
#endif

#if defined(__GNUC__)
#define TEST_COVERAGE_NO_INSTRUMENT \
    __attribute__((no_instrument_function))
#else
#define TEST_COVERAGE_NO_INSTRUMENT
#endif

static test_coverage_address_t coverage_addresses[
    TEST_COVERAGE_ADDRESS_CAPACITY];
static test_coverage_address_t coverage_hash[TEST_COVERAGE_HASH_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char coverage_case[TEST_COVERAGE_CASE_CAPACITY];

static uint32_t TEST_COVERAGE_NO_INSTRUMENT
coverage_append(char* output, uint32_t offset, uint32_t capacity,
                const char* text) {
    uint32_t index = 0U;

    if (!output || !text || offset >= capacity) return offset;
    while (text[index] && offset + 1U < capacity) {
        output[offset++] = text[index++];
    }
    output[offset] = '\0';
    return offset;
}

static uint32_t TEST_COVERAGE_NO_INSTRUMENT
coverage_append_hex(char* output, uint32_t offset, uint32_t capacity,
                    test_coverage_address_t value) {
    static const char digits[] = "0123456789ABCDEF";
    test_coverage_address_t shift;

    if (!output || offset + TEST_COVERAGE_HEX_DIGITS + 1U >= capacity) {
        return offset;
    }
    output[offset++] = '0';
    output[offset++] = 'x';
    shift = (TEST_COVERAGE_HEX_DIGITS - 1U) * 4U;
    for (;; shift -= 4U) {
        output[offset++] = digits[(value >> shift) & 0x0FU];
        if (shift == 0U) break;
    }
    output[offset] = '\0';
    return offset;
}

static void TEST_COVERAGE_NO_INSTRUMENT coverage_emit_line(
    const char* line, uint32_t length) {
    uint32_t offset = 0U;

    while (offset < length) {
        uint32_t written = serial_write_text(line + offset, length - offset);
        if (!written) break;
        offset += written;
    }
    serial_flush(TEST_COVERAGE_SERIAL_FLUSH_BUDGET);
}

static void TEST_COVERAGE_NO_INSTRUMENT coverage_emit_header(
    const char* name, uint32_t value) {
    char line[TEST_COVERAGE_LINE_CAPACITY];
    uint32_t length = 0U;

    line[0] = '\0';
    length = coverage_append(line, length, sizeof(line), name);
    length = coverage_append(line, length, sizeof(line), "|case=");
    length = coverage_append(line, length, sizeof(line), coverage_case);
    length = coverage_append(line, length, sizeof(line), "|value=");
    length = coverage_append_hex(line, length, sizeof(line), value);
    length = coverage_append(line, length, sizeof(line), "\n");
    coverage_emit_line(line, length);
}

static void TEST_COVERAGE_NO_INSTRUMENT coverage_emit_addresses(void) {
    char line[TEST_COVERAGE_LINE_CAPACITY];
    uint32_t index = 0U;

    while (index < coverage_count) {
        uint32_t line_count = 0U;
        uint32_t length = 0U;

        line[0] = '\0';
        length = coverage_append(line, length, sizeof(line),
                                 "ZCOV_DATA|case=");
        length = coverage_append(line, length, sizeof(line), coverage_case);
        length = coverage_append(line, length, sizeof(line), "|addresses=");
        while (index < coverage_count &&
               line_count++ < TEST_COVERAGE_DATA_PER_LINE) {
            if (line_count > 1U) {
                length = coverage_append(line, length, sizeof(line), ",");
            }
            length = coverage_append_hex(line, length, sizeof(line),
                                         coverage_addresses[index++]);
        }
        length = coverage_append(line, length, sizeof(line), "\n");
        coverage_emit_line(line, length);
    }
}

#if defined(ZEPHYROS_HOST_TEST)
static uint32_t TEST_COVERAGE_NO_INSTRUMENT coverage_hash_slot(
    test_coverage_address_t address);
static void TEST_COVERAGE_NO_INSTRUMENT coverage_record(
    test_coverage_address_t address);
void TEST_COVERAGE_NO_INSTRUMENT __cyg_profile_func_enter(
    void* function, void* caller);
void TEST_COVERAGE_NO_INSTRUMENT __cyg_profile_func_exit(
    void* function, void* caller);
void TEST_COVERAGE_NO_INSTRUMENT test_coverage_begin_case(
    const char* case_id, uint32_t case_length);
void TEST_COVERAGE_NO_INSTRUMENT test_coverage_end_case(int result);

void test_coverage_host_exercise(void) {
    char buffer[TEST_COVERAGE_LINE_CAPACITY];
    uint32_t offset = 0U;

    if (!coverage_active) coverage_emit_addresses();
    buffer[0] = '\0';
    offset = coverage_append(buffer, offset, sizeof(buffer), "HOST");
    offset = coverage_append_hex(buffer, offset, sizeof(buffer), 0x1234U);
    coverage_hash_slot(0x1234U);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_append);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_append_hex);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_emit_addresses);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_emit_header);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_emit_line);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_hash_slot);
    coverage_record((test_coverage_address_t)(unsigned long long)&coverage_record);
    coverage_record((test_coverage_address_t)(unsigned long long)&__cyg_profile_func_enter);
    coverage_record((test_coverage_address_t)(unsigned long long)&__cyg_profile_func_exit);
    coverage_record((test_coverage_address_t)(unsigned long long)&test_coverage_begin_case);
    coverage_record((test_coverage_address_t)(unsigned long long)&test_coverage_end_case);
    coverage_emit_line(buffer, offset);
    coverage_emit_header("HOST_HEADER", offset);
}
#endif

static uint32_t TEST_COVERAGE_NO_INSTRUMENT coverage_hash_slot(
    test_coverage_address_t address) {
    return (uint32_t)(address * 2654435761U) &
           (TEST_COVERAGE_HASH_CAPACITY - 1U);
}

static void TEST_COVERAGE_NO_INSTRUMENT coverage_record(
    test_coverage_address_t address) {
    uint32_t slot;

    if (!coverage_active || !address) return;
    slot = coverage_hash_slot(address);
    for (uint32_t probe = 0U; probe < TEST_COVERAGE_HASH_CAPACITY; probe++) {
        if (!coverage_hash[slot]) {
            if (coverage_count >= TEST_COVERAGE_ADDRESS_CAPACITY) return;
            coverage_hash[slot] = address;
            coverage_addresses[coverage_count++] = address;
            return;
        }
        if (coverage_hash[slot] == address) return;
        slot = (slot + 1U) & (TEST_COVERAGE_HASH_CAPACITY - 1U);
    }
}

void TEST_COVERAGE_NO_INSTRUMENT __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
#if defined(ZEPHYROS_HOST_TEST)
    coverage_record((test_coverage_address_t)(unsigned long long)function);
#else
    coverage_record((test_coverage_address_t)(uint32_t)function);
#endif
}

void TEST_COVERAGE_NO_INSTRUMENT __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

void TEST_COVERAGE_NO_INSTRUMENT test_coverage_record_address(
    uint32_t address) {
#if defined(ZEPHYROS_HOST_TEST)
    coverage_record((test_coverage_address_t)(unsigned long long)
                    &test_coverage_record_address);
#else
    coverage_record((test_coverage_address_t)(uint32_t)
                    &test_coverage_record_address);
#endif
    coverage_record((test_coverage_address_t)address);
}

void TEST_COVERAGE_NO_INSTRUMENT test_coverage_begin_case(
    const char* case_id, uint32_t case_length) {
    uint32_t length = case_length;

    coverage_count = 0U;
    coverage_active = 0U;
    for (uint32_t index = 0U; index < TEST_COVERAGE_HASH_CAPACITY; index++) {
        coverage_hash[index] = 0U;
    }
    if (!case_id) length = 0U;
    if (length >= TEST_COVERAGE_CASE_CAPACITY) {
        length = TEST_COVERAGE_CASE_CAPACITY - 1U;
    }
    for (uint32_t index = 0U; index < length; index++) {
        coverage_case[index] = case_id[index];
    }
    coverage_case[length] = '\0';
    coverage_active = 1U;
}

void TEST_COVERAGE_NO_INSTRUMENT test_coverage_end_case(int result) {
    coverage_active = 0U;
    coverage_emit_header("ZCOV_BEGIN", coverage_count);
    coverage_emit_addresses();
    coverage_emit_header("ZCOV_END", (uint32_t)result);
}
