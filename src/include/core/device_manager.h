#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "types.h"

#define DEVICE_MANAGER_MAX_DEVICES 72U
#define DEVICE_ID_SIZE 16U
#define DEVICE_NAME_SIZE 32U
#define DEVICE_TYPE_SIZE 16U
#define DEVICE_LOCATION_SIZE 24U
#define DEVICE_DETAIL_SIZE 48U
#define DEVICE_IRQ_UNKNOWN 0xFFU

typedef enum {
    DEVICE_STATUS_READY = 0,
    DEVICE_STATUS_DEGRADED,
    DEVICE_STATUS_DISABLED,
    DEVICE_STATUS_UNKNOWN
} device_status_t;

typedef struct {
    char id[DEVICE_ID_SIZE];
    char name[DEVICE_NAME_SIZE];
    char type[DEVICE_TYPE_SIZE];
    char location[DEVICE_LOCATION_SIZE];
    char detail[DEVICE_DETAIL_SIZE];
    device_status_t status;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uint32_t capacity_sectors;
} device_info_t;

int device_manager_init(void);
int device_manager_refresh(void);
int device_manager_get_count(uint32_t* out_count);
int device_manager_get_info(uint32_t index, device_info_t* out_info);
int device_manager_find(const char* id, device_info_t* out_info);
const char* device_manager_status_name(device_status_t status);

#endif
