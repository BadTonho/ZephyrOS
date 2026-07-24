# Resiliencia e fallback seguro — ZephyrOS

## Resumo de Progresso

- [x] Criar registro fixo de estados dos componentes.
- [x] Registrar falhas sem depender do heap.
- [x] Adicionar fallback de VESA para VGA/classic.
- [x] Verificar falhas na criação dos processos principais.
- [x] Adicionar comando shell `health`.
- [x] Isolar processos em modo usuário.
- [x] Recuperar page faults de aplicações ring 3 sem reiniciar o kernel.

## Atalhos

| Comando | Ação |
|---------|------|
| `health` | Lista estado, falhas e último código dos componentes |
| `guimode classic` | Usa a interface clássica |
| `guimode modern` | Usa a interface moderna quando VESA e backbuffer estão disponíveis |

## Fases

### Fase 1 — Fallback operacional

- Gerenciador estático em `src/core/recovery.c`.
- Estados `READY`, `DEGRADED`, `DISABLED` e `UNKNOWN`.
- Códigos de retorno em paging, VESA e registro de handlers da IDT.
- Falhas de disco, filesystem, AC97 e processos não interrompem o boot.
- Entradas protegidas para Task Manager, File Manager, Settings, Media Player,
  Editor, GUI Test e Window Manager.
- Dependências ausentes bloqueiam somente o aplicativo afetado e aparecem no
  comando `health`.
- Falhas recuperáveis de leitura, alocação e criação de janelas marcam o
  componente como `DEGRADED` sem derrubar o kernel.
- Cliques do mouse geram solicitações IPC; o processo System não executa
  diretamente os loops bloqueantes dos aplicativos.

### Fase 2 — Isolamento de processos ✅

- Aplicações ZAPP executam fora do contexto privilegiado, em ring 3.
- Page fault, general protection fault, invalid opcode e falhas de segmento
  originadas em ring 3 encerram apenas o processo afetado.
- O resultado da falha é preservado para `health`; Shell, kernel e demais
  processos continuam ativos.

## Limitações

- Exceções fatais do kernel continuam exibindo `KERNEL PANIC`.
- Funções C não são capturadas automaticamente; cada operação precisa retornar e verificar um código.
- Não há reinício automático de componentes nesta etapa.

## Referências

- `src/core/recovery.c`
- `src/include/core/recovery.h`
- `src/kernel/kernel.c`
- `src/drivers/idt.c`
- `src/memory/paging.c`
