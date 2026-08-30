# 08 - Sistema de Arquivos

## Visão Geral

O ZephyrOS usa um sistema de arquivos com interface unificada que suporta **FAT12** e **FAT32**, além de formatos de arquivo como **BMP** e **WAV**.

As funcoes `fat12_get_free_clusters()` e `fat32_get_free_clusters()` contam
os clusters livres do volume montado. A camada unificada propaga esse dado em
`fs_get_info()` como `free_clusters` e `free_sectors`, permitindo que servicos
internos facam pre-validacao de espaco antes de iniciar uma escrita composta.

## Arquivos

```
src/drivers/
│   └── ata.c         → Driver de disco (leitura/escrita de setores)
src/fs/
│   ├── block.c       → Dispositivos de bloco unificados (ATA e USB MSC)
│   ├── fat12.c       → Sistema de arquivos FAT12
│   ├── fat32.c       → Sistema de arquivos FAT32
│   ├── fs.c          → Interface unificada FAT12/FAT32
│   ├── storage.c     → Inventário de volumes ATA/USB e volume de sistema
│   ├── file_index.c  → Indice global cooperativo em RAM
│   ├── bmp.c         → Leitura de imagens BMP
│   └── wav.c         → Leitura de áudio WAV
```

---

### Procfs PROC2

O VFS monta automaticamente o pseudo-filesystem somente leitura `procfs` em
`/proc`. A montagem e pinned, nao consome Storage e nao pode ser desmontada.
Os nos globais seguem a ordem `uptime`, `meminfo`, `cpuinfo`, `version` e
`cmdline`; diretorios PID sao enumerados numericamente e listam `status`,
`cmdline` e `maps`.

Cada `open()` gera um snapshot ASCII de ate 16 KiB, pertencente ao contexto do
`file_t`. `read()` usa `file_t.offset`, aceita blocos parciais e retorna zero
no EOF; `lseek()` aceita `SET`, `CUR` e `END` dentro do snapshot. Nenhuma
leitura retem ponteiro de processo, VMA ou tabela interna. Assim, remocao e
reutilizacao de PID nao invalidam descritores ja abertos; uma nova abertura
valida a geracao composta e retorna `ERR_NOT_FOUND` quando o no nao existe.

`/proc/uptime`, `/proc/meminfo`, `/proc/cpuinfo`, `/proc/version`,
`/proc/cmdline`, `/proc/<pid>/status`, `/proc/<pid>/cmdline` e
`/proc/<pid>/maps` sao publicos e nao gravaveis. Erros de serializacao,
memoria e churn de VMAs retornam `ERR_OVERFLOW`, `ERR_MEM` e `ERR_AGAIN`,
sem truncamento ou referencia residual.

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

## Storage EP2 (`storage.c`)

Storage e a camada orientada a volume sobre ATA/USB. O artefato principal
`build\\zephyros.img` e hibrido: preserva o FAT12 bruto para boot e recuperacao
e publica um volume FAT32 `ZEPHYROS` iniciado no LBA 8192. O FAT12 continua
disponivel por `legacy:/`; caminhos sem prefixo usam o FAT32 do sistema quando
ele estiver montado. O registro suporta ate `BLOCK_MAX_DEVICES` discos, 16
volumes e quatro montagens simultaneas contando boot e sistema. Getters
retornam sempre copias.

Os discos ATA usam IDs `ata0` a `ata3`. Um superfloppy FAT reconhecido antes do
MBR usa `ataNraw`; as quatro entradas primarias MBR usam `ataNp1` a
`ataNp4`. Discos USB MSC usam os IDs estaveis `usb-ms-BB:DD.F-pN-aN-l0` e
os mesmos sufixos de volume. O volume de boot e fixo e nao pode ser
desmontado. O FAT32 com label `ZEPHYROS` e montado automaticamente, fixado e
gravavel quando o provedor permite escrita. Volumes externos continuam
desmontados por padrao; FAT12 e USB permanecem somente-leitura, enquanto FAT32
ATA pode ser gravavel quando o provedor permite escrita.

```c
int storage_get_status(storage_status_t* out_status);
int storage_refresh(void);
int storage_get_volume_at(uint8_t index, storage_volume_t* out_volume);
int storage_mount(const char* id);
int storage_unmount(const char* id);
int storage_list_dir(const char* id, const char* path,
                     storage_dir_entry_t* entries, uint32_t capacity,
                     uint32_t* out_count);
int storage_read_file_range(const char* id, const char* path,
                            uint32_t offset, uint8_t* buffer,
                            uint32_t max_size, uint32_t* out_read);
int storage_list_dir_long(const char* id, const char* path,
                          storage_long_dir_entry_t* entries,
                          uint32_t capacity, uint32_t* out_count);
int storage_write_file(const char* id, const char* path,
                       const uint8_t* data, uint32_t size,
                       uint8_t attributes);
int storage_atomic_write_file(const char* id, const char* path,
                              const uint8_t* data, uint32_t size,
                              uint8_t attributes, storage_atomic_mode_t mode);
int storage_delete_file(const char* id, const char* path);
int storage_rename_file(const char* id, const char* path,
                        const char* new_name);
int storage_get_free_space(const char* id, uint32_t* out_free_sectors,
                           uint32_t* out_free_clusters);
int storage_check(const char* id);
```

As escritas FAT32 reservam clusters, gravam dados, sincronizam as duas FATs e
publicam a entrada LFN/8.3 por ultimo; em falha a cadeia nova e liberada. O
streaming usa buffer limitado e nunca carrega a imagem inteira. A descoberta le
somente quatro
particoes MBR primarias e isola flag invalida, overflow, limite fora do disco
e sobreposicao. No mount, o BPB e relido e valida assinatura `55AA`, setor de
512 bytes, cluster em potencia de dois, FAT suficiente, geometria/raiz e
limite da particao. A montagem automatica aceita exatamente um FAT32 com label
`ZEPHYROS`; volumes ambiguos nao sao montados. FAT16 e formatos desconhecidos
retornam `ERR_UNAVAILABLE`.

