#include "types.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/log.h"
#include "core/errors.h"
#include "core/device_manager.h"
#include "core/network_manager.h"
#include "core/power.h"
#include "core/recovery.h"
#include "core/app_api.h"
#include "core/app_catalog.h"
#include "core/app_loader.h"
#include "core/app_package.h"
#include "core/update.h"
#include "core/update_remote.h"
#include "core/version.h"
#include "core/syscall.h"
#include "drivers/idt.h"
#include "core/keyboard.h"
#include "drivers/mouse.h"
#include "core/timer.h"
#include "core/memory.h"
#include "memory/paging.h"
#include "process/process.h"
#include "drivers/tss.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "fs/fs.h"
#include "apps/shell.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/ac97.h"
#include "drivers/acpi.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
#include "ui/updater.h"
#include "ui/wm.h"
#include "ui/filemanager.h"
#include "ui/icons.h"
#include "ui/display.h"
#include "apps/taskmanager.h"
#include "apps/guitest.h"

#define SHELL_KEYBOARD_DISPATCH_BUDGET (IPC_MSG_QUEUE_SIZE / 2U)

static int kernel_service_fallback = 0;
static int kernel_network_poll_enabled = 1;
static uint32_t kernel_shell_pid = 0;
static volatile uint32_t kernel_pending_shell_request = 0;

static int kernel_send_shell_request(uint32_t request) {
    ipc_msg_t msg;
    uint32_t target_pid = kernel_shell_pid;
    process_t* target;

    if (!target_pid) target_pid = process_get_focus();
    if (!target_pid) {
        LOG_WARN("KERNEL", "Nenhum processo Shell definido para IPC");
        return 0;
    }

    target = process_get_by_pid(target_pid);
    if (!target) {
        LOG_WARN("KERNEL", "PID do Shell nao encontrado");
        return 0;
    }

    msg.type = IPC_MSG_APP_REQUEST;
    msg.data1 = request;
    msg.data2 = 0;
    return ipc_send(target->pid, &msg);
}

static int kernel_cancel_foreground_app(void) {
    int result;

    if (app_loader_get_foreground_pid() == 0) return 1;

    result = app_loader_cancel_foreground(PROCESS_EXIT_CANCELLED);
    if (result == OK) return 1;

    LOG_WARN("KERNEL", "Nao foi possivel encerrar app antes de abrir interface");
    return 0;
}

static void kernel_request_shell_app(ipc_app_request_t request) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_SHELL)) {
        LOG_WARN("KERNEL", "Shell indisponivel; solicitacao de aplicativo ignorada");
        return;
    }

    if (request < IPC_APP_OPEN_SHELL ||
        request > IPC_APP_OPEN_UPDATER) {
        LOG_ERROR("KERNEL", "Solicitacao de aplicativo invalida");
        return;
    }
    if (!kernel_cancel_foreground_app()) return;

    /* Mantem somente a intencao mais recente para evitar abrir dois apps. */
    kernel_pending_shell_request = request;
    if (kernel_send_shell_request(request)) {
        kernel_pending_shell_request = 0;
        return;
    }

    LOG_WARN("KERNEL", "Solicitacao ao Shell pendente para nova tentativa");
}

static void kernel_redraw_after_menu_close(void) {
    if (wm_is_active()) {
        wm_draw_all();
        return;
    }
    if (guitest_is_active()) {
        guitest_draw();
        return;
    }
    if (taskmgr_is_gui_open()) {
        if (taskmgr_is_gui_minimized()) desktop_draw();
        else taskmgr_gui_restore();
        return;
    }
    if (settings_is_open()) {
        settings_draw();
        return;
    }
    if (updater_is_open()) {
        updater_draw();
        return;
    }
    if (fm_is_running()) {
        fm_draw();
        return;
    }
    if (desktop_is_active()) desktop_draw();
    else taskbar_draw();
}

static void kernel_retry_shell_request(void) {
    uint32_t request = kernel_pending_shell_request;

    if (!request || !recovery_is_enabled(RECOVERY_COMPONENT_SHELL)) return;
    if (kernel_send_shell_request(request)) {
        kernel_pending_shell_request = 0;
    }
}

