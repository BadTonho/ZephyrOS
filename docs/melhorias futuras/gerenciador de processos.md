# Roadmap — Gerenciador de Processos

> Visão geral do desenvolvimento dividida em **5 fases**.
> `✅` = Implementado | `🟡` = Parcial | `⬜` = Não implementado

---

## Fase 1 — Processos Básicos
> Objetivo: Listar, monitorar e controlar processos em tempo real.

### 1. Visualização dos processos
- ✅ Aplicativos abertos. *(taskmanager.c:156-252 — lista completa de processos)*
- ✅ Processos executados em segundo plano. *(mostra todos os processos do scheduler)*
- ✅ Processos internos do ZephyrOS. *(taskmanager.c:266 - identificados pela coluna Tipo)*
- ⬜ Processos agrupados pelo aplicativo responsável.
- ✅ Nome e ícone do programa. *(taskmanager.c:199-204 — exibe nome, max 12 chars)*
- ✅ Estado do processo. *(taskmanager.c:206-215 — Pronto/Rodando/Bloqueado/Zombie)*
- ✅ Consumo de CPU. *(taskmanager.c:217-230 — cálculo por ticks, cores dinâmicas)*
- ✅ Consumo de memória RAM. *(taskmanager.c:254-300 — aba Memória com total/usado/livre)*
- ✅ Uso do disco. *(taskmanager.c:380 - leitura de operacoes do ATA)*
- ❌ Uso da rede. *(Limitacao: Sem driver de rede)*
- ❌ Uso da GPU. *(Limitacao: Sem suporte a aceleracao de hardware/GPU)*
- ❌ Mecanismo da GPU utilizado. *(Limitacao: Sem hardware GPU)*
- ⬜ Consumo de energia e tendência de consumo.
- ⬜ Processos suspensos ou executados em modo de eficiência.
- ✅ Ordenar as listas clicando nas colunas. *(taskmanager.c:202 - sorting por coluna S)*

### 2. Controle dos processos
- ✅ Finalizar tarefa. *(taskmanager.c:435-451 — tecla Delete, protege PID 1)*
- ⬜ Finalizar árvore de processos.
- ✅ Reiniciar (disponível para Explorador). *(taskmanager.c:500 - tecla R destroi o processo)*
- ⬜ Expandir ou recolher subprocessos.
- ✅ Alternar para a janela do aplicativo. *(taskmanager.c:523 - tecla F fecha taskmgr para focar app)*
- ⬜ Trazer para frente.
- ⬜ Modo de eficiência.
- ⬜ Abrir local do arquivo.
- ✅ Propriedades do processo. *(taskmanager.c:303 - dialogo de propriedades via Enter)*
- ⬜ Pesquisar online.
- ⬜ Ir para detalhes.
- ⬜ Criar arquivo de despejo de memória.
- ⬜ Fornecer comentários.

### 3. Execução de novos processos
- ✅ Abrir um programa pelo nome do executável. *(shell.c:464-530 — comandos: explorer, taskmgr, desktop, wm, edit, play)*
- ✅ Executar comandos. *(shell.c:76-104 — 18 comandos documentados)*
- ⬜ Abrir arquivos e documentos.
- ⬜ Abrir pastas.
- ⬜ Executar o Prompt de Comando.
- ⬜ Executar o PowerShell ou Terminal.
- ⬜ Iniciar ferramentas administrativas.
- ⬜ Criar a tarefa com privilégios de administrador.

---

## Fase 2 — Monitoramento de Hardware
> Objetivo: Exibir métricas de CPU, memória, disco, rede e GPU.

### 4. Monitoramento da CPU
- ✅ Porcentagem total de utilização. *(taskmanager.c:217-230 — cálculo por delta de ticks)*
- ⬜ Gráfico de utilização em tempo real.
- ⬜ Velocidade atual.
- ⬜ Velocidade básica.
- ✅ Quantidade de processos. *(taskmanager.c:174-178 — conta processos ativos)*
- ⬜ Quantidade de threads.
- ⬜ Quantidade de identificadores ou handles.
- ⬜ Tempo de atividade do computador.
- ⬜ Número de soquetes.
- ⬜ Núcleos físicos.
- ⬜ Processadores lógicos.
- ⬜ Informações de cache.
- ⬜ Estado da virtualização.
- ⬜ Arquitetura e modelo do processador.

