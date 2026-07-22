# Comandos do Shell

Este documento lista e detalha todos os comandos disponíveis no shell (terminal) interativo do ZephyrOS.

> **Nota:** Consulte também o documento unificado [Atalhos e Comandos do Sistema](../atalhos_e_comandos.md) para ver a lista de atalhos de teclado de todos os aplicativos.

## `help`
Mostra a lista de todos os comandos.

```
zephyr> help
Comandos disponiveis:
  help      - Mostra esta mensagem
  clear     - Limpa tela e historico do terminal
  desktop   - Abre a area de trabalho
  settings  - Abre o painel de configuracoes
  wm        - Abre gerenciador de janelas
  ls        - Lista arquivos
  cat       - Exibe conteudo de arquivo
  echo      - Exibe texto
  mem       - Mostra informacoes de memoria
  procs     - Mostra processos ativos
  threads   - Mostra threads ativas
  uptime    - Mostra tempo ligado
  beep      - Toca um beep (freq duracao_ms)
  melody    - Toca uma melodia
  explorer  - Abre o gerenciador de arquivos
  taskmgr   - Abre o gerenciador de tarefas
  taskcfg   - Configura a barra de tarefas
  compress  - Liga/desliga compressao de RAM
  stats     - Mostra estatisticas de compressao
  play      - Toca arquivo WAV
  view      - Exibe imagem BMP
  stop      - Para player de midia
  edit      - Editor de texto (edit ARQUIVO.TXT)
  mouse     - Mostra status do mouse PS/2
  guitest   - Testa primitivas GUI 2D
  guimode   - Altera entre gui classica (TUI) e moderna
  health    - Exibe metricas e estado de recovery do kernel
  appcheck  - Testa a API de aplicativos
  usertest  - Executa teste isolado em ring 3
  reboot    - Reinicia o sistema
  shutdown  - Desliga o sistema
```

## `clear`
Limpa a tela e apaga todo o historico do terminal atual.

## Historico rolavel do Shell

O Shell preserva as ultimas 200 linhas de saida textual. A rolagem funciona
somente quando nenhuma aplicacao esta com foco:

- `Seta para cima` / `Seta para baixo`: navega uma linha;
- `Page Up` / `Page Down`: navega uma pagina;
- `Home` / `End`: vai ao inicio ou ao fim do historico.

Ao digitar, apagar ou enviar um comando, o Shell volta automaticamente ao fim
do historico para manter o prompt visivel. O comando `clear` remove tambem as
linhas armazenadas.

## `ls`
Lista todos os arquivos no disco FAT12.

```
zephyr> ls
Arquivos no disco:
  ARQUIVO.TXT  128 bytes
  DADOS.DAT    256 bytes
```

## `cat <arquivo>`
Exibe o conteúdo de um arquivo de texto.

```
zephyr> cat ARQUIVO.TXT
Olá, este é o conteúdo do arquivo!
```

## `echo <texto>`
Imprime texto na tela.

```
zephyr> echo Ola Mundo
Ola Mundo
```

## `mem`
Mostra informações de memória.

```
zephyr> mem
Memoria:
  Total: 128 MB
  Livre: 120 MB
  Usada: 8 MB
```

## `procs`
Lista todos os processos ativos.

```
zephyr> procs
Processos ativos:
  PID 1  idle  RUNNING
  PID 2  shell  RUNNING
Total: 2 processos
```

## `threads`
Lista todas as threads ativas.

```
zephyr> threads
Threads ativas:
  TID 1  main  RUNNING
  TID 2  timer  BLOCKED
Total: 2 threads
```

## `uptime`
Mostra o tempo desde que o sistema foi ligado.

```
zephyr> uptime
Uptime: 2h 30m 15s
```

## `beep [frequência] [duração]`
Toca um beep pelo PC Speaker.

```
zephyr> beep          → Beep padrão (800Hz, 200ms)
zephyr> beep 440 500  → 440 Hz por 500ms
```

## `melody`
Toca uma escala musical (C4→C5).

```
zephyr> melody
Tocando melodia...
Melodia concluida!
```

