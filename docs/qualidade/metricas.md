# Registro de metricas de otimizacao

Use este registro apenas quando uma mudanca tiver objetivo de desempenho. A
alteracao nao e aceita como otimizacao sem uma comparacao reproduzivel entre
antes e depois. Para mudancas sem esse objetivo, registre `N/D` na revisao da
mudanca; nao crie uma entrada artificial.

## Modelo de registro

### AAAA-MM-DD - resumo da mudanca

- Cenario QEMU: configuracao, comando e passos reproduziveis.
- Metrica observavel: unidade e metodo de coleta.
- Antes: valor observado na base comparada.
- Depois: valor observado apos a mudanca.
- Conclusao: ganho, empate ou regressao.
- Impacto: contratos preservados, riscos e validacao executada.

## PWR1 - Contabilidade de Idle

O PWR1 introduz `idle_ticks` e `active_ticks` em `scheduler_stats_t` como
contadores acumulados baseados no PIT de 50 Hz. A soma representa os ticks
observados desde o boot, com aritmetica de delta de 32 bits para preservar o
comportamento apos wraparound. `cpu usage reset` captura uma linha-base
privada do Shell; nao altera o scheduler nem as metricas do kernel.

`idle_ticks` mede a residencia do PID 0 no scheduler e deve coincidir com
`processes[0].total_ticks`. A porcentagem publicada por `cpu usage` e uma
estimativa de tempo ativo/ocioso do scheduler, distinta do `TCK%` historico
do Task Manager e da CPU real por RDTSC/PMU, que continua `N/D`. Janelas sem
ticks exibem `N/D` em vez de dividir por zero.

A comparacao do consumo do processo QEMU e do host ainda depende da medicao
do usuario; este documento nao infere ganho sem valores pareados no mesmo
cenario.

## Registros

### 2026-08-30 - PWR1, Idle arquitetural com HLT

- Cenario QEMU: manter Shell/Desktop ociosos, executar `cpu usage reset`,
  aguardar uma janela PIT e executar `cpu usage`; repetir com uma carga curta.
- Metrica observavel: `idle_ticks`, `active_ticks`, percentuais publicados,
  responsividade das IRQs e uso do processo QEMU no host.
- Antes: kernel_main, System e Desktop mantinham caminhos cooperativos com
  yields e `hlt` separado; nenhuma contabilidade de Idle estava publicada.
- Depois: o PID 0 possui handoff e stack próprios, o Idle usa `sti; hlt`, os
  serviços bloqueiam por tick e o scheduler publica os contadores. Valores
  pareados do host/QEMU ainda são N/D até a execução do usuário.
- Conclusao: implementação registrada; validação funcional e comparação de
  consumo pendentes dos gates e da medição QEMU do usuário.
- Impacto: não houve alteração em App API, syscalls, bootloader, paging,
  `thread_t` ou quantum de ring 3.

### 2026-08-26 - EP9.4B, publicacao FAT32 da imagem de 256 MiB

- Cenario QEMU: preparacao host da imagem usada pela matriz QEMU, incluindo a
  publicacao dos arquivos fixos no volume FAT32 antes da inicializacao.
- Metrica observavel: quantidade de ciclos completos de leitura e regravacao
  da imagem durante a injecao dos arquivos.
- Antes: 26 ciclos completos, um para cada invocacao individual do injetor.
- Depois: 1 ciclo completo com a operacao em lote `inject-files-fat32`.
- Conclusao: reducao deterministica de 26 para 1 passagem pela imagem; o tempo
  de parede depende da validacao do usuario e permanece N/D.
- Impacto: aliases, conteudo e transacoes FAT32 permanecem inalterados; o
  injetor individual continua disponivel como wrapper compativel.
  Registrado em: 2026-08-26 12:30 (America/Sao_Paulo).

### 2026-08-26 - EP9.3, alocacao FAT32 durante aplicacao ZSYS

- Cenario QEMU: `SYSTEM_UPDATE_GUIDED.img`, `update system apply --confirm`,
  cancelamento com F12/Esc apos observar a reconstrucao do indice.
- Metrica observavel: tempo ate a publicacao do slot pendente e progresso do
  job cooperativo.
- Antes: mais de 10 minutos ate o cancelamento; a reconstrucao do indice foi
  iniciada, mas a aplicacao nao publicou o pendente.
