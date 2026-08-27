# Roadmaps executaveis do ZephyrOS

Este diretorio separa o planejamento do ZephyrOS por frentes de trabalho. O
arquivo [ROADMAP.md](../../ROADMAP.md) continua sendo o mapa geral do projeto;
os documentos daqui definem ordem, limites e criterios de saida de cada frente.

## Estado de referencia

- Base original (boot, memoria, processos, filesystem, Shell e desktop):
  validada no QEMU.
- Plataforma de aplicativos: Fases 1 a 7 validadas, incluindo processos ring
  3, syscalls, loader ZAPP, foco, teclado, argumentos simples, as migracoes
  internas de `echo`, `uptime` e `mem`, e o contrato de console e ciclo de
  vida validado por `app outputtest [fail]`. A Fase 7 validou `ZPKG` v1,
  empacotador host, alias FAT12 `.ZPK`, servico `PKG` e comandos `pkg` no
  fluxo completo de instalacao, execucao e remocao.
- Estabilizacao e qualidade: Q1 e Q2 validados no QEMU, com matriz de
  regressao, retorno de foco, prompt unico, referencia para os resultados do
  `appcheck`, politica de logs, resumo seguro de falhas isoladas e o atalho
  compacto `q2check`; Q3 validado com o gate `make q3check`, seus auto-testes,
  build e smoke test da matriz de regressao no QEMU. Q4 foi validada com
  `regcheck`, cancelamento real por `F12`, `procs`, `health`, `schedcheck` e
  `memcheck`, sem substituir a matriz detalhada.
- Kernel e desempenho: K1 foi validada no QEMU com `kmetrics`, linha-base
  manual de PIT, scheduler, filas, memoria e VESA nos cenarios de Shell, ring
  3 e interfaces Simple/Classic. CPU real foi adiada; `TCK%` identifica
  somente a estimativa baseada em ticks. K2 foi validada no QEMU com
  `schedcheck`, preempcao de ring 3, testes de regressao e interfaces
  Simple/Classic; permanece o round-robin de 1 tick para ring 3. K3 foi
  validada no QEMU com estatisticas e guardas de heap/PMM, registro seguro de
  diretorios de usuario e o diagnostico compacto `memcheck`, inclusive apos
  ciclos ring 3 e nas interfaces Simple/Classic. K4 foi validada no QEMU:
  a copia VESA do cursor por regioes minimas reduziu bytes no cenario manual,
  preservando foco, Shell e ausencia de artefatos visuais.
- Interface: UI1 a UI7 validadas. Desktop, taskbar, Window Manager, Shell,
  Explorer, Task Manager e Settings possuem modo Classic e fallback Simple;
  janelas e icones aceitam interacao direta, os BMPs usam cache com fallback
  e a roda PS/2 e os atalhos de acessibilidade estao integrados.
