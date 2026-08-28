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
