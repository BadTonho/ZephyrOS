# 02 - Arquitetura do Sistema

## Visão Geral

O ZephyrOS é organizado em camadas, cada uma responsável por uma parte específica do sistema.

```
┌─────────────────────────────────────┐
│     Shell + Apps (zephyr>)          │  ← Interface com o usuário
│  (editor, mediaplayer, taskmgr)     │
├─────────────────────────────────────┤
│     Desktop / WM / Taskbar          │  ← Ambiente visual
│     (desktop, wm, taskbar,          │
│      settings, filemanager, icons)  │
├─────────────────────────────────────┤
│         Sistema de Arquivos         │  ← FAT12, FAT32, BMP, WAV
├─────────────────────────────────────┤
│        Processos / Threads          │  ← Scheduler, context switch
├─────────────────────────────────────┤
│           Gerenciamento             │  ← Memória, paging, heap
│              de Memória             │
│           + Compressão LZSS         │
├─────────────────────────────────────┤
│           Drivers de Hardware       │  ← Teclado, timer, vídeo, disco
│    (ATA, AC97, PCI, VESA, font)    │
├─────────────────────────────────────┤
│           IDT / IRQ / ISR           │  ← Interrupções e exceções
├─────────────────────────────────────┤
│     Kernel Core (log, panic)        │  ← Entry point, logging
├─────────────────────────────────────┤
│           Bootloader                │  ← Assembly, switch de modo
└─────────────────────────────────────┘
```

## Fluxo de Execução

```
BIOS → Boot.asm → Protected Mode → kernel_main() → Shell
```

1. **BIOS** carrega o boot sector (512 bytes) em `0x7C00`
2. **Bootloader** detecta memória, carrega kernel, muda para 32-bit
3. **Kernel** inicializa todos os subsistemas em ordem
4. **Shell** aguarda input do usuário

## Ordem de Inicialização

```
vesa_init()         → Modo gráfico VESA configurado
font_init()         → Fonte bitmap carregada
video_init()        → Tela pronta para mostrar mensagens
log_init()          → Sistema de logging ativo
idt_init()          → Interrupções funcionando
keyboard_init()     → Teclado respondendo
timer_init(50)      → Timer a 50 Hz
memory_init()       → Memória detectada e alocável
paging_init()       → Paginação ativa
tss_init()          → Kernel stack configurado
process_init()      → Gerenciador de processos pronto
thread_init()       → Gerenciador de threads pronto
ata_init()          → Disco detectado
fs_init()           → Sistema de arquivos montado (FAT12/FAT32)
speaker_init()      → PC Speaker pronto
ac97_init()         → Driver de áudio ativo
icons_init()        → Registro de ícones carregado
taskbar_init()      → Barra de tarefas desenhada
desktop_init()      → Desktop com ícones
settings_init()     → Configurações carregadas
wm_init()           → Window Manager ativo
shell_init()        → Shell aguardando input
```

## Estrutura de Arquivos

```
src/
├── boot/           → Bootloader (Assembly puro)
├── kernel/         → Código central do SO (entry, panic, switch)
├── core/           → Serviços centrais (log)
├── drivers/        → Drivers de hardware (video, vesa, font, idt, isr, irq, keyboard, timer, tss, ata, speaker, pci, ac97)
├── memory/         → Gerenciamento de memória (memory, paging, compress)
├── fs/             → Sistema de arquivos (fat12, fat32, fs, wav, bmp)
├── process/        → Gerenciador de processos
├── thread/         → Gerenciador de threads
├── shell/          → Apps do shell (editor, mediaplayer, taskmanager)
├── filemanager/    → File Manager
├── taskbar/        → Barra de tarefas
├── desktop/        → Ambiente desktop
├── settings/       → Configurações do sistema
├── wm/             → Window Manager
├── icons/          → Sistema de ícones
└── include/        → Headers organizados por módulo
```

## Convenções de Código

- **Nomes**: `snake_case` para funções e variáveis
- **Structs**: `snake_case_t` (typedef)
- **Headers**: `#ifndef HEADER_H` para include guards
- **Kernel**: Tudo `freestanding` (sem libc, sem stdlib)
- **Assembly**: Sintaxe NASM, Intel syntax
- **Logging**: `LOG_INFO`, `LOG_ERROR`, `LOG_WARN`, `LOG_DEBUG` via `core/log.h`
- **Erros**: Retornam códigos (`OK`, `ERR_NULL`, `ERR_MEM`, `ERR_DISK`, `ERR_NOT_FOUND`)
