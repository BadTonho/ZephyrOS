# Roadmap 21 — Hardware, rede e energia da 1.0.0

## Estado

Status por etapa: HW1 CONCLUIDO; HW2 CONCLUIDO; HW3 CONCLUIDO; HW4 CONCLUIDO;
HW5 CONCLUIDO.
Os sete perfis base QEMU estao versionados, testados e
reproduziveis; o perfil interno EHCI do HW4 esta coberto pelo runner;
hardware fisico permanece `PENDING`.

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
- modelo mínimo de dispositivo com relação pai/filho, bus, classe, driver e
  ciclo de vida;
- diagnósticos `health`, `devices`, `device-scan`, `acpi`, `net`, `usb` e
  `power`;
- timeouts, confirmação de capacidade e fallback seguro.

Esta frente não promete suporte a hardware que não possa ser exercitado. Wi-Fi
completo, Bluetooth, hotplug universal, IPv6, novos chipsets e carregamento
dinâmico de drivers ficam fora da matriz base, salvo uma decisão explícita.

## Dependências

- [Roadmap 18](18-kernel-processos-e-userland-v1.0.md) para a matriz de base;
- [Roadmap 19](19-abi-seguranca-e-permissoes-v1.0.md) para ownership e validação de
  entradas;
- [Roadmap 20](20-vfs-storage-e-atualizacao-v1.0.md) para ATA, USB MSC,
  volumes e falhas de I/O;
- Roadmaps 05, 14, 15 e 16 para contratos de dispositivos, rede, pseudo-FS e
  energia.

## Fases

### HW1 - resultado validado

- [x] Perfis `baseline`, `no-acpi`, `no-nic`, `no-usb`, `no-vesa`, `no-audio`
  e `no-storage` definidos em `config/hardware-profiles.json`.
- [x] Runner QEMU, catalogo, registro de cobertura e Makefiles atualizados.
- [x] Validacao host, catalogo e matriz QEMU concluida com quatro workers e
  seed `2101`.
- [x] Hardware fisico mantido como `PENDING`, sem declaracao de suporte sem
  evidencia reproduzivel.

### HW1 — Catálogo de perfis

- [x] Definir perfis mínimos: QEMU padrão, sem ACPI, sem NIC, sem USB, sem
  VESA, sem áudio e sem Storage adicional.
- [x] Registrar, por perfil, hardware detectado, driver ativo, capacidade,
  fallback, erro esperado e diagnóstico observável.
- [x] Separar hardware apenas inventariado de hardware com driver inicializado
  e validado funcionalmente.
- [x] Separar “não presente”, “não suportado”, “falhou ao inicializar” e
  “desabilitado por política”.
- [x] Definir quais cenários são obrigatórios para a 1.0.0 e quais dependem de
  hardware real.
- [x] Manter IDs, BDFs, endereços e versões estáveis nos snapshots publicados.

### HW2 — Inicialização e ownership

- [x] Documentar a ordem de probe, reset, habilitação, registro e publicação
  de cada driver.
- [x] Definir a relação entre dispositivo pai, bus, classe e driver, além dos
  pontos de `probe`, `remove`, `shutdown` e quiescência.
- [x] Confirmar que recursos adquiridos sejam liberados ou publicados como
  degradados quando uma etapa posterior falhar.
- [x] Validar IRQ compartilhada, EOI, DMA de 32 bits, alinhamento, buffers e
  limites de polling.
- [x] Rejeitar chamadas antes de READY, durante quiescência ou depois de
  encerramento.
- [x] Garantir que a remoção ou falha de um dispositivo invalide handles,
  callbacks e buffers sem deixar referências para o objeto físico.
- [x] Evitar alocação, bloqueio e logging pesado em IRQ e hot paths.

HW2 foi validado com o registro interno de lifecycle, testes host-only dos
drivers diretamente afetados, catalogo sincronizado e 23 casos QEMU em quatro
workers, incluindo os diagnosticos KRN6 e SEC6. Hardware fisico permanece
`PENDING`; HW3 em diante continuam planejados.

### HW3 — Entrada, vídeo e áudio