- Depois: conclusao em menos de um minuto observada pelo usuario, sem medicao
  exata em segundos (valor quantitativo N/D); foi introduzida uma dica de alocacao FAT32 por volume para
  eliminar a varredura desde o primeiro cluster a cada reserva, e a atualizacao
  do diretorio passou a ocorrer em checkpoints. A geometria das imagens tambem
  passa a usar clusters de 8 setores para reduzir operacoes FAT repetidas. Nova
  medicao depende dos gates e da matriz do usuario.
- Conclusao: melhora funcional confirmada; comparacao quantitativa permanece
  pendente ate uma medicao reproduzivel.
- Impacto: a ordem de alocacao e os contratos FAT32 permanecem inalterados;
  a dica e reinicializada junto com o inventario de storage.
  Registrado em: 2026-08-26 11:37 (America/Sao_Paulo).
  Confirmacao qualitativa registrada em: 2026-08-26 11:55 (America/Sao_Paulo).

### 2026-08-21 - Fase 5, saida longa do Shell

- Cenario QEMU: usar a mesma resolucao e executar `kmetrics reset`, `net check`,
  `job status` e `kmetrics`; repetir em `guimode simple` e em Classic com o
  terminal hospedado. Em uma segunda passagem, cancelar com `F12` depois de
  uma secao ja impressa e confirmar que o prompt retorna uma unica vez.
- Metrica observavel: apresentacoes, bytes e `max_boot` de VESA no `kmetrics`,
  alem de pendencias, falhas e filas cheias de IPC. A conclusao funcional e a
  ausencia de tela preta tambem sao criterios obrigatorios.
- Antes: `net check` podia recompor a superficie inteira do terminal para
  cada linha que rolava. A sessao que revelou a tela preta nao reteve um
  snapshot pareado de `kmetrics`; portanto, nenhum valor quantitativo e
  inferido retroativamente.
- Depois: cada `video_print()` agrupa a apresentacao de sua saida, e o
  diagnostico padrao de rede publica uma secao por passo, agendando o proximo
  despertar sem chamar `process_yield()` no meio da mesma execucao. A
  validacao quantitativa e funcional no QEMU fica pendente do usuario.
- Conclusao: pendente. A alteracao remove o caminho conhecido de redesenho
  integral por linha, mas nao declara ganho ate a comparacao reproduzivel.
- Impacto: o historico estatico cresce de 200 para 500 linhas, consumindo
  mais 76800 bytes (75 KiB) de BSS para texto e cor. APIs publicas e
  `boot.asm` permanecem inalterados.

### 2026-08-01 - MV4, aplicativos Classic Modern Dark

- Cenario QEMU: manter a mesma resolucao, `guimode classic` e escala normal.
  Para cada cena, executar `kmetrics reset`, realizar a acao e executar
  `kmetrics`: Explorer (abrir, F5, mover a selecao e fechar), Settings (abrir,
  trocar categoria e opcao sem persistir e fechar), Task Manager (abrir,
  alternar Processos/Memoria/Threads e fechar) e App Store (abrir, F5,
  selecionar entradas/detalhes e fechar).
- Metrica observavel: bytes apresentados e ticks de copia VESA do snapshot
  `kmetrics`; a comparacao usa a mesma cena, escala e estado inicial.
- Antes: a linha-base AS3 por cena nao foi retida antes do build MV4; ela nao
  foi inferida de snapshots globais ou de outra escala.
- Depois: os quatro cenarios foram executados e aprovados pelo usuario no
  QEMU em Classic/escala normal. Os valores abaixo sao os snapshots MV4 apos
  `kmetrics reset`; a apresentacao final em cada caso foi de 576 bytes e 0
  tick, abaixo da resolucao do PIT.
- Criterio: aceitar cada cena somente se bytes e ticks nao crescerem mais que
  10%. Quando a medida-base de ticks for zero, registrar `N/D` para percentual
  e justificar que o PIT nao resolveu a duracao; ainda registrar os bytes.
- Conclusao: a modernizacao MV4 foi aprovada funcionalmente nas quatro cenas;
  a comparacao quantitativa permanece `N/D`, pois a linha-base AS3 por cena
  nao foi registrada e nao pode ser reconstruida com rigor.
