#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "time.h" 
#include <WiFiManager.h>
#include <math.h>

#include "config.h"
#include "StatusLed.h" 
#include "WebHandler.h" 

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

const char* firmwareVersion = FIRMWARE_VERSION;
const char* firmwareReleaseApi =
    "https://api.github.com/repos/dgqgalaxy-create/Estacion_Meteorologica/releases/latest";
const unsigned long firmwareCheckInterval = 6UL * 60UL * 60UL * 1000UL;
unsigned long lastFirmwareCheck = 0;

// --- HARDWARE ---
Adafruit_BMP280 bmp; 
Adafruit_AHTX0 aht;

StatusLed ledWifi(12);
StatusLed ledSensor(14);
StatusLed ledError(27);

// --- VARIABLES GLOBALES ---
WebServer server(80);
Preferences preferences;

float temperature = 0.0;
float humidity = 0.0;
float pressure = 0.0;

float heatIndex = 0.0;
float dewPoint = 0.0;
float altitude = 0.0;

float tempMax = -100.0, tempMin = 100.0;
float humMax = 0.0, humMin = 100.0;
float presMax = 0.0, presMin = 2000.0;

bool sendToSheetsEnabled = true;
unsigned long intervaloEnvio = 10000;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -21600;
const int   daylightOffset_sec = 0;

float tempHistory[4][25]; 
float humHistory[4][25];  
int currentDay = -1;

// --- ESTADOS PARA EL ENVÍO NO BLOQUEANTE ---
enum EstadoEnvio {
  ENVIO_INACTIVO,
  ENVIO_INTENTANDO,
  ENVIO_ESPERA_REINTENTO,
  ENVIO_ERROR
};
EstadoEnvio estadoEnvio = ENVIO_INACTIVO;
unsigned long tiempoUltimoIntento = 0;
int intentosRealizados = 0;
const int maxIntentos = 6;
const unsigned long tiempoEntreIntentos = 10000;

float envioTemp, envioHum, envioPres;

// --- VARIABLES DE ESTADO PARA LA WEB ---
String estadoWifiWeb = "Desconectado";
String estadoSensorWeb = "OK";
String estadoSheetsWeb = "OK";

// --- DATOS PERSISTENTES PARA REINTENTOS Y RESPALDO ---
float lastTemp = 0.0, lastHum = 0.0, lastPres = 0.0;
bool lecturaValida = false;

// --- FUNCIONES MATEMÁTICAS ---
float calcularDewPoint(float t, float h) {
  float a = 17.27;
  float b = 237.7;
  float alpha = ((a * t) / (b + t)) + log(h / 100.0);
  return (b * alpha) / (a - alpha);
}

float calcularHeatIndex(float t, float h) {
  float tf = t * 1.8 + 32; 
  float hi = 0.5 * (tf + 61.0 + ((tf - 68.0) * 1.2) + (h * 0.094));
  if (hi > 80) {
    hi = -42.379 + 2.04901523 * tf + 10.14333127 * h - .22475541 * tf * h - .00683783 * tf * tf - .05481717 * h * h + .00122874 * tf * tf * h + .00085282 * tf * h * h - .00000199 * tf * tf * h * h;
  }
  return (hi - 32) * 0.55555;
}

// --- GESTIÓN DEL TIEMPO Y RESET DIARIO ---
void verificarCambioDeDia() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return;

  if (currentDay != timeinfo.tm_mday) {
    currentDay = timeinfo.tm_mday;
    
    for(int d=3; d>0; d--) {
        for(int i=0; i<25; i++) {
            tempHistory[d][i] = tempHistory[d-1][i];
            humHistory[d][i] = humHistory[d-1][i];
        }
    }
    for(int i=0; i<25; i++) {
        tempHistory[0][i] = NAN;
        humHistory[0][i] = NAN;
    }
    
    tempMax = -100.0; tempMin = 100.0;
    humMax = 0.0; humMin = 100.0;
    presMax = 0.0; presMin = 2000.0;
    
    Serial.println("--- NUEVO DÍA: Récords y Gráficas reiniciados ---");
  }
}

String obtenerHora() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "--:--:--"; 
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void guardarIntervalo(unsigned long nuevoIntervalo) {
  preferences.begin("config", false);
  preferences.putULong("intervalo", nuevoIntervalo);
  preferences.end();
  intervaloEnvio = nuevoIntervalo;
}

void actualizarHistorial(float t, float h) {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)) {
      int hora = timeinfo.tm_hour;
      if(hora >= 0 && hora <= 24) {
          tempHistory[0][hora] = t;
          humHistory[0][hora] = h;
      }
  }
}

void actualizarRecords(float t, float h, float p) {
    if (t > tempMax) tempMax = t;
    if (t < tempMin) tempMin = t;
    if (h > humMax) humMax = h;
    if (h < humMin) humMin = h;
    if (p > presMax) presMax = p;
    if (p < presMin) presMin = p;
}

