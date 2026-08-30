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
| `src/include/apps/shell_pipeline.h` | `docs/09-shell/refatoracao-shell.md` |
| `src/include/apps/shell_introspection.h` | `docs/09-shell/refatoracao-shell.md` |
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
| `src/include/core/net_buffer.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/sk_buff.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/socket.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/net_socket.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/network_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/wifi_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/panic.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/power.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/poll.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/route.h` | `docs/roadmaps/14-stack-de-rede-avancada.md` |
| `src/include/core/recovery.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/spinlock.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/string.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/syscall.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/tcp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/timer.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/tls.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/core/wait.h` | `docs/07-processos/processos.md` |
| `src/include/core/workqueue.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/udp.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/update.h` | `docs/14-atualizacoes/contrato-zupd-v1.md` |
| `src/include/core/update_runtime.h` | `docs/14-atualizacoes/contrato-zupd-v2.md` |
| `src/include/core/update_system.h` | `docs/14-atualizacoes/contrato-zsys-v1.md` |
| `src/include/core/update_system_slots.h` | `docs/14-atualizacoes/contrato-zsys-v1.md` |
| `src/include/core/update_remote.h` | `docs/14-atualizacoes/distribuicao-remota.md` |
| `src/include/core/update_remote_system.h` | `docs/14-atualizacoes/contrato-zsys-v1.md` |
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
| `src/include/fs/block_cache.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/devfs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/procfs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/sysfs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat12.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat32.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/file_index.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/storage.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/vfs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/vfs_internal.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/wav.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/memory/compress.h` | `docs/06-memoria/memoria.md` |
| `src/include/memory/paging.h` | `docs/06-memoria/memoria.md` |
| `src/include/memory/slab.h` | `docs/06-memoria/memoria.md` |
| `src/include/memory/vma.h` | `docs/06-memoria/memoria.md` |
| `src/include/process/process.h` | `docs/07-processos/processos.md` |
| `src/include/process/signal.h` | `docs/07-processos/processos.md` |
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
assinaturas existentes nem a API de processos ring 3. Desde a VFS2, processos
ring 3 reservam 8 KiB de kernel stack para o caminho de syscall VFS/Storage;
essa pilha não pertence ao espaço de usuário e não modifica a ABI.

O contrato de `src/include/process/thread.h` reserva quatro paginas (16 KiB)
para cada stack de `thread_create()`. A capacidade cobre workers cooperativos
que percorrem VFS, Storage e FAT/LFN, inclusive o pipeline do Shell, sem
alterar a ABI dos processos ou das aplicações ring 3.
Threads criadas em contexto de processo registram tambem o PID proprietario;
operacoes VFS executadas pelo worker continuam usando a tabela de descritores
desse processo quando o scheduler de processos estiver executando outro
contexto.

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
abstracao de interface, montagem, servico de causas pendentes, polling,
contadores L2 e despacho sincrono por EtherType.
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
compartilhados, ocorrencias e quantidade de handlers por linha, e PCI confirma
I/O Space com Bus Mastering.

No NET1, `src/include/core/net_buffer.h` continua definindo o lifetime e
`src/include/core/sk_buff.h` define a estrutura unificada, suas operacoes de
geometria, referencias, conclusao e metricas. As transicoes invalidas retornam
erros canonicos. `skb_self_test()` usa fixtures privadas e restaura as
metricas; a Ethernet mantem callbacks sincronos e o modelo de copia fallback.
Nenhum driver transfere ownership de DMA. Clones, fragmentos reais e zero-copy
permanecem fora deste contrato; `dev` e um handle opaco e `boot.asm` nao e
alterado.

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
escrita FAT32 de volumes gravaveis, cursores LFN, aliases 8.3, transacoes
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
autoteste privado. Na SYNC3, acrescenta o notificador append-only de callbacks
pendentes para a workqueue. O contrato canonico fica em
`docs/05-drivers/drivers.md`.

Desde a SYNC3, `src/include/core/workqueue.h` define trabalhos estaticos com
registro geracional, prioridades `HIGH`/`NORMAL`, estados `IDLE`, `READY`,
`DELAYED` e `RUNNING`, prazos absolutos, coalescencia, reexecucao,
cancelamento, snapshots com geracao/falhas de wake e fixture privada. A
`Zephyr kworker` usa Wait Queue, `workqueue_probe_worker()` valida o percurso de wake real e System
permanece como fallback. O contrato canonico fica em
`docs/04-kernel/kernel.md`.

Desde a R3, `src/include/core/wait.h` define canais estaticos com sequencia de
condicao, disponibilidade, motivos de desbloqueio e deadlines absolutos. A
SYNC2 torna `wait_queue_head_t` o tipo canonico, preserva `wait_channel_t` como
alias e acrescenta entradas intrusivas FIFO, registro limitado por IDs
geracionais, `wait_event`, wake-one/all, snapshots e invariantes. O contrato
de `wait_event` avalia a condicao e captura sua geracao na mesma regiao com
interrupcoes desabilitadas, antes do encadeamento atomico. O contrato canonico
fica em `docs/07-processos/processos.md`; as APIs de processo/thread
permanecem internas ao kernel e nao alteram a ABI ring 3.

