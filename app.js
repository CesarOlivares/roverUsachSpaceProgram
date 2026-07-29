/**
 * Rover 6x6 Ground Control Station - MVC Application Logic
 * WebSocket Controller, Gamepad Controller, View Rendering, Pre-Flight Wizard & Test/Demo Simulator
 */

// Hardware Map Global: Motor orientation direction signs (All positive 1 since pins were physically corrected)
const MOTOR_SIGNS = [1, 1, 1, 1, 1, 1];

const AppState = {
    connected: false,
    isDemo: false,
    isSerial: false,
    serialPort: null,
    serialReader: null,
    serialWriter: null,
    armed: false,
    ip: '192.168.4.1',
    port: '8080',
    socket: null,
    latency: 0,
    lastPingTime: 0,
    heartbeatInterval: null,
    gamepadInterval: null,
    demoInterval: null,
    isManualTesting: false,
    activeTestMotor: undefined,
    testMotorInterval: null,

    // Pre-Flight Wizard State (12 Total Steps)
    wizardStep: 1,
    totalWizardSteps: 12,

    // Telemetry Data
    batteryV: 11.85,
    rawMv: 2755,
    uptime: 0,
    motors: [0, 0, 0, 0, 0, 0],
    servoAngles: [90, 90, 90, 90, 90],
    stepperPos: 0,

    // Drive Control State
    keys: { w: false, a: false, s: false, d: false },
    linearSpeed: 0,
    angularSpeed: 0,
    leftSpeed: 0,
    rightSpeed: 0
};

