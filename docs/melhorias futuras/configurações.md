# Roadmap — Configurações

> Visão geral do desenvolvimento dividida em **5 fases**.
> `✅` = Implementado | `🟡` = Parcial | `⬜` = Não implementado

---

## Fase 1 — Acesso e Estrutura Básica
> Objetivo: Painel de configurações funcional com navegação por teclado.

### Formas de abrir configurações
- ✅ Via comando `settings` no shell. *(shell.c:477-478)*
- ✅ Via Menu Iniciar → Configurações. *(taskbar.c:31,440 → shell.c:339-341)*
- ⬜ Botão direito no Menu Iniciar → Configurações.
- ⬜ Pesquisar diretamente pelo nome de uma configuração.
- ⬜ Usar comandos `ms-settings:` para abrir páginas específicas.

### Interface do painel
- ✅ Tela de categorias (sidebar esquerda). *(settings.c:155-167)*
- ✅ Painel de opções (área direita). *(settings.c:169-197)*
- ✅ Barra de título "Configurações do MiniOS". *(settings.c:155-156)*
- ✅ Barra de status com atalhos. *(settings.c:199-200)*
- ✅ Navegação por Tab entre categorias. *(settings.c:507-516)*
- ✅ Navegação por setas nas opções. *(settings.c:518-536)*
- ✅ Enter para editar. *(settings.c:538-542)*
- ✅ Esc para fechar. *(settings.c:502-504)*
- ✅ Tipos de opção: Toggle, Lista, Ação. *(settings.h:20-25)*

---

## Fase 2 — Sistema e Personalização
> Objetivo: Configurações de sistema, janelas, taskbar e ícones.

### 1. Sistema
- ✅ Nome do computador. *(settings.c:94-95 — exibe "MiniOS v0.1")*
- ✅ Informações de memória. *(settings.c:97-98 — total/livre/usado em KB)*
- ✅ Lista de processos ativos. *(settings.c:100-101 — lista com PID/nome/estado)*
- ✅ Reiniciar o computador. *(settings.c:103-104 — outb 0xFE, 0x64)*
- ⬜ Escolher o monitor principal.
- ⬜ Organizar vários monitores.
- ⬜ Duplicar ou estender a imagem.
- ⬜ Alterar resolução.
- ⬜ Alterar escala de textos e aplicativos.
- ⬜ Ajustar brilho.
- ⬜ Ativar luz noturna.
- ⬜ Configurar HDR.
- ⬜ Alterar orientação da tela.
- ⬜ Escolher taxa de atualização.
- ⬜ Configurar gráficos por aplicativo.
- ⬜ Ver informações do monitor e da placa de vídeo.
- ⬜ Detectar e conectar monitores sem fio.
- ⬜ Escolher alto-falantes ou fones de ouvido.
- ⬜ Escolher microfone.
- ⬜ Alterar volume geral.
- ⬜ Controlar volume por aplicativo.
- ⬜ Testar dispositivos.
- ⬜ Alterar formato e qualidade do áudio.
- ⬜ Ativar áudio mono.
- ⬜ Configurar áudio espacial.
- ⬜ Melhorar ou desativar aprimoramentos de áudio.
- ⬜ Solucionar problemas de som.
- ⬜ Definir dispositivos padrão de entrada e saída.
- ⬜ Ativar ou desativar notificações.
- ⬜ Escolher quais aplicativos podem notificar.
- ⬜ Exibir notificações na tela de bloqueio.
- ⬜ Ativar ou desativar sons de notificação.
- ⬜ Definir prioridades.
- ⬜ Ativar Não incomodar.
- ⬜ Criar regras automáticas.
- ⬜ Ocultar notificações durante jogos ou apresentações.
- ⬜ Escolher modo de energia.
- ⬜ Priorizar desempenho, equilíbrio ou economia.
- ⬜ Ver consumo de bateria por aplicativo.
- ⬜ Ativar economia de bateria.
- ⬜ Definir quando a tela será desligada.
- ⬜ Definir quando o computador entrará em suspensão.
- ⬜ Controlar suspensão e hibernação.
- ⬜ Receber recomendações de economia de energia.
- ⬜ Configurar o botão de energia.
- ⬜ Ver quanto espaço está ocupado.
- ⬜ Separar o consumo por categorias.
- ⬜ Apagar arquivos temporários.
- ⬜ Ativar o Sensor de Armazenamento.
- ⬜ Limpar automaticamente a Lixeira.
- ⬜ Receber recomendações de limpeza.
- ⬜ Escolher onde novos arquivos serão salvos.
- ⬜ Ver discos e volumes.
- ⬜ Gerenciar Espaços de Armazenamento.
- ⬜ Ver uso em outras unidades.
- ⬜ Gerenciar armazenamento reservado.
- ⬜ Ativar ou desativar o Snap.
- ⬜ Configurar layouts de janelas.
- ⬜ Controlar sugestões do Snap.
- ⬜ Configurar Alt + Tab.
- ⬜ Escolher quais janelas aparecem nas áreas virtuais.
- ⬜ Ativar a agitação da barra de título.
- ⬜ Definir comportamento de áreas de trabalho virtuais.
- ⬜ Sessões de foco.
- ⬜ Compartilhamento por proximidade.
- ⬜ Projeção neste computador.
- ⬜ Transmissão para telas sem fio.
- ⬜ Área de transferência e histórico.
- ⬜ Sincronização da área de transferência.
- ⬜ Área de Trabalho Remota.
- ⬜ Recursos opcionais.
- ⬜ Solução de problemas.
- ⬜ Recuperação e restauração do Windows.
- ⬜ Inicialização avançada.
- ⬜ Restaurar o computador.
- ⬜ Voltar para uma versão anterior.
- ✅ Informações do dispositivo. *(settings.c:94-95 — versão e nome)*
- ⬜ Especificações de CPU, RAM e sistema.
- ⬜ Ativação e chave do Windows.
- ⬜ Recursos para desenvolvedores.

