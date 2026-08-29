# Roadmap 17 - Migração gradual do ZephyrOS para Rust

## Objetivo

Introduzir Rust no ZephyrOS de forma incremental após a estabilização da
versão 1.0.0, formando um kernel híbrido em C, Rust e Assembly. A migração
deve reduzir riscos de memória em novos componentes sem quebrar a ABI ring 3,
as syscalls, os formatos de arquivos, o build atual ou os contratos já
validados.

Este documento é uma frente independente. Ele não altera o escopo dos
Roadmaps 10, 11, 12 ou de qualquer outra etapa existente.

## Resumo de progresso

- [ ] RUST0 - Congelamento da base da versão 1.0.0.
- [ ] RUST1 - Toolchain, target freestanding e fronteira C/Rust.
- [ ] RUST2 - Primeiro módulo Rust isolado e diagnóstico `rustcheck`.
- [ ] RUST3 - Migração de lógica pura e estruturas sem hardware.
- [ ] RUST4 - Migração seletiva de recursos do kernel.
- [ ] RUST5 - Migração seletiva de drivers e protocolos.
- [ ] RUST6 - Avaliação de componentes críticos e SDK de aplicativos.
- [ ] RUST7 - Consolidação do kernel híbrido e decisão de novas migrações.

Estado atual: planejado para depois da versão 1.0.0. Nenhuma migração Rust
foi iniciada por este roadmap. RUST0 só pode começar após a base escolhida da
1.0.0 estar reproduzível, validada e com rollback preservado.

## Atalhos

- [Roadmap 03 - Kernel e Desempenho](03-kernel-e-desempenho.md)
- [Roadmap 09 - Funcionalidades aplicáveis](09-funcionalidades-aplicaveis.md)
- [Roadmap 11 - Gerenciamento Avançado de Memória](11-gerenciamento-avancado-de-memoria.md)
- [Roadmap 12 - Concorrência e Sincronização](12-concorrencia-e-sincronizacao.md)
- [Contratos públicos](../qualidade/contratos-publicos.md)
- [Dívidas técnicas da v1.0.0](../qualidade/dividas-tecnicas-v1.0.0.md)
- [Registro de validações](../qualidade/registro-validacoes.md)

## Decisão arquitetural

O ZephyrOS não será reescrito integralmente em Rust. A estratégia será:

1. lançar a versão 1.0.0 com a base atual estabilizada;
2. preservar C e Assembly onde a integração de baixo nível já é confiável;
3. escrever novos componentes em Rust quando houver benefício verificável;
4. migrar módulos antigos somente após uma implementação equivalente, testes e
   comparação de métricas;
5. manter uma fronteira C/Rust pequena, explícita e compatível.

Rust não elimina a necessidade de `unsafe` em MMIO, portas de I/O, acesso a
interrupções, DMA, troca de contexto e FFI. A segurança deve ser concentrada
nas fronteiras e demonstrada por validação, não presumida pela linguagem.

A integração seguirá três camadas com responsabilidades separadas:

1. `bindings` e FFI: representação mínima dos contratos C e das estruturas
   externas, sem espalhar acesso bruto pelo restante do código;
2. helpers e abstrações: validação de ponteiros, tamanhos, ownership,
   alinhamento, locks, erros e recursos, concentrando o `unsafe` revisado;
3. módulos finais: lógica Rust que consome as abstrações e não acessa
   diretamente bindings C quando uma abstração segura validada existir.

Cada bloco `unsafe` deve possuir uma invariante verificável, uma fronteira
pequena e uma revisão específica. Rust reduz classes de erros, mas não torna
automaticamente seguro FFI, MMIO, DMA, Assembly, interrupções ou contratos de
ownership vindos de C.

## Critérios de decisão técnica

Uma migração só avança quando houver benefício verificável para segurança de
memória, estabilidade, desempenho, tamanho, manutenção ou clareza de
invariantes. A adoção de Rust por si só não é critério suficiente.

Cada etapa deve comparar, no mínimo:

- tamanho do objeto e da imagem final;
- uso de heap, PMM, SLAB e pico de memória;
- tempo de boot e latência das operações afetadas;
- resultado funcional, warnings, falhas injetadas e cobertura da matriz;
- complexidade da fronteira C/Rust e custo de manutenção;
- capacidade de rollback para a implementação anterior.

