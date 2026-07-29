# Roadmap 07 - Modernizacao Visual

## Objetivo

Prover uma interface grafica moderna (Flat Design, cantos arredondados, paleta de cores dark/moderna e efeitos visuais sutis) para o ZephyrOS, preparando o subsistema grafico para novas telas de forma modular e altamente otimizada, sem comprometer a taxa de quadros (FPS) da renderização em CPU.

Esta frente inicia apos a conclusao do Roadmap 06 (App Store) e serve como base visual e de arquitetura para as interfaces que virao no Roadmap 08.

## Base ja validada

- [x] Framebuffer VESA LFB com Double Buffering (Backbuffer) e Vesa Flip ativos.
- [x] Primitivas graficas 2D (pixel, retangulo, linha, texto bitmap) em `src/gui/gui.c`.
- [x] Desktop, Taskbar, Window Manager, Explorer, Settings e Task Manager com suporte a janelas Modern.
- [x] Roteamento de eventos de teclado, mouse, Z-order e foco de janelas estaveis.

Estas capacidades continuam sendo a fonte de verdade. Este roadmap atua estritamente na refatoracao visual e evolucao das funcoes de desenho em `src/gui/gui.c` e nos componentes de interface.

## Decisoes de produto

- **Classic Mode (TUI) permanece intocado como fallback operacional.** Nenhuma mudanca grafica no modo Modern deve quebrar ou alterar a renderizacao em modo texto.
- **Adeus ao visual Windows 95**: A identidade visual moderna adotará Flat Design, cantos arredondados, bordas de 1px e paleta escura (Dark Mode). Os relevos e chanfros 3D grossos cinzas serao removidos do modo Modern.
- **Otimizacao em CPU em primeiro lugar**: Como nao ha aceleracao por GPU, efeitos complexos de transparencia (alpha blending) e cantos arredondados devem usar mascaras de bits pré-calculadas ou equacoes simplificadas para evitar divisoes/calculos de ponto flutuante em loops de rendering.
- **Sem alteracoes na App API ou no loader ZAPP**: As mudancas focam puramente no desenho e apresentacao das janelas e componentes do kernel.

## Ordem de dependencia

1. Evolucao das primitivas graficas em `gui.c` (cantos arredondados e bordas flat).
2. Defincao do novo sistema de cores global (Dark Mode moderno).
3. Redesenho da moldura de janelas no Window Manager e da barra de tarefas.
4. Refatoracao visual dos aplicativos nativos (Explorer, Settings, Task Manager).
5. Otimizacao e validacao de desempenho de rendering (FPS).

## MV1 - Evolucao das Primitivas Graficas

### Implementacao

- [ ] Modificar `src/gui/gui.c` e `src/include/ui/gui.h` para introduzir:
  - `gui_draw_rounded_rect`: preenchimento de retangulos com cantos arredondados de raio parametrizado.
  - `gui_draw_flat_border`: borda simples de 1 pixel (substituindo o chanfro 3D clássico).
  - `gui_draw_vertical_gradient`: gradientes de duas cores muito leves para barras de titulo.
- [ ] Otimizar os algoritmos de circulo/cantos arredondados para usar equacoes incrementais de ponto fixo (como o algoritmo de circulo de midpoint de Bresenham) para evitar processamento pesado.
- [ ] Adicionar um comando diagnostico `guitest modern` no Shell para renderizar e inspecionar visualmente as novas primitivas e gradientes.

### Criterio de saida

Novas primitivas desenham formas sem artefatos visuais ou estouro de limites. O comando `guitest modern` executa e exibe as formas perfeitamente nas resolucoes VESA testadas, sem travar o kernel.

## MV2 - Novo Sistema de Cores e Estilos (Tema Dark Modern)

### Implementacao

- [ ] Definir a paleta de cores global do tema moderno no header `src/include/ui/gui.h` utilizando constantes:
  - Fundo principal: Cinza-escuro/Chumbo (ex: `#1E1E24`).
  - Barras de titulo e janelas: Cinza-azulado escuro (ex: `#2A2B36`).
  - Cor de acento (foco): Ciano ou Azul Neon (ex: `#00ADB5`).
  - Texto principal: Branco ou Cinza-claro (ex: `#EEEEEE`).
  - Bordas de janela ativa/inativa.
- [ ] Padronizar `gui_draw_button` para desenhar botoes com cantos ligeiramente arredondados (raio de 3 a 4 pixels), sem relevo 3D cinza, usando a nova paleta para os estados Normal, Hover (Foco) e Pressed (Pressionado).
- [ ] Registrar o novo tema de cores no Settings, permitindo futuramente alternar paletas (sem persistência nesta fase).

### Criterio de saida

Botoes e controles desenham com o novo design Flat e Dark. A alternancia de estados de foco e clique responde visualmente de forma imediata.

## MV3 - Redesenho do Window Manager e Desktop

### Implementacao

- [ ] Refatorar a funcao de desenho de moldura de janela no Window Manager (`wm_draw_window_frame` ou equivalente em `src/wm/wm.c`):
  - Aplicar cantos arredondados no topo da moldura.
  - Substituir os botoes `[_][X]` classicos por icones minimalistas de controle de janela (ex: um circulo ou tracos finos).
  - Usar gradiente suave na barra de titulo da janela ativa.
- [ ] Atualizar o visual do Desktop:
  - Fundo padrao em tom escuro harmonioso ou gradiente simples caso nao haja bitmap de wallpaper.
  - Icones com bordas arredondadas e selecao azul-neon flat.
- [ ] Aplicar transparancia simulada (Alpha Blending estatico) sutil na Barra de Tarefas:
  - Mesclar o background da barra com os pixels de fundo do wallpaper uma unica vez no boot ou na atualizacao do wallpaper para evitar processamento continuo.

### Criterio de saida

O Desktop e o gerenciador de janelas adotam integralmente o novo visual sem rastro de pixels, piscadas (flickering) ou perda perceptivel de performance ao arrastar janelas no QEMU.

## MV4 - Modernizacao de Aplicativos e Performance

### Implementacao

- [ ] Refatorar o desenho das telas internas dos aplicativos nativos hospedados no modo Modern:
  - **Explorer Moderno**: Painéis de arquivos flat, linhas de grade sutis e selecao moderna.
  - **Settings Moderno**: Categorias organizadas em cards modernos com bordas arredondadas.
  - **Task Manager Moderno**: Graficos de performance desenhados com linhas finas coloridas sobre fundo escuro.
- [ ] Otimizar o rendering de fontes pixel a pixel para suportar espacamento proporcional basico se necessario, ou melhorar o contraste das fontes bitmap contra o fundo escuro.
- [ ] Realizar teste de performance comparativo: coletar ticks de atualizacao de tela via `kmetrics` e garantir que o tempo de redesenho de janela nao subiu mais do que 10% em comparacao com o visual classic.

### Criterio de saida

Explorer, Settings e Task Manager abrem e funcionam com o novo visual. O redesenho de janelas arrastadas se mantem fluido e o benchmark `kmetrics` valida que a sobrecarga visual na CPU esta dentro do limite seguro.

## Validacao por etapa

O desenvolvedor devera orientar o usuario a rodar:

```text
make q3check
make clean && make
make run
```

No QEMU, validar visualmente a transicao para o modo modern com `guimode modern`. A interface deve aparecer com o tema Dark, cantos arredondados nas janelas, botoes modernos e sem artefatos ou lentidao severa.
