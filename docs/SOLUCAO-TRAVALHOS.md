# Solução para Travamentos e Falhas do ZephyrOS

Auditoria completa de estabilidade. Identificadas **45+ vulnerabilidades** em 15 arquivos que causam travamentos, crashes e corrupção de memória.

---

## Resumo Executivo

| Severidade | Qtd | Impacto |
|-----------|-----|---------|
| **CRÍTICO** | 8 | Sistema trava (freeze) — sem saída |
| **ALTO** | 12 | Crash / page fault / triple fault |
| **MÉDIO** | 15+ | Corrupção de memória / overflow / uso indevido |
| **TOTAL** | **45+** | |

---

## CRÍTICO — Travamento do Sistema

### C1. Loops infinitos vazios (`while(1){}`)

**Arquivos:** `settings.c:413`, `shell.c:285`, `shell.c:292`

```c
// settings.c — caminho inalcançável que trava o sistema
while(1) {}

// shell.c — dois loops vazios em branches de erro
while(1) {}  // linha 285
while(1) {}  // linha 292
```

**Problema:** Se a execução chegar nessas linhas, o sistema trava para sempre. Sem panic, sem log, sem saída.

**Solução:** Substituir por `panic()` com mensagem ou `return` seguro.

---

### C2. Polling de teclado sem timeout (`settings.c`)

**Arquivo:** `settings.c:18-24`

```c
static uint8_t keyboard_get_scancode(void) {
    uint8_t scancode = 0;
    while (!scancode) {
        __asm__ volatile("inb $0x60, %0" : "=a"(scancode));
    }
    return scancode;
}
```

**Problema:** Loop apertado lendo porta 0x60 sem timeout. Se o hardware do teclado não responder, o CPU trava para sempre. O Settings é o único módulo que ignora o sistema de interrupções do teclado.

**Solução:** Adicionar contador máximo de iterações (ex: 100000) e retornar 0/timeout. Ou usar o callback do teclado (`keyboard_set_callback`) como todos os outros módulos.

---

### C3. Polling duplo sem timeout (`settings.c:294`)

```c
while (!scancode) { scancode = keyboard_get_scancode(); }
```

**Problema:** Chama C2 dentro de outro loop — mesmo problema amplificado.

**Solução:** Mesma que C2.

---

## ALTO — Crash / Dereferência de NULL

### C4. `kmalloc()` sem NULL check em `process_create()`

**Arquivo:** `process.c:63-64`

```c
proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
// Se kmalloc retorna NULL: kernel_stack_top = 0x1000 → escrita em memória baixa
```

**Problema:** Se a memória estiver cheia, `kmalloc` retorna NULL. O código calcula `kernel_stack_top = 0 + 4096 = 0x1000` e escreve registradores CPU nesse endereço (BIOS IVT). Na próxima troca de contexto → triple fault.

**Solução:** Verificar NULL antes de usar:
```c
proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
if (!proc->kernel_stack) {
    LOG_ERROR("PROC", "Falha ao alocar kernel stack");
    return NULL;
}
```

---

### C5. `kmalloc()` sem NULL check — 7 ocorrências em `fat12.c`

**Arquivo:** `fat12.c` — linhas 440, 479, 586, 616, 817, 933, 1088

```c
uint8_t* cluster_buf = (uint8_t*)kmalloc(bytes_per_cluster);
// Sem NULL check — usado imediatamente para leitura do disco
```

**Problema:** Qualquer operação de arquivo (listar, ler, criar, deletar) pode crashar se a memória estiver baixa.

**Solução:** Adicionar NULL check em cada alocação e retornar erro.

---

### C6. `kmalloc()` sem NULL check — múltiplas ocorrências em `fat32.c`

Mesmo padrão que C5, mas no driver FAT32.

---

### C7. `kmalloc()` sem NULL check em `editor.c`

**Arquivo:** `editor.c:354`

```c
editor_buffer = kmalloc(131072);  // 128 KB
// Sem NULL check — usado imediatamente
```

**Problema:** Se o heap não tiver 128KB livres, o editor crasha ao abrir.

**Solução:** Verificar NULL e mostrar mensagem de erro ao usuário.

---

### C8. `alloc_line()` sem NULL check em `editor.c`

**Arquivo:** `editor.c:285`, `editor.c:687`

```c
editor.lines[line_idx + 1] = alloc_line();
// alloc_line() Internamente faz kmalloc(512) sem NULL check
// Resultado usado imediatamente
```

**Problema:** Se a alocação falhar, `editor.lines[n]` é NULL → crash ao acessar.

---

### C9. `str_insert()` sem limite de tamanho — buffer overflow

**Arquivo:** `editor.c:35-41`