const AppController = {

    init() {
        this.bindEvents();
    },

    bindEvents() {
        // Keyboard listeners for WASD driving
        window.addEventListener('keydown', (e) => this.handleKeyDown(e));
        window.addEventListener('keyup', (e) => this.handleKeyUp(e));
        window.addEventListener('blur', () => {
            AppState.keys = { w: false, a: false, s: false, d: false };
            const hudWasd = document.getElementById('hud-wasd');
            if (hudWasd) hudWasd.classList.remove('active');
        });

        // Gamepad connected events
        window.addEventListener('gamepadconnected', (e) => {
            console.log("Gamepad connected:", e.gamepad.id);
            document.getElementById('hud-gamepad').classList.add('active');
            this.startGamepadLoop();
        });

        window.addEventListener('gamepaddisconnected', () => {
            document.getElementById('hud-gamepad').classList.remove('active');
        });

        // Bind wheel clicks for testing
        const wheels = document.querySelectorAll('.wheel');
        wheels.forEach((wheel) => {
            wheel.addEventListener('click', (e) => {
                e.stopPropagation();
                this.openMotorPopover(wheel.id, e);
            });
        });

        // Close popover when clicking elsewhere
        document.addEventListener('click', () => {
            this.closeMotorPopover();
        });

        // Prevent popover from closing when clicking inside it
        const popover = document.getElementById('motor-test-popover');
        if (popover) {
            popover.addEventListener('click', (e) => e.stopPropagation());
        }

        // Setup test button hold events
        const btnForward = document.getElementById('btn-popover-forward');
        const btnBackward = document.getElementById('btn-popover-backward');

        const setupHoldEvent = (btn, dir) => {
            const startHandler = (e) => {
                e.preventDefault();
                const motorIdx = AppState.activeTestMotor;
                if (motorIdx !== undefined) {
                    this.startMotorTest(motorIdx, dir);
                }
            };
            const stopHandler = (e) => {
                e.preventDefault();
                const motorIdx = AppState.activeTestMotor;
                if (motorIdx !== undefined) {
                    this.stopMotorTest(motorIdx);
                }
            };

            btn.addEventListener('mousedown', startHandler);
            btn.addEventListener('touchstart', startHandler);

            btn.addEventListener('mouseup', stopHandler);
            btn.addEventListener('mouseleave', stopHandler);
            btn.addEventListener('touchend', stopHandler);
        };

        if (btnForward && btnBackward) {
            setupHoldEvent(btnForward, 'forward');
            setupHoldEvent(btnBackward, 'backward');
        }
    },

    // 1. CONEXIÓN REAL WEBSOCKET (ESP32-S3)
    connect() {
        AppState.isDemo = false;
        AppState.isSerial = false;
        const ipInput = document.getElementById('rover-ip').value.trim();
        const portInput = document.getElementById('rover-port').value.trim();
        AppState.ip = ipInput || '192.168.4.1';
        AppState.port = portInput || '8080';

        const statusMsg = document.getElementById('login-status');
        statusMsg.innerText = "Conectando con ESP32-S3...";
        statusMsg.style.color = "var(--warning)";

        const wsUrl = `ws://${AppState.ip}:${AppState.port}/ws`;

        try {
            AppState.socket = new WebSocket(wsUrl);

            AppState.socket.onopen = () => {
                AppState.connected = true;
                document.getElementById('login-screen').style.display = 'none';

                this.updateBadge('status-badge', '● CONECTADO (ESP32)', 'badge-warning');
                document.getElementById('btn-arm-toggle').disabled = false;

                const debugConn = document.getElementById('debug-connection-status');
                if (debugConn) {
                    debugConn.innerText = `CONECTADO a ${AppState.ip}:${AppState.port}`;
                    debugConn.style.color = "#10b981";
                }

                if (document.activeElement) document.activeElement.blur();
                window.focus();

                this.startHeartbeat();
            };

            AppState.socket.onmessage = (event) => {
                this.handleTelemetry(event.data);
            };

            AppState.socket.onerror = (err) => {
                console.error("WebSocket Error:", err);
                statusMsg.innerText = "Error de conexión. Verifica la IP o usa Modo Prueba.";
                statusMsg.style.color = "var(--danger)";
            };

            AppState.socket.onclose = () => {
                AppState.connected = false;
                AppState.armed = false;
                this.updateBadge('status-badge', '● DESCONECTADO', 'badge-danger');
                this.updateBadge('arm-badge', 'DESARMADO', 'badge-danger');
                document.getElementById('btn-arm-toggle').disabled = true;

                const debugConn = document.getElementById('debug-connection-status');
                if (debugConn) {
                    debugConn.innerText = "DESCONECTADO";
                    debugConn.style.color = "#ef4444";
                }
                const debugArmed = document.getElementById('debug-armed-status');
                if (debugArmed) {
                    debugArmed.innerText = "DISARMED (Seguro)";
                    debugArmed.style.color = "#ef4444";
                }
            };

        } catch (e) {
            statusMsg.innerText = "IP no válida o inaccesible.";
            statusMsg.style.color = "var(--danger)";
        }
    },

    // 2. MODO PRUEBA / SIMULACIÓN (SIN ESP32)
    connectDemo() {
        AppState.isDemo = true;
        AppState.isSerial = false;
        AppState.connected = true;
        AppState.batteryV = 11.85;
        AppState.rawMv = 2755;
        AppState.uptime = 12;

        document.getElementById('login-screen').style.display = 'none';

        this.updateBadge('status-badge', '🧪 MODO PRUEBA (SIMULADO)', 'badge-gray');
        this.updateBadge('latency-badge', 'Latencia: 1 ms (Local)', 'badge-gray');
        document.getElementById('btn-arm-toggle').disabled = false;

        const debugConn = document.getElementById('debug-connection-status');
        if (debugConn) {
            debugConn.innerText = "CONECTADO (Modo Simulación)";
            debugConn.style.color = "#10b981";
        }

        if (document.activeElement) document.activeElement.blur();
        window.focus();

        console.log("=== MODO PRUEBA ACTIVO. PROTOCOLO JSON VISIBLE EN CONSOLA ===");
        this.startDemoSimulation();
    },

    async connectSerial() {
        AppState.isDemo = false;
        AppState.isSerial = true;
        const statusMsg = document.getElementById('login-status');
        statusMsg.innerText = "Solicitando puerto serial...";
        statusMsg.style.color = "var(--warning)";

        try {
            if (!("serial" in navigator)) {
                throw new Error("Tu navegador no soporta la API Web Serial. Usa Chrome o Edge.");
            }

            AppState.serialPort = await navigator.serial.requestPort();
            await AppState.serialPort.open({ baudRate: 115200 });

            if (AppState.serialPort.writable) {
                AppState.serialWriter = AppState.serialPort.writable.getWriter();
            }

            AppState.connected = true;
            document.getElementById('login-screen').style.display = 'none';

            this.updateBadge('status-badge', '🔌 CONECTADO (CABLE / SERIAL)', 'badge-success');
            document.getElementById('btn-arm-toggle').disabled = false;

            const debugConn = document.getElementById('debug-connection-status');
            if (debugConn) {
                debugConn.innerText = "CONECTADO (Cable USB Serial)";
                debugConn.style.color = "#10b981";
            }

            if (document.activeElement) document.activeElement.blur();
            window.focus();

            this.startHeartbeat();
            this.readSerialLoop();

        } catch (err) {
            console.error("Serial connection error:", err);
            statusMsg.innerText = err.message || "Error al abrir el puerto serial.";
            statusMsg.style.color = "var(--danger)";
            AppState.isSerial = false;
            AppState.connected = false;
            if (AppState.serialWriter) {
                try { AppState.serialWriter.releaseLock(); } catch (e) { }
                AppState.serialWriter = null;
            }
        }
    },

    async readSerialLoop() {
        const textDecoder = new TextDecoderStream();
        const readableStreamClosed = AppState.serialPort.readable.pipeTo(textDecoder.writable);
        const reader = textDecoder.readable.getReader();
        AppState.serialReader = reader;

        let buffer = "";

        try {
            while (true) {
                const { value, done } = await reader.read();
                if (done) {
                    break;
                }
                if (value) {
                    buffer += value;
                    let lines = buffer.split("\n");
                    buffer = lines.pop(); // guardar última línea incompleta

                    for (const line of lines) {
                        const trimmed = line.trim();
                        if (trimmed) {
                            this.handleTelemetry(trimmed);
                        }
                    }
                }
            }
        } catch (err) {
            console.error("Serial read loop error:", err);
            this.updateBadge('status-badge', '● DESCONECTADO (CABLE)', 'badge-danger');
            AppState.connected = false;
            AppState.isSerial = false;
            document.getElementById('btn-arm-toggle').disabled = true;
        } finally {
            if (AppState.serialWriter) {
                try { AppState.serialWriter.releaseLock(); } catch (e) { }
                AppState.serialWriter = null;
            }
            reader.releaseLock();
        }
    },

    startDemoSimulation() {
        if (AppState.demoInterval) clearInterval(AppState.demoInterval);

        AppState.demoInterval = setInterval(() => {
            if (AppState.isDemo && AppState.connected) {
                // Simular incremento de tiempo
                AppState.uptime += 0.05;

                // Simular variación de batería ligera
                AppState.batteryV = 11.85 + (Math.sin(AppState.uptime) * 0.05);
                AppState.rawMv = Math.round((AppState.batteryV / 4.3) * 1000);

                const isDriving = AppState.keys.w || AppState.keys.a || AppState.keys.s || AppState.keys.d;

                if ((AppState.armed || isDriving) && !AppState.isManualTesting) {
                    this.calculateDriveVector();
                    const lin = AppState.linearSpeed;
                    const ang = AppState.angularSpeed;

                    // Cinemática de Tracción: Giro a la Izquierda (ang < 0) reduce rueda izquierda y aumenta derecha
                    const leftSpeed = Math.max(-100, Math.min(100, lin + ang));
                    const rightSpeed = Math.max(-100, Math.min(100, lin - ang));

                    AppState.motors = [
                        leftSpeed, rightSpeed, // M1 (FL), M2 (FR)
                        leftSpeed, rightSpeed, // M3 (ML), M4 (MR)
                        leftSpeed, rightSpeed  // M5 (RL), M6 (RR)
                    ];

                    // Cinemática de Servos: 45° Izquierda (ang = -100) -> 90° Centro (ang = 0) -> 135° Derecha (ang = +100)
                    const targetServoAngle = Math.round(90 + (ang / 100.0) * 45.0);
                    for (let s = 0; s < 5; s++) {
                        AppState.servoAngles[s] = targetServoAngle;
                    }
                } else if (!AppState.isManualTesting) {
                    AppState.motors = [0, 0, 0, 0, 0, 0];
                    for (let s = 0; s < 5; s++) {
                        AppState.servoAngles[s] = 90; // Centro por defecto
                    }
                }

                this.renderTelemetryView();
            }
        }, 50); // 20 Hz Simulation loop
    },

    startHeartbeat() {
        if (AppState.heartbeatInterval) clearInterval(AppState.heartbeatInterval);
        AppState.heartbeatInterval = setInterval(async () => {
            const canSend = (!AppState.isDemo && AppState.connected &&
                ((AppState.isSerial && AppState.serialWriter) ||
                    (AppState.socket && AppState.socket.readyState === WebSocket.OPEN)));

            if (canSend) {
                AppState.lastPingTime = performance.now();

                if (AppState.armed && !AppState.isManualTesting) {
                    this.calculateDriveVector();

                    // Send a single un unified DRIVE command instead of 6 individual TEST_MOTOR commands
                    await this.sendJSON({
                        action: 'DRIVE',
                        linear: AppState.linearSpeed,
                        angular: AppState.angularSpeed
                    });
                } else {
                    await this.sendJSON({ action: 'HEARTBEAT' });
                }
            }
        }, 100);
    },

    async sendBatchJSON(objects) {
        if (!objects || !objects.length) return false;

        let payloadStr = '';
        for (const obj of objects) {
            payloadStr += JSON.stringify(obj) + '\n';
        }

        const debugTx = document.getElementById('debug-tx-data');
        if (debugTx) {
            debugTx.innerText = payloadStr.trim();
        }

        if (AppState.isSerial && AppState.connected && AppState.serialWriter) {
            try {
                const encoder = new TextEncoder();
                await AppState.serialWriter.write(encoder.encode(payloadStr));
                return true;
            } catch (err) {
                console.error("Error writing batch to serial port:", err);
                return false;
            }
        } else if (!AppState.isDemo && AppState.connected && AppState.socket && AppState.socket.readyState === WebSocket.OPEN) {
            for (const obj of objects) {
                AppState.socket.send(JSON.stringify(obj));
            }
            return true;
        }
        return false;
    },

    // Enviar JSON (Igual en Modo Real y Modo Simulación para verificar tramas)
    async sendJSON(obj) {
        const payloadStr = JSON.stringify(obj) + '\n';

        const debugTx = document.getElementById('debug-tx-data');
        if (debugTx) {
            debugTx.innerText = payloadStr.trim();
        }

        if (AppState.isSerial && AppState.connected && AppState.serialWriter) {
            try {
                const encoder = new TextEncoder();
                await AppState.serialWriter.write(encoder.encode(payloadStr));
                return true;
            } catch (err) {
                console.error("Error writing to serial port:", err);
                return false;
            }
        } else if (!AppState.isDemo && AppState.connected && AppState.socket && AppState.socket.readyState === WebSocket.OPEN) {
            AppState.socket.send(payloadStr);
            return true;
        }
        return false;
    },

    handleTelemetry(rawJSON) {
        const debugRx = document.getElementById('debug-rx-data');
        if (debugRx) {
            debugRx.innerText = rawJSON;
        }
        try {
            const data = JSON.parse(rawJSON);
            if (data.type === 'telemetry') {
                if (AppState.lastPingTime > 0) {
                    AppState.latency = Math.round(performance.now() - AppState.lastPingTime);
                    document.getElementById('latency-badge').innerText = `Latencia: ${AppState.latency} ms`;
                }

                AppState.batteryV = data.battery_v || 0;
                AppState.rawMv = data.raw_mv || 0;
                AppState.uptime = data.uptime || 0;
                let rawMotors = data.motors || [0, 0, 0, 0, 0, 0];
                AppState.motors = [
                    rawMotors[0] * MOTOR_SIGNS[0],
                    rawMotors[1] * MOTOR_SIGNS[1],
                    rawMotors[2] * MOTOR_SIGNS[2],
                    rawMotors[3] * MOTOR_SIGNS[3],
                    rawMotors[4] * MOTOR_SIGNS[4],
                    rawMotors[5] * MOTOR_SIGNS[5]
                ];
                AppState.stepperPos = data.stepper_pos || 0;

                this.renderTelemetryView();
            }
        } catch (e) {
            console.error("Error parsing telemetry:", e);
        }
    },

    renderTelemetryView() {
        // Raw ADC Monitor
        document.getElementById('adc-mv-val').innerText = `${AppState.rawMv} mV`;
        document.getElementById('adc-pin-v').innerText = `${(AppState.rawMv / 1000).toFixed(2)} V`;

        // Stepper Position
        document.getElementById('stepper-pos-val').innerText = AppState.stepperPos;

        // Dynamic GPIO Pin Status Badges Update
        const motorPinBadges = ['pin-m1-badge', 'pin-m2-badge', 'pin-m3-badge', 'pin-m4-badge', 'pin-m5-badge', 'pin-m6-badge'];
        const wheelIds = ['m1', 'm2', 'm3', 'm4', 'm5', 'm6'];

        for (let i = 0; i < 6; i++) {
            const spd = AppState.motors[i] || 0;
            const badgeEl = document.getElementById(motorPinBadges[i]);

            if (badgeEl) {
                if (Math.abs(spd) > 1.0) {
                    badgeEl.className = "pin-badge pin-active";
                    badgeEl.innerText = `ACTIVO (${Math.round(spd)}%)`;
                } else {
                    badgeEl.className = "pin-badge pin-idle";
                    badgeEl.innerText = "INACTIVO";
                }
            }

            const wheelEl = document.getElementById(wheelIds[i]);
            if (wheelEl) {
                const valSpan = wheelEl.querySelector('.wheel-val');
                if (valSpan) valSpan.innerText = `${Math.round(spd)}%`;

                if (Math.abs(spd) > 1.0) {
                    wheelEl.classList.add('active');
                    const alpha = 0.5 + (Math.abs(spd) / 100.0) * 0.5;
                    wheelEl.style.backgroundColor = `rgba(16, 185, 129, ${alpha})`;
                    wheelEl.style.borderColor = '#059669';
                } else {
                    wheelEl.classList.remove('active');
                    wheelEl.style.backgroundColor = '';
                    wheelEl.style.borderColor = '';
                }
            }
        }

        // Servo Direction Badges Update (Servos 1 to 5 / Channels 0 to 4)
        for (let s = 0; s < 5; s++) {
            const ang = AppState.servoAngles[s] || 90;
            let dirText = `⏺️ CENTRO (${ang}°)`;
            if (ang < 80) dirText = `⬅️ IZQ (${ang}°)`;
            else if (ang > 100) dirText = `➡️ DER (${ang}°)`;

            const tableBadge = document.getElementById(`servo-dir-${s}-badge`);
            if (tableBadge) {
                tableBadge.innerText = dirText;
                if (ang < 80 || ang > 100) tableBadge.className = "pin-badge pin-active";
                else tableBadge.className = "pin-badge pin-i2c";
            }
        }

        // I2C Pin Badge (GPIO 40/41)
        const i2cBadge = document.getElementById('pin-i2c-badge');
        if (i2cBadge) {
            i2cBadge.className = AppState.connected ? "pin-badge pin-i2c" : "pin-badge pin-idle";
            i2cBadge.innerText = AppState.connected ? "COMUNICANDO (50Hz)" : "DESCONECTADO";
        }

        // Stepper Badge (GPIO 37/38/39)
        const stepperBadge = document.getElementById('pin-stepper-badge');
        if (stepperBadge) {
            const isMoving = AppState.stepperPos !== 0;
            stepperBadge.className = isMoving ? "pin-badge pin-active" : "pin-badge pin-idle";
            stepperBadge.innerText = isMoving ? `EN PASOS (${AppState.stepperPos})` : "REPOSO";
        }
    },

    switchMode(mode) {
        const viewGen = document.getElementById('view-general');
        const viewDiag = document.getElementById('view-diagnostics');
        const tabGen = document.getElementById('tab-general');
        const tabDiag = document.getElementById('tab-diagnostics');

        if (mode === 'general') {
            viewGen.classList.remove('hidden');
            viewDiag.classList.add('hidden');
            tabGen.classList.add('active');
            tabDiag.classList.remove('active');
        } else {
            viewGen.classList.add('hidden');
            viewDiag.classList.remove('hidden');
            tabGen.classList.remove('active');
            tabDiag.classList.add('active');
        }
    },

    // =========================================================================
    // PRE-FLIGHT WIZARD STATE MACHINE (12 PASOS COMPLETOS)
    // Paso 1: Conexión y Batería
    // Pasos 2-6: Test de 5 Servos PCA9685 (Canales 0..4) -> Izquierda (45°) y Derecha (135°)
    // Pasos 7-12: Test de 6 Motores DC (M1..M6) -> Avance (+40%) y Retroceso (-40%)
    // =========================================================================
    startPreFlight() {
        AppState.wizardStep = 1;
        document.getElementById('calibration-modal').style.display = 'flex';
        this.renderWizardStep();
    },

    renderWizardStep() {
        const badge = document.getElementById('modal-step-badge');
        const title = document.getElementById('modal-step-title');
        const desc = document.getElementById('modal-step-desc');

        const step = AppState.wizardStep;
        badge.innerText = `PASO ${step} DE 12`;

        // 1. PASO 1: Conexión y Batería
        if (step === 1) {
            title.innerText = 'Test de Conexión y Nivel de Batería';
            desc.innerHTML = `Verificando enlace ${AppState.isDemo ? 'Modo Prueba' : 'WebSocket Real'} y voltaje de seguridad...<br><br>
                <strong>Estado:</strong> Conectado | <strong>Voltaje:</strong> ${AppState.batteryV.toFixed(2)} V<br>
                ¿Deseas iniciar la secuencia de calibración de los 5 Servos y 6 Motores?`;
        }

        // 2. PASOS 2 A 6: PRUEBA DE LOS 5 SERVOS PCA9685 (Canales 0..4)
        else if (step >= 2 && step <= 6) {
            const servoIndex = step - 2; // 0, 1, 2, 3, 4
            const channel = servoIndex;
            const servoNum = servoIndex + 1;

            title.innerText = `Prueba de Servo ${servoNum} (Canal ${channel})`;
            desc.innerText = `Enviando comando de movimiento a Servo ${servoNum} (Izquierda 45° -> Derecha 135° -> Centro 90°). ¿El servo respondió correctamente?`;

            // Secuencia de comandos JSON
            this.sendJSON({ action: 'SERVO_SET', channel: channel, angle: 45 });
            if (AppState.isDemo) {
                AppState.servoAngles[channel] = 45;
                document.getElementById(`servo-${channel}-val`).innerText = "45° (IZQ)";
            }

            setTimeout(() => {
                this.sendJSON({ action: 'SERVO_SET', channel: channel, angle: 135 });
                if (AppState.isDemo) {
                    AppState.servoAngles[channel] = 135;
                    document.getElementById(`servo-${channel}-val`).innerText = "135° (DER)";
                }
            }, 600);

            setTimeout(() => {
                this.sendJSON({ action: 'SERVO_SET', channel: channel, angle: 90 });
                if (AppState.isDemo) {
                    AppState.servoAngles[channel] = 90;
                    document.getElementById(`servo-${channel}-val`).innerText = "90°";
                }
            }, 1200);
        }

        // 3. PASOS 7 A 12: PRUEBA DE LOS 6 MOTORES DC (M1..M6)
        else if (step >= 7 && step <= 12) {
            const motorIndex = step - 7; // 0, 1, 2, 3, 4, 5
            const motorNames = ["M1 (Front Izq)", "M2 (Front Der)", "M3 (Medio Izq)", "M4 (Medio Der)", "M5 (Tras Izq)", "M6 (Tras Der)"];
            const motorName = motorNames[motorIndex];
            const motorNum = motorIndex + 1;

            title.innerText = `Prueba de Motor ${motorName}`;
            desc.innerText = `Enviando impulso de Avance (+40%) y Retroceso (-40%) a Motor M${motorNum}. ¿La rueda giró en ambos sentidos?`;

            AppState.isManualTesting = true;

            // Secuencia de comandos JSON: Avance (+40%) -> Retroceso (-40%) -> Stop (0%)
            this.sendJSON({ action: 'TEST_MOTOR', motor: motorIndex, speed: 40 });
            if (AppState.isDemo) {
                AppState.motors = [0, 0, 0, 0, 0, 0];
                AppState.motors[motorIndex] = 40;
                this.renderTelemetryView();
            }

            setTimeout(() => {
                this.sendJSON({ action: 'TEST_MOTOR', motor: motorIndex, speed: -40 });
                if (AppState.isDemo) {
                    AppState.motors[motorIndex] = -40;
                    this.renderTelemetryView();
                }
            }, 600);

            setTimeout(() => {
                this.sendJSON({ action: 'TEST_MOTOR', motor: motorIndex, speed: 0 });
                if (AppState.isDemo) {
                    AppState.motors[motorIndex] = 0;
                    AppState.isManualTesting = false;
                    this.renderTelemetryView();
                }
            }, 1200);
        }
    },

    passPreFlightStep(confirmed) {
        if (!confirmed) {
            alert("Calibración cancelada. El Rover permanece DESARMADO por seguridad.");
            document.getElementById('calibration-modal').style.display = 'none';
            this.stopAllMotors();
            this.disarm();
            return;
        }

        if (AppState.wizardStep < AppState.totalWizardSteps) {
            AppState.wizardStep++;
            this.renderWizardStep();
        } else {
            document.getElementById('calibration-modal').style.display = 'none';
            alert(`¡Calibración Pre-Flight exitosa de los 5 Servos y 6 Motores! Rover ARMADO (${AppState.isDemo ? 'Modo Simulación' : 'Real'}).`);
            this.arm();
        }
    },

    arm() {
        AppState.armed = true;
        this.sendJSON({ action: 'ARM' });

        const badgeText = AppState.isDemo ? 'ARMADO (SIMULACIÓN)' : 'ARMADO Y LISTO';
        this.updateBadge('arm-badge', badgeText, 'badge-success');
        this.updateBadge('status-badge', '● ARMADO', 'badge-success');

        const btn = document.getElementById('btn-arm-toggle');
        btn.innerText = '🛡️ DESARMAR ROVER';
        btn.classList.add('armed');

        const debugArmed = document.getElementById('debug-armed-status');
        if (debugArmed) {
            debugArmed.innerText = "ARMED (Conducción Activa)";
            debugArmed.style.color = "#10b981";
        }
    },

    disarm() {
        AppState.armed = false;
        this.sendJSON({ action: 'DISARM' });
        this.updateBadge('arm-badge', 'DESARMADO', 'badge-danger');

        const statusText = AppState.isDemo ? '🧪 MODO PRUEBA (SIMULADO)' : '● DISARMED';
        this.updateBadge('status-badge', statusText, AppState.isDemo ? 'badge-gray' : 'badge-warning');

        const btn = document.getElementById('btn-arm-toggle');
        btn.innerText = '🛡️ ARMAR ROVER';
        btn.classList.remove('armed');

        const debugArmed = document.getElementById('debug-armed-status');
        if (debugArmed) {
            debugArmed.innerText = "DISARMED (Seguro)";
            debugArmed.style.color = "#ef4444";
        }
    },

    toggleArm() {
        if (AppState.armed) {
            this.disarm();
        } else {
            this.arm();
        }
    },

    updateBadge(id, text, className) {
        const badge = document.getElementById(id);
        if (badge) {
            badge.innerText = text;
            badge.className = `badge ${className}`;
        }
    },

    handleKeyDown(e) {
        const key = e.key ? e.key.toLowerCase() : '';
        const code = e.code;
        if (['w', 'a', 's', 'd'].includes(key) || ['KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(code)) {
            console.log(`[KEY DOWN] key: ${key}, code: ${code}`);
            if (key === 'w' || code === 'KeyW') AppState.keys.w = true;
            if (key === 'a' || code === 'KeyA') AppState.keys.a = true;
            if (key === 's' || code === 'KeyS') AppState.keys.s = true;
            if (key === 'd' || code === 'KeyD') AppState.keys.d = true;

            const hudWasd = document.getElementById('hud-wasd');
            if (hudWasd) hudWasd.classList.add('active');
        }
    },

    handleKeyUp(e) {
        const key = e.key ? e.key.toLowerCase() : '';
        const code = e.code;
        if (['w', 'a', 's', 'd'].includes(key) || ['KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(code)) {
            if (key === 'w' || code === 'KeyW') AppState.keys.w = false;
            if (key === 'a' || code === 'KeyA') AppState.keys.a = false;
            if (key === 's' || code === 'KeyS') AppState.keys.s = false;
            if (key === 'd' || code === 'KeyD') AppState.keys.d = false;

            if (!AppState.keys.w && !AppState.keys.a && !AppState.keys.s && !AppState.keys.d) {
                const hudWasd = document.getElementById('hud-wasd');
                if (hudWasd) hudWasd.classList.remove('active');
            }
        }
    },

    calculateDriveVector() {
        let left = 0;
        let right = 0;

        const w = AppState.keys.w;
        const s = AppState.keys.s;
        const a = AppState.keys.a;
        const d = AppState.keys.d;

        if (w && !s) {
            if (a && !d) {
                // W + A: Avanzar curva a la izquierda (Izquierda: 0, Derecha: +100)
                left = 0;
                right = 100;
            } else if (d && !a) {
                // W + D: Avanzar curva a la derecha (Izquierda: +100, Derecha: 0)
                left = 100;
                right = 0;
            } else {
                // W solo: Avance recto (Izquierda: +100, Derecha: +100)
                left = 100;
                right = 100;
            }
        } else if (s && !w) {
            if (a && !d) {
                // S + A: Retroceso curva a la izquierda (Izquierda: -100, Derecha: 0)
                left = -100;
                right = 0;
            } else if (d && !a) {
                // S + D: Retroceso curva a la derecha (Izquierda: 0, Derecha: -100)
                left = 0;
                right = -100;
            } else {
                // S solo: Retroceso recto (Izquierda: -100, Derecha: -100)
                left = -100;
                right = -100;
            }
        } else if (a && !d) {
            // A solo: Giro en el propio eje a la izquierda (Izquierda: -100, Derecha: +100)
            left = -100;
            right = 100;
        } else if (d && !a) {
            // D solo: Giro en el propio eje a la derecha (Izquierda: +100, Derecha: -100)
            left = 100;
            right = -100;
        }

        const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
        if (gamepads[0]) {
            const gp = gamepads[0];
            const rawLin = -gp.axes[1];
            const rawAng = gp.axes[0];

            if (Math.abs(rawLin) > 0.1 || Math.abs(rawAng) > 0.1) {
                left = Math.max(-100, Math.min(100, (rawLin * 100) + (rawAng * 100)));
                right = Math.max(-100, Math.min(100, (rawLin * 100) - (rawAng * 100)));
            }
        }

        AppState.leftSpeed = left;
        AppState.rightSpeed = right;
        AppState.linearSpeed = (left + right) / 2;
        AppState.angularSpeed = (left - right) / 2;
    },

    startGamepadLoop() {
        if (AppState.gamepadInterval) return;
        AppState.gamepadInterval = setInterval(() => {
            const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            if (gamepads[0]) {
                document.getElementById('hud-gamepad').classList.add('active');
            }
        }, 100);
    },

    // Diagnostic Functions
    testMotor(index, speedVal) {
        const display = document.querySelector(`.motor-control-item[data-motor="${index}"] .val-display`);
        if (display) display.innerText = `${speedVal}%`;

        if (AppState.isDemo) {
            AppState.isManualTesting = true;
            AppState.motors[index] = parseFloat(speedVal);
            this.renderTelemetryView();
        }

        let targetMotor = parseInt(index);
        let rawSpeed = parseFloat(speedVal);

        // Hardware Map Global motor direction sign
        let targetSpeed = rawSpeed * MOTOR_SIGNS[targetMotor];

        this.sendJSON({
            action: 'TEST_MOTOR',
            motor: targetMotor,
            speed: targetSpeed
        });
    },

    stopAllMotors() {
        AppState.lastSentSpeeds = [null, null, null, null, null, null];
        const ranges = document.querySelectorAll('.motor-control-item input[type="range"]');
        ranges.forEach(r => r.value = 0);
        const displays = document.querySelectorAll('.motor-control-item .val-display');
        displays.forEach(d => d.innerText = '0%');

        AppState.isManualTesting = false;
        AppState.motors = [0, 0, 0, 0, 0, 0];
        this.renderTelemetryView();

        for (let i = 0; i < 6; i++) {
            this.sendJSON({ action: 'TEST_MOTOR', motor: i, speed: 0 });
        }
    },

    testServo(channel, angleVal) {
        const ang = parseFloat(angleVal);
        AppState.servoAngles[channel] = ang;

        let dirText = `⏺️ CENTRO (${ang}°)`;
        if (ang < 80) dirText = `⬅️ IZQ (${ang}°)`;
        else if (ang > 100) dirText = `➡️ DER (${ang}°)`;

        const chSpan = document.getElementById(`servo-${channel}-val`);
        if (chSpan) chSpan.innerText = dirText;

        const tableBadge = document.getElementById(`servo-dir-${channel}-badge`);
        if (tableBadge) {
            tableBadge.innerText = dirText;
            if (ang < 80) tableBadge.className = "pin-badge pin-active";
            else if (ang > 100) tableBadge.className = "pin-badge pin-active";
            else tableBadge.className = "pin-badge pin-i2c";
        }

        this.sendJSON({
            action: 'SERVO_SET',
            channel: parseInt(channel),
            angle: ang
        });
    },

    jogStepper(steps) {
        if (AppState.isDemo) {
            AppState.stepperPos += steps;
            this.renderTelemetryView();
        }
        this.sendJSON({
            action: 'STEPPER_JOG',
            steps: parseInt(steps),
            speed: 1000
        });
    },

    // =========================================================================
    // DIAGNÓSTICO INDIVIDUAL DE MOTORES DESDE LA VISTA VISUAL (POPOVER)
    // =========================================================================
    openMotorPopover(wheelId, event) {
        // Detener pruebas previas
        if (AppState.activeTestMotor !== undefined) {
            this.stopMotorTest(AppState.activeTestMotor);
        }

        const motorNames = {
            m1: "FL (M1)",
            m2: "FR (M2)",
            m3: "ML (M3)",
            m4: "MR (M4)",
            m5: "RL (M5)",
            m6: "RR (M6)"
        };

        const motorIndices = {
            m1: 0, m2: 1, m3: 2, m4: 3, m5: 4, m6: 5
        };

        const motorIdx = motorIndices[wheelId];
        AppState.activeTestMotor = motorIdx;

        const popover = document.getElementById('motor-test-popover');
        const title = document.getElementById('popover-title');
        if (popover && title) {
            title.innerText = `Motor ${motorNames[wheelId] || wheelId}`;

            // Posicionar el popover relativo al contenedor '.rover-view'
            const wheelEl = document.getElementById(wheelId);
            const containerEl = document.querySelector('.rover-view');

            if (wheelEl && containerEl) {
                const wheelRect = wheelEl.getBoundingClientRect();
                const containerRect = containerEl.getBoundingClientRect();

                let top = (wheelRect.top - containerRect.top) + (wheelRect.height / 2) - 60;
                let left = (wheelRect.left - containerRect.left) + (wheelRect.width / 2) - 85;

                // Límites para evitar que salga del contenedor
                if (top < 10) top = 10;
                if (top > containerRect.height - 120) top = containerRect.height - 120;
                if (left < 10) left = 10;
                if (left > containerRect.width - 180) left = containerRect.width - 180;

                popover.style.top = `${top}px`;
                popover.style.left = `${left}px`;
                popover.classList.remove('hidden');
            }
        }
    },

    closeMotorPopover() {
        if (AppState.activeTestMotor !== undefined) {
            this.stopMotorTest(AppState.activeTestMotor);
            AppState.activeTestMotor = undefined;
        }
        const popover = document.getElementById('motor-test-popover');
        if (popover) {
            popover.classList.add('hidden');
        }
    },

    startMotorTest(motorIndex, direction) {
        if (AppState.testMotorInterval) clearInterval(AppState.testMotorInterval);

        AppState.isManualTesting = true;
        const speed = direction === 'forward' ? 50 : -50;

        if (AppState.isDemo) {
            AppState.motors[motorIndex] = speed;
            this.renderTelemetryView();
        }

        let targetMotor = motorIndex;
        let targetSpeed = speed * MOTOR_SIGNS[targetMotor];

        const sendCommand = () => {
            this.sendJSON({
                action: 'TEST_MOTOR',
                motor: targetMotor,
                speed: targetSpeed
            });
        };

        sendCommand();
        AppState.testMotorInterval = setInterval(sendCommand, 50);
    },

    stopMotorTest(motorIndex) {
        if (AppState.testMotorInterval) {
            clearInterval(AppState.testMotorInterval);
            AppState.testMotorInterval = null;
        }

        AppState.isManualTesting = false;

        if (AppState.isDemo) {
            AppState.motors[motorIndex] = 0;
            this.renderTelemetryView();
        }

        this.sendJSON({
            action: 'TEST_MOTOR',
            motor: motorIndex,
            speed: 0
        });
    }
};

// Initialize App on DOM Ready
document.addEventListener('DOMContentLoaded', () => AppController.init());
