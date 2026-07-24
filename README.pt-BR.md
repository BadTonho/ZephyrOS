# ZephyrOS

[ 🇺🇸 [English](README.md) | 🇧🇷 **Português** ]

Sistema operacional do zero em C + Assembly (x86), com o objetivo de ser um OS real e funcional.

---

## Status do projeto

O ZephyrOS é um sistema operacional experimental em desenvolvimento ativo. Ele é executado
principalmente no QEMU e mantém duas interfaces: modo clássico em VGA text mode e modo
moderno com VESA quando o hardware ou emulador oferece suporte.

O projeto ainda não deve ser considerado pronto para uso em hardware real. Consulte o
[`ROADMAP.md`](ROADMAP.md) e a documentação em [`docs/`](docs/) para acompanhar as etapas
concluídas, limitações conhecidas e próximos objetivos.

---

## Funcionalidades

| Módulo | Status | Descrição |
|--------|--------|-----------|
| Bootloader | ✅ | Assembly 16-bit → Protected Mode 32-bit |
| Kernel | ✅ | Entry point, panic handler, context switch |
| VGA Video | ✅ | Text mode 80x25, cores, cursor |
| VESA | ✅ | Modo gráfico, múltiplas resoluções (640x480 a 1920x1200) |
| Font | ✅ | Fonte bitmap 8x16 para renderização gráfica |
| Teclado | ✅ | Driver PS/2, scancode → ASCII |
| Timer | ✅ | PIT 50 Hz, ticks |
| IDT/IRQ/ISR | ✅ | 32 exceções + 16 IRQs mapeadas |
| Memória | ✅ | Detecção E820, bitmap allocator, heap |
| Paging | ✅ | Page directory/table, mapeamento virtual |
| Compress (RAM) | ✅ | Compressão LZSS para dados em memória |
| TSS | ✅ | Kernel stack ring 0 |
| Processos | ✅ | PID, estados, scheduler round-robin |
| Threads | ✅ | Create, block, yield, round-robin |
| ATA Driver | ✅ | Leitura/escrita de setores (PIO) |
| FAT12 | ✅ | Ler/escrever/deletar arquivos, listar diretório |
| FAT32 | ✅ | Suporte a discos maiores (BPB, clusters de 32 bits) |
| FS Unificado | ✅ | Interface única sobre FAT12/FAT32 |
| BMP | ✅ | Leitura e renderização de imagens BMP (1/4/8/24 bpp) |
| WAV | ✅ | Leitura e reprodução de áudio WAV |
| PCI | ✅ | Enumeração do barramento PCI |
| AC97 | ✅ | Driver de áudio AC97 (play, stop, volume) |
| PC Speaker | ✅ | Beep, melodias |
| Shell | ✅ | Terminal com scrollback, diagnósticos e comandos nativos/ring 3 |
| App API | ✅ | API 0.3, syscalls `int 0x80`, IPC, arquivos e loader ZAPP |
| Editor | ✅ | Editor de texto com syntax highlight, word wrap |
| Media Player | ✅ | Player de áudio WAV com visualização |
| Task Manager | ✅ | Monitor de processos/threads/CPU/memória |
| File Manager | ✅ | Explorer clássico e moderno (navegar, criar, renomear, excluir) |
| Desktop | ✅ | Ambiente desktop moderno com fallback clássico, ícones e menu Iniciar |
| Window Manager | ✅ | Gerenciador de janelas (mover, redimensionar, minimizar) |
| Taskbar | ✅ | Barra de tarefas configurável (posição, tamanho, relógio) |
| Settings | ✅ | Sistema de configurações (tela, taskbar, janelas, ícones, som) |
| Icons | ✅ | Sistema de ícones customizáveis (desktop, janelas, arquivos) |

---

## Estrutura do Projeto

