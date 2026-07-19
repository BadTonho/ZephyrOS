# 08 - Sistema de Arquivos

## Visão Geral

O ZephyrOS usa um sistema de arquivos com interface unificada que suporta **FAT12** e **FAT32**, além de formatos de arquivo como **BMP** e **WAV**.

## Arquivos

```
src/drivers/
│   └── ata.c         → Driver de disco (leitura/escrita de setores)
src/fs/
│   ├── fat12.c       → Sistema de arquivos FAT12
│   ├── fat32.c       → Sistema de arquivos FAT32
│   ├── fs.c          → Interface unificada FAT12/FAT32
│   ├── bmp.c         → Leitura de imagens BMP
│   └── wav.c         → Leitura de áudio WAV
```

---

## ATA Driver (`ata.c`)

### O que é ATA?

**ATA** (AT Attachment) é o padrão para comunicação com discos rígidos. O ZephyrOS usa o modo **PIO** (Programmed I/O), onde o CPU lê/escreve dados diretamente via portas.

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

---

## FAT32 (`fat32.c`)

Suporte a discos com **FAT32** (File Allocation Table 32-bit).

### Diferenças do FAT12

| Característica | FAT12 | FAT32 |
|---------------|-------|-------|
| Bits por entrada | 12 | 32 (apenas 28 usados) |
| Clusters máximos | 4.096 | ~268 milhões |
| Tamanho máximo | 32 MB | 2 TB |
| Root directory | Área fixa | Cluster encadeado |

### BPB FAT32

```c
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t max_root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_large;
    uint32_t sectors_per_fat;      // FAT32: > 0
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;         // FAT32: cluster do root dir
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    char     reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat32_bpb_t;
```

### Cluster Chain (32-bit)

```c
uint32_t value = fat32_get_cluster(cluster);
// value & 0x0FFFFFFF = próximo cluster
// FAT32_CLUSTER_FREE (0)      = livre
// FAT32_CLUSTER_END (0x0FFFFFFF) = último cluster da chain
// FAT32_CLUSTER_BAD (0x0FFFFFF7) = cluster defeituoso
```

### API

```c
void fat32_init(void);
int  fat32_read_file(filename, buffer, max_size);
int  fat32_write_file(filename, data, size);
int  fat32_delete_file(filename);
int  fat32_list_dir(void);
int  fat32_get_file_count(void);
int  fat32_get_file_info(index, name, size, attr);
```

---

## FS Unificado (`fs.c`)

Interface única que abstrai FAT12 e FAT32, detectando automaticamente o formato do disco.

### Inicialização

```c
fs_init();
// Detecta FAT12 primeiro; se falhar, tenta FAT32
```

### API Unificada

```c
int  fs_read_file(filename, buffer, max_size);
int  fs_write_file(filename, data, size);
int  fs_delete_file(filename);
int  fs_list_dir(void);
int  fs_get_file_count(void);
int  fs_get_file_info(index, name, size, attr);
int  fs_get_info(fs_info_t* info);   // Obtém info do FS ativo
uint8_t fs_get_type(void);           // FS_TYPE_FAT12 ou FS_TYPE_FAT32
```

### Estrutura de Informação

```c
typedef struct {
    uint8_t  type;
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint32_t total_sectors;
    uint32_t free_sectors;
    uint32_t total_clusters;
    uint32_t free_clusters;
    char     label[12];
} fs_info_t;
```

---

## BMP (`bmp.c`)

Leitura e renderização de imagens **BMP** (Bitmap).

### Formatos Suportados

| BPP | Tipo | Paleta |
|-----|------|--------|
| 1 | Monocromático | 2 cores |
| 4 | 16 cores | 16 entradas |
| 8 | 256 cores | 256 entradas |
| 24 | True color | Sem paleta |

### Estrutura

```c
typedef struct {
    char     signature[2];      // "BM"
    uint32_t file_size;
    uint32_t data_offset;
} bmp_file_header_t;

typedef struct {
    uint32_t header_size;
    int32_t  width, height;
    uint16_t planes, bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    // ...
} bmp_info_header_t;

typedef struct {
    uint32_t width, height, bpp;
    uint8_t* pixel_data;
    bmp_color_table_t* color_table;
    int initialized;
} bmp_image_t;
```

### API

```c
int  bmp_load(raw_data, size, &image);      // Carrega BMP da memória
void bmp_draw(&image, x, y);                 // Renderiza na tela (VESA)
void bmp_draw_scaled(&image, x, y, scale);   // Renderiza com escala
void bmp_free(&image);                       // Libera memória
```

### Exemplo

```c
uint8_t* data = fs_read_file("IMAGEM.BMP", buffer, 65536);
bmp_image_t img;
if (bmp_load(data, size, &img) == 0) {
    bmp_draw(&img, 10, 10);   // Desenha no modo VESA
    bmp_free(&img);
}
```

---

## WAV (`wav.c`)

Leitura e reprodução de arquivos de áudio **WAV** (Waveform Audio).

### Formato

O WAV usa o container RIFF com chunks:

```
RIFF header: "RIFF" + size + "WAVE"
  fmt chunk: "fmt " + size + audio_format + channels + sample_rate + ...
  data chunk: "data" + size + PCM data
```

### Estrutura

```c
typedef struct {
    uint16_t audio_format;     // 1 = PCM
    uint16_t num_channels;     // 1 = mono, 2 = stereo
    uint32_t sample_rate;      // 44100, 22050, etc.
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;  // 8 ou 16
    uint8_t* data;
    uint32_t data_size;
    int initialized;
} wav_file_t;
```

### API

```c
void wav_init(void);
int  wav_load(raw_data, size, &wav);     // Carrega WAV da memória
void wav_play(&wav);                      // Reproduz via AC97
void wav_free(&wav);                      // Libera memória
uint32_t wav_get_duration_ms(&wav);       // Duração em ms
```

### Exemplo

```c
uint8_t* data = fs_read_file("MUSICA.WAV", buffer, 65536);
wav_file_t wav;
if (wav_load(data, size, &wav) == 0) {
    wav_play(&wav);   // Toca o áudio
    wav_free(&wav);
}
```
