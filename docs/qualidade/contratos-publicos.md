# Catalogo de contratos publicos

Este catalogo associa cada header em `src/include/` ao documento tecnico que
descreve seu contrato. Quando um desses headers mudar, o `make q3check` exige
que o documento correspondente seja atualizado no mesmo conjunto de mudancas.

A Fase 5 acrescenta somente campos e funcoes ao fim dos contratos alterados:
geracao de execucao no Shell Job, App Loader, pacotes e operacoes remotas,
estado de drenagem, deadline e proximo despertar, geracao de operacao do
indice e contador de eventos de processos. `shell_job.h` acrescenta
`next_wake_tick`, `next_wake_active`, `shell_job_set_next_wake()` e
`shell_job_clear_next_wake()` sem remover assinaturas existentes.
Nenhuma assinatura de `shell.h` ou ABI ring 3 foi alterada.
O contrato de `core/video.h` mantém o scrollback textual estático e fixa sua
capacidade em 500 linhas; `video_print()` agrupa a apresentação de saída longa
sem alterar suas assinaturas públicas.

| Header publico | Documento canonico |
|---|---|
| `src/include/apps/editor.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/guitest.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/mediaplayer.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/shell.h` | `docs/09-shell/shell.md` |
| `src/include/apps/shell_dispatch.h` | `docs/09-shell/refatoracao-shell.md` |
| `src/include/apps/shell_command_utils.h` | `docs/09-shell/refatoracao-shell.md` |
| `src/include/apps/shell_input.h` | `docs/09-shell/refatoracao-shell.md` |
| `src/include/apps/shell_runtime.h` | `docs/09-shell/refatoracao-shell.md` |
| `src/include/apps/shell_job.h` | `docs/09-shell/refatoracao-shell.md` |
| `src/include/apps/taskmanager.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/core/app_api.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_catalog.h` | `docs/13-aplicativos/app-store.md` |
| `src/include/core/app_builtin.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_files.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_loader.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_package.h` | `docs/13-aplicativos/pacotes.md` |
| `src/include/core/app_remote.h` | `docs/13-aplicativos/app-store.md` |
| `src/include/core/app_remote_config.h` | `docs/13-aplicativos/app-store.md` |
| `src/include/core/app_remote_trust.h` | `docs/13-aplicativos/app-store.md` |
| `src/include/core/arp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/crypto.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/clock.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/core/device_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/dhcp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/dns.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/errors.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/ethernet.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/http.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/icmp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/ipv4.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/input.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/irq_deferred.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/keyboard.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/log.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/memory.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/net_socket.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/network_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/wifi_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/panic.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/power.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/recovery.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/spinlock.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/string.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/syscall.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/tcp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/timer.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/tls.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/core/wait.h` | `docs/07-processos/processos.md` |
| `src/include/core/udp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/update.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/update_runtime.h` | `docs/14-atualizacoes/contrato-zupd-v2.md` |
| `src/include/core/update_system.h` | `docs/14-atualizacoes/contrato-zsys-v1.md` |
| `src/include/core/update_system_slots.h` | `docs/14-atualizacoes/contrato-zsys-v1.md` |
| `src/include/core/update_remote.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/core/update_remote_runtime.h` | `docs/14-atualizacoes/contrato-zupd-v2.md` |
| `src/include/core/update_remote_config.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/core/update_remote_github.h` | `docs/14-atualizacoes/contrato-zupd-v2.md` |
| `src/include/core/update_trust.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/usb_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/usb_transport.h` | `docs/04-kernel/kernel.md` |
| `src/include/drivers/ehci.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/uhci.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/usb_hid.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/usb_msc.h` | `docs/05-drivers/drivers.md` |
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
| `src/include/drivers/rtl8811cu.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/rtc.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/drivers/speaker.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/tss.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/vesa.h` | `docs/05-drivers/drivers.md` |
| `src/include/fs/bmp.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/block.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat12.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat32.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/file_index.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/storage.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/wav.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/memory/compress.h` | `docs/06-memoria/memoria.md` |
| `src/include/memory/paging.h` | `docs/06-memoria/memoria.md` |
| `src/include/process/process.h` | `docs/07-processos/processos.md` |
| `src/include/process/thread.h` | `docs/07-processos/processos.md` |
| `src/include/types.h` | `docs/02-arquitetura/arquitetura.md` |
| `src/include/ui/desktop.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/display.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/filemanager.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/gui.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/icons.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/settings.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/appstore.h` | `docs/13-aplicativos/app-store.md` |
| `src/include/ui/taskbar.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/updater.h` | `docs/14-atualizacoes/system-updater.md` |
| `src/include/ui/wm.h` | `docs/12-desktop/desktop.md` |

