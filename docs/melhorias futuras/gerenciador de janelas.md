# Roadmap — Gerenciador de Janelas

> Visão geral do desenvolvimento dividida em **5 fases**.
> `✅` = Implementado | `🟡` = Parcial | `⬜` = Não implementado

---

## Fase 1 — Operações Básicas de Janela
> Objetivo: Criar, mover, redimensionar, fechar e gerenciar o foco das janelas.

### 1. Operações básicas de janela
- ✅ Abrir uma janela. *(wm.c:263-300 — wm_create_window com callbacks)*
- ✅ Fechar uma janela. *(wm.c:302-320 — wm_destroy_window / wm_close_focused)*
- ✅ Minimizar para a barra de tarefas. *(wm.c:385-397 — WM_STATE_MINIMIZED)*
- ✅ Maximizar para ocupar a tela. *(wm.c:399-403 — WM_STATE_MAXIMIZED, 80x24)*
- ✅ Restaurar ao tamanho anterior. *(wm.c:405-411 — WM_STATE_NORMAL)*
- ✅ Mover pela barra de título. *(wm.c:413-418 — wm_move_window, apenas teclado)*
- ✅ Redimensionar pelas bordas e cantos. *(wm.c:420-427 — wm_resize_window com min 10x5)*
- ⬜ Abrir o menu da janela. *(Alt+Espaço)*
- ✅ Colocar uma janela em primeiro plano. *(wm.c:331 — z_order incrementado ao focar)*
- ✅ Alternar o foco entre janelas. *(wm.c:434-437 — Tab rotaciona foco)*
- ✅ Manter várias janelas abertas simultaneamente. *(WM_MAX_WINDOWS = 16)*
- ✅ Controlar qual janela fica acima das outras. *(wm.c:254-260 — z-order rendering)*
- ⬜ Organizar janelas sobrepostas.
- ⬜ Exibir caixas de diálogo pertencentes a uma janela.
- ⬜ Manter janelas-filhas associadas ao aplicativo principal.

---

## Fase 2 — Alternância e Snap
> Objetivo: Trocar rapidamente entre janelas e organizar na tela.

### 2. Alternância entre janelas
- ✅ Tab: alternar entre janelas abertas. *(wm.c:434-437 — wm_focus_next)*
- ⬜ Alt + Shift + Tab: alternar no sentido contrário.
- ⬜ Super + Tab: abrir a Visão de Tarefas.
- ✅ Clicar no ícone do aplicativo na barra de tarefas. *(taskbar.c:137-156 — botões com indicador)*
- ⬜ Alternar entre janelas do mesmo aplicativo.
- ⬜ Visualizar miniaturas ao passar o mouse sobre a barra de tarefas.
- ⬜ Fechar uma janela diretamente pela miniatura.
- ✅ Restaurar uma janela minimizada. *(wm.c:405-411 — wm_restore_window)*
- ⬜ Alternar entre janelas usando toque ou trackpad.
- ⬜ Mostrar grupos de janelas organizadas pelo Snap.

### 3. Snap de janelas
- ⬜ Posicionar na metade esquerda.
- ⬜ Posicionar na metade direita.
- ⬜ Posicionar no canto superior esquerdo.
- ⬜ Posicionar no canto superior direito.
- ⬜ Posicionar no canto inferior esquerdo.
- ⬜ Posicionar no canto inferior direito.
- ⬜ Maximizar arrastando para o topo.
- ⬜ Ajustar janelas pelo mouse, toque ou teclado.
- ⬜ Redimensionar simultaneamente janelas encaixadas.
- ⬜ Preencher automaticamente o espaço restante.
- ⬜ Mostrar sugestões de outras janelas abertas.
- ⬜ Organizar duas, três ou quatro janelas.
- ⬜ Usar configurações diferentes conforme o tamanho do monitor.

### 4. Layouts do Snap
- ⬜ Dividir a tela em duas partes iguais.
- ⬜ Criar uma área maior e outra menor.
- ⬜ Organizar três colunas.
- ⬜ Criar uma janela principal e duas secundárias.
- ⬜ Dividir a tela em quatro áreas.
- ⬜ Escolher uma posição dentro do layout.
- ⬜ Preencher as posições restantes com o Snap Assist.
- ⬜ Exibir layouts diferentes conforme resolução e tamanho da tela.
- ⬜ Abrir a barra de layouts arrastando uma janela para o topo da tela.

