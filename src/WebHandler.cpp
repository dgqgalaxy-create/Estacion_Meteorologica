#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_system.h>
#include <math.h>
#include "WebPage.h"
#include "config.h"

extern WebServer server;
extern float temperature;
extern float humidity;
extern float pressure;

extern float heatIndex;
extern float dewPoint;
extern float altitude;

extern float tempMax; extern float tempMin;
extern float humMax; extern float humMin;
extern float presMax; extern float presMin;

extern bool sendToSheetsEnabled;
extern unsigned long intervaloEnvio;
extern void guardarIntervalo(unsigned long);
extern String obtenerHora();

extern float tempHistory[4][24];
extern float humHistory[4][24];

extern String estadoWifiWeb;
extern String estadoSensorWeb;
extern String estadoSheetsWeb;
extern const char* firmwareVersion;
extern const char* firmwareBuildDate;

extern String tendenciaEstadoWeb;
extern float tendenciaActual;
extern bool alertasEnabled;
extern float alertaTempMax;
extern float alertaTempMin;
extern float alertaHumMax;
extern float alertaHumMin;
extern String alertaWeb;
extern void guardarAlertas();
extern int colaPendiente;
extern String obtenerFecha();
extern String obtenerLogEventos();
extern void guardarSheetsUrl(const String&);
extern String obtenerSheetsUrl();

extern long gmtOffset_sec;
extern void guardarZonaHoraria(long);

// Plantilla HTML reutilizable (cargada una sola vez)
String htmlTemplate;

// --- AUTENTICACION BASICA DEL PANEL ---
// Credenciales en include/config.h (WEB_USERNAME / WEB_PASSWORD).
bool requiereAuth() {
    if (server.authenticate(WEB_USERNAME, WEB_PASSWORD)) return true;
    server.requestAuthentication(BASIC_AUTH, "estacion-clima", "Acceso denegado");
    return false;
}

void handleRoot() {
    if (!requiereAuth()) return;

    if (htmlTemplate.length() == 0) {
        htmlTemplate = index_html;
        htmlTemplate.reserve(htmlTemplate.length() + 200);
    }

    String html = htmlTemplate;
    html.reserve(html.length() + 150);
    
    html.replace("%TEMPERATURA%", String(temperature, 1));
    html.replace("%HUMEDAD%", String(humidity, 1));
    html.replace("%PRESION%", String(pressure, 1));
    html.replace("%SENSACION%", String(heatIndex, 1));
    html.replace("%ROCIO%", String(dewPoint, 1));
    html.replace("%ALTURA%", String(altitude, 0));

    html.replace("%TEMP_MAX%", tempMax == -100.0 ? "--" : String(tempMax, 1));
    html.replace("%TEMP_MIN%", tempMin == 100.0 ? "--" : String(tempMin, 1));
    
    html.replace("%HUM_MAX%", humMax == 0.0 ? "--" : String(humMax, 1));
    html.replace("%HUM_MIN%", humMin == 100.0 ? "--" : String(humMin, 1));
    
    html.replace("%PRES_MAX%", presMax == 0.0 ? "--" : String(presMax, 1));
    html.replace("%PRES_MIN%", presMin == 2000.0 ? "--" : String(presMin, 1));

    String status = sendToSheetsEnabled ? "ACTIVADO" : "PAUSADO";
    html.replace("%ESTADO%", status);
    html.replace("%INTERVALO_SEC%", String(intervaloEnvio / 1000));
    html.replace("%GMT_OFFSET%", String(gmtOffset_sec / 3600.0, 1));
    html.replace("%TOGGLE_TEXT%", sendToSheetsEnabled ? "<i class='bx bx-pause-circle'></i> Pausar Envío" : "<i class='bx bx-play-circle'></i> Reanudar Envío");
    html.replace("%TOGGLE_CLASS%", sendToSheetsEnabled ? "btn-danger" : "btn-success");
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%RSSI%", String(WiFi.RSSI()));
    html.replace("%TIEMPO%", obtenerHora());
    html.replace("%FIRMWARE_VERSION%", firmwareVersion);
    html.replace("%FIRMWARE_DATE%", firmwareBuildDate);

    html.replace("%ESTADO_WIFI%", estadoWifiWeb);
    html.replace("%ESTADO_SENSOR%", estadoSensorWeb);
    html.replace("%ESTADO_SHEETS%", estadoSheetsWeb);

    html.replace("%TENDENCIA%", tendenciaEstadoWeb);
    html.replace("%ALERTA_CLASE%", alertaWeb == "Sin alertas" ? "dot-ok" : "dot-error");
    html.replace("%ALERTA%", alertaWeb);
    html.replace("%ALERTA_CHECK%", alertasEnabled ? "checked" : "");
    html.replace("%ALERTA_TMAX%", String(alertaTempMax, 1));
    html.replace("%ALERTA_TMIN%", String(alertaTempMin, 1));
    html.replace("%ALERTA_HMAX%", String(alertaHumMax, 0));
    html.replace("%ALERTA_HMIN%", String(alertaHumMin, 0));
    html.replace("%SHEETS_URL%", obtenerSheetsUrl());
    
    server.send(200, "text/html", html);
}

