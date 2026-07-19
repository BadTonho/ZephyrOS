Sim. O gerenciamento nativo de dispositivos e drivers do Windows não fica em uma única ferramenta. Ele é formado principalmente por:

Gerenciador de Dispositivos: interface gráfica.
Plug and Play — PnP: detecta e configura o hardware.
Windows Update: distribui drivers certificados.
Driver Store: repositório local de pacotes de drivers.
PnPUtil: gerenciamento avançado por linha de comando.
1. Visualização dos dispositivos

O Gerenciador de Dispositivos lista o hardware reconhecido pelo Windows, organizado em categorias como:

Adaptadores de vídeo.
Adaptadores de rede.
Bluetooth.
Câmeras.
Controladores de áudio, vídeo e jogos.
Controladores USB.
Dispositivos de interface humana.
Dispositivos de sistema.
Impressoras e filas de impressão.
Monitores.
Processadores.
Teclados e mouses.
Unidades de disco.
Unidades de armazenamento.
Firmware.
Baterias.
Portas COM e LPT, quando presentes.
Outros dispositivos ainda não identificados.

A lista pode apresentar tanto componentes físicos quanto dispositivos virtuais criados por drivers, máquinas virtuais, VPNs, softwares de áudio e outros programas.

Formas de organização

Dependendo da versão e do hardware, é possível visualizar:

Dispositivos por tipo.
Dispositivos por conexão.
Dispositivos por contêiner.
Recursos por tipo.
Recursos por conexão.
Dispositivos ocultos.
Dispositivos que já estiveram conectados.
Componentes internos da estrutura Plug and Play.
2. Estado do dispositivo

O Gerenciador mostra se o componente está funcionando corretamente.

Os indicadores mais comuns incluem:

Nenhum símbolo: dispositivo aparentemente funcionando.
Triângulo amarelo: problema de configuração, driver ou funcionamento.
Seta para baixo: dispositivo desativado.
Dispositivo esmaecido: dispositivo oculto, desconectado ou não presente.
Dispositivo desconhecido: Windows não conseguiu identificar o hardware ou localizar um driver adequado.

Na página Geral das propriedades, aparece uma mensagem de estado e, em caso de falha, um código como:

Código 10: dispositivo não pode ser iniciado.
Código 22: dispositivo desativado.
Código 28: drivers não instalados.
Código 31: Windows não consegue carregar o driver necessário.
Código 39: driver ausente ou corrompido.
Código 43: o próprio dispositivo informou uma falha.
Código 45: hardware atualmente desconectado.

A Microsoft mantém uma lista extensa desses códigos, e as soluções variam entre reinstalar o driver, reconectar o hardware, reiniciar ou verificar o fabricante.

3. Informações do dispositivo

Ao abrir as propriedades de um componente, podem aparecer várias páginas.

Geral
Nome do dispositivo.
Tipo ou categoria.
Fabricante informado.
Localização física ou lógica.
Estado atual.
Código de erro.
Driver
Fabricante do driver.
Data do driver.
Versão.
Assinatura digital.
Arquivos utilizados.
Provedor do pacote.
Botões de atualização, reversão e desinstalação.
Detalhes

Permite consultar propriedades técnicas como:

IDs de hardware.
IDs compatíveis.
ID da instância do dispositivo.
Classe do dispositivo.
GUID da classe.
Nome do serviço do driver.
Nome do enumerador.
Caminho da instância.
Relações com dispositivos pais e filhos.
Barramento utilizado.
Número de localização.
Estado Plug and Play.
Recursos exigidos.
Nome físico do dispositivo.
Informações do contêiner.

Os IDs de hardware são especialmente úteis para identificar componentes desconhecidos e encontrar o driver exato.

Eventos

Pode exibir um histórico relacionado à configuração do dispositivo:

Instalação solicitada.
Dispositivo configurado.
Driver instalado.
Driver migrado durante uma atualização do Windows.
Inicialização do dispositivo.
Falha na configuração.
Necessidade de reinicialização.
Recursos

Em dispositivos compatíveis, pode mostrar:

Intervalos de memória.
Portas de entrada e saída.
Solicitações de interrupção, ou IRQ.
Canais de DMA.
Conflitos com outros dispositivos.

Algumas configurações antigas de recursos podem ser alteradas manualmente, embora a maior parte seja atualmente controlada de maneira automática pelo Plug and Play e pelo firmware.

Gerenciamento de energia

Alguns dispositivos mostram opções como:

Permitir que o Windows desligue o dispositivo para economizar energia.
Permitir que o dispositivo desperte o computador.
Permitir somente um pacote especial de rede para ativar o computador.
Consultar recursos de suspensão ou ativação.

Essas opções aparecem com frequência em adaptadores de rede, USB, Bluetooth, teclados e mouses.

4. Atualização automática de drivers

Ao selecionar Atualizar driver, o Windows pode:

Procurar automaticamente um driver adequado.
Verificar drivers já presentes no computador.
Instalar um pacote mais apropriado encontrado localmente.
Encaminhar o usuário para procurar drivers no Windows Update.
Informar que o melhor driver disponível já está instalado.

O Windows Update também verifica regularmente drivers certificados publicados pelos fabricantes e pode baixá-los automaticamente.

