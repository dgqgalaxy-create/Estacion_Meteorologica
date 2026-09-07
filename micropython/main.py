# ============================================================================
#  Estacion Meteorologica - MicroPython (ESP32)
#  Variante en Python del firmware C++ (misma pagina web y funcionalidades).
#
#  Requisitos: MicroPython >= 1.20 en un ESP32 (4 MB de flash).
#  Archivos en el dispositivo:
#    boot.py        conexion WiFi inicial + WebREPL (actualizacion por WiFi)
#    main.py        aplicacion completa (este archivo)
#    index.html     pagina web (plantilla con marcadores %NOMBRE%)
#    lib/ahtx0.py   driver del AHT20
#    lib/bmp280.py  driver del BMP280
#    config.json    ajustes (se crea solo)
#    wifi.json      credenciales WiFi (se crea solo)
#    cola.csv       lecturas pendientes para Google Sheets (se crea solo)
#
#  Diferencias con la version C++:
#   - La actualizacion por WiFi usa WebREPL (ArduinoOTA no existe en
#     MicroPython).
#   - No hay mDNS: accede al panel con la IP del ESP32 (http://IP/).
#   - Los ajustes se guardan en config.json, no en NVS.
#   - Usuario y contrasena del panel se editan en config.json.
# ============================================================================

import gc
import json
import math
import os
import socket
import ssl
import time
import binascii

import _thread
import network
import ntptime
from machine import I2C, Pin, WDT, reset, reset_cause
from machine import PWRON_RESET, HARD_RESET, WDT_RESET, SOFT_RESET
from machine import DEEPSLEEP_RESET, BROWNOUT_RESET

try:
    from ahtx0 import AHT20
except Exception:
    AHT20 = None
try:
    from bmp280 import BMP280
except Exception:
    BMP280 = None

# --- Identificacion ---------------------------------------------------------
VERSION = "1.0.0-mp"
BUILD = "MicroPython"

# --- Archivos y ajustes por defecto -----------------------------------------
CONFIG_FILE = "config.json"
WIFI_FILE = "wifi.json"
COLA_FILE = "cola.csv"
COLA_MAX = 200

DEFAULTS = {
    "intervalo": 10,          # segundos entre lecturas/envios (5..86400)
    "gmt": -6.0,              # diferencia con UTC en horas
    "sheets_url": "https://script.google.com/macros/s/TU_ID/exec",
    "enviar": True,           # envio a Google Sheets activado
    "web_user": "admin",      # usuario del panel web
    "web_pass": "04330",      # contrasena del panel web
    "webrepl_pass": "04330",  # contrasena de WebREPL (la usa boot.py)
    "alertas": {"on": False, "tmax": 35.0, "tmin": 0.0, "hmax": 85.0, "hmin": 15.0},
}

# --- Pines ------------------------------------------------------------------
PIN_LED_WIFI = 12
PIN_LED_SENSOR = 14
PIN_LED_ERROR = 27
PIN_SDA = 21
PIN_SCL = 22

# --- Estado global ----------------------------------------------------------
cfg = dict(DEFAULTS)

TEMP = 0.0
HUM = 0.0
PRES = 0.0
heat_index = 0.0
dew_point = 0.0
altitude = 0.0

temp_max = -100.0
temp_min = 100.0
hum_max = 0.0
hum_min = 100.0
pres_max = 0.0
pres_min = 2000.0

hist_t = [[None] * 24 for _ in range(4)]   # 4 dias x 24 horas
hist_h = [[None] * 24 for _ in range(4)]
current_day = -1

estado_wifi = "Desconectado"
estado_sensor = "OK"
estado_sheets = "OK"

eventos = []                 # log de eventos en RAM (ultimos 20)
EVENTOS_MAX = 20

pres_ring = []               # [(epoch, hPa), ...] max 512 muestras
PRES_RING_MAX = 512
tendencia = None             # hPa/hora o None
tendencia_estado = "--"

alerta_web = "Sin alertas"

# --- Envio a Google Sheets (misma maquina de estados que la version C++) ----
ENVIO_INACTIVO = 0
ENVIO_INTENTANDO = 1
ENVIO_ESPERA = 2
estado_envio = ENVIO_INACTIVO
ultimo_intento = 0
intentos = 0
MAX_INTENTOS = 6
TIEMPO_ENTRE_INTENTOS_MS = 10000

envio_t = 0.0
envio_h = 0.0
envio_p = 0.0
last_valid = (0.0, 0.0, 0.0)
lectura_valida = False
cola_pendiente = 0
heap_min = 0

wlan = network.WLAN(network.STA_IF)

# Plantilla de la pagina web (se carga una sola vez al arrancar)
try:
    with open("index.html") as _f:
        TEMPLATE = _f.read()
except Exception:
    TEMPLATE = None