Desde a SYNC4, `src/include/process/signal.h` publica envio restrito a ring3,
ações, máscaras, preparação/retorno de handler, snapshots, métricas,
invariantes e fixture privada. `process_t` acrescenta pai, bitmaps, ações,
contexto salvo e contadores. `wait.h` acrescenta, ao fim dos contratos, o
motivo e a métrica `WAIT_REASON_SIGNAL`. A App API 0.4 acrescenta as syscalls
10-13 e `app_signal_action_t`; o App Loader publica o sinal de término sem
alterar os campos anteriores.

Desde a VFS1, `src/include/fs/vfs.h` publica `file_operations_t`, `vnode_t`,
`file_t`, a tabela de 32 descritores por processo, o pool global de 32 arquivos
regulares, stdio, snapshots, metricas, invariantes e autoteste. `process_t`
acrescenta `fd_table` ao fim do contrato e seu ciclo de vida passa a instalar e
liberar descritores. `app_files.h` permanece como fachada compativel;
`app_api.h` publica a versao 0.5, fds padrao, origens de seek e
`app_api_file_lseek()`. `syscall.h` preserva os numeros 4-7 e acrescenta a
syscall 14.

Desde a VFS2, `vfs.h` acrescenta snapshots de montagem e lookup, diretorio
virtual, `cwd` na tabela por processo e as APIs de refresh, resolucao,
`chdir/getcwd`, montagem e desmontagem. A tabela acompanha o limite de quatro
montagens de `storage.h`; descritores conservam volume, caminho relativo e
geracao por contexto privado. `process.h` herda o `cwd` na criacao e o valida
no ciclo de vida. `app_api.h` publica a versao 0.6 e `app_api_chdir()` /
`app_api_getcwd()`; `syscall.h` acrescenta os numeros 15 e 16 sem renumerar os
anteriores. Pacotes 0.3, 0.4, 0.5 e 0.6 permanecem aceitos.

Desde a VFS3, `devfs.h` publica o registro fixo, snapshots, métricas,
invariantes e autoteste de `/dev/null`, `/dev/zero`, `/dev/tty`,
`/dev/speaker` e `/dev/hda`. `vfs.h` acrescenta nós de caractere e bloco,
quinta montagem virtual independente das quatro vagas do Storage,
`vfs_dir_entry_t`, `vfs_list_dir()` e `vfs_ioctl()`. `app_api.h` publica a
versão 0.7, `app_speaker_tone_t`, requests do speaker e
`app_api_file_ioctl()`; `syscall.h` acrescenta o número 17 sem renumerar os
anteriores. Pacotes 0.3 a 0.7 permanecem aceitos.

Desde a VFS4, `vfs.h` acrescenta `VFS_NODE_PIPE`, `vfs_pipe()` e
`vfs_write_redirect()`, com buffer circular de 4096 bytes, pool de oito pipes,
EOF, backpressure por Wait Queues e metricas de pipes. `app_files.h` e
`app_api.h` acrescentam `app_files_pipe()` e `app_api_pipe()`; a App API passa
a versao 0.8 e `syscall.h` acrescenta o numero 18 sem renumerar os anteriores.
Pacotes 0.3 a 0.8 permanecem aceitos. `shell_pipeline.h` publica o bridge
interno de I/O, `grep` e o autoteste `pipetest`, sem alterar as assinaturas de
`shell.h`.

Desde a MM1, `slab.h` publica o ciclo de vida de caches de objetos fixos,
consultas de estatisticas, verificacao de posse, validacao global e autoteste.
`kmem_cache_destroy()` retorna erro quando o cache e invalido ou ainda possui
objetos ativos. Os metadados dos caches sao estaticos; as paginas dos slabs
sao obtidas e devolvidas ao PMM. A migracao das tabelas internas para ponteiros
nao altera a ABI ring 3 nem as assinaturas publicas de processo, thread ou VFS.
A Ethernet usa `sk_buff_t` neste NET1 sem expor um novo `net_device` publico.
`shell_runtime.h` acrescenta
`slab_integrity` ao resultado interno de `memcheck`.
`thread.h` acrescenta `thread_is_ready()` para publicar o estado de
inicializacao do scheduler cooperativo; a assinatura legada de `thread_init()`
permanece intacta.

Desde a MM2, `memory/vma.h` publica `vm_area_t`, as flags `VM_READ`,
`VM_WRITE`, `VM_EXEC`, `VM_SHARED` e `VM_ANONYMOUS`, snapshots de VMA e as
operacoes de registro, `mmap`, `munmap` e limpeza por processo. `process_t`
acrescenta `vma_list` e `vma_count` ao final do layout. A imagem ring 3
registra automaticamente codigo, dados, lancamento e stack; somente VMAs
anonimas privadas podem ser criadas dinamicamente. A App API passa a versao
0.9 e acrescenta `app_api_mmap()` e `app_api_munmap()`; `syscall.h` preserva
os numeros anteriores e acrescenta 19 e 20. Pacotes 0.3 a 0.9 permanecem
aceitos.

