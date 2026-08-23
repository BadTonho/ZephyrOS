#include "apps/shell_command_utils.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "core/wifi_manager.h"

#define WIFI_SHELL_COLOR_TEXT 0x07U
#define WIFI_SHELL_COLOR_INFO 0x08U
#define WIFI_SHELL_COLOR_READY 0x0AU
#define WIFI_SHELL_COLOR_LABEL 0x0BU
#define WIFI_SHELL_COLOR_ERROR 0x0CU
#define WIFI_SHELL_COLOR_WARN 0x0EU

static int wifi_ensure_initialized(void) {
    wifi_manager_status_t status;
    int result = wifi_manager_get_status(&status);

    if (result != ERR_STATE) return result;
    result = wifi_manager_init();
    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("WIFI", "Falha ao inicializar inventario sob demanda");
        return result;
    }
    return OK;
}

static uint8_t wifi_state_color(wifi_interface_state_t state) {
    if (state == WIFI_INTERFACE_READY) return WIFI_SHELL_COLOR_READY;
    if (state == WIFI_INTERFACE_UNSUPPORTED) return WIFI_SHELL_COLOR_WARN;
    if (state == WIFI_INTERFACE_ERROR) return WIFI_SHELL_COLOR_ERROR;
    return WIFI_SHELL_COLOR_INFO;
}

static void wifi_print_pci_location(const wifi_interface_info_t* info) {
    video_print(" PCI=", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->bus, 2U);
    video_print(":", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->device, 2U);
    video_print(".", WIFI_SHELL_COLOR_INFO);
    shell_command_print_num(info->function);
}

static void wifi_print_usb_location(const wifi_interface_info_t* info) {
    video_print(" USB=", WIFI_SHELL_COLOR_INFO);
    video_print(info->usb_device_id, WIFI_SHELL_COLOR_LABEL);
    video_print(" porta=", WIFI_SHELL_COLOR_INFO);
    shell_command_print_num(info->usb_port);
    video_print(" endereco=", WIFI_SHELL_COLOR_INFO);
    shell_command_print_num(info->usb_address);
    video_print(" revisao=0x", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->usb_revision, 4U);
    video_print(" endpoints=", WIFI_SHELL_COLOR_INFO);
    shell_command_print_num(info->usb_endpoint_count);
}

static void wifi_print_interface(const wifi_interface_info_t* info) {
    if (!info) {
        LOG_ERROR("SHELL", "Entrada nula no diagnostico Wi-Fi");
        return;
    }
    video_print("  ", WIFI_SHELL_COLOR_TEXT);
    video_print(info->id, WIFI_SHELL_COLOR_LABEL);
    video_print("  ", WIFI_SHELL_COLOR_TEXT);
    video_print(wifi_manager_state_name(info->state),
                wifi_state_color(info->state));
    video_print("  vendor=0x", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->vendor_id, 4U);
    video_print(" device=0x", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->device_id, 4U);
    video_print(" classe=0x", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->class_code, 2U);
    video_print(" subclasse=0x", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->subclass_code, 2U);
    video_print(" prog-if=0x", WIFI_SHELL_COLOR_INFO);
    shell_command_print_hex(info->prog_if, 2U);
    if (info->transport == WIFI_TRANSPORT_USB) {
        wifi_print_usb_location(info);
    } else {
        video_print(" revisao=0x", WIFI_SHELL_COLOR_INFO);
        shell_command_print_hex(info->revision, 2U);
        wifi_print_pci_location(info);
        video_print(" IRQ=", WIFI_SHELL_COLOR_INFO);
        if (info->irq == WIFI_PCI_IRQ_UNKNOWN) {
            video_print("N/D", WIFI_SHELL_COLOR_INFO);
        } else {
            shell_command_print_num(info->irq);
        }
    }
    video_print(" erro=", WIFI_SHELL_COLOR_INFO);
    shell_command_print_num((uint32_t)info->driver_error);
    for (uint32_t bar = 0U; bar < WIFI_PCI_BAR_COUNT; bar++) {
        video_print(" BAR", WIFI_SHELL_COLOR_INFO);
        shell_command_print_num(bar);
        video_print("=0x", WIFI_SHELL_COLOR_INFO);
        shell_command_print_hex(info->bars[bar], 8U);
    }
    video_print("\n", WIFI_SHELL_COLOR_TEXT);
}

static int wifi_print_inventory(void) {
    uint32_t count = 0U;

    if (wifi_ensure_initialized() != OK) {
        LOG_ERROR("SHELL", "Inventario Wi-Fi indisponivel");
        video_print("Erro: inventario Wi-Fi indisponivel.\n", WIFI_SHELL_COLOR_ERROR);
        return ERR_STATE;
    }
    if (wifi_manager_get_count(&count) != OK) {
        LOG_ERROR("SHELL", "Inventario Wi-Fi indisponivel");
        video_print("Erro: inventario Wi-Fi indisponivel.\n", WIFI_SHELL_COLOR_ERROR);
        return ERR_STATE;
    }
    if (!count) {
        video_print("Nenhum candidato PCI ou USB de rede desconhecido detectado.\n",
                    WIFI_SHELL_COLOR_INFO);
        return OK;
    }
    video_print("Candidatos PCI/USB de rede (Wi-Fi nao confirmado):\n",
                WIFI_SHELL_COLOR_LABEL);
    for (uint32_t index = 0U; index < count; index++) {
        wifi_interface_info_t info;

        if (wifi_manager_get_interface(&info, index) != OK) {
            LOG_ERROR("SHELL", "Falha ao ler candidato Wi-Fi");
            video_print("Erro: entrada Wi-Fi indisponivel.\n", WIFI_SHELL_COLOR_ERROR);
            return ERR_STATE;
        }
        wifi_print_interface(&info);
    }
    return OK;
}

