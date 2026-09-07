# ============================================================================
#  boot.py - Estacion Meteorologica (MicroPython)
#  Conecta el WiFi al arrancar y activa WebREPL (sustituto local del OTA de
#  ArduinoOTA) para actualizar main.py / index.html por WiFi.
#
#  La contrasena de WebREPL se toma de config.json ("webrepl_pass");
#  si no existe, se usa "04330".
# ============================================================================

import json
import network
import time

WIFI_FILE = "wifi.json"
WEBREPL_PASS_DEFAULT = "04330"


def conectar():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    try:
        with open(WIFI_FILE) as f:
            creds = json.load(f)
        wlan.connect(creds.get("ssid", ""), creds.get("pass", ""))
        for _ in range(40):
            if wlan.isconnected():
                break
            time.sleep_ms(500)
    except Exception:
        pass
    return wlan.isconnected()


def iniciar_webrepl():
    try:
        with open("config.json") as f:
            cfg = json.load(f)
        pw = cfg.get("webrepl_pass", WEBREPL_PASS_DEFAULT)
    except Exception:
        pw = WEBREPL_PASS_DEFAULT
    try:
        import webrepl
        webrepl.start(password=pw)
        print("WebREPL activo (puerto 8266) con contrasena configurada.")
    except Exception:
        print("WebREPL no disponible en este firmware.")


if conectar():
    iniciar_webrepl()
else:
    print("Sin WiFi configurado todavia; usa el portal Estacion-Clima-Config.")
