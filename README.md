# Estacion Meteorologica

Firmware para un ESP32 que lee temperatura, humedad y presion atmosferica,
calcula indicadores meteorologicos y publica los datos en una interfaz web
local. Tambien puede enviar cada lectura a Google Sheets mediante un Google
Apps Script.

## Caracteristicas

- Lectura de un sensor AHT20 (temperatura y humedad).
- Lectura de un BMP280 (presion y altitud estimada).
- Calculo de sensacion termica y punto de rocio.
- Tendencia de presion (subiendo/estable/bajando en hPa/h).
- Alertas configurables por umbrales de temperatura y humedad (persistidas en NVS).
- Historial de hasta cuatro dias y registros maximos/minimos en memoria.
- Cola offline: si no hay WiFi o el envio falla, las lecturas se guardan en
  LittleFS y se reenvian a Google Sheets al reconectar.
- Panel web responsive servido directamente por el ESP32, con pagina de
  diagnostico (RAM, reinicios, motivo de reset, log de eventos).
- Actualizacion automatica del panel mediante `/api/current` y `/data.json`.
- Configuracion WiFi con WiFiManager.
- Intervalo de lectura/envio configurable entre 5 y 86400 segundos (hasta 24 horas).
- Envio con reintentos a Google Sheets.
- URL de Google Sheets configurable desde el panel y persistida en NVS.
- Actualizacion de firmware manual por WiFi (ArduinoOTA con contrasena,
  nombre `estacion-clima`) o por cable USB. No hay auto-actualizacion desde
  GitHub.
- Panel web y actualizacion OTA protegidos con usuario y contrasena.
- Zona horaria configurable desde el panel (persistida en NVS).
- Persistencia en NVS del intervalo de envio, de los umbrales de alerta, de la
  zona horaria y de la URL de Google Sheets.

## Hardware y conexiones

| Componente | ESP32 |
| --- | --- |
| SDA de AHT20 y BMP280 | GPIO 21 |
| SCL de AHT20 y BMP280 | GPIO 22 |
| LED de estado WiFi | GPIO 12 |
| LED de estado del sensor/envio | GPIO 14 |
| LED de error | GPIO 27 |
| Alimentacion de sensores | 3.3 V |
| Tierra de sensores | GND |

El BMP280 se busca primero en la direccion I2C `0x76` y despues en `0x77`.
El AHT20 debe estar conectado al mismo bus I2C.

## Estados de los LEDs

Los LEDs se controlan con logica activa: encendido significa nivel HIGH en el
GPIO correspondiente. El parpadeo no bloquea el funcionamiento del equipo.

| LED | GPIO | Estado | Interpretacion |
| --- | ---: | --- | --- |
| WiFi | 12 | Pulso breve: 10 ms encendido y 3 s apagado | Conectado a la red WiFi |
| WiFi | 12 | Parpadeo rapido, cambio cada 100 ms | WiFi desconectado o intentando reconectar |
| WiFi, sensor y error | 12, 14, 27 | Los tres parpadean cada 200 ms durante 3 ciclos | El ESP32 esta entrando en el portal de configuracion WiFi |
| WiFi | 12 | Encendido fijo despues de la secuencia anterior | Portal `Estacion-Clima-Config` activo |
| Sensor/envio | 14 | Encendido fijo durante una peticion | Envio de datos a Google Sheets en curso |
| Sensor/envio | 14 | Apagado | No hay un envio activo |
| Error | 27 | Parpadeo rapido, cambio cada 100 ms | Lectura invalida o fallo temporal de un sensor |
| Error | 27 | Parpadeo, cambio cada 200 ms | Se agotaron los reintentos de envio a Google Sheets |
| Error | 27 | Apagado | Lectura valida y ultimo envio correcto, o no hay error activo |
| WiFi, sensor y error | 12, 14, 27 | Secuencia: uno, otro, otro y los tres encendidos | Actualizacion OTA de firmware en curso |

Durante un fallo temporal de sensor, si existe una lectura valida anterior, el
panel muestra `Sensor recuperando` y conserva ese ultimo valor mientras el LED
de error parpadea. Al recuperar una lectura correcta, el LED de error se apaga.

## Requisitos

- ESP32 compatible con la definicion `esp32dev`.
- PlatformIO Core o Visual Studio Code con la extension PlatformIO.
- Cable USB para la primera carga.
- Sensores AHT20 y BMP280 conectados.
- Red WiFi de 2.4 GHz.

