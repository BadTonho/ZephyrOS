# Roadmap 18 — Kernel, processos e userland básico da 1.0.0

## Estado

Planejado. Esta é a primeira frente de implementação da base 1.0.0. Ela fecha
as garantias mínimas de execução sobre as quais os Roadmaps 19–24 dependerão.
Não inicia Rust e não substitui os Roadmaps 01–16, que permanecem históricos
e intocados.

## Objetivo

Garantir um kernel unicore capaz de inicializar, gerenciar memória, executar
processos isolados, encaminhar eventos, encerrar tarefas e recuperar-se de
falhas sem perder o controle do sistema.

## Escopo

- entrada do kernel, exceções, IRQs, PIT e estado de interrupções;
- heap, PMM, paging, VMA e caches já existentes;
- PID, generation, scheduler, Idle, processos ring 0/ring 3 e threads;
- criação, bloqueio, sinais, zombie, reaping e encerramento;
- IPC, pipes, sockets, filas de espera e workqueue;
- cópia segura entre userland e kernel;
- métricas e diagnósticos do ciclo de execução.

Não haverá SMP, segundo scheduler produtivo, alteração especulativa do
quantum ou migração para Rust nesta etapa.

## Dependências

- Roadmaps 01–16 concluídos como base histórica;
- [Escopo da versão 1.0.0](escopo-v1.0.0.md);
- contratos de `process.h`, `thread.h`, `errors.h`, paging e memória.

## Fases

### KRN1 — Entrada e invariantes do kernel

- [ ] Confirmar sequência de inicialização, estado de interrupções, GDT/TSS,
  IDT, PIC e PIT.
- [ ] Garantir que exceções publiquem diagnóstico e não corrompam o estado
  quando a recuperação for possível.
- [ ] Validar stacks de bootstrap, kernel e processos em todas as transições.
- [ ] Definir invariantes de contexto, owner e estado para cada entrada do
  scheduler.
- [ ] Testar entradas inesperadas, reentrada e retorno de handlers.

### KRN2 — Memória e isolamento

- [ ] Auditar heap, PMM, paging, VMA e caches para overflow, double free,
  vazamento, uso após liberação e referências órfãs.
- [ ] Validar mapeamentos de kernel, userland, VGA, buffers e páginas de
  dispositivos.
- [ ] Confirmar que page fault de userland não corrompa o kernel nem outro
  processo.
- [ ] Definir limites de heap, páginas, argumentos, stacks e alocações de cada
  processo.
- [ ] Repetir `memcheck` depois de criação, falha, encerramento e reutilização
  de PID.

### KRN3 — Scheduler e Idle

- [ ] Confirmar PID 0 como único Idle, fora do round-robin e com contexto e
  stack próprios.
- [ ] Garantir `sti; hlt` sem janela de corrida e sem polling ativo quando não
  houver trabalho.
- [ ] Manter prioridades, quantum e identidade de processos compatíveis com o
  contrato atual.
- [ ] Contabilizar ticks, trocas, wakeups, bloqueios e atividade sem logging
  por tick.
- [ ] Garantir que timer, IRQ, teclado, mouse, rede e workqueue acordem os
  consumidores corretos.

### KRN4 — Processos e threads

- [ ] Validar criação, execução, bloqueio, suspensão, retomada, término,
  zombie, reaping e falha de processo.
- [ ] Revalidar PID + generation em ações administrativas, callbacks e eventos
  atrasados.
- [ ] Impedir criação acima do limite e retornar erro sem deixar slot, stack ou
  página residual.
- [ ] Preservar ring 0 para serviços nativos e ring 3 para aplicativos, com
  tratamento explícito de chamadas fora de ordem.
- [ ] Manter `thread_t` e seu autoteste sem criar um segundo caminho produtivo
  não documentado.

### KRN5 — IPC e serviços básicos

- [ ] Testar IPC, pipes, sockets, wait queues e workqueue com fila cheia,
  cancelamento, timeout, fechamento e consumidor ausente.
- [ ] Garantir ownership explícito de mensagens, buffers, FDs e callbacks.
- [ ] Impedir que um processo encerrado receba dados ou callbacks pendentes.
- [ ] Fazer System, Shell, Desktop e kworker bloquearem quando não houver
  trabalho imediato.
- [ ] Validar que uma falha de serviço não deixe o sistema sem Shell ou saída
  textual.

### KRN6 — Integração e diagnóstico

- [ ] Integrar `schedcheck`, `memcheck`, `health`, `regcheck full` e métricas de
  kernel sem alterar o estado produtivo dos testes.
- [ ] Validar pressão de processos, memória, filas, IRQs e interrupções.
- [ ] Confirmar limpeza após ciclos repetidos de aplicativos e serviços.
- [ ] Registrar falhas na camada que possui contexto e usar os códigos
  canônicos de `errors.h`.
- [ ] Produzir uma matriz de comportamento para boot normal, falha de serviço,
  falta de memória e ausência de hardware.

## Contratos

- Nenhum ponteiro privado do kernel atravessa o userland.
- Toda aquisição possui owner e liberação ou transferência verificável.
- Toda entrada externa tem validação de tamanho, range, alinhamento e estado.
- Falhas recuperáveis retornam erro; invariantes fatais usam o mecanismo de
  panic já estabelecido.
- O caminho de fallback do Shell permanece disponível.

## Critérios de saída

- O kernel inicializa e continua operável após falhas negativas previstas.
- Processos ring 3 não acessam memória, recursos ou callbacks de outro processo.
- Não existem resíduos conhecidos após ciclos de execução e encerramento.
- O Idle reduz trabalho ativo sem perda de wakeups ou eventos.
- Diagnósticos e métricas concordam com o estado real do scheduler e da memória.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário deverá executar os
gates do projeto e a matriz de processos, memória, IRQ, IPC, Shell, Simple e
Classic, registrando a evidência antes de concluir qualquer fase.
