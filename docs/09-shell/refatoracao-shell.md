# Refatoração do Shell

## Diagnóstico

O arquivo `src/shell/shell.c` concentra responsabilidades demais para o
estágio atual do sistema. A linha de base observada em 20/08/2026 é:

- aproximadamente 12.289 linhas;
- aproximadamente 455 KB;
- cerca de 326 funções;
- entrada do teclado, histórico, prompt e terminal hospedado;
- dispatcher de comandos, diagnósticos, rede, pacotes, updates e abertura de
  aplicativos.

O tamanho do arquivo, por si só, não causa problemas de digitação em runtime.
O problema é o acoplamento: uma alteração no fluxo de entrada pode ser afetada
por código de UI, rede, testes ou carregamento de aplicativos.

## Pontos relacionados à digitação

O fluxo de entrada fica no final de `shell.c`, principalmente em
`shell_init()`, `shell_handle_terminal_key()` e `shell_process_command()`.

Há alguns pontos que devem ser investigados junto com a refatoração:

1. `SHELL_BUFFER_SIZE` é 256, portanto a linha aceita no máximo 255 caracteres
   mais o terminador. Quando o limite é atingido, novos caracteres são
   ignorados silenciosamente.
2. A tabela de scancodes é duplicada no Shell e no driver de teclado. Isso pode
   fazer com que a mesma tecla seja interpretada de forma diferente em cada
   camada.
3. Os comandos são executados diretamente pelo processo do Shell. Operações
   demoradas, como rede, testes ou instalação de pacotes, podem impedir o
   consumo dos eventos de teclado por tempo suficiente para encher filas IPC e
   perder teclas.
4. O dispatcher atual usa uma cadeia extensa de `if/else`, o que torna difícil
   verificar se um comando está sendo reconhecido e se seus argumentos chegam
   corretamente ao handler.

## Refatoração proposta

A refatoração deve ser incremental, mantendo as funções públicas declaradas em
`src/include/apps/shell.h`.

### Fase 1 — Entrada de linha

Estado: concluida e validada. O buffer, historico, prompt, navegacao e
scancodes agora vivem em `src/shell/shell_input.c`; o Shell continua sendo o
responsavel por executar o comando e decidir quando mostrar o prompt.

O contrato de `src/include/apps/shell_input.h` mantém a fronteira explícita:
`shell_input_handle_key()` altera a linha e retorna
`SHELL_INPUT_EVENT_COMMAND_READY` no Enter; `shell.c` consulta o buffer, chama
`shell_process_command()` e decide se deve exibir o próximo prompt.

A implementação de `src/shell/shell_input.c` e seu header concentram:

- buffer e posição do cursor;
- backspace e Enter;
- histórico de comandos;
- Shift e scancodes estendidos;
- conversão de scancode para caractere;
- prompt e edição da linha;
- limite do buffer e aviso ao usuário.

O mapa de teclado foi centralizado em uma única camada compartilhada pelo
driver e pelo Shell. A implementação agora usa a tabela unificada de
`src/drivers/keyboard.c` através de `keyboard_scancode_to_ascii_shifted()`;
`shell.c` não mantém mais uma tabela duplicada.

### Fase 2 — Dispatcher

Estado: concluída e validada no build limpo e no QEMU.

`src/shell/shell_dispatch.c` e `src/include/apps/shell_dispatch.h` agora
separam:

- remoção de espaços e extração do nome do comando;
- preservação do contrato de argumentos existente;
- tabela de comandos;
- chamada do handler correspondente;
- mensagem para comandos desconhecidos.

Todos os comandos top-level atuais usam a tabela com nome, handler e flags de
execução. Adaptadores uniformes permanecem em `shell.c`, portanto os handlers
e suas dependências privadas ainda não foram movidos para os módulos da Fase 3.
As flags são metadados e não alteram prompt, bloqueio ou execução assíncrona.
`shell_process_command()` mantém a API pública, valida entrada nula, retoma o
terminal e delega ao dispatcher.

### Fase 3 — Comandos por domínio

Separar os handlers em módulos pequenos:

- `shell_commands_core.c` — `help`, `clear`, `ls`, `cat`, `echo` e comandos
  básicos;
- `shell_commands_diagnostics.c` — `health`, `log`, `timer`, `wait`,
  `memcheck`, `regcheck` e testes;
- `shell_commands_network.c` — `net`, `ping`, `nslookup` e `http`;
- `shell_commands_apps.c` — Desktop, Explorer, Task Manager, Settings,
  Updater, Media Player e Editor;
- `shell_commands_storage.c` — `storage`, `index` e `search`;
- `shell_checks.c` — estados e workflows dos testes assíncronos;
- `shell_hosted.c` — terminal hospedado no Window Manager.

### Fase 4 — Operações demoradas

O executor cooperativo de `src/shell/shell_job.c` agora atende essa fronteira:
comandos marcados com `SHELL_DISPATCH_FLAG_COOPERATIVE` mantêm um único job
ativo, consomem teclado durante o polling e registram cancelamento, timeout,
falha e saturação da fila IPC. A validação funcional ainda está pendente dos
gates descritos no estado da Fase 4 ao final deste documento.

## Ordem recomendada

1. Extrair a entrada de linha sem mudar a API pública.
2. Centralizar o mapa de teclado.
3. Extrair o dispatcher mantendo os handlers atuais.
4. Separar comandos por domínio.
5. Medir filas e eventos durante comandos demorados.
6. Só então alterar o modelo de execução para operações assíncronas.

## Validação

