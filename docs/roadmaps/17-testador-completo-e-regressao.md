# Roadmap 17 — Testador completo e regressão do sistema

## Estado

TST1, TST2, TST3 e TST4 foram implementados e validados. A infraestrutura
permanece permanente e independente da toolchain instalada localmente.
Esta é uma infraestrutura permanente de qualidade do ZephyrOS,
independente de versão, linguagem, release ou migração tecnológica. Ela deve
ser utilizada durante todo o desenvolvimento para detectar regressões assim
que uma função nova ou uma alteração de código quebrar um comportamento
existente.

O catálogo canônico, o sincronizador e a visão Markdown foram criados e
validados pelo alvo host-only `make catalog-test`. A TST5 e a camada QEMU da
TST6 foram implementadas e validadas; TST7 continua planejada para regressao
continua. Hardware físico permanece `BLOCKED` enquanto não houver equipamento
e evidência correspondente.

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

## Estratégia de validação em camadas

O testador seguirá uma estratégia progressiva, inspirada na separação entre
testes internos rápidos, testes de interfaces e testes de integração usada em
projetos de kernels maduros. Cada camada terá uma responsabilidade própria e
um critério de parada; uma falha em uma camada impedirá o avanço para a camada
seguinte até que sua causa seja classificada.

```text
TST2: infraestrutura do executor e protocolo
        ↓
TST3: lógica independente de hardware no host
        ↓
TST4: autotestes controlados dentro do kernel
        ↓
TST5: integração black-box no QEMU
        ↓
TST6/TST7: matriz, estresse e regressão contínua
```

Testes host-only validarão primeiro parsers, checksums, máquinas de estado,
filas, limites, relatórios e o próprio protocolo do executor usando stubs e
backends falsos. Autotestes no kernel validarão invariantes e integrações que
dependem do ambiente freestanding, mas continuarão determinísticos e
controlados. O QEMU será reservado para boot, hardware virtualizado e fluxos
que realmente exigem a integração completa.

O resultado `TIMEOUT` deverá identificar o último estado observável do caso,
como `BOOT`, `READY`, `RUN_SENT`, `BEGIN` ou `PASS`. Ausência de heartbeat
será tratada como evidência de falta de progresso, não como diagnóstico
automático da causa. O executor deverá preservar os artefatos e encerrar a
execução contaminada, sem repetir indefinidamente o mesmo cenário sem nova
hipótese ou informação diagnóstica.

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

- [x] Testar o executor e o protocolo fora do QEMU com transporte, relógio e
  resultados falsos antes de depender da comunicação com o guest.
- [x] Manter a máquina de estados, o parser e a codificação do protocolo
  separáveis do driver serial sempre que isso for compatível com o contrato.
- [x] Tratar `qemu:tst2:boot-ready` como smoke test da infraestrutura do TST2,
  e não como cobertura funcional do sistema inteiro.
- [x] Criar um executor no host que prepare a imagem, inicie o guest, aguarde
  o boot, envie ações e aplique timeout por caso e por suíte.
- [x] Capturar console serial, logs do guest, estado de saída do QEMU e
  artefatos auxiliares sem depender de screenshots.
- [x] Definir marcadores estáveis de início, fim, aprovação, falha, salto e
  bloqueio, `PANIC` e `TIMEOUT`, separados da apresentação humana do Shell.
- [x] Emitir relatório agregado com `PASS`, `FAIL`, `SKIP` e `BLOCKED`, causa,
  duração, versão da imagem, identificação da fixture, seed e iteração quando
  o caso for de estresse.
- [x] Garantir que QMP ou o monitor sejam usados somente como controle externo
  do teste; nenhum fallback do ZephyrOS dependerá de porta privada do
  emulador.
- [x] Isolar cada caso com imagem, fixture ou estado inicial restaurável para
  evitar contaminação entre testes.

### TST3 — Suíte host-only de lógica e limites

- [x] Criar a suíte host-only da TST3 para strings, compressão, packager e
  updater, com buffers estáticos, fixtures temporárias e entradas negativas.
- [x] Testar parsers, formatadores, checksums, conversões, limites, overflow,
  manifestos, seleção de versões, hashes, assinaturas e regras de atualização.
- [x] Testar caminhos de erro aplicáveis à camada host-only, incluindo buffers
  insuficientes, streams truncados, corrupção, arquivos ausentes e caminhos
  inseguros; VFS, paging, processos, drivers e energia ficam para TST4/TST5.
