#ifndef SERVICE_SUPERVISOR_H
#define SERVICE_SUPERVISOR_H

#include "types.h"
#include "core/recovery.h"
#include "process/process.h"

#define SERVICE_SUPERVISOR_NAME_LENGTH 24U
#define SERVICE_SUPERVISOR_ID_COUNT 4U

typedef enum {
    SERVICE_SUPERVISOR_KWORKER = 0,
    SERVICE_SUPERVISOR_SYSTEM,
    SERVICE_SUPERVISOR_SHELL,
    SERVICE_SUPERVISOR_DESKTOP
} service_supervisor_id_t;

typedef enum {
    SERVICE_SUPERVISOR_STARTING = 0,
    SERVICE_SUPERVISOR_READY,
    SERVICE_SUPERVISOR_FAILED,
    SERVICE_SUPERVISOR_STOPPED
} service_supervisor_state_t;

typedef process_t* (*service_supervisor_create_fn)(void);
typedef int (*service_supervisor_prepare_fn)(process_t* process);
typedef int (*service_supervisor_dependency_fn)(void);
typedef void (*service_supervisor_fallback_fn)(uint8_t active);

typedef struct {
    service_supervisor_id_t id;
    const char* name;
    recovery_component_id_t recovery_component;
    service_supervisor_create_fn create;
    service_supervisor_prepare_fn prepare;
    service_supervisor_dependency_fn dependency;
    service_supervisor_fallback_fn fallback;
} service_supervisor_definition_t;

typedef struct {
    service_supervisor_id_t id;
    char name[SERVICE_SUPERVISOR_NAME_LENGTH];
    service_supervisor_state_t state;
    uint32_t pid;
    uint32_t generation;
    uint32_t restart_attempts;
    uint32_t failures;
    int last_error;
    uint8_t fallback_active;
} service_supervisor_snapshot_t;

int service_supervisor_init(void);
int service_supervisor_configure(
    const service_supervisor_definition_t* definition);
int service_supervisor_start(void);
int service_supervisor_poll(void);
int service_supervisor_set_quiescing(uint8_t active);
int service_supervisor_get_identity(service_supervisor_id_t id,
                                    uint32_t* pid, uint32_t* generation);
int service_supervisor_snapshot_copy(
    service_supervisor_id_t id, service_supervisor_snapshot_t* output);
int service_supervisor_snapshot_list(service_supervisor_snapshot_t* output,
                                     uint32_t capacity, uint32_t* out_count);
int service_supervisor_validate_state(void);
int service_supervisor_is_initialized(void);
const char* service_supervisor_state_name(service_supervisor_state_t state);
int service_supervisor_test_fail_next(service_supervisor_id_t id);

#if defined(ZEPHYROS_HOST_TEST)
void service_supervisor_test_reset(void);
#endif

#endif
