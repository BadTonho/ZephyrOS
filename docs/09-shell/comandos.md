# Comandos do Shell

Este documento lista e detalha todos os comandos disponíveis no shell (terminal) interativo do ZephyrOS.

> **Nota:** Consulte também o documento unificado [Atalhos e Comandos do Sistema](../atalhos_e_comandos.md) para ver a lista de atalhos de teclado de todos os aplicativos.

## `help`
Mostra a lista de todos os comandos.

```
zephyr> help
Comandos disponiveis:
  help      - Mostra esta mensagem
  clear     - Limpa tela e historico do terminal
  desktop   - Abre a area de trabalho
  settings  - Abre o painel de configuracoes
  wm        - Abre gerenciador de janelas
  ls        - Lista arquivos
  cat       - Exibe conteudo de arquivo
  echo      - Exibe texto
  mem       - Mostra informacoes de memoria
  procs     - Mostra processos ativos
  threads   - Mostra threads ativas
  threadtest - Valida troca cooperativa de threads
  uptime    - Mostra tempo ligado
  beep      - Toca um beep (freq duracao_ms)
  melody    - Toca uma melodia
  explorer  - Abre o gerenciador de arquivos
  taskmgr   - Abre o gerenciador de tarefas
  taskcfg   - Configura a barra de tarefas
  compress  - Liga/desliga compressao de RAM
  stats     - Mostra estatisticas de compressao
  play      - Toca arquivo WAV
  view      - Exibe imagem BMP
  stop      - Para player de midia
  edit      - Editor de texto (edit ARQUIVO.TXT)
  mouse     - Mostra status do mouse PS/2
  guitest   - Testa primitivas GUI 2D
  guimode   - Altera entre gui classica (TUI) e moderna
  health [summary] - Exibe estado completo ou resumo compacto
  devices   - Lista o inventario de hardware (`-v` inclui detalhes)
  device-info <id> - Exibe detalhes de um dispositivo inventariado
  device-scan - Refaz somente a varredura PCI e atualiza o inventario
  net status - Exibe o estado observavel da rede
  net devices - Lista controladores de rede PCI
  net info <id> - Exibe detalhes de uma interface inventariada
  net ethernet <id> - Inspeciona recepcao Ethernet L2
  net test <id> - Envia um frame Ethernet de diagnostico
  net arp config <id> <ip> - Configura interface e IPv4 em RAM
  net arp status|table|clear - Inspeciona ou limpa o cache ARP
  net arp resolve <ip> - Resolve IPv4 para MAC sem bloquear
  net ipv4 config <id> <ip> <mask> <gw> - Configura IPv4 em RAM
  net ipv4 status - Inspeciona IPv4 e ICMP
  net udp status - Inspeciona endpoints e datagramas
  net dhcp acquire <id>|status|renew|release - Gerencia o lease
  net dns config <ip>|status|table|clear - Gerencia DNS
  net tcp status|connect <host> <porta> - Inspeciona/testa TCP
  net socket status|table - Inspeciona sockets nativos
  http get <url>|status - Executa e inspeciona HTTP GET
  nslookup <dominio> - Resolve um registro A
  ping <ip-ou-dominio> [1-10] - Executa ICMP Echo cooperativo
  net check [id] - Agrupa os diagnosticos de rede
  net check qemu <id> <ip> - Executa a suite de rede no QEMU
  net check qemu dhcp <id> <dominio> - Executa a suite S2.6
  net check qemu tcp <id> <dominio> - Executa a suite S2.7
  acpi status - Exibe tabelas, PM1, modo ACPI e `_S5_`
  power status - Mostra prontidao ACPI S5 e fallback HLT
  kmetrics  - Mostra linha-base manual de metricas do kernel
  memcheck  - Valida heap, PMM e diretorios de usuario
  schedcheck - Valida invariantes do scheduler
  q2check   - Executa diagnostico compacto da Q2
  regcheck [full] - Executa regressao compacta com F12
  appcheck  - Testa API, arquivos, IPC e carregador ZAPP
  pkg       - Gerencia pacotes .ZPK locais
  pkgcheck  - Testa validacoes de pacote sem gravar
  update verify <arquivo.ZUP> - Verifica atualizacao sem gravar
  app run <arquivo.ZAP> [args] - Executa aplicativo ring 3 de forma assincrona
  app inputtest - Testa entrada de teclado em aplicativo ring 3
  app outputtest [fail] - Testa saida ZAPP em blocos e codigos de saida
  app argtest <texto> - Testa argumentos em aplicativo ring 3
  usertest  - Executa teste isolado em ring 3
  reboot    - Reinicia o sistema
  shutdown  - Desliga por ACPI ou usa fallback HLT
```

## `clear`
Limpa a tela e apaga todo o historico do terminal atual.

## Historico rolavel do Shell

O Shell preserva as ultimas 200 linhas de saida textual. A rolagem funciona
somente quando nenhuma aplicacao esta com foco:

- `Seta para cima` / `Seta para baixo`: navega uma linha;
- `Page Up` / `Page Down`: navega uma pagina;
- `Home` / `End`: vai ao inicio ou ao fim do historico.

Ao digitar, apagar ou enviar um comando, o Shell volta automaticamente ao fim
do historico para manter o prompt visivel. O comando `clear` remove tambem as
linhas armazenadas.

## `ls`
Lista todos os arquivos no disco FAT12.