# ============================================================================
#  LEDs (control no bloqueante, igual que StatusLed.h)
# ============================================================================
class StatusLed:
    def __init__(self, pin):
        self.pin = Pin(pin, Pin.OUT, value=0)
        self.state = 0
        self.mode = None          # None, "blink", "pulse"
        self.interval = 0
        self.dur = 0
        self.last = time.ticks_ms()

    def _set(self, v):
        self.state = v
        self.pin.value(v)

    def encender(self):
        self.mode = None
        self._set(1)

    def apagar(self):
        self.mode = None
        self._set(0)

    def parpadear(self, interval):
        if self.mode != "blink" or self.interval != interval:
            self.mode = "blink"
            self.interval = interval
            self.last = time.ticks_ms()

    def pulsar(self, pausa, dur):
        if self.mode != "pulse" or self.interval != pausa or self.dur != dur:
            self.mode = "pulse"
            self.interval = pausa
            self.dur = dur
            self.last = time.ticks_ms()
            self._set(1)

    def actualizar(self):
        now = time.ticks_ms()
        if self.mode == "blink":
            if time.ticks_diff(now, self.last) >= self.interval:
                self.last = now
                self._set(1 - self.state)
        elif self.mode == "pulse":
            espera = self.dur if self.state else self.interval
            if time.ticks_diff(now, self.last) >= espera:
                self.last = now
                self._set(1 - self.state)


led_wifi = StatusLed(PIN_LED_WIFI)
led_sensor = StatusLed(PIN_LED_SENSOR)
led_error = StatusLed(PIN_LED_ERROR)


# ============================================================================
#  Configuracion persistente (config.json)
# ============================================================================
def cargar_config():
    global cfg
    cfg = dict(DEFAULTS)
    try:
        with open(CONFIG_FILE) as f:
            d = json.load(f)
        cfg.update(d)
        cfg.setdefault("alertas", dict(DEFAULTS["alertas"]))
    except Exception:
        cfg = dict(DEFAULTS)
    return cfg


def guardar_config():
    try:
        with open(CONFIG_FILE, "w") as f:
            json.dump(cfg, f)
    except Exception:
        registrar_evento("Config: error al guardar")


# ============================================================================
#  Tiempo y zona horaria
# ============================================================================
def gmt_seg():
    return int(round(cfg.get("gmt", -6.0) * 3600.0))


def tiempo_ok():
    # La hora solo es valida tras sincronizar NTP (ano >= 2024).
    return time.time() > 1704067200


def hora_local():
    if not tiempo_ok():
        return "--:--:--"
    t = time.localtime(time.time() + gmt_seg())
    return "%02d:%02d:%02d" % (t[3], t[4], t[5])


def fecha_local():
    if not tiempo_ok():
        return "--/--/----"
    t = time.localtime(time.time() + gmt_seg())
    return "%02d/%02d/%04d" % (t[2], t[1], t[0])


def sincronizar_ntp():
    try:
        ntptime.settime()
        return True
    except Exception:
        return False


def registrar_evento(e):
    h = hora_local()
    linea = e if h == "--:--:--" else h + "  " + e
    eventos.append(linea)
    while len(eventos) > EVENTOS_MAX:
        eventos.pop(0)


# ============================================================================
#  WiFi y portal de configuracion (sustituye a WiFiManager)
# ============================================================================
def cargar_wifi():
    try:
        with open(WIFI_FILE) as f:
            return json.load(f)
    except Exception:
        return None


def conectar_wifi(timeout_s=15):
    creds = cargar_wifi()
    wlan.active(True)
    if creds:
        try:
            wlan.connect(creds.get("ssid", ""), creds.get("pass", ""))
        except Exception:
            pass
        t0 = time.ticks_ms()
        while not wlan.isconnected() and time.ticks_diff(time.ticks_ms(), t0) < timeout_s * 1000:
            time.sleep_ms(250)
    return wlan.isconnected()


def ip_actual():
    try:
        return wlan.ifconfig()[0]
    except Exception:
        return "0.0.0.0"


def rssi_actual():
    try:
        return wlan.status("rssi")
    except Exception:
        return 0


def ssid_actual():
    try:
        return wlan.config("essid").decode()
    except Exception:
        return ""


def canal_actual():
    try:
        return wlan.config("channel")
    except Exception:
        return 0


def mac_actual():
    try:
        return ":".join("%02x" % b for b in wlan.config("mac"))
    except Exception:
        return ""


FORM_WIFI = """<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Configurar WiFi</title>
<style>body{font-family:sans-serif;background:#f0f4f8;display:flex;justify-content:center;padding:30px}
.card{background:#fff;border-radius:16px;padding:24px;max-width:360px;width:100%;box-shadow:0 8px 32px rgba(31,38,135,.15)}
h2{margin-top:0;color:#2d3748}input{width:100%;box-sizing:border-box;margin:6px 0 14px;padding:9px;border:1px solid #cbd5e0;border-radius:8px}
button{width:100%;padding:10px;border:0;border-radius:30px;background:#4299e1;color:#fff;font-weight:bold;cursor:pointer}</style>
</head><body><div class="card"><h2>Estacion Clima - WiFi</h2>
<p>Conectate a tu red WiFi:</p>
<form method="GET" action="/save">
<input type="text" name="ssid" placeholder="Nombre de la red (SSID)" required>
<input type="password" name="pass" placeholder="Contrasena" required>
<button type="submit">Guardar y conectar</button>
</form></div></body></html>
"""


