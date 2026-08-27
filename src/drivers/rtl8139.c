#include "drivers/rtl8139.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/irq_deferred.h"

#define RTL8139_VENDOR_ID 0x10ECU
#define RTL8139_DEVICE_ID 0x8139U
#define RTL8139_PCI_CLASS 0x02U
#define RTL8139_PCI_BAR_IO 0x01U
#define RTL8139_PCI_BAR_ADDRESS_MASK 0xFFFFFFFCU
#define RTL8139_IO_SPACE_BYTES 0x100U
#define RTL8139_IRQ_MAX 15U
#define RTL8139_IRQ_UNKNOWN 0xFFU
#define RTL8139_RESET_TIMEOUT_TICKS 50U
#define RTL8139_TX_DESCRIPTOR_COUNT 4U
#define RTL8139_TX_BUFFER_SIZE 2048U
#define RTL8139_TX_BUFFER_BYTES \
    (RTL8139_TX_DESCRIPTOR_COUNT * RTL8139_TX_BUFFER_SIZE)
#define RTL8139_TX_BUFFER_PAGES \
    (RTL8139_TX_BUFFER_BYTES / PAGE_SIZE)
#define RTL8139_RX_RING_SIZE 8192U
#define RTL8139_RX_RING_PAD 16U
#define RTL8139_RX_WRAP_BYTES 1536U
#define RTL8139_RX_BUFFER_BYTES \
    (RTL8139_RX_RING_SIZE + RTL8139_RX_RING_PAD + \
     RTL8139_RX_WRAP_BYTES)
#define RTL8139_RX_BUFFER_PAGES \
    ((RTL8139_RX_BUFFER_BYTES + PAGE_SIZE - 1U) / PAGE_SIZE)
#define RTL8139_MIN_FRAME_SIZE 60U
#define RTL8139_MAX_FRAME_SIZE 1518U
#define RTL8139_FCS_SIZE 4U
#define RTL8139_RX_HEADER_SIZE 4U
#define RTL8139_RX_QUEUE_SLOT_COUNT 8U
#define RTL8139_RX_DRAIN_BUDGET 32U
#define RTL8139_MIN_WIRE_SIZE \
    (RTL8139_MIN_FRAME_SIZE + RTL8139_FCS_SIZE)
#define RTL8139_MAX_WIRE_SIZE \
    (RTL8139_MAX_FRAME_SIZE + RTL8139_FCS_SIZE)
#define RTL8139_MAC_ADDRESS_SIZE 6U

#define RTL8139_REG_IDR0 0x00U
#define RTL8139_REG_TSD0 0x10U
#define RTL8139_REG_TSAD0 0x20U
#define RTL8139_REG_RBSTART 0x30U
#define RTL8139_REG_CR 0x37U
#define RTL8139_REG_CAPR 0x38U
#define RTL8139_REG_IMR 0x3CU
#define RTL8139_REG_ISR 0x3EU
#define RTL8139_REG_TCR 0x40U
#define RTL8139_REG_RCR 0x44U
#define RTL8139_REG_CONFIG1 0x52U
#define RTL8139_REG_MSR 0x58U

#define RTL8139_CR_BUFE 0x01U
#define RTL8139_CR_TE 0x04U
#define RTL8139_CR_RE 0x08U
#define RTL8139_CR_RST 0x10U
#define RTL8139_TSD_OWN (1U << 13)
#define RTL8139_TSD_TUN (1U << 14)
#define RTL8139_TSD_TOK (1U << 15)
#define RTL8139_TSD_TABT (1U << 30)
#define RTL8139_TSD_ERROR_MASK \
    (RTL8139_TSD_TUN | RTL8139_TSD_TABT)
#define RTL8139_RCR_APM (1U << 1)
#define RTL8139_RCR_AB (1U << 3)
#define RTL8139_RCR_WRAP (1U << 7)
#define RTL8139_RCR_MXDMA_256 (4U << 8)
#define RTL8139_RCR_RXFTH_NONE (7U << 13)
#define RTL8139_RCR_DEFAULT \
    (RTL8139_RCR_APM | RTL8139_RCR_AB | RTL8139_RCR_WRAP | \
     RTL8139_RCR_MXDMA_256 | RTL8139_RCR_RXFTH_NONE)
