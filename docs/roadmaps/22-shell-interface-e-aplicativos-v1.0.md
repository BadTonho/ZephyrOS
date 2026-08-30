# Roadmap 22 — Shell, interface e aplicativos básicos da 1.0.0

## Estado

Planejado. Esta frente fecha a experiência básica de uso do ZephyrOS depois
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

- [ ] Mapear cada comando, subcomando, argumento, retorno, job, cena e
  requisito de foco.
- [ ] Garantir que parsing inválido retorne uso correto, `LOG_WARN` e prompt.
- [ ] Garantir prompt único após sucesso, erro, cancelamento, timeout, crash de
  aplicativo, fechamento de cena e dispositivo ausente.
- [ ] Reproduzir e corrigir o caso em que uma execução deixa a tela vazia sem
  devolver `zephyr>`.
- [ ] Validar `F12`, `Ctrl+C`, histórico, edição, rolagem e reentrada sem
  descritores, jobs ou callbacks residuais.

### SHELL2 — Comandos básicos e diagnóstico

- [ ] Validar `help`, `clear`, `echo`, `mem`, `procs`, `threads`, `uptime`,
  `ls`, `cat`, `mount`, `devices`, `device-info` e `device-scan`.
- [ ] Validar `health`, `regcheck full`, `memcheck`, `schedcheck` e
  `proccheck` em sucesso, erro, cancelamento e ausência de hardware.
- [ ] Confirmar mensagens determinísticas e códigos canônicos sem logs
  duplicados.
- [ ] Garantir que diagnósticos somente leitura não alterem inventários,
  processos, volumes ou hardware.
- [ ] Exibir o estado do supervisor de serviços e permitir diagnóstico de
  serviços `STARTING`, `READY`, `FAILED` e `STOPPED`.
- [ ] Documentar comandos suportados, limites, fallbacks e exemplos de erro.

### SHELL3 — Arquivos e administração

- [ ] Confirmar navegação de diretórios, leitura, criação, rename, exclusão,
  mount, unmount, busca e índice global.
- [ ] Integrar abertura de arquivos, pipes, redirecionamento e `grep` sem
  deixar FDs ou jobs residuais.
- [ ] Exibir erros de permissão separadamente de caminho inexistente,
  filesystem indisponível ou dispositivo ausente.
- [ ] Validar Explorer Simple/Classic, teclado, mouse, seleção, confirmação,
  fallback e retorno ao Shell.
- [ ] Validar Task Manager com snapshots de `/proc`, ações por PID +
  generation e fallback Simple.
- [ ] Validar Settings, Desktop, WM e Taskbar sem apagar a tela de maneira
  universal nem perder o contexto da cena.

### SHELL4 — Aplicativos e pacotes

- [ ] Validar instalação, execução, remoção e rollback de pacotes locais.
- [ ] Validar catálogo, dependências, assinatura, versão, caminho e limite de
  arquivos antes de qualquer efeito persistente.
- [ ] Confirmar que aplicativo ring 3 com falha seja encerrado sem derrubar o
  Shell, o Desktop ou o kernel.
- [ ] Preservar Shell como fallback quando App Store, GUI ou rede estiverem
  indisponíveis.
- [ ] Validar atualização remota de aplicativos sem confundir seu estado com
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
