# Roadmap — Sincronização e Concorrência

> Visão geral do que precisa ser corrigido após a introdução da Multitarefa Preemptiva real no ZephyrOS.

---

## 1. O Problema do Buffer de Vídeo (VGA)
Com a ativação da multitarefa preemptiva (IRQ 0 chamando o Scheduler), o processador pode interromper um aplicativo no meio da execução de funções de desenho na tela (como `video_print` ou renderização de janelas).
Se o Processo A for interrompido enquanto imprimia uma string, e o Processo B assumir a CPU e tentar imprimir outra string usando a mesma porta serial ou escrevendo nos mesmos endereços globais (`video.c`), ocorrerão **glitches visuais**.
- **Sintomas:** Caracteres embaralhados, cursor desincronizado, cores vazando entre aplicativos.

### Solução Necessária: Mutexes (Locks)
É preciso implementar primitivas de sincronização no sistema (Spinlocks ou Mutexes) e proteger as seções críticas.
1. Criar `core/spinlock.h` (utilizando a instrução `xchg` do assembly x86).
2. Adicionar uma trava global em `video.c`, por exemplo: `spinlock_t video_lock;`.
3. Toda vez que `video_print` for chamado, a thread deve adquirir a trava (`spinlock_acquire(&video_lock)`), e soltá-la ao final (`spinlock_release(&video_lock)`).

## 2. A Fila Global de Teclado
Atualmente, o input do usuário é disparado no `keyboard_process_events` e evoca uma única função de callback registrada. Com múltiplos processos rodando ao mesmo tempo (Shell, TaskManager, Editor), todos competem pela única `static void (*key_callback)(uint8_t)`.
- **Sintoma Atual:** Funciona como um "focus" rudimentar (o último processo a chamar `keyboard_set_callback` rouba os eventos pra ele), o que evita que o sistema quebre.
- **Sintoma Futuro:** Quando os aplicativos começarem a rodar paralelamente de forma assíncrona, eles poderão roubar os eventos de teclado no momento errado, causando cliques fantasmas ou perda de digitação.

### Solução Necessária: Filas de Mensagens (IPC)
O processo "Zephyr System" (PID 2) deve ser o único responsável por ler o teclado (Driver de Teclado). Ao detectar teclas pressionadas, ele não deve chamar callbacks diretamente. Ele deve enviar uma *Mensagem* (IPC Message) contendo a tecla para a fila de mensagens (Message Queue) do processo que atualmente possui o *Foco da Janela*.
1. Implementar sistema de IPC (Inter-Process Communication).
2. Implementar `get_message()` e `send_message()`.
3. Atualizar os aplicativos para usar um loop: `while(get_message(&msg)) { ... }`.
