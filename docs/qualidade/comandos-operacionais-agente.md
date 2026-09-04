# Memoria operacional do agente — comandos e validacoes gerais

> Documento de contexto operacional para agentes. Nao e contrato publico e nao
> deve armazenar senhas, tokens, chaves privadas, certificados privados,
> caminhos pessoais ou outros dados sensiveis.

## Regra de consulta

Consultar este arquivo antes de orientar comandos, builds ou validacoes do
projeto. O catalogo de comandos visiveis ao usuario fica em `comandos.md`; as
regras de trabalho ficam em `AGENTS.md`; os procedimentos especificos de cada
tarefa ficam em seus documentos canonicos e nos roadmaps.

Nao inventar comandos, argumentos, tags, IDs, endpoints ou opcoes do QEMU. Se
um valor dinamico nao estiver neste arquivo ou na saida mais recente fornecida
pelo usuario, usar a fonte real do projeto ou pedir a saida correspondente.

## Gates de build e QEMU

Depois de alterar codigo, header ou Makefile, o usuario executa os gates
operacionais:

```text
make q3check
make clean && make
make run
```

O agente nao executa `make`, build, testes ou QEMU neste projeto. Depois que o
usuario confirmar esses gates para a mesma versao do codigo, eles nao devem ser
reapresentados como testes funcionais pendentes da fase.

Nao adicionar opcoes genericas ao comando de execucao. Em especial, nao
sugerir `-cpu max` como parte do fluxo atual: essa opcao nao pertence ao
comando documentado enquanto nao estiver explicitamente configurada no
`Makefile`.

## Comandos host verificados

As baterias host-only atuais usam o compilador configurado em `HOST_CC` e
geram um relatorio de cobertura por caso. Alem dos alvos TST2/TST3, os testes
diretos de rede e contratos podem ser executados assim:

```text
make test-network-host
make test-route-host
make test-ipv4-host
make test-crypto-host
make test-scheduling-host
make test-package-host
make test-state-host
make test-device-manager-host
make test-app-api-host
make test-app-files-host
make test-app-builtin-host
make test-app-catalog-host
make test-input-host
make test-power-host
make test-network-manager-host
make test-vfs-path-host
make test-file-index-host
make test-fs-host
make test-storage-host
make test-block-host
make test-fat12-host
make test-fat32-host
make test-vfs-host
make test-slab-host
make test-timer-host
make test-udp-host
make test-arp-host
make test-icmp-host
make test-dns-host
make test-dhcp-host
make test-bearssl-compat-host
make test-ethernet-host
make test-tcp-host
make test-tls-host
make test-tls-client-host
make test-mediaplayer-host
make test-shell-job-host
make test-shell-pipeline-host
make test-socket-runtime-host
make test-sysfs-host
make test-process-host
make test-thread-host
```

O caso de scheduling cobre `wait`, `workqueue` e `irq_deferred` em processo
host com `ZEPHYROS_HOST_TEST=1`; o caminho freestanding continua sendo usado
no build do kernel. Cada alvo preserva `manifest.json`, `result.json`,
`coverage.json`, `coverage-symbols.json`, `stdout.log` e `stderr.log` em
`build/test-results/<suite>/`.

O caso `test-tls-client-host` compila o adaptador `tls_client.c` real com um
engine BearSSL falso, socket, relogio e RNG deterministas. Ele exercita
handshake, configuracao de data, envio, recepcao, EOF, falhas de I/O,
indisponibilidade de entropia, limites de SNI, estado e limpeza sem rede
externa; seu relatorio fica em `build/test-results/tls-client-host/`.

O caso `test-mediaplayer-host` compila o `src/shell/mediaplayer.c` real com
arquivos, audio, imagem, AC97, VESA, timer e recovery falsos. Ele exercita
playback individual e combinado, pausa, retomada, parada, atualizacao,
limites de nome, formatos invalidos, arquivos ausentes, dependencias
indisponiveis e limpeza; seu relatorio fica em
`build/test-results/mediaplayer-host/`.

O caso `test-shell-job-host` compila o executor cooperativo do Shell com
relogio, teclado, IPC, video e runtime falsos. Ele exercita sucesso, falha,
cancelamento, drenagem, timeout, deadlines, wakeups, eventos bloqueados,
geracoes obsoletas e o comando `job status`; seu relatorio fica em
`build/test-results/shell-job-host/`.

O caso `test-shell-pipeline-host` compila o executor de pipelines do Shell com
VFS, threads, video e logs falsos. Ele exercita parsing, limites de comandos e
destinos, pipes, leitura, escrita, redirecionamento, autoteste de pipe,
workers, falhas de criacao e limpeza; seu relatorio fica em
`build/test-results/shell-pipeline-host/`.

O caso `test-crypto-host` valida os contratos SHA-256, SHA-512 e Ed25519,
incluindo o ajuste de scalar para `uint32_t` e a rejeicao de entradas invalidas.
O helper `fe_cswap`, que nao possuia referencias no codigo ativo, foi removido;
`fe_ccopy` continua sendo o helper utilizado pelo caminho Ed25519.

O caso `test-package-host` cobre o servico de pacotes em filesystem simulado,
incluindo pacotes ZPKG/ZAPP validos e corrompidos, parsing, CRC, versoes,
instalacao transacional, atualizacao, recuperacao por failpoint, rollback,
remocao, modo legado FAT32, motivos canonicos, limites e limpeza sem escrever
em armazenamento real. O relatorio instrumentado fica em
`build/test-results/package-host/coverage.json`.

O caso `test-state-host` cobre recovery e a cadeia de notificadores de energia
com callbacks estaticos, incluindo estados opcionais, duplicatas, capacidade,
falhas canonicas e timeout. Seu relatorio instrumentado fica em
`build/test-results/state-host/coverage.json`.

Os casos `test-device-manager-host`, `test-app-api-host`,
`test-app-files-host`, `test-app-builtin-host` e `test-app-catalog-host`
cobrem, respectivamente, inventario de dispositivos com backends simulados, a
fachada geral de aplicativos, a fachada de arquivos sobre uma VFS falsa, a
geracao de imagens ZAPP internas com loader falso e o catalogo da App Store.
Eles exercitam estados indisponiveis, limites, erros canonicos e limpeza em
processos host instrumentados, sem hardware nem armazenamento real. Os
relatorios ficam em `build/test-results/device-manager-host/`,
`build/test-results/app-api-host/`, `build/test-results/app-files-host/`,
`build/test-results/app-builtin-host/` e `build/test-results/app-catalog-host/`.

O caso `test-input-host` valida as filas estaticas de teclado e ponteiro,
coalescencia, saturacao de deltas, filas cheias, despacho alternado e erro de
consumidor. O relatorio fica em `build/test-results/input-host/`.

Os casos `test-power-host` e `test-network-manager-host` exercitam, com
fixtures estaticos, os estados ACPI, as rotas de reboot terminal em seam
controlado no host e a limpeza de energia apos falhas, alem
do inventario PCI/USB, drivers ausentes e ativos, estado offline, configuracao
estatica, DHCP, lease aplicado/removido, restauracao atomica apos erro,
validacao de rotas e recusas de operacoes. Os relatorios instrumentados ficam
em `build/test-results/power-host/` e `build/test-results/network-manager-host/`.

Os casos `test-vfs-path-host` e `test-file-index-host` exercitam VFS/path e
file index com volumes, mounts, cursores e alocacao estaticos. O primeiro cobre
normalizacao, aliases, cwd, listagens, quiescencia e limites; o segundo cobre
pesquisa, rebuild cooperativo, cancelamento, resultados stale/missing,
corrupcao de candidato e tabela ativa e recuperacao. Os relatorios ficam em
`build/test-results/vfs-path-host/` e `build/test-results/file-index-host/`.

O caso `test-fs-host` valida a interface unificada com fixtures FAT12/FAT32 e
storage, cobrindo paths legacy e de volume, cursores, leitura por faixa,
mutacoes, operacoes atomicas, streaming, geracao e erros canonicos. O relatorio
fica em `build/test-results/fs-host/`.

O caso `test-storage-host` valida o backend de armazenamento com uma imagem
FAT12 estatica, cobrindo MBR/BPB, inventario, montagem, aliases, cursores,
leitura, espaco livre, rejeicao de mutacoes e limpeza. O caso
`test-block-host` executa os autotestes reais de BIO e block-cache, incluindo
limites, cancelamento, failpoints, eviction, writeback, sync e restauracao do
inventario. A fixture tambem invoca os callbacks ATA publicados pelo
inventario, despacha uma requisicao assincrona, cobre escrita parcial com
leitura fisica e força a espera de uma entrada em leitura a retornar
`ERR_TIMEOUT`, sem aguardar indefinidamente. Os relatorios ficam em
`build/test-results/storage-host/` e `build/test-results/block-host/`.

O caso `test-fat12-host` exercita o driver legado sobre uma imagem FAT12
estatica, incluindo leitura, paths de subdiretorio, metadados, operacoes
atomicas, streaming, cancelamento e erros de nome/tamanho. A fixture tambem
valida as APIs legadas de escrita e remocao na raiz, escrita e remocao em
subdiretorio e criacao de entradas de diretorio, com nomes codificados em
8.3. O relatorio fica em `build/test-results/fat12-host/`.

O caso `test-fat32-host` exercita o driver FAT32 sobre uma imagem estatica com
cadeia de clusters, leitura, paths, metadados, criacao, escrita, remocao e
limites, incluindo as APIs publicas de leitura e escrita sobre nomes 8.3.
O caso `test-storage-fat32-host` usa uma imagem FAT32 minima em memoria, sem
hardware ou allocator real. Ele cobre validacao de BPB/FSInfo e das duas FATs,
marcacao de volume com erro, alocacao e liberacao de cadeias com dois clusters,
escrita/leitura, nomes longos, substituicao, remocao e os writers transacionais
de storage, incluindo finish e abort. O relatorio fica em
`build/test-results/storage-fat32-host/` e pode ser executado com:

```text
make test-storage-fat32-host
```

O caso `test-vfs-host` valida o nucleo de descritores e I/O da VFS,
incluindo arquivos regulares, dispositivos, pipes, sockets, poll/select,
quiescencia e invariantes. Os relatorios ficam em
`build/test-results/fat32-host/` e `build/test-results/vfs-host/`.

