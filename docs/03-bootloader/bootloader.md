# 03 - Bootloader

## O que é o Bootloader?

O bootloader de primeiro estágio é o primeiro código que roda quando o computador liga.
Ele tem exatamente **512 bytes**, fica no setor de boot do disco e carrega o
segundo estágio antes de qualquer trabalho maior.

## Arquivo

```
src/boot/boot.asm    # estágio 1: BPB, geometria e carga do stage2
src/boot/stage2.asm  # estágio 2: E820, VESA, kernel, GDT e Protected Mode
```

## Como Funciona

### Etapa 1: Inicialização (16-bit Real Mode)

```nasm
start:
    cli                    ; Desabilita interrupções
    xor ax, ax             ; Zera registradores
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00         ; Stack no início do boot sector
    ; Interrupções permanecem desabilitadas até a IDT do kernel estar pronta
```

O CPU começa em **Real Mode** (16-bit), endereçando apenas 1 MB de RAM.

### Etapa 2: Carregar o segundo estágio

O estágio 1 consulta a geometria do disco pela BIOS, lê `stage2.asm` a partir
do setor seguinte ao boot e transfere o controle para `0x5000`. Ele mantém o
BPB FAT12 e continua limitado a 512 bytes.

### Etapa 3: Detecção de memória e vídeo (stage2)

```nasm
detect_memory:
    mov di, 0x8000         ; Buffer para o mapa
    mov eax, 0xE820        ; Função do BIOS
    int 0x15               ; Chama BIOS
```

O estágio 2 coleta o mapa E820 e carrega o recovery loader ainda em modo real.
Antes de transferir controle ao loader protegido, configura VESA pelo caminho
BIOS já usado pelo boot legado. O loader fixo desenha seus diagnósticos no
framebuffer em 24 ou 32 bpp; quando VESA não está disponível, mantém a saída
VGA em texto. O mesmo bloco de informações segue intacto para o kernel. Se a
configuração falhar, o kernel mantém o fallback Simple.

O BIOS retorna uma tabela com as regiões de memória disponíveis:
- Endereço base
- Tamanho
- Tipo (livre, reservada, ACPI, etc.)

### Etapa 4: Carregar Kernel do Disco

```nasm
detect_disk_access:
    mov ah, 0x41           ; Consulta extensões de disco EDD
    mov bx, 0x55AA
    int 0x13
    ; Assinatura 0xAA55 e CX bit 0 selecionam o caminho LBA.

read_kernel_lba:
    mov si, disk_address_packet
    mov ah, 0x42           ; Leitura estendida por LBA
    int 0x13               ; DAP aponta para o bounce buffer em 0x10000
```

O estágio 2 verifica o mapa E820, habilita a linha A20 e lê o kernel em lotes
de até 63 setores para `0x10000`. Em discos com EDD, `INT 13h/AH=41` valida a
assinatura e o bit de acesso estendido, e `INT 13h/AH=42` lê o lote por LBA com
um Disk Address Packet de 16 bytes. A leitura LBA não depende da geometria
CHS. Se EDD não estiver disponível, o stage2 consulta a geometria e usa
`INT 13h/AH=02`; nesse caminho cada lote também termina antes da próxima
trilha. Os dois caminhos permanecem dentro da janela de 64 KiB exigida pela
BIOS.

Cada lote admite três tentativas totais, com reset `INT 13h/AH=00` entre
falhas. Uma BIOS que não anuncia EDD seleciona CHS. Se EDD foi anunciado mas
a leitura estendida esgotou as tentativas, o stage2 encerra com diagnóstico
LBA específico, sem esconder a falha tentando CHS. Falhas do fallback também
possuem diagnóstico CHS específico.

Após cada leitura, o stage2 entra temporariamente em protected mode e copia o
lote com segmentos flat para a janela iniciada em
`0x00100000`. A saída passa por um descritor protegido de código 16-bit antes
de limpar `CR0.PE` e retornar ao real mode para a próxima chamada da BIOS. A
imagem completa nunca precisa caber na memória baixa.

A quantidade de setores continua calculada durante o build. O stage2 recusa
imagens cujo conteúdo carregável ultrapasse `0x00800000`, enquanto o linker
aplica o mesmo limite ao kernel, incluindo a BSS.

O estágio 1 continua carregando apenas o stage2 por CHS e permanece limitado
a 512 bytes. Os alvos `run-stage2-lba` e `run-stage2-chs` validam,
respectivamente, o disco IDE sem geometria fixa e o fallback por floppy.

### Etapa 5: Configurar GDT

```nasm
gdt_start:
    dd 0x0                 ; Null descriptor
    dd 0x0

gdt_code:                  ; Code segment
    dw 0xFFFF             ; Limit
    db 10011010b          ; Flags: executável, legível

gdt_data:                  ; Data segment
    dw 0xFFFF
    db 10010010b          ; Flags: legível, gravável
```

A GDT define os segmentos de memória para o Protected Mode.

### Etapa 6: Switch para Protected Mode

```nasm
    mov eax, cr0
    or eax, 1             ; Liga bit 0 do CR0
    mov cr0, eax          ; Ativa Protected Mode

    jmp 0x08:protected_mode  ; Jump para código 32-bit
```

### Etapa 7: Entry Point do Kernel

```nasm
[BITS 32]
protected_mode:
    mov ax, 0x10           ; Segmento de dados
    mov ds, ax
    mov ss, ax
    mov esp, 0x9F000       ; Kernel stack

    mov esi, 0x3000        ; Passa mapa de memória
    call 0x00100000        ; Chama kernel_main()
```

## Layout da Memória

```
0x7C00   → Boot sector (estágio 1, 512 bytes)
0x5000   → Segundo estágio do bootloader
0x3000   → Mapa de memória E820 e contador
0x2000   → Informações do modo VESA
0x10000–0x17E00 → Bounce buffer para lotes de até 63 setores
0x88000–0x98000  → Bitmaps do PMM
0x98000–0x9F000  → Stack inicial do kernel
0x9F000–0xA0000  → Margem reservada para BIOS/EBDA
0x100000–0x800000 → Kernel carregado e BSS (7 MiB)
0x800000–0x1000000 → Janela virtual ZAPP e reserva física
0x1000000–0x1400000 → Heap do kernel (4 MiB)
0x1F00   → Stack temporária do stage2
```

## BPB (BIOS Parameter Block)

O bootloader inclui um BPB para compatibilidade com FAT12:

| Campo | Valor | Descrição |
|-------|-------|-----------|
| bytes_per_sector | 512 | Tamanho de cada setor |
| sectors_per_cluster | 1 | Setores por cluster |
| reserved_sectors | dinâmico | `ceil((boot + stage2 + kernel) / 512)` |
| num_fats | 2 | Número de tabelas FAT |
| root_entries | 224 | Entradas no diretório raiz |
| total_sectors | 2880 | Total de setores (1.44 MB) |

O valor montado inicialmente no setor de boot é substituído pelo empacotador
ao finalizar a imagem. A gravação só ocorre depois de validar assinatura,
geometria FAT12, capacidade e preservação integral do payload.