### 5. Snap Assist
- ⬜ Mostrar miniaturas das janelas disponíveis.
- ⬜ Escolher rapidamente a segunda janela.
- ⬜ Preencher todas as posições do layout.
- ⬜ Ignorar uma posição.
- ⬜ Substituir uma janela do grupo.
- ⬜ Redimensionar janelas encaixadas juntas.
- ⬜ Evitar espaços vazios entre as janelas.
- ⬜ Sugerir aplicativos usados recentemente.

### 6. Grupos de Snap
- ⬜ Alternar para todo o conjunto de janelas.
- ⬜ Restaurar a disposição anterior.
- ⬜ Mostrar o grupo na barra de tarefas.
- ⬜ Mostrar o grupo no Alt + Tab.
- ⬜ Mostrar o grupo na Visão de Tarefas.
- ⬜ Restaurar apenas uma janela do grupo.
- ⬜ Retornar ao ambiente de trabalho depois de abrir outro aplicativo.

---

## Fase 3 — Visão de Tarefas e Áreas Virtuais
> Objetivo: Visualizar todas as janelas e criar workspaces.

### 7. Visão de Tarefas
- ⬜ Visualizar todas as janelas abertas.
- ⬜ Escolher uma janela.
- ⬜ Fechar janelas.
- ⬜ Ver grupos do Snap.
- ⬜ Criar áreas de trabalho virtuais.
- ⬜ Alternar entre áreas de trabalho.
- ⬜ Mover janelas entre áreas de trabalho.
- ⬜ Reorganizar áreas de trabalho.
- ⬜ Fechar uma área de trabalho.
- ⬜ Identificar quais aplicativos estão abertos em cada ambiente.

### 8. Áreas de trabalho virtuais
- ⬜ Criar uma nova área de trabalho.
- ⬜ Alternar entre áreas.
- ⬜ Fechar uma área.
- ⬜ Mover uma janela de uma área para outra.
- ⬜ Exibir uma janela em várias áreas.
- ⬜ Separar aplicativos por atividade.
- ⬜ Escolher se a barra de tarefas mostra apenas as janelas da área atual ou de todas.
- ⬜ Escolher se Alt + Tab mostra janelas da área atual ou de todas.

---

## Fase 4 — Barra de Tarefas e Desktop
> Objetivo: Tarefa completa com botões, menus, atalhos e aparência.

### 9. Barra de tarefas e janelas
- ✅ Mostrar aplicativos abertos. *(taskbar.c:234-251 — taskbar_add_app)*
- ✅ Mostrar aplicativos minimizados. *(taskbar.c:137-156 — botão mantido mas inativo)*
- ⬜ Agrupar janelas do mesmo programa.
- ✅ Exibir indicadores de execução. *(taskbar.c:144 — cor 0x1F ativo vs 0x07 inativo)*
- ⬜ Mostrar miniaturas de cada janela.
- ⬜ Alternar entre janelas de um mesmo aplicativo.
- ⬜ Abrir outra instância do programa.
- ⬜ Fechar uma janela pela miniatura.
- ⬜ Fixar aplicativos.
- ⬜ Exibir grupos do Snap.
- ✅ Mostrar o menu do aplicativo. *(taskbar.c:268-289 — menu iniciar com 7 itens)*
- ⬜ Mostrar listas de arquivos recentes.
- ✅ Indicar qual janela está ativa. *(cor diferente no botão)*

### 10. Mostrar a área de trabalho
- ⬜ Super + D: mostrar ou restaurar a área de trabalho.
- ⬜ Super + M: minimizar todas as janelas.
- ⬜ Super + Shift + M: restaurar as janelas minimizadas.
- ⬜ Clicar no canto direito da barra de tarefas.
- ⬜ Usar a visualização temporária da área de trabalho.

### 11. Agitar a barra de título
- ⬜ Isolar rapidamente uma janela.
- ⬜ Minimizar todas as demais.
- ⬜ Restaurar as janelas repetindo o movimento.
- ⬜ Ser ativado ou desativado nas configurações.

### 12. Gerenciamento em vários monitores
- ⬜ Mover uma janela de uma tela para outra.
- ⬜ Maximizar separadamente em cada monitor.
- ⬜ Encaixar janelas nas bordas de cada monitor.
- ⬜ Manter janelas abertas em telas diferentes.
- ⬜ Ajustar o tamanho conforme a escala de cada monitor.
- ⬜ Adaptar janelas a monitores com resoluções diferentes.
- ⬜ Alternar entre duplicar e estender a área de trabalho.
- ⬜ Definir um monitor principal.
- ⬜ Mostrar a barra de tarefas em vários monitores.
- ⬜ Reorganizar virtualmente a posição das telas.

