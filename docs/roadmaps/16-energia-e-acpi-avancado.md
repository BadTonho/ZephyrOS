# Roadmap 16 - Energia e ACPI Avancado

## Objetivo

Consolidar o subsistema de gerenciamento de energia e controle de hardware do
ZephyrOS através de capacidades ACPI observadas e contratos seguros de idle,
desligamento e reinicialização. A etapa separa descoberta/validação de tabelas,
execução de métodos AML, política de idle e transição final de energia; nenhum
fallback específico de emulador será tratado como comportamento universal.

## Resumo de progresso

- [x] PWR0 - Contrato de capacidades, estados e ordem de desligamento.
- [ ] PWR1 - Idle arquitetural seguro com economia de energia via `hlt`.
- [x] PWR2 - Descoberta e validação de tabelas ACPI (RSDP, RSDT, XSDT, FADT, MADT).
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

- [x] Definir os estados do serviço `UNKNOWN`, `DISCOVERING`, `READY`,
  `DEGRADED` e `UNAVAILABLE`, separados dos estados ACPI S0-S5. O coordenador
  publica capacidades observadas sem executar escrita especulativa em
  hardware; a indisponibilidade de uma capacidade não invalida o diagnóstico
  das demais.
- [x] Definir uma transação única para `shutdown` e `reboot`, com a ordem
  `admission -> notification -> sync/flush -> quiescence -> hardware commit ->
  terminal`. O alvo da operação é fixado na admissão e somente o método
  terminal muda entre desligamento e reinicialização.
- [x] Fixar orçamentos em ticks PIT de 50 Hz, sem empréstimo entre fases:
  notificação em 250 ticks (5 s), sync/flush em 1500 ticks (30 s),
  quiescência em 250 ticks (5 s), commit de hardware em 100 ticks (2 s) e
  orçamento total de 2100 ticks (42 s).
- [x] Definir ownership, cancelamento e resultado. O coordenador possui a
  transação, os prazos e o estado; cada participante possui seus recursos e
  deve preparar, confirmar e liberar de forma idempotente. O cancelamento só
  é aceito antes do commit; depois da primeira escrita ou comando de hardware
  a operação é irreversível.
- [x] Definir que desligamento e reboot exigem pré-condições verificadas e
  registram cada falha ou fallback. Antes do commit, uma falha retorna ao
  estado operacional anterior; depois do commit, somente um caminho terminal
  seguro pode ser usado.
- [x] Separar descoberta e validação de tabelas ACPI, interpretação AML e uso
  de métodos de energia. O parser de cabeçalhos não será chamado de ACPI
  completo e nenhum método não validado será executado.
- [x] Fixar os erros canônicos `ERR_STATE`, `ERR_UNAVAILABLE`, `ERR_TIMEOUT`,
  `ERR_CANCELLED`, `ERR_AGAIN` e `ERR_INVALID`, além de logging limitado por
  fase, falha e fallback.

### Critério de saída

Cada capacidade de energia possui estado observável, pré-condição, fallback e
erro público, sem desligar ou reiniciar quando o contrato não estiver pronto.
O PWR0 é documental: a aplicação dos estados, orçamentos, notificações,
quiescência e transições físicas pertence às etapas PWR1-PWR4. Os comandos
`power status` e `acpi status` continuam sendo os mecanismos de observabilidade
sem criar comando, syscall, App API ou ABI binária nova.

---

## PWR1 - Loop de CPU Idle com HLT

### Implementação

- [x] Consolidar o PID 0 como o único contexto Idle do kernel unicore, fora do
  round-robin e escolhido somente quando não houver processo normal `READY`.
- [x] Iniciar o scheduler por um contexto de bootstrap separado, carregando a
  stack e o contexto próprios do PID 0 sem sobrescrevê-los com a stack do
  `kernel_main`.
- [x] No corpo do Idle, executar um loop seguro:
  ```c
  while (1) {
      asm volatile ("sti; hlt");
      process_yield();
  }
  ```
- [x] Contabilizar ticks `idle_ticks` e `active_ticks` no scheduler, mantendo
  `idle_ticks` igual ao `total_ticks` do PID 0 e protegendo snapshots contra
  atualização concorrente do PIT.
- [x] Usar bloqueio temporizado nos loops cooperativos de System e Desktop;
  timer, teclado, rede, IPC e workqueue continuam acordando seus consumidores.
- [x] Manter uma única tarefa Idle no sistema unicore e preservar o loop
  degradado do `kernel_main` com `sti; hlt` quando System não puder ser criado.
- [x] Expor `cpu usage` e `cpu usage reset`, além dos deltas no `kmetrics`,
  sem alterar contadores do kernel no reset.

### Critério de saída

A funcionalidade do scheduler PWR1 foi confirmada pelo usuário: a janela após
`cpu usage reset` reportou 4 ticks ativos, 145 ociosos e `schedcheck` confirmou
`contabilidade_idle=OK`. A queda de uso do host/QEMU para próximo de 0-1%
quando o ZephyrOS aguarda interação ainda deve ser medida pelo usuário no
mesmo cenário antes de marcar o resumo PWR1 como concluído.

### Comandos Shell / Diagnóstico

- `cpu usage`: exibe ticks e porcentagens de CPU ativa versus tempo ocioso.
- `cpu usage reset`: captura uma linha-base privada para a próxima consulta.

---

## PWR2 - Descoberta e Validação de Tabelas ACPI

### Implementação

- [x] Localizar e validar a assinatura da RSDP (Root System Description Pointer)
  na EBDA ou nos primeiros 1MB de memória física, validando comprimento e
  checksums v1/v2.
- [x] Mapear e validar a RSDT/XSDT verificando limites, endereços físicos,
  comprimento e checksum de 8 bits; preferir XSDT e usar RSDT como fallback.
- [x] Registrar a raiz selecionada e as SDTs válidas em snapshot estático,
  preservando a ordem da raiz, eliminando duplicatas e limitando o inventário a
  `ACPI_MAX_TABLES`.
- [x] Manter o snapshot da FADT com PM1a, PM1b, `SMI_CMD`, campos de energia e
  `_S5_`, sem executar métodos ou escrever em hardware durante a descoberta.
- [x] Mapear a MADT e copiar suas entradas sem ponteiros persistentes, com
  limite de 64 entradas, contagem de CPUs habilitadas, APICs locais e I/O APICs.
  Entradas desconhecidas válidas são preservadas como bytes copiados.
- [x] Manter a interpretação AML/DSDT fora do escopo novo desta etapa. O
  reconhecedor `_S5_` já existente permanece limitado e isolado; nenhum método
  AML adicional é interpretado ou executado.

### Critério de saída

O sistema lista as tabelas ACPI encontradas com endereços, comprimentos,
revisões e checksums válidos, distinguindo descoberta de execução de métodos.
`acpi tables` publica RSDP, a raiz, todas as SDTs copiadas e o resumo MADT. As
consultas `acpi_get_madt_info()`, `acpi_get_madt_entry_count()` e
`acpi_get_madt_entry_at()` retornam somente cópias sem referências físicas.
O usuário confirmou a validação funcional no QEMU: RSDP, RSDT, FACP, APIC,
HPET, WAET e DSDT foram listadas com `checksum=OK`, sem tabelas inválidas ou
ignoradas; o MADT reportou 1 processador, 1 Local APIC e 1 I/O APIC. `RegCheck`
terminou em `OK`.

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
