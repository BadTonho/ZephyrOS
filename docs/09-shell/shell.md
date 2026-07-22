# 09 - Shell

## Visão Geral

O shell é a interface que permite ao usuário interagir com o sistema operacional através de comandos digitados no teclado.

## Arquivo

```
src/shell/
│   ├── shell.c          → Shell interativo com 27 comandos
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
zephyr> _
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

> **Nota:** Consulte também o documento unificado [Atalhos e Comandos do Sistema](../atalhos_e_comandos.md) para ver a lista de atalhos de teclado de todos os aplicativos.

> **Nota:** A lista detalhada de todos os comandos do shell foi separada no documento [Comandos do Shell](comandos.md).

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
- **Line Endings**: detecta CRLF (ZephyrOS), LF (Unix) ou CR (Mac)

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

Gerenciador de arquivos estilo ZephyrOS Explorer.

```
┌──────────────────────────────────────────────┐
│                    ZephyrOS Explorer                     │
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
