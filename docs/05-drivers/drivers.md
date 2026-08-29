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
├── uhci.c           → Controlador USB UHCI, portas raiz e transferencias USB
├── ehci.c           → Controlador USB EHCI high-speed e transferencias USB
├── usb_hid.c        → Teclado e mouse USB HID Boot
├── rtl8811cu.c      → Probe seguro do USB Realtek RTL8811CU
├── vesa.c           → VESA BIOS Extensions (modo gráfico)
└── video.c          → VGA Text Mode
```

Na EP4.3, `src/drivers/usb_msc.c` complementa `uhci.c` com BOT/SCSI
somente-leitura. Na EP4.4, `src/drivers/usb_hid.c` usa Interrupt IN Boot; os
contratos publicos ficam em `usb_msc.h`, `usb_hid.h` e `uhci.h`.

Na EP7.1B, `src/drivers/ehci.c` fornece o caminho PCI high-speed separado do
UHCI: DMA estatico para queue heads/qTDs, IRQ compartilhada, enumeracao de
portas raiz, descritores, controle, Bulk, Interrupt, timeout e recuperacao.
`src/core/usb_transport.c` seleciona o backend por `controller_model`. HID e
MSC continuam usando UHCI e nao sao redirecionados para EHCI nesta etapa.

Na EP7.1B, `src/drivers/rtl8811cu.c` somente identifica
`USB\VID_0BDA&PID_C811` com `bcdDevice` `0x0200`, verifica a presenca externa
de `RTL8811.BIN` e publica estado/erros. Ele nao executa sequencia de radio,
nao carrega firmware no dispositivo e nao fornece frames Ethernet enquanto a
operacao do chipset nao estiver respaldada por uma referencia tecnica
verificavel.

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
instancias associadas a linha. `idt_get_irq_status()` fornece snapshot
somente-leitura das ocorrencias e da quantidade total de handlers por linha;
`idt_validate_irq_state()` verifica os limites da tabela compartilhada.

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

Exceções vindas de ring 3 registram vetor, código, endereço e PID e são
convertidas em `SIGSEGV`. O processo afetado entra em `ZOMBIE` e o retorno é
redirecionado à trampoline segura do scheduler. Exceções de ring 0 continuam
seguindo para `KERNEL PANIC`.

Desde a SYNC4, os handlers C de exceção, syscall e IRQ chamam a preparação de
sinais no fim do percurso. Na IRQ isso ocorre somente depois do ACK/EOI. O
kernel entrega no máximo um sinal antes do `iret`; nenhuma rotina Assembly de
interrupção foi alterada.

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
Tecla → IRQ1 → bytes brutos → Bottom-Half → input core → processo em foco
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

O driver tambem expoe `keyboard_scancode_to_ascii_shifted(scancode, shifted)`
para consumidores que precisam da mesma tabela com Shift. A funcao legada
`keyboard_scancode_to_ascii()` permanece como atalho para a conversao sem
Shift. As barras ISO/ABNT2 sao normalizadas pelo proprio driver: `/` sem Shift
e `?` com Shift. No adaptador HID ABNT2, o Usage `0x38` representa a posicao
fisica `;/:` e e convertido para o scancode `0x35`; a tecla brasileira `/ ?`
usa o Usage `Keyboard International1` (`0x87`) e e convertida para o scancode
ABNT2 `0x73`. O Usage `0x64` permanece reservado para a tecla ISO `\\|`.

### Metricas

`keyboard_metrics_t` informa ocupacao atual e capacidade da fila, descartes,
eventos processados e o maior pico de ocupacao desde o boot. Esses valores
aparecem em `kmetrics` e permitem separar atraso de entrada de custo de
renderizacao sem alterar o despacho por IPC. A fila fisica comporta 255
eventos e o processo System encaminha lotes de ate 16; o Shell tambem consome
ate 16 eventos por rodada, abaixo da capacidade util de 31 mensagens IPC.
Na SYNC1, o Top-Half da IRQ1 apenas le a porta e preserva o byte bruto. A
montagem de prefixos e a publicacao ocorrem na `Zephyr kworker`; a chamada
normal de `keyboard_process_events()` no processo System tambem drena os bytes
como fallback se a fila diferida estiver cheia.

---

## Mouse Driver (`mouse.c`)

Driver de mouse PS/2 que captura movimentos, cliques e, quando o dispositivo
oferece o protocolo Intellimouse, roda vertical via IRQ12.

### Inicialização

```c
if (mouse_init() != OK) {
    /* Teclado e Shell continuam disponiveis sem o dispositivo. */
}
```

Configura o controlador PS/2 para habilitar o mouse auxiliar e negocia a
sequencia de taxas `200, 100, 80` do Intellimouse. Se o dispositivo nao
confirmar a identificacao `0x03`, o driver mantem o protocolo de tres bytes e
registra um unico aviso; movimento e cliques continuam operacionais.
Timeout, ACK ausente ou falha ao registrar a IRQ retornam erro, deixam o
driver indisponivel e nao bloqueiam o restante da inicializacao.

### Fluxo de Dados

```
Mouse move/clica → IRQ12 → bytes brutos → Bottom-Half → input core → callback
```

### API

```c
int            mouse_init(void);
void           mouse_process_events(void);
mouse_callback_t mouse_set_callback(mouse_callback_t cb);
int            mouse_get_x(void);
int            mouse_get_y(void);
uint8_t        mouse_get_buttons(void);
int            mouse_has_wheel(void);
int            mouse_get_config(mouse_config_t* config);
int            mouse_get_status(mouse_status_t* status);
int            mouse_set_speed(uint8_t speed);
int            mouse_set_acceleration(int enabled);
int            mouse_set_primary_button(mouse_primary_button_t primary);
```

`mouse_has_wheel()` retorna diferente de zero somente depois que a negociacao
Intellimouse conclui com sucesso. Os pacotes comuns continuam tendo tres bytes;
os de roda usam quatro bytes e geram `MOUSE_EVENT_WHEEL`.

`mouse_config_t` mantem velocidade `1-10`, aceleracao e botao principal. Os
padroes sao velocidade `3`, aceleracao desligada e botao esquerdo. Com o botao
direito como principal, esquerda e direita sao trocados antes do callback;
`mouse_get_buttons()` e `mouse_event_t` continuam expondo a mascara efetiva.
`mouse_status_t` acrescenta a mascara bruta, disponibilidade, configuracao,
ultimo erro e total de pacotes descartados para diagnostico.

O Top-Half da IRQ12 somente le o byte auxiliar e agenda trabalho coalescido.
O Bottom-Half monta o pacote completo antes de publicar movimento, botoes e
roda, preservando as transicoes. `mouse_process_events()` conserva a drenagem
normal como fallback para rejeicao da fila diferida. Se a fila bruta saturar,
o driver registra a lacuna, reinicia a montagem depois dela e procura o
proximo cabecalho PS/2 plausivel, evitando manter o ponteiro desalinhado.
Enquanto ainda houver bytes brutos e capacidade no `input core`, o trabalho
solicita reexecucao. Movimentos consecutivos sem roda e com os mesmos botoes
sao acumulados antes de ocupar novas entradas; o `input core` tambem exige a
mesma origem. Roda e mudancas de botoes permanecem entradas distintas. O
consumidor processa ate 32 limites de lote por passagem, acompanhando a vazao
maxima do despacho intermediario sem contabilizar a recusa temporaria do
consumidor como descarte definitivo.

A aceleracao usa somente inteiros: movimento bruto abaixo de 4 conserva `1x`,
entre 4 e 7 usa `1,5x` e a partir de 8 usa `2x`. A velocidade e aplicada depois
da retirada da fila, e a posicao final permanece limitada ao framebuffer.

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
mouse                            # Status bruto/efetivo e configuracao
mouse speed 1                    # Velocidade entre 1 e 10
mouse primary right              # Botao principal left ou right
mouse acceleration on            # Aceleracao on ou off
```

