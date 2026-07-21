# Roadmap: Modernização da GUI (Fase 10)

O sistema operacional ZephyrOS possui todos os elementos para uma interface gráfica (VESA, Mouse PS/2, Decoder de imagens BMP). O objetivo desta fase é abandonar a renderização atual baseada em texto (TUI) e substituí-la por primitivas gráficas 2D modernas.

### Decisão Arquitetural: Linguagem
- **Etapa Atual (C Puro):** A GUI será construída inteiramente em C para garantir máxima performance, fácil integração com o Kernel atual e evitar complexidades de *runtime* (como inicialização global de objetos e runtime errors). Usaremos uma abordagem *C Object-Oriented* (ex: `ui_create_window()`, `ui_button_draw()`) para manter a organização.
- **Etapa Futura (C++):** Quando a arquitetura do SO amadurecer (com chamadas de sistema, User-Space/Ring 3 e uma biblioteca padrão C - `libc`), planejaremos a migração do código da UI para C++ visando aproveitar Orientação a Objetos nativa e frameworks robustos.

## Resumo de Progresso

| Etapa | Componente | Status |
|---|---|---|
| 1 | **Primitivas 2D (`gui.c`)**: `gui_draw_button`, `gui_draw_window_frame`, `gui_draw_text` implementados. | ✅ Concluído |
| 2 | **Mouse Interativo (`wm.c` / `desktop.c`)**: Transformar coordenadas e implementar Drag and Drop (arrastar janelas). | Planejado |
| 3 | **Desktop Real (`desktop.c`)**: Substituir as caixas de texto de ícones por renderização de imagens BMP. | Planejado |
| 4 | **Taskbar Moderna (`taskbar.c`)**: Desenho com gradientes e botões planos. | Planejado |
| 5 | **Windows Decorator (`wm.c`)**: Janelas com titlebar desenhadas via pixel art ou preenchimento de cores. | Planejado |

## Detalhes das Fases

### Fase 1: Motor Gráfico Básico ✅
Implementado em `src/gui/gui.c` com as seguintes primitivas:
- `gui_draw_text(x, y, text, color)` - Renderiza texto pixel a pixel via fonte bitmap
- `gui_draw_button(x, y, w, h, text, pressed)` - Botão com estado pressed/released
- `gui_draw_window_frame(x, y, w, h, title, active)` - Moldura de janela com barra de título
- Comando `guitest` no shell para testar as primitivas

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
