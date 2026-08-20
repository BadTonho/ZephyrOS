#include "apps/shell.h"
#include "apps/shell_input.h"
#include "apps/shell_runtime.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/video.h"
#include "ui/desktop.h"
#include "ui/wm.h"

#define SHELL_HOSTED_DEFAULT_CONTENT_WIDTH 880
#define SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT 560
#define SHELL_HOSTED_FRAME_WIDTH 4
#define SHELL_HOSTED_FRAME_HEIGHT 28

static uint8_t shell_hosted_visible = 0;

static void shell_hosted_draw(int x, int y, int width, int height);
static void shell_hosted_key(uint8_t scancode);
static int shell_hosted_mouse(mouse_event_t* event, int x, int y,
                              int width, int height);
static void shell_hosted_close(void);

static const wm_hosted_app_t shell_hosted_app = {
    WM_APP_SHELL, "ZephyrOS Shell", "Shell",
    WM_HOSTED_MIN_WIDTH, WM_HOSTED_MIN_HEIGHT,
    SHELL_HOSTED_DEFAULT_CONTENT_WIDTH + SHELL_HOSTED_FRAME_WIDTH,
    SHELL_HOSTED_DEFAULT_CONTENT_HEIGHT + SHELL_HOSTED_FRAME_HEIGHT,
    WM_KEY_REDRAW_APPLICATION,
    shell_hosted_draw, shell_hosted_key, shell_hosted_mouse, shell_hosted_close
};

void shell_hosted_reset(void) {
    shell_hosted_visible = 0;
}

int shell_runtime_is_hosted_visible(void) {
    return shell_hosted_visible && wm_is_active() &&
           desktop_get_mode() == DESKTOP_MODE_CLASSIC &&
           video_terminal_is_hosted();
}

static void shell_hosted_draw(int x, int y, int width, int height) {
    if (video_terminal_draw(x, y, width, height) != OK) {
        LOG_WARN("SHELL", "Falha ao desenhar terminal hospedado");
    }
}

static void shell_hosted_key(uint8_t scancode) {
    shell_runtime_handle_terminal_key(scancode);
    shell_hosted_present_progress();
}

void shell_hosted_present_progress(void) {
    if (!shell_runtime_is_hosted_visible() ||
        !wm_is_hosted_app_focused(WM_APP_SHELL)) return;
    if (video_terminal_present_hosted_dirty() != OK) {
        LOG_WARN("SHELL", "Falha ao apresentar entrada do terminal hospedado");
    }
}

static int shell_hosted_mouse(mouse_event_t* event, int x, int y,
                              int width, int height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;

    if (!event) {
        LOG_ERROR("SHELL", "Evento de mouse nulo no terminal hospedado");
        return 0;
    }
    if (!shell_handle_mouse(event)) return 0;
    /* O WM recompõe o mesmo frame ao terminar este callback. */
    (void)video_terminal_take_hosted_dirty();
    return 1;
}

static void shell_hosted_close(void) {
    shell_hosted_visible = 0;
    shell_input_reset_modifiers();
}

int shell_hosted_open(void) {
    int result;

    if (desktop_get_mode() != DESKTOP_MODE_CLASSIC) {
        LOG_WARN("SHELL", "Shell hospedado requer modo Classic");
        return ERR_UNAVAILABLE;
    }
    wm_set_active(1);
    if (shell_hosted_visible) return wm_register_hosted_app(&shell_hosted_app);

    video_terminal_set_hosted(1);
    shell_hosted_visible = 1;
    result = wm_register_hosted_app(&shell_hosted_app);
    if (result != OK) {
        shell_hosted_visible = 0;
        video_terminal_set_hosted(0);
        wm_set_active(0);
        desktop_set_active(0);
        LOG_WARN("SHELL", "Workspace nao comporta o Shell hospedado");
        return result;
    }
    shell_diagnostics_print_usb_fixture_report();
    shell_print_prompt();
    return OK;
}