```c
static void str_insert(char* str, int pos, char c) {
    int len = str_len(str);
    for (int i = len; i >= pos; i--) {
        str[i + 1] = str[i];  // Se len = 511, escreve na posição 512
    }
    str[pos] = c;
}
```

**Problema:** Cada linha tem 512 bytes. Se a linha já tem 511 caracteres, inserir um caractere escreve além do buffer → corrupção do heap.

**Solução:** Verificar `if (len >= EDITOR_MAX_LINE_LENGTH - 1) return;` antes de inserir.

---

### C10. `editor.lines[]` — acesso fora dos limites

**Arquivo:** `editor.c:395-406`

```c
if (line >= EDITOR_MAX_LINES) break;  // Quebra o loop
// ... mas depois:
editor.lines[line][pos] = '\0';  // line pode ser == EDITOR_MAX_LINES → OOB write
```

**Problema:** Se o arquivo tem mais de 2000 linhas, o loop quebra mas o código continua acessando `lines[2000]` → escrita fora do array.

**Solução:** Adicionar `if (line >= EDITOR_MAX_LINES) line = EDITOR_MAX_LINES - 1;` antes da linha 406.

---

### C11. `panic()` sem NULL check na mensagem

**Arquivo:** `panic.c:42`

```c
video_print(message);  // message pode ser NULL
```

**Problema:** Se alguém chamar `panic(NULL)`, o handler de crash crasha.

---

### C12. `ac97_init()` — PCI device sem NULL check

**Arquivo:** `ac97.c`

```c
pci_device_t* dev = pci_find_device(...);  // Pode retornar NULL
// Usado imediatamente sem verificação
```

**Problema:** Se não houver controladora AC97 (ex: QEMU sem áudio), o driver crasha.

---

### C13. `thread_create()` — cleanup incompleto na falha

**Arquivo:** `thread.c:50-51`

```c
if (!stack) {
    // thread->state já foi setado para THREAD_RUNNING
    // thread->id já foi atribuído
    // Sem rollback → zombie thread no scheduler
    return NULL;
}
```

**Problema:** Se a alocação da stack falhar, a thread fica marcada como RUNNING mas sem stack → crash no próximo scheduler tick.

**Solução:** Resetar `thread->state = THREAD_UNUSED` antes de retornar NULL.

---

## MÉDIO — Corrupção de Memória

### C14. Use-after-free em `fat32.c`

**Arquivo:** `fat32.c:120-122`

```c
if (strncmp(entry->name, filename, 11) == 0) {
    kfree(cluster_buf);      // Libera o buffer
    return entry;            // entry aponta para o buffer liberado!
}
```

**Problema:** O ponteiro retornado aponta para memória já liberada. O caller lê dados de memória que pode ser reescrita por outra alocação → corrupção silenciosa.

**Solução:** Copiar os dados da entrada antes de liberar o buffer, ou não liberar até o caller terminar.

---

### C15. Cadeia de clusters sem limite em `fat12.c`

**Arquivo:** `fat12.c`

```c
while (cluster < 0xFF8) {
    // Lê cluster, segue para o próximo
    cluster = fat12_get_cluster(cluster);
}
```

**Problema:** Se a FAT estiver corrompida com um ciclo (A → B → A), isso vira um loop infinito → CPU trava.

**Solução:** Adicionar contador de iterações:
```c
uint32_t iter = 0;
while (cluster < 0xFF8 && iter++ < MAX_CLUSTERS) {
    ...
}
if (iter >= MAX_CLUSTERS) {
    LOG_ERROR("FAT12", "Cadeia de clusters corrompida (ciclo)");
    break;
}
```

---

### C16. Buffer overflow em concatenação de paths

**Arquivo:** `filemanager.c:779-783`

```c
char new_path[FM_MAX_PATH];  // 256 bytes
int pi = 0;
for (int i = 0; state.current_path[i]; i++) new_path[pi++] = state.current_path[i];
new_path[pi++] = '/';
for (int i = 0; f->name[i]; i++) new_path[pi++] = f->name[i];
// Se current_path tem 255 chars + '/' + nome = 268 bytes → overflow do buffer
```

**Problema:** Se o caminho atual estiver perto do tamanho máximo, a concatenação ultrapassa o buffer na stack.

**Solução:** Verificar `if (pi >= FM_MAX_PATH - 1) break;` dentro de cada loop.

---

### C17. Decompressão sem limite de saída

**Arquivo:** `compress.c:126-163`

```c
while (si < src_size) {
    ...
    dst[di++] = c;  // Nunca verifica se di < dst_buffer_size
}
```

**Problema:** `dst_size` é parâmetro de SAÍDA (output), não de limite. Dados comprimidos maliciosos podem expandir para muito além do buffer → overflow do heap.

