#include <Arduino.h>

// Definimos os pinos
const int pinoBotaoMais = 4;   // Botão para incrementar (D4)
const int pinoBotaoMenos = 26; // Botão para decrementar (D26 - escolha um pino livre)
const int pinoLed = 2;         // LED interno da placa

int contador = 0;

// Variáveis para o controle de estado e debounce dos dois botões
unsigned long ultimoTempoMais = 0;
unsigned long ultimoTempoMenos = 0;
const unsigned long tempoDebounce = 50;

bool estadoAnteriorMais = HIGH;
bool estadoAnteriorMenos = HIGH;

void setup() {
  Serial.begin(115200);
  
  // Configura ambos os botões com pull-up interno
  pinMode(pinoBotaoMais, INPUT_PULLUP);
  pinMode(pinoBotaoMenos, INPUT_PULLUP);
  pinMode(pinoLed, OUTPUT);
  
  Serial.println("\nSistema de Dois Botões Iniciado!");
  Serial.print("Valor inicial do contador: ");
  Serial.println(contador);
}

void loop() {
  unsigned long tempoAtual = millis();

  // --- LÓGICA DO BOTÃO MAIS ---
  int leituraMais = digitalRead(pinoBotaoMais);
  if (leituraMais != estadoAnteriorMais) {
    ultimoTempoMais = tempoAtual;
  }
  if ((tempoAtual - ultimoTempoMais) > tempoDebounce) {
    static bool ultimoEstadoRealMais = HIGH;
    if (leituraMais == LOW && ultimoEstadoRealMais == HIGH) {
      contador++;
      digitalWrite(pinoLed, HIGH);
      Serial.print("Botão MAIS pressionado. Contador: ");
      Serial.println(contador);
    } else if (leituraMais == HIGH) {
      digitalWrite(pinoLed, LOW);
    }
    ultimoEstadoRealMais = leituraMais;
  }
  estadoAnteriorMais = leituraMais;

  // --- LÓGICA DO BOTÃO MENOS ---
  int leituraMenos = digitalRead(pinoBotaoMenos);
  if (leituraMenos != estadoAnteriorMenos) {
    ultimoTempoMenos = tempoAtual;
  }
  if ((tempoAtual - ultimoTempoMenos) > tempoDebounce) {
    static bool ultimoEstadoRealMenos = HIGH;
    if (leituraMenos == LOW && ultimoEstadoRealMenos == HIGH) {
      contador--;
      digitalWrite(pinoLed, HIGH);
      Serial.print("Botão MENOS pressionado. Contador: ");
      Serial.println(contador);
    } else if (leituraMenos == HIGH) {
      digitalWrite(pinoLed, LOW);
    }
    ultimoEstadoRealMenos = leituraMenos;
  }
  estadoAnteriorMenos = leituraMenos;
}