As preferencias permanecem somente em RAM e voltam aos padroes no proximo
boot. Entradas invalidas ou driver indisponivel preservam o estado anterior.

### Limitações

- Movimento relativo apenas (sem posição absoluta)
- Preferencias nao persistem apos reiniciar
- Cursor de 12x16 pixels
- Roda vertical opcional; botao central e roda horizontal nao sao tratados

---

## Video e terminal hospedado (`video.c`)

O buffer textual do Shell continua sendo um histórico circular de 500 linhas.
No modo Classic, ele também pode ser uma superfície do Window Manager: o
driver mantém o buffer e a geometria da área interna da janela. Saídas longas
continuam disponíveis para recomposição pelo WM; cada chamada `video_print()`
agrupa a escrita e recompõe a cauda uma única vez, em vez de redesenhar a
superfície a cada quebra de linha. Na digitação, o próprio terminal pode
apresentar apenas as células alteradas e o cursor.

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

## UHCI (`uhci.c`)

### Bulk e MSC (EP4.3)

`uhci_bulk_transfer()` e sincrona, limitada a 1024 bytes por operacao e a
`UHCI_BULK_TIMEOUT_MS`. O buffer DMA e fixo; os TDs sao fragmentados pelo
`wMaxPacketSize` do endpoint e cada endpoint conserva seu toggle DATA0/DATA1.
Timeout ou erro recupera o controlador UHCI e retorna codigo controlado.

