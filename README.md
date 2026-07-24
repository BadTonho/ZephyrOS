# ZephyrOS

[ 🇺🇸 **English** | 🇧🇷 [Português](README.pt-BR.md) ]

Operating system built from scratch in C + x86 Assembly, aiming to be a real, functional, and modular OS.

---

## Project Status

ZephyrOS is an experimental operating system under active development. It runs primarily on **QEMU** and maintains dual user interfaces: a **Classic Mode** utilizing VGA text mode and a **Modern Mode** leveraging VESA VBE graphic resolutions when hardware or emulators support it.

The project is currently intended for educational and experimental purposes and is not yet ready for production on real hardware. Refer to [`ROADMAP.md`](ROADMAP.md) and [`docs/`](docs/) for progress logs, known limitations, and upcoming milestones.

---

## Features & Modules

| Module | Status | Description |
|--------|--------|-------------|
| Bootloader | ✅ | 16-bit Real Mode Assembly → 32-bit Protected Mode |
| Kernel | ✅ | Entry point, panic handler, register context switching |
| VGA Video | ✅ | 80x25 Text mode, custom colors, cursor management |
| VESA VBE | ✅ | Graphic mode, multiple resolutions (640x480 up to 1920x1200, 32bpp) |
| Font | ✅ | 8x16 Bitmap font engine for graphical text rendering |
| Keyboard | ✅ | PS/2 driver, scancode-to-ASCII translation, callback registration |
| Timer | ✅ | Programmable Interval Timer (PIT) at 50 Hz, system ticks |
| IDT / IRQ / ISR | ✅ | 32 CPU exceptions + 16 mapped PIC IRQs |
| Memory | ✅ | E820 map detection, page-level bitmap allocator, dynamic kernel heap |
| Paging | ✅ | Page Directory / Page Tables, virtual memory mapping |
| RAM Compression | ✅ | LZSS compression engine for in-memory data structures |
| TSS | ✅ | Task State Segment for Ring 0 kernel stack switching |
| Processes | ✅ | PID lifecycle, states, preemptive round-robin scheduler |
| Threads | ✅ | Kernel threads (Create, block, yield, round-robin) |
| ATA Driver | ✅ | PIO mode sector read/write operations for IDE drives |
| FAT12 | ✅ | File read/write/delete, root directory listing |
| FAT32 | ✅ | Large disk support (BPB parsing, 32-bit cluster chains) |
| Unified VFS | ✅ | Abstract file system layer over FAT12 and FAT32 |
| BMP Engine | ✅ | Parse and render BMP images (1, 4, 8, and 24 bpp, color palettes) |
| WAV Audio | ✅ | Parse and stream WAV audio files |
| PCI Bus | ✅ | PCI bus enumeration, BAR configuration, vendor/device scanning |
| AC97 Driver | ✅ | AC97 audio controller driver (Play, Stop, Volume control via DMA) |
| PC Speaker | ✅ | Frequency tones, beeps, and square-wave melodies |
| Shell | ✅ | Interactive terminal, scrollback history, diagnostics, Ring 3 executable launcher |
| App API | ✅ | Public API 0.3, `int 0x80` syscalls, IPC, files, and ZAPP loader |
| Text Editor | ✅ | Built-in editor with syntax highlighting and word wrap |
| Media Player | ✅ | WAV audio player with visual playback indicators |
| Task Manager | ✅ | Real-time monitoring of processes, threads, CPU, and memory |
| File Manager | ✅ | Graphical and classic explorer (navigate, create, rename, delete) |
| Desktop Environment | ✅ | Desktop GUI with customizable icons, start menu, and classic fallback |
| Window Manager | ✅ | Overlapping windows (focus, z-order, titlebars, resize, minimize, move) |
| Taskbar | ✅ | Taskbar with application buttons, digital clock, and Start Menu |
| Settings | ✅ | System configuration suite (display, taskbar, window rules, icons, sound) |
| Icon Registry | ✅ | Customizable icon management (desktop, windows, file extensions) |

