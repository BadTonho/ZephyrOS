# Roadmap: Bluetooth

---

## Resumo de Progresso

| Fase | Descrição | Itens | Feitos |
|------|-----------|-------|--------|
| 1 | HCI + Stack básico | 22 | 0 |
| 2 | Discovery e Pairing | 14 | 0 |
| 3 | Perfis (A2DP, HID, HFP, OPP) | 24 | 0 |
| 4 | Áudio Bluetooth (SBC + AC97) | 10 | 0 |
| 5 | UI (TUI + Shell) | 20 | 0 |
| 6 | Integrações (mouse, teclado, phone link) | 12 | 0 |
| **TOTAL** | | **102** | **0** |

**Progresso geral: 0%**

---

## Fase 1 — HCI + Stack Bluetooth

### 1.1 Estrutura de Diretórios

- [ ] Criar `src/net/bluetooth/`
- [ ] Criar `src/include/net/bluetooth/`

### 1.2 HCI (Host Controller Interface)

- [ ] Criar `src/net/bluetooth/hci.c` + `src/include/net/bluetooth/hci.h`
- [ ] Struct `hci_packet_t`: opcode (16 bits), params, data, len
- [ ] Função `hci_send_command(uint16_t opcode, uint8_t* params, uint8_t len)`
- [ ] Função `hci_send_acl(uint16_t handle, uint8_t* data, uint16_t len)`
- [ ] Função `hci_receive(uint8_t* buffer, int max_len)` — recebe packets do controlador
- [ ] Função `hci_read_local_name(char* name, int max)` — lê nome do controlador
- [ ] Função `hci_read_local_address(uint8_t* addr)` — lê endereço BD_ADDR (6 bytes)
- [ ] Função `hci_reset()` — reseta controlador (opcode 0x0C03)
- [ ] Função `hci_set_scan(uint8_t enable)` — liga/desliga inquiry scan
- [ ] Parse de eventos: Command Complete, Command Status, Connection Complete, Disconnection Complete, Inquiry Result, Inquiry Complete, Number of Completed Packets
- [ ] Buffer de recepção: ring buffer de 256 bytes
- [ ] Integração com PCI (discover controller via class 0x0E, subclass 0x01) ou USB (EHCI/XHCI)

### 1.3 Core Bluetooth

- [ ] Criar `src/net/bluetooth/bt_core.c` + `src/include/net/bluetooth/bt_core.h`
- [ ] Enum `bt_state_t`: `BT_OFF`, `BT_ON`, `BT_DISCOVERING`, `BT_CONNECTING`, `BT_CONNECTED`
- [ ] Struct `bt_device_t`: nome[48], endereço[6], classe, estado, tipo (CLASSIC/LE), handle (se conectado), RSSI
- [ ] Função `bt_init()` — detecta controlador, reseta, lê endereço
- [ ] Função `bt_on()` — liga rádio (HCI Reset + Enable Scan)
- [ ] Função `bt_off()` — desliga rádio
- [ ] Função `bt_get_state()` — retorna estado atual
- [ ] Função `bt_get_address(uint8_t* addr)` — retorna BD_ADDR
- [ ] Função `bt_get_name(char* name, int max)` — retorna nome do adaptador
- [ ] Função `bt_set_name(char* name)` — muda nome do adaptador
- [ ] Array global `bt_devices[16]` — dispositivos conhecidos (máx 16)
- [ ] Contador `bt_device_count`
- [ ] Callback: `bt_set_event_callback(void (*cb)(bt_event_t*, void*))` — notifica UI

### 1.4 Conexões

- [ ] Função `bt_connect(uint8_t* address)` — inicia conexão ACL
- [ ] Função `bt_disconnect(uint16_t handle)` — desconecta
- [ ] Função `bt_get_connected(bt_device_t* list, int max)` — lista dispositivos conectados
- [ ] Máximo de conexões simultâneas: 3
- [ ] Timeout de conexão: 10 segundos

### 1.5 Limitações

- **Bluetooth**: 4.0 (Classic + LE básico)
- **Máximo de dispositivos**: 16
- **Máximo de conexões simultâneas**: 3
- **Máximo de pareados salvos**: 8
- **Sem**: Bluetooth 5.x, Mesh, LE Audio, GATT avançado