**Solução:** Adicionar parâmetro de tamanho do buffer de saída e verificar `if (di >= dst_buffer_size) return ERR_OVERFLOW;`

---

### C18. BMP sem validação de pixel offset

**Arquivo:** `bmp.c:108-151`

```c
uint32_t offset = actual_y * row_size + x * 3;
color.channels.blue = img->pixel_data[offset];
// offset pode ultrapassar pixel_data_size
```

**Problema:** Valores do header BMP vêm do disco (não confiáveis). Um BMP corrompido pode gerar offset fora do buffer.

---

### C19. FAT12/FAT32 — BPB sem validação

**Arquivo:** `fat12.c:48-85`, `fat32.c:144-178`

```c
fs.fat_start = fs.bpb.reserved_sectors;
fs.root_start = fs.fat_start + (fs.bpb.num_fats * fs.bpb.sectors_per_fat);
// Se bytes_per_sector = 0 → divisão por zero
// Se sectors_per_fat enorme → kmalloc(exaustão)
```

**Problema:** Campos do BPB vêm do disco sem validação. Disco corrompido → crash.

---

### C20. Overflow inteiro no speaker

**Arquivo:** `speaker.c:36`

```c
uint32_t ticks = duration_ms * 50;  // Se duration_ms > 85899 → overflow
```

---

### C21. Buffer grande na stack em `compress.c`

**Arquivo:** `compress.c:60`

```c
uint8_t ring[COMPRESS_LZSS_N];  // ~4KB na stack do kernel
```

**Problema:** Se chamado de uma cadeia de chamadas profunda, pode estourar a stack do kernel.

**Solução:** Usar `kmalloc` para o ring buffer ou torná-lo `static`.

---

## Plano de Correção — Prioridade

### Fase 1: Estabilidade Imediata (elimina 80% dos travamentos)

| # | Arquivo | O que fazer | Esforço |
|---|---------|-------------|---------|
| C1 | settings.c, shell.c | Substituir `while(1){}` por `panic()` ou `return` | 5 min |
| C2-C3 | settings.c | Adicionar timeout ao polling de teclado | 10 min |
| C4 | process.c | Adicionar NULL check no kmalloc do kernel_stack | 2 min |
| C5 | fat12.c | Adicionar NULL checks em 7 alocações | 15 min |
| C6 | fat32.c | Adicionar NULL checks nas alocações | 15 min |
| C7-C8 | editor.c | Adicionar NULL checks em kmalloc e alloc_line | 5 min |
| C11 | panic.c | Verificar message != NULL | 1 min |
| C12 | ac97.c | Verificar pci_find_device != NULL | 2 min |
| C13 | thread.c | Resetar state na falha de allocação | 2 min |

### Fase 2: Prevenção de Crash (elimina crashes)

| # | Arquivo | O que fazer | Esforço |
|---|---------|-------------|---------|
| C9 | editor.c | Adicionar limite em str_insert | 3 min |
| C10 | editor.c | Corrigir acesso OOB no editor.lines | 3 min |
| C14 | fat32.c | Corrigir use-after-free | 10 min |
| C15 | fat12.c | Adicionar limite de iterações na cadeia de clusters | 5 min |
| C16 | filemanager.c | Adicionar bounds check na concatenação de paths | 5 min |

### Fase 3: Proteção de Dados (elimina corrupção)

| # | Arquivo | O que fazer | Esforço |
|---|---------|-------------|---------|
| C17 | compress.c | Adicionar limite de tamanho no output buffer | 10 min |
| C18 | bmp.c | Validar offset do pixel contra buffer | 5 min |
| C19 | fat12.c, fat32.c | Validar campos do BPB antes de usar | 15 min |
| C20 | speaker.c | Adicionar check de overflow | 2 min |
| C21 | compress.c | Mover ring buffer para static ou kmalloc | 5 min |

---

## Padrão de Correção Recomendado

### Para kmalloc sem NULL check:

```c
// ANTES (perigoso)
uint8_t* buf = kmalloc(size);
memcpy(buf, data, size);

// DEPOIS (seguro)
uint8_t* buf = kmalloc(size);
if (!buf) {
    LOG_ERROR("MOD", "Falha ao alocar %u bytes", size);
    return ERR_MEM;
}
memcpy(buf, data, size);
```

### Para loops infinitos:

```c
// ANTES (trava o sistema)
while(1) {}

// DEPOIS (panic com informação)
panic("MOD: Caminho inalcançável atingido");
```

### Para polling sem timeout:

```c
// ANDES (trava se hardware falhar)
while (!scancode) {
    scancode = inb(0x60);
}

// DEPOIS (timeout seguro)
uint32_t timeout = 100000;
while (!scancode && timeout--) {
    scancode = inb(0x60);
}
if (!scancode) {
    LOG_WARN("MOD", "Timeout no polling de teclado");
    return 0;
}
```

