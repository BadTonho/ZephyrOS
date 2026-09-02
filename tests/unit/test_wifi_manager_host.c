#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/errors.h"
#include "core/log.h"
#include "core/usb_manager.h"
#include "core/wifi_manager.h"
#include "drivers/pci.h"
#include "drivers/rtl8811cu.h"

#define HOST_COVERAGE_CAPACITY 256U
#define HOST_COVERAGE_LINE_SIZE 32U
#define FAKE_PCI_CAPACITY 2U
#define FAKE_USB_CAPACITY 2U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static pci_device_t fake_pci[FAKE_PCI_CAPACITY];
static uint8_t fake_pci_count;
static usb_device_info_t fake_usb[FAKE_USB_CAPACITY];
static uint32_t fake_usb_count;
static int fake_usb_count_result;
static int fake_probe_result;
static rtl8811cu_probe_info_t fake_probe;
static rtl8811cu_status_t fake_driver_status;
static int fake_driver_status_result;
static int fake_driver_validate_result;
static rtl8811cu_scan_result_t fake_scan_results[RTL8811CU_SCAN_RESULT_CAPACITY];
static uint32_t fake_scan_count;
static int fake_scan_result;
static char fake_connected_ssid[WIFI_SSID_MAX_LENGTH + 1U];

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

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void video_print(const char* text, uint8_t color) {
    (void)text;
    (void)color;
}

void video_newline(void) {
}

int serial_is_ready(void) {
    return 1;
}

uint32_t serial_write_text(const char* text, uint32_t length) {
    (void)text;
    return length;
}

int pci_get_device_count(uint8_t* out_count) {
    if (!out_count) return ERR_NULL;
    *out_count = fake_pci_count;
    return OK;
}

int pci_get_device_at(uint8_t index, pci_device_t* out_device) {
    if (!out_device) return ERR_NULL;
    if (index >= fake_pci_count) return ERR_NOT_FOUND;
    *out_device = fake_pci[index];
    return OK;
}

int usb_manager_get_device_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_usb_count_result != OK) return fake_usb_count_result;
    *out_count = fake_usb_count;
    return OK;
}

int usb_manager_get_device(uint32_t index, usb_device_info_t* out_info) {
    if (!out_info) return ERR_NULL;
    if (index >= fake_usb_count) return ERR_NOT_FOUND;
    *out_info = fake_usb[index];
    return OK;
}

int rtl8811cu_probe(const usb_device_info_t* device,
                    rtl8811cu_probe_info_t* out_probe) {
    if (!device || !out_probe) return ERR_NULL;
    *out_probe = fake_probe;
    return fake_probe_result;
}

int rtl8811cu_get_status(rtl8811cu_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fake_driver_status_result != OK) return fake_driver_status_result;
    *out_status = fake_driver_status;
    return OK;
}

int rtl8811cu_validate_state(void) {
    return fake_driver_validate_result;
}

int rtl8811cu_scan(rtl8811cu_scan_result_t* results, uint32_t capacity,
                   uint32_t* out_count) {
    if (!results || !out_count) return ERR_NULL;
    if (!capacity || capacity > RTL8811CU_SCAN_RESULT_CAPACITY) {
        return ERR_INVALID;
    }
    if (fake_scan_result != OK) return fake_scan_result;
    if (fake_scan_count > capacity) return ERR_OVERFLOW;
    memcpy(results, fake_scan_results,
           sizeof(fake_scan_results[0]) * fake_scan_count);
    *out_count = fake_scan_count;
    return OK;
}

int rtl8811cu_connect_open(const char* ssid) {
    if (!ssid) return ERR_NULL;
    strncpy(fake_connected_ssid, ssid, sizeof(fake_connected_ssid) - 1U);
    fake_connected_ssid[sizeof(fake_connected_ssid) - 1U] = '\0';
    return OK;
}

static void __attribute__((no_instrument_function)) coverage_emit(int result) {
    printf("ZCOV_BEGIN|case=host:core:wifi-manager|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:core:wifi-manager|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:core:wifi-manager|value=0x%08X\n",
           (uint32_t)result);
}