#define RTL8139_TCR_MXDMA_2048 (7U << 8)
#define RTL8139_TCR_IFG96 (3U << 24)
#define RTL8139_TCR_DEFAULT \
    (RTL8139_TCR_MXDMA_2048 | RTL8139_TCR_IFG96)
#define RTL8139_INT_ROK (1U << 0)
#define RTL8139_INT_RER (1U << 1)
#define RTL8139_INT_TOK (1U << 2)
#define RTL8139_INT_TER (1U << 3)
#define RTL8139_INT_ROVW (1U << 4)
#define RTL8139_INT_PUN_LINK (1U << 5)
#define RTL8139_INT_FOVW (1U << 6)
#define RTL8139_INT_SERR (1U << 15)
#define RTL8139_INT_MASK \
    (RTL8139_INT_ROK | RTL8139_INT_RER | RTL8139_INT_TOK | \
     RTL8139_INT_TER | RTL8139_INT_ROVW | RTL8139_INT_PUN_LINK | \
     RTL8139_INT_FOVW | RTL8139_INT_SERR)
#define RTL8139_RX_STATUS_ROK (1U << 0)
#define RTL8139_RX_STATUS_ERROR_MASK 0x003EU
#define RTL8139_MSR_LINKB (1U << 2)
#define RTL8139_CAPR_INITIAL 0xFFF0U

typedef struct {
    uint16_t length;
    uint8_t data[RTL8139_MAX_FRAME_SIZE];
} rtl8139_rx_slot_t;

typedef struct {
    uint8_t used;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uint16_t io_base;
    uint8_t mac_address[RTL8139_MAC_ADDRESS_SIZE];
    ethernet_driver_status_t status;
    uint8_t* rx_buffer;
    uint8_t* tx_buffers;
    uint16_t rx_offset;
    uint8_t tx_next;
    rtl8139_rx_slot_t rx_queue[RTL8139_RX_QUEUE_SLOT_COUNT];
    volatile uint8_t rx_queue_head;
    volatile uint8_t rx_queue_tail;
    volatile uint8_t rx_pending;
    volatile uint8_t rx_needs_reset;
    volatile uint16_t pending_irq_status;
    volatile uint32_t pending_rx_interrupts;
    irq_deferred_work_t bottom_half_work;
} rtl8139_device_t;

static rtl8139_device_t rtl8139_devices[RTL8139_DEVICE_CAPACITY];

