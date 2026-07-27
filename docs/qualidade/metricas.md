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

### 2026-07-27 - High Memory, bootstrap do paging em blocos

- Cenario QEMU: executar `make clean && make`, iniciar com `make run`, usar
  `kmetrics`; repetir `kmetrics reset` e digitacao nos modos Classic e Modern.
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
- Conclusao: o custo estrutural redundante do bootstrap foi removido sem
  migrar o PMM para mapeamento sob demanda. A validacao manual final deve
  confirmar pico abaixo de 255 e zero descartes durante a digitacao normal.
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

- Cenario QEMU: mesma janela e modo VESA; `kmetrics reset`, Desktop moderno,
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
| K1-D interfaces | Para classic e modern: `kmetrics reset`; abrir/fechar Desktop, Explorer, Settings e Task Manager; `kmetrics`. | Concluido: ambos os modos testados; apresentacoes/copias VESA observadas no moderno. |

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
| K2-D interfaces | Nos modos classic e modern: abrir/fechar Desktop, Explorer, Settings e Task Manager; `schedcheck`; `kmetrics`. | Concluido: Shell e interfaces operacionais nos dois modos; invariantes aprovados. |

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
| K3-D interfaces | Nos modos classic e modern, abrir/fechar Desktop, Explorer, Settings e Task Manager; `memcheck`; `kmetrics`; `schedcheck`. | Concluido: interfaces e diagnosticos preservados nos dois modos. |