static int kernel_handle_taskbar_mouse(mouse_event_t* evt) {
    int tb_result;
    int menu_was_open;
    int hosted_workspace;
    tb_rect_t bounds;

    if (!evt) return 0;
    if (evt->event == MOUSE_EVENT_WHEEL) {
        if (taskbar_is_menu_open()) return 1;
        if (taskbar_get_bounds(&bounds) &&
            evt->x >= bounds.x && evt->x < bounds.x + bounds.width &&
            evt->y >= bounds.y && evt->y < bounds.y + bounds.height) {
            return 1;
        }
        return 0;
    }
    if (evt->event != MOUSE_EVENT_PRESS ||
        !(evt->changed & MOUSE_BTN_LEFT)) return 0;

    menu_was_open = taskbar_is_menu_open();
    tb_result = taskbar_handle_click(evt->x, evt->y);
    if (!tb_result) return 0;

    if (menu_was_open && (tb_result == 1 || tb_result == 9)) {
        kernel_redraw_after_menu_close();
        return 1;
    }

    hosted_workspace = wm_is_active() &&
                       desktop_get_mode() == DESKTOP_MODE_MODERN;
    if (!hosted_workspace) {
        if (taskmgr_is_gui_open() && tb_result >= 2 && tb_result <= 8) {
            if (tb_result == 4) {
                taskmgr_gui_restore();
                return 1;
            }
            taskmgr_close();
        } else if (taskmgr_is_open() && tb_result >= 2 && tb_result <= 8) {
            if (tb_result == 4) {
                taskmgr_refresh();
                return 1;
            }
            taskmgr_close();
        }

        if (settings_is_open() &&
            ((tb_result >= 2 && tb_result <= 8) ||
             tb_result == TB_ACTION_UPDATER)) {
            settings_close();
        }
        if (updater_is_open() &&
            ((tb_result >= 2 && tb_result <= 8) ||
             tb_result == TB_ACTION_UPDATER)) {
            if (tb_result == TB_ACTION_UPDATER) {
                updater_draw();
                return 1;
            }
            updater_close();
        }
        if (wm_is_active() && tb_result >= 2 && tb_result <= 8) {
            wm_set_active(0);
        }
    }

    switch (tb_result) {
        case 2:
            kernel_request_shell_app(IPC_APP_OPEN_SHELL);
            break;
        case 3: kernel_request_shell_app(IPC_APP_OPEN_EXPLORER); break;
        case 4: kernel_request_shell_app(IPC_APP_OPEN_TASKMANAGER_GUI); break;
        case 5:
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE),
                         "Nd"((uint16_t)0x64));
            break;
        case 6:
            power_shutdown();
            break;
        case 7:
            if (hosted_workspace) wm_set_active(0);
            kernel_request_shell_app(IPC_APP_OPEN_DESKTOP);
            break;
        case 8: kernel_request_shell_app(IPC_APP_OPEN_SETTINGS); break;
        case TB_ACTION_UPDATER:
            kernel_request_shell_app(IPC_APP_OPEN_UPDATER);
            break;
        case TB_ACTION_WINDOW: {
            int window_id = taskbar_take_window_request();
            if (window_id >= 0 && wm_is_active()) wm_toggle_window(window_id);
            break;
        }
        default: break;
    }

    return 1;
}

