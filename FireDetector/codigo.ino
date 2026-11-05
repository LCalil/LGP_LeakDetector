/*
 * IoT-Based Smart Smoke and Fire Detection System — ESP32-WROVER-DEV
 * Com Botão de Pânico/Silenciamento do Buzzer e Alerta para os Bombeiros
 */

#include <WiFi.h>
#include <PubSubClient.h>

// ---------------- Rede ----------------
const char*    WIFI_SSID   = "SUA_REDE";          // o ESP32 só conecta em redes 2,4 GHz
const char*    WIFI_PASS   = "SUA_SENHA";
const char*    MQTT_HOST   = "broker.hivemq.com"; // broker público para testes
const uint16_t MQTT_PORT   = 1883;
const char*    MQTT_USER   = "";                  // deixe vazio se o broker não exige login
const char*    MQTT_PASS   = "";
const char*    TOPICO_BASE = "smartkitchen";

// ---------------- Pinos ----------------
const uint8_t PINO_LED     = 14;
const uint8_t PINO_BUZZER  = 33;
const uint8_t PINO_CHAMA   = 13;
const uint8_t PINO_MQ135   = 32;   // ADC1
const uint8_t PINO_BOTAO   = 4;    // Botão de silenciamento e alerta para bombeiros

// ---------------- Configurações ----------------
#define BUZZER_PASSIVO     false  // true = buzzer passivo (tone); false = buzzer ativo
#define CHAMA_ATIVA_EM_LOW true   // módulos com LM393 normalmente vão a LOW ao ver chama

// O ESP32 lê o MQ-135 já convertido em milivolts (0–3300 mV aprox.)
const int LIMITE_GAS_MV    = 1300; // tensão no pino que dispara o alerta — calibre!
const int HISTERESE_GAS_MV = 100;  // margem para desligar o alerta

const unsigned long AQUECIMENTO_MS        = 60000; // aquecimento do MQ-135 (chama funciona durante)
const unsigned long INTERVALO_GAS        = 1000;  // leitura do MQ-135
const unsigned long DEBOUNCE_CHAMA_MS    = 200;   // chama precisa ficar estável por esse tempo
const unsigned long INTERVALO_TELEMETRIA = 5000;  // publicação periódica
const unsigned long INTERVALO_RECONEXAO  = 5000;  // nova tentativa de conexão ao broker
const unsigned long DEBOUNCE_BOTAO_MS    = 50;    // debounce para o botão físico
const int AMOSTRAS_MQ = 16;
const int FREQ_BUZZER = 2000;                     // Hz (só buzzer passivo)

// ---------------- Estado ----------------
enum Estado { SEGURO, ALERTA_GAS, ALERTA_FOGO, ALERTA_FOGO_GAS };
const char* NOMES_ESTADO[] = { "SAFE", "GAS", "FIRE", "FIRE_GAS" };

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

char idDispositivo[20];
char topTelemetria[64], topEstado[64], topAlerta[64], topOnline[64], topCmd[64], topBombeiros[64];

int  gasMv      = 0;
bool gasPronto = false;
bool alarmeGas = false;

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
  bool leitura = lerChamaBruta();
  if (leitura != leituraChamaAnterior) {
    leituraChamaAnterior = leitura;
    tMudancaChama = agora;
  }
  if (leitura != chama && agora - tMudancaChama >= DEBOUNCE_CHAMA_MS) {
    chama = leitura;
    Serial.println(chama ? F("!!! CHAMA DETECTADA !!!") : F("Chama não detectada."));
  }
}

void atualizarGas(unsigned long agora) {
  if (!gasPronto && agora >= AQUECIMENTO_MS) {
    gasPronto = true;
    Serial.println(F("MQ-135 aquecido e pronto."));
  }
  if (agora - tGas < INTERVALO_GAS) return;
  tGas = agora;

  gasMv = lerMQ135mV();
  if (!gasPronto) return;              // durante o aquecimento a leitura não dispara alarme

  if (!alarmeGas && gasMv >= LIMITE_GAS_MV) {
    alarmeGas = true;
    Serial.println(F("!!! GÁS/FUMAÇA ACIMA DO LIMITE !!!"));
  } else if (alarmeGas && gasMv <= LIMITE_GAS_MV - HISTERESE_GAS_MV) {
    alarmeGas = false;
    Serial.println(F("Nível de gás normalizado."));
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
  
  mqtt.publish(topBombeiros, msg);
  Serial.println(F("🚨 ALERTA MANUAL ENVIADO AOS BOMBEIROS VIA MQTT! 🚨"));
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
      if (!gasPronto) led = (agora / 1000) % 2;  // aquecendo
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
  setBuzzer(false);

  // ADC do ESP32: 12 bits e atenuação de 11 dB (faixa até ~3,3 V)
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
  Serial.println(F("=== Smart Smoke and Fire Detection (ESP32) + Silenciador ==="));
  Serial.printf("ID do dispositivo: %s\n", idDispositivo);
  Serial.printf("Tópicos em: %s/%s/#\n", TOPICO_BASE, idDispositivo);
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
    Serial.printf("MQ-135: %4d mV | Chama: %s | Estado: %s | MQTT: %s\n",
                  gasMv, chama ? "SIM" : "não", NOMES_ESTADO[estado],
                  mqtt.connected() ? "conectado" : "offline");
  }
}
