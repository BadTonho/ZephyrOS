#include "drivers/uhci.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/spinlock.h"
#include "core/timer.h"
#include "core/irq_deferred.h"
#include "drivers/idt.h"

#define UHCI_CONTROLLER_CAPACITY USB_MANAGER_MAX_CONTROLLERS
#define UHCI_TD_SIZE 16U
#define UHCI_QH_STRIDE 16U
#define UHCI_SYNC_TD_CAPACITY (USB_UHCI_TD_CAPACITY - \
                               USB_UHCI_INTERRUPT_CAPACITY)
#define UHCI_INTERRUPT_QH_BASE 1U
#define UHCI_INTERRUPT_TD_BASE UHCI_SYNC_TD_CAPACITY
#define UHCI_SETUP_SIZE 8U
#define UHCI_DEVICE_DESCRIPTOR_LENGTH 18U
#define UHCI_CONFIGURATION_HEADER_LENGTH 9U
#define UHCI_DESCRIPTOR_TYPE_DEVICE 1U
#define UHCI_DESCRIPTOR_TYPE_CONFIGURATION 2U
#define UHCI_DESCRIPTOR_TYPE_INTERFACE 4U
#define UHCI_DESCRIPTOR_TYPE_ENDPOINT 5U
#define UHCI_REQUEST_GET_DESCRIPTOR 6U
#define UHCI_REQUEST_SET_ADDRESS 5U
#define UHCI_REQUEST_SET_CONFIGURATION 9U
#define UHCI_DESCRIPTOR_DEVICE 1U
#define UHCI_DESCRIPTOR_CONFIGURATION 2U
#define UHCI_REQUEST_DEVICE_TO_HOST 0x80U
#define UHCI_REQUEST_HOST_TO_DEVICE 0x00U
#define UHCI_REQUEST_STANDARD 0x00U
#define UHCI_REQUEST_RECIPIENT_DEVICE 0x00U
#define UHCI_PID_OUT 0xE1U
#define UHCI_PID_IN 0x69U
#define UHCI_PID_SETUP 0x2DU
#define UHCI_PTR_TERM 0x0001U
#define UHCI_PTR_QH 0x0002U
#define UHCI_USBCMD 0x00U
#define UHCI_USBSTS 0x02U
#define UHCI_USBINTR 0x04U
#define UHCI_FRNUM 0x06U
#define UHCI_FLBASEADD 0x08U
#define UHCI_SOFMOD 0x0CU
#define UHCI_USBCMD_RUN_STOP 0x0001U
#define UHCI_USBCMD_HOST_RESET 0x0002U
#define UHCI_USBCMD_CONFIG_FLAG 0x0040U
#define UHCI_USBCMD_MAX_PACKET 0x0080U
#define UHCI_USBSTS_RELEVANT 0x001FU
#define UHCI_USBSTS_HCH 0x0020U
#define UHCI_USBINTR_ALL 0x000FU
#define UHCI_PORT_CCS 0x0001U
#define UHCI_PORT_CSC 0x0002U
#define UHCI_PORT_PE 0x0004U
#define UHCI_PORT_PEC 0x0008U
#define UHCI_PORT_LSDA 0x0100U
#define UHCI_PORT_PR 0x0200U
#define UHCI_PORT_SUSP 0x1000U
#define UHCI_TD_SPD (1U << 29U)
#define UHCI_TD_LOW_SPEED (1U << 26U)
#define UHCI_TD_IOC (1U << 24U)
#define UHCI_TD_ACTIVE (1U << 23U)
#define UHCI_TD_STALLED (1U << 22U)
#define UHCI_TD_DBUFERR (1U << 21U)
#define UHCI_TD_BABBLE (1U << 20U)
#define UHCI_TD_NAK (1U << 19U)
#define UHCI_TD_CRCTIMEO (1U << 18U)
#define UHCI_TD_BITSTUFF (1U << 17U)
#define UHCI_TD_ERROR_MASK (UHCI_TD_STALLED | UHCI_TD_DBUFERR | \
                            UHCI_TD_BABBLE | UHCI_TD_NAK | \
                            UHCI_TD_CRCTIMEO | UHCI_TD_BITSTUFF)
#define UHCI_TD_ERROR_COUNT_SHIFT 27U
#define UHCI_TD_TOGGLE_SHIFT 19U
#define UHCI_TD_LENGTH_SHIFT 21U
#define UHCI_TD_LENGTH_MASK 0x7FFU
#define UHCI_TD_TOKEN_ADDRESS_SHIFT 8U
#define UHCI_TD_TOKEN_ENDPOINT_SHIFT 15U
#define UHCI_TD_TOKEN_MAX_LENGTH_MASK 0x7FFU
#define UHCI_MAX_USB_ADDRESS 127U
#define UHCI_MAX_DESCRIPTOR_LENGTH USB_UHCI_DESCRIPTOR_BUFFER_SIZE
#define UHCI_CONFIG_VALUE_OFFSET 5U
#define UHCI_CONFIG_TOTAL_LENGTH_OFFSET 2U
#define UHCI_DEVICE_VENDOR_OFFSET 8U
#define UHCI_DEVICE_PRODUCT_OFFSET 10U
#define UHCI_DEVICE_REVISION_OFFSET 12U
#define UHCI_DEVICE_CLASS_OFFSET 4U
#define UHCI_DEVICE_SUBCLASS_OFFSET 5U
#define UHCI_DEVICE_PROTOCOL_OFFSET 6U
#define UHCI_DEVICE_MPS_OFFSET 7U
#define UHCI_DEVICE_CONFIG_COUNT_OFFSET 17U
#define UHCI_INTERFACE_NUMBER_OFFSET 2U
#define UHCI_INTERFACE_ALTERNATE_OFFSET 3U
#define UHCI_INTERFACE_CLASS_OFFSET 5U
#define UHCI_INTERFACE_SUBCLASS_OFFSET 6U
#define UHCI_INTERFACE_PROTOCOL_OFFSET 7U
#define UHCI_ENDPOINT_ADDRESS_OFFSET 2U
#define UHCI_ENDPOINT_MAX_PACKET_OFFSET 4U
#define UHCI_ENDPOINT_INTERVAL_OFFSET 6U
#define UHCI_ENDPOINT_DESCRIPTOR_LENGTH 7U
#define UHCI_INTERFACE_DESCRIPTOR_LENGTH 9U
#define UHCI_CONFIG_DESCRIPTOR_LENGTH 9U
#define UHCI_INTERFACE_COUNT_LIMIT 1U
#define UHCI_CONTROL_BUFFER_SETUP 0U
#define UHCI_CONTROL_BUFFER_DATA 1U
#define UHCI_CONTROL_BUFFER_DATA_OFFSET (UHCI_CONTROL_BUFFER_DATA * \
                                         USB_UHCI_DESCRIPTOR_BUFFER_SIZE)
#define UHCI_CONTROL_DEFAULT_MAX_PACKET 8U
#define UHCI_ENUMERATION_RETRIES 3U
#define UHCI_DEVICE_MAX_PACKET_MIN 8U
#define UHCI_DEVICE_MAX_PACKET_MAX 64U
#define UHCI_DIAGNOSTIC_PREVIEW_BYTES 20U

typedef enum {
    UHCI_ENUM_STAGE_RESET = 0,
    UHCI_ENUM_STAGE_DEVICE_PREFIX,
    UHCI_ENUM_STAGE_SET_ADDRESS,
    UHCI_ENUM_STAGE_ADDRESS_SETTLE,
    UHCI_ENUM_STAGE_DEVICE_FULL,
    UHCI_ENUM_STAGE_CONFIGURATION_HEADER,
    UHCI_ENUM_STAGE_CONFIGURATION_FULL,
    UHCI_ENUM_STAGE_CONFIGURATION_PARSE,
    UHCI_ENUM_STAGE_SET_CONFIGURATION,
    UHCI_ENUM_STAGE_PUBLISH
} uhci_enum_stage_t;

typedef struct {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
} uhci_td_t __attribute__((aligned(16)));

typedef struct {
    uint32_t link;
    uint32_t element;
} uhci_qh_t __attribute__((aligned(16)));

typedef struct uhci_controller uhci_controller_t;
typedef struct uhci_device_record uhci_device_record_t;

typedef struct {
    uint8_t used;
    uint8_t active;
    uint8_t completion_pending;
    uint8_t cancelled;
    uint8_t endpoint_address;
    uint8_t max_packet;
    uint8_t interval;
    uint8_t phase;
    uint8_t td_index;
    uint8_t qh_index;
    uint8_t buffer_index;
    uint8_t toggle;
    uint32_t generation;
    uint32_t deadline;
    uint16_t actual_length;
    int result;
    uhci_controller_t* controller;
    uhci_device_record_t* record;
    uhci_interrupt_callback_t callback;
    void* context;
    irq_deferred_work_t completion_work;
} uhci_interrupt_request_t;

struct uhci_device_record {
    usb_device_info_t info;
    uint8_t device_descriptor[UHCI_DEVICE_DESCRIPTOR_LENGTH];
    uint8_t configuration_descriptor[USB_UHCI_DESCRIPTOR_BUFFER_SIZE];
    uint16_t configuration_length;
    uint8_t valid_descriptors;
    uint8_t bulk_in_toggle;
    uint8_t bulk_out_toggle;
};

typedef struct {
    uint8_t active;
    uint8_t address;
    uint8_t max_packet_size0;
    uint8_t attempt;
    usb_device_speed_t speed;
    uhci_enum_stage_t stage;
    int last_error;
    const char* failure_detail;
} uhci_enum_context_t;

struct uhci_controller {
    uint8_t used;
    uint8_t initialized;
    uint8_t running;
    uint8_t irq_registered;
    uint8_t irq_pending;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uint16_t io_base;
    char controller_id[USB_CONTROLLER_ID_SIZE];
    uint32_t* frame_list;
    uhci_qh_t* queue_head;
    uhci_td_t* td_pool;
    uint8_t* buffer_pool;
    uint32_t frame_list_phys;
    uint32_t queue_head_phys;
    uint32_t td_pool_phys;
    uint32_t buffer_pool_phys;
    uint32_t td_in_use;
    uint32_t buffer_in_use;
    uint32_t sync_td_in_use;
    uint32_t sync_buffer_in_use;
    uint32_t irq_events;
    uint32_t timeout_count;
    uint32_t recovery_count;
    uint32_t bulk_transfer_count;
    uint32_t interrupt_transfer_count;
    uint32_t interrupt_timeout_count;
    uint32_t interrupt_error_count;
    uint32_t interrupt_cancel_count;
    uint8_t next_address;
    uint8_t port_errors;
    uint8_t interrupt_active_count;
    uint8_t frame_owner[USB_UHCI_FRAME_COUNT];
    spinlock_t transfer_lock;
    int last_error;
    usb_port_info_t ports[USB_UHCI_PORT_COUNT];
    uhci_device_record_t devices[USB_UHCI_PORT_COUNT];
    uhci_enum_context_t enumeration[USB_UHCI_PORT_COUNT];
    uint8_t device_count;
    uhci_interrupt_request_t interrupt_requests[USB_UHCI_INTERRUPT_CAPACITY];
};

static uhci_controller_t uhci_controllers[UHCI_CONTROLLER_CAPACITY];

static uhci_qh_t* uhci_queue_head_at(const uhci_controller_t* controller,
                                     uint32_t index) {
    if (!controller || !controller->queue_head) return 0;
    return (uhci_qh_t*)((uint8_t*)controller->queue_head +
                        index * UHCI_QH_STRIDE);
}

static void uhci_update_resource_usage(uhci_controller_t* controller) {
    if (!controller) return;
    controller->td_in_use = controller->sync_td_in_use +
                            controller->interrupt_active_count;
    controller->buffer_in_use = controller->sync_buffer_in_use +
                                controller->interrupt_active_count;
}

static uint16_t uhci_in16(const uhci_controller_t* controller,
                          uint16_t offset) {
    uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) :
                 "Nd"((uint16_t)(controller->io_base + offset)) : "memory");
    return value;
}

static void uhci_out8(const uhci_controller_t* controller,
                      uint16_t offset, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value),
                 "Nd"((uint16_t)(controller->io_base + offset)) : "memory");
}

static void uhci_out16(const uhci_controller_t* controller,
                       uint16_t offset, uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value),
                 "Nd"((uint16_t)(controller->io_base + offset)) : "memory");
}

