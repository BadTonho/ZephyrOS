# Referência: Comandos e Atalhos — ZephyrOS

Este documento consolida todos os comandos de Shell e atalhos de teclado (e interações de mouse) disponíveis em todo o ecossistema do ZephyrOS.

---

## 💻 1. Comandos do Shell (Terminal)

Os comandos a seguir podem ser digitados na janela de terminal interativo (`shell`).

| Comando | Parâmetros | Descrição |
|---------|------------|-----------|
| `help` | - | Lista todos os comandos disponíveis. |
| `clear` | - | Limpa a tela e o historico do terminal. |
| `desktop` | - | Abre a Área de Trabalho (Desktop). |
| `settings`| - | Abre o Painel de Configurações (Settings). |
| `wm` | - | Abre o Gerenciador de Janelas (Window Manager). |
| `ls` | - | Lista os arquivos e pastas do disco atual. |
| `cat` | `<arquivo>` | Exibe o conteúdo de um arquivo de texto. |
| `echo` | `<texto>` | Imprime texto por ZAPP ring 3, com fallback nativo seguro. |
| `mem` | - | Exibe o uso da memória RAM (total, livre, usada). |
| `procs` | - | Lista os processos ativos no sistema. |
| `threads` | - | Lista as threads ativas do sistema. |
| `threadtest` | - | Valida a troca cooperativa de contexto entre duas threads temporarias. |
| `uptime` | - | Exibe o tempo que o sistema está ligado. |
| `beep` | `[freq] [dur]` | Emite um som de aviso ou reproduz uma frequência. |
| `melody` | - | Toca uma escala musical no PC Speaker. |
| `explorer`| - | Abre o Gerenciador de Arquivos (File Manager). |
| `taskmgr` | - | Abre o Gerenciador de Tarefas (Task Manager). |
| `updater` | - | Abre o System Updater em Simple ou Classic. |
| `taskcfg` | - | Abre rapidamente as configurações da Barra de Tarefas. |
| `compress`| `on/off/status`| Gerencia a compressão de memória RAM em tempo real. |
| `stats` | - | Exibe estatísticas de compactação LZSS de memória. |
| `play` | `<arquivo.wav>`| Toca um arquivo de áudio WAV via driver AC97. |
| `view` | `<arquivo.bmp>`| Visualiza uma imagem BMP na tela VESA. |
| `stop` | - | Interrompe imediatamente qualquer reprodução de áudio. |
| `edit` | `<arquivo>` | Abre o Editor de Texto integrado. |
| `mouse` | - | Exibe debug em tempo real (X, Y, cliques) do Mouse PS/2. |
| `guitest` | - | Roda um teste nativo das primitivas gráficas GUI 2D. |
| `health` | `[summary]` | Mostra o relatorio completo ou um resumo compacto para testes. |
| `update remote` | `status/enable/disable/clear [--confirm]` | Controla o transporte remoto opcional da sessao. |
| `update fetch` | `[--url <manifesto>] [--confirm]` | Consulta ou baixa um ZUPD autenticado sem aplicar. |
| `kmetrics` | `[reset]` | Mostra ou inicia a janela manual de métricas K1. |
| `devices` | `[-v]` | Lista o inventario nativo de hardware; `-v` inclui localizacao, IRQ e IDs PCI. |
| `device-info` | `<id>` | Mostra os detalhes de um dispositivo listado por `devices`. |
| `device-scan` | - | Refaz somente a varredura PCI e atualiza o inventario sem reinicializar drivers. |
| `net` | `status` | Mostra inventario, drivers ativos, link, RX/TX e IPv4. |
| `net` | `devices` | Lista os controladores PCI de rede observados. |
| `net` | `info <id>` | Mostra MAC, contadores, erro, IDs, localizacao, IRQ e BARs de uma interface. |
| `net` | `test <id>` | Envia um frame Ethernet pela E1000 ou RTL8139 escolhida. |
| `net` | `arp ...` | Configura, resolve e inspeciona o cache ARP. |
| `net` | `ipv4 config <id> <ip> <mask> <gw>` | Configura IPv4 estatico somente em RAM. |
| `net` | `ipv4 status` | Mostra configuracao, rotas, IPv4, ICMP, perdas e RTT. |
| `net` | `tcp status` | Mostra conexoes, retransmissoes e descartes TCP. |
| `net` | `tcp connect <host> <porta>` | Testa uma abertura TCP cooperativa. |
| `net` | `socket status|table` | Inspeciona os sockets nativos e suas filas. |
| `net` | `check [id]` | Agrupa os diagnosticos de rede sem remover os comandos individuais. |
| `net` | `check qemu <id> <ip>` | Executa a suite ARP, IPv4 e ICMP do QEMU. |
| `net` | `check qemu dhcp <id> <dominio>` | Executa a suite UDP, DHCP e DNS. |
| `net` | `check qemu tcp <id> <dominio>` | Executa a suite TCP, sockets e HTTP. |
| `net` | `check qemu multi <id-a> <id-b>` | Valida TX e contadores isolados em duas NICs. |
| `http` | `get <url>|status` | Executa HTTP GET limitado ou inspeciona a sessao. |
| `nslookup` | `<dominio>` | Resolve um registro DNS A cooperativamente. |
| `ping` | `<ip> [1-10]` | Executa ICMP Echo e entrega eventos e resumo em uma chamada. |
| `acpi` | `status` | Mostra tabelas, PM1, modo ACPI, `_S5_` e prontidao S5, sem executar transicoes. |
| `power` | `status` | Mostra ativacao do modo, prontidao S5, desligamento fisico e fallback HLT. |
| `memcheck` | - | Valida heap, coalescencia, PMM e diretorios ring 3 residuais. |
| `schedcheck` | - | Valida os invariantes atuais do scheduler sem alterar processos. |
| `q2check` | - | Executa o diagnóstico compacto da Q2 com duas falhas isoladas. |
| `regcheck` | - | Valida health, processos, scheduler e memoria sem listagem; pausa para `F12`. |
| `regcheck` | `full` | Soma varredura PCI, Devices, Network, ACPI e Power; mostra somente falhas e o resultado final. |
| `appcheck` | - | Testa API, arquivos, IPC e carregador ZAPP. |
| `pkg` | `list` | Lista os pacotes locais instalados. |
| `pkg` | `info <ID|arquivo.ZPK>` | Mostra metadados instalados ou do pacote fonte validado. |
| `pkg` | `verify <arquivo.ZPK>` | Valida formato, manifesto, CRC32 e ZAPP sem gravar. |
| `pkg` | `install <arquivo.ZPK>` | Instala um pacote local em `APPS/<ID>/`. |
| `pkg` | `remove <ID>` | Remove pacote sem apagar o arquivo fonte `.ZPK`. |
| `store` | `status` | Mostra estado, contagens e limites do catalogo local sem gravar. |
| `store` | `list` | Lista fontes e instalados em ordem deterministica. |
| `store` | `info <ID|alias.ZPK>` | Mostra versoes, confianca, dependencias e capacidades. |
| `pkgcheck` | - | Executa as pre-validacoes compactas do servico de pacotes. |
| `update` | `verify <arquivo.ZUP>` | Verifica assinatura e compatibilidade sem gravar. |
| `update` | `apply <arquivo.ZUP> [--confirm]` | Executa preflight ou aplica uma transacao FAT12 confirmada. |
| `update` | `rollback [--confirm]` | Inspeciona ou restaura a ultima geracao de rollback. |
| `update` | `status` | Mostra integridade, versoes, journal e capacidades sem gravar. |
| `update` | `history` | Lista os oito eventos persistidos mais recentes sem gravar. |
| `update` | `test fail-after <1-3>` | Arma uma interrupcao diagnostica para validar recuperacao no boot. |
| `app` | `run <arquivo.ZAP> [args]` | Executa uma imagem flat i386 em ring 3, em primeiro plano. |
| `app` | `inputtest` | Cria e executa um teste temporario de teclado para `.ZAP`. |
| `app` | `outputtest [fail]` | Emite 1152 bytes em blocos ZAPP e testa saida com codigo 0 ou 1. |
| `app` | `argtest <texto>` | Exibe argumentos recebidos por uma imagem ZAPP interna. |
| `usertest` | `fault` opcional | Executa e valida o primeiro processo isolado em ring 3. |
| `guimode` | `simple/classic` | Alterna globalmente entre interface TUI (modo texto) e VESA (gráfica). |
| `reboot` | - | Reinicia imediatamente o sistema operacional. |
| `shutdown`| - | Desliga por ACPI S5 quando seguro; caso contrario usa `CLI+HLT`. |

