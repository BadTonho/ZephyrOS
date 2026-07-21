#include "types.h"
#include "core/video.h"
#include "core/panic.h"
#include "core/log.h"
#include "drivers/idt.h"
#include "core/keyboard.h"
#include "drivers/mouse.h"
#include "core/timer.h"
#include "core/memory.h"
#include "memory/paging.h"
#include "process/process.h"
#include "drivers/tss.h"
#include "drivers/ata.h"
#include "fs/fs.h"
#include "apps/shell.h"
#include "drivers/speaker.h"
#include "process/thread.h"
#include "drivers/vesa.h"
#include "drivers/font.h"
#include "drivers/ac97.h"
#include "ui/taskbar.h"
#include "ui/desktop.h"
#include "ui/settings.h"
#include "ui/wm.h"
#include "ui/icons.h"
#include "ui/filemanager.h"
#include "apps/taskmanager.h"
#include "apps/guitest.h"

/* Handler global de eventos do mouse, despacha para a UI ativa */
static void global_mouse_handler(mouse_event_t* evt) {
    if (guitest_is_active()) {
        guitest_handle_mouse(evt);
        return;
    }

    /* O restante do sistema legado so processa clique esquerdo (press) */
    if (evt->event != MOUSE_EVENT_PRESS) return;
    if (!(evt->changed & MOUSE_BTN_LEFT)) return;

    /* Tenta taskbar primeiro */
    int tb_result = taskbar_handle_click(evt->x, evt->y);
    if (tb_result) {
        if (tb_result == 2) {
            desktop_set_active(0);
            video_clear();
            shell_print_prompt();
            taskbar_draw();
        } else if (tb_result == 3) {
            fm_run();
        } else if (tb_result == 4) {
            taskmgr_run();
        } else if (tb_result == 5) {
            // Reiniciar - para contornar a falta de acesso a cmd_reboot aqui
            // enviaremos um scan_code falso pro shell tratar? Nao, reset via port.
            asm volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
        } else if (tb_result == 6) {
            // Desligar - QEMU/Bochs poweroff via ACPI/APM
            asm volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
        } else if (tb_result == 7) {
            if (!desktop_is_active()) {
                desktop_set_active(1);
            }
            desktop_draw();
        } else if (tb_result == 8) {
            settings_open();
        }
        return;
    }

    /* Tenta desktop */
    if (desktop_is_active()) {
        int result = desktop_handle_click(evt->x, evt->y);
        if (result == DESKTOP_APP_SHELL) {
            desktop_set_active(0);
            video_clear();
            taskbar_draw();
        } else if (result == DESKTOP_APP_EXPLORER) {
            desktop_set_active(0);
            fm_run();
        } else if (result == DESKTOP_APP_TASKMGR) {
            desktop_set_active(0);
            taskmgr_run();
        }
    }
}

void system_process_main(void) {
    while (1) {
        keyboard_process_events();
        mouse_process_events();
        taskbar_update_clock();
        wm_update_cpu_stats();
        process_yield();
    }
}

void shell_process_main(void) {
    shell_init();
    process_set_focus(process_get_current()->pid);
    ipc_msg_t msg;
    while (1) {
        if (ipc_receive(&msg)) {
            if (msg.type == IPC_MSG_KEYBOARD) {
                shell_handle_key((uint8_t)msg.data1);
            }
        } else {
            process_yield();
        }
    }
}

void desktop_process_main(void) {
    desktop_set_active(1);
    desktop_draw();
    while (1) {
        process_yield();
    }
}

void kernel_main(uint32_t mmap_addr, uint32_t vesa_info_addr) {
    vesa_init(vesa_info_addr);
    font_init();
    video_init();
    log_init();

    vesa_mode_t* vmode = vesa_get_mode();
    if (vmode && vmode->initialized) {
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
        video_print("[!!] VESA nao encontrado, usando VGA fallback\n", 0x0C);
    }

    video_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    video_print("========================================\n", 0x0B);
    video_print("           ZephyrOS v0.1                 \n", 0x0B);
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

    video_print("[..] Configurando paginacao...\n", 0x08);
    paging_init();
    video_print("[OK] Paging ativo\n", 0x07);

    video_print("[..] Iniciando processos...\n", 0x08);
    tss_init();
    process_init();
    process_bootstrap_idle();
    video_print("[OK] TSS configurado\n", 0x07);

    video_print("[..] Iniciando threads...\n", 0x08);
    thread_init();
    video_print("[OK] Thread scheduler pronto\n", 0x07);

    video_print("[..] Detectando disco...\n", 0x08);
    ata_init();
    ata_device_t* dev = ata_get_device();
    if (dev) {
        video_print("[OK] Disco: ", 0x07);
        video_print(dev->model, 0x07);
        video_print("\n", 0x07);
    } else {
        video_print("[!!] Nenhum disco encontrado\n", 0x0C);
    }

    video_print("[..] Montando sistema de arquivos...\n", 0x08);
    fs_init();
    if (fs_get_type() != 0) {
        video_print("[OK] Sistema de arquivos montado\n", 0x07);
    } else {
        video_print("[!!] Nenhum sistema de arquivos encontrado\n", 0x0C);
    }

    video_print("[..] Iniciando PC Speaker...\n", 0x08);
    speaker_init();
    video_print("[OK] PC Speaker pronto\n", 0x07);

    video_print("[..] Iniciando AC97...\n", 0x08);
    ac97_init();
    ac97_device_t* ac97 = ac97_get_device();
    if (ac97 && ac97->initialized) {
        video_print("[OK] AC97 pronto\n", 0x07);
    } else {
        video_print("[!!] AC97 nao encontrado\n", 0x0C);
    }

    video_print("[..] Iniciando shell...\n", 0x08);
    video_print("[OK] Shell pronta\n", 0x07);

    video_print("[..] Iniciando icones...\n", 0x08);
    icons_init();
    video_print("[OK] Icones prontos\n", 0x07);

    video_print("[..] Iniciando taskbar...\n", 0x08);
    taskbar_init();
    video_print("[OK] Taskbar pronta\n", 0x07);

    video_print("[..] Iniciando desktop...\n", 0x08);
    desktop_init();
    video_print("[OK] Desktop pronto\n", 0x07);

    video_print("[..] Iniciando configuracoes...\n", 0x08);
    settings_init();
    video_print("[OK] Configuracoes prontas\n", 0x07);

    video_print("[..] Iniciando gerenciador de janelas...\n", 0x08);
    wm_init();
    video_print("[OK] Gerenciador de janelas pronto\n", 0x07);

    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    video_print("\nZephyrOS pronto! Digite 'help' para comandos.\n", 0x0E);

    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    taskbar_draw();

    process_create("Zephyr System", system_process_main);
    process_create("Shell", shell_process_main);
    process_create("Desktop", desktop_process_main);

    while (1) {
        process_yield();
        asm volatile("hlt");
    }
}