### 2. Personalização
- ✅ Tema (Classico/Escuro/Azul). *(settings.c:27-30 — 3 opções)*
- ✅ Resolução (80x25/80x50/Auto). *(settings.c:31-34 — 3 opções)*
- ✅ Mostrar grade. *(settings.c:35-37 — toggle)*
- ⬜ Escolher imagem, cor sólida ou apresentação de slides.
- ⬜ Usar Windows Spotlight.
- ⬜ Escolher como a imagem se ajusta à tela.
- ⬜ Aplicar temas.
- ⬜ Salvar temas personalizados.
- ⬜ Alterar sons e ponteiros associados ao tema.
- ⬜ Escolher modo claro, escuro ou personalizado.
- ⬜ Alterar cor de destaque.
- ⬜ Exibir cor no Menu Iniciar e barra de tarefas.
- ⬜ Exibir cor nas barras de título.
- ⬜ Ativar ou desativar transparência.
- ⬜ Escolher imagem ou Spotlight na tela de bloqueio.
- ⬜ Exibir clima e outras informações.
- ⬜ Escolher aplicativos que mostram status.
- ⬜ Configurar proteção de tela.
- ⬜ Controlar quando a tela é desligada.
- ⬜ Mostrar aplicativos adicionados recentemente.
- ⬜ Mostrar aplicativos mais usados.
- ⬜ Mostrar arquivos recomendados.
- ⬜ Escolher pastas próximas ao botão de energia.
- ⬜ Controlar layout de itens fixados e recomendações.
- ⬜ Mostrar ou ocultar Pesquisa, Widgets e Visão de Tarefas.
- ⬜ Escolher ícones da bandeja.
- ⬜ Alinhar ícones ao centro ou à esquerda.
- ⬜ Mostrar distintivos e notificações piscantes.
- ⬜ Configurar agrupamento e rótulos.
- ⬜ Configurar a barra em vários monitores.
- ⬜ Ativar o botão Mostrar área de trabalho.
- ⬜ Instalar e remover fontes.
- ⬜ Configurar teclado virtual e entrada de texto.
- ⬜ Personalizar iluminação RGB.
- ⬜ Configurar informações de uso do dispositivo.
- ⬜ Alterar aparência de áreas virtuais.

