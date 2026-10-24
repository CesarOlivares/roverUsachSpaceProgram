/**
 * Rover 6x6 - Estacion de control en tierra
 *
 * El panel habla por USB (Web Serial) con la estacion base, que reenvia por
 * radio nRF24L01 al rover. Tambien puede enchufarse al USB del rover para
 * pruebas en banco: el protocolo es el mismo.
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
    MOTOR_COUNT: 6,
    STEER_CENTER: 90,           // misma geometria que el firmware (RoverConfig.h)
    STEER_THROW: 45,
    BASE_STATUS_STALE_MS: 1500, // la base informa cada 500 ms; sin noticias, no hay base
};

/*
 * Identidad de este panel, valida mientras dure la pestana. El firmware concede
 * el mando al primer cliente que actua y rechaza al resto, para que dos paneles
 * conectados a la vez no manden consignas contradictorias a 20 Hz.
 */
const CLIENT_ID = 'p' + Math.random().toString(36).slice(2, 10);

const MOTOR_NAMES = [
    'M1 (Delantero Izq)', 'M2 (Delantero Der)',
    'M3 (Central Izq)', 'M4 (Central Der)',
    'M5 (Trasero Izq)', 'M6 (Trasero Der)',
];