---

## Project Structure

```
Sistema/
├── Makefile                 # Master Build System
├── ROADMAP.md               # Development roadmap
├── build/                   # Compiled binaries and disk image outputs
├── docs/                    # Architectural & module documentation (13 chapters)
└── src/
    ├── linker.ld            # ELF 32-bit Linker script
    ├── boot/                # Bootloader and Stage 2 loader (Assembly)
    │   ├── boot.asm
    │   └── stage2.asm
    ├── core/                # Core kernel services, App API, syscalls, and ZAPP loader
    ├── kernel/              # Core kernel entry, initialization, and context switch
    │   ├── entry.asm        # Assembly entry point
    │   ├── kernel.c         # Main kernel logic (initializes 20+ subsystems)
    │   ├── panic.c          # Kernel panic handling
    │   └── switch.asm       # Register context switching
    ├── drivers/             # Hardware device drivers
    │   ├── ata.c            # ATA PIO disk driver
    │   ├── idt.c            # IDT configuration & PIC remapping
    │   ├── irq.asm          # Hardware interrupt request handlers
    │   ├── isr.asm          # Software interrupt exception handlers
    │   ├── keyboard.c       # PS/2 keyboard driver
    │   ├── speaker.c        # PC Speaker sound driver
    │   ├── timer.c          # PIT timer driver
    │   ├── tss.c            # Task State Segment driver
    │   ├── video.c          # VGA 80x25 text mode driver
    │   ├── vesa.c           # VESA VBE graphics driver
    │   ├── font.c           # Bitmap font engine
    │   ├── pci.c            # PCI bus enumerator
    │   └── ac97.c           # AC97 audio driver
    ├── memory/              # Memory management subsystem
    │   ├── memory.c         # Bitmap physical allocator & kernel heap
    │   ├── paging.c         # Virtual paging directory and table management
    │   └── compress.c       # LZSS memory compression engine
    ├── fs/                  # File system subsystem
    │   ├── fat12.c          # FAT12 driver
    │   ├── fat32.c          # FAT32 driver
    │   ├── fs.c             # Unified VFS interface
    │   ├── bmp.c            # BMP image decoder
    │   └── wav.c            # WAV audio decoder
    ├── process/             # Process management & IPC
    │   ├── process.c        # Process control block manager & scheduler
    │   └── ipc.c            # Message queues, window focus, and IPC handling
    ├── thread/              # Threading subsystem
    │   └── thread.c         # Thread scheduler
    ├── shell/               # Interactive terminal & shell applications
    │   ├── shell.c          # Interactive shell CLI & ZAPP launcher
    │   ├── editor.c         # Text editor application
    │   ├── mediaplayer.c    # Audio media player application
    │   └── taskmanager.c    # Process & thread task manager application
    ├── filemanager/         # File manager application
    │   └── filemanager.c    # Dual classic/modern file explorer
    ├── desktop/             # Graphical Desktop environment
    │   └── desktop.c        # Desktop renderer & icon layout
    ├── wm/                  # Window Manager
    │   └── wm.c             # Window manager (decorations, z-order, events)
    ├── taskbar/             # Desktop Taskbar
    │   └── taskbar.c        # Taskbar, clock, and Start Menu implementation
    ├── settings/            # System Settings application
    │   └── settings.c       # Settings GUI (display, taskbar, windows, audio)
    ├── icons/               # Icon management system
    │   └── icons.c          # Icon registry and drawing helpers
    └── include/             # Header files organized by module
        ├── types.h, video.h, keyboard.h, idt.h, timer.h,
        ├── memory.h, paging.h, ata.h, fat12.h, fat32.h,
        ├── process.h, thread.h, shell.h, speaker.h, tss.h,
        ├── filemanager.h, panic.h, compress.h, editor.h,
        ├── mediaplayer.h, settings.h, wm.h, icons.h,
        ├── ac97.h, pci.h, bmp.h, wav.h, fs.h, font.h,
        ├── vesa.h, desktop.h, taskbar.h, taskmanager.h
```

