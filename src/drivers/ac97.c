#include "drivers/ac97.h"
#include "drivers/pci.h"
#include "core/memory.h"
#include "drivers/idt.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"

static ac97_device_t ac97_dev;
static ac97_stream_t output_stream;
static uint8_t ac97_playing = 0;

#ifdef ZEPHYROS_HOST_TEST
extern void ac97_host_outb(uint16_t port, uint8_t value);
extern uint8_t ac97_host_inb(uint16_t port);
extern void ac97_host_outw(uint16_t port, uint16_t value);
extern uint16_t ac97_host_inw(uint16_t port);
extern void ac97_host_outl(uint16_t port, uint32_t value);
extern uint32_t ac97_host_inl(uint16_t port);
#endif

static void outb(uint16_t port, uint8_t val) {
#ifdef ZEPHYROS_HOST_TEST
    ac97_host_outb(port, val);
#else
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}

static uint8_t inb(uint16_t port) {
#ifdef ZEPHYROS_HOST_TEST
    return ac97_host_inb(port);
#else
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
#endif
}

static void outw(uint16_t port, uint16_t val) {
#ifdef ZEPHYROS_HOST_TEST
    ac97_host_outw(port, val);
#else
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
#endif
}

static uint16_t inw(uint16_t port) {
#ifdef ZEPHYROS_HOST_TEST
    return ac97_host_inw(port);
#else
    uint16_t result;
    asm volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
#endif
}

static void outl(uint16_t port, uint32_t val) {
#ifdef ZEPHYROS_HOST_TEST
    ac97_host_outl(port, val);
#else
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
#endif
}

static uint32_t inl(uint16_t port) {
#ifdef ZEPHYROS_HOST_TEST
    return ac97_host_inl(port);
#else
    uint32_t result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
#endif
}

static void ac97_delay(void) {
#ifdef ZEPHYROS_HOST_TEST
    return;
#else
    for (int i = 0; i < 1000; i++) {
        asm volatile("nop");
    }
#endif
}

static uint16_t ac97_read(uint8_t reg) {
    return inw(ac97_dev.io_base + reg);
}

static void ac97_write(uint8_t reg, uint16_t value) {
    outw(ac97_dev.io_base + reg, value);
}

static void ac97_reset(void) {
    ac97_write(AC97_REG_RESET, 0);
    ac97_delay();
    ac97_delay();
}

static void ac97_power_down(void) {
    uint16_t power = ac97_read(AC97_REG_POWER);
    power &= ~(1 << 0);
    ac97_write(AC97_REG_POWER, power);
    ac97_delay();
}

static void ac97_set_sample_rate(uint32_t rate) {
    ac97_write(AC97_REG_PCM_FRONT_DAC_RATE, rate);
    ac97_delay();
}

static uint32_t ac97_get_sample_rate(void) {
    return ac97_read(AC97_REG_PCM_FRONT_DAC_RATE);
}

void ac97_init(void) {
    LOG_INFO("AC97", "Inicializando controlador de audio");
    ac97_dev.initialized = 0;

    pci_device_t* pci = pci_get_device(0x04, 0x01);
    if (!pci) {
        LOG_ERROR("AC97", "Dispositivo AC97 nao encontrado via PCI");
        return;
    }

    ac97_dev.io_base = pci->bar0 & 0xFFFE;
    ac97_dev.ctrl_base = pci->bar1 & 0xFFFE;
    ac97_dev.irq = pci->irq;
    ac97_dev.slot = pci->device;
    ac97_dev.codec_type = 0;

    pci_enable_bus_mastering(pci);

    ac97_reset();
    ac97_power_down();

    ac97_delay();
    ac97_delay();

    uint16_t master_vol = 0x0000;
    ac97_write(AC97_REG_MASTER_VOL, master_vol);

    uint16_t pcm_vol = 0x0000;
    ac97_write(AC97_REG_PCM_OUT_VOL, pcm_vol);

    ac97_write(AC97_REG_RECORD_GAIN, 0x0000);

    uint16_t ext_status = ac97_read(AC97_REG_EXT_AUDIO);
    ac97_write(AC97_REG_EXT_AUDIO, ext_status);

    ac97_set_sample_rate(44100);

    ac97_dev.sample_rate = 44100;
    ac97_dev.bits_per_sample = 16;
    ac97_dev.initialized = 1;

    output_stream.status = 0;
    output_stream.buffer = 0;
    output_stream.position = 0;

    if (idt_register_handler(ac97_dev.irq, (isr_handler_t)ac97_handler) != OK) {
        LOG_ERROR("AC97", "Falha ao registrar IRQ de audio");
        ac97_dev.initialized = 0;
        return;
    }

    LOG_INFO("AC97", "Controlador AC97 inicializado com sucesso");
}

