# Gerenciador de Rede — ZephyrOS v0.1

> **Arquitetura:** As funções core (comandos shell: `net`, `ifconfig`, `ping`, `wget`, `ftp`) são nativas do sistema. A interface visual (TUI com painel de rede, firewall) é um app opcional distribuído pelo [Gerenciador de Aplicativos](gerenciador%20de%20aplicativos.md).

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Driver de NIC (Ethernet) | 52 | 6 | 0 | 46 |
| 2. Stack TCP/IP básico | 68 | 17 | 0 | 51 |
| 3. Protocolos de rede | 48 | 0 | 0 | 48 |
| 4. Serviços de rede | 42 | 0 | 0 | 42 |
| 5. Interface TUI do Gerenciador | 64 | 0 | 0 | 64 |
| 6. Comandos de rede (Shell) | 38 | 0 | 0 | 38 |
| 7. Firewall e segurança | 44 | 0 | 0 | 44 |
| **TOTAL** | **356** | **23** | **0** | **333** |

**Progresso geral: 6%** (23/356 itens completos)

---

## S2.1 segura - Fundacao de rede observavel

- [x] Servico `network_manager` com snapshot estatico de ate quatro
  controladores PCI de classe `0x02`.
- [x] Consultas PCI exclusivamente por copia, sem bus mastering, MMIO, I/O,
  IRQ, buffers ou transmissao.
- [x] IDs estaveis `net-pci-BB:DD.F` e consultas por indice ou ID.
- [x] Intel `8086:100E` reconhecida como E1000 e Realtek `10EC:8139` como
  RTL8139, ambas ainda sem driver.
- [x] `Network` no `health`: degradado com NIC sem driver, desabilitado sem
  controlador e pronto reservado para driver ativo.
- [x] `net status`, `net devices` e `net info <id>` documentam somente
  capacidades reais.
- [x] `device-scan` atualiza o snapshot de rede sem inicializar hardware.
- [x] `regcheck full` valida a varredura, o snapshot, IDs e recovery de rede
  junto dos contratos de Devices, ACPI e Power.
- [x] Validacao manual concluida com `regcheck full` no QEMU padrao e com
  `-nic none`; os dois cenarios retornaram `RegCheck: OK` apos `F12`.

Esta entrega nao e contabilizada nos 356 itens do roadmap legado abaixo.

## S2.2 - E1000 Ethernet L2 (implementada e validada)

- [x] Driver `src/drivers/e1000.c` para Intel `8086:100E`, com BAR0 MMIO de
  32 bits, habilitacao confirmada de Memory Space/DMA, reset com timeout e
  MAC por RAL/RAH.
- [x] Oito descritores RX/TX e buffers DMA de 2 KiB, IRQ PCI legado,
  contadores, link e falhas observaveis sem bloquear boot ou Shell.
- [x] `network_manager` preserva `net-pci-BB:DD.F`, mostra MAC, estado, link,
  contadores e ultimo erro; `device-scan` nao reinicializa o hardware.
- [x] `net test <id>` transmite um frame broadcast privado `0x88B5` somente
  por solicitacao; RX e contabilizado/reciclado, sem ARP, IPv4 ou DHCP.
- [x] Validada pelo usuario com Q3, build limpo e QEMU padrao: `net test`,
  `device-scan` e `regcheck full` aprovados. Sem NIC e com RTL8139, o boot e
  o RegCheck permaneceram funcionais e `net test` foi recusado controladamente.

## S2.3 - Camada Ethernet e entrega de RX (concluida e validada)

- [x] Criar uma fronteira segura entre a IRQ do E1000 e o processamento de
  protocolos: frames recebidos devem ser copiados para fila fixa ou obtidos
  por polling antes de reciclar o descritor, sem executar protocolo na IRQ.
- [x] Definir a abstracao minima de interface usada pela camada Ethernet, sem
  antecipar configuracao IP, DHCP ou um segundo driver de hardware.
- [x] Implementar montagem, validacao e parsing de cabecalho Ethernet;
  suportar broadcast e unicast para a MAC local, com contadores observaveis.
- [x] Acrescentar comando Shell de diagnostico Ethernet e preservar o
  `net test <id>` como transmissao L2 sob demanda.
- [x] Validada pelo usuario no QEMU padrao: camada ativa, polling ocioso
  zerado, TX contabilizado no E1000 e na camada, `device-scan` aprovado e
  `regcheck full` concluido em `OK`.
- [ ] Cobertura complementar: injetar RX externo e frame invalido e repetir a
  regressao visual Classic/Modern; esses cenarios nao bloqueiam a conclusao.

## S2.4 - ARP com cache e resolucao assincrona (concluida e validada)

- [x] EtherType `0x0806` entregue fora da IRQ por tabela fixa de handlers.
- [x] Pacotes ARP serializados explicitamente em ordem de rede.
- [x] Uma interface e um IPv4 local configurados somente em RAM.
- [x] Cache de 32 entradas com estados, expiracao, retries e timeout.
- [x] Aprendizado restrito a request local e reply de resolucao pendente.
- [x] Reply automatico somente para request valido dirigido ao IP local.
- [x] Comandos `net arp config/status/resolve/table/clear` e diagnostico
  agregado `net check [id]`.
- [x] Perfil ativo `net check qemu <id> <ip>` para validar reply, cache hit e
  timeout em uma unica execucao cooperativa.
