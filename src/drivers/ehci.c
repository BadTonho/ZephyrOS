#include "drivers/ehci.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/spinlock.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/idt.h"
#include "memory/paging.h"

#define EHCI_CONTROLLER_CAPACITY USB_MANAGER_MAX_CONTROLLERS
#define EHCI_DESCRIPTOR_DEVICE 1U
#define EHCI_DESCRIPTOR_CONFIGURATION 2U
#define EHCI_DESCRIPTOR_TYPE_CONFIGURATION 2U
#define EHCI_DESCRIPTOR_TYPE_INTERFACE 4U
#define EHCI_DESCRIPTOR_TYPE_ENDPOINT 5U
#define EHCI_REQUEST_GET_DESCRIPTOR 6U
#define EHCI_REQUEST_SET_ADDRESS 5U
#define EHCI_REQUEST_SET_CONFIGURATION 9U
#define EHCI_REQUEST_DEVICE_TO_HOST 0x80U
#define EHCI_REQUEST_HOST_TO_DEVICE 0x00U
#define EHCI_REQUEST_STANDARD 0x00U
#define EHCI_REQUEST_RECIPIENT_DEVICE 0x00U
#define EHCI_PORT_CCS (1U << 0U)
#define EHCI_PORT_CSC (1U << 1U)
#define EHCI_PORT_PED (1U << 2U)
#define EHCI_PORT_PEC (1U << 3U)
#define EHCI_PORT_OCA (1U << 4U)
#define EHCI_PORT_PR (1U << 8U)
#define EHCI_PORT_PP (1U << 12U)
#define EHCI_PORT_RWC (EHCI_PORT_CSC | EHCI_PORT_PEC | EHCI_PORT_OCA)
#define EHCI_USBCMD 0x00U
#define EHCI_USBSTS 0x04U
#define EHCI_USBINTR 0x08U
#define EHCI_FRINDEX 0x0CU
#define EHCI_CTRLDSSEGMENT 0x10U
#define EHCI_PERIODICLISTBASE 0x14U
#define EHCI_ASYNCLISTADDR 0x18U
#define EHCI_CONFIGFLAG 0x40U
#define EHCI_PORTSC_BASE 0x44U
#define EHCI_USBCMD_RUN_STOP (1U << 0U)
#define EHCI_USBCMD_HC_RESET (1U << 1U)
#define EHCI_USBCMD_ASYNC_ENABLE (1U << 5U)
#define EHCI_USBSTS_USBINT (1U << 0U)
#define EHCI_USBSTS_USBERRINT (1U << 1U)
#define EHCI_USBSTS_PORT_CHANGE (1U << 2U)
#define EHCI_USBSTS_HSE (1U << 4U)
#define EHCI_USBSTS_HCH (1U << 12U)
#define EHCI_USBSTS_RELEVANT (EHCI_USBSTS_USBINT | EHCI_USBSTS_USBERRINT | \
                               EHCI_USBSTS_PORT_CHANGE | EHCI_USBSTS_HSE)
#define EHCI_USBINTR_DEFAULT (EHCI_USBSTS_USBINT | EHCI_USBSTS_USBERRINT | \
                              EHCI_USBSTS_PORT_CHANGE)
#define EHCI_PTR_TERM 0x00000001U
#define EHCI_PTR_QH 0x00000002U
#define EHCI_QTD_STATUS_ACTIVE (1U << 7U)
#define EHCI_QTD_STATUS_HALTED (1U << 6U)
#define EHCI_QTD_STATUS_BUFFER_ERROR (1U << 5U)
#define EHCI_QTD_STATUS_BABBLE (1U << 4U)
#define EHCI_QTD_STATUS_TRANSACTION_ERROR (1U << 3U)
#define EHCI_QTD_STATUS_MISSED_UFRAME (1U << 2U)
#define EHCI_QTD_STATUS_ERROR_MASK (EHCI_QTD_STATUS_HALTED | \
                                    EHCI_QTD_STATUS_BUFFER_ERROR | \
                                    EHCI_QTD_STATUS_BABBLE | \
                                    EHCI_QTD_STATUS_TRANSACTION_ERROR | \
                                    EHCI_QTD_STATUS_MISSED_UFRAME)
#define EHCI_QTD_PID_OUT (0U << 8U)
#define EHCI_QTD_PID_IN (1U << 8U)
#define EHCI_QTD_PID_SETUP (2U << 8U)
#define EHCI_QTD_CERR 3U
#define EHCI_QTD_CERR_SHIFT 10U
#define EHCI_QTD_BYTES_SHIFT 16U
#define EHCI_QTD_BYTES_MASK 0x7FFFU
#define EHCI_QTD_TOGGLE (1U << 31U)
#define EHCI_QH_SPEED_FULL 0U
#define EHCI_QH_SPEED_LOW 1U
#define EHCI_QH_SPEED_HIGH 2U
#define EHCI_QH_DTC (1U << 14U)
#define EHCI_QH_MAX_PACKET_SHIFT 16U
#define EHCI_QH_CONTROL_ENDPOINT (1U << 27U)
#define EHCI_QH_MULT 1U
#define EHCI_QH_LINK_TYPE_SHIFT 1U
#define EHCI_QH_LINK(qh) ((uint32_t)(qh) | EHCI_PTR_QH)
#define EHCI_QTD_CHUNK 4096U
#define EHCI_CONTROL_SETUP_OFFSET 0U
#define EHCI_CONTROL_DATA_OFFSET 4096U
#define EHCI_INTERRUPT_BUFFER_OFFSET (EHCI_CONTROL_DATA_OFFSET + \
                                      EHCI_SYNC_BUFFER_SIZE)
#define EHCI_DEVICE_VENDOR_OFFSET 8U
#define EHCI_DEVICE_PRODUCT_OFFSET 10U
#define EHCI_DEVICE_REVISION_OFFSET 12U
#define EHCI_DEVICE_CLASS_OFFSET 4U
#define EHCI_DEVICE_SUBCLASS_OFFSET 5U
#define EHCI_DEVICE_PROTOCOL_OFFSET 6U
#define EHCI_DEVICE_MPS_OFFSET 7U
#define EHCI_DEVICE_CONFIG_COUNT_OFFSET 17U
#define EHCI_CONFIG_TOTAL_LENGTH_OFFSET 2U
#define EHCI_CONFIG_VALUE_OFFSET 5U
#define EHCI_INTERFACE_NUMBER_OFFSET 2U
#define EHCI_INTERFACE_ALTERNATE_OFFSET 3U
#define EHCI_INTERFACE_CLASS_OFFSET 5U
#define EHCI_INTERFACE_SUBCLASS_OFFSET 6U
#define EHCI_INTERFACE_PROTOCOL_OFFSET 7U
#define EHCI_ENDPOINT_ADDRESS_OFFSET 2U
#define EHCI_ENDPOINT_MAX_PACKET_OFFSET 4U
#define EHCI_ENDPOINT_INTERVAL_OFFSET 6U
#define EHCI_INTERFACE_LENGTH 9U
#define EHCI_ENDPOINT_LENGTH 7U
#define EHCI_CONFIGURATION_LENGTH 9U
#define EHCI_MAX_USB_ADDRESS 127U
#define EHCI_SETUP_PACKET_SIZE 8U
#define EHCI_CONTROL_PACKET_SIZE_MIN 8U
#define EHCI_CONTROL_PACKET_SIZE_MAX 64U

typedef struct {
    volatile uint32_t next;
    volatile uint32_t alternate_next;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
} ehci_qtd_t __attribute__((aligned(32)));

typedef struct {
    volatile uint32_t horizontal_link;
    volatile uint32_t endpoint_characteristics;
    volatile uint32_t endpoint_capabilities;
    volatile uint32_t current_qtd;
    volatile uint32_t next_qtd;
    volatile uint32_t alternate_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t reserved[4];
} ehci_qh_t __attribute__((aligned(32)));

typedef struct {
    usb_device_info_t info;
    uint8_t allocated;
    uint8_t valid_descriptors;
    uint8_t device_descriptor[EHCI_DEVICE_DESCRIPTOR_LENGTH];
    uint8_t configuration_descriptor[EHCI_DESCRIPTOR_BUFFER_SIZE];
    uint16_t configuration_length;
    uint8_t bulk_in_toggle;
    uint8_t bulk_out_toggle;
} ehci_device_record_t;

typedef struct {
    uint8_t used;
    uint8_t active;
    uint8_t endpoint_address;
    uint16_t max_packet;
    uint8_t interval;
    uint8_t toggle;
    uint32_t deadline;
    int result;
    uint16_t actual_length;
    ehci_device_record_t* record;
    usb_interrupt_callback_t callback;
    void* context;
} ehci_interrupt_request_t;

