# Roadmap 04 - Interface e experiencia

## Objetivo

Evoluir a interface sem perder a identidade visual existente nem o fallback
Simple. A interface Classic continua baseada em paineis cinza, bordas 3D,
selecao azul e fonte bitmap; gradientes e cantos arredondados nao sao a base
visual atual.

## Estado validado

- [x] Desktop Classic com icones desenhados por primitivas e clique duplo.
- [x] Explorer Classic com painel lateral, lista detalhada e fallback TUI.
- [x] Task Manager grafico e TUI de diagnostico preservada.
- [x] Settings Classic com dialogos e fallback TUI.
- [x] Backbuffer, invalidacao de cursor e ciclos de renderizacao controlados.

## Etapa UI1 - Consolidacao visual

- [x] Corrigir apenas inconsistencias visuais ou de entrada observadas nos
  fluxos atuais antes de criar novos componentes.
- [x] Garantir que taskbar e Menu Iniciar mantenham prioridade sobre janelas e
  aplicativos externos.
- [x] Preservar uma unica atualizacao de frame por cena e evitar rastros de
  cursor.

## Etapa UI2 - Taskbar e Window Manager grafico

- [x] Modernizar a taskbar sem mudar sua semantica de botoes, relogio e Menu
  Iniciar.
- [x] Integrar o Window Manager textual a uma camada grafica somente apos
  definir propriedade, foco e ordem de desenho das janelas.
- [x] Manter Task Manager, Explorer e Settings funcionais durante a transicao.

## Etapa UI3 - Interacao direta

- [x] Implementar arraste e redimensionamento ao vivo nas duas janelas
  demonstrativas do Window Manager no modo Classic.
- [x] Limitar movimento e dimensoes a area de trabalho da taskbar, preservar
  foco, Z-order, controles de titulo e a composicao com cursor invalidado.
- [x] Avaliar arraste de icones depois de existir hit-testing e invalidacao de
  regioes confiaveis.
- [x] Separar roda do mouse e acessibilidade para a etapa UI7, apos a entrada
  basica estar estavel.

## Etapa UI4 - Aplicativos hospedados pelo Window Manager

- [x] Migrar Shell, Explorer, Settings e Task Manager para janelas singleton
  hospedadas pelo WM no modo Classic, preservando seus fluxos Simple.
- [x] Reutilizar foco, Z-order, taskbar, arraste, redimensionamento e controles
  da moldura para os aplicativos hospedados.
- [x] Manter o terminal interativo em uma superficie VESA do WM, com historico,
  scroll, refluxo ao redimensionar e fechamento que apenas oculta a janela.
- [x] Validar no QEMU os fluxos Classic e Simple, incluindo taskbar nas
  cinco posicoes, reabertura sem duplicacao e retorno seguro ao Desktop.

## Etapa UI5 - Icones interativos do Desktop

- [x] Implementar arraste de icones apenas no Desktop Classic, com grade de
  slots em memoria, encaixe no proximo slot livre e sem sobreposicao.
- [x] Respeitar a area util das taskbars acopladas e reservar a taskbar
  personalizada para que nenhum icone fique sob ela.
- [x] Manter os icones como fundo das janelas hospedadas e restaurar o Desktop
  interativo ao fechar a ultima janela hospedada.
- [x] Validar no QEMU o arraste, o clique duplo, as cinco posicoes da taskbar
  e a ausencia de regressao no modo Simple.

## Etapa UI6 - Icones BMP com cache

- [x] Carregar BMPs de Shell, Explorer e Task Manager apenas no Desktop
  Classic, com chave magenta transparente e cache em memoria.
- [x] Injetar os tres assets na imagem FAT12 e manter os simbolos desenhados
  como fallback para filesystem, arquivo, formato ou memoria indisponiveis.
- [x] Validar no QEMU os BMPs, o comando `icons`, o fallback e a ausencia de
  regressao no modo Simple antes de concluir a etapa.

## Etapa UI7 - Roda PS/2 e acessibilidade de janelas

- [x] Negociar Intellimouse no driver PS/2, publicar eventos de roda vertical
  e manter fallback seguro de tres bytes quando o hardware nao oferecer suporte.
- [x] Entregar a roda apenas ao conteudo da janela superior sob o cursor, sem
  alterar foco ou Z-order e preservando a prioridade da taskbar e do Menu
  Iniciar.
- [x] Aplicar scroll ao Shell, Explorer e Task Manager e manter Settings sem
  acao de roda.
- [x] Adicionar `Alt+Tab`, `Alt+Shift+Tab`, `Alt+F4`, `Alt+F9` e `Alt+F10` ao
  WM Classic, com contorno azul de foco e sem regressao dos atalhos Simple.
- [x] Validar no QEMU a roda, os atalhos, taskbar nas cinco posicoes e os
  fluxos essenciais dos modos Classic e Simple antes de concluir a etapa.

## Fora do escopo atual

- Tema novo, transparencia, gradientes ou redesenho total da identidade.
- API grafica para aplicativos externos.
- Persistencia de posicao de janelas ou icones sem um formato seguro de dados.

## Criterio de saida

Cada etapa deve funcionar no Classic VESA e continuar abrindo em modo Simple quando
VESA ou backbuffer nao estiverem disponiveis.