static void reset_fixtures(void) {
    memset(fake_pci, 0, sizeof(fake_pci));
    memset(fake_usb, 0, sizeof(fake_usb));
    memset(&fake_probe, 0, sizeof(fake_probe));
    memset(&fake_driver_status, 0, sizeof(fake_driver_status));
    memset(fake_scan_results, 0, sizeof(fake_scan_results));
    memset(fake_connected_ssid, 0, sizeof(fake_connected_ssid));
    fake_pci_count = 2U;
    fake_pci[0].vendor_id = 0x1234U;
    fake_pci[0].device_id = 0x5678U;
    fake_pci[0].class = WIFI_PCI_CLASS_NETWORK;
    fake_pci[0].revision = 1U;
    fake_pci[0].irq = 10U;
    fake_pci[0].bus = 1U;
    fake_pci[0].device = 2U;
    fake_pci[0].function = 3U;
    fake_pci[0].bar0 = 0x1000U;
    fake_pci[0].present = 1U;
    fake_pci[1].vendor_id = 0x8086U;
    fake_pci[1].device_id = 0x100EU;
    fake_pci[1].class = WIFI_PCI_CLASS_NETWORK;
    fake_pci[1].bus = 2U;
    fake_pci[1].device = 4U;
    fake_pci[1].function = 0U;
    fake_pci[1].present = 1U;
    fake_usb_count = 0U;
    fake_usb_count_result = OK;
    fake_probe_result = OK;
    fake_probe.revision_supported = 1U;
    fake_driver_status_result = OK;
    fake_driver_status.state = RTL8811CU_STATE_READY;
    fake_driver_status.initialized = 1U;
    fake_driver_status.hardware_initialized = 1U;
    fake_driver_status.tx_ready = 1U;
    fake_driver_status.rx_ready = 1U;
    fake_driver_status.last_error = OK;
    fake_driver_validate_result = OK;
    fake_scan_count = 0U;
    fake_scan_result = OK;
}

static void configure_usb_fixture(void) {
    fake_usb_count = 2U;
    fake_usb[0].vendor_id = 0x1111U;
    fake_usb[0].product_id = 0x2222U;
    fake_usb[1].vendor_id = RTL8811CU_VENDOR_ID;
    fake_usb[1].product_id = RTL8811CU_PRODUCT_ID;
    fake_usb[1].controller_bus = 3U;
    fake_usb[1].controller_device = 4U;
    fake_usb[1].controller_function = 1U;
    fake_usb[1].port_number = 2U;
    fake_usb[1].usb_address = 7U;
    fake_usb[1].device_revision = RTL8811CU_DEVICE_REVISION;
    memcpy(fake_usb[1].id, "usb-device-1", sizeof("usb-device-1"));
}

static int check_before_initialization(void) {
    wifi_manager_status_t status;
    wifi_interface_info_t info;
    wifi_scan_result_t scan_results[1];
    uint32_t count = 0U;

    if (wifi_manager_get_status(NULL) != ERR_NULL ||
        wifi_manager_get_status(&status) != ERR_STATE ||
        wifi_manager_get_count(NULL) != ERR_NULL ||
        wifi_manager_get_count(&count) != ERR_STATE ||
        wifi_manager_get_interface(NULL, 0U) != ERR_NULL ||
        wifi_manager_get_interface(&info, 0U) != ERR_STATE ||
        wifi_manager_find(NULL, &info) != ERR_NULL ||
        wifi_manager_find("wifi-01:02.3", NULL) != ERR_NULL ||
        wifi_manager_find("wifi-01:02.3", &info) != ERR_STATE ||
        wifi_manager_scan(NULL, 1U, &count) != ERR_NULL ||
        wifi_manager_scan(scan_results, 1U, NULL) != ERR_NULL ||
        wifi_manager_scan(scan_results, 0U, &count) != ERR_INVALID ||
        wifi_manager_scan(scan_results, 1U, &count) != ERR_STATE ||
        wifi_manager_connect_open(NULL) != ERR_NULL ||
        wifi_manager_connect_open("test") != ERR_STATE) return 1;
    if (strcmp(wifi_manager_state_name(WIFI_INTERFACE_INVENTORIED),
               "INVENTORIED") != 0 ||
        strcmp(wifi_manager_state_name(WIFI_INTERFACE_UNSUPPORTED),
               "UNSUPPORTED") != 0 ||
        strcmp(wifi_manager_state_name(WIFI_INTERFACE_READY), "READY") != 0 ||
        strcmp(wifi_manager_state_name(WIFI_INTERFACE_ERROR), "ERROR") != 0 ||
        strcmp(wifi_manager_state_name((wifi_interface_state_t)99U),
               "UNKNOWN") != 0) return 2;
    return 0;
}