O contrato de `src/include/process/process.h` inclui, desde a K2, os
contadores de motivo do scheduler, o quantum fixo de usuario e a validacao de
invariantes descritos em `docs/07-processos/processos.md`.
Desde a EP6.4, `process_create()` mantém a stack padrão de 4 KiB e
`process_create_with_stack_size()` aceita stacks nativas de 4 KiB a 16 KiB,
alinhadas a 16 bytes. `process_stack_get_info()`,
`process_stack_validate_all()`, `process_stack_check_current()` e
`process_stack_self_test()` expõem diagnóstico e validação sem alterar
assinaturas existentes nem a API de processos ring 3.

Os contratos de `src/include/core/memory.h` e `src/include/memory/paging.h`
incluem, desde a K3, estatisticas seguras do heap/PMM e do ciclo de vida de
diretorios de usuario. Seus detalhes tecnicos permanecem em
`docs/04-kernel/kernel.md` e `docs/06-memoria/memoria.md`, respectivamente.
Desde a EP9.2A, `memory.h` tambem delimita a pagina supervisor-only do contexto
de boot em `0x2000–0x2FFF`; nenhuma assinatura publica foi alterada.

Desde a S1.4, `src/include/drivers/acpi.h` inclui os indicadores
`mode_enable_available` e `s5_transition_ready`, alem da operacao terminal
`acpi_enter_s5()`. `src/include/core/power.h` expoe os indicadores derivados e
centraliza todos os caminhos de desligamento em `power_shutdown()`. Os
contratos canonicos permanecem, respectivamente, em
`docs/05-drivers/drivers.md` e `docs/04-kernel/kernel.md`.

Desde a S2.5, `src/include/core/network_manager.h` preserva o snapshot PCI e
os IDs estaveis de rede, acrescentando a disponibilidade e configuracao das
camadas Ethernet, ARP, IPv4 e ICMP. Na EP7.1B, o contrato acrescenta
transporte USB, metadados do RTL8811CU e IDs `net-usb-BB:DD.F-pN` sem alterar
os IDs PCI. `src/include/core/ethernet.h` define a
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

Desde a EP7.0, `src/include/core/wifi_manager.h` define um inventario somente-
leitura para candidatos PCI de rede que nao sejam E1000 ou RTL8139. Na EP7.1B,
o mesmo contrato tambem publica dispositivos USB Realtek `0x0BDA:0xC811`,
transportes PCI/USB, ID da sessao USB, porta, endereco, `bcdDevice` e contagem
de endpoints. O `rtl8811cu.h` aceita somente a revisao observada `0x0200` no
probe; `rtl8811cu_init()` valida presenca, tamanho e cabecalho de
`RTL8811.BIN`, mas retorna `ERR_UNAVAILABLE` enquanto o checksum e a sequencia
de radio nao estiverem confirmados. Nenhum firmware binario e versionado e
nenhum comando de radio, associacao ou credencial e executado. O
`network_manager.h` preserva as interfaces PCI e acrescenta transporte USB,
metadados USB e IDs `net-usb-BB:DD.F-pN`; `ethernet_interface_t` somente e
anexada para um driver em `READY`. O comando `wifi connect <ssid>` permanece
controlado e nao aceita senhas.

`wifi_manager_scan()` e `wifi_manager_connect_open()` aceitam somente a tabela
limitada de resultados e SSIDs abertos; nao ha argumento, armazenamento ou
log de senha. Enquanto o backend nao estiver `READY`, ambos retornam erro
controlado e nao executam radio.

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
`src/include/ui/updater.h` define o ciclo de vida e a entrada Simple/Classic
do System Updater. Os contratos canonicos permanecem em
`docs/14-atualizacoes/contrato-zupd-v1.md` e
`docs/14-atualizacoes/system-updater.md`.

Desde o MV0, `src/include/ui/desktop.h` define `DESKTOP_MODE_SIMPLE`,
`DESKTOP_MODE_CLASSIC` e reserva `DESKTOP_MODE_MODERN` para a interface
futura. `filemanager.h`, `settings.h` e `updater.h` expõem apenas seus
renderers implementados `SIMPLE` e `CLASSIC`. A política e os contratos
canônicos ficam em `docs/12-desktop/desktop.md`.

