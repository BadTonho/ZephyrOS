#include <stdint.h>
#include <stdio.h>

#include "core/errors.h"
#include "core/log.h"
#include "drivers/ac97.h"
#include "drivers/idt.h"
#include "drivers/pci.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_IO_BASE 0xC000U
#define HOST_IO_SIZE 0x0100U
#define HOST_ALLOC_SIZE ((AC97_BUF_SIZE / 2U) * sizeof(uint32_t))
#define HOST_POWER_DEFAULT 0x0001U
#define HOST_STATUS_EVENTS (AC97_PO_LVBCI | AC97_PO_FIFOE)

typedef union {
    uint64_t alignment;
    uint8_t bytes[HOST_ALLOC_SIZE];
} host_allocation_t;

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint8_t host_io[HOST_IO_SIZE];
static host_allocation_t host_allocation;
static uint8_t host_allocation_used;
static uint8_t host_allocation_failure;
static uint32_t host_allocation_request;
static uint32_t host_free_count;
static uint32_t host_log_count;
static uint32_t host_bus_master_count;
static uint32_t host_irq_count;
static int host_irq_result;
static isr_handler_t host_irq_handler;
static pci_device_t host_pci;
static uint8_t host_pci_present;

void ac97_host_reset_devices(void);
uint32_t ac97_host_get_sample_rate(void);
void ac97_host_exercise_io(uint16_t port, uint8_t byte_value,
                           uint16_t word_value, uint32_t dword_value);

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
    printf("ZCOV_BEGIN|case=host:drivers:ac97|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:drivers:ac97|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:drivers:ac97|value=0x%08X\n",
           (uint32_t)result);
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
    host_log_count++;
}

static uint16_t host_port_offset(uint16_t port) {
    if (port < HOST_IO_BASE || port - HOST_IO_BASE >= HOST_IO_SIZE) {
        return HOST_IO_SIZE;
    }
    return (uint16_t)(port - HOST_IO_BASE);
}

uint8_t ac97_host_inb(uint16_t port) {
    uint16_t offset = host_port_offset(port);

    return offset < HOST_IO_SIZE ? host_io[offset] : 0U;
}

uint16_t ac97_host_inw(uint16_t port) {
    uint16_t offset = host_port_offset(port);

    if (offset >= HOST_IO_SIZE - 1U) return 0U;
    return (uint16_t)(host_io[offset] | ((uint16_t)host_io[offset + 1U] << 8U));
}

uint32_t ac97_host_inl(uint16_t port) {
    uint16_t offset = host_port_offset(port);

    if (offset >= HOST_IO_SIZE - 3U) return 0U;
    return (uint32_t)ac97_host_inw(port) |
           ((uint32_t)ac97_host_inw((uint16_t)(port + 2U)) << 16U);
}

void ac97_host_outb(uint16_t port, uint8_t value) {
    uint16_t offset = host_port_offset(port);

    if (offset < HOST_IO_SIZE) host_io[offset] = value;
}

void ac97_host_outw(uint16_t port, uint16_t value) {
    uint16_t offset = host_port_offset(port);

    if (offset >= HOST_IO_SIZE - 1U) return;
    if (offset == AC97_PO_REG_STATUS && !(value & AC97_PO_DMA_EN) &&
        (value & HOST_STATUS_EVENTS)) {
        uint16_t current = ac97_host_inw(port);
        value = (uint16_t)(current & (uint16_t)~value);
    }
    host_io[offset] = (uint8_t)value;
    host_io[offset + 1U] = (uint8_t)(value >> 8U);
}

void ac97_host_outl(uint16_t port, uint32_t value) {
    uint16_t offset = host_port_offset(port);

    if (offset >= HOST_IO_SIZE - 3U) return;
    host_io[offset] = (uint8_t)value;
    host_io[offset + 1U] = (uint8_t)(value >> 8U);
    host_io[offset + 2U] = (uint8_t)(value >> 16U);
    host_io[offset + 3U] = (uint8_t)(value >> 24U);
}

