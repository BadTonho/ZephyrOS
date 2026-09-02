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
TST6 foram implementadas e validadas. A TST7 foi implementada, teve o baseline
aprovado explicitamente e passou por uma execução `full` posterior contra
esse baseline. Hardware físico permanece `BLOCKED` enquanto não houver
equipamento e evidência correspondente.

A infraestrutura TST1–TST7 está concluída para a matriz automatizada existente,
mas o programa de cobertura integral ainda não está concluído. O catálogo
mantém 36 casos `AUTOMATED` e superfícies de API e comportamento em
`PENDING`. O próximo objetivo deste roadmap é eliminar esse `PENDING` de
todas as superfícies de software testáveis, vinculando cada uma a um caso
executável e a evidência reproduzível. Isso não significa declarar hardware
físico validado sem equipamento.

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

Revalidação final executada em 2026-08-31 23:53 (America/Sao_Paulo), após
`make q3check`, `make clean`, `make` e `make test-qemu-selftest`. Os 20 alvos
TST6 foram executados individualmente, sem retry: 8 casos de matriz, 4 casos
de stress com 8 iterações cada e 8 casos de falha/recuperação. Todos
produziram `READY -> HEARTBEAT -> BEGIN -> PASS`, com `status=PASS` e
`last_state=PASS`; os artefatos obrigatórios foram preservados e não restou
processo QEMU. A falha intermitente observada no `full` anterior não se
reproduziu nesta revalidação. A TST6 atende agora aos critérios de aceite;
hardware físico continua `BLOCKED` por não fazer parte desta validação.

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

## Implementacao atual da TST7

A infraestrutura da regressao continua foi implementada em host, sem criar
casos QEMU novos. O runner `tools/tst7_regression_runner.py` possui os modos
`quick`, `full` e `approve --run-id`, executa cada comando e caso QEMU em
processo separado, usa seed deterministico, timeout por processo e teto de
suite, e nunca repete automaticamente uma execucao.

Os resultados persistentes ficam em `.tst7-results/<run-id>/`, fora de `build/`,
para sobreviver a `make clean`. Cada execucao registra manifesto, resultado,
cobertura, resumo, stdout/stderr e indice SHA-256 dos artefatos. Os artefatos
dos casos QEMU sao escritos dentro da execucao TST7 antes de qualquer limpeza.
O baseline versionado em `tests/baselines/tst7-approved.json` foi criado por
aprovacao explicita do `full` `tst7-20260901T124115Z-19420`. A execucao final
`tst7-20260901T135144Z-12828` passou contra esse baseline com as suites host,
build limpo, catalogo, fixtures e os 36 casos QEMU aprovados.

O comparador cobre mudancas de status, timeout, fase, evento, warnings
normalizados, cobertura aprovada e duracao. A duracao so e comparada quando o
ambiente e identico e so reprova quando o aumento e simultaneamente maior que
20% e 5 segundos; etapas de preparacao do host, como `build`, nao sao casos e
nao entram nessa comparacao. Superficies `PENDING`, hardware fisico e
limitacoes de fixture permanecem explicitos; nao sao convertidos em cobertura
artificial.

### TST7 — Checklist de saida

- [x] Runner host-only independente de provedor de CI com modos `quick`,
  `full` e aprovacao atomica de baseline.
- [x] Execucao sem retry automatico, com timeout por subprocesso e teto de
  suite; a matriz continua apos falhas para coletar diagnosticos.
- [x] Comparador de contratos, status, timeout, warnings, duracao e cobertura,
  incluindo mutacoes sinteticas nos testes unitarios.
- [x] Manifesto versionado de regressao permanente com caso, condicao
  observavel e origem validados contra o catalogo.
- [x] `make test-tst7-host`, `make test-tst7-quick`, `make test-tst7-full` e
  uma aprovacao real de baseline executados com evidencia.
- [x] Uma segunda execucao `full` passou contra o baseline aprovado sem
  regressao.