- Sistema e ecossistema: S1.1 concluida com inventario de dispositivos somente
  de leitura e diagnostico de energia sem ACPI, validados manualmente no QEMU
  com os fallbacks e a matriz de regressao preservados. S1.2 concluiu a
  descoberta ACPI somente de leitura, validada no QEMU padrao e sem ACPI.
  S1.3 concluiu a observacao de PM1, modo ACPI e `_S5_`, validada no QEMU
  padrao e sem ACPI. S1.4 concluiu o desligamento fisico S5 por PM1, com
  aquisicao tardia do modo ACPI e fallback HLT; diagnosticos, regressao,
  sintaxe invalida e encerramento fisico foram validados no QEMU padrao.
  O cenario sem ACPI e os atalhos Simple/Classic permanecem como cobertura
  complementar. S2.1 concluiu o inventario PCI de rede somente de leitura,
  health e comandos nativos; `regcheck full` foi validado no QEMU padrao e
  sem NIC. S2.2 concluiu e validou o E1000 L2. A S2.3 concluiu fila RX,
  camada Ethernet e diagnostico fora da IRQ, validados com estado ocioso, TX,
  `device-scan` e `regcheck full`; RX externo permanece como cobertura
  complementar. S2.4 a S2.7 concluiram ARP, IPv4/ICMP, UDP/DHCP/DNS e
  TCP/sockets/HTTP; as suites agrupadas e o `regcheck full` foram validados
  no QEMU com E1000, incluindo recuperacao de timeout HTTP. A S2.8 concluiu
  Multi-NIC e RTL8139, com a suite Multi-NIC e o `regcheck full` aprovados no
  QEMU pelo usuario nos modos Simple e Classic. A U1 concluiu a politica de
  integridade, o contrato ZUPD v1 e quatro vetores publicos validados. A U2
  concluiu o verificador local, raiz publica, comando Shell, `health` e sete
  fixtures, com build, matriz QEMU, memoria, imagem inalterada e regressao
  aprovados pelo usuario. A U3 concluiu aplicacao/rollback FAT12, recuperacao,
  fixture, failpoint e auditoria offline; os cenarios QEMU terminaram com
  `regcheck full` em `OK` e journal limpo. A U4 concluiu diagnosticos
  persistentes e System Updater Simple/Classic, incluindo recuperacao por
  failpoint, quatro eventos e auditoria final limpa. A U5 concluiu o transporte
  HTTP manual, manifesto assinado, cache redundante e aba Remoto. Fixtures,
  comandos, System Updater Classic, falhas controladas, cancelamento,
  aplicacao/rollback e auditoria final foram aprovados. O Simple continua
  disponivel como fallback e sua regressao e cobertura complementar.
  O AS1 do roadmap da App Store esta validado no host e no QEMU: catalogo com
  seis fixtures, estados deterministas, ciclo `SAME_VERSION`, memoria estavel,
  recovery degradado e regressao completa passaram sem processos residuais.
  O ciclo de vida AS2 tambem esta validado no host e no QEMU: preflights sem
  escrita, confirmacao explicita, bloqueio de dependentes, execucao com `F12`,
  regressao completa e memoria estavel passaram sem residuos. Os roadmaps 06 e
  07 agora usam execucao intercalada para que a App Store Modern nao seja
  desenhada duas vezes: MV0-MV3, AS3, MV4 e, depois, AS4-AS5. O AS3 foi
  validado no host e no QEMU com janela singleton, worker cooperativo,
  fallback Simple, ciclo completo de pacotes e retorno de foco por `F12`. O
  AS4 tambem foi validado com planos topologicos, update/downgrade, rollback e
  recuperacao por failpoint. O AS5 foi validado com `ZAC1` assinado, cache
  FAT12 A/B, instalacao offline, recuperacao, Shell e aba Remoto Classic.
- Evolucao da plataforma: EP1 validada no QEMU com velocidade `1-10`,
  aceleracao, botao principal esquerdo/direito, estado bruto/efetivo e Settings
  Simple/Classic. A falha inicial da transacao compartilhada PS/2 preservou o
  fallback por teclado e foi corrigida protegendo a inicializacao contra a IRQ
  do teclado; MemCheck e RegCheck terminaram em `OK`.
  A EP2 tambem esta validada com quatro slots ATA, FAT12/FAT32 somente-leitura,
  fixtures e hashes intactos. A EP3 foi validada em QEMU com indice cooperativo
  em RAM, Shell, pesquisa no Explorer Classic, mutacoes com rebuild automatico,
  mount/unmount, cancelamento, fallback Simple, MemCheck e RegCheck em `OK`.
  A EP4.1 foi implementada com inventario USB somente-leitura via snapshot PCI,
  comandos `usb`, Recovery, `device-scan`, `regcheck full` e o alvo `run-usb`;
  UHCI e EHCI foram validados pelo usuario no QEMU. A EP4.2 implementa o
  primeiro runtime UHCI, enumeracao de portas raiz e controle USB; a etapa foi
  validada no QEMU com `usb-kbd`, portas vazias e fixture EHCI fora do escopo.
  A EP4.3 implementa Bulk sincrono, driver Mass Storage BOT/SCSI somente-leitura,
  registro em `block_device_t` e integracao com `storage mount`, validada no
  QEMU com `storage-valid.img` e regressao completa. A EP4.4 foi implementada
  com Interrupt IN UHCI, `input core`, fila de conclusoes diferidas e driver
  USB HID Boot para teclado e mouse; a validacao manual foi concluida.
  A EP5 foi implementada e validada no host com Releases, trava de versao
  assinada e tag auxiliar opcional. A EP6.0 foi implementada sobre fixtures
  HTTP, com selecao explicita por tag e cache U5, validada no QEMU; EP6.1 foi
  concluida e EP6.2 implementa BearSSL, HTTPS GitHub e preflight por tag; EP6.3
  cobre a matriz ampliada de falhas. A EP7.0 adiciona inventario PCI
  somente-leitura, comandos de diagnostico e validacao sem inicializar
  hardware; a EP9
  registra a futura imagem de sistema,
  slots A/B e recuperacao de boot, ainda sem autorizacao para alterar boot ou
  stage2.