### Para loops de cadeia de clusters:

```c
// ANTES (loop infinito se FAT corrompida)
while (cluster < 0xFF8) {
    cluster = fat_get_cluster(cluster);
}

// DEPOIS (protegido)
uint32_t safety = 0;
while (cluster < 0xFF8 && safety++ < MAX_CLUSTERS) {
    cluster = fat_get_cluster(cluster);
}
if (safety >= MAX_CLUSTERS) {
    LOG_ERROR("FAT", "Cadeia de clusters corrompida");
    return ERR_DISK;
}
```

---

## Checklist de Verificação

Após implementar as correções, verificar:

- [ ] Todo `kmalloc()` tem NULL check?
- [ ] Todo `while(1)` vazio foi substituído?
- [ ] Todo polling de hardware tem timeout?
- [ ] Todo loop de cadeia de clusters tem limite?
- [ ] Todo buffer de concatenação tem bounds check?
- [ ] Todo `str_insert()` verifica tamanho?
- [ ] O editor verifica `EDITOR_MAX_LINES` antes de escrever?
- [ ] O `fat32_find_in_dir()` não retorna ponteiroAfter-free?
- [ ] O `thread_create()` faz rollback na falha?
- [ ] O `panic()` verifica NULL no message?

---

## Arquivos Afetados

| Arquivo | Qtd Issues | Prioridade |
|---------|-----------|------------|
| `settings.c` | 3 (C1, C2, C3) | Alta |
| `editor.c` | 4 (C7, C8, C9, C10) | Alta |
| `fat12.c` | 3 (C5, C15, C19) | Alta |
| `fat32.c` | 3 (C6, C14, C19) | Alta |
| `process.c` | 1 (C4) | Crítica |
| `thread.c` | 1 (C13) | Média |
| `shell.c` | 2 (C1) | Média |
| `filemanager.c` | 1 (C16) | Média |
| `compress.c` | 2 (C17, C21) | Média |
| `panic.c` | 1 (C11) | Baixa |
| `ac97.c` | 1 (C12) | Baixa |
| `bmp.c` | 1 (C18) | Baixa |
| `speaker.c` | 1 (C20) | Baixa |

---

## Status da implementacao - 2026-07-19

### Corrigido nesta fase

- C2/C3: Settings nao faz mais polling direto do teclado. A IRQ apenas enfileira scancodes e o loop principal despacha os eventos.
- C1: loops terminais de reboot, shutdown e panic agora usam `hlt`; o editor de icones deixou de bloquear o sistema e virou uma maquina de estados.
- C4/C13: `process_create()` e `thread_create()` fazem validacao e rollback completo quando uma alocacao falha.
- C7/C8/C9/C10: editor verifica alocacoes, limites de linha e limite de caracteres; operacoes de merge e quebra de linha nao escrevem fora do buffer.
- C11: `panic()` aceita mensagem nula sem causar uma segunda falha.
- C12: AC97 registra falha de alocacao e mantem a reproducao desativada.
- C14: FAT32 copia a entrada encontrada para uma estrutura de saida antes de liberar o buffer do diretorio.
- C15: cadeias FAT12/FAT32 possuem limite de passos para evitar loops em discos corrompidos.
- C16: caminhos construidos pelo File Manager sao limitados a `FM_MAX_PATH`.
- C17/C21: compressao e descompressao recebem capacidade de destino e rejeitam overflow ou stream truncado.
- C18: BMP valida dimensoes, tabela de cores, offset e tamanho dos pixels antes de copiar ou acessar dados.
- Falhas ATA agora possuem tentativas limitadas e retornam erro ao filesystem sem provocar panic.

### Correcoes de contrato

- `fs_init()`, `fat12_init()` e `fat32_init()` retornam codigo de erro.
- Falhas esperadas de disco, memoria e formato deixam o modulo inativo e permitem que o kernel continue.
- `panic()` permanece reservado para invariantes de paging, IDT, GDT e memoria essencial.

### Itens ainda classificados como terminais intencionais

- Reboot, shutdown e `panic_halt()` interrompem deliberadamente a execucao. Eles nao sao tratados como falhas recuperaveis.
- O loop de desenho de linhas VESA termina pela condicao geometrica e nao e um loop de espera.

### Observacao sobre a auditoria original

As contagens originais de C5, C6 e C12 misturavam pontos ja protegidos com pontos realmente vulneraveis. A correcao foi aplicada aos caminhos de inicializacao que podiam chamar `panic()` e aos buffers sem limite, mantendo os checks existentes que ja eram validos.