Se a implementação Rust não melhorar uma métrica relevante sem piorar outra,
a versão C permanece válida. Nenhuma versão C deve ser removida apenas para
forçar a adoção da linguagem.

## Ordem otimizada de migração

Para maximizar estabilidade e desempenho, a ordem prática recomendada é:

1. `RUST0` e `RUST1`: congelar a base 1.0.0, validar o target i686 e criar a
   fronteira C/Rust reproduzível;
2. `RUST2`: migrar o parser e a validação do pipeline do Shell, mantendo o
   executor, threads, pipes e VFS em C;
3. `RUST3.1`: migrar lógica pura de `vfs_path.c`, validadores de pacotes,
   manifestos, argumentos, estados de atualização e partes separáveis de
   `file_index.c` e do catálogo de aplicativos;
4. `RUST3.2`: extrair da `vma.c` somente o algoritmo puro de intervalos,
   first-fit, ordenação, divisão e coalescência; PMM, paging e mapeamento de
   páginas continuam em C;
5. `RUST4`: criar wrappers de ownership para buffers de pipes, descritores,
   `net_packet_t`, requisições de bloco e filas, sem alocação ou bloqueio em
   IRQ e hot paths;
6. `RUST5`: começar por inventário PCI, diagnósticos e um PHY ou driver simples,
   mantendo o caminho C como fallback até a matriz completa;
7. `RUST6`: disponibilizar o SDK Rust opcional para aplicativos ring 3, sem
   alterar a ABI de syscalls ou os aplicativos C existentes.

Essa ordem evita iniciar pelo núcleo do PMM, VMM, scheduler, troca de contexto,
VFS de disco, USB complexo ou boot. Esses componentes só avançam quando as
abstrações, métricas e mecanismos de rollback já estiverem comprovados.

### Regras de otimização dos caminhos críticos

- [ ] Não alocar dinamicamente em IRQ ou em hot paths sem uma justificativa e
  uma medição específica.
- [ ] Preferir pools fixos, buffers de capacidade conhecida e estruturas com
  layout previsível quando latência e tamanho forem prioridades.
- [ ] Não atravessar FFI com `Vec`, `Box`, slices ou tipos Rust que carreguem
  ownership implícito; usar handles e contratos explícitos.
- [ ] Usar `#[repr(C)]` somente nos tipos compartilhados e `#[repr(transparent)]`
  para wrappers de handles quando isso preservar o ABI sem custo adicional.
- [ ] Evitar generics excessivos, dispatch dinâmico e formatação pesada nos
  caminhos críticos quando aumentarem o binário ou a latência medida.
- [ ] Concentrar `unsafe` nos wrappers de hardware, FFI e ownership; a lógica
  final deve consumir abstrações revisadas.
- [ ] Comparar tamanho de código, acessos à memória, latência, throughput,
  tempo de boot e consumo de PMM/heap antes de aceitar uma otimização.

## RUST0 - Base 1.0.0 e contratos congelados

### Objetivo

Usar a versão 1.0.0 como linha de base funcional antes de iniciar a
migração.

### Checklist

- [ ] Fechar a matriz funcional básica do sistema e registrar seus resultados.
- [ ] Validar novamente `q3check`, build completo e a matriz QEMU da versão.
- [ ] Confirmar que não há dívida técnica nova sem aceitação explícita.
- [ ] Congelar os números e formatos públicos das syscalls e da App API.
- [ ] Congelar os layouts C que atravessam módulos públicos ou ring 3.
- [ ] Registrar linhas-base de memória, boot, scheduler, VFS, rede e tempo de
  compilação.
- [ ] Criar uma branch ou marco de referência da versão 1.0.0 antes da primeira
  alteração Rust.
- [ ] Registrar explicitamente quais pendências não bloqueiam a base congelada
  e quais devem ser resolvidas antes de abrir uma migração.

### Critério de saída

A versão 1.0.0 pode ser reproduzida e validada sem depender de Rust. A futura
migração poderá ser revertida para essa base sem alteração do bootloader.

## RUST1 - Toolchain e fronteira C/Rust

### Objetivo

Compilar um módulo Rust mínimo para o ambiente freestanding atual e chamá-lo a
partir de C.

### Trabalho

