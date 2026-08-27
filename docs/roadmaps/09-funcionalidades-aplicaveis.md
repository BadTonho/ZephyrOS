# Roadmap 09 - Funcionalidades aplicáveis

## Objetivo

Melhorar o ZephyrOS com funcionalidades e contratos próprios, sempre de forma
compatível com a arquitetura atual.

Este roadmap não adiciona outro sistema ao projeto, não cria dependência
externa e não autoriza copiar código, APIs, drivers ou estruturas de terceiros.
Ele registra ideias de engenharia que serão reimplementadas no ZephyrOS quando
forem necessárias.

## Separação entre apresentação e função

- [x] Tratar Classic e Simple como superfícies de apresentação que consomem
  serviços de domínio, sem tornar vídeo, GUI ou Shell a fonte de verdade das
  capacidades do sistema.
- [x] Permitir que diagnósticos do Shell e interfaces gráficas consultem as
  mesmas APIs públicas, mantendo a validação funcional independente do modo
  visual.
- [ ] Continuar a migração incremental de handlers legados que ainda misturam
  operação, formatação e entrada, sem quebrar os contratos públicos existentes.

Diretriz transferida do Roadmap 04 e registrada aqui em:
2026-08-22 09:18 (America/Sao_Paulo).

## R9 - Perfil de distribuição e composição do sistema

- [ ] Definir um manifesto versionado de distribuição com identidade, versão,
  arquitetura base, perfil visual, configurações padrão, conjunto de pacotes e
  políticas de atualização e confiança.
- [ ] Permitir que uma distribuição componha temas, assets, aplicativos e
  padrões por contratos públicos, sem duplicar ou modificar a lógica do kernel
  e dos serviços de domínio.
- [ ] Separar claramente a base ZephyrOS, o perfil da distribuição, pacotes
  opcionais e dados do usuário, preservando rollback e compatibilidade.
- [ ] Criar uma matriz de compatibilidade que valide o perfil em Classic,
  Simple, Shell, updater, contratos públicos e serviços essenciais.

Registro da frente futura: 2026-08-22 09:21 (America/Sao_Paulo).

## Resumo de progresso

- [x] R0 - análise arquitetural e seleção das funcionalidades aplicáveis.
- [x] R1 - observabilidade e log circular (implementada e validada).
- [x] R2 - serviço de temporizadores canceláveis (implementado e validado).
- [x] R3 - espera por eventos, timeout e cancelamento (implementada e validada).
- [ ] R4 - fila de trabalho cooperativa.
- [ ] R5 - modelo unificado de dispositivos.
- [ ] R6 - fila de requisições de bloco.
- [ ] R7 - cache de caminhos e resolução de nomes.
- [ ] R8 - contabilidade e organização do scheduler.
- [ ] R9 - perfil de distribuição e composição do sistema.

## Atalhos

- [Catálogo de funcionalidades](../02-arquitetura/funcionalidades-aplicaveis.md)
- [Roadmap 08 - Evolução da Plataforma](08-evolucao-da-plataforma.md)
- [Contrato público de headers](../qualidade/contratos-publicos.md)
- [Métricas de otimização](../qualidade/metricas.md)
- [Índice da documentação](../indice.md)

## Estado atual

O ZephyrOS já possui bootloader, kernel, processos, threads, paging, syscalls,
FAT12, FAT32, volumes ATA, índice de arquivos, rede IPv4/TCP/HTTP, PCI,
mouse PS/2, teclado, VESA, interfaces Simple e Classic, Desktop, Explorer,
App Store e atualização ZUPD/ZUM1.

O Roadmap 09 deve ampliar essa base sem reabrir contratos validados do boot,
filesystem, rede, atualização e interface. As fases EP1, EP2 e EP3 do Roadmap
08 continuam sendo a fonte de verdade para suas respectivas capacidades.

## Ordem de dependência

1. R1 - observabilidade e log circular.
2. R2 - temporizadores com prazo e cancelamento.
3. R3 - espera por condição e evento.
4. R4 - fila de trabalho cooperativa.
5. R5 - modelo comum de dispositivos.
6. R6 - requisições de bloco para ATA e USB.
7. R7 - cache de caminhos sobre os volumes existentes.
8. R8 - contabilidade e organização do scheduler.
9. R9 - perfil de distribuição, depois da estabilização dos contratos públicos
   e da base de pacotes.

R5 pode receber manutenção incremental durante as fases USB e conectividade do
Roadmap 08. R6 só deve avançar depois de R2, R3 e R5 estarem estáveis.

