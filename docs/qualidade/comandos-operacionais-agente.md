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
make test-ethernet-host
make test-tcp-host
make test-tls-host
```

O caso de scheduling cobre `wait`, `workqueue` e `irq_deferred` em processo
host com `ZEPHYROS_HOST_TEST=1`; o caminho freestanding continua sendo usado
no build do kernel. Cada alvo preserva `manifest.json`, `result.json`,
`coverage.json`, `coverage-symbols.json`, `stdout.log` e `stderr.log` em
`build/test-results/<suite>/`.

O caso `test-package-host` cobre contratos puros do servico de pacotes,
incluindo versoes, motivos canonicos, estados indisponiveis e failpoints sem
escrever no armazenamento.

O caso `test-state-host` cobre recovery e a cadeia de notificadores de energia
com callbacks estaticos, incluindo estados opcionais, duplicatas, capacidade,
falhas canonicas e timeout. Seu relatorio instrumentado fica em
`build/test-results/state-host/coverage.json`.

Os casos `test-device-manager-host`, `test-app-api-host` e
`test-app-catalog-host` cobrem, respectivamente, inventario de dispositivos
com backends simulados, a fachada de aplicativos e o catalogo da App Store.
Eles exercitam estados indisponiveis, limites, erros canonicos e limpeza em
processos host instrumentados, sem hardware nem armazenamento real. Os
relatorios ficam em `build/test-results/device-manager-host/`,
`build/test-results/app-api-host/` e `build/test-results/app-catalog-host/`.

O caso `test-input-host` valida as filas estaticas de teclado e ponteiro,
coalescencia, saturacao de deltas, filas cheias, despacho alternado e erro de
consumidor. O relatorio fica em `build/test-results/input-host/`.

Os casos `test-power-host` e `test-network-manager-host` exercitam, com
fixtures estaticos, os estados ACPI e a limpeza de energia apos falhas, alem
do inventario PCI, drivers ausentes, estado offline e recusas de operacoes que
exigem uma interface ativa. Os relatorios instrumentados ficam em
`build/test-results/power-host/` e `build/test-results/network-manager-host/`.

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
inventario. Os relatorios ficam em `build/test-results/storage-host/` e
`build/test-results/block-host/`.

O caso `test-fat12-host` exercita o driver legado sobre uma imagem FAT12
estatica, incluindo leitura, paths de subdiretorio, metadados, operacoes
atomicas, streaming, cancelamento e erros de nome/tamanho. O relatorio fica
em `build/test-results/fat12-host/`.

O caso `test-fat32-host` exercita o driver FAT32 sobre uma imagem estatica com
cadeia de clusters, leitura, paths, metadados, criacao, escrita, remocao e
limites. O caso `test-vfs-host` valida o nucleo de descritores e I/O da VFS,
incluindo arquivos regulares, dispositivos, pipes, sockets, poll/select,
quiescencia e invariantes. Os relatorios ficam em
`build/test-results/fat32-host/` e `build/test-results/vfs-host/`.

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
oferta, lease, renovacao, rebinding, expiracao, NAK, mensagens invalidas e
falhas de transporte; seu relatorio fica em `build/test-results/dhcp-host/`.
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
O caso `test-vma-host` usa processo ring 3, paging, PMM e VFS falsos para
exercitar VMAs fixas e anonimas, materializacao lazy, page faults validos e
invalidos, `mmap`, `munmap`, limites, estatisticas e limpeza; seu relatorio fica
em `build/test-results/vma-host/`.
O caso `test-paging-host` exercita diretamente o diretorio de paginas, tabelas,
mapas de kernel e usuario, framebuffer, copia entre espacos, materializacao
lazy, limites, overflow, paginas ausentes e limpeza. A fixture usa PMM, VESA e
processo falsos com buffers estaticos, sem instrucoes privilegiadas ou hardware;
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
-Werror`, executa os testes Python formais de packager/updater e roda os
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
