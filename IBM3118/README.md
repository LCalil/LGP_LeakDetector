# Smart Smoke and Fire Detection System (ESP8266)

Sistema IoT de baixo custo para detecção de fumaça, gás e fogo em cozinhas e outros ambientes internos com risco de incêndio. Um ESP8266 lê um **sensor de chama infravermelho** e um **sensor de gás MQ-135**, sinaliza o perigo localmente com **LED** e **buzzer** e envia telemetria e alertas em tempo real via **MQTT**.

Os alarmes locais funcionam de forma independente da rede: se o Wi-Fi ou o broker cair, o sistema continua detectando e alertando no local.

> Firmware desenvolvido para o artigo *IoT-Based Smart Smoke and Fire Detection System* (SBrT 2026).

---

## Funcionalidades

- Detecção de chama com *debounce* de 200 ms para evitar disparos falsos
- Monitoramento de gás/fumaça com média de amostras e histerese
- Aquecimento do MQ-135 sem bloquear o sistema (a detecção de chama já funciona durante o aquecimento)
- Quatro estados de segurança, com padrões distintos de LED e buzzer
- Publicação MQTT de telemetria, estado (retido), alertas e disponibilidade (*Last Will*)
- Comandos remotos via MQTT (`test`, `status`)
- Reconexão automática de Wi-Fi e MQTT sem travar o loop principal

---

## Componentes

| Componente | Quantidade | Observação |
|---|---|---|
| NodeMCU ESP8266 (ou Wemos D1 mini) | 1 | Chip USB CH340G ou CP2102 |
| Sensor de gás MQ-135 (módulo) | 1 | Saída analógica AOUT |
| Sensor de chama IR (módulo com LM393) | 1 | Saída digital DO |
| LED | 1 | Qualquer cor (sugestão: vermelho) |
| Resistor 220 Ω | 1 | Para o LED |
| Buzzer (ativo ou passivo) | 1 | Configurável no código |
| Protoboard e jumpers | — | — |
| Resistores 10 kΩ e 20 kΩ (opcional) | 1 de cada | Divisor de tensão para o A0, ver abaixo |

---

## Ligações

| Componente | Pino do componente | Pino do ESP8266 |
|---|---|---|
| LED | Ânodo (+), via resistor de 220 Ω | D1 (GPIO5) |
| LED | Cátodo (–) | GND |
| Buzzer | + | D2 (GPIO4) |
| Buzzer | – | GND |
| Sensor de chama | DO | D5 (GPIO14) |
| Sensor de chama | VCC / GND | 3V3 / GND |
| MQ-135 | AOUT | A0 |
| MQ-135 | VCC / GND | VIN (5 V) / GND |

> **Atenção (A0):** o MQ-135 alimentado em 5 V pode entregar até 5 V na saída analógica, e o A0 do NodeMCU aceita no máximo cerca de 3,3 V. Se as leituras ficarem saturadas em 1023, use um divisor de tensão (AOUT → 10 kΩ → A0 → 20 kΩ → GND).

Os pinos D3, D4 e D8 foram evitados porque interferem no boot do ESP8266.

---

## Instalação

### 1. Placa ESP8266 no Arduino IDE

