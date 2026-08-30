#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>
#include "soc/soc.h"             
#include "soc/rtc_cntl_reg.h"    

const char *WIFI_SSID = "NewtonHome";            
const char *WIFI_PASS = "NewMarNewIsaFla@2007"; 

const char *TELEMETRY_ENDPOINT = "http://webhook.site/c89d070c-f6b9-48c4-a7a2-a4e433382524"; 

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

unsigned long lastTelemetryDispatch = 0;
const unsigned long DISPATCH_INTERVAL = 5000;    

unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 150;      

unsigned long lastBtnPhUpTime = 0;
unsigned long lastBtnPhDownTime = 0;
const unsigned long DEBOUNCE_DELAY = 180;

bool wifiConectado = false;
String ipLocal = "Sem IP";
int wifiRSSI = 0;
unsigned long totalPacotesEnviados = 0;
int ultimoHttpStatus = 0;
String ultimoStatusMsg = "Iniciando...";

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("[Wi-Fi] Conectado ao Ponto de Acesso (AP) com sucesso!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  wifiConectado = true;
  ipLocal = WiFi.localIP().toString();
  wifiRSSI = WiFi.RSSI();
  Serial.printf("[Wi-Fi] IP Obtido via DHCP: %s | Sinal (RSSI): %d dBm\n", 
                ipLocal.c_str(), wifiRSSI);
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  wifiConectado = false;
  ipLocal = "Offline";
  Serial.println("[Wi-Fi] Desconectado da rede! Tentando reconectar em background...");
  WiFi.reconnect();
}

void inicializarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  WiFi.setTxPower(WIFI_POWER_11dBm);

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  Serial.printf("[Wi-Fi] Conectando a rede SSID: %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

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
      if (dados.ph < 14.0f) dados.ph += 0.1f;
      lastBtnPhUpTime = agora;
    }
  }

  if (digitalRead(PIN_BTN_PH_DOWN) == LOW) {
    if (agora - lastBtnPhDownTime >= DEBOUNCE_DELAY) {
      if (dados.ph > 0.0f) dados.ph -= 0.1f;
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
  fert["alertaEC"] = (dados.condutividadePct > 80 || dados.condutividadePct < 20);

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

void executarDisparoHttp(const String &jsonPayload) {
  totalPacotesEnviados++;

  bool conectado = (WiFi.status() == WL_CONNECTED);
  if (!conectado) {
    ultimoHttpStatus = -1;
    ultimoStatusMsg = "Wi-Fi Desconectado";
    Serial.printf("\n[HTTP POST] Pacote #%lu cancelado: Sem conexao Wi-Fi (Status: %d)\n", 
                  totalPacotesEnviados, WiFi.status());
    return;
  }

  WiFiClient client;
  HTTPClient http;
  
  if (http.begin(client, TELEMETRY_ENDPOINT)) {
    http.setTimeout(4000); 
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Token", "agrosync-device-jwt-secret");

    Serial.println("\n=======================================================");
    Serial.printf(">>> [HTTP POST] DISPARANDO TELEMETRIA #%lu VIA WI-FI\n", totalPacotesEnviados);
    Serial.printf("Endpoint : %s\n", TELEMETRY_ENDPOINT);
    Serial.println("Payload  : " + jsonPayload);

    unsigned long startReq = millis();
    int httpResponseCode = http.POST(jsonPayload);
    unsigned long tempoGasto = millis() - startReq;

    ultimoHttpStatus = httpResponseCode;

    if (httpResponseCode > 0) {
      Serial.printf("<<< [HTTP STATUS: %d] Sucesso em %lums!\n", httpResponseCode, tempoGasto);
      ultimoStatusMsg = "POST " + String(httpResponseCode) + " OK (" + String(tempoGasto) + "ms)";
    } else {
      Serial.printf("<<< [HTTP ERRO]: %s (%d) apos %lums\n", 
                    http.errorToString(httpResponseCode).c_str(), httpResponseCode, tempoGasto);
      ultimoStatusMsg = "Erro HTTP: " + String(httpResponseCode);
    }
    Serial.println("=======================================================\n");

    http.end();
  } else {
    Serial.println("[HTTP] Falha ao iniciar conexao com o endpoint!");
    ultimoStatusMsg = "Falha conexao HTTP";
  }
}

void atualizarDisplay(const TelemetriaSensores &dados) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  if (wifiConectado) {
    display.printf("WiFi: ON (%ddBm)", WiFi.RSSI());
  } else {
    display.print("WiFi: OFFLINE");
  }

  display.setCursor(0, 13);
  if (dados.dhtValido) {
    display.printf("T: %.1fC | U: %.1f%%", dados.temperatura, dados.umidade);
  } else {
    display.print("DHT: [Sem Sinal]");
  }

  display.setCursor(0, 25);
  display.printf("EC: %d%% | pH: %.1f", dados.condutividadePct, dados.ph);

  display.setCursor(0, 37);
  if (wifiConectado) {
    display.printf("IP: %s", ipLocal.c_str());
  } else {
    display.print("IP: Desconectado");
  }

  display.setCursor(0, 50);
  display.printf("#%lu: %s", totalPacotesEnviados, ultimoStatusMsg.c_str());

  display.display();
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_POTENCIOMETRO, INPUT);

  pinMode(PIN_BTN_PH_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_PH_DOWN, INPUT_PULLUP);

  dht.begin();
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("[Display] Falha ao inicializar o OLED SSD1306!");
  } else {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("AgroSync Node v1.0");
    display.setCursor(10, 35);
    display.println("Conectando Wi-Fi...");
    display.display();
  }

  delay(300);
  inicializarWiFi();
  Serial.println("[SISTEMA] Loop temporal e estacao Wi-Fi configurados com sucesso.");
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
    executarDisparoHttp(payloadJson);
  }

  if (agora - lastDisplayUpdate >= DISPLAY_INTERVAL) {
    lastDisplayUpdate = agora;
    atualizarDisplay(telemetriaAtual);
  }
}