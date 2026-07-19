# Roadmap — Gerenciador de Energia

> Visão geral do desenvolvimento dividida em **5 fases**.
> `✅` = Implementado | `🟡` = Parcial | `⬜` = Não implementado

---

## Fase 1 — Fundamentos de Energia
> Objetivo: Mínimo viável para reiniciar, desligar e idle da CPU.

### 1. Modos de energia
- ⬜ Melhor eficiência energética.
- ⬜ Equilibrado.
- ⬜ Melhor desempenho.
- ⬜ Configurações diferentes para bateria e tomada.
- ⬜ Modos do fabricante (silencioso, turbo, conservação).

### 2. Controlo da tela
- ⬜ Desligar após determinado período de inatividade.
- ⬜ Usar tempo diferente na bateria.
- ⬜ Usar outro tempo conectado à tomada.
- ⬜ Nunca desligar automaticamente.
- ⬜ Reduzir o brilho ao usar economia de energia.
- ⬜ Ajustar brilho automaticamente, quando há sensor.
- ⬜ Usar brilho adaptável ao conteúdo.
- ⬜ Reduzir a taxa de atualização para economizar energia.
- ⬜ Usar taxa de atualização dinâmica.

### 3. Suspensão automática
- ⬜ Definir tempo na bateria.
- ⬜ Definir tempo conectado.
- ⬜ Suspender ao fechar a tampa.
- ⬜ Suspender ao pressionar o botão de energia.
- ⬜ Suspender pelo Menu Iniciar.
- ⬜ Desativar a suspensão automática.
- ⬜ Retomar pelo botão de energia, teclado, mouse ou tampa.

### 4. Hibernação
- ⬜ Retomar aplicativos e documentos abertos.
- ⬜ Consumir menos energia que a suspensão.
- ⬜ Manter a sessão mesmo se a bateria acabar.
- ⬜ Hibernar automaticamente após determinado tempo.
- ⬜ Hibernar ao fechar a tampa.
- ⬜ Hibernar ao pressionar o botão de energia.
- ⬜ Adicionar a opção Hibernar ao Menu Iniciar.

### 5. Estados de energia
- ✅ Estado S0 (computador funcionando). *(kernel.c:202 — HLT no main loop)*
- ⬜ S0 Low Power Idle (Modern Standby).
- ⬜ S1/S2/S3 (formas tradicionais de suspensão).
- ⬜ S4 (hibernação).
- ✅ S5 (desligamento normal). *(shell.c:287-292 — CLI+HLT simulado)*
- ⬜ Ver estados disponíveis via powercfg /a.

### 6. Modern Standby
- ⬜ Desligar a tela.
- ⬜ Reduzir atividade de CPU, GPU e armazenamento.
- ⬜ Manter alguns componentes em estados de baixo consumo.
- ⬜ Permitir determinadas atividades em segundo plano.
- ⬜ Manter ou interromper a rede durante o repouso.
- ⬜ Retomar rapidamente.
- ⬜ Permitir diagnóstico por meio do SleepStudy.

---

## Fase 2 — Economia e Bateria
> Objetivo: Monitorar bateria e implementar economia de energia.

### 7. Economia de energia
- ⬜ Ser ativado manualmente.
- ⬜ Entrar automaticamente quando a bateria chega a determinado nível.
- ⬜ Reduzir o brilho.
- ⬜ Limitar atividades em segundo plano.
- ⬜ Reduzir sincronizações.
- ⬜ Diminuir determinadas notificações.
- ⬜ Priorizar maior duração da bateria.
- ⬜ Ser desativado automaticamente ao conectar o carregador.

### 8. Uso da bateria
- ⬜ Percentual atual.
- ⬜ Estado de carregamento.
- ⬜ Uso ao longo das últimas horas ou dias.
- ⬜ Tempo de tela ligada.
- ⬜ Tempo de tela desligada.
- ⬜ Tempo em suspensão.
- ⬜ Consumo por aplicativo.
- ⬜ Aplicativos que mais gastam energia.
- ⬜ Consumo em primeiro e segundo plano.
- ⬜ Histórico recente de utilização.

### 9. Planos de energia clássicos
- ⬜ Equilibrado.
- ⬜ Economia de energia, quando disponível.
- ⬜ Alto desempenho, quando disponível.
- ⬜ Planos criados pelo fabricante.
- ⬜ Planos personalizados pelo usuário.
- ⬜ Criar um plano.
- ⬜ Duplicar um plano.
- ⬜ Alterar configurações.
- ⬜ Escolher o plano ativo.
- ⬜ Restaurar valores padrão.
- ⬜ Excluir planos personalizados.
- ⬜ Exportar ou importar planos pelo powercfg.

### 10. Configurações avançadas do plano
- ⬜ Tempo para desligar o disco.
- ⬜ Tempo para suspender.
- ⬜ Tempo para hibernar.
- ⬜ Suspensão híbrida.
- ⬜ Temporizadores de ativação.
- ⬜ Suspensão seletiva USB.
- ⬜ Gerenciamento de energia do PCI Express.
- ⬜ Economia de energia de adaptadores sem fio.
- ⬜ Estado mínimo do processador.
- ⬜ Estado máximo do processador.
- ⬜ Política de resfriamento.
- ⬜ Brilho da tela.
- ⬜ Configurações multimídia.
- ⬜ Ações para bateria fraca e crítica.
- ⬜ Níveis de bateria baixa, reserva e crítica.