1. Em **Arquivo → Preferências**, adicione esta URL em *URLs adicionais para Gerenciadores de Placas*:
   ```
   http://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
2. Em **Ferramentas → Placa → Gerenciador de Placas**, instale **esp8266 by ESP8266 Community**.
3. Selecione **NodeMCU 1.0 (ESP-12E Module)** ou **LOLIN(WEMOS) D1 R2 & mini**.
4. Instale o driver **CH340** (site da WCH) ou **CP2102** (Silicon Labs), conforme o chip USB da placa.

### 2. Biblioteca

Em **Ferramentas → Gerenciar Bibliotecas**, instale **PubSubClient** (de Nick O'Leary).

### 3. Configuração

Edite as constantes no início de `smart_fire_detection.ino`:

```cpp
const char* WIFI_SSID   = "SUA_REDE";          // somente redes 2,4 GHz
const char* WIFI_PASS   = "SUA_SENHA";
const char* MQTT_HOST   = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER   = "";                  // vazio se o broker não exige login
const char* MQTT_PASS   = "";
const char* TOPICO_BASE = "smartkitchen";
```

Outros ajustes disponíveis:

| Constante | Padrão | Função |
|---|---|---|
| `BUZZER_PASSIVO` | `false` | `true` se o buzzer for passivo (só faz "clique" com nível alto) |
| `CHAMA_ATIVA_EM_LOW` | `true` | Inverta se o sensor de chama indicar detecção sem chama |
| `LIMITE_GAS` | `400` | Valor do ADC (0–1023) que dispara o alerta de gás |
| `HISTERESE_GAS` | `30` | Margem abaixo do limite para desligar o alerta |
| `AQUECIMENTO_MS` | `60000` | Tempo de aquecimento do MQ-135 |
| `DEBOUNCE_CHAMA_MS` | `200` | Tempo de estabilidade exigido para a leitura de chama |
| `INTERVALO_TELEMETRIA` | `5000` | Intervalo de publicação da telemetria (ms) |

### 4. Upload

Conecte a placa, selecione a porta em **Ferramentas → Porta** e faça o upload. Abra o **Monitor Serial a 115200 baud** e anote o **ID do dispositivo** exibido (ex.: `esp-a1b2c3`).

---

## Estados e sinalização local

| Estado | Condição | LED | Buzzer |
|---|---|---|---|
| `SAFE` | Sem chama, gás abaixo do limite | Flash curto a cada 3 s (pisca a cada 1 s durante o aquecimento) | Silêncio |
| `GAS` | Gás acima do limite | Pisca a 1 Hz | Bipe de 200 ms por segundo |
| `FIRE` | Chama detectada | Pisca rápido (5 Hz) | Alterna rápido (5 Hz) |
| `FIRE_GAS` | Chama e gás acima do limite | Pisca rápido (5 Hz) | Alterna rápido (5 Hz) |

A detecção de chama tem prioridade sobre o alerta de gás.

---

## MQTT

Todos os tópicos seguem o formato `smartkitchen/<id>/...`, onde `<id>` é `esp-` seguido do chip ID em hexadecimal.

| Tópico | Direção | Retido | Conteúdo |
|---|---|---|---|
| `.../telemetry` | ESP → broker | Não | JSON com as leituras, a cada 5 s |
| `.../state` | ESP → broker | Sim | `SAFE`, `GAS`, `FIRE` ou `FIRE_GAS` |
| `.../alert` | ESP → broker | Não | JSON enviado a cada mudança de estado |
| `.../online` | ESP → broker | Sim | `online`, ou `offline` via *Last Will* |
| `.../cmd` | broker → ESP | — | Comandos `test` e `status` |

### Exemplos de mensagens

Telemetria:
```json
{"gas":412,"gas_ready":true,"flame":false,"state":"GAS","rssi":-61,"uptime":1234}
```

Alerta:
```json
{"event":"FIRE","gas":385,"flame":true}
```

### Comandos

| Comando | Efeito |
|---|---|
| `test` | Aciona o buzzer por 1 s (teste de comunicação nos dois sentidos) |
| `status` | Republica imediatamente o estado e a telemetria |

### Testando

No PC, com o Mosquitto instalado:
```bash
# Acompanhar todas as mensagens
mosquitto_sub -h broker.hivemq.com -t "smartkitchen/#" -v

# Enviar o comando de teste (troque pelo seu ID)
mosquitto_pub -h broker.hivemq.com -t "smartkitchen/esp-a1b2c3/cmd" -m "test"
```

Também funcionam o **MQTT Explorer** (PC) e apps como **IoT MQTT Panel** ou **MQTT Dash** (celular).

> O `broker.hivemq.com` é público: qualquer pessoa pode ler os tópicos. Use-o apenas para testes. Em uso real, prefira um broker próprio com autenticação (Mosquitto, HiveMQ Cloud etc.).

---

## Calibração do MQ-135

1. Na primeira utilização, deixe o sensor energizado por **24 horas** para estabilizar o elemento sensor.
2. Em ar limpo, observe o valor de `MQ-135` no Monitor Serial após o aquecimento.
3. Ajuste `LIMITE_GAS` para cerca de **100 a 150 pontos acima** desse valor de referência.
4. Valide aproximando uma fonte controlada de fumaça (por exemplo, um fósforo apagado) e verifique se o estado muda para `GAS`.

O código trabalha com o valor bruto do ADC. A conversão para ppm exigiria a curva do datasheet e a calibração da resistência R0 em ar limpo.

---

## Solução de problemas

| Problema | Possível causa e solução |
|---|---|
| ESP8266 não aparece na lista de placas | Adicione a URL do gerenciador de placas (ver Instalação) |
| Porta não aparece no Arduino IDE | Instale o driver CH340/CP2102 ou troque o cabo USB (muitos só carregam) |
| Upload trava em "Connecting..." | Segure o botão **FLASH** no início do upload |
| Wi-Fi não conecta | Confira o SSID e a senha e se a rede é de 2,4 GHz |
| MQTT falha com `rc=-2` | Broker inacessível: verifique o endereço, a porta e a conexão com a internet |
| MQTT falha com `rc=4` ou `rc=5` | Usuário ou senha incorretos, ou cliente não autorizado |
| Leitura do MQ-135 sempre em 1023 | Tensão acima do limite no A0: use o divisor de tensão |
| Alarme de chama dispara sem fogo | Inverta `CHAMA_ATIVA_EM_LOW`, ajuste o potenciômetro do módulo ou afaste-o de luz solar direta e lâmpadas incandescentes |
| Buzzer só faz "clique" | O buzzer é passivo: defina `BUZZER_PASSIVO` como `true` |

---

## Estrutura

```
smart_fire_detection/
├── smart_fire_detection.ino   # firmware
└── README.md                  # este arquivo
```

O Arduino IDE exige que o `.ino` fique em uma pasta com o mesmo nome do arquivo.

---

## Autores

Lucas Calil Cavalcanti, Bernardo de Souza Silva, Matheus de Andrade Alvarenga e Rigel P. Fernandes.