Com esses criterios atendidos, a TST7 esta concluida para a matriz automatizada.
Hardware fisico continua `BLOCKED` e nao entra na matriz QEMU.

## Plano de fechamento da cobertura integral

### Meta: 100% das superfícies de software testáveis

“100% testável” significa que nenhuma superfície de software permanece sem
um caminho de validação definido. Cada entrada do catálogo deverá estar
vinculada a pelo menos um caso executável, com resultado, timeout, artefato e
condição de limpeza verificáveis. Um caso pode cobrir várias funções quando o
fluxo realmente as exercitar, mas o vínculo com cada superfície deverá ser
explícito no catálogo.

A meta não será medida apenas por cobertura de linhas. Para cada superfície
aplicável, o catálogo deverá identificar:

- caminho de sucesso;
- entradas inválidas e limites relevantes;
- códigos de erro e estados indisponíveis;
- ownership, limpeza e ausência de recursos residuais;
- concorrência, cancelamento ou timeout quando fizerem parte do contrato;
- observação do resultado pelo host, pelo harness interno ou pelo fluxo
  black-box.

Uma superfície só poderá sair de `PENDING` quando houver execução real e
evidência preservada. `BLOCKED` será reservado para dependência externa
comprovadamente ausente, como hardware físico; não será usado para esconder
um teste ainda não implementado.

### Etapas de implementação

- [ ] Auditar as superfícies `PENDING` por subsistema, removendo referências
  obsoletas e agrupando APIs que possam compartilhar um fixture determinístico.
- [ ] Criar uma matriz de cobertura que associe cada superfície a um executor:
  host-only, harness interno do kernel, QEMU black-box ou perfil de hardware.
- [ ] Completar os testes host-only para lógica pura, estruturas de dados,
  parsing, serialização, limites, overflow, corrupção e códigos de erro.
- [ ] Expandir os harnesses internos do kernel para memória, paging, processos,
  threads, scheduler, IPC, wait queues, workqueues, VFS, storage, rede e
  plataforma, incluindo limpeza e invariantes de cada fixture.
- [ ] Expandir os casos black-box para Shell, entrada, aplicações, processos,
  VFS, atualização, recuperação, reboot e poweroff, mantendo scripts de
  entrada allowlisted e observação estruturada.
- [ ] Criar perfis QEMU e fixtures negativos para capacidades ausentes,
  dispositivos degradados, falhas controladas, cancelamento e recuperação.
- [ ] Adicionar ao catálogo somente APIs realmente exercitadas e regenerar a
  visão Markdown após cada lote de cobertura.
- [ ] Integrar todos os casos automatizados ao `full` da TST7, mantendo o
  `quick` curto e sem retries ocultos.
- [ ] Executar o supervisor contínuo somente depois que a matriz de cobertura
  atingir o critério de saída definido abaixo.

### Ordem recomendada por domínio

```text
catálogo e contratos
    -> core e lógica host-only
    -> memória e kernel determinístico
    -> processos, threads e IPC
    -> storage, VFS e atualizações
    -> rede e protocolos offline
    -> drivers e plataforma em perfis QEMU
    -> Shell, aplicações e fluxos black-box
    -> integração completa no TST7
```

Cada lote deve preservar a separação entre teste de unidade, integração,
black-box e hardware. O mesmo caso não deve ser contado como cobertura de uma
API que ele não chama apenas porque pertence ao mesmo módulo.

### Critérios de saída da cobertura integral

- [ ] Nenhuma superfície de software elegível permanece `PENDING` no catálogo.
- [ ] Cada superfície está vinculada a um caso `AUTOMATED` ou a um caso
  `BLOCKED` com capacidade ausente, evidência e próximo critério reproduzível.
- [ ] Cada subsistema possui testes de sucesso, erro, limite e limpeza quando
  esses comportamentos fazem parte do contrato.
- [ ] O `full` da TST7 executa todos os casos automatizados em processos
  separados, com timeout, seed e artefatos próprios.
- [ ] O supervisor contínuo consegue repetir ciclos sem apagar falhas,
  atualizar baseline automaticamente ou permitir processos QEMU residuais.
