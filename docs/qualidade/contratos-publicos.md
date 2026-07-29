# Catalogo de contratos publicos

Este catalogo associa cada header em `src/include/` ao documento tecnico que
descreve seu contrato. Quando um desses headers mudar, o `make q3check` exige
que o documento correspondente seja atualizado no mesmo conjunto de mudancas.

| Header publico | Documento canonico |
|---|---|
| `src/include/apps/editor.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/guitest.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/mediaplayer.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/shell.h` | `docs/09-shell/shell.md` |
| `src/include/apps/taskmanager.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/core/app_api.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_builtin.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_files.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_loader.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_package.h` | `docs/13-aplicativos/pacotes.md` |
| `src/include/core/arp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/crypto.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/device_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/dhcp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/dns.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/errors.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/ethernet.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/http.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/icmp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/ipv4.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/keyboard.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/log.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/memory.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/net_socket.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/network_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/panic.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/power.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/recovery.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/spinlock.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/string.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/syscall.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/tcp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/timer.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/udp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/update.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/update_trust.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/video.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/version.h` | `docs/04-kernel/kernel.md` |
| `src/include/drivers/acpi.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/ac97.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/ata.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/e1000.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/font.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/idt.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/mouse.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/pci.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/rtl8139.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/speaker.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/tss.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/vesa.h` | `docs/05-drivers/drivers.md` |
| `src/include/fs/bmp.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat12.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat32.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/wav.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/memory/compress.h` | `docs/06-memoria/memoria.md` |
| `src/include/memory/paging.h` | `docs/06-memoria/memoria.md` |
| `src/include/process/process.h` | `docs/07-processos/processos.md` |
| `src/include/process/thread.h` | `docs/07-processos/processos.md` |
| `src/include/types.h` | `docs/02-arquitetura/arquitetura.md` |
| `src/include/ui/desktop.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/filemanager.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/gui.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/icons.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/settings.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/taskbar.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/updater.h` | `docs/14-atualizacoes/system-updater.md` |
| `src/include/ui/wm.h` | `docs/12-desktop/desktop.md` |

O contrato de `src/include/process/process.h` inclui, desde a K2, os
contadores de motivo do scheduler, o quantum fixo de usuario e a validacao de
invariantes descritos em `docs/07-processos/processos.md`.

Os contratos de `src/include/core/memory.h` e `src/include/memory/paging.h`
incluem, desde a K3, estatisticas seguras do heap/PMM e do ciclo de vida de
diretorios de usuario. Seus detalhes tecnicos permanecem em
`docs/04-kernel/kernel.md` e `docs/06-memoria/memoria.md`, respectivamente.

Desde a S1.4, `src/include/drivers/acpi.h` inclui os indicadores
`mode_enable_available` e `s5_transition_ready`, alem da operacao terminal
`acpi_enter_s5()`. `src/include/core/power.h` expoe os indicadores derivados e
centraliza todos os caminhos de desligamento em `power_shutdown()`. Os
contratos canonicos permanecem, respectivamente, em
`docs/05-drivers/drivers.md` e `docs/04-kernel/kernel.md`.

Desde a S2.5, `src/include/core/network_manager.h` preserva o snapshot PCI e
os IDs estaveis de rede, acrescentando a disponibilidade e configuracao das
camadas Ethernet, ARP, IPv4 e ICMP. `src/include/core/ethernet.h` define a
abstracao de interface, montagem, polling, contadores L2 e despacho sincrono
por EtherType.
`src/include/core/arp.h` define IPv4 canonico, configuracao local em RAM,
resolucao assincrona, cache limitado e consultas por copia.
`src/include/core/ipv4.h` define configuracao estatica, visao sincrona de
datagrama, despacho por protocolo, envio assincrono em relacao ao ARP,
contadores e invariantes. `src/include/core/icmp.h` define Echo, a sessao unica
de ping, eventos por tentativa, RTT e o reply automatico.
`src/include/drivers/e1000.h` limita o driver ao Intel `8086:100E`, expoe uma
fila RX fixa e mantem consultas por copia. Os contratos canonicos permanecem
em `docs/04-kernel/kernel.md` e `docs/05-drivers/drivers.md`.

Desde a S2.6, `src/include/core/udp.h` define endpoints fixos, visao sincrona
de datagrama, checksum e envio unicast/broadcast. `src/include/core/dhcp.h`
define a maquina de estados de aquisicao e renovacao, leases por copia e
eventos consumidos pelo Network Manager. `src/include/core/dns.h` define
consulta A assincrona, CNAME limitado, cache com TTL e consultas por copia.
Os contratos de IPv4 e Network Manager tambem incluem broadcast limitado,
origem da configuracao e coordenacao estatica/DHCP/DNS.

Desde a S2.7, `src/include/core/tcp.h` define conexoes clientes com handles
geracionais, eventos sincronizados, retransmissao e consultas por copia.
`src/include/core/net_socket.h` expoe sockets `STREAM` nativos com filas
limitadas e operacoes nao bloqueantes. `src/include/core/http.h` define uma
sessao HTTP GET limitada e acesso somente-leitura ao corpo recebido. IPv4 e
Network Manager passam a expor o protocolo TCP, disponibilidade dos novos
modulos e contagens ativas.

Desde a S2.8, `src/include/core/ethernet.h` define um registro de quatro NICs,
callbacks com contexto opaco, TX direcionado e status agregado/por interface.
As visoes Ethernet, IPv4 e UDP carregam o ID da interface e broadcasts
limitados exigem esse ID. `src/include/core/network_manager.h` expoe erros de
driver, interface L3, vinculo Ethernet e DHCP pendente. Os headers de E1000 e
RTL8139 inicializam o dispositivo PCI exato; IDT oferece handlers
compartilhados e PCI confirma I/O Space com Bus Mastering.

Desde a U2, `src/include/core/crypto.h` define SHA-2 incremental, verificacao
Ed25519 e autotestes; `src/include/core/update.h` fixa motivos, metadados e
capacidades do verificador ZUPD somente-leitura; e `update_trust.h` contem
somente a raiz publica derivada. `version.h` centraliza `0.1.0`, epoch `0` e o
texto de exibicao. Os contratos permanecem em
`docs/14-atualizacoes/contrato-zupd-v1.md` e `docs/04-kernel/kernel.md`.

Desde a U3, `src/include/fs/fat12.h` e `src/include/fs/fs.h` acrescentam
consulta de arquivo raiz, escrita copy-on-write create-or-replace/replace-only
e exclusao atomica FAT12. `src/include/core/update.h` acrescenta versao
instalada, motivos de acao, aplicacao, rollback, cancelamento e failpoint
diagnostico. Os layouts redundantes de estado/journal, a ordem de commit e os
retornos FAT32 permanecem canonicos em
`docs/14-atualizacoes/contrato-zupd-v1.md` e
`docs/08-sistema-arquivos/sistema-arquivos.md`.

Desde a U4, `src/include/core/update.h` acrescenta estados de armazenamento,
status agregado, historico redundante e conversores textuais estaveis.
`src/include/ui/updater.h` define o ciclo de vida e a entrada Classic/Modern
do System Updater. Os contratos canonicos permanecem em
`docs/14-atualizacoes/contrato-zupd-v1.md` e
`docs/14-atualizacoes/system-updater.md`.