`usb_msc.c` aceita somente classe `0x08`, subclass `0x06`, protocolo `0x50`,
uma interface, LUN 0 e exatamente um Bulk IN/OUT. O BOT valida CBW/CSW,
assinatura, tag, residue e status e implementa somente `INQUIRY`, `TEST UNIT
READY`, `READ CAPACITY(10)` e `READ(10)`. O reset de recuperacao envia Mass
Storage Reset, limpa HALT nos dois endpoints e permite uma unica tentativa
adicional. O provedor publicado em `block_device_t` e somente-leitura e usa
setores de 512 bytes.

O driver UHCI usa somente a porta I/O descrita no BAR0 do controlador PCI e
habilita I/O/bus mastering apenas depois de validar classe, ProgIF, IRQ e
limites do BAR. O agendamento DMA possui frame list de 1024 entradas, um queue
head de controle, pool fixo de TDs e buffers de descritor limitados; todas as
estruturas são páginas físicas alinhadas do PMM.

O handler de IRQ compartilhada apenas reconhece o status e sinaliza trabalho.
`uhci_poll()` processa o estado fora da IRQ, com deadlines absolutos para
reset, controle, `SET_ADDRESS` e recuperação. Cada porta raiz pode ficar
`EMPTY`, `ENUMERATING`, `CONFIGURED` ou `DEGRADED` sem desabilitar as demais.

A enumeração aceita apenas uma configuração, uma interface e seus endpoints,
lê os descritores Device/Configuration, atribui um endereço e executa
`SET_CONFIGURATION`. Não existem hubs, strings, hot-plug ou HID no UHCI. A
EP4.3 acrescenta Bulk síncrono e MSC somente-leitura conforme o contrato acima.

Na EP7.1B, o EHCI separado aceita apenas portas raiz high-speed e uma
configuração simples, usando queue heads/qTDs para controle, Bulk e Interrupt.
Ele também não implementa hubs, strings ou hot-plug; HID/MSC continuam
restritos ao caminho UHCI.

---

## Timer (`timer.c`)

