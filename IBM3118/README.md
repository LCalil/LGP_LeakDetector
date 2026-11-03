# Smart Smoke and Fire Detection System (ESP32-WROVER-DEV)

Sistema IoT de baixo custo para detecção de fumaça, gás e fogo em cozinhas e outros ambientes internos com risco de incêndio. Uma placa **ESP32-WROVER-DEV** lê um **sensor de chama infravermelho** e um **sensor de gás MQ-135**, sinaliza o perigo localmente com **LED** e **buzzer** e envia telemetria e alertas em tempo real via **MQTT**.

Os alarmes locais funcionam de forma independente da rede: se o Wi-Fi ou o broker cair, o sistema continua detectando e alertando no local.

> Firmware desenvolvido para o artigo *IoT-Based Smart Smoke and Fire Detection System* (SBrT 2026).

---

## Funcionalidades

- Detecção de chama com *debounce* de 200 ms para evitar disparos falsos
- Monitoramento de gás/fumaça em milivolts, com média de amostras, calibração de fábrica do ADC e histerese
- Aquecimento do MQ-135 sem bloquear o sistema (a detecção de chama já funciona durante o aquecimento)
- Quatro estados de segurança, com padrões distintos de LED e buzzer
- Publicação MQTT de telemetria, estado (retido), alertas e disponibilidade (*Last Will*)
- Comandos remotos via MQTT (`test`, `status`)
- Reconexão automática de Wi-Fi e MQTT sem travar o loop principal

---

## Componentes

| Componente | Quantidade | Observação |
|---|---|---|
| Placa ESP32-WROVER-DEV | 1 | Módulo ESP32-WROVER com PSRAM; chip USB CH340 ou CP2102 |
| Sensor de gás MQ-135 (módulo) | 1 | Saída analógica AOUT |
| Sensor de chama IR (módulo com LM393) | 1 | Saída digital DO |
| LED | 1 | Qualquer cor (sugestão: vermelho) |
| Resistor 220 Ω | 1 | Para o LED |
| Buzzer (ativo ou passivo) | 1 | Configurável no código |
| Resistores 10 kΩ e 20 kΩ | 1 de cada | Divisor de tensão para a saída do MQ-135 |
| Protoboard e jumpers | — | — |

---

## Ligações

| Componente | Pino do componente | Pino da ESP32 |
|---|---|---|
| LED | Ânodo (+), via resistor de 220 Ω | GPIO14 |
| LED | Cátodo (–) | GND |
| Buzzer | + | GPIO33 |
| Buzzer | – | GND |
| Sensor de chama | DO | GPIO13 |
| Sensor de chama | VCC / GND | 3V3 / GND |
| MQ-135 | AOUT, via divisor de tensão | GPIO32 (ADC1) |
| MQ-135 | VCC / GND | 5V / GND |

### Divisor de tensão do MQ-135

O aquecedor do MQ-135 precisa de 5 V, e com isso a saída AOUT pode chegar a 5 V. Os pinos da ESP32 aceitam **no máximo 3,3 V**, então a ligação direta pode danificar a placa. Use um divisor:

```
AOUT ── 10 kΩ ──┬── GPIO32
                │
              20 kΩ
                │
               GND
```

Com esse divisor, 5 V na saída do sensor viram cerca de 3,3 V no pino.

### Por que esses pinos

| Pinos | Motivo para evitar |
|---|---|
| GPIO16 e GPIO17 | Usados pela PSRAM do módulo WROVER |
| GPIO6 a GPIO11 | Usados pela memória flash |
| GPIO0, 2, 5, 12 e 15 | Pinos de boot (*strapping*): podem impedir a inicialização ou o upload |
| Pinos do ADC2 (GPIO0, 2, 4, 12–15, 25–27) para leitura analógica | O ADC2 não funciona enquanto o Wi-Fi está ligado; por isso o MQ-135 usa o ADC1 (GPIO32) |

Os pinos escolhidos também ficam livres nas placas WROVER-DEV que vêm com câmera, então a câmera pode continuar conectada.

---

## Instalação

### 1. Placa ESP32 no Arduino IDE

1. Em **Arquivo → Preferências**, adicione esta URL em *URLs adicionais para Gerenciadores de Placas* (se já houver outra, separe com vírgula):
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. Em **Ferramentas → Placa → Gerenciador de Placas**, instale **esp32 by Espressif Systems** (versão 2.x ou 3.x).
3. Selecione **Ferramentas → Placa → esp32 → ESP32 Wrover Module**.
4. Instale o driver do chip USB da placa: **CH340** (site da WCH) ou **CP2102** (Silicon Labs). O nome do chip aparece impresso perto do conector USB.

### 2. Biblioteca