def portal_wifi():
    ap = network.WLAN(network.AP_IF)
    ap.active(True)
    ap.config(essid="Estacion-Clima-Config")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 80))
    s.listen(2)
    s.settimeout(0.5)
    print("Portal WiFi: conectate a Estacion-Clima-Config y abre http://192.168.4.1/")
    while True:
        led_wifi.parpadear(100)
        led_sensor.parpadear(100)
        led_error.parpadear(100)
        led_wifi.actualizar()
        led_sensor.actualizar()
        led_error.actualizar()
        try:
            conn, addr = s.accept()
        except OSError:
            continue
        try:
            req = conn.recv(2048)
            linea = req.split(b"\r\n")[0].decode("utf-8", "replace")
            partes = linea.split(" ")
            destino = partes[1] if len(partes) > 1 else "/"
            path, _, query = destino.partition("?")
            if path == "/save":
                args = parse_query(query)
                ssid = args.get("ssid", "").strip()
                clave = args.get("pass", "").strip()
                if ssid:
                    try:
                        with open(WIFI_FILE, "w") as f:
                            json.dump({"ssid": ssid, "pass": clave}, f)
                    except Exception:
                        pass
                    send_all(conn, b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                                   b"Connection: close\r\n\r\nGuardado. Reiniciando y conectando...")
                    time.sleep_ms(500)
                    reset()
                    return
                responder(conn, 400, "text/html", "<a href='/'>Falta el SSID</a>")
            else:
                responder(conn, 200, "text/html", FORM_WIFI)
        except Exception:
            pass
        finally:
            try:
                conn.close()
            except Exception:
                pass


# ============================================================================
#  Calculos meteorologicos
# ============================================================================
def calc_dew_point(t, h):
    a = 17.27
    b = 237.7
    alpha = ((a * t) / (b + t)) + math.log(h / 100.0)
    return (b * alpha) / (a - alpha)


def calc_heat_index(t, h):
    tf = t * 1.8 + 32
    hi = 0.5 * (tf + 61.0 + ((tf - 68.0) * 1.2) + (h * 0.094))
    if hi > 80:
        hi = (-42.379 + 2.04901523 * tf + 10.14333127 * h
              - 0.22475541 * tf * h - 0.00683783 * tf * tf
              - 0.05481717 * h * h + 0.00122874 * tf * tf * h
              + 0.00085282 * tf * h * h - 0.00000199 * tf * tf * h * h)
    return (hi - 32) * 0.55555


def calc_altitude(p_hpa, ref=1013.25):
    return 44330.0 * (1.0 - (p_hpa / ref) ** (1 / 5.255))


# ============================================================================
#  Historial, records y cambio de dia
# ============================================================================
def verificar_dia():
    global current_day, temp_max, temp_min, hum_max, hum_min, pres_max, pres_min
    if not tiempo_ok():
        return
    t = time.localtime(time.time() + gmt_seg())
    if current_day != t[2]:
        current_day = t[2]
        for d in range(3, 0, -1):
            hist_t[d] = hist_t[d - 1][:]
            hist_h[d] = hist_h[d - 1][:]
        hist_t[0] = [None] * 24
        hist_h[0] = [None] * 24
        temp_max = -100.0
        temp_min = 100.0
        hum_max = 0.0
        hum_min = 100.0
        pres_max = 0.0
        pres_min = 2000.0
        registrar_evento("--- NUEVO DIA: records y graficas reiniciados ---")


def actualizar_historial(t, h):
    if tiempo_ok():
        lt = time.localtime(time.time() + gmt_seg())
        hora = lt[3]
        if 0 <= hora <= 23:
            hist_t[0][hora] = t
            hist_h[0][hora] = h


def actualizar_records(t, h, p):
    global temp_max, temp_min, hum_max, hum_min, pres_max, pres_min
    if t > temp_max:
        temp_max = t
    if t < temp_min:
        temp_min = t
    if h > hum_max:
        hum_max = h
    if h < hum_min:
        hum_min = h
    if p > pres_max:
        pres_max = p
    if p < pres_min:
        pres_min = p


