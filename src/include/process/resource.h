#ifndef PROCESS_RESOURCE_H
#define PROCESS_RESOURCE_H

#include "types.h"
#include "core/app_api.h"
#include "memory/paging.h"

struct process;

#define PROCESS_RESOURCE_MAX_RESIDENT_PAGES 128U
#define PROCESS_RESOURCE_MAX_ANONYMOUS_BYTES (1024U * 1024U)
#define PROCESS_RESOURCE_MAX_DYNAMIC_VMAS 16U
#define PROCESS_RESOURCE_MAX_ARGUMENTS APP_LAUNCH_MAX_ARGS
#define PROCESS_RESOURCE_MAX_ARGUMENT_BYTES APP_LAUNCH_MAX_RAW_LENGTH
#define PROCESS_RESOURCE_USER_STACK_BYTES (USER_STACK_TOP - USER_STACK_BASE)

typedef enum {
    PROCESS_RESOURCE_FAILURE_NONE = 0,
    PROCESS_RESOURCE_FAILURE_ANONYMOUS_BYTES,
    PROCESS_RESOURCE_FAILURE_DYNAMIC_VMAS,
    PROCESS_RESOURCE_FAILURE_RESIDENT_PAGES,
    PROCESS_RESOURCE_FAILURE_ARGUMENTS,
    PROCESS_RESOURCE_FAILURE_STACK,
    PROCESS_RESOURCE_FAILURE_IMAGE,
    PROCESS_RESOURCE_FAILURE_OOM
} process_resource_failure_t;

typedef struct {
    uint32_t pid;
    uint32_t generation;
    uint32_t limits_active;
    uint32_t resident_pages;
    uint32_t resident_peak_pages;
    uint32_t resident_limit_pages;
    uint32_t anonymous_bytes;
    uint32_t anonymous_peak_bytes;
    uint32_t anonymous_limit_bytes;
    uint32_t dynamic_vmas;
    uint32_t dynamic_vma_limit;
    uint32_t argument_count;
    uint32_t argument_count_limit;
    uint32_t argument_bytes;
    uint32_t argument_bytes_limit;
    uint32_t image_bytes;
    uint32_t user_stack_bytes;
    uint32_t kernel_stack_bytes;
    uint32_t allocation_failures;
    process_resource_failure_t last_failure;
    uint32_t last_error;
    uint32_t last_requested;
} process_resource_snapshot_t;

typedef struct {
    uint32_t checked;
    uint32_t valid;
    uint32_t invalid;
} process_resource_validation_t;

int process_resource_init(void);
int process_resource_attach(struct process* process);
void process_resource_detach(const struct process* process);
int process_resource_check_vma(struct process* process, uint32_t length);
int process_resource_check_vma_split(struct process* process);
int process_resource_check_page(struct process* process);
void process_resource_note_vma_success(struct process* process);
void process_resource_note_page_success(struct process* process);
void process_resource_record_failure(struct process* process,
                                     process_resource_failure_t failure,
                                     uint32_t error, uint32_t requested);
int process_resource_snapshot_copy(uint32_t pid, uint32_t generation,
                                   process_resource_snapshot_t* output);
int process_resource_validate_all(process_resource_validation_t* validation);

#endif
