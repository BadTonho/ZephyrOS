# Escopo e critérios da versão 1.0.0

## Finalidade

Este documento define o que significa entregar o ZephyrOS 1.0.0. Ele não é
uma etapa de implementação nem substitui os Roadmaps 18–22. Sua função é
separar as capacidades básicas obrigatórias das expansões que podem ser
realizadas depois da primeira versão estável.

O Roadmap 17, de migração gradual para Rust, só poderá começar depois que este
escopo for atendido, os Roadmaps 18–22 forem encerrados e a base C/Assembly
estiver congelada.

## Definição da 1.0.0

O ZephyrOS 1.0.0 será um sistema operacional funcional de uso geral básico,
com boot, kernel, memória, processos, armazenamento, entrada, saída,
aplicativos, rede básica, diagnóstico, recuperação e desligamento confiáveis.

A versão não precisa possuir todos os recursos de sistemas operacionais
maduros, mas não deve depender de funcionalidades essenciais apenas como
protótipos, fixtures ou comportamento não documentado.

## Capacidades obrigatórias

### 1. Boot e recuperação

- [ ] Boot normal reproduzível e recovery funcional.
- [ ] Layout da imagem, kernel, recovery loader e FAT32 sem sobreposição.
- [ ] `boot.bin` preservado em 512 bytes.
- [ ] Fallback seguro quando uma capacidade opcional não estiver disponível.
- [ ] Procedimento documentado de rollback para a última imagem aprovada.

### 2. Kernel e memória

- [ ] Interrupções, exceções, PIT, scheduler e troca de contexto estáveis.
- [ ] Heap, PMM, paging, VMA, SLAB/SLUB e desalocação sem corrupção ou
  vazamentos conhecidos.
- [ ] Idle arquitetural sem busy-wait desnecessário.
- [ ] Contadores e diagnósticos de memória e scheduler consistentes.
- [ ] Nenhuma falha de serviço deixa o sistema sem caminho de recuperação.

### 3. Processos e execução de aplicativos

- [ ] Processos ring 3 isolados do kernel e de outros processos.
- [ ] Criação, execução, bloqueio, sinais, término, zombie e reaping corretos.
- [ ] PID e generation revalidados em ações, callbacks e snapshots.
- [ ] IPC, pipes, sockets e threads sem handles, buffers ou callbacks residuais.
- [ ] Falhas de aplicativos não derrubam o kernel nem deixam o Shell preso.

### 4. App API e syscalls

- [ ] Syscalls e App API possuem números, tipos, erros e ownership definidos.
- [ ] Ponteiros, tamanhos, alinhamento, ranges e handles são validados.
- [ ] ABI ring 3 e layouts binários são congelados antes do Rust 17.
- [ ] Mudanças futuras exigem versão ou extensão explícita, nunca alteração
  silenciosa do contrato.
- [ ] `appcheck`, `memcheck`, `schedcheck` e diagnósticos relacionados passam
  na matriz final.

### 5. VFS e armazenamento

- [ ] VFS, FAT12/FAT32, diretórios, leitura, escrita, exclusão, rename, mount,
  unmount e CWD funcionam de modo determinístico.
- [ ] Storage, Block Layer, cache e filas possuem ownership e sincronização
  claros.
- [ ] `sync/flush` não é duplicado e não publica escrita como concluída quando
  o dispositivo falha.
- [ ] Existe verificação de consistência para detectar FAT, diretórios,
  cadeias, tamanhos e setores inválidos.
- [ ] Operações interrompidas ficam concluídas, revertidas ou explicitamente
  recuperáveis.
- [ ] Volumes ocupados e volumes pinned são tratados sem fechar handles à
  força.

### 6. Entrada, vídeo e áudio

- [ ] Teclado PS/2 e mouse PS/2 funcionam com filas, foco e recuperação.
- [ ] USB HID possui fallback para a entrada PS/2 quando aplicável.
- [ ] VGA textual permanece disponível quando VESA ou a GUI falharem.
- [ ] Simple e Classic possuem fallback controlado.
- [ ] AC97 e PC Speaker têm estados degradados claros quando ausentes.
- [ ] Nenhuma fila de entrada satura sem diagnóstico e recuperação definidos.

### 7. Shell e experiência básica

- [ ] Comandos básicos, diagnósticos e operações cooperativas retornam ao
  prompt `zephyr>` após sucesso, erro, cancelamento ou timeout.
- [ ] O caso de execução que deixa a tela sem prompt deve ser reproduzido,
  corrigido e validado antes da release.
- [ ] Jobs, cenas, foco, `Ctrl+C`, `F12` e fechamento de aplicativos não deixam
  o terminal bloqueado ou visualmente vazio.
- [ ] Explorer, Task Manager, Settings, Desktop, Window Manager e Taskbar
  possuem funcionamento básico e fallback documentado.

### 8. Dispositivos, rede e energia

- [ ] PCI, ATA, USB, NIC, áudio, vídeo e entrada publicam estados detectados,
  indisponíveis, degradados ou com falha.
- [ ] `devices`, `device-info`, `device-scan`, `health` e `regcheck full`
  permanecem úteis sem hardware opcional.