typedef struct {
    uint8_t used;
    uint8_t initialized;
    uint8_t running;
    uint8_t irq_registered;
    uint8_t irq_pending;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uint8_t cap_length;
    uint8_t port_count;
    uint8_t device_count;
    uint8_t port_errors;
    uint32_t mmio_base;
    volatile uint8_t* mmio;
    char controller_id[USB_CONTROLLER_ID_SIZE];
    uint8_t* dma_pool;
    uint32_t dma_pool_phys;
    ehci_qh_t* queue_heads;
    ehci_qtd_t* qtd_pool;
    uint8_t* buffer_pool;
    uint32_t qh_pool_phys;
    uint32_t qtd_pool_phys;
    uint32_t buffer_pool_phys;
    uint32_t qtd_in_use;
    uint32_t buffer_in_use;
    uint32_t irq_events;
    uint32_t timeout_count;
    uint32_t recovery_count;
    uint32_t control_transfer_count;
    uint32_t bulk_transfer_count;
    uint32_t interrupt_transfer_count;
    uint32_t interrupt_timeout_count;
    uint32_t interrupt_error_count;
    uint32_t interrupt_cancel_count;
    uint8_t next_address;
    int last_error;
    spinlock_t transfer_lock;
    usb_port_info_t ports[USB_EHCI_PORT_COUNT];
    ehci_device_record_t devices[USB_EHCI_PORT_COUNT];
    ehci_interrupt_request_t interrupt_requests[EHCI_INTERRUPT_REQUEST_CAPACITY];
} ehci_controller_t;

static ehci_controller_t ehci_controllers[EHCI_CONTROLLER_CAPACITY];

static uint32_t ehci_timeout_ticks(uint32_t milliseconds) {
    uint32_t frequency = timer_get_frequency();
    uint32_t whole_seconds;
    uint32_t remainder_ms;
    uint32_t whole_ticks;
    uint32_t fractional_ticks;
    uint32_t frequency_whole;
    uint32_t frequency_remainder;

    if (!frequency) return milliseconds ? milliseconds : 1U;
    whole_seconds = milliseconds / 1000U;
    remainder_ms = milliseconds % 1000U;
    if (whole_seconds && frequency > 0xFFFFFFFFU / whole_seconds) {
        return 0xFFFFFFFFU;
    }
    whole_ticks = frequency * whole_seconds;
    frequency_whole = frequency / 1000U;
    frequency_remainder = frequency % 1000U;
    fractional_ticks = frequency_whole * remainder_ms;
    fractional_ticks += (frequency_remainder * remainder_ms + 999U) /
                        1000U;
    if (whole_ticks > 0xFFFFFFFFU - fractional_ticks) {
        return 0xFFFFFFFFU;
    }
    whole_ticks += fractional_ticks;
    return whole_ticks ? whole_ticks : 1U;
}

static int ehci_deadline_expired(uint32_t start, uint32_t milliseconds) {
    return (uint32_t)(timer_get_ticks() - start) >=
           ehci_timeout_ticks(milliseconds);
}

static uint32_t ehci_read(const ehci_controller_t* controller,
                          uint32_t offset) {
    if (!controller || !controller->mmio) return 0xFFFFFFFFU;
    return *(volatile uint32_t*)(controller->mmio + offset);
}

static void ehci_write(ehci_controller_t* controller, uint32_t offset,
                       uint32_t value) {
    if (!controller || !controller->mmio) return;
    *(volatile uint32_t*)(controller->mmio + offset) = value;
    asm volatile("" : : : "memory");
}

static ehci_controller_t* ehci_find(uint8_t bus, uint8_t device,
                                    uint8_t function) {
    for (uint32_t index = 0U; index < EHCI_CONTROLLER_CAPACITY; index++) {
        ehci_controller_t* controller = &ehci_controllers[index];

        if (controller->used && controller->bus == bus &&
            controller->device == device && controller->function == function) {
            return controller;
        }
    }
    return 0;
}

static ehci_device_record_t* ehci_find_device_record(
    ehci_controller_t* controller, const usb_device_info_t* device) {
    if (!controller || !device || device->controller_bus != controller->bus ||
        device->controller_device != controller->device ||
        device->controller_function != controller->function) return 0;
    for (uint32_t index = 0U; index < controller->port_count; index++) {
        ehci_device_record_t* record = &controller->devices[index];

        if (record->allocated && record->info.port_number == device->port_number &&
            record->info.usb_address == device->usb_address) return record;
    }
    return 0;
}

static ehci_device_record_t* ehci_find_port_record(
    ehci_controller_t* controller, uint32_t port) {
    if (!controller || port >= controller->port_count) return 0;
    return &controller->devices[port];
}

static void ehci_make_device_id(const ehci_controller_t* controller,
                                uint32_t port, uint8_t address,
                                char* output, uint32_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t values[5];
    uint32_t widths[5] = {2U, 2U, 1U, 1U, 1U};
    uint32_t offset = 0U;
    const char* prefix = "usb-dev-";

    if (!controller || !output || !capacity) return;
    values[0] = controller->bus;
    values[1] = controller->device;
    values[2] = controller->function;
    values[3] = port + 1U;
    values[4] = address;
    output[0] = '\0';
    while (*prefix && offset + 1U < capacity) output[offset++] = *prefix++;
    for (uint32_t value = 0U; value < 5U; value++) {
        if (value == 1U) output[offset++] = ':';
        else if (value == 2U) output[offset++] = '.';
        else if (value == 3U) {
            output[offset++] = '-';
            output[offset++] = 'p';
        } else if (value == 4U) {
            output[offset++] = '-';
            output[offset++] = 'a';
        }
        for (uint32_t digit = widths[value]; digit > 0U; digit--) {
            if (offset + 1U >= capacity) break;
            output[offset++] = hex[(values[value] >> ((digit - 1U) * 4U)) & 0xFU];
        }
    }
    output[offset < capacity ? offset : capacity - 1U] = '\0';
}

static int ehci_validate_pci(const pci_device_t* pci, uint32_t* out_base) {
    uint32_t base;

    if (!pci || !out_base) {
        LOG_ERROR("EHCI", "Argumento nulo ao validar PCI EHCI");
        return ERR_NULL;
    }
    if (pci->class != USB_CONTROLLER_PCI_CLASS ||
        pci->subclass != USB_CONTROLLER_PCI_SUBCLASS ||
        pci->prog_if != USB_CONTROLLER_PROG_IF_EHCI ||
        pci->irq == USB_CONTROLLER_IRQ_UNKNOWN || pci->irq > EHCI_IRQ_MAX) {
        LOG_ERROR("EHCI", "Capacidade PCI EHCI invalida");
        return ERR_UNAVAILABLE;
    }
    if (pci->bar0 & 1U) {
        LOG_ERROR("EHCI", "BAR0 EHCI nao e MMIO");
        return ERR_UNAVAILABLE;
    }
    base = pci->bar0 & EHCI_PCI_BAR_ADDRESS_MASK;
    if (!base) {
        LOG_ERROR("EHCI", "BAR0 EHCI ausente");
        return ERR_UNAVAILABLE;
    }
    *out_base = base;
    return OK;
}

static int ehci_map_mmio(ehci_controller_t* controller, uint32_t base) {
    uint32_t start;
    uint32_t end;

    if (!controller || !base) {
        LOG_ERROR("EHCI", "MMIO EHCI invalido");
        return ERR_INVALID;
    }
    start = base & ~(PAGE_SIZE - 1U);
    end = (base + EHCI_MMIO_REQUIRED_BYTES + PAGE_SIZE - 1U) &
          ~(PAGE_SIZE - 1U);
    for (uint32_t address = start; address < end; address += PAGE_SIZE) {
        if (paging_map_page(address, address,
                            PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE) != OK) {
            LOG_ERROR("EHCI", "Falha ao mapear MMIO EHCI");
            return ERR_MEM;
        }
    }
    controller->mmio_base = base;
    controller->mmio = (volatile uint8_t*)base;
    return OK;
}

static int ehci_allocate_dma(ehci_controller_t* controller) {
    if (!controller) return ERR_NULL;
    controller->dma_pool = (uint8_t*)pmm_alloc_pages_in_zone(
        EHCI_DMA_PAGE_COUNT, MEMORY_ZONE_BUFFER);
    if (!controller->dma_pool) {
        LOG_ERROR("EHCI", "Falha ao alocar estruturas DMA EHCI");
        return ERR_MEM;
    }
    controller->dma_pool_phys = (uint32_t)controller->dma_pool;
    controller->queue_heads = (ehci_qh_t*)controller->dma_pool;
    controller->qtd_pool = (ehci_qtd_t*)(controller->dma_pool + PAGE_SIZE);
    controller->buffer_pool = controller->dma_pool + PAGE_SIZE * 2U;
    controller->qh_pool_phys = controller->dma_pool_phys;
    controller->qtd_pool_phys = controller->dma_pool_phys + PAGE_SIZE;
    controller->buffer_pool_phys = controller->dma_pool_phys + PAGE_SIZE * 2U;
    if ((controller->dma_pool_phys & (PAGE_SIZE - 1U)) ||
        (controller->qh_pool_phys & 0x1FU) ||
        (controller->qtd_pool_phys & 0x1FU)) {
        LOG_ERROR("EHCI", "Estruturas DMA EHCI desalinhadas");
        pmm_free_pages(controller->dma_pool, EHCI_DMA_PAGE_COUNT);
        controller->dma_pool = 0;
        return ERR_INVALID;
    }
    kmemset(controller->dma_pool, 0, EHCI_DMA_PAGE_COUNT * PAGE_SIZE);
    controller->queue_heads[0].horizontal_link = EHCI_QH_LINK(
        controller->qh_pool_phys);
    controller->queue_heads[0].next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[0].alternate_next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[1].horizontal_link = EHCI_QH_LINK(
        controller->qh_pool_phys);
    controller->queue_heads[1].next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[1].alternate_next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[2].horizontal_link = EHCI_QH_LINK(
        controller->qh_pool_phys);
    controller->queue_heads[2].next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[2].alternate_next_qtd = EHCI_PTR_TERM;
    return OK;
}

