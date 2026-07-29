# Roadmap 05 - Sistema e ecossistema

## Objetivo

Planejar recursos de plataforma que dependem de uma base estavel: dispositivos,
energia, rede, atualizacoes e aplicativos opcionais. Esta frente nao deve
antecipar interfaces ou permissoes que ainda nao existem.

## Ordem de dependencia

1. Confiabilidade, diagnostico e contratos de kernel.
2. Plataforma de aplicativos e formato de pacote.
3. Servicos de dispositivo, energia e rede.
4. Atualizacao segura e distribuicao de aplicativos.
5. Ferramentas produtivas, multimidia e jogos.

## Etapa S1 - Servicos de sistema

- [x] S1.1: inventario nativo somente de leitura para dispositivos e comando
  `power status` com capacidades reais, sem ACPI ou alteracao de hardware.
- [x] S1.1 validada manualmente no QEMU com `devices`, `devices -v`,
  `device-info`, `device-scan`, `power status`, `health` e a matriz de
  regressao, sem erros bloqueantes.
- [x] S1.2: snapshot ACPI somente de leitura com RSDP, RSDT/XSDT, FADT, DSDT,
  FACS, `acpi status` e integracao com `Power`/`health`.
- [x] S1.2 validada no QEMU padrao e sem ACPI: tabelas completas em zero
  ticks, fallback controlado, sintaxe invalida e matriz de regressao aprovados.
- [x] S1.3: snapshot somente de leitura de PM1a/PM1b, modo ACPI observado e
  reconhecedor AML limitado para `_S5_`, sem transicoes ou escritas.
- [x] S1.3 validada manualmente no QEMU padrao e sem ACPI, incluindo
  `health`, PM1, `_S5_`, fallback, comandos diagnosticos, entrada ZAPP e
  matriz Classic/Modern.
- [x] S1.4: desligamento fisico por PM1 System I/O, aquisicao tardia do modo
  ACPI, prontidao fechada e fallback terminal `CLI+HLT`.
- [x] Todos os caminhos de shutdown centralizados em `power_shutdown()`, sem
  a porta privada `0xB004` do QEMU.
- [x] S1.4 validada manualmente no QEMU padrao: diagnosticos e regressao
  permaneceram operacionais, sintaxe invalida foi recusada e `shutdown`
  encerrou fisicamente a VM.
- [ ] Cobertura complementar: repetir sem ACPI e pelos menus Classic/Modern
  com e sem Task Manager, sem bloquear a conclusao da entrega principal.
- [x] Gerenciador de dispositivos com inventario e erros controlados.
- [x] Gerenciador de energia com estados claros e desligamento S5 seguro.
- [ ] Evolucao do filesystem somente quando novos recursos exigirem metadados
  ou operacoes inexistentes.

## Etapa S2 - Rede e atualizacoes

- [x] S2.1: arquitetura observavel de rede definida com snapshot PCI estatico,
  componente `Network` e comandos `net status`, `net devices` e `net info`.
- [x] E1000 e RTL8139 reconhecidas sem habilitar bus mastering, acessar BARs,
  registrar IRQ ou fingir conectividade.
- [x] `regcheck full` automatiza a varredura e a consistencia de Devices,
  Network, ACPI e Power, com uma unica validacao manual por `F12`.
- [x] S2.1 validada manualmente com `regcheck full` no QEMU padrao e sem NIC;
  ambos os cenarios concluiram em `OK` apos `F12`.
- [x] S2.2: driver E1000 `8086:100E` implementado com MMIO, DMA, IRQ, MAC,
  link e teste TX L2, preservando contratos e IDs da S2.1. Validada com Q3,
  build limpo, QEMU padrao, sem NIC e com RTL8139; os fallbacks concluiram
  com falhas controladas e `RegCheck: OK`.
- [x] S2.3: camada Ethernet concluida com fila RX fixa, processamento fora da
  IRQ, montagem/parsing de frames, abstracao minima e diagnostico Shell.
  Validada com polling ocioso zerado, TX, `device-scan` e `regcheck full`;
  injecao externa de RX e Classic/Modern permanecem como cobertura
  complementar, sem ARP ou IPv4.
- [x] S2.4: ARP implementado com despacho EtherType, cache de 32 entradas,
  requests/replies, resolucao assincrona, comandos `net arp` e diagnostico
  agregado `net check [id]`, alem da suite ativa `net check qemu`. Validada
  pelo usuario no QEMU com reply, cache hit, timeout, polling e invariantes
  em `OK`.
- [ ] Cobertura complementar da S2.4: peer externo, fallbacks sem
  NIC/RTL8139, `device-scan`, sintaxe invalida e Classic/Modern; esses
  cenarios nao bloqueiam a conclusao.
