/**
 * Rover 6x6 - Estacion de control en tierra
 *
 * El panel habla por USB (Web Serial) con la estacion base, que reenvia por
 * radio nRF24L01 al rover. Tambien puede enchufarse al USB del rover para
 * pruebas en banco, o ir por Wi-Fi (WebSocket) como enlace de emergencia:
 * el protocolo es el mismo.
 *
 * Estructura (MVC). Son scripts clasicos, no modulos ES, para que el panel
 * funcione abierto desde disco (file://). Se cargan en el orden de index.html
 * y comparten el ambito global:
 *
 *   config.js, util.js   constantes y utilidades
 *   model/               estado (RoverModel), protocolo y cinematica
 *   transport/           serie USB / simulador + cola de salida
 *   view/                pintado del DOM; solo emite intenciones del usuario
 *   controller/          reglas: conexion, conduccion, diagnostico, pre-vuelo
 *   main.js              cableado
 */

'use strict';

const CONFIG = {
    TX_HZ: 20,                  // debe ir muy por encima del failsafe del firmware (400 ms)
    STALE_MS: 800,              // sin telemetria durante este tiempo el enlace se marca caido
    ARM_CONFIRM_MS: 1000,       // plazo para que el rover confirme un armado/desarmado
    BAUD_RATE: 115200,
    MAX_QUEUED_EVENTS: 32,
    TEST_SPEED: 50,             // % de las pruebas rapidas desde el esquema del rover
    WIZARD_SPEED: 40,           // % de las pruebas del asistente de pre-vuelo
    SERVO_COUNT: 5,             // canales de servo que publica la telemetria
    STEERING_SERVOS: 4,         // servos que siguen la direccion; el 5.o esta en suspension
    MOTOR_COUNT: 6,             // ruedas que dibuja el panel; todas las mueve un unico BTS7960
    STEER_CENTER: 90,           // misma geometria que el firmware (RoverConfig.h)
    STEER_THROW: 45,
    BASE_STATUS_STALE_MS: 1500, // la base informa cada 500 ms; sin noticias, no hay base

    // Reconexion: los mismos tiempos que el firmware (RoverConfig.h). A los
    // FAILSAFE_MS sin telemetria el rover ya corto la traccion; se dan
    // RETRY_COUNT intentos de RETRY_WINDOW_MS y, si no vuelve, se desconecta.
    FAILSAFE_MS: 400,
    RETRY_COUNT: 3,
    RETRY_WINDOW_MS: 1000,

    // Wi-Fi de emergencia. Reconectar el portatil a una red tarda mas que
    // reabrir un USB: se dan mas intentos. Pasados 3,4 s el rover ya se desarmo.
    WIFI_DEFAULT_HOST: '192.168.4.1',   // punto de acceso propio del rover
    WIFI_MDNS_HOST: 'rover6x6.local',   // el rover en otra red (mDNS)
    WIFI_OPEN_TIMEOUT_MS: 4000,
    WIFI_RETRY_COUNT: 6,
    WIFI_STATUS_STALE_MS: 3000,         // el rover informa cada segundo

    // Analisis de senal: una muestra cada SAMPLE_MS, se guardan HISTORY_S segundos.
    SAMPLE_MS: 500,
    HISTORY_S: 120,

    // Gesto de "asentir" con el servo 5 (canal 4): adelante y atras alrededor del
    // centro. Si el servo cabecea al reves, pon AMPLITUDE en negativo.
    NOD: {
        CHANNEL: 4,
        CENTER: 90,
        AMPLITUDE: 25,          // grados a cada lado del centro
        CYCLES: 2,              // veces que va adelante y atras
        STEP_MS: 350,           // pausa entre posiciones: da tiempo al servo a llegar
    },
};

/*
 * Identidad de este panel, valida mientras dure la pestana. El firmware concede
 * el mando al primer cliente que actua y rechaza al resto, para que dos paneles
 * conectados a la vez no manden consignas contradictorias a 20 Hz.
 */
const CLIENT_ID = 'p' + Math.random().toString(36).slice(2, 10);

// Nombre de la unica traccion: la prueba de cualquier rueda mueve todas.
const TRACTION_NAME = 'Tracción (las 6 ruedas)';
