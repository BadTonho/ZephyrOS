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
  classica/moderna.

CPU real por RDTSC/PMU fica adiada ate existir calibracao confiavel e uma
politica propria; esta etapa nao altera quantum, prioridade, heap ou paging.

## Etapa K2 - Scheduler e processos (em validacao)

- [x] Revisar invariantes e pontos de troca de contexto: round-robin entre
  processos normais, Idle somente como fallback, preempcao de ring 3 e yields
  cooperativos de ring 0.
- [x] Avaliar prioridade e quantum sem remover o fallback Idle: permanece
  quantum de usuario de 1 tick e nao ha prioridade nesta etapa.
- [x] Manter a relacao processo/thread atual por nao haver dados ou requisito
  de execucao que justifique uma evolucao.
- [x] Manter encerramento de ring 3 isolado e processos essenciais protegidos;
  `schedcheck` consulta os invariantes sem criar processos.
- [ ] Validar build, diagnosticos e matriz QEMU antes de concluir a etapa.

## Etapa K3 - Memoria e paging

- [ ] Medir fragmentacao e falhas de alocacao antes de alterar o heap.
- [ ] Reforcar limites, liberacao e diagnostico de diretorios de usuario.
- [ ] Avaliar memoria por processo somente depois de uma fonte confiavel de
  contagem por alocacao.

## Etapa K4 - Otimizacao dirigida por evidencia

- [ ] Otimizar apenas gargalos repetiveis observados no QEMU.
- [ ] Preferir atualizacao parcial de renderizacao e filas coalescidas a
  aumentar complexidade do scheduler.
- [ ] Comparar antes/depois e documentar o resultado, inclusive se nao houver
  ganho perceptivel.

## Criterio de saida

Toda alteracao de kernel precisa manter boot, `health`, Shell, aplicativos
nativos, processos ring 3, modos de interface e fallbacks funcionando.
