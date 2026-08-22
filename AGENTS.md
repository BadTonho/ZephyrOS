# AGENTS.md — ZephyrOS

Leia este arquivo no início de toda sessão. Siga estas regras SEMPRE.

---

## Build e validação

Os comandos abaixo são referências para execução pelo usuário. Eles são
pré-requisitos operacionais para abrir/testar no QEMU qualquer alteração de
código, header ou Makefile:

```bash
# Gate de qualidade após alterar código
make q3check

# Build completo
make clean && make

# Execução no QEMU
make run
```

O usuário não consegue testar a versão alterada no QEMU sem executar primeiro
`make q3check` e `make clean && make`. Depois que esses pré-requisitos forem
confirmados para a mesma versão do código, o agente não deve reapresentá-los
como testes funcionais pendentes da fase; deve listar apenas a matriz de
validação específica da funcionalidade.

As ferramentas devem ser encontradas pelo `PATH` ou configuradas em
`Makefile.local`, que não é versionado. O agente pode revisar o Makefile e os
comandos, mas não executa build, testes ou QEMU neste projeto.

---

## Regra #0: NÃO MEXER NO BOOT

NÃO edite, otimize, reduza ou modifique `src/boot/boot.asm` sem perguntar ao usuário primeiro. O boot sector tem limites rígidos (512 bytes) e o usuário é responsável por alterações nesse arquivo.

caso for necessario mexer no boot, tem que ser comunicado ao usuario, e é de extrema importancia avisar ao usuario que tem que ser mudado o bootloader, nao precisa pular nem ignorar isso

---

## Regra #1: Log de Erros

Toda falha observável em API pública, inicialização, operação de hardware,
I/O, alocação ou validação DEVE ter log.

Helpers internos podem apenas propagar o erro quando o chamador registrar o
contexto final, evitando logs duplicados e excesso de ruído.

```c
#include "core/log.h"

// Na inicialização:
LOG_INFO("MODULO", "Inicializado com sucesso");

// Na falha:
LOG_ERROR("MODULO", "Falha ao ler disco");
LOG_WARN("MODULO", "Memoria baixa, continuando...");
LOG_DEBUG("MODULO", "Variavel x = 5");
```

Módulos: `BOOT`, `LOG`, `IDT`, `KBD`, `TIMER`, `MEM`, `ATA`, `VESA`, `FAT12`, `FAT32`, `AC97`, `PCI`, `UHCI`, `USB`, `MSC`, `BLOCK`, `STORAGE`, `THRD`, `SHELL`, `WM`, `PROC`, `FS`, `DESKTOP`, `MOUSE`, `IPC`, `GUI`, `STRING`

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

Os códigos canônicos ficam em `src/include/core/errors.h`; novos códigos não
devem ser redefinidos localmente sem atualizar esse contrato. Assinaturas
públicas existentes devem ser preservadas, atualizando todos os chamadores
quando uma alteração for realmente necessária.

Para erros fatais que derrubam o sistema:
```c
LOG_ERROR("MOD", "Erro fatal");
panic("MOD: Erro fatal");
```

---

## Regra #3: Inicialização de Módulos

Toda função `xxx_init()` nova ou modificada DEVE:
1. Logar `LOG_INFO` antes de iniciar
2. Logar `LOG_INFO` após sucesso
3. Logar `LOG_ERROR` e retornar/panic em falha

Funções legadas que já retornam `void` não devem ter sua assinatura alterada
somente por esta regra. Nesses casos, a falha deve ser registrada e o módulo
deve publicar estado degradado ou indisponível quando aplicável.

---

## Convenções de Código

- **Nomes de funções**: `modulo_verbo()` → `ata_read_sector()`, `fat12_list_dir()`
- **Nomes de variáveis**: `snake_case` → `sector_count`, `current_pid`
- **Constantes**: `UPPER_SNAKE_CASE` → `MAX_SECTORS`, `BUFFER_SIZE`
- **Funções novas**: preferencialmente até 100 linhas; funções legadas maiores
  devem ser alteradas somente quando houver benefício claro ou necessidade de
  manutenção.
- **Aninhamento novo**: preferencialmente até 4 níveis; exceções devem ser
  simplificadas quando possível, sem criar refatorações artificiais.
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

