# Formatação Inteligente (Smart Format)

## 📊 Resumo de Progresso
**Objetivo:** Desenvolver uma ferramenta nativa de formatação/recuperação do sistema (System Reset). A ferramenta permitirá ao usuário restaurar o SO, preservando seletivamente arquivos pessoais, diretórios ou aplicativos instalados, atuando como um "Format" interativo e guiado.
**Status Atual:** 🔴 Não Iniciado
**Prioridade:** Baixa/Média

## 🔗 Atalhos
- [ROADMAP Principal](../../ROADMAP.md)
- [Índice da Documentação](../indice.md)

## 📅 Fases de Implementação

### Fase 1: Mapeamento de Aplicativos e Dados (Fundação)
- [ ] Criar sistema de inventário para identificar aplicativos instalados no sistema (reconhecendo pacotes `.zephyrosapp` ou pastas na raiz).
- [ ] Implementar divisão lógica clara entre Diretórios de Sistema (ex: `boot/`, `sys/`) e Diretórios de Usuário.
- [ ] Função no Virtual File System (VFS/FS) para calcular espaço em disco dos apps e exibir um resumo dos dados no disco.

### Fase 2: Interface de Usuário (GUI do Reset)
- [ ] Desenvolver a aba "Recuperação / Formatação" no aplicativo de Configurações (`settings.c`).
- [ ] Criar lista de seleção (checkboxes) via `gui.c` para permitir ao usuário escolher o que será mantido:
  - Manter Arquivos Pessoais
  - Manter Aplicativos (Exibe a lista de apps para selecionar quais manter)
  - Limpeza Completa (Factory Reset)
- [ ] Exibir telas de confirmação e alertas de segurança avisando sobre a exclusão irreversível.

### Fase 3: Lógica de Limpeza Seletiva
- [ ] **Limpeza Baseada em Árvore:** Como uma formatação em baixo nível (formatar a partição toda) apagaria os arquivos a preservar, o processo será uma "exclusão em massa seletiva". O driver `fat32.c` / `fs.c` irá deletar apenas as entradas de diretório não protegidas pela seleção do usuário.
- [ ] Apagar as configurações e o "registro" antigo do sistema.
- [ ] Manter intactos os binários principais do OS (Bootloader, `kernel.bin`), a não ser que exista uma imagem de fábrica para substituir a instalação atual.

### Fase 4: Reinicialização Segura
- [ ] Finalizar todos os processos de usuário antes da formatação.
- [ ] Exibir barra de progresso gráfica durantes as exclusões em lote.
- [ ] Chamar `cmd_reboot()` no final do processo para iniciar o sistema limpo.

## ⚠️ Limitações Conhecidas & Desafios
- **Disco em Uso:** Fazer limpeza profunda na mesma partição onde o OS está sendo executado exige que arquivos abertos pelo sistema não gerem conflitos (ex: não excluir a fonte do sistema ou os binários de GUI em execução).
- **Recuperação de Sistema Quebrado:** Esta formatação limpa dados e configurações. Se o próprio arquivo `kernel.bin` estiver corrompido, a formatação inteligente não resolve o problema a menos que tenhamos uma *Recovery Partition* separada (partição de recuperação) que contenha uma imagem intacta.
- **Performance de I/O:** Excluir recursivamente grandes quantidades de arquivos via ATA PIO sem suporte a DMA pode causar pequenos gargalos. A UI precisa atualizar continuamente para o sistema não parecer travado.

## 📚 Referências
- `src/fs/fat32.c`: Modificações necessárias para deleção recursiva rápida e condicional.
- `src/settings/settings.c`: Local onde o botão e interface de início da formatação ficará alocado.
- `src/ui/gui.c`: Para renderizar a checklist de preservação.
