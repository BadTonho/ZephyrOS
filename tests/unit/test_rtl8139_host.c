#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/irq_deferred.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "drivers/rtl8139.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_IO_SIZE 0x100U
#define HOST_IO_BASE 0xC000U
#define HOST_RX_PAGE_COUNT 3U
#define HOST_TX_PAGE_COUNT 2U
#define HOST_RX_SIZE 9744U
#define HOST_FRAME_LENGTH 60U
#define HOST_WIRE_LENGTH (HOST_FRAME_LENGTH + 4U)
#define HOST_ISR 0x3EU
#define HOST_CR 0x37U
#define HOST_CAPR 0x38U
#define HOST_TSD0 0x10U
#define HOST_MSR 0x58U
#define HOST_CR_BUFE 0x01U
#define HOST_CR_RST 0x10U
#define HOST_CR_RE 0x08U
#define HOST_CR_TE 0x04U
#define HOST_TSD_OWN (1U << 13U)
#define HOST_TSD_TOK (1U << 15U)
#define HOST_TSD_TABT (1U << 30U)
#define HOST_INT_ROK (1U << 0U)
#define HOST_INT_TER (1U << 3U)
#define HOST_INT_ROVW (1U << 4U)
#define HOST_INT_PUN_LINK (1U << 5U)
#define HOST_INT_SERR (1U << 15U)
#define HOST_IRQ_UNKNOWN 0xFFU
#define HOST_RX_STATUS_ROK (1U << 0U)
#define HOST_MSR_LINKB (1U << 2U)
#define HOST_QUEUE_CAPACITY 4U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_io[HOST_IO_SIZE];
static uint32_t host_tsd[4];
static uint8_t host_rx[HOST_RX_PAGE_COUNT][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t host_tx[HOST_TX_PAGE_COUNT][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t host_rx_used;
static uint8_t host_tx_used;
static uint32_t host_ticks;
static uint32_t host_log_count;
static uint32_t host_irq_count;
static uint8_t host_reset_stuck;
static uint8_t host_invalid_mac;
static uint8_t host_fail_rx;
static uint8_t host_fail_tx;
static uint8_t host_tx_timeout;
static uint8_t host_tx_error;
static uint8_t host_rx_stop_after_advance;
static uint8_t host_irq_enabled;
static isr_handler_t host_irq_handler;
static irq_deferred_work_t* host_queue[HOST_QUEUE_CAPACITY];
static uint32_t host_queue_count;

void rtl8139_host_reset(void);

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
    printf("ZCOV_BEGIN|case=host:drivers:rtl8139|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:rtl8139|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:rtl8139|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    host_log_count++;
}

void log_print_code(log_level_t level, const char* module, int32_t error_code,
                    const char* message) {
    (void)level;
    (void)module;
    (void)error_code;
    (void)message;
    host_log_count++;
}

uint32_t timer_get_ticks(void) {
    return host_ticks++;
}

static uint8_t host_read8(uint16_t port) {
    uint16_t offset = port >= HOST_IO_BASE ? port - HOST_IO_BASE : port;

    if (offset == HOST_CR && host_reset_stuck) return HOST_CR_RST;
    if (offset < 6U && host_invalid_mac) return 0U;
    if (offset >= HOST_IO_SIZE) return 0xFFU;
    return host_io[offset];
}

static uint16_t host_read16(uint16_t port) {
    uint16_t offset = port >= HOST_IO_BASE ? port - HOST_IO_BASE : port;

    if (offset == HOST_ISR || offset == HOST_CAPR) {
        return (uint16_t)(host_io[offset] | ((uint16_t)host_io[offset + 1U] << 8U));
    }
    if (offset >= HOST_TSD0 && offset < HOST_TSD0 + sizeof(host_tsd) &&
        (offset - HOST_TSD0) % sizeof(uint32_t) == 0U) {
        return (uint16_t)host_tsd[(offset - HOST_TSD0) / sizeof(uint32_t)];
    }
    return host_read8(port);
}

static uint32_t host_read32(uint16_t port) {
    uint16_t offset = port >= HOST_IO_BASE ? port - HOST_IO_BASE : port;

    if (offset >= HOST_TSD0 && offset < HOST_TSD0 + sizeof(host_tsd) &&
        (offset - HOST_TSD0) % sizeof(uint32_t) == 0U) {
        return host_tsd[(offset - HOST_TSD0) / sizeof(uint32_t)];
    }
    return (uint32_t)host_read16(port) |
           ((uint32_t)host_read16((uint16_t)(port + 2U)) << 16U);
}

static void host_write8(uint16_t port, uint8_t value) {
    uint16_t offset = port >= HOST_IO_BASE ? port - HOST_IO_BASE : port;

    if (offset >= HOST_IO_SIZE) return;
    if (offset == HOST_CR && (value & HOST_CR_RST)) {
        host_io[offset] = host_reset_stuck ? HOST_CR_RST : 0U;
        return;
    }
    host_io[offset] = value;
}

static void host_write16(uint16_t port, uint16_t value) {
    uint16_t offset = port >= HOST_IO_BASE ? port - HOST_IO_BASE : port;

    if (offset >= HOST_IO_SIZE - 1U) return;
    if (offset == HOST_ISR) {
        uint16_t current = host_read16(port);

        current &= (uint16_t)~value;
        host_io[offset] = (uint8_t)current;
        host_io[offset + 1U] = (uint8_t)(current >> 8U);
        return;
    }
    host_io[offset] = (uint8_t)value;
    host_io[offset + 1U] = (uint8_t)(value >> 8U);
    if (offset == HOST_CAPR && host_rx_stop_after_advance) {
        host_io[HOST_CR] |= HOST_CR_BUFE;
    }
}

static void host_write32(uint16_t port, uint32_t value) {
    uint16_t offset = port >= HOST_IO_BASE ? port - HOST_IO_BASE : port;

    if (offset >= HOST_TSD0 && offset < HOST_TSD0 + sizeof(host_tsd) &&
        (offset - HOST_TSD0) % sizeof(uint32_t) == 0U) {
        uint32_t index = (offset - HOST_TSD0) / sizeof(uint32_t);

        if (host_tx_timeout) host_tsd[index] = 0U;
        else if (host_tx_error) {
            host_tsd[index] = HOST_TSD_OWN | HOST_TSD_TABT;
            host_tx_error = 0U;
        } else host_tsd[index] = HOST_TSD_OWN | HOST_TSD_TOK;
        return;
    }
    if (offset < HOST_IO_SIZE - 3U) {
        host_io[offset] = (uint8_t)value;
        host_io[offset + 1U] = (uint8_t)(value >> 8U);
        host_io[offset + 2U] = (uint8_t)(value >> 16U);
        host_io[offset + 3U] = (uint8_t)(value >> 24U);
    }
}

uint8_t rtl8139_host_in8(uint16_t port) {
    return host_read8(port);
}

uint16_t rtl8139_host_in16(uint16_t port) {
    return host_read16(port);
}

uint32_t rtl8139_host_in32(uint16_t port) {
    return host_read32(port);
}

void rtl8139_host_out8(uint16_t port, uint8_t value) {
    host_write8(port, value);
}

void rtl8139_host_out16(uint16_t port, uint16_t value) {
    host_write16(port, value);
}

void rtl8139_host_out32(uint16_t port, uint32_t value) {
    host_write32(port, value);
}

uint32_t rtl8139_host_physical_address(const void* address) {
    const uint8_t* bytes = (const uint8_t*)address;
    const uint8_t* rx_start = host_rx[0];
    const uint8_t* tx_start = host_tx[0];

    if (bytes >= rx_start && bytes < rx_start + sizeof(host_rx)) {
        return 0x00300000U + (uint32_t)(bytes - rx_start);
    }
    if (bytes >= tx_start && bytes < tx_start + sizeof(host_tx)) {
        return 0x00400000U + (uint32_t)(bytes - tx_start);
    }
    return 0U;
}

uint32_t rtl8139_host_irq_save(void) {
    return host_irq_enabled ? (1U << 9U) : 0U;
}

void rtl8139_host_irq_restore(uint32_t flags) {
    host_irq_enabled = (flags & (1U << 9U)) != 0U;
}

int pci_enable_io_and_bus_mastering(const pci_device_t* device) {
    return device ? OK : ERR_NULL;
}

int idt_register_shared_irq_handler(uint8_t irq_line, isr_handler_t handler) {
    if (!handler || irq_line > 15U) return ERR_INVALID;
    host_irq_handler = handler;
    host_irq_count++;
    return OK;
}

int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line, irq_deferred_callback_t callback,
                           void* context) {
    if (!work || !owner || !callback) return ERR_NULL;
    kmemset(work, 0, sizeof(*work));
    work->callback = callback;
    work->context = context;
    work->irq_line = irq_line;
    work->initialized = 1U;
    for (uint32_t index = 0U; index + 1U < IRQ_DEFERRED_OWNER_SIZE &&
         owner[index]; index++) {
        work->owner[index] = owner[index];
    }
    return OK;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    if (!work || !work->initialized) return ERR_STATE;
    if (work->queued) {
        work->coalesced++;
        return OK;
    }
    if (host_queue_count >= HOST_QUEUE_CAPACITY) {
        work->rejected++;
        return ERR_OVERFLOW;
    }
    work->queued = 1U;
    work->scheduled++;
    host_queue[host_queue_count++] = work;
    return OK;
}

int irq_deferred_cancel(irq_deferred_work_t* work) {
    if (!work || !work->initialized) return ERR_STATE;
    for (uint32_t index = 0U; index < host_queue_count; index++) {
        if (host_queue[index] != work) continue;
        host_queue[index] = host_queue[host_queue_count - 1U];
        host_queue_count--;
        break;
    }
    work->queued = 0U;
    work->cancelled = 1U;
    return OK;
}

int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!out_processed) return ERR_NULL;
    while (processed < budget && host_queue_count) {
        irq_deferred_work_t* work = host_queue[0];

        for (uint32_t index = 1U; index < host_queue_count; index++) {
            host_queue[index - 1U] = host_queue[index];
        }
        host_queue_count--;
        work->queued = 0U;
        work->dispatched++;
        if (work->callback) work->callback(work->context);
        processed++;
    }
    *out_processed = processed;
    return OK;
}

