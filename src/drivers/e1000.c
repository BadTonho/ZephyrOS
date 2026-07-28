#include "drivers/e1000.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/string.h"
#include "core/timer.h"
#include "drivers/pci.h"
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
#define E1000_IRQ_VECTOR_BASE 32U
#define E1000_IRQ_MAX 15U
#define E1000_IRQ_UNKNOWN 0xFFU
#define E1000_DESCRIPTOR_COUNT 8U
#define E1000_RX_QUEUE_SLOT_COUNT (E1000_RX_QUEUE_CAPACITY + 1U)
#define E1000_FRAME_BUFFER_SIZE 2048U
#define E1000_MIN_FRAME_SIZE 14U
#define E1000_MAC_LOW_BYTES 4U
#define E1000_BUFFER_PAGES ((E1000_DESCRIPTOR_COUNT * E1000_FRAME_BUFFER_SIZE) / PAGE_SIZE)

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
#define E1000_INT_RXO 0x00000040U
#define E1000_INT_RXT0 0x00000080U
#define E1000_INT_RXSEQ 0x00000008U
#define E1000_INT_MASK (E1000_INT_TXDW | E1000_INT_LSC | E1000_INT_RXO | E1000_INT_RXT0 | E1000_INT_RXSEQ)

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

static e1000_status_t e1000_status;
static volatile uint32_t* e1000_mmio = 0;
static volatile e1000_rx_descriptor_t* e1000_rx_ring = 0;
static volatile e1000_tx_descriptor_t* e1000_tx_ring = 0;
static uint8_t* e1000_rx_buffers = 0;
static uint8_t* e1000_tx_buffers = 0;
static uint8_t e1000_rx_next = 0;
static uint8_t e1000_tx_next = 0;
static uint8_t e1000_tx_in_use[E1000_DESCRIPTOR_COUNT];
static e1000_rx_queue_entry_t
    e1000_rx_queue[E1000_RX_QUEUE_SLOT_COUNT];
static volatile uint8_t e1000_rx_queue_head = 0;
static volatile uint8_t e1000_rx_queue_tail = 0;
static volatile uint8_t e1000_rx_pending = 0;

static uint32_t e1000_read(uint32_t offset) {
    return e1000_mmio[offset / sizeof(uint32_t)];
}

static void e1000_write(uint32_t offset, uint32_t value) {
    e1000_mmio[offset / sizeof(uint32_t)] = value;
}

static void e1000_memory_barrier(void) {
    asm volatile("" : : : "memory");
}

static void e1000_update_link(void) {
    if (e1000_status.initialized && e1000_mmio) {
        e1000_status.link_up =
            (e1000_read(E1000_REG_STATUS) & E1000_STATUS_LU) ? 1U : 0U;
    }
}

static void e1000_reset_status(int error_code) {
    kmemset(&e1000_status, 0, sizeof(e1000_status));
    e1000_rx_queue_head = 0;
    e1000_rx_queue_tail = 0;
    e1000_rx_pending = 0;
    e1000_status.last_error = error_code;
}

static uint8_t e1000_rx_queue_depth(void) {
    uint8_t head = e1000_rx_queue_head;
    uint8_t tail = e1000_rx_queue_tail;

    if (tail >= head) return tail - head;
    return (uint8_t)(E1000_RX_QUEUE_SLOT_COUNT - head + tail);
}

static void e1000_disable_hardware(void) {
    if (!e1000_mmio) return;
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
    e1000_write(E1000_REG_RCTL,
                e1000_read(E1000_REG_RCTL) & ~E1000_RCTL_EN);
    e1000_write(E1000_REG_TCTL,
                e1000_read(E1000_REG_TCTL) & ~E1000_TCTL_EN);
    (void)e1000_read(E1000_REG_ICR);
}

