# 05 - Drivers de Hardware

## O que são Drivers?

Drivers são programas que permitem ao kernel comunicar com o hardware (disco, teclado, vídeo, etc.).

## Arquivos

```
src/drivers/
├── ata.c            → Driver de disco (ATA PIO)
├── idt.c            → Tabela de interrupções
├── irq.asm          → Handlers de interrupção de hardware
├── isr.asm          → Handlers de exceção do CPU
├── keyboard.c       → Driver de teclado PS/2
├── speaker.c        → PC Speaker (som)
├── timer.c          → Timer (PIT)
├── tss.c            → Task State Segment
└── video.c          → VGA Text Mode
```

---

## IDT (`idt.c`)

A **IDT** (Interrupt Descriptor Table) é uma tabela que o CPU consulta quando ocorre uma interrupção ou exceção.

### O que ela faz

| Vetor | Tipo | Descrição |
|-------|------|-----------|
| 0-31 | ISR | Exceções do CPU (div by zero, page fault, etc.) |
| 32-47 | IRQ | Interrupções de hardware (teclado, timer, disco) |

### Remapeamento PIC

O PIC (Programmable Interrupt Controller) precisa ser remapeado porque os vetores 0-7 conflitam com as ISRs:

```c
// Master PIC: IRQ 0-7 → IDT 32-39
outb(0x20, 0x11);  // ICW1: Inicialização
outb(0x21, 0x20);  // ICW2: Vetor base = 32

// Slave PIC: IRQ 8-15 → IDT 40-47
outb(0xA0, 0x11);
outb(0xA1, 0x28);  // Vetor base = 40
```

### Registrando Handlers

```c
// Registrar handler para IRQ1 (teclado)
register_interrupt_handler(33, keyboard_handler);
```

---

## ISRs (`isr.asm`)

**ISR** = Interrupt Service Routine. São handlers para exceções do CPU.

### Exceções Comuns

| Vetor | Nome | Descrição |
|-------|------|-----------|
| 0 | Division By Zero | Divisão por zero |
| 6 | Invalid Opcode | Instrução inválida |
| 13 | General Protection Fault | Acesso ilegítimo a memória |
| 14 | Page Fault | Página não mapeada |

### Fluxo

```
CPU detecta exceção → ISR salva registradores → isr_handler() em C
```

---

## IRQs (`irq.asm`)

**IRQ** = Interrupt Request. São interrupções de hardware.

### Mapeamento

| IRQ | IDT | Hardware | Driver |
|-----|-----|----------|--------|
| 0 | 32 | Timer PIT | timer.c |
| 1 | 33 | Teclado PS/2 | keyboard.c |
| 2 | 34 | Cascade | — |
| 6 | 38 | Floppy | — |
| 14 | 46 | ATA Primary | ata.c |

---

## Driver de Teclado (`keyboard.c`)

Lê scancodes da porta `0x60` e converte para ASCII.

### Fluxo

```
Tecla pressionada → IRQ1 → keyboard_handler() → lê porta 0x60 → callback
```

### Scancode Table

Mapeia cada scancode para um caractere:

```c
static const char scancode_table[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    // ...
};
```

### Callback

O shell registra uma callback para receber teclas:

```c
keyboard_set_callback(shell_handle_key);
```

---

## Timer (`timer.c`)

O **PIT** (Programmable Interval Timer) gera interrupções periódicas.

### Configuração

```c
timer_init(50);  // 50 Hz = 20ms por tick
```

### Como funciona

1. Configura o canal 0 do PIT para gerar IRQ0
2. A cada tick, `timer_handler()` é chamado
3. Incrementa contador de ticks
4. O scheduler usa ticks para preemptar processos

---

## VGA Video (`video.c`)

Modo de texto 80x25 com cores.

### Memória de Vídeo

Cada caractere ocupa 2 bytes na memória `0xB8000`:

```
Byte 0: Caractere ASCII
Byte 1: Cor (fundo 4 bits + frente 4 bits)
```

### Cores Disponíveis

```
0x0 = Preto      0x8 = Cinza Escuro
0x1 = Azul       0x9 = Azul Claro
0x2 = Verde      0xA = Verde Claro
0x3 = Ciano      0xB = Ciano Claro
0x4 = Vermelho   0xC = Vermelho Claro
0x5 = Magenta    0xD = Magenta Claro
0x6 = Marrom     0xE = Amarelo
0x7 = Cinza      0xF = Branco
```

---

## ATA Driver (`ata.c`)

Comunicação com disco rígido via modo **PIO** (Programmed I/O).

### Portas

| Porta | Função |
|-------|--------|
| 0x1F0 | Dados (16-bit) |
| 0x1F1 | Erro / Features |
| 0x1F2 | Número de setores |
| 0x1F3 | LBA Low (bits 0-7) |
| 0x1F4 | LBA Mid (bits 8-15) |
| 0x1F5 | LBA High (bits 16-23) |
| 0x1F6 | Drive + LBA (bits 24-27) |
| 0x1F7 | Comando / Status |

### Comandos

| Comando | Código | Função |
|---------|--------|--------|
| READ | 0x20 | Ler setores |
| WRITE | 0x30 | Escrever setores |
| IDENTIFY | 0xEC | Identificar disco |

---

## TSS (`tss.c`)

O **TSS** (Task State Segment) guarda o kernel stack para quando o CPU muda de ring 3 (user) para ring 0 (kernel).

### Configuração

```c
tss.ss0 = 0x10;    // Kernel data segment
tss.esp0 = 0x90000; // Topo do kernel stack
```

---

## PC Speaker (`speaker.c`)

Controla o buzzer do PC via portas `0x42` e `0x61`.

### Beep

```c
speaker_beep(800, 200);  // 800 Hz por 200ms
```

### Melodia

```c
uint32_t freqs[] = {523, 587, 659, 698, 784, 880, 988, 1047};
uint32_t durs[] =  {200, 200, 200, 200, 200, 200, 200, 400};
speaker_play_melody(freqs, durs, 8);
```