O **PIT** (Programmable Interval Timer) permanece configurado em 50 Hz, com
resolução de 20 ms. `timer_init()` valida frequência e divisor, registra a
IRQ0 e retorna um código de erro; a ausência do PIT é fatal porque scheduler,
uptime e os serviços temporizados dependem dele.

### Configuração

```c
if (timer_init(50U) != OK) {
    panic("TIMER: falha ao inicializar PIT");
}
```

### Serviço de temporizadores R2

O driver mantém tabelas estáticas de 32 timers e 16 proprietários. Ambos usam
handles opacos com slot e geração; destruir um proprietário invalida
atomicamente todos os seus timers. Nomes possuem 16 bytes incluindo o
terminador. Não há alocação dinâmica.

`timer_create()` associa callback e contexto a um proprietário. Um timer
`IDLE` pode ser iniciado por `timer_start_once()` ou
`timer_start_periodic()`, sempre em milissegundos. A conversão arredonda para
cima, usa no mínimo um tick e recusa intervalos acima de `0x7FFFFFFF` ticks.
`timer_cancel()` aceita `IDLE` de forma idempotente e também cancela um
vencimento já marcado como `PENDING`; handles antigos retornam `ERR_INVALID`.

Os prazos são absolutos e monotônicos no contador de 32 bits. A comparação é
segura durante wrap desde que o intervalo respeite o limite. Um periódico
atrasado executa uma vez, avança a partir do prazo anterior e contabiliza os
períodos perdidos, sem deriva nem tempestade de callbacks.

### Contexto de execução

1. Configura o canal 0 do PIT para gerar IRQ0
2. A cada tick, `timer_handler()` é chamado
3. A IRQ incrementa o contador, marca timers vencidos e atualiza os schedulers
4. A IRQ notifica a workqueue quando cria callbacks pendentes
5. A `Zephyr kworker` chama `timer_dispatch_pending()` e executa até oito
   callbacks fora da IRQ; System assume somente no fallback

O prazo periódico seguinte é atualizado antes do callback. Assim, o callback
pode cancelar ou destruir seu próprio timer com segurança. Erros retornados
pelo callback são registrados no log circular e nas estatísticas.

`timer_get_frequency()` expoe a frequencia configurada para diagnosticos. A
50 Hz, o quantum de ring 3 e 1 tick (20 ms); processos nativos de ring 0
cedem cooperativamente. Os ticks do PIT nao representam tempo de CPU real.

`timer_get_stats()`, `timer_get_info()` e `timer_copy_active()` fornecem
snapshots de ocupação, estados, prazos, execuções, cancelamentos, atrasos,
períodos perdidos, erros e operações inválidas. `timer_validate_state()` é
somente-leitura e participa do `regcheck`; `timer_self_test()` usa tabelas
privadas e não altera timers reais. O Shell expõe `timer status`, `timer list`
e `timer check`.

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

### Slots, canais e compatibilidade

O driver mantem quatro slots fixos: `ata0`/`ata1` no canal primario
(`0x1F0`, IRQ 14) e `ata2`/`ata3` no secundario (`0x170`, IRQ 15). O indice
par e master e o impar e slave. `ata_init()` retorna erro quando o canal
primario ou todos os discos falham; falha no registro do canal secundario e
isolada e nao derruba o primario.

`ata_get_device()` e as leituras/escritas legadas continuam apontando para o
primeiro disco presente, usado pelo volume de boot. `ata_get_device_at()`
retorna snapshot por copia; `ata_read_device_sectors()` e
`ata_write_device_sectors()` selecionam um slot sem alterar o estado do
chamador; `ata_get_device_counters()` separa leituras e escritas por disco.
Todo acesso e limitado a LBA28.

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
| FLUSH CACHE | 0xE7 | Sincronizar cache interno quando suportado |

### Interrupcoes

As IRQs 14 e 15 sao registradas para reconhecer e limpar o status dos dois
canais. A transferencia PIO permanece sincronizada por polling com limites e
tentativas finitas; ela nao promete I/O assincrono nem bloqueio de thread. A
SYNC1 nao cria um Bottom-Half artificial para ATA; a fila assincrona pertence
a BLK1/R6.

