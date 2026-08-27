# Roadmap 03 - Kernel e desempenho

## Objetivo

Fortalecer o kernel com mudancas incrementais e mensuraveis. O kernel nao sera
reescrito para perseguir otimizações teoricas: cada mudanca deve preservar os
contratos de processos, memoria, IPC e diagnostico ja validados.

## Pre-requisitos preservados

- Scheduler atual: round-robin a 50 Hz.
- Idle como caminho seguro quando nao ha processo pronto.
- Paging, TSS, CR3 e isolamento ring 3 ativos.
- `health` como diagnostico basico de processos, IPC, paging e memoria.
- `boot.asm` fora de escopo salvo autorizacao explicita.

## Etapa K1 - Medicao antes da otimizacao (validada no QEMU)

- [x] Definir `kmetrics [reset]` com tempo de apresentacao, tamanho de filas,
  uso de memoria e custo de atualizacao de interfaces.
- [x] Separar CPU real de estimativas por ticks: Task Manager usa `TCK%` e
  CPU real permanece `N/D`.
- [x] Registrar os quatro cenarios reproduziveis no QEMU antes de mudar
  scheduler ou alocador: boot/Shell, scrollback, ciclo ring 3 e interfaces
  Simple/Classic.

CPU real por RDTSC/PMU fica adiada ate existir calibracao confiavel e uma
politica propria; esta etapa nao altera quantum, prioridade, heap ou paging.

## Etapa K2 - Scheduler e processos (validada no QEMU)

- [x] Revisar invariantes e pontos de troca de contexto: round-robin entre
  processos normais, Idle somente como fallback, preempcao de ring 3 e yields
  cooperativos de ring 0.
- [x] Avaliar prioridade e quantum sem remover o fallback Idle: permanece
  quantum de usuario de 1 tick e nao ha prioridade nesta etapa.
- [x] Manter a relacao processo/thread atual por nao haver dados ou requisito
  de execucao que justifique uma evolucao.
- [x] Manter encerramento de ring 3 isolado e processos essenciais protegidos;
  `schedcheck` consulta os invariantes sem criar processos.
- [x] Validar build, diagnosticos e matriz QEMU: `schedcheck`, preempcao por
  `app inputtest`/`F12`, testes ring 3, `threadtest`, `appcheck`, `health`,
  ausencia de processos residuais e interfaces Simple/Classic.

## Etapa K3 - Memoria e paging (validada no QEMU)

- [x] Medir fragmentacao, blocos e falhas do heap sem alterar o first-fit nem
  a coalescencia; `memcheck` confirma integridade e restauracao da linha-base.
- [x] Reforcar limites e propriedade do PMM, e registrar/liberar somente
  diretorios de usuario conhecidos.
- [x] Validado no QEMU: `memcheck`, ring 3, F12, regressao e interfaces
  simple/classic; desempenho permanece `N/D`.
- [ ] Avaliar memoria por processo somente depois de uma fonte confiavel de
  contagem por alocacao.

## Etapa K4 - Otimizacao dirigida por evidencia

- [x] Otimizar a apresentacao VESA do cursor: escolher entre a uniao das
  posicoes antiga/nova ou duas regioes minimas pelo menor volume de bytes,
  sem mudar fila, callbacks ou politica de scheduler.
- [x] Expor `media_bytes` em `kmetrics` e comparar a janela manual do cursor
  no mesmo QEMU antes/depois; a reducao de bytes, e nao a quantidade bruta de
  apresentacoes, e a medida primaria.
- [x] Validar ausencia de rastro ou piscada do cursor e manter Shell, ring 3,
  diagnosticos e interfaces Simple/Classic; documentar empate ou regressao
  se nao houver ganho perceptivel.

Validado no QEMU: a comparacao manual confirmou menos bytes VESA no mesmo
cenario; a apresentacao do cursor novo antes da regiao antiga eliminou o
piscar sem deixar rastro. `regcheck` e `kmetrics` permaneceram operacionais.

## Etapa K5 - Otimizacao sistemica para a v1.0.0

Esta etapa e responsavel por quitar a divida tecnica
[`DT100-001`](../qualidade/dividas-tecnicas-v1.0.0.md#dt100-001---regcheck-full-e-entrada-ps2).

- [ ] Medir por fase o custo de `regcheck full`, incluindo varredura PCI,
  inventarios, filesystem, pacotes, Ring 3 e apresentacao de logs.
- [ ] Registrar taxa e pico de IRQ1/IRQ12, ocupacao das filas bruta,
  normalizada e do `input core`, execucoes diferidas e ciclos do processo
  System no mesmo cenario reproduzivel.
- [ ] Ajustar orcamentos, pontos de yield e coalescencia com comparacao
  antes/depois, sem ocultar descartes nem fundir teclas, roda ou transicoes de
  botoes.
- [ ] Validar `regcheck full` sob movimento, clique, arraste e roda intensos,
  com zero descarte e interfaces Classic/Shell responsivas.

Esta etapa foi reservada para a v1.0.0 por decisao do usuario. Ate la, a
lentidao e o overflow de entrada sob esse estresse permanecem limitacoes
conhecidas registradas em `DT100-001`, sem reivindicacao de ganho de
desempenho.

## Criterio de saida

Toda alteracao de kernel precisa manter boot, `health`, Shell, aplicativos
nativos, processos ring 3, modos de interface e fallbacks funcionando.
