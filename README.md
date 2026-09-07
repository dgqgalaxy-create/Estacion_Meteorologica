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
- Intervalo de lectura/envio configurable entre 5 y 3600 segundos.
- Envio con reintentos a Google Sheets.
- Auto-OTA desde GitHub Releases con verificacion SHA-256 del binario y
  rollback automatico si el firmware nuevo no arranca.
- Actualizacion de firmware por OTA clasica con el nombre `estacion-clima`.
- Persistencia en NVS del intervalo de envio y de los umbrales de alerta.

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

- Ver temperatura, humedad, presion, altitud, sensacion termica, punto de
  rocio y tendencia de presion.
- Consultar maximos y minimos del dia.
- Activar o pausar el envio a Google Sheets.
- Configurar umbrales de alerta de temperatura y humedad.
- Consultar por separado el estado de Google Sheets y de las actualizaciones OTA.
- Cambiar el intervalo de lectura/envio entre 5 y 3600 segundos.
- Forzar un reintento del ultimo envio.
- Buscar manualmente nuevas versiones del firmware.
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
| `/setsheetsurl?url=...` | Guarda la URL de Google Sheets en NVS |
| `/setalerts?...` | Guarda umbrales de alerta (`on`, `tmax`, `tmin`, `hmax`, `hmin`) |
| `/retry` | Reintenta el ultimo envio |
| `/checkupdate` | Consulta inmediatamente una nueva version del firmware |
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
primer arranque. Asi, una actualizacion OTA (cuyo binario publico no contiene
tu URL privada) no rompe el envio de los dispositivos ya configurados.

Si el WiFi esta caido o un envio agota sus reintentos, la lectura se guarda en
una cola offline (`/cola.csv` en LittleFS, hasta 200 lecturas) y se reenvia
automaticamente (5 por ciclo de 15 s) cuando hay conexion y el envio esta
activado. El numero de lecturas pendientes se muestra en `/api/current` y en
la pagina de diagnostico.

## Actualizaciones OTA

El proyecto admite dos mecanismos OTA. Despues de la primera carga por USB, el
dispositivo anuncia el servicio OTA con el nombre `estacion-clima`. Si
PlatformIO lo detecta en la red, configura el puerto OTA en `platformio.ini` o
ejecuta:

```bash
pio run -t upload --upload-port estacion-clima.local
```

Tambien puedes reemplazar el nombre por la IP local del ESP32 si mDNS no esta
disponible.

Ademas, cada seis horas el ESP32 consulta la ultima GitHub Release publica del
repositorio. Si encuentra un tag semantico mayor que su version instalada,
descarga el asset `firmware.bin`, verifica su SHA-256 contra el que publica la
API de GitHub, lo instala en la particion OTA alternativa y reinicia. Si el
hash no coincide, el binario se descarta y el firmware actual no se toca. La
primera consulta se realiza aproximadamente un minuto despues del arranque.

Tras instalar un OTA, el nuevo firmware se marca como "pendiente de
confirmar": si arranca 3 veces sin confirmarse sano (WiFi conectado, sensores
OK durante 90 s), el dispositivo vuelve automaticamente a la version anterior
con `Update.rollBack()`.

El panel muestra la version instalada y la fecha/hora de compilacion de ese
firmware como **Ultima actualizacion**. Esta fecha corresponde a la compilacion
que se cargo en el ESP32; no es la fecha de la ultima consulta a GitHub.

### Primera instalacion de esta funcion

El ESP32 no puede actualizarse por si mismo si nunca ha recibido un firmware
con esta logica OTA. Por eso, cada dispositivo debe recibir una primera carga
por USB con la version actual del proyecto (`1.1.4`), incluyendo su
`include/config.h` local. Despues de esa carga, las siguientes versiones se
pueden distribuir mediante GitHub Releases sin volver a conectar el USB.

Durante una actualizacion OTA los LEDs ejecutan una secuencia de baile basada
en el progreso de descarga: LED WiFi, LED sensor, LED de error y los tres
juntos. Al terminar, el ESP32 reinicia con el firmware nuevo.

Tambien puedes pulsar **Buscar actualizacion** en el panel para no esperar la
consulta automatica. Si no hay una version nueva, el estado mostrara
`Firmware comprobado`.

Para publicar una version automatica:

1. Cambia `firmwareVersion` en `src/main.cpp` y la version
   `FIRMWARE_VERSION` de `platformio.ini` al mismo valor.
2. Haz commit de los cambios.
3. Crea y sube un tag:

   ```bash
   git tag v1.1.0
   git push origin v1.1.0
   ```

GitHub Actions compilara el firmware y creara una release con `firmware.bin`.
El repositorio debe ser publico para que el ESP32 pueda consultar la release y
descargarla sin credenciales. La actualizacion usa HTTPS, pero el firmware no
puede validar la cadena de certificados de GitHub de forma estricta porque los
certificados pueden rotar; por eso se recomienda usar esta funcion solo con
releases controladas y una red WiFi confiable.

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
reiniciar el ESP32. Persisten en NVS el intervalo de envio y los umbrales de
alerta; en LittleFS persiste la cola de lecturas pendientes de Google Sheets.

## Seguridad y notas

- No publiques `include/config.h`; esta incluido en `.gitignore`.
- La interfaz web no tiene autenticacion. Usala dentro de una red confiable y
  evita exponer el puerto 80 directamente a Internet.
- La interfaz carga Chart.js, Boxicons y la fuente Inter desde CDN; para ver
  todos los elementos visuales el navegador necesita acceso a Internet.
- Los valores de altitud usan una presion de referencia fija de 1013.25 hPa.
