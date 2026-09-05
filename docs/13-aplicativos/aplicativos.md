# 13 - Aplicativos

Aplicativos nativos do ZephyrOS: Editor, Media Player, File Manager e Task
Manager. Explorer, Task Manager e Settings também possuem caminhos gráficos
modernos com fallback TUI; aplicativos ZAPP externos executam em ring 3 pela
App API.

## Arquivos

```
src/shell/editor.c          → Editor de texto
src/shell/mediaplayer.c     → Media player (WAV)
src/shell/taskmanager.c     → Gerenciador de tarefas
src/filemanager/filemanager.c → Gerenciador de arquivos
```

---

## Editor (`editor.c`)

### Visão Geral

Editor de texto completo com interface TUI, syntax highlighting e word wrap.

### Estrutura

```c
#define EDITOR_MAX_LINES 1000
#define EDITOR_MAX_LINE_LENGTH 256
#define EDITOR_TAB_WIDTH 4
#define EDITOR_VISIBLE_LINES 20
#define EDITOR_VISIBLE_COLS 78

typedef struct {
    char** lines;              // Linhas do buffer
    int line_count;            // Total de linhas
    int cursor_x, cursor_y;    // Posição do cursor
    int scroll_x, scroll_y;    // Scroll
    char filename[64];         // Arquivo atual
    int modified;              // Modificado?
    int encoding;              // ASCII, LATIN1, UTF8
    int line_ending;           // LF, CR, CRLF
} editor_t;
```

### Funcionalidades

#### Syntax Highlight

Detecta linguagem pela extensão do arquivo:

| Extensão | Linguagem | Destaques |
|----------|-----------|-----------|
| `.c`, `.h` | C | `int`, `if`, `return` (azul); strings (verde); comentários (vermelho); `#include` (magenta) |
| `.py` | Python | `def`, `class`, `import` (azul); strings (verde); comentários (vermelho) |
| `.asm` | Assembly | Instruções (azul); registradores (ciano); diretivas (magenta) |
| `.md` | Markdown | Títulos (amarelo); links (azul); code (verde) |

#### Word Wrap

Quebra automática de linhas longas na exibição sem modificar o arquivo em disco.

#### Detecção de Encoding

```c
uint8_t detect_encoding(const uint8_t* data, uint32_t size) {
    // 1. Verifica BOM UTF-8 (EF BB BF)
    // 2. Conta sequências UTF-8 válidas
    // 3. Se não ASCII, assume Latin1
    // 4. Padrão: ASCII
}
```

#### Detecção de Line Ending

```c
uint8_t detect_line_ending(const uint8_t* data, uint32_t size) {
    // CRLF (\r\n) → ZephyrOS
    // CR (\r)     → Mac
    // LF (\n)     → Unix
}
```

### API

```c
void editor_init(void);
int  editor_open(filename);             // Abre arquivo
void editor_new(void);                  // Cria documento vazio
void editor_run(void);                  // Executa editor interativo
void editor_run_file(filename);         // Abre arquivo no editor
void editor_close(void);                // Fecha editor
int  editor_handle_key(scancode);       // Processa tecla
uint8_t editor_is_running(void);        // Informa se esta aberto
```

`editor_main` não faz parte mais do contrato público: a antiga declaração não
possuía implementação nem chamadores no código ativo e foi aposentada. O
Shell usa `editor_run` e `editor_run_file` para iniciar o fluxo interativo.
O header interno `apps/editor_test.h` e seu contrato host-only só existem para
fixtures determinísticas de cobertura e não participam do build freestanding.

### Teclas

| Tecla | Ação |
|-------|------|
| Setas | Navegação |
| Home | Início da linha |
| End | Fim da linha |
| Page Up | Sobe página |
| Page Down | Desce página |
| Ctrl+S | Salvar |
| Ctrl+Q | Sair |
| Backspace | Apagar caractere |
| Enter | Nova linha |

### Tela

```
┌─────────────────────────────────────────────────────────────┐
│  ARQUIVO.TXT                      Lin 10 Col 25  ASCII  LF  │
├─────────────────────────────────────────────────────────────┤
│  1| #include <stdio.h>                                      │
│  2|                                                        │
│  3| int main(void) {                                        │
│  4|     printf("Hello, World!\n");                          │
│  5|     return 0;                                           │
│  6| }                                                        │
│  7|                                                        │
│  8| # Azul = keyword                                        │
│  9| # Verde = string                                        │
│ 10| # Vermelho = comentário                                 │
├─────────────────────────────────────────────────────────────┤
│  Ctrl+S:Salvar  Ctrl+Q:Sair                    Lin 10 Col 25│
└─────────────────────────────────────────────────────────────┘
```