- [ ] Fixar versões de `rustc`, `rust-src`, LLVM/bindgen e ferramentas
  auxiliares em uma configuração reproduzível (`rust-toolchain` e lockfiles
  quando aplicável).
- [ ] Definir um target Rust para o ambiente x86 de 32 bits do ZephyrOS, usando
  target nativo compatível quando suficiente ou um target personalizado quando
  necessário, validando ABI, calling convention, alinhamento e integração com
  `i686-elf-gcc`.
- [ ] Configurar `#![no_std]`, `panic_handler` e `panic=abort`.
- [ ] Integrar a compilação do objeto Rust ao `Makefile`, sem substituir o
  linker ou a ABI existentes sem validação.
- [ ] Fixar flags de compilação, linker, layout de seções, símbolos esperados e
  estratégia de panic para que C, Rust e Assembly produzam uma imagem
  reproduzível.
- [ ] Definir convenções para `extern "C"`, `#[repr(C)]`, `#[no_mangle]`, tipos
  de largura fixa e retorno dos códigos de `core/errors.h`.
- [ ] Proibir `std`, unwinding e dependências que exijam runtime hospedado.
- [ ] Definir como Rust acessará `LOG_*`, os allocators existentes (`kmalloc`,
  `kmem_cache`, PMM, páginas e arenas), locks, wait queues e funções de vídeo
  sem duplicar implementações.
- [ ] Definir a camada `bindings` mínima, os helpers de FFI e as abstrações
  seguras que os módulos Rust finais consumirão.
- [ ] Definir a validação de ponteiros, tamanhos, alinhamento e posse antes de
  toda chamada recebida de C.
- [ ] Definir uma política para `unsafe`, incluindo invariantes documentadas,
  revisão dos blocos e proibição de acesso bruto aos bindings quando houver
  abstração segura validada.
- [ ] Adicionar um teste mínimo de link, chamada C/Rust, retorno de erro e
  confirmação de que nenhum panic/unwind atravessa a fronteira C.

### Critério de saída

Um módulo Rust sem `std` é compilado, linkado e executado no kernel mantendo o
build C/ASM e os contratos existentes. Falhas de inicialização ou FFI são
registradas com o módulo responsável.

## RUST2 - Primeiro módulo e `rustcheck`

### Objetivo

Validar Rust em código real, mas sem tocar inicialmente em scheduler, memória
física, VFS ou drivers.

### Alvo recomendado

Começar por uma unidade de lógica determinística e isolada, preferencialmente
o parser e a validação de pipeline do Shell. O parsing pode ser separado de
threads, pipes, VFS e `video_print`; a execução continuará em C até que a
fronteira esteja validada.

### Trabalho

- [ ] Implementar o parser Rust mantendo o comportamento de operadores `|`,
  `>` e `>>` já definido pelo Shell.
- [ ] Expor uma API C pequena para entrada, saída e códigos de erro.
- [ ] Cobrir espaços, operadores colados, limites de estágios e sintaxes
  inválidas.
- [ ] Manter o executor cooperativo e os descritores existentes em C.
- [ ] Criar `rustcheck` como diagnóstico do Shell para validar o módulo,
  fronteiras FFI e limpeza de estado.
- [ ] Integrar o resultado a `health`, `regcheck` ou a outro diagnóstico
  existente quando a validação deixar de ser experimental.

### Critério de saída

O parser Rust passa a matriz determinística e a regressão do Shell sem
alterar os resultados de `procs | grep shell`, `echo texto | grep texto` e dos
redirecionamentos existentes.

## RUST3 - Lógica pura e estruturas sem hardware

### Objetivo

Expandir Rust para componentes em que a maior parte do risco está em
invariantes de dados, limites e posse de memória, não em acesso direto ao
hardware.

### Ordem sugerida

- [ ] Helpers de strings e parsing, mantendo wrappers compatíveis para os
  chamadores C.
- [ ] Validadores de argumentos, manifestos e estruturas de diagnóstico que
  não façam I/O direto.
- [ ] Componentes auxiliares de índice, catálogo ou estado, apenas depois de
  medir custo de código e memória.
- [ ] Metadados e validações de pacotes, mantendo criptografia e BearSSL em C
  até existir uma decisão específica para essa dependência.
