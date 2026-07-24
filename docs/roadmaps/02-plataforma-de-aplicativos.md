# Roadmap 02 - Plataforma de aplicativos

## Objetivo

Evoluir aplicativos externos sem migrar funcionalidades nativas de forma
abrupta. Cada migracao preserva um fallback nativo e usa a App API como
contrato publico.

## Estado validado

- [x] App API `0.3` e ABI de lancamento com ate 8 argumentos simples.
- [x] Syscalls `0-9`, `int 0x80` em DPL3 e marshalling de memoria de usuario.
- [x] Handles de arquivo, IPC e entrada de teclado por mensagem.
- [x] Processo ring 3 isolado, `usertest`, falha isolada e `process_exit`.
- [x] Loader de imagens flat i386 `ZAPP` com extensao `.ZAP` no FAT12.
- [x] Foco exclusivo, cancelamento com `F12` e retorno seguro ao Shell.
- [x] Argumentos simples e primeira migracao: `echo` em ring 3 com fallback.
- [x] Migracoes adicionais: `uptime` e `mem` em ring 3 com fallback nativo.
- [x] Contrato de console sincrono e ciclo de vida de saida, com diagnostico
  `app outputtest [fail]`.

## Fase 6C - Migracao gradual de comandos CLI ✅

- [x] Selecionar somente comandos cujo servico ja exista na App API.
- [x] Migrar `uptime` e `mem`, pois dependem apenas de consultas ja expostas
  e produzem texto.
- [x] Executar cada comando em uma imagem interna ZAPP.
- [x] Preservar a versao nativa como fallback quando a plataforma estiver
  indisponivel.
- [x] Garantir saida unica, foco devolvido e ausencia de zumbis em cada
  comando migrado.
- [x] Expandir `appcheck` com cenarios positivos e negativos de cada migracao.
- [x] Validar no QEMU os dois comandos, `appcheck`, `health`, `echo`,
  `usertest`, falha isolada e cancelamento por `F12`.

## Fase 6D - Contrato de console e ciclo de vida ✅

- [x] Formalizar `console_write` como escrita sincrona de 1 a 1024 bytes;
  blocos consecutivos sao permitidos, nao usam filas nem possuem quota total.
- [x] Adicionar `app outputtest [fail]`, que emite nove blocos ASCII de 128
  bytes e encerra com codigo `0` ou `1`.
- [x] Reservar `0xF120` ao runtime e tratar termino normal com codigo nao-zero
  como `ERRO`, sem confundir com falha isolada ou cancelamento por `F12`.
- [x] Manter historico de comandos e entrada de linha fora da App API ate
  existir caso de uso concreto; nao adicionar syscall nesta fase.
- [x] Validar no QEMU os caminhos de sucesso e erro de `app outputtest`,
  argumento invalido, `appcheck`, foco por `Enter` e `F12`, falha isolada,
  `health`, migracoes e ausencia de processos de usuario residuais.

## Fase 7 - Pacotes e distribuicao

- [ ] Criar empacotador no host; ele nao deve rodar dentro do kernel.
- [ ] Definir `.zephyrosapp` com manifesto, versao, arquitetura e integridade.
- [ ] Validar instalacao, remocao e leitura de pacote sem expor estruturas FAT.
- [ ] Adicionar diagnostico para pacote invalido, dependencia ausente e espaco
  insuficiente.

## Fora desta frente por enquanto

- Migrar Explorer, Settings, Task Manager, Desktop ou Window Manager.
- Criar API grafica, mouse para aplicativos externos ou janelas de apps.
- ELF, relocacao, bibliotecas dinamicas, permissoes complexas ou rede.

## Criterio de saida da Fase 6C ✅

Pelo menos duas ferramentas CLI simples executam em ring 3, mantem fallback
nativo, nao repetem output ou prompt e continuam seguras quando loader,
filesystem, paging ou modo usuario falham.
