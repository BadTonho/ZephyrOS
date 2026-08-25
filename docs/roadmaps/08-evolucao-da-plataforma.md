# Roadmap 08 - Evolucao da Plataforma

## Objetivo

Evoluir as capacidades de entrada, armazenamento e conectividade do ZephyrOS
sem reabrir contratos ja validados de boot, filesystem, rede cabeada ou
atualizacao segura.

Esta frente sucede o Roadmap 07. A escala e o layout da interface pertencem a
MV0 no roteiro visual; este documento cobre preferencias de mouse, volumes,
indice, USB, releases por tags e radios sem fio. As etapas menores so avancam
quando suas dependencias tecnicas estiverem validadas.

O histórico de implementações, correções e validações concluídas está em
[`registro-validacoes.md`](../qualidade/registro-validacoes.md).

## Base ja validada

- [x] Mouse PS/2, cursor grafico, roda e roteamento global de eventos.
- [x] VESA com framebuffer e interfaces Simple/Classic; troca de modo em
  runtime permanece desabilitada.
- [x] Settings, Desktop, Taskbar, Window Manager e Explorer nos dois modos.
- [x] ATA PIO, FAT12, FAT32 e interface unificada de filesystem.
- [x] E1000, RTL8139, IPv4, DHCP, DNS, TCP, HTTP e operacao multi-NIC.
- [x] Atualizacao ZUPD v1: verificacao assinada, aplicacao recuperavel,
  rollback, manifesto ZUM1 e cache remoto HTTP opcional.

Essas capacidades continuam sendo a fonte de verdade. O Roadmap 08 acrescenta
camadas de configuracao, descoberta, montagem e distribuicao sobre elas.

## Decisoes de produto

- Simple continua como fallback completo; Classic e Shell devem permanecer
  operacionais durante qualquer falha de mouse, volume, USB ou radio.
- Nenhuma fase altera `src/boot/boot.asm`, stage2 ou escreve setores crus sem
  uma etapa transacional especificamente aprovada.
- Preferencias de mouse nao dependem de USB: elas usam o driver PS/2 atual.
- A primeira entrega de particoes e somente-leitura. Criar, formatar,
  redimensionar e apagar particoes sao trabalhos posteriores.
- A primeira busca indexa nomes e caminhos, nao conteudo de arquivos.
- USB legado continua usando UHCI para HID/MSC; a EP7.1B acrescenta EHCI
  isolado para o transporte high-speed do Wi-Fi. Hubs, xHCI e hot-plug seguem
  fora do escopo.
- Tags do GitHub selecionam a release, mas nao sao raiz de confianca: somente
  ZUM1 e ZUPD assinados podem ser aceitos pelo sistema.
- Wi-Fi e Bluetooth sao fases independentes e exigem chipset, transporte e
  matriz de validacao definidos antes de inicializar hardware.
- Toda capacidade executavel tera comandos Shell, logs, erros controlados e
  regressao Simple/Classic correspondente.

## Ordem de dependencia

1. Preferencias de mouse sobre o driver PS/2 existente.
2. Volumes ATA e montagem somente-leitura.
3. Indice em RAM sobre os volumes montados.
4. USB fatiado: inventario, UHCI/controle, MSC/Bulk e HID/Interrupt.
5. Publicacao e verificacao de Releases no host, com tag auxiliar opcional.
6. EP6.0: selecao explicita de Release por tag, ainda protegida por ZUM1/ZUPD.
7. EP6.1: TLS, certificados e estrategia de tempo.
8. EP6.2/6.3: canal GitHub, recuperacao e matriz de validacao.
9. Wi-Fi para um chipset e transporte escolhidos.
10. Bluetooth HCI para um controlador e transporte escolhidos, quando houver
    hardware real disponivel para validacao.
11. EP9: imagem do sistema, slots de boot e recuperacao pos-reboot.

## EP1 - Preferencias de mouse

**Estado:** implementada e validada pelo usuario.

### Implementacao

- [x] Criar configuracao central de sensibilidade, aceleracao opcional e
  botao principal, com valores padrao seguros.
- [x] Remapear o botao principal antes do despacho para Desktop, Taskbar, WM
  e apps; manter o estado bruto observavel para diagnostico.
- [x] Garantir que o escalonamento de movimento respeite limites de tela e nao
  descarte pacotes da fila PS/2.
- [x] Adicionar `mouse speed <1-10>` e `mouse primary left|right`; `mouse`
  exibira configuracao efetiva, estado bruto e falhas do driver.
- [x] Expor os controles equivalentes no Settings Simple/Classic e registrar
  toda recusa de configuracao com `LOG_ERROR`.

Persistencia de preferencias fica fora da EP1. Enquanto nao houver uma area de
configuracao recuperavel, os valores permanecem em RAM e isso e informado ao
usuario.

### Criterio de saida

Mouse, clique, arrasto, roda, teclado, cursor e Shell continuam utilizaveis.
Valores invalidos e hardware ausente falham controladamente, sem bloquear o
fallback Simple.

### Matriz QEMU aprovada

1. Os padroes `3`, `off` e `left`, as velocidades `1` e `10` e a aceleracao
   `on/off` foram aprovados; valores invalidos preservaram o estado anterior.
2. O botao direito como principal preservou clique, arraste, resize, roda,
   cursor e despacho para Desktop, Taskbar, WM, Settings e aplicativos.
3. `mouse` distinguiu estado bruto/efetivo, configuracao, roda, ultimo erro e
   pacotes descartados; as preferencias permaneceram somente em RAM.
4. Settings Simple e Classic sincronizaram e aplicaram os tres controles.
5. A primeira execucao sem resposta valida do mouse preservou teclado, Shell,
   boot e regressao; a transacao PS/2 protegida foi revalidada com sucesso.
6. `health summary`, `memcheck` e `regcheck full` foram executados;
   MemCheck e RegCheck terminaram em `OK`.

## EP2 - Volumes ATA e montagem de particoes

**Estado:** implementada e validada pelo usuario.

### Implementacao

- [x] Descobrir MBR somente em leitura e inventariar discos ATA, particoes e
  volumes por identificadores estaveis.
- [x] Criar uma abstracao de volume acima de ATA sem alterar a API FAT atual
  ate os chamadores passarem explicitamente o volume alvo.
- [x] Validar limites do volume e BPB antes de montar FAT12/FAT32 sob demanda.
- [x] Limitar memoria, numero de volumes e operacoes concorrentes; toda falha
  de leitura, formato ou montagem deve ter log por volume.
- [x] Adicionar `storage list`, `storage info <id>`, `storage mount <id>` e
  `storage unmount <id>`; comandos de consulta nao gravam no disco.
- [x] Mostrar volumes montados no Explorer e Settings, mantendo o volume de
  boot como fallback quando nenhum volume adicional for valido.
