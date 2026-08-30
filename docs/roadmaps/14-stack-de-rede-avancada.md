# Roadmap 14 - Stack de Rede Avancada

## Objetivo

Aprimorar a pilha de rede TCP/IP do ZephyrOS através de buffers de pacotes com
ownership explícito, cópia evitável entre camadas, suporte a múltiplos tipos de
sockets (incluindo IPC local `AF_UNIX`) e multiplexação de I/O por readiness,
wait queues e timeout. Zero-copy será uma otimização medida, não uma garantia
para todos os caminhos de driver, DMA e checksum.

## Resumo de progresso

- [x] NET0 - Contrato de ownership, lifetime, cópia e conclusão de buffers.
- [x] NET1 - Estrutura de buffer de pacotes unificada (`sk_buff_t`) com cópia evitável.
- [x] NET2 - Camada genérica de sockets (`AF_INET` e `AF_UNIX` para IPC local).
- [x] NET3 - Multiplexação de I/O não-bloqueante (`select()` / `poll()`).
- [ ] NET4 - Roteamento avançado, tabela de conexões TCP e ferramentas de diagnóstico.

## Atalhos

- [Roadmap 05 - Sistema e Ecossistema](05-sistema-e-ecossistema.md)
- [Roadmap 10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md)
- [Roadmap 12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Drivers de placa de rede Ethernet Intel E1000 e Realtek RTL8139.
- Pilha de protocolos ARP, IPv4, ICMP (ping), UDP, DHCP e DNS.
- Implementação inicial de conexões TCP, sockets e cliente HTTP.
- Suporte a Multi-NIC validado em ambiente QEMU.

## Princípios de engenharia

- **Ownership explícito:** Cada buffer tem proprietário, referência, estado de
  RX/TX e uma regra de liberação; clones e fragmentos compartilham dados sem
  uso após liberação.
- **Cópia evitável:** Ethernet, IP e TCP deslocam `data`/`tail` quando possível;
  cópias continuam permitidas para alinhamento, checksum, DMA, segurança ou
  mudança de ownership, e devem ser contabilizadas.
- **Integração com VFS:** Sockets de rede são descritores de arquivo válidos
  (`fd`), permitindo uso com `read()`, `write()`, `close()` e readiness.
- **Sockets UNIX Locais (`AF_UNIX`):** Comunicação entre processos usa uma
  fila local própria, sem fingir que o tráfego passou pela pilha IP.
- **I/O Não-Bloqueante:** `poll()` consulta máscaras de readiness e registra
  wait queues; `select()` pode ser um wrapper compatível. Escalabilidade só é
  declarada após benchmark reproduzível.

## Ordem de dependência

1. NET0 - Contratos de ownership e lifetime.
2. NET1 - Alocador e estruturas `sk_buff_t`.
3. NET2 - Camada de abstração de sockets e suporte a `AF_UNIX`.
4. NET3 - Primitiva de multiplexação `poll()` e wrapper `select()`.
5. NET4 - Tabela de estados TCP, estatísticas e ferramentas CLI.

---

## NET0 - Contrato de buffers e conclusões

Estado da implementacao: contrato e runtime entregues e validados
funcionalmente pelo usuario em 2026-08-29. Os resumos acima foram marcados como
  [x]; NET4 permanece pendente.

### Implementação

- [x] Definir estados de buffer (`ALLOCATED`, `RX`, `QUEUED`, `IN_FLIGHT`,
  `DELIVERED`, `DROPPED` e `FREED`) e a transição permitida entre eles.
- [x] Definir ownership, contagem de referências, clones, fragmentos,
  alinhamento, headroom, tailroom e limites de tamanho.
- [x] Definir quem conclui RX/TX, como o erro chega ao socket e como timeout,
  cancelamento e fila cheia liberam o buffer exatamente uma vez.
- [x] Criar contadores de cópia, clone, fragmentação, drop e pico de buffers.

O runtime de src/include/core/net_buffer.h e estatico, limitado a 32
descritores, e nao retém ponteiros de payload externos. A ultima referencia
somente e liberada depois de DELIVERED ou DROPPED; FREED e terminal. Clones e
fragmentos possuem contadores reservados, sem implementar compartilhamento de
dados nesta etapa.

No NET0, o descriptor era privado da camada Ethernet. A implementacao do NET1
substitui esse detalhe por `sk_buff_t`, preservando os mesmos ciclos RX/TX:
RX percorre `ALLOCATED -> RX -> DELIVERED/DROPPED -> FREED` e TX percorre
`ALLOCATED -> IN_FLIGHT -> DELIVERED/DROPPED -> FREED`. Os callbacks dos
drivers continuam sincronos, com buffers emprestados validos somente durante o
callback; as copias nas fronteiras Ethernet e socket sao contabilizadas.

`regcheck full` e `net check` executam os self-tests privados e restauram suas
metricas. `health check` verifica invariantes, buffers ativos e erros residuais.
Nao ha transferencia de ownership de DMA, zero-copy garantido, clones ou
fragmentos reais nesta etapa, nem alteracao de `boot.asm`.

### Critério de saída

O contrato permite verificar lifetime e ownership em cada caminho, incluindo
erro e cancelamento, sem depender da promessa de zero-copy total.

---

## NET1 - Estrutura de Buffer de Pacotes (sk_buff)

Estado da implementacao: codigo integrado e validado funcionalmente pelo
usuario em 2026-08-29. O resumo NET1 foi marcado como `[x]`; NET4 permanece
pendentes.

### Implementação

- [x] Definir a estrutura `sk_buff_t`:
  - `uint8_t* head;` (início do buffer físico)
  - `uint8_t* data;` (início do payload da camada atual)
  - `uint8_t* tail;` (fim do payload)
  - `uint8_t* end;`  (fim do buffer físico)
  - `uint32_t len;`
  - `uint32_t refcount;`
  - `net_device_t* dev;` (handle opaco associado ao slot Ethernet)
- [x] Manter um `net_buffer_t` privado por skb como fonte unica de estado,
  owner, referencias e inventario.
- [x] Alocar objetos por SLAB, com storage interno de no maximo 2048 bytes e
  sem `kmalloc` por pacote.
- [x] Implementar funções de manipulação de ponteiros:
  `sk_buff_t* alloc_skb(uint32_t size);`
  `void free_skb(sk_buff_t* skb);`
  `void* skb_put(sk_buff_t* skb, uint32_t len);`   (expande dados ao final)
  `void* skb_push(sk_buff_t* skb, uint32_t len);`  (adiciona cabeçalho ao início)
  `void* skb_pull(sk_buff_t* skb, uint32_t len);`  (remove cabeçalho do início)
- [x] Adaptar a recepção RX da Ethernet para operar sobre `sk_buff_t`, sem
  alterar os callbacks `uint8_t*` dos drivers E1000 e RTL8139.
- [x] Adaptar TX e contabilizar cópias, conclusões, descartes e liberações; o
  fallback permanece síncrono e baseado em cópia.
- [x] Validar operações de ponteiros, referências, conclusão única, descarte,
  pool vazio, inventário e repetição dos diagnósticos.

### Critério de saída

Pacotes usam a estrutura unificada durante o callback síncrono, com geometria
`head/data/tail/end` validada e cópias inevitáveis contabilizadas. Não há
ownership DMA transferido, zero-copy real, clones ou fragmentos reais no NET1,
e não permanecem buffers vivos após os diagnósticos.

### Comandos Shell / Diagnóstico

- `skbstat`: exibe buffers ativos, pico, alocações, liberações, conclusões,
  descartes, cópias, clones reservados, fragmentos reservados e erros.

---

## NET2 - Camada Genérica de Sockets e AF_UNIX

Estado da implementacao: codigo integrado e validado funcionalmente pelo
usuario em 2026-08-29. O resumo NET2 foi marcado como `[x]`; NET4 permanece
pendentes.

### Implementação

- [x] Criar a estrutura `socket_t` com operações `socket_ops_t` (`bind`, `connect`, `listen`, `accept`, `send`, `recv`).
- [x] Mapear sockets na tabela de descritores de arquivos do processo (`fd`).
- [x] Implementacao preparada para validacao funcional: `socket_t` permanece
  privado em `src/core/socket.c`, os objetos usam FDs reais `VFS_NODE_SOCKET` e
  `AF_UNIX/SOCK_STREAM` usa namespace global, backlog limitado e filas
  `sk_buff_t` com copia.
- [x] `AF_INET/SOCK_STREAM` adapta somente o cliente TCP ativo legado; o
  caminho bloqueante libera locks antes da espera e
  `SOCKET_FLAG_NONBLOCK` retorna `ERR_AGAIN` sem progresso.
- [x] `sockstat`, `socket_self_test()`, `net check`, `regcheck full` e
  `health check` foram integrados; a fixture cobre lifecycle, FD VFS,
  bind duplicado, connect/listen/accept, leitura parcial, fila cheia, EOF,
  entradas invalidas, cancelamento e limpeza.
- [x] Nao ha syscall, wrapper de App API, `poll/select`, `socketpair`,
  datagramas UNIX ou ownership DMA transferido. Mensagens longas usam buffers
  independentes de ate 2048 bytes; clones, fragmentos compartilhados e
  zero-copy real permanecem fora da NET2. `boot.asm` nao foi alterado.

- [x] Implementar a família de endereços `AF_UNIX` (ou `AF_LOCAL`) para IPC
  entre processos no mesmo sistema através de filas e buffers locais.
- [x] Integrar a família `AF_INET` existente à camada genérica sem quebrar
  sockets, TCP, UDP, HTTP ou os diagnósticos já validados.
- [x] Definir bloqueio, não-bloqueio, fechamento concorrente e ownership dos
  buffers entre socket e fila.

### Critério de saída

Aplicativos criam e comunicam através de sockets locais e remotos utilizando a mesma API POSIX-like padronizada.

### Comandos Shell / Diagnóstico

- `sockstat`: lista sockets `STREAM` `AF_UNIX`/`AF_INET`, seus descritores
  VFS e o processo proprietário; datagramas permanecem fora da NET2.

---

## NET3 - Multiplexação de I/O Não-Bloqueante (poll/select)

### Implementação

Estado da implementacao NET3: codigo integrado e validado funcionalmente pelo
usuario em 2026-08-30. O resumo NET3 esta marcado como `[x]`; NET4 permanece
pendente.

- [x] Implementar a syscall `poll()` com máscaras de leitura, escrita, erro e
  hangup, timeout e retorno por descritor.
- [x] Integrar `poll()` com wait queues: registrar o waiter, liberar o lock
  antes de dormir, acordar após mudança de readiness e revalidar a condição.
- [x] Implementar `select()` como camada de compatibilidade, preservando
  `fd_set`, suas macros e o contrato público existente.
- [x] Garantir cancelamento por sinal, fechamento concorrente e retorno de
  erro sem waiter ou referência residual.
- [x] Deixar `epoll` fora desta etapa até que `poll()` e o lifetime das filas
  estejam estáveis.

### Critério de saída

Aplicações conseguem esperar por múltiplos descritores sem busy-waiting ou
uma thread por conexão, com latência, wakeups, timeouts e descritores residuais
medidos nos cenários definidos.

### Comandos Shell / Diagnóstico

- `selecttest`: teste automatizado de monitoramento simultâneo de teclado, socket TCP e pipe em uma única chamada `select()`.

---

## NET4 - Diagnósticos e Monitoramento de Conexões

Estado da implementacao NET4: codigo integrado; validacao funcional do usuario
pendente. O resumo NET4 permanece `[ ]` ate a execucao da matriz definida no
criterio de saida.

### Implementação

- [x] Rastrear o estado de todas as conexões TCP (`CLOSED`, `LISTEN`, `SYN_SENT`, `ESTABLISHED`, `FIN_WAIT`, `TIME_WAIT`) na visão agregada `netstat`, preservando o enum append-only.
- [x] Implementar tabela de rotas simples em RAM, com gateway padrão, rotas locais de sub-rede, longest-prefix match, reset e limite de 16 entradas.
- [x] Integrar estatísticas de pacotes transmitidos/recebidos, erros e descartes por interface de rede.
- [x] Adicionar autoteste determinístico de rotas e integrar invariantes ao `net check`, `regcheck full` e `health`.
- [x] Registrar `route` e `netstat` no dispatcher central, ajuda do Shell, Makefile e contratos públicos.

O forwarding IPv4 entre interfaces, persistência das rotas e criação passiva
de sockets TCP permanecem fora da NET4. Rotas para interfaces diferentes da
L3 atual são recusadas pelo comando e reportadas como indisponiveis no envio.

### Critério de saída

O sistema expõe visibilidade completa da rede para administradores e ferramentas de diagnóstico.

### Comandos Shell / Diagnóstico

- `netstat`: exibe conexões TCP, sockets `AF_UNIX` e estatísticas das interfaces.
- `route`: visualiza e configura a tabela de roteamento de pacotes IPv4 em RAM.