- Impacto: Explorer, Settings, Task Manager e App Store usam somente as
  primitivas Modern existentes no caminho Classic. O historico de 60 pontos do
  Task Manager e local a janela, e `Carga agregada` continua a leitura
  observacional baseada em `TCK%`, nao CPU real, API ou persistencia.

| Cena | Bytes antes | Bytes depois | Ticks antes | Ticks depois | Resultado |
|---|---:|---:|---:|---:|---|
| Explorer | N/D | 69951855 | N/D | N/D (ultima=0) | Funcional OK; comparacao indisponivel |
| Settings | N/D | 67887492 | N/D | N/D (ultima=0) | Funcional OK; comparacao indisponivel |
| Task Manager | N/D | 330176325 | N/D | N/D (ultima=0) | Funcional OK; comparacao indisponivel |
| App Store | N/D | 100875579 | N/D | N/D (ultima=0) | Funcional OK; comparacao indisponivel |

### 2026-07-30 - MV3, composicao visual Modern estatica

- Cenario QEMU: em `guimode classic`, executar `kmetrics reset`, abrir uma
  janela hospedada, arrasta-la entre cantos opostos dez vezes e executar
  `kmetrics`; repetir com a mesma resolucao, escala e posicao da Taskbar da
  linha-base anterior.
- Metrica observavel: apresentacoes VESA, bytes copiados e maior tempo de
  copia em ticks.
- Antes: os valores quantitativos da base MV2 nao foram retidos.
- Depois: a cena foi aprovada pelo usuario no QEMU sem flickering, rastros ou
  regressao perceptivel; os valores pareados nao foram retidos.
- Conclusao: validacao visual e funcional concluida. O efeito glass foi
  pre-mesclado no boot para nao introduzir alpha blending no caminho de cada
  frame.
- Impacto: Simple, boot, API de hospedagem e geometria de hit-testing devem
  permanecer inalterados.

### 2026-07-27 - High Memory, bootstrap do paging em blocos

- Cenario QEMU: executar `make clean && make`, iniciar com `make run`, usar
  `kmetrics`; repetir `kmetrics reset` e digitacao nos modos Simple e Classic.
- Metrica observavel: paginas identity-mapped, Page Tables e ticks de
  `paging_init()`, alem de processados, pico e descartes da fila de teclado.
- Antes: com 128 MiB e framebuffer 1024x768x32, cerca de 31275 paginas do
  bootstrap percorriam o mapeador generico; cada pagina ainda pesquisava ate
  64 registros de diretorio de usuario, chegando a aproximadamente 2 milhoes
  de comparacoes redundantes. Nao havia contador interno de ticks para essa
  etapa.
- Depois: as mesmas paginas e o mesmo contrato identity-mapped sao montados
  diretamente por tabela, sem a pesquisa de diretorios por pagina.
  `paging_boot_stats_t` torna paginas, tabelas e ticks observaveis.
- Validacao QEMU inicial (128 MiB): `paging boot: paginas=31051 tabelas=31
  ticks=0`; o tempo ficou abaixo da resolucao de 20 ms do PIT. `memcheck`,
  `schedcheck` e `regcheck` terminaram em `OK`.
- A mesma sessao revelou uma regressao independente no caminho de entrada:
  pico de `63/63` e `180` descartes. A fila fisica passa a comportar 255
  eventos e o Shell consome ate 16 eventos de teclado por rodada; a fila IPC
  continua protegida pelo limite de 31 mensagens uteis.
- Validacao QEMU apos a correcao: em uma janela de 2506 ticks, a fila terminou
  em `0/255`, com pico `6`, `0` descartes e `48` eventos processados. IPC
  registrou `48` envios/recebimentos, sem falhas ou fila cheia; `app inputtest`
  tambem confirmou o cancelamento por `F12` e a devolucao de foco ao Shell.
- A latencia visual restante foi localizada no Shell hospedado: o WM executava
  `wm_gui_draw_all()` para press e release, recompondo Desktop, janelas e
  taskbar por scancode. O pior frame observado chegou a `6` ticks. O terminal
  classic agora assume a apresentacao da entrada e acumula apenas as celulas
  da tecla e do cursor; scroll e saidas longas mantem fallback por area.
- A entrada no sistema tambem acumulava apresentacoes completas quando os logs
  da segunda metade do boot atingiam o fim da tela. Essa etapa passa a compor
  um unico frame ate a cena inicial; o panic handler força a apresentacao
  pendente antes de interromper o kernel.
