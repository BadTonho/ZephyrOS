#include "drivers/ata.h"
#include "core/video.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/string.h"
#include "drivers/idt.h"

static ata_device_t devices[ATA_MAX_DEVICES];
static int driver_initialized = 0;
static uint32_t ata_read_ops = 0;
static uint32_t ata_write_ops = 0;

static uint8_t inb(uint16_t port);

static void ata_primary_irq_handler(registers_t* regs) {
    (void)regs;
    inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
}

static void ata_secondary_irq_handler(registers_t* regs) {
    (void)regs;
    inb(ATA_SECONDARY_IO + ATA_REG_STATUS);
}

#define ATA_READ_RETRIES 3
#define ATA_STATUS_DELAY_READS 4
#define ATA_RESET_DELAY_READS 64

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
    for (int i = 0; i < ATA_STATUS_DELAY_READS; i++) {
        inb(port + ATA_REG_STATUS);
    }
}

static int ata_wait_ready(uint16_t port) {
    uint8_t status;
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            LOG_WARN("ATA", "Status invalido ao aguardar disco pronto");
            return ERR_DISK;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRDY)) {
            return OK;
        }
    }
    LOG_WARN("ATA", "Timeout ao aguardar disco pronto");
    return ERR_TIMEOUT;
}

static int ata_wait_drq(uint16_t port) {
    uint8_t status;
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            LOG_WARN("ATA", "Status invalido ao aguardar DRQ");
            return ERR_DISK;
        }
        if (status & ATA_SR_DRQ) return OK;
    }
    LOG_WARN("ATA", "Timeout ao aguardar DRQ");
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
            LOG_WARN("ATA", "Status invalido ao concluir escrita PIO");
            return ERR_DISK;
        }
        /* Depois da ultima palavra PIO, alguns controladores mantem DRQ
           visivel ate o proximo comando. BSY limpo sem erro confirma que
           o dispositivo concluiu a operacao, sem depender desse detalhe. */
        if (!(status & ATA_SR_BSY)) {
            return OK;
        }
    }

    LOG_WARN("ATA", "Timeout ao concluir escrita PIO");
    return ERR_TIMEOUT;
}

static int ata_wait_identify(uint16_t port) {
    uint8_t status;

    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        status = inb(port + ATA_REG_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_SR_ERR)) {
            LOG_WARN("ATA", "Status invalido durante IDENTIFY");
            return ERR_DISK;
        }
        if (status & ATA_SR_BSY) continue;
        if (inb(port + ATA_REG_LBA_MID) != 0 ||
            inb(port + ATA_REG_LBA_HIGH) != 0) {
            LOG_WARN("ATA", "Dispositivo IDENTIFY nao e ATA PIO suportado");
            return ERR_DISK;
        }
        if (status & ATA_SR_DRQ) return OK;
    }

    LOG_WARN("ATA", "Timeout durante IDENTIFY");
    return ERR_TIMEOUT;
}

static void ata_select_drive(uint16_t port, uint8_t slave) {
    outb(port + ATA_REG_DRIVE, 0xA0 | (slave << 4));
    ata_delay(port);
}

static void ata_soft_reset(uint16_t io, uint16_t ctrl) {
    outb(ctrl, 0x04);
    for (int i = 0; i < ATA_STATUS_DELAY_READS; i++) inb(ctrl);
    outb(ctrl, 0x00);
    /* O reset afeta master e slave. De tempo ao canal para concluir a
       desassertacao antes de selecionar um dos dispositivos. */
    for (int i = 0; i < ATA_RESET_DELAY_READS; i++) inb(ctrl);
    ata_delay(io);
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

static int ata_detect(uint8_t slot, uint16_t io, uint16_t ctrl,
                      uint8_t channel, uint8_t slave, ata_device_t* dev) {
    if (!dev) {
        LOG_ERROR("ATA", "Destino nulo ao detectar dispositivo");
        return ERR_NULL;
    }

    kmemset(dev, 0, sizeof(ata_device_t));
    dev->base_port = io;
    dev->ctrl_port = ctrl;
    dev->slot = slot;
    dev->channel = channel;
    dev->slave = slave;
    dev->present = 0;
    dev->last_error = ERR_NOT_FOUND;

    ata_select_drive(io, slave);
    ata_delay(io);

    for (uint32_t wait = 0; wait < ATA_WAIT_LIMIT; wait++) {
        uint8_t selected_status = inb(io + ATA_REG_STATUS);

        if (selected_status == 0 || selected_status == 0xFF) {
            LOG_DEBUG("ATA", "Nenhum dispositivo no canal selecionado");
            return ERR_NOT_FOUND;
        }
        if (!(selected_status & ATA_SR_BSY)) break;
        if (wait + 1U == ATA_WAIT_LIMIT) {
            LOG_WARN("ATA", "Timeout aguardando dispositivo apos reset");
            dev->last_error = ERR_TIMEOUT;
            return ERR_TIMEOUT;
        }
    }

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
        dev->last_error = ERR_DISK;
        return ERR_DISK;
    }

    uint16_t ident[256];
    for (int i = 0; i < 256; i++) {
        ident[i] = inw(io + ATA_REG_DATA);
    }

    dev->signature = ident[0];
    dev->capabilities = ident[49];
    dev->sectors = ident[60] | ((uint32_t)ident[61] << 16);
    if (dev->sectors > ATA_LBA28_MAX_SECTORS) {
        dev->sectors = ATA_LBA28_MAX_SECTORS;
        LOG_WARN("ATA", "Capacidade limitada ao endereco LBA28");
    }

    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = (char)(ident[27 + i] >> 8);
        dev->model[i * 2 + 1] = (char)(ident[27 + i] & 0xFF);
    }
    dev->model[40] = '\0';

    dev->present = 1;
    dev->last_error = OK;
    LOG_INFO("ATA", "Dispositivo ATA identificado");
    return OK;
}

