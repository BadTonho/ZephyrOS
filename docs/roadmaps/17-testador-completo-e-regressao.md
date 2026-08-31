# Roadmap 17 — Testador completo e regressão do sistema

## Estado

TST1 concluído; TST2 está implementado e aguarda validação funcional. Esta é uma infraestrutura permanente de qualidade do ZephyrOS,
independente de versão, linguagem, release ou migração tecnológica. Ela deve
ser utilizada durante todo o desenvolvimento para detectar regressões assim
que uma função nova ou uma alteração de código quebrar um comportamento
existente.

O catálogo canônico, o sincronizador e a visão Markdown foram criados e
validados pelo alvo host-only `make catalog-test`. As fases TST2 em diante
continuam planejadas.

## Objetivo

Criar um testador capaz de exercitar o sistema por camadas, registrar os
resultados de forma reproduzível e relacionar cada função, comando e contrato
observável a pelo menos um caso de teste. O testador não promete provar a
ausência absoluta de bugs; ele impede que comportamento não testado seja
confundido com comportamento funcionando.

O testador será separado do código de produção sempre que possível. Testes
internos necessários ao kernel usarão um perfil controlado, fixtures e
backends falsos, sem alterar o comportamento normal de boot ou depender de
hardware específico.

## Escopo

- catálogo único de testes, contratos, pré-condições, resultados e limpeza;
- testes unitários no host para lógica independente de hardware;
- autotestes determinísticos dos subsistemas do kernel;
- testes de integração entre processos, VFS, drivers, Shell e aplicativos;
- executor externo para iniciar, controlar e observar o ZephyrOS no QEMU;
- matriz de perfis com hardware presente, ausente, degradado e com falha;
- fixtures para falta de memória, espaço, rede, energia, integridade e
  dispositivos;
- testes de repetição, timeout, cancelamento, reboot, recuperação e rollback;
- relatórios legíveis por máquina e resumos legíveis por pessoa;
- histórico de regressões, evidências, artefatos e última versão conhecida boa;
- medição de cobertura por contrato, estado, erro e limpeza, além de cobertura
  de código quando a ferramenta for compatível com o módulo.

O testador poderá validar qualquer versão do sistema e continuará válido após
grandes reorganizações internas. Nenhuma suíte dependerá da linguagem usada na
implementação do componente testado.

## Dependências e integração

- `make q3check` continua sendo o gate estático do projeto;
- `health`, `regcheck full`, `memcheck`, `schedcheck`, `proccheck` e os demais
  diagnósticos continuam sendo fontes de observação do guest;
- os `*_self_test()` existentes serão incorporados ao catálogo quando
  possuírem contrato de resultado, restauração e limpeza;
- os Roadmaps 18 em diante fornecerão funcionalidades para a matriz, mas não
  são pré-requisitos para criar o núcleo do testador;
- o testador não substitui a validação manual de interface nem a análise dos
  logs de uma falha.

## Fases

### TST1 — Catálogo de contratos e casos

Implementação inicial: `tests/catalog.json` é a fonte canônica, `tools/test_catalog.py`
descobre e sincroniza superfícies sem usar linhas como identidade, e
`docs/qualidade/catalogo-testes.md` é uma visão gerada. A sincronização cria
superfícies novas como `PENDING` e aposenta superfícies removidas com motivo;
nenhuma cobertura foi declarada automaticamente.

- [x] Inventariar APIs públicas, syscalls, comandos, diagnósticos, drivers,
  processos nativos, aplicativos e transições de estado.
- [x] Definir um identificador estável, proprietário, nível, pré-condições,
  ação, resultado esperado, erros possíveis, efeitos persistentes e limpeza
  para cada caso.
- [x] Separar testes `smoke`, `subsystem`, `integration`, `matrix` e `full`.
- [x] Registrar explicitamente casos positivos, negativos, indisponíveis,
  concorrentes, repetidos, cancelados e interrompidos.
- [x] Bloquear a conclusão de uma função quando seu contrato não possuir teste
  ou justificativa documentada para `SKIP`.

