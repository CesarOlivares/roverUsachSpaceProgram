# Firmware del Rover 6×6: rover + estación base (Arduino, nRF24L01)

```
 PC (panel en el navegador)
   │  USB · líneas JSON (Web Serial, 115200)
   ▼
 ESP32 estación base   ── estacion_base/
   │  nRF24L01 · paquetes binarios de ≤32 bytes, canal 110 (2,510 GHz)
   ▼
 ESP32-S3 del rover    ── rover_6x6/
     tracción de las 6 ruedas (1× BTS7960) · dirección por servos (PCA9685)
```

## Enlace de emergencia por Wi-Fi

Si la radio falla, el panel puede hablar con el rover directamente por Wi-Fi. El rover hace dos cosas a la vez:

1. **Red propia (punto de acceso), siempre encendida:** `ROVER-6x6`. Conecta el PC a esa red y abre **http://192.168.4.1/**.
2. **Se une a una red existente** (por ejemplo, la del lugar de la competencia). En esa red, el panel está en **http://rover6x6.local/** o en la IP que el rover muestra en la tarjeta *Wi-Fi del rover* (pestaña **Diagnóstico**) y en su log por USB.

- **El rover sirve su propio panel.** Está incrustado en el firmware (`WebAssets.h`), así que no hace falta Internet ni GitHub Pages. GitHub Pages es https, y desde https el navegador bloquea la conexión `ws://` sin cifrar con el rover. Por eso, desde Pages, el botón Wi-Fi te lleva al panel del rover.
- **Cambiar de red desde el panel:** en **Diagnóstico → Wi-Fi del rover**, pulsa *Buscar redes*, elige una, escribe la clave y pulsa *Unir el rover a esta red*. El rover la guarda en su memoria (NVS) y la recuerda al reiniciar. *Olvidar red* lo deja solo con su red propia. Hay que desarmar antes.
  - Funciona con el panel conectado por Wi-Fi o por cable directo al rover. A través de la estación base, no.
- **Primera red y claves:** en `rover_6x6/src/config/Secrets.h`, que git ignora. Cópialo de `Secrets.example.h`. Ahí van la clave de la red propia y la red a la que se une la primera vez. Lo que se configure desde el panel manda sobre esto.
- **Una sola radio Wi-Fi:** al unirse a otra red, la red propia del rover se muda al canal de esa red y los conectados a ella se cortan unos segundos. El panel reintenta 6 veces. Si la red guardada no aparece, el rover la vuelve a buscar cada 30 s, para no desestabilizar su red propia.
- **Potencia de transmisión reducida (8,5 dBm):** a máxima potencia, el rover oía a CONEIM pero el router no le contestaba (motivos 2 y 4 en el log: `AUTH_EXPIRE`/`ASSOC_EXPIRE`). Con 8,5 dBm se une a la primera. Se ajusta en `kWifiTxPowerQdbm` (`RoverConfig.h`). Si te falta alcance en la red propia, súbela por pasos y comprueba que sigue uniéndose. Si un intento falla, el log por USB muestra el motivo.
- **El failsafe es el mismo:** sin tráfico del operador durante 400 ms corta la tracción, y a los ~3,4 s se desarma, venga el mando por radio, cable o Wi-Fi.
- **Si cambias el panel,** vuelve a incrustarlo antes de compilar el rover:

```bash
powershell -ExecutionPolicy Bypass -File Proyecto_rover_esp/tools/embed_web.ps1
```

**Órdenes Wi-Fi** (por cable o WebSocket):

| Orden | Respuesta |
|---|---|
| `{"action":"WIFI_SET","ssid":"…","pass":"…"}` | guarda la red y se une a ella |
| `{"action":"WIFI_FORGET"}` | olvida la red y se queda solo con la red propia |
| `{"action":"WIFI_SCAN"}` | `{"type":"wifi_scan","nets":[{"ssid","rssi","ch","open"}…]}` |
| `{"action":"WIFI_STATUS"}` | estado inmediato |

Cada segundo, el rover publica `{"type":"wifi","ap","ap_ip","ap_clients","sta","sta_state","sta_ip","rssi","ch","host","ws","scanning","msg","msg_seq"}`.

