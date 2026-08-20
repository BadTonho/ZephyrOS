# Roadmap 13 - Armazenamento e Buffer Cache

## Objetivo

Implementar uma camada de abstração de dispositivos de bloco (*Block Layer*) e um sistema de cache de leitura e escrita (*Buffer Cache / Page Cache*) no ZephyrOS, otimizando o acesso a discos ATA e mídias USB Mass Storage através do agrupamento de requisições contíguas e sincronização controlada de blocos modificados (*dirty pages*).

## Resumo de progresso

- [ ] BLK1 - Fila unificada de requisições de bloco (`block_device_t` e `bio_request_t`).
- [ ] BLK2 - Buffer Cache de leitura em memória com política de substituição LRU.
- [ ] BLK3 - Escrita em cache com marcação de blocos modificados (*dirty*) e flush assíncrono (`sync`).
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

- **Desacoplamento de Hardware:** Filesystems requisitam leitura/escrita de blocos através da camada de bloco genérica, sem saber se a mídia física é ATA PIO, ATA DMA ou USB.
- **Fusão de Setores (*Request Merging*):** Requisições consecutivas de leitura ou escrita para setores adjacentes são combinadas em uma única transferência de hardware.
- **Escrita Segura com Confirmação:** Operações críticas do sistema (metadados de filesystem, atualização do sistema) podem requisitar gravação direta síncrona (*write-through / O_SYNC*).
- **Sem Vaza-Vaza de Memória:** O Buffer Cache opera com limite fixo configurável de memória (ex: 512KB a 2MB), descartando blocos limpos antigos quando a memória estiver cheia.

## Ordem de dependência

1. BLK1 - Estruturas de dispositivo de bloco e fila de requisições.
2. BLK2 - Buffer Cache de leitura e algoritmo de cache LRU.
3. BLK3 - Marcação de blocos dirty e thread de sincronização `sync`.
4. BLK4 - Testes de integridade, desligamento forçado e failpoint recovery.

---

## BLK1 - Fila Unificada de Requisições de Bloco

### Implementação

- [ ] Definir a estrutura `block_device_t`:
  - `const char* name;`
  - `uint32_t sector_size;`
  - `uint32_t total_sectors;`
  - `int (*submit_bio)(struct block_device*, struct bio_request*);`
- [ ] Implementar a estrutura de requisição de I/O `bio_request_t` contendo LBA inicial, contagem de setores, buffer e callback de conclusão.
- [ ] Implementar algoritmo de ordenação de fila (elevador simples / FIFO) para reduzir saltos desnecessários de leitura.
- [ ] Migrar o driver ATA PIO existente para o contrato de `block_device_t`.

### Critério de saída

Todas as operações de I/O de baixo nível passam pela fila de requisições com rastreabilidade completa de setores lidos e gravados.

### Comandos Shell / Diagnóstico

- `blkstat`: exibe os dispositivos de bloco registrados, taxa de transferência, setores lidos/gravados e tamanho da fila.

---

## BLK2 - Buffer Cache de Leitura com LRU

### Implementação

- [ ] Criar a tabela de hash indexada por `(device_id, lba)` para busca $O(1)$ de blocos em memória.
- [ ] Manter lista duplamente ligada LRU (*Least Recently Used*) de buffers livres e ocupados.
- [ ] Interceptar leituras do VFS/FAT: se o bloco já estiver em cache (*cache hit*), copiar da RAM instantaneamente sem acessar o hardware.
- [ ] Se houver *cache miss*, alocar um slot no buffer, carregar do disco e inserir no topo da lista LRU.

### Critério de saída

Leituras consecutivas de arquivos ou diretórios já visitados ocorrem sem acionar interrupções ou transferências no controlador ATA, atingindo mais de 80% de *cache hit* em benchmarks normais.

### Comandos Shell / Diagnóstico

- `cachestat`: exibe taxa de acerto (*hit rate*), leituras evitadas no disco e memória utilizada pelo buffer cache.
- `cache clear`: invalida e esvazia todos os buffers limpos em memória.

---

## BLK3 - Dirty Page Writeback e Sincronização

### Implementação

- [ ] Suportar gravação em cache marcando o buffer com o estado `BUF_DIRTY`.
- [ ] Criar a função do kernel `int sync_buffers(block_device_t* dev)` que varre e grava todos os blocos alterados no disco.
- [ ] Integrar a rotina de flush periódico na fila de trabalho (*Workqueue* do Roadmap 12) executando a cada 5 ou 10 segundos.
- [ ] Implementar a syscall e comando Shell `sync` para forçar a gravação imediata de todos os dados pendentes.

### Critério de saída

Escritas de arquivos pequenos retornam instantaneamente para o aplicativo e são consolidadas no disco de forma ordenada pelo worker em background.

### Comandos Shell / Diagnóstico

- `sync`: sincroniza todos os buffers modificados com as mídias de armazenamento e aguarda confirmação.

---

## BLK4 - Resiliência e Failpoint Recovery

### Implementação

- [ ] Injetar pontos de falha controlados (*failpoints*) simulando corte de energia ou erro de hardware durante gravação de blocos.
- [ ] Validar que o rollback de atualizações ZUPD e a recuperação de consistência FAT12/FAT32 funcionam corretamente com o cache ativo.
- [ ] Garantir que o comando de desligamento (`poweroff` do Roadmap 16) execute `sync` automático antes de desligar a CPU.

### Critério de saída

Nenhum dado é corrompido em cenários de desmontagem de volume ou desligamento controlado do sistema operacional.

### Comandos Shell / Diagnóstico

- `blkcheck`: suite de estresse com gravação em lote, failpoints e auditoria de hashes dos arquivos gravados.
