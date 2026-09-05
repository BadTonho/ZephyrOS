#ifndef SETTINGS_TEST_H
#define SETTINGS_TEST_H

#ifdef ZEPHYROS_HOST_TEST
int settings_host_test_icon_editor(void);
int settings_host_test_contracts(void);
void settings_host_fixture_simple(void);
void settings_host_fixture_classic(void);
void settings_host_fixture_storage(void);
#endif

#endif
