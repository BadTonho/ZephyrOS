#include "video.h"
#include "panic.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"

void kernel_main(void) {
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

    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    video_print("\nSistema operacional funcional!\n", 0x0E);
    video_print("Em breve: memoria e shell.\n", 0x0E);

    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    video_print("\n> ", 0x0F);

    while (1) {}
}
