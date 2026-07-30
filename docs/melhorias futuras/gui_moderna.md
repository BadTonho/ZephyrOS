# Roadmap histórico: GUI Classic (Fase 10)

O ZephyrOS mantem a interface Simple como fallback e oferece uma interface
Classic baseada em VESA, mouse PS/2, primitivas 2D e imagens BMP. As duas
interfaces coexistem; a modernizacao nao remove os fluxos TUI.
O nome Modern agora fica reservado ao redesenho futuro descrito no Roadmap 07.

### Decisão Arquitetural: Linguagem
- **Etapa Atual (C Puro):** A GUI será construída inteiramente em C para garantir máxima performance, fácil integração com o Kernel atual e evitar complexidades de *runtime* (como inicialização global de objetos e runtime errors). Usaremos uma abordagem *C Object-Oriented* (ex: `ui_create_window()`, `ui_button_draw()`) para manter a organização.
- **Etapa Futura (C++):** Quando a arquitetura do SO amadurecer (com chamadas de sistema, User-Space/Ring 3 e uma biblioteca padrão C - `libc`), planejaremos a migração do código da UI para C++ visando aproveitar Orientação a Objetos nativa e frameworks robustos.

## Resumo de Progresso

| Etapa | Componente | Status |
|---|---|---|
| 1 | **Primitivas 2D (`gui.c`)**: `gui_draw_panel`, `gui_draw_button`, `gui_draw_window_frame`, `gui_draw_text`. | ✅ Concluído |
| 2 | **Desktop Gráfico (`desktop.c`)**: Cards 3D, modo simple/classic, layout responsivo e fallback TUI. | ✅ Concluído |
| 3 | **Mouse no Desktop (`desktop.c`)**: Seleção por clique e abertura por duplo clique. | ✅ Concluído |
| 4 | **Mouse Interativo (`wm.c`)**: Foco, arraste e redimensionamento de janelas. | ✅ Concluído |
| 5 | **Desktop com BMP (`desktop.c`)**: Imagens com cache e fallback desenhado. | ✅ Concluído |
| 6 | **Taskbar Classic (`taskbar.c`)**: Redesenho gráfico preservando a semântica atual. | ✅ Concluído |
| 7 | **Windows Decorator (`wm.c`)**: Janelas com titlebar desenhadas via primitivas gráficas. | ✅ Concluído |
| 8 | **Double Buffering (`vesa.c`)**: Renderização suave e sem cintilação via backbuffer na RAM. | ✅ Concluído |
| 9 | **Aplicativos hospedados**: Shell, Explorer, Settings e Task Manager em janelas singleton. | ✅ Concluído |
| 10 | **Acessibilidade**: Roda PS/2 e atalhos de gerenciamento de janelas. | ✅ Concluído |

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
- Modos `simple` e `classic`, com fallback automático quando VESA não está disponível
- Comando `guimode simple|classic` no shell
- Símbolos desenhados por primitivas, sem dependência de arquivos BMP

### Fase 3: Input do Desktop ✅
O Desktop classic agora recebe eventos gráficos do mouse:
- Clique esquerdo seleciona um card
- Clique em área vazia remove a seleção
- Duplo clique em até 500 ms abre o aplicativo
- O cursor é invalidado antes de redesenhos completos para evitar artefatos no backbuffer

### Fase 4: Barra de Tarefas Classic (GUI) ✅

- Preserva os botões, relógio e Menu Iniciar nas cinco posições suportadas.
- Reutiliza a identidade atual: cinza, bordas 3D, seleção azul e fonte bitmap.
- Integra o desenho ao ciclo de frame sem múltiplos flips nem perda da
  prioridade de clique.

### Fase 5: Input Universal (Mouse) ✅

- Clique define foco e Z-order sem retirar a prioridade da taskbar.
- Arraste e redimensionamento ao vivo respeitam a área útil do Desktop.
- Ícones do Desktop usam grade sem sobreposição.
- A roda PS/2 é entregue ao conteúdo da janela superior sob o cursor.

### Fase 6: Ícones e detalhes visuais ✅

O Desktop Classic carrega BMPs de Shell, Explorer e Task Manager com chave
magenta e cache. Falhas de filesystem, formato ou memoria preservam os
simbolos desenhados. O modo Simple permanece independente desses arquivos.

## Limitações Atuais e Atenções
1. **Performance VESA Resolvida (Double Buffering):** O problema de *flickering* (cintilação) foi resolvido através da implementação de um backbuffer em memória RAM (`vesa_init_backbuffer`). Agora, a tela é copiada de uma só vez para a VRAM (`vesa_flip`).
2. **Escopo visual:** Transparencia, alpha blending, gradientes e um novo tema
   permanecem fora do escopo validado.

## Referências
- OSDev Wiki - Double Buffering
- OSDev Wiki - Windowing Systems e Compositing
- Arquitetura de eventos UI (DOM like events: mouse down, hover, up).
