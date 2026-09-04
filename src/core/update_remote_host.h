#ifndef UPDATE_REMOTE_HOST_H
#define UPDATE_REMOTE_HOST_H

#include "types.h"

int update_remote_host_test_contracts(void);
void update_remote_host_set_fs_type(uint8_t type);
void update_remote_host_set_update_ready(uint8_t ready);
void update_remote_host_set_network_ready(uint8_t ready);

#endif