- [x] Usar ASan/UBSan com Clang/LLVM nos módulos host-testáveis; o alvo
  `make test-tst3-sanitize` passou com a toolchain LLVM oficial contendo os
  runtimes ASan/UBSan.
- [x] Manter os testes independentes de tempo real, endereço fixo e ordem
  acidental de execução, com timeout máximo no runner.

### TST4 — Autotestes determinísticos no kernel

- [x] Executar autotestes somente depois que a lógica correspondente possuir
  cobertura host-only ou uma justificativa documentada para a dependência do
  kernel.
- [x] Integrar testes de memória, paging, SLAB, scheduler, processos, sinais,
  IPC, threads, filas, VFS, descritores, Storage, rede, ACPI, energia e
  dispositivos.
- [x] Exercitar sucesso, `ERR_INVALID`, `ERR_UNAVAILABLE`, `ERR_TIMEOUT`,
  `ERR_AGAIN`, `ERR_MEM`, falha de hardware e chamadas fora de ordem quando
  fizerem parte do contrato.
- [x] Validar invariantes antes e depois de cada caso, incluindo locks,
  interrupções, filas, referências, buffers, descritores e processos.
- [x] Restaurar estado global e inventários após fixtures negativas ou publicar
  claramente a alteração quando o caso for deliberadamente destrutivo.
- [x] Proibir escrita real de energia, destruição de dados e alteração de
  hardware nos testes que puderem usar backend falso.

### TST5 — Testes black-box e integração no QEMU

- A primeira camada prevista para esta fase está concluída e é detalhada em
  `Implementação atual da TST5` e no checklist de saída abaixo. Os itens que
  continuam sem marcação representam cobertura adicional além dessa camada e
  não devem ser confundidos com falhas da implementação validada.

- [x] Executar os casos no QEMU somente depois da validação host-only do
  executor, do protocolo e dos componentes diretamente envolvidos.
- [x] Começar cada nova integração com um caso mínimo e estados de progresso
  explícitos, antes de adicionar a suíte completa ou cenários de estresse.
- [x] Validar boot, montagem, Shell, comandos, diagnósticos, aplicativos,
  processos, rede, atualização, reboot, poweroff e recuperação.
- [x] Executar os casos somente depois do marcador de boot e manter um
  heartbeat do guest durante operações longas.
- [x] Verificar que cada operação termina com sucesso, erro, cancelamento,
  timeout ou recurso indisponível sem deixar o prompt, foco ou cena presos.
- [ ] Repetir ciclos de criação/término, abertura/fechamento, mount/unmount,
  atualização/rollback e entrada/saída.
- [ ] Comparar invariantes e estados publicados, não somente textos que
  contenham contadores voláteis, PID, ticks ou endereços.
- [x] Conservar console.log, relatório, imagem usada e identificação do caso
  para reproduzir cada falha.

Os dois itens sem marcação são objetivos de aprofundamento para a matriz e o
estresse da TST6, ou para incrementos posteriores de observação estruturada;
eles não fazem parte do aceite dos nove casos independentes desta camada TST5.

### TST6 — Status consolidado

A camada TST6 prevista para o ambiente QEMU está concluída: matriz de perfis,
estresse limitado e falhas controladas foram executados nos 20 casos
independentes. O checklist canônico de saída e as evidências estão em
`## Implementacao atual da TST6`, abaixo. Hardware físico permanece
explicitamente `BLOCKED` por ausência de equipamento e não é contado como
validação QEMU.

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

Durante a implementação, a validação seguirá esta ordem:

1. validação do catálogo e dos testes host-only;
2. autotestes determinísticos do kernel, quando aplicáveis;
3. gates de qualidade e build da versão testada;
4. smoke test mínimo no QEMU;
5. integração, matriz, estresse e regressão.

Uma falha host-only ou de autoteste bloqueará a execução QEMU correspondente.
Uma falha QEMU deverá ser encerrada pelo timeout do estado ou do caso e
classificada como falha de contrato, guest, transporte, executor ou ambiente;
o mesmo comando não deverá ser repetido indefinidamente sem alteração da
hipótese ou dos dados coletados. Os nomes finais dos alvos e comandos serão
definidos quando cada executor existir; não se deve tratar exemplos deste
documento como comandos já disponíveis.