- Conclusao: o custo estrutural redundante do bootstrap foi removido sem
  migrar o PMM para mapeamento sob demanda, e a digitacao normal nao apresentou
  backlog nem perda de eventos no cenario validado. A reducao visual aguarda
  comparacao manual de apresentacoes completas, bytes e ticks no QEMU.
- Impacto: enderecos High Memory, ABI ZAPP, RAM suportada, stage2 e
  `boot.asm` permanecem inalterados. O Shell deixa a atualizacao do relogio
  exclusivamente com o processo System e seu fallback.

### 2026-07-25 - K5, capacidade da imagem do kernel

- Cenario QEMU: compilar com `make clean && make`; iniciar com `make run`;
  executar `memcheck`, `health`, `schedcheck`, `app inputtest`, cancelar com
  `F12` e repetir `memcheck` e `schedcheck`.
- Metrica observavel: soma decimal de `text + data + bss` dos objetos em
  `build/*.o`, medida com `i686-elf-size`, e capacidade reservada pelo linker.
- Antes: 457488 bytes para 458752 bytes reservados; o linker interrompeu a
  imagem com `kernel exceeds reserved memory`.
- Depois: 457700 bytes para 491520 bytes reservados, com margem de 33820
  bytes; build e validacao manual no QEMU concluidos.
- Conclusao: a expansao de 32 KiB removeu o limite estrutural sem manter a
  excecao temporaria de `-Os` nos modulos de interface.
- Impacto: stage2, linker, PMM e TSS usam o mesmo mapa baixo; a stack e
  reservada no PMM e o E820 e validado antes da inicializacao. `boot.asm` nao
  foi alterado.

### 2026-07-24 - K4, cursor VESA por regioes minimas

- Cenario QEMU: mesma janela e modo VESA; `kmetrics reset`, Desktop classic,
  dez movimentos entre cantos opostos sem clique, retorno ao Shell e
  `kmetrics`.
- Metrica observavel: bytes VESA, apresentacoes parciais, `media_bytes` e
  duracao maxima em ticks.
- Antes: maior volume de bytes no mesmo cenario; os valores da sessao manual
  nao foram retidos como texto.
- Depois: menor volume de bytes, confirmado manualmente no QEMU.
- Conclusao: ganho confirmado. A inversao final de apresentacao eliminou o
  piscar observado na primeira tentativa; nao houve rastro do cursor.
- Impacto: sem mudanca de scheduler, heap, paging, App API, syscall ou
  bootloader; `regcheck` permaneceu aprovado e os snapshots nao mostraram
  processos, zumbis ou paginas de usuario residuais.

## Linha-base K1

Esta secao registra observacoes antes de qualquer otimizacao de scheduler,
heap ou paging. Ela nao e um registro de ganho de desempenho: os valores sao
referencias para comparacoes futuras. A linha-base K1 abaixo foi validada
manualmente no QEMU; cada nova comparacao deve registrar seu proprio snapshot
`kmetrics` no formato de registro deste documento.

Para cada cenario, execute `kmetrics reset`, realize os passos e execute
`kmetrics`. Registre ticks do PIT, trocas de contexto, filas, memoria e VESA.
Duracoes de apresentacao em `0` tick estao abaixo da resolucao de 20 ms do
PIT, nao indicam ausencia de custo.

| Cenario | Passos QEMU | Registro K1 |
|---|---|---|
| K1-A boot/Shell | Aguardar o boot e consultar `kmetrics`. | Concluido: snapshot de processos, filas, memoria e VESA capturado. |
| K1-B Shell/scrollback | `kmetrics reset`; `health`; `PgUp`, `PgDn` e `End`; `kmetrics`. | Concluido: scrollback navegavel e filas sem pendencia residual. |
| K1-C ring 3 | `kmetrics reset`; `app outputtest`; `app inputtest` com `F12`; `q2check`; `kmetrics`. | Concluido: deltas IPC/scheduler observados; foco restaurado e sem ZAPP/zumbi. |
| K1-D interfaces | Para simple e classic: `kmetrics reset`; abrir/fechar Desktop, Explorer, Settings e Task Manager; `kmetrics`. | Concluido: ambos os modos testados; apresentacoes/copias VESA observadas no classic. |

