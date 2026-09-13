# ZephyrOS

[English] | [Portuguese (Brazil)](README.pt-BR.md)

ZephyrOS is a 32-bit x86 operating system built from scratch in freestanding C
and x86 Assembly. It includes its own boot flow, kernel, drivers, filesystems,
shell, desktop environment, user-mode application API, networking stack, and
validation tooling.

The project is aimed at learning through real systems engineering: every
subsystem has an explicit contract, diagnostics, fallback behavior, and a
reproducible validation path whenever the hardware can be exercised.

## Current status

ZephyrOS is under active development. The base system and many of the planned
1.0.0 platform tracks are implemented and validated primarily in QEMU. It is
not a general-purpose production operating system yet.

The current limitations are important:

- Physical hardware support is still `PENDING`; QEMU is the primary supported
  validation environment.
- Hardware coverage, compatibility, security review, and recovery workflows
  continue to evolve.
- `Simple` and `Classic` are the available interfaces. `Modern` is reserved for
  a future rendering engine and is not selectable.
- Roadmap 23 is in progress. PERF1 metric instrumentation and its operational
  host/QEMU baseline matrix are validated for the current revision.

For the authoritative progress and accepted technical debt, see
[`ROADMAP.md`](ROADMAP.md) and the [roadmap index](docs/roadmaps/README.md).

## What is implemented

| Area | Current capabilities |
|------|----------------------|
| Boot and kernel | Custom bootloader and Stage 2 loader, protected mode, GDT, IDT/PIC, interrupts, panic handling, paging, physical memory, kernel heap, SLAB/SLUB caches, VMA, and demand paging |
| Execution | Processes, kernel threads, preemptive round-robin scheduling, wait queues, workqueues, IPC, signals, and isolated Ring 3 execution |
| Storage | ATA PIO, FAT12, FAT32, unified VFS, block layer, block cache, mount lifecycle, permissions, `devfs`, `/proc`, and `/sys` |
| Input, video, and audio | VGA text mode, VESA VBE graphics, bitmap fonts, PS/2 keyboard and mouse, USB HID, PC Speaker, and AC97 audio |
| USB and devices | PCI enumeration, UHCI/EHCI support, USB Mass Storage, device lifecycle tracking, capability snapshots, and degraded fallbacks |
| Networking | E1000 and RTL8139 Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, sockets, HTTP, and TLS building blocks |
| Desktop and applications | Simple VGA fallback, Classic desktop, Window Manager, Taskbar, Settings, File Manager, Text Editor, Media Player, and Task Manager |
| Application platform | Versioned application API, syscalls, ZAPP/Ring 3 loader, pipes, redirection, local packages, App Store flows, and application diagnostics |
| System lifecycle | ACPI and power controls, signed ZUPD/ZSYS update flows, A/B runtime support, rollback, recovery paths, and service supervision |
| Validation | Host-only tests, deterministic fixtures, QEMU test runners, hardware profiles, coverage catalog, diagnostics, and performance metric collection |

The status of an individual capability can be more specific than this summary.
The roadmaps distinguish implemented, validated, pending, accepted debt, and
hardware-dependent work.

## Try it in QEMU

### Prerequisites

The build expects these tools to be available in `PATH` or configured in the
untracked `Makefile.local` file:

- GNU Make
- Python 3
- NASM
- `i686-elf-gcc`, `i686-elf-ld`, and `i686-elf-nm`
- `qemu-system-i386`
- `cc` for host-only tests; `clang` is also used by sanitizer checks when available

The kernel must be built with the `i686-elf-*` cross-compiler. A native GCC is
not a replacement for the freestanding kernel toolchain.

On Ubuntu/Debian, the host-side basics are:

```bash
sudo apt update
sudo apt install make python3 nasm gcc qemu-system-x86 clang
```

