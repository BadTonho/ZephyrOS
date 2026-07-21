# Roadmap — Barra de Tarefas

> Visão geral do desenvolvimento dividida em **5 fases**.
> `✅` = Implementado | `🟡` = Parcial | `⬜` = Não implementado

---

## Fase 1 — Menu Iniciar e Botões Básicos
> Objetivo: Menu funcional com aplicativos e navegação por teclado.

### 1. Abrir o Menu Iniciar
- ✅ Abrir a lista de aplicativos instalados. *(taskbar.c:25-34 — 7 itens: Desktop, Shell, Explorer, TaskMgr, Config, Reiniciar, Desligar)*
- ✅ Abrir aplicativos fixados. *(taskbar.c:431-444 — Enter abre o app selecionado)*
- ⬜ Pesquisar programas, arquivos e configurações.
- ⬜ Acessar arquivos recentes e recomendados.
- ✅ Abrir as Configurações. *(taskbar.c:441 — case 4 retorna Settings)*
- ⬜ Abrir pastas pessoais.
- ⬜ Acessar as opções da conta.
- ⬜ Bloquear o computador.
- ⬜ Trocar de usuário.
- ⬜ Sair da conta.
- ⬜ Suspender.
- ✅ Reiniciar o computador. *(taskbar.c:442 — case 5 retorna Reiniciar)*
- ✅ Desligar o computador. *(taskbar.c:443 — case 6 retorna Desligar)*
- ✅ Menu pode ser aberto pela tecla Alt e Win. *(taskbar.c:577-582)*

### 2. Fixar aplicativos
- ⬜ Fixar um aplicativo.
- ⬜ Desafixar um aplicativo.
- ⬜ Fixar um programa pelo Menu Iniciar.
- ⬜ Fixar um programa que esteja sendo executado.
- ⬜ Arrastar ícones para mudar sua ordem.
- ⬜ Manter o ícone mesmo quando o aplicativo estiver fechado.
- ⬜ Abrir rapidamente programas usados com frequência.

### 3. Abrir e alternar entre aplicativos
- ✅ Abrir o aplicativo. *(taskbar.c:234-251 — taskbar_add_app registra e ativa)*
- ⬜ Restaurar uma janela minimizada.
- ⬜ Colocar uma janela em primeiro plano.
- ✅ Alternar entre programas. *(taskbar.c:615-671 — botões clicáveis por teclado e mouse)*
- ⬜ Alternar entre várias janelas do mesmo programa.
- ✅ Identificar qual aplicativo está aberto. *(cor 0x1F = ativo, 0x07 = inativo)*
- ✅ Identificar qual janela está ativa. *(taskbar.c:144 — cor diferente para active)*
- ⬜ Exibir o progresso de determinadas operações.
- ⬜ Abrir uma nova instância do programa.

### 4. Agrupamento de janelas
- ⬜ Ver todas as janelas daquele aplicativo.
- ⬜ Alternar entre elas.
- ⬜ Fechar uma janela individual.
- ⬜ Fechar todas as janelas do grupo.
- ⬜ Restaurar uma janela específica.
- ⬜ Diferenciar documentos ou abas abertas em janelas separadas.
- ⬜ Percorrer as janelas do grupo usando `Ctrl + clique`.

### 5. Miniaturas das janelas
- ⬜ Miniatura de cada janela.
- ⬜ Título da janela.
- ⬜ Visualização em tempo real.
- ⬜ Botão para fechar a janela.
- ⬜ Visualização temporária da janela ao apontar para a miniatura.
- ⬜ Grupos de janelas encaixadas pelo Snap.
- ⬜ Controles multimídia em alguns aplicativos.

---

## Fase 2 — Menus, Jump Lists e Indicadores
> Objetivo: Menus de contexto, listas de atalhos e indicadores visuais.

### 6. Menu do aplicativo
- ⬜ Abrir o aplicativo.
- ⬜ Fixar ou desafixar.
- ⬜ Fechar a janela.
- ⬜ Fechar todas as janelas.
- ⬜ Abrir uma nova janela.
- ⬜ Abrir uma janela privada.
- ⬜ Executar como administrador.
- ⬜ Abrir propriedades.
- ⬜ Acessar arquivos recentes.
- ⬜ Acessar tarefas específicas do programa.

