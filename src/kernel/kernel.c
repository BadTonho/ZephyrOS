#include "types.h"
#include "video.h"
#include "panic.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"
#include "paging.h"
#include "process.h"
#include "tss.h"
#include "ata.h"
#include "fs.h"
#include "shell.h"
#include "speaker.h"
#include "thread.h"
#include "vesa.h"
#include "font.h"
#include "ac97.h"
#include "taskbar.h"
#include "desktop.h"
#include "settings.h"
#include "wm.h"
#include "icons.h"

void kernel_main(uint32_t mmap_addr) {
    video_init();

    video_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    video_print("========================================\n", 0x0B);
    video_print("           MiniOS v0.1                  \n", 0x0B);
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

    video_print("[..] Iniciando VESA...\n", 0x08);
    font_init();
    vesa_init();
    vesa_mode_t* vmode = vesa_get_mode();
    if (vmode && vmode->initialized) {
        video_print("[OK] VESA ", 0x07);
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
        video_print(res_buf, 0x07);
        video_print("x32\n", 0x07);
    } else {
        video_print("[!!] VESA nao encontrado\n", 0x0C);
    }

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
    video_print("\nMiniOS pronto! Digite 'help' para comandos.\n", 0x0E);

    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    taskbar_draw();
    shell_init();
    desktop_set_active(1);
    desktop_draw();

    while (1) {
        taskbar_update_clock();
        wm_update_cpu_stats();
        asm volatile("hlt");
    }
}
