# Roadmap — Gerenciador de Arquivos

> Visão geral do desenvolvimento dividida em **5 fases**, do básico ao avançado.
> `✅` = Implementado | `🟡` = Parcial | `⬜` = Não implementado

---

## Fase 1 — Fundamentos
> Objetivo: Ter um explorador funcional com navegação e operações básicas.

### 1. Navegação por arquivos e pastas
- ✅ Abrir discos internos, SSDs, HDs e partições. *(suporte a subdiretórios em FAT12/FAT32)*
- ⬜ Acessar pendrives, cartões de memória, celulares e discos externos.
- ✅ Navegar pelas pastas Desktop, Documentos, Downloads, Imagens, Música e Vídeos. *(navegação completa por subdiretórios)*
- ⬜ Usar Este Computador para visualizar unidades e espaço disponível.
- ⬜ Navegar por pastas usando o painel lateral em formato de árvore.
- ✅ Voltar para a pasta anterior. *(Alt+← com histórico)*
- ✅ Avançar para a próxima pasta. *(Alt+→ com histórico)*
- ✅ Subir um nível na hierarquia. *(Backspace)*
- ✅ Digitar ou colar um caminho diretamente na barra de endereço. *(Ctrl+L abre barra de endereço)*
- ⬜ Copiar o caminho completo de um arquivo ou pasta.

### 2. Criação de itens
- ⬜ Criar novas pastas.
- ⬜ Criar atalhos.
- 🟡 Criar documentos e outros tipos de arquivo disponibilizados pelos programas instalados. *(F7 cria arquivo vazio, 1 byte)*
- ⬜ Criar arquivos compactados.
- ⬜ Criar itens pelo menu Novo.
- ⬜ Criar pastas automaticamente pelo atalho `Ctrl + Shift + N`.

### 3. Operações com arquivos e pastas
- ✅ Abrir arquivos. *(Enter abre, F3 visualiza conteúdo)*
- ⬜ Escolher outro programa usando "Abrir com".
- ⬜ Copiar.
- ⬜ Recortar.
- ⬜ Colar.
- ⬜ Mover.
- ⬜ Duplicar usando copiar e colar.
- ⬜ Arrastar e soltar entre pastas.
- ✅ Renomear arquivos e pastas. *(F2 com diálago, recria arquivo com novo nome)*
- ⬜ Renomear vários itens selecionados.
- ⬜ Excluir enviando para a Lixeira.
- ✅ Excluir permanentemente com `Shift + Delete`. *(F8 com confirmação S/N, exclui direto)*
- ⬜ Restaurar itens da Lixeira para o local original.
- ⬜ Esvaziar a Lixeira.
- ⬜ Criar atalhos para arquivos, pastas, programas e unidades.
- ⬜ Copiar o arquivo como caminho.
- ⬜ Abrir a localização de um arquivo.
- ⬜ Fixar ou remover uma pasta do Acesso rápido.
- ⬜ Usar comandos adicionais instalados por outros programas.
- ⬜ Acessar o menu clássico pelo comando "Mostrar mais opções" no Windows 11.

### 4. Seleção de arquivos
- ✅ Selecionar um item. *(setas cima/baixo)*
- ⬜ Selecionar todos os itens.
- ⬜ Selecionar um intervalo usando `Shift`.
- ⬜ Selecionar itens separados usando `Ctrl`.
- ⬜ Usar caixas de seleção ao lado dos arquivos.
- ⬜ Inverter a seleção.
- ⬜ Limpar a seleção.
- ⬜ Executar ações em vários arquivos simultaneamente.
- ⬜ Copiar, mover, excluir ou compartilhar vários itens de uma vez.

---

## Fase 2 — Visualização e Organização
> Objetivo: Personalizar como os arquivos são exibidos e organizados.

### 5. Formas de visualização
- ⬜ Ícones extragrandes.
- ⬜ Ícones grandes.
- ⬜ Ícones médios.
- ⬜ Ícones pequenos.
- ✅ Lista. *(modo padrão atual com colunas: ícone + nome + tamanho + tipo)*
- ⬜ Detalhes.
- ⬜ Blocos.
- ✅ Conteúdo. *(F3 visualiza o conteúdo inteiro do arquivo)*
- ⬜ Visualização compacta.
- ⬜ Alterar o tamanho dos ícones com `Ctrl + roda do mouse`.
- ⬜ Exibir miniaturas de imagens e vídeos.
- ⬜ Exibir ícones no lugar de miniaturas.
- ⬜ Mostrar ou ocultar o painel de navegação.
- ⬜ Mostrar ou ocultar o painel de visualização.
- ⬜ Mostrar ou ocultar o painel de detalhes.
- ⬜ Exibir informações sem abrir o arquivo, quando o formato é compatível.
- ⬜ Exibir em tela cheia.
- ✅ Atualizar o conteúdo da pasta. *(F5 e Backspace)*
- ⬜ Aplicar uma configuração de visualização a pastas semelhantes.

