# 12 - Desktop e Interface

Ambiente visual do ZephyrOS com dois modos implementados: Simple TUI
(fallback) e GUI Classic baseada em VESA. O nome Modern fica reservado para a
futura interface realmente moderna.

## Arquivos

```
src/desktop/desktop.c    → Ambiente desktop com ícones (TUI e GUI)
src/wm/wm.c              → Gerenciador de janelas (Dual interface frames)
src/taskbar/taskbar.c    → Barra de tarefas e menu Iniciar (Dual interface)
src/settings/settings.c  → Sistema de configurações
src/icons/icons.c        → Registro de ícones customizáveis
src/gui/gui.c            → Primitivas gráficas 2D para a GUI Classic
src/gui/display.c        → Escala e métricas centrais da GUI Classic
```

---

## Modos de interface

O sistema operacional segue a política da regra `#15` do `AGENTS.md`:

- **Simple TUI**: usa `video.c` como fallback operacional congelado. Recebe
  correções críticas, mas não novas funcionalidades ou regressão visual
  completa.
- **GUI Classic**: é a interface principal e a matriz obrigatória de
  aceitação. No MV3, ela usa a aparência Modern Dark: superfícies flat,
  fundo escuro, bordas de 1 px, cantos arredondados e acento teal.
- **GUI Modern**: reservada para o redesenho futuro e ainda não selecionável.
- **Alternância Dinâmica**: O comando `guimode simple|classic` permite alterar a engine visual em tempo de execução sem desligar os aplicativos rodando.

`guimode modern` apenas informa que o modo está reservado.
`desktop_set_mode(DESKTOP_MODE_MODERN)` retorna `ERR_UNAVAILABLE` e preserva
o modo ativo. A validação do Simple é um smoke test: entrar no modo, confirmar
vídeo, teclado e Shell, executar um comando básico e retornar ao Classic.
Desktop, Taskbar, WM e aplicativos hospedados são testados integralmente
apenas no Classic.

```c
typedef enum {
    DESKTOP_MODE_SIMPLE = 0,
    DESKTOP_MODE_CLASSIC,
    DESKTOP_MODE_MODERN
} desktop_mode_t;
```

Em `desktop_icon_t`, `x`/`y` pertencem à grade Simple; `classic_x`,
`classic_y`, `classic_width` e `classic_height` guardam a geometria em pixels
do Classic.

---

## Escala global do modo Classic (`display.c`)

O MV0 centraliza as metricas da interface em `ui/display.h`. A escala existe
somente em RAM, inicia em `normal` a cada boot e nunca troca a resolucao VESA.
O modo Simple nao consulta essas metricas: sua grade, fonte 8x16, cursor,
taskbar e aplicativos permanecem inalterados.

| Escala | Fator | Fonte | Espaco | Taskbar | Botao minimo | Icone | Titulo | VESA minima |
|--------|------:|------:|-------:|--------:|-------------:|------:|-------:|------------:|
| pequena | 1/1 | 8x16 | 8 px | 24 px | 60x24 px | 32 px | 24 px | 800x600 |
| normal | 5/4 | 10x20 | 10 px | 30 px | 75x30 px | 40 px | 30 px | 800x600 |
| grande | 3/2 | 12x24 | 12 px | 36 px | 90x36 px | 48 px | 36 px | 1024x768 |

Desde o MV0.1, essas tres dimensoes selecionam faces nativas da familia
monoespacada `Zephyr UI Bitmap`. A GUI Classic nao amplia mais a fonte 8x16
por nearest-neighbor no caminho normal. O redimensionador anterior permanece
somente como fallback com log caso uma combinacao de metricas nao tenha face.

```c
typedef struct {
    display_scale_t scale;
    uint16_t factor_numerator, factor_denominator;
    uint16_t font_width, font_height, spacing;
    uint16_t taskbar_height, taskbar_side_width;
    uint16_t button_min_width, button_min_height;
    uint16_t icon_size, title_bar_height, row_height;
    uint16_t min_width, min_height;
    uint8_t available;
} display_metrics_t;

int display_init(void);
int display_get_metrics(display_metrics_t* metrics);
int display_apply_scale(display_scale_t scale);
int display_parse_scale(const char* name, display_scale_t* scale);
const char* display_scale_name(display_scale_t scale);
uint32_t display_scale_px(uint32_t pixels);
```