static void ehci_release_dma(ehci_controller_t* controller) {
    if (!controller || !controller->dma_pool) return;
    pmm_free_pages(controller->dma_pool, EHCI_DMA_PAGE_COUNT);
    controller->dma_pool = 0;
    controller->queue_heads = 0;
    controller->qtd_pool = 0;
    controller->buffer_pool = 0;
}

static void ehci_link_schedule(ehci_controller_t* controller) {
    ehci_qh_t* anchor;
    ehci_qh_t* sync;
    ehci_qh_t* interrupt;

    if (!controller || !controller->queue_heads) return;
    anchor = &controller->queue_heads[0];
    sync = &controller->queue_heads[1];
    interrupt = &controller->queue_heads[2];
    anchor->horizontal_link = EHCI_QH_LINK(
        controller->qh_pool_phys + sizeof(ehci_qh_t));
    if (controller->interrupt_requests[0].used) {
        sync->horizontal_link = EHCI_QH_LINK(controller->qh_pool_phys +
                                             sizeof(ehci_qh_t) * 2U);
        interrupt->horizontal_link = EHCI_QH_LINK(controller->qh_pool_phys);
    } else {
        sync->horizontal_link = EHCI_QH_LINK(controller->qh_pool_phys);
    }
}

static void ehci_disable(ehci_controller_t* controller) {
    uint32_t command;

    if (!controller || !controller->mmio ||
        controller->cap_length < EHCI_CAPABILITY_MIN_LENGTH) return;
    ehci_write(controller, controller->cap_length + EHCI_USBINTR, 0U);
    command = ehci_read(controller, controller->cap_length + EHCI_USBCMD);
    command &= ~(EHCI_USBCMD_RUN_STOP | EHCI_USBCMD_ASYNC_ENABLE);
    ehci_write(controller, controller->cap_length + EHCI_USBCMD, command);
}

static int ehci_reset_controller(ehci_controller_t* controller) {
    uint32_t base;
    uint32_t command;
    uint32_t start;

    if (!controller) return ERR_NULL;
    base = controller->cap_length;
    ehci_write(controller, base + EHCI_USBINTR, 0U);
    command = ehci_read(controller, base + EHCI_USBCMD);
    command &= ~(EHCI_USBCMD_RUN_STOP | EHCI_USBCMD_ASYNC_ENABLE);
    ehci_write(controller, base + EHCI_USBCMD, command);
    start = timer_get_ticks();
    while (!(ehci_read(controller, base + EHCI_USBSTS) & EHCI_USBSTS_HCH)) {
        if (ehci_deadline_expired(start, EHCI_RESET_TIMEOUT_MS)) {
            LOG_ERROR("EHCI", "Timeout ao parar controlador EHCI");
            return ERR_TIMEOUT;
        }
        asm volatile("pause");
    }
    ehci_write(controller, base + EHCI_USBCMD, EHCI_USBCMD_HC_RESET);
    start = timer_get_ticks();
    while (ehci_read(controller, base + EHCI_USBCMD) & EHCI_USBCMD_HC_RESET) {
        if (ehci_deadline_expired(start, EHCI_RESET_TIMEOUT_MS)) {
            LOG_ERROR("EHCI", "Timeout no reset do controlador EHCI");
            return ERR_TIMEOUT;
        }
        asm volatile("pause");
    }
    ehci_write(controller, base + EHCI_USBSTS, 0xFFFFFFFFU);
    ehci_write(controller, base + EHCI_CTRLDSSEGMENT, 0U);
    ehci_write(controller, base + EHCI_ASYNCLISTADDR,
               controller->qh_pool_phys);
    ehci_write(controller, base + EHCI_CONFIGFLAG, 1U);
    return OK;
}

static int ehci_start_controller(ehci_controller_t* controller) {
    uint32_t command;
    uint32_t start;

    if (!controller) return ERR_NULL;
    ehci_link_schedule(controller);
    ehci_write(controller, controller->cap_length + EHCI_USBINTR,
               EHCI_USBINTR_DEFAULT);
    command = ehci_read(controller, controller->cap_length + EHCI_USBCMD);
    command |= EHCI_USBCMD_RUN_STOP | EHCI_USBCMD_ASYNC_ENABLE;
    ehci_write(controller, controller->cap_length + EHCI_USBCMD, command);
    start = timer_get_ticks();
    while (ehci_read(controller, controller->cap_length + EHCI_USBSTS) &
           EHCI_USBSTS_HCH) {
        if (ehci_deadline_expired(start, EHCI_RESET_TIMEOUT_MS)) {
            LOG_ERROR("EHCI", "Controlador EHCI permaneceu parado");
            return ERR_UNAVAILABLE;
        }
        asm volatile("pause");
    }
    return OK;
}

static int ehci_recover_controller(ehci_controller_t* controller) {
    int result;

    if (!controller) return ERR_NULL;
    controller->recovery_count++;
    controller->running = 0U;
    ehci_disable(controller);
    result = ehci_reset_controller(controller);
    if (result == OK) result = ehci_start_controller(controller);
    if (result != OK) {
        controller->last_error = result;
        LOG_ERROR("EHCI", "Recuperacao do controlador EHCI falhou");
        return result;
    }
    controller->running = 1U;
    controller->last_error = OK;
    return OK;
}

static void ehci_qh_configure(ehci_controller_t* controller, ehci_qh_t* qh,
                              uint8_t address, uint8_t endpoint,
                              uint16_t max_packet, uint8_t control_endpoint) {
    uint32_t characteristics;

    if (!controller || !qh) return;
    characteristics = (address & 0x7FU) |
                      ((uint32_t)(endpoint & 0x0FU) << 8U) |
                      (EHCI_QH_SPEED_HIGH << 12U) | EHCI_QH_DTC |
                      ((uint32_t)max_packet << EHCI_QH_MAX_PACKET_SHIFT);
    if (control_endpoint) characteristics |= EHCI_QH_CONTROL_ENDPOINT;
    qh->endpoint_characteristics = characteristics;
    qh->endpoint_capabilities = EHCI_QH_MULT;
    qh->current_qtd = 0U;
    qh->alternate_next_qtd = EHCI_PTR_TERM;
    qh->token = 0U;
    qh->buffer[0] = 0U;
    qh->buffer[1] = 0U;
    qh->buffer[2] = 0U;
    qh->buffer[3] = 0U;
    qh->buffer[4] = 0U;
    (void)controller;
}

static void ehci_qtd_set_buffers(ehci_qtd_t* qtd, uint32_t address,
                                 uint32_t length) {
    uint32_t page;

    if (!qtd) return;
    for (uint32_t index = 0U; index < 5U; index++) qtd->buffer[index] = 0U;
    if (!length) return;
    qtd->buffer[0] = address;
    page = (address & ~(PAGE_SIZE - 1U)) + PAGE_SIZE;
    for (uint32_t index = 1U; index < 5U &&
         page < address + length; index++, page += PAGE_SIZE) {
        qtd->buffer[index] = page;
    }
}

static void ehci_qtd_prepare(ehci_qtd_t* qtd, uint32_t next,
                             uint32_t buffer, uint32_t length,
                             uint32_t pid, uint8_t toggle) {
    if (!qtd) return;
    qtd->next = next;
    qtd->alternate_next = EHCI_PTR_TERM;
    qtd->token = EHCI_QTD_STATUS_ACTIVE |
                 (EHCI_QTD_CERR << EHCI_QTD_CERR_SHIFT) | pid |
                 ((length & EHCI_QTD_BYTES_MASK) << EHCI_QTD_BYTES_SHIFT) |
                 (toggle ? EHCI_QTD_TOGGLE : 0U);
    ehci_qtd_set_buffers(qtd, buffer, length);
}

static uint32_t ehci_qtd_actual(const ehci_qtd_t* qtd, uint32_t requested) {
    uint32_t remaining;

    if (!qtd) return 0U;
    remaining = (qtd->token >> EHCI_QTD_BYTES_SHIFT) & EHCI_QTD_BYTES_MASK;
    return remaining > requested ? 0U : requested - remaining;
}

static int ehci_qtd_error(const ehci_qtd_t* qtd) {
    return qtd && (qtd->token & EHCI_QTD_STATUS_ERROR_MASK);
}

