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
mantém 156 casos `AUTOMATED`; após os incrementos de Shell, RTC,
processos/threads, FAT32, update U3/U4, os contratos remotos ZSYS e o
repositório remoto de aplicativos, dos helpers de pacotes do Shell, da
interface App Store e dos relatórios de rede do Shell, registra 7.307
superfícies, 6.414 `COVERED` e 893
`PENDING`. O próximo objetivo deste
roadmap é eliminar esse `PENDING` de todas as superfícies de software
testáveis, vinculando cada uma a um caso executável e a evidência reproduzível.
Isso não significa declarar hardware físico validado sem equipamento.

### Incremento Shell/checks e fixtures ZAPP — 2026-09-05

- [x] A fixture host-only `host:shell:checks` passou a exercitar diretamente
      os formatadores de resultado, a classificação de recursos opcionais e a
      construção das imagens ZAPP de demonstração, VMA, page fault, entrada e
      cancelamento.
- [x] Foram validados os estados `FS_TYPE_NONE`/loader indisponível, os
      formatos de imagem e seus limites estruturais, sem iniciar processos,
      acessar armazenamento ou depender de hardware real.
- [x] `make test-shell-checks-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
      passou com cobertura dinâmica real. A sincronização cobriu 11
      superfícies novas de `src/shell/shell_checks.c`; o catálogo registra
      7.307 superfícies, 6.414 `COVERED`, 893 `PENDING` e 158 casos. As
      pendências restantes continuam explícitas.

### Incremento Shell/UI App Store — 2026-09-05

- [x] Foi criado o caso host-only `host:ui:appstore`, com header interno
      compilado somente sob `ZEPHYROS_HOST_TEST`; a API pública e o build
      normal permanecem inalterados.
- [x] A fixture exercitou diretamente os contratos determinísticos da App
      Store: cópia e truncamento de texto, formatação de inteiros, dependências,
      bloqueios, seleção e restauração, planos de downgrade, estados, rollback,
      confiança e geometria dos botões, usando doubles estáticos.
- [x] A evidência dinâmica terminou `PASS`, com 32 superfícies reais de
      `src/appstore/appstore.c` e `src/core/string.c`, sem endereços
      desconhecidos ou símbolos ambíguos.
- [x] Passaram `make test-appstore-host` com `HOST_CC`, sincronização e
      renderização do catálogo e `make catalog-test`. O catálogo registra
      7.307 superfícies, 6.403 `COVERED`, 904 `PENDING` e 158 casos; as
      pendências restantes continuam explícitas após a sincronização dos
      relatórios mais recentes.

### Incremento Shell/network reports — 2026-09-05

- [x] A fixture host-only `host:shell:network-checks` foi ampliada para chamar
      os caminhos reais de relatório somente leitura do Shell: estado de link,
      interface, MAC, IPv4, último frame Ethernet, rotas, configuração e
      contadores IPv4, lease DHCP, estado ICMP, status agregado e inventário de
      dispositivos.
- [x] Os doubles permanecem estáticos e sem rede, hardware ou armazenamento
      reais. Foram exercitados caminhos nulos, estados vazios e dados válidos;
      a fixture não inicia jobs nem altera estado persistente.
- [x] `make test-shell-network-checks-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
      passou com evidência dinâmica `PASS`, sem endereços desconhecidos ou
      símbolos ambíguos. A sincronização cobriu 20 superfícies novas no
      catálogo; o total permanece em 7.307 superfícies, com 6.403 `COVERED`,
      904 `PENDING` e 158 casos. As pendências continuam explícitas.

### Incremento Shell/commands packages — 2026-09-04

- [x] Foi criado o caso host-only `host:shell:commands-packages`, com hook
      interno compilado somente sob `ZEPHYROS_HOST_TEST`.
- [x] A fixture chamou diretamente os helpers de `shell_commands_packages.c`
      para validar extensões `.ZPK`, normalização de IDs, tokens com espaços e
      truncamento, argumentos nulos e a matriz de ações de `pkg`, `store` e
      `update`, sem iniciar jobs ou acessar armazenamento real.
- [x] Passaram `make test-shell-commands-packages-host` com `HOST_CC`,
      `make q3check`, `make clean` seguido de `make`, sincronização/renderização
      do catálogo e `make catalog-test`.
- [x] A evidência dinâmica resolveu 10 superfícies reais, sem endereços
      desconhecidos ou símbolos ambíguos. O catálogo registra 7.299
      superfícies, 6.372 `COVERED` e 927 `PENDING`; as pendências restantes
      continuam explícitas.

### Incremento Core/app-remote — 2026-09-04

- [x] Fixture host-only criada para `src/core/app_remote.c`.
- [x] Catálogo ZAC1, dependências, planejamento, preflight, cache FAT12 em
      memória, aplicação, procedência, cancelamento, failpoint e recovery
      exercitados com doubles estáticos.
- [x] Caso `host:core:app-remote` integrado ao Makefile, TST7 e catálogo.
- [x] Evidência real: `make test-app-remote-host` passou, cobertura resolveu
      76 superfícies de `src/core/app_remote.c` e não registrou endereços
      desconhecidos ou símbolos ambíguos.
- [x] O cenário negativo do motor de pacotes confirmou a tradução de
      `APP_PACKAGE_ACTION_REASON_PACKAGE_INVALID` para
      `APP_REMOTE_REASON_PACKAGE_VERIFY`; nenhuma superfície do arquivo ficou
      `PENDING`.

### Incremento Core/update-runtime — 2026-09-04

- [x] Fixture host-only criada para `src/core/update_runtime.c`.
- [x] Estado, journal, manifestos ZUM2, entradas ZUPD, planejamento e
      comparação de arquivos exercitados com doubles determinísticos.
- [x] Caso `host:core:update-runtime` integrado ao Makefile, TST7 e catálogo.
- [x] Evidência real: `make test-update-runtime-host` e `make catalog-test`
      passaram; cobertura desconhecida e símbolos ambíguos permaneceram vazios.
- [x] Aplicação transacional, slots, filesystem mutável, cache seletivo,
      cancelamento, failpoints e recuperação foram exercitados em fixture
      FAT12 em memória; as 46 superfícies pendentes do arquivo foram cobertas.

### Atualizacao vigente — 2026-09-04

O sincronizador mais recente registra 7.299 superficies, 6.372 `COVERED`,
927 `PENDING` e 157 casos `AUTOMATED`. Os incrementos mais recentes
adicionaram o caso host-only `host:shell:diagnostics-helpers`, com evidencia
real para 34 helpers extraidos de `shell_commands_diagnostics.c` e duas rotinas
de string, ampliaram `host:process:runtime` com cinco helpers de stack e o
idle controlado, confirmaram as 17 funcoes do driver RTC pela fixture CMOS,
revalidaram as fixtures host-only de Shell hosted, entrada e syscall e
exercitaram o cancelamento real do transporte HTTP remoto e registraram a
evidencia declarativa das 25 constantes da ABI de syscalls e revalidaram o
caso de panic host-only apos o build limpo. O caso adicional
`host:shell:diagnostics` cobre diretamente os dispatchers de `pwd`, `cd`,
`mouse`, `log`, `timer`, `clock`, `irqstat`, `wait`, `wqinfo`, `workq`, `tls`,
`vfs`, `mount`, `devcheck`, `devices`, `device-info`, `usb`, `slabinfo` e
`slabtest` com VFS, mouse,
video, timer, RTC, IRQ, IDT, wait, workqueue, TLS, mounts, descritores, devfs,
inventario de dispositivos, USB, HID, MSC e SLAB falsos. O gate estrito e a cobertura
integral permanecem pendentes para os demais subsistemas sem evidencia
especifica.

### Incremento Shell/diagnostics commands — 2026-09-04

- [x] Fixture host-only criada para `src/shell/shell_commands_diagnostics.c`.
- [x] Os dispatchers reais de `pwd`, `cd`, `mouse`, `log`, `timer`, `clock`,
      `irqstat`, `wait`, `wqinfo`, `workq`, `tls`, `vfs`, `mount`, `devcheck`, `devices`,
      `device-info`, `usb`, `slabinfo` e `slabtest` foram exercitados
      com caminhos válidos,
      argumentos extras, limites,
      estados indisponíveis e preservação da configuração após rejeição.
- [x] Caso `host:shell:diagnostics` integrado ao runner, Makefile e catálogo
      com cobertura dinâmica, doubles estáticos e sem hardware real.
- [x] O alvo passou com `HOST_CC`, warnings tratados como erro, sincronização
      e renderização do catálogo; as superfícies cobertas foram vinculadas
      somente às chamadas observadas.
- [x] O catálogo registra 7.293 superfícies, 6.219 `COVERED`, 1.074
      `PENDING` e 154 casos. As pendências restantes continuam explícitas.

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

Incremento Core/workqueue concluido em 2026-09-02 14:04: o caso host-only
`host:core:workqueue` e o alvo `make test-workqueue-host` foram adicionados. A
fixture executa o autoteste interno e exercita callbacks, prioridades, FIFO,
atrasos, coalescencia, rerun, cancelamento, fallback, quiescencia, worker e
validacao de invariantes. O worker usa quatro iteracoes somente no build host
para impedir espera indefinida; o relatorio instrumentado
`build/test-results/workqueue-host/coverage.json` terminou `PASS`, com as 59
funcoes de `src/core/workqueue.c` resolvidas, sem enderecos desconhecidos ou
ambiguos. `make test-workqueue-host`, `make test-tst7-host`,
`make q3check`, `make clean`, `make`, `make catalog-test` e `git diff --check`
passaram. A sincronizacao atual
registra 6.827 superficies, 3.082 `COVERED`, 3.745 `PENDING` e 78 casos; o
fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.

Incremento Rede/DHCP/limites concluido em 2026-09-02 14:08: a fixture existente
`host:network:dhcp` passou a enviar uma opcao DHCP com comprimento incompatível,
exercitando o rejeito canonico de `dhcp_invalid_option_length` sem conexao
externa. O relatorio instrumentado `build/test-results/dhcp-host/coverage.json`
terminou `PASS` com as 52 funcoes de `src/core/dhcp.c` resolvidas, sem enderecos
desconhecidos ou ambiguos. `make test-dhcp-host`, `make catalog-test` e
`git diff --check` passaram. A sincronizacao atual registra 6.827 superficies,
3.083 `COVERED`, 3.744 `PENDING` e 78 casos; o fechamento integral do catalogo,
o gate estrito e a validacao TST7 completa continuam pendentes.

Incremento Core/BearSSL compat concluido em 2026-09-02 14:15: o novo caso
host-only `host:core:bearssl-compat` e o alvo `make test-bearssl-compat-host`
foram adicionados. A fixture exercita diretamente `memcpy`, `memmove`,
`memset`, `memcmp` e `strlen`, incluindo sobreposicao, comparacao ordenada,
preenchimento, buffers vazios e entrada nula. O relatorio instrumentado
`build/test-results/bearssl-compat-host/coverage.json` terminou `PASS` com as
2 superficies pendentes de `src/core/bearssl_compat.c` resolvidas, sem
enderecos desconhecidos ou ambiguos. `make test-bearssl-compat-host`,
`make catalog-test` e `git diff --check` passaram. A sincronizacao atual
registra 6.827 superficies, 3.085 `COVERED`, 3.742 `PENDING` e 79 casos; o
fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.

Incremento Shell/dispatcher concluido em 2026-09-02: o novo caso host-only
`host:shell:dispatch` e o alvo `make test-shell-dispatch-host` foram adicionados.
A fixture exercita o caminho real de `shell_dispatch_execute()` para diagnostico
de comando desconhecido, entrada com espacos e escape, limite de 31 caracteres,
encaminhamento de comando conhecido e entrada nula. O relatorio instrumentado
`build/test-results/shell-dispatch-host/coverage.json` terminou `PASS` e
resolveu a superficie estatica `shell_dispatch_print_unknown`, sem enderecos
desconhecidos ou ambiguos. `make test-shell-dispatch-host`, a regeneracao dos
relatorios host-only, `make catalog-test` e `git diff --check` passaram. A
sincronizacao atual registra 6.827 superficies, 3.086 `COVERED`, 3.741
`PENDING` e 80 casos; o fechamento integral do catalogo, o gate estrito e a
validacao TST7 completa continuam pendentes.

Incremento Core/Crypto concluido em 2026-09-02: a fixture existente de
`host:core:crypto` passou a exercitar diretamente `crypto_eddsa_trim_scalar`,
verificando copia, mascaras de bits e os limites do scalar. A funcao
`fe_cswap` foi removida e registrada como `RETIRED` porque nao havia referencias
no codigo ativo; `fe_ccopy` permanece como substituto utilizado pelo caminho
Ed25519. `make test-crypto-host`, a sincronizacao do catalogo, `make catalog-test`
e o build limpo passaram. A sincronizacao atual registra 6.826 superficies,
3.089 `COVERED`, 3.737 `PENDING`, 81 casos e uma superficie aposentada; o
fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.

Incremento Shell/introspeccao concluido em 2026-09-02: o novo caso host-only
`host:shell:introspection` e o alvo `make test-shell-introspection-host` foram
adicionados. A fixture chama o parser hexadecimal real para valores numericos,
minusculos, maiusculos e `uint32_t` maximo, alem de prefixos, digitos invalidos,
entrada nula e overflow. O relatorio instrumentado
`build/test-results/shell-introspection-host/coverage.json` terminou `PASS` e
resolveu as superficies `shell_introspection_hex_digit` e
`shell_introspection_parse_hex_u32`, sem enderecos desconhecidos ou ambiguos.
`make test-shell-introspection-host`, `make catalog-test` e a renderizacao da
visao passaram. A sincronizacao atual registra 6.827 superficies, 3.088
`COVERED`, 3.739 `PENDING` e 81 casos; o fechamento integral do catalogo, o gate
estrito e a validacao TST7 completa continuam pendentes.

