#include "drivers/e1000.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "core/irq_deferred.h"
#include "memory/paging.h"

#define E1000_VENDOR_ID 0x8086U
#define E1000_DEVICE_ID 0x100EU
#define E1000_PCI_CLASS 0x02U
#define E1000_MMIO_BAR_IO 0x01U
#define E1000_MMIO_BAR_TYPE_MASK 0x06U
#define E1000_MMIO_BAR_32BIT 0x00U
#define E1000_MMIO_BAR_ADDRESS_MASK 0xFFFFFFF0U
#define E1000_MMIO_REQUIRED_BYTES 0x6000U
#define E1000_RESET_TIMEOUT_TICKS 50U
#define E1000_IRQ_MAX 15U
#define E1000_IRQ_UNKNOWN 0xFFU
#define E1000_DESCRIPTOR_COUNT 8U
#define E1000_RX_QUEUE_CAPACITY 8U
#define E1000_RX_QUEUE_SLOT_COUNT (E1000_RX_QUEUE_CAPACITY + 1U)
#define E1000_FRAME_BUFFER_SIZE 2048U
#define E1000_MIN_FRAME_SIZE 14U
#define E1000_MAX_FRAME_SIZE 1518U
#define E1000_MAC_ADDRESS_SIZE 6U
#define E1000_MAC_LOW_BYTES 4U
#define E1000_BUFFER_PAGES \
    ((E1000_DESCRIPTOR_COUNT * E1000_FRAME_BUFFER_SIZE) / PAGE_SIZE)

#define E1000_REG_CTRL 0x0000U
#define E1000_REG_STATUS 0x0008U
#define E1000_REG_ICR 0x00C0U
#define E1000_REG_IMS 0x00D0U
#define E1000_REG_IMC 0x00D8U
#define E1000_REG_RCTL 0x0100U
#define E1000_REG_TCTL 0x0400U
#define E1000_REG_TIPG 0x0410U
#define E1000_REG_RDBAL 0x2800U
#define E1000_REG_RDBAH 0x2804U
#define E1000_REG_RDLEN 0x2808U
#define E1000_REG_RDH 0x2810U
#define E1000_REG_RDT 0x2818U
#define E1000_REG_TDBAL 0x3800U
#define E1000_REG_TDBAH 0x3804U
#define E1000_REG_TDLEN 0x3808U
#define E1000_REG_TDH 0x3810U
#define E1000_REG_TDT 0x3818U
#define E1000_REG_RAL0 0x5400U
#define E1000_REG_RAH0 0x5404U

#define E1000_CTRL_RST (1U << 26)
#define E1000_STATUS_LU (1U << 1)
#define E1000_RCTL_EN (1U << 1)
#define E1000_RCTL_BAM (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)
#define E1000_TCTL_EN (1U << 1)
#define E1000_TCTL_PSP (1U << 3)
#define E1000_TCTL_CT (0x0FU << 4)
#define E1000_TCTL_COLD (0x3FU << 12)
#define E1000_TIPG_DEFAULT 0x0060200AU
#define E1000_RX_STATUS_DD 0x01U
#define E1000_RX_STATUS_EOP 0x02U
#define E1000_TX_STATUS_DD 0x01U
#define E1000_TX_CMD_EOP 0x01U
#define E1000_TX_CMD_IFCS 0x02U
#define E1000_TX_CMD_RS 0x08U
#define E1000_INT_TXDW 0x00000001U
#define E1000_INT_LSC 0x00000004U
#define E1000_INT_RXSEQ 0x00000008U
#define E1000_INT_RXO 0x00000040U
#define E1000_INT_RXT0 0x00000080U
#define E1000_INT_MASK \
    (E1000_INT_TXDW | E1000_INT_LSC | E1000_INT_RXSEQ | \
     E1000_INT_RXO | E1000_INT_RXT0)

typedef struct {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_descriptor_t;

typedef struct {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed)) e1000_tx_descriptor_t;

typedef struct {
    uint16_t length;
    uint8_t data[E1000_MAX_FRAME_SIZE];
} e1000_rx_queue_entry_t;