```
zephyr> ls
Arquivos no disco:
  ARQUIVO.TXT  128 bytes
  DADOS.DAT    256 bytes
```

## `cat <arquivo>`
Exibe o conteúdo de um arquivo de texto.

```
zephyr> cat ARQUIVO.TXT
Olá, este é o conteúdo do arquivo!
```

## Saída de texto

O comportamento atual de `echo` é descrito na seção específica mais adiante:
ele tenta executar uma imagem ZAPP em ring 3 e usa a implementação nativa
somente como fallback seguro.

## `mem`
Mostra informações de memória.

```
zephyr> mem
Memoria:
  Total: 128 MB
  Livre: 120 MB
  Usada: 8 MB
```

## `procs`
Lista todos os processos ativos.

```
zephyr> procs
Processos ativos:
  PID 1  idle  RUNNING
  PID 2  shell  RUNNING
Total: 2 processos
```

## `threads`
Lista todas as threads ativas.

```
zephyr> threads
Threads ativas:
  TID 1  main  RUNNING
  TID 2  timer  BLOCKED
Total: 2 threads
```

## `threadtest`
Cria duas threads temporarias, alterna entre elas cooperativamente e confirma
que os registradores e as stacks foram trocados na ordem esperada. O teste
limpa as threads antes de retornar ao Shell.

```
zephyr> threadtest
Teste de threads: OK
```

## `uptime`
Mostra o tempo desde que o sistema foi ligado.

```
zephyr> uptime
Uptime: 2h 30m 15s
```

## `beep [frequência] [duração]`
Toca um beep pelo PC Speaker.

```
zephyr> beep          → Beep padrão (800Hz, 200ms)
zephyr> beep 440 500  → 440 Hz por 500ms
```

## `melody`
Toca uma escala musical (C4→C5).

```
zephyr> melody
Tocando melodia...
Melodia concluida!
```

## `reboot`
Reinicia o computador.

## `shutdown`

Executa imediatamente o servico terminal `power_shutdown()`. Quando
`power status` informa `Transicao S5 ACPI: PRONTA`, o kernel adquire o modo
ACPI se necessario e solicita o desligamento fisico por PM1. Se S5 nao for
seguro ou estiver indisponivel, o fallback `CLI+HLT` para a CPU e mantem a
maquina ligada.

O comando nao aceita argumentos. `shutdown invalido` mostra
`Uso: shutdown` e mantem o sistema ativo.

## `mouse`
Mostra o status atual do mouse PS/2 (posição X/Y e botões pressionados).

```
zephyr> mouse
X=100 Y=200 Buttons=0x01
```

## `guitest`
Testa as primitivas gráficas 2D do módulo GUI (janela, botões, texto).

```
zephyr> guitest
```

## `guimode classic|modern`
Altera o modo de interface do sistema (Classic TUI ou GUI Moderna) dinamicamente, demonstrando o fallback visual sem afetar a arquitetura interna do kernel.

```
zephyr> guimode modern
```

## `health [summary]`
Exibe métricas detalhadas do kernel, estado do recovery, paginação, processos e saúde estrutural da arquitetura. Também mostra o componente `Update` e separa verificação local, aplicação, rollback e remoto. Aplicacao fica `READY` somente em FAT12 com estado persistente integro; rollback exige backup valido; remoto U5 permanece `DISABLED` sem degradar o verificador local.

Use `Page Up`, `Page Down`, `Home` e `End` para consultar toda a saida quando
o relatorio ocupar mais de uma tela.

No uso normal, os eventos bem-sucedidos repetitivos de criação, foco e coleta
de processos ring 3 ficam no nível `DEBUG`; inicializações, `WARN` e `ERRO`
continuam visíveis.

```
zephyr> health
```

`health summary` nao altera nem substitui o relatorio completo. Ele cabe em uma
tela durante os testes e mostra contagens por estado, todos os componentes nao
`READY`, capacidades de Update, processos, paging, memoria e validade do heap:

```text
zephyr> health summary
Resumo do health:
  Componentes: READY=20 DEGRADED=1 DISABLED=1 UNKNOWN=0
  AC97: DISABLED erro=4 falhas=1
  Media Player: DEGRADED erro=9 falhas=1
  Update: READY
    local=READY apply=READY rollback=DISABLED historico=READY remoto=DISABLED
  Kernel: proc=4 READY=3 RUNNING=1 BLOCKED=0 ZOMBIE=0 paging=READY
  Memoria KB: usada=20652 livre=110292 heap=READY
```

Argumentos diferentes de `summary` sao recusados com
`Uso: health [summary]`.

O campo `historico` fica `READY` para historico vazio ou valido, `DEGRADED`
quando os controles U4 perderam integridade e `DISABLED` quando o armazenamento
nao esta disponivel.

## `devices`, `device-info` e `device-scan`

Os comandos de dispositivos sao somente de leitura e nao abrem uma interface
grafica. Eles nao reinicializam ATA, AC97 ou PS/2 e nao gravam no disco.

```text
zephyr> devices
zephyr> devices -v
zephyr> device-info ata-primary
zephyr> device-scan
```

`devices` mostra ID, estado, tipo e nome. `devices -v` acrescenta localizacao,
IRQ e IDs PCI quando existirem. `device-info <id>` consulta um ID retornado
pela lista. `device-scan` apenas rele o espaco de configuracao PCI e atualiza
o snapshot; se o limite de 64 entradas for atingido, o resultado e parcial e
o Shell permanece utilizavel.

