# 05 - Drivers de Hardware

## O que são Drivers?

Drivers são programas que permitem ao kernel comunicar com o hardware (disco, teclado, vídeo, etc.).

## Arquivos

```
src/drivers/
├── ac97.c           → Driver de áudio AC97
├── ata.c            → Driver de disco (ATA PIO)
├── font.c           → Fonte bitmap 8x16
├── idt.c            → Tabela de interrupções
├── irq.asm          → Handlers de interrupção de hardware
├── isr.asm          → Handlers de exceção do CPU
├── keyboard.c       → Driver de teclado PS/2
├── pci.c            → Enumeração do barramento PCI
├── speaker.c        → PC Speaker (som)
├── timer.c          → Timer (PIT)
├── tss.c            → Task State Segment
├── vesa.c           → VESA BIOS Extensions (modo gráfico)
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

### Operação Assíncrona (IRQ 14)

A leitura e gravação de disco são implementadas de maneira assíncrona usando o **IRQ 14** e `thread_block()`.
Quando a thread pede para ler do disco, ela cede o processador, e quando o disco finaliza a operação e envia a interrupção, o manipulador do IRQ 14 realiza `thread_unblock()` permitindo que a thread volte a processar sem gastar ciclos de CPU à toa.

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

---

## VESA (`vesa.c`)

**VESA BIOS Extensions (VBE)** permite usar modos gráficos de alta resolução.

### Inicialização

```c
vesa_init();
```

Escaneia todos os modos suportados pela placa de vídeo e seleciona a melhor resolução disponível (até 1920x1200, 32bpp).

### Primitivas Gráficas

| Função | Descrição |
|--------|-----------|
| `vesa_put_pixel(x, y, color)` | Desenha um pixel |
| `vesa_get_pixel(x, y)` | Retorna cor de um pixel |
| `vesa_clear(color)` | Limpa a tela com uma cor |
| `vesa_fill_rect(x, y, w, h, color)` | Preenche retângulo |
| `vesa_draw_rect(x, y, w, h, color)` | Desenha borda de retângulo |
| `vesa_draw_line(x0, y0, x1, y1, color)` | Desenha linha (Bresenham) |
| `vesa_draw_circle(cx, cy, r, color)` | Desenha círculo |
| `vesa_fill_circle(cx, cy, r, color)` | Preenche círculo |
| `vesa_draw_bitmap(x, y, bitmap, w, h, color)` | Desenha bitmap monocromático |
| `vesa_draw_char(x, y, c, color, scale)` | Desenha caractere com fonte |
| `vesa_draw_string(x, y, str, color, scale)` | Desenha texto |

### Cores

```c
uint32_t color = vesa_rgb(255, 0, 0);    // Vermelho
uint32_t color = vesa_rgba(0, 255, 0, 128); // Verde com alpha
```

### Framebuffer

O framebuffer é mapeado diretamente na memória:

```c
vesa_mode_t* mode = vesa_get_mode();
// mode->framebuffer → ponteiro para memória de vídeo
// mode->width, mode->height, mode->pitch, mode->bpp
```

---

## Font (`font.c`)

Fonte bitmap **8x16** para renderização de texto em modo gráfico (VESA).

### Carregamento

```c
font_init();
```

### Obtendo Glyph

```c
const uint8_t* glyph = font_get_glyph('A');
// glyph[0..15] = 16 bytes representando 8x16 pixels
```

Cada byte representa uma linha de 8 pixels (1 bit por pixel).

---

## PCI (`pci.c`)

**Peripheral Component Interconnect** - Barramento para detectar dispositivos de hardware.

### Enumeração

```c
pci_init();
```

Escaneia 256 buses × 32 devices × 8 functions.

### Estrutura

```c
typedef struct {
    uint16_t vendor_id, device_id;
    uint8_t class, subclass, prog_if, revision;
    uint32_t bar0..bar5;  // Base Address Registers
    uint8_t irq;
    uint8_t bus, device, function;
    uint8_t present;
} pci_device_t;
```

### Busca de Dispositivos

```c
// Por classe/subclasse (ex: 0x04/0x01 = audio)
pci_device_t* dev = pci_get_device(0x04, 0x01);

// Por vendor/device ID
pci_device_t* dev = pci_get_device_by_id(0x8086, 0x2415);
```

### Bus Mastering

```c
pci_enable_bus_mastering(dev);  // Habilita DMA
```

---

## AC97 (`ac97.c`)

**Audio Codec '97** - Driver de áudio para reprodução de som.

### Inicialização

```c
ac97_init();
```

Localiza o dispositivo via PCI (classe 0x04, subclasse 0x01), configura sample rate (44100 Hz) e volume.

### Reprodução

```c
ac97_play(data, size, sample_rate, channels, bits);
```

- `data`: buffer PCM (Pulse Code Modulation)
- `size`: tamanho em bytes
- `sample_rate`: 44100, 22050, etc.
- `channels`: 1 (mono) ou 2 (stereo)
- `bits`: 8 ou 16 bits por sample

### Controles

```c
ac97_stop();              // Para reprodução
ac97_set_volume(20);      // Volume 0-31
ac97_get_device();        // Obtém estado do device
```

### Registros

| Registro | Endereço | Função |
|----------|----------|--------|
| AC97_REG_RESET | 0x00 | Reset do codec |
| AC97_REG_MASTER_VOL | 0x02 | Volume master (esquerdo/direito) |
| AC97_REG_PCM_OUT_VOL | 0x18 | Volume PCM |
| AC97_REG_PCM_FRONT_DAC_RATE | 0x2C | Sample rate |
| AC97_REG_POWER | 0x26 | Power management |
| AC97_REG_EXT_AUDIO | 0x28 | Audio estendido |
```