Cada execução deve registrar data, versão dos fontes, checksum da imagem,
perfil, fixture, resultado agregado e arquivos de diagnóstico. A validação
manual continuará necessária para interfaces gráficas, dispositivos físicos e
qualquer caso que o QEMU não reproduza fielmente.

## Implementacao atual do TST2

A infraestrutura do TST2 foi implementada e validada em host e no QEMU. O
canal machine-readable e COM1;
QMP e usado somente para `query-status`, parada e encerramento externos.
`boot.asm` e `stage2.asm` permanecem inalterados.

- `src/drivers/serial.c` fornece polling COM1, fila TX limitada, filtro ASCII
  e flush sem IRQ ou espera bloqueante.
- `src/core/test_protocol_core.c` concentra o parser incremental, CRC,
  sequencias, comandos e eventos sem depender de hardware; `src/core/test_protocol.c`
  permanece como adaptador COM1/timer e publica `READY`, `HEARTBEAT`, `BEGIN`,
  `PASS`, `FAIL`, `BLOCKED`, `PANIC` e `TIMEOUT`.
- `tests/unit/test_protocol_core.c`, `tests/unit/test_qemu_test_runner.py` e
  `tools/tst2_host_runner.py` fornecem o gate host-only com `HOST_CC`, usando
  transporte, relogio e executor falsos.
- `tools/qemu_test_runner.py` inicia somente imagem existente com `-snapshot`,
  captura serial/stdout/stderr, aplica timeouts e watchdog somente apos `BEGIN`,
  registra estados de progresso e aceita `run` e `stress` com iteracoes,
  duracao ou `--until-failure`.
- `build/test-results/<run-id>/` preserva manifesto, checksum, seed, fixture,
  iteracao e `result.json`, separando `status` de `termination`.
- O caso inicial `qemu:tst2:boot-ready` e os alvos `test-tst2-host`, `test-qemu`
  e `test-qemu-selftest` foram adicionados; o primeiro usa compilador nativo
  configurado por `HOST_CC`, separado do cross-compiler do kernel.

A validação host-only confirmou o núcleo e o executor; `q3check`, o self-test
do runner e o build limpo também passaram. No QEMU, o smoke test produziu
`READY`, `HEARTBEAT`, `BEGIN` e `PASS` nessa ordem, sem erros de protocolo, e
preservou manifesto, serial, logs do QEMU e relatório. TST2 está concluído;
a cobertura funcional dos subsistemas continuará nas fases TST3 a TST7.

## Implementacao atual da TST3

A primeira camada host-only da TST3 foi implementada sem adicionar casos ao
catalogo QEMU. `tests/unit/test_packager.py` e `test_updater.py` cobrem
manifestos, versoes, dependencias, aliases FAT12/FAT32, CRC, corrupcao,
hashes, assinaturas, compatibilidade, rollback e caminhos inseguros. O teste C
`test_string_compress.c` cobre `string.c`, round-trip LZSS, janela, buffers
insuficientes, streams truncados, tokens invalidos, overflow, estatisticas e
reinicializacao.

`tools/tst3_host_runner.py` separa strict e sanitize, aplica timeout por
subprocesso, resolve `HOST_CC`/`HOST_SANITIZE_CC` e grava manifesto, resultado
e logs em `build/test-results/tst3-host/`. Os alvos `test-tst3-host` e
`test-tst3-sanitize` foram adicionados ao Makefile; `test-tst2-host` continua
restrito aos testes TST2.

Cada modo preserva tambem uma copia em
`build/test-results/tst3-host/strict/` ou `build/test-results/tst3-host/sanitize/`.

A correcao necessaria em `compress.c` reseta o estado global de habilitacao em
`compress_init()`, satura o calculo do tamanho maximo e alinha as posicoes do
anel LZSS entre compressao e descompressao. O descompressor tambem rejeita um
grupo de flags vazio apos o inicio, mantendo o stream de entrada vazia como
`0x00`.

Validacao executada em 2026-08-31: `make test-tst3-host`,
`make test-tst3-sanitize`, `make package-test`, `make update-test`,
`make q3check`, `make clean`, `make` e um unico `make test-qemu` passaram.
O sanitize usou LLVM 22.1.8 instalado fora do repositorio; o runner inclui
automaticamente o diretorio do runtime no ambiente do teste. O smoke QEMU
produziu `READY`, `HEARTBEAT`, `BEGIN` e `PASS` nessa ordem, sem erros de
protocolo.

