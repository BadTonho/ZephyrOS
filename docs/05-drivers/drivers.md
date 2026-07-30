# 05 - Drivers de Hardware

## O que são Drivers?

Drivers são programas que permitem ao kernel comunicar com o hardware (disco, teclado, vídeo, etc.).

## Arquivos

```
src/drivers/
├── acpi.c           → Descoberta e inventario ACPI
├── ac97.c           → Driver de áudio AC97
├── ata.c            → Driver de disco (ATA PIO)
├── e1000.c          → Driver Ethernet Intel 82540EM
├── font.c           → Fonte legada 8x16 e faces nativas Zephyr UI
├── idt.c            → Tabela de interrupções
├── irq.asm          → Handlers de interrupção de hardware
├── isr.asm          → Handlers de exceção do CPU
├── keyboard.c       → Driver de teclado PS/2
├── mouse.c          → Driver de mouse PS/2
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
| 128 | Syscall | `int 0x80` para a App API |

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
idt_register_handler(33, keyboard_handler);
```

`idt_register_handler()` recusa substituir um handler diferente que ja ocupa
o vetor. `idt_register_shared_irq_handler(irq_line, handler)` mantem, em
paralelo, ate quatro handlers por linha IRQ legada. O dispatcher chama
primeiro o handler exclusivo, depois todos os compartilhados e envia um unico
EOI ao PIC. E1000 e RTL8139 usam essa tabela e cada handler percorre as suas
instancias associadas a linha.

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

Exceções vindas de ring 3 registram o vetor, código e PID, encerram somente o
processo de usuário e devolvem a execução ao scheduler. Exceções de ring 0
continuam seguindo para `KERNEL PANIC`.

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
Tecla → IRQ1 → ring buffer → processo System → IPC → processo em foco
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

### Metricas

`keyboard_metrics_t` informa ocupacao atual e capacidade da fila, descartes,
eventos processados e o maior pico de ocupacao desde o boot. Esses valores
aparecem em `kmetrics` e permitem separar atraso de entrada de custo de
renderizacao sem alterar o despacho por IPC. A fila fisica comporta 255
eventos e o processo System encaminha lotes de ate 16; o Shell tambem consome
ate 16 eventos por rodada, abaixo da capacidade util de 31 mensagens IPC.

---

## Mouse Driver (`mouse.c`)

Driver de mouse PS/2 que captura movimentos, cliques e, quando o dispositivo
oferece o protocolo Intellimouse, roda vertical via IRQ12.

### Inicialização

```c
mouse_init();
```

Configura o controlador PS/2 para habilitar o mouse auxiliar e negocia a
sequencia de taxas `200, 100, 80` do Intellimouse. Se o dispositivo nao
confirmar a identificacao `0x03`, o driver mantem o protocolo de tres bytes e
registra um unico aviso; movimento e cliques continuam operacionais.

### Fluxo de Dados

```
Mouse move/clica → IRQ12 → mouse_handler() → ring buffer → process_events() → callback
```

### API

```c
void           mouse_init(void);
void           mouse_process_events(void);
mouse_callback_t mouse_set_callback(mouse_callback_t cb);
int            mouse_get_x(void);
int            mouse_get_y(void);
uint8_t        mouse_get_buttons(void);
int            mouse_has_wheel(void);
```

`mouse_has_wheel()` retorna diferente de zero somente depois que a negociacao
Intellimouse conclui com sucesso. Os pacotes comuns continuam tendo tres bytes;
os de roda usam quatro bytes e geram `MOUSE_EVENT_WHEEL`.

### Callback

```c
void my_handler(mouse_event_t* event) {
    if (event->event == MOUSE_EVENT_PRESS &&
        (event->changed & MOUSE_BTN_LEFT)) {
        /* clique esquerdo */
    }
    if (event->event == MOUSE_EVENT_WHEEL) {
        /* event->wheel positivo representa roda para cima */
    }
}

mouse_set_callback(my_handler);
```

### Cursor

O cursor é renderizado pixel a pixel no framebuffer VESA. A posição é salva antes de desenhar e restaurada ao mover:

```c
static void erase_cursor(void);  // Restaura pixels originais
static void draw_cursor(void);   // Desenha cursor e salva backup
```

Na apresentacao, o driver compara a area que uniria o cursor antigo ao novo
com a soma das duas areas individuais. Movimentos curtos usam uma unica copia;
movimentos distantes copiam as duas regioes minimas. Isso evita transferir uma
faixa grande do framebuffer sem alterar a fila, os callbacks ou os cliques.