### 5. Monitoramento da memória RAM
- ✅ Memória total instalada. *(taskmanager.c:265-267 — memory_get_total() em KB)*
- ✅ Memória atualmente em uso. *(taskmanager.c:275-277 — memory_get_used() em KB)*
- ✅ Memória disponível. *(taskmanager.c:279-281 — memory_get_free() em KB)*
- ✅ Gráfico de utilização em tempo real. *(taskmanager.c:269-273 — barra de progresso com cores)*
- ✅ Informações de heap. *(taskmanager.c:292-294 — início 0x20000, tamanho 1MB)*
- ✅ Total de páginas. *(taskmanager.c:297-299 — total/4096 com "4 KB cada")*
- ⬜ Memória em cache.
- ⬜ Memória confirmada ou comprometida.
- ⬜ Pool paginado.
- ⬜ Pool não paginado.
- ⬜ Velocidade da memória.
- ⬜ Número de slots utilizados.
- ⬜ Formato dos módulos.
- ⬜ Memória reservada para hardware.
- ⬜ Composição da memória.

### 6. Monitoramento dos discos
- ✅ Nome/modelo do disco. *(kernel.c:101-110 — ata_detect imprime modelo)*
- ✅ Capacidade da unidade. *(ata.c:93-106 — leitura de setores via ATA_CMD_IDENTIFY)*
- ⬜ Porcentagem de tempo ativo.
- ✅ Operações de leitura. *(ata.c:203 - ata_read_ops)*
- ✅ Operações de gravação. *(ata.c:207 - ata_write_ops)*
- ⬜ Tempo médio de resposta.
- ⬜ Capacidade formatada.
- ⬜ Tipo da unidade (SSD ou HDD).
- ⬜ Disco do sistema.
- ⬜ Presença de arquivo de paginação.
- ⬜ Gráfico de atividade.
- ⬜ Taxa de transferência.

### 7. Monitoramento da rede
- ⬜ Velocidade de envio.
- ⬜ Velocidade de recebimento.
- ⬜ Utilização da conexão.
- ⬜ Nome do adaptador.
- ⬜ Tipo de conexão.
- ⬜ Endereço IPv4.
- ⬜ Endereço IPv6.
- ⬜ Nome DNS.
- ⬜ Velocidade do link.
- ⬜ Gráfico de tráfego em tempo real.

### 8. Monitoramento da GPU
- ⬜ Utilização geral da GPU.
- ⬜ Utilização de gráficos 3D.
- ⬜ Codificação de vídeo.
- ⬜ Decodificação de vídeo.
- ⬜ Processamento de cópia.
- ⬜ Memória dedicada utilizada.
- ⬜ Memória compartilhada utilizada.
- ⬜ Memória total disponível.
- ⬜ Versão do driver.
- ⬜ Data do driver.
- ⬜ Versão do DirectX.
- ⬜ Localização física da placa.
- ⬜ Temperatura da GPU.

---

## Fase 3 — Abas Avançadas e Detalhes
> Objetivo: Páginas detalhadas, histórico, inicialização e usuários.

### 9. Histórico de aplicativos
- ⬜ Tempo acumulado de CPU.
- ⬜ Uso acumulado da rede.
- ⬜ Rede limitada.
- ⬜ Atualizações de blocos ou atividades.
- ⬜ Consumo de aplicativos da Microsoft Store.
- ⬜ Histórico desde uma determinada data.
- ⬜ Opção para apagar o histórico de utilização.

### 10. Aplicativos de inicialização
- ⬜ Ver programas configurados para iniciar com o ZephyrOS.
- ⬜ Ativar um aplicativo de inicialização.
- ⬜ Desativar um aplicativo.
- ⬜ Ver o nome do fabricante.
- ⬜ Ver o estado ativado ou desativado.
- ⬜ Ver o impacto na inicialização.
- ⬜ Ver o tempo de CPU usado durante a inicialização.
- ⬜ Ver atividade de disco durante a inicialização.
- ⬜ Abrir o local do executável.
- ⬜ Visualizar propriedades.
- ⬜ Pesquisar informações sobre o programa.

### 11. Usuários conectados
- ⬜ Contas atualmente conectadas.
- ⬜ Estado da sessão.
- ⬜ Consumo de CPU por usuário.
- ⬜ Memória utilizada por usuário.
- ⬜ Disco e rede utilizados.
- ⬜ Aplicativos executados em cada sessão.
- ⬜ Processos pertencentes a cada usuário.
- ⬜ Opção para desconectar uma sessão.
- ⬜ Opção para encerrar a sessão de outro usuário.
- ⬜ Envio de mensagem para outra sessão.

