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
#include <LittleFS.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include "time.h" 
#include <WiFiManager.h>
#include <math.h>

#include "config.h"
#include "StatusLed.h" 
#include "WebHandler.h" 

// La versión se define en una única fuente: platformio.ini (build_flags).
// Mantenerla aquí duplicada provocó releases desincronizadas.
#ifndef FIRMWARE_VERSION
#error "FIRMWARE_VERSION no esta definida: defínela en platformio.ini dentro de build_flags"
#endif

const char* firmwareVersion = FIRMWARE_VERSION;
const char* firmwareBuildDate = __DATE__ " " __TIME__;
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

float tempHistory[4][24]; 
float humHistory[4][24];  
int currentDay = -1;

// --- ESTADOS PARA EL ENVÍO NO BLOQUEANTE ---
enum EstadoEnvio {
  ENVIO_INACTIVO,
  ENVIO_INTENTANDO,
  ENVIO_ESPERA_REINTENTO
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
String estadoFirmwareWeb = "Actualizado";
bool otaEnProgreso = false; // Evita lanzar dos OTA simultáneas
bool otaNuevaPendiente = false; // True tras instalar un OTA, hasta arranque sano

// --- LOG DE EVENTOS EN RAM (últimos eventos para diagnóstico web) ---
const int LOG_EVENTOS_MAX = 20;
String eventosLog[LOG_EVENTOS_MAX];
int eventosIdx = 0;
int eventosCount = 0;

// --- TENDENCIA DE PRESIÓN (anillo de muestras con época) ---
const int RING_PRES_MAX = 512;
time_t presTiempos[RING_PRES_MAX];
float presValores[RING_PRES_MAX];
int presN = 0;
int presIdx = 0;
float tendenciaActual = NAN;       // hPa por hora
String tendenciaEstadoWeb = "--";

// --- ALERTAS CONFIGURABLES (guardadas en NVS) ---
bool alertasEnabled = false;
float alertaTempMax = 35.0;
float alertaTempMin = 0.0;
float alertaHumMax = 85.0;
float alertaHumMin = 15.0;
String alertaWeb = "Sin alertas";

// --- DATOS PERSISTENTES PARA REINTENTOS Y RESPALDO ---
float lastTemp = 0.0, lastHum = 0.0, lastPres = 0.0;
bool lecturaValida = false;

// URL de Google Sheets en uso. Se persiste en NVS (configurable desde el panel);
// config.h solo se usa como valor inicial del primer arranque.
String sheetsUrlActual;

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
// La hora solo es válida tras sincronizar NTP; sin eso la fecha queda en 1970
// y rompería el historial/récords diarios con datos fantasma.
bool tiempoSincronizado() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 200)) return false;
  return timeinfo.tm_year >= (2024 - 1900);
}

