# 01 - Introdução

## Visão Geral

O MiniOS é um sistema operacional educacional que demonstra os conceitos fundamentais de como um SO funciona.

## Objetivos de Aprendizado

Ao estudar o código do MiniOS, você vai aprender:

1. **Como um computador liga** — do BIOS ao primeiro código executado
2. **Modo protegido** — como o processador muda de 16-bit para 32-bit
3. **Drivers** — como o software comunica com o hardware
4. **Memória** — como o SO gerencia RAM
5. **Processos** — como múltiplas tarefas rodam ao mesmo tempo
6. **Sistema de arquivos** — como dados são persistidos em disco
7. **Shell** — como o usuário interage com o sistema

## Tecnologias Utilizadas

| Tecnologia | Uso |
|-----------|-----|
| **Assembly x86** | Bootloader, ISRs, context switch |
| **C** | Kernel, drivers, shell |
| **Makefile** | Sistema de build |
| **NASM** | Assembler (monta Assembly) |
| **GCC cross-compiler** | Compilador C freestanding |
| **QEMU** | Emulador para testes |

## Pré-requisitos para Estudar

- Conhecimento básico de **C** (ponteiros, structs, funções)
- Conhecimento básico de **Assembly** (registradores, instruções)
- Conhecimento de **como um PC funciona** (CPU, memória, periféricos)

## Como Usar Esta Documentação

A documentação está organizada em categorias, seguindo a ordem que o sistema é executado:

1. Comece pelo **Bootloader** (como tudo começa)
2. Depois o **Kernel** (o coração)
3. Os **Drivers** (comunicação com hardware)
4. **Memória** (gerenciamento de recursos)
5. **Processos** (execução concorrente)
6. **Sistema de Arquivos** (persistência)
7. **Shell** (interface com o usuário)