- [ ] A execução final contra baseline aprovado passa sem `FAIL`, `TIMEOUT`,
  warning novo ou perda de cobertura.
- [ ] Hardware físico continua explicitamente separado como `BLOCKED` até
  existir equipamento e evidência real.

O documento operacional do supervisor está em
`docs/qualidade/testador-continuo.md`. Ele só deve ser usado em execução
permanente depois que os critérios acima forem comprovados; antes disso, ele
serve para encontrar as lacunas restantes sem mascará-las.

## Estado da implementação do fechamento

Em 2026-09-01, foi implementada a primeira parte executável do fechamento: o
registro declarativo
`tests/coverage/registry.json`, sincronização bidirecional do catálogo, o gate
`make catalog-test-strict`, os casos host-only `host:tst2:protocol-core` e
`host:tst3:string-compress`, e a integração desses casos ao `full` da TST7.
O renderer do catálogo também passou a aceitar casos host sem `guest_case`.

As evidências desta rodada foram:

- `make test-tst2-host`: PASS;
- `make test-tst3-host`: PASS;
- `make test-qemu-selftest`: PASS, self-test e 12 testes unitários;
- `make q3check`: PASS;
- `make catalog-test`: PASS, 6.808 superfícies e 38 casos;
- `make test-tst7-host`: PASS, 19 testes;
- `make test-tst7-continuous-host`: PASS, 7 testes.
- `make clean && make`: PASS, imagem `build/zephyros.img` gerada em
  2026-09-01 13:04 (America/Sao_Paulo); o build ainda reporta warnings
  preexistentes do código freestanding, sem erro de compilação ou linkedição.

O catálogo passou de 17 para 2.006 superfícies `COVERED` porque somente
superfícies vinculadas a testes host reais ou a relatórios QEMU `PASS` foram
promovidas. A bateria host agora inclui rede IPv4, rotas, criptografia,
`wait`, `workqueue` e `irq_deferred`; os relatórios dessas suítes não possuem
endereços desconhecidos ou ambíguos. Restam 4.814 superfícies `PENDING`; por
isso `make catalog-test-strict` falha corretamente e a TST7 ainda não está
concluída para a meta integral. O supervisor contínuo já possui entrada Linux,
modos `quick`, `full` e `soak`, limites e parada graciosa, mas seu uso
permanente deve aguardar o gate estrito.

Na mesma rodada, o caminho host-only `ZEPHYROS_HOST_TEST` foi adicionado aos
helpers de interrupção de `wait.c`, `workqueue.c` e `irq_deferred.c`. Ele
simula o estado de interrupções sem executar `cli/sti` em processo de usuário;
o caminho freestanding não foi alterado. `make test-scheduling-host` passou
com instrumentação e a nova entrada `scheduling-host-dynamic` foi sincronizada
no catálogo somente a partir desse relatório real.

O incremento seguinte adicionou `host:core:app-package` e o alvo
`make test-package-host`. O teste executa, com `-Werror` e instrumentação,
comparação de versões, estados sem serviço, histórico indisponível, failpoints
e nomes canônicos de ações. Também foi corrigido o acesso a membros `packed`
no caminho de histórico e journal sem alterar o layout persistido. O relatório
`build/test-results/package-host/coverage.json` passou sem endereços
desconhecidos ou ambíguos. O estado atual é 6.820 superfícies, 2.013
`COVERED`, 4.807 `PENDING` e 45 casos; o gate estrito continua reprovando
corretamente pelas superfícies ainda sem evidência.

O lote Core/estado adicionou o caso host-only `host:core:state` e o alvo
`make test-state-host`. Recovery e a cadeia de notificadores de energia sao
exercitados com erros, limites, estados opcionais e timeout em processo
instrumentado; o relatorio `build/test-results/state-host/coverage.json` foi
aprovado sem enderecos desconhecidos ou ambiguos. A sincronizacao real
resultou em 2.046 superficies `COVERED`, 4.774 `PENDING` e 46 casos. O gate
estrito continua pendente, pois os lotes de memoria/paging, processos,
storage/VFS, rede restante, drivers/plataforma e Shell/UI ainda precisam de
executores e evidencia especificos.

