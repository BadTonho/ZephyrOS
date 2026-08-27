# Dividas tecnicas da v1.0.0

## Objetivo

Este documento e a fonte canonica das dividas tecnicas aceitas que devem ser
quitadas antes do fechamento da v1.0.0 do ZephyrOS. Ele nao substitui os
roadmaps: cada roadmap continua responsavel pelo escopo da implementacao, e
este registro concentra identidade, impacto, evidencia e criterio de quitacao.

Pendencias funcionais ainda planejadas, ideias futuras e itens fora de escopo
nao sao dividas tecnicas por si so. Um item entra aqui somente quando uma
limitacao conhecida e aceita explicitamente para permitir o encerramento de
uma etapa.

## Convencoes

- Identificador imutavel no formato `DT100-NNN`, em ordem crescente.
- Estados permitidos: `ACEITA`, `EM TRATAMENTO` e `QUITADA`.
- O roadmap proprietario deve apontar para o identificador da divida.
- A quitacao exige o criterio de saida atendido, evidencia reproduzivel e o
  registro da validacao em `registro-validacoes.md`.
- Itens nao podem ser removidos nem marcados como quitados apenas porque o
  sintoma deixou de ser observado em uma execucao isolada.
- Uma divida quitada permanece neste documento como historico, com data e
  referencia da validacao final.

## Resumo

| ID | Estado | Origem | Responsavel | Alvo |
|---|---|---|---|---|
| `DT100-001` | ACEITA | SYNC1 | Roadmap 03 / K5 | v1.0.0 |

## DT100-001 - RegCheck full e entrada PS/2

- **Estado:** `ACEITA`.
- **Aceita em:** 2026-08-27 00:19 (America/Sao_Paulo).
- **Origem:** SYNC1 - Top-Half e Bottom-Half de interrupcoes.
- **Responsavel:** [Roadmap 03 - Etapa K5](../roadmaps/03-kernel-e-desempenho.md#etapa-k5---otimizacao-sistemica-para-a-v100).
- **Versao limite:** v1.0.0.

### Motivo da aceitacao

A infraestrutura da SYNC1 e seus diagnosticos foram validados e terminam em
`OK`. A otimizacao do caminho cooperativo de `regcheck full` foi separada da
entrega de concorrencia para ser executada com medicao sistemica na K5, sem
reabrir a SYNC1 nem antecipar a `kworker` da SYNC3.

### Impacto conhecido

Durante entrada manual extrema enquanto `regcheck full` esta em execucao, o
pipeline PS/2 pode saturar, interromper temporariamente a atualizacao do cursor
e descartar pacotes. O mouse recupera o funcionamento ao final do job e os
diagnosticos estruturais continuam aprovados, mas a perda impede considerar o
cenario otimizado para a v1.0.0.

### Evidencia de referencia

Na ultima execucao registrada, a IRQ12 passou de 305 ocorrencias, 86
Bottom-Halfs e 230 coalescencias para 25.421 ocorrencias, 397 Bottom-Halfs e
2.548 coalescencias. Nao houve rejeicao diferida, mas o mouse terminou com
22.537 pacotes descartados e `ERR_OVERFLOW`.

### Escopo da quitacao

- Medir separadamente as fases de `regcheck full` e os ciclos do processo
  System.
- Correlacionar IRQ1/IRQ12, filas brutas e normalizadas, `input core` e
  execucoes diferidas no mesmo cenario reproduzivel.
- Ajustar orcamentos, pontos de yield e coalescencia sem ocultar descarte nem
  fundir teclas, roda ou transicoes de botoes.
- Preservar Top-Halves minimos e callbacks diferidos somente no processo
  System, com interrupcoes habilitadas.

### Criterio de quitacao

No mesmo ciclo de validacao, `irqstat check` e `regcheck full` devem terminar
em `OK`; movimento, clique, arraste e roda intensos devem produzir incremento
zero de pacotes descartados e nenhum novo `ERR_OVERFLOW`; teclas, roda e
transicoes de botoes devem ser preservadas; nao pode haver rejeicao diferida
permanente; e as interfaces Classic e Shell devem permanecer responsivas.

### Procedimento de validacao

Executar no QEMU, registrando os contadores antes e depois do estresse:

```text
irqstat check
irqstat list
mouse
regcheck full
irqstat list
mouse
```

Durante `regcheck full`, mover o ponteiro, clicar, arrastar e usar a roda. A
validacao final e seu horario devem ser registrados em
`registro-validacoes.md` antes da mudanca do estado para `QUITADA`.

## Dividas quitadas

Nenhuma ate o momento.

## Referencias

- [Roadmap 03 - Kernel e desempenho](../roadmaps/03-kernel-e-desempenho.md)
- [Roadmap 12 - Concorrencia e sincronizacao](../roadmaps/12-concorrencia-e-sincronizacao.md)
- [Responsividade do sistema](../melhorias%20futuras/responsividade%20do%20sistema.md)
- [Metricas de otimizacao](metricas.md)
- [Registro de validacoes](registro-validacoes.md)
