# Firmware del Rover 6×6 (Arduino, ESP32-S3 N16R8)

Sketch de Arduino en C++ con estructura MVC. Controla 6 motores DC (3 × L298N), 5 servos (PCA9685) y un paso a paso (DRV8825). Recibe órdenes y envía telemetría por cable (Serial) y por Wi-Fi (SoftAP + WebSocket).

## Subir el firmware

1. **Core:** en *Herramientas → Placa → Gestor de placas*, instala **esp32 de Espressif 3.x**. Está probado con la 3.3.12. No hace falta ninguna librería extra.
2. **Credenciales del Wi-Fi:** copia `rover_6x6/src/config/Secrets.example.h` como `Secrets.h` en la misma carpeta y cambia la clave (mínimo 8 caracteres). Git ignora `Secrets.h`. Si no lo creas, el sketch compila igual, pero usa la clave pública del repositorio y avisa al compilar.
3. Abre `rover_6x6/rover_6x6.ino` y configura *Herramientas* así:

   | Opción | Valor |
   |---|---|
   | Placa | ESP32S3 Dev Module |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM |
   | Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
   | Core Debug Level | None (el log del core ensuciaría el enlace por cable) |
   | USB CDC On Boot | Disabled si conectas al puerto UART/COM de la placa · Enabled si usas el USB nativo |

4. Pulsa **Subir**. Con estas opciones el binario ocupa ~1 MB (32 %).

Después de subirlo, el panel queda disponible en **http://192.168.4.1/** al conectarte a la red `Rover_6x6_AP`.

## Si cambias el panel web

Arduino no tiene un equivalente a `EMBED_FILES`, así que el panel se mete dentro del firmware como un header generado. La única fuente de verdad sigue siendo `Proyecto_rover_panel/rover_web_gcs/`. Tras editar cualquier archivo del panel, regenera el header y vuelve a subir:

```bash
powershell -ExecutionPolicy Bypass -File Proyecto_rover_esp/tools/embed_web.ps1
```

Ese comando reescribe `rover_6x6/src/transport/WebAssets.h`. No lo edites a mano.

## Estructura

```
rover_6x6/
├── rover_6x6.ino            arranque: crea y conecta las piezas
└── src/
    ├── config/              RoverConfig.h (parámetros), Secrets.h (clave Wi-Fi)
    ├── model/               MODELO: estado del rover, órdenes y cinemática, sin hardware
    │   ├── RoverModel       armado, mando, salud del enlace, consignas vigentes
    │   ├── Command.h        orden ya decodificada (el controlador no ve JSON)
    │   └── Kinematics       mezcla diferencial y ángulo de dirección
    ├── view/
    │   └── TelemetryView    VISTA: el estado tal como lo ven los paneles (JSON)
    ├── controller/          CONTROLADOR
    │   ├── CommandController  reglas: arbitraje del mando, armado, límites
    │   └── SafetyController   failsafe de pérdida de enlace
    ├── hal/                 drivers: MotorDriver (LEDC), ServoDriver (Wire), StepperDriver, Pinout.h
    ├── transport/           bytes <-> órdenes/telemetría: SerialLink, WebLink, JsonProtocol
    └── util/Log             log por Serial que no rompe las tramas
```

Reglas de dependencia:
- `model` no incluye nada de hardware.
- `view` solo lee el modelo.
- `controller` es el único que toca `model` y `hal` a la vez, y todo pasa por su mutex.
- `transport` solo habla con `controller` y `view`.

Un transporte nuevo (por ejemplo, un receptor i-BUS en el GPIO 15) solo tiene que producir `Command`.

## Protocolo

Cada orden es una línea JSON plana: `{"action":"…","client":"…","seq":N, …}`.

| Acción | Campos | Necesita el mando | Necesita estar armado |
|---|---|---|---|
| `HEARTBEAT` | — | no | no |
| `STOP`, `DISARM` | — | no (cualquiera puede parar) | no |
| `ARM` | — | sí | no |
| `DRIVE` | `linear`, `angular` (-100..100) | sí | sí |
| `TEST_MOTOR` | `motor` (0..5), `speed` (-100..100) | sí | no |
| `SERVO_SET` | `channel` (0..15), `angle` (0..180) | sí | no |
| `STEPPER_JOG` | `steps` (±100000), `speed` (Hz, opcional) | sí | no |

La telemetría sale a 20 Hz y su formato es el mismo de siempre.

**Failsafe:** si el operador que tiene el mando pasa 400 ms sin enviar nada, el rover se desarma, para y libera el mando. Los latidos de los paneles observadores no cuentan para este temporizador.

**Servo 5 (canal 4):** está en suspensión. `DRIVE` mueve solo los canales 0..3, y el canal 4 solo se mueve desde diagnóstico. Para reactivarlo, cambia `kSteeringServosInDrive` en `RoverConfig.h` y `STEERING_SERVOS` en `js/config.js`.
