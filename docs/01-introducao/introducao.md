# 01 - Introdução

## Visão Geral

O ZephyrOS é um sistema operacional desenvolvido do zero em C + Assembly (x86), com o objetivo de ser funcional e completo o suficiente para uso real. O projeto começou como exercício educacional e evoluiu para um OS com ambições de produção.

## O que o ZephyrOS faz

- Inicia sozinho (bootloader → Protected Mode)
- Gerencia memória (detecção E820, bitmap allocator, heap, paging)
- Roda processos e threads com scheduler preemptivo
- Lê e escreve arquivos em disco (FAT12, FAT32 com subdiretórios)
- Suporta imagens BMP e áudio WAV com áudio AC97
- Sistema robusto de Comunicação entre Processos (IPC)
- Interface Dual Completa: Fallback clássico TUI e GUI Moderna (VESA/Pixel-Level)
- Editor de texto, media player, file manager, task manager e gerenciador de janelas
- Suporte a mouse PS/2 com integração à UI

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

O sistema funciona em emuladores (QEMU) com todos os módulos listados operando. Existem limitações técnicas conhecidas que estão sendo endereçadas — ver `ROADMAP.md` e os documentos na pasta `docs/melhorias futuras/` para detalhes.

## Documentação

A documentação está organizada seguindo a ordem de execução do sistema:

1. **Bootloader** — como tudo começa
2. **Kernel** — o coração do sistema
3. **Drivers** — comunicação com hardware
4. **Memória** — gerenciamento de recursos
5. **Processos** — execução concorrente
6. **Sistema de Arquivos** — persistência
7. **Shell** — interface com o usuário