- [x] Documentar uma fase futura, separada, para GPT, formatacao e alteracao
  da tabela de particoes com staging, journal e recuperacao.

### Criterio de saida

Listar e montar um volume ATA valido nao modifica setores. Particoes ausentes,
corrompidas ou com filesystem desconhecido permanecem isoladas, sem afetar o
boot, Shell, filesystem atual ou outros volumes montados.

### Resultados da validacao

1. `make q3check`, `make clean && make`, `make storage-fixtures-test` e
   `make storage-fixtures` foram aprovados.
2. `run-storage` detectou `ata0`-`ata3`, dez volumes e nenhum MBR fantasma
   no boot. FAT12/FAT32 montaram e navegaram; BPB/MBR corrompidos e tipo
   desconhecido permaneceram isolados.
3. Mount repetido, unmount repetido/do boot, IDs e argumentos invalidos e o
   limite de quatro montagens foram recusados sem alterar o estado.
4. Explorer Classic navegou e visualizou as fixtures, bloqueou mutacoes
   somente-leitura e retornou a “Este Computador” ao detectar unmount por
   geracao. Settings sincronizou discos e montagens.
5. Os discos auxiliares terminaram com zero escritas. Os SHA-256 permaneceram
   `4727e93495809cfae2d746332454f1c23e0faeb37924c9fd16e00fc45c6e4078`,
   `4f0f4f98f9a5988dedaec577de2808648948bb1f9c12bf6394bf7da1be45d469` e
   `068d2e5b814af6efba8c4daf88857a915311ea099bebc277c8dec171fdc09abf`.
6. `make run` preservou o boot com um disco/volume/montagem; o fallback
   Simple, `devices`, `health summary`, `memcheck` e `regcheck full`
   passaram. MemCheck e RegCheck terminaram em `OK`.

## EP3 - Indice e pesquisa de arquivos

**Estado:** implementada e validada pelo usuario.

### Implementacao

- [x] Definir o contrato inicial do indice: volume, caminho, nome, tipo e
  tamanho; pesquisa de conteudo e metadados ricos ficam fora desta etapa.
- [x] Construir o indice cooperativamente, com orcamento por tick,
  cancelamento, progresso e limites explicitos de memoria e entradas.
- [x] Associar entradas ao ID e a geracao do volume para detectar montagem,
  desmontagem ou alteracao externa que deixe resultados desatualizados.
- [x] Atualizar ou invalidar entradas nas operacoes existentes de criar,
  renomear, copiar, mover e excluir do filesystem.
- [x] Adicionar `index status`, `index rebuild`, `index cancel`, `index check`
  e `search <termo>` no Shell;
  a busca informa resultado parcial, volume ausente ou indice desatualizado.
- [x] Integrar uma tela de pesquisa no Explorer sem bloquear desenho, entrada,
  mouse, rede ou Shell.
- [x] Manter a primeira versao em RAM. Persistencia so entra depois de definir
  gravacao recuperavel sobre a camada de volumes da EP2.

### Criterio de saida

Uma busca limitada encontra caminhos corretos sem travar a interface. Indice
corrompido, cancelado ou sem memoria gera log e pode ser reconstruido, sem
impedir navegacao normal do Explorer ou uso do filesystem.

### Validacao

1. `make q3check`, build limpo e execucao no QEMU passaram apos a revisao dos
   warnings encontrados durante a integracao.
2. As fixtures FAT12/FAT32 foram montadas em `run-storage`; buscas globais
   encontraram raiz, subdiretorios e nomes repetidos. Mount/unmount atualizou
   fontes e resultados sem bloquear Shell ou Explorer.
3. `storage-fixtures-verify` preservou os tres SHA-256 canonicos da EP2, sem
   escrita nos discos adicionais.
4. Rebuild, cancelamento, publicacao posterior e `index check` passaram. Os
   autotestes cobriram matching, limites, cancelamento e corrupcao.
5. Criacao, renomeacao, copia, movimentacao e exclusao no volume de boot
   dispararam rebuild automatico e produziram resultados atuais no Shell e no
   Explorer Classic. A validacao tambem corrigiu renomeacao FAT12 na propria
   entrada, exclusao atomica da raiz e filtragem do marcador apagado `0xE5`.
6. Pesquisa por teclado e mouse, abertura de resultados, roda, retorno com
   Esc e atualizacao da tela Classic passaram. O smoke test Simple preservou
   video, teclado, Shell, busca e retorno ao Classic.
7. `memcheck`, `index check` e `regcheck full` terminaram em `OK`.

## EP4 - USB incremental

### EP4.1 - Inventario e contrato de controladores

**Estado:** implementada e validada pelo usuario.

O escopo desta entrega e somente a descoberta por PCI. O `usb_manager` le o
snapshot ja criado pelo PCI, copia BDF, IRQ e BARs e nao escreve configuracao,
nao acessa BARs e nao inicializa DMA, IRQ, portas ou transferencias USB. O
limite e de oito controladores, com IDs estaveis no formato
`usb-pci-BB:DD.F`. UHCI (`ProgIF 0x00`) e EHCI (`ProgIF 0x20`) sao
classificados; outros controladores permanecem inventariados como fora do
escopo.

- [x] Detectar e inventariar controladores USB no PCI, distinguindo UHCI e
  EHCI, sem habilitar DMA, IRQ ou transferencias durante a descoberta.
- [x] Adicionar `usb status`, `usb list` e `usb device <id>` para consultas
  somente-leitura e integrar o componente USB ao `health`.
- [x] Definir IDs estaveis, estados `READY`, `DEGRADED` e `DISABLED`, limites
  de dispositivos e motivos de erro antes de inicializar um controlador.

`device-scan` atualiza o inventario USB de forma idempotente. `regcheck full`
verifica limites, classificacao, IDs, dados copiados do PCI, coerencia com o
Recovery e a ausencia de inicializacao de DMA, IRQ ou transferencias. O alvo
`run` permanece inalterado; `run-usb` acrescenta um controlador UHCI PIIX3 ao
QEMU para a validacao manual.

### Validacao concluida da EP4.1

1. No `run` padrao, USB reportou `DISABLED`, sem controladores; entradas
   inexistentes e comandos invalidos falharam de forma controlada.
2. No `run-usb`, UHCI foi identificado como `usb-pci-00:04.0`, com dados PCI,
   ID estavel, `device-scan` concluido e DMA, IRQ e transferencias indisponiveis.
3. Com `QEMU_USB_ARGS` alternativo, EHCI foi identificado com inventario
   completo e sem inicializacao de transferencias.
4. `health summary`, `regcheck full` e `memcheck` terminaram em `OK` nos
   cenarios validados.

### EP4.2 - UHCI, portas e transferencias de controle

**Estado:** implementada e validada pelo usuario.

- [x] Inicializar apenas UHCI, com portas I/O, frame list, queue head, pool
  limitado de TDs, buffers DMA alinhados, timeout absoluto, IRQ compartilhada
  e recuperacao controlada; EHCI continua fora do escopo ativo.
