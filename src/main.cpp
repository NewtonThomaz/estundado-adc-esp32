#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>

#define PIN_POTENCIOMETRO 34
#define PIN_DHT           23
#define PIN_BTN_PH_UP     18
#define PIN_BTN_PH_DOWN   19

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

struct TelemetriaSensores {
  int valorRawADC;
  int condutividadePct;
  float temperatura;
  float umidade;
  float ph;
  bool dhtValido;
  unsigned long timestampMs;
};

TelemetriaSensores telemetriaAtual = {
  .valorRawADC = 0,
  .condutividadePct = 0,
  .temperatura = 0.0f,
  .umidade = 0.0f,
  .ph = 7.0f,
  .dhtValido = false,
  .timestampMs = 0
};

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000;

void lerEletrocondutividade(TelemetriaSensores &dados) {
  long soma = 0;
  for (int i = 0; i < 16; i++) {
    soma += analogRead(PIN_POTENCIOMETRO);
    delayMicroseconds(200);
  }
  dados.valorRawADC = soma / 16;
  dados.condutividadePct = map(dados.valorRawADC, 0, 4095, 0, 100);
}

void lerDHT(TelemetriaSensores &dados) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    dados.dhtValido = false;
    Serial.println("[ERRO DHT] Falha ao ler sensor! Verifique conexao no GPIO 23 / Alimentacao.");
  } else {
    dados.temperatura = t;
    dados.umidade = h;
    dados.dhtValido = true;
  }
}

void processarBotoesPH(TelemetriaSensores &dados) {
  if (digitalRead(PIN_BTN_PH_UP) == LOW) {
    if (dados.ph < 14.0f) dados.ph += 0.1f;
    delay(150);
  }
  if (digitalRead(PIN_BTN_PH_DOWN) == LOW) {
    if (dados.ph > 0.0f) dados.ph -= 0.1f;
    delay(150);
  }
}

void atualizarDisplay(const TelemetriaSensores &dados) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("--- AGROSYNC IOT ---");

  display.setCursor(0, 16);
  if (!dados.dhtValido && dados.temperatura == 0.0f) {
    display.print("DHT: Lendo/Falha...");
  } else {
    display.printf("Temp: %.1f C", dados.temperatura);
  }

  display.setCursor(0, 28);
  display.printf("Umid: %.1f %%", dados.umidade);

  display.setCursor(0, 40);
  display.printf("Cond (EC): %d %%", dados.condutividadePct);

  display.setCursor(0, 52);
  display.printf("pH: %.1f | ADC: %d", dados.ph, dados.valorRawADC);

  display.display();
}

void logTelemetriaSerial(const TelemetriaSensores &dados) {
  Serial.printf("[TELEMETRIA] T: %.1f C | U: %.1f%% | EC: %d%% (ADC: %d) | pH: %.1f | DHT: %s | Timestamp: %lums\n",
                dados.temperatura,
                dados.umidade,
                dados.condutividadePct,
                dados.valorRawADC,
                dados.ph,
                dados.dhtValido ? "OK" : "FALHA",
                dados.timestampMs);
}

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
    display.setCursor(15, 25);
    display.println("AgroSync Iniciado");
    display.display();
    delay(1000);
  }
}

void loop() {
  processarBotoesPH(telemetriaAtual);
  lerEletrocondutividade(telemetriaAtual);
  telemetriaAtual.timestampMs = millis();

  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = currentMillis;
    lerDHT(telemetriaAtual);
    logTelemetriaSerial(telemetriaAtual);
  }

  atualizarDisplay(telemetriaAtual);
  delay(50);
}