CPU real permanece `N/D`: `TCK%` e somente a participacao estimada nos ticks
do PIT. RDTSC/PMU exigem calibracao e ficam explicitamente adiados.

## Validacao K2 (robustez, nao otimizacao)

K2 nao reivindica ganho de desempenho; o registro de otimizacao permanece
`N/D`. A validacao usa os novos deltas de yields cooperativos, preempcoes de
ring 3 e fallbacks do Idle apenas para confirmar o contrato do scheduler.

| Cenario | Passos QEMU | Registro K2 |
|---|---|---|
| K2-A invariantes | `schedcheck`; `kmetrics`. | Concluido: todas as linhas em `OK`; quantum de usuario confirmado em 1 tick. |
| K2-B preempcao | `kmetrics reset`; `app inputtest`; aguardar; `F12`; `kmetrics`; `schedcheck`. | Concluido: preempcoes de usuario positivas; foco, prompt e invariantes restaurados. |
| K2-C regressao | `app outputtest`, `q2check`, `usertest fault`, `threadtest`, `appcheck`, `health` e `procs`. | Concluido: sem panic ou processo residual; erros deliberados de `appcheck` permaneceram controlados. |
| K2-D interfaces | Nos modos simple e classic: abrir/fechar Desktop, Explorer, Settings e Task Manager; `schedcheck`; `kmetrics`. | Concluido: Shell e interfaces operacionais nos dois modos; invariantes aprovados. |

## Validacao K3 (integridade, nao otimizacao)

K3 preserva o heap first-fit com coalescencia e o paging existente. Nao ha
meta de desempenho: a conclusao depende de integridade, restauracao do heap e
limpeza de diretorios de usuario. Fragmentacao e um diagnostico observavel,
nao um alvo numerico nesta etapa.

| Cenario | Passos QEMU | Registro K3 |
|---|---|---|
| K3-A diagnostico | `memcheck`; `memcheck invalido`; `kmetrics`; `health`; `procs`. | Concluido: cinco linhas compactas em `OK`, uso invalido controlado e nenhum processo criado. |
| K3-B ring 3 | `kmetrics reset`; `app inputtest`; `F12`; `kmetrics`; `memcheck`. | Concluido: diretorios e paginas de usuario retornaram a zero apos coleta. |
| K3-C regressao | `app outputtest`; `q2check`; `usertest fault`; `appcheck`; `threadtest`; `memcheck`. | Concluido: sem panic, ZAPP ou zumbi residual; falhas deliberadas permaneceram controladas. |
| K3-D interfaces | Nos modos simple e classic, abrir/fechar Desktop, Explorer, Settings e Task Manager; `memcheck`; `kmetrics`; `schedcheck`. | Concluido: interfaces e diagnosticos preservados nos dois modos. |

## MM4 - Metricas de fragmentacao e monitoramento

MM4 nao e uma otimizacao; o ganho de desempenho permanece `N/D`. O contrato
observavel separa as paginas fisicas em seis categorias exclusivas: `KERNEL`,
`HEAP`, `SLAB`, `PROCESS`, `BUFFER` e `FREE`. A soma das categorias deve ser
igual a `total_pages`, e `FREE` deve coincidir com o contador global de paginas
livres do PMM.

O indice de fragmentacao fisica usa o maior run livre:

```text
((free_pages - largest_free_run) * 100) / free_pages
```

O resultado e zero quando nao ha paginas livres. `isolated_free_pages` conta
runs livres de exatamente uma pagina. A fragmentacao interna do heap continua
sendo medida separadamente por `memory_get_heap_stats()`.

`memory_get_detailed_stats()` faz uma varredura sob demanda. O Task Manager
mantem o ultimo snapshot por no maximo um segundo entre atualizacoes; IRQs e
page faults nao executam a coleta. A validacao funcional MM4 permanece
pendente da execucao do usuario no QEMU, portanto esta secao registra o
contrato e nao declara valores observados.

## BLK1 - Fila de requisicoes de bloco

O BLK1 mede a fila em um snapshot cumulativo desde `block_init()`. `submitted`,
`completed`, `failed` e `cancelled` contam BIOs aceitos ou concluidos pela
camada; `merged` conta os BIOs adicionais absorvidos por uma requisicao fisica.
`read_sectors` e `write_sectors` contam setores realmente reportados como
concluidos pelo driver, inclusive conclucoes parciais antes de um erro. As
taxas medias sao calculadas sob demanda desde o inicio da camada, usando a
frequencia do timer, e valem zero quando ainda nao decorreu um tick.

