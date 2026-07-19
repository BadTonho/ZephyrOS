# 10 - Extras

## PC Speaker

### O que é?

O PC Speaker é o buzzer embutido nos computadores antigos. Ele pode produzir sons de diferentes frequências.

### Arquivo

```
src/drivers/speaker.c
```

### Como Funciona

O PIT (Programmable Interval Timer) pode ser configurado para gerar uma onda quadrada em uma frequência específica:

```c
void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    // Calcula o divisor do PIT
    uint32_t divisor = 1193180 / frequency;

    // Configura o canal 2 do PIT
    outb(0x43, 0xB6);
    outb(0x42, divisor & 0xFF);
    outb(0x42, (divisor >> 8) & 0xFF);

    // Liga o speaker
    outb(0x61, inb(0x61) | 3);

    // Espera a duração
    sleep(duration_ms);

    // Desliga o speaker
    outb(0x61, inb(0x61) & 0xFC);
}
```

### Notas Musicais

| Nota | Frequência (Hz) |
|------|-----------------|
| C4 | 262 |
| D4 | 294 |
| E4 | 330 |
| F4 | 349 |
| G4 | 392 |
| A4 | 440 |
| B4 | 494 |
| C5 | 523 |

### Tocando uma Melodia

```c
uint32_t freqs[] = {523, 587, 659, 698, 784, 880, 988, 1047};
uint32_t durs[] =  {200, 200, 200, 200, 200, 200, 200, 400};
speaker_play_melody(freqs, durs, 8);
```

---

## Multi-threading

### O que é?

Multi-threading permite que múltiplas linhas de execução rodem concorrentemente.

### Arquivo

```
src/thread/thread.c
```

### Criando uma Thread

```c
void minha_thread(void) {
    while (1) {
        video_print("Thread rodando!\n", 0x0A);
        thread_block(50);  // Bloqueia por 50 ticks
    }
}

thread_create("minha_thread", minha_thread);
```

### Diferença para Processos

| Característica | Processo | Thread |
|---------------|----------|--------|
| Memória | Própria | Compartilhada |
| Custo de criação | Alto | Baixo |
| Comunicação | IPC | Memória direta |
| Isolamento | Alto | Baixo |

### Quando Usar Threads

- Tarefas que precisam compartilhar dados
- Tarefas leves que não precisam de isolamento
- Background jobs (timers, atualizações)

### Quando Usar Processos

- Tarefas que precisam de isolamento
- Código que pode crashar sem afetar outros
- Segurança (cada processo em ring 3)

---

## Syscalls (Chamadas de Sistema)

### O que são?

Syscalls são a forma como processos de usuário pedem serviços ao kernel.

### Implementação Atual

O ZephyrOS tem uma implementação básica via interrupções:

```nasm
; Exemplo de syscall
mov eax, 1      ; Número da syscall (write)
mov ebx, 1      ; File descriptor (stdout)
mov ecx, msg    ; Ponteiro para mensagem
mov edx, len    ; Tamanho
int 0x80        ; Chama o kernel
```

### Tipos de Syscalls

| Número | Nome | Função |
|--------|------|--------|
| 0 | exit | Encerra processo |
| 1 | write | Escreve na tela |
| 2 | read | Lê do teclado |
| 3 | fork | Cria processo filho |
| 4 | exec | Executa programa |
| 5 | wait | Espera processo filho |
| 6 | open | Abre arquivo |
| 7 | close | Fecha arquivo |
| 8 | malloc | Aloca memória |
| 9 | free | Libera memória |

### Handlers

```c
void syscall_handler(registers_t* regs) {
    switch (regs->eax) {
        case 0: sys_exit(); break;
        case 1: sys_write(regs->ebx, regs->ecx, regs->edx); break;
        // ...
    }
}
```
