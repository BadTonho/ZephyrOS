# AGENTS.md — ZephyrOS

Leia este arquivo no início de toda sessão. Siga estas regras SEMPRE.

---

## Objetivo do projeto

O objetivo do ZephyrOS é construir o melhor sistema possível, priorizando
correção, segurança, robustez, manutenibilidade, desempenho e coerência
arquitetural. Esta é uma orientação de produto e contexto para as decisões do
agente, não uma regra que substitua o escopo ou a autorização de cada tarefa.

## Regra #21: Recomendações pela melhor opção técnica

Quando o usuário perguntar qual é a melhor opção, melhor arquitetura, melhor
solução ou equivalente, o agente DEVE recomendar a opção tecnicamente superior
para o objetivo informado, independentemente de ela se encaixar facilmente no
estado atual do projeto. A recomendação deve considerar correção, segurança,
robustez, manutenção, desempenho, compatibilidade e custo total de evolução.

Se a melhor opção exigir migração, refatoração ou mudança de contrato, o agente
deve informar o impacto, os riscos e o caminho de adoção. Uma alternativa mais
fácil ou compatível pode ser apresentada separadamente, mas não deve substituir
a recomendação principal apenas por ser mais conveniente para o código atual.

Esta regra não autoriza alterações fora do escopo pedido: a recomendação e a
implementação continuam limitadas à autorização do usuário e às demais regras
deste arquivo.

---

## Build e validação

Os comandos abaixo são referências para execução pelo usuário. Eles são
pré-requisitos operacionais para abrir/testar no QEMU qualquer alteração de
código, header ou Makefile:

```bash
# Gate de qualidade após alterar código
make q3check

# Build completo
make clean && make

# Execução no QEMU
make run
```

O usuário não consegue testar a versão alterada no QEMU sem executar primeiro
`make q3check` e `make clean && make`. Depois que esses pré-requisitos forem
confirmados para a mesma versão do código, o agente não deve reapresentá-los
como testes funcionais pendentes da fase; deve listar apenas a matriz de
validação específica da funcionalidade.

As ferramentas devem ser encontradas pelo `PATH` ou configuradas em
`Makefile.local`, que não é versionado. Por padrão, o usuário executa build,
testes e QEMU. O agente só pode executar esses comandos quando o usuário
autorizar explicitamente a execução na conversa; sem essa autorização, o
agente deve apenas revisar o Makefile, os comandos e os artefatos.

---

## Regra #0: Comunicar alterações no boot

É permitido editar, otimizar, reduzir ou modificar `src/boot/boot.asm` quando
isso fizer parte do escopo da tarefa. Antes da alteração, o agente DEVE
comunicar explicitamente ao usuário que o bootloader será modificado,
informando o motivo, o escopo e o impacto esperado. O boot sector possui o
limite rígido de 512 bytes, que deve ser preservado e verificado após a
alteração. A mudança no bootloader não deve ser silenciosa nem omitida da
validação e da documentação da etapa.

---

## Regra #1: Logs de Erros e Eventos Relevantes

Toda falha observável em API pública, inicialização, operação de hardware,
I/O, alocação ou validação DEVE ser registrada em log com a severidade
adequada.

O log deve ser feito pela camada que possui contexto suficiente para explicar
a operação, a causa e o impacto. Não é necessário registrar o mesmo erro em
cada camada: helpers internos podem propagar o erro, deixando o chamador
responsável pelo registro final e evitando duplicidade e excesso de ruído.

Rejeições esperadas, entradas inválidas, recursos opcionais indisponíveis e
fixtures negativas continuam visíveis no contrato de retorno e nos
diagnósticos, mas podem usar `LOG_WARN` ou `LOG_DEBUG` quando não representarem
uma falha inesperada do sistema. Falhas reais devem usar `LOG_ERROR`.

```c
#include "core/log.h"

LOG_INFO("MODULO", "Inicializado com sucesso");
LOG_ERROR("MODULO", "Falha ao ler disco");
LOG_WARN("MODULO", "Memoria baixa, continuando...");
LOG_DEBUG("MODULO", "Variavel x = 5");
```

Tags devem identificar de forma estável o módulo ou subsistema responsável;
esta regra não depende de uma lista fechada de módulos. O registro não deve
expor senhas, tokens, chaves, dados pessoais ou buffers sensíveis.

Em interrupções, hot paths e loops frequentes, o logging não deve bloquear nem
gerar ruído excessivo; quando necessário, registrar de forma limitada,
agrupar eventos ou encaminhar o diagnóstico para processamento posterior.

---

## Regra #2: Contratos e Tratamento de Erros

Toda API pública ou operação nova que possa falhar DEVE declarar claramente
como informa sucesso e falha. O formato deve ser adequado à API:

- operações do kernel e APIs públicas que já usam códigos devem preferir
  `int` com os valores canônicos de `src/include/core/errors.h`;
- funções que retornam ponteiros podem usar `NULL` para falha quando esse for
  o contrato estabelecido;
- funções predicativas podem usar `bool` quando só houver sucesso ou falha
  sem motivo detalhado;
- estruturas de resultado podem ser usadas quando a operação precisar
  retornar dados e estado simultaneamente;