`display_init()` roda depois de VESA/backbuffer e antes de Taskbar/Desktop.
Sem VESA, backbuffer ou area minima, o kernel preserva o fallback Simple.
`display_apply_scale()` valida o preset antes de altera-lo, reorganiza a cena
ativa e restaura as metricas anteriores se o reflow falhar. Desktop, Taskbar,
WM, Explorer, Settings e Task Manager usam a mesma geometria no desenho e no
hit-testing.

---

## Desktop (`desktop.c`)

### Inicialização

```c
desktop_init();
```

Cria 3 ícones padrão: Shell, Explorer e TaskMgr.

### Renderização (Desktop Gráfico)

No modo `GUI Classic`, o desktop desenha fundo, cards de ícone e símbolos por
primitivas, permitindo:
- Seleção visual azul em vez de caractere invertido.
- Grade responsiva com cartões-base de 112×96 px, escalados pelo preset, e
  slots em memória calculados conforme a resolução VESA e a área útil.
- Clique simples para seleção, duplo clique para abrir e arraste com encaixe
  no próximo slot livre.
- Barras acopladas reduzem a grade; a taskbar personalizada também reserva
  seus slots para que nenhum cartão fique sob ela.

As posições duram somente até reiniciar. Shell, Explorer e Task Manager usam
BMPs 24 bpp de 32×32 px no cache e os desenham em 32, 40 ou 48 px conforme o
preset; os arquivos usam magenta como cor transparente. Seleção múltipla e
persistência em disco continuam fora do modo classic atual.

No modo Simple, os ícones são organizados em grade (5 colunas) e mostrados na VGA Text Mode/Grid:

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
| Arrastar (Classic) | Move para o slot livre mais próximo |
| Esc | Sai do desktop |

---

## Primitivas Gráficas 2D (`gui.c`)

Motor gráfico da GUI Classic, permitindo renderização independente da grade TUI simple.

### Funções Base

```c
void gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t color);
void gui_draw_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const char* label, int pressed);
void gui_draw_scaled_text(uint32_t x, uint32_t y, const char* text,
                          uint32_t color);
int  gui_measure_scaled_text(const char* text, uint32_t* width, uint32_t* height);
void gui_draw_scaled_button(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            const char* label, int pressed);
void gui_draw_window_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           const char* title, int active);
void gui_draw_scaled_window_frame(uint32_t x, uint32_t y,
                                  uint32_t w, uint32_t h,
                                  const char* title, int active);
void gui_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t width,
                           uint32_t height, uint32_t radius,
                           uint32_t color);
void gui_draw_flat_border(uint32_t x, uint32_t y, uint32_t width,
                          uint32_t height, uint32_t color);
void gui_draw_vertical_gradient(uint32_t x, uint32_t y, uint32_t width,
                                uint32_t height, uint32_t top_color,
                                uint32_t bottom_color);
int gui_set_theme(gui_theme_t theme);
gui_theme_t gui_get_theme(void);
const char* gui_theme_name(gui_theme_t theme);
void gui_draw_modern_button(uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            const char* text, gui_button_state_t state);
```

As cenas Classic usam backbuffer e um único ciclo de frame. O MV1 adiciona
primitivas preenchidas de cantos arredondados, borda flat de 1 pixel e
gradiente vertical RGB. Essas APIs são aditivas e ainda não alteram painéis,
botões, molduras ou a paleta Classic.

O raio é limitado à metade da menor dimensão e usa spans calculados por
midpoint/Bresenham, sem ponto flutuante ou alocação. O gradiente interpola uma
cor por linha com ponto fixo e preserva exatamente as cores da primeira e da
última linha. Dimensões vazias ou regiões fora do framebuffer não desenham;
as demais operações respeitam o clip VESA ativo.

O MV2 acrescentou a paleta e os controles Modern. No MV3, essa aparência
passa a compor Desktop, Window Manager e Taskbar dentro do modo Classic;
`guimode modern` continua reservado.

- `gui_theme_t` registra `GUI_THEME_CLASSIC` ou `GUI_THEME_MODERN_DARK`;
- `gui_button_state_t` diferencia `NORMAL`, `HOVER` e `PRESSED`;
- `gui_set_theme(GUI_THEME_MODERN_DARK)` confirma o estilo ativo; o valor
  legado `GUI_THEME_CLASSIC` retorna `ERR_UNAVAILABLE` e preserva o Modern;
- `gui_draw_modern_button()` usa raio base escalado de 4 px, borda flat,
  texto nativo centralizado e altera apenas as cores entre os três estados.

A paleta oficial Modern é `#1E1E24` para fundo, `#2A2B36` para janela/título,
`#00ADB5` para acento, `#EEEEEE` para texto, `#4B4D5A` para borda inativa,
`#343746` para hover e `#007F86` para pressed. As constantes `GUI_COLOR_*`,
os painéis 3D permanecem disponíveis somente para rotas legadas fora do
Desktop/WM/Taskbar MV3.

