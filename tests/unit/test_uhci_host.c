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
#include "drivers/uhci.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_PAGE_COUNT 4U
#define HOST_IO_SPACE_BYTES 0x20U
#define HOST_USBCMD 0x00U
#define HOST_USBSTS 0x02U
#define HOST_PORT0 0x10U
#define HOST_PORT1 0x12U
#define HOST_USBCMD_HOST_RESET 0x0002U
#define HOST_PORT_CCS 0x0001U
#define HOST_PORT_PE 0x0004U
#define HOST_PORT_PR 0x0200U
#define HOST_PID_IN 0x69U
#define HOST_PID_OUT 0xE1U
#define HOST_PID_SETUP 0x2DU
#define HOST_TD_ACTIVE (1U << 23U)
#define HOST_TD_LENGTH_MASK 0x7FFU
#define HOST_TD_LENGTH_SHIFT 21U
#define HOST_REQUEST_GET_DESCRIPTOR 6U
#define HOST_DESCRIPTOR_DEVICE 1U
#define HOST_DESCRIPTOR_CONFIGURATION 2U
#define HOST_DESCRIPTOR_BUFFER_OFFSET 512U
#define HOST_BULK_BUFFER_OFFSET 1024U

typedef struct {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
} host_td_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_pages_used[HOST_PAGE_COUNT];
static uint8_t host_pages[HOST_PAGE_COUNT][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint16_t host_registers[HOST_IO_SPACE_BYTES / sizeof(uint16_t)];
static uint16_t host_port0_status;
static uint32_t host_ticks;
static uint8_t host_irq_registered;
static uint32_t host_irq_handler_count;
static isr_handler_t host_irq_handler;
static uint32_t host_log_count;
static uint32_t host_transfer_count;
static uint8_t host_invalid_device;
static uint8_t host_transfer_fault;
static uint8_t host_interrupt_leave_active;
static irq_deferred_work_t* host_queued_work[4];
static uint32_t host_queued_count;

void uhci_host_reset(void);

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
    printf("ZCOV_BEGIN|case=host:drivers:uhci|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:uhci|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:uhci|value=0x%08X\n",
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

uint32_t timer_get_frequency(void) {
    return 1000U;
}

int pci_enable_io_and_bus_mastering(const pci_device_t* device) {
    return device ? OK : ERR_NULL;
}

int idt_register_shared_irq_handler(uint8_t irq_line, isr_handler_t handler) {
    if (!handler || irq_line > 15U) return ERR_INVALID;
    host_irq_registered = 1U;
    host_irq_handler = handler;
    host_irq_handler_count++;
    return OK;
}

int idt_get_shared_irq_handler_count(uint8_t irq_line, uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (!host_irq_registered || irq_line > 15U) return ERR_NOT_FOUND;
    *out_count = 1U;
    return OK;
}

void* pmm_alloc_page_in_zone(memory_zone_t zone) {
    if (zone != MEMORY_ZONE_BUFFER) return 0;
    for (uint32_t index = 0U; index < HOST_PAGE_COUNT; index++) {
        if (!host_pages_used[index]) {
            host_pages_used[index] = 1U;
            kmemset(host_pages[index], 0, PAGE_SIZE);
            return host_pages[index];
        }
    }
    return 0;
}

void pmm_free_page(void* address) {
    for (uint32_t index = 0U; index < HOST_PAGE_COUNT; index++) {
        if (address == host_pages[index]) host_pages_used[index] = 0U;
    }
}

int irq_deferred_work_init(irq_deferred_work_t* work, const char* owner,
                           uint8_t irq_line, irq_deferred_callback_t callback,
                           void* context) {
    if (!work || !callback || !owner) return ERR_NULL;
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

int irq_deferred_cancel(irq_deferred_work_t* work) {
    if (!work || !work->initialized) return ERR_STATE;
    for (uint32_t index = 0U; index < host_queued_count; index++) {
        if (host_queued_work[index] != work) continue;
        host_queued_work[index] = host_queued_work[host_queued_count - 1U];
        host_queued_count--;
        break;
    }
    work->queued = 0U;
    work->cancelled = 1U;
    return OK;
}

int irq_deferred_dispatch(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!out_processed) return ERR_NULL;
    while (processed < budget && host_queued_count) {
        irq_deferred_work_t* work = host_queued_work[0];

        for (uint32_t index = 1U; index < host_queued_count; index++) {
            host_queued_work[index - 1U] = host_queued_work[index];
        }
        host_queued_count--;
        work->queued = 0U;
        work->dispatched++;
        if (work->callback) work->callback(work->context);
        processed++;
    }
    *out_processed = processed;
    return OK;
}

static int host_schedule_work(irq_deferred_work_t* work) {
    if (!work || !work->initialized) return ERR_STATE;
    if (work->queued) return OK;
    if (host_queued_count >= 4U) return ERR_OVERFLOW;
    work->queued = 1U;
    work->scheduled++;
    host_queued_work[host_queued_count++] = work;
    return OK;
}

int irq_deferred_schedule(irq_deferred_work_t* work) {
    return host_schedule_work(work);
}

uint32_t uhci_host_physical_address(const void* address) {
    for (uint32_t index = 0U; index < HOST_PAGE_COUNT; index++) {
        const uint8_t* page = host_pages[index];
        const uint8_t* bytes = (const uint8_t*)address;

        if (bytes >= page && bytes < page + PAGE_SIZE) {
            return 0x00100000U + index * PAGE_SIZE +
                   (uint32_t)(bytes - page);
        }
    }
    return 0U;
}

uint16_t uhci_host_in16(uint16_t io_base, uint16_t offset) {
    (void)io_base;
    if (offset == HOST_PORT0) return host_port0_status;
    if (offset == HOST_PORT1) return 0U;
    if (offset == HOST_USBSTS) return host_registers[HOST_USBSTS / 2U];
    if (offset == HOST_USBCMD) return host_registers[HOST_USBCMD / 2U];
    return offset < HOST_IO_SPACE_BYTES ? host_registers[offset / 2U] : 0U;
}

void uhci_host_out8(uint16_t io_base, uint16_t offset, uint8_t value) {
    (void)io_base;
    (void)offset;
    (void)value;
}

void uhci_host_out16(uint16_t io_base, uint16_t offset, uint16_t value) {
    (void)io_base;
    if (offset == HOST_USBCMD && (value & HOST_USBCMD_HOST_RESET)) {
        host_registers[HOST_USBCMD / 2U] = 0U;
        return;
    }
    if (offset == HOST_PORT0 && (value & HOST_PORT_PR)) {
        host_port0_status = HOST_PORT_CCS | HOST_PORT_PE;
        return;
    }
    if (offset == HOST_USBSTS) {
        host_registers[HOST_USBSTS / 2U] &= (uint16_t)~value;
        return;
    }
    if (offset < HOST_IO_SPACE_BYTES) host_registers[offset / 2U] = value;
}

void uhci_host_out32(uint16_t io_base, uint16_t offset, uint32_t value) {
    (void)io_base;
    if (offset + sizeof(uint32_t) > HOST_IO_SPACE_BYTES) return;
    host_registers[offset / 2U] = (uint16_t)value;
    host_registers[offset / 2U + 1U] = (uint16_t)(value >> 16U);
}

static uint16_t host_packet_length(uint32_t token) {
    uint32_t encoded = (token >> HOST_TD_LENGTH_SHIFT) &
                       HOST_TD_LENGTH_MASK;

    return encoded == HOST_TD_LENGTH_MASK ? 0U : (uint16_t)(encoded + 1U);
}

static void host_fill_device_descriptor(uint8_t* data, uint16_t length) {
    static const uint8_t descriptor[18] = {
        18U, 1U, 0U, 2U, 0U, 0U, 0U, 64U, 0x34U, 0x12U,
        0x78U, 0x56U, 1U, 0U, 1U, 2U, 3U, 1U
    };

    for (uint16_t index = 0U; index < length; index++) {
        data[index] = descriptor[index];
    }
}

static void host_fill_configuration_descriptor(uint8_t* data,
                                               uint16_t length) {
    static const uint8_t descriptor[] = {
        9U, 2U, 39U, 0U, 1U, 1U, 0x80U, 50U, 0U,
        9U, 4U, 0U, 0U, 3U, 3U, 1U, 1U, 0U,
        7U, 5U, 0x81U, 3U, 8U, 0U, 10U,
        7U, 5U, 0x82U, 2U, 64U, 0U, 0U,
        7U, 5U, 0x02U, 2U, 64U, 0U, 0U
    };

    for (uint16_t index = 0U; index < length; index++) {
        data[index] = descriptor[index];
    }
}

void uhci_host_complete_transfer(void* pool, uint32_t td_count,
                                 uint8_t* buffer_pool) {
    host_td_t* td_pool = (host_td_t*)pool;
    uint8_t* setup = buffer_pool;
    uint8_t* transfer_data = buffer_pool + HOST_DESCRIPTOR_BUFFER_OFFSET;
    uint8_t pid = (uint8_t)td_pool[0].token;
    uint16_t length = (uint16_t)(setup[6] | ((uint16_t)setup[7] << 8U));

    host_transfer_count++;
    if (pid == HOST_PID_SETUP && (setup[0] & 0x80U) && length) {
        uint8_t type = setup[3];

        if (type == HOST_DESCRIPTOR_DEVICE) {
            if (host_invalid_device) {
                transfer_data[0] = 1U;
                transfer_data[1] = HOST_DESCRIPTOR_CONFIGURATION;
            } else {
                host_fill_device_descriptor(transfer_data, length);
            }
        } else if (type == HOST_DESCRIPTOR_CONFIGURATION) {
            host_fill_configuration_descriptor(transfer_data, length);
        }
    } else if ((pid == HOST_PID_IN || pid == HOST_PID_OUT) &&
               ((td_pool[0].token >> 15U) & 0x0FU)) {
        uint16_t transfer_length = 0U;

        for (uint32_t index = 0U; index < td_count; index++) {
            transfer_length = (uint16_t)(transfer_length +
                                         host_packet_length(td_pool[index].token));
        }
        for (uint16_t index = 0U; index < transfer_length &&
             pid == HOST_PID_IN; index++) {
            buffer_pool[HOST_BULK_BUFFER_OFFSET + index] =
                (uint8_t)(0xA0U + index);
        }
    }
    for (uint32_t index = 0U; index < td_count; index++) {
        uint16_t packet = host_packet_length(td_pool[index].token);

        td_pool[index].status = packet ? (uint32_t)(packet - 1U) : 0U;
        td_pool[index].status &= ~HOST_TD_ACTIVE;
    }
    if (host_transfer_fault) {
        td_pool[0].status = 1U << 22U;
        host_transfer_fault = 0U;
    }
}

void uhci_host_complete_interrupt(void* pool, uint32_t td_index,
                                  uint8_t* buffer, uint16_t max_packet) {
    host_td_t* td_pool = (host_td_t*)pool;

    if (host_interrupt_leave_active) return;
    td_pool[td_index].status = max_packet ? (uint32_t)(max_packet - 1U) : 0U;
    for (uint16_t index = 0U; index < max_packet; index++) {
        buffer[index] = (uint8_t)(0xC0U + index);
    }
}

static void reset_fixture(void) {
    uhci_host_reset();
    kmemset(host_pages_used, 0, sizeof(host_pages_used));
    kmemset(host_registers, 0, sizeof(host_registers));
    host_port0_status = HOST_PORT_CCS;
    host_ticks = 0U;
    host_irq_registered = 0U;
    host_irq_handler_count = 0U;
    host_irq_handler = 0;
    host_log_count = 0U;
    host_transfer_count = 0U;
    host_invalid_device = 0U;
    host_transfer_fault = 0U;
    host_interrupt_leave_active = 0U;
    host_queued_count = 0U;
}

static pci_device_t valid_pci(void) {
    pci_device_t device = {0};

    device.class = USB_CONTROLLER_PCI_CLASS;
    device.subclass = USB_CONTROLLER_PCI_SUBCLASS;
    device.prog_if = USB_CONTROLLER_PROG_IF_UHCI;
    device.irq = 11U;
    device.bus = 0U;
    device.device = 29U;
    device.function = 0U;
    device.bar0 = 0x1001U;
    return device;
}

static int check_invalid_arguments(void) {
    pci_device_t pci = valid_pci();
    pci_device_t invalid_pci = pci;
    usb_uhci_status_t status;
    usb_port_info_t port;
    usb_device_info_t device;
    uint32_t count;
    uint8_t buffer[8];
    uint16_t length;

    if (uhci_init(0, "uhci") != ERR_NULL) return 1;
    if (uhci_init(&pci, 0) != ERR_NULL) return 2;
    invalid_pci.class = 0U;
    if (uhci_init(&invalid_pci, "uhci") != ERR_UNAVAILABLE) return 3;
    invalid_pci = pci;
    invalid_pci.bar0 = 0U;
    if (uhci_init(&invalid_pci, "uhci") != ERR_UNAVAILABLE) return 4;
    if (uhci_get_status(0U, 0U, 0U, 0) != ERR_NULL) return 5;
    if (uhci_get_port_count(0U, 0U, 0U, 0) != ERR_NULL) return 6;
    if (uhci_get_port(0U, 0U, 0U, 0U, 0) != ERR_NULL) return 7;
    if (uhci_get_device_count(0U, 0U, 0U, 0) != ERR_NULL) return 8;
    if (uhci_get_device(0U, 0U, 0U, 0U, 0) != ERR_NULL) return 9;
    if (uhci_get_status(0U, 0U, 0U, &status) != ERR_NOT_FOUND) return 10;
    if (uhci_get_port_count(0U, 0U, 0U, &count) != ERR_NOT_FOUND) return 11;
    if (uhci_get_port(0U, 0U, 0U, 0U, &port) != ERR_NOT_FOUND) return 12;
    if (uhci_get_device_count(0U, 0U, 0U, &count) != ERR_NOT_FOUND) return 13;
    if (uhci_get_device(0U, 0U, 0U, 0U, &device) != ERR_NOT_FOUND) return 14;
    if (uhci_log_port_diagnostics(0U, 0U, 0U) != ERR_NOT_FOUND) return 15;
    if (uhci_validate_state(0U, 0U, 0U) != ERR_NOT_FOUND) return 16;
    if (uhci_control_request(0, 0U, 0U, 0U, 0U, 0U, 0, &length) != ERR_NULL) return 17;
    if (uhci_bulk_transfer(0, 0U, 0U, buffer, sizeof(buffer), &length) != ERR_NULL) return 18;
    if (uhci_reset_bulk_toggles(0) != ERR_NULL) return 19;
    if (uhci_interrupt_submit(0, 0x81U, 8U, 1U, 0, 0) != ERR_NULL) return 20;
    if (uhci_interrupt_cancel(0, 0x81U) != ERR_NULL) return 21;
    if (uhci_poll(0U, 0) != ERR_NULL) return 22;
    return 0;
}

static void interrupt_callback(void* context, int result, const uint8_t* data,
                               uint16_t length) {
    uint32_t* calls = (uint32_t*)context;

    (void)result;
    (void)data;
    (void)length;
    (*calls)++;
}

static int check_controller(void) {
    pci_device_t pci = valid_pci();
    usb_uhci_status_t status;
    usb_port_info_t port;
    usb_device_info_t device;
    uint32_t count;
    uint16_t length;
    uint8_t buffer[64];
    uint32_t callback_calls = 0U;
    uint32_t processed = 0U;

    if (uhci_init(&pci, "uhci-host") != OK) return 1;
    if (!host_irq_handler) return 2;
    {
        registers_t regs = {0};

        regs.int_no = 42U;
        host_irq_handler(&regs);
        regs.int_no = 43U;
        host_irq_handler(&regs);
        regs.int_no = 1U;
        host_irq_handler(&regs);
    }
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        !status.initialized || !status.running || !status.irq_registered ||
        !status.dma_ready || status.port_count != USB_UHCI_PORT_COUNT ||
        status.device_count != 1U || status.port_errors != 0U) return 3;
    if (uhci_get_port_count(pci.bus, pci.device, pci.function, &count) != OK ||
        count != USB_UHCI_PORT_COUNT) return 3;
    if (uhci_get_port(pci.bus, pci.device, pci.function, 0U, &port) != OK ||
        port.state != USB_PORT_CONFIGURED || port.usb_address != 1U) return 4;
    if (uhci_get_port(pci.bus, pci.device, pci.function, 1U, &port) != OK ||
        port.state != USB_PORT_EMPTY) return 5;
    if (uhci_get_port(pci.bus, pci.device, pci.function, 2U, &port) != ERR_INVALID) return 6;
    if (uhci_get_device_count(pci.bus, pci.device, pci.function, &count) != OK ||
        count != 1U) return 7;
    if (uhci_get_device(pci.bus, pci.device, pci.function, 0U, &device) != OK ||
        device.vendor_id != 0x1234U || device.product_id != 0x5678U ||
        device.endpoint_count != 3U || device.bulk_in_endpoint != 0x82U ||
        device.bulk_out_endpoint != 0x02U ||
        device.interrupt_in_endpoint != 0x81U) return 8;
    if (uhci_get_device(pci.bus, pci.device, pci.function, 1U, &device) != ERR_INVALID) return 9;
    if (uhci_validate_state(pci.bus, pci.device, pci.function) != OK) return 10;
    if (uhci_log_port_diagnostics(pci.bus, pci.device, pci.function) != OK) return 11;
    if (host_transfer_count < 5U) return 12;

    kmemset(buffer, 0, sizeof(buffer));
    if (uhci_control_request(&device, 0x80U, HOST_REQUEST_GET_DESCRIPTOR,
                             HOST_DESCRIPTOR_DEVICE << 8U, 0U, 18U, buffer,
                             &length) != OK || length != 18U ||
        buffer[0] != 18U || buffer[1] != HOST_DESCRIPTOR_DEVICE) return 13;
    if (uhci_control_request(&device, 0x80U, HOST_REQUEST_GET_DESCRIPTOR,
                             HOST_DESCRIPTOR_DEVICE << 8U, 0U, 513U, buffer,
                             &length) != ERR_INVALID) return 14;
    if (uhci_bulk_transfer(&device, 0x82U, 1U, buffer, 16U, &length) != OK ||
        length != 16U || buffer[0] != 0xA0U || buffer[15] != 0xAFU) return 15;
    if (uhci_bulk_transfer(&device, 0x02U, 0U, buffer, 16U, &length) != OK ||
        length != 16U) return 16;
    if (uhci_bulk_transfer(&device, 0x82U, 0U, buffer, 16U, &length) != ERR_INVALID) return 17;
    if (uhci_reset_bulk_toggles(&device) != OK) return 18;
    if (uhci_interrupt_submit(&device, 0x81U, 0U, 1U,
                              interrupt_callback, &callback_calls) != ERR_INVALID) return 19;
    if (uhci_interrupt_submit(&device, 0x01U, 8U, 1U,
                              interrupt_callback, &callback_calls) != ERR_INVALID) return 20;
    if (uhci_interrupt_submit(&device, 0x81U, 8U, 0U,
                              interrupt_callback, &callback_calls) != ERR_INVALID) return 21;
    if (uhci_interrupt_submit(&device, 0x81U, 8U, 255U,
                              interrupt_callback, &callback_calls) != OK) return 22;
    if (uhci_interrupt_submit(&device, 0x84U, 8U, 254U,
                              interrupt_callback, &callback_calls) != OK) return 23;
    if (uhci_interrupt_submit(&device, 0x85U, 8U, 1U,
                              interrupt_callback, &callback_calls) != ERR_OVERFLOW) return 24;
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.interrupt_request_count != 2U || status.interrupt_active_count != 2U) return 25;
    if (uhci_poll(0U, &processed) != OK || processed != 0U) return 26;
    if (irq_deferred_dispatch(2U, &processed) != OK || processed != 2U ||
        callback_calls != 2U) return 27;
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.interrupt_transfer_count != 2U ||
        status.interrupt_active_count != 2U) return 28;
    if (uhci_interrupt_cancel(&device, 0x84U) != OK) return 29;
    host_interrupt_leave_active = 1U;
    host_ticks = 5000U;
    if (uhci_poll(0U, &processed) != OK || processed != 0U) return 30;
    if (irq_deferred_dispatch(1U, &processed) != OK || processed != 1U ||
        callback_calls != 3U) return 31;
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.interrupt_timeout_count != 1U) return 32;
    if (uhci_interrupt_cancel(&device, 0x81U) != OK ||
        uhci_interrupt_cancel(&device, 0x84U) != ERR_NOT_FOUND) return 33;
    host_interrupt_leave_active = 0U;
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.interrupt_request_count != 0U || status.interrupt_active_count != 0U) return 34;
    if (uhci_validate_state(pci.bus, pci.device, pci.function) != OK) return 35;
    host_transfer_fault = 1U;
    if (uhci_control_request(&device, 0x00U, 0U, 0U, 0U, 0U, 0, &length) != ERR_STATE) return 36;
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        !status.running || status.recovery_count == 0U) return 37;
    host_transfer_fault = 0U;
    uhci_host_reset();
    for (uint32_t index = 0U; index < HOST_PAGE_COUNT; index++) {
        if (host_pages_used[index]) return 38;
    }
    return 0;
}