static uint8_t rtl8139_in8(const rtl8139_device_t* device,
                           uint16_t offset) {
    uint8_t value;
    uint16_t port = (uint16_t)(device->io_base + offset);

    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t rtl8139_in16(const rtl8139_device_t* device,
                             uint16_t offset) {
    uint16_t value;
    uint16_t port = (uint16_t)(device->io_base + offset);

    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t rtl8139_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void rtl8139_irq_restore(uint32_t flags) {
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
}

static uint32_t rtl8139_in32(const rtl8139_device_t* device,
                             uint16_t offset) {
    uint32_t value;
    uint16_t port = (uint16_t)(device->io_base + offset);

    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void rtl8139_out8(const rtl8139_device_t* device,
                         uint16_t offset, uint8_t value) {
    uint16_t port = (uint16_t)(device->io_base + offset);
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void rtl8139_out16(const rtl8139_device_t* device,
                          uint16_t offset, uint16_t value) {
    uint16_t port = (uint16_t)(device->io_base + offset);
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void rtl8139_out32(const rtl8139_device_t* device,
                          uint16_t offset, uint32_t value) {
    uint16_t port = (uint16_t)(device->io_base + offset);
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static void rtl8139_memory_barrier(void) {
    asm volatile("" : : : "memory");
}

static uint16_t rtl8139_read_u16(const uint8_t* data) {
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
}

static void rtl8139_update_link(rtl8139_device_t* device) {
    if (device && device->status.initialized) {
        device->status.link_up =
            (rtl8139_in8(device, RTL8139_REG_MSR) &
             RTL8139_MSR_LINKB) ? 0U : 1U;
    }
}

static rtl8139_device_t* rtl8139_find_device(
    const pci_device_t* pci) {
    for (uint32_t index = 0; index < RTL8139_DEVICE_CAPACITY; index++) {
        rtl8139_device_t* device = &rtl8139_devices[index];

        if (device->used && device->bus == pci->bus &&
            device->device == pci->device &&
            device->function == pci->function) return device;
    }
    return NULL;
}

static rtl8139_device_t* rtl8139_allocate_device(void) {
    for (uint32_t index = 0; index < RTL8139_DEVICE_CAPACITY; index++) {
        if (!rtl8139_devices[index].used) {
            kmemset(&rtl8139_devices[index], 0,
                    sizeof(rtl8139_devices[index]));
            rtl8139_devices[index].used = 1;
            return &rtl8139_devices[index];
        }
    }
    return NULL;
}

static int rtl8139_copy_interface_id(char* destination,
                                     const char* source) {
    uint32_t index = 0;

    if (!destination || !source) {
        LOG_ERROR("RTL8139", "ID de interface nulo");
        return ERR_NULL;
    }
    while (source[index] && index + 1U < ETHERNET_INTERFACE_ID_SIZE) {
        destination[index] = source[index];
        index++;
    }
    if (!index || source[index]) {
        LOG_ERROR("RTL8139", "ID de interface invalido");
        return source[index] ? ERR_OVERFLOW : ERR_INVALID;
    }
    destination[index] = '\0';
    return OK;
}

static int rtl8139_prepare_pci(rtl8139_device_t* device,
                               const pci_device_t* pci) {
    uint32_t io_base;
    int result;

    if (pci->vendor_id != RTL8139_VENDOR_ID ||
        pci->device_id != RTL8139_DEVICE_ID ||
        pci->class != RTL8139_PCI_CLASS ||
        pci->irq == RTL8139_IRQ_UNKNOWN || pci->irq > RTL8139_IRQ_MAX ||
        !(pci->bar0 & RTL8139_PCI_BAR_IO)) {
        LOG_ERROR("RTL8139", "Configuracao PCI do RTL8139 invalida");
        return ERR_UNAVAILABLE;
    }
    io_base = pci->bar0 & RTL8139_PCI_BAR_ADDRESS_MASK;
    if (!io_base || io_base > 0xFFFFU ||
        io_base + RTL8139_IO_SPACE_BYTES > 0x10000U) {
        LOG_ERROR("RTL8139", "BAR0 de port I/O incompativel");
        return ERR_UNAVAILABLE;
    }
    result = pci_enable_io_and_bus_mastering(pci);
    if (result != OK) {
        LOG_ERROR("RTL8139", "Falha ao habilitar I/O e DMA PCI");
        return result;
    }
    device->io_base = (uint16_t)io_base;
    return OK;
}

static void rtl8139_release_dma(rtl8139_device_t* device) {
    if (device->tx_buffers) {
        pmm_free_pages(device->tx_buffers, RTL8139_TX_BUFFER_PAGES);
        device->tx_buffers = NULL;
    }
    if (device->rx_buffer) {
        pmm_free_pages(device->rx_buffer, RTL8139_RX_BUFFER_PAGES);
        device->rx_buffer = NULL;
    }
}

static int rtl8139_allocate_dma(rtl8139_device_t* device) {
    device->rx_buffer =
        (uint8_t*)pmm_alloc_pages(RTL8139_RX_BUFFER_PAGES);
    device->tx_buffers =
        (uint8_t*)pmm_alloc_pages(RTL8139_TX_BUFFER_PAGES);
    if (!device->rx_buffer || !device->tx_buffers) {
        LOG_ERROR("RTL8139", "Falha ao alocar memoria DMA");
        rtl8139_release_dma(device);
        return ERR_MEM;
    }
    kmemset(device->rx_buffer, 0,
            RTL8139_RX_BUFFER_PAGES * PAGE_SIZE);
    kmemset(device->tx_buffers, 0,
            RTL8139_TX_BUFFER_PAGES * PAGE_SIZE);
    return OK;
}

static int rtl8139_wait_reset(rtl8139_device_t* device) {
    uint32_t start = timer_get_ticks();

    rtl8139_out8(device, RTL8139_REG_CR, RTL8139_CR_RST);
    while (rtl8139_in8(device, RTL8139_REG_CR) & RTL8139_CR_RST) {
        if (timer_get_ticks() - start >=
            RTL8139_RESET_TIMEOUT_TICKS) {
            LOG_ERROR("RTL8139", "Timeout no reset do controlador");
            return ERR_TIMEOUT;
        }
    }
    return OK;
}

static int rtl8139_read_mac(rtl8139_device_t* device) {
    uint8_t all_zero = 1;
    uint8_t all_ff = 1;

    for (uint32_t index = 0;
         index < RTL8139_MAC_ADDRESS_SIZE; index++) {
        device->mac_address[index] =
            rtl8139_in8(device, RTL8139_REG_IDR0 + index);
        if (device->mac_address[index] != 0U) all_zero = 0;
        if (device->mac_address[index] != 0xFFU) all_ff = 0;
    }
    if (all_zero || all_ff || (device->mac_address[0] & 0x01U)) {
        LOG_ERROR("RTL8139", "MAC invalido no controlador");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static void rtl8139_configure_dma(rtl8139_device_t* device) {
    rtl8139_out32(device, RTL8139_REG_RBSTART,
                  (uint32_t)device->rx_buffer);
    for (uint32_t index = 0;
         index < RTL8139_TX_DESCRIPTOR_COUNT; index++) {
        rtl8139_out32(device,
            RTL8139_REG_TSAD0 + index * sizeof(uint32_t),
            (uint32_t)(device->tx_buffers +
                       index * RTL8139_TX_BUFFER_SIZE));
    }
    device->rx_offset = 0;
    device->tx_next = 0;
    rtl8139_out16(device, RTL8139_REG_CAPR,
                  RTL8139_CAPR_INITIAL);
    rtl8139_out8(device, RTL8139_REG_CR,
                 RTL8139_CR_RE | RTL8139_CR_TE);
    rtl8139_out32(device, RTL8139_REG_TCR, RTL8139_TCR_DEFAULT);
    rtl8139_out32(device, RTL8139_REG_RCR, RTL8139_RCR_DEFAULT);
}

static void rtl8139_disable_hardware(rtl8139_device_t* device) {
    if (!device || !device->io_base) return;
    rtl8139_out16(device, RTL8139_REG_IMR, 0U);
    rtl8139_out8(device, RTL8139_REG_CR, 0U);
    rtl8139_out16(device, RTL8139_REG_ISR, 0xFFFFU);
}

static void rtl8139_reset_receiver(rtl8139_device_t* device) {
    uint8_t command = rtl8139_in8(device, RTL8139_REG_CR);

    rtl8139_out8(device, RTL8139_REG_CR,
                 (uint8_t)(command & ~RTL8139_CR_RE));
    rtl8139_out32(device, RTL8139_REG_RBSTART,
                  (uint32_t)device->rx_buffer);
    device->rx_offset = 0;
    rtl8139_out16(device, RTL8139_REG_CAPR,
                  RTL8139_CAPR_INITIAL);
    rtl8139_out32(device, RTL8139_REG_RCR, RTL8139_RCR_DEFAULT);
    rtl8139_out8(device, RTL8139_REG_CR,
                 RTL8139_CR_RE | RTL8139_CR_TE);
    device->rx_pending =
        device->rx_queue_head != device->rx_queue_tail;
    device->status.rx_queue_depth =
        device->rx_queue_tail >= device->rx_queue_head ?
        device->rx_queue_tail - device->rx_queue_head :
        RTL8139_RX_QUEUE_SLOT_COUNT - device->rx_queue_head +
        device->rx_queue_tail;
    device->rx_needs_reset = 0;
}

static int rtl8139_get_driver_status(
    void* driver_context, ethernet_driver_status_t* out_status) {
    rtl8139_device_t* device =
        (rtl8139_device_t*)driver_context;

    if (!device || !out_status) {
        LOG_ERROR("RTL8139", "Argumento nulo ao consultar estado");
        return ERR_NULL;
    }
    if (!device->used || !device->status.initialized) {
        LOG_ERROR("RTL8139", "Consulta de instancia inativa");
        return ERR_STATE;
    }
    rtl8139_update_link(device);
    device->status.rx_queue_depth =
        device->rx_queue_tail >= device->rx_queue_head ?
        device->rx_queue_tail - device->rx_queue_head :
        RTL8139_RX_QUEUE_SLOT_COUNT - device->rx_queue_head +
        device->rx_queue_tail;
    if (device->status.rx_queue_depth >
        device->status.rx_queue_high_water) {
        device->status.rx_queue_high_water =
            device->status.rx_queue_depth;
    }
    *out_status = device->status;
    return OK;
}

static int rtl8139_send_frame(void* driver_context,
                              const uint8_t* data, uint16_t length);
static int rtl8139_has_pending_rx(void* driver_context,
                                  uint8_t* out_pending);
static int rtl8139_receive_frame(void* driver_context, uint8_t* data,
                                 uint16_t capacity,
                                 uint16_t* out_length,
                                 uint8_t* out_received);
static int rtl8139_service_pending(void* driver_context);
static void rtl8139_bottom_half(void* context);

static int rtl8139_fill_interface(
    rtl8139_device_t* device, const char* interface_id,
    ethernet_interface_t* out_interface) {
    int result;

    kmemset(out_interface, 0, sizeof(*out_interface));
    result = rtl8139_copy_interface_id(out_interface->interface_id,
                                      interface_id);
    if (result != OK) return result;
    out_interface->initialized = 1;
    kmemcpy(out_interface->mac_address, device->mac_address,
            RTL8139_MAC_ADDRESS_SIZE);
    out_interface->driver_context = device;
    out_interface->get_driver_status = rtl8139_get_driver_status;
    out_interface->service_pending = rtl8139_service_pending;
    out_interface->rx_pending = rtl8139_has_pending_rx;
    out_interface->receive_frame = rtl8139_receive_frame;
    out_interface->send_frame = rtl8139_send_frame;
    return OK;
}

static void rtl8139_fail_init(rtl8139_device_t* device) {
    rtl8139_disable_hardware(device);
    rtl8139_release_dma(device);
    kmemset(device, 0, sizeof(*device));
}

int rtl8139_init(const pci_device_t* pci, const char* interface_id,
                 ethernet_interface_t* out_interface) {
    rtl8139_device_t* device;
    int result;

    LOG_INFO("RTL8139", "Inicializando controlador RTL8139");
    if (!pci || !interface_id || !out_interface) {
        LOG_ERROR("RTL8139", "Argumento nulo na inicializacao");
        return ERR_NULL;
    }
    device = rtl8139_find_device(pci);
    if (device) {
        result = rtl8139_fill_interface(
            device, interface_id, out_interface);
        if (result == OK) {
            LOG_INFO("RTL8139",
                     "Controlador RTL8139 inicializado com sucesso");
        }
        return result;
    }
    device = rtl8139_allocate_device();
    if (!device) {
        LOG_ERROR("RTL8139", "Limite de controladores atingido");
        return ERR_OVERFLOW;
    }
    device->bus = pci->bus;
    device->device = pci->device;
    device->function = pci->function;
    device->irq = pci->irq;
    device->status.last_error = ERR_STATE;
    result = rtl8139_prepare_pci(device, pci);
    if (result != OK) goto failed;
    rtl8139_out16(device, RTL8139_REG_IMR, 0U);
    rtl8139_out8(device, RTL8139_REG_CONFIG1, 0U);
    result = rtl8139_wait_reset(device);
    if (result != OK) goto failed;
    result = rtl8139_read_mac(device);
    if (result != OK) goto failed;
    result = rtl8139_allocate_dma(device);
    if (result != OK) goto failed;
    rtl8139_configure_dma(device);
    rtl8139_out16(device, RTL8139_REG_ISR, 0xFFFFU);
    result = irq_deferred_work_init(&device->bottom_half_work, "RTL8139",
                                    pci->irq, rtl8139_bottom_half, device);
    if (result != OK) goto failed;
    result = idt_register_shared_irq_handler(
        pci->irq, rtl8139_handler);
    if (result != OK) {
        LOG_ERROR("RTL8139", "Falha ao registrar IRQ compartilhada");
        goto failed;
    }
    device->status.initialized = 1;
    device->status.last_error = OK;
    rtl8139_update_link(device);
    rtl8139_out16(device, RTL8139_REG_IMR, RTL8139_INT_MASK);
    result = rtl8139_fill_interface(
        device, interface_id, out_interface);
    if (result != OK) goto failed;
    LOG_INFO("RTL8139", "Controlador RTL8139 inicializado com sucesso");
    return OK;

failed:
    rtl8139_fail_init(device);
    LOG_ERROR("RTL8139", "Falha ao inicializar controlador RTL8139");
    return result;
}

static int rtl8139_send_frame(void* driver_context,
                              const uint8_t* data, uint16_t length) {
    rtl8139_device_t* device =
        (rtl8139_device_t*)driver_context;
    uint8_t descriptor;
    uint32_t status;
    uint32_t start;

    if (!device || !data) {
        LOG_ERROR("RTL8139", "Argumento nulo para transmissao");
        return ERR_NULL;
    }
    if (length < RTL8139_MIN_FRAME_SIZE ||
        length > RTL8139_MAX_FRAME_SIZE) {
        LOG_ERROR("RTL8139", "Tamanho de frame invalido");
        return ERR_INVALID;
    }
    if (!device->status.initialized) {
        LOG_ERROR("RTL8139", "Transmissao sem instancia ativa");
        return ERR_STATE;
    }
    rtl8139_update_link(device);
    if (!device->status.link_up) {
        LOG_WARN("RTL8139", "Transmissao com link indisponivel");
        return ERR_UNAVAILABLE;
    }
    descriptor = device->tx_next;
    status = rtl8139_in32(
        device, RTL8139_REG_TSD0 +
        descriptor * sizeof(uint32_t));
    if (!(status & RTL8139_TSD_OWN)) {
        LOG_WARN("RTL8139", "Descritor TX ainda pertence ao hardware");
        return ERR_UNAVAILABLE;
    }
    kmemcpy(device->tx_buffers +
            descriptor * RTL8139_TX_BUFFER_SIZE, data, length);
    rtl8139_memory_barrier();
    rtl8139_out32(device,
        RTL8139_REG_TSD0 + descriptor * sizeof(uint32_t), length);
    start = timer_get_ticks();
    do {
        status = rtl8139_in32(
            device, RTL8139_REG_TSD0 +
            descriptor * sizeof(uint32_t));
        if (status & RTL8139_TSD_OWN) break;
        if (timer_get_ticks() - start >=
            RTL8139_RESET_TIMEOUT_TICKS) {
            device->status.tx_errors++;
            device->status.last_error = ERR_TIMEOUT;
            LOG_ERROR("RTL8139", "Timeout ao transmitir frame");
            return ERR_TIMEOUT;
        }
    } while (1);
    if (!(status & RTL8139_TSD_TOK) ||
        (status & RTL8139_TSD_ERROR_MASK)) {
        device->status.tx_errors++;
        device->status.last_error = ERR_STATE;
        LOG_ERROR("RTL8139", "Hardware recusou transmissao");
        return ERR_STATE;
    }
    device->tx_next =
        (descriptor + 1U) % RTL8139_TX_DESCRIPTOR_COUNT;
    device->status.tx_packets++;
    device->status.last_error = OK;
    return OK;
}

static int rtl8139_has_pending_rx(void* driver_context,
                                  uint8_t* out_pending) {
    rtl8139_device_t* device =
        (rtl8139_device_t*)driver_context;

    if (!device || !out_pending) {
        LOG_ERROR("RTL8139", "Argumento nulo ao consultar RX");
        return ERR_NULL;
    }
    if (!device->status.initialized) {
        LOG_ERROR("RTL8139", "Consulta RX sem instancia ativa");
        return ERR_STATE;
    }
    *out_pending = device->rx_queue_head != device->rx_queue_tail;
    return OK;
}

static void rtl8139_advance_rx(rtl8139_device_t* device,
                               uint16_t wire_length) {
    uint32_t next = device->rx_offset + RTL8139_RX_HEADER_SIZE +
                    wire_length;

    next = (next + 3U) & ~3U;
    next %= RTL8139_RX_RING_SIZE;
    device->rx_offset = (uint16_t)next;
    rtl8139_memory_barrier();
    rtl8139_out16(device, RTL8139_REG_CAPR,
                  (uint16_t)(device->rx_offset - 16U));
}

static void rtl8139_drain_rx(rtl8139_device_t* device) {
    for (uint32_t processed = 0U;
         processed < RTL8139_RX_DRAIN_BUDGET; processed++) {
        const uint8_t* header;
        uint16_t rx_status;
        uint16_t wire_length;
        uint16_t frame_length;
        uint8_t next;

        if (rtl8139_in8(device, RTL8139_REG_CR) & RTL8139_CR_BUFE) break;
        header = device->rx_buffer + device->rx_offset;
        rx_status = rtl8139_read_u16(header);
        wire_length = rtl8139_read_u16(header + 2U);
        if (wire_length < RTL8139_MIN_WIRE_SIZE ||
            wire_length > RTL8139_MAX_WIRE_SIZE) {
            device->status.rx_errors++;
            device->status.rx_dropped++;
            device->status.last_error = ERR_INVALID;
            rtl8139_reset_receiver(device);
            LOG_WARN("RTL8139", "Ring RX continha comprimento invalido");
            break;
        }
        frame_length = wire_length - RTL8139_FCS_SIZE;
        if (!(rx_status & RTL8139_RX_STATUS_ROK) ||
            (rx_status & RTL8139_RX_STATUS_ERROR_MASK)) {
            device->status.rx_errors++;
            device->status.rx_dropped++;
            rtl8139_advance_rx(device, wire_length);
            continue;
        }
        next = (uint8_t)((device->rx_queue_tail + 1U) %
                         RTL8139_RX_QUEUE_SLOT_COUNT);
        if (next == device->rx_queue_head) {
            device->status.rx_queue_dropped++;
            device->status.rx_dropped++;
            rtl8139_advance_rx(device, wire_length);
            continue;
        }
        device->rx_queue[device->rx_queue_tail].length = frame_length;
        kmemcpy(device->rx_queue[device->rx_queue_tail].data,
                header + RTL8139_RX_HEADER_SIZE, frame_length);
        rtl8139_memory_barrier();
        device->rx_queue_tail = next;
        device->status.rx_packets++;
        rtl8139_advance_rx(device, wire_length);
    }
    device->rx_pending =
        device->rx_queue_head != device->rx_queue_tail;
    device->status.rx_queue_depth =
        device->rx_queue_tail >= device->rx_queue_head ?
        device->rx_queue_tail - device->rx_queue_head :
        RTL8139_RX_QUEUE_SLOT_COUNT - device->rx_queue_head +
        device->rx_queue_tail;
    if (device->status.rx_queue_depth >
        device->status.rx_queue_high_water) {
        device->status.rx_queue_high_water =
            device->status.rx_queue_depth;
    }
}

static int rtl8139_receive_frame(void* driver_context, uint8_t* data,
                                 uint16_t capacity,
                                 uint16_t* out_length,
                                 uint8_t* out_received) {
    rtl8139_device_t* device =
        (rtl8139_device_t*)driver_context;
    uint8_t head;
    uint16_t frame_length;

    if (!device || !data || !out_length || !out_received) {
        LOG_ERROR("RTL8139", "Argumento nulo ao receber frame");
        return ERR_NULL;
    }
    *out_length = 0;
    *out_received = 0;
    if (!device->status.initialized) {
        LOG_ERROR("RTL8139", "Recepcao sem instancia ativa");
        return ERR_STATE;
    }
    head = device->rx_queue_head;
    if (head == device->rx_queue_tail) return OK;
    frame_length = device->rx_queue[head].length;
    if (capacity < frame_length) {
        LOG_ERROR("RTL8139", "Buffer insuficiente para frame recebido");
        return ERR_OVERFLOW;
    }
    kmemcpy(data, device->rx_queue[head].data, frame_length);
    device->rx_queue_head =
        (uint8_t)((head + 1U) % RTL8139_RX_QUEUE_SLOT_COUNT);
    device->rx_pending =
        device->rx_queue_head != device->rx_queue_tail;
    device->status.rx_queue_depth =
        device->rx_queue_tail >= device->rx_queue_head ?
        device->rx_queue_tail - device->rx_queue_head :
        RTL8139_RX_QUEUE_SLOT_COUNT - device->rx_queue_head +
        device->rx_queue_tail;
    device->status.last_error = OK;
    *out_length = frame_length;
    *out_received = 1;
    return OK;
}

static int rtl8139_service_pending(void* driver_context) {
    rtl8139_device_t* device = (rtl8139_device_t*)driver_context;
    uint16_t status;
    uint32_t rx_interrupts;
    uint32_t flags;

    if (!device || !device->used || !device->status.initialized) {
        return ERR_STATE;
    }
    flags = rtl8139_irq_save();
    status = device->pending_irq_status;
    rx_interrupts = device->pending_rx_interrupts;
    device->pending_irq_status = 0U;
    device->pending_rx_interrupts = 0U;
    rtl8139_irq_restore(flags);
    if (!status &&
        (rtl8139_in8(device, RTL8139_REG_CR) & RTL8139_CR_BUFE)) {
        return OK;
    }
    device->status.rx_interrupts += rx_interrupts;
    if (status & RTL8139_INT_ROK) {
        device->rx_pending = 1;
    }
    if (status & (RTL8139_INT_RER | RTL8139_INT_ROVW |
                  RTL8139_INT_FOVW | RTL8139_INT_SERR)) {
        device->status.rx_errors++;
        device->status.rx_dropped++;
    }
    if (status & (RTL8139_INT_ROVW | RTL8139_INT_FOVW)) {
        device->rx_needs_reset = 1;
        device->status.rx_queue_dropped++;
        rtl8139_reset_receiver(device);
    }
    if (status & RTL8139_INT_TER) device->status.tx_errors++;
    if (status & RTL8139_INT_PUN_LINK) rtl8139_update_link(device);
    if (!(rtl8139_in8(device, RTL8139_REG_CR) & RTL8139_CR_BUFE)) {
        rtl8139_drain_rx(device);
    }
    return OK;
}

static void rtl8139_bottom_half(void* context) {
    rtl8139_device_t* device = (rtl8139_device_t*)context;
    int result = rtl8139_service_pending(device);

    if (result != OK) {
        if (device) device->status.last_error = result;
        LOG_ERROR_CODE("RTL8139", result, "Bottom-Half RTL8139 falhou");
    }
}

void rtl8139_handler(registers_t* regs) {
    uint8_t irq_line;

    if (!regs || regs->int_no < 32U || regs->int_no > 47U) return;
    irq_line = (uint8_t)(regs->int_no - 32U);
    for (uint32_t index = 0; index < RTL8139_DEVICE_CAPACITY; index++) {
        rtl8139_device_t* device = &rtl8139_devices[index];

        if (device->used && device->status.initialized &&
            device->io_base && device->irq == irq_line) {
            uint16_t status = rtl8139_in16(device, RTL8139_REG_ISR);

            if (!status || status == 0xFFFFU) continue;
            rtl8139_out16(device, RTL8139_REG_ISR, status);
            device->pending_irq_status |= status;
            if (status & RTL8139_INT_ROK) device->pending_rx_interrupts++;
            (void)irq_deferred_schedule(&device->bottom_half_work);
        }
    }
}
