# Relatório de Auditoria — ZephyrOS

**Data:** 2026-07-21
**Escopo:** Análise completa do código-fonte em busca de erros, falhas, melhorias e otimizações.

---

## Resumo

| Severidade | Qtd |
|------------|-----|
| CRÍTICO    | 2   |
| ALTO       | 8   |
| MÉDIO      | 17  |
| BAIXO      | 11  |
| **Total**  | **38** |

---

## CRÍTICO (2)

### 1. Registros AC97 sobrepostos
- **Arquivo:** `src/include/drivers/ac97.h`
- **Problema:** `AC97_REG_POWER` e `AC97_REG_PCM_FRONT_DAC_RATE` ambos definidos como `0x2C`. Sobrescrever um corrompe o outro. Outros conflitos: `0x28` (DMA vs EXTENDED), `0x2A` (EXT_AUDIO vs RSTATUS), `0x02` e `0x04` (NMI vs MASTER_VOL/MONO).
- **Correção:** Seguir o spec AC97严格按照: usar offsets corretos para cada registrador.

### 2. `fat12_find_in_dir` retorna ponteiro para variável `static`
- **Arquivo:** `src/fs/fat12.c:554`
- **Problema:** Toda chamada sobrescreve o mesmo buffer estático. Chamadas aninhadas corrompem dados anteriores.
- **Correção:** Alocar via `kmalloc` e retornar (exigindo `kfree` pelo chamador), ou usar buffer fornecido pelo chamador (como `fat32_find_in_dir` faz).

---

## ALTO (8)

### 3. `switch.asm` não salva `eax`, `ecx`, `edx`
- **Arquivo:** `src/kernel/switch.asm:18-20`
- **Problema:** O context switch não salva todos os registradores definidos em `process_context_t`. Pode causar corrupção silenciosa de dados entre processos.
- **Correção:** Salvar todos os registradores do `process_context_t`, incluindo `eax`, `ecx`, `edx`.

### 4. FAT32 ignora cache em RAM
- **Arquivo:** `src/fs/fat32.c:35,49`
- **Problema:** `fat32_get_cluster` e `fat32_set_cluster` fazem I/O direto do disco (`ata_read_sectors`) a cada chamada, ignorando que a FAT inteira já foi carregada em RAM no init. Performance catastrófica.
- **Correção:** Ler de `fs.fat[cluster]` e modificar o array em RAM, sincronizando com disco ao final de operações.

### 5. Funções `inb/outb/inw/outw` duplicadas em 8 arquivos
- **Arquivos:** `ata.c`, `mouse.c`, `speaker.c`, `idt.c`, `keyboard.c`, `pci.c`, `ac97.c`, `timer.c`
- **Problema:** ~26 definições duplicadas do mesmo inline assembly de I/O de porta.
- **Correção:** Criar um header único `core/portio.h` com `static inline` e incluir em todos os drivers.

### 6. Funções de string duplicadas em 6+ arquivos
- **Arquivos:** `wm.c`, `settings.c`, `shell.c`, `mediaplayer.c`, `editor.c`, `filemanager.c`, `taskbar.c`
- **Problema:** `str_copy`, `str_len`, `int_to_str`, `str_compare` redefinidas como `static` em cada arquivo. ~13+ definições duplicadas.
- **Correção:** Consolidar em `core/string.c` e `core/string.h` (que já existe mas não expõe todas essas funções).

### 7. Código de conversão 8.3 duplicado ~8 vezes em `fat12.c`
- **Arquivo:** `src/fs/fat12.c` (linhas 173-196, 617-637, 768-787, 832-851, 1015-1034, 1155-1174)
- **Problema:** A lógica de preencher nome FAT com espaços e uppercase está copiada em 6+ lugares diferentes.
- **Correção:** Criar uma função `fat12_str_to_name` (como `fat32_str_to_name`) e usar em todos os lugares.

### 8. IPC sem proteção adequada
- **Arquivo:** `src/process/ipc.c`
- **Problema:** Spinlock não desabilita interrupts. Timer interrupt durante operação multi-step pode corromper a fila.
- **Correção:** Usar `cli/sti` ao redor das operações na fila IPC, ou usar operações atômicas.

### 9. `fat32_create_dir_entry` — loop redundante de cópia de FAT
- **Arquivo:** `src/fs/fat32.c:903`
- **Problema:** Após `fat32_set_cluster` já gravar a entrada, um loop adicional copia a FAT inteira desnecessariamente.
- **Correção:** Remover o loop redundante.

### 10. `fat12_write_file` — cópia de nome sem formato 8.3
- **Arquivo:** `src/fs/fat12.c:268`
- **Problema:** `kmemcpy(entry->name, filename, 11)` copia bytes direto sem separar `name[8]` e `ext[3]`.
- **Correção:** Usar conversão para formato 8.3 antes de copiar.

---

## MÉDIO (17)

