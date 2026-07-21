# Task Manager moderno - ZephyrOS

## Resumo de Progresso

- [x] Manter `taskmgr` do Shell como TUI de diagnostico.
- [x] Criar uma janela grafica propria para a taskbar e o Desktop.
- [x] Adicionar abas de processos, memoria e threads.
- [x] Adicionar selecao, propriedades, ordenacao e acoes pelo teclado.
- [x] Adicionar fechar, minimizar, maximizar, restaurar e arrastar com mouse.
- [x] Enriquecer TUI e GUI com CPU, tempo, espera, paginas, ATA e contexto tecnico.
- [x] Adicionar painel persistente de detalhes para o processo selecionado.
- [x] Usar fallback controlado para TUI quando VESA ou backbuffer nao estiverem disponiveis.
- [ ] Integrar a janela ao Window Manager grafico futuro.

## Atalhos

| Acao | Atalho |
|------|--------|
| Trocar aba | `Tab` |
| Navegar | Setas |
| Ordenar processos | `S` |
| Propriedades | `Enter` |
| Encerrar processo | `Delete` |
| Focar processo compativel | `F` |
| Reiniciar processo compativel | `R` |
| Fechar | `Esc` |
| Abrir diagnostico textual | `taskmgr` no Shell |

## Fases

### Fase 1 - Janela grafica com fallback

- A taskbar e o Desktop enviam `IPC_APP_OPEN_TASKMANAGER_GUI`.
- A TUI continua sendo aberta diretamente pelo comando `taskmgr`.
- A janela usa as primitivas 3D existentes e compartilha os dados de processos,
  memoria e threads com a TUI.
- A atualizacao grafica e nao bloqueante e respeita o backbuffer.

### Fase 2 - Integracao com Window Manager

- Unificar foco, z-order e ciclo de vida no Window Manager grafico.
- Permitir mais de uma janela grafica sem duplicar os dados do sistema.

### Fase 3 - Metricas avancadas

- Memoria por processo, prioridade e quantum.
- PID pai, arvore de processos e proprietario das threads.
- Historico limitado de CPU, memoria e atividade de disco.
- Detalhamento desta evolucao em `task manager metricas avancadas.md`.

## Limitacoes

- Nao ha persistencia de posicao, tamanho ou estado.
- O comando `taskmgr` permanece deliberadamente textual para diagnostico.
- Esta etapa ainda nao adiciona graficos historicos nem novas metricas do kernel.
- Falhas visuais recuperaveis retornam erro e usam a TUI; falhas estruturais continuam
  sujeitas a `panic` conforme a politica de resiliencia.

## Referencias

- `src/shell/taskmanager.c`
- `src/include/apps/taskmanager.h`
- `src/include/process/process.h`
- `src/gui/gui.c`
- `src/taskbar/taskbar.c`
