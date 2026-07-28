# Roadmaps executaveis do ZephyrOS

Este diretorio separa o planejamento do ZephyrOS por frentes de trabalho. O
arquivo [ROADMAP.md](../../ROADMAP.md) continua sendo o mapa geral do projeto;
os documentos daqui definem ordem, limites e criterios de saida de cada frente.

## Estado de referencia

- Base original (boot, memoria, processos, filesystem, Shell e desktop):
  validada no QEMU.
- Plataforma de aplicativos: Fases 1 a 7 validadas, incluindo processos ring
  3, syscalls, loader ZAPP, foco, teclado, argumentos simples, as migracoes
  internas de `echo`, `uptime` e `mem`, e o contrato de console e ciclo de
  vida validado por `app outputtest [fail]`. A Fase 7 validou `ZPKG` v1,
  empacotador host, alias FAT12 `.ZPK`, servico `PKG` e comandos `pkg` no
  fluxo completo de instalacao, execucao e remocao.
- Estabilizacao e qualidade: Q1 e Q2 validados no QEMU, com matriz de
  regressao, retorno de foco, prompt unico, referencia para os resultados do
  `appcheck`, politica de logs, resumo seguro de falhas isoladas e o atalho
  compacto `q2check`; Q3 validado com o gate `make q3check`, seus auto-testes,
  build e smoke test da matriz de regressao no QEMU. Q4 foi validada com
  `regcheck`, cancelamento real por `F12`, `procs`, `health`, `schedcheck` e
  `memcheck`, sem substituir a matriz detalhada.
- Kernel e desempenho: K1 foi validada no QEMU com `kmetrics`, linha-base
  manual de PIT, scheduler, filas, memoria e VESA nos cenarios de Shell, ring
  3 e interfaces classica/moderna. CPU real foi adiada; `TCK%` identifica
  somente a estimativa baseada em ticks. K2 foi validada no QEMU com
  `schedcheck`, preempcao de ring 3, testes de regressao e interfaces
  classica/moderna; permanece o round-robin de 1 tick para ring 3. K3 foi
  validada no QEMU com estatisticas e guardas de heap/PMM, registro seguro de
  diretorios de usuario e o diagnostico compacto `memcheck`, inclusive apos
  ciclos ring 3 e nas interfaces classica/moderna. K4 foi validada no QEMU:
  a copia VESA do cursor por regioes minimas reduziu bytes no cenario manual,
  preservando foco, Shell e ausencia de artefatos visuais.
- Interface: Desktop, Explorer, Task Manager e Settings possuem modo moderno
  e fallback classico; a taskbar e o Window Manager grafico ainda sao etapas
  futuras.
- Sistema e ecossistema: S1.1 concluida com inventario de dispositivos somente
  de leitura e diagnostico de energia sem ACPI, validados manualmente no QEMU
  com os fallbacks e a matriz de regressao preservados. S1.2 concluiu a
  descoberta ACPI somente de leitura, validada no QEMU padrao e sem ACPI.
  S1.3 concluiu a observacao de PM1, modo ACPI e `_S5_`, validada no QEMU
  padrao e sem ACPI; nenhuma transicao foi habilitada.

## K4 validada

A K4 compara a copia VESA do cursor por regioes minimas, sem abrir mudancas
de scheduler, memoria, App API ou bootloader. A comparacao antes/depois no
QEMU confirmou reducao de bytes e ausencia de rastro ou piscada apos a
correcao da ordem de apresentacao.

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
