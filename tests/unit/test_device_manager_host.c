#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/device_manager.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/recovery.h"
#include "core/timer.h"
#include "drivers/ac97.h"
#include "drivers/ata.h"
#include "drivers/pci.h"

#define HOST_COVERAGE_CAPACITY 4096U
#define HOST_COVERAGE_LINE_SIZE 32U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static uint32_t fake_ticks = 100U;
static uint8_t fake_pci_count;
static int fake_pci_count_result;
static int fake_pci_failure_index = -1;
static pci_device_t fake_pci;
static uint8_t fake_ata_present;
static ata_device_t fake_ata;
static ac97_device_t fake_ac97;

static void __attribute__((no_instrument_function))
coverage_record(void* function) {
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

static void __attribute__((no_instrument_function))
coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:device-manager|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:device-manager|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:device-manager|value=0x%08X\n",
           (uint32_t)result);
}

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = fake_pci_count;
    return fake_pci_count_result;
}

int pci_get_device_at(uint8_t index, pci_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if ((int)index == fake_pci_failure_index) return ERR_DISK;
    if (index >= fake_pci_count) return ERR_NOT_FOUND;
    *out_device = fake_pci;
    return OK;
}

ata_device_t* ata_get_device(void) {
    return fake_ata_present ? &fake_ata : NULL;
}

int ata_get_device_at(uint8_t slot, ata_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (!fake_ata_present || slot != 0U) return ERR_NOT_FOUND;
    *out_device = fake_ata;
    return OK;
}

ac97_device_t* ac97_get_device(void) {
    return &fake_ac97;
}

uint32_t timer_get_ticks(void) {
    return fake_ticks++;
}

uint32_t timer_get_frequency(void) {
    return 1000U;
}

