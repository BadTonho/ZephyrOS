# Roadmap 07 - Evolucao da Plataforma

## Objetivo

Evoluir o ZephyrOS para ser mais confortavel de usar e mais capaz de lidar com
armazenamento e conectividade, sem reabrir contratos ja validados de boot,
filesystem, rede cabeada ou atualizacao segura.

Esta frente organiza preferencias de mouse e video, volumes/particoes,
pesquisa de arquivos, distribuicao por tags do GitHub e suporte inicial a
Wi-Fi/Bluetooth. Ela inicia depois da etapa ativa da App Store, mas suas fases
menores podem receber manutencao de planejamento sem antecipar implementacao.

## Base ja validada

- [x] Mouse PS/2, cursor grafico, roda e roteamento global de eventos.
- [x] VESA com enumeracao de modos, framebuffer e interfaces Classic/Modern.
- [x] Settings, Desktop, Taskbar, Window Manager e Explorer nos dois modos.
- [x] ATA PIO, FAT12, FAT32 e interface unificada de filesystem.
- [x] E1000, RTL8139, IPv4, DHCP, DNS, TCP, HTTP e operacao multi-NIC.
- [x] Atualizacao ZUPD v1: verificacao assinada, aplicacao recuperavel,
  rollback, manifesto ZUM1 e cache remoto HTTP opcional.

Essas capacidades continuam sendo a fonte de verdade. O Roadmap 07 acrescenta
camadas de configuracao, descoberta, montagem e distribuicao sobre elas.

## Decisoes de produto

- Classic continua como fallback completo; Modern e Shell devem permanecer
  operacionais durante qualquer falha de video, volume ou radio.
- Nenhuma fase altera `src/boot/boot.asm`, stage2 ou escreve setores crus sem
  uma etapa transacional especificamente aprovada.
- A primeira entrega de particoes e somente-leitura. Criar, formatar,
  redimensionar e apagar particoes sao trabalhos posteriores.
- A primeira busca indexa nomes e caminhos, nao conteudo de arquivos.
- Tags do GitHub ajudam a selecionar a release, mas nao sao a raiz de
  confianca: somente ZUM1 e ZUPD assinados podem ser aceitos pelo sistema.
- Nenhuma consulta remota instala atualizacao automaticamente; `update apply`
  continuara exigindo confirmacao explicita.
- Wi-Fi e Bluetooth serao feitos por chipset e transporte definidos. Nenhum
  driver deve alegar suporte generico para hardware que nao foi validado.
- Toda capacidade executavel tera comandos Shell, logs, erros controlados e
  regressao Classic/Modern correspondente.

## Ordem de dependencia

1. Pilha USB e suporte inicial a controladoras UHCI/EHCI.
2. Configuracao de mouse e escala de interface sem persistencia.
3. Descoberta de volumes e montagem somente-leitura (incluindo armazenamento USB MSC).
4. Indice em RAM e busca por nomes sobre volumes montados.
5. Canal de release por tag sobre o ZUPD remoto existente.
6. Transporte e drivers especificos de Wi-Fi e Bluetooth (com suporte a transporte USB).

## EP1 - Pilha USB e controladoras base

### Implementacao

- [ ] Detectar e inventariar controladores de host USB no barramento PCI (UHCI/EHCI via PCI class `0x0C` and subclass `0x03`).
- [ ] Inicializar o controlador UHCI (configuração de portas I/O, enfileiramento de descritores de transferência e habilitar interrupções).
- [ ] Implementar a pilha USB Core: enumeração de dispositivos no barramento, atribuição de endereços de porta e leitura de descritores de dispositivo, configuração e interface.
- [ ] Desenvolver suporte a transferências de Controle (Control) e Lote (Bulk) para comunicação de dados.
- [ ] Criar drivers mínimos para classes USB de exemplo:
  - USB Mass Storage Class (MSC) para leitura de setores em pendrives virtuais.
  - USB Human Interface Device (HID) para teclado e mouse como alternativa aos drivers PS/2 nativos.
- [ ] Adicionar comandos do Shell: `usb status`, `usb list` e `usb device <id>`.
- [ ] Registrar o status do serviço USB no sistema de `health` do kernel.

### Criterio de saida

A enumeração e leitura de dispositivos USB no QEMU não interferem com o mouse e teclado PS/2 existentes. Controladores ausentes no PCI resultam em serviço desabilitado no `health` de forma controlada, e erros de comunicação USB geram `LOG_ERROR` sem travar o kernel.

