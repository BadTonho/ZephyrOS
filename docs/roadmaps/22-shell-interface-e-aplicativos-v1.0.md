# Roadmap 22 — Shell, interface e aplicativos básicos da 1.0.0

## Estado

SHELL1, SHELL2, SHELL3 e SHELL4 concluidos e validados; SHELL5 permanece planejado.

Esta frente fecha a experiência básica de uso do ZephyrOS depois
que kernel, segurança, Storage e hardware estiverem com contratos estáveis.
Simple continua sendo fallback obrigatório; Classic é a interface principal
quando seus recursos estiverem disponíveis.

## Objetivo

Entregar um ambiente utilizável para abrir programas, navegar por arquivos,
consultar o sistema, administrar processos, configurar a máquina e retornar ao
prompt de forma determinística em todos os caminhos.

## Escopo

- dispatcher, parsing, histórico, jobs, cancelamento, foco e prompt do Shell;
- comandos básicos, diagnósticos, processos, arquivos, dispositivos, rede,
  energia e atualização;
- Explorer, Task Manager, Settings, Desktop, Window Manager e Taskbar;
- fallback Simple/Classic e saída VGA/VESA;
- loader, pacotes, instalação, execução, atualização e rollback de aplicativos;
- apresentação da atualização remota do sistema, enquanto o Roadmap 20 mantém
  o backend de staging, ativação e rollback;
- acessibilidade mínima por teclado, mouse e entrada USB suportada.

Não fazem parte da 1.0.0 Snap avançado, áreas de trabalho virtuais, TUI
completa dos gerenciadores opcionais, codecs avançados, Game Manager completo
ou ferramentas de programação.

## Dependências

- [Escopo da versão 1.0.0](escopo-v1.0.0.md);
- [Roadmap 18 — Kernel e processos](18-kernel-processos-e-userland-v1.0.md);
- [Roadmap 19 — ABI, segurança e permissões](19-abi-seguranca-e-permissoes-v1.0.md);
- [Roadmap 20 — VFS, Storage e atualização](20-vfs-storage-e-atualizacao-v1.0.md);
- [Roadmap 21 — Hardware, rede e energia](21-hardware-rede-e-energia-v1.0.md).

## Fases

### SHELL1 — Dispatcher e ciclo de vida

- [x] Mapear cada comando, subcomando, argumento, retorno, job, cena e
  requisito de foco.
- [x] Garantir que parsing inválido retorne uso correto, `LOG_WARN` e prompt.
- [x] Garantir prompt único após sucesso, erro, cancelamento, timeout, crash de
  aplicativo, fechamento de cena e dispositivo ausente.
- [x] Reproduzir e corrigir o caso em que uma execução deixa a tela vazia sem
  devolver `zephyr>`.
- [x] Validar `F12`, `Ctrl+C`, histórico, edição, rolagem e reentrada sem
  descritores, jobs ou callbacks residuais.

### SHELL2 — Comandos básicos e diagnóstico

- [x] Validar `help`, `clear`, `echo`, `mem`, `procs`, `threads`, `uptime`,
  `ls`, `cat`, `mount`, `devices`, `device-info` e `device-scan`.
- [x] Validar `health`, `regcheck full`, `memcheck`, `schedcheck` e
  `proccheck` em sucesso, erro, cancelamento e ausência de hardware.
- [x] Confirmar mensagens determinísticas e códigos canônicos sem logs
  duplicados.
- [x] Garantir que diagnósticos somente leitura não alterem inventários,
  processos, volumes ou hardware.
- [x] Exibir o estado do supervisor de serviços e permitir diagnóstico de
  serviços `STARTING`, `READY`, `FAILED` e `STOPPED`.
- [x] Documentar comandos suportados, limites, fallbacks e exemplos de erro.

### SHELL3 — Arquivos e administração

- [x] Confirmar navegação de diretórios, leitura, criação, rename, exclusão,
  mount, unmount, busca e índice global.
- [x] Integrar abertura de arquivos, pipes, redirecionamento e `grep` sem
  deixar FDs ou jobs residuais.
