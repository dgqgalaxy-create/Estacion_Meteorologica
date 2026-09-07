# Estacion Meteorologica

Firmware para un ESP32 que lee temperatura, humedad y presion atmosferica,
calcula indicadores meteorologicos y publica los datos en una interfaz web
local. Tambien puede enviar cada lectura a Google Sheets mediante un Google
Apps Script.

## Caracteristicas

- Lectura de un sensor AHT20 (temperatura y humedad).
- Lectura de un BMP280 (presion y altitud estimada).
- Calculo de sensacion termica y punto de rocio.
- Historial de hasta cuatro dias y registros maximos/minimos en memoria.
- Panel web responsive servido directamente por el ESP32.
- Actualizacion automatica del panel mediante `/api/current` y `/data.json`.
- Configuracion WiFi con WiFiManager.
- Intervalo de envio configurable entre 5 y 3600 segundos.
- Envio con reintentos a Google Sheets.
- Actualizacion de firmware por OTA con el nombre `estacion-clima`.
- Persistencia del intervalo de envio en la memoria NVS del ESP32.

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

2. Crea `include/config.h`. Este archivo esta excluido de Git porque puede
   contener la URL privada del Google Apps Script:

   ```cpp
   #ifndef CONFIG_H
   #define CONFIG_H

   const char* GOOGLE_SCRIPT_URL =
       "https://script.google.com/macros/s/TU_ID/exec";

   #endif
   ```

   Sustituye `TU_ID` por la URL de tu despliegue de Google Apps Script. Si no
   vas a usar Google Sheets, conserva una URL valida o desactiva el envio
   desde el panel web despues de iniciar el dispositivo.

3. Compila el proyecto. PlatformIO descargara automaticamente las
   dependencias declaradas en `platformio.ini`:

   ```bash
   pio run
   ```

4. Conecta el ESP32 por USB y realiza la primera carga:

   ```bash
   pio run -t upload
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
`pool.ntp.org` y la zona horaria UTC-6 sin horario de verano.

## Uso del panel web

El firmware sirve el panel en el puerto HTTP 80. Abre en el navegador la IP
mostrada en el monitor serie o la direccion:

```text
http://estacion-clima.local/
```

El nombre mDNS puede no funcionar en algunas redes; en ese caso usa la IP
local del ESP32.

Desde el panel puedes:

- Ver temperatura, humedad, presion, altitud, sensacion termica y punto de
  rocio.
- Consultar maximos y minimos del dia.
- Activar o pausar el envio a Google Sheets.
- Cambiar el intervalo de lectura/envio entre 5 y 3600 segundos.
- Forzar un reintento del ultimo envio.
- Borrar las credenciales WiFi.

### Endpoints

| Ruta | Funcion |
| --- | --- |
| `/` | Panel web principal |
| `/api/current` | Lectura actual y estados en JSON |
| `/data.json` | Historial de temperatura y humedad |
| `/toggle` | Activa o pausa el envio |
| `/setinterval?segundos=30` | Guarda un nuevo intervalo |
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

## Actualizaciones OTA

Despues de la primera carga por USB, el dispositivo anuncia el servicio OTA
con el nombre `estacion-clima`. Si PlatformIO lo detecta en la red, configura
el puerto OTA en `platformio.ini` o ejecuta:

```bash
pio run -t upload --upload-port estacion-clima.local
```

Tambien puedes reemplazar el nombre por la IP local del ESP32 si mDNS no esta
disponible.

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
intervalo guardado, conecta el WiFi y configura NTP, OTA y el servidor web.
Despues realiza una lectura inmediata.

En el ciclo principal atiende peticiones HTTP y OTA, mantiene los LEDs,
reconecta el WiFi y procesa el estado de los envios. Cuando vence el intervalo
configurado, valida las lecturas del AHT20 y BMP280, actualiza los calculos y
registros, y comienza el envio a Google Sheets. Las lecturas validas se
conservan como respaldo si un sensor falla temporalmente.

El historial y los maximos/minimos viven en memoria RAM y se reinician al
reiniciar el ESP32. El intervalo de envio es el unico ajuste persistente.

## Seguridad y notas

- No publiques `include/config.h`; esta incluido en `.gitignore`.
- La interfaz web no tiene autenticacion. Usala dentro de una red confiable y
  evita exponer el puerto 80 directamente a Internet.
- La interfaz carga Chart.js, Boxicons y la fuente Inter desde CDN; para ver
  todos los elementos visuales el navegador necesita acceso a Internet.
- Los valores de altitud usan una presion de referencia fija de 1013.25 hPa.
