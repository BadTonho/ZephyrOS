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
- [ ] S2.1 pendente de validacao manual no QEMU padrao e sem NIC.
- [ ] S2.2: implementar primeiro o driver E1000 `8086:100E`, preservando os
  contratos e IDs definidos na S2.1.
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