---

## Fase 2 — Discovery e Pairing

### 2.1 Inquiry (Busca de Dispositivos)

- [ ] Criar `src/net/bluetooth/bt_discovery.c` + `src/include/net/bluetooth/bt_discovery.h`
- [ ] Função `bt_inquiry(int timeout_sec)` — inicia busca (HCI Inquiry Command 0x0401)
- [ ] Função `bt_inquiry_cancel()` — cancela busca (opcode 0x040E)
- [ ] Struct `bt_inquiry_result_t`: endereço[6], nome[48], classe (3 bytes), RSSI
- [ ] Parse de Inquiry Result event (subevent 0x02) e Extended Inquiry Result (subevent 0x0D)
- [ ] Array `bt_inquiry_results[16]` — resultados da busca
- [ ] Contador `bt_inquiry_count`
- [ ] Filtro de duplicatas — não adicionar mesmo device duas vezes
- [ ] Callback de progresso: notificar UI a cada dispositivo encontrado

### 2.2 Pairing (Pareamento)

- [ ] Função `bt_pair(uint8_t* address)` — inicia pairing
- [ ] Função `bt_unpair(uint8_t* address)` — remove pareamento
- [ ] Função `bt_is_paired(uint8_t* address)` — verifica se está pareado
- [ ] Função `bt_get_paired(bt_device_t* list, int max)` — lista pareados
- [ ] Struct `bt_paired_device_t`: endereço[6], nome[48], classe, chave[16] (link key)
- [ ] Array `bt_paired[8]` — dispositivos pareados (máx 8)
- [ ] Salvar/carregar pareados em `/network/bt_paired.conf`
- [ ] IO Capabilities: DisplayOnly (sem entrada no OS, sempre "Just Works")
- [ ] Usar pairing "Just Works" (sem PIN)

### 2.3 Class of Device

- [ ] Função `bt_decode_class(uint32_t class, char* category, int max)` — decodifica classe em texto
- [ ] Categorias: Computador, Telefone, Áudio, Periférico, etc.
- [ ] Usar para exibir ícone/tipo na UI

---

## Fase 3 — Perfis Bluetooth

### 3.1 A2DP (Áudio Stereo)

- [ ] Criar `src/net/bluetooth/bt_a2dp.c` + `src/include/net/bluetooth/bt_a2dp.h`
- [ ] Função `bt_a2dp_connect(uint8_t* address)` — abre canal L2CAP para A2DP (UUID 0x110D)
- [ ] Função `bt_a2dp_disconnect()` — fecha canal
- [ ] Função `bt_a2dp_start()` — inicia stream (AVDTP Start)
- [ ] Função `bt_a2dp_stop()` — para stream (AVDTP Suspend)
- [ ] Função `bt_a2dp_send_audio(uint8_t* data, uint32_t len)` — envia dados SBC
- [ ] Struct `a2dp_state_t`: conectado, streaming, handle, sei (stream endpoint identifier)
- [ ] Negociação de capabilities: codec SBC obrigatório, sampling rate 44.1kHz
- [ ] Timeout de buffer: 100ms

### 3.2 HID (Mouse/Teclado)

- [ ] Criar `src/net/bluetooth/bt_hid.c` + `src/include/net/bluetooth/bt_hid.h`
- [ ] Função `bt_hid_connect(uint8_t* address)` — abre canal L2CAP para HID (UUID 0x1124)
- [ ] Função `bt_hid_disconnect()`
- [ ] Função `bt_hid_receive(uint8_t* data, uint16_t len)` — recebe relatório HID
- [ ] Parse de relatório HID: mouse (botões + dx + dy), teclado (modifier + keycodes)
- [ ] Integrar com `keyboard_set_callback()` para HID keyboards — repassar teclas recebidas
- [ ] Integrar com mouse driver (quando existir) para HID mice — repassar movimento/botões
- [ ] Struct `hid_state_t`: conectado, handle, addr

### 3.3 HFP (Chamadas)

