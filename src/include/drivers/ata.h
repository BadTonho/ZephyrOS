#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECONDARY_IO   0x170
#define ATA_SECONDARY_CTRL 0x376
#define ATA_MAX_DEVICES    4U
#define ATA_PRIMARY_VECTOR 46U
#define ATA_SECONDARY_VECTOR 47U
#define ATA_LBA28_MAX_SECTORS 0x10000000U

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LOW    3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HIGH   5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7
#define ATA_REG_CTRL       6

#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DRQ   0x08
#define ATA_SR_ERR   0x01

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_WAIT_LIMIT 100000U

typedef struct {
    uint16_t base_port;
    uint16_t ctrl_port;
    uint8_t  slot;
    uint8_t  channel;
    uint8_t  slave;
    uint16_t signature;
    uint16_t capabilities;
    uint32_t sectors;
    uint32_t read_ops;
    uint32_t write_ops;
    int      last_error;
    char     model[41];
    int      present;
} ata_device_t;

int  ata_init(void);
int  ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);
int  ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);
ata_device_t* ata_get_device(void);
int  ata_get_device_count(uint8_t* out_count);
int  ata_get_device_at(uint8_t slot, ata_device_t* out_device);
int  ata_read_device_sectors(uint8_t slot, uint32_t lba, uint8_t count,
                             uint8_t* buffer);
int  ata_write_device_sectors(uint8_t slot, uint32_t lba, uint8_t count,
                              const uint8_t* buffer);
int  ata_get_device_counters(uint8_t slot, uint32_t* out_reads,
                             uint32_t* out_writes);

uint32_t ata_get_read_ops(void);
uint32_t ata_get_write_ops(void);

#endif