- [x] Validar teclado PS/2, mouse PS/2, USB HID, VGA, VESA, backbuffer, AC97 e
  PC Speaker nos perfis com e sem o dispositivo correspondente.
- [x] Confirmar preservação do Shell e de uma saída diagnóstica quando a GUI,
  VESA, mouse ou áudio estiverem indisponíveis.
- [x] Medir filas, descartes, timeouts e recuperação de entrada sob carga.
- [x] Confirmar que desativação de áudio e vídeo seja idempotente e não afete
  o diagnóstico textual.
- [x] Registrar a dívida do PS/2 separadamente até que o critério do Roadmap
  22 seja satisfeito.

HW3 foi validado sobre o lifecycle interno: os caminhos PS/2, USB HID, VESA,
backbuffer, AC97 e speaker validam estado, limites e limpeza sem alterar ABI,
syscalls, headers publicos, bootloader ou Stage 2. Os 14 casos QEMU associados
pela tag `hw3` terminaram `PASS` com quatro workers, seed `2103` e nenhum
processo QEMU residual. Hardware fisico e a validacao PS/2 continuam
`PENDING` sem bloquear esta etapa.

### HW4 — Storage e USB

- [x] Validar ATA PIO, FAT12/FAT32, USB MSC, UHCI/EHCI e ausência de volumes
  adicionais.
- [x] Confirmar que probe, `device-scan` e diagnósticos não inicializem ou
  reinicializem hardware fora de seu contrato.
- [x] Testar setor inválido, timeout, dispositivo ausente, fila cheia e DMA
  incompatível.
- [x] Preservar volumes pinned e impedir desmontagem de volumes ocupados.
- [x] Conferir integração com sync, rollback e recuperação do Roadmap 20.

O HW4 integra o MSC ao lifecycle interno dos drivers e mantém o provedor USB
somente-leitura. O runner possui os perfis `usb-storage`, `no-storage`,
`no-usb` e o perfil interno determinístico `usb-storage-ehci`, que
seleciona EHCI high-speed e exige uma fixture raw anexada ao snapshot. A
A cobertura host-only e a matriz QEMU terminaram `PASS` com quatro workers,
seed `2104`, sete casos selecionados e nenhum processo QEMU residual. O caso
EHCI validou leitura real do MSC e rejeição de escrita; hardware físico
continua `PENDING`.

### HW5 — Rede e energia

- [x] Validar E1000, RTL8139, ausência de NIC, múltiplas NICs e o estado
  degradado de interfaces sem driver.
- [x] Confirmar Ethernet, ARP, IPv4, DHCP, DNS, TCP e HTTP nos perfis em que
  a capacidade estiver presente.
- [x] Validar ACPI RSDP, raiz, FADT, MADT, PM1, S5, RESET_REG e fallbacks de
  reboot sem escrever em capacidade não validada.
- [x] Exercitar `poweroff`, `reboot`, quiescência e retorno de erro antes do
  commit.
- [x] Confirmar que nenhum fallback dependa de porta privada de emulador.
- [x] Publicar hora monotônica e hora civil do relógio/RTC quando a fonte
  estiver ausente, inválida ou ainda não sincronizada.

HW5 foi concluida em 2026-09-12 com a matriz host e QEMU validada.
E1000, RTL8139, ACPI, energia e RTC/clock publicam estados, ownership,
fallbacks e diagnosticos; `network-dual` confirmou duas NICs E1000 distintas
em redes privadas restritas. A matriz executou 12 casos com quatro workers,
seed 2105, todos `PASS`, sem acesso a Internet e sem processos residuais.
Hardware fisico continua `PENDING`; DT100-003, DT100-004 e a divida fisica
do PS/2 permanecem separadas.

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
- Cada dispositivo tem relação de dependência e ciclo de vida definidos,
  inclusive no caminho de falha e encerramento.
- A matriz diferencia capacidade validada de capacidade apenas inventariada.
- A relação pai/filho e o ciclo de vida dos dispositivos permanecem válidos
  durante probe, falha, quiescência e encerramento.
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