# --- Tendencia de presion (hPa/hora, ventana minima de 20 minutos) ---------
def registrar_presion(p):
    global tendencia, tendencia_estado
    if not tiempo_ok():
        return
    ahora = time.time()
    pres_ring.append((ahora, p))
    if len(pres_ring) > PRES_RING_MAX:
        pres_ring.pop(0)
    tendencia = None
    if len(pres_ring) >= 2:
        t_ult, p_ult = pres_ring[-1]
        for (t_i, p_i) in reversed(pres_ring):
            if t_ult - t_i >= 1200:
                dt = t_ult - t_i
                if dt > 0:
                    tendencia = (p_ult - p_i) / (dt / 3600.0)
                break
    if tendencia is None:
        tendencia_estado = "--"
    elif tendencia >= 0.30:
        tendencia_estado = "Subiendo"
    elif tendencia <= -0.30:
        tendencia_estado = "Bajando"
    else:
        tendencia_estado = "Estable"


# ============================================================================
#  Alertas configurables
# ============================================================================
def evaluar_alertas():
    global alerta_web
    nueva = "Sin alertas"
    a = cfg.get("alertas", {})
    if a.get("on"):
        if TEMP > a.get("tmax", 35.0):
            nueva = "Temperatura alta (%.1f C)" % TEMP
        elif TEMP < a.get("tmin", 0.0):
            nueva = "Temperatura baja (%.1f C)" % TEMP
        elif HUM > a.get("hmax", 85.0):
            nueva = "Humedad alta (%.0f %%)" % HUM
        elif HUM < a.get("hmin", 15.0):
            nueva = "Humedad baja (%.0f %%)" % HUM
    if nueva != alerta_web:
        if nueva != "Sin alertas":
            registrar_evento("Alerta: " + nueva)
        else:
            registrar_evento("Alerta: condiciones normales")
        alerta_web = nueva


# ============================================================================
#  Cola offline (cola.csv)
# ============================================================================
def contar_cola():
    n = 0
    try:
        with open(COLA_FILE) as f:
            for linea in f:
                if linea.strip():
                    n += 1
    except Exception:
        pass
    return n


def encolar(t, h, p):
    global cola_pendiente
    if cola_pendiente >= COLA_MAX:
        registrar_evento("Cola: llena, se descarta lectura")
        return
    try:
        with open(COLA_FILE, "a") as f:
            f.write("%.1f,%.1f,%.1f\n" % (t, h, p))
        cola_pendiente += 1
    except Exception:
        pass


def drenar_cola(max_por_vez):
    global cola_pendiente
    if cola_pendiente <= 0:
        return
    if not wlan.isconnected() or not cfg.get("enviar", True):
        return
    if estado_envio != ENVIO_INACTIVO:
        return
    try:
        with open(COLA_FILE) as f:
            lineas = [l.strip() for l in f if l.strip()]
    except Exception:
        return
    enviadas = 0
    ultimo = 0
    for linea in lineas:
        if enviadas >= max_por_vez:
            break
        try:
            t, h, p = [float(x) for x in linea.split(",")]
        except Exception:
            continue
        ultimo = enviar_lectura(t, h, p)
        if 200 <= ultimo < 400:
            enviadas += 1
            cola_pendiente -= 1
        else:
            break
    if enviadas:
        try:
            with open(COLA_FILE, "w") as f:
                for linea in lineas[enviadas:]:
                    f.write(linea + "\n")
        except Exception:
            pass
        registrar_evento("Cola: %d lecturas reenviadas (%d pendientes)" % (enviadas, cola_pendiente))
    elif ultimo:
        registrar_evento("Cola: envio bloqueado (HTTP %d)" % ultimo)


# ============================================================================
#  Envio HTTP a Google Sheets (TLS con sockets, sin librerias externas)
#  Igual que la version C++: exito = respuesta 2xx/3xx (Apps Script devuelve
#  302 y no hay que seguir la redireccion).
# ============================================================================
def enviar_lectura(t, h, p):
    if not wlan.isconnected():
        return -1
    url = cfg.get("sheets_url", "")
    if len(url) < 15:
        return -1
    try:
        resto = url.split("//", 1)[1]
        host = resto.split("/", 1)[0]
        path = "/" + (resto.split("/", 1)[1] if "/" in resto else "")
    except Exception:
        return -1
    cuerpo = "temp=%.1f&hum=%.1f&pres=%.1f" % (t, h, p)
    s = None
    try:
        addr = socket.getaddrinfo(host, 443)[0][-1]
        s = socket.socket()
        s.settimeout(8)
        s.connect(addr)
        s = ssl.wrap_socket(s, server_hostname=host)
        peticion = ("POST %s HTTP/1.1\r\n"
                    "Host: %s\r\n"
                    "Content-Type: application/x-www-form-urlencoded\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n"
                    "\r\n%s" % (path, host, len(cuerpo), cuerpo)).encode()
        s.write(peticion)
        resp = b""
        while b"\r\n\r\n" not in resp:
            trozo = s.read(512)
            if not trozo:
                break
            resp += trozo
        linea = resp.split(b"\r\n")[0].decode("utf-8", "replace")
        return int(linea.split(" ")[1])
    except Exception:
        return -2
    finally:
        if s:
            try:
                s.close()
            except Exception:
                pass