int serial_is_ready(void) {
    return 1;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

static void reset_fixtures(void) {
    memset(&fake_pci, 0, sizeof(fake_pci));
    memset(&fake_ata, 0, sizeof(fake_ata));
    memset(&fake_ac97, 0, sizeof(fake_ac97));
    fake_pci_count = 0U;
    fake_pci_count_result = OK;
    fake_pci_failure_index = -1;
    fake_ata_present = 0U;
    fake_ticks = 100U;
}

static int test_before_initialization(void) {
    uint32_t count = 0U;
    device_info_t info;

    if (device_manager_get_count(NULL) != ERR_NULL) return 1;
    if (device_manager_get_count(&count) != ERR_STATE) return 2;
    if (device_manager_get_info(0U, NULL) != ERR_NULL) return 3;
    if (device_manager_get_info(0U, &info) != ERR_STATE) return 4;
    if (device_manager_find(NULL, &info) != ERR_NULL ||
        device_manager_find("vga-text", NULL) != ERR_NULL ||
        device_manager_find("vga-text", &info) != ERR_STATE) return 5;
    if (device_manager_refresh() != ERR_STATE) return 6;
    if (device_manager_status_name(DEVICE_STATUS_READY) == NULL ||
        device_manager_status_name(DEVICE_STATUS_DEGRADED) == NULL ||
        device_manager_status_name(DEVICE_STATUS_DISABLED) == NULL ||
        strcmp(device_manager_status_name(DEVICE_STATUS_UNKNOWN), "UNKNOWN") != 0) {
        return 7;
    }
    return 0;
}

static int test_inventory_without_optional_devices(void) {
    uint32_t count = 0U;
    device_info_t info;
    device_text_t text;

    if (recovery_init() != OK ||
        recovery_mark_ready(RECOVERY_COMPONENT_VESA) != OK) return 10;
    reset_fixtures();
    if (device_manager_init() != OK) return 11;
    if (device_manager_get_count(&count) != OK || count != 8U) return 12;
    if (device_manager_get_info(count, &info) != ERR_INVALID) return 13;
    if (device_manager_get_info(0U, &info) != OK ||
        info.kind != DEVICE_KIND_ATA_PRIMARY ||
        info.status != DEVICE_STATUS_DISABLED) return 14;
    if (device_manager_format_text(&info, &text) != OK ||
        strcmp(text.id, "ata0") != 0 ||
        strcmp(text.detail, "Nenhum disco ATA detectado") != 0) return 15;
    if (device_manager_format_text(NULL, &text) != ERR_NULL ||
        device_manager_format_text(&info, NULL) != ERR_NULL) return 16;
    info.kind = (device_kind_t)99;
    if (device_manager_format_text(&info, &text) != ERR_INVALID) return 17;
    if (device_manager_find("VGA-TEXT", &info) != OK ||
        info.kind != DEVICE_KIND_VGA_TEXT) return 18;
    if (device_manager_find("vesa", &info) != OK ||
        info.status != DEVICE_STATUS_READY) return 19;
    if (device_manager_find("ata-primary", &info) != ERR_NOT_FOUND ||
        device_manager_find("missing", &info) != ERR_NOT_FOUND) return 20;
    if (device_manager_refresh() != OK) return 21;
    if (device_manager_get_count(&count) != OK || count != 8U) return 22;
    return 0;
}

static int test_inventory_with_devices(void) {
    uint32_t count = 0U;
    device_info_t info;
    device_text_t text;

    fake_ata_present = 1U;
    fake_ata.channel = 0U;
    fake_ata.slave = 1U;
    fake_ata.sectors = 12345U;
    fake_ata.present = 1;
    strcpy(fake_ata.model, "Host ATA");
    fake_ac97.initialized = 1U;
    fake_ac97.irq = 5U;
    fake_pci_count = 1U;
    fake_pci.vendor_id = 0x1234U;
    fake_pci.device_id = 0x5678U;
    fake_pci.class = 0x02U;
    fake_pci.subclass = 0U;
    fake_pci.bus = 1U;
    fake_pci.device = 2U;
    fake_pci.function = 0U;
    fake_pci.irq = 11U;
    if (device_manager_refresh() != OK) return 30;
    if (device_manager_get_count(&count) != OK || count != 9U) return 31;
    if (device_manager_find("ATA-PRIMARY", &info) != OK ||
        info.kind != DEVICE_KIND_ATA_PRIMARY ||
        info.status != DEVICE_STATUS_READY) return 32;
    if (device_manager_format_text(&info, &text) != OK ||
        strcmp(text.name, "Host ATA") != 0 ||
        strcmp(text.detail, "Canal primario slave") != 0) return 33;
    if (device_manager_find("PCI-01-02.0", &info) != OK ||
        info.kind != DEVICE_KIND_PCI) return 34;
    if (device_manager_format_text(&info, &text) != OK ||
        strcmp(text.id, "pci-01:02.0") != 0 ||
        strcmp(text.name, "Controlador de rede") != 0 ||
        strcmp(text.type, "PCI") != 0) return 35;
    if (device_manager_get_info(1U, &info) != OK ||
        info.kind != DEVICE_KIND_AC97 || info.status != DEVICE_STATUS_READY) {
        return 36;
    }
    if (device_manager_format_text(&info, &text) != OK ||
        strcmp(text.detail, "Driver de audio pronto") != 0) return 37;
    fake_pci_failure_index = 0;
    if (device_manager_refresh() != ERR_DISK) return 38;
    fake_pci_failure_index = -1;
    return 0;
}

static int test_inventory_overflow(void) {
    uint32_t count = 0U;

    fake_pci_count = (uint8_t)(PCI_MAX_DEVICES + 1U);
    if (device_manager_refresh() != ERR_OVERFLOW) return 40;
    if (device_manager_get_count(&count) != OK ||
        count != DEVICE_MANAGER_MAX_DEVICES) return 41;
    if (device_manager_get_info(count, NULL) != ERR_NULL ||
        device_manager_get_info(count, &(device_info_t){0}) != ERR_INVALID) {
        return 42;
    }
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    log_init();
    if (!result) result = test_before_initialization();
    if (!result) result = test_inventory_without_optional_devices();
    if (!result) result = test_inventory_with_devices();
    if (!result) result = test_inventory_overflow();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
