#include "recovery_menu.h"

#define RECOVERY_CONSOLE_COLUMNS 80U
#define RECOVERY_CONSOLE_ROWS 25U
#define RECOVERY_CONSOLE_CELL_WIDTH 12U
#define RECOVERY_CONSOLE_CELL_HEIGHT 16U
#define RECOVERY_CONSOLE_VGA ((volatile uint16_t*)0xB8000U)
#define RECOVERY_CONSOLE_VGA_ATTRIBUTE 0x0F00U
#define RECOVERY_CONSOLE_MIN_WIDTH 960U
#define RECOVERY_CONSOLE_MIN_HEIGHT 400U
#define RECOVERY_CONSOLE_MAX_FRAMEBUFFER_SIZE 0x01000000U
#define RECOVERY_CONSOLE_MIN_FRAMEBUFFER 0x01000000U
#define RECOVERY_VESA_FRAMEBUFFER_OFFSET 0U
#define RECOVERY_VESA_PITCH_OFFSET 4U
#define RECOVERY_VESA_WIDTH_OFFSET 6U
#define RECOVERY_VESA_HEIGHT_OFFSET 8U
#define RECOVERY_VESA_BPP_OFFSET 10U
#define RECOVERY_VESA_AVAILABLE_OFFSET 11U
#define RECOVERY_F8_TICKS 37U
#define RECOVERY_FAILURE_TICKS 183U
#define RECOVERY_WAIT_FOREVER 0xFFFFFFFFU
#define RECOVERY_KEY_SCAN_F8 0x42U
#define RECOVERY_KEY_SCAN_UP 0x48U
#define RECOVERY_KEY_SCAN_DOWN 0x50U
#define RECOVERY_KEY_SCAN_ENTER 0x1CU
#define RECOVERY_KEY_SCAN_ESCAPE 0x01U
#define RECOVERY_MENU_MAX_ACTIONS 4U

static const uint8_t recovery_letters[26][5] = {
    {0x1EU,0x05U,0x05U,0x1EU,0x00U},{0x1FU,0x15U,0x15U,0x0AU,0x00U},
    {0x0EU,0x11U,0x11U,0x0AU,0x00U},{0x1FU,0x11U,0x11U,0x0EU,0x00U},
    {0x1FU,0x15U,0x15U,0x11U,0x00U},{0x1FU,0x05U,0x05U,0x01U,0x00U},
    {0x0EU,0x11U,0x15U,0x1DU,0x00U},{0x1FU,0x04U,0x04U,0x1FU,0x00U},
    {0x11U,0x1FU,0x11U,0x00U,0x00U},{0x08U,0x10U,0x10U,0x0FU,0x00U},
    {0x1FU,0x04U,0x0AU,0x11U,0x00U},{0x1FU,0x10U,0x10U,0x10U,0x00U},
    {0x1FU,0x02U,0x04U,0x02U,0x1FU},{0x1FU,0x02U,0x04U,0x1FU,0x00U},
    {0x0EU,0x11U,0x11U,0x0EU,0x00U},{0x1FU,0x05U,0x05U,0x02U,0x00U},
    {0x0EU,0x11U,0x19U,0x1EU,0x00U},{0x1FU,0x05U,0x0DU,0x12U,0x00U},
    {0x12U,0x15U,0x15U,0x09U,0x00U},{0x01U,0x1FU,0x01U,0x00U,0x00U},
    {0x0FU,0x10U,0x10U,0x0FU,0x00U},{0x07U,0x08U,0x10U,0x08U,0x07U},
    {0x1FU,0x08U,0x04U,0x08U,0x1FU},{0x1BU,0x04U,0x04U,0x1BU,0x00U},
    {0x03U,0x04U,0x18U,0x04U,0x03U},{0x19U,0x15U,0x13U,0x00U,0x00U}
};

static const uint8_t recovery_digits[10][5] = {
    {0x0EU,0x11U,0x11U,0x0EU,0x00U},{0x12U,0x1FU,0x10U,0x00U,0x00U},
    {0x19U,0x15U,0x15U,0x12U,0x00U},{0x11U,0x15U,0x15U,0x0AU,0x00U},
    {0x07U,0x04U,0x04U,0x1FU,0x00U},{0x17U,0x15U,0x15U,0x09U,0x00U},
    {0x0EU,0x15U,0x15U,0x08U,0x00U},{0x01U,0x01U,0x1DU,0x03U,0x00U},
    {0x0AU,0x15U,0x15U,0x0AU,0x00U},{0x02U,0x15U,0x15U,0x0EU,0x00U}
};

