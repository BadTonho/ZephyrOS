#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

#define BLOCK_MAX_DEVICES 20U
#define BLOCK_DEVICE_ID_SIZE 48U
#define BLOCK_DEVICE_MODEL_SIZE 48U
#define BLOCK_SECTOR_SIZE 512U

typedef enum {
    BLOCK_PROVIDER_ATA = 0,
    BLOCK_PROVIDER_USB_MSC
} block_provider_t;

typedef int (*block_read_callback_t)(void* context, uint32_t lba,
                                     uint8_t count, uint8_t* buffer);
typedef int (*block_write_callback_t)(void* context, uint32_t lba,
                                      uint8_t count, const uint8_t* buffer);

typedef struct {
    void* context;
    block_read_callback_t read;
    block_write_callback_t write;
} block_ops_t;

typedef struct {
    char id[BLOCK_DEVICE_ID_SIZE];
    char model[BLOCK_DEVICE_MODEL_SIZE];
    block_provider_t provider;
    uint32_t sector_count;
    uint16_t sector_size;
    uint8_t read_only;
    uint8_t online;
    uint32_t read_ops;
    uint32_t write_ops;
    int last_error;
    block_ops_t ops;
} block_device_t;

int block_init(void);
int block_register(const block_device_t* descriptor);
int block_unregister(const char* id);
int block_get_count(uint32_t* out_count);
int block_get_at(uint32_t index, block_device_t* out_device);
int block_find(const char* id, block_device_t* out_device);
int block_read(const char* id, uint32_t lba, uint8_t count,
               uint8_t* buffer);
int block_write(const char* id, uint32_t lba, uint8_t count,
                const uint8_t* buffer);
int block_validate_state(void);

#endif
