# Auditoría del código y propuesta de arquitectura MVC

Proyecto: réplica funcional del rover Perseverance (6×6, ESP32-S3 N16R8)
Alcance: `Proyecto_rover_esp/main/*` (firmware ESP-IDF) y `Proyecto_rover_panel/rover_web_gcs/*` (estación de control web).
Commit auditado: `607f786`.

---

## 1. Resumen

| Parte | Tamaño | Estado general |
|---|---|---|
| Firmware | ~1 100 líneas C, 5 módulos | Drivers bien hechos (mutex, sin reconfigurar GPIO en caliente, filtro I2C). Todo lo demás está concentrado en `rover_link.c`. |
| Panel web | ~1 300 líneas JS en un solo archivo | Ya tiene capas con nombre (`State`, `Link`, `View`, `App`…), pero se llaman entre sí en todas direcciones y la vista accede al estado directamente. |

**Lo más importante antes de reestructurar:** hay un defecto de seguridad en el failsafe (§2.1). Conviene corregirlo primero, como cambio aparte, y no esperar a la migración a MVC.

---

## 2. Hallazgos funcionales (defectos)

### 2.1 🔴 El failsafe no salta mientras haya un panel observador conectado
`rover_link.c:184` llama a `state_mark_packet()` con **cualquier** trama válida, incluido un `HEARTBEAT` de un cliente que no tiene el mando. Por su parte, el panel en modo observador manda `HEARTBEAT` a 20 Hz (`app.js:522-525`).

**Escenario:** el operador A arma el rover y conduce, y el panel B solo observa. A pierde el Wi-Fi. B sigue latiendo, el temporizador del failsafe se renueva y el rover **sigue con la última consigna DRIVE** de forma indefinida. El mando tampoco se libera nunca, porque eso solo ocurre en el failsafe, así que B ni siquiera puede tomar el control. Lo único que funciona es que B pulse STOP.

El comentario de `app.js:523-524` ya cuenta con este riesgo para DRIVE, pero el HEARTBEAT tiene el mismo efecto. Además, en `rover_link.h:38-40` está documentado como una característica ("un cliente web que se caiga no dispara el failsafe"), y eso solo es correcto si quien sigue vivo es el **dueño** del mando.

**Corrección propuesta:** usar dos marcas de vida.
- `last_owner_packet_us`: solo la renuevan las tramas del dueño. Si el rover está armado o hay hardware activo, el failsafe se rige por esta marca.
- `last_any_packet_us`: queda solo para estadísticas.

### 2.2 🟠 Aviso falso de "failsafe" justo después de armar
`App.arm()` pone `State.armed = true` de forma optimista (`app.js:1091`). Si llega una telemetría generada **antes** de que el firmware procese el ARM, `applyTelemetry` la ve con `armed:false`, desarma el panel y muestra "El rover se desarmó (failsafe del firmware)" (`app.js:396-400`). En la siguiente trama vuelve a armarse. Con cable a 115200 baudios la ventana es de una trama (50 ms), así que es fácil que ocurra.
**Corrección:** separar la *intención* local del *estado confirmado*: mantener `armRequested` hasta que la telemetría lo confirme o pase un timeout.

### 2.3 🟠 La parada al cerrar la pestaña probablemente no llega
`beforeunload` llama a `emergencyStop()`, que encola el STOP en una escritura asíncrona (`app.js:972-974`). El navegador puede cerrar la página antes de que salga. Hoy lo que protege es el failsafe, pero ese failsafe es el que falla en §2.1.

### 2.4 🟡 La geometría de dirección no corresponde a un Perseverance
`rover_link.c:235-243` pone los **5** servos en el mismo ángulo y calcula ese ángulo a partir de `angular`.
- En un giro sobre el propio eje (solo A o D: `linear=0`, `angular=±100`), los servos van a 45° o 135° mientras las ruedas hacen skid-steer. En el Perseverance las 4 esquinas se orientan tangentes al círculo de giro: las delanteras y las traseras en sentidos **opuestos**.
- En un giro con avance, las ruedas traseras deberían girar al revés que las delanteras (Ackermann de doble eje), y la velocidad de cada rueda depende de su radio de giro.
- ¿Qué es el 5.º servo? Si no es de dirección (por ejemplo, el mástil), no debería recibir la consigna de DRIVE.

Esto no es un fallo de código. Es un modelo cinemático que falta, y es justo lo que una capa de *modelo* debería tener aislado y con pruebas (§4.1).

