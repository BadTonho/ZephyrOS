# Fundacao do Kernel

## Resumo de Progresso

Status: planejado.

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