static int check_pci_inventory(void) {
    wifi_manager_status_t status;
    wifi_interface_info_t info;
    uint32_t count = 0U;

    if (wifi_manager_init() != OK) return 10;
    if (wifi_manager_get_status(&status) != OK || !status.initialized ||
        status.interface_count != 1U || status.candidate_count != 1U ||
        status.unsupported_count != 1U || status.ready_count != 0U ||
        status.error_count != 0U || status.last_error != ERR_UNAVAILABLE) {
        return 11;
    }
    if (wifi_manager_get_count(&count) != OK || count != 1U ||
        wifi_manager_get_interface(&info, 0U) != OK ||
        strcmp(info.id, "wifi-01:02.3") != 0 ||
        info.transport != WIFI_TRANSPORT_PCI ||
        info.state != WIFI_INTERFACE_UNSUPPORTED ||
        info.driver_error != ERR_UNAVAILABLE) return 12;
    if (wifi_manager_get_interface(&info, 1U) != ERR_INVALID ||
        wifi_manager_find("WIFI-01:02.3", &info) != OK ||
        wifi_manager_find("wifi-missing", &info) != ERR_NOT_FOUND ||
        wifi_manager_validate_state() != OK ||
        wifi_manager_scan(&((wifi_scan_result_t){0}), 1U, &count) !=
            ERR_UNAVAILABLE ||
        wifi_manager_connect_open("offline") != ERR_UNAVAILABLE) return 13;
    return 0;
}

static int check_usb_ready(void) {
    wifi_manager_status_t status;
    wifi_interface_info_t info;
    wifi_scan_result_t scan_results[2];
    uint32_t count = 0U;

    fake_pci_count = 0U;
    configure_usb_fixture();
    fake_scan_count = 2U;
    fake_scan_results[0].ssid_length = 5U;
    memcpy(fake_scan_results[0].ssid, "alpha", 6U);
    fake_scan_results[0].bssid[0] = 0x10U;
    fake_scan_results[0].channel = 1U;
    fake_scan_results[0].open_security = 1U;
    fake_scan_results[1].ssid_length = 4U;
    memcpy(fake_scan_results[1].ssid, "beta", 5U);
    fake_scan_results[1].bssid[0] = 0x20U;
    fake_scan_results[1].channel = 6U;

    if (wifi_manager_refresh() != OK ||
        wifi_manager_get_status(&status) != OK ||
        status.interface_count != 1U || status.candidate_count != 1U ||
        status.ready_count != 1U || status.unsupported_count != 0U ||
        status.error_count != 0U || status.last_error != OK) return 20;
    if (wifi_manager_get_interface(&info, 0U) != OK ||
        strcmp(info.id, "wifi-usb-03:04.1-p2") != 0 ||
        info.transport != WIFI_TRANSPORT_USB || info.state != WIFI_INTERFACE_READY ||
        info.usb_port != 2U || info.usb_address != 7U ||
        wifi_manager_find("WIFI-USB-03:04.1-P2", &info) != OK) return 21;
    if (wifi_manager_scan(scan_results, 0U, &count) != ERR_INVALID ||
        wifi_manager_scan(scan_results, WIFI_SCAN_RESULT_CAPACITY + 1U, &count) !=
            ERR_INVALID ||
        wifi_manager_scan(scan_results, 2U, &count) != OK || count != 2U ||
        strcmp(scan_results[0].ssid, "alpha") != 0 ||
        scan_results[1].channel != 6U) return 22;
    if (wifi_manager_connect_open("zephyr") != OK ||
        strcmp(fake_connected_ssid, "zephyr") != 0 ||
        wifi_manager_validate_state() != OK) return 23;
    return 0;
}