The cross-compiler can be built by following the
[OSDev cross-compiler guide](https://wiki.osdev.org/GCC_Cross-Compiler).

On Windows, NASM and QEMU can be installed with:

```powershell
winget install nasm
winget install qemu
```

Install or provide an `i686-elf` cross-toolchain separately. The repository
does not download toolchains automatically.

### Local toolchain paths

If a tool is not on `PATH`, create `Makefile.local` in the repository root.
It is ignored by Git and should contain only machine-specific paths:

```makefile
NASM = /path/to/nasm
GCC = /path/to/i686-elf-gcc
LD = /path/to/i686-elf-ld
NM = /path/to/i686-elf-nm
QEMU = /path/to/qemu-system-i386
```

On Linux, the default `make` command selects the Linux build flow through the
versioned `GNUmakefile`. To select it explicitly, use `make -f Makefile.linux`.

### Required build flow

From the repository root:

```bash
# Quality gate for the working tree
make q3check

# Clean build of build/zephyros.img
make clean && make

# Launch the image in QEMU
make run
```

The generated image is `build/zephyros.img`. `make run` starts the default QEMU
profile with an IDE boot disk and an E1000 network device. The image can also
be exercised with the Stage 2 scenarios:

```bash
make run-stage2-lba
make run-stage2-chs
```

For a GDB-ready QEMU session:

```bash
make debug
```

`make run` is an interactive smoke test, not a replacement for the automated
test suites.

## Useful Shell commands

The Shell is available from the Desktop Start Menu or by launching the Shell
scene. A few commands to explore the system:

```text
help
health
regcheck full
memcheck
kmetrics
devices
device-scan
net status
usb status
ls
cat FILE.TXT
edit FILE.TXT
explorer
taskmgr
settings
app run DEMO.ZAP alpha beta
store status
update status
guimode simple
guimode classic
shutdown
```

The complete command and keyboard reference is in
[`docs/atalhos_e_comandos.md`](docs/atalhos_e_comandos.md). The `kmetrics`
command exposes runtime counters; `kmetrics machine` emits the structured
PERF1 metric envelope used by the host collector.

## Validation

The project keeps separate host-only, deterministic, and QEMU validation
layers. The minimum gates before opening a newly built image in QEMU are:

```text
make q3check
make clean && make
make run
```

Useful focused checks include:

```text
make test-qemu-selftest
make test-shell-diagnostics-host
make test-perf1-host
make test-perf1-qemu
```

The PERF1 QEMU baseline depends on an already-built image and preserves its
artifacts under `build/test-results/perf1-baseline/`. Run only the suites that
match the change being made; the full catalog and command index are available
in [`docs/qualidade/catalogo-testes.md`](docs/qualidade/catalogo-testes.md) and
[`docs/qualidade/comandos-testes-sistema.md`](docs/qualidade/comandos-testes-sistema.md).

## Repository guide

| Path | Purpose |
|------|---------|
| [`src/boot/`](src/boot/) | Bootloader, Stage 2, and recovery boot paths |
| [`src/kernel/`](src/kernel/) | Kernel entry, initialization, panic handling, and context switching |
| [`src/core/`](src/core/) | Logging, syscalls, application services, networking, updates, power, and recovery |
| [`src/drivers/`](src/drivers/) | Video, input, storage, USB, PCI, audio, timer, ACPI, and network drivers |
| [`src/memory/`](src/memory/) | Physical memory, heap, paging, caches, and compression |
| [`src/fs/`](src/fs/) | FAT, VFS, block storage, pseudo-filesystems, and media formats |
| [`src/process/`](src/process/) and [`src/thread/`](src/thread/) | Scheduling, process lifecycle, IPC, and kernel threads |
| [`src/shell/`](src/shell/) and UI directories | Shell commands, jobs, Desktop, WM, Taskbar, Settings, and File Manager |
| [`tests/`](tests/) and [`tools/`](tools/) | Test catalog, fixtures, host runners, QEMU runners, and analysis tools |

## Documentation map

- [Documentation index](docs/indice.md): module guides, contracts, operations, and history.
- [Architecture](docs/02-arquitetura/arquitetura.md): system structure and dependencies.
- [Roadmap](ROADMAP.md): overall progress, completed work, and backlog.
- [Roadmaps by stage](docs/roadmaps/README.md): scope, dependencies, criteria, and validation by workstream.
- [Kernel, processes, and userland](docs/roadmaps/18-kernel-processos-e-userland-v1.0.md)
- [ABI, security, and permissions](docs/roadmaps/19-abi-seguranca-e-permissoes-v1.0.md)
- [VFS, storage, and system update](docs/roadmaps/20-vfs-storage-e-atualizacao-v1.0.md)
- [Hardware, networking, and power](docs/roadmaps/21-hardware-rede-e-energia-v1.0.md)
- [Shell, interface, and applications](docs/roadmaps/22-shell-interface-e-aplicativos-v1.0.md)
- [Performance, validation, and technical debt](docs/roadmaps/23-desempenho-e-dividas-v1.0.md)
- [Release and 1.0.0 acceptance](docs/roadmaps/24-release-e-aceitacao-v1.0.md)
- [Linux environment](docs/qualidade/ambiente-linux.md): Linux-specific setup and image checks.
- [Optimization metrics](docs/qualidade/metricas.md): reproducible performance records and baselines.

## Contributing

Contributions are welcome. Before opening an issue or pull request:

1. Read [`CONTRIBUTING.md`](CONTRIBUTING.md), [`AGENTS.md`](AGENTS.md), and [`docs/regras.md`](docs/regras.md).
2. Keep new code in the subsystem that owns its responsibility.
3. Update the directly affected tests and canonical documentation.
4. Run the relevant host checks, `make q3check`, and a clean build before QEMU validation.

Do not commit `build/`, `Makefile.local`, private keys, or generated local
artifacts. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for adapted
third-party code and its licenses.

## References

- [OSDev Wiki](https://wiki.osdev.org)
- [OSDev GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler)
- [Writing a Simple Operating System from Scratch](https://www.cs.bham.ac.uk/~exr/lectures/opsys/10_11/lectures/os-dev.pdf)
- [James Molloy's Kernel Development Tutorial](http://www.jamesmolloy.co.uk/tutorial_html/)

## License

ZephyrOS is licensed under the [GNU General Public License v3.0](LICENSE).
Third-party components retain their respective licenses as documented in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
