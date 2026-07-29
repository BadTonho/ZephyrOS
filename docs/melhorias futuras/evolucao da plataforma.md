# Roadmap — Evolucao da Plataforma

> Este roteiro organiza as ideias registradas em `docs/ideias.md`. Ele amplia
> capacidades ja existentes sem substituir os contratos de VESA, FAT,
> `network_manager` ou ZUPD v1.

## Resumo de Progresso

| Etapa | Objetivo | Estado |
|---|---|---|
| EP0 | Base disponivel: VESA, mouse PS/2, FAT12/FAT32, Ethernet e ZUPD remoto | Concluida |
| EP1 | Preferencias de mouse | Planejada |
| EP2 | Video legivel e selecao segura de modo | Planejada |
| EP3 | Volumes e montagem de particoes | Planejada |
| EP4 | Indice e pesquisa de arquivos | Planejada |
| EP5 | Canal de releases baseado em tags do GitHub | Planejada |
| EP6 | Wi-Fi e Bluetooth por hardware suportado | Planejada |

## Atalhos e Comandos Planejados

| Comando | Etapa | Acao |
|---|---|---|
| `mouse speed <1-10>` | EP1 | Ajusta a sensibilidade do ponteiro. |
| `mouse primary left|right` | EP1 | Define o botao principal antes do despacho para a UI. |
| `display modes` / `display set <modo>` | EP2 | Lista modos VESA e troca de modo com confirmacao. |
| `storage list` / `storage mount <id>` | EP3 | Inspeciona volumes e monta um volume reconhecido. |
| `index status` / `index rebuild` / `search <termo>` | EP4 | Inspeciona, recria e consulta o indice. |
| `update github status|check|fetch` | EP5 | Opera o canal opcional de releases baseado em tags. |
| `wifi status|scan|connect` | EP6 | Diagnostica e conecta uma interface Wi-Fi suportada. |
| `bluetooth status|scan|pair` | EP6 | Diagnostica, procura e emparelha um dispositivo suportado. |

Nenhum comando remoto instalara atualizacoes automaticamente. As interfaces
Classic e Modern devem oferecer as mesmas operacoes, com o Shell como fallback.

## Fase EP1 — Preferencias de Mouse

- [ ] Criar uma configuracao central de sensibilidade, aceleracao opcional e
  botao principal, com valores padrao seguros.
- [ ] Aplicar o remapeamento de botao antes de Desktop, Taskbar, WM e apps
  receberem o evento; o estado bruto do driver continua observavel para debug.
- [ ] Escalar movimento com limite de tela e sem perder pacotes da fila PS/2.
- [ ] Expor os comandos `mouse speed` e `mouse primary`, o estado em
  `mouse`, e controles equivalentes em Settings Classic/Modern.
- [ ] Persistir preferencias somente depois de existir uma area de configuracao
  transacional; ate la, manter a configuracao em RAM e documentar isso.

**Criterio de saida:** clique, arrasto, roda, foco e retorno ao Shell funcionam
nos dois modos de interface, incluindo valores invalidos e mouse ausente.

## Fase EP2 — Video Legivel e Selecao Segura de Resolucao

- [ ] Reaproveitar a enumeracao VESA para listar somente modos de framebuffer
  que o driver consegue renderizar.
- [ ] Separar resolucao, escala de UI e escala de fonte: aumentar a resolucao
  nao deve tornar texto, cursores ou alvos de clique menores.
- [ ] Centralizar metricas de layout para Desktop, Taskbar, WM, Explorer,
  Settings e aplicativos, preservando o Modo Classico em texto.
- [ ] Implementar uma troca temporaria com confirmacao e reversao por timeout;
  se a confirmacao falhar, restaurar o ultimo modo conhecido como valido.
- [ ] Criar `display modes` e `display set <modo>`, mais o painel equivalente
  no Settings, com log de toda falha VESA.

**Criterio de saida:** um modo alternativo pode ser aplicado e revertido sem
perder input, cursor ou fallback Classico; nao ha dependencias do bootloader.

## Fase EP3 — Volumes, Particoes e Montagem

- [ ] Comecar por descoberta somente-leitura de MBR e por um inventario de
  discos, particoes e volumes com identificadores estaveis.
- [ ] Introduzir uma camada de volume acima de ATA, sem alterar a API FAT
  existente ate que os chamadores possam receber explicitamente o volume.
- [ ] Montar FAT12/FAT32 reconhecidos sob demanda, com limites de memoria,
  checagem de BPB, logs e erros controlados por volume.
- [ ] Mostrar os volumes montados no Explorer e em Settings, mantendo o disco
  de boot como fallback quando nenhum volume adicional for montavel.
- [ ] Planejar escrita em tabela de particoes, formatacao, redimensionamento e
  GPT como etapas posteriores, cada uma com recuperacao propria.

**Criterio de saida:** listar e montar volumes validos nao altera setores;
particao invalida ou ausente deixa boot, Shell e filesystem atual operacionais.

## Fase EP4 — Indice e Pesquisa de Arquivos

