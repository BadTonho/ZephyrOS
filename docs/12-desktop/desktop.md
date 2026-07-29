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
- Grade responsiva de cartões 112×96 px com slots em memória, calculada
  conforme a resolução VESA e a área útil da taskbar.
- Clique simples para seleção, duplo clique para abrir e arraste com encaixe
  no próximo slot livre.
- Barras acopladas reduzem a grade; a taskbar personalizada também reserva
  seus slots para que nenhum cartão fique sob ela.

As posições duram somente até reiniciar. Shell, Explorer e Task Manager usam
BMPs 24 bpp de 32×32 px no modo moderno quando estiverem no cache; os arquivos
usam magenta como cor transparente. Seleção múltipla e persistência em disco
continuam fora do modo moderno atual.

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
void desktop_draw_workspace(void);         // Compõe fundo e ícones no frame VESA atual
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
| Arrastar (Moderno) | Move para o slot livre mais próximo |
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

O comando `wm` preserva o gerenciador textual no modo Clássico. No modo
Moderno, ele abre um workspace VESA vazio. Shell, Explorer, Settings, Task
Manager e System Updater são aplicativos hospedados: seus comandos e ações do
Menu Iniciar criam ou focalizam uma única janela de cada tipo.

### Estado de renderização

- **Modo TUI**: janelas do WM usam bordas de caracteres e as APIs legadas.
- **Modo moderno**: janelas hospedadas são compostas do menor para o maior
  Z-order e a taskbar é desenhada por último no mesmo ciclo VESA. Quando uma
  janela é aberta a partir do Desktop, os ícones continuam como fundo da
  composição; ao fechar a última janela, o controle retorna ao Desktop. Antes
  de desenhar cada aplicativo, o WM recorta a pintura para a área interna da
  moldura; conteúdo sem espaço fica invisível, sem vazar para outras cenas.
- **Entrada**: o corpo focaliza uma janela; os controles da barra de título
  fecham, minimizam ou maximizam/restauram. Teclado e mouse do conteúdo são
  encaminhados ao aplicativo focalizado; `Esc`, `Tab`, `F1` e `F2` não são
  atalhos globais do WM moderno. Em um workspace vazio, `Esc` não produz ação.
  A janela focalizada recebe um contorno azul de 2 px, além da sua barra de
  título ativa.

### Acessibilidade por teclado

No modo Moderno, `Alt+Tab` avança o foco entre janelas visíveis e
`Alt+Shift+Tab` retorna. `Alt+F4` fecha a janela focalizada, `Alt+F9` a
minimiza e `Alt+F10` alterna entre maximizar e restaurar. Esses atalhos não
alteram a API pública do WM. Teclas sem `Alt`, inclusive `Tab`, `F1` e `F2`,
continuam sendo encaminhadas diretamente ao aplicativo focalizado. O modo
Clássico preserva os atalhos do WM textual.

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
int  wm_handle_key(scancode);             // WM_RESULT_NONE ou WM_RESULT_EXIT
int  wm_handle_mouse(event);
void wm_toggle_window(id);
```

### Aplicativos hospedados (modo Moderno)

`wm_window_t` e as APIs TUI permanecem legadas. A hospedagem gráfica usa o
descritor público abaixo; suas dimensões incluem a moldura do WM e os callbacks
recebem apenas a área interna de conteúdo.

```c
typedef struct {
    wm_app_type_t app_type;
    const char* title;
    const char* taskbar_label;
    int min_width, min_height;
    int default_width, default_height;
    wm_key_redraw_t key_redraw;
    wm_redraw_handler_t on_draw;
    wm_key_handler_t on_key;
    wm_mouse_handler_t on_mouse;
    wm_close_handler_t on_close;
} wm_hosted_app_t;