O caso QEMU `qemu:tst4:storage-vfs` complementa essa fixture com o autoteste
real de storage e VFS, incluindo os callbacks de fixture para abertura,
leitura, escrita e poll, descritores invalidos e limpeza de tabelas. Execute
uma unica iteracao depois do build:

```text
make test-tst4-qemu-storage-vfs
```

Para atualizar a evidencia dinamica do caso em uma imagem instrumentada,
preserve a execucao em `build-coverage/test-results/`, gere o mapa com
`make coverage-map` e colete o relatorio apontando para o catalogo completo:

```text
python tools/coverage_collector.py collect --serial <execucao>/serial.log --symbols build-coverage/coverage-symbols.json --catalog tests/catalog.json --output <execucao>/coverage.json
```

O caso normal deve produzir `READY`, `HEARTBEAT`, `BEGIN` e `PASS`; a imagem
instrumentada pode exigir limites maiores, mas continua sujeita ao teto
absoluto de 600 segundos e nao deve ser repetida automaticamente.

O caso `test-slab-host` valida o ciclo de vida e os metadados do registrador
SLAB sem alocar paginas reais: inicializacao idempotente, limites, duplicidade,
informacoes por indice, estatisticas, ownership nulo e limpeza. O relatorio
instrumentado fica em `build/test-results/slab-host/`; a alocacao real e os
testes de paginas continuam cobertos pelo caso QEMU TST4.

O caso `test-timer-host` usa stubs de IDT, PIC e scheduler para exercitar
inicializacao, conversao de intervalos, one-shot, periodicos, notifier,
dispatch, cancelamento, callbacks com erro, snapshots e limpeza. O caso
`test-udp-host` usa um transporte IPv4 falso para exercitar envio, reinjecao,
checksum, listeners, broadcast, callbacks recusados, limites e limpeza de
endpoints. Os relatorios ficam em `build/test-results/timer-host/` e
`build/test-results/udp-host/`, sem hardware ou conexao externa. O caso
`test-arp-host` usa Ethernet falsa e relogio controlado para exercitar
configuracao, validacao de enderecos, cache, retries, timeout, requests,
replies, entradas invalidas e limpeza; seu relatorio fica em
`build/test-results/arp-host/`.
O caso `test-icmp-host` usa IPv4 e timer falsos para exercitar configuracao,
checksum, echo request/reply, RTT, timeout, mudanca de configuracao, fila
pendente, pacotes invalidos e falhas de transporte; seu relatorio fica em
`build/test-results/icmp-host/`.
O caso `test-dns-host` usa UDP, IPv4 e timer falsos para exercitar consultas,
cache, CNAME, timeout, respostas invalidas e falhas de transporte; seu
relatorio fica em `build/test-results/dns-host/`.
O caso `test-dhcp-host` usa UDP e timer falsos para exercitar descoberta,
oferta, lease, renovacao, rebinding, expiracao, NAK, mensagens invalidas,
comprimentos de opcoes rejeitados e falhas de transporte; seu relatorio fica
em `build/test-results/dhcp-host/`.
O caso `test-bearssl-compat-host` valida diretamente as rotinas de memoria e
string exigidas pelo BearSSL, incluindo sobreposicao em `memmove`, comparacao,
preenchimento e comprimentos vazios ou nulos; seu relatorio fica em
`build/test-results/bearssl-compat-host/`.
O caso `test-ethernet-host` usa drivers, interfaces e frames falsos para
exercitar handlers, polling, filtragem, transmissao, quiescencia, sk_buff,
net_buffer e limpeza sem hardware real; seu relatorio fica em
`build/test-results/ethernet-host/`.
O caso `test-tcp-host` usa IPv4 e timer falsos para exercitar handshake,
dados, ACK, FIN, RST, retransmissao, timeout, callbacks recusados, janelas,
limites e limpeza sem rede externa; seu relatorio fica em
`build/test-results/tcp-host/`.
O caso `test-tls-host` usa relogio, RNG e cliente TLS falsos para exercitar
politica, validade, cadeia, SAN, pinning, rotacao, revogacao, estados
indisponiveis e autoteste sem rede externa; seu relatorio fica em
`build/test-results/tls-host/`.
O caso `test-http-host` usa DNS, socket, TLS, timer e stack falsos para
exercitar URLs, opcoes, headers, respostas com tamanho/chunked/EOF,
streaming, redirects, HTTPS, limites, timeouts e falhas sem rede externa; seu
relatorio fica em `build/test-results/http-host/`.
O caso `test-net-socket-host` usa TCP, timer, filas de espera e VFS falsos para
exercitar handles geracionais, conexao, filas RX/TX, eventos, EOF, timeout,
cancelamento, limites, reset e limpeza sem rede externa; seu relatorio fica em
`build/test-results/net-socket-host/`.
O caso `test-socket-runtime-host` usa o `src/core/socket.c` real com TCP, VFS,
filas UNIX, espera cancelada e SKBs falsos para exercitar o runtime completo,
incluindo adaptadores de descritor, EOF, erros, limites e limpeza; seu
relatorio instrumentado fica em `build/test-results/socket-runtime-host/`.
O caso `test-sysfs-host` compila o provider sysfs real com inventarios PCI,
rede, bloco e energia falsos. Ele exercita lookup, listagens ordenadas,
renderizacao de todos os atributos, snapshots somente leitura, seek, poll,
fallback de energia, overflow e limpeza; seu relatorio instrumentado fica em
`build/test-results/sysfs-host/`.
O caso `test-vma-host` usa processo ring 3, paging, PMM e VFS falsos para
exercitar VMAs fixas e anonimas, materializacao lazy, page faults validos e
invalidos, `mmap`, `munmap`, limites, estatisticas e limpeza; seu relatorio fica
em `build/test-results/vma-host/`.
O caso `test-paging-host` exercita diretamente o diretorio de paginas, tabelas,
mapas de kernel e usuario, framebuffer, copia entre espacos, materializacao
lazy, consulta do diretorio atual, limites, overflow, paginas ausentes e
limpeza. A fixture usa PMM, VESA e processo falsos com buffers estaticos, sem
instrucoes privilegiadas ou hardware;
seu relatorio fica em `build/test-results/paging-host/`. Execute-o com
`HOST_CC` apontando para um compilador C nativo:

```text
make test-paging-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

Para reconstruir a imagem instrumentada separada e gerar o mapa de simbolos:

```text
make coverage-image
make coverage-map
```

O caso `test-memory-host` exercita diretamente o mapa E820, inicializacao do
PMM por zona, alocacao contigua, heap estatico, alinhamento, limites, erros
canonicos, alocacao de pagina individual, coalescencia e restauracao das
estatisticas. A fixture nao acessa enderecos fisicos nem hardware; seu
relatorio fica em
`build/test-results/memory-host/`. Execute-o com `HOST_CC` apontando para um
compilador C nativo:

```text
make test-memory-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-process-signal-host` exercita o ciclo de sinais com processos
estaticos: nomes, inicializacao, mascaras, acoes, coalescencia, entrega a
handler, `sigreturn`, terminacao padrao, notificacao `SIGCHLD`, snapshots e
validacao de invariantes. O caminho de IRQ e substituido por um stub somente
no build host; o relatorio fica em `build/test-results/process-signal-host/`:

```text
make test-process-signal-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-process-ipc-host` exercita IPC e foco com processos estaticos:
mensagens validas e invalidas, fila cheia, recebimento, espera com timeout e
sinal, foco, fallback, restauracao e limpeza. O relatorio fica em
`build/test-results/process-ipc-host/`:

```text
make test-process-ipc-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-process-host` compila `src/process/process.c` real com paging,
VMA, memoria, SLAB, syscall, sinais, IPC, VFS e scheduler falsos. Ele cobre
estado inicial, snapshots, limites de criacao, transicoes, cancelamento,
terminacao, desligamento, wait queues e limpeza da fixture, sem instrucoes
privilegiadas ou hardware. O relatorio fica em
`build/test-results/process-host/`:

```text
make test-process-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

A fixture tambem valida o bootstrap sem cache, a pre-condicao do inicio do
scheduler, o descarte apos falha de criacao, a copia/cancelamento de uma espera
ativa, o diagnostico de canario e os helpers de formatacao da stack. O idle e
executado uma vez no caminho `ZEPHYROS_HOST_TEST`, sem `hlt`; a troca de
contexto Assembly continua fora do processo host.

O caso `test-workqueue-host` exercita a fila de trabalho com autoteste interno,
callbacks, prioridades, FIFO, atrasos, coalescencia, rerun, cancelamento,
fallback, quiescencia, worker e validacao de invariantes. O worker usa quatro
iteracoes somente no build host para que a fixture nunca aguarde
indefinidamente; o relatorio fica em `build/test-results/workqueue-host/`:

```text
make test-workqueue-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-dispatch-host` exercita o dispatcher com handlers falsos e
verifica o diagnostico de comando desconhecido, a normalizacao de espacos e
escape, o limite de 31 caracteres, o encaminhamento de um comando conhecido,
o erro canonico para entrada nula e o despacho unico de cada comando da tabela
com argumentos sentinela. O relatorio fica em
`build/test-results/shell-dispatch-host/`:

```text
make test-shell-dispatch-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-introspection-host` exercita diretamente o parser hexadecimal
da introspeccao do Shell, incluindo digitos numericos, minusculos e maiusculos,
prefixos invalidos, digito invalido, entrada nula e overflow de `uint32_t`. O
relatorio fica em `build/test-results/shell-introspection-host/`:

```text
make test-shell-introspection-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-font-host` exercita o driver de fonte sem hardware, verificando
inicializacao idempotente, as dimensoes publicadas de 8x16 e a consulta de
glyphs validos e fora da faixa com fallback seguro. O relatorio fica em
`build/test-results/font-host/`:

```text
make test-font-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-rtc-status-host` exercita o RTC com uma porta CMOS simulada no
build host, sem executar instrucoes privilegiadas. Ele verifica destino nulo,
inicializacao invalida e valida, leituras BCD/binaria e 12/24 horas, calendario,
leituras estaveis, autoteste, timeout de atualizacao e estado restaurado. O
relatorio cobre as 17 funcoes de `src/drivers/rtc.c` e fica em
`build/test-results/rtc-status-host/`:

