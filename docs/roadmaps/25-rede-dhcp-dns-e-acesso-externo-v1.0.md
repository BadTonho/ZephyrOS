# Roadmap 25 - DHCP, DNS e acesso externo da versao 1.0.0

## Estado

IMPLEMENTADO; PASS. Esta frente vem antes da geracao da ISO e
nao altera a versao do produto: o build continua em `0.1.0` ate que todos os
criterios de aceite da 1.0.0 sejam aprovados.

## Objetivo

Comprovar que a rede do ZephyrOS funciona fora dos perfis QEMU isolados,
obtendo configuracao por DHCP, resolvendo nomes por DNS e estabelecendo uma
conexao externa observavel. A validacao deve usar a mesma imagem candidata e
deve distinguir claramente rede disponivel, rede restrita e ausencia de NIC.

Esta frente reutiliza os drivers e a pilha existentes. Nao cria syscall, ABI,
formato de pacote ou protocolo proprietario novo.

## Escopo de implementacao

- [x] Publicar o estado da interface Ethernet, link, endereco IPv4, mascara,
  gateway, lease DHCP, servidor DNS, ultimo erro e capacidade observada.
- [x] Reutilizar e observar o fluxo DHCP existente: discover, offer,
  request, ack, timeout, retry, renovacao e perda de lease.
- [x] Reutilizar e observar o fluxo DNS existente: consulta UDP,
  parsing, timeout, retry, resposta negativa e cache limitado.
- [x] Garantir, por meio dos comandos e do runner, que falhas de DHCP ou DNS retornem erro visivel ao Shell e nao
  deixem jobs, sockets, buffers ou filas residuais.
- [x] Validar conexao TCP externa e uma requisicao HTTP ou HTTPS observavel,
  com timeout e cancelamento deterministas.
- [x] Manter fallback claro para ausencia de NIC, DHCP indisponivel, DNS
  indisponivel e rede restrita; nenhum desses estados pode ser apresentado
  como Internet funcionando.
- [x] Preservar o caminho offline/local do updater e nao baixar artefatos sem
  verificacao de assinatura, hash, tamanho e compatibilidade.

## Implementacao NET1

`tools/net1_external_connectivity.py` executa 12 sessoes em paralelo por
padrao com quatro workers: seis externas, tres restritas e tres sem NIC. O
destino e configuravel por `NET1_EXTERNAL_HOST` e `NET1_EXTERNAL_URL`, sem
credenciais; a rede positiva usa `user,model=e1000` e o perfil restrito usa
`restrict=on`. O `kmetrics machine` agora publica estados e contadores de
DHCP, IPv4, DNS, TCP e HTTP como `ZMETRIC/1`, preservando `ND` para getters
indisponiveis e deltas com wraparound.

O relatorio e os artefatos ficam em
`build/test-results/net1-external-connectivity/`. A implementacao nao altera
ABI, syscalls, bootloader, formato da imagem ou a versao `0.1.0`.

## Contratos de rede

O caso positivo deve usar uma rede QEMU user-mode sem `restrict=on`, ou uma
interface fisica explicitamente autorizada pelo usuario. O caso negativo com
`restrict=on` continua obrigatorio para provar que o sistema identifica a
restricao, mas nao conta como acesso externo.

O teste nao deve depender de um servidor ZephyrOS privado. O destino externo,
o metodo de resolucao, o endereco obtido, o horario, o timeout e o resultado
devem ser registrados no relatorio. Credenciais, tokens e dados pessoais nunca
podem ser gravados nos artefatos.

## Testador e relatorio

Criar:

- caso `qemu:net1:external-connectivity`;
- caso host `host:net1:external-connectivity`;
- ferramenta `tools/net1_external_connectivity.py`;
- relatorio `build/test-results/net1-external-connectivity/net1-external-connectivity.json`;
- schema `zephyros-net1-external-connectivity-v1`.

Cada sessao deve registrar boot, link, DHCP, DNS, conexao externa,
cancelamento/timeout e estado final do prompt. O relatorio deve conter imagem,
SHA-256, perfil, modo, iteracao, endereco/rota/DNS observados, destino de
teste, latencias observaveis, logs serial/QMP e status `PASS`, `FAIL`,
`BLOCKED` ou `ND` por recurso.

Falha de suporte QEMU e `BLOCKED`; ausencia de uma metric obrigatoria, erro de
protocolo, timeout nao tratado, prompt ausente, socket residual ou conexao
externa nao comprovada no caso positivo e `FAIL`. `ND` e permitido somente
para amostras host opcionais ou capacidades explicitamente indisponiveis.

## Testes e gates

- [x] Testes host para DHCP, DNS, parsing, timeout, retry, lease, cache,
  sockets, buffers, cancelamento e estados sem NIC.
- [x] Testes Python para parser, relatorio, destino, rede restrita, `ND`,
  chaves duplicadas, envelope incompleto e ausencia de credenciais.
- [x] Atualizar catalogo, registry, manifesto, comandos operacionais,
  metricas e registro de validacoes.
- [x] Executar `make q3check` — PASS; permanece apenas o aviso aceito
  DT100-003 da fixture AS5 sem assinatura.
- [x] Executar `make clean && make` — PASS; imagem de 268435456 bytes
  regenerada.
- [x] Executar `make catalog-test` — PASS; 7893 superfícies e 208 casos.
- [x] Executar `make test-net1-host` — PASS; casos de rede e 37 testes Python.
- [x] Executar a matriz `make test-net1-qemu` com a rede autorizada —
  PASS fora do sandbox de execucao: 12/12 sessoes aprovadas, incluindo 6
  externas, 3 restritas e 3 sem NIC.

## Aceite

NET1 so sera `PASS` quando a mesma imagem candidata obtiver lease DHCP,
resolver um nome por DNS e concluir uma conexao TCP/HTTP ou HTTPS externa,
com prompt restaurado e sem residuos. O resultado restrito deve continuar
visivel como isolamento, nao como sucesso de Internet.

Depois do `PASS`, o resultado sera dependencia obrigatoria do Roadmap 26.

## Resultado da validacao NET1

O relatório atual está em
`build/test-results/net1-external-connectivity/net1-external-connectivity.json`.
As lanes `external/Simple`, `external/Classic`, `restricted/Simple` e
`no-nic/Simple` passaram integralmente: 12/12 sessoes aprovadas. As seis
sessoes externas obtiveram DHCP automatico, resolveram `example.com` por DNS
e concluiram HTTP; as lanes restrita e sem NIC confirmaram os fallbacks sem
residuos. O relatorio registra `credentials_stored=false`, prompt restaurado,
filas/socket/buffers drenados e a mesma imagem usada na matriz.

A primeira execucao dentro do sandbox falhou porque o processo QEMU nao tinha
permissao de acessar a rede externa. A repeticao autorizada fora do sandbox
passou sem alteracao produtiva; o runner e a pilha do guest permanecem
inalterados. O Roadmap 26 pode iniciar a validacao da ISO.