void* pmm_alloc_pages_in_zone(uint32_t count, memory_zone_t zone) {
    if (zone != MEMORY_ZONE_BUFFER) return 0;
    if (count == HOST_RX_PAGE_COUNT && !host_rx_used && !host_fail_rx) {
        host_rx_used = 1U;
        kmemset(host_rx, 0, sizeof(host_rx));
        return host_rx;
    }
    if (count == HOST_TX_PAGE_COUNT && !host_tx_used && !host_fail_tx) {
        host_tx_used = 1U;
        kmemset(host_tx, 0, sizeof(host_tx));
        for (uint32_t index = 0U; index < 4U; index++) {
            host_tsd[index] = HOST_TSD_OWN | HOST_TSD_TOK;
        }
        return host_tx;
    }
    return 0;
}

void pmm_free_pages(void* address, uint32_t count) {
    if (address == host_rx && count == HOST_RX_PAGE_COUNT) host_rx_used = 0U;
    if (address == host_tx && count == HOST_TX_PAGE_COUNT) host_tx_used = 0U;
}

static void reset_fixture(void) {
    rtl8139_host_reset();
    kmemset(host_io, 0, sizeof(host_io));
    kmemset(host_tsd, 0, sizeof(host_tsd));
    host_rx_used = 0U;
    host_tx_used = 0U;
    host_ticks = 0U;
    host_log_count = 0U;
    host_irq_count = 0U;
    host_reset_stuck = 0U;
    host_invalid_mac = 0U;
    host_fail_rx = 0U;
    host_fail_tx = 0U;
    host_tx_timeout = 0U;
    host_tx_error = 0U;
    host_rx_stop_after_advance = 1U;
    host_irq_enabled = 1U;
    host_irq_handler = 0;
    host_queue_count = 0U;
    host_io[HOST_MSR] = 0U;
    host_io[HOST_CR] = HOST_CR_RE | HOST_CR_TE;
    host_io[0U] = 0x52U;
    host_io[1U] = 0x54U;
    host_io[2U] = 0x56U;
    host_io[3U] = 0x78U;
    host_io[4U] = 0x9AU;
    host_io[5U] = 0xBCU;
}

