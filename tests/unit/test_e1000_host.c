#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/e1000.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "memory/paging.h"

void e1000_host_inject_rx(void* context, const uint8_t* data,
                          uint16_t length, uint8_t status, uint8_t errors);
void e1000_host_reset_devices(void);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_MMIO_WORDS (0x6000U / sizeof(uint32_t))
#define HOST_RX_BUFFER_BYTES (8U * 2048U)
#define HOST_TX_BUFFER_BYTES (8U * 2048U)
#define HOST_IRQ_LINE 11U
#define HOST_FRAME_LENGTH 60U
#define HOST_E1000_VENDOR 0x8086U
#define HOST_E1000_DEVICE 0x100EU
#define HOST_E1000_CLASS 0x02U
#define HOST_E1000_BAR 0x10000000U
#define HOST_E1000_STATUS 0x0008U
#define HOST_E1000_ICR 0x00C0U
#define HOST_E1000_RAL0 0x5400U
#define HOST_E1000_RAH0 0x5404U
#define HOST_E1000_CTRL 0x0000U
#define HOST_E1000_TDT 0x3818U
#define HOST_E1000_RESET (1U << 26)
#define HOST_E1000_LINK (1U << 1)
#define HOST_E1000_RXT0 (1U << 7)
#define HOST_E1000_LSC (1U << 2)
#define HOST_E1000_RXO (1U << 6)
#define HOST_E1000_RXSEQ (1U << 3)
#define HOST_E1000_RX_DD 0x01U
#define HOST_E1000_RX_EOP 0x02U
#define HOST_E1000_TX_DD 0x01U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static volatile uint32_t host_mmio[HOST_MMIO_WORDS];
static uint8_t host_rx_buffers[HOST_RX_BUFFER_BYTES];
static uint8_t host_tx_buffers[HOST_TX_BUFFER_BYTES];
static uint8_t host_rx_ring[4096U];
static uint8_t host_tx_ring[4096U];
static uint32_t host_ticks;
static uint32_t host_map_calls;
static uint32_t host_pmm_page_calls;
static uint32_t host_pmm_pages_calls;
static uint32_t host_pmm_page_frees;
static uint32_t host_pmm_pages_frees;
static uint32_t host_irq_dispatches;
static uint32_t host_irq_schedules;
static uint32_t host_map_failure;
static uint32_t host_pmm_failure;
static int host_pci_result;
static int host_deferred_result;
static int host_irq_result;
static uint8_t host_reset_stuck;
static uint8_t host_tx_complete;
static uint8_t host_pmm_page_used[2];
static uint8_t host_pmm_buffer_used[2];
static irq_deferred_work_t* host_deferred_queue[4];
static uint32_t host_deferred_queue_count;
static isr_handler_t host_irq_handler;
static uint8_t host_irq_line;
static uint32_t host_log_calls;

static void __attribute__((no_instrument_function)) coverage_record(
    void* function) {
    uintptr_t address = (uintptr_t)function;

    if (!coverage_active || !address) return;
    for (uint32_t index = 0U; index < coverage_count; index++) {
        if (coverage_addresses[index] == address) return;
    }
    if (coverage_count < HOST_COVERAGE_CAPACITY) {
        coverage_addresses[coverage_count++] = address;
    }
}

void __attribute__((no_instrument_function)) __cyg_profile_func_enter(
    void* function, void* caller) {
    (void)caller;
    coverage_record(function);
}

void __attribute__((no_instrument_function)) __cyg_profile_func_exit(
    void* function, void* caller) {
    (void)function;
    (void)caller;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:drivers:e1000|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:e1000|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:e1000|value=0x%08X\n",
           (uint32_t)result);
}

void kmemset(void* destination, uint8_t value, uint32_t size) {
    uint8_t* bytes = (uint8_t*)destination;

    if (!bytes) return;
    for (uint32_t index = 0U; index < size; index++) bytes[index] = value;
}