// --- ENVÍO A GOOGLE SHEETS (no bloqueante, método original + antídotos 24/7) ---
void intentarEnvio() {
  if (WiFi.status() != WL_CONNECTED || !sendToSheetsEnabled) return;

  estadoSheetsWeb = "Enviando...";
  ledSensor.encender();

  HTTPClient http;
  http.begin(String(GOOGLE_SCRIPT_URL));
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);          // 🔒 Protección contra fugas de sockets a largo plazo

  String postData = "temp=" + String(envioTemp, 1) + "&hum=" + String(envioHum, 1) + "&pres=" + String(envioPres, 1);
  int codigo = http.POST(postData);
  
  // Diagnóstico opcional en el monitor serie (puedes eliminarlo más adelante)
  Serial.print("HTTP Code: ");
  Serial.println(codigo);
  if (codigo <= 0) {
    Serial.println("Fallo envío – socket liberado correctamente.");
  }
  
  http.end();
  ledSensor.apagar();

  if (codigo > 0) {
    ledError.apagar();
    estadoSheetsWeb = "OK";
    estadoEnvio = ENVIO_INACTIVO;
    intentosRealizados = 0;
  } else {
    intentosRealizados++;
    if (intentosRealizados >= maxIntentos) {
      ledError.parpadear(200);
      estadoSheetsWeb = "Error envío";
      estadoEnvio = ENVIO_INACTIVO;
      intentosRealizados = 0;
    } else {
      estadoSheetsWeb = "Reintentando...";
      estadoEnvio = ENVIO_ESPERA_REINTENTO;
      tiempoUltimoIntento = millis();
    }
  }
}

void iniciarEnvio(float t, float h, float p) {
  if (estadoEnvio == ENVIO_INACTIVO && sendToSheetsEnabled) {
    envioTemp = t;
    envioHum = h;
    envioPres = p;
    intentosRealizados = 0;
    estadoEnvio = ENVIO_INTENTANDO;
    intentarEnvio();
  }
}

void reintentarEnvioAhora() {
  if (estadoEnvio == ENVIO_INACTIVO || estadoEnvio == ENVIO_ERROR) {
    envioTemp = lastTemp;
    envioHum = lastHum;
    envioPres = lastPres;
    intentosRealizados = 0;
    estadoEnvio = ENVIO_INTENTANDO;
    intentarEnvio();
    ledError.apagar();
    estadoSheetsWeb = "Reintentando...";
  }
}

void procesarEstadoEnvio() {
  if (!sendToSheetsEnabled) {
    if (estadoEnvio != ENVIO_INACTIVO) {
      estadoEnvio = ENVIO_INACTIVO;
      intentosRealizados = 0;
      ledSensor.apagar();
      estadoSheetsWeb = "Pausado";
    }
    return;
  }

  switch (estadoEnvio) {
    case ENVIO_ESPERA_REINTENTO:
      if (millis() - tiempoUltimoIntento >= tiempoEntreIntentos) {
        estadoEnvio = ENVIO_INTENTANDO;
        intentarEnvio();
      }
      break;
    default:
      break;
  }
}

// --- LECTURA DE SENSORES (validación corregida para altitud) ---
void leerSensor() {
  for (int intento = 0; intento < 3; intento++) {
    sensors_event_t humidity_event, temp_event;
    aht.getEvent(&humidity_event, &temp_event); 
    float p = bmp.readPressure() / 100.0F;

    if (!isnan(p) && p >= 600 && p <= 1100 &&
        !isnan(temp_event.temperature) && temp_event.temperature >= -20 && temp_event.temperature <= 60 &&
        !isnan(humidity_event.relative_humidity) && humidity_event.relative_humidity >= 0 && humidity_event.relative_humidity <= 100) {
      
      temperature = temp_event.temperature;
      humidity = humidity_event.relative_humidity;
      pressure = p;
      
      heatIndex = calcularHeatIndex(temperature, humidity);
      dewPoint = calcularDewPoint(temperature, humidity);
      altitude = bmp.readAltitude(1013.25);

      verificarCambioDeDia();
      actualizarHistorial(temperature, humidity);
      actualizarRecords(temperature, humidity, pressure);

      ledError.apagar();
      estadoSensorWeb = "OK";
      
      lastTemp = temperature;
      lastHum = humidity;
      lastPres = pressure;
      lecturaValida = true;

      iniciarEnvio(temperature, humidity, pressure);
      return;
    }
    
    if (intento < 2) {
      delay(200);
    }
  }
  
  if (lecturaValida) {
    temperature = lastTemp;
    humidity = lastHum;
    pressure = lastPres;
    estadoSensorWeb = "Sensor recuperando";
  } else {
    estadoSensorWeb = "Error sensor";
  }
  ledError.parpadear(100);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  for(int i=0; i<3; i++) {
    digitalWrite(12, HIGH); digitalWrite(14, HIGH); digitalWrite(27, HIGH); delay(200);
    digitalWrite(12, LOW); digitalWrite(14, LOW); digitalWrite(27, LOW); delay(200);
  }
  digitalWrite(12, HIGH); 
}