## Scroll do Shell

- **Shift + tecla**: produz maiusculas e os simbolos da fileira numerica e
  de pontuacao; `Shift+;` produz `:`.
- **Seta para Cima / Seta para Baixo**: navega pelos ultimos 16 comandos.
- **Shift + Seta para Cima / Baixo**: rola uma linha na saida do terminal.
- **Page Up / Page Down**: rola uma pagina no historico do terminal.
- **Home / End**: vai ao inicio ou ao fim do historico.
- **Roda do mouse**: rola tres linhas no Shell Simple e Classic.
- **Digitacao, Backspace ou Enter**: retorna ao fim para manter o prompt visivel.
- **`clear`**: apaga as 200 linhas de saida, mas preserva os comandos da sessao.

---

## ⌨️ 2. Teclas de Atalho por Aplicativo

### 2.1. Barra de Tarefas e Menu Iniciar
- **`Win`** ou **`Alt`**: Abre ou fecha o Menu Iniciar.
- **`F1`**: Abre a janela de Configurações da Barra de Tarefas.
- **`Setas ↑/↓`**: Navega entre as opções do menu.
- **`Enter`**: Abre o aplicativo ou configuração selecionada.
- **`Esc`**: Fecha qualquer menu que esteja aberto.
- **`Clique Esquerdo`**: Seleciona apps do menu ou alterna a janela ativa na barra inferior.