Importante: “o melhor driver já está instalado” normalmente significa que o Windows não encontrou um pacote que considere superior em suas fontes disponíveis. Isso não garante que seja a versão mais recente publicada diretamente pela NVIDIA, AMD, Intel, Realtek ou pelo fabricante do computador.

5. Instalação manual de driver

O Gerenciador permite instalar um driver previamente baixado:

Selecionar Atualizar driver.
Escolher Procurar drivers no meu computador.
Indicar a pasta que contém os arquivos.
Permitir a busca nas subpastas.
Instalar o pacote compatível encontrado.

Também é possível escolher manualmente entre drivers compatíveis já registrados no computador.

Os pacotes normalmente incluem:

Arquivo .inf: instruções de instalação.
Arquivo .sys: driver executável.
Arquivos .cat: catálogo e assinatura.
DLLs, firmwares e componentes auxiliares.

A Microsoft recomenda baixar manualmente drivers apenas do site oficial do fabricante do computador ou do componente.

6. Reverter driver

A opção Reverter driver restaura a versão utilizada antes da última atualização.

Ela pode ser usada quando um driver novo provoca:

Tela preta.
Instabilidade.
Perda de desempenho.
Falhas de áudio.
Problemas de rede.
Tela azul.
Incompatibilidade com jogos ou programas.
Dispositivo que deixou de funcionar.

A opção pode ficar indisponível quando não existe uma versão anterior armazenada. A reversão normalmente exige privilégios de administrador.

7. Desativar e ativar dispositivos

A opção Desativar dispositivo impede que o Windows utilize aquele componente sem necessariamente remover seu driver.

Pode ser usada para:

Desativar uma webcam.
Desativar Bluetooth.
Desligar um adaptador de rede.
Desativar áudio integrado.
Impedir temporariamente o uso de uma GPU.
Testar conflitos entre dispositivos.
Desativar hardware virtual.
Investigar falhas.

Depois, a opção Ativar dispositivo restaura seu funcionamento.

Desativar componentes essenciais, como controladores de armazenamento, vídeo, USB ou dispositivos do sistema, pode deixar o computador inutilizável até uma reinicialização ou recuperação.

8. Desinstalar dispositivo

A função Desinstalar dispositivo remove a instância configurada daquele hardware.

Dependendo do dispositivo, o Windows pode oferecer também a opção de tentar remover o pacote do driver.

Depois da desinstalação:

O dispositivo pode desaparecer temporariamente.
O Windows pode detectá-lo novamente.
O driver pode ser reinstalado automaticamente.
Pode ser necessário reiniciar.
Pode ser necessário fornecer um novo driver.

A Microsoft orienta que, após a reinicialização, o Windows tente reinstalar automaticamente o driver disponível.

Diferença importante
Desinstalar dispositivo: remove a instância do hardware.
Remover pacote do Driver Store: remove os arquivos usados para futuras instalações.
Desativar: mantém o dispositivo instalado, mas impede seu uso.
9. Procurar alterações de hardware

A função Verificar se há alterações de hardware força uma nova enumeração dos dispositivos.

Ela pode:

Detectar novamente um componente.
Reinstalar um dispositivo removido.
Reconhecer hardware conectado recentemente.
Detectar alterações em dispositivos virtuais.
Recarregar a estrutura Plug and Play.
Tentar instalar um driver disponível.

Essa opção costuma ser usada depois de desinstalar um dispositivo para provocar sua reinstalação.

10. Mostrar dispositivos ocultos

A opção Exibir dispositivos ocultos pode revelar:

Dispositivos desconectados.
Pendrives usados anteriormente.
Adaptadores de rede antigos.
Interfaces virtuais.
Monitores que já foram conectados.
Dispositivos não presentes.
Drivers e componentes internos normalmente escondidos.
Instâncias duplicadas ou antigas.

Remover indiscriminadamente dispositivos ocultos pode apagar configurações que seriam reutilizadas quando o equipamento fosse conectado novamente.

11. Plug and Play

O sistema Plug and Play executa a detecção e configuração automática de hardware.

Quando um dispositivo é conectado, o Windows pode:

Identificar o dispositivo pelo barramento e pelos IDs de hardware.
Procurar pacotes compatíveis no Driver Store.
Comparar os drivers disponíveis.
Escolher um pacote compatível.
Copiar e registrar os arquivos.
Criar a instância do dispositivo.
Inicializar o driver.
Informar se é necessário reiniciar.

Esse mecanismo é utilizado para USB, PCI Express, Bluetooth, monitores, armazenamento, áudio, impressoras e diversos dispositivos virtuais.

Administradores também podem permitir ou bloquear a instalação de dispositivos específicos ou classes inteiras usando políticas de instalação.

12. Windows Update e drivers opcionais

O Windows Update pode instalar automaticamente drivers considerados importantes ou adequados para o sistema. Fabricantes participantes do programa de hardware da Microsoft podem publicar drivers certificados diretamente no Windows Update.

Na área de Atualizações opcionais, podem aparecer drivers para:

Chipset.
Áudio.
Bluetooth.
Wi-Fi e Ethernet.
Impressoras.
Monitores.
Câmeras.
Touchpads.
Controladores USB.
Firmware.
Dispositivos de entrada.
Componentes específicos do fabricante.

Drivers opcionais não precisam ser instalados somente porque estão disponíveis. Normalmente são mais úteis quando há um problema específico com o dispositivo.

13. Driver Store

O Driver Store é o repositório protegido onde o Windows mantém pacotes preparados para instalação.

Ele permite:

Reutilizar um driver sem novo download.
Reinstalar automaticamente um dispositivo.
Manter pacotes para diferentes componentes.
Validar a integridade e a assinatura do pacote.
Armazenar versões publicadas por fabricantes.
Instalar dispositivos enquanto estão sendo detectados.

Normalmente, seus arquivos ficam sob:

C:\Windows\System32\DriverStore\FileRepository

Não é recomendado apagar manualmente arquivos dessa pasta. A remoção correta deve ser feita por ferramentas do Windows, como o PnPUtil.

14. PnPUtil

O PnPUtil.exe é a ferramenta nativa de linha de comando para gerenciamento avançado de pacotes e dispositivos. Ele está incluído no Windows desde o Windows Vista e fica no diretório do sistema.

Entre suas funções estão:

Listar pacotes de drivers de terceiros.
Listar dispositivos.
Listar dispositivos com problemas.
Adicionar pacotes ao Driver Store.
Instalar um pacote.
Atualizar dispositivos com um pacote.
Excluir pacotes do Driver Store.
Desinstalar o pacote dos dispositivos que o utilizam.
Ativar dispositivos.
Desativar dispositivos.
Reiniciar dispositivos.
Remover dispositivos.
Procurar alterações de hardware.
Exportar drivers.
Enumerar classes de dispositivos.
Ver informações de interfaces e serviços relacionados.

Exemplos de operações:

pnputil /enum-devices /connected
pnputil /enum-devices /problem
pnputil /enum-drivers
pnputil /scan-devices

O PnPUtil deve ser executado como administrador para muitas operações. A ferramenta gerencia pacotes no Driver Store e pode adicionar, instalar, enumerar ou excluir drivers.

15. Exportação de drivers

O Windows permite exportar pacotes de drivers de terceiros para uma pasta.

Isso é útil para:

Reinstalar o Windows.
Guardar drivers de um notebook antigo.
Preparar uma instalação sem internet.
Replicar drivers em computadores semelhantes.
Criar uma cópia antes de testar versões novas.

A exportação pode ser feita com PnPUtil ou DISM. Ela normalmente não inclui programas de controle completos, como painéis da NVIDIA, AMD ou fabricantes de impressoras; exporta principalmente os pacotes de drivers.

16. Assinatura e segurança dos drivers

O Windows verifica a integridade e a assinatura dos pacotes antes de permitir determinadas instalações.

O sistema pode:

Identificar o editor do driver.
Validar o catálogo do pacote.
Bloquear drivers incompatíveis.
Bloquear drivers de kernel vulneráveis.
Avisar sobre drivers não confiáveis.
Aplicar requisitos de assinatura.
Impedir a instalação por políticas corporativas.
Usar isolamento de núcleo e integridade de memória para limitar drivers incompatíveis.

Drivers operam em uma parte sensível do sistema; um driver defeituoso ou malicioso pode provocar falhas, perda de dados ou comprometer a segurança.

17. Firmware

Alguns firmwares aparecem como dispositivos dentro do Gerenciador, incluindo:

UEFI.
Firmware de SSD.
Controladores.
Dispositivos Surface.
Docking stations.
Componentes de segurança.
Periféricos atualizáveis.

Atualizações de firmware podem ser distribuídas por:

Windows Update.
Aplicativo do fabricante.
Instalador próprio.
Atualização de BIOS/UEFI.
Pacote de driver especial.

O Gerenciador de Dispositivos consegue mostrar e administrar a instância, mas geralmente não oferece todos os controles especializados de atualização do fabricante.

18. Ferramentas nativas relacionadas
Ferramenta	Função
Gerenciador de Dispositivos	Visualizar e controlar hardware e drivers
Windows Update	Baixar drivers e firmwares certificados
PnPUtil	Gerenciar dispositivos e pacotes por comando
DISM	Adicionar ou remover drivers de imagens do Windows
Informações do Sistema	Mostrar detalhes de componentes e recursos
Visualizador de Eventos	Investigar falhas de instalação e inicialização
DXDiag	Diagnosticar vídeo, som e DirectX
Configurações	Conectar Bluetooth, impressoras, câmeras e periféricos
Painel de Controle	Configurações clássicas de impressoras, som e energia
Windows Security	Verificar segurança e compatibilidade de drivers
PowerShell	Consultar dispositivos Plug and Play e automatizar tarefas
19. O que o sistema nativo não faz completamente

O gerenciamento nativo possui algumas limitações:

Não configura todos os recursos específicos da placa de vídeo.
Não substitui o painel da NVIDIA, AMD ou Intel.
Não oferece todas as funções de impressoras e scanners profissionais.
Não atualiza necessariamente para o driver mais recente do fabricante.
Não atualiza todos os firmwares existentes.
Não mostra detalhadamente temperaturas e sensores.
Não testa completamente a saúde física do hardware.
Não oferece controle avançado de ventoinhas ou overclock.
Não garante que um dispositivo com Código 43 seja recuperável por software.
Não substitui aplicativos próprios de notebooks e placas-mãe.
Resumo
Ação	Ferramenta principal
Ver hardware instalado	Gerenciador de Dispositivos
Descobrir componente desconhecido	Propriedades → IDs de hardware
Atualizar automaticamente	Windows Update ou Gerenciador
Instalar driver baixado	Gerenciador ou PnPUtil
Voltar ao driver anterior	Reverter driver
Desativar temporariamente	Desativar dispositivo
Reinstalar	Desinstalar e verificar alterações
Guardar drivers	PnPUtil ou DISM
Remover pacote completamente	PnPUtil
Diagnosticar erro	Código do dispositivo e Visualizador de Eventos
e o gerenciador de energia?
Pensou por 41s

Sim. O gerenciamento de energia nativo do Windows também não é um único programa. Ele é composto por:

Configurações → Sistema → Energia e bateria
Opções de Energia do Painel de Controle
Gerenciamento de energia dos dispositivos
Sistema de suspensão e hibernação
Ferramenta de linha de comando powercfg
Recursos do firmware, drivers e fabricantes
1. Modos de energia

Em Configurações → Sistema → Energia e bateria, o Windows permite escolher o equilíbrio entre consumo e desempenho:

Melhor eficiência energética: limita atividades em segundo plano e reduz o consumo.
Equilibrado: ajusta automaticamente desempenho e economia.
Melhor desempenho: favorece velocidade e resposta, consumindo mais energia.

Em notebooks compatíveis, podem existir configurações diferentes para uso na bateria e conectado à tomada. Alguns fabricantes também adicionam modos como silencioso, desempenho, turbo ou conservação de bateria.

2. Controle da tela

O sistema pode determinar quando a tela será desligada:

Desligar após determinado período de inatividade.
Usar um tempo diferente na bateria.
Usar outro tempo conectado à tomada.
Nunca desligar automaticamente.
Reduzir o brilho ao usar economia de energia.
Ajustar brilho automaticamente, quando há sensor.
Usar brilho adaptável ao conteúdo, em equipamentos compatíveis.
Reduzir a taxa de atualização para economizar energia.
Usar taxa de atualização dinâmica em monitores compatíveis.

Desligar a tela não significa suspender o computador; programas, downloads e tarefas podem continuar sendo executados.

3. Suspensão automática

O Windows pode colocar o computador em suspensão depois de um período sem atividade:

Definir tempo na bateria.
Definir tempo conectado.
Suspender ao fechar a tampa.
Suspender ao pressionar o botão de energia.
Suspender pelo Menu Iniciar.
Desativar a suspensão automática.
Retomar pelo botão de energia, teclado, mouse ou abertura da tampa, dependendo do hardware.

A suspensão mantém a sessão para que o computador volte rapidamente ao ponto anterior, consumindo muito menos energia do que em funcionamento normal.

4. Hibernação

A hibernação grava o conteúdo da sessão no armazenamento e praticamente desliga o computador.

Ela permite:

Retomar aplicativos e documentos abertos.
Consumir menos energia que a suspensão.
Manter a sessão mesmo se a bateria acabar.
Hibernar automaticamente após determinado tempo.
Hibernar ao fechar a tampa.
Hibernar ao pressionar o botão de energia.
Adicionar a opção Hibernar ao Menu Iniciar.

A retomada costuma ser mais lenta que na suspensão. A opção pode não aparecer em todos os equipamentos ou configurações.

5. Estados de energia

Internamente, o Windows trabalha com vários estados:

Estado	Significado
S0	Computador funcionando
S0 Low Power Idle	Modern Standby
S1/S2/S3	Formas tradicionais de suspensão
S4	Hibernação
S5	Desligamento normal

Equipamentos com Modern Standby utilizam o estado S0 de baixo consumo e podem retomar quase imediatamente. Sistemas com Modern Standby normalmente não usam a suspensão tradicional S3. O comando powercfg /a mostra quais estados estão disponíveis no computador.

6. Modern Standby

Em computadores compatíveis, o Modern Standby funciona de forma semelhante ao modo de espera de celulares:

Desliga a tela.
Reduz atividade de CPU, GPU e armazenamento.
Mantém alguns componentes em estados de baixo consumo.
Pode permitir determinadas atividades em segundo plano.
Pode manter ou interromper a rede durante o repouso.
Retoma rapidamente.
Permite diagnóstico por meio do SleepStudy.

O comportamento exato depende do firmware, drivers, processador e configuração do fabricante.

7. Economia de energia

O recurso Economia de energia pode:

Ser ativado manualmente.
Entrar automaticamente quando a bateria chega a determinado nível.
Reduzir o brilho.
Limitar atividades em segundo plano.
Reduzir sincronizações.
Diminuir determinadas notificações.
Priorizar maior duração da bateria.
Ser desativado automaticamente ao conectar o carregador, conforme a configuração.

Nas versões atuais do Windows 11, o recurso pode ser utilizado para reduzir o consumo mesmo em alguns computadores conectados à tomada.