- [ ] Criar `src/net/bluetooth/bt_hfp.c` + `src/include/net/bluetooth/bt_hfp.h`
- [ ] Função `bt_hfp_connect(uint8_t* address)` — RFCOMM para HFP (UUID 0x111F)
- [ ] Função `bt_hfp_disconnect()`
- [ ] Função `bt_hfp_answer_call()` — AT+ATA
- [ ] Função `bt_hfp_hangup()` — AT+CHUP
- [ ] Função `bt_hfp_dial(char* number)` — ATD<number>;
- [ ] Função `bt_hfp_get_status()` — retorna: idle, ringing, call active
- [ ] Parse de indicadores: +CIEV (service, call, signal, battery)
- [ ] Codec negotiation: CVSD (narrowband) ou mSBC (wideband)

### 3.4 OPP (Transferência de Arquivos)

- [ ] Criar `src/net/bluetooth/bt_opp.c` + `src/include/net/bluetooth/bt_opp.h`
- [ ] Função `bt_opp_send_file(uint8_t* address, char* filepath)` — envia arquivo via OBEX Push
- [ ] Função `bt_opp_receive_handler(void (*cb)(char* filepath, uint32_t size))` — callback de recebimento
- [ ] UUID OBEX Object Push: 0x1105
- [ ] Formato OBEX: cabeçalho + body (arquivo)
- [ ] Salvar arquivos recebidos em `/bluetooth/incoming/`

---

## Fase 4 — Áudio Bluetooth

### 4.1 Codec SBC

- [ ] Criar `src/net/bluetooth/sbc.c` + `src/include/net/bluetooth/sbc.h`
- [ ] Função `sbc_init()` — configura codec
- [ ] Função `sbc_encode(int16_t* samples, int count, uint8_t* output, int* out_len)` — PCM → SBC
- [ ] Função `sbc_decode(uint8_t* input, int in_len, int16_t* output, int* out_len)` — SBC → PCM
- [ ] Parâmetros: 44.1kHz, Stereo, Bitpool 53 (qualidade média)
- [ ] Frame SBC: 128 bytes (bitpool 53, 44.1kHz, stereo)
- [ ] Buffer de output: 16 frames (16 × 128 = 2048 bytes)

### 4.2 Integração com AC97

- [ ] Criar `src/net/bluetooth/bt_audio.c` + `src/include/net/bluetooth/bt_audio.h`
- [ ] Função `bt_audio_start()` — inicia pipeline: A2DP → SBC decode → AC97 output
- [ ] Função `bt_audio_stop()` — para pipeline
- [ ] Função `bt_audio_set_volume(uint8_t vol)` — ajusta volume (0-100)
- [ ] Pipeline: receber SBC via A2DP → decodificar → enviar PCM para AC97
- [ ] Buffer circular entre SBC decode e AC97: 8KB
- [ ] Usar thread dedicada para decode + output (não bloquear main loop)

---

## Fase 5 — UI e Shell

### 5.1 TUI de Bluetooth

- [ ] Criar `src/apps/bt_manager.c` + `src/include/apps/bt_manager.h`
- [ ] Tela principal: toggle ON/OFF, estado, endereço
- [ ] Sub-tela Dispositivos Pareados: lista com nome, tipo, status de conexão
- [ ] Sub-tela Buscar: resultados do inquiry com botão "Parear"
- [ ] Sub-tela Conectados: dispositivos ativos, perfil conectado
- [ ] Navegação: setas, Enter seleciona, ESC volta
- [ ] Integrar com keyboard callback (padrão dos outros apps)
- [ ] Abrir via: Start Menu → Bluetooth, ou comando shell `bluetooth`

### 5.2 Ícone na Taskbar

- [ ] Criar ícone BT na taskbar (usando ícone existente ou novo)
- [ ] Estados: desligado, ligado mas sem conexão, conectado (com dispositivo)
- [ ] Click abre mini-menu: status rápido, dispositivo conectado

### 5.3 Comandos Shell

