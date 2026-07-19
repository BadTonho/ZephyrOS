# AGENTS.md — ZephyrOS

Leia este arquivo no início de toda sessão. Siga estas regras SEMPRE.

---

## Build

```bash
# Build completo
make clean && make

# Run no QEMU
make run
```

Ferramentas (Windows):
- NASM: `C:\Users\Admin\AppData\Local\bin\NASM\nasm.exe`
- GCC: `D:\code\i686-elf-tools-windows\bin\i686-elf-gcc.exe`
- LD: `D:\code\i686-elf-tools-windows\bin\i686-elf-ld.exe`
- QEMU: `C:\Program Files\QEMU\qemu-system-i386.exe`

---

## Regra #0: NÃO MEXER NO BOOT

NÃO edite, otimize, reduza ou modifique `src/boot/boot.asm` sem perguntar ao usuário primeiro. O boot sector tem limites rígidos (512 bytes) e o usuário é responsável por alterações nesse arquivo.

---

## Regra #1: Log de Erros

TODA função que pode falhar DEVE ter log.

```c
#include "core/log.h"

// Na inicialização:
LOG_INFO("MODULO", "Inicializado com sucesso");

// Na falha:
LOG_ERROR("MODULO", "Falha ao ler disco");
LOG_WARN("MODULO", "Memoria baixa, continuando...");
LOG_DEBUG("MODULO", "Variavel x = 5");
```

Módulos: `BOOT`, `LOG`, `IDT`, `KBD`, `TIMER`, `MEM`, `ATA`, `VESA`, `FAT12`, `FAT32`, `AC97`, `PCI`, `THRD`, `SHELL`, `WM`, `PROC`, `FS`, `DESKTOP`

---

## Regra #2: Tratamento de Erros

Funções que falham retornam código de erro:

```c
#define OK           0
#define ERR_NULL     1
#define ERR_MEM      2
#define ERR_DISK     3
#define ERR_NOT_FOUND 4
#define ERR_OVERFLOW  5

int funcao(void) {
    if (!ptr) { LOG_ERROR("MOD", "Null pointer"); return ERR_NULL; }
    if (falha) { LOG_ERROR("MOD", "Operacao falhou"); return ERR_DISK; }
    return OK;
}
```

Para erros fatais que derrubam o sistema:
```c
LOG_ERROR("MOD", "Erro fatal");
panic("MOD: Erro fatal");
```

---

## Regra #3: Inicialização de Módulos

Toda função `xxx_init()` DEVE:
1. Logar `LOG_INFO` antes de iniciar
2. Logar `LOG_INFO` após sucesso
3. Logar `LOG_ERROR` e retornar/panic em falha

---

## Convenções de Código

- **Nomes de funções**: `modulo_verbo()` → `ata_read_sector()`, `fat12_list_dir()`
- **Nomes de variáveis**: `snake_case` → `sector_count`, `current_pid`
- **Constantes**: `UPPER_SNAKE_CASE` → `MAX_SECTORS`, `BUFFER_SIZE`
- **Funções**: máx 100 linhas
- **Aninhamento**: máx 4 níveis
- **Sem magic numbers**: usar `#define`
- **Comentários**: explicar o "porquê", não o "o quê"

---

## Arquitetura

- **Nativo (kernel)**: Shell, Editor, Media Player, Task Manager, File Manager, Settings, Desktop, WM, Taskbar, System Updater
- **Módulos CLI nativos**: Device Manager, Game Manager, Media Manager, Network Manager (funções via shell)
- **Opcional (App Store)**: TUI dos 4 managers acima, Anti-Virus, PCSista, Developer Tools
- **Formato de pacote**: `.zephyrosapp`

---

## Documentação

- Roadmaps: `docs/melhorias futuras/*.md`
- Regras detalhadas: `docs/regras.md`
- Roadmap principal: `ROADMAP.md`
- Índice da docs: `docs/indice.md`

---

## Regra #4: Organização de Diretórios

Novos arquivos DEVEM seguir esta estrutura:

```
src/
├── boot/           → Bootloader (ASM)
├── kernel/         → Kernel core (entry, panic, switch)
├── core/           → Serviços centrais (log)
├── drivers/        → Drivers de hardware (video, vesa, font, idt, isr, irq, keyboard, timer, tss, ata, speaker, pci, ac97)
├── memory/         → Gerenciamento de memória (memory, paging, compress)
├── fs/             → Sistema de arquivos (fat12, fat32, fs, wav, bmp)
├── process/        → Gerenciador de processos
├── thread/         → Gerenciador de threads
├── shell/          → Apps do shell (editor, taskmanager, mediaplayer)
├── filemanager/    → File Manager
├── taskbar/        → Taskbar
├── desktop/        → Desktop
├── settings/       → Settings
├── wm/             → Window Manager
├── icons/          → Sistema de ícones
└── include/        → Headers organizados por módulo
    ├── core/       → video.h, panic.h, log.h, keyboard.h, timer.h, memory.h
    ├── drivers/    → idt.h, ata.h, ac97.h, pci.h, vesa.h, speaker.h, font.h, tss.h
    ├── fs/         → fat12.h, fat32.h, fs.h, wav.h, bmp.h
    ├── memory/     → paging.h, compress.h
    ├── process/    → process.h, thread.h
    ├── apps/       → shell.h, editor.h, mediaplayer.h, taskmanager.h
    └── ui/         → taskbar.h, desktop.h, settings.h, wm.h, filemanager.h, icons.h
```

### Regras

- [ ] Drivers de hardware → `src/drivers/`
- [ ] Serviços do kernel → `src/core/`
- [ ] Apps do shell → `src/shell/`
- [ ] Headers → `src/include/<modulo>/`
- [ ] NÃO misturar drivers com apps
- [ ] NÃO criar arquivos na raiz de `src/`
- [ ] Cada módulo DEVE ter no máximo 2-3 arquivos (.c + .h)

---

## Regra #5: Headers e Include Guards

Todo `.h` DEVE ter include guard:

```c
#ifndef MODULO_H
#define MODULO_H

#include "types.h"

// Declarações aqui

#endif
```

### Regras de include

- [ ] Sempre incluir `types.h` primeiro se precisar de tipos
- [ ] Usar aspas para headers do projeto: `#include "core/log.h"`
- [ ] NÃO incluir `.c` em outros `.c`
- [ ] Headers DEVEM ser auto-contidos (incluir tudo que precisam)
- [ ] NÃO incluir headers desnecessários (minimizar dependências)

---

## Regra #6: Convenções de Structs

```c
// Nome: snake_case com sufixo _t
typedef struct {
    uint32_t lba;
    uint8_t  sector_count;
    uint8_t* buffer;
} ata_request_t;

// Variáveis: snake_case sem _t
ata_request_t request;

// Funções que operam na struct: modulo_verbo()
int ata_read(ata_request_t* req);
void ata_free(ata_request_t* req);
```

### Regras

- [ ] Structs: `snake_case_t` (ex: `process_t`, `fat12_entry_t`)
- [ ] Enums: `snake_case_t` com valores `UPPER_SNAKE` (ex: `state_t { STATE_IDLE, STATE_RUNNING }`)
- [ ] Typedef SEMPRE (ex: `typedef struct { ... } foo_t;`)
- [ ] Ponteiros em parâmetros: primeiro argumento (ex: `func(dados, ...)`)

---

## Regra #7: Gerenciamento de Memória

```c
// Alocar
void* ptr = kmalloc(size);
if (!ptr) { LOG_ERROR("MOD", "Falha ao alocar memoria"); return ERR_MEM; }

// Liberar
kfree(ptr);
ptr = NULL; // sempre nullar após free
```

### Regras

- [ ] SEMPRE verificar se `kmalloc` retornou NULL
- [ ] SEMPRE `kfree` ao final da função que alocou
- [ ] NUNCA usar `kfree` em ponteiro NULL
- [ ] NUNCA usar ponteiro após `kfree`
- [ ] Usar `kmalloc_aligned()` quando precisar de alinhamento de página
- [ ] NÃO vazar memória — cada `malloc` tem um `free`

---

## Regra #8: Estrutura de um Driver

```c
// src/drivers/nomedriver.c
#include "drivers/nomedriver.h"
#include "core/log.h"
#include "core/panic.h"

// Variáveis estáticas do driver
static int driver_initialized = 0;

// Inicialização
void driver_init(void) {
    LOG_INFO("DRIVER", "Inicializando...");

    if (hardware_falhou) {
        LOG_ERROR("DRIVER", "Hardware nao encontrado!");
        return;
    }

    driver_initialized = 1;
    LOG_INFO("DRIVER", "Inicializado com sucesso");
}

// Funções públicas
int driver_read(uint32_t addr, uint8_t* buf, int size) {
    if (!driver_initialized) {
        LOG_ERROR("DRIVER", "Driver nao inicializado");
        return ERR_NOT_FOUND;
    }
    if (!buf) {
        LOG_ERROR("DRIVER", "Buffer nulo");
        return ERR_NULL;
    }

    // implementação...

    return OK;
}
```

