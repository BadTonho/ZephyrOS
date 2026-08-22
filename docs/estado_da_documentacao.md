# Estado da documentacao

## Auditoria atual

**Revisado em 21 de agosto de 2026.** Este documento define como interpretar a
documentacao do ZephyrOS e evita que um plano antigo seja confundido com uma
funcionalidade entregue.

## Fonte de verdade por assunto

| Assunto | Fonte principal | Uso |
|---|---|---|
| Estado global e fases concluidas | [`../ROADMAP.md`](../ROADMAP.md) | Visao resumida do projeto. |
| Ordem dos proximos trabalhos | [`roadmaps/README.md`](roadmaps/README.md) | Planejamento executavel por dependencia. |
| Arquitetura e modulos atuais | [02 - Arquitetura](02-arquitetura/arquitetura.md) | Mapa de componentes e inicializacao. |
| Kernel, memoria, drivers, processos e FS | Capitulos 03 a 08 | Contratos tecnicos atuais. |
| Shell e atalhos | [09 - Shell](09-shell/shell.md) e [Atalhos](atalhos_e_comandos.md) | Comandos e interacoes atuais. |
| Desktop e aplicativos nativos | Capitulos 12 e 13 | Interfaces Simple e Classic atuais; Modern reservado. |
| Atualizacoes do sistema | [14 - Atualizacoes](14-atualizacoes/contrato-zupd-v1.md) | Contratos ZUPD v1, U1 a U5 e System Updater. |
| App Store e pacotes | [App Store](13-aplicativos/app-store.md) e [Pacotes](13-aplicativos/pacotes.md) | Contratos AS1 a AS5 e pacotes ZPKG v1. |
| App API e apps ring 3 | [API de Aplicativos e Syscalls](melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md) | ABI 0.3, ZAPP, foco e limites. |

## Estado tecnico documentado

- Desktop inicia como cena padrao; o terminal e aberto explicitamente pelo
  Desktop, Menu Iniciar ou taskbar.
- Shell possui scrollback de 500 linhas, com renderizacao agrupada de saída
  longa; `clear` apaga tela e historico.
- Desktop, Explorer, Settings, System Updater e App Store oferecem modo Classic
  Modern Dark com fallback Simple. O comando `taskmgr` preserva a TUI de diagnostico.
- App API 0.3, syscalls 0 a 9, `int 0x80` em DPL3, arquivos, IPC, loader ZAPP,
  argumentos simples, pacotes `ZPKG` v1 e foco seguro foram validados no QEMU.
- `echo`, `uptime` e `mem` executam em ring 3 com fallback nativo controlado.
- O subsistema de rede (S2.1 a S2.8) suporta Multi-NIC (E1000 e RTL8139), ARP,
  IPv4, ICMP Echo, UDP, DHCP, DNS, TCP cliente, sockets nativos e HTTP GET.
- Atualizações do sistema (U1 a U5) fornecem verificação Ed25519/SHA-256,
  transação copy-on-write FAT12, journal redundante, recuperação no boot,
  rollback, histórico persistido e distribuição remota HTTP opcional.
- App Store (AS1 a AS5) suporta catálogo local, transações com resolução
  topológica de dependências, rollback, repositório remoto autenticado `ZAC1` e
  janela singleton Classic.
- EP6.1 possui fundacao RTC/UTC ancorada no PIT e contrato TLS policy-only:
  CA estatica, SAN, validade temporal, pin SPKI opcional, rotacao e revogacao
  estao documentados; handshake, X.509 e HTTPS funcional permanecem pendentes
  de etapa posterior; a sequencia host e a matriz especifica da EP6.1 no QEMU
  foram validadas, e a regressao EP6.0/U5 foi preservada conforme a validacao
  anterior da EP6.0.
- Evolução da plataforma (EP1 a EP4.3) suporta preferências de mouse PS/2 em RAM,
  volumes ATA/USB somente-leitura com até 4 montagens simultâneas, índice
  global em RAM com busca e suporte USB UHCI com Mass Storage BOT/SCSI integrado
  à camada de bloco unificada `block_device_t`.

## Como ler os roadmaps antigos

Os arquivos em `melhorias futuras/` sao backlogs detalhados por produto. Eles
podem conter wireframes, estimativas e checklists escritos antes de uma fase
ser implementada. Nao devem, sozinhos, ser usados como prova do estado atual.
Quando houver conflito, a precedencia e:

1. codigo e testes recentes no QEMU;
2. `ROADMAP.md`;
3. `docs/roadmaps/`;
4. capitulos tecnicos atuais;
5. backlog detalhado em `melhorias futuras/`.

Ao terminar uma fase, atualizar o roadmap por etapa, o documento tecnico
afetado, os comandos/atalhos e o `ROADMAP.md`. Isso mantem o historico de
planejamento sem perder a descricao do sistema que realmente existe.

## Documentos revisados nesta auditoria

- Raiz: `README.md`, `ROADMAP.md`, `AGENTS.md` e `lembrar.md`.
- Referencias operacionais: `docs/indice.md`, `docs/regras.md` e
  `docs/atalhos_e_comandos.md`.
- Capitulos 01 a 14, incluindo boot, kernel, drivers, memoria, processos,
  arquivos, Shell, extras, referencias, Desktop, aplicativos e atualizacoes.
- Qualidade: `docs/qualidade/contratos-publicos.md` e `docs/qualidade/metricas.md`.
- Todos os roadmaps em `docs/melhorias futuras/` e `docs/roadmaps/` (01 a 16).