Desde a MM3, os campos append-only `user_code_image`, `user_data_image`,
`user_data_size` e `user_launch` de `process_t` mantêm o backing kernel-owned da
imagem ring 3 e a cópia persistente de lançamento até a destruição do processo.
Esses campos não alteram a ABI da App API nem das syscalls. As páginas de
codigo, dados, lancamento, stack e `mmap` anonimo são materializadas sob
demanda; `process_vma_ensure_page()` atende cópias de buffers válidos e
`process_vma_handle_page_fault()` trata faults originadas em ring 3. O helper
`process_vma_get_page_fault_stats()` publica `page_fault_stats_t`, com os
contadores cumulativos `handled` e `invalid`. `munmap` libera somente páginas
residentes e mantém a limpeza do metadado da VMA. READ, WRITE e EXEC são
validados contra os flags da VMA; o bit WRITE continua sendo o único bit de
proteção de página disponível no paging atual.

Desde a MM4, `src/include/core/memory.h` publica `memory_zone_t` com as zonas
exclusivas `KERNEL`, `HEAP`, `SLAB`, `PROCESS`, `BUFFER` e `FREE`, além de
`memory_detailed_stats_t` e `memory_get_detailed_stats()`. As novas funções
`pmm_alloc_page_in_zone()` e `pmm_alloc_pages_in_zone()` classificam páginas
físicas; `pmm_alloc_page()` e `pmm_alloc_pages()` permanecem wrappers legados
para `KERNEL`. A consulta retorna `OK` e um snapshot consistente, `ERR_NULL`
para destino nulo ou `ERR_STATE` quando o PMM não está pronto ou o metadata
está inconsistente. A struct publica total por zona, runs livres, maior run,
páginas isoladas, percentual de fragmentação e flags `initialized`/`valid`.
Páginas reservadas não podem ser liberadas, e uma rejeição não altera os
contadores do PMM.

`shell_memcheck_result_t` acrescenta `memory_metrics` ao final do layout; o
campo valida a soma das zonas e a estabilidade do snapshot antes/depois do
teste de heap. O acréscimo é append-only e não altera `shell.h`, a ABI ring 3
ou as assinaturas de syscalls.

O cancelamento F12 de um ZAPP bloqueado em stdin usa os campos append-only
`cancel_exit_code` e `cancel_pending` de `process_t`. `process_cancel_user()`
acorda a wait queue e `process_apply_pending_cancel()` conclui o encerramento
no retorno da syscall, depois que a VFS libera a operacao ativa.

Na integracao IPC da SYNC2, `process_t` conserva a ultima geracao consumida do
seu canal. `ipc_wait()` considera pronta uma mensagem ou uma geracao nova, de
modo que notificacoes internas e workers sem payload nao sejam perdidos pela
revalidacao da condicao.

Desde a SYNC2, `src/include/core/net_socket.h` acrescenta mascaras de eventos,
`net_socket_wait()`, waiters por socket e metricas/autoteste de espera sem
alterar `net_socket_receive()`. O contrato canonico permanece em
`docs/04-kernel/kernel.md`.

Desde a NET2, `src/include/core/socket.h` publica a camada generica de
sockets sobre descritores VFS. `AF_UNIX/SOCK_STREAM` oferece namespace global
de caminhos, `bind`, `listen`, `accept`, `connect`, filas locais limitadas e
EOF no fechamento do peer. `AF_INET/SOCK_STREAM` adapta somente o cliente TCP
ativo legado; `listen` e `accept` passivos IPv4 retornam `ERR_UNAVAILABLE`.
Sockets sao bloqueantes por padrao e aceitam `SOCKET_FLAG_NONBLOCK`, cujo
progresso ausente retorna `ERR_AGAIN`. Nao ha syscall nova, alteracao da ABI
ring 3, heranca automatica de FDs, `poll/select`, UDP generico, `socketpair`,
datagramas UNIX ou ownership DMA transferido.

Cada socket ocupa um descritor VFS real com `VFS_NODE_SOCKET` e adaptadores de
leitura, escrita, fechamento, `lseek` e `fsync`. As filas UNIX usam
`sk_buff_t` com storage interno de 2048 bytes; mensagens maiores sao
fragmentadas em buffers independentes apenas para transportar a stream e os
payloads sao copiados. Clones, fragmentos compartilhados e zero-copy real
permanecem fora da NET2. `sockstat` e `socket_self_test()` expõem o estado e
validam limpeza, sem alterar o inventario de NICs ou o trafego legado.

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

Desde o BLK0, `block.h` tambem publica `block_operation_t`,
`block_request_state_t`, `bio_request_t`, `block_submit_sync()` e
`block_self_test()`. No BLK1, `block_request_t` tornou-se o contrato publico
da requisicao fisica entregue ao driver, e `block_submit_callback_t` foi
acrescentado ao final de `block_ops_t`; os callbacks legados permanecem como
fallback. O BIO usa um ID de dispositivo emprestado, LBA, quantidade de
setores, buffer emprestado, tamanho declarado, operacao, flags, callback e
contexto; a camada escreve estado, status e setores concluidos sem assumir a
liberacao do buffer. A callback de conclusao ocorre uma vez depois do estado
terminal, inclusive quando BIOs sao fundidos.

