#include "drivers/ata.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "drivers/idt.h"

static ata_device_t devices[2];
static int driver_initialized = 0;
static uint32_t ata_read_ops = 0;
static uint32_t ata_write_ops = 0;

static uint8_t inb(uint16_t port);

static void ata_irq_handler(registers_t* regs) {
    (void)regs;
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
}

#define ATA_READ_RETRIES 3

static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static uint16_t inw(uint16_t port) {
    uint16_t result;
    asm volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void ata_delay(uint16_t port) {
    for (int i = 0; i < 4; i++) {
        inb(port + ATA_REG_STATUS);
    }
}

static int ata_wait_ready(uint16_t port) {
    uint8_t status;
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            return ERR_DISK;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return OK;
        }
    }
    return ERR_TIMEOUT;
}

static int ata_wait_drq(uint16_t port) {
    uint8_t status;
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            return ERR_DISK;
        }
        if (status & ATA_SR_DRQ) return OK;
    }
    return ERR_TIMEOUT;
}

static int ata_wait_data_ready(uint16_t port) {
    return ata_wait_drq(port);
}

static int ata_wait_write_complete(uint16_t port) {
    uint8_t status;

    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            return ERR_DISK;
        }
        /* Depois da ultima palavra PIO, alguns controladores mantem DRQ
           visivel ate o proximo comando. BSY limpo sem erro confirma que
           o dispositivo concluiu a operacao, sem depender desse detalhe. */
        if (!(status & ATA_SR_BSY)) {
            return OK;
        }
    }

    return ERR_TIMEOUT;
}

static int ata_wait_identify(uint16_t port) {
    uint8_t status;

    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            return ERR_DISK;
        }
        if (status & ATA_SR_BSY) continue;
        if (inb(port + ATA_REG_LBA_MID) != 0 ||
            inb(port + ATA_REG_LBA_HIGH) != 0) {
            return ERR_DISK;
        }
        if (status & ATA_SR_DRQ) return OK;
    }

    return ERR_TIMEOUT;
}

static void ata_select_drive(uint16_t port, uint8_t slave) {
    outb(port + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    ata_delay(port);
}

static void ata_soft_reset(uint16_t ctrl) {
    outb(ctrl, 0x04);
    for (int i = 0; i < 4; i++) inb(ctrl);
    outb(ctrl, 0x00);
    for (int i = 0; i < 4; i++) inb(ctrl);
}

static int ata_prepare_transfer(const ata_device_t* dev, uint32_t lba) {
    uint16_t io;

    if (!dev) {
        LOG_ERROR("ATA", "Dispositivo nulo ao preparar transferencia");
        return ERR_NULL;
    }

    io = dev->base_port;

    /* A sonda de um slave ausente pode deixar seu status de erro no canal.
       Selecione o disco real antes de consultar DRDY. */
    outb(dev->ctrl_port, 0x02);
    ata_delay(io);
    outb(io + ATA_REG_DRIVE,
         0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F));
    ata_delay(io);

    return ata_wait_ready(io);
}

static int ata_detect(uint16_t io, uint16_t ctrl, uint8_t slave, ata_device_t* dev) {
    dev->base_port = io;
    dev->ctrl_port = ctrl;
    dev->slave = slave;
    dev->present = 0;

    ata_soft_reset(ctrl);
    ata_select_drive(io, slave);
    ata_delay(io);

    outb(io + ATA_REG_SECCOUNT, 0);
    outb(io + ATA_REG_LBA_LOW, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HIGH, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(io);

    uint8_t status = inb(io + ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        LOG_DEBUG("ATA", "Nenhum dispositivo no canal selecionado");
        return ERR_NOT_FOUND;
    }

    if (ata_wait_identify(io) != 0) {
        LOG_WARN("ATA", "IDENTIFY expirou ou foi rejeitado");
        return ERR_DISK;
    }

    uint16_t ident[256];
    for (int i = 0; i < 256; i++) {
        ident[i] = inw(io + ATA_REG_DATA);
    }

    dev->signature = ident[0];
    dev->capabilities = ident[49];
    dev->sectors = ident[60] | ((uint32_t)ident[61] << 16);

    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = (char)(ident[27 + i] >> 8);
        dev->model[i * 2 + 1] = (char)(ident[27 + i] & 0xFF);
    }
    dev->model[40] = '\0';

    dev->present = 1;
    LOG_INFO("ATA", "Dispositivo ATA identificado");
    return 0;
}

static void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

