# Variante MicroPython

Version en **MicroPython** del firmware de la estacion meteorologica, con las
mismas funciones principales que la version C++: panel web (la misma pagina),
sensores AHT20 y BMP280, envio a Google Sheets con reintentos y cola offline,
alertas, historial de 4 dias, tendencia de presion, intervalo de envio en
segundos y zona horaria configurables, y panel protegido con usuario y
contrasena.

Las dos variantes conviven en este repositorio y no se interfieren: usa la
version C++ con PlatformIO o esta con MicroPython segun lo que cargues en el
ESP32.

## Diferencias con la version C++

- **Actualizacion por WiFi**: se usa **WebREPL** (puerto 8266), porque
  ArduinoOTA no existe en MicroPython. Por cable se usa `mpremote`.
- **Sin mDNS**: el panel se abre con la IP del ESP32 (`http://IP/`), no con
  `estacion-clima.local`.
- **Configuracion en archivos** en vez de NVS:
  - `config.json` → intervalo, zona horaria, URL de Sheets, alertas,
    usuario/contrasena del panel y de WebREPL.
  - `wifi.json` → credenciales de la red WiFi.
  - `cola.csv` → lecturas pendientes para Google Sheets.
- El portal de configuracion WiFi es propio (no usa WiFiManager) y el
  servidor web corre en un hilo aparte, asi que la pagina nunca se congela
  durante un envio.

## Requisitos

- ESP32 con 4 MB de flash.
- Python en la computadora, con `esptool` y `mpremote`:

  ```bash
  pip install esptool mpremote
  ```

- Firmware de MicroPython (v1.20 o superior) para ESP32:
  descargalo de <https://micropython.org/download/ESP32_GENERIC/>.
- Sensores AHT20 y BMP280 conectados igual que en la version C++ (SDA GPIO 21,
  SCL GPIO 22) y LEDs en GPIO 12, 14 y 27.

## Instalacion (primera vez)

1. Borra la flash y graba MicroPython (ajusta el puerto; ejemplos:
   `/dev/ttyUSB0` en Linux, `/dev/cu.usbserial-*` en macOS, `COM3` en Windows):

   ```bash
   esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
   esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash -z 0x1000 ESP32_GENERIC-XXXX.bin
   ```

2. Copia los archivos de esta carpeta al dispositivo:

   ```bash
   cd micropython
   mpremote cp boot.py main.py index.html :
   mpremote cp -r lib :
   mpremote reset
   ```

   (Si tu version de `mpremote` no acepta `cp -r`, crea la carpeta y copia
   los drivers sueltos: `mpremote mkdir lib` y luego
   `mpremote cp lib/ahtx0.py lib/bmp280.py :lib/`.)

3. En el primer arranque (sin WiFi guardado) el ESP32 crea la red
   `Estacion-Clima-Config`. Conecta un celular o PC a esa red y abre
   `http://192.168.4.1/` para guardar tu WiFi. El dispositivo se reinicia y
   conecta solo.

4. Abre el panel en el navegador con la IP del ESP32 (se muestra en el monitor
   serie con `mpremote monitor`). Pide usuario y contrasena:
   **`admin` / `04330`** (por defecto; se cambian en `config.json`).

5. Desde el panel ajusta el intervalo de envio (en segundos, 5 a 86400) y la
   zona horaria (diferencia con UTC en horas).

## Configuracion

Edita `config.json` en el dispositivo (con `mpremote cat config.json` /
`mpremote cp config.json .`) o usa el propio panel. Campos utiles:

```json
{
  "intervalo": 10,
  "gmt": -6.0,
  "sheets_url": "https://script.google.com/macros/s/TU_ID/exec",
  "enviar": true,
  "web_user": "admin",
  "web_pass": "04330",
  "webrepl_pass": "04330",
  "alertas": {"on": false, "tmax": 35.0, "tmin": 0.0, "hmax": 85.0, "hmin": 15.0}
}
```

La URL de Google Sheets tambien se puede cambiar desde el panel
(`/setsheetsurl`). El Apps Script debe estar desplegado como aplicacion web
con acceso "Any user".

## Actualizar el firmware (los archivos .py)

### Por cable USB

```bash
mpremote cp main.py :
mpremote cp index.html :   # solo si cambiaste la pagina
mpremote reset
```

### Por WiFi (WebREPL, sustituto del OTA)

El ESP32 escucha WebREPL en el puerto 8266 con la contrasena de
`webrepl_pass`. Desde la misma red puedes:

- Con Thonny: `Ejecutar > Configurar intérprete` y elegir WebREPL con la IP
  y contrasena del ESP32.
- Con el cliente oficial (`webrepl_cli.py`, incluido en las herramientas de
  MicroPython):

  ```bash
  python webrepl_cli.py -p 04330 main.py 192.168.1.45:/main.py
  ```

Si WebREPL no arranca, verifica que tu firmware de MicroPython lo incluya
(los builds oficiales de ESP32 lo incluyen).

## Endpoints del panel

Los mismos que la version C++: `/`, `/api/current`, `/api/diag`, `/data.json`,
`/toggle`, `/setinterval?segundos=30`, `/settimezone?horas=-6`,
`/setsheetsurl?url=...`, `/setalerts?...`, `/retry` y `/resetwifi`.
Todos piden autenticacion basica.

## Notas

- `main.py`, `boot.py` e `index.html` van en la raiz del filesystem del
  dispositivo; los drivers en `/lib`.
- Los maximos/minimos y el historial viven en RAM y se reinician al
  reiniciar el ESP32, igual que en la version C++.
- La autenticacion es Basic Auth sobre HTTP; adecuada para una red local de
  confianza.
- Para volver a la version C++ en cualquier momento, solo tienes que cargarla
  con PlatformIO por cable (pisa la flash completa).
