#ifndef SHELL_KMETRICS_H
#define SHELL_KMETRICS_H

#include "types.h"
#include "apps/shell_job.h"
#include "core/ethernet.h"
#include "core/input.h"
#include "core/irq_deferred.h"
#include "core/keyboard.h"
#include "core/network_manager.h"
#include "core/recovery.h"
#include "core/service_supervisor.h"
#include "core/update.h"
#include "core/update_system_slots.h"
#include "core/workqueue.h"
#include "drivers/idt.h"
#include "drivers/mouse.h"
#include "drivers/vesa.h"
#include "fs/block.h"
#include "fs/block_cache.h"
#include "fs/permissions.h"
#include "fs/vfs.h"
#include "core/memory.h"
#include "memory/paging.h"
#include "process/credentials.h"
#include "process/process.h"
#include "process/resource.h"

#define SHELL_KMETRICS_DOMAIN_INPUT (1U << 0)
#define SHELL_KMETRICS_DOMAIN_WORKQUEUE (1U << 1)
#define SHELL_KMETRICS_DOMAIN_JOB (1U << 2)
#define SHELL_KMETRICS_DOMAIN_MEMORY (1U << 3)
#define SHELL_KMETRICS_DOMAIN_VFS (1U << 4)
#define SHELL_KMETRICS_DOMAIN_BLOCK (1U << 5)
#define SHELL_KMETRICS_DOMAIN_CACHE (1U << 6)
#define SHELL_KMETRICS_DOMAIN_NETWORK (1U << 7)
#define SHELL_KMETRICS_DOMAIN_VIDEO (1U << 8)
#define SHELL_KMETRICS_DOMAIN_PROCESS (1U << 9)
#define SHELL_KMETRICS_DOMAIN_SERVICES (1U << 10)
#define SHELL_KMETRICS_DOMAIN_SECURITY (1U << 11)
#define SHELL_KMETRICS_DOMAIN_UPDATE (1U << 12)
#define SHELL_KMETRICS_DOMAIN_RECOVERY (1U << 13)

typedef struct {
    recovery_state_t state;
    uint32_t failures;
    int last_error;
} shell_kmetrics_recovery_snapshot_t;

typedef struct {
    uint32_t ticks;
    uint32_t frequency;
    uint32_t capture_ticks;
    uint32_t valid_domains;
    uint32_t state_counts[5];
    uint32_t process_count;
    uint32_t user_process_count;
    keyboard_metrics_t keyboard;
    keyboard_flow_metrics_t keyboard_flow;
    input_metrics_t input;
    input_flow_metrics_t input_flow;
    mouse_flow_metrics_t mouse_flow;
    mouse_status_t mouse_status;
    ipc_stats_t ipc;
    scheduler_stats_t scheduler;
    idt_irq_status_t irq[IDT_IRQ_LINE_COUNT];
    uint8_t irq_valid[IDT_IRQ_LINE_COUNT];
    irq_deferred_status_t deferred;
    irq_deferred_irq_status_t deferred_irq[IRQ_DEFERRED_IRQ_COUNT];
    uint8_t deferred_irq_valid[IRQ_DEFERRED_IRQ_COUNT];
    workqueue_stats_t workqueue;
    shell_job_status_t job;
    vesa_metrics_t vesa;
    uint8_t vesa_available;
    uint8_t vesa_backbuffer;
    memory_heap_stats_t heap;
    memory_pmm_stats_t pmm;
    paging_user_stats_t paging_user;
    paging_boot_stats_t paging_boot;
    int paging_boot_result;
    vfs_status_t vfs;
    block_queue_stats_t block;
    block_cache_stats_t cache;
    block_durability_status_t durability;
    ethernet_status_t ethernet;
    network_manager_status_t network;
    service_supervisor_snapshot_t services[SERVICE_SUPERVISOR_ID_COUNT];
    uint32_t service_count;
    process_resource_validation_t resource_validation;
    process_resource_snapshot_t current_resource;
    uint32_t current_resource_valid;
    process_stack_validation_t stack_validation;
    process_credentials_t credentials;
    uint32_t credentials_valid;
    int permissions_result;
    update_capabilities_t update_capabilities;
    update_status_t update_status;
    update_system_slots_status_t update_slots;
    shell_kmetrics_recovery_snapshot_t recovery[RECOVERY_COMPONENT_COUNT];
    uint8_t recovery_valid[RECOVERY_COMPONENT_COUNT];
    uint32_t recovery_count;
    int input_result;
    int deferred_result;
    int input_flow_result;
    int keyboard_flow_result;
    int mouse_flow_result;
    int mouse_status_result;
    int workqueue_result;
    int job_result;
    int vfs_result;
    int block_result;
    int cache_result;
    int durability_result;
    int ethernet_result;
    int network_result;
    int service_result;
    int resource_result;
    int stack_result;
    int update_capabilities_result;
    int update_status_result;
    int update_slots_result;
} shell_kmetrics_snapshot_t;

typedef struct {
    shell_kmetrics_snapshot_t snapshot;
    uint8_t valid;
} shell_kmetrics_baseline_t;

int shell_kmetrics_take_snapshot(shell_kmetrics_snapshot_t* snapshot);
int shell_kmetrics_emit_machine(
    const shell_kmetrics_snapshot_t* current,
    const shell_kmetrics_snapshot_t* baseline,
    uint8_t baseline_valid);

#endif