```
Sistema/
├── Makefile                 # Sistema de build
├── ROADMAP.md               # Roadmap de desenvolvimento
├── build/                   # Arquivos de saída
├── docs/                    # Documentação (13 capítulos + roadmaps)
└── src/
    ├── linker.ld            # Linker script
    ├── boot/                # Bootloader e segundo estágio (Assembly)
    │   ├── boot.asm
    │   └── stage2.asm
    ├── core/                # Serviços centrais, App API, syscalls e loader
    ├── kernel/              # Kernel core
    │   ├── entry.asm        # Entry point Assembly
    │   ├── kernel.c         # Kernel principal (inicializa 20+ subsistemas)
    │   ├── panic.c          # Kernel panic
    │   └── switch.asm       # Context switch
    ├── drivers/             # Drivers de hardware (13 arquivos)
    │   ├── ata.c            # Driver ATA PIO (disco)
    │   ├── idt.c            # IDT + remapeamento PIC
    │   ├── irq.asm          # IRQ handlers
    │   ├── isr.asm          # ISR handlers (exceções)
    │   ├── keyboard.c       # Driver teclado PS/2
    │   ├── speaker.c        # PC Speaker (som)
    │   ├── timer.c          # Timer PIT
    │   ├── tss.c            # Task State Segment
    │   ├── video.c          # VGA Text Mode
    │   ├── vesa.c           # VESA BIOS Extensions (modo gráfico)
    │   ├── font.c           # Fonte bitmap 8x16
    │   ├── pci.c            # Enumeração PCI
    │   └── ac97.c           # Driver de áudio AC97
    ├── memory/              # Gerenciamento de memória
    │   ├── memory.c         # Bitmap allocator + heap
    │   ├── paging.c         # Page tables
    │   └── compress.c       # Compressão LZSS
    ├── fs/                  # Sistema de arquivos
    │   ├── fat12.c          # FAT12
    │   ├── fat32.c          # FAT32
    │   ├── fs.c             # Interface unificada FAT12/FAT32
    │   ├── bmp.c            # Leitura de imagens BMP
    │   └── wav.c            # Leitura de áudio WAV
    ├── process/             # Processos e IPC
    │   ├── process.c        # Process manager + scheduler
    │   └── ipc.c            # Filas, foco e mensagens entre processos
    ├── thread/              # Threads
    │   └── thread.c         # Thread scheduler
    ├── shell/               # Terminal e aplicativos
    │   ├── shell.c          # Shell interativo, diagnósticos e apps ZAPP
    │   ├── editor.c         # Editor de texto com syntax highlight
    │   ├── mediaplayer.c    # Media player (WAV)
    │   └── taskmanager.c    # Gerenciador de tarefas
    ├── filemanager/         # Gerenciador de arquivos
    │   └── filemanager.c    # Explorer clássico/moderno
    ├── desktop/             # Ambiente desktop
    │   └── desktop.c        # Desktop com ícones
    ├── wm/                  # Gerenciador de janelas
    │   └── wm.c             # Window manager (título, botões, borda)
    ├── taskbar/             # Barra de tarefas
    │   └── taskbar.c        # Taskbar com menu Iniciar e relógio
    ├── settings/            # Sistema de configurações
    │   └── settings.c       # Configurações (tela, taskbar, janelas, som)
    ├── icons/               # Sistema de ícones
    │   └── icons.c          # Ícones customizáveis
    └── include/             # Headers organizados por módulo
        ├── types.h, video.h, keyboard.h, idt.h, timer.h,
        ├── memory.h, paging.h, ata.h, fat12.h, fat32.h,
        ├── process.h, thread.h, shell.h, speaker.h, tss.h,
        ├── filemanager.h, panic.h, compress.h, editor.h,
        ├── mediaplayer.h, settings.h, wm.h, icons.h,
        ├── ac97.h, pci.h, bmp.h, wav.h, fs.h, font.h,
        ├── vesa.h, desktop.h, taskbar.h, taskmanager.h
```

---

## Serviços centrais atuais

Além dos diretórios mostrados no mapa resumido, `src/core/` concentra
`log`, `string`, `recovery`, `app_api`, `app_files`, `syscall`, `app_loader`
e imagens internas de aplicativos. `src/process/` contém tanto o scheduler
quanto `ipc.c`, responsável por filas e foco. Consulte
[Arquitetura](docs/02-arquitetura/arquitetura.md) para o mapa de dependências
atualizado.