`block_submit()` e a API assincrona e retorna com o BIO em `QUEUED`. A fila
FIFO tem capacidade fixa de 32 entradas, nao aloca memoria dinamica e copia o
ID do dispositivo para o slot; o chamador deve manter ID e buffer validos ate
a conclusao. `block_submit_sync()` usa a mesma fila e drena o dispatcher ate
o estado terminal. `block_cancel()` aceita somente BIOs enfileirados, marca
`CANCELLED`/`ERR_CANCELLED` e retorna `ERR_STATE` para um BIO em voo.
`block_dispatch()` preserva a ordem e funde somente operacoes do mesmo
dispositivo, flags, LBA adjacente e buffers contiguos, sem bounce buffer,
reordenacao ou fusao de FLUSH. `block_get_stats()` publica profundidade, pico,
requisicoes, fusoes, setores e taxas medias; `ERR_TIMEOUT` e demais erros do
driver sao propagados sem retry generico.

Os campos `max_transfer_sectors` e `capabilities`, os callbacks opcionais de
FLUSH/escrita com flags e o callback de submissao foram acrescentados ao final
das structs existentes para preservar o uso legado. O limite atual e 255
setores por requisicao, o setor logico permanece em 512 bytes e FUA continua
indisponivel. ATA publica `BLOCK_DEVICE_CAP_FLUSH` somente quando o IDENTIFY
confirma FLUSH CACHE; USB MSC permanece sem FLUSH/FUA. `block_read()` e
`block_write()` mantem as assinaturas: o primeiro usa o cache e o segundo faz
write-back. `block_submit_sync()` continua fisico direto; o caminho interno
`block_submit_physical_sync()` permite ao writeback escrever a entrada em
`WRITEBACK` sem invalidar a propria chave. Unregister, refresh e substituicao
sincronizam o dispositivo antes de invalidar o cache e recusam a operacao se
restar dado sujo ou houver erro; requisicoes pendentes tambem bloqueiam a
transicao. Se a workqueue nao estiver disponivel, o caminho sincrono e
`block_write()` continuam funcionais e `block_submit()` retorna
`ERR_UNAVAILABLE`.

Desde o BLK2, `src/include/fs/block_cache.h` publica o cache de leitura de
blocos. A capacidade e fixa em 64 entradas de `BLOCK_SECTOR_SIZE` bytes, sem
`kmalloc`, bounce buffer ou ownership do buffer do chamador. A chave e
`(device_id, lba, block_size)` e os estados sao `FREE`, `READING`, `VALID`,
`DIRTY`, `WRITEBACK` e `ERROR`. `block_cache_read()` recebe um dispositivo,
faixa, destino e backend BLK1; hits copiam da entrada valida e misses carregam
o backend, publicando `VALID` somente depois da transferencia concluida.

`block_cache_get_stats()` retorna capacidade, memoria reservada, ocupacao,
estados, pins, hits, misses, leituras evitadas e fisicas, evictions,
invalidacoes, bypasses, erros, bytes sujos, tentativas/sucessos/falhas de
writeback, escritas fisicas, syncs, flushes, estado de durabilidade, taxa de
acerto e ultimos erros. `block_cache_write()` aceita faixa de bytes, faz
preload fisico em miss parcial e nao guarda o buffer externo. O writeback
periodico e limitado a 8 blocos por ciclo; erros mantem a entrada `DIRTY`.
`block_cache_sync_device()` e `block_cache_sync_all()` drenam dirty blocks e
submetem FLUSH quando suportado. `block_cache_get_durability_status()` publica
`READY`, `DEGRADED` ou `ERROR`. As APIs de limpeza e invalidacao falham com
`ERR_STATE` sem remover parcialmente a operacao quando existe referencia, pin,
leitura, entrada suja, writeback ou waiter correspondente. `cachestat` exige
zero argumentos e `cache clear` exige exatamente `clear`; entradas invalidas
somente exibem o uso.

No BLK4, os controladores de failpoint permanecem privados a `block.c` e
`block_cache.c`; nenhum simbolo foi acrescentado aos headers publicos para
arma-los. Cada injecao e one-shot, possui ocorrencia e codigo forcados e e
limpa em sucesso, erro ou cancelamento. `block_self_test()` e
`block_cache_self_test()` exercitam submissao, execucao, conclusao, FLUSH,
eviction e writeback abortado, preservando callback unico, fila vazia,
inventario, hash/LRU, pins e dados `DIRTY` legiveis ate um novo sync.

`src/include/fs/vfs.h` acrescenta o callback final `file_operations_t.sync`,
`vfs_fsync(fd)` e `vfs_sync()`. Arquivos regulares sincronizam seu volume;
`/dev/hda` sincroniza o dispositivo de bloco; pipes, terminal, speaker e
objetos sem persistencia retornam `ERR_UNAVAILABLE`. `app_files.h` e
`app_api.h` acrescentam as fachadas correspondentes. `syscall.h` anexa
`APP_SYSCALL_FSYNC` (21) e `APP_SYSCALL_SYNC` (22), sem renumerar a ABI
existente; ambas rejeitam argumentos excedentes com `ERR_INVALID`.

