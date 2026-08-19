# Roadmap 14 - Stack de Rede Avancada

## Objetivo

Aprimorar a pilha de rede TCP/IP do ZephyrOS através da adoção do conceito arquitetural de *Socket Buffers (sk_buff)* do Linux para manipulação de cabeçalhos com zero-copy entre camadas, suporte a múltiplos tipos de sockets (incluindo IPC local `AF_UNIX`) e multiplexação de I/O através de primitivas como `select()` e `poll()`.

## Resumo de progresso

- [ ] NET1 - Estrutura de buffer de pacotes unificada (`sk_buff_t`) com zero-copy.
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

- **Zero-Copy de Cabeçalhos:** As camadas Ethernet, IP e TCP não copiam dados do pacote; apenas deslocam os ponteiros `data` e `tail` do buffer.
- **Integração com VFS:** Sockets de rede são descritores de arquivo válidos (`fd`), permitindo uso com `read()`, `write()` e `close()`.
- **Sockets UNIX Locais (`AF_UNIX`):** Comunicação entre processos no mesmo sistema através do mesmo mecanismo de sockets de rede, com alta performance em RAM.
- **I/O Não-Bloqueante:** Servidores e daemons podem escutar centenas de conexões simultâneas usando uma única thread via `select()`.

## Ordem de dependência

1. NET1 - Alocador e estruturas `sk_buff_t`.
2. NET2 - Camada de abstração de sockets e suporte a `AF_UNIX`.
3. NET3 - Primitiva de multiplexação `select()` / `poll()`.
4. NET4 - Tabela de estados TCP, estatísticas e ferramentas CLI.

---

## NET1 - Estrutura de Buffer de Pacotes (sk_buff)

### Implementação

- [ ] Definir a estrutura `sk_buff_t`:
  - `uint8_t* head;` (início do buffer físico)
  - `uint8_t* data;` (início do payload da camada atual)
  - `uint8_t* tail;` (fim do payload)
  - `uint8_t* end;`  (fim do buffer físico)
  - `uint32_t len;`
  - `net_device_t* dev;`
- [ ] Implementar funções de manipulação de ponteiros:
  `sk_buff_t* alloc_skb(uint32_t size);`
  `void free_skb(sk_buff_t* skb);`
  `void* skb_put(sk_buff_t* skb, uint32_t len);`   (expande dados ao final)
  `void* skb_push(sk_buff_t* skb, uint32_t len);`  (adiciona cabeçalho ao início)
  `void* skb_pull(sk_buff_t* skb, uint32_t len);`  (remove cabeçalho do início)
- [ ] Adaptar a recepção RX dos drivers E1000 e RTL8139 para entregar `sk_buff_t` diretamente.

### Critério de saída

Pacotes de rede trafegam do driver Ethernet até a aplicação sem nenhuma chamada intermediária de `memcpy` para extração de cabeçalhos.

### Comandos Shell / Diagnóstico

- `skbstat`: exibe o total de `sk_buff` alocados, em trânsito e liberados.

---

## NET2 - Camada Genérica de Sockets e AF_UNIX

### Implementação

- [ ] Criar a estrutura `socket_t` com operações `socket_ops_t` (`bind`, `connect`, `listen`, `accept`, `send`, `recv`).
- [ ] Mapear sockets na tabela de descritores de arquivos do processo (`fd`).
- [ ] Implementar a família de endereços `AF_UNIX` (ou `AF_LOCAL`) para IPC ultrarrápido entre processos no mesmo sistema via memória compartilhada/buffer.
- [ ] Implementar a família `AF_INET` para conexões de rede IPv4.

### Critério de saída

Aplicativos criam e comunicam através de sockets locais e remotos utilizando a mesma API POSIX-like padronizada.

### Comandos Shell / Diagnóstico

- `sockstat`: lista todos os sockets abertos no sistema, tipo (`STREAM`/`DGRAM`), família (`INET`/`UNIX`) e processo proprietário.

---

## NET3 - Multiplexação de I/O Não-Bloqueante (select/poll)

### Implementação

- [ ] Implementar os tipos `fd_set` e as macros `FD_ZERO`, `FD_SET`, `FD_CLR`, `FD_ISSET`.
- [ ] Implementar a syscall `sys_select(int maxfd, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, uint32_t timeout_ms)`.
- [ ] Integrar `select()` com as *Wait Queues* do Roadmap 12, permitindo que a thread aguarde até que ao menos um descritor esteja pronto para leitura/escrita ou o timeout expire.

### Critério de saída

Um processo de servidor HTTP ou telnet consegue atender múltiplos clientes simultaneamente sem travar e sem necessitar de uma thread por cliente.

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