Incremento Processos/IPC concluido em 2026-09-02 13:52: o caso host-only
`host:process:ipc` e o alvo `make test-process-ipc-host` foram adicionados. A
fixture usa processos, filas e wait falsos para exercitar inicializacao,
mensagens validas e invalidas, fila cheia, recebimento, espera com timeout e
sinal, foco, fallback, restauracao e limpeza. O relatorio instrumentado
`build/test-results/process-ipc-host/coverage.json` terminou `PASS`, com as 14
superficies de `src/process/ipc.c` resolvidas, sem enderecos desconhecidos ou
ambiguos. `make test-process-ipc-host`, `make test-tst7-host`,
`make catalog-test` e `git diff --check` passaram. A sincronizacao atual
registra 6.827 superficies, 3.080 `COVERED`, 3.747 `PENDING` e 77 casos; o
fechamento integral do catalogo, o gate estrito e a validacao TST7 completa
continuam pendentes.

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

Incremento Rede/sockets concluido em 2026-09-02 12:10: o caso host-only
`host:network:socket` e o alvo `make test-net-socket-host` foram adicionados.
A fixture passou com TCP, timer, filas de espera e VFS falsos, cobrindo handles
geracionais, conexao, filas RX/TX, eventos, EOF, timeout, cancelamento,
limites, reset e limpeza sem rede externa. O relatorio instrumentado terminou
`PASS` com 86 superficies resolvidas, sem enderecos desconhecidos ou
ambiguos. A sincronizacao atual registra 6.820 superficies, 3.018 `COVERED`,
3.802 `PENDING` e 72 casos; o fechamento integral do catalogo, o gate estrito
e a validacao TST7 completa continuam pendentes.

- Incremento Shell/entrada concluido em 2026-09-02 15:45
  (America/Sao_Paulo): o novo caso host-only `host:shell:input` e o alvo
  `make test-shell-input-host` foram adicionados. A fixture exercita o fluxo
  real de entrada com terminal, historico, navegacao, edicao, rolagem,
  cancelamento, bloqueio, modificadores e limite do buffer, usando somente
  video, teclado e logs falsos. O relatorio instrumentado
  `build/test-results/shell-input-host/coverage.json` terminou `PASS`, com as
  superficies de `src/shell/shell_input.c` observadas sem enderecos
  desconhecidos ou ambiguos. O sincronizador passou a resolver APIs publicas
  quando o simbolo observado possui uma unica implementacao C em outro
  subdiretorio, preservando o vinculo por evidencia e evitando associacoes por
  arquivo. A sincronizacao atual registra 6.826 superficies, 3.679
  `COVERED`, 3.147 `PENDING` e 84 casos. O fechamento integral do catalogo,
  o gate estrito e a validacao TST7 completa continuam pendentes.

Incremento Memoria/VMA concluido em 2026-09-02 12:26: o caso host-only
`host:memory:vma` e o alvo `make test-vma-host` foram adicionados. A fixture
passou com processo ring 3, paging, PMM e VFS falsos, cobrindo VMAs fixas e
anonimas, materializacao lazy, page faults validos e invalidos, `mmap`,
`munmap`, limites, estatisticas e limpeza. O relatorio instrumentado terminou
`PASS` com 34 superficies resolvidas, sem enderecos desconhecidos ou
ambiguos. `make catalog-test`, `make q3check`, `make clean` seguido de `make`
e `git diff --check` passaram. A sincronizacao atual registra 6.820
superficies, 3.021 `COVERED`, 3.799 `PENDING` e 73 casos; o fechamento
integral do catalogo, o gate estrito e a validacao TST7 completa continuam
pendentes.

Incremento Memoria/paging concluido em 2026-09-02 12:56: o caso host-only
`host:memory:paging` e o alvo `make test-paging-host` foram adicionados. A
fixture usa PMM, VESA, processo e timer falsos com buffers estaticos para
exercitar tabelas, diretorios, mapas de kernel e usuario, framebuffer, copia
entre espacos, materializacao lazy, limites, overflow, paginas ausentes,
fallback de framebuffer e limpeza completa. O relatorio instrumentado terminou
`PASS` com as superficies declaradas resolvidas, sem enderecos desconhecidos ou
ambiguos. `make test-paging-host`, `make q3check`, `make clean` seguido de
`make`, `make catalog-test`, `make test-tst7-host` e `git diff --check` passaram.
A sincronizacao atual registra 6.825 superficies, 3.039 `COVERED`, 3.786
`PENDING` e 74 casos; o fechamento integral do catalogo, o gate estrito e a
validacao TST7 completa continuam pendentes.

Incremento Memoria/PMM/heap concluido em 2026-09-02 13:17: o caso host-only
`host:memory:memory` e o alvo `make test-memory-host` foram adicionados. A
fixture usa um mapa E820 estatico e memoria de heap estatica para exercitar
inicializacao, estatisticas, zonas, alocacoes contiguas, alinhamento, limites,
ponteiros invalidos, double free, coalescencia, reutilizacao e restauracao do
estado. O relatorio instrumentado terminou `PASS`, com as superficies de
`src/memory/memory.c` resolvidas e sem enderecos desconhecidos ou ambiguos.
`make test-memory-host`, `make q3check`, `make clean` seguido de `make`,
`make catalog-test`, `make test-tst7-host` e `git diff --check` passaram. O
build completo manteve somente warnings preexistentes em outros modulos. A
sincronizacao atual registra 6.827 superficies, 3.050 `COVERED`, 3.777
`PENDING` e 75 casos; o fechamento integral do catalogo, o gate estrito e a
validacao TST7 completa continuam pendentes.

Incremento Processos/sinais concluido em 2026-09-02 13:42: o caso host-only
`host:process:signals` e o alvo `make test-process-signal-host` foram
adicionados. A fixture usa processos estaticos e exercita inicializacao,
nomes, acoes, mascaras, coalescencia, entrega a handler, `sigreturn`,
terminacao padrao, notificacao `SIGCHLD`, snapshots, estatisticas e invariantes
finais. O caminho de IRQ foi substituido somente no build host por um stub
controlado. O relatorio instrumentado
`build/test-results/process-signal-host/coverage.json` terminou `PASS`, com as
28 superficies de `src/process/signal.c` resolvidas, sem enderecos
desconhecidos ou ambiguos. `make test-process-signal-host`, `make q3check`,
`make clean` seguido de `make`, `make catalog-test`, `make test-tst7-host` e
`git diff --check` passaram. O build completo manteve somente warnings
preexistentes em outros modulos. A sincronizacao atual registra 6.827
superficies, 3.074 `COVERED`, 3.753 `PENDING` e 76 casos; o fechamento
integral do catalogo, o gate estrito e a validacao TST7 completa continuam
pendentes.

Incremento Drivers/Fonte concluido em 2026-09-02: o caso host-only
`host:drivers:font` e o alvo `make test-font-host` foram adicionados. A fixture
exercita diretamente `font_init`, `font_get_width` e `font_get_height`,
verificando inicializacao idempotente e dimensoes 8x16 com cobertura
instrumentada. A validacao host encontrou e corrigiu uma comparacao de `char`
que gerava `-Werror=type-limits`, sem alterar o comportamento para valores
validos. A sincronizacao atual registra 6.826 superficies, 3.095 `COVERED`,
3.731 `PENDING`, 82 casos e 21 superficies aposentadas; o fechamento integral
do catalogo, o gate estrito e a validacao TST7 completa continuam pendentes.

Incremento Drivers/RTC concluido em 2026-09-02 15:30: o novo caso host-only
`host:drivers:rtc-status` e o alvo `make test-rtc-status-host` foram
adicionados. A fixture exercita diretamente `rtc_get_status`, cobrindo destino
nulo, estado inicial zerado e leitura repetida sem mutacao, com cobertura
instrumentada. As rotinas CMOS privilegiadas permanecem fora deste caso e
continuam pendentes para uma fixture QEMU segura. A sincronizacao atual
registra 6.826 superficies, 3.097 `COVERED`, 3.729 `PENDING`, 83 casos e
nenhuma superficie `RETIRED`; o fechamento integral do catalogo, o gate estrito
e a validacao TST7 completa continuam pendentes.

- Incremento Shell/utilitarios concluido em 2026-09-02 16:00
  (America/Sao_Paulo): o novo caso host-only `host:shell:command-utils` e o
  alvo `make test-shell-command-utils-host` foram adicionados. A fixture
  passou com parsing de tokens e argumentos, comparacao de subcomandos,
  normalizacao, conversao numerica, limites, entradas invalidas e formatacao
  decimal/hexadecimal, usando somente buffers e logs falsos. O relatorio
  instrumentado `build/test-results/shell-command-utils-host/coverage.json`
  terminou `PASS` e resolveu diretamente as funcoes e APIs do utilitario,
  incluindo `shell_command_match_subcommand`. O descobridor do catalogo passou
  a registrar declaracoes e definicoes com retorno por ponteiro; o teste
  unitario confirmou esse comportamento. A sincronizacao atual registra 7.197
  superficies, 3.793 `COVERED`, 3.404 `PENDING` e 85 casos. O fechamento
  integral do catalogo, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Core/nomes e erros concluido em: 2026-09-02 16:09
  (America/Sao_Paulo): o caso existente `host:core:contracts` passou a validar
  explicitamente `clock_source_name` para fontes RTC, ausente e desconhecida.
  A fixture tambem confirmou `log_level_str` para niveis validos e invalidos.
  No mesmo fluxo, uma execucao `RUN` rejeitada confirmou `core_error_name`
  atraves do evento `FAIL` com `ERR_INVALID`.
  O relatorio instrumentado `build/test-results/core-host/coverage.json`
  terminou `PASS`, sem enderecos desconhecidos ou ambiguos, e a sincronizacao
  vinculou as tres superficies por chamada real. O catalogo agora registra
  7.197 superficies, 3.884 `COVERED`, 3.313 `PENDING` e 85 casos. O fechamento
  integral do catalogo, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento de acessores Core, memoria, paging, rede e fonte concluido em:
  2026-09-02 16:24 (America/Sao_Paulo): os fixtures existentes de TST3,
  memoria, paging, IPv4 e font passaram a verificar `compress_get_stats`,
  `pmm_alloc_page`, `paging_get_page`, `ipv4_protocol_name` e
  `font_get_glyph`, incluindo seus vinculos C diretos no registro de cobertura.
  As execucoes host-only terminaram `PASS` com instrumentacao, limites e
  limpeza preservados. A sincronizacao atual registra 7.197 superficies, 3.890
  `COVERED`, 3.307 `PENDING` e 85 casos. O fechamento integral do catalogo, o
  gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/RTC concluido em 2026-09-02: a fixture host-only de
  `host:drivers:rtc-status` passou a simular a porta CMOS em vez de acessar I/O
  privilegiado. O caso cobre inicializacao invalida e valida, leituras BCD e
  binarias, modos de 12 e 24 horas, calendario, leituras estaveis, autoteste,
  timeout de atualizacao e estado publicado apos falha. O relatorio
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, observou
  26 enderecos sem desconhecidos ou ambiguos e resolveu todas as 17 superficies
  de `src/drivers/rtc.c`. O catalogo registra 7.196 superficies, 4.113
  `COVERED`, 3.083 `PENDING` e 87 casos; o fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

