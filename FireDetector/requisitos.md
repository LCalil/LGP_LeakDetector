
# Projeto: Sistema de Detecção de Incêndio para Smart Homes

## 1. Lista de Requisitos do Projeto

**Requisitos Funcionais (RF)**
* **RF01:** O sistema deve monitorar o ambiente de forma contínua para detectar a presença de chamas ou fumaça.
* **RF02:** O sistema deve acionar um alerta sonoro (buzzer) de forma autônoma sempre que o sensor identificar um princípio de incêndio.
* **RF03:** O sistema deve possuir um botão de emergência que, quando acionado, force o disparo imediato do alarme sonoro, independentemente da leitura do sensor.
* **RF04:** O alarme sonoro deve permanecer ativo enquanto a ameaça for detectada pelos sensores ou enquanto o botão de emergência estiver sendo pressionado.

**Requisitos Não-Funcionais (RNF)**
* **RNF01:** O tempo de resposta entre a detecção do fogo (ou o acionamento do botão) e o disparo do alarme deve ser menor que 1 segundo, caracterizando um sistema de tempo real.
* **RNF02:** O código embarcado deve ser desenvolvido em C/C++ utilizando a IDE do Arduino.
* **RNF03:** O sistema deve possuir calibração física para evitar falsos positivos (ex: não disparar o alarme devido à luz do sol ou lâmpadas comuns).

---

## 2. Especificações do Sistema

* **Comunicação do Sensor:** O sensor principal utilizará um pino digital do Arduino, enviando um sinal de nível lógico `HIGH` ou `LOW` (dependendo do módulo) quando a detecção ocorrer. A sensibilidade de disparo será ajustada via trimpot (potenciômetro) na própria placa do sensor.
* **Lógica do Botão:** O botão de emergência utilizará o resistor interno do microcontrolador (`INPUT_PULLUP`). O pino fará a leitura de nível `LOW` (0V) quando o botão for pressionado.
* **Tensão de Operação:** Todo o circuito lógico e os periféricos operarão em 5V, alimentados pelo próprio pino 5V do Arduino.
* **Alimentação do Sistema:** O Arduino poderá ser alimentado via cabo USB para testes de bancada ou por uma fonte externa (ex: 9V na entrada P4) para instalação autônoma.

---

## 3. Componentes Necessários

| Componente | Quant. | Função no Projeto |
| :--- | :--- | :--- |
| **Arduino Uno R3 ou Nano** | 1 | Microcontrolador para leitura dos dados e execução da lógica. |
| **Sensor de Chama (Flame) ou MQ-2** | 1 | Detectar radiação infravermelha (fogo) ou fumaça/gases. |
| **Buzzer Ativo (5V)** | 1 | Emissor do alerta sonoro. |
| **Push Button (Chave Táctil)** | 1 | Acionador manual de emergência. |
| **Protoboard** | 1 | Base para montagem dos circuitos. |
| **Jumpers** | Vários | Fios de conexão (Macho-Macho / Macho-Fêmea). |