static void uhci_out32(const uhci_controller_t* controller,
                       uint16_t offset, uint32_t value) {
    asm volatile("outl %0, %1" : : "a"(value),
                 "Nd"((uint16_t)(controller->io_base + offset)) : "memory");
}

static uint32_t uhci_timeout_ticks(uint32_t milliseconds) {
    uint32_t frequency = timer_get_frequency();
    uint32_t whole_seconds;
    uint32_t remainder_ms;
    uint32_t whole_ticks;
    uint32_t partial_ticks;

    if (!frequency) return 1U;
    whole_seconds = milliseconds / 1000U;
    remainder_ms = milliseconds % 1000U;
    if (whole_seconds > 0xFFFFFFFFU / frequency) return 0xFFFFFFFFU;
    whole_ticks = whole_seconds * frequency;
    partial_ticks = (remainder_ms * frequency + 999U) / 1000U;
    if (whole_ticks > 0xFFFFFFFFU - partial_ticks) return 0xFFFFFFFFU;
    whole_ticks += partial_ticks;
    return whole_ticks ? whole_ticks : 1U;
}

static int uhci_timeout_expired(uint32_t start, uint32_t milliseconds) {
    return (uint32_t)(timer_get_ticks() - start) >=
           uhci_timeout_ticks(milliseconds);
}

static uint16_t uhci_port_offset(uint32_t port) {
    return (uint16_t)(UHCI_PORT_STATUS_BASE +
                      port * UHCI_PORT_STATUS_STRIDE);
}

static uhci_controller_t* uhci_find(uint8_t bus, uint8_t device,
                                    uint8_t function) {
    for (uint32_t index = 0; index < UHCI_CONTROLLER_CAPACITY; index++) {
        uhci_controller_t* controller = &uhci_controllers[index];

        if (controller->used && controller->bus == bus &&
            controller->device == device && controller->function == function) {
            return controller;
        }
    }
    return 0;
}

static uhci_device_record_t* uhci_find_device_record(
    uhci_controller_t* controller, const usb_device_info_t* device) {
    if (!controller || !device || device->controller_bus != controller->bus ||
        device->controller_device != controller->device ||
        device->controller_function != controller->function) return 0;
    for (uint32_t index = 0U; index < USB_UHCI_PORT_COUNT; index++) {
        uhci_device_record_t* record = &controller->devices[index];

        if (record->valid_descriptors &&
            record->info.usb_address == device->usb_address &&
            record->info.port_number == device->port_number) return record;
    }
    return 0;
}

static uhci_device_record_t* uhci_get_device_record_at(
    uhci_controller_t* controller, uint32_t index) {
    uint32_t found = 0U;

    if (!controller) return 0;
    for (uint32_t port = 0U; port < USB_UHCI_PORT_COUNT; port++) {
        uhci_device_record_t* record = &controller->devices[port];

        if (!record->valid_descriptors) continue;
        if (found == index) return record;
        found++;
    }
    return 0;
}

static uint32_t uhci_count_valid_devices(const uhci_controller_t* controller) {
    uint32_t count = 0U;

    if (!controller) return 0U;
    for (uint32_t port = 0U; port < USB_UHCI_PORT_COUNT; port++) {
        if (controller->devices[port].valid_descriptors) count++;
    }
    return count;
}

static uhci_controller_t* uhci_allocate(void) {
    for (uint32_t index = 0; index < UHCI_CONTROLLER_CAPACITY; index++) {
        if (!uhci_controllers[index].used) {
            kmemset(&uhci_controllers[index], 0,
                    sizeof(uhci_controllers[index]));
            uhci_controllers[index].used = 1;
            uhci_controllers[index].next_address = 1U;
            spinlock_init(&uhci_controllers[index].transfer_lock);
            return &uhci_controllers[index];
        }
    }
    return 0;
}

static void uhci_release_dma(uhci_controller_t* controller) {
    if (controller->buffer_pool) {
        pmm_free_page(controller->buffer_pool);
        controller->buffer_pool = 0;
    }
    if (controller->td_pool) {
        pmm_free_page(controller->td_pool);
        controller->td_pool = 0;
    }
    if (controller->queue_head) {
        pmm_free_page(controller->queue_head);
        controller->queue_head = 0;
    }
    if (controller->frame_list) {
        pmm_free_page(controller->frame_list);
        controller->frame_list = 0;
    }
}

static void uhci_disable(uhci_controller_t* controller) {
    if (!controller || !controller->io_base) return;
    uhci_out16(controller, UHCI_USBINTR, 0U);
    uhci_out16(controller, UHCI_USBCMD, 0U);
    uhci_out16(controller, UHCI_USBSTS, UHCI_USBSTS_RELEVANT);
}

static int uhci_validate_pci(const pci_device_t* pci, uint16_t* out_io) {
    const uint32_t bars[USB_CONTROLLER_BAR_COUNT] = {
        pci ? pci->bar0 : 0U,
        pci ? pci->bar1 : 0U,
        pci ? pci->bar2 : 0U,
        pci ? pci->bar3 : 0U,
        pci ? pci->bar4 : 0U,
        pci ? pci->bar5 : 0U
    };
    uint32_t io_base;
    uint32_t bar_index;

    if (!pci || !out_io) {
        LOG_ERROR("UHCI", "Argumento nulo ao validar PCI UHCI");
        return ERR_NULL;
    }
    if (pci->class != USB_CONTROLLER_PCI_CLASS ||
        pci->subclass != USB_CONTROLLER_PCI_SUBCLASS ||
        pci->prog_if != USB_CONTROLLER_PROG_IF_UHCI ||
        pci->irq == USB_CONTROLLER_IRQ_UNKNOWN || pci->irq > UHCI_IRQ_MAX) {
        LOG_ERROR("UHCI", "Capacidade PCI UHCI invalida");
        return ERR_UNAVAILABLE;
    }
    for (bar_index = 0U; bar_index < USB_CONTROLLER_BAR_COUNT; bar_index++) {
        if (!(bars[bar_index] & UHCI_PCI_BAR_IO)) continue;
        io_base = bars[bar_index] & UHCI_PCI_BAR_ADDRESS_MASK;
        if (io_base && io_base <= 0xFFFFU &&
            io_base + UHCI_IO_SPACE_BYTES <= 0x10000U) {
            *out_io = (uint16_t)io_base;
            return OK;
        }
    }
    LOG_ERROR("UHCI", "Nenhum BAR I/O UHCI valido encontrado");
    return ERR_UNAVAILABLE;
}

static int uhci_allocate_dma(uhci_controller_t* controller) {
    controller->frame_list = (uint32_t*)pmm_alloc_page();
    controller->queue_head = (uhci_qh_t*)pmm_alloc_page();
    controller->td_pool = (uhci_td_t*)pmm_alloc_page();
    controller->buffer_pool = (uint8_t*)pmm_alloc_page();
    if (!controller->frame_list || !controller->queue_head ||
        !controller->td_pool || !controller->buffer_pool) {
        LOG_ERROR("UHCI", "Falha ao alocar estruturas DMA UHCI");
        uhci_release_dma(controller);
        return ERR_MEM;
    }
    controller->frame_list_phys = (uint32_t)controller->frame_list;
    controller->queue_head_phys = (uint32_t)controller->queue_head;
    controller->td_pool_phys = (uint32_t)controller->td_pool;
    controller->buffer_pool_phys = (uint32_t)controller->buffer_pool;
    if ((controller->frame_list_phys & 0xFFFU) ||
        (controller->queue_head_phys & 0xFU) ||
        (controller->td_pool_phys & 0xFU) ||
        (controller->buffer_pool_phys & 0xFFFU)) {
        LOG_ERROR("UHCI", "Estrutura DMA UHCI desalinhada");
        uhci_release_dma(controller);
        return ERR_INVALID;
    }
    kmemset(controller->frame_list, 0, PAGE_SIZE);
    kmemset(controller->queue_head, 0, PAGE_SIZE);
    kmemset(controller->td_pool, 0, PAGE_SIZE);
    kmemset(controller->buffer_pool, 0, PAGE_SIZE);
    kmemset(controller->frame_owner, 0xFF, sizeof(controller->frame_owner));
    for (uint32_t index = 0; index < USB_UHCI_FRAME_COUNT; index++) {
        controller->frame_list[index] = UHCI_PTR_QH |
                                         controller->queue_head_phys;
    }
    controller->queue_head->link = UHCI_PTR_TERM;
    controller->queue_head->element = UHCI_PTR_TERM;
    uhci_update_resource_usage(controller);
    return OK;
}

static int uhci_reset_controller(uhci_controller_t* controller) {
    uint32_t start;

    uhci_out16(controller, UHCI_USBINTR, 0U);
    uhci_out16(controller, UHCI_USBCMD, UHCI_USBCMD_HOST_RESET);
    start = timer_get_ticks();
    while (uhci_in16(controller, UHCI_USBCMD) & UHCI_USBCMD_HOST_RESET) {
        if (uhci_timeout_expired(start, UHCI_RESET_TIMEOUT_MS)) {
            LOG_ERROR("UHCI", "Timeout no reset do controlador UHCI");
            return ERR_TIMEOUT;
        }
        asm volatile("pause");
    }
    uhci_out16(controller, UHCI_USBCMD, 0U);
    uhci_out16(controller, UHCI_USBSTS, UHCI_USBSTS_RELEVANT);
    uhci_out16(controller, UHCI_FRNUM, 0U);
    uhci_out32(controller, UHCI_FLBASEADD, controller->frame_list_phys);
    uhci_out8(controller, UHCI_SOFMOD, 64U);
    return OK;
}

static int uhci_start_controller(uhci_controller_t* controller) {
    uint16_t command = UHCI_USBCMD_RUN_STOP | UHCI_USBCMD_CONFIG_FLAG |
                       UHCI_USBCMD_MAX_PACKET;

    uhci_out16(controller, UHCI_USBINTR, UHCI_USBINTR_ALL);
    uhci_out16(controller, UHCI_USBCMD, command);
    for (uint32_t attempt = 0; attempt < 1000U; attempt++) {
        if (!(uhci_in16(controller, UHCI_USBSTS) & 0x0020U)) return OK;
        asm volatile("pause");
    }
    LOG_ERROR("UHCI", "Controlador UHCI permaneceu parado");
    return ERR_UNAVAILABLE;
}

static int uhci_recover_controller(uhci_controller_t* controller) {
    uint32_t start = timer_get_ticks();
    int result;

    controller->recovery_count++;
    uhci_out16(controller, UHCI_USBINTR, 0U);
    uhci_out16(controller, UHCI_USBCMD, 0U);
    while (!(uhci_in16(controller, UHCI_USBSTS) & UHCI_USBSTS_HCH) &&
           !uhci_timeout_expired(start, UHCI_RECOVERY_TIMEOUT_MS)) {
        asm volatile("pause");
    }
    result = uhci_reset_controller(controller);
    if (result == OK) result = uhci_start_controller(controller);
    if (result != OK) {
        controller->running = 0U;
        controller->last_error = result;
        LOG_ERROR("UHCI", "Recuperacao do controlador UHCI falhou");
        return result;
    }
    controller->running = 1U;
    controller->last_error = OK;
    return OK;
}

static void uhci_make_device_id(const uhci_controller_t* controller,
                                uint32_t port, uint8_t address,
                                char* output, uint32_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t offset = 0;
    uint32_t values[5] = { controller->bus, controller->device,
                           controller->function, port + 1U, address };
    uint32_t widths[5] = { 2U, 2U, 1U, 1U, 1U };
    const char* prefix = "usb-dev-";

    if (!output || !capacity) return;
    output[0] = '\0';
    while (*prefix && offset + 1U < capacity) output[offset++] = *prefix++;
    for (uint32_t value_index = 0; value_index < 5U; value_index++) {
        uint32_t width = widths[value_index];
        if (value_index == 1U) output[offset++] = ':';
        else if (value_index == 2U) output[offset++] = '.';
        else if (value_index == 3U) {
            output[offset++] = '-';
            output[offset++] = 'p';
        } else if (value_index == 4U) {
            output[offset++] = '-';
            output[offset++] = 'a';
        }
        while (width > 0U && offset + 1U < capacity) {
            uint32_t shift = (width - 1U) * 4U;
            output[offset++] = hex[(values[value_index] >> shift) & 0xFU];
            width--;
        }
    }
    output[offset] = '\0';
}

