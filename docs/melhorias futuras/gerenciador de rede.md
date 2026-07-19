Sim. Assim como nos outros componentes, o Windows não possui um único aplicativo chamado Gerenciador de Rede. O gerenciamento nativo é dividido entre:

Configurações → Rede e Internet
Configurações Rápidas da barra de tarefas
Conexões de Rede (ncpa.cpl)
Central de Rede e Compartilhamento
Firewall do Windows
Gerenciador de Dispositivos
Ferramentas como ipconfig, ping, netstat, netsh e PowerShell
1. Estado geral da conexão

A página Rede e Internet mostra:

Se o computador está conectado.
Se existe acesso à internet.
Qual conexão está sendo usada.
Nome da rede.
Wi-Fi, Ethernet, rede celular ou VPN.
Endereço IPv4 e IPv6.
Servidores DNS.
Gateway.
Velocidade do link.
Endereço físico ou MAC.
Fabricante e descrição do adaptador.
Quantidade de dados utilizados.

Ela pode ser aberta com Windows + I ou clicando com o botão direito no ícone de rede da barra de tarefas.

2. Gerenciamento do Wi-Fi

O Windows permite:

Ativar ou desativar o Wi-Fi.
Ver redes disponíveis.
Conectar-se a uma rede.
Desconectar-se.
Digitar e armazenar a senha.
Conectar automaticamente quando a rede estiver disponível.
Exibir a senha de uma rede salva.
Esquecer redes conhecidas.
Escolher quais redes devem conectar automaticamente.
Ver intensidade do sinal.
Ver padrão e velocidade da conexão.
Definir a rede como pública ou privada.
Configurar IP e DNS.
Usar endereço MAC aleatório em hardware compatível.
Gerenciar redes salvas mesmo quando não estão próximas.

Os endereços de hardware aleatórios podem ser usados globalmente ou em uma rede Wi-Fi específica para dificultar o rastreamento do dispositivo.

3. Gerenciamento da Ethernet

Para conexões por cabo, é possível:

Ver se o cabo está conectado.
Ver velocidade negociada, como 100 Mbps, 1 Gbps ou 2,5 Gbps.
Definir perfil público ou privado.
Configurar endereço IP.
Configurar gateway.
Configurar DNS.
Ativar conexão limitada.
Consultar endereço MAC.
Ver nome e fabricante do adaptador.
Desativar ou reativar o adaptador.
Diagnosticar falhas.
Redefinir configurações.

A página também pode indicar mensagens como Conectado, Sem internet ou Ação necessária.

4. Perfis público e privado

Cada conexão pode receber um perfil:

Rede pública

Indicada para:

Aeroportos.
Hotéis.
Cafés.
Redes desconhecidas.
Wi-Fi compartilhado.

Nesse perfil, o computador fica menos visível para outros dispositivos e as regras do firewall são normalmente mais restritivas.

Rede privada

Indicada para uma rede doméstica ou confiável. Ela permite:

Descoberta de rede.
Compartilhamento de arquivos.
Compartilhamento de impressoras.
Comunicação mais fácil entre computadores locais.

O Windows 11 define novas redes como públicas por padrão.

5. Configuração de IP

O Windows pode receber configurações automaticamente por DHCP ou usar valores manuais.

É possível definir:

IPv4.
IPv6.
Endereço IP.
Máscara de sub-rede.
Comprimento do prefixo IPv6.
Gateway padrão.
DNS preferencial.
DNS alternativo.
Configuração automática ou manual.

O DHCP normalmente recebe essas informações do roteador. A configuração manual é usada em servidores locais, impressoras, laboratórios, redes empresariais ou equipamentos que precisam manter o mesmo endereço.

6. Configuração de DNS

O gerenciador permite:

Usar o DNS fornecido automaticamente.
Definir DNS preferencial e alternativo.
Configurar DNS separadamente para IPv4 e IPv6.
Usar DNS público ou corporativo.
Limpar o cache DNS por comando.
Consultar os servidores DNS atuais.
Usar DNS sobre HTTPS — DoH no Windows 11.
Permitir ou impedir retorno para DNS não criptografado.

