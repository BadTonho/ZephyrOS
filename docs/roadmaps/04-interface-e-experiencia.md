# Roadmap 04 - Interface e experiencia

## Objetivo

Evoluir a interface sem perder a identidade visual existente nem o fallback
classico. A interface moderna continua baseada em paineis cinza, bordas 3D,
selecao azul e fonte bitmap; gradientes e cantos arredondados nao sao a base
visual atual.

## Estado validado

- [x] Desktop moderno com icones desenhados por primitivas e clique duplo.
- [x] Explorer moderno com painel lateral, lista detalhada e fallback TUI.
- [x] Task Manager grafico e TUI de diagnostico preservada.
- [x] Settings moderno com dialogos e fallback TUI.
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
  demonstrativas do Window Manager no modo Moderno.
- [x] Limitar movimento e dimensoes a area de trabalho da taskbar, preservar
  foco, Z-order, controles de titulo e a composicao com cursor invalidado.
- [ ] Avaliar arraste de icones depois de existir hit-testing e invalidacao de
  regioes confiaveis.
- [ ] Considerar icones BMP apenas com cache, fallback desenhado e tratamento
  de filesystem indisponivel.
- [ ] Roda do mouse e melhorias de acessibilidade ficam apos a entrada basica
  estar estavel.

## Etapa UI4 - Aplicativos hospedados pelo Window Manager

- [x] Migrar Shell, Explorer, Settings e Task Manager para janelas singleton
  hospedadas pelo WM no modo Moderno, preservando seus fluxos Classicos.
- [x] Reutilizar foco, Z-order, taskbar, arraste, redimensionamento e controles
  da moldura para os aplicativos hospedados.
- [x] Manter o terminal interativo em uma superficie VESA do WM, com historico,
  scroll, refluxo ao redimensionar e fechamento que apenas oculta a janela.
- [x] Validar no QEMU os fluxos modernos e classicos, incluindo taskbar nas
  cinco posicoes, reabertura sem duplicacao e retorno seguro ao Desktop.

## Fora do escopo atual

- Tema novo, transparencia, gradientes ou redesenho total da identidade.
- API grafica para aplicativos externos.
- Persistencia de posicao de janelas ou icones sem um formato seguro de dados.

## Criterio de saida

Cada etapa deve funcionar em VESA e continuar abrindo em modo classico quando
VESA ou backbuffer nao estiverem disponiveis.