static const uint8_t recovery_symbol_colon[5] = {0x00U,0x0AU,0x00U,0x00U,0x00U};
static const uint8_t recovery_symbol_equal[5] = {0x0AU,0x0AU,0x0AU,0x00U,0x00U};
static const uint8_t recovery_symbol_dash[5] = {0x04U,0x04U,0x04U,0x00U,0x00U};
static const uint8_t recovery_symbol_arrow[5] = {0x04U,0x0EU,0x1FU,0x00U,0x00U};
static const uint8_t recovery_symbol_slash[5] = {0x18U,0x04U,0x03U,0x00U,0x00U};
static const uint8_t recovery_symbol_dot[5] = {0x00U,0x10U,0x00U,0x00U,0x00U};
static const uint8_t recovery_symbol_underscore[5] = {0x10U,0x10U,0x10U,0x10U,0x10U};

static const uint8_t* recovery_console_vesa;
static uint32_t recovery_console_cursor;

extern uint16_t recovery_bios_wait_key(uint32_t timeout_ticks);

static uint16_t recovery_console_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8U);
}

static uint32_t recovery_console_u32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}

static const uint8_t* recovery_console_glyph(char value) {
    if (value >= 'A' && value <= 'Z') return recovery_letters[value - 'A'];
    if (value >= '0' && value <= '9') return recovery_digits[value - '0'];
    if (value == ':') return recovery_symbol_colon;
    if (value == '=') return recovery_symbol_equal;
    if (value == '-') return recovery_symbol_dash;
    if (value == '>') return recovery_symbol_arrow;
    if (value == '/') return recovery_symbol_slash;
    if (value == '.') return recovery_symbol_dot;
    if (value == '_') return recovery_symbol_underscore;
    return 0;
}

static int recovery_console_vesa_ready(void) {
    uint32_t framebuffer;
    uint32_t size;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t bytes_per_pixel;
    if (!recovery_console_vesa ||
        !recovery_console_vesa[RECOVERY_VESA_AVAILABLE_OFFSET])
        return 0;
    framebuffer = recovery_console_u32(
        recovery_console_vesa + RECOVERY_VESA_FRAMEBUFFER_OFFSET);
    pitch = recovery_console_u16(
        recovery_console_vesa + RECOVERY_VESA_PITCH_OFFSET);
    width = recovery_console_u16(
        recovery_console_vesa + RECOVERY_VESA_WIDTH_OFFSET);
    height = recovery_console_u16(
        recovery_console_vesa + RECOVERY_VESA_HEIGHT_OFFSET);
    bpp = recovery_console_vesa[RECOVERY_VESA_BPP_OFFSET];
    if (bpp != 24U && bpp != 32U) return 0;
    bytes_per_pixel = bpp / 8U;
    if (framebuffer < RECOVERY_CONSOLE_MIN_FRAMEBUFFER ||
        width < RECOVERY_CONSOLE_MIN_WIDTH ||
        height < RECOVERY_CONSOLE_MIN_HEIGHT ||
        width > pitch / bytes_per_pixel)
        return 0;
    size = (uint32_t)pitch * height;
    return size && size <= RECOVERY_CONSOLE_MAX_FRAMEBUFFER_SIZE &&
           framebuffer <= 0xFFFFFFFFU - size;
}

static void recovery_console_pixel(uint32_t framebuffer, uint16_t pitch,
                                   uint8_t bpp, uint32_t x, uint32_t y) {
    uint8_t* pixel = (uint8_t*)(framebuffer + y * pitch + x * (bpp / 8U));
    pixel[0] = 0xFFU;
    pixel[1] = 0xFFU;
    pixel[2] = 0xFFU;
    if (bpp == 32U) pixel[3] = 0U;
}

static void recovery_console_draw_glyph(uint32_t x, uint32_t y,
                                        const uint8_t* glyph) {
    uint32_t framebuffer = recovery_console_u32(
        recovery_console_vesa + RECOVERY_VESA_FRAMEBUFFER_OFFSET);
    uint16_t pitch = recovery_console_u16(
        recovery_console_vesa + RECOVERY_VESA_PITCH_OFFSET);
    uint8_t bpp = recovery_console_vesa[RECOVERY_VESA_BPP_OFFSET];
    for (uint32_t column = 0U; column < 5U; column++) {
        for (uint32_t row = 0U; row < 5U; row++) {
            uint32_t pixel_x;
            uint32_t pixel_y;
            if (!(glyph[column] & (1U << row))) continue;
            pixel_x = x + column * 2U;
            pixel_y = y + row * 2U;
            recovery_console_pixel(framebuffer, pitch, bpp, pixel_x, pixel_y);
            recovery_console_pixel(framebuffer, pitch, bpp, pixel_x + 1U, pixel_y);
            recovery_console_pixel(framebuffer, pitch, bpp, pixel_x, pixel_y + 1U);
            recovery_console_pixel(framebuffer, pitch, bpp, pixel_x + 1U, pixel_y + 1U);
        }
    }
}