static void e1000_release_dma(void) {
    if (e1000_tx_buffers) {
        pmm_free_pages(e1000_tx_buffers, E1000_BUFFER_PAGES);
        e1000_tx_buffers = 0;
    }
    if (e1000_rx_buffers) {
        pmm_free_pages(e1000_rx_buffers, E1000_BUFFER_PAGES);
        e1000_rx_buffers = 0;
    }
    if (e1000_tx_ring) {
        pmm_free_page((void*)e1000_tx_ring);
        e1000_tx_ring = 0;
    }
    if (e1000_rx_ring) {
        pmm_free_page((void*)e1000_rx_ring);
        e1000_rx_ring = 0;
    }
}

static int e1000_allocate_dma(void) {
    e1000_rx_ring = (volatile e1000_rx_descriptor_t*)pmm_alloc_page();
    e1000_tx_ring = (volatile e1000_tx_descriptor_t*)pmm_alloc_page();
    e1000_rx_buffers = (uint8_t*)pmm_alloc_pages(E1000_BUFFER_PAGES);
    e1000_tx_buffers = (uint8_t*)pmm_alloc_pages(E1000_BUFFER_PAGES);
    if (!e1000_rx_ring || !e1000_tx_ring || !e1000_rx_buffers ||
        !e1000_tx_buffers) {
        LOG_ERROR("E1000", "Falha ao alocar memoria DMA");
        e1000_release_dma();
        return ERR_MEM;
    }
    kmemset((void*)e1000_rx_ring, 0, PAGE_SIZE);
    kmemset((void*)e1000_tx_ring, 0, PAGE_SIZE);
    kmemset(e1000_rx_buffers, 0,
            E1000_DESCRIPTOR_COUNT * E1000_FRAME_BUFFER_SIZE);
    kmemset(e1000_tx_buffers, 0,
            E1000_DESCRIPTOR_COUNT * E1000_FRAME_BUFFER_SIZE);
    kmemset(e1000_tx_in_use, 0, sizeof(e1000_tx_in_use));
    return OK;
}

static int e1000_map_mmio(uint32_t bar0) {
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
                            PAGING_FLAG_PRESENT | PAGING_FLAG_WRITE) != OK) {
            LOG_ERROR("E1000", "Falha ao mapear BAR0 MMIO");
            return ERR_MEM;
        }
    }
    e1000_mmio = (volatile uint32_t*)base;
    return OK;
}

static int e1000_wait_reset(void) {
    uint32_t start = timer_get_ticks();
    uint32_t control = e1000_read(E1000_REG_CTRL);

    e1000_write(E1000_REG_CTRL, control | E1000_CTRL_RST);
    while (e1000_read(E1000_REG_CTRL) & E1000_CTRL_RST) {
        if (timer_get_ticks() - start >= E1000_RESET_TIMEOUT_TICKS) {
            LOG_ERROR("E1000", "Timeout no reset do controlador");
            return ERR_TIMEOUT;
        }
    }
    return OK;
}

static int e1000_read_mac(void) {
    uint32_t low = e1000_read(E1000_REG_RAL0);
    uint32_t high = e1000_read(E1000_REG_RAH0);
    uint8_t all_zero = 1;
    uint8_t all_ff = 1;

    for (uint32_t index = 0; index < E1000_MAC_ADDRESS_SIZE; index++) {
        uint32_t source = index < E1000_MAC_LOW_BYTES ? low : high;
        uint32_t shift = (index % E1000_MAC_LOW_BYTES) * 8U;
        e1000_status.mac_address[index] = (uint8_t)(source >> shift);
        if (e1000_status.mac_address[index] != 0U) all_zero = 0;
        if (e1000_status.mac_address[index] != 0xFFU) all_ff = 0;
    }
    if (all_zero || all_ff) {
        LOG_ERROR("E1000", "MAC invalido no controlador");
        return ERR_UNAVAILABLE;
    }
    return OK;
}