Em **Ferramentas → Gerenciar Bibliotecas**, instale **PubSubClient** (de Nick O'Leary).

### 3. Configuração

Edite as constantes no início de `smart_fire_detection_esp32.ino`:

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
| `LIMITE_GAS_MV` | `1300` | Tensão no pino (mV) que dispara o alerta de gás |
| `HISTERESE_GAS_MV` | `100` | Margem abaixo do limite (mV) para desligar o alerta |
| `AQUECIMENTO_MS` | `60000` | Tempo de aquecimento do MQ-135 |
| `DEBOUNCE_CHAMA_MS` | `200` | Tempo de estabilidade exigido para a leitura de chama |
| `INTERVALO_TELEMETRIA` | `5000` | Intervalo de publicação da telemetria (ms) |

### 4. Upload

Conecte a placa, selecione a porta em **Ferramentas → Porta** e faça o upload. Abra o **Monitor Serial a 115200 baud** e anote o **ID do dispositivo** exibido (ex.: `esp32-a1b2c3`).

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

Todos os tópicos seguem o formato `smartkitchen/<id>/...`, onde `<id>` é `esp32-` seguido dos três últimos bytes do endereço MAC.

| Tópico | Direção | Retido | Conteúdo |
|---|---|---|---|
| `.../telemetry` | ESP32 → broker | Não | JSON com as leituras, a cada 5 s |
| `.../state` | ESP32 → broker | Sim | `SAFE`, `GAS`, `FIRE` ou `FIRE_GAS` |
| `.../alert` | ESP32 → broker | Não | JSON enviado a cada mudança de estado |
| `.../online` | ESP32 → broker | Sim | `online`, ou `offline` via *Last Will* |
| `.../cmd` | broker → ESP32 | — | Comandos `test` e `status` |

### Exemplos de mensagens

Telemetria:
```json
{"gas_mv":1342,"gas_ready":true,"flame":false,"state":"GAS","rssi":-61,"uptime":1234}
```

Alerta:
```json
{"event":"FIRE","gas_mv":980,"flame":true}
```

O campo `gas_mv` é a tensão medida no pino GPIO32, depois do divisor. Para obter a tensão na saída do sensor, multiplique por 1,5.

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
mosquitto_pub -h broker.hivemq.com -t "smartkitchen/esp32-a1b2c3/cmd" -m "test"
```

Também funcionam o **MQTT Explorer** (PC) e apps como **IoT MQTT Panel** ou **MQTT Dash** (celular).

> O `broker.hivemq.com` é público: qualquer pessoa pode ler os tópicos. Use-o apenas para testes. Em uso real, prefira um broker próprio com autenticação (Mosquitto, HiveMQ Cloud etc.).

---

## Calibração do MQ-135

1. Na primeira utilização, deixe o sensor energizado por **24 horas** para estabilizar o elemento sensor.
2. Em ar limpo, observe o valor em mV do `MQ-135` no Monitor Serial após o aquecimento.
3. Ajuste `LIMITE_GAS_MV` para cerca de **300 a 500 mV acima** desse valor de referência.
4. Valide aproximando uma fonte controlada de fumaça (por exemplo, um fósforo apagado) e verifique se o estado muda para `GAS`.

O código trabalha com a tensão medida. A conversão para ppm exigiria a curva do datasheet e a calibração da resistência R0 em ar limpo.

---

## Solução de problemas

| Problema | Possível causa e solução |
|---|---|
| ESP32 não aparece na lista de placas | Adicione a URL do gerenciador de placas (ver Instalação) |
| Porta não aparece no Arduino IDE | Instale o driver CH340/CP2102 ou troque o cabo USB (muitos só carregam) |
| Upload trava em "Connecting..." | Segure o botão **BOOT** (ou IO0) quando o upload começar e solte ao aparecer "Writing" |
| Placa reinicia sozinha (*brownout*) | Fonte ou porta USB fraca para o aquecedor do MQ-135: use outra porta, um hub com alimentação ou uma fonte externa de 5 V |
| Wi-Fi não conecta | Confira o SSID e a senha e se a rede é de 2,4 GHz |
| MQTT falha com `rc=-2` | Broker inacessível: verifique o endereço, a porta e a conexão com a internet |
| MQTT falha com `rc=4` ou `rc=5` | Usuário ou senha incorretos, ou cliente não autorizado |
| Leitura do MQ-135 sempre perto de 3100 mV ou mais | Pino saturado: confira o divisor de tensão |
| Leitura do MQ-135 sempre em 0 | Sensor ligado em um pino do ADC2 ou sem alimentação: use o GPIO32 e confira os 5 V |
| Alarme de chama dispara sem fogo | Inverta `CHAMA_ATIVA_EM_LOW`, ajuste o potenciômetro do módulo ou afaste-o de luz solar direta e lâmpadas incandescentes |
| Buzzer só faz "clique" | O buzzer é passivo: defina `BUZZER_PASSIVO` como `true` |

---

## Estrutura

```
smart_fire_detection_esp32/
├── smart_fire_detection_esp32.ino   # firmware
└── README.md                        # este arquivo
```

O Arduino IDE exige que o `.ino` fique em uma pasta com o mesmo nome do arquivo.

---

## Autores

Lucas Calil Cavalcanti, Bernardo de Souza Silva, Matheus de Andrade Alvarenga e Rigel P. Fernandes.