## `reboot`
Reinicia o computador.

## `shutdown`
Desliga o computador (para o CPU).

## `mouse`
Mostra o status atual do mouse PS/2 (posição X/Y e botões pressionados).

```
zephyr> mouse
X=100 Y=200 Buttons=0x01
```

## `guitest`
Testa as primitivas gráficas 2D do módulo GUI (janela, botões, texto).

```
zephyr> guitest
```

## `guimode classic|modern`
Altera o modo de interface do sistema (Classic TUI ou GUI Moderna) dinamicamente, demonstrando o fallback visual sem afetar a arquitetura interna do kernel.

```
zephyr> guimode modern
```

## `health`
Exibe métricas detalhadas do kernel, estado do recovery, paginação, processos e saúde estrutural da arquitetura.

Use `Page Up`, `Page Down`, `Home` e `End` para consultar toda a saida quando
o relatorio ocupar mais de uma tela.

```
zephyr> health
```

## `appcheck`
Testa a fachada segura da API de aplicativos, incluindo versão, console,
uptime, memória e validação de argumentos.

```
zephyr> appcheck
```

Na Fase 3, o `appcheck` tambem valida handles de arquivo, leitura
sequencial, handles invalidos, envio/recebimento IPC, PID inexistente e
tipos de mensagem invalidos. A ponte continua restrita ao ring 0.

As chamadas de arquivo usam handles opacos e leitura sequencial. As chamadas
de IPC validam o PID, o estado do processo, o tipo da mensagem e a fila.

O comando usa a ponte interna do dispatcher e testa números inválidos,
argumentos nulos e `process_exit`. O vetor `int 0x80` também está disponível
para o processo de teste ring 3 depois da inicialização segura do kernel.

## `usertest`
Cria um processo mínimo em ring 3, com diretório de páginas e stack de kernel
próprios. O teste chama `console_write`, `uptime`, `memory_info` e
`process_exit` através de `int 0x80`.

```text
zephyr> usertest
zephyr> usertest fault
```

`usertest fault` acessa uma página inválida para confirmar que uma exceção de
usuário encerra somente o processo de teste. O estado do processo e do gate
de syscall aparece no comando `health`.

## `explorer`
Abre o gerenciador de arquivos estilo ZephyrOS Explorer (TUI).

```
zephyr> explorer
```

Navegação com setas, F3 visualizar, F7 criar, F8 excluir com confirmação, F2 renomear.

## `desktop`
Ativa o ambiente desktop com ícones e menu Iniciar.

```
zephyr> desktop
```

## `taskman`
Abre o gerenciador de tarefas com monitoramento de processos, threads, CPU e memória.

```
zephyr> taskman
```

Guias: Processos, CPU, Memória e Threads.

## `edit <arquivo>`
Editor de texto com syntax highlighting e word wrap.

```
zephyr> edit TESTE.TXT
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

## `play <arquivo.wav>`
Reproduz um arquivo de áudio WAV via AC97.

```
zephyr> play MUSICA.WAV
```

## `compress on|off|status`
Gerencia o módulo de compressão de RAM.

```
zephyr> compress on      → Ativa compressão
zephyr> compress off     → Desativa
zephyr> compress status  → Mostra estatísticas
```

## `stats`
Mostra estatísticas detalhadas de compressão.

```
zephyr> stats
```

Exibe: total de compressões, bytes comprimidos, espaço economizado.

## `view <arquivo.bmp>`
Exibe uma imagem BMP na tela (modo VESA).

```
zephyr> view IMAGEM.BMP
```

## `stop`
Para a reprodução de mídia (áudio WAV em reprodução).

```
zephyr> stop
```

## `wm`
Abre o gerenciador de janelas (Window Manager).

```
zephyr> wm
```

## `taskcfg`
Configura a barra de tarefas (posição, tamanho, fixação).

```
zephyr> taskcfg
```

## `settings`
Abre o sistema de configurações do ZephyrOS.

```
zephyr> settings
```

Categorias: Tela, Barra de Tarefas, Janelas, Ícones, Sistema, Som, Sobre.