void kmemcpy(void* destination, const void* source, uint32_t size) {
    uint8_t* target = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;

    if (!target || !input) return;
    for (uint32_t index = 0U; index < size; index++) target[index] = input[index];
}

int kstrcmp(const char* first, const char* second) {
    if (!first || !second) return first == second ? 0 : 1;
    while (*first && *first == *second) {
        first++;
        second++;
    }
    return (unsigned char)*first - (unsigned char)*second;
}

uint32_t kstrlen(const char* text) {
    uint32_t length = 0U;

    if (!text) return 0U;
    while (text[length]) length++;
    return length;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    host_log_calls++;
}

void log_print_code(log_level_t level, const char* module, int32_t error,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error;
    (void)message;
    host_log_calls++;
}

uint32_t timer_get_ticks(void) {
    return host_ticks++;
}

int paging_map_page(uint32_t virtual_address, uint32_t physical_address,
                    uint32_t flags) {
    (void)virtual_address;
    (void)physical_address;
    (void)flags;
    host_map_calls++;
    return host_map_failure ? ERR_MEM : OK;
}

int pci_enable_memory_and_bus_mastering(const pci_device_t* device) {
    if (!device) return ERR_NULL;
    return host_pci_result;
}

int idt_register_shared_irq_handler(uint8_t irq_line, isr_handler_t handler) {
    if (host_irq_result != OK) return host_irq_result;
    host_irq_line = irq_line;
    host_irq_handler = handler;
    return OK;
}

int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line, irq_deferred_callback_t callback,
                           void* context) {
    if (!work || !owner || !callback) return ERR_NULL;
    if (host_deferred_result != OK) return host_deferred_result;
    kmemset(work, 0, sizeof(*work));
    work->callback = callback;
    work->context = context;
    work->irq_line = irq_line;
    work->initialized = 1U;
    while (*owner && work->owner[host_irq_dispatches] == '\0' &&
           host_irq_dispatches < IRQ_DEFERRED_OWNER_SIZE - 1U) {
        work->owner[host_irq_dispatches] = *owner++;
        host_irq_dispatches++;
    }
    host_irq_dispatches = 0U;
    return OK;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    if (!work || !work->initialized) return ERR_STATE;
    if (work->queued) return OK;
    if (host_deferred_queue_count >= 4U) return ERR_OVERFLOW;
    work->queued = 1U;
    work->scheduled++;
    host_deferred_queue[host_deferred_queue_count++] = work;
    host_irq_schedules++;
    return OK;
}

int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!out_processed) return ERR_NULL;
    *out_processed = 0U;
    while (processed < budget && host_deferred_queue_count) {
        irq_deferred_work_t* work = host_deferred_queue[0];

        for (uint32_t index = 1U; index < host_deferred_queue_count; index++) {
            host_deferred_queue[index - 1U] = host_deferred_queue[index];
        }
        host_deferred_queue_count--;
        work->queued = 0U;
        work->dispatched++;
        if (work->callback) work->callback(work->context);
        processed++;
        host_irq_dispatches++;
    }
    *out_processed = processed;
    return OK;
}

volatile uint32_t* e1000_host_map_mmio(uint32_t base) {
    (void)base;
    return host_map_failure == 2U ? 0 : host_mmio;
}

uint32_t e1000_host_read_mmio(volatile uint32_t* mmio, uint32_t offset) {
    uint32_t index = offset / sizeof(uint32_t);

    if (!mmio || index >= HOST_MMIO_WORDS) return 0U;
    if (offset == HOST_E1000_CTRL && !host_reset_stuck &&
        (mmio[index] & HOST_E1000_RESET)) {
        mmio[index] &= ~HOST_E1000_RESET;
    }
    return mmio[index];
}

void e1000_host_write_mmio(volatile uint32_t* mmio, uint32_t offset,
                           uint32_t value) {
    uint32_t index = offset / sizeof(uint32_t);

    if (!mmio || index >= HOST_MMIO_WORDS) return;
    mmio[index] = value;
    if (offset == HOST_E1000_TDT && host_tx_complete) return;
}