## Requisitos

- **NASM** - Assembler (monta Assembly para binário/ELF)
- **GCC cross-compiler i686-elf** - Compilador C 32-bit freestanding
- **GNU ld** - Linker ELF 32-bit
- **QEMU** ou **Bochs** - Emulador para testar

### Instalação no Windows

```powershell
# NASM
winget install nasm

# QEMU
winget install qemu

# GCC cross-compiler (via WSL ou MinGW)
# Ou usar: i686-elf-gcc de https://github.com/lordmilko/i686-elf-gcc
```

### Instalação no Linux

```bash
# Ubuntu/Debian
sudo apt install nasm gcc make qemu-system-x86

# Para cross-compiler i686-elf (recomendado)
# Siga: https://wiki.osdev.org/GCC_Cross-Compiler
```

### Configuração das ferramentas

O `Makefile` procura `nasm`, `i686-elf-gcc`, `i686-elf-ld` e `qemu-system-i386` no `PATH`.
Também é possível definir caminhos específicos em um arquivo local chamado `Makefile.local`.
Esse arquivo é ignorado pelo Git e não deve ser enviado ao repositório.

Exemplo de `Makefile.local`:

```makefile
NASM = nasm
GCC = i686-elf-gcc
LD = i686-elf-ld
QEMU = qemu-system-i386
```

Ou, em uma execução isolada:

```powershell
make NASM=nasm GCC=i686-elf-gcc LD=i686-elf-ld QEMU=qemu-system-i386
```

---

## Como Compilar e Rodar

```bash
# Compilar tudo
make

# Compilar e rodar no QEMU
make run

# Compilar com debug (GDB)
make debug

# Limpar arquivos build
make clean
```

---

## Comandos do Shell

O serviço do Shell é inicializado durante o boot. No fluxo gráfico padrão, o
Desktop é exibido primeiro e o terminal é aberto pelo item **Shell** da
taskbar/Menu Iniciar ou pelo ícone do Desktop.

| Comando | Descrição | Exemplo |
|---------|-----------|---------|
| `help` | Lista todos os comandos | `help` |
| `clear` | Limpa a tela e o histórico do terminal | `clear` |
| `ls` | Lista arquivos no disco | `ls` |
| `cat` | Exibe conteúdo de arquivo | `cat ARQUIVO.TXT` |
| `echo` | Executa a primeira app ZAPP interna, com fallback nativo | `echo Ola Mundo` |
| `mem` | Mostra info de memória | `mem` |
| `procs` | Lista processos ativos | `procs` |
| `threads` | Lista threads ativas | `threads` |
| `uptime` | Tempo desde boot | `uptime` |
| `beep` | Toca um beep | `beep` ou `beep 440 500` |
| `melody` | Toca uma escala musical | `melody` |
| `explorer` | Abre gerenciador de arquivos | `explorer` |
| `desktop` | Abre ambiente desktop | `desktop` |
| `taskmgr` | Abre gerenciador de tarefas | `taskmgr` |
| `edit` | Editor de texto | `edit ARQUIVO.TXT` |
| `play` | Toca arquivo WAV | `play MUSICA.WAV` |
| `compress` | Gerencia compressão de RAM | `compress on/off/status` |
| `settings` | Abre configurações | `settings` |
| `health` | Mostra o estado dos componentes e do kernel | `health` |
| `appcheck` | Testa App API, syscalls, arquivos, IPC, loader e argumentos | `appcheck` |
| `app run` | Executa `.ZAP` ring 3 com argumentos simples | `app run DEMO.ZAP alpha beta` |
| `app inputtest` | Testa foco e teclado de app ring 3 | `app inputtest` |
| `app argtest` | Testa argumentos entregues a uma app ring 3 | `app argtest alpha beta` |
| `usertest` | Executa o processo de teste em modo usuário | `usertest` |
| `guimode` | Alterna entre `classic` e `modern` | `guimode modern` |
| `reboot` | Reinicia o sistema | `reboot` |
| `shutdown` | Desliga o sistema | `shutdown` |