- [x] Invariantes ARP adicionadas ao `regcheck full` sem efeitos colaterais.
- [x] Validada pelo usuario no QEMU padrao: reply, cache hit sem novo TX,
  timeout apos tres tentativas, polling sem erros e invariantes em `OK`.
- [ ] Cobertura complementar: peer externo, ausencia de NIC, RTL8139,
  `device-scan`, sintaxe invalida e Classic/Modern; esses cenarios nao
  bloqueiam a conclusao.

## S2.5 - IPv4 estatico e ICMP Echo (concluida e validada)

- [x] IPv4 minimo, configuracao estatica em RAM, rota direta/gateway,
  checksum, MTU 1500 e despacho por protocolo.
- [x] ICMP Echo com resposta automatica, ping cooperativo e RTT.
- [x] Suite QEMU e invariantes validadas pelo usuario.

## S2.6 - UDP, DHCP e DNS (concluida e validada)

- [x] UDP com 16 endpoints fixos, checksum, entrega sincrona e broadcast
  limitado para DHCP.
- [x] Cliente DHCP com DORA, retentativas, T1/T2, renovacao, rebinding,
  expiracao, NAK e release.
- [x] Configuracao DHCP aplicada atomicamente sem remover IPv4 estatico antes
  de um ACK valido.
- [x] DNS A por UDP, compressao segura, CNAME limitado e cache de 16 entradas
  com TTL.
- [x] Comandos individuais, `nslookup`, `ping` por dominio e suite agrupada
  `net check qemu dhcp <id> <dominio>`.
- [x] Vetores puros e invariantes integrados ao `regcheck full`.
- [x] Suite agrupada validada pelo usuario no QEMU padrao, com UDP, DORA,
  lease, DNS, cache, ICMP, polling e invariantes em `OK`.
- [ ] Cobertura complementar: renovacao/liberacao individual, fallbacks,
  peer externo e Classic/Modern.

## S2.7 - TCP cliente, sockets e HTTP GET (implementada)

- [x] Protocolo TCP IPv4 cliente com 16 conexoes, handles geracionais,
  checksum, handshake, ACK, dados, FIN/RST e descarte fora de ordem.
- [x] MSS local 536, janela RX 4096, um segmento pendente, RTO adaptativo,
  regra de Karn, backoff e no maximo tres retransmissoes.
- [x] API de 16 sockets `STREAM` nativos, filas TX de 2 KiB e RX de 4 KiB,
  sem syscall ou ABI para aplicacoes.
- [x] Cliente HTTP GET com uma sessao, DNS/IP literal, headers de 4 KiB,
  corpo de 16 KiB e framing por `Content-Length` ou EOF.
- [x] Comandos `net tcp status/connect`, `net socket status/table`,
  `http get/status` e diagnostico agrupado da S2.7.
- [x] Vetores puros e invariantes adicionados ao `regcheck full`.
- [x] Suite principal validada no QEMU padrao com
  `net check qemu tcp net-pci-00-03.0 example.com`.
- [x] Revalidar entrada Shift e integridade da pilha/heap apos separar os
  frames das suites: `echo A:B?` funcionou e `regcheck full` permaneceu em
  `OK` antes e depois da rede.
- [x] GET individual revalidado com `neverssl.com`: HTTP 302, headers e
  corpo recebidos, seguido por `regcheck full` em `OK`.
- [ ] Revalidar a suite agrupada apos adicionar repeticao controlada para
  timeouts de transporte do servidor externo.
- [ ] Cobertura complementar: perda/retransmissao com peer, RST, janela zero,
  handles obsoletos, tabela cheia, fallbacks e Classic/Modern.

## Sequencia prevista apos S2.7

1. **S2.8 - Multiplas NICs:** ampliar a abstracao e adicionar RTL8139 sem
   prender os protocolos ao E1000.

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F3 | Abrir janela de Rede |
| F5 | Atualizar status de rede |
| Esc | Fechar janela |
| Tab | Alternar entre seções |
| Setas | Navegar na lista |
| Enter | Selecionar opção |
| Space | Ativar/desativar adaptador |

---

## Fase 1: Driver de NIC (Ethernet) ⬜

### 1.1 Detecção de NIC via PCI

- [x] PCI enumeration completa — `pci.c:91-115`
- [x] Leitura/Escrita de config space — `pci.c:37-47`
- [x] Busca por class/vendor — `pci.c:117-137`
- [x] Bus mastering enable — `pci.c:139-144`
- [x] Filtrar class 0x02 no `network_manager`, sem ampliar a API mutavel PCI
- [ ] Criar função `pci_get_network_device()` para encontrar NIC
- [x] Reconhecer E1000 e RTL8139 pelo snapshot PCI

### 1.2 Driver RTL8139 (comum em VMs)

- [ ] Criar módulo `src/net/rtl8139.c` e `rtl8139.h`
- [ ] Criar struct `rtl8139_device_t`:
  ```
  - io_base          = uint16_t (port I/O)
  - mac_address[6]   = uint8_t
  - rx_buffer         = uint8_t* (256KB + 16 bytes)
  - tx_buffers[4]     = uint8_t* (4 × 2KB)
  - tx_cur_descriptor = uint8_t
  - irq               = uint8_t
  - initialized       = bool
  ```