### 6. Organização
- ⬜ Ordenar por nome.
- ⬜ Ordenar por data de modificação.
- ⬜ Ordenar por tipo.
- ⬜ Ordenar por tamanho.
- ⬜ Ordenar em ordem crescente ou decrescente.
- ⬜ Agrupar por nome, data, tipo, tamanho ou outras propriedades.
- ✅ Escolher quais colunas aparecem na visualização de detalhes. *(fixo: Nome, Tamanho, Tipo)*
- ⬜ Ajustar automaticamente a largura das colunas.
- ⬜ Organizar arquivos por autor, título, duração, resolução ou outras propriedades, dependendo do formato.
- ⬜ Usar Bibliotecas para reunir pastas de diferentes locais em uma única visualização.

### 7. Exibição de arquivos especiais
- ⬜ Mostrar ou ocultar extensões, como `.exe`, `.jpg`, `.pdf` e `.txt`.
- ⬜ Mostrar arquivos e pastas ocultos.
- ⬜ Mostrar arquivos protegidos do sistema.
- ⬜ Mostrar unidades vazias.
- ⬜ Ocultar arquivos conhecidos.
- ⬜ Exibir arquivos somente leitura.
- ⬜ Alterar atributos de arquivo oculto e somente leitura.
- ⬜ Exibir atalhos e links simbólicos.
- ⬜ Visualizar arquivos do sistema, quando autorizado.

---

## Fase 3 — Pesquisa e Metadados
> Objetivo: Encontrar arquivos rapidamente e trabalhar com propriedades.

### 8. Pesquisa de arquivos
- ⬜ Pesquisar pelo nome.
- ⬜ Pesquisar por parte do nome.
- ⬜ Pesquisar pela extensão, como `*.pdf` ou `*.mp3`.
- ⬜ Pesquisar dentro da pasta atual.
- ⬜ Pesquisar em todas as subpastas.
- ⬜ Pesquisar em uma unidade inteira.
- ⬜ Pesquisar em Este Computador.
- ⬜ Pesquisar arquivos locais e do OneDrive.
- ⬜ Pesquisar pelo conteúdo interno de documentos indexados.
- ⬜ Pesquisar por data.
- ⬜ Pesquisar por tamanho.
- ⬜ Pesquisar por tipo.
- ⬜ Pesquisar por autor, título, tags e outras propriedades.
- ⬜ Usar curingas como `*`.
- ⬜ Abrir diretamente a localização de um resultado.
- ⬜ Controlar quais pastas e tipos de arquivo são indexados pela Pesquisa do Windows.

### 9. Propriedades e metadados
- ✅ Ver nome e tipo do arquivo. *(exibido na lista e na status bar)*
- ✅ Ver tamanho do arquivo. *(coluna "Tamanho" + status bar)*
- ⬜ Ver tamanho total de uma pasta.
- ⬜ Ver localização.
- ⬜ Ver data de criação.
- ⬜ Ver data de modificação.
- ⬜ Ver data do último acesso.
- ⬜ Ver o programa associado ao formato.
- ⬜ Alterar o programa padrão usado para abrir o arquivo.
- ⬜ Ver resolução de imagens.
- ⬜ Ver duração e taxa de bits de áudio ou vídeo.
- ⬜ Editar título, autor, classificação e tags em formatos compatíveis.
- 🟡 Ver informações de unidades, como capacidade, espaço usado e espaço livre. *(estrutura `fs_info_t` existe mas `free_sectors` = 0)*
- ⬜ Ver propriedades de atalhos.
- ⬜ Ver versões anteriores disponíveis.

---

## Fase 4 — Segurança, Compressão e Compartilhamento
> Objetivo: Proteger dados, trabalhar com arquivos compactados e compartilhar.