> **Nota:** Para a lista completa de atalhos de teclado e comandos detalhados do sistema, consulte: [Atalhos e Comandos](docs/atalhos_e_comandos.md).

---

## Arquitetura

### Boot (16-bit → 32-bit)

1. BIOS carrega `boot.asm` em `0x7C00`
2. Detecta memória via BIOS int 0x15 (E820)
3. Carrega kernel de 30 setores para `0x1000`
4. Configura GDT (Global Descriptor Table)
5. Switch para Protected Mode (32-bit)
6. Passa mapa de memória em ESI para o kernel

### Kernel

1. Inicializa vídeo VGA text mode
2. Configura IDT (Interrupt Descriptor Table)
3. Remapeia PIC master/slave
4. Inicializa drivers (teclado, timer)
5. Detecta memória e cria bitmap allocator
6. Configura paging (page tables)
7. Inicializa TSS (Task State Segment)
8. Cria processos e threads
9. Detecta disco e monta FAT12/FAT32 (fs unificado)
10. Inicializa PC Speaker
11. Inicializa VESA (modo gráfico) e fontes
12. Inicializa AC97 (áudio)
13. Inicializa ícones, taskbar, desktop, configurações, window manager
14. Inicia shell interativo com desktop

### Memória

```
0x00000 - 0x7C00   Bootloader (reusado após boot)
0x7C00  - 0x8000   Boot sector
0x8000  - 0x10000  Mapa de memória (E820)
0x1000  - 0x20000  Kernel em disco
0x20000 - 0x120000 Heap (1 MB)
0x90000 - 0x9FFFF  Kernel stack
0xB8000 - 0xBFFFF  VGA video memory
```

### Processos

- **PID 1-64** - Processos do sistema
- **Estados**: UNUSED, READY, RUNNING, BLOCKED, ZOMBIE
- **Scheduler**: Round-robin preemptivo (via timer IRQ0)
- **Context switch**: Salva/restaura todos os registradores em Assembly
- **Modo usuário**: processos ZAPP executam em ring 3 com diretório de páginas isolado

### Plataforma de aplicativos

- App API pública `0.3`, sem expor endereços internos do kernel.
- Syscalls `0–9` no vetor `int 0x80`, com validação de ponteiros e tamanhos.
- Imagens flat i386 `ZAPP` carregadas de arquivos `.ZAP` ou de imagens internas.
- Argumentos simples em página de lançamento dedicada; até 8 argumentos e 511 caracteres.
- Foco de teclado exclusivo para a app externa; `F12` cancela somente a app em foco.
- `echo` é a primeira migração nativa e mantém fallback se o loader estiver indisponível.

---

## Módulos Detalhados

### Bootloader (`src/boot/boot.asm`)
- BPB (BIOS Parameter Block) para FAT12
- Detecção de memória E820
- Load do kernel via BIOS int 0x13
- Switch para Protected Mode

### IDT (`src/drivers/idt.c`)
- 32 ISRs para exceções do CPU (div by zero, page fault, etc.)
- 16 IRQs mapeadas para IDT 32-47
- Remapeamento PIC master (0x20) → 32, slave (0xA0) → 40

### VESA (`src/drivers/vesa.c`)
- Modo gráfico via VESA BIOS Extensions (VBE)
- Enumeração automática de modos (640x480 a 1920x1200, 32bpp)
- Primitivas: pixel, retângulo, linha, círculo, bitmap, texto com fonte

### PCI (`src/drivers/pci.c`)
- Enumeração do barramento PCI (256 buses × 32 devices × 8 functions)
- Leitura/escrita de configuração (BARs, IRQ, classe)
- Busca por vendor/device ID e classe/subclasse
- Bus Mastering enable

### AC97 (`src/drivers/ac97.c`)
- Driver de áudio via controladora AC97 encontrada no PCI
- Reset, power management, configuração de sample rate (44100 Hz)
- Play/Stop com buffer DMA, controle de volume (0-31)
- Handler de interrupção

