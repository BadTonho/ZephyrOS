# Estado da documentacao

## Auditoria atual

**Revisado em 27 de agosto de 2026.** Este documento define como interpretar a
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
| App API e apps ring 3 | [API de Aplicativos e Syscalls](melhorias%20futuras/api%20de%20aplicativos%20e%20syscalls.md) | ABI 0.8, VFS, devfs, pipes, redirecionamento, ioctl, cwd, ZAPP, sinais, foco e limites. |

## Estado tecnico documentado

- Desktop inicia como cena padrao; o terminal e aberto explicitamente pelo
  Desktop, Menu Iniciar ou taskbar.
- Shell possui scrollback de 500 linhas, com renderizacao agrupada de saída
  longa; `clear` apaga tela e historico.
- Desktop, Explorer, Settings, System Updater e App Store oferecem modo Classic
  Modern Dark com fallback Simple. O comando `taskmgr` preserva a TUI de diagnostico.
- A base App API 0.4, VFS1/App API 0.5 e VFS2/App API 0.6 foram validadas. A
  VFS3 publica a App API 0.7, devfs, listagem universal e syscall 17 e foi
  validada no QEMU padrão, USB HID e USB MSC compacto. A VFS4 acrescenta
  pipes, redirecionamento, App API 0.8 e syscall 18; sua validação QEMU
  específica permanece pendente.
- `echo`, `uptime` e `mem` executam em ring 3 com fallback nativo controlado.
- O subsistema de rede (S2.1 a S2.8) suporta Multi-NIC (E1000 e RTL8139), ARP,
  IPv4, ICMP Echo, UDP, DHCP, DNS, TCP cliente, sockets nativos e HTTP GET.
- Atualizações do sistema (U1 a U5) fornecem verificação Ed25519/SHA-256,
  transação copy-on-write FAT12, journal redundante, recuperação no boot,
  rollback, histórico persistido e distribuição remota HTTP opcional.
- App Store (AS1 a AS5) suporta catálogo local, transações com resolução
  topológica de dependências, rollback, repositório remoto autenticado `ZAC1` e
  janela singleton Classic.
- EP6.1 possui fundacao RTC/UTC ancorada no PIT e EP6.2 acrescenta BearSSL
  0.6, TLS 1.2, X.509, SNI, SAN, trust anchor estatico, RDRAND e HTTPS GitHub
  configuravel. A descoberta por tag, fingerprint de preflight, release.json,
  cache U5 e separacao de `update apply` estao implementados; a validacao
  executavel da EP6.2 ainda deve ser registrada depois do gate host e da matriz
  QEMU pelo usuario.
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