Após cada etapa de código, o usuário deve executar `make q3check` e, quando o
conjunto estiver estável, `make clean && make`. A Fase 1 foi validada com esses
gates e no QEMU, cobrindo digitação normal, comandos, símbolos, métricas e
consulta do log, sem regressão observada no Shell hospedado.

A Fase 2 foi validada com os mesmos gates e no QEMU, cobrindo caminhos de
comandos conhecidos, argumentos com espaços, entrada vazia, comando
desconhecido, comando longo, diagnósticos, aplicativos nos modos Simple e
Classic e bloqueio de entrada durante `q2check` e `usertest`.

## Referências do diagnóstico

- [Documentação atual do Shell](shell.md)
- [Lista de comandos](comandos.md)
- [Header público do Shell](../../src/include/apps/shell.h)
- [Contrato interno do dispatcher](../../src/include/apps/shell_dispatch.h)
- [Helpers internos de comandos](../../src/include/apps/shell_command_utils.h)
- [Bridge interno de runtime](../../src/include/apps/shell_runtime.h)
- [Implementação do dispatcher](../../src/shell/shell_dispatch.c)
- [Implementação atual do Shell](../../src/shell/shell.c)
- [Driver de teclado](../../src/drivers/keyboard.c)

## Fase 3 — Estado da implementacao

A implementacao estrutural da Fase 3 e a matriz funcional no QEMU foram
concluidas. A marcacao formal como concluida ainda depende da confirmacao de
`q3check` e do build limpo.

### Subfases e fronteiras

- **3A — Core, storage e utilitarios:** `shell_command_utils.c`,
  `shell_commands_core.c` e `shell_commands_storage.c` concentram formatacao,
  parsing de argumentos, comandos basicos e o estado de indexacao/busca.
- **3B — Rede, diagnosticos e testes:** `shell_commands_diagnostics.c`,
  `shell_commands_network.c` e `shell_checks.c` mantem os estados de rede,
  UserTest, RegCheck e AppCheck, incluindo o bloqueio de entrada e os hooks de
  validacao entre modulos.
- **3C — Aplicativos, pacotes e terminal hospedado:**
  `shell_commands_packages.c`, `shell_commands_apps.c` e `shell_hosted.c`
  isolam os workspaces de pacotes, cenas nativas e callbacks do Window
  Manager.

O header interno `src/include/apps/shell_runtime.h` e o unico bridge entre
esses dominios e `shell.c`. Ele nao substitui nem altera `shell.h`: fornece
somente operacoes de ciclo de vida do terminal, prompt, File Manager,
bloqueio de entrada, resultados de testes/App Loader e hooks estreitos para
diagnosticos, rede e reboot/shutdown. Os adaptadores `shell_dispatch_cmd_*`
continuam com uma unica definicao, fora de `shell.c`, e a ordem de consumo dos
resultados do App Loader permanece a mesma.

As funcoes e estados abaixo continuam privados aos modulos:

- Core: estado dos loaders de `echo`, `mem` e `uptime`.
- Storage: workspace de `index` e `search`.
- Diagnosticos/rede: registros, metricas, buffers HTTP e validacoes.
- Checks: estados Q2, RegCheck, AppCheck, UserTest e imagens ZAPP de teste.
- Pacotes: workspaces de Package Manager, App Store e Update.
- Hosted: visibilidade e callbacks da janela do Shell.

Nenhuma alteracao foi feita em `src/boot/boot.asm`.

### Validacao da Fase 3

O QEMU foi validado cobrindo os comandos basicos e `storage/index/search`,
diagnosticos, rede, `q2check`, `regcheck`, `appcheck`, `usertest`, pacotes,
Desktop, Explorer, Task Manager, Settings, Updater, WM, Editor, Player e o
terminal hospedado, incluindo o smoke test Simple/Classic e o bloqueio e
retomada da entrada.

Para fechar os gates formais, o usuario deve executar:

```text
make q3check
make clean && make
```
## Fase 4 - Estado da implementacao

A estrutura inicial de jobs cooperativos foi implementada em
`src/shell/shell_job.c` e `src/include/apps/shell_job.h`. O executor possui um
unico job ativo, contexto estatico, progresso, fases, erros, ticks e contadores
de eventos bloqueados. O comando `job status` consulta o ultimo estado sem
alterar a API publica de `shell.h`.

Os adaptadores cooperativos cobrem rede (`ping`, `nslookup`, `http` e os
subcomandos longos de `net`), `index rebuild`, pacotes/Store/Update e os
workflows Q2, RegCheck e AppCheck. O loop do Shell usa espera IPC curta durante
um job, continua drenando teclado e encaminha mensagens de aplicativo e
resultados do App Loader na ordem original. `Esc` e `F12` solicitam
cancelamento; as demais teclas sao consumidas, com um unico aviso de log.

As operacoes de rede e indice avancam por APIs de estado/poll ja existentes.
Os caminhos de pacotes e atualizacao continuam usando os wrappers sincronos
compatíveis, com bombeamento de eventos nos pontos de cancelamento existentes;
uma decomposicao interna adicional dessas operacoes fica para uma iteracao
posterior. Editor, Player, Task Manager, WM e demais cenas interativas mantem
o fluxo atual.

Ainda nao marcar a Fase 4 como concluida: faltam `make q3check`, build limpo e
validacao funcional no QEMU. A validacao deve confirmar `job status`, sucesso,
falha, timeout, operacao ocupada, cancelamento, ausencia de prompt duplicado,
fila IPC sem saturacao e o smoke test Simple/Classic/terminal hospedado.

Referencias adicionais:

- [Contrato do executor de jobs](../../src/include/apps/shell_job.h)
- [Implementacao do executor](../../src/shell/shell_job.c)
- [Comandos do Shell](comandos.md)