```text
make test-rtc-status-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-wifi-manager-host` exercita o inventario de candidatos PCI e USB
com fixtures estaticos, incluindo formatacao e busca de identificadores,
contadores, estados READY/UNSUPPORTED/ERROR, scan, conexao aberta, metadados
invalidos, indisponibilidade do backend e recuperacao. O relatorio fica em
`build/test-results/wifi-manager-host/`:

```text
make test-wifi-manager-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-usb-manager-host` exercita o inventario USB com controladores PCI
UHCI, EHCI e fora do escopo, portas vazias e configuradas, dispositivos MSC e
HID, sincronizacao de estados, polling, formatacao, limites, falhas de driver,
indisponibilidade e recuperacao. A fixture usa somente backends estaticos e o
relatorio fica em `build/test-results/usb-manager-host/`:

```text
make test-usb-manager-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-usb-hid-host` exercita o driver HID Boot com teclados e mouses
UHCI simulados, relatorios validos e corrompidos, rollover, duplicidade,
overflow, timeout, falhas de controle e interrupt, reconfiguracao, remocao,
limites e estados indisponiveis. A fixture publica apenas eventos de entrada
falsos e o relatorio fica em `build/test-results/usb-hid-host/`:

```text
make test-usb-hid-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-usb-msc-host` exercita o driver USB Mass Storage com transporte
BOT/SCSI simulado: inquiry, TUR, capacity, READ10, registro de bloco somente
leitura, filtros, limites, retry, reset recovery, CSW corrompido e estados de
indisponibilidade. Nenhuma escrita real e feita; o relatorio fica em
`build/test-results/usb-msc-host/`:

```text
make test-usb-msc-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-devfs-host` exercita a camada de dispositivos virtuais com ATA e
speaker simulados: inicializacao, registro, listagem, lookup, permissoes,
null/zero, speaker, hda, leituras, seeks, ioctl, sincronizacao e caminhos
indisponiveis. Nenhum hardware, VFS real ou armazenamento real e acessado; o
relatorio fica em `build/test-results/devfs-host/`:

```text
make test-devfs-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-procfs-host` compila o provider procfs real com VFS, processos,
snapshots e controles de log simulados. Ele cobre listagem, lookup, leitura,
mapas, seeks, poll, ioctl, sync, permissoes, limites e limpeza sem kernel ou
hardware real; o relatorio fica em `build/test-results/procfs-host/`:

```text
make test-procfs-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-wav-host` compila o parser e reprodutor WAV reais com buffers
estaticos, chunks RIFF/WAVE, metadados, duracao, playback simulado e entradas
truncadas ou invalidas. A fixture nao acessa hardware de audio; o relatorio
fica em `build/test-results/wav-host/`:

```text
make test-wav-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-bmp-host` compila o parser e renderizador BMP reais com imagens
estaticas de 1, 4, 8 e 24 bpp, paletas, orientacao, transparencia,
redimensionamento e escala. A fixture cobre truncamento, overflow e falhas de
alocacao usando somente framebuffer e VESA simulados; o relatorio fica em
`build/test-results/bmp-host/`:

```text
make test-bmp-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-rng-host` compila o driver RNG real com um backend deterministico
de CPUID/RDRAND somente para o host. A fixture cobre inicializacao com e sem
capacidade, leitura de palavras, falha de hardware, limites de buffer e
validacao do estado publicado; o relatorio fica em
`build/test-results/rng-host/`:

```text
make test-rng-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-serial-host` compila o driver COM1 real com portas UART e flags de
interrupcao simuladas. A fixture cobre inicializacao, leitura sem dados,
recepcao, fila de transmissao, filtragem de bytes, sequencias ANSI e limites de
flush sem acessar I/O privilegiado; o relatorio fica em
`build/test-results/serial-host/`:

```text
make test-serial-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-tss-host` compila o driver TSS real com GDT e `tss_flush()`
simulados no host. A fixture cobre estado antes da inicializacao, stacks
invalidas e validas, inicializacao repetida e montagem do descritor sem
executar `lgdt` ou trocar segmentos; o relatorio fica em
`build/test-results/tss-host/`:

```text
make test-tss-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-speaker-host` compila o driver PC speaker real com portas PIT e
controle simulados. A fixture cobre inicializacao, silencio, frequencia zero,
beep, melody e esperas limitadas por ticks sem executar I/O privilegiado ou
`hlt`; o relatorio fica em `build/test-results/speaker-host/`:

```text
make test-speaker-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-keyboard-host` compila o driver PS/2 real com controlador, IRQ,
fila e portas simulados. A fixture cobre tabelas de scancode, teclas ABNT2,
inicializacao, filtro de cancelamento, metricas, reset e falhas de dependencia
sem executar `cli`, `sti`, I/O privilegiado ou espera ativa no host; o relatorio
fica em `build/test-results/keyboard-host/`:

```text
make test-keyboard-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-protocol-adapter-host` compila o adaptador ZTEST real com o nucleo
incremental, transporte serial, relogio e executor de casos simulados. A
fixture cobre inicializacao, recepcao fragmentada, handshake, heartbeat, RUN,
falha com fase, ABORT, panic, timeout e bloqueios sem acessar QEMU ou hardware;
o relatorio fica em `build/test-results/protocol-adapter-host/`:

```text
make test-protocol-adapter-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-blackbox-host` compila o harness black-box TST5 real com
observador de terminal falso. A fixture cobre os nove marcadores de cenário,
mudança de geração do terminal, `process_yield()` limitado e seleção inválida
sem acessar QEMU ou hardware; o relatorio fica em
`build/test-results/blackbox-host/`:

```text
make test-blackbox-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-coverage-host` compila o coletor de cobertura real com transporte
serial falso. A fixture cobre escrita parcial, falta de progresso, truncamento
de identificador, tabela hash, callbacks de instrumentacao e relatorio ZCOV;
o relatorio fica em `build/test-results/test-coverage-host/`:

```text
make test-coverage-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-input-host` exercita a entrada do Shell com terminal,
historico, navegacao para cima/baixo, edicao, teclas de rolagem, cancelamento,
bloqueio, modificadores e limite do buffer. A fixture usa apenas video,
teclado e logs falsos; nao acessa hardware. O relatorio fica em
`build/test-results/shell-input-host/`:

```text
make test-shell-input-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-hosted-host` exercita a superficie Classic hospedada do
Shell com Window Manager, incluindo modo indisponivel, abertura, reabertura,
desenho, tecla, mouse valido/invalido, fechamento e rollback quando o registro
falha. A fixture usa somente callbacks e terminal falsos; nao executa GUI ou
hardware. O relatorio fica em `build/test-results/shell-hosted-host/`:

```text
make test-shell-hosted-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-display-host` exercita as metricas e escalas do display com VESA,
backbuffer, desktop, taskbar e Window Manager falsos. A fixture cobre
disponibilidade, limites de resolucao, parsing, conversao de pixels, refresh de
cenas e rollback de reflow; o relatorio fica em
`build/test-results/display-host/`:

```text
make test-display-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-core-host` exercita o ciclo de vida central do Shell com
servicos falsos. A fixture cobre inicializacao, mouse/scroll, suspensao do
terminal, conclusao de comando e redraw pelo fluxo de `shell_handle_key`, sem
hardware ou GUI real. O relatorio fica em
`build/test-results/shell-core-host/`:

```text
make test-shell-core-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-usb-transport-host` exercita o despachante de transporte USB com
backends EHCI e UHCI falsos. A fixture cobre argumentos nulos, controlador
desconhecido, encaminhamento de controle, Bulk, toggles e Interrupt, sem
hardware real. O relatorio fica em
`build/test-results/usb-transport-host/`:

```text
make test-usb-transport-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-gui-host` exercita as primitivas da GUI com framebuffer, fonte e
metricas falsos. A fixture cobre temas, texto nativo e escalado, medicao,
paineis, formas, gradientes, botoes, molduras e limites de tela, sem hardware
real. O relatorio fica em `build/test-results/gui-host/`:

```text
make test-gui-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-commands-vfs-host` exercita `grep` e `pipetest` com
entrada de pipeline, saida de video e autoteste falsos. A fixture cobre
entrada fragmentada, comparacao sem diferenca de maiusculas, argumentos
invalidos, erro de leitura/escrita, linha acima do limite e propagacao do
codigo do pipetest. O relatorio fica em
`build/test-results/shell-commands-vfs-host/`:

```text
make test-shell-commands-vfs-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-recovery-runtime-host` exercita os utilitarios freestanding do
runtime de recuperacao com buffers estaticos. A fixture cobre preenchimento,
copia, comprimento, comparacao lexicografica e os niveis do ponto de entrada
de log, sem executar o loader, acessar imagem ou depender de hardware. O
relatorio fica em `build/test-results/recovery-runtime-host/`:

```text
make test-recovery-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-panic-host` exercita as rotas de `panic` e `panic_memory` com
captura do protocolo, da tela, das metricas e do halt por fixture host-only.
Mensagens ausentes e explicitas, valores de memoria zero e no limite e o
encaminhamento dos motivos canonicos sao verificados sem executar halt real.
O relatorio fica em `build/test-results/panic-host/`:

```text
make test-panic-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-pci-host` exercita a leitura e escrita do espaco de configuracao,
varredura, funcoes multifuncao, inventario, limite de dispositivos e habilitacao
de memoria, I/O e bus mastering com portas PCI falsas. O relatorio fica em
`build/test-results/pci-host/`:

```text
make test-pci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-ata-host` exercita a descoberta e identificacao ATA, inventario,
IRQ, leitura e escrita PIO, flush, limites de LBA, retries, timeouts e falhas
com portas, dados IDENTIFY e armazenamento falsos. Nenhum acesso a disco ou
porta I/O real e executado pelo teste. O relatorio fica em
`build/test-results/ata-host/`:

```text
make test-ata-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de funcoes
e warnings tratados como erro. Uma execucao `PASS` deve resolver as funcoes
de `src/drivers/ata.c` sem enderecos desconhecidos ou ambiguos e manter
dispositivos, contadores, handlers e estado de portas confinados ao processo.

O caso `test-idt-host` exercita a inicializacao da IDT, montagem de gates, PIC,
handlers simples e compartilhados, mascaras, estatisticas de IRQ, syscall,
despacho, EOI e panic controlado usando stubs de ISR, portas e flags. Nenhuma
instrucao `lidt`, `sti`, `cli` ou I/O privilegiado e executada pelo processo
host. O relatorio fica em `build/test-results/idt-host/`:

```text
make test-idt-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-icons-host` exercita o registro e o cache de icones com filesystem,
BMP, memoria e VESA falsos. A fixture verifica fallback sem filesystem, carga
valida, formato invalido, falha de memoria, mutacoes do registro e limites de
desenho. O relatorio fica em `build/test-results/icons-host/`:

```text
make test-icons-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-vesa-host` exercita o driver VESA com fixtures estaticas de boot,
framebuffer, memoria, fonte e relogio. A fixture verifica inicializacao valida
e invalida, modos 24/32 bpp, backbuffer, pixels, desenho, clipping, frames,
metricas, flip, falha de alocacao e desativacao. O relatorio fica em
`build/test-results/vesa-host/`:

```text
make test-vesa-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-video-host` exercita o driver de video e o terminal hospedado
com framebuffer, fonte, VESA, mouse e logs falsos. A fixture verifica desenho,
cursor, flush, scrollback, snapshots validos e corrompidos, rolagem,
inicializacao, suspensao, quiescencia e estados indisponiveis. O relatorio
fica em `build/test-results/video-host/`:

```text
make test-video-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-acpi-host` exercita a descoberta ACPI com firmware e mapa E820
falsos, cobrindo RSDP, RSDT/XSDT, FADT, MADT, FACS, AML `_S5_`, consultas,
checksums, tabelas corrompidas e rotas de energia sem I/O real. O relatorio
fica em `build/test-results/acpi-host/`:

```text
make test-acpi-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso `test-shell-command-utils-host` exercita os utilitarios de comandos do
Shell com parsing de tokens e argumentos, comparacao de subcomandos,
normalizacao para maiusculas, conversao numerica, limites, entradas invalidas
e formatacao decimal/hexadecimal. A fixture usa somente buffers de entrada,
saida e logs falsos. O relatorio fica em
`build/test-results/shell-command-utils-host/`:

```text
make test-shell-command-utils-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O registro em `tests/coverage/registry.json` somente seleciona enderecos de
relatorios `PASS` existentes e filtra as fontes declaradas. Um relatorio
ausente, com endereco desconhecido ou ambiguo bloqueia o gate estrito; nao ha
inferência de cobertura por arquivo ou modulo.

Para instalar as dependencias Python fixadas pelo atualizador:

```text
python -m pip install -r tools/requirements-updater.txt
```

O autoteste do atualizador pode ser executado pelo usuario com:

```text
make update-test
python tools/updater.py selftest
```

Para conferir ou gerar a configuracao remota, os comandos suportados sao:

```text
python tools/updater.py check-remote --config config/update-remote.json --header src/include/core/update_remote_config.h
python tools/updater.py sync-remote --config config/update-remote.json --output src/include/core/update_remote_config.h
```

`sync-remote` gera um header e somente deve ser usado quando essa alteracao
estiver dentro do escopo da tarefa. Nao sobrescrever configuracao de producao
por tentativa ou por inferencia.

O servidor HTTP local da integracao U5 usa:

```text
python tools/updater.py serve-u5 --root docs/fixtures/updates/u5 --port 8000
```

Procedimentos especificos de distribuicao remota ficam na documentacao
canonica da funcionalidade, nao nesta memoria geral.

## EP9.0A: fixtures ZSYS para QEMU

O alvo `system-fixtures` gera uma matriz compacta assinada usando a chave
privada indicada por `SYSTEM_PRIVATE_KEY` em `Makefile.local`. A chave privada
permanece fora do repositorio. A imagem de fixture e hibrida: FAT12 legado no
inicio e FAT32 `ZEPHYROS` a partir do LBA 8192. Os arquivos usam a extensao
`.ZSYS` no FAT32.

Cada fixture e gravada em uma imagem propria dentro de
`build\system-fixture-images`. O alvo nao injeta a matriz inteira em
`build\zephyros.img`; cada imagem armazena somente um envelope ZSYS na raiz
FAT32. Antes da primeira geracao apos uma tentativa antiga, recrie a imagem
base com `make clean` e `make`.

Depois de gerar as fixtures, inicie uma imagem por vez com estes comandos
completos:

```text
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\VALID.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\TRUNC.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\HDRBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\PAYBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\SIGBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\OVERSIZ.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\MISALGN.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\VERBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\EPCHBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\ABIBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\SCHBAD.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\IMGHASH.img
make run-system-fixture SYSTEM_FIXTURE_IMAGE=build\system-fixture-images\CMPHASH.img
```

Dentro de cada QEMU, execute o comando correspondente ao alias da imagem:

```text
update system verify system:/VALID.ZSYS
update system verify system:/TRUNC.ZSYS
update system verify system:/HDRBAD.ZSYS
update system verify system:/PAYBAD.ZSYS
update system verify system:/SIGBAD.ZSYS
update system verify system:/OVERSIZ.ZSYS
update system verify system:/MISALGN.ZSYS
update system verify system:/VERBAD.ZSYS
update system verify system:/EPCHBAD.ZSYS
update system verify system:/ABIBAD.ZSYS
update system verify system:/SCHBAD.ZSYS
update system verify system:/IMGHASH.ZSYS
update system verify system:/CMPHASH.ZSYS
```

Somente `system:/VALID.ZSYS` deve ser aceito; os demais devem ser recusados
sem alterar imagem, cache, FAT12 legado ou estado persistente.

Para o diagnostico somente leitura do volume FAT32, primeiro copie o ID exato
mostrado por `storage list` e execute:

```text
storage check <id-exato-do-volume-fat32>
```

## EP9.1: matriz de recuperacao dos slots

O alvo `system-slots-matrix` gera imagens independentes em
`build\system-slots-matrix`, a partir da fixture base. O gerador nao precisa
de uma chave privada: ele copia o envelope ja assinado e altera somente os
controles FAT32 da fixture. A matriz cobre uma copia de estado invalida, as
duas copias de estado invalidas, cada fase do journal, journal redundante
parcial ou totalmente invalido, falta de espaco e volume FAT32 ausente.

Gere todas as imagens com:

```text
make system-slots-matrix
```

Inicie cada caso com o comando completo correspondente:

```text
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_ONE_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_BOTH_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_NEWER.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_PREPARED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_STAGING.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_VERIFIED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_COMMITTED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_NEWER.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_ONE_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_BOTH_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\NO_SPACE.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\NO_VOLUME.img
```

Expectativas: `STATE_ONE_BAD` deve continuar `READY`; `STATE_NEWER` deve
selecionar a sequencia 2; `STATE_BOTH_BAD` e `JOURNAL_BOTH_BAD` devem
aparecer `DEGRADED`; `JOURNAL_PREPARED`, `JOURNAL_STAGING` e
`JOURNAL_NEWER` devem preservar A, remover o staging e limpar o journal;
`JOURNAL_VERIFIED` e `JOURNAL_COMMITTED` devem publicar B como pendente;
`JOURNAL_ONE_BAD` deve recuperar usando a copia valida; `NO_SPACE` deve
recusar o preflight com `SPACE`; e `NO_VOLUME` deve deixar os slots
indisponiveis/degradados.

Para cancelamento cooperativo, use uma fixture nova e pressione F12 durante
a copia, antes de consultar novamente o estado:

```text
make run-system-slots-fixture
```

```text
update system stage system:/VALID.ZSYS --confirm
update system slots
```

O resultado esperado e cancelamento sem slot pendente, com A preservado e
journal limpo. A aplicacao, a selecao no boot e o reboot continuam fora da
EP9.1.

## EP9.2A: matriz do recovery loader

Depois de gerar a matriz, os casos de boot devem ser iniciados um por vez:

```text
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_ACTIVE_VALID.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_PENDING_VALID.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_BAD_SIGNATURE.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_BAD_IMAGE_HASH.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_BAD_COMPONENT_HASH.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_ATTEMPT_INTERRUPTED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_ONE_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_BOTH_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_PREPARED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\NO_VOLUME.img
```

`BOOT_ACTIVE_VALID` deve iniciar A autenticado. `BOOT_PENDING_VALID` deve
iniciar B, persistir a tentativa e permitir que o kernel a confirme. Os tres
casos de hash/assinatura, journal e FAT32 ausente devem exibir o diagnostico
do loader e iniciar somente o fallback legado autenticado. No caso interrompido,
o boot seguinte deve marcar B como `FAILED`, limpar o pendente e preservar A.

## EP9.2B: matriz do menu pre-kernel

Depois de qualquer alteracao de codigo, header ou Makefile da EP9.2B, o
usuario executa os gates e regenera as fixtures da mesma revisao:

```text
make q3check
make clean && make
make system-slots-matrix
```

Inicie cada caso com o comando completo correspondente:

```text
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_ACTIVE_VALID.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\MENU_PREVIOUS_VALID.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\MENU_FAILED_VALID.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\MENU_RETRY_NO_CONTROL.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_BAD_SIGNATURE.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_BAD_IMAGE_HASH.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\BOOT_BAD_COMPONENT_HASH.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_ONE_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\STATE_BOTH_BAD.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\JOURNAL_PREPARED.img
make run-system-slots-matrix SYSTEM_SLOTS_MATRIX_IMAGE=build\system-slots-matrix\NO_VOLUME.img
make run-recovery-menu-vga
```

Em `BOOT_ACTIVE_VALID`, deixar a janela de dois segundos expirar deve iniciar
A; F8 abre o menu sem timeout e Esc continua A sem escrita. Em
`MENU_PREVIOUS_VALID`, escolha o anterior one-shot, confirme que
`update system slots` ainda mostra B como ativo e reinicie sem F8 para voltar
a B. Em `MENU_FAILED_VALID`, deixe primeiro os dez segundos expirarem para
confirmar que A anterior inicia por padrao. Em outra execucao, cancele uma vez
para confirmar estado inalterado; depois regenere a fixture, escolha retry,
confirme duas vezes e consulte `update system slots` para verificar a promocao
de B. Uma execucao separada deve ser reiniciada antes do acknowledge e voltar
a `FAILED` sem loop.

`MENU_RETRY_NO_CONTROL` deve manter retry desabilitado. Nos tres casos
`BOOT_BAD_*`, o primeiro boot transforma o pendente em `FAILED` com
`SIGNATURE` ou `HASH`. Antes do timeout, selecione o retry nessa mesma
instancia; a segunda confirmacao deve revalidar, recusar o candidato e voltar
ao menu com o anterior ainda disponivel. `STATE_ONE_BAD` deve operar pela copia
valida. `STATE_BOTH_BAD`, `JOURNAL_PREPARED` e `NO_VOLUME` devem restringir as
acoes ao legado autenticado. `run-recovery-menu-vga` valida a mesma navegacao
e os diagnosticos sem VESA.

A EP9.2B somente pode ser marcada validada depois de confirmar todos esses
fluxos, incluindo ausencia de payload nao autenticado, retry automatico ou
alteracao persistente durante boot one-shot/cancelamento.

## EP9.3: fluxo ZSYS em uma matriz guiada

Depois da implementacao, executar apenas estes comandos, uma vez para a mesma
revisao:

```text
make q3check
make clean && make
make run-system-update-matrix
```

O ultimo alvo regenera as fixtures e abre `SYSTEM_UPDATE_GUIDED.img` com disco
temporario `-snapshot` e monitor QEMU no mesmo terminal. A imagem ja contem um
cache ZSYS autenticado; use `update system status`, `update system verify
--cached`, `update system apply`, `update system apply --confirm`, `update
system cancel` e `update system cancel --confirm`. `apply --confirm` deve
publicar somente o pendente e solicitar `reboot`; `cancel --confirm` deve
preservar os arquivos dos slots.

O monitor permite salvar e restaurar o ponto inicial dentro da mesma execucao
com `savevm ep93` e `loadvm ep93`, evitando regenerar imagens ou repetir a
senha da chave. O preflight remoto e o fetch usam a tag exata publicada na
Release v2 configurada; nao inventar outra tag nem repetir a matriz local para
cada fixture. As imagens `SYSTEM_CACHE_ONE_BAD`, `SYSTEM_CACHE_BOTH_BAD` e
`SYSTEM_CACHE_INTERRUPTED` ficam reservadas para diagnostico dirigido quando
o caso guiado apontar divergencia.

## EP9.4C: reinicio controlado pelo Updater Classic

Para a mesma revisao, o usuario executa somente:

```text
make q3check
make clean && make
make run-ep94c-matrix
```

O ultimo alvo gera uma unica imagem guiada de 256 MiB com slot A ativo e cache
ABI 2 autenticado. Na aba `Sistema`, validar edicao da tag, verificacao,
aplicacao, `Reiniciar depois`, banner persistente e o reinicio com confirmacao
final. As fixtures compactas de preflight ficam em
`build\ep94c-matrix\preflight-fixtures`; nao executar imagens separadas nem
repetir a matriz EP9.4B.

## EP9.4B: cadeia autenticada em uma imagem guiada

Para a mesma revisao, o usuario executa somente:

```text
make q3check
make clean && make
make run-ep94b-matrix
```

O alvo solicita a chave privada uma unica vez e gera uma imagem de 256 MiB com
slot A ABI 1, cache ABI 2 e os vetores `BADBOOT.ZSY`, `BADSTG2.ZSY`,
`BADKERN.ZSY`, `BADHAND.ZSY` e `RETURN.ZSY`. Assim que o QEMU abrir, salve o
estado inicial no monitor:

```text
savevm ep94b
```

No primeiro boot, use:

```text
update system status
update system slots
update system verify system:/BADBOOT.ZSY
update system verify system:/BADSTG2.ZSY
update system verify system:/BADKERN.ZSY
update system apply --confirm
reboot
update system slots
health check
regcheck full
```

Os tres vetores devem ser recusados com `HASH`. A aplicacao publica B como
pendente sem reinicio automatico; depois de `reboot`, a cadeia ABI 2 deve abrir
o kernel e confirmar B. Para validar rollback, restaure o snapshot inicial no
monitor QEMU, aplique novamente e reinicie a maquina antes do acknowledge. O
boot seguinte deve manter A e preservar B como `FAILED`, sem repeticao
automatica.

Os retornos protegidos usam o mesmo snapshot, sem reconstruir a imagem:

```text
loadvm ep94b
```

```text
update system stage system:/BADHAND.ZSY --confirm
reboot
```

Repita o `loadvm ep94b` e use `RETURN.ZSY` no lugar de `BADHAND.ZSY`. Ambos
devem limpar `ZSBC` e `ZSBH`, recusar a cadeia e retornar ao slot A ou ao
kernel legado sem confirmar B.

Para as duas corrupcoes de disco, restaure `ep94b`, execute uma linha no
monitor e reinicie:

```text
qemu-io ep94bmatrix "write -P 0 446 64"
system_reset
```

```text
qemu-io ep94bmatrix "write -P 0 2097152 512"
system_reset
```

A primeira remove a entrada MBR da particao; a segunda invalida o BPB do LBA
4096. Em ambos os casos, a raiz fixa deve iniciar o kernel legado autenticado.
Execute primeiro `storage volumes` quando o diagnostico de volume for
necessario; o ID de `storage check` deve ser copiado literalmente dessa saida.

## TST2: executor QEMU

Antes de executar o QEMU, valide a infraestrutura host-only. O alvo compila o
núcleo C do protocolo com `HOST_CC` e executa os testes Python do runner. O
compilador host não é o cross-compiler usado pelo kernel; configure `HOST_CC`
no `Makefile.local` ou no `PATH`.

```text
make test-tst2-host
```

O runner host-only de TST2 trabalha sobre uma imagem ja compilada e exige
`build/zephyros.img` presente; ele nao dispara `make`. Os comandos disponiveis
apos a implementacao sao:

```text
make test-qemu-selftest
make test-qemu
python tools/qemu_test_runner.py run --image build/zephyros.img --profile smoke
python tools/qemu_test_runner.py stress --image build/zephyros.img --case qemu:tst2:boot-ready --iterations 10
python tools/qemu_test_runner.py stress --image build/zephyros.img --case qemu:tst2:boot-ready --duration 60
python tools/qemu_test_runner.py stress --image build/zephyros.img --case qemu:tst2:boot-ready --until-failure
```

O modo indefinido requer `Ctrl+C`. Cada execucao cria um diretorio novo em
`build/test-results/<run-id>/`, preservando manifesto, checksum, serial e
logs do QEMU. A infraestrutura TST2 ja foi validada; para alteracoes novas,
repita os gates operacionais e o smoke test somente apos a validacao host-only.

## TST3: suite host-only de logica e limites

Os testes da TST3 nao usam hardware, QEMU, allocator real ou tempo real. O
compilador host e independente do cross-compiler do kernel. Configure `HOST_CC`
e, para a verificacao sanitizada, `HOST_SANITIZE_CC` em `Makefile.local` ou no
`PATH`.

No Windows, se o pacote MSYS2 UCRT64 nao fornecer os runtimes ASan/UBSan, use
o instalador oficial do LLVM fora do repositorio e aponte `HOST_SANITIZE_CC`
para `<LLVM_DIR>\bin\clang.exe` em `Makefile.local`. O runner consulta o
diretorio de recursos do Clang e adiciona automaticamente
`lib\clang\<versao>\lib\windows` ao ambiente do teste. Nao copie runtimes para
o repositorio nem use fallback silencioso; ausencia do compilador ou runtime
retorna `BLOCKED`/2.

```text
make test-tst3-host
make test-tst3-sanitize
make package-test
make update-test
```

`test-tst3-host` compila strings e compressao com `-std=c11 -Wall -Wextra
-Werror`, consulta as estatisticas publicadas da compressao, executa os testes
Python formais de packager/updater e roda os
self-tests existentes. `test-tst3-sanitize` usa Clang/LLVM com ASan/UBSan e
nao faz fallback para outro compilador. O resultado fica em
`build/test-results/tst3-host/manifest.json` e `result.json`, com logs dos
subprocessos; `PASS` retorna 0, `FAIL` retorna 1 e dependencia ausente ou
runtime sanitizador indisponivel retorna `BLOCKED`/2.
Cada modo tambem preserva uma copia em `build/test-results/tst3-host/strict/`
ou `build/test-results/tst3-host/sanitize/`.

O fluxo recomendado para uma alteracao que toque o kernel continua sendo:
TST3 host-only, gates de qualidade, build limpo e somente entao o QEMU. A
TST3 cobre nesta camada strings, compressao, empacotamento e atualizacao; VFS,
paging, processos, drivers e energia permanecem nas fases posteriores.

## TST4.1: autoteste de memoria e SLAB no kernel

A primeira camada da TST4 nao usa Shell nem inicia testes durante o boot. O
caso agregado `qemu:tst4:memory-slab` e acionado uma unica vez por `RUN`
explicito no protocolo ZTEST depois de `READY`. O alvo focado usa o modo
`stress` com `--iterations 1`; nao ha repeticao automatica do caso apos falha.

Depois dos gates da alteracao, execute:

```text
make test-qemu-selftest
make q3check
make clean
make
make test-tst4-qemu
make catalog-test
```

O autoteste registra fases `preconditions`, `memory-before`, `slab-self-test`,
`memory-after` e `postconditions`. O resultado esperado no QEMU e
`READY -> HEARTBEAT -> BEGIN -> PASS`. Uma falha publica a fase no serial/log
do guest e o codigo canonico no evento `FAIL`.

Cada execucao cria um diretorio novo em `build/test-results/<run-id>/`, com
`manifest.json`, `serial.log`, `qemu.stdout.log`, `qemu.stderr.log` e
`result.json`. O runner encerra o QEMU via QMP e aplica timeout de boot,
heartbeat e caso; nenhum autoteste pode aguardar indefinidamente.

O caso cobre as invariantes atuais de heap, PMM, memoria detalhada, paging
pronto e SLAB. As demais areas possuem casos independentes na camada TST4.2-
TST4.6.

## TST4.2-TST4.6: autotestes restantes do kernel

Os cinco casos seguintes tambem sao acionados somente por `RUN`, sem Shell,
bootloader ou conexao de rede real. Cada alvo executa exatamente uma iteracao
com timeout de boot, caso e heartbeat configuraveis por
`TST4_QEMU_BOOT_TIMEOUT`, `TST4_QEMU_CASE_TIMEOUT` e
`TST4_QEMU_HEARTBEAT_TIMEOUT` no `Makefile.local` ou na linha de comando.

```text
make test-tst4-qemu-paging-vma
make test-tst4-qemu-execution
make test-tst4-qemu-storage-vfs
make test-tst4-qemu-network
make test-tst4-qemu-platform
```

O caso `paging-vma` usa fixture ring 3 estatico, `mmap`/`munmap`, faults
lazy, erros negativos, cancelamento e reaping limitado. `execution` cobre
threads, scheduler, sinais, wait queues, workqueue e IPC. `storage-vfs`
exercita block/cache, file index, VFS, descritores, pipes, mounts, devfs,
procfs e sysfs sem escrita destrutiva. `network` usa somente backends locais
para buffers, sockets, rotas, protocolos, crypto e TLS. `platform` valida os
servicos, inventarios, ACPI/energia e dispositivos opcionais sem reset,
reboot ou poweroff.

A sequencia esperada e `READY -> HEARTBEAT -> BEGIN -> PASS`. Em caso de
falha, o evento identifica a fase e o codigo canonico. O runner nao repete o
caso automaticamente e preserva `manifest.json`, `serial.log`,
`qemu.stdout.log`, `qemu.stderr.log` e `result.json` em
`build/test-results/<run-id>/`.

Validacao final da TST4.2-TST4.6 em 2026-08-31: os cinco alvos passaram na
imagem gerada por `make clean` seguido de `make`; `make test-qemu-selftest`,
`make q3check` e `make catalog-test` tambem passaram. TST5 e a camada QEMU da
TST6 possuem procedimentos e evidencias proprios abaixo; TST7 continua
reservada a regressao continua.

## TST5: testes black-box e integracao completa no QEMU

O host injeta teclado somente pelo QMP. Os scripts de entrada ficam no
catalogo, passam por allowlist e sao registrados em `input.log`; o host nao
chama o Shell diretamente. Cada caso usa `--iterations 1`, imagem em
`-snapshot` e timeout limitado, sem retry automatico. Reboot e poweroff afetam
somente a instancia QEMU isolada.

Depois de alterar o kernel ou o runner, execute os gates nesta ordem:

```text
make test-qemu-selftest
make test-tst5-host
make q3check
make clean
make
make test-tst5-qemu-shell
make test-tst5-qemu-input
make test-tst5-qemu-apps
make test-tst5-qemu-processes
make test-tst5-qemu-storage
make test-tst5-qemu-network
make test-tst5-qemu-update-recovery
make test-tst5-qemu-reboot
make test-tst5-qemu-poweroff
make catalog-test
```

Os alvos de rede e atualizacao usam `--network none`; o caso de atualizacao
usa a fixture declarada no catalogo. Os nove casos sao:

```text
qemu:tst5:shell
qemu:tst5:input
qemu:tst5:apps
qemu:tst5:processes
qemu:tst5:storage
qemu:tst5:network
qemu:tst5:update-recovery
qemu:tst5:reboot
qemu:tst5:poweroff
```

O resultado esperado e `READY -> HEARTBEAT -> BEGIN -> PASS`. O runner
classifica infraestrutura, imagem, QEMU ou protocolo como `BLOCKED`, e falha
do guest, Shell ou aplicacao como `FAIL`. Em ambos os casos preserva
`manifest.json`, `serial.log`, `qemu.stdout.log`, `qemu.stderr.log`,
`input.log`, `qmp-events.log`, screenshots disponiveis e `result.json` em
`build/test-results/<run-id>/`. Reboot exige novo handshake apos `RESET`; o
poweroff exige `SHUTDOWN` ou a saida esperada da instancia.

## TST6: matriz, estresse e falhas controladas

A TST6 usa casos QEMU independentes, sempre em `-snapshot`, sem retry
automatico e sem alvo agregado. O runner aceita somente os perfis
`baseline`, `minimal`, `network`, `usb-hid`, `usb-storage`, `audio`, `display`
e `pci`. Os perfis de rede usam `user,model=e1000,restrict=on`, sem
encaminhamento para a rede externa; armazenamento USB usa fixture raw
somente leitura.

Depois de alterar o kernel, o runner ou o Makefile, execute:

```text
make test-qemu-selftest
make test-tst6-host
make q3check
make clean
make
```

Os limites globais do runner sao 1.000 iteracoes e 600 segundos. Os alvos de
estresse usam oito iteracoes e `TST6_QEMU_STRESS_DURATION` (padrao de 300
segundos); os alvos de matriz e falha usam uma iteracao. O heartbeat padrao dos
alvos TST6 e 60 segundos por caso para acomodar autotestes longos, sem remover
o timeout de caso de 120 segundos.

Execute cada alvo individualmente:

```text
make test-tst6-qemu-matrix-baseline
make test-tst6-qemu-matrix-minimal
make test-tst6-qemu-matrix-network
make test-tst6-qemu-matrix-usb-hid
make test-tst6-qemu-matrix-usb-storage
make test-tst6-qemu-matrix-audio
make test-tst6-qemu-matrix-display
make test-tst6-qemu-matrix-pci
make test-tst6-qemu-stress-kernel
make test-tst6-qemu-stress-storage
make test-tst6-qemu-stress-network
make test-tst6-qemu-stress-apps
make test-tst6-qemu-fault-memory
make test-tst6-qemu-fault-block
make test-tst6-qemu-fault-block-cache
make test-tst6-qemu-fault-package
make test-tst6-qemu-fault-update
make test-tst6-qemu-fault-network
make test-tst6-qemu-fault-process
make test-tst6-qemu-fault-recovery
make catalog-test
```

Cada execucao deve produzir `READY -> HEARTBEAT -> BEGIN -> PASS` ou um
resultado identificavel `FAIL`/`BLOCKED`. O runner preserva em
`build/test-results/<run-id>/` o `manifest.json`, `serial.log`,
`qemu.stdout.log`, `qemu.stderr.log`, `input.log`, `qmp-events.log`,
screenshots disponiveis e `result.json`, incluindo perfil, capacidades,
seed, fase, primeiro erro e iteracao. Hardware fisico nao e validado por
esses alvos e permanece `BLOCKED` ate existir equipamento e evidencia real.

## Driver UHCI: teste host-only

O caso `host:drivers:uhci` usa PCI, DMA, portas, IRQ, temporizador e
dispositivos USB falsos no processo host. Ele cobre inicializacao, reset,
enumeracao, descritores, transfers de controle e bulk, interrupt, timeout,
recuperacao, entradas invalidas e limpeza. Nenhuma instrucao de I/O
privilegiado ou hardware real e executada pelo teste.

```text
make test-uhci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de
funcoes e warnings tratados como erro. O relatorio fica em
`build/test-results/uhci-host/coverage.json`; uma execucao `PASS` deve ter
status `PASS`, nenhum endereco desconhecido ou ambiguo e deixar o controlador
falso, paginas DMA e filas restaurados.