### 7. Listas de Atalhos — Jump Lists
- ⬜ Arquivos recentes.
- ⬜ Documentos frequentes.
- ⬜ Pastas recentes.
- ⬜ Sites visitados.
- ⬜ Projetos utilizados.
- ⬜ Itens fixados pelo usuário.
- ⬜ Ações rápidas do aplicativo.
- ⬜ Abrir uma nova janela.
- ⬜ Criar um novo documento.
- ⬜ Abrir configurações específicas.
- ⬜ Escrever uma nova mensagem.

### 8. Indicadores de atividade
- ✅ Linha indicando que está aberto. *(cor diferente: 0x1F ativo vs 0x07 inativo)*
- ✅ Destaque indicando que está ativo. *(taskbar.c:144 — btn->active ? 0x1F : 0x07)*
- ⬜ Barra de progresso de download ou instalação.
- ⬜ Número de mensagens não lidas.
- ⬜ Indicador de notificação.
- ⬜ Alarme ativo.
- ⬜ Status de sincronização.
- ⬜ Erro ou aviso.
- ⬜ Aplicativo pedindo atenção.
- ⬜ Ícone piscando.

---

## Fase 3 — Pesquisa, Widgets e Bandeja
> Objetivo: Pesquisa integrada, widgets, ícones de sistema e notificações.

### 9. Pesquisa do ZephyrOS
- ⬜ Pesquisar aplicativos.
- ⬜ Pesquisar arquivos.
- ⬜ Pesquisar configurações.
- ⬜ Pesquisar pastas.
- ⬜ Pesquisar conteúdo indexado.
- ⬜ Localizar opções do Painel de Controle.
- ⬜ Mostrar resultados da internet.
- ⬜ Executar comandos.
- ⬜ Fazer cálculos simples.
- ⬜ Encontrar documentos recentes.

### 10. Visão de Tarefas
- ⬜ Mostrar todas as janelas abertas.
- ⬜ Escolher uma janela.
- ⬜ Fechar uma janela.
- ⬜ Visualizar grupos do Snap.
- ⬜ Criar áreas de trabalho virtuais.
- ⬜ Alternar entre áreas de trabalho.
- ⬜ Mover janelas para outra área.
- ⬜ Reorganizar áreas de trabalho.
- ⬜ Fechar uma área virtual.

### 11. Widgets
- ⬜ Previsão do tempo.
- ⬜ Notícias.
- ⬜ Calendário.
- ⬜ Esportes.
- ⬜ Bolsa e mercado.
- ⬜ Trânsito.
- ⬜ Fotos.
- ⬜ Conteúdo personalizado.
- ⬜ Informações de aplicativos compatíveis.

### 12. Bandeja do sistema
- ⬜ Ícones de recursos e programas em segundo plano.
- ⬜ Status temporários.
- ⬜ Notificações.
- ⬜ Controles do sistema.

### 13. Ícones ocultos
- ⬜ Antivírus.
- ⬜ Drivers de vídeo e áudio.
- ⬜ Aplicativos de sincronização.
- ⬜ Programas de captura de tela.
- ⬜ Aplicativos de comunicação.
- ⬜ Gerenciadores de hardware.
- ⬜ Utilitários que rodam em segundo plano.
- ⬜ Escolher quais ficam visíveis.

### 14. Rede
- ⬜ Ver o estado da conexão.
- ⬜ Identificar Ethernet ou Wi-Fi.
- ⬜ Mostrar redes Wi-Fi disponíveis.
- ⬜ Conectar-se a uma rede.
- ⬜ Desconectar-se.
- ⬜ Ativar ou desativar Wi-Fi.
- ⬜ Ativar o modo avião.
- ⬜ Acessar configurações de rede.
- ⬜ Detectar falta de internet.
- ⬜ Ver conexão por VPN.
- ⬜ Abrir as Configurações Rápidas no ZephyrOS.

### 15. Som e volume
- ⬜ Aumentar ou diminuir o volume.
- ⬜ Silenciar o sistema.
- ⬜ Escolher o dispositivo de saída.
- ⬜ Escolher alto-falantes ou fones.
- ⬜ Abrir o mixer de volume.
- ⬜ Controlar o volume de aplicativos separadamente.
- ⬜ Acessar configurações de áudio.
- ⬜ Identificar problemas no dispositivo de som.
- ⬜ Ativar recursos de áudio espacial.

### 16. Bateria e energia
- ⬜ Percentual da bateria.
- ⬜ Estado de carregamento.
- ⬜ Tempo estimado restante.
- ⬜ Modo de economia de energia.
- ⬜ Recomendações de energia.
- ⬜ Estado de carregamento inteligente.
- ⬜ Acesso às configurações de bateria e energia.

