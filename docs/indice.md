# ZephyrOS

Sistema operacional desenvolvido do zero em C + Assembly (x86).

---

## O que é o ZephyrOS?

O ZephyrOS é um sistema operacional funcional, construído desde o bootloader até um ambiente desktop completo. O projeto tem o objetivo de ser real, confiável e utilizável por pessoas.

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
- App API 0.5, descritores VFS, syscalls, sinais, loader ZAPP, IPC e argumentos simples

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
| [08 - Sistema de Arquivos](08-sistema-arquivos/sistema-arquivos.md) | FAT12 legado, volume híbrido FAT32, Storage, LFN e índice global |
| [09 - Shell](09-shell/shell.md) | Terminal interativo, comandos e aplicativos |
| [Refatoração do Shell](09-shell/refatoracao-shell.md) | Diagnóstico e plano incremental para separar entrada, dispatcher e comandos |
| [10 - Extras](10-extras/extras.md) | PC Speaker, syscalls |
| [11 - Referências](11-referencias/referencias.md) | Links e glossário |
| [12 - Desktop e Interface](12-desktop/desktop.md) | Desktop, Window Manager, Taskbar, Settings, Icons |
| [13 - Aplicativos](13-aplicativos/aplicativos.md) | Editor, Media Player, File Manager, Task Manager |
| [Pacotes locais](13-aplicativos/pacotes.md) | Formato ZPKG v1, transacoes e fonte de plano em diretorio AS5 |
| [App Store - AS1 a AS5](13-aplicativos/app-store.md) | Catalogos local/remoto, Ed25519, cache A/B, transacoes e interface nativa |
| [Configuracoes](melhorias%20futuras/configura%C3%A7%C3%B5es.md) | Painel de configuracoes e interface grafica com fallback TUI |
| [Resiliência e fallback seguro](melhorias%20futuras/resiliencia%20do%20sistema.md) | Estados de componentes, códigos de erro e fallbacks |
| [Explorer Classic](melhorias%20futuras/explorer%20moderno.md) | Interface gráfica do File Manager com fallback TUI |
| [Task Manager Classic](melhorias%20futuras/task%20manager%20moderno.md) | Janela grafica com TUI de diagnostico preservada |
| [Task Manager - metricas avancadas](melhorias%20futuras/task%20manager%20metricas%20avancadas.md) | Proxima etapa para metricas do kernel e historico |
| [Responsividade do sistema](melhorias%20futuras/responsividade%20do%20sistema.md) | Frames parciais, atualizacao responsiva e otimizacao grafica |
| [Fundacao do Kernel](melhorias%20futuras/fundacao%20do%20kernel.md) | APIs, modulos, memoria, processos e diagnostico |
| [Atualizacao e Otimizacao do Kernel](melhorias%20futuras/atualizacao%20do%20kernel.md) | Evolucao segura do kernel baseada em metricas |
| [API de Aplicativos e Syscalls](melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md) | Contrato 0.5, descritores VFS, syscalls, sinais, argumentos, loader ZAPP e foco seguro ring 3 |
| [Gerenciador de Aplicativos](melhorias%20futuras/gerenciador%20de%20aplicativos.md) | Inventario detalhado e referencia ao roadmap AS1-AS5 |
| [Gerenciador de Dispositivos](melhorias%20futuras/gerenciador%20de%20dispositivos.md) | Inventario nativo seguro e evolucao do gerenciamento de hardware |
| [Gerenciador de Energia](melhorias%20futuras/gerenciador%20de%20energia.md) | Diagnostico de energia, ACPI, PM1 e desligamento fisico S5 com fallback |
| [Gerenciador de Rede](melhorias%20futuras/gerenciador%20de%20rede.md) | Inventario PCI, Ethernet, ARP, IPv4, UDP, DHCP, DNS, TCP e HTTP |
| [Atualizacoes do Sistema](melhorias%20futuras/atualiza%C3%A7%C3%B5es.md) | Base U1-U5 para integridade, rollback e distribuicao; continuidade EP5-EP9 no Roadmap 08 |
| [Contrato ZUPD v1](14-atualizacoes/contrato-zupd-v1.md) | Formato autenticado, transacao FAT12 e historico redundante U4 |
| [Contrato ZSYS v1](14-atualizacoes/contrato-zsys-v1.md) | Envelope da imagem completa, compatibilidade assinada e preflight EP9.0A |
| [Contrato ZUM2/ZUPD v2](14-atualizacoes/contrato-zupd-v2.md) | Runtime v2, catálogo, cache seletivo/completo, staging, rollback e falhas EP6.3 |
| [Distribuicao remota ZUPD v1](14-atualizacoes/distribuicao-remota.md) | Manifesto ZUM1, HTTP manual, cache U5 e selecao EP6.0 por tag exata |
| [System Updater](14-atualizacoes/system-updater.md) | Aplicativo nativo Simple/Classic para pacotes, estado, historico e remoto |
| [Ferramenta Host ZUPD v1](14-atualizacoes/ferramenta-zupd.md) | Chave, fixtures EP5/EP6, ZSYS e formatador híbrido FAT32 |
| [Avisos de terceiros](../THIRD_PARTY_NOTICES.md) | Proveniencia e licencas de codigo adaptado |
| [GUI Classic](melhorias%20futuras/gui_moderna.md) | Histórico da transição para primitivas gráficas 2D e VESA |
| [Formatação Inteligente](melhorias%20futuras/formatacao%20inteligente.md) | Sistema de reset e reinstalação preservando arquivos |
| [Verificação e Auto-reparo do Sistema](melhorias%20futuras/verifica%C3%A7%C3%A3o%20de%20sistema.md) | Diagnóstico de integridade e autocorreção de arquivos (SFC) |
| [Atalhos e Comandos do Sistema](atalhos_e_comandos.md) | Lista completa de atalhos de teclado e comandos do shell |
| [Estado da Documentação](estado_da_documentacao.md) | Fonte de verdade, escopo da auditoria e como interpretar os roadmaps |
| [Política de documentação do código](qualidade/politica-documentacao-codigo.md) | Onde registrar decisões técnicas sem comentários explicativos no código-fonte |
| [Contratos publicos](qualidade/contratos-publicos.md) | Mapa de headers publicos e documentos tecnicos canonicos |
| [Metricas de otimizacao](qualidade/metricas.md) | Linhas-base K1, validacoes K2/K3 e ganho K4 registrado |
| [Dividas tecnicas da v1.0.0](qualidade/dividas-tecnicas-v1.0.0.md) | Registro canonico das limitacoes aceitas que devem ser quitadas antes da v1.0.0 |
| [Registro de validacoes](qualidade/registro-validacoes.md) | Evidencias cronologicas de implementacoes, testes e conclusoes de fase |
| [Validação EP6.3 Runtime](qualidade/validacao-ep63-runtime.md) | Procedimento host e QEMU para fixtures, Releases A/B, rollback e auditoria do runtime v2 |
| [Validação do stage2 LBA](qualidade/validacao-stage2-lba.md) | Gates, tamanhos e cenários QEMU para EDD/LBA e fallback CHS |
| [Roadmaps por Etapa](roadmaps/README.md) | Ordem executável das próximas frentes do projeto |
| [01 - Estabilização e Qualidade](roadmaps/01-estabilizacao-e-qualidade.md) | Regressão, diagnósticos e fallbacks |
| [02 - Plataforma de Aplicativos](roadmaps/02-plataforma-de-aplicativos.md) | Migração gradual, ZAPP e pacotes |
| [03 - Kernel e Desempenho](roadmaps/03-kernel-e-desempenho.md) | Métricas, scheduler, memória e otimização segura |
| [04 - Interface e Experiência](roadmaps/04-interface-e-experiencia.md) | GUI Classic, taskbar, WM e interação |
| [05 - Sistema e Ecossistema](roadmaps/05-sistema-e-ecossistema.md) | Dispositivos, energia, rede, atualizações e apps |
| [06 - App Store](roadmaps/06-app-store.md) | Catalogo local ZPKG, ciclo de vida e repositorio remoto autenticado |
| [07 - Modernização Visual](roadmaps/07-modernizacao-visual.md) | Escala acessível, visual flat/dark e desempenho VESA mensurável |
| [08 - Evolução da Plataforma](roadmaps/08-evolucao-da-plataforma.md) | EP1-EP6.4 implementadas; EP7.0 encerrada; EP7.1B implementada; EP9.0A, EP9.1 e EP9.4A implementadas e validadas |
| [09 - Funcionalidades aplicáveis](roadmaps/09-funcionalidades-aplicaveis.md) | Logs, timers, espera, work queue, dispositivos, I/O, cache e scheduler |
| [10 - VFS e Abstração de I/O](roadmaps/10-vfs-e-abstracao-io.md) | VFS, descritores de arquivos, montagens, /dev/ e pipes |
| [11 - Gerenciamento Avançado de Memória](roadmaps/11-gerenciamento-avancado-de-memoria.md) | Alocador SLAB/SLUB kmem_cache, áreas virtuais VMA e demand paging |
| [12 - Concorrência e Sincronização](roadmaps/12-concorrencia-e-sincronizacao.md) | Top-Half/Bottom-Half, wait queues, workqueues e sinais assíncronos |
| [13 - Armazenamento e Buffer Cache](roadmaps/13-armazenamento-e-buffer-cache.md) | Block layer, fila de requisições, buffer cache LRU e sincronização sync |
| [14 - Stack de Rede Avançada](roadmaps/14-stack-de-rede-avancada.md) | Socket buffers sk_buff zero-copy, sockets AF_UNIX/AF_INET e select/poll |
| [15 - Introspecção e Pseudo-Filesystems](roadmaps/15-introspeccao-e-pseudo-fs.md) | Pseudo-filesystems /proc e /sys para diagnósticos, processos e hardware |
| [16 - Energia e ACPI Avançado](roadmaps/16-energia-e-acpi-avancado.md) | Loop de CPU idle com HLT, parser ACPI (FADT/MADT), poweroff e reboot |