### Comando Shell

```bash
mouse          # Mostra X, Y, botoes e disponibilidade da roda
```

### Limitações

- Movimento relativo apenas (sem posição absoluta)
- Velocidade fixa (MOUSE_SPEED = 3)
- Cursor de 12x16 pixels
- Roda vertical opcional; botao central e roda horizontal nao sao tratados

---

## Video e terminal hospedado (`video.c`)

O buffer textual do Shell continua sendo um histórico circular de 200 linhas.
No modo Classic, ele também pode ser uma superfície do Window Manager: o
driver mantém o buffer e a geometria da área interna da janela. Saídas longas
continuam disponíveis para recomposição pelo WM; na digitação, o próprio
terminal pode apresentar apenas as células alteradas e o cursor.

```c
void video_terminal_set_hosted(int hosted);
int  video_terminal_is_hosted(void);
int  video_terminal_draw(int x, int y, int width, int height);
int  video_terminal_take_hosted_dirty(void);
int  video_terminal_present_hosted_dirty(void);
```

`video_terminal_set_hosted(1)` ativa a superfície sem limpar o histórico.
Enquanto ela estiver ativa, `video_print()`, `video_put_char()` e as operações
de scroll atualizam o buffer e acumulam um retângulo sujo. O consumidor pode
usar `video_terminal_present_hosted_dirty()` para apresentar diretamente a
menor região segura; uma tecla comum cobre a célula escrita e a nova posição
do cursor. Saídas assíncronas continuam usando
`video_terminal_take_hosted_dirty()` para solicitar recomposição ao WM.

`video_terminal_draw()` recebe o retângulo VESA de conteúdo, preserva a paleta
VGA, desenha cursor e rodapé de histórico e faz refluxo visual conforme a
largura e a altura disponíveis. Ela exige VESA, backbuffer e pelo menos uma
célula de texto mais o rodapé; em caso contrário retorna erro e registra
`LOG_WARN`. O mínimo estrutural da janela hospedada é aplicado pelo WM, não
pelo terminal. No modo Clássico, o terminal continua usando a apresentação
textual normal.

Durante a segunda metade do boot, `video_begin_update()` mantém os logs no
backbuffer até a cena inicial estar pronta, reduzindo cópias completas causadas
por scroll. `video_flush_updates()` encerra frames de texto pendentes e é usado
pelo panic handler para garantir que um diagnóstico fatal continue visível.

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
4. O scheduler atualiza bloqueios temporizados e preempta somente processos
   interrompidos em ring 3

`timer_get_frequency()` expoe a frequencia configurada para diagnosticos. A
50 Hz, o quantum de ring 3 e 1 tick (20 ms); processos nativos de ring 0
cedem cooperativamente. Os ticks do PIT nao representam tempo de CPU real.

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
tss.esp0 = 0x9F000; // Topo do kernel stack
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
| `vesa_set_clip_rect(x, y, w, h)` | Limita as primitivas de desenho ao retângulo |
| `vesa_reset_clip_rect()` | Remove o limite de desenho atual |

O recorte é temporário e vale para todas as primitivas VESA, inclusive texto,
bitmaps e `vesa_clear()`. O Window Manager o ativa ao chamar o conteúdo de uma
janela hospedada: conteúdo que ultrapassa a área interna fica invisível e não
sobrescreve o Desktop, outras janelas ou a taskbar.

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

### Metricas K1 e K4

`keyboard_get_metrics()` informa ocupacao, capacidade util e descartes
acumulados da fila PS/2. `vesa_get_metrics()` informa apresentacoes efetivas,
copias completas/parciais, bytes copiados e duracoes em ticks. `kmetrics`
tambem calcula `media_bytes` por apresentacao na janela atual; essa media pode
cair mesmo que a quantidade de apresentacoes aumente, pois o cursor distante
pode exigir duas copias pequenas em vez de uma faixa grande. A duracao de uma
copia pode ser `0` quando termina entre dois ticks do PIT; isso significa que
ficou abaixo da resolucao, nao que nao teve custo.

---

## Font (`font.c`)

O driver preserva a fonte bitmap legada **8x16** e oferece a familia derivada
**Zephyr UI Bitmap** em faces nativas 8x16, 10x20 e 12x24. A familia nova usa
somente o subconjunto ASCII imprimivel da Terminus Font 4.49.1 normal e nao
faz redimensionamento durante o desenho.

