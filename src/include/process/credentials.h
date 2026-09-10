#ifndef PROCESS_CREDENTIALS_H
#define PROCESS_CREDENTIALS_H

#include "types.h"

#define PROCESS_UID_ROOT 0U
#define PROCESS_GID_ROOT 0U
#define PROCESS_UID_USER 1000U
#define PROCESS_GID_USER 1000U

typedef enum {
    PROCESS_CAPABILITY_FILE_READ = 1U << 0,
    PROCESS_CAPABILITY_FILE_WRITE = 1U << 1,
    PROCESS_CAPABILITY_FILE_EXECUTE = 1U << 2,
    PROCESS_CAPABILITY_DEVICE_BASIC = 1U << 3,
    PROCESS_CAPABILITY_DEVICE_AUDIO = 1U << 4,
    PROCESS_CAPABILITY_DEVICE_BLOCK = 1U << 5,
    PROCESS_CAPABILITY_NETWORK = 1U << 6,
    PROCESS_CAPABILITY_POWER = 1U << 7,
    PROCESS_CAPABILITY_DIAGNOSTIC_READ = 1U << 8,
    PROCESS_CAPABILITY_DIAGNOSTIC_WRITE = 1U << 9,
    PROCESS_CAPABILITY_GLOBAL_SYNC = 1U << 10,
    PROCESS_CAPABILITY_PACKAGE_ADMIN = 1U << 11,
    PROCESS_CAPABILITY_IPC = 1U << 12
} process_capability_t;

#define PROCESS_CAPABILITIES_NATIVE 0xFFFFFFFFU
#define PROCESS_CAPABILITIES_USER \
    (PROCESS_CAPABILITY_FILE_READ | PROCESS_CAPABILITY_FILE_WRITE | \
     PROCESS_CAPABILITY_FILE_EXECUTE | PROCESS_CAPABILITY_DEVICE_BASIC | \
     PROCESS_CAPABILITY_DEVICE_AUDIO | PROCESS_CAPABILITY_DIAGNOSTIC_READ | \
     PROCESS_CAPABILITY_IPC)

typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint32_t capabilities;
} process_credentials_t;

struct process;

int process_credentials_init_native(process_credentials_t* credentials);
int process_credentials_init_user(process_credentials_t* credentials);
int process_credentials_copy(const struct process* process,
                             process_credentials_t* output);
int process_credentials_current(process_credentials_t* output);
int process_credentials_validate(const process_credentials_t* credentials);
int process_credentials_has(const process_credentials_t* credentials,
                            uint32_t capability);
const char* process_capability_name(uint32_t capability);

#endif
