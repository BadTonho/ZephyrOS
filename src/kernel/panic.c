#include "panic.h"
#include "video.h"

void panic(const char* message) {
    video_clear();
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("========================================\n", 0x4F);
    video_print("           KERNEL PANIC                 \n", 0x4F);
    video_print("========================================\n", 0x4F);
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("\nO kernel encontrou um erro fatal:\n\n", 0x4F);
    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_RED);
    video_print(message, 0x4E);
    video_print("\n\n", 0x4F);
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("O sistema foi interrompido.\n", 0x4F);
    video_print("Reinicie o computador para continuar.\n", 0x4F);

    panic_halt();
}

void panic_halt(void) {
    asm volatile("cli");
    asm volatile("hlt");
    while (1) {}
}
