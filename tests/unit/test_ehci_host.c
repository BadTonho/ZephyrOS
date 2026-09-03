#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/ehci.h"
#include "drivers/idt.h"
#include "drivers/pci.h"
#include "memory/paging.h"

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_MMIO_SIZE 0x1000U
#define HOST_DMA_BASE 0x00200000U
#define HOST_MMIO_BASE 0xE0000000U
#define HOST_CAP_LENGTH 0x20U
#define HOST_USBCMD (HOST_CAP_LENGTH + 0x00U)
#define HOST_USBSTS (HOST_CAP_LENGTH + 0x04U)
#define HOST_USBINTR (HOST_CAP_LENGTH + 0x08U)
#define HOST_PORT0 (HOST_CAP_LENGTH + 0x44U)
#define HOST_PORT1 (HOST_PORT0 + 0x04U)
#define HOST_USBCMD_RUN_STOP (1U << 0U)
#define HOST_USBCMD_HC_RESET (1U << 1U)
#define HOST_USBSTS_USBINT (1U << 0U)
#define HOST_USBSTS_HSE (1U << 4U)
#define HOST_USBSTS_HCH (1U << 12U)
#define HOST_USBSTS_RELEVANT (0x17U)
#define HOST_PORT_CCS (1U << 0U)
#define HOST_PORT_PED (1U << 2U)
#define HOST_PORT_PR (1U << 8U)
#define HOST_PORT_RWC ((1U << 1U) | (1U << 3U) | (1U << 4U))
#define HOST_QTD_ACTIVE (1U << 7U)
#define HOST_QTD_HALTED (1U << 6U)
#define HOST_QTD_BYTES_SHIFT 16U
#define HOST_QTD_BYTES_MASK 0x7FFFU
#define HOST_QTD_PID_IN (1U << 8U)
#define HOST_REQUEST_GET_DESCRIPTOR 6U
#define HOST_DESCRIPTOR_DEVICE 1U
#define HOST_DESCRIPTOR_CONFIGURATION 2U
#define HOST_CONTROL_DATA_OFFSET 4096U
#define HOST_INTERRUPT_REQUEST_INDEX 15U
#define HOST_PAGE_COUNT EHCI_DMA_PAGE_COUNT
#define HOST_CONFIG_LENGTH 39U

typedef struct {
    uint32_t next;
    uint32_t alternate_next;
    uint32_t token;
    uint32_t buffer[5];
} host_qtd_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_mmio[HOST_MMIO_SIZE] __attribute__((aligned(4)));
static uint8_t host_dma[HOST_PAGE_COUNT][PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));
static uint8_t host_dma_used;
static uint32_t host_ticks;
static uint32_t host_map_count;
static uint32_t host_log_count;
static uint32_t host_transfer_count;
static uint32_t host_irq_count;
static uint8_t host_invalid_device;
static uint8_t host_transfer_fault;
static uint8_t host_transfer_leave_active;
static uint8_t host_interrupt_leave_active;
static isr_handler_t host_irq_handler;