---

## Fase 5 — Foco, Aparência e APIs
> Objetivo: Controle de foco, visual, composição e APIs para apps.

### 13. Foco e janela ativa
- ✅ Qual janela está ativa. *(wm.c:322-335 — wm_focus_window)*
- ✅ Qual janela recebe o teclado. *(wm.c:462-464 — encaminha para on_key)*
- ⬜ Qual controle recebe a digitação.
- ⬜ Qual janela recebe cliques do mouse.
- ✅ Qual janela aparece em primeiro plano. *(z_order ao focar)*
- ⬜ Qual aplicativo pisca na barra de tarefas solicitando atenção.
- ⬜ Qual janela pode abrir uma caixa de diálogo.
- ⬜ Qual janela deve ser restaurada ao alternar de aplicativo.
- ✅ A ordem das janelas sobrepostas. *(z-order rendering)*

### 14. Ordem de sobreposição (Z-Order)
- ✅ Janela ativa na frente. *(wm.c:331 — z_counter++ ao focar)*
- ✅ Janelas normais atrás dela. *(wm.c:254-260 — renderiza por z-order)*
- ⬜ Caixas de diálogo acima da janela principal.
- ⬜ Menus e avisos temporariamente acima.
- ⬜ Janelas configuradas como sempre visíveis.
- ⬜ Elementos da área de trabalho abaixo das janelas comuns.

### 15. Menus, caixas de diálogo e janelas secundárias
- ✅ Menu Iniciar. *(taskbar.c:268-289 — popup com borda)*
- ✅ Menu de configuração da Taskbar. *(taskbar.c:297-334 — 8 opções)*
- ✅ Painel de Configurações (Settings). *(settings.c:152-201 — 7 categorias)*
- ✅ Caixa de confirmação. *(filemanager.c:223-227 — Excluir? S/N)*
- ✅ Diálogos de input. *(filemanager.c:211-221 — criar/renomear)*
- ✅ Editor modal de ícones. *(settings.c:233-311 — icon_editor)*
- ⬜ Janela principal.
- ⬜ Janela secundária.
- ⬜ Caixa de mensagem.
- ⬜ Menu de contexto.
- ⬜ Tooltip.
- ⬜ Janela flutuante.
- ⬜ Popup genérico.
- ⬜ Tela de abertura.
- ⬜ Janela modal.
- ⬜ Janela não modal.
- ⬜ Painéis de ferramentas.
- ⬜ Notificações.

### 16. Aparência das janelas
- ✅ Barra de título. *(wm.c:172-204 — com cor diferenciada focada/desfocada)*
- ✅ Bordas. *(wm.c:219-234 — 2 estilos: duplas e simples)*
- ✅ Botões de minimizar, maximizar e fechar. *(wm.c:79-170 — 6 ordens configuráveis)*
- ⬜ Cantos arredondados.
- ⬜ Sombras.
- ⬜ Transparência.
- ⬜ Desfoque de fundo.
- ⬜ Animações de abertura e fechamento.
- ⬜ Animações de minimizar e restaurar.
- ✅ Indicação de janela ativa ou inativa. *(wm.c:172-204 — cor da title bar muda)*
- 🟡 Temas claro e escuro. *(settings.c:25-29 — 3 opções definidas, sem efeito visual extra)*
- ⬜ Cores de destaque.
- ⬜ Materiais visuais como Mica ou Acrílico.
- ⬜ Dimensionamento para telas de alta resolução.

### 17. Desktop Window Manager — dwm.exe
- ⬜ Compor todas as janelas na imagem final da área de trabalho.
- ⬜ Manter superfícies separadas para os aplicativos.
- ⬜ Controlar animações de janela.
- ⬜ Renderizar sombras e transparências.
- ⬜ Produzir desfoque de fundo.
- ⬜ Aplicar efeitos às áreas não clientes das janelas.
- ⬜ Produzir miniaturas em tempo real.
- ⬜ Apoiar o Alt + Tab.
- ⬜ Apoiar miniaturas da barra de tarefas.
- ⬜ Auxiliar no dimensionamento de aplicativos antigos em telas com DPI elevado.
- ⬜ Coordenar a apresentação dos quadros.
- ⬜ Eliminar rastros visuais ao mover janelas.
- ⬜ Combinar conteúdo vindo de diferentes aplicativos e monitores.