- assinaturas legadas `void` não devem ser alteradas somente para introduzir
  um código de erro; nesses casos, a falha deve ser registrada e o módulo
  deve publicar estado degradado ou indisponível quando aplicável;
- syscalls e fronteiras de ABI devem preservar o contrato numérico e traduzir
  falhas internas para códigos públicos estáveis.

Os códigos canônicos ficam em `src/include/core/errors.h`. Novos códigos só
devem ser adicionados quando forem semanticamente reutilizáveis, documentados
nesse contrato e usados pelos chamadores apropriados; nunca devem ser
redefinidos localmente. Helpers internos podem propagar a falha sem registrar
novamente quando a camada chamadora possuir o contexto final.

Toda falha deve liberar ou transferir corretamente os recursos que a operação
possui, sem vazamentos, uso após liberação ou double free. Assinaturas
públicas existentes devem ser preservadas, atualizando todos os chamadores
quando uma alteração for realmente necessária.

Exemplo para uma operação com código canônico:

```c
int funcao(void) {
    if (!ptr) { LOG_ERROR("MOD", "Ponteiro nulo"); return ERR_NULL; }
    if (falha) { LOG_ERROR("MOD", "Operacao falhou"); return ERR_DISK; }
    return OK;
}
```

Para erros fatais que derrubam o sistema:

```c
LOG_ERROR("MOD", "Erro fatal");
panic("MOD: Erro fatal");
```

---

## Regra #3: Inicialização de Módulos

Toda função `xxx_init()` nova ou modificada DEVE definir claramente o estado
que publica e o contrato para sucesso, degradação e falha.

- Inicializações relevantes devem registrar o início e a conclusão com
  `LOG_INFO`, quando isso ajudar a acompanhar o ciclo de vida do módulo;
- uma inicialização bem-sucedida deve publicar o estado `READY` ou equivalente;
- falhas devem usar a severidade adequada e seguir o contrato da função:
  retornar erro quando recuperáveis, publicar `DEGRADED`/`UNAVAILABLE` quando
  o módulo opcional puder continuar indisponível, ou chamar `panic()` somente
  quando a falha violar uma invariante essencial do sistema;
- funções legadas que retornam `void` não devem ter sua assinatura alterada
  somente por esta regra; nesses casos, a falha deve ser registrada e o estado
  publicado deve deixar a indisponibilidade explícita;
- inicializações idempotentes, chamadas repetidas e wrappers simples não
  devem gerar logs duplicados ou ruído desnecessário; podem usar `LOG_DEBUG`
  ou omitir um novo log quando o estado já estiver publicado;
- dependências devem ser verificadas pela camada que possui contexto para
  decidir se a falha é fatal, recuperável ou apenas uma degradação opcional.

---

## Convenções de Código

- **Nomes de funções**: `modulo_verbo()` → `ata_read_sector()`, `fat12_list_dir()`
- **Nomes de variáveis**: `snake_case` → `sector_count`, `current_pid`
- **Constantes**: `UPPER_SNAKE_CASE` → `MAX_SECTORS`, `BUFFER_SIZE`
- **Funções novas**: preferencialmente até 100 linhas; funções legadas maiores
  devem ser alteradas somente quando houver benefício claro ou necessidade de
  manutenção.
- **Aninhamento novo**: preferencialmente até 4 níveis; exceções devem ser
  simplificadas quando possível, sem criar refatorações artificiais.
- **Sem magic numbers**: usar `#define`
- **Comentários e justificativas**: não adicionar explicações no código-fonte.
  Registrar decisões, invariantes, ABI, layouts, limitações e o "porquê" nos
  documentos técnicos canônicos do módulo. No código, preferir nomes,
  constantes, tipos e funções autoexplicativos. Comentários existentes devem
  ser migrados quando o trecho for alterado, sem limpeza massiva fora do
  escopo. Exceções ficam limitadas a avisos legais ou marcações exigidas pela
  ferramenta, formato ou gerador.

---

## Arquitetura

- **Nativo (kernel)**: Shell, Editor, Media Player, Task Manager, File Manager, Settings, Desktop, WM, Taskbar, System Updater
- **Módulos CLI nativos**: Device Manager, Game Manager, Media Manager, Network Manager (funções via shell)
- **Opcional (App Store)**: TUI dos 4 managers acima, Anti-Virus, PCSista, Developer Tools
- **Formato de pacote**: `.zephyrosapp`

---

## Documentação

- Roadmaps principais: `docs/roadmaps/*.md`
- Ideias e melhorias futuras: `docs/melhorias futuras/*.md`
- Regras detalhadas: `docs/regras.md`
- Roadmap principal: `ROADMAP.md`
- Índice da docs: `docs/indice.md`
- Memória operacional geral de comandos e validações: `docs/qualidade/comandos-operacionais-agente.md`
  (consultar antes de orientar comandos; não armazenar segredos nesse arquivo).
- Política para justificativas fora do código:
  `docs/qualidade/politica-documentacao-codigo.md`.
- Registro cronológico de implementações e validações:
  `docs/qualidade/registro-validacoes.md`.

---

## Regra #4: Organização de Diretórios

Novos arquivos DEVEM respeitar os limites arquiteturais desta estrutura:

```
src/
├── boot/           → Bootloader (ASM)
├── kernel/         → Kernel core (entry, panic, switch)
├── core/           → Serviços centrais (log, string)
├── drivers/        → Drivers de hardware (video, vesa, font, idt, isr, irq, keyboard, mouse, timer, tss, ata, speaker, pci, ac97, uhci, usb_msc)
├── memory/         → Gerenciamento de memória (memory, paging, compress)
├── fs/             → Sistema de arquivos (fat12, fat32, fs, block, storage, file_index, wav, bmp)
├── process/        → Gerenciador de processos
├── thread/         → Gerenciador de threads
├── shell/          → Apps do shell (editor, taskmanager, mediaplayer)
├── filemanager/    → File Manager
├── taskbar/        → Taskbar
├── desktop/        → Desktop
├── settings/       → Settings
├── wm/             → Window Manager
├── icons/          → Sistema de ícones
├── gui/            → Primitivas gráficas 2D (gui.c)
└── include/        → Headers organizados por módulo
    ├── core/       → video.h, panic.h, log.h, keyboard.h, timer.h, memory.h, errors.h, spinlock.h, string.h
    ├── drivers/    → idt.h, ata.h, ac97.h, pci.h, vesa.h, speaker.h, font.h, tss.h, mouse.h, uhci.h, usb_msc.h
    ├── fs/         → fat12.h, fat32.h, fs.h, block.h, storage.h, file_index.h, wav.h, bmp.h
    ├── memory/     → paging.h, compress.h
    ├── process/    → process.h, thread.h
    ├── apps/       → shell.h, editor.h, mediaplayer.h, taskmanager.h
    └── ui/         → taskbar.h, desktop.h, settings.h, wm.h, filemanager.h, icons.h, gui.h
```

### Regras

A árvore acima é uma referência de organização, não uma lista fechada nem uma
estrutura imutável de arquivos. Cada arquivo novo deve ter um proprietário
arquitetural claro e ficar no subsistema que possui sua responsabilidade
principal.

É permitido criar subdiretórios, dividir módulos maiores em vários arquivos e
manter arquivos legados onde já estão quando uma migração não trouxer benefício
claro. Funcionalidades transversais devem ser atribuídas ao subsistema que
coordena seu contrato principal, evitando cópias ou localização arbitrária.
Uma reorganização que atravesse limites de módulos deve ser justificada na
documentação técnica quando não for evidente.

- [ ] Drivers de hardware → `src/drivers/`
- [ ] Serviços do kernel → `src/core/`
- [ ] Apps do shell → `src/shell/`
- [ ] Headers → `src/include/<modulo>/`
- [ ] Todo arquivo novo possui um subsistema proprietário e uma responsabilidade
      principal identificável.
- [ ] Código novo segue o diretório da responsabilidade principal, sem exigir
      uma árvore adicional quando o módulo ainda for pequeno.
- [ ] NÃO misturar drivers com apps
- [ ] NÃO criar arquivos na raiz de `src/`
- [ ] Submódulos novos devem permanecer pequenos quando possível; módulos
      maiores podem ter mais arquivos ou subdiretórios quando isso melhorar a
      separação e a manutenção. Exceções devem ser justificadas na documentação
      técnica.

---

## Regra #5: Headers e Include Guards

Todo `.h` DEVE poder ser incluído várias vezes com segurança e ter um include
guard único, baseado no caminho ou no módulo:

```c
#ifndef MODULO_H
#define MODULO_H

/* Declarações aqui */

#endif
```

### Regras de include

- [ ] Incluir `types.h` quando o header usar tipos definidos nele; a ordem não
      deve ser fixa quando as dependências reais exigirem outra organização.
- [ ] Usar aspas para headers do projeto: `#include "core/log.h"`.
- [ ] Em um `.c`, incluir primeiro o header correspondente, quando existir, para
      detectar dependências ausentes; os demais includes seguem as dependências
      reais e a clareza do módulo.
- [ ] NUNCA incluir `.c` em outro arquivo.
- [ ] Headers DEVEM ser autocontidos e incluir diretamente tudo que precisam
      para compilar, sem depender de includes transitivos.
- [ ] Usar declarações antecipadas quando apenas ponteiros ou referências
      incompletas forem necessários, reduzindo dependências desnecessárias.
- [ ] NÃO incluir headers sem uso direto; dependências devem ser mantidas
      mínimas e justificadas pelo contrato do header.

---

## Regra #6: Convenções de Tipos e Structs

```c
// Nome: snake_case com sufixo _t
typedef struct {
    uint32_t lba;
    uint8_t  sector_count;
    uint8_t* buffer;
} ata_request_t;

// Variáveis: snake_case sem _t
ata_request_t request;

// Funções que operam na struct: modulo_verbo()
int ata_read(ata_request_t* req);
void ata_free(ata_request_t* req);
```

### Regras

- [ ] Tipos novos de domínio usam nomes `snake_case_t` (ex: `process_t`,
      `fat12_entry_t`) e campos novos usam `snake_case`.
- [ ] Structs públicas e tipos compartilhados devem preferir `typedef`; structs
      locais podem ser anônimas quando isso melhorar a clareza.
