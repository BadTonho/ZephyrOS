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

Neste roadmap, `userland básico` significa a base de execução dos processos e
serviços nativos. Shell, GUI e aplicativos de uso final permanecem organizados
no Roadmap 22.

## Escopo

- entrada do kernel, exceções, IRQs, PIT e estado de interrupções;
- heap, PMM, paging, VMA e caches já existentes;
- PID, generation, scheduler, Idle, processos ring 0/ring 3 e threads;
- criação, bloqueio, sinais, zombie, reaping e encerramento;
- IPC, pipes, sockets, filas de espera e workqueue;
- supervisor mínimo de serviços nativos, dependências de inicialização,
  estados de falha e recuperação;
- limites de processos, memória, descritores, filas e argumentos;
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

- [x] Confirmar sequência de inicialização, estado de interrupções, GDT/TSS,
  IDT, PIC e PIT.
- [x] Garantir que exceções publiquem diagnóstico e não corrompam o estado
  quando a recuperação for possível.
- [x] Validar stacks de bootstrap, kernel e processos em todas as transições.
- [x] Definir invariantes de contexto, owner e estado para cada entrada do
  scheduler.
- [x] Testar entradas inesperadas, reentrada e retorno de handlers.

### KRN2 — Memória e isolamento

- [x] Auditar heap, PMM, paging, VMA e caches para overflow, double free,
  vazamento, uso após liberação e referências órfãs.
- [x] Validar mapeamentos de kernel, userland, VGA, buffers e páginas de
  dispositivos.
- [x] Confirmar que page fault de userland não corrompa o kernel nem outro
  processo.
- [x] Definir limites de heap, páginas, argumentos, stacks e alocações de cada
  processo.
- [x] Definir política para falta de memória, encerramento por limite e
  diagnóstico do processo que exceder recursos.
- [x] Confirmar que endereços e objetos privados do kernel não sejam publicados
  em interfaces de processo ou diagnósticos.
- [x] Repetir `memcheck` depois de criação, falha, encerramento e reutilização
  de PID.

#### KRN2.1 - Auditoria e reforço dos invariantes de memória

- [x] Reforçar validações de overflow, alinhamento, limites, foreign free,
  double free, rollback, ownership e contadores em PMM, heap, paging, VMA e
  SLAB.
- [x] Cobrir falhas de mapeamento, cópia de usuário atravessando páginas,
  VMAs sobrepostas, `munmap` parcial e isolamento de diretórios nos testes
  host e QEMU.
- [x] Confirmar execução sem falha funcional nos casos de page fault de memória,
  paging/VMA, estresse de kernel e assembly do TST7.
- [x] Reexecutar o gate TST7 com o comparador de duração sem regressão; quotas
  por processo, OOM e encerramento por limite foram tratados na KRN2.2 abaixo.

#### KRN2.2 - Quotas, OOM e diagnóstico por processo

- [x] Adicionar controlador privado por `PID + generation`, sem alterar
  `process_t`, snapshots públicos, syscalls, ABI, scheduler ou bootloader.
- [x] Aplicar limites de 128 páginas residentes, 1 MiB anônimo, 16 VMAs
  dinâmicas, 8 argumentos/511 bytes e os limites atuais de stack e imagem.
- [x] Rejeitar `mmap`, page fault e alocações físicas acima da quota com
  códigos canônicos e rollback completo; encerrar apenas o processo que sofreu
  o fault irrecuperável.
- [x] Atualizar a contabilidade somente após liberação real e validar destroy,
  reap, reutilização de PID e generation incorreta.
- [x] Expor somente métricas numéricas seguras no `/proc/<pid>/status`, sem
  endereços físicos, ponteiros ou objetos privados.
- [x] Cobrir quotas, falhas, OOM, isolamento e baseline do controlador nos
  testes host, QEMU, catálogo e TST7.
- [x] Confirmar `make q3check`, build limpo, matriz host, paging/VMA,
  fault-memory, stress-kernel, TST7 completo e catálogo estrito.

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
- [ ] Definir semântica de pai/filho, `wait`, status de saída, órfãos e reaper
  principal.
- [ ] Revalidar PID + generation em ações administrativas, callbacks e eventos
  atrasados.
- [ ] Garantir que descritores, snapshots e ações abertas não sejam
  redirecionados para outro processo após reutilização de PID.
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
- [ ] Definir o supervisor dos serviços nativos, sem confundi-lo com o PID 0
  Idle, com estados `STARTING`, `READY`, `FAILED` e `STOPPED`.
- [ ] Ordenar inicialização, dependências, encerramento e reativação de
  serviços sem deixar o Shell sem caminho de recuperação.
- [ ] Recolher órfãos e publicar falhas de serviços sem manter ponteiros
  persistentes para processos encerrados.
- [ ] Fazer System, Shell, Desktop e kworker bloquearem quando não houver
  trabalho imediato.
- [ ] Validar que uma falha de serviço não deixe o sistema sem Shell ou saída
  textual.

### KRN6 — Integração e diagnóstico

- [ ] Integrar `schedcheck`, `memcheck`, `health`, `regcheck full` e métricas de
  kernel sem alterar o estado produtivo dos testes.
- [ ] Validar pressão de processos, memória, filas, IRQs e interrupções.
- [ ] Confirmar limpeza após ciclos repetidos de aplicativos e serviços.
- [ ] Exercitar falha, reinício, degradação e modo de recuperação do supervisor
  de serviços.
- [ ] Registrar falhas na camada que possui contexto e usar os códigos
  canônicos de `errors.h`.
- [ ] Produzir uma matriz de comportamento para boot normal, falha de serviço,
  falta de memória e ausência de hardware.

## Contratos

- Nenhum ponteiro privado do kernel atravessa o userland.
- O supervisor de serviços possui identidade e ciclo de vida distintos do PID 0
  Idle.
- Processos possuem limites e credenciais que podem ser verificados pelo
  Roadmap 19 sem expor estruturas privadas.
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
- Uma falha de serviço é isolada, diagnosticada e recuperável sem perder o
  supervisor ou a saída textual.
- Diagnósticos e métricas concordam com o estado real do scheduler e da memória.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário deverá executar os
gates do projeto e a matriz de processos, memória, IRQ, IPC, Shell, Simple e
Classic, registrando a evidência antes de concluir qualquer fase.