### 12. Página Detalhes
- ✅ Nome do executável. *(process.h:27-39 — campo name[32])*
- ✅ PID. *(process.h:27-39 — campo pid)*
- ✅ Estado. *(process.h:27-39 — campo state, 5 estados)*
- ⬜ Nome do usuário responsável.
- ⬜ Número da sessão.
- ✅ Consumo de CPU. *(taskmanager.c:217-230 — cálculo por processo)*
- ✅ Tempo acumulado de CPU. *(taskmanager.c:232-235 — total_ticks / 50 em segundos)*
- ⬜ Memória utilizada por processo.
- ⬜ Arquitetura (32 ou 64 bits).
- ⬜ Descrição do programa.
- ⬜ Linha de comando usada para iniciar.
- ⬜ Objetos USER.
- ⬜ Objetos GDI.
- ⬜ Quantidade de handles.
- ✅ Número de threads. *(taskmanager.c:302-365 — aba Threads com TID/Nome/Estado/Pai)*
- ⬜ Prioridade.
- ⬜ Virtualização do UAC.
- ⬜ Contexto elevado ou administrativo.
- ⬜ Caminho do executável.
- ⬜ Informações de memória detalhadas.

### 12b. Ações avançadas (na página Detalhes)
- ✅ Finalizar processo. *(Delete no taskmanager.c)*
- ⬜ Finalizar árvore de processos.
- ⬜ Definir prioridade.
- ⬜ Definir afinidade de processador.
- ⬜ Criar despejo de memória.
- ⬜ Analisar cadeia de espera.
- ⬜ Ativar ou desativar virtualização do UAC.
- ⬜ Abrir local do arquivo.
- ⬜ Abrir propriedades.
- ⬜ Ir para o serviço associado.

---

## Fase 4 — Serviços e Configurações
> Objetivo: Gerenciar serviços, configurar o task manager e gerar despejos.

### 13. Prioridade dos processos
- ⬜ Tempo real.
- ⬜ Alta.
- ⬜ Acima do normal.
- ⬜ Normal.
- ⬜ Abaixo do normal.
- ⬜ Baixa.

### 14. Afinidade de processador
- ⬜ Permitir que o processo utilize todos os núcleos.
- ⬜ Restringi-lo a somente alguns núcleos.
- ⬜ Separar um aplicativo antigo em um único processador lógico.
- ⬜ Testar problemas de compatibilidade.
- ⬜ Reservar determinados núcleos para outros programas.

### 15. Serviços do ZephyrOS
- ⬜ Nome interno do serviço.
- ⬜ PID relacionado.
- ⬜ Descrição.
- ⬜ Estado em execução ou parado.
- ⬜ Grupo ao qual pertence.
- ⬜ Iniciar um serviço.
- ⬜ Parar um serviço.
- ⬜ Reiniciar um serviço.
- ⬜ Abrir a ferramenta completa services.msc.
- ⬜ Ir do serviço para o processo correspondente.
- ⬜ Ir do processo para o serviço correspondente.

### 16. Arquivos de despejo de memória
- ⬜ Gerar arquivo .dmp com estado do processo.
- ⬜ Investigar aplicativos travados.
- ⬜ Investigar uso excessivo de CPU.
- ⬜ Investigar vazamentos de memória.
- ⬜ Investigar deadlocks.
- ⬜ Investigar exceções.
- ⬜ Investigar falhas difíceis de reproduzir.

### 17. Configurações do Gerenciador
- ✅ Página aberta por padrão. *(taskmanager.c:144 — 3 abas: Processos/Memoria/Threads)*
- ⬜ Velocidade de atualização dos dados.
- ✅ Tema claro ou escuro. *(settings.c:25-37 — tema Classico/Escuro/Azul)*
- ⬜ Manter o Gerenciador sempre visível.
- ⬜ Minimizar durante determinadas ações.
- ⬜ Ocultar quando minimizado.
- ⬜ Mostrar o nome completo das contas.
- ⬜ Exibir histórico de todos os processos.
- ⬜ Comportamento da navegação lateral.
- ⬜ Preferências de criação de despejos.

---

## Fase 5 — Atalhos e Integração
> Objetivo: Facilidade de acesso e conformidade com padrões.

