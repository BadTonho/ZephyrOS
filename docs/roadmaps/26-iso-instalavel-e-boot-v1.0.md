# Roadmap 26 - ISO instalavel e boot da versao 1.0.0

## Estado

PENDENTE. Esta frente depende do `PASS` do Roadmap 25 e e a segunda etapa
necessaria para transformar a candidata documental em uma distribuicao
instalavel. O produto permanece em `0.1.0` ate o aceite final.

## Objetivo

Gerar uma ISO bootavel e instalavel, distribuir o artefato com manifesto e
hash, instalar o sistema em um disco vazio e confirmar que o sistema basico
continua funcionando depois de remover a midia de instalacao.

`build/zephyros.img` e uma imagem bruta de disco validada no QEMU. Ela nao e
uma ISO e nao deve ser apenas renomeada para `.iso`. A solucao deve definir
explicitamente o fluxo de boot El Torito/ISO9660 ou o fluxo de instalador que
grava a imagem bruta em um disco de destino.

## Dependencia e decisao de arquitetura

- [ ] Confirmar o contrato de instalacao suportado: BIOS i386, tamanho minimo
  do destino, particionamento, FAT32, recovery e forma de reinstalacao.
- [ ] Preservar o layout e o bootloader da imagem instalada, salvo alteracao
  aprovada e documentada em roadmap proprio.
- [ ] Preferir uma ISO de instalacao que contenha o artefato raw validado e um
  fluxo deterministico para grava-lo no disco de destino.
- [ ] Se a ISO precisar inicializar diretamente o sistema instalado, provar a
  compatibilidade El Torito, setores, LBAs e FAT32 sem sobreposicao.
- [ ] Nao armazenar chaves, segredos ou caminhos pessoais no artefato.

## Escopo de implementacao

- [ ] Gerar `build/zephyros.iso` com tamanho, versao, origem, SHA-256 e
  manifesto reproduziveis.
- [ ] Validar assinatura/assinatura de boot, setor de boot, catalogo El
  Torito, entradas de filesystem, kernel, recovery e imagem de destino.
- [ ] Implementar ou integrar o instalador minimo: selecionar o destino,
  confirmar a operacao, gravar a imagem validada, verificar a escrita e
  reportar falhas sem deixar um disco parcialmente aceito como instalado.
- [ ] Manter uma opcao de recovery/reinstalacao e documentar limites,
  particionamento, rollback e comportamento diante de falta de espaco ou
  interrupcao.
- [ ] Preservar a imagem de origem e permitir repetir a instalacao sem
  depender de estado residual da sessao anterior.

## Testador e relatorio

Criar:

- caso `qemu:iso1:install-boot`;
- caso host `host:iso1:install-boot`;
- ferramenta `tools/iso1_install_boot.py`;
- relatorio `build/test-results/iso1-install-boot/iso1-install-boot.json`;
- schema `zephyros-iso1-install-boot-v1`.

O cenario minimo deve:

1. iniciar pela ISO em uma maquina virtual limpa;
2. confirmar menu, instalador e destino vazio;
3. instalar o sistema e verificar a leitura do artefato gravado;
4. desligar/remover a ISO e reiniciar pelo disco instalado;
5. confirmar `zephyr>`, comandos basicos, armazenamento e entrada;
6. validar recovery/reinstalacao em fixture isolada;
7. repetir a conectividade NET1 depois da instalacao;
8. registrar serial, QMP, input, manifesto, imagem, ISO e hashes.

Boot incompleto, ISO ausente, catalogo invalido, escrita truncada, hash
divergente, prompt ausente, instalacao nao inicializavel, fila residual ou
recovery inseguro produzem `FAIL`. Falta de suporte QEMU produz `BLOCKED`.

## Testes e gates

- [ ] Testes host para layout ISO, El Torito, hashes, manifesto, tamanho,
  destino vazio, escrita truncada, rollback e caminhos pessoais.
- [ ] Testes Python para parser, fases, artefatos, chaves duplicadas,
  envelopes incompletos, `ND`, `PASS`, `FAIL` e `BLOCKED`.
- [ ] Atualizar catalogo, registry, manifesto, indice, comandos operacionais,
  escopo 1.0.0 e registro de validacoes.
- [ ] Executar `make q3check`.
- [ ] Executar `make clean && make`.
- [ ] Executar `make catalog-test`.
- [ ] Executar `make test-iso1-host`.
- [ ] Executar `make test-iso1-qemu`.

## Aceite

ISO1 so sera `PASS` quando a ISO for reproduzivel, inicializar, instalar em
disco vazio, reiniciar sem a midia, apresentar o sistema basico funcional e
passar novamente na validacao NET1. A release `1.0.0` so podera ser declarada
depois de NET1 e ISO1 aprovados para a mesma imagem, commit e conjunto de
artefatos.