Desde a EP4.4, `src/include/core/input.h` define eventos HID Usage de teclado,
eventos relativos de ponteiro, filas estaticas separadas, metricas e despacho
para os consumidores PS/2 legados. O adaptador de teclado preserva as posicoes
ABNT2 `;/:` (Usage `0x38`) e `/ ?` (Usage `0x87`) antes de entregar os
scancodes ao Shell. `src/include/core/irq_deferred.h` define a fila limitada
de conclusoes fora de contexto de IRQ, inicializacao por proprietario/IRQ,
coalescencia, reexecucao, cancelamento seguro, snapshots globais/por IRQ e
autoteste privado. Na SYNC3, acrescenta um notificador append-only que agenda
a drenagem pela `Zephyr kworker`. `src/include/drivers/idt.h` publica
ocorrencias e quantidade
de handlers das 16 linhas PIC por snapshot somente-leitura.
`src/include/drivers/uhci.h` acrescenta o contrato de
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

Desde a EP9.4B, `build\zephyros.img` usa uma imagem híbrida de 256 MiB: o FAT12
bruto continua no início para boot e recuperação e o FAT32 `ZEPHYROS` começa
no LBA 8192. O kernel legado fica no LBA 64 e o recovery loader no LBA 6144,
com janelas sem sobreposicao. `fs.h` roteia caminhos sem prefixo e `system:/` para o volume de
sistema quando montado, preserva `legacy:/` para o FAT12 e não faz fallback
silencioso. A correcao de layout atualiza somente constantes e validacoes do
`stage2` e do empacotador; nao altera App API, syscalls, slots, staging, reboot
ou journaling. `boot.asm` permanece inalterado.

EP9.2A adiciona somente contratos privados de boot: o recovery loader fixo
seleciona `ZSI*.STA`, grava tentativa/rollback in-place na copia redundante ja
prealocada e autentica o ZSYS em streaming antes de executar o kernel. Nao ha
API publica nova de Shell; `update_system_slots_boot_confirm()` e `ZSBH`
permanecem o contrato entre loader e kernel. `boot.asm` continua inalterado.

Desde a EP9.3, `update_remote_system.h` define cache remoto ZSYS A/B, status,
motivos, fetch confirmado, limpeza e resolução do pacote publicado.
`update_system.h` acrescenta transferência autenticada por callbacks;
`update_system_slots.h` acrescenta cancelamento confirmado do pendente; e
`storage.h` generaliza o escritor FAT32 transacional preservando os wrappers
de slot. Os contratos canônicos permanecem no contrato ZSYS e na documentação
do filesystem.

Na EP9.4B, `update_system.h` nomeia as ABI de boot 1 e 2. ABI 1 preserva a
execucao direta do kernel; ABI 2 executa boot e stage2 protegidos somente
depois da autenticacao e da releitura dos tres componentes. O handoff `ZSBC`
e privado do loader e nao amplia a API publica, o estado dos slots ou `ZSBH`.

Na EP9.4C, `update_system_slots.h` acrescenta o preflight somente leitura que
exige controles v2 redundantes e equivalentes imediatamente antes do reboot.
`power.h` acrescenta `power_reboot()`, usado por Shell, Settings, Task Manager
e System Updater. `updater.h` preserva sua API; a aba Sistema e seus estados
continuam privados ao aplicativo Classic.

No BLK4, `power.h` acrescenta ao final `power_shutdown_prepare()`. A funcao
executa `storage_sync_all()` e retorna `OK` quando o writeback termina,
inclusive no estado degradado por ausencia de FLUSH. Erros de escrita ou
FLUSH sao propagados e impedem que os caminhos normais chamem a primitiva
terminal `power_shutdown()`. `power_shutdown()` permanece `void`, `noreturn`
e com o mesmo contrato; `power_reboot()` nao foi alterado.

## NET3 - Multiplexacao VFS com poll/select

`src/include/core/poll.h` acrescenta `pollfd_t` com layout fixo de tres campos
de 32 bits (`fd`, `events`, `revents`) e `fd_set_t` de 32 bits. O limite de
`poll()` e `select()` e `POLL_MAX_FDS` (32); os timeouts sao expressos em ticks,
com zero imediato e `WAIT_TIMEOUT_INFINITE` infinito. As mascaras publicas sao
`POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP` e `POLLNVAL`, e as macros `FD_ZERO`,
`FD_SET`, `FD_CLR` e `FD_ISSET` ignoram descritores fora desse limite.

`file_operations_t.poll` foi anexado depois de `sync`, preservando o layout
anterior dos campos existentes. A VFS consulta stdin/TTY, arquivos regulares,
pipes e sockets genericos; operacoes sem readiness publicado retornam
`POLLERR`. Pipes publicam dados/EOF, espaco/fechamento do leitor e usam o
canal global `VFS-poll`. Sockets AF_UNIX e AF_INET traduzem filas, estado,
erro, fechamento e capacidade TCP para as mesmas mascaras.

`vfs_poll()` limpa e reconstroi `revents` a cada varredura, aceita zero
descritores como espera temporizada, retorna `POLLNVAL` por entrada invalida,
`ERR_OVERFLOW` acima de 32 entradas, `OK` com zero eventos no timeout e
`ERR_CANCELLED` para sinal/cancelamento. O bloqueio registra somente um
waiter no canal global e nao conserva `file_t` nem locks durante a espera.
`vfs_select()` usa o mesmo caminho e traduz `POLLNVAL` para `ERR_INVALID`.