static void recovery_console_put(char value) {
    uint32_t column;
    uint32_t row;
    const uint8_t* glyph;
    if (value == '\n') {
        recovery_console_cursor =
            ((recovery_console_cursor / RECOVERY_CONSOLE_COLUMNS) + 1U) *
            RECOVERY_CONSOLE_COLUMNS;
        return;
    }
    if (recovery_console_cursor >= RECOVERY_CONSOLE_COLUMNS * RECOVERY_CONSOLE_ROWS) return;
    column = recovery_console_cursor % RECOVERY_CONSOLE_COLUMNS;
    row = recovery_console_cursor / RECOVERY_CONSOLE_COLUMNS;
    RECOVERY_CONSOLE_VGA[recovery_console_cursor] =
        RECOVERY_CONSOLE_VGA_ATTRIBUTE | (uint8_t)value;
    glyph = recovery_console_glyph(value);
    if (glyph && recovery_console_vesa_ready()) {
        recovery_console_draw_glyph(column * RECOVERY_CONSOLE_CELL_WIDTH,
                                    row * RECOVERY_CONSOLE_CELL_HEIGHT, glyph);
    }
    recovery_console_cursor++;
}

void recovery_console_init(uint8_t* vesa) {
    recovery_console_vesa = vesa;
    recovery_console_cursor = 0U;
    if (vesa && !recovery_console_vesa_ready())
        vesa[RECOVERY_VESA_AVAILABLE_OFFSET] = 0U;
}

void recovery_console_clear(void) {
    for (uint32_t index = 0U;
         index < RECOVERY_CONSOLE_COLUMNS * RECOVERY_CONSOLE_ROWS; index++) {
        RECOVERY_CONSOLE_VGA[index] = 0x0720U;
    }
    if (recovery_console_vesa_ready()) {
        uint32_t framebuffer = recovery_console_u32(
            recovery_console_vesa + RECOVERY_VESA_FRAMEBUFFER_OFFSET);
        uint16_t pitch = recovery_console_u16(
            recovery_console_vesa + RECOVERY_VESA_PITCH_OFFSET);
        uint16_t height = recovery_console_u16(
            recovery_console_vesa + RECOVERY_VESA_HEIGHT_OFFSET);
        uint8_t* output = (uint8_t*)framebuffer;
        uint32_t size = (uint32_t)pitch * height;
        for (uint32_t index = 0U; index < size; index++) output[index] = 0U;
    }
    recovery_console_cursor = 0U;
}

void recovery_console_print(const char* message) {
    if (!message) return;
    while (*message) recovery_console_put(*message++);
}

void recovery_console_print_u32(uint32_t value) {
    char digits[10];
    uint32_t size = 0U;
    if (!value) {
        recovery_console_put('0');
        return;
    }
    while (value && size < sizeof(digits)) {
        digits[size++] = (char)('0' + value % 10U);
        value /= 10U;
    }
    while (size) recovery_console_put(digits[--size]);
}

static void recovery_menu_print_version(uint16_t major, uint16_t minor,
                                        uint16_t patch) {
    recovery_console_print_u32(major);
    recovery_console_print(".");
    recovery_console_print_u32(minor);
    recovery_console_print(".");
    recovery_console_print_u32(patch);
}

static void recovery_menu_render_state(const recovery_menu_view_t* view) {
    recovery_console_print("ZEPHYROS RECOVERY\n\n");
    recovery_console_print("DIAGNOSTICO: ");
    recovery_console_print(view->diagnostic);
    recovery_console_print("\nSEQUENCIA: ");
    recovery_console_print_u32(view->sequence);
    recovery_console_print("\nATIVO: ");
    recovery_console_print(view->active);
    recovery_console_print("  PENDENTE: ");
    recovery_console_print(view->pending);
    recovery_console_print("\nANTERIOR: ");
    recovery_console_print(view->previous);
    recovery_console_print("  TENTATIVA: ");
    recovery_console_print(view->attempt);
    recovery_console_print("\nBOOT: ");
    recovery_console_print(view->boot_state);
    recovery_console_print("  MOTIVO: ");
    recovery_console_print(view->reason);
    recovery_console_print("\nTENTATIVA SEQ: ");
    recovery_console_print_u32(view->attempt_sequence);
    recovery_console_print("\nSLOT A: ");
    recovery_console_print(view->slot_a_state);
    if (view->slot_a_version_available) {
        recovery_console_print(" ");
        recovery_menu_print_version(view->slot_a_major, view->slot_a_minor,
                                    view->slot_a_patch);
    }
    recovery_console_print("\nSLOT B: ");
    recovery_console_print(view->slot_b_state);
    if (view->slot_b_version_available) {
        recovery_console_print(" ");
        recovery_menu_print_version(view->slot_b_major, view->slot_b_minor,
                                    view->slot_b_patch);
    }
    recovery_console_print("\n\n");
}

