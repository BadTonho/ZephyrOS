# Supervisor contínuo de testes

## Objetivo

Executar o testador do ZephyrOS em um computador separado por longos períodos,
registrando falhas, travamentos, timeouts e artefatos sem depender de ação
manual a cada execução.

Esta proposta aproveita a infraestrutura da TST7. O supervisor fica no host e
coordena novas execuções; os testes individuais continuam isolados, limitados
por timeout e sem retry automático.

## Arquitetura

```text
supervisor no host
    |
    +-- inicia um ciclo TST7
    |       |
    |       +-- executa cada caso em processo separado
    |       +-- usa snapshot isolado no QEMU
    |       +-- aplica timeout e limite de recursos
    |       +-- preserva os artefatos
    |
    +-- classifica PASS, FAIL ou BLOCKED
    +-- atualiza o índice de execuções
    +-- aguarda o intervalo configurado
    +-- inicia o próximo ciclo
```

O loop contínuo existe somente no supervisor. Um caso que travar não pode
prender o ciclo inteiro: o processo do caso deve ser encerrado pelo watchdog,
seus logs devem ser preservados e o ciclo deve continuar nos casos seguintes.

## Modos de execução

- `quick`: gates host-only e verificações rápidas após alterações.
- `full`: ciclo completo da TST7, incluindo catálogo e casos QEMU.
- `soak`: ciclos determinísticos e limitados de estresse, principalmente os
  casos da TST6.

Cada ciclo terá um identificador próprio, seed reproduzível e um limite claro
de duração. O modo contínuo pode repetir ciclos, mas nunca repete
automaticamente o mesmo caso dentro do ciclo após uma falha.

## Artefatos

Cada execução deve manter um diretório próprio, sem sobrescrever execuções
anteriores:

```text
.tst7-results/<run-id>/
  manifest.json
  result.json
  coverage.json
  summary.md
  stdout.log
  stderr.log
  artifact-index.json
  cases/<case-id>/
```

O índice global deve apontar para cada ciclo, com status, commit, seed, início,
fim, duração, primeiro erro e diretório dos artefatos. Uma política de retenção
pode remover execuções antigas somente quando isso for explicitamente
configurado; a execução mais recente e todas as falhas devem ser preservadas.

## Diagnóstico de falhas

O supervisor deve distinguir:

- `FAIL`: falha observada no kernel, no teste ou em um contrato esperado;
- `TIMEOUT`: caso ou ciclo excedeu seu limite;
- `BLOCKED`: dependência, QEMU, fixture ou capacidade obrigatória ausente;
- `PASS`: execução concluída conforme o contrato.

Cada falha deve registrar o caso, fase, código, evento QMP quando existir,
último evento ZTEST, seed, commit e os logs serial/QEMU. Falhas iguais em
execuções diferentes podem ser agrupadas por uma assinatura estável, mas o
artefato original nunca deve ser descartado.

O baseline aprovado da TST7 não pode ser alterado automaticamente. Uma falha
nunca pode ser escondida por atualização do baseline, supressão de warning ou
repetição silenciosa do caso.

## Isolamento e segurança

- Cada execução QEMU usará snapshot.
- A rede permanecerá isolada ou em backend sem encaminhamento externo.
- Nenhum teste poderá escrever em armazenamento real do host.
- Reboot e poweroff ocorrerão somente dentro do processo QEMU isolado.
- O supervisor terá watchdog para o caso, para o ciclo e para sua própria
  atividade.
- O supervisor deverá detectar falta de espaço, QEMU residual e processos
  órfãos antes de iniciar outro ciclo.

O computador dedicado deve ter a toolchain, Python, QEMU e espaço em disco
configurados. Hardware físico não será considerado validado pelos ciclos QEMU.

## Aplicativos necessários

Para o ambiente atual do projeto no Windows, o computador dedicado precisa ter:

- Git, para obter o repositório e identificar o commit testado;
- Python 3, para executar os runners, geradores de fixtures e validações;
- GNU Make, disponível como `make` no `PATH`;
- NASM, disponível como `nasm` no `PATH`;
- cross-compiler C freestanding para o kernel: `i686-elf-gcc` e
  `i686-elf-ld`;
- QEMU com o executável `qemu-system-i386`;
- compilador C nativo (`cc`, GCC ou Clang) para os testes host-only;
- Clang/LLVM e os runtimes ASan/UBSan para `make test-tst3-sanitize`;
- `pip` e o pacote `cryptography` exigido pelas ferramentas de atualização.

O `GCC` usado para compilar o kernel não substitui o compilador C nativo dos
testes host-only. São toolchains diferentes: o primeiro gera código
freestanding para i386; o segundo gera e executa testes no sistema
operacional do computador dedicado.

Não é necessário instalar um servidor, banco de dados, Docker ou conexão de
rede externa para executar a matriz QEMU. A rede dos casos deve continuar
isolada conforme o perfil do teste.

## Requisitos do projeto

O computador dedicado deve possuir:

- uma cópia completa do repositório, incluindo `vendor/`, `tests/` e
  `docs/fixtures/`;
- permissão para iniciar e encerrar processos filhos, incluindo QEMU;
- permissão de leitura do código e de escrita em `build/` e `.tst7-results/`;
- espaço em disco suficiente para preservar vários ciclos e seus logs;
- execução sem suspensão ou hibernação durante o período de soak;
- relógio do sistema correto para ordenar os artefatos e os relatórios;
- uma instalação local das ferramentas, sem depender de caminhos pessoais de
  outro computador.

No Windows, as ferramentas podem ser instaladas pelo MSYS2/UCRT64 ou por
instaladores equivalentes, desde que sejam compatíveis com o `Makefile` e
estejam no `PATH`. Caminhos específicos da máquina devem ficar somente em
`Makefile.local`, que não deve ser versionado.

Uma configuração local mínima pode ser:

```text
HOST_CC=cc
HOST_SANITIZE_CC=clang
GCC=i686-elf-gcc
LD=i686-elf-ld
NASM=nasm
QEMU=qemu-system-i386
```

Antes de deixar o supervisor em execução permanente, validar as ferramentas
com os gates existentes:

```text
make test-tst7-host
make test-tst7-quick
make test-tst7-full
```

Se uma dependência obrigatória estiver ausente, o resultado deve ser
`BLOCKED`. Não se deve substituir silenciosamente o cross-compiler por um
compilador host, ignorar o runtime sanitizador ou iniciar ciclos QEMU sem a
imagem construída.

## Primeiro MVP

O primeiro executor pode ser um programa Python no host que:

1. inicia `make test-tst7-full`;
2. cria um diretório de execução único;
3. coleta o resultado e os artefatos produzidos pela TST7;
4. atualiza o índice e um arquivo `latest.json`;
5. registra `PASS`, `FAIL`, `TIMEOUT` ou `BLOCKED`;
6. espera o intervalo configurado;
7. inicia o próximo ciclo.

O MVP deve aceitar um limite de ciclos para validação e exigir uma opção
explícita para execução permanente. Mesmo no modo permanente, cada subprocesso
terá timeout máximo e o supervisor deverá ser interrompido de forma segura.

## Evoluções possíveis

- rotação configurável de artefatos;
- resumo HTML ou dashboard local;
- alerta por arquivo, e-mail ou outro canal externo;
- execução automática ao iniciar o computador;
- comparação histórica de duração, warnings e frequência de falhas;
- seleção de uma suíte específica para reproduzir uma assinatura de falha.

Esta documentação descreve a proposta. A implementação do supervisor contínuo
será uma etapa posterior à infraestrutura TST7 já existente.