uint8_t e1000_host_tx_complete(void* device, uint8_t descriptor) {
    (void)device;
    (void)descriptor;
    return host_tx_complete;
}

void* pmm_alloc_page_in_zone(memory_zone_t zone) {
    if (zone != MEMORY_ZONE_BUFFER) return 0;
    if (host_pmm_failure == 1U && host_pmm_page_calls == 0U) {
        host_pmm_page_calls++;
        return 0;
    }
    if (!host_pmm_page_used[0]) {
        host_pmm_page_used[0] = 1U;
        host_pmm_page_calls++;
        return host_rx_ring;
    }
    if (!host_pmm_page_used[1]) {
        host_pmm_page_used[1] = 1U;
        host_pmm_page_calls++;
        return host_tx_ring;
    }
    return 0;
}

void* pmm_alloc_pages_in_zone(uint32_t count, memory_zone_t zone) {
    if (zone != MEMORY_ZONE_BUFFER || count != 4U) return 0;
    if (host_pmm_failure == 3U && host_pmm_pages_calls == 0U) return 0;
    if (host_pmm_failure == 4U && host_pmm_pages_calls == 1U) return 0;
    if (!host_pmm_buffer_used[0]) {
        host_pmm_buffer_used[0] = 1U;
        host_pmm_pages_calls++;
        return host_rx_buffers;
    }
    if (!host_pmm_buffer_used[1]) {
        host_pmm_buffer_used[1] = 1U;
        host_pmm_pages_calls++;
        return host_tx_buffers;
    }
    return 0;
}

void pmm_free_page(void* address) {
    if (address == host_rx_ring) host_pmm_page_used[0] = 0U;
    if (address == host_tx_ring) host_pmm_page_used[1] = 0U;
    host_pmm_page_frees++;
}

void pmm_free_pages(void* address, uint32_t count) {
    if (count != 4U) return;
    if (address == host_rx_buffers) host_pmm_buffer_used[0] = 0U;
    if (address == host_tx_buffers) host_pmm_buffer_used[1] = 0U;
    host_pmm_pages_frees++;
}

static void reset_fixture(void) {
    kmemset((void*)host_mmio, 0, sizeof(host_mmio));
    kmemset(host_rx_buffers, 0, sizeof(host_rx_buffers));
    kmemset(host_tx_buffers, 0, sizeof(host_tx_buffers));
    kmemset(host_rx_ring, 0, sizeof(host_rx_ring));
    kmemset(host_tx_ring, 0, sizeof(host_tx_ring));
    host_mmio[HOST_E1000_STATUS / 4U] = HOST_E1000_LINK;
    host_mmio[HOST_E1000_RAL0 / 4U] = 0x56341210U;
    host_mmio[HOST_E1000_RAH0 / 4U] = 0x0000BC9AU;
    host_ticks = 0U;
    host_map_calls = 0U;
    host_pmm_page_calls = 0U;
    host_pmm_pages_calls = 0U;
    host_pmm_page_frees = 0U;
    host_pmm_pages_frees = 0U;
    host_irq_dispatches = 0U;
    host_irq_schedules = 0U;
    host_map_failure = 0U;
    host_pmm_failure = 0U;
    host_pci_result = OK;
    host_deferred_result = OK;
    host_irq_result = OK;
    host_reset_stuck = 0U;
    host_tx_complete = 1U;
    host_pmm_page_used[0] = 0U;
    host_pmm_page_used[1] = 0U;
    host_pmm_buffer_used[0] = 0U;
    host_pmm_buffer_used[1] = 0U;
    host_deferred_queue_count = 0U;
    host_irq_handler = 0;
    host_irq_line = 0U;
    host_log_calls = 0U;
}

static pci_device_t valid_pci(uint8_t device_number) {
    pci_device_t device = {0};

    device.vendor_id = HOST_E1000_VENDOR;
    device.device_id = HOST_E1000_DEVICE;
    device.class = HOST_E1000_CLASS;
    device.irq = HOST_IRQ_LINE;
    device.bus = 0U;
    device.device = device_number;
    device.function = 0U;
    device.bar0 = HOST_E1000_BAR;
    return device;
}