# ============================================================================
#  Maquina de estados del envio (no bloqueante para LEDs y pagina web)
# ============================================================================
def intentar_envio():
    global estado_envio, estado_sheets, intentos, ultimo_intento
    if not wlan.isconnected() or not cfg.get("enviar", True):
        return
    estado_sheets = "Enviando..."
    led_sensor.encender()
    codigo = enviar_lectura(envio_t, envio_h, envio_p)
    led_sensor.apagar()
    if 200 <= codigo < 400:
        led_error.apagar()
        estado_sheets = "OK"
        estado_envio = ENVIO_INACTIVO
        intentos = 0
    else:
        intentos += 1
        if intentos >= MAX_INTENTOS:
            led_error.parpadear(200)
            estado_sheets = "Error envio"
            estado_envio = ENVIO_INACTIVO
            intentos = 0
            encolar(envio_t, envio_h, envio_p)
        else:
            estado_sheets = "Reintentando..."
            estado_envio = ENVIO_ESPERA
            ultimo_intento = time.ticks_ms()


def iniciar_envio(t, h, p):
    global estado_envio, envio_t, envio_h, envio_p, intentos
    if estado_envio == ENVIO_INACTIVO and cfg.get("enviar", True):
        envio_t, envio_h, envio_p = t, h, p
        intentos = 0
        estado_envio = ENVIO_INTENTANDO
        intentar_envio()


def reintentar_ahora():
    global estado_envio, envio_t, envio_h, envio_p, intentos, estado_sheets
    if estado_envio == ENVIO_INACTIVO:
        envio_t, envio_h, envio_p = last_valid
        intentos = 0
        estado_envio = ENVIO_INTENTANDO
        intentar_envio()
        led_error.apagar()
        estado_sheets = "Reintentando..."


def procesar_estado_envio():
    global estado_envio, intentos, estado_sheets
    if not cfg.get("enviar", True):
        if estado_envio != ENVIO_INACTIVO:
            estado_envio = ENVIO_INACTIVO
            intentos = 0
            led_sensor.apagar()
            estado_sheets = "Pausado"
        return
    if estado_envio == ENVIO_ESPERA:
        if time.ticks_diff(time.ticks_ms(), ultimo_intento) >= TIEMPO_ENTRE_INTENTOS_MS:
            estado_envio = ENVIO_INTENTANDO
            intentar_envio()


# ============================================================================
#  Lectura de sensores (validacion igual que la version C++)
# ============================================================================
def leer_sensores():
    global TEMP, HUM, PRES, heat_index, dew_point, altitude
    global estado_sensor, lectura_valida, last_valid
    if aht is None or bmp is None:
        estado_sensor = "Error sensor"
        led_error.parpadear(100)
        return
    for intento in range(3):
        try:
            h = aht.relative_humidity
            t = aht.temperature
        except Exception:
            t = h = float("nan")
        try:
            p = bmp.pressure / 100.0
        except Exception:
            p = float("nan")
        if 600.0 <= p <= 1100.0 and -20.0 <= t <= 60.0 and 0.0 <= h <= 100.0:
            TEMP, HUM, PRES = t, h, p
            heat_index = calc_heat_index(t, h)
            dew_point = calc_dew_point(t, h)
            altitude = calc_altitude(p)
            verificar_dia()
            actualizar_historial(t, h)
            actualizar_records(t, h, p)
            registrar_presion(p)
            evaluar_alertas()
            led_error.apagar()
            estado_sensor = "OK"
            lectura_valida = True
            last_valid = (t, h, p)
            if cfg.get("enviar", True):
                if wlan.isconnected():
                    iniciar_envio(t, h, p)
                else:
                    encolar(t, h, p)
            return
        time.sleep_ms(200)
    if lectura_valida:
        TEMP, HUM, PRES = last_valid
        estado_sensor = "Sensor recuperando"
    else:
        estado_sensor = "Error sensor"
    led_error.parpadear(100)


# ============================================================================
#  Servidor web (hilo aparte: la pagina nunca se congela durante un envio)
# ============================================================================
def send_all(conn, data):
    vista = memoryview(data)
    total = len(vista)
    enviado = 0
    while enviado < total:
        n = conn.send(vista[enviado:])
        if n <= 0:
            return
        enviado += n


CODIGOS = {200: "OK", 303: "See Other", 400: "Bad Request",
           401: "Unauthorized", 404: "Not Found", 500: "Error interno"}


def responder(conn, code, ctype, body="", extra=None):
    head = "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n" % (code, CODIGOS.get(code, "OK"), ctype)
    if code == 401:
        head += 'WWW-Authenticate: Basic realm="estacion-clima"\r\n'
    if extra:
        for k, v in extra.items():
            head += "%s: %s\r\n" % (k, v)
    head += "Connection: close\r\n\r\n"
    send_all(conn, head.encode())
    if body:
        send_all(conn, body.encode() if isinstance(body, str) else body)


