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

- [x] Definir uma lista curta de boot, Shell, Desktop, Explorer, Settings e
  Task Manager para ser repetida apos cada etapa relevante.
- [x] Registrar em documentacao quais falhas sao esperadas em `appcheck` e
  quais sintomas representam regressao real.
- [x] Verificar retorno de foco e existencia de somente um prompt apos apps
  assincronas, dialogos e interfaces nativas.
- [x] Testar explicitamente cenarios sem VESA, filesystem ou AC97 quando uma
  mudanca tocar essas dependencias.

Validacao registrada no QEMU apos a Fase 6C: `health`, `threadtest`,
`appcheck`, `echo`, `uptime`, `mem`, `usertest`, `usertest fault` e
`app inputtest` com encerramento normal e por `F12` funcionaram; as interfaces
classica e moderna tambem foram abertas e fechadas sem regressao observada.
Esta etapa define o protocolo; ela nao cria um novo comando, uma variante de
build ou uma simulacao de falha. Os cenarios sem VESA, filesystem ou AC97
permanecem obrigatorios sempre que uma mudanca futura tocar essas dependencias.

### Matriz de regressao manual

Execute esta lista apos uma mudanca relevante e registre qualquer divergencia
antes de iniciar a proxima etapa.

| Area | Sequencia | Aprovacao |
|------|-----------|-----------|
| Boot e Shell | Inicie o sistema; execute `health`, use `PgUp`, `PgDn` e `End`, depois execute `threadtest`, `echo regressao q1`, `uptime` e `mem`. | Sem `KERNEL PANIC`; `health` permanece navegavel; cada comando conclui e o Shell aceita o proximo. |
| Ring 3 e loader | Execute `appcheck` e aguarde sua conclusao, incluindo o demonstrativo do loader e as migracoes de `uptime` e `mem`. Execute `app inputtest`, envie uma tecla e termine com `Enter`; execute-o novamente e termine com `F12`. Execute `usertest` e `usertest fault`. | Os fluxos normais concluem, a falha isolada encerra somente o processo de usuario e o Shell continua utilizavel. |
| Interfaces nativas | Em `guimode classic` e novamente em `guimode modern`, abra e feche Desktop, Explorer, Settings e Task Manager. No Explorer, abra o dialogo de nova pasta com `F6` e cancele com `Esc`, sem confirmar ou gravar nada. | Cada interface abre no modo disponivel, fecha de forma controlada e devolve o controle ao Shell. |

### Foco, prompt e limpeza

Ao fim de cada aplicativo assincrono, dialogo ou interface que retornar ao
terminal, o Shell deve aceitar entrada com exatamente um `zephyr>` visivel.
Saida duplicada, prompt ausente, dois prompts ou entrada entregue a um
aplicativo ja encerrado sao regressao.

Para `appcheck`, alem da saida visual, a aprovacao requer as verificacoes de
foco retornado e a preservacao das contagens anteriores de processos de
usuario e zumbis. Um aplicativo em primeiro plano deve receber foco apenas
enquanto estiver ativo; apos `Enter`, `F12`, encerramento normal ou falha
isolada, o foco volta ao Shell.

### Como interpretar o `appcheck`

O diagnostico executa tanto caminhos validos quanto entradas deliberadamente
invalidas. Portanto, uma linha com `ERRO` nao e automaticamente uma
regressao: ela pode representar o retorno controlado esperado da API.

- Devem retornar `OK`: versao, console, uptime e memoria validos; IPC valido;
  operacoes de arquivo validas quando houver filesystem e arquivo elegivel;
  validacao e execucao do demonstrativo ZAPP, limpeza de `DEMO.ZAP`, foco
  adquirido/devolvido e as migracoes de `uptime` e `mem`.
- As verificacoes comparadas devem aparecer como `OK`: `memory_info nulo`
  espera `ERR_NULL`; `migracao_uptime_concorrente` e
  `migracao_mem_concorrente` esperam `ERR_STATE` enquanto o loader esta
  ocupado.
- `ERRO` e deliberado para numero de syscall invalido, ponteiros e textos
  invalidos, `process_exit` fora do modo usuario, handles e arquivos
  inexistentes ou expirados, I/O acima do limite, PID ou mensagem IPC
  invalidos, argumentos excessivos e imagens ZAPP com cabecalho, flags,
  tamanho ou ponto de entrada invalidos. A suite deve continuar apos cada um
  desses retornos controlados.
- `file_open_sem_arquivo` tambem e esperado quando o filesystem nao possui
  arquivo regular elegivel para leitura. `file_service_indisponivel` e
  `loader_indisponivel` so sao esperados quando seus componentes estiverem
  indisponiveis no `health`; nesse caso, os testes dependentes nao devem ser
  interpretados como falha da plataforma.

E regressao qualquer erro em caminho valido, divergencia em uma verificacao
comparada, arquivo temporario remanescente, foco ou limpeza ausentes,
`KERNEL PANIC`, travamento ou prompt ausente/duplicado.

### Gatilhos para recursos opcionais

Quando uma mudanca tocar uma das dependencias abaixo, execute tambem uma
configuracao ja existente em que ela esteja indisponivel. Nao crie hook,
comando ou variante de build apenas para este teste.

| Dependencia | Confirmar |
|-------------|-----------|
| VESA ou backbuffer | `health` informa o estado; `guimode modern` usa fallback classico ou falha de modo controlado; Shell e interfaces classicas continuam acessiveis. |
| Filesystem | `health` informa o estado; operacoes dependentes de arquivos e loader retornam indisponibilidade controlada; Shell e diagnosticos continuam acessiveis. |
| AC97 | `health` informa o estado; a tentativa de fluxo de audio retorna indisponibilidade controlada; Shell permanece operacional. |

## Etapa Q2 - Diagnostico utilizavel

- [x] Manter `health` completo e navegavel, sem esconder dados para caber em
  uma tela.
- [x] Revisar logs de sucesso excessivos em comandos comuns, sem remover logs
  de erro ou diagnosticos necessarios.
- [x] Guardar a ultima falha isolada de aplicativo e exibir apenas dados que
  nao exponham ponteiros ou estado interno sensivel.
- [x] Adicionar contadores apenas quando eles responderem uma pergunta de
  diagnostico concreta.
- [x] Adicionar `q2check` para resumir automaticamente duas falhas isoladas,
  logger, resumo seguro e limpeza, sem substituir `appcheck` ou `F12`.
- [x] Validado no QEMU: `q2check` com duas falhas isoladas, `health` com
  contador acumulado e resumo seguro, navegacao do scrollback, cancelamento
  por `F12`, comandos ZAPP, `appcheck` e ausencia de processos de usuario ou
  zumbis residuais.

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