static void uhci_log_configuration_preview(
    const uhci_device_record_t* record) {
    static const char hex[] = "0123456789ABCDEF";
    static const char prefix[] = "Config USB bytes:";
    char message[LOG_MESSAGE_CAPACITY];
    uint32_t offset = 0U;
    uint32_t count;

    if (!record) return;
    for (uint32_t index = 0U;
         prefix[index] && offset + 1U < LOG_MESSAGE_CAPACITY; index++) {
        message[offset++] = prefix[index];
    }
    count = record->configuration_length < UHCI_DIAGNOSTIC_PREVIEW_BYTES ?
            record->configuration_length : UHCI_DIAGNOSTIC_PREVIEW_BYTES;
    for (uint32_t index = 0U; index < count && offset + 3U <
         LOG_MESSAGE_CAPACITY; index++) {
        uint8_t value = record->configuration_descriptor[index];

        message[offset++] = ' ';
        message[offset++] = hex[value >> 4U];
        message[offset++] = hex[value & 0x0FU];
    }
    message[offset] = '\0';
    LOG_WARN("UHCI", message);
}

static void uhci_log_device_preview(const uhci_device_record_t* record) {
    static const char hex[] = "0123456789ABCDEF";
    static const char prefix[] = "Device USB bytes:";
    char message[LOG_MESSAGE_CAPACITY];
    uint32_t offset = 0U;

    if (!record || !record->info.device_descriptor_valid) return;
    for (uint32_t index = 0U;
         prefix[index] && offset + 1U < LOG_MESSAGE_CAPACITY; index++) {
        message[offset++] = prefix[index];
    }
    for (uint32_t index = 0U; index < UHCI_DEVICE_DESCRIPTOR_LENGTH &&
         offset + 3U < LOG_MESSAGE_CAPACITY; index++) {
        uint8_t value = record->device_descriptor[index];

        message[offset++] = ' ';
        message[offset++] = hex[value >> 4U];
        message[offset++] = hex[value & 0x0FU];
    }
    message[offset] = '\0';
    LOG_WARN("UHCI", message);
}

static void uhci_set_port_empty(uhci_controller_t* controller,
                                uint32_t port) {
    usb_port_info_t* info = &controller->ports[port];

    kmemset(info, 0, sizeof(*info));
    kmemcpy(info->controller_id, controller->controller_id,
            USB_CONTROLLER_ID_SIZE);
    info->controller_bus = controller->bus;
    info->controller_device = controller->device;
    info->controller_function = controller->function;
    info->port_number = (uint8_t)(port + 1U);
    info->controller_model = USB_CONTROLLER_MODEL_UHCI;
    info->state = USB_PORT_EMPTY;
    info->reason = USB_PORT_REASON_NO_DEVICE;
    info->speed = USB_DEVICE_SPEED_FULL;
}

static int uhci_reset_port(uhci_controller_t* controller, uint32_t port) {
    uint16_t offset = uhci_port_offset(port);
    uint16_t value = uhci_in16(controller, offset);
    uint32_t start;

    uhci_out16(controller, offset, (uint16_t)(value | UHCI_PORT_PR));
    start = timer_get_ticks();
    while (!uhci_timeout_expired(start, UHCI_RESET_TIMEOUT_MS / 2U)) {
        asm volatile("pause");
    }
    value = uhci_in16(controller, offset);
    value = (uint16_t)((value & (UHCI_PORT_CCS | UHCI_PORT_LSDA)) |
                       UHCI_PORT_PE | UHCI_PORT_CSC | UHCI_PORT_PEC);
    uhci_out16(controller, offset, value);
    start = timer_get_ticks();
    while (!uhci_timeout_expired(start, UHCI_SET_ADDRESS_DELAY_MS * 5U)) {
        asm volatile("pause");
    }
    value = uhci_in16(controller, offset);
    if (!(value & UHCI_PORT_CCS) || !(value & UHCI_PORT_PE)) {
        LOG_ERROR("UHCI", "Reset de porta UHCI nao habilitou dispositivo");
        return ERR_TIMEOUT;
    }
    return OK;
}

static uint32_t uhci_token(uint8_t pid, uint8_t address, uint8_t endpoint,
                           uint8_t toggle, uint16_t length) {
    uint32_t encoded_length = length ? (uint32_t)(length - 1U) : 0x7FFU;

    return pid | ((uint32_t)address << UHCI_TD_TOKEN_ADDRESS_SHIFT) |
           ((uint32_t)endpoint << UHCI_TD_TOKEN_ENDPOINT_SHIFT) |
           ((uint32_t)toggle << UHCI_TD_TOGGLE_SHIFT) |
           ((encoded_length & UHCI_TD_TOKEN_MAX_LENGTH_MASK) <<
            UHCI_TD_LENGTH_SHIFT);
}

static uint32_t uhci_td_status(uint8_t low_speed, uint8_t interrupt) {
    uint32_t status = UHCI_TD_ACTIVE | (3U << UHCI_TD_ERROR_COUNT_SHIFT);

    if (low_speed) status |= UHCI_TD_LOW_SPEED;
    if (interrupt) status |= UHCI_TD_IOC;
    return status;
}

static uint32_t uhci_td_actual(uint32_t status) {
    uint32_t length = status & UHCI_TD_LENGTH_MASK;

    return length == UHCI_TD_LENGTH_MASK ? 0U : length + 1U;
}

static int uhci_wait_transfer(uhci_controller_t* controller,
                              uint32_t td_count, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();

    while (1) {
        uint32_t active = 0;
        uint32_t errors = 0;

        for (uint32_t index = 0; index < td_count; index++) {
            uint32_t status = controller->td_pool[index].status;

            if (status & UHCI_TD_ACTIVE) active++;
            errors |= status & UHCI_TD_ERROR_MASK;
        }
        if (!active) {
            if (errors) {
                controller->recovery_count++;
                LOG_ERROR("UHCI", "Transferencia UHCI retornou erro");
                return ERR_STATE;
            }
            return OK;
        }
        if (uhci_timeout_expired(start, timeout_ms)) {
            controller->timeout_count++;
            LOG_ERROR("UHCI", "Timeout em transferencia UHCI");
            return ERR_TIMEOUT;
        }
        asm volatile("pause");
    }
}

static int uhci_valid_control_packet_size(uint8_t max_packet_size) {
    return max_packet_size >= UHCI_DEVICE_MAX_PACKET_MIN &&
           max_packet_size <= UHCI_DEVICE_MAX_PACKET_MAX &&
           (max_packet_size == 8U || max_packet_size == 16U ||
            max_packet_size == 32U || max_packet_size == 64U);
}

static void uhci_control_release_sync(uhci_controller_t* controller) {
    if (!controller) return;
    controller->sync_td_in_use = 0U;
    controller->sync_buffer_in_use = 0U;
    uhci_update_resource_usage(controller);
}

static int uhci_control_transfer(uhci_controller_t* controller,
                                 uint8_t address, uint8_t low_speed,
                                 uint8_t max_packet_size,
                                 uint8_t request_type, uint8_t request,
                                 uint16_t value, uint16_t index,
                                 uint16_t length, uint8_t* data,
                                 uint16_t* out_length) {
    uint8_t* setup;
    uint8_t* transfer_data;
    uint32_t data_td_count = 0U;
    uint32_t td_count;
    uint32_t copied = 0U;
    uint32_t actual_total = 0U;
    uint8_t data_in = request_type & UHCI_REQUEST_DEVICE_TO_HOST;
    int result;

    if (!controller || !controller->running ||
        length > USB_UHCI_DESCRIPTOR_BUFFER_SIZE ||
        (length && !data) || !uhci_valid_control_packet_size(max_packet_size)) {
        LOG_ERROR("UHCI", "Argumento invalido na transferencia de controle");
        return !controller || !controller->running ? ERR_STATE : ERR_INVALID;
    }
    if (length) {
        data_td_count = (length + max_packet_size - 1U) / max_packet_size;
        td_count = data_td_count + 2U;
    } else {
        td_count = 2U;
    }
    if (td_count > UHCI_SYNC_TD_CAPACITY) {
        LOG_ERROR("UHCI", "Transferencia de controle excede os TDs UHCI");
        return ERR_OVERFLOW;
    }
    if (out_length) *out_length = 0U;
    setup = controller->buffer_pool +
                     UHCI_CONTROL_BUFFER_SETUP *
                     USB_UHCI_DESCRIPTOR_BUFFER_SIZE;
    transfer_data = controller->buffer_pool +
                              UHCI_CONTROL_BUFFER_DATA_OFFSET;
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8U);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8U);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8U);
    if (length && !data_in) {
        kmemcpy(transfer_data, data, length);
    }
    kmemset(controller->td_pool, 0, td_count * UHCI_TD_SIZE);
    controller->sync_td_in_use = td_count;
    controller->sync_buffer_in_use = length ? 2U : 1U;
    uhci_update_resource_usage(controller);
    for (uint32_t td = 0; td < td_count; td++) {
        uint32_t next = td + 1U < td_count ? controller->td_pool_phys +
                     (td + 1U) * UHCI_TD_SIZE : UHCI_PTR_TERM;
        controller->td_pool[td].link = next;
    }
    controller->td_pool[0].status = uhci_td_status(low_speed, !length);
    controller->td_pool[0].token = uhci_token(UHCI_PID_SETUP, address, 0U, 0U,
                                               UHCI_SETUP_SIZE);
    controller->td_pool[0].buffer = controller->buffer_pool_phys;
    if (length) {
        copied = 0U;
        for (uint32_t td = 0U; td < data_td_count; td++) {
            uint32_t remaining = length - copied;
            uint16_t packet = remaining > max_packet_size ? max_packet_size :
                              (uint16_t)remaining;
            uint32_t status = uhci_td_status(low_speed, 0U);

            if (data_in) status |= UHCI_TD_SPD;
            controller->td_pool[td + 1U].status = status;
            controller->td_pool[td + 1U].token = uhci_token(
                data_in ? UHCI_PID_IN : UHCI_PID_OUT, address, 0U,
                (uint8_t)(1U ^ (td & 1U)), packet);
            controller->td_pool[td + 1U].buffer =
                controller->buffer_pool_phys + UHCI_CONTROL_BUFFER_DATA_OFFSET +
                copied;
            copied += packet;
        }
        controller->td_pool[td_count - 1U].status =
            uhci_td_status(low_speed, 1U);
        controller->td_pool[td_count - 1U].token = uhci_token(
            data_in ? UHCI_PID_OUT : UHCI_PID_IN, address, 0U, 1U, 0U);
    } else {
        controller->td_pool[1].status = uhci_td_status(low_speed, 1U);
        controller->td_pool[1].token = uhci_token(
            data_in ? UHCI_PID_OUT : UHCI_PID_IN, address, 0U, 1U, 0U);
    }
    controller->queue_head->element = controller->td_pool_phys;
    asm volatile("" : : : "memory");
    result = uhci_wait_transfer(controller, td_count, UHCI_CONTROL_TIMEOUT_MS);
    controller->queue_head->element = UHCI_PTR_TERM;
    if (result != OK) {
        uhci_recover_controller(controller);
        uhci_control_release_sync(controller);
        return result;
    }
    if (length && data_in) {
        copied = 0U;
        for (uint32_t td = 0U; td < data_td_count; td++) {
            uint32_t remaining = length - copied;
            uint16_t packet = remaining > max_packet_size ? max_packet_size :
                              (uint16_t)remaining;
            uint32_t actual = uhci_td_actual(
                controller->td_pool[td + 1U].status);

            if (actual > packet) actual = packet;
            kmemcpy(data + copied, transfer_data + copied, actual);
            copied += actual;
            actual_total += actual;
            if (actual < packet) break;
        }
        if (out_length) *out_length = (uint16_t)actual_total;
    }
    uhci_control_release_sync(controller);
    return OK;
}

static int uhci_read_descriptor(uhci_controller_t* controller, uint8_t address,
                                uint8_t low_speed, uint8_t max_packet_size,
                                uint8_t type,
                                uint8_t* data, uint16_t length) {
    uint16_t actual = 0;
    int result;

    if (!data || !length) {
        LOG_ERROR("UHCI", "Buffer invalido ao ler descritor USB");
        return ERR_INVALID;
    }
    result = uhci_control_transfer(
        controller, address, low_speed, max_packet_size,
        UHCI_REQUEST_DEVICE_TO_HOST | UHCI_REQUEST_STANDARD |
        UHCI_REQUEST_RECIPIENT_DEVICE, UHCI_REQUEST_GET_DESCRIPTOR,
        (uint16_t)(type << 8U), 0U, length, data, &actual);

    if (result != OK) return result;
    if (actual < 2U || data[1] != type) {
        LOG_ERROR("UHCI", "Tipo de descritor USB inesperado");
        return ERR_INVALID;
    }
    if (actual < length) {
        LOG_ERROR("UHCI", "Descritor USB incompleto");
        return ERR_INVALID;
    }
    return OK;
}

