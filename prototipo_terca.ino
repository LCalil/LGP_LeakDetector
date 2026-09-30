// Definição dos pinos
const int pinoChama = 2;       // Sensor de Chama (Entrada Digital D2)
const int pinoFumaca = A0;     // Sensor de Fumaça (Entrada Analógica A0)
const int pinoBuzzer = 8;      // Buzzer (Saída Digital D8)

// Configurações
const int ESTADO_CHAMA_ATIVO = LOW; // Chama ativa enviando LOW (comum na maioria dos módulos)
const int LIMIAR_FUMACA = 300;      // Nível de corte (0 a 1023). Aumente/diminua para ajustar a sensibilidade.

void setup() {
  Serial.begin(9600);
  
  pinMode(pinoChama, INPUT);
  pinMode(pinoBuzzer, OUTPUT);

  digitalWrite(pinoBuzzer, LOW);
  
  Serial.println("Sistema de monitoramento iniciado...");
}

void loop() {
  // Leitura dos sensores
  int leituraChama = digitalRead(pinoChama);
  int leituraFumaca = analogRead(pinoFumaca); // Retorna um valor entre 0 e 1023
  
  bool perigoDetectado = false;

  // Verifica o sensor de chama
  if (leituraChama == ESTADO_CHAMA_ATIVO) {
    Serial.println("ALERTA: Chama detectada!");
    perigoDetectado = true;
  }

  // Verifica o sensor de fumaça (quanto maior a concentração, maior o valor)
  if (leituraFumaca > LIMIAR_FUMACA) {
    Serial.print("ALERTA: Fumaça detectada! Nível atual: ");
    Serial.println(leituraFumaca);
    perigoDetectado = true;
  }

  // Aciona ou desliga o buzzer
  if (perigoDetectado) {
    digitalWrite(pinoBuzzer, HIGH);
  } else {
    digitalWrite(pinoBuzzer, LOW);
  }

  delay(500);
}