IDs PCI sao exibidos como `pci-BB:DD.F`. O terminal interpreta Shift para
maiusculas e simbolos, incluindo `:` com `Shift+;`. A forma
`pci-BB-DD.F` e letras minusculas continuam aceitas como alternativas.

## Rede: comandos individuais e diagnostico agrupado

Os comandos mantem o snapshot PCI e mostram o estado real de ate quatro E1000
`8086:100E` ou RTL8139 `10EC:8139`. A inicializacao de hardware acontece no
boot pelo Network Manager; `device-scan` apenas atualiza o inventario e nunca
reinicializa drivers.

```text
zephyr> net status
zephyr> net devices
zephyr> net info net-pci-00:03.0
zephyr> net ethernet net-pci-00:03.0
zephyr> net test net-pci-00:03.0
zephyr> net arp config net-pci-00-03.0 10.0.2.15
zephyr> net arp status
zephyr> net arp resolve 10.0.2.2
zephyr> net arp table
zephyr> net arp clear
zephyr> net ipv4 config net-pci-00-03.0 10.0.2.15 255.255.255.0 10.0.2.2
zephyr> net ipv4 status
zephyr> net udp status
zephyr> net dhcp acquire net-pci-00-03.0
zephyr> net dhcp status
zephyr> net dns status
zephyr> net tcp status
zephyr> net tcp connect example.com 80
zephyr> net socket status
zephyr> net socket table
zephyr> nslookup example.com
zephyr> ping 10.0.2.2
zephyr> ping example.com 1
zephyr> http get http://neverssl.com/
zephyr> http status
zephyr> net check net-pci-00-03.0
zephyr> net check qemu net-pci-00-03.0 10.0.2.15
zephyr> net check qemu dhcp net-pci-00-03.0 example.com
zephyr> net check qemu tcp net-pci-00-03.0 neverssl.com
zephyr> net check qemu multi net-pci-00-03.0 net-pci-00-04.0
```

`net status` separa inventario, controladores reconhecidos, drivers ativos,
erros de driver, interface L3, link e protocolos. `net devices` lista IDs
estaveis, modelo, estado e marca `[L3]`/`[DHCP]`. `net info <id>` mostra
vinculo Ethernet, papel L3, aquisicao DHCP, MAC, contadores, fila RX, erro do
driver, PCI, IRQ e BAR0-BAR5. A forma `net-pci-BB-DD.F` tambem e aceita.

`net ethernet <id>` consulta a camada sem transmitir. A saida mostra frames
processados na consulta, fila atual/pico, descartes por fila cheia, IRQs RX,
unicast, broadcast, frames invalidos/filtrados, payloads ainda sem protocolo e
o ultimo cabecalho aceito. `Polls` conta somente ciclos acionados por RX
pendente. O processo de sistema drena a fila fora da IRQ, por isso o contador
da consulta pode ser zero enquanto os totais continuam aumentando.

`net test <id>` pede que a camada Ethernet monte e envie um unico frame
broadcast com EtherType privado `0x88B5`, confirmando somente a conclusao do
descritor TX. Ele nao abre conexao, nao configura IP e nao transmite
automaticamente no boot. Sem NIC ativa, com link indisponivel, ID desconhecido
ou sintaxe invalida, o Shell retorna erro controlado. O mesmo comando atende
RTL8139; sem NIC, os comandos seguem utilizaveis e mostram inventario vazio.
`regcheck full` consulta o registro sem resetar drivers ou a camada Ethernet.

`net arp config <id> <ip-local>` continua disponivel para diagnosticos da
camada ARP. Repetir a mesma configuracao preserva cache e contadores. Se uma
configuracao IPv4 ativa usar outro ID ou IP, ela e a sessao ICMP sao
invalidadas antes de trocar a sessao ARP.

`net arp resolve <ip>` retorna imediatamente o MAC em cache ou informa
`pendente` depois de enviar o primeiro request. O servico repete em um e dois
segundos e marca `FAILED` no terceiro segundo, sem bloquear o terminal.
`net arp status` mostra configuracao, estados do cache, requests/replies,
cache hits, ciclos de manutencao, invalidos, ignorados e timeouts. A linha
`Cobertura` distingue testes aprovados de cenarios ainda nao executados.
`net arp table` lista IP, MAC, estado, idade e tentativas; `net arp clear`
limpa apenas o cache. Entradas resolvidas e falhas expiram apos 30 segundos.

`net ipv4 config <id> <ip> <mascara> <gateway>` configura uma unica interface
somente em RAM. A mascara deve ser contigua entre `/1` e `/30`. O gateway
`0.0.0.0` significa rede local sem rota padrao; quando presente, ele deve
pertencer a mesma sub-rede e nao pode coincidir com host, rede ou broadcast.
Uma configuracao identica e idempotente; qualquer mudanca cancela ping,
descarta reply ICMP pendente e limpa o cache ARP.

`net ipv4 status` agrega o estado IPv4 e ICMP. IPv4 mostra interface, IP,
mascara, gateway, MTU, pacotes, bytes, rota direta/gateway e descartes por
checksum, opcoes, fragmentos ou protocolo. ICMP mostra requests/replies RX/TX,
sessao, perdas, RTT minimo/medio/maximo, invalidos e reply pendente.

`net udp status` mostra os 16 endpoints fixos, datagramas e bytes RX/TX,
broadcast, checksum, portas sem listener e erros de callback.