static pci_device_t valid_pci(void) {
    pci_device_t pci = {0};

    pci.vendor_id = 0x10ECU;
    pci.device_id = 0x8139U;
    pci.class = 0x02U;
    pci.irq = 11U;
    pci.bus = 0U;
    pci.device = 3U;
    pci.function = 0U;
    pci.bar0 = HOST_IO_BASE | 1U;
    return pci;
}

static int check_invalid_arguments(void) {
    pci_device_t pci = valid_pci();
    pci_device_t invalid = pci;
    ethernet_interface_t interface;
    char long_id[ETHERNET_INTERFACE_ID_SIZE + 4U] =
        "abcdefghijklmnopqrstuv";

    if (rtl8139_init(0, "rtl", &interface) != ERR_NULL) return 1;
    if (rtl8139_init(&pci, 0, &interface) != ERR_NULL) return 2;
    if (rtl8139_init(&pci, "rtl", 0) != ERR_NULL) return 3;
    invalid.vendor_id = 0U;
    if (rtl8139_init(&invalid, "rtl", &interface) != ERR_UNAVAILABLE) return 4;
    invalid = pci;
    invalid.bar0 &= ~1U;
    if (rtl8139_init(&invalid, "rtl", &interface) != ERR_UNAVAILABLE) return 5;
    invalid = pci;
    invalid.bar0 = 0xFF05U;
    if (rtl8139_init(&invalid, "rtl", &interface) != ERR_UNAVAILABLE) return 6;
    invalid = pci;
    invalid.irq = HOST_IRQ_UNKNOWN;
    if (rtl8139_init(&invalid, "rtl", &interface) != ERR_UNAVAILABLE) return 7;
    if (rtl8139_init(&pci, "", &interface) != ERR_INVALID) return 8;
    if (rtl8139_init(&pci, long_id, &interface) != ERR_OVERFLOW) return 9;
    if (rtl8139_init(&pci, "rtl", &interface) != OK) return 10;
    if (interface.send_frame(0, 0, HOST_FRAME_LENGTH) != ERR_NULL) return 11;
    if (interface.get_driver_status(0, 0) != ERR_NULL) return 12;
    if (interface.rx_pending(0, 0) != ERR_NULL) return 13;
    if (interface.receive_frame(0, 0, HOST_FRAME_LENGTH, 0, 0) != ERR_NULL) {
        return 14;
    }
    if (interface.service_pending(0) != ERR_STATE) return 15;
    if (interface.quiesce(0) != ERR_NULL) return 16;
    rtl8139_handler(0);
    return 0;
}