- [ ] Enums usam um tipo `snake_case_t` e valores `UPPER_SNAKE_CASE` (ex:
      `state_t { STATE_IDLE, STATE_RUNNING }`).
- [ ] Structs recursivas devem usar um nome de tag explícito para permitir
      referências ao próprio tipo.
- [ ] Funções que operam sobre um módulo ou tipo devem preferir o formato
      `modulo_verbo()`; o objeto principal deve ser o primeiro parâmetro quando
      isso tornar a API mais legível.
- [ ] A ordem dos parâmetros deve priorizar entradas antes de saídas quando
      isso for natural; callbacks, APIs externas e contratos existentes podem
      seguir convenções diferentes.
- [ ] Ownership, mutabilidade, validade e responsabilidade de liberação dos
      ponteiros devem ser claros no nome, no contrato ou na documentação.
- [ ] Código legado, ABI pública, callbacks e convenções de terceiros não devem
      ser renomeados ou reordenados apenas para cumprir estilo.
- [ ] Alterações em layout público, ABI, alinhamento ou uso de `packed` exigem
      revisão dos consumidores e documentação da decisão.

---

## Regra #7: Gerenciamento de Memória e Ownership

Toda alocação ou aquisição de recurso deve ter um proprietário claro, um ciclo
de vida definido e o par correto de liberação. O allocator deve ser escolhido
conforme o tipo do recurso, tamanho, alinhamento, duração, contexto de execução
e requisitos de desempenho.

```c
void* ptr = kmalloc(size);
if (!ptr) {
    LOG_ERROR("MOD", "Falha ao alocar memoria");
    return ERR_MEM;
}

/* uso do recurso */

kfree(ptr);
ptr = NULL; /* recomendado enquanto o ponteiro continuar vivo */
```

### Regras

- [ ] Verificar toda indicação de falha do allocator antes de usar o recurso;
      em caminhos fatais, registrar o erro e seguir o contrato de falha do
      módulo.
- [ ] Escolher o par correspondente ao recurso: por exemplo,
      `kmalloc`/`kfree`, cache de objetos, PMM, páginas ou arenas não devem ser
      misturados sem um contrato explícito.
- [ ] Validar tamanho, overflow, alinhamento, quantidade e contexto antes da
      alocação quando esses valores puderem ser inválidos.
- [ ] Toda alocação deve ter um proprietário claramente definido e uma
      liberação, transferência ou retenção documentada em todos os caminhos de
      saída aplicáveis.
- [ ] Não liberar memória estática, global, emprestada ou cuja posse tenha sido
      transferida; `kfree` não deve ser usado em recursos de outro allocator.
- [ ] Nunca usar memória após sua liberação, liberar o mesmo recurso duas vezes
      ou manter aliases ativos sem considerar a validade do objeto.
- [ ] Atribuir `NULL` a ponteiros próprios ainda vivos após a liberação é
      recomendado, mas não deve ser aplicado artificialmente a variáveis que já
      saem de escopo; aliases e handles também devem ser invalidados quando
      necessário.
- [ ] Usar `kmalloc_aligned()` ou outro allocator apropriado somente quando o
      contrato exigir alinhamento específico, como alinhamento de página.
- [ ] Não usar `malloc`/`free` da biblioteca padrão em código freestanding; cada
      recurso obtido deve usar a API de gerenciamento correspondente.
- [ ] Não vazar recursos: cada aquisição deve ter uma liberação ou transferência
      de ownership verificável.

---

## Regra #8: Contrato de um Driver

Drivers são módulos orientados a hardware. A regra define um contrato mínimo,
mas não exige que todos tenham a mesma estrutura interna, o mesmo estado ou a
mesma assinatura de função.

### Requisitos do driver

- [ ] Definir o proprietário, o ciclo de vida e os estados relevantes do
      hardware, como não inicializado, sondando, pronto, degradado,
      indisponível ou em encerramento, quando aplicável.
- [ ] Usar o modelo de estado adequado ao recurso: um estado global, uma
      estrutura de status, um estado por dispositivo ou um estado por
      controlador. Não é obrigatório criar uma variável `initialized` global.
- [ ] Declarar as capacidades, dependências, recursos e limitações do driver
      no contrato público ou na documentação técnica do módulo.
- [ ] Tornar a inicialização idempotente quando isso for compatível com o
      hardware; rejeitar ou tratar explicitamente chamadas fora de ordem.
- [ ] Registrar o início, o sucesso, a falha ou o estado degradado da
      inicialização conforme as Regras #1 e #3.
- [ ] Se a inicialização falhar depois de adquirir recursos, desfazer os
      recursos já adquiridos ou publicar um estado seguro e indisponível.
- [ ] Validar ponteiros, tamanhos, intervalos, handles, capacidades e estado
      somente quando forem pertinentes à operação e ao contrato da API.
- [ ] Escolher o retorno adequado à operação: código de `errors.h`, booleano,
      contagem, resultado estruturado ou `void` com estado publicado. Não
      forçar `int` onde ele não representa corretamente o contrato.
- [ ] Definir ownership e liberação de portas, MMIO, IRQs, DMA, buffers,
      descritores, locks, filas e work items, respeitando a Regra #7.