### Carregamento

```c
font_init();
```

### Obtendo glyph legado

```c
const uint8_t* glyph = font_get_glyph('A');
// glyph[0..15] = 16 bytes representando 8x16 pixels
```

Esse contrato permanece inalterado para Simple, Shell hospedado, Updater e
primitivas fora do MV0.

### Obtendo uma face nativa

```c
const font_face_t* face;
const uint8_t* glyph;

font_get_face(10, 20, &face);
font_get_face_glyph(face, 'A', &glyph);
```

`font_face_t` publica largura, altura, bytes por linha, bytes por glyph,
intervalo de caracteres e um ponteiro imutavel para os dados. Os bits usam
ordem MSB-first; bytes fora de U+0020..U+007E selecionam `?`. As consultas nao
alocam memoria e retornam codigos de erro com log para ponteiros, estados ou
dimensoes invalidas.

Os BDFs, hashes e a licenca ficam em `assets/fonts/terminus`.
`tools/vendor_terminus.py` valida os originais e gera deterministicamente
`src/drivers/font_data.inc`; `make q3check` confirma que o arquivo gerado esta
sincronizado.

---

## ACPI (`acpi.c`)

O driver ACPI cria nas S1.2 e S1.3 um snapshot de leitura antes do paging. Na
S1.4, esse snapshot tambem controla uma unica operacao terminal de S5. A
inicializacao ocorre depois do mapa E820, pois as regioes da EBDA e do BIOS
ainda estao identity-mapped nesse ponto. A busca cobre o primeiro KiB da EBDA
e `0xE0000-0xFFFFF`, sempre em enderecos alinhados a 16 bytes.

O driver valida RSDP, RSDT/XSDT e os checksums das SDTs, prefere XSDT quando
os enderecos cabem em 32 bits e usa RSDT como fallback. Um snapshot estatico
guarda no maximo 64 tabelas e identifica FADT, DSDT e FACS. No maximo 256
entradas da raiz sao examinadas e tabelas maiores que 1 MiB sao recusadas.

`acpi_init()` recebe o mapa E820 apenas durante o bootstrap. Depois do retorno,
o driver zera essa referencia e as consultas `acpi_get_status()`,
`acpi_get_table_at()` e `acpi_find_table()` devolvem somente copias dos
metadados. Nenhum ponteiro fisico e dereferenciado depois que o paging entra
em operacao.

Na S1.3, `acpi_register_t` normaliza os campos GAS e legados da FADT. O
snapshot `acpi_power_info_t`, consultado por copia com
`acpi_get_power_info()`, registra:

- PM1a/PM1b Control, espaco de endereco, largura, offset e tamanho de acesso;
- `SMI_CMD`, valores ACPI enable/disable, `PM1_CNT_LEN` e
  `HW_REDUCED_ACPI`;
- o valor de `SCI_EN` observado e o modo habilitado, desabilitado,
  inconsistente ou desconhecido;
- o estado da declaracao `_S5_` e os tipos A/B quando forem inequivocos.

Somente portas System I/O compativeis com leitura de 16 bits sao acessadas.
Campos MMIO, espacos desconhecidos e enderecos incompativeis permanecem no
snapshot como metadados e nunca sao mapeados ou dereferenciados.

O reconhecedor AML e limitado a `Name(_S5_, Package(...))`: valida
`PkgLength`, limites, quantidade de elementos e constantes inteiras. Ausencia,
malformacao ou mais de uma declaracao valida fecham a capacidade de forma
segura. SSDTs e AML generico nao sao interpretados.

Na S1.4, `mode_enable_available` informa se o modo ACPI pode ser adquirido por
`SMI_CMD` e `s5_transition_ready` consolida todas as pre-condicoes de
seguranca. S5 so fica pronto com snapshot completo, FADT/DSDT validas,
plataforma nao hardware-reduced, PM1a e PM1b opcional em System I/O de 16
bits, uma unica declaracao `_S5_` valida e modo ACPI habilitado ou ativavel.

`acpi_enter_s5()` repete essas validacoes antes da primeira escrita. Se
`SCI_EN` estiver limpo, desabilita interrupcoes, escreve `ACPI_ENABLE` em
`SMI_CMD` e limita a espera por confirmacao a 1.000.000 leituras. Em seguida
preserva os outros bits de PM1, substitui apenas `SLP_TYP`, define `SLP_EN` e
escreve PM1a antes de PM1b. A funcao retorna erro somente quando nenhuma
escrita ocorreu; depois da primeira escrita, qualquer falha termina em HLT.

