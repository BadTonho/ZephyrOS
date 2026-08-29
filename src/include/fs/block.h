#ifndef BLOCK_H
#define BLOCK_H

#include "types.h"

#define BLOCK_MAX_DEVICES 20U
#define BLOCK_DEVICE_ID_SIZE 48U
#define BLOCK_DEVICE_MODEL_SIZE 48U
#define BLOCK_SECTOR_SIZE 512U
#define BLOCK_MAX_TRANSFER_SECTORS 255U
#define BLOCK_QUEUE_CAPACITY 32U
#define BLOCK_DISPATCH_BUDGET 8U

#define BLOCK_DEVICE_CAP_FLUSH 0x00000001U
#define BLOCK_DEVICE_CAP_FUA 0x00000002U
#define BLOCK_DEVICE_CAPABILITIES_SUPPORTED \
    (BLOCK_DEVICE_CAP_FLUSH | BLOCK_DEVICE_CAP_FUA)

#define BLOCK_BIO_FLAG_FUA 0x00000001U
#define BLOCK_BIO_FLAGS_SUPPORTED BLOCK_BIO_FLAG_FUA

typedef enum {
    BLOCK_PROVIDER_ATA = 0,
    BLOCK_PROVIDER_USB_MSC
} block_provider_t;

typedef enum {
    BLOCK_OPERATION_READ = 0,
    BLOCK_OPERATION_WRITE,
    BLOCK_OPERATION_FLUSH
} block_operation_t;

typedef enum {
    BLOCK_REQUEST_QUEUED = 0,
    BLOCK_REQUEST_IN_FLIGHT,
    BLOCK_REQUEST_COMPLETED,
    BLOCK_REQUEST_CANCELLED,
    BLOCK_REQUEST_ERROR
} block_request_state_t;

typedef int (*block_read_callback_t)(void* context, uint32_t lba,
                                     uint8_t count, uint8_t* buffer);
typedef int (*block_write_callback_t)(void* context, uint32_t lba,
                                      uint8_t count, const uint8_t* buffer);
typedef int (*block_flush_callback_t)(void* context);
typedef int (*block_write_flags_callback_t)(void* context, uint32_t lba,
                                             uint8_t count,
                                             const uint8_t* buffer,
                                             uint32_t flags);

typedef struct bio_request bio_request_t;
typedef struct block_request block_request_t;
typedef void (*block_completion_callback_t)(bio_request_t* request,
                                             void* context);
typedef int (*block_submit_callback_t)(block_request_t* request);

struct bio_request {
    const char* device_id;
    uint32_t lba;
    uint32_t sector_count;
    void* buffer;
    uint32_t buffer_bytes;
    block_operation_t operation;
    uint32_t flags;
    block_completion_callback_t completion;
    void* context;
    block_request_state_t state;
    uint32_t completed_sectors;
    int status;
};

struct block_request {
    const char* device_id;
    void* device_context;
    uint32_t lba;
    uint32_t sector_count;
    void* buffer;
    uint32_t buffer_bytes;
    block_operation_t operation;
    uint32_t flags;
    uint32_t completed_sectors;
    int status;
};

typedef struct {
    void* context;
    block_read_callback_t read;
    block_write_callback_t write;
    block_flush_callback_t flush;
    block_write_flags_callback_t write_flags;
    block_submit_callback_t submit;
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
    uint32_t max_transfer_sectors;
    uint32_t capabilities;
} block_device_t;

typedef struct {
    uint32_t queue_capacity;
    uint32_t queue_depth;
    uint32_t in_flight;
    uint32_t peak_depth;
    uint32_t submitted;
    uint32_t completed;
    uint32_t failed;
    uint32_t cancelled;
    uint32_t merged;
    uint32_t read_sectors;
    uint32_t write_sectors;
    uint32_t read_sectors_per_second;
    uint32_t write_sectors_per_second;
    int last_error;
} block_queue_stats_t;

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
int block_submit_sync(bio_request_t* request);
int block_submit(bio_request_t* request);
int block_dispatch(uint32_t budget, uint32_t* out_processed);
int block_cancel(bio_request_t* request);
int block_get_stats(block_queue_stats_t* out_stats);
int block_self_test(void);
int block_validate_state(void);

#endif