Incremento Storage/FAT32 concluido em 2026-09-02: o caso existente
`host:storage:fat32` passou a exercitar diretamente as APIs publicas
`fat32_write_file` e `fat32_read_file` em round-trip sobre imagem FAT32
estatica, seguido de remocao e verificacao de limpeza. O alvo
`make test-fat32-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou
`PASS`, e a evidencia instrumentada foi sincronizada sem enderecos
desconhecidos ou ambiguos. O catalogo registra 7.197 superficies, 3.892
`COVERED`, 3.305 `PENDING` e 85 casos; o fechamento integral continua
pendente.

- Incremento Storage/BIO concluido em 2026-09-02: o caso existente
  `host:storage:block` passou a exercitar os callbacks ATA publicados pelo
  inventario, o despacho assincrono de BIO, a leitura fisica usada no
  writeback parcial e os caminhos de espera do block-cache. A reentrada
  durante leitura retorna `ERR_TIMEOUT` de forma deterministica, sem espera
  real ou hardware. `make test-block-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`; a evidencia
  foi sincronizada sem enderecos desconhecidos ou ambiguos. O catalogo registra
  7.197 superficies, 3.898 `COVERED`, 3.299 `PENDING` e 85 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Reconciliação de cobertura Core/panic concluída em 2026-09-04 10:08
  (America/Sao_Paulo). O caso existente `host:kernel:panic` foi executado para
  gerar o relatório `build/test-results/panic-host/coverage.json`, que resolveu
  as rotas reais de `src/kernel/panic.c`. A sincronização reconheceu também as
  três APIs públicas correspondentes, `panic`, `panic_halt` e `panic_memory`,
  antes pendentes porque o relatório da execução anterior não estava presente.
  Não houve alteração no contrato do panic nem execução de halt real.

  A sincronização, a renderização e `make catalog-test` passaram. O catálogo
  registra 7.219 superfícies, 5.220 `COVERED`, 1.999 `PENDING` e 137 casos.

- Incremento Shell/hosted — fechamento final concluído em 2026-09-04 10:04
  (America/Sao_Paulo). O caso existente `host:shell:hosted` foi executado
  novamente e o relatório `build/test-results/shell-hosted-host/coverage.json`
  confirmou as três superfícies que ainda estavam `PENDING`, sem endereços
  desconhecidos ou símbolos ambíguos. A sincronização e a renderização do
  catálogo passaram sem alteração no código do Shell.

  O catálogo registra 7.219 superfícies, 5.217 `COVERED`, 2.002 `PENDING` e
  137 casos. O fechamento integral, o gate estrito e a validação TST7 completa
  continuam pendentes.

- Incremento Processos/runtime concluido em 2026-09-04. Foi criado o caso
  host-only `host:process:runtime` e o alvo `make test-process-host`, compilando
  `src/process/process.c` real com fixtures estaticas de paging, VMA, memoria,
  SLAB, syscall, sinais, IPC, VFS e scheduler. A fixture cobre estado inicial,
  snapshots, limites de criacao, transicoes, cancelamento, terminacao,
  desligamento, wait queues e limpeza, sem executar instrucoes privilegiadas,
  hardware ou allocator real.

  A execucao instrumentada terminou `PASS`, com `status=PASS`, `covered=70`,
  `unknown=0` e `ambiguous=0` no relatorio
  `build/test-results/process-host/coverage.json`. Tambem passaram
  `make q3check`, `make clean`, `make`, `make test-process-host`,
  `make catalog-test` e 71 testes unitarios dos runners. O catalogo atual
  registra 7.219 superficies, 5.183 `COVERED`, 2.036 `PENDING` e 136 casos.
  As entradas de baixo nivel que dependem de contexto real de entrada
  (`process_context_switch`, entradas de usuario, idle e bootstrap do
  scheduler) permanecem pendentes; nao foram marcadas artificialmente.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento UI/taskbar concluido em 2026-09-03. O novo caso host-only
  `host:ui:taskbar` usa dependencias falsas de VESA, display, desktop, mouse,
  timer e desenho para exercitar TUI e GUI, layouts, limites de botoes, menus,
  configuracao, cliques, relogio e selecao de janelas. A execucao instrumentada
  terminou `PASS`, resolveu todas as 47 funcoes de `src/taskbar/taskbar.c` e
  nao registrou enderecos desconhecidos ou simbolos ambiguos. O caso foi
  adicionado ao `full` do TST7, o catalogo foi sincronizado, renderizado e
  validado com 7.219 superficies, 5.120 `COVERED`, 2.099 `PENDING` e 134
  casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Storage/procfs concluido em 2026-09-02: foi criado o caso
  host-only `host:storage:procfs` com o provider procfs real, VFS, processos,
  snapshots e controles de log simulados. A fixture exercitou inicializacao,
  listagem, lookup, leitura dos nos globais e de processo, mapas, seeks, poll,
  ioctl, sync, permissoes, controles, limites, callbacks de erro e limpeza.
  A execucao instrumentada terminou `PASS` e resolveu as 10 superficies
  pendentes de `src/fs/procfs.c`, incluindo `procfs_test_process_entry`, sem
  enderecos desconhecidos ou ambiguos. A sincronizacao registra 7.196
  superficies, 4.222 `COVERED`, 2.974 `PENDING` e 93 casos. A variante de
  CPU no host usa somente o fallback de teste `ZEPHYROS_HOST_TEST`; o build
  freestanding normal preserva o caminho Assembly original. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Storage/WAV concluido em 2026-09-02: foi criado o caso host-only
  `host:storage:wav` com o parser e reprodutor reais, allocator estatico,
  playback AC97 simulado e instrumentacao dinamica. A fixture exercitou
  headers RIFF/WAVE, chunks `fmt` e `data`, metadados, duracao, playback,
  ownership, double free, entradas invalidas, truncadas, taxa de amostragem
  zero e limpeza. Foi corrigido o caminho de rejeicao de taxa de amostragem
  zero para liberar o buffer ja adquirido. `make test-wav-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos. O catalogo registra 7.196 superficies, 4.235
  `COVERED`, 2.961 `PENDING` e 94 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Storage/BMP concluido em 2026-09-02: foi criado o caso host-only
  `host:storage:bmp` com o parser e renderizador reais, allocator estatico,
  framebuffer e VESA simulados e instrumentacao dinamica. A fixture exercitou
  formatos 1, 4, 8 e 24 bpp, paletas, orientacao, desenho, transparencia,
  redimensionamento, escala, entradas invalidas, truncadas, overflow e falhas
  de alocacao. `make test-bmp-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`, cobrindo as
  nove superficies pendentes de `src/fs/bmp.c`, sem enderecos desconhecidos ou
  ambiguos. O catalogo registra 7.196 superficies, 4.244 `COVERED`, 2.952
  `PENDING` e 95 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/RNG concluido em 2026-09-02: foi criado o caso host-only
  `host:drivers:rng` com backend deterministico de CPUID/RDRAND somente para o
  host e instrumentacao dinamica. A fixture exercitou inicializacao com CPU ou
  RDRAND indisponivel, leitura de palavras, buffer nulo, leitura vazia, falha
  de hardware e validacao do estado publicado. `make test-rng-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`, cobrindo as
  cinco superficies pendentes de `src/drivers/rng.c`, sem enderecos desconhecidos
  ou ambiguos. O caminho freestanding continua usando as instrucoes Assembly
  originais. O catalogo registra 7.196 superficies, 4.244 `COVERED`, 2.952
  `PENDING` e 96 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/Serial concluido em 2026-09-02: foi criado o caso host-only
  `host:drivers:serial` com portas UART simuladas, instrumentacao dinamica e
  isolamento de instrucoes privilegiadas no host. A fixture exercitou
  inicializacao COM1, leitura sem dados e com dados, fila de transmissao,
  filtragem de bytes, sequencias ANSI, estado do transmissor e limites de
  `flush`. `make test-serial-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`, cobrindo as
  duas superficies pendentes de `src/drivers/serial.c`, sem enderecos
  desconhecidos ou ambiguos. O caminho freestanding preserva as operacoes de
  portas e flags originais. O catalogo registra 7.196 superficies, 4.251
  `COVERED`, 2.945 `PENDING` e 97 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/TSS concluido em 2026-09-02: foi criado o caso host-only
  `host:drivers:tss` com fixture de GDT, `tss_flush()` simulado e instrumentacao
  dinamica. A fixture exercitou estado antes da inicializacao, stacks invalidas
  e validas, inicializacao repetida e montagem do descritor TSS. `make
  test-tss-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`,
  cobrindo as duas superficies pendentes de `src/drivers/tss.c`, sem enderecos
  desconhecidos ou ambiguos. O caminho freestanding preserva `lgdt`, a troca de
  segmentos e o flush original. O catalogo registra 7.196 superficies, 4.256
  `COVERED`, 2.940 `PENDING` e 98 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/Speaker concluido em 2026-09-02: foi criado o caso
  host-only `host:drivers:speaker` com portas PIT e PC speaker simuladas e
  instrumentacao dinamica. A fixture exercitou inicializacao, desligamento,
  frequencia zero, beep, melody, duracoes e espera por ticks. `make
  test-speaker-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`,
  cobrindo as duas superficies pendentes de `src/drivers/speaker.c`, sem
  enderecos desconhecidos ou ambiguos. O caminho freestanding preserva I/O de
  portas e `hlt`; no host, apenas a fixture substitui essas operacoes. O
  catalogo registra 7.196 superficies, 4.258 `COVERED`, 2.938 `PENDING` e 99
  casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Boot/recovery runtime concluido em 2026-09-03: foi criado o caso
  host-only `host:boot:recovery-runtime` e o alvo
  `make test-recovery-runtime-host`. A fixture validou diretamente os cinco
  utilitarios freestanding de memoria, strings e log com buffers estaticos,
  sem executar o loader de recuperacao nem acessar hardware. O relatorio
  instrumentado terminou `PASS`, resolveu as cinco superficies de
  `src/boot/recovery_runtime.c` e nao registrou enderecos desconhecidos ou
  ambiguos. Foram executados `make test-recovery-runtime-host`, as 69 fixtures
  host-only do registro e a sincronizacao/renderizacao do catalogo. O catalogo
  registra 7.198 superficies, 4.342 `COVERED`, 2.856 `PENDING` e 110 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Shell/Media Player concluido em 2026-09-03. Foi criado o caso
  host-only `host:shell:mediaplayer` com o alvo
  `make test-mediaplayer-host`. A fixture compila o
  `src/shell/mediaplayer.c` real com arquivos, audio, imagem, AC97, VESA,
  timer e recovery falsos; exercita playback individual e combinado, pausa,
  retomada, parada, atualizacao, limite de nome, arquivos ausentes, formatos
  invalidos, dependencias indisponiveis e limpeza.

  `make test-mediaplayer-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`. O relatorio instrumentado
  `build/test-results/mediaplayer-host/coverage.json` resolveu as 12
  superficies de `src/shell/mediaplayer.c`, sem enderecos desconhecidos ou
  ambiguos. Tambem passaram a sincronizacao/renderizacao do catalogo,
  `make catalog-test`, `make q3check`, `make clean`, `make` e a execucao do
  caso apos o build limpo. O catalogo registra 7.219 superficies, 4.929
  `COVERED`, 2.290 `PENDING` e 130 casos. A declaracao de cobertura integral,
  o gate estrito e a validacao TST7 completa continuam pendentes; `mp_main`,
  que nao possui implementacao no codigo ativo, permanece pendente para
  decisao de implementacao ou aposentadoria documentada.

- Incremento Drivers/USB names concluido em 2026-09-02: os casos host-only
  existentes `host:drivers:usb-hid` e `host:drivers:usb-msc` passaram a ativar
  a instrumentacao tambem durante os contratos de nomes de estado e tipo. As
  execucoes `make test-usb-hid-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  e `make test-usb-msc-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminaram `PASS`; os relatorios resolveram as tres superficies pendentes,
  sem enderecos desconhecidos ou ambiguos. O catalogo registra 7.196
  superficies, 4.261 `COVERED`, 2.935 `PENDING` e 99 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/Keyboard concluido em 2026-09-02: foi criado o caso
  host-only `host:drivers:keyboard` com controlador PS/2, IRQ, fila de eventos
  e portas simulados e instrumentacao dinamica. A fixture exercitou tabelas de
  scancode, teclas ABNT2, estado antes da inicializacao, registro de filtros,
  inicializacao, metricas, reset e falha de dependencia. `make
  test-keyboard-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`,
  cobrindo as sete superficies pendentes de `src/drivers/keyboard.c`, sem
  enderecos desconhecidos ou ambiguos. O caminho freestanding preserva CLI,
  STI, I/O e espera do controlador; no host, essas operacoes sao neutralizadas
  somente pela fixture. O catalogo registra 7.196 superficies, 4.268
  `COVERED`, 2.928 `PENDING` e 100 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Core/ZTEST adapter concluido em 2026-09-02: foi criado o caso
  host-only `host:tst2:protocol-adapter` com transporte serial, relogio e
  executores de kernel simulados. A fixture exercitou inicializacao sem serial,
  handshake fragmentado, `READY`, `HEARTBEAT`, `RUN`, `ABORT`, panic, timeout,
  roteamento TST4/TST5/TST6, falha com fase e bloqueios antes de `READY`.
  `make test-protocol-adapter-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`; o relatorio instrumentado cobriu as nove superficies
  pendentes de `src/core/test_protocol.c`, sem enderecos desconhecidos ou
  ambiguos. O catalogo registra 7.196 superficies, 4.277 `COVERED`, 2.919
  `PENDING` e 101 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento TST5/black-box marker concluido em 2026-09-02: foi criado o caso
  host-only `host:tst5:blackbox` com observador de terminal estatico e
  `process_yield()` controlado. A fixture exercitou os nove marcadores de
  cenarios TST5, snapshots com nova geracao, progresso limitado e selecao
  invalida. `make test-blackbox-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`; o relatorio instrumentado cobriu a superficie pendente de
  `src/core/kernel_tests_blackbox.c`, sem enderecos desconhecidos ou
  ambiguos. O catalogo registra 7.196 superficies, 4.278 `COVERED`, 2.918
  `PENDING` e 102 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Core/test_coverage concluido em 2026-09-03: foi criado o caso
  host-only `host:core:test-coverage` com transporte serial falso e o alvo
  `make test-coverage-host`. A fixture exercitou escrita parcial, ausencia de
  progresso, identificador nulo e truncado, tabela hash, callbacks de
  instrumentacao e emissao ZCOV. O relatorio instrumentado terminou `PASS`, sem
  enderecos desconhecidos ou ambiguos, resolvendo as 11 superficies pendentes
  de `src/core/test_coverage.c`; a variante host usa enderecos de 64 bits e o
  caminho freestanding permanece em 32 bits. O catalogo registra 7.197
  superficies, 4.290 `COVERED`, 2.907 `PENDING` e 103 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/wifi_manager concluido em 2026-09-02: foi criado o caso
  host-only `host:core:wifi-manager` com fixtures estaticos de PCI, USB e
  RTL8811CU. A fixture exercitou formatacao e busca case-insensitive de IDs,
  inventario PCI, candidatos USB, estados READY/UNSUPPORTED/ERROR, scan,
  conexao aberta, limites, metadados PCI invalidos, backend indisponivel,
  falha do driver, validacao e recuperacao. `make test-wifi-manager-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS`; o relatorio
  instrumentado resolveu todas as 25 superficies de `src/core/wifi_manager.c`
  sem enderecos desconhecidos ou ambiguos. O catalogo foi sincronizado e
  validado com 7.196 superficies, 4.131 `COVERED`, 3.065 `PENDING` e 88
  casos; o fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Core/app_package concluido em 2026-09-02: a fixture host-only
  `host:core:app-package` foi ampliada com filesystem FAT12/FAT32 simulado,
  pacotes ZPKG/ZAPP validos, parsing, CRC, instalacao, atualizacao, failpoint
  com recuperacao, rollback, remocao e modo legado. O relatorio instrumentado
  `build/test-results/package-host/coverage.json` terminou `PASS` e cobriu as
  111 superficies de `src/core/app_package.c`, sem escrita em armazenamento
  real. A sincronizacao registra 7.196 superficies, 4.093 `COVERED` e 3.103
  `PENDING`; o fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Storage/FAT12 concluido em 2026-09-02: o caso existente
  `host:storage:fat12` passou a exercitar as APIs legadas de escrita e remocao
  na raiz, escrita e remocao em subdiretorio e criacao de entradas de
  diretorio, sobre a imagem FAT12 falsa. A fixture detectou e corrigiu a
  gravacao inconsistente de nomes 8.3 em `fat12_write_file`; o helper privado
  `to_upper`, sem chamadores, foi removido. `make test-fat12-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` terminou `PASS` e a evidencia
  instrumentada foi sincronizada sem enderecos desconhecidos ou ambiguos. O
  catalogo registra 7.196 superficies, 3.903 `COVERED`, 3.293 `PENDING` e 85
  casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Storage/VFS concluido em 2026-09-02: a fixture host-only existente
  passou a exercitar stdin com mensagem de teclado, callbacks nao suportados,
  poll, pipes sem leitores, pipe cheio, socket sem poll e redirecionamento de
  escrita com limites, caminhos invalidos e limpeza. O autoteste real
  `qemu:tst4:storage-vfs` tambem passou a verificar abertura, escrita e poll
  da fixture e as operacoes invalidas de stdin/stdout. A execucao normal
  `make test-tst4-qemu-storage-vfs` passou em uma unica iteracao com
  `READY -> HEARTBEAT -> BEGIN -> PASS`; a imagem instrumentada
  `cov-tst4-storage-6` tambem passou e resolveu 595 superficies no relatorio
  dinamico. O catalogo foi sincronizado e validado com 7.196 superficies,
  3.921 `COVERED`, 3.275 `PENDING` e 85 casos. O fechamento integral continua
  pendente; `tst3-sanitize` segue `BLOCKED` nesta maquina pela permissao do
  runtime LLVM.