O backend FAT32 usa buffers setoriais fixos, clusters de 4 setores na imagem
hibrida de 256 MiB, nomes longos LFN em UTF-16LE,
aliases 8.3, comparacao ASCII sem diferenciar maiusculas/minusculas e
comparacao exata para UTF-8 nao ASCII. O limite de nome e 255 bytes, o caminho
tem ate 256 bytes e cada listagem tem ate 64 entradas. GPT, EBR, LBA48,
particoes logicas e hot-plug ficam fora desta etapa. Journaling e filesystem
nativo permanecem posteriores. Na EP9.4B, os componentes operacionais
autenticados usam o FAT32, enquanto bootstrap e recuperacao permanecem fixos.

### Escritor transacional de imagem

`storage_transaction_writer_begin/write/finish/abort` grava arquivos grandes
em blocos limitados usando aliases temporario e final informados pelo chamador.
O alias final so e substituido depois que o tamanho completo foi persistido.
Os wrappers `storage_slot_writer_*` preservam `ZSTG.ZSY` e a API usada pelos
slots ZSYS. O cache EP9.3 usa o mesmo mecanismo com `ZSCT.ZSY`, sem reservar o
pacote inteiro em RAM.
Quando o destino da renomeacao transacional e um nome 8.3 valido, a entrada
publicada usa exatamente esse alias final. Consumidores pre-kernel encontram
`ZSA0.ZSY`, `ZSB0.ZSY` e os controles fixos sem depender do alias temporario.

### Fixtures e verificacao

`tools/storage_fixtures.py` gera imagens deterministicas valida, corrompida e
desconhecida; a matriz FAT32 cobre LFN, alias, volume sem espaco, divergencia
de FAT, cadeia corrompida e diretorio LFN invalido. `make storage-fixtures-test`
testa o gerador, `make storage-fixtures` cria imagens/hashes, `make run-storage`
conecta boot e as fixtures nos slots IDE, e `make storage-fixtures-verify`
compara tamanho e SHA-256 depois do QEMU. `storage check <id>` e somente
leitura e verifica BPB, backup, FSInfo, FATs, cadeias e LFNs.

## Camada de bloco e USB MSC (EP4.3/BLK0/BLK1/BLK2)

`block_device_t` e a fronteira comum entre o ATA legado e dispositivos USB
Mass Storage. Cada provedor possui ID unico, modelo, capacidade em setores,
setor fixo de 512 bytes, estado online, contadores de leitura/escrita e ultimo
erro. Os campos append-only `max_transfer_sectors` e `capabilities` publicam o
limite de transferencia e o suporte a FLUSH/FUA. A API valida callbacks,
limites de LBA e capacidade antes de chamar o driver; escrita em um provedor
somente-leitura retorna `ERR_UNAVAILABLE`.

O BLK0 introduz `bio_request_t` como descricao de uma operacao logica e
`block_submit_sync()` como adaptador bloqueante para a camada existente. Um
BIO informa dispositivo, LBA, quantidade, buffer, tamanho declarado do buffer,
operacao, flags, callback e contexto; a conclusao publica estado, erro e
setores concluidos. Os estados observaveis sao `QUEUED`, `IN_FLIGHT`,
`COMPLETED`, `CANCELLED` e `ERROR`, embora a fila e o cancelamento efetivo
fiquem para o BLK1.

O buffer permanece sob ownership do chamador e deve continuar valido ate a
conclusao. A camada de bloco nao libera nem copia o buffer. A callback de
conclusao e chamada uma vez depois do estado terminal no adaptador sincrono.
`block_read()` e `block_write()` preservam suas assinaturas e passam pelo mesmo
adaptador, mantendo contadores e `last_error`.

Como o adaptador e bloqueante e os drivers atuais usam espera ativa com limite,
`block_submit_sync()` so pode ser usado em contexto de processo ou worker; ele
nao deve ser chamado a partir de IRQ. A fila, espera por wait queue,
cancelamento efetivo e conclusao fora de ordem serao responsabilidades do
BLK1.

O adaptador retorna `ERR_NULL` para ponteiros ausentes, `ERR_INVALID` para
formato ou flags invalidas, `ERR_OVERFLOW` para limites de transferencia,
`ERR_DISK` para LBA invalido e `ERR_UNAVAILABLE` para operacao ou capacidade
nao suportada. `ERR_TIMEOUT` e outros erros do driver sao propagados sem
retry generico. ATA e USB MSC continuam sem FLUSH/FUA; o backend deterministico
do autoteste exercita sucesso, erro, limite, somente-leitura e capacidades
indisponiveis sem alterar o inventario real.

O BLK1 acrescenta uma fila FIFO estatica de 32 entradas, protegida por
spinlock, e o dispatcher executado pela `Zephyr kworker`. `block_submit()`
retorna com o BIO em `QUEUED`; `block_submit_sync()` usa a mesma fila e drena
ate o estado terminal. `block_cancel()` remove somente BIOs ainda enfileirados,
publicando `ERR_CANCELLED`; um BIO em voo retorna `ERR_STATE`. O dispositivo nao
pode ser removido ou substituido enquanto houver requisicoes pendentes.

O dispatcher preserva a ordem e funde somente BIOs do mesmo dispositivo,
operacao e flags, com LBA e buffers contiguos e dentro do limite do dispositivo.
FLUSH nunca e fundido e nenhum buffer de bounce e criado. O callback de
conclusao ocorre exatamente uma vez depois do estado e dos setores concluidos;
erros do driver, inclusive `ERR_TIMEOUT`, sao propagados sem retry generico.
`block_get_stats()` publica profundidade, pico, fusoes, conclusoes, falhas,
cancelamentos, setores e taxas medias sob demanda. `blkstat` exibe essas
metricas e os contadores por dispositivo. Se a workqueue estiver indisponivel,
o caminho sincrono continua funcional e submissao assincrona retorna
`ERR_UNAVAILABLE`.