- [ ] Testes de propriedade ou vetores determinísticos no host, quando o
  algoritmo puder ser separado do kernel.

### Regras

- [ ] Cada migração deve preservar a assinatura pública ou fornecer um wrapper
  C equivalente.
- [ ] Cada etapa deve comparar tamanho do objeto e da imagem, uso de heap/PMM/
  SLAB, pico de memória, tempo de boot, latência, warnings e resultado
  funcional com a implementação anterior.
- [ ] A versão C só poderá ser removida após a matriz de regressão e a
  confirmação de rollback da etapa.

### Critério de saída

Pelo menos um componente de lógica pura é mantido em Rust em produção de
desenvolvimento, com testes, diagnóstico e ABI documentados, sem aumento não
aceito de falhas, memória ou tempo de boot.

## RUST4 - Recursos do kernel e posse de memória

### Objetivo

Usar Rust em recursos que lidam com buffers e estados compartilhados, sem
começar pelos componentes que controlam a inicialização inteira do kernel.

### Candidatos

- [ ] Validação e manipulação de `net_packet_t`, mantendo a programação dos
  dispositivos e o caminho de IRQ em C inicialmente.
- [ ] Estruturas auxiliares de pipes, wait queues ou filas, mantendo os
  pontos de bloqueio e wakeup compatíveis com o kernel atual.
- [ ] Wrappers seguros para slices de buffers, tamanhos e transferência de
  posse entre VFS, rede e Shell.
- [ ] Wrappers de ownership para cada allocator e recurso, com criação,
  empréstimo, transferência, destruição e invalidação de aliases explícitos.
- [ ] Componentes de diagnóstico de SLAB e integridade, sem substituir o
  alocador `kmem_cache` antes de uma avaliação separada.

### Restrições

- [ ] Reutilizar os allocators existentes por meio de wrappers validados; não
  implementar um segundo alocador global em Rust.
- [ ] Não permitir que `Vec`, `Box` ou `alloc` atravessem a FFI sem contrato
  explícito de criação e destruição.
- [ ] Usar o par correto de alocação e liberação para cada recurso; Rust não
  deve liberar memória C, PMM ou páginas com uma API incompatível.
- [ ] Não manter referências Rust após o objeto C ser liberado.
- [ ] Usar locks e wait queues existentes através de wrappers validados.
- [ ] Registrar toda falha de alocação, conversão, posse ou cancelamento.
- [ ] Concentrar `unsafe` nos wrappers revisados e testar invariantes de
  ownership, aliasing, alinhamento, tamanho e ciclo de vida.

### Critério de saída

Os componentes escolhidos suportam pressão de buffers, cancelamento,
concorrência e destruição sem vazamentos, double free, descritores residuais
ou alteração das estatísticas existentes de PMM, heap e SLAB.

## RUST5 - Drivers e protocolos

### Objetivo

Migrar seletivamente código de hardware somente quando as abstrações de
acesso forem suficientes e houver uma matriz QEMU ou hardware reproduzível.

### Ordem sugerida

- [ ] Começar por um componente de leitura ou estado com pouca dependência de
  DMA, como metadados PCI ou parte de um diagnóstico.
- [ ] Criar wrappers para MMIO, portas de I/O, barreiras, IRQ, DMA e ownership
  de buffers.
- [ ] Fazer os drivers consumirem abstrações de dispositivo e bus, evitando
  bindings C diretos no código final sempre que houver wrapper validado.
- [ ] Escolher um único driver experimental, mantendo o driver C como fallback
  até a validação completa.
- [ ] Avaliar depois drivers de rede, USB e armazenamento, que dependem de
  interrupções, filas e buffers físicos.
- [ ] Migrar protocolos (`ARP`, `IPv4`, `UDP`, `TCP`, `DNS`, `DHCP` e HTTP)
  somente depois de estabilizar as fronteiras de buffers e sockets.

### Critério de saída

O driver Rust escolhido passa a matriz de inicialização, erro, reset,
interrupção, RX/TX e desligamento sem regressão nos perfis de rede, USB ou
armazenamento aplicáveis.

## RUST6 - Componentes críticos e SDK de aplicativos

### Componentes que ficam por último

