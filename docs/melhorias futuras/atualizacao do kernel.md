# Atualizacao e Otimizacao do Kernel

## Resumo de Progresso

Status: planejado para depois da Fundacao do Kernel.

Neste projeto, "atualizar o kernel" significa evoluir a implementacao do
kernel com seguranca. Nao existe uma versao upstream que precise ser trocada
automaticamente. A prioridade sera medir os gargalos e melhorar somente os
componentes que realmente limitam a experiencia.

## Objetivo

Manter um kernel pequeno, estavel e eficiente para que o ZephyrOS possa
executar jogos, aplicativos produtivos e interfaces classica e moderna com
baixo custo de recursos.

## Ordem de trabalho

### Fase 1 - Medicao

- medir tempo gasto em interrupcoes, scheduler, troca de contexto e desenho;
- acompanhar uso de CPU, memoria, paging e filas de eventos;
- registrar resultados antes e depois de cada alteracao;
- evitar otimizar por intuicao quando nao houver gargalo confirmado.

### Fase 2 - Caminhos de execucao

- reduzir trabalho redundante no loop principal;
- revisar frequencia e custo de atualizacao dos processos;
- melhorar filas de teclado, mouse e IPC;
- evitar bloqueios longos em drivers e operacoes de disco.

### Fase 3 - Scheduler e processos

- revisar o custo da troca de contexto;
- avaliar prioridades e fatias de tempo somente com metricas;
- evitar starvation sem comprometer a simplicidade;
- preservar a recuperacao controlada de aplicativos.

### Fase 4 - Memoria e paging

- reduzir fragmentacao do heap;
- revisar caminhos quentes do allocator;
- otimizar mapeamentos somente depois de validar os limites;
- manter diagnosticos de memoria e page fault.

### Fase 5 - Drivers e I/O

- reduzir esperas ocupadas quando for seguro;
- melhorar buffers de video, disco e audio;
- priorizar drivers que afetam jogos e produtividade;
- manter fallback funcional para hardware ausente.

### Fase 6 - Validacao de regressao

- comparar boot, responsividade e uso de memoria;
- testar Shell, Desktop, Explorer, Settings e Task Manager;
- testar ausencia de VESA, disco e AC97;
- confirmar que excecoes fatais continuam chegando ao `panic`.

## Dependencias

Esta etapa depende da conclusao da [Fundacao do Kernel](fundacao%20do%20kernel.md).
Sem APIs estaveis e diagnosticos confiaveis, uma otimizacao pode esconder
falhas ou dificultar a manutencao.

## Limites

- Nao alterar `src/boot/boot.asm` sem autorizacao explicita.
- Nao trocar componentes estaveis sem medicao que justifique a mudanca.
- Nao sacrificar seguranca, recuperacao ou legibilidade por ganhos pequenos.
- Nao presumir que otimizar o kernel resolvera compatibilidade com aplicativos
  de outros sistemas; isso exige APIs, bibliotecas e drivers compativeis.

## Criterios de conclusao

- melhoria mensuravel na responsividade ou no uso de recursos;
- nenhum aumento de travamentos ou falhas recuperaveis;
- interfaces classica e moderna preservadas;
- documentacao das metricas e dos trade-offs;
- build e testes do usuario aprovados.

## Referencias

- [Fundacao do Kernel](fundacao%20do%20kernel.md)
- `docs/04-kernel/kernel.md`
- `docs/melhorias futuras/responsividade do sistema.md`
- `src/kernel/`
- `src/process/`
- `src/memory/`