### 10. Permissões e segurança
- ⬜ Visualizar permissões NTFS.
- ⬜ Conceder ou remover acesso para usuários e grupos.
- ⬜ Configurar leitura, gravação, modificação e controle total.
- ⬜ Alterar permissões avançadas.
- ⬜ Visualizar e alterar o proprietário.
- ⬜ Controlar herança de permissões.
- ⬜ Compartilhar uma pasta somente com determinados usuários.
- ⬜ Marcar arquivos como somente leitura ou ocultos.
- ⬜ Aplicar rótulos de confidencialidade em ambientes corporativos compatíveis.
- ⬜ Proteger arquivos usando recursos do Microsoft Purview.
- ⬜ Criptografar unidades com Criptografia de Dispositivo ou BitLocker, dependendo da edição e do computador.

### 11. Compactação e arquivos
- ⬜ Criar arquivos ZIP.
- ⬜ Abrir arquivos compactados como se fossem pastas.
- ⬜ Copiar arquivos para dentro ou para fora de arquivos compactados.
- ⬜ Extrair todo o conteúdo.
- ⬜ Escolher a pasta de destino da extração.
- 🟡 Visualizar o conteúdo sem extrair tudo. *(compressão LZSS existe em RAM, mas não para arquivos no disco)*
- ⬜ Trabalhar nativamente com ZIP, RAR, 7z e TAR nas versões recentes do Windows 11.
- ⬜ Compactar arquivos e pastas em formatos suportados.
- ⬜ Usar programas externos como 7-Zip e WinRAR por meio do menu de contexto.

> **Nota:** No Windows 11 24H2 ou posterior, o suporte nativo alcança ZIP, RAR, 7z e TAR. Arquivos compactados criptografados ainda precisam, em muitos casos, de um programa externo.

### 12. Compartilhamento
- ⬜ Compartilhar arquivos pelo menu do Windows.
- ⬜ Enviar arquivos para aplicativos compatíveis.
- ⬜ Compartilhar pelo OneDrive.
- ⬜ Criar links de compartilhamento na nuvem.
- ⬜ Compartilhar com dispositivos próximos.
- ⬜ Enviar arquivos por Bluetooth, dependendo do dispositivo.
- ⬜ Compartilhar pastas pela rede local.
- ⬜ Definir permissões de leitura ou leitura e gravação.
- ⬜ Acessar pastas compartilhadas de outros computadores.
- ⬜ Copiar o endereço de rede de um arquivo ou pasta.

---

## Fase 5 — Rede, Nuvem e Manutenção
> Objetivo: Integrar com redes, serviços em nuvem e manter o sistema saudável.

### 13. Rede
- ⬜ Localizar computadores e dispositivos na rede.
- ⬜ Abrir caminhos no formato `\\computador\pasta`.
- ⬜ Mapear uma pasta de rede como uma unidade.
- ⬜ Escolher uma letra para uma unidade de rede.
- ⬜ Reconectar automaticamente a unidade ao entrar no Windows.
- ⬜ Desconectar unidades de rede.
- ⬜ Acessar servidores Windows, NAS e compartilhamentos SMB compatíveis.
- ⬜ Compartilhar pastas e impressoras pela rede local.

### 14. Integração com OneDrive e nuvem
- ⬜ Exibir arquivos do OneDrive diretamente no Explorador.
- ⬜ Sincronizar pastas locais com a nuvem.
- ⬜ Visualizar o status de sincronização.
- ⬜ Manter um arquivo somente na nuvem.
- ⬜ Baixar um arquivo para uso offline.
- ⬜ Marcar como "Sempre manter neste dispositivo".
- ⬜ Liberar espaço removendo apenas a cópia local.
- ⬜ Compartilhar arquivos por link.
- ⬜ Visualizar e restaurar versões anteriores de arquivos.
- ⬜ Fazer backup de Desktop, Documentos, Imagens e outras pastas selecionadas.

### 15. Backup e recuperação
- ⬜ Recuperar arquivos da Lixeira.
- ⬜ Restaurar versões anteriores.
- ⬜ Usar o Histórico de Arquivos.
- ⬜ Recuperar uma pasta inteira de uma data anterior.
- ⬜ Fazer backup de pastas em unidades externas.
- ⬜ Fazer backup pelo Windows Backup e OneDrive.
- ⬜ Restaurar arquivos e configurações em outro computador.
- ⬜ Usar o Windows File Recovery para tentar recuperar arquivos apagados que não estão mais na Lixeira.
- ⬜ Recuperar arquivos de backups existentes.
- ⬜ Consultar históricos de versão do OneDrive.