8. Uso da bateria

A página de utilização da bateria pode mostrar:

Percentual atual.
Estado de carregamento.
Uso ao longo das últimas horas ou dias.
Tempo de tela ligada.
Tempo de tela desligada.
Tempo em suspensão.
Consumo por aplicativo.
Aplicativos que mais gastam energia.
Consumo em primeiro e segundo plano.
Histórico recente de utilização.

Para aplicativos compatíveis, é possível controlar se eles podem executar atividades em segundo plano, deixar o Windows decidir ou restringi-los para economizar bateria.

9. Planos de energia clássicos

O Painel de Controle mantém os planos de energia, também chamados de esquemas:

Equilibrado
Economia de energia, quando disponível
Alto desempenho, quando disponível
Planos criados pelo fabricante
Planos personalizados pelo usuário

É possível:

Criar um plano.
Duplicar um plano.
Alterar configurações.
Escolher o plano ativo.
Restaurar valores padrão.
Excluir planos personalizados.
Exportar ou importar planos pelo powercfg.

O modo de energia moderno e os planos clássicos são relacionados, mas não são exatamente a mesma coisa. Um plano personalizado selecionado no Painel de Controle pode impedir que o seletor de modo de energia apareça ou seja alterado nas Configurações.

10. Configurações avançadas do plano

Em Opções de Energia → Alterar configurações avançadas, podem aparecer controles como:

Tempo para desligar o disco.
Tempo para suspender.
Tempo para hibernar.
Suspensão híbrida.
Temporizadores de ativação.
Suspensão seletiva USB.
Gerenciamento de energia do PCI Express.
Economia de energia de adaptadores sem fio.
Estado mínimo do processador.
Estado máximo do processador.
Política de resfriamento.
Brilho da tela.
Configurações multimídia.
Ações para bateria fraca e crítica.
Níveis de bateria baixa, reserva e crítica.

A lista varia de acordo com o computador, drivers, versão do Windows e recursos expostos pelo firmware. Algumas opções são ocultas em equipamentos modernos.

11. Gerenciamento do processador

O Windows pode controlar como a CPU responde à carga:

Reduzir frequência durante períodos ociosos.
Aumentar desempenho quando necessário.
Definir estado mínimo do processador.
Definir estado máximo.
Controlar estacionamento de núcleos.
Gerenciar estados de baixo consumo.
Ajustar agressividade do modo turbo, dependendo do processador e driver.
Escolher política de resfriamento ativa ou passiva.

Na política ativa, o sistema tende a aumentar a ventoinha antes de reduzir desempenho. Na política passiva, pode diminuir o desempenho primeiro para produzir menos calor. Muitas dessas opções são administradas automaticamente e podem ficar ocultas.

12. Gerenciamento de dispositivos

O Windows também controla individualmente o consumo de dispositivos.

No Gerenciador de Dispositivos, alguns componentes oferecem opções como:

Permitir que o Windows desligue o dispositivo para economizar energia.
Permitir que o dispositivo acorde o computador.
Permitir apenas um pacote mágico para ativação por rede.
Suspender uma porta USB ociosa.
Reduzir consumo do adaptador de rede.
Colocar Bluetooth em estado de baixa energia.
Desligar controladores quando não estão em uso.

Essas opções aparecem principalmente em adaptadores de rede, controladores USB, Bluetooth, teclados e mouses. Desativá-las incorretamente pode provocar desconexões ou impedir o computador de acordar.

13. Suspensão seletiva USB

A suspensão seletiva USB permite desligar apenas uma porta ou dispositivo USB ocioso, sem suspender os demais dispositivos conectados.

Ela ajuda a:

Economizar bateria.
Reduzir atividade do controlador USB.
Permitir que o processador entre em estados mais profundos de economia.
Suspender leitores biométricos, webcams e outros dispositivos pouco utilizados.
Retomar automaticamente o dispositivo quando necessário.

Em alguns computadores, desativar temporariamente essa função pode ajudar a testar dispositivos USB que se desconectam ou deixam de responder.

14. PCI Express e placa de vídeo

O gerenciamento de energia do PCI Express pode colocar os links dos dispositivos em estados de menor consumo quando estão ociosos.

Isso pode afetar:

Placas de vídeo.
SSDs NVMe.
Placas de rede.
Controladores PCI Express.
Outros dispositivos conectados ao barramento.

Os níveis normalmente equilibram economia e latência. A economia máxima pode reduzir o consumo, mas, em hardware ou drivers problemáticos, pode causar instabilidade ou atrasos na retomada.

15. Botões de energia e tampa

Pelas Opções de Energia, o usuário pode escolher o que acontece:

Ao pressionar o botão de energia.
Ao pressionar o botão de suspensão.
Ao fechar a tampa do notebook.
Quando está na bateria.
Quando está conectado à tomada.

As ações disponíveis podem incluir:

Não fazer nada.
Suspender.
Hibernar.
Desligar.
Desligar apenas a tela, em alguns equipamentos.

Também é possível exigir senha quando o computador retorna da suspensão.

16. Bateria baixa e crítica

Nos planos avançados, o Windows pode controlar:

Percentual considerado bateria baixa.
Percentual de reserva.
Percentual considerado crítico.
Notificação de bateria baixa.
Ação ao atingir nível baixo.
Ação ao atingir nível crítico.
Comportamento diferente na bateria e na tomada.

A ação crítica pode ser suspender, hibernar ou desligar. Em notebooks modernos, parte dessas regras pode ser definida pelo fabricante e não aparecer para edição.

17. Temporizadores de ativação

Programas e serviços podem criar temporizadores para acordar o computador em determinado horário.

Eles podem ser usados para:

Atualizações.
Manutenção automática.
Backups.
Tarefas agendadas.
Gravações.
Verificações do sistema.

O Windows permite habilitar, desabilitar ou permitir apenas temporizadores importantes. O comando powercfg /waketimers mostra temporizadores atualmente registrados.

18. Solicitações que impedem a suspensão

Um aplicativo, serviço ou driver pode solicitar que o Windows não:

Desligue a tela.
Suspenda o computador.
Entre em modo ausente.

Isso é comum durante:

Reprodução de vídeos.
Downloads.
Compartilhamento de tela.
Cópias de arquivos.
Atualizações.
Jogos.
Gravações.

O comando powercfg /requests identifica processos, serviços ou drivers que estão impedindo a suspensão. O /requestsoverride permite criar uma substituição, mas deve ser usado com cuidado.

19. Relatório da bateria

O comando nativo:

powercfg /batteryreport

gera um relatório HTML com informações como:

Baterias instaladas.
Capacidade de projeto.
Capacidade de carga completa.
Histórico de uso.
Ciclos, quando informados pelo hardware.
Uso recente.
Estimativas de autonomia.
Evolução da capacidade.

Ele é especialmente útil para verificar desgaste e comparar a capacidade atual com a capacidade original.

20. Relatório de eficiência energética

O comando:

powercfg /energy

analisa o computador e cria um relatório com possíveis problemas, como:

Dispositivos que não entram em repouso.
Configurações pouco eficientes.
Atividade excessiva.
Temporizadores de alta resolução.
Drivers com comportamento inadequado.
USB sem suspensão seletiva.
Solicitações que impedem economia de energia.

A Microsoft recomenda executar essa análise com o computador ocioso e sem documentos ou programas importantes abertos.

21. Diagnóstico de suspensão

O powercfg também oferece funções como:

powercfg /a
powercfg /lastwake
powercfg /waketimers
powercfg /requests
powercfg /sleepstudy
powercfg /systemsleepdiagnostics
powercfg /systempowerreport

Esses comandos podem mostrar:

Estados de suspensão disponíveis.
Qual dispositivo acordou o computador.
Temporizadores de ativação.
O que está impedindo a suspensão.
Consumo durante o Modern Standby.
Histórico de entrada e saída da suspensão.
Transições de energia do sistema.
22. Funções administrativas do powercfg

A ferramenta também permite:

Listar planos.
Consultar todas as configurações.
Alterar valores na bateria e na tomada.
Ativar um plano.
Criar ou duplicar planos.
Excluir planos.
Importar e exportar configurações.
Ativar ou desativar hibernação.
Controlar quais dispositivos podem acordar o computador.
Configurar substituições de solicitações.
Gerar relatórios de energia.

Muitas dessas ações exigem Terminal, PowerShell ou Prompt de Comando executado como administrador.

23. Limitações do gerenciamento nativo

O Windows não controla sozinho todas as funções de energia do computador. Algumas dependem de:

BIOS ou UEFI.
Firmware do notebook.
Controlador de bateria.
Driver do chipset.
Processador.
Placa de vídeo.
Aplicativo do fabricante.
Carregador e bateria instalados.

Funções como limite de carga em 60% ou 80%, controle avançado de ventoinhas, perfis turbo, tensão da CPU e carregamento inteligente geralmente ficam no firmware ou no aplicativo do fabricante.

Resumo
Componente	Função principal
Energia e bateria	Controles simples de consumo, suspensão e bateria
Modo de energia	Equilibrar desempenho e eficiência
Economia de energia	Limitar consumo e atividade em segundo plano
Opções de Energia	Planos e configurações avançadas
Suspensão	Retomada rápida com baixo consumo
Hibernação	Salvar a sessão no armazenamento
Modern Standby	Repouso moderno de retomada rápida
Gerenciador de Dispositivos	Energia e capacidade de ativação dos componentes
powercfg	Configuração e diagnóstico avançados
Aplicativo do fabricante	Limites de carga, ventoinhas e perfis específicos
e o gerenciador de jogos? eu sei que o windows tem o modo jogo que da prioridade pra jogo
Pensou por 1m 13s

Sim. O Windows não possui um único aplicativo chamado Gerenciador de Jogos. O gerenciamento nativo de jogos é dividido entre:

Modo de Jogo
Xbox Game Bar
Capturas
Configurações gráficas por jogo
Xbox app
Recursos de tela, HDR e taxa de atualização
Modo Xbox, nas versões recentes do Windows 11
1. Modo de Jogo

O Modo de Jogo fica em:

Configurações → Jogos → Modo de Jogo

