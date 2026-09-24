// Definicao dos Pinos
const int PINO_SENSOR_CHAMA = 2; // Pino DO do sensor
const int PINO_BUZZER       = 8; // Pino do Buzzer
const int PINO_BOTAO        = 4; // Pino do Botao

bool disparou = false; // Guarda se o alarme foi acionado

void setup() {
  Serial.begin(9600);
  
  pinMode(PINO_SENSOR_CHAMA, INPUT);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP); // GND + Pino 7
}

void loop() {
  int sensor = digitalRead(PINO_SENSOR_CHAMA);
  int botao  = digitalRead(PINO_BOTAO);

  // 1. Se detectar chama, TRAVA em modo de disparo
  if (sensor == LOW) {
    disparou = true;
  }

  // 2. Se apertar o botao, DESLIGA e reseta o disparo
  if (botao == LOW) {
    disparou = false;
    noTone(PINO_BUZZER); // Para o som imediatamente
    delay(300);          // Tempo para soltar o botao
  }

  // 3. Estado do Buzzer
  if (disparou) {
    // Apita continuamente enquanto estiver travado
    tone(PINO_BUZZER, 1000); 
  } else {
    noTone(PINO_BUZZER);
  }
}