pci_device_t* pci_get_device(uint8_t class, uint8_t subclass) {
    if (!host_pci_present || host_pci.class != class ||
        host_pci.subclass != subclass) return 0;
    return &host_pci;
}

void pci_enable_bus_mastering(pci_device_t* device) {
    if (device) host_bus_master_count++;
}

int idt_register_handler(uint8_t irq, isr_handler_t handler) {
    if (host_irq_result != OK) return host_irq_result;
    if (!handler || irq >= IDT_IRQ_LINE_COUNT) return ERR_INVALID;
    host_irq_handler = handler;
    host_irq_count++;
    return OK;
}

void* kmalloc(uint32_t size) {
    host_allocation_request = size;
    if (host_allocation_failure || !size || size > HOST_ALLOC_SIZE ||
        host_allocation_used) return 0;
    host_allocation_used = 1U;
    return host_allocation.bytes;
}

void kfree(void* pointer) {
    if (pointer == host_allocation.bytes && host_allocation_used) {
        host_allocation_used = 0U;
        host_free_count++;
    }
}

static int expect_true(int condition, const char* expression) {
    if (condition) return OK;
    fprintf(stderr, "ac97-host: falhou: %s\n", expression);
    return ERR_STATE;
}

#define EXPECT(expression) \
    do { \
        if (expect_true((expression), #expression) != OK) failures++; \
    } while (0)

static void clear_bytes(uint8_t* bytes, uint32_t count) {
    for (uint32_t index = 0U; index < count; index++) bytes[index] = 0U;
}

static void reset_fixture(void) {
    ac97_host_reset_devices();
    clear_bytes(host_io, sizeof(host_io));
    clear_bytes(host_allocation.bytes, sizeof(host_allocation.bytes));
    host_allocation_used = 0U;
    host_allocation_failure = 0U;
    host_allocation_request = 0U;
    host_free_count = 0U;
    host_log_count = 0U;
    host_bus_master_count = 0U;
    host_irq_count = 0U;
    host_irq_result = OK;
    host_irq_handler = 0;
    host_pci_present = 1U;
    host_pci.vendor_id = 0x8086U;
    host_pci.device_id = 0x2415U;
    host_pci.class = AC97_PCI_CLASS;
    host_pci.subclass = AC97_PCI_SUBCLASS;
    host_pci.device = 3U;
    host_pci.irq = 5U;
    host_pci.bar0 = HOST_IO_BASE | 1U;
    host_pci.bar1 = 0xD000U | 1U;
    host_io[AC97_REG_POWER] = (uint8_t)HOST_POWER_DEFAULT;
    host_io[AC97_REG_EXT_AUDIO] = 0x00U;
    host_io[AC97_REG_EXT_AUDIO + 1U] = 0x02U;
}

static int check_uninitialized(void) {
    ac97_device_t* device;
    uint8_t frame[4] = {1U, 2U, 3U, 4U};

    reset_fixture();
    device = ac97_get_device();
    if (!device || device->initialized || device->sample_rate) return 10;
    ac97_play(frame, sizeof(frame), 8000U, 1U, 8U);
    ac97_stop();
    ac97_set_volume(31U);
    ac97_host_exercise_io(HOST_IO_BASE + 0x60U, 0x5AU, 0x1234U,
                          0x89ABCDEFU);
    return ac97_host_get_sample_rate() == 0U && !host_allocation_used ? 0 : 11;
}

static int check_missing_device(void) {
    reset_fixture();
    host_pci_present = 0U;
    ac97_init();
    if (ac97_get_device()->initialized) return 20;
    return host_log_count == 2U ? 0 : 21;
}

static int check_initialization(void) {
    ac97_device_t* device;

    reset_fixture();
    ac97_init();
    device = ac97_get_device();
    if (!device->initialized || device->io_base != HOST_IO_BASE ||
        device->ctrl_base != 0xD000U || device->irq != 5U ||
        device->slot != 3U || device->sample_rate != 44100U ||
        device->bits_per_sample != 16U) return 30;
    if (host_bus_master_count != 1U || host_irq_count != 1U ||
        !host_irq_handler) return 31;
    if (ac97_host_get_sample_rate() != 44100U) return 32;
    return 0;
}

static int check_controls_and_handler(void) {
    uint8_t frame[5] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U};
    uint16_t status;

    if (ac97_set_volume(0U), ac97_host_inw(HOST_IO_BASE + AC97_REG_MASTER_VOL) !=
        0U) return 40;
    ac97_set_volume(31U);
    if (ac97_host_inw(HOST_IO_BASE + AC97_REG_MASTER_VOL) != 0x1F1FU) {
        return 41;
    }
    ac97_set_volume(255U);
    if (ac97_host_inw(HOST_IO_BASE + AC97_REG_MASTER_VOL) != 0x1F1FU) {
        return 42;
    }
    ac97_play(0, sizeof(frame), 22050U, 2U, 16U);
    ac97_play(frame, 0U, 22050U, 2U, 16U);
    if (host_allocation_used) return 43;
    ac97_play(frame, sizeof(frame), 22050U, 2U, 16U);
    if (!host_allocation_used || host_allocation_request != 12U ||
        ((uint32_t*)host_allocation.bytes)[0] != 0x2010U ||
        ((uint32_t*)host_allocation.bytes)[1] != 0x4030U ||
        ((uint32_t*)host_allocation.bytes)[2] != 0x50U) return 44;
    if (!(ac97_host_inw(HOST_IO_BASE + AC97_PO_REG_PCR) & AC97_PO_DMA_EN)) {
        return 45;
    }
    ac97_play(frame, sizeof(frame), 44100U, 2U, 16U);
    if (host_free_count != 1U || !host_allocation_used) return 46;
    ac97_stop();
    if (host_free_count != 2U || host_allocation_used ||
        (ac97_host_inw(HOST_IO_BASE + AC97_PO_REG_PCR) & AC97_PO_DMA_EN)) {
        return 47;
    }
    status = HOST_STATUS_EVENTS;
    ac97_host_outw(HOST_IO_BASE + AC97_PO_REG_STATUS, status);
    ac97_handler(0);
    if (ac97_host_inw(HOST_IO_BASE + AC97_PO_REG_STATUS) != 0U) return 48;
    return 0;
}

static int check_limits_and_failures(void) {
    static uint8_t large_frame[AC97_BUF_SIZE + 64U];
    uint32_t free_count;

    for (uint32_t index = 0U; index < sizeof(large_frame); index++) {
        large_frame[index] = (uint8_t)index;
    }
    ac97_play(large_frame, sizeof(large_frame), 48000U, 2U, 16U);
    if (!host_allocation_used || host_allocation_request != HOST_ALLOC_SIZE) {
        return 50;
    }
    ac97_stop();
    free_count = host_free_count;
    host_allocation_failure = 1U;
    ac97_play(large_frame, 16U, 48000U, 2U, 16U);
    if (host_allocation_used || host_free_count != free_count) return 51;
    host_allocation_failure = 0U;
    host_irq_result = ERR_INVALID;
    ac97_host_reset_devices();
    reset_fixture();
    host_irq_result = ERR_INVALID;
    ac97_init();
    if (ac97_get_device()->initialized || host_irq_count != 0U) return 52;
    return 0;
}

int main(void) {
    int result = 0;
    int failures = 0;

    coverage_active = 1U;
    if (!result) result = check_uninitialized();
    if (!result) result = check_missing_device();
    if (!result) result = check_initialization();
    if (!result) result = check_controls_and_handler();
    if (!result) result = check_limits_and_failures();
    EXPECT(result == 0);
    coverage_active = 0U;
    coverage_emit(result ? ERR_STATE : OK);
    return result || failures ? 1 : 0;
}