Ele procura reduzir a interferência de outras atividades enquanto um jogo está em primeiro plano. A documentação técnica descreve o recurso como uma forma de conceder ao jogo acesso exclusivo ou prioritário a determinados recursos de hardware, incluindo CPU e GPU. O ganho depende principalmente da quantidade de processos concorrentes executados no computador.

O que ele pode fazer
Identificar que um jogo está sendo executado.
Favorecer o jogo na distribuição de determinados recursos.
Reduzir a interferência de tarefas em segundo plano.
Ajudar a manter o tempo entre quadros mais consistente.
Melhorar a estabilidade dos FPS em computadores muito ocupados.
Favorecer o jogo que está aberto e possui o foco.
Aplicar automaticamente o modo à maioria dos jogos reconhecidos.
A questão da prioridade

Você está certo ao dizer que ele dá prioridade ao jogo, mas existe uma diferença importante:

O Modo de Jogo não é simplesmente colocar o processo em “Prioridade alta” no Gerenciador de Tarefas. Ele administra diferentes recursos do sistema e pode reservar ou priorizar recursos para o jogo. Por isso, a prioridade mostrada na página Detalhes do Gerenciador de Tarefas pode continuar aparecendo como “Normal”. A implementação exata das versões atuais não é totalmente exposta pela Microsoft.

2. O que o Modo de Jogo não faz

O recurso não:

Cria mais desempenho do que o hardware consegue oferecer.
Aumenta automaticamente a frequência da CPU ou GPU.
Faz overclock.
Libera toda a memória RAM.
Fecha todos os programas em segundo plano.
Coloca obrigatoriamente o processo em prioridade alta ou tempo real.
Altera os gráficos internos do jogo.
Aumenta o FPS em todos os computadores.
Corrige superaquecimento, falta de RAM ou gargalo de hardware.
Substitui drivers da NVIDIA, AMD ou Intel.

Em um computador que já está com poucos processos em segundo plano, a diferença pode ser pequena. O benefício tende a ser maior quando outras aplicações disputam CPU, GPU, armazenamento ou memória com o jogo.

3. Xbox Game Bar

A Xbox Game Bar é a sobreposição nativa de jogos do Windows. Ela pode ser aberta com:

Windows + G

Ela funciona sobre a maioria dos jogos e permite acessar controles sem precisar sair do jogo ou usar Alt + Tab.

Principais funções
Tirar capturas de tela.
Gravar vídeos do jogo.
Gravar os últimos momentos da partida, quando a gravação em segundo plano está configurada.
Ativar ou desativar o microfone.
Incluir áudio do jogo e do sistema na gravação.
Controlar o volume geral.
Controlar o volume de cada aplicativo.
Monitorar métricas de desempenho.
Conversar com amigos da rede Xbox.
Ver notificações e atividades sociais.
Fixar widgets sobre o jogo.
Abrir a galeria de capturas.
Acessar widgets instalados por outros aplicativos.
Utilizar controles de mídia.
Abrir recursos adicionais sem minimizar o jogo.

A Game Bar possui uma loja de widgets que permite descobrir, instalar e remover extensões compatíveis.

4. Monitoramento de desempenho

O widget de desempenho da Game Bar pode acompanhar informações enquanto o jogo está aberto, como:

Utilização da CPU.
Utilização da GPU.
Consumo de memória RAM.
Memória de vídeo, dependendo do hardware e da versão.
Quadros por segundo.
Gráficos de utilização.
Processos ou recursos que estão consumindo desempenho.

O widget pode ser fixado para continuar aparecendo mesmo depois que a sobreposição principal da Game Bar for fechada. A Microsoft apresenta a Game Bar como uma ferramenta para acompanhar desempenho e comunicação sem interromper o jogo.

5. Captura de jogos

As opções ficam em:

Configurações → Jogos → Capturas

Elas administram as gravações feitas pela Game Bar.

Funções disponíveis
Gravar manualmente uma partida.
Capturar uma imagem.
Incluir áudio do sistema.
Incluir microfone.
Definir a pasta das gravações.
Controlar a qualidade do vídeo.
Definir taxa de quadros da gravação.
Gravar o jogo em segundo plano.
Registrar os últimos segundos ou minutos.
Mostrar o cursor do mouse.
Definir o volume de captura.
Abrir as capturas existentes.

Os vídeos são normalmente salvos como MP4 na pasta Vídeos\Capturas, e as imagens são armazenadas como PNG.

Atalhos principais
Atalho	Função
Windows + G	Abrir a Game Bar
Windows + Alt + R	Iniciar ou parar gravação
Windows + Alt + Print Screen	Capturar imagem do jogo
Windows + Alt + G	Salvar os últimos momentos
Windows + Alt + M	Ativar ou desativar o microfone

A gravação em segundo plano pode consumir armazenamento, CPU, GPU e memória. Em computadores com desempenho limitado, desativá-la pode diminuir travamentos ou oscilações durante o jogo.

6. Escolha da GPU por jogo

Em computadores com GPU integrada e dedicada, o Windows permite selecionar qual placa será usada por cada jogo:

Configurações → Sistema → Tela → Gráficos

As opções são:

Deixar o Windows decidir
Economia de energia
Alto desempenho