static void outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static ata_device_t* ata_first_present(void) {
    for (uint8_t i = 0; i < ATA_MAX_DEVICES; i++) {
        if (devices[i].present) return &devices[i];
    }
    return 0;
}

int ata_init(void) {
    int secondary_available = 1;

    LOG_INFO("ATA", "Inicializando controlador ATA");
    driver_initialized = 0;
    ata_read_ops = 0;
    ata_write_ops = 0;
    for (uint8_t i = 0; i < ATA_MAX_DEVICES; i++) {
        kmemset(&devices[i], 0, sizeof(ata_device_t));
        devices[i].slot = i;
        devices[i].last_error = ERR_NOT_FOUND;
    }

    if (idt_register_handler(ATA_PRIMARY_VECTOR,
                             (isr_handler_t)ata_primary_irq_handler) != OK) {
        LOG_ERROR("ATA", "Falha ao registrar IRQ do canal primario");
        return ERR_STATE;
    }
    if (idt_register_handler(ATA_SECONDARY_VECTOR,
                             (isr_handler_t)ata_secondary_irq_handler) != OK) {
        LOG_ERROR("ATA", "Falha ao registrar IRQ do canal secundario");
        secondary_available = 0;
    }

    /* Reiniciar uma vez por canal preserva o slave durante a enumeracao. */
    ata_soft_reset(ATA_PRIMARY_IO, ATA_PRIMARY_CTRL);
    ata_detect(0, ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 0, 0, &devices[0]);
    ata_detect(1, ATA_PRIMARY_IO, ATA_PRIMARY_CTRL, 0, 1, &devices[1]);
    if (secondary_available) {
        ata_soft_reset(ATA_SECONDARY_IO, ATA_SECONDARY_CTRL);
        ata_detect(2, ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 1, 0, &devices[2]);
        ata_detect(3, ATA_SECONDARY_IO, ATA_SECONDARY_CTRL, 1, 1, &devices[3]);
    }

    if (!ata_first_present()) {
        LOG_ERROR("ATA", "Nenhum disco ATA disponivel");
        return ERR_NOT_FOUND;
    }
    driver_initialized = 1;
    LOG_INFO("ATA", "Controlador ATA inicializado com sucesso");
    return OK;
}

ata_device_t* ata_get_device(void) {
    if (!driver_initialized) {
        LOG_DEBUG("ATA", "Dispositivo legado consultado antes da inicializacao");
        return 0;
    }
    return ata_first_present();
}

int ata_get_device_count(uint8_t* out_count) {
    uint8_t count = 0;

    if (!out_count) {
        LOG_ERROR("ATA", "Destino nulo na contagem de dispositivos");
        return ERR_NULL;
    }
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Contagem solicitada antes da inicializacao");
        return ERR_STATE;
    }
    for (uint8_t i = 0; i < ATA_MAX_DEVICES; i++) {
        if (devices[i].present) count++;
    }
    *out_count = count;
    return OK;
}

int ata_get_device_at(uint8_t slot, ata_device_t* out_device) {
    if (!out_device) {
        LOG_ERROR("ATA", "Destino nulo na consulta de dispositivo");
        return ERR_NULL;
    }
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Consulta solicitada antes da inicializacao");
        return ERR_STATE;
    }
    if (slot >= ATA_MAX_DEVICES) {
        LOG_ERROR("ATA", "Slot ATA invalido");
        return ERR_INVALID;
    }
    if (!devices[slot].present) {
        LOG_DEBUG("ATA", "Slot consultado nao possui dispositivo");
        return ERR_NOT_FOUND;
    }
    kmemcpy(out_device, &devices[slot], sizeof(ata_device_t));
    return OK;
}

uint32_t ata_get_read_ops(void) {
    return ata_read_ops;
}

uint32_t ata_get_write_ops(void) {
    return ata_write_ops;
}