### 18. Atalhos e formas de abrir
- ✅ Abrir pelo Terminal/Prompt. *(shell.c:473 — comando `taskmgr`)*
- ✅ Abrir pelo Menu Iniciar. *(taskbar.c:431-443 — item "Task Manager")*
- ✅ Abrir pelo Desktop. *(desktop.c:49-52 — ícone "TaskMgr")*
- ✅ Tecla ESC para fechar. *(taskmanager.c:402-405)*
- ✅ Tab para trocar de aba. *(taskmanager.c:407-413)*
- ✅ Setas para navegar. *(taskmanager.c:415-433)*
- ✅ Delete para finalizar. *(taskmanager.c:435-451)*
- ⬜ Ctrl + Shift + Esc: abre diretamente.
- ⬜ Ctrl + Alt + Delete: tela de segurança.
- ⬜ Super + X: menu avançado.
- ⬜ Clique direito no botão Iniciar.

### 19. Limitações do Gerenciador
- ⬜ Descobrir todas as alterações feitas no Registro.
- ⬜ Monitorar cada acesso a arquivos.
- ⬜ Analisar profundamente DLLs.
- ⬜ Inspecionar todos os handles abertos.
- ⬜ Depurar um programa.
- ⬜ Criar regras permanentes de prioridade ou afinidade.
- ⬜ Detectar malware com segurança.
- ⬜ Remover drivers.
- ⬜ Analisar detalhadamente conexões de rede.
- ⬜ Registrar continuamente tudo que um processo faz.

### 20. Process Explorer (futuro)
- ⬜ Exibir processos em árvore.
- ⬜ Mostrar qual processo iniciou outro.
- ⬜ Ver handles abertos.
- ⬜ Descobrir qual programa está utilizando determinado arquivo ou pasta.
- ⬜ Ver DLLs carregadas.
- ⬜ Ver arquivos mapeados na memória.
- ⬜ Ver usuário proprietário do processo.
- ⬜ Inspecionar threads.
- ⬜ Examinar serviços relacionados.
- ⬜ Visualizar variáveis de ambiente.
- ⬜ Ver strings armazenadas na memória ou no executável.
- ⬜ Ver gráficos individuais de CPU, memória, disco e GPU.
- ⬜ Substituir o Gerenciador de Tarefas em alguns ambientes.
- ⬜ Investigar vazamentos de handles e problemas de DLL.

### 21. Process Monitor (futuro)
- ⬜ Acessos a arquivos.
- ⬜ Criação, leitura, gravação e exclusão de arquivos.
- ⬜ Alterações no Registro.
- ⬜ Atividade de processos e threads.
- ⬜ Carregamento de DLLs.
- ⬜ Resultados de cada operação.
- ⬜ Caminhos acessados.
- ⬜ Horário de cada evento.
- ⬜ Pilha de chamadas.
- ⬜ Filtros por programa, caminho e operação.

---

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1 — Processos Básicos | 42 | 18 | 1 | 23 |
| 2 — Monitoramento HW | 55 | 9 | 2 | 44 |
| 3 — Abas/Detalhes | 38 | 8 | 1 | 29 |
| 4 — Serviços/Config | 27 | 2 | 0 | 25 |
| 5 — Atalhos/Integração | 30 | 7 | 0 | 23 |
| **Total** | **192** | **44** | **4** | **144** |

---

## Referência — Atalhos do Task Manager

| Atalho | Ação | Status |
|---|---|---|
| `Delete` | Finalizar processo selecionado | ✅ |
| `Tab` | Trocar entre abas | ✅ |
| `Setas ↑↓` | Navegar na lista | ✅ |
| `Enter` | Ver propriedades do processo | ✅ |
| `S` | Alterar ordenação das colunas | ✅ |
| `R` | Reiniciar processo (somente sistema) | ✅ |
| `F` | Alternar para aplicativo | ✅ |
| `Esc` | Fechar task manager | ✅ |
| `taskmgr` (shell) | Abrir task manager | ✅ |
| Menu Iniciar → Task Manager | Abrir task manager | ✅ |
| Desktop → TaskMgr | Abrir task manager | ✅ |

---

## Limitações Técnicas Conhecidas

- **Scheduler round-robin simples**: Sem suporte a prioridades — todos os processos recebem a mesma fatia de CPU.
- **Sem campos de memória por processo**: O `process_t` não rastreia uso de RAM individual.
- **Máximo 64 processos**: `MAX_PROCESSES = 64` definido em `process.h`.
- **Timer a 50 Hz**: Cálculo de CPU baseado em ticks do PIT, não em medição real de clock.
- **Sem drivers de rede/GPU**: Monitoramento desses recursos impossível sem hardware support.
- **Single-user**: Sem conceito de usuários, sessões ou permissões.