int  wm_register_hosted_app(const wm_hosted_app_t* app);
int  wm_close_hosted_app(wm_app_type_t app_type);
int  wm_is_hosted_app_focused(wm_app_type_t app_type);
void wm_request_hosted_redraw(wm_app_type_t app_type);
```

`key_redraw` define quem apresenta a resposta ao teclado. O valor
`WM_KEY_REDRAW_WINDOW_MANAGER` preserva a recomposição completa usada pelos
aplicativos gerais; `WM_KEY_REDRAW_APPLICATION` permite que uma superfície com
controle de dano, como o terminal do Shell, apresente somente sua região suja.
O WM não inicia uma segunda recomposição depois desse callback enquanto o mesmo
aplicativo permanecer focado. Se o callback abrir ou focalizar outra janela, a
composição completa preserva o Z-order; o aplicativo pode consultar essa
condição com `wm_is_hosted_app_focused()`.

`WM_HOSTED_MIN_WIDTH` e `WM_HOSTED_MIN_HEIGHT` definem o mínimo estrutural
comum de 180x128 px para as janelas hospedadas. Os aplicativos não impõem um
mínimo adicional pela quantidade de controles: ao reduzir uma janela, o WM
mantém a moldura e o recorte da área interna, enquanto cada conteúdo pode
ocultar ou interromper elementos que não couberem.

O registro é singleton por `app_type`: registrar uma janela visível apenas a
restaura/focaliza, sem criar botão duplicado. Fechar uma janela chama `on_close`
somente para aquele aplicativo; minimizar e restaurar preservam seu estado.
Se VESA, backbuffer ou a área útil não comportarem o mínimo, o WM registra um
aviso e o chamador mantém o fluxo TUI correspondente.

### Integração com o Mouse

O kernel entrega cliques e roda primeiro à taskbar e ao Menu Iniciar. Botões de janela da taskbar pedem
`wm_toggle_window(id)`: uma janela minimizada é restaurada, uma janela visível
sem foco é focalizada e a janela focalizada é minimizada. Com o WM ativo, seus
eventos consomem o mouse antes de Desktop e aplicativos.

No modo Moderno, Shell, Explorer, Settings, Task Manager e System Updater
reutilizam a interação direta do WM: os controles da barra de título têm
prioridade, a área livre da barra inicia arraste e uma faixa de 8 px nas
bordas e cantos inicia redimensionamento. A captura termina em `RELEASE`;
janelas maximizadas devem ser restauradas antes de mover ou redimensionar.
Cada aplicativo desenha somente seu conteúdo e solicita recomposição ao mudar
de estado; o WM recompõe o workspace no backbuffer, desenha a taskbar por
último e invalida o cursor antes da pintura.

A roda só é entregue à área de conteúdo da janela visível de maior Z-order sob
o cursor; ela não focaliza nem altera a ordem das janelas. Barra de título,
bordas, Desktop vazio, taskbar e Menu Iniciar a consomem ou ignoram antes que
ela alcance um aplicativo. Shell rola três linhas visuais por notch; Explorer
move a seleção somente sobre a lista de arquivos; Task Manager faz o mesmo nas
listas de Processos e Threads. Settings não tem uma área rolável e ignora a
roda. Arraste de ícones, BMP e aplicativos externos já são componentes
separados do Desktop moderno; seleção múltipla e roda fora das janelas
hospedadas continuam fora do escopo.

---

## Taskbar (`taskbar.c`)

A taskbar preserva a identidade visual existente e funciona nos dois modos de
interface. Ela desenha menu, botões e relógio e mantém prioridade sobre as
interfaces abertas. Cada compositor de cena desenha a taskbar por ultimo; as
funcoes que alteram seus botoes ou configuracao apenas atualizam estado e nao
apresentam um frame por conta propria.

No modo moderno, a taskbar usa limites em pixels e suporta as cinco posições:
Baixo e Cima usam 24 px de altura; Esquerda e Direita usam 96 px de largura;
Custom usa 320×24 px em `custom_x * 8`/`custom_y * 16`, sempre limitada à
tela. Barras acopladas reduzem a área de trabalho; a barra customizada fica
sobreposta e recebe pintura e clique antes do conteúdo abaixo.

`tb_rect_t`, `taskbar_get_bounds()` e `taskbar_get_work_area()` formam o
contrato de geometria moderna. `taskbar_add_window()`,
`taskbar_remove_window()`, `taskbar_set_window_active()` e
`taskbar_take_window_request()` integram botões de janelas ao WM sem redesenho
implícito.

### Menu Iniciar

Enquanto estiver aberto, um clique fora do Menu Iniciar apenas o fecha; o
evento e consumido e nao pode acionar a interface que esteja abaixo dele.
No workspace moderno, `Shell` abre ou focaliza a janela singleton do terminal;
o X da janela apenas a oculta e preserva seu histórico. `Desktop` encerra o
workspace antes de voltar à área de trabalho. `Atualizacoes` abre ou focaliza
o System Updater; no modo Classic abre a TUI correspondente.

```
┌─────────────────┐
│ Desktop          │
│ Shell            │
│ Explorer         │
│ Task Manager     │
│ Configuracoes    │
│ Atualizacoes     │
│ Reiniciar        │
│ Desligar         │
└─────────────────┘
```

`TB_ACTION_UPDATER` e `WM_APP_UPDATER` foram anexados ao fim dos contratos
publicos existentes. O aplicativo nao adiciona icone ao Desktop e nao altera
os BMPs da interface.

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

No modo Moderno, a opção de posição da taskbar também expõe Baixo, Cima,
Esquerda, Direita e Custom. Ao mudar uma posição acoplada, o painel recalcula
seu layout a partir de `taskbar_get_work_area()`.

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

Permite a customização dinâmica de ícones de caracteres e mantém um cache dos
BMPs de Shell, Explorer e Task Manager. O build injeta `SHELL.BMP`,
`EXPLORER.BMP` e `TASKMGR.BMP` no diretório raiz FAT12; eles são carregados uma
única vez na inicialização. Se VESA, filesystem, arquivo, formato ou memória
não estiverem disponíveis, o cartão moderno usa o símbolo vetorial existente e
o modo Clássico continua usando caracteres.

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
int icons_get_desktop_bitmap_status(id); // OK para BMP no cache
int icons_draw_desktop_bitmap(id, x, y); // OK ou código para fallback
void icons_reset_defaults(void);
```

O comando Shell `icons` mostra o estado do filesystem e se cada um dos três
ícones de Desktop está em modo `BMP` ou `FALLBACK`.