- [x] Exibir erros de permissão separadamente de caminho inexistente,
  filesystem indisponível ou dispositivo ausente.
- [x] Validar Explorer Simple/Classic, teclado, mouse, seleção, confirmação,
  fallback e retorno ao Shell.
- [x] Validar Task Manager com snapshots de `/proc`, ações por PID +
  generation e fallback Simple.
- [x] Validar Settings, Desktop, WM e Taskbar sem apagar a tela de maneira
  universal nem perder o contexto da cena.

### SHELL4 — Aplicativos e pacotes

- [x] Validar instalação, execução, remoção e rollback de pacotes locais.
- [x] Validar catálogo, dependências, assinatura, versão, caminho e limite de
  arquivos antes de qualquer efeito persistente.
- [x] Confirmar que aplicativo ring 3 com falha seja encerrado sem derrubar o
  Shell, o Desktop ou o kernel.
- [x] Preservar Shell como fallback quando App Store, GUI ou rede estiverem
  indisponíveis.
- [x] Validar atualização remota de aplicativos sem confundir seu estado com
  atualização do sistema operacional.

### SHELL5 — Atualização do sistema

- [ ] Expor no Shell e Settings o estado da atualização do sistema: versão
  atual, candidata, progresso, erro, tentativa de boot e confirmação de estado
  saudável.
- [ ] Permitir consulta e download remoto somente de manifesto e artefato
  autenticados.
- [ ] Não sobrescrever o sistema em execução; delegar staging, commit e
  recuperação ao contrato do Roadmap 20.
- [ ] Exibir explicitamente os estados `CHECK`, `DOWNLOAD`, `STAGE`, `PENDING`,
  `REBOOT`, `GOOD` e `ROLLBACK`, sem confundir aplicação com sistema.
- [ ] Exigir confirmação da ativação quando a política da atualização não for
  automática e manter o fallback offline.
- [ ] Garantir retorno ao prompt após falha de rede, falta de espaço,
  cancelamento ou reboot necessário.
- [ ] Validar que a versão anterior permaneça inicializável após falha de
  atualização.

### SHELL6 — Interface e compatibilidade de uso

- [ ] Validar Classic com VESA/backbuffer e Simple com VGA textual.
- [ ] Confirmar foco, teclado, mouse, USB HID, escalas, cores, mensagens e
  acessibilidade básica.
- [ ] Repetir abertura e fechamento dos aplicativos nativos sem vazamentos ou
  prompt ausente.
- [ ] Validar cenários sem VESA, mouse, áudio, USB, NIC, ACPI e Storage
  adicional.
- [ ] Registrar diferenças legítimas entre Simple e Classic sem duplicar a
  política de domínio.

## Contratos

- Novos comandos entram somente no dispatcher central e no módulo de domínio
  responsável.
- Handlers não armazenam ponteiros de processos, drivers ou VFS entre eventos.
- Operações demoradas usam jobs e cancelamento existentes; não bloqueiam o
  roteamento da entrada.
- A interface não acessa diretamente processos, drivers, volumes ou slots de
  atualização; usa os serviços e contratos de seus roadmaps proprietários.
- Shell, cenas e aplicativos liberam seus recursos em sucesso, erro e
  cancelamento.
- App API, syscalls, formatos de pacotes e contratos de erro só mudam por
  extensão versionada aprovada no Roadmap 19.

## Critérios de saída

- O usuário sempre recupera um prompt ou uma mensagem terminal observável
  após cada comando.
- Os aplicativos básicos abrem, funcionam, falham e fecham sem resíduos.
- Explorer, Task Manager, Settings, Desktop, WM e Taskbar têm fluxo básico em
  Classic e fallback Simple.
- Atualizações de aplicativos e do sistema preservam autenticação, rollback e
  recuperação.
- A experiência permanece utilizável nos perfis obrigatórios do Roadmap 21.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário deverá percorrer os
comandos e aplicativos desta frente na matriz Simple/Classic, incluindo o
cenário de prompt ausente, cancelamento, erro, atualização remota e retorno ao
Shell, registrando cada resultado.

