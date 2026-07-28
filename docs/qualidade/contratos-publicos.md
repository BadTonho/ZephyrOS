# Catalogo de contratos publicos

Este catalogo associa cada header em `src/include/` ao documento tecnico que
descreve seu contrato. Quando um desses headers mudar, o `make q3check` exige
que o documento correspondente seja atualizado no mesmo conjunto de mudancas.

| Header publico | Documento canonico |
|---|---|
| `src/include/apps/editor.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/guitest.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/mediaplayer.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/apps/shell.h` | `docs/09-shell/shell.md` |
| `src/include/apps/taskmanager.h` | `docs/13-aplicativos/aplicativos.md` |
| `src/include/core/app_api.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_builtin.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_files.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_loader.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/app_package.h` | `docs/13-aplicativos/pacotes.md` |
| `src/include/core/device_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/errors.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/ethernet.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/keyboard.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/log.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/memory.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/network_manager.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/panic.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/power.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/recovery.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/spinlock.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/string.h` | `docs/04-kernel/kernel.md` |
| `src/include/core/syscall.h` | `docs/melhorias futuras/api de aplicativos e syscalls.md` |
| `src/include/core/timer.h` | `docs/05-drivers/drivers.md` |
| `src/include/core/video.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/acpi.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/ac97.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/ata.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/e1000.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/font.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/idt.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/mouse.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/pci.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/speaker.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/tss.h` | `docs/05-drivers/drivers.md` |
| `src/include/drivers/vesa.h` | `docs/05-drivers/drivers.md` |
| `src/include/fs/bmp.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat12.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fat32.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/fs.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/fs/wav.h` | `docs/08-sistema-arquivos/sistema-arquivos.md` |
| `src/include/memory/compress.h` | `docs/06-memoria/memoria.md` |
| `src/include/memory/paging.h` | `docs/06-memoria/memoria.md` |
| `src/include/process/process.h` | `docs/07-processos/processos.md` |
| `src/include/process/thread.h` | `docs/07-processos/processos.md` |
| `src/include/types.h` | `docs/02-arquitetura/arquitetura.md` |
| `src/include/ui/desktop.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/filemanager.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/gui.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/icons.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/settings.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/taskbar.h` | `docs/12-desktop/desktop.md` |
| `src/include/ui/wm.h` | `docs/12-desktop/desktop.md` |

O contrato de `src/include/process/process.h` inclui, desde a K2, os
contadores de motivo do scheduler, o quantum fixo de usuario e a validacao de
invariantes descritos em `docs/07-processos/processos.md`.

Os contratos de `src/include/core/memory.h` e `src/include/memory/paging.h`
incluem, desde a K3, estatisticas seguras do heap/PMM e do ciclo de vida de
diretorios de usuario. Seus detalhes tecnicos permanecem em
`docs/04-kernel/kernel.md` e `docs/06-memoria/memoria.md`, respectivamente.

Desde a S1.4, `src/include/drivers/acpi.h` inclui os indicadores
`mode_enable_available` e `s5_transition_ready`, alem da operacao terminal
`acpi_enter_s5()`. `src/include/core/power.h` expoe os indicadores derivados e
centraliza todos os caminhos de desligamento em `power_shutdown()`. Os
contratos canonicos permanecem, respectivamente, em
`docs/05-drivers/drivers.md` e `docs/04-kernel/kernel.md`.

Desde a S2.3, `src/include/core/network_manager.h` preserva o snapshot PCI e
os IDs estaveis de rede, acrescentando a disponibilidade e o diagnostico da
camada Ethernet. `src/include/core/ethernet.h` define a abstracao minima de
interface, montagem, polling e contadores L2. `src/include/drivers/e1000.h`
limita o driver ao Intel `8086:100E`, expoe uma fila RX fixa e mantem consultas
por copia. Os contratos canonicos permanecem em `docs/04-kernel/kernel.md` e
`docs/05-drivers/drivers.md`.