- [x] Detectar velocidade, resetar portas raiz, atribuir endereco e ler os
  descritores Device e Configuration, validando uma Configuration, uma
  Interface e seus Endpoints.
- [x] Executar transferencias de controle e `SET_CONFIGURATION`, mantendo
  portas com falha isoladas e sem hubs, hot-plug, strings, HID, Bulk, MSC ou
  qualquer driver de classe.
- [x] Adicionar `usb ports` e `usb devices`, polling UHCI nos loops normal e
  fallback, IDs de sessao `usb-dev-BB:DD.F-pN-aN` e invariantes no
  `regcheck full`.

### Validacao concluida da EP4.2

1. `make q3check`, `make clean && make`, `make run` e `make run-usb`.
2. No `run`, `usb status`, `usb ports`, `usb devices`, `health summary`,
   `regcheck full` e `memcheck`: USB `DISABLED`, sem portas ativas e PS/2
   preservado.
3. No `run-usb`, `usb-kbd` deve produzir uma porta `CONFIGURED`, um ID de
   sessao, descritores Device/Configuration validos e `SET_CONFIGURATION`,
   sem atividade HID.
4. Sem dispositivo, as portas ficam `EMPTY`, zero dispositivos e UHCI pronto.
   Com `QEMU_USB_ARGS="-device usb-ehci,id=usb"`, EHCI permanece apenas
   inventariado, sem BAR, DMA, IRQ ou transferencia, com USB `DEGRADED`.

Os quatro cenarios foram executados no QEMU. O `usb-kbd` produziu o dispositivo
`usb-dev-00:04.0-p1-a1`, com a porta 1 `CONFIGURED`, endereco USB 1 e os
descritores Device/Configuration validos. O cenario sem dispositivo manteve as
duas portas `EMPTY`, zero dispositivos e UHCI `READY`. O `run` padrao manteve
USB `DISABLED`, Shell, teclado PS/2 e mouse funcionais, com `regcheck full` e
`memcheck` em `OK`. O fixture EHCI foi somente inventariado como `DEGRADED`,
com DMA, IRQ, portas e transferencias indisponiveis, conforme o escopo.

### EP4.3 - Bulk e USB Mass Storage somente-leitura

**Estado:** implementada e validada pelo usuario.

Nesta implementacao, UHCI fornece Bulk sincrono com TDs fragmentados por
`wMaxPacketSize`, toggles por endpoint, timeout, buffers DMA fixos e
recuperacao Mass Storage Reset/CLEAR_FEATURE com uma unica tentativa. O BOT
valida CBW/CSW e o SCSI cobre INQUIRY, TEST UNIT READY, READ CAPACITY(10) e
READ(10), sempre em LUN 0 e setores de 512 bytes.

`block_device_t` publica ATA e USB MSC no mesmo inventario. `storage_refresh()`
reconcilia os dispositivos apos `device-scan`, `usb storage` exibe os
metadados do MSC e `run-usb-msc` conecta `storage-valid.img` somente-leitura.
`run-usb` permanece sem MSC para preservar a regressao da EP4.2.

Hubs, hot-plug, HID, EHCI, multiplos LUNs, `READ CAPACITY(16)` e escrita USB
continuam fora do escopo.

- [x] Implementar transferencias Bulk e o transporte Bulk-Only Transport (BOT).
- [x] Implementar o subconjunto SCSI necessario: Inquiry, Test Unit Ready,
  Read Capacity e Read10, sempre sem escrita no dispositivo USB.
- [x] Registrar o dispositivo MSC como provedor da camada de bloco (`block_device_t`),
  permitindo sua montagem transparente de volumes FAT sem acoplamento direto
  entre o driver USB e o sistema de arquivos.
- [x] Manter disco ATA e volume de boot como fallbacks operacionais prioritários.

### Resultados da validação EP4.3

- [x] `make run` preservou ATA, volume de boot e Shell.
- [x] `make run-usb QEMU_USB_DEVICE_ARGS=` deixou UHCI/Bulk prontos sem
  registrar MSC.
- [x] `make run-usb-msc QEMU_USB_DEVICE_ARGS=` enumerou o MSC, publicou o
  disco `usb-ms-00:04.0-p1-a1-l0` e detectou quatro partições FAT.
- [x] Montagem e desmontagem de `usb-ms-00:04.0-p1-a1-l0p1` e
  `usb-ms-00:04.0-p1-a1-l0p4` funcionaram em somente-leitura; `VALID.ZPK`
  retornou o cabeçalho `ZPKG`.
- [x] Dois `device-scan` permaneceram idempotentes, com `discos=2` e
  `volumes=5`; `index check`, `health summary`, `memcheck` e `regcheck full`
  terminaram em `OK`.
- [x] O MSC terminou com escritas `0`, resets `0`, erro `0` e o hash SHA-256
  de `build/storage-valid.img` permaneceu
  `4727E93495809CFAE2D746332454F1C23E0FAEB37924C9FD16E00FC45C6E4078`.

### EP4.4 - Interrupt e USB HID

**Estado:** implementada e validada pelo usuário.

- [x] Implementar transferências Interrupt através do despachante assíncrono,
  sem bloqueios por espera ocupada na CPU, com QH/TD/buffer persistentes,
  deadlines, toggle, cancelamento e proteção contra callback atrasado.
- [x] Adicionar teclado e mouse HID somente como fontes adicionais de eventos de
  entrada, roteando para o `input core` comum sem alterar o foco nem substituir
  os drivers PS/2 existentes.
- [x] Adicionar diagnósticos `usb hid status` e `usb hid check`, estado HID em
  `health` e `regcheck full`, e o alvo `run-usb-hid`.
- [x] Rejeitar rollover, tamanhos inválidos e relatórios malformados sem travar
  o kernel; cancelar requisições HID durante refresh ou ausência do dispositivo.
- [x] Validar desconexão, pacote inválido, timeout e dispositivo ausente sem
  travar kernel, Shell ou interfaces gráficas.

### Matriz de validação pendente da EP4.4

- [x] `make q3check` e `make clean && make`.
- [x] `make run` sem USB, preservando PS/2; `usb hid status` mostrou zero
  registros, `usb hid check` retornou `OK`, `health` e `regcheck full` ficaram
  operacionais.
- [x] `make run-usb-hid` com teclado e mouse USB no Shell e no Desktop Classic.
- [x] F12/Esc cancelando `net check`, `ping` e HTTP; modificadores, Enter,
  Backspace, setas, teclas de função, clique, arrasto e roda.
- [x] USB ausente, endpoint inválido, relatório malformado, timeout,
  cancelamento e refresh sem callback tardio ou evento de dispositivo removido.
- [x] Regressão de MSC, ATA, rede, `health summary`, `memcheck` e
  `regcheck full`.