- Incremento Core/app_files concluido em 2026-09-02: foi criado o caso
  host-only `host:core:app-files` com VFS falsa, cobrindo pre-condicoes antes
  da inicializacao, inicializacao idempotente, encaminhamento das operacoes de
  arquivo, validacao de saidas, limites e propagacao de erros canonicos. A
  fixture instrumentada terminou `PASS` e resolveu 33 superficies reais no
  relatorio dinamico, incluindo todas as funcoes de `src/core/app_files.c`.
  Foram executados `make q3check`, `make clean`, `make`,
  `make test-app-files-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos terminaram com sucesso. O catalogo registra
  7.196 superficies, 3.953 `COVERED`, 3.243 `PENDING` e 86 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/RTC — fechamento final concluído em 2026-09-04 10:01
  (America/Sao_Paulo). O caso existente `host:drivers:rtc-status` foi executado
  novamente após o build limpo. A fixture usa CMOS falso e resolveu as nove
  superfícies que ainda estavam `PENDING`: I/O CMOS, validação de estado,
  leituras estáveis, conversão e inicialização. O relatório
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, com
  `unknown_addresses=[]` e `ambiguous_symbols=[]`.

  Também passaram a sincronização/renderização do catálogo e
  `make catalog-test`. O catálogo registra 7.219 superfícies, 5.214
  `COVERED`, 2.005 `PENDING` e 137 casos. O fechamento integral, o gate estrito
  e a validação TST7 completa continuam pendentes.

- Reconciliação de cobertura Shell/entrada concluída em 2026-09-04. O caso
  existente `host:shell:input` foi executado novamente e o relatório
  `build/test-results/shell-input-host/coverage.json` confirmou a execução de
  `shell_input_init` e `shell_input_get_buffer`, que ainda estavam pendentes
  no catálogo apesar de já serem exercitadas pela fixture. A sincronização e
  a renderização marcaram essas duas superfícies C como `COVERED`; não houve
  alteração no código do Shell. O catálogo registra 7.219 superfícies, 5.205
  `COVERED`, 2.014 `PENDING` e 137 casos. O fechamento integral, o gate estrito
  e a validação TST7 completa continuam pendentes.

- Incremento Processos/threads concluído em 2026-09-04 09:52
  (America/Sao_Paulo). Foi adicionado o caso host-only
  `host:process:threads` e o alvo `make test-thread-host`. A fixture compila
  `src/thread/thread.c` real com pool de threads e stacks estáticas, cobrindo
  inicialização, criação, seleção, yield, bloqueio, espera, cancelamento,
  desbloqueio, timeout, indisponibilidade, limites e limpeza. A espera usada
  nos cenários de indisponibilidade e timeout é criada pela API real, para que
  os callbacks internos de transição e a restauração do estado sejam
  exercitados sem executar instruções privilegiadas no processo host.

  A execução instrumentada terminou `PASS`, resolveu 29 superfícies de
  `src/thread/thread.c` e registrou `unknown_addresses=[]` e
  `ambiguous_symbols=[]` em `build/test-results/thread-host/coverage.json`.
  Também passaram `make q3check`, `make clean`, `make`,
  `make test-thread-host`, `make catalog-test` e os 72 testes unitários de
  `tests.unit.test_core_host_runner` e `tests.unit.test_tst7_runner`.
  O catálogo registra 7.219 superfícies, 5.203 `COVERED`, 2.016 `PENDING` e
  137 casos. `thread_context_switch` e a entrada Assembly correspondente
  continuam pendentes porque esta etapa host-only não simula cobertura
  freestanding; elas exigem evidência real no kernel/QEMU.

- Incremento Core/syscall concluido em 2026-09-04. O novo caso
  `host:core:syscall` usa uma fixture estatica para exercitar o dispatcher real
  em ring 0 e ring 3, incluindo inicializacao, habilitacao, limites, copias
  protegidas, VFS/App API, IPC, sinais e rejeicoes de estado. A cobertura
  instrumentada resolveu as 34 funcoes de `src/core/syscall.c`, sem enderecos
  desconhecidos ou simbolos ambiguos. O fechamento integral, o gate estrito e
  a validacao TST7 completa continuam pendentes.

- Incremento Core/app_loader concluido em 2026-09-03. O caso host-only
  `host:core:app-loader` foi adicionado com fixture estatica de processo,
  filesystem, paging e syscall. O teste cobre parser de argumentos, limites,
  corrupcao de offsets, cabecalhos e layouts ZAPP invalidos, inicializacao,
  execucao suspensa, foco, reap, cancelamento, leitura de arquivo e falhas
  controladas. O relatorio instrumentado terminou `PASS`, sem enderecos
  desconhecidos ou ambiguos, e o catalogo foi sincronizado, renderizado e
  validado por `make catalog-test`. O catalogo atual registra 7.219
  superficies, 5.110 `COVERED`, 2.109 `PENDING` e 133 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/ZTEST adapter - correção da fixture e APIs públicas - concluído
  em 2026-09-03. O caso `host:tst2:protocol-adapter` passou a incluir o stub
  controlado de `kernel_tests_run_assembly()` e exercita também a rota
  `qemu:tst7:assembly`, sem vincular o kernel real ao host-only. Depois do build
  limpo, `make test-protocol-adapter-host`, `make q3check`, `make clean`, `make`
  e `make catalog-test` terminaram com sucesso. O relatório instrumentado
  `build/test-results/protocol-adapter-host/coverage.json` terminou `PASS`, sem
  endereços desconhecidos ou símbolos ambíguos; as seis APIs públicas de
  `src/include/core/test_protocol.h` foram vinculadas aos seus símbolos C
  observados. O único pendente relacionado é
  `c:src/kernel/kernel.c:test_protocol_process_main`, que exige evidência QEMU
  do processo do protocolo e não foi marcado por este fixture host-only. O
  catálogo atual registra 7.219 superfícies, 5.092 `COVERED`, 2.127 `PENDING`
  e 132 casos.

- Incremento Shell/hosted — evidencia final — concluido em 2026-09-03. O caso
  existente `host:shell:hosted` foi executado novamente após o build limpo com
  Window Manager, terminal e mouse falsos. O relatorio
  `build/test-results/shell-hosted-host/coverage.json` terminou `PASS`,
  resolveu as 8 funcoes de `src/shell/shell_hosted.c` e nao registrou erros de
  cobertura. Tambem passaram `python tools/test_catalog.py sync`,
  `python tools/test_catalog.py render` e `make catalog-test`. O catalogo atual
  registra 7.219 superficies, 5.086 `COVERED`, 2.133 `PENDING` e 132 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/RTC — fechamento de cobertura — concluido em 2026-09-03.
  O caso existente `host:drivers:rtc-status` foi executado novamente depois do
  build limpo, com CMOS falso e sem I/O privilegiado. O relatorio
  `build/test-results/rtc-status-host/coverage.json` terminou `PASS`, resolveu
  as 17 funcoes de `src/drivers/rtc.c` — incluindo I/O CMOS, leituras estaveis,
  conversao e inicializacao — e nao registrou erros de cobertura.

  Tambem passaram `python tools/test_catalog.py sync`,
  `python tools/test_catalog.py render` e `make catalog-test`. O catalogo atual
  registra 7.219 superficies, 5.081 `COVERED`, 2.138 `PENDING` e 132 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Shell/dispatcher — fechamento da tabela — concluido em 2026-09-03.
  A fixture existente `host:shell:dispatch` passou a enviar cada um dos 95
  comandos registrados com argumentos sentinela, confirmando despacho unico,
  preservacao dos argumentos e retorno `OK`. Os handlers foram substituidos
  por stubs somente nesta fixture, portanto a evidencia cobre a tabela e o
  contrato do dispatcher, nao a implementacao interna de cada handler.

  `make test-shell-dispatch-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`. O relatorio instrumentado manteve
  `unknown_addresses=[]` e `ambiguous_symbols=[]`; o catalogo sincronizado e
  renderizado passou a registrar todas as 97 superficies de
  `src/shell/shell_dispatch.c` como `COVERED`, com os 95 comandos vinculados em
  modo `integration` e as duas funcoes C vinculadas pelo relatorio direto. O
  catalogo atual registra 7.219 superficies, 5.072 `COVERED`, 2.147
  `PENDING` e 132 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Shell/pipeline concluido em 2026-09-03. Foi criado o caso
  host-only `host:shell:pipeline` com o alvo
  `make test-shell-pipeline-host`. A fixture compila o
  `src/shell/shell_pipeline.c` real com VFS, threads, video e logs falsos;
  exercita parsing, limites de segmentos e destinos, pipes, leitura, escrita,
  redirecionamento, workers, autoteste de pipe, falhas de criacao, erros de
  I/O, overflow e limpeza.

  A execucao terminou `PASS` com warnings tratados como erro. O relatorio
  instrumentado `build/test-results/shell-pipeline-host/coverage.json`
  resolveu as 26 superficies C de `src/shell/shell_pipeline.c`, sem enderecos
  desconhecidos ou ambiguos. O catalogo foi sincronizado e renderizado;
  registra 7.219 superficies, 4.977 `COVERED`, 2.242 `PENDING` e 132 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Shell/job concluido em 2026-09-03. Foi criado o caso host-only
  `host:shell:job` com o alvo `make test-shell-job-host`. A fixture compilou o
  `src/shell/shell_job.c` real com relogio, teclado, IPC, video e runtime
  falsos; exercitou ciclo de sucesso, falha, cancelamento, drenagem, timeout,
  deadlines, wakeups, geracoes obsoletas, eventos bloqueados e `job status`.

  A primeira execucao terminou `PASS` com warnings tratados como erro. O
  relatorio instrumentado `build/test-results/shell-job-host/coverage.json`
  resolveu as 30 superficies C de `src/shell/shell_job.c` e nao apresentou
  enderecos desconhecidos ou ambiguos. Tambem passaram a sincronizacao e
  renderizacao do catalogo. O catalogo registra 7.219 superficies, 4.955
  `COVERED`, 2.264 `PENDING` e 131 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Shell/entrada — fechamento de cobertura — concluido em 2026-09-03.
  O caso host-only existente `host:shell:input` foi executado para registrar
  tambem os caminhos de inicializacao e consulta do buffer que ainda estavam
  pendentes. O relatorio `build/test-results/shell-input-host/coverage.json`
  terminou `PASS`, resolveu as 16 funcoes de `src/shell/shell_input.c` e nao
  encontrou enderecos desconhecidos ou ambiguos. A sincronizacao e a
  renderizacao do catalogo passaram; o catalogo registra 7.219 superficies,
  4.898 `COVERED`, 2.321 `PENDING` e 128 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Shell/entrada — evidência final — concluido em 2026-09-03.
  O caso existente `host:shell:input` foi executado após o build limpo. O
  relatorio `build/test-results/shell-input-host/coverage.json` terminou
  `PASS`, resolveu as 16 funcoes de `src/shell/shell_input.c`, incluindo
  `shell_input_init` e `shell_input_get_buffer`, sem erros de cobertura.
  Tambem passaram a sincronizacao/renderizacao do catalogo e `make
  catalog-test`. O catalogo atual registra 7.219 superficies, 5.083
  `COVERED`, 2.136 `PENDING` e 132 casos. O fechamento integral, o gate estrito
  e a validacao TST7 completa continuam pendentes.

- Incremento Seguranca/tls_client concluido em 2026-09-03. Foi criado o caso
  host-only `host:security:tls-client` com o alvo
  `make test-tls-client-host`. A fixture compila o `src/core/tls_client.c`
  real contra um engine BearSSL falso e exercita inicializacao, configuracao,
  conversao de tempo, handshake, envio, recepcao, EOF, falhas de I/O,
  indisponibilidade de entropia, limites de SNI, validacao de estado e
  limpeza, sem rede externa. O relatorio instrumentado terminou `PASS`,
  resolveu as 12 superficies antes pendentes de `src/core/tls_client.c` e
  nao apresentou enderecos desconhecidos ou ambiguos. A sincronizacao,
  renderizacao, `make catalog-test` e os testes unitarios do runner passaram;
  o catalogo registra 7.219 superficies, 4.922 `COVERED`, 2.297 `PENDING` e
  129 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Storage/sysfs concluido em 2026-09-03. Foi criado o caso
  host-only `host:storage:sysfs` com o alvo `make test-sysfs-host`. A fixture
  liga o provider `src/fs/sysfs.c` real a inventarios falsos de PCI, rede,
  bloco e energia; exercita lookup, listagens, todos os atributos, snapshots
  somente leitura, permissoes, seek, poll, overflow, fallback de energia,
  autoteste e limpeza. O relatorio instrumentado
  `build/test-results/sysfs-host/coverage.json` terminou `PASS`, resolveu as
  58 funcoes e 8 APIs publicas de `src/fs/sysfs.c`, sem enderecos desconhecidos
  ou ambiguos. Tambem passaram os testes unitarios dos runners,
  `make catalog-test`, `make q3check`, `make clean`, `make` e o caso sysfs
  apos o build limpo. O catalogo registra 7.219
  superficies, 4.895 `COVERED`, 2.324 `PENDING` e 128 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Network/socket runtime concluido em 2026-09-03: foi criado o caso
  host-only `host:network:socket-runtime` e o alvo
  `make test-socket-runtime-host`. A fixture conecta o `src/core/socket.c`
  real a backends falsos de TCP, VFS, filas de espera, SKB e processo, sem
  rede ou hardware. Foram exercitados inicializacao idempotente, criacao,
  bind/listen/connect/accept, envio e recepcao TCP, EOF, erros, polling,
  remocao de cliente UNIX pendente antes do `accept`, adaptadores VFS, filas
  UNIX, cancelamento, capacidade e autoteste com
  limpeza do estado global.

  `make test-socket-runtime-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`. O relatorio instrumentado
  `build/test-results/socket-runtime-host/coverage.json` resolveu as 65
  funcoes de `src/core/socket.c`, sem enderecos desconhecidos ou ambiguos.
  Tambem passaram `make test-net-socket-host`, os testes unitarios dos
  runners, sincronizacao/renderizacao e `make catalog-test`. O catalogo
  registra 7.219 superficies, 4.896 `COVERED`, 2.323 `PENDING` e 128 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/RTL8139 concluido em: 2026-09-03 (America/Sao_Paulo).
  Foi criado o caso host-only `host:drivers:rtl8139` e o alvo
  `make test-rtl8139-host`. A fixture simulou PCI, portas I/O, DMA,
  temporizador, IRQ e bottom-half para exercitar inicializacao, reset, leitura
  de MAC, transmissao, recepcao, erros de ring, timeout, quiescencia,
  recuperacao e limpeza. O relatorio instrumentado
  `build/test-results/rtl8139-host/coverage.json` terminou `PASS`, observou 66
  enderecos sem desconhecidos ou ambiguos e resolveu as 36 funcoes de
  `src/drivers/rtl8139.c` e as duas APIs publicas correspondentes. Foram
  executados `make q3check`, `make clean`, `make`,
  `make test-rtl8139-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos passaram. O catalogo registra 7.204 superficies,
  4.619 `COVERED`, 2.585 `PENDING` e 119 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/EHCI concluido em 2026-09-03: foi criado o caso
  host-only `host:drivers:ehci` e o alvo `make test-ehci-host`. A fixture usa
  PCI, MMIO, DMA, temporizador e dispositivos USB falsos para exercitar
  inicializacao, reset, enumeracao high-speed, descritores, transfers de
  controle e bulk, interrupt, timeout, erro de qTD, recuperacao, falhas de
  hardware e limpeza. O relatorio instrumentado
  `build/test-results/ehci-host/coverage.json` terminou `PASS`, observou 83
  enderecos sem desconhecidos ou ambiguos e resolveu as 52 superficies de
  `src/drivers/ehci.c`, sem I/O privilegiado ou hardware real. Foram executados
  `make q3check`, `make clean`, `make`,
  `make test-ehci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos passaram. O catalogo registra 7.203 superficies,
  4.580 `COVERED`, 2.623 `PENDING` e 118 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/UHCI concluido em 2026-09-03: foi criado o caso
  host-only `host:drivers:uhci` e o alvo `make test-uhci-host`. A fixture usa
  PCI, DMA, portas, IRQ, temporizador e dispositivos USB falsos para exercitar
  inicializacao, reset, enumeracao, descritores, transfers de controle e bulk,
  interrupt, timeout, recuperacao, entradas invalidas e limpeza. O relatorio
  instrumentado `build/test-results/uhci-host/coverage.json` terminou `PASS`,
  observou 105 enderecos sem desconhecidos ou ambiguos e resolveu as 71
  superficies de `src/drivers/uhci.c`, sem I/O privilegiado ou hardware real.
  Foram executados `make q3check`, `make clean`, `make`,
  `make test-uhci-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos passaram. O catalogo registra 7.202 superficies,
  4.529 `COVERED`, 2.673 `PENDING` e 117 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/mouse concluido em 2026-09-03: foi criado o caso
  host-only `host:drivers:mouse` e o alvo `make test-mouse-host`. A fixture
  simulou a controladora PS/2, IRQ12, fila de entrada, framebuffer VESA e
  respostas Intellimouse para exercitar inicializacao, fallback de tres bytes,
  eventos de movimento, botoes e roda, coalescencia, cursor, configuracao,
  timeouts, ACK invalido, indisponibilidade, recuperacao e limpeza. O relatorio
  instrumentado `build/test-results/mouse-host/coverage.json` terminou `PASS`,
  resolveu as 46 superficies de `src/drivers/mouse.c` e as 13 APIs publicas
  correspondentes, sem enderecos desconhecidos ou ambiguos. Foram executados
  `make q3check`, `make clean`, `make`,
  `make test-mouse-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, a
  sincronizacao, renderizacao e validacao do catalogo e `git diff --check`;
  todos passaram. O build completo manteve apenas warnings legados em modulos
  nao relacionados ao mouse. O catalogo registra 7.204 superficies, 4.665
  `COVERED`, 2.539 `PENDING` e 120 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/E1000 concluido em 2026-09-03: foi criado o caso
  host-only `host:drivers:e1000` e o alvo `make test-e1000-host`. A fixture
  simulou PCI, MMIO, reset, MAC, DMA, IRQ deferred, descritores, transmissao,
  recepcao, fila RX, quiescencia e falhas de inicializacao. O relatorio
  instrumentado `build/test-results/e1000-host/coverage.json` terminou `PASS`,
  resolveu as 34 funcoes de `src/drivers/e1000.c` e as duas APIs publicas
  correspondentes, sem enderecos desconhecidos ou ambiguos. Foram executados
  `make test-e1000-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate` e `git diff --check`; os gates de
  `q3check`, build limpo e build completo ja haviam passado nesta sequencia.
  O catalogo registra 7.206 superficies, 4.689 `COVERED`, 2.517 `PENDING` e
  121 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.


- Incremento Drivers/RTL8811CU concluido em 2026-09-03. Foi criado o caso
  `host:drivers:rtl8811cu` e o alvo `make test-rtl8811cu-host`. A fixture
  simulou dispositivos USB EHCI high-speed, descritores, endpoints Bulk,
  filesystem e firmware falso, exercitando probe, estados de inicializacao,
  callbacks Ethernet, scan, associacao aberta, limites de SSID e caminhos
  de indisponibilidade segura. O relatorio instrumentado
  `build/test-results/rtl8811cu-host/coverage.json` terminou `PASS`, resolveu
  17 funcoes de `src/drivers/rtl8811cu.c` e as 7 APIs publicas correspondentes,
  sem enderecos desconhecidos ou ambiguos. Foram executados
  `make test-rtl8811cu-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`,
  `python tools/test_catalog.py sync`, `python tools/test_catalog.py render`,
  `python tools/test_catalog.py validate`, `make q3check`, `make clean`,
  `make` e `git diff --check`; todos passaram nesta etapa. O catalogo registra
  7.209 superficies, 4.741 `COVERED`, 2.468 `PENDING` e 123 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/AC97 concluido em 2026-09-03. Foi criado o caso
  host-only `host:drivers:ac97` e o alvo `make test-ac97-host`. A fixture
  simulou PCI, portas I/O, codec, reset, energia, playback, memoria, IRQ,
  limites de amostras, parada e falhas de inicializacao. O relatorio
  instrumentado `build/test-results/ac97-host/coverage.json` terminou `PASS`,
  resolveu as 22 funcoes de `src/drivers/ac97.c` e as APIs publicas
  correspondentes, sem enderecos desconhecidos ou ambiguos. Tambem foi
  corrigido o calculo do buffer de playback para alocar espaco por amostra e
  evitar escrita alem do buffer. Foram executados `make test-ac97-host
  HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, `python tools/test_catalog.py
  sync`, `python tools/test_catalog.py render`, `python tools/test_catalog.py
  validate`, `make q3check`, `make clean`, `make` e `git diff --check`; todos
  passaram nesta etapa. O catalogo registra 7.209 superficies,
  4.717 `COVERED`, 2.492 `PENDING` e 122 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/ATA concluido em 2026-09-03. Foi criado o caso host-only
  `host:drivers:ata` e o alvo `make test-ata-host`. A fixture usa portas,
  dados IDENTIFY, PIO e IRQ falsos para exercitar descoberta, inventario,
  parsing, limite LBA28, leitura, escrita, flush, contadores, retries,
  timeouts, entradas invalidas e recuperacao. O relatorio instrumentado
  `build/test-results/ata-host/coverage.json` terminou `PASS`, resolveu as 30
  funcoes observadas de `src/drivers/ata.c`, sem enderecos desconhecidos ou
  ambiguos, e nao acessou armazenamento ou I/O real. Foram executados
  `make test-ata-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`, a
  sincronizacao e renderizacao do catalogo, `python tools/test_catalog.py
  validate` e `git diff --check`; todos passaram nesta etapa. O catalogo
  registra 7.209 superficies, 4.757 `COVERED`, 2.452 `PENDING` e 124 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/RTC concluido em 2026-09-03. O caso existente
  `host:drivers:rtc-status` foi executado novamente apos o build limpo, com
  CMOS falso, leituras BCD/binaria e 12/24 horas, calendario, leituras
  estaveis, autoteste, timeout de atualizacao e estado publicado apos erro.
  O relatorio instrumentado terminou `PASS`, resolveu as 17 funcoes de
  `src/drivers/rtc.c` e a funcao `kmemset` usada pela fixture, sem enderecos
  desconhecidos ou ambiguos. O catalogo foi sincronizado, renderizado e
  validado; agora registra 7.219 superficies, 4.910 `COVERED`, 2.309
  `PENDING` e 128 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/IDT concluido em 2026-09-03. Foi criado o caso host-only
  `host:drivers:idt` e o alvo `make test-idt-host`. A fixture usa stubs para
  ISR/IRQ, PIC, flags, `lidt` e panic para exercitar inicializacao, gates,
  handlers simples e compartilhados, limites, unmask, estatisticas, EOI,
  syscall e despacho sem executar instrucoes privilegiadas. O relatorio
  instrumentado terminou `PASS`, resolveu as 20 funcoes observadas de
  `src/drivers/idt.c`, sem enderecos desconhecidos ou ambiguos. O catalogo foi
  sincronizado, renderizado e validado; agora registra 7.211 superficies,
  4.781 `COVERED`, 2.430 `PENDING` e 125 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/EHCI concluido em 2026-09-03. O caso host-only existente
  `host:drivers:ehci` foi executado novamente com a fixture PCI/MMIO/DMA/USB
  falsa. O registro passou a importar as APIs publicas de `ehci.h` somente
  quando o relatorio dinamico confirmou as implementacoes correspondentes.
  As 13 APIs pendentes do header foram vinculadas ao caso, sem alterar ABI ou
  hardware real. O catalogo foi sincronizado, renderizado e validado; agora
  registra 7.211 superficies, 4.794 `COVERED`, 2.417 `PENDING` e 125 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/video concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:video` e o alvo `make test-video-host`. A fixture usa
  framebuffer, fonte, VESA, mouse e logs falsos para exercitar inicializacao,
  desenho, cursor, flush, terminal, scrollback, snapshots validos e
  corrompidos, rolagem, suspensao, quiescencia e estados indisponiveis. O
  relatorio instrumentado `build/test-results/video-host/coverage.json`
  terminou `PASS`, observou 96 enderecos sem desconhecidos ou ambiguos e
  resolveu as 30 superficies pendentes de `src/drivers/video.c`. Foram
  executados `make q3check`, `make clean`, `make`, o caso apos o build limpo,
  a sincronizacao e renderizacao do catalogo e `make catalog-test`; todos
  passaram. O catalogo registra 7.198 superficies, 4.393 `COVERED`, 2.805
  `PENDING` e 115 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Drivers/ACPI concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:acpi` e o alvo `make test-acpi-host`. A fixture usa firmware,
  mapa E820, portas I/O e halt falsos para exercitar RSDP, RSDT/XSDT, FADT,
  MADT, FACS, AML `_S5_`, consultas, checksums, tabelas corrompidas e rotas
  de energia sem acesso a hardware real. O relatorio instrumentado
  `build/test-results/acpi-host/coverage.json` terminou `PASS`, observou 88
  enderecos sem desconhecidos ou ambiguos e resolveu as 56 superficies
  pendentes originais de `src/drivers/acpi.c`, alem dos dois seams exclusivos
  do build host. Tambem foi corrigida a sincronizacao para preservar os
  vinculos anteriores quando um relatorio dinamico ainda nao esta disponivel
  depois de `make clean`, sem promover associacoes novas. Foram executados
  `make q3check`, `make clean`, `make`, `make test-acpi-host` apos o build
  limpo, `make catalog-test`, os testes unitarios do catalogo e dos runners,
  `git diff --check` e a verificacao de processos residuais; todos passaram.
  O catalogo registra 7.200 superficies, 4.463 `COVERED`, 2.737 `PENDING` e
  116 casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Shell/hosted concluido em 2026-09-03: o caso host-only existente
  `host:shell:hosted` e o alvo `make test-shell-hosted-host` foram executados
  novamente com Window Manager, terminal e mouse falsos. A fixture exercitou
  modo Classic, abertura, reabertura, callbacks de desenho/tecla/mouse,
  fechamento e rollback quando o registro falha. A execucao terminou `PASS`,
  sem enderecos desconhecidos ou ambiguos, resolvendo as 8 funcoes de
  `src/shell/shell_hosted.c`. O catalogo registra 7.219 superficies, 4.901
  `COVERED`, 2.318 `PENDING` e 128 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento GUI/display e Shell/core concluido em 2026-09-03: os casos
  host-only `host:gui:display` e `host:shell:core` passaram com fixtures
  estaticas e cobertura dinamica real. Foram cobertos disponibilidade VESA,
  escalas, limites, conversao de pixels, refresh/rollback de cenas,
  inicializacao do Shell, mouse/scroll, suspensao do terminal, conclusao de
  comando e redraw pelo fluxo de `shell_handle_key`. Os 65 casos host-only,
  `make q3check`, build limpo, `make catalog-test` e os testes dos runners
  passaram; `display.c` ficou sem pendencias e `shell.c` tambem. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/usb_transport concluido em 2026-09-03: foi criado o caso
  host-only `host:core:usb-transport` com backends EHCI e UHCI falsos. A
  fixture cobriu entradas nulas, controlador desconhecido, controle, Bulk,
  reset de toggles, submissao e cancelamento de Interrupt, confirmando os
  codigos `ERR_NULL`/`ERR_UNAVAILABLE` e o dispatch correto dos argumentos.
  O relatorio instrumentado terminou `PASS`, resolveu os sete simbolos de
  `src/core/usb_transport.c` e nao registrou enderecos desconhecidos ou
  ambiguos. Foram executados `make q3check`, `make clean`, `make`, os 66 casos
  host-only do registro, `make catalog-test` e os 53 testes unitarios dos
  runners; todos passaram. O catalogo registra 7.198 superficies, 4.320
  `COVERED`, 2.878 `PENDING` e 107 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento GUI/primitivas concluido em 2026-09-03: foi criado o caso
  host-only `host:gui:widgets` e o alvo `make test-gui-host`. A fixture usa
  framebuffer, fonte, metricas de display e VESA falsos para exercitar temas,
  texto nativo e escalado, medicao, paineis, formas, gradientes, botoes,
  molduras e limites de tela. O relatorio instrumentado terminou `PASS`, com
  23 superficies resolvidas e nenhum endereco desconhecido ou ambiguo; as 11
  superficies C que estavam pendentes em `src/gui/gui.c` foram exercitadas por
  chamadas reais. Foram executados `make q3check`, `make clean`, `make`, os 67
  casos host-only do registro, a sincronizacao e renderizacao do catalogo,
  `make catalog-test`, 55 testes unitarios dos runners e `git diff --check`;
  todos passaram. O catalogo registra 7.198 superficies, 4.331 `COVERED`,
  2.867 `PENDING` e 108 casos. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Shell/VFS concluido em 2026-09-03: foi criado o caso host-only
  `host:shell:commands-vfs` e o alvo `make test-shell-commands-vfs-host`. A
  fixture exercitou `grep` com entrada fragmentada, comparacao sem diferenca
  de maiusculas, argumentos invalidos, falha de leitura/escrita e linha acima
  do limite; tambem validou `pipetest` em sucesso, erro e argumento invalido.
  O relatorio instrumentado terminou `PASS`, com 23 superficies resolvidas e
  nenhum endereco desconhecido ou ambiguo; as seis funcoes de
  `src/shell/shell_commands_vfs.c` foram exercitadas por chamadas reais. Foram
  executados `make test-shell-commands-vfs-host`, a sincronizacao e
  renderizacao do catalogo, `make catalog-test` e os testes dos runners. O
  catalogo registra 7.198 superficies, 4.337 `COVERED`, 2.861 `PENDING` e 109
  casos. O fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Kernel/panic concluido em 2026-09-03: foi criado o caso host-only
  `host:kernel:panic` e o alvo `make test-panic-host`. A fixture exercitou
  `panic`, `panic_memory`, o cabecalho de diagnostico, metricas com valores
  zero e no limite, mensagens ausentes e explicitas e o encaminhamento dos
  motivos `ERR_STATE`, `ERR_MEM` e personalizados ao protocolo. O halt foi
  capturado por `setjmp`/`longjmp` somente no build host; o build freestanding
  continua com o halt real. O relatorio instrumentado terminou `PASS`, resolveu
  as seis superficies C e as tres APIs publicas correspondentes, sem enderecos
  desconhecidos ou ambiguos. Foram regeneradas as 70 fixtures host-only do
  registro e todas passaram. Tambem passaram `make catalog-test`, 58 testes
  unitarios dos runners, `make q3check`, `make clean`, `make` e a repeticao de
  `make test-panic-host` apos o build limpo. O catalogo registra 7.198
  superficies, 4.351 `COVERED`, 2.847 `PENDING` e 111 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Drivers/PCI concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:pci` e o alvo `make test-pci-host`. A fixture usa um espaco de
  configuracao PCI falso para exercitar leitura, escrita, varredura de
  barramento, funcoes multifuncao, inventario, buscas por classe e ID,
  habilitacao de memoria/I/O/DMA, recusas de comando, estado nao inicializado,
  limite de 64 dispositivos e reinicializacao do inventario. O relatorio
  instrumentado terminou `PASS`, resolveu as 14 funcoes de
  `src/drivers/pci.c` sem enderecos desconhecidos ou ambiguos, e nao acessou
  portas I/O reais. Foram regeneradas as 71 fixtures host-only do registro e
  todas passaram; a sincronizacao e renderizacao do catalogo tambem passaram.
  O catalogo registra 7.198 superficies, 4.363 `COVERED`, 2.835 `PENDING` e
  112 casos. Tambem passaram `make q3check`, `make clean`, `make`, a repeticao
  de `make test-pci-host` apos o build limpo, `make catalog-test` e 59 testes
  unitarios dos runners. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento UI/icones concluido em 2026-09-03: foi criado o caso host-only
  `host:ui:icons` e o alvo `make test-icons-host`. A fixture usa filesystem,
  memoria, BMP e VESA falsos para exercitar fallback sem filesystem, defaults,
  mutacoes do registro, carga valida, formato invalido, falha de memoria,
  cache, desenho e limites de tela. O relatorio instrumentado terminou `PASS`,
  resolveu as 18 funcoes de `src/icons/icons.c` sem enderecos desconhecidos ou
  ambiguos e nao acessou hardware. Foram executados `make q3check`, `make
  clean`, `make`, o caso apos o build limpo, a matriz host compativel, a
  sincronizacao/renderizacao e `make catalog-test`; todos passaram. O catalogo
  registra 7.198 superficies, 4.380 `COVERED`, 2.818 `PENDING` e 113 casos.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/VESA concluido em 2026-09-03: foi criado o caso host-only
  `host:drivers:vesa` e o alvo `make test-vesa-host`. A fixture usa seams
  exclusivos do build host para o boot info e o framebuffer, mantendo o build
  freestanding com os enderecos reais. Foram exercitados inicializacao valida e
  invalida, modos 24/32 bpp, backbuffer, pixels, desenho, clipping, frames,
  metricas, flip, falha de alocacao e desativacao. O relatorio instrumentado
  terminou `PASS`, resolveu as 12 funcoes pendentes de `src/drivers/vesa.c`
  sem enderecos desconhecidos ou ambiguos e nao acessou hardware real. Tambem
  passaram `make q3check`, `make clean`, `make`, o caso apos o build limpo, 75
  casos host compativeis, a sincronizacao/renderizacao, `make catalog-test` e
  os testes unitarios dos runners. O catalogo registra 7.198 superficies,
  4.392 `COVERED`, 2.806 `PENDING` e 114 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/energia terminal concluido em 2026-09-03: o caso
  `host:core:power` passou a exercitar `power_reboot_commit`,
  `power_trigger_triple_fault` e `power_terminal_halt` por um seam exclusivo
  do build host. A fixture captura as acoes terminais com `setjmp`/`longjmp`,
  sem executar reset, halt ou triple fault no processo de teste, e valida as
  rotas de reboot por triple fault, halt apos commit parcial e ausencia de
  metodo de reboot. `make test-power-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  terminou `PASS`; os 62 casos host-only registrados tambem passaram. O
  catalogo registra 7.198 superficies, 4.294 `COVERED` e 2.904 `PENDING`.
  O fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/usb_hid concluido em 2026-09-02: foi criado o caso
  host-only `host:drivers:usb-hid` com dispositivos HID Boot UHCI simulados.
  A fixture cobre teclados e mouses, parsing de relatorios, rollover,
  duplicidade, eventos de entrada, overflow, timeout, falhas de controle e
  interrupt, reconfiguracao, remocao, filtros de candidatos e capacidade.
  A evidencia instrumentada resolveu as 24 superficies de
  `src/drivers/usb_hid.c`, sem enderecos desconhecidos ou ambiguos. O catalogo
  foi sincronizado com 7.196 superficies, 4.182 `COVERED`, 3.014 `PENDING` e
  90 casos. Foram executados `make q3check`, `make clean`, `make`,
  `make test-usb-hid-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos passaram. Os 29 testes unitarios dos runners
  tambem passaram. `make test-tst7-quick HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe`
  confirmou todas as suites host-only e permaneceu `BLOCKED` somente em
  `test-tst3-sanitize` pela indisponibilidade/permissao do runtime LLVM. O
  fechamento integral, o gate estrito e a validacao TST7 completa continuam
  pendentes.

- Incremento Drivers/usb_msc concluido em 2026-09-02: foi criado o caso
  host-only `host:drivers:usb-msc` com transporte BOT/SCSI e registro de
  bloco somente leitura simulados. A fixture exercitou inquiry, TUR,
  capacity, READ10, leituras de bloco, filtros de candidatos, limites,
  retry, reset recovery, CSW corrompido, falhas de controle e registro,
  indisponibilidade e recuperacao. A evidencia instrumentada resolveu as 25
  superficies pendentes de `src/drivers/usb_msc.c`, sem enderecos
  desconhecidos ou ambiguos. O catalogo foi sincronizado e validado com
  7.196 superficies, 4.206 `COVERED`, 2.990 `PENDING` e 91 casos. Foram
  executados `make q3check`, `make clean`, `make`, `make test-usb-msc-host`,
  `make catalog-test` e os 30 testes unitarios dos runners; todos passaram.
  O `make test-tst7-quick` confirmou todas as suites host-only, com resultado
  geral `BLOCKED` somente em `test-tst3-sanitize` pela indisponibilidade/
  permissao do runtime LLVM. O fechamento integral, o gate estrito e a
  validacao TST7 completa continuam pendentes.

- Incremento Storage/devfs concluido em 2026-09-02: foi criado o caso
  host-only `host:storage:devfs` com ATA e speaker simulados, sem VFS ou
  hardware real. A fixture exercitou inicializacao idempotente, registro,
  listagem, lookup, permissoes, dispositivos null/zero, speaker, hda,
  leituras, seeks, ioctl, sincronizacao, caminhos indisponiveis e invariantes.
  O relatorio instrumentado terminou `PASS`, resolveu as seis superficies
  pendentes de `src/fs/devfs.c` e nao registrou enderecos desconhecidos ou
  ambiguos. O catalogo foi sincronizado e validado com 7.196 superficies,
  4.212 `COVERED`, 2.984 `PENDING` e 92 casos. O fechamento integral, o gate
  estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/usb_manager concluido em 2026-09-02: foi criado o caso
  host-only `host:core:usb-manager` com fixtures estaticos de PCI, UHCI, EHCI,
  MSC e HID. A fixture exercitou inventario de controladores, estados de
  portas e dispositivos, identificadores, agregacoes, polling, refresh,
  formatacao, limites, falhas de driver, indisponibilidade e recuperacao. O
  relatorio instrumentado resolveu as 41 superficies de
  `src/core/usb_manager.c`, sem enderecos desconhecidos ou ambiguos. A
  sincronizacao registra 7.196 superficies, 4.164 `COVERED`, 3.032 `PENDING`
  e 89 casos; o fechamento integral, o gate estrito e a validacao TST7 completa
  continuam pendentes.

- Incremento Core/network_manager concluido em 2026-09-02: o caso existente
  `host:core:network-manager` passou a usar NIC PCI e USB simuladas e a
  exercitar configuracao estatica, validacao de parametros e rotas, DHCP,
  aplicacao e remocao de lease, clientes remotos, restauracao atomica apos
  erro e limpeza de IPv4/ARP/DNS. O relatorio instrumentado
  `build/test-results/network-manager-host/coverage.json` terminou `PASS`,
  observou 89 enderecos sem desconhecidos ou ambiguos e resolveu todas as
  superficies de `src/core/network_manager.c`. O catalogo registra 7.196
  superficies, 4.103 `COVERED` e 3.093 `PENDING`; o fechamento integral, o
  gate estrito e a validacao TST7 completa continuam pendentes.

- Incremento Core/app_builtin concluido em 2026-09-02: foi criado o caso
  host-only `host:core:app-builtin` com loader falso, validando cabecalhos,
  limites e entradas das imagens ZAPP de Echo, ArgTest, Uptime, Mem, PathTest,
  DevTest e OutputTest. A fixture tambem cobriu pre-condicoes do loader,
  propagacao de erros, codigo reservado de cancelamento e saidas nulas. A
  execucao instrumentada terminou `PASS` e resolveu 61 superficies reais,
  incluindo todas as funcoes de `src/core/app_builtin.c`. Foram executados
  `make test-tst7-quick HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos os testes host-only, incluindo o novo caso,
  passaram; `tst3-sanitize` permaneceu `BLOCKED` pela permissao do runtime
  LLVM. O catalogo registra 7.196 superficies, 4.002 `COVERED`, 3.194
  `PENDING` e 87 casos. O fechamento integral, o gate estrito e a validacao
  TST7 completa continuam pendentes.