---

## Fase 3 — Processador e Dispositivos
> Objetivo: Controlar CPU, GPU e periféricos para economizar energia.

### 11. Gerenciamento do processador
- ✅ Estado C1 (HLT). *(kernel.c:202 — `asm volatile("hlt")`)*
- ⬜ Reduzir frequência durante períodos ociosos.
- ⬜ Aumentar desempenho quando necessário.
- ⬜ Definir estado mínimo do processador.
- ⬜ Definir estado máximo do processador.
- ⬜ Controlar estacionamento de núcleos.
- ⬜ Gerenciar estados de baixo consumo (C1E, C3, C6).
- ⬜ Ajustar agressividade do modo turbo.
- ⬜ Escolher política de resfriamento ativa ou passiva.

### 12. Gerenciamento de dispositivos
- ✅ Power-down do codec AC97. *(ac97.c:61-66 — ac97_power_down())*
- ⬜ Permitir que o Windows desligue o dispositivo para economizar energia.
- ⬜ Permitir que o dispositivo acorde o computador.
- ⬜ Permitir apenas um pacote mágico para ativação por rede.
- ⬜ Suspender uma porta USB ociosa.
- ⬜ Reduzir consumo do adaptador de rede.
- ⬜ Colocar Bluetooth em estado de baixa energia.
- ⬜ Desligar controladores quando não estão em uso.

### 13. Suspensão seletiva USB
- ⬜ Desligar apenas uma porta ou dispositivo USB ocioso.
- ⬜ Economizar bateria.
- ⬜ Reduzir atividade do controlador USB.
- ⬜ Permitir que o processador entre em estados mais profundos.
- ⬜ Suspender leitores biométricos, webcams e outros dispositivos pouco utilizados.
- ⬜ Retomar automaticamente o dispositivo quando necessário.

### 14. PCI Express e placa de vídeo
- ✅ PCI enumeration básico. *(pci.c:37-47 — leitura/escrita de config space)*
- ⬜ Gerenciamento de energia do PCI Express (estados L0s/L1).
- ⬜ Colocar links em estados de menor consumo quando ociosos.
- ⬜ Economia máxima (pode causar latência).
- ⬜ Gerenciar SSDs NVMe.
- ⬜ Gerenciar placas de rede.
- ⬜ Gerenciar controladores PCI Express.

### 15. Botões de energia e tampa
- ⬜ Ao pressionar o botão de energia.
- ⬜ Ao pressionar o botão de suspensão.
- ⬜ Ao fechar a tampa do notebook.
- ⬜ Configuração diferente para bateria e tomada.
- ⬜ Não fazer nada / Suspender / Hibernar / Desligar.
- ⬜ Exigir senha quando retorna da suspensão.

### 16. Bateria baixa e crítica
- ⬜ Percentual considerado bateria baixa.
- ⬜ Percentual de reserva.
- ⬜ Percentual considerado crítico.
- ⬜ Notificação de bateria baixa.
- ⬜ Ação ao atingir nível baixo.
- ⬜ Ação ao atingir nível crítico.
- ⬜ Comportamento diferente na bateria e na tomada.

---

## Fase 4 — Temporizadores e Solicitações
> Objetivo: Gerenciar wake timers e solicitações que impedem a suspensão.

### 17. Temporizadores de ativação
- ✅ PIT Timer (50 Hz). *(timer.c:10-17 — divisor 23863)*
- ⬜ Programas e serviços podem criar temporizadores para acordar o computador.
- ⬜ Atualizações.
- ⬜ Manutenção automática.
- ⬜ Backups.
- ⬜ Tarefas agendadas.
- ⬜ Gravações.
- ⬜ Verificações do sistema.
- ⬜ Habilitar, desabilitar ou permitir apenas temporizadores importantes.
- ⬜ powercfg /waketimers — mostrar temporizadores registrados.

### 18. Solicitações que impedem a suspensão
- ⬜ Um aplicativo pode solicitar que o Windows não desligue a tela.
- ⬜ Um aplicativo pode solicitar que o Windows não suspenda.
- ⬜ Um aplicativo pode solicitar que o Windows não entre em modo ausente.
- ⬜ Durante reprodução de vídeos.
- ⬜ Durante downloads.
- ⬜ Durante compartilhamento de tela.
- ⬜ Durante cópias de arquivos.
- ⬜ Durante atualizações.
- ⬜ Durante jogos.
- ⬜ Durante gravações.
- ⬜ powercfg /requests — identificar processos/serviços/drivers.
- ⬜ powercfg /requestsoverride — criar substituições.

### 19. Relatório da bateria
- ⬜ Baterias instaladas.
- ⬜ Capacidade de projeto.
- ⬜ Capacidade de carga completa.
- ⬜ Histórico de uso.
- ⬜ Ciclos, quando informados pelo hardware.
- ⬜ Uso recente.
- ⬜ Estimativas de autonomia.
- ⬜ Evolução da capacidade.
- ⬜ powercfg /batteryreport — gerar relatório HTML.

