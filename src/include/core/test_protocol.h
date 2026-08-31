#ifndef TEST_PROTOCOL_H
#define TEST_PROTOCOL_H

#include "types.h"

#define TEST_PROTOCOL_VERSION 1U
#define TEST_PROTOCOL_FRAME_CAPACITY 512U
#define TEST_PROTOCOL_RUN_CAPACITY 48U
#define TEST_PROTOCOL_CASE_CAPACITY 64U
#define TEST_PROTOCOL_HEARTBEAT_TICKS 25U

int test_protocol_init(void);
void test_protocol_poll(void);
void test_protocol_set_boot_ready(void);
void test_protocol_panic(const char* reason);
void test_protocol_timeout(const char* reason);
uint8_t test_protocol_is_active(void);

#endif
