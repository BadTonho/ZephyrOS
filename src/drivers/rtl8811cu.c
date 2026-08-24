#include "drivers/rtl8811cu.h"
#include "core/errors.h"
#include "core/usb_transport.h"
#include "fs/fs.h"
#include "core/log.h"
#include "core/string.h"

#define RTL8811CU_FW_SIGNATURE_OFFSET 0U
#define RTL8811CU_FW_VERSION_OFFSET 4U
#define RTL8811CU_FW_HEADER_PROBE_SIZE RTL8811CU_FIRMWARE_HEADER_SIZE

static rtl8811cu_status_t rtl8811cu_status;
static uint8_t rtl8811cu_initialized;

static uint16_t rtl8811cu_read_le16(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int rtl8811cu_device_matches(const usb_device_info_t* device) {
    if (!device) return 0;
    return device->vendor_id == RTL8811CU_VENDOR_ID &&
           device->product_id == RTL8811CU_PRODUCT_ID;
}

static int rtl8811cu_firmware_check(uint32_t* out_size) {
    uint8_t header[RTL8811CU_FW_HEADER_PROBE_SIZE];
    uint32_t size = 0U;
    uint32_t bytes_read = 0U;
    int result;

    if (!out_size) {
        LOG_ERROR("RTL8811CU", "Destino nulo ao consultar firmware");
        return ERR_NULL;
    }
    result = fs_get_root_file_info(RTL8811CU_FIRMWARE_FILE, &size, NULL);
    if (result != OK) return result;
    if (!size || size > RTL8811CU_FIRMWARE_MAX_SIZE) {
        LOG_ERROR("RTL8811CU", "Firmware RTL8811CU invalido");
        return ERR_INVALID;
    }
    if (size < RTL8811CU_FIRMWARE_HEADER_SIZE) {
        LOG_ERROR("RTL8811CU", "Firmware sem cabecalho completo");
        return ERR_INVALID;
    }
    result = fs_read_file_range_at(RTL8811CU_FIRMWARE_FILE, 0U, header,
                                   sizeof(header), &bytes_read);
    if (result != OK || bytes_read != sizeof(header)) {
        LOG_ERROR("RTL8811CU", "Falha ao ler cabecalho do firmware");
        return result != OK ? result : ERR_INVALID;
    }
    if ((!header[RTL8811CU_FW_SIGNATURE_OFFSET] &&
         !header[RTL8811CU_FW_SIGNATURE_OFFSET + 1U]) ||
        !rtl8811cu_read_le16(&header[RTL8811CU_FW_VERSION_OFFSET])) {
        LOG_ERROR("RTL8811CU", "Cabecalho do firmware sem assinatura ou versao");
        return ERR_INVALID;
    }
    /*
     * O rtw88 reserva oito bytes para a verificacao do firmware, mas a
     * rotina exata depende do fluxo de carregamento do transporte. Sem uma
     * fonte confirmada para esse calculo, o radio permanece desligado.
     */
    *out_size = size;
    return ERR_UNAVAILABLE;
}

int rtl8811cu_probe(const usb_device_info_t* device,
                    rtl8811cu_probe_info_t* out_probe) {
    if (!device || !out_probe) {
        LOG_ERROR("RTL8811CU", "Argumento nulo no probe USB");
        return ERR_NULL;
    }
    kmemset(out_probe, 0, sizeof(*out_probe));
    out_probe->configured = device->state == USB_DEVICE_CONFIGURED;
    out_probe->endpoint_count = device->endpoint_count;
    out_probe->bulk_in_count = device->bulk_in_count;
    out_probe->bulk_out_count = device->bulk_out_count;
    out_probe->interrupt_in_count = device->interrupt_in_count;
    if (!rtl8811cu_device_matches(device)) {
        out_probe->result = ERR_NOT_FOUND;
        LOG_WARN("RTL8811CU", "Dispositivo USB fora do ID RTL8811CU suportado");
        return ERR_NOT_FOUND;
    }
    out_probe->matched = 1U;
    out_probe->revision_supported =
        device->device_revision == RTL8811CU_DEVICE_REVISION;
    if (!out_probe->revision_supported) {
        out_probe->result = ERR_UNAVAILABLE;
        LOG_WARN("RTL8811CU", "Revisao bcdDevice RTL8811CU nao suportada");
        return ERR_UNAVAILABLE;
    }
    if (!out_probe->configured || !device->device_descriptor_valid ||
        !device->configuration_descriptor_valid) {
        out_probe->result = ERR_STATE;
        LOG_ERROR("RTL8811CU", "Descritores USB RTL8811CU nao estao prontos");
        return ERR_STATE;
    }
    if (device->controller_model != USB_CONTROLLER_MODEL_EHCI ||
        device->speed != USB_DEVICE_SPEED_HIGH) {
        out_probe->result = ERR_UNAVAILABLE;
        LOG_WARN("RTL8811CU", "RTL8811CU exige transporte USB high-speed EHCI");
        return ERR_UNAVAILABLE;
    }
    if (!device->bulk_in_endpoint || !device->bulk_out_endpoint ||
        !device->bulk_in_max_packet || !device->bulk_out_max_packet ||
        device->bulk_in_max_packet > USB_ENDPOINT_MAX_PACKET_SIZE_HIGH ||
        device->bulk_out_max_packet > USB_ENDPOINT_MAX_PACKET_SIZE_HIGH) {
        out_probe->result = ERR_INVALID;
        LOG_ERROR("RTL8811CU", "Endpoints Bulk RTL8811CU invalidos");
        return ERR_INVALID;
    }
    out_probe->result = OK;
    return OK;
}

static int rtl8811cu_get_driver_status(
    void* driver_context, ethernet_driver_status_t* out_status) {
    (void)driver_context;
    if (!out_status) {
        LOG_ERROR("RTL8811CU", "Destino nulo no status Ethernet");
        return ERR_NULL;
    }
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "Status Ethernet antes da inicializacao");
        return ERR_STATE;
    }
    kmemset(out_status, 0, sizeof(*out_status));
    out_status->initialized = rtl8811cu_status.hardware_initialized;
    out_status->link_up = rtl8811cu_status.link_up;
    out_status->rx_packets = rtl8811cu_status.rx_packets;
    out_status->tx_packets = rtl8811cu_status.tx_packets;
    out_status->rx_errors = rtl8811cu_status.rx_errors;
    out_status->tx_errors = rtl8811cu_status.tx_errors;
    out_status->last_error = rtl8811cu_status.last_error;
    return OK;
}

