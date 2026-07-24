# Roadmaps executaveis do ZephyrOS

Este diretorio separa o planejamento do ZephyrOS por frentes de trabalho. O
arquivo [ROADMAP.md](../../ROADMAP.md) continua sendo o mapa geral do projeto;
os documentos daqui definem ordem, limites e criterios de saida de cada frente.

## Estado de referencia

- Base original (boot, memoria, processos, filesystem, Shell e desktop):
  validada no QEMU.
- Plataforma de aplicativos: Fases 1 a 6D validadas, incluindo processos ring
  3, syscalls, loader ZAPP, foco, teclado, argumentos simples, as migracoes
  internas de `echo`, `uptime` e `mem`, e o contrato de console e ciclo de
  vida validado por `app outputtest [fail]`.
- Estabilizacao e qualidade: Q1 validado no QEMU, com matriz de regressao,
  retorno de foco, prompt unico e referencia para os resultados do `appcheck`;
  Q2 implementado e aguardando validacao do diagnostico de falhas isoladas e
  do atalho compacto `q2check`.
- Interface: Desktop, Explorer, Task Manager e Settings possuem modo moderno
  e fallback classico; a taskbar e o Window Manager grafico ainda sao etapas
  futuras.

## Ordem sugerida

1. [01 - Estabilizacao e qualidade](01-estabilizacao-e-qualidade.md)
2. [02 - Plataforma de aplicativos](02-plataforma-de-aplicativos.md)
3. [03 - Kernel e desempenho](03-kernel-e-desempenho.md)
4. [04 - Interface e experiencia](04-interface-e-experiencia.md)
5. [05 - Sistema e ecossistema](05-sistema-e-ecossistema.md)

As frentes podem receber manutencao corretiva a qualquer momento. Para novas
funcionalidades, a prioridade e estabilizar a etapa atual antes de abrir uma
dependencia maior.

## Regra de conclusao de uma etapa

1. Implementar somente o escopo descrito no roadmap da frente.
2. Registrar comandos de diagnostico ou testes no Shell quando houver nova
   capacidade executavel.
3. Validar no QEMU com `make clean && make` e `make run` executados pelo
   usuario.
4. Atualizar este documento, o `ROADMAP.md`, o indice e a documentacao tecnica
   relacionada.
5. Revisar somente os arquivos modificados antes de criar um commit, conforme
   `AGENTS.md`.

## Documentos de apoio

Os documentos em `docs/melhorias futuras/` preservam especificacoes detalhadas
e ideias de longo prazo. Quando houver conflito entre uma especificacao antiga
e estes roadmaps, o roadmap da etapa ativa e o codigo validado sao a fonte de
verdade; a especificacao detalhada deve ser corrigida na mesma tarefa.
