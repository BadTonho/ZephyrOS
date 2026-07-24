# Estado da documentacao

## Auditoria atual

**Revisado em 24 de julho de 2026.** Este documento define como interpretar a
documentacao do ZephyrOS e evita que um plano antigo seja confundido com uma
funcionalidade entregue.

## Fonte de verdade por assunto

| Assunto | Fonte principal | Uso |
|---|---|---|
| Estado global e fases concluidas | [`../ROADMAP.md`](../ROADMAP.md) | Visao resumida do projeto. |
| Ordem dos proximos trabalhos | [`roadmaps/README.md`](roadmaps/README.md) | Planejamento executavel por dependencia. |
| Arquitetura e modulos atuais | [02 - Arquitetura](02-arquitetura/arquitetura.md) | Mapa de componentes e inicializacao. |
| Kernel, memoria, drivers, processos e FS | Capitulos 03 a 08 | Contratos tecnicos atuais. |
| Shell e atalhos | [09 - Shell](09-shell/shell.md) e [Atalhos](atalhos_e_comandos.md) | Comandos e interacoes atuais. |
| Desktop e aplicativos nativos | Capitulos 12 e 13 | Interfaces classica e moderna atuais. |
| App API e apps ring 3 | [API de Aplicativos e Syscalls](melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md) | ABI 0.3, ZAPP, foco e limites. |

## Estado tecnico documentado

- Desktop inicia como cena padrao; o terminal e aberto explicitamente pelo
  Desktop, Menu Iniciar ou taskbar.
- Shell possui scrollback de 200 linhas e `clear` apaga tela e historico.
- Desktop, Explorer, Settings e a janela do Task Manager oferecem modo moderno
  com fallback classico. O comando `taskmgr` preserva a TUI de diagnostico.
- App API 0.3, syscalls 0 a 9, `int 0x80` em DPL3, arquivos, IPC, loader ZAPP,
  argumentos simples e foco seguro foram validados no QEMU.
- `echo` e a primeira ferramenta migrada para ring 3, sempre com fallback
  nativo controlado.
- O Window Manager generico continua textual; a taskbar ainda usa a identidade
  visual existente. Esses dois pontos pertencem ao backlog de interface.

## Como ler os roadmaps antigos

Os arquivos em `melhorias futuras/` sao backlogs detalhados por produto. Eles
podem conter wireframes, estimativas e checklists escritos antes de uma fase
ser implementada. Nao devem, sozinhos, ser usados como prova do estado atual.
Quando houver conflito, a precedencia e:

1. codigo e testes recentes no QEMU;
2. `ROADMAP.md`;
3. `docs/roadmaps/`;
4. capitulos tecnicos atuais;
5. backlog detalhado em `melhorias futuras/`.

Ao terminar uma fase, atualizar o roadmap por etapa, o documento tecnico
afetado, os comandos/atalhos e o `ROADMAP.md`. Isso mantem o historico de
planejamento sem perder a descricao do sistema que realmente existe.

## Documentos revisados nesta auditoria

- Raiz: `README.md`, `ROADMAP.md`, `AGENTS.md` e `lembrar.md` (este ultimo e
  local e permanece ignorado pelo Git).
- Referencias operacionais: `docs/indice.md`, `docs/regras.md` e
  `docs/atalhos_e_comandos.md`.
- Capitulos 01 a 13, incluindo boot, kernel, drivers, memoria, processos,
  arquivos, Shell, extras, referencias, Desktop e aplicativos.
- Todos os roadmaps em `docs/melhorias futuras/`, classificados como estado
  atual quando a fase ja foi entregue ou como backlog quando ainda planejada.
- Novo conjunto `docs/roadmaps/`, que agrupa as proximas etapas por dependencia.