### 11. `bmp_draw` / `bmp_draw_scaled` — Sem clipping de bounds
- **Arquivo:** `src/fs/bmp.c:180-204`
- **Problema:** `vesa_put_pixel(x + col, y + row, color)` pode escrever fora do framebuffer se coordenadas excederem a resolução.
- **Correção:** Adicionar clipping antes de cada `vesa_put_pixel`.

### 12. Heap fixo de 4 MiB pode ser insuficiente
- **Arquivo:** `src/include/core/memory.h:13`
- **Problema:** `HEAP_SIZE 0x400000` (4 MiB). Com VESA 1024x768x32, o backbuffer sozinho consome ~3 MiB. FAT32 carrega a FAT inteira em RAM. Com múltiplos apps, vai faltar.
- **Correção:** Considerar heap dinâmico baseado no mapa de memória do BIOS (E820), ou aumentar significativamente.

### 13. `fat12_read_file_at` / `fat32_read_file_at` — Buffer sem limite
- **Arquivos:** `src/fs/fat12.c:743`, `src/fs/fat32.c:773`
- **Problema:** `char filename[13]` sem verificação de limite na cópia. Componente de path >12 caracteres causa overflow.
- **Correção:** Adicionar limite: `if (fi >= 12) break;`

### 14. `fat12_write_file_in_dir` — `dir_path[256]` sem verificação
- **Arquivo:** `src/fs/fat12.c:742`
- **Problema:** Se `path` tiver mais de 255 caracteres, há overflow no buffer.
- **Correção:** Adicionar `if (len >= 255) return -1;`

### 15. `fat32_read_file` — Só busca no root directory
- **Arquivo:** `src/fs/fat32.c:212-215`
- **Problema:** Sempre busca em `fs.root_cluster`. Arquivos em subdiretórios não são encontrados.
- **Correção:** Depreciar em favor de `fat32_read_file_at`, ou adicionar path resolution.

### 16. `processes[]` e `process_count` expostos globalmente
- **Arquivos:** `src/include/process/process.h:91-92`
- **Problema:** Array global e contador são `extern`, permitindo que qualquer módulo acesse e modifique diretamente a tabela de processos.
- **Correção:** Tornar `static` em `process.c` e criar funções accessor.

### 17. `str_copy` em `filemanager.c` sem limite de tamanho
- **Arquivo:** `src/filemanager/filemanager.c:110`
- **Problema:** Diferente das versões em `editor.c` e `mediaplayer.c` que aceitam `uint32_t max`.
- **Correção:** Adicionar parâmetro de limite ou usar a versão com limite.

### 18. Tipos de retorno inconsistentes
- **Arquivos:** Vários (`fat12.c`, `fat32.c`)
- **Problema:** Algumas funções retornam `-1`, outras usam códigos de erro `ERR_*`. Sem padronização.
- **Correção:** Usar sempre os códigos de erro definidos em `errors.h`.

### 19. `NULL` redefinido em `settings.c`
- **Arquivo:** `src/settings/settings.c:21`
- **Problema:** `#define NULL ((void*)0)` é redefinido apesar de já estar em `types.h`.
- **Correção:** Remover a redefinição; incluir `types.h`.

### 20. `sti` antes de `iret` em `irq.asm`/`isr.asm`
- **Arquivos:** `src/drivers/irq.asm:66`, `src/drivers/isr.asm:91`
- **Problema:** `sti` antes de `iret` é redundante (o `iret` já restaura `EFLAGS` com `IF`) e cria uma janela de vulnerabilidade.
- **Correção:** Remover o `sti`.

### 21. `jmp ecx` sem validação em `switch.asm`
- **Arquivo:** `src/kernel/switch.asm:53`
- **Problema:** Se o `eip` restaurado for inválido (processo destruído/corrompido), o sistema entra em estado indefinido.
- **Correção:** Validar `eip` antes do jump, ou confiar que o scheduler sempre fornece valores válidos.

### 22. Makefile com caminhos hardcoded
- **Arquivo:** `Makefile:6-9`
- **Problema:** Caminhos como `C:\Users\Admin\AppData\...` e `D:\code\...` são específicos de uma máquina.
- **Correção:** Usar variáveis de ambiente ou paths relativos detectados pelo make.

### 23. LDFLAGS `-m elf_i386` conflita com `OUTPUT_FORMAT(binary)`
- **Arquivo:** `Makefile:13`, `src/linker.ld:1`
- **Problema:** O LD usa `-m elf_i386` como formato de output, mas `linker.ld` especifica `OUTPUT_FORMAT(binary)`.
- **Correção:** Verificar se o output final é binário puro; se sim, remover `-m elf_i386`.

### 24. FAT32 busca sequencial por cluster livre
- **Arquivo:** `src/fs/fat32.c`
- **Problema:** `fat32_find_free_cluster` percorre toda a FAT sequencialmente.
- **Correção:** Manter um cache do último cluster livre conhecido, ou usar bitmaps.