---

## Media Player (`mediaplayer.c`)

### Visão Geral

Player de áudio para arquivos WAV com interface TUI.

### Estados

```c
typedef enum {
    MP_STATE_IDLE,      // Parado
    MP_STATE_PLAYING,   // Tocando
    MP_STATE_PAUSED     // Pausado
} mp_state_t;
```

### Layout

```
┌─────────────────────────────────────────────────────────────┐
│                   ZephyrOS Media Player                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Estado: PLAYING                                           │
│   Arquivo: MUSICA.WAV                                       │
│                                                             │
│   Sample Rate: 44100 Hz                                     │
│   Bits: 16                                                  │
│   Canais: Stereo                                            │
│   Duracao: 00:30                                            │
│                                                             │
│   [> Play/Pause] [Stop]  Vol: ████████░░                    │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│  P:Play/Pause S:Stop  +/-:Volume  Esc:Sair                 │
└─────────────────────────────────────────────────────────────┘
```

### API

```c
void mp_init(void);
int  mp_play_audio(filename);     // Carrega e toca WAV
int  mp_play_image(filename);     // Carrega e apresenta BMP
int  mp_play_media(audio, image); // Reproduz audio e imagem
void mp_stop(void);               // Para reprodução
void mp_pause(void);              // Pausa/retoma
void mp_resume(void);              // Retoma reprodução
mp_status_t* mp_get_status(void);  // Retorna o estado atual
void mp_update(void);             // Atualiza display
```

`mp_main` não faz parte mais do contrato público: a antiga declaração não
possuía implementação nem chamadores no código ativo e foi aposentada. O
fluxo de execução do aplicativo usa as operações do Media Player chamadas
pelos handlers do Shell.

### Teclas

| Tecla | Ação |
|-------|------|
| P | Play/Pause |
| S | Stop |
| + | Aumentar volume |
| - | Diminuir volume |
| Esc | Sair |

---

## Task Manager (`taskmanager.c`)

### Visão Geral

Gerenciador de tarefas com monitoramento de processos, CPU, memória e threads.

### Abas e interfaces

| Aba | Conteúdo |
|-----|----------|
| Processos | PID, nome, estado, TCK%, tipo, tempo, espera e painel de detalhes |
| Memória | Total, usada, livre, páginas, zonas MM4, maior run, páginas isoladas, fragmentação e resumo ATA |
| Threads | Estado, espera, EIP, ESP e stack sem desreferenciar endereços |

O comando `taskmgr` abre deliberadamente a TUI para diagnóstico. O Desktop e
a taskbar, no modo classic, abrem uma janela gráfica própria com as mesmas
fontes de dados, botões de janela e arraste pela barra de título.

A aba Memória mantém o resumo global e acrescenta um snapshot das zonas
físicas `KERNEL`, `HEAP`, `SLAB`, `PROCESS`, `BUFFER` e `FREE`, além de runs
livres, maior run, páginas isoladas e fragmentação. A coleta detalhada é
atualizada no máximo uma vez por segundo; Simple e Classic usam o mesmo
contrato e não executam a varredura em IRQ ou page fault.

### Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Processos    CPU    Memoria    Threads                     │
├─────────────────────────────────────────────────────────────┤
│  PID  Nome             Estado     CPU                       │
│ ─────────────────────────────────────────────────────────    │
│  1    idle             RUNNING    ████████░░ 80%            │
│  2    shell            RUNNING    ██░░░░░░░░ 20%            │
│                                                             │
│                                                             │
│  Processos ativos: 2                                        │
├─────────────────────────────────────────────────────────────┤
│  Tab:Guia  Up/Down:Selecionar  Esc:Sair                    │
└─────────────────────────────────────────────────────────────┘
```

### Cálculo de CPU

```c
static uint32_t last_tick_sample = 0;
static uint32_t last_process_ticks[64] = {0};
static uint32_t tick_usage[64] = {0};

