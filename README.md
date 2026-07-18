# MiniOS - Sistema Operacional Educacional

Sistema operacional do zero em C + Assembly (x86), para aprendizado de como um SO funciona.

---

## Funcionalidades

| Módulo | Status | Descrição |
|--------|--------|-----------|
| Bootloader | ✅ | Assembly 16-bit → Protected Mode 32-bit |
| Kernel | ✅ | Entry point, panic handler |
| VGA Video | ✅ | Text mode 80x25, cores, cursor |
| Teclado | ✅ | Driver PS/2, scancode → ASCII |
| Timer | ✅ | PIT 50 Hz, ticks |
| IDT/IRQ/ISR | ✅ | 32 exceções + 16 IRQs mapeadas |
| Memória | ✅ | Detecção E820, bitmap allocator, heap |
| Paging | ✅ | Page directory/table, mapeamento virtual |
| TSS | ✅ | Kernel stack ring 0 |
| Processos | ✅ | PID, estados, scheduler round-robin |
| Context Switch | ✅ | Salva/restaura registradores em Assembly |
| ATA Driver | ✅ | Leitura/escrita de setores (PIO) |
| FAT12 | ✅ | Ler/escrever/deletar arquivos, listar diretório |
| Shell | ✅ | 14 comandos interativos |
| PC Speaker | ✅ | Beep, melodias |
| Threads | ✅ | Create, block, yield |
| File Manager | ✅ | Gerenciador de arquivos estilo Windows Explorer |

---

## Estrutura do Projeto

```
Sistema/
├── Makefile                 # Sistema de build
├── ROADMAP.md               # Roadmap de desenvolvimento
├── build/                   # Arquivos de saída
└── src/
    ├── boot/                # Bootloader (Assembly 16-bit)
    │   └── boot.asm
    ├── kernel/              # Kernel core
    │   ├── entry.asm        # Entry point Assembly
    │   ├── kernel.c         # Kernel principal
    │   ├── panic.c          # Kernel panic
    │   └── switch.asm       # Context switch
    ├── drivers/             # Drivers de hardware
    │   ├── ata.c            # Driver ATA PIO (disco)
    │   ├── idt.c            # IDT + remapeamento PIC
    │   ├── irq.asm          # IRQ handlers
    │   ├── isr.asm          # ISR handlers (exceções)
    │   ├── keyboard.c       # Driver teclado PS/2
    │   ├── speaker.c        # PC Speaker (som)
    │   ├── timer.c          # Timer PIT
    │   ├── tss.c            # Task State Segment
    │   └── video.c          # VGA Text Mode
    ├── memory/              # Gerenciamento de memória
    │   ├── memory.c         # Bitmap allocator + heap
    │   └── paging.c         # Page tables
    ├── fs/                  # Sistema de arquivos
    │   └── fat12.c          # FAT12
    ├── process/             # Processos
    │   └── process.c        # Process manager + scheduler
    ├── thread/              # Threads
    │   └── thread.c         # Thread scheduler
    ├── shell/               # Terminal
    │   └── shell.c          # Shell interativo
    ├── filemanager/         # Gerenciador de arquivos
    │   └── filemanager.c    # Explorer estilo Windows
    ├── include/             # Headers
    │   ├── types.h          # Typedefs (uint8_t, etc)
    │   ├── video.h          # Funções de vídeo
    │   ├── keyboard.h       # Driver de teclado
    │   ├── idt.h            # IDT + registers_t
    │   ├── timer.h          # Timer
    │   ├── memory.h         # Memória
    │   ├── paging.h         # Paging
    │   ├── ata.h            # Driver ATA
    │   ├── fat12.h          # FAT12
    │   ├── process.h        # Processos
    │   ├── thread.h         # Threads
    │   ├── shell.h          # Shell
    │   ├── speaker.h        # PC Speaker
    │   ├── tss.h            # TSS
    │   ├── filemanager.h    # File Manager
    │   └── panic.h          # Panic handler
    └── linker.ld            # Linker script
```

---

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

O shell inicia automaticamente após a inicialização do sistema.

| Comando | Descrição | Exemplo |
|---------|-----------|---------|
| `help` | Lista todos os comandos | `help` |
| `clear` | Limpa a tela | `clear` |
| `ls` | Lista arquivos no disco | `ls` |
| `cat` | Exibe conteúdo de arquivo | `cat ARQUIVO.TXT` |
| `echo` | Imprime texto na tela | `echo Ola Mundo` |
| `mem` | Mostra info de memória | `mem` |
| `procs` | Lista processos ativos | `procs` |
| `threads` | Lista threads ativas | `threads` |
| `uptime` | Tempo desde boot | `uptime` |
| `beep` | Toca um beep | `beep` ou `beep 440 500` |
| `melody` | Toca uma escala musical | `melody` |
| `explorer` | Abre gerenciador de arquivos | `explorer` |
| `reboot` | Reinicia o sistema | `reboot` |
| `shutdown` | Desliga o sistema | `shutdown` |

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
9. Detecta disco e monta FAT12
10. Inicia shell interativo

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

### Memória (`src/memory/memory.c`)
- Bitmap allocator: 1 bit por página (4KB)
- Heap: first-fit com coalescência de blocos livres
- `kmalloc()`, `kfree()`, `kmalloc_aligned()`

### FAT12 (`src/fs/fat12.c`)
- Leitura do BPB (BIOS Parameter Block)
- Interpretação da FAT (File Allocation Table)
- Leitura/escrita de arquivos por cluster chain
- Listagem de diretório raiz

---

## Troubleshooting

### "Nenhum disco encontrado"
O QEMU precisa de uma imagem de disco. O Makefile já gera `build/minios.img`.

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
- [OSDev Wiki - ATA PIO](https://wiki.osdev.org/ATA PIO_Mode)

---

## Licença

Projeto educacional. Livre para uso e modificação.