static int check_invalid_enumeration(void) {
    pci_device_t pci = valid_pci();
    usb_uhci_status_t status;
    usb_port_info_t port;

    reset_fixture();
    host_invalid_device = 1U;
    if (uhci_init(&pci, "uhci-invalid") != OK) return 1;
    if (uhci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.device_count != 0U || status.port_errors != 1U ||
        status.last_error != ERR_INVALID) return 2;
    if (uhci_get_port(pci.bus, pci.device, pci.function, 0U, &port) != OK ||
        port.state != USB_PORT_DEGRADED ||
        port.reason != USB_PORT_REASON_INVALID_DESCRIPTOR) return 3;
    if (uhci_log_port_diagnostics(pci.bus, pci.device, pci.function) != OK) return 4;
    if (uhci_validate_state(pci.bus, pci.device, pci.function) != OK) return 5;
    uhci_host_reset();
    for (uint32_t index = 0U; index < HOST_PAGE_COUNT; index++) {
        if (host_pages_used[index]) return 6;
    }
    return 0;
}

int main(void) {
    int result;

    coverage_active = 1U;
    reset_fixture();
    result = check_invalid_arguments();
    if (result == 0) {
        result = check_controller();
    }
    if (result == 0) result = check_invalid_enumeration();
    coverage_active = 0U;
    printf("UHCI_HOST_RESULT=%d logs=%u transfers=%u irq=%u\n", result,
           host_log_count, host_transfer_count, host_irq_handler_count);
    coverage_emit(result);
    return result;
}
