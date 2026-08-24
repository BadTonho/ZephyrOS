# Registro de validações

Este documento é o histórico cronológico de implementações, testes e
conclusões de fase. Cada entrada registra a evidência reproduzível e o horário
real. Os roadmaps mantêm apenas o estado e o link para a entrada correspondente.

Não registrar chaves privadas, senhas, tokens, caminhos pessoais ou outros
segredos.

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
