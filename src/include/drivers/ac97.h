#ifndef AC97_H
#define AC97_H

#include "types.h"

#define AC97_PCI_CLASS    0x04
#define AC97_PCI_SUBCLASS 0x01

#define AC97_REG_RESET        0x00
#define AC97_REG_MASTER_VOL   0x02
#define AC97_REG_MASTER_MONO  0x04
#define AC97_REG_PCBEEP_VOL   0x06
#define AC97_REG_MIC_VOL      0x08
#define AC97_REG_LINE_IN_VOL  0x0A
#define AC97_REG_CD_VOL       0x0C
#define AC97_REG_AUX_VOL      0x0E
#define AC97_REG_PCM_OUT_VOL  0x10
#define AC97_REG_RECORD_VOL   0x1A
#define AC97_REG_RECORD_GAIN  0x1C
#define AC97_REG_EXTENDED     0x28
#define AC97_REG_POWER        0x2C
#define AC97_REG_EXT_AUDIO    0x2A
#define AC97_REG_EXT_AUDIO_2  0x3A
#define AC97_REG_PCM_FRONT_DAC_RATE  0x2C
#define AC97_REG_PCM_SURR_DAC_RATE   0x2E
#define AC97_REG_PCM_LFE_DAC_RATE    0x30
#define AC97_REG_CODEC_AUD_DATA_1    0x3C
#define AC97_REG_CODEC_AUD_DATA_2    0x3E

#define AC97_REG_STATUS      0x26
#define AC97_REG_RSTATUS     0x2A
#define AC97_REG_DMA_OUT_PADDR  0x28
#define AC97_REG_DMA_OUT_LBASE  0x20
#define AC97_REG_DMA_OUT_HBASE  0x22

#define AC97_REG_NMI_STATUS  0x04
#define AC97_REG_NMI_ENABLE  0x02

#define AC97_PO_BUFFER       0
#define AC97_PO_LAST         1
#define AC97_PO_FE           2

#define AC97_PO_SR           (1 << 0)
#define AC97_PO_FIFOE        (1 << 1)
#define AC97_PO_LVBCI        (1 << 2)
#define AC97_PO_LVBCI_EN     (1 << 3)
#define AC97_PO_FIFOE_EN     (1 << 4)
#define AC97_PO_DMA_EN       (1 << 7)

#define AC97_PO_REG_STATUS     0x16
#define AC97_PO_REG_LPIB       0x04
#define AC97_PO_REG_CIV        0x0A
#define AC97_PO_REG_LVI        0x0B
#define AC97_PO_REG_LVF        0x0D
#define AC97_PO_REG_POBDB      0x1C
#define AC97_PO_REG_PPLBAC     0x0E
#define AC97_PO_REG_PPHBAC     0x10
#define AC97_PO_REG_PPCBA      0x12
#define AC97_PO_REG_PPCBAU     0x14

#define AC97_PO_REG_PICB       0x08
#define AC97_PO_REG_PCR        0x16

#define AC97_BUF_COUNT 32
#define AC97_BUF_SIZE  4096

typedef struct {
    uint16_t io_base;
    uint16_t ctrl_base;
    uint8_t  irq;
    uint8_t  slot;
    uint8_t  codec_type;
    uint32_t sample_rate;
    uint32_t bits_per_sample;
    uint8_t  initialized;
    uint32_t* buffer_virt;
    uint32_t  buffer_phys;
} ac97_device_t;

typedef struct {
    uint8_t  status;
    uint8_t  format_lo;
    uint8_t  format_hi;
    uint8_t  sub_status;
    uint32_t position;
    uint32_t* buffer;
} ac97_stream_t;

void ac97_init(void);
void ac97_play(const uint8_t* data, uint32_t size, uint32_t sample_rate, uint8_t channels, uint8_t bits);
void ac97_stop(void);
void ac97_set_volume(uint8_t volume);
ac97_device_t* ac97_get_device(void);
void ac97_handler(registers_t* regs);

#endif