### Memória (`src/memory/memory.c`)
- Bitmap allocator: 1 bit por página (4KB)
- Heap: first-fit com coalescência de blocos livres
- `kmalloc()`, `kfree()`, `kmalloc_aligned()`

### Compress (`src/memory/compress.c`)
- Compressão LZSS com dicionário deslizante
- `compress_data()` / `decompress_data()`
- Estatísticas de compressão (taxa, espaço economizado)
- Ativável/desativável via shell

### FAT12 (`src/fs/fat12.c`)
- Leitura do BPB (BIOS Parameter Block)
- Interpretação da FAT (File Allocation Table)
- Leitura/escrita de arquivos por cluster chain
- Listagem de diretório raiz

### FAT32 (`src/fs/fat32.c`)
- Suporte a discos com BPB FAT32 (sectors_per_fat > 0)
- Cluster chain de 32 bits (0x0FFFFFFF = EOF)
- Leitura/escrita/exclusão de arquivos
- Listagem de diretório com suporte a cluster chain

### FS Unificado (`src/fs/fs.c`)
- Interface única: `fs_read_file()`, `fs_write_file()`, `fs_delete_file()`, `fs_list_dir()`
- Detecta automaticamente FAT12 ou FAT32
- `fs_get_info()` retorna informações do sistema de arquivos ativo

### BMP (`src/fs/bmp.c`)
- Leitura de imagens BMP (1, 4, 8, 24 bpp)
- Renderização (`bmp_draw`) e redimensionamento (`bmp_draw_scaled`)
- Suporte a paleta de cores (bpp <= 8)

### WAV (`src/fs/wav.c`)
- Parse de arquivos WAV (RIFF/WAVE)
- Suporte a múltiplos formatos (sample rate, bits, canais)
- Reprodução via AC97 (`wav_play`)
- Cálculo de duração

### Desktop (`src/desktop/desktop.c`)
- Ambiente desktop com ícones (Shell, Explorer, TaskMgr)
- Navegação por setas e Enter para abrir apps
- Integração com taskbar

### Window Manager (`src/wm/wm.c`)
- Múltiplas janelas com foco, Z-order, título e botões
- Botões de fechar/minimizar/maximizar (posição e ordem configuráveis)
- Redimensionamento e movimentação
- Atalhos: F1=foco próximo, Esc=fechar, F5=minimizar, F6=max/min

### Taskbar (`src/taskbar/taskbar.c`)
- Barra de tarefas com botões de aplicativos e relógio (HH:MM)
- Menu Iniciar: Desktop, Shell, Explorer, TaskMgr, Config, Reiniciar, Desligar
- Menu de configuração (F1): posição, tamanho, fixar
- Posições: baixo, cima, esquerda, direita, custom

### Settings (`src/settings/settings.c`)
- Sistema completo de configurações com categorias: Tela, Taskbar, Janelas, Ícones, Sistema, Som, Sobre
- Editor visual de ícones (caractere, cor, cor de seleção)
- Aplicação em tempo real das configurações (taskbar, WM)

### Icons (`src/icons/icons.c`)
- Registry com 4 categorias: desktop, WM, file manager, taskbar
- Funções get/set para cada ícone
- `icons_reset_defaults()` restaura valores padrão

---

## Troubleshooting

### "Nenhum disco encontrado"
O QEMU precisa de uma imagem de disco. O Makefile já gera `build/zephyros.img`.

### "FAT12 não encontrado"
A imagem precisa ter partição FAT12. Para testar, crie uma imagem FAT12 separada ou use um floppy virtual.

### Kernel não inicia
Verifique se o cross-compiler está correto. Use `i686-elf-gcc` em vez de `gcc` padrão.

---

## Referências

- [OSDev Wiki](https://wiki.osdev.org)
- [Writing a Simple OS from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
- [James Molloy's Kernel Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/)
- [OSDev Wiki - FAT12](https://wiki.osdev.org/FAT12)
- [OSDev Wiki - ATA PIO](https://wiki.osdev.org/ATA_PIO_Mode)

---

## Licença

Este projeto é distribuído sob a [Licença MIT](LICENSE).