static void fill_frame(uint8_t* frame) {
    for (uint32_t index = 0U; index < HOST_FRAME_LENGTH; index++) {
        frame[index] = (uint8_t)(index + 1U);
    }
}

static int check_invalid_arguments(void) {
    pci_device_t device = valid_pci(1U);
    ethernet_interface_t interface;
    char long_id[32U] =
        "abcdefghijklmnopqrs-long";
    pci_device_t invalid = device;

    if (e1000_init(0, "e1000", &interface) != ERR_NULL) return 1;
    if (e1000_init(&device, 0, &interface) != ERR_NULL) return 2;
    if (e1000_init(&device, "e1000", 0) != ERR_NULL) return 3;
    invalid.vendor_id = 0U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 4;
    invalid = device;
    invalid.device_id = 0U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 5;
    invalid = device;
    invalid.class = 1U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 6;
    invalid = device;
    invalid.irq = 0xFFU;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 7;
    invalid = device;
    invalid.irq = 16U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 8;
    invalid = device;
    invalid.bar0 = HOST_E1000_BAR | 1U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 9;
    invalid = device;
    invalid.bar0 = HOST_E1000_BAR | 4U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 10;
    invalid = device;
    invalid.bar0 = 0U;
    if (e1000_init(&invalid, "e1000", &interface) != ERR_UNAVAILABLE) return 11;
    if (e1000_init(&device, "", &interface) != ERR_INVALID) return 12;
    if (e1000_init(&device, long_id, &interface) != ERR_OVERFLOW) return 13;
    return 0;
}

static int check_initialization_and_io(void) {
    pci_device_t device = valid_pci(2U);
    ethernet_interface_t interface;
    ethernet_driver_status_t status;
    uint8_t frame[HOST_FRAME_LENGTH];
    uint8_t received_frame[HOST_FRAME_LENGTH];
    uint8_t pending;
    uint8_t received;
    uint16_t length;
    uint32_t processed;
    registers_t regs = {0};

    fill_frame(frame);
    if (e1000_init(&device, "e1000-host", &interface) != OK) return 1;
    if (!interface.initialized || !interface.driver_context ||
        host_map_calls != 6U || host_irq_line != HOST_IRQ_LINE ||
        !host_irq_handler) return 2;
    if (e1000_init(&device, "e1000-host", &interface) != OK ||
        interface.driver_context == 0) return 3;
    if (interface.get_driver_status(0, &status) != ERR_NULL) return 4;
    if (interface.get_driver_status(interface.driver_context, 0) != ERR_NULL) {
        return 5;
    }
    if (interface.get_driver_status(interface.driver_context, &status) != OK ||
        !status.initialized || !status.link_up || status.rx_queue_depth != 0U) {
        return 6;
    }
    if (interface.send_frame(0, frame, sizeof(frame)) != ERR_NULL) return 7;
    if (interface.send_frame(interface.driver_context, 0, sizeof(frame)) != ERR_NULL) {
        return 8;
    }
    if (interface.send_frame(interface.driver_context, frame, 13U) != ERR_INVALID) {
        return 9;
    }
    if (interface.send_frame(interface.driver_context, frame, 1519U) != ERR_INVALID) {
        return 10;
    }
    host_mmio[HOST_E1000_STATUS / 4U] = 0U;
    if (interface.send_frame(interface.driver_context, frame, sizeof(frame)) !=
        ERR_UNAVAILABLE) return 11;
    host_mmio[HOST_E1000_STATUS / 4U] = HOST_E1000_LINK;
    host_tx_complete = 1U;
    if (interface.send_frame(interface.driver_context, frame, sizeof(frame)) != OK) {
        return 12;
    }
    if (interface.get_driver_status(interface.driver_context, &status) != OK ||
        status.tx_packets != 1U) {
        return 13;
    }
    host_tx_complete = 0U;
    if (interface.send_frame(interface.driver_context, frame, sizeof(frame)) !=
        ERR_TIMEOUT) return 14;
    for (uint32_t index = 0U; index < 7U; index++) {
        if (interface.send_frame(interface.driver_context, frame,
                                 sizeof(frame)) != ERR_TIMEOUT) return 15;
    }
    if (interface.send_frame(interface.driver_context, frame, sizeof(frame)) !=
        ERR_UNAVAILABLE) return 16;
    host_tx_complete = 1U;
    host_mmio[HOST_E1000_ICR / 4U] = HOST_E1000_RXT0 | HOST_E1000_LSC;
    e1000_host_inject_rx(interface.driver_context, frame, sizeof(frame),
                         HOST_E1000_RX_DD | HOST_E1000_RX_EOP, 0U);
    regs.int_no = 32U + HOST_IRQ_LINE;
    if (!host_irq_handler) return 17;
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) {
        return 17;
    }
    if (interface.rx_pending(interface.driver_context, &pending) != OK ||
        !pending) return 17;
    if (interface.receive_frame(interface.driver_context, received_frame, 10U,
                                &length, &received) != ERR_OVERFLOW) return 18;
    if (interface.receive_frame(interface.driver_context, received_frame,
                                sizeof(received_frame), &length, &received) != OK ||
        !received || length != HOST_FRAME_LENGTH ||
        received_frame[0] != frame[0] || received_frame[length - 1U] != frame[length - 1U]) {
        return 19;
    }
    if (interface.service_pending(0) != ERR_STATE) return 20;
    if (interface.rx_pending(interface.driver_context, 0) != ERR_NULL) return 22;
    if (interface.receive_frame(interface.driver_context, 0, sizeof(frame),
                                &length, &received) != ERR_NULL) return 23;
    if (interface.quiesce(0) != ERR_NULL) return 24;
    if (interface.quiesce(interface.driver_context) != OK) return 25;
    regs.int_no = 31U;
    e1000_handler(&regs);
    regs.int_no = 48U;
    e1000_handler(&regs);
    e1000_handler(0);
    host_mmio[HOST_E1000_ICR / 4U] = 0U;
    e1000_handler(&regs);
    return 0;
}

