#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <math.h>
#include "WebPage.h" 

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

extern float tempHistory[4][25];
extern float humHistory[4][25];

extern String estadoWifiWeb;
extern String estadoSensorWeb;
extern String estadoSheetsWeb;

// Plantilla HTML reutilizable (cargada una sola vez)
String htmlTemplate;

void handleRoot() {
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
    html.replace("%INTERVALO%", String(intervaloEnvio / 1000));
    html.replace("%INTERVALO_SEC%", String(intervaloEnvio / 1000));
    html.replace("%TOGGLE_TEXT%", sendToSheetsEnabled ? "<i class='bx bx-pause-circle'></i> Pausar Envío" : "<i class='bx bx-play-circle'></i> Reanudar Envío");
    html.replace("%TOGGLE_CLASS%", sendToSheetsEnabled ? "btn-danger" : "btn-success");
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%RSSI%", String(WiFi.RSSI()));
    html.replace("%TIEMPO%", obtenerHora());

    html.replace("%ESTADO_WIFI%", estadoWifiWeb);
    html.replace("%ESTADO_SENSOR%", estadoSensorWeb);
    html.replace("%ESTADO_SHEETS%", estadoSheetsWeb);
    
    server.send(200, "text/html", html);
}

void handleToggle() {
    sendToSheetsEnabled = !sendToSheetsEnabled;
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetInterval() {
    if (server.hasArg("segundos")) {
        unsigned long secs = server.arg("segundos").toInt();
        if (secs >= 5 && secs <= 3600) guardarIntervalo(secs * 1000);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleDataJson() {
  String json;
  json.reserve(4000); // Reserva suficiente para evitar fragmentación
  
  json = "{";
  for(int d=0; d<4; d++) {
    json += "\"t" + String(d) + "\":[";
    for (int i = 0; i < 25; i++) {
      if (isnan(tempHistory[d][i])) json += "null";
      else json += String(tempHistory[d][i], 1);
      if (i < 24) json += ",";
    }
    json += "],";
    
    json += "\"h" + String(d) + "\":[";
    for (int i = 0; i < 25; i++) {
      if (isnan(humHistory[d][i])) json += "null";
      else json += String(humHistory[d][i], 1);
      if (i < 24) json += ",";
    }
    json += "]";
    if (d < 3) json += ",";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleCurrentJson() {
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
  json += "}";
  server.send(200, "application/json", json);
}

void handleRetry() {
    extern void reintentarEnvioAhora();
    reintentarEnvioAhora();
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleResetWifi() {
    WiFiManager wm;
    server.send(200, "text/html", "<h1>Borrando WiFi...</h1>");
    delay(1000);
    wm.resetSettings();
    ESP.restart();
}

void handleNotFound() { server.send(404, "text/plain", "404: No encontrado"); }

void setupWeb() {
    server.on("/", handleRoot);
    server.on("/toggle", handleToggle);
    server.on("/setinterval", handleSetInterval);
    server.on("/data.json", handleDataJson);
    server.on("/api/current", handleCurrentJson);
    server.on("/retry", handleRetry);
    server.on("/resetwifi", handleResetWifi);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Servidor Web Modular Iniciado");
}