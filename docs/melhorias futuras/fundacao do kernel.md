# Fundacao do Kernel

## Resumo de Progresso

Status: em andamento.

Esta etapa organiza as bases internas do ZephyrOS antes de buscar otimizacoes
de baixo nivel. A meta e tornar o kernel previsivel, modular e facil de
estender sem abandonar os modos classico e moderno.

## Objetivo

Para o ZephyrOS, o melhor caminho e primeiro organizar as bases do kernel:

- APIs claras e contratos de erro consistentes;
- modulos independentes, com dependencias explicitas;
- gerenciamento de memoria seguro e observavel;
- processos e threads estaveis;
- mecanismos de log, diagnostico e health confiaveis.

Essa fundacao deve facilitar a construcao de drivers, aplicativos, interfaces
e futuras camadas de compatibilidade.

## Implementacao da Etapa 5

Implementado nesta etapa:

- [x] alinhamento de 8 bytes para blocos do heap dinamico;
- [x] confirmacao controlada da conclusao de escritas ATA PIO;
- [x] troca cooperativa real de contexto de threads, com stacks dedicadas;
- [x] round-robin de threads com indice persistente e auto teste `threadtest`;
- [x] pool fixo de PIDs opacos, sem busca dupla na criacao de processos;
- [x] desenho de texto VESA diretamente no backbuffer por glyph;
- [x] reinicializacao dos controles do scheduler e do registro de processos;
- [x] busca segura por PID, validacao de estados e protecao do Idle;
- [x] fallback para o Idle quando nao houver processo pronto;
- [x] validacao de mensagens IPC, filas, foco e desbloqueio por mensagem;
- [x] contadores de envio, recebimento, falha e fila cheia no IPC;
- [x] validacao de alinhamento, flags e diretorio ativo no paging;
- [x] consulta `paging_is_ready()` para diagnostico;
- [x] ampliacao do comando `health` com estado do kernel e recovery;
- [x] registro dos contratos de estabilidade nesta documentacao.

Pendente para a validacao no ambiente do usuario:

- [ ] executar `make clean && make`;
- [ ] executar `make run` e testar `health`, `desktop`, `explorer`, `taskmgr`
      e `settings`;
- [ ] validar os caminhos de fallback sem VESA, disco e audio.

## Fases

### Fase 1 - Auditoria da arquitetura

- mapear dependencias entre kernel, drivers, memoria, processos e apps;
- identificar funcoes que falham sem retornar erro;
- separar responsabilidades que hoje estao concentradas no `kernel.c`;
- definir quais falhas sao recuperaveis e quais exigem `panic`.

### Fase 2 - Contratos e APIs

- padronizar codigos de retorno;
- validar ponteiros, tamanhos e estados de inicializacao;
- documentar pre-condicoes e pos-condicoes das APIs publicas;
- manter compatibilidade com os chamadores existentes durante a transicao.

### Fase 3 - Memoria e paging

- revisar limites do heap e do bitmap;
- melhorar diagnostico de alocacoes e liberacoes;
- garantir que falhas de mapeamento retornem erro controlado;
- preparar testes para detectar corrupcao e vazamentos.

### Fase 4 - Processos e threads

- estabilizar estados, filas e troca de contexto;
- tornar limites de processos e threads explicitos;
- registrar falhas de criacao, encerramento e espera;
- preservar a continuidade do Shell quando um app falhar.

### Fase 5 - Diagnostico

- ampliar o comando `health`;
- registrar estado dos componentes e ultimas falhas;
- adicionar contadores uteis para memoria, processos e interrupcoes;
- diferenciar degradacao, desabilitacao e falha fatal.

### Fase 6 - Validacao

- validar boot normal e fallback sem VESA, disco ou audio;
- testar repetidamente abertura e fechamento dos aplicativos;
- confirmar que uma falha recuperavel nao prende o loop principal;
- documentar as interfaces estabilizadas para as proximas etapas.

## Limites

- Nao alterar `src/boot/boot.asm` nesta etapa.
- Nao implementar isolamento completo de processos.
- Nao trocar o scheduler apenas por preferencia de design.
- Nao introduzir otimizacoes que dificultem diagnostico ou manutencao.
- O scheduler de threads permanece cooperativo; a preempcao continua sob o
  scheduler de processos existente.

## Resultado esperado

Ao terminar esta etapa, o kernel devera oferecer uma base estavel para
otimizacoes de desempenho, novos drivers, aplicativos produtivos e suporte
futuro a jogos.

## Referencias

- `docs/04-kernel/kernel.md`
- `docs/02-arquitetura/arquitetura.md`
- `docs/melhorias futuras/resiliencia do sistema.md`
- `src/kernel/kernel.c`
- `src/memory/`
- `src/process/`