static int ehci_wait_qtds(ehci_controller_t* controller, uint32_t count,
                          uint32_t timeout_ms) {
    uint32_t start;

    if (!controller || !count || count > EHCI_SYNC_QTD_CAPACITY) {
        LOG_ERROR("EHCI", "Cadeia qTD EHCI invalida");
        return ERR_INVALID;
    }
    start = timer_get_ticks();
    for (;;) {
        uint8_t active = 0U;

        for (uint32_t index = 0U; index < count; index++) {
            uint32_t token = controller->qtd_pool[index].token;

            if (token & EHCI_QTD_STATUS_ACTIVE) active = 1U;
            if (ehci_qtd_error(&controller->qtd_pool[index])) {
                controller->last_error = ERR_STATE;
                return ERR_STATE;
            }
        }
        if (!active) return OK;
        if (ehci_deadline_expired(start, timeout_ms)) {
            controller->timeout_count++;
            controller->last_error = ERR_TIMEOUT;
            (void)ehci_recover_controller(controller);
            return ERR_TIMEOUT;
        }
        asm volatile("pause");
    }
}

static void ehci_clear_sync(ehci_controller_t* controller) {
    if (!controller || !controller->queue_heads) return;
    controller->queue_heads[1].next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[1].alternate_next_qtd = EHCI_PTR_TERM;
    controller->queue_heads[1].token = 0U;
    controller->qtd_in_use = 0U;
    controller->buffer_in_use = 0U;
    ehci_link_schedule(controller);
}

static int ehci_submit_sync(ehci_controller_t* controller, uint32_t count,
                            uint32_t timeout_ms) {
    if (!controller || !controller->running) {
        LOG_ERROR("EHCI", "Controlador EHCI indisponivel na transferencia");
        return ERR_NOT_FOUND;
    }
    controller->queue_heads[1].next_qtd = controller->qtd_pool_phys;
    controller->queue_heads[1].alternate_next_qtd = EHCI_PTR_TERM;
    controller->qtd_in_use = count;
    controller->buffer_in_use = 1U;
    asm volatile("" : : : "memory");
    return ehci_wait_qtds(controller, count, timeout_ms);
}

static int ehci_control_transfer(ehci_controller_t* controller,
                                 ehci_device_record_t* record,
                                 uint8_t request_type, uint8_t request,
                                 uint16_t value, uint16_t index,
                                 uint16_t length, uint8_t* data,
                                 uint16_t* out_length) {
    uint8_t* setup;
    uint8_t* payload;
    uint32_t data_count = (length + EHCI_QTD_CHUNK - 1U) / EHCI_QTD_CHUNK;
    uint32_t qtd_count = 2U + data_count;
    uint32_t copied = 0U;
    uint16_t control_packet;
    uint8_t data_toggle = 1U;
    uint8_t direction_in = request_type & EHCI_REQUEST_DEVICE_TO_HOST;
    int result;

    if (!controller || !record || qtd_count > EHCI_SYNC_QTD_CAPACITY ||
        length > EHCI_SYNC_BUFFER_SIZE || (length && !data)) {
        LOG_ERROR("EHCI", "Argumento invalido na transferencia de controle");
        return ERR_INVALID;
    }
    control_packet = record->info.max_packet_size0;
    if (control_packet < EHCI_CONTROL_PACKET_SIZE_MIN ||
        control_packet > EHCI_CONTROL_PACKET_SIZE_MAX) {
        LOG_ERROR("EHCI", "Tamanho do endpoint de controle EHCI invalido");
        return ERR_INVALID;
    }
    setup = controller->buffer_pool + EHCI_CONTROL_SETUP_OFFSET;
    payload = controller->buffer_pool + EHCI_CONTROL_DATA_OFFSET;
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8U);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8U);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8U);
    if (length && !direction_in) kmemcpy(payload, data, length);
    ehci_qh_configure(controller, &controller->queue_heads[1],
                      record->info.usb_address, 0U,
                      control_packet, 1U);
    ehci_qtd_prepare(&controller->qtd_pool[0], controller->qtd_pool_phys +
                     sizeof(ehci_qtd_t), controller->buffer_pool_phys,
                     EHCI_SETUP_PACKET_SIZE, EHCI_QTD_PID_SETUP, 0U);
    if (length) {
        for (uint32_t qtd = 0U; qtd < data_count; qtd++) {
            uint32_t chunk = length - copied > EHCI_QTD_CHUNK ?
                             EHCI_QTD_CHUNK : length - copied;
            uint32_t next = qtd + 1U < data_count ?
                controller->qtd_pool_phys + sizeof(ehci_qtd_t) * (qtd + 2U) :
                controller->qtd_pool_phys + sizeof(ehci_qtd_t) * (data_count + 1U);
            uint32_t packets = (chunk + control_packet - 1U) /
                               control_packet;
            ehci_qtd_prepare(&controller->qtd_pool[qtd + 1U], next,
                             controller->buffer_pool_phys +
                             EHCI_CONTROL_DATA_OFFSET + copied, chunk,
                             direction_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT,
                             data_toggle);
            data_toggle ^= (uint8_t)(packets & 1U);
            copied += chunk;
        }
    }
    ehci_qtd_prepare(&controller->qtd_pool[qtd_count - 1U], EHCI_PTR_TERM,
                     0U, 0U, direction_in ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN,
                     1U);
    result = ehci_submit_sync(controller, qtd_count,
                              EHCI_CONTROL_TIMEOUT_MS);
    if (result == OK && length && direction_in) {
        copied = 0U;
        for (uint32_t qtd = 0U; qtd < data_count; qtd++) {
            uint32_t chunk = length - copied > EHCI_QTD_CHUNK ?
                             EHCI_QTD_CHUNK : length - copied;
            uint32_t actual = ehci_qtd_actual(&controller->qtd_pool[qtd + 1U],
                                              chunk);
            if (actual) kmemcpy(data + copied, payload + copied, actual);
            copied += actual;
            if (actual < chunk) break;
        }
        if (out_length) *out_length = (uint16_t)copied;
    } else if (result == OK && out_length) {
        *out_length = length;
    }
    if (result == OK) controller->control_transfer_count++;
    ehci_clear_sync(controller);
    return result;
}

static int ehci_read_descriptor(ehci_controller_t* controller,
                                ehci_device_record_t* record, uint8_t type,
                                uint8_t* data, uint16_t length) {
    uint16_t actual = 0U;
    int result;

    result = ehci_control_transfer(controller, record,
        EHCI_REQUEST_DEVICE_TO_HOST | EHCI_REQUEST_STANDARD |
        EHCI_REQUEST_RECIPIENT_DEVICE, EHCI_REQUEST_GET_DESCRIPTOR,
        (uint16_t)(type << 8U), 0U, length, data, &actual);
    if (result != OK) return result;
    if (actual < 2U || data[1] != type || actual < length) {
        LOG_ERROR("EHCI", "Descritor USB incompleto ou inesperado");
        return ERR_INVALID;
    }
    return OK;
}