`app_api.h` e `app_files.h` acrescentam as fachadas `app_api_poll()` e
`app_api_select()`. A App API permanece 0.9. `APP_SYSCALL_POLL` e
`APP_SYSCALL_SELECT` sao anexadas como syscalls 23 e 24; `select` recebe um
`select_request_t` por ponteiro para preservar o limite de cinco argumentos
da ABI. A fronteira ring 3 valida ranges, copia arrays e request, verifica
overflow e somente publica resultados depois de uma operacao concluida.
Nenhuma syscall de criacao de socket foi adicionada.

## NET4 - Rotas IPv4 e diagnostico agregado

`src/include/core/route.h` publica uma tabela estatica de 16 entradas com
`network`, `subnet_mask`, `prefix_length`, `gateway` e `interface_id`. As
mascaras aceitas vao de `/0` a `/32`; redes devem ser canonicas, gateways
devem ser unicast quando presentes e a interface nao pode ser vazia. A tabela
vive somente em RAM, suporta rota direta com gateway `0.0.0.0`, rota default,
duplicidade rejeitada, exclusao, reset da configuracao IPv4 atual e lookup por
longest-prefix match. `route_status_t` expoe contagem, lookups, matches,
misses, alteracoes e ultimo erro; `route_validate_state()` e
`route_self_test()` verificam as invariantes sem deixar entradas residuais.

`ipv4_send()` consulta a tabela antes de resolver ARP. A rota selecionada deve
apontar para a interface L3 atual; encaminhamento entre NICs permanece fora da
NET4 e nao e implementado pelo comando `route`.

`TCP_STATE_LISTEN` foi anexado ao final de `tcp_state_t`, preservando os valores
anteriores. `tcp_state_name()` o apresenta como `LISTEN`, mas o TCP passivo
continua indisponivel nesta etapa. O comando `netstat` agrega conexoes da
visao TCP, sockets `AF_UNIX` da tabela generica e contadores RX/TX, erros e
descartes de cada interface, sem duplicar conexoes `AF_INET`.

## PROC0 - ABI textual de introspeccao

O PROC0 define o contrato comum dos futuros pseudo-filesystems `/proc` e
`/sys`, sem criar header, syscall, App API ou layout binario. O acesso sera
feito pelas operacoes VFS existentes e os nos iniciais serao publicos e
somente leitura; permissoes por usuario, UID/GID e controles de energia ficam
fora desta etapa.

O conteudo usa ASCII e linhas `<chave> <valor>\n`, com chaves estaveis sem
espacos, sem `NUL`, `CR`, ANSI ou locale; o terminador e `LF`, nunca `CRLF`.
Contadores sao decimais, hardware e mascaras sao hexadecimais, IPv4 usa
notacao pontuada e estados usam tokens estaveis. Chaves repetidas representam
registros multiplos em ordem documentada pelo no.

Cada abertura captura um snapshot imutavel de no maximo 16 KiB, pertencente ao
`file_t` ate `close()`. O `file_t.offset` e o cursor; `read()` entrega partes
do snapshot e retorna `OK` com zero bytes no EOF; `lseek()` opera dentro dos
limites. Excesso retorna `ERR_OVERFLOW` sem truncamento. A remocao de um
processo ou dispositivo nao invalida o snapshot aberto; novas aberturas usam
PID/event-generation ou identificador/generation e retornam `ERR_NOT_FOUND`
quando o no nao existe.

A enumeracao e deterministica: nos fixos em ordem contratual, PIDs crescentes
e identificadores estaveis em `/sys`. Escrita ou operacao nao suportada retorna
`ERR_UNAVAILABLE`; caminho ou cursor invalido retorna `ERR_INVALID` e falta de
memoria retorna `ERR_MEM`. Os formatos especificos de `/proc` e `/sys` serao
acrescentados em PROC2 e PROC3 sem quebrar essa gramatica.

## PROC1 - Procfs no namespace VFS

`src/include/fs/procfs.h` publica o contrato interno do primeiro pseudo-
filesystem: `proc_entry_t`, callbacks de leitura com retorno de erro separado
de `out_len`, callback de escrita reservado, contexto de snapshot e resultado
do autoteste. A tabela inicial possui somente `uptime`, gerado como
`uptime_ticks <decimal>\n` e `frequency_hz <decimal>\n`.

`VFS_MOUNT_PROCFS` foi anexado ao enum de montagens, sem renumerar os valores
anteriores. A montagem fixa `procfs` em `/proc` e pinned, somente leitura e
nao desmontavel; os arquivos continuam usando `VFS_NODE_REGULAR`. O buffer
de ate 16 KiB pertence ao contexto privado do arquivo, e imutavel ate
`close()` e nao cria layout binario para aplicativos.

O campo `procfs` foi acrescentado ao final de `vfs_test_result_t`. Nao houve
alteracao de App API, syscalls, `file_operations_t` ou bootloader nesta etapa;
as operacoes continuam acessiveis somente pelas APIs VFS ja existentes.

## PROC2 - Nos globais e processos em /proc

O `procfs` publica os nos globais `uptime`, `meminfo`, `cpuinfo`, `version` e
`cmdline` nessa ordem, seguidos por diretorios de todos os processos
registrados em ordem crescente de PID. Cada diretorio lista `status`,
`cmdline` e `maps`. Os formatos seguem a ABI textual ASCII do PROC0, com
contadores decimais, enderecos de VMA hexadecimais e estados textuais.