As primitivas antigas continuam usando a fonte legada 8x16. Somente as
variantes `scaled` consultam `ui/display.h` e selecionam a face nativa Zephyr
UI de mesma dimensao, preservando Simple, Updater, Shell hospedado e
aplicacoes fora do MV0. Nao ha antialiasing nem alocacao durante o desenho.

---

## Window Manager (`wm.c`)

O comando `wm` preserva o gerenciador textual no modo Simple. No modo
Classic, ele abre um workspace VESA vazio. Shell, Explorer, Settings, Task
Manager e System Updater são aplicativos hospedados: seus comandos e ações do
Menu Iniciar criam ou focalizam uma única janela de cada tipo.

### Estado de renderização

- **Modo TUI**: janelas do WM usam bordas de caracteres e as APIs legadas.
- **Modo classic**: janelas hospedadas são compostas do menor para o maior
  Z-order e a taskbar é desenhada por último no mesmo ciclo VESA. Quando uma
  janela é aberta a partir do Desktop, os ícones continuam como fundo da
  composição; ao fechar a última janela, o controle retorna ao Desktop. Antes
  de desenhar cada aplicativo, o WM recorta a pintura para a área interna da
  moldura; conteúdo sem espaço fica invisível, sem vazar para outras cenas.
- **Entrada**: o corpo focaliza uma janela; os controles da barra de título
  fecham, minimizam ou maximizam/restauram. Teclado e mouse do conteúdo são
  encaminhados ao aplicativo focalizado; `Esc`, `Tab`, `F1` e `F2` não são
  atalhos globais do WM classic. Em um workspace vazio ou aplicativo hospedado
  ocioso, `Esc` não fecha janelas; ele permanece reservado para cancelar o
  contexto interno atual.
  A janela focalizada recebe borda teal flat de 1 px e gradiente sutil na
  barra de título; janelas inativas usam a borda inativa. Os controles
  continuam nas mesmas zonas de clique, mas aparecem como círculos compactos
  de minimizar, maximizar/restaurar e fechar.
  O padrao inicial segue o Windows: controles no lado direito, com minimizar,
  maximizar/restaurar e fechar da esquerda para a direita. Lado e ordem
  continuam configuraveis no Settings.

### Acessibilidade por teclado

No modo Classic, `Alt+Tab` avança o foco entre janelas visíveis e
`Alt+Shift+Tab` retorna. `Alt+F4` fecha a janela focalizada, `Alt+F9` a
minimiza e `Alt+F10` alterna entre maximizar e restaurar. Esses atalhos não
alteram a API pública do WM. Teclas sem `Alt`, inclusive `Tab`, `F1` e `F2`,
continuam sendo encaminhadas diretamente ao aplicativo focalizado. O modo
Simple preserva os atalhos do WM textual e o fechamento ocioso por `Esc`.

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

