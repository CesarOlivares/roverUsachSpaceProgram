# Rover 6x6 — Firmware Arduino (ESP32-S3 N16R8)

Versión rápida en Arduino del firmware del rover, para el hardware actual:

- **Tracción**: 6 motores DC en paralelo sobre **un solo canal** de un puente H **BTS7960 / IBT-2** (una consigna de velocidad para todo el rover).
- **Dirección**: 4 servos, uno por esquina (FL/FR/RL/RR), en un **PCA9685** (I2C, addr `0x40`, canales 0–3). Giro **4WS coordinado**: los delanteros giran hacia un lado y los traseros hacia el opuesto. El giro es solo por servos (sin diferencial).
- **Control**: el mismo panel web de siempre (`Proyecto_rover_panel/rover_web_gcs`), embebido en el firmware y servido por SoftAP, más el enlace por Web Serial (USB). Mismo protocolo JSON, telemetría a 20 Hz, failsafe de 400 ms y mando exclusivo.
- Sin stepper (el DRV8825 del firmware ESP-IDF se eliminó de esta versión).

## Conexión

| GPIO | Función |
|---|---|
| 4 | RPWM del BTS7960 (PWM avance, 20 kHz) |
| 5 | LPWM del BTS7960 (PWM retroceso, 20 kHz) |
| 6 | R_EN + L_EN juntos (HIGH = habilitado). Si van fijos a 5V, poner `PIN_DRIVE_EN -1` en `pinout.h` |
| 8 | I2C SDA → PCA9685 |
| 9 | I2C SCL → PCA9685 |

Reservados (no usar): 0/3/45/46 (strapping), 19/20 (USB nativo), 43/44 (UART0), 26–37 (flash/PSRAM octal).

Servos: FL=canal 0, FR=canal 1, RL=canal 2, RR=canal 3. El centro/sentido de cada servo se ajusta en `pinout.h` (`SERVO_TRIM_DEG[]`, `SERVO_INVERT[]`).

## Compilación (Arduino IDE)

1. Instalar el paquete de placas **esp32 de Espressif** (v3.x) y seleccionar:
   - Placa: **ESP32S3 Dev Module**
   - Flash Size: **16MB** · PSRAM: **OPI PSRAM**
   - **USB CDC On Boot: Enabled** (el enlace serie del panel va por el USB nativo)
2. Instalar desde el Library Manager:
   - **ESP Async WebServer** (autor *ESP32Async*) y su dependencia **Async TCP** (*ESP32Async*)
   - **Adafruit PWM Servo Driver Library**
   - **ArduinoJson** (v7)
3. Abrir `rover_arduino/rover_arduino.ino`, compilar y flashear.

## Uso

- Conectarse al WiFi **`Rover_6x6_AP`** (clave `Rover6x6Secure`) y abrir **http://192.168.4.1** — el panel se conecta solo.
- O abrir el panel desde disco (`index.html`) y conectar por **cable USB** (Web Serial, Chrome/Edge).
- El monitor serie a 115200 muestra la telemetría JSON; los logs van prefijados con `# `.

## Panel embebido

Los archivos del panel se embeben en PROGMEM vía `web_pages.h`. Tras editar
`Proyecto_rover_panel/rover_web_gcs/` hay que regenerarlo:

```
python Proyecto_rover_arduino/tools/embed_web.py
```

## Protocolo (resumen)

Una orden JSON por línea (`\n`), igual que el firmware ESP-IDF:

- `HEARTBEAT`, `ARM`, `DISARM`, `STOP` (parar nunca se arbitra y libera el mando)
- `DRIVE {linear, angular}` — `linear` → canal de tracción; `angular` → solo servos (4WS)
- `TEST_MOTOR {motor, speed}` — el índice `motor` se ignora: hay un solo canal
- `SERVO_SET {channel 0..3, angle 0..180}`

Telemetría (20 Hz): `{"type":"telemetry", seq, uptime, armed, owner, motors:[6], servos:[4], rx_ok, rx_err, clients, heap}` — `motors` lleva 6 copias de la única consigna de tracción para que el esquema del panel siga animando las ruedas.