O lote Storage/VFS adicionou os casos host-only `host:storage:vfs-path`,
`host:storage:file-index` e `host:storage:fs`, com os alvos
`make test-vfs-path-host`, `make test-file-index-host` e `make test-fs-host`.
As suites usam fixtures estaticas e instrumentacao dinamica para exercitar
normalizacao de caminhos, mounts, cwd, cursores FAT12/FAT32, pesquisa, rebuild,
cancelamento, streaming, operacoes atomicas, estados stale/missing, corrupcao
e recuperacao. Os relatorios dos tres casos terminaram `PASS` sem enderecos
desconhecidos ou ambiguos. As superficies de `src/fs/vfs_path.c`,
`src/fs/file_index.c` e `src/fs/fs.c` agora estao cobertas; a sincronizacao
deixou 6.820 superficies, 2.395 `COVERED`, 4.425 `PENDING` e 55 casos.
`make catalog-test` permanece valido, enquanto `catalog-test-strict` continua
reprovando pelas superficies de outros lotes ainda sem executor real.

O lote Storage/VFS adicionou os casos host-only `host:storage:vfs-path` e
`host:storage:file-index`, com os alvos `make test-vfs-path-host` e
`make test-file-index-host`. Os dois testes usam fixtures estaticas e
instrumentacao dinamica; cobrem normalizacao de caminhos, mounts, cwd,
quiescencia, pesquisa, rebuild cooperativo, cancelamento, estados stale e
missing, corrupcao de tabelas e recuperacao. Ambos passaram e seus relatorios
foram sincronizados somente a partir dos enderecos observados. `src/fs/vfs_path.c`
e `src/fs/file_index.c` nao possuem mais superficies `PENDING`; o catalogo
agora registra 6.820 superficies, 2.334 `COVERED`, 4.486 `PENDING` e 54 casos.
`make catalog-test` permanece valido, enquanto `catalog-test-strict` continua
reprovando pelas superficies de outros lotes ainda sem executor real.

## Fora do escopo

- vincular o testador à versão 1.0.0 ou a uma release específica;
- vincular o testador a uma versão, release ou reorganização interna;
- substituir revisão de código, análise de contratos ou diagnóstico manual;
- declarar cobertura total apenas com percentual de linhas;
- depender de portas privadas de QEMU, Bochs ou VirtualBox no sistema;
- executar testes destrutivos reais quando houver backend simulado;
- mascarar uma limitação de hardware como aprovação da funcionalidade.
`host:core:app-api` e `host:core:app-catalog`, com os alvos
`make test-device-manager-host`, `make test-app-api-host` e
`make test-app-catalog-host`. As tres suites usam `HOST_CC`, `-Werror`,
`-finstrument-functions` e backends estaticos; os relatorios terminaram
`PASS` sem enderecos desconhecidos ou ambiguos. O catalogo foi sincronizado
somente com os enderecos observados e passou a registrar 6.820 superficies,
2.199 `COVERED`, 4.621 `PENDING` e 49 casos. O validador tambem passou a
combinar relatorios dinamicos distintos do mesmo caso sem transformar a
associacao em cobertura por arquivo. `make catalog-test` permanece valido;
`make catalog-test-strict` continua corretamente pendente pelas superficies
que ainda nao possuem executor e evidencia real.
O lote Core/entrada adicionou `host:core:input` e o alvo
`make test-input-host`, com instrumentacao dinamica e sinks estaticos. O
teste passou por filas cheias, coalescencia, limites, erro de consumidor e
validacao do estado publicado. A sincronizacao ficou em 6.820 superficies,
2.208 `COVERED`, 4.612 `PENDING` e 50 casos. O gate estrito continua
reprovando somente pelas superficies ainda sem executor e evidencia real;
nenhuma pendencia foi promovida por associacao generica.
O lote Core/energia adicionou `host:core:power` e o alvo `make test-power-host`.
O caso usa ACPI, Storage, VFS, processos, workqueue e rede como dependencias
estaticas, exercita estados disponiveis e indisponiveis, quiescencia, falhas de
sync/S5 e limpeza apos falha; reset, triple fault e halt terminal continuam
reservados a cenarios QEMU controlados. O lote Rede adicionou
`host:core:network-manager` e `make test-network-manager-host`, cobrindo
inventario PCI, driver ausente, estado offline, recusas de operacoes sem
interface ativa, formatacao e nomes canonicos. Ambos passaram com cobertura
dinamica e foram sincronizados somente a partir dos enderecos observados. O
catalogo agora registra 6.820 superficies, 2.312 `COVERED`, 4.508 `PENDING` e
52 casos; `make catalog-test` permanece valido e `catalog-test-strict` continua
reprovando corretamente pelas superficies ainda sem executor real.

