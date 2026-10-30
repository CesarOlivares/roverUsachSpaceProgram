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
2. **Librería:** en *Programa → Incluir librería → Gestionar librerías*, instala **RF24 de TMRh20**. Está probado con la 1.6.2.
3. **Rover:** abre `rover_6x6/rover_6x6.ino` y configura *Herramientas* así:

   | Opción | Valor |
   |---|---|
   | Placa | ESP32S3 Dev Module |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS), o la de por defecto (el binario ocupa ~360 KB) |
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
└── transport/              RadioLink (nRF24, receptor) y SerialLink (USB directo)

estacion_base/src/
├── model/BaseModel         última telemetría del rover, calidad del enlace, id del panel
├── view/PcView             lo que recibe el panel por USB
├── controller/BridgeController  reenvía cada orden por radio y recoge la telemetría
└── transport/              PcSerialLink (USB) y RadioTx (nRF24, transmisor)
```

## Seguridad del enlace

- **La base nunca repite ni encola órdenes.** Solo reenvía lo que llega del panel, en el momento en que llega. Si una orden no llega al rover tras los reintentos del hardware, se descarta: no sale más tarde.
- **Failsafe:** si el panel se cierra, se desenchufa la base o se pierde la radio, deja de llegar tráfico. Pasados 400 ms, el rover se desarma, para y libera el mando.
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
- **Cada 500 ms, un estado de la base:** `{"type":"base","rover":true,"lq":95,"tx_ok":N,"tx_fail":N}`.

**Servo 5 (canal 4):** está en suspensión. `DRIVE` mueve solo los canales 0..3. Para reactivarlo, cambia `kSteeringServosInDrive` en `rover_6x6/src/config/RoverConfig.h` y `STEERING_SERVOS` en `js/config.js`.