### Aplicativos hospedados (modo Classic)

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
int  wm_reflow_display(void);
```

`key_redraw` define quem apresenta a resposta ao teclado. O valor
`WM_KEY_REDRAW_WINDOW_MANAGER` preserva a recomposição completa usada pelos
aplicativos gerais; `WM_KEY_REDRAW_APPLICATION` permite que uma superfície com
controle de dano, como o terminal do Shell, apresente somente sua região suja.
O WM não inicia uma segunda recomposição depois desse callback enquanto o mesmo
aplicativo permanecer focado. Se o callback abrir ou focalizar outra janela, a
composição completa preserva o Z-order; o aplicativo pode consultar essa
condição com `wm_is_hosted_app_focused()`.

`wm_request_hosted_redraw()` agenda a recomposição no ciclo do Window Manager.
Workers cooperativos podem atualizar seu estado e solicitar a apresentação sem
executar diretamente o compositor VESA em sua própria pilha. Solicitações
repetidas antes do ciclo seguinte são consolidadas em uma única recomposição.

`WM_HOSTED_MIN_WIDTH` e `WM_HOSTED_MIN_HEIGHT` definem o mínimo estrutural
comum de 180x128 px para as janelas hospedadas. Explorer, Settings e Task
Manager publicam mínimos funcionais maiores. As constantes
`WM_HOSTED_FRAME_MAX_WIDTH` e `WM_HOSTED_FRAME_MAX_HEIGHT` reservam a moldura
da escala grande nesses descritores. Os mínimos externos registrados são
666x402 para Explorer, 726x492 para Settings e 726x542 para Task Manager.

Ao trocar a escala, `wm_reflow_display()` cancela capturas antigas de arraste
e redimensionamento, recalcula moldura e área útil, preserva dimensões ainda
válidas e limita cada janela ao novo workspace. Se algum mínimo não couber, o
reflow falha e a escala anterior é restaurada.

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

No modo Classic, Shell, Explorer, Settings, Task Manager e System Updater
reutilizam a interação direta do WM: os controles da barra de título têm
prioridade, a área livre da barra inicia arraste e uma faixa escalada de
8, 10 ou 12 px nas bordas e cantos inicia redimensionamento. A captura termina em `RELEASE`;
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
separados do Desktop classic; seleção múltipla e roda fora das janelas
hospedadas continuam fora do escopo.

---

## Taskbar (`taskbar.c`)

A taskbar mantém a semântica nos dois modos de interface. No Classic MV3, ela
usa fundo glass estático, botões flat arredondados, bordas de 1 px e Menu
Iniciar Modern; no Simple preserva a TUI. Cada compositor de cena desenha a
taskbar por ultimo; as funcoes que alteram seus botoes ou configuracao apenas
atualizam estado e nao apresentam um frame por conta propria.

No modo classic, a taskbar usa as métricas do preset e suporta as cinco
posições. Baixo/Cima usam 24, 30 ou 36 px; Esquerda/Direita e o modo Custom
escalam pelo mesmo fator. Texto, relógio, botões, menus, limites de clique e
área de trabalho compartilham a geometria calculada. Barras acopladas reduzem
a área de trabalho; a barra customizada fica sobreposta e recebe pintura e
clique antes do conteúdo abaixo.

Sem wallpaper configurável, o glass é a mistura pré-calculada de 75% de
`#2A2B36` sobre `#1E1E24`, feita uma vez no boot. A atualização isolada do
relógio repinta a mesma base cacheada antes do texto; não há alpha blending,
leitura do framebuffer ou alocação no caminho de cada frame.

`tb_rect_t`, `taskbar_get_bounds()` e `taskbar_get_work_area()` formam o
contrato de geometria classic. `taskbar_add_window()`,
`taskbar_remove_window()`, `taskbar_set_window_active()` e
`taskbar_take_window_request()` integram botões de janelas ao WM sem redesenho
implícito.

### Menu Iniciar

Enquanto estiver aberto, um clique fora do Menu Iniciar apenas o fecha; o
evento e consumido e nao pode acionar a interface que esteja abaixo dele.
No workspace classic, `Shell` abre ou focaliza a janela singleton do terminal;
o X da janela apenas a oculta e preserva seu histórico. `Desktop` encerra o
workspace antes de voltar à área de trabalho. `Atualizacoes` abre ou focaliza
o System Updater; no modo Simple abre a TUI correspondente.

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

## Explorer (`filemanager.c`)