## Driver EHCI: teste host-only

O caso `host:drivers:ehci` usa PCI, MMIO, DMA, temporizador e dispositivos USB
falsos no processo host. Ele cobre inicializacao, reset, enumeracao high-speed,
descritores, transfers de controle e bulk, interrupt, timeout, erro de qTD,
recuperacao, falhas de hardware e limpeza. Nenhuma instrucao de I/O
privilegiado ou hardware real e executada pelo teste.

```text
make test-ehci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de
funcoes e warnings tratados como erro. O relatorio fica em
`build/test-results/ehci-host/coverage.json`; uma execucao `PASS` deve ter
status `PASS`, nenhum endereco desconhecido ou ambiguo e deixar o controlador
falso, paginas DMA e filas restaurados. As APIs publicas declaradas em
`src/include/drivers/ehci.h` tambem entram no catalogo somente quando o
relatorio confirma as implementacoes chamadas pela fixture.

## Driver RTL8139: teste host-only

O caso `host:drivers:rtl8139` usa PCI, portas I/O, DMA, temporizador, IRQ e
bottom-half falsos no processo host. Ele cobre inicializacao, reset, leitura
de MAC, transmissao, recepcao, erros de ring, timeout, quiescencia,
recuperacao e limpeza. Nenhuma instrucao de I/O privilegiado ou hardware real
e executada pelo teste.

```text
make test-rtl8139-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de
funcoes e warnings tratados como erro. O relatorio fica em
`build/test-results/rtl8139-host/coverage.json`; uma execucao `PASS` deve ter
status `PASS`, nenhum endereco desconhecido ou ambiguo e deixar o controlador
falso, paginas DMA, filas e estado de IRQ restaurados.

## Driver de mouse: teste host-only

O caso `host:drivers:mouse` usa uma controladora PS/2, IRQ12, fila de entrada e
framebuffer VESA falsos no processo host. Ele cobre inicializacao com
Intellimouse, fallback de tres bytes, ACK invalido, timeout, eventos de
movimento, botoes e roda, coalescencia, cursor, configuracao, limites,
recuperacao e limpeza. Nenhuma instrucao de I/O privilegiado ou hardware real
e executada pelo teste.

```text
make test-mouse-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de funcoes
e warnings tratados como erro. O relatorio fica em
`build/test-results/mouse-host/coverage.json`; uma execucao `PASS` deve ter
status `PASS`, nenhum endereco desconhecido ou ambiguo e deixar as filas,
estado da controladora, cursor e framebuffer falsos restritos ao processo.

## Driver E1000: teste host-only

O caso `host:drivers:e1000` usa PCI, MMIO, reset, MAC, DMA, IRQ deferred,
descritores e frames Ethernet falsos no processo host. Ele cobre transmissao,
recepcao, fila RX, quiescencia, limites, timeouts, interrupcoes, falhas de
inicializacao e limpeza. Nenhum acesso MMIO, DMA ou IRQ real e executado pelo
teste.

```text
make test-e1000-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de funcoes
e warnings tratados como erro. O relatorio fica em
`build/test-results/e1000-host/coverage.json`; uma execucao `PASS` deve ter
status `PASS`, nenhum endereco desconhecido ou ambiguo e deixar o dispositivo,
descritores, buffers DMA, fila IRQ e MMIO falsos restritos ao processo.