Isso permite, por exemplo:

Executar um jogo na GPU dedicada.
Executar jogos leves na GPU integrada.
Economizar bateria.
Evitar que um jogo selecione a GPU errada.
Configurar jogos individualmente.
Aplicar preferências diferentes para jogos e programas comuns.

Escolher Alto desempenho apenas indica qual GPU deve ser utilizada. Isso não coloca automaticamente a CPU em prioridade alta nem faz overclock na placa de vídeo.

7. Otimizações para jogos em janela

O Windows 11 possui a opção:

Configurações → Sistema → Tela → Gráficos → Configurações gráficas padrão → Otimizações para jogos em janela

Ela é voltada principalmente para jogos DirectX 10 e DirectX 11 executados:

Em janela.
Em janela sem bordas.
Sem tela cheia exclusiva.

O Windows muda jogos compatíveis do modelo de apresentação gráfico antigo para o modelo flip mais moderno. Isso pode reduzir a latência dos quadros e permitir recursos como HDR Automático e taxa de atualização variável.

Também é possível desativar essa otimização para um jogo específico caso ela provoque:

Travamentos.
Piscadas.
Problemas com sobreposições.
Incompatibilidade com captura.
Oscilações no tempo dos quadros.
Problemas com programas antigos.
8. HDR Automático

O HDR Automático permite que determinados jogos originalmente produzidos em SDR sejam apresentados com uma faixa maior de brilho e cores em uma tela HDR compatível.

Ele depende de:

Windows 11.
Monitor ou televisão HDR.
GPU e driver compatíveis.
HDR habilitado no Windows.
Jogo compatível.

Ao ativar o HDR Automático, o Windows também pode habilitar automaticamente as otimizações para jogos em janela. É possível desativá-lo para jogos específicos nas configurações gráficas.

9. Taxa de atualização e VRR

As configurações de tela do Windows também fazem parte da experiência de jogos:

Escolher 60 Hz, 120 Hz, 144 Hz, 165 Hz, 240 Hz ou outra taxa suportada.
Verificar se o monitor suporta taxa de atualização variável.
Usar VRR para sincronizar melhor a exibição com os quadros produzidos.
Usar taxa de atualização dinâmica em dispositivos compatíveis.
Reduzir desfoque de movimento.
Diminuir cortes de imagem.
Melhorar a resposta visual.

A taxa selecionada depende do monitor, resolução, cabo, conexão e placa de vídeo.

10. Super-resolução automática

Em alguns Copilot+ PCs compatíveis, o Windows 11 possui a Super Resolução Automática, ou Auto SR.

Ela pode:

Executar o jogo internamente em resolução menor.
Aumentar a imagem por processamento de IA.
Melhorar o desempenho.
Manter uma qualidade visual próxima de resoluções maiores.
Ser configurada individualmente para jogos compatíveis.
Ser controlada por notificações ou pela Game Bar.

A disponibilidade depende do processador, NPU, GPU, versão do Windows e compatibilidade do jogo; portanto, não aparece em todos os computadores.

11. Xbox app

O aplicativo Xbox complementa os recursos do Windows, mas não é responsável pelo agendamento de CPU ou pela prioridade dos processos.

Ele pode:

Organizar a biblioteca de jogos Xbox para PC.
Instalar e desinstalar jogos.
Gerenciar downloads.
Escolher unidades e pastas de instalação.
Acessar jogos do PC Game Pass.
Abrir jogos instalados.
Exibir conquistas.
Gerenciar perfil Xbox.
Mostrar amigos e atividades.
Acessar jogos na nuvem, conforme assinatura e região.
Atualizar jogos distribuídos pelo ecossistema Xbox.

Jogos instalados por Steam, Epic Games, GOG e outros launchers continuam sendo administrados principalmente pelos respectivos aplicativos.

12. Modo Xbox e experiência de tela inteira

Nas versões recentes do Windows 11 existe também o Modo Xbox, uma interface inspirada em console e otimizada para controle.

Dependendo do dispositivo e da região, ele pode:

Abrir um aplicativo de jogos como tela inicial.
Exibir jogos e aplicativos em tela cheia.
Permitir navegação por controle.
Facilitar a troca de jogos pela Visão de Tarefas.
Reduzir a atividade de inicialização e processos de segundo plano em dispositivos portáteis.
Oferecer acesso pela Game Bar.
Entrar ou sair com Windows + F11.

O recurso exige Windows 11 24H2 ou posterior e sua disponibilidade depende do mercado e do tipo de dispositivo.

13. Configurações nativas relacionadas
Recurso	Principal função
Modo de Jogo	Favorecer o jogo e reduzir interferências
Xbox Game Bar	Sobreposição, desempenho, áudio e comunicação
Capturas	Gravar partidas e tirar imagens
Configurações gráficas	Escolher GPU e opções por jogo
Otimizações em janela	Reduzir latência em DX10 e DX11
HDR Automático	Converter visualmente jogos SDR para HDR
VRR	Sincronizar taxa do monitor e quadros
Auto SR	Melhorar desempenho usando super-resolução
Xbox app	Biblioteca, instalação, Game Pass e perfil
Modo Xbox	Interface de jogos em tela inteira