### 3. Barra de tarefas
- ✅ Posição (Baixo/Cima/Esquerda/Direita/Custom). *(settings.c:41-44)*
- ✅ Tamanho do ícone (Pequeno/Médio/Grande). *(settings.c:45-48)*
- ✅ Fixada (toggle). *(settings.c:49-51)*
- ✅ Mostrar relógio (toggle). *(settings.c:52-54)*
- ✅ Auto-ocultar (toggle). *(settings.c:55-57)*
- ✅ Aplicar configurações em tempo real. *(settings.c:203-209)*

### 4. Janelas
- ✅ Posição dos botões (Direita/Esquerda). *(settings.c:61-64)*
- ✅ Ordem dos botões (6 combinações). *(settings.c:65-68)*
- ✅ Mostrar título (toggle). *(settings.c:69-71)*
- ✅ Estilo de borda (Simples/Dupla). *(settings.c:72-75)*
- ✅ Aplicar configurações em tempo real. *(settings.c:211-219)*

### 5. Ícones
- ✅ Editor de ícones do Desktop. *(settings.c:79-81 — icon_editor)*
- ✅ Editor de ícones da Janela (WM). *(settings.c:82-84)*
- ✅ Editor de ícones do File Manager. *(settings.c:85-87)*
- ✅ Restaurar padrão. *(settings.c:88-90 — icons_reset_defaults)*
- ✅ Personalização de caractere e cores. *(settings.c:233-311)*

---

## Fase 3 — Bluetooth, Rede e Contas
> Objetivo: Gerenciar periféricos, conectividade e usuários.

### 6. Bluetooth e dispositivos
- ⬜ Ativar ou desativar Bluetooth.
- ⬜ Procurar dispositivos.
- ⬜ Emparelhar fones, caixas de som, controles e celulares.
- ⬜ Remover dispositivos.
- ⬜ Ver nível de bateria de equipamentos compatíveis.
- ⬜ Gerenciar conexões Bluetooth.
- ⬜ Adicionar uma impressora.
- ⬜ Remover uma impressora.
- ⬜ Definir impressora padrão.
- ⬜ Abrir fila de impressão.
- ⬜ Imprimir página de teste.
- ⬜ Gerenciar propriedades.
- ⬜ Adicionar scanners.
- ⬜ Solucionar problemas de impressão.
- ⬜ Conectar Android ou iPhone.
- ⬜ Configurar o Vincular ao Celular.
- ⬜ Mostrar fotos e notificações do celular.
- ⬜ Usar o celular como câmera.
- ⬜ Controlar quais dispositivos móveis podem acessar o computador.
- ⬜ Configurar mouse.
- ⬜ Alterar velocidade do ponteiro.
- ⬜ Trocar botão principal.
- ⬜ Configurar rolagem.
- ⬜ Configurar touchpad e gestos.
- ⬜ Configurar teclado virtual.
- ⬜ Configurar caneta e Windows Ink.
- ⬜ Configurar telas sensíveis ao toque.
- ⬜ Gerenciar câmeras.
- ⬜ Gerenciar dispositivos de áudio.
- ⬜ Configurar reprodução automática.
- ⬜ Controlar avisos e comportamento de dispositivos USB.

