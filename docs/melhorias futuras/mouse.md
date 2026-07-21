# Roadmap: Mouse

## Resumo de Progresso

| Componente | Status |
|------------|--------|
| IRQ12 assembly stub (irq.asm) | Pronto |
| IDT gate para INT 44 (idt.c) | Pronto |
| PIC routing (slave PIC) | Pronto |
| Registro do handler (IRQ12) | Pronto |
| Driver PS/2 mouse | Pronto |
| Fila de eventos | Pronto |
| Callback API | Pronto |
| Renderizacao do cursor | Pronto |
| Makefile | Pronto |
| Integracao no main loop | Pronto |
| Clique no Desktop | Pronto |
| Clique na Taskbar | Pronto |
| Comando shell `mouse` | Pronto |

---

## Arquivos Novos

### src/drivers/mouse.c

Driver principal do mouse PS/2.

```
Inicializacao PS/2:
  - Enviar comando 0xA8 na porta 0x64 (habilitar porta auxiliar)
  - Ler byte de configuracao via 0x20/0x60
  - Setar bits 1 (IRQ auxiliar) e 2 (mouse clock disable)
  - Enviar 0xD3 + 0x00 na porta 0x64/0x60 (resetar mouse)
  - Enviar 0xF4 na porta 0x64/0x60 (habilitar data reporting)

Handler de IRQ12:
  - Registrado com: register_interrupt_handler(44, mouse_handler)
  - Le 3 bytes de 0x60 (dx, dy, botoes)
  - Enfileira em ring buffer

Fila de eventos:
  - Ring buffer com pacotes de 3 bytes
  - volatile head/tail para seguranca ISR
  - Tamanho recomendado: 64 entradas

API:
  - mouse_init()         - inicializa hardware e registra IRQ
  - mouse_process_events() - chamado no main loop, drain da fila
  - mouse_set_callback()   - salva/restaura callback (igual keyboard)
  - mouse_get_x()          - posicao X atual do cursor
  - mouse_get_y()          - posicao Y atual do cursor
  - mouse_get_buttons()    - estado dos botoes
```

### src/include/drivers/mouse.h

Header do driver.

```
#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

typedef struct {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
} mouse_packet_t;

typedef void (*mouse_callback_t)(mouse_packet_t*);

void mouse_init(void);
void mouse_process_events(void);
mouse_callback_t mouse_set_callback(mouse_callback_t cb);
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);

#endif
```

---

## Alteracoes em Arquivos Existentes

### Makefile

Adicionar no topo:
```makefile
MOUSE_C = src/drivers/mouse.c
MOUSE_OBJ = build/mouse.o
```

Adicionar regra:
```makefile
$(MOUSE_OBJ): $(MOUSE_C)
	@if not exist build mkdir build
	$(GCC) $(CFLAGS) -c $< -o $@
```

Adicionar `$(MOUSE_OBJ)` a lista `OBJS`.

### src/kernel/kernel.c

Na inicializacao (apos keyboard_init):
```c
mouse_init();
```

No loop principal (apos keyboard_process_events):
```c
mouse_process_events();
```

---

## Fluxo de Dados

```
Mouse PS/2 hardware
       |
       v
IRQ12 -> irq12 (asm, ja existe em irq.asm)
       |
       v
irq_handler() (idt.c, ja existe)
       |
       v
mouse_handler() [NOVO - le 3 bytes de 0x60]
       |
       v
event_queue[] (ring buffer, NOVO)
       |
       v
mouse_process_events() [NOVO - chamado no main loop]
       |
       v
mouse_callback(packet) [NOVO - despacha para quem registrou]
       |
       v
Cursor rendering (usa vesa_put_pixel / vesa_fill_rect / vesa_get_pixel)
```

---

## Renderizacao do Cursor

O cursor deve ser desenhado na camada VESA (pixel-level):

1. **Save**: Ler pixels do fundo com `vesa_get_pixel()` antes de desenhar
2. **Draw**: Desenhar o cursor (formato seta ou ponteiro) com `vesa_put_pixel()` ou `vesa_fill_rect()`
3. **Restore**: Redesenhar os pixels do fundo salvos ao mover
4. **Tamanho recomendado**: 12x16 ou 16x16 pixels
5. **Cor**: Branco com borda preta (ou inverso quando sobre elementos)

Posicao do cursor em coordenadas de tela:
- Usar as mesmas coordenadas do VESA (1024x768)
- Converter para coordenadas de texto (SCREEN_COLS x SCREEN_ROWS) quando necessario

---

## Limitacoes

- PS/2 mouse suporta apenas movimentacao relativa (nao absoluta como USB).
- Velocidade do cursor depende da sensibilidade configurada no driver.
- *(Resolvido)* O *flickering* (cintilação) ao sobrepor áreas dinâmicas foi resolvido pela implementação de renderização com Double Buffering no VESA.

---

## Referencias

- IBM PS/2 Mouse Interface (port 0x60/0x64 protocol)
- OS Dev Wiki: PS/2 Mouse
- OS Dev Wiki: IRQs