static int uhci_parse_configuration(uhci_device_record_t* record,
                                     uhci_enum_context_t* context) {
    uint8_t* data;
    uint32_t offset = 0;
    uint32_t configuration_count = 0;
    uint32_t interface_count = 0;
    uint32_t endpoint_count = 0;
    uint8_t interface_seen = 0;

    if (!record || record->configuration_length < UHCI_CONFIG_DESCRIPTOR_LENGTH) {
        LOG_ERROR("UHCI", "Descritor Configuration ausente");
        return ERR_INVALID;
    }
    data = record->configuration_descriptor;
    while (offset < record->configuration_length) {
        uint8_t length;
        uint8_t type;

        if (offset + 2U > record->configuration_length) {
            LOG_ERROR("UHCI", "Descritor USB truncado");
            return ERR_INVALID;
        }
        length = data[offset];
        type = data[offset + 1U];
        if (length < 2U || offset + length > record->configuration_length) {
            LOG_ERROR("UHCI", "Cadeia de descritores USB invalida");
            return ERR_INVALID;
        }
        if (type == UHCI_DESCRIPTOR_TYPE_CONFIGURATION) {
            if (length < UHCI_CONFIG_DESCRIPTOR_LENGTH) {
                LOG_ERROR("UHCI", "Descritor Configuration curto");
                return ERR_INVALID;
            }
            configuration_count++;
            record->info.configuration_value = data[offset +
                                                   UHCI_CONFIG_VALUE_OFFSET];
        } else if (type == UHCI_DESCRIPTOR_TYPE_INTERFACE) {
            if (length < UHCI_INTERFACE_DESCRIPTOR_LENGTH) {
                LOG_ERROR("UHCI", "Descritor Interface USB curto");
                return ERR_INVALID;
            }
            if (interface_seen) {
                if (context) {
                    context->failure_detail =
                        "Configuration USB possui mais de uma interface";
                }
                LOG_ERROR_CODE("UHCI", ERR_UNAVAILABLE,
                               "Configuration USB possui mais de uma interface");
                return ERR_UNAVAILABLE;
            }
            if (data[offset + UHCI_INTERFACE_ALTERNATE_OFFSET] != 0U) {
                if (context) {
                    context->failure_detail =
                        "Interface USB usa alternate setting";
                }
                LOG_ERROR_CODE("UHCI", ERR_UNAVAILABLE,
                               "Interface USB usa alternate setting");
                return ERR_UNAVAILABLE;
            }
            interface_seen = 1U;
            interface_count++;
            record->info.interface_number = data[offset +
                                                 UHCI_INTERFACE_NUMBER_OFFSET];
            record->info.interface_class = data[offset +
                                               UHCI_INTERFACE_CLASS_OFFSET];
            record->info.interface_subclass = data[offset +
                                                  UHCI_INTERFACE_SUBCLASS_OFFSET];
            record->info.interface_protocol = data[offset +
                                                  UHCI_INTERFACE_PROTOCOL_OFFSET];
        } else if (type == UHCI_DESCRIPTOR_TYPE_ENDPOINT) {
            uint16_t max_packet;
            uint8_t endpoint_address;
            uint8_t transfer_type;

            if (length < UHCI_ENDPOINT_DESCRIPTOR_LENGTH || !interface_seen) {
                LOG_ERROR("UHCI", "Endpoint USB fora de uma interface");
                return ERR_INVALID;
            }
            endpoint_address = data[offset + UHCI_ENDPOINT_ADDRESS_OFFSET];
            transfer_type = data[offset + 3U] & 0x03U;
            max_packet = (uint16_t)(data[offset + UHCI_ENDPOINT_MAX_PACKET_OFFSET] |
                          ((uint16_t)data[offset + UHCI_ENDPOINT_MAX_PACKET_OFFSET +
                                            1U] << 8U));
            if (!(endpoint_address & USB_ENDPOINT_ADDRESS_NUMBER_MASK)) {
                LOG_ERROR("UHCI", "Endpoint USB zero invalido");
                return ERR_INVALID;
            }
            if (!max_packet || max_packet > USB_ENDPOINT_MAX_PACKET_SIZE_FULL) {
                LOG_ERROR("UHCI", "Tamanho de pacote USB invalido");
                return ERR_INVALID;
            }
            if (endpoint_count >= USB_DEVICE_MAX_ENDPOINTS) {
                if (context) {
                    context->failure_detail =
                        "Configuration USB excede a tabela de endpoints";
                }
                LOG_ERROR_CODE("UHCI", ERR_OVERFLOW,
                               "Configuration USB excede a tabela de endpoints");
                return ERR_OVERFLOW;
            }
            record->info.endpoints[endpoint_count].address = endpoint_address;
            record->info.endpoints[endpoint_count].transfer_type =
                transfer_type;
            record->info.endpoints[endpoint_count].max_packet = max_packet;
            record->info.endpoints[endpoint_count].interval =
                data[offset + UHCI_ENDPOINT_INTERVAL_OFFSET];
            endpoint_count++;
            if (transfer_type == 2U &&
                (max_packet == 8U || max_packet == 16U ||
                 max_packet == 32U || max_packet == 64U)) {
                if (endpoint_address & 0x80U) {
                    record->info.bulk_in_count++;
                    record->info.bulk_in_endpoint = endpoint_address;
                    record->info.bulk_in_max_packet = max_packet;
                } else {
                    record->info.bulk_out_count++;
                    record->info.bulk_out_endpoint = endpoint_address;
                    record->info.bulk_out_max_packet = max_packet;
                }
            } else if (transfer_type == 3U && (endpoint_address & 0x80U)) {
                if (!data[offset + UHCI_ENDPOINT_INTERVAL_OFFSET]) {
                    LOG_ERROR("UHCI", "Intervalo Interrupt USB invalido");
                    return ERR_INVALID;
                }
                record->info.interrupt_in_count++;
                if (!record->info.interrupt_in_endpoint) {
                    record->info.interrupt_in_endpoint = endpoint_address;
                    record->info.interrupt_in_max_packet = max_packet;
                    record->info.interrupt_interval =
                        data[offset + UHCI_ENDPOINT_INTERVAL_OFFSET];
                }
            }
        }
        offset += length;
    }
    if (configuration_count != 1U) {
        if (context) {
            context->failure_detail =
                "Configuration USB sem descritor unico";
        }
        LOG_ERROR_CODE("UHCI", ERR_UNAVAILABLE,
                       "Configuration USB sem descritor unico");
        return ERR_UNAVAILABLE;
    }
    if (interface_count != UHCI_INTERFACE_COUNT_LIMIT || !interface_seen) {
        if (context) {
            context->failure_detail =
                "Configuration USB sem interface Boot unica";
        }
        LOG_ERROR_CODE("UHCI", ERR_UNAVAILABLE,
                       "Configuration USB sem interface Boot unica");
        return ERR_UNAVAILABLE;
    }
    if (!endpoint_count && record->info.interface_class != 0U) {
        if (context) {
            context->failure_detail =
                "Interface USB possui classe sem endpoint";
        }
        LOG_ERROR_CODE("UHCI", ERR_UNAVAILABLE,
                       "Interface USB possui classe sem endpoint");
        return ERR_UNAVAILABLE;
    }
    record->info.endpoint_count = (uint8_t)endpoint_count;
    record->info.hub_present =
        record->info.interface_class == 0x09U ? 1U : 0U;
    if (record->info.hub_present) {
        if (context) {
            context->failure_detail =
                "Hubs USB nao sao suportados nesta etapa";
        }
        LOG_ERROR_CODE("UHCI", ERR_UNAVAILABLE,
                       "Hubs USB nao sao suportados nesta etapa");
        return ERR_UNAVAILABLE;
    }
    record->valid_descriptors = 1U;
    return OK;
}

static void uhci_log_enumeration_failure(
    uint32_t port, const uhci_enum_context_t* context, int result) {
    if (port == 0U) {
        LOG_WARN_CODE("UHCI", result,
                      "Falha na enumeracao UHCI da porta 1");
    } else if (port == 1U) {
        LOG_WARN_CODE("UHCI", result,
                      "Falha na enumeracao UHCI da porta 2");
    } else {
        LOG_WARN_CODE("UHCI", result,
                      "Falha na enumeracao UHCI de porta desconhecida");
    }
    if (!context) return;
    switch (context->stage) {
        case UHCI_ENUM_STAGE_RESET:
            LOG_WARN_CODE("UHCI", result, "Etapa UHCI: reset da porta");
            break;
        case UHCI_ENUM_STAGE_DEVICE_PREFIX:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: Device Descriptor inicial");
            break;
        case UHCI_ENUM_STAGE_SET_ADDRESS:
            LOG_WARN_CODE("UHCI", result, "Etapa UHCI: SET_ADDRESS");
            break;
        case UHCI_ENUM_STAGE_ADDRESS_SETTLE:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: espera apos SET_ADDRESS");
            break;
        case UHCI_ENUM_STAGE_DEVICE_FULL:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: Device Descriptor completo");
            break;
        case UHCI_ENUM_STAGE_CONFIGURATION_HEADER:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: Configuration Descriptor inicial");
            break;
        case UHCI_ENUM_STAGE_CONFIGURATION_FULL:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: Configuration Descriptor completo");
            break;
        case UHCI_ENUM_STAGE_CONFIGURATION_PARSE:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: parsing da configuracao");
            break;
        case UHCI_ENUM_STAGE_SET_CONFIGURATION:
            LOG_WARN_CODE("UHCI", result, "Etapa UHCI: SET_CONFIGURATION");
            break;
        case UHCI_ENUM_STAGE_PUBLISH:
            LOG_WARN_CODE("UHCI", result,
                          "Etapa UHCI: publicacao do dispositivo");
            break;
        default:
            LOG_WARN_CODE("UHCI", result, "Etapa UHCI desconhecida");
            break;
    }
    if (context->failure_detail) {
        LOG_WARN_CODE("UHCI", result, context->failure_detail);
    }
}