Validação TST1 concluída em 2026-08-30 pelo usuário: `make catalog-test`
reportou catálogo válido e visão válida, com 6.661 superfícies e 0 casos.
As superfícies permanecem `PENDING` até a implementação dos executores e dos
casos de comportamento; isso não invalida a conclusão da infraestrutura do
catálogo.

### TST2 — Executor e protocolo de resultados

- [ ] Criar um executor no host que prepare a imagem, inicie o guest, aguarde
  o boot, envie ações e aplique timeout por caso e por suíte.
- [ ] Capturar console serial, logs do guest, estado de saída do QEMU e
  artefatos auxiliares sem depender de screenshots.
- [ ] Definir marcadores estáveis de início, fim, aprovação, falha, salto e
  bloqueio, `PANIC` e `TIMEOUT`, separados da apresentação humana do Shell.
- [ ] Emitir relatório agregado com `PASS`, `FAIL`, `SKIP` e `BLOCKED`, causa,
  duração, versão da imagem, identificação da fixture, seed e iteração quando
  o caso for de estresse.
- [ ] Garantir que QMP ou o monitor sejam usados somente como controle externo
  do teste; nenhum fallback do ZephyrOS dependerá de porta privada do
  emulador.
- [ ] Isolar cada caso com imagem, fixture ou estado inicial restaurável para
  evitar contaminação entre testes.

### TST3 — Testes unitários e de lógica no host

- [ ] Testar parsers, formatadores, checksums, conversões, limites, overflow,
  manifestos, seleção de versões e regras de atualização.
- [ ] Testar caminhos de erro com stubs de memória, disco, rede, relógio,
  energia e hardware.
- [ ] Usar sanitizadores, análise estática ou ferramentas equivalentes nos
  módulos que puderem ser compilados fora do ambiente freestanding.
- [ ] Manter os testes independentes de tempo real, endereço fixo e ordem
  acidental de execução.

### TST4 — Autotestes determinísticos no kernel

- [ ] Integrar testes de memória, paging, SLAB, scheduler, processos, sinais,
  IPC, threads, filas, VFS, descritores, Storage, rede, ACPI, energia e
  dispositivos.
- [ ] Exercitar sucesso, `ERR_INVALID`, `ERR_UNAVAILABLE`, `ERR_TIMEOUT`,
  `ERR_AGAIN`, `ERR_MEM`, falha de hardware e chamadas fora de ordem quando
  fizerem parte do contrato.
- [ ] Validar invariantes antes e depois de cada caso, incluindo locks,
  interrupções, filas, referências, buffers, descritores e processos.
- [ ] Restaurar estado global e inventários após fixtures negativas ou publicar
  claramente a alteração quando o caso for deliberadamente destrutivo.
- [ ] Proibir escrita real de energia, destruição de dados e alteração de
  hardware nos testes que puderem usar backend falso.

### TST5 — Testes black-box e integração no QEMU

- [ ] Validar boot, montagem, Shell, comandos, diagnósticos, aplicativos,
  processos, rede, atualização, reboot, poweroff e recuperação.
- [ ] Executar os casos somente depois do marcador de boot e manter um
  heartbeat do guest durante operações longas.
- [ ] Verificar que cada operação termina com sucesso, erro, cancelamento,
  timeout ou recurso indisponível sem deixar o prompt, foco ou cena presos.
- [ ] Repetir ciclos de criação/término, abertura/fechamento, mount/unmount,
  atualização/rollback e entrada/saída.
- [ ] Comparar invariantes e estados publicados, não somente textos que
  contenham contadores voláteis, PID, ticks ou endereços.
- [ ] Conservar console.log, relatório, imagem usada e identificação do caso
  para reproduzir cada falha.

### TST6 — Matriz de hardware, estresse, falhas e recuperação

- [ ] Executar perfis Simple e Classic.
- [ ] Executar perfis com e sem ACPI, NIC, USB HID, VESA, áudio, Storage
  adicional e dispositivos PCI opcionais.
