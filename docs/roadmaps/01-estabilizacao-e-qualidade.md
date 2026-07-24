# Roadmap 01 - Estabilizacao e qualidade

## Objetivo

Manter o ZephyrOS utilizavel enquanto novas capacidades sao adicionadas. Esta
frente nao cria recursos grandes: ela reduz regressoes, preserva fallbacks e
transforma falhas observadas em testes repetiveis.

## Base ja disponivel

- Recovery com estados `READY`, `DEGRADED` e `DISABLED`.
- `health`, `appcheck`, `usertest`, `usertest fault` e `threadtest`.
- Fallback classico quando VESA ou backbuffer nao estao disponiveis.
- Isolamento de falhas ring 3 e `KERNEL PANIC` reservado a falhas de kernel.
- Scrollback de 200 linhas e `clear` no Shell.

## Etapa Q1 - Matriz de regressao

- [ ] Definir uma lista curta de boot, Shell, Desktop, Explorer, Settings e
  Task Manager para ser repetida apos cada etapa relevante.
- [ ] Registrar em documentacao quais falhas sao esperadas em `appcheck` e
  quais sintomas representam regressao real.
- [ ] Verificar retorno de foco e existencia de somente um prompt apos apps
  assincronas, dialogos e interfaces nativas.
- [ ] Testar explicitamente cenarios sem VESA, filesystem ou AC97 quando uma
  mudanca tocar essas dependencias.

## Etapa Q2 - Diagnostico utilizavel

- [ ] Manter `health` completo e navegavel, sem esconder dados para caber em
  uma tela.
- [ ] Revisar logs de sucesso excessivos em comandos comuns, sem remover logs
  de erro ou diagnosticos necessarios.
- [ ] Guardar a ultima falha isolada de aplicativo e exibir apenas dados que
  nao exponham ponteiros ou estado interno sensivel.
- [ ] Adicionar contadores apenas quando eles responderem uma pergunta de
  diagnostico concreta.

## Etapa Q3 - Disciplina de mudanca

- [ ] Toda nova funcao que pode falhar retorna codigo controlado e registra o
  erro no modulo correto.
- [ ] Alteracoes de estruturas publicas atualizam todos os chamadores e os
  documentos tecnicos correspondentes.
- [ ] Nenhuma otimização e aceita sem comparacao observavel ou metrica.
- [ ] Alteracoes no bootloader permanecem fora desta frente e exigem decisao
  explicita do mantenedor.

## Criterio de saida

Uma etapa funcional so avanca quando o boot e os comandos de diagnostico
continuam acessiveis, os modos classico e moderno funcionam e uma falha de
recurso opcional nao derruba o kernel.