### 7. Rede e Internet
- ⬜ Ativar ou desativar Wi-Fi.
- ⬜ Ver redes disponíveis.
- ⬜ Conectar e desconectar.
- ⬜ Esquecer redes salvas.
- ⬜ Gerenciar redes conhecidas.
- ⬜ Definir conexão automática.
- ⬜ Definir rede pública ou privada.
- ⬜ Consultar propriedades da conexão.
- ⬜ Configurar endereço IP e DNS.
- ⬜ Ver estado da conexão por cabo.
- ⬜ Configurar IP manual ou automático.
- ⬜ Configurar DNS.
- ⬜ Ver velocidade do link.
- ⬜ Consultar endereços de rede.
- ⬜ Configurar VPN.
- ⬜ Criar ponto de acesso móvel.
- ⬜ Ativar modo avião.
- ⬜ Configurar proxy.
- ⬜ Configurar acesso discado.
- ⬜ Ver consumo de dados.
- ⬜ Definir limite de dados.
- ⬜ Definir conexão limitada.
- ⬜ Alterar adaptadores de rede.
- ⬜ Redefinir completamente a rede.
- ⬜ Consultar propriedades de hardware.
- ⬜ Configurar compartilhamento e descoberta de rede.
- ⬜ Executar solução de problemas.

### 8. Contas
- ⬜ Ver a conta atualmente conectada.
- ⬜ Usar conta local ou Microsoft.
- ⬜ Alterar foto da conta.
- ⬜ Acessar informações da conta Microsoft.
- ⬜ Ver status de administrador.
- ⬜ Configurar PIN.
- ⬜ Configurar reconhecimento facial.
- ⬜ Configurar impressão digital.
- ⬜ Alterar senha.
- ⬜ Usar chave de segurança.
- ⬜ Configurar senha de imagem.
- ⬜ Exigir entrada ao sair da suspensão.
- ⬜ Configurar bloqueio dinâmico.
- ⬜ Definir reinício automático de aplicativos.
- ⬜ Escolher quando o Windows solicitar nova autenticação.
- ⬜ Criar conta de usuário.
- ⬜ Adicionar conta Microsoft.
- ⬜ Criar conta local.
- ⬜ Alterar tipo entre padrão e administrador.
- ⬜ Remover usuário.
- ⬜ Configurar Microsoft Family.
- ⬜ Criar conta infantil.
- ⬜ Configurar acesso atribuído ou modo quiosque.
- ⬜ Adicionar contas de e-mail.
- ⬜ Conectar conta profissional ou escolar.
- ⬜ Entrar ou sair de uma organização.
- ⬜ Gerenciar contas utilizadas por aplicativos.
- ⬜ Sincronizar configurações.
- ⬜ Configurar Backup do Windows.
- ⬜ Sincronizar arquivos e preferências pela conta Microsoft.

---

## Fase 4 — Hora, Jogos, Acessibilidade e Privacidade
> Objetivo: Idioma, jogos, acessibilidade e segurança.

### 9. Hora e idioma
- ⬜ Ajustar data e hora automaticamente.
- ⬜ Alterar data e hora manualmente.
- ⬜ Definir fuso horário.
- ⬜ Ajustar automaticamente o horário de verão.
- ⬜ Sincronizar com servidor de horário.
- ⬜ Mostrar calendários adicionais.
- ⬜ Alterar idioma de exibição.
- ⬜ Instalar pacotes de idioma.
- ⬜ Alterar país ou região.
- ⬜ Definir formato regional.
- ⬜ Gerenciar teclados.
- ⬜ Adicionar layouts de teclado.
- ⬜ Alterar idioma de aplicativos e sites.
- ⬜ Gerenciar recursos de reconhecimento de escrita.
- ⬜ Ativar correção automática.
- ⬜ Mostrar sugestões de texto.
- ⬜ Ativar sugestões multilíngues.
- ⬜ Configurar teclado virtual.
- ⬜ Personalizar dicionário.
- ⬜ Configurar digitação por voz.
- ⬜ Escolher idioma de reconhecimento de fala.
- ⬜ Configurar microfone para reconhecimento de voz.
- ⬜ Gerenciar vozes instaladas.

### 10. Jogos
- ⬜ Ativar e configurar a Barra de Jogo.
- ⬜ Definir atalhos para captura.
- ⬜ Gravar jogos e aplicativos.
- ⬜ Capturar tela.
- ⬜ Escolher pasta de gravações.
- ⬜ Configurar qualidade e taxa de quadros.
- ⬜ Gravar áudio e microfone.
- ⬜ Ativar Modo de Jogo.
- ⬜ Reduzir atividades em segundo plano durante jogos.
- ⬜ Configurar recursos gráficos relacionados a jogos.
- ⬜ Permitir que controles abram a Barra de Jogo.

