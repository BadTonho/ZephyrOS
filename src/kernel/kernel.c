#include "video.h"
#include "panic.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"
#include "paging.h"
#include "process.h"
#include "tss.h"

static void task_a(void) {
    while (1) {
        video_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        video_print("[A] ", 0x0A);
        process_block(50);
    }
}

static void task_b(void) {
    while (1) {
        video_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        video_print("[B] ", 0x0B);
        process_block(30);
    }
}

static void task_c(void) {
    while (1) {
        video_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        video_print("[C] ", 0x0C);
        process_block(20);
    }
}

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

    process_create("task_a", task_a);
    process_create("task_b", task_b);
    process_create("task_c", task_c);
    video_print("[OK] 3 processos criados\n", 0x07);

    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    video_print("\nSistema funcional com processos!\n", 0x0E);
    video_print("Em breve: shell.\n", 0x0E);

    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    video_print("\n> ", 0x0F);

    current_process = process_get_current();
    if (!current_process) {
        process_t* idle = process_create("idle", 0);
        if (idle) current_process = idle;
    }

    while (1) {
        process_yield();
    }
}