typedef struct {
    uint8_t used;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t irq;
    uint8_t mac_address[E1000_MAC_ADDRESS_SIZE];
    ethernet_driver_status_t status;
    volatile uint32_t* mmio;
    volatile e1000_rx_descriptor_t* rx_ring;
    volatile e1000_tx_descriptor_t* tx_ring;
    uint8_t* rx_buffers;
    uint8_t* tx_buffers;
    uint8_t rx_next;
    uint8_t tx_next;
    uint8_t tx_in_use[E1000_DESCRIPTOR_COUNT];
    e1000_rx_queue_entry_t rx_queue[E1000_RX_QUEUE_SLOT_COUNT];
    volatile uint8_t rx_queue_head;
    volatile uint8_t rx_queue_tail;
    volatile uint8_t rx_pending;
    volatile uint32_t pending_irq_causes;
    volatile uint32_t pending_rx_interrupts;
    irq_deferred_work_t bottom_half_work;
} e1000_device_t;

static e1000_device_t e1000_devices[E1000_DEVICE_CAPACITY];

static uint32_t e1000_read(e1000_device_t* device, uint32_t offset) {
    return device->mmio[offset / sizeof(uint32_t)];
}

static void e1000_write(e1000_device_t* device, uint32_t offset,
                        uint32_t value) {
    device->mmio[offset / sizeof(uint32_t)] = value;
}

static void e1000_memory_barrier(void) {
    asm volatile("" : : : "memory");
}

