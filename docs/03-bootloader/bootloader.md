# 03 - Bootloader

## O que é o Bootloader?

O bootloader é o primeiro código que roda quando o computador liga. Ele tem apenas **512 bytes** e fica no setor de boot do disco.

## Arquivo

```
src/boot/boot.asm
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
    sti                    ; Habilita interrupções
```

O CPU começa em **Real Mode** (16-bit), endereçando apenas 1 MB de RAM.

### Etapa 2: Detecção de Memória (E820)

```nasm
detect_memory:
    mov di, 0x8000         ; Buffer para o mapa
    mov eax, 0xE820        ; Função do BIOS
    int 0x15               ; Chama BIOS
```

O BIOS retorna uma tabela com as regiões de memória disponíveis:
- Endereço base
- Tamanho
- Tipo (livre, reservada, ACPI, etc.)

### Etapa 3: Carregar Kernel do Disco

```nasm
disk_load:
    mov ah, 0x02           ; Função: ler setores
    mov al, 30             ; 30 setores (15 KB)
    mov cl, 0x02           ; Começa no setor 2
    int 0x13               ; Chama BIOS
```

Lê 30 setores do disco e copia para `0x1000` na memória.

### Etapa 4: Configurar GDT

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

### Etapa 5: Switch para Protected Mode

```nasm
    mov eax, cr0
    or eax, 1             ; Liga bit 0 do CR0
    mov cr0, eax          ; Ativa Protected Mode

    jmp 0x08:protected_mode  ; Jump para código 32-bit
```

### Etapa 6: Entry Point do Kernel

```nasm
[BITS 32]
protected_mode:
    mov ax, 0x10           ; Segmento de dados
    mov ds, ax
    mov ss, ax
    mov esp, 0x90000       ; Kernel stack

    mov esi, 0x8000        ; Passa mapa de memória
    call 0x1000            ; Chama kernel_main()
```

## Layout da Memória

```
0x7C00   → Boot sector (512 bytes)
0x8000   → Mapa de memória E820
0x1000   → Kernel carregado do disco
0x90000  → Kernel stack
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