static void wifi_print_status(void) {
    wifi_manager_status_t status;
    int result = wifi_ensure_initialized();
    const char* state = "NOT_FOUND";
    uint8_t state_color = WIFI_SHELL_COLOR_INFO;

    if (result != OK) {
        LOG_ERROR("SHELL", "Estado Wi-Fi indisponivel");
        video_print("Erro: estado Wi-Fi indisponivel.\n", WIFI_SHELL_COLOR_ERROR);
        return;
    }
    result = wifi_manager_get_status(&status);
    if (result != OK) {
        LOG_ERROR("SHELL", "Estado Wi-Fi indisponivel");
        video_print("Erro: estado Wi-Fi indisponivel.\n", WIFI_SHELL_COLOR_ERROR);
        return;
    }
    if (wifi_manager_validate_state() != OK) {
        LOG_ERROR("SHELL", "Estado Wi-Fi invalido");
        video_print("Erro: estado Wi-Fi invalido.\n", WIFI_SHELL_COLOR_ERROR);
        return;
    }
    if (status.error_count) {
        state = "ERROR";
        state_color = WIFI_SHELL_COLOR_ERROR;
    } else if (status.ready_count) {
        state = "READY";
        state_color = WIFI_SHELL_COLOR_READY;
    } else if (status.unsupported_count) {
        state = "UNSUPPORTED";
        state_color = WIFI_SHELL_COLOR_WARN;
    }
    video_print("Estado Wi-Fi EP7.1: ", WIFI_SHELL_COLOR_LABEL);
    video_print(state, state_color);
    video_print("\n", WIFI_SHELL_COLOR_TEXT);
    video_print("  Interfaces inventariadas: ", WIFI_SHELL_COLOR_TEXT);
    shell_command_print_num(status.interface_count);
    video_print("\n  Candidatos: ", WIFI_SHELL_COLOR_TEXT);
    shell_command_print_num(status.candidate_count);
    video_print("\n  Unsupported: ", WIFI_SHELL_COLOR_TEXT);
    shell_command_print_num(status.unsupported_count);
    video_print("\n  Ready: ", WIFI_SHELL_COLOR_TEXT);
    shell_command_print_num(status.ready_count);
    video_print("\n  Erros: ", WIFI_SHELL_COLOR_TEXT);
    shell_command_print_num(status.error_count);
    video_print("\n  Ultimo codigo: ", WIFI_SHELL_COLOR_TEXT);
    shell_command_print_num((uint32_t)status.last_error);
    video_print("\n  Parcial: ", WIFI_SHELL_COLOR_TEXT);
    video_print(status.partial ? "SIM" : "NAO", WIFI_SHELL_COLOR_TEXT);
    video_print("\n  Motivo: ", WIFI_SHELL_COLOR_TEXT);
    if (status.error_count) {
        video_print("erro no inventario", WIFI_SHELL_COLOR_ERROR);
    } else if (status.candidate_count) {
        video_print("nenhum backend Wi-Fi inicializado",
                    WIFI_SHELL_COLOR_WARN);
    } else {
        video_print("nenhum controlador PCI classe 0x02 ou USB RTL8811CU encontrado",
                    WIFI_SHELL_COLOR_INFO);
    }
    video_print("\n", WIFI_SHELL_COLOR_TEXT);
    wifi_print_inventory();
}

static void wifi_scan(void) {
    int result = wifi_manager_refresh();

    if (result == ERR_STATE) result = wifi_manager_init();

    if (result != OK && result != ERR_OVERFLOW) {
        LOG_ERROR("SHELL", "Falha ao atualizar inventario Wi-Fi");
        video_print("Erro: varredura Wi-Fi indisponivel.\n", WIFI_SHELL_COLOR_ERROR);
        return;
    }
    wifi_print_status();
    if (result == ERR_OVERFLOW) {
        video_print("Aviso: inventario Wi-Fi parcial.\n", WIFI_SHELL_COLOR_WARN);
    } else {
        video_print("Inventario PCI/USB concluido; nenhuma varredura 802.11 ou inicializacao de radio foi executada.\n",
                    WIFI_SHELL_COLOR_READY);
    }
}

static void wifi_connect(const char* args) {
    if (!args || args[0] == '\0') {
        video_print("Uso: wifi connect <ssid>\n", WIFI_SHELL_COLOR_WARN);
        return;
    }
    LOG_WARN("WIFI", "Associacao Wi-Fi indisponivel nesta etapa");
    video_print("wifi connect: ERR_UNAVAILABLE\n", WIFI_SHELL_COLOR_WARN);
    video_print("Backend RTL8811CU ainda nao inicializado; conexao nao executada.\n",
                WIFI_SHELL_COLOR_WARN);
    video_print("Nenhuma senha foi aceita, processada, exibida ou armazenada.\n",
                WIFI_SHELL_COLOR_INFO);
}

static void wifi_command(const char* args) {
    const char* connect_args;

    if (!args || shell_command_args_equal(args, "") ||
        shell_command_args_equal(args, "status")) {
        wifi_print_status();
        return;
    }
    if (shell_command_args_equal(args, "scan")) {
        wifi_scan();
        return;
    }
    connect_args = shell_command_match_subcommand(args, "connect");
    if (connect_args) {
        wifi_connect(connect_args);
        return;
    }
    LOG_WARN("SHELL", "Uso invalido do comando wifi");
    video_print("Uso: wifi status|scan|connect <ssid>\n", WIFI_SHELL_COLOR_WARN);
}

void shell_dispatch_cmd_wifi(const char* arguments) {
    wifi_command(arguments);
}
