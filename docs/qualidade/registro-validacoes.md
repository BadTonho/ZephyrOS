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