- Roadmaps principais: `docs/roadmaps/*.md`
- Ideias e melhorias futuras: `docs/melhorias futuras/*.md`
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
├── core/           → Serviços centrais (log, string)
├── drivers/        → Drivers de hardware (video, vesa, font, idt, isr, irq, keyboard, mouse, timer, tss, ata, speaker, pci, ac97, uhci, usb_msc)
├── memory/         → Gerenciamento de memória (memory, paging, compress)
├── fs/             → Sistema de arquivos (fat12, fat32, fs, block, storage, file_index, wav, bmp)
├── process/        → Gerenciador de processos
├── thread/         → Gerenciador de threads
├── shell/          → Apps do shell (editor, taskmanager, mediaplayer)
├── filemanager/    → File Manager
├── taskbar/        → Taskbar
├── desktop/        → Desktop
├── settings/       → Settings
├── wm/             → Window Manager
├── icons/          → Sistema de ícones
├── gui/            → Primitivas gráficas 2D (gui.c)
└── include/        → Headers organizados por módulo
    ├── core/       → video.h, panic.h, log.h, keyboard.h, timer.h, memory.h, errors.h, spinlock.h, string.h
    ├── drivers/    → idt.h, ata.h, ac97.h, pci.h, vesa.h, speaker.h, font.h, tss.h, mouse.h, uhci.h, usb_msc.h
    ├── fs/         → fat12.h, fat32.h, fs.h, block.h, storage.h, file_index.h, wav.h, bmp.h
    ├── memory/     → paging.h, compress.h
    ├── process/    → process.h, thread.h
    ├── apps/       → shell.h, editor.h, mediaplayer.h, taskmanager.h
    └── ui/         → taskbar.h, desktop.h, settings.h, wm.h, filemanager.h, icons.h, gui.h
```

### Regras

A árvore acima é a referência de organização, não uma lista fechada de arquivos.
Novos arquivos devem seguir a separação por responsabilidade e o diretório do
módulo correspondente.

- [ ] Drivers de hardware → `src/drivers/`
- [ ] Serviços do kernel → `src/core/`
- [ ] Apps do shell → `src/shell/`
- [ ] Headers → `src/include/<modulo>/`
- [ ] NÃO misturar drivers com apps
- [ ] NÃO criar arquivos na raiz de `src/`
- [ ] Submódulos novos DEVEM permanecer pequenos quando possível; módulos
      maiores podem ter mais arquivos quando isso melhorar a separação e a
      manutenção. Exceções devem ser justificadas na documentação técnica.

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
- [ ] Toda alocação deve ter um proprietário claramente definido e uma
      liberação em todos os caminhos de saída aplicáveis
- [ ] Não liberar memória estática, global, emprestada ou cuja posse tenha
      sido transferida
- [ ] Não usar memória após `kfree` e não liberar o mesmo bloco duas vezes
- [ ] Usar `kmalloc_aligned()` quando precisar de alinhamento de página
- [ ] NÃO vazar memória — cada `malloc` tem um `free`

---

## Regra #8: Estrutura de um Driver

```c
// src/drivers/nomedriver.c
#include "drivers/nomedriver.h"
#include "core/errors.h"
#include "core/log.h"
#include "core/panic.h"

// Variáveis estáticas do driver
static int driver_initialized = 0;