- [ ] Implementar `rtl8139_init()`:
  - [ ] Ler BAR0 e BAR1 do PCI config space
  - [ ] Habilitar bus mastering via PCI
  - [ ] Soft reset (escrever 0x10 no COMMAND register)
  - [ ] Ler MAC address (registradores 0x00-0x05)
  - [ ] Alocar RX buffer (256KB + 16 bytes, alinhado em 4 bytes)
  - [ ] Alocar 4 TX buffers (2KB cada)
  - [ ] Configurar RX: RCR (接收配置), RBSTART (RX buffer start)
  - [ ] Configurar TX: TSD0-TSD3 (TX status)
  - [ ] Habilitar RX + TX (COMMAND register bits 0-1)
  - [ ] Registrar handler de IRQ
- [ ] Implementar `rtl8139_send_packet(data, length)`:
  - [ ] Copiar dados para TX buffer atual
  - [ ] Escrever tamanho no TSD register
  - [ ] Avançar descriptor (tx_cur = (tx_cur + 1) % 4)
  - [ ] Esperar confirmação (TSDOWNOWN bit)
- [ ] Implementar `rtl8139_receive_packet()`:
  - [ ] Ler CAPR (Current Address of Packet Read)
  - [ ] Extrair packet do RX buffer
  - [ ] Processar header (status, length)
  - [ ] Chamar callback de recepção
- [ ] Implementar `rtl8139_handler()` (IRQ handler):
  - [ ] Ler Interrupt Status Register (ISR)
  - [ ] Limpar bits de interrupção
  - [ ] Processar packets recebidos
  - [ ] Processar erros (overflow, CRC, etc.)

### 1.3 Abstração de Interface de Rede

- [ ] Criar módulo `src/net/netdev.c` e `netdev.h`
- [ ] Criar struct `net_device_t` (abstrata):
  ```
  - name[16]         = "eth0"
  - mac_address[6]   = uint8_t
  - ip_address       = uint32_t
  - subnet_mask      = uint32_t
  - gateway          = uint32_t
  - dns[2]           = uint32_t
  - status           = enum (DOWN, UP, ERROR)
  - speed            = uint32_t (Mbps)
  - mtu              = uint16_t (default 1500)
  - tx_packets       = uint32_t
  - rx_packets       = uint32_t
  - tx_bytes         = uint64_t
  - rx_bytes         = uint64_t
  - tx_errors        = uint32_t
  - rx_errors        = uint32_t
  - ops              = net_ops_t (function pointers)
  ```
- [ ] Criar struct `net_ops_t`:
  ```
  - init()           = int (*init)(net_device_t*)
  - send()           = int (*send)(net_device_t*, void*, size_t)
  - receive()        = int (*receive)(net_device_t*, void*, size_t*)
  - set_mac()        = int (*set_mac)(net_device_t*, uint8_t*)
  - get_stats()      = int (*get_stats)(net_device_t*, net_stats_t*)
  - shutdown()       = int (*shutdown)(net_device_t*)
  ```
- [ ] Criar array `net_devices[MAX_NET_DEVICES]` (máximo 4)
- [ ] Criar função `netdev_register(dev)` para registrar NIC
- [ ] Criar função `netdev_get(name)` para obter device por nome
- [ ] Criar função `netdev_get_all()` para listar todos
- [ ] Criar função `netdev_set_ip(name, ip)` para configurar IP
- [ ] Criar função `netdev_set_gateway(name, gw)` para gateway
- [ ] Criar função `netdev_set_dns(name, dns)` para DNS
- [ ] Criar função `netdev_up(name)` / `netdev_down(name)` para ativar/desativar

### 1.4 Gerenciamento de IP

- [ ] Criar módulo `src/net/ipconfig.c` e `ipconfig.h`
- [ ] Criar struct `ip_config_t`:
  ```
  - ip_address       = uint32_t
  - subnet_mask      = uint32_t
  - gateway          = uint32_t
  - dns_primary      = uint32_t
  - dns_secondary    = uint32_t
  - dhcp_enabled     = bool
  - mac_address[6]   = uint8_t
  ```
- [ ] Criar função `ipconfig_set_static(dev, config)` para IP estático
- [ ] Criar função `ipconfig_set_dhcp(dev)` para DHCP
- [ ] Criar função `ipconfig_get(dev, config)` para obter configuração
- [ ] Criar função `ipconfig_show(dev)` para mostrar na tela
- [ ] Salvar configurações em `/network/config.txt`
- [ ] Carregar configurações no boot

### 1.5 Integração com PCI

- [ ] Criar hook no `kernel.c` para detectar NIC após PCI init
- [ ] Auto-inicializar driver apropriado se NIC encontrada
- [ ] Mostrar mensagem no boot: "NIC: RTL8139 em IRQ XX"
- [ ] Criar comando shell `netdev` para listar dispositivos
- [ ] Criar comando shell `netdev info <name>` para detalhes

---

## Fase 2: Stack TCP/IP Básico ⬜

### 2.1 Camada de Enlace (Ethernet)

- [x] Criar modulo `src/core/ethernet.c` e header publico
- [x] Expor visao sincrona validada do frame Ethernet:
  ```
  - dst_mac[6]   = uint8_t
  - src_mac[6]   = uint8_t
  - ether_type   = uint16_t (0x0800=IP, 0x0806=ARP)
  ```
- [x] Criar funcao `ethernet_send()` desacoplada por callback
- [x] Receber frames por fila fixa e polling acionado por evento
- [x] Validar e analisar cabecalho Ethernet II
- [x] Implementar transmissao e recepcao broadcast
- [x] Criar tabela ARP cache (`arp_cache[ARP_CACHE_CAPACITY]`)

