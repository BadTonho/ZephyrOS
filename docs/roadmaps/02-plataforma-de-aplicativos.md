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

## Fase 6D - Contrato de console e ciclo de vida

- [ ] Definir como aplicativos poderao produzir saidas maiores sem saturar
  filas ou poluir a interface do Shell.
- [ ] Avaliar historico de comandos e entrada de linha somente se houver caso
  de uso concreto.
- [ ] Definir convencao estavel para codigo de saida e mensagens finais de
  aplicativos externos.
- [ ] Nao adicionar syscalls sem uma necessidade comprovada por app migrado.

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