void ehci_host_reset(void);

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
    printf("ZCOV_BEGIN|case=host:drivers:ehci|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:ehci|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:ehci|value=0x%08X\n",
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

int pci_enable_memory_and_bus_mastering(const pci_device_t* device) {
    return device ? OK : ERR_NULL;
}

int idt_register_shared_irq_handler(uint8_t irq_line, isr_handler_t handler) {
    if (!handler || irq_line > 15U) return ERR_INVALID;
    host_irq_handler = handler;
    host_irq_count++;
    return OK;
}

int paging_map_page(uint32_t virtual_address, uint32_t physical_address,
                    uint32_t flags) {
    if (!virtual_address || !physical_address || !flags) return ERR_INVALID;
    host_map_count++;
    return OK;
}

void* pmm_alloc_pages_in_zone(uint32_t count, memory_zone_t zone) {
    if (count != HOST_PAGE_COUNT || zone != MEMORY_ZONE_BUFFER ||
        host_dma_used) return 0;
    host_dma_used = 1U;
    kmemset(host_dma, 0, sizeof(host_dma));
    return host_dma;
}

void pmm_free_pages(void* address, uint32_t count) {
    if (address == host_dma && count == HOST_PAGE_COUNT) host_dma_used = 0U;
}

static void host_mmio_set32(uint32_t offset, uint32_t value) {
    if (offset + sizeof(uint32_t) > sizeof(host_mmio)) return;
    *(uint32_t*)(host_mmio + offset) = value;
}

static uint32_t host_mmio_get32(uint32_t offset) {
    if (offset + sizeof(uint32_t) > sizeof(host_mmio)) return 0xFFFFFFFFU;
    return *(uint32_t*)(host_mmio + offset);
}

volatile uint8_t* ehci_host_mmio(uint32_t base) {
    return base == HOST_MMIO_BASE ? host_mmio : 0;
}

uint32_t ehci_host_mmio_read(volatile uint8_t* mmio, uint32_t offset) {
    if (mmio != host_mmio) return 0xFFFFFFFFU;
    return host_mmio_get32(offset);
}

void ehci_host_mmio_write(volatile uint8_t* mmio, uint32_t offset,
                          uint32_t value) {
    uint32_t current;

    if (mmio != host_mmio || offset + sizeof(uint32_t) > sizeof(host_mmio)) {
        return;
    }
    if (offset == HOST_USBCMD) {
        if (value & HOST_USBCMD_HC_RESET) {
            host_mmio_set32(offset, 0U);
            host_mmio_set32(HOST_USBSTS, HOST_USBSTS_HCH);
            return;
        }
        host_mmio_set32(offset, value);
        current = host_mmio_get32(HOST_USBSTS);
        if (value & HOST_USBCMD_RUN_STOP) current &= ~HOST_USBSTS_HCH;
        else current |= HOST_USBSTS_HCH;
        host_mmio_set32(HOST_USBSTS, current);
        return;
    }
    if (offset == HOST_USBSTS) {
        current = host_mmio_get32(offset);
        current &= ~(value & HOST_USBSTS_RELEVANT);
        host_mmio_set32(offset, current);
        return;
    }
    if (offset == HOST_PORT0 || offset == HOST_PORT1) {
        current = host_mmio_get32(offset);
        if (value & HOST_PORT_PR) current = (current | HOST_PORT_CCS |
                                               HOST_PORT_PED) & ~HOST_PORT_PR;
        else current = (current & ~(value & HOST_PORT_RWC)) |
                       (current & (HOST_PORT_CCS | HOST_PORT_PED));
        host_mmio_set32(offset, current);
        return;
    }
    host_mmio_set32(offset, value);
}

uint32_t ehci_host_physical_address(const void* address) {
    const uint8_t* base = host_dma[0];
    const uint8_t* bytes = (const uint8_t*)address;

    if (bytes < base || bytes >= base + sizeof(host_dma)) return 0U;
    return HOST_DMA_BASE + (uint32_t)(bytes - base);
}

static void host_copy(uint8_t* destination, const uint8_t* source,
                      uint32_t length) {
    for (uint32_t index = 0U; index < length; index++) {
        destination[index] = source[index];
    }
}

static uint32_t host_descriptor(const uint8_t* setup, uint8_t* payload,
                               uint32_t length) {
    static const uint8_t device_descriptor[EHCI_DEVICE_DESCRIPTOR_LENGTH] = {
        18U, 1U, 0x00U, 0x02U, 0U, 0U, 0U, 64U,
        0x34U, 0x12U, 0x78U, 0x56U, 0x01U, 0x00U, 1U, 2U, 1U, 1U
    };
    static const uint8_t configuration_descriptor[HOST_CONFIG_LENGTH] = {
        9U, 2U, HOST_CONFIG_LENGTH, 0U, 1U, 1U, 0U, 0x80U, 50U,
        9U, 4U, 0U, 0U, 3U, 8U, 6U, 80U, 0U,
        7U, 5U, 0x81U, 3U, 8U, 0U, 1U,
        7U, 5U, 0x01U, 2U, 64U, 0U, 1U,
        7U, 5U, 0x82U, 2U, 64U, 0U, 1U
    };
    const uint8_t* source = 0;
    uint32_t source_length = 0U;

    if (setup[1] != HOST_REQUEST_GET_DESCRIPTOR) return 0U;
    if (setup[2] != 0U) return 0U;
    if (setup[3] == HOST_DESCRIPTOR_DEVICE) {
        source = device_descriptor;
        source_length = sizeof(device_descriptor);
    } else if (setup[3] == HOST_DESCRIPTOR_CONFIGURATION) {
        source = configuration_descriptor;
        source_length = sizeof(configuration_descriptor);
    }
    if (!source) return 0U;
    if (host_invalid_device && setup[3] == HOST_DESCRIPTOR_DEVICE) {
        static const uint8_t invalid_descriptor[EHCI_DEVICE_DESCRIPTOR_LENGTH] = {
            18U, 0U, 0U, 0U, 0U, 0U, 0U, 64U,
            0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U
        };
        source = invalid_descriptor;
    }
    if (length > source_length) length = source_length;
    host_copy(payload, source, length);
    return length;
}

void ehci_host_complete_transfer(void* pool, uint32_t qtd_count,
                                 uint8_t* buffer_pool) {
    host_qtd_t* qtd_pool = (host_qtd_t*)pool;
    uint8_t* setup = buffer_pool;
    uint32_t requested = (uint32_t)setup[6] | ((uint32_t)setup[7] << 8U);
    uint32_t pid = qtd_count ? qtd_pool[0].token & (3U << 8U) : 0U;

    host_transfer_count++;
    if (host_transfer_leave_active) return;
    if (qtd_count && pid == (2U << 8U) && requested && (setup[0] & 0x80U)) {
        (void)host_descriptor(setup, buffer_pool + HOST_CONTROL_DATA_OFFSET,
                              requested);
    }
    if (qtd_count && pid == HOST_QTD_PID_IN) {
        for (uint32_t index = 0U; index < 16U; index++) {
            buffer_pool[index] = (uint8_t)(0xA0U + index);
        }
    }
    for (uint32_t index = 0U; index < qtd_count; index++) {
        qtd_pool[index].token &= ~HOST_QTD_ACTIVE;
        qtd_pool[index].token &= ~(HOST_QTD_BYTES_MASK << HOST_QTD_BYTES_SHIFT);
    }
    if (host_transfer_fault && qtd_count) {
        qtd_pool[0].token |= HOST_QTD_HALTED;
        host_transfer_fault = 0U;
    }
}

void ehci_host_complete_interrupt(void* pool, uint32_t qtd_index,
                                  uint16_t max_packet) {
    host_qtd_t* qtd_pool = (host_qtd_t*)pool;

    (void)max_packet;
    if (host_interrupt_leave_active) return;
    qtd_pool[qtd_index].token &= ~HOST_QTD_ACTIVE;
    qtd_pool[qtd_index].token &= ~(HOST_QTD_BYTES_MASK << HOST_QTD_BYTES_SHIFT);
}

static void reset_fixture(void) {
    ehci_host_reset();
    kmemset(host_mmio, 0, sizeof(host_mmio));
    host_mmio_set32(0U, HOST_CAP_LENGTH);
    host_mmio_set32(4U, 2U);
    host_mmio_set32(HOST_USBSTS, HOST_USBSTS_HCH);
    host_mmio_set32(HOST_PORT0, HOST_PORT_CCS);
    host_mmio_set32(HOST_PORT1, 0U);
    host_dma_used = 0U;
    host_ticks = 0U;
    host_map_count = 0U;
    host_transfer_count = 0U;
    host_irq_count = 0U;
    host_log_count = 0U;
    host_invalid_device = 0U;
    host_transfer_fault = 0U;
    host_transfer_leave_active = 0U;
    host_interrupt_leave_active = 0U;
    host_irq_handler = 0;
}

static pci_device_t valid_pci(void) {
    pci_device_t device = {0};

    device.class = USB_CONTROLLER_PCI_CLASS;
    device.subclass = USB_CONTROLLER_PCI_SUBCLASS;
    device.prog_if = USB_CONTROLLER_PROG_IF_EHCI;
    device.irq = 11U;
    device.bus = 0U;
    device.device = 20U;
    device.function = 0U;
    device.bar0 = HOST_MMIO_BASE;
    return device;
}

static int check_invalid_arguments(void) {
    pci_device_t pci = valid_pci();
    pci_device_t invalid = pci;
    usb_ehci_status_t status;
    usb_port_info_t port;
    usb_device_info_t device;
    uint32_t count;
    uint8_t buffer[32];
    uint16_t length;

    if (ehci_init(0, "ehci") != ERR_NULL) return 1;
    if (ehci_init(&pci, 0) != ERR_NULL) return 2;
    invalid.class = 0U;
    if (ehci_init(&invalid, "ehci") != ERR_UNAVAILABLE) return 3;
    invalid = pci;
    invalid.bar0 |= 1U;
    if (ehci_init(&invalid, "ehci") != ERR_UNAVAILABLE) return 4;
    invalid = pci;
    invalid.irq = USB_CONTROLLER_IRQ_UNKNOWN;
    if (ehci_init(&invalid, "ehci") != ERR_UNAVAILABLE) return 5;
    if (ehci_get_status(0U, 0U, 0U, 0) != ERR_NULL) return 6;
    if (ehci_get_port_count(0U, 0U, 0U, 0) != ERR_NULL) return 7;
    if (ehci_get_port(0U, 0U, 0U, 0U, 0) != ERR_NULL) return 8;
    if (ehci_get_device_count(0U, 0U, 0U, 0) != ERR_NULL) return 9;
    if (ehci_get_device(0U, 0U, 0U, 0U, 0) != ERR_NULL) return 10;
    if (ehci_get_status(0U, 0U, 0U, &status) != ERR_NOT_FOUND) return 11;
    if (ehci_get_port_count(0U, 0U, 0U, &count) != ERR_NOT_FOUND) return 12;
    if (ehci_get_port(0U, 0U, 0U, 0U, &port) != ERR_NOT_FOUND) return 13;
    if (ehci_get_device_count(0U, 0U, 0U, &count) != ERR_NOT_FOUND) return 14;
    if (ehci_get_device(0U, 0U, 0U, 0U, &device) != ERR_NOT_FOUND) return 15;
    if (ehci_validate_state(0U, 0U, 0U) != ERR_NOT_FOUND) return 16;
    if (ehci_control_request(0, 0U, 0U, 0U, 0U, 0U, buffer, &length) !=
        ERR_NULL) return 17;
    if (ehci_bulk_transfer(0, 0U, 0U, buffer, sizeof(buffer), &length) !=
        ERR_NULL) return 18;
    if (ehci_reset_bulk_toggles(0) != ERR_NULL) return 19;
    if (ehci_interrupt_submit(0, 0x81U, 8U, 1U, 0, 0) != ERR_INVALID) return 20;
    if (ehci_interrupt_cancel(0, 0x81U) != ERR_NULL) return 21;
    if (ehci_poll(0U, 0) != ERR_NULL) return 22;
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
    static const char controller_id[USB_CONTROLLER_ID_SIZE] = "ehci-host";
    pci_device_t pci = valid_pci();
    usb_ehci_status_t status;
    usb_port_info_t port;
    usb_device_info_t device;
    uint32_t count;
    uint32_t processed;
    uint32_t callback_calls = 0U;
    uint16_t length;
    uint8_t buffer[128];

    if (ehci_init(&pci, controller_id) != OK) return 1;
    if (ehci_init(&pci, controller_id) != OK) return 2;
    if (!host_irq_handler || host_irq_count != 1U || host_map_count != 1U) {
        return 3;
    }
    {
        registers_t regs = {0};

        regs.int_no = 43U;
        host_mmio_set32(HOST_USBSTS, HOST_USBSTS_USBINT);
        host_irq_handler(&regs);
        regs.int_no = 44U;
        host_irq_handler(&regs);
        regs.int_no = 1U;
        host_irq_handler(&regs);
    }
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        !status.initialized || !status.running || !status.irq_registered ||
        !status.dma_ready || status.port_count != 2U ||
        status.device_count != 1U || status.port_errors != 0U) return 4;
    if (ehci_get_port_count(pci.bus, pci.device, pci.function, &count) != OK ||
        count != 2U) return 5;
    if (ehci_get_port(pci.bus, pci.device, pci.function, 0U, &port) != OK ||
        port.state != USB_PORT_CONFIGURED || port.usb_address != 1U) return 6;
    if (ehci_get_port(pci.bus, pci.device, pci.function, 1U, &port) != OK ||
        port.state != USB_PORT_EMPTY) return 7;
    if (ehci_get_port(pci.bus, pci.device, pci.function, 2U, &port) !=
        ERR_INVALID) return 8;
    if (ehci_get_device_count(pci.bus, pci.device, pci.function, &count) != OK ||
        count != 1U) return 9;
    if (ehci_get_device(pci.bus, pci.device, pci.function, 0U, &device) != OK ||
        device.vendor_id != 0x1234U || device.product_id != 0x5678U ||
        device.endpoint_count != 3U || device.bulk_in_endpoint != 0x82U ||
        device.bulk_out_endpoint != 0x01U ||
        device.interrupt_in_endpoint != 0x81U) return 10;
    if (ehci_get_device(pci.bus, pci.device, pci.function, 1U, &device) !=
        ERR_INVALID) return 11;
    if (ehci_validate_state(pci.bus, pci.device, pci.function) != OK) return 12;
    if (ehci_poll(0U, &processed) != OK || processed != 0U) return 13;
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK) {
        return 14;
    }
    if (status.irq_pending != 1U || status.irq_events != 1U) return 14;

    kmemset(buffer, 0, sizeof(buffer));
    if (ehci_control_request(&device, 0x80U, HOST_REQUEST_GET_DESCRIPTOR,
                             HOST_DESCRIPTOR_DEVICE << 8U, 0U, 18U, buffer,
                             &length) != OK || length != 18U ||
        buffer[0] != 18U || buffer[1] != HOST_DESCRIPTOR_DEVICE) return 15;
    if (ehci_control_request(&device, 0x80U, HOST_REQUEST_GET_DESCRIPTOR,
                             HOST_DESCRIPTOR_DEVICE << 8U, 0U, 16385U, buffer,
                             &length) != ERR_INVALID) return 16;
    if (ehci_control_request(&device, 0x80U, HOST_REQUEST_GET_DESCRIPTOR,
                             HOST_DESCRIPTOR_DEVICE << 8U, 0U, 1U, 0,
                             &length) != ERR_INVALID) return 17;
    if (ehci_bulk_transfer(&device, 0x82U, 1U, buffer, 16U, &length) != OK ||
        length != 16U || buffer[0] != 0xA0U || buffer[15] != 0xAFU) return 18;
    if (ehci_bulk_transfer(&device, 0x01U, 0U, buffer, 16U, &length) != OK ||
        length != 16U) return 19;
    if (ehci_bulk_transfer(&device, 0x82U, 0U, buffer, 16U, &length) !=
        ERR_INVALID) return 20;
    if (ehci_reset_bulk_toggles(&device) != OK) return 21;
    if (ehci_interrupt_submit(&device, 0x81U, 0U, 1U,
                              interrupt_callback, &callback_calls) !=
        ERR_INVALID) return 22;
    if (ehci_interrupt_submit(&device, 0x01U, 8U, 1U,
                              interrupt_callback, &callback_calls) !=
        ERR_INVALID) return 23;
    if (ehci_interrupt_submit(&device, 0x81U, EHCI_INTERRUPT_BUFFER_SIZE + 1U,
                              1U, interrupt_callback, &callback_calls) !=
        ERR_INVALID) return 24;
    if (ehci_interrupt_submit(&device, 0x81U, 8U, 1U,
                              interrupt_callback, &callback_calls) != OK) {
        return 25;
    }
    if (ehci_interrupt_submit(&device, 0x82U, 8U, 1U,
                              interrupt_callback, &callback_calls) !=
        ERR_OVERFLOW) return 26;
    if (ehci_poll(1U, &processed) != OK || processed != 1U ||
        callback_calls != 1U) return 27;
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.interrupt_transfer_count != 1U) return 28;
    if (ehci_interrupt_cancel(&device, 0x81U) != OK) return 29;
    if (ehci_interrupt_cancel(&device, 0x81U) != ERR_NOT_FOUND) return 30;
    if (ehci_interrupt_submit(&device, 0x81U, 8U, 1U,
                              interrupt_callback, &callback_calls) != OK) {
        return 31;
    }
    host_interrupt_leave_active = 1U;
    host_ticks = 100000U;
    if (ehci_poll(1U, &processed) != OK || processed != 1U ||
        callback_calls != 2U) return 32;
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.interrupt_timeout_count != 1U ||
        ehci_interrupt_cancel(&device, 0x81U) != ERR_NOT_FOUND) return 33;
    host_interrupt_leave_active = 0U;
    host_transfer_fault = 1U;
    if (ehci_control_request(&device, 0x00U, 0U, 0U, 0U, 0U, 0,
                             &length) != ERR_STATE) return 34;
    host_transfer_leave_active = 1U;
    host_ticks = 100000U;
    if (ehci_control_request(&device, 0x00U, 0U, 0U, 0U, 0U, 0,
                             &length) != ERR_TIMEOUT) return 35;
    host_transfer_leave_active = 0U;
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        !status.running || status.recovery_count == 0U) return 36;
    if (ehci_validate_state(pci.bus, pci.device, pci.function) != OK) return 37;
    ehci_host_reset();
    if (host_dma_used) return 38;
    return 0;
}