- Incremento Core/app_files concluido em 2026-09-02: foi criado o caso
  host-only `host:core:app-files` com VFS falsa, cobrindo pre-condicoes antes
  da inicializacao, inicializacao idempotente, encaminhamento das operacoes de
  arquivo, validacao de saidas, limites e propagacao de erros canonicos. A
  fixture instrumentada terminou `PASS` e resolveu 33 superficies reais no
  relatorio dinamico, incluindo todas as funcoes de `src/core/app_files.c`.
  Foram executados `make q3check`, `make clean`, `make`,
  `make test-app-files-host HOST_CC=C:\\msys64\\ucrt64\\bin\\gcc.exe` e
  `make catalog-test`; todos terminaram com sucesso. O catalogo registra
  7.196 superficies, 3.953 `COVERED`, 3.243 `PENDING` e 86 casos. O fechamento
  integral, o gate estrito e a validacao TST7 completa continuam pendentes.
- Incremento Shell/commands-core concluído em 2026-09-04 10:54
  (America/Sao_Paulo). Foi adicionada uma fixture host-only que executa os
  handlers reais de `src/shell/shell_commands_core.c`, com caminhos válidos e
  negativos e dependências de hardware substituídas por doubles estáticos. O
  caso `host:shell:commands-core` está no catálogo e no allowlist do TST7. A
  evidência dinâmica terminou `PASS`; `make q3check`, build limpo,
  `make test-shell-commands-core-host`, os testes unitários dos runners e
  `make catalog-test` passaram. O catálogo atual registra 7.219 superfícies,
  5.240 `COVERED`, 1.979 `PENDING` e 138 casos. O fechamento integral ainda
  depende das 1.979 superfícies pendentes restantes.
