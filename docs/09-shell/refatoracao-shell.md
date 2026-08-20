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

Estado: implementada estruturalmente. O buffer, historico, prompt, navegacao e
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

Criar `src/shell/shell_dispatch.c` para separar:

- remoção de espaços e extração do nome do comando;
- validação dos argumentos;
- tabela de comandos;
- chamada do handler correspondente;
- mensagem para comandos desconhecidos.

Uma tabela com nome, handler e flags de execução deve substituir gradualmente a
cadeia de `if/else`, sem exigir a conversão de todos os comandos de uma vez.

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

Depois da separação estrutural, comandos demorados devem usar uma operação
cooperativa ou uma work queue. Enquanto isso não existir, o Shell deve deixar
claro quando a entrada está temporariamente bloqueada e registrar falhas de
fila com o módulo `SHELL` ou `KBD`.

## Ordem recomendada

1. Extrair a entrada de linha sem mudar a API pública.
2. Centralizar o mapa de teclado.
3. Extrair o dispatcher mantendo os handlers atuais.
4. Separar comandos por domínio.
5. Medir filas e eventos durante comandos demorados.
6. Só então alterar o modelo de execução para operações assíncronas.

## Validação

Após cada etapa de código, o usuário deve executar `make q3check` e, quando o
conjunto estiver estável, `make clean && make`. A validação no QEMU deve cobrir
digitação normal, Backspace, Enter, histórico, Shift, barra `/`, comandos
longos e entrada durante operações demoradas.

## Referências do diagnóstico

- [Documentação atual do Shell](shell.md)
- [Lista de comandos](comandos.md)
- [Header público do Shell](../../src/include/apps/shell.h)
- [Implementação atual do Shell](../../src/shell/shell.c)
- [Driver de teclado](../../src/drivers/keyboard.c)