### Validacao SHELL1

O conjunto deterministico da etapa e:

```text
make q3check
make clean
make
make test-shell1-host
make catalog-test
make test-shell1-qemu SHELL1_QEMU_WORKERS=4 SHELL1_QEMU_SEED=2201
```

A implementacao usa estado privado central (`HIDDEN`, `REQUESTED`, `VISIBLE` e
`BLOCKED`) e geracao de renderizacao para reconciliar o prompt de forma
idempotente. O caso dedicado `qemu:shell1:prompt-lifecycle` complementa as
regressoes de Shell, entrada, aplicativos e SEC6 Simple/Classic.

### Validacao SHELL2

O conjunto deterministico da etapa e:

```text
make q3check
make clean
make
make test-shell2-host
make catalog-test
make test-shell2-qemu SHELL2_QEMU_WORKERS=4 SHELL2_QEMU_SEED=2202
```

O caso dedicado `qemu:shell2:commands-diagnostics` cobre os comandos basicos,
erros, cancelamento, diagnosticos, reentrada e retorno ao prompt. Os casos
existentes de Shell, diagnosticos, SEC6 e perfis sem hardware sao reutilizados
pela tag `shell2`, sem alterar ABI, syscalls, `shell.h` ou codigos de erro.

Estado da etapa: concluida e validada. O agregado host passou, o catalogo foi
validado e a matriz QEMU passou 15/15 casos com 4 workers e seed 2202.

### Validacao SHELL3

O conjunto deterministico da etapa e:

```text
make q3check
make clean
make
make test-shell3-host
make catalog-test
make test-shell3-qemu SHELL3_QEMU_WORKERS=4 SHELL3_QEMU_SEED=2203
```

O caso dedicado `qemu:shell3:files-admin` cobre arquivos, VFS, pipelines,
redirecionamento, Storage, Explorer, Task Manager, Settings, Desktop, WM,
Taskbar, entradas invalidas, cancelamento e reentrada. As mutacoes do Explorer
ficam restritas ao snapshot descartavel; casos existentes sao reutilizados pela
tag `shell3` sem alterar `shell.h`, ABI, syscalls ou codigos de erro.

Estado da etapa: concluida e validada. O agregado host passou, o catalogo foi
validado e a matriz QEMU passou 15/15 casos com 4 workers e seed 2203.
Os comandos CLI `mkdir`, `rm`, `mv` e `cp` permanecem registrados como
`DT100-005`; as operacoes equivalentes continuam disponiveis pelo Explorer e
pelas APIs existentes de FS/VFS.

### Validacao SHELL4

O conjunto deterministico da etapa e:

```text
make q3check
make clean && make
make test-shell4-host
make catalog-test
make test-shell4-qemu SHELL4_QEMU_WORKERS=4 SHELL4_QEMU_SEED=2204
```

O caso dedicado `qemu:shell4:apps-packages` cobre verificacao, instalacao,
execucao, atualizacao, remocao, rollback, historico, catalogo, App Store,
falha controlada de aplicativo, pacotes nao confiaveis, cancelamento,
reentrada e retorno ao prompt. As mutacoes ficam restritas ao snapshot
descartavel. A confianca ZPKG v2 continua obrigatoria para instalar, atualizar
e executar; pacotes v1 permanecem disponiveis somente para inspecao e
remocao. A limitacao de fixtures remotas assinadas por chave privada externa
continua registrada como `DT100-003`.

A etapa foi concluida e validada em 2026-09-12. O worker da App Store passou a
usar stack interna de 16 KiB depois que a matriz reproduziu overflow de canario
durante o caso dedicado. O agregado host, o catalogo e a matriz QEMU passaram;
o run `qpp-20260912T200700Z-22188` concluiu 4/4 casos com 4 workers e seed
2204, sem timeout ou processo QEMU residual. `DT100-003`, `DT100-004` e
`DT100-005` permanecem separadas.