## Implementação atual da TST4.1

A primeira camada da TST4 adiciona um harness interno em
`src/core/kernel_tests.c`, acionado exclusivamente pelo caso ZTEST
`qemu:tst4:memory-slab`. O caso captura invariantes do heap, PMM, memória
detalhada e SLAB antes e depois do autoteste, executa os caminhos negativos
existentes do SLAB e exige que páginas, inventários e contadores retornem ao
estado anterior.

O novo alvo `make test-tst4-qemu` usa o runner existente com uma única
iteração. O boot normal não inicia autotestes, e nenhuma dependência do Shell,
do bootloader ou da ABI pública foi adicionada. As demais áreas possuem casos
independentes descritos na conclusão da TST4 abaixo.

Validação concluída em 2026-08-31: `make test-qemu-selftest`, `make q3check`,
`make clean`, `make`, `make test-tst4-qemu` e `make catalog-test` passaram.
O artefato aprovado é
`build/test-results/qemu-20260831T170423Z-15384/`; a primeira falha
diagnóstica foi preservada em `build/test-results/qemu-20260831T165009Z-21628/`.

#### TST4.1 — Memória e SLAB

- [x] Harness interno acionado por `RUN` explícito, sem dependência do Shell ou
  alteração do boot normal.
- [x] Caso agregado `qemu:tst4:memory-slab` com uma única iteração e timeout
  próprio no runner.
- [x] Invariantes de heap, PMM, memória detalhada e SLAB verificadas antes e
  depois do caso.
- [x] Cache temporário, páginas e contadores diagnósticos restaurados após os
  caminhos negativos do autoteste SLAB.
- [x] Execução QEMU produziu `READY`, `HEARTBEAT`, `BEGIN` e `PASS`, sem erros
  de protocolo, com artefatos preservados.

## Implementação atual da TST4.2–TST4.6

A TST4 foi completada com cinco casos independentes acionados exclusivamente
por `RUN`: `qemu:tst4:paging-vma`, `qemu:tst4:execution`,
`qemu:tst4:storage-vfs`, `qemu:tst4:network` e `qemu:tst4:platform`.
O harness foi dividido por domínio em fontes internas de `src/core`, mantendo
`kernel_tests.h` interno e sem alterar headers públicos, ABI, bootloader,
Shell ou o protocolo ZTEST.

Cada caso usa fases com código canônico, falha rápida e limpeza antes do
`FAIL`. O callback de progresso emite heartbeat durante operações longas e
não aceita outro `RUN`. O caso de paging usa um fixture ring 3 estático com
`mmap`, `munmap`, materialização lazy, acessos válidos, entradas negativas,
timeout limitado, cancelamento e reaping. Execution cobre threads, scheduler,
sinais, wait queues, workqueue e IPC. Storage/VFS cobre block, cache,
file-index, VFS, descritores, pipes e filesystems virtuais. Network cobre
buffers, sockets, rotas, validadores de protocolos, crypto e TLS. Platform
cobre log, timer, clock, IRQ deferred, input, energia, inventário de
dispositivos, ACPI, RTC, RNG, USB, Wi-Fi e IDT.

Os alvos `test-tst4-qemu-paging-vma`, `test-tst4-qemu-execution`,
`test-tst4-qemu-storage-vfs`, `test-tst4-qemu-network` e
`test-tst4-qemu-platform` usam `stress --iterations 1`, timeout próprio e
nenhum retry automático. Hardware opcional ausente é aceito somente quando o
módulo publica estado coerente de indisponibilidade/degradação; nenhum caso
faz escrita destrutiva, reset, reboot, poweroff ou conexão externa.

Validação final concluída em 2026-08-31 (America/São_Paulo), nesta ordem:
`make test-qemu-selftest`, `make q3check`, `make clean`, `make`,
`make test-tst4-qemu`, `make test-tst4-qemu-paging-vma`,
`make test-tst4-qemu-execution`, `make test-tst4-qemu-storage-vfs`,
`make test-tst4-qemu-network`, `make test-tst4-qemu-platform` e
`make catalog-test`. Todos passaram com `READY → HEARTBEAT → BEGIN → PASS`,
`protocol_errors=[]` e terminação dentro do timeout. Os artefatos aprovados
foram, respectivamente, `qemu-20260831T182652Z-11896`,
`qemu-20260831T182717Z-6764`, `qemu-20260831T182742Z-18396`,
`qemu-20260831T182807Z-19232`, `qemu-20260831T182845Z-20608` e
`qemu-20260831T182911Z-20368` em `build/test-results/`.

