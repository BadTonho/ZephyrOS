# Roadmap 21 — Compatibilidade e matriz de hardware

## Estado

Planejado. Esta frente define o conjunto de hardware suportado pela versão
1.0.0 e garante que hardware ausente, parcial ou incompatível produza
capacidade degradada e diagnóstico útil, sem travar o boot.

## Objetivo

Transformar a detecção atual de PCI, ACPI, Storage, USB, rede, áudio, vídeo e
entrada em uma matriz reproduzível de capacidades, dependências, fallbacks,
timeouts e critérios de suporte.

## Escopo

- ordem de inicialização e encerramento dos drivers;
- ownership de portas, MMIO, IRQ, DMA, buffers, filas e callbacks;
- perfis QEMU e hardware real disponível para validação;
- ausência e falha de ACPI, PCI, ATA, USB, NIC, áudio, VESA, VGA, teclado e
  mouse;
- diagnósticos `health`, `devices`, `device-scan`, `acpi`, `net`, `usb` e
  `power`;
- timeouts, confirmação de capacidade e fallback seguro.

Esta frente não promete suporte a hardware que não possa ser exercitado. Wi-Fi
completo, Bluetooth, hotplug universal, IPv6, novos chipsets e carregamento
dinâmico de drivers ficam fora da matriz base, salvo uma decisão explícita.

## Dependências

- [Roadmap 18](18-estabilizacao-e-release-v1.0.md) para a matriz de release;
- [Roadmap 19](19-seguranca-e-isolamento-v1.0.md) para ownership e validação de
  entradas;
- [Roadmap 20](20-integridade-e-recuperacao-do-storage.md) para ATA, USB MSC,
  volumes e falhas de I/O;
- Roadmaps 05, 14, 15 e 16 para contratos de dispositivos, rede, pseudo-FS e
  energia.

## Fases

### HW1 — Catálogo de perfis

- [ ] Definir perfis mínimos: QEMU padrão, sem ACPI, sem NIC, sem USB, sem
  VESA, sem áudio e sem Storage adicional.
- [ ] Registrar, por perfil, hardware detectado, driver ativo, capacidade,
  fallback, erro esperado e diagnóstico observável.
- [ ] Separar “não presente”, “não suportado”, “falhou ao inicializar” e
  “desabilitado por política”.
- [ ] Definir quais cenários são obrigatórios para a 1.0.0 e quais dependem de
  hardware real.
- [ ] Manter IDs, BDFs, endereços e versões estáveis nos snapshots publicados.

### HW2 — Inicialização e ownership

- [ ] Documentar a ordem de probe, reset, habilitação, registro e publicação
  de cada driver.
- [ ] Confirmar que recursos adquiridos sejam liberados ou publicados como
  degradados quando uma etapa posterior falhar.
- [ ] Validar IRQ compartilhada, EOI, DMA de 32 bits, alinhamento, buffers e
  limites de polling.
- [ ] Rejeitar chamadas antes de READY, durante quiescência ou depois de
  encerramento.
- [ ] Evitar alocação, bloqueio e logging pesado em IRQ e hot paths.

### HW3 — Entrada, vídeo e áudio

- [ ] Validar teclado PS/2, mouse PS/2, USB HID, VGA, VESA, backbuffer, AC97 e
  PC Speaker nos perfis com e sem o dispositivo correspondente.
- [ ] Confirmar preservação do Shell e de uma saída diagnóstica quando a GUI,
  VESA, mouse ou áudio estiverem indisponíveis.
- [ ] Medir filas, descartes, timeouts e recuperação de entrada sob carga.
- [ ] Confirmar que desativação de áudio e vídeo seja idempotente e não afete
  o diagnóstico textual.
- [ ] Registrar a dívida do PS/2 separadamente até que o critério do Roadmap
  22 seja satisfeito.

### HW4 — Storage e USB

- [ ] Validar ATA PIO, FAT12/FAT32, USB MSC, UHCI/EHCI e ausência de volumes
  adicionais.
- [ ] Confirmar que probe, `device-scan` e diagnósticos não inicializem ou
  reinicializem hardware fora de seu contrato.
- [ ] Testar setor inválido, timeout, dispositivo ausente, fila cheia e DMA
  incompatível.
- [ ] Preservar volumes pinned e impedir desmontagem de volumes ocupados.
- [ ] Conferir integração com sync, rollback e recuperação do Roadmap 20.

### HW5 — Rede e energia

- [ ] Validar E1000, RTL8139, ausência de NIC, múltiplas NICs e o estado
  degradado de interfaces sem driver.
- [ ] Confirmar Ethernet, ARP, IPv4, DHCP, DNS, TCP e HTTP nos perfis em que
  a capacidade estiver presente.
- [ ] Validar ACPI RSDP, raiz, FADT, MADT, PM1, S5, RESET_REG e fallbacks de
  reboot sem escrever em capacidade não validada.
- [ ] Exercitar `poweroff`, `reboot`, quiescência e retorno de erro antes do
  commit.
- [ ] Confirmar que nenhum fallback dependa de porta privada de emulador.

### HW6 — Diagnóstico e suporte

- [ ] Fazer `health` e `regcheck full` relatarem a causa e o impacto de cada
  indisponibilidade sem mascarar falhas reais.
- [ ] Garantir que `devices`, `device-info`, `acpi tables`, `net status`,
  `usb status` e `power status` publiquem snapshots sem ponteiros persistentes.
- [ ] Repetir probe, diagnóstico e shutdown para detectar recursos residuais.
- [ ] Produzir uma tabela pública de suporte, limitações e fallback por perfil.
- [ ] Registrar o hardware real testado separadamente dos fixtures QEMU.

## Critérios de saída

- O boot e o Shell continuam funcionais em todos os perfis obrigatórios.
- Ausência ou falha de hardware opcional nunca causa panic, loop infinito ou
  espera sem limite.
- Cada recurso tem owner, estado, timeout, liberação e diagnóstico definidos.
- A matriz diferencia capacidade validada de capacidade apenas inventariada.
- A lista de hardware suportado da 1.0.0 é reproduzível e não promete testes
  que não foram executados.

## Fora do escopo

Bluetooth, Wi-Fi completo, IPv6, VLAN, hotplug universal, múltiplas rotas,
drivers proprietários, ACPI AML genérico e suporte a novos chipsets sem
hardware de validação não entram automaticamente na versão 1.0.0.

## Validação do usuário

O agente não executará build, testes ou QEMU. O usuário deverá executar cada
perfil suportado, registrar o hardware detectado e anexar os resultados de
`health`, `regcheck full` e os diagnósticos do subsistema correspondente.