`process_t.event_generation` foi anexado ao layout existente e recebe uma
identidade monotonicamente crescente a cada criacao. `process_snapshot_t` e
as APIs `process_snapshot_copy()`, `process_snapshot_list()` e
`process_snapshot_copy_vmas()` sao contratos internos sem ponteiros retidos
pelo procfs. A captura de VMAs valida PID e geracao, repete uma vez e retorna
`ERR_AGAIN` se o processo continuar mudando. `memory_bytes` soma stack de
kernel, paginas residentes de usuario e imagens de codigo/dados.

O contexto de cada arquivo continua dono de um snapshot de no maximo 16 KiB;
`read`, EOF e `lseek` operam sobre esse buffer imutavel. O acesso e publico e
somente leitura; nao foram adicionados layouts binarios para aplicativos,
syscalls, App API, persistencia ou alteracoes no bootloader.

## PROC3 - Sysfs integrado ao VFS

`src/include/fs/sysfs.h` publica o contrato interno do provider separado
`sysfs`: lookup, abertura, listagem, validacao e autoteste, alem do contexto de
snapshot e do resultado append-only do VFS. O contexto contem apenas o buffer,
tipo, identificador, geracao e referencia da montagem; nao retém ponteiros para
`pci_device_t`, `network_interface_info_t` ou `block_device_t`.

`VFS_MOUNT_SYSFS` foi anexado ao enum de montagens sem renumerar os valores
anteriores. A montagem `sysfs` em `/sys` e pinned, publica, somente leitura,
usa `STORAGE_FS_NONE` e nao e desmontavel. Os nos sao `VFS_NODE_DIRECTORY` ou
`VFS_NODE_REGULAR`; nao foi criada uma nova classe de vnode, syscall ou App API.
O campo `sysfs` foi acrescentado ao final de `vfs_test_result_t`.

A hierarquia fixa inclui `bus/pci/devices`, `class/net`, `class/block` e
`power/state`. A raiz lista `bus`, `class`, `power`; PCI e ordenado por
`bus/device/function`; rede usa os IDs de `network_manager_format_text()`;
blocos usam `block_device_t.id`; classes sem inventario ficam vazias. Cada
atributo e um arquivo ASCII de uma linha no formato `<atributo> <valor>\n`.
PCI, rede, blocos e energia publicam os atributos definidos no contrato PROC3,
com numeros decimais, valores de hardware em hexadecimal minúsculo com `0x`,
MAC hexadecimal e tokens de estado estaveis. `/sys/power/state` usa linhas
repetidas `state S0` ate `state S5`, seguidas por `cpu_idle`,
`hardware_poweroff` e `reboot`.

Cada abertura copia os inventarios e captura um snapshot imutavel de no maximo
16 KiB. `file_t.offset` e o cursor; leituras parciais, EOF e `lseek(SET/CUR/END)`
seguem o contrato PROC0. Excesso retorna `ERR_OVERFLOW`, caminho invalido
`ERR_INVALID`, dispositivo ausente `ERR_NOT_FOUND`, falta de memoria `ERR_MEM`
e escrita, `ioctl` ou `sync` `ERR_UNAVAILABLE`. A remocao posterior do
hardware nao invalida um snapshot ja aberto. A geracao interna do inventario
fica preparada para atualizacoes futuras, sem hotplug ou rescan nesta etapa.

## PROC4 - Consumidores de introspeccao

`src/include/apps/shell_introspection.h` e um contrato interno do Shell para
ler arquivos procfs/sysfs somente pela VFS. O adaptador executa leituras
parciais ate EOF, fecha o descritor em todos os caminhos, rejeita NUL, CR,
ANSI e bytes fora de ASCII, e interpreta linhas `<chave> <valor>\n` sem
reter buffers ou descritores.

O Task Manager Classic usa copias de `/proc/<pid>/status` e `/proc/meminfo`.
Sua linha visual e identificada por `pid + generation`, e a revalidacao ocorre
antes de uma acao sobre o processo. O fallback Simple e a aba de threads nao
mudam. `devices` e `device-info` usam snapshots de `/sys` para PCI, rede e
bloco e retornam ao inventario legado quando nao existe um no correspondente.

`proccheck` publica somente um resultado agregado. PROC4 nao altera
`taskmanager.h`, App API, syscalls, layouts binarios, persistencia,
`boot.asm` ou `stage2.asm`, e nao habilita escrita em `/proc/sys`.

## PROC5 - Controles de runtime em /proc/sys

PROC5 está implementado no provider `procfs`. Nenhuma syscall, App API ou
layout binário foi criado, e a escrita continua exclusiva de controles nomeados
em tabela estática; não existe escrita genérica em estruturas do kernel nem
qualquer escrita em `/sys`.

O conjunto inicial implementado é:

```text
/proc/sys/kernel/console_log_level
/proc/sys/kernel/buffer_log_level
```

