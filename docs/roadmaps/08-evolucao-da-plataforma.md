# Roadmap 08 - Evolucao da Plataforma

## Objetivo

Evoluir as capacidades de entrada, armazenamento e conectividade do ZephyrOS
sem reabrir contratos ja validados de boot, filesystem, rede cabeada ou
atualizacao segura.

Esta frente sucede o Roadmap 07. A escala e o layout da interface pertencem a
MV0 no roteiro visual; este documento cobre preferencias de mouse, volumes,
indice, USB, releases por tags e radios sem fio. As etapas menores so avancam
quando suas dependencias tecnicas estiverem validadas.

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
- USB comeca por um unico controlador UHCI. EHCI e apenas inventariado nesta
  frente; seu agendamento proprio fica fora do primeiro driver.
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
10. Bluetooth HCI para um controlador e transporte escolhidos.
11. EP9: imagem do sistema, slots de boot e recuperacao pos-reboot.

## EP1 - Preferencias de mouse

**Estado:** implementada e validada pelo usuario em 01/08/2026. O gate Q3,
build completo e matriz QEMU foram aprovados depois de proteger a transacao
compartilhada PS/2 contra a IRQ do teclado.

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

**Estado:** implementada em 01/08/2026 e validada pelo usuario em 02/08/2026.
Q3, build completo, gerador deterministico, hashes antes/depois, boot normal e
matriz QEMU com quatro slots IDE foram aprovados.

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

**Estado:** implementada em 02/08/2026 e validada pelo usuario em 05/08/2026.

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

### Validacao concluida em 05/08/2026

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

**Estado:** implementada e validada pelo usuario em 20/08/2026.

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

**Estado:** implementada e validada pelo usuario em 20/08/2026.

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

**Estado:** implementada e validada pelo usuario em 20/08/2026.

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

**Implementação registrada em:** 21/08/2026 12:10:35 (America/Sao_Paulo).

**Validação parcial registrada em:** 21/08/2026 12:31:49
(America/Sao_Paulo).

**Validação final concluída em:** 2026-08-21 16:13:14
(America/Sao_Paulo).

**Correção do mapeamento USB HID/ABNT2 implementada em:** 2026-08-21
15:00:11 (America/Sao_Paulo). **Validação desta correção confirmada em:**
2026-08-21 16:13:14 (America/Sao_Paulo).

**Correção das posições ABNT2 `;/:` e `/ ?` implementada em:** 2026-08-21
15:08:18 (America/Sao_Paulo). **Validação desta correção confirmada em:**
2026-08-21 16:13:14 (America/Sao_Paulo).

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

**Implementacao concluida em:** 2026-08-21 16:33:39
(America/Sao_Paulo).

**Validacao concluida em:** 2026-08-21 16:36:35
(America/Sao_Paulo).

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

**Planejamento atualizado em:** 2026-08-21 16:39:48
(America/Sao_Paulo).

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

Implementacao concluida em: 2026-08-21 17:52 (America/Sao_Paulo).
Correcao do hash publicado da fixture EP6 concluida em: 2026-08-21 18:10
(America/Sao_Paulo).
Selftest de fixtures EP6 concluido em: 2026-08-21 18:12
(America/Sao_Paulo).
Correcao do apagao no comando EP6.0 concluida em: 2026-08-21 18:19
(America/Sao_Paulo).
Correcao das capacidades dos buffers estaticos do descritor concluida em:
2026-08-21 18:46 (America/Sao_Paulo).
Correcao da capacidade do hash de asset concluida em: 2026-08-21 18:52
(America/Sao_Paulo).
Diagnostico granular da validacao de assets EP6.0 concluido em: 2026-08-21
18:58 (America/Sao_Paulo).
Instrumentacao dos submotivos de asset EP6.0 concluida em: 2026-08-21 19:00
(America/Sao_Paulo).
Instrumentacao das transicoes JSON de assets EP6.0 concluida em: 2026-08-21
19:05 (America/Sao_Paulo).
Correcao do parser numerico com espacos JSON concluida em: 2026-08-21 19:08
(America/Sao_Paulo).
Correcao da propagacao de motivos antes dos assets concluida em: 2026-08-21
19:20 (America/Sao_Paulo).
Correcao do diagnostico de cabecalho e das rotas das fixtures invalidas
concluida em: 2026-08-21 19:27 (America/Sao_Paulo).
Correcao do estado EMPTY apos falha de download sem pacote ativo concluida
em: 2026-08-21 19:39 (America/Sao_Paulo).
Validacao funcional da EP6.0 concluida em: 2026-08-21 20:07
(America/Sao_Paulo).

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
- [x] Preservar a regressao EP6.0/U5 ja validada na EP6.0 em 2026-08-21
  20:07 (America/Sao_Paulo); a EP6.1 nao altera U5, boot ou transporte remoto.

