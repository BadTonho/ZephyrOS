#include <stdint.h>
#include <stdio.h>

#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/string.h"
#include "core/video.h"
#include "core/wifi_manager.h"

void shell_dispatch_cmd_wifi(const char* arguments);

#define HOST_COVERAGE_CAPACITY 512U
#define HOST_COVERAGE_LINE_SIZE 32U
#define HOST_OUTPUT_CAPACITY 16384U
#define HOST_INTERFACE_CAPACITY 4U

static uintptr_t coverage_addresses[HOST_COVERAGE_CAPACITY];
static uint32_t coverage_count;
static uint8_t coverage_active;
static char video_output[HOST_OUTPUT_CAPACITY];
static uint32_t video_output_length;
static wifi_manager_status_t fake_status;
static wifi_interface_info_t fake_interfaces[HOST_INTERFACE_CAPACITY];
static uint32_t fake_interface_count;
static wifi_scan_result_t fake_scan_results[WIFI_SCAN_RESULT_CAPACITY];
static uint32_t fake_scan_count;
static char fake_connected_ssid[WIFI_SSID_MAX_LENGTH + 1U];
static int fake_init_result;
static int fake_refresh_result;
static int fake_status_result;
static int fake_count_result;
static int fake_interface_result;
static int fake_validate_result;
static int fake_scan_result;
static int fake_connect_result;

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
    printf("ZCOV_BEGIN|case=host:shell:wifi|value=0x%08X\n",
           coverage_count);
    for (uint32_t offset = 0U; offset < coverage_count;
         offset += HOST_COVERAGE_LINE_SIZE) {
        printf("ZCOV_DATA|case=host:shell:wifi|addresses=");
        for (uint32_t index = offset;
             index < coverage_count && index < offset + HOST_COVERAGE_LINE_SIZE;
             index++) {
            if (index != offset) printf(",");
            printf("0x%llX", (unsigned long long)coverage_addresses[index]);
        }
        printf("\n");
    }
    printf("ZCOV_END|case=host:shell:wifi|value=0x%08X\n",
           (uint32_t)result);
}