void verificarCambioDeDia() {
  struct tm timeinfo;
  if (!tiempoSincronizado()) return;
  if(!getLocalTime(&timeinfo)) return;

  if (currentDay != timeinfo.tm_mday) {
    currentDay = timeinfo.tm_mday;
    
    for(int d=3; d>0; d--) {
        for(int i=0; i<24; i++) {
            tempHistory[d][i] = tempHistory[d-1][i];
            humHistory[d][i] = humHistory[d-1][i];
        }
    }
    for(int i=0; i<24; i++) {
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
  if(!tiempoSincronizado()) return "--:--:--"; 
  getLocalTime(&timeinfo);
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

String obtenerFecha() {
  struct tm timeinfo;
  if (!tiempoSincronizado()) return "--/--/----";
  getLocalTime(&timeinfo);
  char buf[16];
  strftime(buf, sizeof(buf), "%d/%m/%Y", &timeinfo);
  return String(buf);
}

void registrarEvento(const String& e) {
  String hora = obtenerHora();
  String linea = (hora == "--:--:--") ? e : hora + "  " + e;
  eventosLog[eventosIdx] = linea;
  eventosIdx = (eventosIdx + 1) % LOG_EVENTOS_MAX;
  if (eventosCount < LOG_EVENTOS_MAX) eventosCount++;
}

String obtenerLogEventos() {
  String out;
  for (int i = 0; i < eventosCount; i++) {
    int idx = (eventosIdx - eventosCount + i + LOG_EVENTOS_MAX) % LOG_EVENTOS_MAX;
    out += eventosLog[idx];
    out += '\n';
  }
  return out;
}

void guardarIntervalo(unsigned long nuevoIntervalo) {
  preferences.begin("config", false);
  preferences.putULong("intervalo", nuevoIntervalo);
  preferences.end();
  intervaloEnvio = nuevoIntervalo;
}

void guardarSheetsUrl(const String& url) {
  String limpia = url;
  limpia.trim();
  if (!limpia.startsWith("https://") && !limpia.startsWith("http://")) {
    Serial.println("URL de Google Sheets invalida; se ignora");
    return;
  }
  sheetsUrlActual = limpia;
  preferences.begin("sheets", false);
  preferences.putString("url", limpia);
  preferences.end();
  registrarEvento("Sheets: URL configurada");
  Serial.println("URL de Google Sheets guardada en NVS");
}

String obtenerSheetsUrl() {
  if (sheetsUrlActual.length() > 0) return sheetsUrlActual;
  return String(GOOGLE_SCRIPT_URL);
}

void actualizarHistorial(float t, float h) {
  struct tm timeinfo;
  if(getLocalTime(&timeinfo) && timeinfo.tm_year >= (2024 - 1900)) {
      int hora = timeinfo.tm_hour;
      if(hora >= 0 && hora <= 23) {
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

// --- TENDENCIA DE PRESIÓN ---
// Variación en hPa/hora entre la muestra más reciente y otra con >= 20 min
// de antigüedad (busca la primera que ya supere la ventana).
float tendenciaPresionHora() {
  if (presN < 2) return NAN;
  int ultimo = (presIdx - 1 + RING_PRES_MAX) % RING_PRES_MAX;
  time_t tUlt = presTiempos[ultimo];
  float pUlt = presValores[ultimo];
  for (int k = 0; k < presN; k++) {
    int i = (presIdx - 1 - k + 2 * RING_PRES_MAX) % RING_PRES_MAX;
    double dt = difftime(tUlt, presTiempos[i]);
    if (dt >= 1200) { // 20 minutos
      return (pUlt - presValores[i]) / (float)(dt / 3600.0);
    }
  }
  return NAN;
}

void registrarMuestraPres(float p) {
  if (!tiempoSincronizado()) return;
  presTiempos[presIdx] = time(nullptr);
  presValores[presIdx] = p;
  presIdx = (presIdx + 1) % RING_PRES_MAX;
  if (presN < RING_PRES_MAX) presN++;

  tendenciaActual = tendenciaPresionHora();
  if (isnan(tendenciaActual)) {
    tendenciaEstadoWeb = "--";
  } else if (tendenciaActual >= 0.30) {
    tendenciaEstadoWeb = "Subiendo";
  } else if (tendenciaActual <= -0.30) {
    tendenciaEstadoWeb = "Bajando";
  } else {
    tendenciaEstadoWeb = "Estable";
  }
}

// --- ALERTAS CONFIGURABLES ---
void cargarAlertas() {
  preferences.begin("alerts", true);
  alertasEnabled = preferences.getBool("enabled", false);
  alertaTempMax = preferences.getFloat("tmax", 35.0);
  alertaTempMin = preferences.getFloat("tmin", 0.0);
  alertaHumMax = preferences.getFloat("hmax", 85.0);
  alertaHumMin = preferences.getFloat("hmin", 15.0);
  preferences.end();
}

void guardarAlertas() {
  preferences.begin("alerts", false);
  preferences.putBool("enabled", alertasEnabled);
  preferences.putFloat("tmax", alertaTempMax);
  preferences.putFloat("tmin", alertaTempMin);
  preferences.putFloat("hmax", alertaHumMax);
  preferences.putFloat("hmin", alertaHumMin);
  preferences.end();
}

void evaluarAlertas() {
  String nueva = "Sin alertas";
  if (alertasEnabled) {
    if (temperature > alertaTempMax) nueva = "Temperatura alta (" + String(temperature, 1) + " C)";
    else if (temperature < alertaTempMin) nueva = "Temperatura baja (" + String(temperature, 1) + " C)";
    else if (humidity > alertaHumMax) nueva = "Humedad alta (" + String(humidity, 0) + " %)";
    else if (humidity < alertaHumMin) nueva = "Humedad baja (" + String(humidity, 0) + " %)";
  }
  if (nueva != alertaWeb) {
    if (nueva != "Sin alertas") registrarEvento("Alerta: " + nueva);
    else registrarEvento("Alerta: condiciones normales");
    alertaWeb = nueva;
  }
}

// --- COLA OFFLINE DE LECTURAS (LittleFS) ---
// Si el WiFi está caído o el envío falla, las lecturas se guardan aquí y se
// reenvían automáticamente cuando hay red y el envío está activado.
#define COLA_ARCHIVO "/cola.csv"
const int COLA_MAX = 200;
bool fsListo = false;
int colaPendiente = 0;

int contarCola() {
  if (!fsListo) return 0;
  File f = LittleFS.open(COLA_ARCHIVO, "r");
  if (!f) return 0;
  int n = 0;
  while (f.available()) {
    String linea = f.readStringUntil('\n');
    linea.trim();
    if (linea.length() > 0) n++;
  }
  f.close();
  return n;
}

bool inicializarFS() {
  fsListo = LittleFS.begin(true, "/littlefs", 8, "spiffs");
  if (fsListo) {
    colaPendiente = contarCola();
    Serial.printf("LittleFS listo (%d lecturas pendientes)\n", colaPendiente);
  } else {
    Serial.println("Fallo al montar LittleFS");
  }
  return fsListo;
}

void encolarLectura(float t, float h, float p) {
  if (!fsListo) return;
  if (colaPendiente >= COLA_MAX) {
    registrarEvento("Cola: llena, se descarta lectura");
    return;
  }
  File f = LittleFS.open(COLA_ARCHIVO, FILE_APPEND);
  if (f) {
    f.printf("%.1f,%.1f,%.1f\n", t, h, p);
    f.close();
    colaPendiente++;
  }
}

// Envía UNA lectura a Google Sheets; true si el servidor respondió (2xx/3xx).
bool enviarLecturaHttp(float t, float h, float p) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String url = obtenerSheetsUrl();
  if (url.length() < 15) return false;
  HTTPClient http;
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);          // 🔒 Protección contra fugas de sockets a largo plazo
  String postData = "temp=" + String(t, 1) + "&hum=" + String(h, 1) + "&pres=" + String(p, 1);
  int codigo = http.POST(postData);
  http.end();
  if (codigo <= 0) {
    Serial.printf("Envio fallido (HTTP %d)\n", codigo);
    return false;
  }
  return true;
}

// Reenvía hasta maxPorVez lecturas pendientes (la más antigua primero).
void drenarCola(int maxPorVez) {
  if (!fsListo || colaPendiente <= 0) return;
  if (WiFi.status() != WL_CONNECTED || !sendToSheetsEnabled) return;
  if (estadoEnvio != ENVIO_INACTIVO) return; // no interferir con un envío en curso

  String lineas[COLA_MAX];
  int n = 0;
  File f = LittleFS.open(COLA_ARCHIVO, "r");
  if (!f) return;
  while (f.available() && n < COLA_MAX) {
    String linea = f.readStringUntil('\n');
    linea.trim();
    if (linea.length() > 0) lineas[n++] = linea;
  }
  f.close();

  int enviadas = 0;
  for (int i = 0; i < n && enviadas < maxPorVez; i++) {
    float t, h, p;
    if (sscanf(lineas[i].c_str(), "%f,%f,%f", &t, &h, &p) == 3) {
      if (enviarLecturaHttp(t, h, p)) {
        enviadas++;
        colaPendiente--;
      } else {
        break; // si falla una, esperar al siguiente ciclo
      }
    }
  }
  if (enviadas > 0) {
    File fw = LittleFS.open(COLA_ARCHIVO, "w");
    if (fw) {
      for (int i = enviadas; i < n; i++) fw.println(lineas[i]);
      fw.close();
    }
    registrarEvento("Cola: " + String(enviadas) + " lecturas reenviadas (" +
                    String(colaPendiente) + " pendientes)");
  }
}

// --- ENVÍO A GOOGLE SHEETS (no bloqueante, método original + antídotos 24/7) ---
void intentarEnvio() {
  if (WiFi.status() != WL_CONNECTED || !sendToSheetsEnabled) return;
  static bool envioFallando = false;

  estadoSheetsWeb = "Enviando...";
  ledSensor.encender();

  bool ok = enviarLecturaHttp(envioTemp, envioHum, envioPres);
  ledSensor.apagar();

  if (ok) {
    if (envioFallando) {
      registrarEvento("Sheets: envio recuperado");
      envioFallando = false;
    }
    ledError.apagar();
    estadoSheetsWeb = "OK";
    estadoEnvio = ENVIO_INACTIVO;
    intentosRealizados = 0;
  } else {
    if (!envioFallando) {
      registrarEvento("Sheets: fallo de envio");
      envioFallando = true;
    }
    intentosRealizados++;
    if (intentosRealizados >= maxIntentos) {
      ledError.parpadear(200);
      estadoSheetsWeb = "Error envío";
      estadoEnvio = ENVIO_INACTIVO;
      intentosRealizados = 0;
      // Conservar la lectura para reenviarla después (cola offline)
      encolarLectura(envioTemp, envioHum, envioPres);
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
  if (estadoEnvio == ENVIO_INACTIVO) {
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
  static bool sensorEnError = false;
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
      registrarMuestraPres(pressure);
      evaluarAlertas();

      if (sensorEnError) {
        registrarEvento("Sensor: lectura recuperada");
        sensorEnError = false;
      }
      ledError.apagar();
      estadoSensorWeb = "OK";
      
      lastTemp = temperature;
      lastHum = humidity;
      lastPres = pressure;
      lecturaValida = true;

      if (sendToSheetsEnabled) {
        if (WiFi.status() == WL_CONNECTED) {
          iniciarEnvio(temperature, humidity, pressure);
        } else {
          // Sin red: guardar la lectura para reenviarla cuando vuelva el WiFi
          encolarLectura(temperature, humidity, pressure);
        }
      }
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
  if (!sensorEnError) {
    registrarEvento("Sensor: error de lectura");
    sensorEnError = true;
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
  String marker = "\"" + key + "\"";
  int inicio = json.indexOf(marker, desde);
  if (inicio < 0) return "";
  inicio = json.indexOf(':', inicio + marker.length());
  if (inicio < 0) return "";
  inicio++;
  while (inicio < json.length() && isspace(json[inicio])) inicio++;
  if (inicio >= json.length() || json[inicio] != '"') return "";
  inicio++;
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

void mostrarBaileActualizacion(unsigned int progreso, unsigned int total) {
  static int ultimoPaso = -1;
  int paso = total == 0 ? 0 : (progreso * 4UL / total) % 4;
  if (paso == ultimoPaso) return;
  ultimoPaso = paso;

  ledWifi.apagar();
  ledSensor.apagar();
  ledError.apagar();

  if (paso == 0) ledWifi.encender();
  if (paso == 1) ledSensor.encender();
  if (paso == 2) ledError.encender();
  if (paso == 3) {
    ledWifi.encender();
    ledSensor.encender();
    ledError.encender();
  }
}

// --- GESTIÓN POST-OTA (marcar pendiente y rollback si el nuevo firmware no arranca) ---
// Antes de reiniciar tras un OTA se marca "pendiente". El nuevo firmware, al
// arrancar, incrementa un contador; si llega a 3 reinicios sin confirmarse
// sano, se vuelve a la partición anterior con Update.rollBack().
void marcarOtaPendiente() {
  preferences.begin("ota", false);
  preferences.putBool("pend", true);
  preferences.putUChar("bcount", 0);
  preferences.end();
}

void gestionarArranquePostOta() {
  preferences.begin("ota", true);
  bool pend = preferences.getBool("pend", false);
  if (!pend) {
    preferences.end();
    otaNuevaPendiente = false;
    return;
  }
  uint8_t cont = preferences.getUChar("bcount", 0) + 1;
  preferences.end();

  preferences.begin("ota", false);
  preferences.putUChar("bcount", cont);
  preferences.end();

  otaNuevaPendiente = true;
  Serial.printf("Arranque post-OTA (%u).\n", cont);
  if (cont >= 3) {
    registrarEvento("Post-OTA: 3 arranques sin confirmar, volviendo a la version anterior");
    Serial.println("Post-OTA: aplicando rollback...");
    bool ok = Update.rollBack();
    preferences.begin("ota", false);
    preferences.remove("pend");
    preferences.end();
    if (ok) {
      delay(500);
      ESP.restart();
    }
  }
}

void limpiarOtaPendiente() {
  if (!otaNuevaPendiente) return;
  if (millis() > 90000 && WiFi.status() == WL_CONNECTED && estadoSensorWeb == "OK") {
    preferences.begin("ota", false);
    preferences.remove("pend");
    preferences.end();
    otaNuevaPendiente = false;
    registrarEvento("Post-OTA: firmware nuevo confirmado estable");
    Serial.println("Post-OTA: firmware confirmado estable");
  }
}

// SHA-256 del asset firmware.bin según la API de GitHub (campo "digest").
// Tolera espacios tras los ':' del JSON (GitHub los incluye).
String extraerSha256Asset(const String& json) {
  String assetKey = "\"name\":";
  int assetPos = -1;
  int desde = 0;
  while (true) {
    assetPos = json.indexOf(assetKey, desde);
    if (assetPos < 0) return "";
    int p = assetPos + assetKey.length();
    while (p < json.length() && isspace(json[p])) p++;
    if (json.substring(p, p + 14) == "\"firmware.bin\"") break;
    desde = assetPos + 1;
  }

  String digestKey = "\"digest\":";
  int inicio = json.indexOf(digestKey, assetPos);
  if (inicio < 0) return "";
  int q = inicio + digestKey.length();
  while (q < json.length() && isspace(json[q])) q++;
  if (json.substring(q, q + 8) != "\"sha256:") return "";
  q += 8;
  int fin = json.indexOf('"', q);
  if (fin < 0) return "";
  return json.substring(q, fin); // 64 caracteres hex
}

// --- DESCARGA E INSTALACIÓN OTA (bloqueante, con verificación SHA-256) ---
// Descarga el binario, calcula su SHA-256 mientras lo escribe en la partición
// OTA alternativa y solo la activa si el hash coincide con el publicado por
// GitHub. El watchdog de tarea se retira durante el proceso (en redes lentas
// superaría los 30 s y reiniciaría a mitad de escritura).
void descargarYActualizarFirmware(const String& urlFirmware, const String& sha256Esperado) {
  registrarEvento("OTA: descarga iniciada");
  Serial.printf("Actualizando a: %s\n", urlFirmware.c_str());

  if (sha256Esperado.length() != 64) {
    Serial.println("La release no expone el SHA-256 del asset; se aborta por seguridad.");
    estadoFirmwareWeb = "Error OTA";
    registrarEvento("OTA: release sin SHA-256");
    ledError.parpadear(200);
    return;
  }
  Serial.printf("SHA-256 esperado: %s\n", sha256Esperado.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  http.addHeader("User-Agent", "Estacion-Meteorologica-ESP32");

  if (!http.begin(client, urlFirmware)) {
    Serial.println("No se pudo iniciar la descarga");
    estadoFirmwareWeb = "Error OTA";
    registrarEvento("OTA: no se pudo iniciar la descarga");
    ledError.parpadear(200);
    return;
  }

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("Descarga rechazada (HTTP %d)\n", codigo);
    estadoFirmwareWeb = "Error OTA";
    registrarEvento("OTA: HTTP " + String(codigo) + " en la descarga");
    http.end();
    ledError.parpadear(200);
    return;
  }
  int tam = http.getSize();
  if (tam <= 0) {
    Serial.println("El servidor no reporto tamano");
    estadoFirmwareWeb = "Error OTA";
    registrarEvento("OTA: sin Content-Length");
    http.end();
    ledError.parpadear(200);
    return;
  }

  WiFiClient* tcp = http.getStreamPtr();
  esp_err_t wdtErr = esp_task_wdt_delete(NULL);

  bool exito = false;
  String motivo = "desconocido";

  if (Update.begin(tam, U_FLASH)) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);

    size_t escrito = 0;
    uint8_t buf[4096];
    while (escrito < (size_t)tam) {
      size_t n = tcp->read(buf, sizeof(buf));
      if (n == 0) { motivo = "descarga incompleta (corte de red)"; break; }
      if (Update.write(buf, n) != n) { motivo = "error escribiendo en flash"; break; }
      mbedtls_sha256_update_ret(&ctx, buf, n);
      escrito += n;
      mostrarBaileActualizacion(escrito, tam);
    }

    if (escrito == (size_t)tam) {
      unsigned char hash[32];
      mbedtls_sha256_finish_ret(&ctx, hash);
      char hex[65];
      for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", hash[i]);
      hex[64] = 0;
      Serial.printf("SHA-256 recibido: %s\n", hex);

      if (strcmp(hex, sha256Esperado.c_str()) != 0) {
        motivo = "SHA-256 no coincide";
        registrarEvento("OTA: hash no coincide, firmware descartado");
        Update.abort();
      } else if (Update.end()) {
        exito = true;
      } else {
        motivo = "activacion de la particion";
        Update.abort();
      }
    } else {
      Update.abort();
    }
    mbedtls_sha256_free(&ctx);
  } else {
    motivo = "sin espacio en particion OTA";
  }

  http.end();
  if (wdtErr == ESP_OK) {
    esp_task_wdt_add(NULL);
  }

  if (exito) {
    marcarOtaPendiente();
    Serial.println("OTA: firmware verificado e instalado. Reiniciando...");
    registrarEvento("OTA: instalado y verificado, reiniciando");
    ledWifi.apagar();
    ledSensor.apagar();
    ledError.apagar();
    delay(500);
    ESP.restart();
  } else {
    Serial.printf("Fallo OTA: %s\n", motivo.c_str());
    estadoFirmwareWeb = "Error OTA";
    registrarEvento("OTA: fallo - " + motivo);
    ledWifi.pulsar(3000, 10);
    ledSensor.apagar();
    ledError.parpadear(200);
  }
}

void comprobarActualizacionFirmware() {
  if (otaEnProgreso) return;
  if (WiFi.status() != WL_CONNECTED) return;

  otaEnProgreso = true;
  registrarEvento("OTA: buscando actualizacion");
  Serial.println("--- Buscando actualizacion de firmware ---");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  if (!http.begin(client, firmwareReleaseApi)) {
    Serial.println("No se pudo iniciar HTTP hacia la API de GitHub");
    estadoFirmwareWeb = "Error consulta";
    otaEnProgreso = false;
    return;
  }
  http.addHeader("User-Agent", "Estacion-Meteorologica-ESP32");

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.printf("No se pudo consultar firmware (%d)\n", codigo);
    estadoFirmwareWeb = "Error consulta";
    registrarEvento("OTA: error consultando GitHub (" + String(codigo) + ")");
    http.end();
    otaEnProgreso = false;
    return;
  }

  String respuesta = http.getString();
  http.end();

  String versionRemota = extraerJsonString(respuesta, "tag_name");
  if (versionRemota.isEmpty()) {
    Serial.println("Release sin tag de version");
    estadoFirmwareWeb = "Error consulta";
    otaEnProgreso = false;
    return;
  }

  Serial.printf("Firmware local: %s, remoto: %s\n", firmwareVersion, versionRemota.c_str());
  if (!versionNueva(versionRemota)) {
    Serial.println("Sin version nueva disponible");
    estadoFirmwareWeb = "Firmware comprobado";
    registrarEvento("OTA: comprobado, sin version nueva");
    otaEnProgreso = false;
    return;
  }

  estadoFirmwareWeb = "Actualizando...";
  String urlFirmware =
      "https://github.com/dgqgalaxy-create/Estacion_Meteorologica/releases/download/" +
      versionRemota + "/firmware.bin";
  String shaEsperado = extraerSha256Asset(respuesta);

  descargarYActualizarFirmware(urlFirmware, shaEsperado);
  otaEnProgreso = false;
}

void setup() {
  Serial.begin(115200);
  registrarEvento("Arranque - firmware " + String(firmwareVersion) + " (" + String(firmwareBuildDate) + ")");
  Wire.begin(21, 22); 
  if (!aht.begin()) Serial.println("Fallo AHT20");
  if (!bmp.begin(0x76) && !bmp.begin(0x77)) Serial.println("Fallo BMP280");
  
  for(int d=0; d<4; d++) {
    for(int i=0; i<24; i++) { 
        tempHistory[d][i] = NAN; 
        humHistory[d][i] = NAN; 
    }
  }

  preferences.begin("config", true);
  intervaloEnvio = preferences.getULong("intervalo", 10000);
  preferences.end();

  // URL de Google Sheets: la guardada en NVS o, si no existe, la de config.h
  preferences.begin("sheets", true);
  sheetsUrlActual = preferences.getString("url", "");
  preferences.end();
  if (sheetsUrlActual.length() == 0) {
    sheetsUrlActual = GOOGLE_SCRIPT_URL;
  }

  cargarAlertas();
  inicializarFS();

  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("Estacion-Clima-Config")) ESP.restart();
  WiFi.setAutoReconnect(true);

  gestionarArranquePostOta();

  if (WiFi.status() == WL_CONNECTED) {
    ledWifi.pulsar(3000, 10);
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
    registrarEvento("WiFi: conexion perdida");
  }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("WiFi conectado. IP: " + WiFi.localIP().toString());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    ledWifi.pulsar(3000, 10);
    estadoWifiWeb = "Conectado";
    registrarEvento("WiFi: conectado, IP " + WiFi.localIP().toString());
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

  // Reintentar NTP (1 vez por minuto) hasta conseguir una hora válida.
  static unsigned long ultimoIntentoNTP = 0;
  if (!tiempoSincronizado() && millis() - ultimoIntentoNTP >= 60000) {
    ultimoIntentoNTP = millis();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
  
  ledWifi.actualizar(); ledSensor.actualizar(); ledError.actualizar();

  procesarEstadoEnvio();

  limpiarOtaPendiente();

  // Drenar la cola offline (hasta 5 lecturas) cada 15 s cuando hay red
  static unsigned long ultimoDrenaje = 0;
  if (millis() - ultimoDrenaje >= 15000) {
    ultimoDrenaje = millis();
    drenarCola(5);
  }

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