O lote Storage/backend adicionou `host:storage:storage` e o alvo
`make test-storage-host`. O caso usa uma imagem FAT12 e um provider de bloco
falsos, cobrindo MBR/BPB, inventario, mount, aliases, cursores, leitura,
espaco livre, estados somente-leitura e limpeza. O lote Storage/BIO adicionou
`host:storage:block` e `make test-block-host`, executando os autotestes reais
de BIO e block-cache, com limites, cancelamento, failpoints, fusao/FIFO,
eviction, writeback, sync e restauracao do inventario. Ambos passaram com
instrumentacao dinamica e sem enderecos desconhecidos ou ambiguos. A rodada
atual registra 6.820 superficies, 2.479 `COVERED`, 4.341 `PENDING` e 57 casos;
`make catalog-test` passou e o gate estrito continua pendente pelas superficies
de outros lotes ainda sem executor real.

O lote Storage/FAT12 adicionou `host:storage:fat12` e `make test-fat12-host`.
O caso usa uma imagem FAT12 estatica com raiz e subdiretorio para exercitar
leitura, paths, metadados, listagem, operacoes atomicas, streaming,
cancelamento e erros canonicos. O relatorio instrumentado passou sem
enderecos desconhecidos ou ambiguos; o helper privado `strncmp` foi renomeado
para `fat12_strncmp` para permitir `-Werror` no host, sem alteracao da API
publica. A rodada atual registra 6.820 superficies, 2.543 `COVERED`, 4.277
`PENDING` e 58 casos. `make catalog-test` passou e o gate estrito continua
pendente pelas superficies de outros lotes ainda sem executor real.

O lote Storage/FAT32 adicionou `host:storage:fat32` e `make test-fat32-host`.
O caso usa imagem FAT32 estatica com cadeia de clusters suficiente para
exercitar classificacao, leitura, paths, metadados, criacao, escrita, remocao
e limites. O relatorio instrumentado passou sem enderecos desconhecidos ou
ambiguos; o helper privado `strncmp` foi renomeado para `fat32_strncmp` para
permitir `-Werror` no host, sem alteracao da API publica.

O lote Storage/VFS adicionou `host:storage:vfs` e `make test-vfs-host`. A
fixture host-only exercita descritores, arquivos regulares, dispositivos,
pipes, sockets, poll/select, fsync/sync, quiescencia, limites e invariantes,
sem hardware ou armazenamento real. O relatorio instrumentado passou sem
enderecos desconhecidos ou ambiguos. A rodada atual registra 6.820
superficies, 2.608 `COVERED`, 4.212 `PENDING` e 60 casos; `make catalog-test`
passou e `catalog-test-strict` continua pendente pelos lotes ainda sem
executor real.

O lote Core/scheduling foi reforçado no caso existente
`host:core:scheduling`, sem criar associação por arquivo. A fixture passou a
validar o notifier de IRQ deferred, cancelamento e snapshot de trabalhos,
quiescência com deadline do relógio falso e restauração da fila. O teste
também eliminou uma dependência de stack não inicializada no fixture do
protocolo Core. `make test-scheduling-host` passou com warnings tratados como
erro, e o relatório instrumentado não apresentou endereços desconhecidos ou
ambíguos. A sincronização atual registra 6.820 superfícies, 2.632
`COVERED`, 4.188 `PENDING` e 60 casos; `make catalog-test` passou.

