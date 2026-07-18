#include "ata.h"
#include "video.h"

static ata_device_t devices[2];

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
    for (int i = 0; i < 100000; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_drq(uint16_t port) {
    uint8_t status;
    for (int i = 0; i < 100000; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static void ata_select_drive(uint16_t port, uint8_t slave) {
    outb(port + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    ata_delay(port);
}

static void ata_soft_reset(uint16_t ctrl) {
    outb(ctrl, 0x04);
    ata_delay(ctrl);
    outb(ctrl, 0x00);
    ata_delay(ctrl);
}

static void ata_detect(uint16_t io, uint16_t ctrl, uint8_t slave, ata_device_t* dev) {
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
    if (status == 0) return;

    if (ata_wait_ready(io) != 0) return;

    if (inb(io + ATA_REG_LBA_MID) != 0 || inb(io + ATA_REG_LBA_HIGH) != 0) {
        return;
    }

    while (1) {
        status = inb(io + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return;
        if (status & ATA_SR_DRQ) break;
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
}

void ata_init(void) {
    for (int i = 0; i < 2; i++) {
        devices[i].present = 0;
    }

    ata_detect(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 0, &devices[0]);
    ata_detect(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 1, &devices[1]);
}

ata_device_t* ata_get_device(void) {
    for (int i = 0; i < 2; i++) {
        if (devices[i].present) return &devices[i];
    }
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    ata_device_t* dev = ata_get_device();
    if (!dev) return -1;

    uint16_t io = dev->base_port;

    if (ata_wait_ready(io) != 0) return -1;

    outb(io + ATA_REG_CTRL, 0x02);
    ata_delay(io);

    ata_select_drive(io, dev->slave);
    ata_delay(io);

    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_DRIVE, 0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F));
    outb(io + ATA_REG_COMMAND, ATA_CMD_READ);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq(io) != 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t data = inw(io + ATA_REG_DATA);
            buffer[s * 512 + i * 2] = data & 0xFF;
            buffer[s * 512 + i * 2 + 1] = (data >> 8) & 0xFF;
        }
    }

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    ata_device_t* dev = ata_get_device();
    if (!dev) return -1;

    uint16_t io = dev->base_port;

    if (ata_wait_ready(io) != 0) return -1;

    outb(io + ATA_REG_CTRL, 0x02);
    ata_delay(io);

    ata_select_drive(io, dev->slave);
    ata_delay(io);

    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_DRIVE, 0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F));
    outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq(io) != 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t data = buffer[s * 512 + i * 2] | (buffer[s * 512 + i * 2 + 1] << 8);
            outw(io + ATA_REG_DATA, data);
        }

        for (int i = 0; i < 4; i++) {
            inb(io + ATA_REG_STATUS);
        }
    }

    return 0;
}

static void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
