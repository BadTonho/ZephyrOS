#ifndef TEST_COVERAGE_H
#define TEST_COVERAGE_H

#include "types.h"

void test_coverage_begin_case(const char* case_id, uint32_t case_length);
void test_coverage_end_case(int result);

#endif