- [ ] Impedir uso antes do estado pronto, double release, acesso concorrente
      sem sincronização e reconfiguração enquanto o recurso estiver em uso.
- [ ] Respeitar o contexto de execução: não bloquear, alocar dinamicamente,
      aguardar sem limite ou gerar log pesado em IRQ e hot paths; transferir o
      trabalho para contexto adiável quando necessário.
- [ ] Usar timeouts, limites de tentativa, confirmação de estado e tratamento
      de reset quando a operação de hardware puder ficar pendente.
- [ ] Usar `panic` somente para invariantes fatais do kernel; hardware ausente,
      opcional ou indisponível deve resultar em erro, estado degradado ou
      indisponível quando for possível continuar.
- [ ] Manter o caminho de fallback ou o estado degradado quando a arquitetura
      do sistema exigir continuidade sem aquele hardware.
- [ ] Manter o header público no diretório do módulo, com include guard e
      dependências diretas, sem incluir arquivos `.c`.

Drivers legados que já possuem uma função de inicialização `void` devem
preservar sua assinatura, registrar a falha e publicar estado indisponível ou
degradado. Funções públicas legadas também não devem ser remodeladas apenas
para se encaixar neste modelo; alterações de contrato exigem atualizar os
chamadores e a documentação correspondente.

---

## Regra #9: Contrato de um Módulo do Shell

Um módulo do Shell pode ser um comando de terminal, diagnóstico, aplicativo
gráfico, cena hospedada, adaptador de domínio ou operação cooperativa. O
contrato deve se adaptar ao tipo de módulo; não é obrigatório implementar
`modulo_open()`, `modulo_close()`, `modulo_draw()` e
`modulo_handle_key()` em todos os casos.

### Requisitos do módulo Shell

- [ ] Definir a responsabilidade, o estado, o ciclo de vida e a forma de
      integração do módulo, quando esses conceitos forem aplicáveis.
- [ ] Escolher a camada correta: comandos devem passar pelo dispatcher;
      apresentação, entrada, domínio e operações demoradas devem permanecer
      separados conforme a organização atual do Shell.
- [ ] Registrar comandos na tabela única de `src/shell/shell_dispatch.c`, com
      as flags corretas para bloqueio, cena, cancelamento ou demais políticas
      suportadas pelo dispatcher.
- [ ] Manter o adaptador e o handler no arquivo de domínio correspondente;
      não concentrar novos comandos, parsing ou estado de domínio em
      `shell.c`.
- [ ] Usar o fluxo existente de teclado, mouse, IPC, terminal, cenas e
      `shell_runtime`; não inventar callbacks, funções de teclado ou atalhos
      paralelos.
- [ ] Separar parsing, validação de argumentos, apresentação, execução e
      efeitos de domínio; handlers não devem duplicar a política de prompt.
- [ ] Respeitar o modo de interface em uso. Novas interfaces devem priorizar
      Classic, enquanto Simple permanece como fallback conforme a Regra #15.
- [ ] Ao abrir, fechar, suspender ou restaurar uma cena, preservar somente o
      contexto que o módulo realmente assumiu e usar as APIs apropriadas. Não
      chamar `video_clear()` ou `taskbar_draw()` de forma universal.
- [ ] Para operações demoradas, marcar o comando com a política adequada e
      usar `shell_job` ou o mecanismo cooperativo correspondente; não bloquear
      o roteamento de entrada nem deixar o prompt suspenso em caminhos de erro.
- [ ] Definir cancelamento, reentrada e chamadas fora de ordem quando o
      módulo puder receber eventos assíncronos ou ser aberto mais de uma vez.
- [ ] Liberar jobs, buffers, handlers, recursos visuais e estado temporário em
      todos os caminhos de saída aplicáveis, restaurando o contexto anterior
      quando necessário.
- [ ] Tratar falhas e entradas inválidas conforme as Regras #1 e #2,
      informando o usuário pelo canal apropriado e evitando logs duplicados ou
      ruído em operações normais.
- [ ] Preservar as assinaturas públicas de `src/include/apps/shell.h` e usar
      os headers e contratos existentes; alterações de ABI exigem revisão e
      documentação próprias.
- [ ] Oferecer validação por comando Shell, diagnóstico, `health`,
      `regcheck` ou teste determinístico quando a funcionalidade for
      executável ou observável pelo usuário.

### Organizacao atual do Shell

Desde a refatoracao do Shell, nao concentrar novos comandos ou estados de
dominio em `shell.c`:

- `shell.c`: API publica de `shell.h`, roteamento de teclado/mouse/IPC,
  politica de terminal e prompt, e resultados genericos de aplicativos;
- `shell_input.c`: buffer, historico, edicao e scancodes da entrada;
- `shell_dispatch.c`: parsing e tabela unica de comandos;
- `shell_command_utils.c`: helpers internos de argumentos e formatacao;
- `shell_commands_*.c` e `shell_checks.c`: handlers e estados privados por
  dominio;
- `shell_hosted.c`: terminal hospedado e callbacks do Window Manager;
- `shell_job.c`: executor cooperativo de operacoes demoradas;
- `shell_runtime.h`: bridge interno entre os modulos do Shell.