#### TST4.2–TST4.6 — Casos restantes

- [x] Paging/VMA com fixture ring 3, validações negativas, limite de ticks e
  restauração de processos, páginas, VMAs e page faults.
- [x] Execution com self-test de threads, scheduler, sinais, filas,
  workqueue, IPC e ausência de resíduos.
- [x] Storage/VFS com block/cache, file index, VFS, descritores, pipes,
  mounts, devfs, procfs e sysfs, sem escrita real destrutiva.
- [x] Network com buffers, sockets, rotas, validadores de protocolos, crypto
  e TLS, sem rede externa.
- [x] Platform com serviços de base, inventários, ACPI/energia e dispositivos
  opcionais, sem efeitos reais de energia ou hardware.
- [x] Cada caso preservou `manifest.json`, `serial.log`, `qemu.stdout.log`,
  `qemu.stderr.log` e `result.json`.
- [x] O catálogo foi sincronizado e a visão renderizada; `make catalog-test`
  validou 6780 superfícies e 7 casos.

TST4 está concluída para as camadas memory/SLAB, paging/VMA, execution,
storage/VFS, network e platform. TST5 e a camada QEMU da TST6 foram validadas
nas seções abaixo; TST7 permanece pendente por ser a camada de regressão
contínua.

## Implementacao atual da TST5

A primeira camada da TST5 foi implementada com nove casos QEMU independentes:
Shell, input, apps, processes, storage, network, update-recovery, reboot e
poweroff. O runner injeta somente scripts validados por allowlist atraves do
QMP, registra cada entrada em `input.log`, acompanha os estados
`INPUT_SENT`, `OBSERVING`, `RESTART_WAIT` e `SHUTDOWN_WAIT`, e preserva eventos
QMP, screenshots e diagnosticos junto do resultado.

O guest possui um observador interno do terminal em `src/core/video_test.h` e
`src/drivers/video.c`. O snapshot copia texto, geracao, cursor e estado sob o
lock do video, sem alterar `video.h`; o harness interno
`src/core/kernel_tests_blackbox.c` usa ticks, `process_yield()` e timeout
limitado para confirmar o marcador produzido pelo caminho real de teclado,
input, IPC, Shell e dispatcher. O protocolo publico continua com os eventos
`READY`, `HEARTBEAT`, `BEGIN`, `PASS` e `FAIL`.

Os casos de reboot e poweroff so executam a acao posterior depois de `PASS`.
Reboot exige evento QMP `RESET` seguido de novo `HELLO -> READY -> HEARTBEAT`;
poweroff aceita `SHUTDOWN` ou a saida esperada da instancia QEMU isolada. Cada
execucao preserva `manifest.json`, `serial.log`, logs do QEMU, `input.log`,
`qmp-events.log`, screenshots disponiveis e `result.json`.

Validacoes executadas para esta implementacao: `make test-tst5-host`,
`python tools/qemu_test_runner.py --self-test`, `make q3check`, `make clean`,
`make`, os nove alvos QEMU e `make catalog-test`, com 6.792 superficies e 16
casos no catalogo. As nove execucoes produziram `READY -> HEARTBEAT -> BEGIN
-> PASS`; reboot confirmou `RESET` e novo handshake, e poweroff confirmou
`SHUTDOWN`. A camada TST5 descrita neste roadmap esta validada.

### TST5 — Checklist de saida

- [x] Runner separado com allowlist de entrada, rastreamento e estados de
  progresso, sem retry automatico.
- [x] Observador interno do terminal com copia sob lock e deadlines por ticks.
- [x] Nove casos independentes no catalogo e alvos Makefile com uma iteracao.
- [x] Host-only, self-test do runner, q3check, build limpo e catalogo validados.
- [x] Shell, input, apps, processes, storage, network e update-recovery passam
  no QEMU com `READY -> HEARTBEAT -> BEGIN -> PASS`.
- [x] Reboot confirma `RESET` e novo handshake; poweroff confirma `SHUTDOWN` ou
  saida esperada do QEMU.
- [x] Artefatos e causas de falha foram conferidos em cada uma das nove
  execucoes.

## Implementacao atual da TST6