- Correção incremental de Shell/commands-core concluída em 2026-09-04 11:00
  (America/Sao_Paulo). O caminho de falha do loader de aplicativo migrado foi
  exercitado com resultado `start_failed` e limpeza observável. A evidência
  dinâmica resolveu `shell_report_builtin_failure`; não restam superfícies
  `PENDING` nesse arquivo. O catálogo atual registra 7.219 superfícies, 5.241
  `COVERED`, 1.978 `PENDING` e 138 casos. O restante do fechamento integral
  continua pendente nos demais subsistemas.
- Evidência Shell/input regenerada em 2026-09-04 11:01
  (America/Sao_Paulo). O caso existente `host:shell:input` foi executado após o
  build limpo, e suas funções de inicialização e leitura do buffer ficaram
  novamente cobertas pelo relatório dinâmico real. `make test-shell-input-host`
  e `make catalog-test` passaram. O catálogo atual registra 7.219 superfícies,
  5.243 `COVERED`, 1.976 `PENDING` e 138 casos; as pendências restantes não
  foram mascaradas.
- Evidência Shell/hosted regenerada em 2026-09-04 11:03
  (America/Sao_Paulo). O caso existente `host:shell:hosted` foi executado com
  a fixture host-only real e cobriu as três superfícies pendentes de
  `src/shell/shell_hosted.c`. `make test-shell-hosted-host` e
  `make catalog-test` passaram. O catálogo atual registra 7.219 superfícies,
  5.246 `COVERED`, 1.973 `PENDING` e 138 casos.