## EP2 - Mouse e video acessiveis

### Implementacao

- [ ] Criar configuracao central de sensibilidade, aceleracao opcional e
  botao principal do mouse, com valores padrao seguros.
- [ ] Remapear o botao principal antes do despacho para Desktop, Taskbar, WM e
  apps; manter o estado bruto observavel para diagnostico.
- [ ] Garantir que o escalonamento de movimento respeite limites de tela e nao
  descarte pacotes da fila PS/2.
- [ ] Adicionar `mouse speed <1-10>` e `mouse primary left|right`; `mouse`
  exibira configuracao efetiva, estado bruto e falhas do driver.
- [ ] Reutilizar a enumeracao VESA para `display modes`, listando somente
  modos que o renderer atual suporta.
- [ ] Separar resolucao, escala de UI e escala de fonte. Aumentar a resolucao
  nao pode reduzir textos, cursores ou alvos de clique sem compensacao.
- [ ] Centralizar metricas de layout para Desktop, Taskbar, WM, Explorer e
  Settings, preservando o Modo Classico em texto.
- [ ] Adicionar `display set <modo>` como troca temporaria com confirmacao e
  reversao por timeout para o ultimo modo conhecido como valido.
- [ ] Expor os controles equivalentes no Settings Classic/Modern e registrar
  toda recusa ou falha VESA com `LOG_ERROR`.

Persistencia de preferencias fica fora da EP1. Enquanto nao houver uma area de
configuracao recuperavel, os valores permanecem em RAM e isso e informado ao
usuario.

### Criterio de saida

Mouse, clique, arrasto, roda, teclado, cursor e Shell continuam utilizaveis
apos aplicar ou reverter um modo VESA. Valores invalidos, hardware ausente e
timeout de confirmacao falham controladamente, sem bloquear o fallback
Classico.

## EP3 - Volumes e montagem de particoes

### Implementacao

- [ ] Descobrir MBR somente em leitura e inventariar discos, particoes e
  volumes por identificadores estaveis.
- [ ] Criar uma abstracao de volume acima de ATA (e do driver USB Mass Storage da EP1) sem alterar a API FAT atual
  ate os chamadores passarem explicitamente o volume alvo.
- [ ] Validar limites do volume e BPB antes de montar FAT12/FAT32 sob demanda.
- [ ] Limitar memoria, numero de volumes e operacoes concorrentes; toda falha
  de leitura, formato ou montagem deve ter log por volume.
- [ ] Adicionar `storage list`, `storage info <id>`, `storage mount <id>` e
  `storage unmount <id>`; comandos de consulta nao gravam no disco.
- [ ] Mostrar volumes montados no Explorer e Settings, mantendo o volume de
  boot como fallback quando nenhum volume adicional for valido.
- [ ] Integrar volumes detectados via USB MSC na EP1 à interface de volumes montáveis.
- [ ] Documentar uma fase futura, separada, para GPT, formatacao e alteracao
  da tabela de particoes com staging, journal e recuperacao.

### Criterio de saida

Listar e montar um volume valido nao modifica setores. Particoes ausentes,
corrompidas ou com filesystem desconhecido permanecem isoladas, sem afetar o
boot, Shell, filesystem atual ou outros volumes montados.

## EP4 - Indice e pesquisa de arquivos

### Implementacao

- [ ] Definir o contrato inicial do indice: volume, caminho, nome, tipo e
  tamanho; pesquisa de conteudo e metadados ricos ficam fora desta etapa.
- [ ] Construir o indice cooperativamente, com orcamento por tick,
  cancelamento, progresso e limites explicitos de memoria e entradas.
- [ ] Associar entradas ao ID e a geracao do volume para detectar montagem,
  desmontagem ou alteracao externa que deixe resultados desatualizados.
- [ ] Atualizar ou invalidar entradas nas operacoes existentes de criar,
  renomear, copiar, mover e excluir do filesystem.
- [ ] Adicionar `index status`, `index rebuild` e `search <termo>` no Shell;
  a busca informa resultado parcial, volume ausente ou indice desatualizado.
- [ ] Integrar uma tela de pesquisa no Explorer sem bloquear o desenho,
  entrada, mouse, rede ou Shell.
- [ ] Manter a primeira versao em RAM. Persistencia so entra depois de definir
  gravacao recuperavel sobre a camada de volumes da EP2.

### Criterio de saida