O BLK2 acrescenta `block_cache.c` como cache de leitura estatico com 64 blocos
de 512 bytes, 64 buckets de hash e lista LRU por indices. A chave inclui ID do
dispositivo, LBA e tamanho do bloco. Leituras de FAT, VFS e DevFS passam por
`block_read()`: hits copiam da RAM, misses carregam pela fila BLK1 e leituras
contiguas podem ser agrupadas ate o limite publicado pelo dispositivo. Quando
nao ha entrada elegivel, a leitura faz bypass direto sem falhar.

As entradas publicam `FREE`, `READING`, `VALID`, `DIRTY`, `WRITEBACK` e `ERROR`.
Referencias, pins e wait queues impedem eviction ou descarte durante uso. No
BLK3, `block_write()` copia dados para o cache e retorna depois da aceitação
logica; uma escrita parcial faz preload fisico somente do setor necessario e
preserva os bytes nao alterados. Nenhum ponteiro do chamador e retido.

O writeback periodico usa uma work item a cada 250 ticks, processando ate 8
blocos por ciclo e sem FLUSH fisico. `block_cache_sync_device()` e
`block_cache_sync_all()` processam todos os blocos sujos e, quando suportado,
submetem FLUSH ao dispositivo. ATA detecta FLUSH CACHE durante IDENTIFY e
publica `BLOCK_DEVICE_CAP_FLUSH`; USB MSC continua somente-leitura e sem
FLUSH/FUA. A ausencia de FLUSH retorna `OK` com durabilidade `DEGRADED`, e
erros de escrita ou FLUSH mantem os dados sujos e propagam o erro.

Entradas sujas, fixadas ou em I/O nao podem ser removidas por `cache clear`,
refresh, desmontagem, unregister ou substituicao. Essas operacoes sincronizam
antes da invalidacao e recusam a transicao quando o writeback falha. `sync` e
`fsync` sao explicitos; fechamento de descritor e saida de processo nao fazem
sync automatico. `cachestat` expoe hits, misses, leituras fisicas, bytes sujos,
writeback, syncs, flushes, durabilidade e estados; `cache clear` remove
somente entradas limpas e elegiveis.

O BLK4 acrescenta failpoints privados, one-shot e direcionados aos mocks de
autoteste. Submissao, execucao, conclusao, FLUSH, eviction e writeback podem
retornar `ERR_DISK` ou `ERR_TIMEOUT` na ocorrencia armada. A conclusao continua
unica, falha de eviction faz bypass sem perder a vitima, e writeback abortado
republica `DIRTY` com os mesmos dados; nao existe retry generico automatico.
Um novo `sync` e a operacao que tenta novamente.

`blkcheck` exige as fixtures `ata1p1` FAT12 e `ata1p4` FAT32 do
`run-storage`. Se estiverem apenas detectadas, o comando monta essas duas
fixtures controladas; elas permanecem montadas apos sucesso para permitir a
verificacao seguinte e nao alteram o volume padrao. FAT12 e validada sem
escrita por listagem, leitura e SHA-256. A FAT32 usa exclusivamente
`BLK4CHK.BIN`, recusa um arquivo preexistente, confirma a leitura pelo cache
antes do writeback, sincroniza, rele, compara o hash, remove e sincroniza
novamente. O acesso gravavel acompanha a capacidade do disco ATA; um disco
realmente somente leitura torna a fase FAT32 indisponivel. O comando nao
executa reboot real e nao adiciona journaling aos formatos FAT; a recuperacao
pos-reboot ZUPD permanece em sua matriz propria.

`vfs_fsync()` sincroniza o volume de arquivos regulares e o dispositivo de
bloco `/dev/hda`; pipes, terminal, speaker e demais objetos sem persistencia
retornam `ERR_UNAVAILABLE`. `vfs_sync()` e as syscalls append-only
`APP_SYSCALL_FSYNC`/`APP_SYSCALL_SYNC` fornecem as fachadas correspondentes.

Os IDs ATA permanecem `ata0` a `ata3`. Um MSC valido recebe um ID de bloco no
formato `usb-ms-BB:DD.F-pN-aN-l0`, derivado do ID estavel da sessao UHCI. O
inventario `storage` consome somente `block_device_t`, portanto discos USB e
ATA aparecem juntos sem aplicar topologia de canal/master/slave ao USB.

O driver MSC aceita somente uma interface de classe `0x08`, subclass `0x06`,
protocolo BOT `0x50`, LUN 0, exatamente um Bulk IN e um Bulk OUT e setores de
512 bytes. O BOT implementa CBW, Data-In e CSW com validacao de assinatura,
tag, residue e status. O subconjunto SCSI e `INQUIRY`, `TEST UNIT READY`,
`READ CAPACITY(10)` e `READ(10)`. `storage list` detecta e lista os volumes;
na imagem hibrida, exatamente um FAT32 com label `ZEPHYROS` e montado
automaticamente e gravavel pelo provedor ATA. `storage mount <id>` continua
disponivel para volumes adicionais e cria uma montagem manual somente em RAM.

O caminho USB usa UHCI Bulk sincrono, TDs fragmentados por `wMaxPacketSize`,
toggles por endpoint, buffers DMA fixos e timeout absoluto. Em falha, executa
Mass Storage Reset, `CLEAR_FEATURE(HALT)` nos dois endpoints, reseta os toggles
e permite uma unica nova tentativa. Hubs, hot-plug, EHCI, multiplos LUNs,
`READ CAPACITY(16)` e escrita USB permanecem fora do escopo.

## Indice global EP3 (`file_index.c`)

