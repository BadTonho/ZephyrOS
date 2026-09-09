# Auditoria SEC3 - Ciclo de vida e isolamento de processos

Este documento registra a auditoria incremental da SEC3 do Roadmap 19. A
identidade privada usada nas operações assíncronas é sempre `PID +
process_t.event_generation`. O PID isolado continua válido somente para APIs
legadas de operação imediata; nenhum ponteiro de processo é mantido em
callbacks, resultados pendentes ou contextos de espera.

## Regras de identidade e estado

- `event_generation` identifica a instância do processo e não é reutilizada
  quando o PID volta ao pool.
- A operação que recebe uma geração valida o PID, a geração e o estado antes
  de modificar o processo. `ERR_AGAIN` indica que a instância mudou;
  `ERR_NOT_FOUND` indica PID ausente ou processo inativo.
- `READY`, `RUNNING` e `BLOCKED` podem chegar a `ZOMBIE` uma única vez.
  Encerramentos repetidos sobre um zombie retornam sucesso sem repetir
  liberação, notificação ou `SIGCHLD`.
- O reparenting e a notificação de saída acontecem antes de o slot e o PID
  serem liberados. Um callback com geração antiga é descartado sem alterar
  estado persistente.
- PID 0/Idle, o processo atual e processos ring 0 permanecem protegidos contra
  cancelamento, sinal fatal ou reaping indevido.

## Matriz de fronteiras SEC3

| Entrada ou operação | Identidade e estado validados | Ownership e ponteiros | Callback/efeito tardio | Erro canônico | Teste essencial |
|---|---|---|---|---|---|
| Criação e bootstrap de processo | PID e geração são publicados antes do callback; pai precisa estar ativo | `process_t` permanece privado do kernel | Callback de criação com geração incorreta é rejeitado | `ERR_NOT_FOUND`/`ERR_AGAIN` no callback | `test-process-host`, `test-process-signal-host` |
| Cancelamento e terminação por sinal | PID + geração antes de alterar estado; zombie é idempotente | Recursos pertencem ao processo e só são liberados no caminho validado | Sinal enviado a zombie, ring 0 ou PID reutilizado não atinge a nova instância | `ERR_UNAVAILABLE`, `ERR_NOT_FOUND`, `ERR_AGAIN` | `test-process-host`, `test-process-signal-host` |
| `SIGCHLD` e saída | Geração do filho e do pai são revalidadas | Pai e filho são consultados somente durante a operação | `signal_exit_notified` impede duplicação; callback antigo não reparenta | `ERR_NOT_FOUND`/`ERR_AGAIN` como diagnóstico interno | `test-process-signal-host` |
| Reparenting e destruição | PID + geração do processo destruído | Waiters, IPC, descritores, foco, VMA, páginas, threads e recursos seguem seus proprietários | Reparenting ocorre antes da liberação do slot/PID | Falha preserva o estado observável | `test-process-host`, `test-process-resource-host`, `test-thread-host` |
| Encerramento por energia | Snapshot contém geração e é comparado antes do sinal | Snapshot é cópia; nenhum ponteiro sobrevive ao loop | Processo trocado durante o shutdown é ignorado ou rejeitado | `ERR_AGAIN`/`ERR_NOT_FOUND` | `test-process-host` |
| `ipc_send` e `ipc_wait` | Espera guarda PID e geração; condição relocaliza o processo | Mensagens são copiadas; contexto da espera não retém `process_t*` | Wakeup de zombie, destruído ou identidade trocada não publica estado na nova instância | `ERR_NOT_FOUND`/`ERR_STATE` | `test-process-ipc-host` |
| App Loader pendente/ativo | Geração capturada na criação e revalidada antes de iniciar, focar, cancelar e reaping | Handles são liberados somente após confirmar a mesma instância | PID reutilizado não pode iniciar, cancelar, destruir ou recolher o aplicativo antigo | `ERR_AGAIN`/`ERR_NOT_FOUND` | `test-app-loader-host` |
| Workqueue e supervisor nativo | Registros privados já usam PID + geração | Callbacks não retêm processo; recursos são liberados pelo registro privado | Worker/serviço obsoleto é descartado sem reinício ou IPC residual | `ERR_AGAIN`/estado `STOPPED` | `test-workqueue-host`, `test-service-supervisor-host` |
| Threads e quotas | Owner PID/generation e estado de thread são conferidos | Thread, fila e quota pertencem ao processo proprietário | Thread terminada ou callback antigo não altera owner reutilizado | `ERR_NOT_FOUND`/`ERR_AGAIN` | `test-thread-host`, `test-process-resource-host` |
| Scheduler e filas | Estados `ZOMBIE`/`UNUSED` não são selecionáveis; Idle é único | Wait queue é removida antes de destruir o processo | Wakeup tardio não reativa processo encerrado | `ERR_STATE`/`ERR_NOT_FOUND` | `test-scheduling-host`, `test-process-host` |

## Contratos preservados e limites da etapa

As assinaturas legadas de syscalls, App API e `waitpid` permanecem inalteradas.
`process_t` não recebeu campos novos: `event_generation` já existente passou a
ser usado de forma consistente nas operações privadas. Os helpers de geração
para cancelamento, sinais e callbacks são internos ao kernel e mantêm os
wrappers legados baseados somente em PID para operações imediatas.

Esta etapa não implementa permissões efetivas por UID/GID, metadados de
proprietário/modo no VFS, ACL ou contas persistentes. Esses itens permanecem
reservados à SEC5. A suíte completa, QEMU, TST7 e a matriz adversarial de
fechamento continuam pendentes para o encerramento do Roadmap 19.

## Estado de validação

Os testes essenciais listados na matriz foram executados com sucesso. Os gates
`make q3check`, `make clean` e `make` também passaram para a mesma versão.
Nenhuma validação QEMU, TST7 ou suíte completa faz parte desta etapa.
