#include "drivers/rtl8811cu.h"
#include "core/errors.h"
#include "fs/fs.h"
#include "core/log.h"
#include "core/string.h"

static rtl8811cu_status_t rtl8811cu_status;
static uint8_t rtl8811cu_initialized;

static int rtl8811cu_device_matches(const usb_device_info_t* device) {
    if (!device) return 0;
    return device->vendor_id == RTL8811CU_VENDOR_ID &&
           device->product_id == RTL8811CU_PRODUCT_ID;
}

static int rtl8811cu_firmware_check(uint32_t* out_size) {
    uint32_t size = 0U;
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
    *out_size = size;
    return OK;
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
    out_probe->result = OK;
    return OK;
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
    if (result != OK) {
        rtl8811cu_status.state = RTL8811CU_STATE_ERROR;
        rtl8811cu_status.last_error = result;
        LOG_ERROR("RTL8811CU", "Firmware externo RTL8811.BIN indisponivel");
        return result;
    }
    rtl8811cu_status.firmware_available = 1U;
    rtl8811cu_status.last_error = ERR_UNAVAILABLE;
    rtl8811cu_status.state = RTL8811CU_STATE_ERROR;
    LOG_ERROR("RTL8811CU", "Sequencia de radio RTL8811CU ainda nao implementada");
    (void)firmware_size;
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

const char* rtl8811cu_state_name(rtl8811cu_state_t state) {
    if (state == RTL8811CU_STATE_DISABLED) return "DISABLED";
    if (state == RTL8811CU_STATE_INVENTORIED) return "INVENTORIED";
    if (state == RTL8811CU_STATE_UNSUPPORTED) return "UNSUPPORTED";
    if (state == RTL8811CU_STATE_READY) return "READY";
    if (state == RTL8811CU_STATE_ERROR) return "ERROR";
    return "UNKNOWN";
}