MMIO, hardware-reduced ACPI, AML generico, `_PTS`, SCI, GPE, suspensao e
hibernacao continuam indisponiveis. A implementacao nao usa a porta privada
`0xB004` do QEMU. A ordem segue o
[modelo de programacao ACPI 6.6](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html)
e o contrato de
[objetos de estado do sistema](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/07_Power_and_Performance_Mgmt/oem-supplied-system-level-control-methods.html).

---

## PCI (`pci.c`)

**Peripheral Component Interconnect** - Barramento para detectar dispositivos de hardware.

### Enumeração

```c
int pci_result = pci_init();
if (pci_result != OK && pci_result != ERR_OVERFLOW) {
    /* O inventario PCI esta indisponivel. */
    return;
}
```

Escaneia 256 buses × 32 devices × 8 functions.

`pci_init()` registra no maximo `PCI_MAX_DEVICES` (64) funcoes na tabela
estatica. Ao atingir esse limite, retorna `ERR_OVERFLOW`, preserva as entradas
ja lidas e permite que o inventario de dispositivos continue de forma parcial.
Cada funcao PCI e lida uma unica vez. `pci_get_device_count()` e
`pci_get_device_at()` devolvem copias seguras para consumidores; um novo scan
somente consulta a configuracao PCI, sem reinicializar ATA, AC97 ou PS/2.

`pci_enable_memory_and_bus_mastering()` habilita explicitamente os bits de
Memory Space e Bus Master para um dispositivo ja enumerado e confirma a leitura
de volta. Ela retorna erro para ponteiro nulo, PCI indisponivel ou configuracao
nao aceita pelo hardware. A API antiga `pci_enable_bus_mastering()` permanece
para os drivers existentes. `pci_enable_io_and_bus_mastering()` oferece a
mesma confirmacao para drivers baseados em port I/O, como o RTL8139.

---

## E1000 (`e1000.c`)

O driver atende ao Intel `8086:100E` (82540EM) usado pelo QEMU. Cada chamada
recebe o dispositivo PCI exato e cria ou recupera uma das quatro instancias:

```c
int e1000_init(const pci_device_t* pci, const char* interface_id,
               ethernet_interface_t* out_interface);
```

Cada instancia possui BAR0 MMIO de 32 bits, IRQ, MAC RAL/RAH, filas DMA de
oito descritores RX/TX, buffers e contadores proprios. O resultado e uma
`ethernet_interface_t` com contexto opaco e callbacks de status, RX, TX e
polling; nao existe mais busca interna pelo primeiro `8086:100E` nem estado
singleton.

A IRQ compartilhada apenas reconhece, contabiliza e marca RX. A copia dos
descritores para a fila estatica e a execucao dos protocolos ocorrem no
polling normal. Ponteiros nulos, instancia inativa, frame invalido, falta de
memoria e timeout retornam erro controlado e nao desativam outras instancias.

## RTL8139 (`rtl8139.c`)

O driver S2.8 atende ao Realtek `10EC:8139` em modo classico, sem C+. Ele usa
BAR0 de port I/O, DMA de 32 bits, quatro buffers TX de 2 KiB e ring RX de
8 KiB + 16 bytes, com extensao de 1,5 KiB para pacotes que cruzam o fim:

```c
int rtl8139_init(const pci_device_t* pci, const char* interface_id,
                 ethernet_interface_t* out_interface);
```

Reset, MAC, RBSTART, TSAD0-3, RCR/TCR e mascaras de interrupcao sao
configurados por instancia. A IRQ reconhece ISR, contabiliza eventos e marca
RX/overflow; parsing do cabecalho RX, retirada do FCS, avanco de CAPR e
protocolos permanecem fora da IRQ. Erro de ring reinicia somente o receptor
da instancia afetada.

O desenho segue o
[datasheet RTL8139C(L)+](https://people.freebsd.org/~wpaul/RealTek/spec-8139cp%28160%29.pdf)
em modo classico e o dispositivo
[`rtl8139` do QEMU](https://www.qemu.org/2018/05/31/nic-parameter/) e o alvo
de validacao. Promiscuidade, multicast, VLAN e modo C+ nao fazem parte do
contrato.

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