// Inicialização
int driver_init(void) {
    LOG_INFO("DRIVER", "Inicializando...");

    if (hardware_falhou) {
        LOG_ERROR("DRIVER", "Hardware nao encontrado!");
        return ERR_NOT_FOUND;
    }

    driver_initialized = 1;
    LOG_INFO("DRIVER", "Inicializado com sucesso");
    return OK;
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

Drivers legados que já possuem uma função de inicialização `void` devem
preservar sua assinatura; nesse caso, registrar a falha e publicar o estado
indisponível ou degradado.

---

## Regra #9: Estrutura de um Módulo Shell

```c
// src/shell/nomemodulo.c
#include "apps/shell.h"
#include "core/log.h"
#include "core/video.h"

static int modulo_active = 0;

void modulo_open(void) {
    if (modulo_active) return;
    modulo_active = 1;

    modulo_draw();
    LOG_INFO("SHELL", "Modulo aberto");
}

void modulo_close(void) {
    modulo_active = 0;
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
- [ ] Usar o fluxo de teclado/IPC e as APIs declaradas nos headers atuais
- [ ] Chamar `taskbar_draw()` ao fechar
- [ ] Comando registrado na tabela de `src/shell/shell_dispatch.c`
- [ ] Adaptador e handler mantidos no modulo de dominio correspondente

### Organizacao atual do Shell

Desde a refatoracao do Shell, nao concentrar novos comandos ou estados de
dominio em `shell.c`:

- `shell.c`: API publica de `shell.h`, roteamento de teclado/mouse/IPC,
  politica de terminal e prompt, e resultados genericos de aplicativos;
- `shell_input.c`: buffer, historico, edicao e scancodes da entrada;
- `shell_dispatch.c`: parsing e tabela unica de comandos;
- `shell_command_utils.c`: helpers internos de argumentos e formatacao;
- `shell_commands_*.c` e `shell_checks.c`: handlers e estados privados por
  dominio;
- `shell_hosted.c`: terminal hospedado e callbacks do Window Manager;
- `shell_job.c`: executor cooperativo de operacoes demoradas;
- `shell_runtime.h`: bridge interno entre os modulos do Shell.

As assinaturas publicas de `src/include/apps/shell.h` devem permanecer
intactas. Novos comandos devem ser adicionados a tabela do dispatcher e ao
adaptador do modulo responsavel, sem duplicar parsing ou mover a politica de
prompt para os handlers.

Não inventar callbacks ou funções de teclado. Antes de criar uma integração,
consultar `src/include/core/keyboard.h` e o dispatcher atual do Shell.

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
| `docs/roadmaps/*.md` | Roadmaps das fases e features planejadas |
| `docs/melhorias futuras/*.md` | Ideias e melhorias ainda não priorizadas |

### Ao criar nova feature

- [ ] Criar roadmap em `docs/roadmaps/nome.md` para feature priorizada
- [ ] Usar `docs/melhorias futuras/nome.md` para proposta ainda não priorizada
- [ ] Seguir formato: Resumo de Progresso → Atalhos → Fases → Limitações → Referências
- [ ] Atualizar `docs/indice.md` se necessário
- [ ] Atualizar `ROADMAP.md` se for fase principal

---

## Regra #12: Não Quebrar o Build

- [ ] NUNCA commitar código que não compila
- [ ] Para alterações de código, tratar `make q3check` e depois
      `make clean && make` como pré-requisitos para abrir/testar a versão
      alterada no QEMU e antes de commitar; não reapresentá-los como pendência
      funcional depois de confirmados para a mesma versão
- [ ] A validação executável pertence ao usuário; o agente não deve executar
      build, testes ou QEMU neste projeto
- [ ] Warnings novos devem ser revisados; warnings existentes devem ser documentados quando não puderem ser corrigidos na etapa atual
- [ ] Se adicionar header, verificar se não quebra outros arquivos
- [ ] Se modificar struct, verificar todas as funções que usam ela
- [ ] NUNCA mudar assinatura de função sem atualizar todos os chamadores

---

## Checklist Geral

Antes de commitar:
1. [ ] Se houve alteração de código, o usuário validou o build e os warnings novos foram revisados?
2. [ ] Toda função de erro tem `LOG_ERROR`?
3. [ ] Toda init tem `LOG_INFO`?
4. [ ] Sem magic numbers?
5. [ ] Funções novas respeitam preferencialmente 100 linhas e 4 níveis de aninhamento?
6. [ ] Arquivo no diretório correto?
7. [ ] Header com include guard?
8. [ ] Makefile atualizado (se novo .c)?
9. [ ] O build foi solicitado ao usuário quando a alteração exigia validação de compilação?
10. [ ] Não quebrei nenhuma função existente?
11. [ ] Revisei o diff staged e confirmei que ele contém apenas a alteração pretendida?
12. [ ] Confirmei que não há senhas, tokens, chaves privadas ou credenciais?
13. [ ] Confirmei que não há caminhos pessoais, configurações locais ou arquivos de backup?
14. [ ] Confirmei que `.mailmap`, `Makefile.local`, `build/` e artefatos locais não estão staged?
15. [ ] Se um header público mudou, atualizei seu documento canônico listado em `docs/qualidade/contratos-publicos.md`?
16. [ ] Se a mudança é uma otimização, registrei a comparação antes/depois em `docs/qualidade/metricas.md`?

---

## Regra #13: Comandos Shell para Novas Funcionalidades

Funcionalidades executáveis voltadas ao usuário DEVEM ter um comando Shell ou
diagnóstico equivalente para testar, inspecionar ou executar a capacidade.
Camadas internas, helpers, callbacks e mudanças somente de infraestrutura
podem ser validados por `health`, `regcheck`, testes determinísticos ou comandos
já existentes, sem criar um comando artificial para cada função.

---

## Regra #14: Execução de Build

O agente de IA **NUNCA** deve executar comandos de build via terminal (`make`, `make clean`, `make run`, etc). O usuário será o único responsável por rodar o build e testar o sistema no emulador. Apenas instrua o usuário a executar o build quando o código estiver pronto.

---

## Regra #15: Modos de Interface (Simple / Classic / Modern)

- **Simple**: TUI original baseada em `video.c`, preservada somente como fallback
  operacional para falha de VESA/backbuffer. Fica congelada: recebe correções
  críticas, mas não novas funcionalidades nem regressão visual completa.
- **Classic**: GUI VESA atual baseada em `gui.c`/`gui.h`. É a interface
  principal, recebe as novas funcionalidades e concentra a matriz obrigatória
  de testes de Desktop, Taskbar, Window Manager e aplicativos hospedados.
- **Modern**: nome reservado para a futura interface realmente moderna. Não
  deve ser oferecido como modo selecionável enquanto essa implementação não
  existir.

Novos aplicativos e interfaces DEVEM priorizar o modo Classic gráfico. O modo
Simple permanece como fallback operacional e não é critério obrigatório de
validação das fases. Ele só deve ser testado quando a alteração tocar
diretamente o fallback Simple, o vídeo ou o teclado.

---

## Regra #16: Verificação de informações antes do commit

Antes de sugerir, criar ou executar qualquer commit, o agente DEVE revisar somente os arquivos
que estão modificados, novos ou staged no Source Control e verificar se não existem informações
sensíveis ou locais, incluindo:

- senhas, tokens, chaves privadas, credenciais, cookies ou arquivos de ambiente;
- e-mails pessoais, nomes de usuário, caminhos locais e dados identificáveis;
- `Makefile.local`, `.mailmap`, backups, dumps, imagens de disco e artefatos de build;
- alterações não relacionadas ao objetivo atual ou arquivos pertencentes ao usuário.

A verificação deve usar `git status --short` e os diffs dos arquivos alterados como fonte de
verdade. Para os arquivos que irão no próximo commit, a revisão principal deve ser feita com
`git diff --cached` e `git diff --cached --check`. Arquivos modificados mas ainda não staged
podem ser revisados para preparar o commit, mas não devem ser tratados como já autorizados.

Arquivos que não aparecem no Source Control não precisam ser verificados novamente. A busca
por segredos também deve ser limitada aos arquivos alterados ou staged, evitando reexaminar o
repositório inteiro sem necessidade. O histórico só deve ser revisado quando a tarefa envolver
limpeza ou reescrita de histórico, remoção de informações pessoais ou alteração de tags.

Se houver dúvida sobre qualquer arquivo modificado ou staged, o agente DEVE parar e informar
o usuário antes do commit.

---

## Regra #17: IDs Operacionais Exatos

Ao orientar qualquer comando que use um ID de dispositivo, disco, volume,
partição ou outro identificador dinâmico, o agente DEVE copiar o ID exatamente
da saída mais recente fornecida pelo usuário ou obtida no sistema. É PROIBIDO
inventar, reconstruir, abreviar, normalizar, trocar separadores ou presumir
sufixos de um ID.

Se o ID não estiver completamente legível ou disponível, o agente DEVE pedir a
saída textual ou uma captura mais clara antes de indicar o comando. Quando
houver vários IDs, o agente DEVE identificar qual linha da saída originou o ID
e repetir o valor literalmente.

---

## Regra #18: Registro de Etapas e Validações

Toda etapa, subetapa, fase, correção ou validação concluída DEVE registrar a
data e a hora exatas em que foi concluída, no roadmap ou documento canônico da
frente correspondente.

O registro DEVE usar o formato ISO 8601 com fuso explícito:

```text
Concluída em: YYYY-MM-DD HH:MM (America/Sao_Paulo)
```

Quando implementação e validação ocorrerem em momentos diferentes, registrar
ambos os horários separadamente. Não inventar nem estimar horários de etapas
históricas; nesses casos, manter o registro sem horário até que o usuário
forneça a informação.
