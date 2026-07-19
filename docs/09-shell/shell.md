# 09 - Shell

## Visão Geral

O shell é a interface que permite ao usuário interagir com o sistema operacional através de comandos digitados no teclado.

## Arquivo

```
src/shell/
│   ├── shell.c          → Shell interativo com 20+ comandos
│   ├── editor.c         → Editor de texto com syntax highlight
│   ├── mediaplayer.c    → Media player (WAV)
│   └── taskmanager.c    → Gerenciador de tarefas
```

---

## Como Funciona

### Fluxo

```
1. Usuário digita tecla
2. keyboard_handler() recebe scancode
3. shell_handle_key() converte para caractere
4. Caractere é adicionado ao buffer
5. Se Enter, processa o comando
6. Mostra novo prompt
```

### Buffer de Input

```c
#define SHELL_BUFFER_SIZE 256

static char input_buffer[SHELL_BUFFER_SIZE];
static int input_pos = 0;
```

O buffer armazena até 255 caracteres + null terminator.

### Prompt

```
minios> _
```

O prompt é verde (`0x0A`) e aparece após cada comando.

---

## Tratamento de Teclas

### Teclas Especiais

| Scancode | Ação |
|----------|------|
| 0x0E (Backspace) | Apaga último caractere |
| 0x1C (Enter) | Processa comando |
| Qualquer outra | Adiciona ao buffer |

### Tabela de Scancodes

```c
static const char scancode_table[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    // ...
};
```

---

## Parser de Comandos

### Separando Comando e Argumentos

```c
int shell_process_command(const char* input) {
    // Remove espaços no início
    while (*input == ' ') input++;

    // Extrai o comando (até espaço ou fim)
    char cmd[32];
    int i = 0;
    while (*input && *input != ' ' && i < 31) {
        cmd[i++] = *input++;
    }
    cmd[i] = '\0';

    // Pula espaços entre comando e argumentos
    while (*input == ' ') input++;

    // Executa o comando
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(input);  // input agora é o argumento
    }
    // ...
}
```

---

## Comandos Disponíveis

### `help`
Mostra a lista de todos os comandos.

```
minios> help
Comandos disponiveis:
  help      - Mostra esta mensagem
  clear     - Limpa a tela
  ls        - Lista arquivos
  cat       - Exibe conteudo de arquivo
  echo      - Exibe texto
  mem       - Mostra informacoes de memoria
  procs     - Mostra processos ativos
  threads   - Mostra threads ativas
  uptime    - Mostra tempo ligado
  beep      - Toca um beep (freq duracao_ms)
  melody    - Toca uma melodia
  explorer  - Abre gerenciador de arquivos (Explorer TUI)
  desktop   - Abre o ambiente desktop
  taskman   - Abre o gerenciador de tarefas
  edit      - Abre o editor de texto (edit ARQUIVO.TXT)
  play      - Toca arquivo WAV (play MUSICA.WAV)
  compress  - Gerencia compressao (on/off/status)
  settings  - Abre configuracoes do sistema
  reboot    - Reinicia o sistema
  shutdown  - Desliga o sistema
```

### `clear`
Limpa a tela e volta ao topo.

### `ls`
Lista todos os arquivos no disco FAT12.

```
minios> ls
Arquivos no disco:
  ARQUIVO.TXT  128 bytes
  DADOS.DAT    256 bytes
```

### `cat <arquivo>`
Exibe o conteúdo de um arquivo de texto.

```
minios> cat ARQUIVO.TXT
Olá, este é o conteúdo do arquivo!
```

### `echo <texto>`
Imprime texto na tela.

```
minios> echo Ola Mundo
Ola Mundo
```

### `mem`
Mostra informações de memória.

```
minios> mem
Memoria:
  Total: 128 MB
  Livre: 120 MB
  Usada: 8 MB
```

### `procs`
Lista todos os processos ativos.

```
minios> procs
Processos ativos:
  PID 1  idle  RUNNING
  PID 2  shell  RUNNING
Total: 2 processos
```

### `threads`
Lista todas as threads ativas.

```
minios> threads
Threads ativas:
  TID 1  main  RUNNING
  TID 2  timer  BLOCKED
Total: 2 threads
```

### `uptime`
Mostra o tempo desde que o sistema foi ligado.

```
minios> uptime
Uptime: 2h 30m 15s
```

### `beep [frequência] [duração]`
Toca um beep pelo PC Speaker.

```
minios> beep          → Beep padrão (800Hz, 200ms)
minios> beep 440 500  → 440 Hz por 500ms
```

### `melody`
Toca uma escala musical (C4→C5).

```
minios> melody
Tocando melodia...
Melodia concluida!
```

### `reboot`
Reinicia o computador.

### `shutdown`
Desliga o computador (para o CPU).

### `explorer`
Abre o gerenciador de arquivos estilo Windows Explorer (TUI).

```
minios> explorer
```