### 11. Acessibilidade
- ⬜ Alterar tamanho do texto.
- ⬜ Alterar tamanho do ponteiro.
- ⬜ Alterar cor do ponteiro.
- ⬜ Configurar cursor de texto.
- ⬜ Ativar Lupa.
- ⬜ Usar filtros de cor.
- ⬜ Ativar temas de contraste.
- ⬜ Configurar Narrador.
- ⬜ Reduzir animações e transparência.
- ⬜ Ajustar barras de rolagem.
- ⬜ Ativar áudio mono.
- ⬜ Mostrar alertas visuais para sons.
- ⬜ Configurar aparelhos auditivos compatíveis.
- ⬜ Ativar e personalizar legendas ao vivo.
- ⬜ Alterar aparência das legendas.
- ⬜ Configurar Acesso por Voz.
- ⬜ Usar reconhecimento de fala.
- ⬜ Configurar Teclas de Aderência.
- ⬜ Configurar Teclas de Filtragem.
- ⬜ Usar teclado virtual.
- ⬜ Controlar o mouse pelo teclado.
- ⬜ Personalizar gestos de toque.
- ⬜ Configurar controle ocular.

### 12. Privacidade e segurança
- ⬜ Abrir o antivírus Microsoft Defender.
- ⬜ Ver proteção contra vírus e ameaças.
- ⬜ Executar verificações.
- ⬜ Controlar firewall.
- ⬜ Ver proteção de conta.
- ⬜ Controlar aplicativos e navegador.
- ⬜ Ver segurança do dispositivo.
- ⬜ Ativar isolamento de núcleo.
- ⬜ Consultar integridade e desempenho.
- ⬜ Configurar proteção contra ransomware.
- ⬜ Controlar notificações de segurança.
- ⬜ Ativar Criptografia de Dispositivo.
- ⬜ Acessar BitLocker.
- ⬜ Configurar Localizar meu dispositivo.
- ⬜ Controlar serviços de localização.
- ⬜ Limpar histórico de localização.
- ⬜ Controlar permissões de localização.
- ⬜ Controlar permissões de câmera.
- ⬜ Controlar permissões de microfone.
- ⬜ Controlar permissões de notificações.
- ⬜ Controlar permissões de informações da conta.
- ⬜ Controlar permissões de contatos.
- ⬜ Controlar permissões de calendário.
- ⬜ Controlar permissões de chamadas.
- ⬜ Controlar permissões de e-mail.
- ⬜ Controlar permissões de tarefas.
- ⬜ Controlar permissões de mensagens.
- ⬜ Controlar permissões de rádios e Bluetooth.
- ⬜ Controlar permissões de outros dispositivos.
- ⬜ Controlar permissões do sistema de arquivos.
- ⬜ Controlar permissões de Downloads, Documentos, Imagens, Vídeos e Música.
- ⬜ Escolher quais dados de diagnóstico são enviados.
- ⬜ Ver dados de diagnóstico.
- ⬜ Excluir dados de diagnóstico.
- ⬜ Controlar experiências personalizadas.
- ⬜ Gerenciar histórico de atividades.
- ⬜ Controlar ID de publicidade.
- ⬜ Configurar pesquisas e permissões de pesquisa.
- ⬜ Controlar indexação de arquivos.

---

## Fase 5 — Updates, Som e Ferramentas Clássicas
> Objetivo: Atualizações, configurações de som avançadas e ferramentas do sistema.