### 2.5 🟡 Detalles menores
- `rover_link.h:26-27`: el SSID y la contraseña del AP están en el código de un repositorio **público**. Conviene moverlos a `Kconfig.projbuild` / `sdkconfig` (que ya está en `.gitignore`).
- `pca9685_servo.c:81-97`: si falla `i2c_new_master_bus`, la función devuelve un error y `main.c:38` (`ESP_ERROR_CHECK`) reinicia la placa. Eso contradice "el rover debe poder rodar aunque falte la placa de servos".
- `rover_safe_stop()` centra los servos por I2C desde la tarea del failsafe. Sin la placa son hasta 5 × 50 ms de timeout bloqueando esa tarea.
- `STEPPER_JOG` no acota `steps`, y `target_pos` puede desbordar `int32`.
- `g_last_seq` es global y no va por cliente: con dos paneles, cada uno mide el RTT con el seq del otro.
- El simulador (`DemoTransport`) mezcla la cinemática de otra forma que el firmware: satura con *clamp* y el firmware escala proporcionalmente. Por eso la simulación no se comporta igual que el rover.
- `mcpwm_motors.*` usa LEDC y no MCPWM. El nombre confunde.
- El ESP_LOG y la telemetría comparten UART0: las líneas pueden mezclarse. El panel lo tolera, pero hace que `rx_err` suba en el lado del rover.

---

## 3. Diagnóstico de arquitectura

### 3.1 Firmware
`rover_link.c` (750 líneas) hace **siete trabajos**:

| Responsabilidad | Líneas | Rol MVC real |
|---|---|---|
| Estado compartido (armado, dueño, contadores) | 46-157 | Modelo |
| Reglas de negocio (arbitraje, qué orden requiere armado) | 175-276 | Controlador |
| Cinemática de dirección | 38-40, 235-243 | Modelo (dominio) |
| Parseo JSON del protocolo | 279-288 | Adaptador de entrada |
| Serialización de telemetría | 305-343 | Vista |
| Failsafe | 349-382 | Controlador (seguridad) |
| Transportes UART, Wi-Fi, HTTP, WebSocket | 387-703 | Infraestructura |

Además, el **estado real del rover está repartido entre los drivers**: `g_speeds` en motores, `g_last_angles` en PCA9685 y `g_st` en stepper. La telemetría lo va reuniendo pidiéndoselo a cada driver, y por eso no hay un sitio único que diga "así está el rover ahora".

### 3.2 Panel web
- La `View` lee `State` y llama a `hasControl()` (lógica). `Link` y `applyTelemetry` llaman a `View.*` directamente. `App` y `Wizard` tocan el DOM (`querySelector`, `getElementById`).
- El HTML está acoplado a un global: `onclick="App.setMotor(0, this.value)"`.
- Los nombres de acción (`'DRIVE'`, `'TEST_MOTOR'`…) y la cinemática están duplicados en el firmware y en el simulador, sin un contrato común.

---

## 4. Propuesta MVC

En un sistema embebido de tiempo real, el MVC se adapta así:
**Modelo** = estado del rover + reglas de dominio puras.
**Vista** = representación hacia fuera (telemetría JSON en el firmware, DOM en el panel).
**Controlador** = recibe intenciones (comandos, teclas), aplica las reglas y actualiza el modelo y el hardware.
El hardware y los transportes quedan **fuera** del triángulo, como adaptadores.

### 4.1 Firmware: componentes ESP-IDF

```
Proyecto_rover_esp/
├── main/
│   └── main.c                      # solo arranque y cableado
└── components/
    ├── rover_config/               # Kconfig.projbuild: SSID, clave, timeouts, Hz
    ├── hal/                        # drivers puros, sin estado de negocio
    │   ├── pinout.h
    │   ├── motors_ledc.c/.h        # (renombrado de mcpwm_motors)
    │   ├── servo_pca9685.c/.h
    │   └── stepper_drv8825.c/.h
    ├── model/                      # SIN dependencias de ESP-IDF: compila en PC
    │   ├── rover_model.c/.h        # estado único + mutex + snapshot()
    │   ├── kinematics.c/.h         # mezcla diferencial + geometría de 4 esquinas
    │   └── command.h               # struct rover_cmd_t (tipos, no JSON)
    ├── controller/
    │   ├── command_controller.c/.h # arbitraje, armado, validación -> model + hal
    │   └── safety_controller.c/.h  # failsafe (marca de vida del dueño, §2.1)
    ├── view/
    │   └── telemetry_view.c/.h     # snapshot del modelo -> JSON
    └── transport/                  # adaptadores: bytes <-> comandos/telemetría
        ├── protocol_json.c/.h      # JSON -> rover_cmd_t (único sitio con cJSON)
        ├── link_uart.c/.h
        └── link_ws.c/.h            # SoftAP + HTTP + WebSocket + assets embebidos
```

