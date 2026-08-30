#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
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

struct TelemetriaSensores {
  int valorRawADC;
  int condutividadePct;
  float temperatura;
  float umidade;
  float ph;
  bool dhtValido;
  unsigned long timestampMs;
};

TelemetriaSensores telemetriaAtual = {.valorRawADC = 0,
                                      .condutividadePct = 0,
                                      .temperatura = 0.0f,
                                      .umidade = 0.0f,
                                      .ph = 7.0f,
                                      .dhtValido = false,
                                      .timestampMs = 0};

unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 2000;

unsigned long lastTelemetryDispatch = 0;
const unsigned long DISPATCH_INTERVAL = 2000;

unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 100;

unsigned long lastBtnPhUpTime = 0;
unsigned long lastBtnPhDownTime = 0;
const unsigned long DEBOUNCE_DELAY = 180;

unsigned long totalPacotesEnviados = 0;
String ultimoStatusEnvio = "Aguardando";

void lerEletrocondutividade(TelemetriaSensores &dados) {
  long soma = 0;
  for (int i = 0; i < 16; i++) {
    soma += analogRead(PIN_POTENCIOMETRO);
    delayMicroseconds(100);
  }
  dados.valorRawADC = soma / 16;
  dados.condutividadePct = map(dados.valorRawADC, 0, 4095, 0, 100);
}

void lerDHT(TelemetriaSensores &dados) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    dados.dhtValido = false;
  } else {
    dados.temperatura = t;
    dados.umidade = h;
    dados.dhtValido = true;
  }
}

void processarBotoesPH(TelemetriaSensores &dados) {
  unsigned long agora = millis();

  if (digitalRead(PIN_BTN_PH_UP) == LOW) {
    if (agora - lastBtnPhUpTime >= DEBOUNCE_DELAY) {
      if (dados.ph < 14.0f)
        dados.ph += 0.1f;
      lastBtnPhUpTime = agora;
    }
  }

  if (digitalRead(PIN_BTN_PH_DOWN) == LOW) {
    if (agora - lastBtnPhDownTime >= DEBOUNCE_DELAY) {
      if (dados.ph > 0.0f)
        dados.ph -= 0.1f;
      lastBtnPhDownTime = agora;
    }
  }
}

String serializarTelemetriaJson(const TelemetriaSensores &dados) {
  JsonDocument doc;

  doc["dispositivoId"] = "esp32-estufa-01";
  doc["tipoDispositivo"] = "AGRO_NODE_V1";

  doc["dataHoraLocalMs"] = dados.timestampMs;
  doc["uptimeSegundos"] = millis() / 1000;

  JsonObject ambient = doc["ambiente"].to<JsonObject>();
  if (dados.dhtValido) {
    ambient["temperatura"] = round(dados.temperatura * 10.0f) / 10.0f;
    ambient["umidade"] = round(dados.umidade * 10.0f) / 10.0f;
    ambient["sensorStatus"] = "OK";
  } else {
    ambient["temperatura"] = nullptr;
    ambient["umidade"] = nullptr;
    ambient["sensorStatus"] = "ERRO_LEITURA";
  }

  JsonObject fert = doc["solucaoNutritiva"].to<JsonObject>();
  fert["valorEC"] = dados.condutividadePct;
  fert["rawADC"] = dados.valorRawADC;
  fert["ph"] = round(dados.ph * 10.0f) / 10.0f;

  if (dados.condutividadePct > 80 || dados.condutividadePct < 20) {
    fert["alertaEC"] = true;
  } else {
    fert["alertaEC"] = false;
  }

  String jsonString;
  serializeJsonPretty(doc, jsonString);
  return jsonString;
}

void simularDisparoHttp(const String &jsonPayload) {
  totalPacotesEnviados++;

  Serial.println("\n=======================================================");
  Serial.printf(">>> [HTTP POST] DISPARO DE TELEMETRIA #%lu (5s)\n",
                totalPacotesEnviados);
  Serial.println("Endpoint: POST /api/v1/telemetria/leitura");
  Serial.println("Header  : Content-Type: application/json");
  Serial.println("Header  : X-Device-Token: agrosync-jwt-token");
  Serial.println("----------------- PAYLOAD JSON GERADO -----------------");
  Serial.println(jsonPayload);
  Serial.println("-------------------------------------------------------");
  Serial.println("<<< [HTTP STATUS: 201 Created] Pacote persistido com sucesso "
                 "no AgroSync!");
  Serial.println("=======================================================\n");

  ultimoStatusEnvio = "POST 201 OK #" + String(totalPacotesEnviados);
}

void atualizarDisplay(const TelemetriaSensores &dados) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("AGROSYNC - IOT NODE");

  display.setCursor(0, 14);
  if (dados.dhtValido) {
    display.printf("T: %.1fC  U: %.1f%%", dados.temperatura, dados.umidade);
  } else {
    display.print("DHT: [Falha/Lendo]");
  }

  display.setCursor(0, 26);
  display.printf("EC: %d%% | pH: %.1f", dados.condutividadePct, dados.ph);

  unsigned long tempoRestante =
      (DISPATCH_INTERVAL - (millis() - lastTelemetryDispatch)) / 1000;
  display.setCursor(0, 40);
  display.printf("Prox Envio: %lus", tempoRestante + 1);

  display.setCursor(0, 52);
  display.printf("Envios: #%lu", totalPacotesEnviados);

  display.display();
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

  Serial.println("\n[SISTEMA INICIADO] Loop Temporal nao-bloqueante ativado.");
}

void loop() {
  unsigned long agora = millis();

  processarBotoesPH(telemetriaAtual);

  if (agora - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = agora;
    lerEletrocondutividade(telemetriaAtual);
    lerDHT(telemetriaAtual);
    telemetriaAtual.timestampMs = agora;
  }

  if (agora - lastTelemetryDispatch >= DISPATCH_INTERVAL) {
    lastTelemetryDispatch = agora;
    String payloadJson = serializarTelemetriaJson(telemetriaAtual);
    simularDisparoHttp(payloadJson);
  }

  if (agora - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = agora;
    atualizarDisplay(telemetriaAtual);
  }
}