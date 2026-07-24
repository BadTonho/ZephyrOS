# Roadmap — Sincronização e Concorrência

## Estado atual

- [x] Scheduler de processos round-robin preemptivo a 50 Hz.
- [x] Threads cooperativas com `threadtest`.
- [x] Processo System encaminha teclado e mouse por IPC ao processo em foco.
- [x] Foco com fallback para o Shell e cancelamento seguro de app ring 3.
- [x] Filas IPC fixas retornam erro controlado quando cheias.

O texto antigo deste roadmap descrevia uma arquitetura baseada em um callback
global de teclado. Esse caminho foi substituído pelo encaminhamento controlado
através de IPC e foco de processo.

## Próxima etapa

Esta frente só deve avançar depois de métricas da fundação do kernel mostrarem
um problema concreto de concorrência ou latência. A simplicidade atual é uma
decisão deliberada.

### Fase 1 — Seções críticas observáveis

- [ ] Identificar, por métrica ou falha reproduzível, estruturas realmente
  compartilhadas que precisam de proteção.
- [ ] Revisar uso de spinlocks já existentes sem manter lock durante operações
  lentas de I/O ou desenho completo.
- [ ] Garantir que logs, filas e estado de foco não sejam atualizados de modo
  inconsistente durante troca de contexto.

### Fase 2 — Entrada e filas

- [ ] Medir tamanho de fila, eventos descartados e tempo até consumo.
- [ ] Coalescer somente eventos de movimento que não alterem semântica.
- [ ] Não entregar eventos ao processo encerrado, bloqueado de forma inválida
  ou sem foco.

### Fase 3 — Evolução do scheduler

- [ ] Avaliar prioridade, quantum e bloqueio por evento apenas após a coleta
  de métricas da etapa anterior.
- [ ] Preservar Idle, fallbacks e isolamento de exceções ring 3.
- [ ] Revalidar Shell, Desktop, Explorer, Settings, Task Manager e ZAPP após
  qualquer mudança de escalonamento.

## Limites

- Não substituir IPC por callbacks globais.
- Não mudar `boot.asm` nesta frente.
- Não adicionar locks como correção preventiva sem um recurso compartilhado
  identificado.
