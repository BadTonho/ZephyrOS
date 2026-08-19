# Roadmap 09 - Funcionalidades aplicáveis

## Objetivo

Melhorar o ZephyrOS com funcionalidades e contratos próprios, sempre de forma
compatível com a arquitetura atual.

Este roadmap não adiciona outro sistema ao projeto, não cria dependência
externa e não autoriza copiar código, APIs, drivers ou estruturas de terceiros.
Ele registra ideias de engenharia que serão reimplementadas no ZephyrOS quando
forem necessárias.

## Resumo de progresso

- [x] R0 - análise arquitetural e seleção das funcionalidades aplicáveis.
- [ ] R1 - observabilidade e log circular (implementada; validação pendente).
- [ ] R2 - serviço de temporizadores canceláveis.
- [ ] R3 - espera por eventos, timeout e cancelamento.
- [ ] R4 - fila de trabalho cooperativa.
- [ ] R5 - modelo unificado de dispositivos.
- [ ] R6 - fila de requisições de bloco.
- [ ] R7 - cache de caminhos e resolução de nomes.
- [ ] R8 - contabilidade e organização do scheduler.

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

Implementação concluída no código e na documentação. A fase permanece aberta
até a validação manual do usuário no QEMU com `log status`, `log tail`,
`log level`, `log clear`, entradas inválidas, repetição exponencial e
`log check`, seguida por `q2check`, `regcheck full`, `memcheck` e smoke tests
nos modos Simple e Classic.

### Critério de saída

O log continua aceitando novas mensagens quando o buffer está cheio, mantém a
ordem das mensagens recentes, informa descartes e não bloqueia uma IRQ ou o
Shell. Falhas de inicialização e recuperação continuam registradas.

## R2 - Serviço de temporizadores canceláveis

### Implementação

- [ ] Criar timer de disparo único e timer periódico.
- [ ] Usar prazo monotônico absoluto, sem cada módulo calcular sua própria
  conversão de segundos para ticks.
- [ ] Adicionar criação, início, cancelamento, consulta e destruição segura.
- [ ] Impedir callback depois da destruição do proprietário.
- [ ] Registrar timeout, cancelamento, atraso e execução do callback.
- [ ] Migrar gradualmente retries de rede, ATA, atualização e futuras operações
  USB.
- [ ] Adicionar comando Shell de status e diagnóstico.

### Critério de saída

Timers cancelados não executam callbacks antigos, timeouts não dependem de
loops ocupados e a regressão existente mantém seus prazos atuais.

## R3 - Espera por eventos, timeout e cancelamento

### Implementação

- [ ] Definir um canal de espera com condição, proprietário e motivo.
- [ ] Permitir bloquear até evento, prazo ou cancelamento.
- [ ] Permitir acordar uma tarefa ou todas as tarefas de um canal.
- [ ] Diferenciar conclusão, timeout, cancelamento e dispositivo ausente.
- [ ] Integrar com os estados atuais `READY`, `RUNNING`, `BLOCKED` e `ZOMBIE`.
- [ ] Manter processos e threads existentes, mas documentar um contrato comum
  de espera e desbloqueio.
- [ ] Substituir polling manual apenas em módulos que tenham cobertura de
  regressão.
- [ ] Adicionar diagnóstico de tarefas bloqueadas e seus motivos.

### Critério de saída

Uma tarefa bloqueada acorda somente por sua condição, timeout ou cancelamento;
nenhuma espera fica ocupando CPU sem necessidade; Shell, rede, índice e
interfaces continuam responsivos.

## R4 - Fila de trabalho cooperativa

### Implementação

- [ ] Criar item de trabalho com estado, proprietário, prioridade limitada e
  callback.
- [ ] Implementar fila com limite de itens e orçamento de execução por rodada.
- [ ] Permitir cancelamento antes do início e pedido de cancelamento durante
  uma operação cooperativa.
- [ ] Separar captura mínima de IRQ do processamento pesado.
- [ ] Integrar primeiro o índice de arquivos, tarefas de rede e diagnósticos.
- [ ] Reservar o mesmo contrato para enumeração USB e operações de bloco.
- [ ] Expor profundidade, maior ocupação, itens cancelados e itens falhos.
- [ ] Adicionar comando Shell de status e cancelamento controlado.

### Critério de saída

Uma tarefa longa não bloqueia teclado, mouse, Shell, Desktop ou rede. A fila
respeita o limite de memória, pode ser cancelada e produz diagnóstico quando
fica congestionada.

## R5 - Modelo unificado de dispositivos

### Implementação