### Criterio de saida

Cada subetapa preserva o funcionamento de teclado e mouse PS/2. UHCI ausente,
dispositivo malformado ou erro de comunicacao produz `LOG_ERROR` e componente
degradado, sem suporte ficticio a EHCI, hubs ou HID. O MSC da EP4.3 fica
restrito ao contrato BOT/SCSI somente-leitura descrito acima.

## EP5 - Releases oficiais e verificacao no host

**Estado:** implementada e validada pelo usuario.

### Implementacao

- [x] Definir uma politica host em que a Release agrupa ZUPD, manifesto ZUM1,
  hashes e descritor coerentes; a tag e somente um marcador auxiliar opcional.
- [x] Manter a trava oficial de versao e epoch nos campos assinados do ZUPD e
  ZUM1, sem deriva-la do identificador, titulo ou tag da Release.
- [x] Estender a ferramenta host com `release-build` e `release-check` para
  verificar offline origem Git, assets, versao minima, hashes e assinaturas.
- [x] Tratar GitHub apenas como origem de distribuicao: o kernel aceita a
  release somente depois de validar ZUM1 e ZUPD pelas chaves ja confiaveis.
- [x] Cobrir fixtures deterministicas de Release com e sem tag, asset ausente,
  manifesto adulterado, pacote invalido, trava, commit e tag divergentes.

### Resultados da validacao

- [x] `make update-test` terminou com `Updater selftest: OK`.
- [x] `make q3check` aprovou whitespace, protecao do boot, funcoes falhaveis,
  contratos publicos, metricas e confianca ZUPD/AS5.
- [x] A verificacao da Terminus Font confirmou fontes e dados gerados em `OK`.
- [x] `make clean` e o build completo terminaram sem erros.

Esta etapa nao cria um comando novo no kernel nem muda o transporte U5. Ela
torna a publicacao reproduzivel e valida que a Release ou sua tag nunca
substituem a assinatura do manifesto ou do pacote.

### Criterio de saida

Uma Release gera artefatos verificaveis e coerentes antes da publicacao.
Assets, origem, trava ou tag opcional inconsistentes falham no host sem gerar
uma Release utilizavel. O criterio foi aprovado pela suite host e pelo gate de
qualidade; esta etapa nao exige QEMU.

## EP6 - Selecao por tag, TLS e canal GitHub opcional

**Estado:** EP6.0 implementada e validada pelo usuario.

Esta frente implementa a ideia de escolher uma Release do sistema por uma tag
especifica, sem transformar a tag em raiz de confianca ou em versao numerica.
A versao oficial continua vindo dos campos assinados do ZUPD/ZUM1. Nao existe
`latest`, comparacao numerica de tags ou atualizacao automatica no boot.

### EP6.0 - Contrato de selecao por tag

- [x] Definir `update github check --tag <tag>` e
  `update github fetch --tag <tag> [--confirm]` como operacoes explicitamente
  opt-in; a ausencia de tag, tag inexistente ou Release sem asset falha sem
  alterar o cache.
- [x] Resolver a tag para uma Release e seus assets usando um canal de origem
  configuravel, exigindo um manifesto ZUM1 e um pacote ZUPD assinados; a
  metadata da Release, o titulo e a tag somente selecionam o candidato.
- [x] Reutilizar o cache, o download cooperativo e a aplicacao confirmada da
  U5: baixar nao instala, e `update apply` continua separado e exige reboot.
- [x] Exibir antes da confirmacao a tag solicitada, a Release encontrada, a
  versao/epoch assinados, o hash e o estado do cache; nunca escolher a maior
  tag automaticamente.

A matriz EP6.0 foi executada e concluida pelo usuario, cobrindo duas tags
validas, tag ausente/invalida/inexistente,
argumentos extras, descritor invalido, asset ausente, hashes e
`version_lock` divergentes, manifesto ZUM1 adulterado, ZUPD invalido,
confirmacao sem preflight ou com tag diferente, cancelamento, rede ausente,
preservacao do cache e regressao U5/Classic.

### EP6.1 - TLS e identidade do canal

**Estado:** fundacao implementada e matriz especifica da EP6.1 validada no
QEMU; regressao EP6.0/U5 preservada conforme validacao anterior.

- [x] Criar o RTC CMOS em UTC, com leitura estavel, BCD/binario, 12/24 horas,
  calendario validado de 2000 a 2099, estado, autoteste e logs.
- [x] Ancorar o UTC validado no tick monotono do PIT, com rollover de 32 bits,
  estado fail-closed e comandos `clock status|check`.
- [x] Definir a politica TLS policy-only: CA estatica obrigatoria, SAN do host,
  janela temporal, pin SPKI opcional, rotacao atual/proxima e revogacao por
  versao assinada; `https://` continua recusado antes de DNS/socket.
- [x] Expor `tls status|check`, `health` e `regcheck` sem habilitar handshake,
  parser X.509, armazenamento real de CA ou fallback HTTP.
- [x] Validar a sequencia informada pelo usuario: `make q3check`, `make clean &&
  make`, `make run` e `make update-test`; o updater terminou com `OK`.
- [x] Validar no QEMU `clock status`, `clock check`, `tls status`, `tls check`,
  `health`, `regcheck full` e `memcheck`; RTC/UTC, monotono e politica TLS
  ficaram operacionais, com todos os autotestes em `OK`.
- [x] Preservar a regressao EP6.0/U5; a EP6.1 nao altera U5, boot ou
  transporte remoto.

### EP6.2 - Canal GitHub configuravel

**Estado:** concluida.

- [x] Integrar BearSSL 0.6 vendorizado, com licença e versão fixadas, perfil
  freestanding, trust anchor estático, TLS 1.2, SNI/SAN, validade temporal via
  `clock`, RDRAND/CPUID e falha fechada sem segurança suficiente; `boot.asm`
  permaneceu intocado.
- [x] Estender HTTP para `https://`, `Accept`, `X-GitHub-Api-Version`, redirects
  absolutos HTTPS limitados a três saltos, bloqueio de downgrade e status
  público de TLS/segurança/redirects.
- [x] Versionar `config/update-remote.json` e o header derivado com endpoint,
  proprietário, repositório, template por `{owner}`, `{repo}` e `{tag}`, versão
  da API e nomes de `release.json`, `release.zum` e `update.zephyrosupd`.
- [x] Implementar parser JSON limitado da resposta de Releases: tag exata,
  publicação, ausência de draft/prerelease, assets únicos, estado, tamanho,
  URL HTTPS e digest SHA-256 opcional; registrar fingerprint dos metadados no
  preflight e exigir igualdade na confirmação.
- [x] Buscar `release.json` descoberto pela API e reutilizar o parser EP6.0;
  hashes, versão, epoch, compatibilidade, ZUM1 e ZUPD continuam sendo a
  autoridade assinada. O download publica somente no cache U5; `update apply`
  permanece separado.
