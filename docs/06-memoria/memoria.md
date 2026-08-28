# 06 - Gerenciamento de Memória

## Visão Geral

O ZephyrOS gerencia memória em duas camadas:

1. **Física** — Alocador de páginas (bitmap)
2. **Virtual** — Paging (page tables)

## Arquivos

```
src/memory/
├── memory.c         → Alocador de memória física + heap
├── slab.c           → Caches SLAB/SLUB de objetos fixos
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

### Guardas e estatisticas K3

O PMM mantem um segundo bitmap interno de propriedade. Uma pagina so pode ser
liberada por `pmm_free_page()` ou `pmm_free_pages()` quando foi previamente
entregue pelo proprio PMM. Ponteiros nulos, desalinhados, fora da RAM, reservas
do kernel, intervalos invalidos e double-free sao recusados, registrados e nao
alteram o bitmap principal. A busca de intervalos contiguos tambem rejeita
contagem zero ou maior que a RAM e inclui o ultimo intervalo possivel.

`memory_pmm_stats_t` informa paginas atualmente entregues, falhas de alocacao
e liberacoes recusadas. O bitmap adicional e reservado durante `memory_init()`
e nunca volta a ser memoria livre. Os dois bitmaps ocupam a faixa segura entre
o fim do kernel e a stack inicial do sistema. As paginas da stack tambem sao
reservadas no PMM; a inicializacao valida a cobertura E820 de toda a memoria
baixa reservada e falha de forma controlada em vez de sobrepor memoria critica.

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
 HEAP_START (0x01000000)
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

### Diagnostico K3 do heap

O heap continua first-fit com coalescencia. `memory_heap_stats_t` inclui
blocos livres/ocupados, maior bloco livre, falhas e rejeicoes de `kfree`.
Fragmentacao externa e `(livre_total - maior_bloco_livre) / livre_total`; sem
espaco livre, o resultado e `0`. A leitura das estatisticas e limitada por
faixa e por encadeamento fisico esperado: metadata ou links invalidos deixam
o estado de integridade invalido e retornam controle ao chamador sem percorrer
um ciclo corrompido.

---

## Alocador SLAB/SLUB (MM1)

`kmem_cache_init()` registra os metadados estáticos dos caches. Cada cache pode
usar até 128 slabs globais, com páginas de 4 KiB obtidas por
`pmm_alloc_pages()` somente depois de `paging_init()`. O registro suporta até
16 caches e cada slab comporta pelo menos oito e no máximo 128 objetos.

`kmem_cache_create()` valida tamanho e alinhamento potência de dois até
`PAGE_SIZE`. A capacidade usa páginas arredondadas e os objetos são mantidos
em listas `full`, `partial` e `empty`, com bitmap de ocupação e freelist.
Slabs vazios permanecem reservados para reutilização até
`kmem_cache_destroy()`; destruição com objetos ativos, ponteiros externos e
double free são recusados, contabilizados e registrados.

`process_t`, `thread_t`, `file_t`, `vnode_t` e os pacotes internos RX/TX da
Ethernet usam caches dedicados. Stacks continuam em `kmalloc` por possuírem
tamanho variável, canários e limites próprios. `slabinfo`, `slabtest`,
`health`, `memcheck`, `schedcheck`, `regcheck full` e `vfs_validate_state()`
expõem ou validam o estado do alocador. A ABI ring 3 e as assinaturas
funcionais existentes permanecem compatíveis.

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

Durante `paging_init()`, as faixas supervisoras, toda a RAM fisica livre e o
framebuffer disponivel continuam mapeados por identidade. O bootstrap preenche
as PTEs diretamente por Page Table, sem executar a validacao de diretorios de
usuario para cada pagina. O caminho publico `paging_map_page()` permanece
reservado aos mapeamentos normais e de ring 3.

A pagina `0x2000–0x2FFF` permanece mapeada por identidade somente como
supervisora enquanto o kernel consome o contexto fixo de boot. Ela contem o
bloco VESA e o handoff `ZSBH` em `0x2800`, usado depois da inicializacao
essencial para confirmar o slot. A pagina zero, a pagina do mapa E820 em
`0x3000` e as demais lacunas baixas continuam ausentes; diretorios de usuario
nao recebem a flag `USER` para essa pagina.

`paging_boot_stats_t`, consultado por `paging_get_boot_stats()`, registra o
numero de paginas identity-mapped, Page Tables criadas e ticks consumidos pela
inicializacao. A consulta e somente diagnostica e aparece em `kmetrics`.

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
0x00000 - 0x2000   Bootloader e buffers reutilizados
0x2000  - 0x3000   Contexto VESA e ZSBH, supervisor-only
0x3000  - 0x5000   Mapa E820 e contador
0x5000  - 0x10000  Segundo estagio do bootloader
0x88000 - 0x98000  Bitmaps PMM (tamanho varia com a RAM)
0x98000 - 0x9F000  Kernel stack (28 KiB)
0x9F000 - 0xA0000  Margem reservada para BIOS/EBDA
0xA0000 - 0xC0000  Memoria de video VGA
0x100000 - 0x800000 Kernel e BSS (7 MiB)
0x800000 - 0x1000000 Janela virtual ZAPP e reserva fisica
0x1000000 - 0x1400000 Heap (4 MiB)
0x1400000 em diante RAM fisica livre mapeada por identidade
```

O PMM começa considerando todas as páginas ocupadas, libera apenas entradas
E820 tipo 1 e volta a reservar toda a faixa abaixo de `0x01400000`. Isso
preserva os endereços virtuais ZAPP existentes e garante que cada página
física entregue ao kernel também tenha um mapeamento supervisor por
identidade. O sistema requer no mínimo 32 MiB de RAM.

---

## Espaço de usuário

Processos ZAPP em ring 3 recebem um diretório de páginas isolado. O kernel,
heap, VGA e framebuffer permanecem supervisor (`user = 0`). A primeira faixa
de usuário é fixa e usada apenas pelo carregador:

```text
0x00800000  código, uma página, somente leitura em ring 3
0x00801000  dados, uma página gravável
0x00802000  informações de lançamento e argumentos, uma página gravável
0x00C00000  stack de usuário, uma página gravável
```

Na SYNC4, os 16 bytes finais da página de lançamento, em `0x00802FF0`, são
reservados ao trampoline de `signal_return`. O kernel restaura esses bytes em
cada entrega antes de montar o frame na stack ring3; o restante da estrutura
de argumentos e seus offsets permanece inalterado.

A página de lançamento contém `app_launch_info_t` da App API `0.4`, com até
oito argumentos representados por offsets e comprimentos relativos ao texto
bruto. Ela evita expor ponteiros do kernel e preserva a página de dados das
imagens ZAPP antigas.

Diretorios de usuario sao registrados quando criados. Somente um diretorio
registrado pode ser liberado pelo liberador de usuario; diretorio ativo, do
kernel ou desconhecido e recusado. O liberador generico tambem recusa um
diretorio registrado. `paging_user_stats_t` informa diretorios e paginas
ativos, criados, liberados e rejeicoes; os deltas aparecem em `kmetrics`.

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
