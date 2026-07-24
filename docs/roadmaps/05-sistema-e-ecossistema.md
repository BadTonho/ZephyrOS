# Roadmap 05 - Sistema e ecossistema

## Objetivo

Planejar recursos de plataforma que dependem de uma base estavel: dispositivos,
energia, rede, atualizacoes e aplicativos opcionais. Esta frente nao deve
antecipar interfaces ou permissoes que ainda nao existem.

## Ordem de dependencia

1. Confiabilidade, diagnostico e contratos de kernel.
2. Plataforma de aplicativos e formato de pacote.
3. Servicos de dispositivo, energia e rede.
4. Atualizacao segura e distribuicao de aplicativos.
5. Ferramentas produtivas, multimidia e jogos.

## Etapa S1 - Servicos de sistema

- [ ] Gerenciador de dispositivos com inventario e erros controlados.
- [ ] Gerenciador de energia com estados claros e sem desligar recursos em uso.
- [ ] Evolucao do filesystem somente quando novos recursos exigirem metadados
  ou operacoes inexistentes.

## Etapa S2 - Rede e atualizacoes

- [ ] Definir primeiro a arquitetura de rede e conexao; nao criar comandos
  que finjam conectividade antes de haver driver e protocolo.
- [ ] Planejar atualizacao assinada ou verificada somente apos existir formato
  de pacote e politica de integridade.
- [ ] Manter operacoes remotas opcionalmente desabilitadas e visiveis em
  `health` quando indisponiveis.

## Etapa S3 - Ecossistema de aplicativos

- [ ] Usar `.zephyrosapp` como base para instalacao e catalogo de aplicativos.
- [ ] Evoluir Media Manager, Game Manager, ferramentas de desenvolvedor,
  PCSista e Anti-Virus somente sobre APIs ja estabelecidas.
- [ ] Tratar cada aplicativo opcional como modulo com diagnostico, fallback e
  documentacao propria.

## Criterio de saida

Nenhum servico opcional deve impedir boot, Shell, diagnostico ou uso local do
sistema quando seu hardware, arquivo, rede ou pacote nao estiver disponivel.