- [ ] Ampliar o inventário atual com ID estável, barramento, classe, pai,
  capacidades, driver e último erro.
- [ ] Definir ciclo `DISCOVERED`, `PROBING`, `READY`, `DEGRADED`, `DISABLED`
  e `REMOVED`.
- [ ] Separar descoberta, associação do driver, inicialização, parada e
  remoção.
- [ ] Registrar recursos utilizados: IRQ, portas, memória, DMA e filas.
- [ ] Associar dispositivos PCI, ATA, rede, AC97, PS/2 e USB ao contrato
  comum sem apagar suas estruturas específicas.
- [ ] Exibir relações pai/filho e motivo de indisponibilidade.
- [ ] Manter `devices`, `device-info`, `device-scan` e `health` compatíveis.
- [ ] Adicionar diagnóstico de transições inválidas de estado.

### Critério de saída

Um dispositivo ausente, malformado ou com driver falho permanece isolado e
visível no diagnóstico. A falha não bloqueia boot, Shell, Simple, Classic ou
outros dispositivos.

## R6 - Fila de requisições de bloco

### Implementação

- [ ] Criar `block_request_t` próprio com operação, dispositivo, LBA,
  quantidade, buffer, identificador, estado e erro.
- [ ] Implementar fila FIFO limitada antes de qualquer reordenação.
- [ ] Adicionar estados `QUEUED`, `ACTIVE`, `COMPLETED`, `FAILED` e
  `CANCELLED`.
- [ ] Associar conclusão a identificador sem procurar linearmente a requisição.
- [ ] Registrar timeout, retry, latência e descartes.
- [ ] Adaptar ATA sem alterar contratos FAT ou volumes já validados.
- [ ] Usar o mesmo contrato para USB Mass Storage somente-leitura.
- [ ] Adicionar comando Shell de status das filas e requisições.

### Critério de saída

ATA continua funcionando com os mesmos limites e fixtures, requisições
inválidas falham sem escrita indevida e uma futura fonte USB pode usar a
mesma camada sem conhecer FAT12 ou FAT32.

## R7 - Cache de caminhos e resolução de nomes

### Implementação

- [ ] Criar cache limitado por volume, diretório pai e nome.
- [ ] Registrar entrada encontrada e entrada ausente separadamente.
- [ ] Associar cada entrada à geração do volume.
- [ ] Invalidar por criação, exclusão, renomeação, movimentação, montagem e
  desmontagem.
- [ ] Manter o índice global como fonte de pesquisa, sem duplicar seu contrato.
- [ ] Medir acertos, falhas, invalidações e memória consumida.
- [ ] Adicionar comando Shell para status, limpeza e reconstrução do cache.

### Critério de saída

Explorer, Shell e aplicativos resolvem caminhos atuais sem observar entradas
de um volume antigo. Cache corrompido ou cheio volta para a consulta normal
sem impedir o uso do filesystem.

## R8 - Contabilidade e organização do scheduler

### Implementação

- [ ] Manter round-robin como política inicial.
- [ ] Separar operações de inserir, remover, acordar e escolher processos.
- [ ] Reduzir a dependência de varreduras completas quando a tabela crescer.
- [ ] Registrar tempo executando, pronto, bloqueado e latência de despertar.
- [ ] Registrar motivo de bloqueio e número de preempções por processo.
- [ ] Adicionar fila explícita de tarefas prontas, com limite e invariantes.
- [ ] Integrar as métricas ao Task Manager, `kmetrics` e `schedcheck`.
- [ ] Só testar outra política depois de existir uma linha de base confiável.

### Critério de saída

O scheduler mantém as invariantes atuais, não perde processos acordados,
mede a distribuição de CPU e permite comparar mudanças sem alterar boot,
syscalls ou o contrato dos aplicativos.

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
- regressão Simple e Classic;
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
- `src/process/process.c` - processos, bloqueio e scheduler.
- `src/thread/thread.c` - threads e bloqueio temporizado.
- `src/core/device_manager.c` - inventário e estados atuais de dispositivos.
- `src/fs/file_index.c` - índice cooperativo e polling por orçamento.
- `src/drivers/ata.c` - leitura, escrita, retries e timeouts ATA.
- `docs/roadmaps/08-evolucao-da-plataforma.md` - volumes, USB e conectividade.

## Estado

Roadmap próprio do ZephyrOS, criado para orientar melhorias incrementais sem
substituir a arquitetura existente. R0 registra a análise; R1 está implementada
e aguarda validação manual, enquanto R2-R8 ainda não foram implementadas.