Os valores válidos são os tokens estáveis `error`, `warn`, `info` e `debug`,
espelhando as operações já existentes do subsistema de log. A leitura obedece à
ABI textual do PROC0, com uma linha ASCII por arquivo, snapshot imutável de até
16 KiB, `file_t.offset`, EOF e `lseek`. A abertura em modo de leitura é pública;
a abertura e a escrita exigem o gate de processo nativo/ring0. Processos ring3
recebem `ERR_UNAVAILABLE`.

Uma escrita aceita exatamente um token válido e `LF` opcional. A entrada é
validada completamente antes de um commit atômico; falhas preservam o valor
anterior, e o sucesso retorna toda a carga em `bytes_written`. Os valores são
mantidos somente em RAM. Uma abertura existente conserva o snapshot antigo,
enquanto uma nova abertura observa o valor atualizado. O console não pode ser
mais detalhado que o buffer; a regra é aplicada pelo backend de log.

O contrato de erros de PROC5 e: caminho ou valor invalido em `ERR_INVALID`, nó
ausente em `ERR_NOT_FOUND`, entrada excedente em `ERR_OVERFLOW`, falta de
memoria em `ERR_MEM`, mudanca concorrente em `ERR_AGAIN` e escrita nao
autorizada, `ioctl`, `sync` ou acesso a diretorio em `ERR_UNAVAILABLE`.
Scheduler, forwarding IPv4, energia, memória e parâmetros de processos ficam
fora do primeiro conjunto. `procfs_self_test()`, `vfs_self_test()` e
`proccheck` cobrem listagem, leitura, escrita válida, rejeições, rollback,
snapshot, reset e limpeza. A confirmação funcional no QEMU ainda é pendente;
ela será registrada antes de marcar PROC5 como concluído no roadmap.

## PWR0 - Contrato de energia e ACPI

O PWR0 é uma etapa somente documental. Não foram alterados `src/`, headers,
Makefile, App API, syscalls, layouts binários, `boot.asm`, `stage2.asm` ou as
transições reais de energia. O contrato prepara PWR1-PWR4 e preserva as
assinaturas existentes de `power.h` e `acpi.h`.

O serviço de energia possui os estados `UNKNOWN`, `DISCOVERING`, `READY`,
`DEGRADED` e `UNAVAILABLE`, distintos dos estados ACPI S0-S5. Cada capacidade
publica seu próprio estado, pré-condição, fallback e erro. `READY` indica que
o coordenador está utilizável; não é uma promessa de desligamento físico ou
de reboot por hardware.

`shutdown` e `reboot` compartilham uma transação com a ordem
`admission -> notification -> sync/flush -> quiescence -> hardware commit ->
terminal`. O alvo é fixado na admissão. Os orçamentos são PIT de 50 Hz, sem
empréstimo entre fases: 250 ticks (5 s) para notificação, 1500 (30 s) para
sync/flush, 250 (5 s) para quiescência e 100 (2 s) para commit, com total de
2100 ticks (42 s).

O coordenador possui transação, prazos, cancelamento e resultado; cada
participante possui seus recursos e deve ser idempotente. Cancelamento e
rollback são permitidos somente antes do commit. A primeira escrita ou
comando de hardware inicia a região irreversível. Os erros canônicos são
`ERR_STATE`, `ERR_UNAVAILABLE`, `ERR_TIMEOUT`, `ERR_CANCELLED`, `ERR_AGAIN` e
`ERR_INVALID`; a camada com contexto registra fase, falha e fallback.

Descoberta/validação ACPI, interpretação AML e uso de métodos permanecem
separados. Somente capacidades detectadas e validadas podem ser usadas, e
portas privadas de QEMU, Bochs ou VirtualBox não são fallback genérico.
`power status` e `acpi status` continuam sendo a observação pública, sem
novo comando, layout binário ou ABI de aplicativo nesta etapa.

## PWR1 - Idle e metricas de residencia

O PWR1 usa o processo PID 0 existente como o unico Idle do kernel unicore. O
PID 0 fica fora do round-robin e e escolhido somente quando nenhum processo
normal esta `READY`. O handoff inicial usa um contexto de bootstrap separado,
preservando a stack propria do Idle; `boot.asm`, `stage2.asm`, syscalls, App
API e layouts binarios permanecem inalterados.

O Idle executa `sti; hlt` e depois `process_yield()`. System e Desktop usam
bloqueio temporizado em vez de polling ativo; o caminho degradado do
`kernel_main` tambem usa `sti; hlt` quando System nao pode ser criado.

`scheduler_stats_t` mantem o layout existente por extensao append-only:
`idle_ticks` e `active_ticks` sao acrescentados depois de
`user_quantum_ticks`. O scheduler incrementa exatamente um contador por tick
do PIT e `scheduler_get_stats()` copia a estrutura com interrupcoes
protegidas. A invariante `idle_ticks == processes[0].total_ticks` e publicada
por `idle_accounting_valid` em `scheduler_validation_t` e validada por
`schedcheck` e `regcheck full`.

`cpu usage` e `cpu usage reset` sao comandos do Shell, nao uma nova fronteira
de ABI. O reset captura uma linha-base privada e nao zera contadores do
kernel. As porcentagens representam residencia do scheduler baseada no PIT;
nao representam CPU fisica, consumo eletrico ou RDTSC/PMU. Nenhuma assinatura
da App API, syscall, `taskmanager.h` ou ABI binaria foi criada.