def redirect(conn, location="/"):
    responder(conn, 303, "text/plain", "", {"Location": location})


def unquote(s):
    res = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c == "%" and i + 2 < len(s):
            try:
                res.append(int(s[i + 1:i + 3], 16))
                i += 3
                continue
            except ValueError:
                pass
        res.extend(c.encode("utf-8"))
        i += 1
    return res.decode("utf-8", "replace")


def parse_query(q):
    args = {}
    if not q:
        return args
    for par in q.split("&"):
        if "=" in par:
            k, v = par.split("=", 1)
            args[unquote(k)] = unquote(v)
    return args


def auth_ok(req):
    try:
        marcador = b"Authorization: Basic "
        idx = req.find(marcador)
        if idx < 0:
            return False
        token = req[idx + len(marcador):].split(b"\r\n")[0].strip()
        esperado = binascii.b2a_base64(
            ("%s:%s" % (cfg.get("web_user", "admin"), cfg.get("web_pass", "04330"))).encode()
        ).strip()
        return token == esperado
    except Exception:
        return False


def _fmt(v, dec=1):
    return "%.*f" % (dec, v)


def pagina_principal(conn):
    if TEMPLATE is None:
        responder(conn, 200, "text/plain",
                  "Falta index.html en el dispositivo (copia con mpremote cp index.html :)")
        return
    h = TEMPLATE
    h = h.replace("%TEMPERATURA%", _fmt(TEMP))
    h = h.replace("%HUMEDAD%", _fmt(HUM))
    h = h.replace("%PRESION%", _fmt(PRES))
    h = h.replace("%SENSACION%", _fmt(heat_index))
    h = h.replace("%ROCIO%", _fmt(dew_point))
    h = h.replace("%ALTURA%", _fmt(altitude, 0))
    h = h.replace("%TEMP_MAX%", "--" if temp_max == -100.0 else _fmt(temp_max))
    h = h.replace("%TEMP_MIN%", "--" if temp_min == 100.0 else _fmt(temp_min))
    h = h.replace("%HUM_MAX%", "--" if hum_max == 0.0 else _fmt(hum_max))
    h = h.replace("%HUM_MIN%", "--" if hum_min == 100.0 else _fmt(hum_min))
    h = h.replace("%PRES_MAX%", "--" if pres_max == 0.0 else _fmt(pres_max))
    h = h.replace("%PRES_MIN%", "--" if pres_min == 2000.0 else _fmt(pres_min))
    h = h.replace("%ESTADO%", "ACTIVADO" if cfg.get("enviar", True) else "PAUSADO")
    h = h.replace("%INTERVALO_SEC%", str(cfg.get("intervalo", 10)))
    h = h.replace("%GMT_OFFSET%", _fmt(cfg.get("gmt", -6.0)))
    h = h.replace("%TOGGLE_TEXT%",
                  "<i class='bx bx-pause-circle'></i> Pausar Envío" if cfg.get("enviar", True)
                  else "<i class='bx bx-play-circle'></i> Reanudar Envío")
    h = h.replace("%TOGGLE_CLASS%", "btn-danger" if cfg.get("enviar", True) else "btn-success")
    h = h.replace("%IP%", ip_actual())
    h = h.replace("%RSSI%", str(rssi_actual()))
    h = h.replace("%TIEMPO%", hora_local())
    h = h.replace("%FIRMWARE_VERSION%", VERSION)
    h = h.replace("%FIRMWARE_DATE%", BUILD)
    h = h.replace("%ESTADO_WIFI%", estado_wifi)
    h = h.replace("%ESTADO_SENSOR%", estado_sensor)
    h = h.replace("%ESTADO_SHEETS%", estado_sheets)
    h = h.replace("%TENDENCIA%", tendencia_estado)
    h = h.replace("%ALERTA%", alerta_web)
    h = h.replace("%ALERTA_CHECK%", "checked" if cfg.get("alertas", {}).get("on") else "")
    a = cfg.get("alertas", {})
    h = h.replace("%ALERTA_TMAX%", _fmt(a.get("tmax", 35.0)))
    h = h.replace("%ALERTA_TMIN%", _fmt(a.get("tmin", 0.0)))
    h = h.replace("%ALERTA_HMAX%", _fmt(a.get("hmax", 85.0), 0))
    h = h.replace("%ALERTA_HMIN%", _fmt(a.get("hmin", 15.0), 0))
    h = h.replace("%SHEETS_URL%", cfg.get("sheets_url", ""))
    responder(conn, 200, "text/html", h)