static int ehci_parse_configuration(ehci_device_record_t* record) {
    uint32_t offset = 0U;
    uint32_t endpoints = 0U;
    uint8_t interface_seen = 0U;

    if (!record || record->configuration_length < EHCI_CONFIGURATION_LENGTH) {
        LOG_ERROR("EHCI", "Descritor Configuration ausente");
        return ERR_INVALID;
    }
    while (offset < record->configuration_length) {
        uint8_t length;
        uint8_t type;

        if (offset + 2U > record->configuration_length) return ERR_INVALID;
        length = record->configuration_descriptor[offset];
        type = record->configuration_descriptor[offset + 1U];
        if (length < 2U || offset + length > record->configuration_length) {
            LOG_ERROR("EHCI", "Cadeia de descritores USB invalida");
            return ERR_INVALID;
        }
        if (type == EHCI_DESCRIPTOR_TYPE_CONFIGURATION) {
            if (length < EHCI_CONFIGURATION_LENGTH) return ERR_INVALID;
            record->info.configuration_value = record->configuration_descriptor[
                offset + EHCI_CONFIG_VALUE_OFFSET];
        } else if (type == EHCI_DESCRIPTOR_TYPE_INTERFACE) {
            if (length < EHCI_INTERFACE_LENGTH || interface_seen) {
                LOG_ERROR("EHCI", "Configuration USB possui layout nao suportado");
                return ERR_UNAVAILABLE;
            }
            if (record->configuration_descriptor[offset +
                EHCI_INTERFACE_ALTERNATE_OFFSET] != 0U) {
                LOG_ERROR("EHCI", "Interface USB usa alternate setting");
                return ERR_UNAVAILABLE;
            }
            interface_seen = 1U;
            record->info.interface_number = record->configuration_descriptor[
                offset + EHCI_INTERFACE_NUMBER_OFFSET];
            record->info.interface_class = record->configuration_descriptor[
                offset + EHCI_INTERFACE_CLASS_OFFSET];
            record->info.interface_subclass = record->configuration_descriptor[
                offset + EHCI_INTERFACE_SUBCLASS_OFFSET];
            record->info.interface_protocol = record->configuration_descriptor[
                offset + EHCI_INTERFACE_PROTOCOL_OFFSET];
        } else if (type == EHCI_DESCRIPTOR_TYPE_ENDPOINT) {
            uint8_t address;
            uint8_t transfer_type;
            uint16_t max_packet;

            if (length < EHCI_ENDPOINT_LENGTH || !interface_seen) {
                LOG_ERROR("EHCI", "Endpoint USB fora de uma interface");
                return ERR_INVALID;
            }
            address = record->configuration_descriptor[offset +
                                                       EHCI_ENDPOINT_ADDRESS_OFFSET];
            transfer_type = record->configuration_descriptor[offset + 3U] & 3U;
            max_packet = (uint16_t)(record->configuration_descriptor[offset +
                EHCI_ENDPOINT_MAX_PACKET_OFFSET] |
                ((uint16_t)record->configuration_descriptor[offset +
                EHCI_ENDPOINT_MAX_PACKET_OFFSET + 1U] << 8U));
            max_packet &= 0x07FFU;
            if (!(address & USB_ENDPOINT_ADDRESS_NUMBER_MASK) || !max_packet ||
                max_packet > USB_ENDPOINT_MAX_PACKET_SIZE_HIGH ||
                endpoints >= USB_DEVICE_MAX_ENDPOINTS) {
                LOG_ERROR("EHCI", "Endpoint USB invalido ou excedente");
                return ERR_INVALID;
            }
            record->info.endpoints[endpoints].address = address;
            record->info.endpoints[endpoints].transfer_type = transfer_type;
            record->info.endpoints[endpoints].max_packet = max_packet;
            record->info.endpoints[endpoints].interval =
                record->configuration_descriptor[offset + EHCI_ENDPOINT_INTERVAL_OFFSET];
            if (transfer_type == 2U && (address & 0x80U)) {
                record->info.bulk_in_count++;
                if (!record->info.bulk_in_endpoint) {
                    record->info.bulk_in_endpoint = address;
                    record->info.bulk_in_max_packet = max_packet;
                }
            } else if (transfer_type == 2U && !(address & 0x80U)) {
                record->info.bulk_out_count++;
                if (!record->info.bulk_out_endpoint) {
                    record->info.bulk_out_endpoint = address;
                    record->info.bulk_out_max_packet = max_packet;
                }
            } else if (transfer_type == 3U && (address & 0x80U)) {
                record->info.interrupt_in_count++;
                if (!record->info.interrupt_in_endpoint) {
                    record->info.interrupt_in_endpoint = address;
                    record->info.interrupt_in_max_packet = max_packet;
                    record->info.interrupt_interval = record->info.endpoints[
                        endpoints].interval;
                }
            }
            endpoints++;
        }
        offset += length;
    }
    if (!interface_seen || !record->info.configuration_value || !endpoints) {
        LOG_ERROR("EHCI", "Configuration USB sem interface ou endpoint");
        return ERR_UNAVAILABLE;
    }
    record->info.endpoint_count = (uint8_t)endpoints;
    return OK;
}

static void ehci_set_port_empty(ehci_controller_t* controller, uint32_t port) {
    usb_port_info_t* info;

    if (!controller || port >= controller->port_count) return;
    info = &controller->ports[port];
    kmemset(info, 0, sizeof(*info));
    kmemcpy(info->controller_id, controller->controller_id,
            USB_CONTROLLER_ID_SIZE);
    info->controller_bus = controller->bus;
    info->controller_device = controller->device;
    info->controller_function = controller->function;
    info->controller_model = USB_CONTROLLER_MODEL_EHCI;
    info->port_number = (uint8_t)(port + 1U);
    info->state = USB_PORT_EMPTY;
    info->reason = USB_PORT_REASON_NO_DEVICE;
    info->speed = USB_DEVICE_SPEED_HIGH;
}