### 2.2 Protocolo ARP

- [x] Criar modulo `src/core/arp.c` e `src/include/core/arp.h`
- [x] Serializar explicitamente o pacote ARP, sem struct packed:
  ```
  - hw_type       = uint16_t (1=Ethernet)
  - proto_type    = uint16_t (0x0800=IPv4)
  - hw_addr_len   = uint8_t (6)
  - proto_addr_len= uint8_t (4)
  - operation     = uint16_t (1=Request, 2=Reply)
  - sender_mac[6] = uint8_t
  - sender_ip     = uint32_t
  - target_mac[6] = uint8_t
  - target_ip     = uint32_t
  ```
- [x] Enviar request ARP sob demanda
- [x] Responder automaticamente a request valido para o IP local
- [x] Criar `arp_resolve(ip, mac, resolved)` assincrono e com cache
- [x] Adicionar aprendizado restrito ao cache
- [x] Buscar resolucao pendente ou concluida no cache
- [x] Limpar, expirar e substituir entradas de forma limitada
- [x] Timeout de cache (30 segundos)
- [x] Integrar com despacho e transmissao Ethernet

### 2.3 Protocolo IPv4

- [ ] Criar módulo `src/net/ipv4.c` e `ipv4.h`
- [ ] Criar struct `ipv4_header_t`:
  ```
  - version_ihl  = uint8_t (4 bits cada)
  - tos           = uint8_t
  - total_length  = uint16_t
  - identification= uint16_t
  - flags_frag    = uint16_t
  - ttl           = uint8_t (default 64)
  - protocol      = uint8_t (1=ICMP, 6=TCP, 17=UDP)
  - checksum      = uint16_t
  - src_ip        = uint32_t
  - dst_ip        = uint32_t
  ```
- [ ] Criar função `ipv4_send(dev, src_ip, dst_ip, protocol, data, length)`
- [ ] Criar função `ipv4_receive(dev, data, length)`
- [ ] Criar função `ipv4_checksum(header)` para calcular checksum
- [ ] Criar função `ipv4_fragment(data, length, mtu)` para fragmentação
- [ ] Criar função `ipv4_reassemble(fragments[])` para reassembly
- [ ] Integrar com ethernet layer

### 2.4 Protocolo ICMP

- [ ] Criar módulo `src/net/icmp.c` e `icmp.h`
- [ ] Criar struct `icmp_header_t`:
  ```
  - type          = uint8_t (8=Echo Request, 0=Echo Reply)
  - code          = uint8_t
  - checksum      = uint16_t
  - id            = uint16_t
  - sequence      = uint16_t
  ```
- [ ] Criar função `icmp_ping(dev, target_ip, count, timeout)`:
  - [ ] Enviar Echo Request
  - [ ] Aguardar Echo Reply
  - [ ] Calcular RTT (Round Trip Time)
  - [ ] Retornar estatísticas (sent, received, loss%, min/avg/max RTT)
- [ ] Criar função `icmp_receive(dev, data, length)` para processar respostas
- [ ] Criar função `icmp_traceroute(dev, target_ip, max_hops)` para rota
- [ ] Integrar com IPv4 layer

### 2.5 Protocolo UDP

- [ ] Criar módulo `src/net/udp.c` e `udp.h`
- [ ] Criar struct `udp_header_t`:
  ```
  - src_port      = uint16_t
  - dst_port      = uint16_t
  - length        = uint16_t
  - checksum      = uint16_t
  ```
- [ ] Criar struct `udp_socket_t`:
  ```
  - local_port    = uint16_t
  - remote_port   = uint16_t
  - remote_ip     = uint32_t
  - recv_buffer   = uint8_t*
  - recv_size     = uint32_t
  - callback      = void (*on_receive)(uint8_t*, uint32_t)
  ```
- [ ] Criar função `udp_socket_create()` para criar socket
- [ ] Criar função `udp_socket_bind(socket, port)` para vincular porta
- [ ] Criar função `udp_socket_send(socket, data, length, dst_ip, dst_port)`
- [ ] Criar função `udp_socket_receive(socket, buffer, length)` para receber
- [ ] Criar função `udp_socket_close(socket)` para fechar
- [ ] Integrar com IPv4 layer

### 2.6 Protocolo TCP

- [x] Criar `src/core/tcp.c` e `src/include/core/tcp.h`.
- [x] Serializar e validar o cabeçalho/checksum sem structs packed.
- [x] Implementar abertura ativa SYN/SYN-ACK/ACK.
- [x] Implementar dados bidirecionais, ACK imediato e sequenciamento.
- [x] Implementar FIN, RST, fechamento ativo/passivo e `TIME_WAIT`.
- [x] Implementar MSS 536, janela RX e um segmento não confirmado.
- [x] Implementar RTO, SRTT/RTTVAR, Karn, backoff e três retransmissões.
- [x] Integrar com IPv4 e sockets nativos.
- [ ] Implementar abertura passiva, `LISTEN`, `ACCEPT` e backlog.
- [ ] Implementar reassembly, SACK, window scaling e controle de congestão
  completo.

---

## Fase 3: Protocolos de Rede ⬜

### 3.1 DHCP Client