/* Handler global de eventos do mouse, despacha para a UI ativa */
static void global_mouse_handler(mouse_event_t* evt) {
    if (kernel_handle_taskbar_mouse(evt)) return;

    if (wm_is_active()) {
        wm_handle_mouse(evt);
        return;
    }

    if (guitest_is_active()) {
        guitest_handle_mouse(evt);
        return;
    }

    /* Arraste e soltura pertencem a janela grafica antes do Desktop. */
    if (taskmgr_is_gui_open() && evt->event != MOUSE_EVENT_PRESS) {
        taskmgr_gui_handle_mouse(evt);
        return;
    }

    /* O Desktop moderno precisa do gesto completo para arrastar icones. */
    if (desktop_is_active() && desktop_get_mode() == DESKTOP_MODE_MODERN) {
        int result = desktop_handle_mouse(evt);

        if (result == DESKTOP_APP_SHELL) {
            kernel_request_shell_app(IPC_APP_OPEN_SHELL);
        } else if (result == DESKTOP_APP_EXPLORER) {
            kernel_request_shell_app(IPC_APP_OPEN_EXPLORER);
        } else if (result == DESKTOP_APP_TASKMGR) {
            kernel_request_shell_app(IPC_APP_OPEN_TASKMANAGER_GUI);
        }
        return;
    }

    if (evt->event == MOUSE_EVENT_WHEEL && shell_handle_mouse(evt)) return;

    /* O restante do sistema legado so processa clique esquerdo (press). */
    if (evt->event != MOUSE_EVENT_PRESS) return;
    if (!(evt->changed & MOUSE_BTN_LEFT)) return;

    if (taskmgr_is_gui_open()) {
        taskmgr_gui_handle_mouse(evt);
        return;
    }

    if (settings_is_open()) {
        settings_handle_mouse(evt);
        return;
    }

    /* Tenta desktop */
    if (desktop_is_active()) {
        int result;

        result = desktop_handle_click(evt->x, evt->y);

        if (result == DESKTOP_APP_SHELL) {
            kernel_request_shell_app(IPC_APP_OPEN_SHELL);
        } else if (result == DESKTOP_APP_EXPLORER) {
            kernel_request_shell_app(IPC_APP_OPEN_EXPLORER);
        } else if (result == DESKTOP_APP_TASKMGR) {
            kernel_request_shell_app(IPC_APP_OPEN_TASKMANAGER_GUI);
        }
    }

}

void system_process_main(void) {
    while (1) {
        uint32_t network_processed = 0;

        keyboard_process_events();
        mouse_process_events();
        if (kernel_network_poll_enabled &&
            network_manager_poll(&network_processed) != OK) {
            kernel_network_poll_enabled = 0;
            LOG_ERROR("KERNEL", "Processamento Ethernet foi desabilitado");
        }
        shell_update_hosted_terminal();
        kernel_retry_shell_request();
        taskbar_update_clock();
        wm_update_cpu_stats();
        process_yield();
    }
}

void shell_process_main(void) {
    shell_init();
    ipc_msg_t msg;
    while (1) {
        uint32_t keyboard_events = 0;
        int received = 0;

        while (keyboard_events < SHELL_KEYBOARD_DISPATCH_BUDGET &&
               ipc_receive(&msg)) {
            received = 1;
            if (msg.type == IPC_MSG_KEYBOARD) {
                shell_handle_key((uint8_t)msg.data1);
                keyboard_events++;
            } else if (msg.type == IPC_MSG_APP_REQUEST) {
                shell_handle_app_request(msg.data1);
                break;
            }
        }
        if (!received) {
            process_yield();
        }
        app_loader_reap_finished();
        shell_report_user_test_result();
        shell_report_app_loader_result();
        taskmgr_gui_update();
        shell_update_hosted_terminal();
    }
}

void desktop_process_main(void) {
    /* A cena inicial e desenhada pelo kernel apos o boot completo. */
    while (1) {
        process_yield();
    }
}

static int kernel_start_automatic_dhcp(void) {
    network_interface_info_t info;
    network_interface_text_t text;
    uint32_t interface_count = 0;
    int result;

    result = network_manager_get_count(&interface_count);
    if (result != OK) {
        LOG_ERROR("KERNEL", "Falha ao enumerar NICs para DHCP automatico");
        return result;
    }
    for (uint32_t index = 0; index < interface_count; index++) {
        result = network_manager_get_interface(index, &info);
        if (result != OK) {
            LOG_ERROR("KERNEL", "Falha ao consultar NIC para DHCP automatico");
            return result;
        }
        if (info.state != NETWORK_INTERFACE_ACTIVE ||
            !info.ethernet_attached ||
            info.link != NETWORK_LINK_UP) continue;
        result = network_manager_format_text(&info, &text);
        if (result != OK) {
            LOG_ERROR("KERNEL", "Falha ao identificar NIC para DHCP automatico");
            return result;
        }
        LOG_INFO("KERNEL", "Iniciando aquisicao DHCP automatica");
        result = network_manager_acquire_dhcp(text.id);
        if (result != OK) {
            LOG_ERROR("KERNEL", "Falha ao iniciar DHCP automatico");
            return result;
        }
        LOG_INFO("KERNEL", "DHCP automatico iniciado sem bloquear o boot");
        return OK;
    }
    LOG_WARN("KERNEL", "Nenhuma NIC ativa com link para DHCP automatico");
    return ERR_NOT_FOUND;
}

