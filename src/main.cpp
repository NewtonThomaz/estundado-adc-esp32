#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>

#define PIN_POTENCIOMETRO 34
#define PIN_DHT 23
#define PIN_BTN_PH_UP 18
#define PIN_BTN_PH_DOWN 19

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

float simulatedPH = 7.0;
float temperatura = 0.0;
float umidade = 0.0;
int condutividadePct = 0;

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000;

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_POTENCIOMETRO, INPUT);

  pinMode(PIN_BTN_PH_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_PH_DOWN, INPUT_PULLUP);

  dht.begin();
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Falha ao inicializar o Display OLED!");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(20, 25);
    display.println("Sistema Iniciado");
    display.display();
    delay(1000);
  }
}

void loop() {
  if (digitalRead(PIN_BTN_PH_UP) == LOW) {
    if (simulatedPH < 14.0)
      simulatedPH += 0.1;
    delay(150);
  }

  if (digitalRead(PIN_BTN_PH_DOWN) == LOW) {
    if (simulatedPH > 0.0)
      simulatedPH -= 0.1;
    delay(150);
  }

  long soma = 0;
  for (int i = 0; i < 16; i++) {
    soma += analogRead(PIN_POTENCIOMETRO);
    delayMicroseconds(200);
  }
  int potRaw = soma / 16;
  condutividadePct = map(potRaw, 0, 4095, 0, 100);

  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = currentMillis;
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("[ERRO] Falha ao ler do sensor DHT! Verifique conexao/alimentacao (3V3 vs VIN) no GPIO 4.");
    } else {
      temperatura = t;
      umidade = h;
    }

    Serial.printf("ADC Pot (GPIO 34): %d | Cond: %d%% | Temp: %.1f C | Umid: %.1f%% | pH: %.1f\n", potRaw, condutividadePct, temperatura, umidade, simulatedPH);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("--- MONITOR IOT ---");

  display.setCursor(0, 16);
  if (temperatura == 0.0 && umidade == 0.0) {
    display.print("DHT: Lendo/Falha...");
  } else {
    display.printf("Temp: %.1f C", temperatura);
  }

  display.setCursor(0, 28);
  display.printf("Umid: %.1f %%", umidade);

  display.setCursor(0, 40);
  display.printf("Cond (EC): %d %%", condutividadePct);

  display.setCursor(0, 52);
  display.printf("pH Simulado: %.1f", simulatedPH);

  display.display();
  delay(50);
}