static int check_usb_degraded_and_recovery(void) {
    wifi_manager_status_t status;
    wifi_interface_info_t info;
    wifi_scan_result_t scan_results[1];
    uint32_t count = 0U;

    fake_probe_result = ERR_UNAVAILABLE;
    fake_probe.revision_supported = 0U;
    if (wifi_manager_refresh() != OK ||
        wifi_manager_get_status(&status) != OK || status.unsupported_count != 1U ||
        status.ready_count != 0U || status.error_count != 0U ||
        status.last_error != ERR_UNAVAILABLE ||
        wifi_manager_get_interface(&info, 0U) != OK ||
        info.state != WIFI_INTERFACE_UNSUPPORTED ||
        wifi_manager_validate_state() != OK ||
        wifi_manager_scan(scan_results, 1U, &count) != ERR_UNAVAILABLE ||
        wifi_manager_connect_open("offline") != ERR_UNAVAILABLE) return 30;

    fake_probe.revision_supported = 1U;
    if (wifi_manager_refresh() != OK ||
        wifi_manager_get_status(&status) != OK || status.error_count != 1U ||
        status.last_error != ERR_UNAVAILABLE ||
        wifi_manager_get_interface(&info, 0U) != OK ||
        info.state != WIFI_INTERFACE_ERROR || info.driver_error != ERR_UNAVAILABLE ||
        wifi_manager_validate_state() != OK) return 31;

    fake_probe_result = OK;
    fake_driver_status.state = RTL8811CU_STATE_ERROR;
    fake_driver_status.last_error = ERR_DISK;
    if (wifi_manager_refresh() != OK ||
        wifi_manager_get_status(&status) != OK || status.error_count != 1U ||
        status.last_error != ERR_DISK ||
        wifi_manager_get_interface(&info, 0U) != OK ||
        info.driver_error != ERR_DISK || wifi_manager_validate_state() != OK) {
        return 32;
    }

    fake_driver_status.state = RTL8811CU_STATE_READY;
    fake_driver_status.last_error = OK;
    fake_driver_status_result = ERR_STATE;
    if (wifi_manager_refresh() != OK ||
        wifi_manager_get_status(&status) != OK || status.unsupported_count != 1U ||
        status.last_error != ERR_UNAVAILABLE ||
        wifi_manager_validate_state() != OK) return 33;

    fake_driver_status_result = OK;
    fake_driver_validate_result = ERR_STATE;
    fake_probe_result = OK;
    fake_driver_status.state = RTL8811CU_STATE_READY;
    if (wifi_manager_refresh() != OK ||
        wifi_manager_validate_state() != ERR_STATE) return 34;
    fake_driver_validate_result = OK;

    fake_usb_count_result = ERR_STATE;
    fake_pci_count = 2U;
    if (wifi_manager_refresh() != OK ||
        wifi_manager_get_status(&status) != OK || status.interface_count != 1U ||
        status.last_error != ERR_UNAVAILABLE ||
        wifi_manager_validate_state() != OK) return 35;

    fake_usb_count_result = OK;
    return 0;
}

static int check_invalid_pci_and_restore(void) {
    wifi_manager_status_t status;

    fake_pci_count = 1U;
    fake_pci[0].device = 32U;
    if (wifi_manager_refresh() != ERR_INVALID ||
        wifi_manager_get_status(&status) != OK || status.last_error != ERR_INVALID) {
        return 40;
    }
    reset_fixtures();
    if (wifi_manager_refresh() != OK || wifi_manager_validate_state() != OK) {
        return 41;
    }
    return 0;
}

int main(void) {
    int result;

    reset_fixtures();
    coverage_active = 1U;
    result = check_before_initialization();
    if (!result) result = check_pci_inventory();
    if (!result) result = check_usb_ready();
    if (!result) result = check_usb_degraded_and_recovery();
    if (!result) result = check_invalid_pci_and_restore();
    coverage_active = 0U;
    coverage_emit(result);
    return result;
}