- [ ] Criar módulo `src/net/dhcp.c` e `dhcp.h`
- [ ] Criar struct `dhcp_message_t`:
  ```
  - op            = uint8_t (1=Request, 2=Reply)
  - htype         = uint8_t (1=Ethernet)
  - hlen          = uint8_t (6)
  - hops          = uint8_t
  - xid           = uint32_t (transaction ID)
  - secs          = uint16_t
  - flags         = uint16_t
  - ciaddr        = uint32_t
  - yiaddr        = uint32_t (your IP)
  - siaddr        = uint32_t (server IP)
  - giaddr        = uint32_t (gateway IP)
  - chaddr[16]    = uint8_t (client MAC)
  - sname[64]     = uint8_t
  - file[128]     = uint8_t
  - magic[4]      = {0x63, 0x82, 0x53, 0x63}
  - options[]     = uint8_t (DHCP options)
  ```
- [ ] Implementar states DHCP:
  - [ ] INIT → SELECTING → REQUESTING → BOUND
  - [ ] RENEWING → REBINDING → REBOUND
- [ ] Criar função `dhcp_discover(dev)` para enviar DHCPDISCOVER
- [ ] Criar função `dhcp_request(dev, server_ip, requested_ip)`
- [ ] Criar função `dhcp_release(dev)` para liberar lease
- [ ] Criar função `dhcp_handle_reply(dev, data)` para processar DHCPOFFER/DHCPACK
- [ ] Extrair opções DHCP:
  - [ ] Opção 1: Subnet Mask
  - [ ] Opção 3: Router (Gateway)
  - [ ] Opção 6: DNS Server
  - [ ] Opção 51: Lease Time
  - [ ] Opção 53: Message Type
- [ ] Criar função `dhcp_get_config(dev, config)` para obter configuração
- [ ] Integrar com UDP socket (porta 67/68)
- [ ] Criar renew automático (50% do lease time)

### 3.2 DNS Client

- [ ] Criar módulo `src/net/dns.c` e `dns.h`
- [ ] Criar struct `dns_header_t`:
  ```
  - id            = uint16_t
  - flags         = uint16_t (0x0100 = standard query)
  - questions     = uint16_t
  - answers       = uint16_t
  - authority     = uint16_t
  - additional    = uint16_t
  ```
- [ ] Criar struct `dns_record_t`:
  ```
  - name[256]     = "example.com"
  - type          = uint16_t (1=A, 5=CNAME, 28=AAAA)
  - class         = uint16_t (1=IN)
  - ttl           = uint32_t
  - rdata_len     = uint16_t
  - rdata[]       = uint8_t
  ```
- [ ] Criar função `dns_resolve(domain, ip)`:
  - [ ] Construir query packet
  - [ ] Enviar via UDP para DNS server (porta 53)
  - [ ] Aguardar resposta
  - [ ] Parse da resposta
  - [ ] Retornar IP
- [ ] Criar função `dns_query(server, domain, type)` para query genérica
- [ ] Criar cache DNS (`dns_cache[MAX_DNS_ENTRIES]`)
- [ ] Criar função `dns_cache_add(domain, ip, ttl)`
- [ ] Criar função `dns_cache_find(domain)` para lookup
- [ ] Criar timeout de cache (baseado no TTL)
- [ ] Integrar com UDP socket

### 3.3 Resolução de Nomes

- [ ] Criar módulo `src/net/hostname.c` e `hostname.h`
- [ ] Criar arquivo `/etc/hosts` para mapeamento estático
- [ ] Criar função `hostname_resolve(name, ip)`:
  - [ ] Primeiro verificar `/etc/hosts`
  - [ ] Se não encontrado, usar DNS
  - [ ] Cache resultado
- [ ] Criar função `hostname_get()` para obter nome do host
- [ ] Criar função `hostname_set(name)` para definir nome
- [ ] Integrar com filesystem

---

## Fase 4: Serviços de Rede ⬜

### 4.1 Cliente HTTP

- [x] Criar módulo `src/core/http.c` e `src/include/core/http.h`
- [ ] Criar struct `http_request_t`:
  ```
  - method[8]     = "GET"
  - url[256]      = "/index.html"
  - host[128]     = "example.com"
  - headers[][64] = {"User-Agent: ZephyrOS/0.1"}
  - body[]        = NULL
  ```
- [ ] Criar struct `http_response_t`:
  ```
  - status_code   = uint16_t (200, 404, etc.)
  - status_text[32]= "OK"
  - headers[][64] = {"Content-Type: text/html"}
  - body          = uint8_t*
  - body_length   = uint32_t
  ```
- [x] Criar início/manutenção/status para uma sessão GET
- [ ] Criar função `http_post(url, data, response)` para POST request
- [x] Parse de URL HTTP (protocolo, host, porta e caminho)
- [x] Conexão TCP com host:porta
- [x] Envio de request HTTP/1.1
- [x] Receção por `Content-Length` ou fechamento
- [x] Parse limitado de status e headers
- [x] Corpo em buffer fixo de 16 KiB
- [ ] Criar função `http_download_file(url, filepath)` para download
- [x] Sem alocação dinâmica; reset reutiliza os buffers fixos

### 4.2 Cliente FTP (básico)

- [ ] Criar módulo `src/net/ftp.c` e `ftp.h`
- [ ] Criar conexão FTP via TCP (porta 21)
- [ ] Implementar comandos básicos:
  - [ ] USER/PASS para autenticação
  - [ ] LIST para listar arquivos
  - [ ] RETR para baixar arquivo
  - [ ] STOR para enviar arquivo
  - [ ] CWD/PWD para navegar
  - [ ] QUIT para desconectar