void handleToggle() {
    if (!requiereAuth()) return;
    sendToSheetsEnabled = !sendToSheetsEnabled;
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetInterval() {
    if (!requiereAuth()) return;
    if (server.hasArg("segundos")) {
        long secs = server.arg("segundos").toInt();
        // Rango flexible: de 5 s a 24 h (86400 s). Unidad = segundos.
        if (secs >= 5 && secs <= 86400) guardarIntervalo((unsigned long)secs * 1000UL);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetTimezone() {
    if (!requiereAuth()) return;
    if (server.hasArg("horas")) {
        float horas = server.arg("horas").toFloat();
        // Diferencia con UTC en horas; se guarda en segundos en NVS.
        if (horas >= -12.0f && horas <= 14.0f) {
            guardarZonaHoraria(lroundf(horas * 3600.0f));
        }
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetSheetsUrl() {
    if (!requiereAuth()) return;
    if (server.hasArg("url")) {
        guardarSheetsUrl(server.arg("url"));
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetAlerts() {
    if (!requiereAuth()) return;
    if (server.hasArg("on")) {
        alertasEnabled = (server.arg("on") == "1" || server.arg("on") == "true" || server.arg("on") == "on" || server.arg("on") == "checked");
    } else {
        alertasEnabled = false;
    }
    if (server.hasArg("tmax")) alertaTempMax = server.arg("tmax").toFloat();
    if (server.hasArg("tmin")) alertaTempMin = server.arg("tmin").toFloat();
    if (server.hasArg("hmax")) alertaHumMax = server.arg("hmax").toFloat();
    if (server.hasArg("hmin")) alertaHumMin = server.arg("hmin").toFloat();
    alertaTempMax = constrain(alertaTempMax, -20.0f, 60.0f);
    alertaTempMin = constrain(alertaTempMin, -20.0f, 60.0f);
    alertaHumMax = constrain(alertaHumMax, 0.0f, 100.0f);
    alertaHumMin = constrain(alertaHumMin, 0.0f, 100.0f);
    guardarAlertas();
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleDataJson() {
    if (!requiereAuth()) return;

  String json;
  json.reserve(4000); // Reserva suficiente para evitar fragmentación
  
  json = "{";
  for(int d=0; d<4; d++) {
    json += "\"t" + String(d) + "\":[";
    for (int i = 0; i < 24; i++) {
      if (isnan(tempHistory[d][i])) json += "null";
      else json += String(tempHistory[d][i], 1);
      if (i < 23) json += ",";
    }
    json += "],";
    
    json += "\"h" + String(d) + "\":[";
    for (int i = 0; i < 24; i++) {
      if (isnan(humHistory[d][i])) json += "null";
      else json += String(humHistory[d][i], 1);
      if (i < 23) json += ",";
    }
    json += "]";
    if (d < 3) json += ",";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleCurrentJson() {
    if (!requiereAuth()) return;

  String json;
  json.reserve(600);
  json = "{";
  json += "\"temp\":" + String(temperature, 1) + ",";
  json += "\"hum\":" + String(humidity, 1) + ",";
  json += "\"pres\":" + String(pressure, 1) + ",";
  json += "\"hi\":" + String(heatIndex, 1) + ",";
  json += "\"dp\":" + String(dewPoint, 1) + ",";
  json += "\"alt\":" + String(altitude, 0) + ",";
  json += "\"tmax\":" + String(tempMax, 1) + ",";
  json += "\"tmin\":" + String(tempMin, 1) + ",";
  json += "\"hmax\":" + String(humMax, 1) + ",";
  json += "\"hmin\":" + String(humMin, 1) + ",";
  json += "\"pmax\":" + String(presMax, 1) + ",";
  json += "\"pmin\":" + String(presMin, 1) + ",";
  json += "\"wifi\":\"" + estadoWifiWeb + "\",";
  json += "\"sensor\":\"" + estadoSensorWeb + "\",";
  json += "\"sheets\":\"" + estadoSheetsWeb + "\",";
  json += "\"hora\":\"" + obtenerHora() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"intervalo\":" + String(intervaloEnvio / 1000) + ",";
  json += "\"estado\":\"" + String(sendToSheetsEnabled ? "ACTIVADO" : "PAUSADO") + "\"";
  json += ",\"version\":\"" + String(firmwareVersion) + "\"";
  json += ",\"actualizado\":\"" + String(firmwareBuildDate) + "\"";
  json += ",\"tend\":\"" + tendenciaEstadoWeb + "\"";
  json += ",\"trate\":" + String(isnan(tendenciaActual) ? "null" : String(tendenciaActual, 2));
  json += ",\"alerta\":\"" + alertaWeb + "\"";
  json += ",\"pendientes\":" + String(colaPendiente);
  json += ",\"gmt\":" + String(gmtOffset_sec / 3600.0, 1);
  json += "}";
  server.send(200, "application/json", json);
}

void handleRetry() {
    if (!requiereAuth()) return;
    extern void reintentarEnvioAhora();
    reintentarEnvioAhora();
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleResetWifi() {
    if (!requiereAuth()) return;
    WiFiManager wm;
    server.send(200, "text/html", "<h1>Borrando WiFi...</h1>");
    delay(1000);
    wm.resetSettings();
    ESP.restart();
}

String razonResetTexto() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "Encendido";
        case ESP_RST_EXT: return "Pin de reset";
        case ESP_RST_SW: return "Reinicio por software";
        case ESP_RST_PANIC: return "Panico/Excepcion";
        case ESP_RST_INT_WDT: return "Watchdog de interrupcion";
        case ESP_RST_TASK_WDT: return "Watchdog de tarea";
        case ESP_RST_WDT: return "Otro watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT: return "Brownout (voltaje)";
        case ESP_RST_SDIO: return "SDIO";
        default: return "Desconocido";
    }
}

void handleDiagJson() {
    if (!requiereAuth()) return;

    String log = obtenerLogEventos();
    log.replace("\n", " | ");

    String json;
    json.reserve(900);
    json = "{";
    json += "\"uptime\":" + String(millis() / 1000) + ",";
    json += "\"fecha\":\"" + obtenerFecha() + "\",";
    json += "\"hora\":\"" + obtenerHora() + "\",";
    json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"heapMin\":" + String(ESP.getMinFreeHeap()) + ",";
    json += "\"heapMax\":" + String(ESP.getMaxAllocHeap()) + ",";
    json += "\"reset\":\"" + razonResetTexto() + "\",";
    json += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
    json += "\"canal\":" + String(WiFi.channel()) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"mac\":\"" + String(WiFi.macAddress()) + "\",";
    json += "\"version\":\"" + String(firmwareVersion) + "\",";
    json += "\"build\":\"" + String(firmwareBuildDate) + "\",";
    json += "\"intervalo\":" + String(intervaloEnvio / 1000) + ",";
    json += "\"enviar\":" + String(sendToSheetsEnabled ? 1 : 0) + ",";
    json += "\"pendientes\":" + String(colaPendiente) + ",";
    json += "\"sketch\":" + String(ESP.getSketchSize()) + ",";
    json += "\"sketchLibre\":" + String(ESP.getFreeSketchSpace()) + ",";
    json += "\"flash\":" + String(ESP.getFlashChipSize()) + ",";
    json += "\"sdk\":\"" + String(ESP.getSdkVersion()) + "\",";
    json += "\"cpu\":" + String(ESP.getCpuFreqMHz()) + ",";
    json += "\"gmt\":" + String(gmtOffset_sec / 3600.0, 1) + ",";
    json += "\"log\":\"" + log + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleNotFound() { server.send(404, "text/plain", "404: No encontrado"); }

void setupWeb() {
    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.on("/setinterval", handleSetInterval);
    server.on("/settimezone", handleSetTimezone);
    server.on("/setsheetsurl", handleSetSheetsUrl);
    server.on("/setalerts", handleSetAlerts);
    server.on("/data.json", handleDataJson);
    server.on("/api/current", handleCurrentJson);
    server.on("/api/diag", handleDiagJson);
    server.on("/retry", handleRetry);
    server.on("/resetwifi", handleResetWifi);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Servidor Web Modular Iniciado");
}