- [ ] Exercitar falta de memória, falta de espaço, erro de leitura/escrita,
  pacote inválido, manifesto inválido, rede interrompida, dispositivo ausente
  e operação concorrente.
- [ ] Criar suítes `stress` e `soak` para executar o sistema já inicializado,
  com limite configurável de iterações ou duração e modo planejado
  `--until-failure` para continuar até a primeira falha observável.
- [ ] Misturar operações seguras em ciclos repetidos, como criação/término de
  processos, alocação/liberação, I/O VFS, filas, sockets e abertura/fechamento
  de aplicativos, sem destruir dados ou executar poweroff no perfil completo.
- [ ] Registrar heartbeat, watchdog, seed reproduzível, caso, perfil e número
  da iteração; ao primeiro `FAIL`, `PANIC` ou `TIMEOUT`, preservar o contexto e
  interromper a suíte contaminada.
- [ ] Permitir interrupção externa de uma execução indefinida e aplicar um
  timeout máximo do host para detectar guest congelado ou sem heartbeat.
- [ ] Testar criação, término, zombie, reaping, reutilização de PID e
  revalidação de handles, snapshots e callbacks.
- [ ] Testar interrupção durante staging, boot não saudável, rollback e
  recuperação sem destruir a única cópia válida.
- [ ] Manter uma lista explícita do que é suportado, complementar ou
  `BLOCKED` por ausência de hardware real.

### TST7 — Regressão contínua e cobertura

- [ ] Executar uma suíte rápida após cada alteração e a matriz completa antes
  de uma integração relevante ou release.
- [ ] Transformar toda falha corrigida em um caso permanente de regressão.
- [ ] Comparar a versão atual com a última execução aprovada e destacar novos
  `FAIL`, `SKIP`, `BLOCKED`, warnings e aumento de tempo.
- [ ] Exigir cobertura de todas as APIs públicas, erros documentados,
  transições de estado e caminhos de limpeza; cobertura de linhas isolada não
  será considerada suficiente.
- [ ] Introduzir falhas controladas ou mutation testing nos módulos críticos
  para confirmar que a suíte realmente detecta uma regressão.
- [ ] Publicar a matriz, os artefatos e as limitações junto da versão testada,
  sem apagar evidências de execuções anteriores.

## Arquitetura proposta

Os artefatos devem permanecer separados por responsabilidade:

- `tests/unit/`: testes host de lógica pura e stubs;
- `tests/kernel/`: casos e fixtures dos autotestes internos;
- `tests/qemu/`: suítes black-box e matriz de perfis;
- `tests/stress/`: cenários de estresse, soak, seeds e políticas de parada;
- `tests/fixtures/`: imagens, dados e falhas reproduzíveis;
- `tools/`: executor host e conversores de relatório;
- `docs/qualidade/`: catálogo, cobertura, procedimentos e evidências.

O nome e o formato dos arquivos podem mudar durante a implementação, mas o
executor deve possuir uma fonte de verdade para os casos e não duplicar
expectativas em scripts independentes.

Os perfis de estresse serão separados em `smoke`, `stress` e `soak`. O perfil
`stress` terá limite padrão para uso contínuo em desenvolvimento; o modo
`--until-failure` será explícito e dependerá de watchdog e controle externo.
Cada execução deverá poder ser repetida com a mesma imagem, fixture e seed.

## Contratos e invariantes

- O resultado de um teste será determinado pelo contrato observado, não por
  um atraso arbitrário ou por uma tela visual.
- Uma execução longa deverá distinguir progresso, ausência de heartbeat,
  timeout, panic, falha de contrato e encerramento solicitado pelo usuário.
- Uma falha de estresse deverá conservar seed, iteração, perfil, imagem,
  fixture e console serial suficientes para reproduzir o caso.
- Testes serão idempotentes quando o recurso permitir e declararão seu efeito
  quando não forem.
- Falhas esperadas devem ser verificadas pelo código de erro e pelo estado
  final, não somente por uma mensagem de log.