### 20. Relatório de eficiência energética
- ⬜ Dispositivos que não entram em repouso.
- ⬜ Configurações pouco eficientes.
- ⬜ Atividade excessiva.
- ⬜ Temporizadores de alta resolução.
- ⬜ Drivers com comportamento inadequado.
- ⬜ USB sem suspensão seletiva.
- ⬜ Solicitações que impedem economia de energia.
- ⬜ powercfg /energy — analisar e criar relatório.

---

## Fase 5 — powercfg, ACPI e Diagnósticos
> Objetivo: Ferramentas de linha de comando, ACPI completo e diagnósticos.

### 21. Diagnóstico de suspensão
- ⬜ powercfg /a — estados de suspensão disponíveis.
- ⬜ powercfg /lastwake — qual dispositivo acordou o computador.
- ⬜ powercfg /waketimers — temporizadores de ativação.
- ⬜ powercfg /requests — o que está impedindo a suspensão.
- ⬜ powercfg /sleepstudy — consumo durante Modern Standby.
- ⬜ powercfg /systemsleepdiagnostics — histórico de entrada/saída.
- ⬜ powercfg /systempowerreport — transições de energia do sistema.

### 22. Funções administrativas do powercfg
- ⬜ Listar planos.
- ⬜ Consultar todas as configurações.
- ⬜ Alterar valores na bateria e na tomada.
- ⬜ Ativar um plano.
- ⬜ Criar ou duplicar planos.
- ⬜ Excluir planos.
- ⬜ Importar e exportar configurações.
- ⬜ Ativar ou desativar hibernação.
- ⬜ Controlar quais dispositivos podem acordar o computador.
- ⬜ Configurar substituições de solicitações.
- ⬜ Gerar relatórios de energia.

### 23. Limitações do gerenciamento nativo
- ⬜ BIOS ou UEFI.
- ⬜ Firmware do notebook.
- ⬜ Controlador de bateria.
- ⬜ Driver do chipset.
- ⬜ Processador.
- ⬜ Placa de vídeo.
- ⬜ Aplicativo do fabricante.
- ⬜ Carregador e bateria instalados.

### 24. ACPI (fundação para tudo)
- 🟡 Campo ACPI no mapa de memória E820. *(memory.h:20 — lido mas ignorado)*
- ⬜ Parsing de RSDP/RSDT/XSDT.
- ⬜ Parsing de FADT/DSDT/SSDT.
- ⬜ Driver ACPI completo.
- ⬜ Registos PM1a/PM1b.
- ⬜ GPE (General Purpose Events) handlers.
- ⬜ FACS (Firmware ACPI Control Structure).
- ⬜ ACPI thermal zones.

### 25. Comandos de shell existentes
- ✅ Comando `reboot`. *(shell.c:280-285 — outb 0xFE, 0x64)*
- ✅ Comando `shutdown`. *(shell.c:287-292 — CLI+HLT simulado)*
- ✅ Reiniciar via Menu Iniciar. *(taskbar.c:441)*
- ✅ Desligar via Menu Iniciar. *(taskbar.c:442)*
- ✅ Reiniciar via Settings. *(settings.c:103-104)*

---

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1 — Fundamentos | 40 | 2 | 0 | 38 |
| 2 — Economia/Bateria | 47 | 0 | 0 | 47 |
| 3 — Processador/Dispositivos | 30 | 2 | 0 | 28 |
| 4 — Temporizadores/Solicitações | 36 | 1 | 0 | 35 |
| 5 — powercfg/ACPI/Diagnósticos | 30 | 5 | 1 | 24 |
| **Total** | **183** | **10** | **1** | **172** |

---

## Referência — O que existe hoje

| Funcionalidade | Local | Descrição |
|----------------|-------|-----------|
| HLT (idle CPU) | kernel.c:202 | CPU para entre interrupções |
| Reboot | shell.c:280-285, taskbar.c:441, settings.c:103 | `outb(0xFE, 0x64)` |
| Shutdown (simulado) | shell.c:287-292, taskbar.c:442 | CLI+HLT (não desliga realmente) |
| AC97 power-down | ac97.c:61-66 | Desliga codec de áudio |
| PIT Timer 50Hz | timer.c:10-17 | Único temporizador |

---

## Limitações Técnicas Conhecidas

- **Sem ACPI**: Não há parsing de tabelas ACPI, registos PM, nem GPE handlers.
- **Sem bateria**: O sistema foi concebido para QEMU (desktop), sem leitura de bateria.
- **Shutdown simulado**: `CLI+HLT` apenas para a CPU — não desliga a máquina真正的.
- **Sem USB**: Não existe driver USB, logo não há suspensão seletiva.
- **Sem CPU frequency scaling**: Não há leitura de MSR nem alteração de frequência.
- **HLT = C1 apenas**: O único estado de baixo consumo da CPU é o C1 (halt).
- **PCI sem PM**: O driver PCI existe mas não implementa power management.
