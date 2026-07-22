# Responsividade do sistema

## Resumo de Progresso

- [x] Adicionar frames VESA com regiao de atualizacao.
- [x] Reduzir copias completas da taskbar e do relogio.
- [x] Coalescer o arraste da janela grafica do Task Manager.
- [x] Atualizar metricas do Task Manager a cada 100 ms.
- [x] Ativar otimizacao segura na compilacao C.
- [x] Adicionar historico rolavel de 200 linhas ao Shell.
- [ ] Medir tempos de renderizacao em hardware real.

## Atalhos

Esta melhoria nao adiciona comandos novos. O comando `clear` agora remove
tambem o historico do terminal. Os comandos existentes continuam validos nos
modos `classic` e `modern`.

## Fases

### Fase 1 - Renderizacao eficiente

- O VESA acumula uma regiao suja entre `vesa_frame_begin()` e
  `vesa_frame_end()`.
- Frames localizados usam `vesa_frame_begin_region()` e fazem uma unica copia.
- A area antiga do cursor e incluida antes de qualquer redesenho parcial.
- O caminho classico continua usando VGA sem depender do backbuffer.

### Fase 2 - Atualizacao responsiva

- O timer permanece em 50 Hz.
- O Task Manager atualiza metricas a 10 Hz.
- Movimentos do mouse durante o arraste sao agrupados ate o proximo ciclo.
- O relogio da taskbar continua atualizando uma vez por segundo.

### Fase 3 - Otimizacao da compilacao

- O codigo C usa `-O2` e `-fno-strict-aliasing`.
- A copia de framebuffer em 32 bits usa transferencia por palavra.

### Fase 4 - Terminal rolavel

- O Shell armazena as ultimas 200 linhas de texto em memoria fixa, sem
  `kmalloc`.
- `Seta para Cima`, `Seta para Baixo`, `Page Up`, `Page Down`, `Home` e `End`
  navegam pelo historico.
- A digitacao retorna ao fim do historico antes de escrever no prompt.
- A captura e suspensa enquanto Desktop e aplicativos desenham suas interfaces,
  evitando poluir o terminal.

## Limitacoes

- O custo de cada cena completa ainda depende da resolucao VESA.
- A frequencia maxima continua limitada pelo timer de 50 Hz.
- Nao ha medicao persistente de FPS ou tempo de renderizacao nesta etapa.

## Referencias

- `src/drivers/vesa.c`
- `src/include/drivers/vesa.h`
- `src/drivers/video.c`
- `src/include/core/video.h`
- `src/drivers/mouse.c`
- `src/shell/shell.c`
- `src/shell/taskmanager.c`
- `Makefile`
