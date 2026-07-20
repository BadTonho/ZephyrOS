# 06 - Gerenciamento de Memória

## Visão Geral

O ZephyrOS gerencia memória em duas camadas:

1. **Física** — Alocador de páginas (bitmap)
2. **Virtual** — Paging (page tables)

## Arquivos

```
src/memory/
├── memory.c         → Alocador de memória física + heap
├── paging.c         → Page tables (memória virtual)
└── compress.c       → Compressão LZSS (compactação de RAM)
```

---

## Mapa de Memória (E820)

O bootloader detecta a memória disponível usando a interrupção `0x15` com `eax=0xE820`.

### Estrutura de Entrada

```c
typedef struct {
    uint64_t base;       // Endereço inicial
    uint64_t length;     // Tamanho em bytes
    uint32_t type;       // 1=livre, 2=reservada, 3=ACPI
    uint32_t acpi;       // Flags ACPI
} __attribute__((packed)) mmap_entry_t;
```

### Tipos de Memória

| Tipo | Descrição |
|------|-----------|
| 1 | Memória livre (usável) |
| 2 | Memória reservada |
| 3 | Memória ACPI Reclaimable |
| 4 | Memória ACPI NVS |
| 5 | Memória danificada |

---

## Bitmap Allocator

O alocador usa um **bitmap** para rastrear quais páginas estão livres ou ocupadas.

### Conceito

Cada página (4 KB) é representada por 1 bit:
- `0` = Livre
- `1` = Ocupada

```
Byte 0: [bit7][bit6][bit5][bit4][bit3][bit2][bit1][bit0]
         pg7   pg6   pg5   pg4   pg3   pg2   pg1   pg0
```

### Alocação

```c
void* pmm_alloc_page(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            bitmap[i / 8] |= (1 << (i % 8));  // Marca como ocupada
            return (void*)(i * PAGE_SIZE);      // Retorna endereço
        }
    }
    return 0;  // Sem memória
}
```

### Liberação

```c
void pmm_free_page(void* addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    bitmap[page / 8] &= ~(1 << (page % 8));  // Marca como livre
}
```

### Alocação Múltipla

```c
void* pmm_alloc_pages(uint32_t count) {
    // Busca 'count' páginas consecutivas livres
    for (uint32_t i = 0; i < total_pages - count; i++) {
        int found = 1;
        for (uint32_t j = 0; j < count; j++) {
            if (bitmap[(i + j) / 8] & (1 << ((i + j) % 8))) {
                found = 0;
                break;
            }
        }
        if (found) { /* marca e retorna */ }
    }
}
```

---

## Heap (kmalloc/kfree)

O heap permite alocar blocos de memória de tamanho arbitrário.

### Estrutura do Bloco

```c
typedef struct heap_block {
    uint32_t size;           // Tamanho do bloco (sem header)
    int free;                // 1=livre, 0=ocupado
    struct heap_block* prev; // Bloco anterior
    struct heap_block* next; // Próximo bloco
} heap_block_t;
```

### Mapa do Heap

```
 HEAP_START (0x100000)
┌──────────────────┐
│  heap_block_t    │ ← size=4096, free=1
│  (cabeçalho)     │
├──────────────────┤
│                  │
│  Dados           │ ← kmalloc retorna este endereço
│                  │
├──────────────────┤
│  heap_block_t    │ ← Próximo bloco
│  (cabeçalho)     │
├──────────────────┤
│  ...             │
└──────────────────┘
```

### kmalloc()

```c
void* kmalloc(uint32_t size) {
    heap_block_t* curr = heap_base;
    while (curr) {
        if (curr->free && curr->size >= size) {
            // Se o bloco é muito grande, divide
            if (curr->size > size + sizeof(heap_block_t) + 16) {
                // Cria novo bloco livre depois
                heap_block_t* new = (void*)curr + sizeof(heap_block_t) + size;
                new->size = curr->size - size - sizeof(heap_block_t);
                new->free = 1;
                new->next = curr->next;
                curr->next = new;
                curr->size = size;
            }
            curr->free = 0;
            return (void*)curr + sizeof(heap_block_t);
        }
        curr = curr->next;
    }
    return 0;  // Sem memória
}
```

### kfree()

```c
void kfree(void* ptr) {
    heap_block_t* block = ptr - sizeof(heap_block_t);
    block->free = 1;

    // Coalescência: junta blocos livres adjacentes
    heap_block_t* curr = heap_base;
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(heap_block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}
```

