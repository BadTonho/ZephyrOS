# MiniOS

Sistema operacional educacional desenvolvido do zero em C + Assembly (x86).

---

## O que é o MiniOS?

O MiniOS é um sistema operacional funcional criado para fins educacionais. Ele demonstra como um SO real funciona internamente, desde o boot do computador até um shell interativo onde o usuário pode digitar comandos.

## O que ele faz?

- Inicia sozinho (bootloader)
- Mostra mensagens na tela (VGA text mode + VESA gráfico)
- Responde a teclado (input do usuário)
- Gerencia memória (alocação dinâmica, compressão LZSS)
- Roda processos e threads concorrentemente
- Lê e escreve arquivos em disco (FAT12, FAT32)
- Suporta imagens BMP e áudio WAV
- Tem um shell com 20+ comandos
- Ambiente desktop com janelas, ícones e barra de tarefas
- Editor de texto com syntax highlight
- Media player e gerenciador de tarefas

## Por que existe?

Para aprender como um sistema operacional funciona de verdade, codificando cada parte do zero — sem usar bibliotecas prontas ou frameworks.

---

## Navegação

| Documento | Descrição |
|-----------|-----------|
| [01 - Introdução](01-introducao/introducao.md) | Visão geral e motivação |
| [02 - Arquitetura](02-arquitetura/arquitetura.md) | Estrutura geral do sistema |
| [03 - Bootloader](03-bootloader/bootloader.md) | Como o PC inicia o MiniOS |
| [04 - Kernel](04-kernel/kernel.md) | O coração do sistema |
| [05 - Drivers](05-drivers/drivers.md) | Comunicação com hardware (VGA, VESA, PCI, AC97) |
| [06 - Memória](06-memoria/memoria.md) | Gerenciamento de memória + compressão LZSS |
| [07 - Processos](07-processos/processos.md) | Processos e threads |
| [08 - Sistema de Arquivos](08-sistema-arquivos/sistema-arquivos.md) | FAT12, FAT32, BMP, WAV, FS unificado |
| [09 - Shell](09-shell/shell.md) | Terminal interativo, comandos e aplicativos |
| [10 - Extras](10-extras/extras.md) | PC Speaker, syscalls |
| [11 - Referências](11-referencias/referencias.md) | Links e glossário |
| [12 - Desktop e Interface](12-desktop/desktop.md) | Desktop, Window Manager, Taskbar, Settings, Icons |
| [13 - Aplicativos](13-aplicativos/aplicativos.md) | Editor, Media Player, File Manager, Task Manager |