## R1 - Observabilidade e log circular

### Implementação

- [x] Trocar o buffer linear por um buffer circular limitado.
- [x] Adicionar sequência monotônica, tick, módulo, nível e código de erro.
- [x] Contabilizar mensagens descartadas e mensagens repetidas.
- [x] Separar nível mínimo de armazenamento do nível de exibição.
- [x] Criar consulta de últimas mensagens e estatísticas do buffer.
- [x] Aplicar agrupamento genérico aos logs consecutivos de qualquer módulo.
- [x] Preservar o formato legível no console Simple e Classic.
- [x] Adicionar comando Shell de consulta, configuração, teste e limpeza.

### Estado da entrega

Implementação e validação manual concluídas. O usuário confirmou no QEMU
`log status`, `log tail`, `log level`, `log clear`, sintaxe inválida e
`log check` com oito casos aprovados. `q2check`, `regcheck full` e `memcheck`
também terminaram em `OK`, preservando o Shell e os modos Simple e Classic.

### Critério de saída

O log continua aceitando novas mensagens quando o buffer está cheio, mantém a
ordem das mensagens recentes, informa descartes e não bloqueia uma IRQ ou o
Shell. Falhas de inicialização e recuperação continuam registradas.

## R2 - Serviço de temporizadores canceláveis

### Implementação

- [x] Criar timer de disparo único e timer periódico.
- [x] Usar prazo monotônico absoluto, sem cada módulo calcular sua própria
  conversão de segundos para ticks.
- [x] Adicionar criação, início, cancelamento, consulta e destruição segura.
- [x] Impedir callback depois da destruição do proprietário.
- [x] Registrar timeout, cancelamento, atraso e execução do callback.
- [x] Migrar o timeout do ICMP como primeiro consumidor real do serviço.
- [x] Adicionar comando Shell de status, listagem e diagnóstico.
- [ ] Migrar gradualmente os demais retries de rede, ATA, atualização e futuras
  operações USB depois da validação do piloto ICMP.

### Estado da entrega

R2 está implementada no código e na documentação, com validação manual
concluída no QEMU. O PIT permanece em 50 Hz; a IRQ somente marca vencimentos e
o processo System despacha até oito callbacks depois do polling de rede. O
autoteste usa tabelas privadas e o `regcheck` inclui apenas a validação
estrutural somente-leitura.

No QEMU foram validados `timer status`, `timer list`, `timer check`, ping com
reply e timeout, `net check qemu`, `q2check`, `regcheck full`, `memcheck`,
`log check` e o smoke test dos modos Simple e Classic.

### Critério de saída

Timers cancelados não executam callbacks antigos, timeouts não dependem de
loops ocupados e a regressão existente mantém seus prazos atuais. Critério
validado no QEMU.

## R3 - Espera por eventos, timeout e cancelamento

### Implementação

- [x] Definir um canal de espera com condição, proprietário e motivo.
- [x] Permitir bloquear até evento, prazo ou cancelamento.
- [x] Permitir acordar uma tarefa ou todas as tarefas de um canal.
- [x] Diferenciar conclusão, timeout, cancelamento e dispositivo ausente.
- [x] Integrar com os estados atuais `READY`, `RUNNING`, `BLOCKED` e `ZOMBIE`.
- [x] Manter processos e threads existentes, mas documentar um contrato comum
  de espera e desbloqueio.
- [x] Substituir polling manual apenas em módulos que tenham cobertura de
  regressão.
- [x] Adicionar diagnóstico de tarefas bloqueadas e seus motivos.

### Estado da entrega

R3 está implementada e validada manualmente no QEMU. `wait status`, `wait
list`, `wait check`, `q2check`, `regcheck full`, `memcheck` e `log check`
concluíram sem falhas, e o smoke test Simple/Classic preservou o retorno ao
Shell.

### Critério de saída

Uma tarefa bloqueada acorda somente por sua condição, timeout, cancelamento ou
indisponibilidade do recurso;
nenhuma espera fica ocupando CPU sem necessidade; Shell, rede, índice e
interfaces continuam responsivos. O código e o `wait check` estão concluídos;
a validação de integração no QEMU também foi concluída.

## R4 - Fila de trabalho cooperativa (Mapeada para Roadmap 12)