static int check_receive_errors_and_queue(void) {
    pci_device_t device = valid_pci(3U);
    ethernet_interface_t interface;
    ethernet_driver_status_t status;
    uint8_t frame[HOST_FRAME_LENGTH];
    uint8_t output[HOST_FRAME_LENGTH];
    uint8_t pending;
    uint8_t received;
    uint16_t length;
    uint32_t processed;
    registers_t regs = {0};

    fill_frame(frame);
    if (e1000_init(&device, "e1000-rx", &interface) != OK) return 1;
    regs.int_no = 32U + HOST_IRQ_LINE;
    host_mmio[HOST_E1000_ICR / 4U] = HOST_E1000_RXO | HOST_E1000_RXSEQ;
    e1000_host_inject_rx(interface.driver_context, frame, sizeof(frame),
                         HOST_E1000_RX_DD, 0U);
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) {
        return 2;
    }
    e1000_host_inject_rx(interface.driver_context, frame, sizeof(frame),
                         HOST_E1000_RX_DD | HOST_E1000_RX_EOP, 1U);
    host_mmio[HOST_E1000_ICR / 4U] = HOST_E1000_RXT0;
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) return 3;
    e1000_host_inject_rx(interface.driver_context, frame, 13U,
                         HOST_E1000_RX_DD | HOST_E1000_RX_EOP, 0U);
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) return 4;
    if (interface.get_driver_status(interface.driver_context, &status) != OK ||
        status.rx_errors < 3U || status.rx_dropped < 3U) return 5;
    if (interface.receive_frame(interface.driver_context, output, sizeof(output),
                                &length, &received) != OK || received) return 6;
    host_mmio[HOST_E1000_ICR / 4U] = HOST_E1000_RXT0;
    for (uint32_t index = 0U; index < 9U; index++) {
        e1000_host_inject_rx(interface.driver_context, frame, sizeof(frame),
                             HOST_E1000_RX_DD | HOST_E1000_RX_EOP, 0U);
        host_irq_handler(&regs);
        if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) {
            return 7;
        }
    }
    if (interface.get_driver_status(interface.driver_context, &status) != OK ||
        status.rx_queue_dropped == 0U || status.rx_queue_high_water == 0U) {
        return 8;
    }
    while (interface.rx_pending(interface.driver_context, &pending) == OK &&
           pending) {
        if (interface.receive_frame(interface.driver_context, output,
                                    sizeof(output), &length, &received) != OK) {
            return 9;
        }
    }
    if (interface.quiesce(interface.driver_context) != OK) return 10;
    return 0;
}

