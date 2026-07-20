# 13 - Aplicativos

Aplicativos do ZephyrOS: Editor, Media Player, File Manager e Task Manager.

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
int  editor_save(void);                 // Salva arquivo
int  editor_save_as(filename);          // Salva como
void editor_close(void);                // Fecha editor
void editor_draw(void);                 // Redesenha tela
int  editor_handle_key(scancode);       // Processa tecla
```

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
void mp_stop(void);               // Para reprodução
void mp_pause(void);              // Pausa/retoma
void mp_update(void);             // Atualiza display
void mp_draw(void);               // Desenha interface
int  mp_handle_key(scancode);     // Processa tecla
```

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

### Guias

| Guia | Conteúdo |
|------|----------|
| 0 | Processos: PID, Nome, Estado |
| 1 | CPU: Uso por processo (barra: verde <50%, amarelo <80%, vermelho >80%) |
| 2 | Memória: Total, Livre, Usada (barra: verde <60%, amarelo <80%, vermelho >80%) |
| 3 | Threads: TID, Nome, Estado |

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
static uint32_t last_cpu_ticks = 0;
static uint32_t last_proc_ticks[64] = {0};
static uint32_t cpu_usage[64] = {0};

// A cada refresh, calcula delta de ticks por processo
// e divide pelo total de ticks decorridos
```

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
| Esc | Fechar |

---

## File Manager (`filemanager.c`)

### Visão Geral

Gerenciador de arquivos estilo ZephyrOS Explorer com interface TUI.

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
| Esc | Sair |

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