Desde o MV3, o modo `DESKTOP_MODE_CLASSIC` adota a aparencia Modern Dark sem
alterar as assinaturas de `ui/gui.h`, `ui/wm.h` ou `ui/taskbar.h`. O enum
`GUI_THEME_CLASSIC` permanece por compatibilidade, mas `gui_set_theme()` o
recusa com `ERR_UNAVAILABLE`; `GUI_THEME_MODERN_DARK` e a unica apresentacao
ativa. A hospedagem de aplicativos, suas areas internas e callbacks permanecem
inalterados.

Desde a U5, `src/include/core/http.h` acrescenta GET por streaming com callback
e limite definido pelo chamador. `src/include/fs/fat12.h` e
`src/include/fs/fs.h` acrescentam escrita sequencial FAT12 de ate 128 KiB.
Desde a EP6.0, `src/include/core/update_remote.h` acrescenta
`update_remote_release_t`, o hash do manifesto no resultado e as consultas
`update_remote_release_check()`/`update_remote_release_fetch()` para o
descritor `zephyros-release-v1`; a confianca continua nos artefatos assinados
ZUM1/ZUPD.

Desde a EP6.1, `src/include/drivers/rtc.h` define o snapshot UTC validado do
CMOS, seu estado e autoteste; `src/include/core/clock.h` ancora esse UTC no
monotono do PIT, expoe status, rollover, validacao e autoteste; e
`src/include/core/tls.h` define a politica, identidade de peer, motivos de
recusa, versoes de confianca atual/proxima, revogacao e autoteste. A EP6.2
acrescenta o adaptador BearSSL em `src/include/core/tls_client.h` e o estado
RDRAND em `src/include/drivers/rng.h`; CA estatica, SAN, validade temporal,
TLS 1.2 e fallback HTTP proibido sao requisitos efetivos do canal HTTPS.

`src/include/core/update_remote.h` define o transporte manual, estados,
motivos, candidato, metadados de Release, fingerprint da API e cache
redundante; `src/include/core/http.h` acrescenta HTTPS, headers configuráveis,
redirects HTTPS limitados e status TLS; `update_remote_config.h` deriva o
canal Stable versionado, o template HTTP `{tag}`, o endpoint GitHub, o
template de consulta por `{owner}`, `{repo}` e `{tag}`, a versão da API e os
três nomes de assets; `src/include/core/update_remote_github.h` mantém o
parser limitado e a descoberta por tag exata. Os contratos canonicos permanecem em
`docs/04-kernel/kernel.md`,
`docs/08-sistema-arquivos/sistema-arquivos.md` e
`docs/14-atualizacoes/distribuicao-remota.md`.

Os campos novos de `http_status_t` e `update_remote_result_t` são append-only:
segurança HTTPS, validação TLS, contador de redirects e motivo/erro BearSSL
ficam disponíveis sem renumerar os campos anteriores. Os motivos públicos
`UPDATE_REMOTE_REASON_TLS`, `UPDATE_REMOTE_REASON_REDIRECT` e
`UPDATE_REMOTE_REASON_RELEASE_API` também foram anexados ao fim do enum.

Desde o AS1/AS4, `src/include/core/app_catalog.h` define o snapshot local
somente-leitura, estados, motivos, capacidades, consultas por copia e
construtores de plano local da App Store. `src/include/core/recovery.h` acrescenta
`RECOVERY_COMPONENT_APP_STORE` ao fim da enumeracao. Os contratos canonicos
permanecem em `docs/13-aplicativos/app-store.md` e
`docs/04-kernel/kernel.md`.

Desde o AS2/AS4, `src/include/core/app_package.h` acrescenta preflights,
confirmacao, motivos de acao, bloqueadores, serializacao, planos topologicos,
status transacional, tabela de rollback por app, historico e execucao por ID instalado. O
contrato canonico permanece em
`docs/13-aplicativos/pacotes.md`.

Desde o AS3, `src/include/ui/appstore.h` define o ciclo de vida, os modos
Simple/Classic e a entrada da App Store nativa. O contrato canonico permanece
em `docs/13-aplicativos/app-store.md`.