- Um teste não poderá esconder falhas do sistema para produzir `PASS`.
- O perfil de teste não alterará App API, syscalls, layouts binários ou o
  comportamento normal de produção sem contrato específico.
- Nenhum teste manterá descritor, ponteiro, buffer, processo, lock, fixture ou
  conexão após seu encerramento.
- Funções novas deverão receber testes no mesmo ciclo da implementação; a
  regressão não será uma atividade exclusiva de release.

## Critérios de saída

- Existe um catálogo único e versionado com todos os contratos observáveis.
- O executor inicia uma imagem limpa, aplica uma suíte, captura evidências e
  termina com resultado agregado reproduzível.
- Cada subsistema possui testes de sucesso, falha e limpeza compatíveis com
  seu contrato.
- A matriz de hardware e os casos ausentes ou degradados são identificados
  sem confundir `SKIP` com `PASS`.
- O sistema consegue executar uma suíte de estresse após o boot, interrompê-la
  na primeira falha reproduzível ou no limite configurado e detectar guest sem
  heartbeat.
- Toda falha de estresse gera um relatório reproduzível com seed e iteração,
  sem depender de screenshot.
- Cada regressão corrigida possui um teste permanente.
- Uma falha pode ser reproduzida a partir do relatório, da imagem/fixture e
  da versão registrada.
- A suíte não deixa recursos residuais nem altera permanentemente o ambiente
  usado por outros casos.
- Os resultados deixam claro o que foi executado, o que não foi executado e o
  que permanece sem cobertura.

## Validação

Durante a implementação, o usuário executará os gates atuais do projeto e as
suítes novas do executor. Os nomes finais dos alvos e comandos serão definidos
quando o executor existir; não se deve tratar exemplos deste documento como
comandos já disponíveis.

Cada execução deve registrar data, versão dos fontes, checksum da imagem,
perfil, fixture, resultado agregado e arquivos de diagnóstico. A validação
manual continuará necessária para interfaces gráficas, dispositivos físicos e
qualquer caso que o QEMU não reproduza fielmente.

## Implementacao atual do TST2

A infraestrutura inicial do TST2 foi implementada e permanece pendente de
validacao funcional no QEMU pelo usuario. O canal machine-readable e COM1;
QMP e usado somente para `query-status`, parada e encerramento externos.
`boot.asm` e `stage2.asm` permanecem inalterados.

- `src/drivers/serial.c` fornece polling COM1, fila TX limitada, filtro ASCII
  e flush sem IRQ ou espera bloqueante.
- `src/core/test_protocol.c` permanece inerte ate `HELLO` com versao, sequencia
  e CRC validos e publica `READY`, `HEARTBEAT`, `BEGIN`, `PASS`, `FAIL`,
  `BLOCKED`, `PANIC` e `TIMEOUT`.
- `tools/qemu_test_runner.py` inicia somente imagem existente com `-snapshot`,
  captura serial/stdout/stderr, aplica timeouts e watchdog e aceita `run` e
  `stress` com iteracoes, duracao ou `--until-failure`.
- `build/test-results/<run-id>/` preserva manifesto, checksum, seed, fixture,
  iteracao e `result.json`, separando `status` de `termination`.
- O caso inicial `qemu:tst2:boot-ready` e os alvos `test-qemu` e
  `test-qemu-selftest` foram adicionados sem dependencia de build.

A validacao pendente deve confirmar self-test host-only, boot, handshake,
`READY`, `HEARTBEAT`, `PASS`, watchdog, artefatos e estresse no QEMU antes de
marcar TST2 como concluido.

## Fora do escopo

- vincular o testador à versão 1.0.0 ou a uma release específica;
- vincular o testador a uma versão, release ou reorganização interna;
- substituir revisão de código, análise de contratos ou diagnóstico manual;
- declarar cobertura total apenas com percentual de linhas;
- depender de portas privadas de QEMU, Bochs ou VirtualBox no sistema;
- executar testes destrutivos reais quando houver backend simulado;
- mascarar uma limitação de hardware como aprovação da funcionalidade.
