#ifndef POWER_NOTIFIER_H
#define POWER_NOTIFIER_H

#include "types.h"

#define POWER_NOTIFIER_CAPACITY 6U
#define POWER_NOTIFIER_NAME_SIZE 24U

typedef int (*power_notifier_callback_t)(uint32_t deadline_tick);

int power_notifier_init(void);
int power_notifier_register(const char* name,
                            power_notifier_callback_t notification,
                            power_notifier_callback_t quiescence,
                            uint8_t optional);
int power_notifier_finalize(void);
int power_notifier_notify(uint32_t deadline_tick, uint32_t* out_completed,
                          uint32_t* out_optional_failures);
int power_notifier_quiesce(uint32_t deadline_tick, uint32_t* out_completed,
                           uint32_t* out_optional_failures);
int power_notifier_get_count(uint32_t* out_count);
int power_notifier_validate_state(void);

#endif