- [ ] Definir a primeira versao do indice: nome, caminho, tipo, tamanho e
  volume; pesquisa de conteudo fica fora do escopo inicial.
- [ ] Construir o indice de forma cooperativa, com orcamento por tick,
  cancelamento, progresso e limites explicitos de memoria e entradas.
- [ ] Atualizar ou invalidar entradas nas operacoes do FS e quando um volume
  for montado, desmontado ou modificado fora do sistema.
- [ ] Persistir o indice apenas apos validar uma gravacao recuperavel; ate la,
  um indice em RAM pode ser reconstruido com `index rebuild`.
- [ ] Adicionar `search <termo>` ao Shell e uma tela de pesquisa em Explorer,
  ambos capazes de informar resultados parciais ou indice desatualizado.

**Criterio de saida:** uma pesquisa limitada retorna caminhos corretos sem
travar a interface; indice corrompido e memoria insuficiente falham com log e
permitem o uso normal do filesystem.

## Fase EP5 — Releases por Tags do GitHub

- [ ] Definir no host uma politica de versao: uma tag imutavel gera o ZUPD,
  o manifesto ZUM1 assinado e hashes dos artefatos publicados.
- [ ] Criar validacao offline no empacotador para conferir que tag, versao do
  manifesto e versao minima do sistema sao coerentes antes da publicacao.
- [ ] Tratar GitHub como origem de distribuicao, nunca como raiz de confianca:
  o kernel aceita somente ZUM1 e ZUPD autenticados pelas chaves ja confiaveis.
- [ ] Adaptar o transporte remoto U5 a um canal configuravel de release e
  adicionar os comandos `update github status`, `check` e `fetch`, todos
  opt-in e sem instalar automaticamente.
- [ ] Cobrir tag inexistente, asset ausente, download interrompido, manifesto
  adulterado, pacote invalido e rollback usando fixtures locais e QEMU.

**Criterio de saida:** uma tag publicada pode ser descoberta e baixada como
cache verificavel, mas so `update apply` instala o pacote apos confirmacao.

## Fase EP6 — Wi-Fi e Bluetooth

- [ ] Inventariar controladores sem inicializar hardware e escolher um chipset
  especifico para cada driver; "Wi-Fi generico" e "Bluetooth generico" nao
  sao escopos implementaveis.
- [ ] Planejar primeiro o transporte necessario (PCI/PCIe ou USB), DMA/IRQ,
  firmware, logs e diagnosticos antes de associacao ou emparelhamento.
- [ ] Integrar uma interface Wi-Fi suportada a `network_manager`, reutilizando
  Ethernet, IPv4, DHCP, DNS, TCP e HTTP sem duplicar a pilha IP.
- [ ] Implementar Bluetooth sobre HCI para um controlador escolhido, com
  descoberta e emparelhamento minimo antes de perfis como audio ou HID.
- [ ] Validar com hardware ou emulacao que represente o chipset alvo; quando
  indisponivel, manter o componente degradado e o sistema cabeado funcional.

**Criterio de saida:** ausencia de radio, firmware ou driver produz erro
controlado; rede Ethernet, atualizacoes locais e interfaces Classic/Modern
continuam utilizaveis.

## Limitacoes e Dependencias

- Nenhuma etapa altera `src/boot/boot.asm`.
- EP3 precede a persistencia do indice de EP4; a primeira entrega de pesquisa
  pode permanecer somente em RAM.
- EP5 aproveita U5, mas GitHub normalmente exige HTTPS. Enquanto o ZephyrOS
  nao possuir TLS, relogio confiavel e validacao de certificados, a integracao
  direta com `github.com` nao deve ser habilitada no kernel. O fluxo inicial
  deve publicar artefatos assinados e usar um transporte compativel ou ser
  validado no host.
- Nenhum token, senha, conta GitHub ou credencial de Wi-Fi deve ser embutido
  em imagens, logs, fixtures ou repositorio.
- Wi-Fi e Bluetooth dependem de hardware especifico e provavelmente de uma
  base USB ainda inexistente; eles nao bloqueiam a evolucao de rede cabeada.
- Cada capacidade executavel deve registrar comandos no Shell, logs de falha,
  testes de regressao e fallback Classic/Modern.

## Referencias

- `docs/ideias.md` — origem deste roteiro.
- `docs/melhorias futuras/mouse.md` — estado do driver e cursor PS/2.
- `docs/melhorias futuras/configurações.md` — Settings Classic/Modern.
- `docs/08-sistema-arquivos/sistema-arquivos.md` — FAT12, FAT32 e FS atual.
- `docs/melhorias futuras/gerenciador de arquivos.md` — Explorer e busca.
- `docs/melhorias futuras/gerenciador de rede.md` — base Ethernet e rede.
- `docs/melhorias futuras/atualizações.md` — contrato e transporte ZUPD/U5.
- `docs/14-atualizacoes/distribuicao-remota.md` — manifesto remoto ZUM1.
- `docs/regras.md` — logs, erros e criterios de qualidade.
