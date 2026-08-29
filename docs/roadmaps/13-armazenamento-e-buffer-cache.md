# Roadmap 13 - Armazenamento, Block Layer e Cache de Blocos

## Objetivo

Implementar uma camada de abstração de dispositivos de bloco (*Block Layer*) e
um cache de blocos limitado no ZephyrOS, desacoplando FAT/VFS dos drivers ATA e
USB Mass Storage. A etapa deve separar a operação lógica (`bio_request_t`) da
requisição enfileirada que o driver executa, preservar a compatibilidade das
operações síncronas atuais e definir durabilidade antes de habilitar escrita em
cache. Um page cache completo, compartilhado por arquivos e baseado em páginas
virtuais, permanece fora desta etapa.

## Resumo de progresso

- [x] BLK0 - Contrato de I/O, ownership, conclusão e durabilidade.
- [x] BLK1 - Fila unificada de requisições de bloco (`block_device_t`, `bio_request_t` e `block_request_t`).
- [x] BLK2 - Cache de blocos de leitura com estados, referências e política LRU.
- [ ] BLK3 - Writeback de blocos modificados, flush e sincronização explícita.
- [ ] BLK4 - Resiliência, integridade contra interrupção de energia e failpoint testing.

## Atalhos

- [Roadmap 08 - Evolucao da Plataforma](08-evolucao-da-plataforma.md)
- [Roadmap 09 - Funcionalidades aplicáveis](09-funcionalidades-aplicaveis.md)
- [Roadmap 10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Driver ATA PIO de 4 canais primário/secundário master/slave.
- Leitura e escrita de setores em volumes FAT12 e FAT32.
- Mecanismo transacional de atualização ZUPD com rollback.
- Validação de integridade SHA-256 e testes de failpoint.

## Princípios de engenharia

- **Desacoplamento de Hardware:** Filesystems requisitam I/O lógico através de
  `bio_request_t`; somente a camada de bloco conhece filas, capacidades e o
  contrato do driver ATA ou USB.
- **Separação de operações:** Um `bio_request_t` descreve a operação. A fila
  poderá agregá-lo em um `block_request_t`. No BLK0, o adaptador retém o uso
  do buffer até a conclusão, mas a alocação continua sob ownership do
  chamador; status, setores concluídos e erro são publicados no BIO.
- **Compatibilidade síncrona:** A API bloqueante atual usa um wrapper que
  submete uma requisição ao adaptador BLK0, que executa o callback atual e
  aguarda a conclusão antes de retornar. A espera em wait queue fica para a
  fila do BLK1; os chamadores não acessam o driver diretamente.
- **Fusão conservadora:** A primeira política é FIFO com fusão somente de
  operações adjacentes compatíveis. Reordenação e elevator só entram após
  métricas e não podem alterar a semântica de conclusão.
- **Cache limitado:** O cache de blocos tem orçamento fixo, estados explícitos,
  referências/pins e não descarta entradas sujas ou em I/O. Cache de blocos não
  é tratado como page cache nesta etapa.
- **Durabilidade explícita:** `sync`/`fsync` só retornam sucesso após as
  gravações e o flush suportado pelo dispositivo; erros de writeback não são
  ocultados.

## Ordem de dependência

1. BLK0 - Contratos de I/O, ownership e durabilidade.
2. BLK1 - Estruturas de dispositivo de bloco e fila de requisições.
3. BLK2 - Cache de blocos de leitura e algoritmo de cache LRU.
4. BLK3 - Writeback, flush e sincronização explícita.
5. BLK4 - Testes de integridade, desligamento forçado e failpoint recovery.

---

## BLK0 - Contrato de I/O e durabilidade

### Implementação

- [x] Definir `bio_request_t` como descrição de uma operação lógica, com
  dispositivo, LBA, quantidade de setores, buffer, operação, flags, callback,
  contexto e status de conclusão.
- [x] Definir `block_request_t` como objeto interno do adaptador, mantendo o
  uso do buffer até a conclusão sem liberar a memória do chamador; agregação
  de BIOs fica para a fila BLK1.
- [x] Definir estados `QUEUED`, `IN_FLIGHT`, `COMPLETED`, `CANCELLED` e
  `ERROR`, documentando timeout propagado, retry limitado, fila cheia e
  cancelamento efetivo como responsabilidades posteriores do BLK1.
- [x] Definir capacidades do dispositivo: tamanho lógico de setor, limite de
  transferência, somente leitura, flush e FUA quando disponíveis.
- [x] Criar um wrapper `block_submit_sync()` para preservar os chamadores
  bloqueantes sem permitir acesso direto ao driver.
- [x] Registrar no contrato que a conclusão síncrona ocorre antes do retorno,
  que a fila BLK1 preserva FIFO nesta etapa e que o chamador deve consumir o
  status antes de liberar recursos.

### Critério de saída

O contrato documenta ownership, ciclo de vida, alinhamento, erros,
cancelamento, timeout, conclusão e durabilidade, com um backend determinístico
que permite testar esses estados sem depender do hardware. A implementação foi
validada funcionalmente pelo usuário no QEMU; o BLK0 está concluído e a próxima
etapa é o BLK1.

### Comandos Shell / Diagnóstico

- A validação do BLK0 usa os diagnósticos existentes; o `blkstat` pertence ao
  BLK1 e já está publicado para a validação da fila e dos dispositivos reais.

---

## BLK1 - Fila Unificada de Requisições de Bloco

### Implementação

- [x] Definir `block_device_t` com identidade, setor lógico, capacidade,
  limites de transferência, flags de capacidade e callback de submissão de
  `block_request_t` para o driver.
- [x] Implementar a fila limitada e o dispatcher que transforma BIOs em
  requisições, rejeitando overflow com erro canônico e sem vazamento.
- [x] Implementar FIFO com fusão apenas de setores adjacentes compatíveis;
  deixar elevator e reordenação como otimizações posteriores mensuráveis.
- [x] Migrar o driver ATA PIO existente para o contrato de `block_device_t`
  sem mudar as assinaturas públicas do VFS ou das syscalls.
- [x] Adaptar USB Mass Storage somente depois de validar as capacidades e o
  ciclo de conclusão do dispositivo ATA.

### Critério de saída

Todas as operações de I/O de baixo nível passam pela fila de requisições, com
rastreabilidade de setores, status de conclusão, timeout e ownership do buffer.
Os chamadores síncronos continuam funcionando através do wrapper definido em
BLK0. A fila, a fusão, o `blkstat`, o autoteste da camada e a compatibilidade
FAT/VFS foram validados funcionalmente pelo usuário no QEMU.

### Comandos Shell / Diagnóstico

- `blkstat`: exibe os dispositivos de bloco registrados, taxa de transferência,
  setores lidos/gravados, tamanho da fila, pico, fusões e estados cumulativos.

---

## BLK2 - Cache de Blocos de Leitura com LRU

### Implementação

- [x] Criar a tabela de hash indexada por `(device_id, lba, block_size)` para
  busca $O(1)$ e evitar colisões semânticas entre setores e blocos de filesystem.
- [x] Manter estados `FREE`, `READING`, `VALID`, `DIRTY`, `WRITEBACK` e
  `ERROR`, além de referência/pin para impedir eviction durante uso ou I/O.
- [x] Manter lista LRU de entradas elegíveis para eviction, sem remover
  buffers sujos, fixados ou em voo.
- [x] Interceptar primeiro leituras FAT/VFS; em *cache hit*, copiar da RAM, e
  em *cache miss*, carregar o bloco pela BLK1 e acordar os waiters.
- [x] Manter invalidação explícita por dispositivo, faixa e desmontagem.
- [x] Registrar que este cache de blocos não substitui o page cache baseado em
  páginas/folios, que fica para uma etapa futura se houver necessidade.

### Critério de saída

Implementação BLK2 registrada e confirmação funcional recebida no QEMU. O
cache foi validado com os comandos de métricas, invalidação, limpeza,
`regcheck full`, `appcheck compact` e `health check`.

Leituras repetidas dos cenários definidos evitam transferências ao hardware sem
violar coerência, eviction ou ownership. A avaliação compara hit rate,
leituras evitadas, latência e memória usada contra uma linha de base
reproduzível; não há um percentual universal obrigatório.

### Comandos Shell / Diagnóstico

- `cachestat`: exibe taxa de acerto (*hit rate*), leituras evitadas no disco e memória utilizada pelo buffer cache.
- `cache clear`: invalida e esvazia todos os buffers limpos em memória.

---

## BLK3 - Writeback de Blocos e Sincronização

### Implementação

- [x] Marcar entradas sujas com lock, referência, pin e faixa de bytes alterada;
  escritas parciais preservam os bytes não modificados.
- [x] Submeter writeback físico limitado, preservar dados e estado `DIRTY` após
  erro, e expor `block_cache_sync_device()`/`block_cache_sync_all()`.
- [x] Agendar writeback periódico na workqueue a cada 250 ticks, com orçamento
  normal de 8 blocos e orçamento ampliado quando a ocupação suja é elevada.
- [x] Implementar flush ATA quando IDENTIFY publica suporte; USB MSC permanece
  sem FLUSH/FUA e a ausência de FLUSH publica durabilidade degradada.
- [x] Adicionar `vfs_fsync()`, `vfs_sync()`, fachadas, syscalls append-only e o
  comando Shell `sync`, com validação de argumentos e propagação de erros.

A implementação foi registrada; a confirmação funcional dos gates e do QEMU
continua pendente. `block_submit_sync()` permanece no caminho físico direto,
enquanto `block_write()` copia os dados para o cache e não retém ponteiros do
chamador. Fechamento de descritor e saída de processo não fazem sync implícito.

### Critério de saída

Escritas em cache seguem o contrato de retorno escolhido e são consolidadas
sem perder dados sujos; `sync`/`fsync` só concluem após o writeback e o flush
exigidos pelo contrato, ou retornam o erro correspondente.

### Comandos Shell / Diagnóstico

- `sync`: sincroniza todos os buffers modificados com as mídias de armazenamento e aguarda confirmação.
- `cachestat`: também mostra bytes sujos, tentativas/sucessos/falhas de
  writeback, syncs, flushes e estado de durabilidade.
- `fsync`: disponível pela VFS/API para arquivos regulares e dispositivos de
  bloco; pipes e dispositivos sem persistência retornam `ERR_UNAVAILABLE`.

---

## BLK4 - Resiliência e Failpoint Recovery

### Implementação

- [ ] Injetar pontos de falha controlados em submissão, execução, conclusão,
  flush e eviction, simulando corte de energia ou erro de hardware.
- [ ] Validar rollback ZUPD e consistência FAT12/FAT32 com cache ativo,
  deixando explícito que FAT12/FAT32 não ganham journaling automaticamente.
- [ ] Garantir que blocos sujos não sejam descartados e que falhas de sync
  interrompam o desligamento ou publiquem estado seguro conforme o contrato.
- [ ] Integrar `poweroff` somente depois de BLK3, com sync, flush e auditoria
  de erros antes da transição final.

### Critério de saída

Os cenários definidos de desmontagem e desligamento preservam a consistência
esperada ou retornam uma falha detectável antes de desligar. A etapa não promete
atomicidade de filesystem que não esteja implementada no formato FAT/ZUPD.

### Comandos Shell / Diagnóstico

- `blkcheck`: suite de estresse com gravação em lote, failpoints e auditoria de hashes dos arquivos gravados.