static int ehci_reset_port(ehci_controller_t* controller, uint32_t port) {
    uint32_t offset;
    uint32_t status;
    uint32_t start;

    if (!controller || port >= controller->port_count) return ERR_INVALID;
    offset = controller->cap_length + EHCI_PORTSC_BASE +
             port * EHCI_PORTSC_STRIDE;
    status = ehci_read(controller, offset);
    if (status == 0xFFFFFFFFU || !(status & EHCI_PORT_CCS)) {
        return ERR_NOT_FOUND;
    }
    ehci_write(controller, offset, status | EHCI_PORT_PR);
    start = timer_get_ticks();
    while (ehci_read(controller, offset) & EHCI_PORT_PR) {
        if (ehci_deadline_expired(start, EHCI_RESET_TIMEOUT_MS)) {
            LOG_ERROR("EHCI", "Timeout no reset da porta EHCI");
            return ERR_TIMEOUT;
        }
        asm volatile("pause");
    }
    status = ehci_read(controller, offset);
    ehci_write(controller, offset, status | EHCI_PORT_RWC);
    start = timer_get_ticks();
    while (!ehci_deadline_expired(start, EHCI_SET_ADDRESS_DELAY_MS)) {
        asm volatile("pause");
    }
    status = ehci_read(controller, offset);
    if (!(status & EHCI_PORT_CCS) || !(status & EHCI_PORT_PED)) {
        LOG_ERROR("EHCI", "Porta EHCI nao habilitou dispositivo high-speed");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static int ehci_enumerate_port(ehci_controller_t* controller, uint32_t port) {
    ehci_device_record_t* record;
    uint8_t prefix[8];
    uint16_t actual = 0U;
    uint16_t total_length;
    uint8_t address;
    int result;

    record = ehci_find_port_record(controller, port);
    if (!record) return ERR_INVALID;
    kmemset(record, 0, sizeof(*record));
    record->allocated = 1U;
    record->info.controller_bus = controller->bus;
    record->info.controller_device = controller->device;
    record->info.controller_function = controller->function;
    record->info.controller_model = USB_CONTROLLER_MODEL_EHCI;
    record->info.port_number = (uint8_t)(port + 1U);
    record->info.speed = USB_DEVICE_SPEED_HIGH;
    record->info.usb_address = 0U;
    record->info.max_packet_size0 = 64U;
    result = ehci_reset_port(controller, port);
    if (result != OK) return result;
    result = ehci_read_descriptor(controller, record, EHCI_DESCRIPTOR_DEVICE,
                                  prefix, sizeof(prefix));
    if (result != OK || prefix[0] < sizeof(prefix) ||
        prefix[1] != EHCI_DESCRIPTOR_DEVICE ||
        (prefix[7] != 8U && prefix[7] != 16U && prefix[7] != 32U &&
         prefix[7] != 64U)) {
        LOG_ERROR("EHCI", "Prefixo do descritor USB invalido");
        return ERR_INVALID;
    }
    record->info.max_packet_size0 = prefix[7];
    address = controller->next_address++;
    if (!address || address > EHCI_MAX_USB_ADDRESS) return ERR_OVERFLOW;
    result = ehci_control_transfer(controller, record,
        EHCI_REQUEST_HOST_TO_DEVICE | EHCI_REQUEST_STANDARD |
        EHCI_REQUEST_RECIPIENT_DEVICE, EHCI_REQUEST_SET_ADDRESS, address,
        0U, 0U, 0, &actual);
    if (result != OK) return result;
    record->info.usb_address = address;
    {
        uint32_t start = timer_get_ticks();

        while (!ehci_deadline_expired(start, EHCI_SET_ADDRESS_DELAY_MS)) {
            asm volatile("pause");
        }
    }
    result = ehci_read_descriptor(controller, record, EHCI_DESCRIPTOR_DEVICE,
                                  record->device_descriptor,
                                  EHCI_DEVICE_DESCRIPTOR_LENGTH);
    if (result != OK) return result;
    record->info.vendor_id = (uint16_t)(record->device_descriptor[
        EHCI_DEVICE_VENDOR_OFFSET] | ((uint16_t)record->device_descriptor[
        EHCI_DEVICE_VENDOR_OFFSET + 1U] << 8U));
    record->info.product_id = (uint16_t)(record->device_descriptor[
        EHCI_DEVICE_PRODUCT_OFFSET] | ((uint16_t)record->device_descriptor[
        EHCI_DEVICE_PRODUCT_OFFSET + 1U] << 8U));
    record->info.device_revision = (uint16_t)(record->device_descriptor[
        EHCI_DEVICE_REVISION_OFFSET] | ((uint16_t)record->device_descriptor[
        EHCI_DEVICE_REVISION_OFFSET + 1U] << 8U));
    record->info.device_class = record->device_descriptor[EHCI_DEVICE_CLASS_OFFSET];
    record->info.device_subclass = record->device_descriptor[EHCI_DEVICE_SUBCLASS_OFFSET];
    record->info.device_protocol = record->device_descriptor[EHCI_DEVICE_PROTOCOL_OFFSET];
    record->info.num_configurations = record->device_descriptor[
        EHCI_DEVICE_CONFIG_COUNT_OFFSET];
    record->info.device_descriptor_valid = 1U;
    if (!record->info.num_configurations) return ERR_INVALID;
    result = ehci_read_descriptor(controller, record,
                                  EHCI_DESCRIPTOR_CONFIGURATION,
                                  record->configuration_descriptor,
                                  EHCI_CONFIGURATION_HEADER_LENGTH);
    if (result != OK) return result;
    total_length = (uint16_t)(record->configuration_descriptor[
        EHCI_CONFIG_TOTAL_LENGTH_OFFSET] |
        ((uint16_t)record->configuration_descriptor[
        EHCI_CONFIG_TOTAL_LENGTH_OFFSET + 1U] << 8U));
    if (total_length < EHCI_CONFIGURATION_LENGTH ||
        total_length > EHCI_MAX_DESCRIPTOR_LENGTH) return ERR_INVALID;
    result = ehci_read_descriptor(controller, record,
                                  EHCI_DESCRIPTOR_CONFIGURATION,
                                  record->configuration_descriptor,
                                  total_length);
    if (result != OK) return result;
    record->configuration_length = total_length;
    record->info.configuration_length = total_length;
    result = ehci_parse_configuration(record);
    if (result != OK) return result;
    result = ehci_control_transfer(controller, record,
        EHCI_REQUEST_HOST_TO_DEVICE | EHCI_REQUEST_STANDARD |
        EHCI_REQUEST_RECIPIENT_DEVICE, EHCI_REQUEST_SET_CONFIGURATION,
        record->info.configuration_value, 0U, 0U, 0, &actual);
    if (result != OK) return result;
    record->info.state = USB_DEVICE_CONFIGURED;
    record->info.configuration_descriptor_valid = 1U;
    record->info.controller_id[0] = '\0';
    kmemcpy(record->info.controller_id, controller->controller_id,
            USB_CONTROLLER_ID_SIZE);
    ehci_make_device_id(controller, port, address, record->info.id,
                        USB_DEVICE_ID_SIZE);
    record->valid_descriptors = 1U;
    kmemcpy(controller->ports[port].device_id, record->info.id,
            USB_DEVICE_ID_SIZE);
    controller->ports[port].connected = 1U;
    controller->ports[port].enabled = 1U;
    controller->ports[port].usb_address = address;
    controller->ports[port].state = USB_PORT_CONFIGURED;
    controller->ports[port].reason = USB_PORT_REASON_NONE;
    return OK;
}

static int ehci_initialize_ports(ehci_controller_t* controller) {
    if (!controller) return ERR_NULL;
    controller->device_count = 0U;
    controller->port_errors = 0U;
    controller->next_address = 1U;
    kmemset(controller->devices, 0, sizeof(controller->devices));
    for (uint32_t port = 0U; port < controller->port_count; port++) {
        uint32_t status = ehci_read(controller, controller->cap_length +
                                     EHCI_PORTSC_BASE + port *
                                     EHCI_PORTSC_STRIDE);
        int result;

        ehci_set_port_empty(controller, port);
        if (status == 0xFFFFFFFFU) {
            controller->ports[port].state = USB_PORT_DEGRADED;
            controller->ports[port].reason = USB_PORT_REASON_DRIVER_FAILURE;
            controller->port_errors++;
            continue;
        }
        if (!(status & EHCI_PORT_CCS)) continue;
        controller->ports[port].connected = 1U;
        controller->ports[port].state = USB_PORT_ENUMERATING;
        result = ehci_enumerate_port(controller, port);
        if (result == OK) controller->device_count++;
        else {
            controller->ports[port].state = USB_PORT_DEGRADED;
            controller->ports[port].reason = result == ERR_TIMEOUT ?
                USB_PORT_REASON_CONTROL_TIMEOUT :
                result == ERR_UNAVAILABLE ? USB_PORT_REASON_UNSUPPORTED_SPEED :
                result == ERR_INVALID ? USB_PORT_REASON_INVALID_DESCRIPTOR :
                USB_PORT_REASON_DRIVER_FAILURE;
            controller->port_errors++;
            controller->last_error = result;
            LOG_WARN_CODE("EHCI", result, "Porta EHCI degradada");
        }
    }
    return controller->port_errors ? ERR_STATE : OK;
}

static void ehci_irq_handler(registers_t* regs) {
    uint8_t irq_line;

    if (!regs || regs->int_no < 32U || regs->int_no > 47U) return;
    irq_line = (uint8_t)(regs->int_no - 32U);
    for (uint32_t index = 0U; index < EHCI_CONTROLLER_CAPACITY; index++) {
        ehci_controller_t* controller = &ehci_controllers[index];
        uint32_t status;

        if (!controller->used || !controller->running ||
            controller->irq != irq_line) continue;
        status = ehci_read(controller, controller->cap_length + EHCI_USBSTS);
        if (status == 0xFFFFFFFFU || !(status & EHCI_USBSTS_RELEVANT)) continue;
        ehci_write(controller, controller->cap_length + EHCI_USBSTS,
                   status & EHCI_USBSTS_RELEVANT);
        controller->irq_pending = 1U;
        controller->irq_events++;
    }
}

static int ehci_init_instance(ehci_controller_t* controller,
                              const pci_device_t* pci) {
    uint32_t base;
    uint32_t hcs_params;
    uint32_t cap_length;
    int result;

    result = ehci_validate_pci(pci, &base);
    if (result != OK) return result;
    result = pci_enable_memory_and_bus_mastering(pci);
    if (result != OK) return result;
    result = ehci_map_mmio(controller, base);
    if (result != OK) return result;
    cap_length = ehci_read(controller, 0U) & 0xFFU;
    hcs_params = ehci_read(controller, 4U);
    if (cap_length < EHCI_CAPABILITY_MIN_LENGTH ||
        cap_length > EHCI_MMIO_REQUIRED_BYTES ||
        (hcs_params & 0x0FU) == 0U ||
        (hcs_params & 0x0FU) > USB_EHCI_PORT_COUNT) {
        LOG_ERROR("EHCI", "Capacidades EHCI invalidas");
        return ERR_UNAVAILABLE;
    }
    controller->cap_length = (uint8_t)cap_length;
    controller->port_count = (uint8_t)(hcs_params & 0x0FU);
    result = ehci_allocate_dma(controller);
    if (result != OK) return result;
    result = ehci_reset_controller(controller);
    if (result != OK) return result;
    result = idt_register_shared_irq_handler(pci->irq, ehci_irq_handler);
    if (result != OK) {
        LOG_ERROR("EHCI", "Falha ao registrar IRQ compartilhada EHCI");
        return result;
    }
    controller->irq_registered = 1U;
    result = ehci_start_controller(controller);
    if (result != OK) return result;
    controller->initialized = 1U;
    controller->running = 1U;
    controller->last_error = OK;
    result = ehci_initialize_ports(controller);
    if (result != OK) LOG_WARN("EHCI", "Uma ou mais portas EHCI degradadas");
    return OK;
}

int ehci_init(const pci_device_t* pci, const char* controller_id) {
    ehci_controller_t* controller = 0;
    int result;

    LOG_INFO("EHCI", "Inicializando controlador EHCI");
    if (!pci || !controller_id) {
        LOG_ERROR("EHCI", "Argumento nulo na inicializacao EHCI");
        return ERR_NULL;
    }
    controller = ehci_find(pci->bus, pci->device, pci->function);
    if (controller && controller->initialized) {
        LOG_INFO("EHCI", "Controlador EHCI ja estava inicializado");
        return OK;
    }
    if (controller) {
        LOG_ERROR("EHCI", "Instancia EHCI anterior permaneceu invalida");
        return controller->last_error ? controller->last_error : ERR_STATE;
    }
    for (uint32_t index = 0U; index < EHCI_CONTROLLER_CAPACITY; index++) {
        if (!ehci_controllers[index].used) {
            controller = &ehci_controllers[index];
            kmemset(controller, 0, sizeof(*controller));
            controller->used = 1U;
            spinlock_init(&controller->transfer_lock);
            break;
        }
    }
    if (!controller || !controller->used) {
        LOG_ERROR("EHCI", "Limite de controladores EHCI atingido");
        return ERR_OVERFLOW;
    }
    controller->bus = pci->bus;
    controller->device = pci->device;
    controller->function = pci->function;
    controller->irq = pci->irq;
    kmemcpy(controller->controller_id, controller_id, USB_CONTROLLER_ID_SIZE);
    result = ehci_init_instance(controller, pci);
    if (result != OK) {
        ehci_disable(controller);
        ehci_release_dma(controller);
        kmemset(controller, 0, sizeof(*controller));
        LOG_ERROR("EHCI", "Falha ao inicializar controlador EHCI");
        return result;
    }
    LOG_INFO("EHCI", "Controlador EHCI inicializado com sucesso");
    return OK;
}

static void ehci_interrupt_arm(ehci_controller_t* controller,
                               ehci_interrupt_request_t* request) {
    ehci_qtd_t* qtd;

    if (!controller || !request) return;
    qtd = &controller->qtd_pool[EHCI_QTD_CAPACITY - 1U];
    ehci_qh_configure(controller, &controller->queue_heads[2],
                      request->record ? request->record->info.usb_address : 0U,
                      request->endpoint_address & 0x0FU,
                      request->max_packet, 0U);
    ehci_qtd_prepare(qtd, EHCI_PTR_TERM,
                     controller->buffer_pool_phys + EHCI_INTERRUPT_BUFFER_OFFSET,
                     request->max_packet, EHCI_QTD_PID_IN, request->toggle);
    controller->queue_heads[2].next_qtd = controller->qtd_pool_phys +
                                           sizeof(ehci_qtd_t) *
                                           (EHCI_QTD_CAPACITY - 1U);
    request->active = 1U;
    request->deadline = timer_get_ticks() +
                        ehci_timeout_ticks(request->interval ?
                                           request->interval * 10U : 1000U);
    ehci_link_schedule(controller);
}

static int ehci_interrupt_poll(ehci_controller_t* controller) {
    ehci_interrupt_request_t* request;
    ehci_qtd_t* qtd;
    uint16_t actual;
    int result;
    usb_interrupt_callback_t callback;
    void* context;
    const uint8_t* data;

    if (!controller || !controller->interrupt_requests[0].used) return 0;
    request = &controller->interrupt_requests[0];
    if (!request->active) return 0;
    qtd = &controller->qtd_pool[EHCI_QTD_CAPACITY - 1U];
    if (qtd->token & EHCI_QTD_STATUS_ACTIVE) {
        if ((int32_t)(timer_get_ticks() - request->deadline) < 0) return 0;
        result = ERR_TIMEOUT;
        actual = 0U;
        controller->interrupt_timeout_count++;
    } else if (qtd->token & EHCI_QTD_STATUS_ERROR_MASK) {
        result = ERR_STATE;
        actual = 0U;
        controller->interrupt_error_count++;
    } else {
        result = OK;
        actual = (uint16_t)ehci_qtd_actual(qtd, request->max_packet);
        request->toggle ^= 1U;
        controller->interrupt_transfer_count++;
    }
    request->active = 0U;
    request->result = result;
    request->actual_length = actual;
    controller->queue_heads[2].next_qtd = EHCI_PTR_TERM;
    ehci_link_schedule(controller);
    callback = request->callback;
    context = request->context;
    data = controller->buffer_pool + EHCI_INTERRUPT_BUFFER_OFFSET;
    if (result == ERR_TIMEOUT || result == ERR_STATE) request->used = 0U;
    if (callback) callback(context, result, data, actual);
    if (result == OK && request->used && controller->running) {
        ehci_interrupt_arm(controller, request);
    }
    return 1;
}

int ehci_poll(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0U;

    if (!out_processed) {
        LOG_ERROR("EHCI", "Destino nulo no polling EHCI");
        return ERR_NULL;
    }
    for (uint32_t index = 0U; index < EHCI_CONTROLLER_CAPACITY; index++) {
        ehci_controller_t* controller = &ehci_controllers[index];
        uint32_t status;

        if (!controller->used || !controller->running) continue;
        if (processed < budget) processed += (uint32_t)ehci_interrupt_poll(controller);
        status = ehci_read(controller, controller->cap_length + EHCI_USBSTS);
        if (status == 0xFFFFFFFFU || (status & EHCI_USBSTS_HSE)) {
            controller->running = 0U;
            controller->last_error = ERR_UNAVAILABLE;
            LOG_ERROR("EHCI", "Controlador EHCI deixou de responder");
            processed++;
            continue;
        }
        if (status & EHCI_USBSTS_RELEVANT) {
            ehci_write(controller, controller->cap_length + EHCI_USBSTS,
                       status & EHCI_USBSTS_RELEVANT);
            controller->irq_pending = 0U;
            if (processed < budget) processed++;
        }
    }
    *out_processed = processed;
    return OK;
}

static int ehci_copy_status(const ehci_controller_t* controller,
                            usb_ehci_status_t* out_status) {
    if (!controller || !out_status) return ERR_NULL;
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->initialized = controller->initialized;
    out_status->running = controller->running;
    out_status->irq_registered = controller->irq_registered;
    out_status->irq_pending = controller->irq_pending;
    out_status->dma_ready = controller->dma_pool != 0;
    out_status->control_transfer_ready = controller->running;
    out_status->bulk_transfer_ready = controller->running;
    out_status->interrupt_transfer_ready = controller->running;
    out_status->port_count = controller->port_count;
    out_status->device_count = controller->device_count;
    out_status->port_errors = controller->port_errors;
    out_status->capability_base = controller->mmio_base;
    out_status->operational_base = controller->mmio_base + controller->cap_length;
    out_status->async_list_phys = controller->qh_pool_phys;
    out_status->qh_pool_phys = controller->qh_pool_phys;
    out_status->qtd_pool_phys = controller->qtd_pool_phys;
    out_status->buffer_pool_phys = controller->buffer_pool_phys;
    out_status->qtd_capacity = EHCI_QTD_CAPACITY;
    out_status->qtd_in_use = controller->qtd_in_use;
    out_status->buffer_capacity = EHCI_DMA_PAGE_COUNT - 2U;
    out_status->buffer_in_use = controller->buffer_in_use;
    out_status->irq_events = controller->irq_events;
    out_status->timeout_count = controller->timeout_count;
    out_status->recovery_count = controller->recovery_count;
    out_status->control_transfer_count = controller->control_transfer_count;
    out_status->bulk_transfer_count = controller->bulk_transfer_count;
    out_status->interrupt_transfer_count = controller->interrupt_transfer_count;
    out_status->interrupt_timeout_count = controller->interrupt_timeout_count;
    out_status->interrupt_error_count = controller->interrupt_error_count;
    out_status->interrupt_cancel_count = controller->interrupt_cancel_count;
    out_status->last_error = controller->last_error;
    return OK;
}

int ehci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_ehci_status_t* out_status) {
    ehci_controller_t* controller = ehci_find(bus, device, function);

    if (!out_status) {
        LOG_ERROR("EHCI", "Destino nulo ao consultar status EHCI");
        return ERR_NULL;
    }
    if (!controller) {
        LOG_ERROR("EHCI", "Controlador ausente ao consultar status");
        return ERR_NOT_FOUND;
    }
    return ehci_copy_status(controller, out_status);
}

int ehci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count) {
    ehci_controller_t* controller = ehci_find(bus, device, function);

    if (!out_count) {
        LOG_ERROR("EHCI", "Destino nulo ao consultar portas");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("EHCI", "Controlador ausente ao consultar portas");
        return ERR_NOT_FOUND;
    }
    *out_count = controller->port_count;
    return OK;
}