O Windows 11 oferece DNS sobre HTTPS com modelo automático ou endereço de modelo configurado manualmente; essa opção gráfica não está disponível no Windows 10.

7. Adaptadores de rede

A página Configurações avançadas de rede mostra adaptadores como:

Wi-Fi.
Ethernet.
Bluetooth PAN.
Adaptadores de VPN.
Adaptadores virtuais.
Hyper-V.
VMware e VirtualBox.
WSL.
Pontes de rede.
Adaptadores USB.
Rede celular.

Para cada adaptador, é possível:

Ativar.
Desativar.
Ver propriedades.
Ver utilização.
Renomear a conexão em determinados contextos.
Abrir as propriedades clássicas.
Configurar protocolos.
Diagnosticar problemas.
Ver informações de hardware.
8. Conexões de Rede clássicas

A ferramenta ncpa.cpl oferece controles mais técnicos:

Ativar ou desativar adaptadores.
Ver o estado da conexão.
Renomear uma conexão.
Abrir propriedades.
Configurar IPv4 e IPv6.
Instalar ou remover protocolos e serviços.
Ativar compartilhamento de conexão.
Criar ponte entre adaptadores.
Ver velocidade e duração da conexão.
Ver pacotes enviados e recebidos.
Executar diagnóstico.
Alterar métricas e opções avançadas de TCP/IP.

Ela continua existindo porque algumas opções ainda não foram completamente migradas para o aplicativo Configurações.

9. Propriedades avançadas do adaptador

Pelo Gerenciador de Dispositivos, adaptadores compatíveis podem oferecer configurações como:

Velocidade e duplex.
Auto negociação.
Wake-on-LAN.
Jumbo Frames.
Offload de checksum.
Receive Side Scaling.
Energy Efficient Ethernet.
Prioridade e VLAN.
Tamanho dos buffers.
Roaming do Wi-Fi.
Banda preferida.
Largura do canal.
Padrões Wi-Fi permitidos.
Potência de transmissão.
Economia de energia.

Essas opções dependem totalmente do hardware e do driver. Alterações incorretas podem reduzir desempenho ou interromper a conexão.

10. VPN nativa

Em Rede e Internet → VPN, é possível:

Adicionar uma conexão VPN.
Informar servidor ou endereço.
Escolher o tipo de VPN.
Configurar nome de usuário e senha.
Usar certificado ou outros métodos de autenticação.
Conectar e desconectar.
Salvar credenciais.
Configurar proxy exclusivo para a VPN.
Remover ou editar uma conexão.
Acessar redes corporativas remotamente.

O cliente nativo funciona quando o serviço fornece configurações compatíveis. Alguns provedores exigem um aplicativo próprio para protocolos, bloqueios, seleção automática de servidores e outros recursos.

11. Proxy

O Windows pode configurar um servidor intermediário para o tráfego de aplicativos compatíveis.

As opções incluem:

Detectar configurações automaticamente.
Usar script de configuração.
Informar endereço e porta manualmente.
Criar lista de exceções.
Ignorar o proxy para endereços locais.
Usar um proxy diferente para uma VPN.
Ativar ou desativar rapidamente a configuração.

Isso é comum em empresas, escolas, filtros de conteúdo e redes que inspecionam ou controlam o acesso à internet.

12. Hotspot móvel

O Windows consegue transformar o computador em um ponto de acesso.

Ele pode compartilhar uma conexão proveniente de:

Ethernet.
Wi-Fi.
Rede celular.

O compartilhamento pode ser feito por:

Wi-Fi.
Bluetooth, em equipamentos compatíveis.

Também é possível:

Definir nome da rede.
Definir senha.
Escolher a banda.
Mostrar um código QR.
Ver quantos dispositivos estão conectados.
Ativar pelo painel rápido.
Ligar remotamente em determinadas configurações.

O Wi-Fi normalmente oferece maior velocidade que o compartilhamento por Bluetooth.

13. Rede celular e eSIM

Em computadores compatíveis, a categoria Celular pode:

Ativar ou desativar dados móveis.
Escolher a operadora.
Configurar APN.
Escolher entre rede celular e Wi-Fi.
Ativar roaming.
Ver consumo de dados.
Definir limite mensal.
Gerenciar SIM ou eSIM.
Inserir ou alterar PIN do SIM.
Consultar intensidade e tipo do sinal.

Essas opções só aparecem quando o computador possui modem celular compatível.

14. Modo avião

O modo avião permite desligar rapidamente comunicações sem fio, incluindo:

Wi-Fi.
Bluetooth.
Rede celular.
NFC, quando presente.

Depois de ativar o modo avião, o usuário pode reativar individualmente determinadas conexões. O Windows também pode memorizar quais rádios permaneceram ativos na última utilização.

15. Uso de dados

O Windows registra o tráfego utilizado por:

Wi-Fi.
Ethernet.
Rede celular.
Aplicativos individuais.

As funções incluem:

Ver consumo total.
Ver consumo por aplicativo.
Escolher um período.
Redefinir estatísticas.
Criar limite de dados.
Receber alertas ao se aproximar do limite.
Identificar programas que usam muita internet.

O limite é principalmente informativo; ele não funciona como um bloqueador completo de tráfego para todos os aplicativos.

16. Conexão limitada

Uma conexão pode ser marcada como limitada para reduzir o uso de dados.

Isso pode fazer o Windows:

Reduzir downloads automáticos.
Limitar determinadas sincronizações.
Alterar o comportamento de aplicativos.
Adiar algumas atualizações.
Reduzir atividades em segundo plano.

Wi-Fi e Ethernet podem ser definidos manualmente como limitados; conexões celulares normalmente já são tratadas dessa forma.

17. Descoberta de rede

A descoberta permite que o computador:

Encontre outros PCs.
Encontre servidores e NAS.
Encontre impressoras.
Apareça para outros equipamentos.
Acesse dispositivos multimídia.
Navegue pelos recursos locais.

Ela é normalmente ativada apenas em redes privadas. O funcionamento também depende de serviços do Windows e das regras do firewall.

18. Compartilhamento de arquivos e impressoras

O Windows pode:

Compartilhar pastas.
Compartilhar arquivos.
Compartilhar impressoras.
Definir usuários autorizados.
Definir leitura ou leitura e gravação.
Exigir usuário e senha.
Acessar caminhos como \\computador\pasta.
Mapear uma pasta como unidade.
Reconectar unidades ao entrar no Windows.
Acessar servidores e NAS compatíveis com SMB.

Para compartilhamento local, normalmente é necessário usar perfil privado e ativar Descoberta de rede e Compartilhamento de arquivos e impressoras.

19. Firewall do Windows

O firewall nativo:

Filtra tráfego de entrada e saída.
Bloqueia ou permite aplicativos.
Controla conexões por porta.
Controla protocolos.
Usa endereço IP de origem e destino.
Aplica regras diferentes para redes públicas, privadas e de domínio.
Pode registrar conexões permitidas ou bloqueadas.
Trabalha com IPsec.
Pode exigir autenticação ou criptografia entre computadores.

Ele é incluído no Windows e vem ativado por padrão.

Firewall com Segurança Avançada

A ferramenta wf.msc permite:

Criar regras de entrada.
Criar regras de saída.
Liberar ou bloquear portas.
Criar regras por programa.
Restringir endereços IP.
Escolher protocolos.
Aplicar regras por perfil.
Configurar regras de segurança de conexão.
Configurar IPsec.
Importar ou exportar políticas.
Registrar pacotes descartados.
Administrar regras por Política de Grupo.

A interface básica fica em firewall.cpl, enquanto wf.msc oferece os controles avançados.

20. Monitoramento de rede

O Windows possui várias formas de monitorar a conexão:

Gerenciador de Tarefas
Uso total da rede.
Velocidade de envio e recebimento.
Aplicativos que mais usam a conexão.
Desempenho de cada adaptador.
Monitor de Recursos
Processos com atividade de rede.
Endereços remotos.
Conexões TCP.
Portas de escuta.
Quantidade enviada e recebida.
Latência de conexões TCP.
Monitor de Desempenho
Contadores de bytes e pacotes.
Erros.
Descartes.
Utilização do adaptador.
Monitoramento e registro prolongado.
Firewall
Eventos e registros de conexões bloqueadas ou autorizadas.
21. Diagnóstico automático

O solucionador de problemas pode verificar:

Adaptador desativado.
Falha de driver.
Ausência de endereço IP.
Problemas com DHCP.
Problemas de DNS.
Gateway inacessível.
Configuração de proxy.
Ausência de internet.
Falhas de Wi-Fi ou Ethernet.
Serviços de rede parados.

Ele tenta corrigir automaticamente problemas comuns ou informa a possível causa.

22. Redefinição da rede

A opção Redefinição de rede:

Remove os adaptadores instalados.
Remove suas configurações.
Reinicia o computador.
Reinstala os adaptadores.
Restaura configurações padrão.
Pode corrigir falhas depois de atualizações.
Pode resolver problemas com unidades compartilhadas.

Ela deve ser usada como uma das últimas alternativas. Depois da redefinição, pode ser necessário reinstalar VPNs, switches virtuais e outros softwares de rede. As redes também podem voltar ao perfil público.

23. Comandos de diagnóstico
Comando	Função principal
ipconfig	Mostrar IP, gateway e adaptadores
ipconfig /all	Mostrar configuração completa
ipconfig /release	Liberar endereço DHCP
ipconfig /renew	Solicitar novo endereço
ipconfig /flushdns	Limpar cache DNS
ping	Testar comunicação e latência
tracert	Mostrar a rota até um destino
pathping	Examinar rota e perda de pacotes
nslookup	Consultar servidores DNS
netstat	Mostrar conexões, portas e estatísticas
arp	Mostrar a tabela IP/MAC local
route print	Mostrar tabela de roteamento
netsh	Configurar e redefinir componentes de rede
getmac	Mostrar endereços MAC
hostname	Mostrar o nome do computador

A Microsoft descreve ping como uma ferramenta básica para testar conectividade e resolução de nomes; netstat mostra conexões TCP, portas de escuta e estatísticas, enquanto tracert e pathping analisam a rota até o destino.

24. Gerenciamento por PowerShell

O Windows também possui módulos de rede para automação:

Get-NetAdapter
Get-NetIPConfiguration
Get-NetIPAddress
Get-NetTCPConnection
Test-NetConnection
Get-NetRoute
Get-DnsClientServerAddress
Get-NetFirewallRule

Esses comandos permitem consultar adaptadores, endereços, DNS, rotas, conexões TCP e regras de firewall. Também existem comandos para alterar essas configurações, normalmente com privilégios administrativos.

25. O que o Windows não administra completamente

O gerenciador nativo geralmente não substitui:

Painel administrativo do roteador.
Configuração de portas do switch.
Controle completo de QoS.
Análise detalhada do espectro Wi-Fi.
Captura avançada de pacotes.
Bloqueio individual de dispositivos no roteador.
Controle parental de toda a rede.
Criação avançada de VLANs em todos os adaptadores.
Monitoramento do tráfego de outros dispositivos.
Configuração de NAT e encaminhamento de portas do roteador.
Sistemas profissionais de detecção de intrusão.

Para essas funções são usadas ferramentas como o painel do roteador, Wireshark, software do fabricante do adaptador ou soluções empresariais.

Resumo
Componente	Principal função
Rede e Internet	Configurações gerais de conexão
Wi-Fi/Ethernet	Conectar e configurar redes
Configurações avançadas	Gerenciar adaptadores
Central de Rede	Compartilhamento e controles clássicos
VPN	Acesso remoto e túnel seguro
Proxy	Encaminhar tráfego por servidor intermediário
Hotspot móvel	Compartilhar a conexão
Firewall	Filtrar tráfego de entrada e saída
Monitor de Recursos	Investigar uso e conexões
Redefinição de rede	Restaurar a pilha e os adaptadores
CMD e PowerShell	Diagnóstico e automação avançada