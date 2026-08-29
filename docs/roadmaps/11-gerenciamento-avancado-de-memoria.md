# Roadmap 11 - Gerenciamento Avancado de Memoria

## Objetivo

Evoluir o subsistema de memória do ZephyrOS incorporando alocadores de objetos de tamanho fixo *SLAB/SLUB*, estruturas de áreas virtuais *VMA* e paginação sob demanda (*Demand Paging*), garantindo tempo constante $O(1)$ na alocação de estruturas críticas, eliminação de fragmentação de heap e proteção rigorosa de memória em espaço de usuário.

Este roadmap preserva a compatibilidade total com o Bitmap Allocator físico e o Paging existente, sem alterar os contratos do bootloader.

## Resumo de progresso

- [x] MM1 - Alocador SLAB/SLUB de objetos de tamanho fixo (`kmem_cache_t`).
- [x] MM2 - Áreas de Memória Virtual do Processo (VMA - *Virtual Memory Areas*).
- [ ] MM3 - Alocação sob Demanda (*Demand Paging*) e tratamento de Page Faults.
- [ ] MM4 - Métricas de fragmentação, zonas de memória e monitoramento em tempo real.

## Atalhos

- [Roadmap 03 - Kernel e Desempenho](03-kernel-e-desempenho.md)
- [Roadmap 10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md)
- [Roadmap 12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Detecção de mapa de memória E820 via bootloader.
- PMM (Physical Memory Manager) baseado em bitmap de 4KB por página.
- VMM (Virtual Memory Manager) com Page Directory e Page Tables de dois níveis x86.
- Heap de kernel com `kmalloc()`, `kfree()` e alinhamento `kmalloc_aligned()`.
- Ferramenta de diagnóstico `memcheck` validada no QEMU.

## Princípios de engenharia

- **Prevenção de Fragmentação:** O heap genérico (`kmalloc`) não deve ser utilizado para alocação intensiva de estruturas de tamanho fixo repetitivas (como nós de processos, sockets ou buffers).
- **Tempo Constante $O(1)$:** Alocações e liberações em pools SLAB operam em tempo determinístico, sem varredura de blocos livres.
- **Proteção por Segmento:** Cada VMA define explicitamente se a região permite leitura, escrita ou execução (`PROT_READ | PROT_WRITE | PROT_EXEC`).
- **Segurança de Falhas:** Falhas de página em ring 3 devem ser isoladas, sinalizadas ou tratadas sem resultar em Kernel Panic imediato.

## Ordem de dependência

1. MM1 - Alocador SLAB/SLUB de estruturas fixas do kernel.
2. MM2 - Estruturas de VMA no contexto de processos.
3. MM3 - Demand Paging na carga e expansão de heap de aplicativos.
4. MM4 - Métricas de fragmentação e integração com `memcheck` e Task Manager.

---

## MM1 - Alocador SLAB/SLUB de objetos fixos

### Implementação

- [x] Implementar a estrutura `kmem_cache_t` representando um pool de objetos de tamanho homogêneo.
- [x] Criar as funções públicas:
  `kmem_cache_t* kmem_cache_create(const char* name, uint32_t obj_size, uint32_t align);`
  `void* kmem_cache_alloc(kmem_cache_t* cache);`
  `void kmem_cache_free(kmem_cache_t* cache, void* obj);`
  `int kmem_cache_destroy(kmem_cache_t* cache);`
- [x] Organizar cada cache em slabs divididos em três listas: `full` (cheios), `partial` (parciais) e `empty` (vazios), ou lista simples no estilo SLUB com bitmap/freelist interno.
- [x] Migrar estruturas essenciais para caches dedicados: `process_t`, `thread_t`, `file_t`, `vnode_t` e `net_packet_t`.
- [x] Publicar estatísticas, posse de objetos, validação global e autoteste por `slabinfo` e `slabtest`.
- [x] Integrar a validação aos diagnósticos `health`, `memcheck`, `schedcheck`, `regcheck` e `vfs_validate_state()`.

### Status da entrega

A implementação usa metadados estáticos para até 16 caches, 128 slabs globais
e 128 objetos por slab. As páginas dos slabs são obtidas do PMM somente após o
paging e permanecem reutilizáveis enquanto o cache existir. Processos, threads,
arquivos, vnodes e pacotes Ethernet usam caches dedicados; stacks de processos e
threads continuam no `kmalloc` por exigirem tamanho e guardas próprios.

A MM1 foi validada e encerrada conforme confirmação do usuário após a
execução dos comandos e gates correspondentes. A MM2 foi implementada,
validada no QEMU pelo usuário e encerrada nesta etapa.

### Critério de saída

Criação e destruição de centenas de estruturas de processos e arquivos ocorrem sem degradação do heap genérico do kernel e sem vazamento de memória.

### Comandos Shell / Diagnóstico

- `slabinfo`: exibe todos os caches SLAB ativos, tamanho de objeto, quantidade alocada, uso de páginas e taxa de ocupação.
- `slabtest`: bateria de testes de estresse de alocação/liberação simultânea em múltiplos caches.

---

## MM2 - Áreas de Memória Virtual (VMA)

### Implementação

- [x] Definir a estrutura `vm_area_t` contendo:
  - `uint32_t start_addr;`
  - `uint32_t end_addr;`
  - `uint32_t flags;` (`VM_READ`, `VM_WRITE`, `VM_EXEC`, `VM_SHARED`, `VM_ANONYMOUS`)
  - `struct file* file;` (para mapeamentos backed por arquivo)
  - `uint32_t offset;`
- [x] Adicionar lista ligada ordenada de VMAs na estrutura `process_t`.
- [x] Implementar a syscall `sys_mmap()` para alocação anônima de regiões virtuais contíguas.
- [x] Implementar a syscall `sys_munmap()` para liberação e invalidação de páginas virtuais associadas.

### Critério de saída

Processos conseguem alocar e mapear múltiplos segmentos virtuais com permissões
granulares sem manipulação direta e manual de Page Tables. A implementação MM2
e sua validação executável no QEMU foram concluídas pelo usuário.

### Limitações aceitas nesta etapa

- `mmap` aceita somente regiões anônimas privadas e escolhe o primeiro intervalo livre entre `USER_LAUNCH_BASE` e `USER_STACK_BASE`.
- `VM_READ` e `VM_EXEC` permanecem metadados; somente `VM_WRITE` altera o bit de escrita do paging, pois o hardware atual não publica NX.
- `VM_SHARED` e mapeamentos com backing de arquivo permanecem reservados para uma etapa posterior.
- `munmap` exige endereço alinhado, arredonda o tamanho para páginas e recusa as regiões fixas do loader.

### Comandos Shell / Diagnóstico

- `vmamap <pid>`: lista os segmentos virtuais do processo informado (código, dados, heap, stack) com suas permissões.

---

## MM3 - Alocação sob Demanda (Demand Paging)

Implementação preparada no código; a marcação como concluída permanece
dependente da validação funcional do usuário no QEMU.

### Implementação

- [ ] Modificar o loader de aplicativos ring 3 para registrar as VMAs de código e dados sem alocar previamente todas as páginas físicas de uma só vez.
- [ ] No tratador de exceção 14 (Page Fault ISR):
  - Inspecionar o endereço de falta no registrador `CR2`.
  - Verificar se o endereço pertence a uma VMA válida com permissão correspondente.
  - Alocar uma página física livre do PMM e mapeá-la na Page Table do processo sob demanda.
  - Retornar da interrupção para continuar a execução do código de usuário.
- [ ] Caso o endereço não pertença a nenhuma VMA válida, disparar `SIGSEGV` ou encerrar o processo ring 3 de forma isolada, registrando erro no log sem gerar panic no kernel.

### Critério de saída

Executáveis grandes são carregados quase instantaneamente consumindo apenas a quantidade de páginas físicas que realmente são executadas.

### Comandos Shell / Diagnóstico

- `pagefault status`: exibe contadores de faltas de página tratadas com sucesso versus faltas inválidas capturadas.

---

## MM4 - Métricas de fragmentação e monitoramento

### Implementação

- [ ] Calcular índice de fragmentação da memória física (blocos contíguos livres vs páginas isoladas).
- [ ] Mapear as estatísticas de memória em categorias claras: Kernel, Heap, SLAB, Processos, Buffers e Livre.
- [ ] Expor as métricas através da camada de diagnóstico e integrar à interface gráfica (Task Manager Classic/Modern).

### Critério de saída

O sistema fornece diagnóstico preciso da alocação de memória em tempo real sem impacto perceptível no desempenho.

### Comandos Shell / Diagnóstico

- `mem detailed`: relatório completo da divisão da memória entre kernel, processos, caches e blocos físicos livres.