int ehci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                 uint32_t index, usb_port_info_t* out_info) {
    ehci_controller_t* controller = ehci_find(bus, device, function);

    if (!out_info) {
        LOG_ERROR("EHCI", "Destino nulo ao consultar porta");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("EHCI", "Controlador ausente ao consultar porta");
        return ERR_NOT_FOUND;
    }
    if (index >= controller->port_count) {
        LOG_ERROR("EHCI", "Indice de porta EHCI invalido");
        return ERR_INVALID;
    }
    *out_info = controller->ports[index];
    return OK;
}

int ehci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count) {
    ehci_controller_t* controller = ehci_find(bus, device, function);

    if (!out_count) {
        LOG_ERROR("EHCI", "Destino nulo ao consultar dispositivos");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("EHCI", "Controlador ausente ao consultar dispositivos");
        return ERR_NOT_FOUND;
    }
    *out_count = controller->device_count;
    return OK;
}

int ehci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info) {
    ehci_controller_t* controller = ehci_find(bus, device, function);
    uint32_t found = 0U;

    if (!out_info) {
        LOG_ERROR("EHCI", "Destino nulo ao consultar dispositivo");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("EHCI", "Controlador ausente ao consultar dispositivo");
        return ERR_NOT_FOUND;
    }
    for (uint32_t port = 0U; port < controller->port_count; port++) {
        if (!controller->devices[port].valid_descriptors) continue;
        if (found++ == index) {
            *out_info = controller->devices[port].info;
            return OK;
        }
    }
    LOG_ERROR("EHCI", "Indice de dispositivo EHCI invalido");
    return ERR_INVALID;
}

int ehci_validate_state(uint8_t bus, uint8_t device, uint8_t function) {
    ehci_controller_t* controller = ehci_find(bus, device, function);

    if (!controller) {
        LOG_ERROR("EHCI", "Controlador ausente na validacao EHCI");
        return ERR_NOT_FOUND;
    }
    if (!controller->initialized || !controller->running ||
        !controller->irq_registered || !controller->dma_pool ||
        !controller->queue_heads || !controller->qtd_pool ||
        !controller->buffer_pool || controller->port_count == 0U ||
        controller->port_count > USB_EHCI_PORT_COUNT ||
        controller->device_count > controller->port_count ||
        controller->qtd_in_use > EHCI_QTD_CAPACITY) {
        LOG_ERROR("EHCI", "Estado EHCI inconsistente");
        return ERR_STATE;
    }
    if (controller->queue_heads[0].horizontal_link !=
        EHCI_QH_LINK(controller->qh_pool_phys + sizeof(ehci_qh_t))) {
        LOG_ERROR("EHCI", "Anchor da lista assincrona EHCI invalido");
        return ERR_STATE;
    }
    for (uint32_t port = 0U; port < controller->port_count; port++) {
        ehci_device_record_t* record = &controller->devices[port];

        if (controller->ports[port].port_number != port + 1U ||
            controller->ports[port].state > USB_PORT_DEGRADED) {
            LOG_ERROR("EHCI", "Estado de porta EHCI invalido");
            return ERR_STATE;
        }
        if (!record->valid_descriptors) continue;
        if (record->info.state != USB_DEVICE_CONFIGURED ||
            record->info.controller_model != USB_CONTROLLER_MODEL_EHCI ||
            record->info.speed != USB_DEVICE_SPEED_HIGH ||
            !record->info.usb_address || !record->info.id[0] ||
            !record->info.device_descriptor_valid ||
            !record->info.configuration_descriptor_valid ||
            !record->info.endpoint_count ||
            record->info.endpoint_count > USB_DEVICE_MAX_ENDPOINTS) {
            LOG_ERROR("EHCI", "Dispositivo EHCI configurado invalido");
            return ERR_STATE;
        }
        for (uint32_t endpoint = 0U; endpoint < record->info.endpoint_count;
             endpoint++) {
            if (!record->info.endpoints[endpoint].max_packet ||
                record->info.endpoints[endpoint].max_packet >
                USB_ENDPOINT_MAX_PACKET_SIZE_HIGH) {
                LOG_ERROR("EHCI", "Endpoint EHCI invalido");
                return ERR_STATE;
            }
        }
    }
    return OK;
}

