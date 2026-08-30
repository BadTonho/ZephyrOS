# Task Manager - metricas avancadas

## Resumo de Progresso

- [x] Medir memoria utilizada por processo na visao Classic via
  `/proc/<pid>/status`; a visao Simple permanece no caminho interno legado.
- [ ] Registrar prioridade e quantum de cada processo.
- [ ] Associar cada thread ao processo proprietario.
- [ ] Exibir PID pai e arvore de processos.
- [ ] Adicionar historico de CPU, memoria e atividade de disco.

## Proxima etapa

Esta evolucao deve ser feita depois do enriquecimento visual do Task Manager.
Os dados ainda nao existem de forma segura nas estruturas atuais e nao devem ser
simulados pela interface.

## Fases

### Fase 1 - Metricas de processo

- Adicionar contagem de paginas pertencentes a cada processo.
- Expor memoria residente e memoria virtual por processo.
- Registrar prioridade, quantum e tempo de espera do scheduler.
- Manter compatibilidade com a TUI e a janela grafica.

### Fase 2 - Relacionamento entre processos e threads

- Registrar o PID pai durante a criacao do processo.
- Associar cada thread ao processo proprietario.
- Permitir visualizacao em arvore sem alterar as operacoes de encerramento.
- Exibir relacoes ausentes como `N/D` durante a transicao.

### Fase 3 - Historico e diagnostico

- Armazenar amostras limitadas de CPU, memoria e ATA.
- Desenhar graficos compactos na GUI.
- Oferecer uma visao textual equivalente no comando `taskmgr`.
- Limitar o custo de memoria e o tempo de coleta para nao deixar o sistema lento.

## Limitacoes

- Esta etapa nao altera `process_t`, `thread_t` ou o scheduler.
- A visao Classic ja exibe `memory_bytes` publicado por `/proc`; memoria
  residente/virtual detalhada e historico completo ainda dependem de evolucao
  do contrato.
- Nao ha relacao thread-processo disponivel no modelo atual.
- Os graficos historicos dependem de um buffer de amostras controlado.

## Referencias

- `src/shell/taskmanager.c`
- `src/include/process/process.h`
- `src/include/process/thread.h`
- `src/include/core/memory.h`
- `docs/melhorias futuras/task manager moderno.md`