- [ ] `bt on` — liga Bluetooth
- [ ] `bt off` — desliga Bluetooth
- [ ] `bt status` — mostra estado atual e dispositivos conectados
- [ ] `bt scan` — busca dispositivos próximos (timeout 10s)
- [ ] `bt devices` — lista dispositivos pareados
- [ ] `bt pair <address>` — pareia dispositivo
- [ ] `bt unpair <address>` — remove pareamento
- [ ] `bt connect <address>` — conecta dispositivo pareado
- [ ] `bt disconnect` — desconecta tudo
- [ ] `bt name [novo_nome]` — mostra/muda nome do adaptador
- [ ] `bt audio` — conecta A2DP (se dispositivo de áudio pareado)
- [ ] `bt send <address> <arquivo>` — envia arquivo via OPP

### 5.4 Integração com Settings

- [ ] Adicionar seção "Bluetooth" no Settings existente (`settings.c`)
- [ ] Toggle Bluetooth ON/OFF
- [ ] Lista de dispositivos pareados
- [ ] Botão "Buscar dispositivos"
- [ ] Alinhado com Fase 3 do roadmap de `configuracoes.md`

---

## Fase 6 — Integrações

### 6.1 Mouse e Teclado Bluetooth

- [ ] Integrar `bt_hid` com driver de mouse (quando mouse driver existir)
- [ ] Integrar `bt_hid` com driver de teclado (repassar scancodes via `keyboard_set_callback`)
- [ ] Auto-detectar perfil HID ao conectar dispositivo

### 6.2 Phone Link

- [ ] Criar `src/net/bluetooth/bt_phonelink.c` + `src/include/net/bluetooth/bt_phonelink.h`
- [ ] Enviar/receber notificações via PBAP (Phone Book Access Profile) básico
- [ ] Receber SMS via MAP (Message Access Profile) básico
- [ ] Listar contatos do telefone
- [ ] Status de bateria do telefone (via HFP Battery Indicator)

### 6.3 Áudio Integrado

- [ ] Ao conectar fones BT: redirecionar output do Media Player para A2DP
- [ ] Ao desconectar: voltar para AC97 (speaker interno)
- [ ] Indicador de bateria na taskbar (se suportado pelo dispositivo)

---

## Ordem de Implementação

1. **Fase 1** — HCI + core (sem isso nada funciona)
2. **Fase 2** — Discovery + pairing (precisa para conectar qualquer coisa)
3. **Fase 3 parcial** — HID primeiro (mouse/teclado BT são mais simples)
4. **Fase 3 parcial** — A2DP (precisa de SBC)
5. **Fase 4** — Áudio (SBC codec + integração AC97)
6. **Fase 5** — UI e shell
7. **Fase 3 parcial** — HFP + OPP
8. **Fase 6** — Integrações avançadas

---

## Dependências

```
Fase 1 (HCI)
  ├── Depende: PCI/USB driver (já existe PCI)
  └── Hardware: Controlador BT (USB ou serial)

Fase 2 (Discovery/Pairing)
  └── Depende: Fase 1

Fase 3 (Perfis)
  ├── HID: Depende: Fase 1
  ├── A2DP: Depende: Fase 1
  ├── HFP: Depende: Fase 1
  └── OPP: Depende: Fase 1

Fase 4 (Áudio)
  ├── Depende: Fase 3 (A2DP)
  └── Depende: AC97 driver (já existe)

Fase 5 (UI/Shell)
  ├── Depende: Fase 1 + 2 + 3
  └── Pode ser parcial sem hardware real

Fase 6 (Integrações)
  ├── Mouse/Teclado: Depende: Fase 3 (HID) + mouse driver
  └── Phone Link: Depende: Fase 3 (HFP + OPP)
```

---

## Limitações

- **Bluetooth**: 4.0+ apenas (Classic + LE básico)
- **Sem**: Bluetooth 5.x, Mesh, LE Audio, LC3 codec
- **Sem GATT avançado**: apenas perfis SPP/HID/A2DP/HFP/OPP
- **IO**: "Just Works" apenas (sem display para comparação)
- **QEMU**: Não emula Bluetooth nativamente — precisa de hardware real para testes
- **USB**: Se controlador BT for USB, precisa de driver USB básico (futuro)

---

## Referências

- Bluetooth Core Specification v4.0
- HCI Specification v1.2
- A2DP Specification v1.3
- HID Profile v1.1
- HFP Profile v1.6
- OBEX Specification v1.5
- OS Dev Wiki: Bluetooth