O lote Memoria/SLAB adicionou `host:memory:slab-metadata` e o alvo
`make test-slab-host`. A fixture host-only valida inicializacao idempotente,
limites de criacao, duplicidade, metadados por indice, estatisticas,
ownership nulo, validacao e destruicao sem alocar paginas reais. A aritmetica
interna de enderecos do SLAB passou a usar `uint64_t`, evitando truncamento de
ponteiros em hosts 64-bit sem alterar o layout freestanding. O relatorio
instrumentado terminou `PASS`, sem enderecos desconhecidos ou ambiguos. A
sincronizacao atual registra 6.820 superficies, 2.638 `COVERED`, 4.182
`PENDING` e 61 casos; `make catalog-test` passou. A cobertura real de alocacao
de paginas e dos testes negativos de objetos permanece no caso QEMU TST4.

O caso `host:core:scheduling` tambem passou a exercitar `wait_event`,
snapshots de filas e waiters e a prova controlada da kworker, alcançando os
callbacks internos de condicao e execucao sem iniciar o worker infinito. A
sincronizacao atual registra 6.820 superficies, 2.654 `COVERED`, 4.166
`PENDING` e 61 casos; `make catalog-test` e `make q3check` passaram.
`workqueue_worker_main` permanece pendente por ser um loop de servico que
deve ser validado em um cenario QEMU com encerramento controlado.

O lote Core/timer adicionou `host:core:timer` e o alvo `make test-timer-host`.
A fixture usa stubs de IDT, PIC e scheduler para exercitar inicializacao
idempotente, conversao de milissegundos, handles, timers one-shot e
periodicos, notifier, dispatch, cancelamento, callbacks com erro, snapshots,
limites e destruicao de proprietarios. O caminho host nao executa instrucoes
privilegiadas; o caminho freestanding permanece inalterado. O relatorio
instrumentado terminou `PASS`, sem enderecos desconhecidos ou ambiguos. A
sincronizacao atual registra 6.820 superficies, 2.667 `COVERED`, 4.153
`PENDING` e 62 casos; `make catalog-test` passou. Nenhuma superficie de timer
permanece pendente.

O lote Rede/UDP adicionou `host:network:udp` e o alvo `make test-udp-host`.
A fixture usa um transporte IPv4 falso para exercitar envio, reinjecao,
checksum, listeners, broadcast, callback recusado, comprimentos invalidos,
payload fora do limite e limpeza de endpoints, sem conexao externa. O
relatorio instrumentado terminou `PASS`, sem enderecos desconhecidos ou
ambiguos. A sincronizacao atual registra 6.820 superficies, 2.682 `COVERED`,
4.138 `PENDING` e 63 casos; `make catalog-test` passou. Nenhuma superficie de
`udp.c` permanece pendente.

O lote Rede/ARP adicionou `host:network:arp` e o alvo `make test-arp-host`.
A fixture usa Ethernet falsa e relogio deterministico para validar enderecos
IPv4 e MAC, configuracao, cache resolvido e incompleto, retries, timeout,
requests, replies, pacotes invalidos e limpeza. `make test-arp-host` passou
com `HOST_CC` configurado e warnings tratados como erro; o relatorio
`build/test-results/arp-host/coverage.json` terminou `PASS`, sem enderecos
desconhecidos ou ambiguos. A sincronizacao atual registra 6.820 superficies,
2.724 `COVERED`, 4.096 `PENDING` e 64 casos; `make catalog-test` passou.
Nenhuma superficie de `arp.c` permanece pendente.