static int uhci_enumerate_port_once(uhci_controller_t* controller,
                                    uint32_t port,
                                    uhci_enum_context_t* context) {
    uhci_device_record_t* record = &controller->devices[port];
    usb_port_info_t* port_info = &controller->ports[port];
    uint8_t first_descriptor[8] = {0};
    uint8_t address;
    uint16_t actual = 0U;
    uint16_t total_length;
    int result;

    context->stage = UHCI_ENUM_STAGE_RESET;
    port_info->state = USB_PORT_RESETTING;
    result = uhci_reset_port(controller, port);
    if (result != OK) return result;
    port_info->connected = 1U;
    port_info->enabled = 1U;
    port_info->speed = (uhci_in16(controller, uhci_port_offset(port)) &
                        UHCI_PORT_LSDA) ? USB_DEVICE_SPEED_LOW :
                        USB_DEVICE_SPEED_FULL;
    context->speed = port_info->speed;
    context->max_packet_size0 = UHCI_CONTROL_DEFAULT_MAX_PACKET;
    port_info->state = USB_PORT_ENUMERATING;

    context->stage = UHCI_ENUM_STAGE_DEVICE_PREFIX;
    result = uhci_read_descriptor(
        controller, 0U, port_info->speed == USB_DEVICE_SPEED_LOW,
        UHCI_CONTROL_DEFAULT_MAX_PACKET, UHCI_DESCRIPTOR_DEVICE,
        first_descriptor, sizeof(first_descriptor));
    if (result != OK) return result;
    if (first_descriptor[0] < sizeof(first_descriptor) ||
        first_descriptor[1] != UHCI_DESCRIPTOR_TYPE_DEVICE ||
        !uhci_valid_control_packet_size(first_descriptor[7])) {
        LOG_ERROR("UHCI", "Device Descriptor inicial invalido");
        return ERR_INVALID;
    }
    context->max_packet_size0 = first_descriptor[7];

    if (!controller->next_address ||
        controller->next_address > UHCI_MAX_USB_ADDRESS) {
        LOG_ERROR("UHCI", "Limite de enderecos USB UHCI atingido");
        return ERR_OVERFLOW;
    }
    address = controller->next_address++;
    context->address = address;
    context->stage = UHCI_ENUM_STAGE_SET_ADDRESS;
    result = uhci_control_transfer(
        controller, 0U, port_info->speed == USB_DEVICE_SPEED_LOW,
        UHCI_CONTROL_DEFAULT_MAX_PACKET,
        UHCI_REQUEST_HOST_TO_DEVICE | UHCI_REQUEST_STANDARD |
        UHCI_REQUEST_RECIPIENT_DEVICE, UHCI_REQUEST_SET_ADDRESS,
        address, 0U, 0U, 0, &actual);
    if (result != OK) return result;

    context->stage = UHCI_ENUM_STAGE_ADDRESS_SETTLE;
    {
        uint32_t start = timer_get_ticks();
        while (!uhci_timeout_expired(start, UHCI_SET_ADDRESS_DELAY_MS)) {
            asm volatile("pause");
        }
    }

    context->stage = UHCI_ENUM_STAGE_DEVICE_FULL;
    result = uhci_read_descriptor(
        controller, address, port_info->speed == USB_DEVICE_SPEED_LOW,
        context->max_packet_size0, UHCI_DESCRIPTOR_DEVICE,
        record->device_descriptor, UHCI_DEVICE_DESCRIPTOR_LENGTH);
    if (result != OK) return result;
    if (record->device_descriptor[0] < UHCI_DEVICE_DESCRIPTOR_LENGTH ||
        record->device_descriptor[1] != UHCI_DESCRIPTOR_TYPE_DEVICE ||
        !uhci_valid_control_packet_size(record->device_descriptor[
            UHCI_DEVICE_MPS_OFFSET])) {
        LOG_ERROR("UHCI", "Device Descriptor completo invalido");
        return ERR_INVALID;
    }
    context->max_packet_size0 = record->device_descriptor[
        UHCI_DEVICE_MPS_OFFSET];
    record->info.usb_address = address;
    record->info.speed = port_info->speed;
    record->info.vendor_id = (uint16_t)(record->device_descriptor[
                              UHCI_DEVICE_VENDOR_OFFSET] |
                              ((uint16_t)record->device_descriptor[
                              UHCI_DEVICE_VENDOR_OFFSET + 1U] << 8U));
    record->info.product_id = (uint16_t)(record->device_descriptor[
                               UHCI_DEVICE_PRODUCT_OFFSET] |
                               ((uint16_t)record->device_descriptor[
                               UHCI_DEVICE_PRODUCT_OFFSET + 1U] << 8U));
    record->info.device_revision = (uint16_t)(record->device_descriptor[
                                  UHCI_DEVICE_REVISION_OFFSET] |
                                  ((uint16_t)record->device_descriptor[
                                  UHCI_DEVICE_REVISION_OFFSET + 1U] << 8U));
    record->info.device_class = record->device_descriptor[
        UHCI_DEVICE_CLASS_OFFSET];
    record->info.device_subclass = record->device_descriptor[
        UHCI_DEVICE_SUBCLASS_OFFSET];
    record->info.device_protocol = record->device_descriptor[
        UHCI_DEVICE_PROTOCOL_OFFSET];
    record->info.max_packet_size0 = context->max_packet_size0;
    record->info.num_configurations = record->device_descriptor[
        UHCI_DEVICE_CONFIG_COUNT_OFFSET];
    record->info.device_descriptor_valid = 1U;
    if (!record->info.num_configurations) {
        LOG_ERROR("UHCI", "Dispositivo USB sem configuracao");
        return ERR_INVALID;
    }

    context->stage = UHCI_ENUM_STAGE_CONFIGURATION_HEADER;
    result = uhci_read_descriptor(
        controller, address, port_info->speed == USB_DEVICE_SPEED_LOW,
        context->max_packet_size0, UHCI_DESCRIPTOR_CONFIGURATION,
        record->configuration_descriptor, UHCI_CONFIGURATION_HEADER_LENGTH);
    if (result != OK) return result;
    total_length = (uint16_t)(record->configuration_descriptor[
                   UHCI_CONFIG_TOTAL_LENGTH_OFFSET] |
                   ((uint16_t)record->configuration_descriptor[
                   UHCI_CONFIG_TOTAL_LENGTH_OFFSET + 1U] << 8U));
    if (record->configuration_descriptor[0] < UHCI_CONFIGURATION_HEADER_LENGTH ||
        record->configuration_descriptor[1] !=
        UHCI_DESCRIPTOR_TYPE_CONFIGURATION ||
        total_length < UHCI_CONFIGURATION_HEADER_LENGTH ||
        total_length > UHCI_MAX_DESCRIPTOR_LENGTH) {
        LOG_ERROR("UHCI", "Cabecalho Configuration USB invalido");
        return ERR_INVALID;
    }

    context->stage = UHCI_ENUM_STAGE_CONFIGURATION_FULL;
    result = uhci_read_descriptor(
        controller, address, port_info->speed == USB_DEVICE_SPEED_LOW,
        context->max_packet_size0, UHCI_DESCRIPTOR_CONFIGURATION,
        record->configuration_descriptor, total_length);
    if (result != OK) return result;
    record->configuration_length = total_length;
    record->info.configuration_length = total_length;

    context->stage = UHCI_ENUM_STAGE_CONFIGURATION_PARSE;
    result = uhci_parse_configuration(record, context);
    if (result != OK) return result;

    context->stage = UHCI_ENUM_STAGE_SET_CONFIGURATION;
    result = uhci_control_transfer(
        controller, address, port_info->speed == USB_DEVICE_SPEED_LOW,
        context->max_packet_size0,
        UHCI_REQUEST_HOST_TO_DEVICE | UHCI_REQUEST_STANDARD |
        UHCI_REQUEST_RECIPIENT_DEVICE, UHCI_REQUEST_SET_CONFIGURATION,
        record->info.configuration_value, 0U, 0U, 0, &actual);
    if (result != OK) return result;

    context->stage = UHCI_ENUM_STAGE_PUBLISH;
    record->info.state = USB_DEVICE_CONFIGURED;
    record->info.configuration_descriptor_valid = 1U;
    record->info.class_driver_active = 0U;
    record->info.controller_bus = controller->bus;
    record->info.controller_device = controller->device;
    record->info.controller_function = controller->function;
    record->info.controller_model = USB_CONTROLLER_MODEL_UHCI;
    record->info.port_number = (uint8_t)(port + 1U);
    kmemcpy(record->info.controller_id, controller->controller_id,
            USB_CONTROLLER_ID_SIZE);
    uhci_make_device_id(controller, port, address, record->info.id,
                        USB_DEVICE_ID_SIZE);
    kmemcpy(port_info->device_id, record->info.id, USB_DEVICE_ID_SIZE);
    port_info->usb_address = address;
    port_info->state = USB_PORT_CONFIGURED;
    port_info->reason = USB_PORT_REASON_NONE;
    record->valid_descriptors = 1U;
    return OK;
}

static int uhci_enumeration_retryable(int result) {
    return result == ERR_TIMEOUT || result == ERR_STATE ||
           result == ERR_INVALID;
}

static usb_port_reason_t uhci_enumeration_reason(
    const uhci_enum_context_t* context, int result) {
    if (context && context->stage == UHCI_ENUM_STAGE_RESET &&
        result == ERR_TIMEOUT) return USB_PORT_REASON_RESET_TIMEOUT;
    if (result == ERR_TIMEOUT) return USB_PORT_REASON_CONTROL_TIMEOUT;
    if (result == ERR_UNAVAILABLE) return USB_PORT_REASON_UNSUPPORTED_LAYOUT;
    if (result == ERR_INVALID) return USB_PORT_REASON_INVALID_DESCRIPTOR;
    return USB_PORT_REASON_DRIVER_FAILURE;
}

static int uhci_enumerate_port(uhci_controller_t* controller, uint32_t port) {
    uhci_device_record_t* record = &controller->devices[port];
    uhci_enum_context_t* context = &controller->enumeration[port];
    usb_port_info_t* port_info = &controller->ports[port];
    uint8_t address_start = controller->next_address;
    int result = ERR_STATE;

    for (uint32_t attempt = 1U; attempt <= UHCI_ENUMERATION_RETRIES;
         attempt++) {
        /* O reset invalida o endereco parcial da tentativa anterior. */
        controller->next_address = address_start;
        uhci_set_port_empty(controller, port);
        kmemset(record, 0, sizeof(*record));
        kmemset(context, 0, sizeof(*context));
        context->active = 1U;
        context->attempt = (uint8_t)attempt;
        context->max_packet_size0 = UHCI_CONTROL_DEFAULT_MAX_PACKET;
        context->stage = UHCI_ENUM_STAGE_RESET;
        result = uhci_enumerate_port_once(controller, port, context);
        if (result == OK) {
            context->active = 0U;
            context->last_error = OK;
            return OK;
        }
        context->last_error = result;
        uhci_log_enumeration_failure(port, context, result);
        if (!uhci_enumeration_retryable(result) ||
            attempt == UHCI_ENUMERATION_RETRIES) break;
    }
    port_info->state = USB_PORT_DEGRADED;
    port_info->reason = uhci_enumeration_reason(context, result);
    port_info->connected = 1U;
    port_info->enabled = 0U;
    port_info->usb_address = context->address;
    context->active = 0U;
    controller->port_errors++;
    controller->last_error = result;
    LOG_WARN_CODE("UHCI", result,
                  "Porta UHCI degradada apos tentativas de enumeracao");
    return result;
}

static int uhci_initialize_ports(uhci_controller_t* controller) {
    controller->device_count = 0U;
    controller->port_errors = 0U;
    kmemset(controller->devices, 0, sizeof(controller->devices));
    kmemset(controller->enumeration, 0, sizeof(controller->enumeration));
    for (uint32_t port = 0; port < USB_UHCI_PORT_COUNT; port++) {
        uint16_t status = uhci_in16(controller, uhci_port_offset(port));
        int result;

        uhci_set_port_empty(controller, port);
        if (status == 0xFFFFU) {
            controller->ports[port].state = USB_PORT_DEGRADED;
            controller->ports[port].reason = USB_PORT_REASON_DRIVER_FAILURE;
            controller->port_errors++;
            continue;
        }
        if (!(status & UHCI_PORT_CCS)) continue;
        result = uhci_enumerate_port(controller, port);
        if (result == OK) controller->device_count++;
        else if (controller->last_error == OK) controller->last_error = result;
    }
    return controller->port_errors ? ERR_STATE : OK;
}

static void uhci_irq_handler(registers_t* regs) {
    uint8_t irq_line;

    if (!regs || regs->int_no < 32U || regs->int_no > 47U) return;
    irq_line = (uint8_t)(regs->int_no - 32U);
    for (uint32_t index = 0; index < UHCI_CONTROLLER_CAPACITY; index++) {
        uhci_controller_t* controller = &uhci_controllers[index];
        uint16_t status;

        if (!controller->used || !controller->running ||
            controller->irq != irq_line) continue;
        status = uhci_in16(controller, UHCI_USBSTS);
        if (!status || status == 0xFFFFU) continue;
        uhci_out16(controller, UHCI_USBSTS,
                   (uint16_t)(status & UHCI_USBSTS_RELEVANT));
        controller->irq_pending = 1U;
        controller->irq_events++;
    }
}

static int uhci_init_instance(uhci_controller_t* controller,
                              const pci_device_t* pci) {
    int result;

    result = uhci_validate_pci(pci, &controller->io_base);
    if (result != OK) return result;
    result = pci_enable_io_and_bus_mastering(pci);
    if (result != OK) {
        LOG_ERROR("UHCI", "Falha ao habilitar I/O e bus mastering UHCI");
        return result;
    }
    result = uhci_allocate_dma(controller);
    if (result != OK) return result;
    result = uhci_reset_controller(controller);
    if (result != OK) return result;
    result = idt_register_shared_irq_handler(pci->irq, uhci_irq_handler);
    if (result != OK) {
        LOG_ERROR("UHCI", "Falha ao registrar IRQ compartilhada UHCI");
        return result;
    }
    controller->irq_registered = 1U;
    result = uhci_start_controller(controller);
    if (result != OK) return result;
    controller->initialized = 1U;
    controller->running = 1U;
    controller->last_error = OK;
    result = uhci_initialize_ports(controller);
    if (result != OK) LOG_WARN("UHCI", "Uma ou mais portas UHCI degradadas");
    return OK;
}