static void e1000_setup_rx(void) {
    for (uint32_t index = 0; index < E1000_DESCRIPTOR_COUNT; index++) {
        e1000_rx_ring[index].address =
            (uint32_t)(e1000_rx_buffers + index * E1000_FRAME_BUFFER_SIZE);
        e1000_rx_ring[index].status = 0;
        e1000_rx_ring[index].errors = 0;
    }
    e1000_memory_barrier();
    e1000_write(E1000_REG_RDBAL, (uint32_t)e1000_rx_ring);
    e1000_write(E1000_REG_RDBAH, 0);
    e1000_write(E1000_REG_RDLEN,
                E1000_DESCRIPTOR_COUNT * sizeof(e1000_rx_descriptor_t));
    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, E1000_DESCRIPTOR_COUNT - 1U);
    e1000_write(E1000_REG_RCTL,
                E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static void e1000_setup_tx(void) {
    e1000_memory_barrier();
    e1000_write(E1000_REG_TDBAL, (uint32_t)e1000_tx_ring);
    e1000_write(E1000_REG_TDBAH, 0);
    e1000_write(E1000_REG_TDLEN,
                E1000_DESCRIPTOR_COUNT * sizeof(e1000_tx_descriptor_t));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);
    e1000_write(E1000_REG_TIPG, E1000_TIPG_DEFAULT);
    e1000_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                E1000_TCTL_CT | E1000_TCTL_COLD);
}

static int e1000_prepare_device(const pci_device_t* pci) {
    if (pci->class != E1000_PCI_CLASS || pci->irq == E1000_IRQ_UNKNOWN ||
        pci->irq > E1000_IRQ_MAX) {
        LOG_ERROR("E1000", "Configuracao PCI do E1000 invalida");
        return ERR_UNAVAILABLE;
    }
    if (pci_enable_memory_and_bus_mastering(pci) != OK) {
        LOG_ERROR("E1000", "Falha ao habilitar MMIO e DMA no PCI");
        return ERR_UNAVAILABLE;
    }
    return e1000_map_mmio(pci->bar0);
}

static void e1000_fail_init(int result) {
    e1000_disable_hardware();
    e1000_release_dma();
    e1000_status.initialized = 0;
    e1000_status.link_up = 0;
    e1000_status.last_error = result;
}

int e1000_init(void) {
    pci_device_t* pci;
    int result;

    LOG_INFO("E1000", "Inicializando controlador E1000");
    if (e1000_status.initialized) {
        LOG_WARN("E1000", "Controlador E1000 ja estava inicializado");
        return OK;
    }
    e1000_reset_status(ERR_NOT_FOUND);
    pci = pci_get_device_by_id(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!pci) {
        LOG_WARN("E1000", "Controlador E1000 nao encontrado");
        return ERR_NOT_FOUND;
    }
    e1000_status.detected = 1;
    e1000_status.irq = pci->irq;
    e1000_status.bus = pci->bus;
    e1000_status.device = pci->device;
    e1000_status.function = pci->function;
    result = e1000_prepare_device(pci);
    if (result != OK) goto failed;
    e1000_write(E1000_REG_IMC, 0xFFFFFFFFU);
    (void)e1000_read(E1000_REG_ICR);
    result = e1000_wait_reset();
    if (result != OK) goto failed;
    result = e1000_read_mac();
    if (result != OK) goto failed;
    result = e1000_allocate_dma();
    if (result != OK) goto failed;
    e1000_setup_rx();
    e1000_setup_tx();
    result = idt_register_handler(E1000_IRQ_VECTOR_BASE + pci->irq,
                                  e1000_handler);
    if (result != OK) {
        LOG_ERROR("E1000", "Falha ao registrar IRQ do E1000");
        goto failed;
    }
    e1000_status.initialized = 1;
    e1000_status.last_error = OK;
    e1000_update_link();
    e1000_write(E1000_REG_IMS, E1000_INT_MASK);
    LOG_INFO("E1000", "Controlador E1000 inicializado com sucesso");
    return OK;

failed:
    e1000_fail_init(result);
    LOG_ERROR("E1000", "Falha ao inicializar controlador E1000");
    return result;
}