Desde a EP6.3, `src/include/core/update_runtime.h` define o contrato ZUM2/
ZUPD v2, catálogo explícito, verificação, cache, journal, staging, rollback e
motivos próprios; `src/include/core/update_remote_runtime.h` define o transporte
seletivo/completo e seus resultados. `src/include/core/update_remote_github.h`
mantém as APIs v1 e acrescenta a consulta runtime sem renumerar enums ou
reinterpretar os assets legados. Os aliases `ZRV`, `ZTV`, `ZTS` e `ZTB` são
separados de `ZUR/ZUPD` e seus contratos canônicos estão em
`docs/14-atualizacoes/contrato-zupd-v2.md`.
`src/include/core/update.h` recebeu apenas o bridge append-only
`update_sync_runtime_state`, usado para sincronizar o estado U3 após uma
transação v2.

Desde o AS5, `src/include/core/app_remote.h` define estados, motivos, entradas,
planos, cache, procedencia e operacoes manuais do repositorio remoto.
`app_remote_config.h` fixa o canal e a URL de teste; `app_remote_trust.h`
contem somente a chave publica exclusiva da App Store e IDs revogados.
`app_package.h` acrescenta, de forma append-only, preflight e aplicacao de
plano a partir de um diretorio autenticado. Os contratos canonicos permanecem
em `docs/13-aplicativos/app-store.md` e `docs/13-aplicativos/pacotes.md`.

Desde a EP2, `src/include/drivers/ata.h` expoe quatro slots ATA, leitura e
escrita direcionadas e snapshots/contadores por dispositivo, preservando a
API global do disco legado. `src/include/fs/storage.h` define o inventario estatico de
discos e volumes, montagens, leitura direcionada, consulta de espaco livre,
escrita FAT32 do volume de sistema, cursores LFN, aliases 8.3, transacoes
atomicas, streaming, renomeacao, exclusao e `storage_check` somente leitura.
Os tipos novos foram
anexados ao final das estruturas publicas existentes. `RECOVERY_COMPONENT_STORAGE`
foi anexado ao enum de Recovery. `filemanager.h` passa a guardar volume,
geracao e nomes longos no historico Classic e `settings.h` anexa a categoria
de status Storage. Os contratos canonicos ficam em
`docs/05-drivers/drivers.md`, `docs/08-sistema-arquivos/sistema-arquivos.md`,
`docs/04-kernel/kernel.md` e `docs/12-desktop/desktop.md`.

Desde a EP3, `src/include/fs/fs.h` e `src/include/fs/storage.h` expoem cursores
retomaveis de diretorio, incluindo cluster nas entradas. `fs.h` tambem expoe a
geracao monotona do volume de boot e renomeacao FAT12 8.3 na propria entrada
de diretorio; `src/include/fs/fat12.h` publica a primitiva correspondente.
`src/include/fs/file_index.h` define as
tabelas limitadas, estados, busca, polling, cancelamento, validacao e autoteste
do indice global em RAM. `src/include/ui/filemanager.h` acrescenta
`fm_update()` para sincronizar a tela de pesquisa Classic com os eventos do
indice. Os contratos canonicos ficam em
`docs/08-sistema-arquivos/sistema-arquivos.md` e
`docs/12-desktop/desktop.md`.

Desde a R2, `src/include/core/timer.h` define proprietarios e timers por handles
geracionais, modos one-shot/periodico, estados `IDLE`, `ARMED` e `PENDING`,
inicio em milissegundos, cancelamento, consultas, estatisticas, invariantes e
autoteste privado. O contrato canonico fica em `docs/05-drivers/drivers.md`.

Desde a R3, `src/include/core/wait.h` define canais estaticos com sequencia de
condicao, disponibilidade, motivos de desbloqueio, deadlines absolutos,
estatisticas e autoteste privado. O contrato canonico fica em
`docs/07-processos/processos.md`; as APIs de processo/thread permanecem
internas ao kernel e nao alteram a ABI ring 3.