int uhci_init(const pci_device_t* pci, const char* controller_id) {
    uhci_controller_t* controller;
    int result;

    LOG_INFO("UHCI", "Inicializando controlador UHCI");
    if (!pci || !controller_id) {
        LOG_ERROR("UHCI", "Argumento nulo na inicializacao UHCI");
        return ERR_NULL;
    }
    controller = uhci_find(pci->bus, pci->device, pci->function);
    if (controller && controller->initialized) return OK;
    if (controller) {
        LOG_ERROR("UHCI", "Instancia UHCI anterior permaneceu invalida");
        return controller->last_error ? controller->last_error : ERR_STATE;
    }
    controller = uhci_allocate();
    if (!controller) {
        LOG_ERROR("UHCI", "Limite de controladores UHCI atingido");
        return ERR_OVERFLOW;
    }
    controller->bus = pci->bus;
    controller->device = pci->device;
    controller->function = pci->function;
    controller->irq = pci->irq;
    kmemcpy(controller->controller_id, controller_id, USB_CONTROLLER_ID_SIZE);
    result = uhci_init_instance(controller, pci);
    if (result != OK) {
        uhci_disable(controller);
        uhci_release_dma(controller);
        kmemset(controller, 0, sizeof(*controller));
        LOG_ERROR("UHCI", "Falha ao inicializar controlador UHCI");
        return result;
    }
    LOG_INFO("UHCI", "Controlador UHCI inicializado com sucesso");
    return OK;
}

static uhci_interrupt_request_t* uhci_find_interrupt_request(
    uhci_controller_t* controller, const usb_device_info_t* device,
    uint8_t endpoint_address) {
    if (!controller || !device) return 0;
    for (uint32_t index = 0U; index < USB_UHCI_INTERRUPT_CAPACITY; index++) {
        uhci_interrupt_request_t* request =
            &controller->interrupt_requests[index];

        if (!request->used || request->endpoint_address != endpoint_address ||
            !request->record || request->record->info.usb_address !=
            device->usb_address || request->record->info.port_number !=
            device->port_number) continue;
        return request;
    }
    return 0;
}

static uhci_interrupt_request_t* uhci_find_free_interrupt_request(
    uhci_controller_t* controller) {
    if (!controller) return 0;
    for (uint32_t index = 0U; index < USB_UHCI_INTERRUPT_CAPACITY; index++) {
        if (!controller->interrupt_requests[index].used &&
            !controller->interrupt_requests[index].completion_work.queued) {
            return &controller->interrupt_requests[index];
        }
    }
    return 0;
}

static void uhci_interrupt_release_frames(uhci_controller_t* controller,
                                          const uhci_interrupt_request_t* request) {
    if (!controller || !request || !request->interval) return;
    for (uint32_t frame = request->phase; frame < USB_UHCI_FRAME_COUNT;
         frame += request->interval) {
        if (controller->frame_owner[frame] == request->qh_index) {
            controller->frame_owner[frame] = 0xFFU;
            controller->frame_list[frame] = UHCI_PTR_QH |
                                             controller->queue_head_phys;
        }
    }
}

static int uhci_interrupt_reserve_frames(
    uhci_controller_t* controller, uhci_interrupt_request_t* request) {
    uint32_t phase;

    if (!controller || !request || !request->interval ||
        request->interval > USB_UHCI_FRAME_COUNT) return ERR_INVALID;
    for (phase = 0U; phase < request->interval; phase++) {
        uint8_t available = 1U;

        for (uint32_t frame = phase; frame < USB_UHCI_FRAME_COUNT;
             frame += request->interval) {
            if (controller->frame_owner[frame] != 0xFFU) {
                available = 0U;
                break;
            }
        }
        if (!available) continue;
        request->phase = (uint8_t)phase;
        for (uint32_t frame = phase; frame < USB_UHCI_FRAME_COUNT;
             frame += request->interval) {
            controller->frame_owner[frame] = request->qh_index;
            controller->frame_list[frame] = UHCI_PTR_QH |
                (controller->queue_head_phys +
                 request->qh_index * UHCI_QH_STRIDE);
        }
        return OK;
    }
    LOG_WARN("UHCI", "Nao ha fase periodica para endpoint Interrupt");
    return ERR_OVERFLOW;
}

static uint8_t* uhci_interrupt_buffer(uhci_controller_t* controller,
                                      const uhci_interrupt_request_t* request) {
    if (!controller || !request || request->buffer_index >=
        USB_UHCI_INTERRUPT_CAPACITY) return 0;
    return controller->buffer_pool + USB_UHCI_INTERRUPT_BUFFER_OFFSET +
           request->buffer_index * USB_UHCI_INTERRUPT_BUFFER_SIZE;
}

static int uhci_interrupt_arm(uhci_interrupt_request_t* request) {
    uhci_controller_t* controller;
    uhci_qh_t* queue_head;
    uhci_td_t* td;
    uint8_t* buffer;

    if (!request || !request->controller || !request->record) {
        return ERR_NULL;
    }
    controller = request->controller;
    queue_head = uhci_queue_head_at(controller, request->qh_index);
    td = &controller->td_pool[request->td_index];
    buffer = uhci_interrupt_buffer(controller, request);
    if (!queue_head || !buffer || !controller->running || !request->used) {
        return ERR_STATE;
    }
    kmemset(buffer, 0, request->max_packet);
    td->link = UHCI_PTR_TERM;
    td->status = uhci_td_status(
        request->record->info.speed == USB_DEVICE_SPEED_LOW, 1U);
    td->token = uhci_token(UHCI_PID_IN, request->record->info.usb_address,
                           request->endpoint_address & 0x0FU,
                           request->toggle, request->max_packet);
    td->buffer = controller->buffer_pool_phys +
                 USB_UHCI_INTERRUPT_BUFFER_OFFSET +
                 request->buffer_index * USB_UHCI_INTERRUPT_BUFFER_SIZE;
    queue_head->link = UHCI_PTR_QH | controller->queue_head_phys;
    queue_head->element = controller->td_pool_phys +
                          request->td_index * UHCI_TD_SIZE;
    request->deadline = timer_get_ticks() +
                        uhci_timeout_ticks(UHCI_INTERRUPT_TIMEOUT_MS);
    request->active = 1U;
    request->completion_pending = 0U;
    request->cancelled = 0U;
    uhci_update_resource_usage(controller);
    asm volatile("" : : : "memory");
    return OK;
}

static int uhci_interrupt_deadline_expired(uint32_t deadline) {
    return (int32_t)(timer_get_ticks() - deadline) >= 0;
}

static void uhci_interrupt_completion(void* context) {
    uhci_interrupt_request_t* request =
        (uhci_interrupt_request_t*)context;
    uhci_controller_t* controller;
    uhci_interrupt_callback_t callback;
    void* callback_context;
    const uint8_t* data;
    uint16_t length;
    int result;
    uint32_t generation;

    if (!request || !request->controller) return;
    controller = request->controller;
    spinlock_acquire(&controller->transfer_lock);
    if (!request->used || !request->completion_pending) {
        spinlock_release(&controller->transfer_lock);
        return;
    }
    callback = request->callback;
    callback_context = request->context;
    data = uhci_interrupt_buffer(controller, request);
    length = request->actual_length;
    result = request->result;
    generation = request->generation;
    request->completion_pending = 0U;
    spinlock_release(&controller->transfer_lock);

    if (callback) callback(callback_context, result, data, length);

    spinlock_acquire(&controller->transfer_lock);
    if (request->used && !request->cancelled &&
        request->generation == generation && controller->running) {
        if (uhci_interrupt_arm(request) != OK) {
            request->used = 0U;
            request->active = 0U;
            uhci_interrupt_release_frames(controller, request);
            controller->interrupt_error_count++;
            controller->last_error = ERR_STATE;
        } else {
            controller->interrupt_active_count++;
        }
    }
    uhci_update_resource_usage(controller);
    spinlock_release(&controller->transfer_lock);
}

static void uhci_interrupt_try_schedule(
    uhci_controller_t* controller, uhci_interrupt_request_t* request) {
    int result;

    if (!controller || !request || !request->completion_pending ||
        request->completion_work.queued) return;
    result = irq_deferred_schedule(&request->completion_work);
    if (result != OK) {
        controller->last_error = result;
        return;
    }
}

static void uhci_scan_interrupt_requests(uhci_controller_t* controller) {
    if (!controller || !controller->running) return;
    spinlock_acquire(&controller->transfer_lock);
    for (uint32_t index = 0U; index < USB_UHCI_INTERRUPT_CAPACITY; index++) {
        uhci_interrupt_request_t* request =
            &controller->interrupt_requests[index];
        uhci_td_t* td;
        uhci_qh_t* queue_head;
        uint32_t status;
        uint32_t actual;

        if (!request->used) continue;
        if (request->completion_pending) {
            uhci_interrupt_try_schedule(controller, request);
            continue;
        }
        if (!request->active) continue;
        td = &controller->td_pool[request->td_index];
        queue_head = uhci_queue_head_at(controller, request->qh_index);
        status = td->status;
        if (status & UHCI_TD_ACTIVE) {
            if (!uhci_interrupt_deadline_expired(request->deadline)) continue;
            request->result = ERR_TIMEOUT;
            request->actual_length = 0U;
            controller->interrupt_timeout_count++;
        } else if (status & UHCI_TD_NAK) {
            /* NAK e' esperado quando o dispositivo ainda nao tem dados. */
            (void)uhci_interrupt_arm(request);
            continue;
        } else if (status & UHCI_TD_ERROR_MASK) {
            request->result = ERR_STATE;
            request->actual_length = 0U;
            request->toggle = 0U;
            controller->interrupt_error_count++;
        } else {
            request->result = OK;
            actual = uhci_td_actual(status);
            if (actual > request->max_packet) actual = request->max_packet;
            request->actual_length = (uint16_t)actual;
            request->toggle ^= 1U;
            controller->interrupt_transfer_count++;
        }
        if (queue_head) queue_head->element = UHCI_PTR_TERM;
        if (controller->interrupt_active_count) {
            controller->interrupt_active_count--;
        }
        request->active = 0U;
        request->completion_pending = 1U;
        uhci_update_resource_usage(controller);
        uhci_interrupt_try_schedule(controller, request);
    }
    spinlock_release(&controller->transfer_lock);
}