void configurarOTA() {
  ArduinoOTA.setHostname("estacion-clima");
  ArduinoOTA.begin();
  MDNS.begin("estacion-clima");
}

String extraerJsonString(const String& json, const String& key, int desde = 0) {
  String marker = "\"" + key + "\":\"";
  int inicio = json.indexOf(marker, desde);
  if (inicio < 0) return "";
  inicio += marker.length();
  int fin = json.indexOf('"', inicio);
  if (fin < 0) return "";
  return json.substring(inicio, fin);
}

bool versionNueva(const String& remota) {
  String version = remota;
  if (version.startsWith("v") || version.startsWith("V")) version.remove(0, 1);

  int localMajor, localMinor, localPatch;
  int remoteMajor, remoteMinor, remotePatch;
  if (sscanf(firmwareVersion, "%d.%d.%d", &localMajor, &localMinor, &localPatch) != 3 ||
      sscanf(version.c_str(), "%d.%d.%d", &remoteMajor, &remoteMinor, &remotePatch) != 3) {
    return false;
  }

  if (remoteMajor != localMajor) return remoteMajor > localMajor;
  if (remoteMinor != localMinor) return remoteMinor > localMinor;
  return remotePatch > localPatch;
}

void comprobarActualizacionFirmware() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.begin(client, firmwareReleaseApi);
  http.addHeader("User-Agent", "Estacion-Meteorologica-ESP32");

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("No se pudo consultar firmware (%d)\n", codigo);
    http.end();
    return;
  }

  String respuesta = http.getString();
  http.end();

  String versionRemota = extraerJsonString(respuesta, "tag_name");
  int firmwareAsset = respuesta.indexOf("\"name\":\"firmware.bin\"");
  String urlFirmware = extraerJsonString(respuesta, "browser_download_url", firmwareAsset);
  if (versionRemota.isEmpty() || urlFirmware.isEmpty()) {
    Serial.println("Release sin tag o firmware.bin");
    return;
  }

  Serial.printf("Firmware local: %s, remoto: %s\n", firmwareVersion, versionRemota.c_str());
  if (!versionNueva(versionRemota)) return;

  Serial.printf("Actualizando a %s...\n", versionRemota.c_str());
  estadoSheetsWeb = "Actualizando firmware...";
  WiFiClientSecure updateClient;
  updateClient.setInsecure();
  t_httpUpdate_return resultado = httpUpdate.update(updateClient, urlFirmware);

  if (resultado == HTTP_UPDATE_FAILED) {
    Serial.printf("Fallo OTA: %s\n", httpUpdate.getLastErrorString().c_str());
    estadoSheetsWeb = "Error OTA";
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); 
  if (!aht.begin()) Serial.println("Fallo AHT20");
  if (!bmp.begin(0x76) && !bmp.begin(0x77)) Serial.println("Fallo BMP280");
  
  for(int d=0; d<4; d++) {
    for(int i=0; i<25; i++) { 
        tempHistory[d][i] = NAN; 
        humHistory[d][i] = NAN; 
    }
  }

  preferences.begin("config", true);
  intervaloEnvio = preferences.getULong("intervalo", 10000);
  preferences.end();

  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("Estacion-Clima-Config")) ESP.restart();
  WiFi.setAutoReconnect(true);

  if (WiFi.status() == WL_CONNECTED) {
    ledWifi.parpadear(2000);
    estadoWifiWeb = "Conectado";
  } else {
    ledWifi.parpadear(100);
    estadoWifiWeb = "Reconectando...";
  }
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("WiFi perdido. Intentando reconectar...");
    estadoWifiWeb = "Reconectando...";
    ledWifi.parpadear(100);
  }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("WiFi conectado. IP: " + WiFi.localIP().toString());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ledWifi.parpadear(2000);
    estadoWifiWeb = "Conectado";
  }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);

  delay(2000); 
  verificarCambioDeDia();

  configurarOTA();
  setupWeb();

  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);
  
  leerSensor();
  lastFirmwareCheck = millis() - firmwareCheckInterval + 60000;
}

void loop() {
  esp_task_wdt_reset(); 
  ArduinoOTA.handle();  
  server.handleClient(); 
  
  static unsigned long tiempoSinWiFi = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (tiempoSinWiFi == 0) tiempoSinWiFi = millis();
    else if (millis() - tiempoSinWiFi > 5000) {
      Serial.println("Reintentando conexión WiFi forzada...");
      WiFi.reconnect();
      tiempoSinWiFi = millis();
    }
  } else {
    tiempoSinWiFi = 0;
  }
  
  ledWifi.actualizar(); ledSensor.actualizar(); ledError.actualizar();

  procesarEstadoEnvio();

  if (millis() - lastFirmwareCheck >= firmwareCheckInterval) {
    lastFirmwareCheck = millis();
    comprobarActualizacionFirmware();
  }

  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime >= intervaloEnvio) {
    lastSendTime = millis();
    leerSensor();
  }
}