Uma busca limitada encontra caminhos corretos sem travar a interface. Indice
corrompido, cancelado ou sem memoria gera log e pode ser reconstruido, sem
impedir navegacao normal do Explorer ou uso do filesystem.

## EP5 - Canal de releases por tags do GitHub

### Implementacao

- [ ] Definir uma politica host em que uma tag imutavel gera ZUPD, manifesto
  ZUM1 assinado, hashes e artefatos de release coerentes.
- [ ] Estender a ferramenta host com verificacao offline de tag, versao,
  versao minima, hashes e assinatura antes da publicacao.
- [ ] Tratar GitHub apenas como origem de distribuicao: o kernel aceita a
  release somente depois de validar ZUM1 e ZUPD pelas chaves ja confiaveis.
- [ ] Adaptar U5 para um canal de release configuravel e criar
  `update github status`, `update github check` e `update github fetch`,
  sempre opt-in e sem instalacao automatica.
- [ ] Cobrir tag inexistente, asset ausente, download interrompido, manifesto
  adulterado, pacote invalido, cache preservado e rollback com fixtures locais
  e matriz QEMU.

GitHub normalmente requer HTTPS. Enquanto o ZephyrOS nao possuir TLS, relogio
confiavel e validacao de certificados, nenhuma conexao direta a `github.com`
deve ser habilitada no kernel. O primeiro fluxo deve usar publicacao assinada
validada no host e transporte compativel com o contrato remoto existente.

### Criterio de saida

Uma tag publicada pode ser descoberta e baixada como cache verificavel, mas
somente `update apply` instala uma atualizacao apos confirmacao. Rede ausente,
origem maliciosa e falha de download preservam cache, atualizacao local e
rollback existentes.

## EP6 - Wi-Fi e Bluetooth por hardware suportado

### Implementacao

- [ ] Inventariar controladores sem inicializar hardware e selecionar um
  chipset alvo para Wi-Fi e outro para Bluetooth, incluindo seu transporte
  (PCI/PCIe ou USB da EP1), DMA, IRQ e firmware requerido.
- [ ] Planejar e implementar a base de transporte (como o pareamento de USB
  ou drivers de barramento específicos) que estiver faltando antes
  de qualquer associacao Wi-Fi ou emparelhamento Bluetooth.
- [ ] Integrar uma interface Wi-Fi validada ao `network_manager`, reutilizando
  Ethernet, IPv4, DHCP, DNS, TCP e HTTP em vez de duplicar a pilha IP.
- [ ] Adicionar `wifi status`, `wifi scan` e `wifi connect`; conexao deve
  manter credenciais fora de logs, fixtures e imagem de distribuicao.
- [ ] Implementar Bluetooth sobre HCI para o controlador escolhido, iniciando
  por status, descoberta e emparelhamento minimo.
- [ ] Adicionar `bluetooth status`, `bluetooth scan` e `bluetooth pair` antes
  de perfis de audio, HID ou transferencia de arquivos.
- [ ] Validar no hardware ou emulacao que represente os chipsets escolhidos;
  QEMU sem radio deve reportar componente degradado, nunca suporte ficticio.

### Criterio de saida

Ausencia de radio, firmware, USB ou driver resulta em diagnostico e erro
controlado. Ethernet, atualizacoes locais, Shell e interfaces Classic/Modern
permanecem funcionais, e nenhum segredo de Wi-Fi ou Bluetooth chega ao
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

No QEMU, a matriz deve incluir o caminho de sucesso, entradas invalidas,
hardware ou volume ausente, cancelamento, `health`, `mem`, `regcheck full` e
regressao Classic/Modern. Antes de commit, revisar apenas os arquivos alterados
com `git diff --check`, `git status --short` e, quando houver stage,
`git diff --cached --check`.

## Referencias

- `docs/roadmaps/04-interface-e-experiencia.md` - contratos de interface dual.
- `docs/roadmaps/05-sistema-e-ecossistema.md` - rede, dispositivos e ZUPD.
- `docs/roadmaps/06-app-store.md` - etapa anterior desta frente.
- `docs/melhorias futuras/mouse.md` - estado do mouse PS/2.
- `docs/melhorias futuras/gerenciador de arquivos.md` - Explorer e busca.
- `docs/melhorias futuras/gerenciador de rede.md` - base de rede existente.
- `docs/14-atualizacoes/distribuicao-remota.md` - contrato ZUM1/U5.
- `docs/ideias.md` - ideias que originaram o Roadmap 07.