---

## Kernel Core Services

In addition to the directory layout shown above, `src/core/` houses logging (`log.c`), string operations (`string.c`), crash recovery (`recovery.c`), application public API (`app_api.c`), file wrappers (`app_files.c`), system call dispatchers (`syscall.c`), executable application loaders (`app_loader.c`), and embedded application assets. `src/process/` manages the process control tables, scheduling queues, and inter-process communication in `ipc.c`. Consult the [Architecture Documentation](docs/02-arquitetura/arquitetura.md) for full system call details.

---

## Building & Toolchain Setup

### Prerequisites

- **NASM** - Netwide Assembler (assembles 16-bit/32-bit x86 Assembly)
- **GCC i686-elf Cross-Compiler** - Freestanding 32-bit C cross-compiler
- **GNU ld** - 32-bit ELF Linker
- **QEMU** or **Bochs** - x86 Emulator for running and debugging

### Windows Setup

```powershell
# Install NASM
winget install nasm

# Install QEMU
winget install qemu

# GCC cross-compiler i686-elf (via WSL, MinGW, or prebuilt toolchain)
# Recommended toolchain: https://github.com/lordmilko/i686-elf-gcc
```

### Linux Setup (Ubuntu / Debian)

```bash
# Core dependencies
sudo apt update && sudo apt install nasm gcc make qemu-system-x86

# To build an i686-elf cross-compiler:
# Follow: https://wiki.osdev.org/GCC_Cross-Compiler
```

### Local Toolchain Configuration

The `Makefile` searches for `nasm`, `i686-elf-gcc`, `i686-elf-ld`, and `qemu-system-i386` in your system `PATH`.
Custom toolchain locations can be specified in an untracked local overrides file named `Makefile.local`.

Example `Makefile.local`:

```makefile
NASM = C:\Tools\NASM\nasm.exe
GCC  = D:\Toolchains\i686-elf-gcc\bin\i686-elf-gcc.exe
LD   = D:\Toolchains\i686-elf-gcc\bin\i686-elf-ld.exe
QEMU = C:\Program Files\QEMU\qemu-system-i386.exe
```

Or pass toolchain paths directly on the command line:

```powershell
make NASM=nasm GCC=i686-elf-gcc LD=i686-elf-ld QEMU=qemu-system-i386
```

---

## Build & Run Targets

```bash
# Build the complete OS image (build/zephyros.img)
make

# Build and launch inside QEMU emulator
make run

# Build with GDB debugging enabled
make debug

# Clean build artifacts
make clean
```

---

## Shell Commands

The interactive Shell initializes automatically during kernel startup. In the default GUI boot flow, the graphical Desktop loads first and the shell terminal can be launched via the **Shell** item on the Start Menu or Desktop icon.

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Display list of all shell commands | `help` |
| `clear` | Clear terminal screen and scrollback buffer | `clear` |
| `ls` | List files and directories on disk | `ls` |
| `cat` | Display text file contents | `cat FILE.TXT` |
| `echo` | Execute ZAPP binary with native fallback | `echo Hello World` |
| `mem` | Display physical memory allocation stats | `mem` |
| `procs` | List active processes and PIDs | `procs` |
| `threads` | List active kernel threads | `threads` |
| `uptime` | System uptime since boot | `uptime` |
| `beep` | Trigger PC Speaker frequency tone | `beep` or `beep 440 500` |
| `melody` | Play musical scale sequence via PC Speaker | `melody` |
| `explorer` | Launch File Manager application | `explorer` |
| `desktop` | Launch graphical Desktop environment | `desktop` |
| `taskmgr` | Launch Task Manager application | `taskmgr` |
| `edit` | Launch built-in Text Editor | `edit FILE.TXT` |
| `play` | Stream WAV audio file via AC97 / PC Speaker | `play MUSIC.WAV` |
| `compress` | Toggle LZSS memory compression | `compress on/off/status` |
| `settings` | Open Settings configuration utility | `settings` |
| `health` | Report overall kernel subsystem health status | `health` |
| `appcheck` | Validate App API, syscalls, VFS, IPC, and loader | `appcheck` |
| `app run` | Launch a Ring 3 `.ZAP` executable with arguments | `app run DEMO.ZAP alpha beta` |
| `app inputtest` | Test focus and input handling of Ring 3 app | `app inputtest` |
| `app argtest` | Test argument passing to Ring 3 application | `app argtest alpha beta` |
| `usertest` | Execute user-mode test process | `usertest` |
| `guimode` | Switch between `classic` (text) and `modern` (VESA) | `guimode modern` |
| `reboot` | Perform CPU hardware reset | `reboot` |
| `shutdown` | Shutdown system via ACPI / QEMU exit | `shutdown` |

