# 09 - Shell

> Estado atual: o Shell possui histórico rolável, `clear`, diagnósticos de
> kernel e suporte inicial a aplicativos ZAPP em ring 3. A lista exata de
> comandos fica em [comandos.md](comandos.md).

## Visão Geral

O shell é a interface que permite ao usuário interagir com o sistema operacional através de comandos digitados no teclado.

## Arquivo

`src/shell/shell_input.c` concentra o estado e o processamento da linha de
entrada; `src/shell/shell_dispatch.c` concentra o parsing e a tabela de
encaminhamento; `src/shell/shell.c` mantém os handlers e as políticas de
execução.

```
src/shell/
│   ├── shell.c          → Shell interativo, scrollback e handlers nativos/ZAPP
│   ├── shell_dispatch.c → Parser e tabela de comandos
│   ├── editor.c         → Editor de texto com syntax highlight
│   ├── mediaplayer.c    → Media player (WAV)
│   └── taskmanager.c    → Gerenciador de tarefas
```

---

## Como Funciona

O arquivo `src/shell/shell_input.c` concentra o buffer, o historico, a edicao
da linha, o prompt e o tratamento de scancodes. `shell_dispatch.c` remove
espacos, extrai o nome do comando e procura o handler na tabela. `shell.c`
permanece responsavel por rotear teclas, manter o estado de aplicativos e
executar os handlers.

### Fluxo

```
1. Usuário digita tecla
2. keyboard_handler() recebe scancode
3. shell_handle_key() encaminha a tecla para shell_input_handle_key()
4. shell_input_handle_key() adiciona o caractere ao buffer
5. Ao receber Enter, o modulo informa COMMAND_READY ao Shell
6. shell_process_command() retoma o terminal e chama shell_dispatch_execute()
7. shell_dispatch_execute() extrai o comando e procura a entrada na tabela
8. O adaptador chama o handler existente em shell.c
9. O Shell decide se deve mostrar o novo prompt
```

### Buffer de Input

```c
#define SHELL_BUFFER_SIZE 256

/* Estado mantido em src/shell/shell_input.c. */
```

O buffer armazena até 255 caracteres + null terminator.

### Scrollback

O terminal mantém um histórico circular fixo de 200 linhas, sem `kmalloc`.
`Shift+Seta para Cima/Abaixo`, `Page Up/Page Down`, `Home`, `End` e a roda do
mouse navegam pela saída. A roda funciona tanto no Shell Simple quanto na
janela Classic. Ao digitar, apagar ou confirmar um comando, o Shell retorna ao
fim para preservar o prompt. `clear` remove a tela e o histórico de saída.

### Histórico de comandos

O Shell mantém em memória os últimos 16 comandos da sessão, sem alocação
dinâmica. `Seta para Cima` volta aos comandos anteriores e `Seta para Baixo`
avança até o mais recente. Ao ultrapassar o comando mais recente, o texto que
estava sendo digitado antes da navegação é restaurado. Comandos consecutivos
idênticos ocupam uma única entrada. Esse histórico não é persistido em disco e
não é removido pelo comando `clear`.

### Shell hospedado no modo Classic

No modo Classic, o item `Shell` do Menu Iniciar abre ou focaliza uma única
janela hospedada pelo Window Manager. Ela mantém o mesmo buffer de entrada,
histórico e atalhos de scroll do terminal em tela cheia, mas o texto é
refluído visualmente quando a janela é redimensionada ou a taskbar muda de
posição. O mínimo é 80 colunas por 22 linhas de texto, além do rodapé do
histórico.

O botão X apenas oculta a janela e remove seu botão da taskbar; o Shell e seu
histórico continuam ativos. Reabrir pelo Menu Iniciar mostra o mesmo terminal.
Comandos que abrem Explorer, Settings ou Task Manager preservam a janela do
Shell e transferem o foco para o novo aplicativo. No modo Simple, o Shell
permanece em tela cheia.

`shell_update_hosted_terminal()` é chamado tanto pelo processo Shell quanto
pelo processo de sistema. Assim, alterações pendentes são apresentadas quando
a janela estiver visível e em foco mesmo se o Shell estiver bloqueado em rede,
disco ou outra operação cooperativa. A atualização não depende de clique,
tecla, roda do mouse ou término do comando.

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

O mapa unshifted/shifted fica centralizado em `src/drivers/keyboard.c` e e
consumido por `keyboard_scancode_to_ascii_shifted()`. O Shell nao mantem mais
uma tabela propria. Isso garante que a conversao de letras, simbolos e das
barras ISO/ABNT2 seja igual em todas as camadas.

---

## Parser de Comandos

### Separando Comando e Argumentos

`shell_process_command()` mantém a validação da entrada nula e a retomada do
terminal, depois delega para `shell_dispatch_execute()`. O dispatcher mantém o
contrato legado: ignora espaços, tabs, CR, LF e ESC no início; extrai no máximo
31 caracteres imprimíveis do nome; remove espaços e tabs entre nome e
argumentos; e entrega o restante sem interpretar aspas ou subcomandos.

A tabela em `src/shell/shell_dispatch.c` contém nome, handler e flags de
execução. As flags `MAY_BLOCK` e `OPENS_SCENE` são metadados nesta fase e não
alteram o comportamento. Adaptadores uniformes em `shell.c` preservam os
handlers atuais, inclusive os comandos que abrem cenas ou aplicativos.

Comandos desconhecidos mantêm a mensagem existente e retornam `OK`, assim como
comandos vazios. Entrada nula continua retornando `ERR_NULL` por meio da API
pública `shell_process_command()`.

---

## Comandos Disponíveis

> **Nota:** Consulte também o documento unificado [Atalhos e Comandos do Sistema](../atalhos_e_comandos.md) para ver a lista de atalhos de teclado de todos os aplicativos.