O indice publica aliases 8.3 e caminhos de arquivos e diretorios de todos os
volumes montados; o File Manager consulta a API LFN para exibir o nome longo.
A primeira versao existe somente em RAM e limita-se a 512
entradas, 128 diretorios percorridos, profundidade 16, quatro fontes, termos
de 63 caracteres e 64 resultados. Pesquisa de conteudo, curingas, metadados
ricos e persistencia ficam fora da EP3.

`fs_dir_cursor_t` e `storage_dir_cursor_t` preservam cluster, setor e entrada
entre chamadas. Um avanco carrega no maximo um setor de diretorio e, quando a
cadeia termina o cluster atual, somente a consulta FAT necessaria. O indexador
usa DFS deterministico na ordem das montagens e das entradas em disco. Desde
a SYNC3, um trabalho `NORMAL` da `Zephyr kworker` executa um passo por callback,
reexecuta imediatamente enquanto estiver `BUILDING` e verifica novas geracoes
com atraso de um tick quando estiver ocioso. System e o loop principal drenam
a mesma workqueue somente como fallback.

```c
uint32_t fs_get_generation(void);
int fs_dir_cursor_open(const char* path, fs_dir_cursor_t* cursor);
int fs_dir_cursor_next(fs_dir_cursor_t* cursor, fs_dir_entry_t* entry,
                       uint8_t* found, uint8_t* done);
int storage_dir_cursor_open(const char* id, const char* path,
                            storage_dir_cursor_t* cursor);
int storage_dir_cursor_next(storage_dir_cursor_t* cursor,
                            storage_dir_entry_t* entry,
                            uint8_t* found, uint8_t* done);
```

A geracao monotonicamente crescente do filesystem de boot muda depois de
remontagem e de cada mutacao concluida: escrita, criacao, exclusao, operacao
atomica e finalizacao de streaming. O indice compara essa geracao e as
geracoes de montagem do Storage continuamente. Qualquer mudanca descarta o
candidato e inicia um rebuild automatico, mantendo a tabela ativa anterior
ate o novo checksum ser publicado.

```c
int file_index_init(void);
int file_index_poll(uint32_t budget, uint32_t* out_steps);
int file_index_rebuild(void);
int file_index_cancel(void);
int file_index_get_status(file_index_status_t* status);
int file_index_search(const char* query, file_index_result_t* results,
                      uint32_t capacity,
                      file_index_search_status_t* status);
int file_index_validate_state(void);
int file_index_self_test(void);
```

`file_index_status_t.event_generation` identifica mudancas observaveis da
tabela e `operation_generation` identifica cada reconstrucao iniciada. O
Shell conserva a geracao da reconstrucao no job; se outra reconstrucao ou
evento substituir a operacao, o resultado antigo e descartado e a tabela
ativa permanece protegida.

As tabelas ativa e candidata sao alocadas no heap. Falha de memoria preserva
a ativa e suspende repeticoes para a mesma assinatura de fontes; um novo
evento ou `index rebuild` tenta novamente. Cancelamento tambem preserva a
tabela ativa e suspende o rebuild automatico ate evento novo, inclusive se a
tabela candidata terminar antes de o comando `index cancel` ser processado.
Limite cheio publica resultado parcial. Canarios, checksum incremental e
validacao estrutural detectam corrupcao e agendam reconstrucao sem degradar
filesystem, Explorer ou Recovery.

A busca e case-insensitive e procura substring no nome ou no caminho completo
normalizado `<volume-id>:/pasta/nome`. Cada resultado conserva ID e geracao de
montagem; volume desmontado, geracao obsoleta, tabela parcial, rebuild ativo,
cancelamento e indice desatualizado sao estados observaveis.

---

## FAT12 (`fat12.c`)

### Estrutura do Disco

```
Setor 0:          Boot sector (contém BPB)
Setores 1-(R-1):  Stage2 e kernel
Setores R-(R+17): Duas FATs (9 setores cada)
Próximos 14:      Root directory (224 entradas)
Restante:         Data area (clusters)
```

`R` é calculado como `ceil((boot + stage2 + kernel) / 512)`. Dessa forma, a
FAT começa imediatamente após o payload real sem manter uma reserva fixa.

### BPB (BIOS Parameter Block)

O boot sector contém informações sobre o formato:

```c
typedef struct {
    uint8_t  boot_jump[3];       // Jump para bootloader
    char     oem[8];             // Nome do OEM
    uint16_t bytes_per_sector;   // 512
    uint8_t  sectors_per_cluster;// 1
    uint16_t reserved_sectors;   // calculado pelo payload de boot
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

As entradas FAT12 usam os 32 bytes padrao, incluindo o campo de hora de
criacao de 16 bits entre o indicador de decimos e a data. Isso mantem os
offsets `cluster_low` em 26 e `file_size` em 28, compativeis com ferramentas
host que gravam arquivos na imagem.

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

Interface unica que abstrai FAT12 e FAT32. Depois de `storage_init()`, o
volume FAT32 `ZEPHYROS` e selecionado como sistema quando ha exatamente um
candidato valido; o FAT12 bruto permanece como recuperacao explicita.

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
int  fs_use_system_volume(void);
int  fs_has_system_volume(void);
int  fs_rename_file_in_dir(dir_path, old_name, new_name);
```

Os caminhos sem prefixo usam o volume FAT32 do sistema quando ele esta
montado. `system:/arquivo` seleciona esse volume explicitamente;
`<volume-id>:/arquivo` seleciona um volume montado e `legacy:/arquivo` força o
FAT12 bruto. Um arquivo ausente no sistema nao cai silenciosamente no legado.
`fs_rename_file_in_dir()` preserva atributos, tamanho e cadeia de clusters no
FAT32, publicando uma nova entrada LFN/8.3 antes de remover a antiga; o
Explorer usa essa rota em vez de simular renomeacao por exclusao e nova
escrita.

### Operacoes atomicas FAT12 para U3 e AS4

A U3 serializa leituras e mutacoes publicas do filesystem enquanto troca
arquivos controlados. As novas operacoes aceitam somente nomes FAT 8.3
canonicos no diretorio raiz e arquivos de 1 a 64 KiB:

```c
int fs_get_root_file_info(const char* filename, uint32_t* size_out,
                          uint8_t* attributes_out);
int fs_atomic_write_root(const char* filename, const uint8_t* data,
                         uint32_t size, uint8_t attributes,
                         fs_atomic_mode_t mode);
int fs_atomic_delete_root(const char* filename);
int fs_atomic_write_file_in_dir(const char* dir_path, const char* filename,
                                const uint8_t* data, uint32_t size,
                                uint8_t attributes, fs_atomic_mode_t mode);
int fs_atomic_delete_file_in_dir(const char* dir_path, const char* filename);
```

`FS_ATOMIC_CREATE_OR_REPLACE` e reservado aos arquivos internos do updater.
`FS_ATOMIC_REPLACE_ONLY` exige que o alvo ja exista. O valor
`FS_ATTRIBUTES_PRESERVE` conserva os atributos da entrada substituida.

No FAT12, `fat12_atomic_write_root()` grava a nova cadeia em clusters livres,
persiste as duas FATs, troca a entrada raiz em um unico setor e somente depois
libera a cadeia antiga. `fat12_atomic_delete_root()` persiste primeiro a
entrada removida e libera os clusters em seguida. Assim, uma troca nunca
publica uma cadeia parcialmente gravada.

AS4 estende a mesma troca copy-on-write a um arquivo 8.3 dentro de um
subdiretorio FAT12 existente. A EP9.4A acrescenta a mesma politica ao FAT32,
com nomes longos UTF-8 convertidos para entradas LFN UTF-16LE, aliases 8.3,
duas FATs sincronizadas, criacao de diretorios, exclusao e renomeacao.

### Escrita sequencial de raiz para U5

A U5 acrescenta uma sessao FAT12 unica para receber arquivos de ate 128 KiB
sem manter o corpo inteiro em RAM:

```c
int fs_stream_begin_root(const char* filename, uint32_t expected_size,
                         uint8_t attributes);
int fs_stream_write_root(const uint8_t* data, uint32_t size);
int fs_stream_finish_root(void);
int fs_stream_abort_root(void);
int fs_stream_is_active(void);
```

`begin` exige tamanho conhecido e slot ainda inativo; no FAT32 o caminho pode
usar LFN e subdiretorios. `write` aceita blocos sequenciais usando buffer
limitado, sem carregar a imagem inteira.
`finish` exige exatamente o tamanho anunciado antes de publicar o tamanho da
entrada. `abort` remove primeiro a entrada parcial e depois libera a cadeia.
Outras mutacoes publicas sao recusadas enquanto a sessao estiver ativa.

O servico remoto usa apenas aliases internos hidden/system/archive
`ZUR0.ZUP` e `ZUR1.ZUP`. FAT32 nao usa slots A/B nem journaling nesta etapa;
tamanhos acima de 128 KiB retornam `ERR_OVERFLOW`. O commit autenticado entre os
slots e responsabilidade de Update, nao do filesystem.

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

## Arquivos para aplicativos (`app_files.c`)

A App API não expõe estruturas FAT nem ponteiros internos. Desde a VFS1,
`app_files.c` e uma fachada compativel sobre `vfs.c`: cada processo possui
uma tabela de 32 descritores, `0`, `1` e `2` representam stdin, stdout e
stderr, e arquivos regulares usam `3` a `31`. O pool global comporta 32
arquivos regulares abertos. Cada chamada valida filesystem disponível,
caminho, modo, processo atual, tamanho e buffer.

- Leitura: sequencial, até 4096 bytes por chamada; EOF retorna `OK` com zero
  bytes lidos.
- Stdin: bloqueia pela fila IPC do processo focado. Cancelamento e sinal
  entregavel concluem a leitura pendente com zero bytes, permitindo aplicar a
  terminacao no retorno seguro ao ring 3 sem contabilizar falha de I/O.
- Escrita: substitui o conteúdo inteiro do arquivo nesta primeira versão.
- Seek: `SET`, `CUR` e `END` somente em descritor exclusivamente de leitura;
  posicoes fora do arquivo e seek em descritor gravavel sao recusados.
- Fechamento: invalida o descritor; todos os descritores sao liberados pelo
  ciclo de vida do processo, inclusive em descarte e destruicao. Os fds
  padrao `0-2` sao reservados e nao aceitam fechamento pela App API.
- Falhas: retornam códigos de `errors.h`, sem `panic()`.

`app_handle_t` e as funcoes historicas permanecem como aliases e fachadas
compativeis. Esses serviços também são exercitados por `vfs test`, `appcheck`,
`regcheck full` e `health check`.

## Namespace e montagens VFS2

A VFS2 publica um namespace unico sobre Storage. A tabela global possui
quatro entradas: o volume `SYSTEM` ocupa `/`; o volume de boot distinto ocupa
`/mnt/boot`; os demais volumes usam `/mnt/<volume-id>`. Sem `SYSTEM`, o volume
de boot ou o primeiro volume legado torna-se a raiz. `/mnt` existe como
diretorio virtual e nao pode ser aberto como arquivo.

A sincronizacao consulta primeiro o `mounted_count` do snapshot de Storage e
le exatamente essa quantidade de registros. Divergencia entre a contagem e o
inventario e erro estrutural; o fim normal da lista nao e usado como sentinela
falha e nao gera aviso espurio.

O refresh preserva os IDs dos volumes montados que continuam presentes depois
da nova enumeracao e restaura essas montagens antes de publicar a nova geracao.
Todos os chamadores de refresh sincronizam a VFS antes de reconstruir o indice,
inclusive quando o inventario termina degradado. Se um alias obsoleto ainda for
encontrado durante unmount, a VFS o reconcilia sem exigir uma desmontagem que o
Storage ja realizou.