## Pinout del rover (ESP32-S3)

| Componente | Señal | GPIO |
|---|---|---|
| BTS7960 (tracción de las 6 ruedas) | RPWM (adelante) | **5** |
| | LPWM (atrás) | **4** |
| | R_EN y L_EN | a **5 V** fijos, sin GPIO |
| PCA9685 (servos de dirección) | SDA | **8** |
| | SCL | **9** |
| nRF24L01 | CE | **14** |
| | CSN | **10** |
| | SCK | **12** |
| | MOSI | **11** |
| | MISO | **13** |
| Enlace USB directo | UART0 TX / RX | 43 / 44 |

- **Tracción:** un solo BTS7960 mueve las 6 ruedas. No hay tracción diferencial, así que el rover gira solo con los servos y no puede girar sobre su propio eje.
  - **Rampa:** la tracción acelera a 150 %/s (de 0 a 100 en ~0,7 s) para no hundir la fuente al arrancar los 6 motores. STOP, DISARM y el failsafe cortan en seco.
  - **Pines a comprobar:** en la versión Arduino de referencia (rama `feature/arduino-4ws-single-drive`) RPWM era el **4** y LPWM el **5**, al revés que aquí. Si el rover avanza cuando pides atrás, intercambia `kTractionRpwm` y `kTractionLpwm` en `Pinout.h`.
  - **R_EN/L_EN:** en esa misma rama iban al **GPIO 6**. Si en tu rover no están fijos a 5 V, pon el pin en `kTractionEn`; sin habilitar, el puente no mueve las ruedas.
- **Dirección 4WS:** canal 0 = FL, 1 = FR, 2 = RL, 3 = RR. Los delanteros giran hacia un lado y los traseros hacia el otro para cerrar el radio de giro. El centro y el sentido de cada servo se ajustan en `kServoTrimDeg` y `kServoInvert` (`RoverConfig.h`). El canal 4 está en suspensión.
- **Servos sin bloquear la radio:** el controlador solo apunta los ángulos; una tarea propia del `ServoDriver` es la única que escribe por I2C. Antes, cada escritura de servos frenaba la recepción de la radio y saltaba el failsafe. Además, cada segundo comprueba que el PCA9685 siga a 50 Hz y lo reconfigura si un pico de corriente lo reseteó.
- **Paso a paso (DRV8825):** no está montado y su driver queda desactivado (`kStepperMounted = false` en `RoverConfig.h`). Si se vuelve a montar, hay que asignarle pines libres, porque su antiguo EN (GPIO 8) ahora es el SDA.

- **Panel:** habla con la estación base exactamente igual que si estuviera enchufado al rover. No sabe que hay una radio en medio.
- **Estación base:** traduce cada línea JSON a un paquete binario y lo envía por radio. El rover devuelve la telemetría dentro del ACK automático del nRF24, y la base la convierte otra vez en JSON para el panel.
- **Pruebas en banco:** el panel también se puede enchufar directamente al USB del rover. El protocolo es el mismo, solo que sin radio.

## Material

| | Estación base | Rover |
|---|---|---|
| Placa | ESP32 (DevKit/WROOM-32) o ESP32-S3 | ESP32-S3 N16R8 |
| Radio | nRF24L01 o nRF24L01+PA+LNA | nRF24L01 o nRF24L01+PA+LNA |

**Alimentación del nRF24L01:**
- Se alimenta a **3,3 V. Nunca a 5 V.**
- Pon un **condensador de 10 a 100 µF** entre VCC y GND, soldado lo más cerca posible del módulo. Sin él, los picos de corriente al transmitir provocan paquetes perdidos o un módulo que no responde.
- La versión **PA+LNA** consume más de lo que da el regulador de muchas placas. Aliméntala con su propio regulador de 3,3 V o con un adaptador para nRF24.

## Conexiones del nRF24L01