static int check_controller(void) {
    static const char interface_id[ETHERNET_INTERFACE_ID_SIZE] = "rtl-host";
    pci_device_t pci = valid_pci();
    ethernet_interface_t interface;
    ethernet_driver_status_t status;
    uint8_t frame[HOST_FRAME_LENGTH];
    uint8_t output[HOST_FRAME_LENGTH];
    uint8_t pending;
    uint8_t received;
    uint16_t length;
    uint32_t processed;
    registers_t regs = {0};

    for (uint32_t index = 0U; index < sizeof(frame); index++) {
        frame[index] = (uint8_t)(index + 1U);
    }
    if (rtl8139_init(&pci, interface_id, &interface) != OK) return 1;
    if (rtl8139_init(&pci, interface_id, &interface) != OK ||
        !interface.initialized || !interface.driver_context ||
        host_irq_count != 1U || !host_irq_handler) return 2;
    if (interface.get_driver_status(0, &status) != ERR_NULL) return 3;
    if (interface.get_driver_status(interface.driver_context, &status) != OK ||
        !status.initialized || !status.link_up || status.rx_queue_depth != 0U) {
        return 4;
    }
    if (interface.rx_pending(interface.driver_context, &pending) != OK ||
        pending) return 5;
    if (interface.receive_frame(interface.driver_context, output,
                                sizeof(output), &length, &received) != OK ||
        received || length != 0U) return 6;
    if (interface.send_frame(interface.driver_context, frame, 59U) !=
        ERR_INVALID) return 7;
    if (interface.send_frame(interface.driver_context, frame, 1519U) !=
        ERR_INVALID) return 8;
    host_tsd[0] = 0U;
    if (interface.send_frame(interface.driver_context, frame,
                             sizeof(frame)) != ERR_UNAVAILABLE) return 9;
    host_tsd[0] = HOST_TSD_OWN | HOST_TSD_TABT;
    host_tx_error = 1U;
    if (interface.send_frame(interface.driver_context, frame,
                             sizeof(frame)) != ERR_STATE) return 10;
    if (interface.send_frame(interface.driver_context, frame,
                             sizeof(frame)) != OK) return 11;
    if (interface.get_driver_status(interface.driver_context, &status) != OK ||
        status.tx_packets != 1U || status.tx_errors != 1U) return 12;
    host_tx_timeout = 1U;
    if (interface.send_frame(interface.driver_context, frame,
                             sizeof(frame)) != ERR_TIMEOUT) return 13;
    host_tx_timeout = 0U;
    host_tsd[1] = HOST_TSD_OWN | HOST_TSD_TOK;
    if (interface.send_frame(interface.driver_context, frame,
                             sizeof(frame)) != OK) return 14;

    host_rx[0][0] = HOST_RX_STATUS_ROK;
    host_rx[0][1] = 0U;
    host_rx[0][2] = (uint8_t)HOST_WIRE_LENGTH;
    host_rx[0][3] = (uint8_t)(HOST_WIRE_LENGTH >> 8U);
    for (uint32_t index = 0U; index < HOST_FRAME_LENGTH; index++) {
        host_rx[0][4U + index] = (uint8_t)(0xC0U + index);
    }
    host_io[HOST_ISR] = HOST_INT_ROK;
    host_io[HOST_ISR + 1U] = 0U;
    regs.int_no = 43U;
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) {
        return 15;
    }
    if (interface.rx_pending(interface.driver_context, &pending) != OK ||
        !pending) return 16;
    if (interface.receive_frame(interface.driver_context, output, 59U,
                                &length, &received) != ERR_OVERFLOW) return 17;
    if (interface.receive_frame(interface.driver_context, output,
                                sizeof(output), &length, &received) != OK ||
        !received || length != HOST_FRAME_LENGTH || output[0] != 0xC0U ||
        output[HOST_FRAME_LENGTH - 1U] != 0xFBU) return 18;
    if (interface.rx_pending(interface.driver_context, &pending) != OK ||
        pending) return 19;
    host_io[HOST_ISR] = (uint8_t)(HOST_INT_ROVW | HOST_INT_TER |
                                  HOST_INT_PUN_LINK);
    host_io[HOST_ISR + 1U] = (uint8_t)(HOST_INT_SERR >> 8U);
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) {
        return 20;
    }
    if (interface.service_pending(interface.driver_context) != OK) return 21;
    host_rx_stop_after_advance = 0U;
    host_io[HOST_CR] = HOST_CR_RE | HOST_CR_TE;
    host_rx[0][0] = HOST_RX_STATUS_ROK;
    host_rx[0][1] = 0U;
    host_rx[0][2] = 0U;
    host_rx[0][3] = 0U;
    host_io[HOST_ISR] = HOST_INT_ROK;
    host_irq_handler(&regs);
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U) {
        return 22;
    }
    if (interface.quiesce(interface.driver_context) != OK) return 23;
    host_io[HOST_ISR] = 0xFFU;
    host_io[HOST_ISR + 1U] = 0xFFU;
    host_irq_handler(&regs);
    if (interface.service_pending(interface.driver_context) != OK) return 24;
    rtl8139_host_reset();
    if (host_rx_used || host_tx_used) return 25;
    return 0;
}

