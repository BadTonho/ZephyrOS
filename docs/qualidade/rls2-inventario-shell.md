# RLS2 - Inventario verificavel do Shell

Este inventario descreve a superficie auditada pela RLS2. A fonte executavel
continua sendo a tabela unica de `src/shell/shell_dispatch.c`; este documento
serve para tornar a matriz e a cobertura revisaveis sem duplicar a politica do
dispatcher.

## Tabela de comandos

Os comandos abaixo sao agrupados pelas flags publicadas na tabela do
dispatcher. Um comando pode aparecer em mais de um grupo quando combina
flags.

### Pode bloquear

`ls`, `cat`, `grep`, `pipetest`, `threadtest`, `health`, `log`, `irqstat`,
`timer`, `wait`, `workq`, `sigtest`, `vfs`, `devcheck`, `proccheck`, `cd`,
`device-scan`, `usb`, `net`, `selecttest`, `ping`, `nslookup`, `http`,
`reboot`, `shutdown`, `poweroff`, `memcheck`, `slabtest`, `schedcheck`,
`q2check`, `regcheck`, `appcheck`, `blkcheck`, `pkg`, `store`, `update`,
`pkgcheck`, `app`, `usertest`, `updater`, `play`, `view`, `edit`, `storage`,
`sync`, `index` e `search`.

### Cooperativos

`net`, `ping`, `nslookup`, `http`, `q2check`, `regcheck`, `appcheck`,
`blkcheck`, `pkg`, `store`, `update`, `usertest` e `index` usam
`SHELL_DISPATCH_FLAG_COOPERATIVE` e devem permanecer cancelaveis pelo fluxo
de jobs do Shell.

### Abrem ou alteram uma cena

`app`, `desktop`, `guimode`, `display`, `explorer`, `guitest`, `taskmgr`,
`taskcfg`, `settings`, `updater`, `wm`, `play`, `view` e `edit` usam
`SHELL_DISPATCH_FLAG_OPENS_SCENE` ou abrem uma cena nativa por seu handler.

Os comandos restantes da tabela (`help`, `clear`, `echo`, `mem`, `procs`,
`stack`, `threads`, `uptime`, `clock`, `tls`, `wqinfo`, `skbstat`, `sockstat`,
`netstat`, `route`, `wifi`, `acpi`, `power`, `kmetrics`, `cpu`, `slabinfo`,
`pagefault`, `vmamap`, `icons`, `stop`, `compress`, `stats`, `blkstat`,
`cachestat`, `cache`, `mouse`, `job`, `kill` e `mount`) sao
observados como comandos sem cena ou como adaptadores de diagnostico, de
acordo com a flag efetiva na tabela fonte.

A lista integral atualmente publicada e: `job`, `help`, `clear`, `ls`, `cat`,
`echo`, `grep`, `pipetest`, `mem`, `procs`, `stack`, `threads`, `threadtest`,
`uptime`, `health`, `log`, `irqstat`, `timer`, `clock`, `tls`, `wait`,
`wqinfo`, `workq`, `kill`, `sigtest`, `vfs`, `devcheck`, `proccheck`, `mount`,
`pwd`, `cd`, `devices`, `device-info`, `device-scan`, `usb`, `net`, `skbstat`,
`sockstat`, `selecttest`, `netstat`, `route`, `wifi`, `ping`, `nslookup`,
`http`, `acpi`, `power`, `kmetrics`, `cpu`, `memcheck`, `slabinfo`, `slabtest`,
`pagefault`, `vmamap`, `schedcheck`, `q2check`, `regcheck`, `appcheck`,
`blkcheck`, `pkg`, `store`, `update`, `pkgcheck`, `app`, `usertest`, `beep`,
`melody`, `desktop`, `guimode`, `display`, `explorer`, `reboot`, `shutdown`,
`poweroff`, `guitest`, `taskmgr`, `taskcfg`, `settings`, `updater`, `wm`,
`play`, `view`, `icons`, `stop`, `compress`, `stats`, `edit`, `storage`,
`blkstat`, `cachestat`, `cache`, `sync`, `index`, `search` e `mouse`.

## Jobs e finalizacao

- `src/shell/shell_job.c`: ciclo generico `start -> running -> draining ->
  succeeded/failed/cancelled`, timeout, cancelamento por `Esc`/`F12`,
  `shell_job_pump_events()` e retorno ao dispatcher.
- `src/shell/shell_checks.c`: jobs de `q2check`, `regcheck`, `appcheck`,
  `blkcheck` e `usertest`, incluindo callbacks de loader e drenagem.
- `src/shell/shell_commands_core.c`: resultados de aplicativos builtin e
  retorno do loader.

Toda conclusao deve passar por `shell_runtime_finish_command()`. A geração
publicada pelo snapshot permite distinguir uma finalizacao repetida da
finalizacao da operacao ativa.

## Cenas, terminal e foco

As transicoes auditadas sao:

- `shell_handle_app_request()` para Shell, Desktop, Explorer, Task Manager,
  Settings, Updater e App Store;
- `shell_handle_key()` para WM, guitest, Task Manager, Settings, Updater,
  App Store e Desktop, incluindo fechamento por `Esc`;
- `shell_hosted_open()`, `wm_register_hosted_app()` e
  `wm_close_hosted_app()` para Classic;
- `shell_runtime_suspend_terminal_for_scene()`,
  `shell_runtime_resume_terminal()` e `shell_redraw_after_overlay_close()`;
- retorno de `app_loader`, foco de processos e eventos de taskbar.

O snapshot publica a camada atual como `dispatcher`, `job`, `scene`, `focus`,
`video`, `input` ou `loader`. Recursos residuais de cena, job, loader, foco e
entrada bloqueada devem estar zerados na amostra final.

## Caso automatizado

O caso `qemu:tst5:rls2-shell-liveness` executa as fases `boot`, `baseline`,
`commands`, `jobs`, `scenes`, `cancel` e `final` em nove sessoes. A faixa
`no-vesa/Classic` e explicitamente nao aplicavel; o fallback suportado e
`no-vesa/Simple`. A fonte declarativa da sequencia e o bloco
`rls2-shell-liveness-qemu` em `tests/coverage/registry.json`.

O estado de aceitacao permanece `PENDING` ate os gates e a matriz QEMU serem
executados para a mesma versao.