---

## Paging (Memória Virtual)

O paging permite que cada processo tenha seu próprio espaço de endereçamento.

### Conceito

```
Endereço Virtual → Page Directory → Page Table → Endereço Físico
     32 bits           10 bits          10 bits        12 bits
```

### Page Directory

Cada entrada aponta para uma Page Table:

```c
typedef struct {
    page_entry_t entries[1024];
} __attribute__((aligned(4096))) page_table_t;
```

### Page Table

Cada entrada mapeia uma página virtual para uma física:

```c
typedef struct {
    uint32_t present : 1;   // Página presente na memória
    uint32_t rw : 1;       // 0=leitura, 1=leitura+escrita
    uint32_t user : 1;     // 0=kernel, 1=usuário
    uint32_t accessed : 1;  // Página foi acessada
    uint32_t dirty : 1;    // Página foi escrita
    uint32_t unused : 7;
    uint32_t frame : 20;   // Endereço físico (/frame = frame * 4096)
} __attribute__((packed)) page_entry_t;
```

### Mapeamento

```c
void paging_map_page(uint32_t virtual, uint32_t physical, uint32_t flags) {
    page_entry_t* page = paging_get_page(virtual, 1);
    page->frame = physical / PAGE_SIZE;
    page->present = 1;
    page->rw = (flags & 0x2) ? 1 : 0;
    page->user = (flags & 0x4) ? 1 : 0;
}
```

### Ativação

```c
void paging_switch_directory(page_directory_t* dir) {
    asm volatile("mov %0, %%cr3" : : "r"(dir->physical_addr));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Liga bit PG do CR0
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}
```

---

## Mapa de Memória Final

```
0x00000 - 0x7C00   Bootloader (reusado)
0x7C00  - 0x8000   Boot sector
0x8000  - 0x10000  Mapa E820
0x10000 - 0x80000  Kernel e BSS
0x80000 - 0x81000  Bitmap PMM (tamanho varia com a RAM)
0x90000 - 0xA0000  Kernel stack
0x100000 - 0x200000 Heap (1 MB)
0xB8000 - 0xBFFFF  VGA memory
```

---

## Compressão LZSS (`compress.c`)

Algoritmo de compressão LZSS (Lempel-Ziv-Storer-Szymanski) para compactar dados na memória RAM.

### Arquivo

```
src/memory/compress.c
```

### Algoritmo

LZSS usa um dicionário deslizante para encontrar sequências repetidas:

```
Flag byte (8 bits):
  Bit = 1 → par (posição, comprimento) — referência ao dicionário
  Bit = 0 → literal — byte literal

Par (posição, comprimento):
  2 bytes: [posição 12 bits][comprimento 4 bits]
  posição: offset no dicionário (0-4095)
  comprimento: 3-18 bytes (valor 0-15 + threshold)
```

### Constantes

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| COMPRESS_LZSS_N | 4096 | Tamanho do dicionário |
| COMPRESS_LZSS_F | 18 | Lookahead buffer |
| COMPRESS_LZSS_THRESHOLD | 3 | Mínimo para match |

### API

```c
void compress_init(void);                       // Inicializa
void compress_enable(void);                     // Ativa compressão
void compress_disable(void);                    // Desativa
uint8_t compress_is_enabled(void);              // Verifica estado

int compress_data(src, src_size, dst, dst_size);   // Comprime
int decompress_data(src, src_size, dst, dst_size); // Descomprime
```

### Estatísticas

```c
compress_stats_t* stats = compress_get_stats();
// stats->compression_count     — total de compressões
// stats->total_compressed      — bytes comprimidos
// stats->total_saved           — espaço economizado
// stats->original_size         — tamanho original
// stats->compressed_size       — tamanho comprimido

compress_print_stats();  // Exibe no terminal
```

### Exemplo

```c
uint8_t original[] = "AAAAABBBBBCCCCC";
uint32_t src_size = 15;
uint32_t max_dst = compress_get_max_size(src_size);
uint8_t* compressed = kmalloc(max_dst);
uint32_t dst_size;

compress_data(original, src_size, compressed, &dst_size);
// compressed agora tem ~5 bytes (muito repetitivo)

uint8_t* decompressed = kmalloc(src_size);
uint32_t out_size;
decompress_data(compressed, dst_size, decompressed, &out_size);
```
