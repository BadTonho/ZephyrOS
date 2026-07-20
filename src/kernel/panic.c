#include "core/panic.h"
#include "core/video.h"

static void panic_draw_header(void) {
    video_clear();
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("========================================\n", 0x4F);
    video_print("           KERNEL PANIC                 \n", 0x4F);
    video_print("========================================\n", 0x4F);
}

static void panic_print_number(uint32_t value) {
    char buffer[11];
    int index = 0;

    if (value == 0) {
        video_print("0", 0x4E);
        return;
    }

    while (value > 0 && index < 10) {
        buffer[index++] = '0' + (value % 10);
        value /= 10;
    }

    while (index > 0) {
        video_put_char(buffer[--index], 0x4E);
    }
}

static void panic_print_metric(const char* label, uint32_t value) {
    video_print(label, 0x4F);
    panic_print_number(value);
    video_print("\n", 0x4F);
}

void panic(const char* message) {
    panic_draw_header();
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("\nO kernel encontrou um erro fatal:\n\n", 0x4F);
    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_RED);
    video_print(message ? message : "Mensagem de panic ausente", 0x4E);
    video_print("\n\n", 0x4F);
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("O sistema foi interrompido.\n", 0x4F);
    video_print("Reinicie o computador para continuar.\n", 0x4F);

    panic_halt();
}

void panic_memory(const char* message, uint32_t mmap_entries,
                  uint32_t total_memory, uint32_t free_memory,
                  uint32_t free_pages) {
    panic_draw_header();
    video_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    video_print("\nFalha durante a inicializacao da memoria:\n\n", 0x4F);
    video_set_color(VGA_COLOR_YELLOW, VGA_COLOR_RED);
    video_print(message ? message : "Detalhe de memoria ausente", 0x4E);
    video_print("\n\nDiagnostico:\n", 0x4F);
    panic_print_metric("  Entradas E820: ", mmap_entries);
    panic_print_metric("  Memoria total (bytes): ", total_memory);
    panic_print_metric("  Memoria livre (bytes): ", free_memory);
    panic_print_metric("  Paginas livres: ", free_pages);
    video_print("\nVerifique o mapa E820 e o bitmap PMM.\n", 0x4F);
    video_print("O sistema foi interrompido.\n", 0x4F);

    panic_halt();
}

void panic_halt(void) {
    asm volatile("cli");
    for (;;) {
        asm volatile("hlt");
    }
}
