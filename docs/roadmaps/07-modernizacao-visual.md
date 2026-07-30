# Roadmap 07 - Modernizacao Visual

## Objetivo

Prover uma interface grafica moderna (Flat Design, cantos arredondados, paleta de cores dark/moderna e efeitos visuais sutis) para o ZephyrOS. Antes do redesenho, esta frente estabelece metricas de layout e escala que mantem a interface legivel no modo VESA atual, sem depender de FPS ou de aceleracao por GPU.

Esta frente e intercalada com o Roadmap 06 (App Store): MV0-MV3 iniciam depois
de AS1-AS2 e devem terminar antes de AS3. Assim, a App Store nasce sobre a
fundacao visual nova em vez de precisar de um segundo redesenho. MV4 ocorre
depois de AS3 e tambem valida a App Store. Esta frente ainda serve como base
visual e de arquitetura para as interfaces que virao no Roadmap 08.

## Base ja validada

- [x] Framebuffer VESA LFB com Double Buffering (Backbuffer) e Vesa Flip ativos.
- [x] Primitivas graficas 2D (pixel, retangulo, linha, texto bitmap) em `src/gui/gui.c`.
- [x] Desktop, Taskbar, Window Manager, Explorer, Settings e Task Manager com suporte a janelas Classic.
- [x] Roteamento de eventos de teclado, mouse, Z-order e foco de janelas estaveis.

Estas capacidades continuam sendo a fonte de verdade. Este roadmap atua estritamente na refatoracao visual e evolucao das funcoes de desenho em `src/gui/gui.c` e nos componentes de interface.

## Decisoes de produto

- **Simple Mode (TUI) permanece congelado como fallback operacional.** Recebe
  somente correcoes criticas e um smoke test de video, teclado e Shell.
- **Classic Mode e a GUI VESA principal atual.** MV0 aplica escala e metricas
  nessa rota, que concentra a matriz funcional obrigatoria.
- **Modern Mode esta reservado para o futuro.** Ele nao e selecionavel ate que
  o redesenho realmente moderno esteja implementado.
- **Adeus ao visual Windows 95**: A futura identidade Modern adotará Flat
  Design, cantos arredondados, bordas de 1px e paleta escura (Dark Mode). Os
  relevos e chanfros 3D grossos cinzas pertencem ao Classic atual.
- **Otimizacao em CPU em primeiro lugar**: Como nao ha aceleracao por GPU, efeitos complexos de transparencia (alpha blending) e cantos arredondados devem usar mascaras de bits pré-calculadas ou equacoes simplificadas para evitar divisoes/calculos de ponto flutuante em loops de rendering.
- **Sem alteracoes na App API ou no loader ZAPP**: As mudancas focam puramente no desenho e apresentacao das janelas e componentes do kernel.
- **Troca de modo VESA em runtime fica fora do escopo**: a API atual a recusa. Esta frente oferece escala dentro do modo inicial; escolher outra resolucao exige um projeto separado para boot/stage e aprovacao explicita antes de qualquer alteracao nesses componentes.
- **Medicao reproduzivel, nao FPS estimado**: desempenho sera comparado por cenas fixas, bytes apresentados e ticks de copia VESA do `kmetrics`.

## Ordem de dependencia

1. AS1-AS2 do Roadmap 06: backend e Shell da App Store aprovados.
2. Metricas de layout, escala de fonte e alvos de interacao no Classic VESA.
3. Evolucao das primitivas graficas para o futuro Modern (cantos arredondados
   e bordas flat).
4. Definicao do novo sistema de cores global do Modern.
5. Implementacao da moldura Modern no Window Manager, Desktop e Taskbar.
6. AS3 do Roadmap 06: App Store Modern sobre a fundacao MV0-MV3.
7. Refatoracao visual dos aplicativos nativos, incluindo a App Store.
8. Otimizacao e validacao de desempenho por cenas reproduziveis.
9. AS4-AS5 do Roadmap 06: evolucoes posteriores ao MVP.

## MV0 - Layout e escala acessiveis

**Estado:** implementado e validado pelo usuario em 30/07/2026. `make q3check`,
build completo e matriz QEMU foram aprovados com VESA 1024x768x24 e
backbuffer ativo.

### Implementacao

- [x] Definir metricas centrais para espacamento, altura de barra, dimensoes
  minimas de botoes, tamanho de icones e escala da fonte bitmap, calculadas a
  partir do modo VESA inicial.
- [x] Adicionar `display status` e `display scale <pequena|normal|grande>`;
  os comandos mostram e alteram somente a escala em RAM, nunca a resolucao.
- [x] Aplicar essas metricas em Desktop, Taskbar, Window Manager, Explorer,
  Settings e Task Manager no modo Classic antes do redesenho Dark/Flat.