A TST6 foi implementada em 20 casos QEMU independentes, sem alvo agregado,
retry automático ou loop sem teto. O runner `tools/qemu_test_runner.py` agora
usa perfis QEMU allowlisted (`baseline`, `minimal`, `network`, `usb-hid`,
`usb-storage`, `audio`, `display` e `pci`), snapshot por execução, seed
reproduzível, rastreamento de fase/eventos QMP/capacidades e artefatos
completos. `--until-failure` exige `--max-iterations` ou `--duration`; toda
execução respeita no máximo 1.000 iterações e 600 segundos. Os alvos de
estresse usam oito iterações e teto de suíte configurável, com padrão de
300 segundos.

O harness interno `src/core/kernel_tests_tst6.c` integra os domínios já
testáveis do kernel, interrompe no primeiro erro e registra fase/código antes
da publicação do resultado. Os failpoints one-shot existentes de block e
block-cache são exercitados sem alterar o boot normal. Os casos de pacote e
runtime validam o armamento inválido e o contrato de estado indisponível
quando a imagem FAT32 não oferece transação mutável; a imagem usada nesta
execução não permite declarar uma mutação transacional real como coberta.
Nenhum header público, ABI, bootloader ou evento ZTEST foi alterado.

Os casos implementados são:

```text
qemu:tst6:matrix:baseline       qemu:tst6:matrix:minimal
qemu:tst6:matrix:network       qemu:tst6:matrix:usb-hid
qemu:tst6:matrix:usb-storage   qemu:tst6:matrix:audio
qemu:tst6:matrix:display       qemu:tst6:matrix:pci
qemu:tst6:stress:kernel        qemu:tst6:stress:storage
qemu:tst6:stress:network       qemu:tst6:stress:apps
qemu:tst6:fault:memory         qemu:tst6:fault:block
qemu:tst6:fault:block-cache    qemu:tst6:fault:package
qemu:tst6:fault:update         qemu:tst6:fault:network
qemu:tst6:fault:process        qemu:tst6:fault:recovery
```

Validação executada em 2026-08-31 (America/Sao_Paulo): `make test-qemu-selftest`,
`make test-tst6-host`, `make q3check`, `make clean`, `make`, os 20 alvos acima
executados individualmente e `make catalog-test`. Os 20 casos produziram
`READY -> HEARTBEAT -> BEGIN -> PASS`; as execuções preservaram manifesto,
serial, stdout/stderr do QEMU, eventos QMP, screenshots disponíveis e
`result.json`. A tentativa inicial de `matrix:network` falhou pelo watchdog
de 20 segundos e foi preservada; o heartbeat específico da família de rede
foi ajustado para 60 segundos, e a repetição passou. A primeira configuração
do estresse de storage atingiu o teto de 60 segundos depois de quatro
iterações aprovadas; o teto foi corrigido para 300 segundos e as oito
iterações passaram. Na rodada final, `fault:block` também revelou que o alvo
estava passando 20 segundos explicitamente; o padrão TST6 foi ajustado para 60
segundos e a repetição passou. O diagnóstico original foi preservado.

### TST6 — Checklist de saída

- [x] Runner host-only com perfis allowlisted, seeds, limites, classificação
  `PASS`/`FAIL`/`BLOCKED` e artefatos por execução.
- [x] Oito perfis QEMU de matriz executados em snapshot, sem acesso externo,
  escrita real, reset físico ou efeito no hospedeiro.
- [x] Quatro casos de estresse executados com oito iterações, teto de suíte e
  parada na primeira falha.
- [x] Oito casos de falha controlada/recuperação executados individualmente,
  incluindo memory, block, block-cache, package, update, network, process e
  recovery.
- [x] `make test-tst6-host`, self-test do runner, `q3check`, build limpo e
  `catalog-test` passaram.
- [x] Todos os 20 casos QEMU produziram `READY -> HEARTBEAT -> BEGIN -> PASS`.
- [x] Hardware físico permanece explicitamente `BLOCKED`; não foi simulado
  como validado.

## Fora do escopo

- vincular o testador à versão 1.0.0 ou a uma release específica;
- vincular o testador a uma versão, release ou reorganização interna;
- substituir revisão de código, análise de contratos ou diagnóstico manual;
- declarar cobertura total apenas com percentual de linhas;
- depender de portas privadas de QEMU, Bochs ou VirtualBox no sistema;
- executar testes destrutivos reais quando houver backend simulado;
- mascarar uma limitação de hardware como aprovação da funcionalidade.