static int rtl8811cu_rx_pending(void* driver_context, uint8_t* out_pending) {
    (void)driver_context;
    if (!out_pending) {
        LOG_ERROR("RTL8811CU", "Destino nulo no pending RX");
        return ERR_NULL;
    }
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "Pending RX antes da inicializacao");
        return ERR_STATE;
    }
    *out_pending = 0U;
    return rtl8811cu_status.rx_ready ? OK : ERR_UNAVAILABLE;
}

static int rtl8811cu_receive_frame(void* driver_context, uint8_t* data,
                                   uint16_t capacity, uint16_t* out_length,
                                   uint8_t* out_received) {
    (void)driver_context;
    if (!data || !out_length || !out_received) {
        LOG_ERROR("RTL8811CU", "Argumento nulo no RX RTL8811CU");
        return ERR_NULL;
    }
    if (!capacity) {
        LOG_ERROR("RTL8811CU", "Capacidade zero no RX RTL8811CU");
        return ERR_INVALID;
    }
    *out_length = 0U;
    *out_received = 0U;
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "RX antes da inicializacao RTL8811CU");
        return ERR_STATE;
    }
    if (!rtl8811cu_status.rx_ready) {
        rtl8811cu_status.rx_errors++;
        rtl8811cu_status.last_error = ERR_UNAVAILABLE;
        return ERR_UNAVAILABLE;
    }
    /* RX sera ligado ao endpoint Bulk depois da sequencia confirmada. */
    return ERR_UNAVAILABLE;
}

static int rtl8811cu_send_frame(void* driver_context, const uint8_t* data,
                                uint16_t length) {
    (void)driver_context;
    if (!data || !length) {
        LOG_ERROR("RTL8811CU", "Frame invalido no TX RTL8811CU");
        return ERR_INVALID;
    }
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "TX antes da inicializacao RTL8811CU");
        return ERR_STATE;
    }
    if (!rtl8811cu_status.tx_ready) {
        rtl8811cu_status.tx_errors++;
        rtl8811cu_status.last_error = ERR_UNAVAILABLE;
        return ERR_UNAVAILABLE;
    }
    /* TX sera ligado ao endpoint Bulk depois da sequencia confirmada. */
    return ERR_UNAVAILABLE;
}

static void rtl8811cu_prepare_interface(ethernet_interface_t* out_interface) {
    if (!out_interface) return;
    out_interface->driver_context = 0;
    out_interface->get_driver_status = rtl8811cu_get_driver_status;
    out_interface->rx_pending = rtl8811cu_rx_pending;
    out_interface->receive_frame = rtl8811cu_receive_frame;
    out_interface->send_frame = rtl8811cu_send_frame;
}

