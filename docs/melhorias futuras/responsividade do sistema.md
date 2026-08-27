# Responsividade do sistema

## Resumo de Progresso

- [x] Adicionar frames VESA com regiao de atualizacao.
- [x] Reduzir copias completas da taskbar e do relogio.
- [x] Coalescer o arraste da janela grafica do Task Manager.
- [x] Atualizar metricas do Task Manager a cada 100 ms.
- [x] Ativar otimizacao segura na compilacao C.
- [x] Adicionar historico rolavel de 500 linhas ao Shell.
- [ ] Medir tempos de renderizacao em hardware real.
- [ ] Na v1.0.0, otimizar `regcheck full` e o ciclo System com métricas por
  fase, eliminando o overflow PS/2 observado sob entrada intensa.

## Atalhos

Esta melhoria nao adiciona comandos novos. O comando `clear` agora remove
tambem o historico do terminal. Os comandos existentes continuam validos nos
modos `simple` e `classic`.

## Fases

### Fase 1 - Renderizacao eficiente

- O VESA acumula uma regiao suja entre `vesa_frame_begin()` e
  `vesa_frame_end()`.
- Frames localizados usam `vesa_frame_begin_region()` e fazem uma unica copia.
- A area antiga do cursor e incluida antes de qualquer redesenho parcial.
- Ao mover o cursor, a apresentacao escolhe a uniao das posicoes ou duas
  regioes minimas conforme o menor volume de bytes a copiar.
- O caminho Simple continua usando VGA sem depender do backbuffer.

### Fase 2 - Atualizacao responsiva

- O timer permanece em 50 Hz.
- O Task Manager atualiza metricas a 10 Hz.
- Movimentos do mouse durante o arraste sao agrupados ate o proximo ciclo.
- O relogio da taskbar continua atualizando uma vez por segundo.

### Fase 3 - Otimizacao da compilacao

- O codigo C usa `-O2` e `-fno-strict-aliasing`.
- A copia de framebuffer em 32 bits usa transferencia por palavra.

### Fase 4 - Terminal rolavel

- O Shell armazena as ultimas 500 linhas de texto em memoria fixa, sem
  `kmalloc`.
- `Seta para Cima`, `Seta para Baixo`, `Page Up`, `Page Down`, `Home` e `End`
  navegam pelo historico.
- A digitacao retorna ao fim do historico antes de escrever no prompt.
- A captura e suspensa enquanto Desktop e aplicativos desenham suas interfaces,
  evitando poluir o terminal.

### Fase 5 - Responsividade sistemica da v1.0.0

- Medir o tempo de cada fase de `regcheck full` antes de alterar orcamentos.
- Correlacionar ticks, ciclos do processo System, IRQ1/IRQ12 e picos das filas
  de entrada.
- Preservar roda, teclas e transicoes de botoes; somente movimento equivalente
  pode ser coalescido.
- Exigir zero descarte no cenario reproduzivel de entrada intensa.

## Limitacoes

- O custo de cada cena completa ainda depende da resolucao VESA.
- A frequencia maxima continua limitada pelo timer de 50 Hz.
- Nao ha medicao persistente de FPS ou tempo de renderizacao nesta etapa.
- Ate a v1.0.0, `regcheck full` pode ficar lento e saturar a entrada PS/2 sob
  estresse manual extremo; a SYNC1 permanece aberta por esse motivo.

## Referencias

- `src/drivers/vesa.c`
- `src/include/drivers/vesa.h`
- `src/drivers/video.c`
- `src/include/core/video.h`
- `src/drivers/mouse.c`
- `src/shell/shell.c`
- `src/shell/taskmanager.c`
- `Makefile`