Incremento Rede/ICMP concluido em 2026-09-02: o caso host-only
`host:network:icmp` e o alvo `make test-icmp-host` foram adicionados. A
fixture passou com IPv4 e timer falsos, cobrindo configuracao, checksum, echo
request/reply, RTT, timeout, mudanca de configuracao, fila pendente, pacotes
invalidos e falhas de transporte. A cobertura real foi sincronizada no
catalogo: 6.820 superficies, 2.748 `COVERED`, 4.072 `PENDING` e 65 casos.
`make catalog-test` e `make q3check` passaram. O fechamento integral do
catalogo, o gate estrito e a validacao TST7 completa continuam pendentes.

Incremento Rede/DNS concluido em 2026-09-02: o caso host-only
`host:network:dns` e o alvo `make test-dns-host` foram adicionados. A fixture
passou com UDP, IPv4 e timer falsos, cobrindo consultas, normalizacao de
nomes, cache e expiracao, CNAME, timeout, respostas invalidas e falhas de
transporte sem conexao externa. A cobertura real foi sincronizada no
catalogo: 6.820 superficies, 2.785 `COVERED`, 4.035 `PENDING` e 66 casos.
O gate `make catalog-test` permanece valido; a cobertura integral, o gate
estrito e a validacao TST7 completa continuam pendentes.

Incremento Rede/DHCP concluido em 2026-09-02: o caso host-only
`host:network:dhcp` e o alvo `make test-dhcp-host` foram adicionados. A
fixture passou com UDP e timer falsos, cobrindo descoberta, oferta, lease,
renovacao, rebinding, expiracao, NAK, timeout, mensagens invalidas e falhas de
transporte sem conexao externa. A cobertura real foi sincronizada no
catalogo: 6.820 superficies, 2.841 `COVERED`, 3.979 `PENDING` e 67 casos. O
fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.

Incremento Seguranca/TLS concluido em 2026-09-02: o caso host-only
`host:security:tls` e o alvo `make test-tls-host` foram adicionados. A fixture
passou com relogio, RNG e cliente TLS falsos, cobrindo politica, validade,
cadeia, SAN, pinning, rotacao, revogacao, estados indisponiveis e autoteste
sem rede externa. A cobertura real foi sincronizada no catalogo: 6.820
superficies, 2.931 `COVERED`, 3.889 `PENDING` e 70 casos. O fechamento
integral do catalogo, o gate estrito e a validacao TST7 completa continuam
pendentes.

Incremento Rede/HTTP concluido em 2026-09-02: o caso host-only
`host:network:http` e o alvo `make test-http-host` foram adicionados. A
fixture usa DNS, socket, TLS, timer e stack falsos, cobrindo URLs, opcoes,
headers, corpos Content-Length/chunked/EOF, streaming, redirects, HTTPS,
limites, timeouts e falhas sem rede externa. A cobertura real foi sincronizada
no catalogo: 6.820 superficies, 2.979 `COVERED`, 3.841 `PENDING` e 71 casos.
O fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.

Incremento Rede/Ethernet concluido em 2026-09-02: o caso host-only
`host:network:ethernet` e o alvo `make test-ethernet-host` foram adicionados.
A fixture passou com quatro interfaces, drivers, handlers e frames falsos,
cobrindo polling, entrega local e broadcast, filtragem, frames invalidos,
erros de driver, transmissao, quiescencia, sk_buff, net_buffer e limpeza sem
hardware real. A cobertura real foi sincronizada no catalogo: 6.820
superficies, 2.876 `COVERED`, 3.944 `PENDING` e 68 casos. O fechamento
integral do catalogo, o gate estrito e a validacao TST7 completa continuam
pendentes.

Incremento Rede/TCP concluido em 2026-09-02: o caso host-only
`host:network:tcp` e o alvo `make test-tcp-host` foram adicionados. A fixture
passou com IPv4 e timer falsos, cobrindo handshake, dados, ACK, FIN, RST,
retransmissao, timeout, callbacks recusados, janelas, limites, conexoes
simultaneas e limpeza sem rede externa. A cobertura real foi sincronizada no
catalogo: 6.820 superficies, 2.920 `COVERED`, 3.900 `PENDING` e 69 casos. O
fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.