Desde a EP4.2, `src/include/core/usb_manager.h` define o inventario limitado a
oito controladores USB, runtime UHCI/EHCI, estados/motivos por porta,
velocidade, endereco, descritores principais e dispositivos configurados. Na
EP7.1B, `usb_device_info_t` tambem publica `device_revision` (`bcdDevice`),
modelo do controlador e uma tabela limitada de todos os endpoints descritos,
sem remover os campos derivados de Bulk e Interrupt consumidos por MSC e HID.
Os IDs de controlador usam `usb-pci-BB:DD.F`; dispositivos usam a sessao
`usb-dev-BB:DD.F-pN-aN`. `src/include/drivers/uhci.h` preserva os limites de
I/O e as APIs legadas; `src/include/drivers/ehci.h` acrescenta o transporte
high-speed PCI com DMA, IRQ, controle, Bulk, Interrupt, timeout e recuperacao
controlados. `src/include/core/usb_transport.h` seleciona UHCI ou EHCI sem
alterar os chamadores legados. HID e MSC continuam restritos ao UHCI nesta
etapa. O componente `RECOVERY_COMPONENT_USB` preserva os valores numericos
anteriores e `run-usb-wifi` usa `q35`/EHCI com passthrough literal do alvo.

Desde a EP4.3, `src/include/fs/block.h` define o registro estatico unificado
de provedores ATA e USB MSC, capacidades, setor de 512 bytes, leitura
setorial, contadores, ultimo erro e recusa de escrita somente-leitura.
`src/include/drivers/usb_msc.h` define os snapshots de MSC, estados, LUN,
endpoints Bulk, capacidade, contadores BOT e validacao. O ID USB preserva a
sessao UHCI no formato `usb-ms-BB:DD.F-pN-aN-l0`; `run-usb` continua apenas
com teclado e `run-usb-msc` acrescenta a fixture `storage-valid.img` em modo
somente-leitura.

Desde a EP4.4, `src/include/core/input.h` define eventos HID Usage de teclado,
eventos relativos de ponteiro, filas estaticas separadas, metricas e despacho
para os consumidores PS/2 legados. O adaptador de teclado preserva as posicoes
ABNT2 `;/:` (Usage `0x38`) e `/ ?` (Usage `0x87`) antes de entregar os
scancodes ao Shell. `src/include/core/irq_deferred.h` define a
fila limitada de conclusoes fora de contexto de IRQ, com cancelamento por
objeto de trabalho. `src/include/drivers/uhci.h` acrescenta o contrato de
Interrupt IN persistente, callback diferido, cancelamento e diagnostico
explicito de portas degradadas; o UHCI reserva TDs, buffers e fases periodicas
sem alterar Control ou Bulk. `usb_hid.h`
define o driver HID Boot para teclado/mouse, contadores, estados e validacao.
`usb hid status` e `usb hid check` inspecionam os contratos; `run-usb-hid`
conecta teclado e mouse USB ao QEMU nas portas raiz 1 e 2, respectivamente. O
parser completo de Report Descriptor,
hubs, hot-plug real e EHCI continuam fora do escopo.

Desde a EP9.0A, update_system.h define o envelope ZSYS v1, motivos de recusa,
verificação local em streaming e preflight remoto por tag. Desde a EP9.1,
update_system_slots.h define o estado A/B, journal, status, staging dry-run e
resultado detalhado; storage.h acrescenta o escritor FAT32 limitado por buffer.
update_remote.h e
update_remote_github.h acrescentam, ao final dos registros existentes, o
asset system.zsys e seus metadados, preservando as APIs e campos legados.
EP9.1 não aplica o slot, seleciona boot, reinicia ou altera boot.asm/stage2.
Na base da EP9.2, `update_system_slots.h` mantém compatibilidade de leitura
com o estado v1 e publica estado/slot/sequência de tentativa e o handoff
`ZSBH`; `update_system_slots_boot_confirm()` só promove um slot após a
inicialização essencial confirmar o mesmo registro persistido. `boot.asm`
permanece inalterado.

Desde a EP9.4A, `build\zephyros.img` usa uma imagem híbrida de 64 MiB: o FAT12
bruto continua no início para boot e recuperação e o FAT32 `ZEPHYROS` começa
no LBA 4096. `fs.h` roteia caminhos sem prefixo e `system:/` para o volume de
sistema quando montado, preserva `legacy:/` para o FAT12 e não faz fallback
silencioso. Nenhuma API altera `boot.asm`, `stage2`, slots, staging, reboot ou
journaling nesta etapa.

EP9.2A adiciona somente contratos privados de boot: o recovery loader fixo
seleciona `ZSI*.STA`, grava tentativa/rollback in-place na copia redundante ja
prealocada e autentica o ZSYS em streaming antes de executar o kernel. Nao ha
API publica nova de Shell; `update_system_slots_boot_confirm()` e `ZSBH`
permanecem o contrato entre loader e kernel. `boot.asm` continua inalterado.