def api_current(conn):
    d = {
        "temp": TEMP, "hum": HUM, "pres": PRES,
        "hi": heat_index, "dp": dew_point, "alt": round(altitude),
        "tmax": temp_max, "tmin": temp_min,
        "hmax": hum_max, "hmin": hum_min,
        "pmax": pres_max, "pmin": pres_min,
        "wifi": estado_wifi, "sensor": estado_sensor, "sheets": estado_sheets,
        "hora": hora_local(), "ip": ip_actual(), "rssi": rssi_actual(),
        "intervalo": cfg.get("intervalo", 10),
        "estado": "ACTIVADO" if cfg.get("enviar", True) else "PAUSADO",
        "version": VERSION, "actualizado": BUILD,
        "tend": tendencia_estado, "trate": tendencia,
        "alerta": alerta_web, "pendientes": cola_pendiente,
        "gmt": cfg.get("gmt", -6.0),
    }
    responder(conn, 200, "application/json", json.dumps(d))


def api_data(conn):
    d = {}
    for i in range(4):
        d["t%d" % i] = hist_t[i]
        d["h%d" % i] = hist_h[i]
    responder(conn, 200, "application/json", json.dumps(d))


CAUSAS = {
    PWRON_RESET: "Encendido",
    HARD_RESET: "Pin de reset",
    WDT_RESET: "Watchdog",
    SOFT_RESET: "Software",
    DEEPSLEEP_RESET: "Deep sleep",
    BROWNOUT_RESET: "Brownout (voltaje)",
}


def causa_reset():
    try:
        return CAUSAS.get(reset_cause(), "Desconocido")
    except Exception:
        return "Desconocido"


def api_diag(conn):
    try:
        st = os.statvfs("/")
        fs_total = st[0] * st[2]
        fs_libre = st[0] * st[3]
    except Exception:
        fs_total = fs_libre = 0
    d = {
        "uptime": time.ticks_ms() // 1000,
        "fecha": fecha_local(), "hora": hora_local(),
        "heap": gc.mem_free(), "heapMin": heap_min, "heapMax": gc.mem_free(),
        "reset": causa_reset(),
        "ssid": ssid_actual(), "canal": canal_actual(), "rssi": rssi_actual(),
        "mac": mac_actual(),
        "version": VERSION, "build": BUILD,
        "intervalo": cfg.get("intervalo", 10),
        "enviar": 1 if cfg.get("enviar", True) else 0,
        "pendientes": cola_pendiente,
        "fs": fs_total, "fs_libre": fs_libre,
        "sdk": "MicroPython",
        "gmt": cfg.get("gmt", -6.0),
        "log": " | ".join(eventos),
    }
    responder(conn, 200, "application/json", json.dumps(d))


def h_toggle(conn, args):
    cfg["enviar"] = not cfg.get("enviar", True)
    guardar_config()
    redirect(conn)


def h_intervalo(conn, args):
    if "segundos" in args:
        try:
            secs = int(float(args["segundos"]))
            # Rango flexible: de 5 s a 24 h (86400 s). Unidad = segundos.
            if 5 <= secs <= 86400:
                cfg["intervalo"] = secs
                guardar_config()
        except Exception:
            pass
    redirect(conn)


def h_zona(conn, args):
    if "horas" in args:
        try:
            h = float(args["horas"])
            if -12.0 <= h <= 14.0:
                cfg["gmt"] = h
                guardar_config()
                registrar_evento("Zona horaria: UTC %+.1f h" % h)
        except Exception:
            pass
    redirect(conn)


def h_sheets(conn, args):
    if "url" in args:
        u = args["url"].strip()
        if u.startswith("http://") or u.startswith("https://"):
            cfg["sheets_url"] = u
            guardar_config()
            registrar_evento("Sheets: URL configurada")
    redirect(conn)


def h_alertas(conn, args):
    a = dict(cfg.get("alertas", dict(DEFAULTS["alertas"])))
    a["on"] = args.get("on", "") in ("1", "true", "on", "checked")
    try:
        if "tmax" in args:
            a["tmax"] = min(60.0, max(-20.0, float(args["tmax"])))
        if "tmin" in args:
            a["tmin"] = min(60.0, max(-20.0, float(args["tmin"])))
        if "hmax" in args:
            a["hmax"] = min(100.0, max(0.0, float(args["hmax"])))
        if "hmin" in args:
            a["hmin"] = min(100.0, max(0.0, float(args["hmin"])))
    except Exception:
        pass
    cfg["alertas"] = a
    guardar_config()
    redirect(conn)


def h_retry(conn, args):
    reintentar_ahora()
    redirect(conn)


def h_resetwifi(conn, args):
    responder(conn, 200, "text/html", "<h1>Borrando WiFi...</h1>")
    time.sleep_ms(500)
    try:
        os.remove(WIFI_FILE)
    except Exception:
        pass
    reset()


RUTAS = {
    "/": lambda c, a: pagina_principal(c),
    "/toggle": h_toggle,
    "/setinterval": h_intervalo,
    "/settimezone": h_zona,
    "/setsheetsurl": h_sheets,
    "/setalerts": h_alertas,
    "/data.json": lambda c, a: api_data(c),
    "/api/current": lambda c, a: api_current(c),
    "/api/diag": lambda c, a: api_diag(c),
    "/retry": h_retry,
    "/resetwifi": h_resetwifi,
}