> **Nota:** A lista detalhada de todos os comandos do shell foi separada no documento [Comandos do Shell](comandos.md).

### Diagnóstico do log

O comando `log` expõe o ring de observabilidade sem criar outra interface:

- `log` ou `log status`: mostra ocupação, níveis, próxima sequência e
  contadores cumulativos;
- `log tail [1-16]`: mostra os registros recentes em ordem cronológica, com
  sequência, ticks, nível, módulo, ocorrências, flags e código opcional;
- `log clear`: limpa somente os registros e confirma diretamente no console;
- `log level`: mostra os níveis de console e buffer;
- `log level console <error|warn|info|debug>`: altera a exibição;
- `log level buffer <error|warn|info|debug>`: altera o armazenamento;
- `log check`: executa oito verificações em um ring privado, sem apagar o
  histórico real.

O armazenamento deve permanecer tão detalhado quanto o console. Combinações e
argumentos inválidos exibem o uso do comando e são registrados com
`ERR_INVALID`.

### Diagnóstico de temporizadores

O comando `timer` mantém a interface de diagnóstico pequena e somente-leitura:

- `timer` ou `timer status`: mostra frequência, ocupação, estados e contadores;
- `timer list`: mostra todos os timers criados e seus metadados atuais;
- `timer check`: executa 12 casos em tabelas privadas, sem disparar callbacks
  reais nem alterar o timer do ICMP.

Não existem subcomandos para criar, iniciar ou cancelar timers manualmente. A
lista expõe handle, proprietário, nome, modo, estado, prazo, período,
execuções, atrasos, períodos perdidos e último erro. Sintaxe inválida mostra o
uso e registra `ERR_INVALID`.

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

O comando `taskmgr` mantém a TUI de diagnóstico. Desktop e taskbar, no modo
classic, abrem uma janela gráfica própria. As duas interfaces usam três abas:
**Processos**, **Memória** e **Threads**; CPU, espera, tempo, páginas e dados
ATA aparecem nas tabelas e painéis de detalhes conforme houver espaço.

Atalhos: Tab=alterna aba, Setas=navega, S=ordena, Enter=propriedades e
Delete=encerra processo compatível. Esc fecha detalhes e, no Simple ocioso,
sai; no Classic, o fechamento usa o botão `X` ou `Alt+F4`.

### System Updater (`src/updater/updater.c`)

O comando `updater` e o item `Atualizacoes` do menu Iniciar abrem o aplicativo
nativo das U4/U5. A TUI Simple e a janela Classic compartilham Pacotes,
Estado, Historico e Remoto. Toda verificacao, consulta e todo preflight sao
somente-leitura; aplicar, restaurar, baixar e limpar cache exigem confirmacao
explicita e repetem a validacao pelos servicos Update.

Tab alterna as abas, setas mudam a selecao, F5 atualiza, V verifica, A prepara
aplicacao, B prepara rollback e Enter confirma. Esc cancela o contexto atual e
fecha somente a TUI Simple quando ociosa; a janela Classic usa `X` ou
`Alt+F4`. Na aba
Remoto, H alterna o opt-in, C consulta, D prepara download e X prepara a
limpeza do cache. O contrato completo fica em
[`system-updater.md`](../14-atualizacoes/system-updater.md).

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

No Classic, “Este Computador” e uma raiz virtual com `C:\` e os volumes EP2
montados. O historico inclui volume e caminho, enquanto a fonte ativa acompanha
a geracao; uma desmontagem retorna a raiz virtual. Volumes adicionais permitem
listar diretorios e visualizar
arquivos 8.3, mas bloqueiam toda mutacao com indicacao “Somente leitura”. O
Simple continua usando apenas o filesystem global de boot.

### Storage (`storage`)

`storage list`, `storage info <ataN|volume-id>`,
`storage mount <volume-id>` e `storage unmount <volume-id>` controlam o
registro de volumes adicionais. As consultas mostram erros e contadores por
disco; as montagens sao temporarias e nao oferecem qualquer operacao de
escrita. O comando tambem permanece disponivel no fallback Simple para
diagnostico.

### Indice e pesquisa EP3 (`index`, `search`)

`index status` mostra estado, entradas ativa/candidata, fontes, progresso,
memoria, flags e ultimo erro. `index rebuild` inicia uma nova tabela sem
descartar a ativa; `index cancel` preserva a ativa e suspende a repeticao ate
um evento de fonte, mesmo quando o rebuild pequeno ja terminou; `index check`
executa canarios, checksum, validacao estrutural e o autoteste compacto.
`regcheck full` inclui a mesma validacao e os casos de matching, limites,
cancelamento e corrupcao.

`search <termo>` aceita ate 63 caracteres e usa um workspace estatico para
ate 64 resultados. A saida mostra volume, caminho, tipo e tamanho, seguida de
avisos para indice parcial, em construcao, cancelado, desatualizado, erro,
resultado obsoleto ou volume ausente. Esses comandos sao o fallback completo
do modo Simple, que
nao recebe uma tela nova.

---

## Adicionando um Novo Comando

1. Adicione a função do comando em `shell.c`:

```c
static void cmd_meu_comando(const char* args) {
    video_print("Meu comando!\n", 0x0A);
}
```

2. Adicione um adaptador uniforme em `shell.c` e uma entrada na tabela de
   `shell_dispatch.c`:

```c
void shell_dispatch_cmd_meucomando(const char* arguments) {
    cmd_meu_comando(arguments);
}

{"meucomando", shell_dispatch_cmd_meucomando,
 SHELL_DISPATCH_FLAG_NONE},
```

3. Atualize o `cmd_help()`:

```c
video_print("  meucomando - Descrição\n", 0x07);
```