### Checklist do driver

- [ ] Header com include guard em `src/include/drivers/`
- [ ] Variável `static int initialized` para controle
- [ ] `LOG_INFO` no início e fim da init
- [ ] `LOG_ERROR` em toda falha
- [ ] Verificar `initialized` em toda função pública
- [ ] Verificar ponteiros nulos
- [ ] Retornar código de erro

---

## Regra #9: Estrutura de um Módulo Shell

```c
// src/shell/nomemodulo.c
#include "apps/shell.h"
#include "core/log.h"
#include "core/video.h"

static int modulo_active = 0;
static void (*prev_callback)(uint8_t) = NULL;

void modulo_open(void) {
    if (modulo_active) return;
    modulo_active = 1;

    prev_callback = keyboard_get_callback();
    keyboard_set_callback(modulo_handle_key);

    modulo_draw();
    LOG_INFO("SHELL", "Modulo aberto");
}

void modulo_close(void) {
    modulo_active = 0;
    keyboard_set_callback(prev_callback);
    video_clear();
    taskbar_draw();
    LOG_INFO("SHELL", "Modulo fechado");
}

void modulo_draw(void) {
    video_clear();
    // desenha interface...
}

void modulo_handle_key(uint8_t scancode) {
    switch (scancode) {
        case KEY_ESC: modulo_close(); break;
        case KEY_UP:   modulo_navigate(-1); break;
        case KEY_DOWN: modulo_navigate(1); break;
        // ...
    }
}
```

### Checklist do módulo shell

- [ ] `modulo_open()` — abre módulo
- [ ] `modulo_close()` — fecha e restaura estado
- [ ] `modulo_draw()` — desenha interface
- [ ] `modulo_handle_key()` — trata input
- [ ] Salvar/restaurar callback anterior do teclado
- [ ] Chamar `taskbar_draw()` ao fechar
- [ ] Comando registrado no `shell.c`

---

## Regra #10: Build e Makefile

Ao adicionar novo arquivo `.c`:

1. Adicionar variáveis no topo do Makefile:
```makefile
# Arquivos - Novo Modulo
NOVO_C = src/novo/novo.c
NOVO_OBJ = build/novo.o
```

2. Adicionar regra de compilação:
```makefile
$(NOVO_OBJ): $(NOVO_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@
```

3. Adicionar ao `OBJS`:
```makefile
OBJS = ... $(NOVO_OBJ)
```

### Regras

- [ ] NOME_DO_OBJ = `build/nome.o`
- [ ] SEMPRE usar `@if not exist build mkdir build`
- [ ] Usar `$(GCC) $(CFLAGS)` para C, `$(NASM) -f elf32` para ASM
- [ ] Adicionar ao final da lista `OBJS`

---

## Regra #11: Documentação

### Arquivos que DEVEM existir

| Arquivo | Conteúdo |
|---------|----------|
| `AGENTS.md` | Regras para agentes de IA (este arquivo) |
| `ROADMAP.md` | Roadmap geral do projeto |
| `docs/indice.md` | Índice de toda documentação |
| `docs/regras.md` | Regras detalhadas de código |
| `docs/melhorias futuras/*.md` | Roadmaps de cada feature |

### Ao criar nova feature

- [ ] Criar roadmap em `docs/melhorias futuras/nome.md`
- [ ] Seguir formato: Resumo de Progresso → Atalhos → Fases → Limitações → Referências
- [ ] Atualizar `docs/indice.md` se necessário
- [ ] Atualizar `ROADMAP.md` se for fase principal

---

## Regra #12: Não Quebrar o Build

- [ ] NUNCA commitar código que não compila
- [ ] SEMPRE testar `make clean && make` antes de commitar
- [ ] Se adicionar header, verificar se não quebra outros arquivos
- [ ] Se modificar struct, verificar todas as funções que usam ela
- [ ] NUNCA mudar assinatura de função sem atualizar todos os chamadores

---

## Checklist Geral

Antes de commitar:
1. [ ] Código compilou sem warnings?
2. [ ] Toda função de erro tem `LOG_ERROR`?
3. [ ] Toda init tem `LOG_INFO`?
4. [ ] Sem magic numbers?
5. [ ] Funções com máx 100 linhas?
6. [ ] Arquivo no diretório correto?
7. [ ] Header com include guard?
8. [ ] Makefile atualizado (se novo .c)?
9. [ ] `make clean && make` passou?
10. [ ] Não quebrei nenhuma função existente?
