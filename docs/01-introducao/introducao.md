# 01 - Introdução

## Visão Geral

O ZephyrOS é um sistema operacional desenvolvido do zero em C + Assembly (x86), com o objetivo de ser funcional, confiável e utilizável por pessoas. O projeto está sendo estruturado para evoluir de um ambiente de validação em QEMU para hardware real, com requisitos de produção acompanhados no roadmap.

## O que o ZephyrOS faz

- Inicia sozinho (bootloader → Protected Mode)
- Gerencia memória (detecção E820, bitmap allocator, heap, paging)
- Roda processos e threads com scheduler preemptivo
- Lê e escreve arquivos em disco (FAT12, FAT32 com subdiretórios)
- Suporta imagens BMP e áudio WAV com áudio AC97
- Sistema robusto de Comunicação entre Processos (IPC)
- Interface Dual Completa: Fallback simple TUI e GUI Classic (VESA/Pixel-Level)
- Editor de texto, media player, file manager, task manager e gerenciador de janelas
- Suporte a mouse PS/2 com integração à UI
- App API 0.5 com VFS, syscalls, processos ring 3, sinais, loader ZAPP e argumentos

## Stack Técnica

| Tecnologia | Uso |
|-----------|-----|
| **Assembly x86** | Bootloader, ISRs, context switch |
| **C** | Kernel, drivers, apps |
| **NASM** | Assembler |
| **GCC cross-compiler** | Compilador C freestanding (i686-elf) |
| **GNU ld** | Linker |
| **QEMU** | Emulador para testes |

## Status Atual

O sistema funciona em emuladores (QEMU) com todos os módulos listados operando.
As Fases 1 a 6B da plataforma de aplicativos foram validadas; a próxima etapa
é a migração gradual de ferramentas CLI, preservando fallback nativo. Consulte
`ROADMAP.md`, `docs/roadmaps/` e os documentos em `docs/melhorias futuras/`.

## Documentação

A documentação está organizada seguindo a ordem de execução do sistema:

1. **Bootloader** — como tudo começa
2. **Kernel** — o coração do sistema
3. **Drivers** — comunicação com hardware
4. **Memória** — gerenciamento de recursos
5. **Processos** — execução concorrente
6. **Sistema de Arquivos** — persistência
7. **Shell** — interface com o usuário