Na EP2, o Explorer Classic trata “Este Computador” como raiz virtual. Ela
lista `C:\`, que representa o volume de boot e continua usando a API global,
mais os volumes Storage efetivamente montados, exibidos como
`<volume-id>:\`. Entrar, subir e usar voltar/avancar preserva o par
volume/caminho; subir da raiz de qualquer volume retorna a “Este Computador”.

Diretorios e arquivos adicionais usam `storage_list_dir()` e
`storage_read_file_range()`. A fonte ativa guarda a geracao observada; se o
volume for desmontado, o proximo refresh devolve a navegacao a raiz virtual
sem reutilizar dados antigos. Criar, renomear, excluir, recortar, colar e as
demais mutacoes ficam bloqueadas fora do boot, com barra e status
“Somente leitura”. O Explorer Simple permanece limitado ao volume de boot.

Na EP3, o Explorer Classic abre a pesquisa global por `Ctrl+F` ou pelo
controle clicavel `Pesquisar`. O termo aceita ate 63 caracteres; os 64
resultados ficam em workspace estatico e exibem nome, volume, caminho e tipo.
Setas, Page Up/Down, Home/End, roda e clique selecionam resultados. `Enter` ou
um segundo clique abre uma pasta; para arquivo, abre a pasta de origem e deixa
o nome selecionado. `Esc` fecha a pesquisa e restaura a navegacao anterior.

`fm_update()` observa a geracao de eventos do indice e solicita redesenho
hospedado em lotes durante o rebuild, atualizando progresso, resultados e
avisos de parcial, construcao, cancelamento, obsolescencia ou volume ausente.
Falha do indice nao fecha nem degrada o Explorer: a navegacao comum permanece
independente. O Explorer Simple continua congelado e usa `index`/`search` no
Shell como fallback.

A fixture interna `filemanager_test.h`, disponivel somente em builds
`ZEPHYROS_HOST_TEST`, valida diretamente os contratos deterministas de
caminhos, nomes, layout, selecao, historico e pesquisa sem iniciar o Explorer
ou acessar VFS, disco, rede e hardware reais.

## Settings (`settings.c`)

Sistema de configuração geral.

No modo Classic, a opção de posição da taskbar também expõe Baixo, Cima,
Esquerda, Direita e Custom. Ao mudar uma posição acoplada, o painel recalcula
seu layout a partir de `taskbar_get_work_area()`.

A opção `Escala` aparece tanto no Settings Simple quanto no Classic, é
sincronizada ao abrir e chama `display_apply_scale()`. Uma recusa restaura
imediatamente o valor visual anterior.

A categoria `Mouse` também é compartilhada pelos dois modos. Velocidade,
botão principal e aceleração são sincronizados ao abrir e aplicados pela API
do driver. Uma recusa restaura o valor efetivo; as preferências ficam somente
em RAM.

A categoria `Armazenamento` existe somente no Classic e e somente de status.
Ela sincroniza a cada desenho o numero de discos/volumes, os quatro slots ATA
presentes e ate quatro montagens, distinguindo boot gravavel e volumes
adicionais somente-leitura. Mount e unmount continuam exclusivos do Shell. O
Simple preserva as oito categorias anteriores e o comportamento do volume de
boot. No Classic, a altura se ajusta para manter as nove categorias nas tres
escalas.

O tema Modern Dark é aplicado ao modo Classic no boot. O seletor de tema foi
removido do Settings para não reintroduzir a skin Windows 95; isso não
habilita `guimode modern`.

### Categorias

| Categoria | Opções |
|-----------|--------|
| Tela | Escala (Pequena/Normal/Grande), Mostrar grade |
| Barra de Tarefas | Posição, Tamanho ícone, Fixada, Relógio |
| Janelas | Botões lado, Ordem botões, Título, Borda |
| Ícones | Editor visual (Desktop, WM, Arquivos) |
| Sistema | Nome PC, Info memória, Processos, Reiniciar |
| Som | Volume, Beep iniciar, Som teclado |
| Sobre | Versão, Créditos |
| Mouse | Velocidade (1-10), Botão principal, Aceleração |
| Armazenamento (Classic) | Discos e montagens, somente status |

---

## Icons (`icons.c`)

Permite a customização dinâmica de ícones de caracteres e mantém um cache dos
BMPs de Shell, Explorer e Task Manager. O build injeta `SHELL.BMP`,
`EXPLORER.BMP` e `TASKMGR.BMP` no diretório raiz FAT12; eles são carregados uma
única vez na inicialização. Se VESA, filesystem, arquivo, formato ou memória
não estiverem disponíveis, o cartão classic usa o símbolo vetorial existente e
o modo Simple continua usando caracteres.

```c
typedef struct {
    icon_entry_t desktop[ICON_DESKTOP_COUNT];
    icon_entry_t wm[ICON_WM_COUNT];
    icon_entry_t fm[ICON_FM_COUNT];
    icon_entry_t tb[ICON_TB_COUNT];
} icon_registry_t;
```

### App Store AS3

A App Store e um aplicativo hospedado singleton: `WM_APP_APPSTORE` identifica
sua janela e `TB_ACTION_APPSTORE` identifica a acao correspondente no Menu
Iniciar. O item abre ou focaliza a loja no Classic e abre sua TUI funcional no
Simple; ele nao cria icone adicional no Desktop. Os identificadores foram
anexados aos contratos publicos, preservando os valores existentes.

### API

```c
icon_registry_t* icons_get_registry(void);
icon_entry_t* icons_get_desktop(id);
int icons_get_desktop_bitmap_status(id); // OK para BMP no cache
int icons_draw_desktop_bitmap(id, x, y); // OK ou código para fallback
int icons_draw_desktop_bitmap_resized(id, x, y, size);
void icons_reset_defaults(void);
```

Os BMPs 32x32 permanecem armazenados uma única vez. A variante redimensionada
usa nearest-neighbor durante o desenho, sem nova alocação, para produzir
ícones de 32, 40 ou 48 px conforme a escala Classic.

O comando Shell `icons` mostra o estado do filesystem e se cada um dos três
ícones de Desktop está em modo `BMP` ou `FALLBACK`.
