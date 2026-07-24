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

- [ ] Corrigir apenas inconsistencias visuais ou de entrada observadas nos
  fluxos atuais antes de criar novos componentes.
- [ ] Garantir que taskbar e Menu Iniciar mantenham prioridade sobre janelas e
  aplicativos externos.
- [ ] Preservar uma unica atualizacao de frame por cena e evitar rastros de
  cursor.

## Etapa UI2 - Taskbar e Window Manager grafico

- [ ] Modernizar a taskbar sem mudar sua semantica de botoes, relogio e Menu
  Iniciar.
- [ ] Integrar o Window Manager textual a uma camada grafica somente apos
  definir propriedade, foco e ordem de desenho das janelas.
- [ ] Manter Task Manager, Explorer e Settings funcionais durante a transicao.

## Etapa UI3 - Interacao direta

- [ ] Avaliar arraste de icones e janelas depois de existir hit-testing e
  invalidacao de regioes confiaveis.
- [ ] Considerar icones BMP apenas com cache, fallback desenhado e tratamento
  de filesystem indisponivel.
- [ ] Roda do mouse e melhorias de acessibilidade ficam apos a entrada basica
  estar estavel.

## Fora do escopo atual

- Tema novo, transparencia, gradientes ou redesenho total da identidade.
- API grafica para aplicativos externos.
- Persistencia de posicao de janelas ou icones sem um formato seguro de dados.

## Criterio de saida

Cada etapa deve funcionar em VESA e continuar abrindo em modo classico quando
VESA ou backbuffer nao estiverem disponiveis.