- [ ] `src/memory/memory.c`, `src/memory/paging.c` e `src/memory/slab.c`.
- [ ] `src/process/process.c`, `src/process/signal.c` e `src/process/ipc.c`.
- [ ] `src/thread/thread.c`, troca de contexto e integração com TSS.
- [ ] `src/fs/vfs.c`, `fat12.c`, `fat32.c`, `storage.c` e `block.c`.
- [ ] `src/core/syscall.c`, `app_api.c` e a entrada ring 3.
- [ ] `src/kernel/kernel.c`, `panic.c` e a sequência de inicialização.

Esses módulos só serão migrados se houver benefício mensurável e um plano de
rollback. A permanência em C é uma decisão válida quando a migração aumentar
risco sem melhorar segurança, manutenção ou desempenho.

Os módulos centrais permanecem em C durante as primeiras fases, mas podem
receber wrappers Rust, diagnósticos e abstrações de ownership antes da
migração da implementação. A camada segura não implica reescrever o núcleo
nem substituir PMM, VMM, SLAB, scheduler ou ABI sem evidência independente.

### Rust para aplicativos

- [ ] Criar um SDK Rust opcional para a App API já existente.
- [ ] Manter os números de syscall e o formato dos pacotes `.zephyrosapp`.
- [ ] Definir wrappers de `file_read`, `file_write`, `pipe`, console, memória
  e encerramento usando a ABI atual.
- [ ] Validar o SDK com um aplicativo ring 3 pequeno antes de migrar apps
  nativos do Shell.
- [ ] Manter os aplicativos C existentes durante todo o período de transição.

## Mapa de migração por área

| Área | Arquivos principais | Estratégia |
|---|---|---|
| Boot e entrada | `src/boot/*.asm`, `src/kernel/entry.asm`, `src/kernel/switch.asm`, `src/drivers/isr.asm`, `src/drivers/irq.asm` | Permanecem em Assembly por padrão; qualquer alteração no boot deve ser comunicada explicitamente, preservar/verificar os 512 bytes e ser documentada. |
| Inicialização e panic | `src/kernel/kernel.c`, `src/kernel/panic.c` | Permanecem em C no início; Rust usa wrappers de log e erro. |
| Memória | `src/memory/memory.c`, `paging.c`, `slab.c` | Implementação central por último; wrappers Rust de ownership, allocators e diagnósticos podem ser introduzidos antes, preservando PMM, VMM, heap e SLAB. |
| Processos e threads | `src/process/*.c`, `src/thread/thread.c` | Última etapa; preservar assinaturas, scheduler, sinais e troca de contexto. |
| VFS e armazenamento | `src/fs/*.c` | Migrar somente após validar ownership, descritores, FAT e rollback. |
| Shell | `src/shell/shell_pipeline.c`, `shell_command_utils.c` | Primeiro alvo prático: parser e validação, separados do executor. |
| Rede | `src/core/{arp,ipv4,udp,tcp,dns,dhcp}.c`, drivers Ethernet | Migrar por componente; manter caminhos C e fallback durante a validação. |
| USB e drivers | `src/drivers/*.c`, `src/core/usb_*.c` | Após wrappers de MMIO, IRQ, DMA e filas. |
| Criptografia de terceiros | `vendor/bearssl`, `crypto*.c` | Permanece em C até decisão própria; não reescrever por consequência da adoção Rust. |
| Aplicativos | `src/shell/*.c`, `src/*/*.c` de apps | Criar SDK Rust opcional; migração individual e posterior. |

## Contrato C/Rust

- `#[repr(C)]` em todo tipo compartilhado.
- `extern "C"` em toda função exposta ou consumida pelo outro lado.
- Tipos `u8`, `u16`, `u32`, `i32` e ponteiros validados; sem `usize` ou layout
  Rust implícito em contratos persistentes.
- Códigos de erro devem usar `core/errors.h`; `Result` fica restrito à
  implementação Rust ou a wrappers documentados.
- Nenhum panic pode atravessar a fronteira C.
- Toda posse de memória e recurso deve ter criador, consumidor, destrutor e
  allocator/liberador correspondente definidos.
- A fronteira deve seguir `bindings C → helpers/abstrações seguras → módulos
  Rust finais`; módulos finais não acessam bindings brutos sem justificativa.
- Blocos `unsafe` devem ser pequenos, ter invariantes verificáveis e passar por
  revisão específica de aliasing, alinhamento, validade e ciclo de vida.
