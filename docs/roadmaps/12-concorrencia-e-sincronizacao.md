# Roadmap 12 - Concorrencia e Sincronizacao

## Objetivo

Implementar padrões modernos de concorrência, sincronização e tratamento assíncrono no ZephyrOS, eliminando totalmente laços de espera ocupada (*busy-waiting*), separando o tratamento rápido de interrupções de hardware da execução de tarefas pesadas (*Top-Half vs. Bottom-Half*) e permitindo comunicação assíncrona robusta via sinais e filas de espera (*Wait Queues*).

## Resumo de progresso

- [x] SYNC1 - Divisão de Interrupções: concluída com dívida técnica aceita;
  otimização de `regcheck full` e eliminação do overflow sob estresse adiadas
  para a v1.0.0 em
  [`DT100-001`](../qualidade/dividas-tecnicas-v1.0.0.md#dt100-001---regcheck-full-e-entrada-ps2).
- [ ] SYNC2 - Primitivas de Espera sem Espera Ocupada: implementada; matriz
  funcional do usuario pendente.
- [ ] SYNC3 - Filas de Trabalho do Kernel (Kernel Workqueues).
- [ ] SYNC4 - Sistema de Sinais Assíncronos para Processos e Shell (`SIGINT`, `SIGTERM`, `SIGSEGV`).

## Atalhos

- [Roadmap 03 - Kernel e Desempenho](03-kernel-e-desempenho.md)
- [Roadmap 09 - Funcionalidades aplicáveis](09-funcionalidades-aplicaveis.md)
- [Roadmap 10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md)
- [Roadmap 14 - Stack de Rede Avancada](14-stack-de-rede-avancada.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- IDT configurada com ISRs de CPU (0-31) e IRQs remapeadas (32-47).
- Scheduler preemptivo por temporizador PIT a 50 Hz.
- Estados de processo: `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE`.
- Context switch seguro salvando registradores em Assembly.
- Testes cooperativos e preemptivos em `schedcheck`.

## Princípios de engenharia

- **ISRs Mínimas:** Tratadores de interrupção (Top-Half) devem durar apenas alguns microssegundos, fazendo o ACK do PIC/APIC e enfileirando dados antes de reabilitar interrupções.
- **Zero Busy-Waiting:** Nenhuma função de driver ou syscall deve executar `while (!pronto)` consumindo ciclos de CPU. Quando um dado não está pronto, a thread deve dormir em uma *Wait Queue*.
- **Desacoplamento por Sinais:** O encerramento forçado ou notificação de processos deve ser mediado por sinais (`SIGINT`, `SIGTERM`), garantindo oportunidade para descarregar buffers antes do encerramento.
- **Isolamento de Falhas:** Um erro de proteção de memória ou divisão por zero em aplicativo de usuário envia sinal ao processo em vez de congelar a CPU com tela vermelha.

## Ordem de dependência

1. SYNC1 - Top-Half e Bottom-Half no núcleo de interrupções.
2. SYNC2 - Filas de espera (*Wait Queues*) e integração com scheduler.
3. SYNC3 - Fila de trabalho diferido (*Workqueues*).
4. SYNC4 - Tratamento e despacho de sinais assíncronos.

---

## SYNC1 - Divisão de Interrupções (Top-Half & Bottom-Half)

### Implementação

- [x] Consolidar `irq_deferred` como fila estática limitada, inicializada antes
  dos drivers de IRQ e drenada pelo processo System com interrupções
  habilitadas.
- [x] Identificar trabalhos por proprietário e IRQ, coalescer agendamentos,
  solicitar reexecução durante callback e contabilizar agendamento, execução,
  cancelamento, rejeição e pico global e por linha.
- [x] Contabilizar ocorrências e handlers registrados nas 16 linhas da PIC;
  duração permanece `N/D`, sem RDTSC ou PMU nesta etapa.
- [x] Refatorar teclado e mouse PS/2: a IRQ armazena bytes brutos, e o
  Bottom-Half monta scancodes/pacotes e publica no `input core`.
- [x] Preservar `keyboard_process_events()` e `mouse_process_events()` como
  recuperação em contexto normal quando o agendamento diferido for rejeitado.
- [x] Refatorar E1000 e RTL8139: o Top-Half reconhece a causa e acumula bits; o
  Bottom-Half atualiza link/erros, drena RX e recupera o receptor. O polling
  Ethernet chama `service_pending` como fallback.
- [x] Manter ATA PIO síncrono com Top-Half mínimo, limitado à leitura de status
  nas IRQ14/IRQ15; a fila assíncrona de blocos continua pertencendo à BLK1/R6.
- [x] Corrigir o backpressure observado apos jobs cooperativos longos, como o
  `regcheck full`: o despacho deve drenar eventos de entrada continuamente,
  coalescer movimentos do mouse sem descartar transicoes de botoes e preservar
  cliques/teclas mesmo quando a saida do Shell estiver intensa. O overflow deve
  permanecer contabilizado e ter log limitado, sem inundar o console. A
  preparacao do `regcheck full` deve iniciar o job antes das verificacoes e
  ceder CPU entre as fases de inventario e validacao. A vazao do consumidor
  deve acompanhar o despacho intermediario; somente movimentos consecutivos
  equivalentes podem ser acumulados, nunca roda ou transicoes de botoes.
- [x] Priorizar uma passagem diferida no início do ciclo System e executar uma
  segunda passagem limitada depois do polling USB.

O EOI e o reconhecimento do dispositivo permanecem no Top-Half. Nenhum
callback diferido roda antes do `iret` ou na pilha da IRQ. A SYNC1 não cria a
`kworker` prevista para SYNC3 e não altera o bootloader.

### Dívida técnica aceita até a v1.0.0

Identificador canônico:
[`DT100-001`](../qualidade/dividas-tecnicas-v1.0.0.md#dt100-001---regcheck-full-e-entrada-ps2).

`irqstat check` e `regcheck full` terminam em `OK`, os trabalhos de IRQ12 são
executados sem rejeições e o mouse recupera o funcionamento ao fim do job.
Entretanto, a carga manual intensa durante `regcheck full` ainda pode saturar
o pipeline PS/2, deixar o cursor temporariamente sem atualização e descartar
pacotes. A execução mais recente registrou 25.421 ocorrências de IRQ12, 397
Bottom-Halfs, 2.548 coalescências, zero rejeições e 22.537 descartes.

Por decisão do usuario, a investigação de desempenho fica suspensa até a
v1.0.0 e passa ao Roadmap 03. A perda observada foi aceita explicitamente como
dívida técnica e não reabre a SYNC1; a etapa K5 passa a ser responsável por
eliminar `DT100-001` antes do fechamento da v1.0.0.

### Critério de saída desta entrega

Top-Halves permanecem mínimos, callbacks diferidos executam somente no processo
System, filas e diagnósticos estruturais terminam em `OK`, e o sistema recupera
o mouse depois de `regcheck full`. O requisito de zero descarte sob estresse
manual extremo foi transferido, como `DT100-001`, para K5/v1.0.0.

### Comandos Shell / Diagnóstico

- `irqstat` ou `irqstat status`: resume fila, métricas e duração `N/D`.
- `irqstat list`: lista IRQ0-IRQ15 ativas, ocorrências, handlers e Bottom-Halfs.
- `irqstat check`: valida ciclo, coalescência, reexecução, cancelamento,
  capacidade, atribuição por IRQ, contexto e invariantes em fixture privada.

`health check` apresenta rejeições/contexto inválido e `regcheck full` inclui
as invariantes somente-leitura. A matriz funcional foi executada pelo usuário;
a limitação de desempenho e overflow permanece registrada separadamente para
a v1.0.0.

---

## SYNC2 - Primitivas de Espera sem Busy-Waiting (Wait Queues)

### Implementação

- [x] Consolidar `wait_queue_head_t` como fila intrusiva FIFO e manter
  `wait_channel_t` como alias compativel com a R3.
- [x] Embutir uma entrada estatica em cada processo/thread e registrar ate 128
  filas por IDs geracionais, sem alocacao dinamica.
- [x] Implementar `init_waitqueue_head`, `wait_event`,
  `wait_event_timeout`, `wake_up` e `wake_up_all` com retorno de erro,
  revalidacao atomica da condicao com interrupcoes desabilitadas e prazo
  absoluto unico.
- [x] Fazer `wake_up` percorrer somente a fila alvo, em ordem FIFO, sem
  varrer as tabelas completas de processos e threads.
- [x] Remover a entrada exatamente uma vez em evento, timeout, cancelamento,
  indisponibilidade ou destruicao; filas ocupadas recusam reset.
- [x] Preservar os checks temporizados limitados dos schedulers e impedir
  bloqueio fora de contexto executavel ou com interrupcoes desabilitadas.
- [x] Migrar IPC, Editor, Explorer e Task Manager para espera real quando a
  fila de mensagens esta vazia, preservando sinais diretos por uma geracao
  consumida pelo processo.
- [x] Adicionar fila e eventos `CONNECTED`, `READABLE`, `EOF`, `ERROR` e
  `CLOSED` a cada socket nativo, preservando `net_socket_receive()` como API
  nao bloqueante e acrescentando `net_socket_wait()`.
- [x] Migrar `net tcp connect` para a espera do socket. O processo System
  continua executando o polling da rede e nunca bloqueia nessa fila.
- [x] Integrar invariantes a `regcheck full` e saturacao, contexto, vinculos e
  falhas de wake a `health check`.

As APIs R3 de processo/thread e o comando `wait` permanecem compativeis.
ATA PIO continua sincrono: a espera de disco depende da fila assincrona de
requisicoes prevista para BLK1/R6 e nao recebe um Bottom-Half artificial.

### Critério de saída

IPC/teclado e sockets bloqueiam a tarefa consumidora sem polling de
`process_yield`, wake-one respeita FIFO, wake-all nao deixa entradas orfas e
timeout/cancelamento/fechamento preservam os motivos. `wait check`,
`net socket check`, `regcheck full`, `health check`, `memcheck` e `log check`
devem concluir sem falhas antes de marcar a etapa como concluida.

### Comandos Shell / Diagnóstico

- `wait status|list|check`: apresenta metricas, waiters FIFO e autoteste
  privado ampliado.
- `wqinfo`: lista filas registradas, geracao, disponibilidade, ocupacao e
  ordem dos processos/threads bloqueados.
- `net socket status|table|check`: apresenta metricas de espera, waiters por
  socket e fixture privada de eventos.

### Validacao pendente do usuario

Os gates desta versao sao `make q3check`, `make clean && make` e `make run`.
No QEMU padrao, a matriz funcional e:

```text
wait status
wait list
wait check
wqinfo
net socket check
net tcp connect example.com 80
net check qemu tcp net-pci-00:03.0 example.com
regcheck full
health check
memcheck
log check
```

As coberturas adicionais usam `make run-usb-hid`,
`make run QEMU_NET_ARGS=` e
`make run QEMU_NET_ARGS="-netdev user,id=net0 -device e1000,netdev=net0 -netdev user,id=net1 -device rtl8139,netdev=net1"`.
`wqinfo foo` e `net socket foo` devem apenas registrar uso invalido. Durante
esperas e `regcheck full`, teclado, mouse PS/2/USB, Classic e Shell devem
continuar responsivos, sem waiters orfaos.

### Estado da entrega

A primeira matriz QEMU aprovou filas, sockets, rede, memoria, log e uso
invalido, mas revelou uma corrida no cancelamento F11 do `regcheck full`. A
correcao esta implementada e aguarda nova validacao do usuario; a SYNC2
permanece aberta. SYNC3, R4 e a `kworker` continuam pendentes; o bootloader
permanece inalterado.

---

## SYNC3 - Filas de Trabalho do Kernel (Kernel Workqueues)

### Implementação

- [ ] Criar uma thread especial do kernel dedicada ao despacho de tarefas diferidas (`kworker`).
- [ ] Implementar a estrutura `work_struct_t` com ponteiro para função de callback e dados:
  `void schedule_work(work_struct_t* work);`
  `void schedule_delayed_work(work_struct_t* work, uint32_t delay_ticks);`
  `int cancel_work(work_struct_t* work);`
- [ ] Migrar tarefas periódicas (flush de cache, reconciliação de conexões inativas, verificação de integridade) para a fila de trabalho unificada.

### Critério de saída

O kernel executa tarefas assíncronas em contexto de thread normal sem poluir o tratador de interrupção do temporizador.

### Comandos Shell / Diagnóstico

- `workq status`: exibe a fila de trabalhos pendentes, trabalhos executados e tempo médio de execução.

---

## SYNC4 - Sistema de Sinais Assíncronos

### Implementação

- [ ] Definir mapa de bits de sinais pendentes (`pending_signals`) e tabela de ações (`signal_actions`) em cada `process_t`.
- [ ] Definir os sinais essenciais:
  - `SIGINT` (2): interrupção pelo teclado (`Ctrl+C`).
  - `SIGKILL` (9): encerramento imediato incondicional.
  - `SIGSEGV` (11): violação de memória / Page Fault inválido.
  - `SIGTERM` (15): solicitação de encerramento amigável.
  - `SIGCHLD` (17): notificação de término de processo filho.
- [ ] Na saída de interrupções e syscalls, verificar e entregar sinais pendentes antes de retornar para ring 3.
- [ ] Implementar tratador padrão do Shell para `Ctrl+C` cancelando processos em primeiro plano sem fechar o terminal.

### Critério de saída

O usuário consegue interromper programas longos via `Ctrl+C` e o sistema trata falhas de memória de programas ring 3 finalizando o processo de forma limpa.

### Comandos Shell / Diagnóstico

- `kill -<sinal> <pid>`: envia um sinal para o processo especificado.
- `sigtest`: dispara bateria de testes de entrega, bloqueio e captura de sinais.