// A cada refresh, calcula delta de ticks por processo
// e divide pelo total de ticks decorridos
```

O rotulo `TCK%` deixa explicito que este valor e uma estimativa da participacao
nos ticks do PIT, nao uma medicao de uso real de CPU. RDTSC/PMU permanecem
adiados ate existir calibracao confiavel.

### API

```c
void taskmgr_init(void);
void taskmgr_open(void);
void taskmgr_close(void);
void taskmgr_refresh(void);
int  taskmgr_handle_key(scancode);
```

### Teclas

| Tecla | Ação |
|-------|------|
| Tab | Alterna guia |
| Up/Down | Navega na lista |
| Esc | Fechar detalhes; no Simple ocioso, fechar o aplicativo |
| Alt+F4 ou botão X | Fechar a janela Classic |

---

## File Manager (`filemanager.c`)

### Visão Geral

Gerenciador de arquivos ZephyrOS Explorer com modo simple TUI e modo
classic selecionado por `guimode`. Sem VESA ou backbuffer, o fallback TUI é
automático.

### Estrutura

```c
#define FM_MAX_FILES 128
#define FM_NAME_LEN 64

typedef struct {
    char name[FM_NAME_LEN];
    uint32_t size;
    uint8_t is_dir;
    uint8_t attributes;
} fm_file_entry_t;

typedef struct {
    fm_file_entry_t files[FM_MAX_FILES];
    int file_count;
    int selected;
    int scroll_offset;
} fm_state_t;
```

### Layout

```
┌─────────────────────────────────────────────────────────────┐
│                    ZephyrOS Explorer                          │
├─────────────────────────────────────────────────────────────┤
│ F1=Ajuda F3=Ver F5=Atualizar F7=Novo F8=Excluir Esc=Sair  │
├─────────────────────────────────────────────────────────────┤
│  Nome              Tamanho    Tipo                          │
├─────────────────────────────────────────────────────────────┤
│  ARQUIVO.TXT       128 bytes  ARQUIVO                       │
│  DADOS.DAT         256 bytes  ARQUIVO                       │
│  TESTE.C           512 bytes  ARQUIVO                       │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│  Arquivo: ARQUIVO.TXT | Arquivo | 128 bytes                 │
├─────────────────────────────────────────────────────────────┤
│  F1=Ajuda  F3=Ver  F5=Refresh  F7=Novo  F8=Excluir         │
└─────────────────────────────────────────────────────────────┘
```

### Funcionalidades

| Tecla | Ação |
|-------|------|
| Up/Down | Navega arquivos |
| Page Up/Down | Rola página |
| Home | Primeiro arquivo |
| End | Último arquivo |
| Enter | Abre/visualiza arquivo |
| F2 | Renomear arquivo |
| F3 | Visualizar conteúdo |
| F5 | Atualizar lista |
| F7 | Criar novo arquivo |
| F8 | Excluir (com confirmação) |
| Esc | Voltar/cancelar; no Simple ocioso, sair |
| Alt+F4 ou botão X | Fechar a janela Classic |

### API

```c
void fm_init(void);
void fm_open(void);
void fm_close(void);
void fm_refresh(void);
void fm_draw(void);
int  fm_handle_key(scancode);

// Operações de arquivo
void fm_delete_file(void);       // Exclui com confirmação
void fm_rename_file(void);       // Renomeia (F2)
void fm_create_file(void);       // Cria novo arquivo (F7)
void fm_view_file(void);         // Visualiza conteúdo (F3)
```

### Confirmação de Exclusão

```c
// F8 abre diálogo de confirmação:
// "Excluir ARQUIVO.TXT? (S/N)"
// S = confirma exclusão
// N = cancela
```

### Renomeação

```c
// F2 abre modo de edição inline
// Usuário digita novo nome + Enter
// Esc cancela
```

---

## GUI Test (`guitest`)

### O que é?

Comando que testa as primitivas gráficas 2D do módulo GUI.

### Comando

```bash
guitest
guitest modern
```

### O que testa

- `guitest` preserva a cena Classic com `gui_draw_window_frame()`,
  `gui_draw_button()` e `gui_draw_text()`.
- `guitest modern` mantém uma moldura externa Classic e apresenta as
  primitivas do MV1: retângulos preenchidos com raios diferentes, bordas flat
  de 1 pixel, clipping e gradientes verticais nos dois sentidos.
- A cena MV2 preserva esses casos e acrescenta as sete cores da paleta
  oficial, o nome do tema Modern Dark ativo e um botão Modern interativo. O botão
  mostra `Normal` fora do cursor, `Hover` sob o cursor e `Pressed` enquanto o
  botão esquerdo permanece pressionado dentro; arrastar para fora restaura
  `Normal` e soltar dentro restaura `Hover`.
- A cena usa a escala ativa e a área útil da Taskbar. Ela exige
  `guimode classic`; não habilita o modo Modern reservado.
- O X da moldura e `Esc` fecham ambas as cenas. Qualquer outro argumento
  mostra `Uso: guitest [modern]` sem abandonar o Shell.

### Arquivos

```
src/gui/gui.c
src/shell/guitest_app.c
```