| nRF24L01 | Rover (ESP32-S3) | Base: ESP32 clásico | Base: ESP32-S3 |
|---|---|---|---|
| VCC | 3V3 | 3V3 | 3V3 |
| GND | GND | GND | GND |
| CE | GPIO 14 | GPIO 4 | GPIO 14 |
| CSN | GPIO 10 | GPIO 5 | GPIO 10 |
| SCK | GPIO 12 | GPIO 18 | GPIO 12 |
| MOSI | GPIO 11 | GPIO 23 | GPIO 11 |
| MISO | GPIO 13 | GPIO 19 | GPIO 13 |
| IRQ | sin conectar | sin conectar | sin conectar |

La estación base elige sola los pines según la placa para la que compiles (`estacion_base/src/config/BaseConfig.h`).

## Subir el firmware

1. **Core:** en *Herramientas → Placa → Gestor de placas*, instala **esp32 de Espressif 3.x**. Está probado con la 3.3.12.
2. **Librería:** en *Programa → Incluir librería → Gestionar librerías*, instala **RF24 de TMRh20**. Está probado con la 1.6.2. El Wi-Fi no necesita librerías extra.
   - **Credenciales Wi-Fi:** copia `rover_6x6/src/config/Secrets.example.h` como `Secrets.h` y pon tus claves. Si no lo creas, compila igual, pero avisa y usa la clave pública de ejemplo.
3. **Rover:** abre `rover_6x6/rover_6x6.ino` y configura *Herramientas* así:

   | Opción | Valor |
   |---|---|
   | Placa | ESP32S3 Dev Module |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM |
   | Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)**. Con Wi-Fi y el panel incrustado, el binario ocupa ~1,1 MB y apenas cabe en la de por defecto |
   | Core Debug Level | None |
   | USB CDC On Boot | Disabled si usas el puerto UART/COM de la placa · Enabled si usas el USB nativo |

4. **Estación base:** abre `estacion_base/estacion_base.ino`. Elige la placa que tengas (*ESP32 Dev Module* o *ESP32S3 Dev Module*), pon *Core Debug Level* en **None** y súbelo.
5. **Uso:**
   1. Enchufa la base al PC.
   2. Abre el panel (`Proyecto_rover_panel/rover_web_gcs/index.html`) en Chrome o Edge.
   3. Pulsa **Conectar estación base (USB)** y elige el puerto de la base.
   4. En la cabecera tiene que aparecer `📻 RADIO nRF24 · NN%`.

## Si cambias el protocolo

El código común a los dos sketches está en `common/`: las órdenes, el JSON del panel, la telemetría y el protocolo de radio. Arduino solo compila lo que hay dentro de la carpeta del sketch, así que `common/` se copia a `src/common/` de cada uno. **Edita siempre `common/`, nunca las copias**, y después:

```bash
powershell -ExecutionPolicy Bypass -File Proyecto_rover_esp/tools/sync_common.ps1
```

Vuelve a subir **los dos** sketches. Si cambias el formato de los paquetes, sube también `radio::kVersion`: así, un extremo con la versión antigua descarta los paquetes en vez de interpretarlos mal.

**Emparejamiento:** el canal (`kChannel`) y la dirección (`kRoverAddress`) están en `common/RadioProtocol.h`. Si hay otro rover u otro equipo con nRF24 cerca, cambia la dirección y vuelve a subir ambos.

> **Seguridad:** el nRF24L01 no cifra. Cualquiera con otro nRF24 en el mismo canal y con la misma dirección podría enviar órdenes al rover. Para un entorno de pruebas es aceptable. Si el rover va a salir a sitios con público, conviene añadir una firma a los paquetes.

## Estructura (MVC)

```
common/                     protocolo compartido (fuente de verdad)
├── Command.h               orden ya decodificada
├── JsonProtocol            línea JSON del panel -> Command
├── Telemetry               estado del rover -> línea JSON para el panel
└── RadioProtocol           Command / telemetría <-> paquetes nRF24 de ≤32 B

rover_6x6/src/
├── model/                  MODELO: RoverModel (armado, mando, enlace, consignas), Kinematics
├── view/TelemetryView      VISTA: telemetría en JSON (cable) y en binario (ACK de radio)
├── controller/             CONTROLADOR: CommandController (reglas), SafetyController (failsafe)
├── hal/                    drivers: TractionDriver (BTS7960), ServoDriver, StepperDriver (sin montar), Pinout.h
└── transport/              RadioLink (nRF24, receptor), SerialLink (USB directo) y WifiLink
                            (punto de acceso + red configurable, panel embebido y /ws)

estacion_base/src/
├── model/BaseModel         última telemetría del rover, calidad del enlace, id del panel
├── view/PcView             lo que recibe el panel por USB
├── controller/BridgeController  reenvía cada orden por radio y recoge la telemetría
└── transport/              PcSerialLink (USB) y RadioTx (nRF24, transmisor)
```