void kernel_main(uint32_t mmap_addr, uint32_t vesa_info_addr) {
    vesa_init(vesa_info_addr);
    font_init();
    video_init();
    log_init();
    recovery_init();

    vesa_mode_t* vmode = vesa_get_mode();
    if (vmode && vmode->initialized) {
        recovery_mark_ready(RECOVERY_COMPONENT_VESA);
        video_print("[OK] VESA framebuffer ativo\n", 0x0A);
        video_print("[OK] Modo: ", 0x07);
        char res_buf[16];
        uint32_t num = vmode->width;
        int i = 0;
        if (num == 0) { res_buf[i++] = '0'; }
        else {
            char tmp[16];
            int j = 0;
            while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
            while (j > 0) { res_buf[i++] = tmp[--j]; }
        }
        res_buf[i] = 'x';
        i++;
        num = vmode->height;
        if (num == 0) { res_buf[i++] = '0'; }
        else {
            char tmp[16];
            int j = 0;
            while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
            while (j > 0) { res_buf[i++] = tmp[--j]; }
        }
        res_buf[i] = '\0';
        video_print(res_buf, 0x0B);
        video_print("x", 0x07);
        if (vmode->bpp == VESA_BPP_24) {
            video_print("24", 0x07);
        } else if (vmode->bpp == VESA_BPP_32) {
            video_print("32", 0x07);
        } else {
            video_print("??", 0x0C);
        }
        video_print(" bpp\n", 0x07);
    } else {
        recovery_mark_degraded(RECOVERY_COMPONENT_VESA, ERR_NOT_FOUND,
                               "VESA indisponivel; fallback VGA ativo");
        video_print("[!!] VESA nao encontrado, usando VGA fallback\n", 0x0C);
    }

    video_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    video_print("========================================\n", 0x0B);
    video_print("           " ZEPHYROS_DISPLAY_NAME "              \n", 0x0B);
    video_print("========================================\n", 0x0B);

    video_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    video_print("\n[OK] Kernel carregado com sucesso!\n", 0x0A);

    video_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    video_print("[OK] Modo protegido ativo (32-bit)\n", 0x07);
    video_print("[OK] VGA Text Mode inicializado\n", 0x07);
    video_print("[OK] Panic handler pronto\n", 0x07);

    video_print("[..] Carregando interrupcoes...\n", 0x08);
    idt_init();
    video_print("[OK] IDT configurada\n", 0x07);

    video_print("[..] Iniciando teclado...\n", 0x08);
    keyboard_init();
    video_print("[OK] Driver de teclado PS/2\n", 0x07);

    video_print("[..] Iniciando mouse...\n", 0x08);
    mouse_init();
    mouse_set_callback(global_mouse_handler);
    video_print("[OK] Driver de mouse PS/2\n", 0x07);

    video_print("[..] Iniciando timer...\n", 0x08);
    timer_init(50);
    video_print("[OK] Timer PIT (50 Hz)\n", 0x07);

    video_print("[..] Detectando memoria...\n", 0x08);
    memory_init(mmap_addr);
    video_print("[OK] Memoria detectada\n", 0x07);

    int acpi_result = acpi_init((const mmap_entry_t*)mmap_addr,
                                memory_get_mmap_entries());
    acpi_status_t acpi_status;
    if (acpi_get_status(&acpi_status) == OK && acpi_status.available) {
        if (acpi_result == OK) {
            recovery_mark_ready(RECOVERY_COMPONENT_ACPI);
        } else {
            recovery_mark_degraded(RECOVERY_COMPONENT_ACPI, acpi_result,
                                   "Snapshot ACPI parcial");
        }
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_ACPI, acpi_result,
                               "ACPI indisponivel; energia em modo diagnostico");
    }

    uint32_t total_mb = memory_get_total() / (1024 * 1024);
    uint32_t free_mb = memory_get_free() / (1024 * 1024);
    video_print("     Total: ", 0x08);

    char buf[16];
    uint32_t num = total_mb;
    int i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
    video_print(" MB total, ", 0x07);

    num = free_mb;
    i = 0;
    if (num == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (num > 0) { tmp[j++] = '0' + (num % 10); num /= 10; }
        while (j > 0) { buf[i++] = tmp[--j]; }
    }
    buf[i] = '\0';
    video_print(buf, 0x07);
    video_print(" MB livres\n", 0x07);

    if (app_api_init() != OK) {
        LOG_ERROR("KERNEL", "Falha ao inicializar API de aplicativos");
    }
    if (syscall_init() != OK) {
        LOG_ERROR("KERNEL", "Falha ao inicializar dispatcher de syscalls");
    }

    video_print("[..] Configurando paginacao...\n", 0x08);
    if (paging_init() != OK) {
        panic_memory("Falha ao configurar paginacao!",
                     memory_get_mmap_entries(), memory_get_total(),
                     memory_get_free(), memory_get_free_pages());
        return;
    }
    video_print("[OK] Paging ativo\n", 0x07);

    int backbuffer_result = vesa_init_backbuffer();
    if (backbuffer_result == OK) {
        recovery_mark_ready(RECOVERY_COMPONENT_BACKBUFFER);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_BACKBUFFER,
                               backbuffer_result,
                               "Backbuffer indisponivel; Desktop classic ativo");
        if (recovery_is_available(RECOVERY_COMPONENT_VESA)) {
            recovery_mark_degraded(RECOVERY_COMPONENT_VESA, backbuffer_result,
                                   "VESA degradado sem backbuffer");
        }
    }

    /* A segunda metade do boot produz scroll no framebuffer. O frame externo
       preserva os logs no backbuffer e apresenta somente a cena final. */
    video_begin_update();

    video_print("[..] Iniciando processos...\n", 0x08);
    tss_init();
    process_init();
    process_bootstrap_idle();
    ipc_init();
    video_print("[OK] TSS configurado\n", 0x07);

    video_print("[..] Iniciando threads...\n", 0x08);
    thread_init();
    video_print("[OK] Thread scheduler pronto\n", 0x07);

    video_print("[..] Detectando disco...\n", 0x08);
    ata_init();
    ata_device_t* dev = ata_get_device();
    if (dev) {
        recovery_mark_ready(RECOVERY_COMPONENT_ATA);
        video_print("[OK] Disco: ", 0x07);
        video_print(dev->model, 0x07);
        video_print("\n", 0x07);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_ATA, ERR_NOT_FOUND,
                               "Nenhum dispositivo ATA disponivel");
        video_print("[!!] Nenhum disco encontrado\n", 0x0C);
    }

    video_print("[..] Montando sistema de arquivos...\n", 0x08);
    int fs_result = fs_init();
    if (fs_result == OK && fs_get_type() != FS_TYPE_NONE) {
        recovery_mark_ready(RECOVERY_COMPONENT_FILESYSTEM);
        video_print("[OK] Sistema de arquivos montado\n", 0x07);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_FILESYSTEM,
                               fs_result,
                               "Sistema de arquivos indisponivel");
        video_print("[!!] Nenhum sistema de arquivos encontrado\n", 0x0C);
    }

    update_capabilities_t update_capabilities;
    int update_result = update_init();
    if (update_result != OK) {
        recovery_mark_disabled(RECOVERY_COMPONENT_UPDATE, update_result,
                               "Chave ou autoteste de Update invalido");
    } else if (update_get_capabilities(&update_capabilities) != OK) {
        recovery_mark_disabled(RECOVERY_COMPONENT_UPDATE, ERR_STATE,
                               "Capacidades de Update indisponiveis");
    } else if (!update_capabilities.local_file_available) {
        recovery_mark_degraded(RECOVERY_COMPONENT_UPDATE, ERR_UNAVAILABLE,
                               "Verificador pronto; filesystem indisponivel");
    } else if (fs_get_type() == FS_TYPE_FAT12 &&
               !update_capabilities.persistent_state_ready) {
        recovery_mark_degraded(
            RECOVERY_COMPONENT_UPDATE, ERR_STATE,
            update_capabilities.recovery_pending ?
            "Recuperacao de Update pendente" :
            "Estado persistente de Update invalido");
    } else {
        recovery_mark_ready(RECOVERY_COMPONENT_UPDATE);
    }

    video_print("[..] Iniciando PC Speaker...\n", 0x08);
    speaker_init();
    video_print("[OK] PC Speaker pronto\n", 0x07);

    video_print("[..] Enumerando dispositivos PCI...\n", 0x08);
    int pci_result = pci_init();
    if (pci_result == OK) {
        video_print("[OK] Varredura PCI concluida\n", 0x07);
    } else if (pci_result == ERR_OVERFLOW) {
        LOG_WARN("KERNEL", "Inventario PCI parcial; limite atingido");
        video_print("[!!] Inventario PCI parcial\n", 0x0E);
    } else {
        LOG_ERROR("KERNEL", "Falha ao enumerar PCI");
        video_print("[!!] PCI indisponivel\n", 0x0C);
    }

    video_print("[..] Iniciando AC97...\n", 0x08);
    ac97_init();
    ac97_device_t* ac97 = ac97_get_device();
    if (ac97 && ac97->initialized) {
        recovery_mark_ready(RECOVERY_COMPONENT_AC97);
        video_print("[OK] AC97 pronto\n", 0x07);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_AC97, ERR_NOT_FOUND,
                               "Audio AC97 indisponivel");
        video_print("[!!] AC97 nao encontrado\n", 0x0C);
    }

    video_print("[..] Criando inventario de dispositivos...\n", 0x08);
    int device_result = device_manager_init();
    if (device_result == OK) {
        recovery_mark_ready(RECOVERY_COMPONENT_DEVICES);
        video_print("[OK] Inventario de dispositivos pronto\n", 0x07);
    } else if (device_result == ERR_OVERFLOW) {
        recovery_mark_degraded(RECOVERY_COMPONENT_DEVICES, device_result,
                               "Inventario PCI parcial");
        video_print("[!!] Inventario de dispositivos parcial\n", 0x0E);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_DEVICES, device_result,
                               "Inventario de dispositivos indisponivel");
        LOG_ERROR("KERNEL", "Falha ao criar inventario de dispositivos");
        video_print("[!!] Inventario de dispositivos indisponivel\n", 0x0C);
    }

    video_print("[..] Criando inventario de rede...\n", 0x08);
    int network_result = network_manager_init();
    network_manager_status_t network_status;
    if (network_result != OK && network_result != ERR_OVERFLOW) {
        LOG_ERROR("KERNEL", "Falha ao criar inventario de rede");
        video_print("[!!] Inventario de rede indisponivel\n", 0x0C);
    } else if (network_manager_get_status(&network_status) != OK) {
        LOG_ERROR("KERNEL", "Falha ao consultar inventario de rede");
        video_print("[!!] Estado de rede indisponivel\n", 0x0C);
    } else if (network_status.partial) {
        video_print("[!!] Inventario de rede parcial\n", 0x0E);
    } else if (network_status.active_count &&
               network_status.driver_error_count) {
        video_print("[!!] Rede ativa com NIC degradada\n", 0x0E);
    } else if (network_status.active_count &&
               network_status.ethernet_available) {
        video_print("[OK] Camada Ethernet ativa\n", 0x07);
    } else if (network_status.active_count) {
        video_print("[!!] Driver ativo; camada Ethernet indisponivel\n",
                    0x0E);
    } else if (network_status.interface_count) {
        if (network_status.last_error == ERR_UNAVAILABLE) {
            video_print("[!!] NIC detectada; driver pendente\n", 0x0E);
        } else {
            video_print("[!!] NIC detectada; driver com falha\n", 0x0C);
        }
    } else {
        video_print("[--] Nenhum controlador de rede encontrado\n", 0x08);
    }

    if (network_result == OK || network_result == ERR_OVERFLOW) {
        int dhcp_result = kernel_start_automatic_dhcp();

        if (dhcp_result == OK) {
            video_print("[OK] DHCP automatico iniciado em background\n",
                        0x07);
        } else if (dhcp_result == ERR_NOT_FOUND) {
            video_print("[--] DHCP automatico sem interface elegivel\n",
                        0x08);
        } else {
            video_print("[!!] DHCP automatico indisponivel\n", 0x0E);
        }
    }

    video_print("[..] Iniciando distribuicao remota de Update...\n", 0x08);
    if (update_remote_init() == OK) {
        video_print("[OK] Update remoto pronto e desabilitado\n", 0x07);
    } else {
        video_print("[!!] Update remoto indisponivel\n", 0x0E);
    }

    video_print("[..] Iniciando diagnostico de energia...\n", 0x08);
    int power_result = power_init();
    if (power_result == OK) {
        recovery_mark_ready(RECOVERY_COMPONENT_POWER);
        video_print("[OK] Diagnostico de energia pronto\n", 0x07);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_POWER, power_result,
                               "Diagnostico de energia indisponivel");
        LOG_ERROR("KERNEL", "Falha ao iniciar diagnostico de energia");
        video_print("[!!] Diagnostico de energia indisponivel\n", 0x0C);
    }

    video_print("[..] Iniciando shell...\n", 0x08);
    video_print("[OK] Shell pronta\n", 0x07);

    video_print("[..] Iniciando icones...\n", 0x08);
    icons_init();
    video_print("[OK] Icones prontos\n", 0x07);

    video_print("[..] Iniciando metricas de display...\n", 0x08);
    int display_result = display_init();
    if (display_result == OK) {
        video_print("[OK] Metricas de display prontas\n", 0x07);
    } else {
        video_print("[!!] Display Modern indisponivel\n", 0x0E);
    }

    video_print("[..] Iniciando taskbar...\n", 0x08);
    taskbar_init();
    recovery_mark_ready(RECOVERY_COMPONENT_TASKBAR);
    video_print("[OK] Taskbar pronta\n", 0x07);

    video_print("[..] Iniciando desktop...\n", 0x08);
    desktop_init();
    if (!recovery_is_available(RECOVERY_COMPONENT_BACKBUFFER) ||
        display_result != OK) {
        desktop_set_mode(DESKTOP_MODE_CLASSIC);
    }
    recovery_mark_ready(RECOVERY_COMPONENT_DESKTOP);
    video_print("[OK] Desktop pronto\n", 0x07);

    video_print("[..] Iniciando configuracoes...\n", 0x08);
    settings_init();
    recovery_mark_ready(RECOVERY_COMPONENT_SETTINGS);
    video_print("[OK] Configuracoes prontas\n", 0x07);

    video_print("[..] Iniciando gerenciador de janelas...\n", 0x08);
    wm_init();
    recovery_mark_ready(RECOVERY_COMPONENT_WM);
    video_print("[OK] Gerenciador de janelas pronto\n", 0x07);

    video_print("[..] Iniciando System Updater...\n", 0x08);
    if (updater_init() == OK) {
        video_print("[OK] System Updater pronto\n", 0x07);
    } else {
        video_print("[!!] System Updater indisponivel\n", 0x0C);
    }

    recovery_mark_ready(RECOVERY_COMPONENT_TASKMANAGER);
    if (recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        recovery_mark_ready(RECOVERY_COMPONENT_FILEMANAGER);
        recovery_mark_ready(RECOVERY_COMPONENT_EDITOR);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_FILEMANAGER, ERR_UNAVAILABLE,
                               "File Manager requer filesystem");
        recovery_mark_degraded(RECOVERY_COMPONENT_EDITOR, ERR_UNAVAILABLE,
                               "Editor sem filesystem; salvar arquivos indisponivel");
    }

    if (!recovery_is_available(RECOVERY_COMPONENT_FILESYSTEM)) {
        recovery_mark_disabled(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_UNAVAILABLE,
                               "Media Player requer filesystem");
    } else if (recovery_is_available(RECOVERY_COMPONENT_AC97) ||
               recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        if (recovery_is_available(RECOVERY_COMPONENT_AC97) &&
            recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
            recovery_mark_ready(RECOVERY_COMPONENT_MEDIAPLAYER);
        } else {
            recovery_mark_degraded(RECOVERY_COMPONENT_MEDIAPLAYER,
                                   ERR_UNAVAILABLE,
                                   "Media Player parcialmente disponivel");
        }
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_MEDIAPLAYER, ERR_UNAVAILABLE,
                               "Media Player sem audio e video disponiveis");
    }

    if (recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        recovery_mark_ready(RECOVERY_COMPONENT_GUITEST);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_GUITEST, ERR_UNAVAILABLE,
                               "GUI Test requer VESA");
    }

    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    video_print("\nZephyrOS pronto! Digite 'help' para comandos.\n", 0x0E);

    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    taskbar_draw();

    process_t* system_process = process_create("Zephyr System", system_process_main);
    if (system_process) {
        recovery_mark_ready(RECOVERY_COMPONENT_SYSTEM_PROCESS);
    } else {
        kernel_service_fallback = 1;
        recovery_mark_degraded(RECOVERY_COMPONENT_SYSTEM_PROCESS, ERR_MEM,
                               "Processo System falhou; servicos no loop do kernel");
    }

    process_t* shell_process = process_create("Shell", shell_process_main);
    if (shell_process) {
        kernel_shell_pid = shell_process->pid;
        if (process_set_focus_fallback(shell_process->pid) != OK ||
            process_set_focus(shell_process->pid) != OK) {
            recovery_mark_degraded(RECOVERY_COMPONENT_SHELL, ERR_STATE,
                                   "Shell criado sem foco seguro");
        } else {
            recovery_mark_ready(RECOVERY_COMPONENT_SHELL);
        }
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_SHELL, ERR_MEM,
                                "Processo Shell indisponivel");
    }

    process_t* desktop_process = process_create("Desktop", desktop_process_main);
    if (desktop_process) {
        recovery_mark_ready(RECOVERY_COMPONENT_DESKTOP);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_DESKTOP, ERR_MEM,
                               "Processo Desktop indisponivel");
    }

    if (syscall_enable_user_mode() != OK) {
        LOG_WARN("KERNEL", "Modo usuario indisponivel; mantendo processos ring 0");
    }

    if (app_loader_init() == OK) {
        recovery_mark_ready(RECOVERY_COMPONENT_APP_LOADER);
    } else {
        recovery_mark_disabled(RECOVERY_COMPONENT_APP_LOADER,
                               ERR_UNAVAILABLE,
                               "Carregador ZAPP indisponivel");
    }

    app_package_init();
    app_catalog_init();

    /* Desktop e a cena padrao; o Shell abre somente por solicitacao. */
    desktop_set_active(1);
    desktop_draw();
    video_end_update();

    while (1) {
        if (kernel_service_fallback) {
            uint32_t network_processed = 0;

            keyboard_process_events();
            mouse_process_events();
            if (kernel_network_poll_enabled &&
                network_manager_poll(&network_processed) != OK) {
                kernel_network_poll_enabled = 0;
                LOG_ERROR("KERNEL",
                          "Processamento Ethernet fallback desabilitado");
            }
            shell_update_hosted_terminal();
            kernel_retry_shell_request();
            taskbar_update_clock();
            wm_update_cpu_stats();
        }
        process_yield();
        asm volatile("hlt");
    }
}
