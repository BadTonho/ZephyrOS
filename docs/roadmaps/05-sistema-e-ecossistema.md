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
- [x] S2.6 implementada, aguardando validacao: UDP, DHCP e DNS sobre a base
  IPv4 validada, com suite agrupada para o QEMU.
- [ ] S2.7: TCP, sockets e servicos remotos com limites e timeouts.
- [ ] S2.8: suporte multi-NIC e RTL8139 sem duplicar a camada de protocolos.
- [ ] Planejar atualizacao assinada ou verificada somente apos existir formato
  de pacote e politica de integridade.
- [ ] Manter operacoes remotas opcionalmente desabilitadas e visiveis em
  `health` quando indisponiveis.

## Etapa S3 - Ecossistema de aplicativos

- [ ] Usar `.zephyrosapp` como base para instalacao e catalogo de aplicativos.
- [ ] Evoluir Media Manager, Game Manager, ferramentas de desenvolvedor,
  PCSista e Anti-Virus somente sobre APIs ja estabelecidas.
- [ ] Tratar cada aplicativo opcional como modulo com diagnostico, fallback e
  documentacao propria.

## Criterio de saida

Nenhum servico opcional deve impedir boot, Shell, diagnostico ou uso local do
sistema quando seu hardware, arquivo, rede ou pacote nao estiver disponivel.