`queue_depth` e `in_flight` sao instantaneos; `peak_depth` e o maior numero de
BIOs enfileirados observado. A capacidade e fixa em 32 entradas. O dispatcher
mantem FIFO e somente funde BIOs com mesmo dispositivo, operacao e flags, LBA
adjacente, buffers contiguos e limite de transferencia respeitado. Nao ha
reordenacao, bounce buffer ou fusao de FLUSH. `blkstat` e o caminho observavel
para comparar snapshots. Na validacao funcional no QEMU, a fila terminou
vazia, o pico observado foi 32, houve uma fusao e 33 cancelamentos do
autoteste, sem alteracao do inventario real.

## BLK2 - Cache de leitura de blocos

O cache reserva estaticamente 64 blocos de 512 bytes, totalizando 32 KiB de
dados, e usa 64 buckets de hash com LRU por indices. `hits` e `misses` sao
contadores cumulativos de consultas; `reads_avoided` conta setores atendidos
por hits e `physical_reads` conta setores enviados ao backend. A taxa de
acerto e calculada sob demanda como `hits * 100 / (hits + misses)` e vale zero
quando ainda nao ha acessos.

`entries`, `valid_entries`, `reading_entries`, `dirty_entries`,
`writeback_entries` e `pinned_entries` sao um snapshot instantaneo. As
metricas `evictions`, `invalidations`, `bypasses` e `errors` sao cumulativas;
`last_error` preserva o ultimo erro publicado pelo cache. Leituras agrupadas
contam cada setor fisico, enquanto uma entrada sem vitima elegivel incrementa
`bypasses` e continua diretamente pelo BLK1. `cachestat` e a interface de
observacao; `cache clear` somente remove entradas limpas e elegiveis, sem
writeback antecipado.

## BLK3 - Writeback e durabilidade

`dirty_bytes` e a soma das faixas alteradas nas entradas `DIRTY` e
`WRITEBACK`; e um snapshot e pode ser menor que 512 bytes por entrada.
`writeback_attempts`,
`writeback_completed`, `writeback_failures` e `physical_writes` sao contadores
cumulativos. O writeback periodico processa ate 8 blocos a cada 250 ticks;
quando a ocupacao suja alcanca 75%, o orcamento pode usar toda a capacidade
estatica do cache e o proximo ciclo e antecipado para o tick seguinte. Com
menos de 32 paginas livres e blocos sujos, o ciclo tambem e antecipado. O
ciclo periodico nao executa FLUSH.

`sync_operations`, `flush_operations`, `flush_unavailable` e
`degraded_syncs` registram sincronizacoes explicitas. `READY` significa que a
ultima sincronizacao nao encontrou erro nem falta de FLUSH; `DEGRADED`
significa que os dados foram gravados, mas o dispositivo nao confirmou FLUSH;
`ERROR` preserva o primeiro erro de writeback ou FLUSH e a entrada suja para
nova tentativa. `sync` global agrega os dispositivos registrados e
`fsync(fd)` limita a operacao ao volume associado ao descritor.

## BLK4 - Resiliencia e failpoints

Os failpoints nao criam contadores publicos nem alteram a ABI de metricas.
Durante os autotestes, falhas esperadas continuam incrementando os contadores
cumulativos `failed`, `errors` e `writeback_failures`; o aceite observa que
`queue_depth`, `in_flight`, `dirty_entries`, `writeback_entries`, pins e o
ultimo erro residual estejam limpos ao final da drenagem.

`blkcheck` compara `physical_writes` antes e depois da criacao de
`BLK4CHK.BIN`: o valor deve permanecer inalterado antes do `sync` e crescer
quando o writeback explicito e executado. A consistencia dos dados e medida
por SHA-256 antes e depois do sync. As fases tambem exigem inventario de bloco
inalterado e fila vazia. Ausencia de FLUSH aumenta as metricas degradadas do
BLK3, mas nao e falha do BLK4; erro de FLUSH publica `ERROR` e impede o
desligamento normal.

## NET0/NET1 - Buffers de rede

