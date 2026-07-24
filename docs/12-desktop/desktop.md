# 12 - Desktop e Interface

Ambiente visual do ZephyrOS suportando uma arquitetura **Dual Interface**: Classic TUI (Text User Interface) e GUI Moderna (Graphical User Interface baseada em VESA e primitivas 2D).

## Arquivos

```
src/desktop/desktop.c    → Ambiente desktop com ícones (TUI e GUI)
src/wm/wm.c              → Gerenciador de janelas (Dual interface frames)
src/taskbar/taskbar.c    → Barra de tarefas e menu Iniciar (Dual interface)
src/settings/settings.c  → Sistema de configurações
src/icons/icons.c        → Registro de ícones customizáveis
src/gui/gui.c            → Primitivas gráficas 2D para a GUI Moderna
```

---

## Dual Interface (Classic TUI vs GUI Moderna)

O sistema operacional implementa uma estratégia de retrocompatibilidade visual (regra `#15` do `AGENTS.md`). Isso significa que a interface moderna não substitui o modo clássico, mas coexiste como uma camada renderizável alternável. 

- **Classic TUI**: Usa `video.c` (memória VGA) ou desenho alinhado em grid para exibir a interface de maneira retro e otimizada.
- **GUI Moderna**: Usa `gui.c` para desenhar painéis cinza, bordas 3D, barra de título azul, seleção azul e texto fora do grid com a fonte bitmap existente.
- **Alternância Dinâmica**: O comando `guimode classic|modern` permite alterar a engine visual em tempo de execução sem desligar os aplicativos rodando.

---

## Desktop (`desktop.c`)

### Inicialização

```c
desktop_init();
```

Cria 3 ícones padrão: Shell, Explorer e TaskMgr.

### Renderização (Desktop Gráfico)

No modo `GUI Moderna`, o desktop desenha fundo, cards de ícone e símbolos por
primitivas, permitindo:
- Seleção visual azul em vez de caractere invertido.
- Grade responsiva alinhada à esquerda, calculada conforme a resolução VESA.
- Clique simples para seleção e duplo clique para abrir.

Ícones BMP e arrastar ícones continuam planejados; não fazem parte do modo
moderno atual.

No modo Clássico, os ícones são organizados em grade (5 colunas) e mostrados na VGA Text Mode/Grid:

```
┌─────────────────────────────────────────────────────────────┐
│                    ZephyrOS Desktop                           │
│                                                             │
│  [Shell]    [Explorer]  [TaskMgr]                           │
│                                                             │
│                                                             │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ [Inicio]  [Shell]  [Explorer]              HH:MM     │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### API Principal

```c
void desktop_init(void);
void desktop_draw(void);                   // Direciona para TUI ou GUI dependendo do modo
void desktop_add_icon(name, type);         // Adiciona ícone
void desktop_update_selection(void);
int  desktop_handle_key(scancode);         // Processa teclas
int  desktop_get_selected_app(void);       // Retorna app selecionado
void desktop_set_active(active);
```

### Navegação (Mouse e Teclado)

| Input | Ação |
|-------|------|
| Setas | Navega entre ícones |
| Enter / Duplo Clique | Abre aplicativo selecionado |
| Clique Único | Seleciona o ícone |
| Esc | Sai do desktop |

---

## Primitivas Gráficas 2D (`gui.c`)

Motor gráfico da GUI Moderna, permitindo renderização independente da grade TUI clássica.

### Funções Base

```c
void gui_draw_text(int x, int y, const char* text, uint32_t color);
void gui_draw_button(int x, int y, int w, int h, const char* label, int pressed);
void gui_draw_window_frame(int x, int y, int w, int h, const char* title, int active);
```

As cenas modernas usam backbuffer e um único ciclo de frame. As primitivas não
implementam gradientes, transparência ou cantos arredondados nesta etapa.

---

## Window Manager (`wm.c`)

O Window Manager geral continua textual. O Task Manager gráfico possui uma
janela própria e usa `gui_draw_window_frame()`, mas ainda não está integrado a
um Window Manager gráfico genérico.

### Estado de renderização

- **Modo TUI**: janelas do WM usam bordas de caracteres.
- **Modo moderno**: Desktop, Explorer, Settings e Task Manager têm caminhos
  gráficos próprios; a migração do WM continua planejada.

### Estrutura de Janela

```c
typedef struct {
    int id;
    char title[32];
    int x, y, width, height;
    int min_width, min_height;
    int state;           // NORMAL, MINIMIZED, MAXIMIZED
    int visible, focused;
    int z_order;
    wm_app_type_t app_type;
    wm_key_handler_t on_key;
    wm_redraw_handler_t on_redraw;
    uint32_t cpu_ticks, last_cpu_sample;
} wm_window_t;
```

### Gerenciamento Base

```c
int  wm_create_window(title, x, y, w, h, type, on_key, on_redraw);
void wm_destroy_window(id);
void wm_focus_window(id);
void wm_focus_next(void);
void wm_focus_prev(void);
void wm_move_window(id, x, y);
void wm_resize_window(id, w, h);
```

### Integração com o Mouse

Taskbar e Menu Iniciar têm prioridade de clique. A janela gráfica do Task
Manager suporta foco, minimizar, maximizar e arraste pela barra de título.
Arraste genérico de janelas no WM e drag-and-drop de ícones permanecem fora do
escopo atual.

---

## Taskbar (`taskbar.c`)

A taskbar preserva a identidade visual existente e funciona nos dois modos de
interface. Ela desenha menu, botões e relógio e mantém prioridade sobre as
interfaces abertas.

### Próxima evolução visual

O redesenho completo da taskbar como componente gráfico independente ainda é
uma etapa futura. Até lá, a compatibilidade visual e de interação tem
prioridade sobre uma troca de tema.

### Menu Iniciar

```
┌─────────────────┐
│ Desktop          │
│ Shell            │
│ Explorer         │
│ Task Manager     │
│ Configuracoes    │
│ Reiniciar        │
│ Desligar         │
└─────────────────┘
```

### Configuração Dinâmica

```c
typedef struct {
    tb_position_t position;     // BOTTOM, TOP, LEFT, RIGHT, CUSTOM
    tb_icon_size_t icon_size;  // SMALL, MEDIUM, LARGE
    int pinned;
    int custom_x, custom_y;
    int width, height;
} tb_config_t;
```

---

## Settings (`settings.c`)

Sistema de configuração geral.

### Categorias

| Categoria | Opções |
|-----------|--------|
| Tela | Tema (Clássico/Moderna), Resolução |
| Barra de Tarefas | Posição, Tamanho ícone, Fixada, Relógio |
| Janelas | Botões lado, Ordem botões, Título, Borda |
| Ícones | Editor visual (Desktop, WM, Arquivos) |
| Sistema | Nome PC, Info memória, Processos, Reiniciar |
| Som | Volume, Beep iniciar, Som teclado |
| Sobre | Versão, Créditos |

---

## Icons (`icons.c`)

Permite a customização dinâmica de ícones (tanto de caracteres TUI quanto de fallback de cor).

```c
typedef struct {
    icon_entry_t desktop[ICON_DESKTOP_COUNT];
    icon_entry_t wm[ICON_WM_COUNT];
    icon_entry_t fm[ICON_FM_COUNT];
    icon_entry_t tb[ICON_TB_COUNT];
} icon_registry_t;
```

### API

```c
icon_registry_t* icons_get_registry(void);
icon_entry_t* icons_get_desktop(id);
void icons_reset_defaults(void);
```