### 16. Gerenciamento de armazenamento
- 🟡 Ver quanto espaço cada unidade possui. *(campo `total_sectors` preenchido, `free_sectors` = 0)*
- ⬜ Ver quanto espaço está ocupado e disponível.
- ⬜ Identificar categorias que ocupam espaço.
- ⬜ Remover arquivos temporários.
- ⬜ Limpar instalações anteriores do Windows.
- ⬜ Limpar arquivos de atualização.
- ⬜ Configurar o Sensor de Armazenamento.
- ⬜ Limpar automaticamente arquivos temporários.
- ⬜ Limpar automaticamente itens antigos da Lixeira.
- ⬜ Gerenciar a pasta Downloads, quando configurado.
- ⬜ Transformar arquivos locais do OneDrive em arquivos somente online.
- ⬜ Receber alertas de pouco espaço.
- ⬜ Escolher onde novos aplicativos, documentos, músicas, imagens e vídeos serão salvos.

### 17. Gerenciamento de discos e unidades
- 🟡 Inicializar um novo disco. *(detecta discos ATA na inicialização)*
- ⬜ Criar partições e volumes.
- ⬜ Excluir volumes.
- ⬜ Formatar unidades.
- 🟡 Escolher entre sistemas de arquivos compatíveis, como NTFS, exFAT e FAT32. *(suporta FAT12 e FAT32)*
- ⬜ Alterar a letra de uma unidade.
- ⬜ Alterar o nome de uma unidade.
- ⬜ Estender uma partição.
- ⬜ Reduzir uma partição.
- ⬜ Marcar partições.
- ⬜ Colocar unidades online ou offline.
- ⬜ Visualizar a estrutura de discos e partições.

---

## Resumo de Progresso

| Fase | Total | Feito | Parcial | Restante |
|------|-------|-------|---------|----------|
| 1 — Fundamentos | 42 | 9 | 2 | 31 |
| 2 — Visualização | 38 | 4 | 1 | 33 |
| 3 — Pesquisa/Metadados | 31 | 3 | 1 | 27 |
| 4 — Segurança/Compressão | 30 | 0 | 1 | 29 |
| 5 — Rede/Nuvem/Manutenção | 43 | 0 | 2 | 41 |
| **Total** | **184** | **16** | **7** | **161** |

---

## Referência — Principais Atalhos
| Atalho | Ação | Status |
|---|---|---|
| `Windows + E` | Abrir o Explorador | 🟡 via `explorer` no shell |
| `Ctrl + C` | Copiar | ⬜ |
| `Ctrl + X` | Recortar | ⬜ |
| `Ctrl + V` | Colar | ⬜ |
| `Ctrl + A` | Selecionar tudo | ⬜ |
| `Ctrl + F` / `Ctrl + E` | Pesquisar | ⬜ |
| `Ctrl + Shift + N` | Criar uma pasta | ⬜ |
| `F2` | Renomear | ✅ |
| `Delete` | Enviar para a Lixeira | ⬜ |
| `Shift + Delete` | Excluir permanentemente | ✅ (via F8) |
| `Alt + Enter` | Abrir propriedades | ⬜ |
| `Alt + P` | Mostrar ou ocultar a visualização | ⬜ |
| `Alt + ↑` | Subir uma pasta | ⬜ |
| `Alt + ←` | Voltar | ✅ (Alt+← no Explorer) |
| `Alt + →` | Avançar | ✅ (Alt+→ no Explorer) |
| `Ctrl + N` | Abrir uma nova janela | ⬜ |
| `Ctrl + W` | Fechar a janela ou aba ativa | ⬜ |
| `Ctrl + Tab` | Trocar de aba (Windows 11) | ⬜ |
| `Ctrl + L` | Digitar caminho na barra de endereço | ✅ (no Explorer) |
| `F11` | Tela cheia | ⬜ |
| `F3` | Visualizar conteúdo | ✅ |
| `F5` | Atualizar | ✅ |
| `F7` | Criar arquivo | ✅ |
| `F8` | Excluir com confirmação | ✅ |

---

## Limitações Técnicas Conhecidas

- **Máximo 64 arquivos**: `FM_MAX_FILES` limita a 64 itens por diretório.
- **Nomes 8.3**: Limitados a 8+3 caracteres pelo formato FAT12/FAT32.
- **Sem mouse**: Toda interação é por teclado via scancodes PS/2.
- **Tela de texto**: VGA text mode 80x25 colunas.
- **FAT12 subdiretórios**: Suporte a navegação por subdiretórios implementado, mas criação de pastas ainda usa apenas o método de arquivo vazio (sem `.` e `..`).
