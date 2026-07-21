# Roadmap: Modernização da GUI (Fase 10)

O sistema operacional ZephyrOS possui todos os elementos para uma interface gráfica (VESA, Mouse PS/2, Decoder de imagens BMP). O objetivo desta fase é abandonar a renderização atual baseada em texto (TUI) e substituí-la por primitivas gráficas 2D modernas.

## Resumo de Progresso

| Etapa | Componente | Status |
|---|---|---|
| 1 | **Primitivas 2D (`vesa.c` / `video.c`)**: Adicionar `vesa_draw_rect_filled`, `vesa_draw_circle`, sombras e gradientes. | Planejado |
| 2 | **Texto Gráfico (`font.c`)**: Permitir `video_print_pixel(x, y, string, color)`, livrando-se da grade de caracteres fixos. | Planejado |
| 3 | **Mouse Interativo (`wm.c` / `desktop.c`)**: Transformar coordenadas e implementar Drag and Drop (arrastar janelas). | Planejado |
| 4 | **Desktop Real (`desktop.c`)**: Substituir as caixas de texto de ícones por renderização de imagens BMP. | Planejado |
| 5 | **Taskbar Moderna (`taskbar.c`)**: Desenho com gradientes e botões planos. | Planejado |
| 6 | **Windows Decorator (`wm.c`)**: Janelas com titlebar desenhadas via pixel art ou preenchimento de cores. | Planejado |

## Detalhes das Fases

### Fase 1: Motor Gráfico Básico
Atualmente a interface está amarrada aos métodos `video_put_char_at` da VGA text mode (que usa posições fixas de coluna e linha `col = px/8, row = py/16`).
O foco inicial é parar de chamar `video_put_char` para a UI, construindo funções como:
- `gui_draw_button(x, y, w, h, text, color)`
- `gui_draw_window_frame(x, y, w, h, title)`
- Modificação das fontes para aceitar coordenadas exatas de pixel `(x, y)`.

### Fase 2: Input Universal (Mouse)
Atualmente o Window Manager e as interfaces utilizam muito o teclado (`wm_handle_key`). Precisamos interligar o `mouse_event_t` ao:
- **Hover**: Saber quando o ponteiro do mouse está sobre um botão para iluminá-lo.
- **Click (Press)**: Saber onde ocorreu o clique e focar na janela.
- **Drag (Move + Press)**: Atualizar X,Y das janelas enquanto o mouse é movido com o botão pressionado na Titlebar.

### Fase 3: Substituição Visual
Nesta etapa, apagaremos os arquivos antigos (ou os reformularemos) para tirar as dependências de TUI. 
O *Desktop* usará os arquivos `shell.bmp`, `folder.bmp` do disco para renderizar os ícones de forma bonita, utilizando transparência se possível (Alpha blending no BMP).

## Limitações Atuais e Atenções
1. **Performance VESA:** A renderização VESA por software é lenta. Atualizar a tela inteira com milhares de pixels a cada frame causa *flickering* (cintilação) e lag no mouse. **Solução:** Utilizar *Double Buffering* na renderização gráfica. Desenha tudo num buffer em RAM, depois copia para a memória de vídeo (LFB) de uma só vez.
2. **Clipping:** Ao sobrepor janelas, precisamos implementar `clipping` ou redesenho com "sujeira" (dirty rectangles) para não sobrecarregar a CPU renderizando o que está escondido.

## Referências
- OSDev Wiki - Double Buffering
- OSDev Wiki - Windowing Systems e Compositing
- Arquitetura de eventos UI (DOM like events: mouse down, hover, up).