`net dhcp acquire <id>` executa Discover, Offer, Request e ACK em uma chamada
cooperativa. A configuracao IPv4 atual permanece ativa ate um ACK valido,
mesmo quando a aquisicao usa outra NIC; o ACK troca a interface atomicamente
e encerra clientes remotos existentes. `status` mostra lease, T1/T2,
tentativas e contadores; `renew` inicia
renovacao unicast e `release` tenta DHCPRELEASE antes de remover o lease
local. Nenhuma aquisicao acontece automaticamente no boot.

`net dns config <servidor>` configura DNS para IPv4 estatico. DHCP configura
automaticamente o primeiro servidor valido da opcao 6. `status` mostra a
consulta e os contadores, `table` lista nome, IPv4, TTL e idade, e `clear`
limpa somente o cache. `nslookup <dominio>` resolve A/IN, segue CNAMEs
limitados e aguarda cooperativamente.

`net tcp status` mostra conexoes, segmentos, bytes, SYN/FIN/RST,
retransmissoes, timeouts, checksum e descartes. `net tcp connect
<ip-ou-dominio> <porta>` resolve o destino, abre uma conexao ativa,
aguarda cooperativamente pelo handshake e encerra o teste sem deixar um
socket reservado.

`net socket status` mostra capacidade, operacoes e bytes das filas nativas.
`net socket table` lista handle, estado, destino, porta local e ocupacao
TX/RX. Handles possuem geracao; uma referencia antiga e recusada.

`http get <url>` aceita somente `http://host[:porta]/caminho`, aguarda DNS,
TCP e resposta sem bloquear o processo de sistema, e mostra status, tamanhos
e uma previa textual de ate 512 bytes. `http status` inspeciona a sessao.
Respostas com `Transfer-Encoding`/chunked, framing ambiguo, headers acima de
4096 bytes ou corpo acima de 16 KiB sao recusadas.

`ping <ip-ou-dominio> [quantidade]` usa quatro tentativas por padrao e aceita
de uma a dez. Nomes sao resolvidos pelo mesmo cache/cliente DNS antes do ICMP.
Cada Echo Request carrega 32 bytes deterministicos. A espera por ARP nao
consome o timeout ICMP; depois do envio, cada tentativa aguarda um segundo.
O processo Shell dorme um tick entre observacoes, permitindo que o processo de
sistema continue Ethernet, ARP e ICMP. Cada reply ou timeout e impresso antes
do resumo completo em uma unica chamada.

`net check [id]` reduz a sequencia manual sem remover nenhum comando
 individual. Sem ID, agrupa estado geral, controladores, ARP, IPv4/ICMP, UDP,
DHCP, DNS, TCP, sockets, HTTP e invariantes. Com um ID valido, inclui tambem `net info` e
`net ethernet`. O
comando nao
configura IP, nao inicia uma resolucao nem cria transmissao de teste; ele
consolida as consultas e valida o estado atual. A manutencao de uma resolucao
ja pendente continua normalmente. Assim, depois de `net arp config` e
`net arp resolve`, uma unica chamada a `net check <id>` mostra o resultado.

`net check qemu <id> <ip-local>` e a variante ativa para o backend de rede
padrao do QEMU. Ela configura `/24` e gateway `10.0.2.2`, limpa o cache e
executa request/reply, cache hit sem novo TX, timeout ARP para `10.0.2.254` e
um Echo ICMP para o gateway. A suite valida RX/TX IPv4, checksum, Echo Reply,
RTT, polling e invariantes. Ao final, mostra `OK`/`ERRO`, ARP, tabela e
IPv4/ICMP. Esses enderecos pertencem somente ao perfil solicitado e nunca
viram configuracao automatica de boot.

`net check qemu dhcp <id> <dominio>` e a suite agrupada da S2.6. Ela remove
um lease DHCP anterior, executa uma nova aquisicao, confirma UDP e DORA,
valida `/24`, gateway `10.0.2.2` e DNS `10.0.2.3`, consulta o dominio
informado, confirma cache hit sem novo TX e faz um Echo para o gateway. A
consulta real depende da internet do host; por isso o dominio e explicito.

`net check qemu tcp <id> <dominio>` e a suite agrupada da S2.7. Ela
obtem ou reutiliza o lease DHCP, resolve o dominio, executa um GET HTTP na
porta 80 e valida handshake/dados/checksum TCP, filas de socket, resposta
HTTP, FIN, polling e invariantes em uma chamada. O dominio e obrigatorio e o
resultado depende de DNS e HTTP externo disponiveis no host. As tres suites
QEMU usam rotinas separadas e snapshots estaticos: isso impede que o
otimizador some os diagnosticos em um unico frame maior que a pilha de 4 KiB
do processo Shell. O dominio da suite TCP deve responder com
`Content-Length` ou fechamento de conexao; se o servidor escolher
`Transfer-Encoding`, a S2.7 o rejeita de forma controlada. Por isso o exemplo
usa `neverssl.com` em vez de depender do framing atual de `example.com`.
Como o servidor e externo, a suite repete a conexao HTTP ate tres vezes
somente quando ocorre timeout de transporte; erros de protocolo nao sao
mascarados. O comando `http get` individual continua com uma unica sessao.

`net check qemu multi <id-a> <id-b>` e a suite S2.8. Ela envia um frame de
diagnostico por vez e confirma que os contadores L2 e do driver aumentam
somente na interface escolhida. Os dois IDs devem apontar para NICs ativas e
distintas; a suite tambem executa as invariantes Multi-NIC.