As assinaturas publicas de `src/include/apps/shell.h` devem permanecer
intactas. Novos comandos devem ser adicionados a tabela do dispatcher e ao
adaptador do modulo responsavel, sem duplicar parsing ou mover a politica de
prompt para os handlers.

Não inventar callbacks ou funções de teclado. Antes de criar uma integração,
consultar `src/include/core/keyboard.h` e o dispatcher atual do Shell.

---

## Regra #10: Integração de Build e Makefile

O Makefile deve descrever de forma reproduzível como cada fonte, objeto,
ferramenta e imagem participa dos alvos do projeto. A estrutura exata pode
variar por módulo, fornecedor, fonte gerada ou ferramenta de build, desde que
as dependências permaneçam explícitas e verificáveis.

### Requisitos

- [ ] Ao adicionar ou remover uma fonte, atualizar o grupo de variáveis, a
      regra de compilação e o agregador de objetos ou alvo equivalente usado
      pelo módulo.
- [ ] Usar os compiladores, assemblers, flags, linker e convenções de ABI já
      definidos pelo projeto; não criar uma cadeia paralela sem documentar o
      motivo e o impacto.
- [ ] Usar variáveis do Makefile para caminhos e ferramentas. Configurações
      específicas da máquina devem ficar em `Makefile.local`, que não é
      versionado.
- [ ] Criar diretórios de saída pelo mecanismo compatível com o shell e os
      alvos do projeto; não exigir um comando literal quando outra regra
      equivalente for necessária para um subdiretório, fornecedor ou host.
- [ ] Manter dependências de headers, fontes geradas, bibliotecas, imagens,
      etapas de link, limpeza e alvos de validação quando forem afetados.
- [ ] Garantir que o objeto novo não fique órfão nem seja incluído duas vezes,
      e que a ordem de link e o layout das imagens permaneçam intencionais.
- [ ] Ao integrar outra linguagem, preservar os contratos de ABI, símbolos,
      calling convention, alinhamento, linker e tratamento de erros; a nova
      cadeia não deve substituir silenciosamente o fluxo existente.
- [ ] Não incluir caminhos pessoais, segredos, ferramentas locais ou artefatos
      de build no Makefile versionado.

O agente não executa o build, testes ou QEMU; essa validação segue a Regra
#14. A alteração só deve ser considerada pronta para abrir no QEMU depois que
o usuário confirmar os pré-requisitos operacionais para aquela versão.

---

## Regra #11: Documentação e Contratos

A documentação deve acompanhar as decisões arquiteturais, contratos públicos,
limitações, critérios de validação e o estado real do projeto. Roadmaps,
documentos técnicos e registros cronológicos têm finalidades diferentes e não
devem ser usados como cópias uns dos outros.

### Documentos canônicos

Devem permanecer disponíveis os documentos-base do projeto:

| Arquivo | Conteúdo |
|---------|----------|
| `AGENTS.md` | Regras operacionais para agentes de IA |
| `ROADMAP.md` | Roadmap geral do projeto |
| `docs/indice.md` | Índice da documentação |
| `docs/regras.md` | Regras detalhadas de código |
| `docs/roadmaps/` | Roadmaps de fases e funcionalidades priorizadas |
| `docs/melhorias futuras/` | Propostas ainda não priorizadas |

### Requisitos de documentação

- [ ] Criar ou atualizar um roadmap quando uma feature for priorizada; usar
      `docs/melhorias futuras/` enquanto ela ainda for apenas uma proposta.
- [ ] Organizar roadmaps por escopo, requisitos, fases, checklists,
      limitações, critérios de saída e referências.
- [ ] Registrar decisões, invariantes, ABI, layouts, ownership, limitações e
      justificativas técnicas no documento canônico do módulo, não em
      comentários extensos no código.
- [ ] Atualizar `docs/indice.md` e `ROADMAP.md` somente quando a nova
      documentação alterar o índice ou o roadmap geral.
- [ ] Atualizar `docs/qualidade/contratos-publicos.md` quando um contrato
      público, header, syscall, ABI ou layout compartilhado mudar.
- [ ] Atualizar `docs/qualidade/metricas.md` quando a alteração for uma
      otimização ou quando a decisão depender de comparação mensurável.
- [ ] Registrar implementação, correção, diagnóstico e validação concluídos
      em `docs/qualidade/registro-validacoes.md`, conforme a Regra #18.
- [ ] Registrar em `docs/qualidade/dividas-tecnicas-v1.0.0.md` somente as
      limitações aceitas explicitamente como dívida para a v1.0.0, conforme a
      Regra #20.

---

## Regra #12: Qualidade, Compatibilidade e Validação

- [ ] Não declarar que uma alteração compila, foi testada ou está pronta para
      QEMU sem evidência correspondente ou confirmação do usuário.
- [ ] Para alterações de código, tratar `make q3check` e depois
      `make clean && make` como pré-requisitos para abrir ou testar a versão
      alterada no QEMU e para um commit; depois de confirmados para a mesma
      versão, não reapresentá-los como pendência funcional da etapa.
- [ ] A validação executável pertence ao usuário por padrão; o agente só pode
      executar build, testes ou QEMU após autorização explícita, conforme a
      Regra #14.