static int check_invalid_enumeration(void) {
    static const char controller_id[USB_CONTROLLER_ID_SIZE] = "ehci-invalid";
    pci_device_t pci = valid_pci();
    usb_ehci_status_t status;
    usb_port_info_t port;

    reset_fixture();
    host_invalid_device = 1U;
    if (ehci_init(&pci, controller_id) != OK) return 1;
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.device_count != 0U || status.port_errors != 1U ||
        status.last_error != ERR_INVALID) return 2;
    if (ehci_get_port(pci.bus, pci.device, pci.function, 0U, &port) != OK ||
        port.state != USB_PORT_DEGRADED ||
        port.reason != USB_PORT_REASON_INVALID_DESCRIPTOR) return 3;
    if (ehci_validate_state(pci.bus, pci.device, pci.function) != OK) return 4;
    ehci_host_reset();
    if (host_dma_used) return 5;
    return 0;
}

static int check_hardware_failure(void) {
    static const char controller_id[USB_CONTROLLER_ID_SIZE] = "ehci-hse";
    pci_device_t pci = valid_pci();
    usb_ehci_status_t status;
    uint32_t processed;

    reset_fixture();
    if (ehci_init(&pci, controller_id) != OK) return 1;
    host_mmio_set32(HOST_USBSTS, HOST_USBSTS_HSE);
    if (ehci_poll(1U, &processed) != OK || processed != 1U) return 2;
    if (ehci_get_status(pci.bus, pci.device, pci.function, &status) != OK ||
        status.running || status.last_error != ERR_UNAVAILABLE) return 3;
    ehci_host_reset();
    if (host_dma_used) return 4;
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
    if (result == 0) result = check_invalid_enumeration();
    if (result == 0) result = check_hardware_failure();
    coverage_active = 0U;
    printf("EHCI_HOST_RESULT=%d logs=%u transfers=%u irq=%u\n", result,
           host_log_count, host_transfer_count, host_irq_count);
    coverage_emit(result);
    return result;
}
