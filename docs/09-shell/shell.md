# 09 - Shell

## Visão Geral

O shell é a interface que permite ao usuário interagir com o sistema operacional através de comandos digitados no teclado.

## Arquivo

```
src/shell/
│   └── shell.c      → Shell interativo com 13 comandos
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
  help     - Mostra esta mensagem
  clear    - Limpa a tela
  ls       - Lista arquivos
  cat      - Exibe conteudo de arquivo
  echo     - Exibe texto
  mem      - Mostra informacoes de memoria
  procs    - Mostra processos ativos
  threads  - Mostra threads ativas
  uptime   - Mostra tempo ligado
  beep     - Toca um beep (freq duracao_ms)
  melody   - Toca uma melodia
  reboot   - Reinicia o sistema
  shutdown - Desliga o sistema
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