- [x] Preservar `update_remote_release_check()`/`fetch()`, cache A/B,
  cancelamento, retry integral, rollback e nomes de motivos existentes; anexar
  `TLS`, `REDIRECT` e `RELEASE_API` ao fim dos enums públicos.
- [x] Atualizar `tools/updater.py`, fixtures HTTPS GitHub, contratos públicos,
  documentação de distribuição e diagnósticos `tls`/`health`/`health check`/
  `update github`.

### Etapa LBA — Carregamento do kernel no `stage2`

**Estado:** implementada e validada pelo usuário.

#### Implementação

- [x] Fazer o `stage2` preferir as extensões BIOS `INT 13h/AH=42` para carregar
  o kernel por LBA, sem depender da geometria CHS exposta pelo BIOS/QEMU.
- [x] Manter CHS como fallback explícito quando as extensões LBA não estiverem
  disponíveis, com limites de memória, tamanho de lote, retry e diagnóstico
  preservados.
- [x] Manter `src/boot/boot.asm` intocado e preservar a reserva da imagem, o
  bounce buffer e o contrato de carregamento do kernel.
- [x] Adicionar `run-stage2-lba` e `run-stage2-chs` para validar os dois caminhos
  sem comandos QEMU manuais.
- [x] Confirmar no build que `boot.bin` permanece com 512 bytes e que o tamanho
  alinhado do `stage2` não excede sua janela abaixo de `0x10000`.
- [x] Validar com `make q3check`, build limpo, `make run`, `make run-stage2-lba`
  e `make run-stage2-chs`, além de `health check`, `memcheck` e `regcheck full`.

#### Critério de saída

O kernel inicia pelo caminho LBA sem depender da geometria IDE, continua
iniciando pelo fallback CHS quando EDD não existe e mantém intactos o estágio
1, os limites de memória e a ABI entregue ao kernel.

### EP6.3 - Runtime v2, cache seletivo e matriz de falhas

- [x] Contrato `ZUM2`/`ZUPD v2` assinado com Ed25519, hashes SHA-256 por
  arquivo, bases multiplas, limites de 16 entradas/64 KiB/128 KiB, catalogo
  fixo dos tres BMPs e operacoes controladas de substituir, criar e remover.
- [x] Pacote completo independente de cadeia de deltas, cache remoto v2 A/B
  separado do U5 (`ZRV`), manifesto publicado antes dos payloads e download
  seletivo por hash local ou fallback `--full`.
- [x] Estado local v2 em namespaces `ZTV`, `ZTS` e `ZTB`, journal redundante,
  staging atomico, backups, rollback, recuperacao deterministica e preservacao
  do slot ativo e dos dados persistentes do usuario.
- [x] Fluxos HTTP U5 e GitHub HTTPS por tag exata, descritor `release.json`
  v2 como metadado de transporte, Shell `update runtime ...` e aba Runtime
  somente no Updater Classic; o modo Simple permanece congelado.
- [x] Ferramenta host com geracao/verificacao, fixtures EP6.3, servidores HTTP
  e GitHub runtime, auditoria offline de `ZRV/ZTV/ZTS/ZTB` e selftest.
- [x] Compatibilidade operacional com o estado U3 existente: o layout e as
  APIs v1 permanecem, e o estado de arquivos compartilhados e sincronizado
  apos uma transacao runtime v2.
- [x] Validacao do usuario no QEMU do HTTP U5 concluida para os fluxos
  seletivo e completo, incluindo manifestos assinados, rejeicoes de asset/tag/
  JSON/hash/assinatura/pacote e preservacao do cache anterior.
- [x] Auditoria offline da imagem concluida apos os cenarios validos e de
  pacote adulterado, confirmando preservacao da instalacao anterior, estado
  runtime READY, journal CLEAN e ausencia de pending.
- [x] Validacao do Updater Classic concluida com a aba Runtime, incluindo
  exibicao de estado, journal, cache, manifesto e controles da operacao.
- [x] Cancelamento do download runtime v2 concluido com `F12`, preservando o
  cache anterior, a instalacao `0.1.0` e o journal `CLEAN`.
- [x] Limpeza do cache runtime v2 e auditoria offline repetidas com sucesso,
  confirmando `runtime=EMPTY`, `alias=none`, `pending=NO` e journal local
  `CLEAN`.
- [x] Failpoint QEMU (`fail-after 1`), recuperação após reboot, aplicação da
  Release válida e rollback de arquivos substituídos foram validados pelo
  usuário.
- [x] A matriz QEMU A/B com substituição, criação, remoção e rollback foi
  repetida após as correções de auditoria e limpeza de backups. A imagem final
  não reteve `ZTB*`, e a auditoria offline foi aprovada.