A resolucao aceita caminhos absolutos e relativos, `/` ou `\`, separadores
repetidos, `.` e `..`. Caminho vazio, caminho com 256 bytes ou mais e escape
acima de `/` sao recusados. A selecao usa o maior prefixo terminado em limite
de componente. `system:`, `legacy:` e `<volume-id>:` continuam aceitos, mas
caminhos universais sao o formato preferencial da App API 0.6. O separador de
alias `:` so e interpretado no inicio de um caminho nao absoluto. Em caminhos
absolutos ele pode integrar o componente do volume, como em
`/mnt/usb-ms-00:04.0-p1-a1-l0p1`.

Arquivos abertos conservam caminho canonico, volume, caminho relativo e
geracao da montagem. O lock da VFS protege somente tabelas e contadores; as
chamadas FAT/Storage ocorrem sem esse lock. Desmontagem da raiz, de volume
fixo ou de volume referenciado por arquivo aberto ou `cwd` e recusada. FAT12 e
FAT32 permitem leitura; escrita integral continua restrita a FAT32 gravavel.
Descritores de diretorio e `readdir` permanecem fora do escopo.

`storage_get_path_info()` e a consulta comum usada pela VFS para distinguir
arquivo regular e diretorio sem criar descritor. Ela preserva
`storage_get_file_info()` para os chamadores que exigem exclusivamente um
arquivo regular.

## Objetos VFS e MM1

`file_t` e `vnode_t` sao obtidos dos caches `vfs_file` e `vfs_vnode`, enquanto
as tabelas de descritores por processo mantem ponteiros para esses objetos.
Os vnodes de stdin, stdout e stderr sao compartilhados e persistentes; os
objetos de arquivos dinamicos retornam ao cache quando o ultimo descritor e
fechado. `vfs_validate_state()` verifica posse, associacao entre tabelas e
vnode, referencias por processo e a integridade global do SLAB.

## DevFS e listagem universal VFS3

A VFS3 reserva uma quinta montagem virtual fixa em `/dev`. Ela não ocupa uma
das quatro vagas de volumes do Storage, não é removida por refresh e não pode
ser desmontada. A resolução pelo maior prefixo seleciona `/dev` antes da raiz,
mesmo quando o volume FAT contém uma entrada de mesmo nome. Processos podem
usar `/dev` como `cwd`, e referências de diretório continuam contabilizadas.

O registro estático expõe cinco nós no mesmo pool global de arquivos abertos:

- `/dev/null`: leitura em EOF e escrita integral descartada;
- `/dev/zero`: leitura preenchida com zeros e escrita descartada;
- `/dev/tty`: leitura bloqueante pelo fluxo de stdin e escrita no console;
- `/dev/speaker`: escrita de um `app_speaker_tone_t` ou `ioctl` BEEP/STOP;
- `/dev/hda`: primeiro bloco ATA online, somente leitura, com capacidade
  congelada no `open`, acesso por bytes e `lseek` dentro da capacidade.

O speaker aceita 20 a 20.000 Hz e duração de 1 a 2.000 ms. `/dev/hda` usa um
buffer de setor por arquivo para bordas desalinhadas e envia setores completos
diretamente à camada de blocos. EOF retorna zero bytes e nenhum autoteste
escreve no disco. Esperas IPC, console, speaker e I/O de bloco sempre ocorrem
fora dos locks VFS/devfs.

`vfs_list_dir()` lista diretórios FAT12/FAT32 com nomes longos. A raiz combina
as entradas reais com `mnt` e `dev` sem duplicatas; `/mnt` lista os pontos de
montagem e `/dev` lista o registro do devfs. O Shell usa essa API em
`ls [caminho]`; `cat` abre, lê até 4095 bytes e fecha pela VFS.

## PROC0 - Contrato de pseudo-filesystems

O PROC0 congela o contrato documental que o futuro `procfs` e `sysfs` deverão
seguir. A primeira implementação será somente leitura e reutilizará as
operações VFS existentes; não haverá novo descritor, syscall, App API ou
layout binário para consumidores.

`/proc` é reservado a relatórios do sistema e processos; `/sys` é reservado
a objetos, barramentos, dispositivos, interfaces de rede, blocos e atributos
de energia. A separação evita que um relatório composto de `/proc` seja
confundido com um atributo de objeto de `/sys`. A primeira árvore não possui
nós graváveis, incluindo controles de energia; permissões por usuário e
UID/GID também ficam fora do PROC0.

O conteúdo é ASCII, sem `NUL`, `CR`, ANSI ou locale, e usa uma linha por
atributo: `<chave> <valor>\n`; o terminador é `LF`, nunca `CRLF`. Chaves são
estáveis e não possuem espaços; contadores são decimais, valores de hardware
são hexadecimais, IPv4 é pontuado e estados são tokens documentados. Uma
chave pode repetir-se para registros múltiplos, sempre na ordem publicada
pelo nó.

Cada arquivo virtual captura seu conteúdo na abertura em um snapshot de no
máximo 16 KiB. O buffer pertence ao `file_t` e é liberado no fechamento; o
descritor não conserva ponteiros para processos, dispositivos ou montagens.
`file_t.offset` é o cursor, `read()` retorna partes do snapshot até EOF e
EOF retorna `OK` com zero bytes. `lseek()` aceita `SET`, `CUR` e `END` dentro
dos limites do snapshot. Serialização acima de 16 KiB retorna
`ERR_OVERFLOW`, sem truncamento.

Processos são identificados por PID e `process_event_generation`; dispositivos
usam identificador estável e geração. A remoção concorrente não invalida um
snapshot já aberto, enquanto uma nova abertura retorna `ERR_NOT_FOUND`. A
listagem por `vfs_list_dir()` é copiada durante a chamada e segue ordem
determinística: nós fixos, PIDs crescentes e identificadores estáveis.
Escrita ou operação não suportada retorna `ERR_UNAVAILABLE`; caminhos e
cursores inválidos retornam `ERR_INVALID`; falta de memória retorna `ERR_MEM`.

## PROC1 - Procfs integrado ao VFS

O VFS monta automaticamente o volume lógico `procfs` em `/proc` como uma
montagem pinned, fixa, somente leitura e sem uso de Storage. O slot do devfs é
preservado e `/sys` continua reservado para uma etapa posterior. A raiz lista
`proc` junto de `mnt` e `dev`; `ls /proc` lista o registro fixo `uptime`.

`src/fs/procfs.c` implementa `file_operations_t` para nós representados como
`VFS_NODE_REGULAR`. A abertura de `/proc/uptime` executa o callback uma única
vez, aloca um buffer de `PROCFS_MAX_SNAPSHOT_SIZE` (16 KiB), publica o tamanho
gerado e associa o snapshot ao contexto privado do `file_t`. O callback usa
retorno de erro e `uint32_t* out_len`; escrita, `ioctl` e `sync` retornam
`ERR_UNAVAILABLE`.

O snapshot não mantém ponteiros para timer, processos ou dispositivos. O
`file_t.offset` é o cursor, `read()` aceita blocos parciais e retorna `OK` com
zero bytes no EOF, e `lseek()` aceita `SET`, `CUR` e `END` dentro do intervalo
capturado. Excesso retorna `ERR_OVERFLOW` sem truncamento; erro de callback ou
alocação libera o buffer antes de retornar. O fechamento libera o snapshot e a
referência da montagem. O autoteste `procfs_self_test()` é executado pelo
`vfs_self_test()` e verifica que não sobram buffers ou referências.

## PROC3 - Sysfs integrado ao VFS

O VFS monta automaticamente o volume lógico `sysfs` em `/sys`. Essa montagem
é pinned, fixa, pública, somente leitura, usa `STORAGE_FS_NONE` e não depende
de um volume Storage. O provider está em `src/fs/sysfs.c`, separado do
`procfs`, e os arquivos de atributo continuam representados como
`VFS_NODE_REGULAR`. A raiz virtual lista `sys` junto com `mnt`, `dev` e
`proc`; `mount` exibe o tipo `SYSFS`.

A árvore fixa é `/sys/bus/pci/devices`, `/sys/class/net`,
`/sys/class/block` e `/sys/power/state`, com os diretórios intermediários
`bus`, `pci`, `devices`, `class`, `net`, `block` e `power`. A raiz lista
`bus`, `class`, `power`; `bus` lista `pci`; `bus/pci` lista `devices`; e
`class` lista `net`, `block`. PCI usa identificadores `BB:DD.F` ordenados por
bus/device/function. Interfaces usam os IDs produzidos por
`network_manager_format_text()` e blocos usam `block_device_t.id`; ambos são
ordenados lexicograficamente. Classes sem hardware ficam vazias e não há
placeholders.

Cada diretório de dispositivo lista arquivos de atributo em ordem fixa. PCI
publica `bus`, `device`, `function`, `vendor_id`, `device_id`, `class`,
`subclass`, `prog_if`, `revision`, `irq`, `bar0` a `bar5` e `present`. Rede
publica identidade, transporte, estado, dados PCI/USB, MAC, estado L3 e
estatísticas de pacotes. Blocos publicam identidade, modelo, provider,
setores, capacidade, modo, operações e capacidades. A capacidade é calculada
com verificação de overflow. `/sys/power/state` publica `state S0` até
`state S5`, `cpu_idle`, `hardware_poweroff` e `reboot`, usando os tokens
`available`, `simulated` e `unavailable`.

Todos os arquivos são snapshots ASCII de no máximo 16 KiB, capturados durante
`open()` por cópia dos inventários PCI, rede e blocos e do status de energia.
O contexto privado do `file_t` contém somente o buffer, o cursor implícito em
`file_t.offset`, montagem, tipo, identificador e geração; não retém ponteiros
para dispositivos. `read()` suporta leituras parciais e EOF com zero bytes,
e `lseek()` aceita `SET`, `CUR` e `END` dentro do snapshot. O fechamento libera
o buffer e a referência da montagem. Escrita, `ioctl` e `sync` retornam
`ERR_UNAVAILABLE`; caminhos inválidos, dispositivos ausentes, overflow e
falha de memória retornam respectivamente `ERR_INVALID`, `ERR_NOT_FOUND`,
`ERR_OVERFLOW` e `ERR_MEM`.

O provider mantém uma geração interna do inventário para futuras atualizações,
sem hotplug ou rescan nesta etapa. Se o hardware desaparecer depois da
abertura, o snapshot continua válido; uma nova abertura procura o ID novamente.
`sysfs_validate_state()` e `sysfs_self_test()` verificam montagem, ordenação,
formato ASCII, seek, EOF, somente leitura e ausência de buffers ou referências
residuais. A validação funcional observável usa `ls /sys`, as classes, os
atributos disponíveis e `cat /sys/power/state`.

## PROC4 - Consumidores de procfs e sysfs

O Task Manager Classic lê `/proc` exclusivamente pela VFS. A lista é obtida
em ordem numérica, cada status é copiado para uma matriz privada e a identidade
de uma linha é `pid + generation`. A atualização repete a enumeração uma vez
quando há churn; uma captura ainda instável é ignorada naquela atualização.
As ações de reinício e término revalidam PID e geração antes de consultar o
processo atual. O fallback Simple e a aba de threads continuam usando o caminho
interno legado.

A aba de memória usa os campos de `/proc/meminfo`. Detalhes que não existem no
ABI textual, como EIP, ESP, CR3, page directory e ponteiros de stack, não são
exibidos no Classic migrado. Nenhum `process_t*`, descritor ou buffer de
snapshot é mantido entre eventos da interface.

Os comandos `devices` e `device-info` leem atributos dos nós `/sys` para PCI,
rede e bloco, fechando cada snapshot antes de retornar. IDs `pci-*`, `net-*`
e blocos nativos são resolvidos sem manter ponteiros para os inventários. PS/2,
PIT, VGA, VESA, AC97 e speaker continuam usando o snapshot legado quando não
possuem nó correspondente em sysfs.

`proccheck` executa a validação agregada de diretórios e arquivos regulares dos
dois namespaces, incluindo ASCII, EOF, ausência de escrita e caminhos
inválidos. Na entrega PROC4, a migração não criava `/proc/sys` e não alterava a
App API, syscalls, layouts binários ou o bootloader; os controles foram
adicionados somente na etapa PROC5.

## PROC5 - Controles de runtime em /proc/sys

PROC5 está implementado dentro do provider `procfs`; a confirmação funcional no
QEMU permanece pendente. `/proc/sys` e `/proc/sys/kernel` são diretórios
determinísticos. Os dois controles são arquivos regulares: leitura pública,
escrita somente por processo nativo/ring0, sem provider genérico de escrita e
sem qualquer escrita em `/sys`.

O primeiro conjunto implementado é:

```text
/proc/sys
/proc/sys/kernel
/proc/sys/kernel/console_log_level
/proc/sys/kernel/buffer_log_level
```

A montagem continua marcada como RO no inventário VFS; o provider publica uma
exceção explícita de arquivo para os dois controles. O redirecionamento de
escrita continua limitado a Storage/FAT32 e não pode alcançar `/proc/sys`.

Os controles refletem os níveis já suportados pelo subsistema de log e aceitam
somente `error`, `warn`, `info` e `debug`. Cada arquivo lerá uma linha ASCII
com sua chave e o valor efetivo. A abertura com leitura continuará capturando
um snapshot imutável de até 16 KiB; `offset`, leituras parciais, EOF e `lseek`
seguem o contrato PROC0.

A escrita é uma transação de valor único: ASCII sem `NUL`, `CR`, ANSI ou bytes
fora de ASCII, com um token válido e `LF` opcional. A entrada é validada por
inteiro antes do commit; erro não altera o valor anterior e não há truncamento.
Processos ring3 recebem `ERR_UNAVAILABLE`. Aberturas já existentes mantêm seu
snapshot; novas aberturas observam o valor novo.

Os valores ficam somente em RAM e retornam aos padrões após reinicialização ou
reset do provider. Caminhos e valores inválidos retornam `ERR_INVALID`, nós
ausentes retornam `ERR_NOT_FOUND`, entradas acima do limite retornam
`ERR_OVERFLOW`, falta de memória retorna `ERR_MEM` e mudanças concorrentes
retornam `ERR_AGAIN`. `ioctl`, `sync` e escrita em diretórios continuam em
`ERR_UNAVAILABLE`. Scheduler, forwarding IPv4, energia, memória e parâmetros
de processos não fazem parte do primeiro conjunto.

## Pipes anonimos e redirecionamento VFS4

`vfs_pipe()` cria dois descritores no processo atual: `fds[0]` somente para
leitura e `fds[1]` somente para escrita. Cada pipe usa um buffer circular
estatico de 4096 bytes e ocupa uma das oito entradas do pool global. Leitura
com buffer vazio e escrita com buffer cheio dormem em Wait Queues; a leitura
retorna EOF depois que todos os escritores fecham e a escrita retorna
`ERR_UNAVAILABLE` depois que todos os leitores fecham. Sinal e cancelamento
retornam `OK` com zero ou com os bytes ja transferidos.

`app_files_pipe()`, `app_api_pipe()` e a syscall 18 publicam a mesma operacao
para aplicativos. O caminho ring 3 valida o vetor de dois handles como uma
faixa gravavel antes de criar o pipe. As syscalls anteriores permanecem
inalteradas e a App API publica a versao 0.9.

O sink de redirecionamento usa `vfs_write_redirect()`, sem alterar a semantica
integral de `vfs_write()`. `>` cria ou substitui o destino; `>>` le o conteudo
existente e anexa antes de publicar uma escrita atomica. A operacao e aceita
somente em FAT32 gravavel e limita a saida acumulada a 64 KiB. FAT12, volume
somente leitura, diretorio, caminho invalido e excedente retornam erro sem
alterar o destino.

### Sockets como nos VFS (NET2)

`VFS_NODE_SOCKET` e a ponte interna `vfs_open_socket()` associam um socket
privado a um `file_t` e a um descritor real da tabela do processo. `vfs_read`,
`vfs_write` e `vfs_close` encaminham para os adaptadores do socket; `lseek` e
`fsync` retornam `ERR_UNAVAILABLE`. O fechamento normal da VFS encerra o
socket, e `vfs_fd_table_release()` repete esse caminho no encerramento do
processo. Nao ha heranca automatica de FDs entre processos nesta etapa.

Os vnodes de socket nao pertencem ao namespace FAT/DevFS e nao retêm payloads
externos. `vfs_validate_state()` verifica o tipo, contexto privado e tabela
de operacoes; a validacao complementar de peers, filas e lifetime SKB fica em
`socket_validate_state()`. `boot.asm` nao foi alterado.

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
void bmp_draw_transparent(&image, x, y, key);// Ignora pixels da cor-chave
int  bmp_draw_transparent_resized(&image, x, y, width, height, key);
void bmp_draw_scaled(&image, x, y, scale);   // Renderiza com escala
void bmp_free(&image);                       // Libera memória
```

`bmp_draw_transparent()` preserva os pixels cuja cor seja igual à chave
fornecida. O Desktop usa a chave magenta (`#FF00FF`) para os ícones BMP, de
forma que a seleção do cartão continue visível atrás da imagem.

`bmp_draw_transparent_resized()` preserva a mesma cor-chave e redimensiona
para largura e altura arbitrárias por nearest-neighbor. A função não altera a
imagem carregada nem aloca memória durante o desenho; o sistema de ícones a
usa para exibir o único cache 32x32 nos tamanhos da escala Classic.

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
