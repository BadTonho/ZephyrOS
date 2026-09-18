# Roadmap 25 - DHCP, DNS e acesso externo da versao 1.0.0

## Estado

PLANEJADO. Esta frente vem antes da geracao da ISO e nao altera a versao do
produto: o build continua em `0.1.0` ate que todos os criterios de aceite da
1.0.0 sejam aprovados.

## Objetivo

Comprovar que a rede do ZephyrOS funciona fora dos perfis QEMU isolados,
obtendo configuracao por DHCP, resolvendo nomes por DNS e estabelecendo uma
conexao externa observavel. A validacao deve usar a mesma imagem candidata e
deve distinguir claramente rede disponivel, rede restrita e ausencia de NIC.

Esta frente reutiliza os drivers e a pilha existentes. Nao cria syscall, ABI,
formato de pacote ou protocolo proprietario novo.

## Escopo de implementacao

- [ ] Publicar o estado da interface Ethernet, link, endereco IPv4, mascara,
  gateway, lease DHCP, servidor DNS, ultimo erro e capacidade observada.
- [ ] Completar ou corrigir somente o fluxo DHCP existente: discover, offer,
  request, ack, timeout, retry, renovacao e perda de lease.
- [ ] Completar ou corrigir somente o fluxo DNS existente: consulta UDP,
  parsing, timeout, retry, resposta negativa e cache limitado.
- [ ] Garantir que falhas de DHCP ou DNS retornem erro visivel ao Shell e nao
  deixem jobs, sockets, buffers ou filas residuais.
- [ ] Validar conexao TCP externa e uma requisicao HTTP ou HTTPS observavel,
  com timeout e cancelamento deterministas.
- [ ] Manter fallback claro para ausencia de NIC, DHCP indisponivel, DNS
  indisponivel e rede restrita; nenhum desses estados pode ser apresentado
  como Internet funcionando.
- [ ] Preservar o caminho offline/local do updater e nao baixar artefatos sem
  verificacao de assinatura, hash, tamanho e compatibilidade.

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

- [ ] Testes host para DHCP, DNS, parsing, timeout, retry, lease, cache,
  sockets, buffers, cancelamento e estados sem NIC.
- [ ] Testes Python para parser, relatorio, destino, rede restrita, `ND`,
  chaves duplicadas, envelope incompleto e ausencia de credenciais.
- [ ] Atualizar catalogo, registry, manifesto, comandos operacionais,
  metricas e registro de validacoes.
- [ ] Executar `make q3check`.
- [ ] Executar `make clean && make`.
- [ ] Executar `make catalog-test`.
- [ ] Executar `make test-net1-host`.
- [ ] Executar a matriz `make test-net1-qemu` com a rede autorizada.

## Aceite

NET1 so sera `PASS` quando a mesma imagem candidata obtiver lease DHCP,
resolver um nome por DNS e concluir uma conexao TCP/HTTP ou HTTPS externa,
com prompt restaurado e sem residuos. O resultado restrito deve continuar
visivel como isolamento, nao como sucesso de Internet.

Depois do `PASS`, o resultado sera dependencia obrigatoria do Roadmap 26.