A infraestrutura de fila de trabalho do kernel foi consolidada no [Roadmap 12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md#sync3---filas-de-trabalho-do-kernel-kernel-workqueues), onde atua em conjunto com a divisão de interrupções Top-Half/Bottom-Half e o despachante de tarefas assíncronas do kernel (`kworker`).

A SYNC1 implementa apenas o primeiro pré-requisito: Bottom-Halfs limitados
executados pelo processo System. Sua otimização sob `regcheck full` e a
eliminação do overflow PS/2 em estresse foram adiadas para a v1.0.0. R4
permanece pendente porque a `kworker`, a fila cooperativa genérica e os
trabalhos atrasados pertencem à SYNC3.

## R5 - Modelo unificado de dispositivos (Mapeado para Roadmap 15)

O modelo hierárquico e ciclo de vida de dispositivos (`DISCOVERED`, `READY`, `DEGRADED`, `DISABLED`) foi consolidado no [Roadmap 15 - Introspeccao e Pseudo-Filesystems](15-introspeccao-e-pseudo-fs.md#proc3---mapeamento-de-sys-para-hardware), integrando a árvore de barramentos e periféricos com o pseudo-filesystem `/sys`.

## R6 - Fila de requisições de bloco (Mapeada para Roadmap 13)

A fila unificada de requisições de bloco para ATA e USB foi integrada diretamente à arquitetura da Block Layer no [Roadmap 13 - Armazenamento e Buffer Cache](13-armazenamento-e-buffer-cache.md#blk1---fila-unificada-de-requisicoes-de-bloco), conectando o agendamento de I/O ao Buffer Cache com dirty pages.

## R7 - Cache de caminhos e resolução de nomes (Mapeado para Roadmap 10)

O cache de resolução de caminhos (`dentry cache`) foi consolidado como parte integrante da camada de VFS no [Roadmap 10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md#vfs2---tabela-de-montagem-e-caminhos-universais), resolvendo nós e pontos de montagem diretamente nos descritores virtuais.

## R8 - Contabilidade e organização do scheduler (Mapeada para Roadmap 12)

O gerenciamento de contabilidade de tempo de CPU, filas explícitas de tarefas prontas e eliminação de polling foi consolidado no [Roadmap 12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md#sync2---primitivas-de-espera-sem-busy-waiting-wait-queues).

## Limitações e fora de escopo

- Não alterar `src/boot/boot.asm`.
- Não substituir o kernel ou qualquer módulo por outro sistema.
- Não importar código, biblioteca, driver ou API externa.
- Não implementar SMP, RCU, módulos carregáveis ou namespaces nesta frente.
- Não trocar o round-robin por um scheduler avançado sem métricas próprias.
- Não criar VFS completo antes de estabilizar volumes, espera e I/O de bloco.
- Não adicionar escrita em USB ou alterar partições durante a primeira entrega.
- Não remover o fallback Simple nem reduzir a cobertura Classic.

## Validação por fase

Toda fase que alterar código deve incluir:

- logs e códigos de erro para falhas;
- comando Shell de consulta, teste ou execução;
- autoteste determinístico quando possível;
- cobertura de limite, timeout, cancelamento e recurso ausente;
- regressão no modo Classic; o fallback Simple só entra quando a mudança o
  afetar diretamente;
- `health`, `memcheck` e `regcheck full` quando aplicável.

A validação final será executada pelo usuário conforme `AGENTS.md`:

```text
make q3check
make clean && make
make run
```

## Arquivos relacionados

- `src/core/log.c` - log atual e buffer linear.
- `src/drivers/timer.c` - ticks e IRQ de timer.
- `src/core/wait.c` - canais, deadlines, motivos e autoteste privado R3.
- `src/process/process.c` - processos, bloqueio e scheduler.
- `src/process/ipc.c` - fila de mensagens e espera do consumidor IPC.
- `src/thread/thread.c` - threads e bloqueio temporizado.
- `src/shell/shell.c` - comandos `wait status`, `wait list` e `wait check`.
- `src/core/device_manager.c` - inventário e estados atuais de dispositivos.
- `src/fs/file_index.c` - índice cooperativo e polling por orçamento.
- `src/drivers/ata.c` - leitura, escrita, retries e timeouts ATA.
- `docs/roadmaps/08-evolucao-da-plataforma.md` - volumes, USB e conectividade.

## Estado

Roadmap próprio do ZephyrOS, criado para orientar melhorias incrementais sem
substituir a arquitetura existente. R0, R1, R2 e R3 estão concluídas e
validadas; R4-R9 ainda não foram implementadas.
