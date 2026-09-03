#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "test_coverage.h"

#define HOST_SERIAL_CAPACITY 8192U

static char serial_output[HOST_SERIAL_CAPACITY];
static uint32_t serial_length;
static uint32_t serial_chunk;
static uint8_t serial_zero;
static uint32_t flush_count;

uint32_t serial_write_text(const char* text, uint32_t length) {
    uint32_t available;
    uint32_t accepted;

    if (!text || serial_zero || serial_length >= HOST_SERIAL_CAPACITY - 1U) {
        return 0U;
    }
    available = HOST_SERIAL_CAPACITY - 1U - serial_length;
    accepted = length;
    if (serial_chunk && accepted > serial_chunk) accepted = serial_chunk;
    if (accepted > available) accepted = available;
    for (uint32_t index = 0U; index < accepted; index++) {
        serial_output[serial_length++] = text[index];
    }
    serial_output[serial_length] = '\0';
    return accepted;
}

uint32_t serial_flush(uint32_t budget) {
    if (budget) flush_count++;
    return budget;
}

static void reset_serial(void) {
    serial_length = 0U;
    serial_output[0] = '\0';
    flush_count = 0U;
}

static int check_edge_paths(void) {
    static const char long_case[] =
        "host:core:test-coverage:case-name-that-is-longer-than-the-limit";

    reset_serial();
    serial_chunk = 2U;
    serial_zero = 1U;
    test_coverage_host_exercise();
    serial_zero = 0U;
    test_coverage_begin_case(NULL, 4U);
    test_coverage_end_case(OK);
    if (!flush_count || serial_length == 0U) return 1;

    reset_serial();
    serial_zero = 0U;
    test_coverage_begin_case(long_case, sizeof(long_case) - 1U);
    test_coverage_end_case(OK);
    if (!flush_count || !serial_output[0]) return 2;
    return 0;
}

static int check_report(void) {
    static const char case_id[] = "host:core:test-coverage";

    reset_serial();
    serial_chunk = 3U;
    serial_zero = 0U;
    test_coverage_begin_case(case_id, sizeof(case_id) - 1U);
    test_coverage_host_exercise();
    test_coverage_end_case(OK);
    if (flush_count < 2U || !serial_output[0]) return 1;
    if (!strstr(serial_output, "ZCOV_BEGIN|case=host:core:test-coverage|")) {
        return 2;
    }
    if (!strstr(serial_output, "ZCOV_DATA|case=host:core:test-coverage|")) {
        return 3;
    }
    if (!strstr(serial_output, "ZCOV_END|case=host:core:test-coverage|")) {
        return 4;
    }
    return 0;
}

int main(void) {
    int result = check_edge_paths();

    if (!result) result = check_report();
    if (result) return result;
    fputs(serial_output, stdout);
    return 0;
}