Implementacao concluida em: 2026-08-21 23:40 (America/Sao_Paulo).
Correcao do link freestanding concluida em: 2026-08-21 23:46 (America/Sao_Paulo).
Validacao da sequencia host concluida em: 2026-08-21 23:52 (America/Sao_Paulo).
Validacao QEMU parcial concluida em: 2026-08-21 23:56 (America/Sao_Paulo).
Validacao QEMU da EP6.1 concluida em: 2026-08-21 23:58 (America/Sao_Paulo).
Regressao EP6.0/U5 reutilizada para o fechamento da EP6.1 em: 2026-08-22
08:55 (America/Sao_Paulo); execucao original registrada em 2026-08-21 20:07.
EP6.1 concluida em: 2026-08-22 08:55 (America/Sao_Paulo).

### EP6.2 - Canal GitHub configuravel

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
  documentação de distribuição e diagnósticos `tls`/`health`/`update github`.

Transporte BearSSL/HTTP concluído em: 2026-08-22 11:40 (America/Sao_Paulo).
Configuração, parser GitHub e preflight concluídos em: 2026-08-22 11:40
(America/Sao_Paulo).
Fixtures, contratos e documentação concluídos em: 2026-08-22 11:40
(America/Sao_Paulo).
Implementação da EP6.2 concluída em: 2026-08-22 11:40 (America/Sao_Paulo).
Correção dos inicializadores do contrato remoto concluída em: 2026-08-22 11:47
(America/Sao_Paulo).
Auditoria de versionamento, `.gitignore` e arquivos alterados/novos concluída
em: 2026-08-22 11:52 (America/Sao_Paulo).
Correção da regra de criação do subdiretório BearSSL concluída em: 2026-08-22
11:58 (America/Sao_Paulo).
Correção da inclusão do contrato de memória do BearSSL concluída em: 2026-08-22
12:02 (America/Sao_Paulo).
Correção do wrapper freestanding `stddef.h`/`offsetof` do BearSSL concluída em:
2026-08-22 12:04 (America/Sao_Paulo).
Dependência explícita de `stddef.h` no build BearSSL concluída em: 2026-08-22
12:05 (America/Sao_Paulo).
Correção do wrapper freestanding `stdint.h`/`uintptr_t` do BearSSL concluída
em: 2026-08-22 12:07 (America/Sao_Paulo).
Correção da condição de digest da EP6.2 e do fallback de `reason` concluída em:
2026-08-22 12:10 (America/Sao_Paulo).
Diagnóstico host da falha QEMU concluído em: 2026-08-22 12:21
(America/Sao_Paulo); payload concatenado e imagem FAT12 íntegros, com a falha
restrita à leitura CHS do `stage2` ainda pendente de confirmação no QEMU.
Correção da geometria IDE do QEMU concluída em: 2026-08-22 12:25
(America/Sao_Paulo); `Makefile` passou a usar `-drive if=none` e `ide-hd` com
geometria explícita 80/2/18, sem alterar o bootloader.
Validação QEMU da imagem com geometria IDE 80/2/18 concluída em: 2026-08-22
12:25 (America/Sao_Paulo); a imagem abriu e iniciou com sucesso.
Validação host, gate `make q3check`, build completo e matriz QEMU permanecem
pendentes do usuário; registrar cada horário real nesta seção após a execução.

### EP6.3 - Falhas, cache e regressao

- [ ] Cobrir falha de DNS, certificado invalido, hora indisponivel, tag
  inexistente, asset ausente, download interrompido, manifesto adulterado,
  cache preservado e rollback com fixtures e matriz QEMU.
- [ ] Definir um pacote runtime completo por Release, com manifesto assinado,
  hashes por arquivo e compatibilidade com as versoes instaladas suportadas,
  para que uma atualizacao direta nao dependa de baixar uma cadeia de deltas.
  Deltas podem ser uma otimizacao futura, mas nao devem ser obrigatorios.