### 18. Consumo de GPU e memória
- ⬜ GPU para composição.
- ⬜ Memória de vídeo.
- ⬜ Memória RAM compartilhada.
- ⬜ CPU em determinadas operações.
- ⬜ Recursos adicionais quando existem muitos monitores.
- ⬜ Mais memória quando existem muitas janelas.
- ⬜ Mais recursos com resoluções elevadas, HDR, transparência ou múltiplas telas.

### 19. Escala e DPI
- ⬜ 100%, 125%, 150%, 200% ou outras.
- ⬜ Monitores com escalas diferentes.
- ⬜ Aplicativos compatíveis com DPI por monitor.
- ⬜ Redimensionamento ao mover uma janela entre telas.
- ⬜ Escala de aplicativos antigos.
- ⬜ Textos, bordas e controles adaptados à densidade do monitor.

### 20. Recursos para desenvolvedores
- ✅ Criar e destruir janelas. *(wm.h:84-119 — API completa com 17 funções)*
- ✅ Mostrar e ocultar. *(wm.c:302-314 — destroy = invisível)*
- ✅ Minimizar e maximizar. *(wm.c:385-403)*
- ✅ Alterar posição e tamanho. *(wm.c:413-427)*
- ✅ Enumerar janelas abertas. *(wm.c — array wm.ZephyrOS[16])*
- ✅ Obter o título. *(wm.h:98 — wm_get_window retorna struct)*
- ⬜ Localizar uma janela.
- ✅ Alterar o foco. *(wm.c:322-383)*
- ✅ Receber eventos de teclado. *(wm.h:40 — callback on_key)*
- ⬜ Processar mensagens do sistema.
- ⬜ Criar janelas-filhas.
- ✅ Criar caixas de diálogo. *(filemanager.c:211-227 — dialogs inline)*
- ✅ Controlar estilos e bordas. *(wm.h:66-68 — border_style, 2 estilos)*
- ⬜ Definir transparência.
- ⬜ Criar miniaturas pelo DWM.
- ⬜ Detectar monitores.
- ⬜ Adaptar-se ao DPI.
- ⬜ Receber notificações de mudança de resolução.
- ⬜ Capturar ou compartilhar conteúdo de janela.

### 21. Limitações nativas
- ⬜ Regras avançadas permanentes de posicionamento por aplicativo.
- ⬜ Divisões completamente personalizadas da tela.
- ⬜ Fixar universalmente qualquer janela sempre no topo por um botão padrão.
- ⬜ Mover automaticamente programas para posições complexas.
- ⬜ Salvar qualquer layout personalizado com regras detalhadas.
- ⬜ Janelas em mosaico automático como alguns gerenciadores do Linux.
- ⬜ Controle completo das janelas de processos executados com permissões superiores.

---

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1 — Básicas | 15 | 12 | 0 | 3 |
| 2 — Alternância/Snap | 39 | 4 | 0 | 35 |
| 3 — Visão/Áreas | 18 | 0 | 0 | 18 |
| 4 — Taskbar/Desktop | 33 | 7 | 0 | 26 |
| 5 — Foco/Aparência/APIs | 55 | 19 | 1 | 35 |
| **Total** | **160** | **42** | **1** | **117** |

---

## Referência — Atalhos do WM

| Atalho | Ação | Status |
|---|---|---|
| `Tab` | Alternar foco entre janelas | ✅ |
| `Esc` | Fechar janela focada | ✅ |
| `F1` | Minimizar janela focada | ✅ |
| `F2` | Maximizar/restaurar janela | ✅ |
| `wm` (shell) | Abrir o window manager | ✅ |

---

## Limitações Técnicas Conhecidas

- **VGA Text Mode 80x25**: Sem suporte a sombras, transparência, animações ou DPI.
- **Máximo 16 janelas**: `WM_MAX_WINDOWS = 16` definido em `wm.h`.
- **Sem mouse**: Arrasto e redimensionamento apenas por API programática.
- **Sem composição**: Renderização direta em `0xB8000`, repaint completo a cada alteração.
- **Sem multi-monitor**: Apenas um display VGA/VESA.
- **Nomes limitados**: Título da janela truncado a 32 caracteres.