- [x] GitHub HTTPS validado. Evidências e horário em
  [`registro-validacoes.md`](../qualidade/registro-validacoes.md#ep63--github-runtime-v2-via-https).

### Criterio de saida

Uma tag exata seleciona uma Release verificavel, mas somente ZUM2/ZUPD v2
assinados autorizam o runtime. Baixar nao instala; somente `update runtime
apply --confirm` aplica uma atualizacao apos confirmacao e informa o reboot.
Rede ausente, origem maliciosa, tag inexistente, falha de download ou
interrupcao de journal preservam cache, instalacao anterior, rollback e dados
persistentes. A validacao QEMU do HTTP U5 e a auditoria offline foram
concluidas; a matriz restante continua registrada acima.

### EP6.4 - Gerenciamento de stack para rede e TLS

**Estado:** implementada e validada no QEMU; gates de código e smoke tests de
regressão visual pendentes de registro.

### Implementacao

- [x] Instrumentar stacks nativas com área útil preenchida, canários inferior
  e superior, high-water, menor folga, contadores e alocação bruta preservada.
- [x] Formalizar 4 KiB como padrão e mínimo, 16 KiB como máximo, alinhamento
  de 16 bytes, `Zephyr System`/TLS com 16 KiB e Shell mantido em 16 KiB.
- [x] Preservar a ABI ring 3 e expor consulta por PID, validação global e
  autoteste determinístico de limites, alinhamento, canários e margem baixa.
- [x] Adicionar `stack status` e `stack check`; integrar a validação ao
  `regcheck full`.
- [x] Encerrar HTTP/TLS com `ERR_OVERFLOW` ao atingir 1 KiB de folga e entrar
  em `panic` somente se um canário real for corrompido.

### Validacao

- [x] No QEMU, executar `stack status`, `stack check`, `tls check`, consulta e
  download runtime GitHub por tag, `health check`, `memcheck` e `regcheck
  full`. Evidência e horário em
  [`registro-validacoes.md`](../qualidade/registro-validacoes.md#ep64--gerenciamento-de-stack-para-rede-e-tls).
- [x] Confirmar no System/TLS 11924 bytes de menor folga após HTTPS, canários
  íntegros, cache runtime publicado e validações estruturais em `OK`.
- [ ] Registrar `make q3check` e `make clean && make` para esta revisão.
- [ ] Executar os smoke tests específicos de App Store, Simple e Classic.

### Criterio de saida

O worker `Zephyr System` mantém margem de stack segura durante HTTP/TLS. A
margem baixa interrompe somente a operação remota de forma controlada; um
canário rompido produz diagnóstico com PID/nome e `panic`, sem liberar a stack
em execução. Processos nativos fora do worker preservam 4 KiB, salvo o Shell,
e processos ring 3 não mudam sua ABI.

## EP7 - Wi-Fi por hardware suportado

**Estado:** EP7.0 encerrada em 2026-08-23 19:04:32
(America/Sao_Paulo); implementacao e matriz QEMU apresentadas pelo usuario.
Os gates de qualidade sem horario registrado permanecem documentados como
pendencia de evidencia, sem reabrir esta subetapa.
A EP7 geral permanece aberta para driver, associacao, firmware e integracao L3.
O alvo da proxima subetapa foi identificado como USB Realtek RTL8811CU;
isso nao altera o escopo somente-PCI da EP7.0.

### EP7.0 - Inventario e diagnostico seguro

- [x] Criar `wifi_manager` com snapshot estatico de candidatos PCI classe
  `0x02`, ignorando E1000 e RTL8139 sem habilitar hardware.
- [x] Preservar Vendor ID, Device ID, classe, subclasse, ProgIF, revisao,
  BDF, IRQ e BAR0-BAR5 com estados `INVENTORIED`, `UNSUPPORTED`, `READY` e
  `ERROR`.
- [x] Adicionar `wifi status`, `wifi scan` e `wifi connect`; a conexao falha
  controladamente e nao processa credenciais nesta etapa.
- [x] Integrar a atualizacao ao `device-scan` e a validacao ao `health` e
  `regcheck full`.

### EP7.1 - Driver USB Realtek RTL8811CU

**Estado:** EP7.1 pausada por decisao de escopo. EP7.1A e EP7.1B (EHCI,
transporte comum e integracao de inventario) foram implementadas; a
inicializacao do radio, a validacao completa de firmware, TX/RX, scan,
associacao e DHCP ficam adiadas para a validacao em computador real. A
implementacao nao executa comandos USB de radio sem uma sequencia RTL8811CU
verificavel.

**Decisao de escopo:** o Wi-Fi nao e prioridade imediata. O passthrough do
RTL8811CU no Windows fica pendente e nao sera desbloqueado nesta fase com a
troca do driver hospedeiro para WinUSB. A EP7.1 sera retomada quando o
ZephyrOS for executado em hardware real; ate la, Ethernet, USB legado, EHCI,
Shell e Simple/Classic permanecem como escopo ativo.

**Alvo de hardware literal:**

- Modelo reportado pelo Windows: `Realtek 8811CU Wireless LAN 802.11ac USB NIC`.
- ID base: `USB\VID_0BDA&PID_C811`.
- Revisao observada: `USB\VID_0BDA&PID_C811&REV_0200`.
- Transporte: USB; a implementacao nao deve trata-lo como PCI/PCIe.

### Escopo da EP7.1

- [x] Registrar o Vendor ID, Product ID, revisao, modelo e transporte fornecidos
  pelo usuario, sem presumir variantes do RTL8811CU.
- [x] Expor `bcdDevice` e a tabela completa de endpoints USB inventariados,
  preservando os campos derivados usados por MSC e HID.
- [x] Criar probe somente-leitura para o ID USB e a revisao observada, sem
  inicializar o radio.
- [x] Adicionar `run-usb-wifi` com passthrough literal `vendorid=0x0BDA` e
  `productid=0xC811`; a validacao no QEMU/hardware permanece pendente.
- [ ] Confirmar a enumeracao do dispositivo no caminho USB disponivel no
  ZephyrOS e no QEMU com o dispositivo encaminhado para a maquina virtual.
- [ ] Auditar e, se necessario, completar na EP4 as transferencias USB de
  controle, Bulk, Interrupt, timeouts, reset e recuperacao exigidas pelo alvo.
- [ ] Documentar descritores, endpoints, buffers, sincronizacao, firmware,
  inicializacao, TX/RX e tratamento de falhas do `USB\VID_0BDA&PID_C811`.
- [x] Criar o contrato e o backend seguro de diagnostico do RTL8811CU; quando
  `RTL8811.BIN` estiver ausente/invalido ou a sequencia de radio nao estiver
  confirmada, retornar erro controlado sem tocar no hardware.
- [x] Implementar o controlador EHCI PCI high-speed, enumeracao limitada de
  portas raiz, controle/Bulk/Interrupt, timeout, reset e recuperacao isolada.
- [x] Criar transporte USB comum que seleciona UHCI ou EHCI pelo inventario,
  preservando as APIs UHCI e mantendo HID/MSC legados no caminho UHCI.
- [x] Publicar o RTL8811CU no `network_manager` como candidato USB e reservar
  o ID `net-usb-BB:DD.F-pN`; anexar `ethernet_interface_t` somente quando o
  backend atingir `READY`.
- [x] Ajustar `run-usb-wifi` para `q35`, controlador EHCI e passthrough literal
  `0x0BDA:0xC811`, sem incluir firmware binario.
- [ ] Implementar o backend operacional do RTL8811CU sem expor credenciais ou
  depender do driver instalado no Windows hospedeiro.
- [ ] Entregar frames 802.3 por `ethernet_interface_t` ao `network_manager`,
  reutilizando IPv4, DHCP, DNS, TCP e HTTP existentes.
- [ ] Evoluir `wifi status`, `wifi scan` e `wifi connect` para o driver real,
  incluindo associacao e autenticacao somente depois da inicializacao segura.
- [ ] Definir entrada de segredo sem eco e garantir que credenciais nao cheguem
  a logs, fixtures, imagem ou historico do Shell.
- [ ] Validar ausencia, dispositivo nao suportado, falhas USB, scan, conexao,
  perda de link, Ethernet simultanea e interfaces Simple/Classic.

### Subetapas EP7.1

- [x] EP7.1A — inventario USB, `bcdDevice`, endpoints e probe literal.
- [x] EP7.1B — EHCI high-speed, transporte comum e identificacao no Network.
- [ ] EP7.1C — firmware validado, inicializacao RTL8821C/RTL8811CU e MAC/link.
- [ ] EP7.1D — TX/RX, scan e associacao em rede aberta.
- [ ] EP7.1E — DHCP/IPv4, operacao simultanea com E1000 e validacao QEMU/hardware.

### Referencia arquitetural e licenca

O desenho de camadas usa como referencia arquitetural o `rtw88` do Linux,
especialmente a tabela USB do
[`rtw8821cu.c`](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtw88/rtw8821cu.c)
e a separacao de transporte e chipset do
[`rtw8821c.c`](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtw88/rtw8821c.c).
Nenhum trecho GPL-only, firmware binario ou sequencia de registradores foi
copiado para o repositorio; a implementacao permanece bloqueada quando a
operacao nao pode ser confirmada por fonte tecnica.

### Criterio de saida da EP7.1

O RTL8811CU e inicializado e recuperado de forma controlada no hardware real e
no QEMU encaminhado, entrega frames a pilha existente e permite diagnostico de
scan/conexao sem vazar segredos. Falhas de USB, firmware, transporte ou
associacao permanecem observaveis e nao degradam Ethernet, Shell ou as
interfaces Simple/Classic.

### Implementacao futura adicional

- [ ] Adicionar outros chipsets somente depois de seus IDs literais, transporte
  e requisitos de firmware serem identificados.

### Criterio de saida

Ausencia de radio, firmware, USB ou driver resulta em diagnostico e erro
controlado. Ethernet, atualizacoes locais, Shell e interfaces Simple/Classic
permanecem funcionais, e nenhum segredo de Wi-Fi chega ao repositorio.

## EP8 - Bluetooth por hardware suportado

**Estado:** adiada por decisao de escopo. Assim como a continuidade da EP7.1,
a EP8 sera retomada somente quando houver um computador real e um controlador
Bluetooth identificado. Nenhum driver, firmware, HCI, radio ou emparelhamento
sera implementado enquanto essa validacao nao for priorizada.

### Implementacao

- [ ] Inventariar controladores Bluetooth sem inicializar hardware e escolher
  um transporte e controlador HCI especificos.
- [ ] Reutilizar a base USB somente quando ela ja atender controle, Bulk,
  Interrupt, timeout e recuperacao necessarios; caso contrario, criar primeiro
  a capacidade faltante na EP4.
- [ ] Implementar estado do controlador, descoberta e emparelhamento minimo
  antes de perfis de audio, HID ou transferencia de arquivos.
- [ ] Adicionar `bluetooth status`, `bluetooth scan` e `bluetooth pair`, com
  limites de memoria, timeout, cancelamento e logs sem dados sensiveis.
- [ ] Manter audio Bluetooth, controles, teclado/mouse Bluetooth e transferencia
  de arquivos explicitamente fora do escopo desta fase.

### Criterio de saida

Ausencia de radio, firmware, USB ou driver resulta em diagnostico e erro
controlado. Wi-Fi, Ethernet, atualizacoes locais, Shell e interfaces
Simple/Classic permanecem funcionais, e nenhum segredo Bluetooth chega ao
repositorio.

## EP9 - Atualizacao da imagem do sistema e slots de boot

**Estado:** EP9.0A e EP9.4A implementadas e validadas pelo usuario; EP9.1
implementada, aguardando a validacao executavel do usuario. Aplicacao,
rollback pos-reboot e alteracoes no boot/stage2 continuam fora do escopo atual.

Esta fase separa a atualizacao de arquivos do sistema em execucao da
atualizacao da imagem que o proximo boot carregara. O ZUPD v1 continua limitado
a arquivos regulares e nao recebe alvos de boot, stage2, kernel ou setores
crus.

### EP9.0 - Contratos e pacotes separados

- [x] Manter o ZUPD v1 e o runtime v2 separados para recursos e arquivos
  regulares; a EP9 trata somente a imagem completa, boot, stage2 e kernel.
  Arquivos ja carregados so mudam apos recarga ou reboot quando a funcionalidade
  dependente exigir essa recarga.
- [x] Definir um contrato distinto `ZSYS v1` para a imagem de sistema,
  incluindo kernel, stage2 e metadados de compatibilidade, sem aceitar esse
  pacote no parser ZUPD v1.
- [x] Publicar no descritor da Release os artefatos runtime e system
  separadamente, com hashes e assinaturas coerentes com a mesma versao/epoch.
- [x] Definir se os dois artefatos usam caches independentes ou se a primeira
  entrega permite somente um tipo selecionado por vez; o U5 atual possui um
  unico candidato/cache remoto.

#### EP9.0 — Compatibilidade entre versões e publicacao no GitHub

- [x] Reutilizar `release.json` como descritor de descoberta e transporte,
  mantendo `release.zum`, `update.zephyrosupd`, ZUM1/ZUM2 e ZUPD para os
  fluxos ja existentes; a EP9 acrescenta o artefato `ZSYS` sem criar um canal
  paralelo. A autoridade dos campos continua sendo o ZSYS assinado.
- [x] Acrescentar ao descritor campos de transporte conferidos contra o ZSYS
  assinado para `supported_from`,
  `min_updater`, `boot_abi`, `data_schema_from`, `data_schema_to`,
  `requires_reboot`, canal e rota de upgrade.
- [x] Definir rotas de atualizacao direta e por checkpoint, sem obrigar o
  download de todas as Releases intermediarias quando uma imagem cumulativa e
  uma cadeia de migracoes forem suficientes.
- [ ] Definir um updater bridge para sistemas cujo atualizador ou contrato de
  boot ainda nao consiga ler o formato novo; quando nao houver rota segura,
  oferecer recuperacao ou instalacao completa assinada.
- [ ] Publicar no GitHub Release os assets runtime, system, manifesto e
  assinatura como arquivos binarios imutaveis apos a publicacao; tags e
  titulos apenas localizam a Release e nunca substituem a verificacao assinada.
- [ ] Manter a chave privada fora do repositorio e verificar no ZephyrOS a
  assinatura do manifesto, os hashes e a compatibilidade antes de baixar ou
  aplicar qualquer imagem.

### EP9.0A - Contrato ZSYS e preflight

**Estado:** implementada e validada pelo usuario.

- [x] Definir o envelope ZSYS v1 com cabecalho little-endian fixo de 1024
  bytes, payload de imagem completa, hashes dos componentes e assinatura
  Ed25519 sobre o dominio ZEPHYROS-SYSTEM-IMAGE-V1.
- [x] Validar limite de 8 MiB, alinhamento setorial, identidade, chave,
  assinatura, hashes, versao, epoch, ABI, schema, reboot e rota.
- [x] Criar system-build/system-verify e fixtures validas, truncadas,
  adulteradas, incompativeis e com divergencia de hash.
- [x] Publicar release-v2-build/release-v2-check com namespaces legacy,
  runtime e system, mantendo os fluxos ZUPD v1 e runtime v2.
- [x] Implementar update_system com verificacao local em streaming e preflight
  remoto somente leitura por tag.
- [x] Adicionar update system verify e update system check --tag ao Shell,
  preservando APIs remotas existentes por campos append-only.
- [x] Atualizar contrato publico, indice, ferramenta e distribuicao remota.
- [x] Preparar o alvo `system-fixtures` para gerar a matriz assinada fora do
  repositorio e criar uma imagem hibrida FAT12/FAT32 por fixture, usando nomes
  `.ZSYS` no volume `ZEPHYROS` e sem versionar a chave privada; a imagem base
  nao recebe a matriz inteira.
- [x] Executar a validacao do usuario: make update-test, make q3check,
  make clean && make, make run e a matriz QEMU de fixtures/memcheck/regcheck.

### EP9.1 - Staging e slots de imagem

- [x] Criar dois slots de imagem do sistema, com estado redundante, sequencia,
  tamanho, hash, identidade da Release e marcador de slot pendente, sem
  sobrescrever a imagem em uso.
- [x] Gravar e verificar a imagem nova em staging antes de publicar o slot
  pendente; interrupcao preserva o slot ativo anterior e o staging temporario
  e controlado pelo journal.
- [x] Definir limites de tamanho, memoria, espaco, cancelamento cooperativo,
  recuperacao, fases PREPARED/STAGING/VERIFIED/COMMITTED e politica
  anti-downgrade para a imagem completa.
- [x] Adicionar `update system slots` e `update system stage` com preflight,
  `--confirm`, origem exclusivamente local em `system:/` e resultado detalhado.
- [x] Criar fixtures FAT32 com baseline assinado em `ZSA0.ZSY`, `ZSB0.ZSY`
  vazio, estado redundante e candidato `VALID.ZSYS`, sem chave privada no
  build normal.
- [x] Criar o gerador `system-slots-matrix` com casos reproduziveis de
  corrupcao de estado/journal, fases de recuperacao, falta de espaco e volume
  ausente, sem alterar boot ou stage2.
- [ ] Executar a matriz EP9.1 no QEMU e confirmar a regressao Simple/Classic,
  `health`, `memcheck` e `regcheck full`.

### EP9.2 - Boot, tentativa e rollback

- [ ] Alterar o contrato de boot/stage2 somente depois de aprovacao explicita,
  para selecionar o slot pendente e validar sua assinatura/hash antes de
  transferir o controle ao kernel.
- [ ] Registrar tentativa de boot, sucesso confirmado pelo kernel e falha de
  inicializacao; uma tentativa interrompida deve voltar ao slot anterior.
- [ ] Manter uma via de recuperacao que funcione sem rede e sem depender do
  kernel novo, incluindo diagnostico do slot ativo, pendente e anterior.

### EP9.3 - Comandos e validacao

- [ ] Adicionar comandos separados para consultar, baixar, aplicar e cancelar
  uma imagem `system`, sem misturar o fluxo `runtime` do ZUPD v1.
- [ ] Exigir confirmacao explicita e reboot para ativar `ZSYS`; baixar nunca
  instala nem altera a imagem em uso.
- [ ] Cobrir imagem ausente, assinatura/hash invalidos, versao incompatível,
  falta de espaco, falha durante staging, falha no primeiro boot, rollback,
  energia interrompida, cache corrompido e regressao Simple/Classic.

### EP9.4A - Volume de sistema FAT32

**Estado:** implementada e validada pelo usuario.

- [x] Definir imagem hibrida de 64 MiB, preservando o FAT12 bruto no inicio
  para boot, stage2, kernel e recuperacao.
- [x] Formatar a particao MBR FAT32 tipo `0x0C` a partir do LBA 4096, com
  alinhamento de 2 MiB, duas FATs, FSInfo, backup do boot sector e label
  `ZEPHYROS`, sem editar boot.asm ou stage2.
- [x] Migrar icones, pacotes, atualizacoes e fixtures ZSYS para o volume FAT32;
  manter `inject-file` e as fixtures FAT12 para regressao.
- [x] Montar automaticamente exatamente um volume FAT32 `ZEPHYROS`, expor
  `system:/`, `legacy:/` e volume-id, e manter o fallback FAT12 explicito.
- [x] Implementar leitura/escrita FAT32 com aliases 8.3, LFN UTF-16LE,
  checksum, diretorios, escrita atomica, streaming, exclusao, renomeacao e
  diagnostico somente leitura `storage check`.
- [x] Executar a matriz do usuario: storage-fixtures-test, storage-fixtures,
  system-fixtures, q3check, build completo, QEMU, memcheck e regcheck full.

Limites mantidos para etapas posteriores: filesystem nativo, boot direto pelo
FAT32, aplicacao ZSYS, selecao de slot no boot e reboot automatico.

### EP9.4B - Expansão posterior de armazenamento

- [ ] Definir se o volume de boot permanece FAT12 e o sistema usa um volume
  FAT32 separado, ou se haverá migração controlada do volume de boot.
- [ ] Projetar a nova geometria de disco, BPB, partições e compatibilidade com
  o FS unificado sem reduzir o fallback FAT12 existente.
- [ ] Alterar boot/stage2 somente após aprovação explícita, com recuperação
  offline e preservação do caminho de boot atual durante a transição.
- [ ] Validar no host e no QEMU imagens maiores, nomes, espaço, montagem,
  leitura do ZSYS e regressão dos volumes FAT12/FAT32.

### Criterio de saida

Uma imagem nova pode ser baixada e preparada enquanto o sistema antigo segue
operacional. A ativacao ocorre somente no reboot, com verificacao autenticada,
slot anterior preservado e rollback automatico quando o novo kernel nao
confirma inicializacao. Nenhuma escrita de setor cru sera aceita antes do
contrato transacional e da aprovacao do boot.

## Validacao por etapa

Cada implementacao devera atualizar contratos publicos, fixtures e comandos
antes de iniciar a validacao. O agente nao executa build, Q3 ou QEMU; a
validacao de codigo fica para o usuario conforme `AGENTS.md`.

Para uma etapa que altere codigo, o usuario deve executar, nesta ordem:

```text
make q3check
make clean && make
make run
```

No QEMU, a matriz deve incluir caminho de sucesso, entrada invalida, hardware
ou volume ausente, cancelamento, `health`, `mem`, `regcheck full` e regressao
Simple/Classic. Antes de commit, revisar apenas os arquivos alterados com
`git diff --check`, `git status --short` e, quando houver stage,
`git diff --cached --check`.

## Referencias

- `docs/roadmaps/04-interface-e-experiencia.md` - contratos de interface dual.
- `docs/roadmaps/05-sistema-e-ecossistema.md` - rede, dispositivos e ZUPD.
- `docs/roadmaps/07-modernizacao-visual.md` - escala e modernizacao visual.
- `docs/melhorias futuras/mouse.md` - estado do mouse PS/2.
- `docs/melhorias futuras/gerenciador de arquivos.md` - Explorer e busca.
- `docs/melhorias futuras/gerenciador de rede.md` - base de rede existente.
- `docs/14-atualizacoes/distribuicao-remota.md` - contrato ZUM1/U5.
- `docs/ideias.md` - ideias que originaram o Roadmap 08.