static int ata_read_from_device(ata_device_t* dev, uint32_t lba,
                                uint8_t count, uint8_t* buffer) {
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Driver nao inicializado");
        return ERR_NOT_FOUND;
    }
    if (!dev) {
        LOG_ERROR("ATA", "Leitura sem dispositivo ATA");
        return ERR_NOT_FOUND;
    }
    if (!buffer || count == 0) {
        LOG_ERROR("ATA", "Buffer ou quantidade de setores invalida");
        return ERR_NULL;
    }
    if (lba >= ATA_LBA28_MAX_SECTORS ||
        count > ATA_LBA28_MAX_SECTORS - lba ||
        (dev->sectors != 0 &&
         (lba >= dev->sectors || count > dev->sectors - lba))) {
        LOG_ERROR("ATA", "Leitura fora dos limites do disco");
        dev->last_error = ERR_DISK;
        return ERR_DISK;
    }

    if (dev == ata_get_device()) ata_read_ops++;
    dev->read_ops++;
    uint16_t io = dev->base_port;

    for (int attempt = 0; attempt < ATA_READ_RETRIES; attempt++) {
        if (ata_prepare_transfer(dev, lba) != OK) {
            LOG_WARN("ATA", "Disco nao ficou pronto para leitura");
            ata_soft_reset(io, dev->ctrl_port);
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

        if (!failed) {
            dev->last_error = OK;
            return OK;
        }
        LOG_WARN("ATA", "Falha durante leitura; tentando novamente");
        ata_soft_reset(io, dev->ctrl_port);
    }

    LOG_ERROR("ATA", "Leitura ATA falhou apos tentativas");
    dev->last_error = ERR_DISK;
    return ERR_DISK;
}

int ata_read_device_sectors(uint8_t slot, uint32_t lba, uint8_t count,
                            uint8_t* buffer) {
    if (slot >= ATA_MAX_DEVICES) {
        LOG_ERROR("ATA", "Slot invalido na leitura direcionada");
        return ERR_INVALID;
    }
    if (!devices[slot].present) {
        LOG_ERROR("ATA", "Dispositivo direcionado nao encontrado");
        return ERR_NOT_FOUND;
    }
    return ata_read_from_device(&devices[slot], lba, count, buffer);
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    return ata_read_from_device(ata_get_device(), lba, count, buffer);
}

static int ata_write_to_device(ata_device_t* dev, uint32_t lba,
                               uint8_t count, const uint8_t* buffer) {
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Driver nao inicializado");
        return ERR_NOT_FOUND;
    }
    if (!dev) {
        LOG_ERROR("ATA", "Escrita sem dispositivo ATA");
        return ERR_NOT_FOUND;
    }
    if (!buffer || count == 0) {
        LOG_ERROR("ATA", "Buffer ou quantidade de setores invalida");
        return ERR_NULL;
    }
    if (lba >= ATA_LBA28_MAX_SECTORS ||
        count > ATA_LBA28_MAX_SECTORS - lba ||
        (dev->sectors != 0 &&
         (lba >= dev->sectors || count > dev->sectors - lba))) {
        LOG_ERROR("ATA", "Escrita fora dos limites do disco");
        dev->last_error = ERR_DISK;
        return ERR_DISK;
    }

    ata_write_ops++;
    dev->write_ops++;
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

        for (int i = 0; i < ATA_STATUS_DELAY_READS; i++) {
            inb(io + ATA_REG_STATUS);
        }
    }

    if (ata_wait_write_complete(io) != OK) {
        LOG_ERROR("ATA", "Escrita ATA nao confirmou conclusao");
        dev->last_error = ERR_DISK;
        return ERR_DISK;
    }

    dev->last_error = OK;
    return OK;
}

int ata_write_device_sectors(uint8_t slot, uint32_t lba, uint8_t count,
                             const uint8_t* buffer) {
    if (slot >= ATA_MAX_DEVICES) {
        LOG_ERROR("ATA", "Slot invalido na escrita direcionada");
        return ERR_INVALID;
    }
    if (!devices[slot].present) {
        LOG_ERROR("ATA", "Dispositivo direcionado nao encontrado para escrita");
        return ERR_NOT_FOUND;
    }
    return ata_write_to_device(&devices[slot], lba, count, buffer);
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    return ata_write_to_device(ata_get_device(), lba, count, buffer);
}

int ata_get_device_counters(uint8_t slot, uint32_t* out_reads,
                            uint32_t* out_writes) {
    if (!out_reads || !out_writes) {
        LOG_ERROR("ATA", "Destino nulo na consulta de contadores");
        return ERR_NULL;
    }
    if (!driver_initialized) {
        LOG_ERROR("ATA", "Contadores solicitados antes da inicializacao");
        return ERR_STATE;
    }
    if (slot >= ATA_MAX_DEVICES) {
        LOG_ERROR("ATA", "Slot invalido na consulta de contadores");
        return ERR_INVALID;
    }
    if (!devices[slot].present) {
        LOG_ERROR("ATA", "Contadores solicitados para slot ausente");
        return ERR_NOT_FOUND;
    }
    *out_reads = devices[slot].read_ops;
    *out_writes = devices[slot].write_ops;
    return OK;
}