static int check_initialization_failures(void) {
    pci_device_t device;
    ethernet_interface_t interface;

    reset_fixture();
    device = valid_pci(0U);
    host_reset_stuck = 1U;
    if (e1000_init(&device, "reset-fail", &interface) != ERR_TIMEOUT) return 1;
    if (host_pmm_page_used[0] || host_pmm_buffer_used[0]) return 2;
    reset_fixture();
    device = valid_pci(0U);
    host_mmio[HOST_E1000_RAL0 / 4U] = 0U;
    host_mmio[HOST_E1000_RAH0 / 4U] = 0U;
    if (e1000_init(&device, "mac-fail", &interface) != ERR_UNAVAILABLE) return 3;
    if (host_pmm_page_used[0] || host_pmm_buffer_used[0]) return 4;
    reset_fixture();
    device = valid_pci(0U);
    host_pci_result = ERR_UNAVAILABLE;
    if (e1000_init(&device, "pci-fail", &interface) != ERR_UNAVAILABLE) return 5;
    reset_fixture();
    device = valid_pci(0U);
    host_map_failure = 1U;
    if (e1000_init(&device, "map-fail", &interface) != ERR_MEM) return 6;
    reset_fixture();
    device = valid_pci(0U);
    host_map_failure = 2U;
    if (e1000_init(&device, "map-null", &interface) != ERR_MEM) return 7;
    reset_fixture();
    device = valid_pci(0U);
    host_pmm_failure = 1U;
    if (e1000_init(&device, "dma-page", &interface) != ERR_MEM) return 8;
    reset_fixture();
    device = valid_pci(0U);
    host_pmm_failure = 3U;
    if (e1000_init(&device, "dma-rx", &interface) != ERR_MEM) return 9;
    reset_fixture();
    device = valid_pci(0U);
    host_pmm_failure = 4U;
    if (e1000_init(&device, "dma-tx", &interface) != ERR_MEM) return 10;
    reset_fixture();
    device = valid_pci(0U);
    host_deferred_result = ERR_UNAVAILABLE;
    if (e1000_init(&device, "deferred-fail", &interface) != ERR_UNAVAILABLE) {
        return 11;
    }
    reset_fixture();
    device = valid_pci(0U);
    host_irq_result = ERR_UNAVAILABLE;
    if (e1000_init(&device, "irq-fail", &interface) != ERR_UNAVAILABLE) return 12;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    reset_fixture();
    result = check_invalid_arguments();
    if (result == 0) {
        e1000_host_reset_devices();
        reset_fixture();
        result = check_initialization_and_io();
    }
    if (result == 0) {
        e1000_host_reset_devices();
        reset_fixture();
        result = check_receive_errors_and_queue();
    }
    if (result == 0) {
        e1000_host_reset_devices();
        reset_fixture();
        result = check_initialization_failures();
    }
    coverage_active = 0U;
    printf("E1000_HOST_RESULT=%d logs=%u maps=%u schedules=%u frees=%u/%u\n",
           result, host_log_calls, host_map_calls, host_irq_schedules,
           host_pmm_page_frees, host_pmm_pages_frees);
    coverage_emit(result);
    return result;
}