## Driver AC97: teste host-only

O caso `host:drivers:ac97` usa PCI, portas I/O, codec, playback, IRQ e memoria
falsos no processo host. Ele cobre descoberta, reset, energia, sample rate,
volume, copia de amostras, limite de buffer, parada, limpeza e falhas de
inicializacao. Nenhuma porta I/O ou dispositivo de audio real e acessado pelo
teste.

```text
make test-ac97-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de funcoes
e warnings tratados como erro. O relatorio fica em
`build/test-results/ac97-host/coverage.json`; uma execucao `PASS` deve ter
status `PASS`, nenhum endereco desconhecido ou ambiguo e deixar o dispositivo,
buffer de playback, handler IRQ e portas falsas restritos ao processo.

## Driver RTL8811CU: teste host-only

O caso `host:drivers:rtl8811cu` usa dispositivos USB, filesystem, firmware e
interface Ethernet falsos no processo host. Ele cobre identificacao USB,
revisao, descritores, endpoints Bulk, cabecalho de firmware, estados,
callbacks Ethernet, scan, associacao aberta, limites de SSID e
indisponibilidade segura sem firmware e transporte confirmados. Nenhum
dispositivo USB, conexao de rede ou filesystem real e acessado pelo teste.

```text
make test-rtl8811cu-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O alvo exige compilador C nativo e `nm`, compila com instrumentacao de
funcoes e warnings tratados como erro. O relatorio fica em
`build/test-results/rtl8811cu-host/coverage.json`; uma execucao `PASS` deve
ter status `PASS`, nenhum endereco desconhecido ou ambiguo e manter o
firmware falso, interface, estruturas USB e estado global confinados ao
processo.

## TST7: regressao continua

O runner TST7 e independente do provedor de CI. O modo `quick` executa as
suítes host-only e os gates de qualidade previstos para alterações comuns. O
modo `full` faz `make clean`, recompila, executa a suíte rápida, valida o
catálogo e executa os 36 casos QEMU automatizados, cada um em processo
separado, com seed determinístico, timeout declarado e uma única tentativa.

```text
make test-tst7-host
make test-tst7-quick
make test-tst7-full
```

Os resultados ficam em `.tst7-results/<run-id>/`, que não é apagado por
`make clean`. Cada diretório contém `manifest.json`, `result.json`,
`coverage.json`, `summary.md`, `stdout.log`, `stderr.log` e
`artifact-index.json`; os artefatos individuais dos casos QEMU ficam em
`qemu/`. O diretório é ignorado pelo Git e deve ser publicado pelo CI como
artefato.

Um `full` com `--strict-coverage` também reprova enquanto houver superfícies
de software `PENDING`; isso não cria baseline automaticamente. Quando o
catálogo estrito estiver completo e o relatório não tiver falhas ou bloqueios,
revise os artefatos e aprove explicitamente a execução:

```text
python tools/tst7_regression_runner.py approve --run-id <id>
```

A aprovação exige `full` sem falha, timeout, bloqueio, caso não aprovado ou
erro de catálogo e grava `tests/baselines/tst7-approved.json` atomicamente.
Uma nova execução completa compara contratos, sequência de eventos, fase,
warnings normalizados, cobertura aprovada e duração. Um warning novo, perda de
cobertura ou regressão de duração maior que 20% e 5 segundos reprova. Diferença
de ambiente torna apenas a duração `NOT_COMPARABLE`; contratos continuam
comparáveis. Superfícies novas `PENDING` são reportadas sem mascarar seu
estado.

Depois da aprovação explícita, execute `make test-tst7-full` para validar a
regressão contra o baseline. A duração das etapas de preparação do host, como
`build`, não é comparada como duração de caso; os 36 casos QEMU continuam
comparáveis individualmente. Nunca substitua o baseline manualmente para
remover uma falha.

O runner continua após uma falha para coletar a matriz, mas cada caso QEMU é
executado uma única vez. Ausência de QEMU, imagem, fixture, baseline ou outra
dependência obrigatória é `BLOCKED`; falha do guest, timeout ou regressão é
`FAIL`. O hardware físico permanece fora da matriz e `BLOCKED`.

## Supervisor continuo TST7

O supervisor implementado em `tools/tst7_continuous_runner.py` possui os
modos `quick`, `full` e `soak`. O modo `soak` executa somente os quatro casos
de estresse TST6; `full` exige `--strict-coverage` e, portanto, permanece
reprovado enquanto houver superficie de software `PENDING`.

```text
python tools/tst7_continuous_runner.py start --mode quick --max-cycles 2 --interval 0
python tools/tst7_continuous_runner.py start --mode full --max-cycles 2 --interval 60
python tools/tst7_continuous_runner.py start --mode soak --max-cycles 2 --interval 0
python tools/tst7_continuous_runner.py start --mode full --forever --interval 60
```

`Ctrl+C` ou o arquivo definido por `--stop-file` solicita parada graciosa.
Cada ciclo preserva artefatos em `.tst7-results/continuous/` sem sobrescrever
execucoes anteriores. A entrada Linux equivalente e
`tools/tst7-continuous`; use `chmod +x tools/tst7-continuous` antes da primeira
execucao.

## Cobertura Assembly de interrupcoes

O caso QEMU `qemu:tst7:assembly` usa uma imagem de cobertura separada para
disparar `isr0`--`isr31`, `isr128` e `irq0`--`irq15`. Os stubs Assembly so
recebem instrumentacao quando `ZEPHYROS_TEST_COVERAGE` e definido no build de
cobertura; o build normal permanece sem o hook.

```text
make test-assembly-qemu ASSEMBLY_RUN_ID=tst7-assembly-<id>
```

O alvo executa uma unica iteracao, sem retry, com timeout por caso, e grava
`manifest.json`, `serial.log`, logs do QEMU, `qmp-events.log`, screenshot,
`result.json` e `coverage.json` em
`build-coverage/test-results/tst7-assembly/<id>/`. Uma execucao aprovada deve
produzir `READY -> HEARTBEAT -> BEGIN -> PASS`, confirmar as 49 entradas
Assembly e restaurar handlers, IRQs, ocorrencias e contadores da IDT. Use um
`ASSEMBLY_RUN_ID` novo em cada repeticao para preservar os artefatos anteriores.

## Comandos no Shell

Para orientar comandos do sistema, consultar primeiro `comandos.md` e os
handlers existentes. Preservar o fluxo cooperativo, o cancelamento indicado
no proprio comando e o retorno ao prompt. Nao criar uma tag de teste ou um
argumento que nao exista no dispatcher, no header publico ou no parser da
ferramenta correspondente.

## Identificadores dinamicos

- Copiar tags, IDs de dispositivo, volumes, particoes e endpoints exatamente
  da saida mais recente disponível.
- Nao abreviar, normalizar, trocar separadores ou completar identificadores.

## Registro de etapas

Toda implementacao, validacao ou conclusao de fase deve registrar data e hora
reais em `docs/qualidade/registro-validacoes.md`. O roadmap correspondente
mantem escopo, requisitos, checklists, pendencias e criterios; relatos
cronologicos, saidas e tentativas ficam no registro. Usar o formato:

```text
Concluida em: YYYY-MM-DD HH:MM (America/Sao_Paulo)
```

Nao estimar horarios historicos. Se implementacao e validacao ocorrerem em
momentos diferentes, registrar os dois eventos separadamente.

## Registro deste documento

Memoria operacional geral criada em: 2026-08-22 14:52 (America/Sao_Paulo).

## App loader host-only

O caso `test-app-loader-host` compila o `src/core/app_loader.c` real com
dependencias de paging, syscall, filesystem e processo falsas. A fixture
estatica cobre parser de argumentos, validacao de cabecalho e layout ZAPP,
inicializacao, execucao suspensa, foco, reap, cancelamento, leitura de arquivo
e falhas controladas. Execute-o com um compilador C nativo:

```text
make test-app-loader-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/app-loader-host/`; `coverage.json` deve terminar com
`PASS`, sem `unknown_addresses` ou `ambiguous_symbols`. O teste usa somente
buffers estaticos no processo host e nao substitui a validacao QEMU do fluxo
completo de aplicacoes.

## Taskbar host-only

O caso `test-taskbar-host` exercita a taskbar real em TUI e GUI com VESA,
display, desktop, mouse, timer e primitivas de desenho falsas. A fixture cobre
layouts, limites de botoes, menus, configuracao, cliques, relogio, selecao de
janelas e fallback sem hardware grafico real:

```text
make test-taskbar-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/taskbar-host/coverage.json`; ele deve terminar com `PASS`,
sem `unknown_addresses` ou `ambiguous_symbols`.

## Syscall host-only

O caso `test-syscall-host` exercita o dispatcher real em ring 0 e ring 3 com
dependencias falsas de IDT, TSS, paging, processo, IPC, sinais e App API. A
fixture cobre inicializacao, limites, copias protegidas, VFS, IPC, sinais e
rejeicoes de estado:

```text
make test-syscall-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/syscall-host/coverage.json`; ele deve terminar com `PASS`,
sem `unknown_addresses` ou `ambiguous_symbols`.
As constantes `APP_SYSCALL_*` sao verificadas diretamente pelos dispatches da
fixture e sua evidencia declarativa fica em
`tests/coverage/static/syscall-abi.json`, pois macros nao produzem enderecos
no relatorio dinamico.

## Threads e scheduler host-only

O caso `test-thread-host` compila o scheduler cooperativo real com pool de
threads, stacks e filas de espera estaticas. Ele exercita ciclo de vida,
selecao, yield, bloqueio, espera, cancelamento, desbloqueio, timeout,
indisponibilidade, limites e limpeza sem executar a troca de contexto Assembly
privilegiada. Execute-o com:

```text
make test-thread-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/thread-host/coverage.json`; ele deve terminar com `PASS`,
sem `unknown_addresses` ou `ambiguous_symbols`. A cobertura da troca de
contexto Assembly permanece separada e requer o caso freestanding/QEMU.
## Shell commands-core host-only