## Instalacion

1. Clona el repositorio y abre la carpeta en PlatformIO:

   ```bash
   git clone https://github.com/dgqgalaxy-create/Estacion_Meteorologica.git
   cd Estacion_Meteorologica
   ```

2. Crea `include/config.h`. Este archivo esta excluido de Git porque contiene
   la URL privada del Google Apps Script y las credenciales de acceso:

   ```cpp
   #ifndef CONFIG_H
   #define CONFIG_H

   // URL del despliegue de Google Apps Script (sustituye TU_ID por tu ID real).
   #define GOOGLE_SCRIPT_URL "https://script.google.com/macros/s/TU_ID/exec"

   // Credenciales del panel web y de la actualizacion OTA.
   // WEB_PASSWORD debe coincidir con `upload_flags = --auth=...`
   // de platformio.ini.
   #define WEB_USERNAME "admin"
   #define WEB_PASSWORD "04330"

   #endif
   ```

   Sustituye `TU_ID` por la URL de tu despliegue de Google Apps Script y cambia
   las credenciales si lo deseas. Si no vas a usar Google Sheets, conserva una
   URL valida o desactiva el envio desde el panel web despues de iniciar el
   dispositivo.

3. Compila el proyecto. PlatformIO descargara automaticamente las
   dependencias declaradas en `platformio.ini`:

   ```bash
   pio run
   ```

4. Conecta el ESP32 por USB, ajusta el `upload_port` del entorno `[env:usb]`
   en `platformio.ini` y realiza la primera carga por cable:

   ```bash
   pio run -e usb -t upload
   ```

5. Abre el monitor serie a 115200 baudios:

   ```bash
   pio device monitor -b 115200
   ```

## Configuracion WiFi

En el primer arranque, o cuando no existan credenciales guardadas, el ESP32
crea una red WiFi llamada `Estacion-Clima-Config`. Conecta un telefono o
computadora a esa red y completa el portal de configuracion de WiFi.

El portal permanece disponible durante 180 segundos. Las credenciales se
guardan en el ESP32. Para borrarlas, usa el boton **Reset WiFi** del panel web
o elimina las credenciales y reinicia el dispositivo.

Cuando la conexion es correcta, el dispositivo configura NTP usando
`pool.ntp.org` y la zona horaria guardada (UTC-6 por defecto, ajustable desde
el panel web).

## Uso del panel web

El firmware sirve el panel en el puerto HTTP 80. Abre en el navegador la IP
mostrada en el monitor serie o la direccion:

```text
http://estacion-clima.local/
```

El nombre mDNS puede no funcionar en algunas redes; en ese caso usa la IP
local del ESP32.

Desde el panel puedes:

- Ver temperatura, humedad, presion, altitud, sensacion termica, punto de
  rocio y tendencia de presion.
- Consultar maximos y minimos del dia.
- Activar o pausar el envio a Google Sheets.
- Configurar umbrales de alerta de temperatura y humedad.
- Configurar la URL de Google Sheets.
- Cambiar el intervalo de lectura/envio entre 5 y 86400 segundos (hasta 24 horas).
- Cambiar la zona horaria (diferencia con UTC en horas).
- Forzar un reintento del ultimo envio.
- Ver una pagina de diagnostico (RAM, motivo de reinicio, WiFi, log de eventos).
- Borrar las credenciales WiFi.

### Endpoints

| Ruta | Funcion |
| --- | --- |
| `/` | Panel web principal |
| `/api/current` | Lectura actual, tendencia, alertas y estados en JSON |
| `/api/diag` | Diagnostico (RAM, reset, WiFi, log de eventos) en JSON |
| `/data.json` | Historial de temperatura y humedad |
| `/toggle` | Activa o pausa el envio |
| `/setinterval?segundos=30` | Guarda un nuevo intervalo |
| `/settimezone?horas=-6` | Guarda la zona horaria (diferencia con UTC en horas) |
| `/setsheetsurl?url=...` | Guarda la URL de Google Sheets en NVS |
| `/setalerts?...` | Guarda umbrales de alerta (`on`, `tmax`, `tmin`, `hmax`, `hmin`) |
| `/retry` | Reintenta el ultimo envio |
| `/resetwifi` | Borra la configuracion WiFi y reinicia |