int e1000_get_status(e1000_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("E1000", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    e1000_update_link();
    *out_status = e1000_status;
    out_status->rx_queue_depth = e1000_rx_queue_depth();
    return OK;
}

int e1000_send_frame(const uint8_t* data, uint16_t length) {
    uint8_t descriptor;
    uint8_t next;
    uint32_t start;

    if (!data) {
        LOG_ERROR("E1000", "Frame nulo para transmissao");
        return ERR_NULL;
    }
    if (length < E1000_MIN_FRAME_SIZE || length > E1000_MAX_FRAME_SIZE) {
        LOG_ERROR("E1000", "Tamanho de frame Ethernet invalido");
        return ERR_INVALID;
    }
    if (!e1000_status.initialized) {
        LOG_ERROR("E1000", "Transmissao solicitada sem driver ativo");
        return ERR_STATE;
    }
    e1000_update_link();
    if (!e1000_status.link_up) {
        LOG_WARN("E1000", "Transmissao solicitada com link indisponivel");
        return ERR_UNAVAILABLE;
    }
    descriptor = e1000_tx_next;
    if (e1000_tx_in_use[descriptor] &&
        !(e1000_tx_ring[descriptor].status & E1000_TX_STATUS_DD)) {
        LOG_WARN("E1000", "Fila de transmissao temporariamente ocupada");
        return ERR_UNAVAILABLE;
    }
    for (uint32_t index = 0; index < length; index++) {
        e1000_tx_buffers[descriptor * E1000_FRAME_BUFFER_SIZE + index] =
            data[index];
    }
    e1000_tx_ring[descriptor].address =
        (uint32_t)(e1000_tx_buffers + descriptor * E1000_FRAME_BUFFER_SIZE);
    e1000_tx_ring[descriptor].length = length;
    e1000_tx_ring[descriptor].checksum_offset = 0;
    e1000_tx_ring[descriptor].command = E1000_TX_CMD_EOP |
                                      E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    e1000_tx_ring[descriptor].status = 0;
    e1000_tx_ring[descriptor].checksum_start = 0;
    e1000_tx_ring[descriptor].special = 0;
    e1000_tx_in_use[descriptor] = 1;
    next = (descriptor + 1U) % E1000_DESCRIPTOR_COUNT;
    e1000_memory_barrier();
    e1000_write(E1000_REG_TDT, next);
    e1000_tx_next = next;
    start = timer_get_ticks();
    while (!(e1000_tx_ring[descriptor].status & E1000_TX_STATUS_DD)) {
        if (timer_get_ticks() - start >= E1000_RESET_TIMEOUT_TICKS) {
            e1000_status.tx_errors++;
            e1000_status.last_error = ERR_TIMEOUT;
            LOG_ERROR("E1000", "Timeout ao transmitir frame Ethernet");
            return ERR_TIMEOUT;
        }
    }
    e1000_status.tx_packets++;
    e1000_status.last_error = OK;
    return OK;
}

static uint8_t e1000_enqueue_rx(uint8_t descriptor, uint16_t length) {
    uint8_t tail = e1000_rx_queue_tail;
    uint8_t next = (tail + 1U) % E1000_RX_QUEUE_SLOT_COUNT;
    uint8_t depth;

    if (next == e1000_rx_queue_head) return 0;
    e1000_rx_queue[tail].length = length;
    kmemcpy(e1000_rx_queue[tail].data,
            e1000_rx_buffers + descriptor * E1000_FRAME_BUFFER_SIZE,
            length);
    e1000_memory_barrier();
    e1000_rx_queue_tail = next;
    depth = e1000_rx_queue_depth();
    if (depth > e1000_status.rx_queue_high_water) {
        e1000_status.rx_queue_high_water = depth;
    }
    return 1;
}

static void e1000_poll_rx_descriptors(void) {
    for (uint32_t processed = 0; processed < E1000_DESCRIPTOR_COUNT;
         processed++) {
        uint8_t descriptor = e1000_rx_next;
        uint8_t status = e1000_rx_ring[descriptor].status;
        uint16_t length = e1000_rx_ring[descriptor].length;

        if (!(status & E1000_RX_STATUS_DD)) return;
        if (!(status & E1000_RX_STATUS_EOP) ||
            e1000_rx_ring[descriptor].errors ||
            length < E1000_MIN_FRAME_SIZE ||
            length > E1000_MAX_FRAME_SIZE) {
            e1000_status.rx_errors++;
            e1000_status.rx_dropped++;
        } else {
            e1000_status.rx_packets++;
            if (!e1000_enqueue_rx(descriptor, length)) {
                e1000_status.rx_queue_dropped++;
                e1000_status.rx_dropped++;
            }
        }
        e1000_rx_ring[descriptor].status = 0;
        e1000_rx_ring[descriptor].errors = 0;
        e1000_memory_barrier();
        e1000_write(E1000_REG_RDT, descriptor);
        e1000_rx_next = (descriptor + 1U) % E1000_DESCRIPTOR_COUNT;
    }
}

int e1000_has_pending_rx(uint8_t* out_pending) {
    if (!out_pending) {
        LOG_ERROR("E1000", "Destino nulo ao consultar RX pendente");
        return ERR_NULL;
    }
    if (!e1000_status.initialized) {
        LOG_ERROR("E1000", "Consulta RX pendente sem driver ativo");
        return ERR_STATE;
    }
    *out_pending = e1000_rx_pending ||
                   e1000_rx_queue_head != e1000_rx_queue_tail;
    return OK;
}

int e1000_receive_frame(uint8_t* data, uint16_t capacity,
                        uint16_t* out_length, uint8_t* out_received) {
    uint8_t head;
    uint16_t length;

    if (!data || !out_length || !out_received) {
        LOG_ERROR("E1000", "Argumento nulo ao receber frame");
        return ERR_NULL;
    }
    *out_length = 0;
    *out_received = 0;
    if (!e1000_status.initialized) {
        LOG_ERROR("E1000", "Recepcao solicitada sem driver ativo");
        return ERR_STATE;
    }
    head = e1000_rx_queue_head;
    if (head == e1000_rx_queue_tail) {
        if (!e1000_rx_pending) return OK;
        e1000_rx_pending = 0;
        /* A copia ocorre fora da IRQ para que nenhum protocolo dependa do
           buffer DMA depois que o descritor for reciclado. */
        e1000_poll_rx_descriptors();
        head = e1000_rx_queue_head;
        if (head == e1000_rx_queue_tail) return OK;
    }
    length = e1000_rx_queue[head].length;
    if (capacity < length) {
        LOG_ERROR("E1000", "Buffer insuficiente para frame recebido");
        return ERR_OVERFLOW;
    }
    kmemcpy(data, e1000_rx_queue[head].data, length);
    e1000_memory_barrier();
    e1000_rx_queue_head =
        (head + 1U) % E1000_RX_QUEUE_SLOT_COUNT;
    *out_length = length;
    *out_received = 1;
    e1000_status.last_error = OK;
    return OK;
}

void e1000_handler(registers_t* regs) {
    uint32_t cause;

    (void)regs;
    if (!e1000_status.initialized || !e1000_mmio) return;
    cause = e1000_read(E1000_REG_ICR);
    if (!cause) return;
    if (cause & (E1000_INT_RXT0 | E1000_INT_RXO)) {
        e1000_status.rx_interrupts++;
        e1000_rx_pending = 1;
    }
    if (cause & E1000_INT_LSC) e1000_update_link();
    if (cause & E1000_INT_RXO) {
        e1000_status.rx_errors++;
        e1000_status.rx_dropped++;
    }
    if (cause & E1000_INT_RXSEQ) {
        e1000_status.rx_errors++;
    }
}