### 25. BMP renderiza pixel a pixel sem batching
- **Arquivo:** `src/fs/bmp.c:183-189`
- **Problema:** Cada pixel é escrito individualmente via `vesa_put_pixel`. Para imagens grandes, extremamente lento.
- **Correção:** Usar `kmemcpy` direto no framebuffer backbuffer quando possível.

### 26. FAT32 `list_dir` aloca/libera `cluster_buf` a cada iteração
- **Arquivo:** `src/fs/fat32.c:413-483`
- **Problema:** Em cada iteração do loop de clusters, aloca e libera `kmalloc(cluster_size)`. Para diretórios grandes, causa fragmentação do heap.
- **Correção:** Alocar uma vez antes do loop e reutilizar.

---

## BAIXO (11)

### 27. `entry.asm` — Limpeza da BSS ineficiente
- **Arquivo:** `src/kernel/entry.asm:16-20`
- **Problema:** `rep stosb` limpa byte a byte. `rep stosd` seria 4x mais rápido.
- **Correção:** Usar `rep stosd` com `ecx` dividido por 4.

### 28. `stage2.asm` — Sem mensagem de warning se VESA não suportado
- **Arquivo:** `src/boot/stage2.asm:106-145`
- **Problema:** Se o modo VESA não for suportado, o stage2 silenciosamente desativa VESA sem aviso visível.
- **Correção:** Adicionar mensagem de warning antes de desabilitar.

### 29. `stage2.asm` — CHS overflow potencial
- **Arquivo:** `src/boot/stage2.asm:153-165`
- **Problema:** Conversão LBA→CHS não verifica se o resultado cabe nos limites de C/H/S.
- **Correção:** Adicionar verificação de limite ou usar LBA mode (INT 13h extensions).

### 30. `wav_init()` e `bmp_init()` são funções vazias
- **Arquivos:** `src/fs/wav.c:24`, `src/fs/bmp.c:20`
- **Problema:** Funções de inicialização sem implementação.
- **Correção:** Remover ou implementar.

### 31. `str_len` em `filemanager.c` retorna `int` em vez de `uint32_t`
- **Arquivo:** `src/filemanager/filemanager.c:104`
- **Problema:** Tipo de retorno inconsistente com outras implementações.
- **Correção:** Padronizar.

### 32. `wav_get_duration_ms` — Overflow aritmético
- **Arquivo:** `src/fs/wav.c:94`
- **Problema:** `(wav->data_size * 1000)` pode estourar `uint32_t` para arquivos WAV grandes (>4.2MB).
- **Correção:** Usar `wav->data_size / wav->byte_rate * 1000 + (wav->data_size % wav->byte_rate * 1000) / wav->byte_rate`.

### 33. `fat12_delete_file_in_dir` — `cluster_buf` alocado desnecessariamente
- **Arquivo:** `src/fs/fat12.c:1037`
- **Problema:** `cluster_buf` é alocado mesmo quando `dir_cluster == 0` (root não precisa dela).
- **Correção:** Mover a alocação para depois do check `dir_cluster == 0`.

### 34. Sem flags de otimização no Makefile
- **Arquivo:** `Makefile:12`
- **Problema:** `CFLAGS` não inclui `-O2` ou `-Os`.
- **Correção:** Adicionar `-Os` para reduzir tamanho do binário do kernel.

### 35. Sem verificação de warnings como erro
- **Arquivo:** `Makefile:12`
- **Problema:** `-Wall -Wextra` estão presentes mas sem `-Werror`.
- **Correção:** Adicionar `-Werror` no Makefile ou em configuração de CI.

### 36. `fat32_get_cluster` — Leitura pode ultrapassar buffer
- **Arquivo:** `src/fs/fat32.c:45`
- **Problema:** `*(uint32_t*)(buf + entry_offset)` — se `entry_offset > 508`, leitura de 4 bytes ultrapassa o buffer de 512 bytes.
- **Correção:** `if (entry_offset > 512 - 4)` ler byte a byte.

### 37. Código de impressão de tamanho duplicado
- **Arquivos:** `src/fs/fat32.c:453-464`, `src/fs/fat12.c`
- **Problema:** Conversão de `uint32_t` para string decimal manual, duplicada em múltiplos arquivos.
- **Correção:** Usar `int_to_str` de `core/string.c`.

---

## Top 5 Prioridades para Correção

| # | Problema | Impacto | Arquivo |
|---|----------|---------|---------|
| 1 | Registros AC97 conflitantes | Corrupção de áudio / travamento do codec | `ac97.h` |
| 2 | Context switch não salva registradores | Corrupção silenciosa entre processos | `switch.asm` |
| 3 | FAT32 ignora cache em RAM | Performance catastrófica em I/O | `fat32.c` |
| 4 | I/O port functions duplicadas em 8 arquivos | Manutenção impossível | vários drivers |
| 5 | `fat12_find_in_dir` retorna static | Bug clássico de reentrância | `fat12.c` |