- [x] Garantir que textos, cursor e controles continuem dentro do framebuffer
  em cada escala e que o Modo Simple nao seja alterado.
- [x] Expor a escala no Settings Classic e no fallback Simple, com logs para
  entradas invalidas ou layout que nao caiba no modo ativo.

### Criterio de saida

As tres escalas mantem texto legivel e alvos de clique utilizaveis no modo VESA
inicial. Falha de VESA, escala invalida ou area insuficiente preserva a escala
anterior e o fallback Simple.

### Matriz QEMU aprovada

1. `make q3check`, `make clean && make` e `make run` aprovados pelo usuario.
2. `display status` iniciou em `normal`, com VESA 1024x768x24 e backbuffer
   ativo. O MV0 preservou o modo de video existente e nao alterou o boot.
3. As escalas pequena, normal e grande foram aprovadas pelo Shell e Settings.
4. Desktop, cinco posicoes da Taskbar, Menu Iniciar, Explorer, Settings e
   todas as abas do Task Manager foram aprovados nos tres presets.
5. Clique, selecao, arraste, resize, minimizar, maximizar e textos proximos
   das bordas foram aprovados.
6. Sintaxe invalida e area insuficiente preservaram a escala anterior.
7. O smoke test do fallback Simple confirmou video, teclado e Shell; o retorno
   ao Classic preservou a escala em RAM.
8. `health summary`, `memcheck` e `regcheck full` foram executados; MemCheck e
   RegCheck terminaram em `OK`, sem processos, memoria ou pacotes residuais.

## MV0.1 - Tipografia bitmap legivel

**Estado:** implementacao presente no repositorio; aguardando `make q3check`,
build completo e aprovacao da matriz QEMU pelo usuario.

### Implementacao

- [x] Incorporar as faces normais 8x16, 10x20 e 12x24 da familia derivada
  `Zephyr UI Bitmap`, com BDFs, hashes, gerador deterministico e OFL 1.1.
- [x] Preservar a fonte e as APIs legadas 8x16 para Simple, Shell hospedado,
  Updater e primitivas fora do MV0.
- [x] Usar os glyphs nativos no texto escalado do Classic sem mudar metricas,
  medicao, hit-testing ou reflow.
- [x] Manter o redimensionador antigo somente como fallback com log e mapear
  bytes fora do ASCII imprimivel para `?` no caminho nativo.
- [x] Diferenciar no `display status` a fonte Classic nativa da fonte Simple
  legada.
- [ ] Aprovar a nova tipografia nas tres escalas e concluir a matriz abaixo.

### Criterio de saida

As tres escalas usam glyphs nativos nitidos, preservam a geometria validada do
MV0 e distinguem letras, numeros e pontuacao sem cortes. O Simple permanece
visualmente inalterado.

### Matriz QEMU pendente

1. Rodar `make q3check`, `make clean && make` e `make run`, nessa ordem.
2. Alternar pequena, normal e grande pelo Shell e pelo Settings.
3. Verificar Desktop, Menu Iniciar, Taskbar, titulos, Explorer, Settings e
   Task Manager, incluindo `I/l/1`, `O/0`, `S/5` e pontuacao.
4. Confirmar centralizacao, cortes e textos proximos das bordas.
5. Fazer o smoke test do Simple e confirmar a fonte legada 8x16.
6. Encerrar com `health summary`, `memcheck` e `regcheck full`.

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
  - **App Store Moderna**: catalogo, detalhes, botoes e confirmacoes usando as
    mesmas metricas, controles e estados visuais.
- [ ] Refinar contraste e espacamento da Zephyr UI Bitmap sobre as faces
  nativas do MV0.1; fonte proporcional fica fora desta frente.
- [ ] Registrar uma linha-base por cena fixa (modo VESA, escala, janela e
  acao) com `kmetrics`, incluindo bytes apresentados e ticks de copia VESA.
- [ ] Aceitar a nova aparencia somente quando a mesma cena nao aumentar mais
  de 10% os ticks de copia ou bytes apresentados; qualquer excecao precisa ser
  registrada em `docs/qualidade/metricas.md`.

### Criterio de saida

Explorer, Settings, Task Manager e App Store abrem e funcionam com o novo
visual. O redesenho de janelas arrastadas se mantem fluido e a comparacao
`kmetrics` da mesma cena prova que a sobrecarga visual esta dentro do limite
definido.

## Validacao por etapa

O desenvolvedor devera orientar o usuario a rodar:

```text
make q3check
make clean && make
make run
```

No QEMU, manter `guimode classic` para validar `display status`, as tres
escalas e a matriz completa de Desktop, Explorer, Settings, Task Manager e
App Store, comparando `kmetrics`. O Simple recebe somente o smoke test de
fallback descrito no MV0. `guimode modern` deve informar que o nome esta
reservado enquanto o renderer futuro ainda nao existir.
