# Roadmap: Modernização da GUI (Fase 10)

O sistema operacional ZephyrOS possui todos os elementos para uma interface gráfica (VESA, Mouse PS/2, Decoder de imagens BMP). O objetivo desta fase é abandonar a renderização atual baseada em texto (TUI) e substituí-la por primitivas gráficas 2D modernas.

### Decisão Arquitetural: Linguagem
- **Etapa Atual (C Puro):** A GUI será construída inteiramente em C para garantir máxima performance, fácil integração com o Kernel atual e evitar complexidades de *runtime* (como inicialização global de objetos e runtime errors). Usaremos uma abordagem *C Object-Oriented* (ex: `ui_create_window()`, `ui_button_draw()`) para manter a organização.
- **Etapa Futura (C++):** Quando a arquitetura do SO amadurecer (com chamadas de sistema, User-Space/Ring 3 e uma biblioteca padrão C - `libc`), planejaremos a migração do código da UI para C++ visando aproveitar Orientação a Objetos nativa e frameworks robustos.

## Resumo de Progresso

| Etapa | Componente | Status |
|---|---|---|
| 1 | **Primitivas 2D (`gui.c`)**: `gui_draw_panel`, `gui_draw_button`, `gui_draw_window_frame`, `gui_draw_text`. | ✅ Concluído |
| 2 | **Desktop Gráfico (`desktop.c`)**: Cards 3D, modo classic/modern, layout responsivo e fallback TUI. | ✅ Concluído |
| 3 | **Mouse no Desktop (`desktop.c`)**: Seleção por clique e abertura por duplo clique. | ✅ Concluído |
| 4 | **Mouse Interativo (`wm.c`)**: Hover, foco e Drag and Drop de janelas. | Planejado |
| 5 | **Desktop com BMP (`desktop.c`)**: Substituir símbolos por imagens carregadas do disco. | Planejado |
| 6 | **Taskbar Moderna (`taskbar.c`)**: Evoluir o visual atual com novos estilos. | Planejado |
| 7 | **Windows Decorator (`wm.c`)**: Janelas com titlebar desenhadas via primitivas gráficas. | Planejado |

## Detalhes das Fases

### Fase 1: Motor Gráfico Básico ✅
Implementado em `src/gui/gui.c` com as seguintes primitivas:
- `gui_draw_text(x, y, text, color)` - Renderiza texto pixel a pixel via fonte bitmap
- `gui_draw_button(x, y, w, h, text, pressed)` - Botão com estado pressed/released
- `gui_draw_window_frame(x, y, w, h, title, active)` - Moldura de janela com barra de título
- Comando `guitest` no shell para testar as primitivas

### Fase 2: Desktop Gráfico ✅
O Desktop passou a oferecer uma interface gráfica compatível com a identidade visual existente:
- Cards com fundo cinza, bordas 3D e seleção azul
- Grade alinhada à esquerda com posições calculadas conforme a resolução VESA
- Modos `classic` e `modern`, com fallback automático quando VESA não está disponível
- Comando `guimode classic|modern` no shell
- Símbolos desenhados por primitivas, sem dependência de arquivos BMP

### Fase 3: Input do Desktop ✅
O Desktop moderno agora recebe eventos gráficos do mouse:
- Clique esquerdo seleciona um card
- Clique em área vazia remove a seleção
- Duplo clique em até 500 ms abre o aplicativo
- O cursor é invalidado antes de redesenhos completos para evitar artefatos no backbuffer

### Fase 4: Input Universal (Mouse)
Atualmente o Window Manager e as interfaces utilizam muito o teclado (`wm_handle_key`). Precisamos interligar o `mouse_event_t` ao:
- **Hover**: Saber quando o ponteiro do mouse está sobre um botão para iluminá-lo.
- **Click (Press)**: Saber onde ocorreu o clique e focar na janela.
- **Drag (Move + Press)**: Atualizar X,Y das janelas enquanto o mouse é movido com o botão pressionado na Titlebar.

### Fase 5: Substituição Visual
Nesta etapa, apagaremos os arquivos antigos (ou os reformularemos) para tirar as dependências de TUI. 
O *Desktop* usará os arquivos `shell.bmp`, `folder.bmp` do disco para renderizar os ícones de forma bonita, utilizando transparência se possível (Alpha blending no BMP).

## Limitações Atuais e Atenções
1. **Performance VESA:** A renderização VESA por software é lenta. Atualizar a tela inteira com milhares de pixels a cada frame causa *flickering* (cintilação) e lag no mouse. **Solução:** Utilizar *Double Buffering* na renderização gráfica. Desenha tudo num buffer em RAM, depois copia para a memória de vídeo (LFB) de uma só vez.
2. **Clipping:** Ao sobrepor janelas, precisamos implementar `clipping` ou redesenho com "sujeira" (dirty rectangles) para não sobrecarregar a CPU renderizando o que está escondido.

## Referências
- OSDev Wiki - Double Buffering
- OSDev Wiki - Windowing Systems e Compositing
- Arquitetura de eventos UI (DOM like events: mouse down, hover, up).