### 17. Configurações Rápidas
- ⬜ Wi-Fi.
- ⬜ Bluetooth.
- ⬜ Modo avião.
- ⬜ Economia de bateria.
- ⬜ Acessibilidade.
- ⬜ Luz noturna.
- ⬜ Brilho.
- ⬜ Volume.
- ⬜ Transmissão para outra tela.
- ⬜ Compartilhamento por proximidade.
- ⬜ Legendas ao vivo.
- ⬜ Hotspot móvel.
- ⬜ Projeto e conexão com monitores.
- ⬜ Dispositivos de saída de áudio.

---

## Fase 4 — Relógio, Notificações e Área de Trabalho
> Objetivo: Relógio completo, notificações, foco e mostrar desktop.

### 18. Relógio e calendário
- ✅ Mostrar hora. *(taskbar.c:193-232 — HH:MM)*
- ✅ Mostrar minutos. *(taskbar.c:205-216)*
- ⬜ Mostrar segundos, quando ativado.
- ⬜ Mostrar data.
- ⬜ Abrir calendário.
- ⬜ Mostrar agenda.
- ⬜ Acessar notificações.
- ⬜ Abrir configurações de data e hora.
- ⬜ Mostrar relógios adicionais.
- ⬜ Ajudar a iniciar sessões de foco.

### 19. Central de Notificações
- ⬜ Ver notificações recentes.
- ⬜ Abrir a notificação.
- ⬜ Dispensar uma notificação.
- ⬜ Limpar todas.
- ⬜ Agrupar notificações por aplicativo.
- ⬜ Silenciar determinados programas.
- ⬜ Acessar configurações de notificação.
- ⬜ Ativar o modo Não Incomodar.
- ⬜ Controlar notificações prioritárias.
- ⬜ Responder diretamente.

### 20. Foco e Não Incomodar
- ⬜ Sessões de foco.
- ⬜ Temporizador de concentração.
- ⬜ Silenciamento de notificações.
- ⬜ Regras automáticas de Não Incomodar.
- ⬜ Exceções para aplicativos prioritários.
- ⬜ Integração com relógio e calendário.
- ⬜ Redução de distrações durante jogos ou apresentações.

### 21. Mostrar a área de trabalho
- ⬜ Todas as janelas são minimizadas.
- ⬜ A área de trabalho fica visível.
- ⬜ Um segundo clique restaura as janelas.
- ⬜ O recurso pode ser ativado ou desativado.

---

## Fase 5 — Personalização, Multi-Monitor e APIs
> Objetivo: Configurações avançadas, multi-monitor, acessibilidade e APIs.

### 22. Personalização
- ✅ Aplicativos fixados. *(taskbar.c:376 — toggle Fixado)*
- ⬜ Ordem dos ícones.
- ⬜ Exibição da Pesquisa.
- ⬜ Exibição da Visão de Tarefas.
- ⬜ Exibição de Widgets.
- ⬜ Ícones da bandeja.
- ⬜ Ícones ocultos.
- ⬜ Distintivos dos aplicativos.
- ⬜ Ícones piscando.
- 🟡 Ocultação automática. *(settings.c:55-57 — toggle existe, sem lógica funcional)*
- ⬜ Barra em vários monitores.
- ⬜ Agrupamento dos botões.
- ⬜ Exibição de rótulos.
- ⬜ Alinhamento dos ícones.
- ⬜ Cor e transparência.
- ⬜ Exibição do botão Mostrar área de trabalho.
- 🟡 Relógio e segundos. *(parcial — HH:MM sem segundos)*
- ✅ Posições (5 opções). *(taskbar.c:9-15 — Bottom/Top/Left/Right/Custom)*
- ✅ Tamanhos de ícone (3). *(taskbar.c:17-21 — Small/Medium/Large)*
- ✅ Menu de configuração (F1). *(taskbar.c:297-402 — 8 itens configuráveis)*
- ✅ Editor de ícones. *(settings.c:233-311 — personalização visual)*

### 23. Ocultação automática
- ⬜ Permanecer sempre visível.
- ⬜ Ocultar automaticamente.
- ⬜ Aparecer ao levar o mouse até a borda inferior.
- ⬜ Aparecer ao tocar na borda.
- ⬜ Liberar mais espaço para os aplicativos.
- ⬜ Recolher-se automaticamente no modo tablet.