static int check_init_failures(void) {
    static const char interface_id[ETHERNET_INTERFACE_ID_SIZE] = "rtl-fail";
    pci_device_t pci = valid_pci();
    ethernet_interface_t interface;

    reset_fixture();
    host_reset_stuck = 1U;
    if (rtl8139_init(&pci, interface_id, &interface) != ERR_TIMEOUT) return 1;
    if (host_rx_used || host_tx_used) return 2;
    reset_fixture();
    host_invalid_mac = 1U;
    if (rtl8139_init(&pci, interface_id, &interface) != ERR_UNAVAILABLE) return 3;
    if (host_rx_used || host_tx_used) return 4;
    reset_fixture();
    host_fail_rx = 1U;
    if (rtl8139_init(&pci, interface_id, &interface) != ERR_MEM) return 5;
    reset_fixture();
    host_fail_tx = 1U;
    if (rtl8139_init(&pci, interface_id, &interface) != ERR_MEM) return 6;
    if (host_rx_used || host_tx_used) return 7;
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    reset_fixture();
    result = check_invalid_arguments();
    if (result == 0) {
        reset_fixture();
        result = check_controller();
    }
    if (result == 0) result = check_init_failures();
    coverage_active = 0U;
    printf("RTL8139_HOST_RESULT=%d logs=%u irq=%u\n", result,
           host_log_count, host_irq_count);
    coverage_emit(result);
    return result;
}
