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
  threadtest - Valida troca cooperativa de threads
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
  appcheck  - Testa API, arquivos, IPC e carregador ZAPP
  app run <arquivo.ZAP> [args] - Executa aplicativo ring 3 de forma assincrona
  app inputtest - Testa entrada de teclado em aplicativo ring 3
  app outputtest [fail] - Testa saida ZAPP em blocos e codigos de saida
  app argtest <texto> - Testa argumentos em aplicativo ring 3
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

## Saída de texto

O comportamento atual de `echo` é descrito na seção específica mais adiante:
ele tenta executar uma imagem ZAPP em ring 3 e usa a implementação nativa
somente como fallback seguro.

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

## `threadtest`
Cria duas threads temporarias, alterna entre elas cooperativamente e confirma
que os registradores e as stacks foram trocados na ordem esperada. O teste
limpa as threads antes de retornar ao Shell.

```
zephyr> threadtest
Teste de threads: OK
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

Na Fase 5, o `appcheck` também cria temporariamente `DEMO.ZAP`, valida o
cabecalho `ZAPP`, executa o processo ring 3 e remove o arquivo. Também testa
entry point, flags, tamanho de codigo e cabecalho invalidos. O arquivo de
demonstracao nao permanece no disco.

Na Fase 6B, também confirma a ABI de lançamento `0.3`: argumentos válidos,
ausência de argumentos, quantidade excessiva, texto grande, foco, execução e
reaproveitamento seguro do processo externo.

Na Fase 6C, a sequencia assincrona tambem valida as migracoes ZAPP internas
de `uptime` e `mem`, incluindo rejeicao de lancamento concorrente, retorno de
foco e ausencia de processos de usuario ou zumbis residuais.

O `appcheck` mistura operacoes validas com entradas deliberadamente invalidas.
Assim, `ERRO` nas chamadas de syscall, ponteiro, buffer, handle, arquivo, PID,
mensagem, argumento ou imagem ZAPP invalida e esperado se o retorno for
controlado e a suite continuar. `memory_info nulo` e os dois testes de
migracao concorrente usam comparacao de codigo esperado e devem aparecer como
`OK`. Erro em caminho valido, divergencia dessas comparacoes, falha de foco ou
limpeza, `KERNEL PANIC` ou prompt ausente/duplicado representa regressao; a
matriz completa esta em `docs/roadmaps/01-estabilizacao-e-qualidade.md`.

`file_service_indisponivel` e `loader_indisponivel` sao esperados apenas
quando o componente correspondente aparecer indisponivel no `health`.

## `app run <arquivo.ZAP> [arg1 arg2 ...]`

Executa uma imagem flat i386 em ring 3 sem bloquear o Shell.

```text
zephyr> app run DEMO.ZAP alpha beta
```

O formato possui uma pagina maxima de codigo, uma pagina maxima de dados e
uma pagina de stack. Arquivos invalidos, inexistentes ou maiores que o limite
retornam erro controlado e nao causam `panic`.

O aplicativo recebe foco automaticamente e pode obter scancodes PS/2 brutos
com `message_receive` e `APP_MESSAGE_KEYBOARD`. `Esc` continua disponivel para
o aplicativo. `F12` encerra somente o `.ZAP` externo em foco e devolve o
controle ao Shell. Argumentos sao separados por espacos ou tabs; aspas e
escapes ainda nao possuem significado especial.

Cada chamada `console_write` aceita de 1 a 1024 bytes ASCII e conclui de forma
sincrona. Um ZAPP pode enviar blocos consecutivos em ordem, mas deve tratar o
primeiro erro como final e incluir suas proprias quebras de linha. Nao ha fila,
quota total, historico de comandos ou entrada de linha para aplicativos nesta
fase; o scrollback do Shell continua limitado a 200 linhas e `F12` cancela o
aplicativo em foco.

O ciclo de vida diferencia falha ao iniciar (`ERRO`), falha isolada (`WARN`),
cancelamento por `F12` (`INFO`), termino com codigo `0` (`INFO`) e termino
normal com codigo nao-zero (`ERRO`). O codigo `0xF120` e reservado ao runtime
e nao pode ser usado diretamente por `process_exit` em um aplicativo.

## `app argtest <texto>`

Executa uma imagem interna ring 3 que exibe o texto recebido pela pagina de
lancamento. Serve para validar a passagem de argumentos sem criar arquivo no
filesystem.

```text
zephyr> app argtest alpha beta
Argumentos ZAPP: alpha beta
```

## `echo <texto>`

`echo` e a primeira funcao nativa migrada. Quando Loader, filesystem, paging
e modo usuario estao disponiveis, ele roda como imagem ZAPP ring 3 e retorna
ao Shell de forma assincrona. Se essas dependencias estiverem indisponiveis,
o comportamento nativo anterior imprime o mesmo texto como fallback.

## `app inputtest`

Cria temporariamente um `.ZAP` de diagnostico, entrega o foco a ele e remove
o arquivo logo apos o carregamento. Pressione qualquer tecla para exercitar a
fila e `Enter` para encerrar normalmente; `F12` cancela com retorno seguro ao
Shell.

## `app outputtest [fail]`

Executa uma imagem ZAPP interna que escreve nove blocos ASCII de 128 bytes,
totalizando 1152 bytes. Sem argumento, ela encerra com codigo `0`; com `fail`,
encerra normalmente com codigo `1` depois de emitir a mesma saida.

```text
zephyr> app outputtest
zephyr> app outputtest fail
```

O primeiro caso deve terminar com uma unica mensagem `INFO` e um prompt. O
segundo deve terminar com uma unica mensagem `ERRO` contendo o codigo `1`, sem
ser tratado como falha isolada ou cancelamento. Outros argumentos exibem o uso
do comando; se o loader estiver indisponivel, o Shell informa o erro controlado
e nao tenta fallback nativo.

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

Ao terminar normalmente ou por falha isolada, o Shell imprime o resultado do
teste e apresenta um novo prompt, para que o proximo comando nunca fique
misturado ao aviso assincrono do processo.

## `explorer`
Abre o gerenciador de arquivos ZephyrOS Explorer. Em `guimode modern`, usa a
janela gráfica; em `guimode classic` ou sem VESA/backbuffer, mantém a TUI.

```
zephyr> explorer
```

Navegação com setas, F3 visualizar, F7 criar, F8 excluir com confirmação, F2 renomear.

## `desktop`
Ativa o ambiente desktop com ícones e menu Iniciar.

```
zephyr> desktop
```

## `taskmgr`
Abre a TUI de diagnóstico do gerenciador de tarefas, com processos, memória e
threads. Pela taskbar ou Desktop em modo moderno, o mesmo componente abre sua
janela gráfica própria.

```
zephyr> taskmgr
```

Abas: Processos, Memória e Threads. Use `Tab`, Setas, `S`, `Enter`, `Delete`,
`F`, `R` e `Esc` conforme a interface ativa.

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
