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

O estágio 2 coleta o mapa E820 e tenta configurar VESA antes de carregar o
kernel. Se VESA falhar, o kernel recebe essa informação e mantém o fallback
clássico.

O BIOS retorna uma tabela com as regiões de memória disponíveis:
- Endereço base
- Tamanho
- Tipo (livre, reservada, ACPI, etc.)

### Etapa 4: Carregar Kernel do Disco

```nasm
disk_load:
    mov ah, 0x02           ; Função: ler setores
    ; O Makefile passa a quantidade real de setores em KERNEL_SECTORS
    mov cl, 0x02           ; Começa no setor 2
    int 0x13               ; Chama BIOS
```

O estágio 2 lê o kernel setor a setor e copia para `0x10000` na memória. A
quantidade de setores é calculada durante o build, evitando truncar o kernel
quando ele cresce.

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
    mov esp, 0x90000       ; Kernel stack

    mov esi, 0x8000        ; Passa mapa de memória
    call 0x10000           ; Chama kernel_main()
```

## Layout da Memória

```
0x7C00   → Boot sector (estágio 1, 512 bytes)
0x5000   → Segundo estágio do bootloader
0x3000   → Mapa de memória E820 e contador
0x2000   → Informações do modo VESA
0x10000  → Kernel carregado do disco
0x1F00   → Stack temporária do stage2
0x90000  → Stack inicial do kernel
```

## BPB (BIOS Parameter Block)

O bootloader inclui um BPB para compatibilidade com FAT12:

| Campo | Valor | Descrição |
|-------|-------|-----------|
| bytes_per_sector | 512 | Tamanho de cada setor |
| sectors_per_cluster | 1 | Setores por cluster |
| reserved_sectors | 1 | Setores reservados |
| num_fats | 2 | Número de tabelas FAT |
| root_entries | 224 | Entradas no diretório raiz |
| total_sectors | 2880 | Total de setores (1.44 MB) |