- [ ] Criar função `ftp_connect(host, user, pass)`
- [ ] Criar função `ftp_list(path)` para listar
- [ ] Criar função `ftp_download(remote, local)` para baixar
- [ ] Criar função `ftp_upload(local, remote)` para enviar
- [ ] Criar função `ftp_disconnect()`

### 4.3 Servidor HTTP Simples

- [ ] Criar módulo `src/net/httpserver.c` e `httpserver.h`
- [ ] Criar TCP server socket (porta 80)
- [ ] Aguardar conexões (listen + accept)
- [ ] Parse de request HTTP recebido
- [ ] Servir arquivos do filesystem:
  - [ ] Mapear URL para path no disco
  - [ ] Ler arquivo com `fs_read_file()`
  - [ ] Enviar response com dados
- [ ] Criar diretório `/www/` para arquivos web
- [ ] Suportar index.html como página padrão
- [ ] Criar MIME types básicos (text/html, text/plain, image/bmp)
- [ ] Criar comando shell `httpserver start/stop` para iniciar/parar

### 4.4 Servidor TFTP (para boot em rede)

- [ ] Criar módulo `src/net/tftp.c` e `tftp.h`
- [ ] Implementar TFTP server (porta 69 UDP)
- [ ] Suportar comandos RRQ (read) e WRQ (write)
- [ ] Implementar block numbering e ACK
- [ ] Implementar timeout e retransmissão
- [ ] Criar função `tftp_serve(directory)` para servir arquivos
- [ ] Útil para PXE boot (carregar kernel via rede)

### 4.5 NetBIOS/Name Resolution

- [ ] Criar módulo `src/net/netbios.c` e `netbios.h`
- [ ] Implementar NetBIOS Name Query (porta 137 UDP)
- [ ] Criar tabela de nomes (`netbios_names[]`)
- [ ] Criar função `netbios_register(name)` para registrar nome
- [ ] Criar função `netbios_resolve(name, ip)` para resolver
- [ ] Criar função `netbios_broadcast(name)` para broadcast
- [ ] Compatível com redes ZephyrOS (visualização na rede)

---

## Fase 5: Interface TUI do Gerenciador ⬜

### 5.1 Janela Principal

- [ ] Criar módulo `src/net/netui.c` e `netui.h`
- [ ] Criar janela "Gerenciador de Rede"
- [ ] Mostrar status: "Conectado" ou "Desconectado"
- [ ] Mostrar: IP, Gateway, DNS, MAC
- [ ] Mostrar: Adaptadores de rede listados
- [ ] Botão: "Conectar" / "Desconectar"
- [ ] Botão: "Configurar IP"
- [ ] Botão: "Diagnóstico"
- [ ] Botão: "Firewall"
- [ ] Integrar com window manager
- [ ] Registrar na taskbar com ícone de rede
- [ ] Registrar no menu Start
- [ ] Atalho F3 para abrir

### 5.2 Tela de Adaptadores

- [ ] Criar tela "Adaptadores de Rede"
- [ ] Lista de adaptadores (eth0, eth1, etc.)
- [ ] Para cada adaptador:
  - [ ] Nome e descrição
  - [ ] MAC address
  - [ ] Status (Ativo/Inativo)
  - [ ] Velocidade (Mbps)
  - [ ] IP configurado
  - [ ] Pacotes enviados/recebidos
- [ ] Botão "Ativar" / "Desativar"
- [ ] Botão "Propriedades" para configuração
- [ ] Botão "Diagnóstico" para testar

### 5.3 Tela de Configuração IP

- [ ] Criar tela "Configuração de IP"
- [ ] Opção: "Obter IP automaticamente (DHCP)"
- [ ] Opção: "Usar IP estático"
  - [ ] Campo: Endereço IP
  - [ ] Campo: Máscara de sub-rede
  - [ ] Campo: Gateway padrão
  - [ ] Campo: DNS primário
  - [ ] Campo: DNS secundário
- [ ] Botão "Aplicar"
- [ ] Botão "Cancelar"
- [ ] Botão "Testar Conexão" (ping ao gateway)

### 5.4 Tela de Status de Conexão

- [ ] Criar tela "Status da Conexão"
- [ ] Mostrar: Estado (Conectado/Desconectado/Identificando...)
- [ ] Mostrar: Duração da conexão
- [ ] Mostrar: Velocidade do link
- [ ] Mostrar: Bytes enviados/recebidos
- [ ] Mostrar: Pacotes enviados/recebidos
- [ ] Mostrar: Erros e descartes
- [ ] Botão "Detalhes" para info completa
- [ ] Botão "Fechar" para desconectar

### 5.5 Tela de Diagnóstico

- [ ] Criar tela "Diagnóstico de Rede"
- [ ] Teste 1: Adaptador ativo?
- [ ] Teste 2: IP configurado?
- [ ] Teste 3: Gateway acessível? (ping)
- [ ] Teste 4: DNS funcionando? (resolve localhost)
- [ ] Teste 5: Internet acessível? (ping 8.8.8.8)
- [ ] Para cada teste: Pass/Fail com descrição
- [ ] Sugestões de correção para cada falha
- [ ] Botão "Executar Todos" para testar sequencialmente
- [ ] Botão "Corrigir Automaticamente" (renovar DHCP, flush DNS)

### 5.6 Tela de Estatísticas