- Funcionalidades aplicaveis: R1 foi validada no QEMU com log circular,
  diagnosticos e regressao. R2 e R3 foram validadas manualmente no QEMU, com
  timers, espera, rede, cancelamento, `q2check`, `regcheck full`, `memcheck`,
  `log check` e regressao Simple/Classic aprovados. A SYNC1 do Roadmap 12 foi
  implementada como primeiro pre-requisito de R4. Seus diagnosticos concluem
  em `OK`, mas o estresse de entrada durante `regcheck full` ainda causa
  lentidao e overflow PS/2. A SYNC1 foi concluida com essa divida tecnica
  aceita, registrada como
  [`DT100-001`](../qualidade/dividas-tecnicas-v1.0.0.md#dt100-001---regcheck-full-e-entrada-ps2),
  e a otimizacao foi transferida para K5/v1.0.0. R4/SYNC3 e a `kworker` nao
  foram iniciadas. A SYNC2 esta implementada sobre as filas FIFO da R3, com
  IPC e sockets bloqueantes e diagnosticos proprios. A matriz QEMU padrao e a
  correcao do cancelamento F11 e o perfil USB HID foram aprovados; a etapa
  permanece aberta para os perfis sem NIC e multi-NIC.

## K4 validada

A K4 compara a copia VESA do cursor por regioes minimas, sem abrir mudancas
de scheduler, memoria, App API ou bootloader. A comparacao antes/depois no
QEMU confirmou reducao de bytes e ausencia de rastro ou piscada apos a
correcao da ordem de apresentacao.

## Ordem sugerida

1. [01 - Estabilizacao e qualidade](01-estabilizacao-e-qualidade.md)
2. [02 - Plataforma de aplicativos](02-plataforma-de-aplicativos.md)
3. [03 - Kernel e desempenho](03-kernel-e-desempenho.md)
4. [04 - Interface e experiencia](04-interface-e-experiencia.md)
5. [05 - Sistema e ecossistema](05-sistema-e-ecossistema.md)
6. [06 - App Store](06-app-store.md)
7. [07 - Modernizacao Visual](07-modernizacao-visual.md)
8. [08 - Evolucao da Plataforma](08-evolucao-da-plataforma.md)
9. [09 - Funcionalidades aplicaveis](09-funcionalidades-aplicaveis.md)
10. [10 - VFS e Abstracao de I/O](10-vfs-e-abstracao-io.md)
11. [11 - Gerenciamento Avancado de Memoria](11-gerenciamento-avancado-de-memoria.md)
12. [12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md)
13. [13 - Armazenamento e Buffer Cache](13-armazenamento-e-buffer-cache.md)
14. [14 - Stack de Rede Avancada](14-stack-de-rede-avancada.md)
15. [15 - Introspeccao e Pseudo-Filesystems](15-introspeccao-e-pseudo-fs.md)
16. [16 - Energia e ACPI Avancado](16-energia-e-acpi-avancado.md)

Os numeros 06 e 07 mantem a organizacao documental, mas sua execucao e
intercalada nesta ordem:

1. AS1-AS2 do Roadmap 06.
2. MV0-MV3 do Roadmap 07.
3. AS3 do Roadmap 06.
4. MV4 do Roadmap 07, incluindo a App Store.
5. AS4-AS5 do Roadmap 06.

As frentes podem receber manutencao corretiva a qualquer momento. Para novas
funcionalidades, a prioridade e estabilizar a etapa atual antes de abrir uma
dependencia maior.

## Regra de conclusao de uma etapa

1. Implementar somente o escopo descrito no roadmap da frente.
2. Registrar comandos de diagnostico ou testes no Shell quando houver nova
   capacidade executavel.
3. Validar no QEMU com `make clean && make` e `make run` executados pelo
   usuario.
4. Atualizar este documento, o `ROADMAP.md`, o indice e a documentacao tecnica
   relacionada.
5. Revisar somente os arquivos modificados antes de criar um commit, conforme
   `AGENTS.md`.

## Documentos de apoio

Os documentos em `docs/melhorias futuras/` preservam especificacoes detalhadas
e ideias de longo prazo. Quando houver conflito entre uma especificacao antiga
e estes roadmaps, o roadmap da etapa ativa e o codigo validado sao a fonte de
verdade; a especificacao detalhada deve ser corrigida na mesma tarefa.