- Evidência Drivers/RTC regenerada em 2026-09-04 11:11
  (America/Sao_Paulo). O caso `host:drivers:rtc-status` foi executado com CMOS
  falso após o build limpo e confirmou as nove superfícies internas pendentes
  do driver por relatório dinâmico real. `make test-rtc-status-host` e
  `make catalog-test` passaram. O catálogo atual registra 7.219 superfícies,
  5.255 `COVERED`, 1.964 `PENDING` e 138 casos.
- Allowlist do TST7 quick corrigido em 2026-09-04 11:21
  (America/Sao_Paulo). O caso `host:shell:commands-core` foi incluído na suíte
  rápida e confirmado em execução real. O quick terminou `BLOCKED` somente no
  sanitizador LLVM por dependência de ambiente ausente; os demais casos e
  `q3check` passaram. A sincronização restaurou ainda o modo `integration` dos
  comandos cobertos pelo dispatcher. O baseline permaneceu inalterado.

- Incremento Shell/Wi-Fi concluido em 2026-09-04 11:32.
  A nova fixture `host:shell:wifi` executa os handlers reais de
  `src/shell/shell_commands_wifi.c` com inventario PCI/USB e radio simulados.
  Foram exercitados os fluxos de status, scan, conexao, formatacao de
  interfaces, argumentos invalidos e falhas controladas. O alvo host-only,
  os testes do runner e o catalogo passaram; a cobertura foi vinculada por
  evidencia instrumentada real. O catalogo registra 7.219 superficies,
  5.267 `COVERED`, 1.952 `PENDING` e 139 casos. A cobertura integral ainda
  depende das pendencias restantes.

- Incremento Process/runtime concluido em 2026-09-04 11:45.
  A fixture `host:process:runtime` foi ampliada com chamadas reais para
  bootstrap sem cache, inicio do scheduler em estado invalido, descarte apos
  falha de criacao e copia/cancelamento de espera ativa. A etapa passou o alvo
  host-only, `q3check`, build limpo, `make catalog-test` e os testes unitarios
  dos runners. Cinco superficies de `src/process/process.c` foram resolvidas
  por evidencia dinamica; as rotinas de stack dependentes de enderecos 32-bit
  continuam pendentes sem uma fixture adequada. O catalogo registra 7.219
  superficies, 5.272 `COVERED`, 1.947 `PENDING` e 139 casos.

- Incremento Storage/FAT32 concluido em 2026-09-04. A fixture
  `host:storage:storage-fat32` usa uma imagem FAT32 minima em memoria, com duas
  copias de FAT, sem hardware ou armazenamento persistente. O caso exercitou
  validacao de BPB/FSInfo e FATs, estados de volume invalido, montagem,
  cadeias de dois clusters, escrita/leitura, nomes longos, substituicao,
  remocao e writers transacionais com finish e abort. A evidencia instrumentada
  terminou `PASS` e removeu as 18 superficies reais de `src/fs/storage.c` que
  estavam pendentes. A etapa passou `make test-storage-fat32-host`, `q3check`,
  build limpo, a bateria host do TST7 e `make catalog-test`; o quick global ficou
  `BLOCKED` somente pelo runtime LLVM ausente no sanitizador. O catalogo agora
  registra 7.219 superficies, 5.290 `COVERED`, 1.929 `PENDING` e 140 casos.

- Incremento Core/update concluido em 2026-09-04 12:30. O caso
  `host:core:update` foi adicionado com buffers estaticos e doubles de crypto e
  filesystem. A fixture executa os helpers reais de `src/core/update.c` para
  validar registros U3/U4, headers ZUPD, paths, entradas, limites, corrupcao,
  resultados de acao e cancelamento. A evidencia instrumentada e
  `make test-update-host` passaram, assim como `make catalog-test`. O
  catalogo registra 7.231 superficies, 5.356 `COVERED`, 1.875 `PENDING` e 141
  casos. As operacoes transacionais completas ainda dependem de fixture
  integrada e permanecem pendentes.

- Incremento Core/update — contrato publico de indisponibilidade concluido em
  2026-09-04. A fixture `host:core:update` passou a chamar os contratos
  publicos de inicializacao, capacidades, status, versao, verificacao,
  aplicacao, rollback, sincronizacao e historico com filesystem ausente,
  confirmando os retornos canonicos sem declarar uma atualizacao como sucesso.
  A evidencia instrumentada resolveu 76 superficies reais de
  `src/core/update.c`. Passaram `q3check`, build limpo, `make test-update-host`
  com `C:\\msys64\\ucrt64\\bin\\gcc.exe` e `make catalog-test`. O catalogo
  registra 7.240 superficies, 5.417 `COVERED`, 1.823 `PENDING` e 142 casos;
  transacoes FAT12 com slots mutaveis continuam pendentes para fixture
  integrada.

- Incremento Core/update remote runtime concluido em 2026-09-04. A fixture
  host-only `host:core:update-remote-runtime` passou a executar os helpers e
  contratos publicos reais de `src/core/update_remote_runtime.c` com doubles
  estaticos, cobrindo serializacao, CRC, JSON, descriptors, selecao de origem,
  transporte, download, cache, abortamento e indisponibilidade. O relatorio
  instrumentado resolveu 64 superficies reais sem enderecos desconhecidos ou
  simbolos ambiguos. Passaram `make q3check`, build limpo, o alvo
  `make test-update-remote-runtime-host` com `HOST_CC` configurado,
  sincronizacao, renderizacao e `make catalog-test`. O catalogo registra 7.245
  superficies, 5.469 `COVERED`, 1.776 `PENDING` e 143 casos; o restante da
  cobertura integral continua pendente e nao foi mascarado.

- Incremento Core/update remote concluido em 2026-09-04. A fixture host-only
  `host:core:update-remote` executa os helpers e contratos publicos reais de
  `src/core/update_remote.c` com doubles estaticos, cobrindo manifestos,
  registros redundantes, cache, download, cancelamento, estados, erros e
  limites sem rede ou armazenamento reais. A evidencia instrumentada resolveu
  54 superficies do arquivo sem enderecos desconhecidos ou simbolos
  ambiguos. Passaram o alvo host-only com `HOST_CC`, sincronizacao,
  renderizacao, `make catalog-test`, o build limpo e `q3check`. O catalogo
  registra 7.251 superficies, 5.546 `COVERED`, 1.705 `PENDING` e 144 casos; o
  restante da cobertura integral continua pendente e nao foi mascarado.

- Incremento Core/update system slots concluido em 2026-09-04. A fixture
  host-only `host:core:update-system-slots` executa os helpers e contratos
  publicos reais de `src/core/update_system_slots.c` com filesystem, volume,
  crypto, armazenamento e estado de boot simulados em buffers estaticos. A
  evidencia instrumentada resolveu 56 superficies reais sem enderecos
  desconhecidos ou simbolos ambiguos. Passaram o alvo host-only com `HOST_CC`,
  sincronizacao, renderizacao, `make catalog-test`, o build limpo, `q3check` e
  os testes unitarios selecionados. O catalogo registra 7.252 superficies,
  5.616 `COVERED`, 1.636 `PENDING` e 145 casos; o restante da cobertura
  integral continua pendente e nao foi mascarado.

- Incremento Core/update remote system concluido em 2026-09-04. A fixture
  host-only `host:core:update-remote-system` executa os helpers e contratos
  publicos reais de `src/core/update_remote_system.c` com filesystem, volume,
  crypto, armazenamento e transporte simulados em buffers estaticos. A
  evidencia instrumentada resolveu 56 superficies reais sem enderecos
  desconhecidos ou simbolos ambiguos, cobrindo serializacao do controle,
  cache redundante, hash, verificacao, transferencia transacional, limpeza,
  estados e limites. Passaram o alvo host-only com `HOST_CC`, sincronizacao,
  renderizacao, `make catalog-test`, `q3check`, build limpo e os testes
  unitarios selecionados. O catalogo registra 7.253 superficies, 5.651
  `COVERED`, 1.602 `PENDING` e 146 casos; o restante da cobertura integral
  continua pendente e nao foi mascarado.

- Incremento Core/update remote GitHub concluido em 2026-09-04. A fixture
  host-only `host:core:update-remote-github` executa os helpers e contratos
  publicos reais de `src/core/update_remote_github.c` com respostas JSON, HTTP,
  crypto e cancelamento simulados em buffers estaticos. A evidencia
  instrumentada resolveu 44 superficies reais sem enderecos desconhecidos ou
  simbolos ambiguos, cobrindo parser JSON, assets, duplicidades, limites, URLs
  allowlisted, fingerprints, espera, cancelamento e status HTTP. Passaram o
  alvo host-only com `HOST_CC`, sincronizacao, renderizacao,
  `make catalog-test`, `q3check`, `make clean`, `make`, os testes unitarios de
  catalogo, runner host e TST7, e `git diff --check`. O catalogo registra
  7.254 superficies, 5.694 `COVERED`, 1.560 `PENDING` e 147 casos; o restante
  da cobertura integral continua pendente e nao foi mascarado.

- Incremento Core/update remote release concluido em 2026-09-04. A fixture
  host-only `host:core:update-remote-release` executa os helpers e contratos
  publicos reais de `src/core/update_remote_release.c` com descritor JSON,
  HTTP, crypto, canal remoto e consulta GitHub simulados em buffers estaticos.
  A evidencia instrumentada resolveu 35 superficies reais sem enderecos
  desconhecidos ou simbolos ambiguos, cobrindo version lock, tags, assets,
  hashes, URLs, truncamento, status HTTP, cancelamento, selecao por tag,
  pre-condicoes e contrato de download. Passaram o alvo host-only com
  `HOST_CC`, sincronizacao, renderizacao, `make catalog-test`, `q3check`,
  `make clean`, `make`, os testes unitarios de catalogo, runner host e TST7,
  e `git diff --check`. O catalogo registra 7.255 superficies, 5.729
  `COVERED`, 1.526 `PENDING` e 148 casos; o restante da cobertura integral
  continua pendente e nao foi mascarado.

