# Roadmap 14 - Stack de Rede Avancada

## Objetivo

Aprimorar a pilha de rede TCP/IP do ZephyrOS através de buffers de pacotes com
ownership explícito, cópia evitável entre camadas, suporte a múltiplos tipos de
sockets (incluindo IPC local `AF_UNIX`) e multiplexação de I/O por readiness,
wait queues e timeout. Zero-copy será uma otimização medida, não uma garantia
para todos os caminhos de driver, DMA e checksum.

## Resumo de progresso

- [ ] NET0 - Contrato de ownership, lifetime, cópia e conclusão de buffers.
- [ ] NET1 - Estrutura de buffer de pacotes unificada (`sk_buff_t`) com cópia evitável.
- [ ] NET2 - Camada genérica de sockets (`AF_INET` e `AF_UNIX` para IPC local).
- [ ] NET3 - Multiplexação de I/O não-bloqueante (`select()` / `poll()`).
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

### Implementação

- [ ] Definir estados de buffer (`ALLOCATED`, `RX`, `QUEUED`, `IN_FLIGHT`,
  `DELIVERED`, `DROPPED` e `FREED`) e a transição permitida entre eles.
- [ ] Definir ownership, contagem de referências, clones, fragmentos,
  alinhamento, headroom, tailroom e limites de tamanho.
- [ ] Definir quem conclui RX/TX, como o erro chega ao socket e como timeout,
  cancelamento e fila cheia liberam o buffer exatamente uma vez.
- [ ] Criar contadores de cópia, clone, fragmentação, drop e pico de buffers.

### Critério de saída

O contrato permite verificar lifetime e ownership em cada caminho, incluindo
erro e cancelamento, sem depender da promessa de zero-copy total.

---

## NET1 - Estrutura de Buffer de Pacotes (sk_buff)

### Implementação

- [ ] Definir a estrutura `sk_buff_t`:
  - `uint8_t* head;` (início do buffer físico)
  - `uint8_t* data;` (início do payload da camada atual)
  - `uint8_t* tail;` (fim do payload)
  - `uint8_t* end;`  (fim do buffer físico)
  - `uint32_t len;`
  - `uint32_t refcount;`
  - `net_device_t* dev;`
- [ ] Implementar funções de manipulação de ponteiros:
  `sk_buff_t* alloc_skb(uint32_t size);`
  `void free_skb(sk_buff_t* skb);`
  `void* skb_put(sk_buff_t* skb, uint32_t len);`   (expande dados ao final)
  `void* skb_push(sk_buff_t* skb, uint32_t len);`  (adiciona cabeçalho ao início)
  `void* skb_pull(sk_buff_t* skb, uint32_t len);`  (remove cabeçalho do início)
- [ ] Adaptar a recepção RX dos drivers E1000 e RTL8139 para entregar
  `sk_buff_t` diretamente, preservando o fallback de cópia quando DMA ou
  alinhamento não permitirem a transferência do ownership.
- [ ] Adaptar TX e contabilizar cópias, clones e liberações para comparar o
  caminho novo com a implementação existente.

### Critério de saída

Pacotes trafegam entre as camadas sem cópia de cabeçalhos quando a invariante
de ownership permitir; cópias inevitáveis são contabilizadas, justificadas e
não deixam buffers vivos após a conclusão.

### Comandos Shell / Diagnóstico

- `skbstat`: exibe o total de `sk_buff` alocados, em trânsito e liberados.

---

## NET2 - Camada Genérica de Sockets e AF_UNIX

### Implementação

- [ ] Criar a estrutura `socket_t` com operações `socket_ops_t` (`bind`, `connect`, `listen`, `accept`, `send`, `recv`).
- [ ] Mapear sockets na tabela de descritores de arquivos do processo (`fd`).
- [ ] Implementar a família de endereços `AF_UNIX` (ou `AF_LOCAL`) para IPC
  entre processos no mesmo sistema através de filas e buffers locais.
- [ ] Integrar a família `AF_INET` existente à camada genérica sem quebrar
  sockets, TCP, UDP, HTTP ou os diagnósticos já validados.
- [ ] Definir bloqueio, não-bloqueio, fechamento concorrente e ownership dos
  buffers entre socket e fila.

### Critério de saída

Aplicativos criam e comunicam através de sockets locais e remotos utilizando a mesma API POSIX-like padronizada.

### Comandos Shell / Diagnóstico

- `sockstat`: lista todos os sockets abertos no sistema, tipo (`STREAM`/`DGRAM`), família (`INET`/`UNIX`) e processo proprietário.

---

## NET3 - Multiplexação de I/O Não-Bloqueante (poll/select)

### Implementação

- [ ] Implementar a syscall `poll()` com máscaras de leitura, escrita, erro e
  hangup, timeout e retorno por descritor.
- [ ] Integrar `poll()` com wait queues: registrar o waiter, liberar o lock
  antes de dormir, acordar após mudança de readiness e revalidar a condição.
- [ ] Implementar `select()` como camada de compatibilidade, preservando
  `fd_set`, suas macros e o contrato público existente.
- [ ] Garantir cancelamento por sinal, fechamento concorrente e retorno de
  erro sem waiter ou referência residual.
- [ ] Deixar `epoll` fora desta etapa até que `poll()` e o lifetime das filas
  estejam estáveis.

### Critério de saída

Aplicações conseguem esperar por múltiplos descritores sem busy-waiting ou
uma thread por conexão, com latência, wakeups, timeouts e descritores residuais
medidos nos cenários definidos.

### Comandos Shell / Diagnóstico

- `selecttest`: teste automatizado de monitoramento simultâneo de teclado, socket TCP e pipe em uma única chamada `select()`.

---

## NET4 - Diagnósticos e Monitoramento de Conexões

### Implementação

- [ ] Rastrear o estado de todas as conexões TCP (`CLOSED`, `LISTEN`, `SYN_SENT`, `ESTABLISHED`, `FIN_WAIT`, `TIME_WAIT`).
- [ ] Implementar tabela de rotas simples (gateway padrão, rotas locais de sub-rede).
- [ ] Integrar estatísticas de pacotes transmitidos/recebidos por interface de rede.

### Critério de saída

O sistema expõe visibilidade completa da rede para administradores e ferramentas de diagnóstico.

### Comandos Shell / Diagnóstico

- `netstat`: exibe tabela completa de conexões ativas, portas em escuta e estado dos sockets.
- `route`: visualiza e configura a tabela de roteamento de pacotes IPv4.