> **Note:** For full command documentation and keyboard shortcuts, see [Shortcuts and Commands](docs/atalhos_e_comandos.md).

---

## System Architecture Overview

### Boot Flow (16-bit → 32-bit)

1. BIOS loads `boot.asm` sector to physical address `0x7C00`.
2. Queries system RAM layout using BIOS Interrupt 0x15 (E820 map).
3. Reads 30 kernel sectors from disk into `0x1000`.
4. Configures Global Descriptor Table (GDT) flat memory model.
5. Switches CPU control register CR0 to enter 32-bit Protected Mode.
6. Transfers E820 memory map address in `ESI` register to kernel entry.

### Kernel Initialization

1. Initializes VGA text mode buffer.
2. Constructs Interrupt Descriptor Table (IDT) for 256 gates.
3. Remaps Programmable Interrupt Controller (PIC Master `0x20` → 32, Slave `0xA0` → 40).
4. Registers PS/2 Keyboard and PIT Timer IRQ handlers.
5. Parses E820 memory map and initializes physical page bitmap allocator.
6. Sets up initial virtual paging structures (Page Directory / Page Tables).
7. Initializes Task State Segment (TSS) for Ring 0 kernel stack management.
8. Spawns system processes and background threads.
9. Mounts ATA storage drives and initializes unified FAT12/FAT32 file system.
10. Initializes PC Speaker sound driver.
11. Enumerates VESA VBE graphic modes and initializes font renderer.
12. Scans PCI bus and attaches AC97 audio controller driver.
13. Loads Desktop icon registry, taskbar, window manager, and settings.
14. Spawns interactive Shell and Desktop graphical environment.

### Physical Memory Layout

```
0x00000 - 0x7C00   Real mode BIOS workspace (reused post-boot)
0x7C00  - 0x8000   Boot sector location
0x8000  - 0x10000  E820 Memory map buffer
0x1000  - 0x20000  Kernel image binary
0x20000 - 0x120000 Kernel dynamic heap space (1 MB)
0x90000 - 0x9FFFF  Kernel execution stack
0xB8000 - 0xBFFFF  VGA text video memory buffer
```

### Process Management

- **PIDs 1-64**: Reserved for system kernel tasks and desktop processes.
- **States**: `UNUSED`, `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE`.
- **Scheduler**: Preemptive round-robin algorithm driven by IRQ0 timer tick interrupts.
- **Context Switch**: Assembly routine (`switch.asm`) saves and restores general-purpose registers and flags.
- **User Mode**: Isolated Ring 3 process execution for ZAPP applications with isolated page directories.

---

## Contributing

Contributions are welcome! Please read [`CONTRIBUTING.md`](CONTRIBUTING.md) before submitting pull requests or opening issues.

---

## References & Resources

- [OSDev Wiki](https://wiki.osdev.org)
- [Writing a Simple OS from Scratch (University of Birmingham)](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
- [James Molloy's Kernel Development Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/)
- [OSDev Wiki - FAT12 File System](https://wiki.osdev.org/FAT12)
- [OSDev Wiki - ATA PIO Mode](https://wiki.osdev.org/ATA_PIO_Mode)

---

## License

This project is open-source and licensed under the [MIT License](LICENSE).