- [ ] Revisar warnings novos. Warnings existentes que não puderem ser
      corrigidos na etapa devem ser identificados e documentados, sem
      classificá-los automaticamente como falha nova.
- [ ] Ao adicionar ou alterar um header, verificar dependências diretas,
      include guards e impacto nos consumidores.
- [ ] Ao modificar uma struct, enum, assinatura, layout ou contrato, revisar
      todos os consumidores, chamadores, pontos de serialização e fronteiras
      de ABI afetados.
- [ ] Não alterar uma assinatura ou contrato público sem atualizar os
      chamadores, a documentação e a validação correspondentes.
- [ ] Preservar o comportamento existente fora do escopo e registrar qualquer
      incompatibilidade intencional, migração ou rollback necessário.

---

## Checklist Geral

Antes de entregar uma alteração ou preparar um commit:
1. [ ] Escopo, proprietário, impacto e arquivos envolvidos estão claros?
2. [ ] Contratos de erro, logs e estados seguem as Regras #1, #2 e #3 quando aplicáveis?
3. [ ] Ownership, alinhamento, lifetime e liberação seguem a Regra #7?
4. [ ] Funções novas respeitam preferencialmente 100 linhas e 4 níveis de aninhamento, sem refatoração artificial?
5. [ ] Arquivos, headers e dependências estão organizados conforme as Regras #4 e #5?
6. [ ] Structs, enums, assinaturas, chamadores e fronteiras de ABI foram revisados?
7. [ ] Valores fixos têm contrato ou constante apropriada, sem magic numbers evitáveis?
8. [ ] Makefile e alvos afetados foram atualizados conforme a Regra #10?
9. [ ] A documentação canônica, métricas, contratos públicos e dívidas foram atualizados quando aplicável?
10. [ ] Existe uma validação executável ou observável adequada à funcionalidade?
11. [ ] O usuário recebeu os pré-requisitos de build e validação quando a alteração exige QEMU?
12. [ ] Se o agente executou build, testes ou QEMU, havia autorização explícita
    do usuário conforme a Regra #14?
13. [ ] Warnings novos e limitações conhecidas foram identificados sem confundir estado não validado com sucesso?
14. [ ] `git status --short` e os diffs dos arquivos alterados foram revisados?
15. [ ] Se houver commit, o diff staged e o `git diff --cached --check` foram revisados conforme a Regra #16?
16. [ ] Se houver commit, não existem segredos, dados pessoais, caminhos locais, backups ou artefatos indevidos nos arquivos envolvidos?
17. [ ] A etapa concluída foi registrada em `docs/qualidade/registro-validacoes.md` com horário conforme a Regra #18?

---

## Regra #13: Validação de Novas Funcionalidades

Funcionalidades executáveis ou observáveis voltadas ao usuário DEVEM ter um
caminho de validação adequado: comando Shell, diagnóstico, fluxo de interface,
teste determinístico ou outro mecanismo já existente que permita observar o
resultado e as falhas relevantes.

- [ ] Preferir um comando ou diagnóstico existente quando ele cobrir a
      capacidade; adicionar um novo somente quando isso melhorar a observação,
      execução ou reprodução do comportamento.
- [ ] Quando aplicável, validar sucesso, entrada inválida, falha de recurso,
      cancelamento, limpeza e repetição da operação.
- [ ] Diagnósticos destrutivos ou testes que alterem estado devem declarar esse
      efeito e restaurar os recursos utilizados quando possível.
- [ ] Interfaces gráficas podem usar o fluxo de cena, aplicativo hospedado ou
      matriz de validação correspondente, sem criar um comando artificial.
- [ ] Camadas internas, helpers, callbacks e mudanças somente de infraestrutura
      podem ser validados por `health`, `regcheck`, testes determinísticos ou
      comandos já existentes.

---

## Regra #14: Execução de Build

Por padrão, o agente de IA não executa comandos de build, testes ou QEMU via
terminal (`make`, `make clean`, `make run`, `make test-qemu`, etc.). O usuário
é o responsável por essa validação. Se o usuário autorizar explicitamente a
execução na conversa, o agente poderá executar somente os comandos autorizados,
respeitando os gates, os limites do escopo e as regras de segurança. A
autorização não permite alterar App API, syscalls, ABI, bootloader ou outros
arquivos fora do escopo do comando.

---

## Regra #15: Política de Modos de Interface

- **Simple**: TUI original baseada em `video.c`, preservada como fallback
  operacional para falha de VESA/backbuffer. Recebe correções críticas e
  ajustes necessários para preservar o fallback, mas não é o alvo padrão de
  novas funcionalidades.
- **Classic**: GUI VESA atual baseada em `gui.c`/`gui.h`. É a interface
  principal para novos aplicativos e funcionalidades gráficas e concentra a
  matriz de testes de Desktop, Taskbar, Window Manager e aplicativos hospedados.
- **Modern**: nome reservado para a futura interface realmente moderna. Não
  deve ser oferecido como modo selecionável enquanto essa implementação não
  existir.

- [ ] Novos aplicativos e interfaces gráficas priorizam o modo Classic.
- [ ] Mudanças sem impacto de interface não devem ser forçadas a uma camada
      visual apenas para satisfazer esta regra.