- Toda função pública deve registrar falhas observáveis conforme o contrato de
  logs do projeto.
- ABI ring 3, syscalls, App API, formato ZAPP e descritores VFS permanecem
  compatíveis.
- O bootloader permanece Assembly por padrão; se uma alteração for necessária,
  deve ser comunicada explicitamente ao usuário, preservar/verificar os 512
  bytes e ser registrada na documentação da etapa.

## Validação por etapa

- [ ] `rustcheck` valida toolchain, FFI, panic, alinhamento, ownership e
  limpeza.
- [ ] Testes determinísticos e, quando aplicável, testes de propriedade no
  host cobrem a lógica separável do kernel.
- [ ] Cada migração publica a comparação de tamanho, memória, boot, latência,
  warnings, estabilidade e rollback antes de remover a implementação C.
- [ ] `q3check` e `make clean && make` são executados pelo usuário após cada
  alteração de build, header ou código.
- [ ] A matriz QEMU funcional é repetida pelo usuário; o agente não executa
  build, testes ou QEMU.
- [ ] `health`, `memcheck`, `schedcheck`, `regcheck full`, `vfs test` e
  `pipetest` permanecem em `OK` quando aplicáveis.
- [ ] Drivers e rede repetem os perfis QEMU já validados.
- [ ] Se o bootloader for alterado, a imagem confirma o limite de 512 bytes e a
  validação registra explicitamente o motivo, o escopo e o impacto da mudança.
- [ ] O registro cronológico recebe a data e hora real de cada implementação e
  validação.
- [ ] O diff de cada etapa é revisado para excluir `Makefile.local`, `build/`,
  imagens, backups e dados locais.

## Limitações

- Rust não torna automaticamente seguro o código de FFI, MMIO, DMA, Assembly
  ou interrupções.
- O target freestanding de 32 bits e a integração com `i686-elf-gcc` podem
  exigir uma configuração própria e uma versão fixada do compilador.
- `no_std` não fornece runtime; coleções dinâmicas exigem um allocator do
  kernel explicitamente integrado e não devem atravessar a FFI sem contrato.
- O suporte Rust documentado pelo Linux para `x86` é voltado a `x86_64`; o
  target freestanding i686 do ZephyrOS precisa de validação própria, sem
  presumir compatibilidade da toolchain do Linux.
- O kernel continuará dependendo de C e Assembly em partes de baixo nível.
- Bibliotecas de terceiros, especialmente BearSSL, não serão reescritas sem
  roadmap e validação específicos.
- Nenhuma limitação desta lista é dívida técnica aceita; qualquer exceção para
  encerrar uma etapa deve ser aprovada e registrada em
  `docs/qualidade/dividas-tecnicas-v1.0.0.md`.

## Critério de conclusão do Roadmap 17

- [ ] A versão 1.0.0 permanece reproduzível e compatível.
- [ ] Pelo menos um módulo Rust integrado ao kernel passa a matriz funcional.
- [ ] A fronteira C/Rust e a política de ownership estão documentadas.
- [ ] O build e os diagnósticos detectam falhas de integração.
- [ ] As migrações realizadas têm métricas antes/depois e rollback validado.
- [ ] Cada módulo não migrado possui justificativa técnica, sem obrigação de
  reescrita integral.
- [ ] A decisão de continuar, pausar ou ampliar a migração é registrada com
  evidências, sem marcar a etapa como concluída apenas pela existência do
  primeiro módulo Rust.

## Referências

- [Rust `no_std`](https://doc.rust-lang.org/stable/embedded-book/intro/no-std.html)
- [Targets personalizados do Rust](https://doc.rust-lang.org/nightly/rustc/targets/custom.html)
- [Interoperabilidade C/Rust](https://doc.rust-lang.org/stable/embedded-book/interoperability/c-with-rust.html)
- [Documentação Rust do Linux](https://docs.kernel.org/rust/quick-start.html)
- [Abstrações e bindings Rust no Linux](https://docs.kernel.org/6.15/rust/general-information.html)
- [Arquiteturas suportadas pelo Rust no Linux](https://docs.kernel.org/rust/arch-support.html)
- [Projeto Rust for Linux e drivers de referência](https://rust-for-linux.com/rust-reference-drivers)
