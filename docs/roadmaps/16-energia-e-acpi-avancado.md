# Roadmap 16 - Energia e ACPI Avancado

## Objetivo

Consolidar o subsistema de gerenciamento de energia e controle de hardware do
ZephyrOS através de capacidades ACPI observadas e contratos seguros de idle,
desligamento e reinicialização. A etapa separa descoberta/validação de tabelas,
execução de métodos AML, política de idle e transição final de energia; nenhum
fallback específico de emulador será tratado como comportamento universal.

## Resumo de progresso

- [ ] PWR0 - Contrato de capacidades, estados e ordem de desligamento.
- [ ] PWR1 - Idle arquitetural seguro com economia de energia via `hlt`.
- [ ] PWR2 - Descoberta e validação de tabelas ACPI (RSDP, RSDT, XSDT, FADT, MADT).
- [ ] PWR3 - Desligamento e reinicialização determinísticos por hardware.
- [ ] PWR4 - Notificação ordenada de encerramento do sistema para drivers e apps.

## Atalhos

- [Roadmap 05 - Sistema e Ecossistema](05-sistema-e-ecossistema.md)
- [Roadmap 12 - Concorrencia e Sincronizacao](12-concorrencia-e-sincronizacao.md)
- [Roadmap 13 - Armazenamento, Block Layer e Cache de Blocos](13-armazenamento-e-buffer-cache.md)
- [Índice dos Roadmaps](README.md)
- [Índice da Documentação](../indice.md)

## Base já validada

- Descoberta básica de RSDP em memória BIOS.
- Leitura de FADT e tentativa de desligamento via registradores PM1a/PM1b.
- Fallback de suspensão com mensagem e parada em tela.
- Resiliência em ambientes reais e virtualizados (QEMU, Bochs, VirtualBox).

## Princípios de engenharia

- **Idle seguro:** Quando nenhuma thread estiver pronta, a arquitetura entra em
  idle com `hlt` apenas após uma verificação protegida contra a janela entre a
  decisão e a interrupção. O caminho deve contabilizar idle e acordar por IRQ.
- **Capacidades observadas:** ACPI, APM, controlador 8042 e outros fallbacks
  só podem ser usados quando sua capacidade tiver sido detectada e validada.
- **Reinicialização segura:** O reboot tenta o reset ACPI anunciado, depois o
  controlador 8042 e, em último caso, um Triple Fault controlado; portas
  privadas de QEMU/Bochs/VirtualBox não são uma API genérica.
- **Flush antes do desligamento:** Nenhum comando desativa a máquina antes de
  concluir o contrato de sync/flush do Roadmap 13 e publicar falhas de escrita.

## Ordem de dependência

1. PWR0 - Contratos de capacidades, estados e ordem de transição.
2. PWR1 - Tarefa Idle com `hlt` e métricas de tempo de CPU ociosa.
3. PWR2 - Descoberta de tabelas ACPI FADT, MADT e detecção de APIC.
4. PWR3 - Rotinas de desligamento físico e reinicialização segura.
5. PWR4 - Protocolo de notificação e desmontagem prévia.

---

## PWR0 - Contrato de energia e transições

### Implementação

- [ ] Definir estados `UNKNOWN`, `DISCOVERING`, `READY`, `DEGRADED` e
  `UNAVAILABLE`, com capacidades publicadas sem executar escrita especulativa
  em hardware.
- [ ] Definir ordem, ownership, timeout, cancelamento e resultado das
  notificações de drivers, filesystem, workqueues, processos e CPU.
- [ ] Definir que desligamento e reboot são operações irreversíveis, com
  confirmação de pré-condições e registro de cada fallback tentado.
- [ ] Separar descoberta de tabelas ACPI, interpretação AML e uso de métodos de
  energia; um parser de cabeçalhos não será chamado de ACPI completo.

### Critério de saída

Cada capacidade de energia possui estado observável, pré-condição, fallback e
erro público, sem desligar ou reiniciar quando o contrato não estiver pronto.

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
- [ ] Garantir que interrupções de hardware (timer, teclado, rede) acordem a
  CPU sem perder eventos na janela entre a verificação e o `hlt`.
- [ ] Se o sistema continuar unicore, manter uma única tarefa idle; não criar
  uma abstração SMP sem necessidade real.

### Critério de saída

O uso de CPU no computador host (ou processo do QEMU) cai para próximo de 0-1% quando o ZephyrOS estiver aguardando interação do usuário no Shell ou Desktop.

### Comandos Shell / Diagnóstico

- `cpu usage`: exibe porcentagem de CPU ativa vs tempo ocioso (*idle time*).

---

## PWR2 - Descoberta e Validação de Tabelas ACPI

### Implementação

- [ ] Localizar e validar a assinatura da RSDP (Root System Description Pointer) nos primeiros 1MB de memória física ou na EBDA.
- [ ] Mapear e validar o cabeçalho da RSDT / XSDT verificando checksums de 8 bits.
- [ ] Mapear a tabela FADT (Fixed ACPI Description Table) para obter as portas `PM1a_CNT_BLK`, `PM1b_CNT_BLK`, `SMI_CMD` e o valor `SLP_TYPa` para o estado S5 (Soft Off).
- [ ] Mapear a tabela MADT (Multiple APIC Description Table) para identificar
  quantidade de cores da CPU e controladores I/O APIC.
- [ ] Manter a interpretação de AML/DSDT fora do critério desta etapa, salvo se
  uma capacidade de energia depender explicitamente dela; nesse caso, criar
  uma etapa própria para o interpretador e seu isolamento.

### Critério de saída

O sistema lista as tabelas ACPI encontradas com endereços, comprimentos,
revisões e checksums válidos, distinguindo descoberta de execução de métodos.

### Comandos Shell / Diagnóstico

- `acpi tables`: lista todas as tabelas ACPI encontradas na máquina e seus cabeçalhos.

---

## PWR3 - Desligamento e Reinicialização Determinísticos

### Implementação

- [ ] Implementar a função `int acpi_poweroff(void)` que grava a sequência de sleep correspondente ao estado S5 no registrador PM1 Control.
- [ ] Implementar fallbacks somente quando a capacidade for detectada e
  documentada; não usar portas privadas de QEMU, Bochs ou VirtualBox como
  caminho genérico do kernel.
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
  2. Executar o sync/flush do Roadmap 13 e aguardar a confirmação de
     durabilidade;
  3. Desmontar todos os volumes montados no VFS;
  4. Desativar periféricos de áudio, rede e vídeo;
  5. Acionar o vetor de desligamento por hardware somente se as etapas
     anteriores concluírem sem erro não recuperado.

### Critério de saída

O sistema encerra ordenadamente os serviços e detecta falhas de sync/flush
antes da transição irreversível, sem prometer atomicidade que o filesystem não
implemente.

### Comandos Shell / Diagnóstico

- `shutdown -h now`: encerramento com notificação completa.
- `shutdown -r now`: reinicialização com notificação completa.