`net_buffer_stats_t` publica `active_buffers`, `peak_buffers`, `allocations`,
`frees`, `delivered`, `dropped`, `copies`, `copied_bytes`, `clones`,
`fragments`, `invalid_transitions`, `duplicate_completions`,
`ref_acquires`, `ref_releases` e `last_error`. `sk_buff_stats_t` acrescenta
ativos, pico, alocacoes, liberacoes, conclusoes, descartes, operacoes invalidas
e ultimo erro para a camada unificada. Os campos instantaneos de buffers
ativos e o pico permitem verificar ausencia de residuos depois de `net check`,
`regcheck full` e `skbstat`.

As copias medidas sao as fronteiras de entrada/saida Ethernet e as filas de
socket; o NET1 mantem o caminho síncrono e baseado em copia fallback. `clones`
e `fragments` permanecem contadores reservados e nao representam operacoes
reais. Descartes e copias normais sao informativos; transicoes invalidas,
conclusoes duplicadas, erro residual ou buffer ativo sao condicoes de
diagnostico. Nao ha alegacao de ownership DMA transferido, zero-copy real ou
comparacao de desempenho nesta etapa.

## NET2 - Sockets genericos e filas AF_UNIX

`socket_status_t` publica sockets ativos e pico, criacoes, fechamentos,
binds, conexoes, accepts, envios, recebimentos, bytes transferidos,
descartes por fila, FDs obsoletos e falhas. `socket_get_info()` fornece a
visao por entrada para `sockstat`, incluindo familia, estado, caminho ou
destino, filas e modo nao bloqueante.

As filas AF_UNIX usam `sk_buff_t` de ate 2048 bytes e limite conjunto de oito
buffers/4096 bytes por direcao. `net_buffer_stats_t` e `sk_buff_stats_t`
contabilizam as copias de entrada e saida, o pico, descartes e a ausencia de
buffers ativos depois do autoteste. Fragmentacao de mensagens longas cria
buffers independentes apenas para transporte de stream; clones, fragmentos
compartilhados e zero-copy real nao sao medidos nesta etapa.

`socket_self_test()` restaura as metricas do proprio runtime e nao altera o
inventario de NICs, sockets legados, drivers ou trafego. Falhas de invariantes,
FD residual, fila ou erro residual sao diagnosticas; `ERR_AGAIN` e descartes
normais de backpressure sao resultados observaveis, nao falhas do sistema.

## NET3 - Espera multiplexada

NET3 nao cria um contador paralelo ao servico de espera. A observacao usa
`wait_get_stats()` antes e depois de `selecttest`, alem de
`wait_queue_copy_waiters()` ao final. O delta deve mostrar chamadas de espera,
wakeups por evento e timeouts; cancelamentos aparecem quando a matriz executa
um cancelamento por sinal ou pela infraestrutura de espera. Busy-waiting nao e
aceito: o teste finito deve bloquear no canal `VFS-poll` e terminar sem evento,
enquanto escrita, leitura, fechamento de pipe, evento de socket ou mensagem IPC
devem acordar uma nova varredura.

O criterio de limpeza e nao haver `wait_info_t` com `channel_owner` igual a
`VFS-poll` depois de cada retorno, sem ponteiro de `file_t` retido e sem lock
de VFS, pipe ou socket durante o bloqueio. A validacao funcional e os valores
observados permanecem pendentes ate a execucao do usuario no QEMU.

## NET4 - Rotas e monitoramento de rede

`route_status_t` mede `entry_count`, capacidade fixa, lookups, matches, misses,
adicoes, exclusoes, substituicoes e `last_error`. O autoteste `route check`
deve restaurar a tabela e as metricas observaveis antes de retornar. A
selecao e verificada por um destino coberto por rota direta, uma rota mais
especifica e a rota default; a unidade do prefixo e bits de mascara IPv4.

`netstat` consulta os snapshots existentes de `network_manager`: por interface,
`rx_packets`, `tx_packets`, `rx_errors`, `tx_errors`, `rx_dropped` e
`rx_queue_depth`. A verificacao deve comparar esses campos com o diagnostico
Ethernet da mesma interface, sem somar novamente a visao TCP ou os sockets
AF_UNIX. O encaminhamento multi-NIC nao possui metrica nesta etapa porque
permanece explicitamente fora da implementacao.