- [ ] Criar tela "Estatísticas de Rede"
- [ ] Mostrar: Total de conexões
- [ ] Mostrar: Total de bytes enviados/recebidos
- [ ] Mostrar: Total de pacotes enviados/recebidos
- [ ] Mostrar: Erros de transmissão/recepção
- [ ] Mostrar: Tabela ARP (IP ↔ MAC)
- [ ] Mostrar: Tabela de rotas
- [ ] Mostrar: Conexões TCP ativas
- [ ] Mostrar: Portas em escuta
- [ ] Botão "Limpar Estatísticas"

### 5.7 Tela de Firewall

- [ ] Criar tela "Firewall"
- [ ] Opção: "Firewall ligado/desligado"
- [ ] Lista de regras (Entrada/Saída)
- [ ] Botão "Adicionar Regra"
  - [ ] Tipo: Entrada/Saída
  - [ ] Protocolo: TCP/UDP/ICMP/Qualquer
  - [ ] Porta: específica ou range
  - [ ] IP de origem/destino
  - [ ] Ação: Permitir/Bloquear
- [ ] Botão "Remover Regra"
- [ ] Botão "Editar Regra"
- [ ] Botão "Restaurar Padrão"
- [ ] Log de conexões bloqueadas

### 5.8 Integração com Shell

- [ ] Comando `net` — abrir gerenciador de rede
- [ ] Comando `ifconfig` — mostrar configuração de rede
- [ ] Comando `ifconfig <dev> up/down` — ativar/desativar
- [ ] Comando `ifconfig <dev> ip <addr>` — configurar IP
- [ ] Comando `ping <host>` — testar conectividade
- [ ] Comando `ping -c <count> <host>` — ping com contagem
- [ ] Comando `traceroute <host>` — rastrear rota
- [ ] Comando `nslookup <domain>` — consultar DNS
- [ ] Comando `netstat` — mostrar conexões ativas
- [ ] Comando `netstat -a` — mostrar todas as portas
- [ ] Comando `netstat -s` — mostrar estatísticas
- [ ] Comando `arp` — mostrar tabela ARP
- [ ] Comando `route` — mostrar tabela de rotas
- [ ] Comando `hostname` — mostrar/definir nome do host
- [ ] Comando `wget <url>` — baixar arquivo via HTTP
- [ ] Comando `curl <url>` — requisição HTTP simples
- [ ] Comando `ftp <host>` — conectar via FTP
- [ ] Comando `ipconfig /release` — liberar DHCP
- [ ] Comando `ipconfig /renew` — renovar DHCP
- [ ] Comando `ipconfig /flushdns` — limpar cache DNS
- [ ] Comando `firewall status/rules/add/remove` — gerenciar firewall

---

## Fase 6: Comandos de Rede (Shell) ⬜

### 6.1 Comandos Básicos

- [ ] Criar comando `ip` (atalho para ifconfig)
- [ ] Criar comando `route` para tabela de rotas
- [ ] Criar comando `arp` para tabela ARP
- [ ] Criar comando `neighbors` para vizinhos
- [ ] Criar comando `link` para status dos links

### 6.2 Comandos de Diagnóstico

- [ ] Criar comando `mtr` (combinação ping + traceroute)
- [ ] Criar comando `pathping` (análise de perda de pacotes)
- [ ] Criar comando `nmap` básico (scan de portas)
- [ ] Criar comando `whois` (informações de domínio)

### 6.3 Comandos de Transferência

- [ ] Criar comando `scp` (cópia segura via SSH - futuro)
- [ ] Criar comando `rsync` (sincronização - futuro)

---

## Fase 7: Firewall e Segurança ⬜

### 7.1 Firewall Básico

- [ ] Criar módulo `src/net/firewall.c` e `firewall.h`
- [ ] Criar struct `firewall_rule_t`:
  ```
  - direction     = enum (INBOUND, OUTBOUND, BOTH)
  - protocol      = enum (TCP, UDP, ICMP, ANY)
  - src_ip        = uint32_t (0 = qualquer)
  - dst_ip        = uint32_t (0 = qualquer)
  - src_port      = uint16_t (0 = qualquer)
  - dst_port      = uint16_t (0 = qualquer)
  - action        = enum (ALLOW, DENY)
  - log           = bool
  - description[64]= "Regra descritiva"
  ```
- [ ] Criar array `firewall_rules[MAX_RULES]` (máximo 32 regras)
- [ ] Criar função `firewall_init()` para carregar regras
- [ ] Criar função `firewall_add_rule(rule)` para adicionar
- [ ] Criar função `firewall_remove_rule(index)` para remover
- [ ] Criar função `firewall_check(direction, protocol, src_ip, dst_ip, src_port, dst_port)`:
  - [ ] Iterar regras na ordem
  - [ ] Retornar ALLOW ou DENY
  - [ ] Registrar em log se `log`=true
- [ ] Criar função `firewall_enable()` / `firewall_disable()`
- [ ] Criar regras padrão:
  - [ ] Permitir tráfego outbound (padrão)
  - [ ] Bloquear inbound não solicitado
  - [ ] Permitir ICMP (ping) outbound
  - [ ] Permitir DNS outbound (porta 53)
  - [ ] Permitir DHCP outbound (porta 67/68)
- [ ] Integrar com TCP/UDP/IP send/receive

### 7.2 Log de Firewall