static const char* recovery_menu_action_name(recovery_menu_action_t action) {
    if (action == RECOVERY_MENU_ACTION_CONTINUE) return "CONTINUAR BOOT";
    if (action == RECOVERY_MENU_ACTION_PREVIOUS) return "INICIAR ANTERIOR";
    if (action == RECOVERY_MENU_ACTION_RETRY) return "TENTAR CANDIDATO";
    return "KERNEL LEGADO";
}

static uint32_t recovery_menu_actions(const recovery_menu_view_t* view,
                                      recovery_menu_action_t
                                          actions[RECOVERY_MENU_MAX_ACTIONS]) {
    uint32_t count = 0U;
    if (view->allow_continue) actions[count++] = RECOVERY_MENU_ACTION_CONTINUE;
    if (view->allow_previous) actions[count++] = RECOVERY_MENU_ACTION_PREVIOUS;
    if (view->allow_retry) actions[count++] = RECOVERY_MENU_ACTION_RETRY;
    actions[count++] = RECOVERY_MENU_ACTION_LEGACY;
    return count;
}

static void recovery_menu_render(const recovery_menu_view_t* view,
                                 const recovery_menu_action_t* actions,
                                 uint32_t count, uint32_t selected) {
    recovery_console_clear();
    recovery_menu_render_state(view);
    for (uint32_t index = 0U; index < count; index++) {
        recovery_console_print(index == selected ? "> " : "  ");
        recovery_console_print(recovery_menu_action_name(actions[index]));
        recovery_console_print("\n");
    }
    recovery_console_print("\nSETAS ENTER ESC");
    if (view->failure_menu) recovery_console_print("  AUTO 10S");
}

int recovery_menu_wait_f8(void) {
    uint16_t key;
    recovery_console_clear();
    recovery_console_print("ZEPHYROS RECOVERY\n\nPRESSIONE F8 PARA MENU\n");
    key = recovery_bios_wait_key(RECOVERY_F8_TICKS);
    return (uint8_t)(key >> 8U) == RECOVERY_KEY_SCAN_F8;
}

recovery_menu_action_t recovery_menu_run(const recovery_menu_view_t* view) {
    recovery_menu_action_t actions[RECOVERY_MENU_MAX_ACTIONS];
    uint32_t count;
    uint32_t selected = 0U;
    uint32_t timeout;
    if (!view) return RECOVERY_MENU_ACTION_LEGACY;
    count = recovery_menu_actions(view, actions);
    timeout = view->failure_menu ? RECOVERY_FAILURE_TICKS : RECOVERY_WAIT_FOREVER;
    for (;;) {
        uint16_t key;
        uint8_t scan;
        recovery_menu_render(view, actions, count, selected);
        key = recovery_bios_wait_key(timeout);
        if (!key) {
            return view->allow_previous ? RECOVERY_MENU_ACTION_PREVIOUS_DEFAULT :
                                          RECOVERY_MENU_ACTION_LEGACY;
        }
        scan = (uint8_t)(key >> 8U);
        if (scan == RECOVERY_KEY_SCAN_UP) {
            selected = selected ? selected - 1U : count - 1U;
        } else if (scan == RECOVERY_KEY_SCAN_DOWN) {
            selected = selected + 1U == count ? 0U : selected + 1U;
        } else if (scan == RECOVERY_KEY_SCAN_ENTER) {
            return actions[selected];
        } else if (scan == RECOVERY_KEY_SCAN_ESCAPE) {
            if (!view->failure_menu && view->allow_continue)
                return RECOVERY_MENU_ACTION_CONTINUE;
            return view->allow_previous ? RECOVERY_MENU_ACTION_PREVIOUS_DEFAULT :
                                          RECOVERY_MENU_ACTION_LEGACY;
        }
    }
}

int recovery_menu_confirm_retry(const recovery_menu_view_t* view) {
    if (!view) return 0;
    recovery_console_clear();
    recovery_console_print("CONFIRMAR TENTATIVA\n\nCANDIDATO: ");
    recovery_console_print(view->attempt);
    recovery_console_print("\nSEQUENCIA: ");
    recovery_console_print_u32(view->attempt_sequence + 1U);
    recovery_console_print("\n\nENTER CONFIRMA\nESC VOLTA\n");
    for (;;) {
        uint16_t key = recovery_bios_wait_key(RECOVERY_WAIT_FOREVER);
        uint8_t scan = (uint8_t)(key >> 8U);
        if (scan == RECOVERY_KEY_SCAN_ENTER) return 1;
        if (scan == RECOVERY_KEY_SCAN_ESCAPE) return 0;
    }
}
