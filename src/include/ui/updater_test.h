#ifndef UPDATER_TEST_H
#define UPDATER_TEST_H

#include "types.h"

#ifdef ZEPHYROS_HOST_TEST
int updater_host_test_contracts(void);
void updater_host_fixture_set_message(int type, uint32_t data1);
void updater_host_fixture_clear_message(void);
void updater_host_fixture_set_cached_path(int result, const char* path);
void updater_host_fixture_set_slots(int result, uint8_t pending_slot,
                                    uint32_t sequence);
void updater_host_fixture_set_keyboard_ascii(char value);
#endif

#endif