int ehci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length) {
    ehci_controller_t* controller;
    ehci_device_record_t* record;
    int result;

    if (!device) {
        LOG_ERROR("EHCI", "Dispositivo nulo na requisicao de controle");
        return ERR_NULL;
    }
    controller = ehci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = ehci_find_device_record(controller, device);
    if (!record || !controller->running) {
        LOG_ERROR("EHCI", "Dispositivo ausente no controle EHCI");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    result = ehci_control_transfer(controller, record, request_type, request,
                                   value, index, length, data, out_length);
    spinlock_release(&controller->transfer_lock);
    return result;
}

static int ehci_bulk_transfer_locked(ehci_controller_t* controller,
                                     ehci_device_record_t* record,
                                     uint8_t endpoint_address,
                                     uint8_t direction_in, uint8_t* buffer,
                                     uint16_t length, uint16_t* out_length) {
    uint16_t max_packet;
    uint32_t count = (length + EHCI_QTD_CHUNK - 1U) / EHCI_QTD_CHUNK;
    uint32_t copied = 0U;
    uint8_t toggle;
    int result;

    if (!controller || !record || !buffer || !length ||
        length > EHCI_SYNC_BUFFER_SIZE || count > EHCI_SYNC_QTD_CAPACITY ||
        endpoint_address != (direction_in ? record->info.bulk_in_endpoint :
                             record->info.bulk_out_endpoint)) {
        LOG_ERROR("EHCI", "Argumento invalido na transferencia Bulk");
        return ERR_INVALID;
    }
    max_packet = direction_in ? record->info.bulk_in_max_packet :
                 record->info.bulk_out_max_packet;
    if (!max_packet || max_packet > USB_ENDPOINT_MAX_PACKET_SIZE_HIGH) {
        LOG_ERROR("EHCI", "Endpoint Bulk EHCI sem tamanho valido");
        return ERR_UNAVAILABLE;
    }
    if (!direction_in) kmemcpy(controller->buffer_pool, buffer, length);
    toggle = direction_in ? record->bulk_in_toggle : record->bulk_out_toggle;
    for (uint32_t qtd = 0U; qtd < count; qtd++) {
        uint32_t chunk = length - copied > EHCI_QTD_CHUNK ?
                         EHCI_QTD_CHUNK : length - copied;
        uint32_t next = qtd + 1U < count ? controller->qtd_pool_phys +
                     sizeof(ehci_qtd_t) * (qtd + 1U) : EHCI_PTR_TERM;
        uint32_t packets = (chunk + max_packet - 1U) / max_packet;

        ehci_qtd_prepare(&controller->qtd_pool[qtd], next,
                         controller->buffer_pool_phys + copied, chunk,
                         direction_in ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT,
                         toggle);
        toggle ^= (uint8_t)(packets & 1U);
        copied += chunk;
    }
    result = ehci_submit_sync(controller, count, EHCI_BULK_TIMEOUT_MS);
    if (result == OK) {
        uint32_t actual_total = 0U;
        copied = 0U;
        for (uint32_t qtd = 0U; qtd < count; qtd++) {
            uint32_t chunk = length - copied > EHCI_QTD_CHUNK ?
                             EHCI_QTD_CHUNK : length - copied;
            uint32_t actual = ehci_qtd_actual(&controller->qtd_pool[qtd], chunk);

            actual_total += actual;
            copied += chunk;
            if (actual < chunk) break;
        }
        if (direction_in && actual_total) kmemcpy(buffer,
            controller->buffer_pool, actual_total);
        if (direction_in) {
            if (record->info.bulk_in_endpoint == endpoint_address) {
                record->bulk_in_toggle = toggle;
            }
        } else {
            record->bulk_out_toggle = toggle;
        }
        if (out_length) *out_length = direction_in ?
            (uint16_t)actual_total : length;
        controller->bulk_transfer_count++;
    }
    ehci_clear_sync(controller);
    return result;
}

int ehci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length) {
    ehci_controller_t* controller;
    ehci_device_record_t* record;
    int result;

    if (!device || !buffer || !length) {
        LOG_ERROR("EHCI", "Argumento nulo na transferencia Bulk");
        return ERR_NULL;
    }
    if (device->speed != USB_DEVICE_SPEED_HIGH) {
        LOG_ERROR("EHCI", "Bulk EHCI exige dispositivo high-speed");
        return ERR_UNAVAILABLE;
    }
    controller = ehci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = ehci_find_device_record(controller, device);
    if (!record || !controller->running) {
        LOG_ERROR("EHCI", "Dispositivo ausente na transferencia Bulk");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    result = ehci_bulk_transfer_locked(controller, record, endpoint_address,
                                       direction_in, buffer, length,
                                       out_length);
    spinlock_release(&controller->transfer_lock);
    if (result != OK) LOG_ERROR("EHCI", "Transferencia Bulk EHCI falhou");
    return result;
}

int ehci_reset_bulk_toggles(const usb_device_info_t* device) {
    ehci_controller_t* controller;
    ehci_device_record_t* record;

    if (!device) {
        LOG_ERROR("EHCI", "Dispositivo nulo ao resetar toggles");
        return ERR_NULL;
    }
    controller = ehci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = ehci_find_device_record(controller, device);
    if (!record) {
        LOG_ERROR("EHCI", "Dispositivo ausente ao resetar toggles");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    record->bulk_in_toggle = 0U;
    record->bulk_out_toggle = 0U;
    spinlock_release(&controller->transfer_lock);
    return OK;
}

int ehci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval, usb_interrupt_callback_t callback,
                          void* context) {
    ehci_controller_t* controller;
    ehci_device_record_t* record;
    ehci_interrupt_request_t* request;

    if (!device || !callback || !max_packet || max_packet >
        EHCI_INTERRUPT_BUFFER_SIZE || !(endpoint_address & 0x80U)) {
        LOG_ERROR("EHCI", "Argumento invalido no Interrupt EHCI");
        return ERR_INVALID;
    }
    controller = ehci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = ehci_find_device_record(controller, device);
    if (!record || !controller->running) {
        LOG_ERROR("EHCI", "Dispositivo ausente no Interrupt EHCI");
        return ERR_NOT_FOUND;
    }
    request = &controller->interrupt_requests[0];
    spinlock_acquire(&controller->transfer_lock);
    if (request->used) {
        spinlock_release(&controller->transfer_lock);
        LOG_ERROR("EHCI", "Solicitacao Interrupt EHCI ja ocupada");
        return ERR_OVERFLOW;
    }
    kmemset(request, 0, sizeof(*request));
    request->used = 1U;
    request->endpoint_address = endpoint_address;
    request->max_packet = max_packet;
    request->interval = interval ? interval : 1U;
    request->callback = callback;
    request->record = record;
    request->context = context ? context : record;
    ehci_interrupt_arm(controller, request);
    spinlock_release(&controller->transfer_lock);
    return OK;
}

int ehci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address) {
    ehci_controller_t* controller;
    ehci_interrupt_request_t* request;

    if (!device) {
        LOG_ERROR("EHCI", "Dispositivo nulo ao cancelar Interrupt");
        return ERR_NULL;
    }
    controller = ehci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    if (!controller) {
        LOG_ERROR("EHCI", "Controlador ausente ao cancelar Interrupt");
        return ERR_NOT_FOUND;
    }
    request = &controller->interrupt_requests[0];
    spinlock_acquire(&controller->transfer_lock);
    if (!request->used || request->endpoint_address != endpoint_address) {
        spinlock_release(&controller->transfer_lock);
        LOG_ERROR("EHCI", "Solicitacao Interrupt nao encontrada");
        return ERR_NOT_FOUND;
    }
    request->used = 0U;
    request->active = 0U;
    controller->queue_heads[2].next_qtd = EHCI_PTR_TERM;
    ehci_link_schedule(controller);
    controller->interrupt_cancel_count++;
    spinlock_release(&controller->transfer_lock);
    return OK;
}
