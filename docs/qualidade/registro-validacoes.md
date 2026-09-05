# Registro de validações

Este documento é o histórico cronológico de implementações, testes e
conclusões de fase. Cada entrada registra a evidência reproduzível e o horário
real. Os roadmaps mantêm apenas o estado e o link para a entrada correspondente.

Não registrar chaves privadas, senhas, tokens, caminhos pessoais ou outros
segredos.

## 2026-09-04 - Runtime de atualizacao host-only

- Caso: `host:core:update-runtime` / `make test-update-runtime-host`.
- Fixture: filesystem, crypto e estado legado simulados em buffers estaticos,
  sem armazenamento real.
- Cobertura: registros de estado e journal, manifestos ZUM2, entradas ZUPD,
  planejamento, comparacao de arquivos, motivos e rejeicoes do runtime.
- Resultado: o executavel terminou `PASS`, sem enderecos desconhecidos ou
  simbolos ambiguos; `make catalog-test` tambem passou. O catalogo registra
  7.239 superficies, 5.394 `COVERED`, 1.845 `PENDING` e 142 casos.
- Limite conhecido: as rotinas transacionais que exigem filesystem mutavel,
  slots e reboot continuam pendentes para uma fixture integrada.

## 2026-09-04 - Dispatcher de syscalls host-only

- Caso: `host:core:syscall` / `make test-syscall-host`.
- Fixture: dependencias falsas de IDT, TSS, paging, processo, IPC, sinais e
  App API, com mapa estatico de enderecos de usuario.
- Cobertura: 34 funcoes de `src/core/syscall.c` resolvidas pelo relatorio
  instrumentado, sem `unknown_addresses` ou `ambiguous_symbols`.
- Resultado: host-only `PASS`; o fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

## PWR2 - Descoberta e validação ACPI

Implementação registrada em: 2026-08-30 (horário não informado).

Validação funcional confirmada pelo usuário em 2026-08-30, após execução no
QEMU de `acpi status`, `acpi tables`, `regcheck full` e `health check`. O
resumo PWR2 foi marcado `[x]` no roadmap.

- O snapshot ACPI agora registra RSDP v1/v2, a raiz RSDT/XSDT e as SDTs válidas
  na ordem da raiz, com comprimento, revisão, endereço e checksum confirmado.
- A tabela raiz passou a fazer parte do inventário e duplicatas por endereço
  são eliminadas. Endereços acima de 32 bits, limites E820 inválidos,
  comprimentos incorretos e checksums quebrados continuam sendo rejeitados ou
  contabilizados como ignorados.
- A MADT é copiada para `acpi_madt_info_t` e `acpi_madt_entry_t`, sem ponteiros
  persistentes para firmware, com limite de 64 entradas e contagem de CPUs
  habilitadas, APICs locais e I/O APICs.
- `acpi tables` foi adicionado ao comando ACPI existente. `regcheck full`
  valida os novos campos, a ordem, duplicatas, limites e contagens MADT.
- A saída confirmou RSDP, RSDT, FACP, APIC, HPET, WAET e DSDT com
  `checksum=OK`, zero tabelas inválidas/ignoradas e MADT com 1 processador,
  1 Local APIC e 1 I/O APIC. `RegCheck` terminou em `OK`.
- AML adicional, escrita em PM1, habilitação de APIC/SMP e transições de
  energia permanecem fora do PWR2 e reservados para etapas posteriores.
- Não houve alteração de App API, syscalls, layouts de aplicativo, `boot.asm`
  ou `stage2.asm`. O agente não executou build, testes ou QEMU.

## PWR1 - Idle arquitetural com HLT

Implementacao registrada em: 2026-08-30 (horario nao informado).

Validacao funcional principal confirmada pelo usuario em 2026-08-30, com
execucao no QEMU de `cpu usage reset`, `cpu usage`, `schedcheck`, `regcheck
full` e `health check`. O resumo PWR1 permanece `[ ]` no roadmap porque a
medicao pareada do uso do processo QEMU/host ainda nao foi registrada.

- O PID 0 passou a ser o unico Idle real do kernel unicore, fora do
  round-robin. O handoff inicial usa contexto de bootstrap separado para
  preservar a stack propria do Idle.
- O Idle usa `sti; hlt` seguido de `process_yield()`, e System/Desktop usam
  bloqueio temporizado para evitar polling ativo. O caminho degradado do
  `kernel_main` tambem usa a sequencia protegida.
- `scheduler_stats_t` recebeu `idle_ticks` e `active_ticks` em extensao
  append-only; `schedcheck` e `regcheck full` validam a correspondencia entre
  `idle_ticks` e `total_ticks` do PID 0.
- `cpu usage` mostra a janela acumulada e `cpu usage reset` captura linha-base
  privada sem alterar contadores. `kmetrics` publica os deltas dos novos
  campos.
- A janela apos o reset reportou `ticks_ativos=4`, `ticks_idle=145`,
  `ticks_total=149`, `percentual_ativo=2%` e `percentual_idle=97%`.
- `schedcheck` confirmou `contabilidade_idle=OK` e `resultado=OK`; `RegCheck`
  tambem terminou em `OK`. O `health check` manteve apenas estados opcionais
  ja conhecidos, sem falha nova do PWR1.
- Nao houve alteracao de App API, syscalls, layouts binarios, `boot.asm`,
  `stage2.asm`, paging, `thread_t` ou quantum de ring 3. O agente nao
  executou build, testes ou QEMU.

## NET2 - camada generica de sockets e AF_UNIX

Implementacao registrada em: 2026-08-29 21:32:35 (America/Sao_Paulo)

Validacao funcional: pendente da execucao dos gates de build e da matriz QEMU
pelo usuario. O resumo NET2 permanece `[ ]` no roadmap.

- `socket_t` e `socket_ops_t` foram adicionados com layout privado e FDs VFS
  reais do tipo `VFS_NODE_SOCKET`; `ERR_AGAIN` foi incluido no contrato
  canonico sem criar syscall ou alterar a ABI ring 3.
- `AF_UNIX/SOCK_STREAM` recebeu namespace global de caminhos, bind/listen,
  connect/accept, backlog limitado, filas bidirecionais de `sk_buff_t`, EOF,
  backpressure e fechamento do peer. Mensagens longas usam buffers
  independentes de ate 2048 bytes.
- `AF_INET/SOCK_STREAM` adapta somente o cliente TCP ativo legado. UDP,
  TCP passivo, poll/select, socketpair, datagramas UNIX, clones, fragmentos
  compartilhados, zero-copy real e ownership DMA permanecem fora da NET2.
- `sockstat`, `socket_self_test()`, `net check`, `regcheck full` e `health
  check` foram integrados para observar FDs, filas, erros, invariantes e
  limpeza. O self-test usa fixtures privadas e restaura as metricas do
  runtime de sockets.
- O caminho continua sincrono e baseado em copia; drivers, callbacks Ethernet,
  inventario de NICs e `boot.asm` nao foram alterados.

## NET1 - estrutura unificada sk_buff_t

Implementacao registrada em: 2026-08-29 20:21:31 (America/Sao_Paulo)

Validacao funcional confirmada em: 2026-08-29 (horario nao informado na
captura recebida)

- `src/include/core/sk_buff.h` e `src/core/sk_buff.c` foram adicionados com
  pool SLAB, storage interno de 2048 bytes, geometria `head/data/tail/end`,
  referencias, conclusao, descarte e validacao sobre um `net_buffer_t` privado.
- Ethernet foi migrada do descriptor `net_packet_t` privado para `sk_buff_t`
  sem alterar `ethernet.h`, callbacks dos drivers, `net_socket.h` ou ABI.
- `skb_self_test()` cobre limites de ponteiros, transicoes, retencao,
  conclusao unica, timeout, descarte e limpeza; `skbstat` foi adicionado ao
  dispatcher sem alterar trafego ou inventario de NICs.
- O caminho continua sincrono e baseado em copia fallback. Nenhum driver
  transfere ownership de DMA; clones, fragmentos reais e zero-copy permanecem
  fora do NET1; `boot.asm` nao foi alterado.
- `health check` inicial e final exibiram somente degradacoes ou recursos
  opcionais desabilitados esperados; nao houve erro residual do SKB.
- `net status` e `net check` mostraram `ativos=0`, pico 2, copias 4 e
  descartes normais; o diagnostico agrupado terminou com `OK`.
- `net socket check` terminou com todos os casos em `OK`.
- `regcheck full` terminou com `RegCheck: OK` e o job cooperativo concluiu.
- `skbstat` mostrou `ativos=0`, `alocados=5`, `liberados=5`, `concluidos=4`,
  `descartes=1`, `erros=0`, `ultimo_erro=0`, `copias=4`, `bytes=1836`,
  `clones=0` e `fragmentos=0`.
- `appcheck compact` terminou com `resultado=OK` e falhas 0.
- A matriz funcional do NET1 foi confirmada no QEMU; `boot.asm`, inventario de
  NICs, callbacks e trafego real permaneceram inalterados.

## NET0 - contrato de ownership e lifetime de buffers

Validacao funcional confirmada em: 2026-08-29 19:55:10 (America/Sao_Paulo)

- `net status` exibiu `ativos=0`, pico 2, copias 4 e descartes normais.
- `net socket check` terminou com todos os casos em `OK`.
- `net check` terminou com `invariantes: OK` e o job cooperativo concluiu.
- `regcheck full` terminou com `RegCheck: OK`.
- O `health check` final nao reportou residuos ou falhas de buffers NET0;
  as degradacoes restantes sao componentes opcionais esperados.
- A matriz funcional do NET0 foi confirmada no QEMU; `boot.asm`, inventario
  de NICs, sockets e trafego real permaneceram inalterados.

Implementacao registrada em: 2026-08-29 19:43:14 (America/Sao_Paulo)

- `net_buffer` foi adicionado com estados, owners, referencias, conclusao,
  validacao, rastreamento estatico e metricas.
- Ethernet e socket continuam com callbacks sincronos e copia; o descriptor
  privado acompanha RX/TX sem transferencia de ownership de DMA.
- `net check`, `regcheck full` e `health check` foram integrados sem criar
  comando novo. `boot.asm` nao foi alterado.
- A validacao funcional posterior, incluindo a matriz QEMU, esta registrada na
  entrada NET0 acima; o agente nao executou compilacao ou QEMU.

## EP7.0 — Inventário e diagnóstico seguro de candidatos Wi-Fi

Implementação concluída em: 2026-08-23 18:41:57 (America/Sao_Paulo)

Encerramento documental da EP7.0 concluído em: 2026-08-23 19:04:32
(America/Sao_Paulo). A EP7 geral permanece aberta para o driver de um chipset
real, que depende de um adaptador identificado literalmente.

## EP7.1 — Alvo USB Realtek RTL8811CU definido

Definição documental registrada em: 2026-08-23 19:18:36
(America/Sao_Paulo).

O usuário forneceu o modelo `Realtek 8811CU Wireless LAN 802.11ac USB NIC` e
os IDs literais `USB\VID_0BDA&PID_C811&REV_0200` e
`USB\VID_0BDA&PID_C811`. A etapa operacional permanece pendente; a subetapa
de enumeração e diagnóstico foi implementada, mas ainda não houve
encaminhamento ao QEMU, build ou validação executável do driver no ZephyrOS.

### EP7.1A — Enumeração e diagnóstico USB

Implementação concluída em: 2026-08-23 19:58:51 (America/Sao_Paulo)

`usb_device_info_t` passou a publicar `bcdDevice` e uma tabela limitada de
todos os endpoints descritos, mantendo os campos derivados de Bulk e Interrupt
usados por MSC e HID. O probe `rtl8811cu_probe()` aceita somente Vendor
`0x0BDA`, Product `0xC811` e revisão `0x0200`; o inventário USB é projetado
no `wifi_manager` com transporte, sessão, porta, endereço e endpoints.

Foi adicionado `src/drivers/rtl8811cu.c` como backend seguro de diagnóstico e
`RTL8811.BIN` ficou definido apenas como arquivo externo. Sem firmware válido
ou sem uma sequência de rádio verificável, a inicialização retorna erro
controlado e não toca no dispositivo. `wifi status`, `wifi scan` e
`wifi connect <ssid>` permanecem sem inicializar rádio, sem senha e sem
integração com `network_manager`.

O Makefile recebeu o alvo `run-usb-wifi` com passthrough literal
`vendorid=0x0BDA,productid=0xC811`. Os gates `make q3check`, `make clean &&
make` e a matriz QEMU/hardware real permanecem pendentes de execução pelo
usuário.

Validação executável apresentada pelo usuário; horário da captura não
informado.

- `usb devices` não publicou dispositivos USB configurados;
- `wifi status` e `wifi scan` permaneceram em `NOT_FOUND`, com zero candidatos
  e `ERR_NOT_FOUND`, sem inicializar hardware Wi-Fi;
- `wifi connect teste` retornou `ERR_UNAVAILABLE` sem aceitar ou registrar
  senha;
- `net check` manteve a E1000 em `net-pci-00:03.0`, com serviço `READY`, DHCP
  `BOUND` e sem erros de driver;
- `health check` executou sem panic, `memcheck` terminou com resultado `OK` e
  `regcheck full` terminou com `RegCheck: OK`.

Essa evidência valida a ausência segura do adaptador, mas não valida o
passthrough USB nem a enumeração literal `USB\VID_0BDA&PID_C811`.

Foi criado o `wifi_manager` com snapshot PCI somente-leitura para controladores
de classe `0x02` que não sejam E1000 ou RTL8139. O inventário preserva Vendor
ID, Device ID, classe, subclasse, ProgIF, revisão, BDF, IRQ e BAR0-BAR5, sem
habilitar BARs, Bus Mastering, MMIO, DMA, IRQ ou firmware.

Foram adicionados `wifi status`, `wifi scan` e `wifi connect`. A conexão
permanece indisponível nesta etapa e não processa, armazena ou registra
credenciais. `device-scan`, `health` e `regcheck full` validam o novo estado;
Ethernet, Simple, Classic, kernel de boot, boot e stage2 permanecem fora da
alteração.

Validação executável QEMU apresentada pelo usuário; horário da captura não
informado:

- `wifi status` mostrou `NOT_FOUND`, zero candidatos e `ERR_NOT_FOUND` sem
  inicializar hardware;
- `wifi scan` atualizou o inventário e confirmou que nenhuma varredura 802.11
  foi executada;
- `wifi connect` retornou `ERR_UNAVAILABLE` sem processar credenciais;
- `net check` concluiu com invariantes `OK`, `memcheck` concluiu com resultado
  `OK` e `regcheck full` concluiu com `RegCheck: OK`;
- `health check` executou sem panic; os estados degradados exibidos são de
  componentes opcionais já ausentes no QEMU, não do Wi-Fi.

As capturas não comprovam os horários ou a execução dos gates `make q3check` e
`make clean && make`; esses dados permanecem sem horário neste registro.

### EP7.1B — EHCI high-speed, transporte comum e integracao de rede

Implementacao concluida em: 2026-08-23 20:54:34 (America/Sao_Paulo)

Foi adicionado o controlador EHCI PCI para `ProgIF 0x20`, com DMA estatico,
IRQ compartilhada, portas raiz high-speed, descritores, controle, Bulk,
Interrupt, timeout, reset e recuperacao controlada. O transporte comum escolhe
UHCI ou EHCI pelo inventario sem alterar as APIs UHCI. HID e MSC legados
continuam restritos ao caminho UHCI.

O `network_manager` passou a inventariar o RTL8811CU como USB e reservar IDs
`net-usb-BB:DD.F-pN`; a interface Ethernet so e anexada quando o driver
publicar `READY`. O backend valida o cabecalho externo de `RTL8811.BIN`, mas
mantem o radio nao inicializado porque o checksum e a sequencia de firmware e
registradores ainda nao estao confirmados. Nenhum firmware binario ou segredo
foi adicionado.

Ajuste de revisao concluido em: 2026-08-23 21:08:20 (America/Sao_Paulo)

O calculo de pacotes do endpoint de controle EHCI passou a usar o tamanho
observado no descritor, e a documentacao foi alinhada para distinguir o
caminho EHCI novo do caminho UHCI legado. Nenhuma operacao de radio foi
adicionada.

Ajuste adicional concluido em: 2026-08-23 21:09:05 (America/Sao_Paulo)

Falhas de localizacao PCI durante o inicio agora registram o codigo no campo
do controlador correto, EHCI ou UHCI, preservando o diagnostico do transporte.

Ajuste de link concluido em: 2026-08-23 21:18:44 (America/Sao_Paulo)

O EHCI deixou de usar aritmetica e divisao de 64 bits para calcular deadlines,
eliminando a dependencia de `__udivdi3` incompatível com o kernel freestanding.

Validacao executavel ainda pendente do usuario: `make q3check`,
`make clean && make`, `make run-usb` e `make run-usb-wifi`, incluindo
`usb devices`, `wifi status`, `wifi scan`, `net check`, `health check`,
`memcheck` e `regcheck full`. Esta entrada registra implementacao, nao uma
aprovacao funcional em QEMU ou hardware real.

Validacao apresentada pelo usuario; horario nao informado.

- `usb devices` nao publicou dispositivo USB configurado; portanto o
  passthrough `0x0BDA:0xC811` ainda nao foi confirmado dentro da VM.
- `wifi status` permaneceu `NOT_FOUND` e `wifi scan` nao executou varredura
  802.11, comportamento esperado quando nenhum dispositivo chega ao inventario.
- `net check` preservou a E1000 PCI ativa (`net-pci-00:02.0`), sem regressao
  observada na Ethernet.
- `health check` mostrou ATA e Filesystem indisponiveis no perfil q35; isso
  impede considerar a validacao de base aprovada e pode estar relacionado ao
  uso dos argumentos legados `ide-hd` nesse alvo.
- `regcheck full` terminou com `ERR_STATE`; a causa detalhada do componente USB
  ainda exige `usb status`, `usb list` e `usb ports`.

Atualizacao da validacao apresentada pelo usuario; horario nao informado.

Nova validacao de passthrough apresentada pelo usuario; horario nao informado.

- O QEMU `11.0.50` confirmou suporte ao dispositivo `usb-host`.
- `info usbhost` encontrou o adaptador literal `0bda:c811` no host, em `Bus 2,
  Addr 12, Port 8, Speed 480 Mb/s`.
- O passthrough do host agora esta disponivel; falta reiniciar a VM com o
  adaptador conectado e confirmar a enumeracao dentro do ZephyrOS.

Nova validacao apresentada pelo usuario; horario nao informado.

- Mesmo com `0bda:c811` listado por `info usbhost` a `480 Mb/s`, o ZephyrOS
  continuou com as seis portas EHCI `EMPTY`/`NO_DEVICE`.
- O QEMU enumera o dispositivo no host, mas o passthrough nao o anexou ao
  barramento do convidado; a causa provavel e o driver Microsoft do adaptador
  nao fornecer acesso compativel com libusb/WinUSB.
- Nenhum driver, firmware, radio ou pilha de rede Wi-Fi foi inicializado.

Decisao de escopo sobre Bluetooth apresentada pelo usuario; horario nao
informado.

- A EP8 Bluetooth foi adiada pelo mesmo motivo da EP7.1: depende de hardware
  real, controlador identificado, transporte confirmado e validacao no alvo.
- Inventario HCI, firmware, scan, emparelhamento e perfis Bluetooth permanecem
  planejados, mas nao serao iniciados agora.

- `usb status` confirmou um controlador EHCI `READY`, com DMA, IRQ, controle,
  Bulk, Interrupt e high-speed disponiveis.
- `usb list` confirmou `usb-pci-00:03.0` como EHCI ativo.
- `usb ports` mostrou seis portas EHCI `EMPTY`/`NO_DEVICE`; o RTL8811CU ainda
  nao foi encaminhado para a VM. A enumeracao do dispositivo Wi-Fi permanece
  pendente e nenhuma etapa de firmware/radio foi executada.

Decisao de escopo apresentada pelo usuario; horario nao informado.

- A continuidade da EP7.1 foi pausada porque o Wi-Fi nao e prioridade imediata.
- O passthrough Windows/WinUSB, o firmware, o radio, TX/RX, scan, associacao e
  DHCP do RTL8811CU serao retomados somente na validacao em computador real.
- Nenhuma troca de driver hospedeiro sera feita agora; Ethernet, USB legado,
  EHCI, Shell e interfaces Simple/Classic permanecem preservados.

Decisao de arquitetura da EP9 apresentada pelo usuario; horario nao informado.

- O modelo de atualizacao por Releases GitHub foi incorporado ao planejamento:
  `release.json` continuara sendo o descritor assinado e os assets atuais
  `release.zum`/`update.zephyrosupd` serao preservados.
- A EP9 devera acrescentar `ZSYS` para a imagem completa e campos assinados de
  compatibilidade, incluindo versao minima do updater, ABI de boot, schema de
  dados, versoes de origem suportadas e rota de upgrade.
- A rota para sistemas antigos podera ser direta, por checkpoint ou por
  updater bridge; nao sera obrigatorio baixar todas as Releases intermediarias
  quando uma imagem cumulativa e migracoes ordenadas forem suficientes.
- GitHub sera transporte e hospedagem de assets; tag, titulo e ordem da
  Release nao serao raiz de confianca. A verificacao continuara baseada em
  assinatura, hash e compatibilidade antes de qualquer aplicacao.

## EP6.4 — Gerenciamento de stack para rede e TLS

Implementação concluída em: 2026-08-23 17:38:34 (America/Sao_Paulo)

Foram adicionados canários, high-water, menor folga, limites de 4 KiB a
16 KiB e diagnósticos por processo. O `Zephyr System`, que já executa
HTTP/TLS, passou a 16 KiB; o Shell permaneceu em 16 KiB. A margem de 1 KiB
fecha a sessão HTTP/TLS com `ERR_OVERFLOW`; corrupção real de canário registra
PID, nome e uso antes do `panic`.

Validação executável pendente do usuário: gates de código, QEMU e matriz
`stack`/TLS/runtime definida no Roadmap 08.

Validação no QEMU concluída em: 2026-08-23 17:45:10 (America/Sao_Paulo)

- `stack status` e `stack check` passaram antes e depois da operação remota:
  6 processos verificados, 6 válidos, zero margens baixas e zero canários
  corrompidos;
- `tls check` terminou em `Resultado: OK`;
- `update runtime check --tag ep63-runtime-b` e `fetch --confirm`
  descobriram a Release GitHub por tag exata, autenticaram o ZUM2 e
  publicaram o cache seletivo sem iniciar instalação;
- após o fetch, `Zephyr System` registrou stack de 16384 bytes, pico de
  4460 bytes e menor folga de 11924 bytes;
- `health check`, `memcheck` e `regcheck full` terminaram em `OK`. A tentativa
  `helath check` foi um erro de digitação e não substituiu o comando correto.

Os gates de código e os smoke tests específicos de App Store, Simple e Classic
ainda não foram apresentados nesta entrada.

## EP6.3 — GitHub runtime v2 via HTTPS

Concluída em: 2026-08-23 16:47:47 (America/Sao_Paulo)

Escopo: caminho real de Releases públicas GitHub. A Release
`ep63-runtime-b`, compatível com a base `0.1.0`, foi usada para validar
substituição, criação, remoção, aplicação e rollback do runtime v2.

No QEMU, a sequência executada foi:

```text
update remote enable
update runtime clear --confirm
update runtime check --tag ep63-runtime-b
update runtime fetch --tag ep63-runtime-b --confirm
update runtime verify --cached
update runtime status
update runtime apply --confirm
reboot
update runtime status
ls
update runtime rollback --confirm
reboot
update runtime status
ls
```

Resultado:

- `check` descobriu a Release GitHub por tag exata e autenticou o ZUM2;
- `fetch` publicou o cache; `verify --cached` retornou `assets faltantes=0`;
- após a aplicação: `0.1.2/e0`, `rollback=READY`, `EXPLORER.BMP` e
  `SHELL.BMP` presentes, `TASKMGR.BMP` ausente;
- após o rollback: `0.1.0/e0`, `rollback=DISABLED`, `EXPLORER.BMP` e
  `TASKMGR.BMP` presentes, `SHELL.BMP` ausente.

O aviso inicial de falha ao conectar no HTTP U5 local é o fallback anterior à
consulta GitHub e não invalida o cenário quando a descoberta e as operações
GitHub concluem com `NONE`.

Após encerrar o QEMU, a evidência persistida foi:

```text
python tools/updater.py audit-image --image build/zephyros.img --expect-version 0.1.0 --expect-rollback unavailable --expect-runtime-cache valid --expect-runtime-pending clean
```

Saída: `Audit image: OK`, `installed=0.1.0`, `rollback=DISABLED`,
`journal=CLEAN`, `runtime=READY`, `runtime_local=READY` e nenhuma operação
pendente.

## Histórico de implementações e correções — Roadmap 08

Migrado do Roadmap 08 em: 2026-08-23 17:02:47 (America/Sao_Paulo)

As datas e horários abaixo são os registros históricos já existentes no
roadmap; foram movidos sem criar ou estimar horários novos.

### EP1 a EP4.3 — Datas sem horário registrado

- EP1: implementação e validação registradas em 01/08/2026.
- EP2: implementação registrada em 01/08/2026 e validação em 02/08/2026.
- EP3: implementação registrada em 02/08/2026 e validação em 05/08/2026.
- EP4.1, EP4.2 e EP4.3: implementação e validação registradas em 20/08/2026.

Os horários dessas entradas não estavam documentados e não foram inferidos.

### EP4.4 — USB HID

- Implementação registrada em: 2026-08-21 12:10:35 (America/Sao_Paulo).
- Validação parcial registrada em: 2026-08-21 12:31:49 (America/Sao_Paulo).
- Correção do mapeamento USB HID/ABNT2 implementada em: 2026-08-21 15:00:11
  (America/Sao_Paulo).
- Correção das posições ABNT2 `;/:` e `/ ?` implementada em:
  2026-08-21 15:08:18 (America/Sao_Paulo).
- Validação final e confirmação das correções concluídas em:
  2026-08-21 16:13:14 (America/Sao_Paulo).

### EP5 — Releases oficiais e verificação no host

- Implementação concluída em: 2026-08-21 16:33:39 (America/Sao_Paulo).
- Validação concluída em: 2026-08-21 16:36:35 (America/Sao_Paulo).

### EP6 — Seleção por tag, TLS e canal GitHub

- Planejamento atualizado em: 2026-08-21 16:39:48 (America/Sao_Paulo).

#### EP6.0 — Contrato de seleção por tag

- Implementação concluída em: 2026-08-21 17:52 (America/Sao_Paulo).
- Correção do hash publicado da fixture concluída em: 2026-08-21 18:10
  (America/Sao_Paulo).
- Selftest de fixtures concluído em: 2026-08-21 18:12 (America/Sao_Paulo).
- Correção do apagamento no comando concluída em: 2026-08-21 18:19
  (America/Sao_Paulo).
- Correção das capacidades dos buffers estáticos do descritor concluída em:
  2026-08-21 18:46 (America/Sao_Paulo).
- Correção da capacidade do hash de asset concluída em: 2026-08-21 18:52
  (America/Sao_Paulo).
- Diagnóstico granular da validação de assets concluído em: 2026-08-21 18:58
  (America/Sao_Paulo).
- Instrumentação dos submotivos de asset concluída em: 2026-08-21 19:00
  (America/Sao_Paulo).
- Instrumentação das transições JSON de assets concluída em: 2026-08-21 19:05
  (America/Sao_Paulo).
- Correção do parser numérico com espaços JSON concluída em: 2026-08-21 19:08
  (America/Sao_Paulo).
- Correção da propagação de motivos antes dos assets concluída em:
  2026-08-21 19:20 (America/Sao_Paulo).
- Correção do diagnóstico de cabeçalho e das rotas de fixtures inválidas
  concluída em: 2026-08-21 19:27 (America/Sao_Paulo).
- Correção do estado `EMPTY` após falha de download sem pacote ativo concluída
  em: 2026-08-21 19:39 (America/Sao_Paulo).
- Validação funcional concluída em: 2026-08-21 20:07 (America/Sao_Paulo).

#### EP6.1 — TLS e identidade do canal

- Implementação concluída em: 2026-08-21 23:40 (America/Sao_Paulo).
- Correção do link freestanding concluída em: 2026-08-21 23:46
  (America/Sao_Paulo).
- Validação da sequência host concluída em: 2026-08-21 23:52
  (America/Sao_Paulo).
- Validação QEMU parcial concluída em: 2026-08-21 23:56 (America/Sao_Paulo).
- Validação QEMU concluída em: 2026-08-21 23:58 (America/Sao_Paulo).
- Regressão EP6.0/U5 reutilizada para o fechamento em: 2026-08-22 08:55
  (America/Sao_Paulo); execução original registrada em 2026-08-21 20:07.

#### EP6.2 — Canal GitHub configurável

- Transporte BearSSL/HTTP, configuração, parser GitHub, preflight, fixtures,
  contratos, documentação e implementação concluídos em: 2026-08-22 11:40
  (America/Sao_Paulo).
- Correção dos inicializadores do contrato remoto concluída em: 2026-08-22
  11:47 (America/Sao_Paulo).
- Auditoria de versionamento, `.gitignore` e arquivos alterados/novos
  concluída em: 2026-08-22 11:52 (America/Sao_Paulo).
- Correção da criação do subdiretório BearSSL concluída em: 2026-08-22 11:58
  (America/Sao_Paulo).
- Correção da inclusão do contrato de memória BearSSL concluída em:
  2026-08-22 12:02 (America/Sao_Paulo).
- Correção do wrapper freestanding `stddef.h`/`offsetof` concluída em:
  2026-08-22 12:04 (America/Sao_Paulo).
- Dependência explícita de `stddef.h` concluída em: 2026-08-22 12:05
  (America/Sao_Paulo).
- Correção do wrapper freestanding `stdint.h`/`uintptr_t` concluída em:
  2026-08-22 12:07 (America/Sao_Paulo).
- Correção da condição de digest e do fallback de `reason` concluída em:
  2026-08-22 12:10 (America/Sao_Paulo).
- Diagnóstico host da falha QEMU concluído em: 2026-08-22 12:21
  (America/Sao_Paulo); a leitura CHS do `stage2` permaneceu como causa em
  investigação, sem alteração em `boot.asm`.
- Correção da geometria IDE do QEMU e validação da imagem concluídas em:
  2026-08-22 12:25 (America/Sao_Paulo).
- Implementação e correção do filtro do `health check` concluídas em:
  2026-08-22 12:47 e 12:54 (America/Sao_Paulo).
- Correção da rejeição antecipada de HTTPS sem TLS concluída em:
  2026-08-22 13:21 (America/Sao_Paulo).
- Diagnóstico da tela preta, implementação de stack dedicada e reabertura da
  validação local registrados em: 2026-08-22 13:49, 14:04 e 14:48
  (America/Sao_Paulo).
- Encerramento por escopo concluído em: 2026-08-22 15:23
  (America/Sao_Paulo).

### Etapas relacionadas à EP6

- Registro da evolução de stacks para rede/TLS concluído em: 2026-08-22 13:54
  (America/Sao_Paulo).
- Planejamento da leitura LBA no `stage2` concluído em: 2026-08-22 12:30
  (America/Sao_Paulo).
- Implementação da leitura EDD/LBA, fallback CHS, retries e alvos QEMU do
  `stage2` concluída em: 2026-08-23 17:56 (America/Sao_Paulo). Build e
  validação QEMU permanecem pendentes do usuário.
- Validação estrutural concluída em: 2026-08-23 18:02 (America/Sao_Paulo);
  `boot.bin` permaneceu com 512 bytes, `stage2.bin` ficou com 1536 bytes e
  `src/boot/boot.asm` permaneceu sem diferenças.
- Cenário QEMU com geometria IDE explícita concluído em: 2026-08-23 18:03
  (America/Sao_Paulo); o sistema alcançou o Shell, `memcheck` terminou em `OK`
  e `regcheck full` terminou em `OK`.
- Cenário QEMU EDD/LBA sem geometria CHS fixa concluído em: 2026-08-23 18:05
  (America/Sao_Paulo); o sistema alcançou o Shell, `memcheck` terminou em `OK`
  e `regcheck full` terminou em `OK`.
- Primeira execução do fallback CHS concluída em: 2026-08-23 18:06
  (America/Sao_Paulo); o kernel alcançou o Shell e `memcheck` terminou em `OK`,
  mas `regcheck full` encontrou os serviços de bloco indisponíveis porque o
  alvo de teste não anexava um disco ATA.
- Correção do alvo `run-stage2-chs` concluída em: 2026-08-23 18:06
  (America/Sao_Paulo); o boot usa uma cópia floppy e a imagem original fica
  anexada como disco IDE. A repetição somente desse cenário permanece pendente.
- Correção da criação da cópia CHS concluída em: 2026-08-23 18:07
  (America/Sao_Paulo); o `copy` aninhado do CMD foi substituído por
  `Copy-Item` com origem e destino literais. Nenhum cenário anterior foi
  invalidado.
- Validação final do fallback CHS concluída em: 2026-08-23 18:08
  (America/Sao_Paulo); o sistema iniciou pelo floppy, manteve a imagem original
  como disco IDE, alcançou o Shell e concluiu `memcheck` e `regcheck full` em
  `OK`. A etapa LBA do `stage2` foi aprovada pelo usuário.

### EP6.3 — Runtime v2, cache seletivo e matriz de falhas

- Implementação concluída em: 2026-08-22 20:01 (America/Sao_Paulo).
- Correção de compilação, selftest host e inicialização independente concluídas
  em: 2026-08-22 20:25, 20:29 e 20:52 (America/Sao_Paulo).
- Correção da stack dedicada do Shell concluída em: 2026-08-22 22:10
  (America/Sao_Paulo).
- Rotação da chave pública de trust concluída em: 2026-08-22 22:26
  (America/Sao_Paulo).
- Aumento da stack dedicada do Shell para 16 KiB concluído em: 2026-08-22
  22:38 (America/Sao_Paulo).
- Correção da persistência do estado instalado concluída em: 2026-08-22 22:45
  (America/Sao_Paulo).
- Correção da validação no boot sem rollback concluída em: 2026-08-22 23:09
  (America/Sao_Paulo).
- Correção da auditoria offline sem rollback concluída em: 2026-08-22 23:27
  (America/Sao_Paulo).
- Correção do falso positivo da auditoria após limpeza de cache concluída em:
  2026-08-23 00:51 (America/Sao_Paulo).
- Implementação do failpoint e de `fixtures-runtime --changed-assets`
  concluídas em: 2026-08-23 10:34:45 e 11:16:00 (America/Sao_Paulo).
- Correção da sincronização U3 após rollback concluída em: 2026-08-23
  11:54:01 (America/Sao_Paulo).
- Horários iniciais da validação U5 registrados em: 2026-08-23 00:33
  (America/Sao_Paulo).
- Limpeza de cache e auditoria correspondente concluídas em: 2026-08-23 00:52
  (America/Sao_Paulo).
- EP6.3B concluída em: 2026-08-23 00:53 (America/Sao_Paulo).
- Failpoint, recuperação pós-reboot, aplicação e rollback validados em:
  2026-08-23 12:15:07 (America/Sao_Paulo).
- Correção da persistência após a Release A e validação A concluídas em:
  2026-08-23 12:38 e 12:51 (America/Sao_Paulo).
- Validação B e rollback B concluídos em: 2026-08-23 12:55 e 12:57
  (America/Sao_Paulo).
- Correções da auditoria para asset seletivo e poda de backups concluídas em:
  2026-08-23 13:21:30 e 13:24:28 (America/Sao_Paulo).
- Auditoria local A/B final aprovada em: 2026-08-23 14:46:23
  (America/Sao_Paulo).

#### EP6.3 — Evidências locais complementares

- Regeneração dos fixtures U2/U3/U5 e `python tools/updater.py selftest`
  aprovados em: 2026-08-23 12:22:53 (America/Sao_Paulo).
- A aplicação local da Release A chegou a `0.1.1/e0`, mas inicialmente perdeu
  `rollback=READY` após o reboot. A correção preservou o estado anterior dos
  arquivos fora do plano e manteve rollback vazio quando a operação concluída
  é um rollback. Implementação em: 2026-08-23 12:38 (America/Sao_Paulo).
- Validação A concluída em: 2026-08-23 12:51 (America/Sao_Paulo), com
  `0.1.1/e0`, `journal=CLEAN`, `rollback=READY`, `EXPLORER.BMP` e
  `TASKMGR.BMP` presentes e `SHELL.BMP` ausente.
- Validação B concluída em: 2026-08-23 12:55 (America/Sao_Paulo), com
  `0.1.2/e0`, `journal=CLEAN`, `rollback=READY`, `EXPLORER.BMP` e
  `SHELL.BMP` presentes e `TASKMGR.BMP` ausente.
- Rollback B concluído em: 2026-08-23 12:57 (America/Sao_Paulo), retornando a
  `0.1.1/e0`, `journal=CLEAN`, `rollback=DISABLED`, `EXPLORER.BMP` e
  `TASKMGR.BMP` presentes e `SHELL.BMP` ausente.
- A auditoria local interpretava incorretamente o bitmask seletivo `asset_mask=2`
  como booleano e acusava `ZRV0.01` como asset inativo. A correção foi
  implementada em: 2026-08-23 13:21:30 (America/Sao_Paulo).
- A poda de staging/backup foi corrigida para remover todos os slots que não
  correspondem ao rollback vigente em: 2026-08-23 13:24:28
  (America/Sao_Paulo).
- Auditoria local final concluída em: 2026-08-23 14:46:23
  (America/Sao_Paulo): `Audit image: OK`, `installed=0.1.1`,
  `rollback=DISABLED`, journal local `CLEAN`, cache runtime `READY` em
  `ZRV0.MAN`, nenhuma transferência pendente e estado local `READY`.

### EP9 — Atualização da imagem do sistema

- Planejamento registrado em: 2026-08-21 16:45:17 (America/Sao_Paulo).

### EP9.0A — Contrato ZSYS e preflight

- Implementação concluída em: 2026-08-24 00:22 (America/Sao_Paulo).
- Foram integrados o envelope ZSYS v1, a Release combinada v2, fixtures host,
  verificação local em streaming, preflight remoto somente leitura, comandos
  do Shell, campos remotos append-only e a documentação correspondente.
- A validação executável permanece pendente do usuário; o agente não executou
  `make update-test`, `make q3check`, build, QEMU ou testes funcionais.
- Correção do `NameError` no carregamento do callback de Release v2 concluída
  em: 2026-08-24 10:09 (America/Sao_Paulo); o resolver agora é associado em
  tempo de execução, após a definição de `resolve_git_commit`.
- Correção da sincronização do header remoto com a configuração do asset
  `system.zsys` concluída em: 2026-08-24 10:11 (America/Sao_Paulo).
- Correção do campo append-only `system_present` em
  `update_remote_github_release_t`, identificada na compilação, concluída em:
  2026-08-24 10:14 (America/Sao_Paulo).
- Regra de comandos completos adicionada ao `AGENTS.md` em: 2026-08-24 10:21
  (America/Sao_Paulo); comandos futuros devem ser enviados prontos para copiar
  e executar, sem placeholders ou argumentos ausentes.
- Validação host da EP9.0A confirmada pelo usuário em: 2026-08-24 10:30
  (America/Sao_Paulo); `make update-test` concluiu com `Updater selftest: OK`.
- Correção da matriz QEMU EP9.0A implementada em: 2026-08-24 10:42
  (America/Sao_Paulo); foi adicionado o alvo `system-fixtures`, com geração
  assinada compacta e aliases FAT12 8.3 `.ZSY`, mantendo a fixture host de
  imagem completa e a chave privada fora do repositorio.
- Etapa futura EP9.4 para expansão de armazenamento e uso de FAT32 registrada
  em: 2026-08-24 10:44 (America/Sao_Paulo); a etapa permanece sem
  implementação e exige aprovação explícita antes de qualquer alteração em
  boot/stage2.
- Diagnostico da validacao QEMU de fixtures registrado em: 2026-08-24 10:56
  (America/Sao_Paulo); `make system-fixtures` falhou por espaco insuficiente
  na imagem FAT12. A matriz completa de fixtures nao cabe no volume atual;
  isso confirma a necessidade futura de expansao FAT32, sem alterar o escopo
  de boot/stage2 da EP9.0A.
- Ajuste da estrategia QEMU da EP9.0A implementado em: 2026-08-24 11:02
  (America/Sao_Paulo); `system-fixtures` agora gera uma imagem FAT12 separada
  para cada fixture e `run-system-fixture` inicia a imagem selecionada. A
  imagem base nao recebe mais a matriz inteira e boot/stage2 permanecem sem
  alteracoes.
- Reposicionamento do ZephyrOS para uso real implementado em: 2026-08-24 11:11
  (America/Sao_Paulo); referencias explicitas a finalidade educacional foram
  removidas dos READMEs, da politica de seguranca, da introducao, das notas de
  componentes e da tela de Configuracoes. As limitacoes atuais continuam
  descritas como requisitos de maturidade, seguranca e suporte.

### EP9.4A — Volume de sistema FAT32

- Implementação concluída em: 2026-08-24 12:38 (America/Sao_Paulo); imagem
  híbrida de 64 MiB, partição FAT32 `ZEPHYROS` em LBA 4096, montagem automática,
  leitura/escrita, LFN UTF-16LE, aliases 8.3, operações atômicas, streaming,
  diagnóstico `storage check`, fixtures e migração do empacotador foram
  integrados sem alterar boot ou stage2.
- A validação executável permanece pendente do usuário; o agente não executou
  `make storage-fixtures-test`, `make storage-fixtures`, `make system-fixtures`,
  `make q3check`, build ou QEMU.
- Correções pós-primeira compilação implementadas em: 2026-08-24 12:50
  (America/Sao_Paulo); inspeção do FSInfo de backup foi corrigida, o valor
  `STORAGE_FAT32_FREE` foi publicado internamente e as declarações antecipadas
  das funções auxiliares FAT32 foram adicionadas.
- Correção da capacidade de escrita do provedor ATA implementada em:
  2026-08-24 12:57 (America/Sao_Paulo); a API direcionada de escrita foi
  acrescentada, o registro de bloco ATA passou a publicar callback de escrita
  e o volume FAT32 do sistema deixou de ser marcado somente-leitura.
- Mensagem do Shell para `storage list` corrigida em: 2026-08-24 13:00
  (America/Sao_Paulo); a saída agora distingue volume FAT32 do sistema montado
  de fallback FAT12 quando a detecção de disco falha.
- Contratos do Shell, filesystem e driver ATA atualizados em: 2026-08-24
  13:01 (America/Sao_Paulo); a documentação agora descreve a escrita ATA
  direcionada e o estado gravável do volume FAT32 do sistema.
- Correção da enumeração ATA na camada de bloco implementada em: 2026-08-24
  13:06 (America/Sao_Paulo); o registro agora percorre os slots físicos ATA
  em vez de confundir a contagem de dispositivos com o índice do slot.
- Validação parcial confirmada pelo usuário, com disco `ata2`, FAT12 legado e
  volume FAT32 `ata2p1` `ZEPHYROS` montado em modo `READ-WRITE`; horário exato
  da execução não foi informado.
- Correção das entradas LFN geradas pelo empacotador implementada em:
  2026-08-24 13:16 (America/Sao_Paulo); o segmento inicial agora recebe o
  marcador FAT32 de último segmento e o selftest verifica a sequência.
- Validação do comando `storage check ata2p1` confirmada pelo usuário com
  `Volume FAT32 consistente.`; horário exato da execução não foi informado.
- Diagnóstico de `update system verify system:/VALID.ZSYS` registrado sem
  horário exato informado: o Shell retornou `ERR_NOT_FOUND` antes da
  verificação criptográfica; a imagem de fixture `VALID.img` foi conferida
  no host e contém o arquivo, portanto a inicialização deve usar
  `run-system-fixture` com essa imagem específica.
- Correção da extensão de diretórios FAT32 concluída em: 2026-08-24 13:39
  (America/Sao_Paulo); o empacotador agora consome os slots livres finais
  antes de alocar o próximo cluster, preservando a continuidade observável
  pelo leitor do kernel, e o selftest cobre a passagem do limite do cluster.
- Validação QEMU da fixture `VALID.img` confirmada pelo usuário, sem horário
  exato informado: `storage list` mostrou o FAT12 legado e o FAT32 `ZEPHYROS`
  montados em `READ-WRITE`, `storage check ata2p1` retornou `Volume FAT32
  consistente.` e `update system verify system:/VALID.ZSYS` retornou `ZSYS
  autenticado e compatível`, sem gravação.
- Validação QEMU da fixture `TRUNC.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/TRUNC.ZSYS` recusou a divergência de regiões
  com motivo `SIZE`, sem gravação.
- Validação QEMU da fixture `HDRBAD.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/HDRBAD.ZSYS` recusou a assinatura Ed25519
  adulterada com motivo `SIGNATURE`, sem gravação.
- Validação QEMU da fixture `PAYBAD.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/PAYBAD.ZSYS` recusou o payload adulterado por
  invalidar a assinatura Ed25519, com motivo `SIGNATURE`, sem gravação.
- Validação QEMU da fixture `SIGBAD.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/SIGBAD.ZSYS` recusou a assinatura Ed25519
  adulterada com motivo `SIGNATURE`, sem gravação.
- Validação QEMU da fixture `OVERSIZ.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/OVERSIZ.ZSYS` recusou a divergência de regiões
  com motivo `SIZE`, sem gravação.
- Tentativa de validação QEMU da fixture `MISALGN.img` registrada sem horário
  exato informado: `storage check ata2p1` permaneceu consistente, mas a busca
  do arquivo retornou `ERR_NOT_FOUND`; a matriz considera o nome exato
  `MISALGN.ZSYS`, portanto a validação de alinhamento permanece pendente.
- Validação QEMU da fixture `MISALGN.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` retornou `Volume FAT32 consistente.`
  e `update system verify system:/MISALGN.ZSYS` recusou a divergência de
  regiões com motivo `SIZE`, sem gravação.
- Validação QEMU da fixture `VERBAD.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/VERBAD.ZSYS` recusou a versão incompatível com
  motivo `COMPATIBILITY`, sem gravação.
- Tentativa de validação QEMU da fixture `EPCHBAD.img` registrada sem horário
  exato informado: `storage check ata2p1` permaneceu consistente, mas a busca
  do arquivo retornou `ERR_NOT_FOUND`; a validação de epoch permanece pendente
  com o nome exato `EPCHBAD.ZSYS`.
- Validação QEMU da fixture `EPCHBAD.img` confirmada pelo usuário, sem horário
  exato informado: `update system verify system:/EPCHBAD.ZSYS` recusou a base
  de versão/epoch incompatível com motivo `BASE_VERSION`, sem gravação.
- Tentativa de validação QEMU da fixture `ABIBAD.img` registrada sem horário
  exato informado: `storage check ata2p1` permaneceu consistente, mas a busca
  retornou `ERR_NOT_FOUND`; a validação de ABI permanece pendente com o nome
  exato `ABIBAD.ZSYS`.
- Validação QEMU da fixture `ABIBAD.img` confirmada pelo usuário, sem horário
  exato informado: `update system verify system:/ABIBAD.ZSYS` recusou a
  compatibilidade de `boot_abi` com motivo `COMPATIBILITY`, sem gravação.
- Validação QEMU da fixture `SCHBAD.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/SCHBAD.ZSYS` recusou o schema incompatível com
  motivo `COMPATIBILITY`, sem gravação.
- Validação QEMU da fixture `IMGHASH.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/IMGHASH.ZSYS` recusou a divergência do hash da
  imagem com motivo `HASH`, sem gravação.
- Validação QEMU da fixture `CMPHASH.img` confirmada pelo usuário, sem horário
  exato informado: `storage check ata2p1` permaneceu consistente e
  `update system verify system:/CMPHASH.ZSYS` recusou a divergência do hash de
  componente com motivo `HASH`, sem gravação.
- Correção da reconstrução do índice de arquivos implementada em: 2026-08-24
  14:55 (America/Sao_Paulo); a fonte FAT12 de boot agora abre explicitamente
  `legacy:/`, preservando o contrato de caminho do volume de sistema e
  eliminando a tentativa indevida de resolver a raiz como FAT32.
- Validação QEMU da fixture `VALID.img` confirmada pelo usuário, sem horário
  exato informado: `storage list` mostrou `ata2raw` FAT12 e `ata2p1` FAT32
  `ZEPHYROS` montados e graváveis; `storage check ata2p1` retornou volume
  consistente; `update system verify system:/VALID.ZSYS` autenticou e não
  gravou; `regcheck full` retornou `RegCheck: OK`, o índice foi publicado com
  sucesso e `memcheck` retornou todos os itens `OK`.
- Os avisos repetidos de `falha ao listar nomes longos` ocorreram durante os
  diagnósticos negativos do App Store ao consultar o diretório opcional
  `APPS`, ausente na fixture sem pacotes instalados; não houve erro de cursor
  FAT32 nem falha de publicação do índice nesta validação.
- EP9.4A encerrada em: 2026-08-24 15:09 (America/Sao_Paulo); implementação e
  matriz executável do usuário foram concluídas, mantendo journaling,
  filesystem nativo, boot direto pelo FAT32, slots A/B, staging, aplicação
  ZSYS e reboot automático para etapas posteriores.
- EP9.1 implementada em: 2026-08-24 16:43 (America/Sao_Paulo); serviço
  `update_system_slots`, estado/journal redundantes, escritor FAT32 em chunks,
  staging local com preflight/confirm/cancelamento cooperativo, Shell,
  fixtures de slots A/B e documentação foram adicionados. A validação
  executável e a matriz EP9.1 no QEMU permanecem pendentes do usuário.
- Gates EP9.1 confirmados pelo usuário em: 2026-08-24 16:52
  (America/Sao_Paulo); `make q3check`, `make clean && make` e `make run`
  concluídos com sucesso. A matriz específica de fixtures e staging no QEMU
  permanece pendente.
- Correção de preflight EP9.1 implementada em: 2026-08-24 17:01
  (America/Sao_Paulo); a consulta de espaço livre do volume FAT32 de sistema
  deixou de retornar zero artificialmente quando montada pelo Storage. Foi
  adicionada a API `storage_get_free_space`, integrada ao `fs_get_info`, e a
  checagem estática `git diff --check` foi concluída sem erros.
- Gates após a correção confirmados pelo usuário em: 2026-08-24 17:07
  (America/Sao_Paulo); `make q3check`, `make clean && make` e `make run`
  concluídos com sucesso.
- Fixture EP9.1 gerada e `VALID.ZSYS` injetada em: 2026-08-24 17:09
  (America/Sao_Paulo); o alvo `make system-slots-fixtures` foi concluído com
  sucesso.
- Estado inicial dos slots validado pelo usuário em: 2026-08-24 17:12
  (America/Sao_Paulo); `update system slots` reportou `READY`, slot A ativo
  válido, slot B vazio, sequência 1, journal limpo e espaço livre de
  63930880 bytes.
- Preflight EP9.1 validado pelo usuário em: 2026-08-24 17:13
  (America/Sao_Paulo); `update system stage system:/VALID.ZSYS` aceitou o
  envelope com motivo `NONE` e confirmou que nenhuma gravação foi realizada.
- Staging EP9.1 validado pelo usuário em: 2026-08-24 17:16
  (America/Sao_Paulo); `update system stage system:/VALID.ZSYS --confirm`
  verificou o envelope, gravou o slot inativo e publicou-o como pendente.
  As mensagens de exclusão FAT32 referiam-se a entradas ausentes toleradas
  durante a limpeza preparatória; a operação terminou com motivo `NONE`.
- Estado pós-staging validado pelo usuário em: 2026-08-24 17:18
  (America/Sao_Paulo); `update system slots` reportou sequência 2, slot A
  ativo e preservado, slot B `VALID` pendente, journal limpo e recuperação
  limpa.
- Bloqueio de staging pendente validado pelo usuário em: 2026-08-24 17:19
  (America/Sao_Paulo); nova execução confirmada foi recusada com motivo
  `STATE` porque já havia slot ZSYS pendente, sem iniciar gravação.
- Estado após tentativa bloqueada validado pelo usuário em: 2026-08-24 17:21
  (America/Sao_Paulo); nova consulta confirmou sequência 2, A ativo, B
  pendente, journal limpo e recuperação limpa, sem alteração indevida.
- `health` EP9.1 validado pelo usuário em: 2026-08-24 17:22
  (America/Sao_Paulo); `System Updater`, `Storage`, Filesystem, Shell e kernel
  permaneceram `READY`. Degradações pré-existentes de Media Player, App Store,
  USB e AC97 não estão relacionadas ao EP9.1.
- `memcheck` EP9.1 validado pelo usuário em: 2026-08-24 17:22
  (America/Sao_Paulo); heap-integridade, coalescência, PMM, diretórios de
  usuário e resultado geral reportaram `OK`.
- `regcheck full` EP9.1 validado pelo usuário em: 2026-08-24 17:23
  (America/Sao_Paulo); a execução terminou com `RegCheck: OK`. Avisos iniciais
  de listagem de nomes longos, espaço e mutação concorrente do índice foram
  controlados pela própria rotina e não alteraram o resultado final.
- Fixture negativa de assinatura validada pelo usuário em: 2026-08-25 12:30
  (America/Sao_Paulo); `SIGBAD.ZSYS` foi recusado por `SIGNATURE` e confirmou
  que nenhuma gravação foi realizada.
- Fixture negativa de hash validada pelo usuário em: 2026-08-25 12:35
  (America/Sao_Paulo); `IMGHASH.ZSYS` foi recusado por `HASH` e confirmou que
  nenhuma gravação foi realizada.
- Fixture negativa de tamanho validada pelo usuário em: 2026-08-25 12:37
  (America/Sao_Paulo); `OVERSIZ.ZSYS` foi recusado por `SIZE` e confirmou que
  nenhuma gravação foi realizada.
- Fixture negativa de alinhamento/regiões validada pelo usuário em:
  2026-08-25 12:38 (America/Sao_Paulo); `MISALGN.ZSYS` foi recusado por
  `SIZE` e confirmou que nenhuma gravação foi realizada.
- Fixture negativa de compatibilidade de versão validada pelo usuário em:
  2026-08-25 12:39 (America/Sao_Paulo); `VERBAD.ZSYS` foi recusado por
  `COMPATIBILITY` e confirmou que nenhuma gravação foi realizada.
- Fixture negativa de epoch/base validada pelo usuário em: 2026-08-25 12:41
  (America/Sao_Paulo); `EPCHBAD.ZSYS` foi recusado por `BASE_VERSION` e
  confirmou que nenhuma gravação foi realizada.
- Gerador da matriz EP9.1 implementado em: 2026-08-25 12:51
  (America/Sao_Paulo); foi adicionado `tools/system_slots_matrix.py` com
  casos reproduzíveis de estado/journal corrompido, fases de recuperação,
  falta de espaço e volume FAT32 ausente, além dos alvos
  `system-slots-matrix` e `run-system-slots-matrix`. A validação QEMU desses
  casos permanece pendente do usuário.
- Matriz EP9.1 ampliada em: 2026-08-25 12:54
  (America/Sao_Paulo); foram acrescentados casos que comprovam a escolha da
  maior sequência válida para estado e journal (`STATE_NEWER` e
  `JOURNAL_NEWER`). A validação QEMU permanece pendente do usuário.
- Caso `STATE_ONE_BAD` validado pelo usuário em: 2026-08-25 13:02
  (America/Sao_Paulo); uma cópia de estado corrompida foi tolerada, o serviço
  permaneceu `READY`, o slot A continuou ativo e o slot B permaneceu vazio.
  A mensagem de fila de eventos do mouse cheia foi observada como ocorrência
  independente da matriz EP9.1.
- Caso `STATE_BOTH_BAD` validado pelo usuário em: 2026-08-25 13:05
  (America/Sao_Paulo); as duas cópias de estado inválidas deixaram o serviço
  `DEGRADED`, sem slot ativo selecionado e sem reparo silencioso.
- Caso `STATE_NEWER` validado pelo usuário em: 2026-08-25 13:07
  (America/Sao_Paulo); a cópia válida de maior sequência foi selecionada
  (`seq=2`), mantendo o slot A ativo e o slot B vazio.
- Correção do gerador da matriz EP9.1 implementada em: 2026-08-25 13:13
  (America/Sao_Paulo); os casos de journal/staging passaram a separar o
  registro de metadados de 176 bytes do envelope ZSYS completo usado em
  `ZSTG.ZSY` e `ZSB0.ZSY`. O caso `JOURNAL_PREPARED` observado antes desta
  correção foi invalidado e deverá ser repetido após a regeneração.
- Diagnóstico e correção da exclusão FAT32 em recuperação implementados em:
  2026-08-25 13:22 (America/Sao_Paulo); entradas LFN e entradas curtas podem
  atravessar clusters físicos diferentes, portanto a exclusão passou a apagar
  cada offset de entrada individualmente. A rotina anterior apagava o intervalo
  físico inteiro e podia sobrescrever os dados de `ZSA0.ZSY` com `0xE5`.
  `git diff --check` permaneceu sem erros; gates e QEMU precisam ser repetidos
  pelo usuário antes de continuar a matriz.
- Gates após a correção confirmados pelo usuário em: 2026-08-25 14:27
  (America/Sao_Paulo); `make q3check`, `make clean && make` e `make run`
  foram concluídos com sucesso.
- Caso `JOURNAL_PREPARED` validado pelo usuário em: 2026-08-25 14:27
  (America/Sao_Paulo); a recuperação descartou o staging incompleto, limpou o
  journal, preservou o slot A como `VALID`/ativo e deixou o slot B vazio.
- Caso `JOURNAL_STAGING` validado pelo usuário em: 2026-08-25 14:33
  (America/Sao_Paulo); a recuperação em `STAGING` preservou o slot A, removeu
  o staging incompleto, limpou o journal e manteve o serviço `READY` com B
  vazio.
- Caso `JOURNAL_VERIFIED` validado pelo usuário em: 2026-08-25 14:36
  (America/Sao_Paulo); a recuperação publicou o slot B como `VALID` e
  pendente, preservou A como ativo e válido, avançou para `seq=2` e limpou o
  journal.
- Caso `JOURNAL_ONE_BAD` validado pelo usuário em: 2026-08-25 14:44
  (America/Sao_Paulo); uma cópia inválida do journal foi ignorada, a cópia
  válida foi usada, A permaneceu ativo e válido, B permaneceu vazio e o
  journal foi limpo.
- Caso `JOURNAL_BOTH_BAD` validado pelo usuário em: 2026-08-25 14:47
  (America/Sao_Paulo); as duas cópias inválidas deixaram o serviço
  `DEGRADED` com `recovery=pending`, sem reparo silencioso; A permaneceu
  válido e ativo, B permaneceu vazio e o journal ficou limpo.
- Caso `JOURNAL_COMMITTED` validado pelo usuário em: 2026-08-25 14:38
  (America/Sao_Paulo); a recuperação manteve A ativo e válido, publicou B como
  `VALID`/pendente, avançou para `seq=2` e limpou o journal.
- Caso `JOURNAL_NEWER` validado pelo usuário em: 2026-08-25 14:40
  (America/Sao_Paulo); entre journals válidos com sequências 2 e 3, a
  recuperação terminou `READY`, preservou A, descartou o staging conforme a
  fase de maior sequência e limpou o journal.
- Caso `NO_SPACE` validado pelo usuário em: 2026-08-25 14:52
  (America/Sao_Paulo); o candidato foi validado, o staging foi recusado com
  `SPACE` por falta de espaço e nenhuma gravação foi realizada.
  `SPACE` por falta de espaço e nenhuma gravação foi realizada.
- Caso `NO_VOLUME` validado pelo usuário em: 2026-08-25 14:54
  (America/Sao_Paulo); sem volume FAT32 disponível, o serviço ficou
  `DEGRADED`, sem slot ativo, sem slots válidos e sem reparo silencioso.
- Matriz específica de fixtures e staging do EP9.1 concluída pelo usuário em:
  2026-08-25 14:55 (America/Sao_Paulo); os 12 casos previstos foram
  validados, incluindo seleção redundante de estado/journal, recuperação por
  fase, cópias inválidas, falta de espaço e ausência do volume FAT32.
- EP9.1 marcada como validada nos roadmaps em: 2026-08-25 14:56
  (America/Sao_Paulo); o status foi atualizado após a conclusão da matriz
  QEMU e das verificações `health`, `memcheck` e `regcheck full`.
- Subetapas da EP9.2 registradas em: 2026-08-25 15:18 (America/Sao_Paulo);
  o roadmap foi dividido em EP9.2A (recovery loader confiavel) e EP9.2B
  (menu pre-kernel e recuperacao interativa), mantendo ambas pendentes.
- Base da EP9.2 implementada em: 2026-08-25 15:15 (America/Sao_Paulo); o
  estado de slots passou a v2 mantendo leitura v1, o kernel ganhou confirmação
  de tentativa vinculada ao handoff `ZSBH` e o `stage2` legado limpa o handoff
  antes de carregar o kernel. Loader FAT32/Ed25519, menu pré-kernel e a
  validação executável permanecem pendentes.
- EP9.2A implementada em: 2026-08-25 15:36 (America/Sao_Paulo); foi adicionado
  recovery loader fixo, shim no stage2, verificacao FAT32/ZSYS em streaming,
  tentativa persistida com rollback e fallback SHA-256 do kernel legado.
  `boot.asm` permaneceu inalterado. Gates e matriz QEMU da EP9.2A aguardam a
  validacao do usuario.
- Gateway de disco da EP9.2A corrigido em: 2026-08-25 16:52
  (America/Sao_Paulo); o diagnostico QEMU `LEGACY ATA READ FAIL` confirmou que
  o acesso PIO direto do loader nao alcançava a imagem. O backend foi trocado
  por um trampoline BIOS EDD de leitura/escrita no `stage2`, mantendo
  `boot.asm` inalterado. A correcao aguarda novos gates e validacao QEMU.
- Travamento aparente na validacao do slot diagnosticado e corrigido em:
  2026-08-25 17:02 (America/Sao_Paulo); o leitor reiniciava a caminhada FAT a
  cada bloco de 512 bytes, tornando as passagens sobre o ZSYS quadraticas. Foi
  adicionado cursor persistente de cluster para streaming linear e o retorno
  BIOS agora passa por um stub protegido fixo do `stage2`. A tela publica os
  marcos `VERIFY SLOT` e `LOAD KERNEL`; a correcao aguarda novos gates e QEMU.
- Handoff final do kernel da EP9.2A corrigido em: 2026-08-25 17:10
  (America/Sao_Paulo); apos `LOAD KERNEL`, a entrada Assembly passou a
  restaurar `ESP=0x9F000` e a publicar ESI/EDI conforme a ABI original do
  `stage2`, antes de chamar o kernel em `0x00100000`. O marco `KERNEL READY`
  diferencia copia concluida de falha na entrada; a correcao aguarda QEMU.
- Entrada C do kernel alcançada no QEMU em: 2026-08-25 17:22
  (America/Sao_Paulo); os breadcrumbs `ECBM` confirmaram entrada, argumentos,
  limpeza de BSS e chamada de `kernel_main`. Foram adicionados os marcos
  temporarios `1` a `5` ao redor de VESA, fonte, video, log e recovery para
  localizar a primeira inicializacao que falha antes da saida normal.
- Salto para `kernel_main` isolado em: 2026-08-25 17:30
  (America/Sao_Paulo); a ausencia do marco `1` apos `ECBM` mostrou falha na
  transferencia para a funcao C, embora a entrada Assembly estivesse integra.
  O loader passou a comparar o SHA-256 do kernel ja copiado em `0x00100000`
  com o hash autenticado do componente antes de executar a imagem.
- Causa da entrada incompleta identificada em: 2026-08-25 17:38
  (America/Sao_Paulo); `fixtures-system-qemu` incluia somente os primeiros
  512 bytes do kernel, gerando `valid.zsys` de 6.656 bytes. `_start` existia,
  mas `kernel_main` ficava fora da imagem. As fixtures FAT32 da matriz A/B
  agora usam `--full-kernel`, preservando o modo compacto apenas como opcao do
  gerador, e o loader aceita o padding final autenticado do payload alinhado.
- Saida VGA apos o boot autenticado corrigida em: 2026-08-25 17:45
  (America/Sao_Paulo); o kernel alcançou sua inicializacao, mas o fallback
  declarava 100x37 sobre o modo texto BIOS 80x25, deslocando linhas e gravando
  a continuacao fora da area visivel. A geometria Simple foi corrigida para
  80x25 e os breadcrumbs temporarios de entrada foram removidos.
- Politica de comentarios revisada em: 2026-08-25 17:47
  (America/Sao_Paulo); justificativas, invariantes, ABI e layouts passam a ser
  registrados nos documentos tecnicos canonicos, sem novos comentarios
  explicativos no codigo. A migracao dos comentarios existentes sera gradual
  quando cada trecho for alterado.
- Transicao grafica pos-autenticacao corrigida em: 2026-08-25 17:51
  (America/Sao_Paulo); o recovery loader passou a solicitar VESA pelo gateway
  BIOS somente depois de validar e copiar o kernel. Os diagnosticos anteriores
  permanecem em texto, a ABI legada de entrada foi preservada e falha de VESA
  continua encaminhada ao fallback Simple. A validacao QEMU aguarda o usuario.
- ABI final do kernel corrigida em: 2026-08-25 17:59
  (America/Sao_Paulo); a comparacao com o fluxo original mostrou que a ponte
  empilhava E820 e VESA antes de a propria entrada do kernel fazer essa
  conversao. A ponte voltou a entregar somente `ESI`, `EDI` e a pilha limpa.
  O leitor privado do bloco VESA tambem passou a respeitar a ordem
  `pitch/width/height`, e o loader publica o diagnostico final `START KERNEL`.
  A validacao QEMU aguarda o usuario.
- Handoff VESA do recovery loader revisado em: 2026-08-25 18:12
  (America/Sao_Paulo); o QEMU confirmou entrada no modo 1024x768, mas a troca
  tardia deixou a tela preta. A configuracao voltou ao caminho real-mode
  comprovado do `stage2`, antes do loader protegido, eliminando a segunda
  transicao. O renderer fixo agora publica diagnosticos em framebuffers de 24
  ou 32 bpp e conserva VGA texto como fallback. A validacao aguarda o usuario.
- Causa raiz do handoff pre-kernel corrigida em: 2026-08-25 18:18
  (America/Sao_Paulo); a entrada Assembly recebia E820 e VESA do `stage2`, mas
  chamava `recovery_loader_main` sem repassar os argumentos depois de limpar a
  BSS. O C recebia o endereco de retorno como mapa e o mapa como bloco VESA.
  Os dois enderecos agora sao preservados e empilhados explicitamente antes da
  chamada C. A validacao QEMU aguarda o usuario.
- Excecao do kernel tornada diagnosticavel em: 2026-08-25 18:24
  (America/Sao_Paulo); o QEMU passou do handoff e alcancou o panic de excecao
  fatal. Os vetores registrados agora exibem nome, numero, codigo, EIP e CR2
  para page fault, usando o mesmo renderer dos vetores sem handler. A fonte
  minima do loader tambem passou a interpretar corretamente seus glifos por
  colunas. A identificacao do endereco faltoso aguarda nova captura do usuario.
- Page fault do handoff identificado e corrigido em: 2026-08-25 18:31
  (America/Sao_Paulo); o QEMU registrou vetor 14, codigo zero, leitura de
  `0x00002800` e EIP `0x0015D010`, dentro de `kmemcpy`. A pagina que contem o
  `ZSBH` nao sobrevivia a ativacao do paging. O bootstrap passou a mapear por
  identidade somente `0x2000–0x2FFF` como supervisor, mantendo ring 3, pagina
  zero e as demais lacunas baixas sem acesso. A validacao aguarda o usuario.
- Gate documental corrigido em: 2026-08-25 18:33
  (America/Sao_Paulo); `make q3check` recusou a alteracao de `memory.h` porque
  faltava atualizar `docs/04-kernel/kernel.md`. O contrato do kernel agora
  registra a pagina supervisora do contexto de boot e a ABI E820/VESA. O gate
  precisa ser repetido pelo usuario.
- Boot autenticado do slot A validado em: 2026-08-25 18:37
  (America/Sao_Paulo); o usuario confirmou que `BOOT_ACTIVE_VALID.img` abriu o
  ZephyrOS depois dos gates da mesma revisao. O handoff E820/VESA, paging do
  contexto supervisor e entrada do kernel funcionaram sem novo panic. Os
  demais casos da matriz EP9.2A permanecem pendentes.
- Tentativa e confirmacao do slot B validadas em: 2026-08-25 18:40
  (America/Sao_Paulo); `BOOT_PENDING_VALID.img` abriu o ZephyrOS e o comando
  `update system slots` registrou `READY`, sequencia 4, ativo B, pendente
  `NONE`, anterior A, tentativa `NONE`, boot `NONE`, motivo `NONE`, sequencia
  de tentativa zero e journal limpo. Os slots A e B permaneceram `VALID`,
  confirmando a persistencia pre-kernel da tentativa e sua promocao pelo
  acknowledge do kernel. Durante a inspecao ocorreu descarte por fila de
  eventos do mouse cheia; a observacao e externa ao fluxo EP9.2A e nao
  invalidou este caso da matriz.
- Assinatura invalida no slot pendente validada.
  Concluida em: 2026-08-25 22:52 (America/Sao_Paulo).
  `BOOT_BAD_SIGNATURE.img` iniciou somente o kernel legado autenticado. O
  comando `update system slots` registrou `DEGRADED`, sequencia 3, ativo A,
  pendente `NONE`, anterior A, tentativa B, boot `FAILED`, motivo
  `BOOT_FAILED`, sequencia de tentativa 1, journal e recovery limpos, slot A
  `VALID` e slot B preservado como `INVALID`. O descarte observado na fila de
  eventos do mouse permanece externo ao fluxo EP9.2A e nao invalidou o caso.
- Hash de imagem invalido no slot pendente validado.
  Concluida em: 2026-08-25 22:56 (America/Sao_Paulo).
  `BOOT_BAD_IMAGE_HASH.img` iniciou somente o kernel legado autenticado. O
  comando `update system slots` registrou `DEGRADED`, sequencia 3, ativo A,
  pendente `NONE`, anterior A, tentativa B, boot `FAILED`, motivo
  `BOOT_FAILED`, sequencia de tentativa 1, journal e recovery limpos, slot A
  `VALID` e slot B preservado como `INVALID`. O descarte observado na fila de
  eventos do mouse permanece externo ao fluxo EP9.2A e nao invalidou o caso.
- Hash de componente invalido no slot pendente validado.
  Concluida em: 2026-08-25 22:59 (America/Sao_Paulo).
  `BOOT_BAD_COMPONENT_HASH.img` iniciou somente o kernel legado autenticado.
  O comando `update system slots` registrou `DEGRADED`, sequencia 3, ativo A,
  pendente `NONE`, anterior A, tentativa B, boot `FAILED`, motivo
  `BOOT_FAILED`, sequencia de tentativa 1, journal e recovery limpos, slot A
  `VALID` e slot B preservado como `INVALID`. O descarte observado na fila de
  eventos do mouse permanece externo ao fluxo EP9.2A e nao invalidou o caso.
- Rollback de tentativa interrompida validado.
  Concluida em: 2026-08-25 23:01 (America/Sao_Paulo).
  `BOOT_ATTEMPT_INTERRUPTED.img` iniciou somente o kernel legado autenticado.
  O comando `update system slots` registrou `READY`, sequencia 4, ativo A,
  pendente `NONE`, anterior A, tentativa B, boot `FAILED`, motivo
  `BOOT_FAILED`, sequencia de tentativa 1, journal e recovery limpos e os
  slots A e B preservados como `VALID`. O descarte observado na fila de
  eventos do mouse permanece externo ao fluxo EP9.2A e nao invalidou o caso.
- Tolerancia a uma copia de estado corrompida validada.
  Concluida em: 2026-08-25 23:04 (America/Sao_Paulo).
  `STATE_ONE_BAD.img` iniciou A autenticado usando a unica copia valida do
  controle redundante. O comando `update system slots` registrou `READY`,
  sequencia 1, ativo A, pendente `NONE`, anterior A, tentativa `NONE`, boot e
  motivo `NONE`, journal e recovery limpos, slot A `VALID` e slot B `EMPTY`.
- Fallback com as duas copias de estado corrompidas validado.
  Concluida em: 2026-08-25 23:06 (America/Sao_Paulo).
  `STATE_BOTH_BAD.img` iniciou somente o kernel legado autenticado. O comando
  `update system slots` registrou `DEGRADED`, sequencia zero, ativo, pendente,
  anterior, boot e motivo `NONE`, tentativa exibida como A com sequencia zero,
  journal e recovery limpos e os slots A e B como `EMPTY`, sem promover
  metadados nao confiaveis. O fallback foi validado, mas a exibicao incorreta
  do slot de tentativa exige a repeticao deste caso apos a correcao.
- Sentinelas do estado ZSYS ausente corrigidos.
  Concluida em: 2026-08-25 23:08 (America/Sao_Paulo).
  A carga sem qualquer copia de estado valida agora inicializa slot anterior e
  slot em tentativa com `UPDATE_SYSTEM_SLOT_NONE`, coerente com o estado de
  boot `NONE` e com o contrato publico. A validacao QEMU aguarda o usuario.
- Sentinelas do estado ZSYS ausente revalidados.
  Concluida em: 2026-08-25 23:14 (America/Sao_Paulo).
  A repeticao de `STATE_BOTH_BAD.img` registrou `DEGRADED`, sequencia zero,
  ativo, pendente, anterior e tentativa `NONE`, boot e motivo `NONE`, journal
  e recovery limpos e os slots A e B como `EMPTY`. O fallback legado e a
  representacao publica sem indice de slot espurio ficaram validados.
- Recuperacao de journal em `PREPARED` validada.
  Concluida em: 2026-08-25 23:18 (America/Sao_Paulo).
  `JOURNAL_PREPARED.img` iniciou somente o kernel legado autenticado e a
  recuperacao do kernel descartou o staging incompleto. O comando
  `update system slots` registrou `READY`, sequencia 1, ativo A, pendente
  `NONE`, anterior A, tentativa, boot e motivo `NONE`, journal e recovery
  limpos, slot A `VALID` e slot B `EMPTY`. Uma entrada recebida durante o job
  cooperativo foi ignorada conforme a politica do Shell e nao invalidou o
  caso.
- Fallback sem volume FAT32 validado.
  Concluida em: 2026-08-25 23:20 (America/Sao_Paulo).
  `NO_VOLUME.img` iniciou somente o kernel legado autenticado. O comando
  `update system slots` registrou `DEGRADED`, sequencia zero, ativo, pendente,
  anterior e tentativa `NONE`, boot e motivo `NONE`, journal e recovery
  limpos, espaco livre zero e os slots A e B como `EMPTY`.
- Matriz QEMU e EP9.2A concluidas.
  Concluida em: 2026-08-25 23:20 (America/Sao_Paulo).
  O usuario validou os dez casos previstos: A ativo autenticado, B pendente
  confirmado, assinatura invalida, hash de imagem invalido, hash de componente
  invalido, tentativa interrompida, uma ou duas copias de estado corrompidas,
  journal `PREPARED` e volume FAT32 ausente. O `boot.asm` permaneceu
  inalterado; menu, F8 e retry manual continuam exclusivos da EP9.2B.
- Fixtures interativas da EP9.2B implementadas.
  Concluida em: 2026-08-25 23:40 (America/Sao_Paulo).
  A matriz ganhou `MENU_PREVIOUS_VALID`, `MENU_FAILED_VALID` e
  `MENU_RETRY_NO_CONTROL`, cobrindo anterior one-shot, candidato preservado em
  `FAILED` e retry sem controle redundante utilizavel. A execucao da matriz
  permanece reservada ao usuario.
- Integracao de build do menu pre-kernel implementada.
  Concluida em: 2026-08-25 23:50 (America/Sao_Paulo).
  O loader passou a vincular seu console privado e ganhou a variante
  deterministica VGA texto, o patch seguro de `stage2` sobre uma fixture e o
  alvo `make run-recovery-menu-vga`, preservando o layout anterior ao kernel e
  sem alterar `boot.asm`.
- EP9.2B implementada e entregue para validacao.
  Concluida em: 2026-08-26 09:36 (America/Sao_Paulo).
  O recovery loader agora oferece F8 temporizado, menu de falha, diagnostico
  A/B, anterior one-shot, retry manual com dupla confirmacao e fallback legado
  autenticado. O fluxo valida limites FAT32/VESA, estado redundante, journal,
  metadados assinados do slot e o ZSYS integral antes da execucao; tentativas
  sao gravadas e relidas antes de publicar `ZSBH`. A matriz QEMU ainda aguarda
  confirmacao pelo usuario.
- Geracao da matriz EP9.2B validada.
  Concluida em: 2026-08-26 09:41 (America/Sao_Paulo).
  O usuario confirmou que `make system-slots-matrix` terminou normalmente,
  gerando as fixtures e imagens da revisao atual. Essa validacao confirma o
  pipeline da matriz, mas ainda nao confirma os fluxos interativos no QEMU.
- Gates de qualidade e build da EP9.2B validados.
  Concluida em: 2026-08-26 09:45 (America/Sao_Paulo).
  O usuario confirmou que `make q3check` e `make clean && make` terminaram sem
  erros na mesma revisao usada para gerar a matriz. Permanecem pendentes apenas
  os casos funcionais especificos no QEMU.
- Boot automatico e menu voluntario da EP9.2B validados.
  Concluida em: 2026-08-26 09:53 (America/Sao_Paulo).
  Em `BOOT_ACTIVE_VALID`, o usuario confirmou que a expiracao da janela inicia
  A automaticamente e que F8 abre o menu sem timeout; Esc retomou o fluxo e A
  iniciou normalmente, sem alteracao persistente observada.
- Boot anterior one-shot da EP9.2B validado.
  Concluida em: 2026-08-26 09:57 (America/Sao_Paulo).
  Em `MENU_PREVIOUS_VALID`, o usuario confirmou a inicializacao do slot
  anterior A pelo menu, enquanto `update system slots` continuou exibindo B
  como ativo persistido. O retorno automatico a B apos reset ainda precisa ser
  confirmado.
- Retorno automatico ao slot ativo apos anterior one-shot validado.
  Concluida em: 2026-08-26 10:00 (America/Sao_Paulo).
  Apos reset sem F8 na mesma fixture, o usuario confirmou `READY`, `ativo=B`,
  `pendente=NONE`, `anterior=A`, `tentativa=NONE`, `boot=NONE` e `motivo=NONE`.
- Timeout de falha e preservacao do candidato EP9.2B validados.
  Concluida em: 2026-08-26 10:05 (America/Sao_Paulo).
  Em `MENU_FAILED_VALID`, sem F8, o menu expirou e iniciou o anterior A. O
  estado persistido permaneceu com `ativo=A`, `tentativa=B`, `boot=FAILED` e
  `motivo=BOOT_FAILED`, preservando B para diagnostico ou retry explicito.
- Cancelamento e retry manual da EP9.2B validados.
  Concluida em: 2026-08-26 10:07 (America/Sao_Paulo).
  Na mesma fixture, o usuario cancelou a confirmacao com Esc sem escrita e
  depois confirmou o retry de B. O sistema iniciou B e exibiu `ativo=B`,
  `pendente=NONE`, `tentativa=NONE`, `boot=NONE`, `motivo=NONE` e sequencia 6.
- Retry sem controle redundante da EP9.2B validado.
  Concluida em: 2026-08-26 10:09 (America/Sao_Paulo).
  Em `MENU_RETRY_NO_CONTROL`, o menu exibiu somente `INICIAR ANTERIOR` e
  `KERNEL LEGADO`; `TENTAR CANDIDATO` permaneceu desabilitado como exigido.
- Fallback VGA e matriz funcional da EP9.2B validados.
  Concluida em: 2026-08-26 10:11 (America/Sao_Paulo).
  O alvo `make run-recovery-menu-vga` exibiu o menu pre-kernel em VGA texto
  com diagnostico, estado A/B e acoes de anterior, retry e legado. Com os
  gates, a geracao da matriz, o boot ativo/F8, o anterior one-shot, o timeout
  de falha, o cancelamento/retry e a ausencia de controle ja confirmados, a
  matriz funcional especifica da EP9.2B foi concluida pelo usuario. O
  `boot.asm` permaneceu inalterado.
- EP9.3 implementada e entregue para validacao.
  Concluida em: 2026-08-26 10:42 (America/Sao_Paulo).
  Foram adicionados cache remoto ZSYS A/B no FAT32, transferencia autenticada
  em streaming, releitura final, aplicacao pelo staging existente,
  cancelamento confirmado do pendente e comandos `status`, `fetch`,
  `verify --cached`, `apply` e `cancel`. O escritor FAT32 incremental foi
  generalizado mantendo os wrappers dos slots. A matriz ganhou cache valido,
  uma ou duas copias corrompidas, transferencia interrompida e uma imagem
  guiada unica com snapshot QEMU. `boot.asm`, `stage2` e o recovery loader nao
  foram alterados. Gates e QEMU aguardam execucao do usuario.

- EP9.3: observacao de desempenho durante `update system apply --confirm`.
  Registrada em: 2026-08-26 11:19 (America/Sao_Paulo).
  Na matriz guiada, a operacao permaneceu sem conclusao visivel por cerca de
  cinco minutos, com o Shell bloqueado e entradas ignoradas. A validacao da
  aplicacao permanece pendente; investigar o tempo de escrita FAT32 e publicar
  progresso observavel antes de considerar o fluxo concluido.

- EP9.3: progresso observado apos a escrita do candidato.
  Registrado em: 2026-08-26 11:22 (America/Sao_Paulo).
  O job avancou para `Reconstrucao cooperativa do indice iniciada`, confirmando
  que a operacao nao estava parada; a lentidao permanece como pendencia de
  desempenho.

- EP9.3: cancelamento da aplicacao apos mais de dez minutos.
  Registrado em: 2026-08-26 11:30 (America/Sao_Paulo).
  F12/Esc cancelou o job com `Aplicacao ZSYS: CANCELLED`; o slot pendente nao
  foi publicado. A reconstrucao do indice e a exclusao FAT32 reportaram falhas
  repetidas, mantendo a investigacao de desempenho e I/O pendente.

- EP9.3: estado pos-cancelamento confirmado.
  Registrado em: 2026-08-26 11:32 (America/Sao_Paulo).
  `update system status` e `update system slots` reportaram cache `READY`,
  controles 2, slot A ativo, slot B `EMPTY`, pendente `NONE` e journal limpo.

- EP9.3: causa da lentidao identificada e correcao implementada.
  Implementada em: 2026-08-26 11:37 (America/Sao_Paulo).
  A reserva de cada cluster FAT32 reiniciava a busca no primeiro cluster,
  produzindo custo quadratico durante a escrita do ZSYS; a dica de alocacao
  agora avanca por volume, e a atualizacao do diretorio ocorre em checkpoints
  durante a escrita, com tamanho final confirmado no fechamento. O escritor
  transacional tambem passou a usar o alias temporario recebido, corrigindo o
  caminho de cache remoto.
  Apos a alteracao, a medicao QEMU permanece pendente.

- EP9.3: aplicacao confirmada apos a otimizacao de desempenho.
  Concluida em: 2026-08-26 11:48 (America/Sao_Paulo).
  O usuario repetiu `update system apply --confirm` na matriz guiada; a
  operacao terminou em tempo normal observado, o indice foi publicado e o
  slot ZSYS foi preparado e marcado como pendente. O loader informou para
  executar `reboot` para ativar, sem reinicio automatico. Nao houve medicao
  em segundos; a comparacao quantitativa continua N/D. Permaneceram dois
  avisos de exclusao FAT32, sem impedir a publicacao do pendente.

- EP9.3: rollback automatico apos reboot sem confirmacao.
  Validado em: 2026-08-26 11:51 (America/Sao_Paulo).
  Apos o `reboot`, o sistema voltou ao slot A com `pendente=NONE` e
  `anterior=A`. O candidato B ficou `INVALID` e a tentativa foi marcada como
  `BOOT_FAILED` com motivo `IO`, confirmando que a falha nao ativou o candidato
  nem entrou em ciclo automatico.

- EP9.3: diagnostico compacto e regcheck validados.
  Validado em: 2026-08-26 11:54 (America/Sao_Paulo).
  `health check` exibiu somente componentes degradados ou desabilitados, e
  `regcheck full` concluiu com `RegCheck: OK`; os avisos de nomes longos e
  espaco insuficiente foram reportados sem falha do verificador.

- EP9.3: tempo de aplicacao observado abaixo de um minuto.
  Validado em: 2026-08-26 11:55 (America/Sao_Paulo).
  O usuario informou que a execucao do `update system apply --confirm` e a
  sequencia de comandos de verificacao terminaram em menos de um minuto. O
  tempo exato nao foi cronometrado, portanto a metrica quantitativa permanece
  sem valor em segundos.

- EP9.4B: boot operacional autenticado no FAT32 implementado.
  Implementada em: 2026-08-26 12:30 (America/Sao_Paulo).
  A imagem padrao passou a 256 MiB com FAT32 iniciado no LBA 4096 e quatro
  setores por cluster. O recovery verifier ganhou compatibilidade com ABI 2,
  releitura e hash dos tres componentes, handoff privado `ZSBC` e execucao das
  continuacoes protegidas de boot, stage2 e kernel. ABI 1, estado v2, `ZSBH`,
  confirmacao A/B e fallback pelo kernel legado foram preservados. Foi criada
  uma unica matriz QEMU guiada, com uma solicitacao de chave, vetores de
  componente, handoff, retorno e corrupcao MBR/BPB; os gates e a validacao do
  usuario permanecem pendentes.

- EP9.4B: corrigida publicacao do alias final dos slots FAT32.
  Corrigida em: 2026-08-26 16:10 (America/Sao_Paulo).
  A primeira validacao ABI 2 publicou B como pendente, mas o recovery loader
  recusou o candidato com `IO` e fez rollback para A. O escritor havia
  renomeado `ZSTG.ZSY` para o nome longo `ZSB0.ZSY` preservando o alias curto
  temporario; como o loader pre-kernel consulta somente aliases 8.3, ele nao
  encontrava B. A renomeacao agora publica exatamente o alias 8.3 do destino.
  A repeticao dos gates e do caminho ABI 2 permanece pendente.

- EP9.4B: cadeia ABI 2 e promocao do slot B validadas.
  Validada em: 2026-08-26 16:22 (America/Sao_Paulo).
  Na imagem guiada de 256 MiB, o sistema iniciou pelo slot A ABI 1, publicou B
  como pendente com `update system apply --confirm` e reiniciou pela cadeia
  autenticada ABI 2. A confirmacao do kernel promoveu B para ativo, limpou o
  pendente e a tentativa e preservou A e B como `VALID`. O status final ficou
  `seq=4`, `ativo=B`, `anterior=A`, `boot=NONE` e `motivo=NONE`.

- EP9.4B: hashes individuais dos componentes recusados.
  Validada em: 2026-08-26 16:28 (America/Sao_Paulo).
  `BADBOOT.ZSY`, `BADSTG2.ZSY` e `BADKERN.ZSY` foram verificados na mesma
  sessao guiada. Os tres retornaram `ZSYS recusado: HASH`, identificaram
  `boot_abi=2` e nao realizaram gravacao.

- EP9.4B: handoff privado invalido recusado com rollback.
  Validada em: 2026-08-26 16:34 (America/Sao_Paulo).
  O pacote autenticado `BADHAND.ZSY` foi publicado como B pendente. No reboot,
  a continuacao invalidou `ZSBC`, retornou ao verifier e nao executou o kernel
  candidato. O sistema voltou a A com `pendente=NONE`; B permaneceu `VALID`
  para diagnostico e a tentativa ficou `FAILED` com motivo `BOOT_FAILED`, sem
  repeticao automatica.

- EP9.4B: retorno inesperado da cadeia autenticada tratado com rollback.
  Validada em: 2026-08-26 16:42 (America/Sao_Paulo).
  O pacote autenticado `RETURN.ZSY` foi publicado como B pendente e sua
  continuacao de boot retornou deliberadamente. O verifier limpou os handoffs,
  recusou a tentativa e voltou ao slot A. B permaneceu `VALID`, sem pendente,
  com tentativa `FAILED` e motivo `BOOT_FAILED`, sem ciclo automatico.

- EP9.4B: fallback legado com MBR FAT32 indisponivel validado.
  Validada em: 2026-08-26 16:44 (America/Sao_Paulo).
  Na overlay descartavel da matriz, a entrada de particao do MBR foi zerada e
  a maquina reiniciada. O recovery exibiu `FAT32 INDISPONIVEL`, restringiu o
  menu ao kernel legado e, apos o timeout de dez segundos, iniciou o ZephyrOS
  normalmente pela raiz fixa autenticada.

- EP9.4B: fallback legado com BPB FAT32 indisponivel validado.
  Validada em: 2026-08-26 16:48 (America/Sao_Paulo).
  A overlay foi restaurada e o setor do BPB no LBA 4096 foi zerado. O recovery
  voltou a exibir `FAT32 INDISPONIVEL`, manteve somente o kernel legado
  habilitado e iniciou normalmente o ZephyrOS depois do timeout, confirmando
  que a raiz fixa nao depende do BPB ou do volume operacional.

- EP9.4B: diagnosticos finais e consistencia FAT32 validados.
  Concluida em: 2026-08-26 16:51 (America/Sao_Paulo).
  Apos restaurar a imagem guiada, `health check` apresentou somente os estados
  degradados ou indisponiveis esperados do ambiente, e `regcheck full`
  concluiu com `RegCheck: OK`. `storage list` confirmou o volume operacional
  `ata2p1` montado em leitura e escrita, iniciado no LBA 4096, com particao
  tipo `0x0C`, setores de 512 bytes e quatro setores por cluster. A verificacao
  `storage check ata2p1` terminou com `Volume FAT32 consistente.` Os gates do
  host, a cadeia ABI 1/ABI 2, promocao, rollback, recusas autenticadas, falhas
  de handoff/retorno e os fallbacks MBR/BPB foram validados; a EP9.4B atende ao
  criterio de saida.

- EP9.4C: reinicio controlado pelo System Updater formalizado no roadmap.
  Planejada em: 2026-08-26 16:53 (America/Sao_Paulo).
  A subetapa foi definida para oferecer reinicio imediato ou posterior no
  Updater Classic depois da publicacao confirmada do pendente. O reinicio
  imediato exige confirmacao explicita e releitura do estado redundante; nao
  promove slots nem altera os contratos de boot. Falhas preservam o candidato,
  e o comando `reboot` continua disponivel como fluxo separado.

- EP9.4C: aba Sistema e reinicio controlado implementados.
  Concluida em: 2026-08-26 17:25 (America/Sao_Paulo).
  O System Updater Classic recebeu a sexta aba para operar o cache e os slots
  ZSYS pelo worker cooperativo, com tag validada, preflights e confirmacoes. A
  oferta de reinicio permite adiar sem limpar o pendente e exige confirmacao
  final, sequencia inalterada e releitura redundante antes de `power_reboot()`.
  Shell, Settings e Task Manager passaram a reutilizar o mesmo servico. Foram adicionadas
  fixtures compactas do preflight e uma unica imagem guiada pelo alvo
  `make run-ep94c-matrix`; os gates e a validacao QEMU aguardam o usuario.

- EP9.4C: tela preta ao concluir verificacao no Updater corrigida.
  Concluida em: 2026-08-26 17:48 (America/Sao_Paulo).
  A verificacao do cache concluiu dentro do worker cooperativo, que solicitava
  uma recomposicao VESA completa usando sua propria pilha. O Window Manager
  passou a consolidar essas solicitacoes e executar a recomposicao em seu ciclo
  periodico, preservando a interface Classic durante e depois do job. A
  validacao no QEMU aguarda a nova execucao pelo usuario.

- EP9.4C: estouro da pilha do worker de verificacao ZSYS corrigido.
  Concluida em: 2026-08-26 17:56 (America/Sao_Paulo).
  O redesenho adiado tornou observavel o panic de canario que estava oculto
  pela tela preta. O `Updater Worker` usava a pilha nativa padrao de 4 KiB no
  caminho combinado FAT32, SHA-256 e Ed25519. O worker passou a usar 16 KiB,
  mesmo limite ja empregado pelos processos principais, sem alterar outras
  pilhas. A validacao no QEMU aguarda a nova execucao pelo usuario.

- EP9.4C: verificacao ZSYS no System Updater Classic validada.
  Validada em: 2026-08-26 18:02 (America/Sao_Paulo).
  O usuario abriu a aba `Sistema` na imagem guiada, executou `Verificar` e o
  worker concluiu com motivo `NONE`. A janela permaneceu visivel, sem tela
  preta e sem panic de canario, confirmando a correcao da pilha e do redesenho
  cooperativo.

- EP9.4C: cancelamento da confirmacao de aplicacao validado.
  Validada em: 2026-08-26 18:04 (America/Sao_Paulo).
  O preflight abriu a confirmacao de publicacao e o usuario selecionou
  `Cancelar`. A interface retornou sem gravacao, mantendo `ativo=A`,
  `pendente=NONE` e o slot B vazio.

- EP9.4C: aplicacao confirmada e reinicio adiado validados.
  Validada em: 2026-08-26 18:08 (America/Sao_Paulo).
  O Updater publicou o slot B valido como pendente, preservou A como ativo e
  anterior, e a escolha `Depois` manteve o estado sem reiniciar. A aba Sistema
  exibiu o aviso persistente para usar `Reiniciar` quando oportuno.

- EP9.4C: persistencia do aviso de reinicio validada.
  Validada em: 2026-08-26 18:09 (America/Sao_Paulo).
  Depois de fechar e reabrir o aplicativo, a aba Sistema releu o estado e
  continuou exibindo o aviso, com A ativo e B valido e pendente.

- EP9.4C: cancelamento da confirmacao final de reinicio validado.
  Validada em: 2026-08-26 18:11 (America/Sao_Paulo).
  O usuario abriu a confirmacao pelo botao `Reiniciar` e selecionou
  `Cancelar`. A maquina permaneceu ligada e o estado continuou com A ativo e
  B pendente, sem escrita ou promocao antecipada.

- EP9.4C: reinicio controlado e promocao do slot B validados.
  Validada em: 2026-08-26 18:14 (America/Sao_Paulo).
  A confirmacao final reiniciou a maquina, a cadeia ABI 2 iniciou o sistema e
  o acknowledge do kernel promoveu B. Depois do boot, a aba Sistema mostrou
  `ativo=B`, `pendente=NONE`, nenhuma tentativa ou falha e os slots A e B
  validos, sem aviso de reinicio pendente.

- EP9.4C: diagnosticos finais da sessao guiada validados.
  Validada em: 2026-08-26 18:15 (America/Sao_Paulo).
  `health check` mostrou somente indisponibilidades e degradacoes esperadas do
  ambiente QEMU e dos servicos legados dependentes de FAT12. `regcheck full`
  concluiu com `RegCheck: OK`, sem regressao detectada depois do reinicio e da
  promocao do slot B.

- EP9.4C: criterio de saida encerrado.
  Concluida em: 2026-08-26 18:15 (America/Sao_Paulo).
  Os tres gates acordados foram executados pelo usuario, a sessao guiada
  validou o fluxo Classic completo e as recusas possuem fixtures
  deterministicas geradas pelo alvo unico. A evidencia da EP9.4B foi
  reutilizada para rollback conforme o plano. Nao ha comando adicional
  pendente para a EP9.4C.

- SYNC1: Top-Half e Bottom-Half de interrupcoes implementados.
  Concluida em: 2026-08-26 23:12 (America/Sao_Paulo).
  `irq_deferred` foi consolidado como fila estatica limitada executada pelo
  processo System, com identidade por proprietario/IRQ, coalescencia,
  reexecucao, cancelamento seguro, metricas e fixture privada. IDT passou a
  contabilizar ocorrencias e handlers; teclado, mouse, E1000 e RTL8139 foram
  migrados para Top-Halves minimos com recuperacao por polling normal. ATA
  permanece PIO sincrono e nenhuma `kworker` ou alteracao de boot foi criada.
  `irqstat`, `health check` e `regcheck full` receberam os diagnosticos e
  invariantes. Gates, QEMU e a matriz funcional aguardam execucao do usuario;
  por isso a SYNC1 ainda nao esta marcada como concluida no resumo do roadmap.

- SYNC1: primeira matriz QEMU executada pelo usuario.
  O autoteste `irqstat check`, a lista de IRQs, a suite `net check qemu
  net-pci-00:03.0 10.0.2.15`, `regcheck full`, `memcheck`, `log check` e o
  `ping 10.0.2.2` terminaram com `OK`; a NIC E1000 mostrou Bottom-Halfs sem
  rejeicoes. A carga de entrada durante o job, entretanto, observou descarte
  na fila de eventos do mouse/teclado e saturacao temporaria da fila IPC do
  foco. O horario exato desta execucao nao foi informado pelo usuario e fica
  pendente de complementacao.

- SYNC1: correcao de backpressure de entrada implementada.
  Concluida em: 2026-08-26 23:30 (America/Sao_Paulo).
  Os Bottom-Halfs PS/2 passaram a processar lotes limitados, drenando o
  `input core` entre lotes; o mouse passou a processar varios lotes por ciclo,
  coalescendo apenas movimento e preservando transicoes. A nova validacao deve
  confirmar zero overflow residual sob `regcheck full` e saida intensa.

- SYNC1: ajuste adicional de capacidade e contagem dos lotes de entrada.
  Concluida em: 2026-08-26 23:34 (America/Sao_Paulo).
  As filas brutas do teclado e do mouse foram ampliadas, a fila normalizada do
  mouse passou a absorver rajadas maiores e o Bottom-Half passou a contar
  publicacoes com sucesso para respeitar o limite de eventos. A validacao
  funcional permanece pendente do usuario.

- SYNC1: preservacao de roda junto com transicoes e movimento coalescido.
  Concluida em: 2026-08-26 23:36 (America/Sao_Paulo).
  O callback do mouse agora entrega separadamente uma transicao de botao, a
  roda e o movimento agregado quando um mesmo lote contem mais de uma dessas
  categorias. A validacao funcional permanece pendente do usuario.

- SYNC1: contenção de falha de publicação no Bottom-Half do teclado.
  Concluida em: 2026-08-26 23:38 (America/Sao_Paulo).
  Uma recusa inesperada do `input core` encerra o lote imediatamente para não
  consumir bytes adicionais enquanto o consumidor normal se recupera. A
  validação funcional permanece pendente do usuario.

- SYNC1: segunda execução de `regcheck full` diagnosticada pelo usuario.
  O comando terminou com `RegCheck: OK`, mas demorou para apresentar o início
  da operação e o mouse ficou preso no canto. `irqstat list` mostrou 4.393
  ocorrências de IRQ12 para 124 execuções do Bottom-Half, evidenciando que a
  preparação síncrona do diagnóstico impediu a drenagem normal. O horário
  exato desta execução não foi informado pelo usuario.

- SYNC1: rescan PCI cooperativo implementado.
  Concluida em: 2026-08-26 23:53 (America/Sao_Paulo).
  A enumeração PCI passou a ceder CPU a cada oito barramentos quando executada
  em contexto de processo, mantendo o comportamento do bootstrap e permitindo
  que o processo System drene entrada durante `regcheck full`. A validação
  funcional permanece pendente do usuario.

- SYNC1: preparação cooperativa do RegCheck e recuperação PS/2 implementadas.
  Concluida em: 2026-08-26 23:56 (America/Sao_Paulo).
  `regcheck full` passou a iniciar o job antes das verificações, dividir a
  preparação em fases e aguardar ao menos um tick após cada uma. O inventário
  também cede CPU entre subsistemas. O mouse passou a marcar lacunas da fila
  bruta, reiniciar a montagem depois delas e procurar o próximo cabeçalho PS/2
  plausível. A validação funcional permanece pendente do usuario.

- SYNC1: terceira execução de `regcheck full` diagnosticada pelo usuario.
  A operação iniciou mais rápido, terminou com `RegCheck: OK` e o mouse não
  permaneceu preso, mas ainda houve travamento temporário. A IRQ12 passou de
  353 ocorrências e 80 Bottom-Halfs para 17.049 ocorrências e 710
  Bottom-Halfs, sem rejeições diferidas; o mouse passou de zero para 13.153
  pacotes contabilizados como descartados, com `ERR_OVERFLOW`. A revisão do
  pipeline e a correlação dos contadores identificaram saturação da fila bruta:
  o Bottom-Half processava oito eventos por execução, não solicitava nova
  execução apenas por ainda existir backlog e a coalescência acontecia tarde.
  O consumidor final também processava somente oito limites de lote para até
  32 entregas intermediárias, e backpressure recuperável podia ser contado
  como descarte. O horário exato desta execução não foi informado pelo usuario.

- SYNC1: vazão e coalescência antecipada do ponteiro corrigidas.
  Concluida em: 2026-08-27 00:09 (America/Sao_Paulo).
  O `input core` passou a acumular somente movimentos consecutivos com mesma
  origem e estado de botões; a fila normalizada exige o mesmo estado de botões.
  Ambas preservam roda e transições em entradas próprias. O consumidor passou
  a processar 32 limites de lote por passagem, a fila bruta solicita
  reexecução enquanto há capacidade e backpressure recuperável deixou de ser
  contado como descarte definitivo. A validação funcional permanece pendente
  do usuario.

- SYNC1: quarta execução de `regcheck full` registrada pelo usuario.
  `irqstat check` e `regcheck full` terminaram em `OK`, sem rejeições
  diferidas, e o mouse iniciou a execução com zero descartes. Sob entrada
  manual intensa, a IRQ12 passou de 305 ocorrências, 86 Bottom-Halfs e 230
  coalescências para 25.421 ocorrências, 397 Bottom-Halfs e 2.548
  coalescências. O mouse terminou com 22.537 pacotes descartados e
  `ERR_OVERFLOW`, acompanhado do log de saturação do pipeline. O horário exato
  desta execução não foi informado pelo usuario.

- SYNC1: otimização de `regcheck full` adiada para a v1.0.0 por decisão do
  usuario.

  Registrada em: 2026-08-27 00:17 (America/Sao_Paulo).
  A lentidão, a atualização temporariamente interrompida do cursor e o
  overflow PS/2 sob estresse permanecem limitações conhecidas. O Roadmap 03
  recebeu a futura etapa de medição e otimização; a SYNC1 permanece aberta
  perante o critério de zero descarte. Nenhuma alteração de código foi feita
  nesta decisão.

- SYNC1: etapa concluída com dívida técnica aceita por decisão do usuario.
  Concluida em: 2026-08-27 00:19 (America/Sao_Paulo).
  A infraestrutura Top-Half/Bottom-Half, os diagnósticos e a recuperação do
  mouse foram aceitos como entrega da SYNC1. Lentidão e overflow PS/2 durante
  estresse extremo de `regcheck full` permanecem registrados e passaram a ser
  critério pendente de K5/v1.0.0. R4/SYNC3 e a `kworker` continuam pendentes.
  Nenhuma alteração de código foi feita neste encerramento.

- Governanca de dividas tecnicas da v1.0.0 documentada.
  Concluida em: 2026-08-27 00:24 (America/Sao_Paulo).
  Foi criado `docs/qualidade/dividas-tecnicas-v1.0.0.md` como fonte canonica,
  com identificadores `DT100-NNN`, estados, evidencia e criterios de quitacao.
  A limitacao aceita da SYNC1 foi cadastrada como `DT100-001`, ligada a K5 e
  referenciada pelos roadmaps relacionados. `docs/regras.md` e `AGENTS.md`
  passaram a exigir cadastro, rastreabilidade e validacao antes da quitacao.
  Mudanca exclusivamente documental; build e QEMU nao se aplicam.

- Auditoria das demais etapas para dividas tecnicas da v1.0.0 concluida.
  Concluida em: 2026-08-27 00:28 (America/Sao_Paulo).
  Foram revisados os roadmaps 01 a 16, `ROADMAP.md` e as metricas de qualidade.
  Nenhum novo item recebeu identificador: coberturas complementares nao
  bloqueantes, medicoes `N/D`, dependencias de hardware, lacunas de evidencia e
  etapas ainda nao iniciadas nao possuem aceite explicito como divida da
  v1.0.0. A triagem e a regra para eventual aceite futuro foram registradas em
  `docs/qualidade/dividas-tecnicas-v1.0.0.md`.

- SYNC2: implementacao das wait queues FIFO concluida.
  Concluida em: 2026-08-27 10:30 (America/Sao_Paulo).
  O servico R3 foi consolidado em um registro estatico geracional com entradas
  intrusivas de processo/thread, wake-one/all FIFO, condicao revalidada
  atomicamente, timeout absoluto, cancelamento e indisponibilidade. IPC,
  Editor, Explorer e Task Manager passaram a dormir nas filas; sockets nativos
  receberam eventos e espera bloqueante, e `net tcp connect` deixou de usar
  polling por `process_yield`. Foram acrescentados `wqinfo`, a fixture
  `net socket check`, metricas e invariantes em `health check` e
  `regcheck full`. A validacao de build e a matriz QEMU permanecem pendentes
  do usuario e devem ser registradas separadamente antes de concluir a etapa.

- SYNC2: primeira matriz funcional executada pelo usuario.
  `wait check` aprovou 14 invariantes, `net socket check` aprovou a fixture de
  eventos, e conexao TCP, suite QEMU, `memcheck` e `log check` terminaram em
  `OK`. `wqinfo foo` e `net socket foo` registraram o uso invalido esperado.
  `regcheck full` revelou uma regressao no cancelamento F11: o segundo ZAPP foi
  cancelado ainda pendente, antes de adquirir foco, e a validacao terminou com
  `cancelamento_f11 codigo=7`. O horario exato desta execucao nao foi informado
  pelo usuario.

- SYNC2: geracao de sinais IPC e ativacao pendente do App Loader corrigidas.
  Concluida em: 2026-08-27 11:48 (America/Sao_Paulo).
  `ipc_wait()` passou a consumir a geracao persistente do canal alem de
  mensagens, preservando wakes internos do Shell e dos workers do Updater e
  da App Store sem polling. O ciclo do Shell recebeu uma segunda passagem do
  App Loader depois do job, garantindo que um ZAPP criado pelo tratamento de
  resultado adquira foco antes da proxima espera. A nova validacao de build e
  o reteste funcional permanecem sob responsabilidade do usuario.

- SYNC2: correcao F11 revalidada no QEMU padrao pelo usuario.
  Concluida em: 2026-08-27 11:54 (America/Sao_Paulo).
  `regcheck full` terminou em `OK` depois do cancelamento F11. `wait check`
  aprovou 14 invariantes, `net socket check` aprovou toda a fixture e
  `memcheck` e `log check` terminaram sem falhas. `wqinfo` mostrou apenas os
  workers do Updater e da App Store bloqueados normalmente, em filas FIFO,
  sem entrada orfa. `health check` preservou somente os estados degradados ou
  indisponiveis esperados do ambiente. Os perfis USB HID, sem NIC e multi-NIC
  permanecem pendentes antes do encerramento da SYNC2.

- SYNC2: perfil USB HID parcialmente validado pelo usuario.
  Registrada em: 2026-08-27 16:34 (America/Sao_Paulo).
  Teclado e mouse USB Boot apareceram `READY`, `usb hid check`, `wait check`
  com 14 invariantes, `net socket check`, `regcheck full`, `memcheck` e
  `log check` terminaram em `OK`. `wqinfo` mostrou somente os workers do
  Updater e da App Store bloqueados normalmente, sem waiter orfao. A tentativa
  inicial de cancelamento usou quantidade 100, rejeitada corretamente pelo
  limite documentado de 1 a 10 do `ping`; o cancelamento F12 e o `health check`
  deste perfil permanecem pendentes.

- SYNC2: perfil USB HID concluido pelo usuario.
  Concluida em: 2026-08-27 16:35 (America/Sao_Paulo).
  `ping 10.0.2.2 10` foi cancelado por F12 depois da primeira resposta; o job
  encerrou como cancelado e restaurou ICMP, ARP e DNS. Teclado e mouse USB
  permaneceram `READY`, com crescimento dos relatorios, zero malformados,
  erros e descartes. `health check` mostrou apenas componentes degradados ou
  indisponiveis esperados do ambiente. Restam os perfis sem NIC e multi-NIC.

- SYNC2: primeira tentativa do perfil sem NIC diagnosticada.
  Registrada em: 2026-08-27 16:39 (America/Sao_Paulo).
  A execucao com `QEMU_NET_ARGS` vazio iniciou a E1000 padrao do QEMU; `net
  devices` mostrou `net-pci-00:03.0` ativa e a conexao TCP foi estabelecida.
  Os checks de espera, sockets, RegCheck, memoria e log passaram, mas essa
  evidencia pertence novamente ao perfil padrao. A matriz foi corrigida para
  usar `QEMU_NET_ARGS="-nic none"`; o perfil sem NIC permanece pendente.

- SYNC2: perfil sem NIC concluido pelo usuario.
  Concluida em: 2026-08-27 16:44 (America/Sao_Paulo).
  QEMU iniciado com `-nic none` nao publicou controladores. Sockets ficaram
  `INDISPONIVEL`, a fixture privada terminou em `OK` e a tentativa TCP falhou
  controladamente antes da configuracao DNS, sem socket ou waiter residual.
  `wait check` aprovou 14 invariantes, `wqinfo` mostrou somente os dois workers
  esperados, e `regcheck full`, `memcheck` e `log check` terminaram em `OK`.
  `health check` publicou Network como `DISABLED` pelo motivo esperado. Resta
  somente o perfil multi-NIC antes do encerramento da SYNC2.

- SYNC2: perfil multi-NIC concluido pelo usuario.
  Concluida em: 2026-08-27 16:49 (America/Sao_Paulo).
  Os IDs `net-pci-00:03.0` E1000 e `net-pci-00:04.0` RTL8139 foram copiados do
  inventario da execucao. `net check qemu multi` aprovou TX isolado em ambas e
  invariantes Multi-NIC. IRQ11 publicou dois handlers, quatro agendamentos e
  quatro execucoes de Bottom-Half, sem rejeicoes. A conexao TCP foi
  estabelecida; `net socket check` e as 14 invariantes de `wait check`
  terminaram em `OK`. `regcheck full`, `memcheck` e `log check` tambem foram
  aprovados, e `health check` preservou somente limitacoes esperadas do
  ambiente.

- SYNC2: etapa concluida e validada pelo usuario.
  Concluida em: 2026-08-27 16:49 (America/Sao_Paulo).
  A matriz completa aprovou QEMU padrao, cancelamento F11 corrigido, USB HID
  com cancelamento F12, ausencia controlada de NIC e E1000 + RTL8139. Filas
  FIFO, wake-one/all, timeouts, cancelamento, geracoes IPC e eventos de sockets
  permaneceram consistentes, sem espera ocupada, waiter orfao ou rejeicao
  permanente. Nenhuma nova divida tecnica foi aceita. SYNC3/R4 e a `kworker`
  continuam pendentes.

- SYNC3/R4: divida tecnica `DT100-002` aceita pelo usuario.
  Aceita em: 2026-08-27 17:09 (America/Sao_Paulo).
  A `Zephyr kworker` pode encerrar a etapa como processo ring0 dedicado. O
  consumo de um slot e uma stack de processo, sem participacao produtiva no
  scheduler isolado de `thread_t`, deve ser quitado pela K5 antes da v1.0.0.

- SYNC3/R4: implementacao de Kernel Workqueues concluida.
  Concluida em: 2026-08-27 18:51 (America/Sao_Paulo).
  Foi criada a fila estatica geracional com prioridades `HIGH`/`NORMAL`,
  prazos absolutos, coalescencia, reexecucao, cancelamento, snapshots e
  fallback. A `Zephyr kworker` bloqueia numa Wait Queue e assumiu
  Bottom-Halves, timers, rede/sockets e indexacao. `workq check` inclui fixture
  privada e o percurso real Shell -> Wait Queue -> kworker -> wake. Os
  contratos, Roadmaps 03/09/12, diagnosticos, RegCheck e Health foram
  atualizados. Os gates e a matriz QEMU permanecem pendentes do usuario; a
  SYNC3/R4 ainda nao foi marcada como concluida.

- SYNC3/R4: janela da imagem de recovery ajustada apos o primeiro build.
  Concluida em: 2026-08-27 18:57 (America/Sao_Paulo).
  O kernel passou a ocupar 2.956 setores e excedeu em 9.934 bytes a janela
  antiga entre os LBAs 64 e 3000. O recovery loader foi reposicionado para o
  LBA 3584, mantendo o FAT32 no LBA 4096, 256 KiB reservados ao loader e cerca
  de 282 KiB livres para crescimento do kernel. Stage 2, Makefile e contratos
  foram sincronizados; `src/boot/boot.asm` permaneceu inalterado. A repeticao
  dos gates pelo usuario permanece pendente.

- SYNC3/R4: gates e inicializacao do QEMU validados pelo usuario.
  Concluida em: 2026-08-27 19:02 (America/Sao_Paulo).
  `make q3check` e `make clean && make` terminaram corretamente depois do
  reposicionamento do recovery loader. `make run` iniciou a imagem resultante
  no QEMU. A matriz funcional da workqueue e dos servicos migrados permanece
  pendente antes de marcar SYNC3/R4 como concluida.

- SYNC3/R4: nucleo da workqueue validado no QEMU padrao pelo usuario.
  Concluida em: 2026-08-27 19:04 (America/Sao_Paulo).
  `workq status` mostrou a `Zephyr kworker` ativa no PID 2, contexto KWORKER,
  fallback inativo, cinco trabalhos registrados e zero rejeicoes, erros de
  callback ou contexto invalido. `workq check` aprovou ciclo, FIFO,
  prioridades sem starvation, atrasos, rollover, promocao, coalescencia,
  reexecucao, cancelamento, capacidade, interrupcoes, wake real e invariantes.
  `irqstat check`, `timer check` e as 14 invariantes de `wait check` terminaram
  em `OK`. `wqinfo` mostrou as filas KWORKER e WORKQ_PROBE disponiveis, sem
  waiter orfao, e o inventario publicou `net-pci-00:03.0` E1000 ativa. A rede,
  indexacao, RegCheck e diagnosticos finais do perfil permanecem pendentes.

- SYNC3/R4: servicos migrados validados no QEMU padrao pelo usuario.
  Concluida em: 2026-08-27 19:07 (America/Sao_Paulo).
  A reconstrucao cooperativa do indice publicou 26 itens e terminou com
  `index check` valido. A fixture de sockets passou e a conexao TCP com
  `example.com:80` foi estabelecida. `net check qemu tcp
  net-pci-00:03.0 example.com` aprovou DHCP, DNS, TCP, checksum, RX/TX,
  resposta HTTP, fechamento, polling e invariantes. `regcheck full`,
  `memcheck` e `log check` terminaram em `OK`; `health check` preservou apenas
  componentes degradados ou indisponiveis esperados do ambiente. `workq foo`
  foi rejeitado com o uso correto. A confirmacao explicita de responsividade
  da entrada durante a carga e os perfis complementares permanecem pendentes.

- SYNC3/R4: perfil USB HID validado pelo usuario.
  Concluida em: 2026-08-27 19:11 (America/Sao_Paulo).
  Teclado e mouse USB Boot apareceram `READY`, com zero relatorios malformados,
  erros e descartes; `usb hid check` terminou em `OK`. A `Zephyr kworker`
  permaneceu ativa no PID 2, em contexto KWORKER e sem fallback, rejeicoes,
  erros de callback ou contexto invalido. `workq check` aprovou todas as
  invariantes e IRQ11 publicou 227 agendamentos e execucoes de Bottom-Half,
  sem rejeicao. O ping foi cancelado por F12 depois de quatro respostas e
  restaurou ICMP, ARP e DNS. `regcheck full` terminou em `OK` e `health check`
  preservou somente estados esperados do ambiente. A confirmacao explicita de
  clique, arraste e roda e os demais perfis complementares permanecem
  pendentes.

- SYNC3/R4: primeira inicializacao do perfil storage diagnosticada.
  Registrada em: 2026-08-27 19:13 (America/Sao_Paulo).
  As sete imagens de fixture foram geradas, mas o QEMU recusou a topologia com
  `IDE unit 1 is in use`: o disco principal usava selecao automatica enquanto
  as fixtures reservavam os indices IDE 1, 2 e 3. O argumento canonico do
  disco de boot passou a fixa-lo em `ide.0`, unidade 0, preservando as tres
  unidades restantes para as fixtures. O perfil storage ainda nao iniciou e
  sua validacao permanece pendente.

- SYNC3/R4: topologia do perfil storage revalidada pelo usuario.
  Concluida em: 2026-08-27 19:18 (America/Sao_Paulo).
  Os gates terminaram corretamente e `make run-storage` iniciou o QEMU depois
  da fixacao do disco principal em `ide.0`, unidade 0. `storage list` publicou
  `ata0` como sistema, `ata1` como fixture valida, `ata2` com quatro volumes
  invalidos controlados e `ata3p1` como formato nao suportado, totalizando
  quatro discos e onze volumes. Todos os discos auxiliares permaneceram com
  zero escritas. IRQ14 cresceu para 3 ocorrencias e continuou sem Bottom-Half;
  a `Zephyr kworker` permaneceu ativa no PID 2, sem fallback, rejeicoes ou
  erros. Montagem, leitura, indexacao e diagnosticos finais do perfil ainda
  permanecem pendentes.

- SYNC3/R4: leitura e montagem ATA validadas pelo usuario.
  Concluida em: 2026-08-27 19:22 (America/Sao_Paulo).
  Os volumes exatos `ata1p1` FAT12 e `ata1p4` FAT32 foram montados em modo
  somente-leitura. `storage check ata1p4` concluiu a verificacao FAT32 sem
  inconsistencias e as montagens dispararam reconstrucao cooperativa seguida
  de publicacao do indice. `storage list` mostrou quatro montagens e confirmou
  zero escritas em `ata1`, `ata2` e `ata3`, inclusive depois de 1.040 leituras
  na fixture valida. Cancelamento, rebuild completo e diagnosticos finais do
  perfil permanecem pendentes.

- SYNC3/R4: perfil storage concluido pelo usuario.
  Concluida em: 2026-08-27 19:24 (America/Sao_Paulo).
  F12 cancelou o primeiro rebuild depois de um passo, preservando 33 entradas
  ativas e suspendendo a indexacao automatica. O rebuild seguinte retomou e
  publicou 33 entradas de quatro fontes, com seis diretorios e 42 passos;
  `index check` terminou valido. A `Zephyr kworker` permaneceu ativa no PID 2,
  sem fallback, rejeicoes, erros de callback, contexto invalido ou falha de
  wake mesmo depois de mais de 35 mil execucoes. IRQ14 registrou tres
  ocorrencias e nenhum Bottom-Half, preservando o ATA sincrono. `regcheck full`
  terminou em `OK`, incluindo as rejeicoes esperadas das fixtures `ata2*`, e
  `health check` manteve somente estados esperados do ambiente. A confirmacao
  explicita de responsividade da entrada e os perfis sem NIC e multi-NIC
  permanecem pendentes.

- SYNC3/R4: perfil sem NIC concluido pelo usuario.
  Concluida em: 2026-08-27 19:28 (America/Sao_Paulo).
  QEMU iniciado com `-nic none` nao publicou controlador de rede. Sockets
  ficaram `INDISPONIVEL`, a fixture privada terminou em `OK` e a conexao TCP
  falhou controladamente com codigo 7 antes da configuracao DNS. A `Zephyr
  kworker` permaneceu ativa no PID 2, sem fallback, rejeicoes, erros de
  callback, contexto invalido ou falha de wake; o trabalho periodico de rede
  nao foi registrado. `workq check`, as 14 invariantes de `wait check`,
  `regcheck full`, `memcheck` e `log check` terminaram em `OK`. `health check`
  publicou Network como `DISABLED` pelo motivo esperado. O perfil multi-NIC e
  a confirmacao explicita de responsividade da entrada permanecem pendentes.

- SYNC3/R4: inventario multi-NIC confirmado pelo usuario.
  Registrada em: 2026-08-27 19:30 (America/Sao_Paulo).
  O perfil publicou `net-pci-00:03.0` E1000 e `net-pci-00:04.0` RTL8139 como
  controladores ativos. A `Zephyr kworker` iniciou no PID 2, em contexto
  KWORKER, com cinco trabalhos registrados, fallback inativo e zero rejeicoes,
  erros de callback, contexto invalido ou falhas de wake. A suite multi-NIC,
  TCP e os diagnosticos finais do perfil permanecem pendentes.

- SYNC3/R4: diagnosticos do perfil multi-NIC concluidos pelo usuario.
  Concluida em: 2026-08-27 19:34 (America/Sao_Paulo).
  `net check qemu multi net-pci-00:03.0 net-pci-00:04.0` aprovou TX isolado
  na E1000 e na RTL8139 e as invariantes Multi-NIC. IRQ11 publicou dois
  handlers e quatro agendamentos e execucoes de Bottom-Half, sem rejeicoes.
  A fixture de sockets passou e a conexao TCP com `example.com:80` foi
  estabelecida. A `Zephyr kworker` permaneceu ativa no PID 2, sem fallback,
  rejeicoes, erros de callback, contexto invalido ou falhas de wake; seus
  cinco trabalhos e prazos permaneceram consistentes. `workq check`, as 14
  invariantes de `wait check`, `regcheck full`, `memcheck` e `log check`
  terminaram em `OK`; `health check` manteve somente estados esperados do
  ambiente. Resta apenas a confirmacao manual de responsividade da entrada
  antes do encerramento da SYNC3/R4.

- SYNC3/R4: etapa concluida e validada pelo usuario.
  Concluida em: 2026-08-27 19:34 (America/Sao_Paulo).
  O usuario confirmou que teclado, movimento, clique, arraste e roda
  permaneceram responsivos durante rede, indexacao e `regcheck full`, sem
  travamento ou perda perceptivel de transicoes. A matriz completa aprovou os
  gates, QEMU padrao, USB HID, Storage, ausencia controlada de NIC e E1000 +
  RTL8139. Callbacks executaram na `Zephyr kworker` com interrupcoes
  habilitadas, sem espera ocupada, rejeicao permanente, fila orfa ou fallback
  indevido. SYNC3 e R4 foram marcadas como concluidas; SYNC4 permanece
  pendente. Nenhuma nova divida foi criada: a eventual regressao de entrada
  sob carga permanece coberta pela `DT100-001`, ainda `ACEITA`, e o uso da
  kworker como processo ring0 permanece coberto pela `DT100-002`.

- SYNC4: implementação do sistema de sinais assíncronos concluída.
  Concluída em: 2026-08-27 22:52 (America/Sao_Paulo).
  Foi criado o núcleo de sinais para processos ring3 com bitmap coalescido,
  máscaras, ações, contexto salvo, vínculo pai/filho, `SIGCHLD`, snapshots e
  invariantes. A App API passou para 0.4 com as syscalls 10-13, frame ring3 e
  trampoline de `signal_return`. Exceções de usuário geram `SIGSEGV`; IRQs e
  syscalls preparam uma entrega antes do `iret`. Ctrl esquerdo/direito de PS/2
  e USB, Shell, Task Manager, App Loader, Wait Queues, `kill`, `sigtest`,
  RegCheck e Health foram integrados. Pacotes novos usam App API 0.4 e o
  contrato append-only mantém pacotes 0.3 aceitos. `src/boot/boot.asm`,
  `stage2.asm` e as rotinas Assembly de interrupção permaneceram inalterados.
  Os gates e a matriz funcional pertencem ao usuário; SYNC4 continua aberta
  e nenhuma dívida técnica nova foi criada.

- SYNC4: correção de compilação após o primeiro gate do usuário.
  Concluída em: 2026-08-27 23:07 (America/Sao_Paulo).
  `signal.c` passou a incluir o contrato de syscalls que declara
  `APP_SYSCALL_SIGNAL_RETURN`; `process_create_user_test()` passou a fornecer
  a mensagem de dados com o tipo `const uint8_t*`, eliminando o warning de
  signedness observado em `process.c`. Nenhum arquivo de boot ou Assembly foi
  alterado.

- SYNC4: correção do bloqueio de entrada ring3 e diagnóstico de invariantes.
  Concluída em: 2026-08-27 23:32 (America/Sao_Paulo).
  `message_receive` de processos ring3 passou a esperar pela fila IPC com
  interrupções habilitadas, acordando por mensagem, timeout ou sinal sem
  manter o processo em loop ocupado. A condição IPC também revalida sinais
  entregáveis para evitar perda de wakeup quando o sinal chega durante a
  transição para a fila. `process_signal_validate_state()` passou a registrar
  o PID e a classe da invariante que falhar, preservando o estado para o
  diagnóstico do `sigtest`. A validação executável desta correção permanece
  sob responsabilidade do usuário.

- SYNC4: correção da falsa invariante do processo Idle.
  Concluída em: 2026-08-27 23:33 (America/Sao_Paulo).
  A validação de sinais deixou de tratar o PID 0, cujo pai legítimo também é
  0, como processo auto-parentado. O `sigtest` passa a validar o Idle sem
  relaxar a regra para processos ring3. A validação executável permanece sob
  responsabilidade do usuário.

- SYNC4: validação parcial reportada pelo usuário.
  Registrada em: 2026-08-27 23:45 (America/Sao_Paulo).
  A captura fornecida mostra `sigtest`, `usertest fault`, `irqstat check`,
  `timer check`, `wait check`, `workq check`, `schedcheck`, `net socket check`,
  `net check qemu`, `regcheck full`, `memcheck` e `log check` em `OK`. O
  `app inputtest` recebeu Enter e encerrou o ZAPP com código 0; `sigtest foo`
  registrou uso inválido sem alterar o estado. O horário exato da execução
  não foi informado na captura. Permanecem pendentes os cenários de Ctrl+C
  no `app inputtest` e a matriz USB HID antes de encerrar SYNC4.

- SYNC4: Ctrl+C em aplicativo ring3 validado pelo usuário.
  Registrada em: 2026-08-27 23:47 (America/Sao_Paulo).
  A captura mostra `app inputtest` encerrado por `SIGINT`, com foco devolvido
  ao Shell e prompt responsivo. O horário exato da execução não foi informado
  na captura. Permanece pendente a repetição no perfil USB HID e a limpeza de
  uma linha parcial por Ctrl+C no prompt.

- SYNC4: novo encerramento por Ctrl+C reportado pelo usuário.
  Registrada em: 2026-08-27 23:49 (America/Sao_Paulo).
  A captura confirma outro `app inputtest` encerrado por `SIGINT`, com foco
  devolvido ao Shell e prompt responsivo. A origem PS/2 ou USB não pode ser
  identificada nesta captura; a confirmação do dispositivo USB permanece
  pendente.

- SYNC4: dispositivos USB HID confirmados pelo usuário.
  Registrada em: 2026-08-27 23:50 (America/Sao_Paulo).
  `usb hid status` listou teclado e mouse USB em `READY`, ambos ativos, sem
  relatórios malformados, descartes ou cancelamentos. A confirmação de Ctrl+C
  no perfil USB fica associada ao teste anterior; permanece apenas a limpeza
  de uma linha parcial por Ctrl+C no prompt.

- SYNC4: encerramento formal após a matriz funcional do usuário.
  Concluída em: 2026-08-27 23:53 (America/Sao_Paulo).
  A captura final confirma que uma linha parcial no Shell foi limpa por
  `Ctrl+C` e que o prompt reapareceu responsivo. Com as capturas anteriores,
  ficam confirmados `sigtest` em `OK`, entrega de `SIGINT` ao ZAPP, falha ring3
  isolada, diagnósticos de IRQ/timer/wait/workqueue/sockets, `regcheck full`,
  `health check`, `memcheck` e `log check` em `OK`, além de teclado e mouse USB
  em `READY`. O teste específico de `kill` com PID ring3 não foi executado por
  ausência de um PID fornecido; nenhum PID foi presumido, e a fixture `sigtest`
  cobriu as regras de envio e sinais fatais. SYNC4 foi marcada como concluída;
  R5 permanece pendente no Roadmap 09. Nenhuma dívida técnica nova foi criada.

- VFS1: descritores e operacoes unificadas de I/O implementados.
  Implementada em: 2026-08-28 00:35 (America/Sao_Paulo).
  Foram adicionados o nucleo VFS, 32 descritores por processo, pool global de
  32 arquivos regulares, stdin/stdout/stderr, `lseek`, App API 0.5, syscall 14,
  compatibilidade de pacotes 0.3/0.4/0.5, integracao ao ciclo de vida dos
  processos e diagnosticos `vfs`, `appcheck`, `regcheck full` e `health check`.
  `app inputtest` passou a exercer os fds 0 e 1 em ring3. Boot, Stage 2 e
  assembly de interrupcoes nao foram alterados. Conforme a politica do
  projeto, o agente nao executou build, testes ou QEMU. VFS1 permanece
  implementada e aguardando a matriz do usuario; nenhuma divida tecnica foi
  criada.

- VFS1: primeira execucao de `make package-test` reportada pelo usuario.
  O horario exato da execucao nao foi informado. Todas as verificacoes
  passaram, exceto `packager_hybrid_lfn`; o alvo terminou com codigo 1 e a
  validacao da VFS1 permaneceu aberta.

- VFS1: criterio da fixture FAT32 LFN corrigido.
  Corrigida em: 2026-08-28 09:19 (America/Sao_Paulo).
  O autoteste consultava o primeiro byte da entrada curta esperando `0x01`,
  embora esse byte pertença ao alias 8.3. A verificacao agora confere `0x01`
  na segunda entrada LFN e valida separadamente o primeiro byte do alias. O
  agente nao reexecutou testes; `make package-test` aguarda repeticao pelo
  usuario.

- VFS1: segunda execucao de `make package-test` reportada pelo usuario.
  O horario exato da execucao nao foi informado. `packager_hybrid_lfn`
  permaneceu como unica falha; leitura, alias e substituicao FAT32 continuaram
  em `OK`.

- VFS1: fixture de fronteira do diretorio FAT32 corrigida.
  Corrigida em: 2026-08-28 09:21 (America/Sao_Paulo).
  O volume de teste usa quatro setores por cluster e comporta 64 entradas no
  diretorio raiz, mas a fixture criava somente sete arquivos e nao alcancava a
  extensao de cadeia que pretendia validar. A quantidade de preenchimento
  agora e derivada da geometria FAT32 para forcar o LFN a atravessar a
  fronteira. O agente nao reexecutou testes; `make package-test` aguarda nova
  repeticao pelo usuario.

- VFS1: terceira execucao de `make package-test` reportada pelo usuario.
  O horario exato da execucao nao foi informado. `packager_hybrid_lfn`
  permaneceu como unica falha, com os demais casos em `OK`.

- VFS1: travessia nao contigua da cadeia LFN corrigida no autoteste.
  Corrigida em: 2026-08-28 09:24 (America/Sao_Paulo).
  A segunda entrada LFN foi gravada no proximo cluster logico do diretorio,
  enquanto o teste ainda a procurava no proximo endereco fisico. Como os
  clusters intermediarios pertencem aos arquivos de preenchimento, a
  verificacao agora segue a cadeia FAT, calcula o cluster de continuacao e
  confirma nele a segunda entrada e o alias curto. O agente nao reexecutou
  testes; `make package-test` aguarda nova repeticao pelo usuario.

- VFS1: `make package-test` aprovado pelo usuario.
  Registrada em: 2026-08-28 09:27 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Todos os casos do empacotador
  terminaram em `OK`, incluindo compatibilidade de API, fixtures da App Store,
  imagem hibrida, LFN FAT32, alias e substituicao. O alvo encerrou com
  `Packager self-test OK`.

- VFS1: `make q3check` aprovado pelo usuario.
  Registrada em: 2026-08-28 09:29 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Whitespace, protecao do boot,
  funcoes falhaveis, contratos publicos, registro de metricas e cadeias de
  confianca ZUPD/AS5 terminaram em `OK`; as fontes Terminus geradas tambem
  foram confirmadas.

- VFS1: `make clean && make` aprovado pelo usuario.
  Registrada em: 2026-08-28 09:32 (America/Sao_Paulo).
  O horario exato da execucao e a saida textual completa nao foram informados;
  o usuario confirmou que o build limpo terminou sem pendencias. Os gates
  `make package-test`, `make q3check` e `make clean && make` ficam confirmados
  para esta versao antes da abertura no QEMU.

- VFS1: diagnosticos iniciais no QEMU padrao aprovados pelo usuario.
  Registrada em: 2026-08-28 09:35 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `vfs status` exibiu VFS em
  `READY`, pool regular `0/32`, sete processos e 21 descritores, correspondendo
  aos tres fds padrao por processo. `vfs test` aprovou `12/12` e devolveu o
  pool a zero; `vfs test foo` foi rejeitado com o uso correto. `appcheck`
  confirmou App API 0.5, abertura, leitura sequencial, `file_lseek`, fechamento
  e as rejeicoes esperadas, terminando o job cooperativo e devolvendo o prompt
  responsivo.

- VFS1: Enter e Ctrl+C aprovados; F12 revelou limpeza prematura de stdin.
  Registrada em: 2026-08-28 09:44 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Enter encerrou o ZAPP com
  codigo 0 e Ctrl+C encerrou por `SIGINT`; em ambos os casos o foco retornou,
  a VFS permaneceu `READY`, o pool ficou em `0/32` e o Shell reteve somente os
  fds 0-2. No caso F12, a liberacao encontrou o fd 0 ainda ativo, o zombie nao
  foi removido e o Shell ficou preso repetindo a falha.

- VFS1: cancelamento diferido de leitura stdin implementado.
  Corrigida em: 2026-08-28 09:44 (America/Sao_Paulo).
  O cancelamento de processo ring3 bloqueado agora registra estado pendente,
  acorda a wait queue e deixa a syscall desempilhar a operacao VFS. No retorno
  de usuario, o processo aplica o cancelamento, libera os descritores e entra
  na trampoline de termino seguro. `WAIT_REASON_CANCELLED` encerra a leitura
  stdin sem registrar falha espuria. Boot, Stage 2 e assembly de interrupcoes
  permaneceram inalterados. A correcao aguarda repeticao dos gates pelo
  usuario.

- VFS1: cancelamento F12 de stdin aprovado pelo usuario no QEMU padrao.
  Registrada em: 2026-08-28 09:51 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `app inputtest` publicou o
  cancelamento do PID 7 e devolveu o foco ao Shell. `vfs status` confirmou
  estado `READY`, pool regular `0/32`, sete processos, 21 descritores, somente
  os fds 0-2 no Shell e zero falhas VFS. O deadlock de limpeza e a repeticao
  de erros nao reapareceram. A confirmacao textual dos gates da versao
  corrigida ainda permanece pendente.

- VFS1: gates da versao corrigida confirmados pelo usuario.
  Registrada em: 2026-08-28 09:52 (America/Sao_Paulo).
  O usuario confirmou que `make package-test`, `make q3check` e
  `make clean && make` foram executados antes do teste da correcao no QEMU,
  conforme o fluxo operacional obrigatorio do projeto. Esses gates nao devem
  ser reapresentados como pendencias funcionais desta versao.

- VFS1: Enter e Ctrl+C repetidos no QEMU padrao apos a correcao de F12.
  Registrada em: 2026-08-28 09:57 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Enter encerrou o PID 7 com
  codigo 0; Ctrl+C encerrou o PID 7 por `SIGINT`. Nos dois casos o foco voltou
  ao Shell, a VFS permaneceu `READY`, o pool regular ficou em `0/32`, sete
  processos mantiveram 21 descritores e somente os fds 0-2 permaneceram no
  Shell. O caso Ctrl+C, apesar da limpeza correta, registrou `Falha em leitura
  VFS` e elevou a metrica de falhas para um.

- VFS1: interrupcao controlada de stdin por sinal corrigida.
  Corrigida em: 2026-08-28 09:57 (America/Sao_Paulo).
  `WAIT_REASON_SIGNAL` agora conclui a leitura bloqueada com `OK` e zero bytes,
  permitindo que o hook de retorno entregue o sinal e encerre ou retome o
  processo sem log nem metrica espurios de falha VFS. Boot, Stage 2 e assembly
  de interrupcoes permaneceram inalterados. Como houve alteracao de codigo,
  esta nova versao aguarda os gates e a repeticao do Ctrl+C pelo usuario.

- VFS1: repeticao de Ctrl+C sem falha espuria aprovada no QEMU padrao.
  Registrada em: 2026-08-28 10:26 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `app inputtest` encerrou o
  PID 7 por `SIGINT` e devolveu o foco ao Shell sem registrar `Falha em leitura
  VFS`. `vfs status` confirmou estado `READY`, pool regular `0/32`, sete
  processos, 21 descritores, somente os fds 0-2 no Shell e zero falhas. Pelo
  fluxo operacional ja confirmado pelo usuario, os gates foram executados
  antes da abertura desta versao corrigida no QEMU e nao permanecem como
  pendencia funcional. Enter, Ctrl+C e F12 ficam aprovados separadamente.

- VFS1: matriz complementar do QEMU padrao aprovada pelo usuario.
  Registrada em: 2026-08-28 10:29 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `usertest fault` isolou e
  encerrou o PID 7; `regcheck full` terminou em `OK`; `memcheck` aprovou
  integridade do heap, coalescencia, guardas PMM e diretorios de usuario; e
  `log check` aprovou os oito casos sem falhas. `health check` nao publicou
  falha da VFS; os estados exibidos pertencem a AC97, Media Player, USB,
  Update, App Store e USB HID indisponiveis ou degradados neste perfil.

- VFS1: validacao do modo Simple excluida pelo usuario.
  Registrada em: 2026-08-28 10:29 (America/Sao_Paulo).
  O usuario informou que nao testa `guimode simple`. O modo Classic ja estava
  ativo nas capturas e permanece a interface principal. Como a entrega nao
  modifica a implementacao visual do fallback Simple, essa exclusao nao
  representa defeito conhecido nem divida tecnica da VFS1.

- VFS1: matriz do QEMU padrao concluida pelo usuario.
  Registrada em: 2026-08-28 10:31 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. O `vfs status` final exibiu
  estado `READY`, pool regular `0/32`, sete processos, 21 descritores, somente
  stdin, stdout e stderr no Shell e contadores de falha zerados. A matriz do
  perfil padrao fica concluida sem residuos; resta a validacao USB HID.

- VFS1: primeira matriz no perfil USB HID executada pelo usuario.
  Registrada em: 2026-08-28 10:36 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `usb hid status` confirmou
  teclado e mouse em `READY`; `app inputtest` recebeu entrada USB, encerrou o
  PID 7 por `SIGINT` e devolveu o foco; `vfs status` confirmou `READY`, pool
  `0/32`, sete processos, 21 descritores e zero falhas. `health check` nao
  publicou falha VFS. A matriz permaneceu aberta porque `regcheck full`
  terminou em erro `usb codigo=7`, com QH Interrupt e runtime UHCI reportados
  como invalidos.

- VFS1: invariante transitiva do QH Interrupt UHCI corrigida.
  Corrigida em: 2026-08-28 10:36 (America/Sao_Paulo).
  O validador exigia que toda requisicao marcada ativa ainda apontasse para o
  TD. O hardware pode concluir o TD e terminar o QH antes de o polling coletar
  a conclusao e baixar o estado ativo. A validacao agora aceita esse intervalo
  somente quando o TD ja nao possui `UHCI_TD_ACTIVE`; as demais combinacoes
  continuam rejeitadas. Boot, Stage 2 e assembly de interrupcoes permaneceram
  inalterados. A correcao aguarda gates e repeticao da matriz USB HID.

- VFS1: matriz final do perfil USB HID aprovada pelo usuario.
  Validada em: 2026-08-28 10:42 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Os gates da versao corrigida
  precederam a abertura no perfil USB conforme o fluxo operacional confirmado
  pelo usuario. `usb hid status` exibiu teclado e mouse em `READY`;
  `app inputtest` recebeu entrada USB, encerrou o PID 7 por `SIGINT` e devolveu
  o foco ao Shell; `vfs status` confirmou `READY`, pool `0/32`, sete processos,
  21 descritores, somente os fds 0-2 e zero falhas. `regcheck full` terminou em
  `OK` e `health check` nao publicou falha VFS, USB ou USB HID.

- VFS1: descritores e operacoes unificadas de I/O concluidos.
  Concluída em: 2026-08-28 10:42 (America/Sao_Paulo).
  As matrizes de host, QEMU padrao e USB HID foram aprovadas. App API 0.5,
  syscalls 4-7 e 14, isolamento por processo, stdio, `lseek`, limpeza,
  invariantes e diagnosticos ficam validados. O modo Simple foi excluido pelo
  usuario e nao integra o criterio de saida. Nenhuma divida tecnica foi aceita
  ou criada para concluir VFS1.

- VFS2: montagens, caminhos universais e diretorio de trabalho implementados.
  Concluída em: 2026-08-28 11:48 (America/Sao_Paulo).
  Foram implementados namespace limitado a quatro montagens sincronizadas com
  Storage, `/mnt` virtual, maior prefixo por componente, normalizacao absoluta
  e relativa, aliases legados, `cwd` isolado e herdavel, referencias e geracao
  de montagem, FAT12/FAT32 por Storage, App API 0.6, syscalls 15-16, pacotes
  0.3-0.6, comandos `mount`/`pwd`/`cd`, `app pathtest` ring 3 e diagnosticos.
  Bootloader, Stage 2 e assembly de interrupcoes permaneceram inalterados. A
  implementacao aguarda os gates e as matrizes QEMU padrao e USB MSC do
  usuario; nenhuma validacao executavel foi registrada nesta entrada e nenhuma
  divida tecnica foi criada.

- VFS2: primeira execucao funcional revelou bloqueio em `app pathtest`.
  Registrada em: 2026-08-28 12:03 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. A captura confirmou duas
  montagens, navegacao ate `/mnt/boot`, retorno relativo a `/mnt`, `vfs status`
  em `READY` e `vfs test` com 16/16 casos. Ao executar `app pathtest`, o
  sistema deixou de responder e nao devolveu o prompt.

- VFS2: consumo de stack na abertura ring 3 reduzido.
  Corrigida em: 2026-08-28 12:09 (America/Sao_Paulo).
  `file_open` mantinha um snapshot de lookup com tres caminhos de 256 bytes na
  stack da syscall enquanto Storage percorria FAT/LFN. O resultado agora e
  escrito diretamente no contexto global do arquivo; a resolucao deixou de
  duplicar caminho canonico e snapshot de montagem, e `chdir` usa um contexto
  compacto. O autoteste tambem moveu snapshots grandes para armazenamento
  estatico. `app pathtest` abre relativamente o fixture permanente
  `SHELL.BMP`. Storage passou a publicar uma consulta comum de caminho para
  distinguir arquivos e diretorios sem descritor, permitindo `cd` em
  subdiretorios reais. Reservas transitivas do pool possuem estado explicito
  para que as invariantes continuem validas durante a resolucao sem lock. A
  correcao aguarda gates e repeticao funcional pelo usuario.

- VFS2: repeticao de `app pathtest` revelou falha fatal persistente.
  Registrada em: 2026-08-28 12:21 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Na versao com os snapshots
  VFS reduzidos, a execucao apagou integralmente a tela do QEMU e nao devolveu
  o Shell. O comportamento confirma falha no caminho ring 3, sem evidencias de
  vazamento ou erro dos autotestes anteriores.

- VFS2: margem da kernel stack ring 3 ampliada e `pathtest` instrumentado.
  Corrigida em: 2026-08-28 12:21 (America/Sao_Paulo).
  Processos ring 3 agora reservam 8 KiB de kernel stack, dentro do intervalo de
  4 a 16 KiB ja suportado pelas guardas e metricas de processo. A margem cobre
  a cadeia aninhada syscall, VFS, Storage e FAT/LFN sem alterar a stack de
  usuario ou a ABI. O builtin publica marcos apos `chdir`, `getcwd`, abertura
  relativa e fechamento, permitindo identificar a ultima etapa concluida se
  houver nova falha. Bootloader, Stage 2 e assembly de interrupcoes permanecem
  inalterados. A correcao aguarda gates e repeticao funcional pelo usuario.

- VFS2: correcao de `app pathtest` validada pelo usuario.
  Validada em: 2026-08-28 12:32 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Os quatro marcos de `chdir`,
  `getcwd`, abertura relativa e fechamento terminaram em `OK`, e o PID 7
  encerrou com codigo 0. `vfs status` permaneceu `READY`, com pool `0/32`, sete
  processos, 21 descritores, somente os fds 0-2 no Shell e zero falhas. O
  `stack check` aprovou os sete processos sem margem baixa ou canario rompido,
  e `regcheck full` terminou em `OK`. A falha de tela preta fica corrigida e
  validada; VFS2 permanece aberta somente para o restante da matriz prevista.

- VFS2: matriz funcional do QEMU padrao aprovada pelo usuario.
  Validada em: 2026-08-28 12:35 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `appcheck` confirmou App API
  0.6, `getcwd`, `chdir`, restauracao do cwd, abertura relativa, leitura,
  `lseek`, fechamento e rejeicoes esperadas. `health check` nao publicou falha
  da VFS; os estados indisponiveis ou degradados exibidos pertencem a AC97,
  Media Player, USB, Update, App Store e USB HID ausentes neste perfil.
  `memcheck` aprovou heap, coalescencia, guardas PMM e diretorios de usuario, e
  `log check` aprovou os oito casos sem falhas. Com os resultados anteriores de
  namespace, `vfs test`, `app pathtest`, stack e `regcheck full`, a matriz do
  QEMU padrao fica concluida. Restam os testes de host e o perfil USB MSC.

- VFS2: matriz de testes do host aprovada pelo usuario.
  Validada em: 2026-08-28 12:39 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `make package-test` concluiu
  o autoteste do empacotador, fixtures da App Store, compatibilidade de API,
  validacoes de CRC/BPB/imagem e casos de imagem hibrida, LFN, alias e
  substituicao integralmente em `OK`. `make storage-fixtures-test` concluiu o
  autoteste das fixtures de Storage em `OK`. Resta somente a matriz USB MSC.

- VFS2: primeira abertura do perfil USB MSC nao enumerou o dispositivo.
  Registrada em: 2026-08-28 12:43 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage list` exibiu somente
  o disco ATA `ata0`, com os volumes `ata0raw` FAT12 e `ata0p1` FAT32; nenhum
  disco ou volume USB foi publicado. A execucao usou o alvo sem limpar o
  argumento padrao de teclado USB. A matriz historica e o contrato do perfil
  exigem `QEMU_USB_DEVICE_ARGS=` para deixar somente a fixture MSC no UHCI. A
  tentativa e inconclusiva e sera repetida com o comando canonico.

- VFS2: perfil USB MSC enumerado com o comando canonico.
  Registrada em: 2026-08-28 12:46 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage list` exibiu dois
  discos, seis volumes e duas montagens. O disco somente leitura
  `usb-ms-00:04.0-p1-a1-l0` publicou quatro volumes sem erro. A matriz VFS2
  usara literalmente `usb-ms-00:04.0-p1-a1-l0p1` FAT12 e
  `usb-ms-00:04.0-p1-a1-l0p4` FAT32, preservando os IDs fornecidos pelo
  inventario. A enumeracao esta aprovada; montagem, namespace e invariantes
  permanecem pendentes neste perfil.

- VFS2: primeira matriz de namespace USB revelou montagem recusada e colisao
  de separador.
  Registrada em: 2026-08-28 13:03 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `usb storage` permaneceu
  `READY`, sem resets ou erros. As solicitacoes de montagem dos IDs `p1` e
  `p4` retornaram `ERR_NOT_FOUND`, mantendo a VFS em duas montagens. A tentativa
  de navegar ao ponto USB registrou alias ausente e rejeitou o `:` do ID como
  separador invalido. `app pathtest`, `vfs status` e `regcheck full`
  permaneceram em `OK`; o inventario final conservou os quatro volumes USB em
  `DETECTED`, sem montagem ou residuo. A matriz USB MSC permanece aberta.

- VFS2: caminhos universais absolutos com `:` corrigidos.
  Corrigida em: 2026-08-28 13:03 (America/Sao_Paulo).
  O resolvedor agora interpreta aliases `system:`, `legacy:` e
  `<volume-id>:` somente quando a entrada nao comeca por `/` ou `\\`.
  Componentes de caminhos absolutos podem preservar o `:` presente nos IDs
  USB publicados por Storage. A invariante global passou a normalizar e
  comparar um ponto USB completo. A recusa de montagem sera repetida
  separadamente com os IDs copiados do inventario. Bootloader, Stage 2 e
  assembly de interrupcoes permanecem inalterados.

- VFS2: enumeracao USB MSC repetida na imagem com o resolvedor corrigido.
  Registrada em: 2026-08-28 13:11 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage list` voltou a
  publicar dois discos, seis volumes e duas montagens, sem erro no disco
  somente leitura `usb-ms-00:04.0-p1-a1-l0`. O volume FAT12 selecionado para
  isolar a montagem conserva o ID literal `usb-ms-00:04.0-p1-a1-l0p1` e o
  estado `DETECTED`. A consulta individual desse registro permanece pendente.

- VFS2: consulta individual do volume USB continuou sem localizar o ID.
  Registrada em: 2026-08-28 13:12 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage info` recebeu a
  representacao visual `usb-ms-00:04.0-p1-a1-l0p1`, mas `storage_find_volume`
  retornou `ERR_NOT_FOUND`. Os limites do input, dispatcher e parser comportam
  integralmente esse valor, sem truncamento estrutural. O caminho de erro foi
  instrumentado para publicar comprimento, registro mais proximo e tamanho do
  prefixo coincidente, permitindo localizar uma divergencia de byte sem
  aceitar aliases ambiguos ou montar outro volume.

- VFS2: consulta individual do volume USB aprovada na imagem instrumentada.
  Validada em: 2026-08-28 13:21 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage info
  usb-ms-00:04.0-p1-a1-l0p1` localizou o volume FAT12 `EP2FAT12A` em estado
  `DETECTED`, acesso somente leitura, geracao 1 e erro 0, alem do disco pai
  `usb-ms-00:04.0-p1-a1-l0` sem erros. O diagnostico de divergencia nao foi
  acionado porque o ID coincidiu integralmente. A falha anterior de consulta
  nao foi reproduzida; a montagem isolada desse mesmo registro e o proximo
  passo da matriz.

- VFS2: primeira montagem USB FAT12 publicada no namespace.
  Validada em: 2026-08-28 13:22 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage mount
  usb-ms-00:04.0-p1-a1-l0p1` montou `EP2FAT12A` somente leitura em RAM, e
  `mount` publicou `/mnt/usb-ms-00:04.0-p1-a1-l0p1` como FAT12 `RO`, geracao 2
  e zero referencias. O resultado funcional foi aprovado, mas o refresh
  registrou o aviso espurio `Indice de montagem nao encontrado` ao consultar
  uma posicao alem do ultimo registro como sentinela.

- VFS2: refresh de montagens deixou de consultar sentinela invalida.
  Corrigida em: 2026-08-28 13:22 (America/Sao_Paulo).
  `vfs_refresh_mounts()` agora obtem `mounted_count`, rejeita capacidade acima
  de quatro e consulta exatamente os registros publicados. Uma divergencia
  real entre contagem e inventario retorna erro estrutural com log; o fim
  normal nao chama mais `storage_get_mounted_at()` com indice inexistente. A
  correcao aguarda gates e repeticao da matriz USB MSC.

- VFS2: refresh corrigido e montagem USB FAT12 revalidados.
  Validada em: 2026-08-28 13:29 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Na imagem reconstruida,
  `storage mount usb-ms-00:04.0-p1-a1-l0p1` montou o volume somente leitura e
  `mount` publicou `/mnt/usb-ms-00:04.0-p1-a1-l0p1` como FAT12 `RO`, geracao 2
  e zero referencias. O aviso `Indice de montagem nao encontrado` nao voltou
  a ocorrer. A correcao do snapshot e a primeira montagem USB ficam aprovadas;
  restam FAT32, navegacao, ocupacao, limpeza e invariantes deste perfil.

- VFS2: montagem USB FAT32 e capacidade total da tabela aprovadas.
  Validada em: 2026-08-28 13:30 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `storage mount
  usb-ms-00:04.0-p1-a1-l0p4` montou `EP2FAT32` somente leitura, e `mount`
  publicou os dois volumes USB em `/mnt` com geracao 2, zero referencias e sem
  avisos. A tabela atingiu quatro de quatro entradas, preservando `/` e
  `/mnt/boot`. Montagem FAT32 e limite da tabela ficam aprovados; restam
  navegacao, ocupacao, limpeza e invariantes finais.

- VFS2: navegacao universal USB, referencias de cwd e ocupacao aprovadas.
  Validada em: 2026-08-28 13:47 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. O Shell navegou ate os
  diretorios reais `DOCS` dos volumes `usb-ms-00:04.0-p1-a1-l0p1` FAT12 e
  `usb-ms-00:04.0-p1-a1-l0p4` FAT32. `pwd` preservou os caminhos canonicos com
  `:`, e `mount` mostrou `refs=1` somente no volume que continha o `cwd`. A
  desmontagem do FAT12 ocupado foi recusada com codigo 7, e a referencia
  migrou para o FAT32 depois do segundo `cd`. Resolucao, normalizacao,
  referencias e protecao de volume ocupado ficam aprovadas neste perfil.

- VFS2: refresh durante a matriz USB deixou aliases obsoletos na VFS.
  Registrada em: 2026-08-28 14:02 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. `app pathtest`, `stack check`
  e `regcheck full` permaneceram em `OK`, mas o refresh associado aos
  diagnosticos reconstruiu Storage com apenas as duas montagens automaticas.
  Os volumes USB voltaram a `DETECTED`, enquanto `mount` e `vfs status`
  conservaram quatro aliases. As tentativas de unmount encontraram o alias na
  VFS e receberam codigo 7 do Storage porque os volumes ja nao estavam
  montados. USB MSC terminou `READY`, com zero resets, escritas e erros, e
  `memcheck` terminou em `OK`; a etapa nao pode ser encerrada com namespace
  divergente.

- VFS2: montagens manuais preservadas e VFS sincronizada em todo refresh.
  Corrigida em: 2026-08-28 14:02 (America/Sao_Paulo).
  `storage_refresh()` agora salva ate quatro IDs montados, reenumera e restaura
  os volumes ainda presentes antes de publicar a nova geracao. Os refreshes do
  kernel e do diagnostico sincronizam a VFS mesmo em resultado degradado e
  antes da reconstrucao do indice. `vfs_unmount_volume()` tambem remove alias
  obsoleto quando o volume ja estiver ausente ou desmontado no Storage. A
  correcao aguarda gates e repeticao final do perfil USB MSC. Bootloader,
  Stage 2 e assembly de interrupcoes permanecem inalterados.

- VFS2: repeticao final do perfil USB MSC aprovada.
  Validada em: 2026-08-28 14:13 (America/Sao_Paulo).
  O horario exato da execucao nao foi informado. Os volumes FAT12
  `usb-ms-00:04.0-p1-a1-l0p1` e FAT32 `usb-ms-00:04.0-p1-a1-l0p4` foram
  montados somente leitura e permaneceram `MOUNTED` no Storage e publicados
  na VFS depois de `health check`, totalizando quatro montagens. As duas
  desmontagens foram aprovadas, com reconstrucao do indice, e o estado final
  apresentou duas montagens automaticas, pool global `0/32`, sete processos,
  21 descritores, somente os fds 0-2 no processo atual e zero falhas VFS. O
  disco USB permaneceu somente leitura, sem escritas ou erros.

- VFS2: montagens, caminhos universais e diretorio de trabalho concluidos.
  Concluida em: 2026-08-28 14:13 (America/Sao_Paulo).
  Gates de host, build, QEMU padrao e perfil USB MSC foram aprovados. A App
  API 0.6, as syscalls 15/16, `cwd` por processo, caminhos absolutos e
  relativos, aliases legados, montagens FAT12/FAT32, referencias, refresh e
  limpeza do namespace atenderam ao criterio de saida. VFS2 foi encerrada sem
  divida tecnica. Bootloader, Stage 2 e assembly de interrupcoes permaneceram
  inalterados.

- VFS3: devfs, dispositivos unificados e App API 0.7 implementados.
  Implementada em: 2026-08-28 15:01 (America/Sao_Paulo).
  A VFS recebeu uma quinta montagem virtual fixa em `/dev`, independente das
  quatro vagas do Storage, e os nós `null`, `zero`, `tty`, `speaker` e `hda`.
  Foram implementados listagem universal, `ls [caminho]`, `cat` pela VFS,
  leitura bruta ATA somente leitura, `ioctl`, syscall 17, pacotes 0.3-0.7,
  `devcheck`, `app devtest` e `app inputtest tty`. Os diagnósticos VFS,
  App API, RegCheck e Health foram ampliados. A etapa permanece implementada,
  aguardando os gates e as matrizes QEMU padrão, USB HID e USB MSC compacto.
  Nenhuma dívida técnica foi criada. Bootloader, Stage 2 e assembly de
  interrupções permaneceram inalterados.

- VFS3: matriz funcional no QEMU padrao aprovada.
  Validada em: 2026-08-28 16:58 (America/Sao_Paulo).
  `mount`, `pwd`, listagens da raiz e de `/dev`, `cwd` em `/dev` e
  `cat /dev/null` foram aprovados. `devcheck` terminou com 9/9 casos e
  `vfs test` com 19/19; `vfs test foo` recusou corretamente o argumento.
  AppCheck confirmou App API 0.7 e syscall 17, e `app devtest` concluiu em
  ring 3. As tres execucoes de `app inputtest tty` terminaram por Enter,
  Ctrl+C/SIGINT e F12, sempre devolvendo o foco ao Shell. O estado final
  mostrou pool `0/32`, cinco dispositivos, somente os descritores padrao,
  RegCheck e MemCheck em `OK` e LogCheck com 8/8 casos. `health check` nao
  publicou falha VFS; estados degradados de hardware e servicos opcionais
  permaneceram os fallbacks esperados do perfil padrao.

- VFS3: perfil USB HID aprovado.
  Validada em: 2026-08-28 17:01 (America/Sao_Paulo).
  O teclado `usb-dev-00:04.0-p1-a1` e o mouse
  `usb-dev-00:04.0-p2-a2` permaneceram `READY`, ativos e sem erros ou
  descartes. `/dev` publicou os cinco nos e `devcheck` terminou com 9/9
  casos. As execucoes de `app inputtest tty` por Enter, Ctrl+C/SIGINT e F12
  devolveram o foco ao Shell. O estado final apresentou pool `0/32`, somente
  os descritores 0-2, cinco dispositivos, RegCheck em `OK` e nenhuma falha
  VFS no `health check`.

- VFS3: perfil USB MSC compacto aprovado.
  Validada em: 2026-08-28 17:12 (America/Sao_Paulo).
  O `storage list` publicou literalmente os volumes
  `usb-ms-00:04.0-p1-a1-l0p1`, `usb-ms-00:04.0-p1-a1-l0p2`,
  `usb-ms-00:04.0-p1-a1-l0p3` e `usb-ms-00:04.0-p1-a1-l0p4`. Os volumes
  `usb-ms-00:04.0-p1-a1-l0p1` (FAT12) e
  `usb-ms-00:04.0-p1-a1-l0p4` (FAT32) foram montados em modo somente leitura;
  `mount`, listagens dos volumes e `/dev` permaneceram corretos após os
  refreshes. As duas desmontagens foram aprovadas e o estado final voltou a
  duas montagens automáticas mais `/dev`, pool `0/32`, somente os descritores
  0-2 e Storage com duas montagens. Não houve escrita ou erro no disco USB.

- VFS3: entrega concluída e validada.
  Concluída em: 2026-08-28 17:12 (America/Sao_Paulo).
  Os gates de host e build da mesma versão foram confirmados pelo usuário, e
  as matrizes QEMU padrão, USB HID e USB MSC compacto atenderam ao critério de
  saída. DevFS, nós `null`, `zero`, `tty`, `speaker` e `hda`, listagem
  universal, `ioctl`, App API 0.7, syscall 17, sinais/cancelamento e limpeza
  de descritores foram validados sem alteração do bootloader, Stage 2 ou
  assembly de interrupções. VFS3 foi encerrada sem criar dívida técnica.

- VFS4: implementação concluída.
  Concluída em: 2026-08-28 17:58 (America/Sao_Paulo).
  Foram implementados pipes anônimos com buffer circular de 4096 bytes,
  pool de oito entradas, Wait Queues, EOF, backpressure e limpeza de
  endpoints; App API 0.8, syscall 18, compatibilidade de pacotes 0.3 a 0.8,
  pipeline cooperativo de até quatro estágios, `grep`, `>`, `>>`, sink de
  stdout e escrita atômica de redirecionamento. `vfs test`, `pipetest`, os
  pipelines, os destinos FAT e a matriz ring 3 permanecem aguardando os gates
  e a validação QEMU pelo usuário. Não houve alteração em
  `src/boot/boot.asm` e nenhuma dívida técnica foi criada.

- VFS4: revisão estática da implementação concluída.
  Concluída em: 2026-08-28 18:01 (America/Sao_Paulo).
  `git diff --check` não encontrou erros; os objetos e dependências dos novos
  módulos estão presentes no Makefile, os símbolos da App API 0.8 e syscall 18
  estão integrados e `src/boot/boot.asm` permaneceu sem alterações. Nenhum
  build, teste ou execução QEMU foi realizado pelo agente.

- VFS4: correção de conformidade do `q3check` concluída.
  Concluída em: 2026-08-28 18:05 (America/Sao_Paulo).
  Foram adicionados logs de erro e aviso às funções novas do VFS e do parser
  de pipeline apontadas pelo gate. A revisão estática do diff permaneceu limpa;
  o `q3check` deve ser repetido pelo usuário.

- VFS4: correção do include de memória após erro de compilação.
  Concluída em: 2026-08-28 18:08 (America/Sao_Paulo).
  `src/fs/vfs.c` passou a incluir `core/memory.h`, tornando explícitas as
  declarações de `kmalloc` e `kfree` usadas pelo redirecionamento VFS.
  A compilação deve ser repetida pelo usuário.
- VFS4: `make q3check` e verificação do Terminus validados pelo usuário.
  Concluída em: 2026-08-28 18:09 (America/Sao_Paulo).
  `funcoes_falhaveis`, `contratos_publicos`, `registro_metricas`,
  `confianca_zupd`, `confianca_as5`, `whitespace`, `boot_protegido` e o
  `Terminus Font --check` retornaram OK.
- VFS4: build completo e `make run` abertos pelo usuário.
  Concluída em: 2026-08-28 18:13 (America/Sao_Paulo).
  Os gates prévios foram concluídos e o sistema iniciou no QEMU; a matriz
  funcional de pipes, pipeline e redirecionamento permanece em validação.
- VFS4: correção preventiva da stack dos workers do pipeline concluída.
  Concluída em: 2026-08-28 18:20 (America/Sao_Paulo).
  A execução de `ls > lista.txt` revelou corrupção do heap após percorrer
  VFS, Storage e FAT/LFN em uma thread com stack de 4 KiB. `thread_create()`
  passou a reservar quatro páginas (16 KiB), com o contrato técnico atualizado.
  A matriz funcional deve ser repetida após `make q3check` e `make clean && make`.
- VFS4: pipelines e redirecionamentos principais validados no QEMU pelo usuário.
  Concluída em: 2026-08-28 18:33 (America/Sao_Paulo).
  `vfs test` retornou `20/20` e `Pipes: OK`; `pipetest`, `appcheck` com
  syscall 18, `procs | grep shell`, `echo texto | grep texto`,
  `ls > lista.txt`, `cat lista.txt` e `echo segundo >> lista.txt` funcionaram.
  O conteúdo anexado foi confirmado com `cat`; permanecem pendentes as
  sintaxes inválidas, destinos recusados e a confirmação final de limpeza.
- VFS4: sintaxes inválidas e destinos não graváveis validados no QEMU pelo usuário.
  Concluída em: 2026-08-28 18:36 (America/Sao_Paulo).
  `echo | grep texto`, `echo texto > a > b`, `ls >`, `date | grep shell`,
  `cat arquivo-inexistente-vfs4.txt`, `ls > /` e `ls > /dev/null` retornaram
  rejeções controladas, sem corrupção do heap ou erro de limpeza observado.
  Permanecem pendentes apenas o destino FAT somente leitura, o destino FAT
  inválido e a confirmação final explícita de descritores e filas residuais.
- VFS4: montagens, estado VFS e filas residuais inspecionados no QEMU pelo usuário.
  Concluída em: 2026-08-28 18:39 (America/Sao_Paulo).
  A montagem `/mnt/boot` foi confirmada como FAT12 somente leitura; o estado
  VFS mostrou `pipes ativos/capacidade: 0/8` e o processo atual manteve apenas
  `fd=0..2`. `wqinfo` não mostrou filas `VFS-pipe` nem waiters residuais.
  O teste de escrita no volume somente leitura permanece pendente.
- VFS4: matriz funcional completa validada no QEMU pelo usuário.
  Concluída em: 2026-08-28 18:40 (America/Sao_Paulo).
  `ls > /mnt/boot/vfs4-readonly.txt` foi recusado corretamente com
  `ERR_UNAVAILABLE` (código 9) no volume FAT12 somente leitura. Com os gates,
  build, pipelines, redirecionamentos, recusas controladas e inspeções de
  `vfs status`/`wqinfo` já registrados, a matriz VFS4 foi encerrada sem
  descritores, pipes ou waiters residuais observados e sem alterar o bootloader.

- MM1: alocador SLAB/SLUB e contratos implementados.
  Concluída em: 2026-08-28 19:17 (America/Sao_Paulo).
  Foram criados `slab.h` e `slab.c` com metadados estaticos, limites de caches
  e slabs, listas `full`/`partial`/`empty`, bitmap, freelist, estatisticas,
  validacao de posse, deteccao de ponteiro invalido e double free, destruicao
  protegida e autoteste. A inicializacao foi posicionada depois de
  `memory_init()` e antes da App API, sem alterar `src/boot/boot.asm`.

- MM1: migracao estrutural e diagnosticos implementados.
  Concluída em: 2026-08-28 19:17 (America/Sao_Paulo).
  `process_t`, `thread_t`, `file_t`, `vnode_t` e `net_packet_t` passaram a usar
  caches dedicados; stacks continuam no `kmalloc`. `health`, `memcheck`,
  `schedcheck`, `regcheck`, `vfs_validate_state()`, `slabinfo` e `slabtest`
  foram integrados, com liberacao dos objetos nos caminhos de erro e de
  destruicao.

- MM1: revisao estatica e documentacao atualizadas.
  Concluída em: 2026-08-28 19:17 (America/Sao_Paulo).
  Makefile, contratos publicos, documentacao de memoria, kernel, processos,
  VFS, Shell, Roadmap 11 e roadmap geral foram atualizados. A revisao
  `git diff --check` nao encontrou erros de whitespace. Nenhum build, teste ou
  QEMU foi executado pelo agente; gates e matriz funcional permanecem sob
  responsabilidade do usuario.

- MM1: estado de falha de inicializacao publicado no kernel.
  Concluída em: 2026-08-28 19:20 (America/Sao_Paulo).
  O kernel passou a interromper o boot quando o processo Idle nao pode ser
  criado e a publicar o scheduler de threads como indisponivel quando o cache
  `thread` falha. `thread_is_ready()` preserva a assinatura legada de
  `thread_init()` e torna o estado observavel sem confundir tabela vazia com
  falha de inicializacao.

- MM1: auditoria final do alocador e caminhos de liberacao concluida.
  Concluída em: 2026-08-28 19:28 (America/Sao_Paulo).
  A validacao estrutural das listas `full`/`partial`/`empty` foi reforcada e
  a transicao parcial-para-parcial deixou de duplicar slabs nas listas. O
  `slabtest` passou a verificar a devolucao das paginas do PMM apos destruir
  o cache. A revisao estatica permanece sem erros de whitespace; nenhum build,
  teste executavel ou QEMU foi realizado pelo agente.

- MM1: caminho de erro de processo ring 3 corrigido.
  Concluída em: 2026-08-28 19:29 (America/Sao_Paulo).
  A falha na reserva de PID durante a inicializacao agora descarta o objeto
  `process_t` do cache e libera seus recursos parciais, preservando a limpeza
  completa da migracao estrutural.

- MM1: correcao de compilacao do Task Manager concluida.
  Concluída em: 2026-08-28 20:03 (America/Sao_Paulo).
  Os indices de uso de CPU passaram a ser resolvidos pela tabela de ponteiros
  de processos, removendo aritmetica invalida entre `process_t*` e
  `process_t**`. O loop de ordenacao recebeu tipos sem signedness conflitante
  e o helper grafico nao utilizado foi removido. A revisao estatica nao foi
  substituida por novo build; a compilacao deve ser repetida pelo usuario.

- MM1: regressao do `vfs test` corrigida.
  Concluída em: 2026-08-28 22:23 (America/Sao_Paulo).
  A tabela de descritores isolada usada pelo autoteste agora e explicitamente
  zerada antes de `vfs_fd_table_init()`, mantendo a protecao contra
  reinicializacao de tabelas ja ativas sem interpretar lixo de stack como
  estado valido. O `git diff --check` permaneceu sem erros; o autoteste deve
  ser repetido pelo usuario.

- MM1: `vfs test` validado pelo usuario.
  Horario da execucao: nao informado na captura recebida.
  O teste retornou `VFS Test: OK`, `Casos aprovados: 20/20` e `Pipes: OK`.

- MM1: rede Ethernet e diagnosticos pos-RX/TX validados pelo usuario.
  Horario da execucao: nao informado nas capturas recebidas.
  A interface `net-pci-00:03.0` confirmou TX Ethernet, `net check qemu`
  retornou `OK` para ARP, IPv4, ICMP, polling e invariantes, e a consulta
  Ethernet mostrou frames RX/TX aceitos sem descartes. `slabinfo` mostrou
  `net_packet` em `0/8` ativos e sem falhas; `memcheck` e `regcheck full`
  permaneceram em `OK`.

- Antigo Roadmap 17: migração gradual do ZephyrOS para Rust documentada.
  Concluída em: 2026-08-28 22:51 (America/Sao_Paulo).
  Criado o roadmap independente `pos-1.0.0-migracao-rust.md`, com marco
  pós-1.0.0, fases RUST0-RUST7, mapa de módulos candidatos, contrato C/Rust,
  ordem de migração, critérios de validação e componentes que permanecem em
  C/Assembly. Os índices e o roadmap geral receberam somente referências para
  o novo documento; o Roadmap 11 não foi alterado.

- MM1: encerramento formal da etapa confirmado pelo usuário.
  Concluída em: 2026-08-28 22:58 (America/Sao_Paulo).
  Após a confirmação do usuário de que os gates e comandos de validação da
  MM1 foram executados, a etapa foi marcada como concluída nos roadmaps. A
  próxima etapa de implementação é a MM2, de áreas de memória virtual.

- MM2: áreas de memória virtual implementadas.
  Concluída em: 2026-08-29 00:08 (America/Sao_Paulo).
  Criados `vm_area_t`, a lista ordenada por processo e o registro automático
  das regiões fixas de código, dados, lançamento e stack. `mmap` anônimo
  privado usa first-fit, aloca e zera páginas imediatamente; `munmap` libera
  frames, invalida a TLB quando necessário e suporta divisão de VMA. O paging
  evita double free, atualiza `active_pages` e remove tabelas vazias.

- MM2: App API 0.9, Shell e fixture ring 3 implementados.
  Concluída em: 2026-08-29 00:08 (America/Sao_Paulo).
  Syscalls 19 e 20, `vmamap <pid>`, aceitação de pacotes 0.3-0.9 e o
  `appcheck` com alocação, escrita/leitura, arredondamento, erros, divisão,
  regiões fixas e limpeza foram integrados. `git diff --check` não encontrou
  erros; os gates `make q3check`, `make clean && make`, `make run`,
  `appcheck`, `vmamap`, `memcheck`, `schedcheck`, `regcheck full` e
  `health check` aguardam validação do usuário.

- MM2: documentação complementar e revisão estática finais.
  Concluída em: 2026-08-29 00:09 (America/Sao_Paulo).
  Roadmap principal, índices, capítulos de memória/kernel, atalhos, contrato
  público, App API, pacotes e Roadmap 11 foram alinhados ao contrato 0.9.
  A revisão final com `git diff --check` permaneceu sem erros. Nenhum build,
  teste executável ou QEMU foi realizado pelo agente.

- MM2: correção de compilação do comando `vmamap`.
  Concluída em: 2026-08-29 09:51 (America/Sao_Paulo).
  A chamada do helper de impressão de permissões foi alinhada ao símbolo
  `cmd_vmamap_print_permissions`, eliminando a declaração implícita e o
  warning de função não utilizada. A recompilação permanece sob
  responsabilidade do usuário; nenhum build foi executado pelo agente.

- MM2: inicialização da imagem no QEMU confirmada pelo usuário.
  Concluída em: 2026-08-29 09:56 (America/Sao_Paulo).
  Após a correção de compilação do `vmamap`, o usuário informou que o sistema
  abriu normalmente com `make run` e alcançou o Shell. Os testes funcionais
  específicos da MM2 permanecem em execução.

- MM2: validação funcional QEMU apresentada pelo usuário.
  Horário exato não informado.
  `appcheck` confirmou `vma_ring3_inicio` e `vma_ring3_conclusao` com `OK`,
  incluindo os casos negativos de tamanho, overflow, flags e intervalos
  inválidos. `memcheck`, `schedcheck` e `regcheck full` terminaram com `OK`.
  `health check` exibiu apenas estados `DISABLED` ou `DEGRADED` esperados do
  perfil QEMU, sem falha relacionada à memória virtual.

- MM2: inspeção manual do `vmamap` tentada pelo usuário.
  Horário exato não informado.
  `procs` mostrou somente os PIDs 0 a 6, todos pertencentes a processos do
  sistema ou ao Shell, sem processo ring 3 ativo para fornecer um mapa VMA.
  A inspeção positiva do comando permanece pendente; o caminho negativo pode
  ser exercitado com um PID de processo sem espaço ring 3.

- MM2: caminho negativo do `vmamap` validado pelo usuário.
  Horário exato não informado.
  `vmamap 4` recusou corretamente o PID do Shell com a mensagem
  `Erro: processo sem mapa ring 3.`

- MM2: etapa encerrada após validação funcional no QEMU.
  Concluída em: 2026-08-29 10:02 (America/Sao_Paulo).
  O usuário confirmou `make run`, `appcheck`, `memcheck`, `schedcheck`,
  `regcheck full`, `health check` e o caminho negativo de `vmamap`. A MM2 foi
  marcada como concluída nos roadmaps; a listagem positiva de um processo ring
  3 vivo não foi possível porque nenhum processo desse tipo permaneceu ativo.

- Antigo Roadmap 17: critérios da migração gradual para Rust atualizados.
  Concluída em: 2026-08-29 10:22 (America/Sao_Paulo).
  Foram documentadas as camadas `bindings C → helpers/abstrações seguras →
  módulos finais`, a política de `unsafe`, ownership e allocators existentes,
  métricas objetivas, rollback, validação própria do target i686 e a permissão
  de alterar o boot somente com comunicação explícita e verificação dos 512
  bytes. Nenhum código Rust foi migrado e nenhum build foi executado pelo
  agente.

- Antigo Roadmap 17: ordem de migração e regras de otimização detalhadas.
  Concluída em: 2026-08-29 10:28 (America/Sao_Paulo).
  Foram incluídos candidatos concretos do ZephyrOS (`vfs_path.c`,
  validadores de pacotes, `file_index.c`, catálogo, algoritmo de intervalos
  da `vma.c`, buffers/filas, PCI/PHY e SDK ring 3), além de restrições para
  IRQ/hot paths, pools fixos, FFI, `unsafe`, layout e métricas. Nenhum código
  Rust foi migrado e nenhum build foi executado pelo agente.

- AGENTS.md: Regra #8 revisada para um contrato flexível de drivers.
  Concluída em: 2026-08-29 10:31 (America/Sao_Paulo).
  O modelo obrigatório foi substituído por requisitos de ciclo de vida,
  estados, capacidades, rollback de inicialização, contratos de erro,
  ownership, IRQ/DMA, concorrência, timeouts, fallback e uso restrito de
  `panic`, preservando compatibilidade com drivers legados.

- AGENTS.md: Regra #9 revisada para um contrato flexível de módulos do Shell.
  Concluída em: 2026-08-29 10:35 (America/Sao_Paulo).
  O template obrigatório foi substituído por requisitos de integração,
  dispatcher, separação de responsabilidades, modos Simple/Classic, cenas,
  `shell_job`, cancelamento, reentrada, restauração de contexto, limpeza e
  validação, preservando a organização atual do Shell.

- AGENTS.md: regras #10, #11, #12, #13 e #15 revisadas.
  Concluída em: 2026-08-29 10:40 (America/Sao_Paulo).
  O Makefile passou a ser tratado por integração reproduzível e não por
  template rígido; a documentação foi separada por finalidade; qualidade,
  compatibilidade e checklist foram alinhados à validação executável do
  usuário; a Regra #13 passou a aceitar caminhos observáveis existentes; e a
  política Simple/Classic/Modern foi delimitada ao escopo de interface.

- MM3: implementação de demand paging e page faults para VMAs ring 3.
  Concluída em: 2026-08-29 11:16 (America/Sao_Paulo).
  O loader passou a preservar backing kernel-owned de código, dados e
  lançamento, sem alocar páginas de usuário na criação suspensa. VMAs fixas e
  `mmap` anônimo são materializados no primeiro acesso; cópias de buffers de
  usuário materializam páginas lazy; `munmap` libera somente páginas
  residentes; faults válidas retornam ao usuário e faults inválidas isolam o
  processo com `SIGSEGV`. Foram adicionados contadores cumulativos, o comando
  `pagefault status` e fixtures MM3 ao `appcheck`, incluindo reserva sem
  páginas, fault fora de VMA e limpeza de recursos. A marcação da MM3 no
  roadmap permanece pendente da validação funcional do usuário; nenhum build,
  teste ou QEMU foi executado pelo agente.

- MM3: correção do gate `q3check` para contratos públicos.
  Concluída em: 2026-08-29 11:23 (America/Sao_Paulo).
  O catálogo passou a associar `src/include/memory/vma.h` a
  `docs/06-memoria/memoria.md`, e `docs/07-processos/processos.md` passou a
  documentar os campos append-only de `process_t` introduzidos pela MM3. A
  execução do `make q3check` permanece sob responsabilidade do usuário.

- MM3: validação funcional confirmada pelo usuário no QEMU.
  Concluída em: 2026-08-29 11:31 (America/Sao_Paulo).
  As capturas registraram `pagefault status` inicial com `tratadas=0` e
  `invalidas=0`; `appcheck` confirmou `vma_lazy_reserva`, materialização de
  faults válidas, fault fora de VMA isolada e limpeza, todos com `OK`. Em
  seguida, `usertest` terminou com sucesso, `usertest fault` foi isolado, e os
  contadores chegaram a `tratadas=11` e `invalidas=2`. `memcheck`,
  `schedcheck` e `regcheck full` também terminaram com `OK`. `health check`
  manteve apenas estados de hardware/serviços degradados ou desabilitados e
  registrou uma saturação independente do pipeline de entrada do mouse.

- MM3: gate `q3check` confirmado pelo usuário.
  Concluída em: 2026-08-29 11:32 (America/Sao_Paulo).
  O usuário informou que executou `make q3check` após as correções de contrato
  e o resultado terminou sem erros.

- MM4: implementação de métricas de fragmentação física, zonas exclusivas e
  monitoramento no Shell, `memcheck` e Task Manager.
  Implementação registrada em: 2026-08-29 12:08:02 -03:00
  (America/Sao_Paulo).
  O PMM passou a classificar ownership por página para `KERNEL`, `HEAP`,
  `SLAB`, `PROCESS`, `BUFFER` e `FREE`, com contagem de runs livres, maior
  run, páginas isoladas e fragmentação por maior bloco. `mem detailed`,
  `memcheck` e as abas de memória Simple/Classic foram integrados; o Task
  Manager limita a coleta a um snapshot por segundo. Contratos e documentação
  canônica foram atualizados. A MM4 continua pendente da execução funcional
  pelo usuário; o agente não executou build, testes ou QEMU.

- MM4: ajuste visual da aba Memória do Task Manager Classic.
  Correção registrada em: 2026-08-29 12:25:10 -03:00
  (America/Sao_Paulo).
  A fragmentação foi reposicionada para não sobrepor o resumo ATA e o
  framebuffer na largura padrão da janela. A validação funcional da captura
  corrigida permanece pendente do usuário.

- MM4: validação funcional confirmada pelo usuário no QEMU.
  Concluída em: 2026-08-29 12:30:54 -03:00 (America/Sao_Paulo).
  As capturas confirmaram a soma das zonas, `memoria_detalhada OK`, os
  diagnósticos de SLAB, aplicações, processos de usuário, scheduler e registro,
  além da aba Memoria do Task Manager Classic com zonas, runs, maior bloco,
  páginas isoladas, fragmentação e gráficos. O refinamento visual de
  espaçamento permanece aceito como melhoria futura.

- Revisão dos roadmaps futuros após a conclusão da MM4.
  Registrada em: 2026-08-29 12:50:06 -03:00 (America/Sao_Paulo).
  O Roadmap 13 passou a começar por BLK0, separando BIO, requisição enfileirada,
  ownership, conclusão e durabilidade; o cache de blocos foi separado de um
  page cache completo. Os Roadmaps 14, 15, 16 e 17 receberam contratos de
  ownership/lifetime, separação `/proc`/`/sys`, escopo seguro de ACPI e gates
  reproduzíveis para Rust. O Roadmap 09 não foi reaberto: R5-R9 permanecem como
  melhorias futuras mapeadas e não bloqueiam a BLK1.

- BLK0: contrato de I/O e adaptador síncrono implementados.
  Implementação registrada em: 2026-08-29 13:41:03 -03:00
  (America/Sao_Paulo).
  `bio_request_t`, estados de requisição, capacidades FLUSH/FUA, limite de
  transferência, callbacks opcionais e `block_submit_sync()` foram adicionados
  de forma compatível. `block_read()` e `block_write()` agora usam o mesmo
  caminho de submissão; ATA e USB MSC continuam sem FLUSH/FUA. O backend
  determinístico de `block_self_test()` cobre sucesso, erro, limites,
  somente-leitura, capacidades indisponíveis, callback de conclusão e preserva
  o inventário real. `regcheck full` passou a reportar `camada_bloco`.
  O agente não executou build, testes ou QEMU; a validação funcional e a
  marcação final do BLK0 permanecem pendentes da confirmação do usuário.

- BLK0: evidência parcial de validação enviada pelo usuário no QEMU.
  Concluída em: 2026-08-29 13:58:10 -03:00 (America/Sao_Paulo).
  `regcheck full` reportou `camada_bloco OK` e `RegCheck: OK`, incluindo as
  rejeições esperadas de limites, LBA inválido, FLUSH/FUA indisponíveis,
  somente-leitura e erro propagado. `storage list` confirmou o dispositivo ATA
  e os volumes FAT12/FAT32 montados, sem erro no inventário real. A captura não
  incluiu `storage check`, `appcheck` ou `health check`; portanto, esses
  comandos não são declarados como validados e a marcação final do BLK0 fica
  pendente.

- Correção do estado de aceite do BLK0.
  Registrada em: 2026-08-29 14:00:35 -03:00 (America/Sao_Paulo).
  Os IDs para a próxima validação são `ata0raw` e `ata0p1`. O Roadmap 13 foi
  mantido pendente até a execução dos comandos de aceite restantes e o envio
  dos respectivos resultados pelo usuário.

- BLK0: verificação de volumes confirmada parcialmente pelo usuário no QEMU.
  Concluída em: 2026-08-29 14:01:39 -03:00 (America/Sao_Paulo).
  `storage check ata0p1` retornou `Volume FAT32 consistente.`. A rejeição de
  `storage check ata0raw` foi esperada, pois o diagnóstico exige um volume
  FAT32 montado e `ata0raw` é o volume FAT12 legado. O aceite final do BLK0
  permanece pendente dos demais comandos da matriz.

- BLK0: validação funcional concluída pelo usuário no QEMU.
  Concluída em: 2026-08-29 14:02:55 -03:00 (America/Sao_Paulo).
  Além de `regcheck full` com `camada_bloco OK` e `RegCheck: OK`, o usuário
  confirmou `storage list`, `storage check ata0p1` com `Volume FAT32 consistente`,
  `appcheck` concluído e `health check` executado. As mensagens de erro do
  `appcheck` pertencem às fixtures negativas esperadas; os estados opcionais
  `DISABLED`/`DEGRADED` do hardware ausente no QEMU não afetam o BLK0. O BLK0
  foi marcado como concluído; a próxima etapa é o BLK1.

- BLK1: implementacao da fila unificada de requisicoes de bloco.
  Registrada em: 2026-08-29 14:32:37 -03:00 (America/Sao_Paulo).
  A camada agora possui fila FIFO estatica de 32 entradas, submissao
  assincrona/sincrona, despacho pelo `Zephyr kworker`, cancelamento de BIOs
  enfileirados, fusao conservadora de buffers contiguos, metricas cumulativas e
  o comando `blkstat`. ATA e USB MSC publicam o callback fisico novo, mantendo
  os callbacks legados e sem anunciar FLUSH/FUA. `regcheck` foi ampliado no
  autoteste da camada para FIFO, fusao, overflow, cancelamento, erro propagado,
  callback unico e inventario preservado.
  O agente nao executou build, testes ou QEMU; a validacao funcional do BLK1 e
  a marcacao final no roadmap permanecem pendentes da confirmacao do usuario.

- BLK1: correcao do link freestanding apos diagnostico do usuario.
  Registrada em: 2026-08-29 14:54:48 -03:00 (America/Sao_Paulo).
  `block_rate()` deixou de usar divisao direta de `uint64_t`, que gerava a
  referencia ausente a `__udivdi3`; o calculo agora usa divisao por palavras
  de 32 bits com a mesma saturacao da taxa. O agente nao executou build, testes
  ou QEMU; a confirmacao da compilacao permanece pendente do usuario.

- BLK1: validacao funcional concluida pelo usuario no QEMU.
  Concluida em: 2026-08-29 15:01:30 -03:00 (America/Sao_Paulo).
  `blkstat teste` recusou os argumentos e exibiu o uso. Antes do `regcheck`,
  `blkstat` mostrou fila 0/32, sem falhas ou cancelamentos. O `regcheck full`
  exibiu `camada_bloco OK` e `RegCheck: OK`. Depois do autoteste, `blkstat`
  mostrou fila 0/32, pico 32, voo 0, 33 cancelamentos, uma fusao e ultimo
  erro 10 (`ERR_CANCELLED`), com os contadores consistentes. `storage list`
  manteve dois discos e dois volumes montados; `storage check ata0p1`
  confirmou `Volume FAT32 consistente`. `appcheck` e `health check` tambem
  foram executados; as falhas exibidas pertencem as fixtures negativas e aos
  componentes opcionais indisponiveis no QEMU. O BLK1 foi marcado como
  concluido no roadmap.

- AppCheck compacto: implementacao registrada.
  Implementada em: 2026-08-29 15:18:16 -03:00 (America/Sao_Paulo).
  Foi adicionado `appcheck compact` ao dispatcher existente, com a mesma suite
  do `appcheck` detalhado, resumo por fase, classificacao das fixtures
  negativas, falhas reais limitadas por nome/codigo e preservacao do job
  cooperativo, cancelamento, foco e limpeza. `appcheck` sem argumentos manteve
  a saida legada. A validacao funcional nos gates e no QEMU permanece pendente
  da execucao e confirmacao do usuario.

- AppCheck compacto: correcao da finalizacao assincrona.
  Corrigida em: 2026-08-29 15:28:00 -03:00 (America/Sao_Paulo).
  O resumo nao aparecia apos a execucao assincrona porque a identificacao do
  comando fica em `shell_job_context_t.arguments`, enquanto `command` contem
  apenas o nome da definicao do job. O reconhecimento foi ajustado sem mudar
  o fluxo do `appcheck` detalhado; a validacao funcional deve ser repetida.

- AppCheck compacto: validacao funcional confirmada pelo usuario no QEMU.
  Horario exato nao informado.
  `appcheck compact` executou a mesma suite e exibiu as oito fases como `OK`,
  com `falhas=0` e `resultado=OK`. `appcheck compact extra` e
  `appcheck invalido` foram rejeitados com `Uso: appcheck [compact]` sem
  iniciar o job. `memcheck`, `schedcheck`, `regcheck full` e `health check`
  tambem foram executados; os estados `DISABLED`/`DEGRADED` exibidos no
  `health` correspondem aos componentes opcionais ausentes no perfil QEMU.
  A funcionalidade foi validada pelo usuario.

- BLK2: implementação do cache estático de leitura registrada.
  Implementada em: 2026-08-29 16:02:27 -03:00 (America/Sao_Paulo).
  Foram adicionados 64 blocos de 512 bytes, hash, LRU, estados de entrada,
  wait queues, invalidação atômica, bypass sem vítima elegível, integração ao
  `block_read()`, estatísticas, `cachestat`, `cache clear` e autoteste com
  dispositivos mock. Escritas continuam diretas e não há writeback no BLK2.
  O agente não executou build, testes ou QEMU; a validação funcional e a
  confirmação para marcar o BLK2 no resumo do roadmap permanecem pendentes.

- BLK2: validação funcional confirmada pelo usuário no QEMU.
  Confirmada em: 2026-08-29 16:22:36 -03:00 (America/Sao_Paulo).
  `cachestat teste`, `cache` e `cache clear extra` foram recusados com uso,
  sem alteração indevida; `cache clear` removeu as 64 entradas elegíveis.
  `storage check ata0p1` permaneceu consistente, `regcheck full` publicou
  `camada_bloco OK` e `RegCheck: OK`, e `appcheck compact` concluiu as oito
  fases com `falhas=0` e `resultado=OK`. O `health check` manteve apenas os
  estados opcionais `DISABLED`/`DEGRADED` já esperados no perfil QEMU.
  A confirmação funcional permite marcar o BLK2 como concluído no roadmap.

- BLK3: validacao funcional de writeback e sincronizacao explicita confirmada
  apos a correcao da metrica de acerto.
  Confirmada em: 2026-08-29 17:49:40 -03:00 (America/Sao_Paulo).
  Foram adicionados escrita em cache com faixa parcial, estados dirty/writeback,
  writeback periodico limitado, sincronizacao por dispositivo e global,
  durabilidade READY/DEGRADED/ERROR, FLUSH CACHE condicional no ATA,
  `vfs_fsync`, `vfs_sync`, syscalls 21/22 e o comando `sync`. O agente nao
  executou build, testes ou QEMU. O usuario confirmou no QEMU que `sync`
  retornou `OK` com `DEGRADED` esperado sem FLUSH, `storage check ata0p1`
  permaneceu consistente, `regcheck full` publicou `camada_bloco OK` e
  `RegCheck: OK`, `blkstat` terminou com fila vazia e `appcheck compact`
  concluiu com `falhas=0` e `resultado=OK`. O `cachestat` passou a exibir
  `acerto=99%`; o `health check` reportou somente a degradacao esperada de
  durabilidade sem flush fisico. A confirmacao funcional permite marcar o
  BLK3 como concluido no roadmap.

- BLK4: implementacao de resiliencia, failpoints e desligamento seguro.
  Implementada em: 2026-08-29 18:17:42 -03:00 (America/Sao_Paulo).
  Foram adicionados failpoints privados one-shot para submissao, execucao,
  conclusao, FLUSH, eviction e writeback, com autotestes de callback unico,
  fila drenada, hash/LRU, pins, preservacao de `DIRTY`, retry por novo sync e
  inventario inalterado. O comando cooperativo `blkcheck` usa apenas as
  fixtures `ata1p1`/`ata1p4` do `run-storage`, monta-as quando necessario,
  valida SHA-256 e remove `BLK4CHK.BIN` inclusive na drenagem de cancelamento.
  As montagens controladas permanecem disponiveis apos sucesso para a matriz
  `storage check`; os caminhos normais de
  shutdown agora exigem `power_shutdown_prepare()`; erro de writeback/FLUSH
  mantem o sistema ativo e ausencia de FLUSH continua `OK` degradado. Nao foi
  adicionado journaling, reboot simulado ou interface publica de failpoint; a
  matriz ZUPD pos-reboot permanece separada. O agente nao executou build,
  testes ou QEMU. A validacao funcional e a marcacao final do BLK4 no resumo
  do roadmap permanecem pendentes da confirmacao do usuario.

- BLK4: correcao do diagnostico apos a primeira matriz funcional.
  Identificada em: 2026-08-29 19:05:02 -03:00 (America/Sao_Paulo).
  A montagem automatica das fixtures foi confirmada pelo usuario: `ata1p1`
  ficou FAT12 somente-leitura e `ata1p4` FAT32 gravavel. A execucao avancou
  ate a validacao FAT12, que rejeitou um SHA-256 incorreto no diagnostico; a
  constante foi corrigida para os bytes realmente gerados por
  `tools/storage_fixtures.py`, cujo marcador e `ata1p1`, nao o label
  `EP2FAT12A`. A confirmacao funcional completa do BLK4 permanecia pendente
  naquele ponto da investigacao.

- BLK4: validacao funcional confirmada pelo usuario no QEMU.
  Confirmada em: 2026-08-29 19:12:33 -03:00 (America/Sao_Paulo).
  O `blkcheck` concluiu novamente com `baseline OK`, `failpoints OK`,
  `cache OK`, `FAT12 OK`, `FAT32 OK`, `shutdown OK` e `resultado OK`.
  O usuario confirmou que a segunda execucao nao travou o sistema. A etapa
  BLK4 foi marcada como concluida no resumo do roadmap; a matriz ZUPD de
  recuperacao pos-reboot continua sendo validacao separada.

- NET2: correcao do autoteste de sockets genericos apos a primeira matriz
  funcional. Identificada em: 2026-08-29 21:45:59 -03:00
  (America/Sao_Paulo). A conclusao de SKB retirado da fila UNIX aceitava
  apenas `RX` e `IN_FLIGHT`, embora a entrega local use `QUEUED`; isso
  recusava a conclusao, impedia a liberacao e deixava um buffer ativo.
  O caminho agora aceita `QUEUED -> DELIVERED`, e o fixture usa
  `SOCKET_QUEUE_BYTES` em vez de `sizeof` de um ponteiro para exercitar
  fragmentacao, fila cheia e descarte no fechamento. O agente nao executou
  build, testes ou QEMU; a nova matriz funcional permanece pendente.

- NET2: validação funcional confirmada pelo usuário no QEMU.
  Confirmada em: 2026-08-29 22:45:09 -03:00 (America/Sao_Paulo).
  `net socket check` concluiu com todos os casos `OK`, `net check` terminou
  `OK`, `regcheck full` publicou `RegCheck: OK` e `skbstat` confirmou
  `ativos=0`, `alocados=14`, `liberados=14` e `erros=0`. Os logs de FD,
  endereço, conclusão e liberação recusados pertencem às fixtures negativas
  deliberadas; não houve socket ou buffer residual. O resumo NET2 foi marcado
  como concluído no roadmap. `NET4` permanece pendente.

- NET3: implementacao de `poll()`/`select()` sobre a VFS registrada.
  Implementada em: 2026-08-29 23:17:25 -03:00 (America/Sao_Paulo).
  Foram adicionados o contrato `poll.h`, readiness append-only em
  `file_operations_t`, canal global `VFS-poll`, notificacoes de pipe, socket,
  TCP e IPC, wrappers da App API, syscalls 23/24, copia segura ring 3,
  `selecttest`, dependencias do Makefile e os contratos/metricas NET3. O
  `boot.asm` nao foi alterado. O agente nao executou build, testes ou QEMU;
  `make q3check`, `make clean && make`, `make run` e a matriz funcional NET3
  permanecem pendentes da confirmacao do usuario.

- NET3: correcao da ordem de inicializacao apos panic no bootstrap.
  Identificada e corrigida em: 2026-08-29 23:52:34 -03:00
  (America/Sao_Paulo). O novo canal global `VFS-poll` era registrado durante
  `vfs_init()`, mas o servico de espera ainda nao havia executado `wait_init()`;
  por isso a VFS ficava indisponivel e a criacao do processo Idle falhava ao
  inicializar seus descritores. `wait_init()` passou a ocorrer antes de
  `app_api_init()` e deixou de ser repetido antes do bootstrap de processos,
  preservando o registro do canal. O `boot.asm` nao foi alterado. O agente nao
  executou build, testes ou QEMU; os pre-requisitos operacionais e a matriz
  funcional NET3 continuam pendentes.

- NET3: correcao do wakeup de `poll()` durante execucao cooperativa.
  Implementada em: 2026-08-30 00:05:08 -03:00 (America/Sao_Paulo). O
  `selecttest` usa workers cooperativos para cancelar uma espera e fechar um
  pipe enquanto o processo Shell bloqueia em `poll()`. O yield de processo
  agora tambem oferece uma oportunidade ao scheduler de threads, e cada
  thread registra o PID do processo criador para que operacoes VFS do worker
  usem a tabela correta mesmo durante a troca para o Idle. O `boot.asm` nao
  foi alterado. O agente nao executou build, testes ou QEMU; a confirmacao
  funcional continua pendente.

- NET3: contrato documental de `thread_t` sincronizado apos `q3check`.
  Corrigido em: 2026-08-30 00:06:42 -03:00 (America/Sao_Paulo). O gate
  reportou somente a ausencia da referencia a `src/include/process/thread.h`
  em `docs/07-processos/processos.md`; o campo `owner_pid` e sua semantica de
  operacoes VFS durante o bloqueio do processo foram documentados. A
  validacao funcional NET3 permanece pendente.

- NET3: correcao dos fixtures de cancelamento e autoteste cooperativo.
  Implementada em: 2026-08-30 00:14:08 -03:00 (America/Sao_Paulo). O worker de cancelamento
  agora remove a espera imediatamente apos o processo entrar no canal VFS,
  evitando que o timeout vença antes do sinal sintetico. O autoteste de
  threads reinicia o cursor do scheduler antes de verificar a alternancia
  A/B, evitando depender do slot usado pelos workers anteriores. O agente nao
  executou build, testes ou QEMU; a confirmacao funcional permanece pendente.

- NET3: validacao funcional confirmada pelo usuario.
  Confirmada em: 2026-08-30 00:20:36 -03:00 (America/Sao_Paulo). O comando
  `selecttest` retornou `Resultado: OK` em todos os cenarios, incluindo
  cancelamento por sinal e ausencia de waiter VFS residual. O `regcheck full`
  tambem retornou `RegCheck: OK`. O `boot.asm` nao foi alterado.

- NET4: implementacao de rotas IPv4, `netstat` e monitoramento agregado.
  Implementada em: 2026-08-30 08:47:04 -03:00 (America/Sao_Paulo). Foi criada
  a tabela em RAM de 16 entradas com validacao, rota direta/default,
  longest-prefix match, exclusao, reset e autoteste deterministico. `ipv4_send()`
  passou a consultar a tabela e rejeita rotas de interfaces diferentes da L3
  atual. `TCP_STATE_LISTEN` foi anexado ao enum, e `netstat` passou a agregar
  TCP, `AF_UNIX` e contadores por interface. `route_validate_state()` foi
  integrado ao recovery de Network e ao `regcheck full`; `net check`, ajuda,
  dispatcher, Makefile e documentacao foram atualizados. O `boot.asm`, a App
  API, syscalls e persistencia em disco nao foram alterados. O agente nao
  executou build, testes ou QEMU; a confirmacao funcional registrada abaixo
  foi fornecida pelo usuario.

- NET4: correcao do autoteste da rota padrao. Corrigida em: 2026-08-30,
  apos a validacao funcional do usuario. O fixture usava `10.0.0.0/8` como
  se fosse rota default e consultava um destino fora dessa rede; agora usa
  `route_set_default()` com `0.0.0.0/0`, mantendo a verificacao de
  longest-prefix e duplicidade. A nova validacao funcional ainda deve ser
  confirmada pelo usuario.

- NET4: validacao funcional confirmada pelo usuario. Confirmada em:
  2026-08-30. `route check` retornou `Resultado: OK`, `regcheck full`
  retornou `RegCheck: OK` e `health check` concluiu sem falha de rede
  observavel. Os avisos dos casos negativos do autoteste de rotas foram
  mantidos como diagnosticos esperados.

- PROC0: contrato de introspeccao e ABI textual documentado. Implementado em:
  2026-08-30 09:48:38 -03:00 (America/Sao_Paulo). Foram congelados os
  namespaces `/proc` e `/sys`, a gramatica ASCII por linhas, snapshots por
  abertura de 16 KiB, cursor/EOF, ownership pelo `file_t`, identidade por
  geracao, ordenacao deterministica, acesso publico somente leitura e codigos
  de erro. Nenhum arquivo de `src/`, Makefile, boot, App API, syscall ou
  catalogo de headers foi alterado. O agente nao executou build, testes ou
  QEMU; `make q3check` deve ser executado pelo usuario para validar os
  documentos.

- PROC0: gate documental confirmado pelo usuario. Confirmado em: 2026-08-30
  (horario nao informado). `make q3check` retornou `resultado OK` em todas as
  verificacoes, e `python tools/vendor_terminus.py --check` confirmou fontes e
  dados gerados validos. A validacao funcional de `ls /proc`, `cat /proc/...`
  e montagem permanece reservada ao PROC1.

- PROC1: infraestrutura inicial de procfs integrada ao VFS. Implementada em:
  2026-08-30 (horario nao informado). Foi criado o provider `procfs` com o
  no `/proc/uptime`, snapshots imutaveis de 16 KiB, cursor, EOF, seek,
  rejeicao de escrita, montagem pinned e listagem pela VFS. O autoteste foi
  incorporado ao `vfs_self_test()` e o Makefile, contratos, comandos e roadmap
  foram atualizados. O `boot.asm`, `stage2.asm`, App API e syscalls nao foram
  alterados. O agente nao executou build, testes ou QEMU; a confirmacao de
  `make q3check`, build, QEMU e dos comandos `/proc` permanece pendente do
  usuario.

- PROC1: validacao funcional confirmada pelo usuario. Confirmada em:
  2026-08-30 (horario nao informado). `mount` exibiu `/proc -> procfs` em
  modo `RO`, `ls /proc` listou `uptime` e duas execucoes de `cat /proc/uptime`
  produziram snapshots ASCII com ticks diferentes. `regcheck full` retornou
  `RegCheck: OK` e `health check` concluiu sem falha de VFS. A tentativa de
  `mount | grep proc` foi recusada porque pipelines ainda nao sao suportados
  pelo Shell; a montagem foi confirmada diretamente por `mount`. O PROC1 foi
  marcado como concluido no roadmap; `/sys` permanece reservado ao PROC3.

- PROC2: implementacao dos nos globais e processos em `/proc`. Implementada
  em: 2026-08-30 11:18:09 (America/Sao_Paulo). O procfs passou a publicar os cinco
  nos globais, diretorios PID ordenados, `status`, `cmdline` e `maps`, mantendo
  snapshots ASCII imutaveis de 16 KiB e leitura somente. O gerenciador de
  processos passou a fornecer geracao append-only, snapshots sem ponteiros,
  copia de VMAs com retry por geracao e contagem de paginas residentes. O
  Makefile, VFS, contratos, documentacao de processos e comandos foram
  atualizados. `boot.asm`, `stage2.asm`, App API, syscalls e persistencia nao
  foram alterados. O agente nao executou build, testes ou QEMU; a confirmacao
  funcional foi registrada na entrada seguinte.

- PROC2: validacao funcional apresentada pelo usuario em QEMU; horario da
  captura nao informado. `ls /proc` listou os nos globais na ordem contratada e
  os PIDs `0` a `6`. As leituras de `uptime`, `meminfo`, `cpuinfo`, `version`,
  `cmdline`, `/proc/0/status`, `/proc/0/cmdline` e `/proc/0/maps` retornaram
  snapshots ASCII; o mapa de Idle permaneceu vazio conforme o contrato.
  `RegCheck: OK` foi confirmado. `health check` concluiu sem falha de VFS; os
  estados `DISABLED`/`DEGRADED` exibidos pertencem a componentes opcionais já
  indisponíveis no perfil executado. O PROC2 foi marcado como concluído no
  roadmap; `/sys` permanece reservado ao PROC3.

- PROC3: provider sysfs integrado ao VFS. Implementado em: 2026-08-30
  (horário não informado).
  Foram criados `src/fs/sysfs.c` e `src/include/fs/sysfs.h`, com montagem
  automática pinned em `/sys`, hierarquia fixa de PCI, rede, blocos e energia,
  atributos ASCII somente leitura, snapshots de até 16 KiB, cursor, EOF,
  `lseek`, cópia dos inventários, validação de capacidade e limpeza em erro e
  fechamento. O `vfs_self_test()` recebeu o resultado append-only `sysfs`, e a
  raiz virtual, `mount`, lookup, `chdir`, listagem e Makefile foram atualizados.
  Não houve alteração de `boot.asm`, `stage2.asm`, App API, syscalls ou
  persistência. O agente não executou build, testes ou QEMU; a confirmação
  funcional do usuário foi recebida posteriormente.

- PROC3: validação funcional confirmada pelo usuário em QEMU em 2026-08-30
  (horário não informado). `mount` exibiu `/sys -> sysfs` como `SYSFS` e
  somente leitura; `ls /`, `ls /sys`, `ls /sys/bus`,
  `ls /sys/bus/pci/devices`, `ls /sys/class/net` e `ls /sys/class/block`
  confirmaram a raiz e os inventários ordenados de PCI, rede e bloco.
  `RegCheck: OK` e `health check` concluíram sem falha de VFS. A tentativa de
  `ls /sys/power/state` aplicou listagem de diretório a um arquivo regular e
  retornou o erro esperado; a leitura correta é `cat /sys/power/state`.
  O PROC3 foi marcado como concluído no roadmap.

- PROC4: integracao dos consumidores nativos com procfs/sysfs implementada em
  2026-08-30 (horario nao informado). Foi criado o adaptador interno de
  snapshots do Shell em `src/shell/shell_introspection.c`; o Task Manager
  Classic passou a ler status de processos e memoria pela VFS, preservando o
  fallback Simple e a aba de threads. `devices` e `device-info` passaram a
  consultar nos PCI, rede e bloco em `/sys`, com fallback legado para hardware
  sem representacao no pseudo-filesystem.

  O comando `proccheck` foi registrado no dispatcher para validar a hierarquia,
  atributos ASCII, EOF, caminhos invalidos e rejeicao de escrita. O Makefile,
  ajuda e documentacao foram atualizados. Nao houve alteracao de App API,
  syscalls, layouts binarios, persistencia, `boot.asm` ou `stage2.asm`.
  A validacao funcional de PROC4 foi confirmada pelo usuario em QEMU em
  2026-08-30 (horario nao informado). `proccheck` concluiu `testes=45
  aprovados=45`; os avisos exibidos correspondem as fixtures negativas de
  caminho ausente, atributo ausente, listagem de arquivo e abertura com
  escrita, incluindo a rejeicao esperada registrada pela VFS. Em conjunto
  com a validacao anterior de `taskmgr` e `devices`, a integracao funcional
  foi aceita. `RegCheck: OK` e `health check` concluiram sem falha relacionada
  a procfs/sysfs. O PROC4 foi marcado como concluido no roadmap; `/proc/sys/`
  permanece reservado para etapa futura.

- PROC5: contrato documental inicial criado em 2026-08-30 (horario nao
  informado). O registro inicial definiu a futura superficie controlada
  `/proc/sys`, inicialmente
  para `kernel/console_log_level` e `kernel/buffer_log_level`, com valores
  `error`, `warn`, `info` e `debug`. O contrato fixa leitura por snapshot,
  escrita como transacao de valor unico, validacao ASCII, commit atomico,
  preservacao do valor anterior em erro, ausencia de persistencia e gate de
  privilegio explicito. Scheduler, forwarding IPv4, energia, memoria e
  parametros de processos permanecem fora do primeiro conjunto. Nenhum
  codigo, header, Makefile, syscall, App API ou comportamento de escrita foi
  alterado naquele registro; a implementacao foi registrada posteriormente
  abaixo.

- Infraestrutura do layout legado corrigida em: 2026-08-30 (horario nao
  informado). O build havia produzido `kernel.bin` com 1.806.190 bytes,
  equivalentes a 3.528 setores, ultrapassando a janela anterior entre os
  LBAs 64 e 3584 em oito setores. O layout atual reserva kernel no LBA 64,
  recovery loader no LBA 6144 e FAT32 no LBA 8192; `stage2`, Makefile,
  compositor, empacotador e matrizes EP9.4 foram sincronizados. O `stage2`
  agora rejeita antecipadamente um kernel que atravesse a janela do loader,
  enquanto o compositor mantem a validacao de sobreposicao. `boot.asm`, App
   API e syscalls permanecem inalterados. A repeticao de `make q3check`, do
   build completo e a confirmacao funcional no QEMU permanecem pendentes.

- PROC5: controles de runtime em `/proc/sys` implementados em 2026-08-30
  (horario nao informado). O provider `procfs` agora publica `sys/kernel` e os
  controles `console_log_level` e `buffer_log_level`, com snapshots imutaveis,
  escrita validada por token, commit pelo backend de log, regra de dependencia
  console/buffer e reset para `info/info`. O gate aceita somente processos
  nativos/ring0; processos ring3, nos antigos de `/proc`, todo `/sys`,
  diretorios, `ioctl`, `sync`, entradas invalidas e overflow sao rejeitados
  conforme os codigos documentados.

  `procfs_self_test()` cobre listagem, leitura, escrita, rollback, snapshot
  antigo, reset e limpeza; `proccheck` cobre os caminhos e a rejeicao de
  escrita em `/sys`. Nao houve alteracao de App API, syscalls, layouts
  binarios, `taskmanager.h`, `sysfs`, bootloader ou persistencia. A execucao de
  `make q3check` e do build completo permanecem pendentes de confirmacao apos
  a correcao dos logs dos helpers de escrita.

- PROC5: validacao funcional confirmada pelo usuario no QEMU em 2026-08-30
  (horario nao informado). `proccheck` terminou com `testes=51 aprovados=51`,
  `regcheck full` retornou `RegCheck: OK` e `health check` nao apresentou
  falha relacionada ao PROC5. Os avisos e erros exibidos durante o
  `proccheck` pertencem as fixtures negativas de privilegio, valores invalidos,
  caminhos ausentes e escritas rejeitadas. A confirmacao do `make q3check` e do
  build completo apos a correcao dos logs ainda deve ser registrada antes de
  marcar PROC5 como concluido no roadmap.

- PWR0: contrato documental de energia e ACPI definido em 2026-08-30.
  O Roadmap 16 agora fixa os estados do servico `UNKNOWN`, `DISCOVERING`,
  `READY`, `DEGRADED` e `UNAVAILABLE`, separados dos estados ACPI S0-S5, e
  define capacidades individuais com pre-condicao, fallback e erro publico.
  `shutdown` e `reboot` compartilham a transacao
  `admission -> notification -> sync/flush -> quiescence -> hardware commit ->
  terminal`; o alvo e fixado na admissao e a primeira escrita ou comando de
  hardware inicia a regiao irreversivel.

  Foram congelados orcamentos PIT de 50 Hz, sem emprestimo entre fases:
  notificacao 250 ticks (5 s), sync/flush 1500 (30 s), quiescencia 250 (5 s),
  commit 100 (2 s) e total 2100 ticks (42 s). O coordenador possui a
  transacao, prazos, cancelamento e resultado; participantes possuem seus
  proprios recursos e devem ser idempotentes. Descoberta/validacao ACPI,
  interpretacao AML e uso de metodos permanecem separados, sem portas privadas
  de emuladores como fallback generico.

  PWR0 nao alterou `src/`, headers, Makefile, App API, syscalls, layouts
  binarios, `boot.asm`, `stage2.asm` ou transicoes reais de energia. A etapa
  foi validada por revisao cruzada dos documentos canonicos e `git diff --check`;
  nao houve build, teste ou QEMU. A implementacao dos estados, orcamentos,
  idle, metodos de hardware e notificacoes permanece nos PWR1-PWR4.

- PWR3: implementacao do desligamento e reboot deterministicos
  registrada em 2026-08-30T18:04:04-03:00. O driver ACPI passou a publicar o
  snapshot append-only de `RESET_REG` e as operacoes `acpi_reset()` e
  `acpi_poweroff()`; o coordenador `power` passou a aplicar a transacao comum
  com prazos PIT, sync/flush limitado, quiescencia de audio e ultimo erro.
  `system_reboot()` tenta RESET_REG, PS/2 pelo driver e triple fault nessa
  ordem; `poweroff` e a entrada canonica e `shutdown` permanece alias.

  A barra, Shell e Task Manager deixaram de escrever diretamente o reset; nao
  houve alteracao de App API, syscalls, layouts binarios, bootloader ou
  `stage2.asm`. A implementacao ainda nao foi validada pelo agente com build,
  testes ou QEMU. A confirmacao funcional pelo usuario e os gates
  `make q3check`, build completo e `make run` permanecem pendentes antes de
  marcar PWR3 como concluido no Roadmap 16.

- PWR3: validacao funcional confirmada pelo usuario em
  2026-08-30T18:27:18-03:00. Apos `make q3check`, build completo e `make run`,
  o reboot retornou ao Shell; `power status`, `regcheck full` e `health check`
  foram executados sem falha relacionada ao PWR3. Em execucao separada,
  `poweroff` encerrou o QEMU, confirmando o commit ACPI S5. O resumo PWR3 foi
  marcado como concluido no Roadmap 16. Nenhuma App API, syscall, layout
  binario ou bootloader foi alterado.

- PWR4: implementacao da notificacao e encerramento ordenado registrada em
  2026-08-30 (horario nao informado). O coordenador passou a usar uma cadeia
  estatica de processos ring3, workqueue, VFS/Storage, audio, rede e video;
  executa SIGTERM/SIGKILL, reaping, sync unico, bloqueio de novas operacoes,
  desmontagem de volumes nao-pinned e quiescencia best-effort antes do
  commit. `poweroff`, `reboot` e `shutdown` usam a transacao comum; nao houve
  alteracao em App API, syscalls, layouts binarios, `taskmanager.h`,
  `boot.asm` ou `stage2.asm`.

  O agente nao executou `make q3check`, o build completo ou o QEMU. Em
  2026-08-30, o usuario confirmou funcionalmente no QEMU que `poweroff` e
  `shutdown -h now` encerraram o sistema e que `shutdown -r now` reiniciou com
  retorno ao Shell. Depois do reboot, `power status`, `regcheck full` e
  `health check` foram executados; o `regcheck` permaneceu OK e o `health`
  exibiu somente capacidades opcionais ja desabilitadas ou degradadas. O
  resumo PWR4 foi marcado como concluido no Roadmap 16.

- Escopo v1.0.0: decomposição em roadmaps executáveis registrada em
  2026-08-30 (horário não informado). O escopo foi alinhado à sequência
  `18` Kernel/processos/userland, `19` ABI/segurança/permissões, `20`
  VFS/Storage/atualização, `21` hardware/rede/energia, `22`
  Shell/interface/aplicativos, `23` desempenho/validação e `24` release e
  aceitação. A atualização do próprio sistema pela internet passou a ter uma
  fase explícita no Roadmap 20, com manifesto autenticado, staging,
  transação, rollback e fallback offline.

  Os índices, links e dependências foram atualizados; a etapa pós-1.0.0 de
  Rust permanece depois do Roadmap 24. Nenhum roadmap 16 ou anterior foi
  alterado. Esta foi uma alteração documental: não houve build, teste
  executável ou QEMU, e nenhum roadmap foi marcado como concluído por causa
  desta reorganização.

- Roadmaps 18–24: melhorias arquiteturais incorporadas em 2026-08-30 (horário
  não informado). A revisão passou a tratar segurança como requisito
  transversal, incluiu supervisor mínimo de serviços, credenciais e limites
  por processo, reforçou a semântica de PID/reaper e delimitou o userland de
  execução do Shell e da GUI.

  O Roadmap 20 agora separa backend e frontend da atualização do sistema e
  exige slots A/B ou equivalente, confirmação de boot, limite de tentativas,
  compatibilidade entre kernel/recovery/bootloader/filesystem e rollback. O
  Roadmap 21 recebeu ciclo de vida pai/filho de dispositivos, e os Roadmaps
  22–24 receberam estados de atualização, validação contínua e critérios de
  release para identidade, serviços e recuperação.

  Esta foi uma atualização documental baseada em revisão arquitetural; não
  houve build, teste executável ou QEMU. Os Roadmaps 01–16 permaneceram
  inalterados.

- Roadmap 17: infraestrutura permanente de teste completo e regressão criada
  em 2026-08-30 (horário não informado). O documento define catálogo de
  contratos, testes unitários no host, autotestes do kernel, executor externo
  de QEMU, protocolo de resultados, fixtures de falha, matriz de hardware,
  recuperação, limpeza e cobertura por estado/erro/contrato.

  A frente é independente de qualquer versão, release ou reorganização interna.
  Esta criação foi documental; não houve implementação do executor,
  build, teste executável ou QEMU.

- TST1: catálogo completo de funções e casos de teste implementado em
  2026-08-30 (horário não informado). `tests/catalog.json` passou a ser a
  fonte canônica das superfícies C, Assembly/boot, APIs de headers, comandos
  do dispatcher e syscalls. `tools/test_catalog.py` fornece `scan`, `sync`,
  `validate`, `render` e `check-rendered`; novas superfícies entram como
  `PENDING` e superfícies removidas são aposentadas com motivo obrigatório.
  `docs/qualidade/catalogo-testes.md` é gerado deterministicamente e o alvo
  host-only `make catalog-test` foi integrado ao Makefile.

  A implementação não altera App API, syscalls, ABI, bootloader ou o
  comportamento do sistema. O agente não executou build, testes do projeto ou
  QEMU; a validação host-only do catálogo e a marcação formal de TST1 como
  concluído permanecem pendentes da execução do usuário.

- TST1: escopo do testador ampliado em 2026-08-30 (horário não informado) para
  incluir explicitamente execução pós-boot no QEMU, captura serial, heartbeat,
  watchdog, testes `stress`/`soak`, modo `--until-failure`, seed e iteração
  reproduzíveis, além da preservação dos artefatos da primeira falha. O
  catálogo continua sendo somente a base de inventário; executor runtime e
  estresse permanecem planejados para TST2, TST4, TST5 e TST6.

- TST1: validação host-only confirmada pelo usuário em 2026-08-30. `make
  catalog-test` validou o schema e o inventário (`Catalogo valido: 6661
  superficies, 0 casos`) e confirmou a correspondência determinística da
  visão (`Catalogo e visao validos: 6661 superficies, 0 casos`). O TST1 foi
  marcado como concluído no Roadmap 17; as superfícies continuam `PENDING`
  até os executores e casos de comportamento das fases seguintes.

- TST2: infraestrutura do executor QEMU e protocolo ZTEST implementada em
  2026-08-30 23:11 (America/Sao_Paulo). Foram adicionados o driver interno
  COM1, o agente inerte no boot normal, o runner host-only com QMP externo,
  watchdog, stress, `--until-failure`, `-snapshot` e artefatos reproduziveis.
  O caso inicial `qemu:tst2:boot-ready` foi registrado no catalogo, junto dos
  alvos `test-qemu` e `test-qemu-selftest`.

  O agente nao executou build, testes ou QEMU. TST2 permanece pendente da
  confirmacao funcional do usuario; `boot.asm`, `stage2.asm`, App API,
  syscalls, ABI e layouts binarios nao foram alterados.

- Correção do alvo `test-qemu` registrada em 2026-08-30 23:29
  (America/Sao_Paulo). O `Makefile.local` já fornece o caminho do executável
  QEMU entre aspas; o alvo deixou de acrescentar aspas externas duplicadas.
  O agente não executou build, testes ou QEMU após a correção.

- Correção do handshake inicial do TST2 registrada em 2026-08-30 23:38
  (America/Sao_Paulo). O runner passou a retransmitir `HELLO` com a mesma
  sequência até receber `READY`, cobrindo a janela entre a conexão TCP do
  COM1 e a inicialização do UART pelo guest. A execução do usuário terminou
  em `boot_ready_timeout` sem frames ZTEST; não houve `PANIC` nem erro de
  protocolo registrado. O agente não executou build, testes ou QEMU após a
  correção.

- Política operacional do agente atualizada em 2026-08-30 23:45
  (America/Sao_Paulo). O `AGENTS.md` agora permite que o agente execute
  somente comandos de build, testes ou QEMU explicitamente autorizados pelo
  usuário na conversa; sem autorização, a revisão permanece estática. A
  alteração não modifica código, ABI, bootloader ou comportamento do sistema.

- TST2: validação executada pelo agente em 2026-08-30 23:49
  (America/Sao_Paulo). `make q3check` passou; `make clean` e `make` passaram,
  com warnings de compilação; `make test-qemu` falhou com
  `termination=watchdog` e `cause=guest_sem_heartbeat`. O guest publicou
  `READY`, mas não publicou `HEARTBEAT`, `BEGIN` ou `PASS` antes do watchdog.
  O relatório foi preservado em
  `build/test-results/qemu-20260831T024935Z-22908/`. TST2 permanece pendente.

- TST2: núcleo puro do protocolo, suíte host-only e diagnóstico de progresso
  concluídos em 2026-08-31 (America/Sao_Paulo). `src/core/test_protocol_core.c`
  passou a concentrar parser incremental, CRC, sequências, comandos e eventos;
  `src/core/test_protocol.c` ficou como adaptador de COM1 e timer, mantendo a
  API pública existente. O alvo `make test-tst2-host` usa `HOST_CC` configurado
  em `Makefile.local`, separado do cross-compiler freestanding do kernel, e
  executou 5 testes C e 11 testes Python com resultado `OK`.

  A primeira execução do smoke test terminou sem erro agregado, mas o relatório
  mostrou `READY -> BEGIN -> PASS` sem heartbeat. A causa foi o término imediato
  do caso antes do intervalo periódico do guest. O runner passou a enviar
  `PING` depois de `READY` e a exigir o heartbeat correspondente; o teste Python
  também cobre a falha artificial de heartbeat após `BEGIN` com estado
  identificável.

- TST2: validação final executada pelo agente em 2026-08-31 (America/Sao_Paulo).
  `make test-tst2-host`, `make q3check`, `make q3check-test`, `make clean`,
  `make` e `make test-qemu` passaram. O smoke test único produziu no artefato
  `build/test-results/qemu-20260831T150004Z-9904/` os frames `READY seq=1`,
  `HEARTBEAT seq=2 ticks=237`, `BEGIN seq=3` e `PASS seq=4`, nessa ordem;
  `result.json` registrou `status=PASS`, `termination=completed`,
  `last_state=PASS` e `protocol_errors=[]`.

  A execução preservou `manifest.json`, `serial.log`, `qemu.stdout.log`,
  `qemu.stderr.log` e `result.json`. O build exibiu warnings preexistentes em
  outros módulos, sem warning novo no protocolo TST2. `boot.asm` e `stage2.asm`
  permaneceram inalterados.

- TST3: implementacao da primeira camada host-only concluida em 2026-08-31
  12:36 (America/Sao_Paulo). Foram adicionados os testes Python formais de
  `tools/packager.py` e `tools/updater.py`, o teste C host-only de
  `src/core/string.c` e `src/memory/compress.c`, e o runner separado
  `tools/tst3_host_runner.py`. O Makefile ganhou `test-tst3-host` e
  `test-tst3-sanitize`; `test-tst2-host` foi restringido ao conjunto TST2.
  Os testes usam fixtures temporarias, buffers estaticos, stub de
  `video_print()` e timeout por subprocesso.

- TST3: a suite strict passou em 2026-08-31 (America/Sao_Paulo) com
  `make test-tst3-host`: compilacao C com `HOST_CC`, 11 testes Python de
  packager, 11 testes Python de updater, `packager.py selftest` e
  `updater.py selftest` terminaram com `PASS`/`OK`. O alvo sanitizado foi
  executado na mesma validacao e retornou `BLOCKED` de forma explicita: o
  Clang 22 do MSYS2 UCRT64 foi instalado, mas o pacote nao fornece os import
  libs `libclang_rt.asan_dynamic`/UBSan necessarios para link. O diagnostico
  completo foi preservado em `build/test-results/tst3-host/sanitize/`; a
  execucao strict posterior ficou preservada em `build/test-results/tst3-host/strict/`.

- TST3: a correcao de `compress.c` foi validada em 2026-08-31. `compress_init`
  agora reseta tambem o estado global de habilitacao; o tamanho maximo satura
  em `uint32_t`; o compressor usa a mesma coordenada absoluta do anel LZSS
  que o descompressor; e streams com grupo de flags vazio apos o inicio sao
  rejeitados, sem invalidar a codificacao de entrada vazia. `make package-test`,
  `make update-test` e `make q3check` passaram; `make clean` seguido de `make`
  passou com warnings preexistentes em outros modulos.

- TST3: smoke QEMU unico executado em 2026-08-31 12:36 (America/Sao_Paulo)
  apos os gates, conforme a alteracao em `compress.c`. `make test-qemu`
  passou com o caso existente `qemu:tst2:boot-ready`, frames `READY`,
  `HEARTBEAT`, `BEGIN` e `PASS` nessa ordem, `protocol_errors=[]`,
  `last_state=PASS` e `last_event=PASS`. Artefatos preservados em
  `build/test-results/qemu-20260831T153609Z-24084/`.

- TST3: o runtime sanitizador foi disponibilizado em 2026-08-31
  13:25 (America/Sao_Paulo) com o instalador oficial LLVM 22.1.8 fora do
  repositorio. O `tools/tst3_host_runner.py` passou a localizar o diretorio
  de recursos do Clang e inclui-lo no `PATH` dos binarios instrumentados. O
  alvo `make test-tst3-sanitize` passou com ASan/UBSan; o diagnostico anterior
  `BLOCKED` do pacote MSYS2 UCRT64 foi preservado como historico.

- TST3: durante a execucao sanitizada, o ASan encontrou e permitiu corrigir um
  overflow no fixture de strings e uma leitura fora do limite em
  `compress_data`: o loop de match incrementava `si` e ainda somava `k` ao
  indice de origem. A correcao foi validada com `make test-tst3-host` e
  `make test-tst3-sanitize`, ambos com `PASS`.

- TST3: validacao final em 2026-08-31 com `make package-test`, `make update-test`
  e o smoke QEMU unico apos a correcao. O caso `qemu:tst2:boot-ready` passou
  com `READY`, `HEARTBEAT`, `BEGIN` e `PASS` nessa ordem, sem erros de
  protocolo. Artefatos preservados em
  `build/test-results/qemu-20260831T162545Z-25364/`.

- TST4.1: implementacao e validacao executadas em 2026-08-31 14:05
  (America/Sao_Paulo). Foi adicionado o harness interno
  `src/core/kernel_tests.c`, o caso ZTEST `qemu:tst4:memory-slab`, a integracao
  no Makefile e a cobertura especifica no catalogo. `kmem_cache_self_test`
  passou a restaurar seus contadores diagnosticos apos os caminhos negativos.
  `make test-qemu-selftest`, `make q3check`, `make clean`, `make`,
  `make test-tst4-qemu` e `make catalog-test` passaram.

  A primeira execucao do caso foi preservada em
  `build/test-results/qemu-20260831T165009Z-21628/` e terminou com
  `FAIL`/`ERR_STATE` na relacao inicial de paginas do PMM. O diagnostico mostrou
  que paginas reservadas do kernel/heap nao devem ser somadas a
  `pmm_owned_pages`; o harness foi ajustado para respeitar esse contrato.
  A execucao aprovada foi preservada em
  `build/test-results/qemu-20260831T170423Z-15384/`, com uma iteracao,
  `READY -> HEARTBEAT -> BEGIN -> PASS`, `last_state=PASS`,
  `protocol_errors=[]`, `qemu_exit_code=0` e manifesto, serial, stdout,
  stderr e result preservados. O build exibiu apenas warnings preexistentes
  em outros modulos; boot.asm e stage2.asm nao foram alterados.

- TST4.2-TST4.6: implementacao concluida em 2026-08-31 15:31
  (America/Sao_Paulo). Foram adicionados os harnesses internos independentes
  de paging/VMA, execution, storage/VFS, network e platform, o callback de
  progresso/heartbeat e os cinco alvos QEMU com uma unica iteracao e sem retry
  automatico. Nao houve alteracao de bootloader, headers publicos, ABI, Shell
  ou protocolo ZTEST. Os fixtures procfs/sysfs passaram a usar buffers estaticos
  com capacidade adequada para evitar pressao de stack durante os autotestes.

  A primeira execucao de platform foi preservada em
  `build/test-results/qemu-20260831T182201Z-3328/` como `FAIL`/`ERR_STATE` na
  validacao do inventario Wi-Fi ainda nao inicializado. O harness foi ajustado
  para seguir a inicializacao sob demanda existente e validar o estado
  publicado; a execucao posterior passou.

- TST4.2-TST4.6: validacao final executada em 2026-08-31 15:31
  (America/Sao_Paulo), na ordem `make test-qemu-selftest`, `make q3check`,
  `make clean`, `make`, `make test-tst4-qemu`,
  `make test-tst4-qemu-paging-vma`, `make test-tst4-qemu-execution`,
  `make test-tst4-qemu-storage-vfs`, `make test-tst4-qemu-network`,
  `make test-tst4-qemu-platform` e `make catalog-test`. Todos passaram.

  Os casos produziram `READY -> HEARTBEAT -> BEGIN -> PASS`, sem erros de
  protocolo, com timeout limitado e artefatos preservados nos diretorios:
  `qemu-20260831T182652Z-11896`, `qemu-20260831T182717Z-6764`,
  `qemu-20260831T182742Z-18396`, `qemu-20260831T182807Z-19232`,
  `qemu-20260831T182845Z-20608` e `qemu-20260831T182911Z-20368`.
  O catalogo e a visao renderizada foram validados com 6780 superficies e
  7 casos. A TST4 foi marcada como concluida; TST5, TST6 e TST7 permanecem
  pendentes.

- TST5: implementacao da primeira camada black-box concluida em 2026-08-31
  (America/Sao_Paulo). Foram adicionados o observador interno do terminal,
  o harness `kernel_tests_blackbox.c`, a injecao de teclado QMP com allowlist,
  rastreamento de entradas, estados de reboot/poweroff e nove casos QEMU
  independentes. Nao houve alteracao de bootloader, headers publicos, ABI ou
  eventos ZTEST.

- TST5: validacao preliminar executada em 2026-08-31. `make test-tst5-host`,
  `python tools/qemu_test_runner.py --self-test`, `make q3check`, `make clean`,
  `make` e `make catalog-test` passaram. O catalogo foi sincronizado e
  renderizado com 6.792 superficies e 16 casos. A matriz dos nove alvos QEMU
  ainda nao foi executada; portanto a TST5 permanece pendente de evidencia
  funcional e nao e declarada concluida.

- TST5: validacao final executada em 2026-08-31 (America/Sao_Paulo), com
  `make test-qemu-selftest`, `make test-tst5-host`, `make q3check`, `make clean`,
  `make`, os nove alvos QEMU independentes e `make catalog-test`. Todos os
  alvos passaram com uma iteracao e sem retry automatico. As execucoes finais
  foram preservadas em:
  `build/test-results/qemu-20260831T201040Z-25328/`,
  `qemu-20260831T201118Z-21540/`, `qemu-20260831T201816Z-10320/`,
  `qemu-20260831T201851Z-6320/`, `qemu-20260831T201917Z-2608/`,
  `qemu-20260831T201945Z-11812/`, `qemu-20260831T202011Z-23024/`,
  `qemu-20260831T202300Z-14868/` e
  `qemu-20260831T202446Z-20756/`. Cada caso produziu
  `READY -> HEARTBEAT -> BEGIN -> PASS`; reboot registrou `RESET` e novo
  `HELLO -> READY -> HEARTBEAT`, e poweroff registrou `SHUTDOWN`. Cada
  diretorio contem manifesto, serial, stdout/stderr do QEMU, entrada, eventos
  QMP, screenshots e `result.json`, sem processos QEMU residuais.

- TST5: durante a primeira tentativa funcional, foi identificado e corrigido
  o processamento de entrada suspenso enquanto o protocolo ZTEST estava ativo;
  `system_process_main()` agora drena a fila de teclado antes de ceder a CPU.
  O runner tambem passou a tratar desconexao QMP normal durante poweroff e a
  aceitar esperas declarativas limitadas para transicoes de cenas. A TST5 foi
  marcada como validada para os nove casos previstos; TST6 e TST7 permanecem
  pendentes.

- TST6: implementacao e validacao final executadas em 2026-08-31 19:06
  (America/Sao_Paulo). Foram adicionados o runner host-only com perfis QEMU
  allowlisted, seeds reproduziveis, limites de 1.000 iteracoes e 600 segundos,
  os perfis isolados em snapshot, o harness interno `kernel_tests_tst6.c`, os
  20 casos independentes no catalogo e os alvos Makefile correspondentes. Nao
  houve alteracao de bootloader, headers publicos, ABI ou protocolo ZTEST.

  Passaram `make test-qemu-selftest`, `make test-tst6-host`, `make q3check`,
  `make clean`, `make`, `make catalog-test` e os 20 alvos QEMU individuais.
  Os quatro casos de estresse passaram com oito iteracoes. As execucoes finais
  produziram `READY -> HEARTBEAT -> BEGIN -> PASS`, e cada diretorio preservou
  `manifest.json`, `serial.log`, `qemu.stdout.log`, `qemu.stderr.log`,
  `input.log`, `qmp-events.log`, screenshots disponiveis e `result.json`.
  A verificacao automatica confirmou 20/20 resultados com `status=PASS`,
  `last_state=PASS`, ordem inicial de eventos valida e nenhum processo QEMU
  residual.

  Os artefatos finais foram preservados em:
  `qemu-20260831T215120Z-25500`, `qemu-20260831T215153Z-19048`,
  `qemu-20260831T215214Z-15972`, `qemu-20260831T215236Z-20676`,
  `qemu-20260831T215307Z-19148`, `qemu-20260831T215342Z-8276`,
  `qemu-20260831T215404Z-11068`, `qemu-20260831T215426Z-16792`,
  `qemu-20260831T215457Z-3356`, `qemu-20260831T215539Z-20680`,
  `qemu-20260831T215826Z-14544`, `qemu-20260831T215903Z-4840`,
  `qemu-20260831T215951Z-6784`, `qemu-20260831T220204Z-1652`,
  `qemu-20260831T220244Z-8928`, `qemu-20260831T220319Z-14620`,
  `qemu-20260831T220341Z-13476`, `qemu-20260831T220411Z-24812`,
  `qemu-20260831T220432Z-1984` e `qemu-20260831T220454Z-6432`, todos sob
  `build/test-results/`.

  Durante a rodada final, `fault:block` atingiu o watchdog com 20 segundos
  enquanto o autoteste de storage ainda estava executando; o resultado
  `qemu-20260831T220013Z-12660` foi preservado como diagnostico. O padrao
  TST6 foi ajustado para 60 segundos e a repeticao passou em
  `qemu-20260831T220204Z-1652`. A rede dos perfis network foi executada com
  backend QEMU user-mode restrito (`restrict=on`), sem encaminhamento externo.
  Package/update validaram os contratos de failpoint e indisponibilidade da
  imagem FAT32; uma mutacao transacional real permanece dependente de fixture
  FAT12 mutavel. Hardware fisico continua `BLOCKED` por ausencia de equipamento.

- TST7: implementacao inicial concluida em 2026-08-31 20:31 (America/Sao_Paulo).
  Foi criado `tools/tst7_regression_runner.py` com os modos `quick`, `full` e
  `approve --run-id`, o comparador de status/contrato, timeout, warnings,
  cobertura e duracao, alem do armazenamento duravel em `.tst7-results/`.
  Tambem foram adicionados `tests/unit/test_tst7_runner.py`, o manifesto
  versionado `tests/regressions/manifest.json`, os alvos Makefile TST7 e a
  documentacao operacional. A validacao unitária inicial passou com 10 testes
  e o catalogo passou com 6.808 superficies e 36 casos.

  A TST7 ainda nao foi marcada como concluida: falta executar os alvos
  `test-tst7-host`, `test-tst7-quick` e `test-tst7-full`, revisar e aprovar o
  primeiro baseline e repetir o `full` contra ele. Nenhum baseline foi criado
  automaticamente.

- TST7: validacao inicial executada em 2026-08-31 20:34 (America/Sao_Paulo).
  `make test-tst7-host` passou com 11 testes; `make test-tst7-quick` executou
  todos os sete passos, com seis `PASS` e `test-tst3-sanitize` em `BLOCKED`
  porque o Clang configurado retornou `permission denied`. O primeiro `full`
  preservou 36/36 casos QEMU, com 31 `PASS` e failures de perfil/heartbeat;
  nenhum baseline foi criado.

- TST7: segunda validacao `full` concluida em 2026-08-31 21:23
  (America/Sao_Paulo), no run
  `.tst7-results/tst7-20260901T000021Z-21692/`. A execucao fez limpeza,
  build, gates host-only, catalogo e todos os 36 casos QEMU sem retry; foram
  31 `PASS` e 5 `FAIL` (`tst4:network` da configuracao anterior e quatro
  watchdogs TST6), alem do bloqueio ambiental do sanitizador. A aprovacao
  explicita foi rejeitada para esse relatorio, como exigido, e
  `tests/baselines/tst7-approved.json` continua ausente.

  A politica final de rede foi verificada isoladamente: `tst5:apps` passou com
  `-nic none` e `tst4:network` passou com E1000 user-mode restrita. Tambem
  passaram novamente `make test-qemu-selftest`, `make q3check`,
  `make catalog-test` e toda a suite Python com 63 testes. Nao houve processo
  QEMU residual. A TST7 permanece implementada, mas nao concluida: ainda falta
  um `full` sem falhas/bloqueios, a aprovacao real do baseline e uma segunda
  execucao `full` aprovada.

- TST7: revisão final da infraestrutura executada em 2026-08-31 21:32
  (America/Sao_Paulo). Corrigido o fechamento do relatorio para usar somente
  estado pertencente ao proprio relatorio e endurecida a classificacao: codigo
  de saida 2 e `FAIL` por padrao, tornando-se `BLOCKED` somente quando a saida
  declara explicitamente o bloqueio; um caso QEMU sem `result.json` agora e
  `BLOCKED`. `make test-tst7-host` passou com 13 testes, a suite Python completa
  passou com 64 testes, `make q3check` passou e `make catalog-test` confirmou
  6.808 superficies e 36 casos. Nao ha processo QEMU residual.

  Nenhum baseline foi criado: os dois `full` anteriores executaram 36/36 casos,
  mas o mais recente terminou com 31 `PASS` e 5 `FAIL`, e o sanitizador Clang
  continua `BLOCKED` por `permission denied`. A TST7 permanece implementada e
  pendente da primeira execucao `full` totalmente aprovada e da segunda
  comparacao contra esse baseline.

- TST7: `make test-tst7-full` executado integralmente em 2026-08-31 22:48
  (America/Sao_Paulo), no run
  `.tst7-results/tst7-20260901T012735Z-27740/`. A execução concluiu limpeza,
  build, todas as etapas rápidas, `catalog-test`, `storage-fixtures` e os
  36/36 casos QEMU individualmente, sem retry. Todos os 36 casos QEMU ficaram
  `PASS`, incluindo os quatro estresses TST6 e as falhas controladas; não houve
  caso `FAIL` ou `TIMEOUT`. O único bloqueio foi `test-tst3-sanitize`, com
  `permission denied` no Clang configurado, portanto o resultado global ficou
  `BLOCKED`. Foram preservados 306 arquivos de artefato e não há processo QEMU
  residual. O baseline continua ausente e não foi aprovado automaticamente.

- TST7: registro da nova execução completa após liberar o Clang em
  2026-08-31 23:23 (America/Sao_Paulo), no run
  `.tst7-results/tst7-20260901T020106Z-23812/`. O `test-tst3-sanitize` passou
  e todas as etapas de build, host e catálogo passaram. Os 36 casos QEMU foram
  executados sem retry: 34 `PASS` e dois `FAIL` por watchdog do guest,
  `qemu:tst6:matrix:usb-storage` (`BEGIN -> HEARTBEAT`, estado `RUNNING`) e
  `qemu:tst6:stress:network` (iterações 0 e 1 passaram; a iteração 2 ficou sem
  heartbeat suficiente). Foram preservados 306 artefatos e não há processo
  QEMU residual. O baseline continua ausente; a aprovação permanece bloqueada
  pelas falhas reais dos dois casos.

- TST7: rodada `quick` pós-correção executada em 2026-09-01 00:33 UTC
  (2026-08-31 21:33 America/Sao_Paulo), no run
  `.tst7-results/tst7-20260901T003336Z-11788/`. Os sete passos terminaram sem
  loop: seis `PASS` e `test-tst3-sanitize` `BLOCKED` por `permission denied`
  no Clang configurado. O runner encerrou com `status=BLOCKED` e preservou
  `manifest.json`, `result.json`, `coverage.json`, `summary.md`, logs e
  `artifact-index.json`.

- Politica de manutencao continua do testador adicionada ao `AGENTS.md` em
  2026-08-31 23:31 (America/Sao_Paulo). A Regra #22 exige identificar e manter
  atualizada a camada de teste afetada por mudancas de comportamento,
  contrato, estado, erro, API, driver, Shell ou ferramenta; exige registrar
  cobertura ausente como `PENDING`/`BLOCKED`; preserva falhas reais; e mantem
  timeouts, limites e aprovacao explicita do baseline TST7. Refatoracoes sem
  mudanca de comportamento continuam obrigadas a executar os testes existentes,
  mas nao a criar alteracoes artificiais no testador.

- TST6: revalidacao final executada em 2026-08-31 23:53
  (America/Sao_Paulo), depois de `make q3check`, `make clean`, `make` e
  `make test-qemu-selftest`. `make test-tst6-host` passou com 11 testes e
  `make catalog-test` confirmou 6.808 superficies e 36 casos. Os 20 alvos
  QEMU foram executados individualmente, sem retry automatico: os oito casos
  de matriz, os quatro casos de stress com oito iteracoes e os oito casos de
  falha/recuperacao. Todos terminaram com `READY -> HEARTBEAT -> BEGIN -> PASS`,
  `status=PASS` e `last_state=PASS`; nao houve timeout, erro de protocolo ou
  processo QEMU residual.

  Os artefatos desta rodada ficaram preservados em:
  `build/test-results/qemu-20260901T024044Z-28268`,
  `qemu-20260901T024121Z-28096`, `qemu-20260901T024143Z-22384`,
  `qemu-20260901T024205Z-26916`, `qemu-20260901T024228Z-10512`,
  `qemu-20260901T024303Z-26276`, `qemu-20260901T024324Z-23992`,
  `qemu-20260901T024346Z-28648`, `qemu-20260901T024418Z-27312`,
  `qemu-20260901T024453Z-25644`, `qemu-20260901T024730Z-28616`,
  `qemu-20260901T024809Z-1984`, `qemu-20260901T024857Z-5828`,
  `qemu-20260901T024921Z-10792`, `qemu-20260901T024955Z-22132`,
  `qemu-20260901T025029Z-27304`, `qemu-20260901T025052Z-6140`,
  `qemu-20260901T025113Z-20660`, `qemu-20260901T025134Z-3964` e
  `qemu-20260901T025156Z-25816`. A falha intermitente de watchdog registrada
  na execucao TST7 anterior nao se reproduziu nesta rodada; ela permanece
  registrada como diagnostico historico e nao foi apagada.

- TST7: concluida em 2026-09-01 11:12 (America/Sao_Paulo). `make
  test-tst7-host` passou com 14 testes, incluindo a deteccao de mutacoes e a
  regra de que a duracao de etapas de preparacao do host nao e uma regressao de
  caso. A comparacao foi ajustada para considerar duracao somente de casos
  QEMU; a aprovacao passou a recusar relatorios com comparacao reprovada. O
  runner tambem passou a aguardar 1 segundo entre processos QEMU consecutivos,
  sem retry ou repeticao de caso.

  O primeiro `full` elegivel foi aprovado explicitamente em
  `2026-09-01T13:01:38Z`, no run
  `.tst7-results/tst7-20260901T124115Z-19420/`, com todas as etapas e 36/36
  casos QEMU `PASS`; o manifesto aprovado esta em
  `tests/baselines/tst7-approved.json`. A validacao final contra esse baseline
  foi o run `.tst7-results/tst7-20260901T135144Z-12828/`, retornou `PASS` e
  confirmou `comparison=PASS`, sem regressao de contrato, warnings, cobertura
  ou duracao. Os seeds foram deterministas, cada caso teve uma tentativa, e
  os 36 casos produziram os artefatos individuais preservados pelo runner.
  O `quick` posterior a aprovacao, no run
  `.tst7-results/tst7-20260901T141558Z-3500/`, tambem retornou `PASS` com os
  sete gates e a comparacao contra o baseline.
  Nao ha processo QEMU residual.

- Fechamento de cobertura e supervisor TST7 iniciado em 2026-09-01 12:39
  (America/Sao_Paulo). O teste host-only do protocolo foi ampliado para
  exercitar diretamente todas as APIs publicas do nucleo, incluindo estados
  nao inicializado, ticks, motivo de falha e emissao de eventos. `make
  test-tst2-host` passou; `make test-tst3-host` passou depois de concluir os
  testes de strings, compressao, packager e updater.

  O validador passou a exigir vinculos bidirecionais, `coverage_mode`, casos
  automatizados e registro declarativo. O catalogo foi sincronizado sem
  inferir cobertura por arquivo: passou a registrar 179 superficies `COVERED`,
  6.629 `PENDING` e 38 casos. `make catalog-test`, `make test-qemu-selftest`,
  `make q3check`, `make test-tst7-host` e
  `make test-tst7-continuous-host` passaram.

  `make catalog-test-strict` foi executado e falhou pelos `PENDING` reais,
  preservando a pendencia em vez de mascarar a lacuna. O TST7 agora aceita o
  modo `soak`, limitado aos quatro casos de estresse TST6; `full` e `soak`
  podem exigir o gate estrito. O supervisor possui ciclos finitos, modo
  permanente explicito, watchdog, parada por Ctrl+C/arquivo e entrada Linux
  `tools/tst7-continuous`.

- Fechamento da validacao da infraestrutura de cobertura executado em:
  2026-09-01 12:52 (America/Sao_Paulo). O validador passou a verificar cada
  vinculo explicito de uma entrada do registro, inclusive quando ha varias
  superficies, e rejeita um registro invalido antes de alterar o catalogo.
  A suite conjunta passou com 32 testes Python; `make catalog-test` confirmou
  6.808 superficies e 38 casos; `make q3check` passou; e os alvos host TST7
  passaram com 17 e 7 testes.

  O `make test-tst7-quick` de 2026-09-01 12:41 terminou como `BLOCKED` somente
  porque `test-tst3-sanitize` nao conseguiu executar o Clang configurado
  (`permission denied`), preservando o run
  `.tst7-results/tst7-20260901T154116Z-25344/`. O supervisor foi exercitado
  em dois ciclos `full` finitos; ambos terminaram `FAIL` no gate estrito pela
  primeira superficie `PENDING`, preservando os dois runs e agrupando a falha
  em `.tst7-results/continuous/session-20260901T154338Z-16108/`. A cobertura
  permanece em 179 `COVERED` e 6.629 `PENDING`; a TST7 integral continua
  pendente ate os harnesses reais dos lotes restantes serem implementados.

- Revalidacao final dos runners executada em: 2026-09-01 13:01
  (America/Sao_Paulo). `make test-qemu-selftest` passou com o self-test e 12
  testes unitarios; `make test-tst7-host` passou com 19 testes;
  `make test-tst7-continuous-host` passou com 7 testes; `make q3check` e
  `make catalog-test` passaram. A allowlist de fixtures foi aplicada tanto no
  runner QEMU quanto no TST7, e casos host automatizados sem alvo agora sao
  reportados como inconsistencias, nunca ignorados.

- Build limpo revalidado em 2026-09-01 13:04 (America/Sao_Paulo): `make clean`
  e `make` terminaram com sucesso, gerando `build/zephyros.img` de 256 MiB.
  O compilador emitiu warnings preexistentes do código freestanding, mas não
  houve erro de compilação ou linkedição. O gate estrito continua reprovando
  os 6.629 vínculos de cobertura ainda `PENDING`; essa evidência não altera
  artificialmente o status do catálogo.

- Bateria Core/contratos ampliada em 2026-09-01: `make test-crypto-host` e
  `make test-scheduling-host` passaram com `HOST_CC` configurado. Crypto
  exercitou SHA-256, SHA-512, Ed25519 válido pelo autoteste, assinatura
  corrompida, entradas nulas e comparação constante. Scheduling exercitou
  filas de espera, disponibilidade, wake FIFO/all, limites, workqueue,
  coalescência, rerun, cancelamento, fallback e IRQ deferred, incluindo
  invariantes e limpeza.

  Os relatórios instrumentados `build/test-results/crypto-host/coverage.json`
  e `build/test-results/scheduling-host/coverage.json` terminaram `PASS`,
  sem endereços desconhecidos ou ambíguos. O catálogo foi sincronizado a
  partir desses relatórios: 6.820 superfícies, 2.006 `COVERED`, 4.814
  `PENDING` e 44 casos. `make catalog-test` permaneceu válido; o gate estrito
  continua pendente pelas superfícies reais ainda não exercitadas.

- Incremento Core/pacotes em 2026-09-01: `make test-package-host` passou com
  `HOST_CC` configurado. O caso exercitou comparação de versões, estados
  indisponíveis, histórico, failpoints e nomes canônicos sem escrita em disco.
  A compilação revelou e corrigiu três acessos a membros `packed` que geravam
  `-Waddress-of-packed-member` sob `-Werror`; o layout persistido foi mantido.
  O relatório instrumentado `build/test-results/package-host/coverage.json`
  terminou `PASS`, sem endereços desconhecidos ou ambíguos. Após a
  sincronização: 6.820 superfícies, 2.013 `COVERED`, 4.807 `PENDING` e 45
  casos; `make catalog-test` permaneceu válido e o gate estrito segue pendente.

- Incremento Core/estado em 2026-09-01: `make test-state-host` passou com
  `HOST_CC` configurado. O caso exercitou recovery antes/depois da
  inicialização, estados `READY`, `DEGRADED` e `DISABLED`, IDs inválidos,
  cadeia de notificadores na ordem esperada, duplicata, capacidade,
  participante opcional indisponível, falha obrigatória e timeout.
  O relatório instrumentado `build/test-results/state-host/coverage.json`
  terminou `PASS`, sem endereços desconhecidos ou ambíguos. Após a
  sincronização: 6.820 superfícies, 2.046 `COVERED`, 4.774 `PENDING` e 46
  casos. `make catalog-test`, `q3check` e o build limpo permaneceram válidos;
  `catalog-test-strict` continua pendente pelas superfícies sem executor real.
- Incremento Core/App Store em 2026-09-01: `make test-app-catalog-host` passou
  com `HOST_CC` configurado. O caso exercitou fontes validas e invalidas,
  aliases, dependencias instaladas e ausentes, planos de instalacao e
  atualizacao, downgrade, ciclo, erros de leitura e limite de fontes. O
  relatorio instrumentado `build/test-results/app-catalog-host/coverage.json`
  terminou `PASS` e foi usado para sincronizar o catalogo sem associacao por
  arquivo. O estado desta rodada e 6.820 superficies, 2.199 `COVERED`, 4.621
  `PENDING` e 49 casos; o gate estrito continua falhando pelas superficies
  ainda sem executor real.

- Incremento de contrato do registro em 2026-09-01: quando um caso possui
  varios relatorios dinamicos, o validador passou a unir somente as superficies
  observadas por cada relatorio, mantendo a correspondencia bidirecional e
  rejeitando selecoes ausentes, desconhecidas ou ambiguas. A cobertura do
  catalogo foi sincronizada novamente e `make catalog-test` permaneceu valido.
- Incremento Core/entrada em 2026-09-01: `make test-input-host` passou com
  `HOST_CC` configurado. O caso exercitou registro idempotente de sinks,
  filas de teclado e ponteiro, coalescencia e saturacao de deltas, overflow,
  despacho alternado, erro de consumidor e invariantes. O relatorio
  instrumentado `build/test-results/input-host/coverage.json` terminou `PASS`
  e foi sincronizado no catalogo. O estado desta rodada e 6.820 superficies,
  2.208 `COVERED`, 4.612 `PENDING` e 50 casos; o gate estrito continua
  pendente pelas superficies sem executor real.
- Incremento Core/energia em 2026-09-01: `make test-power-host` passou com
  `HOST_CC` configurado. O caso exercitou inicializacao ACPI disponivel e
  indisponivel, cadeia de notificadores, quiescencia, falhas de sync e S5,
  limpeza apos falha, encerramento simulado e estados publicados. O relatorio
  instrumentado `build/test-results/power-host/coverage.json` terminou `PASS`;
  os caminhos de reset, triple fault e halt terminal permanecem reservados a
  QEMU controlado. A sincronizacao deixou 6.820 superficies, 2.240
  `COVERED`, 4.580 `PENDING` e 51 casos.
- Incremento Core/rede em 2026-09-01: `make test-network-manager-host` passou
  com `HOST_CC` configurado. O caso exercitou inventario PCI simulado,
  identificacao de driver ausente, estado offline, recusas canonicas sem
  interface ativa, formatacao, nomes e limpeza. O relatorio instrumentado
  `build/test-results/network-manager-host/coverage.json` terminou `PASS` e
  foi sincronizado no catalogo. A rodada ficou em 6.820 superficies, 2.304
  `COVERED`, 4.516 `PENDING` e 52 casos; o gate estrito continua reprovando
  pelas superficies que ainda nao possuem executor real.
- Incremento Core/relogio em 2026-09-01: `make test-core-host` foi reforcado
  com RTC falso disponivel, conversao UTC do epoch, divisao de ticks e
  validacao monotona. O relatorio dinamico atualizado cobriu tambem os seis
  contratos publicos e as rotinas privadas antes pendentes. A sincronizacao
  ficou em 6.820 superficies, 2.312 `COVERED`, 4.508 `PENDING` e 52 casos.
- Incremento Storage/VFS em 2026-09-01 21:33 (America/Sao_Paulo):
  `make test-vfs-path-host` e `make test-file-index-host` passaram com
  `HOST_CC` configurado. Os casos exercitaram paths, aliases, mounts, cwd,
  listagens, quiescencia, pesquisa, rebuild cooperativo, cancelamento,
  resultados stale/missing, corrupcao de candidato e corrupcao da tabela
  ativa com recuperacao. Os relatorios instrumentados
  `build/test-results/vfs-path-host/coverage.json` e
  `build/test-results/file-index-host/coverage.json` terminaram `PASS`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao deixou 6.820
  superficies, 2.334 `COVERED`, 4.486 `PENDING` e 54 casos; `make
  catalog-test` passou e `catalog-test-strict` continua pendente pelas
  superficies de outros lotes ainda sem executor real.
- Incremento Memoria/SLAB em 2026-09-01 22:49 (America/Sao_Paulo):
  `make test-slab-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture host-only exercitou inicializacao idempotente, limites
  de criacao, duplicidade, metadados por indice, estatisticas, ownership
  nulo, validacao e destruicao sem alocar paginas reais. O teste revelou e
  corrigiu truncamento de ponteiros para `uint32_t` em hosts 64-bit; a
  aritmetica interna passou a usar `uint64_t`, preservando o caminho
  freestanding. O relatorio `build/test-results/slab-host/coverage.json`
  terminou `PASS`, sem enderecos desconhecidos ou ambiguos. A sincronizacao
  deixou 6.820 superficies, 2.638 `COVERED`, 4.182 `PENDING` e 61 casos;
  `make catalog-test` passou. `catalog-test-strict` continua pendente pelas
  superficies de outros lotes ainda sem executor real.
- Incremento Core/scheduling em 2026-09-01:
  `make test-core-host` e `make test-scheduling-host` passaram com
  `HOST_CC` configurado. O caso de scheduling passou a exercitar o notifier
  de IRQ deferred, cancelamento e snapshot de trabalhos, quiescencia com
  deadline do relogio falso e restauracao da fila. O fixture do protocolo
  tambem passou a inicializar explicitamente seu estado, removendo dependencia
  de conteudo residual da stack. O relatorio
  `build/test-results/scheduling-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao deixou 6.820
  superficies, 2.632 `COVERED`, 4.188 `PENDING` e 60 casos; `make
  catalog-test` passou. O gate estrito continua pendente pelas superficies de
  outros lotes ainda sem executor real.
- Incremento Storage/FS em 2026-09-01 21:33 (America/Sao_Paulo):
  `make test-fs-host` passou com `HOST_CC` configurado. A suite exercitou
  paths legacy e de storage em fixtures FAT12/FAT32, cursores com cadeia FAT,
  leitura por faixa, mutacoes, operacoes atomicas, streaming, selecao do
  volume de sistema, geracao e erros canonicos. O relatorio instrumentado
  `build/test-results/fs-host/coverage.json` terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos. A sincronizacao deixou 6.820 superficies,
  2.395 `COVERED`, 4.425 `PENDING` e 55 casos; `make catalog-test` passou e
  `catalog-test-strict` continua pendente pelas superficies de outros lotes
  ainda sem executor real.
- Incremento Storage/backend em 2026-09-01 22:03 (America/Sao_Paulo):
  `make test-storage-host` passou com `HOST_CC` configurado. A suite usou uma
  imagem FAT12 estatica e um provider de bloco falso para exercitar MBR/BPB,
  inventario, mount, aliases, cursores, leitura, espaco livre, estados
  somente-leitura e limpeza, sem escrita real. O relatorio
  `build/test-results/storage-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao deixou 6.820
  superficies, 2.473 `COVERED`, 4.347 `PENDING` e 56 casos; `make
  catalog-test` passou.
- Incremento Storage/BIO em 2026-09-01 22:03 (America/Sao_Paulo):
  `make test-block-host` passou com `HOST_CC` configurado. O caso executou os
  autotestes reais de BIO e block-cache, incluindo limites, cancelamento,
  failpoints, fusao/FIFO, eviction, dirty/writeback, sync e restauracao do
  inventario. Durante a compilacao foram corrigidos um warning de variavel
  residual em `block.c` e uma checagem de overflow impossivel para `uint8_t`
  em `block_cache.c`, mantendo a validacao em aritmetica de 64 bits. O
  relatorio `build/test-results/block-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao deixou 6.820
  superficies, 2.479 `COVERED`, 4.341 `PENDING` e 57 casos; `make
  catalog-test` passou e `catalog-test-strict` continua pendente pelas
  superficies de outros lotes ainda sem executor real.
- Incremento Storage/FAT12 em 2026-09-01 22:09 (America/Sao_Paulo):
  `make test-fat12-host` passou com `HOST_CC` configurado. A suite usou uma
  imagem FAT12 estatica com raiz e subdiretorio para exercitar leitura, paths,
  metadados, listagem, operacoes atomicas, streaming, cancelamento e erros de
  nome/tamanho. A compilacao revelou a colisao do helper privado `strncmp`
  com o builtin da libc sob `-Werror`; ele foi renomeado para
  `fat12_strncmp`, sem alterar a API publica. O relatorio
  `build/test-results/fat12-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao deixou 6.820
  superficies, 2.543 `COVERED`, 4.277 `PENDING` e 58 casos; `make
  catalog-test` passou e `catalog-test-strict` continua pendente pelas
  superficies de outros lotes ainda sem executor real.
- Incremento Storage/FAT32 em 2026-09-01 22:09 (America/Sao_Paulo):
  `make test-fat32-host` passou com `HOST_CC` configurado. A suite usou uma
  imagem FAT32 estatica com mais de 4.085 clusters para exercitar classificacao
  de clusters, leitura, paths, metadados, criacao, escrita, remocao e limites.
  A compilacao revelou a mesma colisao de helper privado `strncmp` com o
  builtin da libc sob `-Werror`; ele foi renomeado para `fat32_strncmp`, sem
  alterar a API publica. O relatorio
  `build/test-results/fat32-host/coverage.json` terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos. A sincronizacao deixou 6.820 superficies,
  2.584 `COVERED`, 4.236 `PENDING` e 59 casos; `make catalog-test` passou.
- Incremento Storage/VFS em 2026-09-01 22:09 (America/Sao_Paulo):
  `make test-vfs-host` passou com `HOST_CC` configurado. A fixture exercitou
  tabelas de descritores, arquivos regulares, dispositivos, pipes, sockets,
  poll/select, fsync/sync, quiescencia, limites e validacao de invariantes sem
  hardware ou armazenamento real. O build host recebeu apenas a protecao do
  `sti` legado durante `ZEPHYROS_HOST_TEST`, preservando o caminho freestanding.
  O relatorio `build/test-results/vfs-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao deixou 6.820
  superficies, 2.608 `COVERED`, 4.212 `PENDING` e 60 casos; `make
  catalog-test` passou e `catalog-test-strict` continua pendente pelas
  superficies de outros lotes ainda sem executor real.
- Incremento Memoria/SLAB e Core/scheduling em 2026-09-01:
  `make test-slab-host` e `make test-scheduling-host` passaram com `HOST_CC`
  configurado e warnings tratados como erro. A fixture SLAB cobriu
  inicializacao, metadados, limites, duplicidade, estatisticas e limpeza sem
  paginas reais. A fixture de scheduling passou a cobrir `wait_event`,
  snapshots de filas e waiters e a prova controlada da kworker, incluindo os
  callbacks de condicao e execucao; `workqueue_worker_main` permanece
  pendente por ser um loop de servico infinito. Os relatorios instrumentados
  terminaram `PASS`, sem enderecos desconhecidos ou ambiguos. A sincronizacao
  atual registra 6.820 superficies, 2.654 `COVERED`, 4.166 `PENDING` e 61
  casos; `make catalog-test` e `make q3check` passaram.
- Incremento Core/timer em 2026-09-01:
  `make test-timer-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa stubs de IDT, PIC e scheduler para exercitar
  inicializacao idempotente, conversao de milissegundos, handles, timers
  one-shot e periodicos, notifier, dispatch, cancelamento, callbacks com
  erro, snapshots, limites e destruicao de proprietarios. O caminho host nao
  executa instrucoes privilegiadas e o caminho freestanding permanece
  inalterado. O relatorio `build/test-results/timer-host/coverage.json`
  terminou `PASS`, sem enderecos desconhecidos ou ambiguos. A sincronizacao
  deixou 6.820 superficies, 2.667 `COVERED`, 4.153 `PENDING` e 62 casos;
  `make catalog-test` passou. Nenhuma superficie de timer permanece pendente.
- Incremento Rede/UDP em 2026-09-01:
  `make test-udp-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa um transporte IPv4 falso para exercitar envio,
  reinjecao, checksum, listeners, broadcast, callback recusado, comprimentos
  invalidos, payload fora do limite e limpeza de endpoints, sem conexao
  externa. O relatorio `build/test-results/udp-host/coverage.json` terminou
  `PASS`, sem enderecos desconhecidos ou ambiguos. A sincronizacao deixou
  6.820 superficies, 2.682 `COVERED`, 4.138 `PENDING` e 63 casos; `make
  catalog-test` passou. Nenhuma superficie de `udp.c` permanece pendente.
- Incremento Rede/ARP em 2026-09-02:
  `make test-arp-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa Ethernet falsa e relogio deterministico para
  exercitar configuracao, validacao de IPv4 e MAC, cache, retries ate timeout,
  requests, replies, pacotes invalidos e limpeza. O relatorio
  `build/test-results/arp-host/coverage.json` terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos. A sincronizacao registra 6.820 superficies,
  2.724 `COVERED`, 4.096 `PENDING` e 64 casos; `make catalog-test` passou.
Nenhuma superficie de `arp.c` permanece pendente.

O lote Rede/ICMP adicionou `host:network:icmp` e o alvo
`make test-icmp-host`. A fixture usa IPv4 e timer falsos para exercitar
configuracao, checksum, echo request/reply, RTT, timeout, mudanca de
configuracao, fila pendente, pacotes invalidos e falhas de transporte, sem
conexao externa. `make test-icmp-host` passou com `HOST_CC` configurado e
warnings tratados como erro; o relatorio instrumentado
`build/test-results/icmp-host/coverage.json` terminou `PASS`, sem enderecos
desconhecidos ou ambiguos. A sincronizacao atual registra 6.820 superficies,
2.748 `COVERED`, 4.072 `PENDING` e 65 casos; `make catalog-test` e
`make q3check` passaram. Nenhuma superficie de `icmp.c` permanece pendente.

- Incremento Rede/DNS em 2026-09-02 10:57 (America/Sao_Paulo):
  `make test-dns-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa UDP, IPv4 e timer falsos para exercitar consultas,
  normalizacao de nomes, cache e expiracao, CNAME, timeout, respostas
  invalidas e falhas de transporte, sem conexao externa. O relatorio
  `build/test-results/dns-host/coverage.json` terminou `PASS`, com 58
  superficies resolvidas, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao atual registra 6.820 superficies, 2.785 `COVERED`, 4.035
  `PENDING` e 66 casos.

- Incremento Rede/DHCP em 2026-09-02 11:12 (America/Sao_Paulo):
  `make test-dhcp-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa UDP e timer falsos para exercitar descoberta,
  oferta, lease, renovacao, rebinding, expiracao, NAK, timeout, mensagens
  invalidas e falhas de transporte, sem conexao externa. O relatorio
  `build/test-results/dhcp-host/coverage.json` terminou `PASS`, com 70
  superficies resolvidas, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao atual registra 6.820 superficies, 2.841 `COVERED`, 3.979
  `PENDING` e 67 casos; `make catalog-test` passou.

- Incremento Rede/Ethernet em 2026-09-02 11:24 (America/Sao_Paulo):
  `make test-ethernet-host` passou com `HOST_CC` configurado e warnings
  tratados como erro. A fixture usa quatro interfaces, drivers, handlers e
  frames falsos para exercitar polling, entrega local e broadcast, filtragem,
  frames invalidos, erros de driver, transmissao, quiescencia, sk_buff,
  net_buffer e limpeza, sem hardware real. O relatorio
  `build/test-results/ethernet-host/coverage.json` terminou `PASS`, com 104
  superficies resolvidas, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao atual registra 6.820 superficies, 2.876 `COVERED`, 3.944
  `PENDING` e 68 casos; `make catalog-test` passou.

- Incremento Rede/TCP em 2026-09-02 11:33 (America/Sao_Paulo):
  `make test-tcp-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa IPv4 e timer falsos para exercitar handshake,
  dados, ACK, FIN, RST, retransmissao, timeout, callbacks recusados, janelas,
  limites, conexoes simultaneas e limpeza, sem rede externa. O relatorio
  `build/test-results/tcp-host/coverage.json` terminou `PASS`, com 71
  superficies resolvidas, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao atual registra 6.820 superficies, 2.920 `COVERED`, 3.900
  `PENDING` e 69 casos; `make catalog-test` passou.

- Incremento Seguranca/TLS em 2026-09-02 11:38 (America/Sao_Paulo):
  `make test-tls-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa relogio, RNG e cliente TLS falsos para exercitar
  politica, validade, cadeia, SAN, pinning, rotacao, revogacao, estados
  indisponiveis e autoteste, sem rede externa. O relatorio
  `build/test-results/tls-host/coverage.json` terminou `PASS`, com 30
  superficies resolvidas, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao atual registra 6.820 superficies, 2.931 `COVERED`, 3.889
  `PENDING` e 70 casos; `make catalog-test` passou.

- Incremento Rede/HTTP concluido em 2026-09-02 11:57 (America/Sao_Paulo):
  `make test-http-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa DNS, socket, TLS, timer e stack falsos para
  exercitar URLs e opcoes, headers, corpos Content-Length/chunked/EOF,
  streaming, redirects, HTTPS, limites, timeouts e falhas sem rede externa.
  O relatorio `build/test-results/http-host/coverage.json` terminou `PASS`,
  sem enderecos desconhecidos ou ambiguos. A sincronizacao atual registra
  6.820 superficies, 2.979 `COVERED`, 3.841 `PENDING` e 71 casos; os gates
  de catalogo e a validacao TST7 completa permanecem pendentes.

- Incremento Rede/sockets concluido em 2026-09-02 12:10 (America/Sao_Paulo):
  `make test-net-socket-host` passou com `HOST_CC` configurado e warnings
  tratados como erro. A fixture usa TCP, timer, filas de espera e VFS falsos
  para exercitar handles geracionais, conexao, filas RX/TX, eventos, EOF,
  timeout, cancelamento, limites, reset e limpeza sem rede externa. O relatorio
  `build/test-results/net-socket-host/coverage.json` terminou `PASS`, com 86
  superficies resolvidas, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao atual registra 6.820 superficies, 3.018 `COVERED`, 3.802
  `PENDING` e 72 casos; `make catalog-test` passou.

- Incremento Memoria/VMA concluido em 2026-09-02 12:26 (America/Sao_Paulo):
  `make test-vma-host` passou com `HOST_CC` configurado e warnings tratados
  como erro. A fixture usa processo ring 3, diretorio, paging, PMM e VFS
  falsos para exercitar VMAs fixas e anonimas, materializacao lazy, page
  faults validos e invalidos, `mmap`, `munmap`, limites, estatisticas e
  limpeza. O relatorio `build/test-results/vma-host/coverage.json` terminou
  `PASS`, com 34 superficies resolvidas, sem enderecos desconhecidos ou
  ambiguos. `make catalog-test`, `make q3check`, `make clean` seguido de
  `make` e `git diff --check` passaram. A sincronizacao atual registra 6.820
  superficies, 3.021 `COVERED`, 3.799 `PENDING` e 73 casos; o gate estrito e
  a validacao TST7 completa continuam pendentes.

- Incremento Memoria/paging concluido em 2026-09-02 12:56 (America/Sao_Paulo):
  o caso host-only `host:memory:paging` e o alvo `make test-paging-host` foram
  adicionados. A fixture usa PMM, VESA, processo e timer falsos com buffers
  estaticos para exercitar tabelas, diretorios, mapas de kernel e usuario,
  framebuffer, copia entre espacos, materializacao lazy, limites, overflow,
  paginas ausentes, fallback de framebuffer e limpeza completa. O relatorio
  instrumentado `build/test-results/paging-host/coverage.json` terminou
  `PASS`, sem enderecos desconhecidos ou ambiguos. `make test-paging-host`,
  `make q3check`, `make clean` seguido de `make`, `make catalog-test`,
  `make test-tst7-host` e `git diff --check` passaram. A sincronizacao atual
  registra 6.825 superficies, 3.039 `COVERED`, 3.786 `PENDING` e 74 casos;
  o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Memoria/PMM/heap concluido em 2026-09-02 13:17 (America/Sao_Paulo):
  o caso host-only `host:memory:memory` e o alvo `make test-memory-host` foram
  adicionados. A fixture usa um mapa E820 estatico e memoria de heap estatica
  para exercitar inicializacao, estatisticas, zonas, alocacoes contiguas,
  alinhamento, limites, ponteiros invalidos, double free, coalescencia,
  reutilizacao e restauracao do estado. O relatorio instrumentado
  `build/test-results/memory-host/coverage.json` terminou `PASS`, com as
  superficies de `src/memory/memory.c` resolvidas e sem enderecos desconhecidos
  ou ambiguos. `make test-memory-host`, `make q3check`, `make clean` seguido de
  `make`, `make catalog-test`, `make test-tst7-host` e `git diff --check`
  passaram. O build completo manteve somente warnings preexistentes em outros
  modulos. A sincronizacao atual registra 6.827 superficies, 3.050 `COVERED`,
  3.777 `PENDING` e 75 casos; o fechamento integral do catalogo, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Processos/sinais concluido em 2026-09-02 13:42
  (America/Sao_Paulo): o caso host-only `host:process:signals` e o alvo
  `make test-process-signal-host` foram adicionados. A fixture usa processos
  estaticos para exercitar inicializacao, nomes, acoes, mascaras, coalescencia,
  entrega a handler, `sigreturn`, terminacao padrao, notificacao `SIGCHLD`,
  snapshots, estatisticas e invariantes finais. O caminho de IRQ foi
  substituido somente no build host por um stub controlado. O relatorio
  `build/test-results/process-signal-host/coverage.json` terminou `PASS`, com
  as 28 superficies de `src/process/signal.c` resolvidas, sem enderecos
  desconhecidos ou ambiguos. `make test-process-signal-host`, `make q3check`,
  `make clean` seguido de `make`, `make catalog-test`, `make test-tst7-host` e
  `git diff --check` passaram. O build completo manteve somente warnings
  preexistentes em outros modulos. A sincronizacao atual registra 6.827
  superficies, 3.074 `COVERED`, 3.753 `PENDING` e 76 casos; o fechamento
  integral do catalogo, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Core/workqueue concluido em 2026-09-02 14:04
  (America/Sao_Paulo): o caso host-only `host:core:workqueue` e o alvo
  `make test-workqueue-host` foram adicionados. A fixture executa o autoteste
  interno e exercita callbacks, prioridades, FIFO, atrasos, coalescencia,
  rerun, cancelamento, fallback, quiescencia, worker e validacao de
  invariantes. O worker usa quatro iteracoes somente no build host para
  impedir espera indefinida. O relatorio
  `build/test-results/workqueue-host/coverage.json` terminou `PASS`, com as 59
  funcoes de `src/core/workqueue.c` resolvidas, sem enderecos desconhecidos ou
  ambiguos. `make test-workqueue-host`, `make test-tst7-host`, `make q3check`,
  `make clean`, `make`, `make catalog-test` e `git diff --check` passaram. A
  sincronizacao atual
  registra 6.827 superficies, 3.082 `COVERED`, 3.745 `PENDING` e 78 casos; o
  fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Rede/DHCP/limites concluido em 2026-09-02 14:08
  (America/Sao_Paulo): a fixture existente `host:network:dhcp` passou a enviar
  uma opcao DHCP com comprimento incompatível, exercitando o rejeito canonico
  de `dhcp_invalid_option_length` sem conexao externa. O relatorio
  instrumentado `build/test-results/dhcp-host/coverage.json` terminou `PASS`
  com as 52 funcoes de `src/core/dhcp.c` resolvidas, sem enderecos
  desconhecidos ou ambiguos. `make test-dhcp-host`, `make catalog-test` e
  `git diff --check` passaram. A sincronizacao atual registra 6.827 superficies,
  3.083 `COVERED`, 3.744 `PENDING` e 78 casos; o fechamento integral do
  catalogo, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/BearSSL compat concluido em 2026-09-02 14:15
  (America/Sao_Paulo): o novo caso host-only `host:core:bearssl-compat` e o
  alvo `make test-bearssl-compat-host` foram adicionados. A fixture exercita
  diretamente `memcpy`, `memmove`, `memset`, `memcmp` e `strlen`, incluindo
  sobreposicao, comparacao ordenada, preenchimento, buffers vazios e entrada
  nula. O relatorio instrumentado
  `build/test-results/bearssl-compat-host/coverage.json` terminou `PASS` com
  as 2 superficies pendentes de `src/core/bearssl_compat.c` resolvidas, sem
  enderecos desconhecidos ou ambiguos. `make test-bearssl-compat-host`,
  `make catalog-test` e `git diff --check` passaram. A sincronizacao atual
  registra 6.827 superficies, 3.085 `COVERED`, 3.742 `PENDING` e 79 casos; o
  fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Shell/dispatcher concluido em 2026-09-02
  (America/Sao_Paulo): o novo caso host-only `host:shell:dispatch` e o alvo
  `make test-shell-dispatch-host` foram adicionados. A fixture exercita o
  caminho real de `shell_dispatch_execute()` para diagnostico de comando
  desconhecido, entrada com espacos e escape, limite de 31 caracteres,
  encaminhamento de comando conhecido e entrada nula. O relatorio instrumentado
  `build/test-results/shell-dispatch-host/coverage.json` terminou `PASS` e
  resolveu a superficie estatica `shell_dispatch_print_unknown`, sem enderecos
  desconhecidos ou ambiguos. `make test-shell-dispatch-host`, a regeneracao dos
  relatorios host-only, `make catalog-test` e `git diff --check` passaram. A
  sincronizacao atual registra 6.827 superficies, 3.086 `COVERED`, 3.741
  `PENDING` e 80 casos; o fechamento integral do catalogo, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Shell/introspeccao concluido em 2026-09-02
  (America/Sao_Paulo): o novo caso host-only `host:shell:introspection` e o
  alvo `make test-shell-introspection-host` foram adicionados. A fixture chama
  o parser hexadecimal real para valores numericos, minusculos, maiusculos e
  `uint32_t` maximo, alem de prefixos, digitos invalidos, entrada nula e
  overflow. O relatorio instrumentado
  `build/test-results/shell-introspection-host/coverage.json` terminou `PASS`
  e resolveu as superficies `shell_introspection_hex_digit` e
  `shell_introspection_parse_hex_u32`, sem enderecos desconhecidos ou
  ambiguos. `make test-shell-introspection-host`, `make catalog-test` e a
  renderizacao da visao passaram. A sincronizacao atual registra 6.827
  superficies, 3.088 `COVERED`, 3.739 `PENDING` e 81 casos; o fechamento
  integral do catalogo, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Core/Crypto concluido em 2026-09-02
  (America/Sao_Paulo): a fixture existente de `host:core:crypto` passou a
  exercitar diretamente `crypto_eddsa_trim_scalar`, verificando copia,
  mascaras de bits e os limites do scalar. A funcao `fe_cswap` foi removida e
  registrada como `RETIRED` porque nao havia referencias no codigo ativo;
  `fe_ccopy` permanece como substituto utilizado pelo caminho Ed25519.
  `make test-crypto-host`, a sincronizacao do catalogo, `make catalog-test` e
  o build limpo passaram. A sincronizacao atual registra 6.826 superficies,
  3.089 `COVERED`, 3.737 `PENDING`, 81 casos e uma superficie aposentada; o
  fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Processos/IPC concluido em 2026-09-02 13:52
  (America/Sao_Paulo): o caso host-only `host:process:ipc` e o alvo
  `make test-process-ipc-host` foram adicionados. A fixture usa processos,
  filas e wait falsos para exercitar inicializacao, mensagens validas e
  invalidas, fila cheia, recebimento, espera com timeout e sinal, foco,
  fallback, restauracao e limpeza. O relatorio
  `build/test-results/process-ipc-host/coverage.json` terminou `PASS`, com as
  14 superficies de `src/process/ipc.c` resolvidas, sem enderecos
  desconhecidos ou ambiguos. `make test-process-ipc-host`,
  `make test-tst7-host`, `make catalog-test` e `git diff --check` passaram. A
  sincronizacao atual registra 6.827 superficies, 3.080 `COVERED`, 3.747
  `PENDING` e 77 casos; o fechamento integral do catalogo, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Drivers/Fonte concluido em 2026-09-02
  (America/Sao_Paulo): o novo caso host-only `host:drivers:font` e o alvo
  `make test-font-host` foram adicionados. A fixture exercita diretamente
  `font_init`, `font_get_width` e `font_get_height`, verifica inicializacao
  idempotente e dimensoes 8x16, e produz cobertura instrumentada sem hardware.
  A compilacao encontrou uma comparacao de `char` incompatível com
  `-Werror=type-limits`; a condicao foi ajustada para preservar o comportamento
  para caracteres validos. `make q3check`, `make clean`, `make`,
  `make test-font-host`, a regeneracao do quick do TST7, `make catalog-test` e
  `git diff --check` passaram. O quick terminou `BLOCKED` somente em
  `tst3-sanitize`, pela ausencia de Clang/runtime sanitizador, e os demais
  alvos passaram. A sincronizacao atual registra 6.826 superficies, 3.095
  `COVERED`, 3.731 `PENDING`, 82 casos e 21 superficies aposentadas.

- Incremento Drivers/RTC concluido em 2026-09-02 15:30
  (America/Sao_Paulo): o novo caso host-only `host:drivers:rtc-status` e o
  alvo `make test-rtc-status-host` foram adicionados. A fixture exercita
  `rtc_get_status` com destino nulo, estado inicial zerado e leituras repetidas
  sem mutacao; o relatorio instrumentado
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos. A compilacao host revelou que `asm`
  desnudo nao e aceito com `-std=c11`; a forma GNU equivalente `__asm__` foi
  usada em `rtc.c`, preservando a instrucao e a ABI. A alteracao do sincronizador
  passou a reaplicar definicoes declarativas de casos do registro antes de
  vincular a cobertura, mantendo o catalogo reproduzivel. As rotinas CMOS
  privilegiadas nao foram marcadas como cobertas. A sincronizacao atual registra
  6.826 superficies, 3.097 `COVERED`, 3.729 `PENDING` e 83 casos.

- Incremento Shell/entrada concluido em: 2026-09-02 15:45
  (America/Sao_Paulo): o caso host-only `host:shell:input` e o alvo
  `make test-shell-input-host` foram adicionados. A fixture passou com terminal
  hospedado, historico, navegacao, edicao, rolagem, cancelamento, bloqueio,
  modificadores e limite do buffer, sem hardware real. O relatorio
  `build/test-results/shell-input-host/coverage.json` terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos, e resolveu as superficies exercitadas
  de `src/shell/shell_input.c`. A sincronizacao do catalogo registra 6.826
  superficies, 3.679 `COVERED`, 3.147 `PENDING` e 84 casos. O teste unitario
  do sincronizador tambem confirmou o vinculo seguro entre API publica e
  implementacao C observada em subdiretorio diferente.

- Incremento Shell/utilitarios concluido em: 2026-09-02 16:00
  (America/Sao_Paulo): o caso host-only `host:shell:command-utils` e o alvo
  `make test-shell-command-utils-host` foram adicionados. A fixture passou com
  parsing de tokens e argumentos, comparacao de subcomandos, normalizacao,
  conversao numerica, limites, entradas invalidas e formatacao
  decimal/hexadecimal, sem hardware real. O relatorio
  `build/test-results/shell-command-utils-host/coverage.json` terminou `PASS`,
  sem enderecos desconhecidos ou ambiguos, e resolveu as funcoes e APIs de
  `src/shell/shell_command_utils.c`. O teste do catalogo confirmou a descoberta
  de declaracoes e definicoes com retorno por ponteiro. A sincronizacao atual
  registra 7.197 superficies, 3.793 `COVERED`, 3.404 `PENDING` e 85 casos.

- Incremento Core/nomes e erros concluido em: 2026-09-02 16:09
  (America/Sao_Paulo): `make test-core-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`. A fixture passou a validar explicitamente `clock_source_name`
  para RTC, ausencia e fonte desconhecida, e `log_level_str` para niveis validos
  e invalidos. Uma execucao `RUN` rejeitada tambem confirmou `core_error_name`
  pelo evento `FAIL` com `ERR_INVALID`. O relatorio
  `build/test-results/core-host/coverage.json` terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos, e a sincronizacao vinculou as tres superficies por
  chamada real. O catalogo registra 7.197 superficies, 3.884 `COVERED`, 3.313
  `PENDING` e 85 casos.

- Incremento de acessores Core, memoria, paging, rede e fonte concluido em:
  2026-09-02 16:24 (America/Sao_Paulo): `make test-font-host`,
  `make test-memory-host`, `make test-paging-host` e `make test-tst3-host`,
  todos com `HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, terminaram `PASS`.
  As fixtures verificaram `font_get_glyph`, `pmm_alloc_page`,
  `paging_get_page`, `ipv4_protocol_name` e `compress_get_stats`. Os relatorios
  instrumentados foram sincronizados sem enderecos desconhecidos ou ambiguos,
  com vinculos C diretos no catalogo. O catalogo registra 7.197 superficies,
  3.890 `COVERED`, 3.307 `PENDING` e 85 casos.

- Incremento Storage/FAT32 concluido em 2026-09-02: o caso existente
  `host:storage:fat32` passou a executar diretamente `fat32_write_file` e
  `fat32_read_file` sobre a imagem FAT32 estatica, verificando round-trip,
  remocao e preservacao da limpeza. `make test-fat32-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`; a cobertura
  instrumentada foi sincronizada sem enderecos desconhecidos ou ambiguos.
  O catalogo agora registra 7.197 superficies, 3.892 `COVERED`, 3.305
  `PENDING` e 85 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Storage/BIO concluido em 2026-09-02: `make test-block-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`. A fixture
  exercitou por contrato real os callbacks ATA do inventario, o despacho
  assincrono de BIO, a leitura fisica usada no writeback parcial e os caminhos
  de espera do block-cache, incluindo retorno deterministico `ERR_TIMEOUT`
  para reentrada durante leitura. A cobertura instrumentada foi sincronizada
  sem enderecos desconhecidos ou ambiguos. O catalogo registra 7.197
  superficies, 3.898 `COVERED`, 3.299 `PENDING` e 85 casos; o fechamento
  integral e o gate estrito continuam pendentes.

- Incremento Storage/FAT12 concluido em 2026-09-02: `make test-fat12-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`. A fixture
  exercitou as APIs legadas de escrita e remocao na raiz, escrita e remocao
  em subdiretorio e criacao de entradas de diretorio sobre a imagem FAT12
  falsa. O teste detectou e corrigiu a gravacao inconsistente de nomes 8.3
  em `fat12_write_file`; o helper privado `to_upper`, que nao possuia
  chamadores, foi removido. A evidencia instrumentada foi sincronizada sem
  enderecos desconhecidos ou ambiguos. O catalogo agora registra 7.196
  superficies, 3.903 `COVERED`, 3.293 `PENDING` e 85 casos; o fechamento
  integral e o gate estrito continuam pendentes.

- Incremento Storage/VFS concluido em 2026-09-02: a fixture host-only de VFS
  passou a exercitar stdin com mensagem de teclado, callbacks nao suportados,
  poll, pipes sem leitores, pipe cheio, socket sem poll e redirecionamento de
  escrita com limites, caminhos invalidos e limpeza. O autoteste real
  `qemu:tst4:storage-vfs` tambem passou a verificar abertura, escrita e poll
  da fixture e as operacoes invalidas de stdin/stdout. `make q3check`,
  `make clean`, `make`, `make test-tst7-quick` (com `tst3-sanitize`
  `BLOCKED` pela permissao do runtime LLVM), `make test-tst4-qemu-storage-vfs`
  e `make catalog-test` foram executados; os gates de codigo, build, QEMU e
  catalogo passaram nos respectivos criterios. A execucao QEMU normal
  `qemu-20260902T201006Z-27872` produziu `READY`, `HEARTBEAT`, `BEGIN` e
  `PASS` em uma iteracao. A imagem instrumentada `cov-tst4-storage-6`
  tambem passou com limites finitos ampliados para a sobrecarga da cobertura;
  `coverage_collector.py` resolveu 595 superficies sem enderecos desconhecidos
  ou simbolos ambiguos. O catalogo registra 7.196 superficies, 3.921
  `COVERED`, 3.275 `PENDING` e 85 casos. O fechamento integral, o gate estrito
  e o baseline TST7 continuam pendentes.

- Incremento Core/app_files concluido em 2026-09-02: o novo caso
  `host:core:app-files` e o alvo `make test-app-files-host` usam VFS falsa para
  exercitar pre-condicoes antes da inicializacao, inicializacao idempotente,
  todas as operacoes de arquivo, validacao de saidas, limites e propagacao de
  erros canonicos. A fixture instrumentada terminou `PASS` e o relatorio
  `build/test-results/app-files-host/coverage.json` resolveu 33 superficies
  reais, incluindo todas as funcoes de `src/core/app_files.c`. Foram executados
  `make q3check`, `make clean`, `make`, `make test-app-files-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e `make catalog-test`, todos com
  sucesso. O catalogo registra 7.196 superficies, 3.953 `COVERED`, 3.243
  `PENDING` e 86 casos. O fechamento integral, o gate estrito e o baseline TST7
  continuam pendentes.

- Incremento Core/app_builtin concluido em 2026-09-02: o novo caso
  `host:core:app-builtin` e o alvo `make test-app-builtin-host` usam loader
  falso para validar cabecalhos, limites e entradas das imagens ZAPP de Echo,
  ArgTest, Uptime, Mem, PathTest, DevTest e OutputTest. A fixture tambem
  exercita pre-condicoes do loader, propagacao de erros, codigo reservado de
  cancelamento e saidas nulas. A execucao instrumentada terminou `PASS` e o
  relatorio `build/test-results/app-builtin-host/coverage.json` resolveu 61
  superficies reais, incluindo todas as funcoes de `src/core/app_builtin.c`.
  `make test-tst7-quick HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` confirmou
  `PASS` em todos os casos host-only, inclusive o novo caso; `tst3-sanitize`
  permaneceu `BLOCKED` pela permissao do runtime LLVM. O catalogo registra
  7.196 superficies, 4.002 `COVERED`, 3.194 `PENDING` e 87 casos. O fechamento
  integral, o gate estrito e o baseline TST7 continuam pendentes.

- Incremento Core/app_package concluido em 2026-09-02: `make test-package-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`. A fixture
  host-only passou a usar filesystem FAT12/FAT32 simulado e exercitou pacotes
  ZPKG/ZAPP validos e corrompidos, parsing, CRC, instalacao transacional,
  atualizacao, recuperacao apos failpoint, rollback, remocao, modo legado,
  erros canonicos e limpeza. O relatorio instrumentado
  `build/test-results/package-host/coverage.json` resolveu 111 superficies de
  `src/core/app_package.c`, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao resultou em 7.196 superficies, 4.093 `COVERED`, 3.103
  `PENDING` e 87 casos; o fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.
- 2026-09-02 — Incremento Core/network_manager: `make test-network-manager-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS` com NIC PCI e USB
  simuladas, configuracao estatica, validacao de parametros e rotas, DHCP,
  aplicacao e remocao de lease, clientes remotos, restauracao atomica apos
  erro e limpeza de IPv4/ARP/DNS. O relatorio
  `build/test-results/network-manager-host/coverage.json` terminou `PASS`,
  observou 89 enderecos sem desconhecidos ou ambiguos e resolveu todas as
  superficies de `src/core/network_manager.c`. O catalogo foi sincronizado e
  validado com 7.196 superficies, 4.103 `COVERED`, 3.093 `PENDING` e 87 casos;
  o fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- 2026-09-02 — Incremento Drivers/RTC: `make test-rtc-status-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS` com uma porta CMOS
  simulada no build host, sem I/O privilegiado. A fixture exercitou
  inicializacao invalida e valida, leituras BCD/binaria e 12/24 horas,
  calendario invalido, leituras estaveis, autoteste, timeout de atualizacao e
  estado restaurado apos erro. O relatorio
  `build/test-results/rtc-status-host/coverage.json` observou 26 enderecos,
  sem desconhecidos ou ambiguos, e resolveu todas as 17 superficies de
  `src/drivers/rtc.c`. O catalogo foi sincronizado e validado com 7.196
  superficies, 4.113 `COVERED`, 3.083 `PENDING` e 87 casos; o fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Processos/runtime concluido em 2026-09-04. O caso host-only
  `host:process:runtime` foi adicionado ao `core_host_runner.py`, ao Makefile,
  ao registro de cobertura e ao allowlist de host do TST7. A fixture compila
  `src/process/process.c` real e usa somente processos, paging, VMA, memoria,
  SLAB, syscall, sinais, IPC, VFS e scheduler estaticos. Foram exercitados
  inicializacao, getters, snapshots, limites de criacao, transicoes,
  cancelamento, terminacao, desligamento, wait queues e limpeza.

  Concluida em: 2026-09-04 09:30 (America/Sao_Paulo)

  `make q3check` passou. `make clean` seguido de `make` passou e gerou
  `build/zephyros.img`; `make test-process-host` passou novamente apos o build
  limpo. `make catalog-test` passou com 19 testes unitarios e catalogo valido.
  A suite `python -m unittest tests.unit.test_core_host_runner
  tests.unit.test_tst7_runner` passou com 71 testes. O relatorio instrumentado
  `build/test-results/process-host/coverage.json` terminou `PASS`, com
  `covered=70`, `unknown_addresses=[]` e `ambiguous_symbols=[]`. O catalogo
  registra 7.219 superficies, 5.183 `COVERED`, 2.036 `PENDING` e 136 casos.
  As rotas de troca de contexto, entrada de usuario, idle e bootstrap do
  scheduler continuam pendentes por exigirem evidencia de execucao real; nao
  foram cobertas por associacao generica.

- Concluida em: 2026-09-02

  Incremento Drivers/TSS: foi criado o caso host-only `host:drivers:tss` e o
  alvo `make test-tss-host` com fixture de GDT, `tss_flush()` simulado e
  cobertura dinamica. A fixture exercitou estado antes da inicializacao,
  stacks invalidas e validas, inicializacao repetida e montagem do descritor
  TSS. A execucao terminou `PASS`, sem enderecos desconhecidos ou ambiguos,
  resolvendo as duas superficies pendentes de `src/drivers/tss.c`; o build
  freestanding preserva `lgdt`, a troca de segmentos e o flush original. O
  catalogo registra 7.196 superficies, 4.256 `COVERED`, 2.940 `PENDING` e 98
  casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Boot/recovery runtime concluido em 2026-09-03: foi criado o caso
  host-only `host:boot:recovery-runtime` e o alvo
  `make test-recovery-runtime-host`. A fixture validou diretamente os cinco
  utilitarios freestanding de memoria, strings e log com buffers estaticos,
  sem executar o loader de recuperacao nem acessar hardware. O relatorio
  instrumentado terminou `PASS`, resolveu as cinco superficies de
  `src/boot/recovery_runtime.c` e nao registrou enderecos desconhecidos ou
  ambiguos. Foram executados `make test-recovery-runtime-host`, as 69 fixtures
  host-only do registro e a sincronizacao/renderizacao do catalogo. O catalogo
  registra 7.198 superficies, 4.342 `COVERED`, 2.856 `PENDING` e 110 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- 2026-09-02 — Incremento Core/wifi_manager: o caso host-only
  `host:core:wifi-manager` passou a usar fixtures estaticos de PCI, USB e
  RTL8811CU. A execucao cobriu IDs, inventario, estados READY/UNSUPPORTED/ERROR,
  scan, conexao aberta, limites, metadados invalidos, erros do driver,
  indisponibilidade e recuperacao. `make test-wifi-manager-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`; o relatorio
  `build/test-results/wifi-manager-host/coverage.json` observou 44 enderecos,
  sem desconhecidos ou ambiguos, e resolveu as 25 superficies do modulo. O
  catalogo foi sincronizado e validado com 7.196 superficies, 4.131 `COVERED`,
  3.065 `PENDING` e 88 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Concluida em: 2026-09-02 19:40 (America/Sao_Paulo)

  Incremento Core/usb_manager: `make test-usb-manager-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS` com fixtures
  estaticos de PCI, UHCI, EHCI, MSC e HID. A fixture verificou inventario de
  controladores, portas e dispositivos, IDs, estados e agregacoes, polling,
  refresh, formatacao, limites, falhas de inicializacao e runtime,
  indisponibilidade, recovery e limpeza. O relatorio
  `build/test-results/usb-manager-host/coverage.json` terminou `PASS`,
  resolveu 41 superficies de `src/core/usb_manager.c` e nao registrou
  enderecos desconhecidos ou ambiguos. Foram executados tambem `make
  catalog-test`, `make q3check`, `make clean` e `make`; todos terminaram com
  sucesso. O catalogo registra 7.196 superficies, 4.164 `COVERED`, 3.032
  `PENDING` e 89 casos. A execucao `make test-tst7-quick` confirmou o novo
  caso e os demais host tests; permaneceu `BLOCKED` somente em
  `test-tst3-sanitize` pela indisponibilidade/permissao do runtime LLVM.

- Concluida em: 2026-09-02

  Incremento Drivers/usb_msc: foi criado o caso host-only
  `host:drivers:usb-msc` e o alvo `make test-usb-msc-host` com transporte
  BOT/SCSI e registro de bloco somente leitura simulados. A fixture exercitou
  inquiry, TUR, capacity, READ10, leituras de bloco, filtros, limites, retry,
  reset recovery, CSW corrompido, falhas de controle e registro,
  indisponibilidade e recuperacao. A execucao instrumentada terminou `PASS`,
  resolveu 25 superficies pendentes de `src/drivers/usb_msc.c` e nao registrou
  enderecos desconhecidos ou ambiguos. O catalogo foi sincronizado e validado
  com 7.196 superficies, 4.206 `COVERED`, 2.990 `PENDING` e 91 casos.
  `make q3check`, `make clean`, `make`, `make test-usb-msc-host` e
  `make catalog-test` passaram; os 30 testes unitarios dos runners tambem
  passaram. `make test-tst7-quick` confirmou todas as suites host-only e
  permaneceu `BLOCKED` somente em `test-tst3-sanitize` pela
  indisponibilidade/permissao do runtime LLVM. Nenhuma escrita real em
  armazenamento foi realizada.

- Concluida em: 2026-09-02

  Incremento Drivers/usb_hid: foi criado o caso host-only
  `host:drivers:usb-hid` e o alvo `make test-usb-hid-host` com dispositivos
  HID Boot UHCI simulados. A fixture exercitou teclado, mouse, relatorios
  validos e invalidos, rollover, duplicidade, eventos de entrada, overflow,
  timeout, falhas de controle e interrupt, reconfiguracao, remocao, filtros
  de candidatos, capacidade e recuperacao. A execucao instrumentada terminou
  `PASS`, resolveu as 24 superficies de `src/drivers/usb_hid.c` e nao registrou
  enderecos desconhecidos ou ambiguos. O catalogo foi sincronizado e validado
  com 7.196 superficies, 4.182 `COVERED`, 3.014 `PENDING` e 90 casos. A
  validacao de `q3check`, build limpo e catalogo tambem passou. Os 29 testes
  unitarios dos runners passaram, e `make test-tst7-quick` confirmou todas as
  suites host-only; o resultado geral permaneceu `BLOCKED` somente em
  `test-tst3-sanitize` pela indisponibilidade/permissao do runtime LLVM.

- Concluida em: 2026-09-02

  Incremento Storage/devfs: foi criado o caso host-only
  `host:storage:devfs` e o alvo `make test-devfs-host` com ATA e speaker
  simulados, sem VFS ou hardware real. A fixture exercitou inicializacao
  idempotente, registro, listagem, lookup, permissoes, dispositivos null/zero,
  speaker, hda, leituras, seeks, ioctl, sincronizacao, caminhos indisponiveis
  e invariantes. A execucao instrumentada terminou `PASS`, resolveu as seis
  superficies pendentes de `src/fs/devfs.c` e nao registrou enderecos
  desconhecidos ou ambiguos. O catalogo foi sincronizado e validado com 7.196
  superficies, 4.212 `COVERED`, 2.984 `PENDING` e 92 casos. Nenhum hardware,
  VFS real ou armazenamento real foi acessado; o fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Concluida em: 2026-09-02

  Incremento Storage/procfs: foi criado o caso host-only `host:storage:procfs`
  e o alvo `make test-procfs-host` com o provider procfs real, VFS, processos,
  snapshots e controles de log simulados. A fixture exercitou inicializacao,
  listagem, lookup, leitura global e de processos, mapas, seeks, poll, ioctl,
  sync, permissoes, controles, limites, callbacks de erro e limpeza. A
  execucao instrumentada terminou `PASS`, resolveu as 10 superficies pendentes
  de `src/fs/procfs.c` e nao registrou enderecos desconhecidos ou ambiguos.
  Foram executados `make test-procfs-host`, `make test-tst7-quick` e
  `make catalog-test`; os casos host-only passaram. O quick permaneceu
  `BLOCKED` somente em `test-tst3-sanitize` pela indisponibilidade/permissao
  do runtime LLVM. O catalogo registra 7.196 superficies, 4.222 `COVERED`,
  2.974 `PENDING` e 93 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Concluida em: 2026-09-02

  Incremento Storage/WAV: foi criado o caso host-only `host:storage:wav` e o
  alvo `make test-wav-host` com o parser e reprodutor WAV reais, allocator
  estatico, playback AC97 simulado e cobertura dinamica. A fixture exercitou
  headers RIFF/WAVE, chunks `fmt` e `data`, metadados, duracao, playback,
  ownership, double free, entradas invalidas, truncadas, taxa de amostragem
  zero e limpeza. A correcao em `src/fs/wav.c` libera o buffer adquirido quando
  a taxa de amostragem e zero. A execucao terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos, resolvendo 13 superficies pendentes do modulo.
  O catalogo registra 7.196 superficies, 4.235 `COVERED`, 2.961 `PENDING` e
  94 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Concluida em: 2026-09-02

  Incremento Storage/BMP: foi criado o caso host-only `host:storage:bmp` e o
  alvo `make test-bmp-host` com o parser e renderizador BMP reais, allocator
  estatico, framebuffer e VESA simulados e cobertura dinamica. A fixture
  exercitou formatos 1, 4, 8 e 24 bpp, paletas, orientacao, desenho,
  transparencia, redimensionamento, escala, entradas invalidas, truncadas,
  overflow e falhas de alocacao. A execucao terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos, resolvendo as nove superficies pendentes de
  `src/fs/bmp.c`. O catalogo registra 7.196 superficies, 4.244 `COVERED`,
  2.952 `PENDING` e 95 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Concluida em: 2026-09-02

  Incremento Drivers/RNG: foi criado o caso host-only `host:drivers:rng` e o
  alvo `make test-rng-host` com backend deterministico de CPUID/RDRAND apenas no
  host e cobertura dinamica. A fixture exercitou inicializacao com capacidade
  ausente, inicializacao pronta, leitura de palavras, buffer nulo, leitura
  vazia, falha de hardware e validacao do estado. A execucao terminou `PASS`,
  sem enderecos desconhecidos ou ambiguos, resolvendo as cinco superficies
  pendentes de `src/drivers/rng.c`; o build freestanding preserva o caminho
  Assembly original. O catalogo registra 7.196 superficies, 4.244 `COVERED`,
  2.952 `PENDING` e 96 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Concluida em: 2026-09-02

  Incremento Drivers/Serial: foi criado o caso host-only `host:drivers:serial` e
  o alvo `make test-serial-host` com portas UART simuladas, flags de interrupcao
  inertes no host e cobertura dinamica. A fixture exercitou inicializacao COM1,
  leitura sem dados e com dados, fila de transmissao, filtragem de bytes,
  sequencias ANSI, estado do transmissor e limites de `flush`. A execucao
  terminou `PASS`, sem enderecos desconhecidos ou ambiguos, resolvendo as duas
  superficies pendentes de `src/drivers/serial.c`; o build freestanding preserva
  as operacoes de portas e flags originais. O catalogo registra 7.196
  superficies, 4.251 `COVERED`, 2.945 `PENDING` e 97 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/Speaker concluido em 2026-09-02: foi criado o caso
  host-only `host:drivers:speaker` e o alvo `make test-speaker-host` com portas
  PIT e PC speaker simuladas e cobertura dinamica. A fixture exercitou
  inicializacao, desligamento, frequencia zero, beep, melody, duracoes e espera
  por ticks. A execucao terminou `PASS`, sem enderecos desconhecidos ou
  ambiguos, resolvendo as duas superficies pendentes de `src/drivers/speaker.c`;
  o build freestanding preserva I/O e `hlt`, enquanto o host usa somente o
  backend de portas falso. O catalogo registra 7.196 superficies, 4.258
  `COVERED`, 2.938 `PENDING` e 99 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/USB names concluido em 2026-09-02: os casos host-only
  `host:drivers:usb-hid` e `host:drivers:usb-msc` passaram a iniciar a
  instrumentacao antes dos contratos de nomes de estado e tipo. As execucoes
  `make test-usb-hid-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make test-usb-msc-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminaram `PASS` e resolveram as tres superficies pendentes dos drivers,
  sem enderecos desconhecidos ou ambiguos. O catalogo foi sincronizado e
  validado com 7.196 superficies, 4.261 `COVERED`, 2.935 `PENDING` e 99 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/Keyboard concluido em 2026-09-02: foi criado o caso
  host-only `host:drivers:keyboard` e o alvo `make test-keyboard-host` com
  controlador PS/2, IRQ, fila de eventos e portas simulados e cobertura
  dinamica. A fixture exercitou tabelas de scancode, teclas ABNT2, estado antes
  da inicializacao, filtros, inicializacao, metricas, reset e falha de
  dependencia. A execucao terminou `PASS`, sem enderecos desconhecidos ou
  ambiguos, resolvendo as sete superficies pendentes de `src/drivers/keyboard.c`;
  o caminho freestanding preserva CLI, STI, I/O e espera do controlador, e o
  host usa somente stubs estaticos. O catalogo registra 7.196 superficies,
  4.268 `COVERED`, 2.928 `PENDING` e 100 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/ZTEST adapter concluido em 2026-09-02: foi criado o caso
  host-only `host:tst2:protocol-adapter` e o alvo
  `make test-protocol-adapter-host` com transporte serial, relogio e
  executores de kernel simulados. A fixture cobriu inicializacao sem serial,
  recepcao fragmentada, handshake, `READY`, `HEARTBEAT`, `RUN`, `ABORT`,
  panic, timeout, roteamento TST4/TST5/TST6, falha com fase e bloqueios antes
  de `READY`. A execucao terminou `PASS`, sem enderecos desconhecidos ou
  ambiguos, resolvendo as nove superficies pendentes de
  `src/core/test_protocol.c`. O catalogo foi sincronizado e validado com
  7.196 superficies, 4.277 `COVERED`, 2.919 `PENDING` e 101 casos. O
  fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento TST5/black-box marker concluido em 2026-09-02: foi criado o caso
  host-only `host:tst5:blackbox` e o alvo `make test-blackbox-host` com
  observador de terminal estatico e `process_yield()` controlado. A fixture
  exercitou os nove marcadores de cenarios TST5, snapshots com nova geracao,
  progresso limitado e selecao invalida. A execucao terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos, resolvendo a superficie pendente de
  `src/core/kernel_tests_blackbox.c`. O catalogo foi sincronizado e validado
  com 7.196 superficies, 4.278 `COVERED`, 2.918 `PENDING` e 102 casos. O
  fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Core/test_coverage concluido em 2026-09-03: foi criado o caso
  host-only `host:core:test-coverage` e o alvo `make test-coverage-host` com
  serial falso. A fixture exercitou escrita parcial, retorno sem progresso,
  truncamento de identificador, tabela hash, callbacks de instrumentacao e
  emissao ZCOV. A execucao terminou `PASS`, sem enderecos desconhecidos ou
  ambiguos, resolvendo as 11 superficies de `src/core/test_coverage.c`; a
  cobertura passou a emitir enderecos de 64 bits somente no host, mantendo o
  caminho freestanding de 32 bits. O catalogo foi sincronizado com 7.197
  superficies, 4.290 `COVERED`, 2.907 `PENDING` e 103 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.
- Incremento GUI/display e Shell/core concluido em 2026-09-03: foram criados
  os casos host-only `host:gui:display` e `host:shell:core`, com fixtures
  estaticas para VESA/display, cenas, taskbar, Window Manager, terminal,
  mouse, inicializacao e conclusao de comando. As execucoes instrumentadas
  terminaram `PASS`, sem enderecos desconhecidos ou ambiguos; `display.c`
  ficou com oito superficies cobertas e `shell.c` sem superficies pendentes.
  Foram executados `make q3check`, `make clean`, `make`, os 65 casos host-only
  do registro, `make catalog-test` e 52 testes unitarios dos runners; todos
  passaram. O catalogo registra 7.198 superficies, 4.308 `COVERED`, 2.890
  `PENDING` e 106 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Core/energia terminal concluido em 2026-09-03: o caso
  `host:core:power` passou a exercitar `power_reboot_commit`,
  `power_trigger_triple_fault` e `power_terminal_halt` por um seam exclusivo
  do build host, que captura a acao terminal com `setjmp`/`longjmp` sem
  executar reset, halt ou triple fault no processo de teste. Foram validadas
  as rotas de reboot por triple fault, halt apos commit parcial e ausencia de
  metodo de reboot, incluindo fase, alvo, erro e estado publicado. O comando
  `make test-power-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou
  `PASS`; os 62 casos host-only registrados tambem passaram, e o catalogo foi
  sincronizado com 7.198 superficies, 4.294 `COVERED` e 2.904 `PENDING`.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Shell/hosted concluido em 2026-09-03: o caso host-only existente
  `host:shell:hosted` e o alvo `make test-shell-hosted-host` foram executados
  novamente com Window Manager, terminal e mouse falsos. A fixture exercitou
  modo Classic, abertura, reabertura, callbacks de desenho/tecla/mouse,
  fechamento e rollback quando o registro falha. A execucao terminou `PASS`,
  sem enderecos desconhecidos ou ambiguos, resolvendo as 8 funcoes de
  `src/shell/shell_hosted.c`. O catalogo foi sincronizado com 7.219
  superficies, 4.901 `COVERED`, 2.318 `PENDING` e 128 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/usb_transport concluido em 2026-09-03: foi criado o caso
  host-only `host:core:usb-transport` e o alvo `make test-usb-transport-host`
  com backends EHCI e UHCI falsos. A fixture exercitou argumentos nulos,
  controlador desconhecido, controle, Bulk, reset de toggles, submissao e
  cancelamento de Interrupt, validando `ERR_NULL`, `ERR_UNAVAILABLE` e o
  encaminhamento integral dos argumentos. A execucao instrumentada terminou
  `PASS`, com os sete simbolos de `src/core/usb_transport.c` resolvidos e sem
  enderecos desconhecidos ou ambiguos. Foram executados `make q3check`,
  `make clean`, `make`, os 66 casos host-only do registro, a sincronizacao e
  renderizacao do catalogo, `make catalog-test` e 53 testes unitarios dos
  runners; todos passaram. O catalogo registra 7.198 superficies, 4.320
  `COVERED`, 2.878 `PENDING` e 107 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento GUI/primitivas concluido em 2026-09-03: foi criado o caso
  host-only `host:gui:widgets` e o alvo `make test-gui-host`. A fixture usa
  framebuffer, fonte, metricas de display e VESA falsos para exercitar temas,
  texto nativo e escalado, medicao, paineis, formas, gradientes, botoes,
  molduras e limites de tela. O relatorio instrumentado terminou `PASS`, com
  23 superficies resolvidas e nenhum endereco desconhecido ou ambiguo; as 11
  superficies C que estavam pendentes em `src/gui/gui.c` foram exercitadas por
  chamadas reais. Foram executados `make q3check`, `make clean`, `make`, os 67
  casos host-only do registro, a sincronizacao e renderizacao do catalogo,
  `make catalog-test`, 55 testes unitarios dos runners e `git diff --check`;
  todos passaram. O catalogo registra 7.198 superficies, 4.331 `COVERED`,
  2.867 `PENDING` e 108 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Shell/VFS concluido em 2026-09-03: foi criado o caso host-only
  `host:shell:commands-vfs` e o alvo `make test-shell-commands-vfs-host`. A
  fixture exercitou `grep` com entrada fragmentada, comparacao sem diferenca
  de maiusculas, argumentos invalidos, falha de leitura/escrita e linha acima
  do limite; tambem validou `pipetest` em sucesso, erro e argumento invalido.
  O relatorio instrumentado terminou `PASS`, com 23 superficies resolvidas e
  nenhum endereco desconhecido ou ambiguo; as seis funcoes de
  `src/shell/shell_commands_vfs.c` foram exercitadas por chamadas reais. Foram
  executados `make test-shell-commands-vfs-host`, a sincronizacao e
  renderizacao do catalogo, `make catalog-test` e os testes dos runners. O
  catalogo registra 7.198 superficies, 4.337 `COVERED`, 2.861 `PENDING` e 109
  casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Kernel/panic concluido em 2026-09-03: foi criado o caso host-only
  `host:kernel:panic` e o alvo `make test-panic-host`. A fixture exercitou
  `panic`, `panic_memory`, o cabecalho de diagnostico, metricas com valores
  zero e no limite, mensagens ausentes e explicitas e o encaminhamento dos
  motivos `ERR_STATE`, `ERR_MEM` e personalizados ao protocolo. O halt foi
  capturado por `setjmp`/`longjmp` somente no build host; o build freestanding
  continua com o halt real. O relatorio instrumentado terminou `PASS`, resolveu
  as seis superficies C e as tres APIs publicas correspondentes, sem enderecos
  desconhecidos ou ambiguos. Foram regeneradas as 70 fixtures host-only do
  registro e todas passaram. Tambem passaram `make catalog-test`, 58 testes
  unitarios dos runners, `make q3check`, `make clean`, `make` e a repeticao de
  `make test-panic-host` apos o build limpo. O catalogo registra 7.198
  superficies, 4.351 `COVERED`, 2.847 `PENDING` e 111 casos.

- Incremento Drivers/PCI concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:pci` e o alvo `make test-pci-host`. A fixture usa um espaco de
  configuracao PCI falso para exercitar leitura, escrita, varredura de
  barramento, funcoes multifuncao, inventario, buscas por classe e ID,
  habilitacao de memoria/I/O/DMA, recusas de comando, estado nao inicializado,
  limite de 64 dispositivos e reinicializacao do inventario. O relatorio
  instrumentado terminou `PASS`, resolveu as 14 funcoes de
  `src/drivers/pci.c` sem enderecos desconhecidos ou ambiguos, e nao acessou
  portas I/O reais. Foram regeneradas as 71 fixtures host-only do registro e
  todas passaram; a sincronizacao e renderizacao do catalogo tambem passaram.
  O catalogo registra 7.198 superficies, 4.363 `COVERED`, 2.835 `PENDING` e
  112 casos. Tambem passaram `make q3check`, `make clean`, `make`, a repeticao
  de `make test-pci-host` apos o build limpo, `make catalog-test` e 59 testes
  unitarios dos runners. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento UI/icones concluido em 2026-09-03: foi criado o caso host-only
  `host:ui:icons` e o alvo `make test-icons-host`. A fixture exercitou defaults,
  mutacoes, fallback sem filesystem, carga de BMP, formato invalido, falha de
  memoria, cache, desenho e limites VESA com dependencias estaticas falsas. O
  relatorio instrumentado terminou `PASS`, resolveu as 18 funcoes de
  `src/icons/icons.c` sem enderecos desconhecidos ou ambiguos e nao acessou
  hardware. Foram executados `make q3check`, `make clean`, `make`, o caso apos
  o build limpo, 74 casos host compativeis do registro, a sincronizacao e
  renderizacao do catalogo e `make catalog-test`; todos passaram. O catalogo
  registra 7.198 superficies, 4.380 `COVERED`, 2.818 `PENDING` e 113 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/VESA concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:vesa` e o alvo `make test-vesa-host`. A fixture usou seams
  exclusivos do build host para boot info e framebuffer e exercitou
  inicializacao valida e invalida, modos 24/32 bpp, backbuffer, desenho,
  clipping, frames, metricas, flip, falha de alocacao e desativacao. O
  relatorio instrumentado terminou `PASS`, resolveu as 12 funcoes pendentes de
  `src/drivers/vesa.c` sem enderecos desconhecidos ou ambiguos e nao acessou
  hardware real. Foram executados `make q3check`, `make clean`, `make`, o caso
  apos o build limpo, 75 casos host compativeis, a sincronizacao e renderizacao
  do catalogo e `make catalog-test`; todos passaram. O catalogo registra 7.198
  superficies, 4.392 `COVERED`, 2.806 `PENDING` e 114 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/video concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:video` e o alvo `make test-video-host`. A fixture usa
  framebuffer, fonte, VESA, mouse e logs falsos para exercitar inicializacao,
  desenho, cursor, flush, terminal, scrollback, snapshots validos e
  corrompidos, rolagem, suspensao, quiescencia e estados indisponiveis. O
  relatorio instrumentado `build/test-results/video-host/coverage.json`
  terminou `PASS`, observou 96 enderecos sem desconhecidos ou ambiguos e
  resolveu as 30 superficies pendentes de `src/drivers/video.c`. Foram
  executados `make q3check`, `make clean`, `make`, o caso apos o build limpo,
  a sincronizacao e renderizacao do catalogo e `make catalog-test`; todos
  passaram. O catalogo registra 7.198 superficies, 4.393 `COVERED`, 2.805
  `PENDING` e 115 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/ACPI concluido em: 2026-09-03 14:16 (America/Sao_Paulo).
  Foi
  criado o caso host-only `host:drivers:acpi` e o alvo `make test-acpi-host`.
  A fixture usa firmware, mapa E820, portas I/O e halt falsos para exercitar
  RSDP, RSDT/XSDT, FADT, MADT, FACS, AML `_S5_`, consultas, checksums, tabelas
  corrompidas e rotas de energia sem acesso a hardware real. O relatorio
  instrumentado `build/test-results/acpi-host/coverage.json` terminou `PASS`,
  observou 88 enderecos sem desconhecidos ou ambiguos e resolveu as 56
  superficies pendentes originais de `src/drivers/acpi.c`, alem dos dois seams
  exclusivos do build host. A sincronizacao foi ajustada para preservar
  vinculos anteriores quando um relatorio dinamico ainda nao esta disponivel
  depois de `make clean`, sem criar cobertura artificial. Foram executados
  `make q3check`, `make clean`, `make`, `make test-acpi-host` apos o build
  limpo, `make catalog-test`, `python -m unittest tests.unit.test_catalog
  tests.unit.test_core_host_runner tests.unit.test_tst7_runner`,
  `git diff --check` e a verificacao de processos residuais; todos passaram.
  O catalogo registra 7.200 superficies, 4.463 `COVERED`, 2.737 `PENDING` e
  116 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Drivers/EHCI concluido em: 2026-09-03 14:52 (America/Sao_Paulo).
  Foi criado o caso host-only `host:drivers:ehci` e o alvo
  `make test-ehci-host`. A fixture simulou PCI, MMIO, DMA, temporizador e
  dispositivos USB para exercitar inicializacao, reset, enumeracao high-speed,
  descritores, transfers de controle e bulk, interrupt, timeout, erro de qTD,
  recuperacao, falhas de hardware e limpeza. O relatorio instrumentado
  `build/test-results/ehci-host/coverage.json` terminou `PASS`, observou 83
  enderecos sem desconhecidos ou ambiguos e resolveu as 52 superficies de
  `src/drivers/ehci.c`, sem I/O privilegiado ou hardware real. Foram executados
  `make q3check`, `make clean`, `make`,
  `make test-ehci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos passaram. O catalogo registra 7.203 superficies,
  4.580 `COVERED`, 2.623 `PENDING` e 118 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/UHCI concluido em: 2026-09-03 (America/Sao_Paulo).
  Foi criado o caso host-only `host:drivers:uhci` e o alvo
  `make test-uhci-host`. A fixture simulou PCI, DMA, portas, IRQ, temporizador
  e dispositivos USB para exercitar inicializacao, reset, enumeracao,
  descritores, transfers de controle e bulk, interrupt, timeout, recuperacao,
  entradas invalidas e limpeza. O relatorio instrumentado
  `build/test-results/uhci-host/coverage.json` terminou `PASS`, observou 105
  enderecos sem desconhecidos ou ambiguos e resolveu as 71 superficies de
  `src/drivers/uhci.c`, sem I/O privilegiado ou hardware real. Foram executados
  `make q3check`, `make clean`, `make`,
  `make test-uhci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos passaram. O catalogo registra 7.202 superficies,
  4.529 `COVERED`, 2.673 `PENDING` e 117 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/RTL8139 concluido em: 2026-09-03 15:11:47 -03:00
  (America/Sao_Paulo). Foi criado o caso host-only `host:drivers:rtl8139` e o
  alvo `make test-rtl8139-host`. A fixture simulou PCI, portas I/O, DMA,
  temporizador, IRQ e bottom-half para exercitar inicializacao, reset, leitura
  de MAC, transmissao, recepcao, erros de ring, timeout, quiescencia,
  recuperacao e limpeza. O relatorio instrumentado
  `build/test-results/rtl8139-host/coverage.json` terminou `PASS`, observou 66
  enderecos sem desconhecidos ou ambiguos e resolveu as 36 funcoes de
  `src/drivers/rtl8139.c` e as duas APIs publicas correspondentes. Foram
  executados `make q3check`, `make clean`, `make`,
  `make test-rtl8139-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`,
  `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate` e `git diff --check`; todos passaram.
  O build completo passou com os warnings legados ja existentes em outros
  modulos. O catalogo registra 7.204 superficies, 4.619 `COVERED`, 2.585
  `PENDING` e 119 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/mouse concluido em 2026-09-03. Foi criado o caso
  host-only `host:drivers:mouse` e o alvo `make test-mouse-host`. A fixture
  simulou a controladora PS/2, IRQ12, fila de entrada, framebuffer VESA e
  respostas Intellimouse para exercitar inicializacao, fallback de tres bytes,
  eventos de movimento, botoes e roda, coalescencia, cursor, configuracao,
  timeouts, ACK invalido, indisponibilidade, recuperacao e limpeza. O relatorio
  instrumentado `build/test-results/mouse-host/coverage.json` terminou `PASS`,
  resolveu as 46 superficies de `src/drivers/mouse.c` e as 13 APIs publicas
  correspondentes, sem enderecos desconhecidos ou ambiguos. Foram executados
  `make q3check`, `make clean`, `make`,
  `make test-mouse-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate` e `git diff --check`; todos passaram.
  O build completo manteve apenas warnings legados em modulos nao relacionados
  ao mouse. O catalogo registra 7.204 superficies, 4.665 `COVERED`, 2.539
  `PENDING` e 120 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/E1000 concluido em 2026-09-03. Foi criado o caso
  host-only `host:drivers:e1000` e o alvo `make test-e1000-host`. A fixture
  simulou PCI, MMIO, reset, MAC, DMA, IRQ deferred, descritores, transmissao,
  recepcao, fila RX, quiescencia e falhas de inicializacao. O relatorio
  instrumentado `build/test-results/e1000-host/coverage.json` terminou `PASS`,
  resolveu as 34 funcoes de `src/drivers/e1000.c` e as duas APIs publicas
  correspondentes, sem enderecos desconhecidos ou ambiguos. Foram executados
  `make test-e1000-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate` e `git diff --check`; os gates de
  `q3check`, build limpo e build completo ja haviam passado nesta sequencia.
  O catalogo registra 7.206 superficies, 4.689 `COVERED`, 2.517 `PENDING` e
  121 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.


- Incremento Drivers/RTL8811CU concluido em 2026-09-03. Foi criado o caso
  `host:drivers:rtl8811cu` e o alvo `make test-rtl8811cu-host`. A fixture
  simulou dispositivos USB EHCI high-speed, descritores, endpoints Bulk,
  filesystem e firmware falso, exercitando probe, estados de inicializacao,
  callbacks Ethernet, scan, associacao aberta, limites de SSID e caminhos
  de indisponibilidade segura. O relatorio instrumentado
  `build/test-results/rtl8811cu-host/coverage.json` terminou `PASS`, resolveu
  17 funcoes de `src/drivers/rtl8811cu.c` e as 7 APIs publicas correspondentes,
  sem enderecos desconhecidos ou ambiguos. Foram executados
  `make test-rtl8811cu-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate`, `make q3check`, `make clean`,
  `make` e `git diff --check`; todos passaram nesta etapa. O catalogo registra
  7.209 superficies, 4.741 `COVERED`, 2.468 `PENDING` e 123 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/AC97 concluido em 2026-09-03. Foi criado o caso
  host-only `host:drivers:ac97` e o alvo `make test-ac97-host`. A fixture
  simulou PCI, portas I/O, codec, reset, energia, playback, memoria, IRQ,
  limites de amostras, parada e falhas de inicializacao. O relatorio
  instrumentado `build/test-results/ac97-host/coverage.json` terminou `PASS`,
  resolveu as 22 funcoes de `src/drivers/ac97.c` e as APIs publicas
  correspondentes, sem enderecos desconhecidos ou ambiguos. Foi corrigido o
  calculo do buffer de playback para alocar espaco por amostra e evitar escrita
  alem do buffer. Foram executados `make test-ac97-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, `python tools/test_catalog.py
  sync`, `python tools/test_catalog.py render`, `python tools/test_catalog.py
  validate`, `make q3check`, `make clean`, `make` e `git diff --check`; todos
  passaram nesta etapa. O catalogo registra 7.209 superficies,
  4.717 `COVERED`, 2.492 `PENDING` e 122 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/ATA concluido em 2026-09-03. Foi criado o caso host-only
  `host:drivers:ata` e o alvo `make test-ata-host`. A fixture substitui as
  instrucoes privilegiadas por portas ATA falsas e exercita descoberta nos
  quatro slots, parsing de IDENTIFY, limite LBA28, inventario, IRQ, leitura e
  escrita PIO, contadores, flush, retries, limites, falhas de estado e
  timeouts, sem acessar disco ou I/O real. O relatorio instrumentado
  `build/test-results/ata-host/coverage.json` terminou `PASS`, resolveu as 30
  funcoes observadas de `src/drivers/ata.c`, sem enderecos desconhecidos ou
  ambiguos, e a fixture retornou os erros canonicos esperados. Foram executados
  `make test-ata-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate` e `git diff --check`; todos passaram
  nesta etapa. O catalogo registra 7.209 superficies, 4.757 `COVERED`, 2.452
  `PENDING` e 124 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/RTC concluido em 2026-09-03. O caso existente
  `host:drivers:rtc-status` foi executado novamente apos o build limpo. A
  fixture usa CMOS falso e exercita inicializacao, leitura de registradores,
  snapshots estaveis, conversao BCD/binaria e 12/24 horas, calendario,
  autoteste, timeout de atualizacao e estado publicado apos erro. O relatorio
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, observou
  26 enderecos, resolveu as 17 funcoes de `src/drivers/rtc.c` e a dependencia
  `kmemset`, sem enderecos desconhecidos ou ambiguos. Foram executados
  `make test-rtc-status-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `make catalog-test` e a contagem de pendencias; todos passaram nesta etapa.
  O catalogo registra 7.219 superficies, 4.910 `COVERED`, 2.309 `PENDING` e
  128 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Drivers/IDT concluido em 2026-09-03. Foi criado o caso host-only
  `host:drivers:idt` e o alvo `make test-idt-host`. A fixture usa stubs de
  ISR/IRQ, PIC, flags, `lidt` e panic para exercitar inicializacao, gates,
  handlers simples e compartilhados, limites, unmask, estatisticas, EOI,
  syscall e despacho sem executar instrucoes privilegiadas. O relatorio
  `build/test-results/idt-host/coverage.json` terminou `PASS`, resolveu as 20
  funcoes observadas de `src/drivers/idt.c`, sem enderecos desconhecidos ou
  ambiguos, e os erros canonicos das fixtures negativas foram confirmados.
  Foram executados `make test-idt-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, `python tools/test_catalog.py
  sync`, `python tools/test_catalog.py render`, `make catalog-test` e a
  contagem de pendencias; todos passaram nesta etapa. O catalogo registra
  7.211 superficies, 4.781 `COVERED`, 2.430 `PENDING` e 125 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/EHCI concluido em 2026-09-03. O caso host-only existente
  `host:drivers:ehci` foi executado novamente com PCI, MMIO, DMA e USB falsos;
  o relatorio `build/test-results/ehci-host/coverage.json` terminou `PASS`,
  sem enderecos desconhecidos ou ambiguos. O registro de cobertura passou a
  usar `include_public_apis: true`, e a sincronizacao vinculou as 13 APIs
  publicas pendentes de `src/include/drivers/ehci.h` as implementacoes reais
  exercitadas pela fixture. Foram executados `make test-ehci-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, `python tools/test_catalog.py
  sync`, `python tools/test_catalog.py render` e `make catalog-test`; todos
  passaram nesta etapa. O catalogo registra 7.211 superficies, 4.794
  `COVERED`, 2.417 `PENDING` e 125 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Network/socket runtime concluido em 2026-09-03. Foi criado o caso
  host-only `host:network:socket-runtime` com o alvo
  `make test-socket-runtime-host`. A fixture liga o `src/core/socket.c` real a
  backends falsos de TCP, VFS, filas UNIX, espera, SKB e processo; exercita
  inicializacao, conexao, I/O, EOF, erros, polling, cancelamento, capacidade,
  autoteste e limpeza sem rede ou hardware.

  `make test-socket-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  passou. O relatorio instrumentado
  `build/test-results/socket-runtime-host/coverage.json` resolveu as 64
  funcoes de `src/core/socket.c`, com `unknown_addresses=[]` e
  `ambiguous_symbols=[]`. A regressao `make test-net-socket-host`, os testes
  unitarios dos runners, a sincronizacao/renderizacao e `make catalog-test`
  tambem passaram. O catalogo registra 7.219 superficies, 4.867 `COVERED`,
  2.352 `PENDING` e 127 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Network/socket runtime — fechamento de cobertura — concluido em
  2026-09-03. O caso host-only `host:network:socket-runtime` foi ampliado
  para fechar o caminho de remocao de um cliente UNIX conectado, mas ainda
  nao aceito, antes do `accept`; a fila pendente e o estado global sao
  validados apos o fechamento.

  `make test-socket-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  passou. O relatorio instrumentado
  `build/test-results/socket-runtime-host/coverage.json` resolveu as 65
  funcoes de `src/core/socket.c`, com `unknown_addresses=[]` e
  `ambiguous_symbols=[]`; a sincronizacao e a renderizacao do catalogo
  tambem passaram. O catalogo registra 7.219 superficies, 4.896 `COVERED`,
  2.323 `PENDING` e 128 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Storage/sysfs concluido em 2026-09-03. Foi criado o caso
  host-only `host:storage:sysfs` com o alvo `make test-sysfs-host`. A fixture
  liga o provider `src/fs/sysfs.c` real a inventarios falsos de PCI, rede,
  bloco e energia; exercita lookup, listagens, todos os atributos, snapshots
  somente leitura, permissoes, seek, poll, overflow, fallback de energia,
  autoteste e limpeza. O relatorio instrumentado
  `build/test-results/sysfs-host/coverage.json` terminou `PASS`, resolveu as
  58 funcoes e 8 APIs publicas de `src/fs/sysfs.c`, sem enderecos desconhecidos
  ou ambiguos. A sincronizacao e a renderizacao do catalogo passaram nesta
  etapa. Tambem passaram os testes unitarios dos runners, `make catalog-test`,
  `make q3check`, `make clean`, `make` e uma nova execucao do sysfs apos o
  build limpo. O catalogo registra 7.219 superficies, 4.895 `COVERED`,
  2.324 `PENDING` e 128 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Shell/entrada — fechamento de cobertura — concluido em
  2026-09-03. O caso host-only existente `host:shell:input` foi executado
  novamente para registrar os caminhos de inicializacao e consulta do buffer
  que ainda estavam pendentes. O relatorio
  `build/test-results/shell-input-host/coverage.json` terminou `PASS`,
  resolveu as 16 funcoes de `src/shell/shell_input.c` e apresentou
  `unknown_addresses=[]` e `ambiguous_symbols=[]`. A sincronizacao e a
  renderizacao do catalogo passaram; o catalogo registra 7.219 superficies,
  4.898 `COVERED`, 2.321 `PENDING` e 128 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Shell/entrada — evidencia final — concluido em 2026-09-03. O
  caso existente `host:shell:input` foi executado apos o build limpo. O
  relatorio `build/test-results/shell-input-host/coverage.json` terminou
  `PASS`, resolveu as 16 funcoes de `src/shell/shell_input.c`, incluindo
  `shell_input_init` e `shell_input_get_buffer`, sem erros de cobertura.
  Tambem passaram a sincronizacao/renderizacao do catalogo e `make
  catalog-test`. O catalogo atual registra 7.219 superficies, 5.083
  `COVERED`, 2.136 `PENDING` e 132 casos.

- Incremento Seguranca/tls_client concluido em 2026-09-03 19:37
  (America/Sao_Paulo). Foi criado o caso host-only `host:security:tls-client`
  com o alvo `make test-tls-client-host`. A fixture compila o
  `src/core/tls_client.c` real contra um engine BearSSL falso e usa socket,
  relogio e RNG deterministas para exercitar inicializacao, configuracao,
  divisao de tempo, handshake, envio, recepcao, EOF, falhas de I/O,
  indisponibilidade de entropia, limites de SNI, estados e limpeza, sem rede
  externa. `make test-tls-client-host` passou com `HOST_CC` configurado e
  warnings tratados como erro. O relatorio
  `build/test-results/tls-client-host/coverage.json` terminou `PASS`, resolveu
  as 12 superficies antes pendentes de `src/core/tls_client.c` e apresentou
  `unknown_addresses=[]` e `ambiguous_symbols=[]`. Tambem passaram
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `make catalog-test` e os 38 testes unitarios de `test_core_host_runner.py`.
  O catalogo registra 7.219 superficies, 4.922 `COVERED`, 2.297 `PENDING` e
  129 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Shell/Media Player concluido em 2026-09-03. Foi criado o caso
  host-only `host:shell:mediaplayer` com o alvo
  `make test-mediaplayer-host`. A fixture compilou o
  `src/shell/mediaplayer.c` real com arquivos, audio, imagem, AC97, VESA,
  timer e recovery falsos; exercitou playback individual e combinado, pausa,
  retomada, parada, atualizacao, limite de nome, arquivos ausentes, formatos
  invalidos, dependencias indisponiveis e limpeza.

  `make test-mediaplayer-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  passou com warnings tratados como erro. O relatorio instrumentado
  `build/test-results/mediaplayer-host/coverage.json` terminou `PASS`,
  resolveu as 12 superficies de `src/shell/mediaplayer.c` e apresentou
  `unknown_addresses=[]` e `ambiguous_symbols=[]`. Tambem passaram
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `make catalog-test`, `make q3check`, `make clean`, `make` e uma nova
  execucao do caso apos o build limpo. O catalogo registra 7.219 superficies,
  4.929 `COVERED`, 2.290 `PENDING` e 130 casos. `mp_main` permanece pendente
  porque nao possui implementacao no codigo ativo; o fechamento integral, o
  gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Shell/job concluido em 2026-09-03. O caso host-only
  `host:shell:job` foi adicionado com `make test-shell-job-host`, compilando
  `src/shell/shell_job.c` real com relogio, teclado, IPC, video e runtime
  falsos. A fixture exercitou sucesso, falha, cancelamento, drenagem,
  timeout, deadlines, wakeups, geracoes obsoletas, eventos bloqueados e o
  comando `job status`. A execucao terminou `PASS` com warnings tratados como
  erro; o relatorio `build/test-results/shell-job-host/coverage.json` resolveu
  as 30 superficies C do modulo sem enderecos desconhecidos ou ambiguos.
  Tambem passaram `python tools/test_catalog.py sync` e
  `python tools/test_catalog.py render`. O catalogo registra 7.219
  superficies, 4.955 `COVERED`, 2.264 `PENDING` e 131 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Shell/pipeline concluido em 2026-09-03. Foi criado o caso
  host-only `host:shell:pipeline` com `make test-shell-pipeline-host`,
  compilando `src/shell/shell_pipeline.c` real com VFS, threads, video e logs
  falsos. A fixture passou por parsing, limites, pipes, leitura, escrita,
  redirecionamento, autoteste de pipe, workers, falhas de criacao, erros de
  I/O, overflow e limpeza.

  `make test-shell-pipeline-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS` com warnings tratados como erro. O relatorio
  `build/test-results/shell-pipeline-host/coverage.json` resolveu as 26
  superficies C do modulo, com `unknown_addresses=[]` e
  `ambiguous_symbols=[]`. Tambem passaram `python tools/test_catalog.py sync`
  e `python tools/test_catalog.py render`. O catalogo registra 7.219
  superficies, 4.977 `COVERED`, 2.242 `PENDING` e 132 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Shell/dispatcher — fechamento da tabela — concluido em 2026-09-03.
  A fixture host-only `host:shell:dispatch` passou a enviar os 95 comandos da
  tabela com argumentos sentinela e confirmou despacho unico, preservacao dos
  argumentos, retorno `OK`, normalizacao, limite e `ERR_NULL`. Os handlers
  ficaram como stubs apenas no processo de teste, mantendo o escopo restrito
  ao dispatcher.

  `make test-shell-dispatch-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`. O relatorio instrumentado manteve
  `unknown_addresses=[]` e `ambiguous_symbols=[]`; o catalogo sincronizado e
  renderizado marcou as 97 superficies de `src/shell/shell_dispatch.c` como
  `COVERED`, sendo os 95 comandos registrados por integracao e as duas funcoes
  C resolvidas diretamente. O catalogo atual registra 7.219 superficies,
  5.072 `COVERED`, 2.147 `PENDING` e 132 casos.

- Incremento Drivers/RTC — fechamento de cobertura — concluido em 2026-09-03.
  O caso existente `host:drivers:rtc-status` foi executado novamente depois do
  build limpo, com CMOS falso e sem I/O privilegiado. O relatorio
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, resolveu
  as 17 funcoes de `src/drivers/rtc.c`, incluindo I/O CMOS, leituras estaveis,
  conversao e inicializacao, e nao registrou erros de cobertura.

  Tambem passaram `python tools/test_catalog.py sync`,
  `python tools/test_catalog.py render` e `make catalog-test`. O catalogo atual
  registra 7.219 superficies, 5.081 `COVERED`, 2.138 `PENDING` e 132 casos.

- Incremento Shell/hosted — evidencia final — concluido em 2026-09-03. O caso
  existente `host:shell:hosted` foi executado novamente apos o build limpo com
  Window Manager, terminal e mouse falsos. O relatorio
  `build/test-results/shell-hosted-host/coverage.json` terminou `PASS`,
  resolveu as 8 funcoes de `src/shell/shell_hosted.c` e nao registrou erros de
  cobertura. Tambem passaram `python tools/test_catalog.py sync`,
  `python tools/test_catalog.py render` e `make catalog-test`. O catalogo atual
  registra 7.219 superficies, 5.086 `COVERED`, 2.133 `PENDING` e 132 casos.

- Incremento Core/ZTEST adapter - correção da fixture e APIs públicas - concluído
  em 2026-09-03. A primeira execução revelou falha de link por ausência do
  símbolo `kernel_tests_run_assembly()` na fixture host-only. O teste foi
  corrigido com um executor falso controlado e passou a enviar a rota
  `qemu:tst7:assembly`, além das rotas TST4/TST5/TST6, sem incluir o harness
  freestanding no binário host. `make test-protocol-adapter-host`, executado
  antes e depois de `make clean` seguido de `make`, terminou `PASS`.
  `make q3check` e `make catalog-test` também passaram. O relatório
  `build/test-results/protocol-adapter-host/coverage.json` terminou `PASS`,
  com `unknown_addresses=[]`, `ambiguous_symbols=[]` e as seis APIs públicas
  de `src/include/core/test_protocol.h` vinculadas por `include_public_apis`.
  O único pendente do entorno é a função estática
  `c:src/kernel/kernel.c:test_protocol_process_main`, que requer evidência do
  processo real no QEMU; ela não foi coberta artificialmente pelo host-only.
  O catálogo atual registra 7.219 superfícies, 5.092 `COVERED`, 2.127
  `PENDING` e 132 casos.

- Incremento UI/taskbar concluído em 2026-09-03. O novo caso
  `host:ui:taskbar` e o alvo `make test-taskbar-host` usam dependências falsas
  de VESA, display, desktop, mouse, timer e desenho para exercitar TUI e GUI,
  layouts, limites de botões, menus, configuração, cliques, relógio e seleção
  de janelas. A execução instrumentada terminou `PASS` e resolveu todas as
  47 superfícies de `src/taskbar/taskbar.c`, sem `unknown_addresses` ou
  `ambiguous_symbols`. Também passaram `make catalog-test`, a unidade dos
  runners e `make q3check`. O catálogo atual registra 7.219 superfícies,
  5.120 `COVERED`, 2.099 `PENDING` e 134 casos. O fechamento integral, o gate
  estrito e a validação TST7 completa continuam pendentes.

- Incremento Core/app_loader concluído em 2026-09-03. O novo caso
  `host:core:app-loader` e o alvo `make test-app-loader-host` usam buffers
  estáticos e dependências falsas para exercitar parsing, validação de ZAPP,
  ciclo de vida de processos, cancelamento, resultados, caminhos inválidos e
  propagação de erros do loader real. A execução instrumentada terminou
  `PASS`; `coverage.json` resolveu todas as 24 superfícies de
  `src/core/app_loader.c`, sem `unknown_addresses` ou `ambiguous_symbols`.
  Também passaram `make q3check`, `make clean`, `make`, `make catalog-test` e
  a suíte unitária dos runners. O catálogo atual registra 7.219 superfícies,
  5.110 `COVERED`, 2.109 `PENDING` e 133 casos. O fechamento integral, o gate
  estrito e a validação TST7 completa continuam pendentes.

- Incremento Processos/threads concluído em 2026-09-04 09:52
  (America/Sao_Paulo). Foi adicionado o caso host-only
  `host:process:threads` e o alvo `make test-thread-host`. A fixture compila
  `src/thread/thread.c` real com pool de threads e stacks estáticas, exercitando
  inicialização, criação, seleção, yield, bloqueio, espera, cancelamento,
  desbloqueio, timeout, indisponibilidade, limites e limpeza. Os cenários de
  espera criam as entradas pela API real, preservando os callbacks internos de
  transição no teste host.

  A execução instrumentada terminou `PASS`, resolveu 29 superfícies de
  `src/thread/thread.c` e registrou `unknown_addresses=[]` e
  `ambiguous_symbols=[]` em `build/test-results/thread-host/coverage.json`.
  Passaram `make q3check`, `make clean`, `make`, `make test-thread-host`,
  `make catalog-test` e os 72 testes unitários de
  `tests.unit.test_core_host_runner` e `tests.unit.test_tst7_runner`.
  O catálogo registra 7.219 superfícies, 5.203 `COVERED`, 2.016 `PENDING` e
  137 casos. `thread_context_switch` e a entrada Assembly correspondente
  permanecem pendentes por exigirem execução freestanding/QEMU.

- Reconciliação de cobertura Shell/entrada concluída em 2026-09-04. O caso
  existente `host:shell:input` foi executado novamente e o relatório
  `build/test-results/shell-input-host/coverage.json` confirmou a execução de
  `shell_input_init` e `shell_input_get_buffer`, que ainda estavam pendentes
  no catálogo apesar de já serem exercitadas pela fixture. A sincronização e
  a renderização marcaram essas duas superfícies C como `COVERED`; não houve
  alteração no código do Shell. O catálogo registra 7.219 superfícies, 5.205
  `COVERED`, 2.014 `PENDING` e 137 casos.

- Incremento Drivers/RTC — fechamento final concluído em 2026-09-04 10:01
  (America/Sao_Paulo). O caso existente `host:drivers:rtc-status` foi executado
  novamente após o build limpo. A fixture usa CMOS falso e resolveu as nove
  superfícies que ainda estavam `PENDING`: I/O CMOS, validação de estado,
  leituras estáveis, conversão e inicialização. O relatório
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, com
  `unknown_addresses=[]` e `ambiguous_symbols=[]`.

  Também passaram a sincronização/renderização do catálogo e
  `make catalog-test`. O catálogo registra 7.219 superfícies, 5.214
  `COVERED`, 2.005 `PENDING` e 137 casos.

- Incremento Shell/hosted — fechamento final concluído em 2026-09-04 10:04
  (America/Sao_Paulo). O caso existente `host:shell:hosted` foi executado
  novamente e o relatório `build/test-results/shell-hosted-host/coverage.json`
  confirmou as três superfícies que ainda estavam `PENDING`, sem endereços
  desconhecidos ou símbolos ambíguos. A sincronização e a renderização do
  catálogo passaram sem alteração no código do Shell.

  O catálogo registra 7.219 superfícies, 5.217 `COVERED`, 2.002 `PENDING` e
  137 casos.

- Reconciliação de cobertura Core/panic concluída em 2026-09-04 10:08
  (America/Sao_Paulo). O caso existente `host:kernel:panic` foi executado para
  gerar `build/test-results/panic-host/coverage.json`, que resolveu as rotas
  reais de `src/kernel/panic.c`. A sincronização reconheceu também as três
  APIs públicas correspondentes, `panic`, `panic_halt` e `panic_memory`, antes
  pendentes porque o relatório da execução anterior não estava presente. Não
  houve alteração no contrato do panic nem execução de halt real.

  A sincronização, a renderização e `make catalog-test` passaram. O catálogo
  registra 7.219 superfícies, 5.220 `COVERED`, 1.999 `PENDING` e 137 casos.

- Correção do allowlist TST7 para drivers concluída em 2026-09-04. A execução
  rápida do TST7 identificou que os casos host-only de AC97, ACPI, ATA, E1000,
  EHCI, IDT, mouse, RTL8139, RTL8811CU e UHCI tinham executores reais e alvos
  Makefile, mas não estavam todos associados no mapa interno do runner. As
  associações foram registradas sem alterar o catálogo ou criar cobertura
  artificial. `python -m unittest tests.unit.test_tst7_runner`,
  `make test-ac97-host`, `make q3check`, `make clean`, `make` e
  `make catalog-test` passaram. A execução rápida completa anterior continua
  registrada como `FAIL` por `test-tst3-sanitize` `BLOCKED`; esse bloqueio de
  ambiente não foi mascarado.

- Reconciliação do modo de cobertura do dispatcher concluída em 2026-09-04.
  Após regenerar os relatórios reais da execução rápida, a sincronização do
  catálogo aplicou o registro `shell-command-table-host` aos 95 comandos
  associados ao dispatcher. Esses comandos são cobertos por integração pelo
  fluxo de despacho, não por chamadas diretas individuais; o catálogo foi
  ajustado de `direct` para `integration` sem alterar `case_ids` ou adicionar
  vínculos artificiais. A renderização, `make catalog-test` e a validação
  bidirecional do TST7 permaneceram válidas.

- Reconciliação de cobertura Shell/PCI concluída em 2026-09-04. Os relatórios
  `build/test-results/shell-core-host/coverage.json` e
  `build/test-results/pci-host/coverage.json` terminaram `PASS`, sem endereços
  desconhecidos ou ambíguos. A sincronização associou 16 superfícies reais de
  `src/shell/shell.c` a `host:shell:core` e 10 APIs reais de PCI a
  `host:drivers:pci`; como as fixtures chamam diretamente essas funções, o
  catálogo registrou `coverage_mode=direct`. Nenhuma associação por arquivo foi
  criada e os vínculos bidirecionais continuam válidos.

- Cobertura Shell/commands-core concluída em 2026-09-04 10:54 (America/Sao_Paulo).
  A nova fixture host-only `host:shell:commands-core` executou diretamente os
  handlers reais de `src/shell/shell_commands_core.c`, incluindo caminhos
  válidos, inválidos, VFS, loader, processos, energia, áudio e compressão.
  O relatório `build/test-results/shell-commands-core-host/coverage.json`
  terminou `PASS`, sem endereços desconhecidos ou símbolos ambíguos. Foram
  validados `make test-shell-commands-core-host`, `make q3check`, `make clean`,
  `make`, `make test-shell-commands-core-host`, os 75 testes unitários dos
  runners, `git diff --check` e `make catalog-test`. O catálogo registra 7.219
  superfícies, 5.240 `COVERED`, 1.979 `PENDING` e 138 casos. As pendências
  restantes continuam reais e não foram mascaradas.
- Fechamento do ramo de falha do loader Shell concluído em 2026-09-04 11:00
  (America/Sao_Paulo). A fixture `host:shell:commands-core` passou a provocar
  um resultado `start_failed` para um aplicativo migrado e confirmou a
  mensagem, a finalização do comando e a limpeza do estado do loader. O
  relatório dinâmico terminou `PASS` e resolveu a última superfície pendente
  de `src/shell/shell_commands_core.c`, `shell_report_builtin_failure`.
  Passaram novamente `make q3check`, `make clean`, `make`,
  `make test-shell-commands-core-host`, `make catalog-test` e os testes
  unitários dos runners. O catálogo agora registra 7.219 superfícies, 5.241
  `COVERED`, 1.978 `PENDING` e 138 casos.
- Evidência Shell/input regenerada em 2026-09-04 11:01 (America/Sao_Paulo).
  Após o build limpo, `make test-shell-input-host` terminou `PASS` e produziu
  novamente `build/test-results/shell-input-host/coverage.json`, sem endereços
  desconhecidos ou símbolos ambíguos. A sincronização, renderização e
  `make catalog-test` passaram; `shell_input_init` e `shell_input_get_buffer`
  voltaram a ser `COVERED` por execução real. O catálogo registra 7.219
  superfícies, 5.243 `COVERED`, 1.976 `PENDING` e 138 casos.
- Evidência Shell/hosted regenerada em 2026-09-04 11:03 (America/Sao_Paulo).
  `make test-shell-hosted-host` terminou `PASS` após o build limpo e produziu
  o relatório instrumentado sem endereços desconhecidos ou símbolos ambíguos.
  A sincronização, renderização e `make catalog-test` passaram; as três
  superfícies `shell_hosted_close`, `shell_hosted_mouse` e `shell_hosted_reset`
  foram confirmadas por execução real. O catálogo registra 7.219 superfícies,
  5.246 `COVERED`, 1.973 `PENDING` e 138 casos.
- Evidência Drivers/RTC regenerada em 2026-09-04 11:11 (America/Sao_Paulo).
  `make test-rtc-status-host` terminou `PASS` com CMOS falso e relatório
  instrumentado sem endereços desconhecidos ou símbolos ambíguos. A execução
  confirmou novamente as nove superfícies internas de `src/drivers/rtc.c`,
  incluindo I/O, leitura estável, validação, conversão e inicialização. A
  sincronização, renderização e `make catalog-test` passaram. O catálogo
  registra 7.219 superfícies, 5.255 `COVERED`, 1.964 `PENDING` e 138 casos.
- Allowlist do TST7 quick corrigido em 2026-09-04 11:21 (America/Sao_Paulo).
  O caso `host:shell:commands-core` passou a participar também da execução
  rápida, com teste unitário protegendo o vínculo. `python -m unittest
  tests.unit.test_tst7_runner` passou. A repetição de `make test-tst7-quick`
  executou todos os casos host-only, incluindo o novo caso, e terminou
  `BLOCKED` somente em `test-tst3-sanitize` pela ausência do runtime LLVM;
  esse bloqueio de ambiente permaneceu explícito e o baseline não foi alterado.
  A sincronização também restaurou `coverage_mode=integration` nos comandos
  cobertos pelo dispatcher, preservando o contrato real de cobertura.

- Incremento Shell/Wi-Fi concluido em 2026-09-04 11:32 (America/Sao_Paulo).
  Foi adicionada uma fixture host-only que executa os handlers reais de
  `src/shell/shell_commands_wifi.c` sobre um gerenciador PCI/USB estatico,
  cobrindo status, inventario, locais PCI/USB, estados de interface, scan,
  conexao, entradas invalidas, erros e indisponibilidade do radio. O teste
  terminou `PASS` com cobertura instrumentada e sem hardware ou rede externa.
  A sincronizacao, renderizacao e `make catalog-test` passaram. O catalogo
  registra 7.219 superficies, 5.267 `COVERED`, 1.952 `PENDING` e 139 casos.

- Incremento Process/runtime concluido em 2026-09-04 11:45 (America/Sao_Paulo).
  A fixture host-only existente foi expandida para exercitar o bootstrap sem
  cache, a pre-condicao invalida de inicio do scheduler, o descarte apos falha
  de criacao e os caminhos de copia e cancelamento de uma espera ativa. O
  teste `make test-process-host` passou apos `q3check`, build limpo e os
  relatorios instrumentados; sincronizacao, renderizacao, `make catalog-test`
  e os testes dos runners tambem passaram. Cinco superficies reais de
  `src/process/process.c` deixaram `PENDING`; as rotinas de stack que exigem
  endereco de 32 bits permanecem explicitamente pendentes para fixture QEMU.
  O catalogo registra 7.219 superficies, 5.272 `COVERED`, 1.947 `PENDING` e
  139 casos.

- Incremento Storage/FAT32 concluido em 2026-09-04. Foi criada a fixture
  host-only `host:storage:storage-fat32`, com uma imagem FAT32 minima em
  memoria e duas copias de FAT, sem hardware ou armazenamento real. O caso
  exercitou validacao de BPB, FSInfo e FATs, marcacao de volume invalido,
  montagem, cadeias de dois clusters, escrita/leitura, nomes longos,
  substituicao, remocao e os writers transacionais com finish e abort. O
  relatorio instrumentado terminou `PASS`, sem enderecos desconhecidos ou
  simbolos ambiguos. Passaram `make test-storage-fat32-host`, `make q3check`,
  `make clean`, `make`, `make test-tst7-quick` para todos os casos host-only
  (com resultado global `BLOCKED` somente por `test-tst3-sanitize` sem runtime
  LLVM), sincronizacao, renderizacao e `make catalog-test`. As 18 superficies
  reais de `src/fs/storage.c` que estavam `PENDING` deixaram esse estado; o
  catalogo registra 7.219 superficies, 5.290 `COVERED`, 1.929 `PENDING` e
  140 casos. O bloqueio do sanitizador permanece explicito e o baseline TST7
  nao foi alterado.

- Incremento Core/update concluido em 2026-09-04 12:30 (America/Sao_Paulo).
  Foi criada a fixture host-only `host:core:update`, com doubles estaticos de
  crypto e filesystem, para executar diretamente os helpers de serializacao e
  validacao de `src/core/update.c`. O caso cobriu round-trip e corrupcao dos
  registros U3/U4, limites e algoritmos dos headers ZUPD, paths, tabela de
  entradas, comparacao de versoes, resultados de acao e cancelamento. O
  relatorio `build/test-results/update-host/coverage.json` terminou `PASS`,
  sem enderecos desconhecidos ou simbolos ambiguos. Passaram
  `make test-update-host`, sincronizacao, renderizacao, `make catalog-test` e
  `git diff --check`; o catalogo registra 7.231 superficies, 5.356
  `COVERED`, 1.875 `PENDING` e 141 casos. As rotinas de transacao que exigem
  filesystem mutavel, slots e reboot continuam pendentes para uma fixture
  integrada, sem serem mascaradas.

- Incremento Core/update — contrato publico de indisponibilidade concluido em
  2026-09-04 (America/Sao_Paulo). A fixture existente foi ampliada para chamar
  `update_init`, capacidades, status, versao instalada, verificacao, aplicacao,
  rollback, sincronizacao, historico e nomes de estado com filesystem ausente.
  O resultado permaneceu `PASS`, sem transformar `ERR_UNAVAILABLE` em sucesso de
  atualizacao. Depois de `make q3check`, `make clean` e `make`, passaram
  `make test-update-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao e `make catalog-test`. A evidencia dinamica agora
  resolve 76 superficies de `src/core/update.c`; as transacoes FAT12 com slots
  mutaveis continuam pendentes para uma fixture integrada. O catalogo registra
  7.240 superficies, 5.417 `COVERED`, 1.823 `PENDING` e 142 casos.

- Incremento Core/update remote runtime concluido em 2026-09-04. Foi criada a
  fixture host-only `host:core:update-remote-runtime`, com buffers estaticos e
  doubles de HTTP, filesystem, crypto, processo e estado de atualizacao. O
  caso exercita diretamente serializacao, CRC, parsing JSON, validacao de
  descriptor, selecao de origem, transporte, download, cache, abortamento e
  contratos publicos de `src/core/update_remote_runtime.c`, incluindo caminhos
  negativos e limites. O relatorio
  `build/test-results/update-remote-runtime-host/coverage.json` terminou
  `PASS`, resolveu 64 superficies reais do arquivo e nao registrou enderecos
  desconhecidos ou simbolos ambiguos. Passaram `make q3check`, `make clean`,
  `make`, `make test-update-remote-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao, `make catalog-test` e `git diff --check`. O
  catalogo registra 7.245 superficies, 5.469 `COVERED`, 1.776 `PENDING` e
  143 casos; o gate estrito e a validacao TST7 completa continuam pendentes
  pelas superficies de software ainda sem evidencia.

- Incremento Core/update remote concluido em 2026-09-04. Foi criada a fixture
  host-only `host:core:update-remote`, com doubles estaticos de HTTP,
  filesystem FAT12, crypto, processo e runtime. O caso exercita diretamente
  os helpers e contratos publicos de `src/core/update_remote.c`, incluindo
  manifestos, caminhos, registros redundantes, validacao de cache, download,
  cancelamento, estados, erros e limites, sem rede ou armazenamento reais. O
  relatorio `build/test-results/update-remote-host/coverage.json` terminou
  `PASS`, resolveu 54 superficies do arquivo e nao registrou enderecos
  desconhecidos ou simbolos ambiguos. Passaram `make test-update-remote-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, sincronizacao, renderizacao,
  `make catalog-test` e `git diff --check`; o build limpo e o `q3check` desta
  etapa tambem passaram antes do commit. O catalogo registra 7.251
  superficies, 5.546 `COVERED`, 1.705 `PENDING` e 144 casos. O gate estrito e
  a validacao TST7 completa continuam pendentes pelas superficies sem
  evidencia real.

- Incremento Core/update system slots concluido em 2026-09-04. Foi criada a
  fixture host-only `host:core:update-system-slots`, com filesystem, volume,
  crypto, armazenamento e estado de boot simulados em buffers estaticos. O
  caso exercita diretamente serializacao de estado e journal, validacao,
  redundancia, recuperacao, paths, limites e contratos publicos sem
  armazenamento real ou reboot. O relatorio
  `build/test-results/update-system-slots-host/coverage.json` terminou `PASS`,
  resolveu 56 superficies reais e nao registrou enderecos desconhecidos ou
  simbolos ambiguos. Passaram `make test-update-system-slots-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, sincronizacao, renderizacao,
  `make catalog-test`, o build limpo, `q3check` e os testes unitarios de
  catalogo, runner host e TST7. O catalogo registra 7.252 superficies, 5.616
  `COVERED`, 1.636 `PENDING` e 145 casos. O gate estrito e a validacao TST7
  completa continuam pendentes pelas superficies sem evidencia real.

- Incremento Core/update remote system concluido em 2026-09-04
  (America/Sao_Paulo). Foi criada a fixture host-only
  `host:core:update-remote-system`, com filesystem, volume, crypto,
  armazenamento e transporte simulados em buffers estaticos. O caso exercita
  diretamente serializacao do controle, cache redundante, hash, verificacao,
  transferencia transacional, limpeza, estados, limites e contratos publicos
  de `src/core/update_remote_system.c`, sem rede ou armazenamento real. O
  relatorio `build/test-results/update-remote-system-host/coverage.json`
  terminou `PASS`, resolveu 56 superficies reais e nao registrou enderecos
  desconhecidos ou simbolos ambiguos. Passaram
  `make test-update-remote-system-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao, `make catalog-test`, `q3check`, `make clean`,
  `make` e os testes unitarios de catalogo, runner host e TST7. O catalogo
  registra 7.253 superficies, 5.651 `COVERED`, 1.602 `PENDING` e 146 casos.
  O gate estrito e a validacao TST7 completa continuam pendentes pelas
  superficies sem evidencia real.

- Incremento Core/update system concluido em 2026-09-04
  (America/Sao_Paulo). Foi criada a fixture host-only
  `host:core:update-system`, com filesystem, crypto, HTTP, processo e consulta
  GitHub simulados em buffers estaticos. O caso exercita diretamente os
  contratos de imagem ZSYS e transferencia remota de
  `src/core/update_system.c`, incluindo headers, componentes, compatibilidade,
  hashes, assinatura, limites, estados e callbacks de transferencia, sem rede
  ou armazenamento reais. O relatorio
  `build/test-results/update-system-host/coverage.json` terminou `PASS`,
  resolveu 35 superficies reais e nao registrou enderecos desconhecidos ou
  simbolos ambiguos. Passaram `make test-update-system-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, sincronizacao, renderizacao,
  `make catalog-test`, `q3check`, `make clean`, `make` e os testes unitarios de
  catalogo, runner host e TST7. O catalogo registra 7.256 superficies, 5.767
  `COVERED`, 1.489 `PENDING` e 149 casos. O gate estrito e a validacao TST7
  completa continuam pendentes pelas superficies sem evidencia real.

- Incremento Core/update transacional concluido em 2026-09-04
  (America/Sao_Paulo). A fixture existente `host:core:update` passou a usar um
  filesystem FAT12 em memoria, com pacote ZUPD e alvos simulados em buffers
  estaticos. O caso exercita baseline, leitura e escrita redundante, apply seco
  e real, staging, backups, journal, commit, rollback, cancelamento, failpoints
  de substituicao, recuperacao no boot, historico corrompido e sincronizacao de
  estado. O relatorio `build/test-results/update-host/coverage.json` terminou
  `PASS`, resolveu todas as 129 superficies de `src/core/update.c` e nao
  registrou enderecos desconhecidos ou simbolos ambiguos. Passaram
  `make test-update-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao, `make catalog-test` e `git diff --check`. O
  catalogo registra 7.256 superficies, 5.821 `COVERED`, 1.435 `PENDING` e 149
  casos; o gate estrito e a validacao TST7 completa continuam pendentes pelas
  superficies sem evidencia real.

- Incremento Core/update remote GitHub concluido em 2026-09-04
  (America/Sao_Paulo). Foi criada a fixture host-only
  `host:core:update-remote-github`, com respostas JSON, HTTP, crypto e
  cancelamento simulados em buffers estaticos. O caso exercita diretamente
  parser JSON, assets, duplicidades, limites, URLs allowlisted, fingerprints,
  espera, cancelamento, status HTTP e contratos publicos de
  `src/core/update_remote_github.c`, sem rede externa. O relatorio
  `build/test-results/update-remote-github-host/coverage.json` terminou
  `PASS`, resolveu 44 superficies reais e nao registrou enderecos desconhecidos
  ou simbolos ambiguos. Passaram
  `make test-update-remote-github-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao, `make catalog-test`, `q3check`, `make clean`,
  `make` e os testes unitarios de catalogo, runner host e TST7. O catalogo
  registra 7.254 superficies, 5.694 `COVERED`, 1.560 `PENDING` e 147 casos.
  O gate estrito e a validacao TST7 completa continuam pendentes pelas
  superficies sem evidencia real.

- Incremento Core/update remote release concluido em 2026-09-04
  (America/Sao_Paulo). Foi criada a fixture host-only
  `host:core:update-remote-release`, com descritor JSON, HTTP, crypto, canal
  remoto e consulta GitHub simulados em buffers estaticos. O caso exercita
  diretamente version lock, tags, assets, hashes, URLs, truncamento, status
  HTTP, cancelamento, selecao por tag, pre-condicoes e contrato de download
  de `src/core/update_remote_release.c`, sem rede externa. O relatorio
  `build/test-results/update-remote-release-host/coverage.json` terminou
  `PASS`, resolveu 35 superficies reais e nao registrou enderecos desconhecidos
  ou simbolos ambiguos. Passaram
  `make test-update-remote-release-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao, `make catalog-test`, `q3check`, `make clean`,
  `make` e os testes unitarios de catalogo, runner host e TST7. O catalogo
  registra 7.255 superficies, 5.729 `COVERED`, 1.526 `PENDING` e 148 casos.
  O gate estrito e a validacao TST7 completa continuam pendentes pelas
  superficies sem evidencia real.

- Incremento Core/update-runtime transacional concluido em 2026-09-04
  (America/Sao_Paulo). O caso `host:core:update-runtime` foi ampliado para uma
  fixture FAT12 em memoria com estado persistente, journal, ZUPD, staging,
  backups, commit, rollback, cancelamento, failpoints, recuperacao no boot,
  cache seletivo e estados sem filesystem. O teste encontrou e corrigiu a
  perda indevida do slot de backup quando um rollback era cancelado antes da
  primeira substituicao. Passaram
  `make test-update-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  sincronizacao, renderizacao, `make catalog-test` e a cobertura resolveu as
  46 superficies pendentes de `src/core/update_runtime.c`, sem enderecos
  desconhecidos ou simbolos ambiguos.

- Incremento Core/app-remote concluido em 2026-09-04 (America/Sao_Paulo).
  Foi criada a fixture host-only `host:core:app-remote`, com filesystem FAT12,
  catalogo ZAC1, HTTP, crypto e motor de pacotes simulados em buffers estaticos.
  O caso exercita catalogo autenticado, dependencias, planejamento, preflight,
  cache alternado, aplicacao, procedencia, cancelamento, failpoint,
  recuperacao e traducao de motivos do motor AS4, sem rede ou armazenamento
  reais. Passaram `make test-app-remote-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, sincronizacao, renderizacao,
  `make catalog-test` e a cobertura resolveu as 76 superficies de
  `src/core/app_remote.c`, sem enderecos desconhecidos ou simbolos ambiguos.

- Incremento Shell/diagnostics-helpers concluido em 2026-09-04
  (America/Sao_Paulo). Os parsers, estados, cores, caminhos e invariantes
  puros dos diagnosticos foram extraidos para
  `src/shell/shell_diagnostics_helpers.c`, com header interno e fixture
  `host:shell:diagnostics-helpers`. Passou
  `make test-shell-diagnostics-helpers-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`; o relatorio terminou `PASS`,
  resolveu 34 superficies do helper e duas rotinas de string, sem enderecos
  desconhecidos ou simbolos ambiguos. A sincronizacao e a renderizacao do
  catalogo tambem foram executadas; o estado atual registra 7.290 superficies,
  6.032 `COVERED`, 1.258 `PENDING` e 151 casos. O gate estrito continua
  pendente pelas superficies restantes sem evidencia especifica.

- Incremento Process/runtime — helpers de stack — concluido em 2026-09-04
  (America/Sao_Paulo). A fixture existente `host:process:runtime` passou a
  exercitar o diagnostico de canario, os helpers de formatacao numerica/textual
  da stack, o calculo de uso em metadados invalidos e uma chamada controlada do
  idle no caminho `ZEPHYROS_HOST_TEST`, sem `hlt` ou contexto privilegiado.
  Passou `make test-process-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`;
  o relatorio terminou `PASS`, cobriu 87 simbolos e nao registrou enderecos
  desconhecidos ou simbolos ambiguos. A sincronizacao e a renderizacao do
  catalogo tambem passaram; o estado atual registra 7.292 superficies, 6.039
  `COVERED`, 1.253 `PENDING` e 151 casos. O gate estrito continua pendente
  pelas superficies restantes sem evidencia especifica.

- Incremento Drivers/RTC — fechamento das superfícies internas — concluido em
  2026-09-04 (America/Sao_Paulo). O caso existente
  `host:drivers:rtc-status` foi executado com CMOS simulado, cobrindo I/O,
  leitura bruta, espera de atualização, conversão, inicialização e leitura UTC.
  O relatório `build/test-results/rtc-status-host/coverage.json` terminou
  `PASS`, sem endereços desconhecidos ou símbolos ambíguos, e confirmou as 17
  funções de `src/drivers/rtc.c`. A sincronização e a renderização do catálogo
  também passaram; o estado atual registra 7.292 superfícies, 6.048
  `COVERED`, 1.244 `PENDING` e 151 casos. O gate estrito continua pendente
  pelas superfícies restantes sem evidência específica.

- Revalidação Shell/QEMU e fixtures host-only — concluída em 2026-09-04
  (America/Sao_Paulo). O caso independente `qemu:tst5:shell` terminou `PASS`
  em uma única iteração, com `READY`, `HEARTBEAT`, `BEGIN`, `PASS`, entrada
  QMP registrada e artefatos preservados em
  `build/test-results/qemu-20260904T201157Z-23988/`. Em seguida, as fixtures
  `host:shell:hosted`, `host:shell:input` e `host:core:syscall` passaram com
  `HOST_CC`; a sincronização dinâmica removeu as cinco superfícies restantes
  desses arquivos. O catálogo agora registra 7.292 superfícies, 6.053
  `COVERED`, 1.239 `PENDING` e 151 casos. O gate estrito continua pendente
  pelas superfícies restantes sem evidência específica.

- Incremento Core/update-remote — cancelamento do transporte — concluído em
  2026-09-04 (America/Sao_Paulo). A fixture existente
  `host:core:update-remote` passou a manter o HTTP falso em
  `HTTP_STATE_RECEIVING_BODY`, armar `update_remote_host_cancel()` por meio
  de um wrapper interno e verificar `ERR_TIMEOUT` com motivo
  `UPDATE_REMOTE_REASON_CANCELLED`. O estado pendente foi limpo antes da
  continuação dos contratos públicos; a execução terminou `PASS` e o relatório
  não registrou endereços desconhecidos ou símbolos ambíguos. Após a segunda
  execução, a sincronização confirmou o wrapper e o driver: o catálogo registra
  7.293 superfícies, 6.055 `COVERED`, 1.238 `PENDING` e 151 casos. O gate
  estrito continua pendente pelas superfícies restantes sem evidência
  específica.

- Incremento Core/syscall — contrato estático da ABI — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture existente `host:core:syscall` referencia cada
  constante `APP_SYSCALL_0..24` nos dispatches válidos, inválidos e na tabela
  de números. Como macros não são endereços executáveis, o vínculo foi
  registrado em `tests/coverage/static/syscall-abi.json` e validado pelo
  sincronizador sem associação genérica por arquivo. A sincronização e a
  renderização passaram; o catálogo registra 7.293 superfícies, 6.080
  `COVERED`, 1.213 `PENDING` e 151 casos. O gate estrito continua pendente
  pelas superfícies restantes sem evidência específica.

- Revalidação Kernel/panic — concluída em 2026-09-04 (America/Sao_Paulo). O
  caso existente `host:kernel:panic` foi executado após o build limpo com
  `HOST_CC`; a fixture capturou `panic`, `panic_halt` e `panic_memory`, incluindo
  mensagens ausentes, razões customizadas, métricas de memória e retorno
  controlado. O relatório instrumentado terminou `PASS`, sem endereços
  desconhecidos ou símbolos ambíguos. A sincronização e a renderização passaram;
  o catálogo registra 7.293 superfícies, 6.083 `COVERED`, 1.210 `PENDING` e
  151 casos. O gate estrito continua pendente pelas superfícies restantes sem
  evidência específica.

- Incremento Core/spinlock — concluído em 2026-09-04 (America/Sao_Paulo). Foi
  adicionada a fixture host-only `host:core:spinlock`, que chama
  `spinlock_init`, `spinlock_acquire` e `spinlock_release` e verifica o estado
  do lock antes e depois das operações. Passaram `make test-spinlock-host`
  com `HOST_CC`, `make catalog-test`, `make q3check`, `make clean` seguido de
  `make` e os 164 testes Python unitários. O catálogo registra 7.293
  superfícies, 6.086 `COVERED`, 1.207 `PENDING` e 152 casos; o gate estrito
  continua pendente pelas superfícies restantes sem evidência específica.

- Incremento Shell/storage commands — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture `host:shell:commands-storage` chama os
  dispatchers reais de `index` e `search` em `src/shell/shell_commands_storage.c`
  e usa doubles estáticos para índice, bloco, cache, storage e VFS. Foram
  validados argumentos nulos, desconhecidos, extras e vazios, pesquisa sem
  índice e as mensagens com `ERR_UNAVAILABLE=9`; o primeiro ajuste corrigiu a
  expectativa incorreta de `12`. O alvo passou com `HOST_CC`, a evidência
  instrumentada foi sincronizada e a visão do catálogo foi regenerada. O
  catálogo registra 7.293 superfícies, 6.095 `COVERED`, 1.198 `PENDING` e
  153 casos; o gate estrito continua pendente pelas superfícies restantes sem
  evidência específica.

- Incremento Shell/storage commands — ampliação concluída em 2026-09-04
  (America/Sao_Paulo). A mesma fixture passou a chamar os dispatchers reais de
  `blkstat`, `cachestat`, `cache`, `sync` e `storage`, além de exercitar status,
  resultados, avisos e callbacks do job cooperativo do índice. Foram cobertos
  caminhos válidos, indisponíveis, diagnósticos, limites de argumentos,
  formatos ATA/USB, volumes FAT12/FAT32 e resultados de busca com volume
  ausente ou obsoleto. O alvo passou antes e depois do build limpo; todos os
  casos host-only foram revalidados para preservar os relatórios dinâmicos.
  `python tools/test_catalog.py sync`, renderização, `make catalog-test`,
  `make q3check`, `make clean` seguido de `make` e a execução final do alvo
  passaram. O catálogo registra 7.293 superfícies, 6.128 `COVERED`, 1.165
  `PENDING` e 153 casos; o gate estrito continua pendente sem mascarar as
  superfícies ainda sem evidência.

- Incremento Shell/diagnostics commands — concluído em 2026-09-04
  (America/Sao_Paulo). Foi criada a fixture host-only `host:shell:diagnostics`,
  que chama diretamente os dispatchers reais de `pwd`, `cd`, `mouse`, `log`,
  `timer`, `clock`, `irqstat`, `wait`, `wqinfo`, `workq`, `tls`, `vfs` e `mount`. Foram validados caminhos
  válidos, argumentos extras, limites de token e velocidade, estados indisponíveis,
  níveis, histórico, filas, waiters, listagens, autotestes e preservação da configuração após rejeição. O alvo
  passou com `HOST_CC`, `-Wall -Wextra -Werror`,
  instrumentação dinâmica e doubles estáticos de VFS, mouse, vídeo, log, timer,
  RTC, IRQ, IDT, wait, workqueue, TLS, mounts e descritores. A
  compilação host
  usa descarte explícito de metadados de unwind para permitir a coleta por
  seções no MinGW; o build freestanding não usa essa configuração. A evidência
  foi sincronizada e a visão do catálogo regenerada; o catálogo registra 7.293
  superfícies, 6.199 `COVERED`, 1.094 `PENDING` e 154 casos. As pendências
  restantes continuam explícitas.

- Incremento Shell/diagnostics devcheck — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  exercitar `devcheck` com resultado estruturado de devfs falso, cobrindo
  sucesso 9/9, falha por `ERR_STATE`, contagem parcial e argumentos inválidos.
  O alvo `make test-shell-diagnostics-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  passou com `-Wall -Wextra -Werror`; o catálogo foi sincronizado e a visão
  renderizada. O incremento moveu duas superfícies de
  `src/shell/shell_commands_diagnostics.c` para `COVERED`, sem hardware ou
  armazenamento real: 7.293 superfícies, 6.199 `COVERED`, 1.094 `PENDING` e
  154 casos. As pendências restantes continuam explícitas.

- Incremento Shell/diagnostics devices e USB — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  exercitar os dispatchers reais de `devices`, `device-info` e `usb`, com
  inventário PCI e ATA falso, controladora EHCI, portas, dispositivos HID e
  MSC simulados. Foram validados inventário detalhado, busca por identificador,
  estados prontos/degradados, listagens, limites, argumentos inválidos,
  dispositivos ausentes e indisponibilidade coerente. O alvo
  `make test-shell-diagnostics-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  passou com `-Wall -Wextra -Werror` e instrumentação dinâmica; os relatórios
  foram sincronizados e a visão do catálogo regenerada. O incremento resolveu
  16 superfícies observadas de `src/shell/shell_commands_diagnostics.c`, sem
  hardware ou armazenamento real: 7.293 superfícies, 6.215 `COVERED`, 1.078
  `PENDING` e 154 casos. As pendências restantes continuam explícitas.

- Incremento Shell/diagnostics SLAB — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  exercitar os dispatchers reais de `slabinfo` e `slabtest` com um registrador
  SLAB estático. Foram validados metadados formatados, cache não inicializado,
  registro vazio, indisponibilidade, execução bem-sucedida, falha do autoteste
  e argumentos inválidos. O alvo `make test-shell-diagnostics-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` passou com `-Wall -Wextra -Werror`
  e instrumentação dinâmica; os relatórios foram regenerados após o build
  limpo, sincronizados e a visão do catálogo validada. O incremento resolveu
  quatro superfícies observadas de `src/shell/shell_commands_diagnostics.c`,
  sem tocar no allocator real: 7.293 superfícies, 6.219 `COVERED`, 1.074
  `PENDING` e 154 casos. As pendências restantes continuam explícitas.

- Incremento Shell/diagnostics CPU, page fault, VMA e scheduler — concluído em
  2026-09-04 (America/Sao_Paulo). A fixture host-only
  `host:shell:diagnostics` passou a chamar os dispatchers reais de `cpu usage`,
  `pagefault`, `vmamap` e `schedcheck`, com estatísticas de scheduler,
  processos, VMAs e page faults falsos em buffers estáticos. Foram exercitados
  linha-base e percentuais de CPU, mapas de código/stack, PID inexistente,
  processo não-usuário, VMA indisponível, argumentos inválidos e falha de
  invariantes. O alvo específico e os demais 111 alvos host que alimentam o
  catálogo passaram com `HOST_CC`; também passaram `make q3check`, `make clean`
  seguido de `make`, sincronização, renderização, `make catalog-test` e
  `git diff --check`. O catálogo registra 7.293 superfícies, 6.230
  `COVERED`, 1.063 `PENDING` e 154 casos; as pendências restantes continuam
  explícitas.

- Incremento Shell/diagnostics MemCheck — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  chamar o dispatcher real de `memcheck` e `shell_diagnostics_run_memcheck`.
  Foram validadas as seis flags do resultado, argumentos inválidos, processo
  ring 3 ou zumbi pendente, aplicação em foreground, falha de validação SLAB e
  limpeza dos três blocos estáticos nos caminhos de sucesso e erro. O alvo
  `make test-shell-diagnostics-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  passou com `-Wall -Wextra -Werror` e instrumentação dinâmica após
  `make clean` seguido de `make`; `make q3check`, `make catalog-test` e
  `git diff --check` também passaram. A evidência foi sincronizada e a visão
  do catálogo regenerada. O catálogo registra 7.293 superfícies, 6.234
  `COVERED`, 1.059 `PENDING` e 154 casos; as pendências restantes continuam
  explícitas.

- Incremento Shell/diagnostics KMetrics — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  chamar o dispatcher real de `kmetrics`, cobrindo a linha-base desde boot,
  `kmetrics reset`, deltas de PIT, scheduler, teclado, IPC, PMM, heap, paging
  user, paging boot e VESA. Também foram exercitados argumentos inválidos,
  paging boot indisponível e VESA sem backbuffer, sem hardware ou estado real
  do kernel. O alvo com `HOST_CC` passou com `-Wall -Wextra -Werror` e
  instrumentação dinâmica; `make catalog-test` e `git diff --check` também
  passaram. A evidência foi sincronizada e a visão renderizada. O catálogo
  registra 7.293 superfícies, 6.241 `COVERED`, 1.052 `PENDING` e 154 casos;
  as pendências restantes continuam explícitas.

- Incremento Shell/diagnostics device-scan — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  exercitar o fluxo integrado de `device-scan`, incluindo PCI, USB, storage,
  refresh de mounts, file index, inventário de dispositivos, rede, Wi-Fi,
  recovery e os seis pontos de `process_yield`. Foram validados o caminho
  pronto, a inicialização tardia de USB/Wi-Fi, inventários parciais por
  overflow, degradações opcionais, falha fatal de PCI, resultado nulo e
  argumentos inválidos, sempre com doubles estáticos e sem hardware real.
  Passaram `make test-shell-diagnostics-host` com `HOST_CC`, `make q3check`,
  `make clean`, `make`, os 117 alvos host-only, sincronização, renderização,
  `make catalog-test` e `git diff --check`. A evidência dinâmica resolveu
  três superfícies reais de `src/shell/shell_commands_diagnostics.c`; o
  catálogo registra 7.293 superfícies, 6.244 `COVERED`, 1.049 `PENDING` e
  154 casos. `power` e `acpi` continuam separados para o próximo incremento.

- Incremento Shell/diagnostics power e ACPI — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  chamar os dispatchers reais de `power` e `acpi` com snapshots estáticos.
  Foram exercitados estados disponíveis, degradados e indisponíveis,
  capacidades, serviço, fase, quiescência, tabelas ACPI, MADT, falhas de
  consulta, listagem, argumentos inválidos e ausência de operações reais de
  desligamento ou reinício. Passaram `make test-shell-diagnostics-host` com
  `HOST_CC`, `make q3check`, `make clean`, `make`, os 117 alvos host-only,
  sincronização, renderização, `make catalog-test` e `git diff --check`.
  A evidência dinâmica será sincronizada para as superfícies efetivamente
  chamadas; as pendências restantes continuam explícitas.

- Incremento Shell/diagnostics sinais — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  chamar os dispatchers reais de `sigtest` e `kill`. Foram exercitados o
  resultado estruturado do autoteste, todos os indicadores publicados,
  conversão de nomes e números, envio para processo de usuário, falha do
  destino, PID inexistente e argumentos inválidos, usando apenas um processo
  estático e sem estado persistente. O alvo específico passou com `HOST_CC`;
  `make q3check`, `make clean`, `make`, a matriz completa de 117 alvos
  host-only, sincronização/renderização do catálogo, `make catalog-test` e
  `git diff --check` passaram. O catálogo ficou com 7.293 superfícies, sendo
  6.259 `COVERED` e 1.034 `PENDING`, e 154 casos; as pendências restantes não
  foram mascaradas.

- Incremento Shell/diagnostics proccheck — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  chamar o dispatcher real de `proccheck` junto com o `shell_introspection.c`
  real. O VFS estático cobriu `/proc`, `/sys`, atributos de dispositivos,
  processos, leitura por cursor, EOF, controles de log, permissões, caminhos
  ausentes e tentativas de escrita somente leitura, sem armazenamento ou
  processos reais. Passaram `make test-shell-diagnostics-host` com `HOST_CC`,
  sincronização/renderização do catálogo, `make catalog-test` e
  `git diff --check`; o catálogo ficou com 7.293 superfícies, sendo 6.267
  `COVERED` e 1.026 `PENDING`. As pendências restantes continuam explícitas.

- Incremento Shell/diagnostics sysfs — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  executar `devices -v` e `device-info` pelo caminho real de atributos sysfs,
  verificando vendor, device, class e fechamento dos handles no VFS estático.
  Passaram o alvo específico e a sincronização da evidência dinâmica; o
  catálogo ficou com 7.293 superfícies, sendo 6.269 `COVERED` e 1.024
  `PENDING`. As pendências restantes continuam explícitas.

- Incremento Shell/diagnostics health — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only `host:shell:diagnostics` passou a
  chamar os dispatchers reais de `health`, `health summary` e `health check`,
  cobrindo argumentos inválidos, resumo, verificação saudável e VFS
  indisponível com estado estático. Passaram o alvo específico, `make q3check`,
  `make clean` e `make`, além da matriz host-only completa com 115/115 alvos.
  A evidência foi sincronizada, o catálogo e a visão renderizada foram
  validados e ficaram com 7.293 superfícies, sendo 6.309 `COVERED` e 984
  `PENDING`; as pendências restantes continuam explícitas.

- Incremento Shell/network-checks — concluído em 2026-09-04
  (America/Sao_Paulo). Foi criado o caso host-only
  `host:shell:network-checks`, que chama a validação real de invariantes de
  rede do Shell com uma interface PCI estática. A fixture passou pelo estado
  coerente, rejeitou interface inconsistente com `ERR_STATE` e propagou
  `ERR_UNAVAILABLE` quando o estado agregado não estava disponível. Passaram
  o alvo específico, sincronização/renderização e `make catalog-test`; o
  catálogo ficou com 7.293 superfícies, sendo 6.319 `COVERED` e 974 `PENDING`.

- Incremento Shell/checks internos — concluído em 2026-09-04
  (America/Sao_Paulo). Foi criado o caso host-only `host:shell:checks` com um
  ponto de entrada interno ativo somente em `ZEPHYROS_HOST_TEST`. A fixture
  chamou helpers reais de `shell_checks.c` para fases, resumo compacto,
  saturação de falhas, estados de job, comparação de inventários,
  validações ACPI/MADT e emissão de fixtures ZAPP. Passaram o alvo específico,
  `make q3check`, `make clean`, `make`, a matriz host-only completa com 117/117
  alvos, sincronização/renderização do catálogo, `make catalog-test` e
  `git diff --check`. A evidência dinâmica cobriu 29 superfícies reais de
  `src/shell/shell_checks.c`; o catálogo registra 7.295 superfícies, sendo
  6.351 `COVERED` e 944 `PENDING`. As pendências restantes continuam
  explícitas.

- Incremento Shell/network helpers — concluído em 2026-09-04
  (America/Sao_Paulo). A fixture host-only existente
  `host:shell:network-checks` passou a chamar diretamente os helpers reais de
  `shell_commands_network.c` por uma entrada compilada somente sob
  `ZEPHYROS_HOST_TEST`. Foram exercitados parsers de IPv4, porta, ping e URL,
  limites e argumentos nulos, estados de interface, destinos Ethernet,
  comparação de identificadores, gateway/interface, conversão de ticks e
  fases do job cooperativo. Passaram o alvo específico com `HOST_CC`,
  `make q3check`, `make clean`, `make`, a matriz host-only completa com
  117/117 alvos, sincronização/renderização do catálogo, `make catalog-test` e
  `git diff --check`. A evidência cobriu 30 superfícies reais, sem endereços
  desconhecidos ou símbolos ambíguos. O catálogo registra 7.297 superfícies,
  6.368 `COVERED`, 929 `PENDING` e 156 casos; as pendências restantes
  continuam explícitas.

- Incremento Shell/commands packages — concluído em 2026-09-04
  (America/Sao_Paulo). Foi criado o caso host-only
  `host:shell:commands-packages` com entrada interna compilada somente sob
  `ZEPHYROS_HOST_TEST`. A fixture chamou diretamente os helpers reais de
  `shell_commands_packages.c` para extensões `.ZPK`, normalização de IDs,
  tokens com espaços e truncamento, argumentos nulos e seleção de ações de
  `pkg`, `store` e `update`, sem iniciar jobs nem tocar em armazenamento real.
  Passaram o alvo específico com `HOST_CC`, `make q3check`, `make clean`,
  `make`, sincronização/renderização do catálogo e `make catalog-test`. A
  evidência dinâmica resolveu 10 superfícies reais, sem endereços desconhecidos
  ou símbolos ambíguos; o catálogo registra 7.299 superfícies, 6.372
  `COVERED`, 927 `PENDING` e 157 casos. As pendências restantes continuam
  explícitas.

- Incremento Shell/UI App Store — concluído em 2026-09-05 (America/Sao_Paulo).
  Foi criado o caso host-only `host:ui:appstore`, com entrada interna ativa
  somente em `ZEPHYROS_HOST_TEST`. A fixture chamou diretamente os helpers
  reais de `src/appstore/appstore.c` para validar formatação e truncamento,
  dependências, bloqueios, seleção e restauração, planos de downgrade,
  estados, rollback, confiança e geometria da interface, usando doubles
  estáticos e sem iniciar workers ou acessar hardware/armazenamento. Passaram
  `make test-appstore-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, a
  sincronização/renderização da cobertura e `make catalog-test`. A evidência
  dinâmica resolveu 32 superfícies reais, sem endereços desconhecidos ou
  símbolos ambíguos. O catálogo registra 7.307 superfícies, 6.383 `COVERED`,
  924 `PENDING` e 158 casos; as pendências restantes continuam explícitas.

- Incremento Shell/network reports — concluído em 2026-09-05
  (America/Sao_Paulo). A fixture host-only existente
  `host:shell:network-checks` passou a chamar os relatórios somente leitura
  reais de estado de link, interface, MAC, IPv4, Ethernet, rotas, DHCP, ICMP,
  status agregado e dispositivos, usando doubles estáticos. Foram cobertos
  caminhos nulos, estado vazio e dados válidos sem rede, hardware ou
  armazenamento reais. Passou `make test-shell-network-checks-host` com
  `HOST_CC`, e o relatório dinâmico terminou com `PASS`, sem endereços
  desconhecidos ou símbolos ambíguos. A sincronização do catálogo cobriu 20
  superfícies novas e registra 7.307 superfícies, 6.403 `COVERED`, 904
  `PENDING` e 158 casos; as pendências restantes continuam explícitas.

- Incremento Shell/checks e fixtures ZAPP — concluído em 2026-09-05
  (America/Sao_Paulo). A fixture host-only existente `host:shell:checks` foi
  ampliada para chamar diretamente os formatadores de resultado, a
  classificação de filesystem/loader indisponível e os builders das imagens
  ZAPP de demonstração, VMA, page fault, entrada e cancelamento. Foram
  exercitados estados disponíveis e indisponíveis, cabeçalhos, tamanhos e
  invariantes estruturais sem iniciar processos, acessar armazenamento ou
  hardware reais. Passou `make test-shell-checks-host` com `HOST_CC`, e a
  evidência dinâmica resolveu 11 superfícies novas sem endereços desconhecidos
  ou símbolos ambíguos. A sincronização registra 7.307 superfícies, 6.414
  `COVERED`, 893 `PENDING` e 158 casos; as pendências restantes continuam
  explícitas.
