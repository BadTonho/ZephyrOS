# ZephyrOS

Sistema operacional desenvolvido do zero em C + Assembly (x86).

---

## O que é o ZephyrOS?

O ZephyrOS é um sistema operacional funcional, construído desde o bootloader até um ambiente desktop completo. O projeto tem o objetivo de ser um OS real e utilizável, não apenas educacional.

## O que ele faz?

- Inicia sozinho (bootloader)
- Mostra mensagens na tela (VGA text mode + VESA gráfico)
- Responde a teclado (input do usuário)
- Gerencia memória (alocação dinâmica, compressão LZSS)
- Roda processos e threads concorrentemente
- Lê e escreve arquivos em disco (FAT12, FAT32 com subdiretórios)
- Suporta imagens BMP (1/4/8/24 bpp) e áudio WAV
- Tem um Shell com scrollback, comandos de diagnóstico e suporte inicial a apps ring 3
- Ambiente desktop com janelas, ícones e barra de tarefas
- Editor de texto com syntax highlight
- Driver de mouse PS/2 com cursor gráfico
- Sistema de IPC (comunicação entre processos)
- Primitivas GUI 2D (gui.c)
- Media player e gerenciador de tarefas
- App API 0.3, syscalls, loader ZAPP, IPC e argumentos simples

## Por que existe?

Para construir um sistema operacional funcional do zero — codificando cada componente sem usar bibliotecas prontas ou frameworks.

---

## Navegação

| Documento | Descrição |
|-----------|-----------|
| [01 - Introdução](01-introducao/introducao.md) | Visão geral e motivação |
| [02 - Arquitetura](02-arquitetura/arquitetura.md) | Estrutura geral do sistema |
| [03 - Bootloader](03-bootloader/bootloader.md) | Como o PC inicia o ZephyrOS |
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
| [Pacotes locais](13-aplicativos/pacotes.md) | Formato ZPKG v1, empacotador host e comandos `pkg` |
| [Configuracoes](melhorias%20futuras/configura%C3%A7%C3%B5es.md) | Painel de configuracoes e interface grafica com fallback TUI |
| [Resiliência e fallback seguro](melhorias%20futuras/resiliencia%20do%20sistema.md) | Estados de componentes, códigos de erro e fallbacks |
| [Explorer moderno](melhorias%20futuras/explorer%20moderno.md) | Interface gráfica do File Manager com fallback TUI |
| [Task Manager moderno](melhorias%20futuras/task%20manager%20moderno.md) | Janela grafica com TUI de diagnostico preservada |
| [Task Manager - metricas avancadas](melhorias%20futuras/task%20manager%20metricas%20avancadas.md) | Proxima etapa para metricas do kernel e historico |
| [Responsividade do sistema](melhorias%20futuras/responsividade%20do%20sistema.md) | Frames parciais, atualizacao responsiva e otimizacao grafica |
| [Fundacao do Kernel](melhorias%20futuras/fundacao%20do%20kernel.md) | APIs, modulos, memoria, processos e diagnostico |
| [Atualizacao e Otimizacao do Kernel](melhorias%20futuras/atualizacao%20do%20kernel.md) | Evolucao segura do kernel baseada em metricas |
| [API de Aplicativos e Syscalls](melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md) | Contrato 0.3, syscalls, argumentos, loader ZAPP e foco seguro ring 3 |
| [Gerenciador de Aplicativos](melhorias%20futuras/gerenciador%20de%20aplicativos.md) | Loader ZAPP, aplicativos nativos e roadmap da App Store |
| [Gerenciador de Dispositivos](melhorias%20futuras/gerenciador%20de%20dispositivos.md) | Inventario nativo seguro e evolucao do gerenciamento de hardware |
| [Gerenciador de Energia](melhorias%20futuras/gerenciador%20de%20energia.md) | Diagnostico de energia, ACPI, PM1 e desligamento fisico S5 com fallback |
| [Gerenciador de Rede](melhorias%20futuras/gerenciador%20de%20rede.md) | Inventario PCI, Ethernet, ARP, IPv4, UDP, DHCP, DNS, TCP e HTTP |
| [Atualizacoes do Sistema](melhorias%20futuras/atualiza%C3%A7%C3%B5es.md) | Roteiro U1-U5 para integridade, rollback e distribuicao opcional |
| [Contrato ZUPD v1](14-atualizacoes/contrato-zupd-v1.md) | Formato autenticado, transacao FAT12 e historico redundante U4 |
| [System Updater](14-atualizacoes/system-updater.md) | Aplicativo nativo Classic/Modern para pacotes, estado e historico |
| [Ferramenta Host ZUPD v1](14-atualizacoes/ferramenta-zupd.md) | Chave, fixtures e auditoria offline dos controles U3/U4 |
| [Avisos de terceiros](../THIRD_PARTY_NOTICES.md) | Proveniencia e licencas de codigo adaptado |
| [GUI Moderna](melhorias%20futuras/gui_moderna.md) | Transição para primitivas gráficas 2D e VESA |
| [Formatação Inteligente](melhorias%20futuras/formatacao%20inteligente.md) | Sistema de reset e reinstalação preservando arquivos |
| [Verificação e Auto-reparo do Sistema](melhorias%20futuras/verifica%C3%A7%C3%A3o%20de%20sistema.md) | Diagnóstico de integridade e autocorreção de arquivos (SFC) |
| [Atalhos e Comandos do Sistema](atalhos_e_comandos.md) | Lista completa de atalhos de teclado e comandos do shell |
| [Estado da Documentação](estado_da_documentacao.md) | Fonte de verdade, escopo da auditoria e como interpretar os roadmaps |
| [Contratos publicos](qualidade/contratos-publicos.md) | Mapa de headers publicos e documentos tecnicos canonicos |
| [Metricas de otimizacao](qualidade/metricas.md) | Linhas-base K1, validacoes K2/K3 e ganho K4 registrado |
| [Roadmaps por Etapa](roadmaps/README.md) | Ordem executável das próximas frentes do projeto |
| [01 - Estabilização e Qualidade](roadmaps/01-estabilizacao-e-qualidade.md) | Regressão, diagnósticos e fallbacks |
| [02 - Plataforma de Aplicativos](roadmaps/02-plataforma-de-aplicativos.md) | Migração gradual, ZAPP e pacotes |
| [03 - Kernel e Desempenho](roadmaps/03-kernel-e-desempenho.md) | Métricas, scheduler, memória e otimização segura |
| [04 - Interface e Experiência](roadmaps/04-interface-e-experiencia.md) | GUI moderna, taskbar, WM e interação |
| [05 - Sistema e Ecossistema](roadmaps/05-sistema-e-ecossistema.md) | Dispositivos, energia, rede, atualizações e apps |
