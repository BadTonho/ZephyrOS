# Programadores — ZephyrOS v0.1

> **Distribuição:** Este aplicativo é distribuído como pacote `.zephyrosapp` através do [Gerenciador de Aplicativos](gerenciador%20de%20aplicativos.md). Não é compilado no kernel padrão.

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1. Editor de código avançado | 48 | 0 | 0 | 48 |
| 2. Terminal e console | 32 | 0 | 0 | 32 |
| 3. Gerenciador de dependências | 36 | 0 | 0 | 36 |
| 4. Compilador e build system | 42 | 0 | 0 | 42 |
| 5. Depurador e profiling | 38 | 0 | 0 | 38 |
| 6. Interface TUI | 52 | 0 | 0 | 52 |
| 7. Integração com sistema | 28 | 0 | 0 | 28 |
| **TOTAL** | **276** | **0** | **0** | **276** |

**Progresso geral: 0%** (0/276 itens completos)

---

## Atalhos de Teclado

| Atalho | Ação |
|--------|------|
| F3 | Abrir IDE / Editor de Código |
| Shift+F3 | Abrir Terminal do Desenvolvedor |
| F8 | Gerenciar dependências |
| Esc | Fechar janela |
| Tab | Alternar entre abas |
| Setas | Navegar |
| Enter | Selecionar/Executar |
| Ctrl+S | Salvar arquivo |
| Ctrl+B | Build (compilar) |
| Ctrl+D | Depurar |

---

## Funcionalidades Planejadas

- Editor de código com syntax highlight para múltiplas linguagens
- Terminal integrado com comandos de build
- Gerenciador de dependências (instalar/remover bibliotecas)
- Sistema de build (Makefile generator)
- Depurador básico (breakpoints, step, inspect)
- Suporte a linguagens: C, Assembly, Python (scripts)
- Integração com o filesystem do ZephyrOS

---

## Notas de Implementação

1. **Prioridade** — Este é um módulo avançado. Depende do framework de aplicativos e possivelmente do Network Manager para download de dependências.

2. **Inspiração** — O conceito é similar a um IDE leve como o Notepad++ ou Geany, adaptado para o ambiente bare-metal do ZephyrOS.

3. **Extensibilidade** — O editor deve suportar plugins para adicionar novas linguagens e funcionalidades.