int uhci_interrupt_submit(const usb_device_info_t* device,
                          uint8_t endpoint_address, uint16_t max_packet,
                          uint8_t interval,
                          uhci_interrupt_callback_t callback,
                          void* context) {
    uhci_controller_t* controller;
    uhci_device_record_t* record;
    uhci_interrupt_request_t* request;
    int result;

    if (!device || !callback) {
        LOG_ERROR("UHCI", "Argumento nulo ao registrar Interrupt IN");
        return ERR_NULL;
    }
    if (!(endpoint_address & 0x80U) || !(endpoint_address & 0x0FU) ||
        !max_packet || max_packet > USB_UHCI_INTERRUPT_BUFFER_SIZE ||
        !interval) {
        LOG_ERROR("UHCI", "Endpoint Interrupt IN invalido");
        return ERR_INVALID;
    }
    controller = uhci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = uhci_find_device_record(controller, device);
    if (!controller || !record || !controller->running) {
        LOG_ERROR("UHCI", "Dispositivo ausente ao registrar Interrupt IN");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    request = uhci_find_interrupt_request(controller, device,
                                          endpoint_address);
    if (request) {
        request->callback = callback;
        request->context = context;
        spinlock_release(&controller->transfer_lock);
        return OK;
    }
    request = uhci_find_free_interrupt_request(controller);
    if (!request) {
        spinlock_release(&controller->transfer_lock);
        LOG_ERROR("UHCI", "Limite de requisicoes Interrupt IN atingido");
        return ERR_OVERFLOW;
    }
    kmemset(request, 0, sizeof(*request));
    request->used = 1U;
    request->endpoint_address = endpoint_address;
    request->max_packet = (uint8_t)max_packet;
    request->interval = interval;
    request->qh_index = UHCI_INTERRUPT_QH_BASE +
                        (uint8_t)(request - controller->interrupt_requests);
    request->td_index = UHCI_INTERRUPT_TD_BASE +
                        (uint8_t)(request - controller->interrupt_requests);
    request->buffer_index = (uint8_t)(request - controller->interrupt_requests);
    request->controller = controller;
    request->record = record;
    request->callback = callback;
    request->context = context;
    request->generation = 1U;
    result = irq_deferred_work_init(&request->completion_work, "UHCI",
                                    controller->irq,
                                    uhci_interrupt_completion, request);
    if (result == OK) {
        result = uhci_interrupt_reserve_frames(controller, request);
    }
    if (result == OK) {
        uhci_qh_t* queue_head = uhci_queue_head_at(controller,
                                                   request->qh_index);

        if (!queue_head) result = ERR_STATE;
        else {
            queue_head->link = UHCI_PTR_QH | controller->queue_head_phys;
            queue_head->element = UHCI_PTR_TERM;
            result = uhci_interrupt_arm(request);
        }
    }
    if (result != OK) {
        uhci_interrupt_release_frames(controller, request);
        kmemset(request, 0, sizeof(*request));
        spinlock_release(&controller->transfer_lock);
        return result;
    }
    controller->interrupt_active_count++;
    uhci_update_resource_usage(controller);
    spinlock_release(&controller->transfer_lock);
    return OK;
}

int uhci_interrupt_cancel(const usb_device_info_t* device,
                          uint8_t endpoint_address) {
    uhci_controller_t* controller;
    uhci_interrupt_request_t* request;

    if (!device) {
        LOG_ERROR("UHCI", "Dispositivo nulo ao cancelar Interrupt IN");
        return ERR_NULL;
    }
    controller = uhci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    if (!controller) return ERR_NOT_FOUND;
    spinlock_acquire(&controller->transfer_lock);
    request = uhci_find_interrupt_request(controller, device,
                                          endpoint_address);
    if (!request) {
        spinlock_release(&controller->transfer_lock);
        return ERR_NOT_FOUND;
    }
    request->cancelled = 1U;
    request->generation++;
    if (request->completion_work.queued) {
        (void)irq_deferred_cancel(&request->completion_work);
    }
    if (request->active && controller->interrupt_active_count) {
        controller->interrupt_active_count--;
    }
    {
        uhci_qh_t* queue_head = uhci_queue_head_at(controller,
                                                   request->qh_index);
        if (queue_head) queue_head->element = UHCI_PTR_TERM;
    }
    uhci_interrupt_release_frames(controller, request);
    request->used = 0U;
    request->active = 0U;
    request->completion_pending = 0U;
    controller->interrupt_cancel_count++;
    uhci_update_resource_usage(controller);
    spinlock_release(&controller->transfer_lock);
    return OK;
}

int uhci_poll(uint32_t budget, uint32_t* out_processed) {
    uint32_t processed = 0;

    if (!out_processed) {
        LOG_ERROR("UHCI", "Destino nulo no polling UHCI");
        return ERR_NULL;
    }
    for (uint32_t index = 0; index < UHCI_CONTROLLER_CAPACITY; index++) {
        uhci_controller_t* controller = &uhci_controllers[index];
        uint16_t status;

        if (!controller->used || !controller->running) continue;
        uhci_scan_interrupt_requests(controller);
        status = uhci_in16(controller, UHCI_USBSTS);
        if (status == 0xFFFFU) {
            controller->last_error = ERR_UNAVAILABLE;
            controller->running = 0U;
            LOG_ERROR("UHCI", "Controlador UHCI deixou de responder");
            processed++;
            continue;
        }
        if (status & UHCI_USBSTS_HCH) {
            controller->last_error = ERR_UNAVAILABLE;
            controller->running = 0U;
            LOG_ERROR("UHCI", "Controlador UHCI reportou halted");
        }
        if ((status & UHCI_USBSTS_RELEVANT) && processed < budget) {
            uhci_out16(controller, UHCI_USBSTS,
                       (uint16_t)(status & UHCI_USBSTS_RELEVANT));
            controller->irq_pending = 0U;
            processed++;
        }
    }
    *out_processed = processed;
    return OK;
}

static int uhci_copy_status(const uhci_controller_t* controller,
                            usb_uhci_status_t* out_status) {
    uint8_t request_count = 0U;

    if (!controller || !out_status) {
        LOG_ERROR("UHCI", "Argumento nulo ao copiar status UHCI");
        return ERR_NULL;
    }
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->initialized = controller->initialized;
    out_status->running = controller->running;
    out_status->irq_registered = controller->irq_registered;
    out_status->irq_pending = controller->irq_pending;
    out_status->dma_ready = controller->frame_list && controller->queue_head &&
                            controller->td_pool && controller->buffer_pool;
    out_status->control_transfer_ready = controller->running;
    out_status->bulk_transfer_ready = controller->running;
    out_status->interrupt_transfer_ready = controller->running &&
                                           controller->frame_list &&
                                           controller->queue_head;
    out_status->class_driver_active = 0U;
    out_status->hub_support_active = 0U;
    out_status->hotplug_active = 0U;
    out_status->port_count = USB_UHCI_PORT_COUNT;
    out_status->device_count = controller->device_count;
    out_status->port_errors = controller->port_errors;
    out_status->frame_list_phys = controller->frame_list_phys;
    out_status->queue_head_phys = controller->queue_head_phys;
    out_status->td_pool_phys = controller->td_pool_phys;
    out_status->buffer_pool_phys = controller->buffer_pool_phys;
    out_status->td_capacity = USB_UHCI_TD_CAPACITY;
    out_status->td_in_use = controller->td_in_use;
    out_status->buffer_capacity = USB_UHCI_BUFFER_CAPACITY;
    out_status->buffer_in_use = controller->buffer_in_use;
    out_status->irq_events = controller->irq_events;
    out_status->timeout_count = controller->timeout_count;
    out_status->recovery_count = controller->recovery_count;
    out_status->bulk_transfer_count = controller->bulk_transfer_count;
    out_status->last_error = controller->last_error;
    for (uint32_t index = 0U; index < USB_UHCI_INTERRUPT_CAPACITY; index++) {
        if (controller->interrupt_requests[index].used) request_count++;
    }
    out_status->interrupt_request_count = request_count;
    out_status->interrupt_active_count = controller->interrupt_active_count;
    out_status->interrupt_transfer_count = controller->interrupt_transfer_count;
    out_status->interrupt_timeout_count = controller->interrupt_timeout_count;
    out_status->interrupt_error_count = controller->interrupt_error_count;
    out_status->interrupt_cancel_count = controller->interrupt_cancel_count;
    return OK;
}

int uhci_get_status(uint8_t bus, uint8_t device, uint8_t function,
                    usb_uhci_status_t* out_status) {
    uhci_controller_t* controller = uhci_find(bus, device, function);

    if (!out_status) {
        LOG_ERROR("UHCI", "Destino nulo ao consultar status UHCI");
        return ERR_NULL;
    }
    if (!controller) {
        LOG_ERROR("UHCI", "Controlador UHCI nao encontrado");
        return ERR_NOT_FOUND;
    }
    return uhci_copy_status(controller, out_status);
}

int uhci_get_port_count(uint8_t bus, uint8_t device, uint8_t function,
                        uint32_t* out_count) {
    uhci_controller_t* controller = uhci_find(bus, device, function);

    if (!out_count) {
        LOG_ERROR("UHCI", "Destino nulo na contagem de portas UHCI");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("UHCI", "Controlador ausente na contagem de portas");
        return ERR_NOT_FOUND;
    }
    *out_count = USB_UHCI_PORT_COUNT;
    return OK;
}

int uhci_get_port(uint8_t bus, uint8_t device, uint8_t function,
                 uint32_t index, usb_port_info_t* out_info) {
    uhci_controller_t* controller = uhci_find(bus, device, function);

    if (!out_info) {
        LOG_ERROR("UHCI", "Destino nulo ao consultar porta UHCI");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("UHCI", "Controlador ausente na consulta de porta");
        return ERR_NOT_FOUND;
    }
    if (index >= USB_UHCI_PORT_COUNT) {
        LOG_ERROR("UHCI", "Indice de porta UHCI invalido");
        return ERR_INVALID;
    }
    *out_info = controller->ports[index];
    return OK;
}

int uhci_log_port_diagnostics(uint8_t bus, uint8_t device, uint8_t function) {
    uhci_controller_t* controller = uhci_find(bus, device, function);

    if (!controller || !controller->initialized) {
        LOG_ERROR("UHCI", "Controlador ausente no diagnostico de portas");
        return ERR_NOT_FOUND;
    }
    for (uint32_t port = 0U; port < USB_UHCI_PORT_COUNT; port++) {
        uhci_enum_context_t* context = &controller->enumeration[port];

        if (controller->ports[port].state != USB_PORT_DEGRADED ||
            context->last_error == OK) continue;
        uhci_log_enumeration_failure(port, context, context->last_error);
        uhci_log_device_preview(&controller->devices[port]);
        uhci_log_configuration_preview(&controller->devices[port]);
    }
    return OK;
}

int uhci_get_device_count(uint8_t bus, uint8_t device, uint8_t function,
                          uint32_t* out_count) {
    uhci_controller_t* controller = uhci_find(bus, device, function);

    if (!out_count) {
        LOG_ERROR("UHCI", "Destino nulo na contagem de dispositivos UHCI");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("UHCI", "Controlador ausente na contagem de dispositivos");
        return ERR_NOT_FOUND;
    }
    *out_count = controller->device_count;
    return OK;
}

int uhci_get_device(uint8_t bus, uint8_t device, uint8_t function,
                    uint32_t index, usb_device_info_t* out_info) {
    uhci_controller_t* controller = uhci_find(bus, device, function);

    if (!out_info) {
        LOG_ERROR("UHCI", "Destino nulo ao consultar dispositivo UHCI");
        return ERR_NULL;
    }
    if (!controller || !controller->initialized) {
        LOG_ERROR("UHCI", "Controlador ausente na consulta de dispositivo");
        return ERR_NOT_FOUND;
    }
    if (index >= controller->device_count) {
        LOG_ERROR("UHCI", "Indice de dispositivo UHCI invalido");
        return ERR_INVALID;
    }
    {
        uhci_device_record_t* record = uhci_get_device_record_at(controller,
                                                                  index);

        if (!record) {
            LOG_ERROR("UHCI", "Registro de dispositivo UHCI ausente");
            return ERR_STATE;
        }
        *out_info = record->info;
    }
    return OK;
}

int uhci_validate_state(uint8_t bus, uint8_t device, uint8_t function) {
    uhci_controller_t* controller = uhci_find(bus, device, function);
    uint8_t shared_irq_count = 0U;
    uint32_t valid_device_count;

    if (!controller) {
        LOG_ERROR("UHCI", "Controlador UHCI ausente na validacao");
        return ERR_NOT_FOUND;
    }
    if (!controller->initialized || !controller->running ||
        !controller->irq_registered || !controller->frame_list ||
        !controller->queue_head || !controller->td_pool ||
        !controller->buffer_pool ||
        (controller->frame_list_phys & 0xFFFU) ||
        (controller->queue_head_phys & 0xFU) ||
        (controller->td_pool_phys & 0xFU) ||
        (controller->buffer_pool_phys & 0xFFFU) ||
        controller->td_in_use > USB_UHCI_TD_CAPACITY ||
        controller->buffer_in_use > USB_UHCI_BUFFER_CAPACITY) {
        LOG_ERROR("UHCI", "Estruturas UHCI inconsistentes");
        return ERR_STATE;
    }
    if (idt_get_shared_irq_handler_count(controller->irq,
                                         &shared_irq_count) != OK ||
        !shared_irq_count ||
        shared_irq_count > IDT_SHARED_IRQ_HANDLER_CAPACITY) {
        LOG_ERROR("UHCI", "IRQ compartilhada UHCI inconsistente");
        return ERR_STATE;
    }
    if (controller->queue_head->link != UHCI_PTR_TERM ||
        (controller->queue_head->element != UHCI_PTR_TERM &&
         controller->queue_head->element < controller->td_pool_phys)) {
        LOG_ERROR("UHCI", "Queue head UHCI inconsistente");
        return ERR_STATE;
    }
    for (uint32_t frame = 0; frame < USB_UHCI_FRAME_COUNT; frame++) {
        uint8_t owner = controller->frame_owner[frame];
        uint32_t expected = UHCI_PTR_QH | controller->queue_head_phys;

        if (owner != 0xFFU && owner >= UHCI_INTERRUPT_QH_BASE &&
            owner < UHCI_INTERRUPT_QH_BASE + USB_UHCI_INTERRUPT_CAPACITY) {
            uhci_interrupt_request_t* request =
                &controller->interrupt_requests[owner - UHCI_INTERRUPT_QH_BASE];

            if (!request->used || !request->interval ||
                frame < request->phase ||
                (frame - request->phase) % request->interval) {
                LOG_ERROR("UHCI", "Proprietario de frame UHCI invalido");
                return ERR_STATE;
            }
            expected = UHCI_PTR_QH | (controller->queue_head_phys +
                                      owner * UHCI_QH_STRIDE);
        } else if (owner != 0xFFU) {
            LOG_ERROR("UHCI", "Indice de frame UHCI invalido");
            return ERR_STATE;
        }
        if (controller->frame_list[frame] != expected) {
            LOG_ERROR("UHCI", "Frame list UHCI inconsistente");
            return ERR_STATE;
        }
    }
    {
        uint8_t active_count = 0U;

        for (uint32_t index = 0U; index < USB_UHCI_INTERRUPT_CAPACITY;
             index++) {
            uhci_interrupt_request_t* request =
                &controller->interrupt_requests[index];
            uhci_qh_t* queue_head;
            uhci_td_t* td;
            uint32_t expected_element;
            uint8_t element_valid;

            if (!request->used) continue;
            if (!request->record || request->qh_index != UHCI_INTERRUPT_QH_BASE +
                index || request->td_index != UHCI_INTERRUPT_TD_BASE + index ||
                request->buffer_index != index || !request->interval ||
                request->interval > USB_UHCI_FRAME_COUNT) {
                LOG_ERROR("UHCI", "Requisicao Interrupt UHCI inconsistente");
                return ERR_STATE;
            }
            queue_head = uhci_queue_head_at(controller, request->qh_index);
            td = &controller->td_pool[request->td_index];
            expected_element = controller->td_pool_phys +
                               request->td_index * UHCI_TD_SIZE;
            element_valid = request->active ?
                (queue_head &&
                 (queue_head->element == expected_element ||
                  (queue_head->element == UHCI_PTR_TERM &&
                   !(td->status & UHCI_TD_ACTIVE)))) :
                (queue_head && queue_head->element == UHCI_PTR_TERM);
            if (!queue_head || queue_head->link !=
                (UHCI_PTR_QH | controller->queue_head_phys) ||
                !element_valid) {
                LOG_ERROR("UHCI", "Queue head Interrupt UHCI inconsistente");
                return ERR_STATE;
            }
            if (request->active) active_count++;
        }
        if (active_count != controller->interrupt_active_count) {
            LOG_ERROR("UHCI", "Contagem Interrupt UHCI inconsistente");
            return ERR_STATE;
        }
    }
    for (uint32_t port = 0; port < USB_UHCI_PORT_COUNT; port++) {
        if (controller->ports[port].state > USB_PORT_DEGRADED ||
            controller->ports[port].port_number != port + 1U) {
            LOG_ERROR("UHCI", "Estado de porta UHCI invalido");
            return ERR_STATE;
        }
    }
    valid_device_count = uhci_count_valid_devices(controller);
    if (valid_device_count != controller->device_count) {
        LOG_ERROR("UHCI", "Contagem de dispositivos UHCI inconsistente");
        return ERR_STATE;
    }
    for (uint32_t port = 0U; port < USB_UHCI_PORT_COUNT; port++) {
        uhci_device_record_t* record = &controller->devices[port];

        if (!record->valid_descriptors) continue;
        if (record->info.state != USB_DEVICE_CONFIGURED ||
            record->info.port_number != port + 1U ||
            !record->info.usb_address ||
            controller->ports[port].state != USB_PORT_CONFIGURED ||
            controller->ports[port].usb_address != record->info.usb_address ||
            record->info.hub_present || !record->info.id[0] ||
            !record->info.device_descriptor_valid ||
            !record->info.configuration_descriptor_valid ||
            !uhci_valid_control_packet_size(record->info.max_packet_size0) ||
            record->info.configuration_length <
            UHCI_CONFIGURATION_HEADER_LENGTH ||
            record->info.configuration_length >
            USB_UHCI_DESCRIPTOR_BUFFER_SIZE ||
            record->info.endpoint_count > USB_DEVICE_MAX_ENDPOINTS ||
            record->info.bulk_in_count > record->info.endpoint_count ||
            record->info.bulk_out_count > record->info.endpoint_count ||
            record->info.interrupt_in_count > record->info.endpoint_count ||
            (record->info.bulk_in_count &&
             !record->info.bulk_in_max_packet) ||
            (record->info.bulk_out_count &&
             !record->info.bulk_out_max_packet) ||
            (record->info.interrupt_in_count &&
             (!record->info.interrupt_in_endpoint ||
              !record->info.interrupt_in_max_packet ||
              !record->info.interrupt_interval))) {
            LOG_ERROR("UHCI", "Dispositivo USB configurado invalido");
            return ERR_STATE;
        }
        for (uint32_t endpoint = 0U;
             endpoint < record->info.endpoint_count; endpoint++) {
            usb_endpoint_info_t* descriptor =
                &record->info.endpoints[endpoint];

            if (!(descriptor->address & USB_ENDPOINT_ADDRESS_NUMBER_MASK) ||
                descriptor->transfer_type > USB_ENDPOINT_TRANSFER_TYPE_MAX ||
                !descriptor->max_packet ||
                descriptor->max_packet > USB_ENDPOINT_MAX_PACKET_SIZE_FULL) {
                LOG_ERROR("UHCI", "Tabela de endpoints USB invalida");
                return ERR_STATE;
            }
        }
    }
    return OK;
}

int uhci_control_request(const usb_device_info_t* device,
                         uint8_t request_type, uint8_t request,
                         uint16_t value, uint16_t index, uint16_t length,
                         uint8_t* data, uint16_t* out_length) {
    uhci_controller_t* controller;
    uhci_device_record_t* record;
    int result;

    if (!device) {
        LOG_ERROR("UHCI", "Dispositivo nulo na requisicao de controle");
        return ERR_NULL;
    }
    controller = uhci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = uhci_find_device_record(controller, device);
    if (!record || !controller->running) {
        LOG_ERROR("UHCI", "Dispositivo ausente no controle UHCI");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    result = uhci_control_transfer(controller, device->usb_address,
                                   record->info.speed == USB_DEVICE_SPEED_LOW,
                                   record->info.max_packet_size0,
                                   request_type, request, value, index, length,
                                   data, out_length);
    spinlock_release(&controller->transfer_lock);
    return result;
}

static int uhci_bulk_transfer_once(uhci_controller_t* controller,
                                   uhci_device_record_t* record,
                                   const usb_device_info_t* device,
                                   uint8_t endpoint_address,
                                   uint8_t direction_in, uint8_t* buffer,
                                   uint16_t length, uint16_t* out_length) {
    uint8_t* dma_buffer;
    uint16_t max_packet;
    uint8_t toggle;
    uint8_t initial_toggle;
    uint32_t td_count;
    uint32_t copied = 0U;
    uint32_t completed_tds = 0U;
    int result;

    if (!controller || !record || !device || !buffer || !length ||
        length > USB_UHCI_BULK_BUFFER_SIZE ||
        (endpoint_address & 0x0FU) == 0U ||
        ((endpoint_address & 0x80U) != 0U) != (direction_in != 0U) ||
        endpoint_address != (direction_in ? record->info.bulk_in_endpoint :
                             record->info.bulk_out_endpoint)) {
        LOG_ERROR("UHCI", "Argumento invalido na transferencia Bulk");
        return ERR_INVALID;
    }
    max_packet = direction_in ? record->info.bulk_in_max_packet :
                 record->info.bulk_out_max_packet;
    if (!max_packet || max_packet > 64U) {
        LOG_ERROR("UHCI", "Endpoint Bulk sem tamanho valido");
        return ERR_UNAVAILABLE;
    }
    dma_buffer = controller->buffer_pool + UHCI_BULK_BUFFER_OFFSET;
    if (!direction_in) kmemcpy(dma_buffer, buffer, length);
    td_count = (length + max_packet - 1U) / max_packet;
    if (!td_count || td_count > UHCI_SYNC_TD_CAPACITY) {
        LOG_ERROR("UHCI", "Quantidade de TDs Bulk fora do limite");
        return ERR_OVERFLOW;
    }
    initial_toggle = direction_in ? record->bulk_in_toggle :
                   record->bulk_out_toggle;
    toggle = initial_toggle;
    kmemset(controller->td_pool, 0, td_count * UHCI_TD_SIZE);
    controller->sync_td_in_use = td_count;
    controller->sync_buffer_in_use = 1U;
    uhci_update_resource_usage(controller);
    for (uint32_t td = 0U; td < td_count; td++) {
        uint32_t remaining = length - copied;
        uint16_t packet = remaining > max_packet ? max_packet :
                          (uint16_t)remaining;
        uint32_t next = td + 1U < td_count ? controller->td_pool_phys +
                     (td + 1U) * UHCI_TD_SIZE : UHCI_PTR_TERM;

        controller->td_pool[td].link = next;
        controller->td_pool[td].status = uhci_td_status(
            device->speed == USB_DEVICE_SPEED_LOW, td + 1U == td_count);
        controller->td_pool[td].token = uhci_token(
            direction_in ? UHCI_PID_IN : UHCI_PID_OUT, device->usb_address,
            endpoint_address & 0x0FU, toggle, packet);
        controller->td_pool[td].buffer = controller->buffer_pool_phys +
                                         UHCI_BULK_BUFFER_OFFSET + copied;
        copied += packet;
        toggle ^= 1U;
    }
    controller->queue_head->element = controller->td_pool_phys;
    asm volatile("" : : : "memory");
    result = uhci_wait_transfer(controller, td_count, UHCI_BULK_TIMEOUT_MS);
    controller->queue_head->element = UHCI_PTR_TERM;
    if (result != OK) {
        uhci_recover_controller(controller);
        controller->sync_td_in_use = 0U;
        controller->sync_buffer_in_use = 0U;
        uhci_update_resource_usage(controller);
        return result;
    }
    copied = 0U;
    for (uint32_t td = 0U; td < td_count; td++) {
        uint32_t remaining = length - copied;
        uint16_t packet = remaining > max_packet ? max_packet :
                          (uint16_t)remaining;
        uint32_t actual = uhci_td_actual(controller->td_pool[td].status);

        if (actual > packet) actual = packet;
        if (direction_in && actual) kmemcpy(buffer + copied, dma_buffer + copied,
                                            actual);
        copied += actual;
        completed_tds = td + 1U;
        if (actual < packet) break;
    }
    if (direction_in ? record->info.bulk_in_endpoint == endpoint_address :
                       record->info.bulk_out_endpoint == endpoint_address) {
        toggle = initial_toggle ^ (completed_tds & 1U);
        if (direction_in) record->bulk_in_toggle = toggle;
        else record->bulk_out_toggle = toggle;
    }
    controller->sync_td_in_use = 0U;
    controller->sync_buffer_in_use = 0U;
    uhci_update_resource_usage(controller);
    controller->bulk_transfer_count++;
    if (out_length) *out_length = direction_in ? (uint16_t)copied : length;
    return OK;
}

int uhci_bulk_transfer(const usb_device_info_t* device,
                       uint8_t endpoint_address, uint8_t direction_in,
                       uint8_t* buffer, uint16_t length,
                       uint16_t* out_length) {
    uhci_controller_t* controller;
    uhci_device_record_t* record;
    int result;

    if (!device || !buffer || !length) {
        LOG_ERROR("UHCI", "Argumento nulo na transferencia Bulk");
        return ERR_NULL;
    }
    if (device->speed == USB_DEVICE_SPEED_LOW) {
        LOG_ERROR("UHCI", "Bulk nao suporta dispositivo low-speed");
        return ERR_UNAVAILABLE;
    }
    controller = uhci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = uhci_find_device_record(controller, device);
    if (!record || !controller->running) {
        LOG_ERROR("UHCI", "Controlador ou dispositivo Bulk indisponivel");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    result = uhci_bulk_transfer_once(controller, record, device,
                                     endpoint_address, direction_in, buffer,
                                     length, out_length);
    spinlock_release(&controller->transfer_lock);
    if (result != OK) LOG_ERROR("UHCI", "Transferencia Bulk falhou");
    return result;
}

int uhci_reset_bulk_toggles(const usb_device_info_t* device) {
    uhci_controller_t* controller;
    uhci_device_record_t* record;

    if (!device) {
        LOG_ERROR("UHCI", "Dispositivo nulo ao resetar toggles Bulk");
        return ERR_NULL;
    }
    controller = uhci_find(device->controller_bus, device->controller_device,
                           device->controller_function);
    record = uhci_find_device_record(controller, device);
    if (!record || !controller->running) {
        LOG_ERROR("UHCI", "Dispositivo ausente ao resetar toggles Bulk");
        return ERR_NOT_FOUND;
    }
    spinlock_acquire(&controller->transfer_lock);
    record->bulk_in_toggle = 0U;
    record->bulk_out_toggle = 0U;
    spinlock_release(&controller->transfer_lock);
    return OK;
}