- [ ] O modo Simple só precisa ser validado quando a alteração tocar
      diretamente o fallback, vídeo, teclado ou código compartilhado que possa
      afetá-lo.
- [ ] Alterações de código compartilhado devem preservar a operação segura do
      Simple quando esse caminho continuar sendo necessário.

---

## Regra #16: Verificação de informações antes do commit

Antes de sugerir, criar ou executar qualquer commit, o agente DEVE revisar somente os arquivos
que estão modificados, novos ou staged no Source Control e verificar se não existem informações
sensíveis ou locais, incluindo:

- senhas, tokens, chaves privadas, credenciais, cookies ou arquivos de ambiente;
- e-mails pessoais, nomes de usuário, caminhos locais e dados identificáveis;
- `Makefile.local`, `.mailmap`, backups, dumps, imagens de disco e artefatos de build;
- alterações não relacionadas ao objetivo atual ou arquivos pertencentes ao usuário.

A verificação deve usar `git status --short` e os diffs dos arquivos alterados como fonte de
verdade. Para os arquivos que irão no próximo commit, a revisão principal deve ser feita com
`git diff --cached` e `git diff --cached --check`. Arquivos modificados mas ainda não staged
podem ser revisados para preparar o commit, mas não devem ser tratados como já autorizados.

Arquivos que não aparecem no Source Control não precisam ser verificados novamente. A busca
por segredos também deve ser limitada aos arquivos alterados ou staged, evitando reexaminar o
repositório inteiro sem necessidade. O histórico só deve ser revisado quando a tarefa envolver
limpeza ou reescrita de histórico, remoção de informações pessoais ou alteração de tags.

Se houver dúvida sobre qualquer arquivo modificado ou staged, o agente DEVE parar e informar
o usuário antes do commit.

---

## Regra #17: IDs Operacionais Exatos

Ao orientar qualquer comando que use um ID de dispositivo, disco, volume,
partição ou outro identificador dinâmico, o agente DEVE copiar o ID exatamente
da saída mais recente fornecida pelo usuário ou obtida no sistema. É PROIBIDO
inventar, reconstruir, abreviar, normalizar, trocar separadores ou presumir
sufixos de um ID.

Se o ID não estiver completamente legível ou disponível, o agente DEVE pedir a
saída textual ou uma captura mais clara antes de indicar o comando. Quando
houver vários IDs, o agente DEVE identificar qual linha da saída originou o ID
e repetir o valor literalmente.

---

## Regra #18: Registro de Etapas e Validações

Toda etapa, subetapa, fase, implementação ou validação concluída DEVE registrar
a data e a hora exatas em `docs/qualidade/registro-validacoes.md`.

O roadmap mantém escopo, requisitos, checklists, pendências e critérios de
saída. Mover para o registro os relatos cronológicos de implementação,
correção, diagnóstico, execução e validação, incluindo seus horários e saídas.

O registro DEVE usar o formato ISO 8601 com fuso explícito:

```text
Concluída em: YYYY-MM-DD HH:MM (America/Sao_Paulo)
```

Quando implementação e validação ocorrerem em momentos diferentes, registrar
ambos os horários separadamente. Não inventar nem estimar horários de etapas
históricas; nesses casos, manter o registro sem horário até que o usuário
forneça a informação.

---

## Regra #19: Comandos Completos e Executáveis

Ao orientar o usuário, o agente NUNCA deve enviar um comando incompleto,
genérico ou contendo placeholders como `<tag>`, `<arquivo>`, `...` ou IDs
presumidos. Todo comando deve estar pronto para copiar e executar no shell
indicado, com todos os argumentos obrigatórios, caminhos e valores dinâmicos
preenchidos exatamente.

Se faltar qualquer valor necessário, o agente DEVE primeiro obtê-lo da saída
mais recente do sistema ou pedir ao usuário essa saída. Enquanto o valor não
estiver disponível, deve fornecer apenas a instrução para obtê-lo, não o
comando final incompleto.

---

## Regra #20: Dividas Tecnicas da v1.0.0

Toda limitacao conhecida aceita para permitir o encerramento de uma etapa e
adiada para a v1.0.0 DEVE ser registrada no documento canonico
`docs/qualidade/dividas-tecnicas-v1.0.0.md` antes de marcar a etapa como
concluida.

Cada divida DEVE usar um identificador imutavel e sequencial `DT100-NNN` e
registrar estado, origem, data de aceitacao, impacto, evidencia, roadmap
responsavel, versao limite e criterio reproduzivel de quitacao. O roadmap de
origem e o roadmap responsavel DEVEM referenciar o mesmo identificador.

Uma divida NUNCA deve ser removida, ocultada ou marcada como `QUITADA` apenas
porque o sintoma nao apareceu em uma execucao isolada. A quitacao exige o
criterio de saida atendido e a validacao final registrada, com horario exato,
em `docs/qualidade/registro-validacoes.md`. Dividas quitadas permanecem no
documento como historico.

Funcionalidades ainda planejadas, ideias futuras e itens fora de escopo nao
devem ser classificados como divida tecnica sem aceitacao explicita do
usuario.