**Flujo:**
```
UART/WS ──bytes──> protocol_json ──rover_cmd_t──> command_controller ──> rover_model
                                                        │                    │
                                                        └──> hal (motores,   │
                                                             servos, stepper)│
telemetry task <── telemetry_view <── rover_model_snapshot() <───────────────┘
      └──> link_uart / link_ws (broadcast)
safety_controller ── vigila rover_model.last_owner_packet ──> command_controller.safe_stop()
```

**Reglas que hace cumplir el propio build** (cada componente declara `REQUIRES` en su `CMakeLists.txt`, así que una dependencia prohibida no compila):

| Componente | Puede depender de |
|---|---|
| `model` | nada (C estándar) |
| `hal` | ESP-IDF drivers, `rover_config` |
| `controller` | `model`, `hal`, `rover_config` |
| `view` | `model` |
| `transport` | `controller`, `view`, `rover_config`, `json`, `esp_http_server`… |

**Ventajas concretas:**
- `kinematics.c` y `command_controller.c` se pueden probar en el PC con Unity (`idf.py` *host test* o CMake normal). Los cálculos de la geometría Perseverance de §2.4 se validan sin el rover sobre la mesa.
- Para añadir un transporte nuevo (por ejemplo, el receptor Flysky i-BUS ya previsto en GPIO 15) basta un archivo nuevo en `transport/` que produzca `rover_cmd_t`. No hay que tocar las reglas.
- El estado vive en un solo sitio: los drivers dejan de ser la fuente de verdad de la telemetría.

### 4.2 Panel web: módulos ES sin bundler

```
rover_web_gcs/
├── index.html                      # sin onclick; solo data-action / ids
├── style.css
└── js/
    ├── main.js                     # crea modelo, vistas y controladores
    ├── model/
    │   ├── RoverModel.js           # estado + suscripción (EventTarget)
    │   ├── protocol.js             # nombres de acción, encode/decode (contrato)
    │   └── kinematics.js           # misma mezcla que el firmware
    ├── view/
    │   ├── HeaderView.js           # badges de enlace / armado / observador
    │   ├── SchematicView.js        # esquema 6×6 + popover
    │   ├── PinTableView.js
    │   ├── LinkMonitorView.js
    │   ├── DiagnosticsView.js
    │   ├── PreflightView.js        # modal del asistente
    │   └── LoginView.js
    ├── controller/
    │   ├── ConnectionController.js # conectar/reconectar, elegir transporte
    │   ├── DriveController.js      # lazo TX a 20 Hz + teclado/mando
    │   ├── DiagnosticsController.js
    │   ├── PreflightController.js  # lógica del Wizard sin DOM
    │   └── SafetyController.js     # parada de emergencia, beforeunload
    └── transport/
        ├── Link.js                 # cola events/frame (se conserva tal cual)
        ├── SerialTransport.js
        ├── WsTransport.js
        └── DemoTransport.js        # usa model/kinematics.js
```

**Reglas:**
- Las **vistas** solo se suscriben al modelo y emiten intenciones (`view.on('arm-toggle', …)`). No llaman al `Link` ni deciden nada.
- Los **controladores** reciben intenciones, aplican reglas (por ejemplo, "desarmar antes de una prueba individual") y llaman a `Link` o actualizan el modelo.
- El **modelo** no conoce ni el DOM ni el transporte. Distingue entre `armed` (confirmado por telemetría) y `armRequested` (§2.2).

**Impacto en el firmware:** `g_assets` y `EMBED_FILES` hoy listan 3 archivos fijos y con módulos serían unos 20. Hay dos opciones:
1. **Recomendada:** un script CMake que recorra `rover_web_gcs/` y genere la tabla de assets. Así no hay paso de build en JS y se sigue depurando con *file://*.
2. Un bundler (esbuild) que vuelva a producir un único `app.js`. Añade Node al flujo de trabajo.