int rtl8811cu_init(const usb_device_info_t* device,
                   ethernet_interface_t* out_interface) {
    rtl8811cu_probe_info_t probe;
    uint32_t firmware_size = 0U;
    int result;

    LOG_INFO("RTL8811CU", "Inicializando backend USB RTL8811CU");
    if (!out_interface) {
        LOG_ERROR("RTL8811CU", "Interface Ethernet de saida nula");
        return ERR_NULL;
    }
    kmemset(out_interface, 0, sizeof(*out_interface));
    rtl8811cu_prepare_interface(out_interface);
    kmemset(&rtl8811cu_status, 0, sizeof(rtl8811cu_status));
    rtl8811cu_initialized = 1U;
    rtl8811cu_status.state = RTL8811CU_STATE_INVENTORIED;
    result = rtl8811cu_probe(device, &probe);
    if (result != OK) {
        rtl8811cu_status.state = probe.matched ?
            RTL8811CU_STATE_UNSUPPORTED : RTL8811CU_STATE_DISABLED;
        rtl8811cu_status.last_error = result;
        LOG_ERROR("RTL8811CU", "Probe USB RTL8811CU recusou o dispositivo");
        return result;
    }
    result = rtl8811cu_firmware_check(&firmware_size);
    rtl8811cu_status.firmware_available = result == ERR_UNAVAILABLE ||
                                          result == OK;
    rtl8811cu_status.firmware_size = firmware_size;
    if (result != ERR_UNAVAILABLE && result != OK) {
        rtl8811cu_status.state = RTL8811CU_STATE_ERROR;
        rtl8811cu_status.last_error = result;
        LOG_ERROR("RTL8811CU", "Firmware externo RTL8811.BIN indisponivel");
        return result;
    }
    rtl8811cu_status.firmware_header_valid = 1U;
    rtl8811cu_status.last_error = ERR_UNAVAILABLE;
    rtl8811cu_status.state = RTL8811CU_STATE_ERROR;
    LOG_ERROR("RTL8811CU", "Checksum do firmware RTL8811CU sem rotina confirmada");
    LOG_WARN("RTL8811CU", "Radio mantido nao inicializado por seguranca");
    return ERR_UNAVAILABLE;
}

int rtl8811cu_get_status(rtl8811cu_status_t* out_status) {
    if (!out_status) {
        LOG_ERROR("RTL8811CU", "Destino nulo ao consultar estado");
        return ERR_NULL;
    }
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "Estado consultado antes da inicializacao");
        return ERR_STATE;
    }
    *out_status = rtl8811cu_status;
    return OK;
}

int rtl8811cu_validate_state(void) {
    if (!rtl8811cu_initialized ||
        rtl8811cu_status.state > RTL8811CU_STATE_ERROR ||
        (rtl8811cu_status.state == RTL8811CU_STATE_READY &&
         (!rtl8811cu_status.hardware_initialized ||
          !rtl8811cu_status.tx_ready || !rtl8811cu_status.rx_ready))) {
        LOG_ERROR("RTL8811CU", "Estado interno RTL8811CU invalido");
        return ERR_STATE;
    }
    if (rtl8811cu_status.state != RTL8811CU_STATE_READY &&
        rtl8811cu_status.last_error == OK) {
        LOG_ERROR("RTL8811CU", "Estado RTL8811CU indisponivel sem erro");
        return ERR_STATE;
    }
    return OK;
}

int rtl8811cu_scan(rtl8811cu_scan_result_t* results, uint32_t capacity,
                   uint32_t* out_count) {
    if (!results || !out_count) {
        LOG_ERROR("RTL8811CU", "Destino nulo no scan RTL8811CU");
        return ERR_NULL;
    }
    if (!capacity || capacity > RTL8811CU_SCAN_RESULT_CAPACITY) {
        LOG_ERROR("RTL8811CU", "Capacidade invalida no scan RTL8811CU");
        return ERR_INVALID;
    }
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "Scan antes da inicializacao RTL8811CU");
        return ERR_STATE;
    }
    *out_count = 0U;
    if (!rtl8811cu_status.hardware_initialized ||
        !rtl8811cu_status.rx_ready) {
        LOG_WARN("RTL8811CU", "Scan indisponivel: radio nao inicializado");
        return ERR_UNAVAILABLE;
    }
    kmemset(results, 0, sizeof(*results) * capacity);
    return ERR_UNAVAILABLE;
}

int rtl8811cu_connect_open(const char* ssid) {
    uint32_t length = 0U;

    if (!ssid) {
        LOG_ERROR("RTL8811CU", "SSID nulo na associacao aberta");
        return ERR_NULL;
    }
    while (ssid[length] && length <= RTL8811CU_SSID_MAX_LENGTH) length++;
    if (!length || length > RTL8811CU_SSID_MAX_LENGTH) {
        LOG_ERROR("RTL8811CU", "SSID invalido na associacao aberta");
        return ERR_INVALID;
    }
    if (!rtl8811cu_initialized) {
        LOG_ERROR("RTL8811CU", "Associacao antes da inicializacao RTL8811CU");
        return ERR_STATE;
    }
    if (!rtl8811cu_status.hardware_initialized) {
        LOG_WARN("RTL8811CU", "Associacao aberta indisponivel: radio nao inicializado");
        return ERR_UNAVAILABLE;
    }
    return ERR_UNAVAILABLE;
}

const char* rtl8811cu_state_name(rtl8811cu_state_t state) {
    if (state == RTL8811CU_STATE_DISABLED) return "DISABLED";
    if (state == RTL8811CU_STATE_INVENTORIED) return "INVENTORIED";
    if (state == RTL8811CU_STATE_UNSUPPORTED) return "UNSUPPORTED";
    if (state == RTL8811CU_STATE_READY) return "READY";
    if (state == RTL8811CU_STATE_ERROR) return "ERROR";
    return "UNKNOWN";
}
