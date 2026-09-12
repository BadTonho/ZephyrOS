#ifndef ZEPHYROS_DRIVER_LIFECYCLE_INTERNAL_H
#define ZEPHYROS_DRIVER_LIFECYCLE_INTERNAL_H

#include "types.h"

#define DRIVER_LIFECYCLE_MAX 64U
#define DRIVER_LIFECYCLE_ID_SIZE 32U
#define DRIVER_LIFECYCLE_PARENT_SIZE 32U
#define DRIVER_LIFECYCLE_BUS_SIZE 16U
#define DRIVER_LIFECYCLE_CLASS_SIZE 32U
#define DRIVER_LIFECYCLE_IDENTITY_SIZE 48U
#define DRIVER_LIFECYCLE_REASON_SIZE 64U

typedef enum {
    DRIVER_LIFECYCLE_UNREGISTERED = 0,
    DRIVER_LIFECYCLE_PROBING,
    DRIVER_LIFECYCLE_RESETTING,
    DRIVER_LIFECYCLE_CONFIGURING,
    DRIVER_LIFECYCLE_REGISTERED,
    DRIVER_LIFECYCLE_READY,
    DRIVER_LIFECYCLE_DEGRADED,
    DRIVER_LIFECYCLE_QUIESCING,
    DRIVER_LIFECYCLE_QUIESCED,
    DRIVER_LIFECYCLE_FAILED,
    DRIVER_LIFECYCLE_STOPPED
} driver_lifecycle_state_t;

#define DRIVER_LIFECYCLE_RESOURCE_IRQ      (1U << 0)
#define DRIVER_LIFECYCLE_RESOURCE_IO       (1U << 1)
#define DRIVER_LIFECYCLE_RESOURCE_MMIO     (1U << 2)
#define DRIVER_LIFECYCLE_RESOURCE_DMA      (1U << 3)
#define DRIVER_LIFECYCLE_RESOURCE_BUFFER   (1U << 4)
#define DRIVER_LIFECYCLE_RESOURCE_CALLBACK (1U << 5)
#define DRIVER_LIFECYCLE_RESOURCE_WORK     (1U << 6)

#define DRIVER_LIFECYCLE_SHARED_IRQ (1U << 0)

typedef struct {
    uint32_t irq;
    uint32_t io;
    uint32_t mmio;
    uint32_t dma;
    uint32_t buffer;
    uint32_t callback;
    uint32_t work;
} driver_lifecycle_resource_ids_t;

typedef struct {
    driver_lifecycle_state_t state;
    uint32_t generation;
    uint32_t required_resources;
    uint32_t owned_resources;
    driver_lifecycle_resource_ids_t resource_ids;
    int last_error;
    char reason[DRIVER_LIFECYCLE_REASON_SIZE];
} driver_lifecycle_snapshot_t;

void driver_lifecycle_init(void);
int driver_lifecycle_begin_probe(const char* driver_id,
                                 const char* parent_id,
                                 const char* bus,
                                 const char* class_name,
                                 const char* identity,
                                 uint32_t required_resources,
                                 const driver_lifecycle_resource_ids_t* resource_ids,
                                 uint32_t resource_flags,
                                 uint32_t* generation_out);
int driver_lifecycle_begin_reset(const char* driver_id, uint32_t generation);
int driver_lifecycle_begin_configure(const char* driver_id,
                                     uint32_t generation);
int driver_lifecycle_acquire(const char* driver_id, uint32_t generation,
                             uint32_t resources, uint32_t resource_flags);
int driver_lifecycle_release(const char* driver_id, uint32_t generation,
                             uint32_t resources);
int driver_lifecycle_mark_registered(const char* driver_id,
                                     uint32_t generation);
int driver_lifecycle_mark_ready(const char* driver_id, uint32_t generation);
int driver_lifecycle_mark_degraded(const char* driver_id, uint32_t generation,
                                   int error, const char* reason);
int driver_lifecycle_begin_quiesce(const char* driver_id,
                                   uint32_t generation);
int driver_lifecycle_mark_quiesced(const char* driver_id,
                                   uint32_t generation);
int driver_lifecycle_mark_failed(const char* driver_id, uint32_t generation,
                                 int error, const char* reason);
int driver_lifecycle_mark_stopped(const char* driver_id, uint32_t generation,
                                  int error, const char* reason);
int driver_lifecycle_get_state(const char* driver_id, uint32_t generation,
                               driver_lifecycle_state_t* state_out);
int driver_lifecycle_validate_ready(const char* driver_id,
                                     uint32_t generation);
int driver_lifecycle_validate_callback(const char* driver_id,
                                       uint32_t generation);
int driver_lifecycle_snapshot(const char* driver_id, uint32_t generation,
                              driver_lifecycle_snapshot_t* snapshot_out);
int driver_lifecycle_validate_state(void);
uint32_t driver_lifecycle_count(void);
int driver_lifecycle_publish(const char* driver_id,
                             const char* parent_id,
                             const char* bus,
                             const char* class_name,
                             const char* identity,
                             uint32_t required_resources,
                             const driver_lifecycle_resource_ids_t* resource_ids,
                             uint32_t resource_flags,
                             int init_result,
                             uint8_t optional,
                             const char* reason);

#endif