> Ojo: `type="module"` no funciona sobre `file://` en Chrome, y hoy el panel se abre así para el modo cable. Con la opción 1 habría que servirlo en local (`python -m http.server`) o mantener un `app.js` empaquetado para ese caso. Hay que decidirlo antes de empezar.

### 4.3 Contrato de protocolo
Crear `docs/PROTOCOLO.md`, o mejor un `protocol.json` del que se generen `command.h` y `protocol.js`, con las acciones, los campos, los rangos y quién puede emitir cada una. Hoy ese contrato está implícito y repartido en tres sitios: firmware, panel y simulador.

---

## 5. Plan de migración por fases

Cada fase deja el rover funcionando y se prueba sobre el hardware antes de seguir.

| Fase | Contenido | Riesgo |
|---|---|---|
| **0** | Corregir §2.1 (failsafe por dueño) y §2.2 (armado confirmado). Aparte, sin reestructurar. | Bajo, **urgente** |
| **1** | Firmware: crear `components/hal` moviendo los drivers sin cambiar su API, y `rover_config` con Kconfig (sacar la clave del repo). | Bajo |
| **2** | Firmware: extraer `model/` (estado + snapshot) y `view/telemetry_view` de `rover_link.c`. El JSON de salida queda idéntico. | Medio |
| **3** | Firmware: extraer `controller/` y `transport/protocol_json`. `rover_link.c` desaparece en `link_uart` y `link_ws`. Añadir pruebas host de `command_controller`. | Medio |
| **4** | Panel: partir `app.js` en módulos manteniendo el comportamiento, quitar los `onclick` del HTML y generar la tabla de assets en CMake. | Medio |
| **5** | Dominio: `kinematics` con la geometría de 4 esquinas tipo Perseverance (§2.4), compartida entre firmware y simulador y con pruebas. | Medio, cambia el comportamiento |

---

## 6. Estado de la implementación

Tras las respuestas del equipo, el firmware pasó a un **sketch de Arduino en C++** (`Proyecto_rover_esp/rover_6x6/`, ver su README), en lugar de los componentes ESP-IDF del §4.1. Las capas son las mismas, pero dentro de `src/`.

- **Corregido:**
  - §2.1: el failsafe ahora depende solo del dueño del mando.
  - §2.2: el panel distingue entre "armado confirmado" y "orden pendiente".
  - §2.3: el STOP al cerrar la pestaña sale directo, sin pasar por la cola.
  - §2.5: la clave sale de `Secrets.h`, el arranque sin placa de servos ya no reinicia la ESP, se acota `STEPPER_JOG` y el simulador usa la misma cinemática que el firmware.
- **Servo 5:** queda en suspensión. `DRIVE` no lo mueve y el pre-vuelo no lo prueba.
- **Pre-vuelo:** ya no arma el rover al terminar. El armado es siempre una acción explícita del operador.
- **Nuevo:** si el dueño del mando desaparece sin desarmar, el mando se libera a los 400 ms aunque el rover esté quieto.
- **Pendiente:** §2.4 (geometría de 4 esquinas tipo Perseverance). Cambia el comportamiento, así que se dejó para cuando el equipo lo decida.
- **Panel:** el panel son scripts clásicos (no módulos ES) para que siga abriéndose desde disco con el acceso directo `.url`.

### 6.1 Cambio de enlace: Wi-Fi → nRF24L01

El SoftAP y el servidor web del rover se sustituyeron por una radio nRF24L01. Una segunda ESP32 (`estacion_base/`), conectada al PC por USB, hace de puente: recibe el mismo JSON que antes y lo envía por radio en paquetes binarios. La telemetría vuelve dentro del ACK. El panel ya no se sirve desde el rover: se abre siempre desde disco. El código común a los dos sketches está en `Proyecto_rover_esp/common/`. Detalles y cableado en `Proyecto_rover_esp/README.md`.

## 7. Preguntas abiertas para el equipo (respondidas)
1. ¿Qué mueve el 5.º servo y qué el paso a paso (¿mástil?, ¿brazo?)? Afecta al modelo de dominio.
2. ¿El panel tiene que seguir funcionando abierto desde disco (`file://`) para el modo cable? Decide entre módulos ES y bundler (§4.2).
3. ¿Al terminar el pre-vuelo el rover se debe **armar solo** (`app.js:809-812`) o debe quedar listo para que el operador lo arme?
4. ¿Se quiere C++ en el firmware? (ESP-IDF lo soporta, y clases como `RoverModel` serían más naturales). La propuesta asume C para no cambiar de lenguaje.
