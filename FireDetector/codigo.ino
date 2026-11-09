/*
 * IoT-Based Smart Smoke and Fire Detection System — ESP32-S3 Zero
 * Arduino IDE: placa ESP32S3 Dev Module; USB CDC On Boot: Enabled.
 * Dependencia: PubSubClient (Nick O'Leary).
 * Preencha WIFI_SSID e WIFI_PASS antes de gravar.
 * Mantem LED externo; nao usa o RGB integrado.
 * Com Botão de Pânico/Silenciamento do Buzzer e Alerta para os Bombeiros
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ---------------- Rede ----------------
const char*    WIFI_SSID   = "Bumblebee";          // o ESP32 só conecta em redes 2,4 GHz
const char*    WIFI_PASS   = "regularidade3r";
const char*    MQTT_HOST   = "broker.hivemq.com"; // broker público para testes
const uint16_t MQTT_PORT   = 1883;
const char*    MQTT_USER   = "";                  // deixe vazio se o broker não exige login
const char*    MQTT_PASS   = "";
const char*    TOPICO_BASE = "smartkitchen";

// ---------------- Pinos ESP32-S3 Zero ----------------
// LED EXTERNO: GPIO10 -> resistor de 330 ohms -> anodo; catodo -> GND.
// O LED RGB integrado (GPIO21 na Waveshare) nao e usado por este codigo.
// MQ-135: AO -> divisor de tensao -> GPIO1 (ADC1).
// Exemplo para AO de ate 5 V: AO -> 10k -> GPIO1 -> 10k -> GND.
// Com esse divisor, a leitura e metade da tensao de AO; os limites usam mV no GPIO.
// Nunca aplique 5 V em um GPIO. Adapte tambem DO do sensor de chama,
// se esse sinal tiver nivel de 5 V. Todos os modulos devem compartilhar GND.
// Botao externo entre GPIO4 e GND; nao e o botao BOOT da placa.
const uint8_t PINO_LED    = 9;
const uint8_t PINO_BUZZER = 13;
const uint8_t PINO_CHAMA  = 11;
const uint8_t PINO_MQ135   = 1;    // ADC1 no ESP32-S3
const uint8_t PINO_BOTAO   = 4;    // Botão de silenciamento e alerta para bombeiros

// ---------------- Configurações ----------------
#define BUZZER_PASSIVO     false  // true = buzzer passivo (tone); false = buzzer ativo
#define CHAMA_ATIVA_EM_LOW true   // módulos com LM393 normalmente vão a LOW ao ver chama

// Leitura em milivolts no GPIO1, apos eventual divisor (ADC: ate ~3100 mV).
const int LEITURAS_CALIBRACAO = 50;
const int MARGEM_ALARME_MV = 80;  // dispara em media inicial + 80 mV
const int HISTERESE_GAS_MV = 20;  // desliga em media inicial + 60 mV

const unsigned long AQUECIMENTO_MS        = 60000; // aquecimento do MQ-135 (chama funciona durante)
const unsigned long INTERVALO_GAS        = 1000;  // leitura do MQ-135
const unsigned long DEBOUNCE_CHAMA_MS    = 200;   // chama precisa ficar estável por esse tempo
const unsigned long INTERVALO_TELEMETRIA = 1000;  // publicação periódica
const unsigned long INTERVALO_RECONEXAO  = 5000;  // nova tentativa de conexão ao broker
const unsigned long DEBOUNCE_BOTAO_MS    = 50;    // debounce para o botão físico
const int AMOSTRAS_MQ = 16;
const int FREQ_BUZZER = 2000;                     // Hz (só buzzer passivo)

// ---------------- Estado ----------------
enum Estado { SEGURO, ALERTA_GAS, ALERTA_FOGO, ALERTA_FOGO_GAS };
Estado calcularEstado(); // Prototipo explicito para o preprocessador Arduino
const char* NOMES_ESTADO[] = { "SAFE", "GAS", "FIRE", "FIRE_GAS" };

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

char idDispositivo[20];
char topTelemetria[64], topEstado[64], topAlerta[64], topOnline[64], topCmd[64], topBombeiros[64];

int  gasMv      = 0;
bool gasPronto = false;
bool alarmeGas = false;
int leiturasCalibracao[LEITURAS_CALIBRACAO];
int totalLeiturasCalibracao = 0;
float gasSafeMv = 0.0f; // referencia fixa ate reiniciar a placa

bool chama = false;
bool leituraChamaAnterior = false;
unsigned long tMudancaChama = 0;

// Variáveis para o botão físico e silenciamento
bool estadoBotaoAnterior = HIGH;
unsigned long tUltimoDebounceBotao = 0;
bool alarmeSilenciado = false; // Controla se o usuário silenciou o buzzer manualmente

Estado estado = SEGURO;
bool   buzzerLigado = false;
bool   wifiAnterior = false;

unsigned long tGas = 0, tTelemetria = 0, tReconexao = 0, tTesteFim = 0;
bool primeiraTentativaMqtt = true;

// ================= Buzzer =================
void setBuzzer(bool ligar) {
  if (ligar == buzzerLigado) return;   // evita reiniciar o tone() a cada loop
  buzzerLigado = ligar;
#if BUZZER_PASSIVO
  if (ligar) tone(PINO_BUZZER, FREQ_BUZZER);
  else { noTone(PINO_BUZZER); digitalWrite(PINO_BUZZER, LOW); }
#else
  digitalWrite(PINO_BUZZER, ligar ? HIGH : LOW);
#endif
}

// ================= Sensores e Botão =================
int lerMQ135mV() {
  long soma = 0;
  for (int i = 0; i < AMOSTRAS_MQ; i++) {
    soma += analogReadMilliVolts(PINO_MQ135);  // usa a calibração de fábrica do ADC
    delay(2);
  }
  return soma / AMOSTRAS_MQ;
}

bool lerChamaBruta() {
  bool nivel = digitalRead(PINO_CHAMA);
  return CHAMA_ATIVA_EM_LOW ? (nivel == LOW) : (nivel == HIGH);
}

void atualizarChama(unsigned long agora) {
  static bool chamaEstavel = false;
  static unsigned long ultimaDeteccao = 0;

  bool leitura = lerChamaBruta();

  if (leitura != leituraChamaAnterior) {
    leituraChamaAnterior = leitura;
    tMudancaChama = agora;
  }

  // Confirma a mudança após 200 ms de sinal estável.
  if (agora - tMudancaChama >= DEBOUNCE_CHAMA_MS) {
    chamaEstavel = leitura;
  }

  if (chamaEstavel) {
    ultimaDeteccao = agora;

    if (!chama) {
      chama = true;
      Serial.println(F("!!! CHAMA DETECTADA !!!"));
    }
  }

  // Mantém o alerta por mais 10 segundos após a detecção terminar.
  if (chama && !chamaEstavel &&
      agora - ultimaDeteccao >= 10000UL) {
    chama = false;
    Serial.println(F("Alerta de chama encerrado."));
  }
}

void atualizarGas(unsigned long agora) {
  if (agora - tGas < INTERVALO_GAS) return;
  tGas = agora;
  gasMv = lerMQ135mV();

  // Aquecimento: leituras apenas para acompanhamento, sem alarme de gas.
  if (agora < AQUECIMENTO_MS) return;

  // Cada leitura e a media das 16 amostras do ADC de lerMQ135mV().
  // Coleta uma leitura por segundo, sem bloquear chama, botao ou MQTT.
  if (!gasPronto) {
    leiturasCalibracao[totalLeiturasCalibracao++] = gasMv;
    Serial.printf("Calibrando MQ-135: %d/%d | %d mV\n",
                  totalLeiturasCalibracao, LEITURAS_CALIBRACAO, gasMv);

    if (totalLeiturasCalibracao == LEITURAS_CALIBRACAO) {
      long soma = 0;
      for (int i = 0; i < LEITURAS_CALIBRACAO; i++) {
        soma += leiturasCalibracao[i];
      }
      gasSafeMv = (float)soma / LEITURAS_CALIBRACAO;
      gasPronto = true;
      Serial.printf("Calibracao concluida. SAFE: %.2f mV | Alarme: %.2f mV | Desliga: %.2f mV\n",
                    gasSafeMv, gasSafeMv + MARGEM_ALARME_MV,
                    gasSafeMv + MARGEM_ALARME_MV - HISTERESE_GAS_MV);
    }
    return;
  }

  const float limiteAlarmeMv = gasSafeMv + MARGEM_ALARME_MV;
  if (!alarmeGas && gasMv >= limiteAlarmeMv) {
    alarmeGas = true;
    Serial.println(F("!!! GAS/FUMACA: 80 mV OU MAIS ACIMA DA REFERENCIA !!!"));
  } else if (alarmeGas && gasMv <= limiteAlarmeMv - HISTERESE_GAS_MV) {
    alarmeGas = false;
    Serial.println(F("Nivel de gas normalizado."));
  }
}

Estado calcularEstado() {
  if (chama && alarmeGas) return ALERTA_FOGO_GAS;
  if (chama)              return ALERTA_FOGO;
  if (alarmeGas)          return ALERTA_GAS;
  return SEGURO;
}

// Publica o alerta direcionado aos bombeiros via MQTT
void publicarAlertaBombeiros() {
  if (!mqtt.connected()) {
    Serial.println(F("MQTT desconectado! Não foi possível enviar o alerta aos bombeiros."));
    return;
  }
  char msg[128];
  snprintf(msg, sizeof(msg),
           "{\"emergency\":\"FIRE_DEPARTMENT_ALERT\",\"state\":\"%s\",\"gas_mv\":%d,\"flame\":%s,\"timestamp\":%lu}",
           NOMES_ESTADO[estado], gasMv, chama ? "true" : "false", millis() / 1000);
  
  // Publicar neste topico nao aciona um servico real de emergencia.
  if (mqtt.publish(topBombeiros, msg)) {
    Serial.println(F("Alerta manual publicado no topico MQTT."));
  } else {
    Serial.println(F("Falha ao publicar o alerta manual no MQTT."));
  }
}

void atualizarBotao(unsigned long agora) {
  bool leituraLeve = digitalRead(PINO_BOTAO);

  if (leituraLeve != estadoBotaoAnterior) {
    tUltimoDebounceBotao = agora;
  }

  if ((agora - tUltimoDebounceBotao) > DEBOUNCE_BOTAO_MS) {
    static bool estadoBotaoEstavel = HIGH;
    if (leituraLeve != estadoBotaoEstavel) {
      estadoBotaoEstavel = leituraLeve;

      if (estadoBotaoEstavel == LOW) { // Botão pressionado (GND)
        Serial.println(F("Botão físico pressionado pelo usuário."));
        
        // 1. Envia sinal MQTT para os bombeiros
        publicarAlertaBombeiros();
        
        // 2. Se houver algum alarme ativo, silencia o buzzer
        if (estado != SEGURO) {
          alarmeSilenciado = true;
          Serial.println(F("🔕 Buzzer silenciado manualmente pelo botão."));
        }
      }
    }
  }
  estadoBotaoAnterior = leituraLeve;
}

// ================= Sinalização local =================
void atualizarSaidas(unsigned long agora) {
  bool led = false, buz = false;

  switch (estado) {
    case ALERTA_FOGO:
    case ALERTA_FOGO_GAS: {
      bool fase = (agora / 100) % 2;   // 100 ms ligado / 100 ms desligado
      led = fase;
      buz = fase;
      break;
    }
    case ALERTA_GAS: {
      unsigned long p = agora % 1000;
      led = p < 500;                    // LED a 1 Hz
      buz = p < 200;                    // bipe de 200 ms por segundo
      break;
    }
    case SEGURO:
      if (!gasPronto) led = (agora / 1000) % 2;  // aquecendo ou calibrando
      else            led = (agora % 3000) < 50; // heartbeat
      break;
  }

  // Se o alarme foi silenciado pelo usuário, força o buzzer a desligar
  if (alarmeSilenciado) {
    buz = false;
  }

  if (agora < tTesteFim) buz = true;   // comando remoto "test"

  digitalWrite(PINO_LED, led ? HIGH : LOW);
  setBuzzer(buz);
}

// ================= MQTT =================
void publicarTelemetria() {
  if (!mqtt.connected()) return;
  char msg[176];
  snprintf(msg, sizeof(msg),
           "{\"gas_mv\":%d,\"gas_ready\":%s,\"flame\":%s,\"state\":\"%s\",\"rssi\":%d,\"uptime\":%lu}",
           gasMv, gasPronto ? "true" : "false", chama ? "true" : "false",
           NOMES_ESTADO[estado], WiFi.RSSI(), millis() / 1000);
  mqtt.publish(topTelemetria, msg);
}

void publicarEstado() {
  if (!mqtt.connected()) return;
  mqtt.publish(topEstado, NOMES_ESTADO[estado], true);   // retido
}

void publicarAlerta() {
  if (!mqtt.connected()) return;
  char msg[96];
  snprintf(msg, sizeof(msg), "{\"event\":\"%s\",\"gas_mv\":%d,\"flame\":%s}",
           NOMES_ESTADO[estado], gasMv, chama ? "true" : "false");
  mqtt.publish(topAlerta, msg);
}

void aoReceberMqtt(char* topico, byte* payload, unsigned int tamanho) {
  char cmd[16];
  unsigned int n = min(tamanho, (unsigned int)(sizeof(cmd) - 1));
  memcpy(cmd, payload, n);
  cmd[n] = '\0';
  Serial.printf("Comando MQTT: %s\n", cmd);

  if (strcmp(cmd, "test") == 0) {
    tTesteFim = millis() + 1000;
  } else if (strcmp(cmd, "status") == 0) {
    publicarEstado();
    publicarTelemetria();
  }
}

void manterRede(unsigned long agora) {
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  if (wifiOk != wifiAnterior) {
    wifiAnterior = wifiOk;
    if (wifiOk) Serial.printf("Wi-Fi conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    else        Serial.println(F("Wi-Fi desconectado (alarmes locais seguem ativos)."));
  }
  if (!wifiOk) return;

  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  if (!primeiraTentativaMqtt && agora - tReconexao < INTERVALO_RECONEXAO) return;
  primeiraTentativaMqtt = false;
  tReconexao = agora;

  Serial.print(F("Conectando ao broker MQTT... "));
  bool ok;
  if (strlen(MQTT_USER) > 0)
    ok = mqtt.connect(idDispositivo, MQTT_USER, MQTT_PASS, topOnline, 1, true, "offline");
  else
    ok = mqtt.connect(idDispositivo, topOnline, 1, true, "offline");

  if (ok) {
    Serial.println(F("ok."));
    mqtt.publish(topOnline, "online", true);
    mqtt.subscribe(topCmd);
    publicarEstado();       
    publicarTelemetria();
  } else {
    Serial.printf("falhou (rc=%d). Nova tentativa em %lu s.\n",
                  mqtt.state(), INTERVALO_RECONEXAO / 1000);
  }
}

// ================= Setup =================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_CHAMA, INPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP); // Botão com pull-up interno (pressionado = LOW)
  
  digitalWrite(PINO_LED, LOW);
  digitalWrite(PINO_BUZZER, LOW);
  setBuzzer(false);

  // ADC do ESP32-S3: 12 bits, atenuacao de 11 dB (ate ~3,1 V)
  analogReadResolution(12);
  analogSetPinAttenuation(PINO_MQ135, ADC_11db);

  WiFi.mode(WIFI_STA);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(idDispositivo, sizeof(idDispositivo), "esp32-%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(topTelemetria, sizeof(topTelemetria), "%s/%s/telemetry", TOPICO_BASE, idDispositivo);
  snprintf(topEstado,     sizeof(topEstado),     "%s/%s/state",     TOPICO_BASE, idDispositivo);
  snprintf(topAlerta,     sizeof(topAlerta),     "%s/%s/alert",     TOPICO_BASE, idDispositivo);
  snprintf(topOnline,     sizeof(topOnline),     "%s/%s/online",    TOPICO_BASE, idDispositivo);
  snprintf(topCmd,        sizeof(topCmd),        "%s/%s/cmd",       TOPICO_BASE, idDispositivo);
  snprintf(topBombeiros,  sizeof(topBombeiros),  "%s/%s/bombeiros", TOPICO_BASE, idDispositivo);

  Serial.println();
  Serial.println(F("=== Smart Smoke and Fire Detection (ESP32-S3 Zero) + Silenciador ==="));
  Serial.printf("ID do dispositivo: %s\n", idDispositivo);
  Serial.printf("Tópicos em: %s/%s/#\n", TOPICO_BASE, idDispositivo);
  Serial.println(F("Depois do aquecimento: 50 leituras, uma por segundo, para definir o SAFE. Calibre em ar limpo."));
  Serial.printf("Aquecendo o MQ-135 por %lu s (sensor de chama já ativo).\n", AQUECIMENTO_MS / 1000);

  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(aoReceberMqtt);

  setBuzzer(true); delay(150); setBuzzer(false);
}

// ================= Loop =================
void loop() {
  unsigned long agora = millis();

  atualizarChama(agora);
  atualizarGas(agora);
  atualizarBotao(agora); // Lê o botão, envia MQTT e silencia o buzzer se houver alarme

  Estado novo = calcularEstado();
  if (novo != estado) {
    estado = novo;
    
    // Se o ambiente voltar ao estado seguro, reseta o silenciamento para o próximo alerta
    if (estado == SEGURO) {
      alarmeSilenciado = false;
    }

    Serial.printf("Estado: %s\n", NOMES_ESTADO[estado]);
    publicarEstado();
    publicarAlerta();
  }

  atualizarSaidas(agora);
  manterRede(agora);

  if (agora - tTelemetria >= INTERVALO_TELEMETRIA) {
    tTelemetria = agora;
    publicarTelemetria();
    Serial.printf("MQ-135: %4d mV | SAFE: %.2f mV | Calibrado: %s | Chama: %s | Estado: %s | MQTT: %s\n",
                  gasMv, gasSafeMv, gasPronto ? "SIM" : "NAO", chama ? "SIM" : "não", NOMES_ESTADO[estado],
                  mqtt.connected() ? "conectado" : "offline");
  }
}
