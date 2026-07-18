#include "ac97.h"
#include "pci.h"
#include "memory.h"
#include "idt.h"
#include "video.h"

static ac97_device_t ac97_dev;
static ac97_stream_t output_stream;
static uint8_t ac97_playing = 0;

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static uint16_t inw(uint16_t port) {
    uint16_t result;
    asm volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t inl(uint16_t port) {
    uint32_t result;
    asm volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void ac97_delay(void) {
    for (int i = 0; i < 1000; i++) {
        asm volatile("nop");
    }
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
    ac97_dev.initialized = 0;

    pci_init();

    pci_device_t* pci = pci_get_device(0x04, 0x01);
    if (!pci) {
        video_print("[!!] AC97 nao encontrado via PCI\n", 0x0C);
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

    register_interrupt_handler(ac97_dev.irq, (isr_handler_t)ac97_handler);
}

void ac97_play(const uint8_t* data, uint32_t size, uint32_t sample_rate, uint8_t channels, uint8_t bits) {
    if (!ac97_dev.initialized) return;
    if (!data || size == 0) return;

    ac97_stop();

    ac97_set_sample_rate(sample_rate);
    ac97_dev.sample_rate = sample_rate;
    ac97_dev.bits_per_sample = bits;

    uint32_t buf_size = size;
    if (buf_size > AC97_BUF_SIZE) buf_size = AC97_BUF_SIZE;

    output_stream.buffer = (uint32_t*)kmalloc(buf_size);
    if (!output_stream.buffer) return;

    for (uint32_t i = 0; i < buf_size / 2; i++) {
        if (i * 2 + 1 < size) {
            output_stream.buffer[i] = data[i * 2] | (data[i * 2 + 1] << 8);
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
