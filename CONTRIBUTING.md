# Contributing to ZephyrOS

Thank you for your interest in contributing to **ZephyrOS**! We welcome bug reports, feature suggestions, and code contributions to help build a functional, secure, robust, and modular 32-bit x86 operating system from scratch.

---

## Bootloader Guidelines (`src/boot/`)

Modifying, optimizing, or updating the bootloader (`src/boot/boot.asm`, `src/boot/stage2.asm`, etc.) is **permitted** when it is part of the task or feature scope.

When modifying bootloader components, contributors must adhere to strict hardware and architectural constraints:

- **512-Byte Boot Sector Limit**: The primary boot sector (`src/boot/boot.asm`) has a hard 512-byte limit (including the `0x55AA` signature). Any change must preserve this exact size constraint. Overflows will corrupt partition tables or cause BIOS boot failures.
- **Bootloader Integrity**: Changes must preserve disk sector loading, Stage 2 handoff, Real Mode to Protected Mode transitions, and overall machine bootability across emulated and real hardware.
- **Communication & Documentation**: Bootloader changes must not be silent. Document the rationale, scope, and expected impact in your pull request and commit message, accompanied by validation in QEMU.

---

## How to Contribute

### 1. Reporting Bugs & Suggesting Enhancements
- Check existing [GitHub Issues](https://github.com/) to see if the topic has already been discussed.
- When opening an issue, provide clear diagnostic details:
  - Host operating system (Windows/Linux) and QEMU version.
  - Step-by-step reproduction instructions.
  - Serial/terminal logs, screen captures, or kernel panic stack output.

### 2. Pull Request Workflow
1. Fork the repository and create a descriptive branch (`git checkout -b feature/my-feature` or `git checkout -b fix/issue-description`).
2. Implement your changes according to the codebase standards detailed below.
3. Validate your code with the local quality gate:
   ```bash
   make q3check
   ```
4. Build cleanly from scratch:
   ```bash
   make clean && make
   ```
5. Test thoroughly in QEMU:
   ```bash
   make run
   ```
6. Submit a Pull Request with a clear summary, testing evidence, and affected roadmaps/docs.

---

## Code Standards & Architecture Guidelines

All contributions must follow the architectural conventions and rules documented in [`AGENTS.md`](AGENTS.md) and [`docs/regras.md`](docs/regras.md).

### 1. Logging Requirements
Every subsystem and driver must provide structured logging using `#include "core/log.h"`:

```c
#include "core/log.h"

// Lifecycle & successful initialization:
LOG_INFO("MODULE_TAG", "Initialized successfully");

// Failures & errors:
LOG_ERROR("MODULE_TAG", "Failed to read disk sector");
LOG_WARN("MODULE_TAG", "Resource constrained, falling back...");
LOG_DEBUG("MODULE_TAG", "Internal variable state: %d", val);
```

- Use meaningful, consistent module tags (e.g., `ATA`, `PCI`, `VESA`, `FAT32`, `NET`, `MEM`, `SHELL`).
- Log failures in the layer that has sufficient context to explain the operation, cause, and impact.
- Avoid noisy or blocking logging inside interrupt handlers (ISRs) and hot loops.
- Do not log sensitive data, cryptographic keys, or security buffers.

### 2. Error Handling & Contracts
Functions that can fail must declare explicit, unambiguous success/failure contracts:
- Use canonical error codes from `src/include/core/errors.h` (`OK`, `ERR_NULL`, `ERR_MEM`, `ERR_DISK`, `ERR_NOT_FOUND`, `ERR_OVERFLOW`, `ERR_TIMEOUT`, `ERR_STATE`, `ERR_UNAVAILABLE`, `ERR_INVALID`).
- Never redefine error constants locally.
- Functions returning pointers may use `NULL` on failure when that is the established contract.
- Always release or transfer owned resources on failure paths (no memory leaks, double frees, or dangling handles).
- Reserve `panic()` exclusively for fatal, unrecoverable kernel invariant violations:
  ```c
  LOG_ERROR("MODULE_TAG", "Fatal unrecoverable error");
  panic("MODULE_TAG: Unrecoverable invariant violation");
  ```

### 3. Module & Driver Initialization
Every `xxx_init()` function must:
1. Define a clear contract for success, degraded operation, and failure.
2. Log initialization lifecycle (`LOG_INFO` on start and completion).
3. Be idempotent where supported by hardware, or reject out-of-order calls gracefully.
4. Clean up partially acquired resources if initialization fails, leaving the module in a safe `UNAVAILABLE` or `DEGRADED` state rather than crashing.

### 4. Memory Management & Resource Ownership
- Every allocated resource must have a clear owner, lifetime, and explicit release.
- Always verify allocator results (e.g., check that `kmalloc()`, `kmalloc_aligned()`, or `pmm_alloc_page()` did not return `NULL`).
- Use the correct deallocator pair: pair `kmalloc` with `kfree`, and physical page allocations with `pmm_free_page`. Never mix allocators.
- Set pointers to `NULL` after freeing when they remain in scope.
- Never use memory after freeing (use-after-free) or double-free resources.

### 5. Naming Conventions & Code Style
- **Functions**: `module_verb()` (e.g., `ata_read_sector()`, `fat32_open_file()`).
- **Variables**: `snake_case` (e.g., `sector_count`, `current_pid`).
- **Constants & Macros**: `UPPER_SNAKE_CASE` (e.g., `MAX_SECTORS`, `BUFFER_SIZE`).
- **Types & Structs**: `snake_case_t` (e.g., `typedef struct { ... } process_t;`).
- **Function Scope**: Aim for functions under 100 lines and no more than 4 levels of indentation.
- **No Magic Numbers**: Always define named constants (`#define` or `enum`).
- **Code Comments**: Keep code clean and self-explanatory. Architectural rationale, ABI, invariants, and designs belong in canonical documentation files (`docs/`), not cluttered as comments in the source code.

### 6. Directory Structure
All new files must reside in their appropriate subsystem directory:
```
src/
├── boot/           → Bootloader, Stage 2 loader, Recovery loader (ASM, C)
├── kernel/         → Kernel core (entry, main kernel, panic, context switch)
├── core/           → Core kernel services (log, string, syscall, crypto, network)
├── drivers/        → Hardware drivers (video, vesa, font, idt, keyboard, mouse, timer, ata, ac97, pci, e1000, etc.)
├── memory/         → Memory managers (physical memory, paging, heap, compression)
├── fs/             → Filesystems & formats (fat12, fat32, vfs, storage, bmp, wav)
├── process/        → Process management, scheduler, and IPC
├── thread/         → Kernel threads
├── shell/          → Shell CLI and built-in commands
├── ui/             → UI primitives, icons, and display metrics
├── wm/             → Window Manager
├── desktop/        → Desktop environment
├── taskbar/        → Taskbar and Start Menu
├── settings/       → System settings
├── filemanager/    → File Manager
└── include/        → Modular public headers by subsystem
```
- Never create source files directly in the root of `src/`.
- Every header must have a unique include guard and include only what it directly requires.

### 7. Interface Modes
ZephyrOS supports defined user interface environments:
- **Simple Mode**: VGA 80x25 text mode (`video.c`). Maintained as a frozen fallback for recovery and low-spec environments.
- **Classic Mode**: VESA VBE graphical mode (`vesa.c`, `gui.c`, `wm.c`). Primary desktop interface receiving full feature development and UI acceptance testing.
- **Modern Mode**: Reserved for a future rendering engine; must not be exposed as selectable until implemented.

### 8. Testing & Validation (Rule #22)
- Any modification that alters behavior, contracts, state, error reporting, drivers, or APIs must maintain and update corresponding test coverage.
- When implementing a new driver, service, or hardware capability, register an interactive test command in `src/shell/shell_dispatch.c` (with its handler in `src/shell/shell_commands_*.c`) or provide deterministic tests.
- Do not weaken, skip, or conceal failing tests to achieve a green result; real defects must remain flagged as failures until fixed.

---

## Build System & Local Testing

### Toolchain Prerequisites
Make sure the following tools are installed and available in your system `PATH`:
- **NASM** (x86 Assembler)
- **i686-elf-gcc** (Freestanding C Cross-Compiler)
- **i686-elf-ld** (32-bit ELF Linker)
- **QEMU** (`qemu-system-i386`)
- **Python 3** (Used by image composition and validation tools in `tools/`)

### Local Overrides (`Makefile.local`)
If your toolchain binaries are located in non-standard paths, create a `Makefile.local` in the project root (this file is ignored by git):

```makefile
NASM = /custom/path/nasm
GCC  = /custom/path/i686-elf-gcc
LD   = /custom/path/i686-elf-ld
QEMU = /custom/path/qemu-system-i386
```

### Pre-commit Verification Workflow
Before submitting a PR or committing code:
1. `make q3check` — Runs quality checks on the working diff.
2. `make clean && make` — Ensures a complete, clean build without compilation errors or warnings.
3. `make run` — Runs the operating system in QEMU to test changes interactively.
4. When public headers change, update the corresponding documentation listed in [`docs/qualidade/contratos-publicos.md`](docs/qualidade/contratos-publicos.md).
5. Never commit build artifacts (`build/`), local configs (`Makefile.local`), or sensitive data.

---

## License
By contributing to ZephyrOS, you agree that your contributions will be licensed under the project's [GNU General Public License v3.0 (GPLv3)](LICENSE).
