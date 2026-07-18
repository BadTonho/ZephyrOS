# 08 - Sistema de Arquivos

## Visão Geral

O MiniOS usa **FAT12** (File Allocation Table 12-bit), o mesmo formato de disquetes de 1.44 MB.

## Arquivos

```
src/drivers/
│   └── ata.c         → Driver de disco (leitura/escrita de setores)
src/fs/
│   └── fat12.c       → Sistema de arquivos FAT12
```

---

## ATA Driver (`ata.c`)

### O que é ATA?

**ATA** (AT Attachment) é o padrão para comunicação com discos rígidos. O MiniOS usa o modo **PIO** (Programmed I/O), onde o CPU lê/escreve dados diretamente via portas.

### Portas de Comunicação

| Porta | Nome | Função |
|-------|------|--------|
| 0x1F0 | DATA | Dados (16-bit, leitura/escrita) |
| 0x1F1 | ERROR | Código de erro (leitura) |
| 0x1F2 | SECCOUNT | Número de setores |
| 0x1F3 | LBA_LOW | LBA bits 0-7 |
| 0x1F4 | LBA_MID | LBA bits 8-15 |
| 0x1F5 | LBA_HIGH | LBA bits 16-23 |
| 0x1F6 | DRIVE | Drive + LBA bits 24-27 |
| 0x1F7 | STATUS | Status (leitura) / Comando (escrita) |

### Identificando o Disco

```c
outb(0x1F7, 0xEC);  // Comando IDENTIFY
// Espera DRQ (Data Request)
// Lê 256 words (512 bytes) de dados
// Contém: modelo, tamanho, capacidade
```

### Lendo Setores

```c
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    outb(0x1F2, count);           // Quantidade de setores
    outb(0x1F3, lba & 0xFF);      // LBA bits 0-7
    outb(0x1F4, (lba >> 8) & 0xFF);  // LBA bits 8-15
    outb(0x1F5, (lba >> 16) & 0xFF); // LBA bits 16-23
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F7, 0x20);            // Comando READ

    for (int s = 0; s < count; s++) {
        // Espera DRQ
        while (!(inb(0x1F7) & 0x08));

        // Lê 256 words (512 bytes)
        for (int i = 0; i < 256; i++) {
            uint16_t data = inw(0x1F0);
            buffer[s * 512 + i * 2] = data & 0xFF;
            buffer[s * 512 + i * 2 + 1] = (data >> 8) & 0xFF;
        }
    }
}
```

---

## FAT12 (`fat12.c`)

### Estrutura do Disco

```
Setor 0:        Boot sector (contém BPB)
Setor 1-9:      FAT (File Allocation Table)
Setor 10-33:    Root directory (224 entradas)
Setor 34-2879:  Data area (clusters)
```

### BPB (BIOS Parameter Block)

O boot sector contém informações sobre o formato:

```c
typedef struct {
    uint8_t  boot_jump[3];       // Jump para bootloader
    char     oem[8];             // Nome do OEM
    uint16_t bytes_per_sector;   // 512
    uint8_t  sectors_per_cluster;// 1
    uint16_t reserved_sectors;   // 1
    uint8_t  num_fats;           // 2
    uint16_t root_entries;       // 224
    uint16_t total_sectors;      // 2880
    uint8_t  media_type;         // 0xF0 (disquete)
    uint16_t sectors_per_fat;    // 9
    // ...
} __attribute__((packed)) fat12_bpb_t;
```

### FAT (File Allocation Table)

A FAT é uma tabela que mapeia clusters:

```
Cluster 0: (reservado)
Cluster 1: (reservado)
Cluster 2: Próximo cluster do arquivo
Cluster 3: Fim do arquivo (0xFF8)
...
```

Cada entrada tem 12 bits (1.5 bytes):

```c
uint16_t fat12_get_cluster(uint16_t cluster) {
    uint32_t offset = cluster + (cluster / 2);
    uint16_t value = *(uint16_t*)(fat + offset);
    if (cluster & 1)
        value >>= 4;        // Cluster ímpar: pega bits altos
    else
        value &= 0x0FFF;    // Cluster par: pega bits baixos
    return value;
}
```

### Entrada de Diretório

Cada arquivo tem uma entrada de 32 bytes:

```c
typedef struct {
    char     name[8];        // Nome (8 caracteres)
    char     ext[3];         // Extensão (3 caracteres)
    uint8_t  attributes;     // Atributos (0x20 = arquivo)
    uint16_t cluster_low;    // Cluster inicial (baixo)
    uint32_t file_size;      // Tamanho em bytes
} __attribute__((packed)) fat12_dir_entry_t;
```

### Lendo um Arquivo

```c
int fat12_read_file(const char* filename, uint8_t* buffer, uint32_t max_size) {
    // 1. Busca o arquivo no root directory
    fat12_dir_entry_t* entry = fat12_find_entry(filename);
    if (!entry) return -1;

    // 2. Sigue a cadeia de clusters
    uint16_t cluster = entry->cluster_low;
    while (cluster < 0xFF8) {
        // 3. Calcula LBA do cluster
        uint32_t lba = data_start + (cluster - 2) * sectors_per_cluster;

        // 4. Lê os setores
        ata_read_sectors(lba, sectors_per_cluster, buffer);

        // 5. Próximo cluster na FAT
        cluster = fat12_get_cluster(cluster);
        buffer += bytes_per_cluster;
    }
}
```

### Escrevendo um Arquivo

```c
int fat12_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    // 1. Busca ou cria entrada no root directory
    // 2. Aloca clusters livres na FAT
    // 3. Escreve dados nos clusters
    // 4. Atualiza a FAT
    // 5. Atualiza o root directory
    // 6. Salva FAT e root dir no disco
}
```

### Listando Diretório

```c
int fat12_list_dir(void) {
    for (int i = 0; i < root_entries; i++) {
        if (root_dir[i].name[0] == 0x00) break;   // Fim
        if (root_dir[i].name[0] == 0xE5) continue; // Deletado
        if (root_dir[i].attributes & 0x08) continue; // Volume

        // Mostra nome + tamanho
    }
}
```

---

## Nomes de Arquivo

FAT12 usa o formato **8.3**: 8 caracteres para o nome + 3 para a extensão.

```
NOME    .TXT    → 11 bytes
ARQUIVO .DAT    → 11 bytes
```

Espaços são usados para preencher:
```
"TESTE.TXT" → "TESTE   TXT"
```

O shell converte automaticamente para maiúsculas.