- Incremento Core/update system concluido em 2026-09-04. A fixture host-only
  `host:core:update-system` executa os validadores e contratos publicos reais
  de `src/core/update_system.c` com crypto, HTTP, filesystem, processo e
  consulta GitHub simulados. O fluxo valido verifica uma imagem ZSYS completa,
  incluindo componentes, compatibilidade, hashes, assinatura e transferencia
  remota; os caminhos negativos cobrem nulos, limites, formatos invalidos e
  estados incompletos sem rede ou armazenamento reais. A evidencia
  `build/test-results/update-system-host/coverage.json` terminou `PASS`,
  resolveu 35 superficies reais e nao registrou enderecos desconhecidos ou
  simbolos ambiguos. Passaram o alvo host-only com `HOST_CC`, sincronizacao,
  renderizacao, `make catalog-test`, `q3check`, `make clean`, `make`, os testes
  unitarios de catalogo, runner host e TST7, e `git diff --check`. O catalogo
  registra 7.256 superficies, 5.767 `COVERED`, 1.489 `PENDING` e 149 casos; o
  restante da cobertura integral continua pendente e nao foi mascarado.

- Incremento Core/update transacional concluido em 2026-09-04. A fixture
  `host:core:update` passou a usar um filesystem FAT12 em memoria e exercita
  diretamente as transacoes U3/U4 de `src/core/update.c`: baseline, leitura e
  escrita redundante, apply seco e real, staging, backups, journal, commit,
  rollback, cancelamento, failpoints de substituicao, recuperacao no boot,
  historico corrompido e sincronizacao de estado. O relatorio instrumentado
  terminou `PASS`, sem enderecos desconhecidos ou simbolos ambiguos, e as
  superficies pendentes do arquivo foram zeradas. O catalogo registra 7.256
  superficies, 5.821 `COVERED`, 1.435 `PENDING` e 149 casos; o restante da
  cobertura integral continua pendente e nao foi mascarado.

### Incremento Core/spinlock — 2026-09-04

- [x] Fixture host-only criada para o contrato inline `spinlock_t`.
- [x] `spinlock_init`, `spinlock_acquire` e `spinlock_release` foram chamados
      diretamente e tiveram o estado livre/adquirido verificado.
- [x] Caso `host:core:spinlock` integrado ao runner, Makefile e catálogo com
      vínculos explícitos somente às três APIs exercitadas.
- [x] Passaram o alvo host-only, `make catalog-test`, `make q3check`, build
      limpo e os 164 testes Python unitários.
- [x] O catálogo registra 7.293 superfícies, 6.086 `COVERED`, 1.207
      `PENDING` e 152 casos; as pendências restantes continuam visíveis.

### Incremento Shell/storage commands — 2026-09-04

- [x] Fixture host-only criada para os dispatchers de `index` e `search`.
- [x] Foram exercitados argumentos nulos, desconhecidos, extras e vazios,
      além da indisponibilidade do índice, preservando o código canônico
      `ERR_UNAVAILABLE` (`9`) e as mensagens observáveis.
- [x] O caso `host:shell:commands-storage` foi integrado ao runner, Makefile
      e catálogo com evidência instrumentada e doubles estáticos para o
      índice, armazenamento, cache, bloco e VFS.
- [x] Passaram o alvo host-only com `HOST_CC`, a revalidação das fixtures
      host-only existentes, a sincronização e renderização do catálogo e
      `make catalog-test`; o gate estrito permanece pendente somente pelas
      superfícies ainda sem evidência específica.
- [x] O catálogo registra 7.293 superfícies, 6.095 `COVERED`, 1.198
      `PENDING` e 153 casos; as pendências restantes continuam visíveis.

### Incremento Shell/storage commands ampliado — 2026-09-04

- [x] A fixture `host:shell:commands-storage` foi ampliada para chamar os
      dispatchers reais de `blkstat`, `cachestat`, `cache`, `sync` e `storage`,
      além de `index` e `search`.
- [x] Foram exercitados estados válidos e indisponíveis de bloco, cache,
      durabilidade, storage e índice; formatos ATA/USB, volumes FAT12/FAT32,
      diagnóstico de IDs, limites de argumentos, resultados de busca e todos
      os caminhos observáveis do job cooperativo do índice.
- [x] A execução instrumentada passou com `HOST_CC`, sem hardware ou
      armazenamento real. A evidência foi sincronizada e a visão renderizada;
      `make catalog-test`, `make q3check`, `make clean` seguido de `make` e a
      reexecução do alvo host-only passaram.
- [x] O catálogo registra 7.293 superfícies, 6.128 `COVERED`, 1.165
      `PENDING` e 153 casos. As superfícies sem evidência específica continuam
      explícitas; o gate estrito integral permanece pendente.

### Incremento Shell/diagnostics CPU, page fault, VMA e scheduler — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar os
      dispatchers reais de `cpu usage`, `pagefault`, `vmamap` e `schedcheck`.
- [x] Foram exercitados percentuais e linha-base de CPU, estatísticas de page
      fault, mapas de código e stack de processo usuário, PID inexistente,
      processo não-usuário, VMA indisponível, argumentos inválidos e falha de
      invariantes do scheduler, usando somente doubles estáticos.
- [x] Passaram o alvo específico com `HOST_CC`, todos os alvos host que
      alimentam a cobertura, `make q3check`, `make clean` seguido de `make`,
      sincronização e renderização da cobertura, `make catalog-test` e
      `git diff --check`.
- [x] O catálogo registra 7.293 superfícies, 6.230 `COVERED`, 1.063
      `PENDING` e 154 casos. As superfícies sem evidência específica continuam
      explícitas; o gate estrito integral permanece pendente.

### Incremento Shell/diagnostics MemCheck — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar o
      dispatcher real de `memcheck` e o executor estruturado do diagnóstico.
- [x] Foram exercitados os seis indicadores de integridade, argumentos
      inválidos, processo ring 3 ou zumbi pendente, aplicação em foreground,
      falha de validação SLAB e limpeza dos blocos estáticos em sucesso e erro.
- [x] Passaram o alvo host-only com `HOST_CC`, o build limpo (`make clean`
      seguido de `make`), `make q3check`, `make catalog-test`, a sincronização
      e renderização da evidência, e `git diff --check`.
- [x] O catálogo registra 7.293 superfícies, 6.234 `COVERED`, 1.059
      `PENDING` e 154 casos. As superfícies sem evidência específica continuam
      explícitas; o gate estrito integral permanece pendente.

### Incremento Shell/diagnostics KMetrics — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar o
      dispatcher real de `kmetrics` com linha-base desde boot e após `reset`.
- [x] Foram exercitados deltas de PIT, scheduler, teclado, IPC, PMM, heap,
      paging user, paging boot e VESA, além de argumentos inválidos, paging
      boot indisponível e VESA sem backbuffer.
- [x] Passaram o alvo host-only com `HOST_CC`, `make catalog-test`, a
      sincronização e renderização da evidência e `git diff --check`.
- [x] O catálogo registra 7.293 superfícies, 6.241 `COVERED`, 1.052
      `PENDING` e 154 casos. As superfícies sem evidência específica continuam
      explícitas; o gate estrito integral permanece pendente.

### Incremento Shell/diagnostics device-scan — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar o
      fluxo integrado de `device-scan`.
- [x] Foram exercitados PCI, USB, storage, mounts, file index, inventário de
      dispositivos, rede, Wi-Fi, recovery, yields, overflow parcial,
      inicialização tardia, degradações opcionais, falha fatal, resultado nulo
      e argumentos inválidos com doubles estáticos.
- [x] Passaram o alvo host-only, `make q3check`, `make clean`, `make`, os 117
      alvos host-only, sincronização, renderização, `make catalog-test` e
      `git diff --check`. A evidência dinâmica cobriu três superfícies reais do
      dispatcher; `power` e `acpi` permanecem como o próximo incremento.
- [x] O catálogo registra 7.293 superfícies, 6.244 `COVERED`, 1.049
      `PENDING` e 154 casos. As pendências restantes continuam explícitas e o
      gate estrito integral permanece pendente.

### Incremento Shell/diagnostics power e ACPI — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar os
      dispatchers reais de `power` e `acpi` com snapshots estáticos.
- [x] Foram exercitados estados disponíveis, degradados e indisponíveis,
      capacidades, serviço, fase, quiescência, tabelas ACPI, MADT, falhas de
      consulta, listagem e argumentos inválidos, sem desligamento, reinício ou
      acesso a energia real.
- [x] Passaram o alvo host-only, `make q3check`, `make clean`, `make`, os 117
      alvos host-only, sincronização, renderização, `make catalog-test` e
      `git diff --check`. As superfícies cobertas são somente as identificadas
      pela evidência dinâmica da fixture; as demais permanecem pendentes.
- [x] O gate estrito integral permanece pendente até que todas as superfícies
      elegíveis tenham executor e evidência real.

### Incremento Shell/diagnostics sinais — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar os
      dispatchers reais de `sigtest` e `kill`.
- [x] Foram exercitados o resultado estruturado do autoteste de sinais, nomes
      e números de sinais, envio para processo de usuário, falha do destino,
      PID inexistente e argumentos inválidos, sem estado persistente.
- [x] `make q3check`, build limpo, matriz host-only completa, sincronização do
      catálogo e `make catalog-test` passaram; o catálogo ficou com 7.293
      superfícies, sendo 6.259 `COVERED` e 1.034 `PENDING`.

### Incremento Shell/diagnostics proccheck — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar o
      dispatcher real de `proccheck` e o `shell_introspection.c` real.
- [x] Foram exercitados `/proc`, `/sys`, atributos de dispositivos, processos,
      leitura por cursor, EOF, controles de log, permissões, caminhos ausentes
      e escrita somente leitura em um VFS estático, sem estado persistente.
- [x] O alvo específico passou e a sincronização da evidência dinâmica elevou
      o catálogo para 7.293 superfícies, com 6.267 `COVERED` e 1.026 `PENDING`;
      o gate de catálogo passou e as pendências restantes continuam explícitas.

### Incremento Shell/diagnostics sysfs — 2026-09-04

- [x] `devices -v` e `device-info` foram exercitados pelo caminho real de
      atributos sysfs usando a fixture VFS estática, incluindo vendor, device e
      class e a validação de fechamento dos handles.
- [x] O alvo específico passou e a sincronização da evidência dinâmica cobriu
      `cmd_sysfs_print_device` e `cmd_sysfs_read_snapshot`; o catálogo ficou
      com 7.293 superfícies, sendo 6.269 `COVERED` e 1.024 `PENDING`.
- [x] As pendências restantes continuam explícitas e não foram associadas por
      pertencerem apenas ao mesmo arquivo.

### Incremento Shell/diagnostics health — 2026-09-04

- [x] A fixture host-only `host:shell:diagnostics` foi ampliada para chamar o
      dispatcher real de `health`, `health summary` e `health check`.
- [x] Foram exercitados os caminhos de argumentos inválidos, resumo, verificação
      saudável e VFS indisponível, usando estado estático para componentes,
      memória, processos, rede, sockets, cache e recuperação.
- [x] O alvo específico passou após a correção da capacidade da fixture de
      dispositivos; `make q3check`, build limpo e build completo também passaram.
- [x] A matriz host-only completa passou em 115/115 alvos, a evidência dinâmica
      foi sincronizada e o catálogo ficou com 7.293 superfícies: 6.309
      `COVERED` e 984 `PENDING`. O gate e a visão renderizada passaram; as
      pendências restantes continuam explícitas.

### Incremento Shell/network-checks — 2026-09-04

- [x] Foi criado o caso host-only `host:shell:network-checks`, chamando a
      função real de validação de rede do Shell com uma interface PCI estática.
- [x] Foram exercitados o estado coerente, a rejeição de interface inconsistente
      e a propagação de `ERR_UNAVAILABLE`, sem hardware, driver ou rede reais.
- [x] O alvo específico, a sincronização/renderização e `make catalog-test`
      passaram; o catálogo ficou com 7.293 superfícies, sendo 6.319 `COVERED`
      e 974 `PENDING`. As pendências restantes continuam explícitas.

### Incremento Shell/checks internos — 2026-09-04

- [x] Foi criado o caso host-only `host:shell:checks` com um ponto de entrada
      interno compilado somente sob `ZEPHYROS_HOST_TEST`; o build normal e a
      ABI pública permanecem inalterados.
- [x] A fixture chamou helpers reais de `shell_checks.c` para nomes de fase,
      resumo compacto, saturação do limite de falhas, estados de job,
      comparação de inventários, tabelas ACPI/MADT e emissão de bytes das
      fixtures ZAPP.
- [x] Passaram o alvo específico, `make q3check`, `make clean`, `make`, a
      matriz host-only completa com 117/117 alvos, sincronização/renderização
      do catálogo, `make catalog-test` e `git diff --check`.
- [x] A evidência dinâmica cobriu 31 símbolos reais de
      `src/shell/shell_checks.c`; o catálogo registra 7.295 superfícies, sendo
      6.351 `COVERED` e 944 `PENDING`. As pendências restantes continuam
      explícitas e não foram associadas por pertencerem apenas ao mesmo
      arquivo.

### Incremento Shell/network helpers — 2026-09-04

- [x] A fixture host-only `host:shell:network-checks` foi ampliada com um
      contrato interno compilado somente sob `ZEPHYROS_HOST_TEST`, chamando
      diretamente os helpers reais de `shell_commands_network.c`.
- [x] Foram exercitados parsers de IPv4, portas, ping e URLs, limites,
      argumentos nulos, destinos Ethernet, estados de interface, comparação
      de identificadores, validação de gateway/interface, conversão de ticks
      e estados/fases do job cooperativo.
- [x] Passaram `make test-shell-network-checks-host` com `HOST_CC`, a matriz
      host-only completa com 117/117 alvos, `make q3check`, `make clean`
      seguido de `make`, sincronização/renderização do catálogo, `make
      catalog-test` e `git diff --check`.
- [x] A evidência dinâmica cobriu 30 superfícies reais, sem endereços
      desconhecidos ou símbolos ambíguos. O catálogo registra 7.297
      superfícies, 6.368 `COVERED`, 929 `PENDING` e 156 casos; as pendências
      restantes continuam explícitas.
