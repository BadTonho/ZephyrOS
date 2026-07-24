# Contributing to ZephyrOS

Thank you for your interest in contributing to **ZephyrOS**! We welcome bug reports, feature suggestions, and code contributions to help build a functional and modular 32-bit x86 operating system from scratch.

---

## 🚨 Critical Rule #0: Do NOT Modify the Bootloader

> [!CAUTION]
> **Rule #0**: Do NOT edit, optimize, shrink, or modify `src/boot/boot.asm` without prior discussion and explicit approval.
> The boot sector has strict 512-byte hardware constraints. Changes to the bootloader can break disk sector loading and machine bootability.

---

## How to Contribute

### 1. Reporting Bugs & Proposing Features
- Check existing [GitHub Issues](https://github.com/) to see if the topic has already been discussed.
- When opening an issue, provide clear details:
  - Operating system host (Windows/Linux) and QEMU version.
  - Clear steps to reproduce the issue.
  - Terminal logs or kernel panic stack output.

### 2. Pull Request Workflow
1. Fork the repository and create a feature branch (`git checkout -b feature/my-new-driver`).
2. Follow the codebase guidelines detailed below.
3. Test your changes thoroughly using `make clean && make run` in QEMU.
4. Submit a Pull Request with a clear summary of your changes.

---

## Code Standards & Architecture Guidelines

All contributions must follow the codebase conventions established in [`AGENTS.md`](AGENTS.md) and [`docs/regras.md`](docs/regras.md).

### 1. Logging Requirements
Every module and driver **MUST** include logging via `#include "core/log.h"`:

```c
#include "core/log.h"

// Upon initialization:
LOG_INFO("MODULE_NAME", "Initialized successfully");

// On failure:
LOG_ERROR("MODULE_NAME", "Failed to read disk sector");
LOG_WARN("MODULE_NAME", "Low memory, continuing with fallback...");
LOG_DEBUG("MODULE_NAME", "Variable x = 5");
```

Standard Log Modules: `BOOT`, `LOG`, `IDT`, `KBD`, `TIMER`, `MEM`, `ATA`, `VESA`, `FAT12`, `FAT32`, `AC97`, `PCI`, `THRD`, `SHELL`, `WM`, `PROC`, `FS`, `DESKTOP`, `MOUSE`, `IPC`, `GUI`, `STRING`.

### 2. Error Handling
Functions that can fail must return integer status codes:

```c
#define OK            0
#define ERR_NULL      1
#define ERR_MEM       2
#define ERR_DISK      3
#define ERR_NOT_FOUND 4
#define ERR_OVERFLOW  5

int my_function(void* ptr) {
    if (!ptr) {
        LOG_ERROR("MODULE_NAME", "Null pointer provided");
        return ERR_NULL;
    }
    return OK;
}
```

For unrecoverable fatal system errors:
```c
LOG_ERROR("MODULE_NAME", "Fatal kernel crash");
panic("MODULE_NAME: Fatal error occurred");
```

### 3. Module & Driver Initialization
Every `xxx_init()` function must:
1. Log `LOG_INFO` at the start of initialization.
2. Log `LOG_INFO` upon successful completion.
3. Log `LOG_ERROR` and return an error code or call `panic()` on failure.
4. Maintain an internal `static int initialized` state flag.

### 4. Naming Conventions & Code Style
- **Functions**: `module_verb()` → `ata_read_sector()`, `fat12_list_dir()`
- **Variables**: `snake_case` → `sector_count`, `current_pid`
- **Constants & Macros**: `UPPER_SNAKE_CASE` → `MAX_SECTORS`, `BUFFER_SIZE`
- **Structs & Typedefs**: `snake_case_t` → `typedef struct { ... } process_t;`
- **Function Length**: Maximum 100 lines per function.
- **Nesting Level**: Maximum 4 indentation levels.
- **Magic Numbers**: Prohibited. Always use `#define` constants.

### 5. Memory Management Rules
- Always verify if `kmalloc()` returns `NULL`.
- Always call `kfree()` when memory is no longer needed, and set the pointer to `NULL` immediately after.
- Use `kmalloc_aligned()` when page-aligned memory (4KB) is required.

### 6. Directory Structure Rules
When adding new source files, place them in the correct directory:
- Hardware drivers → `src/drivers/`
- Kernel core services → `src/core/`
- Shell CLI applications → `src/shell/`
- Public headers → `src/include/<module>/`
- Never add new source files directly to the root of `src/`.

### 7. Dual Interface Rule (Classic & Modern Modes)
ZephyrOS maintains visual backwards compatibility:
- VGA 80x25 Text Mode (`video.c`) is preserved as a **Classic Mode** fallback.
- VESA Graphics Mode (`vesa.c` / `gui.c`) powers the **Modern Mode**.
- New system components and applications should support or gracefully fall back to both rendering modes.

### 8. Shell Test Command Requirement
Whenever implementing a new executable feature, driver, or hardware capability, you **MUST** register a corresponding interactive command in `src/shell/shell.c` so it can be tested and inspected from the command line.

---

## Build System & Local Testing

### Toolchain Setup
Ensure you have the following tools installed and accessible in your system `PATH`:
- **NASM** (Assembler)
- **i686-elf-gcc** (Freestanding C Cross-Compiler)
- **i686-elf-ld** (32-bit ELF Linker)
- **QEMU** (`qemu-system-i386`)

### Local Overrides (`Makefile.local`)
To specify non-standard paths for your local toolchain, create an uncommitted `Makefile.local` in the root directory:

```makefile
NASM = /custom/path/nasm
GCC  = /custom/path/i686-elf-gcc
LD   = /custom/path/i686-elf-ld
QEMU = /custom/path/qemu-system-i386
```

### Pre-commit Verification
Before committing code or submitting a PR:
1. Run `make clean && make` to ensure a clean build with zero compilation errors.
2. Test the build in QEMU with `make run`.
3. Verify that new warnings are addressed or documented.
4. Ensure no local secrets, personal paths, or build artifacts (`build/`, `Makefile.local`) are included in your commit.

---

## License
By contributing to ZephyrOS, you agree that your contributions will be licensed under the project's [MIT License](LICENSE).