O alvo `test-shell-commands-core-host` compila uma fixture estática com os
handlers reais de `src/shell/shell_commands_core.c` e dependências falsas para
VFS, processos, loader, vídeo, áudio, energia e compressão. Ele executa os
caminhos válidos e negativos sem hardware, reset, halt ou armazenamento real:

```text
make test-shell-commands-core-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatório instrumentado fica em
`build/test-results/shell-commands-core-host/coverage.json`. O resultado deve
terminar em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`.

## Shell diagnostics helpers host-only

O alvo `test-shell-diagnostics-helpers-host` compila a fixture dos helpers
puros extraidos de `src/shell/shell_commands_diagnostics.c`. Ele valida parsers
de log, sinais, mouse e VMA, estados, cores, caminhos sysfs/proc e invariantes
de memoria com doubles estaticos, sem hardware ou armazenamento real:

```text
make test-shell-diagnostics-helpers-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/shell-diagnostics-helpers-host/coverage.json` e deve
terminar com `PASS`, `unknown_addresses=[]` e `ambiguous_symbols=[]`.

## Shell Wi-Fi host-only

O alvo `test-shell-commands-wifi-host` executa os handlers reais de
`src/shell/shell_commands_wifi.c` com inventario PCI/USB e radio simulados.
Exercita status, scan, conexao, argumentos invalidos, erros e indisponibilidade
sem hardware ou rede externa:

```text
make test-shell-commands-wifi-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio fica em `build/test-results/shell-wifi-host/coverage.json` e deve
terminar em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`.
Sincronize o catalogo somente apos a execucao real e valide com
`make catalog-test`.

## Update host-only

O alvo `test-update-host` compila `src/core/update.c` com doubles estaticos de
crypto e filesystem. A fixture valida os registros U3/U4, headers ZUPD,
paths, tabela de entradas, transacoes FAT12, slots, apply, rollback,
cancelamento, failpoints e recuperacao por boot sem escrever em armazenamento
real:

```text
make test-update-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-host/coverage.json` e deve terminar em `PASS`, com
`unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da execucao real,
sincronize o catalogo e valide com `make catalog-test`.
Depois de alterar o handler ou a fixture, sincronize o catálogo somente após a
execução real e valide com `make catalog-test`.

## Update runtime host-only

O alvo `test-update-runtime-host` compila `src/core/update_runtime.c` com
filesystem FAT12, crypto e estado legado simulados em buffers estaticos. A
fixture valida inicializacao e recuperacao de estado, manifestos ZUM2,
entradas ZUPD, planejamento, staging, backups, journal, commit, rollback,
cancelamento, failpoints, cache seletivo, comparacao de arquivos, motivos e
rejeicoes sem acessar armazenamento real:

```text
make test-update-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-runtime-host/coverage.json` e deve terminar em
`PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo e valide com `make catalog-test`.

## Update remote runtime host-only

O alvo `test-update-remote-runtime-host` compila o
`src/core/update_remote_runtime.c` com transporte HTTP, filesystem, crypto,
estado de atualizacao e processo simulados em buffers estaticos. A fixture
executa diretamente os helpers de serializacao, validacao, JSON, selecao de
origem, download, cache, abortamento e os contratos publicos de capacidade e
estado, sem rede, armazenamento ou reboot reais:

```text
make test-update-remote-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-remote-runtime-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## Update remoto host-only

O alvo `test-update-remote-host` compila o `src/core/update_remote.c` com
transporte HTTP, filesystem FAT12, crypto, processo e runtime simulados em
buffers estaticos. A fixture valida manifestos, registros redundantes, cache,
download, cancelamento, estados e contratos publicos sem rede ou
armazenamento reais:

```text
make test-update-remote-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-remote-host/coverage.json` e deve terminar em
`PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## App remoto host-only

O alvo `test-app-remote-host` compila `src/core/app_remote.c` com um
filesystem FAT12, transporte HTTP, crypto e motor de pacotes simulados em
buffers estaticos. A fixture valida catalogo ZAC1 autenticado, dependencias,
planejamento, preflight, cache alternado, aplicacao, procedencia,
cancelamento, failpoint de publicacao, recuperacao e traducao de motivos do
motor de pacotes, sem rede ou armazenamento reais:

```text
make test-app-remote-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/app-remote-host/coverage.json` e deve terminar em `PASS`,
com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da execucao real,
sincronize o catalogo, renderize a visao e valide com `make catalog-test`.

## Update system slots host-only

O alvo `test-update-system-slots-host` compila o
`src/core/update_system_slots.c` com filesystem, crypto, armazenamento e
volume de sistema simulados em buffers estaticos. A fixture valida
serializacao de estado e journal, redundancia, recuperacao, paths, limites e
contratos publicos sem armazenamento real, reboot ou hardware:

```text
make test-update-system-slots-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-system-slots-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## Update remote system host-only

O alvo `test-update-remote-system-host` compila o
`src/core/update_remote_system.c` com filesystem, volume, crypto,
armazenamento e transporte simulados em buffers estaticos. A fixture valida
serializacao do controle, cache redundante, hash, verificacao, transferencia
transacional, limpeza, estados e contratos publicos sem rede ou armazenamento
real:

```text
make test-update-remote-system-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-remote-system-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## Update remote GitHub host-only

O alvo `test-update-remote-github-host` compila o
`src/core/update_remote_github.c` com respostas JSON, HTTP, crypto e cancelamento
simulados em buffers estaticos. A fixture valida parser JSON, assets,
duplicidades, limites, URLs allowlisted, fingerprints, espera, cancelamento,
status HTTP e os contratos publicos de consulta sem rede externa:

```text
make test-update-remote-github-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-remote-github-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## Update remote release host-only

O alvo `test-update-remote-release-host` compila o
`src/core/update_remote_release.c` com descritor JSON, HTTP, crypto, canal
remoto e consulta GitHub simulados em buffers estaticos. A fixture valida
version lock, tags, assets, hashes, URLs, truncamento, status HTTP,
cancelamento, selecao por tag, pre-condicoes e contrato de download sem rede
externa:

```text
make test-update-remote-release-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-remote-release-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## Update system host-only

O alvo `test-update-system-host` compila o
`src/core/update_system.c` com crypto, HTTP, filesystem, processo e GitHub
simulados em buffers estaticos. A fixture valida headers e componentes ZSYS,
compatibilidade, hashes, assinatura, transporte, limites e contratos publicos
sem armazenamento ou rede externa:

```text
make test-update-system-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O relatorio instrumentado fica em
`build/test-results/update-system-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. Depois da
execucao real, sincronize o catalogo, renderize a visao e valide com
`make catalog-test`.

## Spinlock host-only

O alvo `test-spinlock-host` executa uma fixture nativa que inicializa, adquire
e libera um `spinlock_t`, verificando o estado livre e adquirido. O teste não
cria threads nem depende de hardware; seu vínculo cobre somente as três
operações inline declaradas em `src/include/core/spinlock.h`:

```text
make test-spinlock-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso automatizado é `host:core:spinlock`. A evidência declarativa fica em
`tests/coverage/registry.json` porque funções `static inline` não formam um
símbolo externo estável no relatório instrumentado.

## Shell: comandos de armazenamento host-only

O alvo `test-shell-commands-storage-host` executa os dispatchers reais de
`index` e `search` com doubles estáticos de índice, bloco, cache, storage e
VFS. A fixture cobre entradas nulas, desconhecidas, extras e vazias, além do
caminho de dependência indisponível, sem hardware ou armazenamento real:

```text
make test-shell-commands-storage-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso automatizado é `host:shell:commands-storage`. O relatório fica em
`build/test-results/shell-commands-storage-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. O contrato
observável usa `ERR_UNAVAILABLE=9`; após uma execução real, sincronize o
catálogo e valide com `make catalog-test`.

O mesmo caso também exercita `blkstat`, `cachestat`, `cache`, `sync` e
`storage`, incluindo estados válidos e indisponíveis, formatos ATA/USB,
volumes, diagnóstico de IDs, limites, busca com resultados e callbacks do job
cooperativo do índice. A fixture usa somente doubles estáticos no processo
host e não acessa hardware ou armazenamento real.

## Shell: comandos diagnósticos host-only

O alvo `test-shell-diagnostics-host` executa os dispatchers reais de `pwd`,
`cd`, `mouse`, `log`, `timer`, `clock`, `irqstat`, `wait`, `wqinfo`, `workq`,
`tls`, `vfs`, `mount`, `devcheck`, `devices`, `device-info`, `usb`, `slabinfo`
e `slabtest` com VFS,
mouse, vídeo, timer, RTC, IRQ, IDT, wait, workqueue, TLS, mounts, descritores,
devfs, inventário de dispositivos, USB, HID, MSC e SLAB falsos. A fixture cobre
caminhos válidos, argumentos extras, limites, estados indisponíveis e
preservação da configuração quando uma preferência é recusada:

```text
make test-shell-diagnostics-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe
```

O caso automatizado é `host:shell:diagnostics`. O relatório instrumentado fica
em `build/test-results/shell-diagnostics-host/coverage.json` e deve terminar
em `PASS`, com `unknown_addresses=[]` e `ambiguous_symbols=[]`. O teste não
acessa hardware, VFS real ou persistência. Os caminhos de `log` validam
status, histórico, limpeza, níveis, código de erro, autoteste e entradas
inválidas. Os caminhos de `timer` e `clock` cobrem status, listagem,
autoteste, indisponibilidade, fonte RTC, valores monotônicos, datas e o estado
da fila Bottom-Half por linha de IRQ, filas de espera, waiters, workqueue,
políticas TLS, mounts, descritores, devfs, inventários de dispositivos, USB,
HID, MSC, listagens, autotestes e estados de indisponibilidade, sem tocar em
hardware ou armazenamento real.

A mesma fixture tambem executa `cpu usage`, `pagefault`, `vmamap` e
`schedcheck`. Ela usa estatisticas de scheduler, processos e VMAs falsos para
validar percentuais, estatisticas de page fault, mapas de codigo/stack,
processos inexistentes, processos nao-usuario, VMA indisponivel, argumentos
invalidos e falhas de invariantes. O relatorio deve continuar com
`unknown_addresses=[]` e `ambiguous_symbols=[]`; a execucao nao acessa
hardware, memoria do kernel ou processos reais.