static void copy_text(char* destination, uint32_t capacity,
                      const char* source) {
    uint32_t index = 0U;

    if (!destination || !capacity) return;
    if (!source) source = "";
    while (source[index] && index + 1U < capacity) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void output_reset(void) {
    video_output_length = 0U;
    video_output[0] = '\0';
}

static int output_contains(const char* text) {
    uint32_t text_length = kstrlen(text);
    uint32_t output_length = kstrlen(video_output);

    if (!text_length || text_length > output_length) return 0;
    for (uint32_t offset = 0U; offset + text_length <= output_length; offset++) {
        uint32_t index = 0U;
        while (index < text_length &&
               video_output[offset + index] == text[index]) index++;
        if (index == text_length) return 1;
    }
    return 0;
}

static void reset_fixture(void) {
    kmemset(&fake_status, 0, sizeof(fake_status));
    kmemset(fake_interfaces, 0, sizeof(fake_interfaces));
    kmemset(fake_scan_results, 0, sizeof(fake_scan_results));
    kmemset(fake_connected_ssid, 0, sizeof(fake_connected_ssid));
    output_reset();
    fake_interface_count = 0U;
    fake_scan_count = 0U;
    fake_init_result = OK;
    fake_refresh_result = OK;
    fake_status_result = ERR_STATE;
    fake_count_result = OK;
    fake_interface_result = OK;
    fake_validate_result = OK;
    fake_scan_result = ERR_UNAVAILABLE;
    fake_connect_result = ERR_UNAVAILABLE;
}

static void configure_interface(wifi_interface_info_t* info,
                                const char* id, wifi_transport_t transport,
                                wifi_interface_state_t state) {
    copy_text(info->id, sizeof(info->id), id);
    copy_text(info->name, sizeof(info->name), "host-wifi");
    info->transport = transport;
    info->vendor_id = 0x1234U;
    info->device_id = 0x5678U;
    info->class_code = WIFI_PCI_CLASS_NETWORK;
    info->subclass_code = 0x80U;
    info->prog_if = 0x01U;
    info->revision = 0x02U;
    info->bus = 1U;
    info->device = 2U;
    info->function = 3U;
    info->irq = 11U;
    info->bars[0] = 0x1000U;
    info->bars[1] = 0x2000U;
    info->usb_port = 2U;
    info->usb_address = 7U;
    info->usb_revision = 0x1234U;
    info->usb_endpoint_count = 3U;
    info->state = state;
    info->driver_error = state == WIFI_INTERFACE_ERROR ? ERR_DISK :
                         state == WIFI_INTERFACE_UNSUPPORTED ? ERR_UNAVAILABLE : OK;
    copy_text(info->usb_device_id, sizeof(info->usb_device_id), "usb-wifi-1");
}

static void configure_interfaces(void) {
    reset_fixture();
    fake_status.initialized = 1U;
    fake_status.partial = 0U;
    fake_status.interface_count = HOST_INTERFACE_CAPACITY;
    fake_status.candidate_count = 3U;
    fake_status.unsupported_count = 1U;
    fake_status.ready_count = 1U;
    fake_status.error_count = 1U;
    fake_status.last_error = ERR_DISK;
    fake_status_result = OK;
    fake_interface_count = HOST_INTERFACE_CAPACITY;
    configure_interface(&fake_interfaces[0], "wifi-pci", WIFI_TRANSPORT_PCI,
                        WIFI_INTERFACE_INVENTORIED);
    configure_interface(&fake_interfaces[1], "wifi-unsupported",
                        WIFI_TRANSPORT_PCI, WIFI_INTERFACE_UNSUPPORTED);
    configure_interface(&fake_interfaces[2], "wifi-ready", WIFI_TRANSPORT_PCI,
                        WIFI_INTERFACE_READY);
    configure_interface(&fake_interfaces[3], "wifi-usb", WIFI_TRANSPORT_USB,
                        WIFI_INTERFACE_ERROR);
    fake_scan_results[0].ssid_length = 5U;
    copy_text(fake_scan_results[0].ssid, sizeof(fake_scan_results[0].ssid),
              "alpha");
    fake_scan_results[0].bssid[0] = 0x10U;
    fake_scan_results[0].bssid[1] = 0x20U;
    fake_scan_results[0].channel = 1U;
    fake_scan_results[0].open_security = 1U;
    fake_scan_results[1].ssid_length = 4U;
    copy_text(fake_scan_results[1].ssid, sizeof(fake_scan_results[1].ssid),
              "beta");
    fake_scan_results[1].bssid[0] = 0x30U;
    fake_scan_results[1].channel = 6U;
    fake_scan_count = 2U;
    fake_scan_result = OK;
}

void log_print(log_level_t level, const char* module, const char* message) {
    (void)level;
    (void)module;
    (void)message;
}

void video_print(const char* text, uint8_t color) {
    (void)color;
    if (!text) return;
    while (*text && video_output_length + 1U < HOST_OUTPUT_CAPACITY) {
        video_output[video_output_length++] = *text++;
    }
    video_output[video_output_length] = '\0';
}

int wifi_manager_init(void) {
    if (fake_init_result != OK && fake_init_result != ERR_OVERFLOW) {
        return fake_init_result;
    }
    fake_status.initialized = 1U;
    fake_status_result = OK;
    return fake_init_result;
}

int wifi_manager_refresh(void) {
    if (fake_refresh_result == OK || fake_refresh_result == ERR_OVERFLOW) {
        fake_status.initialized = 1U;
        fake_status_result = OK;
    }
    return fake_refresh_result;
}

int wifi_manager_get_status(wifi_manager_status_t* out_status) {
    if (!out_status) return ERR_NULL;
    if (fake_status_result != OK) return fake_status_result;
    *out_status = fake_status;
    return OK;
}

int wifi_manager_get_count(uint32_t* out_count) {
    if (!out_count) return ERR_NULL;
    if (fake_count_result != OK) return fake_count_result;
    *out_count = fake_interface_count;
    return OK;
}

int wifi_manager_get_interface(wifi_interface_info_t* out_info,
                               uint32_t index) {
    if (!out_info) return ERR_NULL;
    if (fake_interface_result != OK) return fake_interface_result;
    if (index >= fake_interface_count) return ERR_INVALID;
    *out_info = fake_interfaces[index];
    return OK;
}

int wifi_manager_find(const char* id, wifi_interface_info_t* out_info) {
    (void)id;
    (void)out_info;
    return ERR_UNAVAILABLE;
}

int wifi_manager_validate_state(void) {
    return fake_validate_result;
}

int wifi_manager_scan(wifi_scan_result_t* results, uint32_t capacity,
                      uint32_t* out_count) {
    if (!results || !out_count) return ERR_NULL;
    if (!capacity || capacity > WIFI_SCAN_RESULT_CAPACITY) return ERR_INVALID;
    if (fake_scan_result != OK) return fake_scan_result;
    if (fake_scan_count > capacity) return ERR_OVERFLOW;
    for (uint32_t index = 0U; index < fake_scan_count; index++) {
        results[index] = fake_scan_results[index];
    }
    *out_count = fake_scan_count;
    return OK;
}

int wifi_manager_connect_open(const char* ssid) {
    if (!ssid) return ERR_NULL;
    if (!fake_status.initialized) return ERR_STATE;
    if (fake_connect_result == OK) {
        copy_text(fake_connected_ssid, sizeof(fake_connected_ssid), ssid);
    }
    return fake_connect_result;
}

const char* wifi_manager_state_name(wifi_interface_state_t state) {
    switch (state) {
        case WIFI_INTERFACE_INVENTORIED: return "INVENTORIED";
        case WIFI_INTERFACE_UNSUPPORTED: return "UNSUPPORTED";
        case WIFI_INTERFACE_READY: return "READY";
        case WIFI_INTERFACE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static int check_initialization_and_inventory(void) {
    configure_interfaces();
    fake_interface_count = 0U;
    fake_status.interface_count = 0U;
    fake_status.candidate_count = 0U;
    fake_status.unsupported_count = 0U;
    fake_status.ready_count = 0U;
    fake_status.error_count = 0U;
    fake_status.last_error = OK;
    shell_dispatch_cmd_wifi("");
    if (!output_contains("Estado Wi-Fi EP7.1: NOT_FOUND") ||
        !output_contains("Nenhum candidato PCI ou USB")) return 1;
    return 0;
}

static int check_status_and_scan(void) {
    configure_interfaces();
    shell_dispatch_cmd_wifi("status");
    if (!output_contains("PCI=") || !output_contains("USB=usb-wifi-1") ||
        !output_contains("INVENTORIED") || !output_contains("UNSUPPORTED") ||
        !output_contains("READY") || !output_contains("ERROR")) return 1;
    output_reset();
    shell_dispatch_cmd_wifi("scan");
    if (!output_contains("Redes abertas detectadas:") ||
        !output_contains("SSID=alpha") || !output_contains("SSID=beta")) return 2;
    return 0;
}

static int check_connect_and_invalid(void) {
    configure_interfaces();
    shell_dispatch_cmd_wifi("connect");
    if (!output_contains("Uso: wifi connect <ssid>")) return 1;
    output_reset();
    fake_connect_result = OK;
    shell_dispatch_cmd_wifi("connect lab");
    if (!output_contains("wifi connect: OK") ||
        kstrcmp(fake_connected_ssid, "lab") != 0) return 2;
    output_reset();
    fake_connect_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wifi("connect offline");
    if (!output_contains("ERR_UNAVAILABLE") || !output_contains("Nenhuma senha")) {
        return 3;
    }
    output_reset();
    shell_dispatch_cmd_wifi("unknown");
    return output_contains("Uso: wifi status|scan|connect <ssid>") ? 0 : 4;
}

static int check_failures(void) {
    reset_fixture();
    fake_init_result = ERR_DISK;
    shell_dispatch_cmd_wifi("status");
    if (!output_contains("estado Wi-Fi indisponivel")) return 1;
    configure_interfaces();
    fake_count_result = ERR_DISK;
    shell_dispatch_cmd_wifi("status");
    if (!output_contains("inventario Wi-Fi indisponivel")) return 2;
    configure_interfaces();
    fake_interface_result = ERR_DISK;
    shell_dispatch_cmd_wifi("status");
    if (!output_contains("entrada Wi-Fi indisponivel")) return 3;
    configure_interfaces();
    fake_validate_result = ERR_STATE;
    shell_dispatch_cmd_wifi("status");
    if (!output_contains("estado Wi-Fi invalido")) return 4;
    reset_fixture();
    fake_status.initialized = 1U;
    fake_status_result = OK;
    fake_refresh_result = ERR_DISK;
    shell_dispatch_cmd_wifi("scan");
    if (!output_contains("varredura Wi-Fi indisponivel")) return 5;
    configure_interfaces();
    fake_refresh_result = ERR_OVERFLOW;
    fake_scan_result = ERR_UNAVAILABLE;
    shell_dispatch_cmd_wifi("scan");
    if (!output_contains("inventario Wi-Fi parcial") ||
        !output_contains("Nenhuma varredura 802.11")) return 6;
    configure_interfaces();
    fake_scan_result = ERR_DISK;
    shell_dispatch_cmd_wifi("scan");
    if (!output_contains("scan Wi-Fi indisponivel")) return 7;
    return 0;
}

int main(void) {
    int result = 0;

    coverage_active = 1U;
    if (!result) result = check_initialization_and_inventory();
    if (!result) result = check_status_and_scan();
    if (!result) result = check_connect_and_invalid();
    if (!result) result = check_failures();
    coverage_active = 0U;
    coverage_emit(result);
    if (result) {
        printf("SHELL_WIFI_FAIL:%d\n", result);
        return result;
    }
    puts("SHELL_WIFI_PASS");
    return 0;
}
