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
5. Publicacao e verificacao de releases por tags no host.
6. TLS e acesso direto opcional ao GitHub pelo kernel.
7. Wi-Fi para um chipset e transporte escolhidos.
8. Bluetooth HCI para um controlador e transporte escolhidos.

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

- [ ] Implementar transferencias Bulk e o transporte Bulk-Only Transport (BOT).
- [ ] Implementar o subconjunto SCSI necessario: Inquiry, Test Unit Ready,
  Read Capacity e Read10, sempre sem escrita no dispositivo USB.
- [ ] Registrar o dispositivo MSC como provedor da camada de bloco (`block_device_t`),
  permitindo sua montagem transparente de volumes FAT sem acoplamento direto
  entre o driver USB e o sistema de arquivos.
- [ ] Manter disco ATA e volume de boot como fallbacks operacionais prioritários.

### EP4.4 - Interrupt e USB HID

- [ ] Implementar transferencias Interrupt através do despachante assíncrono,
  sem bloqueios por espera ocupada na CPU.
- [ ] Adicionar teclado e mouse HID somente como fontes adicionais de eventos de
  entrada, roteando para a fila global de input sem alterar o foco nem
  substituir os drivers PS/2 existentes.
- [ ] Validar desconexao, pacote invalido, timeout e dispositivo ausente sem
  travar kernel, Shell ou interfaces gráficas.

### Criterio de saida

Cada subetapa preserva o funcionamento de teclado e mouse PS/2. UHCI ausente,
dispositivo malformado ou erro de comunicacao produz `LOG_ERROR` e componente
degradado, sem suporte ficticio a EHCI, hubs, HID ou MSC ainda nao entregues.

## EP5 - Publicacao por tags e verificacao no host

### Implementacao

- [ ] Definir uma politica host em que uma tag imutavel gera ZUPD, manifesto
  ZUM1 assinado, hashes e artefatos de release coerentes.
- [ ] Estender a ferramenta host com verificacao offline de tag, versao,
  versao minima, hashes e assinatura antes da publicacao.
- [ ] Tratar GitHub apenas como origem de distribuicao: o kernel aceita a
  release somente depois de validar ZUM1 e ZUPD pelas chaves ja confiaveis.
- [ ] Publicar fixtures locais equivalentes a tag inexistente, artefato
  ausente, manifesto adulterado e pacote invalido para a ferramenta host.

Esta etapa nao cria um comando novo no kernel nem muda o transporte U5. Ela
torna a publicacao reproduzivel e valida que a tag nunca substitui a assinatura
do manifesto ou do pacote.

### Criterio de saida

Uma tag gera artefatos verificaveis e coerentes antes da publicacao. Tags ou
assets inconsistentes falham no host sem gerar uma release utilizavel.

## EP6 - TLS e canal GitHub opcional

### Implementacao

- [ ] Definir o contrato minimo de TLS, estrategia de tempo e politica de
  validacao de certificados antes de abrir conexoes HTTPS no kernel.
- [ ] Avaliar pinning de chave ou certificado somente como complemento de TLS,
  com rotacao e revogacao documentadas; nunca como substituto da assinatura
  ZUM1/ZUPD.
- [ ] Adaptar U5 a um canal de release configuravel e criar
  `update github status`, `update github check` e `update github fetch`,
  sempre opt-in e sem instalacao automatica.
- [ ] Limitar URL, redirecionamentos, tamanho de resposta, retries, memoria e
  tempo de operacao; nenhum token ou conta GitHub e necessario.
- [ ] Cobrir falha de DNS, certificado invalido, hora indisponivel, tag
  inexistente, asset ausente, download interrompido, manifesto adulterado,
  cache preservado e rollback com fixtures e matriz QEMU.

Esta etapa so inicia depois da EP5. Enquanto TLS, relogio confiavel e validacao
de certificados nao existirem, conexao direta a `github.com` permanece fora do
kernel e o transporte remoto atual continua opcional.

### Criterio de saida

Uma tag publicada pode ser descoberta e baixada como cache verificavel, mas
somente `update apply` instala uma atualizacao apos confirmacao. Rede ausente,
origem maliciosa e falha de download preservam cache, atualizacao local e
rollback existentes.

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