void ac97_play(const uint8_t* data, uint32_t size, uint32_t sample_rate, uint8_t channels, uint8_t bits) {
    uint32_t sample_count;
    uint32_t allocation_size;

    (void)channels;
    if (!ac97_dev.initialized) {
        LOG_WARN("AC97", "Reproducao solicitada sem dispositivo inicializado");
        return;
    }
    if (!data || size == 0) {
        LOG_ERROR("AC97", "Buffer de audio invalido");
        return;
    }

    ac97_stop();

    ac97_set_sample_rate(sample_rate);
    ac97_dev.sample_rate = sample_rate;
    ac97_dev.bits_per_sample = bits;

    uint32_t buf_size = size;
    if (buf_size > AC97_BUF_SIZE) buf_size = AC97_BUF_SIZE;
    sample_count = (buf_size + 1U) / 2U;
    if (sample_count > 0xFFFFFFFFU / sizeof(uint32_t)) {
        LOG_ERROR("AC97", "Tamanho de buffer de audio excede o limite");
        ac97_playing = 0;
        return;
    }
    allocation_size = sample_count * sizeof(uint32_t);

    output_stream.buffer = (uint32_t*)kmalloc(allocation_size);
    if (!output_stream.buffer) {
        LOG_ERROR("AC97", "Falha ao alocar buffer de audio");
        ac97_playing = 0;
        return;
    }

    for (uint32_t i = 0; i < sample_count; i++) {
        if (i * 2 + 1 < size) {
            output_stream.buffer[i] = data[i * 2] | (data[i * 2 + 1] << 8);
        } else {
            output_stream.buffer[i] = data[i * 2];
        }
    }

    output_stream.position = 0;
    ac97_playing = 1;

    uint16_t cr = inw(ac97_dev.io_base + AC97_PO_REG_PCR);
    cr |= AC97_PO_DMA_EN;
    outw(ac97_dev.io_base + AC97_PO_REG_PCR, cr);
}

void ac97_stop(void) {
    if (!ac97_dev.initialized) return;

    ac97_playing = 0;

    uint16_t cr = inw(ac97_dev.io_base + AC97_PO_REG_PCR);
    cr &= ~AC97_PO_DMA_EN;
    outw(ac97_dev.io_base + AC97_PO_REG_PCR, cr);

    if (output_stream.buffer) {
        kfree(output_stream.buffer);
        output_stream.buffer = 0;
    }
    output_stream.position = 0;
}

void ac97_set_volume(uint8_t volume) {
    if (!ac97_dev.initialized) return;

    uint16_t vol = volume;
    if (vol > 31) vol = 31;
    vol = vol | (vol << 8);
    ac97_write(AC97_REG_MASTER_VOL, vol);
}

ac97_device_t* ac97_get_device(void) {
    return &ac97_dev;
}

void ac97_handler(registers_t* regs) {
    (void)regs;

    uint16_t status = inw(ac97_dev.io_base + AC97_PO_REG_STATUS);

    if (status & AC97_PO_LVBCI) {
        outw(ac97_dev.io_base + AC97_PO_REG_STATUS, AC97_PO_LVBCI);
    }

    if (status & AC97_PO_FIFOE) {
        outw(ac97_dev.io_base + AC97_PO_REG_STATUS, AC97_PO_FIFOE);
    }
}

#ifdef ZEPHYROS_HOST_TEST
uint32_t ac97_host_get_sample_rate(void) {
    return ac97_get_sample_rate();
}

void ac97_host_exercise_io(uint16_t port, uint8_t byte_value,
                           uint16_t word_value, uint32_t dword_value) {
    outb(port, byte_value);
    (void)inb(port);
    outw(port, word_value);
    (void)inw(port);
    outl(port, dword_value);
    (void)inl(port);
}

void ac97_host_reset_devices(void) {
    ac97_stop();
    for (uint32_t index = 0U; index < sizeof(ac97_dev); index++) {
        ((uint8_t*)&ac97_dev)[index] = 0U;
    }
    for (uint32_t index = 0U; index < sizeof(output_stream); index++) {
        ((uint8_t*)&output_stream)[index] = 0U;
    }
    ac97_playing = 0U;
}
#endif