- [ ] Rede básica — Ethernet, IPv4, ARP, DHCP, DNS, TCP e HTTP — funciona nos
  perfis suportados, sem exigir IPv6 ou firewall avançado.
- [ ] ACPI, diagnóstico de energia, `reboot`, `poweroff` e `shutdown` têm
  retorno determinístico antes do commit e caminho terminal após o commit.
- [ ] Nenhum fallback depende de portas privadas de QEMU, Bochs ou VirtualBox.

### 9. Aplicativos, pacotes e atualização

- [ ] Aplicativos podem ser instalados, executados, atualizados, removidos e
  revertidos com validação de pacote.
- [ ] Pacotes assinados, versões, dependências, tamanhos e caminhos são
  validados antes de efeitos persistentes.
- [ ] Falhas de atualização preservam o estado anterior ou deixam recuperação
  explícita.
- [ ] O Shell continua sendo fallback operacional dos aplicativos visuais.

### 10. Diagnóstico e release

- [ ] `health`, `regcheck full`, `memcheck`, `schedcheck`, `proccheck` e os
  diagnósticos de VFS, rede, USB, ACPI e energia passam sem falhas novas.
- [ ] Logs identificam a camada responsável sem expor dados sensíveis ou
  gerar ruído em IRQ e hot paths.
- [ ] A matriz Simple/Classic e os perfis sem ACPI, NIC, USB, VESA, áudio e
  Storage adicional são documentados e reproduzíveis.
- [ ] Tamanho, checksum, layout, tempo de boot e linhas de base de memória e
  desempenho são registrados.

## Recomendação de segurança mínima

Para que a 1.0.0 seja um sistema de uso geral básico, a recomendação é
implementar uma política mínima de identidade e permissões antes do
congelamento da ABI:

- [ ] Identidade de usuário e grupo para processos.
- [ ] Proprietário e permissões básicas para arquivos e diretórios.
- [ ] Separação entre operações normais e privilegiadas.
- [ ] Verificação de permissão nas syscalls, VFS, dispositivos e energia.
- [ ] Erros de autorização distintos de caminho inexistente ou capacidade
  indisponível.

Não é necessário criar ACL complexa, domínio, autenticação remota ou uma
infraestrutura empresarial. Entretanto, sem identidade e permissões, a versão
deve ser descrita honestamente como uma edição single-user técnica, e não como
um sistema operacional geral.

## Ordem de execução

1. [Roadmap 18 — Estabilização e release](18-estabilizacao-e-release-v1.0.md).
2. [Roadmap 19 — Segurança e isolamento](19-seguranca-e-isolamento-v1.0.md),
   incluindo a decisão sobre identidade e permissões mínimas.
3. [Roadmap 20 — Integridade e recuperação do Storage](20-integridade-e-recuperacao-do-storage.md).
4. [Roadmap 21 — Compatibilidade e matriz de hardware](21-compatibilidade-e-matriz-de-hardware.md).
5. [Roadmap 22 — Desempenho e dívidas da 1.0.0](22-desempenho-e-dividas-v1.0.md).
6. Validação final, congelamento da ABI e publicação da base 1.0.0.
7. [Roadmap 17 — Migração gradual para Rust](17-migracao-gradual-rust.md).

O Roadmap 17 não é uma etapa de preparação da 1.0.0. Ele só começa depois do
item 6.

## Pode ficar para depois da 1.0.0

As seguintes capacidades não devem bloquear a primeira versão, desde que suas
ausências estejam documentadas:

- Bluetooth.
- IPv6, VLAN, múltiplas rotas, firewall avançado e hotplug universal.
- Suspensão S1-S4, bateria, hibernação e thermal zones.
- Antivírus completo, quarentena, heurísticas e monitoramento em tempo real.
- SFC com auto-reparo destrutivo e formatação inteligente.
- Snap, áreas de trabalho virtuais e Window Manager avançado.
- Codecs avançados, vídeo, Game Manager completo, PCSista e ferramentas para
  programadores.
- Drivers carregáveis dinamicamente.
- SDK Rust e migração ampla de componentes do kernel.

Esses itens continuam em `docs/melhorias futuras/` até receberem prioridade,
escopo, dependências, contrato e critérios próprios.

## Critérios de declaração da 1.0.0

A versão 1.0.0 só deve ser declarada quando:

- todas as capacidades obrigatórias aplicáveis estiverem implementadas ou
  explicitamente classificadas como indisponíveis por hardware;
- não houver falha conhecida que impeça boot, uso básico, diagnóstico,
  armazenamento, recuperação ou desligamento;
- a matriz de validação tiver evidência do usuário para a mesma imagem;
- as dívidas técnicas aceitas tiverem sido quitadas ou formalmente reavaliadas;
- ABI, syscalls, formatos, limitações e procedimento de rollback estiverem
  congelados e documentados;
- o Roadmap 17 continuar fechado até esse momento.

Este documento é uma decisão de escopo. Ele não declara nenhuma capacidade
como implementada e não substitui a confirmação funcional do usuário.
