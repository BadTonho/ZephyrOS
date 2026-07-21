# Explorer moderno — ZephyrOS

## Resumo de Progresso

- [x] Adicionar `FM_MODE_CLASSIC` e `FM_MODE_MODERN`.
- [x] Selecionar o modo pelo `guimode` global.
- [x] Criar layout moderno com janela, barra de ferramentas, endereço,
  acesso rápido, lista detalhada e status.
- [x] Modernizar ajuda, entrada de nomes, confirmação e visualização de arquivos.
- [x] Manter fallback automático para a TUI quando VESA/backbuffer não estiver disponível.
- [ ] Adicionar interação por mouse na lista de arquivos.
- [ ] Adicionar ícones BMP.

## Atalhos

| Comando | Ação |
|---------|------|
| `guimode modern` | Ativa o visual moderno do Desktop e do Explorer |
| `guimode classic` | Restaura o visual TUI |
| `explorer` | Abre o Explorer usando o modo selecionado |

## Fases

### Fase 1 — Visual moderno do Explorer

- Reutiliza `gui_draw_panel`, `gui_draw_text` e `gui_draw_window_frame`.
- Mantém a lista detalhada para preservar a organização atual.
- Mantém operações FAT32, teclado, IPC e recovery sem alteração de contrato.
- Usa um único ciclo de frame para o redesenho gráfico e inclui a taskbar na cena.

### Fase 2 — Interação gráfica

- Seleção por clique simples.
- Abertura por duplo clique.
- Navegação gráfica da barra lateral e da lista.

## Limitações

- A Fase 1 não adiciona clique em arquivos, arrastar e soltar ou novo Window Manager.
- O modo clássico continua sendo o fallback para VESA ou backbuffer indisponível.
- Falhas de desenho recuperáveis não devem causar `panic` do kernel.

## Referências

- `src/filemanager/filemanager.c`
- `src/include/ui/filemanager.h`
- `src/gui/gui.c`
- `src/include/ui/gui.h`
