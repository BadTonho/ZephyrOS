# Roadmap 16 - Energia e ACPI Avancado

## Objetivo

Consolidar o subsistema de gerenciamento de energia e controle de hardware do ZephyrOS através do padrão ACPI (*Advanced Configuration and Power Interface*), inspirado nas estratégias do Linux para economia de energia em tempo ocioso (CPU Idle com loop seguro de instrução `hlt`), descoberta detalhada de topologia de hardware via tabelas ACPI e desligamento (*poweroff*) e reinicialização (*reboot*) limpos e determinísticos.

## Resumo de progresso

- [ ] PWR1 - Loop de CPU Idle com economia de energia via instrução `hlt`.
- [ ] PWR2 - Parser completo de tabelas ACPI (RSDP, RSDT, XSDT, FADT, MADT).
- [ ] PWR3 - Desligamento e reinicialização determinísticos por hardware.
- [ ] PWR4 - Notificação ordenada de encerramento do sistema para drivers e apps.

## Atalhos

- [Roadmap 05 - Sistema e Ecossistema](05-sistema-e-ecossistema.md)
- [Roadmap 12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md)
- [Roadmap 13 - Armazenamento e Buffer Cache](13-armazenamento-e-buffer-cache.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Descoberta básica de RSDP em memória BIOS.
- Leitura de FADT e tentativa de desligamento via registradores PM1a/PM1b.
- Fallback de suspensão com mensagem e parada em tela.
- Resiliência em ambientes reais e virtualizados (QEMU, Bochs, VirtualBox).

## Princípios de engenharia

- **Economia Total de Ciclos Ociosos:** Quando nenhuma thread estiver pronta para executar (`READY`), a CPU deve entrar em estado de baixo consumo com `hlt` aguardando a próxima interrupção.
- **Múltiplos Vetores de Encerramento:** Se o método ACPI S5 falhar, o sistema deve acionar vetores de fallback ordenados (portas de emulador, APM, fallback de controle).
- **Reinicialização Segura (*Reboot*):** O reboot deve tentar o registrador ACPI Reset, o controlador de teclado 8042 e, em último caso, um Triple Fault controlado.
- **Flush Antes do Desligamento:** Nenhum comando de desligamento desativa a máquina antes de descarregar os caches de disco (`sync`) e desmontar volumes.

## Ordem de dependência

1. PWR1 - Tarefa Idle com `hlt` e métricas de tempo de CPU ociosa.
2. PWR2 - Parser de tabelas ACPI FADT, MADT e detecção de APIC.
3. PWR3 - Rotinas de desligamento físico e reinicialização segura.
4. PWR4 - Protocolo de notificação e desmontagem prévia.

---

## PWR1 - Loop de CPU Idle com HLT

### Implementação

- [ ] Criar a thread especial do kernel `idle_task` com a menor prioridade possível do scheduler.
- [ ] No corpo da `idle_task`, executar um loop seguro:
  ```c
  while (1) {
      asm volatile ("sti; hlt");
  }
  ```
- [ ] Contabilizar o tempo gasto na `idle_task` para calcular a porcentagem real de uso de CPU do sistema (`100% - idle%`).
- [ ] Garantir que interrupções de hardware (timer, teclado, rede) acordem a CPU imediatamente sem latência adicional.

### Critério de saída

O uso de CPU no computador host (ou processo do QEMU) cai para próximo de 0-1% quando o ZephyrOS estiver aguardando interação do usuário no Shell ou Desktop.

### Comandos Shell / Diagnóstico

- `cpu usage`: exibe porcentagem de CPU ativa vs tempo ocioso (*idle time*).

---

## PWR2 - Parser de Tabelas ACPI (FADT, MADT, DSDT)

### Implementação

- [ ] Localizar e validar a assinatura da RSDP (Root System Description Pointer) nos primeiros 1MB de memória física ou na EBDA.
- [ ] Mapear e validar o cabeçalho da RSDT / XSDT verificando checksums de 8 bits.
- [ ] Mapear a tabela FADT (Fixed ACPI Description Table) para obter as portas `PM1a_CNT_BLK`, `PM1b_CNT_BLK`, `SMI_CMD` e o valor `SLP_TYPa` para o estado S5 (Soft Off).
- [ ] Mapear a tabela MADT (Multiple APIC Description Table) para identificar quantidade de cores da CPU e controladores I/O APIC.

### Critério de saída

O sistema lista com sucesso todas as tabelas ACPI disponíveis com seus respectivos endereços físicos e status de checksum.

### Comandos Shell / Diagnóstico

- `acpi tables`: lista todas as tabelas ACPI encontradas na máquina e seus cabeçalhos.

---

## PWR3 - Desligamento e Reinicialização Determinísticos

### Implementação

- [ ] Implementar a função `int acpi_poweroff(void)` que grava a sequência de sleep correspondente ao estado S5 no registrador PM1 Control.
- [ ] Implementar os fallbacks de desligamento para virtualizadores:
  - Porta QEMU / Bochs: `outw(0x604, 0x2000)` ou `outw(0xB004, 0x2000)`.
  - Porta VirtualBox: `outw(0x4004, 0x3400)`.
- [ ] Implementar a função `int system_reboot(void)`:
  - 1ª tentativa: Registrador `RESET_REG` informado na tabela ACPI FADT.
  - 2ª tentativa: Pulso de reset no pino do controlador de teclado PS/2 (porta 0x64, comando 0xFE).
  - 3ª tentativa: Carga de IDT nula (`lidt`) e disparo de interrupção forçada (Triple Fault).

### Critério de saída

Comandos `poweroff` e `reboot` desligam ou reiniciam o computador/emulador de forma imediata e limpa.

### Comandos Shell / Diagnóstico

- `poweroff`: executa flush de disco e desliga a máquina.
- `reboot`: executa flush de disco e reinicia o computador.

---

## PWR4 - Notificação de Encerramento do Sistema

### Implementação

- [ ] Criar a cadeia de notificação de desligamento do kernel (*reboot notifier chain*).
- [ ] Ao receber solicitação de desligamento:
  1. Enviar sinal `SIGTERM` / notificação para aplicativos em execução;
  2. Executar `sync` de todos os buffers e mídias de armazenamento;
  3. Desmontar todos os volumes montados no VFS;
  4. Desativar periféricos de áudio, rede e vídeo;
  5. Acionar o vetor de desligamento por hardware.

### Critério de saída

O sistema encerra ordenadamente todos os serviços sem perda de dados ou estado inconsistente em disco.

### Comandos Shell / Diagnóstico

- `shutdown -h now`: encerramento com notificação completa.
- `shutdown -r now`: reinicialização com notificação completa.