## Google Sheets

El ESP32 realiza una peticion `POST` con contenido
`application/x-www-form-urlencoded` a `GOOGLE_SCRIPT_URL`. Los campos enviados
son:

```text
temp=25.4&hum=58.2&pres=1013.6
```

El Apps Script debe estar desplegado como aplicacion web y aceptar solicitudes
anonimas si el dispositivo no dispone de autenticacion. La aplicacion sigue
hasta seis intentos cuando un envio falla; despues marca el estado como error
y permite reintentarlo desde el panel.

La URL del Apps Script se guarda en NVS y se puede cambiar desde el panel
(`/setsheetsurl`); `include/config.h` solo se usa como valor inicial en el
primer arranque.

Si el WiFi esta caido o un envio agota sus reintentos, la lectura se guarda en
una cola offline (`/cola.csv` en LittleFS, hasta 200 lecturas) y se reenvia
automaticamente (5 por ciclo de 15 s) cuando hay conexion y el envio esta
activado. El numero de lecturas pendientes se muestra en `/api/current` y en
la pagina de diagnostico.

## Actualizacion del firmware

Las actualizaciones son siempre manuales; el firmware no consulta ni descarga
nada de Internet. Hay dos formas:

### Por WiFi (ArduinoOTA)

Despues de la primera carga por USB, el dispositivo anuncia el servicio OTA con
el nombre `estacion-clima`. Con el dispositivo en la misma red:

```bash
pio run -t upload
```

(PlatformIO usa el entorno `[env:esp32dev]` con `upload_protocol = espota`,
`upload_port = estacion-clima.local` y la contrasena en
`upload_flags = --auth=...`; todo ya configurado en `platformio.ini`). Si mDNS
no resuelve, usa la IP local del ESP32:

```bash
pio run -t upload --upload-port 192.168.1.45
```

### Por cable USB

Conecta el ESP32 por USB, ajusta `upload_port` del entorno `[env:usb]` en
`platformio.ini` y ejecuta:

```bash
pio run -e usb -t upload
```

El panel muestra la version instalada y la fecha/hora de compilacion de ese
firmware como **Ultima actualizacion**.

## Estructura del proyecto

```text
include/
  StatusLed.h       Control no bloqueante de los LEDs
  WebHandler.h      Declaraciones del servidor web
  WebPage.h         Interfaz HTML embebida en el firmware
  config.h          Configuracion local, no versionada
src/
  main.cpp          Sensores, WiFi, NTP, OTA y ciclo principal
  WebHandler.cpp    Rutas HTTP y respuestas JSON
platformio.ini      Placa, framework y dependencias
```

## Funcionamiento interno

Al arrancar, el firmware inicializa el bus I2C y ambos sensores, recupera el
intervalo y la zona horaria guardados, conecta el WiFi y configura NTP, OTA y
el servidor web. Despues realiza una lectura inmediata.

En el ciclo principal atiende peticiones HTTP y OTA, mantiene los LEDs,
reconecta el WiFi y procesa el estado de los envios. Cuando vence el intervalo
configurado, valida las lecturas del AHT20 y BMP280, actualiza los calculos y
registros, y comienza el envio a Google Sheets. Las lecturas validas se
conservan como respaldo si un sensor falla temporalmente.

El historial y los maximos/minimos viven en memoria RAM y se reinician al
reiniciar el ESP32. Persisten en NVS el intervalo de envio, la zona horaria y
los umbrales de alerta; en LittleFS persiste la cola de lecturas pendientes de
Google Sheets.

## Seguridad y notas

- No publiques `include/config.h`; esta incluido en `.gitignore` y contiene la
  URL de Google Sheets y las credenciales de acceso.
- El panel web y la actualizacion OTA piden usuario y contrasena
  (`WEB_USERNAME` / `WEB_PASSWORD` en `include/config.h`). La contrasena de OTA
  debe coincidir con `upload_flags = --auth=...` de `platformio.ini`.
- La autenticacion web usa Basic Auth sobre HTTP; es adecuada para una red
  local de confianza. Evita exponer el puerto 80 directamente a Internet.
- La interfaz carga Chart.js, Boxicons y la fuente Inter desde CDN; para ver
  todos los elementos visuales el navegador necesita acceso a Internet.
- Los valores de altitud usan una presion de referencia fija de 1013.25 hPa.