- [ ] Criar arquivo `/network/firewall.log`
- [ ] Criar struct `firewall_log_entry_t`:
  ```
  - timestamp     = uint32_t
  - direction     = enum
  - protocol      = uint8_t
  - src_ip        = uint32_t
  - dst_ip        = uint32_t
  - src_port      = uint16_t
  - dst_port      = uint16_t
  - action        = enum (ALLOWED, DENIED)
  - rule_index    = uint8_t
  ```
- [ ] Criar função `firewall_log_add(entry)` para registrar
- [ ] Criar função `firewall_log_read(count)` para ler
- [ ] Criar função `firewall_log_clear()` para limpar
- [ ] Criar comando shell `firewall log` para mostrar

### 7.3 Proteção Contra Ataques

- [ ] Criar detecção de SYN flood (muitas conexões SYN)
- [ ] Criar detecção de ping flood (muitos ICMP echo)
- [ ] Criar detecção de port scan (muitas portas diferentes)
- [ ] Criar rate limiting por IP
- [ ] Criar blacklist de IPs (bloqueio permanente)
- [ ] Criar função `firewall_block_ip(ip, duration)` para bloqueio temporário
- [ ] Criar notificação quando ataque detectado

---

## Limitações Técnicas

| Limite | Valor | Descrição |
|--------|-------|-----------|
| Máximo de NICs | 4 | Array estático |
| Tamanho do MTU | 1500 | Ethernet padrão |
| Máximo de sockets TCP | 16 | Array estático |
| Máximo de sockets UDP | 16 | Array estático |
| Máximo de regras firewall | 32 | Array estático |
| Máximo de entradas ARP | 32 | Cache limitado |
| Máximo de entradas DNS | 16 | Cache limitado |
| Tamanho do RX buffer | 256KB | RTL8139 padrão |
| Tamanho do TX buffer | 2KB × 4 | RTL8139 padrão |
| Velocidade máxima | 100 Mbps | RTL8139 Fast Ethernet |
| Sem WiFi | Nenhum | Apenas Ethernet |
| Sem IPv6 | Nenhum | Apenas IPv4 |
| Sem criptografia | Nenhum | Sem SSL/TLS/SSH |
| Sem VPN | Nenhum | Sem túnel seguro |
| Sem QoS | Nenhum | Sem priorização |
- Sem SNMP | Nenhum | Sem monitoramento remoto
- Sem SNMP trap | Nenhum | Sem alertas remotos
- Sem NetFlow | Nenhum | Sem fluxo de tráfego
- Sem sFlow | Nenhum | Sem amostragem
- Sem IPsec | Nenhum | Sem segurança de camada 3
- Sem 802.1X | Nenhum | Sem autenticação por porta
- Sem VLAN | Nenhum | Sem segmentação
- Sem bonding | Nenhum | Sem agregação de links
- Sem bridging | Nenhum | Sem ponte de rede
- Sem tunneling | Nenhum | Sem túneis
- Sem multicast | Nenhum | Sem grupo multicast
- Sem broadcast storm protection | Nenhum | Sem proteção contra tempestade

---

## Notas de Implementação

1. **RTL8139 como driver inicial** — O RTL8139 é o NIC mais comum em emuladores (QEMU, VirtualBox, Bochs). É simples de implementar e suporta 100 Mbps Fast Ethernet.

2. **Sem WiFi** — O ZephyrOS não suporta WiFi. Seria necessário driver para chip WiFi (RTL8188, Intel Wireless, etc.) que é muito mais complexo que Ethernet.

3. **TCP/IP simplificado** — O stack TCP/IP é educacional. Não suporta todas as opções RFC (window scaling, SACK, ECN, etc.). É suficiente para HTTP básico e downloads.

4. **Sem criptografia** — Não há SSL/TLS. O HTTP é plaintext. Downloads e uploads não são criptografados. Para uso real, seria necessário AES/SHA/RSA.

5. **Firewall stateless** — O firewall é baseado em regras estáticas (port, IP, protocol). Não há stateful inspection (não rastreia estado de conexões TCP).

6. **Integração existente** — O PCI driver já detecta dispositivos. O filesystem já suporta operações completas. O stack de rede precisa apenas usar essas APIs.

7. **Performance** — O driver RTL8139 usa PIO (não DMA) para simplificar. Performance será limitada (~10 Mbps real vs 100 Mbps teórico).

8. **Memória** — Cada socket TCP usa ~8KB de memória (send + recv buffers). Com 16 sockets, são ~128KB. O RX buffer do RTL8139 usa 256KB. Total ~400KB de RAM para rede.

---

## Referências

- `src/drivers/pci.c` — PCI bus enumeration (137 linhas)
- `src/drivers/ata.c` — ATA disk driver (196 linhas)
- `src/fs/fs.c` — Unified FS API (194 linhas)
- `src/fs/fat12.c` — FAT12 filesystem (1145 linhas)
- `src/memory/memory.c` — Memory manager (206 linhas)
- `src/shell/shell.c` — Shell commands (518 linhas)
- `src/kernel/kernel.c` — Kernel init (203 linhas)
- `src/settings/settings.c` — Settings panel (549 linhas)
- RTL8139 Datasheet — RealTek documentation
- RFC 791 — Internet Protocol (IPv4)
- RFC 792 — Internet Control Message Protocol (ICMP)
- RFC 768 — User Datagram Protocol (UDP)
- RFC 793 — Transmission Control Protocol (TCP)
- RFC 2131 — Dynamic Host Configuration Protocol (DHCP)
- RFC 1035 — Domain Names (DNS)