Durante IDENTIFY, o driver registra o suporte ao bit FLUSH CACHE e a camada de
bloco publica `BLOCK_DEVICE_CAP_FLUSH` somente quando o disco o confirma.
`ata_flush_device()` emite `0xE7` e propaga timeout ou erro ATA. FUA nao e
publicado nesta etapa; USB MSC continua sem FLUSH e FUA.

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

`keyboard_set_focus_cancel_filter()` permite ao runtime registrar uma decisao
contextual para cancelar o aplicativo ring 3 em foco. O driver preserva o F12
como cancelamento geral e usa esse filtro somente para teclas especiais de um
fluxo ativo, como o F11 do RegCheck.

O adaptador do `input core` mantém separadamente Ctrl esquerdo/direito das
origens PS/2 e USB. Quando `C` é pressionado com Ctrl e o foco pertence a um
ZAPP ring3, o acorde é consumido e gera `SIGINT`. No Shell, os scancodes seguem
para `shell_input`, que limpa a linha. Durante job cooperativo, Ctrl+C não
substitui F11/F12/Esc.

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
Durante rescans em contexto de processo, a enumeracao cede CPU a cada oito
barramentos; no bootstrap, a mesma chamada permanece efetivamente sincrona.

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
`ethernet_interface_t` com contexto opaco e callbacks de status, servico de
causas pendentes, RX e TX; nao existe mais busca interna pelo primeiro
`8086:100E` nem estado singleton.

A IRQ compartilhada le e limpa ICR, acumula as causas e agenda o Bottom-Half.
Link, erros e descritores RX sao tratados na `Zephyr kworker`. O callback
`service_pending` executado pelo polling Ethernet preserva a recuperacao se o
agendamento for rejeitado. Os protocolos continuam fora da IRQ.

## RTL8139 (`rtl8139.c`)

O driver S2.8 atende ao Realtek `10EC:8139` em modo classico, sem C+. Ele usa
BAR0 de port I/O, DMA de 32 bits, quatro buffers TX de 2 KiB e ring RX de
8 KiB + 16 bytes, com extensao de 1,5 KiB para pacotes que cruzam o fim:

```c
int rtl8139_init(const pci_device_t* pci, const char* interface_id,
                 ethernet_interface_t* out_interface);
```

Reset, MAC, RBSTART, TSAD0-3, RCR/TCR e mascaras de interrupcao sao
configurados por instancia. A IRQ le e limpa ISR, acumula causas e agenda o
Bottom-Half; RX, link, erros e recuperacao do receptor ocorrem na
`Zephyr kworker`. `service_pending` preserva as causas no polling quando o
agendamento for rejeitado. Parsing do cabecalho RX, retirada do FCS, avanco de CAPR e
protocolos permanecem fora da IRQ.