### 13. Windows Update
- ⬜ Procurar atualizações.
- ⬜ Baixar e instalar atualizações.
- ⬜ Reiniciar para concluir instalação.
- ⬜ Agendar reinicialização.
- ⬜ Pausar atualizações.
- ⬜ Definir horário de atividade.
- ⬜ Ver histórico.
- ⬜ Desinstalar determinadas atualizações.
- ⬜ Receber atualizações de outros produtos Microsoft.
- ⬜ Escolher atualizações opcionais.
- ⬜ Instalar drivers opcionais.
- ⬜ Configurar Otimização de Entrega.
- ⬜ Limitar largura de banda.
- ⬜ Compartilhar atualizações com outros computadores.
- ⬜ Participar do Programa Windows Insider.
- ⬜ Ver versão e compilação do sistema.
- ⬜ Receber atualizações assim que estiverem disponíveis.
- ⬜ Usar recuperação para corrigir problemas de atualização.

### 14. Som
- ✅ Volume (Mudo/Baixo/Médio/Alto/Máximo). *(settings.c:109-112)*
- ✅ Beep ao iniciar (toggle). *(settings.c:113-115)*
- ✅ Som do teclado (toggle). *(settings.c:116-118)*
- ⬜ Escolher alto-falantes ou fones.
- ⬜ Escolher microfone.
- ⬜ Controlar volume por aplicativo.
- ⬜ Testar dispositivos.
- ⬜ Alterar formato e qualidade do áudio.
- ⬜ Ativar áudio mono.
- ⬜ Configurar áudio espacial.
- ⬜ Melhorar ou desativar aprimoramentos de áudio.
- ⬜ Solucionar problemas de som.
- ⬜ Definir dispositivos padrão de entrada e saída.

### 15. Sobre
- ✅ Versão. *(settings.c:122-124 — "MiniOS v0.1")*
- ✅ Créditos. *(settings.c:125-127 — Kernel, Drivers, FS, Interface)*

### 16. Ferramentas clássicas
- ⬜ Painel de Controle.
- ⬜ Gerenciador de Dispositivos.
- ⬜ Gerenciamento de Disco.
- ⬜ Gerenciamento do Computador.
- ⬜ Serviços.
- ⬜ Configuração do Sistema — msconfig.
- ⬜ Visualizador de Eventos.
- ⬜ Informações do Sistema — msinfo32.
- ⬜ Propriedades do Sistema.
- ⬜ Editor do Registro.
- ⬜ Política de Grupo.
- ⬜ Segurança Local.
- ⬜ Agendador de Tarefas.

---

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1 — Acesso/Estrutura | 14 | 12 | 0 | 2 |
| 2 — Sistema/Personalização | 101 | 18 | 0 | 83 |
| 3 — Bluetooth/Rede/Contas | 88 | 0 | 0 | 88 |
| 4 — Hora/Jogos/Acess./Priv. | 103 | 0 | 0 | 103 |
| 5 — Updates/Som/Clássicas | 44 | 4 | 0 | 40 |
| **Total** | **350** | **34** | **0** | **316** |

---

## Referência — Categorias Implementadas

| Categoria | Itens | Status |
|-----------|-------|--------|
| Tela | 3 | ✅ Tema, Resolução, Grade |
| Barra de Tarefas | 5 | ✅ Posição, Tamanho, Fixar, Relógio, Auto-ocultar |
| Janelas | 4 | ✅ Botões, Ordem, Título, Borda |
| Ícones | 4 | ✅ Desktop, WM, FM, Restaurar |
| Sistema | 4 | ✅ Nome, Memória, Processos, Reiniciar |
| Som | 3 | ✅ Volume, Beep, Som teclado |
| Sobre | 2 | ✅ Versão, Créditos |

---

## Limitações Técnicas Conhecidas

- **7 categorias** implementadas de 12 do Windows (Tela, Taskbar, Janelas, Ícones, Sistema, Som, Sobre).
- **5 categorias completas** (Bluetooth/Rede/Contas/Hora/Jogos/Acessibilidade/Privacidade/Updates) — nenhuma implementada.
- **Opções sem efeito real**: Tema, Resolução, Volume e Beep são definidas mas não alteram o comportamento do sistema.
- **Sem pesquisa**: Não é possível pesquisar por configurações.
- **Sem `ms-settings:`**: Não há protocolo de URL para abrir páginas específicas.
- **Sem ferramentas clássicas**: Painel de Controle, Gerenciador de Dispositivos, etc. não existem.
