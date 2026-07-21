#include "apps/guitest.h"
#include "ui/gui.h"
#include "core/log.h"
#include "core/video.h"
#include "drivers/speaker.h"
#include "ui/taskbar.h"
#include "core/recovery.h"

static int guitest_active = 0;
static int btn_pressed = 0;

// Botão "Aperte-me"
#define BTN_X 250
#define BTN_Y 200
#define BTN_W 120
#define BTN_H 30

void guitest_open(void) {
    if (!recovery_is_enabled(RECOVERY_COMPONENT_GUITEST) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("GUITEST", "GUI Test requer VESA; abertura ignorada");
        return;
    }

    if (guitest_active) return;
    guitest_active = 1;
    btn_pressed = 0;
    
    // O fundo inteiro precisa ser limpo para modo gráfico
    // O modo atual ainda não tem double buffer, então limpamos com o video padrão para não quebrar.
    video_clear(); 
    taskbar_draw();

    guitest_draw();
    LOG_INFO("GUITEST", "App aberto");
}

void guitest_close(void) {
    if (!guitest_active) return;
    guitest_active = 0;
    
    video_clear();
    taskbar_draw();
    
    LOG_INFO("GUITEST", "App fechado");
}

int guitest_is_active(void) {
    return guitest_active;
}

void guitest_draw(void) {
    if (!guitest_active) return;
    if (!recovery_is_enabled(RECOVERY_COMPONENT_GUITEST) ||
        !recovery_is_enabled(RECOVERY_COMPONENT_VESA)) {
        LOG_WARN("GUITEST", "GUI Test perdeu suporte VESA; encerrando");
        guitest_active = 0;
        return;
    }

    gui_draw_window_frame(200, 150, 400, 300, "Meu Primeiro App GUI (C)", 1);
    
    // Desenha nosso botão interativo
    gui_draw_button(BTN_X, BTN_Y, BTN_W, BTN_H, "Aperte-me", btn_pressed);
    
    // Texto fixo
    gui_draw_text(250, 260, "Clique no botao acima!", GUI_COLOR_TEXT);
    gui_draw_text(250, 280, "Clique no [X] para fechar.", GUI_COLOR_TEXT);
}

static int is_inside(int px, int py, int x, int y, int w, int h) {
    return (px >= x && px <= x + w && py >= y && py <= y + h);
}

void guitest_handle_mouse(mouse_event_t* event) {
    if (!guitest_active) return;
    if (!event) {
        LOG_ERROR("GUITEST", "Evento de mouse nulo");
        return;
    }

    // Colisão do botão de fechar (X)
    // Coordenadas aproximadas baseadas no gui_draw_window_frame
    // x + w - btn_size - 4, y + 5
    int fechar_x = 200 + 400 - 16 - 4;
    int fechar_y = 150 + 3;
    
    // Clique esquerdo pressionado
    if (event->event == MOUSE_EVENT_PRESS && (event->changed & MOUSE_BTN_LEFT)) {
        if (is_inside(event->x, event->y, BTN_X, BTN_Y, BTN_W, BTN_H)) {
            if (!btn_pressed) {
                btn_pressed = 1;
                guitest_draw(); // Redesenha com botão afundado
            }
        }
    } 
    // Soltou o botão esquerdo
    else if (event->event == MOUSE_EVENT_RELEASE && (event->changed & MOUSE_BTN_LEFT)) {
        if (btn_pressed) {
            btn_pressed = 0;
            guitest_draw(); // Redesenha com botão levantado
            
            // Ação do botão:
            speaker_beep(1000, 50); // Toca um beep
        }
        
        // Verifica se soltou em cima do fechar
        if (is_inside(event->x, event->y, fechar_x, fechar_y, 16, 16)) {
            guitest_close();
        }
    }
}