### 24. Vários monitores
- ⬜ Aparecer em todos os monitores.
- ⬜ Aparecer apenas no monitor principal.
- ⬜ Mostrar aplicativos no monitor em que a janela está.
- ⬜ Mostrar aplicativos em todas as barras.
- ⬜ Usar agrupamentos diferentes.
- ⬜ Facilitar a troca entre janelas de cada monitor.
- ⬜ Manter o Menu Iniciar acessível em mais de uma tela.

### 25. Compartilhamento de janelas
- ⬜ Compartilhar uma janela em uma reunião.
- ⬜ Escolher diretamente qual aplicativo será compartilhado.
- ⬜ Interromper o compartilhamento.
- ⬜ Identificar que uma janela está sendo transmitida.
- ⬜ Silenciar ou controlar chamadas.

### 26. Progresso e estado nos ícones
- ⬜ Progresso normal.
- ⬜ Progresso pausado.
- ⬜ Erro.
- ⬜ Operação concluída.
- ⬜ Download em andamento.
- ⬜ Instalação em andamento.
- ⬜ Reprodução de mídia.
- ⬜ Contagem de mensagens.
- ⬜ Pequenos ícones sobrepostos indicando estado.

### 27. Recursos de acessibilidade
- ✅ Navegar entre os ícones. *(taskbar.c:404-464 — navegação por teclado completa)*
- ✅ Abrir aplicativos. *(Enter seleciona item no menu)*
- ⬜ Acessar a bandeja do sistema.
- ✅ Abrir menus de contexto. *(F1 abre config, Alt abre menu)*
- ⬜ Reorganizar ícones.
- ⬜ Ouvir os elementos usando o Narrador.
- ⬜ Utilizar foco visual.
- ⬜ Operar com toque.
- ⬜ Usar controles maiores no modo tablet.

### 28. Principais atalhos da barra de tarefas
- ✅ Alt / Win: abrir Menu Iniciar. *(taskbar.c:577-582)*
- ✅ F1: abrir configurações da taskbar. *(taskbar.c:450-455)*
- ✅ Setas ↑↓: navegar no menu. *(taskbar.c:417-426)*
- ✅ Enter: selecionar item. *(taskbar.c:431)*
- ✅ Esc: fechar menu. *(taskbar.c:412-414)*
- ⬜ Super + T: percorrer os aplicativos da barra.
- ⬜ Super + B: ir para a bandeja do sistema.
- ⬜ Super + número: abrir ou alternar para o aplicativo naquela posição.
- ⬜ Super + Shift + número: abrir outra instância.
- ⬜ Super + Ctrl + número: alternar para a última janela ativa.
- ⬜ Super + Alt + número: abrir a Jump List.
- ⬜ Shift + clique: abrir outra instância.
- ⬜ Ctrl + Shift + clique: abrir como administrador.
- ⬜ Shift + botão direito: abrir o menu tradicional da janela.
- ⬜ Ctrl + clique em grupo: percorrer as janelas agrupadas.
- ⬜ Super + D: mostrar ou restaurar a área de trabalho.
- ⬜ Super + Tab: abrir a Visão de Tarefas.
- ⬜ Super + W: abrir Widgets.

---

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1 — Menu/Botões | 37 | 10 | 0 | 27 |
| 2 — Menos/Jump/Indicadores | 31 | 2 | 0 | 29 |
| 3 — Pesquisa/Widgets/Bandeja | 51 | 0 | 0 | 51 |
| 4 — Relógio/Notificações/Desktop | 28 | 2 | 0 | 26 |
| 5 — Personalização/Multi/ APIs | 38 | 10 | 1 | 27 |
| **Total** | **185** | **24** | **1** | **160** |

---

## Referência — Atalhos Implementados

| Atalho | Ação | Status |
|---|---|---|
| `Alt` / `Win` | Abrir Menu Iniciar | ✅ |
| `F1` | Abrir config da taskbar | ✅ |
| `Setas ↑↓` | Navegar nos menus | ✅ |
| `Enter` | Selecionar item | ✅ |
| `Esc` | Fechar menu | ✅ |

---

## Limitações Técnicas Conhecidas

- **VGA Text Mode 80x25**: Suporte limitado para TUI, porém agora existe fallback e integração nativa com VESA (GUI 2D) com mouse funcional.
- **Máximo 8 botões**: `TASKBAR_BUTTON_MAX = 8` definido em `taskbar.h`.
- **Relógio sem segundos/data**: Apenas HH:MM baseado em ticks do timer.
- **Ocultação automática**: Opção existe mas não tem lógica funcional.
- **Sem multi-monitor**: Apenas um display VGA/VESA.