O perfil segue o backend documentado em
[QEMU Networking](https://gitlab.com/qemu-project/qemu/blob/master/docs/system/devices/net.rst);
uma configuracao de backend diferente pode nao oferecer o host virtual `.2`.
O alvo `make run` fixa `-nic user,model=e1000`, evitando depender dos
dispositivos padrao da versao local do QEMU. Testes com RTL8139, duas NICs ou
sem NIC podem sobrescrever `QEMU_NET_ARGS`.

## `acpi status`

Mostra o resultado da descoberta ACPI: OEM, revisao, RSDP, tipo e endereco da
raiz, quantidade de tabelas, FADT, DSDT, FACS, anomalias e ticks gastos. Desde
a S1.3 tambem mostra os descritores PM1a/PM1b, SMI_CMD, modo ACPI observado e
a declaracao `_S5_`. Na S1.4, mostra separadamente se o modo pode ser ativado
e se a transicao S5 esta pronta. A saida diferencia ACPI pronto, degradado e
indisponivel, alem de `_S5_` ausente, malformado ou ambiguo.

```text
zephyr> acpi status
```

O unico argumento aceito e `status`; qualquer outra sintaxe retorna uso
controlado. O comando usa somente o snapshot do bootstrap e nao consulta nem
modifica registradores de energia durante sua execucao.

## `power status`

Mostra somente capacidades que o kernel pode confirmar. A presenca de ACPI,
FADT/DSDT, PM1, modo atual, possibilidade de ativacao e declaracao S5 do
firmware aparecem separadas da prontidao da transicao. S1-S4 permanecem
indisponiveis; S0 e idle HLT/C1 estao ativos. S5 e desligamento fisico aparecem
como disponiveis somente quando todas as pre-condicoes seguras forem atendidas;
caso contrario, S5 permanece simulado e o fallback e HLT. O comando e apenas
diagnostico e nao executa transicoes.

```text
zephyr> power status
```

## `kmetrics [reset]`

Mostra uma linha-base de metricas do kernel para comparacoes manuais no QEMU.
Sem argumento, a saida cobre o boot ou a janela iniciada pelo ultimo reset;
`kmetrics reset` captura o ponto inicial somente no Shell, sem zerar os
contadores de `health`, IPC, teclado ou processos.

O relatorio mostra ticks do PIT, trocas de contexto, yields cooperativos,
preempcoes de ring 3, fallbacks para o Idle, filas, memoria e copias VESA. A
fila de teclado inclui eventos processados e pico desde o boot; `Paging boot`
mostra paginas identity-mapped, tabelas criadas e ticks de inicializacao. Na
linha VESA, `media_bytes` e a media inteira de bytes por apresentacao da janela;
`bytes` continua sendo a medida principal para comparacoes. A secao de memoria
inclui fragmentacao e blocos do heap, rejeicoes do PMM e diretorios/paginas de
usuario; contadores de criacao e liberacao de diretorios sao deltas desde o
ultimo reset. O quantum de usuario permanece 1 tick. `TCK%` e uma estimativa baseada em
ticks; CPU real aparece como `N/D` ate uma futura etapa de calibracao por
RDTSC/PMU.

```text
zephyr> kmetrics reset
zephyr> kmetrics
```

## `memcheck`

Executa um diagnostico compacto e sincrono do heap do kernel, dos guardas do
PMM e da ausencia de diretorios de usuario residuais. Ele recusa iniciar se
houver ZAPP ou zumbi pendente. No teste normal, aloca tres blocos pequenos e
os libera em ordem que exige coalescencia; a capacidade, quantidade de blocos
e maior bloco livre devem voltar exatamente a linha-base.

A saida contem somente `OK`/`ERRO` para `heap_integridade`, `coalescencia`,
`pmm_guardas`, `diretorios_user` e `resultado`. Argumentos adicionais sao
recusados com `Uso: memcheck`. O comando nao mede memoria por processo nem
altera a saida global de `mem` ou da App API.

```text
zephyr> memcheck
```

## `schedcheck`

Valida, sem alterar processos, o processo atual, o Idle, a tabela de PIDs e
os estados do scheduler. A saida e curta; `ERRO` indica uma inconsistencia
interna registrada tambem no log. O comando nao cria processos, nao altera o
foco e nao substitui os testes de ring 3 ou `threadtest`.

```text
zephyr> schedcheck
```

Argumentos adicionais sao recusados com `Uso: schedcheck`.

## `appcheck`
Testa a fachada segura da API de aplicativos, incluindo versão, console,
uptime, memória e validação de argumentos.

```
zephyr> appcheck
```

Na Fase 3, o `appcheck` tambem valida handles de arquivo, leitura
sequencial, handles invalidos, envio/recebimento IPC, PID inexistente e
tipos de mensagem invalidos. A ponte continua restrita ao ring 0.

As chamadas de arquivo usam handles opacos e leitura sequencial. As chamadas
de IPC validam o PID, o estado do processo, o tipo da mensagem e a fila.

O comando usa a ponte interna do dispatcher e testa números inválidos,
argumentos nulos e `process_exit`. O vetor `int 0x80` também está disponível
para o processo de teste ring 3 depois da inicialização segura do kernel.

Na Fase 5, o `appcheck` também cria temporariamente `DEMO.ZAP`, valida o
cabecalho `ZAPP`, executa o processo ring 3 e remove o arquivo. Também testa
entry point, flags, tamanho de codigo e cabecalho invalidos. O arquivo de
demonstracao nao permanece no disco.

Na Fase 6B, também confirma a ABI de lançamento `0.3`: argumentos válidos,
ausência de argumentos, quantidade excessiva, texto grande, foco, execução e
reaproveitamento seguro do processo externo.

Na Fase 6C, a sequencia assincrona tambem valida as migracoes ZAPP internas
de `uptime` e `mem`, incluindo rejeicao de lancamento concorrente, retorno de
foco e ausencia de processos de usuario ou zumbis residuais.

O `appcheck` mistura operacoes validas com entradas deliberadamente invalidas.
Assim, `ERRO` nas chamadas de syscall, ponteiro, buffer, handle, arquivo, PID,
mensagem, argumento ou imagem ZAPP invalida e esperado se o retorno for
controlado e a suite continuar. `memory_info nulo` e os dois testes de
migracao concorrente usam comparacao de codigo esperado e devem aparecer como
`OK`. Erro em caminho valido, divergencia dessas comparacoes, falha de foco ou
limpeza, `KERNEL PANIC` ou prompt ausente/duplicado representa regressao; a
matriz completa esta em `docs/roadmaps/01-estabilizacao-e-qualidade.md`.

`file_service_indisponivel` e `loader_indisponivel` sao esperados apenas
quando o componente correspondente aparecer indisponivel no `health`.

## `q2check`
Executa automaticamente duas falhas isoladas de `UserTest` e apresenta um
resumo compacto da configuração de log, contador, resumo seguro e limpeza de
processos. Ele aguarda a conclusão assíncrona, bloqueia a entrada até o fim e
devolve um único prompt. Os dois avisos reais de exceção permanecem visíveis.

```text
zephyr> q2check
```

O comando recusa a execução enquanto houver processo ring 3 ou zumbi pendente.
Ele não substitui o `appcheck` completo nem a validação manual de `F12` com
`app inputtest`.

## `regcheck`

Executa uma regressao curta para o ciclo habitual de desenvolvimento. Sem
mostrar aprovacoes por etapa, ele valida internamente o estado de `health`, a
linha-base equivalente a `procs`, os invariantes de `schedcheck`, o
heap/PMM/paging de `memcheck`, pre-validacoes de pacote, troca cooperativa de
threads e um ciclo ZAPP silencioso de sucesso. Em seguida inicia outro ZAPP
silencioso e mostra a instrucao para pressionar `F12`; o cancelamento e feito
pelo runtime real, nao por simulacao.

`regcheck full` preserva essas etapas e acrescenta uma varredura PCI real,
reconstrucao dos snapshots de Devices e Network, consultas por indice e ID,
validacao das tabelas ACPI copiadas e coerencia entre ACPI, Power e S5. Os
logs informativos da varredura sao silenciados temporariamente; o nivel
anterior e restaurado antes das demais etapas e falhas reais continuam
visiveis.

```text
zephyr> regcheck
RegCheck: pressione F12 para validar cancelamento.
RegCheck: OK
zephyr>

zephyr> regcheck full
RegCheck: pressione F12 para validar cancelamento.
RegCheck: OK
zephyr>
```

Em falha, a saida enumera somente as etapas inesperadas com seu codigo:
`health`, `servicos_base`, `scheduler`, `memoria`, `pacotes`, `threads`,
`processos`, `device_scan`, `devices`, `network`, `acpi`, `power`,
`loader_ring3`, `cancelamento_f12` ou `limpeza_final`. O comando
recusa a execucao se houver outro diagnostico, ZAPP, UserTest ou zumbi
pendente; argumentos extras usam `Uso: regcheck [full]`. Ele nao grava no
FAT, nao cria pacote temporario nem altera o contador de falhas isoladas.
Ausencia coerente de NIC, ACPI ou AC97 nao e tratada como falha; inventario
PCI parcial e reportado como erro.

`regcheck` e um atalho, nao uma substituicao para `appcheck`, `q2check`,
`usertest fault`, `app outputtest [fail]`, `app inputtest` encerrado por
`Enter`, desligamento/reboot ou a validacao manual das interfaces classic e
modern.

## `update verify <arquivo.ZUP>`

Valida um ZUPD v1 local em modo somente-leitura. O comando verifica estrutura,
limites, SHA-256 global e individual, `key_id`, assinatura Ed25519,
arquitetura, versoes, epochs, allowlist e existencia do arquivo alvo.

```text
zephyr> update verify VALID.ZUP
```

Em sucesso, mostra `NONE`, versoes base/alvo, epochs, quantidade de arquivos e
tamanho total. Em falha, mostra o motivo estavel e o codigo generico. Ambos os
caminhos confirmam `Nenhuma gravacao foi realizada.` Desde a U3, a versao base
e comparada com a versao de conteudo instalada, nao com a versao de build do
kernel.

Os sete aliases de teste e os resultados esperados estao em
[`ferramenta-zupd.md`](../14-atualizacoes/ferramenta-zupd.md).

## `update apply <arquivo.ZUP> [--confirm]`

Sem `--confirm`, repete toda a verificacao ZUPD e executa o preflight de
estado, espaco, staging, backup e copy-on-write sem gravar:

```text
zephyr> update apply APPLY.ZUP
```

Com `--confirm`, repete o mesmo preflight e inicia a transacao FAT12:

```text
zephyr> update apply APPLY.ZUP --confirm
```

Esc ou F12 cancela cooperativamente entre staging, backups e arquivos. Antes
do commit, o updater tenta restaurar imediatamente o estado anterior. Se a
restauracao nao puder terminar, o comando informa `RECOVERY_PENDING` e o boot
seguinte continua a recuperacao. Caracteres diferentes de Esc/F12 sao
ignorados durante a operacao.

Sucesso informa a nova versao instalada, quantidade processada e necessidade
de reboot. Os BMPs em memoria nao sao recarregados durante a aplicacao.

## `update rollback [--confirm]`

Sem `--confirm`, valida a geracao anterior e mostra o rollback disponivel sem
gravar. Com confirmacao, restaura atomicamente os arquivos e a versao anterior:

```text
zephyr> update rollback
zephyr> update rollback --confirm
```

Somente a ultima geracao concluida pode ser restaurada. Depois do rollback, o
backup e consumido e a capacidade volta a `DISABLED (sem backup)`.

## `update test fail-after <1-3>`

Arma um failpoint diagnostico one-shot para a proxima aplicacao confirmada. O
updater interrompe depois do N-esimo alvo e deixa o journal deliberadamente
pendente:

```text
zephyr> update test fail-after 1
zephyr> update apply APPLY.ZUP --confirm
```

Esse comando existe somente para provar a recuperacao no proximo boot. Nao
deve ser usado em uma atualizacao normal.

## `update status`

Mostra, sem gravar, as versoes de build, instalada e de rollback, epochs,
integridade dos controles, journal, capacidades e ultima operacao persistida:

```text
zephyr> update status
```

Estado ausente no baseline aparece como `EMPTY`, nao como corrupcao. Historico
com ambas as copias invalidas aparece como `INVALID`, mas nao desabilita por si
so verificacao, aplicacao ou rollback.

## `update history`

Lista ate oito eventos do mais recente para o mais antigo. Cada linha informa
sequencia, operacao, resultado, transicao, progresso, motivos e alias do
pacote. A consulta nao grava nem repara controles:

```text
zephyr> update history
Historico de Update vazio.
```

Aplicacao e rollback confirmados podem registrar `SUCCESS`, `FAILED` ou
`CANCELLED`. Depois de uma recuperacao no boot, aparecem o encerramento
pendente e `RECOVERY_APPLY/RECOVERED` ou
`RECOVERY_ROLLBACK/RECOVERED`.

## `updater`

Abre o aplicativo nativo System Updater. Classic usa TUI em tela cheia;
Modern abre uma janela hospedada pelo Window Manager e volta automaticamente
ao Classic se a hospedagem estiver indisponivel. As abas Pacotes, Estado e
Historico e os fluxos de confirmacao sao descritos em
[`system-updater.md`](../14-atualizacoes/system-updater.md).

## `pkg list|info|verify|install|remove`

Os comandos `pkg` administram o primeiro formato local de distribuicao. O
artefato do host e `.zephyrosapp`; quando injetado na imagem FAT12 ele recebe
o alias `ID.ZPK`, com bytes identicos.

```text
pkg list
pkg info DEMO.ZPK
pkg verify DEMO.ZPK
pkg install DEMO.ZPK
app run APPS/DEMO/APP.ZAP
pkg remove DEMO
```

`pkg info <ID>` consulta um pacote instalado; `pkg info <arquivo.ZPK>` valida
o fonte e mostra o manifesto. `pkg verify` nao grava. `pkg install` valida
header, manifesto, CRC32 e ZAPP antes de criar `APPS/<ID>/APP.ZAP` e
`META.DAT`; ele recusa ID instalado, dependencia ausente, app em foco e falta
de espaco. `pkg remove` recusa a remocao quando outro pacote instalado depende
do ID e preserva o arquivo fonte `ID.ZPK` no diretorio raiz.

O contrato completo, limites e fluxo host para FAT12 estao em
[`pacotes.md`](../13-aplicativos/pacotes.md).

## `pkgcheck`

Executa tres pre-validacoes compactas sem escrever no disco: pacote invalido,
dependencia ausente e espaco insuficiente. Ele nao substitui `appcheck` e nao
instala nem remove pacotes.

## `app run <arquivo.ZAP> [arg1 arg2 ...]`

Executa uma imagem flat i386 em ring 3 sem bloquear o Shell.

```text
zephyr> app run DEMO.ZAP alpha beta
```

O formato possui uma pagina maxima de codigo, uma pagina maxima de dados e
uma pagina de stack. Arquivos invalidos, inexistentes ou maiores que o limite
retornam erro controlado e nao causam `panic`.

O aplicativo recebe foco automaticamente e pode obter scancodes PS/2 brutos
com `message_receive` e `APP_MESSAGE_KEYBOARD`. `Esc` continua disponivel para
o aplicativo. `F12` encerra somente o `.ZAP` externo em foco e devolve o
controle ao Shell. Argumentos sao separados por espacos ou tabs; aspas e
escapes ainda nao possuem significado especial.

Cada chamada `console_write` aceita de 1 a 1024 bytes ASCII e conclui de forma
sincrona. Um ZAPP pode enviar blocos consecutivos em ordem, mas deve tratar o
primeiro erro como final e incluir suas proprias quebras de linha. Nao ha fila,
quota total, historico de comandos ou entrada de linha para aplicativos nesta
fase; o scrollback do Shell continua limitado a 200 linhas e `F12` cancela o
aplicativo em foco.

O ciclo de vida diferencia falha ao iniciar (`ERRO`), falha isolada (`WARN`),
cancelamento por `F12` (`INFO`), termino com codigo `0` (`INFO`) e termino
normal com codigo nao-zero (`ERRO`). O codigo `0xF120` e reservado ao runtime
e nao pode ser usado diretamente por `process_exit` em um aplicativo.

## `app argtest <texto>`

Executa uma imagem interna ring 3 que exibe o texto recebido pela pagina de
lancamento. Serve para validar a passagem de argumentos sem criar arquivo no
filesystem.

```text
zephyr> app argtest alpha beta
Argumentos ZAPP: alpha beta
```

## `echo <texto>`

`echo` e a primeira funcao nativa migrada. Quando Loader, filesystem, paging
e modo usuario estao disponiveis, ele roda como imagem ZAPP ring 3 e retorna
ao Shell de forma assincrona. Se essas dependencias estiverem indisponiveis,
o comportamento nativo anterior imprime o mesmo texto como fallback.

## `app inputtest`

Cria temporariamente um `.ZAP` de diagnostico, entrega o foco a ele e remove
o arquivo logo apos o carregamento. Pressione qualquer tecla para exercitar a
fila e `Enter` para encerrar normalmente; `F12` cancela com retorno seguro ao
Shell.

## `app outputtest [fail]`

Executa uma imagem ZAPP interna que escreve nove blocos ASCII de 128 bytes,
totalizando 1152 bytes. Sem argumento, ela encerra com codigo `0`; com `fail`,
encerra normalmente com codigo `1` depois de emitir a mesma saida.

```text
zephyr> app outputtest
zephyr> app outputtest fail
```

O primeiro caso deve terminar com uma unica mensagem `INFO` e um prompt. O
segundo deve terminar com uma unica mensagem `ERRO` contendo o codigo `1`, sem
ser tratado como falha isolada ou cancelamento. Outros argumentos exibem o uso
do comando; se o loader estiver indisponivel, o Shell informa o erro controlado
e nao tenta fallback nativo.

## `usertest`
Cria um processo mínimo em ring 3, com diretório de páginas e stack de kernel
próprios. O teste chama `console_write`, `uptime`, `memory_info` e
`process_exit` através de `int 0x80`.

```text
zephyr> usertest
zephyr> usertest fault
```

`usertest fault` acessa uma página inválida para confirmar que uma exceção de
usuário encerra somente o processo de teste. O estado do processo e do gate
de syscall aparece no comando `health`.

Ao terminar normalmente ou por falha isolada, o Shell imprime o resultado do
teste e apresenta um novo prompt, para que o proximo comando nunca fique
misturado ao aviso assincrono do processo.

## `explorer`
Abre o gerenciador de arquivos ZephyrOS Explorer. Em `guimode modern`, usa a
janela gráfica; em `guimode classic` ou sem VESA/backbuffer, mantém a TUI.

```
zephyr> explorer
```

Navegação com setas, F3 visualizar, F7 criar, F8 excluir com confirmação, F2 renomear.

## `desktop`
Ativa o ambiente desktop com ícones e menu Iniciar.

```
zephyr> desktop
```

## `taskmgr`
Abre a TUI de diagnóstico do gerenciador de tarefas, com processos, memória e
threads. Pela taskbar ou Desktop em modo moderno, o mesmo componente abre sua
janela gráfica própria.

```
zephyr> taskmgr
```

Abas: Processos, Memória e Threads. Use `Tab`, Setas, `S`, `Enter`, `Delete`,
`F`, `R` e `Esc` conforme a interface ativa.

## `edit <arquivo>`
Editor de texto com syntax highlighting e word wrap.

```
zephyr> edit TESTE.TXT
```

Funcionalidades:
- Syntax highlight: C, Python, Assembly, Markdown
- Word wrap automático
- Detecção de encoding (ASCII, Latin1, UTF-8)
- Detecção de line ending (LF, CR, CRLF)
- Numeração de linhas
- Scroll vertical

Teclas:
| Tecla | Ação |
|-------|------|
| Setas | Navegação |
| Ctrl+S | Salvar |
| Ctrl+Q | Sair |
| Home/End | Início/fim da linha |
| Page Up/Down | Rola página |

## `play <arquivo.wav>`
Reproduz um arquivo de áudio WAV via AC97.

```
zephyr> play MUSICA.WAV
```

## `compress on|off|status`
Gerencia o módulo de compressão de RAM.

```
zephyr> compress on      → Ativa compressão
zephyr> compress off     → Desativa
zephyr> compress status  → Mostra estatísticas
```

## `stats`
Mostra estatísticas detalhadas de compressão.

```
zephyr> stats
```

Exibe: total de compressões, bytes comprimidos, espaço economizado.

## `view <arquivo.bmp>`
Exibe uma imagem BMP na tela (modo VESA).

```
zephyr> view IMAGEM.BMP
```

## `stop`
Para a reprodução de mídia (áudio WAV em reprodução).

```
zephyr> stop
```

## `wm`
Abre o gerenciador de janelas (Window Manager).

```
zephyr> wm
```

## `taskcfg`
Configura a barra de tarefas (posição, tamanho, fixação).

```
zephyr> taskcfg
```

## `settings`
Abre o sistema de configurações do ZephyrOS.

```
zephyr> settings
```

Categorias: Tela, Barra de Tarefas, Janelas, Ícones, Sistema, Som, Sobre.
