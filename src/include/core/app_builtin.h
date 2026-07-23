#ifndef APP_BUILTIN_H
#define APP_BUILTIN_H

#include "types.h"
#include "core/app_api.h"

int app_builtin_run_echo(const app_launch_info_t* launch, uint32_t* pid_out);
int app_builtin_run_argtest(const app_launch_info_t* launch,
                            uint32_t* pid_out);

#endif