### 2.1.1. Aplicativos `.ZAP` em primeiro plano
- **Scancodes PS/2**: sao entregues ao aplicativo por `APP_MESSAGE_KEYBOARD`.
- **Esc**: pertence ao aplicativo em primeiro plano.
- **F12**: cancela somente o `.ZAP` externo em foco e restaura o Shell.
- **Menu Iniciar e taskbar**: ao abrir uma interface nativa, cancelam primeiro
  o `.ZAP` externo para preservar a prioridade da interface do sistema.

### 2.2. Window Manager (Gerenciador de Janelas)
- **`Tab`**: Alterna o foco para a próxima janela aberta.
- **`Esc`**: Cancela o contexto interno do aplicativo; quando ocioso, não fecha janelas Classic.
- **`Alt+F4`** ou **botão `X`**: Fecha a janela Classic atualmente em foco.
- **`F1`**: Minimiza a janela atual.
- **`F2`**: Maximiza a janela atual (ou restaura caso já esteja maximizada).
- **`Clique Esquerdo (na Barra de Título)`**: Permite segurar e arrastar a janela (no modo gráfico).

### 2.3. Explorer (Gerenciador de Arquivos)
- **`Setas` / `Page Up` / `Page Down`**: Navega verticalmente pela lista de arquivos.
- **`Home` / `End`**: Pula rapidamente para o topo ou para o fim da lista.
- **`Enter`**: Abre o diretório ou executa o arquivo.
- **`Backspace`**: Sobe um nível no diretório (volta à pasta pai).
- **`F2`**: Renomeia o arquivo ou pasta sob o cursor.
- **`F3`**: Visualiza (Read-only) um arquivo de texto, imagem ou áudio diretamente.
- **`F5`**: Atualiza / Recarrega a lista do diretório atual.
- **`F6`**: Cria uma **Nova Pasta**.
- **`F7`**: Cria um **Novo Arquivo** em branco.
- **`F8`** ou **`Delete`**: Exclui o arquivo ou diretório atual (pede confirmação).
- **`F9`**: Copia o arquivo selecionado para a área de transferência do Explorer.
- **`F10`**: Recorta o arquivo selecionado (Move).
- **`F11`**: Cola o arquivo copiado ou recortado no diretório atual.
- **`Duplo Clique`**: (Modo Classic) Abre o arquivo ou a pasta.

### 2.4. Gerenciador de Tarefas (Task Manager)
- **`Tab`**: Alterna entre as guias de visualização (Processos, Memória, Threads).
- **`Setas ↑/↓`**: Navega na lista de processos ou threads.
- **`Enter`**: Abre a janela de propriedades e informações avançadas do processo.
- **`Delete`**: Envia um comando de kill e finaliza o processo selecionado.
- **`R`**: Reinicia o processo (permitido apenas para serviços do sistema, ex: Explorer).
- **`F`**: Foca/Alterna a tela diretamente para a janela do aplicativo responsável pelo processo.
- **`S`**: Altera a coluna e o sentido da ordenação (Sort) da tabela de uso de CPU/RAM.
- **`Esc`**: Fecha detalhes; no modo Simple, também fecha o Task Manager.
- **`Alt+F4`** ou **botão `X`**: Fecha o Task Manager Classic.

### 2.5. Área de Trabalho (Desktop)
- **`Setas ←/→/↑/↓`**: Seleciona os diferentes ícones de programas.
- **`Enter`**: Abre o programa focado.
- **`Duplo Clique`**: Seleciona e abre o aplicativo instantaneamente no modo GUI Classic.

### 2.6. System Updater

- **`Tab`**: Alterna entre Pacotes, Estado e Historico.
- **`Setas`**: Muda a aba ou o pacote selecionado.
- **`F5`**: Atualiza pacotes e diagnosticos.
- **`V`**: Verifica o pacote selecionado sem gravar.
- **`A`**: Executa o preflight de aplicacao.
- **`B`**: Executa o preflight de rollback.
- **`Enter`**: Confirma a acao preparada.
- **`Esc`**: Cancela a confirmacao ou operacao; no Simple ocioso, fecha o aplicativo.
- **`Alt+F4`** ou **botao `X`**: Fecha o aplicativo Classic.
- **`F12`**: Solicita cancelamento cooperativo durante uma gravacao.
- **Clique esquerdo (Classic)**: Seleciona abas, pacotes e botoes equivalentes.