O desenho segue o
[datasheet RTL8139C(L)+](https://people.freebsd.org/~wpaul/RealTek/spec-8139cp%28160%29.pdf)
em modo classico e o dispositivo
[`rtl8139` do QEMU](https://www.qemu.org/2018/05/31/nic-parameter/) e o alvo
de validacao. Promiscuidade, multicast, VLAN e modo C+ nao fazem parte do
contrato.

No NET0, E1000 e RTL8139 continuam usando os callbacks Ethernet existentes
com copia sincrona. O driver entrega ou consome os dados somente durante o
callback; nenhum driver transfere ownership de DMA ao protocolo. A camada
Ethernet associa um descriptor privado a cada RX/TX e conclui seu lifetime
antes de reciclar o pacote. Nao ha garantia de zero-copy, clones reais,
fragmentos reais ou `sk_buff_t` nesta etapa.

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

---

## UHCI (`uhci.c`)

O driver USB UHCI (Universal Host Controller Interface) atende aos controladores
PCI de classe `0x0C`, subclasse `0x03`, ProgIF `0x00` (ex: Intel PIIX3/PIIX4).

### Estrutura e Recursos

- **Frame List**: Pool DMA de 1024 entradas alinhadas a 4 KiB apontando para Queue Heads (QH).
- **Queue Heads (QH) e Transfer Descriptors (TD)**: Alocador estático interno de TDs e QHs com alinhamento de 16 bytes.
- **Portas Raiz**: Detecção de conexão, reset de porta com tempo mínimo de 50 ms via PIT, detecção de velocidade (Low Speed 1.5 Mbps / Full Speed 12 Mbps) e atribuição sequencial de endereço USB.
- **Transferências de Controle**: SETUP, DATA-IN/OUT e STATUS com toggles automáticos e deadlines absolutos baseados em ticks do PIT.
- **Transferências Bulk**: Fragmentação automática por `wMaxPacketSize`, alternância de data toggle por endpoint e recuperação Mass Storage Reset / Clear Feature.
- **IRQ Compartilhada**: Handler PCI compatível com interrupções compartilhadas via `idt_register_shared_irq_handler()`.

```c
int uhci_init(const pci_device_t* pci, const char* controller_id);
int uhci_poll(void);
int uhci_control_transfer(uint8_t address, uint8_t speed,
                          const usb_setup_packet_t* setup,
                          void* buffer, uint16_t length,
                          uint32_t timeout_ticks);
int uhci_bulk_transfer(uint8_t address, uint8_t endpoint, uint8_t is_in,
                       uint8_t* toggle, void* buffer, uint32_t length,
                       uint32_t timeout_ticks, uint32_t* out_transferred);
```

---

## Input core e fila diferida (EP4.4)

`src/core/input.c` e o ponto comum entre PS/2 e USB HID. O driver PS/2
converte seus dados para HID Usage; o driver USB publica o mesmo formato. O
`input core` mantem filas estaticas separadas para teclado e ponteiro, registra
metricas de ocupacao, descartes e ultimo erro, e encaminha eventos em lotes no
contexto normal do processo System. O Shell, IPC, Window Manager e GUI
continuam recebendo os scancodes Set 1 e `mouse_event_t` ja existentes.
Eventos relativos de movimento consecutivos sao acumulados somente quando
origem, botoes e ausencia de roda coincidem. Transicoes de botoes, roda e
eventos de origens diferentes preservam sua ordem e nunca participam dessa
coalescencia.

`src/core/irq_deferred.c` recebe somente trabalhos estaticos inicializados com
proprietario, linha IRQ, callback e contexto opaco. Agendamentos repetidos sao
coalescidos; um evento durante o callback solicita uma reexecucao. A fila
publica snapshots globais e por IRQ de agendamentos, execucoes, coalescencia,
cancelamentos, rejeicoes e pico.

O handler de IRQ nao executa o callback: ele reconhece o dispositivo e agenda
um trabalho `HIGH`. A `Zephyr kworker` chama `irq_deferred_dispatch()` com
interrupcoes habilitadas; System e o loop principal assumem apenas se a
kworker estiver indisponivel. O cancelamento remove
trabalho pendente da fila ou impede reexecucao, permitindo reutilizacao segura
do proprietario. Capacidade, atribuicao, contexto e invariantes sao exercitados
por fixture privada em `irqstat check`; duracao permanece `N/D`.

---

### Interrupt IN do UHCI

`uhci_interrupt_submit()` cria uma requisicao persistente para um endpoint
Interrupt IN. Cada controlador reserva dois slots de QH/TD/buffer e fases
periodicas no frame list de 1024 entradas. O TD usa deadline absoluto, toggle,
IOC e buffer DMA fixo. `uhci_interrupt_cancel()` remove a fase, termina o QH e
invalida a geracao antes de liberar o slot. Control e Bulk continuam usando o
QH sincrono e seus buffers originais.

O handler UHCI compartilhado apenas limpa o status e marca pendencia. O
polling verifica TDs, distingue NAK esperado, timeout e erro, e agenda a
conclusao pela fila diferida. O callback recebe o buffer somente durante a
chamada; uma transferencia bem-sucedida e rearmada automaticamente depois do
callback. A validacao aceita o intervalo entre a conclusao do TD pelo hardware
e sua coleta pelo polling, quando o QH ja terminou mas a requisicao ainda
mantem o estado ativo.

### USB HID Boot (`usb_hid.c`)

O driver aceita somente interfaces HID classe `0x03`, subclass Boot `0x01`,
protocolo teclado `0x01` ou mouse `0x02`, com um Interrupt IN. A inicializacao
envia `SET_PROTOCOL(Boot)` e `SET_IDLE`. Relatorios de teclado usam oito bytes
e de mouse tres ou quatro bytes. Rollover, tamanho incorreto e relatorios
invalidos incrementam contadores e sao descartados; nao causam espera ocupada
nem panic. Timeout, erro de transporte e cancelamento deixam o registro
`DEGRADED` ou `DISABLED` e sao exibidos por `usb hid status`.

O teclado USB publica Usage IDs e o adaptador atual os transforma nos
scancodes Set 1 usados por F12, Esc, modificadores, setas e cancelamento. O
mouse USB publica movimento relativo, botao e roda; o processamento existente
mantem aceleracao, remapeamento do botao principal, cursor e callbacks do
Desktop Classic.

O comando `usb hid check` valida HID, input core e fila diferida. O alvo
`run-usb-hid` adiciona `usb-kbd` na porta raiz 1 e `usb-mouse` na porta raiz 2
do QEMU. Parser completo de
Report Descriptor, hubs, hot-plug real e EHCI permanecem fora desta etapa.

---

## USB Mass Storage (`usb_msc.c`)

O driver Mass Storage implementa a especificação USB Mass Storage Class Bulk-Only
Transport (BOT) e comandos SCSI primários (SPC/SBC) em modo somente-leitura.

### Características

- **Transporte**: BOT (Bulk-Only Transport) com Command Block Wrapper (CBW) de 31 bytes e Command Status Wrapper (CSW) de 13 bytes.
- **Comandos SCSI Suportados**:
  - `INQUIRY (0x12)`: Leitura de Vendor ID, Product ID e versão do firmware.
  - `TEST UNIT READY (0x00)`: Confirmação de prontidão do drive.
  - `READ CAPACITY 10 (0x25)`: Leitura de LBA máximo e tamanho do setor (512 bytes).
  - `READ 10 (0x28)`: Leitura setorial LBA em blocos de 512 bytes.
- **Integração de Bloco**: Cada LUN ativo é registrado como provedor em `block_device_t` com ID estável `usb-ms-BB:DD.F-pN-aN-l0`.
- **Recusa de Escrita**: Qualquer tentativa de escrita em dispositivo MSC retorna `ERR_UNAVAILABLE`.
- **Recuperação de Falhas**: Em timeout ou stall, executa Mass Storage Reset via pipe de controle seguido de `CLEAR_FEATURE(ENDPOINT_HALT)` nos endpoints Bulk IN e OUT.

```c
int usb_msc_init(uint8_t controller_index, uint8_t device_address,
                 uint8_t speed, uint8_t endpoint_in, uint8_t endpoint_out,
                 uint16_t max_packet_in, uint16_t max_packet_out,
                 const char* device_id);
int usb_msc_read_sectors(uint32_t msc_index, uint32_t lba, uint32_t count,
                         uint8_t* buffer);
uint32_t usb_msc_get_count(uint32_t* out_count);
int usb_msc_get_at(uint32_t index, usb_msc_info_t* out_info);
```
