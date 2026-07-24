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

## Etapa K1 - Medicao antes da otimizacao

- [ ] Definir metricas de tempo de frame, tamanho de filas, uso de memoria e
  custo de atualizacao de interfaces.
- [ ] Separar metricas reais de CPU de estimativas baseadas em ticks.
- [ ] Registrar cenarios reproduziveis no QEMU antes de mudar scheduler ou
  alocador.

## Etapa K2 - Scheduler e processos

- [ ] Revisar apenas invariantes e pontos de troca de contexto comprovados por
  metricas ou falhas.
- [ ] Avaliar prioridade e quantum sem remover o fallback Idle.
- [ ] Evoluir a relacao processo/thread somente quando houver dados reais para
  exibir ou uma necessidade de execucao.
- [ ] Manter encerramento de ring 3 isolado e processos essenciais protegidos.

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
