# 12 - Desktop e Interface

Ambiente desktop do MiniOS com gerenciador de janelas, barra de tarefas, configurações e ícones.

## Arquivos

```
src/desktop/desktop.c    → Ambiente desktop com ícones
src/wm/wm.c              → Gerenciador de janelas
src/taskbar/taskbar.c    → Barra de tarefas com menu Iniciar
src/settings/settings.c  → Sistema de configurações
src/icons/icons.c        → Registro de ícones customizáveis
```

---

## Desktop (`desktop.c`)

### Inicialização

```c
desktop_init();
```

Cria 3 ícones padrão: Shell, Explorer e TaskMgr.

### Layout

Os ícones são organizados em grade (5 colunas):

```
┌─────────────────────────────────────────────────────────────┐
│                    MiniOS Desktop                           │
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

### API

```c
void desktop_init(void);
void desktop_draw(void);
void desktop_draw_icons(void);
void desktop_add_icon(name, type);         // Adiciona ícone à grade
void desktop_update_selection(void);
int  desktop_handle_key(scancode);         // Processa teclas
int  desktop_get_selected_app(void);       // Retorna app selecionado
void desktop_set_active(active);
```

### Navegação

| Tecla | Ação |
|-------|------|
| Setas | Navega entre ícones |
| Enter | Abre aplicativo selecionado |
| Esc | Sai do desktop |

---

## Window Manager (`wm.c`)

### Inicialização

```c
wm_init();
```

Configuração padrão: botões à direita, ordem Fechar-Minimizar-Maximizar, título visível.

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

### Gerenciamento

```c
int  wm_create_window(title, x, y, w, h, type, on_key, on_redraw);
void wm_destroy_window(id);
void wm_focus_window(id);
void wm_focus_next(void);
void wm_focus_prev(void);
void wm_minimize_window(id);
void wm_maximize_window(id);
void wm_restore_window(id);
void wm_move_window(id, x, y);
void wm_resize_window(id, w, h);
```

### Barra de Título

A barra de título desenha:

1. Botões (fechar `x`, minimizar `_`, maximizar `↑`)
2. Título da janela (alinhado conforme posição dos botões)

### Configurações

```c
typedef struct {
    wm_btn_position_t btn_position;  // LEFT ou RIGHT
    wm_btn_order_t btn_order;        // Ordem dos 3 botões (6 variações)
    int show_title_text;             // Mostrar/esconder título
    int title_bar_height;
    int border_style;                // 0=simples, 1=dupla
} wm_config_t;
```

### Redesenho

```c
void wm_draw_desktop(void);   // Fundo xadrez
void wm_draw_all(void);       // Redesenha desktop + todas janelas
```

### Atalhos

| Tecla | Ação |
|-------|------|
| Tab | Foco próxima janela |
| Esc | Fechar janela focada |
| F5 | Minimizar janela |
| F6 | Maximizar/Restaurar |

---

## Taskbar (`taskbar.c`)

### Inicialização

```c
taskbar_init();
```

Posição padrão: inferior. Adiciona botão "Shell".

### Botões

```c
void taskbar_add_app(type, name);     // Adiciona botão de app
void taskbar_remove_app(type);        // Remove botão de app
```

### Menu Iniciar

Aberto com Alt (scancode 0x38):

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

### Menu de Configuração

Aberto com F1 (scancode 0x3B):

```
┌────────────────────────────────────────┐
│         Configuracoes da Taskbar        │
├────────────────────────────────────────┤
│ Posicao: Baixo                         │
│ Tamanho: Medio                         │
│ Fixado:  Sim                           │
│ Mover p/ Topo                          │
│ Mover p/ Baixo                         │
│ Mover p/ Esquerda                      │
│ Mover p/ Direita                       │
│ Posicao Custom...                      │
├────────────────────────────────────────┤
│ Esc: Fechar | Enter: Alterar           │
└────────────────────────────────────────┘
```

### Relógio

Exibe tempo decorrido desde o boot (HH:MM) no canto direito da barra. Atualizado a cada segundo (50 ticks).

### Configuração

```c
typedef struct {
    tb_position_t position;     // BOTTOM, TOP, LEFT, RIGHT, CUSTOM
    tb_icon_size_t icon_size;  // SMALL, MEDIUM, LARGE
    int pinned;
    int custom_x, custom_y;
    int width, height;
} tb_config_t;
```

### Posições

| Constante | Descrição |
|-----------|-----------|
| TB_POS_BOTTOM | Barra horizontal no rodapé |
| TB_POS_TOP | Barra horizontal no topo |
| TB_POS_LEFT | Barra vertical à esquerda |
| TB_POS_RIGHT | Barra vertical à direita |
| TB_POS_CUSTOM | Posição livre (x, y customizáveis) |

---

## Settings (`settings.c`)

### Inicialização

```c
settings_init();
```

### Categorias

| Categoria | Opções |
|-----------|--------|
| Tela | Tema (Clássico/Escuro/Azul), Resolução, Grade |
| Barra de Tarefas | Posição, Tamanho ícone, Fixada, Relógio, Auto-ocultar |
| Janelas | Botões lado, Ordem botões, Título, Borda |
| Ícones | Editor visual (Desktop, WM, Arquivos) |
| Sistema | Nome PC, Info memória, Processos, Reiniciar |
| Som | Volume (Mudo~Máximo), Beep iniciar, Som teclado |
| Sobre | Versão, Créditos |

### Navegação

| Tecla | Ação |
|-------|------|
| Tab | Alterna categoria |
| Up/Down | Navega opções |
| Enter | Altera/Aciona opção |
| Left/Right | Altera valor (listas) |
| Esc | Fecha configurações |

### Aplicação em Tempo Real

Alterações em opções de Taskbar e Janelas são aplicadas imediatamente:

```c
apply_taskbar_settings();  // Seta posição, tamanho, fixação
apply_wm_settings();       // Seta botões, título, borda
```

---

## Icons (`icons.c`)

### Registro

```c
typedef struct {
    icon_entry_t desktop[ICON_DESKTOP_COUNT];  // 3 ícones
    icon_entry_t wm[ICON_WM_COUNT];           // 3 ícones
    icon_entry_t fm[ICON_FM_COUNT];           // 2 ícones
    icon_entry_t tb[ICON_TB_COUNT];           // 1 ícone
} icon_registry_t;
```

### Ícones Padrão

| Categoria | ID | Char | Cor | Cor Sel |
|-----------|-----|------|-----|---------|
| Desktop | Shell | 'S' | 0x0F | 0x17 |
| Desktop | Explorer | 'E' | 0x0F | 0x17 |
| Desktop | TaskMgr | 'T' | 0x0F | 0x17 |
| WM | Close | 'x' | 0x4F | 0x4F |
| WM | Minimize | '_' | 0x1F | 0x08 |
| WM | Maximize | '↑' | 0x1F | 0x08 |
| FM | Folder | '[' | 0x0B | 0x70 |
| FM | File | '-' | 0x08 | 0x70 |
| TB | Start | '>' | 0x0F | 0x1F |

### API

```c
icon_registry_t* icons_get_registry(void);       // Registro completo
icon_entry_t* icons_get_desktop(id);             // Ícone desktop
icon_entry_t* icons_get_wm(id);                  // Ícone janela
icon_entry_t* icons_get_fm(id);                  // Ícone file manager
icon_entry_t* icons_get_tb(id);                  // Ícone taskbar

void icons_set_desktop(id, ch, color, color_sel);
void icons_set_wm(id, ch, color, color_sel);
void icons_set_fm(id, ch, color, color_sel);
void icons_set_tb(id, ch, color, color_sel);

void icons_reset_defaults(void);                 // Restaura padrão
```
