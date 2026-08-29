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

## Registros

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