Navegação com setas, F3 visualizar, F7 criar, F8 excluir com confirmação, F2 renomear.

### `desktop`
Ativa o ambiente desktop com ícones e menu Iniciar.

```
minios> desktop
```

### `taskman`
Abre o gerenciador de tarefas com monitoramento de processos, threads, CPU e memória.

```
minios> taskman
```

Guias: Processos, CPU, Memória e Threads.

### `edit <arquivo>`
Editor de texto com syntax highlighting e word wrap.

```
minios> edit TESTE.TXT
```

Funcionalidades:
- Syntax highlight: C, Python, Assembly, Markdown
- Word wrap automático
- Detecção de encoding (ASCII, Latin1, UTF-8)
- Detecção de line ending (LF, CR, CRLF)
- Numeração de linhas
- Scroll vertical

Teclas:
| Tecla | Ação |
|-------|------|
| Setas | Navegação |
| Ctrl+S | Salvar |
| Ctrl+Q | Sair |
| Home/End | Início/fim da linha |
| Page Up/Down | Rola página |

### `play <arquivo.wav>`
Reproduz um arquivo de áudio WAV via AC97.

```
minios> play MUSICA.WAV
```

### `compress on|off|status`
Gerencia o módulo de compressão de RAM.

```
minios> compress on      → Ativa compressão
minios> compress off     → Desativa
minios> compress status  → Mostra estatísticas
```

### `settings`
Abre o sistema de configurações do MiniOS.

```
minios> settings
```

Categorias: Tela, Barra de Tarefas, Janelas, Ícones, Sistema, Som, Sobre.

---

## Aplicativos

### Editor de Texto (`editor.c`)

Editor completo com interface TUI. Características:

- **Buffer**: linhas dinâmicas (até 1000), 256 caracteres por linha
- **Modos**: inserção, navegação, seleção
- **Syntax Highlight**: detecta linguagem pela extensão (.c, .py, .asm, .md)
  - C: palavras-chave azul, strings verde, comentários vermelho, diretivas magenta
  - Python: palavras-chave azul, strings verde, comentários vermelho
  - Assembly: instruções azul, registradores ciano, diretivas magenta
  - Markdown: títulos amarelo, links azul, code backticks verde
- **Word Wrap**: quebra linhas longas na exibição sem modificar o arquivo
- **Encoding**: detecta BOM UTF-8, sequências UTF-8, Latin1 ou ASCII
- **Line Endings**: detecta CRLF (Windows), LF (Unix) ou CR (Mac)

### Media Player (`mediaplayer.c`)

Player de áudio com suporte a WAV.

```
Estado: IDLE | PLAYING | PAUSED
Arquivo: MUSICA.WAV
Duração: 00:30
```

- Carrega arquivo WAV do sistema de arquivos
- Reproduz via driver AC97
- Exibe informações: sample rate, bits, canais
- Controles: P=Play/Pause, S=Stop, +/- Volume

### Task Manager (`taskmanager.c`)

Gerenciador de tarefas com 4 guias:

**Processos**: lista PID, nome, estado (RUNNING/READY/BLOCKED/ZOMBIE)
**CPU**: uso de CPU por processo (barra gráfica)
**Memória**: total, livre, usada (barra gráfica com alertas >80% e >90%)
**Threads**: lista TID, nome, estado

Atalhos: Tab=alterna guia, Up/Down=navega, Esc=sair.

### File Manager (`filemanager.c`)

Gerenciador de arquivos estilo Windows Explorer.

```
┌──────────────────────────────────────────────┐
│           MiniOS Explorer                     │
├──────────────────────────────────────────────┤
│ F1=Ajuda F3=Ver F5=Atualizar F7=Novo F8=Exc │
├──────┬─────────┬──────────┬──────────────────┤
│ Nome │ Tamanho │ Tipo     │                  │
├──────┼─────────┼──────────┼──────────────────┤
│ TESTE│ 128     │ ARQUIVO  │                  │
│ DADOS│ 256     │ ARQUIVO  │                  │
└──────┴─────────┴──────────┴──────────────────┘
```

Funcionalidades:
- Navegação com setas, Page Up/Down, Home/End
- F2: Renomear arquivo
- F3: Visualizar conteúdo
- F5: Atualizar lista
- F7: Criar novo arquivo
- F8: Excluir com confirmação
- Barra de status com info do arquivo selecionado
- Integração com taskbar

---

## Adicionando um Novo Comando

1. Adicione a função do comando em `shell.c`:

```c
static void cmd_meu_comando(const char* args) {
    video_print("Meu comando!\n", 0x0A);
}
```

2. Adicione o `strcmp` no parser:

```c
} else if (strcmp(cmd, "meucomando") == 0) {
    cmd_meu_comando(input);
}
```

3. Atualize o `cmd_help()`:

```c
video_print("  meucomando - Descrição\n", 0x07);
```