## Seguridad del enlace

- **La base nunca repite ni encola órdenes.** Solo reenvía lo que llega del panel, en el momento en que llega. Si una orden no llega al rover tras los reintentos del hardware, se descarta: no sale más tarde.
- **Failsafe con reconexión:** si el panel se cierra, se desenchufa la base o se pierde la radio, deja de llegar tráfico.
  - A los **400 ms** el rover corta la tracción en seco, pero sigue armado y conserva el mando.
  - Hace **3 intentos de reconexión de 1 s**: en cada uno reconfigura su nRF24, por si un bajón de tensión lo dejó con la configuración de fábrica. La base hace lo mismo con el suyo, y el panel muestra `RECONECTANDO ROVER · n/3` (o `USB` si lo que se cortó es el cable, y entonces reabre el puerto).
  - Si el enlace vuelve, se sigue conduciendo sin rearmar.
  - Si no vuelve en ~3,4 s, el rover se desarma y libera el mando, y el panel se desconecta.
  - Los tiempos están en `RoverConfig.h` (`kLinkRetryCount`, `kLinkRetryWindowMs`) y en `js/config.js` (`RETRY_COUNT`, `RETRY_WINDOW_MS`); tienen que coincidir.
- **Indicadores del panel:**
  - `BASE OK · SIN SEÑAL DEL ROVER`: la base funciona pero no recibe ACK del rover.
  - `SIN DATOS`: no llega telemetría.
  - El porcentaje es la calidad del enlace: el porcentaje de los últimos 20 envíos que recibieron confirmación.

## Protocolo del panel (JSON por USB)

Cada orden es una línea: `{"action":"…","client":"…","seq":N, …}`.

| Acción | Campos | Necesita el mando | Necesita estar armado |
|---|---|---|---|
| `HEARTBEAT` | — | no | no |
| `STOP`, `DISARM` | — | no (cualquiera puede parar) | no |
| `ARM` | — | sí | no |
| `DRIVE` | `linear`, `angular` (-100..100) | sí | sí |
| `TEST_MOTOR` | `motor` (0..5), `speed` (-100..100); cualquier rueda mueve la tracción entera | sí | no |
| `SERVO_SET` | `channel` (0..15), `angle` (0..180) | sí | no |
| `STEPPER_JOG` | `steps` (±100000), `speed` (Hz, 0..65535); sin efecto mientras el paso a paso no esté montado | sí | no |

**Desde la estación base el panel recibe:**
- **La telemetría de siempre**, más el campo `radio_lq`.
- **Cada 500 ms, un estado de la base:** `{"type":"base","rover":true,"lq":95,"tx_ok":N,"tx_fail":N,"arc":3,"retry":0,"reinit":0,"chip":true,"up":120}`.
  - `arc`: retransmisiones medias por paquete ×10, en los últimos 20 envíos. Sube antes de que se pierdan paquetes.
  - `retry`: intento de reconexión en curso (0 = enlace sano). `reinit`: veces que la base reconfiguró su radio.
  - `chip`: el nRF24 de la base responde por SPI. `up`: segundos desde que arrancó la base; si baja, la base se reinició.
  - El panel usa estos datos en la pestaña **Señal**: gráficas de los últimos 2 minutos, registro de cortes (exportable a CSV) y un diagnóstico de la causa.

**Servo 5 (canal 4):** está en suspensión. `DRIVE` mueve solo los canales 0..3. Para reactivarlo, cambia `kSteeringServosInDrive` en `rover_6x6/src/config/RoverConfig.h` y `STEERING_SERVOS` en `js/config.js`.