static uint32_t e1000_irq_save(void) {
    uint32_t flags;

    asm volatile("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static void e1000_irq_restore(uint32_t flags) {
    if (flags & (1U << 9U)) asm volatile("sti" : : : "memory");
}

static void e1000_update_link(e1000_device_t* device) {
    if (device && device->status.initialized && device->mmio) {
        device->status.link_up =
            (e1000_read(device, E1000_REG_STATUS) & E1000_STATUS_LU) ?
            1U : 0U;
    }
}

static uint8_t e1000_rx_queue_depth(e1000_device_t* device) {
    uint8_t head = device->rx_queue_head;
    uint8_t tail = device->rx_queue_tail;

    if (tail >= head) return tail - head;
    return (uint8_t)(E1000_RX_QUEUE_SLOT_COUNT - head + tail);
}

static void e1000_disable_hardware(e1000_device_t* device) {
    if (!device || !device->mmio) return;
    e1000_write(device, E1000_REG_IMC, 0xFFFFFFFFU);
    e1000_write(device, E1000_REG_RCTL,
                e1000_read(device, E1000_REG_RCTL) & ~E1000_RCTL_EN);
    e1000_write(device, E1000_REG_TCTL,
                e1000_read(device, E1000_REG_TCTL) & ~E1000_TCTL_EN);
    (void)e1000_read(device, E1000_REG_ICR);
}

static void e1000_release_dma(e1000_device_t* device) {
    if (device->tx_buffers) {
        pmm_free_pages(device->tx_buffers, E1000_BUFFER_PAGES);
        device->tx_buffers = NULL;
    }
    if (device->rx_buffers) {
        pmm_free_pages(device->rx_buffers, E1000_BUFFER_PAGES);
        device->rx_buffers = NULL;
    }
    if (device->tx_ring) {
        pmm_free_page((void*)device->tx_ring);
        device->tx_ring = NULL;
    }
    if (device->rx_ring) {
        pmm_free_page((void*)device->rx_ring);
        device->rx_ring = NULL;
    }
}

static int e1000_allocate_dma(e1000_device_t* device) {
    device->rx_ring =
        (volatile e1000_rx_descriptor_t*)pmm_alloc_page();
    device->tx_ring =
        (volatile e1000_tx_descriptor_t*)pmm_alloc_page();
    device->rx_buffers = (uint8_t*)pmm_alloc_pages(E1000_BUFFER_PAGES);
    device->tx_buffers = (uint8_t*)pmm_alloc_pages(E1000_BUFFER_PAGES);
    if (!device->rx_ring || !device->tx_ring ||
        !device->rx_buffers || !device->tx_buffers) {
        LOG_ERROR("E1000", "Falha ao alocar memoria DMA");
        e1000_release_dma(device);
        return ERR_MEM;
    }
    kmemset((void*)device->rx_ring, 0, PAGE_SIZE);
    kmemset((void*)device->tx_ring, 0, PAGE_SIZE);
    kmemset(device->rx_buffers, 0,
            E1000_DESCRIPTOR_COUNT * E1000_FRAME_BUFFER_SIZE);
    kmemset(device->tx_buffers, 0,
            E1000_DESCRIPTOR_COUNT * E1000_FRAME_BUFFER_SIZE);
    kmemset(device->tx_in_use, 0, sizeof(device->tx_in_use));
    return OK;
}

static int e1000_map_mmio(e1000_device_t* device, uint32_t bar0) {
    uint32_t base = bar0 & E1000_MMIO_BAR_ADDRESS_MASK;
    uint32_t map_start = base & ~(PAGE_SIZE - 1U);
    uint32_t map_end = base + E1000_MMIO_REQUIRED_BYTES;

    if ((bar0 & E1000_MMIO_BAR_IO) ||
        (bar0 & E1000_MMIO_BAR_TYPE_MASK) != E1000_MMIO_BAR_32BIT ||
        !base) {
        LOG_ERROR("E1000", "BAR0 MMIO incompativel");
        return ERR_UNAVAILABLE;
    }
    map_end = (map_end + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
    for (uint32_t address = map_start; address < map_end;
         address += PAGE_SIZE) {
        if (paging_map_page(address, address,
                            PAGING_FLAG_PRESENT |
                            PAGING_FLAG_WRITE) != OK) {
            LOG_ERROR("E1000", "Falha ao mapear BAR0 MMIO");
            return ERR_MEM;
        }
    }
    device->mmio = (volatile uint32_t*)base;
    return OK;
}

static int e1000_wait_reset(e1000_device_t* device) {
    uint32_t start = timer_get_ticks();
    uint32_t control = e1000_read(device, E1000_REG_CTRL);

    e1000_write(device, E1000_REG_CTRL, control | E1000_CTRL_RST);
    while (e1000_read(device, E1000_REG_CTRL) & E1000_CTRL_RST) {
        if (timer_get_ticks() - start >= E1000_RESET_TIMEOUT_TICKS) {
            LOG_ERROR("E1000", "Timeout no reset do controlador");
            return ERR_TIMEOUT;
        }
    }
    return OK;
}

static int e1000_read_mac(e1000_device_t* device) {
    uint32_t low = e1000_read(device, E1000_REG_RAL0);
    uint32_t high = e1000_read(device, E1000_REG_RAH0);
    uint8_t all_zero = 1;
    uint8_t all_ff = 1;

    for (uint32_t index = 0; index < E1000_MAC_ADDRESS_SIZE; index++) {
        uint32_t source = index < E1000_MAC_LOW_BYTES ? low : high;
        uint32_t shift = (index % E1000_MAC_LOW_BYTES) * 8U;
        device->mac_address[index] = (uint8_t)(source >> shift);
        if (device->mac_address[index] != 0U) all_zero = 0;
        if (device->mac_address[index] != 0xFFU) all_ff = 0;
    }
    if (all_zero || all_ff) {
        LOG_ERROR("E1000", "MAC invalido no controlador");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static void e1000_setup_rx(e1000_device_t* device) {
    for (uint32_t index = 0; index < E1000_DESCRIPTOR_COUNT; index++) {
        device->rx_ring[index].address =
            (uint32_t)(device->rx_buffers +
                       index * E1000_FRAME_BUFFER_SIZE);
        device->rx_ring[index].status = 0;
        device->rx_ring[index].errors = 0;
    }
    e1000_memory_barrier();
    e1000_write(device, E1000_REG_RDBAL, (uint32_t)device->rx_ring);
    e1000_write(device, E1000_REG_RDBAH, 0);
    e1000_write(device, E1000_REG_RDLEN,
                E1000_DESCRIPTOR_COUNT *
                sizeof(e1000_rx_descriptor_t));
    e1000_write(device, E1000_REG_RDH, 0);
    e1000_write(device, E1000_REG_RDT, E1000_DESCRIPTOR_COUNT - 1U);
    e1000_write(device, E1000_REG_RCTL,
                E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static void e1000_setup_tx(e1000_device_t* device) {
    e1000_memory_barrier();
    e1000_write(device, E1000_REG_TDBAL, (uint32_t)device->tx_ring);
    e1000_write(device, E1000_REG_TDBAH, 0);
    e1000_write(device, E1000_REG_TDLEN,
                E1000_DESCRIPTOR_COUNT *
                sizeof(e1000_tx_descriptor_t));
    e1000_write(device, E1000_REG_TDH, 0);
    e1000_write(device, E1000_REG_TDT, 0);
    e1000_write(device, E1000_REG_TIPG, E1000_TIPG_DEFAULT);
    e1000_write(device, E1000_REG_TCTL,
                E1000_TCTL_EN | E1000_TCTL_PSP |
                E1000_TCTL_CT | E1000_TCTL_COLD);
}

static int e1000_prepare_device(e1000_device_t* device,
                                const pci_device_t* pci) {
    int result;

    if (pci->vendor_id != E1000_VENDOR_ID ||
        pci->device_id != E1000_DEVICE_ID ||
        pci->class != E1000_PCI_CLASS ||
        pci->irq == E1000_IRQ_UNKNOWN || pci->irq > E1000_IRQ_MAX) {
        LOG_ERROR("E1000", "Configuracao PCI do E1000 invalida");
        return ERR_UNAVAILABLE;
    }
    result = pci_enable_memory_and_bus_mastering(pci);
    if (result != OK) {
        LOG_ERROR("E1000", "Falha ao habilitar MMIO e DMA no PCI");
        return result;
    }
    return e1000_map_mmio(device, pci->bar0);
}

static e1000_device_t* e1000_find_device(const pci_device_t* pci) {
    for (uint32_t index = 0; index < E1000_DEVICE_CAPACITY; index++) {
        e1000_device_t* device = &e1000_devices[index];

        if (device->used && device->bus == pci->bus &&
            device->device == pci->device &&
            device->function == pci->function) return device;
    }
    return NULL;
}

static e1000_device_t* e1000_allocate_device(void) {
    for (uint32_t index = 0; index < E1000_DEVICE_CAPACITY; index++) {
        if (!e1000_devices[index].used) {
            kmemset(&e1000_devices[index], 0,
                    sizeof(e1000_devices[index]));
            e1000_devices[index].used = 1;
            return &e1000_devices[index];
        }
    }
    return NULL;
}

static int e1000_copy_interface_id(char* destination,
                                   const char* source) {
    uint32_t index = 0;

    if (!destination || !source) {
        LOG_ERROR("E1000", "ID de interface nulo");
        return ERR_NULL;
    }
    while (source[index] && index + 1U < ETHERNET_INTERFACE_ID_SIZE) {
        destination[index] = source[index];
        index++;
    }
    if (!index || source[index]) {
        LOG_ERROR("E1000", "ID de interface invalido");
        return source[index] ? ERR_OVERFLOW : ERR_INVALID;
    }
    destination[index] = '\0';
    return OK;
}

static int e1000_get_driver_status(
    void* driver_context, ethernet_driver_status_t* out_status) {
    e1000_device_t* device = (e1000_device_t*)driver_context;

    if (!device || !out_status) {
        LOG_ERROR("E1000", "Argumento nulo ao consultar estado");
        return ERR_NULL;
    }
    if (!device->used || !device->status.initialized) {
        LOG_ERROR("E1000", "Consulta de instancia inativa");
        return ERR_STATE;
    }
    e1000_update_link(device);
    device->status.rx_queue_depth = e1000_rx_queue_depth(device);
    *out_status = device->status;
    return OK;
}

static int e1000_fill_interface(
    e1000_device_t* device, const char* interface_id,
    ethernet_interface_t* out_interface);
static int e1000_send_frame(void* driver_context,
                            const uint8_t* data, uint16_t length);
static int e1000_has_pending_rx(void* driver_context,
                                uint8_t* out_pending);
static int e1000_receive_frame(void* driver_context, uint8_t* data,
                               uint16_t capacity, uint16_t* out_length,
                               uint8_t* out_received);
static int e1000_service_pending(void* driver_context);
static void e1000_poll_rx_descriptors(e1000_device_t* device);
static void e1000_bottom_half(void* context);

static int e1000_fill_interface(
    e1000_device_t* device, const char* interface_id,
    ethernet_interface_t* out_interface) {
    int result;

    kmemset(out_interface, 0, sizeof(*out_interface));
    result = e1000_copy_interface_id(out_interface->interface_id,
                                    interface_id);
    if (result != OK) return result;
    out_interface->initialized = 1;
    kmemcpy(out_interface->mac_address, device->mac_address,
            E1000_MAC_ADDRESS_SIZE);
    out_interface->driver_context = device;
    out_interface->get_driver_status = e1000_get_driver_status;
    out_interface->service_pending = e1000_service_pending;
    out_interface->rx_pending = e1000_has_pending_rx;
    out_interface->receive_frame = e1000_receive_frame;
    out_interface->send_frame = e1000_send_frame;
    return OK;
}

static void e1000_fail_init(e1000_device_t* device, int result) {
    e1000_disable_hardware(device);
    e1000_release_dma(device);
    kmemset(device, 0, sizeof(*device));
    (void)result;
}

int e1000_init(const pci_device_t* pci, const char* interface_id,
               ethernet_interface_t* out_interface) {
    e1000_device_t* device;
    int result;

    LOG_INFO("E1000", "Inicializando controlador E1000");
    if (!pci || !interface_id || !out_interface) {
        LOG_ERROR("E1000", "Argumento nulo na inicializacao");
        return ERR_NULL;
    }
    device = e1000_find_device(pci);
    if (device) {
        result = e1000_fill_interface(device, interface_id,
                                      out_interface);
        if (result == OK) {
            LOG_INFO("E1000", "Controlador E1000 inicializado com sucesso");
        }
        return result;
    }
    device = e1000_allocate_device();
    if (!device) {
        LOG_ERROR("E1000", "Limite de controladores E1000 atingido");
        return ERR_OVERFLOW;
    }
    device->bus = pci->bus;
    device->device = pci->device;
    device->function = pci->function;
    device->irq = pci->irq;
    device->status.last_error = ERR_STATE;
    result = e1000_prepare_device(device, pci);
    if (result != OK) goto failed;
    e1000_write(device, E1000_REG_IMC, 0xFFFFFFFFU);
    (void)e1000_read(device, E1000_REG_ICR);
    result = e1000_wait_reset(device);
    if (result != OK) goto failed;
    result = e1000_read_mac(device);
    if (result != OK) goto failed;
    result = e1000_allocate_dma(device);
    if (result != OK) goto failed;
    e1000_setup_rx(device);
    e1000_setup_tx(device);
    result = irq_deferred_work_init(&device->bottom_half_work, "E1000",
                                    pci->irq, e1000_bottom_half, device);
    if (result != OK) goto failed;
    result = idt_register_shared_irq_handler(pci->irq, e1000_handler);
    if (result != OK) {
        LOG_ERROR("E1000", "Falha ao registrar IRQ compartilhada");
        goto failed;
    }
    device->status.initialized = 1;
    device->status.last_error = OK;
    e1000_update_link(device);
    e1000_write(device, E1000_REG_IMS, E1000_INT_MASK);
    result = e1000_fill_interface(device, interface_id, out_interface);
    if (result != OK) goto failed;
    LOG_INFO("E1000", "Controlador E1000 inicializado com sucesso");
    return OK;

failed:
    e1000_fail_init(device, result);
    LOG_ERROR("E1000", "Falha ao inicializar controlador E1000");
    return result;
}

static int e1000_send_frame(void* driver_context,
                            const uint8_t* data, uint16_t length) {
    e1000_device_t* device = (e1000_device_t*)driver_context;
    uint8_t descriptor;
    uint8_t next;
    uint32_t start;

    if (!device || !data) {
        LOG_ERROR("E1000", "Argumento nulo para transmissao");
        return ERR_NULL;
    }
    if (length < E1000_MIN_FRAME_SIZE ||
        length > E1000_MAX_FRAME_SIZE) {
        LOG_ERROR("E1000", "Tamanho de frame Ethernet invalido");
        return ERR_INVALID;
    }
    if (!device->status.initialized) {
        LOG_ERROR("E1000", "Transmissao sem instancia ativa");
        return ERR_STATE;
    }
    e1000_update_link(device);
    if (!device->status.link_up) {
        LOG_WARN("E1000", "Transmissao com link indisponivel");
        return ERR_UNAVAILABLE;
    }
    descriptor = device->tx_next;
    if (device->tx_in_use[descriptor] &&
        !(device->tx_ring[descriptor].status & E1000_TX_STATUS_DD)) {
        LOG_WARN("E1000", "Fila de transmissao ocupada");
        return ERR_UNAVAILABLE;
    }
    kmemcpy(device->tx_buffers +
            descriptor * E1000_FRAME_BUFFER_SIZE, data, length);
    device->tx_ring[descriptor].address =
        (uint32_t)(device->tx_buffers +
                   descriptor * E1000_FRAME_BUFFER_SIZE);
    device->tx_ring[descriptor].length = length;
    device->tx_ring[descriptor].checksum_offset = 0;
    device->tx_ring[descriptor].command =
        E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    device->tx_ring[descriptor].status = 0;
    device->tx_ring[descriptor].checksum_start = 0;
    device->tx_ring[descriptor].special = 0;
    device->tx_in_use[descriptor] = 1;
    next = (descriptor + 1U) % E1000_DESCRIPTOR_COUNT;
    e1000_memory_barrier();
    e1000_write(device, E1000_REG_TDT, next);
    device->tx_next = next;
    start = timer_get_ticks();
    while (!(device->tx_ring[descriptor].status &
             E1000_TX_STATUS_DD)) {
        if (timer_get_ticks() - start >= E1000_RESET_TIMEOUT_TICKS) {
            device->status.tx_errors++;
            device->status.last_error = ERR_TIMEOUT;
            LOG_ERROR("E1000", "Timeout ao transmitir frame Ethernet");
            return ERR_TIMEOUT;
        }
    }
    device->status.tx_packets++;
    device->status.last_error = OK;
    return OK;
}

static uint8_t e1000_enqueue_rx(e1000_device_t* device,
                                uint8_t descriptor, uint16_t length) {
    uint8_t tail = device->rx_queue_tail;
    uint8_t next = (tail + 1U) % E1000_RX_QUEUE_SLOT_COUNT;
    uint8_t depth;

    if (next == device->rx_queue_head) return 0;
    device->rx_queue[tail].length = length;
    kmemcpy(device->rx_queue[tail].data,
            device->rx_buffers +
            descriptor * E1000_FRAME_BUFFER_SIZE, length);
    e1000_memory_barrier();
    device->rx_queue_tail = next;
    depth = e1000_rx_queue_depth(device);
    if (depth > device->status.rx_queue_high_water) {
        device->status.rx_queue_high_water = depth;
    }
    return 1;
}

static void e1000_poll_rx_descriptors(e1000_device_t* device) {
    for (uint32_t processed = 0; processed < E1000_DESCRIPTOR_COUNT;
         processed++) {
        uint8_t descriptor = device->rx_next;
        uint8_t status = device->rx_ring[descriptor].status;
        uint16_t length = device->rx_ring[descriptor].length;

        if (!(status & E1000_RX_STATUS_DD)) return;
        if (!(status & E1000_RX_STATUS_EOP) ||
            device->rx_ring[descriptor].errors ||
            length < E1000_MIN_FRAME_SIZE ||
            length > E1000_MAX_FRAME_SIZE) {
            device->status.rx_errors++;
            device->status.rx_dropped++;
        } else {
            device->status.rx_packets++;
            if (!e1000_enqueue_rx(device, descriptor, length)) {
                device->status.rx_queue_dropped++;
                device->status.rx_dropped++;
            }
        }
        device->rx_ring[descriptor].status = 0;
        device->rx_ring[descriptor].errors = 0;
        e1000_memory_barrier();
        e1000_write(device, E1000_REG_RDT, descriptor);
        device->rx_next =
            (descriptor + 1U) % E1000_DESCRIPTOR_COUNT;
    }
}

static int e1000_has_pending_rx(void* driver_context,
                                uint8_t* out_pending) {
    e1000_device_t* device = (e1000_device_t*)driver_context;

    if (!device || !out_pending) {
        LOG_ERROR("E1000", "Argumento nulo ao consultar RX");
        return ERR_NULL;
    }
    if (!device->status.initialized) {
        LOG_ERROR("E1000", "Consulta RX sem instancia ativa");
        return ERR_STATE;
    }
    *out_pending = device->rx_pending ||
        device->rx_queue_head != device->rx_queue_tail;
    return OK;
}

static int e1000_receive_frame(void* driver_context, uint8_t* data,
                               uint16_t capacity, uint16_t* out_length,
                               uint8_t* out_received) {
    e1000_device_t* device = (e1000_device_t*)driver_context;
    uint8_t head;
    uint16_t length;

    if (!device || !data || !out_length || !out_received) {
        LOG_ERROR("E1000", "Argumento nulo ao receber frame");
        return ERR_NULL;
    }
    *out_length = 0;
    *out_received = 0;
    if (!device->status.initialized) {
        LOG_ERROR("E1000", "Recepcao sem instancia ativa");
        return ERR_STATE;
    }
    head = device->rx_queue_head;
    if (head == device->rx_queue_tail) {
        if (!device->rx_pending) return OK;
        device->rx_pending = 0;
        e1000_poll_rx_descriptors(device);
        head = device->rx_queue_head;
        if (head == device->rx_queue_tail) return OK;
    }
    length = device->rx_queue[head].length;
    if (capacity < length) {
        LOG_ERROR("E1000", "Buffer insuficiente para frame recebido");
        return ERR_OVERFLOW;
    }
    kmemcpy(data, device->rx_queue[head].data, length);
    e1000_memory_barrier();
    device->rx_queue_head =
        (head + 1U) % E1000_RX_QUEUE_SLOT_COUNT;
    *out_length = length;
    *out_received = 1;
    device->status.last_error = OK;
    return OK;
}

static int e1000_service_pending(void* driver_context) {
    e1000_device_t* device = (e1000_device_t*)driver_context;
    uint32_t causes;
    uint32_t rx_interrupts;
    uint32_t flags;

    if (!device || !device->used || !device->status.initialized) {
        return ERR_STATE;
    }
    flags = e1000_irq_save();
    causes = device->pending_irq_causes;
    rx_interrupts = device->pending_rx_interrupts;
    device->pending_irq_causes = 0U;
    device->pending_rx_interrupts = 0U;
    e1000_irq_restore(flags);
    if (!causes) return OK;
    device->status.rx_interrupts += rx_interrupts;
    if (causes & (E1000_INT_RXT0 | E1000_INT_RXO)) {
        device->rx_pending = 1U;
        e1000_poll_rx_descriptors(device);
    }
    if (causes & E1000_INT_LSC) e1000_update_link(device);
    if (causes & E1000_INT_RXO) {
        device->status.rx_errors++;
        device->status.rx_dropped++;
    }
    if (causes & E1000_INT_RXSEQ) device->status.rx_errors++;
    return OK;
}

static void e1000_bottom_half(void* context) {
    e1000_device_t* device = (e1000_device_t*)context;
    int result = e1000_service_pending(device);

    if (result != OK) {
        if (device) device->status.last_error = result;
        LOG_ERROR_CODE("E1000", result, "Bottom-Half E1000 falhou");
    }
}

void e1000_handler(registers_t* regs) {
    uint8_t irq_line;

    if (!regs || regs->int_no < 32U || regs->int_no > 47U) return;
    irq_line = (uint8_t)(regs->int_no - 32U);
    for (uint32_t index = 0; index < E1000_DEVICE_CAPACITY; index++) {
        e1000_device_t* device = &e1000_devices[index];

        if (device->used && device->status.initialized &&
            device->mmio && device->irq == irq_line) {
            uint32_t cause = e1000_read(device, E1000_REG_ICR);

            if (!cause) continue;
            device->pending_irq_causes |= cause;
            if (cause & (E1000_INT_RXT0 | E1000_INT_RXO)) {
                device->pending_rx_interrupts++;
            }
            (void)irq_deferred_schedule(&device->bottom_half_work);
        }
    }
}