void ata_init(void) {
    LOG_INFO("ATA", "Inicializando controlador ATA");

    if (idt_register_handler(46, (isr_handler_t)ata_irq_handler) != OK) {
        LOG_ERROR("ATA", "Falha ao registrar IRQ do controlador");
        return;
    }

    for (int i = 0; i < 2; i++) {
        devices[i].present = 0;
    }

    ata_detect(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 0, &devices[0]);
    ata_detect(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 1, &devices[1]);

    if (!ata_get_device()) {
        LOG_WARN("ATA", "Nenhum disco ATA disponivel");
    } else {
        driver_initialized = 1;
        LOG_INFO("ATA", "Controlador ATA inicializado");
    }
}

ata_device_t* ata_get_device(void) {
    for (int i = 0; i < 2; i++) {
        if (devices[i].present) return &devices[i];
    }
    return 0;
}

uint32_t ata_get_read_ops(void) {
    return ata_read_ops;
}

uint32_t ata_get_write_ops(void) {
    return ata_write_ops;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Driver nao inicializado");
        return ERR_NOT_FOUND;
    }

    ata_device_t* dev = ata_get_device();
    if (!dev) {
        LOG_ERROR("ATA", "Leitura sem dispositivo ATA");
        return ERR_NOT_FOUND;
    }
    if (!buffer || count == 0) {
        LOG_ERROR("ATA", "Buffer ou quantidade de setores invalida");
        return ERR_NULL;
    }
    if (dev->sectors != 0 &&
        (lba >= dev->sectors || count > dev->sectors - lba)) {
        LOG_ERROR("ATA", "Leitura fora dos limites do disco");
        return ERR_DISK;
    }

    ata_read_ops++;
    uint16_t io = dev->base_port;

    for (int attempt = 0; attempt < ATA_READ_RETRIES; attempt++) {
        if (ata_prepare_transfer(dev, lba) != OK) {
            LOG_WARN("ATA", "Disco nao ficou pronto para leitura");
            ata_soft_reset(dev->ctrl_port);
            continue;
        }

        outb(io + ATA_REG_SECCOUNT, count);
        outb(io + ATA_REG_LBA_LOW, lba & 0xFF);
        outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(io + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(io + ATA_REG_COMMAND, ATA_CMD_READ);

        int failed = 0;
        for (uint8_t s = 0; s < count; s++) {
            if (ata_wait_data_ready(io) != OK) {
                failed = 1;
                break;
            }

            for (int i = 0; i < 256; i++) {
                uint16_t data = inw(io + ATA_REG_DATA);
                buffer[s * 512 + i * 2] = data & 0xFF;
                buffer[s * 512 + i * 2 + 1] = (data >> 8) & 0xFF;
            }
        }

        if (!failed) return 0;
        LOG_WARN("ATA", "Falha durante leitura; tentando novamente");
        ata_soft_reset(dev->ctrl_port);
    }

    LOG_ERROR("ATA", "Leitura ATA falhou apos tentativas");
    return ERR_DISK;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Driver nao inicializado");
        return ERR_NOT_FOUND;
    }

    ata_device_t* dev = ata_get_device();
    if (!dev) {
        LOG_ERROR("ATA", "Escrita sem dispositivo ATA");
        return ERR_NOT_FOUND;
    }
    if (!buffer || count == 0) {
        LOG_ERROR("ATA", "Buffer ou quantidade de setores invalida");
        return ERR_NULL;
    }
    if (dev->sectors != 0 &&
        (lba >= dev->sectors || count > dev->sectors - lba)) {
        LOG_ERROR("ATA", "Escrita fora dos limites do disco");
        return ERR_DISK;
    }

    ata_write_ops++;
    uint16_t io = dev->base_port;

    if (ata_prepare_transfer(dev, lba) != OK) {
        LOG_ERROR("ATA", "Disco nao ficou pronto para escrita");
        return ERR_DISK;
    }

    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_data_ready(io) != OK) {
            LOG_ERROR("ATA", "Disco nao ficou pronto para receber dados");
            return ERR_DISK;
        }

        for (int i = 0; i < 256; i++) {
            uint16_t data = buffer[s * 512 + i * 2] | (buffer[s * 512 + i * 2 + 1] << 8);
            outw(io + ATA_REG_DATA, data);
        }

        for (int i = 0; i < 4; i++) {
            inb(io + ATA_REG_STATUS);
        }
    }

    if (ata_wait_write_complete(io) != OK) {
        LOG_ERROR("ATA", "Escrita ATA nao confirmou conclusao");
        return ERR_DISK;
    }

    return OK;
}