- [x] S2.5 concluida e validada: IPv4 estatico com checksum, rota
  direta/gateway,
  despacho por protocolo e ICMP Echo com resposta automatica, ping
  cooperativo, RTT e timeout por tentativa. `net check qemu` agora agrupa
  ARP, IPv4, ICMP, polling e invariantes. A suite concluiu em `OK` no QEMU
  padrao e o ping individual recebeu quatro replies sem perdas.
- [ ] Cobertura complementar da S2.5: sem NIC, RTL8139, peer externo,
  entradas malformadas e modos Classic/Modern.
- [x] S2.6 concluida e validada: UDP, DHCP e DNS sobre a base IPv4; a suite
  agrupada confirmou DORA, lease, DNS A, cache sem novo TX, ICMP, polling e
  invariantes em `OK` no QEMU padrao.
- [ ] Cobertura complementar da S2.6: renovacao/liberacao individual,
  ausencia de NIC, RTL8139, peer externo e modos Classic/Modern.
- [x] S2.7 concluida e validada: TCP cliente com retransmissao, sockets nativos
  limitados e HTTP GET, incluindo comandos individuais, suite QEMU e
  invariantes puras.
- [x] Suite S2.7 validada no QEMU padrao com DHCP, DNS, handshake, checksum,
  socket RX/TX, resposta HTTP, fechamento, polling e invariantes.
- [x] Entrada Shift e integridade da pilha/heap revalidadas: `echo A:B?`
  funcionou e `regcheck full` permaneceu em `OK` antes e depois da rede.
- [x] GET individual revalidado com `neverssl.com`: HTTP 302, headers e
  corpo recebidos, com `regcheck full` posterior em `OK`.
- [x] Suite agrupada revalidada: um timeout inicial foi recuperado pela
  tentativa 2/3, com HTTP 200, todos os itens e `regcheck full` em `OK`.
- [x] Fallback sem NIC validado com falha controlada da suite e
  `regcheck full` em `OK`.
- [ ] Cobertura complementar da S2.7: peer controlado, perda de segmentos,
  janela zero, RST, RTL8139 e modos Classic/Modern.
- [x] S2.8: suporte multi-NIC e RTL8139 validado sem duplicar a camada de
  protocolos; Q3, build e matriz QEMU aprovados pelo usuario.
- [x] Atualizacao segura foi dividida no roteiro U1-U5: politica, verificacao
  local, aplicacao recuperavel, interface dual e distribuicao remota opcional.
- [x] U1: politica de integridade e contrato ZUPD v1 concluidos, separados do
  ZPKG v1, com Ed25519, SHA-256, allowlist e quatro vetores publicos validados.
- [x] U2 concluida: empacotador/verificador host, raiz publica de release,
  servico criptografico, parser local somente-leitura, comando
  `update verify`, componente `Update` no `health` e sete fixtures estao
  implementados e validados. Build, matriz QEMU, memoria, imagem inalterada e
  `regcheck full` foram aprovados pelo usuario.
- [x] U3 concluida: aplicacao/rollback FAT12, persistencia redundante,
  recuperacao no boot, `APPLY.ZUP`, failpoint e `audit-image` foram
  validados. Aplicacao, rollback e interrupcao recuperavel passaram no QEMU,
  com memoria estavel, `regcheck full` em `OK` e journal final limpo.
- [x] U4 concluida: historico redundante, status, auditoria e System Updater
  Classic/Modern validados, incluindo aplicacao, rollback, failpoint,
  recuperacao no boot, `regcheck full` e auditoria final limpa.
- [x] U5 concluida: manifesto `ZUM1`, HTTP streaming, cache FAT12 A/B,
  comandos e aba Remoto foram validados no Shell e no Modern. Fixtures,
  alternancia de slot, falhas controladas, retry, cancelamento,
  aplicacao/rollback, `regcheck full` e auditoria passaram. O Classic permanece
  como fallback com cobertura complementar.
- [x] Manter operacoes remotas opcionalmente desabilitadas e visiveis em
  `health` quando indisponiveis.

## Etapa S3 - Ecossistema de aplicativos

- [ ] Executar o roadmap
  [`06-app-store.md`](06-app-store.md), comecando por AS1: catalogo local e
  observabilidade sobre `.zephyrosapp`/`ZPKG v1`.
- [ ] Evoluir Media Manager, Game Manager, ferramentas de desenvolvedor,
  PCSista e Anti-Virus somente sobre APIs ja estabelecidas.
- [ ] Tratar cada aplicativo opcional como modulo com diagnostico, fallback e
  documentacao propria.

## Criterio de saida

Nenhum servico opcional deve impedir boot, Shell, diagnostico ou uso local do
sistema quando seu hardware, arquivo, rede ou pacote nao estiver disponivel.