- [ ] Publicar o manifesto antes dos payloads e permitir assets por arquivo:
  comparar hashes locais, baixar somente arquivos novos/alterados, reutilizar
  os demais e manter um pacote completo como fallback de recuperacao.
- [ ] Definir o novo contrato de pacote (por exemplo, ZUPD v2) sem alterar
  silenciosamente o ZUPD v1, incluindo criacao/remocao controlada de arquivos,
  staging atomico, rollback e politica de dados persistentes.

EP6.0 continua exercitável com o servidor de fixtures U5. EP6.2 usa o canal
HTTPS BearSSL configurado para a API GitHub e mantém o transporte HTTP U5 para
compatibilidade; a publicação de artefatos continua dependendo de ZUM1/ZUPD
assinados e nenhuma consulta ocorre no boot.

### Criterio de saida

Uma tag exata seleciona uma Release verificavel, mas somente ZUM1/ZUPD
assinados autorizam o candidato. Baixar nao instala; somente `update apply`
aplica uma atualizacao apos confirmacao e reboot. Rede ausente, origem
maliciosa, tag inexistente e falha de download preservam cache, atualizacao
local e rollback existentes.

## EP7 - Wi-Fi por hardware suportado

### Implementacao

- [ ] Inventariar controladores sem inicializar hardware e selecionar um
  chipset Wi-Fi alvo, incluindo transporte (PCI/PCIe ou USB), DMA, IRQ,
  firmware e matriz de teste requeridos.
- [ ] Se o chipset usar USB, exigir as transferencias necessarias da EP4 antes
  de iniciar associacao; se usar PCI/PCIe, documentar seu driver de barramento
  e limites proprios.
- [ ] Integrar uma interface Wi-Fi validada ao `network_manager`, reutilizando
  IPv4, DHCP, DNS, TCP e HTTP em vez de duplicar a pilha IP.
- [ ] Definir autenticacao e entrada de segredo sem eco antes de suportar rede
  protegida; rede aberta existe apenas como diagnostico controlado.
- [ ] Adicionar `wifi status`, `wifi scan` e `wifi connect`, sem expor
  credenciais em logs, fixtures, imagem ou historico do Shell.

### Criterio de saida

Ausencia de radio, firmware, USB ou driver resulta em diagnostico e erro
controlado. Ethernet, atualizacoes locais, Shell e interfaces Simple/Classic
permanecem funcionais, e nenhum segredo de Wi-Fi chega ao repositorio.

## EP8 - Bluetooth por hardware suportado

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

**Estado:** planejada; nao iniciar antes de uma aprovacao explicita para
alterar boot/stage2.

**Planejamento registrado em:** 2026-08-21 16:45:17
(America/Sao_Paulo).

Esta fase separa a atualizacao de arquivos do sistema em execucao da
atualizacao da imagem que o proximo boot carregara. O ZUPD v1 continua limitado
a arquivos regulares e nao recebe alvos de boot, stage2, kernel ou setores
crus.

### EP9.0 - Contratos e pacotes separados

- [ ] Manter o pacote runtime baseado em ZUPD v1 para recursos e arquivos
  regulares enquanto o novo pacote completo nao for definido; download e
  staging podem ocorrer com o sistema em execucao, mas arquivos ja carregados
  so mudam apos recarga ou reboot.
- [ ] Definir um contrato distinto `ZSYS v1` para a imagem de sistema,
  incluindo kernel, stage2 e metadados de compatibilidade, sem aceitar esse
  pacote no parser ZUPD v1.
- [ ] Publicar no descritor da Release os artefatos runtime e system
  separadamente, com hashes e assinaturas coerentes com a mesma versao/epoch.
- [ ] Definir se os dois artefatos usam caches independentes ou se a primeira
  entrega permite somente um tipo selecionado por vez; o U5 atual possui um
  unico candidato/cache remoto.

### EP9.1 - Staging e slots de imagem

- [ ] Criar dois slots de imagem do sistema, com estado redundante, sequencia,
  tamanho, hash e marcador de slot pendente, sem sobrescrever a imagem em uso.
- [ ] Gravar e verificar a imagem nova em staging antes de publicar o slot
  pendente; interrupcao deve preservar o slot ativo anterior.
- [ ] Definir limites de tamanho, memoria, espaco, timeout, cancelamento,
  recuperacao e politica anti-downgrade para a imagem completa.

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