def atender(conn, req):
    try:
        linea = req.split(b"\r\n")[0].decode("utf-8", "replace")
        partes = linea.split(" ")
        if len(partes) < 2:
            responder(conn, 400, "text/plain", "Peticion invalida")
            return
        destino = partes[1]
        path, _, query = destino.partition("?")
        if not auth_ok(req):
            responder(conn, 401, "text/plain", "Acceso denegado")
            return
        args = parse_query(query)
        fn = RUTAS.get(path)
        if fn:
            fn(conn, args)
        else:
            responder(conn, 404, "text/plain", "404: No encontrado")
    except Exception:
        try:
            responder(conn, 500, "text/plain", "Error interno")
        except Exception:
            pass


def servidor_web():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", 80))
    s.listen(2)
    s.settimeout(1.0)
    print("Servidor web en http://" + ip_actual() + "/")
    while True:
        try:
            conn, addr = s.accept()
        except OSError:
            continue
        try:
            conn.settimeout(5)
            req = conn.recv(8192)
            if req:
                atender(conn, req)
        except Exception:
            pass
        finally:
            try:
                conn.close()
            except Exception:
                pass
            gc.collect()


# ============================================================================
#  Arranque
# ============================================================================
def init_sensores():
    global aht, bmp, i2c
    i2c = I2C(0, sda=Pin(PIN_SDA), scl=Pin(PIN_SCL), freq=100000)
    aht = None
    bmp = None
    if AHT20 is not None:
        try:
            aht = AHT20(i2c)
        except Exception:
            aht = None
    if BMP280 is not None:
        for addr in (0x76, 0x77):
            try:
                bmp = BMP280(i2c, addr=addr)
                break
            except Exception:
                bmp = None
    if aht is None:
        print("Fallo AHT20")
    if bmp is None:
        print("Fallo BMP280")


def main():
    global heap_min, cola_pendiente, estado_wifi

    print("\nEstacion Meteorologica (MicroPython) v" + VERSION)
    cargar_config()
    registrar_evento("Arranque - firmware " + VERSION + " (" + BUILD + ")")

    init_sensores()

    # WiFi: si no hay credenciales guardadas, abre el portal de configuracion.
    if not conectar_wifi():
        portal_wifi()
        # Tras guardar y reiniciar, el boot vuelve a empezar con credenciales.
        conectar_wifi()

    if wlan.isconnected():
        led_wifi.pulsar(3000, 10)
        estado_wifi = "Conectado"
        registrar_evento("WiFi: conectado, IP " + ip_actual())
    else:
        led_wifi.parpadear(100)
        estado_wifi = "Reconectando..."

    sincronizar_ntp()
    verificar_dia()
    cola_pendiente = contar_cola()

    try:
        wdt = WDT(timeout=30000)
    except Exception:
        wdt = None

    _thread.start_new_thread(servidor_web, (), {})

    heap_min = gc.mem_free()
    ultimo_lectura = time.ticks_ms()
    ultimo_drenaje = time.ticks_ms()
    ultimo_ntp = time.ticks_ms()
    ultimo_wifi = time.ticks_ms()
    ultimo_gc = time.ticks_ms()

    leer_sensores()

    while True:
        if wdt:
            wdt.feed()
        led_wifi.actualizar()
        led_sensor.actualizar()
        led_error.actualizar()

        if not wlan.isconnected():
            estado_wifi = "Reconectando..."
            if time.ticks_diff(time.ticks_ms(), ultimo_wifi) >= 5000:
                ultimo_wifi = time.ticks_ms()
                conectar_wifi(timeout_s=10)
        else:
            estado_wifi = "Conectado"

        # Reintentar NTP una vez por minuto hasta tener hora valida.
        if not tiempo_ok() and time.ticks_diff(time.ticks_ms(), ultimo_ntp) >= 60000:
            ultimo_ntp = time.ticks_ms()
            sincronizar_ntp()

        procesar_estado_envio()

        # Drenar la cola offline (hasta 5 lecturas) cada 15 s cuando hay red.
        if time.ticks_diff(time.ticks_ms(), ultimo_drenaje) >= 15000:
            ultimo_drenaje = time.ticks_ms()
            drenar_cola(5)

        # Lectura/envio segun el intervalo configurado (segundos).
        if time.ticks_diff(time.ticks_ms(), ultimo_lectura) >= cfg.get("intervalo", 10) * 1000:
            ultimo_lectura = time.ticks_ms()
            leer_sensores()

        if gc.mem_free() < heap_min:
            heap_min = gc.mem_free()
        if time.ticks_diff(time.ticks_ms(), ultimo_gc) >= 30000:
            ultimo_gc = time.ticks_ms()
            gc.collect()

        time.sleep_ms(20)


if __name__ == "__main__":
    main()
